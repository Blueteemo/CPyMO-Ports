#include "cpymo_prelude.h"
#include "cpymo_game_selector.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include "cpymo_localization.h"
#include "../stb/stb_image.h"
#include <assert.h>
#include <string.h>
#ifdef S60V3
#include "../cpymo-backends/sdl1/cpymo_s60_app.h"
#include "../cpymo-backends/sdl1/cpymo_s60_text_metrics.h"
#include "../stb/stb_image_resize.h"
enum { S60_ICON_SIZE = 54, S60_ICON_SLOT = 56, S60_SCROLL_GUTTER = 10 };
#endif

typedef struct {
	cpymo_game_selector_item *items;
	cpymo_game_selector_callback before_init, after_init;
	size_t draw_times;
} cpymo_game_selector;

#ifdef ENABLE_TEXT_EXTRACT
static error_t cpymo_game_selector_visual_im_help_selection_changed_callback(cpymo_engine *e, void *sel)
{
	if (sel) {
		cpymo_game_selector_item *item = (cpymo_game_selector_item *)sel;
		cpymo_backend_text_extract(item->gametitle_text);
	}
	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_game_selector_init_refresh(cpymo_engine *e, float dt, void *selected)
{
	cpymo_game_selector *ui = (cpymo_game_selector *)cpymo_list_ui_data(e);
	cpymo_engine_request_redraw(e);

	if (ui->draw_times) {
		ui->draw_times--;
	}
	else {
		cpymo_list_ui_set_custom_update(e, NULL);
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_game_selector_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	cpymo_game_selector_item *item = (cpymo_game_selector_item *)node_to_draw;
	
#ifdef S60V3
    const float center_y = y + cpymo_list_ui_row_height(e) * 0.5f;
    if (item->icon) {
        cpymo_backend_image_draw(4 + (S60_ICON_SLOT - item->icon_w) / 2.0f,
            center_y - item->icon_h * 0.5f, item->icon_w, item->icon_h,
            item->icon, 0, 0, item->icon_w, item->icon_h, 1.0f,
            cpymo_backend_image_draw_type_ui_element);
    }
    const float x = item->icon ? S60_ICON_SLOT + 8 : 12;
    const float baseline = center_y - item->gametitle_ink_center_y;
    if (item->gametitle)
        cpymo_backend_text_draw(item->gametitle, x, baseline, cpymo_color_white, 1.0f,
            cpymo_backend_image_draw_type_ui_element);
    if (item->game_subtitle)
        cpymo_backend_text_draw(item->game_subtitle, x, baseline + 16.0f, cpymo_color_white, 0.85f,
            cpymo_backend_image_draw_type_ui_element);
#else
	float game_icon_size = 3 * (float)e->gameconfig.fontsize;
	if (item->icon) {
		cpymo_backend_image_draw(16, y + 6, game_icon_size, game_icon_size, item->icon, 0, 0,
			item->icon_w, item->icon_h, 1.0f, cpymo_backend_image_draw_type_ui_element);
	}

	if (item->gametitle) {
		cpymo_backend_text_draw(
			item->gametitle, 28 + game_icon_size, y + (float)e->gameconfig.fontsize, cpymo_color_white, 1.0f,
			cpymo_backend_image_draw_type_ui_element);
	}
#endif
}

static error_t cpymo_game_selector_ok(cpymo_engine *e, void *selected)
{
	cpymo_game_selector_item *item = (cpymo_game_selector_item *)selected;
	char *gamedir = item->gamedir;
	item->gamedir = NULL;
#ifdef S60V3
    /* Do not destroy the active list/update stack inside its own callback. */
    cpymo_s60_request_game_start(gamedir);
    return CPYMO_ERR_SUCC;
#else
	cpymo_game_selector *sel = (cpymo_game_selector *)cpymo_list_ui_data(e);

	if (sel->before_init) {
		error_t err = sel->before_init(e, gamedir);
		if (err != CPYMO_ERR_SUCC) {
			free(gamedir);
			return err;
		}
	}

	cpymo_game_selector_callback after = sel->after_init;

	cpymo_engine_free(e);
	error_t err = cpymo_engine_init(e, gamedir);
	free(gamedir);

	CPYMO_THROW(err);

	if (after) {
		err = after(e, e->assetloader.gamedir);
		CPYMO_THROW(err);
	}
	
	return err;
#endif
}

static void cpymo_game_selector_delete(cpymo_engine *e, void *ui_data)
{
	cpymo_game_selector *ui = (cpymo_game_selector *)ui_data;
	cpymo_game_selector_item_free_all(ui->items);
}

static void *cpymo_game_selector_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_game_selector_item *item = (const cpymo_game_selector_item *)cur;
	return item->next;
}

static void *cpymo_game_selector_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_game_selector_item *item = (const cpymo_game_selector_item *)cur;
	return item->prev;
}

