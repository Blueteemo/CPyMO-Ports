#include "cpymo_prelude.h"
#include "cpymo_assetloader.h"
#include "cpymo_utils.h"
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <assert.h>
#include <limits.h>
#include "garlic_image_cache.h"
#include "garlic_stages.h"
#include "../stb/stb_image.h"

error_t cpymo_assetloader_init(cpymo_assetloader * out, const cpymo_gameconfig * config, const char * gamedir)
{
	error_t err;
	const size_t gamedir_strlen = strlen(gamedir);

	char *chbuf = (char *)malloc(gamedir_strlen + 24);
	out->gamedir = chbuf;

	out->game_config = config;

	if (chbuf == NULL) return CPYMO_ERR_OUT_OF_MEM;

	strcpy(chbuf, gamedir);

	out->use_pkg_bg = false;
	out->use_pkg_chara = false;
	out->use_pkg_se = false;
	out->use_pkg_voice = false;

	// init bg package
	strcpy(chbuf + gamedir_strlen, "/bg/bg.pak");
	err = cpymo_package_open(&out->pkg_bg, chbuf);
	if (err == CPYMO_ERR_SUCC)
		out->use_pkg_bg = true;
	else if (err != CPYMO_ERR_NOT_FOUND && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		cpymo_assetloader_free(out);
		return err;
	}

	// init chara package
	strcpy(chbuf + gamedir_strlen, "/chara/chara.pak");
	err = cpymo_package_open(&out->pkg_chara, chbuf);
	if (err == CPYMO_ERR_SUCC)
		out->use_pkg_chara = true;
	else if (err != CPYMO_ERR_NOT_FOUND && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		cpymo_assetloader_free(out);
		return err;
	}

	// init se package
	strcpy(chbuf + gamedir_strlen, "/se/se.pak");
	err = cpymo_package_open(&out->pkg_se, chbuf);
	if (err == CPYMO_ERR_SUCC)
		out->use_pkg_se = true;
	else if (err != CPYMO_ERR_NOT_FOUND && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		cpymo_assetloader_free(out);
		return err;
	}

	// init voice package
	strcpy(chbuf + gamedir_strlen, "/voice/voice.pak");
	err = cpymo_package_open(&out->pkg_voice, chbuf);
	if (err == CPYMO_ERR_SUCC) 
		out->use_pkg_voice = true;
	else if (err != CPYMO_ERR_NOT_FOUND && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		cpymo_assetloader_free(out);
		return err;
	}

	chbuf[gamedir_strlen] = '\0';
	
	out->gamedir = (char*)realloc((void *)out->gamedir, gamedir_strlen + 1);
	
	return CPYMO_ERR_SUCC;
}

void cpymo_assetloader_free(cpymo_assetloader * loader)
{
	if (loader) {
		if (loader->use_pkg_bg) cpymo_package_close(&loader->pkg_bg);
		if (loader->use_pkg_chara) cpymo_package_close(&loader->pkg_chara);
		if (loader->use_pkg_se) cpymo_package_close(&loader->pkg_se);
		if (loader->use_pkg_voice) cpymo_package_close(&loader->pkg_voice);
		if (loader->gamedir) free((void *)loader->gamedir);
	}
}

static bool cpymo_assetloader_path_readable(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

/* \`path\` is <dir>/<base>.<ext>; look for an entry of <dir> that equals
 * <base>.<ext> ignoring case and, when one exists, replace \`path\` by it.
 * Only reached when the exact and case-variant spellings all missed. */
static bool cpymo_assetloader_find_case_insensitive(char *path)
{
	char *slash = strrchr(path, '/');
	if (!slash) return false;
	const char *want = slash + 1;

	*slash = 0;
	DIR *dir = opendir(path);
	*slash = '/';
	if (!dir) return false;

	char best[256];
	best[0] = 0;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strcasecmp(ent->d_name, want) != 0) continue;
		if (best[0] && strcmp(ent->d_name, best) >= 0) continue;
		snprintf(best, sizeof(best), "%s", ent->d_name);
	}
	closedir(dir);
	if (!best[0]) return false;

	snprintf(slash + 1, sizeof(best) + 1, "%s", best);
	return cpymo_assetloader_path_readable(path);
}

