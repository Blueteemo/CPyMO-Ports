#include "cpymo_prelude.h"
#ifdef S60V3
#include "../cpymo-backends/sdl1/cpymo_s60_text_metrics.h"
#endif
#include "cpymo_msgbox_ui.h"
#include "cpymo_engine.h"
#include "cpymo_localization.h"
#include "cpymo_key_hold.h"
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
extern bool fill_screen_enabled;
const extern bool fill_screen;
const extern bool enhanced_3ds_display_mode;
#endif

typedef struct {
	cpymo_backend_text message;
	float message_width;
	float font_size;

	cpymo_backend_text confirm_btn;
	float confirm_btn_width;
	cpymo_backend_text cancel_btn;
	float cancel_btn_width;
	const char *cancel_label;
#ifdef S60V3
	bool outline_unselected;
	bool legacy_choice;
	bool ignore_initial_ok_release;
	cpymo_backend_text message_detail;
	float message_ink[2][4];
#endif

	int selection;

	error_t(*okcancel_callback)(cpymo_engine *e, void *data, bool);
	void *okcancel_callback_data;
	cpymo_key_hold mouse_button;
} cpymo_msgbox_ui;


static error_t cpymo_msgbox_ui_okcancel_finish(cpymo_engine *e, bool is_ok)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)cpymo_ui_data(e);
	error_t(*func)(cpymo_engine *, void *, bool) = ui->okcancel_callback;
	void *data = ui->okcancel_callback_data;
	ui->okcancel_callback = NULL;
	ui->okcancel_callback_data = NULL;
	cpymo_ui_exit(e);
	return func ? func(e, data, is_ok) : CPYMO_ERR_SUCC;
}

static void cpymo_msgbox_ui_get_btn_rect(
	float *out_xywh,
	float txt_width,
	float font_size,
	float screen_w,
	float y)
{
	float btn_width = txt_width * 3.0f;
	float btn_height = font_size * 1.5f;

	out_xywh[0] = screen_w / 2 - btn_width / 2;
	out_xywh[1] = y;
	out_xywh[2] = btn_width;
	out_xywh[3] = btn_height;
}

static float cpymo_msgbox_ui_confirm_y(const cpymo_msgbox_ui *ui, float screen_h)
{
#ifdef S60V3
	if (ui->message_detail) return screen_h * 0.5f;
#endif
	return ui->cancel_btn ? screen_h * 0.5f : screen_h * 0.618f;
}

static int cpymo_msgbox_ui_get_mouse_selection(cpymo_engine *e)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)cpymo_ui_data(e);
	float fontsize = ui->font_size;

	float screen_w = (float)e->gameconfig.imagesize_w, screen_h = (float)e->gameconfig.imagesize_h;

	const cpymo_input *input = &e->input;
	if (!input->mouse_position_useable)
		input = &e->prev_input;

	float btn_width =
		ui->confirm_btn_width > ui->cancel_btn_width ?
		ui->confirm_btn_width : ui->cancel_btn_width;

	if (input->mouse_position_useable) {
		float xywh[4];
		cpymo_msgbox_ui_get_btn_rect(
			xywh,
			btn_width,
			fontsize,
			screen_w,
			cpymo_msgbox_ui_confirm_y(ui, screen_h));

		float x = input->mouse_x;
		float y = input->mouse_y;
		if (x >= xywh[0] && x <= xywh[0] + xywh[2] && y >= xywh[1] && y <= xywh[1] + xywh[3])
			return 0;

		if (ui->cancel_btn) {
			cpymo_msgbox_ui_get_btn_rect(
				xywh,
				btn_width,
				fontsize,
				screen_w,
				screen_h * 0.5f + fontsize * 1.75f + (8.0f / (fontsize / 32)));

			if (x >= xywh[0] && x <= xywh[0] + xywh[2] && y >= xywh[1] && y <= xywh[1] + xywh[3])
				return 1;
		}
	}

	return -1;
}

static error_t cpymo_msgbox_ui_update(cpymo_engine *e, void *ui_data, float dt)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)ui_data;

	enum cpymo_key_hold_result mbs = cpymo_key_hold_update(
		e, &ui->mouse_button, dt, e->input.mouse_button);
	
	if (CPYMO_INPUT_JUST_RELEASED(e, cancel) || mbs == cpymo_key_hold_result_holding) {
		cpymo_msgbox_ui_okcancel_finish(e, false);
		return CPYMO_ERR_SUCC;
	}

#ifdef ENABLE_TEXT_EXTRACT
	const cpymo_localization *l = cpymo_localization_get(e);
