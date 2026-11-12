#!/bin/sh
# builds gnubox.sis from the files in the S80 SDK tree
set -e
E=${EPOCROOT:-$HOME/sym/s80_20/}epoc32
D=$(mktemp -d)
cp "$E/release/armi/urel/gnubox.app" "$E/data/z/system/apps/gnubox/gnubox.rsc" \
   "$E/data/z/system/apps/gnubox/gnubox.aif" "$D/"
sed -e 's|"[^"]*\\\(gnubox\.[a-z]*\)"-|"\1"-|' "$(dirname "$0")/gnubox.pkg" > "$D/gnubox.pkg"
(cd "$D" && makesis gnubox.pkg gnubox.sis)
cp "$D/gnubox.sis" "$(dirname "$0")/gnubox.sis"
rm -rf "$D"
