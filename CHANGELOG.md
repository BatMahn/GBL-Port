# Changelog

## 1.0.0 (2026-11-10)

The first full release: rendering, simulation, AI, missions, HUD, sound and the shell checked systematically against
the original DOS game's code (running its routines under emulation) and against DOSBox recordings.

- **Rendering, last checks against the original:**
  - **Standing height on sloped floors:** your 'Mech no longer stands 20-odd cm sunk into some sloped floors (the start
    floors of the GUI missions and others): the height on polygon ground now comes from the floor's corners as the
    original works it out, not from the slightly tilted surface direction stored with the model. The cockpit view in
    those missions now sits where the original's does.
  - **Underwater mission's sea floor (PETE):** the light under water comes from a light that drifts above you, as in
    the original, so the sea floor and the rocks are lit and visible; before they were lit from below and came out the
    colour of the water.
  - When an explosion briefly takes over a scene's light in a mission with no ambient light, the shading now uses the
    original's fallback level instead of going darker than black.
  - Tracer streaks that pass right by your cockpit or run far off the screen's edge are drawn the way the original
    draws them (a thinner, stepped streak) instead of as a full line; and a sliver of a polygon that fits in a single row
    of pixels is no longer drawn, as in the original.

- **Enemy AI and collisions, last checks against the original:**
  - A shot or a probe that meets a wall, a building face or the ground exactly on a corner or an edge now counts as a
    hit, as in the original (before it could slip through the outline of a face).
  - Enemy 'Mechs that bump into something back away only when the original would: not while already reversing,
    and not straight after a jump (landing against a wall or another 'Mech no longer sends them backing off).
  - **Enemy 'Mechs sidestep the original's way:** their short sidestep move now heads for a point about 100 m to
    their own side (or ahead and to the side when you are farther than 40 m), not to a point circling round you, and
    their flanking and sidestep moves steer by the original's waypoints - including the jump-jet turn, which now
    looks at the point they are heading for. Their circling attack (unused by the missions) circles round its target
    and returns to it from beyond 150 m as in the original.
  - Enemy 'Mechs spread round the 'Mech they attack as in the original: each target keeps its own count of the
    flanking positions taken (before, every target shared the counts of the attackers round you), so lancemates and
    enemies fighting each other no longer crowd or skip positions because of what happens round you.
  - Enemy 'Mechs set their speed in turns as the original does (from the turn they were already making), and back
    away at full speed even when running hot, as in the original (before they backed off at half speed above 65
    heat).
  - An enemy 'Mech out of ammunition that charges you self-destructs within 20 m measured in 3D, as in the original -
    not when it passes 20 m beside you on a ledge above or below.
  - Resting units hold the original's throttle setting while powered down (no visible change: they stay put until
    they power up, as before).
- **Sound, checked against recordings of the original:**
  - **Distant sounds carry as in the original:** weapon fire, explosions, footsteps and power-ups far away are heard at
    full strength (arriving later with distance, at the speed of sound) instead of fading out by 500 m - the
    original's fade only begins kilometres away. Ambient object loops (machinery, fans) still fade by 500 m, as there.
    Branson and Zack keep their 10 m hearing limit.
  - **Stereo placement as in the original:** a distant sound's left / right balance is set when it arrives, from where
    you are facing then (turning while it travels moves it), and follows your view while it plays.
  - **Ambient loops no longer crowd out other sounds:** machinery and other object loops have their own channels, as in
    the original, so with several of them running weapon fire and footsteps still get all eight effect channels.
  - **Your own power-up sound** at the start of a mission is heard at once and at full strength: on the mission's first
    frame the port listened from the start height below your 'Mech, so it came late and quieter, and in missions that
    start high up (Branson) not at all.
- **HUD colours, checked against the original's code and screenshots:**
  - **Target display:** a selected nav point's name turns from bright to darker yellow once your side has been there,
    as in the original; a targeted building's name is coloured by its side (green for your side's, red for the
    enemy's, blue for neutral ones) instead of always red.
  - **Damage display:** a location turns yellow and red by how much of its armour and internal structure together is
    left, as in the original - it no longer goes red as soon as the armour is nearly gone while the structure is
    intact, and heavily armoured locations stay blue until they are down to 75 points. The same grading colours a
    target's locations in the target display and image enhancement.
