#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 controllercustom@myyahoo.com
# Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

# upload_giga.sh — flash one of the Giga-USBH-tinyusb examples using the
# SEGGER J-Link PLUS (1366:0101) via JLinkExe (reliable for STM32H747XI_M7).
#
# The J-Link is wired to the Giga J10 (10-pin MIPI) SWD header with:
#   VTref=3.3 V, SWDIO, SWCLK, GND. nRST not strictly required.
# Both USB-C (CDC Serial, /dev/ttyACM1) and USB-A (host, PA_15) run at the
# same time, so flashing via SWD and monitoring via USB-C CDC is the
# normal workflow.
#
# Usage:
#   ./scripts/upload_giga.sh <tinyusb-info|tinyusb-ftdi|tinyusb-pl2303|tinyusb-cp2102|tinyusb-midi|tinyusb-midimon|tinyusb-kbd|tinyusb-mouse|tinyusb-joy|tinyusb-xinput|tinyusb-gamepad|tinyusb-dualsense>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJ_ROOT/build"

name="" ; target="" ; builddir=""
case "${1:-}" in
    tinyusb-info) name=HostTinyUSB_Info; target=examples/HostTinyUSB_Info; builddir="$BUILD_DIR/HostTinyUSB_Info" ;;
    tinyusb-ftdi) name=HostTinyUSB_FTDI; target=examples/HostTinyUSB_FTDI; builddir="$BUILD_DIR/HostTinyUSB_FTDI" ;;
    tinyusb-pl2303) name=HostTinyUSB_PL2303; target=examples/HostTinyUSB_PL2303; builddir="$BUILD_DIR/HostTinyUSB_PL2303" ;;
    tinyusb-cp2102) name=HostTinyUSB_CP2102; target=examples/HostTinyUSB_CP2102; builddir="$BUILD_DIR/HostTinyUSB_CP2102" ;;
    tinyusb-midi) name=HostTinyUSB_MIDI; target=examples/HostTinyUSB_MIDI; builddir="$BUILD_DIR/HostTinyUSB_MIDI" ;;
    tinyusb-kbd) name=HostTinyUSB_Keyboard; target=examples/HostTinyUSB_Keyboard; builddir="$BUILD_DIR/HostTinyUSB_Keyboard" ;;
    tinyusb-mouse) name=HostTinyUSB_Mouse; target=examples/HostTinyUSB_Mouse; builddir="$BUILD_DIR/HostTinyUSB_Mouse" ;;
    tinyusb-joy) name=HostTinyUSB_Joystick; target=examples/HostTinyUSB_Joystick; builddir="$BUILD_DIR/HostTinyUSB_Joystick" ;;
    tinyusb-xinput) name=HostTinyUSB_XInput; target=examples/HostTinyUSB_XInput; builddir="$BUILD_DIR/HostTinyUSB_XInput" ;;
    tinyusb-gamepad) name=HostTinyUSB_Gamepad; target=examples/HostTinyUSB_Gamepad; builddir="$BUILD_DIR/HostTinyUSB_Gamepad" ;;
    tinyusb-dualsense) name=HostTinyUSB_DualSense; target=examples/HostTinyUSB_DualSense; builddir="$BUILD_DIR/HostTinyUSB_DualSense" ;;
    tinyusb-midimon) name=HostTinyUSB_MIDIMonitor; target=examples/HostTinyUSB_MIDIMonitor; builddir="$BUILD_DIR/HostTinyUSB_MIDIMonitor" ;;
    *) echo "usage: upload_giga.sh <tinyusb-info|tinyusb-ftdi|tinyusb-pl2303|tinyusb-cp2102|tinyusb-midi|tinyusb-midimon|tinyusb-kbd|tinyusb-mouse|tinyusb-joy|tinyusb-xinput|tinyusb-gamepad|tinyusb-dualsense>" >&2; exit 2;
esac

# Compile fresh into per-target dir. GigaTinyUSB tree root (see build.sh).
echo "[upload_giga] compiling $target..."
rm -rf "$builddir"
arduino-cli compile --fqbn arduino:mbed_giga:giga --build-path "$builddir" --build-property "build.extra_flags=-I$PROJ_ROOT/src/gigatuh" "$target"
buildhex="$builddir/${name}.ino.hex"
[[ -f "$buildhex" ]] || { echo "no .hex file produced at $buildhex" >&2; exit 1; }

echo "[upload_giga] flashing $buildhex via J-Link (JLinkExe)..."
# JLinkExe is more reliable than OpenOCD for the H747 dual-core target
# (OpenOCD stm32h7x.cfg fails CPUID examine without manual DAP setup).
TMPSCRIPT="$(mktemp /tmp/jlink_flash_XXXX.jlink)"
cat > "$TMPSCRIPT" <<JLINK_EOF
si SWD
device STM32H747XI_M7
speed 1000
connect
r
h
loadfile $buildhex
r
g
q
JLINK_EOF
if ! JLinkExe -CommanderScript "$TMPSCRIPT" 2>&1 | tee /tmp/jlink_giga.log; then
    echo "[upload_giga] JLinkExe failed, falling back to OpenOCD..." >&2
    openocd \
        -f /usr/share/openocd/scripts/interface/jlink.cfg \
        -c "transport select swd" \
        -f /usr/share/openocd/scripts/target/stm32h7x.cfg \
        -c "adapter speed 1000; init; reset halt; program $buildhex verify; reset run; exit" 2>&1 | tee -a /tmp/jlink_giga.log
fi
rm -f "$TMPSCRIPT"
grep -E "Downloading|O\.K\.|Verified|error|fail|J-Link:" /tmp/jlink_giga.log || true

echo "[upload_giga] J-Link flash complete."
echo "Verify on the USB-C CDC console (/dev/ttyACM1 @115200):"
echo "  ./test/watch_tinyusb.sh all"
case "$name" in
    HostTinyUSB_FTDI) echo "  FTDI UART: jumper TX-RX for ECHO, or TX->D0 RX->D1 GND->GND for CROSS" ;;
    HostTinyUSB_PL2303) echo "  PL2303 UART: jumper TX-RX for ECHO, or TX->D0 RX->D1 GND->GND for CROSS" ;;
    HostTinyUSB_CP2102) echo "  CP2102 UART: jumper TX-RX for ECHO, or TX->D0 RX->D1 GND->GND for CROSS" ;;
    HostTinyUSB_MIDI) echo "  USB-MIDI echo device on USB-A; seq-verified packet echo" ;;
    HostTinyUSB_MIDIMonitor) echo "  Real USB-MIDI device on USB-A; wiggle controls, l toggles LED" ;;
    HostTinyUSB_Keyboard) echo "  Real USB keyboard on USB-A; type keys" ;;
    HostTinyUSB_Mouse) echo "  Real USB mouse on USB-A; move and click" ;;
    HostTinyUSB_Joystick) echo "  Real USB joystick on USB-A; move stick, twist, throttle, hat, buttons" ;;
    HostTinyUSB_Gamepad) echo "  DS4 gamepad on USB-A; buttons/sticks, r/g/b LED, v rumble" ;;
    HostTinyUSB_DualSense) echo "  DualSense gamepad on USB-A; buttons/sticks, r/g/b LED, v rumble" ;;
esac
