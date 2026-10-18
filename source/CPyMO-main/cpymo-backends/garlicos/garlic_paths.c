#include "garlic_paths.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* small, dependency-free string helpers                                */
/* ------------------------------------------------------------------ */

static size_t gp_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (cap == 0) return strlen(src);
    for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = '\0';
    return i;
}

/* Join "a" and "b" into out, collapsing exactly one separator. `b` may
 * itself contain slashes (a relative asset path). A trailing slash on `a` is
 * dropped, but a lone "/" is preserved. */
static bool gp_join(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    while (la > 1 && a[la - 1] == '/') --la;
    if (b[0] == '/') return false;   /* do not build "//..." silently */
    int n = snprintf(out, cap, "%.*s/%s", (int)la, a, b);
    return n > 0 && (size_t)n < cap;
}

static bool gp_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ------------------------------------------------------------------ */
/* directory of a content path                                        */
/* ------------------------------------------------------------------ */

bool garlic_dir_of_content(const char *content_path, bool is_dir,
                         char *out, size_t out_size)
{
    if (!content_path || !out || out_size == 0) return false;
    const size_t len = strlen(content_path);
    if (len == 0) return false;
    if (len >= out_size) return false;

    memcpy(out, content_path, len + 1);

    if (!is_dir) {
        /* strip the last component */
        char *slash = strrchr(out, '/');
        if (slash == NULL) {          /* "gameconfig.txt" in cwd */
            gp_strlcpy(out, ".", out_size);
        } else if (slash == out) {    /* "/gameconfig.txt" */
            slash[1] = '\0';
        } else {
            *slash = '\0';
        }
    }

    /* Drop a trailing slash, but never turn "/" into "" -- the root really is
     * a directory, and the save layout below appends to it. */
    size_t l = strlen(out);
    while (l > 1 && out[l - 1] == '/') out[--l] = '\0';
    if (out[0] == '\0') gp_strlcpy(out, ".", out_size);
    return true;
}

/* ------------------------------------------------------------------ */
/* stable per-game identifier                                         */
/* ------------------------------------------------------------------ */

static void gp_sanitise(const char *src, char *dst, size_t cap)
{
    size_t i = 0;
    bool any_alnum = false;
    if (cap == 0) return;
    for (; src[i] && i + 1 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9')
               || c == '.' || c == '_' || c == '-';
        dst[i] = ok ? (char)c : '_';
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9'))
            any_alnum = true;
    }
    dst[i] = '\0';

    /* An empty name, and a name made ONLY of punctuation (the filesystem root
     * becomes a lone "_"), both need the same usable fallback -- otherwise the
     * root of the card and a directory literally named "_" would share saves. */
    if (dst[0] == '\0' || !any_alnum) gp_strlcpy(dst, "game", cap);
}

bool garlic_game_id(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) return false;

    char dir[GARLIC_PATH_MAX];
    bool ends_with_gameconfig = false;
    {
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        ends_with_gameconfig = (strcmp(base, "gameconfig.txt") == 0);
    }

    /* A content FILE (normally gameconfig.txt) means the game is its parent
     * directory; anything else is taken as the directory itself. */
    if (!garlic_dir_of_content(path, !ends_with_gameconfig, dir, sizeof(dir)))
        return false;

    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') dir[--len] = '\0';
    const char *base = strrchr(dir, '/');
    base = base ? base + 1 : dir;
    if (*base == '\0') base = dir;      /* "/" */

    /* Keep a leading dot meaningful rather than colliding with the hidden
     * ".." and "." entries after sanitising. */
    char tmp[GARLIC_GAME_ID_MAX];
    if (base[0] == '.') {
        tmp[0] = '.';
        gp_sanitise(base + 1, tmp + 1, sizeof(tmp) - 1);
    } else {
        gp_sanitise(base, tmp, sizeof(tmp));
    }

    if (strcmp(tmp, ".") == 0 || strcmp(tmp, "..") == 0)
        gp_strlcpy(tmp, "game", sizeof(tmp));

    return gp_strlcpy(out, tmp, out_size) < out_size;
}

