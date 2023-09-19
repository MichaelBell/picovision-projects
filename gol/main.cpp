#include <stdio.h>
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "drivers/dv_display/dv_display.hpp"
#include "libraries/pico_graphics/pico_graphics_dv.hpp"

#include "hardware/structs/rosc.h"

bool get_random_bit() {
    return rosc_hw->randombit;
}

uint32_t get_4_random_bits() {
    uint32_t rv = 0;
    for (int i = 0; i < 4; ++i) {
        if (get_random_bit()) rv |= 1 << i;
    }
    return rv;
}

using namespace pimoroni;

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480

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
    graphics.create_pen(255, 255, 255);
}

struct GameOfLife {
    int width; // Must be multiple of 32
    int width_in_words;
    int height;
    int gen_count;
    uint32_t* board;
    uint32_t* next_board;
} gol;

uint32_t gol_lut[1 << 8];

void init_gol(int width, int height) {
    gol.width_in_words = ((width + 31) / 32);
    gol.width = gol.width_in_words * 32;
    gol.height = height;
    gol.gen_count = 0;
    gol.board = (uint32_t*)malloc((gol.width / 8) * gol.height);
    memset(gol.board, 0, (gol.width / 8) * gol.height);
    gol.next_board = (uint32_t*)malloc((gol.width / 8) * gol.height);
    memset(gol.next_board, 0, (gol.width / 8) * gol.height);

    uint32_t b = 0;
    for (int i = 0, j = 0; i < (1 << 12); ++i) {
        int pop = __builtin_popcount(i & 0x777);
        if (i & (1 << 5)) {
            // Alive
            if (pop == 3 || pop == 4) b |= (1 << j);
        }
        else {
            // Dead
            if (pop == 3) b |= (1 << j);
        }
        pop = __builtin_popcount(i & 0xeee);
        ++j;
        if (i & (1 << 6)) {
            // Alive
            if (pop == 3 || pop == 4) b |= (1 << j);
        }
        else {
            // Dead
            if (pop == 3) b |= (1 << j);
        }
        if (++j == 32) {
            gol_lut[i >> 4] = b;
            j = 0;
            b = 0;
        }
    }

    // Initial state
    #if 1
    gol.board[gol.width_in_words] = 0x30;
    gol.board[gol.width_in_words * 2] = 0x30;

    gol.board[gol.width_in_words * 5] = 0x20100;
    gol.board[gol.width_in_words * 6] = 0x40100;
    gol.board[gol.width_in_words * 7] = 0x70100;

    for (int y = 100; y < 380; ++y) {
        for (int x = 100; x < 540; ++x) {
            if (get_4_random_bits() < 5) {
                gol.board[gol.width_in_words * y + (x >> 5)] |= 1 << (x & 0x1f);
            }
        }
    }
    #else
    gol.board[gol.width_in_words * 240 + 13] = 0xFF7C703F;
    gol.board[gol.width_in_words * 240 + 12] = 0xBE000000;
    #endif
}

void setup_from_rle(int w, int h, const char* rle_str, int col_in_words = 0) {
    memset(gol.board, 0, (gol.width / 8) * gol.height);
    gol.gen_count = 0;

    const char* p = rle_str;
    const int start_col = col_in_words == 0 ? ((gol.width - w)/2 + 31) / 32 : col_in_words;
    int x = start_col;
    int y = (gol.height - h)/2;
    uint32_t b = 0;
    int j = 0;
    while (*p != '!') {
        int num = 1;
        if (*p == '\n' || *p == '\r') {
            p++;
            continue;
        }
        if (*p >= '1' && *p <= '9') {
            num = *p++ - '0';
            while (*p >= '0' && *p <= '9') {
                num *= 10;
                num += *p++ - '0';
            }
        }
        if (*p == '$') {
            gol.board[y * gol.width_in_words + x] = b;
            x = start_col;
            y += num;
            p++;
            j = 0;
            b = 0;
        } else {
            if (*p++ == 'o') {
                for (int i = 0; i < num; ++i) {
                    b |= 1 << j;
                    if (++j == 32) {
                        gol.board[y * gol.width_in_words + x++] = b;
                        b = 0;
                        j = 0;
                    }
                }
            }
            else {
                j += num;
                while (j >= 32) {
                    gol.board[y * gol.width_in_words + x++] = b;
                    b = 0;
                    j -= 32;
                }
            }
        }
    }
    gol.board[y * gol.width_in_words + x] = b;
}

