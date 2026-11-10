#!/usr/bin/env python3
"""The positional one-shot sounds' level: GBL MW2.EXE 0x41690 under unicorn (gblemu) against the port's sfx_pos_level
(src/sfx.h) over random and edge distances, plus the scene limit CLIM (DAT_000a5be0, glview snd_at) and the halving flag;
and the pan 0x41800 (the camera yaw [0xa795c]+0xc and the direction, 16.16 degrees) against sfx_pan_engine_ang.

    cc -shared -fPIC -O1 -Isrc tools/audit/sndvol_shim.c -o /tmp/libsndvol.so
    python3 tools/audit/sndvol_diff.py --lib /tmp/libsndvol.so [-n cases]

0x41690 takes the vector (eax, edx, ebx: cm), the sound (ecx) and on the stack the halving flag and the priority; it
calls 0x41780 (stubbed here) with the delay in eax and the level in ecx only when the level is above 0."""
import argparse, ctypes, math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gblemu import Emu

ap = argparse.ArgumentParser()
ap.add_argument('--lib', default='/tmp/libsndvol.so')
ap.add_argument('-n', type=int, default=3000)
ap.add_argument('-s', type=int, default=1)
a = ap.parse_args()
lib = ctypes.CDLL(a.lib)
lib.port_pos_level.restype = ctypes.c_int
lib.port_pos_level.argtypes = [ctypes.c_long]
e = Emu()
got = []
def play(em):
    got.append((em.reg('eax'), em.reg('ecx')))
    return 0
e.stub(0x41780, play, pops=8)
rnd = random.Random(a.s)
edge = [0, 1, 99, 100, 999, 1000, 1001, 35400, 49999, 50000, 50100, 100000, 499999, 500000, 999999, 1000000, 3000000,
        4999999, 5000000, 5000099, 5000100, 6000000]
cases = [(d, 0, 0) for d in edge] + [(0, 0, d) for d in edge[::3]]
while len(cases) < a.n:
    r = rnd.choice((2000, 60000, 600000, 6000000))
    cases.append(tuple(rnd.randint(-r, r) for _ in range(3)))
bad = 0
for i, (dx, dy, dz) in enumerate(cases):
    half = i % 2
    clim = 10 if i % 7 == 3 else 0
    e.reset_data(); e.w32(0xa5be0, clim); got.clear()
    dist = e.call(0x41690, eax=dx & 0xffffffff, edx=dy & 0xffffffff, ebx=dz & 0xffffffff, ecx=0xf7, stack=(half, 0x32))
    dist = dist - (1 << 32) if dist & 0x80000000 else dist
    dos = got[0][1] if got else 0
    # the port: glview snd_at (CLIM by whole metres) then sfx_play_at3 (sfx_pos_level, >> 1 with the flag), the
    # distance from the engine's own estimate (0x2aee0) as the original measures it
    lv = 0 if clim and dist // 100 >= clim else lib.port_pos_level(dist)
    port = (lv >> 1 if half else lv) if lv > 0 else 0
    if dos != port:
        bad += 1
        if bad <= 10: print('DIFF vec', (dx, dy, dz), 'dist', dist, 'clim', clim, 'half', half, 'dos', dos, 'port', port)
print('level: %d cases, %d differ' % (len(cases), bad))
lib.port_pan.restype = ctypes.c_float
lib.port_pan.argtypes = [ctypes.c_float, ctypes.c_float]
cam = e.alloc(64)
pbad = ptie = 0
for i in range(a.n):
    yaw = rnd.uniform(0, 360); ang = rnd.uniform(-180, 180) if i % 2 else float(rnd.randint(-180, 179))
    y16, a16 = int(yaw * 65536), int(ang * 65536)
    e.reset_data(); e.w32(0xa795c, cam); e.w32(cam + 0xc, y16 & 0xffffffff)
    dos = e.call(0x41800, eax=a16 & 0xffffffff, edx=0)
    dos = dos - (1 << 32) if dos & 0x80000000 else dos
    port = int(lib.port_pan(y16 / 65536.0, a16 / 65536.0))
    x = 64 * math.sin(math.radians(y16 / 65536.0 - a16 / 65536.0))
    if dos != port and abs(x - round(x)) < 0.01:
        ptie += 1   # the original's table sine on the other side of a whole step
    elif dos != port:
        pbad += 1
        if pbad <= 10: print('PAN DIFF yaw', yaw, 'dir', ang, 'dos', dos, 'port', port)
print('pan: %d cases, %d differ (+%d within 0.01 of a step: the table sine)' % (a.n, pbad, ptie))
sys.exit(1 if bad or pbad else 0)
