#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <alloca.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <unistd.h>
#ifndef GARLIC_NO_FFMPEG
#include <threads.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include "libretro.h"

#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_game_selector.h"
#include "../../endianness.h/endianness.h"
#include "../software/cpymo_backend_software.h"
#include "../include/cpymo_backend_audio.h"
#include "../include/cpymo_backend_movie.h"

#ifndef GARLIC_NO_FFMPEG
#include <libswscale/swscale.h>
#endif
#include "../garlicos/garlic_adapter.h"
#include "../garlicos/garlic_paths.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../../stb/stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../stb/stb_image_resize.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../stb/stb_image_write.h"
#define STB_DS_IMPLEMENTATION
#include "../../stb/stb_ds.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb/stb_truetype.h"


static void                            fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t              log_cb     = fallback_log;
static retro_video_refresh_t           video_cb;
static retro_input_poll_t              input_poll_cb;
static retro_input_state_t             input_state_cb;
static retro_audio_sample_batch_t      audio_batch_cb;
static retro_environment_t             environ_cb;
static cpymo_engine                    engine;
static cpymo_backend_software_image    soft_image;
static uint16_t                      *garlic_rgb565_frame = NULL;
static bool                           garlic_first_frame = false;
static bool                           garlic_pixel_format_supported = false;
static bool                           garlic_engine_ready = false;
static bool                           garlic_game_loaded = false;
/* Which of the two screens the 180-frame counters describe. The menu and a
 * game are the same "loaded" state to the frontend; this only tells the
 * report, the footer and the tests which one is on screen. */
typedef enum {
    GARLIC_SCREEN_NONE = 0,
    GARLIC_SCREEN_GAME,
    GARLIC_SCREEN_MENU
} garlic_screen_t;
static garlic_screen_t              garlic_screen = GARLIC_SCREEN_NONE;
/* The game the selector chose, so a reload (RetroArch replaying the same
 * content) and the log can name it. */
static char                        garlic_selected_game[PATH_MAX];
/* Menu geometry, at the OUTPUT size, from §4 of the v0.23 task. Drawing the
 * menu at 640x480 makes the RGB565 blit an exact 1:1 copy, so the menu
 * cannot be blurred by the scaler it is used to choose games for. */
#define GARLIC_MENU_FONTSIZE 32
#define GARLIC_MENU_NODES_PER_SCREEN 4
/* Environment switches, always documented where they are read. */
#define GARLIC_MENU_ENV "CPYMO_MENU"           /* "0" disables the menu  */
#define GARLIC_MENU_FONT_ENV "CPYMO_MENU_FONT" /* an explicit font file   */
#define GARLIC_GAMES_DIR_ENV "CPYMO_GAMES_DIR"  /* where the games are    */
/* The GarlicOS convention, used only when the environment and the content
 * path both fail to name a directory of games. */
#define GARLIC_GAMES_DIR "/mnt/mmc/Roms/APPS"
/* Up to this many font candidates, tried in order. */
#define GARLIC_FONT_MAX 8
/* The directory the menu was built from, kept so a game without a font of
 * its own can still use the card-wide one. */
static char                        garlic_menu_root[PATH_MAX];
/* Low-overhead real-hardware profiling. Log each 180 retro_run calls:
 * actual wall time and separately the engine update, draw and RGB565 copy.
 * This deliberately does not mistake RetroArch monitor Hz for CPyMO FPS.
 *
 * The 10.19 device log showed that draw+update+convert accounted for only
 * 28.7% of the measured interval, so the report ALSO carries `outside_us`,
 * i.e. the wall time in the interval that no CPyMO stage accounted for
 * (RetroArch video/audio, audio_batch, the libretro call overhead and the
 * microsecond-clock boundaries). That is the remaining long-frame budget the
 * v0.23 task asks about, and it is reported, not guessed at. */
static unsigned garlic_prof_frames = 0, garlic_prof_draws = 0;
static uint64_t garlic_prof_wall_base = 0, garlic_prof_update_us = 0;
static uint64_t garlic_prof_draw_us = 0, garlic_prof_convert_us = 0;
static uint64_t garlic_prof_video_us = 0, garlic_prof_audio_us = 0;
/* Start of the current game load, and the one-shot stage report that uses it
 * (defined next to retro_run; declared here so retro_load_game can call it). */
static uint64_t garlic_load_begin = 0;
static void garlic_load_report(const char *gamedir, error_t err,
                             uint64_t engine_us, uint64_t font_us,
                             uint64_t alloc_us);
