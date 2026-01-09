#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hackrf_core.h"
#include "platform_detect.h"
#include <libopencm3/lpc43xx/gpio.h>
#include <libopencm3/lpc43xx/m4/nvic.h>
#include <libopencm3/lpc43xx/scu.h>
#include <libopencm3/lpc43xx/uart.h>
#include <libopencm3/lpc43xx/cgu.h>
#include <libopencm3/lpc43xx/ccu.h>

// --------------------UART support---------------------
void uart_str(char *a) { // send string *a to uart
  int i = 0;
  while (a[i] != 0) {
    uart_write(UART0, a[i]);
    i++;
  }
}

bool get_uart_rate_config(int uart_rate, uint16_t *uart_divisor,
                          uint8_t *uart_divaddval, uint8_t *uart_mulval) {
  if (115200 == uart_rate) {
    *uart_divisor = 83;
    *uart_divaddval = 1;
    *uart_mulval = 3;
    return (true);
  }

  if (921600 == uart_rate) {
    *uart_divisor = 9;
    *uart_divaddval = 7;
    *uart_mulval = 13;
    return (true);
  }

  if (9600 == uart_rate) {
    *uart_divisor = 1033;
    *uart_divaddval = 2;
    *uart_mulval = 7;
    return (true);
  }

  return (false);
}

#define WAIT_CPU_CLOCK_INIT_DELAY (34) // 1us

void delay_1us(const uint32_t num_1us) {
  uint32_t i;
  const uint32_t duration = num_1us * WAIT_CPU_CLOCK_INIT_DELAY;

  for (i = 0; i < duration; i++)
    __asm__("nop");
}

// --------------------UART support---------------------

char DISPLAY_BUFFER[128];

int main(void) {
  detect_hardware_platform();
  pin_setup();

  /* Configure UART0 PIN*/
  scu_pinmux(SCU_PINMUX_U0_TXD, SCU_UART_RX_TX | SCU_CONF_FUNCTION1);
  scu_pinmux(SCU_PINMUX_U0_RXD, SCU_UART_RX_TX | SCU_CONF_FUNCTION1);

  /* enable 1V8 power supply so that the 1V8 LED lights up */
  enable_1v8_power();

  cpu_clock_init();

  /* Re-enable UART0 clock */
  CGU_BASE_UART0_CLK =
      CGU_BASE_UART0_CLK_AUTOBLOCK(1) | CGU_BASE_UART0_CLK_CLK_SEL(CGU_SRC_PLL1);
  CCU1_CLK_M4_USART0_CFG = 1;

  {
    uint16_t uart_divisor;
    uint8_t uart_divaddval, uart_mulval;
    get_uart_rate_config(921600, &uart_divisor, &uart_divaddval, &uart_mulval);
    uart_init(UART0, UART_DATABIT_8, UART_STOPBIT_1, UART_PARITY_NONE,
              uart_divisor, uart_divaddval, uart_mulval);
  }

  /* Blink LED1/2/3 on the board. */
  for (int i = 0;; i++) {
    // Turn on one LED at a time in sequence
    if (i % 3 == 0) {
      led_on(LED1);
      led_off(LED2);
      led_off(LED3);
    } else if (i % 3 == 1) {
      led_off(LED1);
      led_on(LED2);
      led_off(LED3);
    } else {
      led_off(LED1);
      led_off(LED2);
      led_on(LED3);
    }

    delay_1us(200000); // Add a small delay so the blinking is visible

    sprintf(DISPLAY_BUFFER, "Hello HACKRF. %d\n", i++);
    uart_str(DISPLAY_BUFFER);
  }

  return 0;
}
