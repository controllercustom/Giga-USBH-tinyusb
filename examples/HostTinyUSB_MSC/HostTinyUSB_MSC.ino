// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_MSC — Giga USB-A Mass Storage host bench (BOT, FS only)
 * Target: Pi Pico 2 TinyUSB MSC RAM disk.
 * Bench: raw SCSI READ10 sequential.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

static uint8_t msc_dev_addr = 0;
static bool msc_ready_flag = false;
static uint32_t block_count = 0;
static uint32_t block_size = 0;

static uint32_t bench_start_ms = 0;
static uint32_t last_report_ms = 0;
static uint32_t bytes_xferred = 0;
static uint32_t blocks_xferred = 0;
static uint32_t bench_total_bytes = 0;

static const uint32_t REPORT_INTERVAL_MS = 1000;
static const uint32_t READ_BLOCKS_PER_XFER = 8;
static CFG_TUH_MEM_ALIGN uint8_t msc_buf[4096];

static volatile bool msc_cb_done = false;
static volatile bool msc_cb_success = false;

static bool msc_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
  (void) dev_addr;
  msc_cb_success = (cb_data->csw->status == 0);
  msc_cb_done = true;
  return true;
}

static void resetBench()
{
  bytes_xferred = 0;
  blocks_xferred = 0;
  bench_total_bytes = block_count * block_size;
  bench_start_ms = millis();
  last_report_ms = millis();
}

static void printStatus()
{
  uint32_t now = millis();
  uint32_t elapsed = now - bench_start_ms;
  if (elapsed == 0)
  {
    elapsed = 1;
  }
  uint32_t bps = (uint32_t)(((uint64_t)bytes_xferred * 1000ULL) / elapsed);
  char line[192];
  snprintf(line, sizeof(line), "[TUH-MSC] elapsed=%lus blocks=%lu bytes=%lu (%.1f KB/s) bs=%lu xfer=%lu",
    elapsed / 1000, (unsigned long)blocks_xferred,
    (unsigned long)bytes_xferred, bps / 1024.0, (unsigned long)block_size,
    (unsigned long)READ_BLOCKS_PER_XFER);
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB MSC bench (FS) ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick msc=%d ready=%d blocks=%lu bs=%lu",
      msc_dev_addr ? (tuh_msc_mounted(msc_dev_addr) ? 1 : 0) : 0,
      msc_ready_flag ? 1 : 0,
      (unsigned long)block_count, (unsigned long)block_size);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick = millis();
  }
  if (msc_dev_addr == 0 || !tuh_msc_mounted(msc_dev_addr))
  {
    return;
  }
  if (!msc_ready_flag)
  {
    return;
  }
  if (block_count == 0 || block_size == 0)
  {
    return;
  }
  if (millis() - last_report_ms > REPORT_INTERVAL_MS)
  {
    if (bytes_xferred)
    {
      printStatus();
    }
    last_report_ms = millis();
  }
  if (blocks_xferred >= block_count)
  {
    blocks_xferred = 0;
  }
  uint32_t lba = blocks_xferred;
  uint32_t remaining = block_count - blocks_xferred;
  uint32_t to_read = remaining > READ_BLOCKS_PER_XFER ? READ_BLOCKS_PER_XFER : remaining;
  uint32_t xfer_bytes = to_read * block_size;
  if (xfer_bytes > sizeof(msc_buf))
  {
    to_read = sizeof(msc_buf) / block_size;
    xfer_bytes = to_read * block_size;
  }
  msc_cb_done = false;
  msc_cb_success = false;
  bool ok = tuh_msc_read10(msc_dev_addr, 0, msc_buf, lba, to_read, msc_complete_cb, 0);
  if (!ok)
  {
    GIGATINYUSB_CONSOLE.println("[TUH-MSC] read10 busy");
    delay(5);
    return;
  }
  uint32_t wait_start = millis();
  while (!msc_cb_done && millis() - wait_start < 5000)
  {
    tuh_task();
  }
  if (!msc_cb_done)
  {
    GIGATINYUSB_CONSOLE.println("[TUH-MSC] read10 timeout");
    msc_ready_flag = false;
    return;
  }
  if (!msc_cb_success)
  {
    GIGATINYUSB_CONSOLE.println("[TUH-MSC] read10 failed");
    msc_ready_flag = false;
    return;
  }
  blocks_xferred += to_read;
  bytes_xferred += xfer_bytes;
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-MSC] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-MSC] device unmounted");
  if (daddr == msc_dev_addr)
  {
    msc_dev_addr = 0;
    msc_ready_flag = false;
    block_count = 0;
    block_size = 0;
  }
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
  msc_dev_addr = dev_addr;
  block_count = tuh_msc_get_block_count(dev_addr, 0);
  block_size = tuh_msc_get_block_size(dev_addr, 0);
  char line[128];
  snprintf(line, sizeof(line), "[TUH-MSC] msc mounted dev=%u blocks=%lu bs=%lu",
    dev_addr, (unsigned long)block_count, (unsigned long)block_size);
  GIGATINYUSB_CONSOLE.println(line);
  msc_ready_flag = true;
  resetBench();
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
  GIGATINYUSB_CONSOLE.println("[TUH-MSC] msc unmounted");
  if (dev_addr == msc_dev_addr)
  {
    msc_dev_addr = 0;
    msc_ready_flag = false;
  }
}
}
