#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 controllercustom@myyahoo.com
# Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

# watch_tinyusb.sh — read Giga's USB-C CDC console for TinyUSB host logs.
# Usage: ./test/watch_tinyusb.sh [all] [port]   (port defaults to /dev/ttyACM1)
set -euo pipefail

MODE="${1:-all}"
PORT="${2:-}"
if [[ -z "$PORT" ]]; then
    if [[ -e /dev/serial/by-id/usb-Arduino_Giga_* ]]; then
        PORT="$(echo /dev/serial/by-id/usb-Arduino_Giga_*-if00 | awk '{print $1}')"
        PORT="$(readlink -f "$PORT" 2>/dev/null || echo "$PORT")"
    elif [[ -c /dev/ttyACM1 ]]; then
        PORT=/dev/ttyACM1
    elif [[ -c /dev/ttyACM0 ]]; then
        PORT=/dev/ttyACM0
    else
        PORT=/dev/ttyACM1
    fi
fi
[[ -c "$PORT" ]] || { echo "no $PORT (expected /dev/ttyACM1)"; exit 1; }
echo "[watch_tinyusb] reading Giga CDC on $PORT @115200 (mode=$MODE)" >&2

stty -F "$PORT" 115200 cs8 -cstopb -parenb 2>/dev/null || true
stty -F "$PORT" raw -echo -icanon

PATTERN='TUH|FTDI|PL2303|CP2102|MIDI|MOUSE|JOY|move|buttons|vendor|mount|unmount|vid=|DESC|mode=|tx=|rx=|ok=|err=|baud=|tick|###|ERROR|FAIL|init rhport|ready|switched'
tr -dc '[:print:]\n' < "$PORT" | grep -E --line-buffered "$PATTERN"
