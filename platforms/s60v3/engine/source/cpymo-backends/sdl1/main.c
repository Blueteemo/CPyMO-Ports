#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#define FASTEST_FILTER STBIR_FILTER_BOX
#define STBIR_DEFAULT_FILTER_DOWNSAMPLE  FASTEST_FILTER
#define STBIR_DEFAULT_FILTER_UPSAMPLE    FASTEST_FILTER

#ifdef LEAKCHECK
#define STB_LEAKCHECK_IMPLEMENTATION
#endif

#include "../../cpymo/cpymo_prelude.h"
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../stb/stb_image_resize.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../stb/stb_image_write.h"

#define STB_DS_IMPLEMENTATION
#include "../../stb/stb_ds.h"

#include <SDL/SDL.h>
#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_error.h"
#include <time.h>

#ifdef S60V3
#include "cpymo_s60_app.h"
#include "cpymo_s60_diagnostics.h"
#include "cpymo_s60_rgb565.h"
#include "cpymo_s60_discovery.h"
#include "../../cpymo/cpymo_msgbox_ui.h"
#ifndef USE_GAME_SELECTOR
#define USE_GAME_SELECTOR
#endif
#ifndef EXIT_TO_GAME_SELECTOR
#define EXIT_TO_GAME_SELECTOR
#endif
#ifndef GAME_SELECTOR_DIR
#define GAME_SELECTOR_DIR CPYMO_S60_DATA_DIR
#endif
#ifndef GAME_SELECTOR_FONTSIZE
#ifdef S60V3
#define GAME_SELECTOR_FONTSIZE 18
#else
#define GAME_SELECTOR_FONTSIZE 16
#endif
#endif
#ifndef GAME_SELECTOR_EMPTY_MSG_FONTSIZE
#ifdef S60V3
#define GAME_SELECTOR_EMPTY_MSG_FONTSIZE 18
#else
#define GAME_SELECTOR_EMPTY_MSG_FONTSIZE 16
#endif
#endif
#ifndef GAME_SELECTOR_COUNT_PER_SCREEN
#define GAME_SELECTOR_COUNT_PER_SCREEN 4
#endif
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 320
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 240
#endif
#endif

cpymo_engine engine;
SDL_Surface *framebuffer;
extern int mouse_wheel;

#ifndef SCREEN_BPP
#define SCREEN_BPP 24
#endif

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH engine.gameconfig.imagesize_w
#endif

#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT engine.gameconfig.imagesize_h
#endif

#ifndef SCREEN_FLAGS
#define SCREEN_FLAGS (SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_ANYFORMAT | SDL_HWPALETTE | SDL_DOUBLEBUF)
#endif

extern error_t cpymo_backend_font_init(const char *gamedir);
extern void cpymo_backend_font_free();

static bool current_full_screen;

#ifdef S60V3
static bool s60_in_selector = true;
static bool s60_return_requested = false;
static bool s60_font_restart_requested = false;
static bool s60_exit_requested = false;
static bool s60_exit_prompt_pending = false;
static bool s60_exit_prompt_open = false;
static bool s60_engine_live = true;
static bool s60_game_load_failed = false;
static char *s60_start_requested = NULL;
static Uint32 s60_start_at;
static SDL_Surface *restore_scene;
static unsigned restore_scene_phase;

static void release_restore_scene(void)
{
    if (restore_scene) SDL_FreeSurface(restore_scene);
    restore_scene = NULL;
    restore_scene_phase = 0;
}

