#include "custom_transceiver.h"

#include "hackrf_core.h"
#include "hackrf_ui.h"
#include "m0_state.h"
#include "operacake_sctimer.h"
#include "radio.h"
#include "streaming.h"
#include "tinyusb_port.h"
#include <libopencm3/lpc43xx/m4/nvic.h>

static uint32_t tx_underrun_limit = 0;
static uint32_t rx_overrun_limit = 0;

static volatile hw_sync_mode_t _hw_sync_mode = HW_SYNC_MODE_OFF;
static volatile uint32_t _tx_underrun_limit;
static volatile uint32_t _rx_overrun_limit;

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

// Borrowed from Ezgi
#define USB_TRANSFER_SIZE 256
#define SAMPLE_RATE 7200000 // 10 Msps sample rate for better quality
#define SINE_FREQ 200000    // 200 kHz sine wave
#define FREQ 915000000      // 915 MHz
#define BIT_PACKET_SIZE 4

const uint32_t BIT_SAMPLES = SAMPLE_RATE * 0.1;

uint32_t receive_sample_count = 0;
uint32_t receive_high_samples = 0;
uint32_t receive_usb_count = 0;
// Constants matching the transmitter
bool receive_prev_bit = false;
uint8_t bit_buffer[USB_TRANSFER_SIZE]; // Buffer to store detected bits
uint32_t receive_bit_buffer_index = 0;
uint64_t receive_mag2_sum = 0;  // IMPORTANT: 64-bit to avoid overflow when summing many samples
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

  static uint32_t last_print_ms = 0;
  const uint32_t MAG2_THRESHOLD = 2500; // Adjust as needed
  if ((m0_state.m0_count - receive_usb_count) >= USB_TRANSFER_SIZE) {
    const uint8_t *buffer =
        &usb_bulk_buffer[receive_usb_count & USB_BULK_BUFFER_MASK];

    for (uint32_t i = 0; i < USB_TRANSFER_SIZE; i += 2) {
      const int32_t I = (int8_t)buffer[i];
      const int32_t Q = (int8_t)buffer[i + 1];

      const uint32_t mag2 = (uint32_t)(I * I + Q * Q);
      receive_mag2_sum += mag2;
      receive_sample_count++;

      if (receive_sample_count >= BIT_SAMPLES) {
        const uint32_t mag2_avg = (uint32_t)(receive_mag2_sum / BIT_SAMPLES);
        const bool current_bit = (mag2_avg > MAG2_THRESHOLD);

        // ---- REP3 ECC majority vote ----
        receive_rep3_sum += current_bit ? 1u : 0u;
        receive_rep3_count += 1u;

        if (receive_rep3_count == 3u) {
          const uint8_t corrected_bit = (receive_rep3_sum >= 2u) ? 1u : 0u;

          // Emit ONE corrected bit for each trio
          bit_buffer[receive_bit_buffer_index++] = corrected_bit;

          // Packetization unchanged
          if (receive_bit_buffer_index >= BIT_PACKET_SIZE) {
            // tusb_uart_printf("bit_buffer_index = %d\n",
            // receive_bit_buffer_index); Send through USB
            const uint32_t now_ms = board_millis();
            const uint32_t delta_ms = now_ms - last_print_ms;
            last_print_ms = now_ms;
            tusb_uart_printf("Decode: dt=%lu ms\n", (unsigned long)delta_ms);
            ftdi_host_write(bit_buffer, BIT_PACKET_SIZE);
            tuh_task();
            receive_bit_buffer_index = 0;
          }

          // reset trio
          receive_rep3_sum = 0;
          receive_rep3_count = 0;
        }
        // ---------------------------------

        // Reset window accumulators
        receive_sample_count = 0;
        receive_mag2_sum = 0;
      }
    }

    receive_usb_count += USB_TRANSFER_SIZE;
    m0_state.m4_count += USB_TRANSFER_SIZE;
    m0_state.m0_count += USB_TRANSFER_SIZE;
  } else {
    // tusb_uart_printf(
    //     "Waiting for more samples... (m0_count=%lu,
    //     receive_usb_count=%lu)\n", m0_state.m0_count, receive_usb_count);
  }
}

// void rx_mode() {
//   sample_rate_frac_set(SAMPLE_RATE, 1);
//   baseband_filter_bandwidth_set(15000000);
//   set_freq(FREQ);
//   max283x_set_lna_gain(&max283x, 40);
//   rf_path_set_lna(&rf_path, 1);
//   rf_path_set_antenna(&rf_path, 1);

