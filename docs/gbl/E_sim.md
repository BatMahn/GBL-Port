# E - GBL simulation (MW2.EXE "REL_11 Addon1") vs the port's sim

Agent E. Sources: GBL sim decompile /home/claude/gbl/re/mw2gbl_decomp.c (VA = Ghidra address), base DOS sim
/home/claude/handoff/mw2-gbl-handoff/re/mw2_decomp.c, 3Dfx DLL d3fx_all.c, port src/mtbl.c / src/msim.c /
tools/sweep_objectives.c (read from a private copy /tmp/E_port; /home/claude/mw2port untouched).

Summary
- BRANSCN1: not a GBL engine difference. The player starts on a moving lift (type-5 collision, path task "lift2") 1438 m
  up; the port's ground ignores moving type-5 objects, so the player drops to y 0, is "outside" the 540 m leave radius
  in 3D and node 5 -> star-lose fires at 0.1 s. GBL's ground (0x1c370 = base) includes them. Port fix in 1.2. DOSBox:
  no failure at start (E_bran2_*).
- ZACKSCN1 and ANDYSCN1: harness problems (teleport into space / destroying the protected General); node 11's "branst01"
  is a data slip that passes harmlessly in GBL as in the port. ANDY passes with the classify() fix (1.4).
- GBL's MTBL runtime, collision, chunk tags and manoeuvre tables are the base code. The gameplay changes are the weapon
  table (35 weapons with callbacks: Inferno SRM / Streak, Torpedo, Arrow IV; re-balanced LB-X / Ultra ACs, LRM5 ammo;
  new Flamer, NARC, Nuke), a real AMS interceptor and NARC instant lock, 36 effects with per-effect splash / heat,
  14 weapons per mech, 255 projectiles, and four boss AI profiles (GPS skill 100-103, GUI1-4) with per-unit engage range.
- The 9 failing PRJ missions are 8-player network arenas, not reachable from GBL's single-player shell; DUMBTURT is an
  invisible armed turret skeleton the port drops (BRAN / ZACK / TIM).

## 1. BRANSCN1 / ZACKSCN1 / ANDYSCN1

### 1.1 The MTBL runtime is unchanged in GBL
- Node runtime: base DOS FUN_00016720 (= 3Dfx 0x1000a1d0) -> GBL FUN_00016810. Not in the masked map (it grew
  0x760 -> 0x890 bytes only because of re-ordered compares / spills), but a token diff of the two decompiles shows the
  SAME kinds, the same tests and the same results. Node stride 0xf7 and table stride 0x2e8a are unchanged
  (base node array DAT_000a578a, GBL DAT_000b70da; node count base DAT_000a5750 -> GBL DAT_000b70a0; clock base
  DAT_000d4030 -> GBL DAT_000e5980; player table base DAT_00096294 -> GBL DAT_000a6724; current node per table base
  DAT_000d3ff0 -> GBL DAT_000e5940; star result base DAT_000a5788 -> GBL DAT_000b70d8, its time base DAT_000a5758 -> GBL DAT_000b70a8).
- Leave (kind 0x2000) = "no attached target within" (GBL 0x16810 loop local_24: bVar4 stays true while
  FUN_00016280 == 0) - identical to base / the port's mtbl.c.
- Per-target tests: destroyed base 0x16030 -> GBL 0x16120 (exact), inspected base 0x160a0 -> GBL 0x16190 (exact),
  within base 0x16190 -> GBL 0x16280 (exact). GBL 0x16280: the table's unit index (DAT_0010cecc + table*0x26, -1 ->
  "not within"), nav target (type 1): 3D squared distance vs the nav radius (nav +0x10, < 1 -> 20000), unit / thing
  (types 2 / 4): 3D, <= 4e8 (200 m). Same as the port's tgt_within().
- Messages base 0x16400 -> GBL 0x164f0, star result base 0x164f0 -> GBL 0x165e0, activation base 0x16650 -> GBL 0x16740:
  all exact matches.
=> Nothing in GBL's mission-table code explains the BRAN / ZACK failures.

### 1.2 BRANSCN1 - root cause: the player's height (the port loses the lift under the start point)
- Data: player start BRANST01 NAVP = (-26045, 143850, 4900) (+4 is the height: 1438.5 m); leave target BRANLVE1 NAVP
  (19000, 145850, 500) radius 540 (x100 = 540 m, class 0x2130 includes 0x2000). 3D distance start -> LVE1 = 448 m, i.e.
  the player starts INSIDE the leave radius, so node 5 ("Fled", 'O', -> node 6 star-lose 0) must not pass at once.
- DOSBox (E_bran2_04 .. E_bran2_18, E_bran_12/20/30/45; command "MW2 BRANSCN1", stars from tools/mkstar): no failure at
  0.1 s. The HUD is normal at 4-12 s (player standing, 0 kph, inside the station hangar); enemies shoot the idle player
  (blue PPC bolt at 12 s, "Critical hit: LRM" / "Critical hit: engine" at 14 s), the player dies at ~16 s and only then
  "Mission failed." So the leave node does not fire in the original.
- Port trace (private copy, E_DBG print in tgt_within): at the first test the player is at y = 0 (u=(-26045 0 4900)
  vs LVE1 t=(19000 145850 500) r=54000) -> 1458 m away in 3D -> "left" -> star-lose.
  Why y = 0: the station (BRANWLD1 -> BLKX (2000, 150000, -11000) -> BRANARE1; BRANARE2) is a set of objects 1400-1600 m up.
  The start point stands on BRANARE2 OBJ 10 = collision type 5, carried by a path task (TSK 5 "10;oneshot,norotate,lift2",
  PTBL "lift2", plus TSK 4 "10;500,elevatr1,1" its sound) - a LIFT. The port's world part 560 (type 5, moving,
  box x[-27046,-24872] y[143941,145372] z[3574,5750]) is exactly under the start. The static floor around it (part 559,
  type 5) has a hole there. src/msim.c msim_ground_n() skips moving type-5 entries
  (`if (c->moving || !qt_ground(...)) continue;`), clist_build builds no faces / quadtree for moving type 5, so the ground
  at the start is 0. (The sweep then re-grounds the player at step 0 because last_px/last_pz start at 0 - tools/
  sweep_objectives.c "teleported" test - which is what drops him to 0 before the first MTBL step; the port's game would
  drop him through the hole by gravity just the same.)
