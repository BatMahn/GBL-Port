#!/usr/bin/env python3
"""simdiff - differential tests: the original GBL MW2.EXE functions (run under unicorn by gblemu.py from the ELF image
of the LE executable, NOT in the repo: GBL_ELF=/path/mw2gbl.elf) against the port's C (tools/audit/build.sh builds the
shims into SIMDIFF_BUILD, default /tmp/simdiff_build).

  tools/audit/build.sh && python3 tools/audit/simdiff.py [-n cases] [-s seed] [test ...]

Each test drives both sides with the same random / edge inputs and prints PASS / FAIL with the first differences.
A test marked 'expect' documents a known, deliberate difference (and checks its size)."""
import ctypes, os, random, sys, struct, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gblemu

BUILD = os.environ.get('SIMDIFF_BUILD', '/tmp/simdiff_build')
TESTS = {}
def test(fn): TESTS[fn.__name__] = fn; return fn

_libs = {}
def lib(name):
    if name not in _libs: _libs[name] = ctypes.CDLL(os.path.join(BUILD, 'libshim_%s.so' % name))
    return _libs[name]

class Report:
    def __init__(self, name): self.name, self.n, self.bad, self.notes = name, 0, [], []
    def check(self, ok, what):
        self.n += 1
        if not ok and len(self.bad) < 12: self.bad.append(what)
        elif not ok: self.bad.append(None)
    def done(self):
        st = 'PASS' if not self.bad else 'FAIL'
        print('%-28s %s  %d checks, %d differ' % (self.name, st, self.n, len(self.bad)))
        for b in self.bad:
            if b: print('    ', b)
        for n in self.notes: print('     note:', n)
        return not self.bad

# ---------------------------------------------------------------------------------------------------------------------
# GBL globals (VA)
NOW, TICKS, PLAYER, DIFPTR = 0xa5c44, 0xa5c48, 0xa5c98, 0xa5c94

class ShimHeat(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ('heat16', 'sink16', 'ticks', 'add16', 'shutdown', 'pending', 'override',
                'latch', 'warn_latch', 'destroyed', 'msg_warn', 'msg_seq', 'msg_down', 'restarted')] + \
               [('seq_ticks', ctypes.c_float), ('heat', ctypes.c_float)]

@test
def heat_state(rep, rnd, n):
    """GBL 0x252b0 (the per-frame heat: sinking, warning, shutdown sequence, shutdown, restart) vs combat_tick, for the
    player, with the override path's roll kept out (heat_override_roll checks it): random frames of 1-20 whole ticks and
    random heat arriving, override pressed now and then while a sequence is pending."""
    e = gblemu.Emu(); L = lib('combat')
    L.shim_unit_new.restype = ctypes.c_void_p
    u = ctypes.c_void_p(L.shim_unit_new())
    for case in range(n):
        e.reset_data()
        calls = []
        e.stub(0x42e40, lambda em: calls.append(('msg', em.reg('eax'))) or 0)
        e.stub(0x41740, lambda em: calls.append(('snd', em.reg('eax'))) or 0, pops=4)
        e.stub(0x4af60, lambda em: calls.append(('rand', em.reg('eax'))) or 1)   # the override roll: stop the case
        e.stub(0x251e0, lambda em: calls.append(('blow', 0)) or 0)
        sink = rnd.choice([50, 70, 100, 45]) * rnd.randint(10, 30)
        dif = e.alloc(8); e.write(dif, bytes([0, 0, 1, 0, 1, 1, 0, 0]))
        obj = e.alloc(0x80); e.w32(obj + 4, 0); e.w16(obj + 0x14, 0)
        c = e.alloc(0x200); e.w32(c, obj); e.w32(c + 0xa0, sink); e.w32(c + 0xa4, 2)
        e.w32(DIFPTR, dif); e.w32(PLAYER, 0); e.w32(0xa6670, 0); e.w32(0x10ce80, 0)
        now = 1000; e.w32(NOW, now)
        L.shim_heat_setup(u, sink, 1, case + 1)
        h = ShimHeat()
        for f in range(400):
            t = rnd.choice([1, 2, 3, 4, 5, 6, 8, 12, 20])
            add = rnd.choice([0, 0, 0, rnd.randint(0, 30) << 16, rnd.randint(0, 0x8000)])
            ovr = 0
            flags = e.r16(c + 0x110)
            if (flags & 4) and not (flags & 8) and e.rs32(c + 0xa4) != 3 and rnd.random() < 0.03: ovr = 1
            heat_before = e.rs32(c + 0x9c)
            if heat_before >> 16 > 100 and (flags & 8): ovr = 0   # keep out of the roll path
            now += t
            e.w32(NOW, now); e.w32(TICKS, t)
            e.w32(c + 0x94, 0); e.w32(c + 0x98, add)
            if ovr: e.w16(c + 0x110, flags | 8)
            calls.clear()
            e.call(0x252b0, eax=c)
            h.ticks, h.add16, h.override = t, add, 1 if (e.r16(c + 0x110) & 8 or ovr) else int(h.override)
            h.override = 1 if ovr else h.override
            L.shim_heat_frame(u, ctypes.byref(h))
            st = e.rs32(c + 0xa4); fl = e.r16(c + 0x110)
            dos = dict(heat=e.rs32(c + 0x9c) / 65536.0, shutdown=int(st == 3), pending=int(bool(fl & 4)),
                       override=int(bool(fl & 8)), latch=int(bool(fl & 0x1000)), warn_latch=e.r32(0xa6670),
                       msg_warn=int(('msg', 0) in calls), msg_seq=int(('msg', 3) in calls), msg_down=int(('msg', 0xd) in calls),
                       restarted=int(st == 0), blow=int(('blow', 0) in calls))
            prt = dict(heat=h.heat, shutdown=h.shutdown, pending=h.pending, override=h.override, latch=h.latch,
                       warn_latch=h.warn_latch, msg_warn=h.msg_warn, msg_seq=h.msg_seq, msg_down=h.msg_down,
                       restarted=h.restarted, blow=h.destroyed)
            if any(x[0] == 'rand' for x in calls): break   # the roll path (heat_override_roll)
            diff = [k for k in dos if (abs(dos[k] - prt[k]) > 0.01 if k == 'heat' else dos[k] != prt[k])]
            rep.check(not diff, 'case %d frame %d t=%d add=%.2f: %s' % (case, f, t, add / 65536.0,
                      ', '.join('%s dos %s port %s' % (k, dos[k], prt[k]) for k in diff)))
            if diff:  # resync the port to DOS and go on
                break
            if st == 0:   # the restart: DOS goes through the start-up (0x27b05 case 0); here up at once on both sides
                e.w32(c + 0xa4, 2); e.w32(c + 0x8c, now); L.shim_set_seq(u, ctypes.c_float(0))
            if dos['blow']: break
    L.shim_unit_free(u)