#endif

	if ((CPYMO_INPUT_JUST_PRESSED(e, up) || CPYMO_INPUT_JUST_PRESSED(e, down))) {
		if (ui->selection == -1) {
			if (ui->cancel_btn) ui->selection = 1;
			else ui->selection = 0;
		}
		else if (ui->selection == 0 && ui->cancel_btn) ui->selection = 1;
		else if (ui->selection == 1 && ui->cancel_btn) ui->selection = 0;

		cpymo_engine_request_redraw(e);

		cpymo_backend_text_extract(ui->selection == 0 ? l->msgbox_ok : ui->cancel_label);
	}
	else if (CPYMO_INPUT_JUST_PRESSED(e, ok)) {
		if (ui->cancel_btn == NULL) {
			ui->selection = 0;
		}

		cpymo_engine_request_redraw(e);
	}
	else if (CPYMO_INPUT_JUST_PRESSED(e, mouse_button)) {
		ui->selection = cpymo_msgbox_ui_get_mouse_selection(e);
		cpymo_engine_request_redraw(e);
	}
	else if (CPYMO_INPUT_JUST_RELEASED(e, ok) && ui->selection != -1) {
#ifdef S60V3
		if (ui->ignore_initial_ok_release) {
			ui->ignore_initial_ok_release = false;
			return CPYMO_ERR_SUCC;
		}
#endif
		cpymo_engine_request_redraw(e);
		return cpymo_msgbox_ui_okcancel_finish(e, !ui->selection);
	}
	else if (CPYMO_INPUT_JUST_RELEASED(e, mouse_button)) {
		cpymo_engine_request_redraw(e);
		int mouse_sel = cpymo_msgbox_ui_get_mouse_selection(e);
		return cpymo_msgbox_ui_okcancel_finish(e, !mouse_sel);
	}

	if (cpymo_input_mouse_moved(e)) {
		int mouse_sel = cpymo_msgbox_ui_get_mouse_selection(e);
		if (mouse_sel != ui->selection) {
			ui->selection = mouse_sel;
			cpymo_engine_request_redraw(e);

			if (ui->selection >= 0)
				cpymo_backend_text_extract(
					ui->selection == 0 ? l->msgbox_ok : ui->cancel_label);
		}
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_msgbox_ui_draw_btn(
	cpymo_backend_text btn_txt,
	float btn_width,
	float txt_width, 
	float font_size, 
	float screen_w,
	float screen_h,
	float y, 
	bool sel,
	bool key_down,
	bool outline_unselected)
{
	float xywh[4];
	cpymo_msgbox_ui_get_btn_rect(xywh, btn_width, font_size, screen_w, y);

	cpymo_color gray;
	gray.r = 128;
	gray.g = 128;
	gray.b = 128;

	cpymo_backend_image_fill_rects(
		xywh,
		1,
		sel ? (key_down ? gray : cpymo_color_white) : cpymo_color_black,
		sel ? 1.0f : 0.5f,
		cpymo_backend_image_draw_type_ui_element);

#ifdef S60V3
    if (!sel && outline_unselected) {
        const float left = xywh[0], top = xywh[1];
        const float w = xywh[2], h = xywh[3];
        const float thickness = 2.0f;
        const float edges[] = {
            left, top, w, thickness,
            left, top + h - thickness, w, thickness,
            left, top + thickness, thickness, h - 2.0f * thickness,
            left + w - thickness, top + thickness, thickness, h - 2.0f * thickness,
        };
        const cpymo_color border = {136, 147, 161};
        cpymo_backend_image_fill_rects(edges, 4, border, 1.0f,
            cpymo_backend_image_draw_type_ui_element);
    }
    float ink[4];
    cpymo_s60_text_ink(btn_txt, ink);
    const float x = xywh[0] + (xywh[2] - ink[0] - ink[2]) * 0.5f;
    const float baseline = xywh[1] + (xywh[3] - ink[1] - ink[3]) * 0.5f;
#else
    (void)outline_unselected;
    const float x = screen_w / 2 - txt_width / 2;
    const float baseline = y + xywh[3] / 2 + font_size / 2 - font_size * 0.15f;
#endif
	cpymo_backend_text_draw(
		btn_txt, x, baseline,
		sel ? cpymo_color_black : cpymo_color_white,
		1.0f,
		cpymo_backend_image_draw_type_ui_element);
}

static void cpymo_msgbox_ui_draw(const cpymo_engine *e, const void *ui_data)
{
	cpymo_bg_draw(e);
	cpymo_scroll_draw(&e->scroll);

	#ifdef __3DS__
	if (fill_screen && !enhanced_3ds_display_mode)
		fill_screen_enabled = false;
	#endif

	float screen_w = (float)e->gameconfig.imagesize_w, screen_h = (float)e->gameconfig.imagesize_h;
	float xywh[] = { -100, -100, screen_w + 200, screen_h + 200 };
	cpymo_backend_image_fill_rects(xywh, 1, cpymo_color_black, 0.5f, cpymo_backend_image_draw_type_bg);

	const cpymo_msgbox_ui *ui = (const cpymo_msgbox_ui *)ui_data;
	float fontsize = ui->font_size;

	float x = (screen_w - ui->message_width) / 2;
	float y = screen_h * 0.3f + fontsize / 2;
#ifdef S60V3
	if (ui->message_detail) {
		const float line_spacing = ui->legacy_choice
			? cpymo_s60_font_baseline(fontsize, false) : fontsize * 1.5f;
		const cpymo_backend_text lines[] = { ui->message, ui->message_detail };
		for (int i = 0; i < 2; ++i) {
			const float *ink = ui->message_ink[i];
			const float left = (screen_w - ink[0] - ink[2]) * 0.5f;
			const float center_y = screen_h * 0.3f + (i - 0.5f) * line_spacing;
			const float baseline = ui->legacy_choice ? y + i * line_spacing
				: center_y - (ink[1] + ink[3]) * 0.5f;
			cpymo_backend_text_draw(lines[i], left, baseline, cpymo_color_white, 1.0f,
				cpymo_backend_image_draw_type_ui_element);
		}
	} else
#endif
		cpymo_backend_text_draw(ui->message, x, y, cpymo_color_white, 1.0f,
			cpymo_backend_image_draw_type_ui_element);

	bool key_down = e->input.mouse_button || e->input.ok;
	bool outline_unselected = false;
#ifdef S60V3
	outline_unselected = ui->outline_unselected;
#endif

	float btn_width = ui->confirm_btn_width > ui->cancel_btn_width ? ui->confirm_btn_width : ui->cancel_btn_width;

	cpymo_msgbox_ui_draw_btn(
		ui->confirm_btn, 
		btn_width,
		ui->confirm_btn_width, 
		fontsize, 
		screen_w, 
		screen_h,
		cpymo_msgbox_ui_confirm_y(ui, screen_h),
		ui->selection == 0,
		key_down,
		outline_unselected);

	if (ui->cancel_btn) {
		cpymo_msgbox_ui_draw_btn(
			ui->cancel_btn,
			btn_width,
			ui->cancel_btn_width,
			fontsize,
			screen_w,
			screen_h,
			screen_h * 0.5f + fontsize * 1.75f + (8.0f / (fontsize / 32)),
			ui->selection == 1,
			key_down,
			outline_unselected);
	}

	#ifdef __3DS__
	fill_screen_enabled = true;
	#endif
}

static void cpymo_msgbox_ui_delete(struct cpymo_engine *e, void *ui_data)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)ui_data;

	if (ui->okcancel_callback) {
#ifdef S60V3
		if (!ui->legacy_choice)
#endif
		ui->okcancel_callback(e, ui->okcancel_callback_data, false);
		ui->okcancel_callback = NULL;
		ui->okcancel_callback_data = NULL;
	}
	
	if (ui->message) cpymo_backend_text_free(ui->message);
