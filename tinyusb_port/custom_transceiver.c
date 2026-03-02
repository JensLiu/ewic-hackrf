#include "custom_transceiver.h"

#include "hackrf_core.h"
#include "hackrf_ui.h"
#include "m0_state.h"
#include "operacake_sctimer.h"
#include "radio.h"
#include "streaming.h"
#include "tinyusb_port.h"
#include <libopencm3/lpc43xx/m4/nvic.h>

#include <math.h>

static uint32_t tx_underrun_limit = 0;
static uint32_t rx_overrun_limit = 0;

static volatile hw_sync_mode_t _hw_sync_mode = HW_SYNC_MODE_OFF;
static volatile uint32_t _tx_underrun_limit;
static volatile uint32_t _rx_overrun_limit;

// radio parameters
#define USB_TRANSFER_SIZE 256
#define SAMPLE_RATE       10000000       // 10 MHz complex sample rate (matching GRC samp_rate)
#define SINE_FREQ         100000         // 100 kHz carrier (matching GRC carrier_freq)
#define FREQ              1000000000     // 1 GHz center frequency (matching GRC center_freq)
#define SAMPLES_PER_BIT   1000           // matching GRC blocks_repeat interp=1000
#define BIT_PACKET_SIZE     4              // Number of bits to buffer before sending to host
// sine lookup
#define SINE_TABLE_SIZE 256
#define SINE_AMPLITUDE 80
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
uint8_t sine_table[SINE_TABLE_SIZE * 2]; // IQ samples

void transceiver_shutdown(void) {
  baseband_streaming_disable(&sgpio_config);
  operacake_sctimer_reset_state();

  led_off(LED2);
  led_off(LED3);
  rf_path_set_direction(&rf_path, RF_PATH_DIRECTION_OFF);
  m0_set_mode(M0_MODE_IDLE);
}

void transceiver_startup(const transceiver_mode_t mode) {
  hackrf_ui()->set_transceiver_mode(mode);

  switch (mode) {
  case TRANSCEIVER_MODE_RX_SWEEP:
  case TRANSCEIVER_MODE_RX:
    led_off(LED3);
    led_on(LED2);
    rf_path_set_direction(&rf_path, RF_PATH_DIRECTION_RX);
    m0_set_mode(M0_MODE_RX);
    m0_state.shortfall_limit = _rx_overrun_limit;
    break;
  case TRANSCEIVER_MODE_TX:
    led_off(LED2);
    led_on(LED3);
    rf_path_set_direction(&rf_path, RF_PATH_DIRECTION_TX);
    m0_set_mode(M0_MODE_TX_START);
    m0_state.shortfall_limit = _tx_underrun_limit;
    break;
  default:
    break;
  }

  activate_best_clock_source();
  hw_sync_enable(_hw_sync_mode);
}

void sine_lookup_init() {
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    // Calculate phase angle for this sample
    float angle = 2.0f * M_PI * i / SINE_TABLE_SIZE;

    // Generate I/Q samples with proper amplitude scaling
    // Scale to -SINE_AMPLITUDE to +SINE_AMPLITUDE range
    int8_t i_sample = (int8_t)(SINE_AMPLITUDE * sinf(angle));
    int8_t q_sample = (int8_t)(SINE_AMPLITUDE * cosf(angle));

    // Store as signed values (bit-preserving cast, NO +128 DC offset)
    // HackRF baseband uses signed 8-bit IQ samples
    sine_table[i * 2] = (uint8_t)i_sample;     // I sample
    sine_table[i * 2 + 1] = (uint8_t)q_sample; // Q sample
  }
}

// void bit_to_iq(uint8_t *iq_data, const uint32_t iq_len,
//                const uint8_t *bit_pattern, const size_t pattern_len) {
//   const uint32_t BYTES_PER_SAMPLE = 2; // I + Q
//   const uint8_t MID_SCALE = 0;         // unsigned zero level

//   // phase increment through the sine table
//   const uint32_t phase_increment = (SINE_FREQ * SINE_TABLE_SIZE) / SAMPLE_RATE;