/* ------------------------------------------------------------------ */
/* save paths                                                        */
/* ------------------------------------------------------------------ */

bool garlic_make_save_dir(const char *gamedir, const char *save_root)
{
    if (gamedir == NULL) return false;

    char path[GARLIC_PATH_MAX];
    if (save_root && save_root[0]) {
        char id[GARLIC_GAME_ID_MAX];
        if (!garlic_game_id(gamedir, id, sizeof(id))) return false;
        if (!gp_join(path, sizeof(path), save_root, id)) return false;
        /* Two levels: the games root, then this game's own directory. */
        (void)mkdir(save_root, 0755);
    } else {
        if (!gp_join(path, sizeof(path), gamedir, "save")) return false;
    }

    if (mkdir(path, 0755) == 0) return true;
    return gp_is_dir(path);
}

bool garlic_save_path(char *out, size_t out_size,
                     const char *gamedir, const char *save_root,
                     const char *name)
{
    if (!out || out_size == 0 || !gamedir || !name) return false;
    if (strchr(name, '/') != NULL) return false;   /* flat slot files only */

    char dir[GARLIC_PATH_MAX];
    if (save_root && save_root[0]) {
        char id[GARLIC_GAME_ID_MAX];
        if (!garlic_game_id(gamedir, id, sizeof(id))) return false;
        if (!gp_join(dir, sizeof(dir), save_root, id)) return false;
    } else {
        if (!gp_join(dir, sizeof(dir), gamedir, "save")) return false;
    }
    if (!gp_join(out, out_size, dir, name)) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* case diagnostics                                                  */
/* ------------------------------------------------------------------ */

/* Case-insensitive equality, ASCII, without locale dependence. */
static bool gp_ieq_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
        if (a[i] == '\0') return b[i] == '\0';
    }
    return true;
}

/* "ICON.png" matches stem "icon" + ext "png" case-insensitively. */
static bool gp_matches_asset(const char *entry, const char *stem, size_t stem_len,
                            const char *ext)
{
    size_t el = strlen(entry), xl = strlen(ext);
    if (el == 0 || el != stem_len + 1 + xl) return false;
    if (entry[stem_len] != '.') return false;
    if (!gp_ieq_n(entry, stem, stem_len)) return false;
    return gp_ieq_n(entry + stem_len + 1, ext, xl);
}

int garlic_diagnose_case(const char *dir,
                       const char *asset_type,
                       const char *asset_name, size_t asset_name_len,
                       const char *ext,
                       char *found, size_t found_size)
{
    if (!dir || !asset_type || !asset_name || !ext || !found || found_size == 0)
        return -1;
    found[0] = '\0';

    /* Work in a bounded copy of the name so the compare is NUL-terminated. */
    char stem[256];
    if (asset_name_len >= sizeof(stem)) return 0;
    memcpy(stem, asset_name, asset_name_len);
    stem[asset_name_len] = '\0';

    char sub[GARLIC_PATH_MAX];
    /* "." or "" means "this directory itself" (used for gameconfig.txt). */
    const bool here = (asset_type[0] == '\0' ||
                       (asset_type[0] == '.' && asset_type[1] == '\0'));
    if (here) {
        gp_strlcpy(sub, dir, sizeof(sub));
    } else if (!gp_join(sub, sizeof(sub), dir, asset_type)) {
        return -1;
    }

    DIR *d = opendir(sub);
    if (d == NULL) return 0;

    struct dirent *de;
    bool hit = false;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' && stem[0] != '.') continue;
        if (!gp_matches_asset(de->d_name, stem, asset_name_len, ext)) continue;

        /* Report the on-disk spelling so the caller can print it verbatim;
         * for the current directory that is the plain entry name. */
        if (here) {
            gp_strlcpy(found, de->d_name, found_size);
        } else {
            size_t need = strlen(asset_type) + 1 + strlen(de->d_name) + 1;
            if (need > found_size) { hit = true; break; }
            snprintf(found, found_size, "%s/%s", asset_type, de->d_name);
        }
        hit = true;
        break;
    }
    closedir(d);
    return hit ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* game directory scan                                                */
