#!/bin/sh
# Windows release build and explicit local/official size gate.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-x86_64-w64-mingw32-gcc}
DD_DEBUG_BUILD=${DD_DEBUG_BUILD:-0}
case "$DD_DEBUG_BUILD" in
    0) ;;
    1)
        source_path=$(pwd -P)/src/main.c
        source_sha=$(shasum -a 256 "$source_path" | awk '{print $1}')
        $CC -c -std=c99 -Os -ffunction-sections -fdata-sections -o build/sokol_impl_diag_win.o src/sokol_impl.c
        $CC -c -std=c99 -Os -DDD_DEBUG "-DDD_DEBUG_SOURCE_SHA256=\"$source_sha\"" "-DDD_DEBUG_SOURCE_PATH=\"$source_path\"" -ffunction-sections -fdata-sections -Wall -Wno-missing-braces -o build/game_diag_win.o src/main.c
        $CC -o build/DisketteDungeon_diag.exe build/sokol_impl_diag_win.o build/game_diag_win.o \
            -mconsole -Wl,--gc-sections -s -static \
            -lkernel32 -luser32 -lshell32 -lgdi32 -lole32
        exit 0
        ;;
    *) echo 'DD_DEBUG_BUILD must be 0 or 1' >&2 ; exit 2 ;;
esac
MODE=${DD_SIZE_MODE:-local}
case "$MODE" in local|official) ;; *) echo 'SIZE_GATE_JSON={"mode":"unknown","baseline":null,"size":null,"delta":null,"max_size":1320960,"delta_limit":65536,"submission_limit":1474560,"reserve_headroom":null,"submission_headroom":null,"status":"fail","reason":"invalid-mode"}' ; exit 2 ;; esac

$CC -c -std=c99 -Os -ffunction-sections -fdata-sections -o build/sokol_impl_win.o src/sokol_impl.c
$CC -c -std=c99 -Os -ffunction-sections -fdata-sections -Wall -Wno-missing-braces -o build/game_win.o src/main.c
$CC -o build/DisketteDungeon.exe build/sokol_impl_win.o build/game_win.o \
    -mwindows -Wl,--gc-sections -s -static \
    -lkernel32 -luser32 -lshell32 -lgdi32 -lole32


SIZE=$(stat -f%z build/DisketteDungeon.exe 2>/dev/null || stat -c%s build/DisketteDungeon.exe)
LIMIT=1474560
RESERVE=153600
MAX_SIZE=1320960
DELTA_LIMIT=65536
if [ "$MODE" = official ]; then
    case "${BASELINE_SIZE:-}" in
        ''|*[!0-9]*|0*) echo 'SIZE_GATE_JSON={"mode":"official","baseline":null,"size":null,"delta":null,"max_size":1320960,"delta_limit":65536,"submission_limit":1474560,"reserve_headroom":null,"submission_headroom":null,"status":"fail","reason":"invalid-baseline"}' ; exit 2 ;;
    esac
    BASELINE=$BASELINE_SIZE
    DELTA=$((SIZE-BASELINE))
    RESERVE_HEADROOM=$((MAX_SIZE-SIZE))
    SUBMISSION_HEADROOM=$((LIMIT-SIZE))
    if [ "$SIZE" -gt "$MAX_SIZE" ] || [ "$DELTA" -gt "$DELTA_LIMIT" ]; then
        echo "SIZE_GATE_JSON={\"mode\":\"official\",\"baseline\":$BASELINE,\"size\":$SIZE,\"delta\":$DELTA,\"max_size\":$MAX_SIZE,\"delta_limit\":$DELTA_LIMIT,\"submission_limit\":$LIMIT,\"reserve_headroom\":$RESERVE_HEADROOM,\"submission_headroom\":$SUBMISSION_HEADROOM,\"status\":\"fail\",\"reason\":\"size-limit\"}"
        exit 1
    fi
    echo "SIZE_GATE_JSON={\"mode\":\"official\",\"baseline\":$BASELINE,\"size\":$SIZE,\"delta\":$DELTA,\"max_size\":$MAX_SIZE,\"delta_limit\":$DELTA_LIMIT,\"submission_limit\":$LIMIT,\"reserve_headroom\":$RESERVE_HEADROOM,\"submission_headroom\":$SUBMISSION_HEADROOM,\"status\":\"pass\"}"
else
    BASELINE=null
    DELTA=null
    if [ -n "${BASELINE_SIZE:-}" ]; then
        case "$BASELINE_SIZE" in
            *[!0-9]*|0*) echo 'SIZE_GATE_JSON={"mode":"local","baseline":null,"size":null,"delta":null,"max_size":1320960,"delta_limit":65536,"submission_limit":1474560,"reserve_headroom":null,"submission_headroom":null,"status":"fail","reason":"invalid-baseline"}' ; exit 2 ;;
        esac
        BASELINE=$BASELINE_SIZE
        DELTA=$((SIZE-BASELINE))
    fi
    RESERVE_HEADROOM=$((MAX_SIZE-SIZE))
    SUBMISSION_HEADROOM=$((LIMIT-SIZE))
    if [ "$SIZE" -gt "$MAX_SIZE" ]; then
        echo "SIZE_GATE_JSON={\"mode\":\"local\",\"baseline\":$BASELINE,\"size\":$SIZE,\"delta\":$DELTA,\"max_size\":$MAX_SIZE,\"delta_limit\":$DELTA_LIMIT,\"submission_limit\":$LIMIT,\"reserve_headroom\":$RESERVE_HEADROOM,\"submission_headroom\":$SUBMISSION_HEADROOM,\"status\":\"fail\",\"reason\":\"size-limit\"}"
        exit 1
    fi
    echo "SIZE_GATE_JSON={\"mode\":\"local\",\"baseline\":$BASELINE,\"size\":$SIZE,\"delta\":$DELTA,\"max_size\":$MAX_SIZE,\"delta_limit\":$DELTA_LIMIT,\"submission_limit\":$LIMIT,\"reserve_headroom\":$RESERVE_HEADROOM,\"submission_headroom\":$SUBMISSION_HEADROOM,\"status\":\"pass\"}"
fi
