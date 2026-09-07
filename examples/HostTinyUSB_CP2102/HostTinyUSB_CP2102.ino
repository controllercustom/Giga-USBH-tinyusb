// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_CP2102 — Giga USB-A host UART bridge for SiLabs CP2102 (10C4:EA60)
 * via the TinyUSB host stack (tuh_cdc + upstream CP210x serial driver).
 *
 * Wiring:
 *   Giga --- USB-A ---> CP2102 USB (10C4:EA60)
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * FTDI UART side, two topologies (same as examples/HostFTDI):
 *   A. ECHO (default): CP2102 TX jumpered to RX. Sketch sends binary seq blocks
 *      0..255 and verifies the loopback echo with seq/CRC counters.
 *   B. CROSS: CP2102 TX->D0 (RX1), RX->D1 (TX1), GND->GND. Sketch bridges the
 *      TinyUSB CDC port and Serial1 so bytes round-trip through Giga UART.
 *
 * Stack: Mbed owns USB-C CDC + clocks; TinyUSB owns USB-A host (OTG_HS).
 * Driver init on mount is chained async: baud(115200) -> 8N1 -> DTR+RTS
 * (CP210x uses separate baud + line-control requests; bulk data has no status header).
 * Console keys: e=ECHO c=CROSS t=TX_FLOOD, 9=9600 1=115200.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

enum FtdiMode { FTDI_ECHO, FTDI_CROSS, FTDI_TX_FLOOD };
static FtdiMode ftdiMode = FTDI_ECHO;
static const char *ftdiModeName(int m)
{
  switch (m)
  {
    case FTDI_ECHO:
      return "ECHO";
    case FTDI_CROSS:
      return "CROSS";
    case FTDI_TX_FLOOD:
      return "TX_FLOOD";
  }
  return "?";
}

static uint32_t tx_seq = 0, rx_seq_expected = 0;
static uint32_t tx_bytes_total = 0, rx_bytes_total = 0, rx_errors = 0, blocks_ok = 0, blocks_bad = 0;
static uint32_t bench_start_ms = 0, last_report_ms = 0;
static const uint32_t REPORT_INTERVAL_MS = 1000;
static uint8_t txbuf[64], rxbuf[64];
static int ftdi_baud = 115200;
static bool ftdi_ready = false; // set after baud+format+modem chain completes

static void fillTxBlock(uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    buf[i] = (uint8_t)(tx_seq++ & 0xFF);
  }
}
static void resetCounters()
{
  tx_seq = 0;
  rx_seq_expected = 0;
  tx_bytes_total = 0;
  rx_bytes_total = 0;
  rx_errors = 0;
  blocks_ok = 0;
  blocks_bad = 0;
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
  uint32_t tx_bps = (tx_bytes_total * 1000UL) / elapsed;
  uint32_t rx_bps = (rx_bytes_total * 1000UL) / elapsed;
  char line[192];
  snprintf(line, sizeof(line), "[TUH-CP2102] mode=%s baud=%d elapsed=%lus tx=%lu (%.1f KB/s) rx=%lu (%.1f KB/s) ok=%lu bad=%lu err=%lu",
    ftdiModeName(ftdiMode), ftdi_baud, elapsed / 1000, (unsigned long)tx_bytes_total, tx_bps / 1024.0,
    (unsigned long)rx_bytes_total, rx_bps / 1024.0, (unsigned long)blocks_ok, (unsigned long)blocks_bad, (unsigned long)rx_errors);
  GIGATINYUSB_CONSOLE.println(line);
}