- Engine (GBL = base, code identical): ground GBL FUN_0001c370 (= base FUN_0001c120, exact) walks the WHOLE collision
  list (DAT_000a5f6c +0x10 chain): type 5 -> FUN_0001f1e0 -> quadtree FUN_0001d900(obj+0x44, obj+0x1c geometry, x, y, z)
  - no "moving" test. The quadtree rectangles (built at load) are tested in x / z only (0x1d900: param_1[0..1], [4..5]),
  the height comes from the leaf polygon's plane through its WORLD vertex (geometry +0x18 block, vertex +0xc..+0x14;
  0x1c2e0 / 3Dfx 0x1000fff0). Those world vertices are re-transformed whenever the object's version (+0x48, bumped by every
  move, 3Dfx 0x1000fbe0 -> 0x10029c10) differs from the geometry's (+0x10): 3Dfx 0x1000d920 / 0x10029f00 -> 0x1000fa10,
  and 0x10029c80 -> 0x10029ab0 transforms every vertex / normal by the node matrix. So the ground of a moving type-5
  object is its CURRENT polygon height inside its load-time x/z footprint: the player stands on (and rides) the lift.
  (All GBL collision functions 0x1c000-0x1f800 match base 1:1 - unmatched entries there are split-point differences with
  similarity 1.00, see 2.1.)
- PROPOSED PORT CHANGE (src/msim.c):
  1. clist_build: build the faces and the quadtree for type 5 also when moving:
     `if ((!c->moving || c->type == 5) && (c->type == 2 || c->type == 5 || c->type == 7))` and in the quadtree loop
     `if (c->type != 5) continue;` (was `|| c->moving`).
  2. msim_ground_n: for a moving type-5 entry use the quadtree at the query point shifted back by the entry's x/z motion
     and add its height change (exact for the norotate paths GBL uses; the engine re-transforms the polygons):
     ```c
     if (c->type == 5) {
         const float *pn = NULL;
         if (!c->moving) { if (!qt_ground(t, c->root, x, z, &h, &pn)) continue; }
         else {
             if (!cobj_now(s, c, box, sph)) continue;
             float dx = ((box[0] + box[1]) - (c->box[0] + c->box[1])) * 0.5f, dz = ((box[4] + box[5]) - (c->box[4] + c->box[5])) * 0.5f;
             if (!qt_ground(t, c->root, x - dx, z - dz, &h, &pn)) continue;
             h += box[2] - c->box[2];
         }
         if (pn) ny = pn;
     }
     ```
     (tested in /tmp/E_port: the player now starts at y 143850 on the lift and node 5 no longer fires at 0.1 s.)
  3. tools/sweep_objectives.c: do not treat step 0 as a teleport (`if (step > 0 && (fabsf(...) > 100 ...))`) - or start
     last_px/last_pz at the start point.
- After 1+2+3 BRANSCN1 runs on to 5.3 s and then fails for HARNESS reasons only: the reach targets BRANGOU1-3 are navs
  inside the station's upper deck (y 156570-157187, 20-100 m radius) 113 m above the lower floor the teleport lands on
  (the station is entered by riding lift2); after 5 s the harness falls back, teleports 120 m from BRANENS1 into empty
  space (no floor: y 0) and that leaves BRANLVE1 in 3D -> star-lose. Not an engine difference.
- Port note (seen in the collision dump, not chased): BRANARE2 OBJ 8 (type 0, child of OBJ 0, PTBL "lift") gets a box at
  y 282299-282309 = about twice the station height - a carried CHILD part seems to receive the record placement twice.
  Worth a check in mech3d.c world3d_paths (carried children of a placed record).

### 1.3 ZACKSCN1 - harness, not engine
- Node 11 ("Airlock", 'M', reach, target "branst01", needs S0.6) - a copy/paste slip from BRAN: ZACK's mission tree has no
  BRANST01 record. Target attach GBL FUN_0004e5a0 (= base FUN_0004d640, exact match): called for every unit / nav /
  thing as it is created, with its record name and widened class; for each table (raw MTBL kept at DAT_00165460[t]) and
  node 0..47 still in load state 7, `strcmpi(name, raw node +0x70 (= L+0x16 target name)) == 0 && (node kind & class)`
  -> append the object code to node +0xd7 (count +0xd6, at most 16 = 0x10 in DOS / GBL; the 3Dfx build and the port
  allow 40). Nothing named BRANST01 is created in ZACK, so the node has 0 targets and the runtime (0x16810, loop over
  +0xd6) passes it as soon as node 6 succeeds - harmless and identical to the port (mtbl.c: no target -> passes).
  (Port note: the DOS cap is 16 targets per node, not 40 - only matters for groups / records with > 16 members.)
- DOSBox ("MW2 ZACKSCN1", stars from tools/mkstar; E_zack_05 / _12 / _22 / _37): no failure; the player stands still
  (0 kph, first frame inside the dropship), a carrier passes overhead at 22 s, still alive with a normal HUD and no message at 37 s.
