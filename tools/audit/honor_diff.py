#!/usr/bin/env python3
"""honor_diff - the debriefing's difficulty multiplier: GBL MW2SHELL.EXE's own instructions (0x25567..0x25579: fild /
fld / fmul / fsubrp / 0x4d382 frndint-chop / fistp, plus the add at 0x2557c) run under unicorn, the x87 control
word set as the runtime sets it (0x127f: double precision), on every honor value
the scoring can produce in a range, against tools/gbl/gbl_score.h (compiled into a small library).

usage: honor_diff.py [--elf shellgbl.elf] [--lo -20000] [--hi 40000]
The ELF (Ghidra's image of MW2SHELL.EXE) is game data, not in the repo."""
import argparse, ctypes, os, struct, subprocess, sys, tempfile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_FPCW

MULT = {0: 0.8, 1: 1.0, 2: 1.3}   # the doubles stored at 0x2540b / 0x25441 / 0x25474


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', default='/home/claude/gbl/re/shellgbl.elf')
    ap.add_argument('--lo', type=int, default=-20000)
    ap.add_argument('--hi', type=int, default=40000)
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, '..', 'gbl')
    d = tempfile.mkdtemp()
    with open(os.path.join(d, 't.c'), 'w') as f:
        f.write('#include "gbl_score.h"\nint honor_mult(int h, int dif) { return gbl_honor_mult(h, dif); }\n')
    subprocess.check_call(['cc', '-shared', '-fPIC', '-O1', '-I', src, os.path.join(d, 't.c'), '-o', os.path.join(d, 't.so')])
    lib = ctypes.CDLL(os.path.join(d, 't.so'))
    img = open(a.elf, 'rb').read()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    phoff, phnum = struct.unpack_from('<I', img, 28)[0], struct.unpack_from('<H', img, 44)[0]
    for k in range(phnum):
        t, off, va, pa, fs, ms = struct.unpack_from('<6I', img, phoff + 32 * k)
        if t != 1: continue
        lo, hi = va & ~0xfff, (va + ms + 0xfff) & ~0xfff
        uc.mem_map(lo, hi - lo)
        uc.mem_write(va, img[off:off + fs])
    uc.mem_map(0x800000, 0x10000)
    ebp = 0x808000
    bad = 0
    n = 0
    for dif, m in MULT.items():
        for h in range(a.lo, a.hi + 1, 5):   # every honor term is a multiple of 5 (5000 1500 500 -4000 250 125 25 750)
            uc.mem_write(ebp + 0x3e, struct.pack('<i', h))
            uc.mem_write(ebp - 0x42, struct.pack('<d', m))
            uc.reg_write(UC_X86_REG_EBP, ebp)
            uc.reg_write(UC_X86_REG_ESP, ebp - 0x100)
            uc.reg_write(UC_X86_REG_FPCW, 0x127f)   # the control word the C runtime loads (0x5c711 from 0x88e88)
            uc.emu_start(0x25567, 0x25579)
            local = struct.unpack('<i', uc.mem_read(ebp + 0x4e, 4))[0]
            dos = h + local                                   # 0x25579: ecx = honor; add the result
            port = lib.honor_mult(h, dif)
            n += 1
            if dos != port:
                bad += 1
                if bad <= 10: print('dif %d honor %d: original %d port %d' % (dif, h, dos, port))
    print('%d values, %d differ' % (n, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
