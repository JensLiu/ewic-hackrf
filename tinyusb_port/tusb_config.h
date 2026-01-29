/*
 * TinyUSB Configuration for HackRF USB Host
 * 
 * This file configures TinyUSB to work with HackRF's LPC4320 using libopencm3.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// LPC4320/LPC4330 is part of the LPC43xx family
#define CFG_TUSB_MCU          OPT_MCU_LPC43XX

// No RTOS - bare metal
#define CFG_TUSB_OS           OPT_OS_NONE

// Debug level (0 = off, 1 = errors, 2 = warnings, 3 = info)
#define CFG_TUSB_DEBUG        0

// Enable debug printf (requires implementing board_uart_write)
// #define CFG_TUSB_DEBUG_PRINTF board_uart_write

//--------------------------------------------------------------------
// Memory Configuration
//--------------------------------------------------------------------

/* USB DMA on LPC43xx requires buffers in AHB SRAM with proper alignment.
 * The LPC4320 has:
 *   - Local SRAM (128KB) at 0x10000000-0x1001FFFF
 *   - AHB SRAM   (32KB+16KB) at 0x20000000-0x2000BFFF
 * USB DMA works best with AHB SRAM.
 */

// Place USB buffers in AHB SRAM section (defined in linker script)
#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION   __attribute__((section(".ahb_sram")))
#endif

// EHCI requires 32-byte alignment for QH and qTD structures
#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN     __attribute__((aligned(32)))
#endif

//--------------------------------------------------------------------
// Host Configuration
//--------------------------------------------------------------------

// Enable Host stack
#define CFG_TUH_ENABLED       1

// Disable Device stack (we're only doing host mode)
#define CFG_TUD_ENABLED       0

//------------------------- Port Configuration --------------------------

// Use USB0 (high-speed capable port) for USB Host
// USB0 is the main USB port on HackRF, exposed via the USB connector
// Note: USB1 is full-speed only and not easily accessible on HackRF
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

// Maximum speed for host mode
// USB0 on LPC43xx supports High-Speed (480 Mbps)
// Change to OPT_MODE_FULL_SPEED if you have issues or want compatibility
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_HIGH_SPEED
#endif

//--------------------------------------------------------------------
// Host Stack Configuration
//--------------------------------------------------------------------

// Size of buffer for enumeration process
#define CFG_TUH_ENUMERATION_BUFSIZE   256

// Maximum number of devices supported (excluding root hub)
// Each hub port counts as a potential device slot
#define CFG_TUH_DEVICE_MAX    4

// Hub class support - enable to use USB hubs
#define CFG_TUH_HUB           1

// HID class support (keyboards, mice, gamepads, etc.)
#define CFG_TUH_HID           4

// CDC class support (serial adapters, modems)
#define CFG_TUH_CDC           2

// CDC subclass drivers (for USB-to-serial chips)
#define CFG_TUH_CDC_FTDI      1   // FTDI chips (FT232, etc.)
#define CFG_TUH_CDC_CP210X    1   // Silicon Labs CP210x
#define CFG_TUH_CDC_CH34X     1   // WCH CH340/CH341

// MSC class support (USB mass storage - flash drives, etc.)
#define CFG_TUH_MSC           1

// Vendor specific class (for custom devices)
#define CFG_TUH_VENDOR        0

//--------------------------------------------------------------------
// Class-specific Configuration
//--------------------------------------------------------------------

//------------- HID -------------//

// Size of HID IN endpoint buffer
#define CFG_TUH_HID_EPIN_BUFSIZE    64

// Size of HID OUT endpoint buffer
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

//------------- CDC -------------//

// Line control state on enumeration (DTR and RTS high)
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM    0x03

// Line coding on enumeration: 115200 baud, 1 stop bit, no parity, 8 data bits
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM     { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

//------------- MSC -------------//

// No special MSC configuration needed

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
