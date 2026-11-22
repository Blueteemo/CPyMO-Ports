#include "cpymo_prelude.h"
#ifdef S60V3
#include "cpymo_s60_pymo_save.h"
#include "../endianness.h/endianness.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { SAVE_LINES = 14, MAX_LEGACY_SAVE_BYTES = 65536, MAX_CALLERS = 64 };

static bool integer(const char *text, int32_t *out)
{
    char *end;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (!*text || *end || errno || value < INT32_MIN || value > INT32_MAX)
        return false;
    *out = (int32_t)value;
    return true;
}

static bool asset_name(const char *text)
{
    const size_t length = strlen(text);
    if (length == 0 || length > 255 || !strcmp(text, ".") || !strcmp(text, ".."))
        return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p < 32 || strchr("/\\:", *p)) return false;
    return true;
}

static char *next_field(char **tail)
{
    if (!*tail || !**tail) return NULL;
    char *start = *tail;
    char *sep = strchr(start, ',');
    if (sep) *sep++ = '\0';
    *tail = sep;
    return start;
}

static bool integer_field(char **tail, int32_t *value)
{
    const char *field = next_field(tail);
    return field && integer(field, value);
}

static bool has_field(const char *tail)
{
    return tail && *tail;
}

static char *save_path(const cpymo_engine *e, size_t extra)
{
    const char *base = e->assetloader.gamedir;
    char *path = malloc(strlen(base) + sizeof("/save") + extra);
    if (path) sprintf(path, "%s/save", base);
    return path;
}

size_t cpymo_s60_pymo_save_list(const cpymo_engine *e,
    unsigned char slots[CPYMO_S60_PYMO_SAVE_COUNT])
{
    char *path = save_path(e, 0);
    if (!path) return 0;
    DIR *dir = opendir(path);
    free(path);
    if (!dir) return 0;
    bool present[CPYMO_S60_PYMO_SAVE_COUNT] = {false};
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (*name < '0' || *name > '9') continue;
        char *end;
        const long slot = strtol(name, &end, 10);
        if ((name[0] != '0' || end == name + 1) &&
                slot >= 0 && slot < CPYMO_S60_PYMO_SAVE_COUNT && !strcmp(end, ".sav"))
            present[slot] = true;
    }
    closedir(dir);
    size_t count = 0;
    for (unsigned int i = 0; i < CPYMO_S60_PYMO_SAVE_COUNT; ++i)
        if (present[i]) slots[count++] = (unsigned char)i;
    return count;
}

static error_t split_save(char *data, size_t size, char *lines[SAVE_LINES])
{
    if (size < 3 || memcmp(data, "\xef\xbb\xbf", 3) || memchr(data, '\0', size))
        return CPYMO_ERR_BAD_FILE_FORMAT;
    char *start = data + 3;
    for (size_t i = 0; i < SAVE_LINES; ++i) {
        if (!start) return CPYMO_ERR_BAD_FILE_FORMAT;
        lines[i] = start;
        char *newline = strchr(start, '\n');
        if (newline) {
            *newline = '\0';
            if (newline > start && newline[-1] == '\r') newline[-1] = '\0';
            start = newline + 1;
        } else start = NULL;
    }
    if (start && *start) return CPYMO_ERR_BAD_FILE_FORMAT;
    return CPYMO_ERR_SUCC;
}

static bool put_string(FILE *out, const char *str)
{
    const size_t length = strlen(str);
    if (length > UINT16_MAX) return false;
    const uint16_t bytes = end_htole16((uint16_t)length);
    return fwrite(&bytes, sizeof(bytes), 1, out) == 1 &&
        fwrite(str, 1, length, out) == length;
}

static bool put_integer(FILE *out, int32_t value)
{
    const uint32_t word = end_htole32((uint32_t)value);
    return fwrite(&word, sizeof(word), 1, out) == 1;
}

static bool fixed16(double fraction, int32_t *value)
{
    if (!(fraction >= -32768.0 && fraction < 32768.0)) return false;
    *value = (int32_t)(fraction * 65536.0);
    return true;
}

static bool write_background(FILE *out, cpymo_engine *e, char *fields)
{
    char *name = next_field(&fields);
    if (!name || !asset_name(name)) return false;
    cpymo_backend_image image = NULL;
    int w, h;
    if (cpymo_assetloader_load_bg_image(&image, &w, &h, cpymo_str_pure(name),
            &e->assetloader) != CPYMO_ERR_SUCC) return false;
    cpymo_backend_image_free(image);
    const float sw = e->gameconfig.imagesize_w, sh = e->gameconfig.imagesize_h;
    double x = 0.0, y = 0.0;
    if (has_field(fields)) {
        char *xs = next_field(&fields), *ys = next_field(&fields);
        char *end;
        if (!xs || !ys || !*xs || !*ys || has_field(fields)) return false;
        x = strtod(xs, &end);
        if (*end) return false;
        y = strtod(ys, &end);
        if (*end) return false;
    }
    int32_t wx, wy;
    if (x == 0.0 && y == 0.0) {
        x = (sw - w) * 0.5 / sw;
        y = (sh - h) * 0.5 / sh;
    } else {
        x = -x * w / (100.0 * sw);
        y = -y * h / (100.0 * sh);
    }
    return fixed16(x, &wx) && fixed16(y, &wy) && put_string(out, name) &&
        put_integer(out, wx) && put_integer(out, wy);
}

