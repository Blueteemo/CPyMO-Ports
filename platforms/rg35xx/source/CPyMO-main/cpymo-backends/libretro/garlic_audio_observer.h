#ifndef GARLIC_AUDIO_OBSERVER_H
#define GARLIC_AUDIO_OBSERVER_H
/* Per-process, bounded diagnostics. The audio thread only appends to RAM;
   the main thread can read an acquire-published immutable prefix at unload. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GARLIC_CAPTURE_FRAMES (48000u * 10u)
static int16_t garlic_capture_pcm[GARLIC_CAPTURE_FRAMES * 2u];
static uint32_t garlic_capture_frames, garlic_audio_observed_calls;
static uint32_t garlic_audio_max_mix_us, garlic_audio_max_submit_us;
static uint32_t garlic_audio_max_start_gap_us, garlic_audio_partial_batches;
static uint32_t garlic_audio_previous_active, garlic_audio_previous_begin;
static bool garlic_audio_observer_enabled, garlic_audio_capture_enabled;
static void garlic_audio_observer_configure(void)
{
    const char *report = getenv("CPYMO_AUDIO_REPORT");
    const char *capture = getenv("CPYMO_AUDIO_CAPTURE");
    garlic_audio_capture_enabled = capture && *capture;
    garlic_audio_observer_enabled = (report && *report) || garlic_audio_capture_enabled;
}
static void garlic_audio_observer_max(uint32_t *value, uint32_t sample)
{
    uint32_t old = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (sample > old && !__atomic_compare_exchange_n(value, &old, sample, 0,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void garlic_audio_observe(const int16_t *pcm, unsigned frames, bool bgm,
    uint32_t begin, uint32_t mixed, uint32_t submitted, size_t accepted)
{
    if (!garlic_audio_observer_enabled) return;
    __atomic_fetch_add(&garlic_audio_observed_calls, 1, __ATOMIC_RELAXED);
    garlic_audio_observer_max(&garlic_audio_max_mix_us, mixed - begin);
    garlic_audio_observer_max(&garlic_audio_max_submit_us, submitted - mixed);
    if (__atomic_exchange_n(&garlic_audio_previous_active, bgm, __ATOMIC_RELAXED) && bgm)
        garlic_audio_observer_max(&garlic_audio_max_start_gap_us, begin - garlic_audio_previous_begin);
    garlic_audio_previous_begin = begin;
    if (accepted < frames)
        __atomic_fetch_add(&garlic_audio_partial_batches, 1, __ATOMIC_RELAXED);
    if (bgm && garlic_audio_capture_enabled) {
        uint32_t at = __atomic_load_n(&garlic_capture_frames, __ATOMIC_RELAXED);
        unsigned take = frames;
        if (take > GARLIC_CAPTURE_FRAMES - at) take = GARLIC_CAPTURE_FRAMES - at;
        if (take) {
            memcpy(garlic_capture_pcm + at * 2u, pcm, take * 4u);
            __atomic_store_n(&garlic_capture_frames, at + take, __ATOMIC_RELEASE);
        }
    }
}
static void garlic_audio_le32(unsigned char *p, uint32_t value)
{ p[0]=value; p[1]=value>>8; p[2]=value>>16; p[3]=value>>24; }
static bool garlic_audio_write_capture(const char *path, uint32_t frames)
{
    if (!path || !*path || !frames || frames > GARLIC_CAPTURE_FRAMES) return false;
    unsigned char header[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,
        'd','a','t','a',0,0,0,0};
    garlic_audio_le32(header + 4, frames * 4u + 36u);
    garlic_audio_le32(header + 40, frames * 4u);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) &&
        fwrite(garlic_capture_pcm, 4, frames, f) == frames;
    if (fclose(f)) ok = false;
    return ok;
}
#endif
