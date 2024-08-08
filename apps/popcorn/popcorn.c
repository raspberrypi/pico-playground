/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * This source file is the result of prototyping work. It is not yet very examplary.
 * In the future it will be cleaned up and commented for a blog post.
 * In the meanwhile enter at your own risk.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "pico/audio_i2s.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/sd_card.h"
#include "hardware/divider.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "platypus.h"
#include "font.h"

#ifdef VGABOARD_BUTTON_A_PIN
#define USE_VGABOARD_BUTTONS 1
#else
#define USE_UART_INPUT 1
#endif

// ----------------------------------------------------------------
// DEBUGGING

CU_REGISTER_DEBUG_PINS(frame_generation, audio_buffering)
//CU_SELECT_DEBUG_PINS(frame_generation)
//CU_SELECT_DEBUG_PINS(audio_buffering)

#ifdef ENABLE_STRICT_ASSERTIONS
#define strict_assert(x) assert(x)
#else
#define strict_assert(x) ((void)0)
#endif

#define popcorn_debug(format, args...) (void)0

static const struct scanvideo_mode vga_mode_320x120_60 =
        {
                .default_timing = &vga_timing_640x480_60_default,
                .pio_program = &video_24mhz_composable,
                .width = 320,
                // only 120 logical scan lines (since we deal with two pixel rows at a time)
                .height = 120,
                .xscale = 2,
                .yscale = 4, // * 4 = 480
        };

#define vga_mode vga_mode_320x120_60

struct text_element {
    const char *text;
    uint16_t color;
    int width;
};

struct movie {
    struct text_element text;
    uint32_t start_sector;
    uint32_t current_sector;
} *movies;

uint movie_count;

#define NAME_COLOR PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x10, 0x10, 0x10)
#define INSTR_COLOR1 PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x10, 0x08, 0x08)
#define INSTR_COLOR2 PICO_SCANVIDEO_PIXEL_FROM_RGB5(0x08, 0x10, 0x10)

struct movie static_movies[] = {
        {.text = {"<unknown name>", NAME_COLOR, 0}, .start_sector = 0},
};

#if USE_UART_INPUT
struct text_element instructions[] = {
        { "SPACE - toggle play/pause", INSTR_COLOR1 },
        { "ENTER - toggle display", INSTR_COLOR2 },
        { "'0' -> '4' - stopped -> fast", INSTR_COLOR1 },
        { "'+' / '-' - faster / slower(-mo)", INSTR_COLOR2 },
        { "'.' / ',' - step + / - one frame", INSTR_COLOR1 },
        { "'r' - reverse play direction!", INSTR_COLOR2 },
        { "'n' / 'p' - next / previous movie", INSTR_COLOR1 },
        { "'[' / ']' - down / up volume", INSTR_COLOR2 },
};
#define DISPLAY_NAME_AFTER_FRAME_COUNT 1
#elif defined(USE_VGABOARD_BUTTONS)
struct text_element instructions[] = {
        {"Short press while playing",        INSTR_COLOR1},
        {"Slower : Pause : Faster",          INSTR_COLOR1},
        {"Short press while paused",         INSTR_COLOR2},
        {"< Step : Play : Step >",           INSTR_COLOR2},
        {"Medium press",                     INSTR_COLOR1},
        {"< File : Toggle Menu : File >",    INSTR_COLOR1},
        {"Long Press",                       INSTR_COLOR2},
        {"< Vol : Toggle Direction : Vol >", INSTR_COLOR2},
};
#define DISPLAY_NAME_AFTER_FRAME_COUNT 4
#endif

uint32_t display_base_frame;
uint current_movie;
uint registered_current_movie; // as seen by decode

static struct font *font12;
static struct font *font18;

static semaphore_t video_setup_complete;
static struct mutex frame_logic_mutex;

static uint8_t playback_forwards = 1;
static int8_t playback_speed;
static int8_t hold_frame_count;
static int remaining_hold_frames = 1;
static int32_t next_frame_sector_override = -1;
static int32_t audio_sector_pairs_to_post_process;
static int32_t volume = 0x100;
static uint32_t total_audio_sectors;
static bool show_menu = false;
static int text_roller = 0;

static void init_core(int core);
static void handle_input();

#if USE_VGABOARD_BUTTONS
volatile uint32_t button_state = 0; // sorry graham
static const uint button_pins[] = {0, 6, 11};

int32_t last_button_time[3];
uint32_t last_button_state;

const uint VSYNC_PIN = PICO_SCANVIDEO_COLOR_PIN_BASE + PICO_SCANVIDEO_COLOR_PIN_COUNT + 1;

// Registered as GPIO interrupt on both edges of vsync. On vsync assertion,
// set pins to input. On deassertion, sample and set back to output.
void vga_board_button_irq_handler() {
    int vsync_current_level = gpio_get(VSYNC_PIN);
    gpio_acknowledge_irq(VSYNC_PIN, vsync_current_level ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL);

    // Note v_sync_polarity == 1 means active-low because anything else would be confusing
    if (vsync_current_level != scanvideo_get_mode().default_timing->v_sync_polarity) {
        for (int i = 0; i < count_of(button_pins); ++i) {
            gpio_pull_down(button_pins[i]);
            gpio_set_oeover(button_pins[i], GPIO_OVERRIDE_LOW);
        }
    } else {
        uint32_t state = 0;
        for (int i = 0; i < count_of(button_pins); ++i) {
            state |= gpio_get(button_pins[i]) << i;
            gpio_set_oeover(button_pins[i], GPIO_OVERRIDE_NORMAL);
        }
        button_state = state;
    }
}

