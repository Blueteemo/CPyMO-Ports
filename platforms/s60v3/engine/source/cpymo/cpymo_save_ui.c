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
#ifdef S60V3
#include "cpymo_s60_pymo_save.h"
#include "../cpymo-backends/sdl1/cpymo_s60_text_metrics.h"
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#endif

#ifndef CPYMO_MAX_SAVES
#define CPYMO_MAX_SAVES 31
#endif

typedef struct {
	cpymo_backend_text text;
#ifdef S60V3
	cpymo_backend_text preview[2], updated;
#endif
#ifdef ENABLE_TEXT_EXTRACT
	char *orginal_text;
#endif
	bool is_empty_save;
} cpymo_save_ui_item;

typedef struct {
	cpymo_save_ui_item items[CPYMO_MAX_SAVES];
	bool is_load_ui;
#ifdef S60V3
	cpymo_backend_text import_label;
	float font_size, preview_offset_y;
#endif
} cpymo_save_ui;

#ifdef DISABLE_AUTOSAVE
const static size_t first_slot = 0;
#else
const static size_t first_slot = 1;
#endif

#ifdef S60V3
/* Keep the closing punctuation when a line exceeds the measured text column. */
static cpymo_str cpymo_s60_save_fit(char *out, size_t capacity, const char *prefix,
	cpymo_str rest, const char *suffix, float width, float size, bool wrap)
{
	const char ellipsis[] = "…";
	const size_t end_size = strlen(suffix) + 1;
	const size_t prefix_size = strlen(prefix);
	size_t end = prefix_size;
	memcpy(out, prefix, end);
	memcpy(out + end, suffix, end_size);
	while (rest.len) {
		const cpymo_str next_line = rest;
		cpymo_str glyph;
		if (rest.len >= 2 && rest.begin[0] == '\\' &&
			(rest.begin[1] == 'n' || rest.begin[1] == 'r')) {
			glyph = cpymo_str_pure(" ");
			rest.begin += 2;
			rest.len -= 2;
		} else {
			glyph = cpymo_str_utf8_try_head(&rest);
			if (!glyph.len) break;
			if (glyph.len == 1 && (unsigned char)glyph.begin[0] < 32)
				glyph = cpymo_str_pure(" ");
		}
		if (end + glyph.len + sizeof(ellipsis) - 1 + end_size <= capacity) {
			memcpy(out + end, glyph.begin, glyph.len);
			if (wrap && rest.len) out[end + glyph.len] = '\0';
			else memcpy(out + end + glyph.len, suffix, end_size);
			if (cpymo_backend_text_width(cpymo_str_pure(out), size) <= width) {
				end += glyph.len;
				continue;
			}
		}
		rest = next_line;
		if (wrap) {
			out[end] = '\0';
			return rest;
		}
		goto truncated;
	}
	return rest;

truncated:
	for (;;) {
		memcpy(out + end, ellipsis, sizeof(ellipsis) - 1);
		memcpy(out + end + sizeof(ellipsis) - 1, suffix, end_size);
		if (cpymo_backend_text_width(cpymo_str_pure(out), size) <= width) return rest;
		if (end == prefix_size) {
			memcpy(out + end, suffix, end_size);
			return rest;
		}
		do { --end; } while (((unsigned char)out[end] & 0xc0) == 0x80);
	}
}

static float cpymo_s60_save_date_size(float size)
{
	return size > 15.0f ? size - 1.0f : size;
}

