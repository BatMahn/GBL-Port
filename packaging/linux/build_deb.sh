#!/bin/sh
# Build a Debian / Ubuntu package of gbl from a Linux build (make gbl) in the current directory (CI: the repo root).
# usage: sh packaging/linux/build_deb.sh VERSION [LIBSDL3]   -> dist/gblport_VERSION_amd64.deb
# LIBSDL3: a libSDL3.so.0 to ship in /usr/lib/gblport (Debian / Ubuntu have no SDL3 package yet); gbl must then be
# linked with  make gbl RPATH='$$ORIGIN/../lib/gblport'. Without it the package depends on the system's libsdl3-0.
set -e
VER=${1:-0.0.0}
SDL3_LIB=$2
ARCH=$(dpkg --print-architecture)
P=dist/deb/gblport
rm -rf dist/deb
mkdir -p $P/DEBIAN $P/usr/bin $P/usr/share/gblport $P/usr/share/applications $P/usr/share/icons/hicolor/256x256/apps \
         $P/usr/share/doc/gblport
install -m755 gbl $P/usr/bin/gbl
if [ -n "$SDL3_LIB" ]; then
  mkdir -p $P/usr/lib/gblport
  install -m644 "$SDL3_LIB" $P/usr/lib/gblport/libSDL3.so.0
  DEPS="libc6, libvulkan1, mesa-vulkan-drivers | vulkan-icd"
else
  DEPS="libsdl3-0, libvulkan1, mesa-vulkan-drivers | vulkan-icd"
fi
cp -r assets $P/usr/share/gblport/
install -m644 packaging/linux/gblport.desktop $P/usr/share/applications/gblport.desktop
install -m644 packaging/linux/gblport.png $P/usr/share/icons/hicolor/256x256/apps/gblport.png
install -m644 README.md FOLDER_SETUP.md THIRD_PARTY.md $P/usr/share/doc/gblport/
cat > $P/DEBIAN/control <<CTRL
Package: gblport
Version: $VER
Section: games
Priority: optional
Architecture: $ARCH
Depends: $DEPS
Maintainer: BatMahn <65992136+BatMahn@users.noreply.github.com>
Description: Native port of MechWarrior 2: Ghost Bear's Legacy (DOS, 1995)
 Ghost Bear's Legacy rebuilt as a native 64-bit program, drawn with Vulkan through SDL3's GPU API.
 Requires your own copy of the original Ghost Bear's Legacy disc.
CTRL
mkdir -p dist
dpkg-deb --root-owner-group --build $P dist/gblport_${VER}_${ARCH}.deb
echo "dist/gblport_${VER}_${ARCH}.deb"
