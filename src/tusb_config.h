// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/* tusb_config.h — TinyUSB configuration for Arduino GIGA R1 WiFi USB-A host.
 *
 * Role split (deliberate):
 *   - Mbed owns: clocks/RCC, USB-C CDC console (OTG_FS device), PA_15 VBUS
 *     (enabled by the sketch via GigaTinyUSB.h BEFORE tuh_init).
 *   - TinyUSB owns: USB-A host only (OTG_HS, rhport 1, embedded FS PHY).
 *     dwc2_clock_init() upstream is a no-op stub, so no clock conflicts.
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU            OPT_MCU_STM32H7
#define CFG_TUSB_OS             OPT_OS_NONE

// USB-A host on OTG_HS (rhport 1). Giga has no ULPI PHY chip, so the HS core
// runs via its embedded full-speed PHY.
#define CFG_TUSB_RHPORT1_MODE   (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

// Force 115200 8N1 at enumeration time (before mount callback). The CP2102
// POR-defaults to 921600 baud — without this, a failed set_baudrate() leaves
// the device at 921600 and the sketch silently runs at the wrong rate.
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM  { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

#define CFG_TUH_ENABLED         1
#define CFG_TUH_CDC             4

// FTDI FT232R (0403:6001) via upstream CDC-host FTDI driver (cdc_host.c).
// Default VID:PID list already includes 0403:6001.
// NOTE: do NOT define CFG_TUH_CDC_FTDI_LATENCY (chip POR default 16 ms is
// fine for bringup) — defining it trips an upstream bug in cdc_host.c
// ftdi_process_set_config() (wrong callback name ftdi_process_config +
// declaration after case label). Revisit with a documented patch if 1 ms
// latency is needed for throughput.
#define CFG_TUH_CDC_FTDI        1

// PL2303 (067B:2303 etc.) via upstream CDC-host PL2303 driver. Uses standard
// CDC line coding (baud+8N1 in one request); bulk data has no status header.
#define CFG_TUH_CDC_PL2303      1

// CP210x (10C4:EA60 etc.) via upstream CDC-host CP210x driver. Baud + data
// format are separate requests (like FTDI); bulk data has no status header.
#define CFG_TUH_CDC_CP210X      1

// USB MIDI host (upstream class driver). 1 KB FIFOs to match CDC.
#define CFG_TUH_MIDI            1
#define CFG_TUH_MIDI_RX_BUFSIZE 1024
#define CFG_TUH_MIDI_TX_BUFSIZE 1024

// USB Mass Storage host (BOT, full-speed only on GIGA — no HS ULPI).
#define CFG_TUH_MSC             1
#define CFG_TUH_MSC_MAXLUN      1
#define CFG_TUH_MSC_EP_BUFSIZE  512

// USB hub support: 4-port USB3.0 HS hub on GIGA FS host → TT overhead,
// needs device slots for hub + downstream devices.
#define CFG_TUH_HUB             1
#define CFG_TUH_HUB_BUFSIZE     64
#define CFG_TUH_DEVICE_MAX      5

// USB HID host (upstream class driver): boot keyboards/mice. Used for real
// HID hardware that the Mbed stack cannot enumerate.
// NOTE: value = max HID *interfaces* (e.g., gaming mouse with 2: mouse + side).
#define CFG_TUH_HID             4

// Stream FIFOs: upstream defaults are 64 B (one bulk packet). At 200 ms
// drain cadence + bursty FTDI latency-timer delivery, 64 B overflows on
// phase alignment and silently drops bytes (permanent seq desync in tests).
// 1024 B keeps the UART pipeline full at 115200 baud (~1.4 KB/s per dir).
#define CFG_TUH_CDC_RX_BUFSIZE  1024
#define CFG_TUH_CDC_TX_BUFSIZE  1024

// Quiet stack logs; the sketch prints its own mount/descriptor lines.
#define CFG_TUSB_DEBUG          0

#ifdef __cplusplus
}
#endif

#endif
