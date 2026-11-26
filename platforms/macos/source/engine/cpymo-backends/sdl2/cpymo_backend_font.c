#include "../../cpymo/cpymo_prelude.h"
#ifndef DISABLE_STB_TRUETYPE

#include "../../cpymo/cpymo_error.h"
#include "../../cpymo/cpymo_utils.h"
#include "../../cpymo/cpymo_parser.h"
#include <stdio.h>
#ifdef CPYMO_MAC_LIBRARY_PREVIEW
#include <CoreText/CoreText.h>
#include <limits.h>
#include <glob.h>
#endif

#ifdef __ANDROID__
#include "cpymo_import_sdl2.h"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb/stb_truetype.h"

#ifdef __UWP__
#include <malloc.h>
#endif

#include "posix_win32.h"

#ifdef __SWITCH__
#include <switch.h>
#endif


stbtt_fontinfo font;
static unsigned char *ttf_buffer = NULL;


static error_t cpymo_backend_font_try_load_font(const char *path)
{
	size_t buf_size = 0;
	
	error_t err = cpymo_utils_loadfile(path, (char **)&ttf_buffer, &buf_size);
	CPYMO_THROW(err);

	const int offset = buf_size >= 12 ? stbtt_GetFontOffsetForIndex(ttf_buffer, 0) : -1;
	if (offset < 0 || stbtt_InitFont(&font, ttf_buffer, offset) == 0) {
		free(ttf_buffer);
		ttf_buffer = NULL;
		return CPYMO_ERR_BAD_FILE_FORMAT;
	}

	printf("[Info] Load font %s.\n", path);

	return CPYMO_ERR_SUCC;
}

#ifdef CPYMO_MAC_LIBRARY_PREVIEW
/* Query Core Text rather than hard-coding a macOS font asset path. Only use the
 * exact Simplified Chinese regular face; the TTC's face order can change. */
static int cpymo_font_name_is(stbtt_fontinfo *info, const char *name)
{
    const size_t len = strlen(name);
    for (int encoding = 0; encoding <= 10; ++encoding) {
        int nb = 0;
        const char *v = stbtt_GetFontNameString(info, &nb, 3, encoding, 0x409, 6);
        if (!v || nb != (int)(2 * len)) continue;
        size_t i;
        for (i = 0; i < len; ++i)
            if (v[2*i] != 0 || v[2*i+1] != name[i]) break;
        if (i == len) return 1;
    }
    return 0;
}

static error_t cpymo_mac_load_pingfang(const char *path);

