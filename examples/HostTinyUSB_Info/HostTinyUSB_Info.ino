// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_Info — discriminant demo: enumerate whatever is plugged into
 * Giga USB-A via the TinyUSB host stack (OTG_HS) and dump its genuine device
 * descriptor bytes over the USB-C CDC console.
 *
 * Wiring:
 *   Giga --- USB-A ---> device under test (FTDI 0403:6001 for the FTDI case)
 *   Pi5  --- USB-C ---> Giga Serial (/dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Stack split: Mbed owns USB-C CDC (Serial) + clocks; TinyUSB owns USB-A
 * host only (rhport 1). tuh_task() is polled from loop() (OPT_OS_NONE).
 *
 * PASS criterion for the FTDI case: mount line shows vid=0403 pid=6001 AND
 * the DESC line shows the full correct 18 bytes:
 *   12 01 00 02 00 00 00 08 03 04 01 60 ...
 * (vs the ST-HAL failure signature: truncated ...08 03 01 + zeros)
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

static uint8_t desc_buf[18];
static uint8_t cfg_buf[256];
static uint8_t rep_buf[512];
static uint8_t rep_itf = 0;
static uint16_t rep_len = 0;

static void print_hex(const char *tag, const uint8_t *b, int n)
{
  GIGATINYUSB_CONSOLE.print(tag);
  for (int i = 0; i < n; i++)
  {
    char hb[4];
    snprintf(hb, sizeof(hb), "%02x ", b[i]);
    GIGATINYUSB_CONSOLE.print(hb);
  }
  GIGATINYUSB_CONSOLE.println();
}

void setup()
{
  GIGATINYUSB_CONSOLE.begin(GIGATINYUSB_BAUD);
  uint32_t t0 = millis();
  while (!GIGATINYUSB_CONSOLE && millis() - t0 < 1500)
  {
    delay(10);
  }
  gigaTinyUSB_enableHostPort();
  delay(1000);
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB host info ###");
  tuh_giga_init();
  // TEMP-BISECT: probe moved to on-demand 'p' key only (setup call removed).
  GIGATINYUSB_CONSOLE.println("[TUH] init rhport=1, waiting for device...");
}

void loop()
{
  tuh_task();
  // Console commands (reader is always attached here, unlike boot prints).
  while (GIGATINYUSB_CONSOLE.available())
  {
    char c = (char) GIGATINYUSB_CONSOLE.read();
    if (c == 'd' || c == 'D')
    {
      if (tuh_mounted(1))
      {
        memset(desc_buf, 0, sizeof(desc_buf));
        tuh_descriptor_get_device(1, desc_buf, sizeof(desc_buf), desc_complete_cb, 0);
      }
      else
      {
        GIGATINYUSB_CONSOLE.println("[TUH] d: no device mounted");
      }
    }
    else if (c == 'g' || c == 'G')
    {
      // Config descriptor dump (up to 128 B): interfaces, HID descriptors,
      // endpoints. Shows composite layouts that break small-host parsers.
      if (tuh_mounted(1))
      {
        memset(cfg_buf, 0, sizeof(cfg_buf));
        tuh_descriptor_get_configuration(1, 0, cfg_buf, sizeof(cfg_buf), cfg_complete_cb, 0);
      }
      else
      {
        GIGATINYUSB_CONSOLE.println("[TUH] g: no device mounted");
      }
    }
    else if (c == 'p' || c == 'P')
    {
      tuh_giga_probe();
    }
    else if (c == 'h' || c == 'H')
    {
      // HID report descriptor read, interface 0, up to 320 B (DS4 148 B,
      // DualSense 289 B).
      if (tuh_mounted(1))
      {
        rep_itf = 0;
        rep_len = 320;
        memset(rep_buf, 0, sizeof(rep_buf));
        tuh_descriptor_get_hid_report(1, 0, 0x22, 0, rep_buf, 320, rep_complete_cb, 0);
      }
      else
      {
        GIGATINYUSB_CONSOLE.println("[TUH] h: no device mounted");
      }
    }
    else if (c == 'j' || c == 'J')
    {
      if (tuh_mounted(1))
      {
        rep_itf = 1;
        rep_len = 160;
        memset(rep_buf, 0, sizeof(rep_buf));
        tuh_descriptor_get_hid_report(1, 1, 0x22, 0, rep_buf, 160, rep_complete_cb, 0);
      }
      else
      {
        GIGATINYUSB_CONSOLE.println("[TUH] j: no device mounted");
      }
    }
  }
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    GIGATINYUSB_CONSOLE.println("tick");
  }
  // Repeating host status (late console readers see it within seconds).
  static uint32_t lastStat = 0;
  if (millis() - lastStat > 5000)
  {
    lastStat = millis();
    GIGATINYUSB_CONSOLE.print("[TUH] status mounted=");
    GIGATINYUSB_CONSOLE.print(tuh_mounted(1) ? "1" : "0");
    GIGATINYUSB_CONSOLE.println(tuh_mounted(1) ? " (device present)" : " (no device)");
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  uint16_t vid = 0;
  uint16_t pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  char line[64];
  snprintf(line, sizeof(line), "[TUH] mount daddr=%u vid=%04x pid=%04x", daddr, vid, pid);
  GIGATINYUSB_CONSOLE.println(line);
  memset(desc_buf, 0, sizeof(desc_buf));
  tuh_descriptor_get_device(daddr, desc_buf, sizeof(desc_buf), desc_complete_cb, 0);
}

void tuh_umount_cb(uint8_t daddr)
{
  char line[48];
  snprintf(line, sizeof(line), "[TUH] unmount daddr=%u", daddr);
  GIGATINYUSB_CONSOLE.println(line);
}

void desc_complete_cb(tuh_xfer_t *xfer)
{
  if (xfer->result == XFER_RESULT_SUCCESS)
  {
    print_hex("[TUH] DESC: ", desc_buf, sizeof(desc_buf));
  }
  else
  {
    char line[48];
    snprintf(line, sizeof(line), "[TUH] DESC failed result=%d", (int) xfer->result);
    GIGATINYUSB_CONSOLE.println(line);
  }
}

void cfg_complete_cb(tuh_xfer_t *xfer)
{
  if (xfer->result == XFER_RESULT_SUCCESS)
  {
    uint16_t total = (uint16_t)cfg_buf[2] | ((uint16_t)cfg_buf[3] << 8);
    if (total > sizeof(cfg_buf))
    {
      total = sizeof(cfg_buf);
    }
    print_hex("[TUH] CFG: ", cfg_buf, total < 64 ? total : 64);
    if (total > 64)
    {
      print_hex("[TUH] CFG+64: ", cfg_buf + 64, total - 64);
    }
  }
  else
  {
    char line[48];
    snprintf(line, sizeof(line), "[TUH] CFG failed result=%d", (int) xfer->result);
    GIGATINYUSB_CONSOLE.println(line);
  }
}

void rep_complete_cb(tuh_xfer_t *xfer)
{
  if (xfer->result == XFER_RESULT_SUCCESS)
  {
    char tag[32];
    snprintf(tag, sizeof(tag), "[TUH] REP itf=%u: ", rep_itf);
    for (uint16_t off = 0; off < rep_len; off += 64)
    {
      char tag2[32];
      if (off == 0)
      {
        snprintf(tag2, sizeof(tag2), "%s", tag);
      }
      else
      {
        snprintf(tag2, sizeof(tag2), "[TUH] REP+%u: ", off);
      }
      uint16_t n = rep_len - off > 64 ? 64 : rep_len - off;
      print_hex(tag2, rep_buf + off, n);
    }
  }
  else
  {
    char line[64];
    snprintf(line, sizeof(line), "[TUH] REP itf=%u failed result=%d", rep_itf, (int) xfer->result);
    GIGATINYUSB_CONSOLE.println(line);
  }
}
}
