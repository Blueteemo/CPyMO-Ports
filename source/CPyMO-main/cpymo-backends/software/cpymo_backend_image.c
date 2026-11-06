#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_utils.h"
#include "../../stb/stb_image_resize.h"
#include "../include/cpymo_backend_image.h"
#include "cpymo_backend_software.h"
#include <stdlib.h>
#include <stdio.h>
#include "../../cpymo/garlic_stages.h"

extern cpymo_backend_software_context 
    *cpymo_backend_software_cur_context;

void cpymo_backend_image_scale_on_load(
    void **pixels, int *width, int *height, size_t channels)
{
    if (!cpymo_backend_software_cur_context->scale_on_load_image)
        return;

    uint64_t stage_begin=garlic_stage_begin();
    int new_width = 
        *width 
        * cpymo_backend_software_cur_context->scale_on_load_image_w_ratio;
        
    int new_height =
        *height
        * cpymo_backend_software_cur_context->scale_on_load_image_h_ratio;
        
    if (new_width < 1) new_width=1;
    if (new_height < 1) new_height=1;
    uint8_t *new_pixel = (uint8_t *)malloc(new_height * new_width * channels);
    if (new_pixel == NULL) return;

#ifdef GARLIC_NO_FFMPEG
    /* Filter once at load; RGBA is straight alpha, so let stb premultiply
       during filtering and unpremultiply afterwards to prevent dark fringes. */
    int ok = stbir_resize_uint8_generic(*pixels, *width, *height, *width * channels,
        new_pixel, new_width, new_height, new_width * channels, channels,
        channels == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE, 0, STBIR_EDGE_CLAMP,
        STBIR_FILTER_BOX, STBIR_COLORSPACE_LINEAR, NULL);
    if (!ok) { free(new_pixel); return; }
#else
    stbir_resize_uint8(*pixels,*width,*height,*width*channels,
        new_pixel,new_width,new_height,new_width*channels,channels);
#endif
    free(*pixels);
    *pixels = (void *)new_pixel;
    *width = new_width;
    *height = new_height;
    garlic_stage_end(GARLIC_STAGE_SCALE,stage_begin);
}

error_t cpymo_backend_image_load(
	cpymo_backend_image *out_image, 
    void *pixels_moveintoimage, 
    int width, int height, 
    enum cpymo_backend_image_format format)
{
    cpymo_backend_software_image *img =
        (cpymo_backend_software_image*)malloc(sizeof(*img));
    if (img == NULL) return CPYMO_ERR_OUT_OF_MEM;

    switch (format) {
    case cpymo_backend_image_format_rgb:
        img->pixel_stride = 3;
        img->has_alpha_channel = false;
        break;
    case cpymo_backend_image_format_rgba:
        img->pixel_stride = 4;
        img->has_alpha_channel = true;
        break;
    default:
        printf("[Error] Can not load from format %d.\n", format);
        abort();
    }

    img->source_w=width; img->source_h=height;
    cpymo_backend_image_scale_on_load(
        &pixels_moveintoimage, &width, &height, img->pixel_stride);

    img->references = 1;
    img->w = (size_t)width;
    img->h = (size_t)height;
    img->pixels = pixels_moveintoimage;
    img->r_offset = 0;
    img->g_offset = 1;
    img->b_offset = 2;
    img->a_offset = 3;

    img->line_stride = img->w * img->pixel_stride;
    *out_image = (cpymo_backend_image *)img;
    return CPYMO_ERR_SUCC;
}

error_t cpymo_backend_image_load_with_mask(
	cpymo_backend_image *out_image, 
    void *px_rgbx32_moveinto, 
    void *mask_a8_moveinto, 
    int w, int h, 
    int mask_w, int mask_h)
{
    cpymo_utils_attach_mask_to_rgba_ex(
        px_rgbx32_moveinto, w, h, mask_a8_moveinto, mask_w, mask_h);

	error_t err = cpymo_backend_image_load(
        out_image, px_rgbx32_moveinto, w, h, cpymo_backend_image_format_rgba);

    if (err == CPYMO_ERR_SUCC) 
        free(mask_a8_moveinto);
    
    return err;
}

