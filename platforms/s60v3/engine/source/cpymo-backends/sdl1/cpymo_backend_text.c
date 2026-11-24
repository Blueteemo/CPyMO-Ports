#include "../../cpymo/cpymo_prelude.h"

#ifndef ENABLE_SDL_TTF
#include <SDL/SDL.h>
#include "../../stb/stb_truetype.h"
#include "../include/cpymo_backend_text.h"
#include "../include/cpymo_backend_image.h"
#include "../../cpymo/cpymo_utils.h"
#include "../../cpymo/cpymo_engine.h"
#ifdef S60V3
#include "cpymo_s60_rgb565.h"
#include "cpymo_s60_text_metrics.h"
#endif

#ifdef S60V3
void cpymo_s60_font_render(void *out_or_null, int *w, int *h, cpymo_str text,
    float pixel_height, float baseline, bool dialogue);
#else
const extern stbtt_fontinfo font;
#endif
const extern cpymo_engine engine;
extern SDL_Surface *framebuffer;


#ifndef FONT_RENDER_QUALITY     // 0 ~ 3
#define FONT_RENDER_QUALITY 3
#endif

#ifndef S60V3
extern void cpymo_backend_font_render(void *, int *, int *, cpymo_str, float, float);
#endif

static float text_baseline(float height, bool dialogue)
{
#ifdef S60V3
    return cpymo_s60_font_baseline(height, dialogue);
#else
    (void)dialogue;
    int ascent;
    stbtt_GetFontVMetrics(&font, &ascent, NULL, NULL);
    return ascent * stbtt_ScaleForPixelHeight(&font, height);
#endif
}

static void text_render(void *pixels, int *w, int *h, cpymo_str text,
    float height, float baseline, bool dialogue)
{
#ifdef S60V3
    cpymo_s60_font_render(pixels, w, h, text, height, baseline, dialogue);
#else
    (void)dialogue;
    cpymo_backend_font_render(pixels, w, h, text,
        stbtt_ScaleForPixelHeight(&font, height), baseline);
#endif
}

typedef struct {
    float baseline;
    size_t w, h;
    uint8_t px[0];
} cpymo_backend_text_impl;

static error_t create_text(
    cpymo_backend_text *out, 
    float *out_width,
    cpymo_str s, 
    float height, bool dialogue)
{
    const float baseline = text_baseline(height, dialogue);

    int w, h;
    text_render(NULL, &w, &h, s, height, baseline, dialogue);
    h += 4; // MAGIC FOR MORE MEMORY


    cpymo_backend_text_impl *impl = malloc(sizeof(cpymo_backend_text_impl) + w * h);
    if (impl == NULL) return CPYMO_ERR_OUT_OF_MEM;

    impl->baseline = baseline;
    impl->w = (size_t)w;
    impl->h = (size_t)h;

    memset(impl->px, 0, w * h);
    
    text_render(impl->px, &w, &h, s, height, baseline, dialogue);
    *out = impl;
    *out_width = (float)w;
    return CPYMO_ERR_SUCC;
}

error_t cpymo_backend_text_create(cpymo_backend_text *out, float *out_width,
    cpymo_str text, float height)
{
    return create_text(out, out_width, text, height, false);
}

#ifdef S60V3
error_t cpymo_s60_dialogue_text_create(cpymo_backend_text *out, float *out_width,
    cpymo_str text, float height)
{
    return create_text(out, out_width, text, height, true);
}
#endif

#ifdef S60V3
void cpymo_s60_text_ink(cpymo_backend_text text, float bounds[4])
{
    const cpymo_backend_text_impl *impl = (const cpymo_backend_text_impl *)text;
    size_t left = impl->w, right = 0, top = impl->h, bottom = 0;
    for (size_t y = 0; y < impl->h; ++y) {
        for (size_t x = 0; x < impl->w; ++x) {
            if (!impl->px[y * impl->w + x]) continue;
            if (x < left) left = x;
            if (x + 1 > right) right = x + 1;
            if (y < top) top = y;
            bottom = y + 1;
        }
    }
    bounds[0] = right ? (float)left : 0.0f;
    bounds[1] = bottom ? (float)top - impl->baseline : 0.0f;
    bounds[2] = right ? (float)right : (float)impl->w;
    bounds[3] = bottom ? (float)bottom - impl->baseline : 0.0f;
}

#endif

void cpymo_backend_text_free(cpymo_backend_text t_)
{
    free(t_);
}

