#include <stdio.h>
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

#include "hardware/structs/rosc.h"

uint32_t get_random_bit() {
    return rosc_hw->randombit & 1;
}

uint32_t get_random_bits(int n) {
    uint32_t rv = 0;
    for (int i = 0; i < n; ++i) {
        if (get_random_bit()) rv |= 1 << i;
    }
    return rv;
}

using namespace pimoroni;

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 240

static DVDisplay display;
static PicoGraphics_PenDV_P5 graphics(FRAME_WIDTH, FRAME_HEIGHT, display);

void on_uart_rx() {
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        putc(ch, stdout);
    }
}

static void init_palette() {
    graphics.create_pen(0, 0, 0);
    graphics.create_pen(7, 7, 7);
    graphics.create_pen(0x1f, 7, 7);
    graphics.create_pen(0x2f, 7, 7);
    graphics.create_pen(0x47, 15, 7);
    graphics.create_pen(0x57, 15, 7);
    graphics.create_pen(0x67, 15, 7);
    graphics.create_pen(0x77, 0x1f, 7);
    graphics.create_pen(0x8f, 0x27, 7);
    graphics.create_pen(0x9f, 0x2f, 7);
    graphics.create_pen(0xb4, 0x44, 7);
    graphics.create_pen(0xc7, 0x47, 7);
    graphics.create_pen(0xdf, 0x4f, 7);
    graphics.create_pen(0xdf, 0x57, 7);
    graphics.create_pen(0xd7, 0x5f, 7);
    graphics.create_pen(0xd7, 0x67, 15);
    graphics.create_pen(0xcf, 0x6f, 15);
    graphics.create_pen(0xcf, 0x7f, 15);
    graphics.create_pen(0xcf, 0x87, 0x17);
    graphics.create_pen(0xc7, 0x87, 0x17);
    graphics.create_pen(0xc7, 0x8f, 0x17);
    graphics.create_pen(0xc7, 0x97, 0x1f);
    graphics.create_pen(0xbf, 0x9f, 0x1f);
    graphics.create_pen(0xbf, 0xa4, 0x24);
    graphics.create_pen(0xbf, 0xa7, 0x27);
    graphics.create_pen(0xbf, 0xaf, 0x2f);
    graphics.create_pen(0xbc, 0xac, 0x2f);
    graphics.create_pen(0xb7, 0xb7, 0x37);
    graphics.create_pen(0xcf, 0xcf, 0x6f);
    graphics.create_pen(0xdf, 0xdf, 0x9f);
    graphics.create_pen(0xef, 0xef, 0xc7);
    graphics.create_pen(255, 255, 255);
}

uint8_t frame[FRAME_WIDTH * FRAME_HEIGHT + 1];

void init_fire() {
    memset(frame, 0, FRAME_WIDTH * (FRAME_HEIGHT - 1));
    for (int x = 0; x < FRAME_WIDTH; ++x) {
        uint32_t colour = get_random_bits(6);
        frame[FRAME_WIDTH * (FRAME_HEIGHT - 1) + x] = colour << 2;
    }
}

void step_fire() {
    for (int y = 1; y < FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FRAME_WIDTH; ++x) {
            uint32_t rand = get_random_bits(2);
            //if (rand == 3) rand = 0;
            uint8_t source = frame[y * FRAME_WIDTH + x];
            if (source > 124) source = 124;
            if (source > 0) {
                frame[((y - 1) * FRAME_WIDTH) + x - 1 + rand] = source - 4 * get_random_bit();
            }
        }
        display.write_palette_pixel_span({0, y-1}, FRAME_WIDTH, &frame[(y-1) * FRAME_WIDTH]);
    }

    for (int x = 0; x < FRAME_WIDTH; ++x) {
        uint32_t colour = frame[FRAME_WIDTH * (FRAME_HEIGHT - 1) + x];
        int32_t rand = get_random_bits(2);
        if (rand < 3 && colour < 252 && colour > 4) {
            rand -= 1;
            colour += rand * 4;
            frame[FRAME_WIDTH * (FRAME_HEIGHT - 1) + x] = colour;
        }
    }

    display.write_palette_pixel_span({0, FRAME_HEIGHT - 1}, FRAME_WIDTH, &frame[(FRAME_HEIGHT - 1) * FRAME_WIDTH]);
}

int main() {
  set_sys_clock_khz(266000, true);

  stdio_init_all();

    // Speed up the ring oscillator to reduce correlation of the random bits
    rosc_hw->ctrl = 0xfabfa7;

  //sleep_ms(5000);

  // Relay UART RX from the display driver
  gpio_set_function(5, GPIO_FUNC_UART);
  uart_init(uart1, 115200);
  uart_set_hw_flow(uart1, false, false);
  uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(uart1, false);
  irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);
  irq_set_enabled(UART1_IRQ, true);
  uart_set_irq_enables(uart1, true, false);

  DVDisplay::preinit();

  display.init(FRAME_WIDTH, FRAME_HEIGHT, DVDisplay::MODE_PALETTE, FRAME_WIDTH, FRAME_HEIGHT);

    init_palette();
    init_fire();

    //multicore_launch_core1(core1_main);

    while(true) {
        //sleep_ms(500);
        absolute_time_t start_time = get_absolute_time();
        step_fire();
        printf("Time %.2fms\n", absolute_time_diff_us(start_time, get_absolute_time()) * 0.001f);
        display.flip();
    }
}
