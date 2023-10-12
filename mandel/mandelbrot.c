// Copyright (C) Michael Bell 2021

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/interp.h"

#include "mandelbrot.h"

// Cycle checking parameters
#define MAX_CYCLE_LEN 8          // Must be power of 2
#define MIN_CYCLE_CHECK_ITER 24  // Must be multiple of max cycle len
#define CYCLE_TOLERANCE (1<<18)

// Fixed point with 6 bits to the left of the point.
// Range [-32,32) with precision 2^-26
typedef int32_t fixed_pt_t;

#define ESCAPE_SQUARE (4<<26)

static inline fixed_pt_t mul(fixed_pt_t a, fixed_pt_t b)
{
  int32_t ah = a >> 13;
  int32_t al = a & 0x1fff;
  int32_t bh = b >> 13;
  int32_t bl = b & 0x1fff;

  // Ignore al * bl as contribution to final result is only the carry.
  fixed_pt_t r = ((ah * bl) + (al * bh)) >> 13;
  r += ah * bh;
  return r;
}

// a * b * 2
static inline fixed_pt_t mul2(fixed_pt_t a, fixed_pt_t b)
{
  int32_t ah = a >> 12;
  int32_t al = (a & 0xfff) << 1;
  int32_t bh = b >> 13;
  int32_t bl = b & 0x1fff;

  fixed_pt_t r = ((ah * bl) + (al * bh)) >> 13;
  r += ah * bh;
  return r;
}

static inline fixed_pt_t square(fixed_pt_t a) {
  int32_t ah = a >> 13;
  int32_t al = a & 0x1fff;

  return ((ah * al) >> 12) + (ah * ah);
}

fixed_pt_t make_fixed(int32_t x) {
  return x << 26;
}

fixed_pt_t make_fixedf(float x) {
  return (int32_t)(x * (67108864.f));
}

void mandel_init()
{
  interp_config cfg = interp_default_config();
  interp_set_config(interp0, 0, &cfg);
  interp_set_config(interp1, 0, &cfg);
  interp0->base[0] = 1;
}

void init_fractal(FractalBuffer* f)
{
  f->done = false;
  f->min_iter = f->max_iter - 1;
  f->iminx = make_fixedf(f->minx);
  f->imaxx = make_fixedf(f->maxx);
  f->iminy = make_fixedf(f->miny);
  f->imaxy = make_fixedf(f->maxy);
  f->incx = (f->imaxx - f->iminx) / (f->cols - 1);
  f->incy = (f->imaxy - f->iminy) / (f->rows - 1);
  f->count_inside = 0;
}

static inline void generate_one(FractalBuffer* f, fixed_pt_t x0, fixed_pt_t y0, uint8_t* buffptr)
{
  fixed_pt_t x = x0;
  fixed_pt_t y = y0;

  uint16_t k = 1;
  for (; k < f->max_iter; ++k) {
    fixed_pt_t x_square = square(x);
    fixed_pt_t y_square = square(y);
    if (x_square + y_square > ESCAPE_SQUARE) break;

    fixed_pt_t nextx = x_square - y_square + x0;
    y = mul2(x,y) + y0;
    x = nextx;
  }
  if (k == f->max_iter) {
    *buffptr = 0;
    f->count_inside++;
  } else {
    if (k > f->iter_offset) k -= f->iter_offset;
    else k = 1;
    *buffptr = k;
    if (f->min_iter > k) f->min_iter = k;
  }
}

static inline void generate_one_cycle_check(FractalBuffer* f, fixed_pt_t x0, fixed_pt_t y0, uint8_t* buffptr)
{
  fixed_pt_t x = x0;
  fixed_pt_t y = y0;
  fixed_pt_t oldx = 0, oldy = 0;

  interp0->accum[0] = 1;
  //uint32_t k = 1;
  while (true) {
    fixed_pt_t x_square = square(x);
    fixed_pt_t y_square = square(y);
    if (x_square + y_square > ESCAPE_SQUARE) break;

    uint32_t k = interp0->pop[0];
    if (k == f->max_iter) {
      *buffptr = 0;
      f->count_inside++;
      return;
    }
    if (k >= MIN_CYCLE_CHECK_ITER) {
      if ((k & (MAX_CYCLE_LEN - 1)) == 0) {
        oldx = x - CYCLE_TOLERANCE;
        oldy = y - CYCLE_TOLERANCE;
      }
      else
      {
        if ((uint32_t)(x - oldx) < (2*CYCLE_TOLERANCE) && (uint32_t)(y - oldy) < (2*CYCLE_TOLERANCE)) {
          // Found a cycle
          *buffptr = 0;
          f->count_inside++;
          return;
        }
      }
    }

    fixed_pt_t nextx = x_square - y_square + x0;
    y = mul2(x,y) + y0;
    x = nextx;
  }

  uint16_t k = interp0->accum[0];
  if (k > f->iter_offset) k -= f->iter_offset;
  else k = 1;
  *buffptr = k;
  if (f->min_iter > k) f->min_iter = k;
}

void generate_one_line(FractalBuffer* f, uint8_t* buf, uint16_t ipos)
{
  if (f->done) return;

  fixed_pt_t y0 = f->iminy + ipos * f->incy;
  fixed_pt_t x0 = f->iminx;
  uint8_t* buf_end = buf + f->cols;
  interp1->base[0] = f->incx;
  interp1->accum[0] = f->iminx;

  while (buf < buf_end) {
    generate_one_cycle_check(f, x0, y0, buf++);
    x0 = interp1->pop[0];
  }
}
