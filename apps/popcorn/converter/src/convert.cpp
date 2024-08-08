/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <cstdio>
#include <cstring>

// add a word at the end of every row with debug information
// #define ADD_EOR_DEBUGGING
#define ENCODE_565

#define RSHIFT 0
#ifdef ENCODE_565
#define GSHIFT 6
#define BSHIFT 11
#else
#define GSHIFT 5
#define BSHIFT 10
#endif


inline static uint16_t to_5n5(uint32_t t) {
#ifndef ENCODE_565
	return ((t & 0xf8) >> 3) | ((t & 0xf800) >> 6 | ((t & 0xf80000) >> 9));
#else
    return ((t & 0xf8) >> 3) | ((t & 0xf800) >> 5 | ((t & 0xf80000) >> 8));
#endif
}

// uint32_t as may be used for 2 pixels
inline static uint32_t to_5n5(uint32_t r5, uint32_t g5, uint32_t b5) {
#ifndef ENCODE_565
    return (b5 << 10) | (g5 << 5) | (r5);
#else
    return (b5 << 11) | (g5 << 6) | (r5);
#endif
}

uint32_t sorted_keys[32] = {
        0x0000,
        0x0020,
        0x0421,
        0x0420,

        0x8420,
        0x0400,
        0x0021,
        0x0441,

        0x0041,
        0x0821,
        0x00442,
        0x00401,

        0x08020,
        0x08440,
        0x08400,
        0x08820,

        0x00822,
        0x08040,
        0x10820,
        0x08800,

        0x00040,
        0x08041,
        0x00422,
        0x00801,

        0x00842,
        0x00042,
        0x10440,
        0x00462,

        0x00062,
        0x00463,
        0x00440,
        0x08021,
};

static uint8_t *key_lookup;
static uint16_t *key_dist;
static int8_t *key_offset;

std::vector<unsigned char> converted;

uint8_t dp[] = {
        0,  8,  2, 10,
        12,  4, 14,  6,
        3, 11,  1,  9,
        15,  7, 13,  5
};

static inline void dither(uint8_t& c, uint x3, uint y3) {
    uint x = c + dp[x3 + y3 * 4] / 2;
    if (x > 255) x = 255;
    c = (x & 0xf8u);
}

static inline void dither(uint8_t& r, uint8_t& g, uint8_t &b, uint x3, uint y3) {
    dither(r, x3, y3);
    dither(g, x3, y3);
    dither(b, x3, y3);
}

void dither_image(uint w, uint h, std::vector<unsigned char> &source)
{
    for(uint y=0; y < h; y++) {
        uint8_t *base = &source[y * w * 3];
        for(uint x = 0; x < w; x+= 4) {
#if 1
            dither(base[0], base[1], base[2], 0, y&3u);
            dither(base[3], base[4], base[5], 1, y&3u);
            dither(base[6], base[7], base[8], 2, y&3u);
            dither(base[9], base[10], base[11], 3, y&3u);
#endif
            base += 12;
        }
    }
}

static int min_cost = 0x7fffffff;
static int max_cost = 0;
static int worst_frame = 0;
static long total_cost = 0;
static long total_vals = 0;

static uint rgb16(uint8_t r, uint8_t g, uint8_t b) {
    return ((b>>3) << 11) | ((g>>3)<<6) | (r>>3);
}

static uint rgb15(uint8_t r, uint8_t g, uint8_t b) {
    return ((b>>3) << 10) | ((g>>3)<<5) | (r>>3);
}

static inline void write_word(uint8_t *&dest, uint v) {
    *dest++ = v & 0xff;
    *dest++ = v >> 8;
}

static inline void write_word_be(uint8_t *&dest, uint v) {
    *dest++ = v >> 8;
    *dest++ = v & 0xff;
}

static uint32_t *row0_222;
static uint32_t *row1_222;
static uint32_t *row0_5;
static uint32_t *row1_5;

static inline uint get_key(uint min, uint da, uint db, uint dc, uint dd) {
    assert(da >= min);
    assert(db >= min);
    assert(dc >= min);
    assert(dd >= min);
    return (((da - min) >> 3u) << 5u * 3u) |
           (((db - min) >> 3u) << 5u * 2u) |
           (((dc - min) >> 3u) << 5u * 1u) |
           (((dd - min) >> 3u) << 5u * 0u);
}