@test
def heat_sink_tiers(rep, rnd, n):
    """GBL 0x4dc8d..0x4dd75 (the loader's dissipation per heat sinking point by player / difficulty / temperature) vs
    combat_sink_rate and the unit's rate as combat_init + combat_set_player_difficulty set it"""
    e = gblemu.Emu(); L = lib('combat'); L.shim_unit_sink.restype = ctypes.c_float
    temps = [-200, -101, -100, -99, -31, -30, -29, 0, 49, 50, 51, 120] + [rnd.randint(-150, 150) for _ in range(n)]
    for player in (0, 1):
        for dif in (0, 1, 2, 3):
            for t in temps:
                e.reset_data()
                d = e.alloc(8); e.write(d, bytes([0, 0, 1, 1, 1, dif, 0, 0]))
                obj = e.alloc(0x40); e.w32(obj + 4, 0 if player else 5)
                c = e.alloc(0x100); e.w32(c, obj)
                e.w32(DIFPTR, d); e.w32(PLAYER, 0); e.w32(0xa5bf8, t)
                e.run(0x4dc8d, 0x4dd75, edx=c)
                dos = gblemu.s32(e.reg('ebx'))
                prt = L.shim_sink_rate(player, dif, t)
                rep.check(dos == prt, 'player %d difficulty %d temp %d: dos %d port %d' % (player, dif, t, dos, prt))
                hs = rnd.randint(10, 40)
                u = L.shim_unit_sink(hs, player, dif, t)
                rep.check(abs(u * 65536 - dos * hs) < 0.02 * hs, 'unit rate player %d dif %d temp %d x%d: dos %d port %.2f'
                          % (player, dif, t, hs, dos * hs, u * 65536))

@test
def heat_override_roll(rep, rnd, n):
    """GBL 0x25430..0x2545c: an overridden unit above 100 heat rolls rand(500 x frame ticks) == 0 each frame (chance
    1 / (500 t) a frame). The port takes the rule at its 30 fps reference (6 ticks a frame): 1 / 18000 a tick. Checks the
    DOS argument for t = 1..20 and the port's mean time to a cook-off against DOS at 6-tick frames (expect: within 15 %)."""
    e = gblemu.Emu(); L = lib('combat')
    for t in range(1, 21):
        e.reset_data()
        args = []
        e.stub(0x4af60, lambda em: args.append(em.reg('eax')) or 1)
        e.stub(0x251e0, lambda em: 0); e.stub(0x42e40, lambda em: 0); e.stub(0x41740, lambda em: 0, pops=4)
        d = e.alloc(8); e.write(d, bytes([0, 0, 1, 1, 1, 1, 0, 0]))
        obj = e.alloc(0x40); c = e.alloc(0x200); e.w32(c, obj); e.w32(c + 0xa4, 2)
        e.w32(c + 0x9c, 105 << 16); e.w16(c + 0x110, 4 | 8); e.w32(c + 0x8c, 0)
        e.w32(DIFPTR, d); e.w32(PLAYER, 0); e.w32(NOW, 100); e.w32(TICKS, t)
        e.call(0x252b0, eax=c)
        rep.check(args == [500 * t], 'ticks %d: rand args %s' % (t, args))
    # the port: mean seconds to the cook-off at 3 / 6 / 12-tick frames vs DOS's 1/(500 t) a frame
    for t in (3, 6, 12):
        fr = [L.shim_override_frames(t, k + 1, 10 ** 7) for k in range(max(n, 200))]
        mean_s = sum(fr) / len(fr) * t / 182.0
        dos_s = 500.0 * t * t / 182.0
        rep.notes.append('frames of %d ticks: port mean %.0f s, DOS at that frame length %.0f s' % (t, mean_s, dos_s))
        if t == 6: rep.check(abs(mean_s / dos_s - 1) < 0.15, 'port mean %.1f s vs DOS %.1f s at 6-tick frames' % (mean_s, dos_s))