static uint64_t garlic_micros(void)
{
    struct timeval t;
    if (gettimeofday(&t, NULL) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000ULL + (uint64_t)t.tv_usec;
}
static void garlic_prof_reset(void)
{
    garlic_prof_frames = garlic_prof_draws = 0;
    garlic_prof_update_us = garlic_prof_draw_us = garlic_prof_convert_us = 0;
    garlic_prof_video_us = garlic_prof_audio_us = 0;
    garlic_prof_wall_base = garlic_micros();
}
static void garlic_prof_report(void)
{
    if (garlic_prof_frames == 0) return;
    static const char *const screen_name[] = { "none", "game", "menu" };
    const char *on = screen_name[(int)garlic_screen <= 2 ? (int)garlic_screen : 0];
    uint64_t wall = garlic_micros() - garlic_prof_wall_base;
    uint64_t accounted = garlic_prof_update_us + garlic_prof_draw_us
                      + garlic_prof_convert_us + garlic_prof_video_us
                      + garlic_prof_audio_us;
    uint64_t outside = wall > accounted ? wall - accounted : 0;
    log_cb(RETRO_LOG_INFO,
           "CPYMO_PERF v0.21 screen=%s frames=%u draws=%u elapsed_ms=%llu "
           "fps_x10=%llu update_us_avg=%llu draw_us_avg=%llu "
           "convert_us_avg=%llu outside_us_avg=%llu video_us_avg=%llu "
           "audio_us_avg=%llu\n",
           on, garlic_prof_frames, garlic_prof_draws,
           (unsigned long long)(wall / 1000),
           (unsigned long long)(wall ? 10000000ULL * garlic_prof_frames / wall : 0),
           (unsigned long long)(garlic_prof_update_us / garlic_prof_frames),
           (unsigned long long)(garlic_prof_draws ? garlic_prof_draw_us / garlic_prof_draws : 0),
           (unsigned long long)(garlic_prof_draws ? garlic_prof_convert_us / garlic_prof_draws : 0),
           (unsigned long long)(outside / garlic_prof_frames),
           (unsigned long long)(garlic_prof_video_us / garlic_prof_frames),
           (unsigned long long)(garlic_prof_audio_us / garlic_prof_frames));
    garlic_prof_reset();
}

static bool                           garlic_directory_changed = false;
static char                           garlic_previous_directory[PATH_MAX];
static char                           garlic_current_directory[PATH_MAX];
static cpymo_backend_software_context  soft_context;
static cpymo_input                     input      = { 0 };
/* Libretro frame-time callback is optional; a missing callback must not
 * freeze PyMO #wait and transitions. Default to 60Hz. */
static float                           delta      = 1.0f / 60.0f;
static stbtt_fontinfo                  font;
#ifndef GARLIC_NO_FFMPEG
static struct SwsContext              *movie_sws;
#endif
static size_t                          movie_height;
static bool                            use_audio_callback = false;
static bool                            audio_enabled = true;
#ifndef GARLIC_NO_FFMPEG
static mtx_t                           audio_mutex;
#endif

cpymo_input cpymo_input_snapshot(void)
{
    return input;
}

void cpymo_backend_audio_free()
{
#ifndef GARLIC_NO_FFMPEG
    if (use_audio_callback)
        mtx_destroy(&audio_mutex);
#endif
}

void cpymo_backend_audio_lock()
{
#ifndef GARLIC_NO_FFMPEG
    mtx_lock(&audio_mutex);
#endif
}

void cpymo_backend_audio_unlock()
{
#ifndef GARLIC_NO_FFMPEG
    mtx_unlock(&audio_mutex);
#endif
}

const cpymo_backend_audio_info *cpymo_backend_audio_get_info(void)
{
    static const cpymo_backend_audio_info audio_info = {
        44100, cpymo_backend_audio_s16, 2
    };
    return &audio_info;
}

#ifndef GARLIC_NO_FFMPEG
enum cpymo_backend_movie_how_to_play cpymo_backend_movie_how_to_play(void)
{
    return cpymo_backend_movie_how_to_play_send_surface;
}

error_t cpymo_backend_movie_init_surface(size_t width, size_t height, enum cpymo_backend_movie_format format)
{
    enum AVPixelFormat src_fmt, dst_fmt;
    switch (format) {
    case cpymo_backend_movie_format_yuv420p:
        src_fmt = AV_PIX_FMT_YUV420P;
        break;
    case cpymo_backend_movie_format_yuv422p:
        src_fmt = AV_PIX_FMT_YUV422P;
        break;
    case cpymo_backend_movie_format_yuv420p16:
        src_fmt = AV_PIX_FMT_YUV420P16;
        break;
    case cpymo_backend_movie_format_yuv422p16:
        src_fmt = AV_PIX_FMT_YUV422P16;
        break;
    case cpymo_backend_movie_format_yuyv422:
        src_fmt = AV_PIX_FMT_YUYV422;
    default:
        return CPYMO_ERR_UNSUPPORTED;
    }
#if ENDIANNESS_LE
    dst_fmt = AV_PIX_FMT_BGRA;
#else
    dst_fmt = AV_PIX_FMT_RGBA;
#endif
    movie_sws = sws_getContext(width, height, src_fmt,
                               soft_image.w, soft_image.h, dst_fmt,
                               SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (movie_sws == NULL)
        return CPYMO_ERR_UNSUPPORTED;
    movie_height = height;
    return CPYMO_ERR_SUCC;
}

void cpymo_backend_movie_update_yuyv_surface(const void *p, size_t pitch)
{
    const uint8_t *src[] = { p };
    uint8_t *dst[] = { soft_image.pixels };
    int src_linesize[] = { pitch };
    int dst_linesize[] = { soft_image.line_stride };
    sws_scale(movie_sws,
              src, src_linesize,
              0, movie_height,
              dst, dst_linesize);
}

void cpymo_backend_movie_update_yuv_surface(const void *y, size_t y_pitch,
                                            const void *u, size_t u_pitch,
                                            const void *v, size_t v_pitch)
{
    const uint8_t *src[] = { y, u, v };
    uint8_t *dst[] = { soft_image.pixels };
    int src_linesize[] = { y_pitch, u_pitch, v_pitch };
    int dst_linesize[] = { soft_image.line_stride };
    sws_scale(movie_sws,
              src, src_linesize,
              0, movie_height,
              dst, dst_linesize);
}

void cpymo_backend_movie_draw_surface(void)
{
}

void cpymo_backend_movie_free_surface(void)
{
    sws_freeContext(movie_sws);
}
#endif /* GARLIC_NO_FFMPEG */

/* The portable path/save logic lives in garlic_paths.c, next to the button and
 * blit adapter, so it can be unit-tested on a host (tests/test_garlic_paths.c).
 * Here we only supply the two things the core decides for itself: the game
 * directory it must act on, and the optional save root.
 *
 * Save isolation (§3 of the v0.23 task)
 * -----------------------------------
 * CPYMO's save layer addresses saves as "<gamedir>/save/<name>"; the fat save
 * API passes `gamedir` through. We keep that contract and place the isolation
 * in the *directory*: if CPYMO_SAVE_ROOT is set, saves go to
 * "<root>/<game_id>/<name>" instead, so many games can share one writable
 * location without ever reading each other's slots. Unset means the historical
 * layout, so the saves already on the user's card keep working. */
static char garlic_save_root[GARLIC_PATH_MAX];
static bool garlic_save_root_known = false;

static const char *garlic_get_save_root(void)
{
    if (!garlic_save_root_known) {
        const char *env = getenv("CPYMO_SAVE_ROOT");
        garlic_save_root[0] = '\0';
        if (env && env[0]) {
            if (strlen(env) >= sizeof(garlic_save_root))
                log_cb(RETRO_LOG_WARN,
                       "CPYMO_SAVE_ROOT is too long; using the in-game save dir\n");
            else
                strcpy(garlic_save_root, env);
        }
        garlic_save_root_known = true;
    }
    return garlic_save_root;
}

static FILE *garlic_open_save(const char *gamedir, const char *name,
                             const char *mode)
{
    char path[GARLIC_PATH_MAX];
    if (!garlic_save_path(path, sizeof(path), gamedir, garlic_get_save_root(),
                          name)) {
        log_cb(RETRO_LOG_WARN, "save path too long for %s\n", name);
        return NULL;
    }
    if (mode[0] == 'w')
        (void)garlic_make_save_dir(gamedir, garlic_get_save_root());
    return fopen(path, mode);
}

FILE *cpymo_backend_read_save(const char *gamedir, const char * name)
{
    return garlic_open_save(gamedir, name, "rb");
}

FILE *cpymo_backend_write_save(const char *gamedir, const char * name)
{
    return garlic_open_save(gamedir, name, "wb");
}

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

static void audio_callback(void)
{
    static const int samples = 2048;
    static int16_t audio_buffer[2048 * 2];
#ifndef GARLIC_NO_FFMPEG
    mtx_lock(&audio_mutex);
#endif
    cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
#ifndef GARLIC_NO_FFMPEG
    mtx_unlock(&audio_mutex);
#endif
    audio_batch_cb(audio_buffer, samples);
}

static void audio_set_state(bool enabled)
{
    audio_enabled = enabled;
}

void retro_set_environment(retro_environment_t cb)
{
    struct retro_log_callback log;
    struct retro_audio_callback audio = {
        .callback = audio_callback,
        .set_state = audio_set_state,
    };
    environ_cb = cb;
    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;

    if (environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio)) {
        use_audio_callback = true;
#ifndef GARLIC_NO_FFMPEG
        mtx_init(&audio_mutex, mtx_plain);
#endif
    }
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
    info->need_fullpath = true;
    info->valid_extensions = "txt|/";
    info->library_version = "1.1.9";
    info->library_name = "CPyMO";
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = GARLIC_OUTPUT_WIDTH;
    info->geometry.base_height = GARLIC_OUTPUT_HEIGHT;
    info->geometry.max_width = GARLIC_OUTPUT_WIDTH;
    info->geometry.max_height = GARLIC_OUTPUT_HEIGHT;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

static void frame_cb(retro_usec_t usec)
{
    /* Ignore bogus suspend/resume intervals; never step the script by
     * seconds in a single frame or stall on zero ticks. */
    delta = (usec >= 1000 && usec <= 100000)
        ? (float)usec / 1000000.0f : 1.0f / 60.0f;
}

void retro_init(void)
{
    delta = 1.0f / 60.0f;
    enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_RGB565;
    struct retro_frame_time_callback frametime = {
        .callback = frame_cb,
        .reference = 1000000 / 60.0,
    };
    garlic_pixel_format_supported =
        environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
    if (!garlic_pixel_format_supported)
        log_cb(RETRO_LOG_ERROR, "GarlicOS: RGB565 pixel format unavailable\n");
    environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frametime);
}


static error_t cpymo_backend_font_try_load_font(const char *path)
{
    unsigned char *data = NULL;
    size_t len = 0;
    /* Replacing a font is normal here: the selector draws its menu with one
     * font and every chosen game brings its own. Without this the menu font
     * would leak (~95 kB) on every selection. */
    if (font.data) {
        free(font.data);
        font.data = NULL;
    }
    error_t err = cpymo_utils_loadfile(path, (char **)&data, &len);
    if (err != CPYMO_ERR_SUCC)
        return err;
    if (stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
        free(data);
        font.data = NULL;
        return CPYMO_ERR_BAD_FILE_FORMAT;
    }
    return CPYMO_ERR_SUCC;
}

/* Which font a run is actually using, said ONCE per load, in the same words
 * for the menu and for a game. `$CPYMO_MENU_FONT` outranks everything, so
 * naming it in the log is how a user confirms the override took effect. */
static void garlic_report_font(const char *path, const char *gamedir)
{
    const char *menu_font = getenv(GARLIC_MENU_FONT_ENV);
    const char *where = "the environment's";
    if (menu_font && menu_font[0] && strcmp(path, menu_font) == 0)
        where = "the environment's";
    else if (gamedir && gamedir[0] && strncmp(path, gamedir, strlen(gamedir)) == 0)
        where = "this game's";
    else if (garlic_menu_root[0]
             && strncmp(path, garlic_menu_root, strlen(garlic_menu_root)) == 0)
        where = "the games root's";
    else
        where = "the frontend's";
    log_cb(RETRO_LOG_INFO, "font: %s (%s)\n", path, where);
}

/* One font rule for the whole core -- the game, the selector's menu and the
 * fallback that used to be hard-coded here all come from
 * garlic_font_candidates(), so what the log prints and what the code tries
 * cannot drift apart. The last candidates are the frontend's
 * <systemdir>/cpymo/default.{ttf,otf}, which is the historical fallback.
 *
 * Candidates are ranked, so a card-wide font is tried only after the game's
 * own: the caller passes the games root as `fallback_dir`. */
static error_t cpymo_backend_font_init(const char *gamedir,
                                      const char *fallback_dir)
{
    const char *systemdir = NULL;
    (void)environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemdir);
    const char *menu_font = getenv(GARLIC_MENU_FONT_ENV);

    char candidates[8][GARLIC_PATH_MAX];
    size_t n = 0;
    /* The explicit font first, then the game's own, then the fallback. */
    if (gamedir && gamedir[0])
        n += garlic_font_candidates(gamedir, NULL, menu_font,
                                   (char *)candidates, GARLIC_PATH_MAX, 8);
    else if (menu_font && menu_font[0])
        n += garlic_font_candidates(NULL, NULL, menu_font,
                                   (char *)candidates, GARLIC_PATH_MAX, 8);
    if (fallback_dir && fallback_dir[0] && n < 8) {
        char more[8][GARLIC_PATH_MAX];
        size_t k = garlic_font_candidates(fallback_dir, NULL, NULL,
                                         (char *)more, GARLIC_PATH_MAX, 8);
        for (size_t i = 0; i < k && n < 8; ++i)
            snprintf(candidates[n++], GARLIC_PATH_MAX, "%s", more[i]);
    }
    if (systemdir && systemdir[0] && n < 8) {
        char more[8][GARLIC_PATH_MAX];
        size_t k = garlic_font_candidates(NULL, systemdir, NULL,
                                         (char *)more, GARLIC_PATH_MAX, 8);
        for (size_t i = 0; i < k && n < 8; ++i)
            snprintf(candidates[n++], GARLIC_PATH_MAX, "%s", more[i]);
    }

    error_t last = CPYMO_ERR_NOT_FOUND;
    for (size_t i = 0; i < n; ++i) {
        error_t err = cpymo_backend_font_try_load_font(candidates[i]);
        if (err == CPYMO_ERR_SUCC) {
            garlic_report_font(candidates[i], gamedir);
            return CPYMO_ERR_SUCC;
        }
        last = err;
    }
    if (n == 0) {
        log_cb(RETRO_LOG_ERROR, "no font candidate: no game dir, no system dir\n");
        return CPYMO_ERR_NOT_FOUND;
    }
    /* Name every place that was tried: a missing font is otherwise the
     * silent-blank-screen failure this project keeps having to diagnose. */
    for (size_t i = 0; i < n; ++i)
        log_cb(RETRO_LOG_WARN, "no font at %s\n", candidates[i]);
    return last;
}

