# B - Pilots, the Sibko roster and the Ghost Bear halls (GBL MW2SHELL.EXE)

Addresses are GBL shell VAs (re/shellgbl.elf, decompile re/shellgbl_decomp.c). "base" = MW2 1995 shell as documented
in mw2port/docs/SHELL.md. DATABASE.MW2 numbering: the code passes a 1-based index ("idx"); entry = idx - 1
(shelltool numbering, `shelltool list`). Fonts: DAT_95ea8 = idx 0x1d (entry 28), DAT_95eac = idx 0x1e (entry 29),
DAT_95eb0 = idx 0x20 (31), DAT_95eb4 = idx 0x21 (32), DAT_95ea0 = DAT_95ea4 = DAT_95eb8 = DAT_95ebc = idx 0x22
(entry 33) (main 0x3a232-0x3a3dc; idx 0x1c is loaded into DAT_95ea4 and then overwritten - never used).
DOSBox captures: /home/claude/gbl/ref/B_r1_* (new pilot, THEWATCHER, delete, hall 0, egg), B_r2_* (crafted roster,
mission list, hall 1 bridge, Esc menu, Combat Simulator), B_r3a_* (won pilot, Finale, TSENG BLOODNAME, cowardice),
B_r3b_* (hall 2 trinary, Hall of Honor), B_r3c_* (double-click accept, LAUNCH OLD MISSION -> briefing).
Pilot files: B_MW2REG_r1.CFG (written by the game, walk 1), B_MW2REG_in2.CFG / _in3_sel1 / _in3_sel3 (crafted inputs,
tools: scratchpad b/mkreg.py), B_MW2REG_r2.CFG, B_MW2REG_r3a.CFG (the game's output after the Bloodname roll),
B_MW2REG_r3c.CFG, B_MW2PRM_r2.CFG.

## 1. MW2REG.CFG - the career file (FUN_0001dfe0 load, FUN_0001e1c0 save)

10520 bytes = 20 records x 0x20e (526) bytes, read/written raw in one fread/fwrite (0x4d7ab / 0x4cb85, size 0x20e,
count 0x14) to/from the array at 0x95ed0. No header, no checksum. Base: 20 x 60 bytes (Wolf 0-9, Falcon 10-19).

GBL meaning of the two halves:
- records 0-9: the Sibko roster (the 10 pilot rows of screen 12). Only these are scanned for cheat names.
- records 10-19: the TSENG BLOODNAME honour roll written by screen 16 (FUN_0001dbd0, section 5) - pilots who
  finished the full 22-mission campaign without a single failed mission. Not shown in the roster (screen 12 only
  uses 0-9; context 1 would use 10-19 but no GBL path enters the roster with context 1). They DO appear in the
  global menu's HALL OF HONOR (capture B_r3b_hallofhonor: KERENSKY / TSENG listed as Cadet, last mission "----").

Record layout (offsets in the 526-byte record; int = int32 LE). Traced: new pilot 0x3c0e4.., delete 0x3bcf2..,
load 0x1e170.., debriefing FUN_00024750 (lines 14785-15290 of the decompile), screen 16, roster panels 0x3b6f0-0x3b860.

| off | type | field | notes |
|---|---|---|---|
| 0x000 | int | in use | 1 = record used |
| 0x004 | int | selected | the active pilot (roster entry picks the first in-use record with it set and clears the others) |
| 0x008 | int | campaign table index | index into 0x8017c (only [0] = Ghost Bear is a campaign); init: 0 for records 0-9, 1 for 10-19 (that is the only nonzero byte pattern in the install's sample file); never set by new/delete; `== 2` tested by the debriefing (Instant Action) |
| 0x00c | int | missions done (campaign position) | index of the next mission in 0x800ac; 22 = "Retired"; THEWATCHER sets 22 at creation |
| 0x010 | int | rank | 0 Cadet, 1 Mechwarrior, 2 Star Commander, 3 Star Captain, 4 Star Colonel, 5 Nova Commander, 6 Galaxy Commander (table 0x7e790; entries 7-11 of that table are clan names Wolf, Jade Falcon, Ghost Bear, Smoke Jaguar, Nova Cat). Debriefing: after a successful trial mission (campaign flag byte 1) rank += objectives succeeded, capped at 6 |
| 0x014 | int | honor | new pilot: round(1000 + rand() * (1/32767) * 1000) (0x3c0e4: fild rand; fmul 3.0518e-5 [0x78b25]; x 1000.0 [0x78b2d]; + 1000) = 1000..2000; debriefing adds the mission honor when MW2MSN status == 2 |
| 0x018 | int | kills | debriefing adds MW2CAR counters |
| 0x01c | int | hits | += MW2CAR u16 +0x15 |
| 0x020 | int | shots | += MW2CAR u16 +0x13 (Hall of Honor Hit % = hits / shots: BRIDGE 40/90 shows 44%) |
| 0x024 | int | (unused) | cleared on new / delete; no read found (the Hall of Honor columns do not use it) |
| 0x028 | char[21] | display name | rebuilt on every load: = name, or sprintf("%s %02d", name, deaths) when deaths != 0 (fmt 0x7359c; capture B_r2: "TRINARY 01"); this is what the roster, RECORD, Hall of Honor and the star slot 0 show |
| 0x03d | char[21] | name | upper case (FUN_00010090 -> toupper 0x4b61a per char); the cheat names are compared with THIS field |
| 0x052 | int | deaths (failed missions, career total) | debriefing status 3: += 1; != 0 => the campaign ends after mission 16 (see rules) and the Bloodname roll is skipped |
| 0x056 | int | failures of the current mission | status 3: += 1, success resets to 0; used with campaign field +5 (retry limit) to pick alternative briefing clips (0xa93b8) - see the debriefing/briefing specs |
| 0x05a | int | hall (location) 0/1/2 | which hall screen 1 shows (section 3); new pilot 0; after a success = campaign record (0x7fba0 + m * 0x39) +9 of the mission just won (m = missions done before the increment): 2 2 0 1 1 0 1 1 1 1 1 0 1 1 1 1 0 0 0 0 0 0 for missions 0..21 |
| 0x05e | 212 bytes | the friendly star | copy of the shell's star 0 (0x856b0, 0x35 dwords) taken at every save (FUN_0001e1c0: `memcpy(pilot + 0x5e, FUN_0003e330(0), 212)`); restored when the pilot is clicked in the roster (FUN_0003e350 / FUN_0003e0b0 per slot) and before a mission replay from the debriefing. Layout below |
| 0x132 | 212 bytes | (unused) | never written by the shell except by the raw fwrite (zero in every file seen) |
| 0x206 | int | MECH PLANT slot limit | written / read by the mission screen 5 (FUN_00028410 lines 17420-17541: star slot editing), cleared on new / delete. INFERRED meaning, see the mission spec |
| 0x20a | int | runtime: text object handle of the roster row | zeroed on load; the save writes whatever pointer is there (B_MW2REG_r1: 44 71 39 00) - garbage, ignore |

Star block (212 bytes, used at pilot + 0x5e, at 0x856b0 / 0x85784 and twice in MW2PRM.CFG):
+0 formation 0-5 (echelonl echelonr lineabreast lineastern vform wedge, 0x85860), +4 slot being edited, +8 max size
(<= 3), +0xc size (<= 3), +0x10 tonnage limit, then 3 slots x 0x40 from +0x14: slot +0 mech index (into 0x84a24,
24-byte records; e.g. 12 = tbr), +4 MEK code char[16] ("tbr00std"; a 3-letter code gets "00std" appended, 0x78c6c),
+0x14 pilot name char[22] (slot 0 = the pilot's display name when screen 1 is entered from the roster),
+0x2a char[22] (second string, empty in all files seen).

Load (FUN_0001dfe0, at shell start and on every roster entry): fopen "MW2REG.CFG" "rb"; missing file => all 20
records zeroed with +8 = (i > 9). Then for each in-use record: +0x20a = 0, rebuild +0x28; for records 0-9 the cheat
names DOOMSDAY / LOVECRAFT / RVWINDOW switch their flag ON (any roster pilot with that name, not only the
selected one). Save (FUN_0001e1c0): fopen "wb" (fail: "Error Opening Career File\n", exit 1), copy star 0 into
the selected pilot (if any), fwrite all 20 records. Saves happen when leaving the roster, after the debriefing,
and after the Bloodname roll.

Campaign rules that use the record (traced):
- "Already won" (hall MISSION COMPUTER, 0x1e7a9): missions done >= 22, or deaths != 0 and missions done >= 17.
- Debriefing success (0x2590x): done += 1; hall = campaign[old done].+9; if deaths != 0 and done > 16 => done = 22.
  So a pilot with any failed mission retires after mission 16 (branSCN1); only flawless pilots fly the five
  Bloodright trials (gui1..guinSCN1, missions 17-21). Flawless and done > 21 => the debriefing goes straight to 16.
- Campaign (0x800ac, 9-byte records {scenario*, trial flag byte, title*}, Ghost Bear only): 0 bassSCN1 "Trial on
  Alshain" (trial) | 1 jess "Defend on Mannedorf" | 2 clai "Patrol on Mannedorf" | 3 newc "Trial on Alshain" | 4 jeeh
  "Defend on Chandler" | 5 tim_ "Strike on Chandler" | 6 andy "Strike on Wolcott" | 7 fost "Circle of Equals" | 8 hoff
  "Raid on Jeanette" | 9 zack "Defend on Jumpship" | 10 sean "Trial of Possession" | 11 fran "Defend on Alshain" |
  12 asah "Trial on Alshain" (trial) | 13 pete "Underwater Strike" | 14 cath "Strike on Moritz" | 15 sach "Strike on
  Moritz 2" | 16 bran "Destroy Orbital Platform" | 17-20 gui1-gui4 "Bloodright Trial 1".."4" | 21 guin "Final
  Bloodright Trial" | 22 (no scenario) "Retired".

Decoded files:
- install MW2REG.CFG (10520): the empty file as the missing-file init writes it - every byte 0 except +8 = 1 in
  records 10-19 (file offsets 5268, 5794, ... 10002).
- B_MW2REG_r1.CFG (walk 1: new pilot "beartest" typed lower case, THEWATCHER created and deleted, hall, egg,
  REGISTER, ACCEPT, MISSION COMPUTER): rec 0 in use, selected, done 0, rank 0, honor 1487, name / display
  "BEARTEST", hall 0, star {formation 0, cur 0, max 3, size 1, ton 100, slots (12 tbr00std "BEARTEST"),
  (0 drw00std "Friend 1"), (1 frm00std "Friend 2")}, +0x20a garbage; rec 1 (the deleted THEWATCHER) all zero.
- B_MW2REG_r3a.CFG (crafted pilot WINNER: done 22, deaths 0, honor 20000; roll had KERENSKY 30000, TSENG 10000):
  rec 10 KERENSKY 30000, rec 11 +0x3d "WINNER" honor 20000 but +0x28 still "TSENG", rec 12 +0x3d "TSENG" 10000 with
  +0x28 empty - screen 16 writes only +0, +0x14 and +0x3d (the display name is rebuilt on the next load, so this
  is harmless; a port must rebuild +0x28 from +0x3d on load too).

## 2. MW2PRM.CFG (704 bytes; FUN_0003aeb0 read at start, FUN_0003af90 write before every shell exit)

| off | size | field |
|---|---|---|
| 0x000 | int | return state (next screen when the shell restarts; -3 = quit) |
| 0x004 | int | context (0 Ghost Bear campaign, 2 Combat Simulator) |
| 0x008 | byte | "pilot accepted" flag (local_18 in the state machine; passed to screen 1 in BL; set by ACCEPT / double-click, cleared on entering the roster and on the title) |
| 0x009 | byte | 0 (written) ; 0x00a-0x00b pad |
| 0x00c | int | 1 if the current star pointer (0x85858) is the friendly star 0x856b0 |
| 0x010 | 212 | friendly star (0x856b0) |
| 0x0e4 | 212 | enemy star (0x85784) |
| 0x1b8 | int | unused (0) |
| 0x1bc | int | selected pilot record index, -1 none (restores DAT_987e8) |
| 0x1c0 | char[256] | MW2.EXE command line: "<scenario> -b=<camo>" (camo buffer 0x84dfc, default "xxxxxxxx.xxx"), + " -x" when the RVWINDOW flag is on, + " -of=<friendly formation>" and, when the enemy star size != 0, " -oe=<enemy formation>" (FUN_0003e3b0, skipped for return state 14 or -3). Quit: "exittos -b=xxxxxxxx.xxx" |

On read, the command line is cut at the first byte <= 0x20 (the scenario name). Install sample: state -3, ctx 0,
friendly star {0,0,3,3,100: tbr00std MechWarrior, drw00std Friend 1, frm00std Friend 2}, enemy star size 0 with
Enemy 1-3, pilot -1, "exittos -b=xxxxxxxx.xxx". The RVWINDOW flag (0xadcec) sits just after the 704 bytes and is
NOT saved - it is recomputed from MW2REG at each start. Base: 536 bytes (no star blocks).

## 3. Other CFG files as the GBL shell uses them
- MW2DIF.CFG, 8 bytes at 0x7e804 (read FUN_00026c20 at start; read/written by the Esc menu FUN_00026f10). Combat
  Variables widgets (records ending at 0x7e490..0x7e674, x 393): +0 UNLIMITED AMMO (y 436, "ON (Dishonorable)"),
  +1 INVULNERABILITY (416, "ON (Dishonorable)"), +2 unknown (1 in the install, no widget), +3 collision damage
  (NO COLLISION DAMAGE y 456, shown inverted: stored 1 = collision damage on = "OFF"), +4 HEAT TRACKING (239),
  +5 DIFFICULTY (219, 0 EASY 1 MEDIUM 2 HARD), +6/+7 unused. The debriefing treats +0 == 1, +1 == 1 or +3 == 0 as
  cheating (no promotion); trial promotions also need +4 == 1. Install: 00 00 01 01 01 01 00 00.
- MW2SND.CFG, 64 bytes at 0x7e7c4 (FUN_00026be0): +4 EFFECTS, +8 VOICE, +0xc MUSIC (16.16 sliders, x 335 y 152 /
  176 / 128), +0x14 OBJECT TEXTURES (277), +0x18 TERRAIN TEXTURES (297), +0x1c DISPLAY DETAIL (317), +0x20 OBJECT
  DENSITY (337), +0x24 CHUNKY EXPLOSIONS (357), +0x2c RESOLUTION char[16] ("" = 320x200, "vesa480.dll" = 640x480,
  "vesa768.dll" = 1024x768; callback 0x269a0 tests byte 0 and byte 4 == '4') (377), +0x3c TRANSITIONS (397, NEW in
  GBL). +0 = 0x10000, +0x10 = 15, +0x28 = 8 in the install: no widget (unknown). (Capture B_r3b_hall2_mcclick shows
  the page: RESOLUTION 320X200, TRANSITIONS ON.)
- MW2MSN.CFG (read by the debriefing, 0x9d4 = 2516 bytes): "MW2M", int count, int start, int end, int status
  (2 success / 3 failed / 4 time), then 48 x 52-byte objectives {succeeded, type, 0, time or -1, primary flag,
  text[32]} - same as the base. Install sample: count 5, status 0, "Destroy Enemy Mech Star 1..5" (type 1).
  MW2CAR.CFG: 0x50 bytes of u16 counters (debriefing). Details belong to the debriefing spec.

## 4. Screen 12 - Sibko roster / registration (FUN_0003b8d0)

Entry (context in EDX; GBL only ever uses 0): FUN_0003c660("agball02") plays SMK/agball02.smk full screen
(640x480, 7 frames, 67 ms, not skippable) - the zoom into the plasma orb; srand (0x4da8b, INFERRED srand(time));
background idx from 0x80208 + ctx * 16 (ctx 0: idx 18 = entry 17, "SIBKO ROSTER / ACTIVE PILOT / RECORD" over
the orb room); hotspots 0x80200 + ctx * 16 -> 0x7eea0, 14 (font DAT_95ea0 = entry 33); FUN_0001dfe0 reloads the
pilots; pilot rows for records ctx 0 -> 0-9 (ctx 1 would be 10-19; table entry 1 is empty). Sound: DATABASE idx
0x51 (entry 80, 11 kHz 8-bit WAV) looped (0x3dd40 loop count 0) at the sample's default volume 5 (the object's
constructor 0x3db50 sets +0x10 = 5 and the roster never calls the volume setter) - base: same sound at 0x1e.
Music: per-screen table 0x84d68 / 0x84db0 [12] = 0 = unchanged.
The "pilot accepted" flag is cleared. The active pilot = first in-use selected record (others' +4 cleared).

Pilot rows: each in-use record's display name at (42, 92 + 35 i), font DAT_95ea8 (entry 28), white (capture glyph
tops y 94 + 35 i, x 43). Panels (FUN_0002e870 show / FUN_0002e980 hide; 11-int records {x, y, w, h, .., obj, make
callback, click callback, index / text}; y 0x8000xxyy = previous y + ((xx >> 4) * previous height) + (yy & 15)):

RECORD 0x84e10 (shown with a pilot): name "~%s" of +0x28 (468, 92) font 28 | "~RANK" (468, 209) font 29 (callback
0x3b6d0, DAT_95eac) | rank title (468, +1 h + 3) font 28 | "~HONOR" (468, +2 h + 2) | honor "~%d" (+1 h + 3) |
"~MISSION" | mission title "~%s" = campaign[ctx][done].title (0x3b860). Measured glyph rows (B_r3a_roster): name
94-105, RANK 215-221, value 230-241, HONOR 264-272, value 281-292, MISSION 317-323, value 332-343; all centred on
x 468.

LAUNCH OLD MISSION list 0x84f70: name (468, 92); "~Select Mission" (388, 200) and (538, 200) font 29; 22 items
(x 338 / 488, w 100, index 0-10 / 11-21, callback 0x3b6f0 font DAT_95ea4 = entry 33, centred x + 50): column 1
first item y = 200 + 2 h(heading) + 15 = 247, then previous y + h + 2; column 2 likewise under its heading. An item
is drawn only when index < missions done and index < 17, or always when the name (+0x3d) is THEWATCHER (all 22).
QUIRK (original): FUN_0002e870 writes the computed y / h back into the table, so the layout freezes the first time
the list is shown in a session; an item that was hidden then inherits the previous record's height. Measured
(B_r2_list22, first shown for a 3-mission pilot): column 1 rows at y 247, 259, 271 ... step 12; column 2 at 247,
265, 283 ... step 18 (its items inherited the heading's height 16). The PILOT INFO label overlaps column 2's last
items (B_r2_list22). Items are plain white, no hover state.

Hotspots 0x7eea0 (x0, y0, x1, y1, label x, label y, text; "<" = label always drawn, "~" = centred; labels grey,
white under the cursor; disabled ones (0x2c220) are not drawn):

| # | box | label | action |
|---|---|---|---|
| 0-9 | (32, 83 + 35 i)-(297, 116 + 35 i) (exact: y0 83 117 152 187 222 257 292 327 362 398, y1 116 151 186 221 256 291 326 361 397 432) | - | empty row: hide panels, disable 10-13, no active pilot, text entry at (42, 92 + 35 i) (FUN_0002e4a0: font 28, max 17 chars, max width 300 px incl. the "_" cursor; chars 0x20-0x7f except '~' that the font has; Backspace; Enter or a mouse click accept; Esc ends too and the typed text is still used - INFERRED from the decompile, the roster only tests name[0] != 0); non-empty result => new pilot (below). Pilot row: select it (all +4 = 0, its +4 = 1, its star restored); a second click within 200 timer ticks (0x3b2f0, 0xc9) = ACCEPT; otherwise enable 10, 11, 12 if done > 0, disable 13, show RECORD |
| 10 | (294,450)-(393,474) | "<~ACCEPT" (344, 455) | with a pilot: accepted flag = 1, next state 1 |
| 11 | (20,450)-(221,474) | "<~DELETE MECHWARRIOR" (121, 455) | dialog "Terminate MechWarrior?#Yes|No" (default No); Yes: cheat flag off for DOOMSDAY / LOVECRAFT / RVWINDOW, clear +0, +4, +0xc..+0x24, +0x52, +0x5a, +0x206, both names; +8, +0x56 and the star stay; no active pilot, panels hidden, 10-13 disabled |
| 12 | (373,403)-(562,427) | "<~LAUNCH OLD MISSION" (468, 408) | disable 12, enable 13, hide RECORD, show the list |
| 13 | (418,403)-(517,427) | "<~PILOT INFO" (468, 408) | enable 12 if done > 0, disable 13, hide the list, show RECORD |

Entering with a pilot: RECORD shown, 13 disabled, 12 disabled when done == 0. No pilot: 10-13 disabled.
New pilot (0x3c0xx): in use, missions done 0 (22 if THEWATCHER), rank 0, honor random 1000-2000, kills / hits /
shots / +0x24 / deaths / current failures / hall = 0, selected (others cleared); DOOMSDAY / LOVECRAFT / RVWINDOW
flags on; ACCEPT and DELETE enabled, RECORD shown. LAUNCH OLD MISSION is NOT enabled for a new pilot even with 22
missions (B_r1_watcher); it appears after the pilot is clicked again or on the next roster entry.
Mission list click (checked before the hotspots, 0x34320: x0 <= x < x0 + w, y <= y < y + h, list visible): item k
< missions done and (k < 17 or THEWATCHER): scenario = campaign[ctx][k], DAT_7e840 = k, star 0 = {0,0,3,1,100},
FUN_0002c810(scenario) loads the mission's default star (BRF2 SDSC), enemy star emptied, slot 0 = pilot +0x28;
next state 5 (the mission / briefing screen) with DAT_95e88 = 1, so the launch (state 10) returns to 12 and the
debriefing is skipped. Base: the item launched state 10 directly, no briefing. Captured: B_r3c_lom_launch1/2
(BRIDGE, item 0 -> the "Trial on Alshain" briefing).
Exit: every exit saves MW2REG.CFG (FUN_0001e1c0) and frees the row texts. Next states: 1 (ACCEPT / double-click),
5 (old mission), or the Esc menu's result (FUN_00026f10).

Cheat pilot names (8 strings at 0x7e80c.., stored XOR 7 and decoded in place + upper-cased at start, main
0x3940a): compared (strcmp, 0x4be40) with name +0x3d.

| stored | name | effect (traced) |
|---|---|---|
| CHHJTCF^ | DOOMSDAY | flag 0xa9bd0 (0x2e7c0(0x80, on)): Mech Lab weapon list entry 30 "Nuke" is offered only with it (0x302b9; also 0x33819) |
| KHQBDUFAS | LOVECRAFT | flag 0xa9bd4 (0x81): weapon entry 32 "Torpedo" offered only with it (0x302cf, 0x33828) |
| UQPNICHP | RVWINDOW | 0xadcec = 1: " -x" on the MW2.EXE command line. Sim option X (0x44e30 parser, letter - 'B' = 0x16): FUN_00049ea0 - INT 10h AX=1Bxx probe for a monochrome adapter, clears 2000 words of text memory, sets 0xa8c34 = a debug output window on a second (mono) monitor (INFERRED) |
| NJJHUSFK | IMMORTAL | debriefing (FUN_00024750 0x25dcb): key code 0x8011 (Alt+W, INFERRED: 0x8000 + scan 0x11) restores the pilot record and forces a success; 0x8026 (Alt+L) forces a failure |
| SHKLBNI | TOLKEIN | decoded but never referenced in the shell (no xref) |
| PNCHP | WIDOW | Combat Simulator (FUN_0002cc00 / 0x2cc0c), compared with the STAR slot 0 pilot name: mech count 0x1f (31) instead of 0x1d |
| KNSSKB@R^ | LITTLEGUY | the same, mech count 0x1e (30) |
| SOBPFSDOBU | THEWATCHER | roster: new pilot starts with 22 missions done (Retired); the LAUNCH OLD MISSION list shows and accepts all 22 missions |

Base comparison (SHELL.md "Sibko roster"): base had 15 hotspots with 0 NEW ALLEGIANCE and two clans (records 0-9 /
10-19, background slots 17 / 24), RECORD at the same x 468 with the same label / value scheme, 16 old missions in one
column, Launch -> state 10 with no briefing. GBL: 14 hotspots (rows are 0-9), one roster, two-column 22-mission list,
launch through the briefing, new cheats (base: FREEBIRTHTOAD / FERRARI live elsewhere; GBL has neither string).

## 5. Screen 1 - the Ghost Bear hall (FUN_0001e270)

Entered only with the accepted flag (BL); without it -> 12 at once. When entered from 12 the state machine sets
star 0 = {formation 0, cur 0, max 3, size 1, ton 100} and slot 0's pilot = display name (0x3975f). The hall index
is the pilot's +0x5a (0, 1, 2) - NOT the shell context. Per-hall record 0x80180 + hall * 16 = {hotspots, 5, bg idx,
38 (unused)}: hall 0 0x7e964 bg idx 11 (entry 10: the ice cave with the plasma orb), hall 1 0x7ea50 idx 12 (entry
11: DropShip bridge), hall 2 0x7eb0c idx 13 (entry 12: the 72nd Assault Trinary barracks). Hotspot labels in font
DAT_95ea0 (entry 33), shown on hover only, white; glyph top = label y + 6 (measured).

Hotspots (hall 0 / hall 1 / hall 2):

| # | hall 0 | hall 1 | hall 2 | click |
|---|---|---|---|---|
| 0 MISSION COMPUTER | (252,244)-(371,377) label (243,350) left-aligned | (257,179)-(374,248) (250,255) | (190,97)-(450,306) "~" (305,304) | the click clip (hall 0, 2), then "already won" test: -> 16 after the dialog "This pilot has|already won the game.#Finale", else -> 5 |
| 1 EXIT | (67,288)-(114,372) "~EXIT" (84,298) | (491,0)-(639,479) (595,290) | (0,420)-(639,479) (320,455) | "Embrace cowardice?#Yes|No" (default No); Yes -> -3 = quit to DOS (writes MW2PRM "exittos") |
| 2 REGISTER | (149,224)-(219,286) (153,293) - the orb | (-1,-1,-1,-1) none | none | -> 12 |
| 3 COMBAT SIMULATOR | (443,286)-(481,364) (406,287) | (0,0)-(147,479) (13,290) | (23,121)-(130,376) (1,156) | -> 7 (Combat Simulator, B_r2_combatsim) |
| 4 (no label) | (97,239)-(113,263) the left lamp | (439,259)-(460,276) small monitor | (472,49)-(500,162) poster | -> 17 (developer easter egg) |

Videos (table 0x7ebe8 + hall * 8 = {count, list}; list records {name, x, y, flags}; slot = list index + 1; flags
bits: 1 paused, 2 restore background under it, 8 loop, 0x20 hidden, 0x40 (always set), 0x200 at the end rewind
to frame 1 and pause (instead of closing); without 8 / 0x200 the slot closes at the end leaving its last frame):

| hall | slot | clip | pos | size / frames / ms | flags | use |
|---|---|---|---|---|---|---|
| 0 | 1 | agbcom01 | (256,359) | 120x20 / 7 / 99 | 0x6a | shown + playing at entry (loop) |
| 0 | 2 | agball01 | (130,200) | 108x100 / 30 / 67 | 0x261 | the orb; restarted every frame it is idle => loops |
| 0 | 3 | agbcom02 | (252,244) | 124x132 / 24 / 99 | 0x261 | first hover of MISSION COMPUTER: shown + played once, with sound idx 0x79 |
| 0 | 4 | agbcom03 | (252,244) | 124x132 / 15 / 99 | 0x69 | when 3 has finished: 3 hidden, 4 shown looping (the rotating emblem) |
| 0 | 5 | agbcom04 | (252,244) | 124x132 / 17 / 99 | 0x63 | MISSION COMPUTER click: 1-4 and 6 paused + hidden, 5 played to the end (0x3d410, blocking) |
| 0 | 6 | agbchmc | (438,295) | 60x76 / 15 / 67 | 0x6a | shown + looping at entry |
| 1 | 1-3 | agbbr001 / 002 / 003 | (132,214) (252,181) (419,221) | 120x76 / 168x104 / 164x88, 301 frames, 71 ms | 0x4a | visible and looping from load; hall 1 has no hover or click clips |
| 2 | 1 | agbtb001 | (160,90) | 300x228 / 13 / 100 | 0x6a | shown + looping at entry |
| 2 | 2 | agbtb002 | (160,90) | 300x228 / 32 / 99 | 0x62 | first hover of MISSION COMPUTER: 1 paused + hidden, 2 played once, sound 0x79 |
| 2 | 3 | agbtb003 | (160,90) | 300x228 / 15 / 99 | 0x6a | after 2: 2 hidden, 3 looping |
| 2 | 4 | agbtb004 | (160,90) | 300x228 / 24 / 99 | 0x6a | click: 1-3 hidden, 4 played once to the end (0x3d410 clears the loop bit) |
| 2 | 5 | agbtb005 | (380,382) | 116x24 / 106 / 67 | 0x4a | visible looping |

(The DOS rip lacks agbbr001-003
and agbtb002 - the Titanium disc's /SMK has them; FUN_0003d1a0 just fails the slot when a file is missing. My hall
1/2 captures used the disc files copied in.)
Sounds (looping ambient: 0x3def0 volume, 0x3dd40 loop forever, 0x3dd70 play): hall 0 idx 0x51 (entry 80) volume
10; hall 1 idx 0x7b (entry 122) volume 40; hall 2 idx 0x7c (entry 123) volume 40. MISSION COMPUTER hover (halls 0
and 2, once per visit): idx 0x79 (entry 120, 22 kHz) volume 100. Music table [1] = 0 (unchanged).
Already-won rule (0x1e7a9): done >= 22, or deaths != 0 and done >= 17 => dialog then 16; else 5.
Captures: hall 0 B_r1_hall_a/b, hovers B_r1_hover_mc1 (agbcom02 beam) / mc2 (agbcom03 emblem) / exit / register /
cs / egg, click B_r1_mc_click (-> screen 5 intro "INITIALIZING UPLINK"); hall 1 B_r2_hall1_*; hall 2 B_r3b_hall2_*;
won dialog B_r3a_won_dialog (box at (172,165) 296x150, Finale button centre ~(322,275)); B_r3a_cowardice.

Base comparison (SHELL.md "Clan halls", base FUN_0001dbe0): base = one hall per clan with CADET TRAINING (-> 14),
ARCHIVE HOLOPROJECTOR (-> 5 archive), READY ROOM (-> 11, or the won message -> 16 at 16 missions), REGISTER (clip
-> 12), EXIT (top strip -> title 8). GBL: no Cadet Training, no Archive, no Ready Room in the hall; MISSION
COMPUTER goes straight to the mission screen 5; EXIT quits to DOS through "Embrace cowardice?"; COMBAT SIMULATOR is
new; the hall changes with the campaign (+0x5a); hidden egg hotspot 4. No REGISTER in halls 1 and 2 - the way back
to the roster there is the Esc menu (NEW MECHWARRIOR goes to the title 8, B_r2_newmw, then REGISTRATION).

## 6. Screen 16 - TSENG BLOODNAME roll (FUN_0001dbd0) and the ending

State machine (0x39b30 / 0x39b4f): state 16: deaths == 0 and done != 17 -> FUN_0001dbd0; else play "mend" (SMK, full
screen, skippable, FUN_0003c430) and -> 1. So a pilot that retired after mission 16 with failures sees the ending
clip again; a flawless pilot after guinSCN1 gets the roll (the debriefing also sends him to 16 directly).
FUN_0001dbd0: background idx 23 (entry 22, "TSENG BLOODNAME" with DNA helices), hotspots 0x801e0 (11; font entry 33):
0 (249,406)-(390,438) = the EXIT plate (no label), 1-10 the rows (32,83+..)-(297,..) labels (198, 143 + 23 k) -
unused. Records 10-19 in use are listed with the current pilot appended unless an entry with the same name (+0x3d)
AND honor exists; sorted by honor descending (simple exchange sort, 25-byte temp records {name[21], honor}); the top
10 written back (+0 = 1, +0x3d, +0x14), saved; names drawn at (198, 145 + 23 k), font DAT_95ea4 (entry 33),
identity colour map. Click on 0 -> 1 (the hall). Capture B_r3a_screen16 (KERENSKY / WINNER / TSENG).

## 7. Screen 17 - developer easter egg (FUN_0004b1a0, "FERREGG1")

Reached only from the hall's unlabelled hotspot 4. Returns to the previous state (1). First visit loads 8 text pages
(FUN_0001c600 into 0xaf9d8[]: MW2.PRJ BWD records FERREGG1 MAMAEGG1 BURTEGG1 FRANEGG1 MATSEGG1 FINDEGG1 KINNEGG1
CANDEGG1 (table 0x87794), chunk ORDR, box (85,27) 469x403, font entry 33). Background per hall: idx 0x18 / 0x19 /
0x1a (entries 23 / 24 / 25 = the team photographed in hall 0 / 1 / 2). Hotspots (9; font entry 33, '~' labels on
hover): hall 0 0x877dc, hall 1 0x87a0c, hall 2 0x878f4:

| # | label | hall 0 | hall 1 | hall 2 |
|---|---|---|---|---|
| 0 | <~EXIT (320,455) | (0,420)-(639,479) | same | same |
| 1 | ~Bill Ferrer | (228,265)-(379,398) (307,376) | (258,248)-(383,419) (303,372) | (226,262)-(349,419) (292,342) |
| 2 | ~Jack Mamais | (77,133)-(163,243) (122,212) | (30,99)-(148,247) (100,166) | (107,159)-(190,261) (151,214) |
| 3 | ~Jack Burton | (486,152)-(568,270) (536,208) | (385,70)-(566,247) (444,164) | (350,145)-(454,261) (387,214) |
| 4 | ~JJ Franzen | (43,243)-(140,383) (87,312) | (51,248)-(205,419) (132,347) | (108,262)-(225,408) (176,335) |
| 5 | ~Danny Matson | (0,133)-(77,236) (55,192) | (150,58)-(257,247) (198,160) | (191,144)-(225,261) (220,198) |
| 6 | ~Chad Findley | (570,152)-(614,273) (590,238) | (567,90)-(639,419) (569,195) | (455,164)-(521,376) (491,230) |
| 7 | ~Sean Kinnear | (255,74)-(379,264) (323,159) | (258,69)-(383,247) (321,169) | (226,147)-(349,261) (302,208) |
| 8 | ~Matt Candler | (493,274)-(614,386) (546,347) | (385,248)-(566,419) (466,361) | (350,262)-(454,398) (401,334) |

Click 1-8: background idx 0x16 (entry 21, "DETAILS" frame), the paged text viewer FUN_0002b0e0 with page k-1,
hotspots 0x802a0 (4): EXIT (278,437)-(361,465) (the others (116,437)-(204,465), (436,437)-(524,465), (510,550)-..
are the viewer's page buttons, not drawn for a one-page text); viewer returns 5 = stay, 8 / -3 propagate; then the
egg screen is rebuilt. Captures B_r1_hover_egg, B_r1_egg, B_r1_egg_hover_ferrer, B_r1_egg_ferrer, B_r1_egg_back.

Archive: GBL has no ARCHWO/ARCHJF file and no archive hotspot. FUN_0002b0e0 is only the generic paged viewer
(callers: debriefing 0x263e7, mission screen 0x29130 / 0x2a51c, egg 0x4b43d); nothing in the hall reaches it.

## 8. Ready-to-use C tables

```c
typedef struct { int x0, y0, x1, y1, lx, ly; const char *text; } gbl_hot;
/* roster 0x7eea0 */
static const gbl_hot gbl_roster_hot[14] = {
 {32,83,297,116,41,91,""},{32,117,297,151,41,125,""},{32,152,297,186,41,160,""},{32,187,297,221,41,195,""},
 {32,222,297,256,41,230,""},{32,257,297,291,41,265,""},{32,292,297,326,41,300,""},{32,327,297,361,41,335,""},
 {32,362,297,397,41,370,""},{32,398,297,432,41,406,""},
 {294,450,393,474,344,455,"<~ACCEPT"},{20,450,221,474,121,455,"<~DELETE MECHWARRIOR"},
 {373,403,562,427,468,408,"<~LAUNCH OLD MISSION"},{418,403,517,427,468,408,"<~PILOT INFO"} };
/* halls 0x7e964 / 0x7ea50 / 0x7eb0c; background idx 11/12/13; ambient idx 0x51 vol 10 / 0x7b vol 40 / 0x7c vol 40 */
static const gbl_hot gbl_hall_hot[3][5] = {
 {{252,244,371,377,243,350,"MISSION COMPUTER"},{67,288,114,372,84,298,"~EXIT"},{149,224,219,286,153,293,"REGISTER"},
  {443,286,481,364,406,287,"COMBAT SIMULATOR"},{97,239,113,263,0,0,""}},
 {{257,179,374,248,250,255,"MISSION COMPUTER"},{491,0,639,479,595,290,"~EXIT"},{-1,-1,-1,-1,150,200,""},
  {0,0,147,479,13,290,"COMBAT SIMULATOR"},{439,259,460,276,0,0,""}},
 {{190,97,450,306,305,304,"~MISSION COMPUTER"},{0,420,639,479,320,455,"~EXIT"},{-1,-1,-1,-1,150,200,""},
  {23,121,130,376,1,156,"COMBAT SIMULATOR"},{472,49,500,162,0,0,""}} };
static const int gbl_hall_next[5] = { 5 /* or 16 */, -3 /* after Yes */, 12, 7, 17 };
typedef struct { const char *name; int x, y, flags; } gbl_clip;
static const gbl_clip gbl_hall0_clips[6] = { {"agbcom01",256,359,0x6a},{"agball01",130,200,0x261},
 {"agbcom02",252,244,0x261},{"agbcom03",252,244,0x69},{"agbcom04",252,244,0x63},{"agbchmc",438,295,0x6a} };
static const gbl_clip gbl_hall1_clips[3] = { {"agbbr001",132,214,0x4a},{"agbbr002",252,181,0x4a},{"agbbr003",419,221,0x4a} };
static const gbl_clip gbl_hall2_clips[5] = { {"agbtb001",160,90,0x6a},{"agbtb002",160,90,0x62},{"agbtb003",160,90,0x6a},
 {"agbtb004",160,90,0x6a},{"agbtb005",380,382,0x4a} };
static const int gbl_hall_after_win[22] = {2,2,0,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0,0,0,0}; /* 0x7fba0 + m*0x39 + 9 */
static const char *gbl_rank[7] = {"Cadet","Mechwarrior","Star Commander","Star Captain","Star Colonel",
 "Nova Commander","Galaxy Commander"};
static const char *gbl_cheats[8] = {"DOOMSDAY","LOVECRAFT","RVWINDOW","IMMORTAL","TOLKEIN","WIDOW","LITTLEGUY","THEWATCHER"};
static const char *gbl_egg_pages[8] = {"FERREGG1","MAMAEGG1","BURTEGG1","FRANEGG1","MATSEGG1","FINDEGG1","KINNEGG1","CANDEGG1"};
/* screen 16 0x801e0: background idx 23; EXIT {249,406,390,438}; names at (198, 145 + 23k), font entry 33 */
#pragma pack(push,1)
typedef struct {                 /* MW2REG.CFG record, 0x20e bytes, 20 per file */
    int32_t in_use, selected, campaign, done, rank, honor, kills, hits, shots, unused24;
    char display[21];            /* 0x28: name or "name %02d" (deaths) - rebuilt on load */
    char name[21];               /* 0x3d */
    int32_t deaths, cur_fail, hall;   /* 0x52 0x56 0x5a */
    uint8_t star[212];           /* 0x5e */
    uint8_t unused132[212];      /* 0x132 */
    int32_t slot_limit;          /* 0x206 (mission screen) */
    int32_t runtime_text;        /* 0x20a garbage */
} gbl_pilot;
#pragma pack(pop)
```

## Unresolved
- Roster sound volume: 5/127 is the sample object's constructor default (0x3db50 +0x10 = 5); not confirmed by ear.
- Double-click window "200 ticks": the unit of the timer 0x41bb8 is not traced (INFERRED ms).
- MW2REG +0x206 meaning (mission screen MECH PLANT logic) and +0x56 with campaign field +5 (alternative briefings)
  belong to the mission / debriefing specs; only their storage is pinned here.
- MW2DIF +2, MW2SND +0 / +0x10 / +0x28: no widget found; values from the install only.
- RVWINDOW / sim -x: purpose INFERRED (secondary monochrome debug display) from the INT 10h 1Bh probe.
- IMMORTAL keys 0x8011 / 0x8026 assumed Alt+W / Alt+L (0x8000 | scan code, the mouse-key code style of 0x3b2f0);
  not tried in DOSBox.
- Hall of Honor ordering (B_r3b_hallofhonor) is not honor order (WINNER 20000, BRIDGE 3100, TRINARY 2200,
  KERENSKY 30000, TSENG 10000, THEWATCHER 1500) and it includes records 10-19; its sort was not traced (global menu).
- DOOMSDAY / LOVECRAFT weapon entries: the gate is traced (0x302b9 / 0x302cf, entry index 0x1e / 0x20 of 0x80af8)
  but not seen in the Mech Lab in DOSBox.
- Videos agbbr001-003 / agbtb002 missing from the rip: the DOS behaviour without them was not captured.
