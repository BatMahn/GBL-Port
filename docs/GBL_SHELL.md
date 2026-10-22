# Ghost Bear's Legacy - shell and simulation notes

Ghost Bear's Legacy (GBL, 1995) shipped as its own DOS program: `GBL.EXE` (a launcher) ran `MW2SHELL.EXE` (the menus)
and `MW2.EXE` (the simulation) in a loop - `mw2shell intro`, then { read MW2PRM.CFG, `mw2.exe <command line>`,
`mw2shell sim` } until the shell exits with 0xff. The port's equivalent is one executable, `gbl` (`make gbl`): the GBL
shell (`tools/gblshell.c` + `tools/gbl/*.c`) and the simulation (`gbl --sim ...`).

Everything was traced in GBL's own binaries (the original disc's `GBL\MW2SHELL.EXE`, `GBL\MW2.EXE`) or measured in DOSBox
running them. Addresses are GBL VAs (Ghidra over the LE executables converted to ELF; shell code VA 0x10000 = file
0x1000, data 0x70000 = file 0x5c000; sim data 0xa0000 = file 0x81000).

## The specs (docs/gbl/)
| file | covers |
|---|---|
| `A_frame.md`, `A_frame_credits.h` | start-up, the state machine FUN_00039ec0, DATABASE.MW2 (127 entries) and its map to the base's 103, DBSOUND.MW2, music (none), the mission clip lists, cheats, the title, the Esc menu and its pages (Combat Variables + TRANSITIONS, Cockpit Controls, Hall of Honor, The Keshik credits) |
| `B_roster_hall.md` | MW2REG.CFG (20 x 0x20e), MW2PRM.CFG (0x2c0), MW2DIF / MW2SND, the Sibko roster (12), the three Ghost Bear halls (1), the Bloodname roll (16), the developer egg (17) |
| `C_campaign.md` | the 22-mission campaign table, the Mission Computer (5: briefing + intelligence video + MECH PLANT star), the launch handoff (USERSTAR GPS 100 bytes), the debriefing (3) and its scoring, trials and ranks |
| `D_ia_mechlab.md` | the Combat Simulator (7), the Mech Lab (9) with GBL's 32-row mech table (Inner Sphere 'Mechs), the 35-weapon shell table (Inferno SRM-2, Torpedo, Inferno SSRM-2, ARROW IV) |
| `E_sim.md`, `E_sim_unmatched.tsv` | the simulation vs the base: mission tables unchanged; lift ground, DUMBTURT; weapons, AMS, NARC, bosses, effects |
| `J_sim_impl.md` | what the port's simulation implements for GBL and how (behind `sim_game_gbl()`) |

## The DOS copy that was analysed first
The first copy examined was a 1995 scene rip (TDU-JAM, "animations and soundtracks removed"). Compared with the original
disc's `GBL\` folder its executables differ only by the CD-check patches - `GBL.EXE` 0x101c5, 0x1021b, 0x104c2 (je -> jmp),
`MW2.EXE` 0x15c5d (je -> jmp over SYSERR 0x53) and 0x42f5e (the MSCDEX 0xADAD test nopped). `MW2SHELL.EXE`, `MW2.PRJ` and
`DATABASE.MW2` are byte-identical, so everything traced in it holds for the original. The check itself: MSCDEX's first CD
drive, a hidden `OLD_HERC.DRV` in its root (on the GBL disc itself). The port has no CD check.

## Game folder
See `FOLDER_SETUP.md`.
