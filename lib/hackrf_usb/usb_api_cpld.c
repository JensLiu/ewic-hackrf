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

#include "usb_api_cpld.h"

#include <hackrf_core.h>
#include <cpld_jtag.h>
#include <cpld_xc2c.h>
#include <usb_queue.h>

#include "usb_endpoint.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

uint8_t cpld_xsvf_buffer[512];

/* cpld_update() lives in tinyusb_port/cpld_mode_tinyusb.c */

usb_request_status_t usb_vendor_request_cpld_checksum(
	usb_endpoint_t* const endpoint,
	const usb_transfer_stage_t stage)
{
	static uint32_t cpld_crc;
	uint8_t length;

	if (stage == USB_TRANSFER_STAGE_SETUP) {
		cpld_jtag_take(&jtag_cpld);
		const bool checksum_success = cpld_xc2c64a_jtag_checksum(
			&jtag_cpld,
			&cpld_hackrf_verify,
			&cpld_crc);
		cpld_jtag_release(&jtag_cpld);

		if (!checksum_success) {
			return USB_REQUEST_STATUS_STALL;
		}

		length = (uint8_t) sizeof(cpld_crc);
		memcpy(endpoint->buffer, &cpld_crc, length);
		usb_transfer_schedule_block(
			endpoint->in,
			endpoint->buffer,
			length,
			NULL,
			NULL);
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}
