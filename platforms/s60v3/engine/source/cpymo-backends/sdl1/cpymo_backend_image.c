#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_engine.h"
#include "../include/cpymo_backend_image.h"
#include "../include/cpymo_backend_masktrans.h"
#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>
#ifdef S60V3
#include "cpymo_s60_image.h"
#include "cpymo_s60_rgb565.h"
#endif

#ifdef ENABLE_SDL_IMAGE
#include <SDL/SDL_image.h>
#endif

const extern cpymo_engine engine;
extern SDL_Surface *framebuffer;

#ifdef S60V3
/* Basic nearest-neighbour draw scaling for the reference-sized UI sheets used
 * by existing 320x240 PyMO games. Original image bytes/masks stay untouched.
 * At most six transformed surfaces, 512 KiB total.
 * Larger one-off surfaces are not retained; this is not a new asset cache. */
#define CPYMO_S60_SCALE_CACHE_ENTRIES 6
#define CPYMO_S60_SCALE_CACHE_BYTES (512U * 1024U)
#define CPYMO_S60_SCALE_TEMP_BYTES (2U * 1024U * 1024U)
typedef struct {
    SDL_Surface *original, *scaled;
    int sx, sy, sw, sh, dw, dh;
    unsigned long used;
    size_t bytes;
} cpymo_s60_scale_entry;
static cpymo_s60_scale_entry s60_scale_cache[CPYMO_S60_SCALE_CACHE_ENTRIES];
static size_t s60_scale_cache_bytes;
static unsigned long s60_scale_tick;

static void s60_scale_drop(size_t index)
{
    cpymo_s60_scale_entry *entry = &s60_scale_cache[index];
    if (entry->scaled) {
        SDL_FreeSurface(entry->scaled);
        s60_scale_cache_bytes -= entry->bytes;
    }
    memset(entry, 0, sizeof(*entry));
}

static void s60_scale_forget(SDL_Surface *original)
{
    size_t i;
    for (i = 0; i < CPYMO_S60_SCALE_CACHE_ENTRIES; ++i)
        if (!original || s60_scale_cache[i].original == original)
            s60_scale_drop(i);
}

static int s60_scale_oldest(void)
{
    int oldest = -1;
    size_t i;
    for (i = 0; i < CPYMO_S60_SCALE_CACHE_ENTRIES; ++i) {
        if (!s60_scale_cache[i].scaled) return (int)i;
        if (oldest < 0 || s60_scale_cache[i].used < s60_scale_cache[oldest].used)
            oldest = (int)i;
    }
    return oldest;
}

static Uint32 s60_scale_raw_pixel(const Uint8 *p, int bpp)
{
    switch (bpp) {
    case 1: return p[0];
    case 2: return *(const Uint16 *)p;
    case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        return ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
#else
        return p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
#endif
    default: return *(const Uint32 *)p;
    }
}