@test
def ai_nav_turn(rep, rnd, n):
    """GBL 0x22ce0 (the nav steering: the leg-turn demand from the 16.16 heading error) vs ai_nav_turn"""
    e = gblemu.Emu(); L = lib('ai'); L.shim_nav_turn.restype = ctypes.c_float; L.shim_nav_turn.argtypes = [ctypes.c_float]
    errs = [0, 1, -1, 0xffff, -0xffff, 0x10000, -0x10000, 0x2d0000, 0x2e0000, -0x2d0000, -0x2e0000, 0xb40000, -0xb40000,
            0xb40001, -0xb40001] + [rnd.randint(-0xb40000, 0xb40000) for _ in range(n * 20)]
    c = e.alloc(0x100); inp = e.alloc(0x20); e.w32(c + 0x4c, inp)
    for err in errs:
        e.w32(c + 0xd4, err); e.w32(c + 0x60, 0)
        e.call(0x22ce0, eax=c)
        dos = gblemu.s32(e.r32(inp + 0xc)) / 65536.0
        w = err - 0x1680000 if err > 0xb40000 else err + 0x1680000 if err < -0xb40000 else err   # the callers' wrap180
        prt = L.shim_nav_turn(w / 65536.0)
        rep.check(abs(dos - prt) < 1e-3, 'err %#x (%.4f deg): dos %.5f port %.5f' % (err & 0xffffffff, err / 65536.0, dos, prt))

@test
def patrol_sweep(rep, rnd, n):
    """GBL 0x24a30 (the patrol torso sweep of states 5-8: 5 degrees every 0x40 / 0x20 ticks, turning at 45, the due time
    pulled in when the interval shortens) over runs of frames vs ai.c patrol_sweep"""
    e = gblemu.Emu(); L = lib('ai')
    F = ctypes.c_float
    for run in range(n):
        frames = 300
        ticks = [rnd.choice([1, 3, 6, 6, 6, 9, 12, 30]) for _ in range(frames)]
        thr = [rnd.choice([0, 0, 0x333, 0x400]) if rnd.random() < 0.1 or k == 0 else None for k in range(frames)]
        for k in range(1, frames):
            if thr[k] is None: thr[k] = thr[k - 1]
        torso0 = rnd.choice([0, 5, -45, 45, 40]) * 0x10000
        dir0 = rnd.randint(0, 1)
        now = 1000; due = now + rnd.randint(-20, 64)
        u = e.alloc(0x200); inp = e.alloc(0x40)
        e.w32(u, 1); e.w32(u + 0x4c, inp); e.w32(inp + 4, torso0); e.w32(u + 0x176, due); e.w32(u + 0x17e, dir0)
        dos = []
        for k in range(frames):
            now += ticks[k]
            e.w32(0xa5c44, now); e.w32(inp + 8, thr[k])
            e.call(0x24a30, eax=u)
            dos.append(gblemu.s32(e.r32(inp + 4)) / 65536.0)
        out = (F * frames)()
        L.shim_patrol_seq(frames, (F * frames)(*ticks), (F * frames)(*[t / 1024.0 for t in thr]), out,
                          1 if dir0 else -1, F(torso0 / 65536.0), F(float(due - 1000)))
        bad = [k for k in range(frames) if abs(out[k] - dos[k]) > 1e-3]
        rep.check(not bad, 'run %d: first differing frame %s dos %s port %s' %
                  (run, bad[0] if bad else '-', dos[bad[0]] if bad else '-', out[bad[0]] if bad else '-'))

@test
def leg_turn_demand(rep, rnd, n):
    """GBL 0x28054..0x280f0 (the leg turn demand c[0xe] walking: speed / 10000, the spot-turn rule for computer-run units
    and the immobile player, cos(min(80, kv)), airborne) vs msim_leg_demand"""
    e = gblemu.Emu(); L = lib('msim')
    L.shim_leg_demand.restype = ctypes.c_float
    L.shim_leg_demand.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.c_float]
    spds = [0, 1, 9999, 10000, 10001, -9999, -10000, -10001, 19999, 80 * 10000, 81 * 10000, 200 * 10000, -300000]
    for k in range(n * 40):
        spd = rnd.choice(spds) if k < len(spds) * 6 else rnd.randint(-400000, 900000)
        inp = rnd.choice([0, 0x3333333, -0x3333333, 0x4000000, -0x4000000, 1023, -1023, 1024, -1025, rnd.randint(-0x4000000, 0x4000000)])
        player = rnd.randint(0, 1); gain = rnd.choice([0, 1, 0x6666, 0x10000]); air = int(rnd.random() < 0.15)
        ap = rnd.randint(0, 1); prev = rnd.randint(-0x500000, 0x500000)
        obj = e.alloc(0x100); ib = e.alloc(0x50); c = e.alloc(0x200)
        e.w32(c, obj); e.w32(obj + 0x4c, ib); e.w32(ib + 0xc, inp); e.w8(obj + 0x80, 4 if air else 0)
        e.w32(c + 0x2c, spd); e.w32(c + 0xb4, gain); e.w32(c + 0xc0, ap); e.w32(c + 0x38, prev)
        ebp = gblemu.STACK_TOP - 0x800
        e.w32(ebp - 4, player)
        e.run(0x28054, 0x280f0, esi=c, ebp=ebp)
        dos = gblemu.s32(e.r32(c + 0x38)) / 65536.0
        prt = L.shim_leg_demand(inp / 65536.0, spd / 65536.0 * 182.0, player, gain / 65536.0, air, ap, prev / 65536.0)
        rep.check(abs(dos - prt) < 0.01, 'spd %d input %.2f player %d gain %#x air %d ap %d: dos %.4f port %.4f'
                  % (spd, inp / 65536.0, player, gain, air, ap, dos, prt))
        if k % 50 == 49: e.reset_data()

