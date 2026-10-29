/* Libretro audio backend: MP3 (BGM / SE / Voice) with no external library.
 *
 * The stock engine audio (cpymo/cpymo_audio.c) is written against FFmpeg and
 * needs libavformat + libavcodec + libswresample at run time. GarlicOS does
 * not ship them, so this build compiles the engine with DISABLE_AUDIO +
 * DISABLE_FFMPEG_AUDIO and implements the same API here on top of minimp3
 * (single public-domain header, ~40 KB of code, no .so dependency).
 *
 * Everything the game actually uses is MP3: the bgm directory, se.pak and
 * voice.pak.
 * Files are streamed through a small ring buffer, so a 4 MB BGM costs ~64 KB
 * of RAM rather than 4 MB. Decoding runs on the emulation thread inside
 * cpymo_audio_copy_mixed_samples(), which libretro.c already calls once per
 * frame with 44100/60 stereo frames; the mixing is therefore lock-free in the
 * batch path RetroArch uses.
 */
#if defined(DISABLE_FFMPEG_AUDIO) && !defined(DISABLE_AUDIO)

#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_assetloader.h"
#include "../../cpymo/cpymo_package.h"
#include "../../cpymo/cpymo_str.h"
#include "../../cpymo/cpymo_error.h"
#include "../include/cpymo_backend_audio.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GARLIC_AUDIO_RING (64u * 1024u)
#define GARLIC_AUDIO_OUT_RATE 44100
#define GARLIC_AUDIO_FILE_MAX (16u * 1024u * 1024u)

typedef struct {
    bool active, loop, eof, has_reader, tried_reload;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    FILE *file;
    cpymo_package_stream_reader reader;
    unsigned char *ring;
    size_t ring_len, ring_pos;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    size_t pcm_frames, pcm_cur;
    uint32_t acc, step;
    int rate, channels;
    float volume;
    char *name;
} garlic_mp3_channel;

static garlic_mp3_channel garlic_ch[CPYMO_AUDIO_MAX_CHANNELS];
static int garlic_audio_ready = 0;

/* --- channel lifecycle ------------------------------------------------ */

static void ch_close_stream(garlic_mp3_channel *c)
{
    if (c->file) { fclose(c->file); c->file = NULL; }
    if (c->has_reader) {
        cpymo_package_stream_reader_close(&c->reader);
        c->has_reader = false;
    }
}

static void ch_stop(garlic_mp3_channel *c)
{
    c->active = false;
    c->loop = false;
    c->eof = false;
    ch_close_stream(c);
    c->ring_len = c->ring_pos = 0;
    c->pcm_frames = c->pcm_cur = 0;
}

static void ch_set_name(garlic_mp3_channel *c, cpymo_str name)
{
    free(c->name);
    c->name = NULL;
    if (name.len) {
        c->name = (char *)malloc(name.len + 1);
        if (c->name) cpymo_str_copy(c->name, name.len + 1, name);
    }
}

static void ch_stream_rewind(garlic_mp3_channel *c)
{
    if (c->has_reader) cpymo_package_stream_reader_seek(0, &c->reader);
    else if (c->file) fseek(c->file, 0, SEEK_SET);
    c->ring_len = c->ring_pos = 0;
    c->pcm_frames = c->pcm_cur = 0;
    mp3dec_init(&c->dec);
}

/* Fill the ring. Returns false only when the stream is over for good. */
static bool ch_refill(garlic_mp3_channel *c)
{
    if (c->ring_pos) {
        memmove(c->ring, c->ring + c->ring_pos, c->ring_len - c->ring_pos);
        c->ring_len -= c->ring_pos;
        c->ring_pos = 0;
    }
    if (c->ring_len + 4096u >= GARLIC_AUDIO_RING) return true;

    size_t want = GARLIC_AUDIO_RING - c->ring_len;
    size_t got;
    if (c->has_reader)
        got = cpymo_package_stream_reader_read(
            (char *)c->ring + c->ring_len, want, &c->reader);
    else if (c->file)
        got = fread(c->ring + c->ring_len, 1, want, c->file);
    else
        got = 0;

    c->ring_len += got;
    if (got) { c->tried_reload = false; return true; }

    if (c->loop && !c->tried_reload) {
        c->tried_reload = true;
        ch_stream_rewind(c);
        return ch_refill(c);
    }

    c->eof = true;
    return false;
}

