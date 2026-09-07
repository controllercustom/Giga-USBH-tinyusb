// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_Joystick — Giga USB-A USB-HID joystick host via TinyUSB.
 * Decodes per-device layouts (detected by VID:PID at mount):
 *   046D:C215 Logitech Extreme 3D Pro: 7-byte reports (10-bit packed X/Y,
 *     4-bit hat, Rz twist, 12 buttons, throttle) at 10 ms.
 *   04D9:011B Holtek game controller + 044F:B10A Thrustmaster stick:
 *     9-byte reports (16 buttons, 4-bit hat, 14-bit X/Y, 8-bit Rz +
 *     slider) at 10 ms (identical DragonRise-style layout).
 *   06A3:075C Saitek X52 stick: 14-byte reports (11-bit X/Y, 10-bit Rz,
 *     Z/Rx/Ry/slider bytes, 34 buttons, 1-8 hat, ministick nibbles).
 * Anything else: raw hex dump on change.
 *
 * Wiring:
 *   Giga --- USB-A ---> joystick (HID)
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Prints on report CHANGE only (10 ms polling would flood otherwise).
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

static uint8_t prev_rep[16] = {0};
static bool have_rep = false;
static uint16_t joy_vid = 0;
static uint16_t joy_pid = 0;

static const char *hatNameX52(uint8_t h)
{
  // X52 hat values are 1-8 (N..NW clockwise); anything else = centered.
  static const char *dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (h >= 1 && h <= 8)
  {
    return dirs[h - 1];
  }
  return "center";
}

static const char *hatName(uint8_t h)
{
  switch (h & 0x0F)
  {
    case 0: return "N";
    case 1: return "NE";
    case 2: return "E";
    case 3: return "SE";
    case 4: return "S";
    case 5: return "SW";
    case 6: return "W";
    case 7: return "NW";
    default: return "center";
  }
}

