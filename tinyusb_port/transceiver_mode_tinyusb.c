/*
 * Transceiver mode loops (off_mode, rx_mode, tx_mode) for TinyUSB port.
 *
 * Main loop (main.c) disables USB IRQ, reads transceiver_request, re-enables IRQ,
 * then switches on request.mode and calls these handlers. This file provides
 * the mode implementations using the TinyUSB bridge (usb_transfer_schedule_block,
 * usb_endpoint_bulk_in/out, usb_queue_*) so bulk streaming goes through TinyUSB.
 *
 * Shared helpers (request_transceiver_mode, transceiver_startup, transceiver_shutdown)
 * remain in lib/hackrf_usb/usb_api_transceiver.c.
 */

#include "usb_api_transceiver.h"
#include "usb_endpoint.h"
#include "usb_bulk_buffer.h"
#include "usb_api_m0_state.h"
#include "streaming.h"
#include "hackrf_ui.h"
#include "operacake_sctimer.h"

#include "uart.h"

#include <stddef.h>
#include <string.h>

#define USB_TRANSFER_SIZE 0x4000

static void transceiver_bulk_transfer_complete(void* user_data, unsigned int bytes_transferred)
{
	(void)user_data;
	m0_state.m4_count += bytes_transferred;
	uart_print("xfer_complete: bytes=%u m4_count=%lu\n", bytes_transferred, (unsigned long)m0_state.m4_count);
}

void off_mode(uint32_t seq)
{
	hackrf_ui()->set_transceiver_mode(TRANSCEIVER_MODE_OFF);

	while (transceiver_request.seq == seq) {}
}

void rx_mode(uint32_t seq)
{
	uint32_t usb_count = 0;

	transceiver_startup(TRANSCEIVER_MODE_RX);

	baseband_streaming_enable(&sgpio_config);

	uart_print("rx_mode: start m0=%lu m4=%lu usb=%lu\n",
	           (unsigned long)m0_state.m0_count, (unsigned long)m0_state.m4_count, (unsigned long)usb_count);

	while (transceiver_request.seq == seq) {
		if ((m0_state.m0_count - usb_count) >= USB_TRANSFER_SIZE) {
			uart_print("rx_mode: sched m0=%lu usb=%lu\n", (unsigned long)m0_state.m0_count, (unsigned long)usb_count);
			usb_transfer_schedule_block(
				&usb_endpoint_bulk_in,
				&usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK],
				USB_TRANSFER_SIZE,
				transceiver_bulk_transfer_complete,
				NULL);
			usb_count += USB_TRANSFER_SIZE;
		}
	}

	transceiver_shutdown();
}

void tx_mode(uint32_t seq)
{
	unsigned int usb_count = 0;
	bool started = false;

	transceiver_startup(TRANSCEIVER_MODE_TX);

	usb_transfer_schedule_block(
		&usb_endpoint_bulk_out,
		&usb_bulk_buffer[0x0000],
		USB_TRANSFER_SIZE,
		transceiver_bulk_transfer_complete,
		NULL);
	usb_count += USB_TRANSFER_SIZE;

	while (transceiver_request.seq == seq) {
		if (!started && (m0_state.m4_count == USB_BULK_BUFFER_SIZE)) {
			baseband_streaming_enable(&sgpio_config);
			started = true;
		}
		if ((usb_count - m0_state.m0_count) <= USB_TRANSFER_SIZE) {
			usb_transfer_schedule_block(
				&usb_endpoint_bulk_out,
				&usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK],
				USB_TRANSFER_SIZE,
				transceiver_bulk_transfer_complete,
				NULL);
			usb_count += USB_TRANSFER_SIZE;
		}
	}

	transceiver_shutdown();
}