#ifdef S60V3
static unsigned char *load_pymo_icon_mask(void)
{
    /* Prefer the drives normally used on S60, then look for other installs. */
    const char drives[] = "CEABDFGHIJKLMNOPQRSTUVWXYZ";
    char path[] = "C:/data/pymo/icon_mask.png";
    for (size_t i = 0; drives[i]; ++i) {
        int width, height;
        path[0] = drives[i];
        unsigned char *mask = stbi_load(path, &width, &height, NULL, 1);
        if (mask && width == 57 && height == 57) return mask;
        stbi_image_free(mask);
    }
    return NULL;
}

static void mask_opaque_icon(unsigned char *rgba, int width, int height,
    const unsigned char *pymo_mask)
{
    for (size_t i = 0; i < (size_t)width * height; ++i)
        if (rgba[i * 4 + 3] != 255) return;

    if (!pymo_mask && (width < 16 || height < 16)) return;
    for (int y = 0; y < height; ++y) {
        const int mask_y = y * 57 / height;
        for (int x = 0; x < width; ++x) {
            const int mask_x = x * 57 / width;
            unsigned char alpha;
            if (pymo_mask) {
                alpha = pymo_mask[mask_y * 57 + mask_x];
            } else {
                /* Rounded corners and a subtle fade for standalone installs. */
                const int dx = mask_x < 7 ? 7 - mask_x : mask_x > 49 ? mask_x - 49 : 0;
                const int dy = mask_y < 7 ? 7 - mask_y : mask_y > 49 ? mask_y - 49 : 0;
                const int distance = dx * dx + dy * dy;
                alpha = distance > 54 ? 0 : distance > 42 ? 112 : 255 - mask_y * 64 / 56;
            }
            rgba[((size_t)y * width + x) * 4 + 3] = alpha;
        }
    }
}

static void load_s60_icon(cpymo_game_selector_item *item, const unsigned char *pymo_mask)
{
    void *pixels = NULL;
    int width = 0, height = 0;
    if (cpymo_assetloader_load_icon_pixels(&pixels, &width, &height, item->gamedir)
            != CPYMO_ERR_SUCC) return;

    mask_opaque_icon(pixels, width, height, pymo_mask);
    /* Resample only once and filter RGB with the alpha channel to avoid halos. */
    const int max_side = S60_ICON_SIZE;
    int side = width > height ? width : height;
    if (side > max_side) {
        int w = (int)((long long)width * max_side / side);
        int h = (int)((long long)height * max_side / side);
        if (!w) w = 1;
        if (!h) h = 1;
        unsigned char *small = malloc((size_t)w * h * 4);
        if (!small) { free(pixels); return; }
        if (!stbir_resize_uint8_srgb(pixels, width, height, 0, small, w, h, 0, 4, 3, 0)) {
            free(small);
            free(pixels);
            return;
        }
        free(pixels);
        pixels = small;
        width = w;
        height = h;
    }
    if (cpymo_backend_image_load(&item->icon, pixels, width, height,
            cpymo_backend_image_format_rgba) != CPYMO_ERR_SUCC) {
        free(pixels);
        return;
    }
    item->icon_w = width;
    item->icon_h = height;
}