/* Libretro runs in RetroArch's process. A failed load must leave its current
 * directory, font state and backend context unchanged for subsequent games.
 * Do not depend on GNU's realpath(path, NULL) extension on uClibc. */
void retro_unload_game(void);

/* ------------------------------------------------------------------ */
/* the screen: one canvas, re-sized per game, and one loader for both   */
/* ------------------------------------------------------------------ */

/* Make the render target (and the RGB565 output) exactly w x h. Called for
 * the menu (the OUTPUT size) and again for every chosen game (its OWN logical
 * size), so the two screens cannot disagree about what "the canvas" is. */
static bool garlic_apply_canvas(size_t w, size_t h)
{
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return false;
    if (soft_image.pixels && soft_image.w == w && soft_image.h == h) {
        soft_context.render_target = &soft_image;
        soft_context.logical_screen_w = (float)w;
        soft_context.logical_screen_h = (float)h;
        soft_context.font = &font;
        cpymo_backend_software_set_context(&soft_context);
        return true;
    }

    uint8_t *pixels = (uint8_t *)calloc(h, w * 4);
    if (!pixels) return false;
    if (soft_image.pixels) free(soft_image.pixels);
    soft_image.pixels = pixels;
    soft_image.w = w;
    soft_image.h = h;
    soft_image.line_stride = w * 4;
    soft_image.pixel_stride = 4;
    soft_image.r_offset = 2;
    soft_image.g_offset = 1;
    soft_image.b_offset = 0;
    soft_image.a_offset = 3;
    soft_image.has_alpha_channel = false;

    if (!garlic_rgb565_frame)
        garlic_rgb565_frame = (uint16_t *)calloc(
            GARLIC_OUTPUT_WIDTH * GARLIC_OUTPUT_HEIGHT, sizeof(uint16_t));
    if (!garlic_rgb565_frame) return false;

    soft_context.font = &font;
    soft_context.render_target = &soft_image;
    soft_context.logical_screen_w = (float)w;
    soft_context.logical_screen_h = (float)h;
    soft_context.scale_on_load_image = false;
    cpymo_backend_software_set_context(&soft_context);
    return true;
}

