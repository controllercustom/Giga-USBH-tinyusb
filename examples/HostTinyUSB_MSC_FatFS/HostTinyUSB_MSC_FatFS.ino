// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

/*
 * HostTinyUSB_MSC_FatFS — Giga MSC FAT bench via mbed FATFileSystem + BlockDevice
 * Target: Pico 2 MSCRamDisk 128KB FAT
 */
#include "GigaTinyUSB.h"
#include "tusb.h"
#include "blockdevice/BlockDevice.h"
#include "fat/FATFileSystem.h"

static uint8_t msc_dev_addr = 0;
static uint32_t msc_block_count = 0;
static uint32_t msc_block_size = 0;

class USBMSCDBlockDevice : public mbed::BlockDevice
{
public:
  USBMSCDBlockDevice() {}
  virtual int init() { return 0; }
  virtual int deinit() { return 0; }
  virtual int sync() { return 0; }
  virtual int read(void *buffer, mbed::bd_addr_t addr, mbed::bd_size_t size)
  {
    return readSync(buffer, (uint32_t)(addr / msc_block_size), (uint16_t)(size / msc_block_size));
  }
  int readSync(void *buffer, uint32_t lba, uint16_t blocks)
  {
    volatile bool done = false;
    volatile bool success = false;
    struct Ctx { volatile bool *done; volatile bool *ok; } ctx{&done, &success};
    auto cb = [](uint8_t daddr, tuh_msc_complete_data_t const *cbd) -> bool
    {
      Ctx *c = (Ctx*)cbd->user_arg;
      *c->ok = (cbd->csw->status == 0);
      *c->done = true;
      return true;
    };
    done = false; success = false;
    if (!tuh_msc_read10(msc_dev_addr, 0, buffer, lba, blocks, cb, (uintptr_t)&ctx))
    {
      return -1;
    }
    uint32_t start = millis();
    while (!done && millis() - start < 5000)
    {
      tuh_task();
    }
    return (done && success) ? 0 : -1;
  }
  virtual int program(const void *buffer, mbed::bd_addr_t addr, mbed::bd_size_t size)
  {
    uint32_t lba = (uint32_t)(addr / msc_block_size);
    uint16_t blocks = (uint16_t)(size / msc_block_size);
    volatile bool done = false;
    volatile bool success = false;
    struct Ctx { volatile bool *done; volatile bool *ok; } ctx{&done, &success};
    auto cb = [](uint8_t daddr, tuh_msc_complete_data_t const *cbd) -> bool
    {
      Ctx *c = (Ctx*)cbd->user_arg;
      *c->ok = (cbd->csw->status == 0);
      *c->done = true;
      return true;
    };
    done = false; success = false;
    if (!tuh_msc_write10(msc_dev_addr, 0, (void*)buffer, lba, blocks, cb, (uintptr_t)&ctx))
    {
      return -1;
    }
    uint32_t start = millis();
    while (!done && millis() - start < 5000)
    {
      tuh_task();
    }
    return (done && success) ? 0 : -1;
  }
  virtual int erase(mbed::bd_addr_t addr, mbed::bd_size_t size) { (void)addr; (void)size; return 0; }
  virtual mbed::bd_size_t get_read_size() const { return msc_block_size ? msc_block_size : 512; }
  virtual mbed::bd_size_t get_program_size() const { return msc_block_size ? msc_block_size : 512; }
  virtual mbed::bd_size_t get_erase_size() const { return msc_block_size ? msc_block_size : 512; }
  virtual mbed::bd_size_t get_erase_size(mbed::bd_addr_t addr) const { (void)addr; return msc_block_size ? msc_block_size : 512; }
  virtual int get_erase_value() const { return 0xFF; }
  virtual mbed::bd_size_t size() const { return (mbed::bd_size_t)msc_block_count * (msc_block_size ? msc_block_size : 512); }
  virtual const char *get_type() const { return "USBMSCD"; }
};

static USBMSCDBlockDevice usb_bd;
static mbed::FATFileSystem fatfs("usb");
static bool fat_mounted = false;