@test
def servo_step(rep, rnd, n):
    """GBL 0x4a9d0 (the integer servo every controller servo uses: value += (target - value) x elapsed / T, idiv, the
    target once reached or passed) vs the port's float drive_servo (msim_servo) over random frame sequences; T 36 / 54 /
    91 / 109 ticks (0x4a990 for 0.2 / 0.3 s, the speed servo's 0x5b, 0.6 s). Tolerance: the integer step's truncation
    (T / ticks units of 1/65536 a frame)"""
    e = gblemu.Emu(); L = lib('msim')
    L.shim_servo.restype = ctypes.c_float; L.shim_servo.argtypes = [ctypes.c_float] * 4
    for case in range(n * 4):
        T = rnd.choice([36, 54, 91, 109])
        sv = e.alloc(16); now = 1000
        v0 = rnd.randint(-0x5a0000, 0x5a0000)
        e.w32(sv, now); e.w32(sv + 8, v0); e.w32(sv + 12, T)
        pv = v0 / 65536.0
        worst = 0
        for f in range(60):
            if f % 15 == 0: tgt = rnd.randint(-0x5a0000, 0x5a0000); e.w32(sv + 4, tgt)
            t = rnd.choice([1, 3, 4, 6, 9, 14, 20])
            now += t; e.w32(NOW, now)
            e.call(0x4a9d0, eax=sv)
            dv = e.rs32(sv + 8) / 65536.0
            pv = L.shim_servo(pv, tgt / 65536.0, float(T), float(t))
            worst = max(worst, abs(dv - pv))
        rep.check(worst < 0.01, 'T %d: worst |dos - port| %.5f (16.16 units %.0f)' % (T, worst, worst * 65536))
        e.reset_data()

@test
def turret_twist(rep, rnd, n):
    """GBL 0x2f193..0x2f1fa (a turret's twist demand, limit >= 360: the servo's value taken round to within 180 degrees of
    the demand) + the servo step 0x4a9d0 (T 145, 0x2eed0's 0.8 s) vs msim_twist_round, demands crossing behind (+-180)"""
    e = gblemu.Emu(); L = lib('msim')
    L.shim_twist_round.restype = ctypes.c_float; L.shim_twist_round.argtypes = [ctypes.c_float] * 4
    for case in range(n * 10):
        obj = e.alloc(0x100); ib = e.alloc(0x50); c = e.alloc(0x200)
        e.w32(c, obj); e.w32(obj + 0x4c, ib); e.w32(c + 0xe4, 0x1680000); e.w32(c + 0x10, 145)
        cur = rnd.randint(-0xb40000, 0xb40000); now = 5000
        e.w32(c + 0xc, cur); e.w32(c + 4, now)
        pc = cur / 65536.0
        for f in range(20):
            dem = rnd.choice([0xb30000, -0xb30000, 0xb40000, -0xb40000, rnd.randint(-0xb40000, 0xb40000)])
            e.w32(ib + 4, dem); e.w32(ib, 0)
            e.run(0x2f193, 0x2f1fa, edx=c)
            t = rnd.choice([3, 4, 6, 12]); now += t; e.w32(NOW, now)
            e.call(0x4a9d0, eax=c + 4)
            dv = gblemu.s32(e.r32(c + 0xc)) / 65536.0
            pc = L.shim_twist_round(pc, dem / 65536.0, 145.0, float(t))
            d = (dv - pc + 180.0) % 360.0 - 180.0
            rep.check(abs(d) < 0.02, 'case %d frame %d demand %.2f: dos %.3f port %.3f' % (case, f, dem / 65536.0, dv, pc))
        if case % 20 == 19: e.reset_data()

