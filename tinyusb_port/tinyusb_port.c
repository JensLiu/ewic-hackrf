/*
 * TinyUSB Port for HackRF (USB Host mode)
 *
 * This file provides:
 *   - USB0 hardware initialization for host mode
 *   - Interrupt handling bridge
 *   - TinyUSB callbacks
 *   - Timing functions
 */

#include "tusb.h"
#include "hackrf_core.h"

#include <stdio.h>

// libopencm3 includes
#include <libopencm3/lpc43xx/cgu.h>
#include <libopencm3/lpc43xx/ccu.h>
#include <libopencm3/lpc43xx/creg.h>
#include <libopencm3/lpc43xx/scu.h>
#include <libopencm3/lpc43xx/m4/nvic.h>
#include <libopencm3/cm3/systick.h>

// For UART debug output
#include "uart.h"

// CCU clock configuration bits (LPC43xx)
// Bit 0: RUN - Enable clock
// Bit 1: AUTO - Auto power down when not needed
#define CCU_CLK_CFG_RUN   (1 << 0)
#define CCU_CLK_CFG_AUTO  (1 << 1)
#define CCU_CLK_STAT_RUN  (1 << 0)

//--------------------------------------------------------------------
// Timing Support
//--------------------------------------------------------------------

// Millisecond tick counter (incremented by SysTick)
static volatile uint32_t tinyusb_millis = 0;

// SysTick handler for TinyUSB timing
// Note: If you already have a SysTick handler, merge this into it
void tinyusb_systick_handler(void) {
  tinyusb_millis++;
}

// TinyUSB requires this function for timing
uint32_t board_millis(void) {
  return tinyusb_millis;
}

// Initialize SysTick for 1ms ticks
// Call this if you don't already have SysTick configured
static void systick_init(void) {
  // Configure SysTick for 1ms interrupt
  // System clock is 204 MHz after cpu_clock_init()
  systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB);
  systick_set_reload(204000 - 1);  // 204 MHz / 1000 = 204000 ticks per ms
  systick_interrupt_enable();
  systick_counter_enable();
}

//--------------------------------------------------------------------
// USB Hardware Initialization
//--------------------------------------------------------------------

/*
 * Initialize USB0 for Host mode
 *
 * USB0 on LPC43xx is an EHCI-compatible high-speed controller.
 * This function:
 *   1. Enables USB0 clocks (PLL0USB should already be configured)
 *   2. Enables the USB0 PHY
 *   3. Configures pins (if needed)
 *
 * Prerequisites:
 *   - cpu_clock_init() must have been called (sets up PLL0USB)
 */
void tinyusb_hardware_init(void) {
  // USB0 clock should already be configured by cpu_clock_init() in hackrf_core.c
  // CGU_BASE_USB0_CLK is set to use PLL0USB (480 MHz)
  
  // Ensure USB0 peripheral clock is enabled
  // CCU1 controls the USB0 peripheral clock
  CCU1_CLK_M4_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_M4_USB0_STAT & CCU_CLK_STAT_RUN)) {}
  
  // Enable USB0 register interface clock
  CCU1_CLK_USB0_CFG = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(CCU1_CLK_USB0_STAT & CCU_CLK_STAT_RUN)) {}

  // Enable USB0 PHY
  // CREG0 bit 5 controls USB0 PHY power
  // 0 = PHY powered, 1 = PHY powered down
  CREG_CREG0 &= ~(1 << 5);  // Clear bit to enable PHY
  
  // Small delay for PHY to stabilize
  delay(10000);
  
  // USB0 D+/D- pins are directly connected to the USB PHY,
  // no pin mux configuration needed for them.
  
  // If your setup needs VBUS power control, configure the GPIO here
  // HackRF doesn't have built-in VBUS control for host mode,
  // so you may need external power for connected devices.
}

/*
 * Initialize TinyUSB stack for USB Host mode
 *
 * Call this after:
 *   - cpu_clock_init()
 *   - tinyusb_hardware_init()
 */
bool tinyusb_host_init(void) {
  // Initialize timing (SysTick)
  systick_init();
  
  // Initialize USB hardware
  tinyusb_hardware_init();
  
  // Initialize TinyUSB host stack
  tusb_rhport_init_t host_init = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_AUTO  // Auto-detect speed
  };
  
  return tusb_init(BOARD_TUH_RHPORT, &host_init);
}

