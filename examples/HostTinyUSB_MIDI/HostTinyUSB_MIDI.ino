// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_MIDI — Giga USB-A USB-MIDI host ECHO test via TinyUSB
 * (tuh_midi + upstream MIDI host driver), verified against the RP2040
 * MIDIEcho emulator (Adafruit USB-MIDI device, echoes 4-byte packets).
 *
 * Test: send Note-On packets; RP2040 echoes them; measure throughput.
 * Includes diagnostic counters for debugging stall issues.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

static uint32_t tx_seq = 0;
static uint32_t tx_bytes_total = 0;
static uint32_t rx_bytes_total = 0;
static uint32_t blocks_ok = 0;
static uint32_t bench_start_ms = 0;
static uint32_t last_report_ms = 0;
static const uint32_t REPORT_INTERVAL_MS = 1000;

static void resetCounters()
{
  tx_seq = 0;
  tx_bytes_total = 0;
  rx_bytes_total = 0;
  blocks_ok = 0;
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
  uint32_t tx_bps = (uint32_t)(((uint64_t)tx_bytes_total * 1000ULL) / elapsed);
  uint32_t rx_bps = (uint32_t)(((uint64_t)rx_bytes_total * 1000ULL) / elapsed);
  char line[192];
  snprintf(line, sizeof(line), "[TUH-MIDI] elapsed=%lus tx=%lu (%.1f KB/s) rx=%lu (%.1f KB/s) pkts=%lu",
    elapsed / 1000, (unsigned long)tx_bytes_total, tx_bps / 1024.0,
    (unsigned long)rx_bytes_total, rx_bps / 1024.0, (unsigned long)blocks_ok);
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB MIDI echo ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick midi=%d rx=%lu tx=%lu",
      tuh_midi_mounted(0) ? 1 : 0, (unsigned long)rx_bytes_total,
      (unsigned long)tx_bytes_total);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick = millis();
  }
  if (!tuh_midi_mounted(0))
  {
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

  uint8_t txpkt[4] = { 0x09, 0x90, 0, 0x7F };
  uint8_t rxbkt[4];

  const uint32_t BATCH = 64;
  uint32_t prev_rx = rx_bytes_total;

  for (uint32_t i = 0; i < BATCH; i++)
  {
    txpkt[2] = (uint8_t)(tx_seq & 0xFF);
    if (!tuh_midi_packet_write(0, txpkt))
    {
      break;
    }
    tx_seq++;
    tx_bytes_total += 4;
  }
  tuh_midi_write_flush(0);

  uint32_t wait_start = millis();
  while (rx_bytes_total < prev_rx + BATCH * 4 && millis() - wait_start < 20)
  {
    tuh_task();

    while (tuh_midi_read_available(0) >= 4)
    {
      if (tuh_midi_packet_read(0, rxbkt))
      {
        rx_bytes_total += 4;
        blocks_ok++;
      }
      else
      {
        break;
      }
    }
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-MIDI] device mounted");
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[TUH-MIDI] device unmounted");
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
  (void) idx;
  (void) mount_cb_data;
  GIGATINYUSB_CONSOLE.println("[TUH-MIDI] midi mounted");
  resetCounters();
}

void tuh_midi_umount_cb(uint8_t idx)
{
  (void) idx;
  GIGATINYUSB_CONSOLE.println("[TUH-MIDI] midi unmounted");
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  (void) idx;
  (void) xferred_bytes;
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  (void) idx;
  (void) xferred_bytes;
}
}