static float cpymo_s60_save_preview_offset(float size, float row_height)
{
	/* Measure representative text once so every card shares the same baseline. */
	cpymo_backend_text sample;
	float width, body[4], date[4];
	if (cpymo_backend_text_create(&sample, &width, cpymo_str_pure("存档"), size)
			!= CPYMO_ERR_SUCC) return 0.0f;
	cpymo_s60_text_ink(sample, body);
	cpymo_backend_text_free(sample);
	if (cpymo_backend_text_create(&sample, &width, cpymo_str_pure("00-00 00:00"),
			cpymo_s60_save_date_size(size)) != CPYMO_ERR_SUCC) return 0.0f;
	cpymo_s60_text_ink(sample, date);
	cpymo_backend_text_free(sample);

	const float step = size + 2.0f;
	const float gap_above = step + body[1] - body[3];
	const float gap_below = row_height - 5.0f + date[1] - (3.0f * step + body[3]);
	const float centered = floorf((gap_below - gap_above) * 0.5f + 0.5f);
	return fmaxf(0.0f, fminf(centered - 1.0f, floorf(gap_below)));
}

static error_t cpymo_s60_save_create_item(cpymo_save_ui_item *item,
	size_t slot, const cpymo_save_title *title, const char *timestamp, float size, float width)
{
	char prefix[64], line[512];
	const char *heading = item->is_empty_save ? "空白" : title->title;
	if (slot == 0) snprintf(prefix, sizeof(prefix), "自动存档%s", *heading ? "　" : "");
	else snprintf(prefix, sizeof(prefix), "存档 %02u%s",
		(unsigned int)slot, *heading ? "　" : "");
	cpymo_s60_save_fit(line, sizeof(line), prefix, cpymo_str_pure(heading), "", width, size, false);
	float rendered_width;
	error_t err = cpymo_backend_text_create(&item->text, &rendered_width,
		cpymo_str_pure(line), size);
	CPYMO_THROW(err);

	if (item->is_empty_save) return CPYMO_ERR_SUCC;
	if (*title->say_text) {
		char speaker[96], opening[104];
		cpymo_s60_save_fit(speaker, sizeof(speaker), "", cpymo_str_pure(title->say_name),
			"", width / 3.0f, size, false);
		snprintf(opening, sizeof(opening), "%s「", speaker);
		cpymo_str remaining = cpymo_s60_save_fit(line, sizeof(line), opening,
			cpymo_str_pure(title->say_text), "」", width, size, true);
		err = cpymo_backend_text_create(&item->preview[0], &rendered_width,
			cpymo_str_pure(line), size);
		CPYMO_THROW(err);
		if (remaining.len) {
			cpymo_s60_save_fit(line, sizeof(line), "", remaining, "」", width, size, false);
			err = cpymo_backend_text_create(&item->preview[1], &rendered_width,
				cpymo_str_pure(line), size);
			CPYMO_THROW(err);
		}
	}
	if (*timestamp) {
		err = cpymo_backend_text_create(&item->updated, &rendered_width,
			cpymo_str_pure(timestamp), cpymo_s60_save_date_size(size));
		CPYMO_THROW(err);
	}
	return CPYMO_ERR_SUCC;
}
#endif

static void cpymo_save_ui_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);
	const uintptr_t index = cpymo_list_ui_encode_uint_node_dec(node_to_draw);
	cpymo_backend_text text;
	float x = 0.0f;
#ifdef S60V3
	x = 8.0f;
	if (index != CPYMO_MAX_SAVES) {
		const cpymo_save_ui_item *slot = &ui->items[index];
		const float baseline = y + ui->font_size + 2.0f;
		const float step = ui->font_size + 2.0f;
		const cpymo_color secondary = {209, 216, 226};
		cpymo_backend_text_draw(slot->text, x, baseline,
			slot->is_empty_save ? secondary : cpymo_color_white, 1.0f,
			cpymo_backend_image_draw_type_ui_element);
		float next = baseline + step + ui->preview_offset_y;
		for (size_t i = 0; i < 2 && slot->preview[i]; ++i, next += step)
			cpymo_backend_text_draw(slot->preview[i], x, next, cpymo_color_white, 1.0f,
				cpymo_backend_image_draw_type_ui_element);
		if (slot->updated) cpymo_backend_text_draw(slot->updated, x,
			y + cpymo_list_ui_row_height(e) - 5.0f,
			secondary, 1.0f, cpymo_backend_image_draw_type_ui_element);
		return;
	}
	text = ui->import_label;
	const float baseline = y + ui->font_size + 2.0f;
