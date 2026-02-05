/*
 * TinyUSB Configuration for HackRF USB Host
 *
 * USB host mode to communicate with FTDI chip.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Common
//--------------------------------------------------------------------
#define CFG_TUSB_MCU          OPT_MCU_LPC43XX
#define CFG_TUSB_OS           OPT_OS_NONE
#define CFG_TUSB_DEBUG        0
#define CFG_TUH_LOG_LEVEL     0
/* TinyUSB debug: 3 for bring-up; 0 for production - UART in ISR blocks streaming */

/* Port debug: 1 = descriptor/event/bridge/stream UART prints; 0 = off */
#ifndef TUSB_PORT_DEBUG
#define TUSB_PORT_DEBUG 1
#endif

/* Stream debug: rate-limited prints in rx/tx loop and completion (1 = on, 0 = off) */
#ifndef TUSB_STREAM_DEBUG
#define TUSB_STREAM_DEBUG 0
#endif

// TinyUSB debug -> same UART path as uart_print (DISPLAY_BUFFER + uart_send_str).
// View at 921600 baud on UART0 (e.g. screen /dev/ttyUSB0 921600).
#define CFG_TUSB_DEBUG_PRINTF tusb_uart_printf
extern int tusb_uart_printf(const char *format, ...);

//--------------------------------------------------------------------
// Host only (HackRF as USB host to FTDI)
//--------------------------------------------------------------------
#define CFG_TUD_ENABLED       0
#define CFG_TUH_ENABLED       1

//--------------------------------------------------------------------
// Host stack
//--------------------------------------------------------------------
/* Larger queue so enumeration events don't overflow */
#define CFG_TUH_TASK_QUEUE_SZ 128

/* Max number of devices we can enumerate */
#define CFG_TUH_DEVICE_MAX    1

/* Hub support (not needed for direct FTDI connection) */
#define CFG_TUH_HUB           0

/* Endpoint 0 buffer size */
#define CFG_TUH_ENUMERATION_BUFSIZE 256

/* Use EHCI for high-speed host on LPC43xx */
#define TUP_USBIP_EHCI

//--------------------------------------------------------------------
// Port
//--------------------------------------------------------------------
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
