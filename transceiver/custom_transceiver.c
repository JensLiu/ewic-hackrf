#include "custom_transceiver.h"
#include "custom_config.h"
#include "gr_lib.h"
#include "uart.h"

#include "hackrf_core.h"
#include "hackrf_ui.h"
#include "m0_state.h"
#include "operacake_sctimer.h"
#include "radio.h"
#include "streaming.h"
#include "tinyusb_port.h"
#include <libopencm3/lpc43xx/m4/nvic.h>

#include <math.h>

static volatile uint32_t _tx_underrun_limit;
static volatile uint32_t _rx_overrun_limit;

void transceiver_shutdown(void) {
  baseband_streaming_disable(&sgpio_config);
  operacake_sctimer_reset_state();

  led_off(LED2);
  led_off(LED3);
  radio_switch_mode(&radio, RADIO_CHANNEL0, TRANSCEIVER_MODE_OFF);
  m0_set_mode(M0_MODE_IDLE);
}

void transceiver_startup(const transceiver_mode_t mode) {
  radio_switch_mode(&radio, RADIO_CHANNEL0, mode);
  hackrf_ui()->set_transceiver_mode(mode);

  switch (mode) {
  case TRANSCEIVER_MODE_RX_SWEEP:
  case TRANSCEIVER_MODE_RX:
    led_off(LED3);
    led_on(LED2);
    m0_set_mode(M0_MODE_RX);
    m0_state.shortfall_limit = _rx_overrun_limit;
    break;
  case TRANSCEIVER_MODE_TX:
    led_off(LED2);
    led_on(LED3);
    m0_set_mode(M0_MODE_TX_START);
    m0_state.shortfall_limit = _tx_underrun_limit;
    break;
  default:
    break;
  }

  activate_best_clock_source();
  trigger_enable(radio_get_trigger_enable(&radio, RADIO_CHANNEL0));
}

// receiver state
uint32_t rx_consume_count = 0;
uint32_t rx_sample_count = 0;
uint64_t rx_mag2_sum = 0;
bool rx_current_bit = false;
uint8_t rx_bit_buffer[RX_BIT_PACKET_SIZE];
uint32_t rx_bit_buffer_index = 0;
uint8_t rx_usb_tx_buffer[RX_BIT_PACKET_SIZE];
uint32_t rx_noise_floor = 0;

void custom_transceiver_receive_begin() {
#ifdef RX_SAMPLE_RATE
  set_sample_rate(RX_SAMPLE_RATE);
#endif
#ifdef RX_BASEBAND_FILTER_BW
  set_baseband_filter_bandwidth(RX_BASEBAND_FILTER_BW);
#endif
#ifdef CENTRE_FREQ
  set_centre_frequency(CENTRE_FREQ);
#endif
#ifdef RX_RF_GAIN
  rx_set_rf_gain(RX_RF_GAIN);
#endif
#ifdef RX_IF_GAIN
  rx_set_if_gain(RX_IF_GAIN);
#endif
#ifdef RX_BB_GAIN
  rx_set_bb_gain(RX_BB_GAIN);
#endif
#ifdef RX_ANTENNA_ENABLE
  set_antenna_enable(RX_ANTENNA_ENABLE);
#endif
  transceiver_startup(TRANSCEIVER_MODE_RX);
  baseband_streaming_enable(&sgpio_config);
  rx_consume_count = 0;
  rx_sample_count = 0;
  rx_mag2_sum = 0;
  rx_current_bit = false;
  rx_bit_buffer_index = 0;
  rx_noise_floor = 0;
  tusb_uart_printf("Custom transceiver receive started\n");
}

void custom_transceiver_receive_end() {
  transceiver_shutdown();
  rx_consume_count = 0;
  rx_sample_count = 0;
  rx_mag2_sum = 0;
  rx_current_bit = false;
  rx_bit_buffer_index = 0;
  rx_noise_floor = 0;
  tusb_uart_printf("Custom transceiver receive stopped\n");
}

void custom_transceiver_receive() {

  if ((m0_state.m0_count - rx_consume_count) >= BATCH_SAMPLE_SIZE) {
    uint8_t *buffer = &usb_bulk_buffer[rx_consume_count & USB_BULK_BUFFER_MASK];

    for (uint32_t i = 0; i < BATCH_SAMPLE_SIZE; i += 2) {
      int32_t I = (int8_t)buffer[i];
      int32_t Q = (int8_t)buffer[i + 1];

      uint32_t mag2 = I * I + Q * Q;
      //   uart_printf("%d\t", mag2);
      rx_mag2_sum += mag2;
      rx_sample_count++;

      if (rx_sample_count >= RX_BIT_SAMPLES) {
        const uint32_t mag2_avg = rx_mag2_sum / RX_BIT_SAMPLES;
        rx_noise_floor = (rx_noise_floor * 15 + mag2_avg) / 16;
        const uint32_t adaptive_threshold =
            rx_noise_floor + RX_THRESHOLD_MARGIN;
        rx_current_bit = (mag2_avg > adaptive_threshold);
        uart_printf("%d", rx_current_bit);

        // data transfer
#ifdef DECODE_PRINT_UART_BATCH
        rx_bit_buffer[rx_bit_buffer_index++] = rx_current_bit;
        if (rx_bit_buffer_index >= RX_BIT_PACKET_SIZE) {
          for (int i = 0; i < RX_BIT_PACKET_SIZE; i++) {
            uart_printf("%d", rx_bit_buffer[i]);
          }
          uart_printf("\n");
          rx_bit_buffer_index = 0;
        }
#endif
        {
          ftdi_host_write(&rx_current_bit, 1);
          tuh_task(); // Ensure USB transfer is processed
          static uint8_t read_buf[256];
          uint32_t read_len = ftdi_host_read(read_buf, sizeof(read_buf));
          if (read_len > 0) {
            tusb_uart_printf("[MAIN] FTDI received %lu bytes\r\n", read_len);
            tusb_uart_printf("[MAIN] Data: ");
            for (uint32_t i = 0; i < read_len; i++) {
              tusb_uart_printf("%02X ", read_buf[i]);
            }
            tusb_uart_printf("\n");
          }
        }

        rx_sample_count = 0;
        rx_mag2_sum = 0;
      }
    }

    rx_consume_count += BATCH_SAMPLE_SIZE;
    m0_state.m4_count += BATCH_SAMPLE_SIZE;
  }
}

