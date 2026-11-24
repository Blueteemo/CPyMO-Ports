#include <stdlib.h>

int cpymo_s60_diagnostics_enabled(void) { return getenv("CPYMO_TEST_DIAGNOSTICS") != NULL; }
void cpymo_s60_exit_stage(int stage) {(void)stage;}
int cpymo_s60_native_confirm_exit(int in_game) {(void)in_game; return -1;}
int cpymo_s60_native_confirm_legacy_mo(void) {return -1;}
int cpymo_s60_movie_start(const char *path) {(void)path; return -1;}
int cpymo_s60_movie_result(int *status) {(void)status; return 0;}
void cpymo_s60_movie_close(void) {}
struct cpymo_engine;
void cpymo_s60_movie_resume_audio(struct cpymo_engine *engine) {(void)engine;}
