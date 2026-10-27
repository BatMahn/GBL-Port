# MechWarrior 2: Ghost Bear's Legacy (DOS, 1995) - 64-bit port

A native 64-bit reimplementation of **MechWarrior 2: Ghost Bear's Legacy**, the 1995 expansion, rebuilt from the
original disc's `GBL\MW2SHELL.EXE` and `GBL\MW2.EXE`. Portable C99 with SDL2 and OpenGL 3.3; builds on macOS, Linux
and Windows. You need your own copy of the game (see "Setting up the game folder").

As on DOS, Ghost Bear's Legacy is its own program: `gbl`.

## Status (2026-10-21)
Playable from start to finish. Tagged releases (`v*`) are built by GitHub Actions: a macOS app (Apple Silicon), a
Debian / Ubuntu package, a Fedora RPM and a Windows zip. Or build from source (below).

- **Shell:** a new shell, written from GBL's own MW2SHELL.EXE. It covers:
  - the Ghost Bear title and the Esc menu, with Combat Variables, Cockpit Controls, Hall of Honor and The Keshik
    (credits);
  - the Sibko roster, the three Ghost Bear halls and the Bloodname roll;
  - the 22-mission campaign: the Mission Computer with briefings, intelligence videos and the MECH PLANT star, then the
    debriefing with GBL's scoring, trials and ranks;
  - the Combat Simulator, and the Mech Lab with GBL's mech table and its new weapons.
- **Simulation:** every GBL mission, including the 22 new scenes, with GBL's simulation rules. Those include:
  - lift ground and the turrets;
  - the new weapons (Inferno SRM-2, Torpedo, Inferno SSRM-2, Arrow IV) and the AMS;
  - the boss pilots and the new effects;
  - GBL's own HUD layout (the 14-weapon list in two columns);
  - enemies dodging only the computer-controlled 'Mechs' missiles and PPCs, never yours, as in DOS.
- **Look:** chosen in Combat Variables (RENDERER):
  - **DOS** (the default): the original software renderer.
  - **ENHANCED:** GBL's own models and textures in true colour and filtered, plus each mission's fog from the Titanium
    Edition.
  - **TITANIUM:** the Titanium Edition's Direct3D look, with its models, textures, sky and particles.

  ENHANCED and TITANIUM need three files from the Titanium disc. The in-game videos have their own look setting:
  original, smooth, scanlines, CRT or curved CRT.
- **Sound:** the original's effects and voices (briefings and debriefings included), and the disc's CD audio as the
  mission music from your own rips.
- **No CD check:** the original looked for a hidden file on the GBL disc at start-up. The port never asks for the disc.

The DOS game is the reference for all behaviour: AI, simulation values, mission logic and music. The Titanium Edition
(1998) contributes looks only. Its AI changes and its shortened soundtrack are not used.

How the original works is documented in [docs/GBL_SHELL.md](docs/GBL_SHELL.md) and the specs in
[docs/gbl/](docs/gbl/); the simulation engine's records are in [docs/engine/](docs/engine/). Each value is traced from
the binaries or measured in DOSBox running the original. See [CHANGELOG.md](CHANGELOG.md) for what changed and
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) for what is still open.

## Build
Needs a C99 compiler, make, SDL2 (development files) and OpenGL 3.3.

    macOS:                  xcode-select --install; brew install sdl2
    Linux (Debian/Ubuntu):  sudo apt install build-essential libsdl2-dev libgl-dev

    make gbl                # the game: menus + missions in one program
    sh scripts/macos_app.sh                  # macOS: "Ghost Bear's Legacy.app", with SDL2 bundled inside
    sh packaging/linux/build_deb.sh 0.1.0    # Debian / Ubuntu: dist/gblport_0.1.0_amd64.deb

Windows (64-bit, not yet tested): MSYS2's MINGW64 shell (`pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2`),
then `make gbl`. Or cross-compile from Linux with the SDL2 MinGW development package:

    make gbl WINDOWS=1 CC=x86_64-w64-mingw32-gcc SDL2_CONFIG=/path/to/SDL2-x.y.z/x86_64-w64-mingw32/bin/sdl2-config

Put `SDL2.dll` beside `gbl.exe`. Run `make clean` when you switch between a Windows build and a native build.

The macOS app is only ad-hoc signed. A downloaded copy needs `xattr -dr com.apple.quarantine "Ghost Bear's Legacy.app"`
once (or System Settings > Privacy & Security > Open Anyway). Move it out of Downloads first: run from there, macOS
uses a temporary copy that can't see a game folder next to it.

## Setting up the game folder
`gbl` needs its own folder, `GBL-game`, built from **your original Ghost Bear's Legacy CD**: the 1995 hybrid DOS /
Windows 95 disc, which carries the whole game. None of the game's files are part of this repository.

    GBL-game/
      cd/         REQUIRED - the whole GBL disc, copied as it is (GBL\, SMK\, SND\, GIDDI\ ...)
      install/    REQUIRED, may start empty - the port writes pilots, settings and the mission handoff here
      music/      optional - the disc's CD audio, Track02..Track21 (FLAC, MP3 or WAV)
      titanium/   optional - MW2.PRJ, SKYGND.PAR, particle.PID from the Titanium disc's INSTALL\16BIT\
                  (for the ENHANCED and TITANIUM looks)

**See [FOLDER_SETUP.md](FOLDER_SETUP.md) for the details:** copying the disc or a BIN/CUE image, ripping the
music and the common mistakes.

## Playing

    ./gbl /path/to/GBL-game      # the first time; afterwards just ./gbl

You don't have to give the folder each time. Without one, `gbl` looks in this order:

1. the current folder;
2. next to itself;
3. one level up (`GBL-game`);
4. `~/GBL-game`.

If it finds none, it asks for the folder with the system's folder picker. The folder is remembered in the settings
file:

    Linux   ~/.config/gblport/gblport.cfg        macOS   ~/Library/Application Support/gblport/gblport.cfg
    Windows %APPDATA%\gblport\gblport.cfg

    game=/path/to/GBL-game
    resolution=desktop        # desktop (native fullscreen) | window | 2560x1440 ...
    aa=4                      # anti-aliasing: 0 2 4 8
    renderer=dos              # dos | enhanced | titanium
    videos=original           # original | smooth | scanlines | crt | crt_curved  (the in-game videos)
    video_fit=4:3             # 4:3 | fill | stretch  (the in-game videos on a widescreen: fill cuts top and bottom)
    hud_layout=wide           # wide | original
    dos_shading=dithered      # dithered | smooth

The same choices are in the game: the global menu (Esc or the right mouse button) -> COMBAT VARIABLES. Controls follow
the install's GAMEKEY.MAP / INPUT.MAP; set them up in COCKPIT CONTROLS, as in the original. In a mission, Esc opens the
original's main menu and pauses the game. The `MW2_*` and `GBL_*` environment variables are for development and tests
only.
If the game runs slowly, start it with `MW2_PERF=1` and it prints the frame rate every 2 seconds, with where each
frame's time goes (simulation, world, HUD, buffer swap). `MW2_PERF=2` also times the GPU.

### Input devices and the gamepad
COCKPIT CONTROLS lists the original's twelve drivers (up to four at a time, the keyboard always). The joysticks
(Two Button Joystick, Flightstick PRO, SideWinder, ThrustMaster, Wingman Extreme, CH Virtual Pilot PRO) are read from the
first joystick the system reports; the Spaceball Avenger as a six-axis controller in the same way. The head trackers
(Virtual i/O i-glasses, VFX1) have no modern equivalent: they can be selected, but the port ignores their lines.

**Gamepad (port addition).** Select *Gamepad (port addition)* in COCKPIT CONTROLS and press ACCEPT. Any controller SDL
knows works (Xbox, PlayStation, Switch Pro and similar); plug it in before or during a mission. The face buttons go by
position: A = bottom (Cross, Switch B), B = right, X = left, Y = top. Default mapping:

| Control | Gamepad | | Control | Gamepad |
|---|---|---|---|---|
| Throttle up / down | left stick up / down | | Fire weapon | right trigger |
| Turn (chassis) | left stick left / right | | Fire group 1 | left trigger |
| Torso twist / pitch | right stick | | Cycle weapon / group | right / left shoulder |
| Recenter torso | right stick press | | Jump jets | A |
| Zoom in / out | d-pad up / down | | Nearest enemy / next target | B / Y |
| Glance left / right | d-pad left / right | | Target at reticle | X |
| Inspect target | left stick press | | Next nav point | Back / Select |

The sticks move the throttle and the torso at a rate, so the setting stays where you leave it. Change any of it in
CUSTOM CONFIGURATION like the other devices; it is saved in INPUT.MAP and GIDDI\CONFIG00.CPC, as the original's are.

### Customising controls
The port reads the original's two control files exactly as GBL's MW2.EXE does: `GAMEKEY.MAP` (one-press commands, one
`COMMAND key` per line) and `INPUT.MAP` (held and moved controls: `control {`, then `+ device key` / `- device key` lines,
then `}`). They are taken from `install/` first, then from the disc copy (`cd/GBL/`), in any letter case; the log names
the two files read (`controls: GAMEKEY.MAP <path>, INPUT.MAP <path>`). To customise, copy both from `cd/GBL/` into
`install/` and edit the copies. COCKPIT CONTROLS rewrites INPUT.MAP only (never GAMEKEY.MAP), and only when you accept
a changed configuration.

- GAMEKEY.MAP: a key is one character (`t`, `;`, `` ` ``, `\` ...) or F1-F12, Esc, Tab, Enter, BSP, Insert, Pause, Plus,
  Pound (`#`), with `CTRL+` / `ALT+` / `SHIFT+` in any order and case. Each key does one command - if two lines name the
  same key, the first wins; a command may have several keys. `#` starts a comment. A misspelt command makes the game
  ignore the rest of the file, as the original did (the log says which line).
- INPUT.MAP: keyboard names are those of `GIDDI\KEYBOARD.DLL` (letters, `One` ... `Zero`, `Space`, `LeftShift`,
  `LeftControl`, `Shift` / `Control` / `Alt` for either side, `GreyUpArrow` ... for the arrow keys - the plain `UpArrow`,
  `Home`, `Insert` ... are the numeric keypad); up to 8 conditions in a block; an unknown control ends the reading.
- In a mission every key belongs to the two files (the port's development keys need `MW2_DEBUG_KEYS=1`).

## Tests
The tests need your own game data, which isn't in the repository:

- `make check GBL=<GBL-game>/install` runs the tool and data checks.
- `MW2_INSTALL_DIR=<install> ./sweep_objectives <install>/MW2.PRJ [scenes]` runs every mission's objectives headless.
- `python3 tests/gbl/gbl_hud_check.py <GBL-game>` checks GBL's HUD: the weapon order and the target display.

## Licences
The port's source code is the author's. miniaudio (third_party/) is public domain / MIT-0, and Liberation Sans
(assets/fonts/) is under the SIL Open Font License: see THIRD_PARTY.md. Ghost Bear's Legacy is Activision's. No game
data is included here.