void rx_mode() {
#ifdef RX_SAMPLE_RATE
  set_sample_rate(RX_SAMPLE_RATE);
#endif
#ifdef RX_BASEBAND_FILTER_BW
  set_baseband_filter_bandwidth(RX_BASEBAND_FILTER_BW);
#endif
#ifdef CENTRE_FREQ
  set_centre_frequency(CENTRE_FREQ);
#endif
#ifdef RX_RF_GAIN
  rx_set_rf_gain(RX_RF_GAIN);
#endif
#ifdef RX_IF_GAIN
  rx_set_if_gain(RX_IF_GAIN);
#endif
#ifdef RX_BB_GAIN
  rx_set_bb_gain(RX_BB_GAIN);
#endif
#ifdef RX_ANTENNA_ENABLE
  set_antenna_enable(RX_ANTENNA_ENABLE);
#endif

  uint32_t consume_count = 0;
  transceiver_startup(TRANSCEIVER_MODE_RX);
  baseband_streaming_enable(&sgpio_config);

  // Constants matching the transmitter
  uint32_t sample_count = 0;
  uint64_t mag2_sum = 0; // Sum of magnitude squared
  bool current_bit = false;
  uint8_t bit_buffer[RX_BIT_PACKET_SIZE]; // Buffer to store detected bits
  uint32_t bit_buffer_index = 0;
  uint8_t tx_buffer[RX_BIT_PACKET_SIZE]; // Separate buffer for USB transfers
  uint32_t noise_floor = 0;

  while (1) {
    if ((m0_state.m0_count - consume_count) >= BATCH_SAMPLE_SIZE) {
      uint8_t *buffer = &usb_bulk_buffer[consume_count & USB_BULK_BUFFER_MASK];

      for (uint32_t i = 0; i < BATCH_SAMPLE_SIZE; i += 2) {
        int32_t I = (int8_t)buffer[i];
        int32_t Q = (int8_t)buffer[i + 1];

        uint32_t mag2 = I * I + Q * Q;
        // uart_printf("%d\t", mag2);
        mag2_sum += mag2;
        sample_count++;

        if (sample_count >= RX_BIT_SAMPLES) {
          const uint32_t mag2_avg = mag2_sum / RX_BIT_SAMPLES;
          noise_floor = (noise_floor * 15 + mag2_avg) / 16;
          const uint32_t adaptive_threshold = noise_floor + RX_THRESHOLD_MARGIN;
          current_bit = (mag2_avg > adaptive_threshold);

          // uart_printf("%d\t%d\n", current_bit, mag2_avg);

          // data transfer
#ifdef DECODE_PRINT_UART_BATCH
          bit_buffer[bit_buffer_index++] = current_bit;
          if (bit_buffer_index >= RX_BIT_PACKET_SIZE) {
            for (int i = 0; i < RX_BIT_PACKET_SIZE; i++) {
              uart_printf("%d", bit_buffer[i]);
            }
            uart_printf("\n");
            bit_buffer_index = 0;
          }
#endif
#ifdef DECODE_PRINT_UART_EACH
          uart_printf("%d", current_bit);
#endif
#ifdef DECODE_PRINT_USB_BATCH
          bit_buffer[bit_buffer_index++] = current_bit;
          if (bit_buffer_index >= RX_BIT_PACKET_SIZE) {
            memcpy(tx_buffer, bit_buffer, RX_BIT_PACKET_SIZE);
            usb_transfer_schedule_block(
                &usb_endpoint_bulk_in, tx_buffer, RX_BIT_PACKET_SIZE,
                transceiver_bulk_transfer_complete, NULL);
            while (!usb_endpoint_bulk_in.transfer_complete) {
              __asm__("nop");
            }
            bit_buffer_index = 0;
          }
#endif
          sample_count = 0;
          mag2_sum = 0;
        }
      }

      // if (m0_state.num_shortfalls > 0) {
      // 	uart_printf(
      // 		"\nWarning: M0 shortfall count = %u\n",
      // 		m0_state.num_shortfalls);
      // }

      consume_count += BATCH_SAMPLE_SIZE;
      m0_state.m4_count += BATCH_SAMPLE_SIZE;
    }
  }

  transceiver_shutdown();
}