void cpymo_backend_image_free(cpymo_backend_image image)
{
    cpymo_backend_software_image *p = 
        (cpymo_backend_software_image *)image;
    if (p->references > 1) { p->references--; return; }
    free(p->pixels);
    free(p);
}

static inline void cpymo_backend_image_trans_pos(float *x, float *y)
{
    float game_w = cpymo_backend_software_cur_context->logical_screen_w;
	float game_h = cpymo_backend_software_cur_context->logical_screen_h;

	float scr_w = (float)cpymo_backend_software_cur_context->render_target->w;
	float scr_h = (float)cpymo_backend_software_cur_context->render_target->h;

	*x = *x / game_w * scr_w;
	*y = *y / game_h * scr_h;
}

/* Source column chosen for every destination column of one draw.
 *
 * v0.21 recomputed the mapping inside the pixel loop, so a 640x480 repaint ran
 * the float multiply, the float divide, the clamp and the multiply again
 * ~307,200 times. The mapping depends only on `dst_x` (and likewise on
 * `dst_y` alone), so it can be resolved once per column and once per row.
 *
 * The arithmetic order below is copied from cpymo_backend_software_image_sample_nearest()
 * and cpymo_backend_image_draw() so that every mapping still rounds identically;
 * on a target with FMA the fused forms are avoided by keeping the
 * multiplication and the addition in separate statements. */
typedef struct {
    size_t  *su;          /* source column, one per destination column  */
    size_t  *sv;          /* source row, one per destination row       */
    size_t   su_stack[CPYMO_BACKEND_SOFTWARE_X_STACK];
    size_t   sv_stack[CPYMO_BACKEND_SOFTWARE_X_STACK];
    bool     su_heap, sv_heap;
} cpymo_backend_image_lookup;

/* The rows/columns a draw actually touches, after clipping to the render
 * target, plus the offset of the first kept index in the ORIGINAL (unclipped)
 * destination rectangle. The UV mapping is a function of that original
 * index, `draw_x - x1` / `draw_y - y1` in v0.21, and a partially visible
 * draw starts in the middle of it.  Forgetting this offset shifts the whole
 * sprite by the clipped amount; it is the one bug this table can introduce
 * that a same-size full-canvas draw cannot reveal. */
static void clipped_range(
    int x1, int x2, int y1, int y2,
    int rtw, int rth,
    int *cx1, int *cx2, int *cy1, int *cy2, int *ix0, int *iy0)
{
    int a = x1 < 0 ? 0 : x1;
    int b = x2 > rtw ? rtw : x2;
    int c = y1 < 0 ? 0 : y1;
    int d = y2 > rth ? rth : y2;
    *cx1 = a; *cx2 = b; *cy1 = c; *cy2 = d;
    *ix0 = a - x1;
    *iy0 = c - y1;
}

static bool lookup_init(
    cpymo_backend_image_lookup *lk,
    const cpymo_backend_software_image *src,
    int x1, int x2, int y1, int y2,
    int ix0, int iy0,
    float u_offset, float u_scale, float v_offset, float v_scale,
    float dstw, float dsth)
{
    const float tex_w_m1 = (float)src->w - 1.0f;
    const float tex_h_m1 = (float)src->h - 1.0f;

    const size_t cols = (size_t)(x2 - x1);
    const size_t rows = (size_t)(y2 - y1);

    lk->su = lk->su_stack;
    lk->sv = lk->sv_stack;
    lk->su_heap = lk->sv_heap = false;

    if (cols > CPYMO_BACKEND_SOFTWARE_X_STACK) {
        lk->su = (size_t *)malloc(cols * sizeof(*lk->su));
        if (!lk->su) return false;
        lk->su_heap = true;
    }
    if (rows > CPYMO_BACKEND_SOFTWARE_X_STACK) {
        lk->sv = (size_t *)malloc(rows * sizeof(*lk->sv));
        if (!lk->sv) {
            if (lk->su_heap) free(lk->su);
            return false;
        }
        lk->sv_heap = true;
    }

    /* dstw/dsth are the integer-aligned extents and are > 0 whenever this is
     * called. Entry i is the mapping of the ORIGINAL destination index
     * ix0 + i, i.e. the per-pixel `u_offset + u_scale * (draw_x - x1) / dstw`
     * and the `clampf` and the `(size_t)(u * (w-1))` of
     * cpymo_backend_software_image_sample_nearest(), computed once.
     *
     * The wording says `du` above is a float; the cast is applied at the end
     * only to keep the float->float subtractions and the cast in exactly the
     * order of the original, so every index still truncates the same way. */
    for (size_t i = 0; i < cols; ++i) {
        float du = (float)(ix0 + (int)i);
        float u = u_offset + u_scale * du / dstw;
        u = cpymo_utils_clampf(u, 0.0f, 1.0f);
        lk->su[i] = (size_t)(u * tex_w_m1);
    }
    for (size_t i = 0; i < rows; ++i) {
        float dv = (float)(iy0 + (int)i);
        float v = v_offset + v_scale * dv / dsth;
        v = cpymo_utils_clampf(v, 0.0f, 1.0f);
        lk->sv[i] = (size_t)(v * tex_h_m1);
    }
    return true;
}

