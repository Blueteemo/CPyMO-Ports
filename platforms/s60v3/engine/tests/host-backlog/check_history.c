#include <SDL/SDL.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpymo/cpymo_engine.h"
#include "cpymo/cpymo_config_ui.h"
#include "cpymo/cpymo_localization.h"
#include "cpymo/cpymo_backlog.h"
#include "cpymo/cpymo_rmenu.h"
#include "cpymo/cpymo_save_ui.h"
#include "cpymo/cpymo_list_ui.h"
#include "cpymo/cpymo_ui.h"
#include "cpymo-backends/sdl1/cpymo_s60_text_metrics.h"
#include "cpymo-backends/sdl1/cpymo_s60_native_font.h"

extern cpymo_engine engine;
extern SDL_Surface *framebuffer;
extern error_t cpymo_backend_font_init(const char *);
extern void cpymo_backend_font_free(void);
extern error_t __real_cpymo_backend_text_create(cpymo_backend_text *,
    float *, cpymo_str, float);
static int ellipsized;
static char cursor_marker;
static int cursor_draws;
static float cursor_rect[4];
static bool capture_settings;
static const char *speed_value_to_capture;
static cpymo_backend_text speed_text;
static float value_center, value_left, value_right;
static float left_button_right, right_button_left, left_button_start, right_button_end;
static int settings_row;
static int speed_draws, buttons_drawn;
static const char *clock_fixture_time;
static float clock_panel[4];
static bool capture_saves;
static struct {
    size_t count;
    float top[4], bottom[4], baseline[4];
} save_rows[3];

size_t __real_strftime(char *, size_t, const char *, const struct tm *);
size_t __wrap_strftime(char *output, size_t size, const char *fmt, const struct tm *local)
{
    if (clock_fixture_time && strcmp(fmt, "%H:%M") == 0 && size >= 6) {
        memcpy(output, clock_fixture_time, 6);
        return 5;
    }
    return __real_strftime(output, size, fmt, local);
}

void __real_cpymo_backend_text_draw(cpymo_backend_text, float, float,
    cpymo_color, float, enum cpymo_backend_image_draw_type);
void __real_cpymo_backend_image_fill_rects(const float *, size_t, cpymo_color,
    float, enum cpymo_backend_image_draw_type);

void __wrap_cpymo_backend_text_draw(cpymo_backend_text text, float x, float y,
    cpymo_color color, float alpha, enum cpymo_backend_image_draw_type type)
{
    if (capture_settings && text == speed_text) {
        float ink[4];
        cpymo_s60_text_ink(text, ink);
        value_left = x + ink[0];
        value_right = x + ink[2];
        value_center = (value_left + value_right) * 0.5f;
        ++speed_draws;
    }
    if (capture_saves && x == 8.0f && y >= 0 && y < 240 &&
        type == cpymo_backend_image_draw_type_ui_element) {
        const size_t row = (size_t)(y / 80.0f);
        const size_t line = save_rows[row].count++;
        assert(line < 4);
        float ink[4];
        cpymo_s60_text_ink(text, ink);
        save_rows[row].top[line] = y + ink[1];
        save_rows[row].bottom[line] = y + ink[3];
        save_rows[row].baseline[line] = y;
    }
    __real_cpymo_backend_text_draw(text, x, y, color, alpha, type);
}

void __wrap_cpymo_backend_image_fill_rects(const float *xywh, size_t count, cpymo_color color,
    float alpha, enum cpymo_backend_image_draw_type type)
{
    if (clock_fixture_time && clock_panel[2] == 0 &&
        type == cpymo_backend_image_draw_type_ui_element_bg && count == 1)
        memcpy(clock_panel, xywh, sizeof(clock_panel));

    const float row_center = (settings_row + 0.5f) * (240.0f / 7.0f);
    if (capture_settings && count == 1 && type == cpymo_backend_image_draw_type_ui_element &&
        fabsf(xywh[1] + (xywh[3] * 0.5f) - row_center) < 0.5f) {
        if (xywh[0] + xywh[2] < left_button_right) {
            left_button_start = xywh[0];
            left_button_right = xywh[0] + xywh[2];
        }
        if (xywh[0] > right_button_left) {
            right_button_left = xywh[0];
            right_button_end = xywh[0] + xywh[2];
        }
        ++buttons_drawn;
    }
    __real_cpymo_backend_image_fill_rects(xywh, count, color, alpha, type);
}

