#ifndef CPYMO_GARLIC_PATHS_H
#define CPYMO_GARLIC_PATHS_H

/* Path, save-isolation and case-diagnostic helpers for the RG35XX Libretro core.
 *
 * Everything here is plain POSIX C with no platform headers, so it both runs on
 * the RG35XX (uClibc) and is unit-testable on a host. The two behaviours the
 * v0.23 task asks for live here:
 *
 *  - "通用游戏路径加载": the core is handed a *game directory* (or a file
 *    inside it) and must work for any game, not only the shipped test game.
 *  - "存档隔离": saves are addressed by a stable per-game identifier, so two
 *    games can never read each other's slots, and an explicit save root can be
 *    used when the game directory itself is read-only.
 *
 * Plus a diagnostic helper for the case-sensitivity requirement: when an asset
 * is missing, say whether the *same name in different case* exists, instead of
 * leaving the user with a silent blank background.
 */

#include <stddef.h>
#include <stdbool.h>

/* Longest path this module will build (as accounted memory). */
#define GARLIC_PATH_MAX 1024

/* Longest accepted save identifier, including the terminating NUL. */
#define GARLIC_GAME_ID_MAX 96

/* `content_path` is whatever the launcher handed to retro_load_game(): either the
 * game directory ("/x/Yume", "/x/Yume/") or a file inside it (normally
 * "/x/Yume/gameconfig.txt"). `is_dir` states which, as decided by the
 * caller's own stat(), because a directory name may itself contain a dot.
 * Writes the normalised directory (no trailing slash, except for "/") into
 * `out` (size `out_size`). Returns false for a NULL/empty input or a result
 * that would not fit -- never a silently truncated path. */
bool garlic_dir_of_content(const char *content_path, bool is_dir,
                         char *out, size_t out_size);

/* A stable, filesystem-safe identifier for one game:
 *   - the directory's basename ("/x/y/Yume" -> "Yume"), and
 *   - for a file path, the basename's parent ("/x/Yume/gameconfig.txt").
 * Characters outside [A-Za-z0-9._-] become '_'; a leading '.' is kept out of
 * the way of hidden entries; an empty or all-invalid name becomes "game".
 * Case is PRESERVED, because two ROM directories can legitimately differ only in
 * case and must still get separate saves. Returns false on bad arguments. */
bool garlic_game_id(const char *path, char *out, size_t out_size);

/* Build a save-file path.
 *
 * With `save_root` NULL or empty, the layout is exactly what earlier versions
 * wrote -- "<gamedir>/save/<name>" -- so existing device saves keep working.
 * With a save_root, it becomes "<save_root>/<game_id>/<name>" so that all
 * games can share one writable directory while staying isolated from each other.
 *
 * Returns false (and does not use `out`) when the path would not fit, so a
 * caller can report a clear error rather than a truncated filename. */
bool garlic_save_path(char *out, size_t out_size,
                     const char *gamedir, const char *save_root,
                     const char *name);

/* Create the directory part of a save path, including parents one level down.
 * Only the final directory (and, when a save_root is used, the id below it)
 * is created; nothing above is touched. Returns true if it exists afterwards. */
bool garlic_make_save_dir(const char *gamedir, const char *save_root);

/* Case-sensitivity diagnostic: if "<dir>/<asset_type>/<asset_name>.<ext>" is
 * missing but the same name in another case exists, report the real spelling.
 *
 * `found` receives a path RELATIVE to nothing (i.e. "<asset_type>/<name>.<ext>"
 * with the on-disk spelling) when a case variant is found; it stays empty when
 * nothing matches (a genuinely absent asset). Returns:
 *    1  a case-different match exists  -> *found is the on-disk relative path
 *    0  nothing matches               -> a plain "not found"
 *   -1  bad arguments
 *
 * This is called only on a load failure, so its directory scan is not on any
 * hot path. */
int garlic_diagnose_case(const char *dir,
                        const char *asset_type,
                        const char *asset_name, size_t asset_name_len,
                        const char *ext,
                        char *found, size_t found_size);

/* Scan `root` for immediate subdirectories that contain a `gameconfig.txt`.
 * `out` receives at most `max_dirs` strdup'd absolute-ish paths (they are
 * "<root>/<name>") and `out_count` the number stored. Entries are visited in
 * the order readdir returns them, then sorted by name here so that the
 * selector order is stable across filesystem implementations.
 *
 * Returns ERROR CODES as bool plus `out_count`:
 *   true  -> scan completed (out_count may be 0)
 *   false -> `root` could not be opened, or an allocation failed.
 * The caller owns each string and frees them. */
bool garlic_scan_game_dirs(const char *root, char *out[], size_t max_dirs,
                         size_t *out_count);

/* ------------------------------------------------------------------ */
/* the two decisions the game selector needs from the platform layer     */
/* ------------------------------------------------------------------ */

/* Font candidates, most specific first, written into `out` (each of size
 * `out_size`). The ORDER is part of the contract, because the log prints
 * exactly what was tried:
 *
 *     1..2  the game's own   <gamedir>/system/default.{ttf,otf}
 *     3..4  the frontend's    <systemdir>/cpymo/default.{ttf,otf}
 *
 * For the game selector there is no game yet, so the caller passes the GAMES
 * directory as `gamedir`; the two candidates then read
 * `<games_dir>/system/default.{ttf,otf}`, which is where a card-wide font
 * belongs. `gamedir` is skipped when NULL/empty, and `systemdir` likewise
 * when the frontend has none. An explicit menu font (`menu_font`, from the
 * environment) is tried FIRST and used VERBATIM, because a user who names a
 * font means that file. Returns the number written (<= `max_candidates`). */
size_t garlic_font_candidates(const char *gamedir, const char *systemdir,
                             const char *menu_font,
                             char *out, size_t out_size, size_t max_candidates);

/* The directory that holds the games.
 *
 * The launcher decides; the core must not invent a path out of thin air. The
 * inputs, in the order they are honoured:
 *
 *   1. `env_value`    -- CPYMO_GAMES_DIR, the launcher's explicit answer;
 *   2. `content_dir`  -- a directory derived from what the frontend handed to
 *                        retro_load_game; the caller passes what makes sense
 *                        for its own launch style;
 *   3. `fallback_dir` -- the compile-time convention (GARLIC_GAMES_DIR),
 *                        used only if it really exists.
 *
 * Each step is verified with a real stat(), so a stale environment value
 * cannot shadow a usable directory, and nothing is guessed. Returns false when
 * none of them is a directory, so the caller can keep the plain
 * single-game path. */
bool garlic_games_dir(const char *env_value, const char *content_dir,
                     const char *fallback_dir, char *out, size_t out_size);

#endif
