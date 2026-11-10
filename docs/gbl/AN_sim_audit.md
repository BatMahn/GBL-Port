# AN - simulation / combat / AI audit against GBL MW2.EXE (Round 10)

Base: main c2d805f, branch an-work. Method: (1) differential tests that EXECUTE the original's functions (unicorn, the
ELF image of MW2.EXE's LE executable) on random and edge inputs and compare with the port's C; (2) a decompile-vs-port
read of the main loop's simulation / AI / combat call graph; (3) fixes for every confirmed difference.

## 1. Harness (tools/audit, no game data in the repo)

    pip install unicorn
    tools/audit/build.sh                        # the port's sources as shared libraries (shims), /tmp/simdiff_build
    GBL_ELF=/path/mw2gbl.elf python3 tools/audit/simdiff.py [-n cases] [-s seed] [test ...]

- `gblemu.py`: loads the image, runs a function (Watcom register calls) or a piece of one (`run(start, until)`) with
  stubbed callees; the engine's sine / atan tables are built by running its own initialiser 0x3b520 first; any access
  outside the image, stack or test heap stops the run (footprint check).
- `shim_*.c`: `#include` the port's sources so their statics can be called through ctypes.
- 9 tests, ~11 000 checks, 3 s: all pass on an-work. Against c2d805f: collision_damage 129 / 900 differ,
  heat_override_roll fails (port 7.8 s mean to a cook-off vs DOS 99 s at 6-tick frames); the others exercise functions
  that the fixes introduced (they encode the original's rules and would fail on the old code's numbers, see 3).

| test | original code run | port function |
|---|---|---|
| heat_state | 0x252b0 (heat per frame: sinking, warning, sequence, shutdown, restart), 400 random frames x 40 | combat_tick |
| heat_sink_tiers | 0x4dc8d..0x4dd75 (loader: dissipation by player / difficulty / temperature) | combat_sink_rate, combat_init + combat_set_player_difficulty |
| heat_override_roll | 0x25430..0x2545c rand(500 x ticks) argument; port mean time to a cook-off | combat_tick |
| ai_nav_turn | 0x22ce0 nav steering | ai_nav_turn |
| leg_turn_demand | 0x28054..0x280f0 (leg turn demand: speed / 10000, spot-turn rule, cos table, airborne) | msim_leg_demand |
| servo_step | 0x4a9d0 integer servo, T 36 / 54 / 91 / 109 | drive_servo (msim_servo) |
| turret_twist | 0x2f193..0x2f1fa + 0x4a9d0 (turret short-way twist) | msim_twist_round |
| collision_damage | 0x20ca0 / 0x20ae0 -> 0x20b80 (0x265e0 stubbed) | combat.c collide |
| manoeuvre_records | 0x2c970 on every class x tonnage x GPS skill | ai_gbl_profile + ai_set_class |

## 2. Fixes (each: GBL address, before -> after; all in the commits of an-work)

| # | what | GBL | before | after |
|---|---|---|---|---|
| 1 | heat sinking tiers | 0x4dc98..0x4dd7b | -100 or colder not modelled (AI / Medium 100 per point); Hard cold 67.5 | 250 (AI / Medium), 350 Easy, 225 Hard; Hard cold 67 - BRAN, CLAI, PETE, ZACK sink 2.5x |
| 2 | override cook-off | 0x25430 rand(500 x frame ticks) == 0 | 3Dfx 3 in 4000 a tick: mean 7.8 s | 1 in 18000 a tick (the per-frame rule at 30 fps): mean ~95 s (DOSBox 45 fps ~44 s, period hardware longer) |
| 3 | leg turn servo | 0x278e1 heading += +0x3c x ticks / 182; servo +0x34 T 54 (0x26ad1) | heading turned at the demand at once | eased (T 54 ticks): turns build up / run on ~0.3 s (player and AI; FEET_TO_TORSO too, 0x2811d) |
| 4 | spot turn / airborne turn | 0x28054..0x280ed | AI turned at any speed; immobile player pivoted; turning in mid-air | no turn under kv 1 (~1 km/h) for computer-run units and the immobile player; none airborne (feet > centre height over ground, 0x276fb) without jets |
| 5 | manoeuvre tick constants | 0x2ced0, 0x2c639, 0x2c727, 0x2d8c0 | 3Dfx 181 Hz values (9050, 1448 + 181 r, 3620, 905, 1810 + 181 r, 724 / 1448, 18100, 543) | 9100, (r + 8) x 182, 3640, 910, (r + 10) x 182, 728 / 1456, 18200, 546, 182 |
| 6 | manoeuvre 10 at a wall | 0x2cfd3 | always reversing | after a 10 in contact with a non-unit: 728 ticks, no reverse flag |
| 7 | closing rate (manoeuvres 2 / 3) | 0x2d6b0 | x 1/7.7 | (closing << 32) / 500000 / 65536 = x 0.131 |
| 8 | AI aim servos | 0x26a50 (0.6 / 0.2 s), 0x2eed0 (0.8 / 0.8 s) | 108.6 / 36.2 for every unit | mechs 109 / 36; turrets 145 / 145 (turret pitch was 4x quick) |
| 9 | turret whole-turn twist | 0x2f1a9..0x2f1d3 | linear servo on +-180: swung the long way when the target crossed behind | value taken to within 180 of the demand first |
| 10 | aim range servo | 0x24f15 | 3620 | 3640 |
| 11 | collision damage | 0x20ca0 / 0x20ae0 / 0x20b80 | 3Dfx threshold / factor, x 2 tonnage for the head, sqrt speed, wall bearing off heading only | 200000 / x 3 / 1300000 on the DOS length; head x 4 tonnage; heading + twist; band edges; impact sound and thing share at the same threshold |
| 12 | jump to turn | 0x2e600 | jets on / off every tick (short hops), manoeuvre 6 too | switched only while standing: a turn jump lasts until the landing; manoeuvres 0-3, 7, 8, 12 |
| 13 | manoeuvre records by class | 0x2c970, 0x2cb80 | every unit the mech table | classes 2-8 one row {0} (manoeuvre 0 only), 1-ton units (Elementals) the jumper table; contact / jump pre-checks for mechs only |