static SDL_Surface *s60_scale_surface(SDL_Surface *src, int sx, int sy,
    int sw, int sh, int dw, int dh, int *temporary)
{
    size_t i, bytes;
    int inside, bpp, x, y, index;
    int *xmap;
    SDL_Surface *out;
    *temporary = 0;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return NULL;
    if (dw > 4096 || dh > 4096) return NULL;
    inside = sx >= 0 && sy >= 0 && sx <= src->w - sw && sy <= src->h - sh;
    bpp = src->format->BytesPerPixel;
    bytes = (size_t)dw * (size_t)dh * (inside ? (size_t)bpp : 4U);
    if (bytes > CPYMO_S60_SCALE_TEMP_BYTES) return NULL;
    for (i = 0; i < CPYMO_S60_SCALE_CACHE_ENTRIES; ++i) {
        cpymo_s60_scale_entry *entry = &s60_scale_cache[i];
        if (entry->original != src) continue;
        if (entry->sx == sx && entry->sy == sy && entry->sw == sw &&
            entry->sh == sh && entry->dw == dw && entry->dh == dh) {
            entry->used = ++s60_scale_tick;
            return entry->scaled;
        }
    }
    out = SDL_CreateRGBSurface(SDL_SWSURFACE, dw, dh,
        inside ? src->format->BitsPerPixel : 32,
        inside ? src->format->Rmask : 0x000000ff,
        inside ? src->format->Gmask : 0x0000ff00,
        inside ? src->format->Bmask : 0x00ff0000,
        inside ? src->format->Amask : 0xff000000);
    if (!out) return NULL;
    if (inside && src->format->palette && out->format->palette)
        SDL_SetColors(out, src->format->palette->colors, 0, src->format->palette->ncolors);
    xmap = (int *)malloc((size_t)dw * sizeof(int));
    if (!xmap) { SDL_FreeSurface(out); return NULL; }
    for (x = 0; x < dw; ++x) xmap[x] = sx + (int)((long)x * sw / dw);
    if (SDL_LockSurface(src) < 0) { free(xmap); SDL_FreeSurface(out); return NULL; }
    if (SDL_LockSurface(out) < 0) {
        SDL_UnlockSurface(src); free(xmap); SDL_FreeSurface(out); return NULL;
    }
    memset(out->pixels, 0, (size_t)out->pitch * (size_t)out->h);
    for (y = 0; y < dh; ++y) {
        int source_y = sy + (int)((long)y * sh / dh);
        Uint8 *dest = (Uint8 *)out->pixels + y * out->pitch;
        if (source_y < 0 || source_y >= src->h) continue;
        for (x = 0; x < dw; ++x) {
            const Uint8 *sample;
            if (xmap[x] < 0 || xmap[x] >= src->w) continue;
            sample = (const Uint8 *)src->pixels + source_y * src->pitch + xmap[x] * bpp;
            if (inside) {
                int c;
                for (c = 0; c < bpp; ++c) dest[x * bpp + c] = sample[c];
            } else {
                Uint8 r, g, b, a;
                SDL_GetRGBA(s60_scale_raw_pixel(sample, bpp), src->format, &r, &g, &b, &a);
                ((Uint32 *)dest)[x] = SDL_MapRGBA(out->format, r, g, b, a);
            }
        }
    }
    SDL_UnlockSurface(out); SDL_UnlockSurface(src); free(xmap);
    if (src->flags & SDL_SRCCOLORKEY)
        SDL_SetColorKey(out, SDL_SRCCOLORKEY, src->format->colorkey);
    bytes = (size_t)out->pitch * (size_t)out->h;
    if (bytes > CPYMO_S60_SCALE_CACHE_BYTES) { *temporary = 1; return out; }
    while (s60_scale_cache_bytes + bytes > CPYMO_S60_SCALE_CACHE_BYTES) {
        index = s60_scale_oldest();
        /* A free slot can coexist with a full byte budget; select used LRU. */
        if (!s60_scale_cache[index].scaled) {
            index = -1;
            for (i = 0; i < CPYMO_S60_SCALE_CACHE_ENTRIES; ++i)
                if (s60_scale_cache[i].scaled && (index < 0 ||
                    s60_scale_cache[i].used < s60_scale_cache[index].used)) index = (int)i;
        }
        if (index < 0) break;
        s60_scale_drop((size_t)index);
    }
    index = s60_scale_oldest(); s60_scale_drop((size_t)index);
    s60_scale_cache[index].original = src; s60_scale_cache[index].scaled = out;
    s60_scale_cache[index].sx = sx; s60_scale_cache[index].sy = sy;
    s60_scale_cache[index].sw = sw; s60_scale_cache[index].sh = sh;
    s60_scale_cache[index].dw = dw; s60_scale_cache[index].dh = dh;
    s60_scale_cache[index].used = ++s60_scale_tick;
    s60_scale_cache[index].bytes = bytes; s60_scale_cache_bytes += bytes;
    return out;
}
/* SDL 1.2 per-pixel alpha blits ignore the surface opacity. Keep both
 * opacities in the same RGB565 path and use the source's actual channel order. */
static bool s60_blit_rgba(SDL_Surface *src, SDL_Rect rect, SDL_Rect dest, float alpha)
{
    const SDL_PixelFormat *sf = src->format, *df = framebuffer->format;
    if (sf->BytesPerPixel != 4 || !sf->Amask ||
            sf->Aloss || sf->Rloss || sf->Gloss || sf->Bloss ||
            !cpymo_s60_is_rgb565(df)) return false;

    SDL_Rect clip;
    SDL_GetClipRect(framebuffer, &clip);
    int left = 0, top = 0, right = rect.w, bottom = rect.h;
    if (left < -rect.x) left = -rect.x;
    if (top < -rect.y) top = -rect.y;
    if (left < clip.x - dest.x) left = clip.x - dest.x;
    if (top < clip.y - dest.y) top = clip.y - dest.y;
    if (right > src->w - rect.x) right = src->w - rect.x;
    if (bottom > src->h - rect.y) bottom = src->h - rect.y;
    if (right > clip.x + clip.w - dest.x) right = clip.x + clip.w - dest.x;
    if (bottom > clip.y + clip.h - dest.y) bottom = clip.y + clip.h - dest.y;
    if (left >= right || top >= bottom) return true;

    if (SDL_LockSurface(src) < 0) return false;
    if (SDL_LockSurface(framebuffer) < 0) {
        SDL_UnlockSurface(src);
        return false;
    }

    const unsigned opacity = alpha >= 1.0f ? 64 : (unsigned)(alpha * 64.0f + 0.5f);
    const unsigned rs = sf->Rshift + 3, gs = sf->Gshift + 2, bs = sf->Bshift + 3;
    for (int y = top; y < bottom; ++y) {
        const Uint32 *in = (const Uint32 *)((const Uint8 *)src->pixels +
            (rect.y + y) * src->pitch) + rect.x + left;
        Uint16 *out = (Uint16 *)((Uint8 *)framebuffer->pixels +
            (dest.y + y) * framebuffer->pitch) + dest.x + left;
        for (int x = left; x < right; ++x, ++in, ++out) {
            const Uint32 pixel = *in;
            const unsigned coverage = (((pixel >> sf->Ashift) & 255) * opacity + 128) >> 8;
            if (!coverage) continue;
            const Uint16 color = (Uint16)((((pixel >> rs) & 31) << df->Rshift) |
                (((pixel >> gs) & 63) << df->Gshift) |
                (((pixel >> bs) & 31) << df->Bshift));
            *out = coverage == 64 ? color : cpymo_s60_blend565(*out, color, coverage);
        }
    }

    SDL_UnlockSurface(framebuffer);
    SDL_UnlockSurface(src);
    return true;
}

