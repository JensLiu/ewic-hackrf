/*
 * FT4232HQ USB host support for HackRF – CDC/FTDI driver layer.
 *
 * Uses TinyUSB's built-in CDC host class with the FTDI serial sub-driver.
 * The driver handles chip detection, baud-rate divisor encoding (including
 * the H-type 120 MHz clock), endpoint management, and buffered stream I/O.
 *
 * Line coding, data format, and modem control lines are configured
 * explicitly after mount via tuh_cdc_set_baudrate(),
 * tuh_cdc_set_data_format(), and tuh_cdc_set_control_line_state().
 *
 * This file implements the CDC callbacks and exposes a simple public API
 * for the rest of the firmware.
 */

#include "ftdi_host.h"
#include "custom_config.h"
#include "tinyusb_port.h"
#include "tusb.h"
#include "uart.h"

/* Target interface: 0->ttyUSB0, 1->ttyUSB1, 2->ttyUSB2, 3->ttyUSB3.
 * We use interface 2 for data transfer.  The CDC driver assigns each
 * FTDI interface an "idx" in mount order; we find ours by matching
 * the interface number. */
#define FTDI_TARGET_INTERFACE 2

/* CDC interface index of the target channel, set in tuh_cdc_mount_cb(). */
static int8_t s_cdc_idx = -1; /* -1 = not yet mounted */

static bool timeout_expired(uint32_t start_ms, uint32_t timeout_ms) {
  return (uint32_t)(board_millis() - start_ms) >= timeout_ms;
}

//--------------------------------------------------------------------+
// Public API – data transfer
//--------------------------------------------------------------------+

bool ftdi_host_ready(void) {
  return (s_cdc_idx >= 0) && tuh_cdc_mounted((uint8_t)s_cdc_idx);
}

uint32_t ftdi_host_write(const void *buffer, uint32_t len) {
  // tusb_uart_printf("ftdi_host_write sent %d bytes: ", len);
  // for (int i = 0; i < len; i++) {
  //   tusb_uart_printf("%02X ", ((uint8_t *)buffer)[i]);
  // }
  // tusb_uart_printf("\n");
#ifdef FTDI_IO_SAFETY_CHECKS
  if (!ftdi_host_ready())
    return 0;
#endif
  uint32_t n = tuh_cdc_write((uint8_t)s_cdc_idx, buffer, len);
  if (n > 0) {
    tuh_cdc_write_flush((uint8_t)s_cdc_idx);
  }
  return n;
}

uint32_t ftdi_host_write_blocking(const void *buffer, uint32_t len,
                                  uint32_t timeout_ms) {
#ifdef FTDI_IO_SAFETY_CHECKS
  if (!ftdi_host_ready() || (buffer == NULL) || (len == 0))
    return 0;
#endif
  // const uint32_t start_ms = board_millis();
  const uint8_t *ptr = (const uint8_t *)buffer;
  uint32_t total = 0;

  while (total < len) {
#ifdef FTDI_IO_SAFETY_CHECKS
    if (!ftdi_host_ready()) {
      break;
    }
#endif
#ifdef FTDI_BLOCKING_IO_CHECK_TIMEOUTS
    const uint32_t start_ms = board_millis();
#endif
    const uint32_t n =
        tuh_cdc_write((uint8_t)s_cdc_idx, ptr + total, len - total);
    if (n > 0) {
      total += n;
      (void)tuh_cdc_write_flush((uint8_t)s_cdc_idx);
      continue;
    }

    tuh_task();
#ifdef FTDI_BLOCKING_IO_CHECK_TIMEOUTS
    if (timeout_expired(start_ms, timeout_ms)) {
      break;
    }
#endif
  }

  if (total > 0) {
    (void)tuh_cdc_write_flush((uint8_t)s_cdc_idx);
  }

  return total;
}

uint32_t ftdi_host_read(void *buffer, uint32_t len) {
#ifdef FTDI_IO_SAFETY_CHECKS
  if (!ftdi_host_ready())
    return 0;
#endif
  return tuh_cdc_read((uint8_t)s_cdc_idx, buffer, len);
}

uint32_t ftdi_host_read_blocking(void *buffer, uint32_t len,
                                 uint32_t timeout_ms) {
#ifdef FTDI_IO_SAFETY_CHECKS
  if (!ftdi_host_ready() || (buffer == NULL) || (len == 0))
    return 0;
#endif
#ifdef FTDI_BLOCKING_IO_CHECK_TIMEOUTS
  const uint32_t start_ms = board_millis();
#endif

  uint8_t *ptr = (uint8_t *)buffer;
  uint32_t total = 0;

  while (total < len) {
    if (!ftdi_host_ready()) {
      break;
    }

    const uint32_t n =
        tuh_cdc_read((uint8_t)s_cdc_idx, ptr + total, len - total);
    if (n > 0) {
      total += n;
      continue;
    }

    tuh_task();
#ifdef FTDI_BLOCKING_IO_CHECK_TIMEOUTS
    if (timeout_expired(start_ms, timeout_ms)) {
      break;
    }
#endif
    // uart_printf("total=%lu, n=%lu\r\n", (unsigned long)total, (unsigned long)n);
  }

  return total;
}

uint32_t ftdi_host_read_available(void) {
  if (!ftdi_host_ready())
    return 0;
  return tuh_cdc_read_available((uint8_t)s_cdc_idx);
}