- **Enemy turrets and AI, checked further against the original:**
  - **Turrets now turn and aim their guns:** the big carrier and base turrets never visibly rotated, and their shots
    left along the turret's base direction - on Branson's carrier deck they landed 40 m away from you. Their body now
    turns and their guns tilt as in the original, and they hit what they aim at (tanks' turrets turn too).
  - **Resting enemies wake up when you come near:** units parked "at rest" by their mission (powered down) now notice
    an enemy within their sighting range and come online to attack, as in the original; before they slept until
    their mission script woke them (Branson's deck turrets let you stand under them for 14 s). A unit sent at a
    target keeps that target when its script moves on.
  - **Branson's carrier deck is as deadly as in the original:** the invisible defence points on the deck no longer
    distract the enemy turrets - the turrets fire on you as you ride the lift up (first hits about 8-9 s in, as in
    the original), including the turret far ahead that the defence points wake.
  - **Moving scenery keeps the original's pace:** spinning and circling objects (radar dishes, fans, patrolling
    aircraft) and lifts ran on the 3D-card edition's clock, about half a percent slow; they now use the DOS timing.
  - **MASC as in the original:** the speed boost (and its malfunction risk) works only while your 'Mech is striding
    on the ground - not standing still or with the jump jets firing - instead of whenever MASC was switched on.
  - Tanks and other vehicles on patrol no longer sweep their turrets from side to side (only 'Mechs scan with their
    torso in the original); a unit turning on an attacker or sent at a target by its lance leader takes its first
    shot after the original's short random delay.
  - Enemy 'Mechs jump up to a target above them only when the original would - when they can't see it from where
    they stand but could from its height - keep the jets on for a second as they clear the edge, and use their
    plain attack pattern against buildings and nav points.
- **HUD, missions and menus, third pass against the original's code:**
  - **Sound on Branson and Zack:** these two missions limit what you can hear to things within 10 m of you, as the original
    does - distant weapon fire, explosions and footsteps are silent there; your own weapons in the
    cockpit, radio and cockpit sounds are unaffected.
  - **Mech Lab masses:** every stock design's mass panel now matches the original's own calculation (checked by running
    the original's code on all 99 stock designs): the Rifleman IIC's second configuration shows 24 single heat sinks and
    64.75 T used (it showed 12 double sinks), and the Elemental's engine rating and heat sinks read as in the original.
  - **Hall of Honor and debriefing order:** pilots with equal rank, honor and missions, and objectives finished at the
    same time (or not at all), are listed in the original's order - the port now sorts exactly as the original's library
    routine does (checked against it on thousands of lists). With many fresh pilots this decides who appears in the Hall.
  - **Heat bar label:** always reads "Heat", as in the original - the red "Shutdown..." / "Overridden" labels came from
    the 3D-card edition and do not exist in this game. Jettisoning ammunition says "Ammo for current weapon
    jettisonned." exactly as the original spells it.
- **HUD, mission messages and menus checked against the original's code** (second pass):
  - **"Enemy power-up detected"** now comes the moment a sleeping or shut-down enemy starts powering up, with its
    start-up sound, as in the original - not 6-8 seconds later when it is already up and moving.
  - **Starting up:** the radar's ring and view lines are drawn thin and in the radar's own colours while the cockpit
    powers up, as in the original (they were thicker and paler until the HUD came on).
  - **Altitude tape:** flying or standing more than 200 m up now shows only the ">200" marker, as in the original,
    instead of the tape's top end.
  - **MASC:** the HUD shows "MASC" next to the speed while MASC is engaged, as the original does.
  - **Nav points in the target display:** a nav point you have already reached shows its "reached" emblem (the
    diamond), as in the original.
  - **Message lines:** when both lines are busy, a new message replaces the one that would stay up longer (the
    original's rule) - e.g. toggling the autopilot no longer wipes "MASC engaged."; short messages stay up for the
    original's 2 seconds.
  - **Mission objectives (F12)** sit where the original puts them (a few pixels lower, more at 1024x768) and use the
    original's blues.
- **Simulation checked against the original game's code** (an automated comparison that runs the original program's
  own routines on thousands of inputs, tools/audit): fixes where the port still followed the 3D-card edition or its own
  guesses -
  - **Heat:** on frozen worlds (-100 degrees and colder: Branson, Claimant, Pete, Zack) heat sinks now dissipate 2.5x
    the normal rate, as in the original (the port gave 2x); overriding a shutdown at full heat now risks an ammunition
    explosion about once in 1.5 minutes, not every few seconds.
  - **Turning:** a 'Mech's legs now take a moment to start and stop turning (it runs on a few degrees after you let go),
    as in the original; computer-run units no longer spin on the spot, a 'Mech with a leg shot off can't pivot standing
    still, and nothing steers its legs in mid-air without jump jets.
  - **Enemy behaviour:** tanks, hovercraft and aircraft use the original's single straight-at-you attack instead of the
    'Mechs' flanking and dodging; Elementals use their own jumping table; skilled pilots' turning jumps last until they
    land; manoeuvre timings as the original's; turrets aim at the original's slower rate and turn the short way round
    when you circle behind them.
  - **Collisions:** ramming and landing on another 'Mech do the original's damage (landing on a heavier 'Mech's head
    twice what it was).
- **DOS look, checked against the original frame by frame** (a new automated comparison of every GBL mission against
  DOSBox, tools/audit/render_audit.py):
  - **Mountains no longer spill over the ground at their foot:** standing close to a mountain, one of its faces could
    cover the ground in front of you in a big dark wedge. The original cuts such polygons to the screen's range before
    filling them; the port now does the same.
  - **Water and ground faces passing under you no longer paint over cliffs:** looking up in a canyon (SACH), a
    stream's surface running back under the 'Mech could be painted across the canyon wall in blue. Cutting such a
    face where it passes the eye overflowed a sum the original works out in 64 bits; the port now does the same.
  - **No more shading stripes at a mountain's foot:** where a face reaching past the screen's edge is cut, its shade
    and texture at the cut are now worked out from the 3D position as the original does, not along the screen.
  - **Autocannon and machine-gun tracers are visible:** the bullet's two dark tracer streaks were not drawn at all; they
    are now drawn pixel for pixel as the original's line routine draws them.
  - **The haze above the horizon** is drawn row for row as the original draws it (its height, its stepped colours and
    their checkerboard - at 640x480 the checkerboard was the wrong way round), and missions without a haze setting
    (ASAH, FOST, JEEH, NEWC, TIM_) have the original's default haze instead of none.
  - **'Mechs keep their detail farther away at 1024x768 and above:** the distances where a 'Mech switches to a simpler
    model now grow with the screen width as in the original (115 / 272 / 720 m at 1024 wide, not 640's 72 / 170 / 450 m).

- **Legacy Windows build for XP-era PCs:** a second Windows zip (`windows-x86-legacy`): 32-bit, Windows XP SP3 and
  newer, a Pentium 4 / Athlon 64 (SSE2) and an OpenGL 1.2 card (GeForce 2, Radeon 7000, GMA 900, Voodoo 3 and newer).
  The same game and all three looks, drawn by a new OpenGL 1.2 fixed-function renderer (docs/RENDERER_GL12.md) on SDL2.
  Against the normal renderer: the DOS look the same to a few pixels a frame, ENHANCED and TITANIUM to a level or two.
  No multisampling; Combat Variables shows GPU OPENGL. Not yet tested on real XP hardware.
  - **Same missions on every build:** the simulation's atan2f is now the same function everywhere (the 32-bit C
    libraries' rounded differently, and one mission played out differently in a 32-bit build); a 32-bit build plays all
    86 missions exactly as the 64-bit one.

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
- **No gap at the horizon when the view rolls.** With the torso twisted and tilted the view rolls; the sky (and in the
  DOS look the horizon line) now rolls with the ground instead of staying level and leaving a sliver of black between
  them. All looks, both renderers.
- **BASS: the snow falls round you again.** The falling snow followed the map's origin instead of the player, so on most
  of the map it was far away; it now moves with you as in the original, at the original's height.
- **The sky object behind everything:** JESS's planets and the stars of BRAN and ZACK are painted before the world, as
  in the original, in the DOS, ENHANCED and TITANIUM looks; the stars (single points) now show in the DOS and ENHANCED
  looks as the original's one-pixel stars.
- **DOS look: surfaces at the same distance overlap as in the original.** Where two surfaces meet at exactly the same
  distance - a wall and the cliff behind it seen straight down the JESS hangar, a mountain's facets - the original's
  sort decides which one is painted on top, and the port now builds the original's list each frame and sorts it the same
  way, instead of always keeping the first one drawn. The mountains seen from the JESS plain now show every facet as the
  original does; where 'Mechs are in view the result depends on where they stand, as in the original.
- **DOS look: close walls no longer vanish at high resolutions.** Polygons nearer than the original's near plane are
  clipped there as the original does, and the fill's arithmetic no longer overflows in large windows - the JESS hangar
  walls stopped coming and going as you walk in.
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
