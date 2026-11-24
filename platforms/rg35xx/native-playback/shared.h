#ifndef RG35XX_NATIVE_PLAYBACK_SHARED_H
#define RG35XX_NATIVE_PLAYBACK_SHARED_H

#include <stdint.h>

enum {
    NATIVE_MAGIC = 0x43505644,
    NATIVE_VERSION = 2,
    NATIVE_WIDTH = 640,
    NATIVE_HEIGHT = 480,
    NATIVE_VIDEO_SLOTS = 4,
    NATIVE_AUDIO_SAMPLES = 32768,
    NATIVE_AUDIO_RATE = 48000,
    NATIVE_MAX_MS = 600000
};

enum { NATIVE_STARTING, NATIVE_RUNNING, NATIVE_FINISHED, NATIVE_FAILED };

struct native_frame {
    uint32_t pts_ms;
    uint16_t pixels[NATIVE_WIDTH * NATIVE_HEIGHT];
};

struct native_playback {
    uint32_t magic, version;
    uint32_t state, stop, audio_done;
    uint32_t video_read, video_write, audio_read, audio_write;
    uint32_t playhead_ms;
    struct native_frame video[NATIVE_VIDEO_SLOTS];
    int16_t audio[NATIVE_AUDIO_SAMPLES * 2];
};

_Static_assert(sizeof(struct native_frame) == 614404, "shared frame layout");
_Static_assert(sizeof(struct native_playback) == 2588728, "shared playback layout");

/* Single producer and single consumer per queue. The published counter
   makes the pixels or samples visible before the other process reads them. */
static inline uint32_t native_load(const uint32_t *field)
{
    return __atomic_load_n(field, __ATOMIC_ACQUIRE);
}

static inline void native_store(uint32_t *field, uint32_t value)
{
    __atomic_store_n(field, value, __ATOMIC_RELEASE);
}

#endif
