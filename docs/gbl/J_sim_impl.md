# J - GBL simulation: implementation report

Agent J. Copy: /home/claude/agents/J/mw2port (built with `make && make mw2 glview missionsim sweep_objectives gbl`: clean,
no new warnings - the only warnings are the existing ones in tools/gblshell.c / tools/mw2shell.c). Spec: docs/gbl/E_sim.md.
Sources read: /home/claude/gbl/re/mw2gbl_decomp.c + mw2gbl.elf (GBL sim), shellgbl.elf (GBL shell), base DOS
/home/claude/handoff/mw2-gbl-handoff/re/mw2_decomp.c + mw2.elf.

## 0. The game switch (core)

- `int sim_game_gbl(void)` in src/mek.c (declared in src/mek.h; mek.c is linked into every program that has the sim or
  the shell, so no Makefile change): true when `getenv("MW2_GAME")` is "gbl" (case-insensitive). Not cached (the shell and
  the sim share one process in `gbl --sim`; a cached value could be read before the shell sets it).
- **One-line core patch for the shell (tools/gblshell.c, main(), first line, before anything reads MEKs):**
  ```c
  setenv("MW2_GAME", "gbl", 1);   /* the simulation's GBL rules (src/mek.c sim_game_gbl); inherited by "gbl --sim" */
  ```
  (setting it at the start, not only in gbl_launch, also gives the shell's Mech Lab the GBL weapon table through
  mek_weapon_for / mek_weapon_table - see 2.6).
- tools/sweep_objectives.c sets it itself when GBL.EXE stands beside the PRJ (and MW2_GAME is unset); it prints
  "game: Ghost Bear's Legacy (MW2_GAME=gbl)".
- Base game bit-for-bit check: with MW2_GAME unset/"mw2", the final binary and the task-1-only binary give IDENTICAL sweep
  output on the GBL data (all 72 PRJ missions: 1796 lines; the 23 loose scenes: 511 lines; `diff` empty;
  /tmp/J_w/idp_t1.txt vs idp_t3.txt, id_t1.txt vs id_t3.txt, NOFLOOR forced to the task-1 value). So every task 2/3 change
  is inert in the base game. (The base game's own data is not here, so base behaviour on base data is unverified.)

## 1. BRANSCN1 lift, sweep fixes, DUMBTURT, network arenas

### 1.1 Moving type-5 ground (engine behaviour, BOTH games)
- Engine: GBL FUN_0001c370 = base DOS FUN_0001c120 (exact match) -> 0x1f1e0 -> quadtree 0x1d900: walks the whole collision
  list, no "moving" test; heights from the polygons' current world vertices (3Dfx 0x1000fa10 / 0x10029c80).
- src/msim.c `clist_build`: faces + quadtree are built for moving type-5 entries too (`(!c->moving || c->type == 5)`, the
  quadtree loop `if (c->type != 5) continue;`).
- src/msim.c `msim_ground_n`: a moving type-5 entry uses its quadtree at the query point moved back by the entry's x/z
  travel (`cobj_now` box centre vs the build-time box) plus its height change (E's 1.2 proposal).
- src/msim.c `cobj_seg` case 5: `!c->moving` added so the moving lift's quadtree is used only by the ground test (segment /
  movement tests unchanged from before - ASSUMED adequate; the engine would also collide with it).
- Result: BRANSCN1's player now starts on the lift at y 143941 (was dropped to 0), node 5 no longer fires at 0.1 s.
- NEW FINDING (not fixed, path code): the lift's path "lift2" (BRANARE2 PTBL) is (-24000,142951,3500) 1820 ticks,
  (-24000,142951,3500) 910 ticks, (-24000,**-142751**,3500): after 10 s the port runs the lift 2.86 km DOWN in 5 s and the
  player falls off (ground 0 under him from ~11 s), leaves BRANLVE1 in 3D and star-lose fires at 26.8 s. In DOSBox
  (E_bran2_10 vs E_bran2_14: identical view at 10 s and 14 s) the player has NOT moved by 14 s, so in the original the
  descent had not started (or the lift does not move the player). Either the path task does not start at load in the
  original (a "oneshot" path started by an event?), or path ticks are not 182 Hz ticks. Not traced (mech3d.c
  world3d_paths = 3Dfx 0x1000ee10 rules); needs the DOS path task code (GBL/base 0x1000ee10 counterpart).
- E's "child part placed twice" note (BRANARE2 OBJ 8 box at y ~282300) not checked.

### 1.2 Sweep harness (tools/sweep_objectives.c)
- step-0 teleport: `last_px / last_pz` start at the start point (the start is no teleport).
- ANDY: `classify()` marks a destroy node bad when a main ('M') protect node of table 0 has the same target.
- no-floor guard: a teleport whose floor (msim_ground at the target's level + 10 m) is more than NOFLOOR_CM (50 m) below the
  target's level is refused - the player stays (note line in -v). 10 m (E's figure) also caught HOFFSCN1's visit target (28 m
  over its ground) and made HOFF fail once; 50 m keeps BRAN (113 m) / ZACK (3.9 km) covered. `MW2_SWEEP_NOFLOOR=cm` overrides
  (tests).
