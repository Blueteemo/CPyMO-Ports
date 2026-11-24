#ifndef RG35XX_NATIVE_PLAYBACK_PRESENT_H
#define RG35XX_NATIVE_PLAYBACK_PRESENT_H

#include "shared.h"
#include <string.h>

struct native_presenter {
    uint32_t audio_clock;
    unsigned screen_clock, frames, dropped, underrun_frames;
    unsigned max_late_ms;
};

/* Samples submitted to the frontend form the master clock. A temporary
   shortage emits silence and holds the time until real samples arrive. */
static inline void native_present_audio(struct native_playback *s, struct native_presenter *p,
    int16_t *buffer, unsigned frames)
{
    memset(buffer, 0, frames*4);
    uint32_t cursor = native_load(&s->audio_read);
    uint32_t available = native_load(&s->audio_write) - cursor;
    int done = native_load(&s->audio_done);
    unsigned clock = native_load(&p->audio_clock);
    if (!native_load(&s->video_write) || (!clock && !done && available < 4800))
        return;
    unsigned count = available < frames ? available : frames;
    unsigned offset = cursor % NATIVE_AUDIO_SAMPLES;
    unsigned consecutive = NATIVE_AUDIO_SAMPLES - offset;
    if (consecutive > count) consecutive = count;
    memcpy(buffer, s->audio + offset*2, consecutive*4);
    memcpy(buffer + consecutive*2, s->audio, (count-consecutive)*4);
    native_store(&s->audio_read, cursor+count);
    if (!done && count < frames) p->underrun_frames += frames-count;
    native_store(&p->audio_clock, clock + (done ? frames : count));
}

static inline void native_present_frame(struct native_playback *s, struct native_presenter *p,
    uint16_t *pixels, unsigned elapsed_samples)
{
    unsigned audio = native_load(&p->audio_clock);
    p->screen_clock += elapsed_samples;
    if (p->screen_clock > audio) p->screen_clock = audio;
    if (audio > p->screen_clock + 4096) p->screen_clock = audio-4096;
    unsigned ms = p->screen_clock/48;
    native_store(&s->playhead_ms, ms);
    uint32_t cursor = native_load(&s->video_read);
    uint32_t ready = native_load(&s->video_write);
    uint32_t next = cursor;
    while (next != ready && s->video[next % NATIVE_VIDEO_SLOTS].pts_ms <= ms)
        ++next;
    if (next == cursor) return;
    const struct native_frame *frame = &s->video[(next-1) % NATIVE_VIDEO_SLOTS];
    memcpy(pixels, frame->pixels, sizeof(frame->pixels));
    ++p->frames;
    p->dropped += next - cursor - 1;
    unsigned lag = ms - frame->pts_ms;
    if (lag > p->max_late_ms) p->max_late_ms = lag;
    native_store(&s->video_read, next);
}

#endif
