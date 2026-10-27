# A - GBL shell frame: start-up, state machine, DATABASE map, title, global menu, registration path

Agent A, 2026-10-21. Binary: GBL MW2SHELL.EXE (DOS rip), `re/shellgbl_decomp.c` / `re/shellgbl.asm` (code VA = file + 0xf000,
data VA 0x70000 = file 0x5c000). Base = MW2 1995 MW2SHELL.EXE (`handoff/.../re/mw2shell_decomp.c`; I disassembled the base
ELF myself for the index scans). Every value below is traced at the cited address unless marked ASSUMED / INFERRED.
DOSBox captures are `/home/claude/gbl/ref/A_*.png` (default MW2SND/MW2DIF of the install, no pilot unless said).

Conventions
- **Getter index vs entry.** Both DATABASE getters are 1-based: `FUN_00022920(db, idx, &ptr, &size)` (raw read) and
  `FUN_000229d0(db, idx, &ptr, &size)` (LZSS-unpacked) read entry `idx - 1` (`FUN_00010470(list, idx - 1)`); idx < 0 or
  idx > count, or a database whose file did not open (+0x80 == 0), return 1 and leave ptr/size untouched. Throughout,
  "idx 0x51" means getter index 0x51 = **entry 80**. Same convention as base (the base shell "getters are 1-based").
- Watcom register calls: Ghidra drops/garbles register args (EDX/EBX/ECX carried over from the previous call). Where the
  decompile disagrees with the asm I quote the asm.
- Background loaders: `FUN_00040bb0(screen, db, idx)` (idx in EBX) and `FUN_00041110(screen)` which loads idx = EDX left
  over from the previous call (`FUN_0002e1e0(evq, idx)` flushes the keyboard and preserves EDX - 0x2e1e0 push/pop edx).
  So `FUN_0002e1e0(DAT_00095e8c, 5); FUN_00041110(screen)` = "background idx 5". (Base: `FUN_0002a650` + `FUN_0003ea60`.)

---------------------------------------------------------------------------------------------------------------------------

## 1. Start-up and the state machine `FUN_00039ec0` (called from 0x5cf0c, the C main)

### 1.1 Command line (0x39ec0 .. 0x39f50)
- `if (argc == 2) { if (strcmp(argv[1], "sim") == 0) resume = 1; }` - "sim" at **0x84df8**. Any other single argument (GBL.EXE
  passes **"intro"**, strings in GBL.EXE: `intro`, `mw2shell.exe`, `mw2prm.cfg`, `mw2.exe`, `sim`) is accepted and ignored.
- `else DAT_0007e784 = 0;` - argc != 2 (shell started bare): **"no simulation" mode**. 0x7e784 (initialised 1) gates the
  launch: case 10 only writes mw2prm.cfg and exits with code 3 when it is set; with it 0 the launch case just continues
  with the return state (a developer mode: missions "launch" without running the sim). It also forces the MSCDEX letter
  off (below) and makes `FUN_0003b2f0` (mouse update, 0x3b2f0) draw a debug readout at (0x230, 0x14) when +0x38 == 1.
- resume (= "sim", i.e. GBL.EXE restarting the shell after MW2.EXE): no `mintro`, start state = the return state read
  from mw2prm.cfg, previous state = 10. Otherwise: `mintro` plays (0x3a207), start state 8 (title), context 0, flag 0.
  Base FUN_00036700: identical logic (bVar1 = "sim"), start context 2 in the base; GBL starts with **context 0**
  (`local_38 = iVar10 = 0` at the `iVar10 == 0` branch).

### 1.2 MSCDEX drive letter (0x39f60 .. 0x3a0a0) -> 0x7e78c
- INT 21h AH=19h (current drive) -> d. INT 2Fh AX=150Bh CX=d: if BX != 0xADAD -> no CD. If AX == 0 (current drive is not
  a CD): INT 2Fh AX=1500h: BX = 0 -> no CD, else d = CX (first CD-ROM drive). INT 2Fh AX=150Ch: version < 0x20A
  (2.10) -> no CD. Only done once (`DAT_00084d64 == -2` guard).
- `0x7e78c` holds the string "A:\" (data `41 3a 5c 00`); its first byte becomes `'A' + d`, or 0 when there is no CD **or
  argc != 2**.