static bool write_characters(FILE *out, cpymo_engine *e, char *fields)
{
    const bool has_y = strncmp(fields, "chara_y,", sizeof("chara_y,") - 1) == 0;
    if (has_y) next_field(&fields);
    const int sw = e->gameconfig.imagesize_w, sh = e->gameconfig.imagesize_h;
    while (has_field(fields)) {
        int32_t id, center, top = 0, layer;
        if (!integer_field(&fields, &id)) return false;
        const char *image = next_field(&fields);
        const char *visible = next_field(&fields);
        if (!image || !asset_name(image) || !visible ||
            !integer_field(&fields, &center) || (has_y && !integer_field(&fields, &top)) ||
            !integer_field(&fields, &layer)) return false;
        if (!strcmp(visible, "False")) continue;
        if (strcmp(visible, "True")) return false;
        cpymo_backend_image sprite = NULL;
        int w, h;
        if (cpymo_assetloader_load_chara_image(&sprite, &w, &h, cpymo_str_pure(image),
                &e->assetloader) != CPYMO_ERR_SUCC) return false;
        cpymo_backend_image_free(sprite);
        if (!has_y) top = sh - h;
        int32_t x, y;
        if (!fixed16(((double)center - w / 2) / sw, &x) ||
            !fixed16(top / (double)sh, &y) || !put_string(out, image) ||
            !put_integer(out, id) || !put_integer(out, layer) ||
            !put_integer(out, x) || !put_integer(out, y)) return false;
    }
    return put_string(out, "");
}

static bool write_animation(FILE *out, cpymo_engine *e, char *fields)
{
    if (!*fields) return put_string(out, "");
    const char *tag = next_field(&fields);
    const char *name = next_field(&fields);
    int32_t frames, x, y, interval, wx, wy;
    return name && !strcmp(tag, "anime") && asset_name(name) &&
        integer_field(&fields, &frames) && frames > 0 && frames <= 1024 &&
        integer_field(&fields, &x) && integer_field(&fields, &y) &&
        integer_field(&fields, &interval) && interval > 0 && !has_field(fields) &&
        fixed16(x / (double)e->gameconfig.imagesize_w, &wx) &&
        fixed16(y / (double)e->gameconfig.imagesize_h, &wy) &&
        put_string(out, name) && put_integer(out, frames) && put_integer(out, interval) &&
        put_integer(out, wx) && put_integer(out, wy);
}

static bool write_variables(FILE *out, char *fields)
{
    while (has_field(fields)) {
        const char *name = next_field(&fields);
        int32_t value;
        if (!name || *name == 'S' || !asset_name(name) || !integer_field(&fields, &value) ||
            !put_string(out, name) || !put_integer(out, value)) return false;
    }
    return put_string(out, "");
}

static bool write_script_frame(FILE *out, cpymo_engine *e, char *name, int32_t line, bool checkpoint)
{
    const size_t length = strlen(name);
    if (length > 4 && !strcmp(name + length - 4, ".txt")) name[length - 4] = '\0';
    if (!asset_name(name) || line < (checkpoint ? 1 : 0)) return false;

    cpymo_script *script = NULL;
    if (cpymo_script_load(&script, cpymo_str_pure(name), &e->assetloader) != CPYMO_ERR_SUCC)
        return false;

    cpymo_interpreter interpreter;
    cpymo_interpreter_init(&interpreter, script, true, NULL);
    int32_t target = checkpoint ? line - 1 : line;
    bool valid = cpymo_interpreter_goto_line(&interpreter, target) == CPYMO_ERR_SUCC;
    if (valid && checkpoint) {
        cpymo_parser parser = interpreter.script_parser;
        const cpymo_str command = cpymo_parser_curline_pop_command(&parser);
        /* Legacy PyMO displayed the saved dialogue after jumping past that line.
           CPyMO replays the dialogue from its checkpoint. */
        valid = cpymo_str_equals_str(command, "say");
    }
    if (valid)
        valid = put_string(out, name) && put_integer(out, interpreter.script_parser.cur_pos) &&
            put_integer(out, target) && put_integer(out, 0) && put_integer(out, target);
    cpymo_interpreter_free(&interpreter);
    return valid;
}

