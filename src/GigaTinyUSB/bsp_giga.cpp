// SPDX-License-Identifier: MIT
// Copyright (c) 2026 controllercustom@myyahoo.com
// Copyright (c) 2026 opencode (anomalyco/opencode, Muse Spark)

// bsp_giga.cpp — TinyUSB BSP glue for Arduino GIGA R1 WiFi (USB-A host).
//
// What Mbed already owns (do NOT touch): clocks/RCC base, USB-C CDC console
// (OTG_FS), SysTick, NVIC core config. What was MISSING (root cause of the
// silent first bringup): nobody enables the OTG_HS peripheral clocks or the
// PB_14/PB_15 alternate functions, because Mbed only does that lazily inside
// USBHALHost::init — which never runs when TinyUSB (not Arduino_USBHostMbed5)
// owns the host port. Pin recipe mirrored from the working Mbed stack
// (Arduino_USBHostMbed5 USBHALHost_STM.h TARGET_GIGA section):
//   PB_14/15 = AF12_OTG2_FS, AF_PP, NOPULL, VERY_HIGH.
// VBUS (PA_15) is enabled by the sketch via GigaTinyUSB.h BEFORE tuh_giga_init().
// USB 48 MHz kernel clock is already configured by Mbed (CDC works); TRDT is
// programmed by TinyUSB itself (dwc2_phy_update, 0x6 at 480 MHz).
#include "Arduino.h"
#include "pinmap.h"
#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_rcc_ex.h"
#include "stm32h7xx_hal_gpio.h"
#include "GigaTinyUSB.h"

// OTG_HS host interrupt -> TinyUSB HCD (rhport 1). Verified: neither the Mbed
// core nor Arduino_USBHostMbed5 defines OTG_HS_IRQHandler, and Mbed startup
// leaves the weak Default_Handler alias free for us to override.
extern "C" void OTG_HS_IRQHandler(void)
{
  hcd_int_handler(1, true);
}

extern "C" uint32_t tusb_time_millis_api(void)
{
  return (uint32_t) millis();
}

// Full OTG_HS host bring-up + tuh_init. Call AFTER gigaTinyUSB_enableHostPort().
static void tu_step(const char *s)
{
  GIGATINYUSB_CONSOLE.print(s);
}

void tuh_giga_init(void)
{
  tu_step("[TUH] step: pins...");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  __HAL_RCC_GPIOB_CLK_ENABLE();
#pragma GCC diagnostic pop
  pin_function(PB_14, STM_PIN_DEFINE_SPEED(STM_MODE_AF_PP, GPIO_NOPULL, GPIO_AF12_OTG2_FS, GPIO_SPEED_FREQ_VERY_HIGH)); // DM
  pin_function(PB_15, STM_PIN_DEFINE_SPEED(STM_MODE_AF_PP, GPIO_NOPULL, GPIO_AF12_OTG2_FS, GPIO_SPEED_FREQ_VERY_HIGH)); // DP
  tu_step("rcc...");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
#pragma GCC diagnostic pop
  // NOTE: do NOT enable the ULPI clock: Giga has no ULPI PHY chip, and a
  // clocked-but-floating ULPI wrapper holds the DWC2 core in reset (CSRST
  // never clears — verified during bringup).
  // NOTE: no HSI48/CRS setup needed — Mbed already runs HSI48 (HSIRDY=1,
  // shared USB clock mux) for the FS console; verified during bringup.
  GIGATINYUSB_CONSOLE.print(" AHB1ENR="); GIGATINYUSB_CONSOLE.println(RCC->AHB1ENR, HEX);
  tu_step("tuh_init...");
  {
    const tusb_rhport_init_t rh_init =
    {
      .role = TUSB_ROLE_HOST,
      .speed = TUSB_SPEED_FULL
    };
    (void) tusb_init(1, &rh_init);
  }
  GIGATINYUSB_CONSOLE.println("[TUH] init done");
}

// Read-only clock/PHY state probe ('p' key). Never hangs, never writes.
// Proved its worth during bringup (caught the ULPI-clock reset hang).
void tuh_giga_probe(void)
{
  uint32_t gsnpsid = ((volatile uint32_t *)0x40040040UL)[0];
  uint32_t ghwcfg2 = ((volatile uint32_t *)0x40040048UL)[0];
  uint32_t grstctl = ((volatile uint32_t *)0x40040010UL)[0];
  uint32_t gusbcfg = ((volatile uint32_t *)0x4004000CUL)[0];
  uint32_t gccfg   = ((volatile uint32_t *)0x40040038UL)[0];
  uint32_t rcc_cr  = RCC->CR;
  uint32_t usbsel  = (RCC->D2CCIP2R >> 20) & 0x3UL; // 0:HSI48? 1:PLL1Q 2:PLL3Q 3:HSE (RM0399)
  uint32_t ahb1enr = RCC->AHB1ENR;
  char line[160];
  snprintf(line, sizeof(line),
    "[TUH] probe GSNPSID=%08lx GHWCFG2=%08lx GRSTCTL=%08lx(AHBIDL=%lu CSRST=%lu)",
    (unsigned long)gsnpsid, (unsigned long)ghwcfg2, (unsigned long)grstctl,
    (unsigned long)((grstctl >> 31) & 1), (unsigned long)(grstctl & 1));
  GIGATINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[TUH] probe GUSBCFG_PHYSEL=%lu GCCFG_PWRDWN=%lu HSI48ON=%lu HSIRDY=%lu USBSRC=%lu OTGHSEN=%lu",
    (unsigned long)((gusbcfg >> 6) & 1), (unsigned long)((gccfg >> 16) & 1),
    (unsigned long)((rcc_cr >> 12) & 1), (unsigned long)((rcc_cr >> 13) & 1),
    (unsigned long)usbsel, (unsigned long)((ahb1enr >> 25) & 1));
  GIGATINYUSB_CONSOLE.println(line);
}