/* The per-game half of the platform work: the engine has already been
 * re-initialised for `gamedir` by the selector / plain loader, so this brings
 * the CWD, the save directory, the font and the canvas along. */
static error_t garlic_after_game_selected(const char *gamedir)
{
    if (chdir(gamedir) != 0) {
        log_cb(RETRO_LOG_ERROR, "chdir: %s\n", strerror(errno));
        return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    }
    garlic_directory_changed = true;
    if (!garlic_make_save_dir(gamedir, garlic_get_save_root())) {
        log_cb(RETRO_LOG_WARN, "save directory is not writable: %s\n",
               strerror(errno));
    }
    error_t err = cpymo_backend_font_init(gamedir, garlic_menu_root);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "no font for %s: %s\n", gamedir,
               cpymo_error_message(err));
        return err;
    }
    if (!garlic_apply_canvas(engine.gameconfig.imagesize_w,
                             engine.gameconfig.imagesize_h)) {
        log_cb(RETRO_LOG_ERROR, "invalid or excessive game canvas size: %ux%u\n",
               (unsigned)engine.gameconfig.imagesize_w,
               (unsigned)engine.gameconfig.imagesize_h);
        return CPYMO_ERR_OUT_OF_MEM;
    }
    snprintf(garlic_selected_game, sizeof(garlic_selected_game), "%s", gamedir);
    garlic_screen = GARLIC_SCREEN_GAME;
    garlic_first_frame = true;
    log_cb(RETRO_LOG_INFO, "CPYMO_SELECT game=%s canvas=%ux%u\n", gamedir,
           (unsigned)engine.gameconfig.imagesize_w,
           (unsigned)engine.gameconfig.imagesize_h);
    return CPYMO_ERR_SUCC;
}