void vga_board_init_buttons() {
    gpio_set_irq_enabled(VSYNC_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    irq_set_exclusive_handler(IO_IRQ_BANK0, vga_board_button_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
}
#endif

struct font {
    uint width_words;
    uint height;
    uint glyph_words; // == width_words * height;
    uint32_t *pixels;
};

struct font *build_font(const lv_font_t *font, uint width_words) {
    struct font *f = (struct font *) calloc(1, sizeof(struct font));
    f->width_words = width_words;
    f->height = font->line_height;

    uint16_t colors[16];
    for (int i = 0; i < count_of(colors); i++) {
#ifdef PLATYPUS_565
        colors[i] = 0x41 * i + 0x800 * (i / 2);
#else
        colors[i] = 0x21 * i +  0x400 * (i/2);
#endif
    }
    uint font_size_words = font->line_height * width_words;
    f->pixels = (uint32_t *) calloc(4, font->dsc->cmaps->range_length * font_size_words);
    f->glyph_words = f->width_words * f->height;
    uint32_t *p = f->pixels;

    for (int c = 0; c < font->dsc->cmaps->range_length; c++) {
        // inefficient but simple
        assert(p == f->pixels + c * f->glyph_words);
        const lv_font_fmt_txt_glyph_dsc_t *g = &font->dsc->glyph_dsc[c + 1];
        const uint8_t *b = font->dsc->glyph_bitmap + g->bitmap_index;
        int bi = 0;
        for (int y = 0; y < f->height; y++) {
            int ey = y - f->height + font->base_line + g->ofs_y + g->box_h;
            for (int x = 0; x < f->width_words * 2; x++) {
                uint32_t pixel;
                int ex = x - g->ofs_x;
                if (ex >= 0 && ex < g->box_w && ey >= 0 && ey < g->box_h) {
                    pixel = bi & 1 ? colors[b[bi >> 1] & 0xf] : colors[b[bi >> 1] >> 4];
                    bi++;
                } else {
                    pixel = 0;
                }
                if (!(x & 1)) {
                    *p = pixel;
                } else {
                    *p++ |= pixel << 16;
                }
            }
            if (ey >= 0 && ey < g->box_h) {
                for (int x = f->width_words * 2 - g->ofs_x; x < g->box_w; x++) {
                    bi++;
                }
            }
        }
    }
    return f;
}

static void __attribute__((noinline)) __time_critical_func(reverse_sector_pair)(uint32_t *s1, uint32_t *s2) {
    for (int i = 0; i < 128; i++) {
        uint32_t tmp = s1[i];
        s1[i] = s2[127 - i];
        s2[127 - i] = tmp;
    }
}

#define IMAGE_DATA_K 126
#define AUDIO_BUFFER_K 6

#define IMAGE_DATA_WORDS (IMAGE_DATA_K * 256)
// todo see where we write off the end of this (hence need for + 128)
uint32_t image_data[
        128 + IMAGE_DATA_K * 1024 / 4] = {1}; // force into data not BSS
#define NUM_AUDIO_BUFFERS 2
uint32_t audio_buffer[AUDIO_BUFFER_K * NUM_AUDIO_BUFFERS * 1024 / 4] = {1};
uint32_t *const audio_buffer_start[NUM_AUDIO_BUFFERS] = {
        audio_buffer,
        audio_buffer + AUDIO_BUFFER_K * 256,
#if NUM_AUDIO_BUFFERS > 2
        audio_buffer + AUDIO_BUFFER_K * 512
#endif
};

extern const uint8_t atlantis_glyph_bitmap[];
extern const uint8_t atlantis_glyph_widths[];
#define menu_glypth_bitmap atlantis_glyph_bitmap
#define menu_glyph_widths atlantis_glyph_widths

#define MENU_GLYPH_MIN 32
#define MENU_GLYPH_COUNT 95
#define MENU_GLYPH_MAX (MENU_GLYPH_MIN + MENU_GLYPH_COUNT - 1)
#define MENU_GLYPH_HEIGHT 9
#define MENU_GLYPH_Y_OFFSET 2
#define MENU_GLYPH_ADVANCE 1

#define OVERLAY_WIDTH 80
#define OVERLAY_HEIGHT (17 + MENU_GLYPH_HEIGHT)
#define OVERLAY_X ((160 - OVERLAY_WIDTH) / 2)
#define OVERLAY_END 110
#define OVERLAY_START (OVERLAY_END - OVERLAY_HEIGHT)
static uint32_t overlay[OVERLAY_WIDTH * OVERLAY_HEIGHT * 2];

struct audio_buffer *audio_buffers[NUM_AUDIO_BUFFERS];
struct audio_buffer_pool *audio_buffer_pool;

#define MOVIE_ROWS 120
// +1 so we can tell full from empty
#define ROW_OFFSET_CIRCLE_SIZE (MOVIE_ROWS * 2 + 1 + 10)
static uint16_t row_buffer_offsets[ROW_OFFSET_CIRCLE_SIZE];

static uint row_wrap_add(uint a, uint b) {
    assert(a < ROW_OFFSET_CIRCLE_SIZE && b <= MOVIE_ROWS);
    a += b;
    if (a >= ROW_OFFSET_CIRCLE_SIZE) a -= ROW_OFFSET_CIRCLE_SIZE;
    return a;
}

static uint row_wrap_sub(uint a, uint b) {
    assert(a < ROW_OFFSET_CIRCLE_SIZE && b < ROW_OFFSET_CIRCLE_SIZE);
    a += ROW_OFFSET_CIRCLE_SIZE - b;
    if (a >= ROW_OFFSET_CIRCLE_SIZE) a -= ROW_OFFSET_CIRCLE_SIZE;
    return a;
}

#ifdef ENABLE_STRICT_ASSERTIONS
uint16_t ram_buffer_owning_row[RAM_BUFFER_K*1024 / 4] = {1};
#endif

static inline void set_owning_row(__unused uint from, __unused uint count, __unused uint16_t row) {
#ifdef ENABLE_STRICT_ASSERTIONS
    static uint16_t last_owning_row;
    if (row != (uint16_t)-1)
    {
        assert(row == last_owning_row || row == row_wrap_add(last_owning_row, 1));
        last_owning_row = row;
    }

    popcorn_debug("set owning row %04x->%04x %d\n", from, from+count, row);
    for(int x = 0; x < count; x++)
    {
        // todo mark as unread once we track read amounts better.
        ram_buffer_owning_row[from + x] = row; // 0x4000|row; // we mark this way as unread yet
    }
#endif
}

// + 1 for null end marker, and +1 for split buffer during wrap
static uint32_t scatter[(PICO_SD_MAX_BLOCK_COUNT + 2) * 4];

static uint32_t frame_header_sector[128];

struct frame_header {
    uint32_t mark0;
    uint32_t mark1;
    uint32_t magic;
    uint8_t major, minior, debug, spare;
    uint32_t sector_number; // relative to start of stream
    uint32_t frame_number;
    uint8_t hh, mm, ss, ff; // good old CD days (bcd)
    uint32_t header_words;
    uint16_t width;
    uint16_t height;
    uint32_t image_words;
    uint32_t audio_words; // just to confirm really
    uint32_t audio_freq;
    uint8_t audio_channels; // always assume 16 bit
    uint8_t pad[3];
    uint32_t unused[4]; // little space for expansion
    uint32_t total_sectors;
    uint32_t last_sector;
    // 1, 2, 4, 8 frame increments
    uint32_t forward_frame_sector[4];
    uint32_t backward_frame_sectors[4];
    // h/2 + 1 row_offsets, last one should == image_words
    uint16_t row_offsets[];
} __attribute__((packed));

static struct decoder_state_state {
    enum {
        INIT,
        NEW_FRAME,
        NEED_FRAME_HEADER_SECTOR,
        READING_FRAME_HEADER_SECTOR,
        NEED_VIDEO_SECTORS,
        PREPPING_VIDEO_SECTORS,
        READING_VIDEO_SECTORS,
        AWAIT_AUDIO_BUFFER,
        NEED_AUDIO_SECTORS,
        READING_AUDIO_SECTORS,
        AUDIO_BUFFER_READY,
        POST_PROCESSING_AUDIO_SECTORS,
        ERROR,
        FRAME_READY,
        HIT_END,
    } state;
    struct {
        uint32_t sector_base;
        uint16_t frame_base_row;
        uint16_t write_buffer_offset;
        uint16_t remaining_row_words;
        // todo we should combine these
        uint16_t frame_row_count;
        uint16_t row_index;
    } video_read, video_read_rollback;
    struct {
        uint32_t sector_base;
        uint16_t sector_count;
    } current_sd_read;
    struct {
        uint16_t valid_from_row;
        uint16_t valid_to_row;
        // we display from frame_start to valid_to_row, and then wrap back prior to display_start (i.e.
        // remaining data from previous frame in a pinch)...
        // todo right now we always try and keep MOVIE_ROWS valid lines ending at valid_to_row
        uint16_t display_start_row;
    } rows;
    struct {
        volatile enum {
            BS_EMPTY, BS_FILLING, BS_QUEUED
        } buffer_state[2];
        uint load_thread_buffer_index;
        uint32_t sector_base;
    } audio;
    bool hold_frame;
    bool paused;
    uint8_t unpause;
    bool awaiting_first_frame;
    uint32_t display_time_code;
    bool loaded_audio_this_frame;
} ds;

uint32_t last_display_time_code = -1;

static uint32_t waste[128]; // todo we can move this to no write thru XIP cache alias

void popcorn_producer_pool_give_buffer(struct audio_connection *connection, struct audio_buffer *buffer) {
    // hand directly to consumer
    queue_full_audio_buffer(connection->consumer_pool, buffer);
}

void popcorn_consumer_pool_give_buffer(struct audio_connection *connection, struct audio_buffer *buffer) {
    for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
        if (buffer == audio_buffers[i]) {
            DEBUG_PINS_CLR(audio_buffering, i + 1);
            ds.audio.buffer_state[i] = BS_EMPTY;
            return;
        }
    }

    __breakpoint();
    __builtin_unreachable();
}

static struct audio_connection popcorn_passthru_connection = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = popcorn_consumer_pool_give_buffer,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = popcorn_producer_pool_give_buffer,
};

#define PLATYPUS_MAGIC (('T'<<24)|('A'<<16)|('L'<<8)|'P')

static inline bool row_index_in_range(uint index, uint from, uint to) {
    if (from <= to) {
        return index >= from && index < to;
    } else {
        return index >= from || index < to;
    }
}

static uint peek_upcoming_row(struct frame_header *head, int ahead) {
    if (ds.video_read.frame_row_count + ahead >= MOVIE_ROWS) {
        return 0;
    }
    return head->row_offsets[ds.video_read.frame_row_count + ahead + 1] -
           head->row_offsets[ds.video_read.frame_row_count + ahead];
}

struct gpt_entry {
    uint64_t ptype1, ptype2;
    uint64_t guid1, guid2;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t u_name[36];
};

#pragma GCC push_options
#pragma GCC optimize("O3")

