/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include "pico.h"
#include "image.h"
#include "spans.h"
#include "pico/scanvideo/composable_scanline.h"

#ifdef __arm__
#pragma GCC push_options
#pragma GCC optimize("O3")
#endif

#ifdef ENABLE_SPAN_ASSERTIONS
#define span_assert(x) assert(x)
#else
#define span_assert(x) false
#endif

inline static void
init_span(struct span *span, uint8_t type, uint16_t flags, uint16_t visible_width, struct span *prev) {
    memset(span, 0, sizeof(struct span));
    if (prev) {
        prev->next = span;
    }
    span->flags = flags;
    span->width = visible_width;
    span->type = type;
}

void init_solid_color_span(struct span *span, uint16_t width, uint16_t color16, struct span *prev) {
    init_span(span, SPAN_SOLID, CF_HAS_OPAQUE, width, prev);
    set_solid_color_span_color(span, color16);
}

void init_vogon_4bit_span(struct span *span, uint16_t content_width, const uint8_t *encoding, uint16_t encoded_size,
                          struct palette16 *palette, struct span *prev) {
    // by default we have a clip_left of 0, and a width of content_width
    init_span(span, SPAN_4BIT_VOGON_OPAQUE, palette->flags & CF_OPACITY_MASK, content_width, prev);
    set_vogon_4bit_span_encoding(span, encoding, encoded_size);
    span->vogon.content_width = content_width;
    span->vogon.palette = palette;
    // palette should be opaque
    assert(CF_HAS_OPAQUE == (palette->flags & CF_OPACITY_MASK));
}

void __time_critical_func(set_solid_color_span_color)(struct span *span, uint16_t color16) {
    assert(span->type == SPAN_SOLID);
    span->solid.color16 = color16;
}

void __time_critical_func(set_vogon_4bit_span_encoding)(struct span *span, const uint8_t *data, uint16_t data_length) {
    assert(span->type == SPAN_4BIT_VOGON_OPAQUE);
    span->vogon.data = data;
    span->vogon.data_length = data_length;
}

void __time_critical_func(set_vogon_4bit_clipping)(struct span *span, int clip_left, int display_width) {
    assert(span->type == SPAN_4BIT_VOGON_OPAQUE);
    assert(clip_left >= 0);
    assert(display_width >= 0); // todo should we allow this? probably
    assert(clip_left + display_width <= span->vogon.content_width);
    span->vogon.clip_left = clip_left;
    span->width = display_width;
}

// todo needs to be shared - currently the same as GAP_SKIPPED_PIXELS as it happens
#define MIN_COLOR_RUN 3

// todo allow for chained DMA (indeed, we may have a pool of small fixed size chunks (says 64 words) we can re-use for scanlines anyway - a big scanline could use more than one
// todo   but we can simply split our rendering across them (and link them into the chain)... this will make it easier to join in raw data etc.
// todo simple span allocation
int32_t __time_critical_func(single_color_scanline)(uint32_t *buf, size_t buf_length, int width, uint32_t color16) {
    assert(buf_length >= 2);
    assert(width >= MIN_COLOR_RUN);
    // | jmp color_run | color | count-3 | buf[0] =
    buf[0] = COMPOSABLE_COLOR_RUN | (color16 << 16);
    buf[1] = (width - MIN_COLOR_RUN) | (COMPOSABLE_RAW_1P << 16);
    // note we must end with a black pixel
    buf[2] = 0 | (COMPOSABLE_EOL_ALIGN << 16);

    return 3;
}

#define output_4bit_paletted_pixels_ff(output, palette_entries, encoding, count) if (true) { \
    span_assert((count)>0); \
    span_assert(!((count)&1)); \
    uint32_t p = *encoding++; \
    if ((count)>2) { \
        *output++ = COMPOSABLE_RAW_RUN; \
        *output++ = palette_entries[p&0xf]; \
        *output++ = (count) - 3; \
        *output++ = palette_entries[p>>4]; \
        int c = count; \
        while (0 < (c = c -2)) { \
            p = *encoding++; \
            *output++ = palette_entries[p&0xf]; \
            *output++ = palette_entries[p>>4]; \
        } \
    } else { \
        *output++ = COMPOSABLE_RAW_2P; \
        *output++ = palette_entries[p&0xf]; \
        *output++ = palette_entries[p>>4]; \
    } \
} else __builtin_unreachable()