#ifdef S60V3
	if (ui->message_detail) cpymo_backend_text_free(ui->message_detail);
#endif
	if (ui->confirm_btn) cpymo_backend_text_free(ui->confirm_btn);
	if (ui->cancel_btn) cpymo_backend_text_free(ui->cancel_btn);
}

static error_t cpymo_msgbox_ui_default_confirm(
	struct cpymo_engine *e, void *d, bool b)
{
	return CPYMO_ERR_SUCC;
}

static error_t cpymo_msgbox_ui_enter_with_size(
	cpymo_engine *e, 
	cpymo_str message, 
	error_t(*okcancel_callback)(cpymo_engine *e, void *data, bool is_confirm), 
	void *okcancel_callback_data,
	float fontsize, bool split_notice, const char *back_label)
{
	cpymo_msgbox_ui *ui = NULL;
	error_t err = cpymo_ui_enter(
		(void **)&ui,
		e,
		sizeof(cpymo_msgbox_ui),
		&cpymo_msgbox_ui_update,
		&cpymo_msgbox_ui_draw,
		&cpymo_msgbox_ui_delete);
	CPYMO_THROW(err);

	ui->message = NULL;
	ui->confirm_btn = NULL;
	ui->cancel_btn = NULL;
	ui->cancel_btn_width = 0;
	ui->confirm_btn_width = 0;
	const cpymo_localization *l = cpymo_localization_get(e);
	ui->cancel_label = back_label ? back_label : l->msgbox_cancel;
#ifdef S60V3
	ui->outline_unselected = false;
	ui->legacy_choice = back_label != NULL;
	ui->ignore_initial_ok_release = ui->legacy_choice && e->input.ok;
	ui->message_detail = NULL;
#endif

	ui->okcancel_callback = okcancel_callback;
	ui->okcancel_callback_data = okcancel_callback_data;
	ui->selection = back_label ? 1 : okcancel_callback ? -1 : 0;
	cpymo_key_hold_init(&ui->mouse_button, e->input.mouse_button);

	cpymo_str first_line = message;
#ifdef S60V3
	cpymo_str detail = { NULL, 0 };
	if (split_notice && message.len) {
		const char *newline = memchr(message.begin, '\n', message.len);
		if (newline && newline != message.begin && newline + 1 < message.begin + message.len) {
			first_line.len = (size_t)(newline - message.begin);
			detail.begin = newline + 1;
			detail.len = message.len - first_line.len - 1;
		}
	}
	/* Fit each line separately so a notice keeps the same scale as other dialogs. */
	const float max_width = (float)e->gameconfig.imagesize_w - 16.0f;
	while (fontsize > 12.0f && (cpymo_backend_text_width(first_line, fontsize) > max_width ||
			(detail.len && cpymo_backend_text_width(detail, fontsize) > max_width)))
		fontsize -= 1.0f;
#else
	(void)split_notice;
#endif
	ui->font_size = fontsize;

	err = cpymo_backend_text_create(&ui->message, &ui->message_width, first_line, fontsize);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

#ifdef S60V3
	if (detail.len) {
		float detail_width;
		err = cpymo_backend_text_create(&ui->message_detail, &detail_width, detail, fontsize);
		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
		cpymo_s60_text_ink(ui->message, ui->message_ink[0]);
		cpymo_s60_text_ink(ui->message_detail, ui->message_ink[1]);
	}
#endif

	err = cpymo_backend_text_create(
		&ui->confirm_btn, 
		&ui->confirm_btn_width, 
		cpymo_str_pure(l->msgbox_ok), 
		fontsize);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	if (okcancel_callback) {
		err = cpymo_backend_text_create(
			&ui->cancel_btn,
			&ui->cancel_btn_width,
			cpymo_str_pure(ui->cancel_label),
			fontsize);
		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}
	else {
		ui->okcancel_callback = &cpymo_msgbox_ui_default_confirm;
	}

	cpymo_engine_extract_text(e, message);
	cpymo_engine_extract_text_cstr(e, ui->cancel_label);
	cpymo_engine_extract_text_submit(e);

	return err;
}

