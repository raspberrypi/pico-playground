/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <string.h>
#include "pico.h"
#include "hardware/gpio.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/stdlib.h"
#include "spans.h"

#ifdef __riscv
#define RISCV_FRONT 1
#endif

#if !PICO_NO_HARDWARE
#include "hardware/clocks.h"
#endif

#include "data2.h"
#include <stdio.h>

#if PICO_ON_DEVICE
static __used uint8_t heap_eater[250000]; // put the scanline buffers in non code banks
#endif

CU_REGISTER_DEBUG_PINS(frame_generation)

// ---- select at most one ---
//CU_SELECT_DEBUG_PINS(frame_generation)

typedef bool (*render_scanline_func)(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_pi(struct scanvideo_scanline_buffer *dest, int core);

extern const struct scanvideo_pio_program video_24mhz_composable;

//#define vga_mode vga_mode_1080p_60
//#define vga_mode vga_mode_1440p_60
//#define vga_mode vga_mode_1024x768_60
//#define vga_mode vga_mode_720p
#define vga_mode vga_mode_640x480_60
//#define vga_mode vga_mode_320x240_60

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
#if PICO_ON_DEVICE
#define RENDER_ON_CORE1
#endif

//#define IRQS_ON_CORE1

//render_scanline_func render_scanline = render_scanline_test_pattern;
render_scanline_func render_scanline = render_scanline_pi;
//render_scanline_func render_scanline = render_scanline_tmp;

int vspeed[2] = { 1 * 1, 2 * 1};
int hspeed[2] = { 4 * -1, 1 * 1};

static const int input_pin0 = 22;
// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;
static int left[2];
static int top[2];
#if 0
#define printf(xxx...) ((void)0)
#endif
#ifdef RISCV_FRONT
static const struct image_data* images[2] = {&pi400_image_data, &riscv_image_data};
#else
static const struct image_data* images[2] = {&riscv_image_data, &pi400_image_data};
#endif
void init_render_state(int core);

//void __isr isr_siob_proc0() {
//    gpio_put(24, 1);
//}

static bool get_input() {
#if PICO_ON_DEVICE
    return getchar_timeout_us(0) > 0;
#else
    return gpio_get(input_pin0) || gpio_get(28);
#endif
}

static inline uint16_t *draw_line(uint16_t *buf, uint16_t color, int len) {
    switch (len) {
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
            if (len > 2) {
                *buf++ = COMPOSABLE_COLOR_RUN;
                *buf++ = color;
                *buf++ = (len - 3);
            }
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

static int selecto;

#define accessctrl_hw ((accessctrl_hw_t *)ACCESSCTRL_BASE)
void __time_critical_func(render_loop)() {
    static uint8_t last_input = 0;
    static uint32_t last_frame_num = 0;
    static int which = 0;
    int core_num = get_core_num();
    assert(core_num >= 0 && core_num < 2);

    printf("Rendering on thae core %d\n", core_num);
    while (true) {
        static int foo;
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
            // this could should be during vblank as we try to create the next line
            // todo should we ignore if we aren't attempting the next line
            last_frame_num = frame_num;
            for(int logo=0; logo < 2; logo++) {
                if (hspeed[logo] > 0) {
                    left[logo] += hspeed[logo];
                    if (left[logo] >= vga_mode.width - images[logo]->width / 2) {
                        hspeed[logo] = -hspeed[logo];
                        left[logo] += hspeed[logo];
                    }
                } else {
                    left[logo] += hspeed[logo];
                    if (left[logo] < -images[logo]->width / 2) {
                        hspeed[logo] = -hspeed[logo];
                        left[logo] += hspeed[logo];
                    }
                }
                if (vspeed[logo] > 0) {
                    top[logo] += vspeed[logo];
                    if (top[logo] >= vga_mode.height - images[logo]->height / 2) {
                        vspeed[logo] = -vspeed[logo];
                        top[logo] += vspeed[logo];
                    }
                } else {
                    top[logo] += vspeed[logo];
                    if (top[logo] < -images[logo]->height / 2) {
                        vspeed[logo] = -vspeed[logo];
                        top[logo] += vspeed[logo];
                    }
                }
            }
            uint8_t new_input = get_input();
            if (last_input && !new_input) {
#if 1
                selecto ^= 1;
#else
                if (which) {
                    hspeed[0]++;
                } else {
                    vspeed[0]++;
                }
#endif
//left++;
//printf("%d\n", left);
                which = !which;
            }
            last_input = new_input;
        }
        mutex_exit(&frame_logic_mutex);
        DEBUG_PINS_SET(frame_generation, (core_num) ? 2 : 4);
        render_scanline(scanline_buffer, core_num);

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

int vga_main(void) {
#if PICO_ON_DEVICE
    heap_eater[0] = 1;
#endif
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
        static uint32_t sl = 0;
        sl = scanvideo_wait_for_scanline_complete(sl);
        scanline_color = (scanline_color + 1) & 0x1fu;
    }
#endif
    __builtin_unreachable();
}

struct palette16 *opaque_palettes[2];
uint16_t pal2[256];

void blend_palette2() {
    uint32_t back_color = 0xffff8000;
    uint32_t __unused ba = (back_color >> 24) & 0xff;
    uint32_t bb = (back_color >> 16) & 0xff;
    uint32_t bg = (back_color >> 8) & 0xff;
    uint32_t br = (back_color >> 0) & 0xff;
    assert(ba == 255); // expect to be on an opaque color
    for (int i0 = 0; i0 < 16; i0++) {
        for (int i1 = 0; i1 < 16; i1++) {
#ifdef RISCV_FRONT
            uint32_t fore_color1 = pi_palette.entries[i0];
            uint32_t fore_color2 = riscv_palette.entries[i1];
#else
            uint32_t fore_color1 = riscv_palette.entries[i0];
            uint32_t fore_color2 = pi_palette.entries[i1];
#endif
            uint32_t fa1 = (fore_color1 >> 24) & 0xff;
            uint32_t fb1 = (fore_color1 >> 16) & 0xff;
            uint32_t fg1 = (fore_color1 >> 8) & 0xff;
            uint32_t fr1 = (fore_color1 >> 0) & 0xff;
            uint32_t fa2 = (fore_color2 >> 24) & 0xff;
            uint32_t fb2 = (fore_color2 >> 16) & 0xff;
            uint32_t fg2 = (fore_color2 >> 8) & 0xff;
            uint32_t fr2 = (fore_color2 >> 0) & 0xff;
            int fa = MAX(fa1, fa2);
            int fr=0, fg=0, fb=0;
            if (fa) {
                fr = (fr1 * fa1 + fr2 * fa2 * 3) / (fa1 + fa2*3);
                fg = (fg1 * fa1 + fg2 * fa2 * 3) / (fa1 + fa2*3);
                fb = (fb1 * fa1 + fb2 * fa2 * 3) / (fa1 + fa2*3);
            }
            if (fa == 255) fa = 256;
            fb = (fa * fb + (256 - fa) * bb) >> 11;
            fg = (fa * fg + (256 - fa) * bg) >> 11;
            fr = (fa * fr + (256 - fa) * br) >> 11;
            pal2[i1*16 + i0] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(fr, fg, fb);
        }
    }
}
// must not be called concurrently
void init_render_state(int core) {
    if (!opaque_palettes[0]) {
        // one time initialization
        uint32_t back_color = 0xffff8000;
#ifdef RISCV_FRONT
        opaque_palettes[0] = blend_palette(&pi_palette, back_color);
        opaque_palettes[1] = blend_palette(&riscv_palette, back_color);
#else
        opaque_palettes[1] = blend_palette(&pi_palette, back_color);
        opaque_palettes[0] = blend_palette(&riscv_palette, back_color);
#endif
        blend_palette2();
        // todo we should of course have a wide solid color span that overlaps
        // todo we can of course also reuse these
#if !FOOPLE
        top[0] = (vga_mode.height - images[0]->height) / 2;
        left[0] = (vga_mode.width - images[0]->width) / 2;
        if (top[0] < 0) top[0] = 0;
        if (left[0] < 0) left[0] = 0;
#endif

        // set to top right for timing issues
        //left = vga_mode.width - pi400_image_data.width;
//        top[0] = -159;
//        left[0] = 148;
    }
}

#define MIN_COLOR_RUN 3

static inline uint16_t *draw_single(uint16_t *dest, int logo, int l, int skipL, int maxR) {
    const struct image_data *img = images[logo];
    const uint16_t *pal = opaque_palettes[logo]->entries;
    const uint8_t *z = img->blob.bytes + img->row_offsets[l];
    const uint8_t *zend = img->blob.bytes + img->row_offsets[l+1];

    int x = -skipL;
    maxR -= skipL;
    if (x < 0) {
        while (z < zend) {
            int c = *z++;
            int len = c < 16 ? *z++ : (c>>4);
            x += len;
            if (x >= 0) {
                if (x >= maxR) {
                    dest = draw_line(dest, pal[c & 0xf], maxR);
                    return dest;
                } else {
                    dest = draw_line(dest, pal[c & 0xf], x);
                    break;
                }
            }
        }
    }
    while (z<zend) {
        int c = *z++;
        int len = c < 16 ? *z++ : (c>>4);
        c = pal[c&0xf];
        if (x + len <= maxR) {
            dest = draw_line(dest, c, len);
            x = x+len;
            continue;
        }
        dest = draw_line(dest, c, maxR - x);
        break;
    }
    return dest;
}

static inline uint16_t *draw_double(uint16_t *dest, int l0, int l1, int skipL0, int skipL1, int maxWidth) {
    const struct image_data *img0 = images[0];
    const struct image_data *img1 = images[1];
    const uint16_t *pal0 = opaque_palettes[0]->entries;
    const uint16_t *pal1 = opaque_palettes[1]->entries;
    const uint8_t *z0 = img0->blob.bytes + img0->row_offsets[l0];
    const uint8_t *z1 = img1->blob.bytes + img1->row_offsets[l1];

    int c0, c1;
    int len0, len1;

//    printf("l0 %d l1 %d skipL0 %d skpiL1 %d\n", l0, l1, skipL0, skipL1);
#define update0 ({ c0 = *z0++; len0 = c0 < 16 ? *z0++ : (c0>>4); })
#define update1 ({ c1 = *z1++; len1 = c1 < 16 ? *z1++ : (c1>>4); })
    update0;
    while (skipL0 > 0) {
        if (skipL0 >= len0) {
            skipL0 -= len0;
            update0;
        } else {
            len0 -= skipL0;
            break;
        }
    }
    update1;
    while (skipL1 > 0) {
        if (skipL1 >= len1) {
            skipL1 -= len1;
            update1;
        } else {
            len1 -= skipL1;
            break;
        }
    }
    int x = 0;
    while (x<maxWidth) {
        uint16_t color = pal2[((c1 & 0xf)<<4) | c0 & 0xf];
        if (len0 <= len1) {
            if (len0 + x > maxWidth) len0 = maxWidth - x;
            x += len0;
            dest = draw_line(dest, color, len0);
            len1 -= len0;
            if (!len1) {
                update1;
            }
            update0;
        } else if (len0 > len1) {
            if (len1 + x > maxWidth) len1 = maxWidth - x;
            x += len1;
            dest = draw_line(dest, color, len1);
            len0 -= len1;
            update1;
        }
    }
    return dest;
}

bool __time_critical_func(render_scanline_pi)(struct scanvideo_scanline_buffer *dest, int core) {
    uint32_t *buf = dest->data;

    int length;
    int l0 = scanvideo_scanline_number(dest->scanline_id) - top[0];
    int l1 = scanvideo_scanline_number(dest->scanline_id) - top[1];
    bool in0 = l0 >= 0 && l0 < images[0]->height;
    bool in1 = l1 >= 0 && l1 < images[1]->height;
    uint16_t *out = (uint16_t *)buf;
#if 0
    uint16_t bg_color = get_core_num() ? PICO_SCANVIDEO_PIXEL_FROM_RGB8(0xff, 0x80, 0xff) : opaque_palettes[0]->entries[15];
#else
    uint16_t bg_color = opaque_palettes[0]->entries[15];
#endif
    if (in0 && in1) {
        int first_logo = left[1] < left[0];
        out = draw_line(out, bg_color, left[first_logo]);
        int skip_first = left[first_logo] < 0 ? -left[first_logo] : 0;
        int skip_second = left[first_logo^1] < 0 ? -left[first_logo^1] : 0;
        int maxR = MIN(vga_mode.width, left[first_logo^1]);
        // draw first logo up to any overlap
        out = draw_single(out, first_logo, first_logo?l1:l0, skip_first, maxR - left[first_logo]);
        int first_right = left[first_logo] + images[first_logo]->width;
        int overlap = first_right - left[first_logo^1];
        if (overlap < 0) {
            // draw gap
            out = draw_line(out, bg_color, -overlap);
            out = draw_single(out, first_logo^1, first_logo?l0:l1, 0, vga_mode.width - left[first_logo^1]);
            out = draw_line(out, bg_color, vga_mode.width - left[first_logo ^ 1] - images[first_logo ^ 1]->width);
        } else {
            overlap -= skip_second;
            maxR = MIN(overlap, vga_mode.width - left[first_logo^1]);
            if (first_logo) {
                out = draw_double(out, l0, l1, skip_second, images[1]->width - overlap, maxR);
            } else {
                out = draw_double(out, l0, l1, images[0]->width - overlap, skip_second, maxR);
            }
            if (first_right < vga_mode.width) {
                // todo could optimize this since we know where we were
                out = draw_single(out, first_logo ^ 1, first_logo ? l0 : l1, overlap + skip_second, vga_mode.width - left[first_logo^1]);
                out = draw_line(out, bg_color, vga_mode.width - left[first_logo ^ 1] - images[first_logo ^ 1]->width);
            }
        }
    } else if (in0 | in1) {
        int logo = in1;
        out = draw_line(out, bg_color, left[logo]);
        int skip = left[logo] < 0 ? -left[logo] : 0;
        out = draw_single(out, logo, logo?l1:l0, skip, vga_mode.width - left[logo]);// - skip);
        out = draw_line(out, bg_color, vga_mode.width - left[logo] - images[logo]->width);
    } else {
        out = draw_line(out, bg_color, vga_mode.width);
    }
    *out++ = COMPOSABLE_RAW_1P;
    *out++ = 0;
    if (2u & (uintptr_t) out) {
        // we are unaligned
        *out++ = COMPOSABLE_EOL_ALIGN;
    } else {
        *out++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *out++ = 0xffff; // eye catcher
    }
    length = ((uint32_t*)out) - buf;
    if (length > 0) {
        dest->status = SCANLINE_OK;
        assert(dest->data_used < dest->data_max);
        dest->data_used = (uint16_t) length;
        assert(dest->data_used < dest->data_max);
    } else {
        dest->status = SCANLINE_ERROR;
        dest->data_used = 0;
    }
    return true;
}

semaphore_t sem;

#if PICO_ON_DEVICE
#include "hardware/vreg.h"
#endif

int main(void) {
#if FOOPLE
    left[0] = 200;
    left[1] = 300;
    top[0] = -100;
    hspeed[0] = hspeed[1] = 0;
    vspeed[0] = vspeed[1] = 0;
#endif

//    stdio_init_all();
//    gpio_init(25);
//    gpio_set_dir(25, 1);
//    gpio_init(4);
//    gpio_set_dir(4, 1);
//    sem_init(&sem, 0, 1);
//    multicore_launch_core1(spap);
//    sem_acquire_blocking(&sem);
//    gpio_put(25, 1);
//    printf("YEP\n");
//    __breakpoint();
//    return 0;
    gpio_put(27, 0);
#if PICO_ON_DEVICE && !PICO_ON_FPGA
#if PICO_SCANVIDEO_48MHZ
    /* set to double frequency 48Mhz for some examples which were written for a higher clock speed */
    set_sys_clock_khz(96000, true);
//    set_sys_clock_khz(48000, true);
#endif
    if (vga_mode.default_timing->clock_freq >= 65000000) {
        vreg_set_voltage(VREG_VOLTAGE_1_30);
        busy_wait_ms(5);
    }
    switch (vga_mode.default_timing->clock_freq) {
        case 65000000:
            set_sys_clock_khz(130000, true);
            break;
        case 148500000:
            set_sys_clock_khz(148500*2, true);
            break;
        case 234000000:
            set_sys_clock_khz(234000*2, true);
            break;
        default:
            // may cause panic later
            break;
    }
#endif

    // Re init uart now that clk_peri has changed
    setup_default_uart();
#if PICO_NO_FLASH
    printf("no flash\n");
#else
    printf("flash\n");
#endif
//    int (*foo)(int) = rom_func_lookup(rom_table_code('S','Q'));
//    printf("FOO IS %p\n", foo);
//    for(int i = 0; i < 10; i++) {
//        printf("%d %d\n", i, foo(i));
//    }
#ifdef __riscv
    printf("This is RISCV yessir\n");
#else
    printf("This is ARM\n");
#endif

    return vga_main();
}