#define output_4bit_paletted_pixels_fx(output, palette_entries, encoding, count) if (true) { \
    span_assert((count)>0); \
    uint32_t p = *encoding++; \
    if ((count)>2) { \
        *output++ = COMPOSABLE_RAW_RUN; \
        *output++ = palette_entries[p&0xf]; \
        *output++ = (count) - 3; \
        *output++ = palette_entries[p>>4]; \
        int c = count; \
        while (1 < (c = c -2)) { \
            p = *encoding++; \
            *output++ = palette_entries[p&0xf]; \
            *output++ = palette_entries[p>>4]; \
        } \
        if (count & 1) { \
            p = *encoding++; \
            *output++ = palette_entries[p&0xf]; \
        } \
    } else { \
        if ((count) == 1) { \
            *output++ = COMPOSABLE_RAW_1P; \
            *output++ = palette_entries[p&0xf]; \
        } else { \
            *output++ = COMPOSABLE_RAW_2P; \
            *output++ = palette_entries[p&0xf]; \
            *output++ = palette_entries[p>>4]; \
        } \
    } \
} else __builtin_unreachable()

#define XXoutput_4bit_paletted_pixels_xf(output, palette_entries, encoding, count) encoding += ((count+1)>>1)

#define output_4bit_paletted_pixels_xf(output, palette_entries, encoding, count) if (true) { \
    span_assert((count)>0); \
    uint32_t p = *encoding++; \
    if ((count)>2) { \
        *output++ = COMPOSABLE_RAW_RUN; \
        if ((count) & 1) { \
            *output++ = palette_entries[p>>4]; \
            *output++ = (count) - 3; \
        } else { \
            *output++ = palette_entries[p&0xf]; \
            *output++ = (count) - 3; \
            *output++ = palette_entries[p>>4]; \
        } \
        int c = ((count)-1)>>1; \
        while (c--) { \
            p = *encoding++; \
            *output++ = palette_entries[p&0xf]; \
            *output++ = palette_entries[p>>4]; \
        } \
    } else { \
        if ((count) == 1) { \
            *output++ = COMPOSABLE_RAW_1P; \
        } else { \
            *output++ = COMPOSABLE_RAW_2P; \
            *output++ = palette_entries[p&0xf]; \
        } \
        *output++ = palette_entries[p>>4]; \
    } \
} else __builtin_unreachable()

#define output_color_one_pixel(output, color) if (true) { \
        *output++ = COMPOSABLE_RAW_1P; \
        *output++ = color; \
} else __builtin_unreachable()

#define output_color_two_pixels(output, color) if (true) { \
        *output++ = COMPOSABLE_RAW_2P; \
        *output++ = color; \
        *output++ = color; \
} else __builtin_unreachable()

#define output_color_run_as_run_length(output, color, run_length) if (true) { \
    span_assert(run_length >= MIN_COLOR_RUN); \
    *output++ = COMPOSABLE_COLOR_RUN; \
    *output++ = color; \
    *output++ = (run_length) - MIN_COLOR_RUN; \
} else __builtin_unreachable()

#define output_color_run_of_min_size(output, color, run_length) if (true) { \
    span_assert(run_length >= MIN_COLOR_RUN); \
    output_color_run_as_run_length(output, color, run_length); \
} else __builtin_unreachable()

#define output_color_run_of_any_size(output, color, run_length) if (true) { \
    if ((run_length) >= 3) { \
        output_color_run_as_run_length(output, color, run_length); \
    } else if ((run_length) == 1) { \
        output_color_one_pixel(output, color); \
    } else if ((run_length) == 2) { \
        output_color_two_pixels(output, color); \
    } else { \
        assert(false); \
    } \
} else __builtin_unreachable()

/**
 * This method is kinda ugly, but really needs to be fast - C++ and particular templates and references could probably make it better
 * but still, this will probably want to be assembly anyway. For now cut and paste code rather than sub-method fragments to string together...
 * assembly being good for state machines!
 *
 * Actually I've started to move some common stuff/loops out into static inline functions that we can hopefully _asm-ify in the short term
 *
 * @param render_spans_buffer
 * @param max_words
 * @param head
 * @param width
 * @param do_free
 * @return
 */
