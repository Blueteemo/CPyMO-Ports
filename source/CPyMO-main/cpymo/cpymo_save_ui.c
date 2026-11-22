#include "cpymo_prelude.h"
#include "cpymo_save_ui.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include "cpymo_save.h"
#include "cpymo_msgbox_ui.h"
#include "cpymo_localization.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#ifdef GARLIC_NO_FFMPEG
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#endif

#ifndef CPYMO_MAX_SAVES
#define CPYMO_MAX_SAVES 31
#endif

typedef struct {
	cpymo_backend_text text;
#ifdef GARLIC_NO_FFMPEG
	cpymo_backend_text dialogue[2], timestamp;
#endif
#ifdef ENABLE_TEXT_EXTRACT
	char *orginal_text;
#endif
	bool is_empty_save;
} cpymo_save_ui_item;

typedef struct {
	cpymo_save_ui_item items[CPYMO_MAX_SAVES];
	bool is_load_ui;
#ifdef GARLIC_NO_FFMPEG
	float row_height, margin, font_size;
#endif
} cpymo_save_ui;

enum { SAVE_ROWS_PER_SCREEN = 3 };

#ifdef DISABLE_AUTOSAVE
const static size_t first_slot = 0;
#else
const static size_t first_slot = 1;
#endif

#ifdef GARLIC_NO_FFMPEG
enum { SAVE_PREVIEW_LINE_CAPACITY = 512 };

/* Trim on a UTF-8 boundary using the same measurements as the renderer. */
static cpymo_str cpymo_save_preview_fit(char *out, size_t capacity, const char *prefix,
	cpymo_str rest, const char *suffix, float width, float size, bool wrap)
{
	static const char ellipsis[] = "…";
	const size_t start = strlen(prefix);
	const size_t ending_size = strlen(suffix) + 1;
	const size_t reserve = sizeof(ellipsis) - 1 + ending_size;
	if (start + reserve > capacity) {
		out[0] = '\0';
		return rest;
	}
	memcpy(out, prefix, start);
	memcpy(out + start, suffix, ending_size);
	size_t used = start;
	while (rest.len) {
		cpymo_str before = rest;
		cpymo_str glyph = cpymo_str_utf8_try_head(&rest);
		if (glyph.len == 1 && glyph.begin[0] == '\\' && rest.len &&
			(rest.begin[0] == 'n' || rest.begin[0] == 'r')) {
			cpymo_str_utf8_try_head(&rest);
			glyph = cpymo_str_pure(" ");
		} else if (glyph.len == 1 && (unsigned char)glyph.begin[0] < 32) {
			glyph = cpymo_str_pure(" ");
		}

		if (used + glyph.len + reserve > capacity) {
			rest = before;
			break;
		}
		const size_t end = used + glyph.len;
		memcpy(out + used, glyph.begin, glyph.len);
		if (wrap && rest.len) out[end] = '\0';
		else memcpy(out + end, suffix, ending_size);
		if (cpymo_backend_text_width(cpymo_str_pure(out), size) > width) {
			rest = before;
			break;
		}
		used = end;
	}
	if (!rest.len) return rest;
	if (wrap) {
		out[used] = '\0';
		return rest;
	}
	for (;;) {
		memcpy(out + used, ellipsis, sizeof(ellipsis) - 1);
		memcpy(out + used + sizeof(ellipsis) - 1, suffix, ending_size);
		if (cpymo_backend_text_width(cpymo_str_pure(out), size) <= width) break;
		if (used == start) {
			memcpy(out + used, suffix, ending_size);
			break;
		}
		do { --used; } while (((unsigned char)out[used] & 0xc0) == 0x80);
	}
	return rest;
}

static error_t cpymo_save_preview_text(cpymo_backend_text *text, const char *line, float size)
{
	float measured_width;
	return cpymo_backend_text_create(text, &measured_width, cpymo_str_pure(line), size);
}

