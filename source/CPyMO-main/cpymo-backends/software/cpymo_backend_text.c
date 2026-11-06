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

/* CFF outlines can overhang their advance (or start left of the origin) and
 * stb's rasteriser may round a box edge outward, so the bitmap needs slack on
 * every side. The slack is part of the allocation; glyphs are written at
 * (pad_x, pad_y) and the draw subtracts the same offset back. */
#ifndef TEXT_EDGE_PAD
#define TEXT_EDGE_PAD 4
#endif

extern cpymo_backend_software_context 
    *cpymo_backend_software_cur_context;

static void cpymo_backend_text_render(
    void *out_or_null, 
    int *w, int *h, 
    cpymo_str text, 
    float scale, float baseline,
    int x_off, int y_off) 
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
				(unsigned char *)out_or_null + x_off + (int)xpos + x0
					+ (y_off + pad_top + (int)(baseline + (float)y0 + y_base)) * *w,
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
    float ink_center_from_baseline;
    uint16_t pad_x, pad_y;
    uint8_t px[0];
} cpymo_backend_text_impl;

error_t cpymo_backend_text_create(
    cpymo_backend_text *out, 
    float *out_width,
    cpymo_str utf8_string, 
    float single_character_size_in_logical_screen)
{
    float game_h = cpymo_backend_software_cur_context->logical_screen_h;

    float win_h = (float)cpymo_backend_software_cur_context->render_target->h;

    stbtt_fontinfo *font = cpymo_backend_software_cur_context->font;

    float height_norm = single_character_size_in_logical_screen / game_h;
    float height_screen = cpymo_backend_software_cur_context->text_layer ?
        single_character_size_in_logical_screen * cpymo_backend_software_cur_context->text_scale :
        height_norm * win_h;

    float scale = stbtt_ScaleForPixelHeight(font, height_screen);
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    float baseline = scale * ascent;

    int w, h;
    cpymo_backend_text_render(NULL, &w, &h, utf8_string, scale, baseline, 0, 0);
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX measure len=%zu w=%d h=%d\n", utf8_string.len, w, h);
#endif
    h += 4; // magic
    int pw = w + 2 * TEXT_EDGE_PAD, ph = h + 2 * TEXT_EDGE_PAD;

    size_t bytes = (size_t)pw * (size_t)ph;
    cpymo_backend_text_impl *o = (cpymo_backend_text_impl *)malloc(sizeof(cpymo_backend_text_impl) + bytes);
    if (o == NULL) return CPYMO_ERR_OUT_OF_MEM;
    memset(o, 0, sizeof(cpymo_backend_text_impl) + bytes);
    o->w = (size_t)pw;
    o->h = (size_t)ph;
    o->pad_x = TEXT_EDGE_PAD;
    o->pad_y = TEXT_EDGE_PAD;
    *out_width = cpymo_backend_text_width(utf8_string, single_character_size_in_logical_screen);
    /* *w is the stride here, so pass the padded width */
    w = pw; h = ph;
    cpymo_backend_text_render(o->px, &w, &h, utf8_string, scale, baseline,
                              TEXT_EDGE_PAD, TEXT_EDGE_PAD);
    *out = o;
#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX create out=%p len=%zu w=%d h=%d\n", (void *)o, utf8_string.len, w, h);
#endif

    o->baseline = baseline;
    size_t ink_min = o->h, ink_max = 0;
    for (size_t yy = 0; yy < o->h; ++yy)
        for (size_t xx = 0; xx < o->w; ++xx)
            if (o->px[yy * o->w + xx]) {
                if (yy < ink_min) ink_min = yy;
                if (yy > ink_max) ink_max = yy;
            }
    o->ink_center_from_baseline = ink_min <= ink_max ?
        ((float)ink_min + (float)ink_max) * 0.5f - baseline - o->pad_y : 0;

#ifdef GARLIC_UI_DEBUG
    fprintf(stderr, "TX end  out=%p w=%u h=%u base=%f\n", (void *)o,
        (unsigned)o->w, (unsigned)o->h, o->baseline);
#endif

    return CPYMO_ERR_SUCC;   
}

/* Only callers that explicitly request visual centring use ink metrics.
 * The popup/button rectangles and the rest of the text layout stay fixed. */