int32_t __time_critical_func(render_spans)(uint32_t *render_spans_buffer, size_t max_words, struct span *head,
                                           int width) {
    uint16_t *output = (uint16_t *) render_spans_buffer;
    assert(!(3u & (uintptr_t) output)); // should be dword aligned
#ifndef NDEBUG
    // todo output_end
    uint16_t *output_end = output + 2 * max_words;
#endif

    int total_pixels_remaining = width;
    for (const struct span *cur = head; cur && total_pixels_remaining > 0; cur = cur->next) {
        int local_pixels_remaining = cur->width;
        if (!local_pixels_remaining) continue;
        total_pixels_remaining -= local_pixels_remaining;
        if (total_pixels_remaining < 0) {
            local_pixels_remaining += total_pixels_remaining;
        }
        // todo i think this is reasonable, since for it to be 0 we'd have to have pixels_remaining == 0
        span_assert(local_pixels_remaining > 0);
        if (cur->type == SPAN_SOLID) {
            // no hard clipping work; we just output what we're told
            uint16_t color = cur->solid.color16;
            output_color_run_of_any_size(output, color, local_pixels_remaining);
        } else if (cur->type == SPAN_4BIT_VOGON_OPAQUE) {
            int skip_pixels_remaining = cur->vogon.clip_left;
            int right_clipped_pixels = cur->vogon.content_width - skip_pixels_remaining - local_pixels_remaining;

            const uint16_t *palette_entries = cur->vogon.palette->entries;
            const uint8_t *encoding = cur->vogon.data;
            uint8_t c;
            // deal with the skip pixels if any (do the whole rendering loop here, because it has been adulterated
            // with code to check for clipping
            while (skip_pixels_remaining > 0) {
                c = *encoding++;
                /* -------------------------------
                // this variant skips a run which is wholly inside the clip_left
                // or does a partially clipped span (which may be both left and right clipped)
                // -------------------------------
                */

                assert(right_clipped_pixels == 0); // can't do that here for now
                if (RAW_PIXELS_SHORT == (c & 0xc0)) {
                    // count is already pairs of pixels count
                    int pair_count = ((c & 0x3f) + 1);
                    int run_length = pair_count << 1;
                    const uint8_t *end = encoding + pair_count;
                    if (skip_pixels_remaining < run_length) {
                        encoding += skip_pixels_remaining >> 1;
                        run_length -= skip_pixels_remaining;
                        output_4bit_paletted_pixels_xf(output, palette_entries, encoding, run_length);
                        skip_pixels_remaining = 0;
                    } else {
                        // wholly clipped
                        skip_pixels_remaining -= run_length;
                        encoding = end;
                    }
                    span_assert(encoding == end);
                } else if (COLOR_PIXELS_SHORT == (c & 0xc0)) {
                    int run_length = ((c & 0x3f) + MIN_COLOR_SPAN_4BIT);
                    skip_pixels_remaining -= run_length;
                    if (skip_pixels_remaining < 0) {
                        run_length = -skip_pixels_remaining;
                        span_assert(run_length > 0);
                        uint16_t color = palette_entries[*encoding++];
                        output_color_run_of_any_size(output, color, run_length);
                    } else {
                        encoding++;
                    }
                } else if (SINGLE_PIXEL == (c & 0xf0)) {
                    // if we are clipped, then there is nothing to do (no pixels left)
                    skip_pixels_remaining--;
                } else if (c == COLOR_PIXELS_LONG) {
                    int run_length = 1 + *encoding++;
                    run_length += (*encoding++ << 8);
                    skip_pixels_remaining -= run_length;
                    if (skip_pixels_remaining < 0) {
                        run_length = -skip_pixels_remaining;
                        span_assert(run_length > 0);
                        uint16_t color = palette_entries[*encoding++];
                        output_color_run_of_any_size(output, color, run_length);
                    } else {
                        encoding++;
                    }
                } else if (c == RAW_PIXELS_LONG) {
                    int run_length = 1 + *encoding++;
                    run_length += (*encoding++ << 8);
                    span_assert(!(run_length & 1)); // we always have even numbers of pixels
                    if (skip_pixels_remaining < run_length) {
                        encoding += skip_pixels_remaining >> 1;
                        run_length -= skip_pixels_remaining;
                        output_4bit_paletted_pixels_xf(output, palette_entries, encoding, run_length);
                        skip_pixels_remaining = 0;
                    } else {
                        encoding += run_length >> 1;
                        skip_pixels_remaining -= run_length;
                    }
                    span_assert(encoding == end);
                } else if (c == END_OF_LINE) {
                    // just pass it on, though we could do some assertiony stuff here
                    encoding--;
                    break;
                } else {
                    return -1;
                }
            }
            if (!right_clipped_pixels) {
                // -------------------------------
                // here we do entirely unclipped runs from now on, without having to bother
                // with book-keeping
                // -------------------------------
                while (true) {
                    c = *encoding++;
                    if (RAW_PIXELS_SHORT == (c & 0xc0)) {
                        // count is pairs of pixels
                        int run_length = ((c & 0x3f) + 1) * 2;
                        output_4bit_paletted_pixels_ff(output, palette_entries, encoding, run_length);
                    } else if (COLOR_PIXELS_SHORT == (c & 0xc0)) {
                        int run_length = ((c & 0x3f) + MIN_COLOR_SPAN_4BIT);
                        uint16_t color = palette_entries[*encoding++];
                        output_color_run_of_min_size(output, color, run_length);
                    } else if (SINGLE_PIXEL == (c & 0xf0)) {
                        uint16_t color = palette_entries[c & 0xf];
                        output_color_one_pixel(output, color);
                    } else if (c == COLOR_PIXELS_LONG) {
                        int run_length = 1 + *encoding++;
                        run_length += (*encoding++) << 8;
                        uint16_t color = palette_entries[*encoding++];
                        output_color_run_of_min_size(output, color, run_length);
                    } else if (c == RAW_PIXELS_LONG) {
                        int run_length = 1 + *encoding++;
                        run_length += (*encoding++) << 8;
                        assert(!(run_length & 1)); // we always have even numbers of pixels
                        output_4bit_paletted_pixels_ff(output, palette_entries, encoding, run_length);
                    } else if (c == END_OF_LINE) {
                        break;
                    } else {
                        return -1;
                    }
                }
            } else {
                span_assert(right_clipped_pixels > 0); // should not be negative ever
                span_assert(local_pixels_remaining > 0); // believe this is impossible
                // similar to the regular loop but we must track local_pixels_remaining;
                while (local_pixels_remaining > 0) {
                    c = *encoding++;
                    if (RAW_PIXELS_SHORT == (c & 0xc0)) {
                        // count is already pairs of pixels count
                        int pair_count = ((c & 0x3f) + 1);
                        const uint8_t *end = encoding + pair_count;
                        int run_length = pair_count * 2;
                        local_pixels_remaining -= run_length;
                        if (local_pixels_remaining >= 0) {
                            output_4bit_paletted_pixels_ff(output, palette_entries, encoding, run_length);
                        } else {
                            run_length += local_pixels_remaining;
                            span_assert(run_length >= 0);
                            output_4bit_paletted_pixels_fx(output, palette_entries, encoding, run_length);
                            encoding = end;
                        }
                        span_assert(encoding == end);
                    } else if (COLOR_PIXELS_SHORT == (c & 0xc0)) {
                        int run_length = ((c & 0x3f) + MIN_COLOR_SPAN_4BIT);
                        uint16_t color = palette_entries[*encoding++];
                        local_pixels_remaining -= run_length;
                        // todo collapse these into a single call?
                        if (local_pixels_remaining < 0) {
                            run_length += local_pixels_remaining;
                            output_color_run_of_any_size(output, color, run_length);
                        } else {
                            output_color_run_of_min_size(output, color, run_length);
                        }
                    } else if (SINGLE_PIXEL == (c & 0xf0)) {
                        uint16_t color = palette_entries[c & 0xf];
                        // since the span is not clipped its one pixel must not be
                        output_color_one_pixel(output, color);
                        local_pixels_remaining--;
                    } else if (c == COLOR_PIXELS_LONG) {
                        int run_length = 1 + *encoding++;
                        run_length += (*encoding++) << 8;
                        local_pixels_remaining -= run_length;
                        uint16_t color = palette_entries[*encoding++];
                        // todo collapse these into a single call? more so because this is a long run
                        if (local_pixels_remaining < 0) {
                            run_length += local_pixels_remaining;
                            output_color_run_of_any_size(output, color, run_length);
                        } else {
                            output_color_run_of_min_size(output, color, run_length);
                        }
                    } else if (c == RAW_PIXELS_LONG) {
                        int run_length = 1 + *encoding++;
                        run_length += (*encoding++) << 8;
                        assert(!(run_length & 1)); // we always have even numbers of pixels
                        const uint8_t *end = encoding + (run_length >> 1);
                        local_pixels_remaining -= run_length;
                        if (local_pixels_remaining >= 0) {
                            output_4bit_paletted_pixels_ff(output, palette_entries, encoding, run_length);
                        } else {
                            run_length += local_pixels_remaining;
                            span_assert(run_length >= 0);
                            output_4bit_paletted_pixels_fx(output, palette_entries, encoding, run_length);
                            encoding = end;
                        }
                        span_assert(encoding == end);
                    } else if (c == END_OF_LINE) {
                        break;
                    } else {
                        return -1;
                    }
                }
            }
        }
    }

    *output++ = COMPOSABLE_RAW_1P;
    *output++ = 0;
    if (2u & (uintptr_t) output) {
        // we are unaligned
        *output++ = COMPOSABLE_EOL_ALIGN;
    } else {
        *output++ = COMPOSABLE_EOL_SKIP_ALIGN;
        *output++ = 0xffff; // eye catcher
//        output++;
    }
//    *output ++ = 29;
//    *output ++ = 29;
    assert(output <= output_end);
    assert(0 == (3u & (uintptr_t) output));
    return ((uint32_t *) output) - render_spans_buffer;
}

#ifdef __arm__
#pragma GCC pop_options
#endif
