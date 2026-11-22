#include <stdlib.h>

int cpymo_s60_diagnostics_enabled(void) { return getenv("CPYMO_TEST_DIAGNOSTICS") != NULL; }
void cpymo_s60_exit_stage(int stage) {(void)stage;}
int cpymo_s60_native_confirm_exit(int in_game) {(void)in_game; return -1;}
