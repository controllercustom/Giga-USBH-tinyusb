// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

#pragma once

/*
 * GigaTinyUSB — TinyUSB USB-A host stack for the Arduino GIGA R1 WiFi.
 *
 * Standalone alternative to Arduino_USBHostMbed5: no mbed USB code, no
 * dependency on Arduino_USBHostMbed5. Mbed still owns clocks/RCC, the USB-C
 * CDC console (OTG_FS) and SysTick; TinyUSB owns the USB-A host port only
 * (OTG_HS, rhport 1, embedded FS PHY).
 *
 * Typical sketch setup:
 *   #include "GigaTinyUSB.h"
 *   void setup() {
 *     GIGATINYUSB_CONSOLE.begin(GIGATINYUSB_BAUD);
 *     gigaTinyUSB_enableHostPort();  // PA_15 HIGH powers USB-A VBUS
 *     tuh_giga_init();               // pins + OTG_HS clocks + tuh_init(1)
 *   }
 *   void loop() { tuh_task(); }
 */

#include "Arduino.h"
#include "tusb.h"

// USB-A host enable / VBUS pin (per Giga R1 WiFi core variant:
// D92 USB HOST ENABLE).
#ifndef GIGATINYUSB_VBUS_PIN
#define GIGATINYUSB_VBUS_PIN PA_15
#endif

// Debug console — USB-C CDC (Serial). Both USB-A (host) and USB-C (CDC)
// remain active simultaneously.
#ifndef GIGATINYUSB_CONSOLE
#define GIGATINYUSB_CONSOLE Serial
#define GIGATINYUSB_BAUD  115200
#endif

// Convenience: power the USB-A port. Safe to call repeatedly. Call BEFORE
// tuh_giga_init().
inline void gigaTinyUSB_enableHostPort()
{
  pinMode(GIGATINYUSB_VBUS_PIN, OUTPUT);
  digitalWrite(GIGATINYUSB_VBUS_PIN, HIGH);
}

// OTG_HS host bring-up + tuh_init(1). Defined in src/GigaTinyUSB/bsp_giga.cpp.
// Call AFTER gigaTinyUSB_enableHostPort().
void tuh_giga_init(void);

// Read-only clock/PHY state probe ('p' key convention in examples). Never
// hangs, never writes. Defined in src/GigaTinyUSB/bsp_giga.cpp.
void tuh_giga_probe(void);
