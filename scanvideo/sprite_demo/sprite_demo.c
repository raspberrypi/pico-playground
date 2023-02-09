/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>

#include "pico.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "sprite.h"
#include "raspberry_128x128_bgar5515.h"
#include "raspberry_128x128_bgar5515_flip.h"

#include "hardware/vreg.h"

//#define VGA_MODE vga_mode_320x240_60
#define VGA_MODE vga_mode_640x480_60
#define DUAL_CORE_RENDER
// #define TURBO_BOOST 1
#define N_BERRIES 45

CU_REGISTER_DEBUG_PINS(generation)
CU_SELECT_DEBUG_PINS(generation)

extern const struct scanvideo_pio_program video_24mhz_composable;

// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;

static void frame_update_logic();
static void render_scanline(struct scanvideo_scanline_buffer *dest, int core);

// "Worker thread" for each core
void __time_critical_func(render_loop)() {
    static uint32_t last_frame_num = 0;
    int core_num = get_core_num();
    printf("Rendering on core %d\n", core_num);
    while (true) {
        struct scanvideo_scanline_buffer *scanline_buffer = scanvideo_begin_scanline_generation(true);
        mutex_enter_blocking(&frame_logic_mutex);
        uint32_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        // Note that with multiple cores we may have got here not for the first
        // scanline, however one of the cores will do this logic first before either
        // does the actual generation
        if (frame_num != last_frame_num) {
            last_frame_num = frame_num;
            frame_update_logic();
        }
        mutex_exit(&frame_logic_mutex);

        render_scanline(scanline_buffer, core_num);

        // Release the rendered buffer into the wild
        scanvideo_end_scanline_generation(scanline_buffer);
    }
}

struct semaphore video_setup_complete;

void core1_func() {
    sem_acquire_blocking(&video_setup_complete);
    render_loop();
}

int vga_main(void) {
    mutex_init(&frame_logic_mutex);
    sem_init(&video_setup_complete, 0, 1);

    // Core 1 will wait for us to finish video setup, and then start rendering
#ifdef DUAL_CORE_RENDER
    multicore_launch_core1(core1_func);
#endif

    hard_assert(VGA_MODE.width + 4 <= PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS * 2);
    scanvideo_setup(&VGA_MODE);
    scanvideo_timing_enable(true);

    sem_release(&video_setup_complete);
    render_loop();

}

// Helper functions to:
// - Get a scanbuf into a state where a region of it can be directly rendered to,
//   and return a pointer to this region
// - After rendering, manipulate this scanbuffer into a form where PIO can
//   yeet it out on VGA

static inline uint16_t *raw_scanline_prepare(struct scanvideo_scanline_buffer *dest, uint width) {
    assert(width >= 3);
    assert(width % 2 == 0);
    // +1 for the black pixel at the end, -3 because the program outputs n+3 pixels.
    dest->data[0] = COMPOSABLE_RAW_RUN | (width + 1 - 3 << 16);
    // After user pixels, 1 black pixel then discard remaining FIFO data
    dest->data[width / 2 + 2] = 0x0000u | (COMPOSABLE_EOL_ALIGN << 16);
    dest->data_used = width / 2 + 2;
    assert(dest->data_used <= dest->data_max);
    return (uint16_t *) &dest->data[1];
}

static inline void raw_scanline_finish(struct scanvideo_scanline_buffer *dest) {
    // Need to pivot the first pixel with the count so that PIO can keep up
    // with its 1 pixel per 2 clocks
    uint32_t first = dest->data[0];
    uint32_t second = dest->data[1];
    dest->data[0] = (first & 0x0000ffffu) | ((second & 0x0000ffffu) << 16);
    dest->data[1] = (second & 0xffff0000u) | ((first & 0xffff0000u) >> 16);
    dest->status = SCANLINE_OK;
}

sprite_t berry[N_BERRIES];
int vx[N_BERRIES];
int vy[N_BERRIES];

void __time_critical_func(render_scanline)(struct scanvideo_scanline_buffer *dest, int core) {
    int l = scanvideo_scanline_number(dest->scanline_id);
    uint16_t *colour_buf = raw_scanline_prepare(dest, VGA_MODE.width);

    DEBUG_PINS_SET(generation, (core + 1));
    DEBUG_PINS_SET(generation, 4);
    const uint16_t bgcol = PICO_SCANVIDEO_PIXEL_FROM_RGB8(0x40, 0xc0, 0xff);
    sprite_fill16(colour_buf, bgcol, VGA_MODE.width);
    DEBUG_PINS_CLR(generation, 4);

    for (int i = 0; i < N_BERRIES; ++i)
        sprite_sprite16(colour_buf, &berry[i], l, VGA_MODE.width);

    DEBUG_PINS_CLR(generation, (core + 1));
    raw_scanline_finish(dest);
}

static inline int random_velocity() {
    // Never return 0 -- it makes the demo much less interesting
    return (rand() % 5 + 1) * (rand() & 0x8000 ? 1 : -1);
}

static inline int clip(int x, int min, int max) {
    return x < min ? min : x > max ? max : x;
}

static int xmin;
static int xmax;
static int ymin;
static int ymax;

void __time_critical_func(frame_update_logic)() {
    for (int i = 0; i < N_BERRIES; ++i) {
        berry[i].x += vx[i];
        berry[i].y += vy[i];
        int xclip = clip(berry[i].x, xmin, xmax);
        int yclip = clip(berry[i].y, ymin, ymax);
        if (berry[i].x != xclip || berry[i].y != yclip) {
            berry[i].x = xclip;
            berry[i].y = yclip;
            vx[i] = random_velocity();
            vy[i] = random_velocity();
            berry[i].img = vx[i] < 0 ? raspberry_128x128_flip : raspberry_128x128;
        }
    }
}

int main(void) {
#if TURBO_BOOST
    vreg_set_voltage(VREG_VOLTAGE_MAX);
    sleep_ms(10);
    set_sys_clock_khz(400000, true);
#else
#if PICO_SCANVIDEO_48MHZ
    set_sys_clock_khz(192000, true);
#else
    set_sys_clock_khz(200000, true);
#endif
#endif
    // Re init uart now that clk_peri has changed
    setup_default_uart();

#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, 1);
#endif

    xmin = -100;
    xmax = VGA_MODE.width - 30;
    ymin = -100;
    ymax = VGA_MODE.height - 30;

    for (int i = 0; i < N_BERRIES; ++i) {
        berry[i].x = rand() % (xmax - xmin + 1) - xmin;
        berry[i].y = rand() % (ymax - ymin + 1) - ymin;
        berry[i].img = raspberry_128x128_flip;
        berry[i].log_size = 7;
        berry[i].has_opacity_metadata = true;
        vx[i] = random_velocity();
        vy[i] = random_velocity();
    }

    return vga_main();
}
