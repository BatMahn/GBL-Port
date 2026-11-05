# Changelog

## Unreleased (work in progress)

- **New renderer: SDL3's GPU API instead of OpenGL.** The game, menus included, now draws with Direct3D 12 or Vulkan on
  Windows (your choice in Combat Variables' GPU; the other one is tried if it can't start), Vulkan on Linux and Metal on
  macOS. OpenGL is gone, and with it Apple's deprecated OpenGL layer and its problems on the Mac. All three looks
  (DOS, ENHANCED, TITANIUM) draw as before: the DOS look matches the old renderer to a few pixels a frame.
  - **DOS look on the Mac:** the exact DOS polygon fill now runs on macOS too (it was switched off there because
    Apple's OpenGL got it wrong).
  - **New minimum hardware:** Direct3D 12 (feature level 11_0), Vulkan 1.0 or Metal (macOS 10.14). See README's
    System requirements. If none starts, the game tells you why.
  - **Windows fixes** (found running the Windows build under Wine): missions start from the menus again (the program's
    own path was doubled); the menus no longer crash at start when Direct3D 12 can't draw them and Vulkan takes over;
    a Direct3D 12 driver that can't run the game's shaders now hands over to Vulkan instead of stopping the game.

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
- **Projectiles move once a frame, as in the original:** each shot advances by the frame's time in one step and then
  steers. A locked torpedo's curve now follows the original's at the same frame rate (checked in the underwater
  mission, PETE: torpedoes climb to the submarines and home in on the 'Mechs).
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
- **DOS-look HUD to the pixel** (checked in DOSBox against GBL's MW2.EXE at 1024x768 and 640x480): the nav marker and
  target bracket use the original's integer projection, radar blips and the radar's cone and edge use its own top-down
  view, and the message bar is clipped as DOS clips it.
- **Dropping in at the start:** a 'Mech that starts above the ground drops straight down during the start-up, as in the
  original. It used to slide a little across the slope it landed on, which moved the cockpit view and the HUD markers.
- **Powering up and shutting down:** lancemates and enemies stay where they landed until they have powered up, with
  their torsos centred, and the start-up takes 6 to 8 seconds as in the original. A shut-down 'Mech coasts to a stop
  without bumping into anything, the shutdown order powers a lancemate down, and the next order starts it up again.
- **DOS look: cockpit struts as in the original.** The struts of cockpits that reach close to the eye (the Timber Wolf's)
  no longer show thin lines of sky along their seams, and lighting uses the original's own intensity, which can be one
  shade darker.
- **DOS look: perspective textures as in the original.** Textured surfaces drawn in perspective (the scree slopes of
  JESS, dropship panels) now cover the same pixels and pick the same texels as the original, in its 16-pixel steps.
- **TITANIUM: SACH's waterfall and river.** Textured surfaces with no bitmap in the mission's texture set are no longer
  drawn as dark flat colours: Titanium leaves them out. In SACH the river shows the water ground beneath it and the
  waterfall its cliff face, instead of dark green slabs.
- **Torso tilt as in the original:** tilting the torso now turns the view by half as much, with the cockpit frame staying
  put and the reticle moving to where the guns point, and it reaches 60 degrees each way. With the torso twisted too, the
  view rolls a little, as the original's does.
- **TITANIUM look: draw order and back faces as Titanium.** Polygons facing away are not drawn and the rest are painted
  farthest first, so a turret against a cliff shows whole there too. Gouraud-coloured polygons are no longer fogged,
  as in Titanium.
- **Turrets on the hills in the DOS look:** the original paints its polygons farthest first, without a depth buffer, and the
  port now orders them the same way. A turret standing against or partly inside a cliff (Mannedorf's, in the first
  mission) shows whole, as in the original, instead of half hidden in the rock.
- **Thin parts in the DOS look:** flat-coloured and most textured polygons now cover the same pixels as the original's
  fill, as the shaded ones already did, so slivers such as the turrets' gun barrels stay solid instead of breaking into
  dots (not on macOS, where the exact DOS fill is off).
- **Heat, throttle and jets bars** ease toward their values as in the original, and the dH/dT bar reads the original's
  net heat.
- **Targets, radar and missiles aim at a building's or vehicle's centre**, not its model origin.
- **Enemy fire, as in GBL's MW2.EXE:** SRMs and Streaks now fly their full 497 m (they burned out at 380 m, so an
  enemy firing them from further off never reached you) and LRMs their full 1001 m; every weapon's reload and volley
  timing is GBL's own. A hit on the centre torso spreads over the left, centre and right torsos, as the original's damage
  display shows. Enemies aim and lock on from 'Mech centre to 'Mech centre, and a missile lock takes GBL's 2.0 s.
  An enemy 'Mech whose ammunition explodes is out of the fight at once, as in GBL.
- **Missile reticle while reloading:** the plain ring while the selected weapon fires its volley or reloads, as in DOS.
- **Cockpit Controls:** you can type the configuration's name (Backspace, Enter / Esc to finish).
- **Joystick / mouse menu files:** the GIDDI\<driver>.STD files are read as the original reads them.
- **SDL3:** the port now runs on SDL3 (3.2.12 or newer) instead of SDL2, with the same OpenGL renderer; menus, missions,
  sound and controls work as before. The macOS app, the Windows zip and the .deb bring their own SDL3.
