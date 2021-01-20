/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdlib.h>
#include "image.h"
#include "pico/scanvideo.h"

struct palette16 *blend_palette(const struct palette32 *source, uint32_t back_color) {
    struct palette16 *dest = (struct palette16 *) malloc(sizeof(struct palette16) + source->size * sizeof(uint16_t));
    dest->flags =
            CF_PALETTE_COMPOSITED | (source->flags & ~(CF_HAS_SEMI_TRANSPARENT | CF_HAS_TRANSPARENT)) | CF_HAS_OPAQUE;
    dest->composited_on_color = back_color;
    dest->size = source->size;
    uint32_t __unused ba = (back_color >> 24) & 0xff;
    uint32_t bb = (back_color >> 16) & 0xff;
    uint32_t bg = (back_color >> 8) & 0xff;
    uint32_t br = (back_color >> 0) & 0xff;
    assert(ba == 255); // expect to be on an opaque color
    for (int i = 0; i < source->size; i++) {
        uint32_t fore_color = source->entries[i];
        uint32_t fa = (fore_color >> 24) & 0xff;
        uint32_t fb = (fore_color >> 16) & 0xff;
        uint32_t fg = (fore_color >> 8) & 0xff;
        uint32_t fr = (fore_color >> 0) & 0xff;
        if (!i && !fa) {
            // even though we don't record alpha in the blended palette, we may care to use a color key (of 0)
            dest->flags |= CF_PALETTE_INDEX_0_TRANSPARENT;
        }
        if (fa == 255) fa = 256;
        fb = (fa * fb + (256 - fa) * bb) >> 11;
        fg = (fa * fg + (256 - fa) * bg) >> 11;
        fr = (fa * fr + (256 - fa) * br) >> 11;
        dest->entries[i] = PICO_SCANVIDEO_PIXEL_FROM_RGB5(fr, fg, fb);
    }
    return dest;
}
