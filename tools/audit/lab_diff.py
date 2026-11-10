"""lab_diff - the Mech Lab's masses: GBL MW2SHELL.EXE's design load FUN_00034b30 executed under unicorn (from the ELF image
Ghidra made of it - not in the repo: GBL_SHELL_ELF, default /home/claude/gbl/re/shellgbl.elf) against the port's
gl_masses (gbl with GBL_LAB_DUMP), for every stock design (*STD MEK record) in a GBL MW2.PRJ.

  python3 tools/audit/lab_diff.py --game <GBL install folder> --bin ./gbl [names...]

The original's globals after the load (1/100 t): 0xabe58 tonnage, 0xabe5c used, 0xabe68 rating, 0xabe6c engine, 0xabe78
gyro, 0xabe7c cockpit, 0xabe80 heat sink type (2 double), 0xabe84 heat sinks beyond 10, 0xabe90 jump jets, 0xabe9c internal,
0xabea8 armour, 0xabeb8 weapons, 0xabec0 ammunition, 0xabec8 equipment - the rows the page prints (0x2fa40..: "%d.%d%d T"
of each, the sinks "%d" / "%d (%d)" of (0xabe84 + 50) / 100 + 10). Stubbed: the PRJ read 0x1ca60 (returns the record) and
its release 0x1cac0."""
import argparse, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gblemu
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID


class ShellEmu(gblemu.Emu):
    """gblemu.Emu over MW2SHELL's image (no sim tables to build)"""
    def __init__(self, path):
        self.img = open(path, 'rb').read()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.segs = []
        phoff, phnum = struct.unpack_from('<I', self.img, 28)[0], struct.unpack_from('<H', self.img, 44)[0]
        for i in range(phnum):
            t, off, va, pa, fs, ms = struct.unpack_from('<6I', self.img, phoff + 32 * i)
            if t != 1: continue
            lo, hi = va & ~0xfff, (va + ms + 0xfff) & ~0xfff
            self.uc.mem_map(lo, hi - lo)
            self.uc.mem_write(va, self.img[off:off + fs])
            self.segs.append((va, va + ms, off))
        self.uc.mem_map(gblemu.STACK_BASE, gblemu.STACK_TOP - gblemu.STACK_BASE)
        self.uc.mem_map(gblemu.HEAP, gblemu.HEAP_SIZE)
        self.uc.mem_map(gblemu.RET_MAGIC, 0x1000)
        self.uc.mem_write(gblemu.RET_MAGIC, b'\xf4' * 0x1000)
        self.err = None; self.stubs = {}; self.writes = []; self.record = False; self.heap_ptr = gblemu.HEAP
        self.uc.hook_add(UC_HOOK_CODE, self._code)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._wr)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._bad)
        # a flat 32-bit GDT: the library's push / pop of es / fs / gs (0x4b7aa) reloads the segment caches, and with
        # unicorn's empty ones the stack would turn 16-bit
        from unicorn.x86_const import (UC_X86_REG_GDTR, UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS,
                                       UC_X86_REG_GS)
        self.uc.mem_map(0x1000, 0x1000)
        self.uc.mem_write(0x1000, struct.pack('<3Q', 0, 0x00cf9a000000ffff, 0x00cf92000000ffff))
        self.uc.reg_write(UC_X86_REG_GDTR, (0, 0x1000, 0x17, 0))
        for r in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS, UC_X86_REG_GS):
            self.uc.reg_write(r, 0x10)
        self.pristine = {va: bytes(self.uc.mem_read(va, hi - va)) for va, hi, _ in self.segs if va >= 0x70000}


def prj_records(path, tag):
    d = open(path, 'rb').read()
    out = {}
    for i in range(struct.unpack_from('<H', d, 24)[0]):
        tg = d[26 + i * 24:30 + i * 24].rstrip(b'\0').decode('latin1')
        io = struct.unpack_from('<I', d, 30 + i * 24)[0]
        if tg != tag: continue
        for k in range(struct.unpack_from('<H', d, io + 16)[0]):
            o, s = struct.unpack_from('<2I', d, io + 22 + k * 8)
            if o and s >= 62: out[d[o + 30:o + 46].split(b'\0')[0].decode('latin1').lower()] = d[o + 62:o + s]
    return out


def dos_masses(emu, meks, name):
    emu.reset_data()
    def load(em):
        b = meks.get(bytes(em.uc.mem_read(em.reg('edx'), 16)).split(b'\0')[0].decode('latin1').lower())
        if b is None: return 0
        a = em.alloc(len(b) + 0x800)
        em.write(a, b)
        return a
    emu.stub(0x1ca60, load)
    emu.stub(0x1cac0, None, 0)
    na = emu.alloc(16)
    emu.write(na, name.encode() + b'\0')
    emu.call(0x34b30, eax=na, limit=50_000_000)
    g = emu.rs32
    e84 = g(0xabe84)
    return {'ton': g(0xabe58), 'used': g(0xabe5c), 'rating': g(0xabe68), 'engine': g(0xabe6c), 'gyro': g(0xabe78),
            'cockpit': g(0xabe7c), 'sinks': e84, 'count': int((e84 + 50) / 100) + 10,   # C idiv (0x2fd70: truncated)
            'dbl': int(g(0xabe80) == 2),
            'jets': g(0xabe90), 'internal': g(0xabe9c), 'armor': g(0xabea8), 'weapons': g(0xabeb8), 'ammo': g(0xabec0),
            'equip': g(0xabec8)}


def port_masses(binary, game, names):
    env = dict(os.environ, MW2_INSTALL_DIR=game, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
               GBL_LAB_DUMP=','.join(names))
    out = subprocess.run([binary], cwd=game, env=env, capture_output=True, text=True, timeout=300).stdout
    res = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) > 2 and f[1] == 'ton':
            res[f[0]] = {f[i]: int(f[i + 1]) for i in range(1, len(f) - 1, 2)}
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True, help='the GBL install folder (MW2.PRJ)')
    ap.add_argument('--bin', default='./gbl')
    ap.add_argument('--elf', default=os.environ.get('GBL_SHELL_ELF', '/home/claude/gbl/re/shellgbl.elf'))
    ap.add_argument('names', nargs='*')
    a = ap.parse_args()
    meks = prj_records(os.path.join(a.game, 'MW2.PRJ'), 'MEK')
    names = a.names or sorted(n for n in meks if n.endswith('std'))
    emu = ShellEmu(a.elf)
    port = port_masses(os.path.abspath(a.bin), a.game, names)
    keys = ('ton', 'used', 'rating', 'engine', 'gyro', 'cockpit', 'sinks', 'count', 'dbl', 'jets', 'internal', 'armor',
            'weapons', 'ammo', 'equip')
    bad = 0
    for n in names:
        try:
            d = dos_masses(emu, meks, n)
        except RuntimeError as ex:
            print('%s: original did not run (%s)' % (n, ex)); bad += 1; continue
        p = port.get(n)
        if not p:
            print('%s: port has no result' % n); bad += 1; continue
        diff = ['%s %d/%d' % (k, d[k], p[k]) for k in keys if d[k] != p[k]]
        if diff:
            bad += 1
            print('%s DIFFERS (original/port): %s' % (n, ', '.join(diff)))
    print('%d designs, %d differ' % (len(names), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
