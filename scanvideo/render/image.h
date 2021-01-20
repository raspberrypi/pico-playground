/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RENDER_IMAGE_H
#define _RENDER_IMAGE_H

#include "pico.h"

typedef uint16_t short_flags;

// common flags
#define CF_HAS_OPAQUE ((short_flags)1)
#define CF_HAS_SEMI_TRANSPARENT ((short_flags)2)
#define CF_HAS_TRANSPARENT ((short_flags)4)
#define CF_PALETTE_INDEX_0_TRANSPARENT ((short_flags)8)
#define CF_PALETTE_COMPOSITED ((short_flags)16)

#define CF_OPACITY_MASK (CF_HAS_OPAQUE | CF_HAS_SEMI_TRANSPARENT | CF_HAS_TRANSPARENT)

struct palette32 {
    uint16_t size;
    short_flags flags;
    uint32_t entries[];
};

struct palette16 {
    uint16_t size;
    short_flags flags;
    uint32_t composited_on_color; // if flags & CF_PALETTE_COMPOSITED
    uint16_t entries[];
};

enum image_format {
    IMG_FMT_4BIT_VOGON = 1,
    IMG_FMT_8_BIT_RAW,
    IMG_FMT_16_BIT_RAW
};

struct blob {
    size_t size;
    const uint8_t *bytes;
};

struct image_data {
    int format;
    int width;
    int height;
    struct blob blob;
    const uint16_t *row_offsets; // optional and possibly initted on demand
};

struct tile_data {
    uint8_t depth;
    uint16_t count;
    uint16_t width;
    uint16_t height;
    struct blob blob;
    const uint16_t *span_numbers;
};

struct tile_data16 {
    uint16_t count;
    uint16_t width;
    uint16_t height;
    struct blob blob;
    const uint16_t *span_offsets;
};

extern struct palette16 *blend_palette(const struct palette32 *source, uint32_t back_color);

#endif //SOFTWARE_IMAGE_H
