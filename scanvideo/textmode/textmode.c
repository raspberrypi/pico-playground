/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "font.h"

// set this to 3, 4 or 5 for smallest to biggest font
#define FRAGMENT_WORDS 4

#if PICO_ON_DEVICE
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"
#endif

CU_REGISTER_DEBUG_PINS(frame_gen)

//CU_SELECT_DEBUG_PINS(frame_gen)

typedef bool (*render_scanline_func)(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_test_pattern(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_bg(struct scanvideo_scanline_buffer *dest, int core);

#define vga_mode vga_mode_640x480_60
//#define vga_mode vga_mode_320x240_60
//#define vga_mode vga_mode_213x160_60
//#define vga_mode vga_mode_160x120_60
////#define vga_mode vga_mode_tft_800x480_50
//#define vga_mode vga_mode_tft_400x240_50

#define COUNT ((vga_mode.width/8)-1)

// for now we want to see second counter on native and don't need both cores
#if PICO_ON_DEVICE
// todo there is a bug in multithreaded rendering atm
#define RENDER_ON_CORE0
#endif
#define RENDER_ON_CORE1

//#define IRQS_ON_CORE1

render_scanline_func render_scanline = render_scanline_bg;

#define COORD_SHIFT 3
int vspeed = 1 * 1;
int hspeed = 1 << COORD_SHIFT;
int hpos;
int vpos;

static const int input_pin0 = 22;

// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
//auto_init_mutex(frame_logic_mutex);
struct mutex frame_logic_mutex;

static int left = 0;
static int top = 0;
static int x_sprites = 1;

void go_core1(void (*execute)());
void init_render_state(int core);

// ok this is going to be the beginning of retained mode
//


void render_loop() {
    static uint8_t last_input = 0;
    static uint32_t last_frame_num = 0;
    int core_num = get_core_num();
    assert(core_num >= 0 && core_num < 2);
    printf("Rendering on core %d\n", core_num);
#if DEBUG_PINS_ENABLED(frame_gen)
    if (core_num == 1) {
        gpio_init(PICO_DEBUG_PIN_BASE + 1);
        gpio_set_dir_out_masked(2 << PICO_DEBUG_PIN_BASE); // steal debug pin 2 for this core
    }
#endif
    while (true) {
        struct scanvideo_scanline_buffer *scanline_buffer = scanvideo_begin_scanline_generation(true);
//        if (scanline_buffer->data_used) {
//            // validate the previous scanline to make sure noone corrupted it
//            validate_scanline(scanline_buffer->data, scanline_buffer->data_used, vga_mode.width, vga_mode.width);
//        }
        // do any frame related logic
        // todo probably a race condition here ... thread dealing with last line of a frame may end
        // todo up waiting on the next frame...
        mutex_enter_blocking(&frame_logic_mutex);
        uint32_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        // note that with multiple cores we may have got here not for the first scanline, however one of the cores will do this logic first before either does the actual generation
        if (frame_num != last_frame_num) {
            // this could should be during vblank as we try to create the next line
            // todo should we ignore if we aren't attempting the next line
            last_frame_num = frame_num;
            hpos += hspeed;
//            if (hpos < 0) {
//                hpos = 0;
//                hspeed = -hspeed;
//            } else if (hpos >= (level0_map_width*8 - vga_mode.width) << COORD_SHIFT) {
//                hpos = (level0_map_width*8 - vga_mode.width) << COORD_SHIFT;
//                hspeed = -hspeed;
//            }
            uint8_t new_input = gpio_get(input_pin0);
            if ((last_input && !new_input) || getchar_timeout_us(0) == ' ') {
                static int foo = 1;
                foo++;
#if PICO_ON_DEVICE
                bus_ctrl_hw->priority = (foo & 1u) << BUSCTRL_BUS_PRIORITY_DMA_R_LSB;
#endif
                hpos++;
            }
            last_input = new_input;
            static int bar = 1;
#if PICO_ON_DEVICE
//            if (bar >= 800 && bar <= 802)
//                bus_ctrl_hw->priority = (bar&1u) << BUSCTRL_BUS_PRIORITY_DMA_R_LSB;
//            bar++;
#endif
        }
        mutex_exit(&frame_logic_mutex);
        DEBUG_PINS_SET(frame_gen, core_num ? 2 : 4);
        render_scanline(scanline_buffer, core_num);
        DEBUG_PINS_CLR(frame_gen, core_num ? 2 : 4);
#if PICO_SCANVIDEO_PLANE_COUNT > 2
        assert(false);
#endif
        // release the scanline into the wild
        scanvideo_end_scanline_generation(scanline_buffer);
        // do this outside mutex and scanline generation
    }
}

struct semaphore video_setup_complete;

void setup_video() {
    scanvideo_setup(&vga_mode);
    scanvideo_timing_enable(true);
    sem_release(&video_setup_complete);
}

void core1_func() {
#ifdef IRQS_ON_CORE1
    setup_video();
#endif
#ifdef RENDER_ON_CORE1
    render_loop();
#endif
}

#define TEST_WAIT_FOR_SCANLINE

#ifdef TEST_WAIT_FOR_SCANLINE
volatile uint32_t scanline_color = 0;
#endif

uint8_t pad[65536];

#if PICO_ON_DEVICE
uint32_t *font_raw_pixels;
#else
uint32_t font_raw_pixels[16384];
#endif
#define FONT_WIDTH_WORDS FRAGMENT_WORDS
#if FRAGMENT_WORDS == 5
const lv_font_t *font = &ubuntu_mono10;
//const lv_font_t *font = &lcd;
#elif FRAGMENT_WORDS == 4
const lv_font_t *font = &ubuntu_mono8;
#else
const lv_font_t *font = &ubuntu_mono6;
#endif
#define FONT_HEIGHT (font->line_height)
#define FONT_SIZE_WORDS (FONT_HEIGHT * FONT_WIDTH_WORDS)

void build_font() {
    uint16_t colors[16];
    for (int i = 0; i < count_of(colors); i++) {
        colors[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(1, 1, 1) * ((i * 3) / 2);
        if (i) i != 0x8000;
    }
#if PICO_ON_DEVICE
    font_raw_pixels = (uint32_t *) calloc(4, font->dsc->cmaps->range_length * FONT_SIZE_WORDS);
#endif
    uint32_t *p = font_raw_pixels;
    assert(font->line_height == FONT_HEIGHT);
    for (int c = 0; c < font->dsc->cmaps->range_length; c++) {
        // inefficient but simple
        const lv_font_fmt_txt_glyph_dsc_t *g = &font->dsc->glyph_dsc[c + 1];
        const uint8_t *b = font->dsc->glyph_bitmap + g->bitmap_index;
        int bi = 0;
        for (int y = 0; y < FONT_HEIGHT; y++) {
            int ey = y - FONT_HEIGHT + font->base_line + g->ofs_y + g->box_h;
            for (int x = 0; x < FONT_WIDTH_WORDS * 2; x++) {
                uint32_t pixel;
                int ex = x - g->ofs_x;
                if (ex >= 0 && ex < g->box_w && ey >= 0 && ey < g->box_h) {
                    pixel = bi & 1 ? colors[b[bi >> 1] & 0xf] : colors[b[bi >> 1] >> 4];
                    bi++;
                } else {
                    pixel = 0;
                }
//                printf("%d", !!pixel);
//                uint q = 7 - (c%7);
//                pixel =  (q&4)*0x0f00 + (q&2) * 0x0f0 + (q&1)*0x0f;
//                if ((c%16) == 1 || (c%16) == 2) pixel = 0xffff;
                if (!(x & 1)) {
                    *p = pixel;
                } else {
                    *p++ |= pixel << 16;
                }
            }
            if (ey >= 0 && ey < g->box_h) {
                for (int x = FONT_WIDTH_WORDS * 2 - g->ofs_x; x < g->box_w; x++) {
                    bi++;
                }
            }

//            printf("\n");
        }
//        printf("\n");
    }
    printf("%p %p\n", p, font_raw_pixels + font->dsc->cmaps->range_length * FONT_SIZE_WORDS);
}

int video_main(void) {

    mutex_init(&frame_logic_mutex);
    // set 18-22 to RIO for debugging
    for (int i = PICO_DEBUG_PIN_BASE; i < 22; ++i)
        gpio_init(i);

//        gpio_set_function(i, 8);
    gpio_init(24);
    gpio_init(22);
    // just from this core
    gpio_set_dir_out_masked(0x01380000);
    gpio_set_dir_in_masked(0x00400000);

    //gpio_set_function(22, 0);

    // debug pin
    gpio_put(24, 0);

    printf("%d\n", pad[0]);
#if 0
    printf("Press button to start\n");
    // todo NOTE THAT ON STARTUP RIGHT NOW WITH RESET ISSUES ON FPGA, THIS CURRENTLY DOES NOT STOP!!! if you make last_input static, then it never releases instead :-(
    uint8_t last_input = 0;
    while (true) {
        uint8_t new_input = gpio_get(input_pin0);
        if (last_input && !new_input) {
            break;
        }
        last_input = new_input;
        yield();
    }
#endif

    // go for launch (debug pin)
    gpio_put(24, 1);

    build_font();
    sem_init(&video_setup_complete, 0, 1);
#ifndef IRQS_ON_CORE1
    setup_video();
#endif

#if PICO_ON_DEVICE
//    bus_ctrl_hw->priority = 1u << BUSCTRL_BUS_PRIORITY_DMA_R_LSB;
#endif

    init_render_state(0);

#ifdef RENDER_ON_CORE1
    init_render_state(1);
#endif
#if defined(RENDER_ON_CORE1) || defined(IRQS_ON_CORE1)
    go_core1(core1_func);
#endif
#ifdef RENDER_ON_CORE0
    render_loop();
#else

    sem_acquire_blocking(&video_setup_complete);
    while (true) {
#ifndef TEST_WAIT_FOR_SCANLINE
        // Just use vblank to print out a value every second
        static int i=0, s=0;
        video_wait_for_vblank();
        if (++i == 60) {
            printf("%d\n", s++);
            i = 0;
        }
#else
        static uint32_t sl = 0;
        sl = scanvideo_wait_for_scanline_complete(sl);
        scanline_color = (scanline_color + 0x10u) & 0xffu;
#endif
    }
#endif
    __builtin_unreachable();
}

//struct palette16 *opaque_pi_palette = NULL;

// must not be called concurrently
void init_render_state(int core) {

    // todo we should of course have a wide solid color span that overlaps
    // todo we can of course also reuse these
}

#if PICO_SCANVIDEO_PLANE1_FRAGMENT_DMA
static __not_in_flash("x") uint16_t beginning_of_line[] = {
        // todo we need to be able to shift scanline to absorb these extra pixels
#if FRAGMENT_WORDS == 5
        COMPOSABLE_RAW_1P, 0,
#endif
#if FRAGMENT_WORDS >= 4
        COMPOSABLE_RAW_1P, 0,
#endif
        COMPOSABLE_RAW_1P, 0,
        // main run, 2 more black pixels
        COMPOSABLE_RAW_RUN, 0,
        0/*COUNT * 2 * FRAGMENT_WORDS -3 + 2*/, 0
};
static __not_in_flash("y") uint16_t end_of_line[] = {
#if FRAGMENT_WORDS == 5 || FRAGMENT_WORDS == 3
        COMPOSABLE_RAW_1P, 0,
#endif
#if FRAGMENT_WORDS == 3
        COMPOSABLE_RAW_1P, 0,
#endif
#if FRAGMENT_WORDS >= 4
        COMPOSABLE_RAW_2P, 0,
        0, COMPOSABLE_RAW_1P_SKIP_ALIGN,
        0, 0,
#endif
        COMPOSABLE_EOL_SKIP_ALIGN, 0xffff // eye catcher
};
#endif

bool render_scanline_bg(struct scanvideo_scanline_buffer *dest, int core) {
    // 1 + line_num red, then white
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;
    int y = scanvideo_scanline_number(dest->scanline_id) + vpos;
    int x = hpos;
    //y = (y + frame_number(dest->scanline_id)) % (level0_map_height * 8);
#if !PICO_SCANVIDEO_PLANE1_FRAGMENT_DMA
    uint16_t *output = (uint16_t*)buf;
    // raw run has an inline first pixel, so we need to handle that here
    // todo shift the video mode over by a pixel to account for this
    *output++ = COMPOSABLE_RAW_RUN;
    *output++ = 0;
#undef COUNT
#define COUNT 10
//(320/FRAGMENT_WORDS)
    *output++ = 2 + COUNT * 2 * FRAGMENT_WORDS - 3;
    *output++ = 0;
    uint32_t *dbase = font_raw_pixels + FONT_WIDTH_WORDS * (y % FONT_HEIGHT);
    for(int i=0;i<COUNT;i++) {
        int ch = 33 + i;
        uint32_t *data = (uint16_t *)(dbase + ch * FONT_HEIGHT * FONT_WIDTH_WORDS);
        for(int j=0;j<FRAGMENT_WORDS;j++) {
            *((uint32_t*)output) = *data++;
            output += 2;
        }
    }
    // todo fix so we don't need whole scanline

    *output++ = COMPOSABLE_COLOR_RUN;
    *output++ = 0;
    *output++ = vga_mode.width - COUNT * 2 * FRAGMENT_WORDS - 2 - 3;

    // end of line stuff
    *output++ = COMPOSABLE_RAW_1P;
    *output++ = 0;
    if (2 & (intptr_t)output) {
        // we are unaligned
        *output++ = COMPOSABLE_EOL_ALIGN;
    } else {
        *output++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *output++ = 0xffff; // eye catcher
    }
    assert(0 == (3u & (intptr_t)output));
    assert((uint32_t*)output <= (buf + dest->data_max));
    dest->data_used = (uint16_t)(((uint32_t*)output) - buf);
#else
    // we handle both ends separately
//    static const uint32_t end_of_line[] = {
//            COMPOSABLE_RAW_1P | (0u<<16),
//            COMPOSABLE_EOL_SKIP_ALIGN | (0xffff << 16) // eye catcher ffff
//    };
#undef COUNT
    // todo for SOME REASON, 80 is the max we can do without starting to really get bus delays (even with priority)... not sure how this could be
    // todo actually it seems it can work, it just mostly starts incorrectly synced!?
#if PICO_RP2040
#define COUNT MIN(vga_mode.width/(FRAGMENT_WORDS*2)-1, 80)//MAX_SCANLINE_BUFFER_WORDS / 2 - 2)
#else
#define COUNT MIN(vga_mode.width/(FRAGMENT_WORDS*2)-1, (PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS / 2 - 2))
#endif
//#undef COUNT
//#define COUNT 79
    // we need to take up 5 words, since we have fixed width
#if PICO_SCANVIDEO_PLANE1_FIXED_FRAGMENT_DMA
    dest->fragment_words = FRAGMENT_WORDS;
#endif
    beginning_of_line[FRAGMENT_WORDS * 2 - 2] = COUNT * 2 * FRAGMENT_WORDS - 3 + 2;
    assert(FRAGMENT_WORDS * 2 == count_of(beginning_of_line));
    assert(FRAGMENT_WORDS * 2 == count_of(end_of_line));

    uint32_t *output32 = buf;
#if PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
    *output32++ = FRAGMENT_WORDS;
#endif
    *output32++ = host_safe_hw_ptr(beginning_of_line);
    uint32_t *dbase = font_raw_pixels + FONT_WIDTH_WORDS * (y % FONT_HEIGHT);
    int cmax = font->dsc->cmaps[0].range_length;
    int ch = 0;
//    __breakpoint();
    for (int i = 0; i < COUNT; i++) {
        ch++;
        if (ch == cmax) ch = 1;
#if PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
        *output32++ = FRAGMENT_WORDS;
#endif
        *output32++ = host_safe_hw_ptr(dbase + ch * FONT_HEIGHT * FONT_WIDTH_WORDS);
    }
#if PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
    *output32++ = FRAGMENT_WORDS;
#endif
    *output32++ = host_safe_hw_ptr(end_of_line);
    *output32++ = 0; // end of chain
#if PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
    *output32++ = 0; // end of chain
#endif
    assert(0 == (3u & (intptr_t) output32));
    assert((uint32_t *) output32 <= (buf + dest->data_max));

    dest->data_used = (uint16_t) (output32 -
                                  buf); // todo we don't want to include the off the end data in the "size" for the dma
#endif
// why was this here, it is buf anyway!
//    dest->data = buf;

#if PICO_SCANVIDEO_PLANE_COUNT > 1
#if !PICO_SCANVIDEO_PLANE2_VARIABLE_FRAGMENT_DMA
    assert(false);
#endif
    buf = dest->data2;
    output32 = buf;

    uint32_t *inline_data = output32 + PICO_SCANVIDEO_MAX_SCANLINE_BUFFER2_WORDS / 2;
    output = (uint16_t *)inline_data;

    uint32_t *base = (uint32_t *)output;

#define MAKE_SEGMENT \
    assert(0 == (3u & (intptr_t)output)); \
    *output32++ = (uint32_t*)output - base; \
    *output32++ = host_safe_hw_ptr(base); \
    base = (uint32_t *)output;

    int wibble = (frame_number(dest->scanline_id)>>2)%7;
    for(int q = 0; q < x_sprites; q++) {
        // nice if we can do two black pixel before
        *output++ = COMPOSABLE_RAW_RUN;
        *output++ = 0;
        *output++ = galaga_tile_data.width + 2 - 3;
        *output++ = 0;
        MAKE_SEGMENT;

        span_offsets = galaga_tile_data.span_offsets + (q+wibble) * galaga_tile_data.height + (y - vpos);//(y%galaga_tile_data.count 7u);
        off = span_offsets[0];
        data = (uint16_t *) (galaga_tile_data.blob.bytes + off);

        *output32++ = galaga_tile_data.width / 2;
        *output32++ = host_safe_hw_ptr(data);
    }
    *output++ = COMPOSABLE_RAW_1P;
    *output++ = 0;
    *output++ = COMPOSABLE_EOL_SKIP_ALIGN;
    *output++ = 0xffff;
    MAKE_SEGMENT;

    // end of dma chain
    *output32++ = 0;
    *output32++ = 0;

    assert(output32 < inline_data);
    assert((uint32_t*)output <= (buf + dest->data2_max));
    dest->data2_used = (uint16_t)(output32 - buf); // todo we don't want to include the inline data in the "size" for the dma
#endif
    dest->status = SCANLINE_OK;
    return true;
}

void go_core1(void (*execute)()) {
    multicore_launch_core1(execute);
}

int main(void) {
#if PICO_SCANVIDEO_48MHZ
    set_sys_clock_48mhz();
#endif
    gpio_put(27, 0);

    setup_default_uart();

#if !PICO_ON_DEVICE
    //#include <math.h>
    //    for(int i = 0; i<64;i++) {
    //        printf("%d, ", (int)(0x7f*cos(i*M_PI/32)));
    //    }
    //    printf("\n");
#endif

    return video_main();
}