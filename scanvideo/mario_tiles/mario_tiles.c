/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "stdlib.h"
#include "pico/sync.h"
#include "pico/stdlib.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/multicore.h"
#include "data.h"
#if PICO_ON_DEVICE
#include "hardware/interp.h"
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

#define COUNT ((vga_mode.width/8))

// for now we want to see second counter on native and don't need both cores
#if !PICO_NO_HARDWARE
// todo there is a bug in multithreaded rendering atm
#define RENDER_ON_CORE0
#endif
#define RENDER_ON_CORE1

//#define IRQS_ON_CORE1

render_scanline_func render_scanline = render_scanline_bg;

#define COORD_SHIFT 3
int vspeed = 1*1;
int hspeed = 1<<COORD_SHIFT;
int hpos;
int vpos;

static const int input_pin0 = 22;

// to make sure only one core updates the state when the frame number changes
// todo note we should actually make sure here that the other core isn't still rendering (i.e. all must arrive before either can proceed - a la barrier)
static struct mutex frame_logic_mutex;
static int x_sprites = 1;

const int16_t hacky_cos_table[] = {
        16384, 16383, 16382, 16381, 16379, 16376, 16372, 16368, 16364, 16359, 16353, 16346, 16339, 16331, 16323, 16314,
        16305, 16294, 16284, 16272, 16260, 16248, 16234, 16221, 16206, 16191, 16175, 16159, 16142, 16125, 16107, 16088,
        16069, 16049, 16028, 16007, 15985, 15963, 15940, 15917, 15892, 15868, 15842, 15817, 15790, 15763, 15735, 15707,
        15678, 15649, 15618, 15588, 15557, 15525, 15492, 15459, 15426, 15392, 15357, 15322, 15286, 15249, 15212, 15175,
        15136, 15098, 15058, 15018, 14978, 14937, 14895, 14853, 14810, 14767, 14723, 14679, 14634, 14589, 14543, 14496,
        14449, 14401, 14353, 14304, 14255, 14205, 14155, 14104, 14053, 14001, 13948, 13895, 13842, 13788, 13733, 13678,
        13622, 13566, 13510, 13452, 13395, 13337, 13278, 13219, 13159, 13099, 13038, 12977, 12916, 12854, 12791, 12728,
        12665, 12600, 12536, 12471, 12406, 12340, 12273, 12207, 12139, 12072, 12003, 11935, 11866, 11796, 11726, 11656,
        11585, 11513, 11442, 11370, 11297, 11224, 11150, 11077, 11002, 10928, 10853, 10777, 10701, 10625, 10548, 10471,
        10393, 10315, 10237, 10159, 10079, 10000, 9920, 9840, 9759, 9679, 9597, 9516, 9434, 9351, 9268, 9185,
        9102, 9018, 8934, 8850, 8765, 8680, 8594, 8509, 8423, 8336, 8249, 8162, 8075, 7988, 7900, 7811,
        7723, 7634, 7545, 7456, 7366, 7276, 7186, 7095, 7005, 6914, 6822, 6731, 6639, 6547, 6455, 6362,
        6269, 6176, 6083, 5990, 5896, 5802, 5708, 5614, 5519, 5424, 5329, 5234, 5139, 5043, 4948, 4852,
        4756, 4659, 4563, 4466, 4369, 4272, 4175, 4078, 3980, 3883, 3785, 3687, 3589, 3491, 3393, 3294,
        3196, 3097, 2998, 2900, 2801, 2701, 2602, 2503, 2404, 2304, 2204, 2105, 2005, 1905, 1805, 1705,
        1605, 1505, 1405, 1305, 1205, 1105, 1004, 904, 803, 703, 603, 502, 402, 301, 201, 100,
        0, -100, -201, -301, -402, -502, -603, -703, -803, -904, -1004, -1105, -1205, -1305, -1405, -1505,
        -1605, -1705, -1805, -1905, -2005, -2105, -2204, -2304, -2404, -2503, -2602, -2701, -2801, -2900, -2998, -3097,
        -3196, -3294, -3393, -3491, -3589, -3687, -3785, -3883, -3980, -4078, -4175, -4272, -4369, -4466, -4563, -4659,
        -4756, -4852, -4948, -5043, -5139, -5234, -5329, -5424, -5519, -5614, -5708, -5802, -5896, -5990, -6083, -6176,
        -6269, -6362, -6455, -6547, -6639, -6731, -6822, -6914, -7005, -7095, -7186, -7276, -7366, -7456, -7545, -7634,
        -7723, -7811, -7900, -7988, -8075, -8162, -8249, -8336, -8423, -8509, -8594, -8680, -8765, -8850, -8934, -9018,
        -9102, -9185, -9268, -9351, -9434, -9516, -9597, -9679, -9759, -9840, -9920, -10000, -10079, -10159, -10237, -10315,
        -10393, -10471, -10548, -10625, -10701, -10777, -10853, -10928, -11002, -11077, -11150, -11224, -11297, -11370, -11442, -11513,
        -11585, -11656, -11726, -11796, -11866, -11935, -12003, -12072, -12139, -12207, -12273, -12340, -12406, -12471, -12536, -12600,
        -12665, -12728, -12791, -12854, -12916, -12977, -13038, -13099, -13159, -13219, -13278, -13337, -13395, -13452, -13510, -13566,
        -13622, -13678, -13733, -13788, -13842, -13895, -13948, -14001, -14053, -14104, -14155, -14205, -14255, -14304, -14353, -14401,
        -14449, -14496, -14543, -14589, -14634, -14679, -14723, -14767, -14810, -14853, -14895, -14937, -14978, -15018, -15058, -15098,
        -15136, -15175, -15212, -15249, -15286, -15322, -15357, -15392, -15426, -15459, -15492, -15525, -15557, -15588, -15618, -15649,
        -15678, -15707, -15735, -15763, -15790, -15817, -15842, -15868, -15892, -15917, -15940, -15963, -15985, -16007, -16028, -16049,
        -16069, -16088, -16107, -16125, -16142, -16159, -16175, -16191, -16206, -16221, -16234, -16248, -16260, -16272, -16284, -16294,
        -16305, -16314, -16323, -16331, -16339, -16346, -16353, -16359, -16364, -16368, -16372, -16376, -16379, -16381, -16382, -16383,
        -16384, -16383, -16382, -16381, -16379, -16376, -16372, -16368, -16364, -16359, -16353, -16346, -16339, -16331, -16323, -16314,
        -16305, -16294, -16284, -16272, -16260, -16248, -16234, -16221, -16206, -16191, -16175, -16159, -16142, -16125, -16107, -16088,
        -16069, -16049, -16028, -16007, -15985, -15963, -15940, -15917, -15892, -15868, -15842, -15817, -15790, -15763, -15735, -15707,
        -15678, -15649, -15618, -15588, -15557, -15525, -15492, -15459, -15426, -15392, -15357, -15322, -15286, -15249, -15212, -15175,
        -15136, -15098, -15058, -15018, -14978, -14937, -14895, -14853, -14810, -14767, -14723, -14679, -14634, -14589, -14543, -14496,
        -14449, -14401, -14353, -14304, -14255, -14205, -14155, -14104, -14053, -14001, -13948, -13895, -13842, -13788, -13733, -13678,
        -13622, -13566, -13510, -13452, -13395, -13337, -13278, -13219, -13159, -13099, -13038, -12977, -12916, -12854, -12791, -12728,
        -12665, -12600, -12536, -12471, -12406, -12340, -12273, -12207, -12139, -12072, -12003, -11935, -11866, -11796, -11726, -11656,
        -11585, -11513, -11442, -11370, -11297, -11224, -11150, -11077, -11002, -10928, -10853, -10777, -10701, -10625, -10548, -10471,
        -10393, -10315, -10237, -10159, -10079, -10000, -9920, -9840, -9759, -9679, -9597, -9516, -9434, -9351, -9268, -9185,
        -9102, -9018, -8934, -8850, -8765, -8680, -8594, -8509, -8423, -8336, -8249, -8162, -8075, -7988, -7900, -7811,
        -7723, -7634, -7545, -7456, -7366, -7276, -7186, -7095, -7005, -6914, -6822, -6731, -6639, -6547, -6455, -6362,
        -6269, -6176, -6083, -5990, -5896, -5802, -5708, -5614, -5519, -5424, -5329, -5234, -5139, -5043, -4948, -4852,
        -4756, -4659, -4563, -4466, -4369, -4272, -4175, -4078, -3980, -3883, -3785, -3687, -3589, -3491, -3393, -3294,
        -3196, -3097, -2998, -2900, -2801, -2701, -2602, -2503, -2404, -2304, -2204, -2105, -2005, -1905, -1805, -1705,
        -1605, -1505, -1405, -1305, -1205, -1105, -1004, -904, -803, -703, -603, -502, -402, -301, -201, -100,
        0, 100, 201, 301, 402, 502, 603, 703, 803, 904, 1004, 1105, 1205, 1305, 1405, 1505,
        1605, 1705, 1805, 1905, 2005, 2105, 2204, 2304, 2404, 2503, 2602, 2701, 2801, 2900, 2998, 3097,
        3196, 3294, 3393, 3491, 3589, 3687, 3785, 3883, 3980, 4078, 4175, 4272, 4369, 4466, 4563, 4659,
        4756, 4852, 4948, 5043, 5139, 5234, 5329, 5424, 5519, 5614, 5708, 5802, 5896, 5990, 6083, 6176,
        6269, 6362, 6455, 6547, 6639, 6731, 6822, 6914, 7005, 7095, 7186, 7276, 7366, 7456, 7545, 7634,
        7723, 7811, 7900, 7988, 8075, 8162, 8249, 8336, 8423, 8509, 8594, 8680, 8765, 8850, 8934, 9018,
        9102, 9185, 9268, 9351, 9434, 9516, 9597, 9679, 9759, 9840, 9920, 10000, 10079, 10159, 10237, 10315,
        10393, 10471, 10548, 10625, 10701, 10777, 10853, 10928, 11002, 11077, 11150, 11224, 11297, 11370, 11442, 11513,
        11585, 11656, 11726, 11796, 11866, 11935, 12003, 12072, 12139, 12207, 12273, 12340, 12406, 12471, 12536, 12600,
        12665, 12728, 12791, 12854, 12916, 12977, 13038, 13099, 13159, 13219, 13278, 13337, 13395, 13452, 13510, 13566,
        13622, 13678, 13733, 13788, 13842, 13895, 13948, 14001, 14053, 14104, 14155, 14205, 14255, 14304, 14353, 14401,
        14449, 14496, 14543, 14589, 14634, 14679, 14723, 14767, 14810, 14853, 14895, 14937, 14978, 15018, 15058, 15098,
        15136, 15175, 15212, 15249, 15286, 15322, 15357, 15392, 15426, 15459, 15492, 15525, 15557, 15588, 15618, 15649,
        15678, 15707, 15735, 15763, 15790, 15817, 15842, 15868, 15892, 15917, 15940, 15963, 15985, 16007, 16028, 16049,
        16069, 16088, 16107, 16125, 16142, 16159, 16175, 16191, 16206, 16221, 16234, 16248, 16260, 16272, 16284, 16294,
        16305, 16314, 16323, 16331, 16339, 16346, 16353, 16359, 16364, 16368, 16372, 16376, 16379, 16381, 16382, 16383,
        16384
};

