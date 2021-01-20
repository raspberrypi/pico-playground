#ifndef SOFTWARE_FONT_H
#define SOFTWARE_FONT_H

#include "pico.h"

typedef struct {
    uint16_t bitmap_index;
    uint16_t adv_w;
    int8_t box_w, box_h, ofs_x, ofs_y;
} __attribute__((packed)) lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint16_t range_start, range_length, glyph_id_start;//, list_length;
//    void *unicode_list, *glyph_id_ofs_list;
    enum {
        LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
        LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    } type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    uint8_t cmap_num, bpp, kern_scale, kern_classes;
    void *kern_dsc;
} lv_font_fmt_txt_dsc_t;

typedef struct {
    lv_font_fmt_txt_dsc_t *dsc;
    uint8_t line_height, base_line;
} lv_font_t;

extern const lv_font_t lcd12;
extern const lv_font_t lcd18;

#define LV_ATTRIBUTE_LARGE_CONST
#endif //SOFTWARE_FONT_H
