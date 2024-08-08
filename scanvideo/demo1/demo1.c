/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <string.h>
#include "pico.h"
#include "hardware/gpio.h"
#include "spans.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/stdlib.h"

#if !PICO_NO_HARDWARE

#include "hardware/clocks.h"

#endif

#include "data.h"
#include <stdio.h>

#if !PICO_NO_HARDWARE

#include "hardware/structs/bus_ctrl.h"

//#define SHOW_PERF_COUNTERS
#endif

CU_REGISTER_DEBUG_PINS(frame_generation)

// ---- select at most one ---
//CU_SELECT_DEBUG_PINS(frame_generation)

typedef bool (*render_scanline_func)(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_test_pattern(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_pi(struct scanvideo_scanline_buffer *dest, int core);

extern const struct scanvideo_pio_program video_24mhz_composable;

//#define vga_mode vga_mode_1080p
//#define vga_mode vga_mode_1024x768_60
//#define vga_mode vga_mode_720p
#define vga_mode vga_mode_640x480_60

//#define vga_mode vga_mode_320x256_60
//#define vga_mode vga_mode_320x240_60
//#define vga_mode vga_mode_213x160_60
//#define vga_mode vga_mode_160x120_60
//#define vga_mode vga_mode_tft_800x480_50
//#define vga_mode vga_mode_tft_400x240_50

// for now we want to see second counter on native and don't need both cores
//#if !PICO_NO_HARDWARE
#define RENDER_ON_CORE0
//#endif
#define RENDER_ON_CORE1

//#define IRQS_ON_CORE1

//render_scanline_func render_scanline = render_scanline_test_pattern;
render_scanline_func render_scanline = render_scanline_pi;
//render_scanline_func render_scanline = render_scanline_tmp;

int vspeed = 1 * 1;
int hspeed = 1 * -1;

#ifdef SHOW_PERF_COUNTERS
#define NUM_PERF_COUNTERS 10
uint32_t perf_count[NUM_PERF_COUNTERS];
uint32_t perf_contention[NUM_PERF_COUNTERS];
uint perf_base;
static uint16_t bar_chart[2][128];
static uint bar_chart_size[2];
#endif
static const int input_pin0 = 22;
// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;
static int left = 0;
static int top = 0;

void init_render_state(int core);

//void __isr isr_siob_proc0() {
//    gpio_put(24, 1);
//}

static bool get_input() {
    return gpio_get(input_pin0) || gpio_get(28);
}

uint16_t *draw_line(uint16_t *buf, uint16_t color, uint len) {
    switch (len) {
        case 0:
            break;
        case 1:
            *buf++ = COMPOSABLE_RAW_1P;
            *buf++ = color;
            break;
        case 2:
            *buf++ = COMPOSABLE_RAW_2P;
            *buf++ = color;
            *buf++ = color;
            break;
        default:
            *buf++ = COMPOSABLE_COLOR_RUN;
            *buf++ = color;
            *buf++ = (len - 3);
            break;
    }
    return buf;
}

uint16_t bar_color(uint a, uint b) {
    int x = a / 0x1000;
    if (x > 0x1f) x = 0x1f;
    return PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x10, 0x1f - x, x);
}

int approx_log2(uint32_t x) {
    int sig = 32 - __builtin_clz(x);
    if (sig < 7) {
        return (sig * 64) + ((x << (7 - sig)) & 63);
    } else {
        return (sig * 64) + ((x >> (sig - 7)) & 63);
    }
}

uint16_t *draw_bar(uint16_t *buf, uint32_t color, uint32_t value, uint32_t maxv, uint bar_width) {
    if (value > maxv) value = maxv;
    uint w = (value * bar_width + maxv - 1) / maxv;
    buf = draw_line(buf, color, w);
    buf = draw_line(buf, PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(2, 2, 2), bar_width - w);
    buf = draw_line(buf, PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(16, 16, 0x16), 4);
    return buf;
}

void __time_critical_func(render_loop)() {
    static uint8_t last_input = 0;
    static uint32_t last_frame_num = 0;
    static int which = 0;
    int core_num = get_core_num();
    assert(core_num >= 0 && core_num < 2);
    printf("Rendering on core %d\n", core_num);
    while (true) {
        struct scanvideo_scanline_buffer *scanline_buffer = scanvideo_begin_scanline_generation(true);
//        if (scanline_buffer->data_used) {
//            // validate the previous scanline to make sure noone corrupted it
//            validate_scanline(scanline_buffer->data, scanline_buffer->data_used, vga_mode.width, vga_mode.width);
//        }
        // do any frame related logic
        mutex_enter_blocking(&frame_logic_mutex);
        uint32_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        // note that with multiple cores we may have got here not for the first scanline, however one of the cores will do this logic first before either does the actual generation
        if (frame_num != last_frame_num) {
#ifdef SHOW_PERF_COUNTERS
            static uint32_t *val_ptr = perf_count;
            static uint32_t *val_ptr2 = perf_count + 2;
            static int rotator = 0;
            for (int i = 0; i < 2; i++) {
                val_ptr[i] = bus_ctrl_hw->counter[i].value;
                val_ptr2[i] = bus_ctrl_hw->counter[i + 2].value;
                bus_ctrl_hw->counter[i].value = 0;
                bus_ctrl_hw->counter[i + 2].value = 0;
            }
            uint r = hw_divider_u32_remainder_inlined(rotator, 5);
            if (r < 2) {
                if (r & 1) {
                    bus_ctrl_hw->counter[0].sel = arbiter_sram0_perf_event_access_contested;
                    bus_ctrl_hw->counter[1].sel = arbiter_sram1_perf_event_access_contested;
                    bus_ctrl_hw->counter[2].sel = arbiter_sram2_perf_event_access_contested;
                    bus_ctrl_hw->counter[3].sel = arbiter_sram3_perf_event_access_contested;
                    val_ptr = perf_contention;
                } else {
                    bus_ctrl_hw->counter[0].sel = arbiter_sram0_perf_event_access;
                    bus_ctrl_hw->counter[1].sel = arbiter_sram1_perf_event_access;
                    bus_ctrl_hw->counter[2].sel = arbiter_sram2_perf_event_access;
                    bus_ctrl_hw->counter[3].sel = arbiter_sram3_perf_event_access;
                    val_ptr = perf_count;
                }
                val_ptr2 = val_ptr + 2;
            } else if (r < 4) {
                if (r & 1) {
                    bus_ctrl_hw->counter[0].sel = arbiter_sram4_perf_event_access_contested;
                    bus_ctrl_hw->counter[1].sel = arbiter_sram5_perf_event_access_contested;
                    bus_ctrl_hw->counter[2].sel = arbiter_rom_perf_event_access_contested;
                    bus_ctrl_hw->counter[3].sel = arbiter_xip_main_perf_event_access_contested;
                    val_ptr = perf_contention + 4;
                } else {
                    bus_ctrl_hw->counter[0].sel = arbiter_sram4_perf_event_access;
                    bus_ctrl_hw->counter[1].sel = arbiter_sram5_perf_event_access;
                    bus_ctrl_hw->counter[2].sel = arbiter_rom_perf_event_access;
                    bus_ctrl_hw->counter[3].sel = arbiter_xip_main_perf_event_access;
                    val_ptr = perf_count + 4;
                }
                val_ptr2 = val_ptr + 2;
            } else {
                bus_ctrl_hw->counter[0].sel = arbiter_apb_perf_event_access_contested;
                bus_ctrl_hw->counter[1].sel = arbiter_fastperi_perf_event_access_contested;
                val_ptr = perf_contention + 8;
                bus_ctrl_hw->counter[2].sel = arbiter_apb_perf_event_access;
                bus_ctrl_hw->counter[3].sel = arbiter_fastperi_perf_event_access;
                val_ptr2 = perf_count + 8;
            }
            for (int i = 0; i < 4; i++) {
                bus_ctrl_hw->counter[i].value = 0;
            }
            rotator++;
#define MAXX 0xc0000
            uint16_t bar_width = hw_divider_u32_quotient(vga_mode.width, (NUM_PERF_COUNTERS + 1)) - 4;
            for (int qq = 0; qq < 2; qq++) {
                int total = 0;
                uint16_t *buf_base = bar_chart[qq];
                uint16_t *buf = buf_base;
                for (int i = 0; i < NUM_PERF_COUNTERS; i++) {
                    if (qq) {
                        total += perf_count[i];
                        buf = draw_bar(buf, 0xfd08, perf_count[i], MAXX, bar_width);
                    } else {
                        total += perf_contention[i];
                        buf = draw_bar(buf, bar_color(perf_contention[i], perf_count[i]), perf_contention[i],
                                       perf_count[i], bar_width);
                    }
                };
                buf = draw_bar(buf, qq ? 0xfd08 : bar_color(total, MAXX), total, MAXX, bar_width);
                *buf++ = COMPOSABLE_RAW_1P;
                *buf++ = 0;
                if (2 & (uintptr_t) buf) {
                    *buf++ = COMPOSABLE_EOL_ALIGN;
                } else {
                    *buf++ = COMPOSABLE_EOL_SKIP_ALIGN;
                    *buf++ = 0xffff;
                }
                bar_chart_size[qq] = (buf - buf_base) / 2;
            }

//            printf("\r%08x %04x %08x %08x %08x %08x %08x %08x %08x : %08x", perf_count[0], approx_log2(perf_count[0]), perf_count[1], perf_count[2], perf_count[3], perf_count[4], perf_count[5], perf_count[6], perf_count[7], total);
//            printf("\r%08x %08x %08x %08x %08x %08x", perf_contention[2], perf_contention[3], perf_contention[4], perf_contention[5], perf_contention[6], perf_contention[7]);
#endif
            // this could should be during vblank as we try to create the next line
            // todo should we ignore if we aren't attempting the next line
            last_frame_num = frame_num;
            if (hspeed > 0) {
                left += hspeed;
                if (left >= vga_mode.width - pi400_image_data.width / 2) {
                    hspeed = -hspeed;
                    left += hspeed;
                }
            } else {
                left += hspeed;
                if (left < -pi400_image_data.width / 2) {
                    hspeed = -hspeed;
                    left += hspeed;
                }
            }
            if (vspeed > 0) {
                top += vspeed;
                if (top >= vga_mode.height - pi400_image_data.height / 2) {
                    vspeed = -vspeed;
                    top += vspeed;
                }
            } else {
                top += vspeed;
                if (top < -pi400_image_data.height / 2) {
                    vspeed = -vspeed;
                    top += vspeed;
                }
            }
            uint8_t new_input = get_input();
            if (last_input && !new_input) {
                if (which) {
                    hspeed++;
                } else {
                    vspeed++;
                }
//left++;
//printf("%d\n", left);
                which = !which;
            }
            last_input = new_input;
        }
        mutex_exit(&frame_logic_mutex);
        DEBUG_PINS_SET(frame_generation, (core_num) ? 2 : 4);
#ifdef SHOW_PERF_COUNTERS
        int l = scanvideo_scanline_number(scanline_buffer->scanline_id);
        if (l < 16 / vga_mode.yscale) {
            int w = l >= (8 / vga_mode.yscale);
            memcpy(scanline_buffer->data, bar_chart[w], bar_chart_size[w] * 4);
            scanline_buffer->data_used = bar_chart_size[w];
        } else {
            render_scanline(scanline_buffer, core_num);
        }
#else
        render_scanline(scanline_buffer, core_num);
#endif

#ifdef SHOW_PERF_COUNTERS
        bool update = l >= 16 / vga_mode.yscale;
        if (!update) {
            static uint32_t blank[] = {
                    COMPOSABLE_RAW_1P | (0 << 16),
                    COMPOSABLE_EOL_SKIP_ALIGN
            };
#if PICO_SCANVIDEO_PLANE_COUNT > 1
            memcpy(scanline_buffer->data2, blank, sizeof(blank));
            scanline_buffer->data2_used = count_of(blank);
#if PICO_SCANVIDEO_PLANE_COUNT > 2
            memcpy(scanline_buffer->data3, blank, sizeof(blank));
            scanline_buffer->data3_used = count_of(blank);
#endif
#endif
        }
#else
        bool update = true;
#endif
        // some colored pixels
        static uint32_t blah[] = {
                COMPOSABLE_COLOR_RUN | (0x0 << 16), /* color */
                /*width-3*/ 0 | (COMPOSABLE_COLOR_RUN << 16),
                0x801f /* color */ | (13 << 16),
                COMPOSABLE_COLOR_RUN | (0x0 << 16), /* color */
                /*width-3*/ 15 | (COMPOSABLE_COLOR_RUN << 16),
                0x83e1 /* color */ | (13 << 16),
                COMPOSABLE_COLOR_RUN |
                ((PICO_SCANVIDEO_ALPHA_MASK | PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0x19)) << 16), /* color */
                /*width-3*/ 5 | (COMPOSABLE_RAW_1P << 16),
                0 | (COMPOSABLE_EOL_ALIGN << 16)
        };
        static uint32_t blah2[] = {
                COMPOSABLE_COLOR_RUN | (0x0000 << 16), /* color */
                /*width-3*/ 0 | (COMPOSABLE_COLOR_RUN << 16),
                0xf01f /* color */ | (13 << 16),
                COMPOSABLE_COLOR_RUN | (0x0 << 16), /* color */
                /*width-3*/ 11 | (COMPOSABLE_COLOR_RUN << 16),
                0xb3e1 /* color */ | (10 << 16),
                COMPOSABLE_COLOR_RUN | (0x81ef << 16), /* color */
                /*width-3*/ 6 | (COMPOSABLE_RAW_1P << 16),
                0 | (COMPOSABLE_EOL_ALIGN << 16)
        };
#if PICO_SCANVIDEO_PLANE_COUNT > 1
        if (update) {
            if (scanline_buffer->data2_used != count_of(blah)) {
                memcpy(scanline_buffer->data2, blah, sizeof(blah));
                scanline_buffer->data2_used = count_of(blah);
            }
            ((uint16_t *) scanline_buffer->data2)[2] = (3 * frame_num / 4) & 255;
        }
#if PICO_SCANVIDEO_PLANE_COUNT > 2
        if (update) {
            if (scanline_buffer->data3_used != count_of(blah2)) {
                memcpy(scanline_buffer->data3, blah2, sizeof(blah2));
                scanline_buffer->data3_used = count_of(blah2);
            }
            ((uint16_t *) scanline_buffer->data3)[2] = 255 - (frame_num & 255);
        }
#endif
#endif
        DEBUG_PINS_CLR(frame_generation, (core_num) ? 2 : 4);
        // release the scanline into the wild
        scanvideo_end_scanline_generation(scanline_buffer);
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
#else
    sem_acquire_blocking(&video_setup_complete);
#endif
#ifdef RENDER_ON_CORE1
    render_loop();
#endif
}

#define TEST_WAIT_FOR_SCANLINE

#ifdef TEST_WAIT_FOR_SCANLINE
volatile uint32_t scanline_color = 0;
#endif

int vga_main(void) {
    mutex_init(&frame_logic_mutex);
//    gpio_debug_pins_init();

    gpio_init(24);
    gpio_init(22);

    // just from this core
    gpio_set_dir_out_masked(0x01380000);
    gpio_set_dir_in_masked(0x00400000);

    // debug pin
    gpio_put(24, 0);

    gpio_set_function(input_pin0, GPIO_FUNC_SIO); // todo is this necessary
    // go for launch (debug pin)
    gpio_put(24, 1);

    sem_init(&video_setup_complete, 0, 1);

    init_render_state(get_core_num());

#ifdef RENDER_ON_CORE1
    init_render_state(1);
#endif
#if defined(RENDER_ON_CORE1) || defined(IRQS_ON_CORE1)
    multicore_launch_core1(core1_func);
#endif
#ifndef IRQS_ON_CORE1
    setup_video();
#else
    sem_acquire_blocking(&video_setup_complete);
#endif
#ifdef RENDER_ON_CORE0
    render_loop();
#else

    while (true) {
#ifndef TEST_WAIT_FOR_SCANLINE
        // Just use vblank to print out a value every second
        static int i=0, s=0;
        vga_wait_for_vblank();
        if (++i == 60) {
            printf("%d\n", s++);
            i = 0;
        }
#else
        static uint32_t sl = 0;
        sl = scanvideo_wait_for_scanline_complete(sl);
        scanline_color = (scanline_color + 1) & 0x1fu;
#endif
    }
#endif
    __builtin_unreachable();
}

struct palette16 *opaque_pi_palette = NULL;

// two copies one for each core
struct span before_span[2];
struct span pi_span[2];
struct span after_span[2];

// must not be called concurrently
void init_render_state(int core) {
    if (!opaque_pi_palette) {
        // one time initialization
        uint32_t back_color = 0xffff8000;
        opaque_pi_palette = blend_palette(&pi_palette, back_color);
        // todo we should of course have a wide solid color span that overlaps
        // todo we can of course also reuse these
        top = (vga_mode.height - pi400_image_data.height) / 2;
        left = (vga_mode.width - pi400_image_data.width) / 2;
        if (top < 0) top = 0;
        if (left < 0) left = 0;

        // set to top right for timing issues
        //left = vga_mode.width - pi400_image_data.width;
        top = -159;
        left = 148;
    }

    // todo we should of course have a wide solid color span that overlaps
    // todo we can of course also reuse these
    init_solid_color_span(&before_span[core], left, opaque_pi_palette->entries[15], NULL);
    init_vogon_4bit_span(&pi_span[core], pi400_image_data.width, NULL, 0, opaque_pi_palette, &before_span[core]);
    init_solid_color_span(&after_span[core], vga_mode.width - left - pi400_image_data.width,
                          opaque_pi_palette->entries[15],
                          &pi_span[core]);
}

bool __time_critical_func(render_scanline_pi)(struct scanvideo_scanline_buffer *dest, int core) {
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;

    int l = scanvideo_scanline_number(dest->scanline_id);
    l -= top;
    int left_offset = left < 0 ? -left : 0;
    int right_offset = left + pi400_image_data.width - vga_mode.width;
    right_offset = right_offset < 0 ? 0 : right_offset;
    before_span[core].width = left + left_offset;
    set_vogon_4bit_clipping(&pi_span[core], left_offset,
                            pi400_image_data.width >= left_offset + right_offset ? pi400_image_data.width -
                                                                                   left_offset - right_offset : 0);
    after_span[core].width = vga_mode.width - left - pi400_image_data.width;
    int length;
    if (l < 0 || l >= pi400_image_data.height) {
        length = single_color_scanline(buf, buf_length, vga_mode.width, opaque_pi_palette->entries[15]);
    } else {
        int sl = l;//&63;
        set_vogon_4bit_span_encoding(&pi_span[core], pi400_image_data.blob.bytes + pi400_image_data.row_offsets[sl],
                                     pi400_image_data.row_offsets[sl + 1] - pi400_image_data.row_offsets[sl]);
        length = render_spans(buf, buf_length, &before_span[core], vga_mode.width);
    }

    if (length > 0) {
        dest->status = SCANLINE_OK;
        dest->data_used = (uint16_t) length;
    } else {
        dest->status = SCANLINE_ERROR;
        dest->data_used = 0;
    }
    return true;
}

// the infamous grey/white triangle
bool render_scanline_test_pattern(struct scanvideo_scanline_buffer *dest, int core) {
    // 1 + line_num red, then white
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;
    int pos = 0;
    int y = scanvideo_scanline_number(dest->scanline_id);
    if (y > vga_mode.width - 10) y = vga_mode.width - 10;
    int yy = vga_mode.height - y;
    if (yy < 4) {
        static uint32_t colors[] = {0xffff, 0x03e0, 0x7c1ef, 0};
        pos = single_color_scanline(buf, buf_length, vga_mode.width, colors[yy]);
    } else {
        int w = MIN(y + 1, vga_mode.width - 6);
        uint32_t red = (((y * 8) / vga_mode.height) & 1) ? 0x001f : 0x021f;
        uint16_t *buf16 = (uint16_t *) buf;
        if (w > 0) {
            if (w == 1) {
                buf16[pos++] = COMPOSABLE_RAW_1P;
            } else if (w == 2) {
                buf16[pos++] = COMPOSABLE_RAW_2P;
                buf16[pos++] = red;
            } else {
                buf16[pos++] = COMPOSABLE_COLOR_RUN;
            }
            buf16[pos++] = red;
            if (w > 2) {
                buf16[pos++] = w - 3;
            }
        }

        w = vga_mode.width - w - 6;
        if (w > 0) {
            buf16[pos++] = COMPOSABLE_RAW_2P;
            buf16[pos++] = 0xfc00;
            buf16[pos++] = 0xffe0;
#ifndef TEST_WAIT_FOR_SCANLINE
            uint16_t c = 0xffff;
#else
            uint16_t c = PICO_SCANVIDEO_PIXEL_FROM_RGB5(1,1,1) * scanline_color;
#endif
            if (w == 1) {
                buf16[pos++] = COMPOSABLE_RAW_1P;
            } else if (w == 2) {
                buf16[pos++] = COMPOSABLE_RAW_2P;
                buf16[pos++] = c;
            } else {
                buf16[pos++] = COMPOSABLE_COLOR_RUN;
            }
            buf16[pos++] = c;
            if (w > 2) {
                buf16[pos++] = w - 3;
            }
        }
        buf16[pos++] = COMPOSABLE_RAW_RUN;
        buf16[pos++] = 0x0000; // p0
        buf16[pos++] = 4 - 3;  // len - 3
        buf16[pos++] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 0x1f); // p1
        buf16[pos++] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x1f, 0, 0); // p2
        buf16[pos++] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0x1f, 0); // p3

        // black for eol
        buf16[pos++] = COMPOSABLE_RAW_1P;
        buf16[pos++] = 0;

        if (pos & 1) {
            buf16[pos++] = COMPOSABLE_EOL_ALIGN;
        } else {
            buf16[pos++] = COMPOSABLE_EOL_SKIP_ALIGN;
            pos++;
        }
        assert(!(pos & 1));
        pos >>= 1;
    }
//    weirdo:
    dest->status = SCANLINE_OK;
    dest->data = buf;
    dest->data_used = (uint16_t) pos;
    return true;
}

int main(void) {

    gpio_put(27, 0);
#if PICO_ON_DEVICE && !PICO_ON_FPGA
#if PICO_SCANVIDEO_48MHZ
    /* set to double frequency 48Mhz for some examples which were written for a higher clock speed */
    set_sys_clock_khz(96000, true);
#endif
    switch (vga_mode.default_timing->clock_freq) {
        case 65000000:
            set_sys_clock_khz(130000, true);
            break;
        default:
            // may cause panic later
            break;
    }
#endif

    // Re init uart now that clk_peri has changed
    setup_default_uart();
#ifdef __riscv
    printf("This is RISCV\n");
#else
    printf("This is ARM\n");
#endif

    return vga_main();
}