// Mount-time init chain (async): baud -> 8N1 -> DTR+RTS -> ready.
// Each stage checks xfer->result before proceeding; a failure stops the
// chain and logs the error so a silent baud-rate mismatch is impossible.
static void ftdi_cfg_format_cb(tuh_xfer_t *xfer);
static void ftdi_cfg_baud_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    char line[64];
    snprintf(line, sizeof(line), "[TUH-CP2102] baud set FAILED result=%d", (int) xfer->result);
    GIGATINYUSB_CONSOLE.println(line);
    return;
  }
  // 8N1: stop=0 (1 bit), parity=0 (none), data=8.
  tuh_cdc_set_data_format(0, 0, 0, 8, ftdi_cfg_format_cb, 0);
}
static void ftdi_cfg_format_cb(tuh_xfer_t *xfer)
{
  if (xfer->result != XFER_RESULT_SUCCESS)
  {
    char line[64];
    snprintf(line, sizeof(line), "[TUH-CP2102] format set FAILED result=%d", (int) xfer->result);
    GIGATINYUSB_CONSOLE.println(line);
    return;
  }
  tuh_cdc_set_control_line_state(0,
    CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
  ftdi_ready = true;
  resetCounters();
  GIGATINYUSB_CONSOLE.println("[TUH-CP2102] ready (baud+8N1+DTR+RTS)");
}
static void ftdi_configure(int baud)
{
  ftdi_ready = false;
  tuh_cdc_read_clear(0);
  tuh_cdc_write_clear(0);
  tuh_cdc_set_baudrate(0, (uint32_t) baud, ftdi_cfg_baud_cb, 0);
}
static void setBaud(int b)
{
  if (b == ftdi_baud && ftdi_ready)
  {
    return;
  }
  ftdi_baud = b;
  char line[48];
  snprintf(line, sizeof(line), "[TUH-CP2102] baud=%d", b);
  GIGATINYUSB_CONSOLE.println(line);
  if (tuh_cdc_mounted(0))
  {
    ftdi_configure(b);
  }
  resetCounters();
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB CP2102 bridge ###");
  GIGATINYUSB_CONSOLE.println("Commands: e=ECHO(TX-RX jumper) c=CROSS(CP2102<->Serial1) t=TX_FLOOD 9=9600 1=115200 X=VBUS POR R=re-configure");
  Serial1.begin(115200); // for CROSS mode: CP2102 TX->D0 RX->D1 GND->GND
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static char cmdBuf[16];
  static uint8_t cmdLen = 0;
  while (GIGATINYUSB_CONSOLE.available())
  {
    char c = (char)GIGATINYUSB_CONSOLE.read();
    if (c == '\r' || c == '\n')
    {
      if (cmdLen == 1)
      {
        char k = cmdBuf[0];
        FtdiMode nm = ftdiMode;
        if (k == 'e' || k == 'E')
        {
          nm = FTDI_ECHO;
        }
        else if (k == 'c' || k == 'C')
        {
          nm = FTDI_CROSS;
        }
        else if (k == 't' || k == 'T')
        {
          nm = FTDI_TX_FLOOD;
        }
        else if (k == '9')
        {
          setBaud(9600);
        }
        else if (k == '1')
        {
          setBaud(115200);
        }
        else if (k == 'X' || k == 'x')
        {
          // Power-cycle the USB-A port (FTDI POR): clears chip-side UART
          // buffers + forces clean re-enumerate/re-mount/re-configure.
          // NOTE: do NOT clear ftdi_ready here — if the POR fails (e.g.
          // UART backpower via D1 idle-HIGH in CROSS wiring keeps the chip
          // alive with no disconnect), the old config stays valid and
          // traffic must continue. Real disconnects clear ready via umount.
          GIGATINYUSB_CONSOLE.println("[TUH-CP2102] VBUS power-cycle...");
          digitalWrite(GIGATINYUSB_VBUS_PIN, LOW);
          delay(1000);
          gigaTinyUSB_enableHostPort();
          delay(500);
          GIGATINYUSB_CONSOLE.println("[TUH-CP2102] VBUS restored, waiting for mount...");
        }
        else if (k == 'R' || k == 'r')
        {
          // Re-run the SIO configure chain on demand (recovery without
          // replug): baud -> 8N1 -> DTR+RTS.
          GIGATINYUSB_CONSOLE.println("[TUH-CP2102] re-configure...");
          if (tuh_cdc_mounted(0))
          {
            ftdi_configure(ftdi_baud);
          }
          else
          {
            GIGATINYUSB_CONSOLE.println("[TUH-CP2102] not mounted");
          }
        }
        if (nm != ftdiMode)
        {
          ftdiMode = nm;
          resetCounters();
          tuh_cdc_read_clear(0);
          tuh_cdc_write_clear(0);
          while (Serial1.available())
          {
            Serial1.read(); // drain stale wire bytes
          }
          GIGATINYUSB_CONSOLE.print("[TUH-CP2102] switched to ");
          GIGATINYUSB_CONSOLE.println(ftdiModeName(ftdiMode));
        }
      }
      cmdLen = 0;
    }
    else if (cmdLen < sizeof(cmdBuf) - 1)
    {
      cmdBuf[cmdLen++] = c;
    }
    else
    {
      cmdLen = 0;
    }
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick conn=%d rx=%lu tx=%lu", tuh_cdc_mounted(0) ? 1 : 0, (unsigned long)rx_bytes_total, (unsigned long)tx_bytes_total);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick = millis();
  }
  if (!tuh_cdc_mounted(0) || !ftdi_ready)
  {
    delay(1);
    return;
  }
  if (millis() - last_report_ms > REPORT_INTERVAL_MS)
  {
    if (tx_bytes_total || rx_bytes_total)
    {
      printStatus();
    }
    last_report_ms = millis();
  }

  switch (ftdiMode)
  {
    case FTDI_ECHO:
    {
      for (int i = 0; i < 8; i++)
      {
        if (tuh_cdc_write_available(0) < sizeof(txbuf))
        {
          break;
        }
        fillTxBlock(txbuf, sizeof(txbuf));
        uint32_t w = tuh_cdc_write(0, txbuf, sizeof(txbuf));
        if (w == 0)
        {
          break;
        }
        tx_bytes_total += w;
      }
      tuh_cdc_write_flush(0);
      // Drain all available RX data.
      while (tuh_cdc_read_available(0))
      {
        uint32_t avail = tuh_cdc_read_available(0);
        uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
        uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
        if (n == 0)
        {
          break;
        }
        for (uint32_t i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(rx_seq_expected & 0xFF);
          if (rxbuf[i] != exp)
          {
            rx_errors++;
          }
          rx_seq_expected++;
        }
        rx_bytes_total += n;
      }
      if (rx_errors == 0)
      {
        blocks_ok++;
      }
      else
      {
        blocks_bad++;
      }
      break;
    }
    case FTDI_CROSS:
    {
      // Per-direction loopback (no forwarding — forwarding would loop forever
      // with Giga on both ends of the FTDI UART):
      //   A: USB-OUT -> FTDI UART-TX -> D0 -> Serial1 RX (checked vs ad_seq)
      //   B: Serial1-TX (D1) -> FTDI UART-RX -> USB-IN (checked vs bd_seq)
      static uint32_t lastX = 0;
      if (millis() - lastX < 200)
      {
        break;
      }
      lastX = millis();
      static uint32_t a_seq = 0;
      static uint32_t a_exp = 0;
      static uint32_t b_seq = 0;
      static uint32_t b_exp = 0;
      static uint32_t a_err = 0;
      static uint32_t b_err = 0;
      uint8_t abuf[16], bbuf[16];
      for (int i = 0; i < 16; i++)
      {
        abuf[i] = (uint8_t)(a_seq++ & 0xFF);
        bbuf[i] = (uint8_t)(b_seq++ & 0xFF);
      }
      tx_bytes_total += tuh_cdc_write(0, abuf, 16);
      tuh_cdc_write_flush(0);
      Serial1.write(bbuf, 16);
      // Drain direction B (USB-IN): expect b_exp sequence.
      while (tuh_cdc_read_available(0))
      {
        uint32_t avail = tuh_cdc_read_available(0);
        uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
        uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
        if (n == 0)
        {
          break;
        }
        for (uint32_t i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(b_exp & 0xFF);
          if (rxbuf[i] != exp)
          {
            b_err++;
            rx_errors++;
          }
          b_exp++;
        }
        rx_bytes_total += n;
      }
      // Drain direction A (Serial1 RX): expect a_exp sequence.
      while (Serial1.available())
      {
        int nav = Serial1.available();
        uint32_t toRead = (uint32_t)nav < sizeof(rxbuf) ? (uint32_t)nav : sizeof(rxbuf);
        int n = Serial1.readBytes((char*)rxbuf, toRead);
        if (n <= 0)
        {
          break;
        }
        for (int i = 0; i < n; i++)
        {
          uint8_t exp = (uint8_t)(a_exp & 0xFF);
          if (rxbuf[i] != exp)
          {
            a_err++;
            rx_errors++;
          }
          a_exp++;
        }
        rx_bytes_total += n;
      }
      blocks_ok++;
      {
        char xbuf[128];
        snprintf(xbuf, sizeof(xbuf), "[TUH-CP2102] cross A(dir USB->D0) err=%lu B(dir D1->USB) err=%lu",
          (unsigned long)a_err, (unsigned long)b_err);
        GIGATINYUSB_CONSOLE.println(xbuf);
      }
      break;
    }
    case FTDI_TX_FLOOD:
    {
      fillTxBlock(txbuf, sizeof(txbuf));
      tx_bytes_total += tuh_cdc_write(0, txbuf, sizeof(txbuf));
      tuh_cdc_write_flush(0);
      while (tuh_cdc_read_available(0))
      {
        uint32_t avail = tuh_cdc_read_available(0);
        uint32_t toRead = avail < sizeof(rxbuf) ? avail : sizeof(rxbuf);
        uint32_t n = tuh_cdc_read(0, rxbuf, toRead);
        if (n == 0)
        {
          break;
        }
        rx_bytes_total += n;
      }
      break;
    }
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-CP2102] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  ftdi_ready = false;
  GIGATINYUSB_CONSOLE.println("[TUH-CP2102] device unmounted");
}

void tuh_cdc_mount_cb(uint8_t idx)
{
  (void) idx;
  GIGATINYUSB_CONSOLE.println("[TUH-CP2102] cdc mounted");
  ftdi_configure(ftdi_baud);
}

void tuh_cdc_umount_cb(uint8_t idx)
{
  (void) idx;
  ftdi_ready = false;
}

void tuh_cdc_rx_cb(uint8_t idx)
{
  (void) idx; // drained in loop()
}

void tuh_cdc_tx_complete_cb(uint8_t idx)
{
  (void) idx;
}
}
