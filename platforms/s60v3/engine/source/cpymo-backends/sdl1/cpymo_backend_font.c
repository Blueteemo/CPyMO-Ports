#ifndef ENABLE_SDL_TTF
#ifndef S60V3
#include "../sdl2/cpymo_backend_font.c"
#else
#include "../../cpymo/cpymo_prelude.h"
#ifndef DISABLE_STB_TRUETYPE

#include "../../cpymo/cpymo_error.h"
#include "../../cpymo/cpymo_utils.h"
#include "../../cpymo/cpymo_parser.h"
#include <stdio.h>
#include "cpymo_s60_native_font.h"
#include "cpymo_s60_diagnostics.h"

#ifdef __ANDROID__
#include "cpymo_import_sdl2.h"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb/stb_truetype.h"

#ifdef __UWP__
#include <malloc.h>
#endif

#include "../sdl2/posix_win32.h"

#ifdef __SWITCH__
#include <switch.h>
#endif


static stbtt_fontinfo smooth_font, story_font, fusion_font;
static unsigned char *ttf_buffer, *story_buffer, *fusion_buffer;
static bool fusion_load_attempted;
static int active_profile, next_profile;
static int reported_dialogue_size;
#ifdef CPYMO_S60_NATIVE_FONT
static bool native_font_active;
static bool reported_native_glyph;
static bool reported_font_fallback;
static unsigned reported_yi_sizes;
#endif


#ifndef CPYMO_S60_DATA_DIR
#define CPYMO_S60_DATA_DIR "C:/Data/CPyMO"
#endif

#define FONT_PROFILE_PATH CPYMO_S60_DATA_DIR "/font-profile.txt"

/* The fallback belongs to the app and is independent of the game directory. */
static error_t load_font(const char *path, unsigned char **buffer, stbtt_fontinfo *face)
{
    unsigned char *candidate = NULL;
    stbtt_fontinfo candidate_info;
    size_t size = 0;
    error_t err = cpymo_utils_loadfile(path, (char **)&candidate, &size);
    CPYMO_THROW(err);
    const int offset = size >= 12 ? stbtt_GetFontOffsetForIndex(candidate, 0) : -1;
    if (offset < 0 || (size_t)offset > size - 12 ||
            stbtt_InitFont(&candidate_info, candidate, offset) == 0 ||
            stbtt_FindGlyphIndex(&candidate_info, 0x4e2d) == 0 ||
            stbtt_IsGlyphEmpty(&candidate_info, stbtt_FindGlyphIndex(&candidate_info, 0x4e2d))) {
        free(candidate);
        return CPYMO_ERR_BAD_FILE_FORMAT;
    }
    free(*buffer);
    *buffer = candidate;
    *face = candidate_info;
    CPYMO_S60_TRACE("[Info] Shared S60 font %s (%lu bytes).\n", path, (unsigned long)size);
    return CPYMO_ERR_SUCC;
}

void cpymo_backend_font_free(void)
{
#ifdef CPYMO_S60_NATIVE_FONT
    if (native_font_active) cpymo_s60_native_font_close();
    native_font_active = false;
    reported_native_glyph = false;
    reported_font_fallback = false;
    reported_yi_sizes = 0;
#endif
    free(ttf_buffer);
    free(story_buffer);
    free(fusion_buffer);
    ttf_buffer = story_buffer = fusion_buffer = NULL;
    fusion_load_attempted = false;
    memset(&smooth_font, 0, sizeof(smooth_font));
    memset(&story_font, 0, sizeof(story_font));
    memset(&fusion_font, 0, sizeof(fusion_font));
    reported_dialogue_size = 0;
}

int cpymo_s60_next_font_profile(void)
{
    return next_profile;
}

int cpymo_s60_active_font_profile(void)
{
    return active_profile;
}

int cpymo_s60_save_font_profile(int profile)
{
    FILE *file = fopen(FONT_PROFILE_PATH, "wb");
    if (!file) return 0;
    const int saved = fputc(profile == CPYMO_FONT_SMOOTH ? '1' : '0', file);
    const int closed = fclose(file);
    if (saved == EOF || closed != 0) return 0;
    next_profile = profile;
    CPYMO_S60_TRACE("[Font] next launch profile %d.\n", next_profile);
    return 1;
}

