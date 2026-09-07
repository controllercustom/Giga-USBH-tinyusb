#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 controllercustom@myyahoo.com
# Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

# build.sh — compile all Giga-USBH-tinyusb examples using arduino-cli.
#
# The examples `#include "GigaTinyUSB.h"` which lives in this repo's src/;
# we register the repo as a symlinked library inside the global sketchbook
# (idempotent) so the header resolves from any sketch.
#
# The vendored TinyUSB tree is now flattened to src/ (tusb.h, common/,
# host/, etc.) so Arduino IDE adds src/ to the include path and
# "common/tusb_common.h" resolves without extra -I. CLI still works
# with or without the flag.
#
# Usage:
#   ./scripts/build.sh                 # build all examples
#   ./scripts/build.sh <target>        # build one (path relative to repo)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GIGA_FQBN="arduino:mbed_giga:giga"

GIGA_EXTRA_FLAGS=""

ensure_core() {
    echo "[build] checking cores..."
    arduino-cli core list 2>&1 | grep -c "mbed_giga" >/dev/null || arduino-cli core install arduino:mbed_giga
}

# Register this repo as a symlinked library inside the global sketchbook,
# so `#include "GigaTinyUSB.h"` resolves from ANY sketch. Idempotent.
ensure_project_lib_linked() {
    local libdir="$HOME/Arduino/libraries"
    mkdir -p "$libdir"
    local link="$libdir/Giga-USBH-tinyusb"
    if [[ ! -f "$link" && ! -d "$link" ]]; then
        ln -s "$PROJ_ROOT" "$link"
        echo "[build] linked $link -> $PROJ_ROOT"
    fi
}

TARGETS=(examples/HostTinyUSB_Info examples/HostTinyUSB_FTDI examples/HostTinyUSB_PL2303 examples/HostTinyUSB_CP2102 examples/HostTinyUSB_MIDI examples/HostTinyUSB_MIDIMonitor examples/HostTinyUSB_Keyboard examples/HostTinyUSB_Mouse examples/HostTinyUSB_Joystick examples/HostTinyUSB_XInput examples/HostTinyUSB_Gamepad examples/HostTinyUSB_DualSense examples/HostTinyUSB_MSC examples/HostTinyUSB_MSC_FatFS examples/HostTinyUSB_TriCDC examples/HostTinyUSB_HID_Boot)
if [[ $# -ge 1 ]]; then TARGETS=("$1"); fi

ensure_core; ensure_project_lib_linked

rc=0
for t in "${TARGETS[@]}"; do
    echo "==================== building $t ===================="
    # Use a per-target build dir so artifacts don't clobber and upload scripts can find them.
    bdir="$PROJ_ROOT/build/$(basename "$t")"
    rm -rf "$bdir"
    if [[ -n "$GIGA_EXTRA_FLAGS" ]]; then
        FLAGS=(--build-property "$GIGA_EXTRA_FLAGS")
    else
        FLAGS=()
    fi
    if ! arduino-cli compile --fqbn "$GIGA_FQBN" --build-path "$bdir" "${FLAGS[@]}" "$t"; then
        echo "[build] FAILED: $t" >&2
        rc=1
    fi
done
exit $rc