static error_t garlic_selector_before(cpymo_engine *e, const char *gamedir)
{
    (void)e;
    (void)gamedir;
    return CPYMO_ERR_SUCC;
}

static error_t garlic_selector_after(cpymo_engine *e, const char *gamedir)
{
    (void)e;
    return garlic_after_game_selected(gamedir);
}

/* Open the game selector over `games_root`.
 *
 * The menu draws at the OUTPUT size on purpose: the RGB565 blit is then an
 * exact 1:1 copy, so the screen a user picks a game on cannot be blurred by
 * the scaler it exists to work around. The canvas is re-sized again by
 * garlic_after_game_selected() once a game is chosen.
 *
 * Returns true with garlic_screen == MENU, or false having logged why; a false
 * here is not fatal for `retro_load_game`, which then falls back to a plain
 * load. */
static bool garlic_open_menu(const char *games_root, const char *initial_game)
{
    char *dirs[64];
    size_t count = 0;
    if (!garlic_scan_game_dirs(games_root, dirs, 64, &count) || count == 0) {
        for (size_t i = 0; i < count; ++i) free(dirs[i]);
        log_cb(RETRO_LOG_INFO, "no game found under %s\n", games_root);
        return false;
    }

    cpymo_game_selector_item *items = NULL, *tail = NULL;
    for (size_t i = 0; i < count; ++i) {
        cpymo_game_selector_item *item = NULL;
        char *path = dirs[i];       /* ownership moves into the item */
        if (cpymo_game_selector_item_create(&item, &path) != CPYMO_ERR_SUCC) {
            free(path);
            cpymo_game_selector_item_free_all(items);
            return false;
        }
        item->prev = tail;
        if (tail) tail->next = item; else items = item;
        tail = item;
    }

    if (!garlic_apply_canvas(GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT)) {
        cpymo_game_selector_item_free_all(items);
        return false;
    }
    /* The menu needs a font OF ITS OWN, before any game has been chosen:
     * <games_root>/system/default.ttf is a candidate, and an explicit
     * CPYMO_MENU_FONT outranks it. */
    if (cpymo_backend_font_init(games_root, NULL) != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR,
               "no usable font for the menu (%s/system/default.ttf, "
               "$" GARLIC_MENU_FONT_ENV ")\n", games_root);
        cpymo_game_selector_item_free_all(items);
        return false;
    }

    char *last_selected = NULL;
    if (initial_game && initial_game[0])
        last_selected = strdup(initial_game);

    garlic_load_begin = garlic_micros();
    uint64_t tick = garlic_load_begin;
    error_t err = cpymo_engine_init_with_game_selector(
        &engine, GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT,
        GARLIC_MENU_FONTSIZE, (float)GARLIC_MENU_FONTSIZE * 2.0f,
        GARLIC_MENU_NODES_PER_SCREEN,
        &items, &garlic_selector_before, &garlic_selector_after,
        &last_selected);
    if (last_selected) free(last_selected);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_engine_init_with_game_selector: %s\n",
               cpymo_error_message(err));
        cpymo_game_selector_item_free_all(items);
        return false;
    }

    garlic_engine_ready = true;
    garlic_game_loaded = true;
    garlic_first_frame = true;
    garlic_screen = GARLIC_SCREEN_MENU;
    garlic_prof_reset();
    {
        uint64_t now = garlic_micros();
        garlic_load_report(games_root, CPYMO_ERR_SUCC, now - tick, 0, 0);
        log_cb(RETRO_LOG_INFO, "CPYMO_MENU root=%s games=%zu\n", games_root,
               count);
    }
    return true;
}