//   // persistent state
//   static uint32_t table_phase = 0;   // index into sine_table
//   static uint32_t sample_in_bit = 0; // 0…BIT_SAMPLES−1
//   static uint32_t bit_index = 0;     // 0…dynamic_pattern_len−1

//   const uint32_t total_samples = iq_len / BYTES_PER_SAMPLE;

//   for (uint32_t s = 0; s < total_samples; s++) {
//     const uint32_t i = s * BYTES_PER_SAMPLE;
//     if (bit_pattern[bit_index]) {
//       // — "1": sine output
//       const uint32_t ti = table_phase % SINE_TABLE_SIZE;
//       iq_data[i] = sine_table[2 * ti];
//       iq_data[i + 1] = sine_table[2 * ti + 1];
//       table_phase = (table_phase + phase_increment) % SINE_TABLE_SIZE;
//     } else {
//       // — "0": flat mid-scale
//       iq_data[i] = MID_SCALE;
//       iq_data[i + 1] = MID_SCALE;
//     }

//     // advance sample count; after BIT_SAMPLES, move to next bit
//     if (++sample_in_bit >= BIT_SAMPLES) {
//       sample_in_bit = 0;
//       bit_index = (bit_index + 1) % pattern_len;
//     }
//   }
// }

void fill_data_buffer(uint8_t* buffer, uint32_t len, uint32_t* _unused_phase)
{
	const uint32_t BYTES_PER_SAMPLE = 2; // I + Q

	// Fixed-point phase accumulator (16.16 format) for accurate carrier frequency.
	// phase_inc = (SINE_FREQ / SAMPLE_RATE) * SINE_TABLE_SIZE * 65536
	//           = (100000 / 10000000) * 256 * 65536 = 167772
	static const uint32_t PHASE_INC_FP = 167772;

	// Persistent state across calls
	static uint32_t table_phase_fp = 0; // 16.16 fixed-point phase
	static uint32_t sample_in_bit = 0;  // 0..SAMPLES_PER_BIT-1
	static uint32_t bit_index = 0;      // 0..DATA_PATTERN_LEN-1

	// Data pattern matching GRC: blocks_vector_source [1, 0, 1]
	static const uint8_t data_pattern[] = {1,1,1,1, 0, 0, 0, 0};
	#define DATA_PATTERN_LEN 8

	uint32_t total_samples = len / BYTES_PER_SAMPLE;

  for (uint32_t s = 0; s < total_samples; s++) {
    uint32_t i = s * BYTES_PER_SAMPLE;
    if (data_pattern[bit_index]) {
      // Bit 1: output carrier sinusoid (ASK on)
      uint32_t ti = (table_phase_fp >> 16) & (SINE_TABLE_SIZE - 1);
      buffer[i]     = sine_table[2 * ti];
      buffer[i + 1] = sine_table[2 * ti + 1];
      table_phase_fp += PHASE_INC_FP;
    } else {
      // Bit 0: silence (ASK off)
      buffer[i]     = 0;
      buffer[i + 1] = 0;
      // table_phase_fp is NOT reset to preserve phase continuity
    }

    // Advance sample count; after SAMPLES_PER_BIT, move to next bit
    if (++sample_in_bit >= SAMPLES_PER_BIT) {
      sample_in_bit = 0;
      bit_index = (bit_index + 1) % DATA_PATTERN_LEN;
    }
  }
}

// receiver
uint32_t receive_sample_count = 0;
uint32_t receive_high_samples = 0;
uint32_t receive_usb_count = 0;
// Constants matching the transmitter
bool receive_prev_bit = false;
uint8_t bit_buffer[USB_TRANSFER_SIZE]; // Buffer to store detected bits
uint32_t receive_bit_buffer_index = 0;
uint64_t receive_mag2_sum =
    0; // IMPORTANT: 64-bit to avoid overflow when summing many samples
uint32_t receive_rep3_sum = 0;
uint32_t receive_rep3_count = 0;

