# Third-party components

| Component | Use | Licence |
|-----------|-----|---------|
| miniaudio (third_party/miniaudio.*) | decoding the CD soundtrack (FLAC / MP3 / WAV) | public domain (Unlicense) or MIT-0 |
| stb_truetype (src/third_party/stb_truetype.h) | rendering the port's own settings text | public domain or MIT |
| Liberation Sans Bold (assets/fonts) | the port's own settings text, matched to the original artwork | SIL Open Font License 1.1 |
| SDL3 (system library; bundled in the macOS app, the Windows zip and the .deb) | window, input, audio output | zlib |
| SDL2 (the legacy Windows build: SDL2.dll in its zip) | window, input, audio output, OpenGL context | zlib |
| fdlibm's atan2f / atanf (src/simmath.h, as in glibc's libm) | the simulation's angles, the same on every build | Sun Microsystems: "Permission to use, copy, modify, and distribute this software is freely granted, provided that this notice is preserved" |

Ghost Bear's Legacy's own data (archives, art, movies, voices, music) is read from your copy of the game. The
exceptions, Activision's and included only for the program's icons: the original game icon (`packaging/windows/gblport.ico`,
`packaging/linux/gblport.png`, from GBLWIN.EXE) and the macOS app icon (`packaging/macos/gblport.icns`, user-supplied).
