#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_error.h"
#include "../../cpymo/cpymo_color.h"
#include "../../cpymo/cpymo_str.h"
#include "../include/cpymo_backend_text.h"
#include "cpymo_backend_software.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef TEXT_LINE_Y_OFFSET
#define TEXT_LINE_Y_OFFSET 0
#endif

#ifndef TEXT_CHARACTER_W_SCALE
#define TEXT_CHARACTER_W_SCALE 4
#endif

/* Glyphs may reach above the ascent (pixel fonts with a tight em box draw up
 * to the em edge, symbols above it), which makes baseline + y0 negative and
 * wrote before the bitmap. Reserve a few rows at the top instead. */
#ifndef TEXT_TOP_PAD
#define TEXT_TOP_PAD 8
#endif

extern cpymo_backend_software_context 
    *cpymo_backend_software_cur_context;

static void cpymo_backend_text_render(
    void *out_or_null, 
    int *w, int *h, 
    cpymo_str text, 
    float scale, float baseline) 
{
    stbtt_fontinfo *font = cpymo_backend_software_cur_context->font;
	float xpos = 0;

	int width = 0, height = 0;
	int pad_top = TEXT_TOP_PAD;
	float y_base = 0;
	while (text.len > 0) {
		uint32_t codepoint = cpymo_str_utf8_try_head_to_utf32(&text);
		int x0, y0, x1, y1;

		int advance_width, lsb;
		float x_shift = xpos - (float)floor(xpos);
		stbtt_GetCodepointHMetrics(font, (int)codepoint, &advance_width, &lsb);

		if (codepoint == '\n') {
			xpos = 0;

			y_base += baseline + TEXT_LINE_Y_OFFSET;

			continue;
		}

		stbtt_GetCodepointBitmapBoxSubpixel(font, (int)codepoint, scale, scale, x_shift, 0, &x0, &y0, &x1, &y1);

		/* Some glyphs reach above the ascent. Grow the top margin instead of
		 * writing before the bitmap, which would overwrite the header. */
		{
			int above = (int)(baseline + (float)y0 + y_base);
			if (above < 0 && -above + 1 > pad_top) pad_top = -above + 1;
		}

		if (out_or_null)
			stbtt_MakeCodepointBitmapSubpixel(
				font,
				(unsigned char *)out_or_null + (int)xpos + x0 + (pad_top + (int)(baseline + (float)y0 + y_base)) * *w,
				x1 - x0, y1 - y0, *w, scale, scale, x_shift, 0, (int)codepoint);

		xpos += (advance_width * scale);

		cpymo_str text2 = text;
		uint32_t next_char = cpymo_str_utf8_try_head_to_utf32(&text2);
		if (next_char) {
			xpos += scale * stbtt_GetCodepointKernAdvance(font, codepoint, next_char);
		}

		int new_width = (int)ceil(xpos);
		if (new_width > width) width = new_width;

		int new_height = (int)((y1 - y0) + baseline + y_base);
		if (new_height > height) height = new_height;
	}

	*w = width;
	*h = height + pad_top;
}

typedef struct {
    size_t w, h;
    float baseline;
    uint8_t px[0];
} cpymo_backend_text_impl;

error_t cpymo_backend_text_create(
    cpymo_backend_text *out, 
    float *out_width,
    cpymo_str utf8_string, 
    float single_character_size_in_logical_screen)
{
    float game_w = cpymo_backend_software_cur_context->logical_screen_w;
    float game_h = cpymo_backend_software_cur_context->logical_screen_h;

    float win_w = (float)cpymo_backend_software_cur_context->render_target->w;
    float win_h = (float)cpymo_backend_software_cur_context->render_target->h;

    stbtt_fontinfo *font = cpymo_backend_software_cur_context->font;

    float height_norm = single_character_size_in_logical_screen / game_h;
    float height_screen = height_norm * win_h;

    float scale = stbtt_ScaleForPixelHeight(font, height_screen);
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    float baseline = scale * ascent;

    int w, h;
    cpymo_backend_text_render(NULL, &w, &h, utf8_string, scale, baseline);
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX measure len=%zu w=%d h=%d\n", utf8_string.len, w, h);
#endif
    h += 4; // magic

    cpymo_backend_text_impl *o = (cpymo_backend_text_impl *)malloc(sizeof(cpymo_backend_text_impl) + w * h);
    if (o == NULL) return CPYMO_ERR_OUT_OF_MEM;
    memset(o, 0, sizeof(cpymo_backend_text_impl) + w * h);
    o->w = (uint16_t)w;
    o->h = (uint16_t)h;
    *out_width = cpymo_backend_text_width(utf8_string, single_character_size_in_logical_screen);
    cpymo_backend_text_render(o->px, &w, &h, utf8_string, scale, baseline);
    *out = o;
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX create out=%p len=%zu w=%d h=%d\n", (void *)o, utf8_string.len, w, h);
#endif

    scale = stbtt_ScaleForPixelHeight(font, single_character_size_in_logical_screen);
    o->baseline = scale * ascent;
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX end  out=%p w=%u h=%u base=%f\n", (void *)o,
        (unsigned)o->w, (unsigned)o->h, o->baseline);