static error_t cpymo_save_preview_create(cpymo_save_ui_item *item, cpymo_engine *e,
	const cpymo_save_ui *ui, size_t slot, const cpymo_save_title *title, const char *timestamp)
{
	const cpymo_localization *l = cpymo_localization_get(e);
	const char *heading = item->is_empty_save ? l->save_empty_slot : title->title;
	char *label = NULL;
	error_t err;
	if (slot == 0 && first_slot != 0) err = l->save_auto_title(&label, heading);
	else err = l->save_title(&label, (int)slot, heading);
	CPYMO_THROW(err);

	const float size = ui->font_size;
	/* Leave room for the rasterizer's overhang and one-pixel shadow. */
	const float width = e->gameconfig.imagesize_w - 2 * ui->margin
		- garlic_text_output_pixels(2.0f);
	char line[SAVE_PREVIEW_LINE_CAPACITY];
	char dialogue[2][SAVE_PREVIEW_LINE_CAPACITY] = {{0}};
	cpymo_save_preview_fit(line, sizeof(line), "", cpymo_str_pure(label), "", width, size, false);
	free(label);
	err = cpymo_save_preview_text(&item->text, line, size);
	CPYMO_THROW(err);

	if (!item->is_empty_save && *title->say_text) {
		char name[96], prefix[sizeof(name) + sizeof("「")];
		cpymo_save_preview_fit(name, sizeof(name), "", cpymo_str_pure(title->say_name),
			"", width / 3, size, false);
		snprintf(prefix, sizeof(prefix), "%s「", name);
		cpymo_str rest = cpymo_save_preview_fit(dialogue[0], sizeof(dialogue[0]), prefix,
			cpymo_str_pure(title->say_text), "」", width, size, true);
		err = cpymo_save_preview_text(&item->dialogue[0], dialogue[0], size);
		CPYMO_THROW(err);
		if (rest.len) {
			cpymo_save_preview_fit(dialogue[1], sizeof(dialogue[1]), "", rest,
				"」", width, size, false);
			err = cpymo_save_preview_text(&item->dialogue[1], dialogue[1], size);
			CPYMO_THROW(err);
		}
	}
	if (*timestamp) {
		err = cpymo_save_preview_text(&item->timestamp, timestamp, size * (15.0f / 16.0f));
		CPYMO_THROW(err);
	}
#ifdef ENABLE_TEXT_EXTRACT
	char readout[3 * SAVE_PREVIEW_LINE_CAPACITY + 32];
	snprintf(readout, sizeof(readout), "%s\n%s\n%s\n%s", line, dialogue[0], dialogue[1], timestamp);
	item->orginal_text = cpymo_str_copy_malloc(cpymo_str_pure(readout));
	if (!item->orginal_text) return CPYMO_ERR_OUT_OF_MEM;
#endif
	return CPYMO_ERR_SUCC;
}

static void cpymo_save_preview_draw_text(cpymo_backend_text text, float margin,
	float baseline, cpymo_color color)
{
	if (!text) return;
	cpymo_backend_text_draw(text, margin - garlic_text_ink_left(text), baseline,
		color, 1.0f, cpymo_backend_image_draw_type_ui_element);
}
#endif

static void cpymo_save_ui_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);
	const cpymo_save_ui_item *item = &ui->items[cpymo_list_ui_encode_uint_node_dec(node_to_draw)];

#ifdef GARLIC_NO_FFMPEG
	const float row = ui->row_height;
	const cpymo_color secondary = {210, 217, 226};
	/* Title, both dialogue lines and date have fixed row-relative baselines. */
	cpymo_save_preview_draw_text(item->text, ui->margin, y + row * (18.0f / 80.0f),
		item->is_empty_save ? secondary : cpymo_color_white);
	cpymo_save_preview_draw_text(item->dialogue[0], ui->margin, y + row * (38.0f / 80.0f), cpymo_color_white);
	cpymo_save_preview_draw_text(item->dialogue[1], ui->margin, y + row * (56.0f / 80.0f), cpymo_color_white);
	cpymo_save_preview_draw_text(item->timestamp, ui->margin, y + row * (73.0f / 80.0f), secondary);
#else
	cpymo_backend_text_draw(
		item->text,
		0,
		y + cpymo_gameconfig_font_size(&e->gameconfig),
		cpymo_color_white,
		1.0f,
		cpymo_backend_image_draw_type_ui_element);
#endif
}

static error_t cpymo_save_confirm(cpymo_engine *e, void *data, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	uintptr_t save_id = cpymo_list_ui_encode_uint_node_dec(data);
	error_t err = cpymo_save_write(e, (unsigned short)save_id);

	const cpymo_localization *l = cpymo_localization_get(e);

	char *msg = NULL;
	error_t errstr;
	if (err == CPYMO_ERR_SUCC) 
		errstr = l->save_already_save_to(&msg, (int)save_id);
	else 
		errstr = l->save_failed(&msg, err);
	
	if (errstr != CPYMO_ERR_SUCC) return errstr;

	cpymo_ui_exit(e);

	err = cpymo_msgbox_ui_enter(
		e,
		cpymo_str_pure(msg),
		NULL,
		NULL);

	free(msg);

	return err;
}

static error_t cpymo_save_ui_ok(cpymo_engine *e, void *selected)
{
	cpymo_save_ui *ui = (cpymo_save_ui *)cpymo_list_ui_data(e);

	uintptr_t save_id = cpymo_list_ui_encode_uint_node_dec(selected);

	if (ui->is_load_ui) {
		if (!ui->items[save_id].is_empty_save) {
			cpymo_list_ui_ignore_key_hold_exit_onetime(e);
			return cpymo_save_ui_load_savedata_yesnobox(e, (unsigned short)save_id);
		}
		else return CPYMO_ERR_SUCC;
	}
	else {
		char *msg = NULL;
		const cpymo_localization *l = cpymo_localization_get(e);
		error_t err = l->save_are_you_sure_save_to(&msg, (int)save_id);
		CPYMO_THROW(err);
		err = cpymo_msgbox_ui_enter(
			e,
			cpymo_str_pure(msg),
			&cpymo_save_confirm,
			selected);
		free(msg);
		return err;
	}
}