static bool write_interpreters(FILE *out, cpymo_engine *e, char *script,
    char *line_str, char *callstack)
{
    int32_t line;
    if (!integer(line_str, &line) || !write_script_frame(out, e, script, line, true))
        return false;
    char *names[MAX_CALLERS];
    int32_t lines[MAX_CALLERS];
    size_t count = 0;
    while (has_field(callstack)) {
        if (count == MAX_CALLERS) return false;
        names[count] = next_field(&callstack);
        if (!names[count] || !integer_field(&callstack, &lines[count])) return false;
        ++count;
    }
    while (count) {
        --count;
        if (!write_script_frame(out, e, names[count], lines[count], false)) return false;
    }
    return put_string(out, "");
}

static error_t convert_save(FILE *out, cpymo_engine *e, char *lines[SAVE_LINES])
{
    char *boxes = lines[12];
    const char *msgbox = next_field(&boxes), *namebox = next_field(&boxes);
    const char *bgm = lines[5];
    if (!msgbox || !namebox || has_field(boxes) || !asset_name(msgbox) || !asset_name(namebox) ||
            (*bgm && !asset_name(bgm))) return CPYMO_ERR_BAD_FILE_FORMAT;
    const unsigned char fade[] = {0, 0, 0, 0};
    const bool ok = put_string(out, lines[4]) && put_string(out, lines[8]) &&
        put_string(out, lines[9]) && put_string(out, namebox) && put_string(out, msgbox) &&
        put_string(out, bgm) && put_string(out, "") && fwrite(fade, sizeof(fade), 1, out) == 1 &&
        write_background(out, e, lines[6]) && write_characters(out, e, lines[7]) &&
        write_animation(out, e, lines[13]) && write_variables(out, lines[10]) &&
        write_interpreters(out, e, lines[0], lines[1], lines[11]);
    if (ferror(out)) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    return ok ? CPYMO_ERR_SUCC : CPYMO_ERR_BAD_FILE_FORMAT;
}

error_t cpymo_s60_pymo_save_import(cpymo_engine *e, unsigned int source_slot,
    unsigned int dest_slot)
{
    if (source_slot >= CPYMO_S60_PYMO_SAVE_COUNT || dest_slot == 0 || dest_slot >= 100 ||
            e->gameconfig.imagesize_w != 320 || e->gameconfig.imagesize_h != 240 ||
            !cpymo_gameconfig_is_symbian(&e->gameconfig))
        return CPYMO_ERR_UNSUPPORTED;

    char *source = save_path(e, 32), *dest = save_path(e, 32), *temp = save_path(e, 32);
    error_t err = CPYMO_ERR_OUT_OF_MEM;
    if (!source || !dest || !temp) goto cleanup;
    sprintf(source + strlen(source), "/%u.sav", source_slot);
    sprintf(dest + strlen(dest), "/save-%02u.csav", dest_slot);
    sprintf(temp + strlen(temp), "/cpymo-import-%02u.part", dest_slot);

    err = CPYMO_ERR_CAN_NOT_OPEN_FILE;
    struct stat status;
    if (stat(dest, &status) == 0 || errno != ENOENT) goto cleanup;
    if (stat(source, &status) != 0 || !S_ISREG(status.st_mode)) goto cleanup;
    FILE *original = fopen(source, "rb");
    if (!original) goto cleanup;
    char *buffer = NULL;
    if (fseek(original, 0, SEEK_END) == 0) {
        const long size = ftell(original);
        if (size >= 3 && size <= MAX_LEGACY_SAVE_BYTES && fseek(original, 0, SEEK_SET) == 0) {
            buffer = malloc((size_t)size + 1);
            if (buffer) {
                if (fread(buffer, 1, (size_t)size, original) == (size_t)size && fgetc(original) == EOF) {
                    buffer[size] = '\0';
                    char *lines[SAVE_LINES];
                    err = split_save(buffer, (size_t)size, lines);
                    if (err == CPYMO_ERR_SUCC) {
                        const int fd = open(temp, O_CREAT | O_EXCL | O_WRONLY, 0600);
                        if (fd < 0) err = CPYMO_ERR_CAN_NOT_OPEN_FILE;
                        else {
                            FILE *out = fdopen(fd, "wb");
                            if (!out) {
                                close(fd);
                                err = CPYMO_ERR_CAN_NOT_OPEN_FILE;
                            } else {
                                err = convert_save(out, e, lines);
                                if (fclose(out) != 0) err = CPYMO_ERR_CAN_NOT_OPEN_FILE;
                            }
                            if (err == CPYMO_ERR_SUCC &&
                                (stat(dest, &status) == 0 || errno != ENOENT || rename(temp, dest) != 0))
                                err = CPYMO_ERR_CAN_NOT_OPEN_FILE;
                            if (err != CPYMO_ERR_SUCC) remove(temp);
                        }
                    }
                }
            } else err = CPYMO_ERR_OUT_OF_MEM;
        } else err = CPYMO_ERR_BAD_FILE_FORMAT;
    }
    free(buffer);
    fclose(original);
cleanup:
    free(source);
    free(dest);
    free(temp);
    return err;
}
#endif