error_t cpymo_msgbox_ui_enter(
	cpymo_engine *e,
	cpymo_str message,
	error_t(*okcancel_callback)(cpymo_engine *e, void *data, bool is_confirm),
	void *okcancel_callback_data)
{
	return cpymo_msgbox_ui_enter_with_size(e, message, okcancel_callback,
		okcancel_callback_data, cpymo_gameconfig_font_size(&e->gameconfig), false, NULL);
}

#ifdef S60V3
error_t cpymo_msgbox_ui_enter_s60_notice(cpymo_engine *e, cpymo_str message)
{
	/* System notices follow the UI scale, independently of the dialogue size. */
	const float fontsize = 16.0f * (float)e->gameconfig.imagesize_h / 240.0f;
	return cpymo_msgbox_ui_enter_with_size(e, message, NULL, NULL, fontsize, true, NULL);
}

error_t cpymo_msgbox_ui_enter_s60_legacy_choice(cpymo_engine *e, cpymo_str message,
	error_t (*choose)(cpymo_engine *, void *, bool))
{
	const float fontsize = 16.0f * (float)e->gameconfig.imagesize_h / 240.0f;
	return cpymo_msgbox_ui_enter_with_size(e, message, choose, NULL, fontsize, true,
		cpymo_localization_get(e)->legacy_notice_back);
}

void cpymo_msgbox_ui_enable_button_outline(cpymo_engine *e)
{
	cpymo_msgbox_ui *ui = cpymo_ui_data(e);
	ui->outline_unselected = true;
}
#endif

