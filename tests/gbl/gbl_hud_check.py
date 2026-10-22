#!/usr/bin/env python3
"""GBL HUD check (agent K, 2026-10-21): the 14-weapon list and the target viewer on GBL's CPIT layout.

    python3 tests/gbl/gbl_hud_check.py [GBL_GAME_DIR]      (default /home/claude/GBL-game: install/ = the GBL files)

Runs glview headless on BASSSCN1 and checks, from the TEST ONLY hooks in tools/glview.c:
  - MW2_WEAPON_ORDER: a Dire Wolf DRW00USR (14 weapons) cycles row by row, left then right, over GBL's 7-row columns
    (windows 3-9 / 10-16, GBL MW2.EXE 0x32be0) and never stops on the AMS (0x53af0: table flags +0x08 bit 4) - the
    order DOSBox shows (/home/claude/gbl/ref/K_dw_c*.png);
  - a Timber Wolf TBR00STD (10 weapons) gets the same windows as DOSBox K_tb_*.png (left LLASER MLASER SLASER MPLAS
    LRM20, right LLASER MLASER MGun LRM20 MGun);
  - MW2_HUD_RECTS: the target viewer is CPIT rect 17 (10,145,52,34 -> 20,348 - 124,430 at 640x480), not rect 13 (a
    right-column weapon window in GBL's layout).
Exits 1 on a failure, 2 if the game folder or the program is missing.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
GAME = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/GBL-game'
GLVIEW = os.path.join(REPO, 'glview')


def userstar(path, skel, mek, bwd_id, mek_id):
    """A one-GPS USERSTAR.BWD (src/lance.c layout: +8 MEK id, +10 BWD id, +36 skeleton, +45 loadout)."""
    g = bytearray(92)
    g[0:4] = b'GPS\0'
    g[4:8] = struct.pack('<I', 92)
    g[8:12] = struct.pack('<HH', mek_id, bwd_id)
    g[13] = 1   # the lance leader; +14 0 = the player
    g[32:36] = struct.pack('<HH', 6, 0x400)
    g[36:36 + len(skel)] = skel.encode()
    g[45:45 + len(mek)] = mek.encode()
    g[54:58] = b'Test'
    rev = b'REV\0' + struct.pack('<I', 12) + b'1.22'
    dtbl = b'DTBL' + struct.pack('<I', 28) + bytes(20)
    body = rev + dtbl + bytes(g)
    with open(path, 'wb') as f:   # header: tag, file size, 0x5c (as tools/mkstar / the shell write it)
        f.write(b'BWD\0' + struct.pack('<II', 12 + len(body), 0x5c) + body)


def run(inst, extra):
    e = {k: v for k, v in os.environ.items() if not k.startswith('MW2_')}
    e.update({'MW2_INSTALL_DIR': inst, 'MW2_CD_DIR': os.path.join(GAME, 'cd'), 'MW2_CD_DATA': 'GBL',
              'MW2_EDITION': 'dos', 'SDL_VIDEODRIVER': 'offscreen', 'SDL_AUDIODRIVER': 'dummy', 'MW2_NO_STARTUP': '1',
              'MW2_SIZE': '640x480', 'MW2_AUTOPILOT': '1.5,0,-999,0,%s,1' % os.path.join(inst, 'final.ppm')})
    e.update(extra)
    prj = os.path.join(inst, 'MW2.PRJ')
    p = subprocess.run([GLVIEW, prj, prj, 'dos', '@', 'BASSSCN1'], env=e, cwd=REPO, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    return p.stdout.decode('latin-1')


def main():
    src = os.path.join(GAME, 'install')
    if not os.path.isfile(os.path.join(src, 'MW2.PRJ')) or not os.path.isfile(GLVIEW):
        print('SKIP: need %s/MW2.PRJ and %s (make glview)' % (src, GLVIEW))
        return 2
    work = tempfile.mkdtemp(prefix='gblhud_')
    inst = os.path.join(work, 'install')
    os.makedirs(inst)
    for e in os.listdir(src):
        p = os.path.join(src, e)
        os.symlink(os.path.realpath(p), os.path.join(inst, e))
    fails = 0

    def check(name, ok, detail=''):
        nonlocal fails
        print('%s %s %s' % ('PASS' if ok else 'FAIL', name, detail if not ok else ''))
        fails += not ok

    def with_star(skel, mek, bwd_id, mek_id, extra):
        sp = os.path.join(inst, 'USERSTAR.BWD')
        if os.path.lexists(sp):
            os.remove(sp)
        userstar(sp, skel, mek, bwd_id, mek_id)
        return run(inst, extra)

    # Dire Wolf DRW00USR (PRJ MEK id 153, BWD DIREWOLF id 17): 14 weapons
    out = with_star('direwolf', 'drw00usr', 17, 153, {'MW2_WEAPON_ORDER': '1'})
    order = [(m.group(1), int(m.group(2))) for m in re.finditer(r'^weapon \d+ (.+) window (\d+)$', out, re.M)]
    want = [('Torpedo', 3), ('MGun', 4), ('Inf SRM', 11)]   # L1, L2, R2 (R1 = AMS skipped)
    wins = [w for _, w in order]
    check('direwolf weapon count', len(order) == 14, str(order))
    check('direwolf starts Torpedo L1, L2, R2', order[:3] == want, str(order[:3]))
    check('direwolf order row by row', wins[:12] == [3, 4, 11, 5, 12, 6, 13, 7, 14, 8, 15, 9], str(wins))
    check('direwolf never the AMS', not any(n == 'AMS' for n, _ in order), str(order))
    # Timber Wolf TBR00STD (MEK id 105, BWD TIMBRWLF 38): 10 weapons, 5 a side
    out = with_star('timbrwlf', 'tbr00std', 38, 105, {'MW2_WEAPON_ORDER': '1', 'MW2_HUD_RECTS': '1', 'MW2_TEST_TARGET': '0'})
    order = [(m.group(1), int(m.group(2))) for m in re.finditer(r'^weapon \d+ (.+) window (\d+)$', out, re.M)]
    left = [n for n, w in sorted(order, key=lambda x: x[1]) if 3 <= w <= 9]
    right = [n for n, w in sorted(order, key=lambda x: x[1]) if 10 <= w <= 16]
    check('timberwolf left column', left == ['LLASER', 'MLASER', 'SLASER', 'MPLAS', 'LRM20'], str(left))
    check('timberwolf right column', right == ['LLASER', 'MLASER', 'MGun', 'LRM20', 'MGun'], str(right))
    vr = [l for l in out.splitlines() if l.startswith('HUDRECT viewer')]
    check('target viewer is CPIT rect 17', bool(vr) and vr[0].split()[3:7] == ['20', '348', '124', '430'], str(vr[:1]))
    shutil.rmtree(work, ignore_errors=True)
    print('%d failed' % fails if fails else 'all passed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