/* Decode one frame into c->pcm. Returns frames per channel, 0 at end. */
static size_t ch_decode(garlic_mp3_channel *c)
{
    for (int guard = 0; guard < 4096; ++guard) {
        if (c->ring_len - c->ring_pos < 2048u && !c->eof) ch_refill(c);
        if (c->ring_len <= c->ring_pos) return 0;

        int n = mp3dec_decode_frame(
            &c->dec, c->ring + c->ring_pos, (int)(c->ring_len - c->ring_pos),
            c->pcm, &c->info);

        if (c->info.frame_bytes > 0) c->ring_pos += (size_t)c->info.frame_bytes;

        if (n > 0) {
            if (c->info.channels < 1) return 0;
            c->channels = c->info.channels;
            c->rate = c->info.hz;
            if (c->rate <= 0) c->rate = GARLIC_AUDIO_OUT_RATE;
            /* nearest-neighbour resample to the output rate, 16.16 fixed */
            c->step = (uint32_t)(((uint64_t)c->rate << 16) /
                GARLIC_AUDIO_OUT_RATE);
            if (!c->step) c->step = 1;
            c->pcm_frames = (size_t)n;
            c->pcm_cur = 0;
            return (size_t)n;
        }

        if (c->info.frame_bytes == 0) {
            if (c->eof) return 0;
            if (c->ring_len - c->ring_pos > 8u) c->ring_pos++; /* resync */
            else ch_refill(c);
        }
    }
    return 0;
}

static error_t ch_play(
    size_t cid, char *path, const cpymo_package_stream_reader *pkg,
    cpymo_str name, bool loop)
{
    garlic_mp3_channel *c = &garlic_ch[cid];
    if (cid >= CPYMO_AUDIO_MAX_CHANNELS) { free(path); return CPYMO_ERR_UNKNOWN; }

    ch_stop(c);
    if (!c->ring) {
        c->ring = (unsigned char *)malloc(GARLIC_AUDIO_RING);
        if (!c->ring) { free(path); return CPYMO_ERR_OUT_OF_MEM; }
    }

    if (pkg) {
        c->reader = *pkg;
        c->has_reader = true;
    }
    else if (path) {
        c->file = fopen(path, "rb");
        if (!c->file) { free(path); return CPYMO_ERR_CAN_NOT_OPEN_FILE; }
    }
    else { free(path); return CPYMO_ERR_CAN_NOT_OPEN_FILE; }
    free(path);

    mp3dec_init(&c->dec);
    c->loop = loop;
    c->tried_reload = false;
    c->rate = GARLIC_AUDIO_OUT_RATE;
    c->channels = 2;

    if (ch_decode(c) == 0) { ch_close_stream(c); return CPYMO_ERR_BAD_FILE_FORMAT; }

    ch_set_name(c, name);
    c->acc = 0;
    c->active = true;
#ifdef GARLIC_AUDIO_DEBUG
    fprintf(stderr, "AUDIO play ch=%u loop=%d rate=%d ch=%d name=%s\n",
        (unsigned)cid, (int)loop, c->rate, c->channels,
        c->name ? c->name : "(file)");
#endif
    return CPYMO_ERR_SUCC;
}

/* --- mixing ----------------------------------------------------------- */

static void ch_mix(garlic_mp3_channel *c, int32_t *out, size_t frames)
{
    size_t produced = 0;
    while (produced < frames) {
        if (!c->active) break;

        while (c->acc >= 65536u) {
            c->acc -= 65536u;
            if (c->pcm_cur + 1 < c->pcm_frames) c->pcm_cur++;
            else if (ch_decode(c) == 0) { c->active = false; break; }
        }
        if (!c->active) break;

        const int ch = c->channels;
        const int16_t l = c->pcm[c->pcm_cur * (size_t)ch];
        const int16_t r = ch > 1 ? c->pcm[c->pcm_cur * (size_t)ch + 1] : l;

        out[produced * 2] += (int32_t)((float)l * c->volume);
        out[produced * 2 + 1] += (int32_t)((float)r * c->volume);

        produced++;
        c->acc += c->step;
    }
}

void cpymo_audio_copy_mixed_samples(void *dst, size_t len, cpymo_audio_system *s)
{
    (void)s;
    int16_t *out = (int16_t *)dst;
    size_t frames = len / 4;
    if (!garlic_audio_ready || len < 4) { memset(dst, 0, len); return; }

    int32_t chunk[512 * 2];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > 512) n = 512;
        memset(chunk, 0, n * 2 * sizeof(int32_t));

        for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i)
            if (garlic_ch[i].active) ch_mix(&garlic_ch[i], chunk, n);

        for (size_t i = 0; i < n * 2; ++i) {
            int32_t v = chunk[i];
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            out[done * 2 + i] = (int16_t)v;
        }
        done += n;
    }
}

bool cpymo_audio_channel_get_samples(
    void **samples, size_t *len, size_t cid, cpymo_audio_system *s)
{
    (void)s; (void)cid;
    *samples = NULL;
    *len = 0;
    return false;
}

/* --- engine API ------------------------------------------------------- */

void cpymo_audio_init(cpymo_audio_system *s)
{
    (void)s;
    for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
        garlic_mp3_channel *c = &garlic_ch[i];
        if (garlic_audio_ready) ch_stop(c);
        memset(c, 0, sizeof(*c));
        c->volume = 1.0f;
    }
    garlic_audio_ready = 1;
}