- No copy-protection check in the shell (confirmed: no OLD_HERC reference, NOTES.md).
- Uses of the letter (`"%ssmk\%s"` 0x76466, `"%ssmk\%s.smk"` 0x78b83, `"%ssmk\%s.shp"`):
  - DATABASE.MW2 / DBSOUND.MW2 (`FUN_000226c0`): open `<name>` in the current directory ("rb"); if that fails **and** the
    letter is set, open `X:\smk\<name>`.
  - Smacker videos (`FUN_0003c430` full-screen, `FUN_0003cf70` / `FUN_0003d0c0` screen clips): `smk\<name>.smk` first, then
    `X:\smk\<name>.smk` (0x3c451 .. 0x3c50f: the first attempt uses the CD only while 0xadedc != 0, which is always reset
    to 0, so it is always local first, CD second). SHP stills: `smk\<name>.shp`, then `X:\smk\<name>.shp`.
  - Port: no MSCDEX; search the install's `SMK\` and then a configured disc directory's `SMK\` the same way.

### 1.3 Interrupt hooks, objects (0x3a0a0 .. 0x3a200)
- DPMI-locks 0x7e78c (4), 0xada24 (6), 0xada20 (4); saves INT 9 (`0x3509`), installs its own INT 9 at 0x39d20 (keyboard
  hook, `0x2509`); restored at exit (0x3ad7a ..). Not portable/needed.
- `DAT_00095ec0 = new DB("DATABASE.MW2")` (0x7e894), `DAT_00095ec4 = new DB("DBSOUND.MW2")` (0x7e898) - FUN_000226c0.
- `DAT_00095e90 = FUN_0003da10()` (sound system: MW2SND / Miles), `if (DAT_0007e788) FUN_0004e1d4()` (Smacker init;
  0x7e788 = 1), `DAT_00095ecc = 0x7e780 ? 0xfe00 : 0` (Smacker open flags = all audio tracks; 0x7e780 = 1).
- `FUN_00041b50()` (video mode / screen), then `mintro` (unless resume), then `DAT_00095e9c = FUN_00040440()` (screen
  object; FUN_00040440 reads idx 0x23 = **entry 34, the MZ DLL**, same as base FUN_0003dd90 idx 0x21 = base entry 32).
- Fonts (`FUN_00027e60(obj, data, screen)`; asm 0x3a224 .. 0x3a3dc):

  | handle | getter idx | entry | font header (glyphs, height) | notes |
  |---|---|---|---|---|
  | 0x95ea4 | 0x1c | 27 | 256, 8 | **overwritten** at 0x3a3d7 by the idx 0x22 font (handle leaked); same bug in base (0x91170) |
  | 0x95ea8 | 0x1d | 28 | 256, 15 | Keshik '<' / '~', Combat Variables values |
  | 0x95eac | 0x1e | 29 | 256, 16 | Keshik '>', Hall of Honor headers |
  | 0x95eb0 | 0x20 | 31 | 256, 15 | global menu: selected item |
  | 0x95eb4 | 0x21 | 32 | 256, 15 | global menu: items |
  | 0x95ea0 = 0x95ea4 = 0x95eb8 = 0x95ebc | 0x22 | 33 | 240, 10 | default font: hotspot labels, dialogs, HoH rows, Keshik plain lines |

  idx 0x1f (entry 30, 128 glyphs, height 8) is **not** loaded at start-up (base also skips its idx 0x1d = base entry 28).
  Base loads idx 0x1a 0x1b 0x1c 0x1e 0x1f 0x20 into 0x91170 0x91174 0x91178 0x9117c 0x91180 / 0x9116c=0x91170=0x91184=0x91188:
  exactly **+2** in GBL.
- Sounds made at start-up (`FUN_0003db50` make, `FUN_0003def0` volume): idx 0x4f (entry 78) -> 0x98834 (no volume set:
  default 5, see 0x3db50 `param_1[4] = 5`); idx 0x76 (117) -> 0x98838 vol 0x32; idx 0x77 (118) -> 0x9883c vol 0x32; idx 0x78
  (119) -> 0x98840 vol 0x32. Base: idx 0x4d / 0x65 / 0x66 / 0x67 (0x91690 .. 0x9169c), so +2 for the first and +0x11 for the
  three button sounds (see section 2).
- Mouse pointer: idx 0x1b (entry 26, SHP "1.10", 1 frame) -> `DAT_00095e98 = FUN_0003b0d0(screen, font, shape)`; base
  idx 0x19 (+2).
- `DAT_00095e8c = FUN_0002e170()` (event queue; flushes the keyboard), `DAT_00095ec8 = FUN_0001c9d0("MW2.PRJ")`.
- `FUN_0001dfe0(prj, &ctx, &flag)` - reads **MW2REG.CFG**: 20 records x **0x20e** bytes at 0x95ed0 (base: 60 bytes). If the
  file is missing it builds 20 default records. For every in-use record **i < 10** it copies the name +0x3d -> +0x28 (when
  +0x52 == 0) and applies the name cheats DOOMSDAY / LOVECRAFT / RVWINDOW (1.8). Records 10-19 are the top-ten list written
  by screen 16 (FUN_0001dbd0) and shown by the Hall of Honor.
- `FUN_00026c20()` reads MW2DIF.CFG (8 bytes -> 0x7e804); `FUN_0003aeb0(&state)` reads mw2prm.cfg (0x2c0 bytes -> 0xada2c:
  +0 return state, +4 context, +8 flag byte, +0x1bc pilot index (-> DAT_000987e8 = 0x95ed0 + i * 0x20e or 0), +0x1c0 sim
  command line; then FUN_0003e060 restores the star); `FUN_00026be0()` reads **MW2SND.CFG, 0x40 bytes** -> 0x7e7c4 (base 0x3c
  bytes). The extra int32 at +0x3c (0x7e800) is the new **TRANSITIONS** switch (section 4.2).
- The cheat strings are decoded in place (XOR 7, then `toupper` FUN_0004b61a) - loop 0x3a5d9. See 1.8.

### 1.4 DBSOUND.MW2
- Same container as DATABASE.MW2 (u32 count, u32 offsets). Titanium disc copy (`cd/INSTALL/DATA/DBSOUND.MW2`, identical
  copy in `cd/INSTALL/DATA/SMK/`): **44 entries, all raw RIFF WAV** (sizes 27 KB .. 250 KB).
- Users (getter `FUN_00022920(DAT_00095ec4, idx)`, so entry = idx - 1), indexed by the campaign mission record
  (0x7fb8c + m * 0x39, see 1.6):
  - briefing FUN_00028410 (0x2851b area, decomp line 17089): idx = mission +0x0c (`0x7fb98 + m*0x39`) = **1 .. 22** ->
    entries 0-21, volume 0x4b, played with the briefing (m = DAT_0007e840, the briefed mission).
  - debriefing FUN_00024750 (decomp 14345): when the result is success (`local_d08 == 2`), idx = mission +0x10
    (`0x7fb9c + m*0x39`) = **23 .. 44** -> entries 22-43, volume 0x4b; -1 (none) for mission 4 (jeeh) and 8 (hoff).
  - So DBSOUND = 22 briefing voice-overs + 22 debriefing voice-overs (INFERRED from the users; content not listened to).
- **DBSOUND.MW2 missing** (the DOS rip has none; under DOSBox D: = the Titanium ISO has no `\SMK\DBSOUND.MW2` either): the
  object exists but its file handle is 0; the getter returns 1 without touching the output (initialised to 0 /
  uninitialised size), the sound object is built over a NULL buffer and nothing is heard. The shell does not crash
  (briefing captures `C_07mission.png` by agent C were taken in exactly this state). Port: treat a missing DBSOUND as
  "no voice-over".

### 1.5 Music
- Per-screen tables (18 x u32, indexed by state): **0x84db0 when context == 0, 0x84d68 otherwise** (base: three tables
  0x838c8 Wolf / 0x83910 Falcon / 0x83880 neutral). Both GBL tables are **identical**:

  | state | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | value | 0 | 0 | 0 | 0 | 0x20000000 | 0 | 0 | 0 | 0x20000000 | 0 | 0x20000000 | 0 | 0 | 0 | 0 | 0x20000000 | 0x20000000 | 0 |

  0 = keep the current song; 0x20000000 = stop the song (0x3a6d7 ..). **No entry selects a song**, so the GBL DOS shell
  never starts XMIDI music, although DATABASE.MW2 still holds 40 songs (entries 35-74, section 2). The tables are never
  written (no store to 0x84d68..0x84df7 in the asm). The only other music code, FUN_00039180 (same logic, one screen),
  has no caller. There is no CD-audio code (no INT 2Fh 1510h). The rip's "soundtracks removed" note may refer to CD audio
  that the original never played from the shell anyway - UNRESOLVED whether a cracker edit zeroed these tables (nothing
  else in the binary references a song number).
- Driver offset table **0x853e8** (14 x {char *name, int offset}, used only when a song would start): ADLIB 0, ADLIBG 8,
  MPU401 32, MT32 24, OPL3 8, PAS 0, PASPLUS 8, SBAWE 16, SBLASTER 0, SBPRO1 8, SBPRO2 8, SNDSCAPE 32, ULTRA 32, TANDY 8 -
  the base table 0x83e14 unchanged (database idx = number + offset; default 8 when the driver name is not listed).
- Port: no menu music in the GBL shell (match the original). If music is ever wanted as an option, the base mapping with
  the +2 shift applies (base song entries 33-72 = GBL 35-74).

### 1.6 Campaign mission records 0x7fb8c (stride 0x39, 22 missions; field offsets from the record start)
Needed here for the videos; full use belongs to the briefing/debriefing specs.
`+0x00 char *brf` (briefing clip "bassbrf1" ..), `+0x04 char *` ("" for all), `+0x08 char *dbfs` ("bassdbfs" ..),
`+0x0c int` briefing voice (DBSOUND idx; code address 0x7fb98 + m*0x39), `+0x10 int` debriefing voice (0x7fb9c),
`+0x14 int` (0x7fba0, debriefing), `+0x18 byte` (0x7fba4, debriefing), `+0x19 int` retry cap (0x7fba5), `+0x1d int`
clan-hall variant written to pilot +0x5a after a win (0x7fba9), then three clip lists `{int count; {char *name; int 0, 0, 0}
*list}`: **A** +0x21 / +0x25 (0x7fbad / 0x7fbb1), **B** +0x29 / +0x2d (0x7fbb5 / 0x7fbb9), **C** +0x31 / +0x35 (0x7fbbd / 0x7fbc1).

| m | mission | voice brf / dbf | hall variant (0x7fba9) | A: before launch (case 10) | B: debrief, success (FUN_00024750) | C: debrief, next mission picked |
|---|---|---|---|---|---|---|
| 0 | bass | 1 / 23 | 2 | - | - | to, attach, warp, sep |
| 1 | jess | 2 / 24 | 2 | - | - | - |
| 2 | clai | 3 / 25 | 0 | - | mraid | attach, warp, sep, land |
| 3 | newc | 4 / 26 | 1 | - | - | to, attach, warp |
| 4 | jeeh | 5 / - | 1 | sep | attach | - |
| 5 | tim_ | 6 / 28 | 0 | sep | attach | warp, sep, land |
| 6 | andy | 7 / 29 | 1 | to, attach, warp, sep | attach | - |
| 7 | fost | 8 / 30 | 1 | sep | attach | - |
| 8 | hoff | 9 / - | 1 | sep | attach | - |
| 9 | zack | 10 / 32 | 1 | - | - | - |
| 10 | sean | 11 / 33 | 1 | sep | attach | warp |
| 11 | fran | 12 / 34 | 0 | sep | - | - |
| 12 | asah | 13 / 35 | 1 | - | - | to, attach, warp |
| 13 | pete | 14 / 36 | 1 | sep | attach | - |
| 14 | cath | 15 / 37 | 1 | sep | - | - |
| 15 | sash | 16 / 38 | 1 | - | attach | - |
| 16 | bran | 17 / 39 | 0 | sep | mend, attach, warp, sep, land | - |
| 17-21 | gui1-4, guin | 18-22 / 40-44 | 0 | - | - | - |

(The "voice" column is DBSOUND getter idx. Hall variant = record +0x1d: the debriefing sets `pilot+0x5a = *(0x7fba9 + m*0x39)`
(decomp line 15257) and the hall picks its background record by pilot +0x5a (0x1e28b); the name "hall variant" is mine.)

### 1.7 The loop (0x3a65x ..) and every case
Top of each iteration: `prev = cur_prev; if (state == 5 && ctx == 2) state = 7; if (state == -2) state = prev;` then the music
table (1.5), then `switch (state)` through the jump table at **0x39e78** (index state - 1, states 1..17; > 17 or 0 ->
default). `FUN_00028210(x, 0x1d1, 0)` called before most screens is a debug free-memory readout (INT 31h 0500h) drawn only
when 0xa917c != 0 - never set: ignore.

| state | code | what it does | next state / context |
|---|---|---|---|
| 1 | 0x3a929 | if prev == 12: `FUN_0003e350(0,0,3,1,100)` (reset star 0: formation 0, size 3, max 1, tonnage 100) and `FUN_0003e0b0(0,0,pilot+0x28,0)` (pilot into slot 0). Then clan hall `FUN_0001e270(db, ctx)` with BL = flag | hall's return. **Hall with flag == 0 returns 12 at once** (0x1e270: `if (!flag) return 0xc;`) |
| 3 | 0x3ac95 | debriefing `FUN_00024750(db, ctx)` | its return (1 / 5 / 8 / 0x10 / -3 / ...) |
| 5 | 0x3a9e1 | if prev is 1 or 3: star reset (as above); pick the scenario (`local_34`): the mission's own `0x8017c[ctx][m*9]` unless the pilot has retries (+0x56) and an alternative list exists (0xa93b8[m]); prev 3: copy the star from the pilot record (+0x5e, +0x66, +0x6a, +0x6e, slots +0x76 * 0x40); else `FUN_0002c810(scen, 1)` (star from the PRJ, type 0x46; also copies the PRJ type 0x47 name into 0x84dfc for "-b="). Then `FUN_0003e350(1,0,0,0,100)`, `FUN_0003e350(0,-1,-1,-1,-1)`, `FUN_0003e0b0(0,0)`, briefing `FUN_00028410(db, scen, ctx, flag, prev)` | its return |
| 7 | 0x3a8ca | ctx = 2; Instant Action `FUN_0002cc80(db, &scen)`; if it returns 1: ctx = 0 and `FUN_0003e0b0(0)` | its return (1 / 9 / 10) |
| 8 | 0x3a882 | `FUN_0003e350(1,0,0,0,100)`; flag = 0; title `FUN_0002dda0(db, &ctx)` | 7 (ctx 2) / 15 (ctx 0 or 1) / -3 / menu result |
| 9 | 0x3ab42 | Mech Lab `FUN_000374e0(db, ctx)` | its return |
| 10 | 0x3ab71 | launch: next = 3 if prev is 5 or 9 and 0x95e88 == 0; else 12 if 0x95e88 == 1 (then cleared); else prev. If prev == 5: play clip list **A** of mission pilot+0xc (`FUN_0003c430(name)` each, full screen); stops at the first clip the player skipped (0xadee0); if 0x7e800 (TRANSITIONS) == 0 only "mraid" / "mend" clips play (none in list A). Then if 0x7e784: `FUN_0003af90(next, ctx, flag, scen)` (writes mw2prm.cfg) and **exit code 3** | (resume after the sim) next |
| 12 | 0x3a98f | 0x95e88 = 0; Sibko roster `FUN_0003b8d0(db, ctx)`; if it returns 5: 0x95e88 = 1 ("launch old mission": the later launch returns to 12 and skips the debriefing) | its return (1 / 5 / -1) |
| 15 | 0x3accc | `FUN_0003c430(NULL, 1)`: a full-screen video with a NULL name (asm 0x3accc: `xor eax,eax; mov edx,1`) - the open of "smk\(null).smk" fails twice and nothing plays (INFERRED: Watcom's printf prints "(null)"; DOSBox confirms no video, A_reg_*). EDX survives the call, so **next = 1** | 1 (clan hall), context unchanged |
| 16 | 0x3ace1 | if pilot +0x52 (missions failed, incremented by the debriefing on a failure) == 0 and pilot +0xc != 0x11: `FUN_0001dbd0(db)` (enter the pilot into the top-ten records 10-19 sorted by honor +0x14, rewrite MW2REG.CFG, show the list, wait for a click; background idx 0x17 via 0x801e8 = entry 22, hotspots 0x801e0 (11)); else full-screen "mend" (0x78a5f) | 1 |
| 17 | 0x3a868 | `FUN_0004b1a0(db, prev)` ("FERREGG1" screen; background idx 0x18 / 0x19 / 0x1a by pilot +0x5a = entries 23 / 24 / 25) | prev / 8 / -3 |
| 0, 2, 4, 6, 11, 13, 14, other | 0x3ad1e | default: `fprintf(stderr, "Unknown Transition Requested: %d\n")` (0x78a64) and exit(1) | - |

GBL has **no** state 0 (base briefing), 11 (Ready Room), 13 (Star Configuration), 14 (Cadet Training); base 15 = landing
cinematic mwoland / mjfland -> 1 and 16 = ending mend / mend2 -> 1 (the base shell "Screen state machine").
Base rewrote `state 11 && ctx 2 -> 7`; GBL rewrites `state 5 && ctx 2 -> 7`.

**Exit** (`state == -3`, 0x3ad50): `FUN_0003af90(-3, ctx, flag, "exittos")` (mw2prm.cfg: +0 = -3, +0x1c0 = "exittos -b=..."),
then free PRJ, both databases, mouse, event queue, screen, sound; restore INT 9; **exit code 0xff** (launch: 3). GBL.EXE loops
`mw2shell intro` -> { read mw2prm.cfg, `mw2.exe <+0x1c0>`, `mw2shell sim` } until exit code 0xff (NOTES.md).

mw2prm.cfg (0x2c0 bytes, FUN_0003af90): +0 next state, +4 context, +8 flag byte, +9 0, ... (FUN_0003e020 / 0x3e3b0 fill the
star records unless next is 14 or -3), +0x1bc pilot index (-1 none), +0x1c0 = `<scenario>` + `" -b="` (0x78ac6) + 0x84dfc
(default "xxxxxxxx.xxx", set from the scenario's PRJ type-0x47 record) + `" -x"` (0x78acb) when the RVWINDOW cheat is on.
The install's MW2PRM.CFG is a leftover exit: `-3, 0, 0, "exittos -b=xxxxxxxx.xxx", pilot -1`.

### 1.8 Videos, in order of appearance
| when | clip | how | ref |
|---|---|---|---|
| start (not resume) | **mintro** (320x200, 1011 frames, 66.7 ms = 67 s) | FUN_0003c430 full screen; a key or mouse button skips (0x3c5c6: INT 33h fn 3 bit 0 -> 0xadee0 = 1; or a key) | 0x3a207; A_intro6.png (Activision logo at 6 s) |
| title | aintro01-04 | screen clips, section 3 | |
| state 10, from the briefing | list A of the mission (to / attach / warp / sep, 320x200, 9-22 s each) | full screen, one after another; a skip ends the list | 0x3abdf |
| debriefing, success | list B (mraid 55 s, attach, mend ...) | FUN_00024750 (decomp 15237) | |
| debriefing, next mission | list C | FUN_00024750 (decomp 15462) | |
| debriefing, failure | deathmov, when the retries on this mission (pilot +0x56) exceed the cap (+0x19) and further conditions (mission < 0x11, ...) hold | FUN_00024750 (decomp 15266) | |
| state 15 | none (NULL name) | | A_reg_0..14 |
| state 16 | mend (33 s) when the pilot has failed a mission, else the top-ten screen | 0x3ace1 | |

TRANSITIONS OFF (MW2SND +0x3c = 0) makes every clip-list loop play only clips named "mraid" or "mend" (strcmp at 0x3ac16
and FUN_00024750); `mintro`, the title clips and state 16's `mend` are not affected.

### 1.9 Cheat names (pointer list 0x7e80c, 8 entries, each string XOR 7 then upper-cased at start-up, 0x3a5d9)
Compared with `strcmp` (FUN_0004be40, case-sensitive) against the pilot name (record +0x3d, entered upper-case by the roster)
or the star-slot-0 name (FUN_0003e330(0) + 0x28).

| # | string (VA) | decoded | where (address) | effect |
|---|---|---|---|---|
| 0 | 0x76b02 | **DOOMSDAY** | FUN_0001dfe0 (any in-use record 0-9 at start-up), roster on create (`FUN_0002e7c0(0x80,1)`) / delete (`(0x80,0)`) | 0xa9bd0 = 1: weapon 0x1e (**"Nuke"**, weapon table 0x80af8 + 0x1e*40) is offered in the Mech Lab (0x302be, 0x33819 skip weapon 0x1e unless set) |
| 1 | 0x76b0b | **LOVECRAFT** | same, `FUN_0002e7c0(0x81, x)` | 0xa9bd4 = 1: weapon 0x20 (**"Torpedo"**) offered in the Mech Lab (0x302d4, 0x33828) |
| 2 | 0x76b15 | **RVWINDOW** | same, FUN_0003ae90 / FUN_0003aea0 | 0xadcec = 1: the sim command line gets **" -x"** (FUN_0003af90 / FUN_0003aeb0). In GBL MW2.EXE the option parser FUN_00044dd0 handles 'X' (case 0x16): `FUN_00049ea0(); DAT_000a8c34 = 1` - the sim-side meaning is UNRESOLVED (sim agent) |
| 3 | 0x76b1e | **IMMORTAL** | debriefing FUN_00024750 (0x25e07, 0x25f01), not in context 2 | on events 0x8011 / 0x8026 the pilot record is restored from the copy taken on entry (0x83 dwords) - the mission's result is discarded (exact trigger: debriefing spec) |
| 4 | 0x76b27 | **TOLKEIN** | - | decoded but **never compared** (no reference to 0x7e81c in the asm) - dead |
| 5 | 0x76b2f | **WIDOW** | Instant Action FUN_0002cc00 / FUN_0002cc80 (slot-0 pilot name) | IA chassis count 0x80388 = 0x1f (31): adds Elemental and Tarantula |
| 6 | 0x76b35 | **LITTLEGUY** | same | count 0x1e (30): adds Elemental. Normal count 0x1d (29). The 32nd chassis (Battle Master IIC, table 0x84a20 #31) is never reachable |
| 7 | 0x76b3f | **THEWATCHER** | roster FUN_0003b8d0 (0x3b6fc, 0x3bbad, 0x3c058) | a new pilot of that name starts with missions done = 0x16 (22, every mission); LAUNCH OLD MISSION entries above index 0x10 (missions 17-21, gui1..guin) are allowed only for this name |

Base equivalents: plain names "Calvin" / "Hobbes" / a third string for the IA mechs, FREEBIRTHTOAD for the Ready Room
(the base shell). C table:
```c
static const char *const GBL_CHEATS[8] = { "DOOMSDAY", "LOVECRAFT", "RVWINDOW", "IMMORTAL",
                                           "TOLKEIN", "WIDOW", "LITTLEGUY", "THEWATCHER" };