void __real_cpymo_backend_image_draw(float, float, float, float, cpymo_backend_image,
    int, int, int, int, float, enum cpymo_backend_image_draw_type);

void __wrap_cpymo_backend_image_draw(float x, float y, float w, float h, cpymo_backend_image image,
    int sx, int sy, int sw, int sh, float alpha, enum cpymo_backend_image_draw_type type)
{
    if (clock_fixture_time && clock_panel[2] == 0 && type == cpymo_backend_image_draw_type_ui_element_bg) {
        clock_panel[0] = x;
        clock_panel[1] = y;
        clock_panel[2] = w;
        clock_panel[3] = h;
    }
    if (image == &cursor_marker) {
        assert(type == cpymo_backend_image_draw_type_text_say);
        assert(sx == 0 && sy == 0 && sw == 30 && sh == 18 && alpha == 1.0f);
        cursor_rect[0] = x;
        cursor_rect[1] = y;
        cursor_rect[2] = w;
        cursor_rect[3] = h;
        ++cursor_draws;
        return;
    }
    __real_cpymo_backend_image_draw(x, y, w, h, image, sx, sy, sw, sh, alpha, type);
}

error_t __wrap_cpymo_backend_text_create(cpymo_backend_text *out, float *width,
    cpymo_str text, float size)
{
    error_t result = __real_cpymo_backend_text_create(out, width, text, size);
    if (result == CPYMO_ERR_SUCC && strstr(text.begin, "…")) ++ellipsized;
    if (result == CPYMO_ERR_SUCC && speed_value_to_capture &&
        text.len == strlen(speed_value_to_capture) &&
        memcmp(text.begin, speed_value_to_capture, text.len) == 0)
        speed_text = *out;
    return result;
}

static void add_record(const char *name, const char *text, float size)
{
    cpymo_backend_text rendered = NULL;
    float width;
    assert(cpymo_s60_dialogue_text_create(&rendered, &width,
        cpymo_str_pure(name), size) == CPYMO_ERR_SUCC);
    cpymo_backlog_record_write_name(&engine.backlog, rendered, cpymo_str_pure(name));
    char *owned = strdup(text);
    assert(owned);
    assert(cpymo_backlog_record_write_text(&engine.backlog, owned, size) == CPYMO_ERR_SUCC);
}

static int ink_rows(int y0, int y1)
{
    int count = 0;
    assert(SDL_LockSurface(framebuffer) == 0);
    for (int y = y0; y < y1; ++y) {
        const Uint16 *row = (const Uint16 *)((const Uint8 *)framebuffer->pixels +
            y * framebuffer->pitch);
        for (int x = 0; x < framebuffer->w; ++x) {
            Uint8 red, green, blue;
            SDL_GetRGB(row[x], framebuffer->format, &red, &green, &blue);
            if (red>220 && green>220 && blue>220) {
                ++count;
                break;
            }
        }
    }
    SDL_UnlockSurface(framebuffer);
    return count;
}

