#!/bin/sh
# Build "Ghost Bear's Legacy.app" from a macOS build (make gbl) in the current directory (CI: the repo root).
# Layout: the binary in Contents/MacOS, the font in Contents/Resources/assets (SDL_GetBasePath() returns
# Contents/Resources inside a bundle; codesign rejects non-code files in Contents/MacOS), the icon (packaging/macos/gblport.icns) in
# Contents/Resources, and the SDL3 dylib the binary was linked against (Homebrew's sdl3) copied into Contents/Frameworks
# so the app runs without Homebrew (the graphics are Metal through SDL3's GPU API: nothing else to bundle). The bundle is ad-hoc signed (codesign -s -), not notarised: a downloaded copy
# needs  xattr -dr com.apple.quarantine "Ghost Bear's Legacy.app"  once (README).
set -e
APP="Ghost Bear's Legacy.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets/fonts"
cp gbl "$APP/Contents/MacOS/"   # one executable: the shell and (gbl --sim) the simulation
cp assets/fonts/LiberationSans-Bold.ttf assets/fonts/README.md "$APP/Contents/Resources/assets/fonts/"
[ -f packaging/macos/gblport.icns ] && cp packaging/macos/gblport.icns "$APP/Contents/Resources/"
# bundle SDL3: the first non-system dylib named libSDL3 in the binary's load commands (Homebrew's sdl3:
# /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib)
if otool -L "$APP/Contents/MacOS/gbl" | grep -q -i -E 'OpenGL|libSDL2'; then
  echo "gbl links OpenGL or SDL2: rebuild it from this source with SDL3 (brew install sdl3 pkgconf; make clean; make gbl)" >&2; exit 1
fi
SDL=$(otool -L "$APP/Contents/MacOS/gbl" | awk '/libSDL3/ {print $1; exit}')
case "$SDL" in
  "") echo "gbl does not link libSDL3 (an SDL3.framework or a static SDL3?) - left as is" ;;
  *)
    SRC=$SDL
    case "$SDL" in
      @rpath/*) SRC="$(${PKG_CONFIG:-pkg-config} --variable=libdir sdl3)/${SDL#@rpath/}" ;;
      @*) SRC="" ;;
    esac
    [ -n "$SRC" ] && [ -f "$SRC" ] || { echo "cannot find the SDL3 library $SDL" >&2; exit 1; }
    mkdir -p "$APP/Contents/Frameworks"
    LIB=$(basename "$SDL")
    cp -L "$SRC" "$APP/Contents/Frameworks/$LIB"
    chmod u+w "$APP/Contents/Frameworks/$LIB"
    install_name_tool -id "@executable_path/../Frameworks/$LIB" "$APP/Contents/Frameworks/$LIB"
    install_name_tool -change "$SDL" "@executable_path/../Frameworks/$LIB" "$APP/Contents/MacOS/gbl"
    echo "bundled $SRC" ;;
esac
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Ghost Bear's Legacy</string>
  <key>CFBundleExecutable</key><string>gbl</string>
  <key>CFBundleIdentifier</key><string>org.gblport.gbl</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>gblport</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>10.14</string>
</dict></plist>
PLIST
# install_name_tool voids the linker's signature; arm64 macOS refuses to run unsigned code, so re-sign ad hoc
if [ -d "$APP/Contents/Frameworks" ]; then codesign --force -s - "$APP/Contents/Frameworks/"*.dylib; fi
codesign --force -s - "$APP"
echo "built $APP (on first launch it finds GBL-game next to the app, one level up or in ~)"