static void __attribute__((noinline)) update_audio_sector_volume(uint32_t *_samples) {
    int16_t *samples = (int16_t *) _samples;
    for (int i = 0; i < 256; i++) {
        samples[i] = (samples[i] * volume) >> 8;
    }
}

#pragma GCC pop_options

static void __time_critical_func(handle_prep_video_sectors)(struct frame_header *head) {
    bool done = false;
    // we are making a decision about how many sectors we can read (we read up to MAX_BLOCK_COUNT - 1) in case one is split
    uint32_t *p = scatter;
    int sector_count = 0;
    uint32_t part1_buffer_offset;
    uint32_t part1_size;
    // note part2 is always loaded at ram offset 0
    uint32_t part2_size;
    uint buffer_offset_limit = row_buffer_offsets[ds.rows.valid_from_row];
    uint frame_row_count_limit = row_wrap_sub(ds.rows.valid_from_row,
                                              row_wrap_add(ds.video_read.frame_base_row, 1));

    // constraint 1) we must have enough space for the scatter information (part1 + (part2) + CRC) * 2 = 6
    while (sector_count < PICO_SD_MAX_BLOCK_COUNT && p < scatter + count_of(scatter) - 6) {
        uint words_until_wrap = IMAGE_DATA_WORDS - ds.video_read.write_buffer_offset;
        bool may_wrap = buffer_offset_limit < ds.video_read.write_buffer_offset ||
                        ds.rows.valid_from_row == ds.rows.valid_to_row; // (latter is empty buffer)
        uint linear_words = may_wrap ? words_until_wrap : buffer_offset_limit - ds.video_read.write_buffer_offset;

        popcorn_debug("s %d r %d val %04x ->| %04x (%d %d %d)", (uint) ds.video_read.sector_base + sector_count,
                      ds.video_read.frame_row_count,
                      ds.video_read.write_buffer_offset, buffer_offset_limit,
                      ds.rows.valid_from_row, ds.rows.display_start_row, ds.rows.valid_to_row);

        if (may_wrap) {
            popcorn_debug(" may-wrap @ %04x", IMAGE_DATA_WORDS);
        }
        popcorn_debug("\n");
        // constraint 2) we must have space for a sector - conservatively we assume if we have to wrap we need space
        // for a whole sector at the beginning of the buffer
        if (linear_words < 128 && !may_wrap) {
            popcorn_debug("    no room and can't wrap\n");
            break;
        }
        uint row_index = ds.video_read.row_index;
        if (!ds.video_read.remaining_row_words) {
            // we need a new row
            uint remaining_row_words = peek_upcoming_row(head, 0);
            row_index = row_wrap_add(ds.video_read.frame_base_row, ds.video_read.frame_row_count);
            if (!remaining_row_words) {
                done = true;
                break;
            }
            if (ds.video_read.frame_row_count == frame_row_count_limit) {
                popcorn_debug("    would start new row on sector boundary but out of rows space\n");
                break;
            }
            ds.video_read.remaining_row_words = remaining_row_words; // we can't update this prior
            popcorn_debug("    start new row on sector boundary ri %d\n", row_index);
            // we have a new row, so decide whether it goes here or after wrap
            if (ds.video_read.remaining_row_words > words_until_wrap) {
                if (ds.video_read.write_buffer_offset <= buffer_offset_limit) {
                    popcorn_debug("    too early to wrap\n");
                    break;
                }
                popcorn_debug("    moving entire new row to beginning\n");
                ds.video_read.write_buffer_offset = 0;
                words_until_wrap = linear_words = buffer_offset_limit;
                may_wrap = false;
            }
            row_buffer_offsets[row_index] = ds.video_read.write_buffer_offset;
            ds.video_read.frame_row_count++;
            ds.video_read.row_index = row_index;
        }
        assert(ds.video_read.remaining_row_words); // there should indeed be row words then
        if (ds.video_read.remaining_row_words >= 128) {
            uint this_time_words = MIN(ds.video_read.remaining_row_words, 128);
            if (linear_words < this_time_words) {
                popcorn_debug("    not enough space yet\n");
                break;
            }
            popcorn_debug("    128 of %d\n", ds.video_read.remaining_row_words);
            part1_buffer_offset = ds.video_read.write_buffer_offset;
            part1_size = 128;
            part2_size = 0;
            set_owning_row(part1_buffer_offset, part1_size, row_index);
            ds.video_read.remaining_row_words -= this_time_words;
            ds.video_read.write_buffer_offset += 128;
        } else {
            // note that here we are in the middle of a row, therefore we cannot be splitting, which is why we care
            // about linear_words
            if (linear_words < ds.video_read.remaining_row_words) {
                popcorn_debug("    not enough space yet even for remaining\n");
                break;
            }
            part1_buffer_offset = ds.video_read.write_buffer_offset;
            part1_size = ds.video_read.remaining_row_words;
            part2_size = 0;
            set_owning_row(part1_buffer_offset, part1_size, row_index);
            uint consumed = ds.video_read.remaining_row_words;
            uint saved_remaining_row_words = ds.video_read.remaining_row_words;
            ds.video_read.write_buffer_offset += ds.video_read.remaining_row_words;
            popcorn_debug("    %d remaining of ri %d, then ", ds.video_read.remaining_row_words, row_index);
            ds.video_read.remaining_row_words = 0;
            bool rollback = false;
            int rows_ahead = 0;
            while (consumed < 128 && !rollback) {
                assert(!ds.video_read.remaining_row_words);
                ds.video_read.remaining_row_words = peek_upcoming_row(head, rows_ahead);
                if (!ds.video_read.remaining_row_words) {
                    if (part2_size) {
                        part2_size += 128 - consumed;
                    } else {
                        part1_size += 128 - consumed;
                    }
                    set_owning_row(ds.video_read.write_buffer_offset, 128 - consumed, -1);
                    done = true;
                    break; // end of frame
                }
                if ((ds.video_read.frame_row_count + rows_ahead) >= frame_row_count_limit) {
                    assert((ds.video_read.frame_row_count + rows_ahead) == frame_row_count_limit);
                    popcorn_debug("    out of rows space; rollback!\n");
                    rollback = true;
                } else {
                    uint to_consume = MIN(128 - consumed, ds.video_read.remaining_row_words);
                    row_index = row_wrap_add(ds.video_read.frame_base_row,
                                             ds.video_read.frame_row_count + rows_ahead);
                    if (consumed + ds.video_read.remaining_row_words > words_until_wrap) {
                        // this row will have to wrap anyway
                        if (!may_wrap || (ds.video_read.remaining_row_words >= buffer_offset_limit &&
                                          buffer_offset_limit < 128)) {
                            popcorn_debug("not enough space to wrap .... rollback!");
                            rollback = true;
                        } else {
                            assert(may_wrap);
                            popcorn_debug("wrap, and ");
                            assert(!part2_size);
                            part2_size += to_consume;
                            ds.video_read.write_buffer_offset = 0;
                            set_owning_row(ds.video_read.write_buffer_offset, to_consume, row_index);
                            words_until_wrap = linear_words = buffer_offset_limit;
                            may_wrap = false;
                        }
                    } else {
                        if (part2_size) {
                            // if we've already wrapped, then we need to add to that
                            part2_size += to_consume;
                        } else {
                            part1_size += to_consume;
                        }
                        set_owning_row(ds.video_read.write_buffer_offset, to_consume, row_index);
                    }
                    if (!rollback) {
                        popcorn_debug("%d from new row ri = %d(of %d)", to_consume, row_index,
                                      (uint) ds.video_read.remaining_row_words);
                        row_buffer_offsets[row_index] = ds.video_read.write_buffer_offset;
                        ds.video_read.remaining_row_words -= to_consume;
                        ds.video_read.write_buffer_offset += to_consume;
                        consumed += to_consume;
                        rows_ahead++;
                    }
                }
            }
            popcorn_debug("\n");
            if (rollback) {
                // we had to rollback
                ds.video_read.remaining_row_words = saved_remaining_row_words;
                ds.video_read.write_buffer_offset = part1_buffer_offset;
                break;
            }
            ds.video_read.frame_row_count += rows_ahead;
            ds.video_read.row_index = row_index;
        }
        // we must read a whole sector
        assert(part1_size + part2_size == 128);
        popcorn_debug("    read %d at %04x ", (uint) part1_size, (uint) part1_buffer_offset);
        *p++ = native_safe_hw_ptr(image_data + part1_buffer_offset);
        *p++ = part1_size;
        if (part2_size) {
            popcorn_debug(" and %d at 0000", (uint) part2_size);
            *p++ = native_safe_hw_ptr(image_data); // part2 always at start of buffer
            *p++ = part2_size;
        }
        popcorn_debug("\n");
        // CRC
        *p++ = native_safe_hw_ptr(waste);
        *p++ = 2;
        sector_count++;
    }
    if (sector_count) {
        *p++ = 0;
        *p++ = 0;
        assert(p <= scatter + count_of(scatter));
        ds.current_sd_read.sector_base = ds.video_read.sector_base;
        ds.current_sd_read.sector_count = sector_count;
        popcorn_debug("starting read %d secs @ %04x?(%04x) -> %04x(%04x)\n", sector_count,
                      row_buffer_offsets[row_wrap_add(ds.video_read.frame_base_row,
                                                      ds.video_read_rollback.frame_row_count)],
                      ds.video_read_rollback.write_buffer_offset,
                      row_buffer_offsets[row_wrap_add(ds.video_read.frame_base_row,
                                                      ds.video_read.frame_row_count - 1)],
                      ds.video_read.write_buffer_offset);
        sd_readblocks_scatter_async(scatter, ds.video_read.sector_base, sector_count);
        ds.state = READING_VIDEO_SECTORS;
    } else if (done) {
        if (!ds.loaded_audio_this_frame) {
            ds.state = AWAIT_AUDIO_BUFFER;
        } else {
            ds.state = FRAME_READY;
        }
    } else {
        ds.state = HIT_END;
    }
}

