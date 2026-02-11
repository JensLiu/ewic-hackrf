/*
 * TinyUSB Host Port for HackRF
 *
 * USB host mode to communicate with FTDI chip.
 * - USB0 hardware init, ISR
 * - SysTick for tusb_time_delay_ms
 * - Debug printf -> UART
 */

#include "tusb.h"
#include "tinyusb_port_debug.h"
#include "hackrf_core.h"
#include "uart.h"
#include "ci_hs_hackrf.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/lpc43xx/cgu.h>
#include <libopencm3/lpc43xx/ccu.h>
#include <libopencm3/lpc43xx/creg.h>
#include <libopencm3/lpc43xx/rgu.h>
#include <libopencm3/lpc43xx/m4/nvic.h>

#include <stdarg.h>
#include <stdio.h>

#define CCU_CLK_CFG_RUN   (1 << 0)
#define CCU_CLK_CFG_AUTO  (1 << 1)
#define CCU_CLK_STAT_RUN  (1 << 0)

#define BOARD_TUH_RHPORT 0

/*---------------------------------------------------------------------------*/
/* Hardware init                                                             */
/*---------------------------------------------------------------------------*/

void tinyusb_hardware_init(void) {
  /* 1. Enable USB0 clocks */
  CCU1_CLK_M4_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_M4_USB0_STAT & CCU_CLK_STAT_RUN)) {}
  CCU1_CLK_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_USB0_STAT & CCU_CLK_STAT_RUN)) {}

  /* 2. USB0 peripheral reset via RGU */
  RESET_CTRL0 = RESET_CTRL0_USB0_RST;
  RESET_CTRL0 = 0;

  /* 3. Enable USB0 PHY (LPC43xx: bit 5 = 0 enables) */
  CREG_CREG0 &= ~CREG_CREG0_USB0PHY;
  delay(10000);
}

/*---------------------------------------------------------------------------*/
/* TinyUSB host init - call after cpu_clock_init()                           */
/*---------------------------------------------------------------------------*/

bool tinyusb_host_init(void) {
  tinyusb_hardware_init();
  
  /* SysTick 1 ms for tusb_time_delay_ms and board_millis (assumes 204 MHz AHB) */
  systick_set_reload(204000 - 1);
  systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB);
  systick_interrupt_enable();
  systick_counter_enable();

  tusb_rhport_init_t host_init = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_HIGH
  };
  if (!tusb_init(BOARD_TUH_RHPORT, &host_init)) {
    tusb_uart_printf("[USB] tusb_init failed\r\n");
    return false;
  }

  tusb_uart_printf("[USB] Host init OK, PORTSC1=0x%08lx USBMODE=0x%08lx\r\n",
                   (unsigned long)CI_HS_REG(BOARD_TUH_RHPORT)->PORTSC1,
                   (unsigned long)CI_HS_REG(BOARD_TUH_RHPORT)->USBMODE);

  return true;
}

/*---------------------------------------------------------------------------*/
/* USB0 ISR - forward to TinyUSB host stack                                  */
/*---------------------------------------------------------------------------*/

static volatile uint32_t s_usb_isr_count;

void tinyusb_usb0_isr(void) {
  s_usb_isr_count++;
  
  tuh_int_handler(BOARD_TUH_RHPORT, true);
  __asm__ volatile ("dsb" ::: "memory");
}

uint32_t tinyusb_usb_isr_count_get_and_reset(void) {
  uint32_t n = s_usb_isr_count;
  s_usb_isr_count = 0;
  return n;
}

/* libopencm3 vector table expects this name */
void usb0_isr(void) {
  tinyusb_usb0_isr();
}

/*---------------------------------------------------------------------------*/
/* SysTick: 1 ms tick for board_millis and tusb_time_delay_ms                */
/*---------------------------------------------------------------------------*/

static volatile uint32_t s_millis;

void tinyusb_systick_handler(void) {
  s_millis++;
}

uint32_t board_millis(void) {
  return s_millis;
}

uint32_t tusb_time_millis_api(void) {
  return board_millis();
}

/* Override weak sys_tick_handler so millis advance */
void sys_tick_handler(void) {
  tinyusb_systick_handler();
}

/*---------------------------------------------------------------------------*/
/* TinyUSB debug printf -> UART (CFG_TUSB_DEBUG_PRINTF) via uart_print path  */
/* Uses same DISPLAY_BUFFER + uart_send_str as uart_print macro.             */
/*---------------------------------------------------------------------------*/

int tusb_uart_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int n = vsnprintf(DISPLAY_BUFFER, sizeof(DISPLAY_BUFFER), format, args);
  va_end(args);
  if (n > 0)
    uart_send_str(DISPLAY_BUFFER);
  return n;
}
