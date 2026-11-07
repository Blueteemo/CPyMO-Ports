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
#include "garlic_audio_trace.h"

#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_game_selector.h"
#include "../../cpymo/cpymo_config_ui.h"
#include "../../cpymo/cpymo_save.h"
#include "../../cpymo/garlic_movie.h"
#include "../../cpymo/garlic_image_cache.h"
#include "../../cpymo/garlic_stages.h"
#include "../../endianness.h/endianness.h"
#include "../software/cpymo_backend_software.h"
#include "../include/cpymo_backend_audio.h"
#include "../include/cpymo_backend_movie.h"

#ifndef GARLIC_NO_FFMPEG
#include <libswscale/swscale.h>
#endif
#include "../garlicos/garlic_adapter.h"
#include "../garlicos/garlic_confirm.h"
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
static void garlic_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t              log_cb = garlic_log;
static retro_log_printf_t              frontend_log = fallback_log;
static _Atomic bool garlic_debug;
static char garlic_log_path[PATH_MAX];
static FILE *garlic_log_file;
static size_t garlic_log_bytes;
static unsigned garlic_output_percent = 50;
#include "garlic_audio_observer.h"
static void garlic_diagnostics_begin(const char *gamedir);
static retro_video_refresh_t           video_cb;
static retro_input_poll_t              input_poll_cb;
static retro_input_state_t             input_state_cb;
static retro_audio_sample_batch_t      audio_batch_cb;
static retro_environment_t             environ_cb;
static cpymo_engine                    engine;
static cpymo_backend_software_image    soft_image;
static uint16_t                      *garlic_rgb565_frame = NULL;
/* Text is rasterised at output resolution and composited here after the
 * canvas blit, so glyphs are never nearest-neighbour upscaled. */
static uint32_t                      *garlic_text_layer = NULL;
static void garlic_text_layer_configure(void);
static bool                           garlic_first_frame = false;
/* Keep the complete library output while the boot script has not drawn yet. */
static bool                           garlic_waiting_for_game_frame = false;
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
static char                        garlic_pending_game[PATH_MAX];
static bool                        garlic_library_requested;
static bool                        garlic_exit_requested;
static error_t                     garlic_abort_error;

error_t garlic_queue_game(const char *gamedir)
{
    if (!gamedir || strlen(gamedir) >= sizeof(garlic_pending_game))
        return CPYMO_ERR_INVALID_ARG;
    strcpy(garlic_pending_game, gamedir);
    return CPYMO_ERR_SUCC;
}

void garlic_request_library(void) { garlic_library_requested = true; }
void garlic_abort_game(error_t cause) { garlic_abort_error = cause; }

void garlic_movie_log(const char *message)
{
    log_cb(RETRO_LOG_INFO, "%s\n", message);
}
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

/* Short-lived on-screen notice for one-shot keys (quick save/load, auto play).
 * Drawn over the finished canvas, so it never disturbs engine layout. */
static cpymo_backend_text garlic_toast_text;
static cpymo_backend_text garlic_library_footer;
static void garlic_toast_clear(void);
static float             garlic_toast_left;
static float             garlic_toast_w, garlic_toast_h;
static uint32_t          garlic_prev_buttons;
static garlic_confirm_key garlic_save_confirm, garlic_load_confirm;
static bool              garlic_drain_buttons;
#ifdef GARLIC_UI_DEBUG
static unsigned          polls_dbg;
#endif
static stbtt_fontinfo                  font;
#ifndef GARLIC_NO_FFMPEG
static struct SwsContext              *movie_sws;
#endif
static size_t                          movie_height;
static bool                            use_audio_callback = false;
static _Atomic bool                    audio_enabled = true;
#ifndef GARLIC_NO_FFMPEG
static mtx_t                           audio_mutex;
/* The mutex is only initialised when the frontend accepts
 * RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, but cpymo_backend_audio_lock() is
 * called by the ENGINE whenever it touches channel state -- not only from the
 * callback. A frontend that keeps the callback (so `use_audio_callback` stays
 * false and no mtx_init happens) would then lock an uninitialised mtx_t, which
 * is undefined behaviour: on glibc it is a zeroed pthread_mutex_t and usually
 * "works", on uClibc it need not. Track initialisation and lock only when there
 * is a mutex to lock; in batch mode there is a single thread, so there is
 * nothing to serialise anyway. */
static bool                            audio_mutex_initialized = false;
#endif

cpymo_input cpymo_input_snapshot(void)
{
    return input;
}

void cpymo_backend_audio_free()
{
#ifndef GARLIC_NO_FFMPEG
    if (audio_mutex_initialized) {
        mtx_destroy(&audio_mutex);
        audio_mutex_initialized = false;
    }
#endif
}

void cpymo_backend_audio_lock()
{
#ifndef GARLIC_NO_FFMPEG
    /* Symmetric with the init in retro_set_environment. Locking an
     * uninitialised mutex is UB, and in batch mode there is no second thread
     * to protect against. */
    if (audio_mutex_initialized) mtx_lock(&audio_mutex);
#endif
}

void cpymo_backend_audio_unlock()
{
#ifndef GARLIC_NO_FFMPEG
    if (audio_mutex_initialized) mtx_unlock(&audio_mutex);
#endif
}