- The 0.1 s failure is node 7 (leave "zackst01", 'O', radius 500 m) -> node 8 star-lose: the harness's protect step
  (node 6, "Defend Dropship 'Justice'") teleports the player 120 m from ZACKENS2, which stands on the moving "Jaguar
  Carrier" deck at y ~3999 m; there is no floor there, msim_ground returns 0, and the player at y 0 is 3.9 km from
  ZACKST01 in 3D. With a "no floor near the target's level -> stay" guard (E_NOFLOOR test in /tmp/E_port) ZACK runs to
  100 s and then leaves the same way at the moment the Jaguar Carrier (structure the harness had put him next to / on) is
  destroyed (INFERRED: the player loses his floor). All harness.
- Harness proposal: only accept teleport points whose ground is within ~10 m of the target's level
  (`place_y_ref - msim_ground(...) <= 1000`), else keep the player where he is (space / station maps BRAN, ZACK).

### 1.4 ANDYSCN1 - harness, not engine
- Table 0: node 1 'M' protect ANDYENS1 ("Defend General") and node 4 'O' destroy ANDYENS1 ("destroy ens1") target the same
  group; node 9 'M' visit; the win path is node 11 (S0.9, 10 s) -> node 13 reach ANDYST01 "Return to dropship
  Immediately" (shown by toggle node 28) -> node 18 star-win 0. The sweep pursued node 4 and killed ANDYENS1 ("Draconis
  Base (Unknown) destroyed" at 110 s) which fails the main protect -> "Mission failed".
- With classify() marking a destroy node 'bad' when a main ('M') protect node of table 0 has the same target, the sweep
  passes: "RESULT ANDYSCN1 Mission successful 82.2 s" (node 13 at 82.2 s -> node 18 star-win). Code (tools/
  sweep_objectives.c classify(), after the bad[i] = ... line):
  ```c
  if ((n->kind == MTBL_K_DESTROY1 || n->kind == MTBL_K_DESTROY2) && n->objective != 'M' && n->target[0])
      for (j = 0; j < t0->node_count; j++)
          if (t0->nodes[j].kind == MTBL_K_PROTECT && t0->nodes[j].objective == 'M' &&
              strcasecmp(t0->nodes[j].target, n->target) == 0) bad[i] = 1;
  ```

## 3. The 9 PRJ missions that do not pass, and DUMBTURT

### 3.1 CALI COLL DOBE HUSK PERS SHAR SIAM TABB TERR = 8-player network arenas, not single-player missions
- Data (MW2.PRJ, decoded with tools/prj.py): every one has 8 MTBL tables, a STAR chunk of 8 entries {team k, alliance
  (0 for star 0, 1 for the rest)} named "Star", INCLs `en00star(-2) .. en07star(-2)` (id -2 = disk-only files, as
  USERSTAR in the campaign) and the start / leave navs `xxxxST01..ST71`, `xxxxlv01..lv72`; NO UserStar, NO player GPS
  ("spawn check: 0 player GPS"), no BRF1/BRF2/DBFF/DBFS briefing records, no MUSI chunk, time limit -1. Table 0 (and every
  table k) = 7 x 'destroy enk star' ("Destroy Enemy Mech"), 'O' order "Destroy all Enemy Mechs" after all of them, and an
  'M' order "Null" with delay -1 that never finishes (e.g. TABBSCN1 node 12) - a free-for-all that never "succeeds" on
  its own. The names are a breed series (calico, dobermann, collie, husky, persian, shar-pei, siamese, tabby, terrier).