/* ------------------------------------------------------------------ */

static int gp_qsort_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

bool garlic_scan_game_dirs(const char *root, char *out[], size_t max_dirs,
                          size_t *out_count)
{
    if (!root || !out || !out_count) return false;
    *out_count = 0;

    DIR *d = opendir(root);
    if (d == NULL) return false;

    char *names[256];
    size_t n = 0, stored = 0, cap = sizeof(names) / sizeof(names[0]);

    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < cap) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        char dir[GARLIC_PATH_MAX], cfg[GARLIC_PATH_MAX];
        if (!gp_join(dir, sizeof(dir), root, de->d_name)) continue;
        if (!gp_is_dir(dir)) continue;
        if (!gp_join(cfg, sizeof(cfg), dir, "gameconfig.txt")) continue;
        struct stat st;
        if (stat(cfg, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        char *copy = strdup(dir);
        if (copy == NULL) {   /* shrink what we already hold, then fail */
            for (size_t i = 0; i < n; ++i) free(names[i]);
            closedir(d);
            return false;
        }
        names[n++] = copy;
    }
    closedir(d);

    qsort(names, n, sizeof(names[0]), gp_qsort_names);

    for (; stored < n && stored < max_dirs; ++stored) out[stored] = names[stored];
    for (size_t i = stored; i < n; ++i) free(names[i]);
    *out_count = stored;
    return true;
}

/* ------------------------------------------------------------------ */
/* the platform decisions the selector needs                            */
/* ------------------------------------------------------------------ */

size_t garlic_font_candidates(const char *gamedir, const char *systemdir,
                             const char *menu_font,
                             char *out, size_t out_size, size_t max_candidates)
{
    if (!out || out_size == 0 || max_candidates == 0) return 0;
    size_t n = 0;

    /* The explicit font is used VERBATIM: the user (or the packaging step)
     * named a file, so nothing is appended to it. `gp_strlcpy` returns the
     * source length, so a name that would not fit is skipped, never cut. */
    if (menu_font && menu_font[0] && n < max_candidates) {
        if (gp_strlcpy(out, menu_font, out_size) < out_size) ++n;
    }

    /* "<dir>/<sub>", appended only when the result fits. Duplicate entries
     * are kept on purpose: the log is meant to show the order really tried. */
    #define GP_ADD(DIR, SUB)                                                \
        do {                                                                \
            if (n < max_candidates) {                                       \
                char *slot = out + n * out_size;                            \
                int w = snprintf(slot, out_size, "%s/%s", (DIR), (SUB));   \
                if (w > 0 && (size_t)w < out_size) ++n;                     \
            }                                                               \
        } while (0)

    if (gamedir && gamedir[0]) {
        GP_ADD(gamedir, "system/default.ttf");
        GP_ADD(gamedir, "system/default.otf");
    }
    if (systemdir && systemdir[0]) {
        GP_ADD(systemdir, "cpymo/default.ttf");
        GP_ADD(systemdir, "cpymo/default.otf");
    }
    #undef GP_ADD
    return n;
}

bool garlic_games_dir(const char *env_value, const char *content_dir,
                     const char *fallback_dir, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;

    /* Ordered, and each candidate verified, so a stale CPYMO_GAMES_DIR (a
     * card that moved, a directory that was renamed) cannot win over a real
     * one. */
    const char *order[3];
    size_t n = 0;
    if (env_value && env_value[0]) order[n++] = env_value;
    if (content_dir && content_dir[0]) order[n++] = content_dir;
    if (fallback_dir && fallback_dir[0]) order[n++] = fallback_dir;

    for (size_t i = 0; i < n; ++i) {
        if (!gp_is_dir(order[i])) continue;
        if (gp_strlcpy(out, order[i], out_size) >= out_size) continue;
        return true;
    }
    return false;
}
