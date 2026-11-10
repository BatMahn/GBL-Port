"""qsort_diff - the shell's sort: GBL MW2SHELL.EXE's library qsort 0x4b7aa with its two comparators (0x2b8a0 the Hall of
Honor, 0x22db0 the debriefing's objective list) executed under unicorn (GBL_SHELL_ELF, default
/home/claude/gbl/re/shellgbl.elf - not in the repo) against src/shelldb.c's wc_qsort, on random arrays full of ties.

  cc -shared -fPIC -O1 -Isrc tools/audit/qsort_shim.c src/shelldb.c -o /tmp/libqs.so
  python3 tools/audit/qsort_diff.py --lib /tmp/libqs.so [--runs 3000]"""
import argparse, ctypes, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab_diff import ShellEmu


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lib', required=True)
    ap.add_argument('--elf', default=os.environ.get('GBL_SHELL_ELF', '/home/claude/gbl/re/shellgbl.elf'))
    ap.add_argument('--runs', type=int, default=3000)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    lib = ctypes.CDLL(os.path.abspath(a.lib))
    emu = ShellEmu(a.elf)
    rnd = random.Random(a.seed)
    bad = hang = 0
    for run in range(a.runs):
        which = run & 1
        n = rnd.choice([1, 2, 3, 4, 5, 8, 15, 16, 20, 29, 30, 31, 42, 43, 48, rnd.randint(1, 48)])
        spread = rnd.choice([1, 2, 3, 5, 100])
        keys = [[rnd.randint(-1 if which else 0, spread) for _ in range(n)] for _ in range(3)]
        emu.reset_data()
        recs = emu.alloc(n * 0x20 + 16)
        for i in range(n):
            emu.w32(recs + i * 0x20 + 0xc, keys[0][i])
            emu.w32(recs + i * 0x20 + 0x10, keys[1][i])
            emu.w32(recs + i * 0x20 + 0x14, keys[2][i])
        arr = emu.alloc(n * 4 + 16)
        for i in range(n):
            emu.w32(arr + 4 * i, recs + i * 0x20)
        try:
            emu.call(0x4b7aa, eax=arr, edx=n, ebx=4, ecx=0x22db0 if which else 0x2b8a0, limit=2_000_000)
        except RuntimeError as ex:
            hang += 1   # the original never ends (see wc_qsort's no-progress note): not compared
            continue
        dos = [(emu.r32(arr + 4 * i) - recs) // 0x20 for i in range(n)]
        I = ctypes.c_int * max(n, 1)
        idx = I(*range(n))
        lib.shim_sort(which, idx, n, I(*keys[0]), I(*keys[1]), I(*keys[2]))
        port = list(idx)[:n]
        if dos != port:
            bad += 1
            if bad <= 5:
                print('run %d (%s, n %d): original %s, port %s, keys %s' % (run, 'objectives' if which else 'hall', n, dos, port, keys))
    print('%d runs, %d differ (%d not compared: the original does not finish)' % (a.runs, bad, hang))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
