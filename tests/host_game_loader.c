/* Host-side acceptance harness for v0.23 task items §1 and §2.
 *
 *   §1  "通用游戏路径加载": one core must load ANY game the launcher names --
 *       the game directory, or a file inside it.
 *   §2  "兼容性验证": the game's OWN logical aspect must survive. The output
 *       is always the 640x480 RGB565 frame, so the observable signal is the
 *       letterbox: a 4:3 game fills it, a 3:2 game (540x360) must leave the
 *       black bars that contain-fit produces. A missing gameconfig.txt, a
 *       case-different one, and a missing directory are the negative paths.
 *
 * The core is dlopen'd exactly as RetroArch does, with a minimal frontend that
 * answers the same environment calls and keeps the newest RGB565 frame.
 *
 *   host_game_loader <core> "expect|want_rgb|topbar|sublog|path" [case...]
 *     expect    ok | fail
 *     want_rgb  "r,g,b" mean of the whole frame (8-bit)
 *     topbar    1 = the top rows must be black (letterboxed), 0 = filled,
 *               "-" = do not care
 *     sublog    required log substring (the case diagnostic), "-" for none
 *     path      game directory, or a file inside it
 */
#define _POSIX_C_SOURCE 200809L
#include <libretro.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define OUTPUT_W 640u
#define OUTPUT_H 480u
/* The fixture colour must be present on the CANVAS; a fade-in is transient,
 * so the check is on the best frame seen, not on a single one. */
#define SETTLE_FRAMES 400u
#define TOP_ROWS 20u
#define COLOUR_TOL 100L      /* squared per-channel distance, i.e. 10 each */

static unsigned frames;
static unsigned last_w, last_h;
static unsigned char *frame;      /* newest RGB565 frame, as delivered    */
static size_t frame_bytes;
static char log_buf[65536];
static size_t log_used;
static bool shutdown_requested;

static unsigned want_r, want_g, want_b;

static void logger(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(log_buf + log_used, sizeof(log_buf) - log_used, fmt, ap);
    va_end(ap);
    if (n > 0) log_used += (size_t)n;
    if (log_used >= sizeof(log_buf)) log_used = sizeof(log_buf) - 1;
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = logger;
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
        return false;
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return false;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = "/nonexistent";
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        shutdown_requested = true;
        return true;
    default:
        return false;
    }
}

/* Mean of a horizontal band, and of the whole frame, in the frame's own
 * RGB565 field widths: r is 5 bits, g 6, b 5. The case specs therefore use
 * e.g. (40,88,200) -> 5,22,25. `top` gets the first TOP_ROWS scanlines:
 * (0,0,0) means they are still pure black, i.e. the frame is letterboxed. */
static void frame_stats(unsigned *whole_r, unsigned *whole_g, unsigned *whole_b,
                      unsigned long *painted, unsigned *top_r, unsigned *top_g,
                      unsigned *top_b)
{
    unsigned long rs = 0, gs = 0, bs = 0, nz = 0;
    unsigned long trs = 0, tgs = 0, tbs = 0;

    if (!frame || last_w == 0 || last_h == 0) {
        *whole_r = *whole_g = *whole_b = 0; *painted = 0;
        *top_r = *top_g = *top_b = 0;
        return;
    }
    size_t pitch = (size_t)last_w * 2;
    for (unsigned y = 0; y < last_h; ++y) {
        for (unsigned x = 0; x < last_w; ++x) {
            uint16_t p;
            memcpy(&p, frame + y * pitch + x * 2, 2);
            unsigned rr = (p >> 11) & 31u, gg = (p >> 5) & 63u, bb = p & 31u;
            rs += rr; gs += gg; bs += bb;
            if (p) ++nz;
            if (y < TOP_ROWS) { trs += rr; tgs += gg; tbs += bb; }
        }
    }
    unsigned long npx = (unsigned long)last_w * last_h;
    unsigned long tpx = (unsigned long)last_w * (last_h < TOP_ROWS ? last_h
                                                                   : TOP_ROWS);
    *whole_r = (unsigned)(rs / npx);
    *whole_g = (unsigned)(gs / npx);
    *whole_b = (unsigned)(bs / npx);
    *painted = nz;
    *top_r = tpx ? (unsigned)(trs / tpx) : 0;
    *top_g = tpx ? (unsigned)(tgs / tpx) : 0;
    *top_b = tpx ? (unsigned)(tbs / tpx) : 0;
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w != OUTPUT_W || h != OUTPUT_H) return;
    size_t need = (size_t)h * pitch;
    if (frame_bytes < need) {
        unsigned char *p = (unsigned char *)realloc(frame, need);
        if (!p) return;
        frame = p;
        frame_bytes = need;
    }
    memcpy(frame, data, need);
    last_w = w;
    last_h = h;
    ++frames;
}

static size_t audio_batch(const int16_t *b, size_t n) { (void)b; return n; }
static void input_poll(void) {}
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