static void lookup_free(cpymo_backend_image_lookup *lk)
{
    if (lk->su_heap) free(lk->su);
    if (lk->sv_heap) free(lk->sv);
}

void cpymo_backend_image_draw(
	float dstx, float dsty, float dstw, float dsth,
	cpymo_backend_image src,
	int srcx, int srcy, int srcw, int srch, float alpha,
	enum cpymo_backend_image_draw_type draw_type)
{ 
    cpymo_backend_image_trans_pos(&dstx, &dsty);
    cpymo_backend_image_trans_pos(&dstw, &dsth);

    cpymo_backend_software_image *render_target =
        cpymo_backend_software_cur_context->render_target;

    int render_target_w = (int)render_target->w;
    int render_target_h = (int)render_target->h;

    int x1 = (int)dstx;
    int y1 = (int)dsty;
    int x2 = (int)(dstw + dstx);
    int y2 = (int)(dsth + dsty);

    dstw = (float)(x2 - x1);
    dsth = (float)(y2 - y1);

    if (x2 <= x1 || y2 <= y1) return;

    float scalex = 1.0f;
    float scaley = 1.0f;

    if (cpymo_backend_software_cur_context->scale_on_load_image) {
        scalex = 
            cpymo_backend_software_cur_context->scale_on_load_image_w_ratio;
            
        scaley =
            cpymo_backend_software_cur_context->scale_on_load_image_h_ratio;
    }

#ifdef GARLIC_NO_FFMPEG
    cpymo_backend_software_image *original = src;
    if (original->source_w && original->source_h) {
        scalex=(float)original->w/original->source_w;
        scaley=(float)original->h/original->source_h;
    }
#endif
    float fsrcx = scalex * (float)srcx;
    float fsrcy = scaley * (float)srcy;
    float fsrcw = scalex * (float)srcw;
    float fsrch = scaley * (float)srch;

    cpymo_backend_software_image *srci = (cpymo_backend_software_image *)src;
    float u_offset = fsrcx / (float)srci->w;
    float v_offset = fsrcy / (float)srci->h;
    float u_scale = fsrcw / (float)srci->w;
    float v_scale = fsrch / (float)srci->h;

    /* The per-pixel early exits of v0.21 skip out-of-range columns and stop the
     * row at the first column past the right edge, and stop entirely at the
     * first row past the bottom edge. Clipping the ranges once is equivalent
     * because dst_y and dst_x both increase. `ix0`/`iy0` remember where the
     * clipped window starts inside the original rectangle, so the UV mapping
     * below is still computed from the original index. */
    int cx1, cx2, cy1, cy2, ix0, iy0;
    clipped_range(x1, x2, y1, y2, render_target_w, render_target_h,
                 &cx1, &cx2, &cy1, &cy2, &ix0, &iy0);
    if (cx1 >= cx2 || cy1 >= cy2) return;

    cpymo_backend_image_lookup lk;
    if (!lookup_init(&lk, srci, cx1, cx2, cy1, cy2, ix0, iy0,
                     u_offset, u_scale, v_offset, v_scale, dstw, dsth))
        return;