#ifndef ENCODE_565
bool compress_image(const char *name, uint w, uint h, std::vector<unsigned char> &source, std::vector<unsigned char> &dest, std::vector<uint32_t> &line_offsets, uint max_rdist, uint max_gdist, uint max_bdist, uint extra_line_words = 0)
{
    bool use_56bit_raw = true;

    assert(!((w|h)&1u));

    if (!key_lookup)
    {
        key_lookup = (uint8_t *)calloc(1,0x100000);
        key_dist = (uint16_t *) calloc(2, 0x100000);
        key_offset = (int8_t *) calloc(1, 0x100000);
        for(int i = 0; i < 0x100000; i++)
        {
            key_dist[i] = 0x7fffu;
        }
        for(int i = 0; i < 32; i++)
        {
            uint8_t choice = (i < 4) ? 0x81 + i : 1 + i;
            key_lookup[sorted_keys[i]] = choice;
            key_dist[sorted_keys[i]] = 0;
            for(int o = -3; o < 3; o++) { // todo really?
//                for(int o=0;o<1;o++) {
                uint da = (sorted_keys[i] >> (5u * 3u)) & 0x1f;
                uint db = (sorted_keys[i] >> (5u * 2u)) & 0x1f;
                uint dc = (sorted_keys[i] >> (5u * 1u)) & 0x1f;
                uint dd = (sorted_keys[i] >> (5u * 0u)) & 0x1f;
//                if (da == db && db == dc & dc == dd && (o == da || o == -da)) continue;
                for(uint a = 0; a < 32; a++)
                {
                    for(uint b = 0; b < 32; b++)
                    {
                        for(uint c = 0; c < 32; c++)
                        {
                            for(uint d = 0; d < 32; d++)
                            {
                                uint j = (a << (5u * 3u)) | (b << (5u * 2u)) | (c << (5u * 1u) | (d << (5u * 0u)));
                                uint score = (a + o - da) * (a + o - da);
                                score += (b + o - db) * (b + o - db);
                                score += (c + o - dc) * (c + o - dc);
                                score += (d + o - dd) * (d + o - dd);
                                if (score < key_dist[j] || (score == key_dist[j] && !o))
                                {
                                    if (score == 0) {
//                                        printf("pants\n");
                                    }

                                    key_dist[j] = score;
                                    key_lookup[j] = choice;
                                    key_offset[j] = o;
                                }
                            }
                        }
                    }
                }
            }
        }

        for(int i = 0; i < 32; i++)
        {
            assert(key_dist[sorted_keys[i]] == 0);
        }
        uint t = 0;
        for(int i = 0; i < 0x100000; i++) {
            if (!key_dist[i]) {
//                printf("%d %d %d\n", i, key_offset[i], key_dist[i]);
                t++;
        }
            if (key_offset[i]) {
                //printf("%d %d %d\n", i, key_offset[i], key_dist[i]);
            }
        }
//        assert(t == 32);
assert(t == 128); // todo why
    }
    uint32_t counts[4] = {0,0,0,0};
    uint8_t *d = &dest[0];
    line_offsets.clear();
    for(uint y=0; y < h; y+=2) {
        uint8_t *base = &source[y * w * 3];
        uint8_t *base2 = base + w * 3;
        line_offsets.push_back(d - &dest[0]);

        for(uint x = 0; x < w; x+= 2) {
            uint rmin = std::min({base[0], base[3], base2[0], base2[3]});
            uint gmin = std::min({base[1], base[4], base2[1], base2[4]});
            uint bmin = std::min({base[2], base[5], base2[2], base2[5]});
            uint rkey = get_key(rmin, base[0], base[3], base2[0], base2[3]);
            uint16_t rdist = key_dist[rkey];
            uint8_t rval = key_lookup[rkey];
            uint gkey =  get_key(gmin, base[1], base[4], base2[1], base2[4]);
            uint16_t gdist = key_dist[gkey];
            uint8_t gval = key_lookup[gkey];
            uint bkey =  get_key(bmin, base[2], base[5], base2[2], base2[5]);
            uint16_t bdist = key_dist[bkey];
            uint8_t bval = key_lookup[bkey];
            uint16_t dist = MAX(rdist, MAX(gdist, bdist));
            uint16_t val = MIN(rval, MIN(gval, bval));
//            if (dist) {
//            if (rdist > 4 || gdist > 4 || bdist > 4) {
            if (rdist > max_rdist || gdist > max_gdist || bdist > max_bdist) {
//                if (rdist <= 4 && gdist <= 4 && bdist <= 4) {
//                    memset(base, 0, 6);
//                    if (key_offset[rkey]) base[0] |= 0xf8;
//                    if (key_offset[gkey]) base[1] |= 0xf8;
//                    if (key_offset[bkey]) base[2] |= 0xf8;
//                    //printf("oobla\n");
//                }
                if (use_56bit_raw)
                {
                    // #2 : encode ABCD raw as 555, 454, 454, 454 in 7 bytes
                    //                                                                                                          v                           v                                           v    v                      v                                           v
                    // | Ga2 Ga1 Ga0 Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | "1" Ba4 Ba3 Ba2 : Ba1 Ba0 Ga4 Ga3
                    //
                    // | Gb2 Gb1 Gb0 Rb4 : Rb3 Rb2 Rb1 Bd2
                    // | "0" Bb4 Bb3 Bb2 : Bb1 Gd4 Gb4 Gb3
                    //
                    // | Gc2 Gc1 Gc0 Rc4 : Rc3 Rc2 Rc1 Bd1
                    // | Bd3 Bc4 Bc3 Bc2 : Bc1 Gd3 Gc4 Gc3
                    //
                    // | Gd2 Gd1 Gd0 Rd4 : Rd3 Rd2 Rd1 Bd4 |

                    uint32_t lo = 0x8000u | rgb15(base[0], base[1], base[2]) | (rgb15(base[3], base[4], base[5]) << 16);
                    uint32_t hi = rgb15(base2[0], base2[1], base2[2]) | (rgb15(base2[3], base2[4], base2[5]) << 16);
                    // from "0" Bd4 Bd3 Bd2 : Bd1 Bd0 Gd4 Gd3
                    *d++ = lo;
                    *d++ = lo >> 8u;
                    *d++ = ((lo >> 16u) & ~1u) | ((hi & 0x10000000u)?1u:0);
                    *d++ = ((lo >> 24u) & ~4u) | ((hi & 0x02000000u)?4u:0);
                    *d++ = (hi & ~1u) | ((hi & 0x08000000u)?1u:0);
                    *d++ = ((hi >> 8u) & ~0x84u) | ((hi & 0x20000000u)?0x80u:0) | ((hi & 0x01000000u)?0x4u:0);
                    *d++ = ((hi >> 16u) & ~1u) | ((hi & 0x40000000u)?1u:0);
                    counts[3]++;
                } else
                {
                    // #1 : encode ABCD raw as 555, 555, 555, 555 in 8 bytes
                    //
                    // | Ga2 Ga1 Ga0 Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | "1" Ba4 Ba3 Ba2 : Ba1 Ba0 Ga4 Ga3

                    // | Gb2 Gb1 Gb0 Rb4 : Rb3 Rb2 Rb1 Rb0
                    // | "1" Bb4 Bb3 Bb2 : Bb1 Bb0 Gb4 Gb3

                    // | Gc2 Gc1 Gc0 Rc4 : Rc3 Rc2 Rc1 Rc0
                    // | "0" Bc4 Bc3 Bc2 : Bc1 Bc0 Gc4 Gc3

                    // | Gd2 Gd1 Gd0 Rd4 : Rd3 Rd2 Rd1 Rc0
                    // | "0" Bd4 Bd3 Bd2 : Bd1 Bd0 Gd4 Gd3 |
                    //
                    write_word(d, 0x8000u | rgb15(base[0], base[1], base[2]));
                    write_word(d, 0x8000u | rgb15(base[3], base[4], base[5]));
                    write_word(d, rgb15(base2[0], base2[1], base2[2]));
                    write_word(d, rgb15(base2[3], base2[4], base2[5]));
                    counts[0]++;
                }
            } else if (val < 0x80) {
                // #1 encode 555 color, then 1 of 32 2x2 add patterns for each component in 4 bytes total
                assert(rval && gval && bval);
                // | Ga2 Ga1 Ga0 Ra4 : Ra3 Ra2 Ra1 Ra0
                // | "0" Ba4 Ba3 Ba2 : Ba1 Ba0 Ga4 Ga3
                // |
                // | "1" Bp4 Bp3 Bp2 : Bp1 Bp0 Gp4 Gp3
                // | Gp2 Gp1 Gp0 Rp4 : Rp3 Rp2 Rp1 Rp0
                write_word(d, rgb15(rmin-key_offset[rkey], gmin - key_offset[gkey], bmin - key_offset[bkey]));
                write_word_be(d, 0x8000u | rgb15((rval - 1u)<<3u, (gval -1u)<<3u, (bval-1u)<<3u));
                counts[1]++;
            } else {
                // #1 encode 555 color, then 1 of 4 2x2 add patterns for each component in 3 bytes total
                  // | Ga2 Ga1 Ga0 Ra4 : Ra3 Ra2 Ra1 Ra0
                  // | "0" Ba4 Ba3 Ba2 : Ba1 Ba0 Ga4 Ga3
                  // |
                  // | "0" "0" Bp1 Bp0 : Gp1 Gp0 Rp1 Rp0
                write_word(d, rgb15(rmin-key_offset[rkey], gmin - key_offset[gkey], bmin - key_offset[bkey]));
                *d++ = ((bval - 0x81u) << 4u) | ((gval - 0x81u) << 2u) | (rval - 0x81u);
                counts[2]++;
            }
            base += 6;
            base2 += 6;
        }
        while (3u & (uintptr_t)d) *d++ = 0; // need aligned rows
        for(int i=0;i<extra_line_words;i++) {
            *d++ = 0;
            *d++ = 0;
            *d++ = 0;
            *d++ = 0;
        }
    }
    line_offsets.push_back(d - &dest[0]);
    while (d < dest.end().base()) *d++ = 0;
    //return counts[0]*8 + counts[3] * 7 + counts[1] *3 + counts[2] * 4;
    int cost = counts[0]*8 + counts[3] * 7 + counts[2] *3 + counts[1] * 4;
    min_cost = MIN(cost, min_cost);
    bool rc = cost > max_cost;
    max_cost = MAX(cost, max_cost);
    total_cost += cost;
    total_vals += w*h;
//    printf("%s %d*8 %d*7 %d*4 %d*3\n", name, counts[0], counts[3], counts[1], counts[2]);
    return rc;
}