static error_t cpymo_mac_find_pingfang(void)
{
    CTFontRef face = CTFontCreateWithName(CFSTR("PingFangSC-Regular"), 16, NULL);
    if (!face) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    CFStringRef name = CTFontCopyPostScriptName(face);
    CFTypeRef url = CTFontCopyAttribute(face, kCTFontURLAttribute);
    char path[PATH_MAX];
    const int found = name && CFEqual(name, CFSTR("PingFangSC-Regular")) &&
        url && CFGetTypeID(url) == CFURLGetTypeID() &&
        CFURLGetFileSystemRepresentation((CFURLRef)url, true, (UInt8 *)path, sizeof(path));
    if (url) CFRelease(url);
    if (name) CFRelease(name);
    CFRelease(face);
    if (found && cpymo_mac_load_pingfang(path) == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;

    /* Some newer macOS builds return the reserved UI font from Core Text.
       Select the installed retail face by name, without copying system fonts. */
    const char *locations[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/AssetsV2/com_apple_MobileAsset_Font*/*.asset/AssetData/PingFang.ttc"
    };
    for (size_t i = 0; i < sizeof(locations)/sizeof(locations[0]); ++i) {
        glob_t matches = {0};
        if (glob(locations[i], 0, NULL, &matches) == 0) {
            for (size_t j = 0; j < matches.gl_pathc; ++j) {
                if (cpymo_mac_load_pingfang(matches.gl_pathv[j]) == CPYMO_ERR_SUCC) {
                    globfree(&matches);
                    return CPYMO_ERR_SUCC;
                }
            }
        }
        globfree(&matches);
    }
    return CPYMO_ERR_CAN_NOT_OPEN_FILE;
}

static error_t cpymo_mac_load_pingfang(const char *path)
{
    const char *wanted = "PingFangSC-Regular";
    size_t size = 0;
    unsigned char *buffer = NULL;
    error_t err = cpymo_utils_loadfile(path, (char **)&buffer, &size);
    if (err != CPYMO_ERR_SUCC) return err;
    const int n = size >= 12 ? stbtt_GetNumberOfFonts(buffer) : 0;
    for (int i = 0; i < n; ++i) {
        int offset = stbtt_GetFontOffsetForIndex(buffer, i);
        stbtt_fontinfo test;
        if (offset >= 0 && stbtt_InitFont(&test, buffer, offset) && cpymo_font_name_is(&test, wanted)) {
            ttf_buffer = buffer;
            stbtt_InitFont(&font, ttf_buffer, offset);
            printf("[Info] Font: PingFang SC Regular.\n");
            return CPYMO_ERR_SUCC;
        }
    }
    free(buffer);
    return CPYMO_ERR_CAN_NOT_OPEN_FILE;
}
#endif

void cpymo_backend_font_free()
{
	if (ttf_buffer) free(ttf_buffer);
	ttf_buffer = NULL;
}

error_t cpymo_backend_font_init(const char *gamedir)
{
	char *path = (char *)alloca((gamedir ? strlen(gamedir) : 0) + 24);
	error_t err = CPYMO_ERR_SUCC;

#ifdef CPYMO_MAC_LIBRARY_PREVIEW
	if (cpymo_mac_find_pingfang() == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;
#endif
	if (gamedir) {
		if (path == NULL) return CPYMO_ERR_OUT_OF_MEM;
		sprintf(path, "%s/system/default.ttf", gamedir);
		err = cpymo_backend_font_try_load_font(path);
		if (err == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;

		sprintf(path, "%s/system/default.otf", gamedir);
		err = cpymo_backend_font_try_load_font(path);
		if (err == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;
	}

#ifdef GAME_SELECTOR_DIR
	err = cpymo_backend_font_try_load_font(GAME_SELECTOR_DIR "/default.ttf");
	if (err == CPYMO_ERR_SUCC) return err;

	err = cpymo_backend_font_try_load_font(GAME_SELECTOR_DIR "/default.otf");
	if (err == CPYMO_ERR_SUCC) return err;
#endif
	
#ifdef GAME_SELECTOR_DIR_2
	err = cpymo_backend_font_try_load_font(GAME_SELECTOR_DIR_2 "/default.ttf");
	if (err == CPYMO_ERR_SUCC) return err;

	err = cpymo_backend_font_try_load_font(GAME_SELECTOR_DIR_2 "/default.otf");
	if (err == CPYMO_ERR_SUCC) return err;
#endif

#ifdef _WIN32
	const char *windir = getenv("windir");
	path = (char *)alloca(strlen(windir) + 32);
	if (path == NULL) return CPYMO_ERR_OUT_OF_MEM;

	const char *fonts[] = {
		"msyh.ttc",
		"msyhbd.ttc",
		"msyhl.ttc",
		"msyi.ttf",
		"meiryo.ttc",
		"meiryob.ttc",
		"mingliub.ttc",
		"msgothic.ttc",
		"msjh.ttc",
		"msjhbd.ttc",
		"msjhl.ttc",
		"msmincho.ttc",
		"simhei.ttf",
		"simkai.ttf",
		"simsun.ttc"
		"simsunb.ttf",
		"simfang.ttf",
		"SIMLI.ttf"
	};

	for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) {
		sprintf(path, "%s/fonts/%s", windir, fonts[i]);
		err = cpymo_backend_font_try_load_font(path);
		if (err == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;
	}
#endif

#ifdef __LINUX__
	const char *fonts[] = {
		// Ubuntu
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
		"/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc",

		// Deepin
		"/usr/share/fonts/truetype/unifont/unifont.ttf",
	};

	for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) 
		if (cpymo_backend_font_try_load_font(fonts[i]) == CPYMO_ERR_SUCC)
			return CPYMO_ERR_SUCC;
#endif

#ifdef __APPLE__
#ifdef __IOS__
    extern const char *get_ios_directory();
    const char *iosdir = get_ios_directory();
    path = (char *) alloca(strlen(iosdir) + 24);
    if (path == NULL) return CPYMO_ERR_OUT_OF_MEM;

    sprintf(path, "%s/default.ttf", iosdir);
    err = cpymo_backend_font_try_load_font(path);
    if (err == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;

    sprintf(path, "%s/default.otf", iosdir);
    err = cpymo_backend_font_try_load_font(path);
    if (err == CPYMO_ERR_SUCC) return CPYMO_ERR_SUCC;
#else
	const char *fonts[] = {
		"/System/Library/Fonts/STHeiti Medium.ttc",
		"/System/Library/Fonts/STHeiti Light.ttc"
	};

	for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i)
		if (cpymo_backend_font_try_load_font(fonts[i]) == CPYMO_ERR_SUCC)
			return CPYMO_ERR_SUCC;
#endif
#endif

#ifdef __SWITCH__
	Result r = plInitialize(PlServiceType_User);
	if (R_FAILED(r)) return CPYMO_ERR_NOT_FOUND;

	PlFontData font_infos[PlSharedFontType_Total];
	s32 total_fonts;
	r = plGetSharedFont(SetLanguage_ZHCN, font_infos, PlSharedFontType_Total, &total_fonts);
	if (R_FAILED(r)) return CPYMO_ERR_NOT_FOUND;
	for (s32 i = 1; i < total_fonts; ++i) {		// Skip 0 font, font 0 is shit.
		const void *address = font_infos[i].address;
		if (stbtt_InitFont(&font, address, stbtt_GetFontOffsetForIndex(address, 0)) == 0) {
			continue;
		}

		return CPYMO_ERR_SUCC;
	}
	plExit();
#endif

#ifdef __ANDROID__
	SDL_AndroidShowToast("Can not find /sdcard/pymogames/default.ttf", 1, -1, 0, 0);
#endif

	return CPYMO_ERR_CAN_NOT_OPEN_FILE;
}

#ifndef TEXT_LINE_Y_OFFSET
#define TEXT_LINE_Y_OFFSET 0
#endif


void cpymo_backend_font_render(void *out_or_null, int *w, int *h, cpymo_str text, float scale, float baseline) 
{
	float xpos = 0;

	int width = 0, height = 0;
	float y_base = 0;
	while (text.len > 0) {
		uint32_t codepoint = cpymo_str_utf8_try_head_to_utf32(&text);
		int x0, y0, x1, y1;

		int advance_width, lsb;
		float x_shift = xpos - (float)floor(xpos);
		stbtt_GetCodepointHMetrics(&font, (int)codepoint, &advance_width, &lsb);

		if (codepoint == '\n') {
			xpos = 0;

			y_base += baseline + TEXT_LINE_Y_OFFSET;

			continue;
		}

		stbtt_GetCodepointBitmapBoxSubpixel(&font, (int)codepoint, scale, scale, x_shift, 0, &x0, &y0, &x1, &y1);
		if (out_or_null) {
            const int gw = x1 - x0, gh = y1 - y0;
            const int ox = (int)xpos + x0, oy = (int)(baseline + y0 + y_base);
            if (gw > 0 && gh > 0) {
                unsigned char *dst = (unsigned char *)out_or_null;
                if (ox >= 0 && oy >= 0 && ox <= *w - gw && oy <= *h - gh) {
                    stbtt_MakeCodepointBitmapSubpixel(&font, dst + (size_t)oy * *w + ox,
                        gw, gh, *w, scale, scale, x_shift, 0, (int)codepoint);
                } else if (ox < *w && oy < *h && ox > -gw && oy > -gh &&
                           (size_t)gw <= SIZE_MAX / (size_t)gh) {
                    unsigned char *glyph = malloc((size_t)gw * gh);
                    if (glyph) {
                        stbtt_MakeCodepointBitmapSubpixel(&font, glyph, gw, gh, gw,
                            scale, scale, x_shift, 0, (int)codepoint);
                        const int sx = ox < 0 ? -ox : 0, sy = oy < 0 ? -oy : 0;
                        const int ex = gw < *w - ox ? gw : *w - ox;
                        const int ey = gh < *h - oy ? gh : *h - oy;
                        for (int yy = sy; yy < ey; ++yy) {
                            unsigned char *row = dst + (size_t)(yy + oy) * *w;
                            const unsigned char *src = glyph + (size_t)yy * gw;
                            for (int xx = sx; xx < ex; ++xx)
                                if (src[xx] > row[ox + xx]) row[ox + xx] = src[xx];
                        }
                        free(glyph);
                    }
                }
            }
        }

		xpos += (advance_width * scale);

		cpymo_str text2 = text;
		uint32_t next_char = cpymo_str_utf8_try_head_to_utf32(&text2);
		if (next_char) {
			xpos += scale * stbtt_GetCodepointKernAdvance(&font, codepoint, next_char);
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