#else
	text = ui->items[index].text;
	const float baseline = y + cpymo_gameconfig_font_size(&e->gameconfig);
#endif
	cpymo_backend_text_draw(
		text,
		x,
		baseline,
		cpymo_color_white,
		1.0f,
		cpymo_backend_image_draw_type_ui_element);
}

#ifdef S60V3
typedef struct {
    size_t count;
    unsigned char slots[CPYMO_S60_PYMO_SAVE_COUNT];
    cpymo_backend_text labels[CPYMO_S60_PYMO_SAVE_COUNT];
} cpymo_pymo_slots;

static void import_slots_delete(cpymo_engine *e, void *data)
{
    (void)data;
    cpymo_pymo_slots *slots = cpymo_list_ui_data(e);
    for (size_t i = 0; i < slots->count; ++i)
        if (slots->labels[i]) cpymo_backend_text_free(slots->labels[i]);
}

static void import_slots_draw(const cpymo_engine *e, const void *node, float y)
{
    const cpymo_pymo_slots *slots = cpymo_list_ui_data_const(e);
    const size_t index = cpymo_list_ui_encode_uint_node_dec(node);
    cpymo_backend_text_draw(slots->labels[index], 12.0f, y + 26.0f,
        cpymo_color_white, 1.0f, cpymo_backend_image_draw_type_ui_element);
}

static void *import_slots_next(const cpymo_engine *e, const void *data, const void *node)
{
    (void)e;
    const cpymo_pymo_slots *slots = data;
    size_t index = cpymo_list_ui_encode_uint_node_dec(node);
    return index + 1 < slots->count ? cpymo_list_ui_encode_uint_node_enc(index + 1) : NULL;
}

static void *import_slots_prev(const cpymo_engine *e, const void *data, const void *node)
{
    (void)e;
    (void)data;
    size_t index = cpymo_list_ui_encode_uint_node_dec(node);
    return index ? cpymo_list_ui_encode_uint_node_enc(index - 1) : NULL;
}

static error_t import_slot_confirm(cpymo_engine *e, void *node, bool confirm)
{
    if (!confirm) return CPYMO_ERR_SUCC;
    const cpymo_pymo_slots *slots = cpymo_list_ui_data_const(e);
    const unsigned int source = slots->slots[cpymo_list_ui_encode_uint_node_dec(node)];
    unsigned short dest = 1; /* Keep CPyMO's automatic slot available to the engine. */
    for (; dest < CPYMO_MAX_SAVES; ++dest) {
        FILE *used = cpymo_save_open_read(e, dest);
        if (!used) break;
        fclose(used);
    }
    if (dest == CPYMO_MAX_SAVES)
        return cpymo_msgbox_ui_enter(e, cpymo_str_pure("没有空白存档位。"), NULL, NULL);

    error_t err = cpymo_s60_pymo_save_import(e, source, dest);
    if (err != CPYMO_ERR_SUCC) {
        printf("[S60] PyMO save import %u: %s\n", source, cpymo_error_message(err));
        return cpymo_msgbox_ui_enter(e, cpymo_str_pure("无法导入此 PyMO 存档。"), NULL, NULL);
    }
    cpymo_ui_exit(e); /* PyMO slots */
    cpymo_ui_exit(e); /* Refresh the CPyMO slots from disk. */
    err = cpymo_save_ui_enter(e, true);
    CPYMO_THROW(err);
    cpymo_list_ui_set_current_node(e, cpymo_list_ui_encode_uint_node_enc(dest));
    cpymo_list_ui_set_selection_relative_to_cur(e, 0);
    char notice[80];
    snprintf(notice, sizeof(notice), "已导入到存档 %u。", (unsigned int)dest);
    return cpymo_msgbox_ui_enter(e, cpymo_str_pure(notice), NULL, NULL);
}

