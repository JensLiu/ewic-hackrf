/*
 * Sweep mode loop for TinyUSB port.
 *
 * Main loop (main.c) calls sweep_mode(request.seq) when request.mode is
 * TRANSCEIVER_MODE_RX_SWEEP. Sweep state (sweep_freq, frequencies, etc.) is
 * set by usb_vendor_request_init_sweep in lib/hackrf_usb/usb_api_sweep.c and
 * read here. Bulk transfers go through the TinyUSB bridge (usb_transfer_schedule_block).
 */

#include "usb_api_sweep.h"
#include "usb_api_sweep_internal.h"
#include "usb_api_transceiver.h"
#include "usb_endpoint.h"
#include "usb_bulk_buffer.h"
#include "usb_api_m0_state.h"
#include "streaming.h"
#include "tuning.h"

#include <libopencm3/lpc43xx/m4/nvic.h>

#include <stdbool.h>
#include <stddef.h>

void sweep_bulk_transfer_complete(void* user_data, unsigned int bytes_transferred)
{
	(void)user_data;
	(void)bytes_transferred;

	m0_state.m4_count += (THROWAWAY_BUFFERS + 1) * 0x4000;
}

void sweep_mode(uint32_t seq)
{
	unsigned int blocks_queued = 0;
	unsigned int phase = 0;
	bool odd = true;
	uint16_t range = 0;

	uint8_t* buffer;

	transceiver_startup(TRANSCEIVER_MODE_RX_SWEEP);

	m0_state.threshold = 0x4000;
	m0_state.next_mode = M0_MODE_WAIT;

	baseband_streaming_enable(&sgpio_config);

	while (transceiver_request.seq == seq) {
		while (m0_state.active_mode != M0_MODE_WAIT) {
			if (transceiver_request.seq != seq) {
				goto end;
			}
		}

		m0_state.threshold += (0x4000 * THROWAWAY_BUFFERS);
		m0_state.next_mode = M0_MODE_RX;

		buffer = &usb_bulk_buffer[phase * 0x4000];
		*buffer = 0x7f;
		*(buffer + 1) = 0x7f;
		*(buffer + 2) = sweep_freq & 0xff;
		*(buffer + 3) = (sweep_freq >> 8) & 0xff;
		*(buffer + 4) = (sweep_freq >> 16) & 0xff;
		*(buffer + 5) = (sweep_freq >> 24) & 0xff;
		*(buffer + 6) = (sweep_freq >> 32) & 0xff;
		*(buffer + 7) = (sweep_freq >> 40) & 0xff;
		*(buffer + 8) = (sweep_freq >> 48) & 0xff;
		*(buffer + 9) = (sweep_freq >> 56) & 0xff;

		usb_transfer_schedule_block(
			&usb_endpoint_bulk_in,
			buffer,
			0x4000,
			sweep_bulk_transfer_complete,
			NULL);

		phase = (phase + 1) % THROWAWAY_BUFFERS;

		if (++blocks_queued == dwell_blocks) {
			if (INTERLEAVED == style) {
				if (!odd &&
				    ((sweep_freq + step_width) >=
				     ((uint64_t)frequencies[1 + range * 2] * FREQ_GRANULARITY))) {
					range = (range + 1) % num_ranges;
					sweep_freq = (uint64_t)frequencies[range * 2] * FREQ_GRANULARITY;
				} else {
					if (odd) {
						sweep_freq += step_width / 4;
					} else {
						sweep_freq += 3 * step_width / 4;
					}
				}
				odd = !odd;
			} else {
				if ((sweep_freq + step_width) >=
				    ((uint64_t)frequencies[1 + range * 2] * FREQ_GRANULARITY)) {
					range = (range + 1) % num_ranges;
					sweep_freq = (uint64_t)frequencies[range * 2] * FREQ_GRANULARITY;
				} else {
					sweep_freq += step_width;
				}
			}
			nvic_disable_irq(NVIC_USB0_IRQ);
			radio_set_frequency(
				&radio,
				RADIO_CHANNEL0,
				RADIO_FREQUENCY_RF,
				(radio_frequency_t){.hz = sweep_freq + offset});
			nvic_enable_irq(NVIC_USB0_IRQ);
			blocks_queued = 0;
		}

		while (m0_state.active_mode != M0_MODE_RX) {
			if (transceiver_request.seq != seq) {
				goto end;
			}
		}

		m0_state.threshold += 0x4000;
		m0_state.next_mode = M0_MODE_WAIT;
	}
end:
	transceiver_shutdown();
}