static void __time_critical_func(handle_audio_buffer_ready)(const struct frame_header *head) {
    DEBUG_PINS_CLR(audio_buffering, 4);
    ds.loaded_audio_this_frame = true;
    ds.audio.buffer_state[ds.audio.load_thread_buffer_index] = BS_QUEUED;
    if (!ds.paused && (playback_speed > -3 && playback_speed < 3)) {
        if (playback_forwards) {
            audio_buffers[ds.audio.load_thread_buffer_index]->buffer->bytes = (uint8_t *) audio_buffer_start[ds.audio.load_thread_buffer_index];
        } else {
            // need to offset the audio because it is now right aligned
            audio_buffers[ds.audio.load_thread_buffer_index]->buffer->bytes = (uint8_t *) (
                    audio_buffer_start[ds.audio.load_thread_buffer_index] + (127u & -head->audio_words));
        }
        DEBUG_PINS_SET(audio_buffering, ds.audio.load_thread_buffer_index + 1);
        give_audio_buffer(audio_buffer_pool, audio_buffers[ds.audio.load_thread_buffer_index]);
    } else {
        ds.audio.buffer_state[ds.audio.load_thread_buffer_index] = BS_EMPTY;
    }
    ds.audio.load_thread_buffer_index++;
    if (ds.audio.load_thread_buffer_index == NUM_AUDIO_BUFFERS) ds.audio.load_thread_buffer_index = 0;
    ds.state = NEED_VIDEO_SECTORS;
}

static void __time_critical_func(handle_need_frame_header_sector)(struct frame_header *head) {
    head->mark0 = 0; // mark as invalid
    popcorn_debug("starting scatter read %d\n", (uint) head->sector_number);
    const int sector_count = 1;
    assert(sector_count <= PICO_SD_MAX_BLOCK_COUNT);
    uint32_t *p = scatter;
    for (int i = 0; i < sector_count; i++) {
        *p++ = native_safe_hw_ptr((frame_header_sector + i * 128));
        *p++ = 128;
        // for now we read the CRCs also
        *p++ = native_safe_hw_ptr(waste);
        *p++ = 2;
    }
    *p++ = 0;
    *p++ = 0;
    ds.current_sd_read.sector_count = sector_count;
    sd_readblocks_scatter_async(scatter, ds.current_sd_read.sector_base, ds.current_sd_read.sector_count);
    ds.state = READING_FRAME_HEADER_SECTOR;
}

static void __time_critical_func(handle_reading_frame_header_sector)() {
    if (sd_scatter_read_complete(NULL)) {
        struct frame_header *head = (struct frame_header *) frame_header_sector;
        if (head->mark0 != 0xffffffff || head->mark1 != 0xffffffff || head->magic != PLATYPUS_MAGIC) {
            printf("no header found @ %d\n", (int) ds.current_sd_read.sector_base);
            ds.current_sd_read.sector_base++;
            ds.state = NEED_FRAME_HEADER_SECTOR;
        } else {
            assert(ds.current_sd_read.sector_count == 1);
            if (head->header_words > 128) {
                printf("expect 1 sector header\n");
                ds.current_sd_read.sector_base++;
                ds.state = NEED_FRAME_HEADER_SECTOR;
            } else {
                movies[registered_current_movie].current_sector = ds.current_sd_read.sector_base;
                ds.display_time_code = (head->hh << 24u) | (head->mm << 16u) | (head->ss << 8u) | (head->ff);
                ds.current_sd_read.sector_base++;
                // skip audio sectors
                ds.audio.sector_base = ds.current_sd_read.sector_base;
                ds.current_sd_read.sector_base += (head->audio_words + 127) / 128;
                ds.video_read.sector_base = ds.current_sd_read.sector_base;
                ds.video_read.frame_base_row = ds.rows.valid_to_row;
                ds.video_read.frame_row_count = 0;
                ds.state = NEED_VIDEO_SECTORS;
            }
        }
    }
}

static void handle_init(const struct frame_header *head) {
    movie_count = 1;
    movies = static_movies;
    if (sd_init_4pins() < 0) {
        ds.state = ERROR;
    } else {
        sd_readblocks_sync(image_data, 1, 1);
        const struct gpt_header {
            uint64_t signature;
            uint32_t revision;
            uint32_t size;
            uint32_t crc;
            uint32_t _pad;
            uint64_t lba;
            uint64_t backup_lba;
            uint64_t first_usable_lba;
            uint64_t last_usabble_lba;
            uint64_t guid1, guid2;
            uint64_t table_lba;
            uint32_t table_count;
            uint32_t table_entry_size;
            uint32_t table_crc;
        } __packed gpt_header = *(const struct gpt_header *) image_data;
        // todo crc
        if (gpt_header.signature == 0x5452415020494645ULL && gpt_header.size == sizeof(struct gpt_header)) {
            printf("Found GPT\n");

            int sectors = (gpt_header.table_count * gpt_header.table_entry_size + 511) / 512;
            int read_sectors = MAX(sectors, 32);
            printf("  reading %d/%d sectors starting at %d\n", read_sectors, sectors,
                   (int) gpt_header.table_lba);
            sd_readblocks_sync(image_data, gpt_header.table_lba, read_sectors);
            uint8_t *buffer = (uint8_t *) image_data;
            movie_count = 0;
            for (uint i = 0; i < gpt_header.table_count; i++) {
                struct gpt_entry *gpt_entry = (struct gpt_entry *) (buffer + i * gpt_header.table_entry_size);
                if (gpt_entry->ptype1 || gpt_entry->ptype2) {
                    sd_readblocks_sync(frame_header_sector, gpt_entry->first_lba, 1);
                    if (head->mark0 == 0xffffffff && head->mark1 == 0xffffffff ||
                        head->magic == PLATYPUS_MAGIC) {
                        movie_count++;
                    } else {
                        gpt_entry->ptype1 = gpt_entry->ptype2 = 0;
                    }
                }
            }
            if (!movie_count) {
                panic("No movies found");
            }
            movies = (struct movie *) calloc(movie_count, sizeof(struct movie));
            for (uint i = 0, j = 0; i < gpt_header.table_count; i++) {
                struct gpt_entry *gpt_entry = (struct gpt_entry *) (buffer + i * gpt_header.table_entry_size);
                if (gpt_entry->ptype1 || gpt_entry->ptype2) {
                    movies[j].start_sector = gpt_entry->first_lba;
                    movies[j].text.color = NAME_COLOR;
                    const int MAX_TEXT = 20; // random ... must be less than 36
                    char *ascii = (char *) gpt_entry->u_name;
                    for (int k = 0; k < MAX_TEXT; k++) {
                        char c = gpt_entry->u_name[k];
                        ascii[k] = c; // overwrite the string at half speed
                        if (!c) break;
                    }
                    ascii[MAX_TEXT] = 0;
                    movies[j].text.text = strdup(ascii);
                    printf("'%s' at %08x\n", movies[j].text.text, (uint) movies[j].start_sector);
                    j++;
                }
            }
        } else {
            printf("No GPT found, so assuming single movie\n");
        }
    }
    ds.audio.buffer_state[0] = ds.audio.buffer_state[1] = BS_EMPTY;
    ds.audio.load_thread_buffer_index = 0;
    ds.rows.valid_from_row = ds.rows.valid_to_row = 0;
    ds.rows.display_start_row = 0;
    ds.awaiting_first_frame = 1;
    ds.hold_frame = true;
    registered_current_movie = current_movie;
    ds.current_sd_read.sector_base = movies[current_movie].start_sector;
    ds.state = NEW_FRAME;
}