bool retro_load_game(const struct retro_game_info *game)
{
    error_t err;
    if (garlic_game_loaded || garlic_engine_ready)
        retro_unload_game();
    if (!game || !game->path || !garlic_pixel_format_supported)
        return false;

    struct stat sb;
    if (stat(game->path, &sb) != 0) {
        log_cb(RETRO_LOG_ERROR, "stat: %s\n", strerror(errno));
        return false;
    }

    /* Universal game loading (§1 of the v0.23 task)
     * -------------------------------------------
     * The launcher may hand us a game DIRECTORY or a FILE inside it (normally
     * "gameconfig.txt"); both must work, for any game. `is_dir` is decided
     * here with a real stat(), never by guessing from the name's shape, so a
     * directory whose name contains a dot ("v1.2") is not mistaken for a
     * file. `gameconfig.txt` is REQUIRED and is reported as a missing file
     * rather than as an opaque engine error. */
    char gamedir[PATH_MAX];
    {
        char resolved[PATH_MAX];
        if (!realpath(game->path, resolved)) {
            log_cb(RETRO_LOG_ERROR, "realpath: %s\n", strerror(errno));
            return false;
        }
        if (!garlic_dir_of_content(resolved, S_ISDIR(sb.st_mode) != 0,
                                 gamedir, sizeof(gamedir))) {
            log_cb(RETRO_LOG_ERROR, "content path too long\n");
            return false;
        }
    }
    char has_config = 0;
    {
        char cfg[PATH_MAX];
        struct stat cfg_sb;
        has_config = (snprintf(cfg, sizeof(cfg), "%s/gameconfig.txt", gamedir)
                          < (int)sizeof(cfg)
                      && stat(cfg, &cfg_sb) == 0);
        if (!has_config) {
            /* §4 of the v0.23 task: a content path that is the GAMES FOLDER
             * rather than a game is not an error -- it is the selector. The
             * directory is verified by garlic_games_dir(), so nothing is
             * guessed from the shape of the path. */
            const char *env = getenv(GARLIC_GAMES_DIR_ENV);
            const char *menu = getenv(GARLIC_MENU_ENV);
            if (!menu || (menu[0] != '0' && menu[0] != 'n' && menu[0] != 'N')) {
                char games_root[PATH_MAX];
                if (garlic_games_dir(env, gamedir, GARLIC_GAMES_DIR,
                                     games_root, sizeof(games_root))) {
                    if (!getcwd(garlic_previous_directory,
                                sizeof(garlic_previous_directory)))
                        log_cb(RETRO_LOG_WARN, "getcwd: %s\n", strerror(errno));
                    snprintf(garlic_menu_root, sizeof(garlic_menu_root), "%s",
                             games_root);
                    if (garlic_open_menu(games_root, NULL)) return true;
                }
            }
            log_cb(RETRO_LOG_ERROR,
                   "no gameconfig.txt in the game directory: %s\n", gamedir);
            /* "." asks the helper to scan `gamedir` itself, because the
             * missing file lives directly in it, not under a subdirectory. */
            char alt[PATH_MAX];
            if (garlic_diagnose_case(gamedir, ".", "gameconfig", 10, "txt",
                                     alt, sizeof(alt)) == 1)
                log_cb(RETRO_LOG_WARN,
                       "a case-different file exists: %s\n", alt);
            return false;
        }
    }

    if (!getcwd(garlic_previous_directory, sizeof(garlic_previous_directory))) {
        log_cb(RETRO_LOG_ERROR, "getcwd: %s\n", strerror(errno));
        return false;
    }
    if (chdir(gamedir) != 0) {
        log_cb(RETRO_LOG_ERROR, "chdir: %s\n", strerror(errno));
        return false;
    }
    garlic_directory_changed = true;
    /* The engine still reads/writes the per-game "save" directory relative to
     * the game; a failure here must not stop the game from starting. */
    if (!garlic_make_save_dir(gamedir, garlic_get_save_root())) {
        if (mkdir("save", 0755) != 0 && errno != EEXIST)
            log_cb(RETRO_LOG_WARN,
                   "save directory is not writable: %s\n", strerror(errno));
    }

    uint64_t tick_engine, tick_font, tick_alloc;
    garlic_load_begin = garlic_micros();

    tick_engine = garlic_load_begin;
    err = cpymo_engine_init(&engine, gamedir);
    tick_font = garlic_micros();
    uint64_t tick_font_done = tick_font;
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_engine_init: %s\n", cpymo_error_message(err));
        garlic_load_report(gamedir, err, tick_font - tick_engine, 0, 0);
        goto fail;
    }
    garlic_engine_ready = true;

    /* Font and canvas: the same two steps the selector performs after a
     * selection, so one game behaves identically however it was started.
     * `garlic_menu_root` supplies the card-wide font of a games folder, which
     * is the same file the menu itself used. */
    {
        error_t ferr = cpymo_backend_font_init(gamedir, garlic_menu_root);
        tick_alloc = garlic_micros();
        tick_font_done = tick_alloc;
        if (ferr != CPYMO_ERR_SUCC) {
            log_cb(RETRO_LOG_ERROR, "cpymo_backend_font_init: %s\n",
                   cpymo_error_message(ferr));
            garlic_load_report(gamedir, ferr, tick_font - tick_engine,
                              tick_alloc - tick_font, 0);
            goto fail;
        }
    }
    if (!garlic_apply_canvas(engine.gameconfig.imagesize_w,
                             engine.gameconfig.imagesize_h)) {
        log_cb(RETRO_LOG_ERROR, "invalid or excessive game canvas size\n");
        goto fail;
    }
    garlic_first_frame = true;
    garlic_game_loaded = true;
    garlic_screen = GARLIC_SCREEN_GAME;
    snprintf(garlic_selected_game, sizeof(garlic_selected_game), "%s", gamedir);
    garlic_prof_reset();
    log_cb(RETRO_LOG_INFO, "CPYMO_SELECT game=%s canvas=%ux%u (direct)\n", gamedir,
           (unsigned)engine.gameconfig.imagesize_w,
           (unsigned)engine.gameconfig.imagesize_h);
    garlic_load_report(gamedir, CPYMO_ERR_SUCC, tick_font - tick_engine,
                      tick_font_done - tick_font,
                      garlic_micros() - tick_font_done);
    return true;

