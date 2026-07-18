#!/bin/sh
# Mac development build. Set DD_DEBUG_BUILD=0 for the non-diagnostic binary
# or DD_DEBUG_BUILD=1 (the default) for the diagnostic binary.
set -eu
cd "$(dirname "$0")/.."

case "${DD_DEBUG_BUILD:-1}" in
    0)
        debug_build=0
        output="build/DisketteDungeon_mac"
        ;;
    1)
        source_path=$(pwd -P)/src/main.c
        source_sha=$(shasum -a 256 "$source_path" | awk '{print $1}')
        debug_build=1
        output="build/DisketteDungeon_diag_mac"
        ;;
    *)
        echo "error: DD_DEBUG_BUILD must be 0 or 1" >&2
        exit 2
        ;;
esac

mkdir -p build

clang -c -x objective-c -O2 -Wall -Wno-missing-braces -o build/sokol_impl_mac.o src/sokol_impl.c
if [ "$debug_build" = 1 ]; then
    clang -c -std=c99 -O2 -Wall -Wno-missing-braces -DDD_DEBUG \
        "-DDD_DEBUG_SOURCE_SHA256=\"$source_sha\"" \
        "-DDD_DEBUG_SOURCE_PATH=\"$source_path\"" -o build/game_mac.o src/main.c
else
    clang -c -std=c99 -O2 -Wall -Wno-missing-braces -o build/game_mac.o src/main.c
fi
clang -o "$output" build/sokol_impl_mac.o build/game_mac.o \
    -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
    -framework AudioToolbox
echo "OK: $output"