static void __time_critical_func(handle_new_frame)() {
    ds.state = NEED_FRAME_HEADER_SECTOR;
    ds.loaded_audio_this_frame = false;
}

static void __time_critical_func(handle_reading_video_sectors)() {
    assert(ds.current_sd_read.sector_count);
    if (sd_scatter_read_complete(NULL)) {
        // todo arguably we can track the read in progress to update rows as they become available
        uint row_count = ds.video_read.frame_row_count;
        // check for incomplete row
        if (ds.video_read.remaining_row_words) {
            assert(row_count);
            // todo is this correct.... seems like we have a partial row visible, or maybe a partial row invisible which is fine
            popcorn_debug("acknowledge %d words from previous\n", ds.video_read.remaining_row_words);
            row_count--;
        }
        uint __unused was = ds.rows.valid_to_row;
        ds.rows.valid_to_row = row_wrap_add(ds.video_read.frame_base_row, row_count);
        popcorn_debug("completed read %d->%d\n", was, ds.rows.valid_to_row);
        ds.video_read.sector_base += ds.current_sd_read.sector_count;
        ds.current_sd_read.sector_base = ds.video_read.sector_base;
        ds.current_sd_read.sector_count = 0;
        ds.state = NEED_VIDEO_SECTORS;
    }
}

static void __time_critical_func(handle_reading_audio_sectors)(const struct frame_header *head) {
    if (sd_scatter_read_complete(NULL)) {
        // todo we have DMA completely capable of reading backwards - seems like a strange thing to expose in any lower level API though
        //  still we could use DMA to reverse the buffers for us (although it is a bit complicated to not step on our toes)
        if (!playback_forwards || volume != 0x100) {
            total_audio_sectors = (head->audio_words + 127) / 128;
            if (total_audio_sectors & 1) {
                panic("expected even sector count");
            }
            audio_sector_pairs_to_post_process = total_audio_sectors / 2; // we do them in pairs
            ds.state = POST_PROCESSING_AUDIO_SECTORS;
        } else {
            ds.state = AUDIO_BUFFER_READY;
        }
    }
}

static void __time_critical_func(handle_frame_ready)(const struct frame_header *head) {
    // not much to do really now other than start reading data for next sector
    ds.awaiting_first_frame = false;
    uint index = playback_speed >= 0 ? MIN(playback_speed, 3) : 0;
    if (next_frame_sector_override != -1) {
        ds.current_sd_read.sector_base = next_frame_sector_override;
        next_frame_sector_override = -1;
        registered_current_movie = current_movie;
    } else {
        uint32_t next_sector;
        if (ds.paused) {
            next_sector = head->sector_number;
        } else {
            if (playback_forwards) {
                next_sector = head->forward_frame_sector[index];
            } else {
                next_sector = head->backward_frame_sectors[index];
            }
        }
        if (next_sector == 0xffffffff) {
            if (playback_forwards) {
                next_sector = 0;
            } else {
                next_sector = head->last_sector;
            }
        }
        ds.current_sd_read.sector_base = movies[registered_current_movie].start_sector + next_sector;
    }
    if (playback_speed < 0) hold_frame_count = 1 - playback_speed;
    else hold_frame_count = 1;
    ds.state = NEW_FRAME;
}

static void __time_critical_func(handle_await_audio_buffer)() {
    if (ds.audio.buffer_state[ds.audio.load_thread_buffer_index] == BS_EMPTY) {
        ds.audio.buffer_state[ds.audio.load_thread_buffer_index] = BS_FILLING;
        ds.state = NEED_AUDIO_SECTORS;
    }
}

static void __time_critical_func(handle_hit_end)() {
    if (ds.hold_frame && ds.audio.buffer_state[ds.audio.load_thread_buffer_index] == BS_EMPTY &&
!ds.loaded_audio_this_frame) {
        ds.audio.buffer_state[ds.audio.load_thread_buffer_index] = BS_FILLING;
        ds.state = NEED_AUDIO_SECTORS;
    } else {
        ds.state = NEED_VIDEO_SECTORS;
    }
}

static void __time_critical_func(handle_post_processing_audio_sectors)() {
    assert(audio_sector_pairs_to_post_process > 0);
    audio_sector_pairs_to_post_process--;
    if (!playback_forwards) {
        reverse_sector_pair(audio_buffer_start[ds.audio.load_thread_buffer_index] +
                            128 * audio_sector_pairs_to_post_process,
                            audio_buffer_start[ds.audio.load_thread_buffer_index] +
                            128 * (total_audio_sectors - 1 - audio_sector_pairs_to_post_process));
    }
    if (volume != 0x100) {
        update_audio_sector_volume(audio_buffer_start[ds.audio.load_thread_buffer_index] +
                                   128 * audio_sector_pairs_to_post_process);
        update_audio_sector_volume(audio_buffer_start[ds.audio.load_thread_buffer_index] +
                                   128 * (total_audio_sectors - 1 - audio_sector_pairs_to_post_process));
    }
    if (!audio_sector_pairs_to_post_process) {
        ds.state = AUDIO_BUFFER_READY;
    }
}

static void __time_critical_func(handle_need_audio_sectors)(const struct frame_header *head) {
    DEBUG_PINS_SET(audio_buffering, 4);
    assert(ds.audio.buffer_state[ds.audio.load_thread_buffer_index] == BS_FILLING);
    audio_buffers[ds.audio.load_thread_buffer_index]->sample_count = head->audio_words;
    // todo update sd.current_read_sector for consistency...
    //  can't do it until we pick the next frame sector explicitly rather than just happening into it.
    sd_readblocks_async(audio_buffer_start[ds.audio.load_thread_buffer_index], ds.audio.sector_base,
                        (head->audio_words + 127) / 128);
    ds.state = READING_AUDIO_SECTORS;
}

static void __time_critical_func(handle_need_video_sectors)() {
    ds.video_read_rollback = ds.video_read;
    ds.state = PREPPING_VIDEO_SECTORS;
}

static void __attribute__((noinline)) __time_critical_func(sd_state_update)() {
    struct frame_header *head = (struct frame_header *) frame_header_sector;
    switch (ds.state) {
        case NEW_FRAME:
            handle_new_frame();
            break;
        case NEED_FRAME_HEADER_SECTOR:
            handle_need_frame_header_sector(head);
            break;
        case READING_FRAME_HEADER_SECTOR:
            handle_reading_frame_header_sector();
            break;
        case READING_VIDEO_SECTORS:
            handle_reading_video_sectors();
            break;
        case INIT:
            handle_init(head);
            break;
        case ERROR:
            panic("doh!");
        case AWAIT_AUDIO_BUFFER:
            handle_await_audio_buffer();
            break;
        case HIT_END:
            handle_hit_end();
            break;
        case READING_AUDIO_SECTORS:
            handle_reading_audio_sectors(head);
            break;
        case POST_PROCESSING_AUDIO_SECTORS:
            handle_post_processing_audio_sectors();
            break;
        case FRAME_READY:
            handle_frame_ready(head);
            break;
        case NEED_VIDEO_SECTORS:
            handle_need_video_sectors();
            break;
        case NEED_AUDIO_SECTORS:
            handle_need_audio_sectors(head);
            break;
        case PREPPING_VIDEO_SECTORS:
        case AUDIO_BUFFER_READY:
            // handled below because we want to be able to do them in conjunction immediately after the above
            break;
    }
    if (ds.state == PREPPING_VIDEO_SECTORS) {
        handle_prep_video_sectors(head);
    } else if (ds.state == AUDIO_BUFFER_READY) {
        handle_audio_buffer_ready(head);
    }
}