static void draw_s60_frame(void)
{
    if (!engine.restoring) {
        release_restore_scene();
        SDL_FillRect(framebuffer, NULL, 0);
        cpymo_engine_draw(&engine);
        return;
    }
    if (engine.restore_at_black) {
        release_restore_scene();
        SDL_FillRect(framebuffer, NULL, 0);
        return;
    }
    const unsigned phase = engine.restore_file ? 1 : 2;
    const float alpha = cpymo_tween_value(&engine.restore_fade);
    if (restore_scene_phase != phase) {
        release_restore_scene();
        SDL_FillRect(framebuffer, NULL, 0);
        cpymo_engine_draw(&engine);
        if (cpymo_s60_is_rgb565(framebuffer->format))
            restore_scene = SDL_ConvertSurface(framebuffer, framebuffer->format, SDL_SWSURFACE);
        restore_scene_phase = phase;
    }
    if (restore_scene != NULL && SDL_LockSurface(framebuffer) == 0) {
        SDL_Rect clip;
        SDL_GetClipRect(framebuffer, &clip);
        const unsigned opacity = (unsigned)(alpha * 64.0f + 0.5f);
        for (int y = clip.y; y < clip.y + clip.h; ++y) {
            const Uint16 *src = (const Uint16 *)((const Uint8 *)restore_scene->pixels +
                y * restore_scene->pitch) + clip.x;
            Uint16 *dst = (Uint16 *)((Uint8 *)framebuffer->pixels +
                y * framebuffer->pitch) + clip.x;
            for (int x = 0; x < clip.w; ++x)
                dst[x] = cpymo_s60_blend565(src[x], 0, opacity);
        }
        SDL_UnlockSurface(framebuffer);
        return;
    }

    SDL_FillRect(framebuffer, NULL, 0);
    cpymo_engine_draw(&engine);
    const float area[] = {0, 0, engine.gameconfig.imagesize_w, engine.gameconfig.imagesize_h};
    cpymo_backend_image_fill_rects(area, 1, cpymo_color_black, alpha,
        cpymo_backend_image_draw_type_bg);
}


void cpymo_s60_request_game_start(char *gamedir)
{
    if (s60_start_requested) free(s60_start_requested);
    s60_start_requested = gamedir;
    // Let Avkon finish the confirm click before SDL takes the audio stream.
    s60_start_at = SDL_GetTicks() + 160;
}
void cpymo_s60_open_game_audio(void)
{
    extern void cpymo_backend_audio_init(void);
    CPYMO_S60_TRACE("[S60] opening game PCM after script-type gate.\n");
    cpymo_backend_audio_init();
}

void cpymo_s60_request_game_list(void) { s60_return_requested = true; }
bool cpymo_s60_game_list_requested(void) { return s60_return_requested; }
void cpymo_s60_request_font_restart(void)
{
    s60_font_restart_requested = true;
    cpymo_s60_request_game_list();
}
void cpymo_s60_request_exit(void) { s60_exit_requested = true; }
int cpymo_s60_exit_is_requested(void) { return s60_exit_requested; }
void cpymo_s60_request_exit_confirmation(void) { s60_exit_prompt_pending = true; }
int cpymo_s60_selector_mode(void) { return s60_in_selector; }

int cpymo_s60_handle_selector_end_key(void)
{
    if (!s60_in_selector || cpymo_s60_about_is_open() || s60_exit_prompt_open)
        return 0;
    cpymo_s60_request_exit();
    return 1;
}

static error_t s60_exit_confirm(cpymo_engine *e, void *data, bool confirm)
{
    s60_exit_prompt_open = false;
    if (confirm) cpymo_s60_request_exit();
    return CPYMO_ERR_SUCC;
}
#endif

static void set_clip_rect(size_t screen_w, size_t screen_h) 
{
    SDL_Rect clip;
    clip.x = (screen_w - engine.gameconfig.imagesize_w) / 2;
    clip.y = (screen_h - engine.gameconfig.imagesize_h) / 2;
    clip.w = engine.gameconfig.imagesize_w;
    clip.h = engine.gameconfig.imagesize_h;

    SDL_SetClipRect(framebuffer, &clip);
}

static error_t set_video_mode(size_t w, size_t h, bool fullscreen) 
{
    Uint32 flags = SCREEN_FLAGS;
    if (fullscreen)
        flags |= SDL_FULLSCREEN;

#ifdef SCREEN_RESIZABLE
    flags |= SDL_RESIZABLE;
#endif

    current_full_screen = fullscreen;

    framebuffer = SDL_SetVideoMode((int)w, (int)h, SCREEN_BPP, flags);
    if (framebuffer == NULL)
        return CPYMO_ERR_UNKNOWN;
    
    set_clip_rect(w, h);
    return CPYMO_ERR_SUCC;
}