```

### 1.10 Dead code found on the way (do not port)
- 0x393d0 .. 0x39ebf: a second copy of the state loop (mw2prm read, XOR loop at 0x39416, music, its own jump table at
  0x39388) with no caller. FUN_00039320 (a stand-alone copy of the XOR loop) and FUN_00039180 (music for one screen): no callers.
- 0x26ce0: a stand-alone Combat Variables page (same body as menu case 1), no caller.
- FUN_00028210: free-memory readout behind 0xa917c (never set).

---------------------------------------------------------------------------------------------------------------------------

## 2. DATABASE.MW2 (GBL, 127 entries) - map and base correspondence

Types read from the file (my parser: raw RIFF / "1.10" SHP / "1." font / MZ; others LZSS with u32 size; PCX decoded):
0-4 PCX, 5-7 SHP, 8-25 PCX (18), 26 SHP, 27-33 fonts, 34 MZ DLL, 35-74 XMIDI (LZSS), 75-126 WAV (raw).
Base (the base shell): 21 PCX, 3 SHP, cursor, 7 fonts, DLL, 40 XMIDI, 30 WAV = 103.
The Titanium disc's DATABASE.MW2 differs only in entries 2 (Combat Variables background) and 27-29, 31-33 (fonts).

| entry | idx | type | GBL user (address) | base entry / user | match |
|---|---|---|---|---|---|
| 0 | 1 | PCX 640x480 (almost black) | title bg (0x2ddcf, FUN_00040bb0 ebx=1) | 0 title | same |
| 1 | 2 | PCX | Hall of Honor bg (0x2b91e) | 1 HoH | same |
| 2 | 3 | PCX | Combat Variables bg (0x27343) | 2 | same (labels differ: GBL adds TRANSITIONS, A_cv.png) |
| 3 | 4 | PCX | Cockpit Controls bg (0x21f49) | 3 | same |
| 4 | 5 | PCX | The Keshik bg (0x2232e) | 4 | same |
| 5 | 6 | SHP (3 frames) | dialog panel (FLEE TO DOS / FUN_00027a40) | 5 | same |
| 6 | 7 | SHP | global menu panel (0x26f52 edx=7) | 6 | same |
| 7 | 8 | SHP (2 frames) | Combat Variables slider parts (0x2732c -> 0x95e84) | 7 | same |
| 8 | 9 | PCX | Instant Action bg (FUN_0002cc80, ebx=9) | 8 IA | same |
| 9 | 10 | PCX | Mech Lab, context 2 (0x802e0 rec) | 9 (IA lab) | same |
| 10 | 11 | PCX | clan hall variant 0 (0x80180 rec, pilot +0x5a = 0) | 10 Wolf hall | replaced |
| 11 | 12 | PCX | clan hall variant 1 (0x80190) | 11 Wolf room | replaced |
| 12 | 13 | PCX | clan hall variant 2 (0x801a0) | 12 | replaced |
| 13 | 14 | PCX | briefing (0x801c0 rec: hotspots 0x7ec00, 13) and 0x80220 rec (hotspots 0x7f028, 4) | 13 | replaced |
| 14 | 15 | PCX | - (no constant user found) | 14 | ? |
| 15 | 16 | PCX | Mech Lab contexts 0 / 1 (0x802c0 / 0x802d0, hotspots 0x7f354, 10) | 14 Wolf lab (slot 15) | moved |
| 16 | 17 | PCX | debriefing variants (0x80260 / 0x80280 / 0x802a0, 4 hotspots each) | 15 | replaced |
| 17 | 18 | PCX | **Sibko roster** (0x80200 rec, hotspots 0x7eea0, 14; A_reg_1.png) | 16 Wolf roster (slot 17) | moved |
| 18 | 19 | PCX | briefing (FUN_00028410 ebx=0x13) | 17 Falcon hall | replaced |
| 19 | 20 | PCX | debriefing (FUN_00024750 ebx=0x14) | 18 | replaced |
| 20 | 21 | PCX | debriefing (0x80240 rec, 4 hotspots 0x7f0b8) | 19 | replaced |
| 21 | 22 | PCX | briefing and screen 17 (ebx=0x16) | 20 | replaced |
| 22 | 23 | PCX | screen 16 top-ten (0x801e8) | 21 | new |
| 23-25 | 24-26 | PCX | screen 17 (FUN_0004b1a0) by pilot +0x5a 0/1/2 | 22-23 Falcon rooms / - | new |
| 26 | 0x1b | SHP | mouse pointer (start-up) | 24 (idx 0x19) | **+2** |
| 27-33 | 0x1c-0x22 | fonts | section 1.3 | 25-31 | **+2** |
| 34 | 0x23 | MZ DLL | FUN_00040440 | 32 (FUN_0003dd90) | **+2** |
| 35-74 | 0x24-0x4b | XMIDI x 40 | none (1.5) | 33-72 | **+2** (types and count match; not played) |
| 75 | 0x4c | WAV 11 kHz 3.5 s | - | 73 title ambient | +2, unused |
| 76 | 0x4d | WAV 22 kHz 2.6 s | Mech Lab (FUN_000374e0) | 74 Mech Lab | +2 |
| 77 | 0x4e | WAV 2.3 s | - | 75 Cadet Training | +2, unused |
| 78 | 0x4f | WAV 0.4 s | start-up sound 0x98834 | 76 start-up | +2 |
| 79 | 0x50 | WAV 0.4 s | Instant Action | 77 IA | +2 |
| 80 | 0x51 | WAV 11 kHz 3.6 s | **title ambient**, clan hall variant 0 (vol 10), roster (0x3bace) | 78 clan hall; base roster used idx 0x51 = base entry 80 | +2 for the hall; the roster's idx is unchanged (so it now plays the hall sound) |
| 81 | 0x52 | WAV 0.25 s | Mech Lab | 79 Mech Lab | +2 |
| 82, 83 | 0x53, 0x54 | WAV 1.9 / 3.1 s | - | 80 roster, 81 IA | +2, unused (GBL IA uses 0x7a) |
| 84-114 | 0x55-0x73 | WAV x 31 | Mech Lab chassis voices (chassis table 0x84a20 +0x14) | 82-98 (17 voices) | first 17 = base +2 (Firemoth base 0x55 -> GBL 0x57) INFERRED; 14 new |
| 115 | 0x74 | WAV 0.7 s | - (Tarantula has -1) | - | new, probably the Tarantula voice (INFERRED) |
| 116 | 0x75 | WAV 1.2 s | - | 99 Ready Room (base idx 0x64) | +17, unused |
| 117-119 | 0x76-0x78 | WAV 0.01 / 0.27 / 1.2 s | button sounds 0x98838-0x98840, vol 0x32 | 100-102 (base idx 0x65-0x67) | **+17** |
| 120 | 0x79 | WAV 1.7 s | clan hall (vol 100) | - | new |
| 121 | 0x7a | WAV 3.2 s | briefing, debriefing, Instant Action (vol 0x32) | - | new |
| 122 | 0x7b | WAV 4.7 s | clan hall variant 1 ambient (vol 0x28) | - | new |
| 123 | 0x7c | WAV 2.2 s | clan hall variant 2 ambient (vol 0x28) | - | new |
| 124 | 0x7d | WAV 2.2 s | - | - | new, unused |
| 125 | 0x7e | WAV 7.6 s | title, vol 0x32, played once at entry | - | new |
| 126 | 0x7f | WAV 13.7 s | - | - | new, unused |

Verdict on "+2 from entry 21": **confirmed for entries 24+ of the base (cursor, fonts, DLL, music, the first nine WAVs
and - by the voice ordering - the chassis voices)**: GBL has 23 PCX backgrounds where the base has 21. Entries 0-9
keep their base roles exactly. Entries 10-25 are GBL's own room images (Ghost Bear halls, briefing, roster ...), not
shifted base images. In the WAV block the base order is kept but 15 chassis voices are inserted after the 17 base
voices (so the base's last four WAVs move +17) and 7 new WAVs are appended (120-126). "Unused" = no constant getter index in
the asm (a table-driven user cannot be excluded for 75, 77, 82, 83, 115, 116, 124, 126). Content identity with the base
WAVs is INFERRED (no base DATABASE.MW2 here to compare bytes).

---------------------------------------------------------------------------------------------------------------------------

## 3. Title screen - state 8, `FUN_0002dda0`
Captures: A_t0.png (3 s after Esc, logo + emblem forming), A_t2.png (5 s, complete), A_t_hover_reg.png, A_t_hover_exit.png,
A_t8.png (pointer outside every hotspot: no label).

- Background idx 1 (entry 0) via FUN_00040bb0 (0x2ddcf).
- Hotspot object: `FUN_0002bce0(obj, screen, font 0x95ea0 = idx 0x22 / entry 33, 0, 0x7f46c, 4)`. Table 0x7f46c
  {x0, y0, x1, y1, label_x, label_y, text}:

  | # | rect | label at | text | action (0x2e041 jump table 0x2dd90) |
  |---|---|---|---|---|
  | 0 | -1 (disabled) | (320, 395) | ~TRIALS OF GRIEVANCE | (would be) state 7, ctx 2 |
  | 1 | (117,71)-(530,419) | (320, 318) | ~REGISTRATION | state 15, ctx 0 |
  | 2 | -1 (disabled) | (124, 374) | ~JADE FALCON CLAN HALL | (would be) state 15, ctx 1 |
  | 3 | (0,420)-(639,479) | (320, 455) | ~EXIT | dialog "Embrace cowardice?#Yes|No" (0x77774, FUN_00027a40 default 1 = No); Yes (0) -> -3 |

  Actions need the left button (FUN_0003b1d0 == 1 = mouse +0x11). The label of the hotspot under the pointer is drawn in
  the default font (entry 33, height 10), centred on label_x (the '~' prefix; same as base) - captured: "REGISTRATION"
  centred at x 320, baseline area y 318-326 (A_t_hover_reg.png), "EXIT" at (320, 455) (A_t_hover_exit.png). The
  disabled hotspots never react (their rects are -1); the REGISTRATION rect covers the whole emblem, so hovering at
  (124, 300) also shows REGISTRATION (A_t_hover_jf.png).
- Videos (`FUN_0003d1a0(slot, name, x, y, flags, fps)`, fps 0 = 10 fps = 100 ms; flag bits from FUN_0003ca50: 2 =
  restore the background under the clip, 4 = hold the last frame, 8 = loop, neither = stop and hide; bit 0 = ended):

  | slot | clip | size | at | flags | when |
  |---|---|---|---|---|---|
  | 1 | aintro01 | 412x68, 65 frames (6.4 s) | (118, 2) | 2 (once) | on entry: "MECHWARRIOR 2" logo forming |
  | 3 | aintro03 | 444x396, 30 frames (3.0 s) | (99, 78) | 6 (once, hold) | on entry: the Ghost Bear X emblem |
  | 2 | aintro02 | 412x68, 40 frames | (118, 2) | 10 (loop) | when slot 1 has ended: slot 1 gets bits 0x20 (frozen) + 1, then aintro02 starts (0x2df8a) |
  | 4 | aintro04 | 408x96, 33 frames (3.3 s) | (122, 256) | 6 (once, hold) | when slot 3 has ended: slot 3 frozen, "GHOST BEAR'S LEGACY" plate (0x2dfe5) |

  Base title: one clip amwlogo1 at (111, 33) (the base shell). The SMKs come from `smk\` (155 rip files = disc files).
- Sounds: idx 0x51 (entry 80): made, then `FUN_0003dc50(snd, 500, 1000, 0, 0x1e)` = volume ramp from 0 to 30, one step
  per 500 calls of FUN_0003dcb0 (called once per loop pass, so CPU-paced), at most 1000 steps; idx 0x7e (entry 125) at
  volume 0x32, started once (0x2decd: `FUN_0003dd70(snd, -1)`). A third getter call (0x2dedf) gets idx -1 (EDX = -1 left
  from the previous call) and fails: no third sound (Ghidra "unreachable block" warnings). Base: idx 0x4a fading in.
- Entry: `FUN_0003b170(mouse, 0x222, 0x1d)` puts the pointer at **(546, 29)** (A_t0.png shows it there). Not in base.
- Loop: clips, mouse, hotspot, then `FUN_00026f10()` (global menu: Esc or the right button) whose result (8 / -3 / -1) is
  the return value when no hotspot acted.
- Music: state 8 stops any song (table value 0x20000000).

---------------------------------------------------------------------------------------------------------------------------

## 4. Global menu `FUN_00026f10` and its pages
Opened from every screen's loop when the event queue returns 3 (Esc, key 0x1b) or the right button is down (mouse +0x15).
Captures: A_menu.png, A_menu_hover_cv.png, A_menu_back*.png.

### 4.1 Menu (diff against base FUN_000258e0)
- Panel: raw idx 7 (entry 6, SHP) drawn at (198, 116) size 244x248 (`FUN_00041170(screen, shp, 0, 0xc6, 0x74, 0xf4, 0xf8)`);
  screen saved first (FUN_000409c0 to 0x95b80) and restored after each page and on exit.
- Items 0x7f93c: **NEW MECHWARRIOR**, COMBAT VARIABLES, COCKPIT CONTROLS, HALL OF HONOR, THE KESHIK, FLEE TO DOS (base item 0
  "NEW ALLEGIANCE"). Centre x 320, y 157 + 33k (minus half the font height), hit box x 198..441 (320 - 122 .. 320 + 121),
  y +-16. Normal items in font idx 0x21 (entry 32), the selected one in idx 0x20 (entry 31) - base 0x1f / 0x1e, i.e. +2.
- Keys: Enter (0x0d) picks; previous = Up 0x8048 / Left 0x804b / **'8' 0x38 / '4' 0x34**; next = Right 0x804d / Down 0x8050
  / **'2' 0x32 / '6' 0x36** (wrapping 0..5). The digit keys are new in GBL (keypad without NumLock); base only arrows.
  Esc or the right button closes the menu (-1 = stay).
- New in GBL: the mouse object's +0x10 flag is cleared while the menu is up (FUN_0003b280) and set again on exit
  (FUN_0003b290), and the panel bitmap is freed on exit (FUN_0004b628).
- Results: 0 NEW MECHWARRIOR -> returns **8** (title; from the title the title restarts, A_newmw.png; from the roster,
  A_roster_menu.png -> A_roster_newmw.png). The item under the pointer is the initial selection; 1-4 run the
  page and return to the menu; 5 FLEE TO DOS -> dialog (below), Yes -> **-3**.

### 4.2 COMBAT VARIABLES (menu case 1, 0x27323) - A_cv.png
- raw idx 8 (entry 7) -> 0x95e84 (slider / knob SHP); background idx 3 (entry 2); value column (375, 124) 258x352 restored
  from the background (FUN_00041070(screen, 0x177, 0x7c, 0x102, 0x160)); clip amwlogo1 at (120, 4) flags 4; re-reads
  MW2SND.CFG (**0x40 bytes**, base 0x3c) and MW2DIF.CFG (8); widget table **0x7e46c** (base 0x7d09c), records of 11 ints
  {x, y, w, h, 0, 0, 0, draw, click, data*, 0}:

  | # | x, y, w | data | draw / click | label in the background | values |
  |---|---|---|---|---|---|
  | 0 | 393, 219, 100 | 0x7e809 MW2DIF +5 | 0x267d0 / 0x269f0 (byte 0..2) | DIFFICULTY | EASY / MEDIUM / HARD |
  | 1 | 393, 239 | 0x7e808 MW2DIF +4 | 0x26860 / 0x26a30 (byte toggle) | HEAT TRACKING | ON / OFF |
  | 2 | 393, 277 | 0x7e7d8 MW2SND +0x14 | 0x26810 / 0x26a10 | OBJECT TEXTURES | ON / OFF |
  | 3 | 393, 297 | 0x7e7dc +0x18 | same | TERRAIN TEXTURES | ON / OFF |
  | 4 | 393, 317 | 0x7e7e0 +0x1c | 0x26950 / 0x26a10 | DISPLAY DETAIL | HIGH / LOW |
  | 5 | 393, 337 | 0x7e7e4 +0x20 | same | OBJECT DENSITY | HIGH / LOW |
  | 6 | 393, 357 | 0x7e7e8 +0x24 | 0x26810 / 0x26a10 | CHUNKY EXPLOSIONS | ON / OFF |
  | 7 | 393, 377 | 0x7e7f0 +0x2c (driver name) | 0x269a0 / 0x26a40 | RESOLUTION | "" = 320x200 -> "vesa480.dll" = 640x480 -> "vesa768.dll" = 1024x768 -> "" |
  | **8** | **393, 397** | **0x7e800 +0x3c** | **0x26810 / 0x26a10** | **TRANSITIONS** (new) | **ON / OFF** (int32 toggled by sete) |
  | 9 | 393, 416 | 0x7e805 MW2DIF +1 | 0x268b0 / 0x26a30 | INVULNERABILITY | OFF / ON (Dishonorable) |
  | 10 | 393, 436 | 0x7e804 MW2DIF +0 | same | UNLIMITED AMMO | OFF / ON |
  | 11 | 393, 456 | 0x7e807 MW2DIF +3 | same | NO COLLISION DAMAGE | OFF / ON |
  | 12 | 335, 128, 285x78 | - | 0x26a90 | slider panel | |
  | 13-15 | 335, 128 / 152 / 176, 285x21 | 0x7e7d0 +0xc / 0x7e7c8 +4 / 0x7e7cc +8 | 0x26ab0 / 0x26b20 | MUSIC / EFFECTS / VOICE | 16.16 |

  Values drawn in font idx 0x1d (entry 28) centred on x + w/2 (= 443). The value strings of rows 0, 1, 9-11 were not
  re-read (same callbacks pattern and same strings as base: "~OFF" / "ON (Dishonorable)" etc.). Base rows 0-7 and 9-11 have the same coordinates; GBL inserts row 8.
  The DOSBox default shows RESOLUTION 320X200, TRANSITIONS ON (install MW2SND.CFG +0x3c = 1).
- Exit: **Esc or the right button only** (`while (FUN_0002e280(evq) != 3)`); base left on any key (`while (poll == 0)`).
  Both files are written back (MW2SND 0x40, MW2DIF 8).

### 4.3 COCKPIT CONTROLS (case 2) - `FUN_00021f40` (base FUN_00021020, prefix40 match) - A_cockpit.png
Background idx 4 (entry 3, 0x21f49). The decompile is the base function line for line (normalised diff: one call moved
two lines, mouse field offsets +0x2b/+0x2f -> +0x2c/+0x30 because the mouse object grew by one byte). Device list,
"Current Config: Default Config", ACCEPT / CUSTOM CONFIGURATION / ABORT and the legend match the base screen; the device
names come from GIDDI\ (not in the EXE). No functional difference found.
Traced further for the port (agent S, src/cockpitcfg.c, tools/gbl/gbl_frame.c): drivers numbered in GIDDI\*.DLL order
(FUN_0001a530), every CPC renumbered by name on loading (FUN_00020fc0); CONFIG00.CPC read on entry (FUN_000213e0(0)) and
the selection taken from it (FUN_00020ee0: keyboard + used drivers, at most 4); the keyboard cannot be toggled, a 5th
driver cannot be selected (0x20040; the others drawn grey, 0x1ffe0); ACCEPT writes only after a change (0x20180) and
saves CONFIG00.CPC (FUN_00021d80); the editor is widget table 0x7b8e4 ("Page One".."Page Four", NEXT PAGE, only the
selected drivers listed). DOSBox runs of each GBL driver: tests/gbl/cockpit/ (INPUT.MAP + CONFIG00.CPC).

### 4.4 HALL OF HONOR (case 3) - `FUN_0002b910` (base FUN_00028250) - A_hoh.png (empty), A_hoh_pilot.png (one pilot)
- Background idx 2 (entry 1); clip amwlogo1 at (120, 4) flags 4.
- Sorts the **20** registry records (stride 0x20e; base 20 x 60 bytes) with FUN_0002b8a0 = base FUN_000281e0 unchanged:
  rank (+0x10) desc, then honor (+0x14) desc, then missions done (+0xc) desc. Shows the first 8 in-use ones.
- Headers in font idx 0x1e (entry 29) at y 150 (0x96): **Pilot 0, Rank 200, Honor 325, Kills 400, Hit % 460, Last Mission 520**.
  The base's **Clan column (x 125) is gone**.
- Rows in the default font (entry 33) from y 182 (0xb6) step 16: name (+0x28), rank title `0x7e790[+0x10]` (Cadet,
  Mechwarrior, Star Commander, Star Captain, Star Colonel, Nova Commander, Galaxy Commander - base 0x7d394 had nine
  titles from Mechwarrior to Khan), honor "%d" (+0x14), kills "%d" (+0x18), hit % "%d%%" = +0x1c * 100 / +0x20 or "-" when
  +0x20 == 0, last mission = campaign title `0x8017c[+8][(+0xc - 1) * 9 + 5]` or "----" when missions done is 0.
- Any key, left or right button returns to the menu. DOSBox with one pilot (TESTC, honor 1857, no missions): row
  "TESTC  Cadet  1857  0  -  ----" (A_hoh_pilot.png) - matches the trace.

### 4.5 THE KESHIK (case 4) - `FUN_00022320` (base FUN_00021400) - A_keshik3/15/40.png
- Background idx 5 (entry 4) "THE KESHIK"; amwlogo1 at (120, 4) flags 4 (the "MECHWARRIOR 2" logo).
- **New credits text**: table 0x7da3c, **642 lines** (base 0x7ca80, 388). Lines 0-255: the GBL team ("MECHWARRIOR 2: Ghost
  Bear's Legacy", Associate Producer Jack Mamais, Game Designed By Chad Findley ...); 256-641: the base MW2 credits under the
  heading "<MECHWARRIOR 2: 31ST CENTURY COMBAT" (the base's own opening lines "ACTIVISION presents" / "MECHWARRIOR 2" /
  "based upon..." / "FASA CORPORATION" dropped; Keith Alexander moved, David Avery, Robert Berger, Chris Campbell, John
  Cibulski, Mark Harwood added to a thanks list). Full table: **/home/claude/gbl/spec/A_frame_credits.h**.
  Markup changed: GBL writes the role in plain text and the name with '>' (base: '>' roles, plain names).
- Fonts: '<' idx 0x1d (entry 28), '>' idx 0x1e (entry 29), '~' idx 0x1d with colour map {0 -> 0xff transparent, 1 -> 0x10}
  (no '~' line in the GBL text), plain lines idx 0x22 (entry 33). Centred at x 320. Base: '<' / '~' 0x1b, '>' 0x1c, plain
  0x20 (= +2).
- Scroll: line k at y = top + 20k, drawn when 0x68 < y <= 0x1cb; window (0, 125)-(640, 460) restored from the background
  each pass, bands (0, 105) 640x20 and (0, 460) 640x20 restored after the text; `top` starts at 0x1cc (460) and drops
  1 px per pass of the outer loop (each pass = redraw + two clip/mouse/event polls) - CPU-paced like the base (no timer,
  no retrace wait). Measured under our DOSBox config: line 40 at y 128 at about 3.5 s after the click (A_keshik3.png),
  i.e. roughly 300 px/s - the port should keep its pinned-speed approach (SHELL / mw2shell.c the_keshik: 79 px/s at
  26800 cycles); the speed is not a property of the original code.
- Removed from the base: the date check (base: if the date is not in Nov/Dec 1995 ... swaps one line for "SHELL
  PROGRAMMING by REDLINE GAMES"); GBL simply has ">SHELL PROGRAMMING by REDLINE GAMES" at line 300.
- Exit: any key, left or right button.

### 4.6 FLEE TO DOS (case 5, 0x275f3)
Dialog panel raw idx 6 (entry 5) at (172, 165) 296x150 (`FUN_00041170(screen, shp, 2, 0xac, 0xa5, 0x128, 0x96)`), text
"Embrace cowardice?" (0x76a35) centred at x 320 from y 197 in the default font; buttons "Yes" / "No" centred at 320 - 30(n-1)
+ 61k, y 276 (0x114), boxes +-26 x +-13 (frame 0 / 1 of the panel SHP at 64x24); keys: Enter = the default (No), Esc = No,
'Y' / 'N' (first letter, upper-cased). Yes -> -3 (exit), No -> back to the menu (A_flee.png, A_after_flee_no.png). Same
code as base (FUN_000263d0's layout).

---------------------------------------------------------------------------------------------------------------------------

## 5. Registration path (title REGISTRATION)
DOSBox: A_reg_0.png (the click), A_reg_1/2/4/8/14.png (roster within 1 s, nothing else plays).
1. Title hotspot 1 -> state **15**, context 0.
2. State 15 -> `FUN_0003c430(NULL)`: no video -> state **1**.
3. State 1 (`FUN_0001e270`) with flag == 0 (the title always clears it) returns **12** immediately - no hall image,
   no sound.
4. State 12: Sibko roster (`FUN_0003b8d0`, background idx 0x12 = entry 17 "SIBKO ROSTER / ACTIVE PILOT / RECORD" ice room,
   hotspots 0x7eea0 (14), idx 0x51 sound (entry 80) on entry - see agent C's roster spec).
5. ACCEPT in the roster sets the flag (`*flag = 1`, 0x84e0c = 1) and returns 1 -> the clan hall is shown for real.
With a pilot already registered (A_reg_pilot.png) the path is the same: the title clears the flag, so the roster always
comes first. Base: 15 played the landing cinematic mwoland / mjfland, then the hall (with its own first-visit
registration clip).

---------------------------------------------------------------------------------------------------------------------------

## 6. Differences from the base shell (summary)
1. Start context 0 (base 2); bare start (argc != 2) = no-sim mode and no CD letter; resume via "sim" unchanged.
2. MW2SND.CFG 0x40 bytes: +0x3c TRANSITIONS (Combat Variables row at y 397) gating the mission clip lists.
3. Registry record 0x20e bytes (base 60); records 10-19 = the top-ten list (screen 16), cheats checked on 0-9.
4. Music tables carry no songs: the GBL shell plays no menu music.
5. States 0, 11, 13, 14 removed (fatal "Unknown Transition"); 15 = no video; 16 = top-ten screen or "mend"; 17 new
   (FERREGG1); 5 -> 7 in context 2 (base 11 -> 7).
6. Case 10 plays the mission's clip list before launching.
7. DBSOUND.MW2 (44 voice-over WAVs) added, optional.
8. Title: four clips, two sounds, initial pointer (546, 29), only REGISTRATION and EXIT active.
9. Menu: item 0 "NEW MECHWARRIOR"; keypad digits 2/4/6/8 navigate; cursor flag and panel free.
10. Combat Variables leaves on Esc / right button only.
11. Hall of Honor: no Clan column; GBL rank titles.
12. The Keshik: new 642-line text, new markup, no date easter egg.
13. Cheats: XOR-7 list of 8 names (section 1.9) instead of Calvin / Hobbes / FREEBIRTHTOAD.
14. DATABASE: 2 more backgrounds, 22 more WAVs; fonts/cursor/DLL/music +2 (section 2).

## 7. C tables
```c
/* title hotspots 0x7f46c: x0, y0, x1, y1, label_x, label_y, text, next state, context (-1 rect = disabled) */
static const struct { int x0, y0, x1, y1, lx, ly; const char *text; int state, ctx; } GBL_TITLE_HS[4] = {
    { -1,  -1,  -1,  -1, 320, 395, "TRIALS OF GRIEVANCE",    7, 2 },
    {117,  71, 530, 419, 320, 318, "REGISTRATION",          15, 0 },
    { -1,  -1,  -1,  -1, 124, 374, "JADE FALCON CLAN HALL", 15, 1 },
    {  0, 420, 639, 479, 320, 455, "EXIT",                  -3, -1 },   /* via "Embrace cowardice?#Yes|No" */
};
/* title clips: slot, name, x, y, flags (2 restore bg, 4 hold, 8 loop), follow-up of slot */
static const struct { int slot; const char *name; int x, y, flags, after_slot; } GBL_TITLE_CLIPS[4] = {
    {1, "aintro01", 118,   2,  2, -1}, {3, "aintro03",  99,  78, 6, -1},
    {2, "aintro02", 118,   2, 10,  1}, {4, "aintro04", 122, 256, 6,  3},
};
static const char *const GBL_MENU[6] = { "NEW MECHWARRIOR", "COMBAT VARIABLES", "COCKPIT CONTROLS",
                                         "HALL OF HONOR", "THE KESHIK", "FLEE TO DOS" };