#ifdef PLATYPUS_565
#define DARKEN_MASK 0x7bcf7bcf
#else
#define DARKEN_MASK 0x3def3def
#endif

static inline void darken(uint32_t *p, uint32_t row_delta, uint32_t *o, int32_t c) {
    int i = 0;
    do {
        p[i] = ((p[i] >> 1) & DARKEN_MASK) + o[i];
        p[row_delta + i] = ((p[row_delta + i] >> 1) & DARKEN_MASK) + o[i + OVERLAY_WIDTH];
        i++;
    } while (i < c);
}

void __attribute__((optimize("Os"))) __no_inline_not_in_flash_func(darken_a)(uint32_t *p, uint32_t row_delta,
                                                                             uint32_t *o, int32_t c) {
    darken(p, row_delta, o, c);
}

void __attribute__((optimize("Os"))) __no_inline_not_in_flash_func(darken_b)(uint32_t *p, uint32_t row_delta,
                                                                             uint32_t *o, int32_t c) {
    darken(p, row_delta, o, c);
}

void check_debug(const uint32_t *end, struct frame_header *header, uint row) {
    // hmmm... we still see this on device, but nothing on host... don't see any visual distortion
    if (false && header->debug) {
        uint8_t *b = (uint8_t *) end;
        if (b[0] != 0xaa || b[3] != row) {
            b -= 4; // we might have overshot todo fix decompress_row to always return the right value
            if (b[0] != 0xaa || b[3] != row) {
                b += 8; // we might have overshot todo fix decompress_row to always return the right value
                if (b[0] != 0xaa || b[3] != row) {
                    printf("%p %02x %02x\n", b, b[0], b[1]);
                    printf("Bad row data %08x hf %d row %d\n", (uint) *end, (int) header->frame_number, row);
                    printf("%p %04x\n", end, ((uint32_t *) b) - image_data);
                    printf("\n");
                }
            }
        }
    }
}

void set_unpause() {
    if (ds.paused) {
        ds.unpause = 3; // take care of buffered (overkill)
    }
}

void draw_glyph(struct font *font, uint32_t *dest, uint32_t c) {
    uint32_t *src = font->pixels + c * font->glyph_words;
    for (int y = 0; y < font->height; y++) {
        for (int x = 0; x < font->width_words; x++) {
            dest[x] = src[x];
        }
        dest += OVERLAY_WIDTH;
        src += font->width_words;
    }
}

uint render_font_line(const struct text_element *element, uint16_t *out, const uint8_t *bitmaps, uint16_t color) {
    const char *p = element->text;
    uint32_t acc = 0;
    uint8_t c;
    int bits = 0;
    uint16_t *o = out;
    while ((c = *p++)) {
        c -= MENU_GLYPH_MIN;
        if (c < MENU_GLYPH_MAX) {
            int cbits = menu_glyph_widths[c] + 1;
            if (cbits <= 8) {
                acc = (acc << cbits) | ((bitmaps[c * MENU_GLYPH_HEIGHT] >> (8 - cbits)));
            } else {
                acc = (acc << cbits) | ((bitmaps[c * MENU_GLYPH_HEIGHT] << (cbits - 8)));
            }
            bits += cbits;
            if (bits >= 24) {
                while (--bits > 0) {
                    *o++ = ((acc >> bits) & 1u) ? color : 0;
                }
                acc = 0;
            }
        }
    }
    while (--bits > 0) {
        *o++ = ((acc >> bits) & 1u) ? color : 0;
    }
    return o - out;
}

void measure_text(struct text_element *element, int max) {
    if (!element->width) {
        element->width = 0;
        if (element->text) {
            const char *p = element->text;
            uint8_t c;
            while ((c = *p++)) {
                c -= MENU_GLYPH_MIN;
                if (c < MENU_GLYPH_MAX) {
                    element->width += menu_glyph_widths[c] + MENU_GLYPH_ADVANCE;
                }
            }
            if (element->width > max) {
                panic("menu text too long");
                element->width = max;
            }
        }
    }
}

void step_forward() {
    struct frame_header *head = (struct frame_header *) frame_header_sector;
    // for now we'll force paused and regular speed
    if (!ds.paused) ds.paused = true;
    playback_speed = 0;
    if (ds.paused && 0xffffffff != head->forward_frame_sector[0]) {
        next_frame_sector_override = movies[registered_current_movie].start_sector + head->forward_frame_sector[0];
        set_unpause();
    }
}

void step_backward() {
    struct frame_header *head = (struct frame_header *) frame_header_sector;
    // for now we'll force paused and regular speed
    if (!ds.paused) ds.paused = true;
    playback_speed = 0;
    if (ds.paused && 0xffffffff != head->backward_frame_sectors[0]) {
        next_frame_sector_override = movies[registered_current_movie].start_sector + head->backward_frame_sectors[0];
        set_unpause();
    }
}

void previous_movie() {
    if (current_movie) {
        current_movie--;
    } else {
        current_movie = movie_count - 1;
    }
    display_base_frame = scanvideo_frame_number(scanvideo_get_next_scanline_id());
}

void next_movie() {
    current_movie++;
    if (current_movie == movie_count) current_movie = 0;
    display_base_frame = scanvideo_frame_number(scanvideo_get_next_scanline_id());
}

void volume_down() { volume = MAX(volume - 8, 0); }

void volume_up() { volume = MIN(volume + 8, 0x100); }