## 3. Checked, no difference (read against the decompile / asm)
Rule engine 0x21b60 and think 0x21a80; conditions 1-3 (0x22290 / 0x22343 / 0x22103); fire decision 0x2eb60 (aim error is
always 0); throttle 0x22bd0; manoeuvre 1 (0x2d590) and its slot re-placement; manoeuvre end 0x2d1b0; manoeuvre 3 weave
(450 x +0x17e); weapon state machine 0x53200 (states 0-3, release loop, repeat); heat state machine 0x252b0 (emulated);
walking heat (0x28407: (throttle - 0x400) x sink / 0x2800 a frame = the port's 30 fps form); jets heat 0x180 a jet a tick
(0x27fa0) and fuel (0x71c, refill ticks >> 2); jet thrust 0x1e50 (loader); top speed / gravity ratio (0x26cd5, 0x15064,
0x4f58e); fall damage (0x26d05); the twist-limit clamp (0x283xx); state 0 / 1 start-up (0x284ea); target heat on a hit
(0x519b9) and the rear test (0x51984); heat-sink critical -50 (0x25cc0); pilot-level bits +0x19c (0x2c9b7..0x2cb26);
servo set-up 0x4a990 (emulated: chop(s x 182)); the mech manoeuvre table 0xa6884 and the jumper / profile tables
(emulated, manoeuvre_records); MASC (0x28325: malfunction roll and x 1.5 only while the current key has flag 0x10) noted.

## 4. Open / ASSUMED
- The cook-off roll is per frame in DOS (chance falls with the square of the frame length); the port takes it at a
  30 fps frame (ASSUMED reference, as the walking heat).
- Airborne with the autopilot on, DOS keeps the last leg demand (0x280e3): the port uses 0.
- Turret twist limit < 360: DOS 0x2f1d8 clamps the target but never takes a new demand (an original quirk; no GBL turret
  seems to have one - not ported).
- Obstacle-avoidance flag +0x18e gates the jump-to-turn (0x2e600): not modelled (the port's avoidance state is in msim).
- Jump pre-check reachability probe 0x2e450 (0x2cb80): not modelled (as before).
- MASC: GBL applies x 1.5 and the malfunction roll only while the walk key with flag 0x10 is current (object +0x80 bit 1 is
  cleared on every key, 0x1b2e1); the port's drive_slow is permanent (T 0x5b is - nothing resets the servo's T) - not
  changed (player MASC only; not traced further).
- Not checked: see the table (175 functions, mostly path tasks 0x1ad22.., collision geometry 0x1c9f0.., the AI's state
  handlers 0x22800 / 0x23xxx beyond the rule engine, manoeuvres 5 / 9 / 12 bodies, 0x52ec0 effects).

## 5. Coverage
The call graph from the main loop 0x15af0 (static calls + the unit class table 0xa6540, the AI state / condition /
action tables 0xa64ac.., the weapon fire pointers 0xafed0.., the path-task table 0xafc14) plus every function in
0x1f000-0x2f400 and 0x51000-0x54d00, and the loader 0x4d670: 695 functions. "checked earlier": an earlier agent's notes / docs trace an address
inside it (AC AD AE AI J U V W Y ...). Runtime: the C library, DOS4GW, sound and video drivers (>= 0x54d00).

| status | functions |
|---|---|
| runtime / driver | 180 |
| not checked | 175 |
| checked earlier | 152 |
| shared (render) | 68 |
| shared (render+mission) | 48 |
| shared (mission) | 30 |
| checked (read) | 24 |
| FIXED | 14 |
| checked (emulator) | 4 |

| GBL | decomp lines | port cites | status | note |
|---|---|---|---|---|
| 0x10780 | 27 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x11020 | 62 | msim.c | shared (render+mission) | also reached from the render / mission loops |
| 0x15180 | 26 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x15a40 | 10 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x174a0 | 9 | - | not checked |  |
| 0x174c0 | 30 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x17520 | 9 | - | not checked |  |
| 0x175c0 | 30 | - | shared (render) | also reached from the render loop (AM) |
| 0x17620 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x17670 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x176b0 | 9 | - | not checked |  |
| 0x1ad22 | 343 | - | checked (read) | AQ: animation task (TSK type 3); 0x1b2e1 key flags +0x80 bits 1 / 3 -> MASC fixed in glview |
| 0x1b3e0 | 20 | - | not checked |  |
| 0x1b4c2 | 102 | - | checked earlier | T |
| 0x1b652 | 106 | - | FIXED | AQ: spin period x 182 (0x1b729), was 181 |
| 0x1b832 | 110 | - | FIXED | AQ: circling / 0xb6 (0x1b9a0), was 181 |
| 0x1ba62 | 90 | - | checked earlier | T |
| 0x1bbc2 | 273 | msim.c | FIXED | AQ: path servos 0.3 s = 54 ticks (0x4a990), was 0.3 x 181 |
| 0x1c2c0 | 15 | - | shared (render) | also reached from the render loop (AM) |
| 0x1c2e0 | 19 | - | checked earlier | E_sim.md |
| 0x1c370 | 58 | msim.c | checked earlier | AC, AD, AE, E_sim.md, J_sim_impl.md |
| 0x1c530 | 10 | - | not checked |  |
| 0x1c560 | 16 | - | not checked |  |
| 0x1c5b0 | 26 | - | not checked |  |
| 0x1c600 | 17 | - | not checked |  |
| 0x1c640 | 59 | - | checked earlier | AD, AE, E_sim.md |
| 0x1c760 | 50 | - | checked earlier | AC, AE |
| 0x1c850 | 53 | - | not checked |  |
| 0x1c970 | 29 | - | not checked |  |
| 0x1c9f0 | 106 | - | not checked |  |
| 0x1cbc0 | 42 | - | not checked |  |
| 0x1cc50 | 108 | - | shared (render) | also reached from the render loop (AM) |
| 0x1ce00 | 108 | - | not checked |  |
| 0x1cfb0 | 108 | - | checked earlier | AH |
| 0x1d160 | 52 | - | shared (render) | also reached from the render loop (AM) |
| 0x1d230 | 117 | - | shared (render) | also reached from the render loop (AM) |
| 0x1d430 | 39 | - | shared (render) | also reached from the render loop (AM) |
| 0x1d4c0 | 23 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x1d4f0 | 62 | - | shared (render) | also reached from the render loop (AM) |
| 0x1d900 | 54 | msim.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x1da90 | 27 | - | checked earlier | AD |
| 0x1daf0 | 35 | - | not checked |  |
| 0x1db90 | 15 | - | not checked |  |
| 0x1dbb0 | 56 | - | not checked |  |
| 0x1dc40 | 26 | - | not checked |  |
| 0x1dcf0 | 35 | - | checked earlier | AH |
| 0x1de20 | 35 | - | not checked |  |
| 0x1de90 | 14 | - | not checked |  |
| 0x1dec0 | 21 | - | not checked |  |
| 0x1def0 | 47 | msim.c | not checked |  |
| 0x1dfb0 | 78 | - | checked earlier | AH, AL |
| 0x1e0b0 | 45 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e140 | 11 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e150 | 9 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x1e190 | 10 | - | checked earlier | AC, AH |
| 0x1e1b0 | 12 | msim.c | shared (render+mission) | also reached from the render / mission loops |
| 0x1e1f0 | 13 | glview.c, mech3d.h | checked earlier | AL, T |
| 0x1e210 | 13 | msim.h | checked earlier | J_sim_impl.md, R, V |
| 0x1e240 | 24 | - | checked earlier | T |
| 0x1e2c0 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e300 | 22 | glview.c | not checked |  |
| 0x1e350 | 13 | mech3d.c | checked earlier | AH, R, T, Z |
| 0x1e390 | 13 | - | not checked |  |
| 0x1e3d0 | 14 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e400 | 25 | - | checked earlier | AL |
| 0x1e440 | 26 | - | checked earlier | AL |
| 0x1e490 | 25 | - | checked earlier | W |
| 0x1e550 | 22 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e590 | 22 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e5d0 | 22 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e610 | 23 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e650 | 44 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e6d0 | 23 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e720 | 9 | - | not checked |  |
| 0x1e730 | 9 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e740 | 9 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e760 | 23 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e790 | 9 | - | checked earlier | AC, AH |
| 0x1e7d0 | 50 | - | shared (render) | also reached from the render loop (AM) |
| 0x1e870 | 27 | - | checked earlier | R |
| 0x1e9a0 | 25 | - | checked earlier | Z |
| 0x1e9f0 | 22 | - | not checked |  |
| 0x1ea30 | 23 | - | not checked |  |
| 0x1eaa0 | 20 | - | not checked |  |
| 0x1eb50 | 35 | - | shared (render) | also reached from the render loop (AM) |
| 0x1ec70 | 66 | - | checked earlier | S, Z |
| 0x1ed50 | 23 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x1edd1 | 12 | - | not checked |  |
| 0x1edf0 | 12 | - | shared (render) | also reached from the render loop (AM) |
| 0x1ee40 | 38 | - | shared (render) | also reached from the render loop (AM) |
| 0x1eeb4 | 103 | - | checked earlier | E_sim.md |
| 0x1f0d0 | 35 | - | not checked |  |
| 0x1f0d2 | 35 | glview.c, mech3d.c, mech3d.h | not checked |  |
| 0x1f1e0 | 12 | msim.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x1f202 | 39 | glview.c, mech3d.h | checked earlier | E_sim.md |
| 0x1f314 | 22 | - | not checked |  |
| 0x1f370 | 72 | glview.c | checked earlier | AL |
| 0x1f4c0 | 64 | glview.c, mech3d.h | checked earlier | AL |
| 0x1f650 | 21 | - | not checked |  |
| 0x1f690 | 10 | - | not checked |  |
| 0x1f6b0 | 14 | - | not checked |  |
| 0x1f6e0 | 11 | - | not checked |  |
| 0x1f740 | 57 | - | checked earlier | E_sim.md |
| 0x1f880 | 45 | - | not checked |  |
| 0x1f980 | 28 | mech3d.h | not checked |  |
| 0x1fa10 | 36 | glview.c, mech3d.h | checked earlier | AL |
| 0x1fab0 | 27 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x1fb30 | 25 | - | checked earlier | AL |
| 0x1fba0 | 19 | mech3d.h | shared (render) | also reached from the render loop (AM) |
| 0x1fc00 | 20 | mech3d.h | shared (render) | also reached from the render loop (AM) |
| 0x1fc60 | 27 | glview.c, msim.c | checked earlier | AC, AL |
| 0x1fce0 | 28 | glview.c, mech3d.h | checked earlier | AL |
| 0x1fd70 | 25 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x1fde0 | 17 | - | not checked |  |
| 0x1fe20 | 15 | - | not checked |  |
| 0x1fe40 | 44 | - | not checked |  |
| 0x1fef0 | 205 | glview.c, msim.c | checked earlier | AC, AE, E_sim.md, L, S, W |
| 0x20320 | 253 | msim.c, msim.h | checked earlier | AC, AE, E_sim.md, W, Y |
| 0x207c0 | 182 | - | checked earlier | AC, AE |
| 0x20ae0 | 46 | combat.c, glview.c, msim.c | FIXED | threshold 200000, x3/1300000, DOS length | test: collision_damage |
| 0x20b80 | 52 | combat.c, msim.c | FIXED | head x4 tonnage; bearing off heading+twist; edges | test: collision_damage |
| 0x20ca0 | 50 | combat.c, glview.c, msim.c | FIXED | threshold 200000, x3/1300000, DOS length; thing share whole points | test: collision_damage |
| 0x20d60 | 17 | msim.h | checked earlier | E_sim.md, W |
| 0x20de0 | 23 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x20e30 | 18 | - | not checked |  |
| 0x20e70 | 163 | - | not checked |  |
| 0x212a0 | 21 | - | not checked |  |
| 0x213b0 | 101 | - | not checked |  |
| 0x216f0 | 26 | - | not checked |  |
| 0x21760 | 23 | - | not checked |  |
| 0x217b0 | 71 | ai.h, msim.c | checked earlier | AI, E_sim.md, J_sim_impl.md |
| 0x219c0 | 35 | - | not checked |  |
| 0x21a80 | 43 | - | FIXED | AQ: rules run in states 10 / 11 too (resting units wake) |
| 0x21b60 | 71 | msim.c | checked (read) | rule engine: matches |
| 0x21ce0 | 64 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x21e40 | 35 | msim.c | checked earlier | AE |
| 0x21f24 | 14 | - | not checked |  |
| 0x21f70 | 29 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x21fe0 | 25 | - | not checked |  |
| 0x22080 | 18 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x22103 | 53 | - | checked (read) | cond 3: matches (nav side effects in msim) |
| 0x22290 | 42 | - | checked (read) | cond 1: matches |
| 0x22343 | 56 | msim.c | checked (read) | cond 2: matches |
| 0x224c0 | 91 | - | checked earlier | AE |
| 0x226d2 | 32 | - | checked (read) | AQ: condition 7 = 0x224c0 with range a/b/c |
| 0x227d0 | 10 | ai.c, msim.c | checked earlier | AC, AE, Y |
| 0x229e1 | 16 | - | checked (read) | AQ: push (+0x13c, slot +0x124) |
| 0x22a42 | 22 | - | checked (read) | AQ: pop |
| 0x22a90 | 35 | - | checked earlier | AE |
| 0x22b50 | 13 | - | checked (read) | target data: matches |
| 0x22b80 | 28 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x22bd0 | 38 | - | checked (read) | throttle: matches |
| 0x22c70 | 10 | - | checked earlier | AI |
| 0x22ca0 | 11 | msim.c | checked earlier | AI, J_sim_impl.md |
| 0x22ce0 | 38 | ai.c, ai.h, glview.c | checked (emulator) | ai_nav_turn |
| 0x22d90 | 57 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x22ef0 | 128 | msim.c | FIXED | AQ: state 2 entry fire delay rand(10) x 0x12 on every path |
| 0x23220 | 87 | - | checked earlier | AE |
| 0x233f0 | 18 | - | not checked |  |
| 0x23420 | 67 | - | checked earlier | AE |
| 0x23531 | 17 | - | not checked |  |
| 0x23580 | 25 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x235e0 | 44 | - | checked earlier | AE |
| 0x236d0 | 21 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x23740 | 65 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x238b0 | 15 | - | not checked | AQ: next nav of manoeuvre 12 (not ported) |
| 0x23900 | 24 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x23970 | 24 | - | not checked |  |
| 0x23a00 | 23 | - | not checked |  |
| 0x23a80 | 17 | - | not checked |  |
| 0x23af0 | 54 | - | not checked |  |
| 0x23c40 | 71 | - | not checked |  |
| 0x23d60 | 36 | - | checked earlier | AI, J_sim_impl.md |
| 0x23db0 | 10 | - | not checked |  |
| 0x23dc0 | 115 | - | FIXED | AQ: a node change with a state pushed writes the stack slot (unit keeps its target) |
| 0x24030 | 15 | - | not checked |  |
| 0x24070 | 59 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x24270 | 34 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x242e0 | 17 | - | not checked |  |
| 0x24310 | 88 | - | checked (read) | AQ: pushed assignment (port counter = 1) |
| 0x24570 | 221 | - | checked earlier | AE |
| 0x249d0 | 9 | - | not checked |  |
| 0x24a30 | 38 | - | FIXED | AQ: class 1 only, standing too, due time; simdiff patrol_sweep |
| 0x24ae0 | 25 | - | not checked |  |
| 0x24b30 | 25 | - | not checked |  |
| 0x24b80 | 26 | - | not checked |  |
| 0x24bd0 | 17 | - | not checked |  |
| 0x24c10 | 17 | - | not checked |  |
| 0x24c50 | 25 | - | not checked |  |
| 0x24c90 | 15 | glview.c, msim.c, msim.h | shared (render) | also reached from the render loop (AM) |
| 0x24cd0 | 23 | - | not checked |  |
| 0x24d10 | 34 | - | checked earlier | E_sim.md |
| 0x24da0 | 21 | - | not checked |  |
| 0x24de0 | 63 | combat.c, combat.h, msim.c | FIXED | aim range servo 20 s = 3640 (0x24f15) |
| 0x24fc0 | 72 | glview.c | checked earlier | E_sim.md, Y |
| 0x251a0 | 18 | - | checked earlier | AC |
| 0x251e0 | 37 | combat.c | checked earlier | J_sim_impl.md |
| 0x252b0 | 101 | combat.c, combat.h, glview.c | FIXED | override cook-off roll rand(500 t) per frame (was 3Dfx 3/4000 a tick) | test: heat_state, heat_override_roll |
| 0x25680 | 164 | msim.c | checked earlier | AI, E_sim.md, J_sim_impl.md, W |
| 0x25a04 | 21 | - | not checked |  |
| 0x25a90 | 59 | - | checked earlier | AI, J_sim_impl.md |
| 0x25cc0 | 341 | combat.c, msim.c | checked (read) | heat sink crit -50 matches |
| 0x265e0 | 154 | combat.c, glview.c, msim.c | checked (read) | (prior AI) + rear test 0x51984 read |
| 0x26960 | 32 | anim.c, glview.c, msim.c, msim.h | checked (read) | mech init (0x26a50, asm): servo T 0.2/0.6/0.3 s - fixed AI twist/pitch/leg |
| 0x26d05 | 561 | anim.c, glview.c, mech3d.c, mech3d.h, msim.c, msim.h | FIXED | heading by the leg turn servo (+0x34, T 54); impact speed = DOS length |
| 0x27b05 | 433 | combat.c, glview.c, msim.c, msim.h | FIXED | leg turn demand: no spot turn for AI / immobile player, none airborne; speed whole kv | test: leg_turn_demand (0x28054..0x280f0) |
| 0x28695 | 113 | glview.c | checked earlier | AE, E_sim.md, U |
| 0x28b20 | 9 | - | not checked |  |
| 0x28b80 | 45 | - | not checked |  |
| 0x28c80 | 10 | - | not checked |  |
| 0x28ca0 | 21 | - | not checked |  |
| 0x28cf0 | 21 | - | not checked |  |
| 0x28d60 | 39 | - | not checked |  |
| 0x28e50 | 41 | glview.c | not checked |  |
| 0x28f60 | 19 | - | checked (read) | fall impact sound: matches |
| 0x28ff0 | 40 | glview.c | not checked |  |
| 0x29102 | 26 | - | not checked |  |
| 0x291b0 | 25 | - | not checked |  |
| 0x29250 | 71 | glview.c | checked earlier | Z |
| 0x29450 | 62 | glview.c | checked earlier | Z |
| 0x296e0 | 59 | glview.c | checked earlier | Z |
| 0x297f0 | 59 | glr.c, glview.c | checked earlier | Z |
| 0x298f0 | 68 | - | checked earlier | Z |
| 0x29ae0 | 42 | glview.c | not checked |  |
| 0x29be0 | 27 | glr.c | checked earlier | AH |
| 0x29c40 | 72 | glr.c | checked earlier | Z |
| 0x29db0 | 76 | glr.c | checked earlier | Z |
| 0x29f30 | 16 | - | not checked |  |
| 0x29f90 | 17 | - | not checked |  |
| 0x2a010 | 33 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2a090 | 65 | glview.c | shared (mission) | also reached from the node interpreter (AO) |
| 0x2a210 | 121 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2a400 | 11 | - | not checked |  |
| 0x2a420 | 66 | glview.c | checked earlier | AC |
| 0x2a590 | 89 | glview.c, msim.c | checked (read) | (prior AE) |
| 0x2a7e0 | 79 | msim.c | checked earlier | AC |
| 0x2a9f0 | 99 | glview.c | checked earlier | AE, Y |
| 0x2acb0 | 15 | glview.c | not checked |  |
| 0x2ad50 | 24 | - | not checked |  |
| 0x2adc0 | 51 | - | not checked |  |
| 0x2aee0 | 45 | msim.c | checked earlier | AI, J_sim_impl.md |
| 0x2af90 | 16 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2afb0 | 10 | - | not checked |  |
| 0x2afe0 | 10 | - | not checked |  |
| 0x2b010 | 10 | - | not checked |  |
| 0x2b070 | 49 | - | not checked |  |
| 0x2b160 | 31 | - | not checked |  |
| 0x2b1b0 | 30 | - | not checked |  |
| 0x2b230 | 12 | - | not checked |  |
| 0x2b250 | 28 | - | not checked |  |
| 0x2b2a0 | 16 | - | not checked |  |
| 0x2b2d0 | 32 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2b360 | 43 | - | not checked |  |
| 0x2b430 | 38 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2b500 | 15 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2b520 | 42 | - | not checked |  |
| 0x2b5c0 | 12 | - | checked earlier | AE, E_sim.md, J_sim_impl.md |
| 0x2b5e0 | 17 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x2b620 | 14 | - | checked earlier | AE |
| 0x2b650 | 14 | - | shared (render) | also reached from the render loop (AM) |
| 0x2b690 | 45 | - | shared (render) | also reached from the render loop (AM) |
| 0x2b770 | 48 | - | shared (render) | also reached from the render loop (AM) |
| 0x2b880 | 65 | - | checked earlier | E_sim.md, W |
| 0x2b9c0 | 22 | - | checked earlier | E_sim.md, W |
| 0x2ba13 | 41 | - | not checked |  |
| 0x2bac0 | 47 | - | not checked |  |
| 0x2bb80 | 55 | - | not checked |  |
| 0x2bc80 | 14 | - | not checked |  |
| 0x2bcb0 | 112 | - | not checked |  |
| 0x2bf30 | 92 | - | not checked |  |
| 0x2c050 | 43 | - | not checked |  |
| 0x2c0f0 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x2c140 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x2c180 | 59 | - | checked earlier | E_sim.md, J_sim_impl.md |
| 0x2c280 | 17 | - | checked earlier | E_sim.md |
| 0x2c2a0 | 35 | - | checked (read) | LOS: +0x19c bit 4 = level <= 4 (always) |
| 0x2c3d2 | 56 | - | not checked |  |
| 0x2c510 | 21 | - | checked (read) | (prior AI) range window |
| 0x2c590 | 165 | - | checked earlier | AE |
| 0x2c594 | 165 | ai.c | checked earlier | AE, E_sim.md, Y |
| 0x2c970 | 67 | ai.c, ai.h | FIXED | manoeuvre record by class / 1-ton jumper table | test: manoeuvre_records |
| 0x2cb80 | 125 | ai.c | FIXED | chooser: contact / jump pre-checks class 1 only |
| 0x2ced0 | 112 | ai.c, ai.h | FIXED | manoeuvre limits in 182 Hz ticks; manoeuvre 10 after 10 at a wall |
| 0x2d1b0 | 55 | - | checked (read) | manoeuvre end: matches |
| 0x2d2f0 | 21 | - | not checked |  |
| 0x2d350 | 65 | - | not checked |  |
| 0x2d4e0 | 34 | ai.c, msim.c | checked earlier | E_sim.md, J_sim_impl.md, W, Y |
| 0x2d590 | 57 | msim.c | checked (read) | manoeuvre 1: matches |
| 0x2d6b0 | 44 | ai.c, msim.c | FIXED | closing x 65536/500000 |
| 0x2d7a0 | 26 | - | not checked |  |
| 0x2d850 | 23 | - | not checked |  |
| 0x2d8c0 | 64 | ai.c | FIXED | AQ: phase 0 needs 0x2dd50, jets on through phase 1 |
| 0x2dab0 | 77 | msim.c, msim.h | checked (read) | AQ: manoeuvre 5 matches |
| 0x2dc40 | 25 | - | not checked |  |
| 0x2dc90 | 33 | - | differs | AQ: manoeuvre 12 beyond 150 m switches its goal to a patrol nav (0x23740) - not ported |
| 0x2dd50 | 25 | - | FIXED | AQ: jump pre-check / manoeuvre 9 line probe |
| 0x2ddc0 | 36 | ai.h, msim.c | checked earlier | AE |
| 0x2de80 | 163 | msim.c | checked earlier | E_sim.md, W |
| 0x2e230 | 62 | - | checked earlier | E_sim.md, W |
| 0x2e360 | 35 | - | not checked |  |
| 0x2e450 | 35 | - | FIXED | AQ: line probe (probe_engine_ex) |
| 0x2e540 | 33 | - | checked (read) | AQ: weapons left |
| 0x2e600 | 60 | ai.c | FIXED | jump-to-turn: standing-only switching, manoeuvres 0-3/7/8/12 |
| 0x2e6d0 | 16 | - | not checked |  |
| 0x2e700 | 27 | - | not checked |  |
| 0x2e750 | 111 | ai.c, ai.h, msim.c | checked earlier | E_sim.md |
| 0x2e9f0 | 74 | - | not checked |  |
| 0x2eb20 | 16 | - | not checked |  |
| 0x2eb60 | 50 | ai.c, ai.h, msim.c | checked (read) | fire decision: matches (aim error always 0) |
| 0x2ec90 | 90 | combat.c | checked earlier | AE, AI, E_sim.md, J_sim_impl.md |
| 0x2ee80 | 22 | glview.c, msim.c, msim.h | FIXED | turret servos 0.8 s = 145 (0x2eed0 init, not decompiled); short-way twist (0x2f0f0 update) | test: turret_twist |
| 0x2f3a0 | 34 | - | not checked |  |
| 0x335d0 | 30 | - | not checked |  |
| 0x33a25 | 116 | - | not checked |  |
| 0x33c80 | 9 | - | checked earlier | E_sim.md, U |
| 0x33d80 | 26 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x33e10 | 33 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x33e80 | 29 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x33f10 | 29 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x33f80 | 58 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x341a0 | 29 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x34200 | 104 | - | checked earlier | AH |
| 0x34500 | 13 | - | shared (render) | also reached from the render loop (AM) |
| 0x34540 | 24 | - | shared (render) | also reached from the render loop (AM) |
| 0x34570 | 10 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x3457c | 13 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x34890 | 42 | - | shared (render) | also reached from the render loop (AM) |
| 0x34ae0 | 48 | bwd.c | shared (mission) | also reached from the node interpreter (AO) |
| 0x34e00 | 31 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x350c0 | 43 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x35560 | 30 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x35770 | 32 | - | not checked |  |
| 0x36140 | 20 | - | checked earlier | AL |
| 0x36240 | 23 | - | checked earlier | AC |
| 0x362c0 | 20 | msim.c | checked earlier | AL |
| 0x36390 | 9 | - | not checked |  |
| 0x36880 | 31 | - | shared (render) | also reached from the render loop (AM) |
| 0x36b30 | 73 | - | checked earlier | AL |
| 0x36c60 | 25 | - | not checked |  |
| 0x36cf0 | 64 | - | checked earlier | AL |
| 0x36ea0 | 124 | mech3d.c, mech3d.h, msim.c, msim.h | checked earlier | AL, T, Y |
| 0x371b0 | 15 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x371e0 | 15 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x37210 | 33 | msim.c | checked earlier | AL, Z |
| 0x376e0 | 29 | - | shared (render) | also reached from the render loop (AM) |
| 0x37a80 | 40 | mech3d.h | checked earlier | AL, Y |
| 0x37bb0 | 113 | - | checked earlier | AH, AL |
| 0x37f10 | 15 | - | checked earlier | AC |
| 0x38830 | 15 | - | not checked |  |
| 0x38880 | 20 | - | not checked |  |
| 0x389f0 | 80 | glr.c, glr.h, glview.c | checked earlier | AL, J_sim_impl.md, M, V, X, Y |
| 0x38b30 | 102 | glr.c | checked earlier | AH, X |
| 0x38da0 | 49 | - | checked earlier | V |
| 0x38ed0 | 12 | - | checked earlier | X |
| 0x38f20 | 46 | - | shared (render) | also reached from the render loop (AM) |
| 0x38ff0 | 26 | - | shared (render) | also reached from the render loop (AM) |
| 0x39040 | 24 | - | shared (render) | also reached from the render loop (AM) |
| 0x390a0 | 16 | - | shared (render) | also reached from the render loop (AM) |
| 0x39130 | 27 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x39160 | 10 | - | shared (render) | also reached from the render loop (AM) |
| 0x39180 | 9 | - | shared (render) | also reached from the render loop (AM) |
| 0x391a0 | 10 | - | shared (render) | also reached from the render loop (AM) |
| 0x391b0 | 10 | - | shared (render) | also reached from the render loop (AM) |
| 0x391c0 | 10 | - | shared (render) | also reached from the render loop (AM) |
| 0x39200 | 19 | - | not checked |  |
| 0x39230 | 20 | - | checked earlier | J_sim_impl.md, V |
| 0x39290 | 20 | - | not checked |  |
| 0x393e0 | 15 | - | not checked |  |
| 0x39410 | 9 | - | checked earlier | R |
| 0x39420 | 10 | - | shared (render) | also reached from the render loop (AM) |
| 0x39450 | 10 | - | checked earlier | AH |
| 0x394b0 | 14 | - | shared (render) | also reached from the render loop (AM) |
| 0x39e90 | 14 | glview.c | not checked |  |
| 0x39ec0 | 9 | glview.c | shared (render) | also reached from the render loop (AM) |
| 0x3a060 | 55 | - | checked earlier | AC, AH |
| 0x3a6e0 | 25 | msim.c, msim.h | shared (render) | also reached from the render loop (AM) |
| 0x3ac18 | 63 | - | shared (render) | also reached from the render loop (AM) |
| 0x3ad64 | 35 | - | shared (render) | also reached from the render loop (AM) |
| 0x3ae05 | 16 | mech3d.c | checked earlier | T |
| 0x3ae3a | 18 | - | shared (render) | also reached from the render loop (AM) |
| 0x3ae68 | 59 | - | not checked |  |
| 0x3af49 | 182 | - | shared (render) | also reached from the render loop (AM) |
| 0x3b1d5 | 77 | mech3d.c | checked earlier | X |
| 0x3b2c8 | 33 | glr.h | checked (emulator) | (table init 0x3b520, used by leg_turn_demand) |
| 0x3b338 | 10 | - | checked (emulator) | (via leg_turn_demand) |
| 0x3b347 | 79 | - | shared (render) | also reached from the render loop (AM) |
| 0x3b45c | 12 | - | shared (render) | also reached from the render loop (AM) |
| 0x3b475 | 9 | glr.c, glr.h | checked earlier | Z |
| 0x3b690 | 12 | - | shared (render) | also reached from the render loop (AM) |
| 0x3b790 | 59 | - | not checked |  |
| 0x3b98c | 40 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x3ba4a | 37 | - | not checked |  |
| 0x3bb08 | 69 | - | shared (render) | also reached from the render loop (AM) |
| 0x3bc9c | 81 | mech3d.c | checked earlier | X |
| 0x3be55 | 26 | mech3d.c | checked earlier | T |
| 0x3bebb | 23 | - | shared (render) | also reached from the render loop (AM) |
| 0x3bef2 | 24 | - | not checked |  |
| 0x3bf51 | 21 | - | shared (render) | also reached from the render loop (AM) |
| 0x3bfb2 | 194 | glr.c, glview.c, mech3d.c, rb_gl12_world.c | checked earlier | X |
| 0x3c31b | 10 | - | not checked |  |
| 0x3c33f | 40 | - | checked earlier | AH |
| 0x3f280 | 70 | glr.c, glr.h, glview.c | checked earlier | X, Z |
| 0x3f400 | 10 | - | not checked |  |
| 0x3f590 | 12 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x3f780 | 9 | - | not checked |  |
| 0x3fdd0 | 73 | glr.c, glr.h | checked earlier | AH, AL, Z |
| 0x40500 | 49 | msim.c | checked earlier | J_sim_impl.md, V |
| 0x40800 | 15 | - | not checked |  |
| 0x40840 | 48 | - | shared (render) | also reached from the render loop (AM) |
| 0x409b0 | 14 | sfx.c, sfx.h | shared (render) | also reached from the render loop (AM) |
| 0x409f0 | 27 | - | not checked |  |
| 0x40a60 | 9 | - | not checked |  |
| 0x40a70 | 156 | glview.c, sfx.c, sfx.h | shared (render+mission) | also reached from the render / mission loops |
| 0x41460 | 55 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x41690 | 39 | - | checked earlier | E_sim.md |
| 0x41740 | 12 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x41780 | 23 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x417e0 | 10 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x41840 | 12 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x418e0 | 90 | glview.c | checked earlier | Y |
| 0x41b70 | 22 | - | not checked |  |
| 0x42060 | 9 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x42630 | 156 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x42950 | 73 | glview.c | shared (render+mission) | also reached from the render / mission loops |
| 0x42da0 | 20 | - | shared (mission) | also reached from the node interpreter (AO) |
| 0x42e40 | 20 | - | checked earlier | J_sim_impl.md |
| 0x42e90 | 10 | - | not checked |  |
| 0x45d20 | 13 | glview.c | shared (render+mission) | also reached from the render / mission loops |
| 0x461a0 | 76 | - | checked earlier | E_sim.md |
| 0x497b0 | 20 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x49bc9 | 11 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x49c0d | 11 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x49c2f | 11 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4a7c0 | 23 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4a810 | 45 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4a990 | 17 | msim.c | checked (read) | servo set-up: T = chop(s x 182) (emulated: 0.2/0.3/0.6/0.8 s -> 36/54/109/145) |
| 0x4a9d0 | 21 | msim.c, msim.h | checked (emulator) | servo_step |
| 0x4aa30 | 19 | - | not checked |  |
| 0x4aa70 | 30 | - | shared (render) | also reached from the render loop (AM) |
| 0x4ab60 | 53 | - | checked earlier | E_sim.md |
| 0x4ac50 | 69 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4ad60 | 35 | - | checked earlier | E_sim.md |
| 0x4ae30 | 20 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4ae90 | 13 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4af60 | 13 | - | checked earlier | AE, AI |
| 0x4afa0 | 13 | - | shared (render) | also reached from the render loop (AM) |
| 0x4b220 | 27 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4b560 | 31 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4b600 | 48 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4b710 | 25 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4b920 | 40 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4bc20 | 113 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4bcf0 | 59 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4be2c | 25 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4c9e0 | 19 | - | shared (render) | also reached from the render loop (AM) |
| 0x4d5b0 | 19 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4d670 | 383 | - | FIXED | heat sink tiers: -100 or colder 250/350/225, Hard cold 67 | test: heat_sink_tiers (0x4dc8d..0x4dd75) |
| 0x4ecc0 | 29 | - | shared (render+mission) | also reached from the render / mission loops |
| 0x4f070 | 9 | - | not checked |  |
| 0x50d50 | 31 | msim.c | shared (render) | also reached from the render loop (AM) |
| 0x50e10 | 21 | fx.h | checked earlier | V |
| 0x50e50 | 10 | - | checked earlier | AH |
| 0x50e60 | 209 | glview.c | checked earlier | AH, J_sim_impl.md, V |
| 0x51410 | 38 | - | checked earlier | AH, T |
| 0x514b0 | 45 | - | not checked |  |
| 0x515d0 | 31 | - | not checked |  |
| 0x51660 | 26 | msim.c, msim.h | checked (read) | (prior AD) |
| 0x516b0 | 230 | combat.h, msim.c | checked (read) | (prior AD) |
| 0x51c22 | 39 | - | not checked |  |
| 0x51cd0 | 126 | msim.c | checked (read) | (prior AD) |
| 0x51f30 | 23 | - | checked earlier | AD |
| 0x51fb0 | 11 | - | not checked |  |
| 0x51fe2 | 13 | - | not checked |  |
| 0x52010 | 11 | msim.c | not checked |  |
| 0x52040 | 308 | fx.c, glview.c, msim.c, msim.h | checked earlier | E_sim.md, J_sim_impl.md, R, V |
| 0x52670 | 119 | combat.c, msim.c, msim.h | checked earlier | AD, E_sim.md, J_sim_impl.md, V |
| 0x52900 | 67 | msim.h | checked earlier | E_sim.md, J_sim_impl.md, V |
| 0x52a70 | 64 | - | checked earlier | AI, E_sim.md, J_sim_impl.md |
| 0x52b60 | 18 | - | not checked |  |
| 0x52ba0 | 21 | - | not checked |  |
| 0x52c00 | 48 | - | checked earlier | J_sim_impl.md |
| 0x52cd0 | 44 | msim.c | checked earlier | AI, E_sim.md, J_sim_impl.md |
| 0x52dd0 | 38 | - | not checked |  |
| 0x52ea0 | 10 | - | checked earlier | E_sim.md |
| 0x52ec0 | 74 | - | not checked |  |
| 0x53010 | 31 | - | not checked |  |
| 0x530d0 | 19 | - | not checked |  |
| 0x531a0 | 28 | - | not checked |  |
| 0x53200 | 237 | ai.c, glview.c | checked (read) | weapon state machine: read, matches combat_tick states (not exhaustively) |
| 0x53823 | 133 | combat.c, glview.c, msim.c | checked earlier | AD, AI, E_sim.md, J_sim_impl.md, R, V |
| 0x53af0 | 39 | combat.c, glview.c | checked earlier | E_sim.md, J_sim_impl.md, K |
| 0x53bb0 | 34 | combat.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x53c50 | 38 | combat.c, glview.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x53d00 | 28 | combat.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x53dc0 | 15 | - | checked earlier | AE |
| 0x53df0 | 29 | - | not checked |  |
| 0x53eb0 | 25 | - | not checked |  |
| 0x53f10 | 12 | - | not checked |  |
| 0x53f30 | 12 | - | not checked |  |
| 0x53f60 | 10 | - | not checked |  |
| 0x53f80 | 37 | - | not checked |  |
| 0x54030 | 37 | ai.c | checked earlier | E_sim.md |
| 0x54160 | 134 | msim.c | checked earlier | AI, E_sim.md, J_sim_impl.md |
| 0x544b0 | 32 | - | checked earlier | AI, J_sim_impl.md |
| 0x54560 | 9 | - | checked earlier | AH |
| 0x54570 | 25 | msim.c | checked earlier | AI, J_sim_impl.md |
| 0x545e0 | 22 | - | checked earlier | AH, AI, J_sim_impl.md |
| 0x54650 | 21 | - | not checked |  |
| 0x546d0 | 10 | - | not checked |  |
| 0x546e0 | 18 | - | not checked |  |
| 0x54730 | 95 | glview.c, msim.c, msim.h | checked earlier | AI, E_sim.md, J_sim_impl.md, R, V |
| 0x548d0 | 34 | combat.h, msim.c, msim.h, wtable.h | checked earlier | E_sim.md, J_sim_impl.md |
| 0x54943 | 81 | ai.c, combat.c, msim.c | checked earlier | E_sim.md, J_sim_impl.md |
| 0x54aa3 | 61 | combat.c, combat.h, msim.c | checked earlier | J_sim_impl.md |
| 0x54bb0 | 97 | msim.c, msim.h | checked earlier | E_sim.md, J_sim_impl.md |
| 0x54d2b | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x54d50 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x57978 | 62 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5a7c0 | 12 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5a7d8 | 63 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5a8ce | 17 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5db42 | 11 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5dc90 | 34 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5e080 | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5e090 | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5e360 | 48 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f510 | 52 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f610 | 56 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f720 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f7a0 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f930 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5f9c0 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fa50 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fbd0 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fc50 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fcd0 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fd50 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x5fdd0 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x60a80 | 52 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x60c80 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63633 | 22 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63782 | 37 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x637dc | 25 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63816 | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x6382e | 19 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63844 | 68 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x638f8 | 30 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x639c0 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x639db | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63c58 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63c87 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63c9c | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63ca8 | 33 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63d11 | 39 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63de1 | 29 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63e2b | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x63e46 | 39 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64032 | 17 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64059 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x6407a | 29 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x640d1 | 86 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x6420a | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x6421c | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64271 | 37 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64350 | 32 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64387 | 63 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x643f3 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x648dd | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x648f8 | 22 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64920 | 11 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64939 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x6497a | 85 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x64ab1 | 127 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x651f0 | 44 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x76911 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x773ea | 12 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77449 | 43 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7756e | 12 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x775d6 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77609 | 40 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7770b | 13 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77790 | 29 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77dad | 54 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77e36 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77e65 | 49 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77f32 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x77f61 | 50 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78012 | 46 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x782fb | 17 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7833d | 68 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78533 | 23 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7864a | 19 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78760 | 11 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78811 | 55 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78a48 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78b46 | 19 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78b97 | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78d40 | 29 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78d89 | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x78f66 | 60 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x796f7 | 74 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7a6aa | 20 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7aae6 | 32 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7abfa | 20 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7acff | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7ad10 | 23 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7aeff | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7af12 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7af38 | 67 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7b04e | 57 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7b27c | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7b413 | 13 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7d019 | 9 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7d01f | 9 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7d025 | 9 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7dd2a | 56 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7ddd2 | 101 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7dedd | 26 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7df1c | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e07d | 41 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e0f1 | 23 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e13f | 80 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e212 | 54 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e2c1 | 75 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e405 | 35 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e47c | 157 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e6fd | 95 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e830 | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e88b | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e8af | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e8d2 | 39 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7e933 | 67 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7ea0e | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7ea15 | 315 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7ee5e | 17 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7f1fc | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7f209 | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7f89b | 80 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7f9b2 | 9 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x7fd50 | 96 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x80ea0 | 37 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x80f10 | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x80f30 | 36 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x80ff0 | 19 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81020 | 19 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81060 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81090 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x810c0 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81140 | 15 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81160 | 22 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81220 | 20 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x812b0 | 18 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x812d0 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x815b0 | 11 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x815c0 | 18 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81630 | 103 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x818e0 | 29 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x81a10 | 71 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x85d29 | 28 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x85f23 | 13 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x85f46 | 13 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x85f59 | 51 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86001 | 24 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86058 | 24 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8608a | 43 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86101 | 45 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x861c0 | 20 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86203 | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86294 | 39 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x862fa | 137 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8651b | 55 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x865f4 | 21 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86629 | 59 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x866af | 82 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8679d | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x867fb | 26 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8683e | 70 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86924 | 170 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86c02 | 138 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86dfa | 18 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86e21 | 22 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86e44 | 16 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86e82 | 27 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x86eba | 11 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x87260 | 26 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x872b5 | 10 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8748c | 22 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x87511 | 38 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x875d4 | 12 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8760f | 60 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x876f4 | 14 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8771c | 144 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x879cf | 18 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8da95 | 39 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |
| 0x8db12 | 26 | - | runtime / driver | C runtime, DOS4GW, sound or video driver - not ported |

## 6. Round 11 (AQ): the leftovers - AI state handlers, turrets, world tasks, MASC, jump probes

Base main 47242bd, branch aq-work. DOSBox experiments use MW2.PRJ copies with NAVPs moved (a 10-line script outside the
repo; the record's byte sum fixed as render_audit.py's patch_prj) and the player's star from mkstar on both sides.

| GBL | what | status | port |
|---|---|---|---|
| 0x2eed0 / 0x2f050 / 0x4f4a0 | a unit's THNG nodes: the first turned by the twist, a turret's second by its pitch | FIXED | mech3d.c pitch_node / gun_pitch; twist_node falls back to THNG 0 |
| 0x21a80 / 0x21b60 | the rule pass in every state but 12 (DEFLT state 10: cond 7 within range a -> post) | FIXED | msim.c rules while powered down; a state entered wakes |
| 0x235e0 / 0x229e1 / 0x22a42 / 0x23dc0 | push / pop depth 1; a node change writes slot +0x13c | FIXED | counter = 1 on the assignment and the reaction; node change skipped |
| 0x22800 | state handler: 2 aim+fire / nav, 4 flee 0x400, 5 sweep + recon mark (0x23900), 6-8 sweep + nav, 1 / 9 / 10 throttle 0x333 | checked | 1 / 9 never entered (no rule); 0x23900 recon marking is mission code (AR) |
| 0x22990 | state 3: trigger flags off, 0x2c590 | checked | ai.c attack |
| 0x24a30 | patrol torso sweep | FIXED | ai.c patrol_sweep (simdiff patrol_sweep) |
| 0x22ef0 | state entry (power, fire delay, flags) | FIXED | next_fire on the reaction / assignment |
| 0x2c590 | manoeuvre dispatch | checked | 5 = 0x2dab0, 9 = 0x2d8c0, 12 = 0x2dc90 |
| 0x2dab0 | manoeuvre 5 (glide on the directional jets) | checked, same | |
| 0x2d8c0 | manoeuvre 9 (jump up) | FIXED | line probe, jets through phase 1 |
| 0x2dc90 | manoeuvre 12 | differs (not ported) | beyond 150 m GBL re-goals to a patrol nav every 0x5b0 ticks (0x236d0 / 0x23740 / 0x238b0) |
| 0x2cb80 | manoeuvre pick: non-unit target -> 0, jump pre-check probes | FIXED | ai.c choose_manoeuvre, msim.c jump_line_clear |
| 0x2dd50 / 0x2e450 | jump line probes | FIXED | probe_engine_ex reports the unit it stops at |
| 0x28298..0x28386 | MASC x 1.5 / roll only with a flag-0x10 key playing, jets off; T 0x5b once | FIXED | glview g_masc_key |
| 0x1b652 / 0x1b832 / 0x1bbc2 | spin / circling / path task clocks | FIXED | mech3d.c 182 |
| 0x1ad22 | task type 3 = the animation task (not lifts; lifts are type 5, 0x1bbc2) | checked (key flags) | anim.c |
| 0x2a590 / 0x2aee0 / 0x224c0 / 0x23220 / 0x22a90 / 0x21e40 | target test (+0xc0 horizontal, +0xc4 3D, +0xd8 elevation), pick, iterator, attacker | checked | pick by horizontal distance as the port |
| 0x2ec90 | weapon choice: +1.875 / +3.75 deg pitch for LRM 20 / SRM 6 | checked: overwritten | 0x2eb60 writes input +0 after it - no effect |

BRANSCN1 (Branson, the carrier deck), DOSBox vs port, player from mkstar:
- DOS: first hits ~9 s, destroyed ~12-13 s (k0); turned starts k1 / k2 dead by 20 s, k3 by 28 s. Port before: never hit
  in 40 s (BRANENT4 aimed along its base heading; BRANENT4 / BRANENT2 busy with the DUMBTURT units). After: k0 first hit
  8.2 s, destroyed 15.8 s; k1 11.5 s, k2 9.8 s, k3 19.5 s.
- DUMBTURT units (BRANUST1-4, the player's side, all nodes DUMMY): ASSUMED from three DOSBox runs with NAVPs moved
  (msim.c dummy_unit): no AI, never picked, but counted for condition 7's range (BRANENT3 347 m ahead wakes for one).

Not changed / open:
- PETE: the port lands as DOS does (altimeter 165 m in both at ~10-12 s); the sea floor is not drawn - rendering (AP).
- ASAH k2: DOS is not repeatable across poses / sizes (k2_640 dead by 20 s, k2_1024 alive at 28 s); the port's k2 dies
  at 25.1 s, before and after this round.
- Manoeuvre 12's patrol re-goal (above). The contact rule of 0x2cb80 also tests the reverse input, +0x194 and the
  current manoeuvre (port: contact alone). States 1 / 9 throttle 0x333 (no rule enters them).
- Collision geometry 0x1c9f0 / 0x1ce00 / 0x20e70 / 0x213b0: not re-read this round (E_sim.md: 1:1 with the 3D-card
  DLL the port follows).
