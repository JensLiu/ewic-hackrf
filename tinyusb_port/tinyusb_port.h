/*
 * TinyUSB Port for HackRF - Header
 */

#ifndef TINYUSB_PORT_H_
#define TINYUSB_PORT_H_

#include <stdint.h>
#include <stdbool.h>

/* TinyUSB host API (tuh_task, etc.) */
#include "host/usbh.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize TinyUSB hardware and host stack
 *
 * Call this after cpu_clock_init() and other basic HackRF initialization.
 * This will:
 *   - Initialize SysTick for timing
 *   - Configure USB0 hardware
 *   - Initialize TinyUSB host stack
 *
 * @return true on success, false on failure
 */
bool tinyusb_host_init(void);

/**
 * USB0 interrupt handler for TinyUSB
 * 
 * Call this from your usb0_isr() if you have one, or use it as the ISR directly.
 */
void tinyusb_usb0_isr(void);

/**
 * SysTick handler for TinyUSB timing
 * 
 * Call this from your SysTick handler to increment the millisecond counter.
 * If you don't have a SysTick handler, tinyusb_host_init() will set one up.
 */
void tinyusb_systick_handler(void);

/**
 * Get current millisecond count
 * 
 * @return milliseconds since tinyusb_host_init() was called
 */
uint32_t board_millis(void);

/** Return USB ISR call count and reset it (for debug: see if host triggers any interrupts). */
uint32_t tinyusb_usb_isr_count_get_and_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* TINYUSB_PORT_H_ */
