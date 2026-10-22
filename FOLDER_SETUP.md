# Ghost Bear's Legacy - game folder setup

Ghost Bear's Legacy (the 1995 expansion) is its own program in this port, as it was on DOS: run `gbl` (or the
Ghost Bear's Legacy app), not `mw2`. It needs its own game folder, `GBL-game/`, built from **your original Ghost
Bear's Legacy CD** ("MechWarrior 2 - Ghost Bear's Legacy (USA) (Windows 95 & MS-DOS)", the hybrid DOS / Windows 95
disc). GBL's disc carries the whole game.

None of the game's files are provided with the port. Everything below comes from your own disc.

```
GBL-game/
├── cd/                      YOUR COPY - the ORIGINAL Ghost Bear's Legacy disc, copied as-is (folders and all)
│   ├── ACRODOS/  ACROREAD/  CFGS/  DIRECTX/  DOS/
│   ├── GBL/                 the game data (MW2.PRJ, DATABASE.MW2, the 22 new mission scenes *SCN1.BWD, configs)
│   ├── GIDDI/  GIDDIWIN/    cockpit control configurations
│   ├── LAUNCH/              drop screens
│   ├── MANUAL/  MERCS/
│   ├── SMK/                 movies, the hall / Mission Computer / Mech Lab art, DBSOUND.MW2 (briefing voices)
│   ├── SND/  SP16/  SP256/  VFX/
│   ├── INSTALL.EXE  GBLINFO.TXT  README.TXT  OLD_HERC.DRV  MW2SHELL.DLL  MW2.DLL ...
│
├── titanium/                YOUR COPY - optional, for the 3D look: 3 files from the Ghost Bear's Legacy Titanium
│   ├── MW2.PRJ              Edition disc's INSTALL\16BIT\ (the 16-bit build: textured models for every GBL
│   ├── SKYGND.PAR           'Mech and mission, sky dome, ground)
│   └── particle.PID
│
├── install/                 the game's own files: pilots (MW2REG.CFG), settings (MW2SND.CFG, MW2DIF.CFG ...),
│                            the mission handoff (USERSTAR.BWD ...). It can START EMPTY - the port reads the disc's
│                            GBL\ defaults and writes everything here, never into cd/.
│
└── music/                   YOUR COPY - optional, the disc's CD audio (FLAC, MP3 or WAV)
    ├── Track02.mp3
    │   ...
    └── Track21.mp3          (20 files; there is no Track01 - that's the data track)
```

## Getting the disc's files

- **From the physical CD:** copy the whole disc into `cd/` (on a Mac: open the disc in Finder, select all, copy).
  Hidden files (`OLD_HERC.DRV`, `GBL_HERC.DRV`, `TECH.GID`) don't matter to the port.
- **From a disc image (BIN/CUE, e.g. a Redump rip):** track 01 is the data track. Mount it (macOS: rename the
  track 01 `.bin` to `.iso` if needed and double-click; Linux: `mount -o loop`) or extract it with 7-Zip, and copy
  everything into `cd/`. The audio tracks 02-21 are the music.
- **Music:** rip tracks 2-21 to `music/` named `Track02` ... `Track21` (any of .flac / .mp3 / .wav). Track 02 starts
  after a 74-frame pregap (index 01 at 00:00:74) - rippers that honour the cue sheet handle it.

## Minimum to play

Only `cd/` is required (and an `install/` folder, which may be empty). Add `music/` for the mission soundtrack and
`titanium/` for the 3D (textured) look.

## Notes

- The look is chosen in Combat Variables (RENDERER). DOS, the default, needs only `cd/`. ENHANCED (GBL's own models
  and textures in true colour, with each mission's Titanium fog) and TITANIUM (the Titanium Edition's 1998 Direct3D
  look: its 16-bit MW2.PRJ carries textured models for all of GBL) need the `titanium/` files.
- There is no CD check: the port never asks for the disc. (The original's check looked for the disc's hidden
  `OLD_HERC.DRV` at start-up.)
- The game folder is found on first run (the command line `gbl /path/to/GBL-game`, the current folder, next to the
  program, one level up, or `~/GBL-game`) and remembered in its own settings file
  (`~/Library/Application Support/gblport/gblport.cfg` on macOS, `~/.config/gblport/gblport.cfg` on Linux,
  `%APPDATA%\gblport\gblport.cfg` on Windows). If no folder is found, `gbl` asks for it with the system's folder picker.

## Common mistakes

| Wrong | Right |
|---|---|
| `cd/MW2.PRJ` (copied only the GBL folder's contents) | `cd/GBL/MW2.PRJ` - copy the whole disc as it is |
| The Titanium Edition disc in `cd/` | `cd/` is the 1995 DOS / Windows 95 GBL disc; Titanium goes in `titanium/`, only those 3 files |
| Titanium files from `INSTALL\8BIT\` | Use `INSTALL\16BIT\` |
| `GBL-game/GBL-game/cd/...` (nested by unzipping) | `GBL-game/cd/...` |
| `Music/`, `CD/`, `Install/` | the folder names exactly as shown: `music/`, `cd/`, `install/` |
