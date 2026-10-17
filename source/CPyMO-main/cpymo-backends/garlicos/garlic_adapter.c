#include "garlic_adapter.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

garlic_actions garlic_map_buttons(uint32_t b, bool dialogue)
{
    garlic_actions a = {0};
    a.up = (b & GARLIC_BUTTON_UP) != 0 || (dialogue && (b & GARLIC_BUTTON_SELECT) != 0);
    a.down = (b & GARLIC_BUTTON_DOWN) != 0;
    a.left = (b & GARLIC_BUTTON_LEFT) != 0;
    a.right = (b & GARLIC_BUTTON_RIGHT) != 0;
    a.ok = (b & GARLIC_BUTTON_A) != 0;
    a.cancel = (b & (GARLIC_BUTTON_B | GARLIC_BUTTON_START)) != 0;
    a.skip = (b & (GARLIC_BUTTON_X | GARLIC_BUTTON_L)) != 0;
    a.hide_window = (b & (GARLIC_BUTTON_Y | GARLIC_BUTTON_R)) != 0;
    return a;
}

bool garlic_compute_viewport(int sw, int sh, int dw, int dh, garlic_viewport *v)
{
    int64_t w, h;
    if (!v || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;
    /* Compare ratios without floating point. */
    if ((int64_t)dw * sh <= (int64_t)dh * sw) {
        w = dw;
        h = ((int64_t)dw * sh + sw / 2) / sw;
    } else {
        h = dh;
        w = ((int64_t)dh * sw + sh / 2) / sh;
    }
    if (w < 1 || h < 1 || w > dw || h > dh) return false;
    v->w = (int)w;
    v->h = (int)h;
    v->x = (dw - v->w) / 2;
    v->y = (dh - v->h) / 2;
    return true;
}

static inline uint16_t rgb565(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)(p[2] & 0xf8) << 8) |
                      ((uint16_t)(p[1] & 0xfc) << 3) |
                      ((uint16_t)(p[0]) >> 3));
}

bool garlic_blit_rgb565(uint16_t *dst, size_t pitch, int dw, int dh,
                        const uint8_t *src, size_t spitch, int sw, int sh)
{
    garlic_viewport v;
    if (!dst || !src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 ||
        dw > 16384 || dh > 16384 || sw > 16384 || sh > 16384 ||
        (size_t)dw > SIZE_MAX / 2 || pitch < (size_t)dw * 2 || (pitch & 1u) ||
        (size_t)sw > SIZE_MAX / 4 || spitch < (size_t)sw * 4 ||
        (size_t)dh > SIZE_MAX / pitch || (size_t)sh > SIZE_MAX / spitch ||
        !garlic_compute_viewport(sw, sh, dw, dh, &v)) return false;

    /* Clear each visible scanline to avoid showing the previous game's image
       in the black bars. Do not touch bytes beyond the active image width. */
    for (int y = 0; y < dh; ++y) {
        memset((uint8_t *)dst + (size_t)y * pitch, 0, (size_t)dw * 2);
    }

    /* Match v0.20 pixel-centre nearest neighbour exactly, but do the
     * integer divisions only once per *column*, not once per pixel.
     * 640x480 now avoids ~307,200 target ARM division operations/frame.
     * Bounds <= 16384 guarantee numerator <= 536870912 (uint32_t). */
    uint16_t x_stack[1024];
    uint16_t *x_lookup = x_stack;
    if (v.w > (int)(sizeof(x_stack) / sizeof(x_stack[0]))) {
        x_lookup = (uint16_t *)malloc((size_t)v.w * sizeof(*x_lookup));
        if (!x_lookup) return false;
    }
    for (int x = 0; x < v.w; ++x) {
        uint32_t sx = ((uint32_t)(2 * x + 1) * (uint32_t)sw)
            / (uint32_t)(2 * v.w);
        if (sx >= (uint32_t)sw) sx = (uint32_t)sw - 1;
        x_lookup[x] = (uint16_t)sx;
    }
    for (int y = 0; y < v.h; ++y) {
        uint32_t sy = ((uint32_t)(2 * y + 1) * (uint32_t)sh)
            / (uint32_t)(2 * v.h);
        if (sy >= (uint32_t)sh) sy = (uint32_t)sh - 1;
        const uint8_t *in = src + (size_t)sy * spitch;
        uint16_t *out = (uint16_t *)((uint8_t *)dst + (size_t)(y + v.y) * pitch);
        for (int x = 0; x < v.w; ++x)
            out[v.x + x] = rgb565(in + (size_t)x_lookup[x] * 4u);
    }
    if (x_lookup != x_stack) free(x_lookup);
    return true;
}
