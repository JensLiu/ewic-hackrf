/*
 * FT4232HQ USB host support - public API.
 *
 * These helpers provide a simple abstraction over TinyUSB host calls so
 * other firmware modules can push and pull byte streams to/from the FTDI
 * bulk endpoints. Higher-level IQ framing and buffering will be built on
 * top of this interface.
 */

#ifndef FTDI_HOST_H_
#define FTDI_HOST_H_

#include <stdbool.h>
#include <stdint.h>

#include "host/usbh.h"

bool ftdi_host_ready(void);

bool ftdi_host_write_async(void const *buffer, uint16_t len,
                           tuh_xfer_cb_t complete_cb,
                           uintptr_t user_data);

bool ftdi_host_read_async(void *buffer, uint16_t len,
                          tuh_xfer_cb_t complete_cb,
                          uintptr_t user_data);

#endif /* FTDI_HOST_H_ */