static const int GBL_MENU_RESULT_NEW = 8, GBL_MENU_RESULT_FLEE = -3;
/* music tables 0x84db0 (ctx 0) == 0x84d68 (ctx != 0): 0 keep, 0x20000000 stop */
static const unsigned GBL_MUSIC[18] = { 0,0,0,0,0x20000000,0,0,0,0x20000000,0,0x20000000,0,0,0,0,0x20000000,0x20000000,0 };
/* Hall of Honor columns (header y 150 font idx 0x1e; rows y 182 + 16k font idx 0x22, 8 rows) */
static const int GBL_HOH_X[6] = { 0, 200, 325, 400, 460, 520 };  /* Pilot Rank Honor Kills Hit% LastMission */
static const char *const GBL_RANKS[7] = { "Cadet", "Mechwarrior", "Star Commander", "Star Captain",
                                          "Star Colonel", "Nova Commander", "Galaxy Commander" };
```

## 8. Unresolved
- Whether the all-zero music tables are original or a rip edit (no second DOS GBL shell binary to compare; Titanium is
  Windows-only). Audio was not captured in DOSBox (dummy audio driver), so "no menu music" rests on the code.
- RVWINDOW's " -x" in the sim (MW2.EXE option 'X' -> FUN_00049ea0, 0xa8c34) - sim agent.
- IMMORTAL: which debriefing control posts events 0x8011 / 0x8026 (debriefing agent).
- Background entries 14 (no constant user) and the exact users of 16 / 20 / 21 (record tables 0x80240-0x802a0 indexing);
  WAV entries 75, 77, 82, 83, 115, 116, 124, 126 have no constant user (table-driven use not excluded).
- WAV content identity with the base (no base DATABASE.MW2 available); chassis-voice ordering INFERRED.
- The value strings of Combat Variables rows 0, 1, 9-11 were not re-read one by one (same as base by the capture).
- Keshik scroll speed is machine-dependent; only one DOSBox speed measured.
- Screen return values other than the title / menu / 15 / 16 were not fully enumerated (belong to the screen specs);
  the hall returns 0xc, 5, 7, 0x10, 0x11, -3 (decompile constants, unverified per hotspot).