void __attribute__((noreturn)) __time_critical_func(render_loop)() {
    static volatile int32_t last_scanline_id[2];
    static uint32_t last_frame_num[2] = {-1, -1};
    static uint32_t core_1_last_frame_num = -1;

    uint core_num = get_core_num();
    assert(core_num >= 0 && core_num < 2);

    printf("Rendering on core %d\r\n", core_num);
    while (true) {
        struct scanvideo_scanline_buffer *sb[2];
        sb[0] = scanvideo_begin_scanline_generation_linked(2, true);
        sb[0]->link_after = 2;
        sb[1] = sb[0]->link;
        struct scanvideo_scanline_buffer *scanline_buffer = sb[0];
        uint this_display_start_row;
        // do any frame related logic
        mutex_enter_blocking(&frame_logic_mutex);
        uint frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        bool core_0_beat_core_1_to_new_frame = false;
        // need to do this for whichever core got here first
        // note that with multiple cores we may have got here not for the first scanline, however one of the cores will do this logic first before either does the actual generation
        if (frame_num != last_frame_num[core_num]) {
            // this could should be during vblank as we try to create the next line
            // todo should we ignore if we aren't attempting the next line
            last_frame_num[core_num] = frame_num;
            if (!core_num) {
                if (show_menu) {
                    if (ds.display_time_code != last_display_time_code) {
                        uint32_t c = ds.display_time_code;
                        uint32_t *o = overlay + OVERLAY_WIDTH * 2 + 18;
                        for (int n = 0; n < 6; n++) {
                            draw_glyph(font18, o, (c >> 28) + 1);
                            c = c << 4;
                            o += font18->width_words;
                            if (n == 1 || n == 3) {
                                draw_glyph(font18, o, 11); // :
                                o += 2;
                            } else if (n == 5) {
                                draw_glyph(font18, o, 0); // .
                                o += 2;
                            }
                        }
                        o += (lcd18.line_height - lcd12.line_height) * OVERLAY_WIDTH;
                        for (int n = 0; n < 2; n++) {
                            draw_glyph(font12, o, (c >> 28) + 1);
                            c = c << 4;
                            o += font12->width_words;
                        }
                        last_display_time_code = ds.display_time_code;
                    }
                    text_roller = 0;
                }
                if (frame_num != core_1_last_frame_num) {
                    core_0_beat_core_1_to_new_frame = true;
                }
            }
        }

        bool this_hold_frame = ds.hold_frame;
        if (core_num) {
            if (frame_num != core_1_last_frame_num) {
                core_1_last_frame_num = frame_num;
                handle_input();

                if (ds.hold_frame) {
                    if ((!ds.paused && --remaining_hold_frames <= 0) || ds.unpause) {
                        if (ds.unpause) ds.unpause--;
                        ds.hold_frame = false;
                        remaining_hold_frames = hold_frame_count;
                    } else {
                        popcorn_debug("======> unexpected %d\n", remaining_hold_frames);
                    }
                } else if (!ds.awaiting_first_frame) {
                    uint new_frame = row_wrap_add(ds.rows.display_start_row, MOVIE_ROWS);
                    if (!row_index_in_range(new_frame, ds.rows.valid_from_row, ds.rows.valid_to_row)) {
                        printf("%d frame not ready %d %d->%d %d valid %04x:%04x\n", (uint) ds.video_read.sector_base,
                               ds.rows.valid_from_row, ds.rows.display_start_row, new_frame, ds.rows.valid_to_row,
                               row_buffer_offsets[ds.rows.valid_from_row], ds.video_read.write_buffer_offset);
                        ds.state = INIT;
                    } else {
                        popcorn_debug("frame switch %d (%d->%d) %d %d+%d.%d\n", ds.rows.valid_from_row,
                                      ds.rows.display_start_row, new_frame, ds.rows.valid_to_row,
                                      ds.video_read.frame_base_row, ds.video_read.frame_row_count,
                                      ds.video_read.remaining_row_words);
                        ds.rows.display_start_row = new_frame;
                        this_hold_frame = ds.hold_frame = true;
                    }
                }
            }

            if (!ds.awaiting_first_frame) {
                int first_must_keep_row;
                uint32_t other_core_scanline_id = last_scanline_id[0];
                uint other_core_frame_number = scanvideo_frame_number(other_core_scanline_id);
                if (this_hold_frame) {
                    popcorn_debug("%d chase reason hold_frame lsi %08x\n", ds.state, (uint) last_scanline_id[1]);
                    if (frame_num == other_core_frame_number) {
                        first_must_keep_row = 0;
                    } else {
                        first_must_keep_row = -1;
                    }
                } else {
                    if (frame_num == other_core_frame_number) {
                        first_must_keep_row = MIN(scanvideo_scanline_number(other_core_scanline_id),
                                                  scanvideo_scanline_number(last_scanline_id[1]));
                    } else if (other_core_frame_number == (uint16_t) (frame_num + 1)) {
                        first_must_keep_row = scanvideo_scanline_number(last_scanline_id[1]);
                    } else {
                        first_must_keep_row = scanvideo_scanline_number(last_scanline_id[1]);
                        // if we are starving out core 0, then it may be stuck behind us... if we've made it SCANLINE_BUFFER/2 rows in
                        // then the old frame must be done now
                        if (first_must_keep_row <= PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT / 2) {
                            first_must_keep_row = -1;
                        }
                    }
                    popcorn_debug("%d chase reason lsi %08x oc %08x fn %d ocfn %d behind %d, %d %d %d\n", ds.state,
                                  (uint) last_scanline_id[1],
                                  (uint) other_core_scanline_id, frame_num, other_core_frame_number,
                                  first_must_keep_row,
                                  ds.rows.valid_from_row, ds.rows.display_start_row, ds.rows.valid_to_row);
                }
                if (first_must_keep_row >= 0) {
                    uint last_displayed_row = row_wrap_add(ds.rows.display_start_row, first_must_keep_row);
                    if (row_index_in_range(last_displayed_row, ds.rows.valid_from_row, ds.rows.valid_to_row)) {
                        popcorn_debug("%d zoom %d %d->%d %d %d\n", ds.state, this_hold_frame,
                                      ds.rows.valid_from_row, last_displayed_row, ds.rows.display_start_row,
                                      ds.rows.valid_to_row);
                        ds.rows.valid_from_row = last_displayed_row;
                    } else {
                        popcorn_debug("UNDERRAN ROWS\n");
                    }
                } else {
                    popcorn_debug("NO FIRST KEEP ROW\n");
                }
            }
            sd_state_update();
        } else if (show_menu) {
            if (text_roller < MENU_GLYPH_HEIGHT) {
                static struct text_element *last_element;
                static struct text_element *element;
                int delta = frame_num - display_base_frame;
                static int last_delta = -1252352;
                if (delta != last_delta || !element) {
                    last_delta = delta;
                    uint count = hw_divider_u32_remainder_inlined(delta >> 8, DISPLAY_NAME_AFTER_FRAME_COUNT + 1);
                    if (count) {
                        static int idx = 0;
                        element = &instructions[idx];
                        if (0xff == (delta & 0xff)) {
                            if (++idx == count_of(instructions)) idx = 0;
                        }
                    } else {
                        element = &movies[registered_current_movie].text;
                    }
                }
                if (text_roller || element != last_element) {
                    uint16_t *out = (uint16_t *) (overlay + (text_roller + 3 + lcd18.line_height) * OVERLAY_WIDTH);
                    if (!element->width) {
                        measure_text(element, OVERLAY_WIDTH * 2);
                    }
                    uint off = 3 + OVERLAY_WIDTH - element->width / 2;
                    __builtin_memset(out, 0, off * 2);
                    uint len = render_font_line(element, out + off, menu_glypth_bitmap + text_roller, element->color);
                    if (off + len < OVERLAY_WIDTH * 2) {
                        __builtin_memset(out + off + len, 0, (OVERLAY_WIDTH * 2 - off - len) * 2);
                    }
                    text_roller++;
                }
                if (text_roller == MENU_GLYPH_HEIGHT) {
                    last_element = element;
                }
            }
        }

        // we must latch this now under lock so we have the right value on core 0 (core 1 may change it afterwards)
        this_display_start_row = ds.rows.display_start_row;
        mutex_exit(&frame_logic_mutex);
        uint16_t scanline_num = scanvideo_scanline_number(sb[0]->scanline_id);
        if (!core_num && core_0_beat_core_1_to_new_frame) {
            // we just do a simple hack to move to the new frame... core 0 does not maintain locks sufficient to interact with core 1 state.
            // this is reasonable as we are about to start drawing the frame anyway.
            this_display_start_row = row_wrap_add(this_display_start_row, MOVIE_ROWS);
        }

        DEBUG_PINS_SET(frame_generation, (core_num) ? 2 : 4);
        uint16_t *buf16_0 = (uint16_t *) sb[0]->data;
        uint16_t *buf16_1 = (uint16_t *) sb[1]->data;
        uint row_number = scanline_num;
        uint row_index = row_wrap_add(this_display_start_row, row_number);
        bool row_valid = row_index_in_range(row_index, ds.rows.valid_from_row, ds.rows.valid_to_row);
        uint pos;
        if (row_valid) {
            if (row_index >= ROW_OFFSET_CIRCLE_SIZE) {
                printf("INVALID STATE rn %d+%d %d %d %d\n", this_display_start_row, row_number, ds.rows.valid_from_row, row_index,
                       ds.rows.valid_to_row);
            }
            assert(row_index < ROW_OFFSET_CIRCLE_SIZE);
            assert(row_buffer_offsets[row_index] < IMAGE_DATA_WORDS);
            const uint32_t *compressed_scanline = image_data + row_buffer_offsets[row_index];
            const int w = 320;
            const __unused uint32_t *end;
            if (core_num) {
                end = platypus_decompress_row_b(sb[0]->data + 1, sb[1]->data + 1, compressed_scanline, w);
                __unused struct frame_header *header = (struct frame_header *) frame_header_sector;
                check_debug(end, header, row_number);
                if (show_menu && row_number >= OVERLAY_START && row_number < OVERLAY_END) {
                    darken_a(sb[0]->data + OVERLAY_X, sb[1]->data - sb[0]->data,
                             overlay + (row_number - OVERLAY_START) * OVERLAY_WIDTH, OVERLAY_WIDTH);
                }
            } else {
                end = platypus_decompress_row_a(sb[0]->data + 1, sb[1]->data + 1, compressed_scanline, w);
                __unused struct frame_header *header = (struct frame_header *) frame_header_sector;
                check_debug(end, header, row_number);
                if (show_menu && row_number >= OVERLAY_START && row_number < OVERLAY_END) {
                    darken_b(sb[0]->data + OVERLAY_X, sb[1]->data - sb[0]->data,
                             overlay + (row_number - OVERLAY_START) * OVERLAY_WIDTH, OVERLAY_WIDTH);
                }
            }
#ifdef ENABLE_STRICT_ASSERTIONS
            const uint16_t *compressed_scanline_owning_row = ram_buffer_owning_row + row_buffer_offsets[row_index];
            uint compressed_len = end - compressed_scanline;
            assert(compressed_len <= (w * 7) / 8);
            for(int x = 0; x < compressed_len; x++) {
                assert(compressed_scanline_owning_row[x] == row_index);
            }
#endif
            buf16_0[1] = buf16_0[2];
            buf16_1[1] = buf16_1[2];
            buf16_0[2] = buf16_1[2] = w - 3;
            buf16_0[0] = buf16_1[0] = COMPOSABLE_RAW_RUN;
            pos = w + 2;
        } else {
            pos = 0; // blank display
            buf16_0[pos] = buf16_1[pos] = COMPOSABLE_RAW_1P;
            pos++;
            buf16_0[pos] = buf16_1[pos] = 0x7c1f;
            pos++;
        }
        {
            buf16_0[pos] = buf16_1[pos] = COMPOSABLE_RAW_1P;
            pos++;
            buf16_0[pos] = buf16_1[pos] = 0;
            pos++;
            buf16_0[pos] = buf16_1[pos] = COMPOSABLE_EOL_SKIP_ALIGN;
            pos++;
            buf16_0[pos] = buf16_1[pos] = 0xffff;
            pos++;
            assert(!(pos & 1u));
            sb[0]->data_used = sb[1]->data_used = pos / 2;
        }
        sb[0]->status = sb[1]->status = SCANLINE_OK;
        DEBUG_PINS_CLR(frame_generation, (core_num) ? 2 : 4);
        last_scanline_id[core_num] = sb[0]->scanline_id;
        scanvideo_end_scanline_generation(sb[0]); // sb[1] is linked
    }
}