fail:
    retro_unload_game();
    return false;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_deinit(void)
{
    if (garlic_game_loaded || garlic_engine_ready || garlic_directory_changed)
        retro_unload_game();
}

void retro_reset(void)
{
}

/* One-shot, load-time report. The 10.19 device log had one redraw costing
 * 235 ms at the opening and 259 ms on a background switch, against ~73 ms in
 * the steady menu, and none of the ordinary counters could say WHERE inside
 * the load that went. This prints the stage boundaries of the game that was
 * just loaded, once, so a slow asset becomes visible instead of inferred. */
static void garlic_load_report(const char *gamedir, error_t err,
                            uint64_t engine_us, uint64_t font_us,
                            uint64_t alloc_us)
{
    uint64_t total = garlic_load_begin ? garlic_micros() - garlic_load_begin : 0;
    log_cb(RETRO_LOG_INFO,
           "CPYMO_LOAD gamedir=%s ok=%d total_us=%llu engine_us=%llu "
           "font_us=%llu alloc_us=%llu canvas=%ux%u\n",
           gamedir, err == CPYMO_ERR_SUCC ? 1 : 0,
           (unsigned long long)total,
           (unsigned long long)engine_us,
           (unsigned long long)font_us,
           (unsigned long long)alloc_us,
           (unsigned)engine.gameconfig.imagesize_w,
           (unsigned)engine.gameconfig.imagesize_h);
}