    /* NOT (uint8_t)(alpha * 255.0f): that byte-quantized the fade and skipped
     * every 0 < alpha < 1/255 span, while v0.21 still blended it and could
     * lower an 8-bit channel by one. `a != 0` below is the exact test (with a
     * source alpha channel, a is the texel alpha times alpha; a == 0.0f makes
     * the blend collapse to (uint8_t)(u8f[dst] * 255.0f) == dst). */
    const bool opaque = (alpha == 1.0f);

    const size_t line_stride = srci->line_stride;
    const size_t pixel_stride = srci->pixel_stride;
    const size_t ro = srci->r_offset, go = srci->g_offset, bo = srci->b_offset;
    const bool src_alpha = srci->has_alpha_channel;
    const size_t ao = srci->a_offset;
    const uint8_t *src_pixels = srci->pixels;
    const size_t dro = render_target->r_offset;
    const size_t dgo = render_target->g_offset;
    const size_t dbo = render_target->b_offset;
    const size_t dstride = render_target->pixel_stride;

#ifdef GARLIC_NO_FFMPEG
    const unsigned global_q=(unsigned)(cpymo_utils_clampf(alpha,0,1)*65536.0f);
    if (!global_q) { lookup_free(&lk); return; }
#endif
    for (int draw_y = cy1; draw_y < cy2; ++draw_y) {
        const uint8_t *tex_row =
            src_pixels + lk.sv[(size_t)(draw_y - cy1)] * line_stride;
        uint8_t *dst_row =
            cpymo_backend_software_image_row(render_target, (size_t)draw_y)
            + (size_t)cx1 * dstride;
        /* `lk.su` is indexed per pixel, not stepped by one: the v0.21 mapping
         * trunc(u * (w-1)), so one source column serves a whole span of
         * adjacent destination columns when the source is wider. Entry 0 already
         * corresponds to the first CLIPPED column (see lookup_init). */
        const size_t *su = lk.su;

        for (int draw_x = cx1; draw_x < cx2; ++draw_x, ++su, dst_row += dstride) {
            const uint8_t *tex = tex_row + *su * pixel_stride;
#ifdef GARLIC_NO_FFMPEG
            unsigned aq=src_alpha ? (global_q*tex[ao])/255u : global_q;
            if (!aq) continue;
            if (aq==65536u) {
                dst_row[dro]=tex[ro];dst_row[dgo]=tex[go];dst_row[dbo]=tex[bo];
            } else {
                unsigned inv=65536u-aq;
                dst_row[dro]=(tex[ro]*aq+dst_row[dro]*inv)>>16;
                dst_row[dgo]=(tex[go]*aq+dst_row[dgo]*inv)>>16;
                dst_row[dbo]=(tex[bo]*aq+dst_row[dbo]*inv)>>16;
            }
            continue;
#endif
            if (opaque) {
                /* a == 1.0f: with no source alpha, v0.21's a = 1.0f * 1.0f;
                 * with source alpha, exactly the texel's own alpha. Both
                 * collapse the expression to u8f[c] * 255.0f, which truncates
                 * back to c for every byte value, so the three stores are the
                 * result of the original arithmetic, not an approximation. */
                const float a = src_alpha
                    ? cpymo_backend_software_u8f[tex[ao]] : 1.0f;
                if (a == 1.0f) {
                    dst_row[dro] = tex[ro];
                    dst_row[dgo] = tex[go];
                    dst_row[dbo] = tex[bo];
                    continue;
                }
                if (a == 0.0f) continue;
                cpymo_backend_software_image_write_blend(
                    render_target, (size_t)draw_x, (size_t)draw_y,
                    cpymo_backend_software_u8f[tex[ro]],
                    cpymo_backend_software_u8f[tex[go]],
                    cpymo_backend_software_u8f[tex[bo]],
                    a);
                continue;
            }

            /* `alpha != 0.0f` rules out the exact no-op; a tiny fade must
             * still go through the float blend, exactly as v0.21 did. When the
             * span's effective alpha is 0 (a 0-alpha texel, or alpha == 0),
             * the blend collapses back to the destination byte, so nothing is
             * written and `continue` is equivalent. */
            if (alpha == 0.0f) continue;

            float a = cpymo_backend_software_image_sample_alpha(
                srci, tex, alpha);
            if (a == 0.0f) continue;
            if (a == 1.0f) {
                dst_row[dro] = tex[ro];
                dst_row[dgo] = tex[go];
                dst_row[dbo] = tex[bo];
                continue;
            }
            cpymo_backend_software_image_write_blend(
                render_target, (size_t)draw_x, (size_t)draw_y,
                cpymo_backend_software_u8f[tex[ro]],
                cpymo_backend_software_u8f[tex[go]],
                cpymo_backend_software_u8f[tex[bo]],
                a);
        }
    }