static uint8_t row_modified[FRAME_HEIGHT];

void gol_compute(int miny, int maxy) {
    uint32_t* board_ptr = &gol.board[gol.width_in_words * (miny - 1)];
    uint32_t* next_board_ptr = &gol.next_board[gol.width_in_words * miny];
    for (int y = miny; y < maxy; ++y) {
        row_modified[y] = 0;
        uint32_t b_above = board_ptr[0];
        uint32_t b_this = board_ptr[gol.width_in_words];
        uint32_t b_below = board_ptr[2 * gol.width_in_words];
        ++board_ptr;
        uint32_t nb = 0;
        for (int x = 1, j = 1; x < gol.width - 1; x += 2) {
            uint32_t lookup = ((b_above & 0xf) << 8) | ((b_this & 0xf) << 4) | (b_below & 0xf);
            uint32_t bit = (gol_lut[lookup >> 4] >> ((2 * lookup) & 0x1e)) & 3;
            nb |= bit << j;
            b_above >>= 2;
            b_this >>= 2;
            b_below >>= 2;
            j += 2;
            if (j == 33) {
                j = 1;
                if (*next_board_ptr != nb) row_modified[y] = 1;
                *next_board_ptr++ = nb;
                nb = bit >> 1;
            }
            if (j == 17) {
                b_above |= board_ptr[0] << 16;
                b_this |= board_ptr[gol.width_in_words] << 16;
                b_below |= board_ptr[gol.width_in_words * 2] << 16;

                if (b_above == 0 && b_this == 0 && b_below == 0) {
                    x += 16;
                    if (x < gol.width - 1) {
                        j = 1;
                        if (*next_board_ptr != nb) row_modified[y] = 1;
                        *next_board_ptr++ = nb;
                        nb = 0;
                    }
                }
            }
            if (j == 1) {
                b_above |= board_ptr[0] & 0xFFFF0000;
                b_this |= board_ptr[gol.width_in_words] & 0xFFFF0000;
                b_below |= board_ptr[gol.width_in_words * 2] & 0xFFFF0000;
                ++board_ptr;
                if (b_above == 0 && b_this == 0 && b_below == 0) {
                    x += 14;
                    j = 15;
                }
            }
        }
        if (*next_board_ptr != nb) row_modified[y] = 1;
        *next_board_ptr++ = nb;
    }
}

void gol_generation() {
    multicore_fifo_push_blocking(0);
    gol_compute(1, gol.height / 2);
    multicore_fifo_pop_blocking();

    std::swap(gol.board, gol.next_board);
    ++gol.gen_count;
}

static void display_row(int y, uint8_t* buf) {
    display.write_palette_pixel_span({0, y}, FRAME_WIDTH, buf);
}

static uint8_t row_buf[FRAME_WIDTH] alignas(4);
void display_gol() {
    for (int y = 0; y < FRAME_HEIGHT; ++y) {
        if (gol.gen_count < 2 || row_modified[y]) {
            uint32_t* board_ptr = &gol.board[y * gol.width_in_words];
            for (int x = 0, j = 0; x < FRAME_WIDTH; ++x) {
                row_buf[x] = (*board_ptr & (1 << j)) ? 4 : 0;
                if (++j == 32) {
                    j = 0;
                    ++board_ptr;
                }
            }
            
            display_row(y, row_buf);
        }
    }
}

void core1_main() {
    while (true) {
        multicore_fifo_pop_blocking();
        gol_compute(gol.height / 2, gol.height - 1);
        multicore_fifo_push_blocking(0);
    }
}

