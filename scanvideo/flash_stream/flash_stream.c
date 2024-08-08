/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/sync.h"
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/structs/dma.h"
#include "hardware/structs/ssi.h"

// This app must be built with PICO_COPY_TO_RAM=1

#define FLASH_IMAGE_BASE 0x1003c000
#define FLASH_IMAGE_SCANLINE_SIZE (640 * sizeof(uint16_t))
#define FLASH_IMAGE_SIZE (FLASH_IMAGE_SCANLINE_SIZE * 480)
#define FLASH_N_IMAGES 3
#define FRAMES_PER_IMAGE 300

#define VGA_MODE vga_mode_640x480_60
extern const struct scanvideo_pio_program video_24mhz_composable;

static void frame_update_logic();
static void render_scanline(struct scanvideo_scanline_buffer *dest);

int __time_critical_func(render_loop)() {
    static uint32_t last_frame_num = 0;
    while (true) {
        struct scanvideo_scanline_buffer *scanline_buffer = scanvideo_begin_scanline_generation(true);
        uint32_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        if (frame_num != last_frame_num) {
            last_frame_num = frame_num;
            frame_update_logic();
        }
        render_scanline(scanline_buffer);
        scanvideo_end_scanline_generation(scanline_buffer);
    }
}

int vga_main(void) {
    scanvideo_setup(&VGA_MODE);
    scanvideo_timing_enable(true);
    render_loop();
    return 0;
}

const uint16_t *img_base = (const uint16_t *) FLASH_IMAGE_BASE;

void __time_critical_func(frame_update_logic)() {
    static uint slideshow_ctr = 0;
    static uint image_index = 0;
    if (++slideshow_ctr >= FRAMES_PER_IMAGE) {
        slideshow_ctr = 0;
        image_index = (image_index + 1) % FLASH_N_IMAGES;
        img_base = (const uint16_t *) (FLASH_IMAGE_BASE + FLASH_IMAGE_SIZE * image_index);
    }
}

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

// Use direct SSI DMA for maximum transfer speed (but cannot execute from
// flash at the same time)
void __no_inline_not_in_flash_func(flash_bulk_read)(uint32_t *rxbuf, uint32_t flash_offs, size_t len,
                                                 uint dma_chan) {
    ssi_hw->ssienr = 0;
    ssi_hw->ctrlr1 = len - 1; // NDF, number of data frames (32b each)
    ssi_hw->dmacr = SSI_DMACR_TDMAE_BITS | SSI_DMACR_RDMAE_BITS;
    ssi_hw->ssienr = 1;

    dma_hw->ch[dma_chan].read_addr = (uint32_t) &ssi_hw->dr0;
    dma_hw->ch[dma_chan].write_addr = (uint32_t) rxbuf;
    dma_hw->ch[dma_chan].transfer_count = len;
    // Must enable DMA byteswap because non-XIP 32-bit flash transfers are
    // big-endian on SSI (we added a hardware tweak to make XIP sensible)
    dma_hw->ch[dma_chan].ctrl_trig =
            DMA_CH0_CTRL_TRIG_BSWAP_BITS |
            DREQ_XIP_SSIRX << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB |
            dma_chan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB |
            DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS |
            DMA_CH0_CTRL_TRIG_DATA_SIZE_VALUE_SIZE_WORD << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB |
            DMA_CH0_CTRL_TRIG_EN_BITS;

    // Now DMA is waiting, kick off the SSI transfer (mode continuation bits in LSBs)
    ssi_hw->dr0 = (flash_offs << 8) | 0xa0;

    while (dma_hw->ch[dma_chan].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS)
        tight_loop_contents();

    ssi_hw->ssienr = 0;
    ssi_hw->ctrlr1 = 0;
    ssi_hw->dmacr = 0;
    ssi_hw->ssienr = 1;
}

void __time_critical_func(render_scanline)(struct scanvideo_scanline_buffer *dest) {
    int l = scanvideo_scanline_number(dest->scanline_id);
    uint16_t *colour_buf = raw_scanline_prepare(dest, VGA_MODE.width);
    // Just use a random DMA channel which hopefully nobody minds us borrowing
    // "It's easier to seek forgiveness than permission, unless you hardfault"
    flash_bulk_read(
            (uint32_t *) colour_buf,
            (uint32_t) img_base + l * FLASH_IMAGE_SCANLINE_SIZE,
            FLASH_IMAGE_SCANLINE_SIZE / sizeof(uint32_t),
            11
    );
    raw_scanline_finish(dest);
}

int main(void) {
    set_sys_clock_khz(200000, true);
    setup_default_uart();

#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, 1);
#endif

    return vga_main();
}