error_t cpymo_backend_font_init(const char *gamedir)
{
    (void)gamedir;
    if (ttf_buffer) return CPYMO_ERR_SUCC;

    FILE *file = fopen(FONT_PROFILE_PATH, "rb");
    active_profile = CPYMO_FONT_COMBINED;
    if (file) {
        if (fgetc(file) == '1') active_profile = CPYMO_FONT_SMOOTH;
        fclose(file);
    }
    next_profile = active_profile;
    printf("[Font] active profile %d (0 Fusion 12px / WenQuanYi 16px + system, 1 smooth).\n", active_profile);
    error_t err = load_font(CPYMO_S60_DATA_DIR "/fonts/default.otf", &ttf_buffer, &smooth_font);
    if (err != CPYMO_ERR_SUCC)
        err = load_font(CPYMO_S60_DATA_DIR "/fonts/default.ttf", &ttf_buffer, &smooth_font);
    CPYMO_THROW(err);
    if (active_profile == CPYMO_FONT_COMBINED) {
        if (load_font(CPYMO_S60_DATA_DIR "/fonts/story-pixel.ttf", &story_buffer, &story_font) != CPYMO_ERR_SUCC)
            printf("[Font] story fallback to bundled smooth font.\n");
#ifdef CPYMO_S60_NATIVE_FONT
        native_font_active = cpymo_s60_native_font_open() != 0;
#endif
    }
    return CPYMO_ERR_SUCC;
}

static const stbtt_fontinfo *dialogue_bitmap_font(float height, bool dialogue)
{
    if (!dialogue || active_profile != CPYMO_FONT_COMBINED) return NULL;
    if (height >= 15.99f && height <= 16.01f)
        return story_buffer ? &story_font : NULL;
    if (height < 11.99f || height > 12.01f) return NULL;

    /* Leave the experimental asset unloaded until the first 12 px dialogue. */
    if (!fusion_load_attempted) {
        fusion_load_attempted = true;
        if (load_font(CPYMO_S60_DATA_DIR "/fonts/story-fusion-12.ttf",
                &fusion_buffer, &fusion_font) != CPYMO_ERR_SUCC)
            printf("[Font] Fusion 12px unavailable; using the usual fallback.\n");
    }
    return fusion_buffer ? &fusion_font : NULL;
}

static float raster_scale(const stbtt_fontinfo *face, float height)
{
    /* WenQuanYi uses 18 design pixels per em, including line spacing. */
    if (face == &story_font) height = 18.0f;
    else if (face == &fusion_font) height = 12.0f;
    return stbtt_ScaleForMappingEmToPixels(face, height);
}

float cpymo_s60_font_baseline(float height, bool dialogue)
{
    const stbtt_fontinfo *face = dialogue_bitmap_font(height, dialogue);
    if (!face) face = &smooth_font;
    int ascent;
    stbtt_GetFontVMetrics(face, &ascent, NULL, NULL);
    return raster_scale(face, height) * ascent;
}

#ifndef TEXT_LINE_Y_OFFSET
#define TEXT_LINE_Y_OFFSET 0
#endif


/* Rasterize at the measured origin, clipping glyph overhang at bitmap edges. */
static void draw_glyph(const stbtt_fontinfo *face, unsigned char *out, int width, int height, int x, int y,
    int glyph_w, int glyph_h, float scale, float shift, uint32_t codepoint)
{
    if (glyph_w <= 0 || glyph_h <= 0 || x >= width || y >= height ||
            x + glyph_w <= 0 || y + glyph_h <= 0) return;
    if (x >= 0 && y >= 0 && x + glyph_w <= width && y + glyph_h <= height) {
        stbtt_MakeCodepointBitmapSubpixel(face, out + y * width + x,
            glyph_w, glyph_h, width, scale, scale, shift, 0, (int)codepoint);
        return;
    }
    unsigned char *glyph = malloc((size_t)glyph_w * glyph_h);
    if (!glyph) return;
    stbtt_MakeCodepointBitmapSubpixel(face, glyph, glyph_w, glyph_h, glyph_w,
        scale, scale, shift, 0, (int)codepoint);
    int left = x < 0 ? 0 : x;
    int right = x + glyph_w > width ? width : x + glyph_w;
    for (int row = y < 0 ? 0 : y; row < height && row < y + glyph_h; ++row) {
        unsigned char *target = out + row * width;
        const unsigned char *src = glyph + (row - y) * glyph_w;
        for (int col = left; col < right; ++col)
            if (src[col - x] > target[col]) target[col] = src[col - x];
    }
    free(glyph);
}

#ifdef CPYMO_S60_NATIVE_FONT
static void draw_native_glyph(unsigned char *out, int width, int height,
    int x, int y, const cpymo_s60_native_glyph *glyph)
{
    for (int row = 0; row < glyph->height; ++row) {
        if (y + row < 0 || y + row >= height) continue;
        for (int col = 0; col < glyph->width; ++col) {
            if (x + col < 0 || x + col >= width) continue;
            unsigned char *pixel = out + (y + row) * width + x + col;
            const unsigned char sample = glyph->pixels[row * glyph->width + col];
            if (sample > *pixel) *pixel = sample;
        }
    }
}
#endif