- network arenas: with GBL detected, the default scene list skips CALI COLL DOBE HUSK PERS SHAR SIAM TABB TERR
  (`RESULT XXXXSCN1 net arena - skipped`).

### 1.3 DUMBTURT (engine behaviour, BOTH games)
- src/mech3d.c `mech3d_load_repr`: `rc = (out->part_count > 0 || out->node_count > 0) ? 0 : -1;` - an all-DUMMY skeleton
  loads with its nodes / POFO / EYEO; the units spawn (BRANUST1-4, ZACKUST1-4/ENT1-4, TIM_ENT1). ASSUMED: with no parts they
  cannot be struck (the engine's DUMMY collision not traced) - TIM_SCN1 now shows 1 anomaly + 1 fallback (the harness
  finishes the unhittable TIM_ENT1 with combat_hit).

### 1.4 Sweep results (MW2_INSTALL_DIR = copy of the GBL install; logs in /tmp/J_w)
| run | 72 PRJ missions | 23 loose *SCN1 |
|---|---|---|
| before (original code) before_prj.txt / before_loose.txt | 63 successful, 2 failed (CALI, DOBE 0.1 s), 7 no outcome (net arenas) | 20/23 (ANDY failed 110 s, BRAN failed 0.1 s, ZACK failed 0.1 s) |
| after task 1 (t1_*) | 63 successful + 9 "net arena - skipped" | 21/23 with the 10 m guard (HOFF failed once) |
| final, GBL rules (t3_prj.txt / t3_loose.txt) | 63 successful + 9 skipped | 21/23: ANDY ok 98 s; BRAN failed 26.8 s (lift path, 1.1); ZACK failed 146 s (harness: carrier deck, E 1.3) |
| final, MW2_GAME=mw2 (t2base_loose) | - | 21/23 (same two) |

## 2. The GBL weapon table (all behind sim_game_gbl)

