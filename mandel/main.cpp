#include <stdio.h>
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

extern "C" {
#include "mandelbrot.h"
}

using namespace pimoroni;

#define FRAME_WIDTH 720
#define FRAME_HEIGHT 480

class MirroredDVDisplay : public DVDisplay
{
public:
    MirroredDVDisplay(uint16_t width, uint16_t height)
        : DVDisplay()
    {}

    void set_scroll_idx_for_lines(int idx, int miny, int maxy) override;
};

static MirroredDVDisplay display(FRAME_WIDTH, FRAME_HEIGHT);
static PicoGraphics_PenDV_P5 graphics(FRAME_WIDTH, FRAME_HEIGHT, display);

static FractalBuffer fractal;

void on_uart_rx() {
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        putc(ch, stdout);
    }
}

static void init_palette() {
    graphics.create_pen(0, 0, 0);
    for (int i = 0; i < 31; ++i) {
        graphics.create_pen_hsv(i * (1.f / 31.f), 1.0f, 0.5f + (i & 7) * (0.5f / 7.f));
    }
}

static void init_mandel() {
  fractal.rows = FRAME_HEIGHT / 2;
  fractal.cols = FRAME_WIDTH;
  fractal.max_iter = 55;
  fractal.iter_offset = 0;
  fractal.minx = -2.25f;
  fractal.maxx = 0.75f;
  fractal.miny = -1.6f;
  fractal.maxy = 0.f - (1.6f / FRAME_HEIGHT); // Half a row
  fractal.use_cycle_check = true;
  init_fractal(&fractal);
}

#define NUM_ZOOMS 100
static uint32_t zoom_count = 0;

static void zoom_mandel() {
  if (++zoom_count == NUM_ZOOMS)
  {
    init_mandel();
    zoom_count = 0;
    sleep_ms(2000);
    return;
  }

  float zoomx = -.75f - .7f * ((float)zoom_count / (float)NUM_ZOOMS);
  float sizex = fractal.maxx - fractal.minx;
  float sizey = fractal.miny * -2.f;
  float zoomr = 0.974f * 0.5f;
  fractal.minx = zoomx - zoomr * sizex;
  fractal.maxx = zoomx + zoomr * sizex;
  fractal.miny = -zoomr * sizey;
  fractal.maxy = 0.f + fractal.miny / FRAME_HEIGHT;
  init_fractal(&fractal);
}

static void display_row(int y, uint8_t* buf) {
    for (int i = 0; i < FRAME_WIDTH; ++i)
    {
        uint8_t col = buf[i];
        if (col > 46) col -= 23;
        else if (col > 31) col -= 31;
        buf[i] = col << 2;
    }

    display.write_palette_pixel_span({0, y}, FRAME_WIDTH, buf);
}

static uint8_t row_buf_core1[FRAME_WIDTH];
void core1_main() {
    mandel_init();
    while (true) {
        int y = multicore_fifo_pop_blocking();
        generate_one_line(&fractal, row_buf_core1, y);
        multicore_fifo_push_blocking(y);
    }
}

static uint8_t row_buf[FRAME_WIDTH];
static void draw_two_rows(int y) {
    multicore_fifo_push_blocking(y+1);
    generate_one_line(&fractal, row_buf, y);

    display_row(y, row_buf);

    multicore_fifo_pop_blocking();
    display_row(y+1, row_buf_core1);
}

void draw_mandel() {
    display.wait_for_flip();
    for (int y = 0; y < FRAME_HEIGHT / 2; y += 2)
    {
        draw_two_rows(y);
    }
    display.flip_async();
}

int main() {
  set_sys_clock_khz(250000, true);

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

  DVDisplay::preinit();

  mandel_init();
  display.init(FRAME_WIDTH, FRAME_HEIGHT, DVDisplay::MODE_PALETTE, FRAME_WIDTH, FRAME_HEIGHT);

    init_palette();

    graphics.set_pen(0);
    graphics.clear();
    display.flip();
    graphics.set_pen(0);
    graphics.clear();

    multicore_launch_core1(core1_main);

    init_mandel();
    draw_mandel();

    while(true) {
        absolute_time_t start_time = get_absolute_time();
        zoom_mandel();
        draw_mandel();
        printf("Drawing zoom %d took %.2fms\n", zoom_count, absolute_time_diff_us(start_time, get_absolute_time()) * 0.001f);
    }
}

void MirroredDVDisplay::set_scroll_idx_for_lines(int idx, int miny, int maxy) 
{
    // This just ignores the arguments and always sets up the full frame table, 
    // but with the bottom half of the screen mirroring the top half.
    //
    // That allows us to draw just one half of the fractal and get the mirrored portion for free.
    uint32_t buf[8];
    uint addr = 4 * 7;
    uint line_type = 0x80000000u + ((uint)mode << 27);
    printf("Write header, line type %08x\n", line_type);
    for (int i = 0; i < display_height/2; i += 8) {
      for (int j = 0; j < 8; ++j) {
        buf[j] = line_type + ((uint32_t)h_repeat << 24) + ((i + j) * frame_width * 3) + base_address;
      }
      ram.write(addr, buf, 8 * 4);
      ram.wait_for_finish_blocking();
      addr += 4 * 8;
    }
    for (int i = 0; i < display_height/2; i += 8) {
      for (int j = 0; j < 8; ++j) {
        buf[j] = line_type + ((uint32_t)h_repeat << 24) + ((display_height/2 - 1 - (i + j)) * frame_width * 3) + base_address;
      }
      ram.write(addr, buf, 8 * 4);
      ram.wait_for_finish_blocking();
      addr += 4 * 8;
    }
}
