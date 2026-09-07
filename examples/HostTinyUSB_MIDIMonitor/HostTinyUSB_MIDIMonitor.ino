// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_MIDIMonitor — Giga USB-A USB-MIDI monitor + LED test for real
 * control surfaces (verified with Korg nanoKONTROL2), via TinyUSB host.
 *
 * Wiring:
 *   Giga --- USB-A ---> USB-MIDI device (e.g. nanoKONTROL2)
 *   Pi5  --- USB-C ---> Giga Serial (CDC debug console, /dev/ttyACM1 @115200)
 *   Pi5  --- J-Link --> Giga (SWD, 10-pin J10)
 *
 * Behavior:
 *   - Prints every incoming USB-MIDI event packet raw + decoded (Note On/Off,
 *     Control Change, Program Change, Pitch Bend, clock/realtime).
 *   - 'l' key: toggles the LED of the LAST touched control by re-sending its
 *     CC with value 127 then 0 (nanoKONTROL2 LEDs mirror their button CCs).
 *     Touch a SOLO/MUTE/REC button first, then press 'l'.
 *   - tick + packet counters every 2 s.
 */
#include "GigaTinyUSB.h"
#include "tusb.h"

// From src/GigaTinyUSB/bsp_giga.cpp (single definition lives in the glue TU).

static uint32_t rx_packets = 0;
static uint32_t tx_packets = 0;
static uint8_t last_cc_ch = 0;
static uint8_t last_cc_num = 0;
static bool have_last_cc = false;

static const char *cinName(uint8_t cin)
{
  switch (cin)
  {
    case 0x8: return "NoteOff";
    case 0x9: return "NoteOn";
    case 0xA: return "PolyKeyPress";
    case 0xB: return "CC";
    case 0xC: return "ProgChg";
    case 0xD: return "ChanPress";
    case 0xE: return "PitchBend";
    case 0xF: return "1Byte";
    default: return "SysEx/Other";
  }
}

static void printPacket(const uint8_t p[4])
{
  // USB-MIDI event header: cable number = HIGH nibble, CIN = LOW nibble
  // (verified against live nanoKONTROL2 traffic: 0x0B = cable 0 + CIN CC).
  uint8_t cable = (p[0] >> 4) & 0xF;
  uint8_t cin = p[0] & 0xF;
  char line[128];
  int o = snprintf(line, sizeof(line), "[MIDI] cable=%u %s %02x %02x %02x %02x : ",
    cable, cinName(cin), p[0], p[1], p[2], p[3]);
  uint8_t status = p[1];
  uint8_t type = status & 0xF0;
  uint8_t ch = status & 0x0F;
  if (type == 0xB0)
  {
    o += snprintf(line + o, sizeof(line) - o, "ch=%u CC#%u val=%u", ch, p[2], p[3]);
  }
  else if (type == 0x90 || type == 0x80)
  {
    o += snprintf(line + o, sizeof(line) - o, "ch=%u note=%u vel=%u%s",
      ch, p[2], p[3], type == 0x90 && p[3] ? " ON" : " off");
  }
  else if (type == 0xC0)
  {
    o += snprintf(line + o, sizeof(line) - o, "ch=%u program=%u", ch, p[2]);
  }
  else if (type == 0xE0)
  {
    o += snprintf(line + o, sizeof(line) - o, "ch=%u bend=%u", ch, (p[3] << 7) | p[2]);
  }
  else if (cin == 0xF)
  {
    o += snprintf(line + o, sizeof(line) - o, "realtime/common 0x%02x", p[1]);
  }
  else
  {
    o += snprintf(line + o, sizeof(line) - o, "(raw)");
  }
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
  GIGATINYUSB_CONSOLE.println("\n### Giga TinyUSB MIDI monitor ###");
  GIGATINYUSB_CONSOLE.println("Move faders/knobs/press buttons. 'l' toggles the last-touched CC LED.");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  while (GIGATINYUSB_CONSOLE.available())
  {
    char c = (char) GIGATINYUSB_CONSOLE.read();
    if (c == 'l' || c == 'L')
    {
      if (!have_last_cc || !tuh_midi_mounted(0))
      {
        GIGATINYUSB_CONSOLE.println("[MIDI] l: touch a button/fader first (and wait for mount)");
        continue;
      }
      // Toggle last-touched CC LED: value 127 then 0, 400 ms apart.
      uint8_t pkt[4] = { 0x0B, (uint8_t)(0xB0 | last_cc_ch), last_cc_num, 127 };
      tuh_midi_packet_write(0, pkt);
      tuh_midi_write_flush(0);
      tx_packets++;
      delay(400);
      pkt[3] = 0;
      tuh_midi_packet_write(0, pkt);
      tuh_midi_write_flush(0);
      tx_packets++;
      char line[64];
      snprintf(line, sizeof(line), "[MIDI] LED toggle ch=%u CC#%u (127 then 0)", last_cc_ch, last_cc_num);
      GIGATINYUSB_CONSOLE.println(line);
    }
  }
  if (tuh_midi_mounted(0))
  {
    uint8_t p[4];
    while (tuh_midi_packet_read(0, p))
    {
      // All-zero packets are bulk padding (nanoKONTROL2 pads short bulk
      // transfers with zeros) — ignore silently, don't count.
      if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0)
      {
        continue;
      }
      rx_packets++;
      printPacket(p);
      if ((p[1] & 0xF0) == 0xB0)
      {
        last_cc_ch = p[1] & 0x0F;
        last_cc_num = p[2];
        have_last_cc = true;
      }
    }
  }
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "tick midi=%d rxpk=%lu txpk=%lu",
      tuh_midi_mounted(0) ? 1 : 0, (unsigned long)rx_packets, (unsigned long)tx_packets);
    GIGATINYUSB_CONSOLE.println(tbuf);
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr)
{
  uint16_t vid = 0;
  uint16_t pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  char line[80];
  snprintf(line, sizeof(line), "[MIDI] mount daddr=%u vid=%04x pid=%04x rx_cables=%u tx_cables=%u",
    daddr, vid, pid, tuh_midi_get_rx_cable_count(0), tuh_midi_get_tx_cable_count(0));
  GIGATINYUSB_CONSOLE.println(line);
}

void tuh_umount_cb(uint8_t daddr)
{
  (void) daddr;
  GIGATINYUSB_CONSOLE.println("[MIDI] unmounted");
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
  (void) idx;
  (void) mount_cb_data;
  GIGATINYUSB_CONSOLE.println("[MIDI] midi mounted");
}

void tuh_midi_umount_cb(uint8_t idx)
{
  (void) idx;
  GIGATINYUSB_CONSOLE.println("[MIDI] midi unmounted");
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  (void) idx;
  (void) xferred_bytes; // drained in loop()
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  (void) idx;
  (void) xferred_bytes;
}
}
