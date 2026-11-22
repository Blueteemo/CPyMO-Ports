#include <SDL/SDL.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpymo/cpymo_engine.h"
#include "cpymo-backends/sdl1/cpymo_s60_app.h"
#include "cpymo-backends/sdl1/cpymo_s60_native_font.h"
#include "cpymo/cpymo_s60_pymo_save.h"

static Uint8 key_state[SDLK_LAST];
static int index_now, released, pending_shot;
static Uint32 initial_ticks;
static FILE *capture;
extern cpymo_engine engine;
int __real_SDL_PollEvent(SDL_Event *event);
int __real_SDL_Flip(SDL_Surface *screen);

typedef struct { unsigned time; SDLKey key; const char *shot; } Step;

static const Step import_actions[] = {
    {  200, SDLK_0, "about" },
    {  435, SDLK_ESCAPE, "list-return" },
    {  650, SDLK_8, "list-2" },
    {  875, SDLK_8, NULL },
    { 1100, SDLK_8, NULL },
    { 1325, SDLK_8, "list-scroll" },
    { 1550, SDLK_SPACE, NULL },
    { 3550, SDLK_5, "dialogue" },
    { 3950, SDLK_LEFT, "dialogue-hidden" },
    { 4275, SDLK_LEFT, NULL },
    { 4600, SDLK_SPACE, "game-menu" },
    { 4850, SDLK_8, "game-menu-next" },
    { 5100, SDLK_8, NULL },
    { 5350, SDLK_5, "read" },
    { 5700, SDLK_2, NULL },
    { 5950, SDLK_5, "legacy-slots" },
    { 6200, SDLK_5, "import-confirm" },
    { 6450, SDLK_8, NULL },
    { 6700, SDLK_8, NULL },
    { 6950, SDLK_5, "import-result" },
    { 7300, SDLK_5, "post-import-slots" },
    { 7550, SDLK_5, NULL },
    { 7800, SDLK_8, NULL },
    { 8050, SDLK_8, NULL },
    { 8300, SDLK_5, NULL },
    { 10000, SDLK_UNKNOWN, "restored-checkpoint" },
    { 10250, SDLK_5, "loaded-dialogue" },
    { 10500, SDLK_END, "exit-confirm" },
    { 10750, SDLK_8, NULL },
    { 11000, SDLK_8, NULL },
    { 11250, SDLK_5, NULL }
};

static const Step font_actions[] = {
    {  200, SDLK_5, NULL },
    { 2400, SDLK_SPACE, NULL },
    { 2650, SDLK_8, NULL },
    { 2900, SDLK_8, NULL },
    { 3150, SDLK_8, NULL },
    { 3400, SDLK_8, NULL },
    { 3650, SDLK_8, NULL },
    { 3900, SDLK_8, NULL },
    { 4150, SDLK_5, NULL },
    { 4400, SDLK_8, NULL },
    { 4650, SDLK_8, NULL },
    { 4900, SDLK_8, NULL },
    { 5150, SDLK_8, NULL },
    { 5400, SDLK_8, NULL },
    { 5650, SDLK_8, NULL },
    { 5900, SDLK_5, NULL },
    { 6070, SDLK_UNKNOWN, "font-restart-prompt" },
    { 6150, SDLK_ESCAPE, "font-restart-cancelled" },
    { 6400, SDLK_5, NULL },
    { 6650, SDLK_5, NULL },
    { 6900, SDLK_8, NULL },
    { 7150, SDLK_8, NULL },
    { 7400, SDLK_5, NULL },
    { 8000, SDLK_ESCAPE, "font-after-restart" },
    { 8250, SDLK_8, NULL },
    { 8500, SDLK_8, NULL },
    { 8750, SDLK_5, NULL }
};

static const Step *actions;
static size_t action_count;

float cpymo_s60_font_baseline(float height, bool dialogue);
void cpymo_s60_font_render(void *output, int *width, int *height, cpymo_str text,
    float pixel_height, float baseline, bool dialogue);

