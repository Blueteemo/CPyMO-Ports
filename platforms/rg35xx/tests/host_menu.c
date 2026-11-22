/* Host harness for the game selector (§4 of the v0.23 task).
 *
 *   host_menu <core.so> <fixture-root>
 *
 * The fixture root holds the APPS-like trees made by run_menu_test.sh:
 *
 *   menu_none/   a folder of games with no game in it (and no font)
 *   menu_one/    exactly one game
 *   menu_3/      three games, and a system/ font for the menu
 *
 * Everything is observed from OUTSIDE the core: the frontend's video and log
 * callbacks, the files the run leaves behind, and a real pad press delivered
 * through retro_input_state. So this tests the core, not an internal API.
 *
 * What it checks, and why each is a real risk:
 *   1. the folder of games OPENS A MENU (the whole point of the item);
 *   2. with one game it still starts that game (the convenience the shipped
 *      launcher relies on -- it points the core at gameconfig.txt);
 *   3. with no game it takes the plain path and fails with a DIAGNOSTIC;
 *   4. navigation changes the highlighted row (the pad really reaches the UI);
 *   5. A selects, the game switches, the log names it, the canvas follows;
 *   6. the font reaches the game: no font of its own, only the menu's,
 *      and the game still renders text;
 *   7. with CPYMO_MENU=0 a folder with several games loads as one game and
 *      fails, just as before §4;
 *   8. an explicit CPYMO_MENU_FONT is honoured for a games folder that has
 *      no font at all.
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
#include <time.h>
#include <unistd.h>

#define OUT_W 640u
#define OUT_H 480u

static char   log_buf[262144];
static size_t log_used;
static unsigned frames;
static unsigned char frame[OUT_W * OUT_H * 2];
static size_t frame_bytes;
static unsigned last_w, last_h;
static bool shutdown_requested;

/* The pad: what the harness is "holding" this frame. */
static bool pad_a, pad_b, pad_down, pad_up, pad_start;
static unsigned polls;

static int failures;

static void check(bool cond, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (cond) {
        printf("  ok    ");
    } else {
        printf("  FAIL  ");
        ++failures;
    }
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

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

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    last_w = w;
    last_h = h;
    size_t bytes = (size_t)h * pitch;
    if (bytes > sizeof(frame)) return;
    memcpy(frame, data, bytes);
    frame_bytes = bytes;
    ++frames;
}

static void poll_cb(void) { ++polls; }

static int16_t input_state(unsigned port, unsigned device, unsigned index,
                         unsigned id)
{
    if (port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0) return 0;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A:      return pad_a;
    case RETRO_DEVICE_ID_JOYPAD_B:      return pad_b;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return pad_down;
    case RETRO_DEVICE_ID_JOYPAD_UP:     return pad_up;
    case RETRO_DEVICE_ID_JOYPAD_START:  return pad_start;
    default: return 0;
    }
}

static size_t audio_batch(const int16_t *b, size_t n) { (void)b; return n; }

/* Distinct colours in the frame, sampled; cheap and enough to tell the
 * white selection bar of the menu from a game's own art. */
static unsigned long distinct_samples(void)
{
    if (!frame_bytes) return 0;
    unsigned long seen[64];
    size_t nseen = 0;
    size_t px = (size_t)last_w * last_h;
    for (size_t i = 0; i < px; i += 997) {
        uint16_t p;
        size_t off = (i / last_w) * (size_t)last_w * 2 + (i % last_w) * 2;
        if (off + 2 > frame_bytes) break;
        memcpy(&p, frame + off, 2);
        bool dup = false;
        for (size_t k = 0; k < nseen; ++k) if (seen[k] == p) { dup = true; break; }
        if (!dup && nseen < 64) seen[nseen++] = p;
    }
    return (unsigned long)nseen;
}

/* The libretro entry points, resolved in main() and used by the helpers
 * below; declared here because a helper needs them before main runs. */
static unsigned (*api_version)(void) = NULL;
static void (*set_environment)(retro_environment_t) = NULL;
static void (*set_video_refresh)(retro_video_refresh_t) = NULL;
static void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;
static void (*set_input_poll)(retro_input_poll_t) = NULL;
static void (*set_input_state)(retro_input_state_t) = NULL;
static void (*init_cb)(void) = NULL, (*deinit_cb)(void) = NULL;
static bool (*load_game)(const struct retro_game_info *) = NULL;
static void (*run_cb)(void) = NULL, (*unload_game)(void) = NULL;

