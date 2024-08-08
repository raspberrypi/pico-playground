/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <stdio.h>

#include "pico/sync.h"
#include "pico/stdlib.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "spans.h"
#include "data.h"
#if PICO_ON_DEVICE
#include "hardware/clocks.h"
#endif

CU_REGISTER_DEBUG_PINS(frame_gen)

//CU_SELECT_DEBUG_PINS(frame_gen)

//#define DEBUG_HALF_PIXEL
typedef bool (*render_scanline_func)(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_test_pattern(struct scanvideo_scanline_buffer *dest, int core);
bool render_scanline_bg(struct scanvideo_scanline_buffer *dest, int core);

////#define vga_mode vga_mode_640x480_60
#define vga_mode vga_mode_320x240_60
//#define vga_mode vga_mode_213x160_60
//#define vga_mode vga_mode_160x120_60
////#define vga_mode vga_mode_tft_800x480_50
//#define vga_mode vga_mode_tft_400x240_50
//#define DISABLE_HPIXELS
//#define vga_mode vga_mode_tft_320x240_60

#define COUNT ((vga_mode.width/8)-1)

// for now we want to see second counter on native and don't need both cores
#if !PICO_NO_HARDWARE
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
#ifndef DISABLE_HPIXELS
bool enable_half_pixels = true;
#else
#define enable_half_pixels false
#endif
bool enable_wave;
int wave_amplitude = 50;

static const int input_pin0 = 22;

// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;
static int left = 0;
static int top = 0;
static int x_sprites = 1;

void go_core1(void (*execute)());
void init_render_state(int core);

void print_status() {
    printf("hspeed %d/8 half-pixel=%d wave=%d (amp=%d) (sprites %d)      \r", hspeed, enable_half_pixels, enable_wave,
           wave_amplitude, x_sprites);
}

// ok this is going to be the beginning of retained mode
//

// data is in the wrong color format
void convert_spans(const struct tile_data16 *td) {
#if PICO_ON_DEVICE
    uint16_t *p = (uint16_t *) td->blob.bytes;
    for (int i = 0; i < td->blob.size / 2; i++) {
        uint r = p[i] & 0x1f;
        uint g = (p[i] >> 5) & 0x1f;
        uint b = (p[i] >> 10) & 0x1f;
        uint alpha = p[i] >> 15;
        p[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(r, g, b) | (alpha ? PICO_SCANVIDEO_ALPHA_MASK : 0);
    }
#endif
}

void render_loop() {
    static uint8_t last_input = 0;
    static uint32_t last_frame_num = 0;
    int core_num = get_core_num();
    assert(core_num >= 0 && core_num < 2);
    printf("Rendering on core %d\r\n", core_num);
    if (DEBUG_PINS_ENABLED(frame_gen)) {
        gpio_init(PICO_DEBUG_PIN_BASE + 1);
        gpio_set_dir_out_masked(2 << PICO_DEBUG_PIN_BASE); // steal debug pin 2 for this core
    }
    while (true) {
        struct scanvideo_scanline_buffer *scanvideo_scanline_buffer = scanvideo_begin_scanline_generation(true);
//		if (scanvideo_scanline_buffer->data_used) {
//            // validate the previous scanline to make sure no one corrupted it
//            validate_scanline(scanvideo_scanline_buffer->data, scanvideo_scanline_buffer->data_used, vga_mode.width, vga_mode.width);
//        }
        // do any frame related logic
        bool ps = false;
        // todo probably a race condition here ... thread dealing with last line of a frame may end
        // todo up waiting on the next frame...
        mutex_enter_blocking(&frame_logic_mutex);
        uint32_t frame_num = scanvideo_frame_number(scanvideo_scanline_buffer->scanline_id);
        // note that with multiple cores we may have got here not for the first scanline, however one of the cores will do this logic first before either does the actual generation
        if (frame_num != last_frame_num) {
            if (frame_num == 1) {
                ps = true;
            }
            // this could should be during vblank as we try to create the next line
            // todo should we ignore if we aren't attempting the next line
            last_frame_num = frame_num;
#if PICO_ON_DEVICE
            if (uart_is_readable(uart_default)) {
                int c = uart_getc(uart_default);
                switch (c) {
                    case '+':
                    case '=':
                        hspeed++;
                        break;
                    case '_':
                    case '-':
                        hspeed--;
                        break;
                    case '9':
                        if (x_sprites > 0) x_sprites--;
                        break;
                    case '0':
                        if (x_sprites < 17) x_sprites++;
                        break;
                    case '[':
                        wave_amplitude--;
                        break;
                    case ']':
                        wave_amplitude++;
                        break;
#ifndef DISABLE_HPIXELS
                    case 'h':
                        enable_half_pixels ^= true;
                        break;
#endif
                    case 'w':
                        enable_wave ^= true;
                }
                ps = true;
            }
#endif
            hpos += hspeed;
            if (hpos < 0) {
                hpos = 0;
                hspeed = -hspeed;
            } else if (hpos >= (level0_map_width * 8 - vga_mode.width) << COORD_SHIFT) {
                hpos = (level0_map_width * 8 - vga_mode.width) << COORD_SHIFT;
                hspeed = -hspeed;
            }
            uint8_t new_input = gpio_get(input_pin0);
            if (last_input && !new_input) {
                hpos++;
            }
            last_input = new_input;
        }
        mutex_exit(&frame_logic_mutex);
        DEBUG_PINS_SET(frame_gen, core_num ? 2 : 4);
        render_scanline(scanvideo_scanline_buffer, core_num);
        DEBUG_PINS_CLR(frame_gen, core_num ? 2 : 4);
#if PICO_SCANVIDEO_PLANE_COUNT > 2
        assert(false);
#endif
        // release the scanline into the wild
        scanvideo_end_scanline_generation(scanvideo_scanline_buffer);
        // do this outside mutex and scanline generation
        if (ps) {
            print_status();
        }
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

int video_main(void) {
#ifndef DISABLE_HPIXELS
    assert(vga_mode.xscale >= 2); // too slow anyway, but we would need to turn half pixel off
#endif

//    gpio_debug_pins_init();
    mutex_init(&frame_logic_mutex);

    // get the bottom
    vpos = level0_map_height * 8 - vga_mode.height;
    assert(vpos >= 0);

    convert_spans(&tiles_tile_data);
    convert_spans(&galaga_tile_data);

    sem_init(&video_setup_complete, 0, 1);
#ifndef IRQS_ON_CORE1
    setup_video();
#endif

    puts("KEYS:");
    puts("  +/-   adjust horizonatal speed");
    puts("  h     toggle half pixel mode");
    puts("  9/0   up/down horizontal sprite count");
    puts("  w     toggle wave mode");
    puts("  [/]   adjust wave amplitude");

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
//	init_solid_color_span(&before_span[core], left, opaque_pi_palette->entries[15], NULL);
//	init_vogon_4bit_span(&pi_span[core], pi400_image_data.width, NULL, 0, opaque_pi_palette, &before_span[core]);
//	init_solid_color_span(&after_span[core], vga_mode.width - left - pi400_image_data.width, opaque_pi_palette->entries[15],
//						  &pi_span[core]);
}

static const int8_t hacky_cos_table[] = {127, 126, 124, 121, 117, 112, 105, 98, 89, 80, 70, 59, 48, 36, 24, 12, 0, -12,
                                         -24, -36, -48, -59, -70, -80, -89, -98, -105, -112, -117, -121, -124, -126,
                                         -127, -126, -124, -121, -117, -112, -105, -98, -89, -80, -70, -59, -48, -36,
                                         -24, -12, 0, 12, 24, 36, 48, 59, 70, 80, 89, 98, 105, 112, 117, 121, 124, 126};

bool render_scanline_bg(struct scanvideo_scanline_buffer *dest, int core) {
    // 1 + line_num red, then white
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;
    int y = scanvideo_scanline_number(dest->scanline_id) + vpos;
    int x = hpos;
    if (enable_wave) {
        x += (wave_amplitude *
              hacky_cos_table[(y + scanvideo_frame_number(dest->scanline_id)) % count_of(hacky_cos_table)]) >> 8;
        if (x < 0) x = 0;
        if (x > ((level0_map_width * 8 - vga_mode.width) << COORD_SHIFT))
            x = (level0_map_width * 8 - vga_mode.width) << COORD_SHIFT;
    }
    //y = (y + frame_number(dest->scanline_id)) % (level0_map_height * 8);
    const uint16_t *map = level0_map + level0_map_width * (y / 8);
    map += (x >> (COORD_SHIFT + 3));
    const uint16_t *map0 = map;
    const uint16_t *span_offsets = tiles_tile_data.span_offsets + (y & 7u);
#if !PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
    uint16_t *output = (uint16_t*)buf;
    // raw run has an inline first pixel, so we need to handle that here
    // todo shift the video mode over by a pixel to account for this
    *output++ = COMPOSABLE_RAW_RUN;
    *output++ = 0;
    *output++ = 1 + COUNT * 8 - 3;
    for(int i=0;i<COUNT;i++) {
        uint32_t off = span_offsets[8 * *map++];
        uint16_t *data = (uint16_t *)(tiles_tile_data.blob.bytes + off);
        for(int j=0;j<8;j++) *output++ = *data++;
    }

    // todo fix so we don't need whole scanline

    *output++ = COMPOSABLE_COLOR_RUN;
    *output++ = 0;
    *output++ = vga_mode.width - COUNT * 8 - 1 - 3;

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
#ifdef DEBUG_HALF_PIXEL
    int debug_half_pixel_count = 0;
#endif
    // we handle both ends separately
//    static const uint32_t end_of_line[] = {
//            COMPOSABLE_RAW_1P | (0u<<16),
//            COMPOSABLE_EOL_SKIP_ALIGN | (0xffff << 16) // eye catcher ffff
//    };
    uint32_t *output32 = buf + 2; // skip one chain segment - we fill in below
    map++; // skip first element
    uint32_t off = span_offsets[8 * map0[0]];
    int i = (x >> COORD_SHIFT) & 7;
    int eol_pixels = i;
    uint16_t *data = (uint16_t *) (tiles_tile_data.blob.bytes + off);
    bool half_pixel = enable_half_pixels && (x & (1 << (COORD_SHIFT - 1)));
    data += i;

    if (half_pixel) i++; // we deal with the second pixel specially
    int j;
    if (i >= 7) {
        j = 1;
        // skip second element in the offset 7 7/12 case
        map++;
    } else {
        j = 0;
    }
    // render full size tiles
    for (; j < COUNT; j++) {
        uint32_t off = span_offsets[8 * *map++];
        uint16_t *data = (uint16_t *) (tiles_tile_data.blob.bytes + off);
        *output32++ = 4;
        assert(!(3u & (intptr_t) data));
        *output32++ = host_safe_hw_ptr(data);
    }
    // end of scanline // to be filled in below
    *output32++ = 0;
    *output32++ = 0;
    // end of dma chain (correct 0 values)
    *output32++ = 0;
    *output32++ = 0;
    uint16_t *output = (uint16_t *) output32;
    // draw a possibly fractional first tile

    // 0: RUN T00 | LEN T01 | T02 T03 | T04 T05 | T06 T07 |
    // 1: R2P T01 | T02 RUN | T03 LEN | T04 T05 | T06 T07 |
    // 2: RUN T02 | LEN T03 | T04 T05 | T06 T07 |
    // 3: R2P T03 | T04 RUN | T05 LEN | T06 T07 |
    // 4: RUN T04 | LEN T05 | T06 T07 |
    // 5: R2P T05 | T06 RUN | T05 LEN | T06 T07 |
    // 6: RUN T06 | LEN T07 |
    // 7: R2P T07 | T10 RUN | T11 LEN | T12 T13 | T14 T15 | T16 T17 |

    // with half pixel (i.e we set i to i+1 above, and always prefix)
    // 0: H1P T00 | R2P T01 | T02 RUN | T03 LEN | T04 T05 | T06 T07 |
    // 1: H1P T01 | RUN T02 | LEN T03 | T04 T05 | T06 T07 |
    // 2: H1P T02 | R2P T03 | T04 RUN | T05 LEN | T06 T07 |
    // 3: H1P T03 | RUN T04 | LEN T05 | T06 T07 |
    // 4: H1P T04 | R2P T05 | T06 RUN | T05 LEN | T06 T07 |
    // 5: H1P T05 | RUN T06 | LEN T07 |
    // 6: H1P T06 | R2P T07 | T10 RUN | T11 LEN | T12 T13 | T14 T15 | T16 T17 |
    // 7: H1P T07 | RUN T10 | LEN T11 | T12 T13 | T14 T15 | T16 T17 |

    int run_length = (8 - (i & 7u)) + COUNT * 8 - 3;
#ifndef DISABLE_HPIXELS
    if (half_pixel) {
        // prefix with the half pixel
        *output++ = COMPOSABLE_RAW_1P_2CYCLE;
        *output++ = *data++;
#ifdef DEBUG_HALF_PIXEL
        debug_half_pixel_count++;
#endif
        if (i == 8) {
            // cope with the case where we've stepped onto the new tile after the half-pixel
            off = span_offsets[8 * map0[1]];
            data = (uint16_t *) (tiles_tile_data.blob.bytes + off);
            i = 0;
            run_length -= 8;
        }
    }
#endif
    if (i & 1) {
        *output++ = COMPOSABLE_RAW_2P;
        *output++ = *data++;
        if (i == 7) {
            // cope with the case where we've stepped onto the new tile
            off = span_offsets[8 * map0[1]];
            data = (uint16_t *) (tiles_tile_data.blob.bytes + off);
            i = 1;
        } else {
            i += 2;
        }
        run_length -= 2;
#ifdef DEBUG_HALF_PIXEL
        debug_half_pixel_count+=4;
#endif
        *output++ = *data++;
        *output++ = COMPOSABLE_RAW_RUN;
    } else {
        *output++ = COMPOSABLE_RAW_RUN;
    }
    *output++ = *data++;
    *output++ = run_length;
    for (; i < 7; i++) {
        *output++ = *data++;
    }
    assert(0 == (3u & (intptr_t) output));
    // setup our first chain segment (to point into our buffer here)
    buf[0] = ((uint32_t *) output) - output32;
    buf[1] = host_safe_hw_ptr(output32);

    // end of line
    uint32_t *eol_base = (uint32_t *) output;

    if (eol_pixels || half_pixel) {
        off = span_offsets[8 * *map++];
        data = (uint16_t *) (tiles_tile_data.blob.bytes + off);
#ifdef DEBUG_HALF_PIXEL
        debug_half_pixel_count+=eol_pixels * 2;
#endif
        switch (eol_pixels) {
            // we could be slightly more optimal in the non half pixel case, but don't reall care
            case 0:
                break;
            case 1:
                *output++ = COMPOSABLE_RAW_1P;
                *output++ = *data++;
                break;
            case 2:
                *output++ = COMPOSABLE_RAW_2P;
                *output++ = *data++;
                *output++ = *data++;
                break;
            default:
                *output++ = COMPOSABLE_RAW_RUN;
                *output++ = *data++;
                *output++ = eol_pixels - 3;
                for (int i = 1; i < eol_pixels; i++) {
                    *output++ = *data++;
                }
                break;
        }
#ifndef DISABLE_HPIXELS
        if (half_pixel) {
            *output++ = COMPOSABLE_RAW_1P_2CYCLE;
            *output++ = *data++;
#ifdef DEBUG_HALF_PIXEL
            debug_half_pixel_count++;
#endif
        }
#endif
    }
    *output++ = COMPOSABLE_RAW_1P;
    *output++ = 0;
    if (2u & (intptr_t) output) {
        *output++ = COMPOSABLE_EOL_ALIGN;
    } else {
        *output++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *output++ = 0xffff; // eye catcher
    }
    // setup our last chain segment (to point into our buffer here)
    output32[-4] = ((uint32_t *) output) - eol_base;; //len
    output32[-3] = host_safe_hw_ptr(eol_base);
#ifdef DEBUG_HALF_PIXEL
    debug_half_pixel_count += (run_length+3)*2;
    if (y==vpos) printf("%d %d %d %d\n", (x>>COORD_SHIFT)&7, half_pixel, debug_half_pixel_count, eol_pixels);
#endif
    assert(0 == (3u & (intptr_t) output));
    assert((uint32_t *) output <= (buf + dest->data_max));
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
    output = (uint16_t *) inline_data;

    uint32_t *base = (uint32_t *) output;

#define MAKE_SEGMENT \
    assert(0 == (3u & (intptr_t)output)); \
    *output32++ = (uint32_t*)output - base; \
    *output32++ = host_safe_hw_ptr(base); \
    base = (uint32_t *)output;

    int wibble = (scanvideo_frame_number(dest->scanline_id) >> 2) % 7;
    for (int q = 0; q < x_sprites; q++) {
        // nice if we can do two black pixel before
        *output++ = COMPOSABLE_RAW_RUN;
        *output++ = 0;
        *output++ = galaga_tile_data.width + 2 - 3;
        *output++ = 0;
        MAKE_SEGMENT;

        span_offsets = galaga_tile_data.span_offsets + (q + wibble) * galaga_tile_data.height +
                       (y - vpos);//(y%galaga_tile_data.count 7u);
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
    assert((uint32_t *) output <= (buf + dest->data2_max));
    dest->data2_used = (uint16_t) (output32 -
                                   buf); // todo we don't want to include the inline data in the "size" for the dma
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
    setup_default_uart();

#if PICO_NO_HARDWARE
    //#include <math.h>
    //	for(int i = 0; i<64;i++) {
    //	    printf("%d, ", (int)(0x7f*cos(i*M_PI/32)));
    //	}
    //	printf("\n");
#endif
//#include "level0.h"
//	uint8_t *p = level0_dat;
//	int w = 1<<p[0];
//    int h = 1<<p[1];
//    printf("const int level0_map_width = %d;\n", w);
//    printf("const int level0_map_height = %d;\n", h);
//    printf("const uint16_t level0_map = {\n");
//    for (int i = 0; i < w*h; i += 32) {
//        printf("\t\t");
//        for (int j = i; j < w*h && j < (i + 32); j++) {
//            uint8_t *q = p + 2 + j*2;
//            printf("0x%04x, ", q[0]*256+q[1]);
//        }
//        printf("\n");
//    }
//    printf("};\n");

    return video_main();
}