void go_core1(void (*execute)());
void init_render_state(int core);

void print_status() {
    printf("hspeed %d/8 (sprites %d)      \r", hspeed, x_sprites);
}

// ok this is going to be the beginning of retained mode
//

struct tile_data16 runtime_tile_data;

int32_t ha_du, ha_dv, ha_dud, ha_dvd;

int32_t hacky_cos(int32_t angle) {
    uint off = (angle >> 8)&0x3ff;
    int32_t a = hacky_cos_table[off];
    int32_t b = hacky_cos_table[off+1];
    return a + ((b - a) * (angle & 0xff))/0x100;
}

int32_t hacky_sin(int32_t angle) {
    return hacky_cos(angle - 0x10000);
}

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

int render_loop() {
	static uint8_t last_input = 0;
	static uint32_t last_frame_num = 0;
	int core_num = get_core_num();
	assert(core_num >=0 && core_num < 2);
	printf("Rendering on core %d\r\n", core_num);
#if DEBUG_PINS_ENABLED(frame_gen)
    if (core_num == 1) {
        gpio_init(PICO_DEBUG_PIN_BASE+1);
        gpio_set_dir_out_masked(2 << PICO_DEBUG_PIN_BASE); // steal debug pin 2 for this core
    }
#endif
	while (true) {
		struct scanvideo_scanline_buffer *scanvideo_scanline_buffer = scanvideo_begin_scanline_generation(true);
//		if (scanline_buffer->data_used) {
//            // validate the previous scanline to make sure noone corrupted it
//            validate_scanline(scanline_buffer->data, scanline_buffer->data_used, vga_mode.width, vga_mode.width);
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
//            if (enable_wave) {
            uint32_t angle2 = frame_num * 17;

            int64_t amp2 = 0x10000 + (2* hacky_sin(frame_num * 120));
            amp2 /= 8;
//                int64_t amp2 = 4 * (1.3f - 0.5f * ((cos(0.3f + frame_num * (M_PI_4 / 60.0f))) +
//                                                       cos(frame_num * (M_PI_4 / 120.0f))));
            ha_du = (int32_t) ((amp2 * hacky_cos(angle2)) / 0x4000);
            ha_dv = (int32_t) ((amp2 * hacky_sin(-angle2)) / 0x4000);
            ha_dud = -ha_dv;
            ha_dvd = ha_du;
//        }

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
                }
                ps = true;
			}
