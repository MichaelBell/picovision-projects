#include <cstdio>
#include <math.h>

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

using namespace pimoroni;

#define DISPLAY_WIDTH 360
#define DISPLAY_HEIGHT 480

#define FRAME_WIDTH 1000
#define FRAME_HEIGHT 600

DVDisplay display;

void on_uart_rx() {
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        putc(ch, stdout);
    }
}

static constexpr int NUM_CIRCLES = 50;
static struct Circle {
  uint16_t x, y, size, grow;
  uint32_t pen;
} circles[NUM_CIRCLES];

void setup_pen(PicoGraphicsDV* graphics, DVDisplay::Mode mode) {
  graphics->create_pen(0, 0, 0);
  graphics->create_pen(0xFF, 0xFF, 0xFF);

  if (mode == DVDisplay::MODE_PALETTE) {
    for (int i = 0; i < 25; ++i) {
      graphics->create_pen_hsv(i * 0.04f, 1.0f, 1.0f);
    }
    for (int i = 0; i < 5; ++i) {
      graphics->create_pen((i+3) * (255/8), 255, 255);
    }
  }

  for(int i =0 ; i < 50 ; i++)
  {
    circles[i].size = (rand() % 50) + 1;
    circles[i].grow = std::max(0, (rand() % 50) - 25);
    circles[i].x = rand() % graphics->bounds.w;
    circles[i].y = rand() % graphics->bounds.h;
    if (mode == DVDisplay::MODE_PALETTE) {
      circles[i].pen = 2 + (i >> 1);
    } else {
      circles[i].pen = graphics->create_pen_hsv(i * 0.02f, 1.0f, 1.0f);
    }
  }

  display.set_scroll_idx_for_lines(1, 100, 200);
  display.set_scroll_idx_for_lines(2, 300, FRAME_HEIGHT);
  display.flip();
  display.set_scroll_idx_for_lines(1, 100, 200);
  display.set_scroll_idx_for_lines(2, 300, FRAME_HEIGHT);
}

int main() {
  set_sys_clock_khz(200000, true);

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

  constexpr uint BUTTON_Y = 9;
  gpio_init(BUTTON_Y);
  gpio_set_dir(BUTTON_Y, GPIO_IN);
  gpio_pull_up(BUTTON_Y);

  //sleep_ms(5000);

  DVDisplay::Mode mode = DVDisplay::MODE_RGB888;
  display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT, mode, FRAME_WIDTH, FRAME_HEIGHT);

  PicoGraphicsDV* graphics = new PicoGraphics_PenDV_RGB888(FRAME_WIDTH, FRAME_HEIGHT, display);
  setup_pen(graphics, mode);

  printf("Starting\n");
  graphics->set_font("bitmap8");

  Point scroll1 = {0, 0};
  Point scroll2 = {0, 0};
  int scroll_dir[2] = {1,1};

  int frames = 0;
  while (true) {
    //while(gpio_get(BUTTON_A) == 1) {
    //  sleep_ms(10);
    //}
    uint32_t render_start_time = time_us_32();

#if 1
    for (int j = 0; j < FRAME_HEIGHT; ++j) {
      graphics->set_pen(j & 0xFF, 0xFF, 0xFF);
      graphics->pixel_span({0,j}, FRAME_WIDTH);
    }
#else
    graphics->set_pen(0xFF, 0xFF, 0xFF);
    graphics->clear();
#endif

#if 0
    for (uint i = 0; i < 128; i++) {
      for (uint j = 0; j < 256; j++) {
        RGB555 col = (j << 7) | i;
        graphics->set_pen((col << 3) & 0xF8, (col >> 2) & 0xF8, (col >> 7) & 0xF8);
        graphics->pixel(Point(j, i));
      }
    }

    for (uint i = 0; i < 128; i++) {
      for (uint j = 0; j < 256; j++) {
        graphics->set_pen((j << 7) | i);
        graphics->pixel(Point(i, j+128));
      }
    }
#endif

    for(int i =0 ; i < NUM_CIRCLES ; i++)
    {
      graphics->set_pen(0, 0, 0);
      graphics->circle(Point(circles[i].x, circles[i].y), circles[i].size);

      //RGB col = RGB::from_hsv(i * 0.02f, 1.0f, 1.0f);
      //graphics->set_pen(col.r, col.g, col.b);
      graphics->set_pen(circles[i].pen);
      graphics->circle(Point(circles[i].x, circles[i].y), circles[i].size-2);
      if (circles[i].grow) {
        circles[i].size++;
        circles[i].grow--;
      } else {
        circles[i].size--;
        if (circles[i].size == 0) {
          circles[i].size = 1;
          circles[i].grow = rand() % 75;
          circles[i].x = rand() % graphics->bounds.w;
          circles[i].y = rand() % graphics->bounds.h;
        }
      }
    }

#if 0
    uint x = 260; //rand() % graphics->bounds.w;
    uint y = 468; //rand() % graphics->bounds.h;
    printf("Circle at (%d, %d)\n", x, y);
    graphics->set_pen(0);
    graphics->circle(Point(x, y), 25);
  #endif

    uint32_t render_time = time_us_32() - render_start_time;

    char buffer[8];
    sprintf(buffer, "%s %s %s", 
            gpio_get(BUTTON_Y) == 0 ? "Y" : " ", 
            display.is_button_x_pressed() ? "X" : " ",
            display.is_button_a_pressed() ? "A" : " ");
    graphics->set_pen(0, 0, 0);
    graphics->text(buffer, {500,10}, FRAME_WIDTH - 500, 3);

    uint32_t flip_start_time = time_us_32();
    display.flip();
    uint32_t flip_time = time_us_32() - flip_start_time;
    if (false) printf("Render: %.3f, flip: %.3f\n", render_time / 1000.f, flip_time / 1000.f);

    //printf("%02x %02x\n", display.get_gpio(), display.get_gpio_hi());

#if 0
    display.setup_scroll_group(scroll1, 1);
    display.setup_scroll_group(scroll2, 2);
    scroll1.x += scroll_dir[0];
    if (scroll1.x + DISPLAY_WIDTH > FRAME_WIDTH || scroll1.x < 0) {
      scroll_dir[0] = -scroll_dir[0];
      scroll1.x += scroll_dir[0];
    }
    scroll2.y += scroll_dir[1];
    if (scroll2.y + DISPLAY_HEIGHT > FRAME_HEIGHT || scroll2.y < 0) {
      scroll_dir[1] = -scroll_dir[1];
      scroll2.y += scroll_dir[1];
    }
#endif

    ++frames;
    display.set_gpio_hi_pull_up_all(frames & 0x3F);
    display.set_gpio_hi_pull_down_all(~(frames & 0x3F));
    if (gpio_get(BUTTON_Y) == 0) display.set_led_level((uint8_t)frames);
    else display.set_led_heartbeat();

    if (display.is_button_a_pressed()) {
      delete graphics;
      if (mode == DVDisplay::MODE_RGB888) {
        graphics = new PicoGraphics_PenDV_P5(FRAME_WIDTH, FRAME_HEIGHT, display);
        mode = DVDisplay::MODE_PALETTE;
        display.set_mode(mode);
      }
      else {
        graphics = new PicoGraphics_PenDV_RGB888(FRAME_WIDTH, FRAME_HEIGHT, display);
        mode = DVDisplay::MODE_RGB888;
        display.set_mode(mode);
      }
      setup_pen(graphics, mode);
    }
  }
}