void handle_input() {
    uint old_movie = current_movie;
#if USE_VGABOARD_BUTTONS
    for (uint b = 0; b < 3; b++) {
        uint m = 1u << b;

        static int SHORT_BUTTON = 300000;
        static int MEDIUM_BUTTON = 850000;
        static int REPEAT = 125000;

        if (!(button_state & m) && (last_button_state & m)) {
            // button released
            int32_t length = time_us_32() - last_button_time[b];
            if (length < SHORT_BUTTON) {
                switch (b) {
                    case 0:
                        if (!ds.paused) {
                            if (playback_speed > -16) playback_speed--;
                        } else {
                            step_backward();
                        }
                        break;
                    case 1:
                        ds.paused ^= 1;
                        playback_speed = 0;
                        break;
                    case 2:
                        if (!ds.paused) {
                            if (playback_speed < 4) playback_speed++;
                        } else {
                            step_forward();
                        }
                        break;
                }
            } else if (length < MEDIUM_BUTTON) {
                switch (b) {
                    case 0:
                        previous_movie();
                        break;
                    case 1:
                        show_menu ^= 1;
                        break;
                    case 2:
                        next_movie();
                        break;
                }
            } else {
                switch (b) {
                    case 1:
                        playback_forwards ^= 1u;
                        break;
                }
            }
        } else if ((button_state & m) && !(last_button_state & m)) {
            last_button_time[b] = time_us_32();
        } else if ((button_state & m) && (last_button_state & m)) {
            // button held
            if (b != 1) {
                int32_t length = time_us_32() - last_button_time[b];
                if (length >= MEDIUM_BUTTON + REPEAT) {
                    if (b == 0) volume_down(); else volume_up();
                    last_button_time[b] += REPEAT;
                }
            }
        }
    }
    last_button_state = button_state;
#endif
#if USE_UART_INPUT
    if (uart_is_readable(uart_default))
        {
            char c = uart_getc(uart_default);
            if (c>='0' && c<='9') {
                if (c== '0')
                {
                    ds.paused = true;
                } else if (c== '1') {
                    playback_speed = 0;
                    ds.paused = false;
                } else if (c== '2') {
                    playback_speed = 1;
                    ds.paused = false;
                } else if (c== '3') {
                    playback_speed = 2;
                    ds.paused = false;
                } else if (c== '4')
                {
                    playback_speed = 3;
                    ds.paused = false;
                }
            } else if (c == 'x') {
                static bool off;
                off = !off;
                int func =off ? GPIO_FUNC_SIO : GPIO_FUNC_PIO0;
                gpio_set_function(0, func);
                gpio_set_function(6, func);
                gpio_set_function(11, func);
            } else if (c == 'r') {
                playback_forwards ^= 1u;
            } else if (c == '=' || c=='+') {
                if (playback_speed < 3) playback_speed++;
            } else if (c== '-' || c=='_') {
                if (playback_speed > -16) playback_speed--;
            } else if (c == '[') {
                volume_down();
            } else if (c== ']') {
                volume_up();
            } else if (c == ' ') {
                ds.paused = !ds.paused;
            } else if (c == 'p' || c == '[' || c == '{') {
                previous_movie();
            } else if (c == 'n' || c==']' || c == '}') {
                next_movie();
            } else if (c=='i' || c=='\r') {
                show_menu = !show_menu;
                display_base_frame = scanvideo_frame_number(scanvideo_get_next_scanline_id());
            } else if (c=='.') {
                step_forward();
            } else if (c==',') {
                step_backward();
            }
        }
#endif
    if (old_movie != current_movie) {
        next_frame_sector_override = movies[current_movie].current_sector;
        if (next_frame_sector_override < movies[current_movie].start_sector) {
            next_frame_sector_override = movies[current_movie].start_sector;
        }
        set_unpause();
    }
}

void setup_video() {
    scanvideo_setup(&vga_mode);
    scanvideo_timing_enable(true);
    sem_release(&video_setup_complete);
}

void core1_func() {
    init_core(1);
    sem_acquire_blocking(&video_setup_complete);
    render_loop();
}

void init_core(int core) {
#if PICO_ON_DEVICE
    platypus_decompress_configure_interp(core);
#endif
    if (core) {
        audio_i2s_set_enabled(true);
    }
}

void setup_audio() {
    struct audio_format platypus_audio_format = {
            .format = AUDIO_BUFFER_FORMAT_PCM_S16,
            .sample_freq = 44100,
            .channel_count = 2,
    };

    struct audio_buffer_format producer_format = {
            .format = &platypus_audio_format,
            .sample_stride = 4
    };

    struct audio_i2s_config config = {
            .data_pin = PICO_AUDIO_I2S_DATA_PIN,
            .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
            .dma_channel = 1,
#if PICO_AUDIO_I2S_PIO == 0
            .pio_sm = 2,
#else
            .pio_sm = 3,
#endif
    };

    audio_buffer_pool = audio_new_producer_pool(&producer_format, 0, 0);
    for (int i = 0; i < NUM_AUDIO_BUFFERS; i++) {
        audio_buffers[i] = audio_new_wrapping_buffer(&producer_format,
                                                     pico_buffer_wrap((uint8_t *) audio_buffer_start[i],
                                                                      AUDIO_BUFFER_K * 1024));
    }

    const struct audio_format *output_format;
    output_format = audio_i2s_setup(&platypus_audio_format, &config);
    if (!output_format) {
        panic("Unable to open audio device.");
    }

    bool ok = audio_i2s_connect_thru(audio_buffer_pool, &popcorn_passthru_connection);
    if (!ok) {
        panic("Failed to connect audio");
    }
}

int main(void) {
#if PICO_SCANVIDEO_48MHZ
    set_sys_clock_48mhz();
#else
    set_sys_clock_khz(50000, true);
#endif

#if USE_RGB_LOW_FOR_DEBUG_PINS
    gpio_dir_out_mask(0x421);
#endif

#if USE_UART_INPUT
    setup_default_uart();
#endif

#if USE_VGABOARD_BUTTONS
    vga_board_init_buttons();
#endif

    font12 = build_font(&lcd12, 4);
    font18 = build_font(&lcd18, 5);

#ifdef ENABLE_STRICT_ASSERTIONS
    memset(ram_buffer_owning_row, 0xee, sizeof(ram_buffer_owning_row));
#endif

    mutex_init(&frame_logic_mutex);
    sem_init(&video_setup_complete, 0, 1);

    ds.state = INIT;
    ds.awaiting_first_frame = true;
    init_core(0);

    setup_audio();
    multicore_launch_core1(core1_func);
    setup_video();

    // run render loop on core 0 (it is also running on core 1)
    render_loop();
}