### 2.1 Table (src/wtable.h)
- `sim_weapons_gbl[35]` (GBL VA 0xafecc, 35 x 0x68; dumped from mw2gbl.elf file 0x90ecc): entries 0-30 = the port's base
  table with the fields GBL changed against the base DOS table (0x9ee58) set to GBL's: LB 2/5/10/20-X damage 4/10/20/40,
  Ultra AC/2/5/10/20 4/10/25/50, Flamer redone (kind 8, visual 10, 10 per volley, no ammo, sound 456, speed 20, damage 0,
  heat 3.5, target heat 5.0, range 50 m, refire 182, interval 4, life 364), NARC (kind 12, visual -1, 6 per ton), AMS (24 per
  volley, 20 per ton, sound 455, damage 0, refire 22, interval 11, life 182), Nuke (kind 4, sound 461, speed 100, damage
  1000, target heat 50, refire 182). LRM5 ammo 24 (base DOS 12; the port's 3Dfx table already had 24). 31-34 new: Inferno
  SRM-2 (kind 3, visual 10, damage 0), Torpedo (kind 9, visual 34, guided, speed 10, damage 50, life 2730), Inferno SSRM-2
  (guided), ARROW IV (kind 10, visual 35, guided, damage 7, heat 10, range 2002 m, refire 3640).
- `sim_weapons_gbl_x[35]` {flags +0x08, texture base +0x0c, muzzle effects +0x18/+0x1c, f14 +0x24, guided +0x28};
  `sim_weapon_short_gbl[35]`; accessors `sim_wtab()`, `sim_wcount()`, `sim_weapon_name()`, `sim_wx()` (NULL in base).
- ASSUMED: unchanged entries keep the port's 3Dfx timings (181-Hz-scaled refire/life, e.g. SRM life 543 vs DOS 709); the
  new 31-34 carry GBL's DOS values as they stand (Inferno SRM life 709).

### 2.2 MEK loader / combat (src/mek.c, src/combat.c, src/combat.h)
- src/mek.c: `mek_weapons_gbl[35]` = GBL MW2SHELL.EXE table at VA 0x80ad4 (35 x 40 bytes {heat, damage, unk, short, medium,
  range m, tons x 100, crits, ammo/t, name*}; names "NARC", "AMS", "Inferno SRM-2", "Torpedo", "Inferno SSRM-2",
  "ARROW IV"; NARC/AMS/Nuke re-figured). `mek_weapon_for()` uses the game's table (ids 3101-3499 accepted in GBL only);
  `mek_weapon_table()` / `mek_weapon_table_count()` for the shell. mek_weapons / mek_weapon_count unchanged.
- src/combat.c: all table reads via `WT` (= sim_wtab()); `combat_init` (GBL 0x4d670): up to 14 weapons
  (COMBAT_MAX_WEAPONS 14; the base path still stops at 10), weapon index = id / 100 (was the pointer offset into
  mek_weapons), and in GBL no "damage > 0" filter (Inferno, Flamer, NARC, AMS load; the base filter kept for base).
- selectable flag (+0x08 bit 4; GBL 0x53af0 / 0x53bb0 / 0x53c50 / 0x53d00 cycling / groups and the AI's choice skip the
  others = the AMS): `combat_weapon_selectable()`; combat_trigger_mask, ai_next_weapon, ai_choose, combat_ai_trigger skip
  non-selectable weapons in GBL. GBL AI fire roll rand(refire / 0x5b + 1) (0x2ec90; base port keeps /90).
- test (scratch harness /home/claude/agents/J/wtest.c on the msim API, GBL data, CLAISCN1/BASSSCN1, the other actors held offline):
  EXE00WTR loads Torpedo x2 (ammo 100) + PPC x4, fires 12 torpedoes, 18 hits, 674 -> 332; NGA00USR / NGA01STD Arrow IV x2 (ammo
  15) fire (4 at 600 m kill a 674-point target in 24 s, splash off - see 2.4); HLB00HOF Inferno SSRM-2 x2, SSRM6 x2, uAC20,
  AMS, MPLAS x2 load and fire; DRW00USR loads all 14 weapons (2 AMS, Inferno SRM, Torpedo, 10 MG). Base mode
  (MW2_GAME unset) on DRW00USR: 10 weapons, the new ids skipped as before.

### 2.3 Projectiles (src/msim.c)
- 255 projectile slots by kind (0x53823: a free slot of the weapon's kind, else no projectile - the round is spent):
  `spawn_shot_h` in GBL caps at 255 and at `gbl_pool[kind]` = the MW2_SHT1 BTHG count of the kind (GBL file: lasers 11/11/11,
  SRM 32, LRM 48, bullet 32, PPC 8, gauss 6, flamer 32, torpedo 8, Arrow IV 5, NARC 3; loaded in msim_init). Test: Torpedo
  max 8 alive, Arrow IV <= 2 (refire), NARC 1.
- missile offset at launch for kinds 3 4 9 10 12 (0x53823 -> 0x54730) in GBL (base 3 / 4).
- guided per the table's +0x28 (`msim_weapon_guided`), lock window min from +0x4c (`lock_min`, base values unchanged).
- Torpedo (flags 0x10): 0x51cd0's eased guidance - the acceleration moves toward the steering by ticks/0x2d8 of the
  difference.
- kinds 8 / 9 / 10 / 12 in the update 0x516b0: no kind-specific movement or hit code (only kind 7 sets hit flag 0x10, and
  flags 0x80 the NARC bit) - they move and hit like any projectile. Torpedo: no water test found (ASSUMED: flies anywhere).
- GBL 0x516b0: the proximity flag (+0x3d & 0x80) with no homing class -> the projectile ends where it is with its plain
  effect (used by AMS intercepts of unguided missiles and a lost target).

### 2.4 Effects, Inferno heat (src/fx.c, src/fx.h, src/msim.c)
- FX_TYPES 32 -> 36. GBL table 0xb0d04 (36 x 0x2c) dumped: changed rows 10 "burning" (910 ticks, sound 467, damaging,
  128 / 1024 per tick, flags 3), 22 nuke (1820 ticks, 8192 / 1024, splash), 32 / 33 (36-tick flashes), 34 torpedo blast
  (254, sound 457, 1024/tick), 35 Arrow IV (254, sound 465, 8192/tick, splash). fx_duration / fx_sound / fx_light use the GBL
  rows; new fx_damaging / fx_damage16 / fx_heat16 / fx_flags. fx_muzzle from the table's +0x18/+0x1c in GBL.
- fx_impact_type (0x52040): GBL types up to 0x23; visuals 0x22 / 0x23 take the missiles' mapping (ground 0x0c, player 0x11,
  structure 0x13, a unit their own 34 / 35) and the 0x10b chunks (missile_fx). fx_spawn's 5 m neighbour rule in GBL: types
  < 0x17 or > 0x21 without the splash flag.
- area damage GBL 0x52670 -> 0x52900 (`gbl_area_damage`): every damaging effect; a follow effect (flags 1: burning) rides the
  unit it struck (an effect not on a unit lasts 0xb6 ticks); reach = the effect's radius, nuke age x 0x1c cm, Arrow IV age x
  0x18 cm; mechs hit only with SPLASH DAMAGE on, or always for 0x16 / 0x23: octagonal distance < unit radius + reach ->
  +0x20 x ticks (16.16) on locations 2-8, a quarter on the head (x2 for a destroyed unit), +0x24 x ticks heat.
- INFERNO finding: its direct damage and target heat are 0; the heat comes only from effect 10 "burning" (visual 10 -> type
  10) attached to the unit for 910 ticks: 1024/65536 = 0.0156 heat a tick (14.2 per burning effect) and 128/65536 damage
  a tick on every location - and only with SPLASH DAMAGE on (0x52670 calls 0x52900 for other types only when MW2DIF +2 is
  set). Test: 16 Inferno SSRM hits, splash on: target heat to 115.6 (overheat), 674 -> 509; splash off: heat 0.9, no damage.
  The Flamer is the same plus +5.0 target heat per hit (+0x48).
- Arrow IV: 7 per hit + effect 35, always splash: ~0.125 per tick per location for 254 ticks (~32 per location) inside a
  radius growing to 61 m. Test above.
- Not modelled (base port gap too): structures/things in an effect's reach (0x52a70 / 0x2c180; base 0x51910 = GBL 0x52a70).
- Visual only, not done (tools/glview.c): projectile models for kinds 8 / 9 / 10 / 12 (GBL MW2_SHT1 BTHG models 5654 /
  5887 / 5883 / 5885 - glview's proj_slot_w has slots for kinds 0-5 only: these shots fly and hit but are not drawn), the
  burning effect drawn following its unit, the Flamer texture animation (flags 0x20), the HUD panel beyond 10 weapons.

## 3. AMS, NARC, boss AI (behind sim_game_gbl)

### 3.1 AMS (weapon 29: fn0 0x54943, fn1 0x54aa3 -> 0x54bb0, toggle 0x548d0)
- msim.c `gbl_ams_scan` (before each unit's combat_tick, units up only): a READY AMS with an enemy missile (kind 3 4 9 10
  12, owner of another alliance, 0x2b5c0) within octagonal 500 m and closing -> volley (state 2, 24 rounds, timer 0).
- combat.c `ams_release` (release loop): every `interval` (11 ticks) a burst of rand(5)+1 rounds (<= the volley's
  remainder) off the ammunition (one bin round per burst, as 0x54aa3); the unlimited-ammo player spends 2 and keeps his;
  empty -> state -1 and the volley stops; no projectile (f14 = 0). Counted in `combat_weapon.ams_bursts`.
- msim.c `gbl_ams_bursts` (after combat_tick): per burst the sound (flags 2), muzzle flashes, and 0x54bb0: every enemy
  missile within 500 m gets its intercept distance rolled once (`msim_shot.ams_d`): <= 25 m -> 0; closing: rand(100) > 75 ->
  0, else 2500; then distance <= it -> the proximity flag.
- FINDING: the flag AMS sets is the guidance's "hit next tick" flag (+0x3d & 0x80, 0x51cd0 at 1 m). A missile homing on a
  unit (a locked LRM / Streak / Arrow IV) is therefore not destroyed - it strikes its target at once; only unguided
  missiles (SRMs, unlocked LRMs) end in the air. Test (BASSSCN1, enemy 3 x SRM6, player HLB00HOF with AMS vs BHM01STD
  without): 10 vs 21 hits taken in 40 s, 29 intercepts traced (MW2_AMS_TRACE=1); against 14 locked LRM5s: 116 vs 117 hits.
- `msim_toggle_ams()` (TOGGLE_AMS, action 0xa0): the player's AMS slots off (CW_OFF = 4) <-> armed; returns the message
  suffix (1 "engaged" voice 80, 2 "disengaged" voice 84) for message 10 "Anti-missle suite" (voice 59, table 0xa83b0).
  The front end (glview) does not bind it yet (not bound in the shipped GAMEKEY.MAP). Out of ammunition: 0x42e40(10, 0) -
  suffix 0 shows nothing (ASSUMED register reading).

### 3.2 NARC (weapon 28, flags 0x80)
- msim.c hit path: a flags-0x80 projectile striking a unit sets `combat_unit.narc` (target +0x20 struct +0x112 |= 1; never
  cleared). `gbl_narc_lock()` (0x54160): the selected weapon guided and the target unit NARCed -> locked at once
  (0x80c0, timer 0, no range / aim window), for the player's lock and the AI's lock. Test (WT_FORCE NARC into slot 0):
  lock 0x8040 (acquiring) before the beacon hits, narc 1 and 0x80c0 from the next frame on.

### 3.3 Boss AI profiles (GPS skill 100-103; 0x217b0, table 0xa62dc, records 0xa6e80..0xa6e98)
- ai.c `ai_gbl_profile()` + GBL_MAN[4][13][9] / GBL_PROF (dumped: 100 summoner = the jumper table, range 0 -> 15000; 101
  timbrwlf 25000; 102 executnr 5000, group fire; 103 kodiak 20000; skill 1, level 1 each); msim_init applies it (skill /
  level replaced, mind gets the table and engage range, unit the group-fire flag).
- ai.c `choose_manoeuvre`: the unit's table when set - first pick rand(rows 13 - 7) = rand(6) (resolves E's open question
  7: the field is rows minus the first-pick count, the same rand(6) as the 3Dfx chooser), the first matching row.
- ai.c manoeuvre 0 (0x2d4e0): R = engage range; up to 150 m the 45 m rule, beyond: inside R -> manoeuvre 10 (backs off).
- combat.c ai_choose: group-fire flag -> heat test with weapon_count x the current weapon's heat (0x2ec90's slip). The
  0x2ee80 "all weapons into group 0" only acts with 1-2 weapons (executnr has 10 MGs: nothing) - not modelled.
