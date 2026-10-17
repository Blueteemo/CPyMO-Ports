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
#include "../../endianness.h/endianness.h"
#include "../software/cpymo_backend_software.h"
#include "../include/cpymo_backend_audio.h"
#include "../include/cpymo_backend_movie.h"

#ifndef GARLIC_NO_FFMPEG
#include <libswscale/swscale.h>
#endif
#include "../garlicos/garlic_adapter.h"
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
/* Low-overhead real-hardware profiling. Log each 180 retro_run calls:
 * actual wall time and separately the engine update, draw and RGB565 copy.
 * This deliberately does not mistake RetroArch monitor Hz for CPyMO FPS. */
static unsigned garlic_prof_frames = 0, garlic_prof_draws = 0;
static uint64_t garlic_prof_wall_base = 0, garlic_prof_update_us = 0;
static uint64_t garlic_prof_draw_us = 0, garlic_prof_convert_us = 0;
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
    garlic_prof_wall_base = garlic_micros();
}
static void garlic_prof_report(void)
{
    if (garlic_prof_frames == 0) return;
    uint64_t wall = garlic_micros() - garlic_prof_wall_base;
    log_cb(RETRO_LOG_INFO,
           "CPYMO_PERF v0.21 frames=%u draws=%u elapsed_ms=%llu "
           "fps_x10=%llu update_us_avg=%llu draw_us_avg=%llu "
           "convert_us_avg=%llu\n",
           garlic_prof_frames, garlic_prof_draws,
           (unsigned long long)(wall / 1000),
           (unsigned long long)(wall ? 10000000ULL * garlic_prof_frames / wall : 0),
           (unsigned long long)(garlic_prof_update_us / garlic_prof_frames),
           (unsigned long long)(garlic_prof_draws ? garlic_prof_draw_us / garlic_prof_draws : 0),
           (unsigned long long)(garlic_prof_draws ? garlic_prof_convert_us / garlic_prof_draws : 0));
    garlic_prof_reset();
}

static bool                           garlic_directory_changed = false;
static char                           garlic_previous_directory[PATH_MAX];
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

FILE *cpymo_backend_read_save(const char *gamedir, const char * name)
{
    char *path = (char *)alloca(strlen(name) + 8);
    sprintf(path, "save/%s", name);
    return fopen(path, "rb");
}

FILE *cpymo_backend_write_save(const char *gamedir, const char * name)
{
    char *path = (char *)alloca(strlen(name) + 8);
    sprintf(path, "save/%s", name);
    return fopen(path, "wb");
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

static error_t cpymo_backend_font_init(const char *gamedir)
{
    error_t err;
    char *fontpath = (char *)alloca(strlen(gamedir) + 24);

    sprintf(fontpath, "%s/system/default.ttf", gamedir);
    err = cpymo_backend_font_try_load_font(fontpath);
    if (err == CPYMO_ERR_SUCC)
        return CPYMO_ERR_SUCC;
    sprintf(fontpath, "%s/system/default.otf", gamedir);
    err = cpymo_backend_font_try_load_font(fontpath);
    if (err == CPYMO_ERR_SUCC)
        return CPYMO_ERR_SUCC;

    const char *systemdir;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemdir)) {
        fontpath = (char *)alloca(strlen(systemdir) + 24);
        sprintf(fontpath, "%s/cpymo/default.ttf", systemdir);
        err = cpymo_backend_font_try_load_font(fontpath);
        if (err == CPYMO_ERR_SUCC)
            return CPYMO_ERR_SUCC;
        sprintf(fontpath, "%s/cpymo/default.otf", systemdir);
        err = cpymo_backend_font_try_load_font(fontpath);
        if (err == CPYMO_ERR_SUCC)
            return CPYMO_ERR_SUCC;
    }
    return err;
}

/* Libretro runs in RetroArch's process. A failed load must leave its current
 * directory, font state and backend context unchanged for subsequent games.
 * Do not depend on GNU's realpath(path, NULL) extension on uClibc. */
void retro_unload_game(void);

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
    char *path_copy = strdup(game->path);
    if (!path_copy)
        return false;
    const char *input_directory = S_ISDIR(sb.st_mode) ? game->path : dirname(path_copy);
    char gamedir[PATH_MAX];
    if (!realpath(input_directory, gamedir)) {
        log_cb(RETRO_LOG_ERROR, "realpath: %s\n", strerror(errno));
        free(path_copy);
        return false;
    }
    free(path_copy);

    if (!getcwd(garlic_previous_directory, sizeof(garlic_previous_directory))) {
        log_cb(RETRO_LOG_ERROR, "getcwd: %s\n", strerror(errno));
        return false;
    }
    if (chdir(gamedir) != 0) {
        log_cb(RETRO_LOG_ERROR, "chdir: %s\n", strerror(errno));
        return false;
    }
    garlic_directory_changed = true;
    /* A missing save directory should not make the game fail to start.
       Engine initialization can still safely ignore the absent save data. */
    if (mkdir("save", 0755) != 0 && errno != EEXIST)
        log_cb(RETRO_LOG_WARN, "save directory is not writable: %s\n", strerror(errno));

    err = cpymo_engine_init(&engine, gamedir);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_engine_init: %s\n", cpymo_error_message(err));
        goto fail;
    }
    garlic_engine_ready = true;
    err = cpymo_backend_font_init(gamedir);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "cpymo_backend_font_init: %s\n", cpymo_error_message(err));
        goto fail;
    }

    soft_image.w = engine.gameconfig.imagesize_w;
    soft_image.h = engine.gameconfig.imagesize_h;
    if (soft_image.w == 0 || soft_image.h == 0 || soft_image.w > 2048 || soft_image.h > 2048) {
        log_cb(RETRO_LOG_ERROR, "invalid or excessive game canvas size\n");
        goto fail;
    }
    soft_image.line_stride = soft_image.w * 4;
    soft_image.pixel_stride = 4;
    soft_image.r_offset = 2;
    soft_image.g_offset = 1;
    soft_image.b_offset = 0;
    soft_image.a_offset = 3;
    soft_image.has_alpha_channel = false;
    soft_image.pixels = (uint8_t *)calloc(soft_image.h, soft_image.line_stride);
    garlic_rgb565_frame = (uint16_t *)calloc(
        GARLIC_OUTPUT_WIDTH * GARLIC_OUTPUT_HEIGHT, sizeof(uint16_t));
    if (!soft_image.pixels || !garlic_rgb565_frame) {
        log_cb(RETRO_LOG_ERROR, "GarlicOS: frame allocation failed\n");
        goto fail;
    }

    soft_context.font = &font;
    soft_context.render_target = &soft_image;
    soft_context.logical_screen_w = soft_image.w;
    soft_context.logical_screen_h = soft_image.h;
    soft_context.scale_on_load_image = false;
    cpymo_backend_software_set_context(&soft_context);
    garlic_first_frame = true;
    garlic_game_loaded = true;
    garlic_prof_reset();
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
    video_cb(garlic_rgb565_frame, GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, GARLIC_OUTPUT_WIDTH * 2);

    if (audio_enabled && !use_audio_callback) {
        cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
        audio_batch_cb(audio_buffer, samples);
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
