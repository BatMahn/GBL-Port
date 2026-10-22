# D - GBL Instant Action ("Combat Simulator", screen 7) and the Mech Lab (screen 9)

Agent D, 2026-10-21. Binary: GBL MW2SHELL.EXE (re/shellgbl_decomp.c, shellgbl.asm; data read with tools/sh.py).
Base references: the base shell (MW2SHELL.EXE) ("Instant Action", "Star Configuration and the Mech Lab", "Mech Lab ... decoding",
"CUSTOMIZE"), base FUN_00029680 / FUN_000339e0 (handoff re/mw2shell_decomp.c), the port's run_mech_lab / run_customize.
Markers: TRACED (address), MEASURED (DOSBox capture in /home/claude/gbl/ref), ASSUMED / INFERRED (with the reason).
Call convention used below: clip(slot, name, x, y, flags, fps) = FUN_0003d1a0 (fps 0 -> 10; it tries smk\<name>.shp,
then .smk, first with the CD-drive prefix; a missing file leaves the slot empty, no error). sound(arg, vol) =
FUN_00022920(DATABASE, arg) + FUN_0003db50 + FUN_0003def0(vol) (arg as passed; the base port's shell_sound convention).
text(font, x, y, s, remap) = FUN_00027f00. Background = FUN_00040bb0(gfx, DATABASE, slot) (slot = DATABASE entry + 1, as base).

-------------------------------------------------------------------------------------------------------------------
## 0. Reachability of screen 7 - it IS reachable, from the clan hall (TRACED + MEASURED)
- Title (FUN_0002dda0, hotspots 0x7f46c, 4): 0 "~TRIALS OF GRIEVANCE" is (-1,-1)-(-1,-1) (label 320,395; -> 7 ctx 2):
  never hit - FUN_0002bf30 tests x0 <= x <= x1 inclusive, the pointer never reaches -1, and hotspot sets are mouse only.
  2 "~JADE FALCON CLAN HALL" is likewise disabled. Live: 1 "~REGISTRATION" (117,71)-(530,419) label (320,318) -> 15
  ctx 0; 3 "~EXIT" (0,420)-(639,479) label (320,455) -> "Embrace cowardice?".
- The route is the CLAN HALL (screen 1, FUN_0001e270) hotspot 3 "COMBAT SIMULATOR" -> return 7. Hall sets 0x80180 +
  hall x 16 = {table, count 5, background slot, 38}; hall = pilot record +0x5a (0..2):
  | hall | table | bg slot | 0 MISSION COMPUTER -> 5 | 1 ~EXIT -> "Embrace cowardice?" -> -3 | 2 REGISTER -> 12 | 3 COMBAT SIMULATOR -> 7 | 4 -> 0x11 (FERREGG1) |
  |---|---|---|---|---|---|---|---|
  | 0 | 0x7e964 | 11 | (252,244)-(371,377) lbl (243,350) | (67,288)-(114,372) lbl (84,298) | (149,224)-(219,286) lbl (153,293) | (443,286)-(481,364) lbl (406,287) | (97,239)-(113,263) no label |
  | 1 | 0x7ea50 | 12 | (257,179)-(374,248) lbl (250,255) | (491,0)-(639,479) lbl (595,290) | (-1..) disabled | (0,0)-(147,479) lbl (13,290) | (439,259)-(460,276) |
  | 2 | 0x7eb0c | 13 | (190,97)-(450,306) lbl (305,304) "~MISSION COMPUTER" | (0,420)-(639,479) lbl (320,455) | disabled | (23,121)-(130,376) lbl (1,156) | (472,49)-(500,162) |
  (Hall 0: MISSION COMPUTER -> 0x10 "This pilot has already won the game" instead of 5 when the pilot's mission
  counter (+0xc) >= 0x16 or (+0x52 != 0 and >= 0x11): FUN_0001e270; detail for the hall agent.)
- State machine FUN_00039ec0 case 7: context := 2; FUN_0002cc80(db, &scenario) with EBX = the previous screen. Return 1
  (EXIT) -> context 0 and FUN_0003e0b0(0); also: any request for screen 5 while context == 2 becomes 7 (loop head).
- DOSBox (MEASURED): title -> REGISTRATION -> roster row 0, typed DTEST + Enter, ACCEPT (343,462) -> hall 0 (D_hall.png)
  -> hover (462,325) shows "COMBAT SIMULATOR" (D_hall_hover_cs.png) -> click -> D_ia.png / D_ia2.png -> its name box
  (210,418) -> the context-2 Mech Lab (D_ia_lab.png; no turntable: see 2.9).

-------------------------------------------------------------------------------------------------------------------
## 1. Screen 7: COMBAT SIMULATOR (FUN_0002cc80) - completely rewritten vs base FUN_00029680
GBL's "Instant Action" is a one-'Mech screen: pick YOUR chassis (or open the Mech Lab), LAUNCH or EXIT. No enemy side,
no clans, no formations, no scenario choice, no Star Configuration: the scenario is random and both stars come from its
BRF2 SDSC records. Background slot 9 (DATABASE entry 8: the frame with the Ghost Bear emblem, the LAUNCH frame and
"COMBAT SIMULATION PROCESSING" / "RANDOMIZING" baked in; the PCX itself shows "RANDOMIZING" at ~ (170,77), covered by clips).
Music: tables 0x84d68 / 0x84db0 [7] = 0 (keep the current song). DOSBox: D_ia.png, D_ia2.png, D_ia_hover_lab.png.

### 1.1 Entry (TRACED)
1. 'Mech count DAT_00080388 (= FUN_0002cc00): the star-0 slot-0 pilot name (FUN_0003e330(0) + 0x28) strcmp
   "WIDOW" -> 31, "LITTLEGUY" -> 30, else 29 (names XOR-7 decoded at start-up from 0x7e820 / 0x7e824; the eight cheat names
   0x7e80c..: DOOMSDAY LOVECRAFT RVWINDOW IMMORTAL TOLKEIN WIDOW LITTLEGUY THEWATCHER). 29 = table rows 0-28 (all clan +
   the five I.S.), 30 adds Elemental, 31 adds Tarantula. Battle Master IIC (row 31) is never reachable.
2. From the hall (EBX == 1): the campaign star (star record 0, 0x35 dwords = 0xd4 bytes) is saved into the pilot record
   +0x132, FUN_0001e1c0 (writes the registry). On leaving with 1 (EXIT) or -3 it is restored from pilot +0x13a/+0x13e/
   +0x142 (FUN_0003e350(0,-1,..)) and, per slot k < +0x13e, FUN_0003e0b0(k, pilot+0x146+0x40k+4, +0x14) (loadout, pilot name).
3. Remap tables: 0xa9458 identity (0 -> 0xff transparent, 1 -> 1); 0xa9558 hover (1 -> 0x5a, green).
4. Sounds: sound(0x50) kept for LAUNCH (vol 0x32); sound(0x7a) vol 0x46, looped (FUN_0003dd40) and started.
5. Hotspots: FUN_0002bce0(table 0x7f514, count 5) - only 0-4 exist (the table has 25 rows; rows 5-24 and the
   switch cases 5-12 are dead leftovers of the base layout).
6. Scenario (FUN_0002cc80 0x2ce30): *scenario = "pinkscn1" first (dead store), then when EBX == 1 (hall) or 10 (back from
   the simulation): DAT_000adbe4 = round(rand() / 32767 x 89) / 10 (rand 0x4da67, seeded once with 0x4da5c / 0x4da8b;
   constant 0x7762a = 1/32767) -> 0..8; else (back from the Mech Lab) the previous index. Table 0x80360 (9, NULL-ended):
   | i | scenario | BRF2 PDSC planet / text | SDSC player star (?, tons, ?, size) loadouts | SDSC enemy star | SUPS |
   |---|---|---|---|---|---|
   | 0 | jackscn1 | 2 Goat Path, Mesa desert | (3,100,3,3) nva00std smn01std stm00std | (7,100,3,3) smn04std nva01std ktf01std | liagoat |
   | 1 | chedscn1 | 4 Devin, Sparse Urban | (3,100,3,3) tbr00std stm00std hlb00std | (7,100,3,3) drw00std tbr00std jnr00std | liadevi |
   | 2 | edamscn1 | 8 Mannedorf, Canyon | (1,100,3,3) mrd00std grg01std tbr00std | (5,100,3,3) mdg00std nva00std mrd00std | liamann |
   | 3 | provscn1 | 9 Garstedt, Ice Plains | (3,100,3,3) smn04std nva00std jnr00std | (7,100,3,3) mdg00std whm00std stm00std | liadawn |
   | 4 | goudscn1 | 3 Bone-Norman, Rolling Hills | (4,80,3,3) mdg00std whm00std stm00std | (7,100,3,3) drw00std mrd01std hlp01std | liabone |
   | 5 | colbscn1 | 5 Dawn, Ice Desert | (1,100,3,3) mdg00std nva00std smn00std | (5,100,3,3) stm00std rfl00std whm00std | liadawn |
   | 6 | goatscn1 | 12 Kanowit, Cratered Vacuum | (1,100,3,3) smn04std stm00std hlb00std | (5,100,3,3) mdg00std nva00std stm00std | liakano |
   | 7 | whizscn1 | 1 Graus, Rocky Highland | (1,100,3,3) drw00std tbr00std grg00std | (5,100,3,3) whk00std whm00std mrd00std | liagrau |
   | 8 | swisscn1 | 10 Port Arthur, Enclosed Trial Area | (3,100,3,3) drw00std nva00std mdg00std | (7,100,3,3) grg01std whk01std whk02std | liaport |
   (base had 10, ricoscn1 included; RICOSCN1 is still in the GBL PRJ but not listed. "hlp01std" in GOUD: no such chassis
   code - the slot fails to match the mech table: FUN_0003e0b0 returns 0/1, ASSUMED left empty.)
7. FUN_0002c810(scenario, fill = (EBX == 1), planet = 1): record <first 4 letters>"brf2" (BWD, FUN_0001ca60 type 0xe);
   chunk 0x47 (SUPS) string -> 0x84dfc (the -b= camo argument); if fill: chunk 0x46 (SDSC) #1 -> star 0 = FUN_0003e350(0,
   0, size(+0xc), ?(+8), tons(+4)) and FUN_0003e0b0(k, loadout k) for k < size (done twice, as written); SDSC #2 -> star 1
   the same, and DAT_0007b728 = its first int; then FUN_0003e350(0,-1,-1,-1,-1); FUN_0003e0b0(0,0,0) (select slot 0);
   chunk 0x45 (PDSC): planet = +0 - 1, clamped 0..11 (else 0) -> clip(0, aplanNN, 156, 52, 0x42) (table 0x80300 pairs
   aplanNN / aplanNNc, NN 01-12). The PDSC text lines are NOT drawn in GBL (base drew them at (239,69)).
   So the IA star is the scenario's star (size 3: the player + 2 AI wingmen), refilled only when coming from the hall;
   after a mission (EBX 10) a new random scenario is chosen but the star is kept.
