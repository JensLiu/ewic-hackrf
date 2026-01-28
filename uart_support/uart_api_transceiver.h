/*
 * Copyright 2012-2022 Great Scott Gadgets <info@greatscottgadgets.com>
 * Copyright 2012 Jared Boone
 * Copyright 2013 Benjamin Vernoux
 *
 * This file is part of HackRF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __UART_API_TRANSCEIVER_H__
#define __UART_API_TRANSCEIVER_H__

#include <hackrf_core.h>
#include <usb_type.h>
#include <usb_request.h>

typedef struct {
	transceiver_mode_t mode;
	uint32_t seq;
} transceiver_request_t;

extern volatile transceiver_request_t transceiver_request;

void set_hw_sync_mode(const hw_sync_mode_t new_hw_sync_mode);
void request_transceiver_mode(transceiver_mode_t mode);
void transceiver_startup(transceiver_mode_t mode);
void transceiver_shutdown(void);
void start_streaming_on_hw_sync();
void rx_mode(uint32_t seq);
void tx_mode(uint32_t seq);
void off_mode(uint32_t seq);

#endif /*__UART_API_TRANSCEIVER_H__*/