static void verify_story_raster(float size, int expected_width, size_t minimum_ink)
{
    const cpymo_str sample = cpymo_str_pure("中梦药永请叩字。");
    const float baseline = cpymo_s60_font_baseline(size, true);
    int width, height;
    cpymo_s60_font_render(NULL, &width, &height, sample, size, baseline, true);
    assert(width == expected_width && height >= (int)size);
    unsigned char *raster = calloc((size_t)width * height, 1);
    assert(raster);
    cpymo_s60_font_render(raster, &width, &height, sample, size, baseline, true);
    size_t visible = 0, antialiased = 0;
    for (int i = 0; i < width * height; ++i) {
        if (raster[i]) ++visible;
        if (raster[i] && raster[i] != 255) ++antialiased;
    }
    free(raster);
    assert(visible > minimum_ink && antialiased == 0);
    printf("[Host] %dpx dialogue raster: %lu visible pixels, %lu edge pixels.\n",
        (int)size, (unsigned long)visible, (unsigned long)antialiased);
}

Uint8 *__wrap_SDL_GetKeyState(int *count)
{
    if (count) *count = SDLK_LAST;
    return key_state;
}

int __wrap_SDL_PollEvent(SDL_Event *event)
{
    if (!initial_ticks) {
        initial_ticks = SDL_GetTicks() + 1;
        if (getenv("CPYMO_TEST_FONT")) {
            actions = font_actions;
            action_count = sizeof(font_actions) / sizeof(*font_actions);
        } else {
            actions = import_actions;
            action_count = sizeof(import_actions) / sizeof(*import_actions);
        }
    }
    const int elapsed = (int)SDL_GetTicks() - (int)initial_ticks;
    if ((size_t)index_now >= action_count) return 0;
    if (elapsed < (int)actions[index_now].time + released * 80) return 0;
    const SDLKey key = actions[index_now].key;
    memset(event, 0, sizeof(*event));
    if (key == SDLK_UNKNOWN) {
        event->type = SDL_VIDEOEXPOSE;
        pending_shot = index_now + 1;
        ++index_now;
        return 1;
    }
    event->type = released ? SDL_KEYUP : SDL_KEYDOWN;
    event->key.keysym.sym = key;
    key_state[key] = !released;
    if (!released && key == SDLK_0 && index_now == 0)
        pending_shot = index_now + 1;
    if (released) {
        if (key == SDLK_0 && index_now == 0) {
            assert(cpymo_s60_about_is_open());
            printf("[Host] About opened on key-down, before the zero key was released.\n");
        } else if (actions[index_now].shot) {
            pending_shot = index_now + 1;
        }
        ++index_now;
    }
    released = !released;
    return 1;
}

int __wrap_SDL_Flip(SDL_Surface *screen)
{
    static int first = 1;
    if (first || pending_shot) {
        const char *label = first ? "first-frame" : actions[pending_shot - 1].shot;
        if (first) {
            verify_story_raster(16.0f, 128, 322);
            verify_story_raster(12.0f, 96, 140);
        }
        if (!strcmp(label, "post-import-slots")) {
            assert(cpymo_s60_pymo_save_import(&engine, 2, 2) == CPYMO_ERR_BAD_FILE_FORMAT);
            assert(cpymo_s60_pymo_save_import(&engine, 1, 1) == CPYMO_ERR_CAN_NOT_OPEN_FILE);
            assert(access("E:/Python/game5/save/save-02.csav", F_OK) != 0);
            assert(access("E:/Python/game5/save/cpymo-import-02.part", F_OK) != 0);
            puts("[Host] Malformed import rejected; occupied slot was not overwritten.");
        }
        if (!strcmp(label, "loaded-dialogue")) {
            assert(cpymo_vars_get(&engine.vars, cpymo_str_pure("FIX_BRANCH")) == 1);
            assert(engine.say.current_text && strstr(engine.say.current_text, "读档检查"));
        }
        if (!strcmp(label, "font-restart-cancelled")) {
            assert(cpymo_s60_active_font_profile() == CPYMO_FONT_COMBINED);
            assert(cpymo_s60_next_font_profile() == CPYMO_FONT_SMOOTH);
        }
        if (!strcmp(label, "font-after-restart"))
            assert(cpymo_s60_active_font_profile() == CPYMO_FONT_SMOOTH);
        char path[128];
        snprintf(path, sizeof(path), "%s.bmp", label);
        if (SDL_SaveBMP(screen, path) != 0) {
            fprintf(stderr, "Screenshot error: %s\n", SDL_GetError());
            exit(2);
        }
        if (!capture) capture = fopen("stage.log", "w");
        if (capture) {
            fprintf(capture, "%s about=%d dialogue=%s hidden=%d\n", label,
                cpymo_s60_about_is_open(), engine.say.current_text ? engine.say.current_text : "",
                engine.say.hide_window);
            fflush(capture);
        }
        first = pending_shot = 0;
    }
    return __real_SDL_Flip(screen);
}
