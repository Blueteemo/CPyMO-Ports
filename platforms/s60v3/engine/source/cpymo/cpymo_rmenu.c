#include "cpymo_prelude.h"
#include "cpymo_rmenu.h"
#include "cpymo_engine.h"
#include "cpymo_config_ui.h"
#include "cpymo_msgbox_ui.h"
#include "cpymo_save_ui.h"
#include "cpymo_save.h"
#include "cpymo_localization.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#ifdef S60V3
#include "../cpymo-backends/sdl1/cpymo_s60_app.h"
#include "../cpymo-backends/sdl1/cpymo_s60_image.h"
#include "../cpymo-backends/sdl1/cpymo_s60_text_metrics.h"
#include <time.h>
#define S60_RMENU_ITEMS 11
#define S60_RMENU_HEADER_FRACTION 0.17f
#define S60_RMENU_CLOCK_INSET 8.0f
#endif

#ifdef __3DS__
extern bool fill_screen_enabled;
const extern bool fill_screen;
const extern bool enhanced_3ds_display_mode;
#endif

#ifdef ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY
#include "../cpymo-backends/android/app/src/main/cpp/include/cpymo_android.h"
#endif

typedef struct {
	cpymo_backend_image bg;
	int bg_w, bg_h;

	cpymo_select_img menu;
	cpymo_wait menu_waiter;

	cpymo_key_hold touch;
	float bg_zoom_out;
#ifdef S60V3
    cpymo_backend_text clock_text;
    char clock_label[6];
    float clock_ink[4], clock_elapsed;
    float panel[4];
#endif

	bool alive;
} cpymo_rmenu;

#ifdef S60V3
static void cpymo_rmenu_refresh_clock(cpymo_engine *e, cpymo_rmenu *menu)
{
    char label[6] = "--:--";
    time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    if (local) strftime(label, sizeof(label), "%H:%M", local);
    if (!strcmp(label, menu->clock_label)) return;
    cpymo_backend_text text;
    float width;
    if (cpymo_backend_text_create(&text, &width, cpymo_str_pure(label), 16.0f)
            != CPYMO_ERR_SUCC) return;
    if (menu->clock_text) cpymo_backend_text_free(menu->clock_text);
    menu->clock_text = text;
    memcpy(menu->clock_label, label, sizeof(label));
    cpymo_s60_text_ink(text, menu->clock_ink);
    cpymo_engine_request_redraw(e);
}
#endif