#endif

    return CPYMO_ERR_SUCC;   
}

void cpymo_backend_text_free(cpymo_backend_text t){
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX free %p\n", t);
#endif
    free(t); }

static void cpymo_backend_text_draw_internal(cpymo_color col, float x, float y, float alpha, cpymo_backend_text_impl *t)
{
    cpymo_backend_software_image *render_target = 
        cpymo_backend_software_cur_context->render_target;

    float 
        window_size_w = (float)render_target->w,
        window_size_h = (float)render_target->h;
    
    x /= cpymo_backend_software_cur_context->logical_screen_w;
    x *= window_size_w;
    y /= cpymo_backend_software_cur_context->logical_screen_h;
    y *= window_size_h;

    /* (float)k/255.0f, read from the shared table instead of divided per
     * pixel: bit-identical, and the whole text box is redrawn on most frames
     * of a dialogue. */
    const float cr = cpymo_backend_software_u8f[col.r];
    const float cg = cpymo_backend_software_u8f[col.g];
    const float cb = cpymo_backend_software_u8f[col.b];

    /* Convert through a signed intermediate: casting a negative float straight
     * to size_t is undefined, and text is regularly drawn above or left of the
     * window (UBSan caught -57 -> unsigned long). */
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TD begin t=%p w=%u h=%u x=%f y=%f\n", (void *)t, (unsigned)t->w, (unsigned)t->h, x, y);
#endif
    for (uint16_t draw_rect_y = 0; draw_rect_y < t->h; ++draw_rect_y) {
        const uint8_t *src_row = t->px + (size_t)draw_rect_y * t->w;

        for (uint16_t draw_rect_x = 0; draw_rect_x < t->w * TEXT_CHARACTER_W_SCALE; ++draw_rect_x) {
            long draw_x = (long)x + (long)draw_rect_x;
            long draw_y = (long)y + (long)draw_rect_y;

            if (draw_x < 0 || draw_y < 0 ||
                draw_x >= (long)window_size_w || draw_y >= (long)window_size_h)
                continue;

            float pixel_alpha = cpymo_backend_software_u8f[
                src_row[draw_rect_x / TEXT_CHARACTER_W_SCALE]];

            /* pixel_alpha == 0 blends the current byte onto itself, so the
             * write is a no-op; skipping it is equivalent, not a shortcut. */
            if (pixel_alpha == 0.0f) continue;

            float a = alpha * pixel_alpha;
            if (a == 0.0f) continue;

            cpymo_backend_software_image_write_blend(
                render_target, 
                (size_t)draw_x,
                (size_t)draw_y,
                cr,
                cg,
                cb,
                a);
        }
    }
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TD end\n");
#endif
}

void cpymo_backend_text_draw(
    cpymo_backend_text t_,
    float x, float y_baseline,
    cpymo_color col, float alpha,
    enum cpymo_backend_image_draw_type draw_type)
{
    cpymo_backend_text_impl *t = (cpymo_backend_text_impl *)t_;
    float y = y_baseline - t->baseline;

    cpymo_backend_text_draw_internal(cpymo_color_inv(col), x + 1, y + 1, alpha, t);
    cpymo_backend_text_draw_internal(col, x, y, alpha, t);
}

float cpymo_backend_text_width(
    cpymo_str s,
    float single_character_size_in_logical_screen)
{ 
    float game_w = cpymo_backend_software_cur_context->logical_screen_w;
    float game_h = cpymo_backend_software_cur_context->logical_screen_h;

    float win_w = (float)cpymo_backend_software_cur_context->render_target->w;
    float win_h = (float)cpymo_backend_software_cur_context->render_target->h;

    stbtt_fontinfo *font = cpymo_backend_software_cur_context->font;

    float height_norm = single_character_size_in_logical_screen / game_h;
    float height_screen = height_norm * win_h;
    float scale = stbtt_ScaleForPixelHeight(font, height_screen);
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    float baseline = scale * ascent;
    int w, h;
    cpymo_backend_text_render(NULL, &w, &h, s, scale, baseline);

    return TEXT_CHARACTER_W_SCALE * (float)w / win_w * game_w;
}