//--------------------------------------------------------------------+
// Public API – line configuration
//--------------------------------------------------------------------+

bool ftdi_host_set_baudrate(uint32_t baudrate) {
  if (!ftdi_host_ready())
    return false;
  return tuh_cdc_set_baudrate((uint8_t)s_cdc_idx, baudrate, NULL, 0);
}

bool ftdi_host_set_data_format(uint8_t stop_bits, uint8_t parity,
                               uint8_t data_bits) {
  if (!ftdi_host_ready())
    return false;
  return tuh_cdc_set_data_format((uint8_t)s_cdc_idx, stop_bits, parity,
                                 data_bits, NULL, 0);
}

bool ftdi_host_set_line_state(uint16_t line_state) {
  if (!ftdi_host_ready())
    return false;
  return tuh_cdc_set_control_line_state((uint8_t)s_cdc_idx, line_state, NULL,
                                        0);
}

//--------------------------------------------------------------------+
// TinyUSB CDC host callbacks
//--------------------------------------------------------------------+

/* Invoked when a CDC (or FTDI) interface is mounted.
 * cdch_set_config() -> ftdi_proccess_set_config() -> set_line_state_on_enum()
 * has already configured 115200 8N1 + DTR/RTS via the
 * CFG_TUH_CDC_LINE_CODING_ON_ENUM / CFG_TUH_CDC_LINE_CONTROL_ON_ENUM
 * macros using properly chained async callbacks.
 *
 * WARNING: Do NOT call blocking tuh_cdc_set_*() here!  This callback
 * is invoked from set_config_complete() inside the enumeration chain.
 * Blocking calls busy-wait by calling tuh_task(), causing re-entrancy. */
void tuh_cdc_mount_cb(uint8_t idx) {
  tuh_itf_info_t info;
  if (!tuh_cdc_itf_get_info(idx, &info))
    return;

  uint8_t itf_num = info.desc.bInterfaceNumber;

  tusb_uart_printf("[FTDI] itf %u mounted (idx=%u)\r\n", (unsigned)itf_num,
                   (unsigned)idx);

  if (itf_num == FTDI_TARGET_INTERFACE) {
    s_cdc_idx = (int8_t)idx;

    /* The enum macros already configured baud/format/DTR+RTS.
     * Just confirm what was negotiated. */
    cdc_line_coding_t coding;
    if (tuh_cdc_get_line_coding_local(idx, &coding)) {
      tusb_uart_printf("[FTDI] itf %u ready – %lu baud %u%c%s\r\n",
                       (unsigned)itf_num, (unsigned long)coding.bit_rate,
                       (unsigned)coding.data_bits,
                       "NOEMS"[coding.parity < 5 ? coding.parity : 0],
                       coding.stop_bits == 0   ? "1"
                       : coding.stop_bits == 1 ? "1.5"
                                               : "2");
    }
  }
}

/* Invoked when a CDC interface is unmounted. */
void tuh_cdc_umount_cb(uint8_t idx) {
  tuh_itf_info_t info;
  if (tuh_cdc_itf_get_info(idx, &info)) {
    tusb_uart_printf("[FTDI] itf %u unmounted (idx=%u)\r\n",
                     (unsigned)info.desc.bInterfaceNumber, (unsigned)idx);
  } else {
    tusb_uart_printf("[FTDI] idx %u unmounted\r\n", (unsigned)idx);
  }
  if ((int8_t)idx == s_cdc_idx) {
    s_cdc_idx = -1;
  }
}

/* Device-level mount/unmount – detect re-enumeration. */
void tuh_mount_cb(uint8_t daddr) {
  tusb_uart_printf("[USB] device addr %u mounted\r\n", (unsigned)daddr);
}

void tuh_umount_cb(uint8_t daddr) {
  tusb_uart_printf("[USB] device addr %u unmounted\r\n", (unsigned)daddr);
}

/* Invoked when new data is received on a CDC interface.
 * Read it and print the payload with hex dump for diagnostics. */
void tuh_cdc_rx_cb(uint8_t idx) {

  // if ((int8_t)idx != s_cdc_idx) return;

  // uint8_t buf[256];
  // uint32_t count = tuh_cdc_read(idx, buf, sizeof(buf));
  // if (count == 0) return;

  // /* Print header with byte count */
  // tusb_uart_printf("[RX %lu] ", (unsigned long)count);

  // /* Hex dump for diagnostic (limit to first 32 bytes) */
  // uint32_t hex_limit = (count < 32) ? count : 32;
  // for (uint32_t i = 0; i < hex_limit; i++) {
  //   tusb_uart_printf("%02X ", buf[i]);
  // }
  // if (count > 32) tusb_uart_printf("...");

  // /* ASCII representation */
  // tusb_uart_printf(" | ");
  // char out[257];
  // uint32_t out_len = 0;
  // for (uint32_t i = 0; i < count && out_len + 1 < sizeof(out); i++) {
  //   char c = (char)buf[i];
  //   if ((c >= 0x20 && c < 0x7f) || c == '\r' || c == '\n') {
  //     out[out_len++] = c;
  //   } else {
  //     out[out_len++] = '.';
  //   }
  // }
  // out[out_len] = '\0';
  // tusb_uart_printf("%s\n", out);
}

/* Invoked when a TX completes and buffer space is available. */
void tuh_cdc_tx_complete_cb(uint8_t idx) {
  (void)idx; /* nothing to do for now */
}
