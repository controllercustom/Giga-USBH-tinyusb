# Giga-USBH-tinyusb

TinyUSB USB-A host stack for the Arduino GIGA R1 WiFi — a standalone
alternative to `Arduino_USBHostMbed5` with no mbed USB code and no dependency
on it. Deliberately **not** API compatible with `Arduino_USBHostMbed5`.

Role split: Mbed still owns clocks/RCC, the USB-C CDC console (`Serial`
@115200) and SysTick. TinyUSB owns the USB-A host port only (OTG_HS, rhport 1,
embedded FS PHY). Sketches power USB-A VBUS (`PA_15`) via
`gigaTinyUSB_enableHostPort()`, then call `tuh_giga_init()` and poll
`tuh_task()`.

```cpp
#include "GigaTinyUSB.h"
void setup() {
  GIGATINYUSB_CONSOLE.begin(GIGATINYUSB_BAUD);
  gigaTinyUSB_enableHostPort();
  tuh_giga_init();
}
void loop() { tuh_task(); /* ... */ }
```

## Install

For Arduino IDE 2.x users:

1. Download the ZIP from GitHub: open https://github.com/controllercustom/Giga-USBH-tinyusb in a browser, click **Code → Download ZIP**.
2. In Arduino IDE 2.x: **Sketch → Include Library → Add .ZIP Library…**, select the downloaded ZIP. The IDE installs it to `Arduino/libraries/Giga-USBH-tinyusb` so `#include "GigaTinyUSB.h"` resolves (no manual copy needed). If you unzip by hand, rename `Giga-USBH-tinyusb-main` → `Giga-USBH-tinyusb`.
3. Install the board support: **Tools → Board → Boards Manager**, search `GIGA`, install **Arduino Mbed OS GIGA Boards** (`arduino:mbed_giga`).
4. Open an example: **File → Examples → Giga-USBH-tinyusb → HostTinyUSB_Info** (or any of the 16 examples), select **Tools → Board → Arduino GIGA R1 WiFi** and the correct port, then **Verify** / **Upload**.

## Build

> For maintainers and developers only — library users can ignore this section.

Maintainer CLI build (requires `arduino-cli` + `arduino:mbed_giga` core):

```bash
./scripts/build.sh                          # all 16 examples
./scripts/build.sh examples/HostTinyUSB_Info  # single example
./scripts/upload_giga.sh tinyusb-info       # compile + J-Link flash
./test/watch_tinyusb.sh all                 # CDC console @115200
```

## Examples

| Example | Device |
|---|---|
| HostTinyUSB_CP2102 | CP2102 bridge |
| HostTinyUSB_DualSense | DualSense gamepad + LED/rumble |
| HostTinyUSB_FTDI | FTDI FT232R bridge (ECHO/CROSS/TX_FLOOD) |
| HostTinyUSB_Gamepad | DS4 gamepad + LED/rumble |
| HostTinyUSB_HID_Boot | HID boot kbd+mouse concurrent via hub |
| HostTinyUSB_Info | Descriptor dumps (`d/g/h/j/p` keys) for any device |
| HostTinyUSB_Joystick | HID joysticks |
| HostTinyUSB_Keyboard | HID boot keyboard |
| HostTinyUSB_MIDI | USB-MIDI packet echo |
| HostTinyUSB_MIDIMonitor | Real MIDI monitor + CC decode |
| HostTinyUSB_Mouse | HID mouse (boot protocol, verified with small LS trackball) |
| HostTinyUSB_MSC | Mass Storage bulk (BOT, FS) — raw SCSI bench vs Pico 2 RAM disk (128 KB) |
| HostTinyUSB_MSC_FatFS | Mass Storage + FATFileSystem (FatFs/Chan via mbed) bench (64 KB file, 4 KB chunks) |
| HostTinyUSB_PL2303 | PL2303 bridge |
| HostTinyUSB_TriCDC | 3× CDC ACM concurrent via hub |
| HostTinyUSB_XInput | **EXPERIMENTAL (WIP)** Xbox controller bring-up |

## Project Summary

This project is an exploration of AI coding for embedded software development.
Most of this was done using the AI coding agent OpenCode and Muse Spark LLM.

The examples are more engineering tests of the TinyUSB port than user friendly
examples.

FTDI, PL2303, and CP2102 work but only for the boards on hand. There are many
variations with different USB Product IDs so you may need to modify the
examples for your boards. CDC ACM was tested using an RP2040 emulating a CDC
ACM device.

MIDI and MIDIMonitor work but only tested with one real MIDI device. An RP2040
emulating a MIDI device was used for MIDI echo testing.

One low-speed keyboard and one low-speed mouse work. Fancy gaming keyboards and
mice probably require modification of the examples. Two Logitech/Saitek and one
Thrustmaster stick work. The example must be modified to parse the output of
other sticks. There is no HID report descriptor parser.

DS4 works but touchpad and motion controls not tested. LED and rumble OK.

DualSense (PS5 gamepad) maybe works but touchpad and motion controls not
tested. LED and rumble might be OK. The gamepad was available only for a short
time so very little testing was done. On the other hand, it is similar to the
DS4.

XInput (Xbox gamepad) unfinished. No plans to finish this.

Mass Storage (MSC and MSC_FatFS) work but with a Pi Pico 2 emulating a USB RAM
drive. Not tested with real Flash drives. Throughput is impressive but so far
only on an emulated device.

Hubs do not work well. One unpowered hub sort of works. Two different powered
hubs do not work at all.

For more details on TinyUSB throughput see AGENTS.md.

No plans to make this work on Giga Zephyr core.

## Sources

Pinned TinyUSB tree flattened to `src/` from `gigatuh` `1adfb1c` (see `src/VERSION`; MIT,
host-only prune, MSC added per request — MIDI uses standard `tuh_midi_packet_write` return for flow-control). Do not edit vendored files under `src/common/`, `src/host/`, `src/class/`, `src/portable/`, `src/osal/` — port glue lives in
`src/GigaTinyUSB/bsp_giga.cpp` (GPIOB CLK + PB_14/15 AF12 + OTGHS CLK + IRQ +
`millis()` timebase; no ULPI clock — Giga has no ULPI PHY).