void cpymo_s60_move_menu_title(cpymo_backend_image image, int header_height)
{
    SDL_Surface *skin = (SDL_Surface *)image;
    if (skin->format->BytesPerPixel != 4 || header_height > skin->h ||
            skin->w < 120 || SDL_LockSurface(skin) != 0) return;

    const int left = skin->w / 48;
    const int end = skin->w / 5;
    const int right = skin->w - end;
    bool flat = true, has_title = false;
    /* Recognize the classic title treatment by its flat scanlines,
       including alpha. Leave illustrated and already-right-aligned skins alone. */
    for (int y = 4; y < header_height && flat; ++y) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)skin->pixels + y * skin->pitch);
        const Uint32 base = row[end];
        for (int x = end + 1; x < right + end - left; ++x)
            if (row[x] != base) { flat = false; break; }
        for (int x = left; x < end; ++x)
            if (row[x] != base) has_title = true;
    }
    if (flat && has_title) {
        for (int y = 4; y < header_height; ++y) {
            Uint32 *row = (Uint32 *)((Uint8 *)skin->pixels + y * skin->pitch);
            memcpy(row + right, row + left, (end - left) * sizeof(*row));
            for (int x = left; x < end; ++x) row[x] = row[end];
        }
    }
    SDL_UnlockSurface(skin);
}

bool cpymo_s60_menu_stretch_band(cpymo_backend_image image, int *first, int *end)
{
    SDL_Surface *skin = (SDL_Surface *)image;
    if (skin->format->BytesPerPixel != 4 || SDL_LockSurface(skin) != 0) return false;

    const Uint8 *pixels = (const Uint8 *)skin->pixels;
    const size_t row_bytes = (size_t)skin->w * 4;
    const int middle = skin->h / 2;
    const Uint8 *row = pixels + (size_t)middle * skin->pitch;
    int start = middle, stop = middle + 1;
    while (start > 0 && !memcmp(row, pixels + (size_t)(start - 1) * skin->pitch, row_bytes))
        --start;
    while (stop < skin->h && !memcmp(row, pixels + (size_t)stop * skin->pitch, row_bytes))
        ++stop;
    SDL_UnlockSurface(skin);

    /* As on NDS, require a substantial band bounded by top and bottom art.
       Compare all four channels across the row, including the side edges. */
    if (start == 0 || stop == skin->h || stop - start < 4 || (stop - start) * 3 < skin->h)
        return false;
    *first = start;
    *end = stop;
    return true;
}
#endif /* S60V3 */

error_t cpymo_backend_image_init(void)
{
	#ifdef ENABLE_SDL_IMAGE
	int flags =
		IMG_INIT_JPG |
		IMG_INIT_PNG |
		IMG_INIT_TIF |
		IMG_INIT_WEBP;

	int result = IMG_Init(flags);

	if (0 == result) {
		printf("[Error] IMG_Init: %s\n", IMG_GetError());
		return CPYMO_ERR_UNKNOWN;
	}

	if (flags != result) 
		printf("[Warning] IMG_Init(%d): %s\n", result, IMG_GetError());

	#endif

	return CPYMO_ERR_SUCC;
}

void cpymo_backend_image_quit(void)
{
#ifdef S60V3
    s60_scale_forget(NULL);
#endif
	#ifdef ENABLE_SDL_IMAGE
	IMG_Quit();
	#endif
}

cpymo_color getpixel(SDL_Surface *surface, int x, int y)
{
    int bpp = surface->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

	Uint32 px;

    switch(bpp) {
    case 1:
        px = *p;
		break;

    case 2:
        px = *(Uint16 *)p;
		break;

    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            px = (p[0] << 16 | p[1] << 8 | p[2]);
        else
            px = (p[0] | p[1] << 8 | p[2] << 16);
		break;

    case 4:
        px = *(Uint32 *)p;
		break;

    default:
        px = 0;
		break;

    }

	cpymo_color col;
	SDL_GetRGB(px, surface->format, &col.r, &col.g, &col.b);
	return col;
}