static void check_cursor_at_sizes(float box_width, float box_height, float x, float y)
{
    const cpymo_color white = {255, 255, 255};
    engine.say.msg_cursor = &cursor_marker;
    engine.say.msg_cursor_w = 30;
    engine.say.msg_cursor_h = 18;
    for (int size = 12; size <= 32; ++size) {
        cpymo_textbox textbox;
        assert(cpymo_textbox_init(&textbox, 8, 180, box_width, box_height, (float)size,
            white, 1.0f, cpymo_str_pure(""), NULL) == CPYMO_ERR_SUCC);
        cpymo_textbox_finalize(&textbox);
        cursor_draws = 0;
        cpymo_textbox_draw(&engine, &textbox, cpymo_backend_image_draw_type_text_say);
        assert(cursor_draws == 1);
        assert(fabsf(cursor_rect[0] - x) < 0.01f && fabsf(cursor_rect[1] - y) < 0.01f);
        assert(fabsf(cursor_rect[2] - 16.0f) < 0.01f && fabsf(cursor_rect[3] - 9.6f) < 0.01f);

        assert(!cpymo_textbox_wait_text_reading(&engine, 0.5f, &textbox));
        cpymo_textbox_draw(&engine, &textbox, cpymo_backend_image_draw_type_text_say);
        assert(cursor_draws == 1);
        assert(!cpymo_textbox_wait_text_reading(&engine, 0.5f, &textbox));
        cpymo_textbox_draw(&engine, &textbox, cpymo_backend_image_draw_type_text_say);
        assert(cursor_draws == 2);

        engine.say.auto_mode = true;
        cpymo_textbox_draw(&engine, &textbox, cpymo_backend_image_draw_type_text_say);
        assert(cursor_draws == 2);
        engine.say.auto_mode = false;
        cpymo_textbox_free(&textbox, NULL);
    }
    engine.say.msg_cursor = NULL;
}

static void check_dialogue_profile(int profile)
{
    assert(cpymo_s60_save_font_profile(profile));
    cpymo_backend_font_free();
    assert(cpymo_backend_font_init(NULL) == CPYMO_ERR_SUCC);
    assert(cpymo_s60_active_font_profile() == profile);

    const cpymo_str samples = cpymo_str_pure("她的回答");
    cpymo_backend_text rendered;
    float width;
    assert(cpymo_s60_dialogue_text_create(&rendered, &width, samples, 16) == CPYMO_ERR_SUCC);
    if (profile == CPYMO_FONT_COMBINED) {
        assert(cpymo_s60_font_baseline(16, true) == 14.0f);
        assert(width == 64.0f);
    }
    float bounds[4];
    cpymo_s60_text_ink(rendered, bounds);
    assert(bounds[2] > bounds[0] && bounds[3] > bounds[1]);
    cpymo_backend_text_free(rendered);

    check_cursor_at_sizes(302, 46, 294.0f, 199.2f);
    check_cursor_at_sizes(7, 11, 16.0f, 183.2f);
    printf("profile %d: real dialogue glyphs, fixed cursor for 12-32 px (normal and narrow/shallow), blink and auto-mode\n", profile);
}

static void tap_setting_key(bool right)
{
    engine.input.right = right;
    engine.input.down = !right;
    assert(cpymo_ui_update(&engine, 0.016f) == CPYMO_ERR_SUCC);
    engine.prev_input = engine.input;
    engine.input.right = false;
    engine.input.down = false;
    assert(cpymo_ui_update(&engine, 0.016f) == CPYMO_ERR_SUCC);
    engine.prev_input = engine.input;
}

static void check_setting_value_alignment(int row)
{
    assert(speed_text);
    speed_draws = buttons_drawn = 0;
    left_button_right = 1e6f;
    right_button_left = 0;
    settings_row = row;
    capture_settings = true;
    assert(SDL_FillRect(framebuffer, NULL, SDL_MapRGB(framebuffer->format, 36, 51, 60)) == 0);
    cpymo_ui_draw(&engine);
    capture_settings = false;

    assert(speed_draws == 1 && buttons_drawn == 2);
    const float group_center = (left_button_start + right_button_end) * 0.5f;
    const float gap_center = (left_button_right + right_button_left) * 0.5f;
    printf("config %dpx row %d %s: buttons %.2f / %.2f, gap center %.2f, glyph center %.2f\n",
        engine.gameconfig.fontsize, row, speed_value_to_capture,
        left_button_start, right_button_left, gap_center, value_center);
    fflush(stdout);
    assert(fabsf(right_button_end - 304.0f) < 0.01f);
    assert(fabsf(right_button_left - 278.4f) < 0.01f);
    assert(fabsf(group_center - gap_center) < 0.01f);
    assert(fabsf(value_center - (gap_center - 2.0f)) <= 0.51f);
    assert(value_left >= left_button_right && value_right <= right_button_left);
}