//--------------------------------------------------------------------
// USB Interrupt Handler
//--------------------------------------------------------------------

/*
 * USB0 Interrupt Service Routine
 *
 * This forwards USB0 interrupts to TinyUSB.
 * 
 * Note: libopencm3 expects the handler to be named usb0_isr().
 * If you have existing USB code using usb0_isr(), you'll need to
 * conditionally compile or merge the handlers.
 */
void tinyusb_usb0_isr(void) {
  tusb_int_handler(0, true);
}

// For standalone use, this can be the actual ISR
// Uncomment if you're replacing the existing USB0 handler:
// void usb0_isr(void) __attribute__((alias("tinyusb_usb0_isr")));

//--------------------------------------------------------------------
// TinyUSB Callbacks - Device Connection
//--------------------------------------------------------------------

// Called when a device is successfully mounted (enumerated)
void tuh_mount_cb(uint8_t dev_addr) {
  uart_send_str("[USB Host] Device mounted at address ");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\n", dev_addr);
  uart_send_str(buf);
}

// Called when a device is unmounted (disconnected)
void tuh_umount_cb(uint8_t dev_addr) {
  uart_send_str("[USB Host] Device unmounted from address ");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\n", dev_addr);
  uart_send_str(buf);
}

//--------------------------------------------------------------------
// TinyUSB Callbacks - HID (Keyboard, Mouse, etc.)
//--------------------------------------------------------------------

#if CFG_TUH_HID

// Called when HID device is mounted
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, 
                      uint8_t const* desc_report, uint16_t desc_len) {
  (void)desc_report;
  (void)desc_len;
  
  uart_send_str("[USB Host] HID device mounted\n");
  
  // Request to receive HID reports
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    uart_send_str("[USB Host] Failed to request HID report\n");
  }
}

// Called when HID device is unmounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr;
  (void)instance;
  uart_send_str("[USB Host] HID device unmounted\n");
}

// Called when HID report is received
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const* report, uint16_t len) {
  // Process the HID report here
  // For a keyboard, report contains key codes
  // For a mouse, report contains movement and button data
  
  uart_send_str("[USB Host] HID report received, len=");
  char buf[16];
  snprintf(buf, sizeof(buf), "%d\n", len);
  uart_send_str(buf);
  
  // Continue receiving reports
  tuh_hid_receive_report(dev_addr, instance);
}

#endif // CFG_TUH_HID

//--------------------------------------------------------------------
// TinyUSB Callbacks - CDC (Serial)
//--------------------------------------------------------------------

#if CFG_TUH_CDC

// Called when CDC device is mounted
void tuh_cdc_mount_cb(uint8_t idx) {
  uart_send_str("[USB Host] CDC device mounted, idx=");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\n", idx);
  uart_send_str(buf);
}

// Called when CDC device is unmounted
void tuh_cdc_umount_cb(uint8_t idx) {
  uart_send_str("[USB Host] CDC device unmounted\n");
  (void)idx;
}

// Called when data is received from CDC device
void tuh_cdc_rx_cb(uint8_t idx) {
  uint8_t buf[64];
  uint32_t count = tuh_cdc_read(idx, buf, sizeof(buf));
  
  if (count > 0) {
    uart_send_str("[USB Host] CDC RX: ");
    // Forward to UART or process
    // uart_send_data(buf, count);
    uart_send_str("\n");
  }
}

#endif // CFG_TUH_CDC

//--------------------------------------------------------------------
// TinyUSB Callbacks - MSC (Mass Storage)
//--------------------------------------------------------------------

#if CFG_TUH_MSC

// Called when MSC device is mounted
void tuh_msc_mount_cb(uint8_t dev_addr) {
  uart_send_str("[USB Host] MSC device mounted at ");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\n", dev_addr);
  uart_send_str(buf);
  
  // Get device capacity
  uint32_t block_count = tuh_msc_get_block_count(dev_addr, 0);
  uint32_t block_size = tuh_msc_get_block_size(dev_addr, 0);
  
  uart_send_str("[USB Host] Capacity: ");
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)(block_count / 2048));  // MB
  uart_send_str(buf);
  uart_send_str(" MB\n");
  
  (void)block_size;
}

// Called when MSC device is unmounted
void tuh_msc_umount_cb(uint8_t dev_addr) {
  uart_send_str("[USB Host] MSC device unmounted\n");
  (void)dev_addr;
}

#endif // CFG_TUH_MSC
