#include "font.h"

/*******************************************************************************
 * FrozenCrystal
 * Size: 12 px
 * Bpp: 4
 * Opts:
 ******************************************************************************/

#ifndef LCD12
#define LCD12 1
#endif

#if LCD12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t gylph_bitmap[] = {
/* U+2E "." */
0x93,

/* U+30 "0" */
0xc, 0xbb, 0x93, 0x47, 0x0, 0x84, 0x58, 0x0,
0xc4, 0x75, 0x31, 0x90, 0x51, 0x73, 0x90, 0xc4,
0x0, 0xe0, 0xc0, 0x1, 0xc0, 0xab, 0xbb, 0x70,

/* U+31 "1" */
0xc9, 0x40, 0x88, 0x8, 0x40, 0x74, 0x6, 0x0,
0xd0, 0xf, 0x0, 0xa0,

/* U+32 "2" */
0xc, 0xbb, 0x93, 0x0, 0x0, 0x84, 0x0, 0x0,
0xc3, 0x3, 0x33, 0xa0, 0x59, 0x88, 0x30, 0xc4,
0x0, 0x0, 0xc0, 0x0, 0x0, 0xab, 0xbb, 0x50,

/* U+33 "3" */
0xc, 0xbb, 0x93, 0x0, 0x0, 0x84, 0x0, 0x0,
0xc3, 0x3, 0x33, 0xa0, 0x8, 0x88, 0xc0, 0x0,
0x0, 0xd0, 0x0, 0x1, 0xc0, 0x9b, 0xba, 0xa0,

/* U+34 "4" */
0x26, 0x0, 0x43, 0x49, 0x0, 0x84, 0x58, 0x0,
0xc4, 0x77, 0x33, 0xb0, 0x7, 0x88, 0xc0, 0x0,
0x0, 0xd0, 0x0, 0x1, 0xc0, 0x0, 0x1, 0xa0,

/* U+35 "5" */
0x29, 0xbb, 0x54, 0x90, 0x0, 0x58, 0x0, 0x7,
0x73, 0x32, 0x7, 0x88, 0xc0, 0x0, 0xd, 0x0,
0x1, 0xc9, 0xbb, 0xaa,

/* U+36 "6" */
0xc, 0xbb, 0xb3, 0x47, 0x0, 0x0, 0x58, 0x0,
0x0, 0x78, 0x33, 0x20, 0x58, 0x88, 0xc0, 0xc4,
0x0, 0xe0, 0xc0, 0x1, 0xc0, 0xab, 0xbb, 0x70,

/* U+37 "7" */
0xcb, 0xb9, 0x30, 0x0, 0x84, 0x0, 0xc, 0x30,
0x0, 0x90, 0x0, 0x9, 0x0, 0x0, 0xd0, 0x0,
0x1c, 0x0, 0x1, 0x90,

/* U+38 "8" */
0xc, 0xbb, 0x93, 0x47, 0x0, 0x84, 0x58, 0x0,
0xc4, 0x78, 0x33, 0xb0, 0x58, 0x88, 0xc0, 0xc4,
0x0, 0xe0, 0xc0, 0x1, 0xc0, 0xab, 0xbb, 0x70,

/* U+39 "9" */
0xb, 0xbb, 0xa3, 0x48, 0x0, 0x84, 0x48, 0x0,
0xa4, 0x88, 0x33, 0xb1, 0x7, 0x88, 0xa0, 0x0,
0x0, 0xf0, 0x0, 0x0, 0xc0, 0x9b, 0xbb, 0x70,

/* U+3A ":" */
0x12, 0x35, 0x0, 0x0, 0x93
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
        {.bitmap_index = 0, .adv_w = 0, .box_h = 0, .box_w = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
        {.bitmap_index = 0, .adv_w = 55, .box_h = 1, .box_w = 2, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 1, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 25, .adv_w = 77, .box_h = 8, .box_w = 3, .ofs_x = 2, .ofs_y = 0},
        {.bitmap_index = 37, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 61, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 85, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 109, .adv_w = 107, .box_h = 8, .box_w = 5, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 129, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 153, .adv_w = 107, .box_h = 8, .box_w = 5, .ofs_x = 2, .ofs_y = 0},
        {.bitmap_index = 173, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 197, .adv_w = 107, .box_h = 8, .box_w = 6, .ofs_x = 1, .ofs_y = 0},
        {.bitmap_index = 221, .adv_w = 55, .box_h = 5, .box_w = 2, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

//static const uint8_t glyph_id_ofs_list_0[] = {
//        0, 0, 1, 2, 3, 4, 5, 6,
//        7, 8, 9, 10, 11
//};
//
/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
        {
                {
                        .range_start = 46, .range_length = 13, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
                        .glyph_id_start = 1,
                }
        };

/*-----------------
 *    KERNING
 *----------------*/


///*Pair left and right glyphs for kerning*/
//static const uint8_t kern_pair_glyph_ids[] =
//        {
//                1, 3,
//                1, 6,
//                12, 3
//        };
//
///* Kerning between the respective left and right glyphs
// * 4.4 format which needs to scaled with `kern_scale`*/
//static const int8_t kern_pair_values[] =
//        {
//                -21, -21, -21
//        };

///*Collect the kern pair's data in one place*/
//static const lv_font_fmt_txt_kern_pair_t kern_pairs =
//        {
//                .glyph_ids = kern_pair_glyph_ids,
//                .values = kern_pair_values,
//                .pair_cnt = 3,
//                .glyph_ids_size = 0
//        };

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

/*Store all the custom data of the font*/
static lv_font_fmt_txt_dsc_t font_dsc = {
        .glyph_bitmap = gylph_bitmap,
        .glyph_dsc = glyph_dsc,
        .cmaps = cmaps,
        .cmap_num = 1,
        .bpp = 4,

//        .kern_scale = 16,
//        .kern_dsc = &kern_pairs,
//        .kern_classes = 0
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
const lv_font_t lcd12 = {
        .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
//        .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
//        .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
        .line_height = 9,          /*The maximum line height required by the font*/
        .base_line = 1,             /*Baseline measured from the bottom of the line*/
};

#endif /*#if LCD12*/
