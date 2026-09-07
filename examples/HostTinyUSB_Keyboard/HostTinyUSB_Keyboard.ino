// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_Keyboard — Giga USB-A USB-HID boot-keyboard host via TinyUSB,
 * verified with a real Logitech keyboard (046D:C313, EP0=8) that the Mbed
 * USBHostKeyboard stack cannot enumerate (multi-packet CTRL-IN truncation).
 *
 * Wiring:
 *   Giga --- USB-A ---> USB keyboard (HID boot protocol)
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Prints "X pressed" / "X released" per key (letters + digits + space/enter/
 * backspace/tab, Shift-aware), plus tick. Only HID interface 0 (boot
 * keyboard) is decoded; extra consumer-control interfaces are ignored.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

// HID usage ID -> printable char (a-z, 0-9, a few controls). Returns 0 if
// not printable; shift selects the shifted variant for letters/digits.
static char hidUsageToChar(uint8_t usage, bool shift)
{
  if (usage >= 0x04 && usage <= 0x1D)
  {
    return (char)('a' + (usage - 0x04) + (shift ? ('A' - 'a') : 0));
  }
  if (usage >= 0x1E && usage <= 0x27)
  {
    static const char plain[] = "1234567890";
    static const char shifted[] = "!@#$%^&*()";
    return shift ? shifted[usage - 0x1E] : plain[usage - 0x1E];
  }
  switch (usage)
  {
    case 0x28: return '\n'; // Enter
    case 0x2B: return '\t'; // Tab
    case 0x2C: return ' ';  // Space
    case 0x2A: return '\b'; // Backspace
    default: return 0;
  }
}

static uint8_t prev_keys[6] = {0};
static uint8_t prev_mod = 0;

static void handleBootReport(const uint8_t *r)
{
  uint8_t mod = r[0];
  // Modifier changes are not printed as keys (Shift only affects case).
  (void) prev_mod;
  prev_mod = mod;
  bool shift = (mod & 0x22) != 0; // left/right Shift
  // Release: present before, absent now.
  for (int i = 0; i < 6; i++)
  {
    if (prev_keys[i] == 0)
    {
      continue;
    }
    bool still = false;
    for (int j = 0; j < 6; j++)
    {
      if (r[2 + j] == prev_keys[i])
      {
        still = true;
        break;
      }
    }
    if (!still)
    {
      char c = hidUsageToChar(prev_keys[i], shift);
      if (c == '\n')
      {
        GIGATINYUSB_CONSOLE.println("Enter released");
      }
      else if (c == '\t')
      {
        GIGATINYUSB_CONSOLE.println("Tab released");
      }
      else if (c == ' ')
      {
        GIGATINYUSB_CONSOLE.println("Space released");
      }
      else if (c == '\b')
      {
        GIGATINYUSB_CONSOLE.println("Backspace released");
      }
      else if (c)
      {
        char line[16];
        snprintf(line, sizeof(line), "%c released", c);
        GIGATINYUSB_CONSOLE.println(line);
      }
    }
  }
  // Press: present now, absent before.
  for (int j = 0; j < 6; j++)
  {
    uint8_t u = r[2 + j];
    if (u == 0)
    {
      continue;
    }
    bool was = false;
    for (int i = 0; i < 6; i++)
    {
      if (prev_keys[i] == u)
      {
        was = true;
        break;
      }
    }
    if (!was)
    {
      char c = hidUsageToChar(u, shift);
      if (c == '\n')
      {
        GIGATINYUSB_CONSOLE.println("Enter pressed");
      }
      else if (c == '\t')
      {
        GIGATINYUSB_CONSOLE.println("Tab pressed");
      }
      else if (c == ' ')
      {
        GIGATINYUSB_CONSOLE.println("Space pressed");
      }
      else if (c == '\b')
      {
        GIGATINYUSB_CONSOLE.println("Backspace pressed");
      }
      else if (c)
      {
        char line[16];
        snprintf(line, sizeof(line), "%c pressed", c);
        GIGATINYUSB_CONSOLE.println(line);
      }
    }
  }
  for (int i = 0; i < 6; i++)
  {
    prev_keys[i] = r[2 + i];
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB keyboard ###");
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
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-KBD] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  memset(prev_keys, 0, sizeof(prev_keys));
  prev_mod = 0;
  GIGATINYUSB_CONSOLE.println("[TUH-KBD] device unmounted");
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *report_desc, uint16_t desc_len)
{
  (void) report_desc;
  (void) desc_len;
  char line[64];
  snprintf(line, sizeof(line), "[TUH-KBD] hid mount daddr=%u idx=%u", daddr, idx);
  GIGATINYUSB_CONSOLE.println(line);
  if (idx == 0)
  {
    // Boot protocol (0) for the keyboard interface, then start streaming.
    if (!tuh_hid_set_protocol(daddr, idx, 0))
    {
      GIGATINYUSB_CONSOLE.println("[TUH-KBD] set_protocol failed");
    }
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
  (void) daddr;
  (void) idx;
  memset(prev_keys, 0, sizeof(prev_keys));
}

void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t protocol)
{
  (void) protocol;
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
  if (idx == 0 && len >= 8)
  {
    handleBootReport(report);
  }
  tuh_hid_receive_report(daddr, idx); // re-arm
}
}
