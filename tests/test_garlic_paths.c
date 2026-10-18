/* Host unit test for the shared path / save-isolation / case-diagnostic helpers.
 *
 * Plain C, no framework: it builds the module on the host and checks the
 * behaviours the v0.23 task depends on. The temporary fixtures are created
 * under a private directory and removed at the end.
 */
#include "../source/CPyMO-main/cpymo-backends/garlicos/garlic_paths.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(COND, ...) do { \
    if (!(COND)) { ++failures; printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("  ok    " __VA_ARGS__); printf("\n"); } \
} while (0)

/* A long-enough buffer for "<fixture>/<relative>", so no snprintf in here can
 * be flagged for possible truncation. */
#define PATHBUF 1200

static char root[256];

static void rooted(char *out, size_t cap, const char *rel)
{
    snprintf(out, cap, "%s/%s", root, rel);
}

/* Append a suffix in place; no format string, so no truncation diagnostics. */
static void appended(char *dst, size_t cap, const char *suffix)
{
    strncat(dst, suffix, cap - strlen(dst) - 1);
}

/* Create `rel` under `root`, plus every missing parent below `root`: one
 * implementation, used both for the fixture root and for nested paths. */
static void mkdirs(const char *rel)
{
    char p[PATHBUF];
    rooted(p, sizeof(p), rel);
    char *from = (*rel == '\0') ? (p + strlen(root)) : (p + strlen(root) + 1);
    for (char *s = from; ; ++s) {
        if (*s == '/' || *s == '\0') {
            char saved = *s;
            *s = '\0';
            /* EEXIST is success; a real failure shows up in the asserts. */
            (void)mkdir(p, 0755);
            *s = saved;
            if (saved == '\0') break;
        }
    }
}

static void touch(const char *rel, const char *content)
{
    char p[PATHBUF];
    rooted(p, sizeof(p), rel);
    FILE *f = fopen(p, "wb");
    if (f) { if (content) fputs(content, f); fclose(f); }
}

static void rm_rf(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char child[PATHBUF];
            snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
            rm_rf(child);
        }
        closedir(d);
        rmdir(path);
    } else {
        remove(path);
    }
}

