#include "garlic_adapter.h"
#include "garlic_bezel_art.h"
#include "garlic_smoked_resin.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

garlic_actions garlic_map_buttons(uint32_t b, bool ui, bool script_menu,
    bool pick_a_slot)
{
    garlic_actions a = {0};
    a.up = (b & GARLIC_BUTTON_UP) != 0;
    a.down = (b & GARLIC_BUTTON_DOWN) != 0;
    a.left = (b & GARLIC_BUTTON_LEFT) != 0;
    a.right = (b & GARLIC_BUTTON_RIGHT) != 0;
    bool story = !ui && !script_menu && !pick_a_slot;
    a.ok = (b & GARLIC_BUTTON_A) != 0 || (story && (b & GARLIC_BUTTON_DOWN));
    a.cancel = (b & (ui || script_menu || pick_a_slot ? GARLIC_BUTTON_B
                     : GARLIC_BUTTON_X | GARLIC_BUTTON_START)) != 0;
    a.skip = story && (b & GARLIC_BUTTON_R) != 0;
    a.hide_window = story && (b & GARLIC_BUTTON_B) != 0;
    a.quick_save = story && (b & GARLIC_BUTTON_R2) != 0;
    a.quick_load = story && (b & GARLIC_BUTTON_L2) != 0;
    a.auto_play = story && (b & GARLIC_BUTTON_Y) != 0;
    a.settings = story && (b & GARLIC_BUTTON_SELECT) != 0;
    if (story) a.down = false; /* a forward key, not a second navigation event */
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

static void garlic_fill_bezel_band(uint16_t *band, int height, bool bottom)
{
    const int rim_rows = sizeof(bezel_rim_colors) / sizeof(*bezel_rim_colors);
    int surface_height = height - rim_rows;
    for (int y = 0; y < height; ++y) {
        int depth = bottom ? y : height - 1 - y;
        uint16_t *line = band + (size_t)y * GARLIC_OUTPUT_WIDTH;
        if (depth < rim_rows) {
            uint16_t color = bezel_rim_colors[depth];
            for (int x = 0; x < GARLIC_OUTPUT_WIDTH; ++x)
                line[x] = color;
        } else {
            int surface_y = bottom ? y - rim_rows : y;
            int texture_y = surface_height > 1
                ? surface_y * (GARLIC_RESIN_ROWS - 1) / (surface_height - 1)
                : 0;
            memcpy(line, smoked_resin_face + (size_t)texture_y * GARLIC_OUTPUT_WIDTH,
                   GARLIC_OUTPUT_WIDTH * sizeof(*line));
        }
    }
}

static void garlic_draw_bezel_detail(uint16_t *band, int height,
    int x, int y, const uint16_t *detail, int width, int rows)
{
    if (y < 0 || rows > height - y) return;
    for (int row = 0; row < rows; ++row) {
        uint16_t *line = band + (size_t)(y + row) * GARLIC_OUTPUT_WIDTH + x;
        for (int col = 0; col < width; ++col) {
            if (detail[col]) line[col] = detail[col];
        }
        detail += width;
    }
}

static void garlic_draw_bezel_lamp(uint16_t *frame, int height)
{
    static const uint16_t transmission[] = {
        256, 250, 243, 232, 219, 204, 188, 170, 152, 134, 116, 98,
        80, 64, 49, 37, 27, 19, 12, 7, 4, 2, 1,
    };
    static const uint16_t spread[] = {
        256, 255, 254, 251, 248, 243, 238, 232, 225, 218, 211, 204,
        196, 189, 181, 174, 167, 160, 153, 146, 140, 134, 128, 122,
        116, 110, 105, 100, 94, 89, 84, 80, 75, 70, 66, 62,
        58, 54, 50, 47, 43, 40, 37, 34, 32, 29, 27, 24,
        22, 20, 19, 17, 15, 14, 13, 11, 10, 9, 8, 7,
        7, 6, 5, 5, 4, 4, 3, 3,
    };
    /* Align the glow with the RG35XX indicator. */
    const int center_right = 132;
    const int horizontal = sizeof(spread) / sizeof(*spread);
    const int profile_height = sizeof(transmission) / sizeof(*transmission);
    int rows = profile_height;
    if (rows > height) rows = height;
    for (int y = 0; y < rows; ++y) {
        uint16_t *line = frame + (size_t)y * GARLIC_OUTPUT_WIDTH;
        unsigned vertical = transmission[y];
        if (height < profile_height && height - y < 5)
            vertical = vertical * (unsigned)(height - y) / 5;
        for (int x = center_right - horizontal; x < center_right + horizontal; ++x) {
            int offset = x - center_right;
            int distance = offset < 0 ? -1 - offset : offset;
            unsigned light = vertical * spread[distance];
            /* Keep the same RGB565 dither in coordinates relative to the lamp. */
            uint32_t grain = (uint32_t)(129 + offset + (GARLIC_OUTPUT_WIDTH + 1) * y) * UINT32_C(0x9e3779b9);
            grain ^= grain >> 13;
            unsigned threshold = 8192 + ((grain >> 19) & 32767);
            unsigned base = line[x];
            unsigned g = ((base >> 5) & 63) + ((10 * light + threshold) >> 16);
            unsigned b = (base & 31) - ((2 * light + threshold) >> 16);
            line[x] = (uint16_t)((base & 0xf800) | (g << 5) | b);
        }
    }
}

void garlic_draw_bezel(uint16_t *frame, const garlic_viewport *picture)
{
    if (picture->x != 0 || picture->w != GARLIC_OUTPUT_WIDTH ||
        picture->y <= 0 || picture->h <= 0 ||
        picture->h >= GARLIC_OUTPUT_HEIGHT - picture->y)
        return;

    int top_height = picture->y;
    int bottom_y = top_height + picture->h;
    int bottom_height = GARLIC_OUTPUT_HEIGHT - bottom_y;
    uint16_t *bottom = frame + (size_t)bottom_y * GARLIC_OUTPUT_WIDTH;

    garlic_fill_bezel_band(frame, top_height, false);
    garlic_fill_bezel_band(bottom, bottom_height, true);
    garlic_draw_bezel_lamp(frame, top_height - 3);

    /* Keep the approved ornaments at their native size as the bars deepen. */
    int accent_y = top_height / 2 - 2;
    const int accent_inset = 47;
    garlic_draw_bezel_detail(frame, top_height - 3, accent_inset, accent_y,
        bezel_accent, BEZEL_ACCENT_W, BEZEL_ACCENT_H);
    garlic_draw_bezel_detail(frame, top_height - 3,
        GARLIC_OUTPUT_WIDTH - accent_inset - BEZEL_ACCENT_W, accent_y,
        bezel_accent, BEZEL_ACCENT_W, BEZEL_ACCENT_H);

    int vent_y = bottom_height / 2 - 3;
    if (vent_y >= 3) {
        garlic_draw_bezel_detail(bottom, bottom_height, 231, vent_y,
            bezel_vent, BEZEL_VENT_W, BEZEL_VENT_H);
        garlic_draw_bezel_detail(bottom, bottom_height, 393, vent_y,
            bezel_vent, BEZEL_VENT_W, BEZEL_VENT_H);
    }
    if (bottom_height >= 27)
        garlic_draw_bezel_detail(bottom, bottom_height,
            (GARLIC_OUTPUT_WIDTH - BEZEL_PLAQUE_W + 1) / 2, bottom_height / 2 - 8,
            bezel_plaque, BEZEL_PLAQUE_W, BEZEL_PLAQUE_H);
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

/* Exact source footprints, normalized to Q8 weights. The handheld's largest
   accepted canvas is 2048x2048, giving at most six taps at 640x480. Unusual
   caller geometries retain the bounded nearest implementation. */
typedef struct { uint16_t first, count, weight[6]; } garlic_area_axis;
static bool area_axis(garlic_area_axis *a, int x, int src, int dst)
{
    uint32_t lo=(uint32_t)x*src, hi=(uint32_t)(x+1)*src;
    unsigned first=lo/dst, end=(hi+dst-1)/dst;
    if (end>(unsigned)src) end=src;
    if (end<=first || end-first>6) return false;
    a->first=first; a->count=end-first;
    unsigned total=0;
    for (unsigned i=0;i<a->count;i++) {
        uint32_t l=(first+i)*dst, r=(first+i+1)*dst;
        if (l<lo) l=lo;
        if (r>hi) r=hi;
        unsigned w=i+1==a->count ? 256-total : (r-l)*256u/src;
        a->weight[i]=w; total+=w;
    }
    return true;
}

bool garlic_blit_rgb565_quality(uint16_t *dst,size_t pitch,int dw,int dh,
    const uint8_t *src,size_t spitch,int sw,int sh)
{
    garlic_viewport v;
    if (!dst || !src || dw<=0 || dh<=0 || dw>1024 || dh>1024 ||
        sw<=0 || sh<=0 || sw>2048 || sh>2048 || pitch<(size_t)dw*2 ||
        (pitch&1u) || spitch<(size_t)sw*4 ||
        (size_t)dh>SIZE_MAX/pitch || (size_t)sh>SIZE_MAX/spitch ||
        !garlic_compute_viewport(sw,sh,dw,dh,&v) || (sw<=v.w && sh<=v.h))
        return garlic_blit_rgb565(dst,pitch,dw,dh,src,spitch,sw,sh);
    garlic_area_axis columns[1024], row;
    for (int x=0;x<v.w;x++)
        if (!area_axis(columns+x,x,sw,v.w))
            return garlic_blit_rgb565(dst,pitch,dw,dh,src,spitch,sw,sh);
    for (int y=0;y<dh;y++) memset((uint8_t *)dst+(size_t)y*pitch,0,(size_t)dw*2);
    for (int y=0;y<v.h;y++) {
        if (!area_axis(&row,y,sh,v.h))
            return garlic_blit_rgb565(dst,pitch,dw,dh,src,spitch,sw,sh);
        uint16_t *out=(uint16_t *)((uint8_t *)dst+(size_t)(y+v.y)*pitch);
        for (int x=0;x<v.w;x++) {
            const garlic_area_axis *col=columns+x;
            uint32_t r=0,g=0,b=0;
            for (unsigned j=0;j<row.count;j++) {
                const uint8_t *p=src+(size_t)(row.first+j)*spitch+(size_t)col->first*4;
                for (unsigned i=0;i<col->count;i++,p+=4) {
                    uint32_t w=(uint32_t)row.weight[j]*col->weight[i];
                    b+=p[0]*w; g+=p[1]*w; r+=p[2]*w;
                }
            }
            r=(r+32768)>>16; g=(g+32768)>>16; b=(b+32768)>>16;
            out[x+v.x]=(uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));
        }
    }
    return true;
}