8. The 'Mech label: index = FUN_0003e2e0(0) (star 0 slot 0 chassis) when from the hall, else pilot record +0x72 (the
   pilot's own star copy, slot 0 chassis - INFERRED from screen 5's use of +0x5e..+0x76); text(font 0x22, 172, 414,
   name or " MECH LAB      " when < 0, 0xa9458). Mech names are the table's padded strings (e.g. "Stormcrow       ").

### 1.2 Clips (TRACED; positions MEASURED consistent in D_ia.png)
| slot | name | x, y | flags | fps | role |
|---|---|---|---|---|---|
| 0 | aplanNN then aplanNNc | 156, 52 | 0x42 then 0x4a | 10 | planet (the c clip restarted whenever slot 0 is idle) |
| 0x19 | agbcs<pic> (sprintf 0x775b6 "agbcs%s", picture code of the chosen chassis) | 103, 238 | 0x48 | 20 | green wire-frame showcase (D_ia.png "Summoner") |
| 0x11 | wgbcs001 | 344, 129 | 0x261 | 10 | grid, plays with 0x0f |
| 0x0f | wgbcs010 (SMK on the CD only, not in the rip's SMK\) | 370, 284 | 0x261 | 10 | "COMBAT PARAMETERS" |
| 0x12 | wgbcs002 | 337, 127 | 0x261 | 10 | "RANDOMIZING CONDITIONS" grid |
| 0x14 | wgbcs005 | 59, 237 | 0x48 | 10 | left gauge |
| 0x15 | wgbcs006 | 540, 133 | 0x48 | 10 | right gauge |
| 0x16 | wgbcs007 | 139, 36 | 0x48 | 10 | planet reticle |
| 0x17 | wgbcs008 | 325, 35 | 0x48 | 10 | red text block |
| 0x10 | wgbcs004 | 473, 9 | 0x41 | 10 | LAUNCH button, frame 0 (FUN_0003d7c0(0x10,0)), frame 1 when pressed |
| 0x18 | wgbcs009 | 408, 402 | 0x41 | 10 | EXIT button, frame 1 while hovered |
| 0x13 | wgbcs003 | 154, 406 | 0x44 | 10 | the name box with its arrows |
Cycle (loop head): when 0x11 and 0x0f have both finished -> stop/hide them (FUN_0003cdc0(s,1,1), (s,0x20)), start 0x12;
when 0x12 finishes -> hide it, restart 0x11 and 0x0f; forever (D_ia.png vs D_ia2.png show both phases).

### 1.3 Hotspots 0x7f514 (count 5) and actions (TRACED)
| # | rect | label pos | hover | click |
|---|---|---|---|---|
| 0 | (473,9)-(608,80) | - | - | frame 1 of 0x10, show, flip; sound 0x50; FUN_0001d430(2,1) (launch prep, ~ base 0x1d6c0) -> 10 |
| 1 | (408,402)-(496,426) | - | frame 1 of 0x18 (EXIT lit) | -> 1 (hall; context 0; campaign star restored) |
| 2 | (178,406)-(242,430) | - | label becomes " MECH LAB      " in green (0xa9558) | FUN_0003e350(0,-1,-1,-1,-1) -> 9 (Mech Lab, ctx 2) (D_ia_hover_lab.png) |
| 3 | (242,406)-(263,430) | (172,414) | label in green | NEXT chassis: index+1; >= count -> -1 (loadout ""); repeated while FUN_0003e0b0(0, code) refuses (chassis tons > star limit +0x10); new label; clip 0x19 reloaded as agbcs<pic> (103,238,0x48,20) |
| 4 | (154,406)-(178,430) | (172,414) | label in green | PREV chassis: index-1; -1 -> ""; -2 -> count-1; same refusal loop |
Leaving the hotspots: EXIT back to frame 0, label back to 0xa9458. The global Esc menu (FUN_00026f10) works here.
Dead code: cases 5/7 (slots 2/4), 8/9, 0xc (re-randomise + formation labels "Echelon Left".. via 0x7e8a8) can't occur.

### 1.4 Differences from base screen 7 (the base shell "Instant Action")
Base: 25 hotspots (scenario, 3 + 3 mechs, formations, clans, Mech Lab / Star Config per side), clan emblems WIA*, planet
APLANnnC at (414,10), PDSC text, 15-18 mechs by "Calvin"/"Hobbes". GBL: 5 hotspots, one chassis, random scenario, no
text, wgbcs* clips, 29-31 mechs by WIDOW / LITTLEGUY, reached from the hall's COMBAT SIMULATOR, EXIT returns to the hall.

-------------------------------------------------------------------------------------------------------------------
## 2. Screen 9: the Mech Lab (FUN_000374e0, helpers FUN_00036720 / FUN_00036c20 / FUN_000365d0)
DOSBox (MEASURED; turntable SHPs added to SMK\ for D2_*: see 2.9): D_ia_lab.png (rip as is), D2_lab_main.png,
D2_c01..D2_c30.png (NEXT CHASSIS x 30 = the whole list), D2_var1.png (NEXT VARIANT), D2_cust*.png (CUSTOMIZE pages),
D2_hover_exit.png, D2_after_abort.png, D2_camp_lab.png (campaign, from screen 5's MECH PLANT box).
The panel / page internals (FUN_00034b30 = base FUN_00030db0, widget tables, callbacks 0x2fa50.. 0x31510) are the base
code moved by ~0x3d80 bytes; layouts, masses and texts match the port's existing pixel-matched implementation except
as listed in 2.8. All text: grey (170,170,170) in both contexts (MEASURED, all three captures; remap 0xacbc8 1 -> 6);
selectable CUSTOMIZE rows white as in base.

### 2.1 Entry (TRACED FUN_000374e0 0x374e0..0x37b71)
- local_54 = pilot record +0x5a (the hall number). Sounds: sound(0x52) -> DAT_000acfd0 vol 0x28 (played by a
  WEAPONS TABLE row click, 0x33862); sound(0x4d) vol 0x11 looped (FUN_0003dd40) and started after FUN_00036720 =
  lab ambience. Remaps 0xacbc8 (1 -> 6), 0xaccc8 (1 -> 1), 0xacdc8 (1 -> 0x34, then 0xca).
- Background: 0x802c8 + ctx x 16: slot 16 (ctx 0/1; DATABASE entry 15) / 10 (ctx 2; entry 9) - both entries are the
  same all-black 640x480 PCX (md5 ee7be74d): all art comes from clips. (Base: slot 15 / 22 / 10 with real pictures.)
- Hotspots 0x802c0 + ctx x 16 -> 0x7f354 for every context, count 10. 7 (SAVE) and 8 (ABORT) disabled at start.
- Music: tables [9] = 0 (keep).
- Chassis count DAT_00084d38 = ctx 2: FUN_0002cc00() (29 / 30 LITTLEGUY / 31 WIDOW), else 0x18 = 24 (the clan 'Mechs
  Firemoth .. Stone Rhino; no I.S. 'Mech in the campaign lab).
- Chassis DAT_00084d60 = FUN_0003e2e0(-1) (current star slot); < 0 -> 0; >= count -> 0. Variant slot DAT_000ada04 =
  the slot loadout's digits [3][4] (+100 when its [5..7] != "std"), e.g. "smn04std" -> 4, "tbr00usr" -> 100.
- ctx 0 and local_54 != 0 -> NEXT / PREV CHASSIS disabled (variants only) (base: trials lock; the GBL key is +0x5a).
- Header: sprintf(0xabbd8, "~CALLSIGN: %s (%d.00 T MAX)", slot pilot name, star tonnage limit +0x10) at (320,4);
  second line '~' + chassis name at (320,28) (widget table 0x84130 rows 0/1, centred, callback 0x2fec0). The name is
  the table string cut at the first double space; rows 24-28 use sprintf("I.S. - %s") (0x77c25 / 0x77c2f / 0x77c39 in
  FUN_00036720 / FUN_00036c20 / variant-prev). Captures show it upper case ("I.S. - RAVEN", "DIRE WOLF", "WARHAMMER"):
  the header font is capitals-only (INFERRED from the capture; same font as base's header).
- Widget table 0x84130 (51 rows, base layout): "Variant:" (20,64) + name (65,64); COMPONENT / MASS / NOTES (20/98/147,
  64+22 = 86); Engine .. Equipment rows every 11 from 108; Used Mass / Max Mass (229 / 240); "WEAPONS AND AMMO" (444,64),
  14 lines from 86 every 11 (callback 0x30110, 130 wide). y encoding 0x8000hhll: previous y + hh? + ll as (hi nibble x
  font height 10 + lo nibble): 0x22 -> +22, 0x11 -> +11 (MEASURED: rows at 86, 97, 108 ..).

### 2.2 Clips (TRACED 0x37580..0x37760; MEASURED panel rectangles in D2_lab_main / D2_cust_*)
| slot | name | x, y | flags | fps | role |
|---|---|---|---|---|---|
| 1 | wgbmp1 | 13, 55 | 0x46 | 10 | left panel (192 x 242 measured: x 13-204) |
| 10 | wgbmp4ar | 216, 55 | 0x64 | 10 | tall middle (armour allocation / criticals), hidden until used |
| 11 | wgbmp7cr | 436, 55 | 0x64 | 10 | tall right (WEAPONS TABLE / UNASSIGNED CRITICALS), y 55-478 |
| 12 | wgbmp4mp | 75, 171 | 0x21 | 10 | the clickable armour diagram (SHP) |
| 13 | wgbmp5wa | 216, 55 | 0x64 | 10 | middle (WEAPONS AND AMMO editor) |
| 14 | wgbmp1es | 436, 55 | 0x46 | 10 | short right (weapons list / engine .. pages) |
| 4 | wgbcsl | 171, 425 | 1 | 10 | CHASSIS box with arrows; frame 1 while an arrow is hovered (FUN_0003d7c0(4,1)) |
| 0 | wgbvrl | 333, 425 | 1 | 10 | VARIANT box; frame 1 while hovered |
| 0x11 / 0x14 | wgbacl / wgbacd | 24, 368 | 0x44 / 0x64 | 10 | ACCEPT normal / hovered |
| 0x12 / 0x15 | wgbcul / wgbcud | 24, 401 | 0x44 / 0x64 | 10 | CUSTOMIZE |
| 0x13 / 0x16 | wgbexl / wgbexd | 24, 434 | 0x44 / 0x64 | 10 | EXIT LAB |
| 0x17 / 0x1a | wgbdll / wgbdld | 493, 434 | 0x64 | 10 | DELETE (only for user designs, slot >= 100) |
| 0x18 / 0x1b | wgbabl / wgbabd | 24, 385 | 0x64 | 10 | ABORT (customise mode) |
| 0x19 / 0x1c | wgbsvl / wgbsvd | 24, 418 | 0x64 | 10 | SAVE (customise mode) |
| 0x10 | ashmp<pic> (sprintf "ashmp%s", 0x84d44 -> 0x84d3c, picture code) | 310, 383 | 0x88 | 20 | turntable SHP (FUN_00036720) |
"l" = cyan normal, "d" = dark hovered (MEASURED D2_hover_exit.png, D2_cust_hover_save.png). Buttons are art, not font
text (base: font-27 labels). Hover logic per frame: case k shows its "d" clip and hides the others' "d"; default shows
all "l" (and DELETE when a user design) and FUN_0003d7c0(0,0), (4,0). Every frame also FUN_0003cdc0(5, ctx 2 ? 0x20 : 1)
and (6..9, 0x20): slots not loaded by the lab - leftovers, no visible effect (INFERRED).
Turntable placement: the capture's platform+mech lies x ~ 310-440, y ~ 250-390 (D2_lab_main.png): slot origin (310,383)
plus the SHP frame offsets (base used (175,127)+offsets for AWOMP; ASSUMED the same SHP-offset rule).

### 2.3 Hotspots 0x7f354 (10; same for all contexts) (TRACED; MEASURED clicks in the run)
| # | rect | action |
|---|---|---|
| 0 | (24,434)-(112,458) EXIT LAB | -> 7 in ctx 2, else 5 (the mission screen) |
| 1 | (287,425)-(307,457) NEXT CHASSIS | index+1 (wrap at count), variant 0, FUN_00036720 (list + turntable + voice) |
| 2 | (171,425)-(189,457) PREV CHASSIS | index-1 (wrap) |
| 3 | (449,425)-(469,457) NEXT VARIANT | sound(0x76) vol 0x32 (global DAT_00098838); FUN_00036c20: next non-empty slot of 200, wrap to 0 |
| 4 | (333,425)-(351,457) PREV VARIANT | sound 0x76; previous non-empty slot (0 -> 199 ..) |
| 5 | (24,401)-(112,425) CUSTOMIZE | first free user slot s (100..199): s >= 200 -> "Error: Too many mechs|of this variant to save.#Ok"; else name "User Variant #<s-99>", pages on (tables 0x83aa8 left, 0x8108c ..), hotspots 0-6 and 9 off, 7 / 8 on |
| 6 | (24,368)-(112,392) ACCEPT MECH | FUN_0003e0b0(-1, variant name): refused -> "'Mech exceeds|Keshik Defined Maximum Tonnage (KDMT)|for mission.#Ok"; else return EBX (the calling screen: 5 or 7) |
| 7 | (24,418)-(112,442) SAVE | see 2.5 |
| 8 | (24,385)-(112,409) ABORT | back to the main page (variant list rebuilt, the edit dropped) |
| 9 | (493,434)-(581,458) DELETE | user designs only: "Delete this 'Mech?|Are you sure?#Yes|No" (default 1); yes -> remove "mek\%s.mek", slot cleared, next variant |
Base 0x7f26c had 11 (STAR CONFIG at (404,414)-(474,474); EXIT LAB (50,445)..): GBL has no STAR CONFIG button and every
rectangle moved. ACCEPT returning to the caller is as base (case 7: EBX).
Chassis voice: sound(table +0x14, vol 0x28) on entry and on each chassis change (FUN_00036720 tail, 0x36a06..0x36a84);
-1 = none (Tarantula).

### 2.4 Variant list (FUN_00036720 / FUN_000365d0, TRACED) - as base
200 slots x 13 bytes at 0xacfdc: slot 0 = sprintf("%s%02dstd", archive code, 0) unconditionally; slots 1-99 =
"%s%02dstd" when the PRJ has it (FUN_0001ca20); slots 100-199 = files mek\<code>??usr.mek on disk (findfirst attr 6,
slot = 100 + the two digits, name cut to 8). PRJ records named *USR / *HOF / *WTR (DRW00USR, NGA00USR, TBR00USR,
HLB00HOF, LBK01WTR, EXE00WTR) are therefore never listed - only missions can use them. Display: FUN_00034b30(slot name)
loads the MEK and fills the panels ("Variant: Primary Config", "Alt. Config A".. = the MEK's 50-byte name).

### 2.5 SAVE and validation (TRACED 0x387xx..0x38d40) - as base
- DAT_000ac108 != -1 -> "Invalid 'Mech specification:|Unassigned criticals detected.#Ok".
- max mass (0xabe58) < used (0xabe5c) -> "Invalid 'Mech specification:|Chassis can not support|current mass.#Ok".
- First free slot s in 100..199, else "Error: Too many mechs|of this variant to save.#Ok". File name
  sprintf("%s%02dusr.mek", archive code, s - 100) - NN starts at 00 (consistent with the PRJ's DRW00USR / NGA00USR;
  note the base shell / the port say 01-99: the original's first design is 00 and is called "User Variant #1").
- fopen("mek\<name>", "wb"); failure -> mkdir("mek") and retry; writes header 0x18, 8 x 0x28 locations, items x 8,
  links x 8, name 0x32 (the base MEK layout); failure -> "Error saving 'Mech.#Ok"; success -> the new slot is current.
- Other strings of the page code (unchanged): "...ed critical|can not be removed.#Ok" (0x77b80 tail), 'MASC', 'CASE'.

### 2.6 The GBL mech table 0x84a20 (32 x 0x18, TRACED) - C table
{picture code (SMK\ashmp<pic>.shp, agbcs<pic>, wgbmc<pic>), archive code (MEK <code>NNstd), file stem, name (padded as
stored), tons, voice arg (FUN_00022920; -1 none)}. Rows 24-28 are the Inner Sphere 'Mechs ("I.S. - %s").
```c
static const struct { const char *pic, *code, *file, *name; int tons, voice; } GBL_MECHS[32] = {
 {"fm","frm","firemoth","Firemoth           ",20, 87}, {"vx","vxn","vixen","Incubus             ",30,102},
 {"kf","ktf","kitfox","Kit Fox              ",30, 91}, {"pg","prg","peregrin","Horned Owl     ",35,103},
 {"jn","jnr","jenner","Jenner IIC          ",35, 90},  {"pt","ptm","phantom","Phantom           ",40,104},
 {"nv","nva","nova","Nova                 ",50, 94},   {"sc","stm","strmcrow","Stormcrow       ",55, 96},
 {"md","mdg","maddog","Mad Dog          ",60, 92},     {"hb","hlb","hellbrgr","Hellbringer        ",65, 89},
 {"lb","lbk","lineback","LineBacker        ",65,105},  {"rf","rfl","rifleman","Rifleman IIC      ",65, 95},
 {"gr","grz","grizzly","Grizzly              ",70,106},{"su","smn","summoner","Summoner       ",70, 97},
 {"tw","tbr","timbrwlf","Timber Wolf    ",75, 98},     {"gg","grg","gargoyle","Gargoyle          ",80, 88},
 {"ng","nga","naga","Naga                 ",80,107},   {"wh","whm","warhammr","Warhammer    ",80, 99},
 {"mr","mrd","marauder","Marauder IIC    ",85, 93},    {"wk","whk","warhawk","Warhawk         ",85,100},
 {"ex","exe","executnr","Executioner       ",95,108},  {"dw","drw","direwolf","Dire Wolf         ",100,85},
 {"kd","kdk","kodiak","Kodiak               ",100,109},{"bh","bhm","behemoth","Stone Rhino      ",100,110},
 {"rv","rvn","raven","Raven                ",35,111},  /* 24 I.S. */
 {"hc","hat","hatamoto","Hatamoto-Chi      ",80,112},  /* 25 I.S. */
 {"vt","vct","victor","Victor               ",80,113}, /* 26 I.S. */
 {"an","ani","anihilat","Anihilator           ",100,114}, /* 27 I.S. (sic) */
 {"at","atl","atlas","Atlas                   ",100,115},  /* 28 I.S. */
 {"el","ele","elementl","Elemental",100,86},           /* 29: LITTLEGUY / WIDOW, IA only */
 {"tr","tar","tarantul","Tarantula",100,-1},           /* 30: WIDOW, IA only */
 {"bm","btm","bttlmstr","Battle Master IIC",100,101},  /* 31: unreachable */
};
```
MEASURED (D2_c01..c30, ctx 2, start Dire Wolf): Kodiak, Stone Rhino, I.S. - Raven, I.S. - Hatamoto-Chi, I.S. - Victor,
I.S. - Anihilator, I.S. - Atlas, Firemoth, Incubus, Kit Fox, Horned Owl, Jenner IIC, Phantom, Nova, Stormcrow, Mad Dog,
Hellbringer, Linebacker, Rifleman IIC, Grizzly, Summoner, Timber Wolf, Gargoyle, Naga, Warhammer, Marauder IIC,
Warhawk, Executioner, Dire Wolf (wrap after 29 = count without cheats), Kodiak. Stock "00std" of the I.S. 'Mechs
(main page): Raven 35 t 210XL, SRM 6, ER ML x2; Hatamoto-Chi 80 t 320XL, ER PPC x2, SRM 6 x2; Victor 80 t 320,
Gauss, SRM 4, Pulse ML x4; Anihilator 100 t 200, Gauss, Pulse ML x4, LB 10-X x4; Atlas 100 t 300XL, Pulse ML x2,
ER LL x2, Gauss, LRM 20. Base (0x83684, 18): Firemoth .. Dire Wolf (15) + 3 egg chassis, other picture codes (ds kf jn
bh sc md lo rf su mc mw wh mr ms da) and voice args 0x55..; GBL renames "Warhammer IIC" -> "Warhammer".

### 2.7 New weapons 31-34 (= MEK ids 3101.. 3402, i.e. weapon index x 100 + instance)
Shell weapon table 0x80ad4 (35 x 40 bytes {heat, damage, ?, short, medium, range m, mass x 100, crits, ammo / ton,
name*}; the name pointer at +0x24 = 0x80af8 + 40 i is what the code indexes), base had 31 (to Nuke). GBL renames 28
"Narc Missile Beacon" -> "NARC", 29 "Anti-Missile System" -> "AMS". New rows (TRACED):
| idx | name | heat | dmg | ? | short | med | range m | tons | crits | ammo/t |
|---|---|---|---|---|---|---|---|---|---|---|
| 31 | Inferno SRM-2 | 2 | -2 | -1 | 3 | 6 | 497 | 0.5 | 1 | 50 |
| 32 | Torpedo | 4 | -2 | -1 | 4 | 8 | 497 | 3 | 2 | 15 |
| 33 | Inferno SSRM-2 | 2 | -2 | -1 | 4 | 8 | 497 | 1 | 1 | 50 |
| 34 | ARROW IV | 10 | -20 | -1 | -1 | -1 | 1000 | 12 | 12 | 5 |
(31 = SRM 2's row, 32 = Streak SRM-6's, 33 = Streak SRM-2's numbers; 34 = Clan Arrow IV 12 t / 12 slots.)
Ammo ids 10000 + weapon id as base (13101 .. 13402). Equipment table 0x8050c (5000 MASC .. 9000 Ferro-Fibrous): unchanged.
Users: NGA01STD-NGA04STD (Naga Alt. A-D: two ARROW IV, 12 slots each arm) - stock variants, so the Mech Lab and the sim
see them; DRW00USR (3101, 3201), LBK01WTR / EXE00WTR (3201/3202), HLB00HOF "Candler" (3301/3302), TBR00USR / NGA00USR
(3401/3402). (NOTES.md listed only the USR/WTR/HOF files; the NGA0xSTD records also carry 3401/3402.)
Sim weapon table (GBL MW2.EXE, VA 0xafedc, 35 x 0x68 bytes; ints, heat / target heat 16.16 fixed; short name at +0x50):
| idx | name | +0 type | +4 vis | +8/+c | +1c per volley | +20 ammo/t | +24 sound | +28 drop | +2c speed | +30 dmg | +34 heat | +38 tgt heat | +40 range cm | +44 refire | +48 interval | +4c life | +60 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 31 | "Inf SRM" | 3 | 10 | 26/27 | 2 | 50 | 258 | 0 | 70 | 0 | 1.34 | 0 | 49686 | 910 | 60 | 709 | 21 |
| 32 | "Torpedo" | 9 (new) | 34 | 32/33 | 2 | 50 | 466 | 0 | 10 | 50 | 1.34 | 0 | 49686 | 910 | 60 | 2730 | 5 |
| 33 | "InfSSRM" | 3 | 10 | 26/27 | 2 | 50 | 258 | 0 | 70 | 0 | 1.34 | 0 | 49686 | 910 | 60 | 709 | 5 |
| 34 | "ArrowIV" | 10 (new) | 35 | -1/-1 | 1 | 5 | 464 | 0 | 100 | 7 | 10.0 | 0 | 200200 | 3640 | 60 | 2002 | 251? |
(+0x18 = 1 on 32-34 as on LRM / Streak rows; Inferno has damage 0 and +0x60 = 21 where others have 5 - the heat effect is
NOT traced. 34's tail overlaps the next data. Type 9 / 10 handling in the sim NOT traced. For reference the DOS rows 0-30
use the same layout; e.g. 0 LRM20 type 4 vis 3 pv 20 apt 6 snd 189 spd 100 dmg 2 heat 0.6 f3c 7500 range 100100.)
Shell vs sim disagree on Torpedo ammo (shell 15 / t, sim 50 / t): both as stored.

### 2.8 Differences from the base Mech Lab (summary)
1. Art: wgbmp* panels at y 55 (base wwomp*/wjfmp*/wiamp* at y 52, left x 12 -> 13), black background, art buttons
   wgb{ac,cu,ex,dl,ab,sv}{l,d} and the wgbcsl / wgbvrl selector boxes (base: font labels + wwocn/cp/vn/vp clips).
2. Turntable: ashmp<pic>.shp at (310,383), 20 fps, one set for every context (base AWOMP / AJFMP / AIAMP at (175,127)).
3. Hotspots: 10 at new places (table above); no STAR CONFIG; EXIT LAB -> 5 (campaign) / 7 (IA) (base -> 11 / 7).
4. Mech list: 32-row table, 24 clan 'Mechs in the campaign, 29 / 30 / 31 in IA (base 15, 15-18). "I.S. - " prefix for
   rows 24-28. Voice args 85-115.
5. Chassis lock key: pilot +0x5a (hall number) instead of the trial flag (ctx 0 only).
6. WEAPONS TABLE 0x82a28 (CUSTOMIZE, right panel): 35 rows, 11 px apart from y 86 with NO group gaps (base 0x81890: 27
   rows with 22-px gaps before ballistic and missile groups), MEASURED D2_cust_weapons.png rows y 86..438 (33 shown):
   order 22 ER LL, 23 ER ML, 24 ER SL, 21 ER PPC, 20 Flamer, 25 LPL, 26 MPL, 27 SPL, 29 AMS, 11 Gauss, 12 LB2, 13 LB5,
   14 LB10, 15 LB20, 10 MG, 16 UAC2, 17 UAC5, 18 UAC10, 19 UAC20, 6 SRM2, 5 SRM4, 4 SRM6, 9 SSRM2, 8 SSRM4, 7 SSRM6,
   3 LRM5, 2 LRM10, 1 LRM15, 0 LRM20, 28 NARC, 31 Inferno SRM-2, 33 Inferno SSRM-2, 34 ARROW IV, 30 Nuke, 32 Torpedo.
   Row make-callback 0x302a0 (click 0x33810): row 30 (Nuke) exists only when DAT_000a9bd0 != 0, row 32 (Torpedo) only
   when DAT_000a9bd4 != 0; set by FUN_0002e7c0(0x80 / 0x81, v) from the pilot name (record +0x3d) "DOOMSDAY" (Nuke) /
   "LOVECRAFT" (Torpedo) at registry load (0x1e11f), pilot creation (0x3c0a7) and cleared on delete (0x3bcae). The
   selected row (id x 100 == 0xabf78) uses remap 0xaccc8. Flamer, AMS, NARC are buyable in GBL (base: not in the table).
```c
static const int GBL_WEAPONS_TABLE[35] = {22,23,24,21,20,25,26,27,29, 11,12,13,14,15,10,16,17,18,19,
    6,5,4,9,8,7,3,2,1,0, 28,31,33,34, 30 /* DOOMSDAY */, 32 /* LOVECRAFT */};   /* y = 86 + 11 * visible row */
```
7. Variant / save / delete / validation: unchanged (2.4, 2.5).

### 2.9 Data missing from the DOS rip (MEASURED)
The rip's SMK\ lacks ASHMP*.SHP (32 turntables), DBSOUND.MW2, WIACN.SHP, WIACP.SHP and the wgbcs010 clip; the
Titanium disc has the SHPs in INSTALL/DATA/SMK. As shipped the lab shows no turntable (D_ia_lab.png: empty middle; the
clip loader just leaves slot 0x10 empty). The D2_* run copied ASHMP*.SHP into SMK\ (PRE) - the port should ship / read
them from the disc tree. ASHMP files exist for all 32 picture codes (an at bh bm dw el ex fm gg gr hb hc jn kd kf lb md
mr ng nv pg pt rf rv sc su tr tw vt vx wh wk); AGBCS<pic>.SHP showcases likewise.

### 2.10 Campaign route (MEASURED D2_mission.png, D2_camp_lab.png)
Hall 0 MISSION COMPUTER (311,310) -> screen 5 (BRIEFING / INTELLIGENCE / MECH PLANT); the MECH PLANT slot boxes =
screen-5 hotspots 2-4 (0x801c0 -> 0x7ec00: (335,306)-(422,323), (335,345)-(422,362), (335,382)-(422,399); label x 345)
-> FUN_0003e0b0(slot) and 9 when that slot holds a 'Mech and is inside the star size. Lab: "CALLSIGN: DTEST
(90.00 T MAX)" / GRIZZLY (mission 1's 'Mech); EXIT LAB -> 5.

-------------------------------------------------------------------------------------------------------------------
## 3. Captures index (/home/claude/gbl/ref)
D_title, D_roster, D_roster_named, D_hall, D_hall_hover_cs, D_ia, D_ia2, D_ia_hover_lab, D_ia_lab (rip as is);
D2_lab_main, D2_c01..D2_c30, D2_var1, D2_cust (CUSTOMIZE entry = ENGINE page), D2_cust_engine, D2_cust_hs, D2_cust_jj,
D2_cust_int, D2_cust_armor, D2_cust_weapons, D2_cust_ammo, D2_cust_equip, D2_cust_crit, D2_cust_hover_save,
D2_after_abort, D2_hover_exit, D2_ia_back, D2_hall_back, D2_mission, D2_camp_lab (ASHMP*.SHP added).
Scripts: run 1 = title (320,250) -> roster (150,100) "DTEST" Enter -> ACCEPT (343,462) -> hall (462,325) -> IA (210,418);
run 2 adds NEXT CHASSIS (297,441) x 30, NEXT VARIANT (459,441), CUSTOMIZE (68,413), left rows x 40 at y 112 / 145 / 156
/ 167 / 178 / 189 / 200 / 211 / 266, ABORT (68,397), EXIT LAB (68,446), IA EXIT (452,414), MISSION COMPUTER (311,310),
MECH PLANT slot 0 (378,314).

## 4. Unresolved
- Clip flag bits (0x41 0x42 0x44 0x46 0x48 0x4a 0x64 0x88 0x21 0x261) quoted raw; meanings only as far as base notes go.
- The turntable's exact pixel placement rule (origin (310,383) + SHP offsets ASSUMED as base); not pixel-compared.
- Sim behaviour of weapon types 9 (Torpedo) / 10 (Arrow IV) and the Inferno heat effect (+0x60 = 21, damage 0): NOT traced.
- Pilot record +0x5a (hall number) and +0x72 semantics are from their uses here only (pilot-record agent to confirm).
- SDSC fields +8 and the first int (3/7/1/5/4) passed to FUN_0003e350: meaning not traced (base: "?").
- goudscn1's enemy "hlp01std" matches no chassis code: effect (empty slot?) not checked in DOSBox.
- Voice arg -> DATABASE entry (the arg - 1 rule of base) not re-verified for the 14 new voices.
- WIDOW / LITTLEGUY lists (Elemental, Tarantula) not captured in DOSBox.
