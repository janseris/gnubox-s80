#!/bin/bash
# Installs the Series 80 Developer Platform 2.0 SDK (Nokia 9300/9500, Symbian 7.0s) for GnuPoc on
# Linux, next to the S60 2.1 SDK that setup_eka1_toolchain.sh installed (its build tools are
# reused: same Symbian 7.0s tools, already patched for Linux and modern perl).
#
# usage: ./setup_s80_sdk.sh S80_DP_2_0_SDK.zip [install dir, default ~/sym]
#   S80_DP_2_0_SDK.zip: Nokia's SDK (proprietary, keep private), from the Symbian Archive
#   (https://mrrosset.github.io/Symbian-Archive/SDKs-Series.html)
# Needs: unshield, unzip; ~/sym from setup_eka1_toolchain.sh.
# Afterwards: export EPOCROOT=~/sym/s80_20/ PATH=~/sym/wrap:$PATH; bldmake bldfiles; abld build armi urel
set -e
ZIP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
PREFIX=${2:-$HOME/sym}
D=$PREFIX/s80_20
TMP=$(mktemp -d)
echo "== unpacking $ZIP"
(cd "$TMP" && unzip -q "$ZIP" && unshield -d x x data1.cab > /dev/null)
rm -rf "$D"; mkdir -p "$D"
mv "$TMP/x/Epoc32" "$D/epoc32"
rm -rf "$TMP"
echo "== lower-casing headers and libraries (GnuPoc)"
cd "$PREFIX/gnupoc/sdks"
./lowercase -map_paths headers "$D/epoc32/include" > /dev/null 2>&1 || true
./lowercase -symlink "$D/epoc32/include" > /dev/null 2>&1 || true
./lowercase -map_files libraries "$D/epoc32/release" > /dev/null 2>&1 || true
./lowercase -symlink "$D/epoc32/release" > /dev/null 2>&1 || true
./lowercase "$D/epoc32/data" > /dev/null 2>&1 || true
./fixinclude "$D/epoc32/include" > /dev/null 2>&1 || true
echo "== build tools from the S60 2.1 install"
rm -rf "$D/epoc32/tools"
cp -a "$PREFIX/s60_21/epoc32/tools" "$D/epoc32/tools"
echo "Done: export EPOCROOT=$D/ PATH=$PREFIX/wrap:\$PATH"
