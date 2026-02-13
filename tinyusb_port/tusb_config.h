/*
 * TinyUSB Configuration for HackRF USB Host
 *
 * USB host mode to communicate with FTDI chip via the built-in
 * CDC host class driver (with FTDI serial sub-driver).
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

/* Port debug: 0 = off, 1 = descriptor/event/bridge/stream UART prints */
#ifndef TUSB_PORT_DEBUG
#define TUSB_PORT_DEBUG 0
#endif

/* Stream debug: 0 = off, 1 = rate-limited prints in rx/tx loop */
#ifndef TUSB_STREAM_DEBUG
#define TUSB_STREAM_DEBUG 0
#endif

// TinyUSB debug -> same UART path as uart_print (DISPLAY_BUFFER + uart_send_str).
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
// CDC Host + FTDI serial sub-driver
//--------------------------------------------------------------------

/* Number of CDC interfaces the host can track.
 * FT4232H has 4 interfaces; we want all of them available. */
#define CFG_TUH_CDC           4

/* Enable FTDI vendor-class serial driver inside the CDC host class.
 * This handles chip detection, baud rate divisor encoding (including
 * the H-type 120 MHz clock), endpoint open, and buffered stream I/O. */
#define CFG_TUH_CDC_FTDI      1

/* Automatically configure 115200 8N1 during enumeration via the FTDI
 * driver's async set_line_state_on_enum() state machine.  This is the
 * correct path – it chains control transfers with callbacks inside the
 * enumeration engine.  Do NOT use blocking tuh_cdc_set_*() from
 * tuh_cdc_mount_cb() as that causes re-entrancy in tuh_task(). */
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM \
  { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

/* Raise DTR+RTS on enum (like a terminal opening the port). */
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM \
  (CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS)

/* RX / TX buffer sizes – 512 matches the HS bulk max packet size. */
#define CFG_TUH_CDC_RX_BUFSIZE  512
#define CFG_TUH_CDC_TX_BUFSIZE  512
#define CFG_TUH_CDC_RX_EPSIZE   512
#define CFG_TUH_CDC_TX_EPSIZE   512

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