@test
def collision_damage(rep, rnd, n):
    """GBL 0x20ca0 (a wall: the unit's own velocity) and 0x20ae0 (a unit: the difference of the two velocities) -> 0x20b80
    (the location by the contact normal and bearing, the head x 4 x tonnage ratio) vs combat.c collide() fed the DOS length
    of the same velocity (the callers' msim_dos_len); damage per location (0x265e0 stubbed)"""
    e = gblemu.Emu(); L = lib('combat')
    L.shim_collide.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
    out = (ctypes.c_float * 8)()
    def dlen(x, y, z):
        a = sorted([abs(x), abs(y), abs(z)], reverse=True)
        return (a[0] * 4 + a[1] + a[2]) >> 2
    for k in range(n * 30):
        hits = []
        e.stub(0x265e0, lambda em: hits.append((gblemu.s32(em.reg('ebx')), gblemu.s32(em.reg('edx')))) or 0)
        e.stub(0x52cd0, lambda em: 0, pops=12)
        d = e.alloc(8); e.write(d, bytes([0, 0, 1, 1, 1, 1, 0, 0])); e.w32(DIFPTR, d)
        obj = e.alloc(0x100); c = e.alloc(0x200); e.w32(c, obj)
        other = e.alloc(0x200)
        own_t, oth_t = rnd.choice([20, 35, 50, 75, 100]), rnd.choice([20, 35, 50, 75, 100])
        e.w32(c + 0xe8, own_t); e.w32(other + 0xe8, oth_t)
        v = [rnd.randint(-0x1800000 // 16, 0x1800000 // 16) for _ in range(3)]
        ov = [rnd.randint(-0x100000, 0x100000) for _ in range(3)] if rnd.random() < 0.5 else [0, 0, 0]
        for j in range(3): e.w32(c + 0x104 + 4 * j, v[j]); e.w32(other + 0x104 + 4 * j, ov[j])
        heading = rnd.randint(-0xb40000, 0xb40000); twist = rnd.randint(-0x3c0000, 0x3c0000)
        e.w32(obj + 0x60, heading); e.w32(obj + 0x6c, twist)
        ang = rnd.uniform(-math.pi, math.pi); ny = rnd.choice([-0.95, -0.87, -0.8, 0.0, 0.3, 0.7, 0.72, 0.95, rnd.uniform(-1, 1)])
        hl = math.sqrt(max(0.0, 1 - ny * ny))
        nx, nz = hl * math.sin(ang), hl * math.cos(ang)
        e.w32(0xa5efc, int(nx * 65536)); e.w32(0xa5f00, int(ny * 65536)); e.w32(0xa5f04, int(nz * 65536))
        unit = rnd.random() < 0.5
        if unit: e.call(0x20ae0, eax=c, edx=other)
        else:
            wall = e.alloc(0x40); e.call(0x20ca0, eax=c, edx=wall)
        dos = [0.0] * 8
        for loc, dmg in hits: dos[loc - 1] += dmg / 65536.0
        S = dlen(v[0] - ov[0], v[1] - ov[1], v[2] - ov[2]) if unit else dlen(*v)
        # the bearing as 0x20b80 computes it: atan2 of the negated normal, off heading + twist (16.16 degrees)
        rel = math.degrees(math.atan2(-int(nx * 65536), -int(nz * 65536))) - (heading + twist) / 65536.0
        while rel > 180: rel -= 360
        while rel < -180: rel += 360
        L.shim_collide(S / 65536.0, int(ny * 65536) / 65536.0, rel, oth_t if unit else 0, own_t, out)
        prt = list(out)
        ok = all(abs(a - b) < 0.02 + 0.002 * abs(a) for a, b in zip(dos, prt))
        rep.check(ok, '%s S %.3f ny %.3f rel %.1f tons %d/%d: dos %s port %s' % ('unit' if unit else 'wall', S / 65536.0, ny, rel,
                  oth_t, own_t, [round(x, 3) for x in dos], [round(x, 3) for x in prt]))
        if k % 50 == 49: e.reset_data()

@test
def manoeuvre_records(rep, rnd, n):
    """GBL 0x2c970 (a unit's manoeuvre record: a profile's, else &0xa6e38[class - 1], the jumper table for a 1-ton mech;
    {u16 rows, u16 first-pick range, rows of 9 shorts}) run on units of every class / tonnage / GPS skill, the record's
    rows read from the image, vs the record ai_gbl_profile + ai_set_class give"""
    e = gblemu.Emu(); L = lib('ai')
    buf = (ctypes.c_short * (13 * 9))(); first = ctypes.c_int()
    profiles = {100: 0xa6e80, 101: 0xa6e88, 102: 0xa6e90, 103: 0xa6e98}
    for cls in range(1, 9):
        for tons in (1, 20, 100):
            for skill in (0, 3, 100, 101, 102, 103):
                e.reset_data()
                u = e.alloc(0x200); c = e.alloc(0x200)
                e.w32(u, cls); e.w32(u + 0x20, c); e.w32(c + 0xe8, tons); e.w8(u + 0x155, 2)
                if skill in profiles: e.w32(u + 0x1ac, profiles[skill])   # 0x217b0 stores the profile's record first
                e.call(0x2c970, eax=u)
                rec = e.r32(u + 0x1ac)
                rows, fp, ptr = e.rs16(rec), e.rs16(rec + 2), e.r32(rec + 4)
                dos = [[e.rs16(ptr + r * 0x12 + 2 * k) for k in range(9)] for r in range(rows)]
                nr = L.shim_man_record(cls, tons, skill, buf, ctypes.byref(first))
                prt = [[buf[r * 9 + k] for k in range(9)] for r in range(nr)]
                rep.check(rows == nr and fp == first.value and dos == prt,
                          'class %d tons %d skill %d: dos %d rows / %d %s, port %d rows / %d %s' % (cls, tons, skill, rows, fp,
                          dos[:2], nr, first.value, prt[:2]))

@test
def seg_poly(rep, rnd, n):
    """GBL 0x1c9f0 (a segment against one face: the plane crossing in 2.29 x cm integers, t = (-f << 16) / (n . d) on the
    octagonal-length direction 0x1dbb0, the crossing point, then 0x1cbc0 -> 0x1cc50 / 0x1cfb0 / 0x1ce00 point in polygon
    on the plane of the normal's largest component, and the segment cut there, 0x1de20) vs eseg_poly + cpoly_in (msim.c,
    floats): random convex faces (3-6 integer vertices round a centre, any orientation) and segments crossing near them.
    Hit / miss must agree, the cut point within 2 cm + 3.5 cm / cos (the DOS plane distance's rounding along a grazing segment); a case whose crossing lies within 2 cm of an edge (either side's
    rounding can decide it) is counted, not judged."""
    e = gblemu.Emu(); L = lib('msim')
    F3 = ctypes.c_float * 3
    edge = 0
    for case in range(n * 40):
        e.reset_data()
        nv = rnd.randint(3, 6)
        # the face's plane: a random unit normal, a centre, two axes in the plane
        while True:
            nx, ny, nz = rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)
            if rnd.random() < 0.3: (nx, ny, nz) = rnd.choice([(0, 1, 0), (1, 0, 0), (0, 0, 1), (0, -1, 0), (1, 1, 0), (0, 1, 1)])
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            if l > 1e-3: break
        nrm = (nx / l, ny / l, nz / l)
        ax = (1, 0, 0) if abs(nrm[0]) < 0.9 else (0, 1, 0)
        u = (nrm[1] * ax[2] - nrm[2] * ax[1], nrm[2] * ax[0] - nrm[0] * ax[2], nrm[0] * ax[1] - nrm[1] * ax[0])
        lu = math.sqrt(sum(c * c for c in u)); u = tuple(c / lu for c in u)
        w = (nrm[1] * u[2] - nrm[2] * u[1], nrm[2] * u[0] - nrm[0] * u[2], nrm[0] * u[1] - nrm[1] * u[0])
        cen = [rnd.randint(-30000, 30000) for _ in range(3)]
        R = rnd.choice([200, 1000, 5000, 20000])
        angs = sorted(rnd.uniform(0, 2 * math.pi) for _ in range(nv))
        if rnd.random() < 0.5: angs.reverse()
        verts = [[int(round(cen[k] + R * (math.cos(a) * u[k] + math.sin(a) * w[k]))) for k in range(3)] for a in angs]
        # DOS records: the face (+2 count, +4 offset of the index bytes, +0x14 normal 2.29), the vertices (0x2c each,
        # world x / y / z at +0xc), the segment (0x38: start, end, delta, direction 16.16, length, +0x34 state)
        N = [int(round(c * (1 << 29))) for c in nrm]
        poly = e.alloc(0x60); vb = e.alloc(0x2c * nv); seg = e.alloc(0x40)
        e.w16(poly + 2, nv); e.w32(poly + 4, 0x40)
        for k in range(3): e.w32(poly + 0x14 + 4 * k, N[k])
        for j in range(nv):
            e.w8(poly + 0x40 + j, j)
            for k in range(3): e.w32(vb + 0x2c * j + 0xc + 4 * k, verts[j][k])
        # a segment from in front of the plane to behind it (now and then parallel, from behind, or short of it)
        a, b = rnd.uniform(0, 1.3), rnd.uniform(0, 1.3)
        q = [cen[k] + R * (a * math.cos(b * 6.28) * u[k] + a * math.sin(b * 6.28) * w[k]) for k in range(3)]
        d0 = rnd.choice([rnd.uniform(1, 3000), rnd.uniform(1, 50), rnd.uniform(-500, -1)])
        d1 = rnd.choice([rnd.uniform(-3000, -1), rnd.uniform(1, 400), -rnd.uniform(0, 30)])
        tilt = [rnd.uniform(-1500, 1500) for _ in range(3)]
        p0 = [int(round(q[k] + nrm[k] * d0 + tilt[k])) for k in range(3)]
        p1 = [int(round(q[k] + nrm[k] * d1 - tilt[k] * rnd.uniform(0, 1))) for k in range(3)]
        if p0 == p1: continue
        for k in range(3):
            e.w32(seg + 4 * k, p0[k]); e.w32(seg + 12 + 4 * k, p1[k]); e.w32(seg + 24 + 4 * k, p1[k] - p0[k])
        e.w32(seg + 0x34, 0)
        e.call(0x1dbb0, eax=seg)
        if e.rs32(seg + 0x30) <= 0: continue
        dh = e.call(0x1c9f0, eax=poly, edx=vb, ebx=seg) != 0
        dpt = [e.rs32(seg + 12 + 4 * k) for k in range(3)]
        out = F3()
        vv = (ctypes.c_float * (3 * nv))(*[float(c) for vtx in verts for c in vtx])
        ph = L.shim_seg_poly(vv, nv, F3(*nrm), F3(*map(float, p0)), F3(*map(float, p1)), out) != 0
        # how near the plane crossing is to the face's edges (in the plane), for the edge class
        dn = sum(nrm[k] * (p1[k] - p0[k]) for k in range(3))
        near = False
        if abs(dn) > 1e-9:
            t = sum(nrm[k] * (verts[0][k] - p0[k]) for k in range(3)) / dn
            X = [p0[k] + (p1[k] - p0[k]) * t for k in range(3)]
            for j in range(nv):
                A, B = verts[j], verts[(j + 1) % nv]
                AB = [B[k] - A[k] for k in range(3)]; AX = [X[k] - A[k] for k in range(3)]
                L2 = sum(c * c for c in AB) or 1
                s = max(0, min(1, sum(AB[k] * AX[k] for k in range(3)) / L2))
                if math.sqrt(sum((AX[k] - AB[k] * s) ** 2 for k in range(3))) < 2.0: near = True
            if abs(t) * math.sqrt(sum((p1[k] - p0[k]) ** 2 for k in range(3))) < 2.0 or abs(1 - t) * math.sqrt(sum((p1[k] - p0[k]) ** 2 for k in range(3))) < 2.0: near = True
            if abs(sum(nrm[k] * (p0[k] - verts[0][k]) for k in range(3))) < 2.0: near = True
        if dh != ph and near: edge += 1; continue
        # the DOS plane distance is a sum of six rounded 2.29 x cm products (up to 3 cm) and t a whole number: along the
        # segment that is up to ~3 cm / cos(angle to the normal) + 1
        sl = math.sqrt(sum((p1[k] - p0[k]) ** 2 for k in range(3)))
        cs = abs(dn) / sl if sl else 1
        tol = 2.0 + 3.5 / max(cs, 1e-3)
        ok = dh == ph and (not dh or max(abs(dpt[k] - out[k]) for k in range(3)) <= tol)
        rep.check(ok, 'case %d nv %d R %d: dos %s %s, port %s %s (p0 %s p1 %s)' % (case, nv, R, dh, dpt if dh else '',
                  ph, [round(c, 1) for c in out] if ph else '', p0, p1))
    rep.notes.append('%d cases within 2 cm of an edge / the plane / an end disagreed (not judged)' % edge)
    # edge inputs: axis-aligned faces (a wall, a floor) with integer corners, hit straight on at a corner, on an edge, just
    # inside / outside them (the DOS crossing rules count a point on an edge or a corner as inside)
    for case in range(n * 20):
        e.reset_data()
        axis = rnd.randint(0, 2); sgn = rnd.choice([1, -1])
        a1, a2 = [k for k in range(3) if k != axis]
        x0, z0 = rnd.randint(-5000, 5000), rnd.randint(-5000, 5000)
        wdt, hgt = rnd.randint(1, 3000), rnd.randint(1, 3000)
        lvl = rnd.randint(-3000, 3000)
        corners = [(x0, z0), (x0 + wdt, z0), (x0 + wdt, z0 + hgt), (x0, z0 + hgt)]
        if rnd.random() < 0.5: corners = [(x0, z0), (x0 + wdt, z0 + hgt // 2), (x0, z0 + hgt)]
        verts = []
        for (c1, c2) in corners:
            v = [0, 0, 0]; v[axis] = lvl; v[a1] = c1; v[a2] = c2; verts.append(v)
        nv = len(verts)
        nrm = [0.0, 0.0, 0.0]; nrm[axis] = float(sgn)
        pick = rnd.choice(['corner', 'edge', 'in1', 'out1', 'mid'])
        j = rnd.randrange(nv); A, B = verts[j], verts[(j + 1) % nv]
        if pick == 'corner': h = list(A)
        elif pick == 'mid': h = [sum(v[k] for v in verts) // nv for k in range(3)]
        else:
            f = rnd.uniform(0, 1); h = [int(round(A[k] + (B[k] - A[k]) * f)) for k in range(3)]
            if pick != 'edge':
                c = [sum(v[k] for v in verts) / nv for k in range(3)]
                for k in (a1, a2):
                    dd = 1 if c[k] > h[k] else -1 if c[k] < h[k] else 0
                    h[k] += dd if pick == 'in1' else -dd
        d0, d1 = rnd.randint(1, 2000), rnd.randint(1, 2000)
        p0 = list(h); p0[axis] = lvl + sgn * d0
        p1 = list(h); p1[axis] = lvl - sgn * d1
        N = [int(round(c * (1 << 29))) for c in nrm]
        poly = e.alloc(0x60); vb = e.alloc(0x2c * nv); seg = e.alloc(0x40)
        e.w16(poly + 2, nv); e.w32(poly + 4, 0x40)
        for k in range(3): e.w32(poly + 0x14 + 4 * k, N[k])
        for jj in range(nv):
            e.w8(poly + 0x40 + jj, jj)
            for k in range(3): e.w32(vb + 0x2c * jj + 0xc + 4 * k, verts[jj][k])
        for k in range(3):
            e.w32(seg + 4 * k, p0[k]); e.w32(seg + 12 + 4 * k, p1[k]); e.w32(seg + 24 + 4 * k, p1[k] - p0[k])
        e.w32(seg + 0x34, 0)
        e.call(0x1dbb0, eax=seg)
        dh = e.call(0x1c9f0, eax=poly, edx=vb, ebx=seg) != 0
        out = F3()
        vv = (ctypes.c_float * (3 * nv))(*[float(c) for vtx in verts for c in vtx])
        ph = L.shim_seg_poly(vv, nv, F3(*nrm), F3(*map(float, p0)), F3(*map(float, p1)), out) != 0
        rep.check(dh == ph, 'edge case %d (%s, axis %d): dos %s port %s, face %s, hit at %s' % (case, pick, axis, dh, ph, verts, h))

@test
def slot_point(rep, rnd, n):
    """GBL 0x2d350 (a point d cm out in a 16-way slot of a unit: (d / 0xa6804[k], d / 0xa6808[k]) through the unit's object
    matrix 0x3b98c, 2.29 fixed with the shrd / adc rounding) vs ai.c slot_point, every slot, d 10000 / 15000, random
    units and headings (the matrix built for the heading as manoeuvre 1's rotation); and 0x2ced0 case 7 (manoeuvre 7's
    start): 0x2d2f0 gets the unit's OWN code (+4 | 0x200), slot 3 / 15 (target 40 m or more away) or 4 / 12, d 10000 -
    the port's manoeuvre 7 point is checked against that point round the unit"""
    e = gblemu.Emu(); L = lib('ai')
    F2 = ctypes.c_float * 2
    for case in range(n * 4):
        e.reset_data()
        idx = rnd.randint(0, 20)
        cx, cy, cz = rnd.randint(-300000, 300000), rnd.randint(-5000, 20000), rnd.randint(-300000, 300000)
        h = rnd.choice([0.0, 90.0, 180.0, -90.0, rnd.uniform(-180, 180)])
        a = math.radians(h); c, s_ = math.cos(a), math.sin(a)
        rec = e.alloc(0x200); obj = e.alloc(0x100)
        e.w32(0x10a390 + 4 * idx, rec); e.w32(rec + 0x40, obj); e.w32(rec + 0x60, 0x12345)
        M = [c, 0, s_, 0, 1, 0, -s_, 0, c]
        for k in range(9): e.w32(obj + 0x3c + 4 * k, int(round(M[k] * (1 << 29))))
        for k, t in enumerate((cx, cy, cz)): e.w32(obj + 0x3c + 0x24 + 4 * k, t)
        for d in (10000, 15000):
            for k in range(16):
                px, pz, py = e.alloc(4), e.alloc(4), e.alloc(4)
                e.call(0x2d350, eax=idx | 0x200, edx=k, ebx=px, ecx=pz, stack=(py, d))
                out = F2(); L.shim_slot_point(ctypes.c_float(cx), ctypes.c_float(cz), ctypes.c_float(h), k, ctypes.c_float(d), out)
                dx, dz = e.rs32(px), e.rs32(pz)
                rep.check(abs(dx - out[0]) <= 1.5 and abs(dz - out[1]) <= 1.5,
                          'slot %d d %d h %.1f: dos (%d, %d) port (%.1f, %.1f)' % (k, d, h, dx - cx, dz - cz, out[0] - cx, out[1] - cz))
    # 0x2ced0 case 7: what goes to 0x2d2f0
    for case in range(n):
        for dist in (3999, 4000, rnd.randint(0, 3999), rnd.randint(4000, 90000)):
            for r in (0, 1):
                e.reset_data()
                u = e.alloc(0x200); e.w32(u + 4, 5); e.w8(u + 0x170, 7); e.w32(u + 0xc0, dist)
                got = []
                e.stub(0x4af60, lambda em, r=r: r)
                e.stub(0x2d2f0, lambda em: got.append((em.reg('edx'), em.reg('ebx') & 0xffff, em.reg('ecx'))) or 0)
                e.call(0x2ced0, eax=u)
                want = (5 | 0x200, (3 if r else 15) if dist >= 4000 else (4 if r else 12), 10000)
                rep.check(got == [want], 'case 7 dist %d rand %d: 0x2d2f0 got %s, the port\'s rule %s' % (dist, r, got, want))
    # the port's manoeuvre 7 point is one of the rule's two slot points round the unit itself
    for case in range(n * 4):
        sx, sz, h = rnd.uniform(-1e5, 1e5), rnd.uniform(-1e5, 1e5), rnd.uniform(-180, 180)
        dist = rnd.choice([rnd.uniform(0, 3999), rnd.uniform(4000, 50000)])
        out = F2(); L.shim_man7(ctypes.c_float(sx), ctypes.c_float(sz), ctypes.c_float(h), ctypes.c_float(dist), rnd.randint(1, 1 << 30), out)
        opts = (3, 15) if dist >= 4000 else (4, 12)
        best = min(math.hypot(out[0] - q[0], out[1] - q[1]) for q in
                   [(lambda o: (o[0], o[1]))((lambda o: (L.shim_slot_point(ctypes.c_float(sx), ctypes.c_float(sz), ctypes.c_float(h), k, ctypes.c_float(10000), o), o)[1])(F2())) for k in opts])
        rep.check(best < 0.5, 'manoeuvre 7 at (%.0f, %.0f) h %.1f dist %.0f: point (%.0f, %.0f) not slot %s' % (sx, sz, h, dist, out[0], out[1], opts))

@test
def contact_rule(rep, rnd, n):
    """GBL 0x2cb80 (the manoeuvre pick: a class 1 unit in contact backs off with 10 - unless its reverse input +0x2f is 1,
    +0x194 is set or its manoeuvre +0x170 is 10) and 0x2eb20 (the same test ending a running manoeuvre, 0x2c590) on every
    combination, vs ai.c contact_rule / choose_manoeuvre. The port had backed off on contact alone"""
    e = gblemu.Emu(); L = lib('ai')
    ends = ctypes.c_int()
    for contact in (0, 1):
        for rev in (0, 1):
            for j in (0, 1):
                for man in (-1, 0, 4, 10):
                    e.reset_data()
                    e.stub(0x22b50, ret=0)
                    u = e.alloc(0x200); obj = e.alloc(0x200); inp = e.alloc(0x60)
                    e.w32(u, 1); e.w32(u + 0x20, obj); e.w32(obj, u); e.w32(u + 0x4c, inp)
                    e.w32(obj + 0xa8, contact); e.w8(inp + 0x2f, rev); e.w32(u + 0x194, j); e.w8(u + 0x170, man & 0xff)
                    e.w16(u + 0x14a, 0x105)   # a nav point: the pick returns 0 after the contact rule
                    dos = e.call(0x2cb80, eax=u)
                    dend = e.call(0x2eb20, eax=obj)
                    prt = L.shim_contact_pick(contact, rev, j, man, ctypes.byref(ends))
                    rep.check(dos == prt and (dend != 0) == (ends.value != 0),
                              'contact %d reverse %d +0x194 %d man %d: dos pick %d end %d, port pick %d end %d' % (contact, rev, j, man, dos, dend, prt, ends.value))

@test
def flank_pick(rep, rnd, n):
    """GBL 0x2e9f0 (manoeuvre 1's slot: the sector of the attacker's bearing off the target's facing - 0x233f0 stubbed with
    the angle - and the counters +0x1a0..+0x1a7: the front case on the attacker's OWN record, the others on the target's)
    vs ai.c flank_pick, random and sector-edge angles, random counters, both rand(2) results. The port had tested the
    front case on the target's counters (one shared set: the player's) and rounded the sector half to even"""
    e = gblemu.Emu(); L = lib('ai')
    I8 = ctypes.c_int * 8
    for case in range(n * 40):
        e.reset_data()
        rel = rnd.choice([rnd.uniform(-180, 180), rnd.randint(-8, 8) * 22.5, rnd.randint(-4, 4) * 45.0, 180.0])
        err = int(round(rel * 65536))
        r2 = rnd.randint(0, 1)
        own = [rnd.choice([0, 0, 1]) for _ in range(8)]
        tgt = [rnd.choice([0, 0, 1, 2]) for _ in range(8)]
        me, t = 3, 7
        u = e.alloc(0x200); tr = e.alloc(0x200)
        e.w32(u + 4, me); e.w16(u + 0x14a, 0x200 | t); e.w32(0x10a390 + 4 * t, tr); e.w32(tr + 0xdc, 0x200 | me)
        for k in range(8): e.w8(u + 0x1a0 + k, own[k]); e.w8(tr + 0x1a0 + k, tgt[k])
        e.stub(0x22b50, ret=0)
        e.stub(0x233f0, ret=err & 0xffffffff)
        e.stub(0x4af60, ret=r2)
        dos = e.call(0x2e9f0, eax=u) & 0xffff
        prt = L.shim_flank_pick(ctypes.c_float(rel), r2, I8(*own), I8(*tgt)) * 2
        rep.check(dos == prt, 'rel %.3f r2 %d own %s tgt %s: dos slot %d port %d' % (rel, r2, own, tgt, dos, prt))

def main():
    args = sys.argv[1:]
    n, seed = 30, 1
    while args and args[0].startswith('-'):
        o = args.pop(0)
        if o == '-n': n = int(args.pop(0))
        elif o == '-s': seed = int(args.pop(0))
    names = args or list(TESTS)
    ok = True
    for nm in names:
        rep = Report(nm)
        TESTS[nm](rep, random.Random(seed), n)
        ok &= rep.done()
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