    lookup_free(&lk);
}

void cpymo_backend_image_fill_rects(
	const float *xywh, size_t count,
	cpymo_color color, float alpha,
	enum cpymo_backend_image_draw_type draw_type)
{ 
    float r = cpymo_utils_clampf((float)color.r / 255.0f, 0.0f, 1.0f);
    float g = cpymo_utils_clampf((float)color.g / 255.0f, 0.0f, 1.0f);
    float b = cpymo_utils_clampf((float)color.b / 255.0f, 0.0f, 1.0f);
    alpha = cpymo_utils_clampf(alpha, 0.0f, 1.0f);

    cpymo_backend_software_image *render_target =
        cpymo_backend_software_cur_context->render_target;

    int render_target_w = (int)render_target->w;
    int render_target_h = (int)render_target->h;

    /* alpha == 0 leaves every destination byte untouched: the expression reduces
     * to (uint8_t)(u8f[d] * 255.0f) == d for all 256 byte values. */
    if (alpha == 0.0f) return;

    for (size_t i = 0; i < count; ++i) {
        const float *rect = xywh + 4 * i;
        float x = rect[0];
        float y = rect[1];
        float w = rect[2];
        float h = rect[3];
        cpymo_backend_image_trans_pos(&x, &y);
        cpymo_backend_image_trans_pos(&w, &h);

        int x1 = (int)x;
        int y1 = (int)y;
        int x2 = (int)(x + w);
        int y2 = (int)(y + h);

        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > render_target_w) x2 = render_target_w;
        if (y2 > render_target_h) y2 = render_target_h;
        if (x1 >= x2 || y1 >= y2) continue;

        const int span = x2 - x1;

        /* alpha == 1 writes the clamped colour itself: the general expression
         * reduces to (uint8_t)(u8f[c] * 255.0f) == c. */
        if (alpha == 1.0f) {
            const uint8_t cr = (uint8_t)(r * 255.0f);
            const uint8_t cg = (uint8_t)(g * 255.0f);
            const uint8_t cb = (uint8_t)(b * 255.0f);
            for (int draw_y = y1; draw_y < y2; ++draw_y) {
                uint8_t *dst = cpymo_backend_software_image_row(
                    render_target, (size_t)draw_y)
                    + (size_t)x1 * render_target->pixel_stride;
                for (int i = 0; i < span; ++i,
                         dst += render_target->pixel_stride) {
                    dst[render_target->r_offset] = cr;
                    dst[render_target->g_offset] = cg;
                    dst[render_target->b_offset] = cb;
                }
            }
            continue;
        }

        for (int draw_y = y1; draw_y < y2; ++draw_y) {
            for (int draw_x = x1; draw_x < x2; ++draw_x) {
#ifdef GARLIC_NO_FFMPEG
                uint8_t *d=cpymo_backend_software_image_ptr(render_target,draw_x,draw_y);
                unsigned q=(unsigned)(alpha*65536.0f),inv=65536u-q;
                d[render_target->r_offset]=(color.r*q+d[render_target->r_offset]*inv)>>16;
                d[render_target->g_offset]=(color.g*q+d[render_target->g_offset]*inv)>>16;
                d[render_target->b_offset]=(color.b*q+d[render_target->b_offset]*inv)>>16;
#else
                cpymo_backend_software_image_write_blend(
                    render_target,
                    (size_t)draw_x,
                    (size_t)draw_y,
                    r, g, b, alpha);
#endif
            }
        }
    }
}

bool cpymo_backend_image_album_ui_writable()
{ 
    #ifdef ENABLE_ALBUM_UI_WRITEABLE
    return true;
    #else
    return false;
    #endif
}