/* Wrap fixed-size UTF-8 lines to the actual text column. */
static cpymo_str selector_line(cpymo_str *text, float size, float width)
{
    cpymo_str line = {0};
    line.begin = text->begin;
    float used = 0;
    while (text->len && text->begin[0] != '\n') {
        cpymo_str rest = *text;
        cpymo_str glyph = cpymo_str_utf8_try_head(&rest);
        if (!glyph.len) break;
        used += cpymo_backend_text_width(glyph, size);
        if (used > width && line.len) break;
        line.len += glyph.len;
        *text = rest;
    }
    if (text->len && text->begin[0] == '\n') {
        ++text->begin;
        --text->len;
    }
    return line;
}

static void load_s60_title(cpymo_game_selector_item *item, cpymo_str text, float title_size)
{
    const float width = 320.0f - S60_SCROLL_GUTTER - (item->icon ? S60_ICON_SLOT + 8 : 12);
    cpymo_str full_title = text;
    const char *newline = memchr(text.begin, '\n', text.len);
    if (newline) full_title.len = newline - text.begin;
    while (title_size > 16.0f && cpymo_backend_text_width(full_title, title_size) > width)
        title_size -= 1.0f;
    const cpymo_str title = selector_line(&text, title_size, width);
    const cpymo_str first = selector_line(&text, 14.0f, width);
    const cpymo_str second = selector_line(&text, 14.0f, width);
    char subtitle[257];
    memcpy(subtitle, first.begin, first.len);
    size_t count = first.len;
    if (second.len) {
        if (count) subtitle[count++] = '\n';
        memcpy(subtitle + count, second.begin, second.len);
        count += second.len;
    }
    subtitle[count] = '\0';

    cpymo_backend_text_create(&item->gametitle, &item->gametitle_w, title, title_size);
    if (!item->gametitle) return;
    float ink[4];
    cpymo_s60_text_ink(item->gametitle, ink);
    float bottom = ink[3];
    if (count) {
        float unused;
        cpymo_backend_text_create(&item->game_subtitle, &unused, cpymo_str_pure(subtitle), 14.0f);
        if (item->game_subtitle) {
            float subtitle_ink[4];
            cpymo_s60_text_ink(item->game_subtitle, subtitle_ink);
            bottom = 16.0f + subtitle_ink[3];
        }
    }
    item->gametitle_ink_center_y = (ink[1] + bottom) * 0.5f;
}

#endif

static void cpymo_game_selector_item_load_info(cpymo_game_selector_item *item, float fontsize)
{
#define FAIL { if (item->gamedir) free(item->gamedir); item->gamedir = NULL; prev = item; item = item->next; continue; }

	cpymo_game_selector_item *prev = NULL;
	char *path = NULL;
#ifdef S60V3
    unsigned char *pymo_mask = item ? load_pymo_icon_mask() : NULL;
#endif
	while (item) {
		item->prev = prev;
		char *next_path = (char *)realloc(path, strlen(item->gamedir) + 24);
		if (next_path == NULL) FAIL;

		path = next_path;

		sprintf(path, "%s/gameconfig.txt", item->gamedir);

		cpymo_gameconfig game_config;
		error_t err = cpymo_gameconfig_parse_from_file(&game_config, path);
		if (err != CPYMO_ERR_SUCC) FAIL;

#ifdef ENABLE_TEXT_EXTRACT
        strcpy(item->gametitle_text, game_config.gametitle);
#endif
#ifdef S60V3
        load_s60_icon(item, pymo_mask);
        load_s60_title(item, cpymo_str_pure(game_config.gametitle), fontsize);
#else
		cpymo_backend_text_create(&item->gametitle, &item->gametitle_w,
			cpymo_str_pure(game_config.gametitle), fontsize);
#endif

#ifndef S60V3
		err = cpymo_assetloader_load_icon(
			&item->icon, &item->icon_w, &item->icon_h, item->gamedir);

		if (err != CPYMO_ERR_SUCC)
			item->icon = NULL;
#endif

		prev = item;
		item = item->next;
	}

	if (path) free(path);
#ifdef S60V3
    stbi_image_free(pymo_mask);
#endif
}