void cpymo_audio_free(cpymo_audio_system *s)
{
    (void)s;
    for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
        ch_stop(&garlic_ch[i]);
        free(garlic_ch[i].name);
        garlic_ch[i].name = NULL;
    }
    garlic_audio_ready = 0;
}

bool cpymo_audio_enabled(struct cpymo_engine *e)
{
    (void)e;
    return garlic_audio_ready != 0;
}

float cpymo_audio_get_channel_volume(
    size_t cid, const cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS ? garlic_ch[cid].volume : 0.0f;
}

void cpymo_audio_set_channel_volume(
    size_t cid, cpymo_audio_system *s, float volume)
{
    (void)s;
    if (cid >= CPYMO_AUDIO_MAX_CHANNELS) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    garlic_ch[cid].volume = volume;
}

bool cpymo_audio_channel_is_playing(size_t cid, cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS && garlic_ch[cid].active;
}

bool cpymo_audio_channel_is_looping(size_t cid, cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS && garlic_ch[cid].active &&
        garlic_ch[cid].loop;
}

bool cpymo_audio_wait_se(struct cpymo_engine *e, float d)
{
    (void)d;
    /* B must be able to cut a long SE short, as in the original engine. */
    if (cpymo_input_foward_key_just_pressed(e)) return true;
    return !garlic_ch[CPYMO_AUDIO_CHANNEL_SE].active;
}

error_t cpymo_audio_bgm_play(cpymo_engine *e, cpymo_str name, bool loop)
{
    char *path = NULL;
    error_t err = cpymo_assetloader_get_bgm_path(&path, name, &e->assetloader);
    if (err != CPYMO_ERR_SUCC) return err;
    return ch_play(CPYMO_AUDIO_CHANNEL_BGM, path, NULL, name, loop);
}

error_t cpymo_audio_se_play(cpymo_engine *e, cpymo_str name, bool loop)
{
    if (e->assetloader.use_pkg_se) {
        cpymo_package_stream_reader r;
        error_t err = cpymo_package_stream_reader_find_create(
            &r, &e->assetloader.pkg_se, name);
        if (err != CPYMO_ERR_SUCC) return err;
        err = ch_play(CPYMO_AUDIO_CHANNEL_SE, NULL, &r, name, loop);
        if (err != CPYMO_ERR_SUCC) cpymo_package_stream_reader_close(&r);
        return err;
    }

    char *path = NULL;
    error_t err = cpymo_assetloader_get_se_path(&path, name, &e->assetloader);
    if (err != CPYMO_ERR_SUCC) return err;
    return ch_play(CPYMO_AUDIO_CHANNEL_SE, path, NULL, name, loop);
}

error_t cpymo_audio_vo_play(cpymo_engine *e, cpymo_str name)
{
    if (e->assetloader.use_pkg_voice) {
        cpymo_package_stream_reader r;
        error_t err = cpymo_package_stream_reader_find_create(
            &r, &e->assetloader.pkg_voice, name);
        if (err != CPYMO_ERR_SUCC) return err;
        err = ch_play(CPYMO_AUDIO_CHANNEL_VO, NULL, &r, name, false);
        if (err != CPYMO_ERR_SUCC) cpymo_package_stream_reader_close(&r);
        return err;
    }

    char *path = NULL;
    error_t err = cpymo_assetloader_get_vo_path(&path, name, &e->assetloader);
    if (err != CPYMO_ERR_SUCC) return err;
    return ch_play(CPYMO_AUDIO_CHANNEL_VO, path, NULL, name, false);
}

void cpymo_audio_bgm_stop(struct cpymo_engine *e)
{
    (void)e;
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_BGM]);
}

void cpymo_audio_se_stop(struct cpymo_engine *e)
{
    (void)e;
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_SE]);
}

void cpymo_audio_vo_stop(struct cpymo_engine *e)
{
    (void)e;
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_VO]);
}

const char *cpymo_audio_get_bgm_name(struct cpymo_engine *e)
{
    (void)e;
    return garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].name
        ? garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].name : "";
}

const char *cpymo_audio_get_se_name(struct cpymo_engine *e)
{
    (void)e;
    return garlic_ch[CPYMO_AUDIO_CHANNEL_SE].name
        ? garlic_ch[CPYMO_AUDIO_CHANNEL_SE].name : "";
}

error_t cpymo_audio_play_video(struct cpymo_engine *e, const char *path)
{
    (void)e;
    char *p = NULL;
    if (path) {
        size_t n = strlen(path);
        p = (char *)malloc(n + 1);
        if (!p) return CPYMO_ERR_OUT_OF_MEM;
        memcpy(p, path, n + 1);
    }
    return ch_play(CPYMO_AUDIO_CHANNEL_BGM, p, NULL,
        (cpymo_str){ "", 0 }, false);
}

#endif