void putpixel(SDL_Surface *surface, int x, int y, cpymo_color col, Uint8 alpha)
{
    int bpp = surface->format->BytesPerPixel;
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

	Uint32 pixel = SDL_MapRGBA(surface->format, col.r, col.g, col.b, alpha);

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	#define RMASK 0xff000000
	#define GMASK 0x00ff0000
	#define BMASK 0x0000ff00
	#define AMASK 0x000000ff
#else
	#define RMASK 0x000000ff
	#define GMASK 0x0000ff00
	#define BMASK 0x00ff0000
	#define AMASK 0xff000000
#endif

error_t cpymo_backend_image_load(
	cpymo_backend_image *out_image, 
	void *pixels_moveintoimage, 
	int width, 
	int height, 
	enum cpymo_backend_image_format format)
{
	Uint32 rmask = RMASK, gmask = GMASK, bmask = BMASK, amask = AMASK;
#ifdef __WII__
	if (format == cpymo_backend_image_format_rgb) {
		rmask >>= 8;
		gmask >>= 8;
		bmask >>= 8;
		amask >>= 8;
	}
#endif

	int channels = format == cpymo_backend_image_format_rgb ? 3 : 4;
#ifdef S60V3
    if (channels == 4) {
        /* Preserve decoded PNG/mask bytes independently of SDL's screen conversion. */
        SDL_Surface *rgba = SDL_CreateRGBSurface(SDL_SWSURFACE,
            width, height, 32, rmask, gmask, bmask, amask);
        if (!rgba) return CPYMO_ERR_OUT_OF_MEM;
        const size_t row_size = (size_t)width * 4;
        for (int y = 0; y < height; ++y)
            memcpy((Uint8 *)rgba->pixels + y * rgba->pitch,
                (const Uint8 *)pixels_moveintoimage + y * row_size, row_size);
        free(pixels_moveintoimage);
        *out_image = (cpymo_backend_image)rgba;
        return CPYMO_ERR_SUCC;
    }
    amask = 0;
#endif
	SDL_Surface *sur = SDL_CreateRGBSurfaceFrom(
		pixels_moveintoimage,
		width,
		height,
		channels * 8,
		width * channels,
		rmask,
		gmask,
		bmask,
		amask);

	if (sur == NULL) {
		return CPYMO_ERR_UNKNOWN;
	}

	SDL_Surface *sur2;
	if (format == cpymo_backend_image_format_rgb)
		sur2 = SDL_DisplayFormat(sur);
	else sur2 = SDL_DisplayFormatAlpha(sur);

	SDL_FreeSurface(sur);
	if (sur2 == NULL) return CPYMO_ERR_UNKNOWN;
	free(pixels_moveintoimage);

	*out_image = (cpymo_backend_image)sur2;

    return CPYMO_ERR_SUCC;
}

error_t cpymo_backend_image_load_with_mask(
	cpymo_backend_image *out_image, 
	void *px_rgbx32_moveinto, 
	void *mask_a8_moveinto, 
	int w, 
	int h, 
	int mask_w, 
	int mask_h)
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
#ifdef S60V3
    s60_scale_forget((SDL_Surface *)image);
#endif
	SDL_FreeSurface((SDL_Surface *)image);
}

static void trans_pos(float *x, float *y) 
{
	float screen_w = (float)framebuffer->w;
	float screen_h = (float)framebuffer->h;

	float game_w = (float)engine.gameconfig.imagesize_w;
	float game_h = (float)engine.gameconfig.imagesize_h;

	*x = *x + (screen_w - game_w) / 2;
	*y = *y + (screen_h - game_h) / 2;
}

void cpymo_backend_image_draw(
	float dstx, float dsty, float dstw, float dsth,
	cpymo_backend_image src_,
	int srcx, int srcy, int srcw, int srch, float alpha,
	enum cpymo_backend_image_draw_type draw_type)
{
#ifdef S60V3
    if (alpha <= 0.0f) return;
#else
	if (draw_type == cpymo_backend_image_draw_type_chara) {
		if (alpha < 0.75f) return;
	}
#endif

	SDL_Surface *src = (SDL_Surface *)src_;
	trans_pos(&dstx, &dsty);

	SDL_Rect srcrect, dstrect;
	srcrect.x = (Sint16)srcx;
	srcrect.y = (Sint16)srcy;
	srcrect.w = (Uint16)srcw;
	srcrect.h = (Uint16)srch;
	dstrect.x = (Sint16)dstx;
	dstrect.y = (Sint16)dsty;
	dstrect.w = (Uint16)dstw;
	dstrect.h = (Uint16)dsth;

#ifdef S60V3
    if (dstw <= 0 || dsth <= 0 || srcw <= 0 || srch <= 0) return;
    if ((int)dstw != srcw || (int)dsth != srch) {
        int temporary = 0;
        SDL_Surface *scaled = s60_scale_surface(src, srcx, srcy, srcw, srch,
            (int)dstw, (int)dsth, &temporary);
        if (!scaled) return;
        SDL_Rect whole = { 0, 0, (Uint16)scaled->w, (Uint16)scaled->h };
        if (!s60_blit_rgba(scaled, whole, dstrect, alpha)) {
            SDL_SetAlpha(scaled, SDL_SRCALPHA, (Uint8)(alpha * 255));
            SDL_BlitSurface(scaled, NULL, framebuffer, &dstrect);
        }
        if (temporary) SDL_FreeSurface(scaled);
        return;
    }
#else
	int xoff = ((int)dstrect.w - (int)srcrect.w) / 2;
	int yoff = ((int)dstrect.h - (int)srcrect.h) / 2;

