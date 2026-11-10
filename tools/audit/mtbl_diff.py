#!/usr/bin/env python3
"""mtbl_diff - differential test of the port's mission-table interpreter (src/mtbl.c) against the ORIGINAL runtime of
GBL MW2.EXE, executed under an emulator (unicorn) from the 32-bit ELF image Ghidra made of the LE executable.

What runs of the original: the MTBL loader 0x4dff0 (one call per MTBL chunk of the scene), the mission start 0x16050
for every table (the clock 0: the table's start time and node 0's armed time; its unit placement 0x2b360 stubbed),
then every frame the
per-frame runner 0x170a0 with everything it calls: the node runner 0x16810, activation 0x16740 / 0x166a0, the node
message 0x164f0 (its announce flags) and the star outcome 0x165e0. Stubbed (answered by the test): the per-target tests
0x16120 (destroyed), 0x16190 (visited / inspected), 0x16280 (within), the sound-name lookup 0x34ae0, the radio queue
0x42ed0, the AI order change 0x23db0, sprintf / string helpers inside the message calls. The end prompt branch of
0x170a0 is switched off (DAT_000a8c34 = 1; glview's end sequence is checked separately).

The port side is the same scene's tables in src/mtbl.c through tools/audit/mtbl_shim.c (a shared library), answered by
the same per-target values. Compared after every frame: each node's state (5 ok, 6 failed, else unfinished; the
original's state 8 counted unfinished), its start and end times (whole seconds), the shown flags, every table's current
node, result and result time, and the radio messages of the player's table (node, state) in order.

usage: mtbl_diff.py [--elf mw2gbl.elf] [--lib libmtbl.so] [--seeds N] [--frames N] [--mutate N] SCENE.BWD ...
  --mutate N: N random nodes per run get random kinds / other nodes / delays / M-O-N (RESET, ALLMAIN, TIMER and the
  other kinds no GBL scene uses get exercised too)
  build the library: cc -shared -fPIC -O1 -Isrc tools/audit/mtbl_shim.c src/mtbl.c src/bwd.c src/datapath.c \\
                        src/prj.c -o /tmp/libmtbl.so
The ELF image and the scenes are game data: they are NOT in the repo (pass their paths).
"""
import argparse, ctypes, os, random, struct, sys
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI,
                               UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)

# GBL MW2.EXE addresses (docs/gbl/E_sim.md 1.1; this file's header)
F_LOAD, F_START, F_FRAME = 0x4dff0, 0x16050, 0x170a0
F_DESTROYED, F_VISITED, F_WITHIN = 0x16120, 0x16190, 0x16280
F_MSG, F_OUTCOME = 0x164f0, 0x165e0
STUBS = (0x4bc20, 0x5a8ce, 0x1abe0, 0x15ff0, 0x42ed0, 0x23db0, 0x42b80, 0x11020, 0x2b360)
NODES, TABLE, NODE = 0xb70da, 0x2e8a, 0xf7          # node (t, i) = NODES + t * TABLE + i * NODE
T_COUNT, T_RESULT, T_RTIME = 0xb70a0, 0xb70d8, 0xb70a8
ANNOUNCE, CURRENT, NTABLES, CLOCK = 0xb6fe0, 0xe5940, 0xe5984, 0xa5c44
PLAYER_TABLE, WIN_CHEAT, PLAYER_DEAD, NO_PROMPT = 0xa6724, 0xa5cc4, 0xa669c, 0xa8c34
STACK_BASE, STACK_TOP, HEAP, RET = 0x200000, 0x300000, 0x400000, 0x190000


