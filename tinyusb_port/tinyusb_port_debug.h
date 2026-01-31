/*
 * TinyUSB Port - unified debug macros
 *
 * Single switch TUSB_PORT_DEBUG (in tusb_config.h) controls all port debug output:
 *   - TUSB_PORT_DBG():       descriptor callbacks, init (tinyusb_port.c)
 *   - TUSB_PORT_DBG_BRIDGE(): control/bulk path (hackrf_usb_bridge.c)
 *   - TUSB_PORT_DBG_EVT():   event hook (evt6, evt7, etc.)
 *
 * Does not affect TinyUSB's own CFG_TUSB_DEBUG / CFG_TUD_LOG_LEVEL.
 * Include tusb.h or tusb_config.h before this header so TUSB_PORT_DEBUG is defined.
 */

#ifndef TINYUSB_PORT_DEBUG_H_
#define TINYUSB_PORT_DEBUG_H_

#ifndef TUSB_PORT_DEBUG
#define TUSB_PORT_DEBUG 0
#endif

extern int tusb_uart_printf(const char *format, ...);
#define TUSB_PORT_DBG(fmt, ...)        do { if (TUSB_PORT_DEBUG) tusb_uart_printf("[port] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define TUSB_PORT_DBG_BRIDGE(fmt, ...) do { if (TUSB_PORT_DEBUG) tusb_uart_printf("[bridge] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define TUSB_PORT_DBG_EVT(eventid)     do { if (TUSB_PORT_DEBUG) tusb_uart_printf("evt%lu ", (unsigned long)(eventid)); } while(0)

#endif /* TINYUSB_PORT_DEBUG_H_ */
