#!/bin/bash
# Sets up the EKA1 (Symbian 7.0s / S80v2 / S60v2) toolchain on Linux, as used to build
# ssladaptor.dll for the Nokia 9300: GnuPoc + Symbian GCC 2.9-psion-98r2 + S60 2nd Ed. FP1 SDK.
#
# usage:  ./setup_eka1_toolchain.sh <dir with the two archives> [install dir, default ~/sym]
#   <dir> must contain gcc-539-2aeh-source.tar.bz2 (in this repo: symbian-build/) and
#   S60_SDK_2_1_NET.zip, or the split 7z (S60_SDK_2_1_NET_2.7z.001 ... .012) from the private
#   repo janseris/s60-sdk-2.1 cloned as <dir>/s60-sdk-2.1 (see BUILD_SYMBIAN_TLS.md).
# Needs: build-essential flex bison libncurses-dev zlib1g-dev cabextract perl git p7zip-full
# Tested on Ubuntu 24.04 (gcc 13, perl 5.38), 2026-10.
#
# Afterwards:  export EPOCROOT=<install dir>/s60_21/ PATH=<install dir>/wrap:$PATH
#              cd bearssl/group && bldmake bldfiles && abld build armi urel
#              cd symbian-tls/group && bldmake bldfiles && abld build armi urel ssladaptor
set -e
SRC=$(cd "$1" && pwd)
PREFIX=${2:-$HOME/sym}
HERE=$(cd "$(dirname "$0")" && pwd)
GNUPOC_COMMIT=d3ddaf58c734ad3ab34dc60a012f9d4768a9ed3e
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
SDK_SHA256=1e535703c11402b70e6c37d32b41047e63a44ec65a744f5012d1c012ccf0cf85

# S60 SDK: extract the split 7z (from <dir> or the s60-sdk-2.1 clone) if the zip isn't there yet
if [ ! -f "$SRC/S60_SDK_2_1_NET.zip" ]; then
    for d in "$SRC" "$SRC/s60-sdk-2.1"; do
        if [ -f "$d/S60_SDK_2_1_NET_2.7z.001" ]; then
            echo "== extracting S60_SDK_2_1_NET.zip from $d/S60_SDK_2_1_NET_2.7z.0*"
            7z x -y -o"$SRC" "$d/S60_SDK_2_1_NET_2.7z.001" > /dev/null
            break
        fi
    done
fi
echo "$SDK_SHA256  $SRC/S60_SDK_2_1_NET.zip" | sha256sum -c -

echo "== GnuPoc ($GNUPOC_COMMIT)"
if [ ! -d "$PREFIX/gnupoc" ]; then
  git clone -q https://github.com/mstorsjo/gnupoc-package "$PREFIX/gnupoc"
fi
cd "$PREFIX/gnupoc"
git checkout -q $GNUPOC_COMMIT
# bmconv: pointer compared with 0 by '>' (rejected by modern g++)
git apply "$HERE/gnupoc-bmconv.patch" 2>/dev/null || echo "   (bmconv patch already applied)"

echo "== Symbian GCC 2.9-psion-98r2 -> $PREFIX/gcc539"
cd "$PREFIX/gnupoc/tools"
rm -rf src obj
tar -jxf "$SRC/gcc-539-2aeh-source.tar.bz2"
patch -s -p0 < gcc-539.patch
# sys_siglist was removed from glibc
sed -i 's/sys_siglist\[\([^]]*\)\]/strsignal(\1)/g' src/gcc/collect2.c
mkdir obj
cd obj
CC="gcc -std=gnu89 -fcommon -w -fpermissive" CFLAGS="-g" sh ../src/configure --prefix="$PREFIX/gcc539" --target=arm-epoc-pe > /dev/null
sed -i 's/-O2//' Makefile
make -s all-binutils all-gas all-ld all-gcc CC="gcc -std=gnu89 -fcommon -w -fpermissive" > /dev/null 2>&1 \
  || make all-binutils all-gas all-ld all-gcc CC="gcc -std=gnu89 -fcommon -w -fpermissive"
make -s install-binutils install-gas install-ld install-gcc > /dev/null
cd ..
rm -rf obj src
cp arm-specs "$PREFIX/gcc539/lib/gcc-lib/arm-epoc-pe/2.9-psion-98r2/specs"
cd "$PREFIX/gcc539/bin"
[ -e arm-epoc-pe-cpp ] || ln -s ../lib/gcc-lib/arm-epoc-pe/2.9-psion-98r2/cpp arm-epoc-pe-cpp
[ -e as ] || ln -s arm-epoc-pe-as as
[ -e cpp ] || ln -s arm-epoc-pe-cpp cpp

echo "== EKA1 tools (petran, rcomp, bmconv, makesis, ...)"
cd "$PREFIX/gnupoc/tools"
# the last step (GNU make 3.81 as "extmake") doesn't build with a modern glibc; it isn't
# needed for building DLLs (abld uses the system make), so its failure is ignored
./install_eka1_tools "$PREFIX/gcc539" > /dev/null 2>&1 || true
for t in petran rcomp bmconv makesis; do
  [ -x "$PREFIX/gcc539/bin/$t" ] || { echo "missing $t"; exit 1; }
done

echo "== S60 2nd Edition FP1 SDK -> $PREFIX/s60_21"
cd "$PREFIX/gnupoc/sdks"
rm -rf "$PREFIX/s60_21"
./install_gnupoc_s60_21 "$SRC/S60_SDK_2_1_NET.zip" "$PREFIX/s60_21" > /dev/null
# modern perl rejects defined(%hash) / defined(@array)
(cd "$PREFIX/s60_21/epoc32/tools" && patch -s -p1 < "$HERE/sdk-perl-fixes.patch")

echo "== wrapper -> $PREFIX/wrap"
rm -rf "$PREFIX/wrap"
./install_wrapper "$PREFIX/wrap" > /dev/null
sed -i "s|^EKA1TOOLS=.*|EKA1TOOLS=$PREFIX/gcc539/bin|" "$PREFIX/wrap/gnupoc-common.sh"

echo
echo "Done. To build:"
echo "  export EPOCROOT=$PREFIX/s60_21/ PATH=$PREFIX/wrap:\$PATH"