static void cpymo_backend_text_draw_internal(
    cpymo_backend_text_impl *t, 
    float x_pos, float y_pos, 
    cpymo_color col, float alpha)
{
    extern cpymo_color getpixel(SDL_Surface *surface, int x, int y);
    extern void putpixel(SDL_Surface *surface, int x, int y, cpymo_color col, Uint8 alpha);

    SDL_Rect clip;
    SDL_GetClipRect(framebuffer, &clip);

#ifdef S60V3
    if (cpymo_s60_is_rgb565(framebuffer->format)) {
        const int bx = (int)x_pos + clip.x, by = (int)y_pos + clip.y;
        const int x0 = bx < clip.x ? clip.x - bx : 0;
        const int y0 = by < clip.y ? clip.y - by : 0;
        int x1 = clip.x + clip.w - bx, y1 = clip.y + clip.h - by;
        if (x1 > (int)t->w) x1 = (int)t->w;
        if (y1 > (int)t->h) y1 = (int)t->h;
        if (x0 >= x1 || y0 >= y1 || alpha <= 0.0f) return;
        const unsigned opacity = alpha >= 1.0f ? 64 : (unsigned)(alpha * 64.0f + 0.5f);
        const Uint16 color = (Uint16)SDL_MapRGB(framebuffer->format, col.r, col.g, col.b);
        for (int y = y0; y < y1; ++y) {
            Uint16 *dst = (Uint16 *)((Uint8 *)framebuffer->pixels +
                (by + y) * framebuffer->pitch) + bx + x0;
            const Uint8 *mask = t->px + y * t->w + x0;
            for (int x = x0; x < x1; ++x, ++mask, ++dst) {
                unsigned coverage = (*mask * opacity + 128) >> 8;
                if (coverage) *dst = cpymo_s60_blend565(*dst, color, coverage);
            }
        }
        return;
    }
#endif
    float src_r = col.r / 255.0f;
    float src_g = col.g / 255.0f;
    float src_b = col.b / 255.0f;

    int base_x = (int)x_pos + clip.x;
    int base_y = (int)y_pos + clip.y;

    for (int y = 0; y < t->h; ++y) {
        for (int x = 0; x < t->w; ++x) {
            int draw_x = x + base_x;
            int draw_y = y + base_y;

            if (draw_x < 0 || draw_x >= clip.x + clip.w || draw_y < 0 || draw_y >= clip.y + clip.h) continue;

            const uint8_t fontsmp = t->px[y * t->w + x];

#if (FONT_RENDER_QUALITY == 0 || FONT_RENDER_QUALITY == 1)
            if (!fontsmp) continue;
            putpixel(framebuffer, draw_x, draw_y, col, 255);
#else
            float fontpx = alpha * (fontsmp / 255.0f);
            if (fontpx == 0.0f) continue;
            cpymo_color dst = getpixel(framebuffer, draw_x, draw_y);
            float dst_r = dst.r / 255.0f;
            float dst_g = dst.g / 255.0f;
            float dst_b = dst.b / 255.0f;

            float blend_r = src_r * fontpx + dst_r * (1.0f - fontpx);
            float blend_g = src_g * fontpx + dst_g * (1.0f - fontpx);
            float blend_b = src_b * fontpx + dst_b * (1.0f - fontpx);

            dst.r = (uint8_t)cpymo_utils_clampf(blend_r * 255.0f, 0.0f, 255.0f);
            dst.g = (uint8_t)cpymo_utils_clampf(blend_g * 255.0f, 0.0f, 255.0f);
            dst.b = (uint8_t)cpymo_utils_clampf(blend_b * 255.0f, 0.0f, 255.0f);

            putpixel(framebuffer, draw_x, draw_y, dst, 255);
#endif
        }
    }
}

void cpymo_backend_text_draw(
    cpymo_backend_text t_,
    float x, float y_baseline,
    cpymo_color col, float alpha,
    enum cpymo_backend_image_draw_type draw_type)
{
    cpymo_backend_text_impl *t = (cpymo_backend_text_impl *)t_;
    float y = y_baseline - t->baseline;

    if (SDL_LockSurface(framebuffer) == -1) return;

#if (FONT_RENDER_QUALITY == 1 || FONT_RENDER_QUALITY == 3)
    cpymo_backend_text_draw_internal(t, x + 1, y + 1, cpymo_color_inv(col), alpha);
#endif
    cpymo_backend_text_draw_internal(t, x, y, col, alpha);

    SDL_UnlockSurface(framebuffer);
}

#ifdef S60V3
void cpymo_s60_text_draw_plain(cpymo_backend_text text, float x, float baseline,
    cpymo_color color, float alpha)
{
    cpymo_backend_text_impl *t = (cpymo_backend_text_impl *)text;
    if (SDL_LockSurface(framebuffer) == -1) return;
    cpymo_backend_text_draw_internal(t, x, baseline - t->baseline, color, alpha);
    SDL_UnlockSurface(framebuffer);
}
#endif

static float text_width(cpymo_str text, float height, bool dialogue)
{
    int w, h;
    text_render(NULL, &w, &h, text, height, text_baseline(height, dialogue), dialogue);
    return (float)w;
}

float cpymo_backend_text_width(cpymo_str text, float height)
{
    return text_width(text, height, false);
}

#ifdef S60V3
float cpymo_s60_dialogue_text_width(cpymo_str text, float height)
{
    return text_width(text, height, true);
}
#endif

#endif