- Reachability: NOT in GBL's single-player shell. MW2SHELL.EXE (GBL) holds only these scene names: campaign
  bass jess clai newc jeeh tim_ andy fost hoff zack sean fran asah pete cath sach bran gui1-4 guin (strings at
  0x7731a..0x77560, the table 0x8017c the brief names) + petescn1 / silv jenn maro aqua gold silv (0x76b85, 0x7709b..),
  "pinkscn1" (0x775ad) and the Instant Action list jack ched edam prov goud colb goat whiz swis (0x776fe..0x77746 -
  base's "ricoscn1" is gone). No cali / dobe / coll / husk / pers / shar / siam / tabb / terr anywhere in the shell.
  The sim keeps the base network code (GBL 0x45830 "Opponent %i exited the game.", 0x452e0 "Opponent mech %i destroyed.",
  0x323b5 " 1 Msg to starmates" / " 2 Msg to enemies", chat entry toggle = input action 0x42, new in GBL, see 2.6), so
  they are for a network launcher that is not part of this install (no NetMech program in the DOS rip or on the disc's
  DOS side). INFERRED: they are NetMech arenas; which EN0x file is the local player is decided by that launcher (not traced).
- Why the sweep fails / runs out:
  - CALI (0.1 s): node 8 'M' reach calinav0 ("Hilltop") then node 9 'N' leave calinav0 -> node 12 force-fail node 11
    ("Defend Hilltop For 5 Minutes"): the harness clamps the player to 0.9 x the radius in 2D while the test is 3D.
    Harness.
  - DOBE (0.1 s): a DATA slip: table 7 node 12 star-lose has other_table 0 (tables 0-6 each point at themselves:
    t0 n12 -> 0, t1 -> 1 ... t6 -> 6, t7 -> 0). With no EN07STAR unit the table's unit index is -1, the within test
    (GBL 0x16280: `if (-1 < unit)` else 0) says "not within", both leave nodes pass at once and star 0 fails. The GBL
    engine does the same (0x170a0 runs every table whether or not it has units - exact base match) - so in the original
    a DOBE game with fewer than 8 players fails star 0 at once too (INFERRED from code; not measured: needs a net game).
  - COLL HUSK PERS SHAR SIAM TABB TERR: no outcome by design (the never-ending 'M' "Null" order).
- Port: exclude scenes without a UserStar include from the single-player sweep (or run them with an explicit local
  star); nothing to implement for the single-player GBL port.

### 3.2 mechview "skeleton DUMBTURT failed" = invisible turrets the port drops
- DUMBTURT (BWD 1966): DTBL per-set 5, 17 OBJs (5 + 3 x 4), EVERY node's model is POLY 1969 "DUMMY"; THNG 1 / 2, GP
  0x30000 (class 3 = turret), POFO (3, 5) and (4, 4) = two weapon mounts, EYEO 2, MGDF 0x38 "dumbturt". An armed unit
  with no visible geometry.
- Used by BRANUST1-4 (GPS skeleton "dumbturt", loadout "nva00std"), ZACKUST1-4, ZACKENT1-4, TIM_ENT1 (2 GPS) - all
  included by their scenes (BRANSCN1 INCL list: branust1..4).
- Port: src/mech3d.c mech3d_load_repr() skips DUMMY models and ends with `rc = out->part_count > 0 ? 0 : -1;`, so the
  load fails, and world3d_actors (mech3d.c ~line 639: `if (!skel || mech3d_load(a, skel, 0, &piece) != 0) continue;`)
  silently drops these units: BRAN spawns 13 actors (no BRANUST*), ZACK 7. In DOSBox the idle player in BRAN is shot
  from inside the station within ~12 s (E_bran2_12: a blue PPC-class bolt) - consistent with hidden guns (INFERRED: which
  unit fired was not identified).
- PROPOSED: accept a skeleton with nodes but no visible parts: `rc = (out->part_count > 0 || out->node_count > 0) ? 0 : -1;`
  (keeps skel / POFO / EYEO), and make sure msim copes with a unit with part_count 0 (hit tests: no geometry -> it cannot
  be hit by projectiles; ASSUMED until traced: what the engine uses for a DUMMY model's collision - DUMMY POLY 1969 is
  122 bytes; if it has a bounding box the unit is hittable). mechview verify: report "all-DUMMY skeleton" instead of a
  failure.

## 2. The unmatched GBL sim functions

### 2.0 Classification (all 567; table: /home/claude/gbl/spec/E_sim_unmatched.tsv)
Method: for every GBL function not in mw2_base_to_gbl.tsv, the base candidates are the UNMATCHED base functions between
the base addresses of its nearest matched neighbours (the map is 95 % in order); similarity = difflib ratio of the
token streams of the two decompiles with FUN_/DAT_/LAB_ names, numbers and register-variable names masked
(/tmp/E_w/classify.py). Result:

| region | >= 0.95 same code | 0.80-0.95 edited copy | 0.50-0.80 reworked | < 0.50 new / mis-split | total |
|---|---|---|---|---|---|
| game code < 0x62000 | 261 | 31 | 21 | 38 | 351 |
| C runtime / AIL / VFX >= 0x62000 | 65 | 14 | 12 | 125 | 216 |

- ">= 0.95" are the same code: Ghidra split them at a different entry (e.g. GBL 0x1f1e0 = base 0x1ef90 byte for byte,
  the map took 0x1efb2 -> 0x1f202 as a "prefix" row) or only struct offsets moved (the unit struct grew by 4 around
  +0x16a; e.g. 0x20320, 0x22343, 0x2d590: +0x166 -> +0x16a, +0x17e -> +0x182). Nothing to port.
- The library region's "new" entries are odd-address mis-splits inside Watcom clib / AIL code (0x7a057 COMSPEC, ...),
  the library simply grew (code size 0x77cde -> 0x7edec). Not gameplay.
- Gameplay-relevant changes found (by priority) are 2.1 - 2.5 below; the rest of the game-region list is HUD / MFD
  (0x26d05, 0x330d5, 0x332b0, 0x33590 - GBL's "MFD_CYCLE" multi-function display, labels "%MASC", "ComHeat", "dH/dT",
  "Jets" at 0xa0d58..), network / chat (0x323b5, 0x452e0, 0x45830, 0x45170-0x461a0, 0x49a10 "Online Connect",
  0x46a00), MW2.INI (0x4ab60-0x4ad60), cheats (0x46530: XOR 0x0a strings, new codes "dorcs", "kaboom", "palex",
  "gonzales", "crillion", "drone", "clark", "merlock"), launcher check (0x44dd0 "This program must be launched from
  GBL.EXE").
- NOT changed (checked): MTBL runtime / conditions / messages (1.1), collision + ground (0x1c000-0x1f800), the BWD
  chunk tag table (GBL 0xafd94 = base 0x9ed20, the same 70 tags DTBL..SUPS, no new chunk types), the base manoeuvre
  tables (GBL 0xa6884 = base 0x963f4 = port MAN_TABLE; jumper table GBL 0xa69a4 = base 0x96514), MTBL node size 0xf7 /
  table 0x2e8a, the mission-result loop 0x170a0.

### 2.1 Weapon table: 35 weapons, 0x68-byte entries with callbacks (HIGH - GBL MEKs use ids 3101-3402)
- GBL table at VA 0xafecc (file 0x90ecc), 35 entries x 0x68 (base: 0x9ee58, 31 x 0x58). Each GBL entry = 16 NEW bytes
  {+0x00 fn0: per-frame callback (0 = none), +0x04 fn1: fire callback (default 0x53820), +0x08 flags, +0x0c int} and
  then the base 0x58-byte record unchanged in layout (+0x10 type, +0x14 visual, +0x18/+0x1c muzzle / impact effect
  index, +0x20 repeat, +0x24 f14 (must be != 0 for the default fire path), +0x28 guided, +0x2c per volley, +0x30 ammo
  per ton, +0x34 sound, +0x38 drop, +0x3c speed, +0x40 damage (int), +0x44 heat 16.16, +0x48 target heat 16.16, +0x4c
  f3c, +0x50 range, +0x54 refire, +0x58 interval, +0x5c life, +0x60 name[8]). Used by the fire routine GBL 0x53200
  (base 0x52080): every frame `if (entry->fn0) fn0(mech)`; firing `n = entry->fn1(mech, entry, slot)` = projectiles
  spent (0 stops the volley). Flags (entry +0x08, copied to the projectile slot +4): 1 = volley sound on the first
  projectile, 2 = sound on every projectile (priority 0x432 instead of 0x32), 4 = selectable in weapon groups
  (0x53af0 / 0x53d00 test `& 4`), 0x10 (Torpedo: eased guidance, 0x51cd0), 0x20 = animate textures +0x0c..+0x0c+7 while
  firing (Flamer: 118-125, 0x53823 FUN_00038880/38830), 0x40 = the projectile NOT moved 2 x its pool object's radius ahead
  of the mount (0x54730; every other weapon is - J_sim_impl 2.3), 0x80 = NARC
  beacon (on a hit the target's +0x20 struct +0x112 |= 1, 0x516b0).
- Decoded entries (dump: /tmp/E_w/dumpw3.py; the port's struct order: type, repeat, per_volley, speed, damage, heat,
  refire, interval, life, target_heat, ammo/t, range, f3c, drop, sound, visual):

| # | id | name | changes vs base DOS table / new |
|---|---|---|---|
| 3 | 300 | LRM5 | ammo per ton 12 -> 24 |
| 12-15 | 1200-1500 | LB 2/5/10/20-X | damage 3/7/15/30 -> 4/10/20/40 |
| 16-19 | 1600-1900 | Ultra AC/2/5/10/20 | damage 3/7/15/30 -> 4/10/25/50 |
| 20 | 2000 | FLAMER | REDONE: type 7 -> 8 (new kind), visual 10, effects 26/27, repeat 1, 10 per volley, no ammo, sound 456, speed 20, damage 0, heat 3.5, target heat 5.0, range 5000 (50 m), refire 182, interval 4, life 364, flags 0x65, x0c 118 |
| 28 | 2800 | NARC | type 3 -> 12 (new kind), visual 8 -> -1, ammo -1 -> 6 per ton, flags 0x85 (NARC beacon) |
| 29 | 2900 | AMS | fn0 0x54940 / fn1 0x54aa0 (a real interceptor, 2.3), flags 0x0a, f14 1 -> 0, 24 per volley, ammo 24 -> 20, sound 1 -> 455, damage 1 -> 0, refire 11 -> 22, interval 5 -> 11, life 273 -> 182 |
| 30 | 3000 | NUKE | type 22 -> 4 (guided missile), sound 189 -> 461, speed 0 -> 100, damage 100 -> 1000, target heat 0 -> 50, refire 36400 -> 182 |
| 31 | 3100 | Inf SRM | NEW: type 3, visual 10, effects 26/27, 2 per volley, 50 per ton, sound 258, speed 70, damage 0, heat 1.34, range 49686, refire 910, interval 60, life 709 |
| 32 | 3200 | Torpedo | NEW: type 9 (new kind), visual 34, effects 32/33, guided, 2 per volley, 50 per ton, sound 466, speed 10, damage 50, heat 1.34, range 49686, refire 910, interval 60, life 2730, flags 0x15 |
| 33 | 3300 | InfSSRM | NEW: as Inf SRM but guided (Streak) |
| 34 | 3400 | ArrowIV | NEW: type 10 (new kind), visual 35, no muzzle effects, guided, 1 per volley, 5 per ton, sound 464, speed 100, damage 7, heat 10, range 200200, refire 3640, interval 60, life 2002 |

  All other entries are byte-identical to base. The MEK ids 3101 / 3201 3202 / 3301 3302 / 3401 3402 = weapon index
  31-34, instance 1 / 2 (DRW00USR EXE00WTR HLB00HOF LBK01WTR NGA00USR: *WTR = water variants with torpedoes).
- Missile-like kinds (homing / AMS-interceptable / "last missile" camera): 3, 4, 9, 10, 12 (0x53823, 0x54943, 0x54bb0).
- Effects (explosion table GBL 0xb0d04, 36 x 0x2c; base 0x9f900, 32 x 0x1c): GBL adds per effect {+0x1c 1|2, +0x20
  damage per tick 16.16 (256 default), +0x24 heat per tick, +0x28 flags (1 follows its object, 2 splash)}. Changed /
  new: 10 "burning" (was empty: 910 ticks, sound 467, damaging, 128 / 1024 per tick, flags 3), 22 nuke 5460 -> 1820
  ticks (8192 / 1024 per tick, splash), 32 / 33 (36-tick flashes: the Torpedo's muzzle / impact), 34 (254 ticks, sound
  457, 1024 per tick: torpedo blast), 35 (254 ticks, sound 465, 8192 per tick, splash: Arrow IV).
  Splash (0x52670 -> 0x52900): for effect 0x16 and 0x23 (or the TOGGLE_SPLASH_DAMAGE cheat), radius = age x 0x1c
  (nuke) / age x 0x18 (Arrow IV) cm, every unit whose octagonal distance < radius + its radius (+0xec) takes
  +0x20 x dt to each of the 8 locations (head a quarter; a unit in state 4 double) and +0x24 x dt heat. Structures and
  things in range take effect damage for every "damaging" effect (+0x18 != 0: 0x52a70 -> 0x52cd0, 0x2c180).
- PROPOSED PORT CHANGES: src/wtable.h `sim_weapons[35]` (append the 4 entries; apply the damage / ammo / Flamer / NARC /
  AMS / NUKE edits for the GBL build only - base stays as is), src/mek.c `mek_weapons[]` +4 names (the shell-side
  figures - name, crits, tons, ranges - come from GBL MW2SHELL's table: shell agent), msim projectile kinds 8 / 9 / 10 /
  12, effect slots 32 -> 36 (msim.h fx_radius[32], fx.c `slot < 32`), the per-effect damage / heat / splash fields,
  and the callback hooks (fn0 per frame, fn1 fire) at least for AMS.
- UNRESOLVED here: how kinds 8 (Flamer), 9 (Torpedo: underwater only?), 10, 12 move / hit inside the projectile
  update (0x516b0 / 0x52040 call trees - only the shared parts were read) and how Inferno (damage 0, target heat 0)
  heats a mech (INFERRED: through the "burning" effect 10 attached to the target, flags 1 = follows it).

### 2.2 Max 14 weapons per mech, 255 projectiles (MEDIUM)
- Weapon cycling / groups (0x53af0, 0x53bb0, 0x53c50, 0x53d00, 0x548d0): `% 0xe`, `< 0xe` (base `% 0xa`): a mech
  carries up to 14 weapon slots (base 10). Weapon slot stride stays 0x68 (mech +0x54 array, count +0xac, current +0xb0).
- Projectile slots (DAT_001657a8): 255 x 0x58 (base DAT_00153940, 175 x 0x50): slot +0 = weapon kind (new), +4 flags,
  the rest shifted by 8 (0x516b0 diff).
- Port: check lance / mek limits (MEK_MAX_ITEMS 64 is fine) and the msim per-unit weapon arrays (10 -> 14).

### 2.3 AMS and NARC are real systems in GBL (HIGH)
- AMS (weapon 29): fn0 GBL 0x54940 (each frame, slot state 1 = armed): for every live projectile of kind 3/4/9/10/12
  whose owner is on another side (0x2b5c0 alliance) within octagonal 500 m (< 50001 cm) and closing (dot(v, d) > 0):
  start the volley (state 2, rounds = per_volley 24, 0x2e750 fire start). fn1 0x54aa0 (fire): uses rand(5) + 1 rounds
  (<= what is left of the volley), out of ammo -> slot state -1 and message (10, 0) = "Anti-missle suite" + "disengaged"
  (status-message table GBL 0xa83b0, 12-byte {sound, text*}; suffixes 0xa838c: 0 / 2 "disengaged", 1 "engaged"; message
  9 is "NARC missle probe"); then 0x54bb0 for every incoming enemy missile within 500 m: its intercept distance (slot +0x54, -1 = not
  rolled): under 25 m -> 0 (never); else if closing: rand(100) > 75 -> 0, else 2500 (+ rand x 100 when rand(100)
  returned >= 0x64 high bits - INFERRED from the decompile's 64-bit compare, effectively 25 m); when 0 < intercept and
  distance <= intercept -> the missile is flagged destroyed (slot +0x3d |= 0x80). So ~76 % of the enemy missiles
  that reach 25 m of an AMS mech are shot down.
- Toggle: input action 0xa0 "TOGGLE_AMS" (name at the action-name table; not bound in the shipped GAMEKEY.MAP) ->
  0x548d0: every AMS slot state 4 (off) <-> 1 (armed), message 10 + "engaged" (1) / "disengaged" (2).
- NARC (weapon 28, kind 12, flag 0x80): a hit sets target +0x112 bit 0 (0x516b0); the lock-on update GBL 0x54160
  (base 0x52ea0) gives an INSTANT lock (+0x44 |= 0x80c0, timer +0x8c = 0) on such a target when the target slot has
  +0xdd & 2 (guided launcher). Base NARC had no effect.
- PROPOSED: msim - add the AMS interceptor (per-frame scan + fire callback as above) and the NARC mark + instant lock;
  input action TOGGLE_AMS.

### 2.4 AI: per-unit engage range and four boss AI profiles (HIGH for GUI1-4)
- GPS skill byte (payload +8, unit +0x12c) 100..103 selects an AI profile (GBL 0x217b0, base 0x21920 had none): table
  0xa62dc (count 4 at 0xa633c), 0x18-byte entries {+0 manoeuvre-table record, +4 skill (-> unit +0x154, +1 after),
  +8 pilot level (-> +0x155), +0xc engage range cm (-> +0x1a8), +0x10 -1, +0x14 group-fire flag}. Values 7..99 would
  read a NULL entry (no data uses them). Used only by GUI1ENS1 (100, summoner), GUI2ENS1 (101, timbrwlf), GUI3ENS1
  (102, executnr), GUI4ENS1 (103, kodiak) - the GUI1-4 trial bosses.

| skill | record | rows (manoeuvre, n, options...) | skill | level | range | group fire |
|---|---|---|---|---|---|---|
| 100 | 0xa6e80 -> 0xa6a8e | = the jumper table (0xa69a4) | 1 | 1 | 0 -> 150 m | 0 |
| 101 | 0xa6e88 -> 0xa6b78 | 0:{0,0,0,0,0,8,8} 1:{0,0,0,0,0,1} 2:{0,0,0,0,0,7,1} 3:{0} 4:{0,0,8,5} 5:{0,0,7,8} 6:{6} 7:{0,0,8} 8:{0,0,0,8,0} 9:{0,0,8,7} 10:{0,0,7} 11:{0} | 1 | 1 | 250 m | 0 |
| 102 | 0xa6e90 -> 0xa6c62 | base table with rows 2:{7,4} 3:{7} 7:{0,1} | 1 | 1 | 50 m | 1 |
| 103 | 0xa6e98 -> 0xa6d4c | as 101 but row 0 {0,0,0,0,0,2,8} | 1 | 1 | 200 m | 0 |

  (row = manoeuvre, option count, then the options; record = {u16 rows 13, u16 7 (first-pick range), ptr}.)
- 0x2c970 (base 0x2c5f0): the manoeuvre record is now per unit (+0x1ac): profile's, else &0xa6e38[class - 1] (class 1
  mech = base table, classes 2-8 = one row {0, 1, 0}), and 0xa6e78 (jumper table) when mech +0xe8 == 1; engage range
  +0x1a8 = 15000 when 0. (Base built the same records at run time in 0xfdf10.)
- Manoeuvre 0 (GBL 0x2d4e0, base 0x2d200 = 3Dfx 0x1001f950, port ai.c line 271 `case 0`): base R = 15000 and forced
  manoeuvre 10 under 45 m; GBL R = +0x1a8 and `if (R < 15001) { if (d <= 4499) man = 10; } else if (d < R) man = 10;`
  - a long-range profile backs away whenever the target is inside its range.
- Fire decision GBL 0x2ec90 (base 0x2e980): unchanged rule (in range, heat + weapon heat < 65, rand(refire / 91 + 1)
  == 0) plus, for a profile with the group-fire flag (+0x15a, i.e. 102), the heat sum of every ready weapon (a slip:
  the loop reads the CURRENT slot +0xb0 each time) and all weapons put into group 0 at spawn (0x2ee80 / 0x53d00).
- PROPOSED (src/ai.c / msim): GPS skill >= 100 -> profile {table, skill 2, level 1, range, group flag}; per-unit
  MAN_TABLE pointer; manoeuvre 0 uses the unit's range as above. (Also note for the base port: the DOS records hold
  first-pick range 7 where the 3Dfx chooser uses rand(6) - not GBL-specific, not checked further.)

### 2.5 Input actions (LOW-MEDIUM)
- Action-name table: GBL inserts "TOGGLE_AMS" (after ENHANCED_VISION) and "CHAT_MODE" (after DISPLAY_OBJECTIVES).
  Dispatcher GBL 0x46e00 (base 0x46060): new case 0x42 toggles DAT_000a7174 (chat entry, used by 0x46a00 / 0x323b5)
  so the base ids 0x42..0x9e are +1 in GBL (e.g. time compression 0x89 -> 0x8a, groups 0x8e-0x90 -> 0x8f-0x91); new
  case 0xa0 = TOGGLE_AMS (0x548d0). The port maps actions by name (GAMEKEY.MAP), so only the two new names matter.

### 2.5b Non-walking movers: class 2 flies at a fixed height (agent U, 2026-10-26)
- Class table 0xa6518 (9 x 0x20: class, create, init, ?, control, player-only x2, ?): classes 1 2 4 5 6 8 all use the
  mech controller (create 0x28a00, init 0x26a50, move 0x26d00, control 0x27b00); 3 turret (0x2f300..), 7 door
  (0x33c90..). No separate flight model: speed = the MEK walk speed, turn = the mech rule, yaw only (no pitch / roll
  per frame, no banking), the same AI (no class test in it).
- New in GBL (base DOS 0x26995 and the 3D editions have none): the move ends with `cmp $2,(class)` -> y = controller
  +0xd0 (0x278ac), the MGEO int[0] that MGDF loads (0x500a6 -> 0x4ca90). For class 2 that value is an absolute height:
  AIRCRF1D 3950 m (ZACK's Jaguar fighters, the JumpShip deck ~3904 m), AIRCRFTD 95 m (BRON / TIM helicopters), SUBA /
  SUBB / SUBD 185 / 194 / 250 m (PETE's submarines), VEHICLEB 1.75 m (MARO hover limo), VEHICLEH 0.55 m (TAUP). The unit
  is level there every step, "grounded" (no fall, no jump lift, vy 0); dead, it stays where it was (state 4 returns).
- Class 5 / 8 (AIRCRFTE / X / Y gunships, DROPA) have no such rule: they move as walkers; AIRCRFTE's model itself is
  built 100 m above its root.
- DOSBox (original MW2.EXE, "MW2 ZACKSCN1", invulnerable): U_zack_5..150 - the fighters fly in at the deck's height from
  ~30 s, shoot, and the friendly turrets bring them down ("Enemy vehicle destroyed" from ~90 s). BRON (U_bron_3..90):
  helicopters / gunships over the player.
- Port: msim fly_y (msim_init, msim_step: y = fly_y, ground = y, no jets / gravity). ASSUMED: the model's root drawn at
  that height (the port's walker mapping puts the feet at engine y - MGEO int[0]; for class 2 that would be y 0, which
  the ZACK capture rules out).
- ZACKARE3: a DUMMY-model object that a GT chunk makes a class-2 thing (hp 0: indestructible), on a looping 5 s path
  over the deck - the destroy target of the fighters (ZACKENS1, table 1 node 2) and of the Jaguar turrets (ZACKENT1-4).
  The port had dropped every DUMMY object, so the node had no target and passed at once (the fighters idled 1.4 km out).
  Now kept as an invisible point (no faces, no collision - ASSUMED for the DUMMY model's one-vertex face); the only
  such object in the GBL data.

### 2.5c Wrecks in a unit's way; the AI's obstacle scan (agent W, 2026-10-26)
- MAROSCN1's hover limousine circled the wreck of the Falcon Nova killed on its road at (21620, -2299) for good (the
  sweep stalled at 664 s). Three DOS rules the port lacked:
- Running into a wreck blows it apart (0x1fef0, the move test of every mech-controller unit in state 2 or 4, every frame
  -> 0x20320, the unit contacts): a unit (not yet blown apart: physics +0x110 bit 0x100) whose centre is nearer the
  mover's new centre than the two radii (+0xec; octagonal (4 max + mid + min) / 4) is a contact unless it is a wreck
  (state +0xa4 == 4): then 0x20d60 throws every node of its object tree off (0x2b9c0 -> 0x2b880 with 999: nodes with an
  OBJL location fly as blown-off parts, 128-slot pool; DUMMY, location 0 or pool full: just leave the collision list),
  sets +0x110 |= 0x100, plays sound 0xb5 at it, and the move goes on as if nothing were there. (0x274ec / 0x27762: a
  moving wreck that hits something blows itself apart - the port's wrecks never move: not modelled.) DOSBox
  /home/claude/gbl/ref/W_m9_*: the "palex" cheat (0x46530, Ctrl+Alt+Shift + "palex" -> 0x25680 on the target: a death
  with nothing blown off) on the Nova standing on the limousine's road; at 123-125 s the limousine reaches it, the wreck
  flies apart and the limousine drives on (montage /home/claude/agents/W/w/m9_scatter.png). Port: msim scatter_wrecks /
  scatter_wreck (msim.scattered[]); a blown-apart wreck is out of the AI probes, the shot test and the drawing.
- The scan's hold between checks (0x2de80, called by the AI movement 0x2d4e0 / 0x2d590 / 0x2d6b0 and the autopilot
  0x24fc0, which steer to the goal only when it returns 0): between checks (now < +0x18a) it returns the remembered
  obstacle (+0x186 != 0, set and cleared with the side +0x18e), so while a side is set the inputs written last are held -
  the avoidance turn or, after a check that found the way clear, that frame's goal steering - for the 0x5b-tick re-check.
  The port's AI steered to the goal every frame between checks (the player's autopilot already held).
- The scan's height: 0x2e230 casts the probes at unit +0x54. A GBL class 2 unit's +0x54 is its MGEO int[0] itself
  (0x278ac -> 0x278d0): the limousine probes at 1.75 m, not 1.75 + 1.75.
- Also: the AI probe now leaves out blown-off parts (they leave the collision list, 0x2c280), as the shot test did.

### 2.6 Not found / checked negative
- No sim code specific to Elementals or Inner Sphere mechs: no new unit classes, skeleton names or class tests in the
  changed functions; the IS mechs / Elementals are data (MEK / BWD skeletons; the 166 GBL MEKs already parse in the port,
  only the 4 new weapon ids are unknown). (Searched: new strings GBL vs base, constant diffs of all 0.6-0.95 pairs.)
- No new MTBL node kinds (the runtime 0x16810 handles exactly the base set; the GBL scenes' kinds all decode with
  mtbl.c), no new BWD chunk tags, MTBL node / table sizes unchanged.
- CD audio (0x43300-0x44710), sound priority (0x41690), PRJ / file reader (0x35890): reworked I/O, not gameplay.

## Test changes made in the private copy (/tmp/E_port, NOT in /home/claude/mw2port)
- src/msim.c: moving type-5 entries get faces + quadtree; msim_ground_n uses them at their current offset (1.2).
- tools/sweep_objectives.c: step 0 is not a teleport (1.2); classify() marks destroy-vs-main-protect nodes bad (1.4);
  E_NOFLOOR guard (1.3, env-gated); E_DBG / E_DBG2 / E_EVERY debug prints.
- Result with these: GBL scenes 21 / 23 pass (ANDY now passes; BRAN and ZACK fail for harness reasons, 1.2 / 1.3);
  the 72 PRJ missions unchanged (63 pass, CALI / DOBE 0.1 s, 7 no outcome - 3.1). Logs: /tmp/E_w/sweep_gbl.txt,
  /tmp/E_w/sweep_all.txt.

## Unresolved
1. BRAN / ZACK end-to-end: the harness cannot ride lift2 into the station or stay on the Jaguar Carrier deck; a
   harness that walks on the station floors (or teleports only onto static floors at the target level) is needed
   before these two can pass. ZACK's "falls when the carrier is destroyed" is INFERRED from the 100.3 s leave.
2. Whether a moving type-5 object's world vertices are refreshed every frame or only when it is drawn at LOD 0
   (3Dfx 0x10029f00 lazily, 0x10029c80 eagerly; the DOS callers of the eager path were not identified). For a lift
   under the player it is drawn every frame, so the proposed "current offset" ground is right in practice.
3. DUMBTURT units: what the engine uses to hit / damage a unit whose every part is the DUMMY POLY (1 point); which unit
   fired at the idle player in DOSBox BRAN (E_bran2_12) was not identified.
4. New weapon kinds 8 / 9 / 10 / 12 inside the projectile update and hit code (0x516b0 / 0x52040 trees) - only the
   shared parts were traced; Inferno's / the Flamer's heat on a mech target (INFERRED: effect 10 attached to the target).
   Torpedo use above water not checked. DOSBox measurement of each would need a mission with those loadouts (e.g. a
   *WTR mech) - not done.
5. AMS: the 64-bit compare in 0x54bb0 (intercept distance 2500 vs 2500 + rand x 100) - read as 25 m.
6. NetMech arenas: which EN0x star is the local player and how a net game launches them (no launcher in this install).
7. GUI1-4 profile tables: the meaning of record field +2 (7) in the DOS chooser vs the 3Dfx rand(6) (base question).

## The missile / PPC dodge (DOS 0x2e750) - DOSBox-verified
Called from the weapon fire (0x53458, 0x54089, 0x54a71) for every shot. First test: the shooter's +0x19c bit 2, which
0x2c970 sets for pilot level <= 2 - and 0x2c970 runs only for computer-run units (0x217b0: +0x10 == 2; the player's unit
is made with +0x10 = 0, 0x24d58). Probe (a patched copy of the original MW2.EXE, BASSSCN1, the player firing with `;`):
a divide fault placed on "player and bit clear" ends the game on the first volley; placed on "player and bit set" the
game runs on. So in DOS the player's shots never make an enemy dodge; only the AI's guided / PPC shots do. The port's
base engine (3D DLL 0x10020ac0) warns on the player's shots as a level 1 pilot; GBL now follows DOS
(`ai_missile_warning_gbl`, msim.c `missile_warning`). The dodge itself (manoeuvre 11, 0x2ced0 case 0xb): 0x16c ticks;
+0x17e = ((q + 1) % 4) * 4 with q = the shooter's quadrant off the target's heading - 0 -> the left jet (+0x1e), else the
forward jet (+0x20). The side probe keeps the port's 50 m sidestep check (ASSUMED equivalent of 0x2e230 / 0x1c640).