static cpymo_game_selector_item *cpymo_game_selector_item_remove_invalid(cpymo_game_selector_item *prev, cpymo_game_selector_item *cur)
{
	if (cur == NULL) return NULL;
	else if (cur->gamedir) {
		cpymo_game_selector_item_remove_invalid(cur, cur->next);
		return cur;
	}
	else {
		cpymo_game_selector_item *next = cur->next;
		if (cur->gametitle) cpymo_backend_text_free(cur->gametitle);
#ifdef S60V3
        if (cur->game_subtitle) cpymo_backend_text_free(cur->game_subtitle);
#endif
		if (cur->icon) cpymo_backend_image_free(cur->icon);
		free(cur);

		if (next) next->prev = prev;
		if (prev) prev->next = next;

		return cpymo_game_selector_item_remove_invalid(prev, next);
	}
}

typedef struct {
	cpymo_backend_text msg1, msg2;
	float msg1_w, msg2_w;
	size_t draw_times;
	float font_size;
} cpymo_game_selector_empty_ui;

static void cpymo_game_selector_empty_ui_draw(const cpymo_engine *e, const void *ui_data)
{
	cpymo_game_selector_empty_ui *ui = (cpymo_game_selector_empty_ui *)ui_data;

	float fontsize = ui->font_size;

	if (ui->msg1) {
		cpymo_backend_text_draw(
			ui->msg1,
			(e->gameconfig.imagesize_w - ui->msg1_w) / 2,
			(e->gameconfig.imagesize_h - fontsize) / 2,
			cpymo_color_white, 0.5f,
			cpymo_backend_image_draw_type_ui_element);
	}

	if (ui->msg2) {
		cpymo_backend_text_draw(
			ui->msg2,
			(e->gameconfig.imagesize_w - ui->msg2_w) / 2,
			(e->gameconfig.imagesize_h - fontsize) / 2 + fontsize + fontsize / 2,
			cpymo_color_white, 0.25f,
			cpymo_backend_image_draw_type_ui_element);
	}
}

static error_t cpymo_game_selector_empty_ui_update(cpymo_engine *e, void *ui_data, float dt)
{
	cpymo_game_selector_empty_ui *ui = (cpymo_game_selector_empty_ui *)ui_data;
	if (ui->draw_times) {
		cpymo_engine_request_redraw(e);
		ui->draw_times--;
	}
	return CPYMO_ERR_SUCC;
}

static void cpymo_game_selector_empty_ui_delete(cpymo_engine *e, void *ui_)
{
	cpymo_game_selector_empty_ui *ui = (cpymo_game_selector_empty_ui *)ui_;
	if (ui->msg1) cpymo_backend_text_free(ui->msg1);
	if (ui->msg2) cpymo_backend_text_free(ui->msg2);
}

typedef struct {
	cpymo_game_selector_item *items;
	cpymo_game_selector_callback before_reinit, after_reinit;
	float title_font_size, empty_message_font_size;
	size_t nodes_per_screen;
	char *last_selected_gamedir;
} cpymo_game_selector_lazy_init;

static void cpymo_game_selector_lazy_init_delete(cpymo_engine *e, void *ui_data)
{
	cpymo_game_selector_lazy_init *ui = (cpymo_game_selector_lazy_init *)ui_data;
	if (ui->items) cpymo_game_selector_item_free_all(ui->items);
	if (ui->last_selected_gamedir) free(ui->last_selected_gamedir);
}

static void cpymo_game_selector_lazy_init_draw(const cpymo_engine *e, const void *_) {}