static void check_speed_alignment(int font_size)
{
    const cpymo_localization *l = cpymo_localization_get(&engine);
    engine.assetloader.gamedir = ".";
    engine.gameconfig.fontsize = font_size;
    memcpy(engine.gameconfig.platform, "s60v3", 6);
    engine.gameconfig.textspeed = 3;
    speed_value_to_capture = l->config_sayspeeds[3];
    assert(cpymo_config_ui_enter(&engine) == CPYMO_ERR_SUCC);
    for (int row = 0; row < 3; ++row) tap_setting_key(false);
    assert(cpymo_list_ui_encode_uint_node_dec(cpymo_list_ui_get_current_selected_const(&engine)) == 3);

    float original_left = 0, original_right = 0;
    for (int step = 0; step < 7; ++step) {
        const int speed = (3 + step) % 6;
        speed_value_to_capture = l->config_sayspeeds[speed];
        if (step) tap_setting_key(true);
        assert(engine.gameconfig.textspeed == (unsigned)speed);
        check_setting_value_alignment(3);
        if (step == 0) {
            original_left = left_button_start;
            original_right = right_button_end;
        } else {
            assert(left_button_start == original_left && right_button_end == original_right);
        }
        if (font_size == 16 && step < 2) {
            char name[32];
            sprintf(name, "config-speed-%d.bmp", speed);
            assert(SDL_SaveBMP(framebuffer, name) == 0);
        }
    }

    tap_setting_key(false);
    assert(cpymo_list_ui_encode_uint_node_dec(cpymo_list_ui_get_current_selected_const(&engine)) == 4);
    /* The 32 px setting wraps to 12, so both extreme values exercise refresh. */
    char label[8];
    sprintf(label, "%d", font_size == 32 ? 12 : font_size + 1);
    speed_value_to_capture = label;
    tap_setting_key(true);
    assert(engine.gameconfig.fontsize == (unsigned)atoi(label));
    check_setting_value_alignment(4);

    cpymo_ui_exit(&engine);
    speed_value_to_capture = NULL;
    speed_text = NULL;
}


static void check_volume_alignment(int font_size)
{
    const size_t channels[] = {CPYMO_AUDIO_CHANNEL_BGM, CPYMO_AUDIO_CHANNEL_SE, CPYMO_AUDIO_CHANNEL_VO};
    engine.assetloader.gamedir = ".";
    engine.gameconfig.fontsize = font_size;
    for (int row = 0; row < 3; ++row) cpymo_audio_set_channel_volume(channels[row], &engine.audio, 0.9f);
    assert(cpymo_config_ui_enter(&engine) == CPYMO_ERR_SUCC);

    for (int row = 0; row < 3; ++row) {
        if (row > 0) tap_setting_key(false);
        assert(cpymo_list_ui_encode_uint_node_dec(cpymo_list_ui_get_current_selected_const(&engine)) == (unsigned)row);
        float initial_left = 0, initial_right = 0;
        for (int step = 0; step < 11; ++step) {
            const int setting = (10 + step) % 11;
            char label[8];
            sprintf(label, "%d%%", setting * 10);
            speed_value_to_capture = label;
            speed_text = NULL;
            tap_setting_key(true);
            assert(fabsf(cpymo_audio_get_channel_volume(channels[row], &engine.audio) - setting * 0.1f) < 0.01f);
            check_setting_value_alignment(row);
            if (step == 0) {
                initial_left = left_button_start;
                initial_right = right_button_end;
            } else {
                assert(left_button_start == initial_left && right_button_end == initial_right);
            }
        }
    }
    speed_value_to_capture = NULL;
    speed_text = NULL;
    cpymo_ui_exit(&engine);
}

