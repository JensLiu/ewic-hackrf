#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hackrf_core.h"
#include "platform_detect.h"
#include "uart_support/uart.h"
#include "uart_support/uart_api_transceiver.h"
#include <libopencm3/lpc43xx/ccu.h>
#include <libopencm3/lpc43xx/cgu.h>
#include <libopencm3/lpc43xx/gpio.h>
#include <libopencm3/lpc43xx/m4/nvic.h>
#include <libopencm3/lpc43xx/scu.h>
#include <libopencm3/lpc43xx/uart.h>

#include "cpld_xc2c.h"
#include "usb_request.h"
#include "usb_standard_request.h"
#include <libopencm3/lpc43xx/ipc.h>

// usb
#include "streaming.h"
#include "tuning.h"
#include "usb_api_m0_state.h"
#include "usb_bulk_buffer.h"
#define USB_TRANSFER_SIZE 0x4000

// USB vendor request handler (stub - UART is used for control instead of USB)
static usb_request_status_t
usb_vendor_request(usb_endpoint_t *const endpoint,
                   const usb_transfer_stage_t stage) {
  (void)endpoint;
  (void)stage;
  return USB_REQUEST_STATUS_STALL;
}

// USB request handlers required by usb_request.c
const usb_request_handlers_t usb_request_handlers = {
    .standard = usb_standard_request,
    .class = 0,
    .vendor = usb_vendor_request,
    .reserved = 0,
};

// Extern declarations for linker symbols
extern uint32_t __ram_m0_start__;
extern uint32_t _etext_rom;
extern uint32_t _etext_ram;
extern uint32_t _text_ram;
extern uint32_t __m0_start__;
extern uint32_t __m0_end__;

// Prototypes for missing functions (not in headers)
static void m0_rom_to_ram() {
  uint32_t *dest = &__ram_m0_start__;

  // Calculate the base address of ROM
  uint32_t base = (uint32_t)(&_etext_rom - (&_etext_ram - &_text_ram));

  // M0 image location, relative to the start of ROM
  uint32_t src = (uint32_t)&__m0_start__;

  uint32_t len = (uint32_t)&__m0_end__ - (uint32_t)src;
  memcpy(dest, (uint32_t *)(base + src), len);
}

static bool cpld_jtag_sram_load(jtag_t *const jtag) {
  cpld_jtag_take(jtag);
  cpld_xc2c64a_jtag_sram_write(jtag, &cpld_hackrf_program_sram);
  const bool success = cpld_xc2c64a_jtag_sram_verify(
      jtag, &cpld_hackrf_program_sram, &cpld_hackrf_verify);
  cpld_jtag_release(jtag);
  return success;
}

// Additional includes for missing symbols
#include "clkin.h"
#include "cpld_jtag.h"
#include "hackrf_ui.h"
#include "mixer.h"
#include "operacake.h"
#include "portapack.h"
#include "rf_path.h"

char DISPLAY_BUFFER[512];