static void cpymo_save_ui_deleter(cpymo_engine *e, void *ui_data)
{
	cpymo_save_ui *ui = (cpymo_save_ui *)cpymo_list_ui_data(e);

	for (size_t i = 0; i < CPYMO_MAX_SAVES; ++i) {
		if (ui->items[i].text) 
			cpymo_backend_text_free(ui->items[i].text);
#ifdef GARLIC_NO_FFMPEG
		for (size_t line = 0; line < 2; ++line)
			if (ui->items[i].dialogue[line]) cpymo_backend_text_free(ui->items[i].dialogue[line]);
		if (ui->items[i].timestamp) cpymo_backend_text_free(ui->items[i].timestamp);
#endif
#ifdef ENABLE_TEXT_EXTRACT
		if (ui->items[i].orginal_text) free(ui->items[i].orginal_text);
#endif
	}
}

static void* cpymo_save_ui_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
	if (i >= CPYMO_MAX_SAVES - 1) 
		return NULL;
	else return cpymo_list_ui_encode_uint_node_enc(i + 1);
}

static void* cpymo_save_ui_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);

	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
	if (i == (ui->is_load_ui ? 0 : first_slot)) return NULL;
	else return cpymo_list_ui_encode_uint_node_enc(i - 1);
}

#ifdef ENABLE_TEXT_EXTRACT
static error_t cpymo_save_ui_visual_impaired_selection_changed(cpymo_engine *e, void *cur)
{
	if (cur) {
		uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
		const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data(e);
		cpymo_backend_text_extract(ui->items[i].orginal_text);
	}
	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_save_ui_enter(cpymo_engine *e, bool is_load_ui)
{
	cpymo_save_ui *ui = NULL;
	uintptr_t first = is_load_ui ? 0 : first_slot;
	error_t err = cpymo_list_ui_enter(
		e,
		(void **)&ui,
		sizeof(*ui),
		&cpymo_save_ui_draw_node,
		&cpymo_save_ui_ok,
		&cpymo_save_ui_deleter,
		cpymo_list_ui_encode_uint_node_enc(first),
		&cpymo_save_ui_get_next,
		&cpymo_save_ui_get_prev,
		false,
		SAVE_ROWS_PER_SCREEN);
	CPYMO_THROW(err);
	
	cpymo_list_ui_enable_loop(e);

#ifdef ENABLE_TEXT_EXTRACT
	cpymo_list_ui_set_selection_changed_callback(e, &cpymo_save_ui_visual_impaired_selection_changed);
	ui->items[0].orginal_text = NULL;
#endif

	ui->is_load_ui = is_load_ui;

#ifdef GARLIC_NO_FFMPEG
	memset(ui->items, 0, sizeof(ui->items));
	ui->row_height = (float)e->gameconfig.imagesize_h / SAVE_ROWS_PER_SCREEN;
	ui->margin = fminf(ui->row_height * 0.10f, e->gameconfig.imagesize_w * 0.05f);
	ui->font_size = fminf(cpymo_gameconfig_font_size(&e->gameconfig),
		fminf(ui->row_height * 0.20f, e->gameconfig.imagesize_w / 12.0f));

	for (size_t i = first; i < CPYMO_MAX_SAVES; ++i) {
		cpymo_save_title title = {0};
		char timestamp[24] = "";
		cpymo_save_ui_item *item = &ui->items[i];
		item->is_empty_save = true;
		FILE *save = cpymo_save_open_read(e, (unsigned short)i);
		if (save) {
			err = cpymo_save_load_title(&title, save);
			/* Old and partial saves can lack a complete preview header. */
			item->is_empty_save = err != CPYMO_ERR_SUCC ||
				!title.title || !title.say_name || !title.say_text;
			struct stat status;
			struct tm modified;
			if (!item->is_empty_save && fstat(fileno(save), &status) == 0 &&
				status.st_mtime > 0 && localtime_r(&status.st_mtime, &modified) &&
				!strftime(timestamp, sizeof(timestamp), "%m-%d %H:%M", &modified))
				timestamp[0] = '\0';
			fclose(save);
		}
		err = cpymo_save_preview_create(item, e, ui, i, &title, timestamp);
		free(title.say_name);
		free(title.say_text);
		free(title.title);
		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}
#else
	for (size_t i = 0; i < CPYMO_MAX_SAVES; ++i) {
		ui->items[i].text = NULL;
		ui->items[i].is_empty_save = false;
	}

	const float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
	size_t characters = (size_t)((float)e->gameconfig.imagesize_w / fontsize * 1.3f);

	char text_buf[1024];
	for (size_t i = is_load_ui ? 0 : first_slot; i < CPYMO_MAX_SAVES; ++i) {
		FILE *save = cpymo_save_open_read(e, (unsigned short)i);
		cpymo_save_title title;
		title.say_name = NULL;
		title.say_text = NULL;
		title.title = NULL;

		if (save) {
			error_t err = cpymo_save_load_title(&title, save);
			fclose(save);
			if (err != CPYMO_ERR_SUCC) ui->items[i].is_empty_save = true;
		}
		else {
			ui->items[i].is_empty_save = true;
		}

		if (!ui->items[i].is_empty_save) {
			assert(title.say_name != NULL);
			assert(title.title != NULL);
			assert(title.say_text != NULL);

			char *tmp_str = (char *)malloc(strlen(title.say_name) + strlen(title.say_text) + 16);
			if (tmp_str == NULL) {
				cpymo_ui_exit(e);
				free(title.say_name);
				free(title.say_text);
				free(title.title);
				return CPYMO_ERR_OUT_OF_MEM;
			}

			*tmp_str = '\0';
			if (strlen(title.say_name) > 0) {
				strcat(tmp_str, title.say_name);
				strcat(tmp_str, " ");
			}

			strcat(tmp_str, title.say_text);

			cpymo_str say_preview_text_tail = cpymo_str_pure(tmp_str);
			cpymo_str say_preview_text = 
				cpymo_str_split(&say_preview_text_tail, characters);

			if (say_preview_text_tail.len > 0) {
				*(char *)say_preview_text_tail.begin = '\0';

				if (say_preview_text.len > 3) {
					((char *)say_preview_text.begin)[say_preview_text.len - 1] = '.';
					((char *)say_preview_text.begin)[say_preview_text.len - 2] = '.';
					((char *)say_preview_text.begin)[say_preview_text.len - 3] = '.';
				}
			}

			const cpymo_localization *l = cpymo_localization_get(e);

			char *str = NULL;
			error_t err;

			#ifndef DISABLE_AUTOSAVE
			if (i == 0) err = l->save_auto_title(&str, title.title);
			else 
			#endif
				err = l->save_title(&str, (int)i, title.title);

			if (err != CPYMO_ERR_SUCC) {
				free(tmp_str);
				free(title.say_name);
				free(title.title);
				free(title.say_text);
				return err;
			}

			snprintf(text_buf, sizeof(text_buf), "%s\n%s", str, tmp_str);
			free(str);

			free(tmp_str);

			free(title.say_name);
			free(title.title);
			free(title.say_text);

			cpymo_utils_replace_str_newline_n(text_buf);
		}
		else {
			error_t err;
			const cpymo_localization *l = cpymo_localization_get(e);
			char *msg = NULL;

			#ifndef DISABLE_AUTOSAVE
			if (i == 0) err = l->save_auto_title(&msg, "");
			else 
			#endif
				err = l->save_title(&msg, (int)i, "");

			CPYMO_THROW(err);
			strncpy(text_buf, msg, sizeof(text_buf) - 1);
			free(msg);
		}

		float w;
		error_t err = cpymo_backend_text_create(
			&ui->items[i].text, &w, cpymo_str_pure(text_buf), fontsize);

#ifdef ENABLE_TEXT_EXTRACT
		ui->items[i].orginal_text = 
			cpymo_str_copy_malloc(cpymo_str_pure(text_buf));
#endif

		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}
#endif

	return CPYMO_ERR_SUCC;
}

static error_t cpymo_save_ui_load_savedata_yesnobox_confirm(
	cpymo_engine *e, void *save_id_x, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	FILE *file = cpymo_save_open_read(e, (unsigned short)(uintptr_t)save_id_x);
	if (file == NULL) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
	error_t err = cpymo_save_load_savedata(e, file);
	fclose(file);
	return err;
}

error_t cpymo_save_ui_load_savedata_yesnobox(cpymo_engine * e, unsigned short save_id)
{
	char *str = NULL;
	error_t err = CPYMO_ERR_SUCC;
	const cpymo_localization *l = cpymo_localization_get(e);

	#ifndef DISABLE_AUTOSAVE
	if (save_id) 
	#endif
	{
		err = l->save_are_you_sure_load(&str, (int)save_id);
		CPYMO_THROW(err);
	}

	err = cpymo_msgbox_ui_enter(
		e,
		cpymo_str_pure(str ? str : l->save_are_you_sure_load_auto_save),
		&cpymo_save_ui_load_savedata_yesnobox_confirm,
		(void *)(uintptr_t)save_id);

	if (str) free(str);

	return err;
}