static void capture_volume_example(void)
{
    engine.gameconfig.fontsize = 16;
    cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_BGM, &engine.audio, 1.0f);
    cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_SE, &engine.audio, 0.5f);
    cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_VO, &engine.audio, 0.9f);
    assert(cpymo_config_ui_enter(&engine) == CPYMO_ERR_SUCC);
    assert(SDL_FillRect(framebuffer, NULL, SDL_MapRGB(framebuffer->format, 36, 51, 60)) == 0);
    cpymo_ui_draw(&engine);
    assert(SDL_SaveBMP(framebuffer, "config-volumes.bmp") == 0);
    cpymo_ui_exit(&engine);
}

static void check_clock_insets(void)
{
    const char *times[] = {"11:11", "20:38", "07:52"};
    const char *fixtures[] = {"menu-fixtures/bare", "menu-fixtures/skin"};
    engine.gameconfig.fontsize = 16;
    engine.assetloader.game_config = &engine.gameconfig;
    for (int i = 0; i < 2; ++i) {
        engine.assetloader.gamedir = fixtures[i];
        for (int t = 0; t < 3; ++t) {
            clock_fixture_time = times[t];
            memset(clock_panel, 0, sizeof(clock_panel));
            assert(cpymo_rmenu_enter(&engine) == CPYMO_ERR_SUCC);
            assert(SDL_FillRect(framebuffer, NULL, SDL_MapRGB(framebuffer->format, 36, 51, 60)) == 0);
            cpymo_ui_draw(&engine);
            assert(clock_panel[2] > 90 && clock_panel[3] > 0);

            const int panel_x = (int)clock_panel[0], panel_y = (int)clock_panel[1];
            const int header_end = panel_y + (int)(clock_panel[3] * 0.17f);
            int glyph_x = panel_x + 90, glyph_y = header_end;
            assert(SDL_LockSurface(framebuffer) == 0);
            const Uint16 background = ((const Uint16 *)((const Uint8 *)framebuffer->pixels +
                panel_y * framebuffer->pitch))[panel_x];
            for (int y = panel_y; y < header_end; ++y) {
                const Uint16 *pixels = (const Uint16 *)((const Uint8 *)framebuffer->pixels +
                    y * framebuffer->pitch);
                for (int x = panel_x; x < panel_x + 90; ++x) {
                    if (pixels[x] == background) continue;
                    if (x < glyph_x) glyph_x = x;
                    if (y < glyph_y) glyph_y = y;
                }
            }
            SDL_UnlockSurface(framebuffer);
            printf("clock %s panel %dx%d text %s: left %d px, top %d px\n",
                fixtures[i], (int)clock_panel[2], (int)clock_panel[3], times[t],
                glyph_x - panel_x, glyph_y - panel_y);
            fflush(stdout);
            assert(glyph_x - panel_x == 8 && glyph_y - panel_y == 8);
            if (i == 1 && t == 1) assert(SDL_SaveBMP(framebuffer, "rmenu-clock.bmp") == 0);
            cpymo_ui_exit(&engine);
        }
    }
    clock_fixture_time = NULL;
}

