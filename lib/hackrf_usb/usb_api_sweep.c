/*
 * Copyright 2016-2022 Great Scott Gadgets <info@greatscottgadgets.com>
 * Copyright 2016 Mike Walters, Dominic Spill
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

#include "usb_api_sweep.h"
#include "usb_api_sweep_internal.h"
#include "usb_queue.h"
#include <stddef.h>
#include <hackrf_core.h>
#include "usb_api_transceiver.h"
#include "usb_bulk_buffer.h"
#include "usb_api_m0_state.h"
#include "tuning.h"
#include "usb_endpoint.h"
#include "streaming.h"

#define MIN(x, y)        ((x) < (y) ? (x) : (y))
#define MAX(x, y)        ((x) > (y) ? (x) : (y))

/* Sweep state: set by usb_vendor_request_init_sweep, read by sweep_mode (tinyusb_port). */
uint64_t sweep_freq;
uint16_t frequencies[MAX_RANGES * 2];
static unsigned char data[9 + MAX_RANGES * 2 * sizeof(frequencies[0])];
uint16_t num_ranges = 0;
uint32_t dwell_blocks = 0;
uint32_t step_width = 0;
uint32_t offset = 0;
enum sweep_style style = LINEAR;

/* Do this before starting sweep mode with request_transceiver_mode(). */
usb_request_status_t usb_vendor_request_init_sweep(
	usb_endpoint_t* const endpoint,
	const usb_transfer_stage_t stage)
{
	uint32_t num_bytes;
	int i;
	if (stage == USB_TRANSFER_STAGE_SETUP) {
		num_bytes = (endpoint->setup.index << 16) | endpoint->setup.value;
		dwell_blocks = num_bytes / 0x4000;
		if (1 > dwell_blocks) {
			return USB_REQUEST_STATUS_STALL;
		}
		num_ranges = (endpoint->setup.length - 9) / (2 * sizeof(frequencies[0]));
		if ((1 > num_ranges) || (MAX_RANGES < num_ranges)) {
			return USB_REQUEST_STATUS_STALL;
		}
		usb_transfer_schedule_block(
			endpoint->out,
			&data,
			endpoint->setup.length,
			NULL,
			NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		step_width = ((uint32_t) (endpoint->buffer[3]) << 24) | ((uint32_t) (endpoint->buffer[2]) << 16) |
			((uint32_t) (endpoint->buffer[1]) << 8) | endpoint->buffer[0];
		if (1 > step_width) {
			return USB_REQUEST_STATUS_STALL;
		}
		offset = ((uint32_t) (endpoint->buffer[7]) << 24) | ((uint32_t) (endpoint->buffer[6]) << 16) |
			((uint32_t) (endpoint->buffer[5]) << 8) | endpoint->buffer[4];
		style = endpoint->buffer[8];
		if (INTERLEAVED < style) {
			return USB_REQUEST_STATUS_STALL;
		}
		for (i = 0; i < (num_ranges * 2); i++) {
			frequencies[i] =
				((uint16_t) (endpoint->buffer[10 + i * 2]) << 8) + endpoint->buffer[9 + i * 2];
		}
		sweep_freq = (uint64_t) frequencies[0] * FREQ_GRANULARITY;
		radio_set_frequency(
			&radio,
			RADIO_CHANNEL0,
			RADIO_FREQUENCY_RF,
			(radio_frequency_t){.hz = sweep_freq + offset});
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}

/* sweep_mode() and sweep_bulk_transfer_complete() live in tinyusb_port/sweep_mode_tinyusb.c */