static error_t cpymo_rmenu_update(cpymo_engine *e, void *ui_data, float dt)
{
	cpymo_rmenu *r = (cpymo_rmenu *)ui_data;
#ifdef S60V3
    if (CPYMO_INPUT_JUST_RELEASED(e, cancel)) {
        cpymo_ui_exit(e);
        return CPYMO_ERR_SUCC;
    }
    r->clock_elapsed += dt;
    if (r->clock_elapsed >= 1.0f) {
        r->clock_elapsed = 0.0f;
        cpymo_rmenu_refresh_clock(e, r);
    }
#endif

	enum cpymo_key_hold_result result = cpymo_key_hold_update(
		e, &r->touch, dt, e->input.mouse_button);
	if (result == cpymo_key_hold_result_just_hold) {
		cpymo_ui_exit(e);
		return CPYMO_ERR_SUCC;
	}

	if (r->alive) {
		error_t err = cpymo_wait_update(&r->menu_waiter, e, dt);
		CPYMO_THROW(err);

		if (r->alive) {
			err = cpymo_select_img_update(e, &r->menu, dt);
			CPYMO_THROW(err);
		}
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_rmenu_draw(const cpymo_engine *e, const void *ui_data)
{
	const cpymo_rmenu *r = (const cpymo_rmenu *)ui_data;

	cpymo_bg_draw(e);
	cpymo_scroll_draw(&e->scroll);
#ifdef S60V3
	cpymo_charas_draw(e);
#endif

	#ifdef __3DS__
	if (fill_screen && !enhanced_3ds_display_mode)
		fill_screen_enabled = false;
	#endif

	float w = (float)r->bg_w * r->bg_zoom_out;
	float h = (float)r->bg_h * r->bg_zoom_out;

	float bg_xywh[4] = {
		((float)e->gameconfig.imagesize_w - (float)w) / 2,
		((float)e->gameconfig.imagesize_h - (float)h) / 2 - 0.018f * (float)e->gameconfig.imagesize_h,
		(float)w,
		(float)h,
	};

#ifdef S60V3
    memcpy(bg_xywh, r->panel, sizeof(bg_xywh));
#endif
	if (r->bg) {
#ifdef DISABLE_IMAGE_SCALING
		cpymo_backend_image_draw(
			bg_xywh[0] + (bg_xywh[2] - r->bg_w) / 2,
			bg_xywh[1] + (bg_xywh[3] - r->bg_h) / 2, 
			r->bg_w,
			r->bg_h,
			r->bg,
			0,
			0,
			r->bg_w,
			r->bg_h,
			1.0f,
			cpymo_backend_image_draw_type_ui_element_bg);
#else
		cpymo_backend_image_draw(
			bg_xywh[0],
			bg_xywh[1],
			bg_xywh[2],
			bg_xywh[3],
			r->bg,
			0,
			0,
			r->bg_w,
			r->bg_h,
			1.0f,
			cpymo_backend_image_draw_type_ui_element_bg);
#endif
	}
	else {
		cpymo_backend_image_fill_rects(
			bg_xywh, 1, cpymo_color_black, 0.75f, 
			cpymo_backend_image_draw_type_ui_element_bg);
	}

#ifdef S60V3
    if (r->clock_text) {
        const float x = bg_xywh[0] + S60_RMENU_CLOCK_INSET - r->clock_ink[0];
        const float y = bg_xywh[1] + S60_RMENU_CLOCK_INSET - r->clock_ink[1];
        cpymo_s60_text_draw_plain(r->clock_text, x, y, cpymo_color_white);
    }
#endif
	cpymo_select_img_draw(&r->menu, e->gameconfig.imagesize_w, e->gameconfig.imagesize_h, false);

	#ifdef __3DS__
	fill_screen_enabled = true;
	#endif
}

static void cpymo_rmenu_delete(cpymo_engine *e, void *ui_data)
{
	cpymo_rmenu *r = (cpymo_rmenu *)ui_data;
	r->alive = false;

	if (r->bg) cpymo_backend_image_free(r->bg);
#ifdef S60V3
    if (r->clock_text) cpymo_backend_text_free(r->clock_text);
#endif

	cpymo_select_img_free(&r->menu);
}

error_t cpymo_rmenu_restart_game(cpymo_engine *e, void *data, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	char *gamedir = 
		cpymo_str_copy_malloc(cpymo_str_pure(e->assetloader.gamedir));
	if (gamedir == NULL) return CPYMO_ERR_OUT_OF_MEM;

	cpymo_engine_free(e);
	error_t err = cpymo_engine_init(e, gamedir);
	free(gamedir);

	return err;
}

#ifdef S60V3
static error_t cpymo_rmenu_s60_back_confirm(cpymo_engine *e, void *data, bool confirm)
{
    if (confirm) cpymo_s60_request_game_list();
    return CPYMO_ERR_SUCC;
}

#endif

static error_t cpymo_rmenu_ok(cpymo_engine *e, int sel, uint64_t hash, bool _)
{
	switch (sel) {
	case 0: cpymo_ui_exit(e); e->say.auto_mode = true; e->say.auto_mode_timer = -1; break;
	case 1: cpymo_ui_exit(e); cpymo_save_ui_enter(e, false); break;
	case 2: cpymo_ui_exit(e); cpymo_save_ui_enter(e, true); break;
	case 3: cpymo_ui_exit(e); e->skipping = true; break;
	case 4: cpymo_say_hidewindow_until_click(e); cpymo_ui_exit(e); break;
	case 5: cpymo_ui_exit(e); cpymo_backlog_ui_enter(e); break;
	case 6: cpymo_ui_exit(e); cpymo_config_ui_enter(e); break;
	case 7: 
		cpymo_ui_exit(e);
		cpymo_msgbox_ui_enter(
			e, 
			cpymo_str_pure(
				cpymo_localization_get(e)->rmenu_restat_game_confirm_message), 
			&cpymo_rmenu_restart_game, 
			NULL);
		break;
	
	case 8: cpymo_ui_exit(e); break;
#ifdef S60V3
    case 9:
        cpymo_ui_exit(e);
        return cpymo_msgbox_ui_enter(e, cpymo_str_pure("返回游戏列表？请先保存进度。"),
            &cpymo_rmenu_s60_back_confirm, NULL);
    case 10:
        cpymo_ui_exit(e);
        cpymo_s60_request_exit_confirmation();
        break;
#endif
	default:
		assert(false);
	}

	return CPYMO_ERR_SUCC;
}

static inline float cpymo_rmenu_bg_zoom(
	const cpymo_rmenu *r,
	float font_size)
{
	#ifdef DISABLE_IMAGE_SCALING
	return 1.0f;
	#else
	const size_t menu_items = 9;
	const float menu_item_padding = 2.0f;
	const float menu_padding = 16.0f;
	const float menu_height =
		(menu_item_padding + font_size) * menu_items + 2.0f * menu_padding;

	float zoom_out = (menu_height / r->bg_h);
	if (zoom_out < 1.0f) zoom_out = 1.0f;
	return zoom_out;
	#endif
}

error_t cpymo_rmenu_enter(cpymo_engine *e)
{
	cpymo_rmenu *rmenu = NULL;
	error_t err = cpymo_ui_enter(
		(void **)&rmenu,
		e,
		sizeof(cpymo_rmenu),
		&cpymo_rmenu_update,
		&cpymo_rmenu_draw,
		&cpymo_rmenu_delete);
	CPYMO_THROW(err);

#ifdef ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY
	cpymo_android_play_sound(SOUND_MENU);
#endif

	rmenu->alive = true;
#ifdef S60V3
    rmenu->clock_text = NULL;
    rmenu->clock_label[0] = '\0';
    rmenu->clock_elapsed = 0.0f;
    cpymo_rmenu_refresh_clock(e, rmenu);
#endif

	cpymo_key_hold_init(&rmenu->touch, e->input.mouse_button);

	cpymo_select_img_init(&rmenu->menu);
	cpymo_wait_reset(&rmenu->menu_waiter);

	rmenu->bg = NULL;
	err = cpymo_assetloader_load_system_image(
		&rmenu->bg,
		&rmenu->bg_w,
		&rmenu->bg_h,
		cpymo_str_pure("menu"),
		&e->assetloader,
		true);

	if (err != CPYMO_ERR_SUCC) {
		rmenu->bg = NULL;

		if (strcmp(e->gameconfig.platform, "s60v3") == 0) {
			rmenu->bg_w = 240;
			rmenu->bg_h = 144;
		}
		else if (strcmp(e->gameconfig.platform, "s60v5") == 0) {
			rmenu->bg_w = 360;
			rmenu->bg_h = 216;
		}
		else {
			rmenu->bg_w = (int)((float)e->gameconfig.imagesize_w * 0.75f);
			rmenu->bg_h = (int)((float)e->gameconfig.imagesize_h * 0.6f);
		}
	}
#ifdef S60V3
	else cpymo_s60_move_menu_title(rmenu->bg, (int)(rmenu->bg_h * S60_RMENU_HEADER_FRACTION));
#endif

	err = cpymo_select_img_configuare_begin(
		&rmenu->menu,
#ifdef S60V3
        S60_RMENU_ITEMS,
#else
		9,
#endif
		cpymo_str_pure(""),
		&e->assetloader,
		&e->gameconfig);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	float font_size = cpymo_gameconfig_font_size(&e->gameconfig);
#ifdef S60V3
    /* Eleven navigation items fit the 240-pixel display independently of game text size. */
    if (font_size > 14.0f) font_size = 14.0f;
#endif
	rmenu->bg_zoom_out = cpymo_rmenu_bg_zoom(rmenu, font_size);

	#define RMENU_ITEM(_, TEXT, ENABLED) \
		err = cpymo_select_img_configuare_select_text( \
			&rmenu->menu, \
			&e->assetloader, \
			&e->gameconfig, \
			&e->flags, \
			cpymo_str_pure(TEXT), \
			ENABLED, \
			cpymo_select_img_selection_nohint, \
			0xFFFFFFFFFFFFFFFF, \
			font_size); \
		if (err != CPYMO_ERR_SUCC) { \
			cpymo_ui_exit(e); \
			return err; \
		}

	cpymo_select_img_configuare_set_ok_callback(&rmenu->menu, &cpymo_rmenu_ok);

	const cpymo_localization *l = cpymo_localization_get(e);

	RMENU_ITEM(0, l->rmenu_auto, e->select_img.selections == NULL);
	RMENU_ITEM(1, l->rmenu_save, true);
	RMENU_ITEM(2, l->rmenu_load, true);
	RMENU_ITEM(3, l->rmenu_skip, e->select_img.selections == NULL);
	RMENU_ITEM(4, l->rmenu_hide_window, e->select_img.selections == NULL);
	RMENU_ITEM(5, l->rmenu_backlog, true);
	RMENU_ITEM(6, l->rmenu_config, true)
	RMENU_ITEM(7, l->rmenu_restart, true);
	RMENU_ITEM(8, l->rmenu_back_to_game, true);
#ifdef S60V3
    RMENU_ITEM(9, "返回游戏列表", true);
    RMENU_ITEM(10, "退出应用", true);
#endif

	float xywh[4] = {
		((float)e->gameconfig.imagesize_w - (float)rmenu->bg_w) / 2,
		((float)e->gameconfig.imagesize_h - (float)rmenu->bg_h) / 2,
		(float)rmenu->bg_w,
		(float)rmenu->bg_h,
	};

#ifdef S60V3
    const float screen_w = e->gameconfig.imagesize_w;
    const float screen_h = e->gameconfig.imagesize_h;
    const float header = S60_RMENU_HEADER_FRACTION;
    const float top_gap = 3.0f, bottom_gap = 8.0f;
    const int item_count = e->select_img.selections ? S60_RMENU_ITEMS - 3 : S60_RMENU_ITEMS;
    const float rows_h = item_count * ((int)font_size + 1);
    const float required_h = (rows_h + top_gap + bottom_gap) / (1.0f - header);
    const float old_h = rmenu->bg_h * rmenu->bg_zoom_out;

    float *panel = rmenu->panel;
    panel[2] = cpymo_utils_clampf(rmenu->bg_w * rmenu->bg_zoom_out, 1.0f, screen_w - 20.0f);
    panel[0] = (screen_w - panel[2]) * 0.5f;
    const float wanted_h = old_h > required_h ? old_h : required_h;
    panel[3] = cpymo_utils_clampf(wanted_h, 1.0f, screen_h - 12.0f);
    panel[1] = (screen_h - panel[3]) * 0.5f;
    xywh[0] = panel[0];
    xywh[1] = panel[1] + panel[3] * header + top_gap;
    xywh[2] = panel[2];
    xywh[3] = panel[3] * (1.0f - header) - top_gap - bottom_gap;
#endif
	cpymo_select_img_configuare_end_select_text(
		&rmenu->menu,
		&rmenu->menu_waiter,
		e,
		xywh[0],
		xywh[1],
		xywh[0] + xywh[2],
		xywh[1] + xywh[3],
		cpymo_color_white,
		0,
		false);

	rmenu->menu.draw_type = cpymo_backend_image_draw_type_ui_element;

	return CPYMO_ERR_SUCC;
}