class Dos:
    """the original runtime under unicorn"""
    def __init__(self, elf):
        img = open(elf, 'rb').read()
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        phoff, phnum = struct.unpack_from('<I', img, 28)[0], struct.unpack_from('<H', img, 44)[0]
        self.data = []
        for k in range(phnum):
            t, off, va, pa, fs, ms = struct.unpack_from('<6I', img, phoff + 32 * k)
            if t != 1: continue
            lo, hi = va & ~0xfff, (va + ms + 0xfff) & ~0xfff
            uc.mem_map(lo, hi - lo)
            uc.mem_write(va, img[off:off + fs])
            if va >= 0xa0000: self.data.append((lo, bytes(uc.mem_read(lo, hi - lo))))
        uc.mem_map(STACK_BASE, STACK_TOP - STACK_BASE)
        uc.mem_map(HEAP, 0x100000)
        uc.mem_map(RET, 0x1000)
        uc.mem_write(RET, b'\xf4' * 0x1000)
        uc.hook_add(UC_HOOK_CODE, self._code)
        self.value = None          # (table, node, k, fn) -> 0/1, set by the test
        self.events = []

    def reset(self):
        for lo, b in self.data: self.uc.mem_write(lo, b)
        self.events = []

    def r8(self, a): return self.uc.mem_read(a, 1)[0]
    def rs16(self, a): return struct.unpack('<h', self.uc.mem_read(a, 2))[0]
    def rs32(self, a): return struct.unpack('<i', self.uc.mem_read(a, 4))[0]
    def w8(self, a, v): self.uc.mem_write(a, bytes([v & 0xff]))
    def w32(self, a, v): self.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def reg(self, r): return self.uc.reg_read(r)

    def _ret(self, eax=None):
        uc = self.uc
        if eax is not None: uc.reg_write(UC_X86_REG_EAX, eax & 0xffffffff)
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, struct.unpack('<I', uc.mem_read(esp, 4))[0])
        uc.reg_write(UC_X86_REG_ESP, esp + 4)

    def _code(self, uc, addr, size, _):
        if addr in (F_DESTROYED, F_VISITED, F_WITHIN):
            off = self.reg(UC_X86_REG_EAX) - NODES - 0xd7    # the target code's address: node + 0xd7 + 2k
            t, rem = divmod(off, TABLE)
            i, b = divmod(rem, NODE)
            self._ret(self.value(t, i, b // 2, addr))        # EDX untouched: 0x16810's loops keep their index in it
        elif addr == F_MSG:   # observed, then run: the message for the player's table once per node index
            t, i, st = self.reg(UC_X86_REG_EAX), self.reg(UC_X86_REG_EDX), self.reg(UC_X86_REG_EBX)
            if t == self.rs32(PLAYER_TABLE) and self.rs32(ANNOUNCE + 4 * i) == 0:
                self.events.append(('msg', t, i, st))
        elif addr == F_OUTCOME:
            t, r = self.reg(UC_X86_REG_EAX), self.reg(UC_X86_REG_EDX)
            if t == self.rs32(PLAYER_TABLE): self.events.append(('outcome', t, r))
        elif addr == 0x34ae0:
            self._ret(1)
        elif addr in STUBS:
            self._ret(0)

    def call(self, addr, eax=0, edx=0, ebx=0, ecx=0):
        uc = self.uc
        esp = STACK_TOP - 0x100
        esp -= 4
        uc.mem_write(esp, struct.pack('<I', RET))
        for r, v in ((UC_X86_REG_EAX, eax), (UC_X86_REG_EDX, edx), (UC_X86_REG_EBX, ebx), (UC_X86_REG_ECX, ecx),
                     (UC_X86_REG_ESI, 0), (UC_X86_REG_EDI, 0), (UC_X86_REG_EBP, 0), (UC_X86_REG_ESP, esp)):
            uc.reg_write(r, v & 0xffffffff)
        uc.emu_start(addr, RET, count=20_000_000)
        if uc.reg_read(UC_X86_REG_EIP) != RET: raise RuntimeError('did not return: eip %#x' % uc.reg_read(UC_X86_REG_EIP))

    def node(self, t, i): return NODES + t * TABLE + i * NODE


def chunks(b):
    off, out = 12, []
    while off + 8 <= len(b):
        tag, sz = b[off:off + 4], struct.unpack_from('<I', b, off + 4)[0]
        if sz < 8: break
        out.append((tag, off, sz))
        off += sz
    return out


TESTS = {1: 'd', 2: 'd', 4: 'd', 8: 'v', 0x20: 'v', 0x100: 'w', 0x2000: 'w'}


ALL_KINDS = (0, 1, 2, 4, 8, 0x10, 0x20, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x10000, 0x20000, 0x40000, 0x80000,
             0x100000, 0x200000, 0x400000)


def mutate(scene, rng, n):
    """a copy of the scene with n random nodes given a random kind (every kind, RESET and ALLMAIN included, which no
    GBL scene uses), other table / node and delay - the same bytes go to both runtimes"""
    b = bytearray(scene)
    tabs = [(off, sz) for tag, off, sz in chunks(scene) if tag == b'MTBL']
    tcount = len(tabs)
    for _ in range(n):
        off, sz = rng.choice(tabs)
        nn = (sz - 0x26) // 0x97
        L = off + 0x5a + 0x97 * rng.randrange(nn)
        struct.pack_into('<I', b, L - 0x34, rng.choice(ALL_KINDS))
        struct.pack_into('<h', b, L + 0x1f, rng.randrange(tcount))
        struct.pack_into('<h', b, L + 0x21, rng.randrange(max(1, nn)))
        struct.pack_into('<i', b, L - 0x0a, rng.choice((-1, 0, 1, 3, 10)))
        b[L - 5] = rng.choice(b'MON')
    return bytes(b)


def run(dos, lib, path, seed, frames, verbose, nmut=0):
    scene = open(path, 'rb').read()
    rng = random.Random(seed)
    if nmut: scene = mutate(scene, random.Random(seed * 7919 + 1), nmut)
    dos.reset()
    heap = HEAP
    tables = []
    for tag, off, sz in chunks(scene):
        if tag != b'MTBL': continue
        c = scene[off:off + sz]
        dos.uc.mem_write(heap, c)
        dos.call(F_LOAD, eax=heap)
        heap += (sz + 15) & ~15
        tables.append(struct.unpack_from('<i', c, 8)[0])
    dos.w32(PLAYER_TABLE, 0); dos.w32(WIN_CHEAT, 0); dos.w32(PLAYER_DEAD, 0); dos.w32(NO_PROMPT, 1)
    dos.w32(CLOCK, 0)
    for t in range(dos.rs32(NTABLES)):
        dos.call(F_START, eax=T_COUNT + t * TABLE, edx=t)
    buf = ctypes.create_string_buffer(scene, len(scene))
    if lib.shim_init(buf, len(scene)) != 0: return ['port: init failed']
    nt = dos.rs32(NTABLES)
    if nt != lib.shim_tables(): return ['table count: original %d port %d' % (nt, lib.shim_tables())]
    # per node: how many targets are attached (0 when the node names none) and their test values over time
    cnt, val, kinds = {}, {}, {}
    for t in range(nt):
        nn = dos.rs32(T_COUNT + t * TABLE)
        if nn != lib.shim_nodes(t): return ['table %d node count: original %d port %d' % (t, nn, lib.shim_nodes(t))]
        for i in range(nn):
            kind = lib.shim_kind(t, i) & 0xffffffff
            kinds[t, i] = kind
            n = (rng.choice((1, 2, 3)) if seed % 3 == 2 else rng.choice((0, 1, 1, 2, 3))) if kind in TESTS else 0
            cnt[t, i] = n
            dos.w8(dos.node(t, i) + 0xd6, n)
            val[t, i] = [0] * n
    dos.value = lambda t, i, k, fn: val[t, i][k] if k < len(val[t, i]) else 0
    ticks = 0
    diffs = []
    p_rate = rng.choice((0.002, 0.01, 0.05))
    for f in range(frames):
        if seed % 3 == 2: ticks += rng.randint(100, 600)   # long frames: past the 25-minute limits within the run
        else: ticks += rng.choice((3, 5, 6, 12)) if rng.random() > 0.02 else rng.randint(100, 2000)
        for (t, i), v in val.items():   # destroyed / visited stay set; within comes and goes
            for k in range(len(v)):
                kind = TESTS.get(kinds[t, i])
                if seed % 3 == 2: v[k] = int(kind == 'w')   # long frames, nothing happens (the player within every
                                                          # radius): the tables run into their time limits
                elif kind == 'w': v[k] ^= rng.random() < p_rate * 4
                elif not v[k]: v[k] = int(rng.random() < p_rate)
            lib.shim_targets(t, i, cnt[t, i], sum(v))
        dos.events = []
        dos.w32(CLOCK, ticks)
        dos.call(F_FRAME)
        nlog0 = lib.shim_nlog()
        lib.shim_step(ticks * 1000 // 182)
        now = ticks // 182
        pmsg = [('msg', lib.shim_log(k, 0), lib.shim_log(k, 1), lib.shim_log(k, 2)) for k in range(nlog0, lib.shim_nlog())]
        dmsg = [e for e in dos.events if e[0] == 'msg']
        if dmsg != pmsg: diffs.append('t%ds frame %d messages: original %s port %s' % (now, f, dmsg, pmsg))
        for t in range(nt):
            for i in range(dos.rs32(T_COUNT + t * TABLE)):
                a = dos.node(t, i)
                ds, ps = dos.r8(a), lib.shim_state(t, i)
                dcls = ds if ds in (5, 6) else 0
                pcls = ps if ps in (5, 6) else 0
                if dcls != pcls: diffs.append('t%ds frame %d node %d.%d (kind %#x) state: original %d port %d' % (now, f, t, i, kinds[t, i], ds, ps))
                dst, pst = dos.rs32(a + 0x4f), lib.shim_started(t, i)
                if dst != (pst // 1000 if pst >= 0 else -1): diffs.append('t%ds frame %d node %d.%d (kind %#x) start: original %d port %d ms' % (now, f, t, i, kinds[t, i], dst, pst))
                den, pen = dos.rs32(a + 0x53), lib.shim_ended(t, i)
                if den != (pen // 1000 if pen >= 0 else -1): diffs.append('t%ds frame %d node %d.%d (kind %#x) end: original %d port %d ms' % (now, f, t, i, kinds[t, i], den, pen))
                if (dos.r8(a + 0x5c) != 0) != (lib.shim_shown(t, i) != 0): diffs.append('t%ds frame %d node %d.%d shown: original %d port %d' % (now, f, t, i, dos.r8(a + 0x5c), lib.shim_shown(t, i)))
            if dos.rs32(CURRENT + 4 * t) != lib.shim_current(t):
                diffs.append('t%ds frame %d table %d current: original %d port %d' % (now, f, t, dos.rs32(CURRENT + 4 * t), lib.shim_current(t)))
            dr, pr = dos.r8(T_RESULT + t * TABLE), lib.shim_result(t)
            if dr != pr: diffs.append('t%ds frame %d table %d result: original %d port %d' % (now, f, t, dr, pr))
            elif dr:
                drt, prt = dos.rs32(T_RTIME + t * TABLE), lib.shim_result_time(t)
                if drt != (prt // 1000 if prt >= 0 else -1): diffs.append('t%ds frame %d table %d result time: original %d port %d ms' % (now, f, t, drt, prt))
        if diffs and not verbose: break
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', default=os.environ.get('GBL_ELF', '/home/claude/gbl/re/mw2gbl.elf'))
    ap.add_argument('--lib', default='/tmp/libmtbl.so')
    ap.add_argument('--seeds', type=int, default=20)
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--mutate', type=int, default=0, help='give N random nodes random kinds (all kinds; see mutate)')
    ap.add_argument('scenes', nargs='+')
    a = ap.parse_args()
    lib = ctypes.CDLL(a.lib)
    lib.shim_init.argtypes = (ctypes.c_char_p, ctypes.c_int)
    dos = Dos(a.elf)
    bad = 0
    for p in a.scenes:
        fails = []
        for seed in range(a.seeds):
            d = run(dos, lib, p, seed, a.frames, a.verbose, a.mutate)
            if d: fails.append((seed, d))
        name = os.path.basename(p)
        if not fails: print('%-14s %d runs x %d frames: identical' % (name, a.seeds, a.frames))
        else:
            bad += 1
            print('%-14s %d of %d runs differ; first: seed %d: %s' % (name, len(fails), a.seeds, fails[0][0], fails[0][1][0]))
            if a.verbose:
                for s, d in fails[:3]:
                    for line in d[:12]: print('    seed %d: %s' % (s, line))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