float garlic_text_centered_baseline(cpymo_backend_text text, float center_y)
{
    cpymo_backend_text_impl *t = (cpymo_backend_text_impl *)text;
    float scale = cpymo_backend_software_cur_context->text_layer ?
        cpymo_backend_software_cur_context->text_scale :
        (float)cpymo_backend_software_cur_context->render_target->h /
        cpymo_backend_software_cur_context->logical_screen_h;
    return center_y - t->ink_center_from_baseline / scale;
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

    /* With the output layer active the coordinates are already output pixels
     * (cpymo_backend_text_draw scaled them), so they map 1:1. */
    if (cpymo_backend_software_cur_context->text_layer) {
        window_size_w = (float)cpymo_backend_software_cur_context->text_layer_w;
        window_size_h = (float)cpymo_backend_software_cur_context->text_layer_h;
    } else {
        x /= cpymo_backend_software_cur_context->logical_screen_w;
        x *= window_size_w;
        y /= cpymo_backend_software_cur_context->logical_screen_h;
        y *= window_size_h;
    }

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
    for (size_t draw_rect_y = 0; draw_rect_y < t->h; ++draw_rect_y) {
        const uint8_t *src_row = t->px + (size_t)draw_rect_y * t->w;

        for (size_t draw_rect_x = 0; draw_rect_x < t->w * TEXT_CHARACTER_W_SCALE; ++draw_rect_x) {
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

            float a = fminf(1.0f, fmaxf(0.0f, alpha)) * pixel_alpha;
            if (a == 0.0f) continue;

            if (cpymo_backend_software_cur_context->text_layer) {
                uint32_t *px = cpymo_backend_software_cur_context->text_layer_px
                    + (size_t)draw_y * (size_t)window_size_w + (size_t)draw_x;
                uint32_t ia = (uint32_t)(a * 255.0f + 0.5f);
                uint32_t ir = (uint32_t)(cr * 255.0f + 0.5f);
                uint32_t ig = (uint32_t)(cg * 255.0f + 0.5f);
                uint32_t ib = (uint32_t)(cb * 255.0f + 0.5f);
                uint32_t sa = *px >> 24;
                if (ia >= 255 || sa == 0) {
                    *px = (ia << 24) | (ib << 16) | (ig << 8) | ir;
                } else {
                    /* source-over in the layer; rgb/stored are 0..255 */
                    uint32_t k = (255u - ia) * sa / 255u;
                    uint32_t oa = ia + k;
                    uint32_t na = oa ? oa : 1;
                    uint32_t r = (ir * ia + ((*px) & 0xff) * k) / na;
                    uint32_t g = (ig * ia + (((*px) >> 8) & 0xff) * k) / na;
                    uint32_t b = (ib * ia + (((*px) >> 16) & 0xff) * k) / na;
                    *px = (oa << 24) | (b << 16) | (g << 8) | r;
                }
                int dy = (int)draw_y;
                if (dy < cpymo_backend_software_cur_context->text_dirty_y0)
                    cpymo_backend_software_cur_context->text_dirty_y0 = dy;
                if (dy + 1 > cpymo_backend_software_cur_context->text_dirty_y1)
                    cpymo_backend_software_cur_context->text_dirty_y1 = dy + 1;
            } else {
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

    if (cpymo_backend_software_cur_context->text_layer) {
        float s = cpymo_backend_software_cur_context->text_scale;
        float ox = (float)cpymo_backend_software_cur_context->text_off_x;
        float oy = (float)cpymo_backend_software_cur_context->text_off_y;
        float px = ox + x * s - (float)t->pad_x;
        float py = oy + y_baseline * s - t->baseline - (float)t->pad_y;
        /* Shadow stays one *output* pixel, matching the unlayered look. */
        cpymo_backend_text_draw_internal(cpymo_color_inv(col), px + 1, py + 1, alpha, t);
        cpymo_backend_text_draw_internal(col, px, py, alpha, t);
        return;
    }

    float y = y_baseline - t->baseline - (float)t->pad_y;
    x -= (float)t->pad_x;
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
    float height_screen = cpymo_backend_software_cur_context->text_layer ?
        single_character_size_in_logical_screen * cpymo_backend_software_cur_context->text_scale :
        height_norm * win_h;
    float scale = stbtt_ScaleForPixelHeight(font, height_screen);
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    float baseline = scale * ascent;
    int w, h;
    cpymo_backend_text_render(NULL, &w, &h, s, scale, baseline, 0, 0);

    if (cpymo_backend_software_cur_context->text_layer)
        return TEXT_CHARACTER_W_SCALE * (float)w / cpymo_backend_software_cur_context->text_scale;
    return TEXT_CHARACTER_W_SCALE * (float)w / win_w * game_w;
}

#ifdef GARLIC_NO_FFMPEG
/* Logical space remaining for baselines, with actual glyph descenders and a
 * two-output-pixel shadow/safe margin reserved below the final baseline. */
float garlic_textbox_safe_height(cpymo_str text,float size,float y,float height)
{
    cpymo_backend_software_context *ctx=cpymo_backend_software_cur_context;
    float s=ctx->text_layer ? ctx->text_scale : (float)ctx->render_target->h/ctx->logical_screen_h;
    float scale=stbtt_ScaleForPixelHeight(ctx->font,size*s);
    int bottom=0;
    while(text.len){
        uint32_t c=cpymo_str_utf8_try_head_to_utf32(&text);
        int x0,y0,x1,y1;stbtt_GetCodepointBitmapBox(ctx->font,c,scale,scale,&x0,&y0,&x1,&y1);
        if(y1>bottom)bottom=y1;
    }
    float available=fminf(height,ctx->logical_screen_h-y);
    return fmaxf(0,available-(bottom+2)/s);
}
#endif