static void handleJoyReport(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[0] | ((uint16_t)(r[1] & 0x03) << 8);
  uint16_t y = ((uint16_t)(r[1] >> 2) & 0x3F) | ((uint16_t)(r[2] & 0x0F) << 6);
  uint8_t hat = (r[2] >> 4) & 0x0F;
  uint8_t rz = r[3];
  uint8_t b18 = r[4];
  uint8_t slider = r[5];
  uint8_t b912 = r[6] & 0x0F;
  char line[160];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%4u Y=%4u Hat=%-6s Rz=%3u Thr=%3u Btn=[", x, y, hatName(hat), rz, slider);
  for (int b = 0; b < 8; b++)
  {
    if (b18 & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  for (int b = 0; b < 4; b++)
  {
    if (b912 & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 9);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  GIGATINYUSB_CONSOLE.println(line);
}

// Holtek 04D9:011B / Thrustmaster 044F:B10A layout (from live 124 B report
// descriptor; both share the same DragonRise-style 9-byte reports), 9 bytes:
//   b0=Btn1-8, b1=Btn9-16, b2=Hat(lo4)+pad, b3=X[7:0],
//   b4=X[13:8](lo6)+pad, b5=Y[7:0], b6=Y[13:8](lo6)+pad, b7=Rz, b8=Slider.
// X/Y 14-bit (0-16383).
static void handleHoltekReport(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[3] | ((uint16_t)(r[4] & 0x3F) << 8);
  uint16_t y = (uint16_t)r[5] | ((uint16_t)(r[6] & 0x3F) << 8);
  uint8_t hat = r[2] & 0x0F;
  char line[160];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%5u Y=%5u Hat=%-6s Rz=%3u Thr=%3u Btn=[",
    x, y, hatName(hat), r[7], r[8]);
  for (int b = 0; b < 8; b++)
  {
    if (r[0] & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  for (int b = 0; b < 8; b++)
  {
    if (r[1] & (1u << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 9);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  GIGATINYUSB_CONSOLE.println(line);
}

// Saitek X52 stick 06A3:075C layout (from live 119 B report descriptor),
// 14-byte reports: X/Y 11-bit, Rz 10-bit, Z/Rx/Ry/Slider 8-bit,
// 34 buttons, 4-bit hat (values 1-8, else centered), ministick nibbles.
static void handleX52Report(const uint8_t *r)
{
  uint16_t x = (uint16_t)r[0] | ((uint16_t)(r[1] & 0x07) << 8);
  uint16_t y = ((uint16_t)(r[1] >> 3) & 0x1F) | ((uint16_t)(r[2] & 0x3F) << 5);
  uint16_t rz = ((uint16_t)(r[2] >> 6) & 0x03) | ((uint16_t)r[3] << 2);
  uint8_t hat = (r[12] >> 4) & 0x0F;
  uint64_t btn = (uint64_t)r[8] | ((uint64_t)r[9] << 8) |
                 ((uint64_t)r[10] << 16) | ((uint64_t)r[11] << 24);
  uint8_t b3334 = r[12] & 0x03;
  if (b3334 & 0x01) btn |= (1ULL << 32);
  if (b3334 & 0x02) btn |= (1ULL << 33);
  char line[192];
  int o = snprintf(line, sizeof(line),
    "[TUH-JOY] X=%4u Y=%4u Hat=%-6s Rz=%4u Z=%3u Rx=%3u Ry=%3u Sl=%3u Mini=%X%X Btn=[",
    x, y, hatNameX52(hat), rz, r[4], r[5], r[6], r[7],
    r[13] & 0x0F, (r[13] >> 4) & 0x0F);
  for (int b = 0; b < 34; b++)
  {
    if (btn & (1ULL << b)) o += snprintf(line + o, sizeof(line) - o, "%d ", b + 1);
  }
  if (o > 0 && line[o - 1] == ' ')
  {
    line[o - 1] = '\0';
    o--;
  }
  snprintf(line + o, sizeof(line) - o, "]");
  GIGATINYUSB_CONSOLE.println(line);
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB joystick ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    GIGATINYUSB_CONSOLE.println("tick");
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  tuh_vid_pid_get(daddr, &joy_vid, &joy_pid);
  char line[64];
  snprintf(line, sizeof(line), "[TUH-JOY] mount daddr=%u vid=%04x pid=%04x", daddr, joy_vid, joy_pid);
  GIGATINYUSB_CONSOLE.println(line);
  have_rep = false;
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  have_rep = false;
  GIGATINYUSB_CONSOLE.println("[TUH-JOY] unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[TUH-JOY] hid mount daddr=%u idx=%u", daddr, idx);
  GIGATINYUSB_CONSOLE.println(line);
  // Vendor HID (03/00/00): stay in REPORT protocol (default), just stream.
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
  if (idx != 0)
  {
    tuh_hid_receive_report(daddr, idx);
    return;
  }
  // Lazy VID:PID (mount-callback timing isn't guaranteed for late readers).
  if (joy_vid == 0)
  {
    bool okv = tuh_vid_pid_get(daddr, &joy_vid, &joy_pid);
    static bool vid_logged = false;
    if (!vid_logged)
    {
      vid_logged = true;
      char vline[64];
      snprintf(vline, sizeof(vline), "[TUH-JOY] vid_pid_get -> %d vid=%04x pid=%04x",
        (int) okv, joy_vid, joy_pid);
      GIGATINYUSB_CONSOLE.println(vline);
    }
  }
  if (joy_vid == 0x046D && joy_pid == 0xC215 && len >= 7)
  {
    // Logitech Extreme 3D Pro: 7-byte packed layout.
    if (!have_rep || memcmp(report, prev_rep, 7) != 0)
    {
      memcpy(prev_rep, report, 7);
      have_rep = true;
      handleJoyReport(report);
    }
  }
  else if (((joy_vid == 0x04D9 && joy_pid == 0x011B) ||
               (joy_vid == 0x044F && joy_pid == 0xB10A)) && len >= 9)
  {
    // Holtek / Thrustmaster 9-byte layout (see handleHoltekReport).
    if (!have_rep || memcmp(report, prev_rep, 9) != 0)
    {
      memcpy(prev_rep, report, 9);
      have_rep = true;
      handleHoltekReport(report);
    }
  }
  else if (joy_vid == 0x06A3 && joy_pid == 0x075C && len >= 14)
  {
    // Saitek X52 stick: 14-byte layout (see handleX52Report).
    if (!have_rep || memcmp(report, prev_rep, 14) != 0)
    {
      memcpy(prev_rep, report, 14);
      have_rep = true;
      handleX52Report(report);
    }
  }
  else if (len > 0 && len <= 16)
  {
    // Unknown stick: raw hex on change.
    if (!have_rep || memcmp(report, prev_rep, len) != 0)
    {
      memcpy(prev_rep, report, len);
      have_rep = true;
      char line[96];
      int o = snprintf(line, sizeof(line), "[TUH-JOY] raw(%u) vid=%04x pid=%04x: ", len, joy_vid, joy_pid);
      for (int i = 0; i < len && o < 90; i++)
      {
        o += snprintf(line + o, sizeof(line) - o, "%02x ", report[i]);
      }
      GIGATINYUSB_CONSOLE.println(line);
    }
  }
  tuh_hid_receive_report(daddr, idx); // re-arm
}
}
