/*
 * CPLD update mode for TinyUSB port.
 *
 * Main loop (main.c) calls cpld_update() when request.mode is
 * TRANSCEIVER_MODE_CPLD_UPDATE. Bulk OUT refills go through the TinyUSB
 * bridge (usb_transfer_schedule, usb_queue_flush_endpoint).
 */

#include "usb_api_cpld.h"
#include "usb_endpoint.h"

#include <hackrf_core.h>
#include <cpld_jtag.h>
#include <cpld_xc2c.h>

#include <stdbool.h>
#include <stddef.h>

extern uint8_t cpld_xsvf_buffer[512];

static volatile bool cpld_wait = false;

static void cpld_buffer_refilled(void* user_data, unsigned int length)
{
	(void)user_data;
	(void)length;
	cpld_wait = false;
}

static void refill_cpld_buffer(void)
{
	cpld_wait = true;
	usb_transfer_schedule(
		&usb_endpoint_bulk_out,
		cpld_xsvf_buffer,
		sizeof(cpld_xsvf_buffer),
		cpld_buffer_refilled,
		NULL);

	while (cpld_wait) {}
}

void cpld_update(void)
{
	int error;

	usb_queue_flush_endpoint(&usb_endpoint_bulk_in);
	usb_queue_flush_endpoint(&usb_endpoint_bulk_out);

	refill_cpld_buffer();

	error = cpld_jtag_program(
		&jtag_cpld,
		sizeof(cpld_xsvf_buffer),
		cpld_xsvf_buffer,
		refill_cpld_buffer);
	if (error == 0) {
		halt_and_flash(6000000);
	} else {
		led_on(LED3);
		while (1) {}
	}
}
