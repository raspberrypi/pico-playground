#pragma once
#include "pico.h"
#include "image.h"

typedef struct {
    size_t size;
    const uint8_t *bytes;
} blob;

extern const struct palette32 pi_palette;
extern const struct palette32 riscv_palette;
extern const struct image_data pi400_image_data;
extern const struct image_data riscv_image_data;
