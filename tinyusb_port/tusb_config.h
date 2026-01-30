/*
 * TinyUSB Configuration for HackRF USB Device
 *
 * Replaces hackrf_usb stack: vendor control + bulk for TX/RX/streaming.
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
#define CFG_TUSB_DEBUG        3
#define CFG_TUD_LOG_LEVEL     3
// Reduce to 0 (or omit TU_LOG_USBD) to avoid UART blocking from ISR: CFG_TUSB_DEBUG 0 and/or CFG_TUD_LOG_LEVEL 0

// TinyUSB debug -> same UART path as uart_print (DISPLAY_BUFFER + uart_send_str).
// View at 921600 baud on UART0 (e.g. screen /dev/ttyUSB0 921600).
#define CFG_TUSB_DEBUG_PRINTF tusb_uart_printf
extern int tusb_uart_printf(const char *format, ...);

//--------------------------------------------------------------------
// Device only (HackRF as USB device to PC)
//--------------------------------------------------------------------
#define CFG_TUD_ENABLED       1
#define CFG_TUH_ENABLED       0

//--------------------------------------------------------------------
// Device stack
//--------------------------------------------------------------------
/* Larger queue so enumeration (many events: reset, setup, suspend) doesn't overflow */
#define CFG_TUD_TASK_QUEUE_SZ 1024

#define CFG_TUD_VENDOR        1
#define CFG_TUD_VENDOR_EPSIZE 512
#define CFG_TUD_VENDOR_RX_BUFSIZE  512
#define CFG_TUD_VENDOR_TX_BUFSIZE  512
/* Use buffered mode with manual RX for streaming */
#define CFG_TUD_VENDOR_TXRX_BUFFERED 1
#define CFG_TUD_VENDOR_RX_MANUAL_XFER 1
/* Disable auto-ZLP: bridge continuously feeds data */
#define CFG_TUD_VENDOR_TX_ZLP_AFTER_FULL_PACKET 0

#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_ENDPOINT0_BUFSIZE 64

//--------------------------------------------------------------------
// Port
//--------------------------------------------------------------------
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