void cpymo_s60_font_render(void *out_or_null, int *w, int *h, cpymo_str text,
    float pixel_height, float baseline, bool dialogue) 
{
    const stbtt_fontinfo *bitmap = dialogue_bitmap_font(pixel_height, dialogue);
	float xpos = 0;

	int width = 0, height = 0;
	float y_base = 0;
    const int size = (int)(pixel_height + 0.5f);
    const bool trace = cpymo_s60_diagnostics_enabled() != 0;
#ifdef CPYMO_S60_NATIVE_FONT
    const int native_size = native_font_active ? size : 0;
#endif
	while (text.len > 0) {
		uint32_t codepoint = cpymo_str_utf8_try_head_to_utf32(&text);
		int x0, y0, x1, y1;

		int advance_width, lsb;

		if (codepoint == '\n') {
			xpos = 0;

			y_base += baseline + TEXT_LINE_Y_OFFSET;

			continue;
		}

        const bool story = bitmap && stbtt_FindGlyphIndex(bitmap, (int)codepoint);
        bool native = false;
#ifdef CPYMO_S60_NATIVE_FONT
        cpymo_s60_native_glyph glyph;
        native = native_size && !story &&
            cpymo_s60_native_font_glyph(codepoint, native_size, &glyph);
#endif
        if (trace && dialogue && codepoint >= 0x4e00 && codepoint <= 0x9fff &&
            reported_dialogue_size != size) {
            printf("[Font] dialogue U+%04lX at %dpx: %s.\n", (unsigned long)codepoint,
                size, story ? (bitmap == &fusion_font ? "Fusion" : "WenQuanYi") :
                    native ? "Symbian" : "Bundled Noto");
            reported_dialogue_size = size;
        }
#ifdef CPYMO_S60_NATIVE_FONT
        if (trace && !dialogue && codepoint == 0x5fc6 && size >= 16 && size <= 18) {
            const unsigned mask = 1u << (size - 16);
            if (!(reported_yi_sizes & mask)) {
                const char *source = native ? "Symbian" : "Bundled Noto";
                printf("[Font] UI U+5FC6 at %dpx: %s.\n", size, source);
                reported_yi_sizes |= mask;
            }
        }
        if (!story && native_size) {
            if (native) {
                if (trace && !reported_native_glyph && codepoint > 0x20) {
                    printf("[Font] native glyph U+%04lX at %dpx.\n",
                        (unsigned long)codepoint, native_size);
                    reported_native_glyph = true;
                }
                const int x = (int)xpos + glyph.left;
                const int y = (int)(baseline + y_base) + glyph.top;
                if (out_or_null)
                    draw_native_glyph(out_or_null, *w, *h, x, y, &glyph);
                xpos += glyph.advance;
                if ((int)ceil(xpos) > width) width = (int)ceil(xpos);
                if (x + glyph.width > width) width = x + glyph.width;
                if (y + glyph.height > height) height = y + glyph.height;
                continue;
            }
            if (trace && !reported_font_fallback) {
                printf("[Font] bundled fallback for U+%04lX.\n", (unsigned long)codepoint);
                reported_font_fallback = true;
            }
        }
#endif
        const stbtt_fontinfo *face = story ? bitmap : &smooth_font;
        const float scale = raster_scale(face, pixel_height);
        const float x_shift = story ? 0.0f : xpos - floorf(xpos);
        stbtt_GetCodepointHMetrics(face, (int)codepoint, &advance_width, &lsb);

		stbtt_GetCodepointBitmapBoxSubpixel(face, (int)codepoint, scale, scale, x_shift, 0, &x0, &y0, &x1, &y1);
        if (out_or_null) {
            draw_glyph(face, out_or_null, *w, *h, (int)xpos + x0,
                (int)(baseline + y0 + y_base), x1 - x0, y1 - y0,
                scale, x_shift, codepoint);
        }

		xpos += (advance_width * scale);

		cpymo_str text2 = text;
		uint32_t next_char = cpymo_str_utf8_try_head_to_utf32(&text2);
        if (next_char && !story
#ifdef CPYMO_S60_NATIVE_FONT
            && !native_size
#endif
        ) {
			xpos += scale * stbtt_GetCodepointKernAdvance(face, codepoint, next_char);
		}

		int new_width = (int)ceil(xpos);
		if (new_width > width) width = new_width;

		int new_height = (int)((y1 - y0) + baseline + y_base);
		if (new_height > height) height = new_height;
	}

	*w = width;
	*h = height;
}

#endif

#endif /* S60V3 */
#endif /* !ENABLE_SDL_TTF */
