# AGENTS.md — Giga-USBH-tinyusb

## What this project is

Standalone TinyUSB USB-A host stack for the Arduino GIGA R1 WiFi. Replaces
`Arduino_USBHostMbed5` with no mbed USB code. Mbed owns clocks/RCC, USB-C
CDC console (`Serial` @115200), and SysTick. TinyUSB owns the USB-A host
port only (OTG_HS, rhport 1, embedded FS PHY).

## Build

```bash
./scripts/build.sh                          # all 16 examples
./scripts/build.sh examples/HostTinyUSB_Info  # single example
./scripts/upload_giga.sh tinyusb-info       # compile + J-Link flash
```

Requires `arduino-cli` with `arduino:mbed_giga` core installed. Also builds in Arduino IDE
(no extra `-I` needed) — install as library via `Arduino/libraries/Giga-USBH-tinyusb`.

## Architecture

- `src/GigaTinyUSB.h` — public API: `gigaTinyUSB_enableHostPort()`, `tuh_giga_init()`, `tuh_giga_probe()`
- `src/GigaTinyUSB/bsp_giga.cpp` — BSP glue: GPIOB CLK, PB_14/15 AF12, OTG_HS CLK, IRQ, `millis()` timebase
- `src/tusb_config.h` — TinyUSB config (MCU, OS, CDC FIFO sizes, line coding on enum)
 - `src/` — flattened TinyUSB tree (`tusb.h`, `common/`, `host/`, `class/`, `portable/`, `osal/`) from `gigatuh` `1adfb1c` (host-only prune, MIT). **Do not edit** vendored dirs (MSC added per request).
 - `examples/` — 16 example sketches (`HostTinyUSB_MSC`, `_MSC_FatFS`, `_TriCDC`, `_HID_Boot`)
- `scripts/build.sh`, `scripts/upload_giga.sh` — build and flash
- `test/watch_tinyusb.sh` — CDC console monitor

## Code style

Arduino community consensus:
- **Allman braces** (opening `{` on its own line) for functions and control structures
- **`else`/`else if`** on separate line from closing `}`
- **2-space indent**
- **camelCase** for functions/variables, **UPPER_SNAKE_CASE** for defines
- Split multi-statement one-liners onto separate lines
- No comments unless asked

## Port glue rules

- Do not edit files under `src/common/`, `src/host/`, `src/class/`, `src/portable/`, `src/osal/` — pinned vendor tree (`gigatuh` `1adfb1c`)
- BSP glue lives in `src/GigaTinyUSB/bsp_giga.cpp`
- Mbed still owns: RCC base clocks, USB-C CDC (OTG_FS), SysTick, NVIC core config
- TinyUSB owns: OTG_HS host only (rhport 1)
- VBUS (PA_15) is enabled by the sketch BEFORE `tuh_giga_init()`
- No ULPI clock — Giga has no ULPI PHY chip

## Testing

- ECHO mode: press `e` in CDC console; reports TX/RX throughput and error counters
- CDC FIFO sizes: 1024 bytes (RX and TX)
- `CFG_TUH_CDC_LINE_CODING_ON_ENUM` forces 115200 8N1 at enumeration — prevents
  silent baud-rate mismatch if async set_baudrate fails
- Callback error checking: all mount-time init chains check `xfer->result`
  and stop on failure (prevents silent baud-rate mismatch)

### UART bridge throughput (TX-RX shorted loopback at 115200 8N1)

| Bridge | TX KB/s | RX KB/s | bad | err |
|--------|---------|---------|-----|-----|
| CP2102 | 11.3 | 11.2 | 0 | 0 |
| FTDI FT232R | 11.3 | 11.2 | 0 | 0 |
| PL2303 | 11.2 | 11.1 | 0 | 0 |

All ~99% of theoretical max (11,520 bytes/s). Optimizations applied to all three
examples: removed 200ms ECHO throttle, removed trailing `delay(1)`, writes multiple
64B blocks per iteration to keep TX FIFO full.

### RP2350 CDC ACM throughput (USB FS bulk, no UART bridge)

| Device | TX KB/s | RX KB/s | bad | err |
|--------|---------|---------|-----|-----|
| RP2350 native USB CDC | 50–75 | 50–75 | 0 | 0 |

Much faster because it's USB-to-USB (no UART bottleneck). Prior art in
`~/Giga_USBH/emulators/CDCEcho/` — the custom VID:PID (`0x239A:0xCAFD`) causes
the GIGA host to receive 0 bytes back; use default RP2040 PID for correct operation.
Previous RP2040 measurements were identical; board was replaced with RP2350 (Raspberry Pi Pico 2, 4 MB flash).

### USB FS bulk throughput ceiling

The GIGA's OTG_HS controller (embedded FS PHY) is capable of ~1 MB/s per direction
using multi-packet bulk transfers (DWC2 HCTSIZ supports up to 1023 packets per
channel). The current ~64 KB/s ceiling is a **TinyUSB software limitation**:

- `tu_edpt_stream_open()` hardcodes `xfer_len = mps` (64B) — one packet per transfer
- USBH enforces one in-flight transfer per endpoint (BUSY flag)
- ISR→callback→re-submit round-trip costs 1 frame (1ms) of dead time
- No `CFG_TUH_*` options exist for multi-packet bulk batching

The DWC2 hardware supports 8-deep non-periodic request queue and multi-packet
channels, but TinyUSB's stream API never programs `packet_count > 1`.

### USB-MIDI echo to RP2040 (USB FS interrupt/bulk, 4-byte MIDI packets)

| Mode | TX KB/s | RX KB/s | loss |
|------|---------|---------|------|
| flush+task flood (64 pkts/loop) | ~183 | ~150 | ~15% |
| request-response (1 pkt, wait for echo) | 53.6 | 53.6 | 0 |
| batched flow-control (64 pkts/batch, `tuh_midi_write_available`) | 319 | 319 | 0 |

The MIDI device echo is bounded by the USB FS echo round-trip. Flooding the
host TX stream (flush+task per packet) sustains strong throughput but overruns
the RP2040 device's TX FIFO (512B = 128 pkts), causing it to NAK IN polls and
drop ~15% of echoes (device CPU clock 133 vs 200 MHz made no difference).

To get **zero loss**, the request-response baseline in `HostTinyUSB_MIDI.ino`
writes one 4-byte packet, flushes, then spins on `tuh_task()` + drain RX until
the echo arrives (5 ms timeout). Batching reclaims throughput: the current
lossless mode writes 64 packets gated by `tuh_midi_packet_write()` return
(standard API, FIFO full = `false`, no custom `tuh_midi_write_available`),
flushes once, then blocks until all 64 echoes return (20 ms timeout). This
batches 64×4B = 256B per bulk transfer, sustaining **319 KB/s TX==RX** lossless
(verified 22 s, 7 MB, pkts == rx/4). The display rate counter now uses 64-bit
math to avoid 32-bit overflow past 4 MB.

### USB-MIDI echo to RP2350 (USB FS interrupt/bulk, 4-byte MIDI packets)

| Mode | TX KB/s | RX KB/s | loss |
|------|---------|---------|------|
| batched flow-control (64 pkts/batch, `tuh_midi_write_available`) | 374 | 374 | 0 |

Same host batching as above, re-verified after replacing RP2040 with RP2350
(Raspberry Pi Pico 2, 4 MB flash, 150 MHz, FQBN `rp2040:rp2040:rpipico2`).
Throughput rises to **374 KB/s TX==RX** lossless (verified 24 s, 9 MB,
`elapsed=24s tx=9201664 rx=9201664 pkts=2300416`).

### USB Mass Storage throughput (BOT bulk, FS, Pi Pico 2 RAM disk 128 KB)

| Mode | Read KB/s | Write KB/s |
|------|-----------|------------|
| `tuh_msc_read10` 8 blocks/xfer (4096 B) | 973–975 | — |

Raw SCSI bench via `HostTinyUSB_MSC.ino` (8×512 B per `READ10` CBW, `tuh_task()` polled
while awaiting `tuh_msc_complete_cb_t`). Pico 2 MSC device is `MSCRamDisk.ino`
(`rp2040:rp2040:rpipico2:usbstack=tinyusb`, 256×512 B = 128 KB RAM disk,
VID:PID `0x239A:0xCAFC`). Host `CFG_TUH_MSC_EP_BUFSIZE 512` matches FS mps 64;
BOT uses direct bulk `usbh_edpt_xfer` (512 B), not `tu_edpt_stream` (64 B),
so the 64 KB/s stream ceiling does **not** apply — sustained **≈975 KB/s**
(`elapsed=22s bytes=21962752`, wraps every 256 blocks) vs FS theoretical ~1 MB/s.

FatFS bench via `HostTinyUSB_MSC_FatFS.ino` (mbed `FATFileSystem` over `mbed::BlockDevice`
wrapping `tuh_msc_read10/write10`, 64 KB `bench.bin`, 4 KB chunks) sustains
**≈592 KB/s write / 842 KB/s read** (`write 65536 B in 108 ms`, `read 65536 B in 76 ms`,
FAT12 overhead vs raw 975 KB/s). `mbed::BlockDevice::get_erase_size(addr)` override
required for `reformat()` (`-22` without it).

### USB HID boot mouse (4-byte reports, 10 ms poll)

`HostTinyUSB_Mouse.ino` (boot protocol `SET_PROTOCOL 0`, `tuh_hid_receive_report`)
verified with small LS trackball direct to GIGA A (FS PHY, `bInterval 10`,
`wMaxPacketSize 8`). Prints `dx/dy/wheel` + `LEFT/MIDDLE/RIGHT` on change;
sustains `100 Hz` `INTR` via `FS` without hub — boot-only.