#endif
			hpos += hspeed;
			if (hpos < 0) {
			    hpos = 0;
			    hspeed = -hspeed;
			} else if (hpos >= (level0_map_width*8 - vga_mode.width) << COORD_SHIFT) {
			    hpos = (level0_map_width*8 - vga_mode.width) << COORD_SHIFT;
			    hspeed = -hspeed;
			}
			uint8_t new_input = gpio_get(input_pin0);
			if (last_input && !new_input) {
				hpos++;
			}
			last_input = new_input;
		}
		mutex_exit(&frame_logic_mutex);
        DEBUG_PINS_SET(frame_gen, core_num?2:4);
        render_scanline(scanvideo_scanline_buffer, core_num);
        DEBUG_PINS_CLR(frame_gen, core_num?2:4);
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
    assert(vga_mode.xscale >= 2); // too slow anyway, but we would need to turn half pixel off

    mutex_init(&frame_logic_mutex);
    //gpio_debug_pins_init()''

	// need to inflat the tiles for this demo (can't reuse span data)
    runtime_tile_data.blob.size = tiles_tile_data.width * tiles_tile_data.height * tiles_tile_data.count * 2;
	runtime_tile_data.blob.bytes = malloc(runtime_tile_data.blob.size);
	assert(runtime_tile_data.blob.bytes);
	const int width = tiles_tile_data.width * 2;
	for(int i=0;i<tiles_tile_data.count * tiles_tile_data.height;i++) {
	    const uint8_t *src = tiles_tile_data.blob.bytes + tiles_tile_data.span_offsets[i];
	    __builtin_memcpy((uint8_t *)runtime_tile_data.blob.bytes + i * width, src, width);
	}

	// get the bottom
	vpos = level0_map_height*8 - vga_mode.height;
	assert(vpos >= 0);

    convert_spans(&runtime_tile_data);
    convert_spans(&galaga_tile_data);

    sem_init(&video_setup_complete, 0, 1);
