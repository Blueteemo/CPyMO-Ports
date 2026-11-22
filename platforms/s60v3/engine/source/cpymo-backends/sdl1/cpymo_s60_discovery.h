#ifndef CPYMO_S60_DISCOVERY_H
#define CPYMO_S60_DISCOVERY_H
#include "../../cpymo/cpymo_game_selector.h"
#ifndef CPYMO_S60_DATA_DIR
#define CPYMO_S60_DATA_DIR "C:/Data/CPyMO"
#endif
/* Public for a bounded host test; app scans defaults plus optional roots.txt. */
void cpymo_s60_scan_root(const char *root, cpymo_game_selector_item **head,
    cpymo_game_selector_item **tail, size_t *count);
cpymo_game_selector_item *cpymo_s60_get_game_list(void);
void cpymo_s60_ensure_data_dir(void);
char *cpymo_s60_read_last_game(void);
void cpymo_s60_write_last_game(const char *gamedir);
#endif
