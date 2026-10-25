#!/bin/sh
# Builds gather_map.nro.
#   sh build.sh                  -> out/gather_map.nro
#   sh build.sh <recomp folder>  -> also installs it into <recomp folder>/mods/zzz-gather-map
#   sh build.sh --zip            -> also packs dist/gather-map-v<version>.zip, the release download
# Needs Zig (on PATH, or ZIG=/path/to/zig) and Python 3. src/points.h comes from tools/make_data.py <recomp folder>.
set -e
cd "$(dirname "$0")"
VERSION=1.1.0
ZIG=${ZIG:-zig}
OUT=out
mkdir -p "$OUT"

CFLAGS="-target arm-freestanding-eabihf -mcpu=cortex_a57 -marm -O2 -fPIC -ffreestanding -fno-builtin
        -fno-stack-protector -fvisibility=hidden -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall
        -DPLUGIN_VERSION=\"$VERSION\" $EXTRA_CFLAGS"
"$ZIG" cc $CFLAGS -c src/plugin.c -o "$OUT/plugin.o"
"$ZIG" cc $CFLAGS -c src/start.S -o "$OUT/start.o"
"$ZIG" ld.lld -shared -T build/nro.ld --no-undefined-version -z undefs --hash-style=sysv \
    -soname gather_map.nro "$OUT/start.o" "$OUT/plugin.o" -o "$OUT/gather_map.elf"
python -I build/elf2nro.py "$OUT/gather_map.elf" "$OUT/gather_map.nro"

case "$1" in
--zip)
    python -I build/pack.py "$OUT/gather_map.nro" "dist/gather-map-v$VERSION.zip" ;;
"") ;;
*)
    mkdir -p "$1/mods/zzz-gather-map/romfs/nativeNX/plugins"
    cp "$OUT/gather_map.nro" "$1/mods/zzz-gather-map/romfs/nativeNX/plugins/"
    echo "installed into $1/mods/zzz-gather-map" ;;
esac