#ifdef S60V3
#include "cpymo_s60_startup_image.h"

static void show_startup_screen(void)
{
    SDL_SetClipRect(framebuffer, NULL);
    SDL_FillRect(framebuffer, NULL, 0);
    int width, height;
    stbi_uc *pixels = stbi_load_from_memory(cpymo_s60_startup_png,
        sizeof(cpymo_s60_startup_png), &width, &height, NULL, 4);
    if (pixels && width == framebuffer->w && height == framebuffer->h) {
        SDL_Surface *image = SDL_CreateRGBSurfaceFrom(pixels, width, height, 32,
            width * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0);
        if (image) {
            if (SDL_BlitSurface(image, NULL, framebuffer, NULL) == 0)
                CPYMO_S60_TRACE("[Startup] embedded image blitted.\n");
            SDL_FreeSurface(image);
        }
    }
    stbi_image_free(pixels);
    SDL_Flip(framebuffer);
}
#endif

#ifndef LOAD_GAME_ICON
#define load_game_icon(...)
#else 
static inline void load_game_icon(const char *gamedir) 
{
    char *path = alloca(strlen(gamedir) + 10);
    sprintf(path, "%s/icon.png", gamedir);

    int w, h;
    stbi_uc *data = stbi_load(path, &w, &h, NULL, 4);
    if (data == NULL) return;

    if (w != 57 || h != 57) {
        printf("[Warning] Icon must be 57x57.\n");
    }

    void *px = (void *)malloc(32 * 32 * 4);
    if (px == NULL) {
        free(data);
        return;
    }

    stbir_resize_uint8(data, w, h, 0, px, 32, 32, 0, 4);
    free(data);

    Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000;
	gmask = 0x00ff0000;
	bmask = 0x0000ff00;
	amask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = 0xff000000;
#endif

    SDL_Surface *sur = SDL_CreateRGBSurfaceFrom(
		px,
		32,
		32,
		32,
		4 * 32,
		rmask,
		gmask,
		bmask,
		amask);

    if (sur == NULL) {
        free(px);
        return;
    }

    SDL_WM_SetIcon(sur, NULL);
    SDL_FreeSurface(sur);
    free(px);
}
#endif

#ifdef _WIN32
#include <direct.h>
#define mkdir(x, y) _mkdir(x)
#else
#include <sys/stat.h>
#endif

static void ensure_save_dir(const char *gamedir)
{
	char *save_dir = (char *)alloca(strlen(gamedir) + 8);
	sprintf(save_dir, "%s/save", gamedir);
	mkdir(save_dir, 0777);
}

#ifdef USE_GAME_SELECTOR
#include "../../cpymo/cpymo_game_selector.h"

#include <dirent.h>

static char *get_last_selected_game_dir()
{
#ifdef S60V3
    return cpymo_s60_read_last_game();
#else
	char *str = NULL;
	size_t len;
	error_t err = cpymo_utils_loadfile(GAME_SELECTOR_DIR "/last_game.txt", &str, &len);

	if (err != CPYMO_ERR_SUCC) return NULL;

	char *ret = realloc(str, len + 1);
	if (ret == NULL) {
		free(str);
		return NULL;
	}

	ret[len] = '\0';
	return ret;
#endif
}

static void save_last_selected_game_dir(const char *gamedir)
{
#ifdef S60V3
    cpymo_s60_write_last_game(gamedir);
#else
	size_t len = strlen(gamedir);
	FILE *f = fopen(GAME_SELECTOR_DIR "/last_game.txt", "wb");
	if (f == NULL) return;

	fwrite(gamedir, len, 1, f);
	fclose(f);
#endif
}