error_t cpymo_assetloader_get_fs_path(
	char **out_str,
	cpymo_str asset_name,
	const char *asset_type,
	const char *asset_ext,
	const cpymo_assetloader *l)
{
	assert(*out_str == NULL);
	char *str = (char *)malloc(
		strlen(l->gamedir)
		+ strlen(asset_type)
		+ 2
		+ asset_name.len
		+ strlen(asset_ext)
		+ 4);

	if (str == NULL) return CPYMO_ERR_OUT_OF_MEM;

	strcpy(str, l->gamedir);
	strcat(str, "/");
	strcat(str, asset_type);
	strcat(str, "/");
	strncat(str, asset_name.begin, asset_name.len);
	strcat(str, ".");
	strcat(str, asset_ext);

	/* PyMO ports disagree on the extension and on its case (Android rips use
	 * .png where the S60 build stored .jpg, and cards written on Windows are
	 * full of .PNG). Try the neighbouring spellings before giving up, so a
	 * game directory works without being rewritten. */
	static const char *const alt_image[] = { "png", "jpg", "jpeg", "bmp", NULL };
	static const char *const alt_audio[] = { "mp3", "ogg", "wav", NULL };
	static const char *const alt_text[] = { "txt", "TXT", "Txt", NULL };

	const char *const *alts = NULL;
	size_t ext_off = strlen(str) - strlen(asset_ext);
	if (strcasecmp(asset_ext, "png") == 0 || strcasecmp(asset_ext, "jpg") == 0
		|| strcasecmp(asset_ext, "jpeg") == 0 || strcasecmp(asset_ext, "bmp") == 0)
		alts = alt_image;
	else if (strcasecmp(asset_ext, "mp3") == 0 || strcasecmp(asset_ext, "ogg") == 0
		|| strcasecmp(asset_ext, "wav") == 0)
		alts = alt_audio;
	else if (strcasecmp(asset_ext, "txt") == 0)
		alts = alt_text;

	if (cpymo_assetloader_path_readable(str)) {
		*out_str = str;
		return CPYMO_ERR_SUCC;
	}

	/* Same extension in another case first (cards written on Windows and
	 * macOS produce .PNG/.Png), then the other formats, each also in other
	 * cases. The order is fixed, so a directory holding both .png and .jpg
	 * always resolves to the .png the gameconfig asked for. */
	#define TRY_EXT(e) \
		do { \
			strcpy(str + ext_off, (e)); \
			if (cpymo_assetloader_path_readable(str)) { \
				*out_str = str; \
				found = true; \
				return CPYMO_ERR_SUCC; \
			} \
		} while (0)

	bool found = false;
	if (alts) {
		char upper[8], titled[8];
		size_t n = strlen(asset_ext);
		if (n + 1 < sizeof(upper)) {
			for (size_t i = 0; i <= n; ++i) {
				upper[i] = (char)toupper((unsigned char)asset_ext[i]);
				titled[i] = (char)(i == 0 ? toupper((unsigned char)asset_ext[i])
					: tolower((unsigned char)asset_ext[i]));
			}
			if (strcmp(upper, asset_ext) != 0) TRY_EXT(upper);
			if (strcmp(titled, asset_ext) != 0 &&
				strcmp(titled, upper) != 0) TRY_EXT(titled);
		}

		for (size_t i = 0; alts[i]; ++i) {
			if (strcasecmp(alts[i], asset_ext) == 0) continue;
			TRY_EXT(alts[i]);
			size_t m = strlen(alts[i]);
			if (m + 1 < sizeof(upper)) {
				for (size_t j = 0; j <= m; ++j) {
					upper[j] = (char)toupper((unsigned char)alts[i][j]);
					titled[j] = (char)(j == 0 ? toupper((unsigned char)alts[i][j])
						: tolower((unsigned char)alts[i][j]));
				}
				if (strcmp(upper, alts[i]) != 0) TRY_EXT(upper);
				if (strcmp(titled, alts[i]) != 0 &&
					strcmp(titled, upper) != 0) TRY_EXT(titled);
			}
		}
		/* Nothing matched by case alone: the base name may be spelled in
		 * another case too (cards written on Windows). Look the directory up
		 * once and take the case-insensitive match of the lowest candidate
		 * extension, so the choice stays deterministic. */
		for (int pass = 0; pass < 2 && !found; ++pass) {
			const char *const *list = pass == 0 ? &asset_ext : alts;
			size_t count = pass == 0 ? 1 : 0;
			while (pass == 1 && alts && alts[count]) count++;
			for (size_t i = 0; i < count && !found; ++i) {
				strcpy(str + ext_off, list[i]);
				found = cpymo_assetloader_find_case_insensitive(str);
			}
		}
		if (found) {
			*out_str = str;
			return CPYMO_ERR_SUCC;
		}
		strcpy(str + ext_off, asset_ext);
	}
	#undef TRY_EXT

	*out_str = str;
	return CPYMO_ERR_SUCC;
}