void custom_transceiver_receive_init() {
  sample_rate_frac_set(SAMPLE_RATE, 1);
  baseband_filter_bandwidth_set(15000000);
  set_freq(FREQ);
  max283x_set_lna_gain(&max283x, 40);
  rf_path_set_lna(&rf_path, 1);
  rf_path_set_antenna(&rf_path, 1);
  tusb_uart_printf("Custom transceiver receive initialized\n");
}

void custom_transceiver_receive_begin() {
  transceiver_startup(TRANSCEIVER_MODE_RX);
  baseband_streaming_enable(&sgpio_config);
  led_on(LED2);
  tusb_uart_printf("Custom transceiver receive started\n");
  receive_usb_count = 0;
  receive_sample_count = 0;
  receive_mag2_sum = 0;
  receive_high_samples = 0;
  receive_bit_buffer_index = 0;
  receive_rep3_sum = 0;
  receive_rep3_count = 0;
  // memset(bit_buffer, 0, sizeof(bit_buffer));
  // memset(tx_buffer, 0, sizeof(tx_buffer));
}

void custom_transceiver_receive_end() {
  transceiver_shutdown();
  tusb_uart_printf("Custom transceiver receive stopped\n");
  receive_usb_count = 0;
  receive_sample_count = 0;
  receive_mag2_sum = 0;
  receive_high_samples = 0;
  receive_bit_buffer_index = 0;
  receive_rep3_sum = 0;
  receive_rep3_count = 0;
}

void custom_transceiver_receive() {

//   static uint32_t last_print_ms = 0;
//   const uint32_t MAG2_THRESHOLD = 2500; // Adjust as needed
//   if ((m0_state.m0_count - receive_usb_count) >= USB_TRANSFER_SIZE) {
//     const uint8_t *buffer =
//         &usb_bulk_buffer[receive_usb_count & USB_BULK_BUFFER_MASK];

//     for (uint32_t i = 0; i < USB_TRANSFER_SIZE; i += 2) {
//       const int32_t I = (int8_t)buffer[i];
//       const int32_t Q = (int8_t)buffer[i + 1];

//       const uint32_t mag2 = (uint32_t)(I * I + Q * Q);
//       receive_mag2_sum += mag2;
//       receive_sample_count++;

//       if (receive_sample_count >= BIT_SAMPLES) {
//         const uint32_t mag2_avg = (uint32_t)(receive_mag2_sum / BIT_SAMPLES);
//         const bool current_bit = (mag2_avg > MAG2_THRESHOLD);

//         // ---- REP3 ECC majority vote ----
//         receive_rep3_sum += current_bit ? 1u : 0u;
//         receive_rep3_count += 1u;

//         if (receive_rep3_count == 3u) {
//           const uint8_t corrected_bit = (receive_rep3_sum >= 2u) ? 1u : 0u;

//           // Emit ONE corrected bit for each trio
//           bit_buffer[receive_bit_buffer_index++] = corrected_bit;

//           // Packetization unchanged
//           if (receive_bit_buffer_index >= BIT_PACKET_SIZE) {
//             // tusb_uart_printf("bit_buffer_index = %d\n",
//             // receive_bit_buffer_index); Send through USB
//             const uint32_t delta_ms = board_millis() - last_print_ms;
//             tusb_uart_printf("Decode: dt=%lu ms\n", (unsigned long)delta_ms);
//             ftdi_host_write(bit_buffer, BIT_PACKET_SIZE);
//             tuh_task();
//             last_print_ms = board_millis();
//             receive_bit_buffer_index = 0;
//           }

//           // reset trio
//           receive_rep3_sum = 0;
//           receive_rep3_count = 0;
//         }
//         // ---------------------------------

//         // Reset window accumulators
//         receive_sample_count = 0;
//         receive_mag2_sum = 0;
//       }
//     }

//     receive_usb_count += USB_TRANSFER_SIZE;
//     m0_state.m4_count += USB_TRANSFER_SIZE;
//     m0_state.m0_count += USB_TRANSFER_SIZE;
//   } else {
//     // tusb_uart_printf(
//     //     "Waiting for more samples... (m0_count=%lu,
//     //     receive_usb_count=%lu)\n", m0_state.m0_count, receive_usb_count);
//   }
}

