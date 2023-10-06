#include <cstdio>
#include <math.h>

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

using namespace pimoroni;

#define DISPLAY_WIDTH 720
#define DISPLAY_HEIGHT 576

#define FRAME_WIDTH DISPLAY_WIDTH
#define FRAME_HEIGHT (DISPLAY_HEIGHT*2)

#define USE_PALETTE 0

extern "C" int decode_edid(unsigned char* edid, char* edid_buf_in);

void on_uart_rx() {
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        putc(ch, stdout);
    }
}

static char decoded_edid[8192];

int main() {
  stdio_init_all();

  // Relay UART RX from the display driver
  gpio_set_function(5, GPIO_FUNC_UART);
  uart_init(uart1, 115200);
  uart_set_hw_flow(uart1, false, false);
  uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(uart1, false);
  irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);
  irq_set_enabled(UART1_IRQ, true);
  uart_set_irq_enables(uart1, true, false);

  sleep_ms(5000);

  DVDisplay::preinit();

  DVDisplay display;
  display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT, DVDisplay::MODE_RGB555, FRAME_WIDTH, FRAME_HEIGHT);
  PicoGraphics_PenDV_RGB555 graphics(FRAME_WIDTH, FRAME_HEIGHT, display);

  graphics.set_pen(0, 0, 0);
  graphics.clear();
  display.set_scroll_idx_for_lines(1, 0, DISPLAY_HEIGHT);
  display.flip();

  uint8_t edid[128];
  display.get_edid(edid);
  int len = decode_edid(edid, decoded_edid);
  printf("%s", decoded_edid);

  graphics.clear();
  graphics.set_pen(200, 200, 200);
  graphics.set_font("bitmap8");
  graphics.text(decoded_edid, {0, 0}, FRAME_WIDTH);
  display.set_scroll_idx_for_lines(1, 0, DISPLAY_HEIGHT);
  display.flip();

  while (true) {
    if (display.is_button_x_pressed()) {
      display.setup_scroll_group({0, DISPLAY_HEIGHT});
    }
    else if (display.is_button_a_pressed()) {
      display.setup_scroll_group({0, 0});
    }

    sleep_ms(50);
  }
}