static error_t cpymo_assetloader_load_filesystem_file(
	char **out_buffer,
	size_t *buf_size,
	const char *asset_type,
	cpymo_str asset_name,
	const char *asset_ext_name,
	const cpymo_assetloader *assetloader) 
{
	char *path = NULL;
	error_t err = cpymo_assetloader_get_fs_path(
		&path, 
		asset_name, 
		asset_type, 
		asset_ext_name, 
		assetloader);
	CPYMO_THROW(err);

	err = cpymo_utils_loadfile(path, out_buffer, buf_size);

	free(path);

	return err;
}

#ifndef DISABLE_STB_IMAGE
static error_t cpymo_assetloader_load_filesystem_image_pixels(
	void **pixels,
	int *w,
	int *h,
	int c,
	const char *asset_type,
	cpymo_str asset_name,
	const char *asset_ext_name,
	const cpymo_assetloader *l)
{
	char *path = NULL;
	error_t err = cpymo_assetloader_get_fs_path(&path, asset_name, asset_type, asset_ext_name, l);
	CPYMO_THROW(err);

#ifdef GARLIC_NO_FFMPEG
    /* One encoded read, then decode from memory. This also separates
       TF/file access from decompression in diagnostics. */
    char *encoded=NULL;size_t encoded_len=0;
    uint64_t io_begin=garlic_stage_begin();
    err=cpymo_utils_loadfile(path,&encoded,&encoded_len);
    garlic_stage_end(GARLIC_STAGE_IMAGE_IO,io_begin);
    free(path);
    if(err!=CPYMO_ERR_SUCC)return err;
    if(encoded_len>INT_MAX){free(encoded);return CPYMO_ERR_BAD_FILE_FORMAT;}
    unsigned kind=encoded_len>2 && (unsigned char)encoded[0]==0xff && (unsigned char)encoded[1]==0xd8 ? GARLIC_STAGE_JPEG : (encoded_len>4 && (unsigned char)encoded[0]==0x89 && encoded[1]=='P' && encoded[2]=='N' && encoded[3]=='G' ? GARLIC_STAGE_PNG : GARLIC_STAGE_IMAGE_OTHER);
    uint64_t decode_begin=garlic_stage_begin();
    *pixels=stbi_load_from_memory((const unsigned char*)encoded,(int)encoded_len,w,h,NULL,c);
    garlic_stage_end(kind,decode_begin);free(encoded);
#else
	*pixels = stbi_load(path, w, h, NULL, c);
	free(path);
#endif

	if (*pixels == NULL) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
	
	return CPYMO_ERR_SUCC;
}


static error_t cpymo_assetloader_load_image_pixels(
	void **pixels,
	int *w,
	int *h,
	int c,
	const char *asset_type,
	cpymo_str asset_name,
	const char *asset_ext_name,
	bool using_pkg,
	const cpymo_package *pkg,
	const cpymo_assetloader *l)
{
    uint64_t begin=garlic_stage_begin();
    error_t result;
    if (using_pkg) result=cpymo_package_read_image(pixels,w,h,c,pkg,asset_name);
    else result=cpymo_assetloader_load_filesystem_image_pixels(
		pixels,
		w,
		h,
		c,
		asset_type,
		asset_name,
		asset_ext_name,
        l);
    garlic_stage_end(GARLIC_STAGE_DECODE,begin);
    return result;
}