// From https://conwaylife.com/ref/DRH/reburn.html
const char reburn_rle[] = 
"27b2o$26b2ob2o$27b4o$18b4o6b2o$17b6o10b2o$16b2ob4o12bo$17b2o14b3o3$8b\
4o8b2o$8bo3bo9bo$8bo11b2o$9bo2bo2b2o5b3o6bo3bo$14b3o6b3o5b4o$9bo2bo2b\
2o5b3o7b2o59bo$8bo11b2o70bo$8bo3bo9bo69b3o$8b4o17b2o$27bo$28bobo$22b2o\
5bo$21b4o$20b2ob2o$21b2o12bo$34bo$34b3o3$19b6o50bo$19bo5bo15bo32bobob\
2o$19bo20bo23bobo7bobobobo$20bo4bo14b3o19b2o2b2o7b2o3bo$22b2o39bob2obo\
8b3o$13b4o18bo11b3o15bo3bo7bo$13bo3bo6bo9bobo10b3obo14bobo$13bo9b2o9bo\
bo15bo13bo$14bo2bo5bobo9bo16b2o12bo$51b2o12bobo11bo$2b2o47bo28bo$b4o\
12bo53bo7bo$2ob2o10b4o21bobo29bo$b2o5b2ob2obo4bo19bo2bobo4bo20b2o5b3o$\
7bo6bob2o20b2o6bobob2o12b2o4bo5b5o$6b2o13b2o14bo4bo3bo3bo12b4o8b2ob3o$\
7bo6b3o2bo3bo12b5obo3bobob2o10b2ob2o9b2o$b2o5b2ob2ob3obob3o12bo8bo4bo\
13b2o$2ob2o30b4o4bo$b4o29bo8bo$2b2o29b3o2bo5bobo$34b3o7bo7bo$9b6o20bo\
10bo3b3o$9bo5bo20b3obo3b3o2b2o$9bo26bo2b2o2b3o3bo$10bo4bo4bo2bo13b3o4b\
2o5bo$12b2o5bo19bo5b6o$19bo3bo13bo8bobo$19b4o14bobo9b4obo$36bo12bo3b2o\
$37bobo11b2o$37bo13b4o$39bo14bo$37b3o$36bo2b2o$36b3obo$35bo$34b3o$33b\
3o2bo$34bo$35b4o$35bo$37b2o$37b4o$40bo!\
";

// From https://conwaylife.com/ref/DRH/back.forth.html
const char back_and_forth_rle[] = 
"29b2o272b2o$28bobo272bobo$13bobo11bo6b2o262b2o6bo11bobo$8bo4bo2bo10bo\
2bo2bo2bob2o254b2obo2bo2bo2bo10bo2bo4bo$9b2o5b2o9bo6b2o2b2o254b2o2b2o\
6bo9b2o5b2o$4b2o8bo3b2o8bobo272bobo8b2o3bo8b2o$4b2o10b2o11b2o272b2o11b\
2o10b2o$13bo2bo8bo282bo8bo2bo$13bobo10b2o278b2o10bobo$25b2o280b2o6$32b\
obo$33b2o$33bo16b2o230b2o$50bo232bo$48bobo232bobo$48b2o234b2o2$40bo13b\
2o222b2o$41b2o11bo224bo$41bo10bobo224bobo$52b2o226b2o$38bo2bo30bo188bo\
30b2ob2o$39b2o29b3o188b3o28b2ob2o$69bo194bo29bo$69b2o192b2o$32b2o266b\
2o$33b2o264b2o$32bo6b2o252b2o6bo$21b2o16b2o252b2o16b2o$20bo3bo284bo3bo\
$9b2o8bo5bo7bo266bo7bo5bo8b2o$9b2o8bo3bob2o4bobo266bobo4b2obo3bo8b2o$\
19bo5bo3b2o12b2o10bo233b2o12b2o3bo5bo$20bo3bo4b2o12b2o11b2o231b2o12b2o\
4bo3bo$21b2o6b2o24b2o246b2o6b2o$31bobo266bobo$33bo266bo$76bo33bo33bo\
33bo33bo33bo$76bo15b3o15bo15b3o15bo15b3o15bo15b3o15bo15b3o15bo15b3o$\
29bo46bo33bo33bo33bo33bo33bo57bo$27bobo274bobo$17b2o6b2o24b2o254b2o6b\
2o$16bo3bo4b2o12b2o11b2o239b2o12b2o4bo3bo$15bo5bo3b2o12b2o10bo241b2o\
12b2o3bo5bo$5b2o8bo3bob2o4bobo274bobo4b2obo3bo8b2o$5b2o8bo5bo7bo274bo\
7bo5bo8b2o$16bo3bo292bo3bo$17b2o16b2o260b2o16b2o$28bo6b2o260b2o6bo$29b\
2o272b2o$28b2o274b2o2$298bo$35b2o259b2ob2o$34bo2bo258b2ob2o$48b2o234b\
2o$37bo10bobo52bo126bo52bobo$37b2o11bo51bobo124bobo51bo$36bo13b2o49bob\
2o15b2o90b2o15b2obo49b2o$95b2o3b2ob2o14bobo90bobo14b2ob2o3b2o$44b2o49b\
2o4bob2o13bo6b2o80b2o6bo13b2obo4b2o49b2o$44bobo55bobo13bo2bo2bo2bob2o\
72b2obo2bo2bo2bo13bobo14b2o39bobo$46bo56bo5bo8bo6b2o2b2o72b2o2b2o6bo8b\
o5bo14bobo39bo$29bo16b2o61bobo7bobo90bobo7bobo22bo38b2o$29b2o68b2o8b2o\
9b2o90b2o9b2o8b2o$28bobo68b2o132b2o4$102bo$101bo132bo$21b2o78b3o129b2o\
76b2o$9bobo10b2o62b2o158b2o62b2o10bobo$9bo2bo8bo63bobo158bobo63bo8bo2b\
o$2o10b2o11b2o58bo21b2o139bo58b2o11b2o10b2o$2o8bo3b2o8bobo57b2o14b2o5b\
o2bo137b2o57bobo8b2o3bo8b2o$5b2o5b2o9bo6b2o2b2o63bo2bo4bo2bo120b2o65b\
2o2b2o6bo9b2o5b2o$4bo4bo2bo10bo2bo2bo2bob2o54b2o8b2o8bo119bobo9b2o54b\
2obo2bo2bo2bo10bo2bo4bo$9bobo11bo6b2o57bobo140bo9bobo57b2o6bo11bobo$\
24bobo62bo17bo2bo133bo62bobo$25b2o61b2o19bo134b2o61b2o4$223b3o$225bo$\
224bo3$124bobo80bobo$116b3o5bo3bo76bo3bo5b3o$111b2o2bo2bobo7bo5b2o62b\
2o5bo7bobo2bo2b2o$109bo2bo2b2o7bo4bo4b2o62b2o4bo4bo7b2o2bo2bo$100b2o6b\
o19bo76bo19bo6b2o$100b2o6bo10b3o2bo3bo76bo3bo2b3o10bo6b2o$108bo15bobo\
80bobo15bo$109bo2bo108bo2bo$111b2o108b2o!";

