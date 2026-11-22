#ifndef CPYMO_S60_APP_H
#define CPYMO_S60_APP_H
#ifdef S60V3
#include <stdbool.h>
#include "../../cpymo/cpymo_error.h"
struct cpymo_engine;
/* Deferred until the current UI callback returns; never free a live UI stack. */
void cpymo_s60_request_game_list(void);
void cpymo_s60_request_font_restart(void);
/* Takes ownership of gamedir; main performs loading outside the list callback. */
void cpymo_s60_request_game_start(char *gamedir);
void cpymo_s60_request_exit(void);
/* Read by the native SDL event pump before it waits for foreground. */
int cpymo_s60_exit_is_requested(void);
void cpymo_s60_request_exit_confirmation(void);
/* Returns 1 when the game list accepted a red End key and queued normal exit. */
int cpymo_s60_handle_selector_end_key(void);
int cpymo_s60_native_confirm_exit(int in_game);
/* Preserve a tap that starts and ends before the next engine update. */
void cpymo_s60_latch_menu_key(void);
void cpymo_s60_latch_cancel_key(void);
/* Native RFs trace at the normal-exit boundary, independent of stdio. */
void cpymo_s60_exit_stage(int stage);
bool cpymo_s60_about_is_open(void);
error_t cpymo_s60_about_open(struct cpymo_engine *engine);
#endif
#endif
