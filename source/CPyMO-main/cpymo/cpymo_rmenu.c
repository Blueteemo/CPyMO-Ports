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

#ifdef GARLIC_NO_FFMPEG
#include <time.h>
#include <math.h>
#define RMENU_ITEMS 10
extern void garlic_request_library(void);
static error_t garlic_confirm_library(cpymo_engine *e, void *data, bool confirmed)
{
    (void)e; (void)data;
    if (confirmed) garlic_request_library();
    return CPYMO_ERR_SUCC;
}
#else
#define RMENU_ITEMS 9
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

#ifdef GARLIC_NO_FFMPEG
    float panel[4],padding,header_height,clock_height,clock_gap,clock_font_size,clock_poll;
    cpymo_backend_text clock_text;
    char clock_label[6];
#endif
	bool alive;
} cpymo_rmenu;

#ifdef GARLIC_NO_FFMPEG
static void garlic_rmenu_clock(cpymo_engine *e,cpymo_rmenu *r,time_t now)
{
    struct tm local;char label[6]="--:--";
    if(now!=(time_t)-1 && localtime_r(&now,&local))
        strftime(label,sizeof(label),"%H:%M",&local);
    if(!strcmp(label,r->clock_label))return;
    cpymo_backend_text text;float width;
    if(cpymo_backend_text_create(&text,&width,cpymo_str_pure(label),r->clock_font_size)!=CPYMO_ERR_SUCC)return;
    if(r->clock_text)cpymo_backend_text_free(r->clock_text);
    r->clock_text=text;memcpy(r->clock_label,label,sizeof(label));
    cpymo_engine_request_redraw(e);
}

static void garlic_rmenu_layout(cpymo_engine *e,cpymo_rmenu *r)
{
    float width,height;cpymo_select_img_measure_text_rows(&r->menu,&width,&height);
    float sw=e->gameconfig.imagesize_w,sh=e->gameconfig.imagesize_h;
    float margin=sh*.01f;
    r->padding=sh*.015f;r->clock_gap=sh*.0125f;
    float top,bottom;garlic_text_ink_bounds(r->clock_text,&top,&bottom);
    r->clock_height=bottom-top;
    float available=sh-2*margin;
    /* The stock SYSTEM skin uses roughly the upper 15% for its title bar.
       Keep all choices below it, including Auto, instead of treating the
       entire bitmap as the selectable content rectangle. */
    float required=fmaxf((height+r->clock_gap+r->padding)/.84f,
        height+r->clock_gap+r->clock_height+3*r->padding);
    if(required>available){
        cpymo_select_img_fit_text_rows(&r->menu,cpymo_gameconfig_font_size(&e->gameconfig),0);
        cpymo_select_img_measure_text_rows(&r->menu,&width,&height);
        required=fmaxf((height+r->clock_gap+r->padding)/.84f,
            height+r->clock_gap+r->clock_height+3*r->padding);
    }
    float scale=fmaxf(1,required/(float)r->bg_h);
    r->panel[2]=fminf(sw-2*margin,fmaxf(width+2*r->padding,r->bg_w*scale));
    r->panel[3]=fminf(available,r->bg_h*scale);
    r->header_height=fmaxf(r->panel[3]*.16f,r->clock_height+2*r->padding);
    r->panel[0]=(sw-r->panel[2])/2;r->panel[1]=(sh-r->panel[3])/2;
}
#endif

static error_t cpymo_rmenu_update(cpymo_engine *e, void *ui_data, float dt)
{
	cpymo_rmenu *r = (cpymo_rmenu *)ui_data;
#ifdef GARLIC_NO_FFMPEG
    r->clock_poll+=dt;
    if(r->clock_poll>=1.0f){r->clock_poll=0;garlic_rmenu_clock(e,r,time(NULL));}
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

#ifdef GARLIC_NO_FFMPEG
    memcpy(bg_xywh,r->panel,sizeof(bg_xywh));
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

	/* Darken the menu art as well so the white labels keep contrast when the
	 * story behind it is bright. */
	cpymo_backend_image_fill_rects(bg_xywh, 1, cpymo_color_black, 0.45f,
		cpymo_backend_image_draw_type_ui_element_bg);

#ifdef GARLIC_NO_FFMPEG
    if(r->clock_text)cpymo_backend_text_draw(r->clock_text,r->panel[0]+r->padding,
        garlic_text_centered_baseline(r->clock_text,r->panel[1]+r->header_height/2),
        cpymo_color_white,1,cpymo_backend_image_draw_type_ui_element);
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
#ifdef GARLIC_NO_FFMPEG
    if(r->clock_text)cpymo_backend_text_free(r->clock_text);
#endif

	cpymo_select_img_free(&r->menu);
}

error_t cpymo_rmenu_restart_game(cpymo_engine *e, void *data, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
#ifdef GARLIC_NO_FFMPEG
    extern error_t garlic_queue_game(const char *gamedir);
    return garlic_queue_game(e->assetloader.gamedir);
#else
	char *gamedir = 
		cpymo_str_copy_malloc(cpymo_str_pure(e->assetloader.gamedir));
	if (gamedir == NULL) return CPYMO_ERR_OUT_OF_MEM;

	cpymo_engine_free(e);
	error_t err = cpymo_engine_init(e, gamedir);
	free(gamedir);

	return err;
#endif
}

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
#ifdef GARLIC_NO_FFMPEG
    case 9:
        cpymo_ui_exit(e);
        return cpymo_msgbox_ui_enter(e, cpymo_str_pure("返回游戏列表？未保存的进度将丢失。"),
            &garlic_confirm_library, NULL);
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
	const size_t menu_items = RMENU_ITEMS;
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

#ifdef GARLIC_NO_FFMPEG
    rmenu->clock_text=NULL;rmenu->clock_label[0]=0;rmenu->clock_poll=0;
    rmenu->clock_font_size=e->gameconfig.imagesize_h*.045f;
#endif
	rmenu->alive = true;

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

	err = cpymo_select_img_configuare_begin(
		&rmenu->menu,
		RMENU_ITEMS,
		cpymo_str_pure(""),
		&e->assetloader,
		&e->gameconfig);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	const float font_size = cpymo_gameconfig_font_size(&e->gameconfig);
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
#ifdef GARLIC_NO_FFMPEG
    RMENU_ITEM(9, "返回游戏列表", true);
#endif

#ifdef GARLIC_NO_FFMPEG
    cpymo_select_img_fit_text_rows(&rmenu->menu,font_size,e->gameconfig.imagesize_h/240.0f);
    rmenu->menu.draw_type=cpymo_backend_image_draw_type_ui_element;
    garlic_rmenu_clock(e,rmenu,time(NULL));
    if(!rmenu->clock_text){cpymo_ui_exit(e);return CPYMO_ERR_OUT_OF_MEM;}
    garlic_rmenu_layout(e,rmenu);
#endif

	float xywh[4] = {
		((float)e->gameconfig.imagesize_w - (float)rmenu->bg_w) / 2,
		((float)e->gameconfig.imagesize_h - (float)rmenu->bg_h) / 2,
		(float)rmenu->bg_w,
		(float)rmenu->bg_h,
	};

#ifdef GARLIC_NO_FFMPEG
    xywh[0]=rmenu->panel[0]+rmenu->padding;
    xywh[1]=rmenu->panel[1]+rmenu->header_height+rmenu->clock_gap;
    xywh[2]=rmenu->panel[2]-2*rmenu->padding;
    xywh[3]=rmenu->panel[1]+rmenu->panel[3]-rmenu->padding-xywh[1];
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