error_t cpymo_assetloader_load_bg_pixels(void ** px, int * w, int * h, cpymo_str name, const cpymo_assetloader * loader)
{
	return cpymo_assetloader_load_image_pixels(
		px, w, h, 3, "bg", name,
		loader->game_config->bgformat, loader->use_pkg_bg, &loader->pkg_bg, loader);
}

#ifndef CPYMO_TOOL
error_t cpymo_assetloader_load_bg_image(cpymo_backend_image * img, int * w, int * h, cpymo_str name, const cpymo_assetloader * loader)
{
#ifdef GARLIC_NO_FFMPEG
    if (garlic_image_cache_get(img,w,h,loader->gamedir,"bg",name,false)) return CPYMO_ERR_SUCC;
#endif
	void *pixels = NULL;
	error_t err = cpymo_assetloader_load_bg_pixels(&pixels, w, h, name, loader);
	CPYMO_THROW(err);

	err = cpymo_backend_image_load(
		img,
		pixels,
		*w,
		*h,
		cpymo_backend_image_format_rgb);

	if (err != CPYMO_ERR_SUCC) {
		free(pixels);
		return err;
	}


#ifdef GARLIC_NO_FFMPEG
    garlic_image_cache_put(*img,*w,*h,loader->gamedir,"bg",name,false);
#endif
	return CPYMO_ERR_SUCC;
}


static error_t cpymo_assetloader_load_image_with_mask(
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
#ifdef GARLIC_NO_FFMPEG
    if (garlic_image_cache_get(img,w,h,loader->gamedir,asset_type,name,load_mask)) return CPYMO_ERR_SUCC;
#endif
	void *pixels = NULL;
	error_t err = cpymo_assetloader_load_image_pixels(
		&pixels, w, h, 4, 
		asset_type, name, asset_ext,
		use_pkg, pkg, loader);
	CPYMO_THROW(err);

	if (load_mask && cpymo_gameconfig_is_symbian(loader->game_config)) {
		char *filename = (char *)malloc(name.len + 6);
		if (filename == NULL) goto LOAD_WITHOUT_MASK;
		
		strncpy(filename, name.begin, name.len);
		strcpy(filename + name.len, "_mask");

		void *mask = NULL;
		int mw, mh;
		err = cpymo_assetloader_load_image_pixels(
			&mask, &mw, &mh, 1,
			asset_type, cpymo_str_pure(filename), mask_ext,
			use_pkg, pkg, loader);
		free(filename);
		if (err == CPYMO_ERR_SUCC) {
			error_t err = cpymo_backend_image_load_with_mask(img, pixels, mask, *w, *h, mw, mh);
			if (err != CPYMO_ERR_SUCC) {
				free(mask);
				goto LOAD_WITHOUT_MASK;
			}
		}
		else goto LOAD_WITHOUT_MASK;
	}
	else {
	LOAD_WITHOUT_MASK:
		err = cpymo_backend_image_load(img, pixels, *w, *h, cpymo_backend_image_format_rgba);
		if (err != CPYMO_ERR_SUCC) free(pixels);
	}

#ifdef GARLIC_NO_FFMPEG
    if (err == CPYMO_ERR_SUCC) garlic_image_cache_put(*img,*w,*h,loader->gamedir,asset_type,name,load_mask);
#endif
	return err;
}
#endif
#else
#ifndef CPYMO_TOOL
error_t cpymo_assetloader_load_image_with_mask(
	cpymo_backend_image *img, int *w, int *h, 
	cpymo_str name, 
	const char *asset_type,
	const char *asset_ext,
	const char *mask_ext,
	bool use_pkg,
	const cpymo_package *pkg,
	const cpymo_assetloader *loader,
	bool load_mask);
#endif
#endif

#ifndef CPYMO_TOOL
error_t cpymo_assetloader_load_chara_image(cpymo_backend_image *img, int *w, int *h, cpymo_str name, const cpymo_assetloader *loader)
{
	return cpymo_assetloader_load_image_with_mask(
		img, w, h,
		name, "chara", loader->game_config->charaformat, loader->game_config->charamaskformat,
		loader->use_pkg_chara, &loader->pkg_chara, loader, true);
}
#endif

