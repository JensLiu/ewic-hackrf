/*
 * HackRF USB Host Firmware for FTDI Communication
 *
 * This firmware configures the HackRF as a USB host to communicate with
 * an FTDI chip (FT4232HQ). The HackRF is powered externally via VBUS/GND pins,
 * so the micro USB port is available for host functionality.
 *
 * Based on original HackRF firmware:
 * Copyright 2012-2022 Great Scott Gadgets <info@greatscottgadgets.com>
 * Copyright 2012 Jared Boone
 * Copyright 2013 Benjamin Vernoux
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <stddef.h>
#include <string.h>

#include <libopencm3/lpc43xx/ipc.h>
#include <libopencm3/lpc43xx/m4/nvic.h>
#include <libopencm3/lpc43xx/rgu.h>
#include <libopencm3/lpc43xx/timer.h>

#include <streaming.h>

#include "tuning.h"
#include <rom_iap.h>

#include "clkin.h"
#include "cpld_xc2c.h"
#include "fpga.h"
#include "hackrf_ui.h"
#include "operacake.h"
#include "platform_detect.h"
#include "portapack.h"
#include "selftest.h"

#include "ci_hs_hackrf.h"
#include "ftdi_host.h"
#include "tinyusb_port.h"
#include "tusb_config.h" /* for tusb_uart_printf */
#include "uart.h"

extern uint32_t __m0_start__;
extern uint32_t __m0_end__;
extern uint32_t __ram_m0_start__;
extern uint32_t _etext_ram, _text_ram, _etext_rom;

#ifndef PRALINE
static bool cpld_jtag_sram_load(jtag_t *const jtag) {
  cpld_jtag_take(jtag);
  cpld_xc2c64a_jtag_sram_write(jtag, &cpld_hackrf_program_sram);
  const bool success = cpld_xc2c64a_jtag_sram_verify(
      jtag, &cpld_hackrf_program_sram, &cpld_hackrf_verify);
  cpld_jtag_release(jtag);
  return success;
}
#endif

static void m0_rom_to_ram() {
  uint32_t *dest = &__ram_m0_start__;

  // Calculate the base address of ROM
  uint32_t base = (uint32_t)(&_etext_rom - (&_etext_ram - &_text_ram));

  // M0 image location, relative to the start of ROM
  uint32_t src = (uint32_t)&__m0_start__;

  uint32_t len = (uint32_t)&__m0_end__ - (uint32_t)src;
  memcpy(dest, (uint32_t *)(base + src), len);
}

int main(void) {
  // Copy M0 image from ROM before SPIFI is disabled
  m0_rom_to_ram();

  // This will be cleared if any self-test check fails.
  selftest.report.pass = true;

  detect_hardware_platform();
  pin_shutdown();
  uart_pin_setup();
#ifndef RAD1O
  clock_gen_shutdown();
#endif
  delay_us_at_mhz(10000, 96);
  pin_setup();
#ifndef PRALINE
  enable_1v8_power();
#ifndef RAD1O
  clock_gen_init();
#endif
#else
  enable_3v3aux_power();
#if !defined(DFU_MODE) && !defined(RAM_MODE)
  enable_1v2_power();
  enable_rf_power();
  clock_gen_init();
#endif
#endif
#ifdef HACKRF_ONE
  // Set up mixer before enabling RF power, because its
  // GPO is used to control the antenna bias tee.
  mixer_setup(&mixer);
#endif
#if (defined HACKRF_ONE || defined RAD1O)
  enable_rf_power();
#endif
#ifdef RAD1O
  clock_gen_init();
#endif
  cpu_clock_init();
  uart_setup();

  /* Wake the M0 */
  ipc_halt_m0();
  ipc_start_m0((uint32_t)&__ram_m0_start__);

#ifndef PRALINE
  if (!cpld_jtag_sram_load(&jtag_cpld)) {
    halt_and_flash(6000000);
  }
#else
  fpga_image_load(0);
  delay_us_at_mhz(100, 204);
  fpga_spi_selftest();
  fpga_sgpio_selftest();
#endif

#if (defined HACKRF_ONE || defined PRALINE)
  portapack_init();
#endif

  uart_send_str("\r\n[USB] HackRF USB Host starting...\r\n");

  /* Initialize USB host stack */
  if (!tinyusb_host_init()) {
    uart_send_str("[USB] ERROR: Host init failed!\r\n");
  }

  nvic_set_priority(NVIC_USB0_IRQ, 255);
  nvic_enable_irq(NVIC_USB0_IRQ);

  hackrf_ui()->init();
  rf_path_init(&rf_path);

#ifndef RAD1O
  rffc5071_lock_test(&mixer);
#endif

#ifdef PRALINE
  fpga_if_xcvr_selftest();
#endif

  bool operacake_allow_gpio = hackrf_ui()->operacake_gpio_compatible();
  operacake_init(operacake_allow_gpio);

  if (detected_platform() == BOARD_ID_HACKRF1_R9) {
    clkin_detect_init();
    clkin_detect_init();
  }

  uart_send_str("[USB] Waiting for device...\r\n");

  uint32_t last_status_print = 0;
  uint32_t last_write_time = 0;
  bool ftdi_was_ready = false;

  int i = 0;

  while (true) {
    /* Ensure ISR-written queue data is visible before we drain (ARM DSB). */
    __asm__ volatile("dsb" ::: "memory");

    /* Always run tuh_task so host events are processed. */
    tuh_task();

    const char *str = "Hello from HackRF USB host!\n";
    const bool ftdi_ready = ftdi_host_ready();
    
    if (ftdi_ready) {
      uint32_t now_w = board_millis();
      if (now_w - last_write_time >= 10) {
        last_write_time = now_w;
        const int len = strlen(str);
        ftdi_host_write(&str[i], 1);
        tuh_task();
        tusb_uart_printf("[TX] Sent '%c' to FTDI\r\n",
                         (str[i] >= 0x20 && str[i] < 0x7f) ? str[i] : '.');
        char ch;
        ftdi_host_read(&ch, 1);
        tuh_task();
        tusb_uart_printf("[ECHO] Received '%c' from FTDI\r\n",
                         (ch >= 0x20 && ch < 0x7f) ? ch : '.');
        i = (i + 1) % len; /* rotate the string by one character each time */
      }
    }

    if (ftdi_ready != ftdi_was_ready) {
      if (ftdi_ready) {
        uart_send_str("[MAIN] FTDI connected and ready!\r\n");
      } else {
        uart_send_str("[MAIN] FTDI disconnected\r\n");
      }
      ftdi_was_ready = ftdi_ready;
    }

    /* Print status every 5 seconds */
    uint32_t now = board_millis();
    if (now - last_status_print >= 5000) {
      last_status_print = now;
      uint32_t isr_count = tinyusb_usb_isr_count_get_and_reset();
      tusb_uart_printf("[USB] t=%lus ISRs=%lu PORTSC1=0x%08lx\r\n",
                       (unsigned long)(now / 1000), (unsigned long)isr_count,
                       (unsigned long)CI_HS_REG(0)->PORTSC1);
    }
  }

  return 0;
}