	if (xoff < 0) xoff = 0;
	if (yoff < 0) yoff = 0;

	dstrect.x += xoff;
	dstrect.y += yoff;

#endif
#ifdef S60V3
    if (s60_blit_rgba(src, srcrect, dstrect, alpha)) return;
#endif
    SDL_SetAlpha(src, SDL_SRCALPHA, (Uint8)(alpha * 255));
    SDL_BlitSurface(src, &srcrect, framebuffer, &dstrect);
}

void cpymo_backend_image_fill_rects(
	const float *xywh, size_t count,
	cpymo_color color, float alpha,
	enum cpymo_backend_image_draw_type draw_type)
{
#ifdef FAST_FILL_RECT
	Uint32 col = SDL_MapRGBA(
		framebuffer->format, 
		(Uint8)(color.r * alpha), (Uint8)(color.g * alpha), (Uint8)(color.b * alpha), 255);

	for (size_t i = 0; i < count; ++i) {
		SDL_Rect rect;
		float x = xywh[i * 4 + 0];
		float y = xywh[i * 4 + 1];
		trans_pos(&x, &y);
		rect.x = (Sint16)x;
		rect.y = (Sint16)y;
		rect.w = (Uint16)xywh[i * 4 + 2];
		rect.h = (Uint16)xywh[i * 4 + 3];

		SDL_FillRect(framebuffer, &rect, col);
	}
#else
	if (SDL_LockSurface(framebuffer) == -1) return;

	SDL_Rect clip;
	SDL_GetClipRect(framebuffer, &clip);

#ifdef S60V3
    if (cpymo_s60_is_rgb565(framebuffer->format)) {
        const unsigned weight = alpha <= 0.0f ? 0 : alpha >= 1.0f ? 64 : (unsigned)(alpha * 64.0f + 0.5f);
        const Uint16 src = (Uint16)SDL_MapRGB(framebuffer->format, color.r, color.g, color.b);
        for (size_t i = 0; weight && i < count; ++i) {
            float x = xywh[4*i], y = xywh[4*i+1];
            trans_pos(&x, &y);
            int x0 = (int)x, y0 = (int)y;
            int x1 = x0 + (int)xywh[4*i+2], y1 = y0 + (int)xywh[4*i+3];
            if (x0 < clip.x) x0 = clip.x;
            if (y0 < clip.y) y0 = clip.y;
            if (x1 > clip.x + clip.w) x1 = clip.x + clip.w;
            if (y1 > clip.y + clip.h) y1 = clip.y + clip.h;
            if (x0 >= x1 || y0 >= y1) continue;
            if (weight == 64) {
                SDL_Rect opaque = {x0, y0, x1 - x0, y1 - y0};
                SDL_UnlockSurface(framebuffer);
                SDL_FillRect(framebuffer, &opaque, src);
                if (SDL_LockSurface(framebuffer) == -1) return;
                continue;
            }
            for (int row = y0; row < y1; ++row) {
                Uint16 *dst = (Uint16 *)((Uint8 *)framebuffer->pixels + row * framebuffer->pitch) + x0;
                for (int col = x0; col < x1; ++col, ++dst)
                    *dst = cpymo_s60_blend565(*dst, src, weight);
            }
        }
        SDL_UnlockSurface(framebuffer);
        return;
    }
#endif

	float r = color.r / 255.0f * alpha;
	float g = color.g / 255.0f * alpha;
	float b = color.b / 255.0f * alpha;

	for (size_t i = 0; i < count; ++i) {
		float x_ = xywh[i * 4 + 0];
		float y_ = xywh[i * 4 + 1];
		int w = (int)xywh[i * 4 + 2];
		int h = (int)xywh[i * 4 + 3];
		trans_pos(&x_, &y_);

		for (int y = (int)y_; y < h + (int)y_; ++y) {
			for (int x = (int)x_; x < w + (int)x_; x++) {
				if (x < 0 || x >= framebuffer->w || y < 0 || y >= framebuffer->h) continue;
				if (x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h) continue;

				cpymo_color dst = getpixel(framebuffer, x, y);
				float dst_r = dst.r / 255.0f * (1 - alpha) + r;
				float dst_g = dst.g / 255.0f * (1 - alpha) + g;
				float dst_b = dst.b / 255.0f * (1 - alpha) + b;

				dst_r = cpymo_utils_clampf(dst_r, 0, 1);
				dst_g = cpymo_utils_clampf(dst_g, 0, 1);
				dst_b = cpymo_utils_clampf(dst_b, 0, 1);

				dst.r = (Uint8)(dst_r * 255);
				dst.g = (Uint8)(dst_g * 255);
				dst.b = (Uint8)(dst_b * 255);
				putpixel(framebuffer, x, y, dst, 255);
			}
		}
	}