error_t cpymo_assetloader_load_script(char ** out_buffer, size_t * buf_size, const char * script_name, const cpymo_assetloader * loader)
{
	error_t err = cpymo_assetloader_load_filesystem_file(
		out_buffer, buf_size, "script", cpymo_str_pure(script_name),
		"txt", loader);

	if (err != CPYMO_ERR_SUCC) return CPYMO_ERR_SCRIPT_FILE_NOT_FOUND;

	cpymo_utils_replace_cr(*out_buffer, *buf_size);

	return CPYMO_ERR_SUCC;
}

#ifndef CPYMO_TOOL
#ifndef DISABLE_STB_IMAGE
error_t cpymo_assetloader_load_system_masktrans(
	cpymo_backend_masktrans *out, cpymo_str name, 
	const cpymo_assetloader * loader)
{
	void *px = NULL;
	int w, h;
	error_t err = cpymo_assetloader_load_filesystem_image_pixels(&px, &w, &h, 1, "system", name, "png", loader);
	CPYMO_THROW(err);

	err = cpymo_backend_masktrans_create(out, px, w, h);
	if (err != CPYMO_ERR_SUCC) {
		free(px);
		return err;
	}

	return CPYMO_ERR_SUCC;
}
#endif
#endif

error_t cpymo_assetloader_get_bgm_path(char ** out_str, cpymo_str bgm_name, const cpymo_assetloader *loader)
{
	return cpymo_assetloader_get_fs_path(out_str, bgm_name, "bgm", loader->game_config->bgmformat, loader);
}

error_t cpymo_assetloader_get_vo_path(char **out_str, cpymo_str vo_name, const cpymo_assetloader *l)
{
	return cpymo_assetloader_get_fs_path(out_str, vo_name, "voice", l->game_config->voiceformat, l);
}

error_t cpymo_assetloader_get_video_path(char ** out_str, cpymo_str movie_name, const cpymo_assetloader * l)
{
	return cpymo_assetloader_get_fs_path(out_str, movie_name, "video", "mp4", l);
}

error_t cpymo_assetloader_get_se_path(char **out_str, cpymo_str vo_name, const cpymo_assetloader *l)
{
	return cpymo_assetloader_get_fs_path(out_str, vo_name, "se", l->game_config->seformat, l);
}


#ifndef CPYMO_TOOL
error_t cpymo_assetloader_load_system_image(
	cpymo_backend_image * out_image, 
	int *out_width, int *out_height,
	cpymo_str filename_span,
	const cpymo_assetloader * loader,
	bool load_mask)
{
	return cpymo_assetloader_load_image_with_mask(
		out_image, out_width, out_height, filename_span, "system", "png", "png", false, NULL, loader, load_mask);
}
#endif

#ifndef DISABLE_STB_IMAGE
error_t cpymo_assetloader_load_icon_pixels(
	void **px, int *w, int *h, const char *gamedir)
{
	cpymo_assetloader l;
	l.gamedir = gamedir;
	error_t e = cpymo_assetloader_load_filesystem_image_pixels(
		px, w, h, 4, ".", cpymo_str_pure("icon"), 
		"png", &l);

	CPYMO_THROW(e);

	if (*w != 57 || *h != 57) 
		printf("[Warning] Icon must be 57x57 (%s).\n", gamedir);
	
	return CPYMO_ERR_SUCC;
}
#ifndef CPYMO_TOOL
error_t cpymo_assetloader_load_icon(
	cpymo_backend_image *out, int *w, int *h, const char *gamedir)
{
	void *px = NULL;
	error_t e = cpymo_assetloader_load_icon_pixels(&px, w, h, gamedir);
	CPYMO_THROW(e);

	e = cpymo_backend_image_load(
		out, px, *w, *h, cpymo_backend_image_format_rgba);

	if (e != CPYMO_ERR_SUCC) free(px);
	return CPYMO_ERR_SUCC;
}
#endif
#endif