static void doFatBench()
{
  if (!fat_mounted)
  {
    int err = fatfs.mount(&usb_bd);
    if (err)
    {
      // try mkfs then mount
      GIGATINYUSB_CONSOLE.println("[MSC-FF] mkfs...");
      err = fatfs.reformat(&usb_bd);
      if (err)
      {
        char line[64];
        snprintf(line, sizeof(line), "[MSC-FF] reformat failed %d", err);
        GIGATINYUSB_CONSOLE.println(line);
        return;
      }
      err = fatfs.mount(&usb_bd);
      if (err)
      {
        char line[64];
        snprintf(line, sizeof(line), "[MSC-FF] mount after mkfs failed %d", err);
        GIGATINYUSB_CONSOLE.println(line);
        return;
      }
    }
    fat_mounted = true;
    GIGATINYUSB_CONSOLE.println("[MSC-FF] fat mounted");
  }
  const char *path = "/usb/bench.bin";
  const uint32_t FILE_SIZE = 64 * 1024;
  const uint32_t CHUNK = 4096;
  static uint8_t wbuf[4096];
  static uint8_t rbuf[4096];
  for (uint32_t i = 0; i < CHUNK; i++) wbuf[i] = (uint8_t)(i & 0xFF);

  FILE *f = fopen(path, "wb");
  if (!f)
  {
    GIGATINYUSB_CONSOLE.println("[MSC-FF] fopen w failed");
    return;
  }
  uint32_t t0 = millis();
  size_t written = 0;
  while (written < FILE_SIZE)
  {
    size_t to_write = CHUNK;
    if (written + to_write > FILE_SIZE) to_write = FILE_SIZE - written;
    size_t bw = fwrite(wbuf, 1, to_write, f);
    if (bw != to_write) { GIGATINYUSB_CONSOLE.println("[MSC-FF] fwrite failed"); fclose(f); return; }
    written += bw;
  }
  fclose(f);
  uint32_t t1 = millis();
  uint32_t elapsed = t1 - t0; if (elapsed==0) elapsed=1;
  uint32_t bps = (uint32_t)(((uint64_t)written * 1000ULL)/elapsed);
  char line[128];
  snprintf(line,sizeof(line),"[MSC-FF] write %lu B in %lu ms (%.1f KB/s)", (unsigned long)written,(unsigned long)elapsed,bps/1024.0);
  GIGATINYUSB_CONSOLE.println(line);

  f = fopen(path, "rb");
  if (!f) { GIGATINYUSB_CONSOLE.println("[MSC-FF] fopen r failed"); return; }
  t0 = millis();
  size_t read_total=0;
  while (read_total < FILE_SIZE)
  {
    size_t br = fread(rbuf,1,CHUNK,f);
    if (br==0) break;
    read_total += br;
  }
  fclose(f);
  t1 = millis();
  elapsed = t1 - t0; if (elapsed==0) elapsed=1;
  bps = (uint32_t)(((uint64_t)read_total*1000ULL)/elapsed);
  snprintf(line,sizeof(line),"[MSC-FF] read %lu B in %lu ms (%.1f KB/s)", (unsigned long)read_total,(unsigned long)elapsed,bps/1024.0);
  GIGATINYUSB_CONSOLE.println(line);
  remove(path);
}

void setup()
{
  GIGATINYUSB_CONSOLE.begin(GIGATINYUSB_BAUD);
  uint32_t t0 = millis();
  while (!GIGATINYUSB_CONSOLE && millis()-t0<1500) delay(10);
  gigaTinyUSB_enableHostPort();
  delay(1000);
  GIGATINYUSB_CONSOLE.println("\n### Giga MSC FatFS bench ###");
  tuh_giga_init();
}

void loop()
{
  tuh_task();
  static uint32_t lastTick=0;
  if (millis()-lastTick>2000)
  {
    char tbuf[96];
    snprintf(tbuf,sizeof(tbuf),"tick msc=%d fat=%d blocks=%lu bs=%lu",
      msc_dev_addr ? (tuh_msc_mounted(msc_dev_addr)?1:0):0,
      fat_mounted?1:0, (unsigned long)msc_block_count,(unsigned long)msc_block_size);
    GIGATINYUSB_CONSOLE.println(tbuf);
    lastTick=millis();
  }
  if (msc_dev_addr==0 || !tuh_msc_mounted(msc_dev_addr)) return;
  static uint32_t lastBench=0;
  if (millis()-lastBench>6000)
  {
    lastBench=millis();
    doFatBench();
  }
}

extern "C"
{
void tuh_mount_cb(uint8_t daddr){(void)daddr; GIGATINYUSB_CONSOLE.println("[MSC-FF] device mounted");}
void tuh_umount_cb(uint8_t daddr){
  (void)daddr;
  GIGATINYUSB_CONSOLE.println("[MSC-FF] device unmounted");
  if (daddr==msc_dev_addr){ msc_dev_addr=0; fatfs.unmount(); fat_mounted=false; }
}
void tuh_msc_mount_cb(uint8_t dev_addr){
  msc_dev_addr=dev_addr;
  msc_block_count=tuh_msc_get_block_count(dev_addr,0);
  msc_block_size=tuh_msc_get_block_size(dev_addr,0);
  char line[96];
  snprintf(line,sizeof(line),"[MSC-FF] msc mounted dev=%u blocks=%lu bs=%lu",dev_addr,(unsigned long)msc_block_count,(unsigned long)msc_block_size);
  GIGATINYUSB_CONSOLE.println(line);
}
void tuh_msc_umount_cb(uint8_t dev_addr){
  (void)dev_addr;
  GIGATINYUSB_CONSOLE.println("[MSC-FF] msc unmounted");
  fatfs.unmount(); fat_mounted=false;
  if (dev_addr==msc_dev_addr) msc_dev_addr=0;
}
}
