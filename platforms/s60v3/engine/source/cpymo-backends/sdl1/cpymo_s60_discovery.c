/* CPyMO S60v3: shared game discovery. No writes to game resources/config. */
#include "cpymo_s60_discovery.h"
#include "cpymo_s60_diagnostics.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_GAMES 256
#define MAX_PATH_BYTES 1024

static int pathcmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)(*a == '\\' ? '/' : *a));
        int cb = tolower((unsigned char)(*b == '\\' ? '/' : *b));
        if (ca != cb) return ca - cb;
        ++a; ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void cpymo_s60_ensure_data_dir(void)
{
    mkdir("C:/Data", 0777);
    mkdir(CPYMO_S60_DATA_DIR, 0777);
}

void cpymo_s60_scan_root(const char *root, cpymo_game_selector_item **head,
    cpymo_game_selector_item **tail, size_t *count)
{
    DIR *d;
    struct dirent *ent;
    if (*count >= MAX_GAMES || strlen(root) >= MAX_PATH_BYTES - 2) return;
    d = opendir(root);
    if (!d) return;
    while ((ent = readdir(d)) != NULL && *count < MAX_GAMES) {
        char path[MAX_PATH_BYTES], config[MAX_PATH_BYTES];
        struct stat st;
        cpymo_game_selector_item *cur, *prev;
        char *owned;
        FILE *f;
        size_t nroot = strlen(root), nname = strlen(ent->d_name);
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (nroot + nname + 20 >= sizeof(path)) continue;
        sprintf(path, "%s/%s", root, ent->d_name);
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        sprintf(config, "%s/gameconfig.txt", path);
        f = fopen(config, "rb");
        if (!f) continue;
        fclose(f);
        for (cur = *head; cur && pathcmp(cur->gamedir, path); cur = cur->next) {}
        if (cur) continue;
        owned = (char *)malloc(strlen(path) + 1);
        if (!owned) break;
        strcpy(owned, path);
        cur = NULL;
        if (cpymo_game_selector_item_create(&cur, &owned) != CPYMO_ERR_SUCC) {
            free(owned); break;
        }
        /* Stable order regardless of filesystem enumeration order. */
        prev = NULL;
        {
            cpymo_game_selector_item *at = *head;
            while (at && pathcmp(at->gamedir, cur->gamedir) < 0) {
                prev = at; at = at->next;
            }
            cur->prev = prev; cur->next = at;
            if (prev) prev->next = cur; else *head = cur;
            if (at) at->prev = cur; else *tail = cur;
        }
        ++*count;
        CPYMO_S60_TRACE("[S60] game: %s\n", path);
    }
    closedir(d);
}

cpymo_game_selector_item *cpymo_s60_get_game_list(void)
{
    cpymo_game_selector_item *head = NULL, *tail = NULL;
    size_t count = 0;
    char drive;
    FILE *f;
    cpymo_s60_scan_root("C:/data/python", &head, &tail, &count);
    for (drive = 'C'; drive <= 'Z'; ++drive) {
        char root[] = "C:/Python";
        root[0] = drive;
        cpymo_s60_scan_root(root, &head, &tail, &count);
    }
    f = fopen(CPYMO_S60_DATA_DIR "/roots.txt", "rb");
    if (f) {
        char root[MAX_PATH_BYTES];
        while (fgets(root, sizeof(root), f)) {
            size_t n = strlen(root), i;
            /* A too-long line is ignored as one unit, not treated as another root. */
            if (n == sizeof(root) - 1 && root[n - 1] != '\n') {
                int c; while ((c = fgetc(f)) != '\n' && c != EOF) {}
                continue;
            }
            while (n && (root[n - 1] == '\n' || root[n - 1] == '\r' || root[n - 1] == ' ' || root[n - 1] == '\t')) root[--n] = 0;
            if (n >= 3 && isalpha((unsigned char)root[0]) && root[1] == ':' && (root[2] == '/' || root[2] == '\\')) {
                for (i = 0; i < n; ++i) if (root[i] == '\\') root[i] = '/';
                while (n > 3 && root[n - 1] == '/') root[--n] = 0;
                cpymo_s60_scan_root(root, &head, &tail, &count);
            }
        }
        fclose(f);
    }
    printf("[S60] discovered %lu games (direct children only)\n", (unsigned long)count);
    return head;
}

char *cpymo_s60_read_last_game(void)
{
    char buf[MAX_PATH_BYTES];
    char *out;
    size_t n;
    FILE *f = fopen(CPYMO_S60_DATA_DIR "/last.txt", "rb");
    if (!f) return NULL;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (!n || n == sizeof(buf)) return NULL;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) --n;
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, buf, n); out[n] = 0;
    return out;
}

void cpymo_s60_write_last_game(const char *gamedir)
{
    FILE *f;
    if (!gamedir || strlen(gamedir) >= MAX_PATH_BYTES) return;
    cpymo_s60_ensure_data_dir();
    f = fopen(CPYMO_S60_DATA_DIR "/last.txt", "wb");
    if (!f) return;
    fwrite(gamedir, 1, strlen(gamedir), f);
    fclose(f);
}