	SDL_UnlockSurface(framebuffer);
#endif
}

bool cpymo_backend_image_album_ui_writable()
{
	return true;
}

error_t cpymo_backend_masktrans_create(cpymo_backend_masktrans *out, void *mask_singlechannel_moveinto, int w, int h)
{
	if (w != engine.gameconfig.imagesize_w || h != engine.gameconfig.imagesize_h)
    	return CPYMO_ERR_UNSUPPORTED;

	*out = (cpymo_backend_masktrans)mask_singlechannel_moveinto;
	return CPYMO_ERR_SUCC;
}

void cpymo_backend_masktrans_free(cpymo_backend_masktrans m) { free(m); }

#ifdef S60V3
bool cpymo_s60_mask_crossfade(cpymo_backend_masktrans mask,
    cpymo_backend_image incoming, float dst_x, float dst_y, float progress)
{
    SDL_Surface *src = (SDL_Surface *)incoming;
    if (!cpymo_s60_is_rgb565(framebuffer->format) || !cpymo_s60_is_rgb565(src->format))
        return false;
    trans_pos(&dst_x, &dst_y);
    SDL_Rect clip;
    SDL_GetClipRect(framebuffer, &clip);
    const int dx = (int)dst_x, dy = (int)dst_y;
    const int game_w = engine.gameconfig.imagesize_w;
    const int game_h = engine.gameconfig.imagesize_h;
    const int clip_w = clip.w < game_w ? clip.w : game_w;
    const int clip_h = clip.h < game_h ? clip.h : game_h;
    const int x0 = dx > clip.x ? dx : clip.x, y0 = dy > clip.y ? dy : clip.y;
    const int x1 = dx + src->w < clip.x + clip_w ? dx + src->w : clip.x + clip_w;
    const int y1 = dy + src->h < clip.y + clip_h ? dy + src->h : clip.y + clip_h;
    if (x0 >= x1 || y0 >= y1) return true;
    if (SDL_LockSurface(src) != 0) return false;
    if (SDL_LockSurface(framebuffer) != 0) {
        SDL_UnlockSurface(src);
        return false;
    }

    /* Legacy PyMO interpolates 0 -> mask -> 255, then uses the
       result as the new scene's opacity over the original scene. */
    Uint8 weights[256];
    const unsigned step = (unsigned)(progress * 128.0f + 0.5f);
    for (int i = 0; i < 256; ++i) {
        unsigned weight = step <= 64 ? i * step : i * (128 - step) + 255 * (step - 64);
        weights[i] = (Uint8)((weight + 127) / 255);
    }
    for (int y = y0; y < y1; ++y) {
        const Uint16 *in = (const Uint16 *)((const Uint8 *)src->pixels +
            (y - dy) * src->pitch) + x0 - dx;
        Uint16 *out = (Uint16 *)((Uint8 *)framebuffer->pixels + y * framebuffer->pitch) + x0;
        const Uint8 *m = (const Uint8 *)mask + (y - clip.y) * game_w + x0 - clip.x;
        for (int x = x0; x < x1; ++x, ++in, ++out, ++m)
            *out = cpymo_s60_blend565(*out, *in, weights[*m]);
    }
    SDL_UnlockSurface(framebuffer);
    SDL_UnlockSurface(src);
    return true;
}
#endif