#else
bool compress_image(const char *name, uint w, uint h, std::vector<unsigned char> &source, std::vector<unsigned char> &dest, std::vector<uint32_t> &line_offsets, uint max_rdist, uint max_gdist, uint max_bdist, uint extra_line_words = 0)
{
    bool use_56bit_raw = true;

    assert(!((w|h)&1u));

    if (!key_lookup)
    {
        key_lookup = (uint8_t *) calloc(1, 0x100000);
        key_dist = (uint16_t *) calloc(2, 0x100000);
        key_offset = (int8_t *) calloc(1, 0x100000);
        for(int i = 0; i < 0x100000; i++)
        {
            key_dist[i] = 0x7fffu;
        }
        for(int i = 0; i < 32; i++)
        {
            uint8_t choice = (i < 4) ? 0x81 + i : 1 + i;
            key_lookup[sorted_keys[i]] = choice;
            key_dist[sorted_keys[i]] = 0;
            for(int o = -3; o < 3; o++) { // todo really?
//                for(int o=0;o<1;o++) {
                uint da = (sorted_keys[i] >> (5u * 3u)) & 0x1f;
                uint db = (sorted_keys[i] >> (5u * 2u)) & 0x1f;
                uint dc = (sorted_keys[i] >> (5u * 1u)) & 0x1f;
                uint dd = (sorted_keys[i] >> (5u * 0u)) & 0x1f;
//                if (da == db && db == dc & dc == dd && (o == da || o == -da)) continue;
                for(uint a = 0; a < 32; a++)
                {
                    for(uint b = 0; b < 32; b++)
                    {
                        for(uint c = 0; c < 32; c++)
                        {
                            for(uint d = 0; d < 32; d++)
                            {
                                uint j = (a << (5u * 3u)) | (b << (5u * 2u)) | (c << (5u * 1u) | (d << (5u * 0u)));
                                uint score = (a + o - da) * (a + o - da);
                                score += (b + o - db) * (b + o - db);
                                score += (c + o - dc) * (c + o - dc);
                                score += (d + o - dd) * (d + o - dd);
                                // todo what is this second half
                                //if (score < key_dist[j]) // || (score == key_dist[j] && !o))
                                if (score < key_dist[j] || (score == key_dist[j] && !o))
                                {
                                    if (score == 0) {
//                                        printf("pants\n");
                                    }

                                    key_dist[j] = score;
                                    key_lookup[j] = choice;
                                    key_offset[j] = o;
                                }
                            }
                        }
                    }
                }
            }
        }

        for(int i = 0; i < 32; i++)
        {
            assert(key_dist[sorted_keys[i]] == 0);
        }
        uint t = 0;
        for(int i = 0; i < 0x100000; i++) {
            if (!key_dist[i]) {
//                printf("%d %d %d\n", i, key_offset[i], key_dist[i]);
                t++;
            }
            if (key_offset[i]) {
                //printf("%d %d %d\n", i, key_offset[i], key_dist[i]);
            }
        }
//        assert(t == 32);
        assert(t == 128); // todo why
    }
    uint32_t counts[4] = {0,0,0,0};
    uint8_t *d = &dest[0];
    line_offsets.clear();
    for(uint y=0; y < h; y+=2) {
        uint8_t *base = &source[y * w * 3];
        uint8_t *base2 = base + w * 3;
        line_offsets.push_back(d - &dest[0]);

        for(uint x = 0; x < w; x+= 2) {
            uint rmin = std::min({base[0], base[3], base2[0], base2[3]});
            uint gmin = std::min({base[1], base[4], base2[1], base2[4]});
            uint bmin = std::min({base[2], base[5], base2[2], base2[5]});
            uint rkey = get_key(rmin, base[0], base[3], base2[0], base2[3]);
            uint16_t rdist = key_dist[rkey];
            uint8_t rval = key_lookup[rkey];
            uint gkey =  get_key(gmin, base[1], base[4], base2[1], base2[4]);
            uint16_t gdist = key_dist[gkey];
            uint8_t gval = key_lookup[gkey];
            uint bkey =  get_key(bmin, base[2], base[5], base2[2], base2[5]);
            uint16_t bdist = key_dist[bkey];
            uint8_t bval = key_lookup[bkey];
            uint16_t dist = std::max({rdist, gdist, bdist});
            uint16_t val = std::min({rval, gval, bval});
//            if (dist) {
//            if (rdist > 4 || gdist > 4 || bdist > 4) {
            if (rdist > max_rdist || gdist > max_gdist || bdist > max_bdist) {
                if (use_56bit_raw)
                {
                    // #2 : encode ABCD raw as 555, 454, 454, 454 in 7 bytes
                    //
                    // | Ga1 Ga0 "1" Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2
                    //
                    // | Gb1 Gb0 "0" Rb4 : Rb3 Rb2 Rb1 Bd1
                    // | Bb4 Bb3 Bb2 Bb1 : Bd2 Gb4 Gb3 Gb2
                    //
                    // | Gc1 Gc0 Bd3 Rc4 : Rc3 Rc2 Rc1 Gd3
                    // | Bc4 Bc3 Bc2 Bc1 : Gd4 Gc4 Gc3 Gc2
                    //
                    // | Gd1 Gd0 (Gd2^Gd4) Rd4 : Rd3 Rd2 Rd1 Bd4

                    // | Bd4 Bd3 Bd2 Bd1 :     Gd4 Gd3 Gd2


                    uint32_t lo = 0x0020u | rgb16(base[0], base[1], base[2]) | (rgb16(base[3], base[4], base[5]) << 16u);
                    uint32_t hi = rgb16(base2[0], base2[1], base2[2]) | (rgb16(base2[3], base2[4], base2[5]) << 16u);
                    // from "0" Bd4 Bd3 Bd2 : Bd1 Bd0 Gd4 Gd3
                    *d++ = lo;
                    *d++ = lo >> 8u;
                    *d++ = ((lo >> 16u) & ~1u) | ((hi & 0x10000000u)?1u:0);
                    *d++ = ((lo >> 24u) & ~8u) | ((hi & 0x20000000u)?8u:0);
                    *d++ = (hi & ~ 0x21u) | ((hi & 0x40000000u)?0x20u:0) | ((hi & 0x02000000u)?1u:0);
                    *d++ = ((hi >> 8u) & ~0x8u) | ((hi & 0x04000000u)?0x8u:0);
                    *d++ = ((hi >> 16u) & ~0x21u) | ((((hi & 0x01000000u)?1u:0)^((hi & 0x04000000u)?1u:0))?0x20u:0) | ((hi & 0x80000000u)?1u:0);
                    counts[3]++;
                } else
                {
                    // #1 : encode ABCD raw as 555, 555, 555, 555 in 8 bytes
                    //
                    // | Ga1 Ga0 "1" Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2

                    // | Ga1 Ga0 "1" Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2

                    // | Ga1 Ga0 "0" Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2

                    // | Ga1 Ga0 "0" Ra4 : Ra3 Ra2 Ra1 Ra0
                    // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2
                    //
                    write_word(d, 0x0020u | rgb16(base[0], base[1], base[2]));
                    write_word(d, 0x0020u | rgb16(base[3], base[4], base[5]));
                    write_word(d, rgb16(base2[0], base2[1], base2[2]));
                    write_word(d, rgb16(base2[3], base2[4], base2[5]));
                    counts[0]++;
                }
            } else if (val < 0x80) {
                // #1 encode 555 color, then 1 of 32 2x2 add patterns for each component in 4 bytes total
                assert(rval && gval && bval);
                // | Ga1 Ga0 "0" Ra4 : Ra3 Ra2 Ra1 Ra0
                // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2
                // |
                // | Gp1 Gp0 Rp4 Rp3 : Rp2 Rp1 Rp0 "1"
                // | Bp4 Bp3 Bp2 Bp1 : Bp0 Gp4 Gp3 Gp2
                write_word(d, rgb16(rmin-key_offset[rkey], gmin - key_offset[gkey], bmin - key_offset[bkey]));
                write_word(d, 1u | ((rgb15((rval - 1u)<<3u, (gval -1u)<<3u, (bval-1u)<<3u)) << 1u));
                counts[1]++;
            } else {
                // #1 encode 555 color, then 1 of 4 2x2 add patterns for each component in 3 bytes total
                // | Ga1 Ga0 "0" Ra4 : Ra3 Ra2 Ra1 Ra0
                // | Ba4 Ba3 Ba2 Ba1 : Ba0 Ga4 Ga3 Ga2
                // |
                // | Bp1 Bp0 Gp1 Gp0 : Rp1 Rp0 "0" "0'
                write_word(d, rgb16(rmin-key_offset[rkey], gmin - key_offset[gkey], bmin - key_offset[bkey]));
                *d++ = ((bval - 0x81u) << 6u) | ((gval - 0x81u) << 4u) | ((rval - 0x81u) << 2u);
                counts[2]++;
            }
            base += 6;
            base2 += 6;
        }
        while (3u & (uintptr_t)d) *d++ = 0; // need aligned rows
        for(int i=0;i<extra_line_words;i++) {
            *d++ = 0;
            *d++ = 0;
            *d++ = 0;
            *d++ = 0;
    }
    }
    line_offsets.push_back(d - &dest[0]);
    while (d < dest.end().base()) *d++ = 0;
    //return counts[0]*8 + counts[3] * 7 + counts[1] *3 + counts[2] * 4;
    int cost = counts[0]*8 + counts[3] * 7 + counts[2] *3 + counts[1] * 4;
    min_cost = std::min(cost, min_cost);
    bool rc = cost > max_cost;
    max_cost = std::max(cost, max_cost);
    total_cost += cost;
    total_vals += w*h;
//    printf("%s %d*8 %d*7 %d*4 %d*3\n", name, counts[0], counts[3], counts[1], counts[2]);
    return rc;
}