static error_t after_start_game(cpymo_engine *e, const char *gamedir)
{
	save_last_selected_game_dir(gamedir);
#ifdef S60V3
    s60_in_selector = false;
    CPYMO_S60_TRACE("[S60] entered game: %s\n", gamedir);
#endif

#ifdef GAME_SELECTOR_RESET_SCREEN_SIZE_AFTER_START_GAME
	set_video_mode(e->gameconfig.imagesize_w, e->gameconfig.imagesize_h, current_full_screen);
#else
    SDL_FillRect(framebuffer, NULL, 0);
    set_clip_rect(SCREEN_WIDTH, SCREEN_HEIGHT);
#endif

#if !(defined __PSP__ || defined __WII__ || defined S60V3)
	cpymo_backend_font_free();
	error_t err = cpymo_backend_font_init(gamedir);
	CPYMO_THROW(err);
#endif

	ensure_save_dir(gamedir);

    SDL_WM_SetCaption(
        engine.gameconfig.gametitle,
        engine.gameconfig.gametitle);

	return CPYMO_ERR_SUCC;
}

cpymo_game_selector_item *get_game_list(const char *game_selector_dir)
{
#ifdef S60V3
    return cpymo_s60_get_game_list();
#else
	cpymo_game_selector_item *item = NULL;
	cpymo_game_selector_item *item_tail = NULL;

	DIR *dir = opendir(game_selector_dir);

	if (dir) {
		struct dirent* ent;
		while ((ent = readdir(dir))) {
			char *path = (char *)malloc(strlen(ent->d_name) + strlen(game_selector_dir) + 4);
			sprintf(path, "%s/%s", game_selector_dir, ent->d_name);

			cpymo_game_selector_item *cur = NULL;
			error_t err = cpymo_game_selector_item_create(&cur, &path);
			if (err != CPYMO_ERR_SUCC) {
				free(path);
				continue;
			}

			if (item == NULL) {
				item = cur;
				item_tail = cur;
			}
			else {
				item_tail->next = cur;
				item_tail = cur;
			}

		}
		closedir(dir);
	}

	return item;
#endif
}
#endif

#if (!(defined DISABLE_FFMPEG_AUDIO) && !(defined DISABLE_FFMPEG_MOVIE))
#include <libavutil/log.h>
#endif

#ifdef __WII__
#include <fat.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#endif

#ifdef ENABLE_EXIT_CONFIRM
#include "../../cpymo/cpymo_msgbox_ui.h"
#include "../../cpymo/cpymo_localization.h"
static error_t cpymo_exit_confirm(struct cpymo_engine *e, void *data, bool exit)
{
    *(bool *)data = false;
#ifdef S60V3
    if (exit) cpymo_s60_request_exit();
    return CPYMO_ERR_SUCC;
#else
	return exit ? CPYMO_ERR_NO_MORE_CONTENT : CPYMO_ERR_SUCC;
#endif
}
#endif

#ifdef __PSP__
#define main SDL_main
#include <psppower.h>
#endif

