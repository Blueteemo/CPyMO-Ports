#ifndef INCLUDE_CPYMO_BACKEND_SOFTWARE
#define INCLUDE_CPYMO_BACKEND_SOFTWARE

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../stb/stb_truetype.h"

typedef struct {
    size_t w, h, line_stride, pixel_stride;
    size_t r_offset, g_offset, b_offset, a_offset;
    bool has_alpha_channel;
    uint8_t *pixels;
} cpymo_backend_software_image;

#define CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL(PIMAGE, X, Y, CHANNEL) \
    ((Y) * (PIMAGE)->line_stride + (X) * (PIMAGE)->pixel_stride + \
    (PIMAGE)->CHANNEL##_offset + (PIMAGE)->pixels)

/* Byte -> unit-float lookup, filled once by
 * cpymo_backend_software_set_context().  Entry `u8f[k]` is `(float)k / 255.0f`
 * computed in float, which is exactly what v0.21 wrote inline as `x / 255.0f`
 * and `(float)x / 255.0f` (an integer->float conversion of 0..255 is exact).
 * Reading the table is therefore byte-for-byte equivalent to the division while
 * removing several divisions from the per-pixel loops that dominate the frame time
 * on the RG35XX. */
extern float cpymo_backend_software_u8f[256];

typedef struct {
    bool scale_on_load_image;
    float scale_on_load_image_w_ratio, scale_on_load_image_h_ratio;

    float logical_screen_w, logical_screen_h;

    cpymo_backend_software_image *render_target;
    // render target will not write to alpha channel.

    stbtt_fontinfo *font;

    /* Optional output-resolution text layer. When `text_layer` is set, text is
     * rasterised at the on-screen scale and composited after the hardware-like
     * blit, so glyphs are never resampled (540x360 games would otherwise be
     * upscaled 1.185x with nearest neighbour and look chunky). Images keep
     * using render_target at the canvas size. */
    bool text_layer;
    uint32_t *text_layer_px;      /* 0xAABBGGRR, straight alpha */
    int text_layer_w, text_layer_h;
    float text_scale;             /* canvas -> output pixels */
    int text_off_x, text_off_y;   /* letterbox origin */
    int text_dirty_y0, text_dirty_y1;
} cpymo_backend_software_context;

void cpymo_backend_software_set_context(
    cpymo_backend_software_context *context);

/* Widest destination span handled from the stack by the draw backends; a wider
 * one falls back to one malloc for the row. */
#define CPYMO_BACKEND_SOFTWARE_X_STACK 1024

/* Address of one channel of one pixel.  One call per pixel is enough: the
 * channel offsets are constants of the image, so r/g/b follow by adding. */
static inline uint8_t *cpymo_backend_software_image_ptr(
    cpymo_backend_software_image *img, size_t x, size_t y)
{
    return img->pixels + y * img->line_stride + x * img->pixel_stride;
}

static inline void cpymo_backend_software_image_write_blend(
    cpymo_backend_software_image *render_target,
    size_t x, size_t y,
    float r, float g, float b, float a)
{
    uint8_t *dst = cpymo_backend_software_image_ptr(render_target, x, y);

    dst[render_target->r_offset] = (uint8_t)(
        (r * a + cpymo_backend_software_u8f[dst[render_target->r_offset]]
                 * (1.0f - a)) * 255.0f);
    dst[render_target->g_offset] = (uint8_t)(
        (g * a + cpymo_backend_software_u8f[dst[render_target->g_offset]]
                 * (1.0f - a)) * 255.0f);
    dst[render_target->b_offset] = (uint8_t)(
        (b * a + cpymo_backend_software_u8f[dst[render_target->b_offset]]
                 * (1.0f - a)) * 255.0f);
}

/* a == 1.0f makes the expression above collapse to u8f[c] * 255.0f, which
 * truncates back to c for every one of the 256 byte values (verified for the
 * full table, and again per frame by the pixel-parity harness).  Whole-byte
 * stores instead of six multiplies, three divides and three truncations. */
static inline void cpymo_backend_software_image_write_rgb_opaque(
    cpymo_backend_software_image *render_target,
    size_t x, size_t y,
    uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *dst = cpymo_backend_software_image_ptr(render_target, x, y);
    dst[render_target->r_offset] = r;
    dst[render_target->g_offset] = g;
    dst[render_target->b_offset] = b;
}

/* Colour view of a render target row, so an inner loop need not redo
 * `y * line_stride + x * pixel_stride` for every channel of every pixel. */
static inline uint8_t *cpymo_backend_software_image_row(
    cpymo_backend_software_image *img, size_t y)
{
    return img->pixels + y * img->line_stride;
}

/* The colour+alpha of one source pixel, before the per-pixel blend. */
static inline float cpymo_backend_software_image_sample_alpha(
    const cpymo_backend_software_image *img,
    const uint8_t *texel, float alpha)
{
    if (img->has_alpha_channel)
        return cpymo_backend_software_u8f[texel[img->a_offset]] * alpha;
    return alpha;
}

static inline void cpymo_backend_software_image_sample_nearest(
    const cpymo_backend_software_image *img,
    float u, float v,
    float *r, float *g, float *b, float *a)
{
    float tex_w = (float)img->w;
    float tex_h = (float)img->h;

    size_t su = (size_t)(u * (tex_w - 1));
    size_t sv = (size_t)(v * (tex_h - 1)); 

    *r = cpymo_backend_software_u8f[
        *CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL(img, su, sv, r)];

    *g = cpymo_backend_software_u8f[
        *CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL(img, su, sv, g)];

    *b = cpymo_backend_software_u8f[
        *CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL(img, su, sv, b)];

    if (img->has_alpha_channel) {
        *a = cpymo_backend_software_u8f[
            *CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL(img, su, sv, a)];
    }
    else {
        *a = 1.0f;
    }
}

#endif