#ifndef IRQS_ON_CORE1
	setup_video();
#endif

    puts("KEYS:");
    puts("  +/-   adjust horizonatal speed");
    puts("  9/0   up/down horizontal sprite count");

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

uint16_t __attribute__((noinline)) *tile_loop(uint16_t *buf, int w0, uint32_t u, uint32_t v, int32_t du, int32_t dv) {
    const int FRACTIONAL_BITS = 16;
    const int MAP_BITS_U = 8;
    const int MAP_BITS_V = 5;
#if PICO_ON_DEVICE
    interp0->base[0] = du;
    interp_config config = interp_default_config();
    interp_config_set_shift(&config, FRACTIONAL_BITS - 1);
    interp_config_set_mask(&config, 1, MAP_BITS_U);
    interp_config_set_add_raw(&config, true);
    interp_set_config(interp0, 0, &config);
    interp0->base[1] = dv;
    config = interp_default_config();
    interp_config_set_shift(&config, FRACTIONAL_BITS - MAP_BITS_U - 1);
    interp_config_set_mask(&config, 1 + MAP_BITS_U, 1 + MAP_BITS_U + (MAP_BITS_V - 1));
    interp_config_set_add_raw(&config, true);
    interp_set_config(interp0, 1, &config);
    interp0->base[2] = (uintptr_t) level0_map;

    interp0->accum[0] = u;
    interp0->accum[1] = v;

    const int FRACTIONAL_BITS2 = 13;
    const int TILE_BITS_U = 3;
    const int TILE_BITS_V = 3;
    interp1->base[0] = du;
    config = interp_default_config();
    interp_config_set_shift(&config, FRACTIONAL_BITS2 - 1);
    interp_config_set_mask(&config, 1, TILE_BITS_U);
    interp_config_set_add_raw(&config, true);
    interp_set_config(interp1, 0, &config);
    interp1->base[1] = dv;
    config = interp_default_config();
    interp_config_set_shift(&config, FRACTIONAL_BITS2 - TILE_BITS_U - 1);
    interp_config_set_mask(&config, 1 + TILE_BITS_U, 1 + TILE_BITS_U + (TILE_BITS_V - 1));
    interp_config_set_add_raw(&config, true);
    interp_set_config(interp1, 1, &config);
    interp1->base[2] = (uintptr_t) runtime_tile_data.blob.bytes;
    interp1->accum[0] = u;
    interp1->accum[1] = v;

    for (int w = 0; w < w0; w++) {
        for(int i=0;i<8;i++) {
            uint16_t *map = (uint16_t *)interp0->pop[2];
            uint16_t *base = (uint16_t *)interp1->pop[2];
            *buf++ = base[64 * *map];
        }
    }
#else
    for (int w = 0; w < w0; w++) {
        for(int i=0;i<8;i++) {
            *buf++ = 0x421*i;
        }
    }
#endif
    return buf;
}