void cpymo_backend_masktrans_draw(cpymo_backend_masktrans m, float t, bool is_fade_in)
{
	if (SDL_LockSurface(framebuffer) == -1) return;

	const float radius = 0.25f;
	t = t * (1.0f + 2 * radius) - radius;

	float t_top = t + radius;
	float t_bottom = t - radius;

	SDL_Rect rect;
	SDL_GetClipRect(framebuffer, &rect);

#ifdef S60V3
    if (cpymo_s60_is_rgb565(framebuffer->format)) {
        Uint8 weights[256];
        for (int i = 0; i < 256; ++i) {
            float w = ((float)i / 255.0f - t_bottom) / (2 * radius);
            if (w < 0.0f) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            if (is_fade_in) w = 1.0f - w;
            weights[i] = (Uint8)(w * 64.0f + 0.5f);
        }

        int width = engine.gameconfig.imagesize_w;
        int height = engine.gameconfig.imagesize_h;
        int draw_w = width < rect.w ? width : rect.w;
        int draw_h = height < rect.h ? height : rect.h;
        for (int y = 0; y < draw_h; ++y) {
            Uint16 *dst = (Uint16 *)((Uint8 *)framebuffer->pixels +
                (rect.y + y) * framebuffer->pitch) + rect.x;
            const Uint8 *mask = (const Uint8 *)m + y * width;
            for (int x = 0; x < draw_w; ++x) {
                unsigned weight = weights[mask[x]];
                if (weight != 64) dst[x] = cpymo_s60_blend565(0, dst[x], weight);
            }
        }
        SDL_UnlockSurface(framebuffer);
        return;
    }
#endif

	for (int y = 0; y < engine.gameconfig.imagesize_h; ++y) {
		for (int x = 0; x < engine.gameconfig.imagesize_w; ++x) {
			int px_x = x + rect.x;
			int px_y = y + rect.y;

			if (px_x < rect.x || px_x >= rect.x + rect.w || px_y < rect.y || px_y >= rect.y + rect.h) continue;

			float mask =
				(float)(((unsigned char*)m)[y * engine.gameconfig.imagesize_w + x]) / 255;

			if (mask > t_top) mask = 1.0f;
			else if (mask < t_bottom) mask = 0.0f;
			else mask = (mask - t_bottom) / (2 * radius);

			if (is_fade_in) mask = 1 - mask;

			cpymo_color col = getpixel(framebuffer, px_x, px_y);

			col.r = (uint8_t)(col.r * mask);
			col.g = (uint8_t)(col.g * mask);
			col.b = (uint8_t)(col.b * mask);
			putpixel(framebuffer, px_x, px_y, col, 255);
		}
	}

	SDL_UnlockSurface(framebuffer);
}

#ifdef ENABLE_SDL_IMAGE
#include <cpymo_package.h>
#include <cpymo_assetloader.h>

error_t cpymo_assetloader_load_icon_pixels(
	void **px, int *w, int *h, const char *gamedir)
{
	return CPYMO_ERR_UNSUPPORTED;
}

error_t cpymo_assetloader_load_icon(
	cpymo_backend_image *out, int *w, int *h, const char *gamedir)
{
	char *filename = (char *)malloc(strlen(gamedir) + 10);
	if (filename == NULL) return CPYMO_ERR_OUT_OF_MEM;
	sprintf(filename, "%s/icon.png", gamedir);
	SDL_Surface *sur = IMG_Load(filename);
	free(filename);
	if (sur == NULL) 
		return CPYMO_ERR_CAN_NOT_OPEN_FILE;

	*w = sur->w;
	*h = sur->h;
	
	SDL_Surface *sur_optimized = SDL_DisplayFormat(sur);
	if (sur_optimized == NULL) {
		*out = (cpymo_backend_image)sur;
		return CPYMO_ERR_SUCC;
	}

	SDL_FreeSurface(sur);
	*out = (cpymo_backend_image)sur_optimized;

	return CPYMO_ERR_SUCC;
}

static error_t cpymo_assetloader_load_image_surface(
	SDL_Surface **out,
	cpymo_str name,
	const char *asset_type,
	const char *asset_ext,
	bool use_pkg,
	const cpymo_package *pkg,
	const cpymo_assetloader *loader)
{
	SDL_Surface *sur;
	if (use_pkg) {
		cpymo_package_stream_reader r;
		error_t err = cpymo_package_stream_reader_find_create(&r, pkg, name);
		CPYMO_THROW(err);

		SDL_RWops *rwops = SDL_RWFromFP(r.stream, 0);
		if (rwops == NULL) {
			cpymo_package_stream_reader_close(&r);
			return CPYMO_ERR_OUT_OF_MEM;
		}

		sur = IMG_Load_RW(rwops, 0);
		SDL_RWclose(rwops);
		cpymo_package_stream_reader_close(&r);
		
		/*char *buf;
		size_t len;
		error_t err = cpymo_package_read_file(&buf, &len, pkg, name);
		CPYMO_THROW(err);

		SDL_RWops *rwops = SDL_RWFromConstMem(buf, (int)len);
		if (rwops == NULL) {
			free(buf);
			return CPYMO_ERR_OUT_OF_MEM;
		}

		sur = IMG_Load_RW(rwops, 0);
		SDL_RWclose(rwops);
		free(buf);*/
	}
	else {
		char *path;
		error_t err = cpymo_assetloader_get_fs_path(
			&path,
			name,
			asset_type,
			asset_ext,
			loader);
		CPYMO_THROW(err);

		sur = IMG_Load(path);
		free(path);
	}

	if (sur == NULL) return CPYMO_ERR_OUT_OF_MEM;
	*out = sur;
	return CPYMO_ERR_SUCC;
}

