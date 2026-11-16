# Mission data audit (tools/audit_missions.c)

`make audit_missions`, then

    MW2_INSTALL_DIR=<GBL-game>/install ./audit_missions <GBL-game>/install/MW2.PRJ [-s seconds] [SCENE ...]

Every `*SCN1` scene of the PRJ is loaded headless (the simulation without drawing: world3d_actors, msim_init, the world's
objects), checked at the start, run 3 s (`-s`) and half a second more, and checked again. The shell's hand-off files
(USERSTAR at the mission's BRF2 SDSC star size, EN01..05STAR, INSTMAP1) are written fresh in a temporary directory - the
objective sweep's fixed lance - so the Instant Action scenes have their enemy stars. One `FIND scene kind ...` line per
finding, `note:` lines for what is expected, `RESULT scene findings n` per scene. The checks:

| kind | what |
| --- | --- |
| BOUNDARY | the player or a unit, after settling, outside every boundary sphere: NAVP type byte (+27) 0x20 or 0x22, radius (u16 +24) x 100 cm about the nav's point, in 3D as the leave nodes test it. GBL MW2.EXE's NAVP handler (0x4f530, the NAVP branch at 0x50450) keeps the radius x 100 (nav +0x10) and never reads the type byte: it is the mission editor's (0x20 the LVE leave records, 0x22 some start navs and SIOULVE2, 0x23 shown navs, 0x21 navs) |
| LEADER | a unit starting more than 1 km from its leader (slot 0 of its table, in its own record) |
| GROUND | a unit more than 2 m above or below the floor under it after settling and not falling (hovercraft, aircraft and dropships - classes 2, 5, 8 - left out); one still falling is a `note:` (a drop-in) |
| OVERLAP | two units (the player too) within 3 m after settling |
| THING | a GT thing's intact object hidden at the start, or its destroyed variant shown (wrecks chained to another thing's destroyed variant and GT 0x8000 debris nodes left out) |
| ORIGIN | a drawn world object within 1 m of the map origin (x / z) after settling while most of its record lies over 500 m away; a ground piece over 500 m across about the origin and a hidden destroyed variant (placed by the swap) are left out |
| NAV | a nav point the player can select (NAVP +16 set, +22 0) outside every boundary |
| CRASH | the scene's own process died |

Debug hooks: `AUDIT_GT=<record>` lists the record's GT chunks and their objects' state, `AUDIT_NEAR=x,z,r` the world
objects within r cm of (x, z).

## Results (2026-11-16; branch aw-work)

Run over all 72 scenes of `/home/claude/GBL-game/install/MW2.PRJ` (the 23 GBL scenes, the base game's campaign, trial,
training and Instant Action scenes, the nine network arenas) and `/tmp/sw_game` (a second install): the same 33 findings
in both, no crash. No THING, LEADER or NAV finding: the destroyed variants, formations and MTBL tables are read as the
engine reads them since the commit before this round (CHANGELOG 1.0.2). None of the 33 is a fault of the port; each is
the mission data as the original has it:

- **BOUNDARY (23)** - all units, never the player, and all arriving or placed on purpose outside the leave spheres, which
  only the player's leave nodes test:
  - dropships and gunships flying in: BROW (BROWEND1), GOLD (GOLDEND1), PUCE (PUCEFRS1 / 2, 400 km out on the z axis),
    SABL (SABLFRS1), UMBE (the six UMBEENH1 / 3 / 4 gunships and UMBEEND1, 0.9 - 3.3 km out);
  - enemy stars that start outside and walk in: FUCH (FUCHENS3, 380 / 490 m out), and the Instant Action arenas' third
    star - GOUD and PROV EN03STAR (300 - 530 m) and BRO2 EN01STAR (75 - 220 m, on a 300 m high ridge: the spheres are 3D).
- **OVERLAP (8)** - start points shared by two groups in the data: COLB (EN01 / EN05STAR) and SWIS (EN03 / EN04STAR) put two
  Instant Action stars on one nav (the objective sweep notes the same); TEAL's TEALENS4 has four members and the stock
  "star" formation (FORMATNS) only two follower slots, so the fourth takes the zero slot 3 on its leader; APAC is a base
  game Instant Action arena that GBL's shell does not offer (its IA list: jack ched edam prov goud colb goat whiz swis,
  docs/gbl/E_sim.md 3.1) - its EN00STAR (the player's star there) is not one of the shell's hand-off files, so the
  audit's player stands on EN01STAR's start nav.
- **GROUND (1)** - GREY: the dropship's turrets (GREYEND1, TURRETD, class 3) stand on the dropship 17 m up, a unit and not
  a world floor.
- **ORIGIN (1)** - GOLD: the shield dome TL1SHIEL (GOLDARE8 object 3) is the shield generator's child (object 2 at
  -95000, 0, 62500) with the offset (95000, 0, -62500), which puts it on the map's origin, 1.1 km from the generator.
  The original draws it there too: DOSBox GOLDSCN1 with the start nav moved to (0, 0, -45000) heading north shows the
  blue dome ahead at the origin, as the port does (79.8 % pixel-equal below the message bar, the rest the cockpit damage
  and the fight). Left as it is.

40 `note:` lines: units still falling after 3 s, dropped from up high on purpose - the network arenas' stars (COLL 8,
HUSK 10, SIAM 10), the trials' second stars (IREN 3, KIM_ 2), the drops of BRO2 (2), MAR2 (4) and RUSTUSR2's
Hellbringer (RUST, 269 m up); they land within seconds (COLL: 175 m above the floor at 1 s, 136 m at 3 s, 3.5 m at 6 s).