static error_t cpymo_game_selector_lazy_init_update(cpymo_engine *e, void *ui_, float _)
{
	float fontsize = (float)e->gameconfig.fontsize;
	cpymo_game_selector_lazy_init *data = (cpymo_game_selector_lazy_init *)ui_;

	cpymo_game_selector_item_load_info(data->items, data->title_font_size);
	data->items = cpymo_game_selector_item_remove_invalid(NULL, data->items);

	cpymo_game_selector_item *last_selected = data->items;
	while (last_selected && data->last_selected_gamedir) {
		if (strcmp(last_selected->gamedir, data->last_selected_gamedir) == 0) break;
		last_selected = last_selected->next;
	}

	if (data->items) {
		cpymo_game_selector *ui = NULL;
		cpymo_game_selector_item *items = data->items;
		data->items = NULL;
		cpymo_game_selector_callback
			after_reinit = data->after_reinit,
			before_reinit = data->before_reinit;

		int relative = 0;
		if (last_selected) {
			while (last_selected->prev && relative < (int)data->nodes_per_screen - 1) {
				last_selected = last_selected->prev;
				relative++;
			}
		}

		error_t err = cpymo_list_ui_enter(
			e,
			(void **)&ui,
			sizeof(cpymo_game_selector),
			&cpymo_game_selector_draw_node,
			&cpymo_game_selector_ok,
			&cpymo_game_selector_delete,
			last_selected ? last_selected : items,
			&cpymo_game_selector_get_next,
			&cpymo_game_selector_get_prev,
			false,
			data->nodes_per_screen
		);
		CPYMO_THROW(err);

		#ifdef ENABLE_TEXT_EXTRACT
			cpymo_list_ui_set_selection_changed_callback(e, &cpymo_game_selector_visual_im_help_selection_changed_callback);
		#endif

		cpymo_list_ui_set_allow_exit(e, false);
		cpymo_list_ui_set_selection_relative_to_cur(e, relative);

		cpymo_list_ui_enable_loop(e);
		ui->after_init = after_reinit;
		ui->before_init = before_reinit;
#ifdef S60V3
        size_t item_count = 0;
        for (cpymo_game_selector_item *item = items; item; item = item->next)
            ++item_count;
        cpymo_list_ui_set_s60_scrollbar(e, item_count);
        ui->draw_times = 0;
        cpymo_engine_request_redraw(e);
#else
		ui->draw_times = 45;
		cpymo_list_ui_set_custom_update(e, &cpymo_game_selector_init_refresh);
#endif

		if (err != CPYMO_ERR_SUCC) {
			cpymo_game_selector_item_free_all(items);
			return err;
		}

		ui->items = items;

		#if !defined(EXIT_TO_GAME_SELECTOR) && !defined(S60V3)
		if (ui->items != NULL)
			if (ui->items->next == NULL && ui->items->prev == NULL)
				return cpymo_game_selector_ok(e, ui->items);
		#endif

		return CPYMO_ERR_SUCC;
	}
	else {
		cpymo_game_selector_empty_ui *ui = NULL;
		float font_size = data->empty_message_font_size;
		error_t err = cpymo_ui_enter(
			(void **)&ui,
			e,
			sizeof(*ui),
			&cpymo_game_selector_empty_ui_update,
			&cpymo_game_selector_empty_ui_draw,
			&cpymo_game_selector_empty_ui_delete);

		if (err != CPYMO_ERR_SUCC) {
			cpymo_engine_free(e);
			return err;
		}

		ui->msg1 = NULL;
		ui->msg2 = NULL;
		ui->msg1_w = 0;
		ui->msg2_w = 0;
		ui->draw_times = 45;
		ui->font_size = font_size;

		const cpymo_localization *l = cpymo_localization_get(e);

		err = cpymo_backend_text_create(&ui->msg1, &ui->msg1_w,
			cpymo_str_pure(l->game_selector_empty),
			font_size);

		if (err != CPYMO_ERR_SUCC) {
			cpymo_engine_free(e);
			return err;
		}

#if (!defined __PSP__ && !defined __UWP__ && !defined __IOS__)
		err = cpymo_backend_text_create(&ui->msg2, &ui->msg2_w,
			cpymo_str_pure(l->game_selector_empty_secondary),
			font_size / 1.3f);

		if (err != CPYMO_ERR_SUCC) {
			cpymo_engine_free(e);
			return err;
		}
#endif
		
		return CPYMO_ERR_SUCC;
	}
}

