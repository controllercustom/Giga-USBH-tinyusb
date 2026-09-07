// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * EXPERIMENTAL (WIP): endpoint open/init handshake incomplete.
 * HostTinyUSB_XInput — Giga USB-A XInput/GIP gamepad sniffer via TinyUSB raw
 * bulk endpoints (no upstream XInput host driver exists).
 * Verified with Microsoft Xbox Adaptive Controller (045E:0B0A): 3 vendor
 * interfaces (FF/47/D0), bulk 64 B endpoints; interface 0: OUT 0x02 + IN 0x82.
 *
 * Wiring:
 *   Giga --- USB-A ---> XInput gamepad
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Behavior: on mount, parses the config descriptor for interface 0 bulk
 * IN/OUT endpoints, opens them, and hex-dumps IN reports on change with
 * packet counters. If nothing flows, the device likely needs an init
 * handshake (Xbox One family sometimes wants LED/power packets first).
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

static uint8_t in_ep_addr = 0;
static uint8_t out_ep_addr = 0;
static uint8_t in_buf[64];
static uint8_t prev_in[64];
static bool have_in = false;
static uint32_t rx_packets = 0;
static uint32_t rx_bytes = 0;
static uint32_t in_errors = 0;
static uint8_t cfg_buf[256];

static void printHex(const char *tag, const uint8_t *b, int n)
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

static void in_complete_cb(tuh_xfer_t *xfer);

static void post_in_read(void)
{
  static tuh_xfer_t rxfer;
  rxfer.daddr = 1;
  rxfer.ep_addr = in_ep_addr;
  rxfer.buffer = in_buf;
  rxfer.buflen = sizeof(in_buf);
  rxfer.complete_cb = in_complete_cb;
  rxfer.user_data = 0;
  if (!tuh_edpt_xfer(&rxfer))
  {
    GIGATINYUSB_CONSOLE.println("[TUH-XIN] post read FAILED");
  }
}

static void in_complete_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS || xfer->actual_len == 0)
  {
    in_errors++;
    post_in_read(); // keep polling (NAK-equivalent / short reads)
    return;
  }
  rx_packets++;
  rx_bytes += xfer->actual_len;
  int n = xfer->actual_len < 64 ? xfer->actual_len : 64;
  if (!have_in || memcmp(in_buf, prev_in, n) != 0)
  {
    memcpy(prev_in, in_buf, n);
    have_in = true;
    char tag[48];
    snprintf(tag, sizeof(tag), "[TUH-XIN] IN(%u): ", xfer->actual_len);
    printHex(tag, in_buf, n);
  }
  post_in_read(); // re-arm
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB XInput sniffer ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick mounted=%d epin=%02x epout=%02x rxpk=%lu rxb=%lu err=%lu",
      tuh_mounted(1) ? 1 : 0, in_ep_addr, out_ep_addr,
      (unsigned long)rx_packets, (unsigned long)rx_bytes, (unsigned long)in_errors);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick = millis();
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
  snprintf(line, sizeof(line), "[TUH-XIN] mount daddr=%u vid=%04x pid=%04x", daddr, vid, pid);
  GIGATINYUSB_CONSOLE.println(line);
  have_in = false;
  rx_packets = 0;
  rx_bytes = 0;
  in_errors = 0;
  // Fetch config for ANY device (don't gate on VID: VID:PID reads can lag
  // mount); parse looks for interface-0 bulk endpoints specifically.
  memset(cfg_buf, 0, sizeof(cfg_buf));
  tuh_descriptor_get_configuration(daddr, 0, cfg_buf, sizeof(cfg_buf), cfg_fetched_cb, 0);
}

// Walk a config descriptor for interface 0 bulk IN/OUT endpoints, open them,
// and post the first IN read.
void cfg_fetched_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    GIGATINYUSB_CONSOLE.println("[TUH-XIN] config fetch failed");
    return;
  }
  uint16_t total = (uint16_t)cfg_buf[2] | ((uint16_t)cfg_buf[3] << 8);
  if (total > sizeof(cfg_buf))
  {
    total = sizeof(cfg_buf);
  }
  uint8_t cur_intf = 0xFF;
  uint16_t pos = 0;
  while (pos + 2 <= total)
  {
    uint8_t len = cfg_buf[pos];
    uint8_t type = cfg_buf[pos + 1];
    if (len < 2 || pos + len > total) break;
    if (type == 4 && len >= 9)
    {
      // interface
      cur_intf = cfg_buf[pos + 2];
    }
    else if (type == 5 && len >= 7 && cur_intf == 0)
    {
      // endpoint, if0
      uint8_t ep = cfg_buf[pos + 2];
      uint8_t attr = cfg_buf[pos + 3];
      if ((attr & 0x03) == 0x02)
      {
        // bulk
        if (ep & 0x80)
        {
          in_ep_addr = ep;
        }
        else
        {
          out_ep_addr = ep;
        }
      }
    }
    pos += len;
  }
  char line[96];
  snprintf(line, sizeof(line), "[TUH-XIN] if0 bulk EPIN=%02x EPOUT=%02x",
    in_ep_addr, out_ep_addr);
  GIGATINYUSB_CONSOLE.println(line);
  if (in_ep_addr == 0)
  {
    return;
  }
  // tuh_edpt_open needs endpoint descriptors; rebuild minimal 7 B descs
  // (address + bulk attr + 64 B MPS, matching the live dump).
  static uint8_t epin_desc[7] = {7, 5, 0, 2, 64, 0, 4};
  static uint8_t epout_desc[7] = {7, 5, 0, 2, 64, 0, 4};
  epin_desc[2] = in_ep_addr;
  if (!tuh_edpt_open(xfer->daddr, (tusb_desc_endpoint_t *)epin_desc))
  {
    GIGATINYUSB_CONSOLE.println("[TUH-XIN] EPIN open failed");
    return;
  }
  if (out_ep_addr != 0)
  {
    epout_desc[2] = out_ep_addr;
    if (!tuh_edpt_open(xfer->daddr, (tusb_desc_endpoint_t *)epout_desc))
    {
      GIGATINYUSB_CONSOLE.println("[TUH-XIN] EPOUT open failed");
    }
  }
  post_in_read();
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  in_ep_addr = 0;
  out_ep_addr = 0;
  have_in = false;
  GIGATINYUSB_CONSOLE.println("[TUH-XIN] unmounted");
}
}
