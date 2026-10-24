#!/bin/sh
# Build "Ghost Bear's Legacy.app" from a macOS build (make gbl) in the current directory (CI: the repo root).
# Layout: the binary in Contents/MacOS, the font in Contents/Resources/assets (SDL_GetBasePath() returns
# Contents/Resources inside a bundle; codesign rejects non-code files in Contents/MacOS), the icon (packaging/macos/gblport.icns) in
# Contents/Resources, and the SDL2 dylib the binary was linked against copied into Contents/Frameworks so the
# app runs without Homebrew. The bundle is ad-hoc signed (codesign -s -), not notarised: a downloaded copy
# needs  xattr -dr com.apple.quarantine "Ghost Bear's Legacy.app"  once (README).
set -e
APP="Ghost Bear's Legacy.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets/fonts"
cp gbl "$APP/Contents/MacOS/"   # one executable: the shell and (gbl --sim) the simulation
cp assets/fonts/LiberationSans-Bold.ttf assets/fonts/README.md "$APP/Contents/Resources/assets/fonts/"
[ -f packaging/macos/gblport.icns ] && cp packaging/macos/gblport.icns "$APP/Contents/Resources/"
# bundle SDL2: the first non-system dylib named libSDL2 in the binary's load commands
SDL=$(otool -L "$APP/Contents/MacOS/gbl" | awk '/libSDL2/ {print $1; exit}')
case "$SDL" in
  ""|@*) echo "SDL2 not linked from a file path ($SDL) - left as is" ;;
  *)
    mkdir -p "$APP/Contents/Frameworks"
    LIB=$(basename "$SDL")
    cp -L "$SDL" "$APP/Contents/Frameworks/$LIB"
    chmod u+w "$APP/Contents/Frameworks/$LIB"
    install_name_tool -id "@executable_path/../Frameworks/$LIB" "$APP/Contents/Frameworks/$LIB"
    install_name_tool -change "$SDL" "@executable_path/../Frameworks/$LIB" "$APP/Contents/MacOS/gbl"
    echo "bundled $SDL"
    # Homebrew's sdl2 is now sdl2-compat, a shim that dlopen()s SDL3 at runtime. It looks for
    # @loader_path/libSDL3.dylib first (i.e. next to itself in Contents/Frameworks), so bundle SDL3 there too.
    if strings "$APP/Contents/Frameworks/$LIB" | grep -q libSDL3; then
      SDL3=""
      for d in "$(brew --prefix sdl3 2>/dev/null)/lib" /opt/homebrew/lib /usr/local/lib; do
        [ -f "$d/libSDL3.0.dylib" ] && { SDL3="$d/libSDL3.0.dylib"; break; }
      done
      [ -n "$SDL3" ] || { echo "sdl2-compat needs SDL3 but libSDL3.0.dylib was not found" >&2; exit 1; }
      cp -L "$SDL3" "$APP/Contents/Frameworks/libSDL3.dylib"
      chmod u+w "$APP/Contents/Frameworks/libSDL3.dylib"
      install_name_tool -id "@loader_path/libSDL3.dylib" "$APP/Contents/Frameworks/libSDL3.dylib"
      echo "bundled $SDL3 (for sdl2-compat)"
    fi ;;
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
</dict></plist>
PLIST
# install_name_tool voids the linker's signature; arm64 macOS refuses to run unsigned code, so re-sign ad hoc
if [ -d "$APP/Contents/Frameworks" ]; then codesign --force -s - "$APP/Contents/Frameworks/"*.dylib; fi
codesign --force -s - "$APP"
echo "built $APP (on first launch it finds GBL-game next to the app, one level up or in ~)"