//   uint32_t usb_count = 0;
//   transceiver_startup(TRANSCEIVER_MODE_RX);
//   baseband_streaming_enable(&sgpio_config);

//   // Constants matching the transmitter
//   uint32_t sample_count = 0;
//   uint64_t mag2_sum = 0; // Sum of magnitude squared
//   bool current_bit = false;

//   // Output buffer (one byte per bit: 0 or 1)
//   uint8_t bit_buffer[USB_TRANSFER_SIZE];
//   uint32_t bit_buffer_index = 0;
//   uint8_t tx_buffer[USB_TRANSFER_SIZE];

//   const uint32_t MAG2_THRESHOLD = 2500; // Adjust as needed

//   // === REP3 ECC state ===
//   uint32_t rep3_sum = 0;   // how many '1's seen in the current trio
//   uint32_t rep3_count = 0; // how many bits collected in current trio

//   while (1) {
//     if ((m0_state.m0_count - usb_count) >= USB_TRANSFER_SIZE) {
//       uint8_t *buffer = &usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK];

//       for (uint32_t i = 0; i < USB_TRANSFER_SIZE; i += 2) {
//         int32_t I = (int8_t)buffer[i];
//         int32_t Q = (int8_t)buffer[i + 1];

//         uint32_t mag2 = (uint32_t)(I * I + Q * Q);
//         mag2_sum += mag2;
//         sample_count++;

//         if (sample_count >= BIT_SAMPLES) {
//           uint32_t mag2_avg = (uint32_t)(mag2_sum / BIT_SAMPLES);
//           current_bit = (mag2_avg > MAG2_THRESHOLD);

//           // ---- REP3 ECC majority vote ----
//           rep3_sum += current_bit ? 1u : 0u;
//           rep3_count += 1u;

//           if (rep3_count == 3u) {
//             uint8_t corrected_bit = (rep3_sum >= 2u) ? 1u : 0u;

//             // LED shows corrected decision (steadier)
//             if (corrected_bit) {
//               led_on(LED3);
//             } else {
//               led_off(LED3);
//             }

//             // Emit ONE corrected bit for each trio
//             bit_buffer[bit_buffer_index++] = corrected_bit;

//             // Packetization unchanged
//             if (bit_buffer_index >= BIT_PACKET_SIZE) {
//               ftdi_host_write(bit_buffer, BIT_PACKET_SIZE);
//               tuh_task();
//               bit_buffer_index = 0;
//             }

//             // reset trio
//             rep3_sum = 0;
//             rep3_count = 0;
//           }
//           // ---------------------------------

//           // Reset window accumulators
//           sample_count = 0;
//           mag2_sum = 0;
//         }
//       }

//       usb_count += USB_TRANSFER_SIZE;
//       m0_state.m4_count += USB_TRANSFER_SIZE;
//       m0_state.m0_count += USB_TRANSFER_SIZE;
//     }
//   }

//   transceiver_shutdown();
// }

void custom_transceiver_send_begin() {
  transceiver_startup(TRANSCEIVER_MODE_TX);
  tusb_uart_printf("Custom transceiver send started\n");
}

void custom_transceiver_send_end() {
  transceiver_shutdown();
  tusb_uart_printf("Custom transceiver send stopped\n");
}

void init_sine_table(void);
void fill_data_buffer(uint8_t *buffer, uint32_t len, uint32_t *_unused_phase);
void custom_transceiver_send() {
  init_sine_table();
  sample_rate_frac_set(SAMPLE_RATE, 1);    // 20 MS/s
  baseband_filter_bandwidth_set(15000000); // 15 MHz bandwidth
  set_freq(FREQ);                          // Frequency 915 MHz
  max283x_set_txvga_gain(&max283x, 47);    // Maximum TX gain
  rf_path_set_lna(&rf_path, 1);            // Enable LNA
  rf_path_set_antenna(&rf_path, 1);        // Select antenna path

  // Start transceiver once
  transceiver_startup(TRANSCEIVER_MODE_TX);
  baseband_streaming_enable(&sgpio_config);

  // Phase tracking for waveform continuity
  static uint32_t phase = 0;

  // USB bulk transfer count initialization
  uint32_t usb_count = 0;

  // Continuously fill the USB bulk buffer with IQ data
  while (1) {
    // Wait until there's space to safely write
    if ((usb_count - m0_state.m0_count) <=
        USB_BULK_BUFFER_SIZE - USB_TRANSFER_SIZE) {
      // Fill the buffer with your desired waveform
      fill_data_buffer(&usb_bulk_buffer[usb_count & USB_BULK_BUFFER_MASK],
                       USB_TRANSFER_SIZE, &phase);

      // Atomically update shared counters
      usb_count += USB_TRANSFER_SIZE;
      nvic_disable_irq(NVIC_M0CORE_IRQ);
      m0_state.m4_count += USB_TRANSFER_SIZE;
      m0_state.m0_count += USB_TRANSFER_SIZE;
      nvic_enable_irq(NVIC_M0CORE_IRQ);
    }

    // Optional small delay
    __asm__("nop");
  }

  // Never reaches here, but cleanup code can be included
  transceiver_shutdown();
}

