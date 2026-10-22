# C - The GBL campaign: missions, Mission Computer (briefing), launch handoff, debriefing

Agent C. Sources: GBL shell decompile `re/shellgbl_decomp.c` / `re/shellgbl.asm` (addresses are GBL MW2SHELL VAs),
GBL sim `re/mw2gbl_decomp.c`, MW2.PRJ records (`tools/prj.py`), DOSBox captures `ref/C_*.png`, files the DOS shell wrote
`ref/C_*.CFG / *.BWD`. "TRACED" = read in the code/data; "MEASURED" = seen in DOSBox; "INFERRED/ASSUMED" = not pinned.

GBL has ONE campaign (Clan Ghost Bear, clan context 0). There is no clan choice, no Ready Room, no Star Configuration
screen and no separate briefing screen: the base screens 0 (briefing), 11 (Ready Room) and 13 (Star Configuration) are
replaced by ONE screen 5 "Mission Computer" (FUN_00028410) that holds the briefing text, the briefing video, the star
(lance) selection and the Mech Lab entry. Screen 5 is the base Archive screen number; the Archive viewer itself survives
only as the full-screen text pager FUN_0002a590 ("DETAILS" / debriefing "AFTERMATH").

## 1. The pilot record (MW2REG.CFG, 20 x 0x20e = 10520 bytes; DAT_00095ed0 + i*0x20e, current = DAT_000987e8)

Only the campaign fields (roster / registration belongs to the roster spec). TRACED + MEASURED (`ref/C_MW2REG_testc.CFG`,
a new pilot "TESTC"):

| off | type | meaning |
|---|---|---|
| +0x00 | int | in use |
| +0x04 | int | selected |
| +0x08 | int | clan context (0) - indexes PTR_PTR_s_bassSCN1_0008017c (only [0] exists) |
| +0x0c | int | **campaign position** = mission index 0..21, 22 = finished ("Retired" title) |
| +0x10 | int | rank index (0x7e790: Cadet, Mechwarrior, Star Commander, Star Captain, Star Colonel, Nova Commander, Galaxy Commander); new pilot 0 |
| +0x14 | int | career honor (new pilot: 1857 = 0x741, MEASURED) |
| +0x18 / +0x1c / +0x20 | int | kills / hits / shots (debriefing adds MW2CAR counters) |
| +0x28 | char[21] | displayed name = slot-0 pilot name; after each failure rewritten `sprintf("%s %02d", +0x3d, +0x52)` (0x25a5f) |
| +0x3d | char[21] | base name (as typed; cheat names compared here) |
| +0x52 | int | **failure count** (debriefing, status 3) - a failed pilot never gets the Bloodright trials |
| +0x56 | int | retries of the current mission (failures since the last success; reset on success) |
| +0x5a | int | **location** (hub variant 0/1/2) = mission table +0x1d of the last mission won; 0 = new pilot |
| +0x5e..+0x131 | 0xd4 | copy of the friendly star record 0x856b0 (FUN_0001e1c0 copies it on every save) |
| +0x206 | int | highest starmate slot in use (screen 5 starmate arrows) |

## 2. Campaign tables (TRACED)

### 2.1 Mission list PTR_PTR_s_bassSCN1_0008017c[ctx] -> 0x800ac, 9-byte records {char *scenario; u8 trial; char *title}

Only context 0 exists (0x8017c holds 0x800ac; 0x80180 is already the hub table). Index = pilot +0x0c.

### 2.2 Per-mission record 0x7fb8c + i*0x39 (23 records, the last is the terminator)

| off | VA (i=0) | type | meaning | used by |
|---|---|---|---|---|
| +0x00 | 0x7fb8c | char* | briefing video `<xxxx>brf1` (SMK) | screen 5 slot 12 (40,267) |
| +0x04 | 0x7fb90 | char* | "" (unused) | - |
| +0x08 | 0x7fb94 | char* | debriefing success video `<xxxx>dbfs` (SMK) | screen 3 slot 4 (41,28) |
| +0x0c | 0x7fb98 | int | DBSOUND.MW2 number of the briefing voice-over (1-based; = i+1) | screen 5 |
| +0x10 | 0x7fb9c | int | DBSOUND number of the debriefing (success) voice-over, -1 none | screen 3 |
| +0x14 | 0x7fba0 | int | trial "always passes" flag (asah = 1) | screen 3 |
| +0x18 | 0x7fba4 | u8 | flag tested on debriefing EXIT after a win - both branches return 1 (no effect, see 5.6) | screen 3 |
| +0x19 | 0x7fba5 | u32 | max index into the retry-alternative table a93b8 (all 0) | 3, 5 |
| +0x1d | 0x7fba9 | int | location after winning this mission -> pilot +0x5a | screen 3 |
| +0x21/+0x25 | 0x7fbad/b1 | int, ptr | clips before the simulation (state 10 coming from screen 5) | state machine |
| +0x29/+0x2d | 0x7fbb5/b9 | int, ptr | clips on a won debriefing (before the screen) | screen 3 |
| +0x31/+0x35 | 0x7fbbd/c1 | int, ptr | clips on leaving a won debriefing (EXIT) | screen 3 |

Clip lists are 16-byte records {char *name, 0, 0, 0}; names are full-screen SMKs played by FUN_0003c430
(`%ssmk\%s.smk` on the CD drive, else `smk\%s.smk`). A list stops early when a clip is aborted with the mouse button
(0xadee0). DAT_0007e800 = 1 (static, never written) - with it 0 only "mraid" / "mend" would play.

The table at 0xa93b8 (23 x char**, "alternative scenario after N failures") is BSS and never written: always NULL, so a
failed mission is always retried as the same scenario. The code (3aa04 / 2623f) would pick list[min(+0x56, +0x19) - 1].

### 2.3 The decoded campaign

