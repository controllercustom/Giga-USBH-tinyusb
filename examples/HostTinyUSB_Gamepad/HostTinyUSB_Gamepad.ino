// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_Gamepad — Giga USB-A Sony DualShock 4 host via TinyUSB,
 * verified with a real DS4 v2 (054C:09CC). Decode map mirrors the proven
 * Giga_USBH_DS4 Mbed project (~/Giga_USBH_DS4 DS4ReportDecoder).
 *
 * Wiring:
 *   Giga --- USB-A ---> DS4 USB (wired)
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Input (64 B report ID 0x01): LX/LY/RX/RY sticks, hat, 14 buttons,
 * PS/touchpad, L2/R2 triggers, 6-bit counter, battery. Prints on content
 * change only (counter byte masked — it increments every report).
 * Output (keys): r/g/b = lightbar red/green/blue, v = rumble small+large
 * 800 ms (report ID 0x05, 32 B, same layout as DS4GamepadHost).
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

static uint8_t prev_rep[64] = {0};
static bool have_rep = false;
static uint8_t cur_led[3] = {0, 0, 0};

static const char *ds4Hat(uint8_t h)
{
  static const char *dirs[9] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "center"};
  return dirs[h <= 8 ? h : 8];
}

static void handleDS4Report(const uint8_t *r)
{
  char line[192];
  int o = snprintf(line, sizeof(line),
    "[TUH-DS4] LX=%3u LY=%3u RX=%3u RY=%3u Hat=%-6s L2=%3u R2=%3u Cnt=%2u Batt=%02x Btn=[",
    r[1], r[2], r[3], r[4], ds4Hat(r[5] & 0x0F), r[8], r[9],
    (r[7] >> 2) & 0x3F, r[30]);
  uint8_t b5 = r[5];
  uint8_t b6 = r[6];
  uint8_t b7 = r[7];
  const char *names[14] = {"SQ", "CR", "CI", "TR", "L1", "R1", "L2B", "R2B",
                           "SH", "OPT", "L3", "R3", "PS", "TP"};
  uint8_t bits[14] = {
    (uint8_t)(b5 & 0x10 ? 1 : 0), (uint8_t)(b5 & 0x20 ? 1 : 0),
    (uint8_t)(b5 & 0x40 ? 1 : 0), (uint8_t)(b5 & 0x80 ? 1 : 0),
    (uint8_t)(b6 & 0x01 ? 1 : 0), (uint8_t)(b6 & 0x02 ? 1 : 0),
    (uint8_t)(b6 & 0x04 ? 1 : 0), (uint8_t)(b6 & 0x08 ? 1 : 0),
    (uint8_t)(b6 & 0x10 ? 1 : 0), (uint8_t)(b6 & 0x20 ? 1 : 0),
    (uint8_t)(b6 & 0x40 ? 1 : 0), (uint8_t)(b6 & 0x80 ? 1 : 0),
    (uint8_t)(b7 & 0x01 ? 1 : 0), (uint8_t)(b7 & 0x02 ? 1 : 0)
  };
  for (int i = 0; i < 14; i++)
  {
    if (bits[i]) o += snprintf(line + o, sizeof(line) - o, "%s ", names[i]);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  GIGATINYUSB_CONSOLE.println(line);
}

// Same 32 B output layout as DS4GamepadHost (report ID 0x05), but sent via
// INTERRUPT OUT: real DS4v2 (09CC) STALLs control SET_REPORT (verified:
// raw control returns XFER_RESULT_STALLED). tuh_hid_send_report prepends
// the ID, so pass the 31 payload bytes (Mbed ctrl[0..30]).
static void sendOutput(uint8_t r, uint8_t g, uint8_t b, uint8_t small, uint8_t large)
{
  static uint8_t out[31];
  memset(out, 0, sizeof(out));
  out[0] = 0xFF;
  out[1] = 0x00;
  out[2] = 0x00;
  out[3] = small;
  out[4] = large;
  out[5] = r;
  out[6] = g;
  out[7] = b;
  cur_led[0] = r;
  cur_led[1] = g;
  cur_led[2] = b;
  if (tuh_hid_mounted(1, 0))
  {
    bool ok = tuh_hid_send_report(1, 0, 0x05, out, sizeof(out));
    char sline[64];
    snprintf(sline, sizeof(sline), "[TUH-DS4] send_report -> %d", (int) ok);
    GIGATINYUSB_CONSOLE.println(sline);
  }
  else
  {
    GIGATINYUSB_CONSOLE.println("[TUH-DS4] send_report skipped (not mounted)");
  }
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB DS4 gamepad ###");
  GIGATINYUSB_CONSOLE.println("Keys: r/g/b=LED red/green/blue, v=rumble 800ms, 0=LED off");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  while (GIGATINYUSB_CONSOLE.available())
  {
    char c = (char) GIGATINYUSB_CONSOLE.read();
    if (c == 'r' || c == 'R')
    {
      sendOutput(255, 0, 0, 0, 0);
      GIGATINYUSB_CONSOLE.println("[TUH-DS4] LED red");
    }
    else if (c == 'g' || c == 'G')
    {
      sendOutput(0, 255, 0, 0, 0);
      GIGATINYUSB_CONSOLE.println("[TUH-DS4] LED green");
    }
    else if (c == 'b' || c == 'B')
    {
      sendOutput(0, 0, 255, 0, 0);
      GIGATINYUSB_CONSOLE.println("[TUH-DS4] LED blue");
    }
    else if (c == '0')
    {
      sendOutput(0, 0, 0, 0, 0);
      GIGATINYUSB_CONSOLE.println("[TUH-DS4] LED off");
    }
    else if (c == 'v' || c == 'V')
    {
      sendOutput(cur_led[0], cur_led[1], cur_led[2], 255, 255);
      GIGATINYUSB_CONSOLE.println("[TUH-DS4] rumble 800ms");
      delay(800);
      sendOutput(cur_led[0], cur_led[1], cur_led[2], 0, 0);
    }
  }
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    char tbuf[64];
    snprintf(tbuf, sizeof(tbuf), "tick ds4=%d", tuh_hid_mounted(1, 0) ? 1 : 0);
    GIGATINYUSB_CONSOLE.println(tbuf);
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  uint16_t vid = 0;
  uint16_t pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  char line[64];
  snprintf(line, sizeof(line), "[TUH-DS4] mount daddr=%u vid=%04x pid=%04x", daddr, vid, pid);
  GIGATINYUSB_CONSOLE.println(line);
  have_rep = false;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  have_rep = false;
  GIGATINYUSB_CONSOLE.println("[TUH-DS4] unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[TUH-DS4] hid mount daddr=%u idx=%u", daddr, idx);
  GIGATINYUSB_CONSOLE.println(line);
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void) daddr;
  (void) idx;
  have_rep = false;
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx == 0 && len >= 31 && report[0] == 0x01)
  {
    // Compare with counter masked (bits 2-7 of byte 7 increment per report).
    uint8_t a[64];
    uint8_t b[64];
    memcpy(a, report, 31);
    memcpy(b, prev_rep, 31);
    a[7] &= 0x03;
    b[7] &= 0x03;
    if (!have_rep || memcmp(a, b, 31) != 0)
    {
      memcpy(prev_rep, report, 31);
      have_rep = true;
      handleDS4Report(report);
    }
  }
  tuh_hid_receive_report(daddr, idx); // re-arm
}
}