// From https://conwaylife.com/patterns/quetzal56.rle
const char quetzal[] = "\
113b2o$112bobo$106b2o4bo$104bo2bo2b2ob4o$104b2obobobobo2bo$107bobobob\
o$107bobob2o$108bo2$121b2o$112b2o7bo$112b2o5bobo$119b2o$44b2o$44bobo\
90b2o$46bo4b2o77bo5bo$42b4ob2o2bo2bo48bo25bo3b2o2bo$42bo2bobobobob2o\
47bo26bo4bo3bo$45bobobobo50b3o10b3o12bobo3bo2bo$46b2obobo57b2o4bo19b\
4o$50bo59bo5bo$107b3o25b4o$36b2o7b3o59bo22bobo3bo2bo$37bo6bo3bo80bo4b\
o3bo$37bobo4bo3bo80bo3b2o2bo$38b2o2b2o5b2o79bo5bo$41bo4bo4bo85b2o$41b\
o3bobo3bo$41bo4bob2obo78bo$42b3o83bo2b2o$89bo38b2o$88bo11b2o$48b2o38b\
3o11bo5bo$48bo52bo2b2o3bo$49b3o48bo3bo4bo24b2o$51bo47bo2bo3bobo25bobo\
$36b2o62b4o32bo$35bobo98b2o$37bo26bo35b4o$62bobo34bo2bo3bobo$63b2o35b\
o3bo4bo$101bo2b2o3bo$102bo5bo$100b2o11bo$75bo7b2o28b2o$74bo7bobo27bob\
o$74b3o5bo$81b2o24b2o$106bo2b2o26bo$81b4o21b2ob2o24bo2bo$22b2o57bo2bo\
22b3o29bo$21bobo41bob2o14bo19b2o25b2o2bo4bo$23bo39b3ob2o9bo5b3o15bobo\
25b2o2bo2b4o$62bo13bobo7bo15bo31bo5bo$63b3ob2o8b2o22b2o32b5o$65bobo8b\
2o41b2o$65bobo8b2o41bobo13b5o$3b2o61bo9b2o41bo14bo5bo$4bo125b2o2bo2b\
4o$2bo127b2o2bo4bo$2b5o14b2o85bo30bo$7bo13bo85bobo25bo2bo$4b3o12bobo\
86bob3o2b2o20bo$3bo9b2o4b2o86b2ob2o3b2o14bo$3b4o6b3o90bo2bo20bobo$b2o\
3bo3b2o2bo92b4o19bo2bo$o2b3o4b2o52b2ob2obo7bo9b2o41b2o$2obo59b2o2b2ob\
2o4b2ob2o7bo18b4o14b2o$3bo67bo3b2ob3o5bobo17bo2bo16b2o$3b2o58b2o10b5o\
6b2o19b2ob2o3b2o8bo9b2o$63b2o5b2o4b3o29bob3o2b2o18bobo$70b2o5bo29bobo\
27bo$11b2o50bo44bo28b2o$12bo50b2ob2o2b2o$9b3o52bob2ob2o23bo2bo2bo14bo\
$9bo81bo2b7o13bobo$91b3o19bo2bo$27b2o66b2obo15b2o$27bobo63b3obobo$27b\
o52b2o10bo3bo2bo20b3o$67b2o22bob2ob2ob2o9b2o8bo$66bo2bo21bobo3bobo9bo\
bo9bo$69bo20b2o2b4obo9bo$69bo16bo5bobobo2b2o7b2o$59b2o5b2obo14b4o4bob\
o3bo$52bo5bo7b2o15bo3bo3b2obo3bo53bo$51bo3b2o2bo23bo2bo6bob2obob2o50b\
2o$51bo4bo3bo23b3o6bo2b3o2bo42b2o$52bobo3bo2bo5b2o23b2obo3bo44b2ob3o\
2bobo$57b4o6b2o23bo2b4o49bo5bo$94bo54b5o$41b2o14b4o12b2o17bo2b4o$41bo\
bo8bobo3bo2bo12bo17b2obo2bo50b5o$41bo9bo4bo3bo13bob2o15bo40b3o11bo5bo\
$51bo3b2o2bo15bo17bobo15b2o3b2o16bo9b2ob3o2bobo$52bo5bo20bo14b2o3b2o\
3b2ob2obo2bo2bo18bo8b2o$59b2o16bo2bo18bobo3bobo3bobo4bo33b2o$78b3o16b\
2o2b4o2bob2obob4obo3b2o27bo$52bo25b3o15bo2b2o3bobobobobobo4bo3bo$51bo\
bo8bo2bo2bo10bo16bobo2bo4bo5bobo2b2ob2obo21bo$51bo2bo7b7o2bo7bo15b2ob\
3o4b2o4b2o2bob2obobo21bobo$52b2o15b3o7bo2bo14bo4bobo5bo2bobo4bo23bo2b\
o$64bob2o12bo2bo8b2o3bob2o3b2o3b3o7b3o23b2o$45b2o16bobob3o10bo3bo12bo\
4bobo5bo2bobo4bo$46b2o8b2o5bo2bo3bo7b3o3bo10b2ob3o4b2o4b2o2bob2obobo\
15bo$45bo10bobo3b2ob2ob2obo11b2o11bobo2bo4bo5bobo2b2ob2obo14b2o9b2o$\
58bo4bobo3bobo4bo3b3o13bo2b2o3bobobobobobo4bo3bo13bobo9bobo$58b2o3bob\
4o2b2o3bobob3o14b2o2b4o2bob2obob4obo3b2o26bo$16bo45b2o2bobobo28bobo3b\
obo3bobo4bo32b2o$15bo2bo45bo3bobo28b2o3b2ob2obo2bo2bo$14bo49bo3bob2o\
39b2o3b2o$14bo4bo2b2o37b2obob2obo$13b4o2bo2b2o37bo2b3o2bo$13bo5bo43bo\
3bob2o$14b5o45b4o2bo26bo$68bo26bobo$14b5o45b4o2bo17b2o6b2o$13bo5bo11b\
2o31bo2bob2o7b3o7bo$13b4o2bo2b2o8b2o35bo8b2o6bobo35bo$14bo4bo2b2o7bo\
13b2o3b2o15bobo9bobo4b2o36b2o$14bo31bo2bo2bob2ob2o3b2o3b2o12bo41bobo$\
15bo2bo25bo4bobo3bobo3bobo18b2o$16bo21b2o3bob4obob2obo2b4o2b2o14bobo$\
22bo16bo3bo4bobobobobobo3b2o2bo13b2o$21bobo15bob2ob2o2bobo5bo4bo2bobo\
$20bo2bo16bobob2obo2b2o4b2o4b3ob2o$21b2o19bo4bobo2bo5bobo4bo$41b3o7b\
3o3b2o3b2obo3b2o$42bo4bobo2bo5bobo4bo45bo$17b2o9b3o9bobob2obo2b2o4b2o\
4b3ob2o41bobo$16bobo9bo10bob2ob2o2bobo5bo4bo2bobo43b2o$16bo12bo9bo3bo\
4bobobobobobo3b2o2bo18b2o34b2o$15b2o21b2o3bob4obob2obo2b4o2b2o19b2o\
33bobo$44bo4bobo3bobo3bobo44b2o10bo$46bo2bo2bob2ob2o3b2o6bobo35b2o2b\
2o5b2obo2bo$45b2o3b2o18b2o22b2o16bobo3bo3b4o$71bo21bobo17bo4b2obo$93b\
o24bo3b4o$84bob2o4b2obo2bo11b2o7b2o5bo$83b5o4bo2b4o12bo8bobob2o2bo$\
83b4o7bo13b3o9bo4bob2o$73b2o17bo2b4o9bo9b2obobobo$74bo9b3o5b2obo3bo\
19bobobobo$42b3o29bobo7b3o6bo2b3o2bo17bobob2ob2o$42bo32b2o8bo7bob2obo\
b2o15b2o2b3o2bo$43bo47b2obo3bo19bobo5bo$92bobo3bo19bob5ob2o$92bobobo\
2b2o18bo4bobo$56bobo31b2o2b4obo20b3o3bo$56b2o33bobo3bobo22bob2o$57bo\
13b2o18bob2ob2ob2o17b3o$72bo19bo3bo2bo18bo2b7o$72bobo18b3obobo21bo2bo\
2bo$43b2o28b3ob2o16b2obo$43bobo29b2ob2o11b3o$45bo10b2o18bo2bo11bo2b7o\
$40bo2bob2o5b2o2b2o17b2ob3o5b2o6bo2bo2bo$40b4o2bo4bobo21b2o3bo5bobo$\
44b2o6bo24bob4o5bo$40b4o2bo30bo3bo6b2o$39bo3bob2o7b2o23b3o$37bo2bo4bo\
8bo$37b2obob2obo9b3o15b2o$40bo3bob2o9bo14bobo$40b2o2bobo25bo$38b2o2bo\
bobo24b2o$39bobo2bo2b2o$39bo5bobo$38b2ob5obo$39bo6bo$39bobo2b2o$40b4o\
48bo$45b3o45b2o$38b7o2bo44b2o$38bo2bo2bo!";