// Test data  ============================================
#include <math.h>
static uint8_t *dynamic_bit_pattern = NULL;
static size_t dynamic_pattern_len = 0;
static bool pattern_sent_once =
    false; // New flag to track if pattern was sent once
#define SINE_TABLE_SIZE 256
#define SINE_AMPLITUDE 80
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint8_t sine_table[SINE_TABLE_SIZE * 2]; // IQ samples

void init_sine_table(void) {
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    // Calculate phase angle for this sample
    float angle = 2.0f * M_PI * i / SINE_TABLE_SIZE;

    // Generate I/Q samples with proper amplitude scaling
    // Scale to -SINE_AMPLITUDE to +SINE_AMPLITUDE range
    int8_t i_sample = (int8_t)(SINE_AMPLITUDE * sinf(angle));
    int8_t q_sample = (int8_t)(SINE_AMPLITUDE * cosf(angle));

    // Convert to unsigned 8-bit by adding 128 (to center around 128)
    sine_table[i * 2] = (uint8_t)(i_sample + 128);     // I sample
    sine_table[i * 2 + 1] = (uint8_t)(q_sample + 128); // Q sample
  }
}

void fill_data_buffer(uint8_t *buffer, uint32_t len, uint32_t *_unused_phase) {
  const uint32_t BYTES_PER_SAMPLE = 2; // I + Q
  const uint8_t MID_SCALE = 0;         // unsigned zero level

  // phase increment through the sine table
  const uint32_t phase_increment = (SINE_FREQ * SINE_TABLE_SIZE) / SAMPLE_RATE;

  // persistent state
  static uint32_t table_phase = 0;   // index into sine_table
  static uint32_t sample_in_bit = 0; // 0…BIT_SAMPLES−1
  static uint32_t bit_index = 0;     // 0…dynamic_pattern_len−1

  // Check if we have a valid pattern
  if (!dynamic_bit_pattern || dynamic_pattern_len == 0) {
    // If no pattern is set, fill buffer with zeros
    memset(buffer, 0, len);
    return;
  }

  // If we've already sent the pattern once, fill with zeros
  if (pattern_sent_once) {
    // TODO: uncomment this
    // memset(buffer, 0, len);
    return;
  }

  uint32_t total_samples = len / BYTES_PER_SAMPLE;

  for (uint32_t s = 0; s < total_samples; s++) {
    uint32_t i = s * BYTES_PER_SAMPLE;
    if (dynamic_bit_pattern[bit_index]) {
      // — "1": sine output
      uint32_t ti = table_phase % SINE_TABLE_SIZE;
      buffer[i] = sine_table[2 * ti];
      buffer[i + 1] = sine_table[2 * ti + 1];
      table_phase = (table_phase + phase_increment) % SINE_TABLE_SIZE;
    } else {
      // — "0": flat mid-scale
      buffer[i] = MID_SCALE;
      buffer[i + 1] = MID_SCALE;
    }

    // advance sample count; after BIT_SAMPLES, move to next bit
    if (++sample_in_bit >= BIT_SAMPLES) {
      sample_in_bit = 0;
      bit_index = (bit_index + 1) % dynamic_pattern_len;

      // If we've completed one full pattern, set the flag
      if (bit_index == 0) {
        pattern_sent_once = true;
      }
    }
  }
}

void fill_square_buffer(uint8_t *buffer, uint32_t len, uint32_t *phase) {
  const uint32_t points_per_state = 8192;
  uint8_t high_val = 127;
  uint8_t low_val = 0;

  for (uint32_t i = 0; i < len; i += 2) {
    uint32_t current_point = (*phase) % (points_per_state * 2);

    if (current_point < points_per_state) {
      // High state (127 amplitude)
      buffer[i] = high_val;     // I sample
      buffer[i + 1] = high_val; // Q sample
    } else {
      // Low state (0 amplitude)
      buffer[i] = low_val;     // I sample
      buffer[i + 1] = low_val; // Q sample
    }

    // Increment phase to move to next point
    *phase = (*phase + 1) % (points_per_state * 2);
  }
}
