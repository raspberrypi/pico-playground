/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <stdio.h>
#include "pico.h"
#include "pico/scanvideo.h"
#include "pico/sync.h"
#include "pico/scanvideo/composable_scanline.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#if PICO_ON_DEVICE

#include "hardware/clocks.h"
#include "hardware/vreg.h"

#endif

CU_REGISTER_DEBUG_PINS(generation)

#define vga_mode vga_mode_320x240_60

#if PICO_RISCV
// seems to be an issue with IRQ timing so moving onto core 1 for now
#define ALARM_POOL_ON_CORE1 1
#endif

#ifndef USE_FLOAT
#if PICO_RP2350 && !PICO_RISCV
#define USE_FLOAT 1 // fastest
#endif
#endif
//#define USE_FLOAT 1

#if USE_FLOAT
typedef float fixed;
//typedef double fixed;
static inline fixed float_to_fixed(float x) {
    return x;
}
static inline fixed fixed_mult(fixed a, fixed b) {
    return a*b;
}
static inline fixed double_to_fixed(double x) {
    return (fixed)x;
}
#else
#define FRAC_BITS 25u
typedef int32_t fixed;

static inline fixed float_to_fixed(float x) {
    return (fixed) (x * (float) (1u << FRAC_BITS));
}

static inline fixed double_to_fixed(double x) {
    return (fixed) (x * (double) (1u << FRAC_BITS));
}

#if !PICO_ON_DEVICE || (FRAC_BITS != 25) || defined(__riscv)
static inline fixed fixed_mult(fixed a, fixed b) {
    int64_t r = ((int64_t) a) * b;
    return (int32_t) (r >> FRAC_BITS);
}
#else
// Since we're trying to go fast, do a better multiply of 32x32 preserving the bits we want
static inline fixed fixed_mult(fixed a, fixed b) {
#if __ARM_ARCH_6M__
    uint32_t tmp1, tmp2, tmp3;
    __asm__ volatile (
    ".syntax unified\n"
    "asrs   %[r_tmp1], %[r_b], #16 \n" // r_tmp1 = BH
    "uxth   %[r_tmp2], %[r_a]      \n" // r_tmp2 = AL
    "muls   %[r_tmp2], %[r_tmp1]   \n" // r_tmp2 = BH * AL
    "asrs   %[r_tmp3], %[r_a], #16 \n" // r_tmp3 = AH
    "muls   %[r_tmp1], %[r_tmp3]   \n" // r_tmp1 = BH * AH
    "uxth   %[r_b], %[r_b]         \n" // r_b = BL
    "uxth   %[r_a], %[r_a]         \n" // r_a = AL
    "muls   %[r_a], %[r_b]         \n" // r_a = AL * BL
    "muls   %[r_b], %[r_tmp3]      \n" // r_b = BL * AH
    "add    %[r_b], %[r_tmp2]      \n" // r_b = BL * AH + BH * AL
    "lsls   %[r_tmp1], #32 - 25    \n" // r_tmp1 = (BH * AH) >> (32 - FRAC_BITS)
    "lsrs   %[r_a], #16            \n" // r_a = (AL & BL) H
    "add    %[r_a], %[r_b]         \n"
    "asrs   %[r_a], #25- 16        \n" // r_a = (BL * AH + BH * AL) H | (AL & BL) H >> (32 - FRAC_BITS)
    "add    %[r_a], %[r_tmp1]      \n"
    : [r_a] "+l" (a), [r_b] "+l" (b), [r_tmp1] "=&l" (tmp1), [r_tmp2] "=&l" (tmp2), [r_tmp3] "=&l" (tmp3)
    :
    );
    return a;
#else
    return (fixed) ((((uint64_t)a) * b) >> FRAC_BITS);
#endif
}
#endif

#endif

#define max_iters 127//255

struct mutex frame_logic_mutex;
static void frame_update_logic();

void fill_scanline_buffer(struct scanvideo_scanline_buffer *buffer);
static uint y;
static fixed x0, y0;
static fixed dx0_dx, dy0_dy;
static fixed max;
static bool params_ready;

static uint16_t framebuffer[320 * 240];
//static uint16_t *framebuffer;

static uint16_t colors[16] = {
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(66, 30, 15),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(25, 7, 26),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(9, 1, 47),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(4, 4, 73),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, 7, 100),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(12, 44, 138),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(24, 82, 177),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(57, 125, 209),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(134, 181, 229),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(211, 236, 248),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(241, 233, 191),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(248, 201, 95),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, 170, 0),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(204, 128, 0),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(153, 87, 0),
        PICO_SCANVIDEO_PIXEL_FROM_RGB8(106, 52, 3),
};

static void scanline(uint16_t *line_buffer, uint length, fixed mx, fixed my, fixed dmx_dx) {
    for (int x = 0; x < length; ++x) {
        int iters;
        fixed cr = mx;
        fixed ci = my;
        fixed zr = cr;
        fixed zi = ci;
        fixed xold = 0;
        fixed yold = 0;
        int period = 0;
        for (iters = 0; iters < max_iters; ++iters) {
            fixed zr2 = fixed_mult(zr, zr);
            fixed zi2 = fixed_mult(zi, zi);
            if (zr2 + zi2 > max) {
                break;
            }
            fixed zrtemp = zr2 - zi2 + cr;
            zi = 2 * fixed_mult(zr, zi) + ci;
            zr = zrtemp;
            if (zr == xold && zi == yold) {
                iters = max_iters + 1;
                break;
            }
            if (++period > 20) {
                period = 0;
                xold = zr;
                yold = zi;
            }
        }
        if (iters == max_iters + 1) {
            line_buffer[x] = 0;//x1f;
        } else {
            line_buffer[x] = iters == max_iters ? 0 : colors[iters & 15u];
        }
        mx += dmx_dx;
    }
}

