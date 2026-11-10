# tools/audit - differential tests against the original GBL MW2.EXE

- `gblemu.py`: runs functions (or pieces of them) of the original executable under unicorn
  (`pip install unicorn`), from the 32-bit ELF image of GBL MW2.EXE's LE executable (not in the repo: game data;
  `GBL_ELF=/path/mw2gbl.elf`, default `/home/claude/gbl/re/mw2gbl.elf`). Watcom register calls, stubbed callees,
  bad accesses reported.
- `shim_*.c` + `build.sh`: the port's sources with their statics reachable, as shared libraries for ctypes
  (`tools/audit/build.sh [dir]`, default `/tmp/simdiff_build`, or `SIMDIFF_BUILD`).
- `simdiff.py`: the tests - both sides fed the same random / edge inputs, PASS / FAIL with the first differences.

      tools/audit/build.sh && python3 tools/audit/simdiff.py [-n cases] [-s seed] [test ...]

- `lab_diff.py`: the Mech Lab's masses - GBL MW2SHELL.EXE's design load (FUN_00034b30, from the shell's ELF image:
  `GBL_SHELL_ELF`, default `/home/claude/gbl/re/shellgbl.elf`) against the port (`gbl` with `GBL_LAB_DUMP`) for every stock
  design of a GBL MW2.PRJ:

      python3 tools/audit/lab_diff.py --game <GBL install> --bin ./gbl

- `qsort_diff.py` + `qsort_shim.c`: the shell's library qsort (0x4b7aa) with the Hall of Honor's and the debriefing's
  comparators against `wc_qsort` (src/shelldb.c), random arrays full of ties:

      cc -shared -fPIC -O1 -Isrc tools/audit/qsort_shim.c src/shelldb.c -o /tmp/libqs.so
      python3 tools/audit/qsort_diff.py --lib /tmp/libqs.so

- `sndvol_diff.py` + `sndvol_shim.c`: the positional sounds' level - 0x41690 (with the scene limit CLIM and the cockpit
  halving) against `sfx_pos_level` - and pan - 0x41800 against `sfx_pan_engine_ang` (src/sfx.h):

      cc -shared -fPIC -O1 -Isrc tools/audit/sndvol_shim.c -o /tmp/libsndvol.so -lm
      python3 tools/audit/sndvol_diff.py --lib /tmp/libsndvol.so

- `mtbl_diff.py` / `mtbl_shim.c`, `honor_diff.py`: the mission tables and the debriefing multiplier (see their headers).

No game data is needed beyond the ELF images (and, for lab_diff, a GBL install); nothing here is built by the main
Makefile.
