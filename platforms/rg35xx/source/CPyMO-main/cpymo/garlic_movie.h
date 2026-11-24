#ifndef GARLIC_MOVIE_H
#define GARLIC_MOVIE_H
#include "cpymo_error.h"
#include "cpymo_str.h"
#include <stdint.h>
struct cpymo_engine;
error_t garlic_movie_play(struct cpymo_engine *, cpymo_str);
/* False leaves normal mixing to the caller. True always fills the batch. */
bool garlic_movie_audio(int16_t *stereo, size_t frames);
void garlic_movie_log(const char *message);
error_t garlic_request_native_probe(void);
void garlic_abort_game(error_t cause);
#endif
