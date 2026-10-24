# Changelog

## Unreleased (work in progress)

- **`gbl`** (`make gbl`): MechWarrior 2: Ghost Bear's Legacy as its own program, as GBL.EXE was. The shell is rebuilt
  from GBL's MW2SHELL.EXE:
  - the title and the Esc menu: Combat Variables (with TRANSITIONS), Cockpit Controls, Hall of Honor, The Keshik;
  - the Sibko roster, the three Ghost Bear halls, the Bloodname roll and the developer easter egg;
  - the Mission Computer, launch and debriefing, with GBL's trials and ranks;
  - the Combat Simulator, and the Mech Lab with Inner Sphere 'Mechs and the four new weapons.

  The game folder is `GBL-game/`, with a copy of the original GBL disc (FOLDER_SETUP.md), and gbl has its own settings
  file. If no game folder is found, it asks for one.
- **Simulation:** GBL's rules throughout:
  - the 35-weapon table (Inferno SRM / Streak, Torpedo, Arrow IV, rebalanced LB-X / Ultra ACs);
  - the AMS interceptor and the NARC lock;
  - 14 weapons per 'Mech and 255 projectiles;
  - the boss pilots of the Bloodright trials;
  - lifts and moving platforms, and invisible armed turrets;
  - the 22 new scenes, read from their loose `<NAME>.BWD` files.
- **Enemy dodging, as in DOS:** enemies dodge only the computer-controlled 'Mechs' missiles and PPCs, never yours. This
  was verified in DOSBox against the original MW2.EXE. The dodge direction also follows DOS.
- **Looks:** DOS (the default); ENHANCED (GBL's own models and textures in true colour, plus the Titanium fog);
  TITANIUM (the Titanium Edition's Direct3D look). The in-game videos have their own look: original, smooth, scanlines,
  CRT or curved CRT, and fill a widescreen display if you like (Combat Variables, FMV 4:3 / FILL / STRETCH).
- **HUD:** GBL's layout, with the 14-weapon list in two 7-row columns and the target viewer bottom left.
- **Sound:** the disc's CD audio as the music, the briefing and debriefing voices, and the menu clips' own sound. There
  is no MIDI, as in the original.
- **No CD check.**