// sender
uint32_t send_usb_count = 0;

void custom_transceiver_send_init() {
  sine_lookup_init();
  sample_rate_frac_set(SAMPLE_RATE, 1);    // 20 MS/s
  baseband_filter_bandwidth_set(15000000); // 15 MHz bandwidth
  set_freq(FREQ);                          // Frequency 915 MHz
  max283x_set_txvga_gain(&max283x, 47);    // Maximum TX gain
  rf_path_set_lna(&rf_path, 1);            // Enable LNA
  rf_path_set_antenna(&rf_path, 1);        // Select antenna path
  tusb_uart_printf("Custom transceiver send initialized\n");
}

void custom_transceiver_send_begin() {
  transceiver_startup(TRANSCEIVER_MODE_TX);
  baseband_streaming_enable(&sgpio_config);
  tusb_uart_printf("Custom transceiver send started\n");
}

void custom_transceiver_send_end() {
  transceiver_shutdown();
  tusb_uart_printf("Custom transceiver send stopped\n");
}

void custom_transceiver_send(const uint8_t *bit_pattern,
                             const size_t pattern_len) {
  // Wait until there's space to safely write
  if ((send_usb_count - m0_state.m0_count) <=
      USB_BULK_BUFFER_SIZE - USB_TRANSFER_SIZE) {
    // bit_to_iq(&usb_bulk_buffer[send_usb_count & USB_BULK_BUFFER_MASK],
    //           USB_TRANSFER_SIZE, bit_pattern, pattern_len);

    // Atomically update shared counters
    nvic_disable_irq(NVIC_M0CORE_IRQ);
    m0_state.m4_count += USB_TRANSFER_SIZE;
    m0_state.m0_count += USB_TRANSFER_SIZE;
    send_usb_count += USB_TRANSFER_SIZE;
    nvic_enable_irq(NVIC_M0CORE_IRQ);
  }

  // Optional small delay
  __asm__("nop");
}


// test
#define BIT_PACKET_SIZE 4