Text = BWD records `<first 4 letters of the scenario>BRF1/BRF2/DBFS/DBFF` in MW2.PRJ (sach uses `sachbrf1`, its videos are
`sash*`). SDSC = BRF2 star description {+8: ?(2), +0xc tonnage limit, +0x10 star size, +0x14 max size, loadouts}; SUPS =
the `-b=` argument. Location: 0 Alshain (Sibko hall: free 'Mech choice), 1 DropShip / JumpShip (agbbr* hub),
2 Mannedorf base (agbtb* hub) - names INFERRED from the clips and briefing texts.

| i | scenario | title | trial | SDSC ton/size/max, loadouts | SUPS | brief snd | debrief snd | loc after | before sim | on win | on win EXIT |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | bassSCN1 | Trial on Alshain | 1 | 90/1/1 grz00std | bass | 1 | 23 | 2 | - | - | to attach warp sep |
| 1 | jessSCN1 | Defend on Mannedorf | 0 | 100/1/1 | jess | 2 | 24 | 2 | - | - | - |
| 2 | claiSCN1 | Patrol on Mannedorf | 0 | 100/1/2 | clai | 3 | 25 | 0 | - | mraid | attach warp sep land |
| 3 | newcSCN1 | Trial on Alshain | 0 | 100/2/3 kdk smn exe (00std) | newcstl | 4 | 26 | 1 | - | - | to attach warp |
| 4 | jeehSCN1 | Defend on Chandler | 0 | 100/1/1 | jeeh | 5 | -1 | 1 | sep | attach | - |
| 5 | tim_SCN1 | Strike on Chandler | 0 | 100/1/1 | tim | 6 | 28 | 0 | sep | attach | warp sep land |
| 6 | andySCN1 | Strike on Wolcott | 0 | 100/1/2 | andy | 7 | 29 | 1 | to attach warp sep | attach | - |
| 7 | fostSCN1 | Circle of Equals | 0 | 100/1/1 | fost | 8 | 30 | 1 | sep | attach | - |
| 8 | hoffSCN1 | Raid on Jeanette | 0 | 100/1/2 | hoff | 9 | -1 | 1 | sep | attach | - |
| 9 | zackSCN1 | Defend on Jumpship | 0 | 100/1/1 | zack | 10 | 32 | 1 | - | - | - |
| 10 | seanSCN1 | Trial of Possession | 0 | 100/1/1 | sean | 11 | 33 | 1 | sep | attach | warp |
| 11 | franSCN1 | Defend on Alshain | 0 | 100/1/1 | fran | 12 | 34 | 0 | sep | - | - |
| 12 | asahSCN1 | Trial on Alshain | 1 (+0x14=1) | 100/1/1 bhm00std | asahi | 13 | 35 | 1 | - | - | to attach warp |
| 13 | peteSCN1 | Underwater Strike | 0 | 100/0/0 (mech fixed by the scene) | pete | 14 | 36 | 1 | sep | attach | - |
| 14 | cathSCN1 | Strike on Moritz | 0 | 100/1/2 | cath | 15 | 37 | 1 | sep | - | - |
| 15 | sachSCN1 | Strike on Moritz 2 | 0 | 100/1/2 | sach | 16 | 38 | 1 | - | attach | - |
| 16 | branSCN1 | Destroy Orbital Platform | 0 | 100/1/1 | bran | 17 | 39 | 0 | sep | mend attach warp sep land | - |
| 17 | gui1SCN1 | Bloodright Trial 1 | 0 | 100/1/1 | guin1 | 18 | 40 | 0 | - | - | - |
| 18 | gui2SCN1 | Bloodright Trial 2 | 0 | 100/1/1 | guin2 | 19 | 41 | 0 | - | - | - |
| 19 | gui3SCN1 | Bloodright Trial 3 | 0 | 100/1/1 | guin3 | 20 | 42 | 0 | - | - | - |
| 20 | gui4SCN1 | Bloodright Trial 4 | 0 | 100/1/1 | guin4 | 21 | 43 | 0 | - | - | - |
| 21 | guinSCN1 | Final Bloodright Trial | 0 | 100/1/1 | guin5 | 22 | 44 | 0 | - | - | - |
| 22 | (NULL) | Retired | 0 | | | -1 | -1 | 0 | | | |

DBSOUND.MW2 (only on the CD, `INSTALL/DATA/SMK/DBSOUND.MW2`, 44 RIFF WAVs; the DOS rip does not have it): entry = number - 1,
so entries 0-21 are the briefings, 22-43 the debriefings (entries 26 / 30, jeeh / hoff, are duplicates of 27 / 31 and unused).
Videos that do not exist on the GBL disc: JESSBRF1, JESSDBFS, BASSDBFS, ASAHDBFS (bass / asah use the rank clips instead
of dbfs, see 5.4); the shell's player then simply fails to open them (INFERRED from FUN_0003d1a0 / 0x3c430 return paths).

### 2.4 How the next mission is chosen (TRACED, screen 3 0x25780-0x25a70 and EXIT 0x26061-0x26161)

There is no branching to different scenarios: the campaign is linear. A success advances, a failure retries the same mission
(the alternative table a93b8 is empty).

- Success (MW2MSN status 2, or a trial forced to success): unless "altered reality" is on (MW2DIF +0 unlimited ammo == 1 or
  +1 invulnerable == 1 or +3 == 0): play the mission's "on win" clips, then +0x56 = 0, **+0x0c += 1**, +0x5a = mission +0x1d,
  and `if (+0x52 != 0 && +0x0c > 16) +0x0c = 22` - a pilot who ever failed a mission is retired after the orbital platform
  (index 16) and never sees the Bloodright trials 17-21. With altered reality on, nothing advances
  ("\cNo Career Advancement with Altered Reality Enabled", honor 0).
- Failure (status 3): +0x56 += 1; "deathmov" plays when `0 < +0x56` (always), the mission is not a trial, index < 17,
  index != 3 (newc) and MW2CAR +0x1d has bit 4 and not bit 2 (the player's 'Mech was destroyed, he did not eject - sim
  FUN_00025680 sets 1 alive / 2 ejected / 4 destroyed); then +0x52 += 1 and +0x28 = "<base name> NN".
- Any other status (0 aborted, 4 time): no change.
- Trial ranks (bass index 0, asah index 12; mission-list byte = 1): the mission counts as WON when the player destroyed any
  'Mech (MW2CAR +0x07 + +0x1e != 0) or when +0x14 is set (asah: always), whatever the simulation said; objective 0 is set
  succeeded. If not altered and heat tracking is on (MW2DIF +4 == 1): **rank += number of succeeded objectives** (bass:
  Primary/Secondary/Tertiary = 1st/2nd/3rd 'Mech destroyed -> Mechwarrior / Star Commander / Star Captain; asah: one rank per
  'Mech), clamped to 6; when the rank is still < 6 the text "\nYou have earned the rank of %s.\n\n" (0x768b8, rank title of
  the new rank) is built into 0x92180 and replaces the `\q` mark of the DBFS text. "If you fail to defeat even one" the Blooding
  is a failure (status from the sim, no kills -> 3) and bassDBFF is shown; the pilot stays at index 0 and retries (+0x52 > 0:
  no Bloodright later).
- Finale: Hub MISSION COMPUTER (FUN_0001e270 case 0) goes to screen 5 only while `+0x0c < 22 && (+0x52 == 0 || +0x0c < 17)`,
  else "This pilot has|already won the game.#..." (0x735d0) and state 16. On the debriefing EXIT after a win:
  `+0x52 == 0 && +0x0c >= 22` -> state 16 directly; `+0x0c >= 22` (failed pilot) -> 1. State 16 (FUN_00039ec0):
  `+0x52 == 0 && +0x0c != 17` -> FUN_0001dbd0 (the winner's ending screen), else the video "mend" then state 1.
  So: flawless pilot = 22 missions + ending screen; pilot with failures = 17 missions, "mend" (played after bran's win anyway)
  and the "already won" message.

C table (ready to use):

```c
typedef struct { const char *scn, *title; unsigned char trial; } gbl_mission;     /* 0x800ac */
static const gbl_mission GBL_MISSIONS[23] = {
  {"bassSCN1","Trial on Alshain",1}, {"jessSCN1","Defend on Mannedorf",0}, {"claiSCN1","Patrol on Mannedorf",0},
  {"newcSCN1","Trial on Alshain",0}, {"jeehSCN1","Defend on Chandler",0}, {"tim_SCN1","Strike on Chandler",0},
  {"andySCN1","Strike on Wolcott",0}, {"fostSCN1","Circle of Equals",0}, {"hoffSCN1","Raid on Jeanette",0},
  {"zackSCN1","Defend on Jumpship",0}, {"seanSCN1","Trial of Possession",0}, {"franSCN1","Defend on Alshain",0},
  {"asahSCN1","Trial on Alshain",1}, {"peteSCN1","Underwater Strike",0}, {"cathSCN1","Strike on Moritz",0},
  {"sachSCN1","Strike on Moritz 2",0}, {"branSCN1","Destroy Orbital Platform",0}, {"gui1SCN1","Bloodright Trial 1",0},
  {"gui2SCN1","Bloodright Trial 2",0}, {"gui3SCN1","Bloodright Trial 3",0}, {"gui4SCN1","Bloodright Trial 4",0},
  {"guinSCN1","Final Bloodright Trial",0}, {NULL,"Retired",0}};
typedef struct {                                   /* 0x7fb8c, stride 0x39 */
  const char *brf_video, *dbfs_video; int brief_snd, debrief_snd, always_pass, location;
  const char *pre[5], *win[5], *exitwin[5];        /* NULL-terminated clip lists */
} gbl_mission_x;
static const gbl_mission_x GBL_MX[23] = {
  {"bassbrf1","bassdbfs", 1,23,0,2, {0},{0},{"to","attach","warp","sep"}},
  {"jessbrf1","jessdbfs", 2,24,0,2, {0},{0},{0}},
  {"claibrf1","claidbfs", 3,25,0,0, {0},{"mraid"},{"attach","warp","sep","land"}},
  {"newcbrf1","newcdbfs", 4,26,0,1, {0},{0},{"to","attach","warp"}},
  {"jeehbrf1","jeehdbfs", 5,-1,0,1, {"sep"},{"attach"},{0}},
  {"tim_brf1","tim_dbfs", 6,28,0,0, {"sep"},{"attach"},{"warp","sep","land"}},
  {"andybrf1","andydbfs", 7,29,0,1, {"to","attach","warp","sep"},{"attach"},{0}},
  {"fostbrf1","fostdbfs", 8,30,0,1, {"sep"},{"attach"},{0}},
  {"hoffbrf1","hoffdbfs", 9,-1,0,1, {"sep"},{"attach"},{0}},
  {"zackbrf1","zackdbfs",10,32,0,1, {0},{0},{0}},
  {"seanbrf1","seandbfs",11,33,0,1, {"sep"},{"attach"},{"warp"}},
  {"franbrf1","frandbfs",12,34,0,0, {"sep"},{0},{0}},
  {"asahbrf1","asahdbfs",13,35,1,1, {0},{0},{"to","attach","warp"}},
  {"petebrf1","petedbfs",14,36,0,1, {"sep"},{"attach"},{0}},
  {"cathbrf1","cathdbfs",15,37,0,1, {"sep"},{0},{0}},
  {"sashbrf1","sashdbfs",16,38,0,1, {0},{"attach"},{0}},
  {"branbrf1","brandbfs",17,39,0,0, {"sep"},{"mend","attach","warp","sep","land"},{0}},
  {"gui1brf1","gui1dbfs",18,40,0,0}, {"gui2brf1","gui2dbfs",19,41,0,0}, {"gui3brf1","gui3dbfs",20,42,0,0},
  {"gui4brf1","gui4dbfs",21,43,0,0}, {"guinbrf1","guindbfs",22,44,0,0}, {"","",-1,-1,0,0}};
```

Mission objectives (BRF1 ORDR text, the part shown in the BRIEFING box) - the win / lose rules are the scenes' own:
bass DESTROY 1/2/3 'Mechs (primary/secondary/tertiary) | jess DEFEND HPG uplink, DESTROY all 'Mechs, return to hangar |
clai DESTROY all units, return NAV DELTA | newc DESTROY all opposing 'Mechs (3 vs 3) | jeeh DEFEND DropShip, DESTROY 'Mechs |
tim DISABLE transport, DISABLE Tengwan's 'Mech + DESTROY others | andy IDENTIFY Rentoshi's Hatamoto-Chi, DESTROY others |
fost DESTROY your commander (Circle of Equals) | hoff INSPECT structures near NAV ALPHA | zack DEFEND JumpShip, DESTROY all |
sean WAIT for orders | fran DEFEND base, DESTROY 'Mechs | asah DESTROY 1/2/3 'Mechs = +1/+2/+3 ranks | pete IDENTIFY and DESTROY
stolen 'Mechs (Executioner fixed, Mech Plant ignored) | cath LOCATE genetic material, DESTROY buildings, DESTROY 'Mechs |
sach LOCATE Tseng's material, DESTROY 'Mechs | bran DESTROY reactor at NAV GAMMA, DESTROY 'Mechs | gui1-guin DESTROY Jocelyn
(Summoner) / Victoria (Timber Wolf) / Grendel (Executioner) / Bjorn (Kodiak) / Ragnarok (Grizzly).
Full texts: `<xxxx>BRF1` (orders `\s` situation), `<xxxx>DBFS` / `DBFF` (summary `\q` extended aftermath).

## 3. State machine paths of the campaign (FUN_00039ec0, TRACED)

`hub (1) --MISSION COMPUTER--> 5 --LAUNCH--> 10 (sim) --> 3 (debriefing) --LOGOFF--> 1`, `3 --REPLAY--> 5`, `5 --LOGOFF--> 1`,
`5 --slot name--> 9 (Mech Lab)`, `3 (win, last mission) --> 16`.

- State 5 entered from 1 (hub) or 3 (debriefing REPLAY): `FUN_0003e350(0, 0,3,1,100)` (friendly star: formation 0, max 3,
  size 1, 100 t), scenario = list[+0x0c] (or the a93b8 alternative, always NULL), then from 1: `FUN_0002c810(scn, 1)` = BRF2
  defaults (SUPS -> the `-b=` string 0x84dfc; first SDSC -> friendly star max / size / tonnage + its loadouts; a second SDSC
  would fill the enemy star and set the enemy skill base DAT_0007b728 - no GBL mission has one); from 3: the star is restored
  from the pilot's saved copy (+0x5e formation, +0x66 max, +0x6a size, +0x6e tonnage, slots +0x76 + k*0x40).
  Always: `FUN_0003e350(1, 0,0,0,100)` (enemy star EMPTY - the campaign never writes enemy lances), select star 0, slot 0.
- State 1 entered from 12 (roster ACCEPT): star 0 = (0,0,3,1,100) and slot 0's pilot name = pilot +0x28.
- State 10 (launch): next state = 3 when coming from 5 or 9 (unless the roster's LAUNCH OLD MISSION flag 0x95e88 is set:
  then 12, no debriefing); from 5 the mission's "before sim" clips play first; then FUN_0003af90 writes the handoff
  and the shell exits with code 3 (only when started by GBL.EXE: DAT_0007e784 is cleared when argc != 2).
- State 16: see 2.4. State -3 writes MW2PRM.CFG with screen -3 and command "exittos" (exit code 0xff).
- Music: the per-screen tables 0x84d68 / 0x84db0 are all 0 / 0x20000000 (keep): the GBL shell plays no XMIDI music on these
  screens (MEASURED: DOSBox had no sound configured, so not verified by ear).

## 4. Screen 5 - Mission Computer (FUN_00028410(db, scenario, ctx, flag, prev_screen))

DOS captures: `C_07mission.png` / `C_10mission.png` (bass, new pilot), `C_11hover_details.png`, `C_15hover_launch.png`,
`C_12details.png` (DETAILS pager), `C_14back.png`, `C_37after_right.png` (after REPLAY from the debriefing).

Panels (all baked into the background, slot 14 = 0x801c8): BRIEFING (top), INTELLIGENCE (bottom left), MECH PLANT (bottom right).

### 4.1 Entry
- `petescn1` (strcmp, 0x76b85): the star is fixed - no slot names are made and hotspots 2-4, 6-11 are disabled.
- Text: BWD record `<first 4 letters, '_' padded>BRF1` (0x76b8e), ORDR chunk, paginated twice by FUN_0001c600 with font
  0x95ebc (DATABASE font 0x22, see note) and colour map 0x7e838:
  box A (85,27) w 469 h 403 -> page 0 drawn in the BRIEFING panel (the orders up to the first `\s`);
  box B (85,27) w 500 h 403 -> the DETAILS pager. Markup (FUN_0001c600): `\h` pilot honor ("%d"), `\q` / `\Q` replaced by the
  caller's text (screen 5: "" ; debriefing: the rank message), `\r0` / `\r1` / `\r2` rank title (current / +1 / +2, max 7);
  everything else (`\n`, `\c`, `\s` page break, `\t`, `\g`, `\b`) is left to the text renderer FUN_0003f4d0.
  If box A has no page at all, the screen returns 10 at once (launch without briefing).
- Coming from the hub (prev_screen 1): background slot 19 and the clip agbmcx01 at (190,431) flags 0x40 played to the end
  (FUN_0003d410) - the zoom into the computer - before the screen proper.
- Voice: DATABASE sound 0x7a (vol 0x32), and DBSOUND number mission+0x0c (vol 0x4b) when DBSOUND.MW2 is open.
- Two clip lists 0x7f96c / 0x7f974 (count 1 each, records at 0xa9414 / 0xa9424) are BSS zeros: two empty looping slots
  13 / 14 (INFERRED: unused leftovers).

### 4.2 Clips (FUN_0003d1a0(slot, name, x, y, flags, 0); TRACED)
| slot | name | x,y | flags | role |
|---|---|---|---|---|
| 12 | mission +0 `<xxxx>brf1` | 40,267 | 0x240 | INTELLIGENCE video (plays once on entry; VIEW replays it with the voice) |
| 7 | wgbmc001 | 2,8 | 0x41 | DETAILS button |
| 6 | wgbmc002 | 2,119 | 0x41 | LOGOFF button |
| 5 | wgbmc003 | 552,8 | 0x41 | LAUNCH button |
| 0 / 1 | wgbmc004 / wgbmcg04 | 14,326 | 0x44 | VIEW button (normal / grey while the video plays) |
| 2 / 3 / 4 | wgbmc005 | 335,306 / 335,345 / 335,382 | 0x44 | the three star-slot name boxes with arrows |
| 11 | `wgbmc%s` (2-letter code of slot 0's 'Mech, table 0x84a20 +0) | 423,284 | 0x48, 0x14 | MECH PLANT wireframe of the player's 'Mech |

Buttons: frame 2 on hover, 1 on click, 0 idle (FUN_0003d7c0). MEASURED: LAUNCH brightens under the cursor (C_15).

### 4.3 Hotspots 0x7ec00 (13; table record 0x801c0 = {0x7ec00, 13, background 14, music -1})
```c
static const gbl_hot MC_HOT[13] = {          /* x0,y0,x1,y1, label x,y */
  {  2,  8, 90,124,   0,  0}, /* 0 DETAILS  -> pager FUN_0002a590 over box B, background slot 22, buttons 0x802a0 */
  {  2,119, 90,235,   0,  0}, /* 1 LOGOFF   -> save pilot (FUN_0001e1c0), return 1 (12 after a roster old-mission) */
  {335,306,422,323, 345,313}, /* 2 slot 0 name -> select slot, screen 9 (Mech Lab) */
  {335,345,422,362, 345,352}, /* 3 slot 1 name */
  {335,382,422,399, 345,389}, /* 4 slot 2 name */
  {552,  8,640,232,   0,  0}, /* 5 LAUNCH   -> save pilot, return 10 */
  {396,324,422,335,   0,  0}, /* 6 slot 0 next 'Mech */
  {396,363,422,374,   0,  0}, /* 7 slot 1 next */
  {396,400,422,411,   0,  0}, /* 8 slot 2 next */
  {335,324,361,332,   0,  0}, /* 9 slot 0 previous 'Mech */
  {335,363,361,371,   0,  0}, /* 10 slot 1 previous */
  {335,400,361,408,   0,  0}, /* 11 slot 2 previous */
  { 14,326, 34,394,   0,  0}, /* 12 VIEW: replay the briefing video + voice when it is not playing */
};
```
### 4.4 Star ("Starmate") selection - the base Star Configuration folded into the MECH PLANT panel
Star record 0x856b0 (friendly) / 0x85784 (enemy), 0xd4 bytes: +0 formation, +4 current slot, +8 max size (<= 3),
+0xc size, +0x10 tonnage limit, slots at +0x14 + k*0x40 {+0 'Mech index (table 0x84a20), +4 loadout[16] ("grz00std"),
+0x14 name 1 [22], +0x2a name 2 [22]}. API: FUN_0003e350(star, formation, max, size, tonnage) (-1 = keep; star selects
the current record 0x85858), FUN_0003e0b0(slot, mech, name1, name2) (mech by its 3-letter code: unknown code removes the last
slot; a chassis over the tonnage limit is refused (returns 0); a 3-letter code gets "00std"; slot == size grows the star),
FUN_0003e2e0(slot) = 'Mech index or -1, FUN_0003e330(star) = record.
- On entry from 1 or 3: slots 1 / 2 get name1 "GhostBear", name2 "Starmate 2" / "Starmate 3" (MEASURED in MW2PRM.CFG);
  the star keeps max / size / tonnage.
- Slot boxes show the table name (0x84a28 +8, "Grizzly") or "None" (index < 0); hovering a slot that has a 'Mech shows
  " MECH LAB " (0x76b62) in that box; hover colour map 0xa9298 (colour 1 -> 0x50), normal 0xa9198. Box text at the
  hotspot label points (345, 313 / 352 / 389), font 0x95ea4.
- Arrows (hotspots 6-11), only for slots < max (acStack_24) and (slot <= pilot +0x206 or location 0):
  location 0 (pilot +0x5a == 0, the Sibko / 'Mech plant): next / previous chassis in the 24-entry table (0x7e83c),
  wrapping to "None" (-1), skipping chassis over the tonnage limit (FUN_0003e0b0 refuses -> loop); "None" on slot k sets
  +0x206 = k - 1, a 'Mech sets +0x206 = k. Location 1 / 2: slot 0's arrows are disabled (the player keeps his 'Mech -
  bass's briefing: "choose your 'Mech well, as you will take it with you wherever you are posted") and a starmate's arrow
  toggles between "None" and the pilot's saved 'Mech for that slot (+0x72 + k*0x40).
- Changing slot 0 redraws the wireframe clip 11.
- Differences from the base (the base shell "Star Configuration" / "Ready Room" / "Briefing"): no formation choice (always 0 =
  echelonl), no ADD / DELETE STARMATE buttons, no separate star screen, no SKIP / FERRARI, no trial "'Mech has been selected"
  messages (trials 0 / 12 let you choose; pete is fixed by name), no orbit / planet clip, briefing + video + star on one
  page; the briefing text is the same BRF1 ORDR record (base: KTWOBRF1 special case gone).

### 4.5 DETAILS pager (FUN_0002b0e0 + FUN_0002a590, also the debriefing AFTERMATH)
Full-screen text (`C_12details.png`): background slot 22 (screen 5) / 20 (debriefing), the page collection from box B
(screen 5: the situation pages after `\s`, MEASURED) drawn at its box. Buttons (FUN_0002a590 0x2a5dc..):
wgbam001 / wgbamg01 at (116,437) (slots 31 / 30: previous page, grey when none), wgbam002 / wgbamg02 at (278,437)
(slots 29 / 28: EXIT, returns -5 -> back to the screen), wgbam003 / wgbamg03 at (436,437) (slots 27 / 26: next page).
Hotspots 0x802a0 (screen 5) / 0x80260 (debriefing), 4 each: 0 (278,437)-(361,465) EXIT, 1 (116,437)-(204,465) previous,
2 (436,437)-(524,465) next, 3 (510,550)-(609,574) off-screen (first page); keys Home 0x8047 -> first page, PgUp 0x8049 ->
previous, PgDn 0x8051 -> next. With one page only EXIT is visible (MEASURED C_12 / C_13: next does nothing).

## 5. Launch handoff (state 10 -> FUN_0003af90 -> FUN_0003e3b0 -> FUN_0001d0b0 / FUN_0001d430)

DOS evidence: files written by the GBL shell launching bass with pilot TESTC (`ref/C_MW2PRM.CFG`, `C_USERSTAR.BWD`,
`C_EN01STAR.BWD` .. `C_EN05STAR.BWD`, `C_MW2PRM_r3.CFG`), the sim starting (`C_16launch1.png`, `C_17launch2.png`,
`C_30sim.png`, sim Esc menu `C_31sim_esc.png`), the sim's `SYMLOG.TXT` (`ref/C_SYMLOG.TXT`).

### 5.1 MW2PRM.CFG (0x2c0 = 704 bytes; base 0x218 = 536)
| off | GBL | value in the capture | base |
|---|---|---|---|
| +0x000 | int return screen (after the sim the shell resumes here: 3 debriefing, 12 roster old mission, 7 IA; -3 = quit) | 3 | same |
| +0x004 | int clan context | 0 | same |
| +0x008 | u8 flag (state machine local_18, passed through; INFERRED "pilot present"), +0x009 forced 0 | 1 | same |
| +0x00c | int 1 = the friendly star is the current star record (0x85858 == 0x856b0) | 1 | - |
| +0x010 | star record 0 (friendly) 0xd4 bytes, layout 4.4 | {0,2,1,1,90; 12 "grz00std" "TESTC" ""; 0 drw00std "GhostBear" "Starmate 2"; 1 frm00std "GhostBear" "Starmate 3"} | base star 0x84 bytes |
| +0x0e4 | star record 1 (enemy) 0xd4 | {0,0,0,0,100; static defaults "tbr00std" "Enemy 1"...} | |
| +0x1b8 | int (0) | 0 | |
| +0x1bc | int pilot index ((DAT_000987e8 - 0x95ed0) / 0x20e, -1 none) | 0 | +0x114 |
| +0x1c0 | char[256] command line for MW2.EXE | `bassSCN1 -b=bass -of=echelonl` | +0x118 |

Command line (FUN_0003af90 + FUN_0003e3b0): `<scenario>` + " -b=" + 0x84dfc (the BRF2 SUPS string; "xxxxxxxx.xxx" when the
mission has none) + (" -x" when DAT_000adcec, set by FUN_0003ae90 - INFERRED net / not used in the campaign) + " -of=" +
formation[star0 +0] + (" -oe=" + formation[star1 +0] only when the enemy star size != 0). Formations 0x85860: echelonl
echelonr lineabreast lineastern vform wedge. The campaign always sends -of=echelonl and no -oe. GBL.EXE (NOTES.md: reads
mw2prm.cfg 0x2c0 bytes, runs `mw2.exe <prm+0x1c0>`, then `mw2shell sim`; first start `mw2shell intro`). The shell restarts
with "sim", FUN_0003aeb0 reads the file back (return screen, ctx, flag, scenario = the command line cut at the first space,
pilot pointer from +0x1bc, both star records restored by FUN_0003e060).

### 5.2 USERSTAR.BWD / EN01STAR..EN05STAR.BWD (FUN_0001d0b0, FUN_0001ce30 GPS writer)
Container identical to the base (BWD header, REV "1.22", empty DTBL 28 bytes) - `lance.c begin()` holds. Differences:
- **GPS record is 100 bytes** (base 92): +0 "GPS", +4 100, +8 MEK record id of the loadout when loadout[5..] == "std"
  else 0xfffe, +10 chassis BWD id (from the table file name), +12 file number (0 user, 1-5 enemies), +13 leader (slot 0),
  +14 0 = player (file 0 slot 0) else 2, +16..+25 skill row (table 0x7b6cc = base 0x7a7a8, same values; skill 0 for file 0),
  +26..+31 0, +32 6, +34 0x400, +36 chassis file[8] +44 0, +45 loadout[8] +53 0, +54 **name 1** (slot +0x14, up to 21 chars)
  +75 0, **+76 name 2** (slot +0x2a, 21 chars) +97 0, +98..99 0 (buffer zero). Bytes measured in `C_USERSTAR.BWD`:
  MEK 0x008a (grz00std), chassis 0x0773, "grizzly", "grz00std", "TESTC", name 2 "". Base lance.c copies the pilot with a
  15-char limit and has no name 2.
- Enemy files: one per file number 1..5, skill base DAT_0007b728 (8, or a mission's second SDSC +8) - 2 (MEDIUM),
  -1 EASY, -3 HARD (MW2DIF +5), one lower per file (base: fixed 8). Units = the enemy star's slots. In the campaign the enemy
  star size is 0, so **EN01..EN05STAR.BWD hold no GPS at all** (52 bytes, MEASURED); the scenes carry their own enemies.
- Instant Action only (return screen 7): random enemy star (1..3 'Mechs, random chassis 0..29, unique random pilots from
  0x7b690 "Nova Cpt. Mamais/Findley/Burton/Ferrer/tik/Lovecraft/Franzen/Jody", name 1 "Enemy Mech"; a Stone Rhino ("bhm")
  is always "Nova Cpt. Franzen"), loadout "%s00std" (0x73438). (Instant Action itself is another agent's topic.)
### 5.3 INSTMAP1.BWD (FUN_0001d430(friendly camo, enemy camo))
Same format as the base / lance.c: BMPJ (CEL id + name from 0x7b738: l1wolfcl l1jadefn l1gostbr l1smojag l1novact
l1steelv) + BMID 0x114, the enemy's + BMID 0x115, "jscamoia" + BMID 0x101. Its only caller is the Instant Action LAUNCH
(FUN_0002cc80 case 0: `FUN_0001d430(2, 1)` = l1gostbr vs l1jadefn, fixed). The campaign launch does NOT write it
(MEASURED: no INSTMAP1.BWD in the DOS copy after the campaign launch, run 3 file list).

## 6. Screen 3 - Debriefing (FUN_00024750(db, ctx, &scenario))

DOS captures: `C_34debrief.png` (bass aborted with Ctrl+Q: MW2MSN status 0), `C_35/36_debrief_hover_*.png`,
`C_44debrief_win.png` (pilot IMMORTAL, status 3 run - see 6.5), win captures `C_52..C_59` (Alt+W cheat, 6.5).

### 6.1 Inputs from the simulation (GBL MW2.EXE)
- `MW2MSN.CFG` (0x9d4 bytes, sim FUN_00017300; same as the base): "MW2M", +4 count, +8 / +0xc star fields, +0x10 status
  (2 success, 3 failed, 4 time, 0 = aborted / no result), 48 x 52-byte objectives at +0x14 {+0 succeeded (state == 5),
  +4 type (1 primary, 2 secondary, 4 tertiary, 8 return), +8, +0xc time or -1, +0x10, +0x14 text[32]}.
  Measured (`ref/C_MW2MSN_r3.CFG`): bass after Ctrl+Q: count 3, status 0, {0,1,-1,-1,1,"Destroy First 'Mech"},
  {0,2,..,"Destroy Second 'Mech"}, {0,4,..,"Destroy Third 'Mech"}.
- `MW2CAR.CFG` (0x50 bytes read; sim FUN_00052ea0 writes 0xb6f80): u16 +0x07 'Mechs the player destroyed, +0x13 shots,
  +0x15 hits, +0x1d **byte player state (1 alive, 2 ejected, 4 destroyed; sim FUN_00025680)**, +0x1e 'Mechs destroyed
  (total), +0x34 friendly units lost, +0x44 / +0x4a vehicles direct / total (same counters as the base).
- `MW2DIF.CFG` (8 bytes, 0x7e804, read at start FUN_00026c20): +0 unlimited ammo, +1 invulnerable, +3 collision damage
  (0 = off = "altered"), +4 heat tracking, +5 difficulty 0/1/2. Install default 00 00 01 01 01 01 00 00.
"Altered reality" = +0 == 1 || +1 == 1 || +3 == 0.

### 6.2 Scoring text (TRACED 0x24750.., strings 0x76500-0x76864; MEASURED C_34)
Statistics box: FUN_0003ee70 at (85,268) w 469 h 196, font 0x95ebc, colour map 0x91080; "\g%03d\b%03d%s" puts the value
right-aligned at x 85 + 350 = 435 (MEASURED). Lines, in order:
1. "Time\g040Type\g110Objective\g400Status\n", then the objectives (sorted with FUN_0004b7aa; MEASURED order Tertiary,
   Secondary, Primary): "%s\g040%s\g110%s\g400%s\n" = time "%02d:%02d" or "DNF", type Default/Primary/Secondary/Tertiary/
   Return/Unknown, text (runs of control chars collapsed), Successful / Failed / Unknown.
2. "Mission Completion:" 5000, 0 if any primary failed.
3. "Secondary Objective Completed:\t\t%d\t(x%d)" n x 1500 (only when n > 0); "Tertiary ..." n x 500 (n > 0).
4. "Friendly Unit Deaths:\t\t\t\t%d\t(x%d)" -4000 each, only when deaths > 0 and status 2.
5. "\t\t\t\t\t\tdirect\ttotal\n"; "Enemy Mechs Destroyed:\t\t\t%d\t%d" 250 x total (+0x1e); pilot kills += total;
   "Enemy Vehicles Destroyed:\t\t\t%d\t%d" 125 x total (+0x4a); pilot kills += direct (+0x44).
6. "Star Underweight Bonus:\t\t\t%d\t(x%d tons)" 25 per ton of (tonnage limit x **max** size - sum of the tons of the star's
   'Mechs), when > 0 (MEASURED: bass, 90 x 1 - Grizzly 70 = 20 t -> 500).
7. "Hit Percentage:\t\t\t\t%3.1f" (hits / shots, 0 when no shots), 750 when >= 0.7, line shown when the ratio <= 1.0;
   pilot hits / shots accumulate.
8. Heat tracking off: "\cThe Keshik deems it Dishonorable to Alter Heat Tracking\n", honor 0. Else when status 2:
   "Mission Honor:" + "Difficulty Multiplier:\t(%s = %1.1f)" (EASY 0.8, MEDIUM 1.0, HARD 1.3), rounded.
9. Altered reality: "\cNo Career Advancement with Altered Reality Enabled\n", honor 0. Status 3:
   "\n\cMission Failed:  NO HONOR ACQUIRED\n", honor 0.
10. Status 2: pilot honor += honor. "Career Honor:" (MEASURED 1857 for TESTC with status 0: nothing added).
Same numbers as the base (the base shell "Debriefing"); new in GBL: the trial override and ranks (2.4), +0x1d / deathmov,
the failure counter and renamed pilot, the location, and no "wingman" / "underweight" differences (underweight uses the
max star size).

### 6.3 Layout (background slot 21 = 0x80248; panels INTELLIGENCE, DEBRIEFING, STATISTICS baked in)
- DEBRIEFING text: BWD `<4 letters>DBFS` on status 2, else `DBFF` (FUN_00024590), paginated into box (367,29) w 230 h 187
  (page 0 shown, MEASURED C_34: the bassDBFF text) and into the AFTERMATH box (88,30) w 454 h 400; `\q` is replaced by
  the rank message (2.4), so the page break after the summary is the `\q` mark.
- Clips (FUN_0003d1a0): status 2: slot 2 / 3 wgbmc004 / wgbmcg04 at (14,86) flags 0x44 (VIEW normal / grey),
  slot 0 wgbdb001 at (552,248) flags 0x41 (AFTERMATH button), slot 4 = the video at (41,28) flags 0x240: trial missions
  rank clip 0x7e444[min(rank, 5)] = {rankia, rankia, rankib, rankic, rankid, rankie} (rank after the update), else mission
  +8 `<xxxx>dbfs`; voice DBSOUND mission +0x10 (vol 0x4b). Other status: slot 0 wgbdb004 at (552,248) flags 0x41 (REPLAY
  button). Always slot 1 wgbdb002 at (2,248) flags 0x41 (LOGOFF). Entry sound DATABASE 0x7a vol 0x32.
- Hotspots 0x7f178 (4; record 0x80240 = {0x7f178, 4, 21, -1}):
```c
static const gbl_hot DB_HOT[4] = {
  {  2,248, 90,476, 0,0},  /* 0 LOGOFF -> 1 (or 16 / 1 after the last mission, 2.4); after a win plays the "on win EXIT" clips */
  {552,248,640,476, 0,0},  /* 1 AFTERMATH: pager over the AFTERMATH box, background slot 20, buttons 0x80260; win only and
                              only when that text has pages */
  {552,248,640,476, 0,0},  /* 2 REPLAY: back to 5 with the same mission (FUN_0002c810(scn,0): SUPS only); disabled on a win,
                              enabled on status 3, left as created otherwise (MEASURED: works after status 0, C_37) */
  { 14, 86, 34,154, 0,0},  /* 3 VIEW: replay the debriefing / rank video + voice; win only */
};
```
### 6.4 Order of events (TRACED)
read MW2CAR, MW2MSN -> trial override -> copy the pilot (0x20e) for the cheat -> scoring -> pilot updates (2.4) ->
win clips (mission +0x29) or deathmov -> FUN_0001e1c0 save -> text boxes -> screen loop. EXIT after a win: next scenario =
list[new +0x0c] and the "on win EXIT" clips (+0x31) play before returning 1.

### 6.5 IMMORTAL cheat (pilot base name +0x3d == "IMMORTAL", 0x7e818 XOR 7)
In the debriefing, key 0x8011 (getch 0 + scan 0x11 = **Alt+W**) restores the pilot copy and redoes the debriefing as a win
(status 2); 0x8026 (**Alt+L**) as a failure (status 3). Not in context 2. (Plain W did nothing: C_44.)

### 6.6 DOSBox check of a win (pilot IMMORTAL, bass, Ctrl+Q in the sim, then Alt+W in the debriefing; run 5)
- `C_52win_a.png` / `C_53win_b.png`: INTELLIGENCE shows the rank clip (rank 0 -> rankia, "RANK RECEIVED", then the
  MechWarrior insignia `C_59view.png` after VIEW), VIEW button at (14,86); DEBRIEFING box = bassDBFS summary (the part before
  `\q`); right button = AFTERMATH (wgbdb001); STATISTICS add "Mission Honor: 500" and "Difficulty Multiplier: (MEDIUM = 1.0) 500",
  Career Honor 1205 -> 1705.
- `C_56aftermath.png`: AFTERMATH pager (header "AFTERMATH", background slot 20) with the bassDBFS text after the summary,
  EXIT only (one page). DETAILS in screen 5 likewise shows the BRF1 text after `\s`: both pagers start with the second page
  of their collection (MEASURED; mechanism INFERRED from FUN_0002b0e0's start index).
- LOGOFF then played the "on win EXIT" clips to attach warp sep (`C_60..C_64`, full screen 640x400 SMKs, aborted by a click).
- Pilot afterwards (`ref/C_MW2REG_r5.CFG`): +0x0c 0 -> 1, +0x5a 0 -> 2 (location of bass), +0x52 0, +0x56 0, honor 1705, rank 0
  (no objective succeeded, so no promotion and no "You have earned the rank" text).
- Status 0 run (`C_34`, `ref/C_MW2REG_r3.CFG`): nothing changed (+0x52 / +0x56 stay 0), DBFF text, REPLAY offered.

### 6.7 DOSBox check of the next mission (pilot after the win, run 6)
- `C_70hub_loc2.png` / `C_71hub_loc2_hover.png`: the hub is now location 2 (table 0x801a0 -> 0x7eb0c, the Mannedorf barracks,
  "~MISSION COMPUTER" centred at (305,304)) - confirms mission +0x1d -> pilot +0x5a -> hub variant.
- `C_72jess_mission.png`: jess briefing; INTELLIGENCE empty (JESSBRF1.SMK does not exist on the disc); slot 0 "Grizzly";
  `C_73`: slot 0's next arrow does nothing (location != 0); `C_74` / `C_75`: slot 1's arrow does nothing (jess max size 1).
- Quirk (TRACED, not launched): on a fresh shell start the star record is the static default 0x856b0 {12, "tbr00std",
  "MechWarrior"}, {0 drw00std "Friend 1"}, {1 frm00std "Friend 2"} (base leftovers) until the roster restores the pilot's copy
  (FUN_0003b8d0 restores +0x5e.. only when a pilot row is clicked). Index 12 is the Grizzly in the GBL table, so the box says
  "Grizzly" while the loadout is tbr00std; a pre-selected pilot who launches without clicking his row would get the
  loadout's chassis (FUN_0001ce30 looks the chassis up by the loadout's first 3 letters) - INFERRED.

## 7. Differences from the base shell (summary)
| base (the base shell) | GBL |
|---|---|
| two clans, 16 missions each, records 0x7f04c + clan*4 {scn, trial, title} | one campaign, 22 missions, 0x8017c[0] -> 0x800ac, same 9-byte record; "Retired" terminator |
| Ready Room 11 + Briefing 0 + Star Configuration 13 | one Mission Computer screen 5 (FUN_00028410) |
| trial byte = "the 'Mech is assigned" (messages) | trial byte = rank trial (kills -> success, objectives -> ranks, rank clips) |
| briefing text BRF1 ORDR, SITUATION button (page 2) | BRF1 page 0 in the BRIEFING box, the rest in the DETAILS pager; video `<x>brf1` + DBSOUND voice |
| SKIP (FERRARI), FREEBIRTHTOAD mission picker | none in these screens (cheats: IMMORTAL Alt+W / Alt+L in the debriefing) |
| formation chosen in Star Configuration | always 0 (echelonl) in the campaign |
| debriefing: EXIT / AFTERMATH / REPLAY(Are you sure) | LOGOFF / AFTERMATH (win) or REPLAY (no win, no question) / VIEW; rank / dbfs video, DBSOUND voice |
| campaign progress on success only | + failure count (+0x52, renames the pilot "NAME NN", blocks the Bloodright trials), retry count, location |
| MW2PRM 536 bytes, cmd at +0x118 | 704 bytes, cmd at +0x1c0, both star records saved (0xd4 each) |
| GPS 92 bytes, pilot name 15 chars | GPS 100 bytes, name 1 at +54 (21), name 2 at +76 (21) |
| enemy lance files from the enemy star (IA) with skill 8 - diff | same code; campaign enemy star empty -> header-only files; skill base from a 2nd SDSC (none in GBL) |
| landing / ending cinematics per clan | clip lists per mission (to attach warp sep land mraid mend), deathmov, ending state 16 |

## 8. Files in ref/
Captures: C_01..C_08 (first pilot, roster, hub, mission), C_10..C_17 (DETAILS, LAUNCH, sim start), C_30..C_39 (sim Esc menu,
debriefing after Ctrl+Q, REPLAY, LOGOFF to hub), C_40..C_51 (slot hover " MECH LAB ", next 'Mech), C_52..C_64 (win via
IMMORTAL Alt+W, AFTERMATH, VIEW, exit clips), C_70..C_76 (location-2 hub, jess). Files: C_MW2PRM.CFG, C_USERSTAR.BWD,
C_EN01STAR.BWD, C_EN05STAR.BWD, C_SYMLOG.TXT (launch of bass), C_MW2MSN_r3 / C_MW2CAR_r3 / C_MW2REG_r3 / C_MW2PRM_r3 (status 0),
C_MW2REG_testc.CFG (new pilot), C_MW2REG_r4.CFG (pilot IMMORTAL, start), C_*_r5 (after the cheat win). Pilots can be reused
with PRE='cp /home/claude/gbl/ref/C_MW2REG_r5.CFG MW2REG.CFG'.

## Unresolved
1. Win / lose rules inside the scenes (objective nodes) are the simulation's (BWD mission tables) - only the briefing texts
   are listed here. A real kill-based Blooding result (rank 1-3, "You have earned the rank of %s") was not reproduced in DOSBox
   (needs winning fights); the rank message's argument is ASSUMED to be the new rank title (sprintf args not decoded).
2. Location names (0 Alshain / Sibko, 1 DropShip-JumpShip, 2 Mannedorf) are INFERRED from the clips / briefings; hub screens are
   the hub spec's.
3. Mission +0x18 byte: both EXIT branches return 1, effect none (maybe a Ghidra-visible dead branch); +0x19 always 0 with an empty
   a93b8 table.
4. prm +0x008 flag meaning (passed through local_18; 1 in the capture) and +0x1b8 (0) not pinned. " -x" switch (DAT_000adcec)
   source not traced (FUN_0003ae90 callers).
5. Fonts: 0x95ea0 / ea4 / eb8 / ebc all end up = DATABASE font 0x22 (0x3a3cd..dc stores); exact text colours of the briefing
   box (map 0x7e838 = {0,0x18,..}) and the statistics map 0x91080 not compared pixel-by-pixel.
6. Background slot -> DATABASE entry (slot - 1) is the base convention, ASSUMED for slots 14 / 19 / 20 / 21 / 22.
7. Clip-state API meaning (FUN_0003cdc0 / FUN_0003ce00 flags 1 / 0x20 / 0x40000000, FUN_0003d7c0 frames 0/1/2) INFERRED from use.
8. Missing videos (JESSBRF1, JESSDBFS, BASSDBFS, ASAHDBFS): MEASURED empty INTELLIGENCE for jess; the trials use rank clips.
9. The two BSS clip lists 0x7f96c / 0x7f974 (slots 13 / 14 in screen 5) - zero names, ASSUMED harmless no-ops.
10. Launching from the Mech Lab (state 9 -> 10 -> 3) exists in the state machine; not exercised.