bool render_scanline_bg(struct scanvideo_scanline_buffer *dest, int core) {
    // 1 + line_num red, then white
    uint32_t *buf = dest->data;
    int y = scanvideo_scanline_number(dest->scanline_id) + vpos;
    int x = hpos;
    //y = (y + frame_number(dest->scanline_id)) % (level0_map_height * 8);
    const uint16_t *map = level0_map + level0_map_width * (y/8);
    map += (x >> (COORD_SHIFT + 3));
    const uint16_t *map0 = map;
    const uint8_t *data_base /* ha */ = runtime_tile_data.blob.bytes + (y&7u)*16;
    uint32_t *output32;
    uint32_t off;
    uint16_t *data;
//    const uint16_t *span_offsets = runtime_tile_data.span_offsets + (y&7u);
#if !PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA
    uint16_t *output = (uint16_t*)buf;
    *output++ = COMPOSABLE_RAW_RUN;
    uint16_t *fixup = output;
    *output++ = 0;
#if 0
    for(int i=0;i<COUNT;i++) {
        uint32_t off = 128 * (*map++);
        uint16_t *data = (uint16_t *)(data_base + off);
        for(int j=0;j<8;j++) *output++ = *data++;
    }
#else
    int32_t u = x << (13 - COORD_SHIFT);
    int32_t v = 0;
    u += ha_dud * (y - vpos);
    v += ha_dvd * (y - vpos);
    output = tile_loop(output, COUNT, u, v, ha_du, ha_dv);
#endif
    fixup[0] = fixup[1];
    fixup[1] = COUNT * 8 - 3;

    // todo fix so we don't need whole scanline

//    *output++ = COMPOSABLE_COLOR_RUN;
//    *output++ = 0;
//    *output++ = vga_mode.width - COUNT * 8 - 1 - 3;

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
    output32 = buf + 2; // skip one chain segment - we fill in below
    map++; // skip first element
    off = 128 * map0[0];
    int i = (x>>COORD_SHIFT)&7;
    int eol_pixels = i;
    data = (uint16_t *)(data_base + off);
    data += i;

    int j;
    if (i>=7) {
        j = 1;
        // skip second element in the offset 7 7/12 case
        map++;
    } else {
        j = 0;
    }
    // render full size tiles
    for(;j<COUNT;j++) {
        uint32_t off = 128 * *map++;
        uint16_t *data = (uint16_t *)(data_base + off);
        *output32++ = 4;
        assert(!(3u & (intptr_t)data));
        *output32++ = native_safe_hw_ptr(data);
    }
    // end of scanline // to be filled in below
    *output32++ = 0;
    *output32++ = 0;
    // end of dma chain (correct 0 values)
    *output32++ = 0;
    *output32++ = 0;
    uint16_t *output = (uint16_t*)output32;
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
    if (i&1) {
        *output++ = COMPOSABLE_RAW_2P;
        *output++ = *data++;
        if (i==7) {
            // cope with the case where we've stepped onto the new tile
            off = 128 * map0[1];
            data = (uint16_t *)(data_base + off);
            i = 1;
        } else {
            i += 2;
        }
        run_length -= 2;
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
    assert(0 == (3u & (intptr_t)output));
    // setup our first chain segment (to point into our buffer here)
    buf[0] = ((uint32_t *)output) - output32;
    buf[1] = native_safe_hw_ptr(output32);

    // end of line
    uint32_t *eol_base = (uint32_t*)output;

    if (eol_pixels) {
        off = 128 * *map++;
        data = (uint16_t *) (data_base + off);
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
    }
    *output++ = COMPOSABLE_RAW_1P;
    *output++ = 0;
    if (2u & (intptr_t)output) {
        *output++ = COMPOSABLE_EOL_ALIGN;
    } else {
        *output++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *output++ = 0xffff; // eye catcher
    }
    // setup our last chain segment (to point into our buffer here)
    output32[-4] = ((uint32_t *)output) - eol_base;; //len
    output32[-3] = native_safe_hw_ptr(eol_base);
    assert(0 == (3u & (intptr_t)output));
    assert((uint32_t*)output <= (buf + dest->data_max));
    dest->data_used = (uint16_t)(output32 - buf); // todo we don't want to include the off the end data in the "size" for the dma
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

    int wibble = (scanvideo_frame_number(dest->scanline_id) >> 2) % 7;
    for(int q = 0; q < x_sprites; q++) {
        // nice if we can do two black pixel before
        *output++ = COMPOSABLE_RAW_RUN;
        *output++ = 0;
        *output++ = galaga_tile_data.width + 2 - 3;
        *output++ = 0;
        MAKE_SEGMENT;

        const uint16_t *span_offsets = galaga_tile_data.span_offsets + (q+wibble) * galaga_tile_data.height + (y - vpos);//(y%galaga_tile_data.count 7u);
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