#define SYM(TYPE, NAME) do { \
    *(void **)(&NAME) = dlsym(h, "retro_" #NAME); \
    if (!NAME) { fprintf(stderr, "missing symbol retro_%s\n", #NAME); return 6; } \
    (void)sizeof(TYPE); \
} while (0)

/* Has the fixture's own background appeared in the current frame? Used to stop
 * waiting early once a game is visibly running, rather than always burning the
 * full settle budget. */
static bool fixture_visible(void)
{
    unsigned r, g, b, tr, tg, tb;
    unsigned long painted;
    frame_stats(&r, &g, &b, &painted, &tr, &tg, &tb);
    long dr = (long)r - (long)want_r, dg = (long)g - (long)want_g,
         db = (long)b - (long)want_b;
    return painted > 1000 && (dr * dr + dg * dg + db * db) <= 400;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <core> <case> [case...]\n", argv[0]);
        return 2;
    }
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 3; }

    unsigned (*api_version)(void) = NULL;
    void (*set_environment)(retro_environment_t) = NULL;
    void (*set_video_refresh)(retro_video_refresh_t) = NULL;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
    void (*set_input_poll)(retro_input_poll_t) = NULL;
    void (*set_input_state)(retro_input_state_t) = NULL;
    void (*init)(void) = NULL, (*deinit)(void) = NULL;
    bool (*load_game)(const struct retro_game_info *) = NULL;
    void (*run)(void) = NULL, (*unload_game)(void) = NULL;
    SYM(unsigned, api_version);
    SYM(void, set_environment);
    SYM(void, set_video_refresh);
    SYM(void, set_audio_sample_batch);
    SYM(void, set_input_poll);
    SYM(void, set_input_state);
    SYM(void, init);
    SYM(void, deinit);
    SYM(bool, load_game);
    SYM(void, run);
    SYM(void, unload_game);

    set_environment(environment);
    set_video_refresh(video);
    set_audio_sample_batch(audio_batch);
    set_input_poll(input_poll);
    set_input_state(input_state);
    init();
    if (api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "libretro API version mismatch\n");
        return 5;
    }

    int failures = 0;
    for (int i = 2; i < argc; ++i) {
        char spec[4096];
        snprintf(spec, sizeof(spec), "%s", argv[i]);
        char *save = NULL;
        const char *expect = strtok_r(spec, "|", &save);
        const char *rgb = strtok_r(NULL, "|", &save);
        const char *topbar = strtok_r(NULL, "|", &save);
        const char *sublog = strtok_r(NULL, "|", &save);
        const char *path = strtok_r(NULL, "|", &save);
        if (!expect || !rgb || !topbar || !sublog || !path) {
            fprintf(stderr, "bad case spec: %s\n", argv[i]);
            ++failures;
            continue;
        }
        want_r = want_g = want_b = 0;
        sscanf(rgb, "%u,%u,%u", &want_r, &want_g, &want_b);
        bool want_ok = (strcmp(expect, "ok") == 0);
        int want_bar = (topbar[0] == '-') ? -1 : atoi(topbar);
        const char *need_log = (sublog[0] == '-') ? NULL : sublog;

        frames = 0;
        last_w = last_h = 0;
        shutdown_requested = false;
        log_buf[0] = '\0';
        log_used = 0;

        struct retro_game_info game = { path, NULL, 0, NULL };
        bool ok = load_game(&game);
        if (ok != want_ok) {
            ++failures;
            printf("  FAIL  load %s -> %s (wanted %s)\n", path,
                   ok ? "success" : "failure", expect);
            if (!ok) printf("        log: %s\n", log_buf);
            continue;
        }
        if (!ok) {
            bool diag = (need_log == NULL) || strstr(log_buf, need_log) != NULL;
            printf("  %s  rejected as expected%s\n", diag ? "ok  " : "FAIL",
                   need_log ? (diag ? " (diagnostic present)"
                                    : " (but the diagnostic is missing)") : "");
            if (!diag) {
                printf("        wanted: %s\n", need_log);
                printf("        log: %s\n", log_buf);
                ++failures;
            }
            unload_game();
            continue;
        }

        /* Run until the fixture's colour is on screen, or out of budget.
         * SETTLE_FRAMES is generous because a REAL game may need many frames
         * of loading and fades before its background is visible; the loop
         * exits as soon as the colour appears, so the budget is not a cost. */
        unsigned settle = 0;
        for (; settle < SETTLE_FRAMES && !shutdown_requested; ++settle) {
            run();
            if (frames > 2 && fixture_visible()) break;
        }

        unsigned r, g, b, tr, tg, tb;
        unsigned long painted;
        frame_stats(&r, &g, &b, &painted, &tr, &tg, &tb);

        bool geom_ok = (last_w == OUTPUT_W && last_h == OUTPUT_H);
        long dr = (long)r - (long)want_r, dg = (long)g - (long)want_g,
             db = (long)b - (long)want_b;
        bool colour_ok = (dr * dr + dg * dg + db * db) <= 400;
        bool painted_ok = painted > 1000;
        bool bar_ok = true;
        if (want_bar == 1) bar_ok = (tr + tg + tb) <= 6;
        if (want_bar == 0) bar_ok = (tr + tg + tb) > 6;
        bool pass = geom_ok && colour_ok && painted_ok && bar_ok;

        printf("  %s  %-9s out=%ux%u mean=(%u,%u,%u) top=(%u,%u,%u) "
               "painted=%lu settle=%u\n", pass ? "ok  " : "FAIL",
               want_ok ? "accept" : "reject", last_w, last_h, r, g, b,
               tr, tg, tb, painted, settle);
        if (!geom_ok) {
            printf("        wanted the %ux%u RGB565 output\n", OUTPUT_W, OUTPUT_H);
            ++failures;
        }
        if (!colour_ok) {
            printf("        wanted the fixture's background, mean ~(%u,%u,%u)\n",
                   want_r, want_g, want_b);
            ++failures;
        }
        if (!painted_ok) {
            printf("        the canvas was never painted\n");
            ++failures;
        }
        if (!bar_ok) {
            printf("        letterbox expectation failed (top=%u,%u,%u,"
                   " want_bar=%d)\n", tr, tg, tb, want_bar);
            ++failures;
        }
        printf("        log: %s", log_buf);
        unload_game();
    }

    unload_game();
    deinit();
    dlclose(h);
    free(frame);
    return failures ? 1 : 0;
}