int main(void) {
  static uint32_t led_counter = 0;
  static int led_state = 0;
  m0_rom_to_ram();
  detect_hardware_platform();
  pin_setup();
  uart_pin_setup();
  enable_1v8_power(); // enable 1V8 power supply so that the 1V8 LED lights up
  // Set up mixer before enabling RF power, because its
  // GPO is used to control the antenna bias tee.
  mixer_setup(&mixer);
  cpu_clock_init();
  uart_setup();

  /* Wake the M0 */
  ipc_halt_m0();
  ipc_start_m0((uint32_t)&__ram_m0_start__);

  if (!cpld_jtag_sram_load(&jtag_cpld)) {
    halt_and_flash(6000000);
  }

  portapack_init();

  hackrf_ui()->init();

  rf_path_init(&rf_path);
  // Configure radio
  sample_rate_set(10000000);              // 10 Msps
  baseband_filter_bandwidth_set(5000000); // 5 MHz filter
  set_freq(433000000ULL);                 // 433 MHz (ISM band)

  bool operacake_allow_gpio;
  if (hackrf_ui()->operacake_gpio_compatible()) {
    operacake_allow_gpio = true;
  } else {
    operacake_allow_gpio = false;
  }
  operacake_init(operacake_allow_gpio);

  // FIXME: clock detection on r9 only works when calling init twice
  if (detected_platform() == BOARD_ID_HACKRF1_R9) {
    clkin_detect_init();
    clkin_detect_init();
  }

  // Debug: Turn on LED1 indicating we reached this point
  led_on(LED1);
  delay_1us(100000);

  uart_send_str("HackRF UART Ready.\n");

  while (1) {
    uint32_t usb_count = 0;

    transceiver_startup(TRANSCEIVER_MODE_RX);

    baseband_streaming_enable(&sgpio_config);

    while (1) {
      if ((m0_state.m0_count - usb_count) >= USB_TRANSFER_SIZE) {
        uart_send_str("Data Available\n");
        const uint32_t idx = usb_count & USB_BULK_BUFFER_MASK;
        sprintf(DISPLAY_BUFFER,
                "usb_bulk_buffer"
                "[%lu..%lu]: ",
                (unsigned long)idx,
                (unsigned long)(idx + USB_TRANSFER_SIZE - 1));
        for (uint32_t i = 0; i < USB_TRANSFER_SIZE; i++) {
          char byte = usb_bulk_buffer[idx + i];
          sprintf(&DISPLAY_BUFFER[strlen(DISPLAY_BUFFER)], "%02X ",
                  (unsigned int)(uint8_t)byte);
        }
        sprintf(&DISPLAY_BUFFER[strlen(DISPLAY_BUFFER)], "\n");
        uart_send_str(DISPLAY_BUFFER);
        // rf_uart_send((const char *)&usb_bulk_buffer[idx], USB_TRANSFER_SIZE);
        usb_count += USB_TRANSFER_SIZE;
        m0_state.m4_count +=
            USB_TRANSFER_SIZE; // Tell M0 we consumed data, freeing buffer space
      }
      // Blink Logic(Non - blocking) led_counter++;
      // if (led_counter > 500000) { // Adjust speed as needed
      //   led_counter = 0;
      //   led_state++;

      //   if (led_state % 3 == 0) {
      //     led_on(LED1);
      //     led_off(LED2);
      //     led_off(LED3);
      //   } else if (led_state % 3 == 1) {
      //     led_off(LED1);
      //     led_on(LED2);
      //     led_off(LED3);
      //   } else {
      //     led_off(LED1);
      //     led_off(LED2);
      //     led_on(LED3);
      //   }
      // }
    }

    transceiver_shutdown();
  }

  /* Blink LED1/2/3 on the board. */
  // uint32_t led_counter = 0;
  // int led_state = 0;

  // while (1) {
  //   // Check for Data or Error` (Overrun)
  //   uart_rx_data_ready_t status = uart_rx_data_ready(UART0);

  //   if (status == UART_RX_DATA_READY || status == UART_RX_DATA_ERROR) {
  //     // Check if RDR (Receive Data Ready) is actually set in hardware to
  //     avoid
  //     // blocking
  //     if (UART_LSR(UART0) & UART_LSR_RDR) {
  //       char ch = uart_read_char();
  //       // sprintf(DISPLAY_BUFFER, "%c", ch);
  //       sprintf(DISPLAY_BUFFER, "Recv: %c (Status: %d)\n", ch, status);
  //       uart_send_str(DISPLAY_BUFFER);
  //     }
  //   }

  //   // Blink Logic (Non-blocking)
  //   led_counter++;
  //   if (led_counter > 500000) { // Adjust speed as needed
  //     led_counter = 0;
  //     led_state++;

  //     if (led_state % 3 == 0) {
  //       led_on(LED1);
  //       led_off(LED2);
  //       led_off(LED3);
  //     } else if (led_state % 3 == 1) {
  //       led_off(LED1);
  //       led_on(LED2);
  //       led_off(LED3);
  //     } else {
  //       led_off(LED1);
  //       led_off(LED2);
  //       led_on(LED3);
  //     }
  //   }
  // }

  return 0;
}
