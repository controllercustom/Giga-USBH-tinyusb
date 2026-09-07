// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_TriCDC — concurrent bench for 3 CDC UART bridges behind hub
 * (CP2102 + PL2303 + FTDI) RX-TX shorted, ECHO verified, 115200 8N1.
 * Host: GIGA A via 4-port HS hub (TT). No ULPI, FS PHY.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

#define MAX_CDC  4

static struct {
  bool mounted;
  bool ready;
  uint32_t tx_seq;
  uint32_t rx_expected;
  uint32_t tx_total;
  uint32_t rx_total;
  uint32_t err;
  uint32_t ok;
  uint32_t bad;
} st[MAX_CDC];

static uint32_t bench_start_ms = 0;
static uint32_t last_report_ms = 0;
static const uint32_t REPORT_INTERVAL_MS = 1000;
static uint8_t txbuf[64], rxbuf[64];

static void resetAll()
{
  for (int i = 0; i < MAX_CDC; i++)
  {
    st[i].tx_seq = 0;
    st[i].rx_expected = 0;
    st[i].tx_total = 0;
    st[i].rx_total = 0;
    st[i].err = 0;
    st[i].ok = 0;
    st[i].bad = 0;
  }
  bench_start_ms = millis();
  last_report_ms = millis();
}

static void printStatus()
{
  uint32_t now = millis();
  uint32_t elapsed = now - bench_start_ms;
  if (elapsed == 0) elapsed = 1;
  for (int i = 0; i < MAX_CDC; i++)
  {
    if (!st[i].mounted) continue;
    uint32_t tx_bps = (uint32_t)(((uint64_t)st[i].tx_total * 1000ULL) / elapsed);
    uint32_t rx_bps = (uint32_t)(((uint64_t)st[i].rx_total * 1000ULL) / elapsed);
    char line[192];
    snprintf(line, sizeof(line), "[TRI-CDC %d] tx=%lu (%.1f KB/s) rx=%lu (%.1f KB/s) ok=%lu bad=%lu err=%lu ready=%d",
      i, (unsigned long)st[i].tx_total, tx_bps/1024.0,
      (unsigned long)st[i].rx_total, rx_bps/1024.0,
      (unsigned long)st[i].ok, (unsigned long)st[i].bad, (unsigned long)st[i].err,
      st[i].ready?1:0);
    GIGATINYUSB_CONSOLE.println(line);
  }
}

static void fillTx(uint8_t idx)
{
  for (int i = 0; i < (int)sizeof(txbuf); i++)
  {
    txbuf[i] = (uint8_t)(st[idx].tx_seq++ & 0xFF);
  }
}

static cdc_line_coding_t cdc_coding;
static void cdc_line_cb(tuh_xfer_t *xfer)
{
  uint8_t idx = (uint8_t)(uintptr_t)xfer->user_data;
  if (xfer->result != XFER_RESULT_SUCCESS) return;
  tuh_cdc_set_control_line_state(idx, CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
  st[idx].ready = true;
  GIGATINYUSB_CONSOLE.println("[TRI-CDC] ready");
}
static void cdc_dummy_cb(tuh_xfer_t *xfer)
{
  uint8_t idx = (uint8_t)(uintptr_t)xfer->user_data;
  cdc_coding.bit_rate = 115200;
  cdc_coding.stop_bits = 0;
  cdc_coding.parity = 0;
  cdc_coding.data_bits = 8;
  tuh_cdc_set_line_coding(idx, &cdc_coding, cdc_line_cb, (uintptr_t)idx);
}
static void cdc_configure(uint8_t idx)
{
  st[idx].ready = false;
  tuh_cdc_read_clear(idx);
  tuh_cdc_write_clear(idx);
  cdc_coding.bit_rate = 1200;
  cdc_coding.stop_bits = 0;
  cdc_coding.parity = 0;
  cdc_coding.data_bits = 8;
  tuh_cdc_set_line_coding(idx, &cdc_coding, cdc_dummy_cb, (uintptr_t)idx);
}

void setup()
{
  GIGATINYUSB_CONSOLE.begin(GIGATINYUSB_BAUD);
  uint32_t t0 = millis();
  while (!GIGATINYUSB_CONSOLE && millis() - t0 < 1500) delay(10);
  gigaTinyUSB_enableHostPort();
  delay(1000);
  GIGATINYUSB_CONSOLE.println("\n### Tri-CDC hub bench (CP2102+PL2303+FTDI) ###");
  tuh_giga_init();
  resetAll();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    int n = 0;
    for (int i = 0; i < MAX_CDC; i++) if (st[i].mounted) n++;
    snprintf(tbuf, sizeof(tbuf), "tick cdc mounted=%d", n);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick = millis();
  }
  if (millis() - last_report_ms > REPORT_INTERVAL_MS)
  {
    printStatus();
    last_report_ms = millis();
  }
  for (int idx = 0; idx < MAX_CDC; idx++)
  {
    if (!tuh_cdc_mounted(idx) || !st[idx].ready) continue;
    for (int i = 0; i < 8; i++)
    {
      if (tuh_cdc_write_available(idx) < sizeof(txbuf)) break;
      fillTx(idx);
      uint32_t w = tuh_cdc_write(idx, txbuf, sizeof(txbuf));
      if (w == 0) break;
      st[idx].tx_total += w;
    }
    tuh_cdc_write_flush(idx);
    while (tuh_cdc_read_available(idx))
    {
      uint32_t avail = tuh_cdc_read_available(idx);
      uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
      uint32_t n = tuh_cdc_read(idx, rxbuf, toRead);
      if (n == 0) break;
      for (uint32_t i = 0; i < n; i++)
      {
        uint8_t exp = (uint8_t)(st[idx].rx_expected & 0xFF);
        if (rxbuf[i] != exp) st[idx].err++;
        st[idx].rx_expected++;
      }
      st[idx].rx_total += n;
    }
    st[idx].ok++;
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr) { (void)daddr; GIGATINYUSB_CONSOLE.println("[TRI-CDC] device mounted"); }
void tuh_umount_cb(uint8_t daddr) { (void)daddr; GIGATINYUSB_CONSOLE.println("[TRI-CDC] device unmounted"); }
void tuh_cdc_mount_cb(uint8_t idx)
{
  char line[64];
  snprintf(line, sizeof(line), "[TRI-CDC] cdc mounted idx=%u", idx);
  GIGATINYUSB_CONSOLE.println(line);
  st[idx].mounted = true;
  st[idx].ready = false;
  cdc_configure(idx);
}
void tuh_cdc_umount_cb(uint8_t idx)
{
  GIGATINYUSB_CONSOLE.println("[TRI-CDC] cdc unmounted");
  st[idx].mounted = false;
  st[idx].ready = false;
}
void tuh_cdc_rx_cb(uint8_t idx) { (void)idx; }
void tuh_cdc_tx_complete_cb(uint8_t idx) { (void)idx; }
}
