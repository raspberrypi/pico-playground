/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RENDER_SPANS_H
#define _RENDER_SPANS_H

#include "image.h"

// ----------------------------------------------------------------------------
// 4bit1 encoding (vogon) - data is paletted and as such may contain alpha
//
// note changing this affects decoder since it is subtracted from length
#define MIN_COLOR_SPAN_4BIT 5
#define MIN_RAW_SPAN_4BIT 4
enum vogon_commands {
    END_OF_LINE = 0,
    RAW_PIXELS_SHORT = 0x40,
    COLOR_PIXELS_SHORT = 0x80,
    SINGLE_PIXEL = 0xc0,
    RAW_PIXELS_LONG = 0xd0,
    COLOR_PIXELS_LONG = 0xd1
};

enum {
    SPAN_SOLID,
    SPAN_4BIT_VOGON_OPAQUE, // vogon data but using a solid color palette
    SPAN_4BIT_RAW,
    SPAN_8BIT_RAW
};

struct span {
    struct span *next;
    short_flags flags;
    uint16_t width; // count of displayed pixels
    uint8_t type;
    union {
        struct {
            uint16_t color16;
        } solid;
        struct {
            uint16_t clip_left; // > 0 to clip pixels off the left
            uint16_t content_width; // pixel width of the original content
            struct palette16 *palette;
            const uint8_t *data;
            uint16_t data_length;
        } vogon, raw_4bit, raw_8bit;
    };
};

extern int32_t render_spans(uint32_t *render_spans_buffer, size_t max_words, struct span *head, int width);
extern int32_t single_color_scanline(uint32_t *buf, size_t buf_length, int width, uint32_t color16);
extern void init_solid_color_span(struct span *span, uint16_t width, uint16_t color16, struct span *prev);
extern void init_vogon_4bit_span(struct span *span, uint16_t width, const uint8_t *encoding, uint16_t encoded_size,
                                 struct palette16 *palette, struct span *prev);
extern void set_solid_color_span_color(struct span *span, uint16_t color16);
extern void set_vogon_4bit_span_encoding(struct span *span, const uint8_t *data, uint16_t data_length);
extern void set_vogon_4bit_clipping(struct span *span, int clip_left, int display_width);

#endif //CONVERT_SPANS_H