static error_t cpymo_assetloader_load_image_with_mask_ex(
	cpymo_backend_image *img, int *w, int *h, 
	cpymo_str name, 
	const char *asset_type,
	const char *asset_ext,
	const char *mask_ext,
	bool use_pkg,
	const cpymo_package *pkg,
	const cpymo_assetloader *loader,
	bool load_mask,
	SDL_Surface *(*display_format_converter)(SDL_Surface *))
{
	SDL_Surface *sur;
	error_t err = cpymo_assetloader_load_image_surface(
		&sur, name, asset_type, asset_ext, use_pkg, pkg, loader);
	CPYMO_THROW(err);

	if (sur == NULL) return CPYMO_ERR_OUT_OF_MEM;

	if (load_mask) {
		char *mask_name = (char *)malloc(name.len + 6);
		if (mask_name) {
			cpymo_str_copy(mask_name, name.len + 6, name);
			strcat(mask_name, "_mask");

			SDL_Surface *mask;
			error_t errmask = cpymo_assetloader_load_image_surface(
				&mask, cpymo_str_pure(mask_name), asset_type, mask_ext, 
				use_pkg, pkg, loader);
			free(mask_name);

			if (errmask == CPYMO_ERR_SUCC) {
				SDL_Surface *masked_image = SDL_CreateRGBSurface(
					0, 
					sur->w, 
					sur->h, 
					32, 
					RMASK, 
					GMASK, 
					BMASK, 
					AMASK);
				
				if (masked_image) {
					for (int y = 0; y < sur->h; ++y) {
						for (int x = 0; x < sur->w; ++x) {
							cpymo_color maskp = getpixel(sur, x, y);
							cpymo_color maskc = getpixel(
								mask, 
								(int)((float)x / (float)sur->w * (mask->w - 1)),
								(int)((float)y / (float)sur->h * (mask->h - 1)));
							putpixel(masked_image, x, y, maskp, maskc.r);
						}
					}

					SDL_FreeSurface(sur);
					sur = masked_image;
				}

				SDL_FreeSurface(mask);
			}
		}
	}

	*w = sur->w;
	*h = sur->h;

	SDL_Surface *sur_optimized = display_format_converter(sur);
	if (sur_optimized) {
		SDL_FreeSurface(sur);
		sur = sur_optimized;
	}

	*img = (cpymo_backend_image)sur;

	return CPYMO_ERR_SUCC;
}

error_t cpymo_assetloader_load_image_with_mask(
	cpymo_backend_image *img, int *w, int *h, 
	cpymo_str name, 
	const char *asset_type,
	const char *asset_ext,
	const char *mask_ext,
	bool use_pkg,
	const cpymo_package *pkg,
	const cpymo_assetloader *loader,
	bool load_mask)
{
	return cpymo_assetloader_load_image_with_mask_ex(
		img, w, h, name, asset_type, asset_ext, mask_ext, use_pkg, pkg, loader,
		load_mask, &SDL_DisplayFormatAlpha);
}

error_t cpymo_assetloader_load_bg_pixels(
	void ** px, int * w, int * h, 
	cpymo_str name, const cpymo_assetloader * loader)
{ return CPYMO_ERR_UNSUPPORTED; }

error_t cpymo_assetloader_load_bg_image(
	cpymo_backend_image * img, int * w, int * h, 
	cpymo_str name, const cpymo_assetloader * loader)
{
	return cpymo_assetloader_load_image_with_mask_ex(
		img, w, h, name, "bg", loader->game_config->bgformat,
		NULL, loader->use_pkg_bg, &loader->pkg_bg, loader,
		false, &SDL_DisplayFormat);
}

error_t cpymo_assetloader_load_system_masktrans(
	cpymo_backend_masktrans *out, cpymo_str name, 
	const cpymo_assetloader *loader)
{ 
	char *filename;
	error_t err = cpymo_assetloader_get_fs_path(
		&filename,
		name,
		"system",
		"png",
		loader);		
	CPYMO_THROW(err);

	SDL_Surface *mask = IMG_Load(filename);
	free(filename);
	if (mask == NULL) return CPYMO_ERR_CAN_NOT_OPEN_FILE;

	Uint8 *mask_surface = (Uint8 *)malloc(
		engine.gameconfig.imagesize_w * engine.gameconfig.imagesize_h);
	if (mask_surface == NULL) {
		SDL_FreeSurface(mask);
		return CPYMO_ERR_OUT_OF_MEM;
	}

	for (Uint16 y = 0; y < engine.gameconfig.imagesize_h; ++y) {
		for (Uint16 x = 0; x < engine.gameconfig.imagesize_w; ++x) {
			float xf = (float)x / (float)engine.gameconfig.imagesize_w;
			float yf = (float)y / (float)engine.gameconfig.imagesize_h;

			int xm = (int)(xf * (mask->w - 1));
			int ym = (int)(yf * (mask->h - 1));
			cpymo_color col = getpixel(mask, xm, ym);
			mask_surface[y * engine.gameconfig.imagesize_w + x] = col.r;
		}
	}

	SDL_FreeSurface(mask);
	*out = (cpymo_backend_masktrans)mask_surface;
	
	return CPYMO_ERR_SUCC;
}

#endif