- Test (wtest WT_MAN, player standing 150 m off): GUI2ENS1 (101) runs manoeuvre 10 whenever closer than 250 m (10 at 104,
  146, 226, 249 m; manoeuvre 0 at 292 m); GUI4ENS1 (103) the same inside 200 m. Sweep: GUI1-4 all successful; GUI2 needs
  the harness fallback (the boss keeps away from the harness's 120 m firing point).
- ASSUMED / not done: GBL 0x2c970 also gives every unit a record by class and the jumper table when controller +0xe8 == 1
  (meaning in DOS not pinned); the port keeps the 3Dfx table for non-profile units (base behaviour).

## Files changed (all in /home/claude/agents/J/mw2port)
- src/mek.c, src/mek.h - sim_game_gbl, GBL shell weapon table, mek_weapon_for by game, mek_weapon_table[_count]
- src/wtable.h - GBL sim table + extra fields + accessors
- src/combat.c, src/combat.h - table accessor, 14 weapons, GBL loader rule, selectable, AMS release, narc/group_fire
  fields, CW_OFF
- src/msim.c, src/msim.h - lift ground (task 1), GBL pools / missile kinds / guidance / prox burst / NARC / AMS / area
  damage / effects follow, toggle API, boss profile hookup
- src/fx.c, src/fx.h - 36 effect types, GBL rows and fields, impact mapping, neighbour rule
- src/ai.c, src/ai.h - boss profiles
- src/mech3d.c - DUMBTURT
- tools/sweep_objectives.c - step 0, ANDY classify, no-floor guard, GBL detection, net arenas
- tools/glview.c - weapon names / kind lookup through the game's table (35 entries; was an out-of-bounds read)
Scratch test harness (not in the repo): /home/claude/agents/J/wtest.c - build in the copy with
`cc -std=c99 -D_DEFAULT_SOURCE -Isrc ../wtest.c src/lance.c src/msim.c src/fx.c src/ai.c src/combat.c src/mtbl.c src/anim.c
src/mek.c src/mech3d.c src/wtb.c src/bwd.c src/prj.c src/datapath.c -o wtest -lm`; run
`MW2_INSTALL_DIR=<GBL install> MW2_NO_STARTUP=1 ./wtest MW2.PRJ SCENE PLAYER_MEK [ENEMY_MEK] secs dist_cm` with WT_FREEZE /
WT_NOFIRE / WT_MASK / WT_SPLASH / WT_FORCE=i:w / WT_EFORCE=w / WT_SEL / WT_SEL2 / WT_MAN / WT_STAY / WT_TRACE.
Sweep logs: /tmp/J_w/*.txt.
