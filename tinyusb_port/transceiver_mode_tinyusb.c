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

#include "tusb.h"
#include "tinyusb_port_debug.h"
#include "tinyusb_port.h"
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

/* Vendor stream FIFO is 512 bytes; schedule that much per transfer so m4_count stays in sync. */
#define USB_TRANSFER_SIZE 512

/* Block size for "logical" scheduling (rx schedules until we've sent this much per iteration). */
#define USB_BLOCK_SIZE 0x4000

static void transceiver_bulk_transfer_complete(void* user_data, unsigned int bytes_transferred)
{
	(void)user_data;
	static uint32_t complete_count;
	m0_state.m4_count += bytes_transferred;
	if (TUSB_STREAM_DEBUG && (++complete_count % 64) == 0) {
		TUSB_PORT_DBG_STREAM("complete #%lu bytes=%u m4=%lu",
			(unsigned long)complete_count, bytes_transferred, (unsigned long)m0_state.m4_count);
	}
}

void off_mode(uint32_t seq)
{
	hackrf_ui()->set_transceiver_mode(TRANSCEIVER_MODE_OFF);

	while (transceiver_request.seq == seq) {}
}

void rx_mode(uint32_t seq)
{
	transceiver_startup(TRANSCEIVER_MODE_RX);

	TUSB_PORT_DBG_STREAM("rx_mode enter m0=%lu m4=%lu avail=%lu",
		(unsigned long)m0_state.m0_count, (unsigned long)m0_state.m4_count,
		(unsigned long)(m0_state.m0_count - m0_state.m4_count));

	baseband_streaming_enable(&sgpio_config);

	uint32_t last_stream_log = 0;
	while (transceiver_request.seq == seq) {
		/* avail = bytes M0 has produced but M4 hasn't sent yet */
		uint32_t avail = m0_state.m0_count - m0_state.m4_count;
		if (avail >= USB_TRANSFER_SIZE) {
			int r = usb_transfer_schedule_block(
				&usb_endpoint_bulk_in,
				&usb_bulk_buffer[m0_state.m4_count & USB_BULK_BUFFER_MASK],
				USB_TRANSFER_SIZE,
				transceiver_bulk_transfer_complete,
				NULL);
			if (TUSB_STREAM_DEBUG && r != 0) {
				TUSB_PORT_DBG_STREAM("sched_block failed r=%d avail=%lu", r, (unsigned long)avail);
			}
		}
		/* Rate-limited status every 500ms */
		if (TUSB_STREAM_DEBUG) {
			uint32_t now = board_millis();
			if (now - last_stream_log >= 500) {
				last_stream_log = now;
				TUSB_PORT_DBG_STREAM("rx loop m0=%lu m4=%lu avail=%lu",
					(unsigned long)m0_state.m0_count, (unsigned long)m0_state.m4_count, (unsigned long)avail);
			}
		}
	}

	TUSB_PORT_DBG_STREAM("rx_mode exit");
	transceiver_shutdown();
}

void tx_mode(uint32_t seq)
{
	unsigned int usb_count = 0;
	bool started = false;

	transceiver_startup(TRANSCEIVER_MODE_TX);

	TUSB_PORT_DBG_STREAM("tx_mode enter m0=%lu m4=%lu",
		(unsigned long)m0_state.m0_count, (unsigned long)m0_state.m4_count);

	usb_transfer_schedule_block(
		&usb_endpoint_bulk_out,
		&usb_bulk_buffer[0x0000],
		USB_BLOCK_SIZE,
		transceiver_bulk_transfer_complete,
		NULL);
	usb_count += USB_BLOCK_SIZE;

	uint32_t last_tx_log = 0;
	while (transceiver_request.seq == seq) {
		if (!started && (m0_state.m4_count == USB_BULK_BUFFER_SIZE)) {
			baseband_streaming_enable(&sgpio_config);
			started = true;
		}
		if ((usb_count - m0_state.m0_count) <= USB_BLOCK_SIZE) {
			usb_transfer_schedule_block(
				&usb_endpoint_bulk_out,
				&usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK],
				USB_BLOCK_SIZE,
				transceiver_bulk_transfer_complete,
				NULL);
			usb_count += USB_BLOCK_SIZE;
		}
		if (TUSB_STREAM_DEBUG) {
			uint32_t now = board_millis();
			if (now - last_tx_log >= 500) {
				last_tx_log = now;
				TUSB_PORT_DBG_STREAM("tx loop usb_count=%lu m0=%lu m4=%lu started=%u",
					(unsigned long)usb_count, (unsigned long)m0_state.m0_count,
					(unsigned long)m0_state.m4_count, (unsigned)started);
			}
		}
	}

	TUSB_PORT_DBG_STREAM("tx_mode exit");
	transceiver_shutdown();
}
