/*
 * FT4232HQ USB host support – public API.
 *
 * Uses TinyUSB's built-in CDC host class with FTDI sub-driver.
 * Line coding, data format, and modem control are configured
 * explicitly via tuh_cdc_set_baudrate / tuh_cdc_set_data_format /
 * tuh_cdc_set_control_line_state after mount.
 */

#ifndef FTDI_HOST_H_
#define FTDI_HOST_H_

#include <stdbool.h>
#include <stdint.h>

/* Returns true when the target FTDI interface is mounted and ready. */
bool ftdi_host_ready(void);

/* Buffered write. Returns number of bytes actually queued.
 * Flushes automatically. */
uint32_t ftdi_host_write(const void *buffer, uint32_t len);

/* Buffered read. Returns number of bytes actually read. */
uint32_t ftdi_host_read(void *buffer, uint32_t len);

/* Returns number of bytes available to read from the RX FIFO. */
uint32_t ftdi_host_read_available(void);

/* Set baud rate on the target FTDI interface.
 * Returns true on success.  If complete_cb is NULL the call blocks. */
bool ftdi_host_set_baudrate(uint32_t baudrate);

/* Set data format (stop_bits, parity, data_bits) on the target interface.
 * Uses CDC_LINE_CODING_STOP_BITS_*, CDC_LINE_CODING_PARITY_* constants.
 * Returns true on success.  If complete_cb is NULL the call blocks. */
bool ftdi_host_set_data_format(uint8_t stop_bits, uint8_t parity,
                               uint8_t data_bits);

/* Set modem control lines (DTR/RTS).
 * line_state: CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS */
bool ftdi_host_set_line_state(uint16_t line_state);

#endif /* FTDI_HOST_H_ */