int main() {
  set_sys_clock_khz(216000, true);

  stdio_init_all();

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

  display.init(FRAME_WIDTH, FRAME_HEIGHT, DVDisplay::MODE_PALETTE, FRAME_WIDTH, FRAME_HEIGHT);

    init_palette();
    init_gol(FRAME_WIDTH, FRAME_HEIGHT);
    //setup_from_rle(95, 73, reburn_rle, 15);
    //setup_from_rle(334, 103, back_and_forth_rle);
    setup_from_rle(155, 175, quetzal);

    display_gol();
    display.flip();
    sleep_ms(2000);

    multicore_launch_core1(core1_main);

    while(true) {
        //sleep_ms(500);
        absolute_time_t start_time = get_absolute_time();
        gol_generation();
        absolute_time_t mid_time = get_absolute_time();
        //printf("Computing gen %d took %.2fms\n", gol.gen_count, absolute_time_diff_us(start_time, mid_time) * 0.001f);
        display.wait_for_flip();
        display_gol();
        printf("Gen %d Compute %.2fms, draw %.2fms\n", gol.gen_count, absolute_time_diff_us(start_time, mid_time) * 0.001f, absolute_time_diff_us(mid_time, get_absolute_time()) * 0.001f);
        display.flip_async();
    }
}