static error_t import_slots_ok(cpymo_engine *e, void *node)
{
    return cpymo_msgbox_ui_enter(e, cpymo_str_pure("确定要导入此 PyMO 存档吗？"),
        &import_slot_confirm, node);
}

static error_t import_slots_open(cpymo_engine *e)
{
    unsigned char original[CPYMO_S60_PYMO_SAVE_COUNT];
    const size_t count = cpymo_s60_pymo_save_list(e, original);
    if (!count) return CPYMO_ERR_SUCC;
    cpymo_pymo_slots *list = NULL;
    error_t err = cpymo_list_ui_enter(e, (void **)&list, sizeof(*list),
        &import_slots_draw, &import_slots_ok, &import_slots_delete,
        cpymo_list_ui_encode_uint_node_enc(0), &import_slots_next, &import_slots_prev, false, 4);
    CPYMO_THROW(err);
    memset(list, 0, sizeof(*list));
    list->count = count;
    memcpy(list->slots, original, count);
    for (size_t i = 0; i < count; ++i) {
        char label[48];
        if (original[i] == 0) strcpy(label, "PyMO 自动存档");
        else snprintf(label, sizeof(label), "PyMO 存档 %u", (unsigned int)original[i]);
        float width;
        err = cpymo_backend_text_create(&list->labels[i], &width, cpymo_str_pure(label), 16.0f);
        if (err != CPYMO_ERR_SUCC) {
            cpymo_ui_exit(e);
            return err;
        }
    }
    cpymo_list_ui_set_s60_scrollbar(e, count);
    return CPYMO_ERR_SUCC;
}
#endif

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

#ifdef S60V3
	if (save_id == CPYMO_MAX_SAVES) return import_slots_open(e);
#endif
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

#ifdef S60V3
	if (ui->import_label) cpymo_backend_text_free(ui->import_label);
#endif
	for (size_t i = 0; i < CPYMO_MAX_SAVES; ++i) {
		if (ui->items[i].text) 
			cpymo_backend_text_free(ui->items[i].text);
#ifdef S60V3
		for (size_t line = 0; line < 2; ++line)
			if (ui->items[i].preview[line]) cpymo_backend_text_free(ui->items[i].preview[line]);
		if (ui->items[i].updated) cpymo_backend_text_free(ui->items[i].updated);
#endif
#ifdef ENABLE_TEXT_EXTRACT
		if (ui->items[i].orginal_text) free(ui->items[i].orginal_text);
#endif
	}
}

static void* cpymo_save_ui_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
#ifdef S60V3
	if (i == CPYMO_MAX_SAVES) return cpymo_list_ui_encode_uint_node_enc(0);
#endif
	if (i >= CPYMO_MAX_SAVES - 1) 
		return NULL;
	else return cpymo_list_ui_encode_uint_node_enc(i + 1);
}

static void* cpymo_save_ui_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);

	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
#ifdef S60V3
	if (i == CPYMO_MAX_SAVES) return NULL;
	if (i == 0 && ui->import_label) return cpymo_list_ui_encode_uint_node_enc(CPYMO_MAX_SAVES);
#endif
	if (i == (ui->is_load_ui ? 0 : first_slot)) return NULL;
	else return cpymo_list_ui_encode_uint_node_enc(i - 1);
}