// "Worker thread" for each core
void __time_critical_func(render_loop)() {
    static uint32_t last_frame_num = 0;
    int core_num = get_core_num();
    printf("Rendering on core %d\n", core_num);
    while (true) {
        mutex_enter_blocking(&frame_logic_mutex);
        if (y == vga_mode.height) {
            params_ready = false;
            frame_update_logic();
            y = 0;
        }
        uint _y = y++;
        fixed _x0 = x0, _y0 = y0;
        fixed _dx0_dx = dx0_dx, _dy0_dy = dy0_dy;
        mutex_exit(&frame_logic_mutex);

        scanline(framebuffer + _y * 320, 320, _x0, _y0 + _dy0_dy * _y, _dx0_dx);
#if !PICO_ON_DEVICE
        struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation(true);
        fill_scanline_buffer(buffer);
        scanvideo_end_scanline_generation(buffer);
#endif
    }
}

int64_t timer_callback(alarm_id_t alarm_id, void *user_data) {
    struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation(false);
    while (buffer) {
        fill_scanline_buffer(buffer);
        scanvideo_end_scanline_generation(buffer);
        buffer = scanvideo_begin_scanline_generation(false);
    }
    return 100;
}

void fill_scanline_buffer(struct scanvideo_scanline_buffer *buffer) {
    static uint32_t postamble[] = {
            0x0000u | (COMPOSABLE_EOL_ALIGN << 16)
    };

    buffer->data[0] = 4;
    buffer->data[1] = host_safe_hw_ptr(buffer->data + 8);
    buffer->data[2] = 158; // first four pixels are handled separately
    uint16_t *pixels = framebuffer + scanvideo_scanline_number(buffer->scanline_id) * 320;
    buffer->data[3] = host_safe_hw_ptr(pixels + 4);
    buffer->data[4] = count_of(postamble);
    buffer->data[5] = host_safe_hw_ptr(postamble);
    buffer->data[6] = 0;
    buffer->data[7] = 0;
    buffer->data_used = 8;

    // 3 pixel run followed by main run, consuming the first 4 pixels
    buffer->data[8] = (pixels[0] << 16u) | COMPOSABLE_RAW_RUN;
    buffer->data[9] = (pixels[1] << 16u) | 0;
    buffer->data[10] = (COMPOSABLE_RAW_RUN << 16u) | pixels[2];
    buffer->data[11] = ((317 + 1 - 3) << 16u) | pixels[3]; // note we add one for the black pixel at the end
}

struct semaphore video_setup_complete;

void core1_func() {
    sem_acquire_blocking(&video_setup_complete);
    printf("CORE 1 go\n");
#if ALARM_POOL_ON_CORE1
#if PICO_ON_DEVICE

    alarm_pool_add_alarm_in_us(alarm_pool_create(0, 3), 100, timer_callback, NULL, true);
#endif
#endif

    render_loop();
}

int vga_main(void) {
//    framebuffer = calloc(320*240, sizeof(uint16_t));
    mutex_init(&frame_logic_mutex);
    sem_init(&video_setup_complete, 0, 1);

    // Core 1 will wait for us to finish video setup, and then start rendering
    multicore_launch_core1(core1_func);

    hard_assert(vga_mode.width + 4 <= PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS * 2);
    scanvideo_setup(&vga_mode);
    scanvideo_timing_enable(true);

    frame_update_logic();
    sem_release(&video_setup_complete);

#if !ALARM_POOL_ON_CORE1
#if PICO_ON_DEVICE
    add_alarm_in_us(100, timer_callback, NULL, true);
#endif
#endif
    render_loop();
    return 0;
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

void __time_critical_func(frame_update_logic)() {
    if (!params_ready) {
        double scale = vga_mode.height / 2;
        static int foo;
        double offx = (MIN(foo, 196.7)) / 500.0;
        double offy = -(MIN(foo, 229)) / 250.0;
        scale *= (10000 + (foo++) * (double) foo) / 10000.0;
        x0 = double_to_fixed(offx + (-vga_mode.width / 2) / scale - 0.5);
        y0 = double_to_fixed(offy + (-vga_mode.height / 2) / scale);
        dx0_dx = double_to_fixed(1.0f / scale);
        dy0_dy = double_to_fixed(1.0f / scale);
        max = double_to_fixed(4.f);
        params_ready = true;
    }
}

int main(void) {
    uint base_freq;
#if PICO_SCANVIDEO_48MHZ
    base_freq = 48000;
#else
    base_freq = 50000;
#endif
#if PICO_ON_DEVICE
#if TURBO_BOOST
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(base_freq*6, true);
#else
    set_sys_clock_khz(base_freq * 3, true);
#endif
#endif
    // Re init uart now that clk_peri has changed
    setup_default_uart();
//    gpio_debug_pins_init();

    return vga_main();
}
