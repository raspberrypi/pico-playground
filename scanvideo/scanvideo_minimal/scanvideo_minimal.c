/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/stdlib.h"
#if PICO_ON_DEVICE
#include "hardware/clocks.h"
#endif

#define DUAL_CORE_RENDER
#define VGA_MODE vga_mode_320x240_60
extern const struct scanvideo_pio_program video_24mhz_composable;

// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;

static void frame_update_logic();
static void render_scanline(struct scanvideo_scanline_buffer *dest, int core);

// "Worker thread" for each core
void render_loop() {
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

    scanvideo_setup(&VGA_MODE);
    scanvideo_timing_enable(true);

    sem_release(&video_setup_complete);
    render_loop();
    return 0;
}

void frame_update_logic() {

}

#define MIN_COLOR_RUN 3

int32_t single_color_scanline(uint32_t *buf, size_t buf_length, int width, uint32_t color16) {
    assert(buf_length >= 2);

    assert(width >= MIN_COLOR_RUN);
    // | jmp color_run | color | count-3 |  buf[0] =
    buf[0] = COMPOSABLE_COLOR_RUN | (color16 << 16);
    buf[1] = (width - MIN_COLOR_RUN) | (COMPOSABLE_RAW_1P << 16);
    // note we must end with a black pixel
    buf[2] = 0 | (COMPOSABLE_EOL_ALIGN << 16);

    return 3;
}

void render_scanline(struct scanvideo_scanline_buffer *dest, int core) {
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;

    int l = scanvideo_scanline_number(dest->scanline_id);
    uint16_t bgcolour = (uint16_t) l << 2;
    dest->data_used = single_color_scanline(buf, buf_length, VGA_MODE.width, bgcolour);
    dest->status = SCANLINE_OK;
}


int main(void) {
#if PICO_SCANVIDEO_48MHZ
    set_sys_clock_48mhz();
#endif
    // Re init uart now that clk_peri has changed
    setup_default_uart();

    return vga_main();
}
