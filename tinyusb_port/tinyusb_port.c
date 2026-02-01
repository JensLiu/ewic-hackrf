/*
 * TinyUSB Device Port for HackRF
 *
 * Replaces hackrf_usb for USB device (TX/RX, vendor requests).
 * - USB0 hardware init, ISR
 * - Descriptor callbacks (HackRF descriptors)
 * - SysTick for tusb_time_delay_ms
 * - Debug printf -> UART
 */

#include "tusb.h"
#include "tinyusb_port_debug.h"
#include "hackrf_core.h"
#include "usb_descriptor.h"
#include "usb_device.h"
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

#define BOARD_TUD_RHPORT 0

/*---------------------------------------------------------------------------*/
/* Descriptor callbacks - return HackRF descriptor data                      */
/*---------------------------------------------------------------------------*/

uint8_t const *tud_descriptor_device_cb(void) {
  TUSB_PORT_DBG("desc: device");
  return usb_descriptor_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  TUSB_PORT_DBG("desc: config");
  return usb_descriptor_configuration_high_speed;
}

uint8_t const *tud_descriptor_device_qualifier_cb(void) {
  TUSB_PORT_DBG("desc: dev_qual");
  return usb_descriptor_device_qualifier;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
  (void)index;
  TUSB_PORT_DBG("desc: other_speed_config");
  return usb_descriptor_configuration_full_speed;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  TUSB_PORT_DBG("desc: string %u", (unsigned)index);
  if (index >= 5) return NULL;
  if (!usb_descriptor_strings[index]) return NULL;
  return (const uint16_t *)usb_descriptor_strings[index];
}

/*---------------------------------------------------------------------------*/
/* Event hook: called when DCD queues an event (ISR context). If you see
 * "evt" but never "USBD" or "desc:", the main loop is not draining the queue. */
/*---------------------------------------------------------------------------*/
static volatile uint32_t s_events_queued_in_isr;

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
  (void)rhport;
  (void)in_isr;
  s_events_queued_in_isr++;
  TUSB_PORT_DBG_EVT(eventid);
}

uint32_t tinyusb_events_queued_in_isr_get_and_reset(void) {
  uint32_t n = s_events_queued_in_isr;
  s_events_queued_in_isr = 0;
  return n;
}

/*---------------------------------------------------------------------------*/
/* Hardware init                                                             */
/*---------------------------------------------------------------------------*/

void tinyusb_hardware_init(void) {
  /* 1. Enable USB0 clocks (same as original HackRF) */
  CCU1_CLK_M4_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_M4_USB0_STAT & CCU_CLK_STAT_RUN)) {}
  CCU1_CLK_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_USB0_STAT & CCU_CLK_STAT_RUN)) {}

  /* 2. USB0 peripheral reset via RGU (pulse; no wait to avoid hanging) */
  RESET_CTRL0 = RESET_CTRL0_USB0_RST;
  RESET_CTRL0 = 0;

  /* 3. Enable USB0 PHY (LPC43xx: bit 5 = 0 enables, 1 = disabled) */
  CREG_CREG0 &= ~CREG_CREG0_USB0PHY;
  delay(10000);
}

/*---------------------------------------------------------------------------*/
/* TinyUSB device init - call after cpu_clock_init()                         */
/*---------------------------------------------------------------------------*/

bool tinyusb_device_init(void) {
  tinyusb_hardware_init();
  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_HIGH
  };
  if (!tusb_init(BOARD_TUD_RHPORT, &dev_init))
    return false;

  /* Present device to host (required on some ports when no VBUS detection) */
  tud_connect();

  TUSB_PORT_DBG("USB0 init: USBCMD=%08lx USBSTS=%08lx PORTSC1=%08lx",
                (unsigned long)CI_HS_REG(BOARD_TUD_RHPORT)->USBCMD,
                (unsigned long)CI_HS_REG(BOARD_TUD_RHPORT)->USBSTS,
                (unsigned long)CI_HS_REG(BOARD_TUD_RHPORT)->PORTSC1);

  /* SysTick 1 ms for tusb_time_delay_ms and board_millis (assumes 204 MHz AHB) */
  systick_set_reload(204000 - 1);
  systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB);
  systick_interrupt_enable();
  systick_counter_enable();

  return true;
}

/*---------------------------------------------------------------------------*/
/* USB0 ISR - forward to TinyUSB device stack                                 */
/*---------------------------------------------------------------------------*/

static volatile uint32_t s_usb_isr_count;

void tinyusb_usb0_isr(void) {
  s_usb_isr_count++;
  tud_int_handler(BOARD_TUD_RHPORT);
  /* Workaround: main loop never sees queue writes (wr=0 rd=0). Process queue here
   * in ISR context so we see our own writes; keeps enumeration working. */
  tud_task();
  __asm__ volatile ("dsb" ::: "memory");
  /* Rate-limited ISR debug: every 10000th call */
  if (TUSB_PORT_DEBUG && (s_usb_isr_count % 10000) == 0) {
    tusb_uart_printf("[isr] cnt=%lu evt=%lu\r\n", (unsigned long)s_usb_isr_count,
                     (unsigned long)s_events_queued_in_isr);
  }
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