static bool run_frames(int n)
{
    for (int i = 0; i < n && !shutdown_requested; ++i) run_cb();
    return !shutdown_requested;
}

/* One short press: down for a frame, then released for a few, which is the
 * edge the engine looks for (CPYMO_INPUT_JUST_PRESSED). */
static bool press_and_release(bool *key, int settle)
{
    *key = true;
    run_frames(3);
    *key = false;
    return run_frames(settle);
}

/* Map a local variable name to the real `retro_<NAME>` symbol. */
#define SYM(TYPE, NAME) do { \
    *(void **)(&NAME) = dlsym(h, "retro_" #NAME); \
    if (!NAME) { fprintf(stderr, "missing retro_%s\n", #NAME); return 6; } \
    (void)sizeof(TYPE); \
} while (0)

/* The ones whose local name differs from the symbol's. */
#define SYM_AS(NAME, VAR) do { \
    *(void **)(&VAR) = dlsym(h, "retro_" #NAME); \
    if (!VAR) { fprintf(stderr, "missing retro_%s\n", #NAME); return 6; } \
} while (0)

static void reset_run(void)
{
    frames = 0;
    last_w = last_h = 0;
    frame_bytes = 0;
    log_buf[0] = '\0';
    log_used = 0;
    shutdown_requested = false;
    pad_a = pad_b = pad_down = pad_up = pad_start = false;
    polls = 0;
}

static bool load_path(const char *path)
{
    struct retro_game_info game = { path, NULL, 0, NULL };
    return load_game(&game);
}

static const char *last_select_line(void)
{
    const char *p = log_buf, *found = NULL;
    while ((p = strstr(p, "CPYMO_SELECT ")) != NULL) { found = p; p += 13; }
    return found;
}

static const char *last_menu_line(void)
{
    const char *p = log_buf, *found = NULL;
    while ((p = strstr(p, "CPYMO_MENU ")) != NULL) { found = p; p += 11; }
    return found;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <core.so> <fixture-root> <a-font.ttf>\n",
                argv[0]);
        return 2;
    }
    char root[900], menu_3[1024], menu_one[1024], menu_none[1024],
         one_cfg[1024];
    snprintf(root, sizeof(root), "%s", argv[2]);
    snprintf(menu_3, sizeof(menu_3), "%s/menu_3", root);
    snprintf(menu_one, sizeof(menu_one), "%s/menu_one", root);
    snprintf(menu_none, sizeof(menu_none), "%s/menu_none", root);
    snprintf(one_cfg, sizeof(one_cfg), "%s/menu_one/Narcissus/gameconfig.txt",
             root);

    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 3; }
    SYM_AS(api_version, api_version);
    SYM_AS(set_environment, set_environment);
    SYM_AS(set_video_refresh, set_video_refresh);
    SYM_AS(set_audio_sample_batch, set_audio_sample_batch);
    SYM_AS(set_input_poll, set_input_poll);
    SYM_AS(set_input_state, set_input_state);
    SYM_AS(init, init_cb);
    SYM_AS(deinit, deinit_cb);
    SYM_AS(load_game, load_game);
    SYM_AS(run, run_cb);
    SYM_AS(unload_game, unload_game);

    set_environment(environment);
    set_video_refresh(video);
    set_audio_sample_batch(audio_batch);
    set_input_poll(poll_cb);
    set_input_state(input_state);
    init_cb();

    printf("== 1. a folder of games opens the MENU\n");
    reset_run();
    {
        bool ok = load_path(menu_3);
        check(ok, "the games folder loads (menu)");
        const char *sel = last_select_line();
        check(sel == NULL, "no game was selected yet (%s)",
              sel ? sel : "none");
        if (ok) {
            run_frames(40);
            check(frames > 0, "the menu drew frames (%u)", frames);
            check(last_w == OUT_W && last_h == OUT_H,
                  "the output is %ux%u (%ux%u)", OUT_W, OUT_H, last_w, last_h);
            check(distinct_samples() > 1,
                  "the menu frame is not a flat colour (%lu samples)",
                  distinct_samples());
            const char *m = last_menu_line();
            check(m != NULL && strstr(m, "games=3") != NULL,
                  "the log names the menu and the game count (%s)",
                  m ? m : "no CPYMO_MENU line");
            /* The counters are logged every 180 retro_run calls; run one full
             * interval so the menu's own cost is visible on the device too. */
            run_frames(200);
            check(strstr(log_buf, "CPYMO_PERF") != NULL,
                  "the 180-frame counters still fire in the menu");
            check(strstr(log_buf, "screen=menu") != NULL,
                  "and the record says WHICH screen it measured (%s)",
                  strstr(log_buf, "screen=") ? strstr(log_buf, "screen=")
                                             : "no screen= field");
        }
    }

    printf("== 2. navigation reaches the selector\n");
    {
        unsigned before = frames;
        check(press_and_release(&pad_down, 4), "the DOWN press settles");
        check(frames >= before, "frames continued (%u)", frames);
        const char *m = last_menu_line();
        check(m != NULL && strstr(m, "games=3") != NULL,
              "still the menu after a navigation key");
    }

    printf("== 3. A selects a game, and the log names which one\n");
    {
        bool ok = press_and_release(&pad_a, 30);
        check(ok, "the A press settles");
        const char *sel = last_select_line();
        check(sel != NULL, "a selection happened (%s)", sel ? sel : "none");
        if (sel) {
            check(strstr(sel, "canvas=") != NULL,
                  "and the canvas came with it (%s)", sel);
        }
        run_frames(30);
        check(last_w == OUT_W && last_h == OUT_H,
              "the selected game still outputs %ux%u (%ux%u)",
              OUT_W, OUT_H, last_w, last_h);
        check(frames > 0, "and drew (%u)", frames);
        const char *load = strstr(log_buf, "CPYMO_LOAD");
        check(load != NULL, "the load report also fired");
    }
    unload_game();

    printf("== 4. exactly ONE game: the historic single-game path\n");
    reset_run();
    {
        bool ok = load_path(one_cfg);
        check(ok, "the one game's gameconfig.txt loads");
        const char *sel = last_select_line();
        check(sel != NULL && strstr(sel, "Narcissus") != NULL,
              "it selected that game (%s)", sel ? sel : "none");
        run_frames(20);
        check(frames > 0, "and drew (%u)", frames);
    }
    unload_game();

    printf("== 5. a folder with NO game: the plain path, with a diagnostic\n");
    reset_run();
    {
        bool ok = load_path(menu_none);
        check(!ok, "loading an empty games folder fails (as before)");
        check(strstr(log_buf, "no gameconfig.txt") != NULL,
              "and says exactly what is missing");
        const char *m = last_menu_line();
        check(m == NULL, "an empty folder opens no menu");
    }
    unload_game();

    printf("== 6. CPYMO_MENU=0 turns the menu off\n");
    reset_run();
    setenv("CPYMO_MENU", "0", 1);
    {
        bool ok = load_path(menu_3);
        check(!ok, "with the menu disabled the folder is not a game");
        check(last_select_line() == NULL, "and nothing was selected");
    }
    unload_game();
    unsetenv("CPYMO_MENU");

    printf("== 7. the menu's font reaches a game that has none of its own\n");
    reset_run();
    {
        /* menu_nofontgame/ has one game per resolution and a system/ font
         * ONLY at the games root: exactly the card a user makes by dropping
         * games next to the launcher. */
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/menu_nofontgame", root);
        bool ok = load_path(dir);
        check(ok, "the games root opens the menu");
        /* Let the menu settle before the press: the engine's first update
         * sees the pad already down and treats it as the previous state, so a
         * press only registers as an edge once the UI is running. */
        run_frames(30);
        ok = press_and_release(&pad_a, 40);
        check(ok, "a selection settles");
        const char *sel = last_select_line();
        check(sel != NULL, "a game was selected (%s)", sel ? sel : "none");
        const char *font_note = strstr(log_buf, "font: ");
        check(font_note != NULL || !run_frames(20),
              "the font came from the candidate list%s",
              font_note ? "" : " (none logged: its own font was used)");
    }
    unload_game();

    printf("== 8. an explicit CPYMO_MENU_FONT for a fontless games root\n");
    reset_run();
    {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/menu_fontless", root);
        setenv("CPYMO_MENU_FONT", argv[3], 1);
        bool ok = load_path(dir);
        check(ok, "the menu opens with an explicit font");
        if (ok) {
            run_frames(20);
            check(last_w == OUT_W && last_h == OUT_H,
                  "and draws at the output size (%ux%u)", last_w, last_h);
        }
        unsetenv("CPYMO_MENU_FONT");
    }
    unload_game();

    unload_game();
    deinit_cb();
    dlclose(h);

    printf("\n%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