int main(void)
{
    snprintf(root, sizeof(root), "/tmp/cpymo_paths_%ld", (long)getpid());
    rm_rf(root);
    /* The empty relative path means "root itself"; mkdirs handles that case. */
    mkdirs("");
    if (!(opendir(root))) {
        printf("FAIL: could not create the fixture root %s\n", root);
        return 1;
    }

    /* ---- fixture ---------------------------------------------------- */
    mkdirs("games/Yume");
    mkdirs("games/Yume/system");
    mkdirs("games/Yume/script");
    mkdirs("games/Narcissus");
    mkdirs("games/empty_dir");
    /* A pre-existing game directory, which is all garlic_make_save_dir()
     * promises to work with (it creates only the "save" level below it). */
    mkdirs("write");
    touch("games/Yume/gameconfig.txt", "gametitle,Yume\n");
    touch("games/Yume/system/Default.TTF", "font");
    touch("games/Narcissus/gameconfig.txt", "gametitle,Narcissus\n");
    touch("games/Narcissus/system/default.ttf", "font");

    printf("== content path -> directory\n");
    {
        char out[GARLIC_PATH_MAX];
        CHECK(garlic_dir_of_content("/a/b/Yume/gameconfig.txt", false, out, sizeof(out))
              && strcmp(out, "/a/b/Yume") == 0, "file path drops the file: %s", out);
        CHECK(garlic_dir_of_content("/a/b/Yume/", true, out, sizeof(out))
              && strcmp(out, "/a/b/Yume") == 0, "dir with a trailing slash: %s", out);
        CHECK(garlic_dir_of_content("/", true, out, sizeof(out))
              && strcmp(out, "/") == 0, "root stays root: %s", out);
        CHECK(garlic_dir_of_content("gameconfig.txt", false, out, sizeof(out))
              && strcmp(out, ".") == 0, "bare filename -> cwd: %s", out);
        /* A directory whose NAME contains a dot must not be mangled; the
         * launcher answers that with stat(), carried here by is_dir. */
        CHECK(garlic_dir_of_content("/a/v1.2", true, out, sizeof(out))
              && strcmp(out, "/a/v1.2") == 0, "dotted directory name survives: %s", out);
        CHECK(!garlic_dir_of_content(NULL, true, out, sizeof(out)), "NULL rejected");
        char tiny[4];
        CHECK(!garlic_dir_of_content("/a/b/Yume", true, tiny, sizeof(tiny)),
              "too long for the buffer -> false, not truncated");
    }

    printf("== game identity (save isolation)\n");
    {
        char a[GARLIC_GAME_ID_MAX], b[GARLIC_GAME_ID_MAX];
        garlic_game_id("/mnt/mmc/Roms/CPyMO/Yume", a, sizeof(a));
        garlic_game_id("/mnt/mmc/Roms/CPyMO/Yume/gameconfig.txt", b, sizeof(b));
        CHECK(strcmp(a, b) == 0, "dir and its gameconfig.txt agree (%s)", a);
        CHECK(strcmp(a, "Yume") == 0, "identifier is the basename (%s)", a);

        garlic_game_id("/mnt/mmc/Roms/CPyMO/yume", a, sizeof(a));
        CHECK(strcmp(a, "yume") == 0, "case is PRESERVED, not folded (%s)", a);

        garlic_game_id("/mnt/mmc/Roms/My Game v1.2!", a, sizeof(a));
        CHECK(strcmp(a, "My_Game_v1.2_") == 0, "odd characters sanitised (%s)", a);

        garlic_game_id("/", a, sizeof(a));
        CHECK(strcmp(a, "game") == 0, "root falls back to a usable id (%s)", a);
    }

    printf("== save paths\n");
    {
        char out[GARLIC_PATH_MAX];
        CHECK(garlic_save_path(out, sizeof(out), "/g/Yume", NULL, "save-00.csav")
              && strcmp(out, "/g/Yume/save/save-00.csav") == 0,
              "no save root -> the historical in-game layout (%s)", out);
        CHECK(garlic_save_path(out, sizeof(out), "/g/Yume", "/data", "global.csav")
              && strcmp(out, "/data/Yume/global.csav") == 0,
              "save root -> isolated by game id (%s)", out);
        CHECK(garlic_save_path(out, sizeof(out), "/g/yume", "/data", "global.csav")
              && strcmp(out, "/data/yume/global.csav") == 0,
              "a case-only sibling gets its OWN directory (%s)", out);
        CHECK(garlic_save_path(out, sizeof(out), "/g/Yume", "/data", "sub/slot.csav")
              == false, "a name with a slash is rejected");
        {
            char p1[GARLIC_PATH_MAX], p2[GARLIC_PATH_MAX];
            garlic_save_path(p1, sizeof(p1), "/g/Yume", "/data", "save-01.csav");
            garlic_save_path(p2, sizeof(p2), "/g/Narcissus", "/data", "save-01.csav");
            CHECK(strcmp(p1, p2) != 0, "different games, same slot -> different files");
        }
    }

    printf("== save directory creation\n");
    {
        char fresh[PATHBUF], p[PATHBUF], sroot[PATHBUF];
        struct stat st;

        rooted(fresh, sizeof(fresh), "write");
        CHECK(garlic_make_save_dir(fresh, NULL), "creates <gamedir>/save");
        rooted(p, sizeof(p), "write/save");
        CHECK(stat(p, &st) == 0 && S_ISDIR(st.st_mode), "the directory exists (%s)", p);

        rooted(sroot, sizeof(sroot), "write/saves");
        CHECK(garlic_make_save_dir("/g/Yume", sroot),
              "creates <save_root>/<game_id>");
        /* Build the expectation with a plain append: no format string, so the
         * compiler cannot warn about truncation in the assertion itself. */
        {
            char expect[PATHBUF];
            snprintf(expect, sizeof(expect), "%s", sroot);
            appended(expect, sizeof(expect), "/Yume");
            CHECK(stat(expect, &st) == 0 && S_ISDIR(st.st_mode),
                  "isolated directory exists (%s)", expect);
        }
    }

    printf("== case diagnostics\n");
    {
        char found[512], gdir[PATHBUF];
        rooted(gdir, sizeof(gdir), "games/Yume");

        /* The fixture font is "Default.TTF"; the engine asks for plain ttf. */
        CHECK(garlic_diagnose_case(gdir, "system", "default", 7, "ttf",
                                 found, sizeof(found)) == 1,
              "case-different font is reported: %s", found);
        CHECK(strcmp(found, "system/Default.TTF") == 0,
              "the ON-DISK spelling is reported (%s)", found);
        CHECK(garlic_diagnose_case(gdir, "system", "absent", 6, "png",
                                 found, sizeof(found)) == 0,
              "a genuinely absent asset reports 0");
        CHECK(found[0] == '\0', "and leaves `found` emptied");
        CHECK(garlic_diagnose_case(gdir, "nosuchtype", "default", 7, "ttf",
                                 found, sizeof(found)) == 0,
              "a missing subdirectory is not an error");
        CHECK(garlic_diagnose_case(NULL, "system", "x", 1, "ttf",
                                 found, sizeof(found)) == -1,
              "bad arguments -> -1");
        CHECK(garlic_diagnose_case(gdir, "system", "default", 7, "png",
                                 found, sizeof(found)) == 0,
              "same stem, other extension -> no match");
        /* "." scans the directory itself: the gameconfig.txt diagnostic. */
        CHECK(garlic_diagnose_case(gdir, ".", "gameconfig", 10, "txt",
                                 found, sizeof(found)) == 1
              && strcmp(found, "gameconfig.txt") == 0,
              "the current directory reports the bare name (%s)", found);
        CHECK(garlic_diagnose_case(gdir, ".", "GameConfig", 10, "TXT",
                                 found, sizeof(found)) == 1,
              "and matches it case-insensitively too (%s)", found);
    }

    printf("== game directory scan\n");
    {
        char sroot[PATHBUF];
        char *dirs[8];
        size_t n = 0;
        rooted(sroot, sizeof(sroot), "games");

        CHECK(garlic_scan_game_dirs(sroot, dirs, 8, &n), "scan completes");
        CHECK(n == 2, "finds exactly the two games, not empty_dir (%zu)", n);
        if (n == 2) {
            CHECK(strstr(dirs[0], "Narcissus") && strstr(dirs[1], "Yume"),
                  "sorted for a stable selector order (%s, %s)", dirs[0], dirs[1]);
            for (size_t i = 0; i < n; ++i) free(dirs[i]);
        }
        n = 0;
        CHECK(!garlic_scan_game_dirs("/nonexistent/dir", dirs, 8, &n)
              && n == 0, "a missing root fails cleanly");
        CHECK(garlic_scan_game_dirs(sroot, dirs, 1, &n) && n == 1,
              "the caller's cap is honoured (%zu)", n);
        for (size_t i = 0; i < n; ++i) free(dirs[i]);
    }

    printf("== font candidates (the order the log will print)\n");
    {
        char cand[8][GARLIC_PATH_MAX];
        size_t n;

        /* A game directory and a frontend directory, in the documented
         * order: the game's own font first, the frontend's second. */
        n = garlic_font_candidates("/games/Yume", "/sysdir", NULL,
                                  (char *)cand, GARLIC_PATH_MAX, 8);
        CHECK(n == 4, "four candidates for a game + a frontend (%zu)", n);
        if (n == 4) {
            CHECK(strcmp(cand[0], "/games/Yume/system/default.ttf") == 0,
                  "the game's own font is tried first (%s)", cand[0]);
            CHECK(strcmp(cand[1], "/games/Yume/system/default.otf") == 0,
                  "then the game's .otf (%s)", cand[1]);
            CHECK(strcmp(cand[2], "/sysdir/cpymo/default.ttf") == 0,
                  "then the frontend's font (%s)", cand[2]);
            CHECK(strcmp(cand[3], "/sysdir/cpymo/default.otf") == 0,
                  "then the frontend's .otf (%s)", cand[3]);
        }

        /* The MENU runs before any game exists: no game directory at all. */
        n = garlic_font_candidates(NULL, "/sysdir", "/explicit/menu.ttf",
                                  (char *)cand, GARLIC_PATH_MAX, 8);
        CHECK(n == 3, "the menu skips the game candidates (%zu)", n);
        if (n == 3) {
            CHECK(strcmp(cand[0], "/explicit/menu.ttf") == 0,
                  "an explicit menu font is used VERBATIM, not joined (%s)",
                  cand[0]);
            CHECK(strcmp(cand[1], "/sysdir/cpymo/default.ttf") == 0,
                  "and the frontend's font still follows (%s)", cand[1]);
        }

        /* No frontend directory: only the game's own. */
        n = garlic_font_candidates("/games/Yume", NULL, NULL,
                                  (char *)cand, GARLIC_PATH_MAX, 8);
        CHECK(n == 2, "an empty systemdir contributes nothing (%zu)", n);

        /* Nothing usable at all is 0, not a template path with "NULL" in it. */
        n = garlic_font_candidates(NULL, NULL, NULL,
                                  (char *)cand, GARLIC_PATH_MAX, 8);
        CHECK(n == 0, "no inputs -> no candidates (%zu)", n);

        /* The cap is honoured, so a caller with room for two gets two. */
        n = garlic_font_candidates("/games/Yume", "/sysdir", NULL,
                                  (char *)cand, GARLIC_PATH_MAX, 2);
        CHECK(n == 2, "the caller's cap is honoured (%zu)", n);
        if (n == 2) {
            for (size_t i = 0; i < n; ++i)
                CHECK(strncmp(cand[i], "/games/Yume/", 12) == 0,
                      "and it keeps the FIRST candidates (%s)", cand[i]);
        }
    }

    printf("== games directory (env -> content -> convention)\n");
    {
        char out[PATHBUF], games[PATHBUF], elsewhere[PATHBUF];

        rooted(games, sizeof(games), "games");
        rooted(elsewhere, sizeof(elsewhere), "not_a_dir");

        CHECK(garlic_games_dir(games, NULL, NULL, out, sizeof(out))
              && strcmp(out, games) == 0,
              "the environment value is honoured (%s)", out);
        CHECK(garlic_games_dir(NULL, games, NULL, out, sizeof(out))
              && strcmp(out, games) == 0,
              "so is a directory derived from the content (%s)", out);
        CHECK(garlic_games_dir(NULL, NULL, games, out, sizeof(out))
              && strcmp(out, games) == 0,
              "and the compile-time convention as the last resort (%s)", out);

        /* A STALE environment value must not win over a real directory: the
         * card moved, the folder was renamed. */
        CHECK(garlic_games_dir(elsewhere, games, NULL, out, sizeof(out))
              && strcmp(out, games) == 0,
              "a nonexistent env value falls through to the content dir (%s)", out);

        CHECK(!garlic_games_dir(elsewhere, elsewhere, elsewhere,
                               out, sizeof(out)),
              "three nonexistent candidates -> false, not a guess");
        CHECK(!garlic_games_dir("", "", "", out, sizeof(out)),
              "empty strings are skipped, not used as a path");
    }

    rm_rf(root);

    printf("\n%s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
