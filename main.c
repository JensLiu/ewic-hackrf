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
#include "usb_descriptor.h"
#include "usb_type.h"
#include <rom_iap.h>

#include "clkin.h"
#include "cpld_xc2c.h"
#include "fpga.h"
#include "hackrf_ui.h"
#include "operacake.h"
#include "platform_detect.h"
#include "portapack.h"
#include "selftest.h"

#include "tinyusb_port.h"
#include "ftdi_host.h"
#include "uart.h"
#include "ci_hs_hackrf.h"
#include "tusb_config.h"  /* for tusb_uart_printf */

extern uint32_t __m0_start__;
extern uint32_t __m0_end__;
extern uint32_t __ram_m0_start__;
extern uint32_t _etext_ram, _text_ram, _etext_rom;

void usb_set_descriptor_by_serial_number(void) {
  iap_cmd_res_t iap_cmd_res;

  /* Read IAP Serial Number Identification */
  iap_cmd_res.cmd_param.command_code = IAP_CMD_READ_SERIAL_NO;
  iap_cmd_call(&iap_cmd_res);

  if (iap_cmd_res.status_res.status_ret == CMD_SUCCESS) {
    usb_descriptor_string_serial_number[0] =
        USB_DESCRIPTOR_STRING_SERIAL_BUF_LEN;
    usb_descriptor_string_serial_number[1] = USB_DESCRIPTOR_TYPE_STRING;

    /* 32 characters of serial number, convert to UTF-16LE */
    for (size_t i = 0; i < USB_DESCRIPTOR_STRING_SERIAL_LEN; i++) {
      const uint_fast8_t nibble =
          (iap_cmd_res.status_res.iap_result[i >> 3] >> (28 - (i & 7) * 4)) &
          0xf;
      const char c = (nibble > 9) ? ('a' + nibble - 10) : ('0' + nibble);
      usb_descriptor_string_serial_number[2 + i * 2] = c;
      usb_descriptor_string_serial_number[3 + i * 2] = 0x00;
    }
  } else {
    usb_descriptor_string_serial_number[0] = 2;
    usb_descriptor_string_serial_number[1] = USB_DESCRIPTOR_TYPE_STRING;
  }
}

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

#ifndef DFU_MODE
  usb_set_descriptor_by_serial_number();
#endif

  uart_print("\r\n[USB] HackRF USB Host starting...\r\n");

  /* Initialize USB host stack */
  if (!tinyusb_host_init()) {
    uart_print("[USB] ERROR: Host init failed!\r\n");
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

  uart_print("[USB] Waiting for device...\r\n");

  uint32_t last_status_print = 0;
  bool ftdi_was_ready = false;
  uint32_t loop_count = 0;
  
  while (true) {
    loop_count++;
    
    /* Ensure ISR-written queue data is visible before we drain (ARM DSB). */
    __asm__ volatile("dsb" ::: "memory");
    
    /* Always run tuh_task so host events are processed. */
    tuh_task();
    
    /* Check FTDI status and print when it changes */
    bool ftdi_ready = ftdi_host_ready();
    if (ftdi_ready != ftdi_was_ready) {
      if (ftdi_ready) {
        uart_print("[MAIN] FTDI connected and ready!\r\n");
      } else {
        uart_print("[MAIN] FTDI disconnected\r\n");
      }
      ftdi_was_ready = ftdi_ready;
    }
    
    /* Print status every 5 seconds */
    uint32_t now = board_millis();
    if (now - last_status_print >= 5000) {
      last_status_print = now;
      uint32_t isr_count = tinyusb_usb_isr_count_get_and_reset();
      tusb_uart_printf("[USB] t=%lus ISRs=%lu PORTSC1=0x%08lx\r\n",
                       (unsigned long)(now/1000),
                       (unsigned long)isr_count,
                       (unsigned long)CI_HS_REG(0)->PORTSC1);
      loop_count = 0;
    }
  }

  return 0;
}
