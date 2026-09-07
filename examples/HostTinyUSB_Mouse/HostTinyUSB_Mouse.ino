// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_Mouse — Giga USB-A boot HID mouse via TinyUSB
 * Boot protocol only (3 buttons + X/Y + wheel). Prints on change.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

static uint8_t prevb[4] = {0};
static bool haveb = false;

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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB mouse (boot) ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick hid=%d", tuh_hid_mounted(1, 0) ? 1 : 0);
    GIGATINYUSB_CONSOLE.println(tbuf);
  }
  delay(1);
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
  haveb = false;
  GIGATINYUSB_CONSOLE.println("[TUH-MOUSE] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  haveb = false;
  GIGATINYUSB_CONSOLE.println("[TUH-MOUSE] device unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[TUH-MOUSE] hid mount daddr=%u idx=%u", daddr, idx);
  GIGATINYUSB_CONSOLE.println(line);
  if (idx == 0)
  {
    bool okp = tuh_hid_set_protocol(daddr, idx, 0);
    char pline[64];
    snprintf(pline, sizeof(pline), "[TUH-MOUSE] set_protocol idx=%u -> %d", idx, (int) okp);
    GIGATINYUSB_CONSOLE.println(pline);
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void) daddr;
  (void) idx;
  haveb = false;
}

void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t protocol)
{
  (void) protocol;
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx == 0 && len >= 3)
  {
    uint8_t cur[4] = {report[0], report[1], report[2], len > 3 ? report[3] : 0};
    if (!haveb || memcmp(cur, prevb, 4) != 0)
    {
      memcpy(prevb, cur, 4);
      haveb = true;
      uint8_t btn = cur[0];
      char line[96];
      int o = snprintf(line, sizeof(line), "[TUH-MOUSE] move dx=%d dy=%d",
        (int)(int8_t)cur[1], (int)(int8_t)cur[2]);
      if (len > 3) o += snprintf(line + o, sizeof(line) - o, " wheel=%d", (int)(int8_t)cur[3]);
      o += snprintf(line + o, sizeof(line) - o, " buttons=[");
      if (btn & 0x01) o += snprintf(line + o, sizeof(line) - o, "LEFT ");
      if (btn & 0x02) o += snprintf(line + o, sizeof(line) - o, "MIDDLE ");
      if (btn & 0x04) o += snprintf(line + o, sizeof(line) - o, "RIGHT ");
      if (o > 0 && line[o - 1] == ' ') { line[o - 1] = '\0'; o--; }
      snprintf(line + o, sizeof(line) - o, "]");
      GIGATINYUSB_CONSOLE.println(line);
    }
  }
  tuh_hid_receive_report(daddr, idx);
}
}
