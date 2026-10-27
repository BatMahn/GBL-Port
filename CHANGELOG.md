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
- **Weapons and effects:** GBL's new projectiles are drawn in all three looks: the Torpedo, Arrow IV, NARC and the
  Flamer. The burning effect follows the 'Mech it hit. Each mission uses its own projectile and effect sets, with their
  own blast sizes, and shots start ahead of the 'Mech as in the original. Torpedo guidance matches it too.
- **Aircraft and hovercraft fly:** fighters, helicopters, submarines and hovercraft hold their height as in GBL. The
  JumpShip fighters in the last mission now attack the ship.
- **Wrecks:** a unit that runs into a wreck blows it apart, as in the original, so escorts no longer get stuck (MARO).
- **The BRAN lift** carries you up into the station as in the original; the mission no longer fails at the start.
- **Cockpit Controls:** all twelve of GBL's control drivers, written to INPUT.MAP exactly as the original shell does,
  and a gamepad option (Xbox / PlayStation / Switch controllers).
- **DOS look matches the original more closely** (checked in DOSBox against GBL's own MW2.EXE at 1024x768 and 640x480):
  - the cockpit struts are pixel-identical;
  - distance fog is applied per polygon;
  - your eye sits at the original's height;
  - the HUD sits where the original puts it. This covers the radar circle and cone, the compass, the altitude ladder,
    the reticle, the weapon list's frame, the bars, the target viewer and the message line.
  - nav arrows are red, and the speed turns blue while reversing;
  - a red damage flash is skipped while a palette fade is running.
- **TITANIUM look:** animated textures (fires, smoke, flags) step as the Titanium engine does.
- **Frame rate on macOS:** the HUD no longer stalls the GPU, and the DOS look's extra shader work is left out of the
  TITANIUM look. Start with `MW2_PERF=1` to print the frame rate and where each frame's time goes; `MW2_PERF=2` also
  times the GPU's world draw.
- **Controls:** GAMEKEY.MAP and INPUT.MAP are read from your install folder (or the disc copy) in any letter case,
  exactly as GBL reads them, so edited key files are no longer ignored. The leftover debug keys are gone from missions.
- **Gameplay, as in the original (each checked against GBL's MW2.EXE):**
  - the external camera (C): an orbit camera you steer, also on the death camera;
  - each radar mode keeps its own zoom; `/` recentres torso and view;
  - nav points are marked reached only the original's ways; the AI and autopilot steer in whole degrees;
  - hit tests use the detail level a 'Mech is drawn at, and 'Mech-to-'Mech contact slides as in DOS;
  - units the mission marks as untargetable can't be targeted (e.g. ANDY's Ravens);
  - the overheat alarm follows GBL's heat rules; looping world sounds (wind, machinery) start as in the original;
  - dropships and gunships are drawn at their flying height.