#endif

void write_hword(uint word, FILE *out) {
    assert(word < 0x10000);
    fputc(word & 0xff, out);
    fputc(word >> 8, out);
}

uint32_t pad_sector(uint32_t sector_offset, FILE *out) {
    assert(sector_offset < 512);
    if (sector_offset) for (;sector_offset < 512; sector_offset++) fputc(0, out);
    return 0;
}

uint8_t to_bcd(uint x) {
    assert(x<100);
    return (x/10)*16 + (x%10);
}

#define PLAT_MAJOR 0
#define PLAT_MINOR 60

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

int encode_movie(const char *filename, const char *audio_filename, const char *filename_out, int start_frame) {
    worst_frame = 0;
    total_cost = 0;
    max_cost = 0;
    min_cost = 0x7fffffff;
    total_vals = 0;

#ifdef ADD_EOR_DEBUGGING
    uint extra_line_words = 1;
#else
    uint extra_line_words = 0;
#endif
    unsigned error;
    uint w = 320;
    uint h = 240;
    assert(!(h&1));
    size_t size3 = w * h * 3;
    size_t size2 = w * h * 2;
    std::vector<uint32_t> frame_sectors;
    std::vector<unsigned char> source;
    std::vector<unsigned char> dest;
    std::vector<uint32_t> line_offsets;
    line_offsets.resize(120);
    source.resize(size3);
    dest.resize(size2);
    FILE *file = fopen(filename, "rb");
    FILE *audio_file = audio_filename ? fopen(audio_filename, "rb") : nullptr;
    FILE *file_out = filename_out ? fopen(filename_out, "wb") : nullptr;
    if (!file) {
        fprintf(stderr, "Couldn't open input rgb file %s\n", filename);
        return -1;
    }
    if (audio_filename && !audio_file) {
        fprintf(stderr, "Couldn't open input pcm file %s\n", audio_filename);
        return -1;
    }
    if (filename_out && !file_out) {
        fprintf(stderr, "Couldn't open output pl2 file %s\n", filename_out);
        return -1;
    }
    fseek(file, 0, SEEK_END);
    size_t frames = ftell(file) / size3;
    printf("Frame count %ld = %02ld:%02ld:%02ld\n", frames, (frames / (3600 * 30)) % 60, (frames / (60 * 30)) % 60, (frames / 30) % 60);
    fseek(file, start_frame * size3, SEEK_SET);
//    frames = 10000; // movie
    uint32_t worst_length = 0;
    int lcount=0;
    int blcount=0;
    int fbmax=0;
    int bfcount=0;
    uint32_t sector_offset = 0;

    // todo i don't remember what the lineage of these were
//    const int max_rdist = 8;
//    const int max_gdist = 6;
//    const int max_bdist = 8;

    const int max_rdist = 4;
    const int max_gdist = 4;
    const int max_bdist = 4;

    for (int i = 0; i<frames;i++)
    {
        if (1 != fread(&source[0], size3, 1, file)) {
            fprintf(stderr, "Error reading frame %d\n", i);
            return -1;
        }
        dither_image(w, h, source);
        if (compress_image(NULL, w, h, source, dest, line_offsets, max_rdist, max_gdist, max_bdist, extra_line_words))
        {
            worst_frame = i;
        }
        int fb = 0;
        uint32_t l = 0;
        for(uint32_t o : line_offsets)
        {
            int len = o - l;
            if (len > 1024)
            {
                blcount++;
                fb++;
            }
            lcount++;
            if (len > worst_length)
            {
                worst_length = len;
            }

            l = o;
        }
#ifdef ADD_EOR_DEBUGGING
        for(int r = 0; r < h/2; r++) {
            int32_t o = line_offsets[r+1] - 4;
            assert(o > 0);
            assert(o < dest.size() - 4);
            assert(!(o&3u));
            // we cost 4 bytes per row, but we can use this instead of CRC to detect bad data (and also
            // help with debugging, since we get a good idea what row the data belongs to)
            dest[o + 0] = 0xaa;
            dest[o + 1] = o + 1;
            dest[o + 2] = i;
            dest[o + 3] = r;
        }
#endif
        fbmax = std::max(fb, fbmax);
        if (fb) bfcount++;
        if (file_out)
        {
            size_t secoff = ftell(file_out);
            uint32_t sector_num = secoff / 512;
            frame_sectors.push_back(sector_num);
            struct frame_header header;
            static_assert(sizeof(header) <= 512);
            memset(&header, 0, sizeof(header));
            header.mark0 = header.mark1 = 0xffffffff;
            header.magic = ('T' << 24) | ('A' << 16) | ('L' << 8) | 'P';
            header.major = PLAT_MAJOR;
            header.minior = PLAT_MINOR;
#ifdef ADD_EOR_DEBUGGING
            header.debug = 1;
#endif
            header.sector_number = sector_num;
            header.frame_number = i;
            int n = i + start_frame;
            header.hh = to_bcd(n / (60 * 60 * 30));
            header.mm = to_bcd((n / (60 * 30)) % 60);
            header.ss = to_bcd((n / 30) % 60);
            header.ff = to_bcd(n % 30);
            header.header_words = ((sizeof(header) + (h+1) + 1) + 3) / 4;
            assert(header.header_words <= 128);
            for(int f=0;f<4;f++) {
                header.forward_frame_sector[f] = header.backward_frame_sectors[f] = 0xffffffff;
            }
            header.width = w;
            header.height = h;
            header.image_words = line_offsets[h / 2] >> 2;
            if (audio_file) {
                header.audio_freq = 44100;
                header.audio_channels = 2;
                header.audio_words = (header.audio_freq * 2 * header.audio_channels / 30) / 4;
            } else {
                header.audio_freq = header.audio_channels = header.audio_words = 0;
            }
            fwrite(&header, 1, sizeof(header), file_out);
            sector_offset += sizeof(header);
            // note there is one extra for the end
            for(uint y = 0; y <= h / 2; y++)
            {
                uint off = line_offsets[y];
                assert(!(3u & off));
                off >>= 2;
                write_hword(off, file_out);
                sector_offset += 2;
            }
            assert(sector_offset <= 0x200);
            sector_offset = pad_sector(sector_offset, file_out);
            if (audio_file)
            {
                assert(header.audio_words * 30 * 4 ==
                       2 * header.audio_channels * header.audio_freq); // make sure we divide correctly
                uint32_t buf[header.audio_words];
                uint audio_size_bytes = header.audio_words * 4;
                fseek(audio_file, audio_size_bytes * (size_t) i, SEEK_SET);
                if (1 != fread(buf, audio_size_bytes, 1, audio_file)) {
                    fprintf(stderr, "Error reading audio for frame %d\n", i);
                    return -1;
                }
                fwrite(buf, audio_size_bytes, 1, file_out);
                sector_offset = pad_sector(audio_size_bytes & 0x1ff, file_out);
            }
            uint32_t actual_size = line_offsets[h / 2];
            fwrite(&dest[0], 1, actual_size, file_out);
            sector_offset = pad_sector(actual_size & 0x1ff, file_out);
        }
        if (!(i%60))
        {
            printf("%02d:%02d:%02d\n", (i / (3600 * 30)) % 60, (i / (60 * 30)) % 60, (i / 30) % 60);
        }
    }
    uint32_t total_sectors = ftell(file_out) / 512;

    fclose(file);
    assert(frames == frame_sectors.size());
    for(int i=0; i<frames; i++) {
        fseek(file_out, 512 * ((uint64_t)frame_sectors[i]) + offsetof(frame_header, total_sectors), SEEK_SET);
        fwrite(&total_sectors, 4, 1, file_out);
        uint32_t v;
        v = frame_sectors[frames-1]; fwrite(&v, 4, 1, file_out);
        v = i < frames - 1 ? frame_sectors[i+1] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i < frames - 2 ? frame_sectors[i+2] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i < frames - 4 ? frame_sectors[i+4] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i < frames - 8 ? frame_sectors[i+8] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i >= 1 ? frame_sectors[i-1] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i >= 2 ? frame_sectors[i-2] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i >= 4 ? frame_sectors[i-4] : 0xffffffff; fwrite(&v, 4, 1, file_out);
        v = i >= 8 ? frame_sectors[i-8] : 0xffffffff; fwrite(&v, 4, 1, file_out);
    }
    if (audio_file) fclose(audio_file);
    if (file_out) fclose(file_out);
    printf("last frame at %d\n", frame_sectors[frames-1]);
    printf("Worst frame %d mic %d mac %d avg %d maxl %d\n", worst_frame, min_cost, max_cost, (int)((total_cost * w * (long)h) / total_vals), worst_length);
    printf("%d %d %d, fbmax %d bfc %d/%ld\n", blcount, lcount, (int)(100l * blcount / lcount), fbmax, bfcount, frames);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: convert <rgb_file> <pcm_file> <output_file.pl2>\n");
        return -1;
    }
    return encode_movie(argv[1], argv[2], argv[3], 0);
}