int main(int argc, char **argv) 
{
#ifdef __PSP__    
    scePowerSetCpuClockFrequency(333);
    scePowerSetBusClockFrequency(167);
#endif

    srand((unsigned)time(NULL));

#if (!(defined DISABLE_FFMPEG_AUDIO) && !(defined DISABLE_FFMPEG_MOVIE))
	av_log_set_level(AV_LOG_ERROR);
#endif

#ifdef __WII__
    fatInitDefault();
    WPAD_Init();
    stbi_set_flip_vertically_on_load_thread(false);
#endif

    if (SDL_Init(
            SDL_INIT_VIDEO
#ifndef DISABLE_AUDIO
            | SDL_INIT_AUDIO
#endif
#ifdef __PSP__
            | SDL_INIT_JOYSTICK
#endif
        ) < 0) {
        printf("[Error] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

#if defined __WII__ || defined __PSP__ || defined(S60V3)
    SDL_ShowCursor(SDL_DISABLE);
#endif

#ifdef DEFAULT_FULLSCREEN
    const bool launch_fullscreen = true;
#else
    const bool launch_fullscreen = false;
#endif
#ifdef S60V3
    if (set_video_mode(SCREEN_WIDTH, SCREEN_HEIGHT, launch_fullscreen) != CPYMO_ERR_SUCC) {
        printf("[Error] SDL_SetVideoMode: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    show_startup_screen();
#endif

    extern void cpymo_backend_audio_init(void);
    extern void cpymo_backend_audio_free(void);

#ifndef S60V3
    cpymo_backend_audio_init();
#endif

    extern error_t cpymo_backend_image_init(void);
    extern void cpymo_backend_image_quit(void);
    error_t err = cpymo_backend_image_init();
    if (err != CPYMO_ERR_SUCC) {
        cpymo_backend_audio_free();
        SDL_Quit();
        printf("[Error] cpymo_backend_image_init: %s\n", 
            cpymo_error_message(err));
        return err;
    }

#ifdef USE_GAME_SELECTOR
#ifdef S60V3
    cpymo_s60_ensure_data_dir();
#endif
    cpymo_game_selector_item *items = get_game_list(GAME_SELECTOR_DIR);
    char *last_selected = get_last_selected_game_dir();
    err = cpymo_engine_init_with_game_selector(
        &engine, 
        SCREEN_WIDTH, SCREEN_HEIGHT,
        GAME_SELECTOR_FONTSIZE,
        GAME_SELECTOR_EMPTY_MSG_FONTSIZE,
        GAME_SELECTOR_COUNT_PER_SCREEN,
        &items,
        NULL,
        &after_start_game,
        &last_selected);

    if (err != CPYMO_ERR_SUCC) {
        printf("[Error] cpymo_engine_init_with_game_selector: %s\n", cpymo_error_message(err));
        cpymo_game_selector_item_free_all(items);
        free(last_selected);
        cpymo_backend_image_quit();
        cpymo_backend_audio_free();
        SDL_Quit();
        return -1;
    }

    err = cpymo_backend_font_init(NULL);
#else    
    const char *gamedir = ".";

    if (argc > 1) {
        gamedir = argv[1];
    }

    load_game_icon(gamedir);

    err = cpymo_engine_init(&engine, gamedir);
    if (err != CPYMO_ERR_SUCC) {
        printf("[Error] cpymo_engine_init: %s\n", cpymo_error_message(err));
        cpymo_backend_image_quit();
        cpymo_backend_audio_free();
        SDL_Quit();
        return -1;
    }

    ensure_save_dir(gamedir);

    err = cpymo_backend_font_init(gamedir);
#endif
    if (err != CPYMO_ERR_SUCC) {
        printf("[Error] cpymo_backend_font_init: %s\n", cpymo_error_message(err));
        cpymo_engine_free(&engine);
        cpymo_backend_image_quit();
        cpymo_backend_audio_free();
        SDL_Quit();
        return -1;
    }
    
#ifdef S60V3
    /* Precompute the list text and icon surfaces before replacing the splash. */
    bool list_ready = false;
    err = cpymo_engine_update(&engine, 0.0f, &list_ready);
    if (err != CPYMO_ERR_SUCC) {
        cpymo_engine_free(&engine);
        cpymo_backend_image_quit();
        cpymo_backend_font_free();
        SDL_Quit();
        return -1;
    }
    set_clip_rect(SCREEN_WIDTH, SCREEN_HEIGHT);
#else
    err = set_video_mode(SCREEN_WIDTH, SCREEN_HEIGHT, launch_fullscreen);
    if (framebuffer == NULL) {
        printf("[Error] SDL_SetVideoMode: %s\n", SDL_GetError());
        cpymo_engine_free(&engine);
        cpymo_backend_image_quit();
        cpymo_backend_audio_free();
        cpymo_backend_font_free();
        SDL_Quit();
        return -1;
    }
#endif

    SDL_WM_SetCaption(
        engine.gameconfig.gametitle,
        engine.gameconfig.gametitle);
#ifdef S60V3
    SDL_FillRect(framebuffer, NULL, 0);
    cpymo_engine_draw(&engine);
    SDL_Flip(framebuffer);
#endif

    Uint32 prev_time = SDL_GetTicks();
    int ret = 0;

    #ifdef ENABLE_EXIT_CONFIRM
    bool exit_okcancel_box_opened = false;
    #endif

    while (1) {
        bool redraw_system = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT: {
#ifdef S60V3
                if (!s60_exit_prompt_open) s60_exit_prompt_pending = true;
                break;
#else
                #ifdef ENABLE_EXIT_CONFIRM
                #ifndef DISABLE_MOVIE
                extern bool playing_movie;
                if (playing_movie) goto EXIT;
                #endif

                if (!exit_okcancel_box_opened)
                {
                    err = cpymo_msgbox_ui_enter(
                        &engine,
                        cpymo_str_pure(
                            cpymo_localization_get(&engine)->exit_confirm),
                        &cpymo_exit_confirm,
                        &exit_okcancel_box_opened);

                    if (err != CPYMO_ERR_SUCC) {
                        printf("[Error] Can not show message box: %s", 
                            cpymo_error_message(err));
                    }

                    exit_okcancel_box_opened = true;
                }
				#else
				goto EXIT;
				#endif
#endif /* !S60V3 */
            }
#if defined(S60V3) || defined(TOGGLE_FULLSCREEN)
#ifdef S60V3
            case SDL_KEYUP:
#endif
            case SDL_KEYDOWN:
#ifdef S60V3
                if (event.key.keysym.sym == SDLK_0 || event.key.keysym.sym == SDLK_KP0 ||
                    (s60_in_selector && event.key.keysym.sym == SDLK_m)) {
                    if (event.type != SDL_KEYDOWN || s60_exit_prompt_open ||
                        s60_exit_prompt_pending) break;
                    if (!s60_in_selector)
                        cpymo_s60_latch_menu_key();
                    else if (!cpymo_s60_about_is_open()) {
                        CPYMO_S60_TRACE("[S60] About shortcut: SDL key %d, scan %u.\n",
                            event.key.keysym.sym, event.key.keysym.scancode);
                        error_t about_err = cpymo_s60_about_open(&engine);
                        if (about_err != CPYMO_ERR_SUCC)
                            printf("[S60] About page: %s\n", cpymo_error_message(about_err));
                    }
                    break;
                }
                if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_END) {
                    if (!cpymo_s60_handle_selector_end_key() && !s60_exit_prompt_open)
                        s60_exit_prompt_pending = true;
                    break;
                }
                if (event.type == SDL_KEYUP && s60_in_selector &&
                    !cpymo_s60_about_is_open() && !s60_exit_prompt_open &&
                    (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_MENU))
                    s60_exit_prompt_pending = true;
                if (event.type == SDL_KEYDOWN &&
                    (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_MENU) &&
                    (!s60_in_selector || cpymo_s60_about_is_open() || s60_exit_prompt_open))
                    cpymo_s60_latch_cancel_key();
                if (event.type == SDL_KEYUP) break;
#endif
#ifdef TOGGLE_FULLSCREEN
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    err = set_video_mode(SCREEN_WIDTH, SCREEN_HEIGHT, !current_full_screen);
                    if (err != CPYMO_ERR_SUCC) {
                        printf("[Error] SDL_SetVideoMode: %s\n", SDL_GetError());
                        ret = -1;
                        goto EXIT;
                    }
                }
#endif
                break;
#endif
            case SDL_VIDEOEXPOSE: redraw_system = true; break;
            case SDL_VIDEORESIZE:
#ifdef S60V3
                // SDL has reconfigured the physical display. Its width/height
                // are not the dimensions of CPyMO's landscape logical canvas.
                // Keep the existing framebuffer (including in-game geometry).
                release_restore_scene();
#else
                err = set_video_mode(event.resize.w, event.resize.h, current_full_screen);
                if (err != CPYMO_ERR_SUCC) {
                    printf("[Error] SDL_SetVideoMode: %s\n", SDL_GetError());
                    ret = -1;
                    goto EXIT;
                }
#endif
                redraw_system = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_WHEELDOWN) {
                    --mouse_wheel;
                }
                else if(event.button.button == SDL_BUTTON_WHEELUP) {
                    ++mouse_wheel;
                }
            };
        }

        bool redraw = false;
        Uint32 cur_time = SDL_GetTicks();
#ifdef S60V3
        if (s60_exit_requested) goto EXIT;
        if (s60_start_requested && (Sint32)(cur_time - s60_start_at) < 0) {
            SDL_Delay(8);
            continue;
        }
        const bool loading_saved_scene = engine.restoring && engine.restore_at_black;
#endif
#ifdef REDRAW_WHAT_EVER
        cpymo_engine_request_redraw(&engine);
#endif
        err = cpymo_engine_update(
            &engine, 
            (float)(cur_time - prev_time) / 1000.0f, 
            &redraw);

#ifdef S60V3
        if (cpymo_s60_movie_take_time_reset()) cur_time = SDL_GetTicks();
        if (loading_saved_scene) {
            const Uint32 loaded_at = SDL_GetTicks();
            CPYMO_S60_TRACE("[S60] Save restore and scene loading: %lu ms.\n",
                (unsigned long)(loaded_at - cur_time));
            cur_time = loaded_at;
        }
        if (s60_exit_requested) goto EXIT;
        if (s60_start_requested && (Sint32)(SDL_GetTicks() - s60_start_at) >= 0) {
            char *path = s60_start_requested;
            s60_start_requested = NULL;
            cpymo_gameconfig preflight_config;
            const bool config_loaded =
                cpymo_engine_s60_read_gameconfig(&preflight_config, path) == CPYMO_ERR_SUCC;
            int legacy_decision = 2;
            if (s60_in_selector && config_loaded &&
                cpymo_engine_s60_uses_legacy_script(&preflight_config)) {
                legacy_decision = cpymo_s60_native_confirm_legacy_mo();
                if (legacy_decision == 0) {
                    // Keep the live list (including selection and key-repeat state).
                    free(path);
                    prev_time = SDL_GetTicks();
                    continue;
                }
            }
            cpymo_engine_free(&engine);
            memset(&engine, 0, sizeof(engine));
            s60_engine_live = false;
            err = cpymo_engine_s60_init_from_list(&engine, path,
                config_loaded ? &preflight_config : NULL, legacy_decision);
            if (err == CPYMO_ERR_SUCC) {
                s60_engine_live = true;
                err = after_start_game(&engine, path);
            }
            else {
                /* engine_init unwinds its partially-created resources on failure,
                   but the initialized audio system still belongs to the caller. */
                cpymo_audio_free(&engine.audio);
                memset(&engine, 0, sizeof(engine));
            }
            free(path);
            if (err != CPYMO_ERR_SUCC) {
                printf("[S60] game load failed: %s; returning to list\n", cpymo_error_message(err));
                s60_game_load_failed = true;
                s60_return_requested = true;
                err = CPYMO_ERR_SUCC;
            }
            else {
                prev_time = SDL_GetTicks();
                continue;
            }
        }
        if (err != CPYMO_ERR_SUCC && !s60_in_selector) {
            if (err != CPYMO_ERR_NO_MORE_CONTENT) {
                printf("[S60] game update failed: %s; returning to list\n", cpymo_error_message(err));
                s60_game_load_failed = true;
            }
            s60_return_requested = true;
            err = CPYMO_ERR_SUCC;
        }
        if (s60_return_requested) {
            release_restore_scene();
            s60_return_requested = false;
            CPYMO_S60_TRACE("[S60] returning to game list; freeing current engine/audio\n");
            if (s60_engine_live) cpymo_engine_free(&engine);
            cpymo_backend_audio_free();
            s60_engine_live = false;
            memset(&engine, 0, sizeof(engine));
            if (s60_font_restart_requested) {
                s60_font_restart_requested = false;
                cpymo_backend_font_free();
                err = cpymo_backend_font_init(NULL);
                if (err != CPYMO_ERR_SUCC) {
                    printf("[S60] Could not reload fonts: %s\n", cpymo_error_message(err));
                    ret = -1;
                    goto EXIT_NO_ENGINE;
                }
                CPYMO_S60_TRACE("[S60] Font restart: rebuilding the game selector.\n");
            }
            items = get_game_list(GAME_SELECTOR_DIR);
            last_selected = get_last_selected_game_dir();
            err = cpymo_engine_init_with_game_selector(
                &engine, SCREEN_WIDTH, SCREEN_HEIGHT,
                GAME_SELECTOR_FONTSIZE, GAME_SELECTOR_EMPTY_MSG_FONTSIZE,
                GAME_SELECTOR_COUNT_PER_SCREEN, &items, NULL,
                &after_start_game, &last_selected);
            if (err != CPYMO_ERR_SUCC) {
                cpymo_game_selector_item_free_all(items);
                free(last_selected); ret = -1; goto EXIT_NO_ENGINE;
            }
            s60_engine_live = true;
            s60_in_selector = true;
            s60_exit_prompt_pending = s60_exit_prompt_open = false;
            set_clip_rect(SCREEN_WIDTH, SCREEN_HEIGHT);
            SDL_WM_SetCaption("CPyMO", "CPyMO");
            if (s60_game_load_failed) {
                s60_game_load_failed = false;
                cpymo_msgbox_ui_enter(&engine,
                    cpymo_str_pure("无法打开游戏，已返回列表。"), NULL, NULL);
            }
            prev_time = SDL_GetTicks();
            continue;
        }
        if (s60_exit_prompt_pending && !s60_exit_prompt_open) {
            s60_exit_prompt_pending = false;
            s60_exit_prompt_open = true;
            const int decision = s60_in_selector ? cpymo_s60_native_confirm_exit(0) : -1;
            if (decision >= 0) {
                s60_exit_prompt_open = false;
                if (decision) {
                    printf("[S60] normal application exit requested\n");
                    goto EXIT;
                }
                cur_time = SDL_GetTicks();
            } else {
                err = cpymo_msgbox_ui_enter(&engine,
                    cpymo_str_pure(s60_in_selector ? "退出 CPyMO？" : "退出 CPyMO？请先保存进度。"),
                    &s60_exit_confirm, NULL);
                if (err != CPYMO_ERR_SUCC) s60_exit_prompt_open = false;
                else if (s60_in_selector) cpymo_msgbox_ui_enable_button_outline(&engine);
            }
            redraw = true;
        }
#endif
        if (err != CPYMO_ERR_SUCC) {
            if (err == CPYMO_ERR_NO_MORE_CONTENT) goto EXIT;
            printf("[Error] cpymo_engine_update: %s\n", cpymo_error_message(err));
            ret = -1;
            goto EXIT;
        }

        if (redraw || redraw_system) 
        {
#ifdef S60V3
            draw_s60_frame();
#else
            SDL_FillRect(framebuffer, NULL, 0);
            cpymo_engine_draw(&engine);
#endif
            SDL_Flip(framebuffer);
#ifdef REDRAW_WHAT_EVER
            SDL_Delay(16);
#endif
        }
        else 
        {
            SDL_Delay(16);
        }

        prev_time = cur_time;
    }

EXIT:
#ifdef S60V3
    if (s60_exit_requested) printf("[S60] normal application exit requested\n");
    free(s60_start_requested);
    s60_start_requested = NULL;
    if (s60_engine_live) cpymo_engine_free(&engine);
#else
    cpymo_engine_free(&engine);
#endif
#ifdef S60V3
EXIT_NO_ENGINE:
    release_restore_scene();
#endif
    cpymo_backend_image_quit();
    cpymo_backend_audio_free();
    cpymo_backend_font_free();
    SDL_Quit();
#ifdef S60V3
    cpymo_s60_exit_stage(1);
    printf("[S60] SDL_main returning %d after cleanup\n", ret);
    fflush(stdout);
    cpymo_s60_exit_stage(2);
#endif

    #ifdef LEAKCHECK
    stb_leakcheck_dumpmem();
    #endif

    return ret;
}