#ifdef ENABLE_TEXT_EXTRACT
static error_t cpymo_save_ui_visual_impaired_selection_changed(cpymo_engine *e, void *cur)
{
	if (cur) {
		uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
		const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data(e);
#ifdef S60V3
		if (i == CPYMO_MAX_SAVES) {
			cpymo_backend_text_extract("导入 PyMO 存档");
			return CPYMO_ERR_SUCC;
		}
#endif
		cpymo_backend_text_extract(ui->items[i].orginal_text);
	}
	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_save_ui_enter(cpymo_engine *e, bool is_load_ui)
{
	cpymo_save_ui *ui = NULL;
	uintptr_t first = is_load_ui ? 0 : first_slot;
#ifdef S60V3
	if (is_load_ui && e->gameconfig.imagesize_w == 320 && e->gameconfig.imagesize_h == 240 &&
			cpymo_gameconfig_is_symbian(&e->gameconfig)) {
		unsigned char slots[CPYMO_S60_PYMO_SAVE_COUNT];
		if (cpymo_s60_pymo_save_list(e, slots)) first = CPYMO_MAX_SAVES;
	}
#endif
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
		3);
	CPYMO_THROW(err);
	
	cpymo_list_ui_enable_loop(e);

#ifdef ENABLE_TEXT_EXTRACT
	cpymo_list_ui_set_selection_changed_callback(e, &cpymo_save_ui_visual_impaired_selection_changed);
	ui->items[0].orginal_text = NULL;
#endif

	ui->is_load_ui = is_load_ui;
	memset(ui->items, 0, sizeof(ui->items));

	const float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
#ifdef S60V3
	const float row_height = cpymo_list_ui_row_height(e);
	ui->font_size = fminf(fontsize, (row_height - 9.0f) / 4.0f);
	ui->preview_offset_y = cpymo_s60_save_preview_offset(ui->font_size, row_height);
	ui->import_label = NULL;
	if (first == CPYMO_MAX_SAVES) {
		float width;
		err = cpymo_backend_text_create(&ui->import_label, &width,
			cpymo_str_pure("导入 PyMO 存档"), ui->font_size);
		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
		cpymo_list_ui_set_selection_relative_to_cur(e, 1);
	}
#else
	size_t characters = (size_t)((float)e->gameconfig.imagesize_w / fontsize * 1.3f);

	char text_buf[1024];
#endif
	for (size_t i = is_load_ui ? 0 : first_slot; i < CPYMO_MAX_SAVES; ++i) {
		FILE *save = cpymo_save_open_read(e, (unsigned short)i);
		cpymo_save_title title;
		title.say_name = NULL;
		title.say_text = NULL;
		title.title = NULL;

#ifdef S60V3
		char timestamp[24] = "";
#endif
		if (save) {
			error_t err = cpymo_save_load_title(&title, save);
#ifdef S60V3
			struct stat status;
			if (err == CPYMO_ERR_SUCC && fstat(fileno(save), &status) == 0 && status.st_mtime > 0) {
				const struct tm *modified = localtime(&status.st_mtime);
				if (modified && !strftime(timestamp, sizeof(timestamp), "%m-%d %H:%M", modified))
					timestamp[0] = '\0';
			}
#endif
			fclose(save);
			if (err != CPYMO_ERR_SUCC) ui->items[i].is_empty_save = true;
		}
		else {
			ui->items[i].is_empty_save = true;
		}

#ifdef S60V3
		error_t err = cpymo_s60_save_create_item(&ui->items[i], i, &title, timestamp,
			ui->font_size, e->gameconfig.imagesize_w - 16.0f);
		free(title.say_name);
		free(title.say_text);
		free(title.title);
#ifdef ENABLE_TEXT_EXTRACT
		if (!ui->items[i].is_empty_save && err == CPYMO_ERR_SUCC) {
			char label[32];
			if (i == 0) strcpy(label, "自动存档");
			else snprintf(label, sizeof(label), "存档 %02u", (unsigned int)i);
			ui->items[i].orginal_text = cpymo_str_copy_malloc(cpymo_str_pure(label));
		}
#endif
#else
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
#endif /* S60V3 */

		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}

	return CPYMO_ERR_SUCC;
}

static error_t cpymo_save_ui_load_savedata_yesnobox_confirm(
	cpymo_engine *e, void *save_id_x, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	FILE *file = cpymo_save_open_read(e, (unsigned short)(uintptr_t)save_id_x);
	if (file == NULL) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
#ifdef S60V3
    if (cpymo_gameconfig_is_symbian(&e->gameconfig)) {
        cpymo_s60_restore_with_fade(e, file);
        return CPYMO_ERR_SUCC;
    }
#endif
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


