#ifndef RG35XX_NATIVE_PLAYBACK_CLIENT_H
#define RG35XX_NATIVE_PLAYBACK_CLIENT_H

#include "cpymo_error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cpymo_engine;
error_t garlic_play_native_movie(struct cpymo_engine *, const char *path);
bool garlic_native_movie_audio(int16_t *, size_t);
bool garlic_native_movie_video(uint16_t *, float);

#endif