static void check_save_preview_spacing(int font_size, bool loading)
{
    engine.gameconfig.fontsize = font_size;
    engine.assetloader.gamedir = "save-fixture";
    engine.assetloader.game_config = &engine.gameconfig;
    assert(cpymo_save_ui_enter(&engine, loading) == CPYMO_ERR_SUCC);
    assert(cpymo_list_ui_row_height(&engine) == 80.0f);
    memset(save_rows, 0, sizeof(save_rows));
    capture_saves = true;
    assert(SDL_FillRect(framebuffer, NULL, SDL_MapRGB(framebuffer->format, 36, 51, 60)) == 0);
    cpymo_ui_draw(&engine);
    capture_saves = false;

    assert(save_rows[0].count == (loading ? 4 : 3));
    assert(save_rows[1].count == (loading ? 3 : 4));
    assert(save_rows[2].count == (loading ? 4 : 1));
    const float preview_start = save_rows[0].baseline[1];
    if (font_size == 16) assert(preview_start == 38.0f);
    for (size_t row = 0; row < 3; ++row) {
        const float start = row * 80.0f;
        const float step = fminf(font_size, (80.0f - 9.0f) / 4.0f) + 2.0f;
        assert(save_rows[row].baseline[0] == start + step);
        if (save_rows[row].count == 1) continue;
        assert(save_rows[row].baseline[save_rows[row].count - 1] == start + 75.0f);
        assert(save_rows[row].baseline[1] - start == preview_start);
        if (save_rows[row].count == 3) {
            printf("%s %d px row %u: single-line preview baseline %.2f px\n",
                loading ? "load" : "save", font_size, (unsigned)row, preview_start);
            continue;
        }
        assert(save_rows[row].baseline[2] - save_rows[row].baseline[1] == step);
        const float above = save_rows[row].top[1] - save_rows[row].bottom[0];
        const float below = save_rows[row].top[3] - save_rows[row].bottom[2];
        printf("%s %d px row %u: two-line preview baseline %.2f px, upper %.2f px, lower %.2f px\n",
            loading ? "load" : "save", font_size, (unsigned)row, preview_start, above, below);
        fflush(stdout);
        assert(above >= 0.0f && below >= 0.0f);
    }

    if (font_size == 16)
        assert(SDL_SaveBMP(framebuffer, loading ? "load-preview.bmp" : "save-preview.bmp") == 0);
    cpymo_ui_exit(&engine);
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16,
        0xf800, 0x07e0, 0x001f, 0);
    assert(framebuffer);
    assert(cpymo_backend_font_init(NULL) == CPYMO_ERR_SUCC);
    engine.gameconfig.imagesize_w = 320;
    engine.gameconfig.imagesize_h = 240;
    assert(cpymo_backlog_init(&engine.backlog) == CPYMO_ERR_SUCC);

    add_record("测试", "第一行较长，后面还有五行。\n第二行正文继续。\n第三行正文继续。\n第四行正文继续。\n第五行正文继续。\n第六行正文继续。", 16);
    add_record("测试", "这一条只有一行。", 16);
    add_record("测试", "大字号第一行\n大字号第二行\n大字号第三行", 32);
    assert(cpymo_backlog_ui_enter(&engine) == CPYMO_ERR_SUCC);
    printf("row height at 320x240: %.0f px\n", cpymo_list_ui_row_height(&engine));
    assert(cpymo_list_ui_row_height(&engine)==80.0f);
    cpymo_ui_draw(&engine);
    assert(ellipsized >= 2);
    assert(ink_rows(3,76)>0 && ink_rows(83,156)>0 && ink_rows(163,236)>0);
    assert(ink_rows(78,82)==0 && ink_rows(158,162)==0);
    assert(SDL_SaveBMP(framebuffer, "history-3-rows.bmp")==0);
    printf("three nonempty rows; two long entries use a preview; gaps between rows have no glyph pixels\n");

    cpymo_ui_exit(&engine);
    cpymo_backlog_free(&engine.backlog);
    check_dialogue_profile(CPYMO_FONT_COMBINED);
    check_dialogue_profile(CPYMO_FONT_SMOOTH);
    check_speed_alignment(12);
    check_speed_alignment(16);
    check_speed_alignment(22);
    check_speed_alignment(32);
    check_volume_alignment(12);
    check_volume_alignment(16);
    check_volume_alignment(22);
    check_volume_alignment(32);
    capture_volume_example();
    check_clock_insets();
    check_save_preview_spacing(12, true);
    check_save_preview_spacing(16, true);
    check_save_preview_spacing(32, true);
    check_save_preview_spacing(16, false);
    cpymo_backend_font_free();
    SDL_FreeSurface(framebuffer);
    SDL_Quit();
    return 0;
}