error_t cpymo_engine_init_with_game_selector(
	cpymo_engine * e, size_t screen_w, size_t screen_h, size_t fontsize_, 
	float empty_message_font_size,
	size_t nodes_per_screen, cpymo_game_selector_item **gamedirs_movein,
	cpymo_game_selector_callback before_reinit,
	cpymo_game_selector_callback after_reinit,
	char **last_selected_game_dir_movein)
{
	cpymo_audio_init(&e->audio);

	error_t err = cpymo_gameconfig_parse(&e->gameconfig, "", 0);
	CPYMO_THROW(err);

	e->gameconfig.imagesize_w = (uint16_t)screen_w;
	e->gameconfig.imagesize_h = (uint16_t)screen_h;
	e->gameconfig.fontsize = (uint16_t)fontsize_;
#ifdef S60V3
	/* The selector has a native pixel canvas, including its confirmation UI. */
    e->gameconfig.fontsize = (uint16_t)empty_message_font_size;
	memcpy(e->gameconfig.platform, "s60v3", sizeof("s60v3"));
#endif

	e->assetloader.use_pkg_bg = false;
	e->assetloader.use_pkg_chara = false;
	e->assetloader.use_pkg_se = false;
	e->assetloader.use_pkg_voice = false;
	e->assetloader.game_config = &e->gameconfig;
	e->assetloader.gamedir = NULL;
	
	cpymo_vars_init(&e->vars);
	e->interpreter = NULL;
	e->title = NULL;

	cpymo_wait_reset(&e->wait);
	cpymo_flash_reset(&e->flash);
	cpymo_fade_reset(&e->fade);
	cpymo_bg_init(&e->bg);
	cpymo_anime_init(&e->anime);
	cpymo_select_img_init(&e->select_img);
	cpymo_charas_init(&e->charas);
	cpymo_scroll_init(&e->scroll);
	cpymo_floating_hint_init(&e->floating_hint);
	cpymo_say_init(&e->say);
	cpymo_text_init(&e->text);
	cpymo_hash_flags_init(&e->flags);
	e->ui = NULL;
#ifdef S60V3
    e->restore_file = NULL;
    e->restoring = false;
#endif
	cpymo_backlog_init(&e->backlog);
	e->skipping = false;
	e->redraw = true;

	e->input = e->prev_input = cpymo_input_snapshot();

	cpymo_game_selector_lazy_init *d = NULL;
	err = cpymo_ui_enter(
		(void **)&d,
		e,
		sizeof(*d),
		&cpymo_game_selector_lazy_init_update,
		&cpymo_game_selector_lazy_init_draw,
		&cpymo_game_selector_lazy_init_delete);

	if (err != CPYMO_ERR_SUCC) {
		cpymo_engine_free(e);
		return err;
	}

	d->after_reinit = after_reinit;
	d->nodes_per_screen = nodes_per_screen;
    d->title_font_size = fontsize_;
	d->items = *gamedirs_movein;
	d->before_reinit = before_reinit;
	d->empty_message_font_size = empty_message_font_size;
	d->last_selected_gamedir = *last_selected_game_dir_movein;
	*last_selected_game_dir_movein = NULL;
	*gamedirs_movein = NULL;

	return CPYMO_ERR_SUCC;
}

error_t cpymo_game_selector_item_create(cpymo_game_selector_item ** out, char **game_dir_move_in)
{
	*out = (cpymo_game_selector_item *)malloc(sizeof(cpymo_game_selector_item));
	if (*out == NULL) return CPYMO_ERR_OUT_OF_MEM;
	(*out)->prev = NULL;
	(*out)->next = NULL;
	(*out)->gamedir = *game_dir_move_in;
	*game_dir_move_in = NULL;
	(*out)->icon = NULL;
	(*out)->gametitle = NULL;
#ifdef S60V3
    (*out)->game_subtitle = NULL;
#endif
	return CPYMO_ERR_SUCC;
}

void cpymo_game_selector_item_free_all(cpymo_game_selector_item * item)
{
	while (item) {
		if (item->gamedir) free(item->gamedir);
		if (item->icon) cpymo_backend_image_free(item->icon);
		if (item->gametitle) cpymo_backend_text_free(item->gametitle);
#ifdef S60V3
        if (item->game_subtitle) cpymo_backend_text_free(item->game_subtitle);
#endif

		cpymo_game_selector_item *cur = item;
		item = item->next;
		free(cur);
	}
}