const cpymo_backend_audio_info *cpymo_backend_audio_get_info(void)
{
    static const cpymo_backend_audio_info audio_info = {
        48000, cpymo_backend_audio_s16, 2
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

/* Bounded diagnostics: no per-frame SD writes in normal play. A fresh
 * per-game trace is opened only for a debug session or an actual error. */
static void garlic_diagnostics_begin(const char *gamedir)
{
    if (garlic_log_file) fclose(garlic_log_file);
    garlic_log_file = NULL; garlic_log_bytes = 0; garlic_log_path[0] = 0;
    const char *v = getenv("CPYMO_DEBUG");
    garlic_debug = v && !strcmp(v, "1");
    FILE *flag = garlic_open_save(gamedir, "rg35xx-debug.flag", "rb");
    if (flag) { garlic_debug = true; fclose(flag); }
    if (garlic_save_path(garlic_log_path, sizeof(garlic_log_path), gamedir,
            garlic_get_save_root(), "rg35xx.log") && garlic_debug) {
        garlic_make_save_dir(gamedir, garlic_get_save_root());
        garlic_log_file = fopen(garlic_log_path, "wb");
    }
}
static void garlic_log(enum retro_log_level level, const char *fmt, ...)
{
    if (!garlic_debug && level < RETRO_LOG_ERROR) return;
    char msg[2048]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    frontend_log(level, "%s", msg);
    if (!garlic_log_file && garlic_log_path[0] && garlic_log_bytes == 0) {
        garlic_log_file = fopen(garlic_log_path, "wb");
        if (!garlic_log_file) garlic_log_bytes = 262144;
    }
    if (garlic_log_file && garlic_log_bytes < 262144) {
        size_t n = strlen(msg), left = 262144 - garlic_log_bytes;
        if (n > left) n = left;
        garlic_log_bytes += fwrite(msg, 1, n, garlic_log_file);
        if (level >= RETRO_LOG_ERROR) fflush(garlic_log_file);
    }
}
void garlic_audio_trace(const char *event, const char *name,
    uint64_t output_frames, uint64_t source_cursor, long file_offset,
    const cpymo_engine *e)
{
    if (!garlic_debug || use_audio_callback) return;
    log_cb(RETRO_LOG_INFO,
        "BGM event=%s name=%s out48k=%llu source=%llu file=%ld script=%s line=%u\n",
        event, name ? name : "", (unsigned long long)output_frames,
        (unsigned long long)source_cursor, file_offset,
        e && e->interpreter ? e->interpreter->script->script_name : "-",
        e && e->interpreter ? (unsigned)e->interpreter->script_parser.cur_line : 0);
}

/* Linear gain after the existing mixer/resampler, identical for all channels.
 * A 0..100% gain cannot introduce clipping or alter game channel settings. */
#ifdef GARLIC_MP3_AUDIO
extern void garlic_audio_set_output_percent(unsigned);
#endif
static void garlic_output_gain(int16_t *samples, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        samples[i] = (int16_t)((int32_t)samples[i] * (int32_t)garlic_output_percent / 100);
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

static void audio_callback(void)
{
    static const int samples = 2048;
    static int16_t audio_buffer[2048 * 2];
    uint32_t begin = garlic_audio_observer_enabled ? (uint32_t)garlic_micros() : 0;
    bool bgm = garlic_audio_observer_enabled &&
        cpymo_audio_channel_is_playing(CPYMO_AUDIO_CHANNEL_BGM, &engine.audio);
#ifndef GARLIC_NO_FFMPEG
    /* This is the OTHER user of the mutex, and it is the one that runs on the
     * frontend's audio thread. Guard it the same way as the public lock:
     * when mtx_init did not happen (frontend refused the callback, or it
     * failed), locking here would be UB. */
    if (audio_mutex_initialized) mtx_lock(&audio_mutex);
#endif
    cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
#ifndef GARLIC_NO_FFMPEG
    if (audio_mutex_initialized) mtx_unlock(&audio_mutex);
#endif
#ifndef GARLIC_MP3_AUDIO
    garlic_output_gain(audio_buffer, samples * 2);
#endif
    uint32_t mixed = garlic_audio_observer_enabled ? (uint32_t)garlic_micros() : 0;
    size_t accepted = audio_batch_cb(audio_buffer, samples);
    if (garlic_audio_observer_enabled)
        garlic_audio_observe(audio_buffer, samples, bgm, begin, mixed,
            (uint32_t)garlic_micros(), accepted);
}

static void audio_set_state(bool enabled)
{
    audio_enabled = enabled;
    if (!enabled) __atomic_store_n(&garlic_audio_previous_active, 0, __ATOMIC_RELAXED);
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
        frontend_log = log.log;

    if (environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio)) {
        use_audio_callback = true;
#ifndef GARLIC_NO_FFMPEG
        if (!audio_mutex_initialized) {
            if (mtx_init(&audio_mutex, mtx_plain) == thrd_success)
                audio_mutex_initialized = true;
            else
                log_cb(RETRO_LOG_ERROR,
                       "audio mutex init failed; running unsynchronised\n");
        }
#endif
    }
}

static void garlic_write_audio_report(void)
{
    const char *path = getenv("CPYMO_AUDIO_REPORT");
    if (!path || !*path) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    uint32_t frames = __atomic_load_n(&garlic_capture_frames, __ATOMIC_ACQUIRE);
    bool saved = garlic_audio_write_capture(getenv("CPYMO_AUDIO_CAPTURE"), frames);
    fprintf(f, "core_audio=phase-headroom7-observer8\nfrontend_audio_callback=%s\noutput_rate=48000\noutput_percent=%u\nresampler_taps=16\nresampler_phase_endpoint=continuous\nlimiter=linked_peak_5ms_hold_80ms_release\ncallbacks=%u\nmax_mix_us=%u\nmax_submit_us=%u\nmax_bgm_callback_start_gap_us=%u\npartial_batches=%u\ncapture_frames=%u\ncapture_file_updated=%u\nhardware_listening=pending\n",
        use_audio_callback ? "accepted" : "rejected_sync_fallback", garlic_output_percent,
        __atomic_load_n(&garlic_audio_observed_calls, __ATOMIC_RELAXED),
        __atomic_load_n(&garlic_audio_max_mix_us, __ATOMIC_RELAXED),
        __atomic_load_n(&garlic_audio_max_submit_us, __ATOMIC_RELAXED),
        __atomic_load_n(&garlic_audio_max_start_gap_us, __ATOMIC_RELAXED),
        __atomic_load_n(&garlic_audio_partial_batches, __ATOMIC_RELAXED), frames, (unsigned)saved);
    fclose(f);
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
    info->library_version = "0.24.5";
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
    info->timing.sample_rate = 48000.0;
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
    garlic_audio_observer_configure();
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


static char garlic_loaded_font_path[PATH_MAX];
static off_t garlic_loaded_font_size;
static time_t garlic_loaded_font_mtime;
static error_t cpymo_backend_font_try_load_font(const char *path)
{
    struct stat st;
    bool stat_ok=stat(path,&st)==0;
    if(font.data && stat_ok && !strcmp(path,garlic_loaded_font_path) &&
        st.st_size==garlic_loaded_font_size && st.st_mtime==garlic_loaded_font_mtime)
        return CPYMO_ERR_SUCC;
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
    snprintf(garlic_loaded_font_path,sizeof(garlic_loaded_font_path),"%s",path);
    garlic_loaded_font_size=stat_ok ? st.st_size : -1;
    garlic_loaded_font_mtime=stat_ok ? st.st_mtime : 0;
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



/* Render-scale: rasterise the game this many times larger than the game's own
 * logical resolution and hand the frontend an exact 1:1 copy. At 1 the canvas
 * IS the game resolution, so 16px text is rasterised at 16px and the blit
 * doubles it afterwards; at 2 the same text is rasterised at 32px while the
 * logical coordinates, the image geometry and the line breaking stay exactly
 * as they are. The menu always renders at 1: it already draws at the output
 * size. CPYMO_RENDER_SCALE is read once per load and clamped to 1..2, so 1
 * keeps the historical, pixel-exact behaviour. */
static unsigned garlic_render_scale = 1;

/* The useful factor is the one that lands ON the output raster, not above it:
 * at 2 a 320x240 game becomes exactly 640x480 and the blit turns into a 1:1
 * copy, while a game that is already 640x480 (or larger, e.g. 800x600) would
 * have to be scaled back down again and keeps 1. */
static unsigned garlic_effective_render_scale(size_t w, size_t h)
{
    unsigned s = garlic_render_scale;
    while (s > 1 && (w * s > GARLIC_OUTPUT_WIDTH || h * s > GARLIC_OUTPUT_HEIGHT))
        --s;
    return s;
}

static void garlic_read_render_scale(void)
{
    const char *s = getenv("CPYMO_RENDER_SCALE");
    int v = s ? atoi(s) : 1;
    if (v < 1) v = 1;
    if (v > 2) v = 2;
    garlic_render_scale = (unsigned)v;
}

/* Make the render target exactly w*scale x h*scale while the LOGICAL screen
 * stays w x h, so every draw keeps its game-resolution coordinates and sizes
 * and only the rasterisation gets finer. Called for the menu (the OUTPUT size,
 * scale 1) and again for every chosen game (its OWN logical size). */
static bool garlic_apply_canvas_scaled(size_t w, size_t h, unsigned scale)
{
    size_t rw = w * scale, rh = h * scale;
    if (scale == 1 && (w > GARLIC_OUTPUT_WIDTH || h > GARLIC_OUTPUT_HEIGHT)) {
        garlic_viewport target;
        if (!garlic_compute_viewport(w,h,GARLIC_OUTPUT_WIDTH,GARLIC_OUTPUT_HEIGHT,&target)) return false;
        rw=target.w;rh=target.h;
    }
    if (w == 0 || h == 0 || rw == 0 || rh == 0 || rw > 2048 || rh > 2048)
        return false;

    if (soft_image.pixels && soft_image.w == rw && soft_image.h == rh) {
        soft_context.render_target = &soft_image;
        soft_context.logical_screen_w = (float)w;
        soft_context.logical_screen_h = (float)h;
        soft_context.font = &font;
        soft_context.scale_on_load_image=rw<w || rh<h;
        soft_context.scale_on_load_image_w_ratio=(float)rw/w;
        soft_context.scale_on_load_image_h_ratio=(float)rh/h;
        cpymo_backend_software_set_context(&soft_context);
        garlic_text_layer_configure();
        return true;
    }

    uint8_t *pixels = (uint8_t *)calloc(rh, rw * 4);
    if (!pixels) return false;
    if (soft_image.pixels) free(soft_image.pixels);
    soft_image.pixels = pixels;
    soft_image.w = rw;
    soft_image.h = rh;
    soft_image.line_stride = rw * 4;
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
    soft_context.scale_on_load_image = rw < w || rh < h;
    soft_context.scale_on_load_image_w_ratio = (float)rw/w;
    soft_context.scale_on_load_image_h_ratio = (float)rh/h;
    cpymo_backend_software_set_context(&soft_context);
    garlic_text_layer_configure();
    return true;
}

/* Point the text layer at this canvas: identity for an output-sized canvas,
 * otherwise the same viewport the blit will use. */
static void garlic_text_layer_configure(void)
{
    soft_context.text_layer = false;
    if (!garlic_text_layer)
        garlic_text_layer = (uint32_t *)calloc(
            (size_t)GARLIC_OUTPUT_WIDTH * GARLIC_OUTPUT_HEIGHT, sizeof(uint32_t));
    if (!garlic_text_layer) return;

    garlic_viewport v;
    if (!garlic_compute_viewport((int)soft_image.w, (int)soft_image.h,
            GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, &v))
        return;

    soft_context.text_layer = true;
    soft_context.text_layer_px = garlic_text_layer;
    soft_context.text_layer_w = GARLIC_OUTPUT_WIDTH;
    soft_context.text_layer_h = GARLIC_OUTPUT_HEIGHT;
    soft_context.text_scale = (float)v.h / soft_context.logical_screen_h;
    soft_context.text_off_x = v.x;
    soft_context.text_off_y = v.y;
    soft_context.text_dirty_y0 = GARLIC_OUTPUT_HEIGHT;
    soft_context.text_dirty_y1 = 0;
}

/* Straight alpha source-over, rounded /255 (the compiler reduces the constant
 * division to multiplies/shifts). Preserve both script colour and opacity. */
static void garlic_text_layer_composite(uint16_t *frame)
{
    static uint8_t e5[32], e6[64];
    static bool e5_ready = false;
    int y0 = soft_context.text_dirty_y0, y1 = soft_context.text_dirty_y1;
    if (!soft_context.text_layer || y1 <= y0) return;
    if (y0 < 0) y0 = 0;
    if (y1 > GARLIC_OUTPUT_HEIGHT) y1 = GARLIC_OUTPUT_HEIGHT;
    if (!e5_ready) {
        for (int i = 0; i < 32; ++i) e5[i] = (uint8_t)((i << 3) | (i >> 2));
        for (int i = 0; i < 64; ++i) e6[i] = (uint8_t)((i << 2) | (i >> 4));
        e5_ready = true;
    }

    for (int y = y0; y < y1; ++y) {
        uint32_t *src = garlic_text_layer + (size_t)y * GARLIC_OUTPUT_WIDTH;
        uint16_t *dst = frame + (size_t)y * GARLIC_OUTPUT_WIDTH;
        for (int x = 0; x < GARLIC_OUTPUT_WIDTH; ++x) {
            uint32_t px = src[x];
            uint32_t a = px >> 24;
            if (!a) continue;
            src[x] = 0;
            uint32_t k = 255u - a;
            uint16_t d = dst[x];
            uint32_t r = ((px & 0xff) * a + e5[(d >> 11) & 0x1f] * k + 127u) / 255u;
            uint32_t g = (((px >> 8) & 0xff) * a + e6[(d >> 5) & 0x3f] * k + 127u) / 255u;
            uint32_t b = (((px >> 16) & 0xff) * a + e5[d & 0x1f] * k + 127u) / 255u;
            dst[x] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
        }
    }
    soft_context.text_dirty_y0 = GARLIC_OUTPUT_HEIGHT;
    soft_context.text_dirty_y1 = 0;
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
    uint64_t font_begin=garlic_stage_begin();
    error_t err = cpymo_backend_font_init(gamedir, garlic_menu_root);
    garlic_stage_end(GARLIC_STAGE_FONT,font_begin);
    if (err != CPYMO_ERR_SUCC) {
        log_cb(RETRO_LOG_ERROR, "no font for %s: %s\n", gamedir,
               cpymo_error_message(err));
        return err;
    }
    unsigned render_scale = garlic_effective_render_scale(
        engine.gameconfig.imagesize_w, engine.gameconfig.imagesize_h);
    if (!garlic_apply_canvas_scaled(engine.gameconfig.imagesize_w,
                                    engine.gameconfig.imagesize_h,
                                    render_scale)) {
        log_cb(RETRO_LOG_ERROR, "invalid or excessive game canvas size: %ux%u\n",
               (unsigned)engine.gameconfig.imagesize_w,
               (unsigned)engine.gameconfig.imagesize_h);
        return CPYMO_ERR_OUT_OF_MEM;
    }
    snprintf(garlic_selected_game, sizeof(garlic_selected_game), "%s", gamedir);
    garlic_screen = GARLIC_SCREEN_GAME;
    garlic_first_frame = true;
    log_cb(RETRO_LOG_INFO,"CPYMO_RENDER logical=%ux%u target=%zux%zu preload_scale=%u text_scale=%.6f\n",
        engine.gameconfig.imagesize_w,engine.gameconfig.imagesize_h,soft_image.w,soft_image.h,
        (unsigned)soft_context.scale_on_load_image,soft_context.text_scale);
    log_cb(RETRO_LOG_INFO, "CPYMO_SELECT game=%s canvas=%ux%u\n", gamedir,
           (unsigned)engine.gameconfig.imagesize_w,
           (unsigned)engine.gameconfig.imagesize_h);
    return CPYMO_ERR_SUCC;
}

static error_t garlic_selector_before(cpymo_engine *e, const char *gamedir)
{
    (void)e;
    garlic_diagnostics_begin(gamedir);
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
    uint64_t scan_begin=garlic_stage_begin();
    if (!garlic_scan_game_dirs(games_root, dirs, 64, &count)) {
        for (size_t i = 0; i < count; ++i) free(dirs[i]);
        log_cb(RETRO_LOG_INFO, "no game found under %s\n", games_root);
        return false;
    }

    garlic_stage_end(GARLIC_STAGE_SCAN,scan_begin);
    cpymo_game_selector_item *items = NULL, *tail = NULL;
    for (size_t i = 0; i < count; ++i) {
        cpymo_game_selector_item *item = NULL;
        char *path = dirs[i];       /* ownership moves into the item */
        if (cpymo_game_selector_item_create(&item, &path) != CPYMO_ERR_SUCC) {
            free(path);
            for (size_t j = i + 1; j < count; ++j) free(dirs[j]);
            cpymo_game_selector_item_free_all(items);
            return false;
        }
        item->prev = tail;
        if (tail) tail->next = item; else items = item;
        tail = item;
    }

    if (!garlic_apply_canvas_scaled(GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, 1)) {
        cpymo_game_selector_item_free_all(items);
        return false;
    }
    /* The menu needs a font OF ITS OWN, before any game has been chosen:
     * <games_root>/system/default.ttf is a candidate, and an explicit
     * CPYMO_MENU_FONT outranks it. */
    if (cpymo_backend_font_init(games_root,
            initial_game && initial_game[0] ? initial_game : (items ? items->gamedir : NULL)) != CPYMO_ERR_SUCC) {
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
        GARLIC_MENU_FONTSIZE, (float)GARLIC_MENU_FONTSIZE,
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
    float footer_width;
    cpymo_backend_text_create(&garlic_library_footer, &footer_width,
        cpymo_str_pure("CPyMO 0.24.5   A：启动   ↑↓：选择   B：退出"), 19);
    garlic_prof_reset();
    {
        uint64_t now = garlic_micros();
        garlic_load_report(games_root, CPYMO_ERR_SUCC, now - tick, 0, 0);
        log_cb(RETRO_LOG_INFO, "CPYMO_MENU root=%s games=%zu\n", games_root,
               count);
    }
    garlic_stage_report("menu_ready");
    return true;
}

static void garlic_toast(const char *text);

/* Called only after cpymo_engine_update returns: every active UI, decoder,
   image and glyph is released with its original context and save directory. */
static bool garlic_return_to_library(error_t cause)
{
    char root[PATH_MAX], selected[PATH_MAX];
    snprintf(root, sizeof(root), "%s", garlic_menu_root);
    snprintf(selected, sizeof(selected), "%s", garlic_selected_game);
    retro_unload_game();
    memset(&engine, 0, sizeof(engine));
    memset(&input, 0, sizeof(input));
    garlic_pending_game[0] = 0;
    garlic_library_requested = false;
    garlic_abort_error = CPYMO_ERR_SUCC;
    garlic_drain_buttons = true;
    if (!getcwd(garlic_previous_directory, sizeof(garlic_previous_directory)))
        return false;
    snprintf(garlic_menu_root, sizeof(garlic_menu_root), "%s", root);
    if (!root[0] || !garlic_open_menu(root, selected)) return false;
    if (cause != CPYMO_ERR_SUCC && cause != CPYMO_ERR_NO_MORE_CONTENT) {
        char message[180];
        snprintf(message, sizeof(message), "游戏启动或运行失败：%s", cpymo_error_message(cause));
        garlic_toast(message);
        garlic_toast_left = 4.0f;
    }
    log_cb(RETRO_LOG_INFO, "CPYMO_RETURN_LIBRARY reason=%d\n", (int)cause);
    return true;
}

static error_t garlic_start_queued_game(void)
{
    uint64_t begin = garlic_micros();
    char gamedir[PATH_MAX];
    snprintf(gamedir, sizeof(gamedir), "%s", garlic_pending_game);
    garlic_pending_game[0] = 0;
    garlic_stage_reset();uint64_t cleanup_begin=garlic_stage_begin();
    if (garlic_engine_ready) cpymo_engine_free(&engine);
    garlic_image_cache_clear();
    garlic_stage_end(GARLIC_STAGE_CLEANUP,cleanup_begin);
    garlic_engine_ready = false;
    garlic_toast_clear();
    memset(&engine, 0, sizeof(engine));
    memset(&input, 0, sizeof(input));
    garlic_drain_buttons = true;
    if (chdir(gamedir) != 0) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    garlic_directory_changed = true;
    garlic_make_save_dir(gamedir, garlic_get_save_root());
    garlic_diagnostics_begin(gamedir);
    snprintf(garlic_selected_game, sizeof(garlic_selected_game), "%s", gamedir);
    uint64_t engine_begin = garlic_micros();
    error_t err = cpymo_engine_init(&engine, gamedir);
    uint64_t engine_end = garlic_micros();
    if (err != CPYMO_ERR_SUCC) {
        cpymo_audio_free(&engine.audio);
        return err;
    }
    garlic_engine_ready = true;
    err = garlic_after_game_selected(gamedir);
    log_cb(RETRO_LOG_INFO, "CPYMO_SWITCH game=%s result=%d total_us=%llu engine_us=%llu backend_us=%llu\n",
        gamedir, (int)err, (unsigned long long)(garlic_micros() - begin),
        (unsigned long long)(engine_end - engine_begin),
        (unsigned long long)(garlic_micros() - engine_end));
    if (err == CPYMO_ERR_SUCC) {
        /* The output RGB565 buffer still contains the complete library,
           including its composited text. Build the game on a clean canvas
           offscreen, and publish only after it submits its first draw. */
        memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);
        if (garlic_text_layer) memset(garlic_text_layer,0,
            (size_t)GARLIC_OUTPUT_WIDTH*GARLIC_OUTPUT_HEIGHT*sizeof(uint32_t));
        garlic_waiting_for_game_frame=true;
        garlic_prof_reset();
    }
    return err;
}

bool retro_load_game(const struct retro_game_info *game)
{
    const char *dbg = getenv("CPYMO_DEBUG");
    garlic_debug = dbg && !strcmp(dbg, "1");
    garlic_stage_enable(garlic_debug);garlic_stage_reset();
    log_cb(RETRO_LOG_INFO, "CPYMO_CORE version=0.24.5 font_steps=72/74 movie=disabled audio=phase-headroom7 ui=centered-layout12\n");
    const char *pct = getenv("CPYMO_OUTPUT_PERCENT");
    char *pct_end = NULL;
    long pct_value = pct ? strtol(pct, &pct_end, 10) : 50;
    garlic_output_percent = pct && (pct_end == pct || *pct_end || pct_value < 0 || pct_value > 100)
        ? 50 : (unsigned)pct_value;
#ifdef GARLIC_MP3_AUDIO
    garlic_audio_set_output_percent(garlic_output_percent);
#endif
    garlic_write_audio_report();
    garlic_prev_buttons = 0;
    garlic_save_confirm=(garlic_confirm_key){0};garlic_load_confirm=(garlic_confirm_key){0};
    garlic_drain_buttons = false;
    error_t err;
    if (garlic_game_loaded || garlic_engine_ready)
        retro_unload_game();
    garlic_pending_game[0] = 0;
    garlic_library_requested = garlic_exit_requested = false;
    garlic_abort_error = CPYMO_ERR_SUCC;
    garlic_menu_root[0] = 0;
    garlic_selected_game[0] = 0;
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
    /* Even explicit single-game content starts at the library by default.
       CPYMO_MENU=0 remains an explicit diagnostic/direct-load override. */
    const char *menu = getenv(GARLIC_MENU_ENV);
    if (!menu || (menu[0] != '0' && menu[0] != 'n' && menu[0] != 'N')) {
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", gamedir);
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent) *slash = 0;
        else strcpy(parent, "/");
        if (garlic_games_dir(getenv(GARLIC_GAMES_DIR_ENV), parent, GARLIC_GAMES_DIR,
                garlic_menu_root, sizeof(garlic_menu_root)) &&
            garlic_open_menu(garlic_menu_root, gamedir)) return true;
        log_cb(RETRO_LOG_ERROR, "could not open game library\n");
        retro_unload_game();
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

    garlic_diagnostics_begin(gamedir);
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
    unsigned render_scale = garlic_effective_render_scale(
        engine.gameconfig.imagesize_w, engine.gameconfig.imagesize_h);
    if (!garlic_apply_canvas_scaled(engine.gameconfig.imagesize_w,
                                    engine.gameconfig.imagesize_h,
                                    render_scale)) {
        log_cb(RETRO_LOG_ERROR, "invalid or excessive game canvas size\n");
        goto fail;
    }
    garlic_first_frame = true;
    garlic_game_loaded = true;
    garlic_screen = GARLIC_SCREEN_GAME;
    snprintf(garlic_selected_game, sizeof(garlic_selected_game), "%s", gamedir);
    garlic_prof_reset();
    if (getenv("CPYMO_START_CONFIG")) cpymo_config_ui_enter(&engine);
    log_cb(RETRO_LOG_INFO,
           "CPYMO_SELECT game=%s canvas=%ux%u render_scale=%u (direct)\n", gamedir,
           (unsigned)engine.gameconfig.imagesize_w,
           (unsigned)engine.gameconfig.imagesize_h, render_scale);
    garlic_load_report(gamedir, CPYMO_ERR_SUCC, tick_font - tick_engine,
                      tick_font_done - tick_font,
                      garlic_micros() - tick_font_done);
    return true;

fail:
    retro_unload_game();
    return false;
}

static void garlic_toast_clear(void)
{
    if (garlic_toast_text) cpymo_backend_text_free(garlic_toast_text);
    garlic_toast_text = NULL;
    garlic_toast_left = 0;
}

static void garlic_toast(const char *text)
{
    garlic_toast_clear();
    if (!text) return;
    /* 25 physical pixels across all supported game canvases. */
    float h = 25.0f / soft_context.text_scale;
    float w = 0;
    if (cpymo_backend_text_create(&garlic_toast_text, &w,
        cpymo_str_pure(text), h) != CPYMO_ERR_SUCC) {
        garlic_toast_text = NULL;
        return;
    }
    garlic_toast_w = w;
    float top,bottom;garlic_text_ink_bounds(garlic_toast_text,&top,&bottom);
    garlic_toast_h = bottom-top;
    garlic_toast_left = 1.6f;
    cpymo_engine_request_redraw(&engine);
}

static void garlic_toast_draw(void)
{
    if (!garlic_toast_text || garlic_toast_left <= 0) return;
    float w = garlic_toast_w, h = garlic_toast_h;
    float cw = (float)engine.gameconfig.imagesize_w;
    float ch = (float)engine.gameconfig.imagesize_h;
    float x = cw * 0.04f, y = ch * 0.035f;
    float pad = 8.0f/soft_context.text_scale;
    float box[] = { x - pad, y - pad, w + pad * 2, h + pad * 2 };
    float alpha = garlic_toast_left < 0.3f ? garlic_toast_left / 0.3f : 1.0f;
    cpymo_backend_image_fill_rects(box, 1, cpymo_color_black, 0.72f * alpha,
        cpymo_backend_image_draw_type_ui_element_bg);
    cpymo_backend_text_draw(garlic_toast_text, x, garlic_text_centered_baseline(garlic_toast_text,y+h/2), cpymo_color_white,
        alpha, cpymo_backend_image_draw_type_ui_element);
}

/* One press, one action; no slot writes while choices/UI/hidden text are active. */
static void garlic_oneshot(const garlic_actions *a)
{
    if (cpymo_ui_enabled(&engine) || !engine.say.active ||
        engine.select_img.selections || engine.say.hide_window) return;
    if (a->quick_save) {
        error_t e = cpymo_save_write(&engine, 1);
        garlic_toast(e == CPYMO_ERR_SUCC ? "已快速保存到存档 1"
                                         : "快速保存失败");
        log_cb(RETRO_LOG_INFO, "quick save slot 1: %d\n", (int)e);
    } else if (a->quick_load) {
        error_t e = CPYMO_ERR_CAN_NOT_OPEN_FILE;
        FILE *f = cpymo_save_open_read(&engine, 1);
        if (f) {
            e = cpymo_save_load_savedata(&engine, f);
            fclose(f);
        }
        garlic_toast(e == CPYMO_ERR_SUCC ? "已快速读取存档 1"
                                         : "快速读取失败或存档不存在");
        log_cb(RETRO_LOG_INFO, "quick load slot 1: %d\n", (int)e);
    } else if (a->settings) {
        cpymo_config_ui_enter(&engine);
    } else if (a->auto_play) {
        engine.say.auto_mode = !engine.say.auto_mode;
        engine.say.auto_mode_timer = -1;
        garlic_toast(engine.say.auto_mode ? "自动播放 开" : "自动播放 关");
    }
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
    if (JOY(L2))     b |= GARLIC_BUTTON_L2;
    if (JOY(R2))     b |= GARLIC_BUTTON_R2;
    if (JOY(START))  b |= GARLIC_BUTTON_START;
    if (JOY(SELECT)) b |= GARLIC_BUTTON_SELECT;
#undef JOY
    bool picking = engine.select_img.selections != NULL;
    bool script_menu = picking && !engine.select_img.save_enabled;
    bool pick_a_slot = picking && engine.select_img.save_enabled;
    garlic_actions a = garlic_map_buttons(b, cpymo_ui_enabled(&engine),
        script_menu, pick_a_slot);
    uint32_t pressed = b & ~garlic_prev_buttons;
    if (garlic_screen == GARLIC_SCREEN_MENU && !garlic_drain_buttons &&
        (pressed & GARLIC_BUTTON_B))
        garlic_exit_requested = true;
    bool story_saveable=garlic_screen==GARLIC_SCREEN_GAME && engine.say.active &&
        !cpymo_ui_enabled(&engine) && !picking && !engine.say.hide_window && !a.hide_window;
    uint64_t confirm_now=garlic_micros();
    a.quick_save=garlic_confirm_update(&garlic_save_confirm,(b&GARLIC_BUTTON_R2)!=0,
        story_saveable && (b==0 || b==GARLIC_BUTTON_R2),confirm_now);
    a.quick_load=garlic_confirm_update(&garlic_load_confirm,(b&GARLIC_BUTTON_L2)!=0,
        story_saveable && (b==0 || b==GARLIC_BUTTON_L2),confirm_now);
    bool quick_confirmed=a.quick_save || a.quick_load;
    a.auto_play = a.auto_play && (pressed & GARLIC_BUTTON_Y);
    a.settings = a.settings && (pressed & GARLIC_BUTTON_SELECT);
    /* Opposing shoulders/chords do nothing. Other held buttons cannot turn a
     * save/load press into an advance, menu-open, or double operation. */
    uint32_t action = b & (GARLIC_BUTTON_L2 | GARLIC_BUTTON_R2 |
                          GARLIC_BUTTON_Y | GARLIC_BUTTON_SELECT);
    if (action && (action & (action - 1)))
        a.quick_save = a.quick_load = a.auto_play = a.settings = false;
    garlic_prev_buttons = b;
    if (!garlic_drain_buttons || quick_confirmed) garlic_oneshot(&a);
    if (action) garlic_drain_buttons = true;
    if (!b) garlic_drain_buttons = false;
    if (garlic_drain_buttons) {
        a.ok = a.cancel = a.skip = a.hide_window = false;
        a.up = a.down = a.left = a.right = false;
    }
#ifdef GARLIC_UI_DEBUG
    if (polls_dbg++ % 20 == 0)
        fprintf(stderr, "DBG poll ui=%d choice=%d say=%d cancel=%d ok=%d\n",
            (int)cpymo_ui_enabled(&engine), (int)script_menu,
            (int)engine.say.active, (int)input.cancel, (int)input.ok);
#endif
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
    /* Consumed chords must not emit a phantom forward/cancel release. */
    if (garlic_drain_buttons) engine.input = input;
}

void retro_run(void)
{
    if (!garlic_game_loaded) return;
    static const int samples = 48000 / 60;
    static int16_t audio_buffer[48000 / 60 * 2];
    bool redraw = false;

    input_poll_cb();
    input_update();

    if (garlic_exit_requested) {
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
        return;
    }

    uint64_t tick = garlic_micros();
    error_t err = cpymo_engine_update(&engine, delta, &redraw);
    if (garlic_abort_error != CPYMO_ERR_SUCC) {
        err = garlic_abort_error;
        garlic_abort_error = CPYMO_ERR_SUCC;
    }
    uint64_t after_update = garlic_micros();
    garlic_prof_update_us += after_update - tick;
    if (err != CPYMO_ERR_SUCC) {
        if (err != CPYMO_ERR_NO_MORE_CONTENT)
            log_cb(RETRO_LOG_ERROR, "engine update failed: %s\n", cpymo_error_message(err));
#ifdef GARLIC_UI_DEBUG
        fprintf(stderr, "DBG err=%d\n", (int)err);
#endif
    }
    if (garlic_pending_game[0]) {
        err = garlic_start_queued_game();
        redraw = true;
    }
    if (garlic_library_requested || err != CPYMO_ERR_SUCC) {
        if (!garlic_return_to_library(err)) {
            log_cb(RETRO_LOG_ERROR, "cannot recover game library\n");
            environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
            return;
        }
        redraw = true;
    }

    if (cpymo_ui_enabled(&engine) && garlic_screen == GARLIC_SCREEN_GAME)
        garlic_toast_clear();
    if (garlic_toast_left > 0) {
        redraw = true;
        garlic_toast_left -= delta;
        if (garlic_toast_left <= 0) garlic_toast_clear();
    }
    if (redraw || garlic_first_frame) {
        garlic_prof_draws++;
        tick = garlic_micros();
        /* A selector has no game background to repaint underneath its alpha
           overlays. Rebuild it from black on every dirty frame. */
        if (garlic_screen == GARLIC_SCREEN_MENU)
            memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);
        soft_context.frame_painted=false;
        cpymo_engine_draw(&engine);
        if (garlic_screen == GARLIC_SCREEN_MENU && garlic_library_footer)
            cpymo_backend_text_draw(garlic_library_footer, 16, 444,
                cpymo_color_white, 1.0f, cpymo_backend_image_draw_type_ui_element);
        garlic_toast_draw();
        uint64_t after_draw = garlic_micros();
        garlic_prof_draw_us += after_draw - tick;
        if (!garlic_waiting_for_game_frame || soft_context.frame_painted) {
            if (garlic_waiting_for_game_frame) {
                garlic_waiting_for_game_frame=false;
                log_cb(RETRO_LOG_INFO,"CPYMO_TRANSITION first_game_frame ready=1\n");
            }
            if (!garlic_blit_rgb565_quality(garlic_rgb565_frame, GARLIC_OUTPUT_WIDTH * 2,
                GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, soft_image.pixels,
                soft_image.line_stride, soft_image.w, soft_image.h)) {
                log_cb(RETRO_LOG_ERROR, "GarlicOS: frame conversion failed\n");
                environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
                return;
            }
            garlic_text_layer_composite(garlic_rgb565_frame);
            garlic_prof_convert_us += garlic_micros() - after_draw;
            garlic_first_frame = false;
        }
    }
    garlic_prof_frames++;
    if (garlic_prof_frames >= 180) garlic_prof_report();

    tick = garlic_micros();
    video_cb(garlic_rgb565_frame, GARLIC_OUTPUT_WIDTH, GARLIC_OUTPUT_HEIGHT, GARLIC_OUTPUT_WIDTH * 2);
    garlic_prof_video_us += garlic_micros() - tick;

#ifdef GARLIC_UI_DEBUG
    if (garlic_prof_frames % 1 == 0) fprintf(stderr, "DBG run end\n");
#endif
    tick = garlic_micros();
    bool movie_audio = false;
#if defined(GARLIC_MP3_AUDIO) && defined(GARLIC_EXPERIMENTAL_MOVIE)
    movie_audio = garlic_movie_audio(audio_buffer, samples);
#endif
    if (audio_enabled && !use_audio_callback) {
        uint32_t observed_begin = garlic_audio_observer_enabled ? (uint32_t)garlic_micros() : 0;
        bool bgm = garlic_audio_observer_enabled && cpymo_audio_channel_is_playing(CPYMO_AUDIO_CHANNEL_BGM, &engine.audio);
        if (!movie_audio)
            cpymo_audio_copy_mixed_samples(audio_buffer, samples * 4, &engine.audio);
    #ifndef GARLIC_MP3_AUDIO
        garlic_output_gain(audio_buffer, samples * 2);
#else
        if (movie_audio) garlic_output_gain(audio_buffer, samples * 2);
#endif
        uint32_t mixed = garlic_audio_observer_enabled ? (uint32_t)garlic_micros() : 0;
        size_t accepted = audio_batch_cb(audio_buffer, samples);
        if (garlic_audio_observer_enabled)
            garlic_audio_observe(audio_buffer, samples, bgm, observed_begin, mixed,
                (uint32_t)garlic_micros(), accepted);
    }
    garlic_prof_audio_us += garlic_micros() - tick;
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
    garlic_stage_report("unload");
    /* Engine release can call UI and save logic; keep the framebuffer context
       and the correct game directory alive until it has completed. */
    if (garlic_engine_ready) {
        cpymo_engine_free(&engine);
        garlic_engine_ready = false;
    }
    size_t cache_bytes; unsigned cache_hits,cache_misses;
    garlic_image_cache_counts(&cache_bytes,&cache_hits,&cache_misses);
    log_cb(RETRO_LOG_INFO,"CPYMO_CACHE bytes=%zu hits=%u misses=%u\n",cache_bytes,cache_hits,cache_misses);
    garlic_image_cache_clear();
    garlic_toast_clear();
    if (garlic_library_footer) cpymo_backend_text_free(garlic_library_footer);
    garlic_library_footer = NULL;
    free(garlic_text_layer); garlic_text_layer = NULL;
    if (garlic_log_file) fclose(garlic_log_file);
    garlic_log_file = NULL; garlic_log_path[0] = 0;
    cpymo_backend_software_set_context(NULL);
    free(garlic_rgb565_frame);
    garlic_rgb565_frame = NULL;
    garlic_first_frame = false;
    garlic_waiting_for_game_frame = false;
    garlic_game_loaded = false;
    garlic_screen = GARLIC_SCREEN_NONE;
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
    garlic_write_audio_report();
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
