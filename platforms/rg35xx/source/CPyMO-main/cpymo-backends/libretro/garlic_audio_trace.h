#ifndef GARLIC_AUDIO_TRACE_H
#define GARLIC_AUDIO_TRACE_H
#include <stdint.h>
struct cpymo_engine;
/* Uses the existing opt-in, per-game, 256 KiB diagnostic sink. */
void garlic_audio_trace(const char *event, const char *name,
    uint64_t output_frames, uint64_t source_cursor, long file_offset,
    const struct cpymo_engine *engine);
#endif