static void input_update(void)
{
#define JOY(x) input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_##x)
    uint32_t b = 0;
    if (JOY(A))      b |= GARLIC_BUTTON_A;
    if (JOY(B))      b |= GARLIC_BUTTON_B;
    if (JOY(X))      b |= GARLIC_BUTTON_X;
    if (JOY(Y))      b |= GARLIC_BUTTON_Y;
    if (JOY(UP))     b |= GARLIC_BUTTON_UP;
    if (JOY(DOWN))   b |= GARLIC_BUTTON_DOWN;
    if (JOY(LEFT))   b |= GARLIC_BUTTON_LEFT;
    if (JOY(RIGHT))  b |= GARLIC_BUTTON_RIGHT;
    if (JOY(L))      b |= GARLIC_BUTTON_L;
    if (JOY(R))      b |= GARLIC_BUTTON_R;
    if (JOY(START))  b |= GARLIC_BUTTON_START;
    if (JOY(SELECT)) b |= GARLIC_BUTTON_SELECT;
#undef JOY
    garlic_actions a = garlic_map_buttons(b, engine.say.active && !engine.ui);
    input.up = a.up;
    input.down = a.down;
    input.left = a.left;
    input.right = a.right;
    input.ok = a.ok;
    input.cancel = a.cancel;
    input.skip = a.skip;
    input.hide_window = a.hide_window;
    /* RG35XX has no touch screen. Never fabricate pointer events. */
    input.mouse_position_useable = false;
    input.mouse_button = false;
    input.mouse_x = 0;
    input.mouse_y = 0;
    input.mouse_wheel_delta = 0;
}

void retro_run(void)
{
    if (!garlic_game_loaded) return;
    static const int samples = 44100 / 60;
    static int16_t audio_buffer[44100 / 60 * 2];
    bool redraw = false;

    input_poll_cb();
    input_update();

    uint64_t tick = garlic_micros();
    error_t err = cpymo_engine_update(&engine, delta, &redraw);
    uint64_t after_update = garlic_micros();
    garlic_prof_update_us += after_update - tick;
    if (err == CPYMO_ERR_NO_MORE_CONTENT)
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);

    if (redraw || garlic_first_frame) {
        garlic_prof_draws++;
        tick = garlic_micros();
        cpymo_engine_draw(&engine);
        uint64_t after_draw = garlic_micros();
        garlic_prof_draw_us += after_draw - tick;
        if (!garlic_blit_rgb565(garlic_rgb565_frame, GARLIC_OUTPUT_WIDTH * 2,
            GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, soft_image.pixels,
            soft_image.line_stride, soft_image.w, soft_image.h)) {
            log_cb(RETRO_LOG_ERROR, "GarlicOS: frame conversion failed\n");
            environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
            return;
        }
        garlic_prof_convert_us += garlic_micros() - after_draw;
        garlic_first_frame = false;
    }
    garlic_prof_frames++;
    if (garlic_prof_frames >= 180) garlic_prof_report();

    tick = garlic_micros();
    video_cb(garlic_rgb565_frame, GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, GARLIC_OUTPUT_WIDTH * 2);
    garlic_prof_video_us += garlic_micros() - tick;

    if (audio_enabled && !use_audio_callback) {
        tick = garlic_micros();
        cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
        audio_batch_cb(audio_buffer, samples);
        garlic_prof_audio_us += garlic_micros() - tick;
    }
}

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void *data, size_t size)
{
    return false;
}

bool retro_unserialize(const void *data, size_t size)
{
    return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    return false;
}

void retro_unload_game(void)
{
    if (garlic_game_loaded) garlic_prof_report();
    /* Engine release can call UI and save logic; keep the framebuffer context
       and the correct game directory alive until it has completed. */
    if (garlic_engine_ready) {
        cpymo_engine_free(&engine);
        garlic_engine_ready = false;
    }
    cpymo_backend_software_set_context(NULL);
    free(garlic_rgb565_frame);
    garlic_rgb565_frame = NULL;
    garlic_first_frame = false;
    garlic_game_loaded = false;
    if (soft_image.pixels) {
        free(soft_image.pixels);
        soft_image.pixels = NULL;
    }
    if (font.data) {
        free(font.data);
        font.data = NULL;
    }
    if (garlic_directory_changed) {
        if (chdir(garlic_previous_directory) != 0)
            log_cb(RETRO_LOG_ERROR, "restore cwd failed: %s\n", strerror(errno));
        garlic_directory_changed = false;
    }
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    return 0;
}

size_t retro_get_memory_size(unsigned id)
{
    return 0;
}