void rx_mode()
{
    // Match GRC: samp_rate=10e6, center_freq=1e9
    // sample_rate_frac_set expects SGPIO clock rate = 2 * complex_sample_rate
    sample_rate_frac_set(SAMPLE_RATE * 2, 1);
    baseband_filter_bandwidth_set(1750000); // Minimum BW (GRC bw0=100kHz clips to 1.75MHz)
    set_freq(FREQ);

    // Match GRC RX gains: gain0=20 (RF amp on), if_gain0=30->32, bb_gain0=30
    max283x_set_lna_gain(&max283x, 32);  // LNA gain (closest to GRC if_gain0=30)
    max283x_set_vga_gain(&max283x, 30);  // VGA gain (matching GRC bb_gain0=30)
    rf_path_set_lna(&rf_path, 1);        // RF amp on (matching GRC gain0=20)
    rf_path_set_antenna(&rf_path, 1);

    uint32_t usb_count = 0;
    transceiver_startup(TRANSCEIVER_MODE_RX);
    baseband_streaming_enable(&sgpio_config);

    // No RRC filter - matching GRC which uses only complex_to_mag -> threshold -> slicer

    uint32_t sample_count = 0;
    uint64_t mag2_sum = 0;
    bool current_bit = false;

    uint8_t bit_buffer[USB_TRANSFER_SIZE];
    uint32_t bit_buffer_index = 0;
    uint8_t tx_buffer[USB_TRANSFER_SIZE];

    // Thresholds adjusted to match transmitter SINE_AMPLITUDE (80)
    // Max mag2 per sample = 80^2 + 80^2 = 12800
    const uint32_t MAG2_PEAK = 2 * SINE_AMPLITUDE * SINE_AMPLITUDE; // 12800
    // Set thresholds at 0.5 and 0.25 of peak
    const uint64_t HIGH_THRESH_SUM = (uint64_t)(MAG2_PEAK * 0.5) * SAMPLES_PER_BIT; // 6400 * SAMPLES_PER_BIT
    const uint64_t LOW_THRESH_SUM  = (uint64_t)(MAG2_PEAK * 0.25) * SAMPLES_PER_BIT; // 3200 * SAMPLES_PER_BIT

    while (1) {
        if ((m0_state.m0_count - usb_count) >= USB_TRANSFER_SIZE) {
            uint8_t* buffer = &usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK];

            for (uint32_t i = 0; i < USB_TRANSFER_SIZE; i += 2) {
                // Read raw signed IQ samples
                int32_t I32 = (int32_t)((int8_t)buffer[i]);
                int32_t Q32 = (int32_t)((int8_t)buffer[i + 1]);

                // Magnitude squared (matching GRC complex_to_mag, squared)
                uint32_t mag2 = (uint32_t)(I32 * I32 + Q32 * Q32);

                // Integrate over one symbol period
                mag2_sum += mag2;
                sample_count++;

                if (sample_count >= SAMPLES_PER_BIT) {
                    // Hysteresis threshold (matching GRC blocks_threshold_ff)
                    if (mag2_sum > HIGH_THRESH_SUM) {
                        current_bit = true;
                    } else if (mag2_sum < LOW_THRESH_SUM) {
                        current_bit = false;
                    }
                    // else: hold previous value (hysteresis)

                    if (current_bit) {
                        led_on(LED3);
                    } else {
                        led_off(LED3);
                    }

                    bit_buffer[bit_buffer_index++] = current_bit ? 1 : 0;

                    if (bit_buffer_index >= BIT_PACKET_SIZE) {
                        tusb_uart_printf("Decoded bits: ");
                        for (uint32_t j = 0; j < BIT_PACKET_SIZE; j++) {
                            tusb_uart_printf("%d ", bit_buffer[j]);
                        }
                        tusb_uart_printf("\n");
                        bit_buffer_index = 0;
                    }

                    sample_count = 0;
                    mag2_sum = 0;
                }
            }

            usb_count += USB_TRANSFER_SIZE;
            m0_state.m4_count += USB_TRANSFER_SIZE;
            m0_state.m0_count += USB_TRANSFER_SIZE;
        }
    }

    transceiver_shutdown();
}

void tx_mode(){
	sine_lookup_init();
	// Match GRC: samp_rate=10e6, center_freq=1e9
	// sample_rate_frac_set expects SGPIO clock rate = 2 * complex_sample_rate
	sample_rate_frac_set(SAMPLE_RATE * 2, 1);
	baseband_filter_bandwidth_set(1750000);    // Minimum BW (GRC bw0=10kHz clips to 1.75MHz)
	set_freq(FREQ);                            // 1 GHz center (matching GRC)
	max283x_set_txvga_gain(&max283x, 20);      // Match GRC if_gain0=20
	rf_path_set_lna(&rf_path, 1);              // RF amp on (matching GRC gain0=20)
	rf_path_set_antenna(&rf_path, 1);

	// Start transceiver once
	transceiver_startup(TRANSCEIVER_MODE_TX);
	baseband_streaming_enable(&sgpio_config);

	// Phase tracking for waveform continuity
	static uint32_t phase = 0;

	// USB bulk transfer count initialization
	uint32_t usb_count = 0;

	// Continuously fill the USB bulk buffer with IQ data
	while (1)
	{
		// Wait until there's space to safely write
		if ((usb_count - m0_state.m0_count) <= USB_BULK_BUFFER_SIZE - USB_TRANSFER_SIZE) {
			// Fill the buffer with your desired waveform
			fill_data_buffer(&usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK],
							 USB_TRANSFER_SIZE, &phase);

			// Atomically update shared counters
			nvic_disable_irq(NVIC_M0CORE_IRQ);
			m0_state.m4_count += USB_TRANSFER_SIZE;
			m0_state.m0_count += USB_TRANSFER_SIZE;
			usb_count += USB_TRANSFER_SIZE;
			nvic_enable_irq(NVIC_M0CORE_IRQ);
		}

		// Optional small delay
		__asm__("nop");
	}

	// Never reaches here, but cleanup code can be included 
	transceiver_shutdown();
}
