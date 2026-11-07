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
 * frame with 48000/60 stereo frames. A short audio-only mutex also protects
 * the optional frontend callback path; UI rendering never owns this lock.
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
#include "garlic_audio_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "garlic_audio_sync.h"
#define GARLIC_AUDIO_GAIN_IN_MIXER 1

/* PyMO ports on Android/PC ship Ogg Vorbis and WAV; both are decoded here so a
 * game directory never has to be converted. stb_vorbis is public domain. */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define GARLIC_AUDIO_RING (64u * 1024u)
/* RG35XX/GarlicOS runs RetroArch at 48 kHz; matching it keeps the frontend
 * resampler out of the chain. */
#define GARLIC_AUDIO_OUT_RATE 48000
/* Windowed-sinc resampler: 16 taps over 512 fractional phases plus a continuous endpoint (~8 KB per
 * channel, built once per play). Images of a 12 kHz tone fall from -21 dB
 * (cubic) to below -70 dB. */
#define GARLIC_AUDIO_RES_TAPS 16
#define GARLIC_AUDIO_RES_PHASES 512
#define GARLIC_AUDIO_RES_SHIFT 7
#define GARLIC_AUDIO_FILE_MAX (16u * 1024u * 1024u)

enum { garlic_dec_mp3, garlic_dec_vorbis, garlic_dec_wav };

typedef struct {
    bool active, loop, eof, has_reader, tried_reload;
    int kind;
    unsigned char *mem;
    size_t mem_len;
    stb_vorbis *vor;
    int wav_bits, wav_align;
    long wav_data_off, wav_data_len;
    size_t wav_bytes_left;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    FILE *file;
    cpymo_package_stream_reader reader;
    unsigned char *ring;
    size_t ring_len, ring_pos;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    int16_t vorbuf[4096 * 2];
    size_t pcm_frames, pcm_cur;
    uint64_t decoded_frames, output_frames;
    /* Windowed-sinc interpolator: TAPS source frames, a 512-phase kernel for
     * the current step, and the fractional read position. `fade` hides the
     * seam after a loop restart. */
    int16_t win[GARLIC_AUDIO_RES_TAPS][2];
    int16_t *res_tab;
    uint32_t res_step;
    uint32_t frac, step, fade, fade_len;
    bool primed;
    int rate, channels;
    float volume;
    char *name;
} garlic_mp3_channel;

static garlic_mp3_channel garlic_ch[CPYMO_AUDIO_MAX_CHANNELS];
static bool ch_build_resampler(garlic_mp3_channel *c);
static int garlic_audio_ready = 0;
static unsigned garlic_mixer_output_percent = 50;
static float garlic_limiter_gain = 1.0f;
static unsigned garlic_limiter_hold;
static void cpymo_audio_free_unlocked(cpymo_audio_system *s);

/* --- channel lifecycle ------------------------------------------------ */

static void ch_close_stream(garlic_mp3_channel *c)
{
    if (c->vor) { stb_vorbis_close(c->vor); c->vor = NULL; }
    if (c->mem) { free(c->mem); c->mem = NULL; c->mem_len = 0; }
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
    if (c->kind == garlic_dec_vorbis) {
        if (c->vor) stb_vorbis_seek_start(c->vor);
    }
    else if (c->kind == garlic_dec_wav) {
        c->wav_bytes_left = (size_t)c->wav_data_len;
        if (c->file) fseek(c->file, c->wav_data_off, SEEK_SET);
        else if (c->has_reader)
            cpymo_package_stream_reader_seek((size_t)c->wav_data_off, &c->reader);
    }
    else if (c->has_reader) cpymo_package_stream_reader_seek(0, &c->reader);
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
    if (c->kind == garlic_dec_wav && want > c->wav_bytes_left)
        want = c->wav_bytes_left;
    if (c->has_reader)
        got = cpymo_package_stream_reader_read(
            (char *)c->ring + c->ring_len, want, &c->reader);
    else if (c->file)
        got = fread(c->ring + c->ring_len, 1, want, c->file);
    else
        got = 0;

    c->ring_len += got;
    if (c->kind == garlic_dec_wav) c->wav_bytes_left -= got;
    if (got) { c->tried_reload = false; return true; }

    /* EOF belongs to the decoder/mixer boundary, not the read-ahead path.
     * MP3 refill starts with <2048 bytes still buffered: rewinding here drops
     * valid tail frames. Drain the tail first; ch_mix alone restarts loops. */
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
            if (!ch_build_resampler(c)) return 0;
            c->decoded_frames += (size_t)n;
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

static size_t ch_read_exact(garlic_mp3_channel *c, void *dst, size_t n);
static void ch_seek_raw(garlic_mp3_channel *c, size_t off);
static error_t ch_open_wav(garlic_mp3_channel *c);
static error_t ch_open_vorbis(garlic_mp3_channel *c);
static bool ch_frame(garlic_mp3_channel *c, int16_t *l, int16_t *r);

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
    c->primed = false;
    c->frac = 0;
    c->fade = c->fade_len = 0;
    c->wav_data_off = c->wav_data_len = 0;
    c->decoded_frames = c->output_frames = 0;

    /* Sniff the container: RIFF/WAVE, Ogg Vorbis or MPEG audio. */
    {
        unsigned char hdr[12];
        size_t hl = ch_read_exact(c, hdr, sizeof(hdr));
        ch_seek_raw(c, 0);
        c->ring_len = c->ring_pos = 0;

        error_t err = CPYMO_ERR_SUCC;
        if (hl >= 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0)
            c->kind = garlic_dec_wav;
        else if (hl >= 4 && memcmp(hdr, "OggS", 4) == 0)
            c->kind = garlic_dec_vorbis;
        else
            c->kind = garlic_dec_mp3;

        if (c->kind == garlic_dec_wav) err = ch_open_wav(c);
        else if (c->kind == garlic_dec_vorbis) err = ch_open_vorbis(c);

        if (err != CPYMO_ERR_SUCC) { ch_close_stream(c); return err; }
    }

    if (c->kind == garlic_dec_mp3) {
        if (ch_decode(c) == 0) { ch_close_stream(c); return CPYMO_ERR_BAD_FILE_FORMAT; }
    }
    else {
        int16_t l = 0, r = 0;
        c->pcm_frames = c->pcm_cur = 0;
        if (!ch_frame(c, &l, &r)) { ch_close_stream(c); return CPYMO_ERR_BAD_FILE_FORMAT; }
        ch_stream_rewind(c);
        c->step = (uint32_t)(((uint64_t)c->rate << 16) / GARLIC_AUDIO_OUT_RATE);
        if (!c->step) c->step = 65536u;
        if (!ch_build_resampler(c)) { ch_close_stream(c); return CPYMO_ERR_OUT_OF_MEM; }
#ifdef GARLIC_AUDIO_DEBUG
        fprintf(stderr, "WAVDBG kind=%d rate=%d ch=%d align=%d bits=%d "
            "datalen=%ld step=%u\n", (int)c->kind, c->rate, c->channels,
            c->wav_align, c->wav_bits, c->wav_data_len, (unsigned)c->step);
#endif
    }

    if (c->kind != garlic_dec_mp3) c->decoded_frames = 0;
    ch_set_name(c, name);
    c->active = true;
#ifdef GARLIC_AUDIO_DEBUG
    fprintf(stderr, "AUDIO play ch=%u loop=%d rate=%d ch=%d name=%s\n",
        (unsigned)cid, (int)loop, c->rate, c->channels,
        c->name ? c->name : "(file)");
#endif
    return CPYMO_ERR_SUCC;
}

/* --- mixing ----------------------------------------------------------- */

/* Read exactly n bytes from the raw stream (header parsing). */
static size_t ch_read_exact(garlic_mp3_channel *c, void *dst, size_t n)
{
    if (c->has_reader)
        return cpymo_package_stream_reader_read((char *)dst, n, &c->reader);
    if (c->file) return fread(dst, 1, n, c->file);
    return 0;
}

static void ch_seek_raw(garlic_mp3_channel *c, size_t off)
{
    if (c->has_reader) cpymo_package_stream_reader_seek(off, &c->reader);
    else if (c->file) fseek(c->file, (long)off, SEEK_SET);
}

/* Parse a RIFF/WAVE header and leave the stream at the PCM data. */
static error_t ch_open_wav(garlic_mp3_channel *c)
{
    ch_seek_raw(c, 12);
    unsigned char ch_hdr[8];
    long off = 12;
    bool have_fmt = false;
    while (ch_read_exact(c, ch_hdr, 8) == 8) {
        unsigned long sz = (unsigned long)ch_hdr[4] | ((unsigned long)ch_hdr[5] << 8)
            | ((unsigned long)ch_hdr[6] << 16) | ((unsigned long)ch_hdr[7] << 24);
        if (memcmp(ch_hdr, "fmt ", 4) == 0 && sz >= 16) {
            unsigned char f[16];
            if (ch_read_exact(c, f, 16) != 16) return CPYMO_ERR_BAD_FILE_FORMAT;
            unsigned fmt = (unsigned)f[0] | ((unsigned)f[1] << 8);
            if (fmt != 1) return CPYMO_ERR_BAD_FILE_FORMAT; /* PCM only */
            c->channels = (int)((unsigned)f[2] | ((unsigned)f[3] << 8));
            c->rate = (int)((unsigned)f[4] | ((unsigned)f[5] << 8)
                | ((unsigned)f[6] << 16) | ((unsigned)f[7] << 24));
            c->wav_align = (int)((unsigned)f[12] | ((unsigned)f[13] << 8));
            c->wav_bits = (int)((unsigned)f[14] | ((unsigned)f[15] << 8));
            have_fmt = true;
            off += 8 + (long)sz;
            ch_seek_raw(c, (size_t)off);
        }
        else if (memcmp(ch_hdr, "data", 4) == 0) {
            if (!have_fmt || c->wav_align <= 0 || c->wav_bits != 16) {
                return CPYMO_ERR_BAD_FILE_FORMAT;
            }
            if (c->channels < 1 || c->channels > 2 ||
                c->wav_align != c->channels * 2) return CPYMO_ERR_BAD_FILE_FORMAT;
            if (c->rate < 8000 || c->rate > 96000) return CPYMO_ERR_BAD_FILE_FORMAT;
            c->wav_data_off = off + 8;
            c->wav_data_len = (long)sz;
            c->wav_bytes_left = (size_t)sz;
            ch_seek_raw(c, (size_t)c->wav_data_off);
            c->ring_len = c->ring_pos = 0;
            return CPYMO_ERR_SUCC;
        }
        else {
            off += 8 + (long)sz;
            ch_seek_raw(c, (size_t)(off + (sz & 1)));
            off += (long)(sz & 1);
        }
    }
    return CPYMO_ERR_BAD_FILE_FORMAT;
}

/* Whole-stream load for Vorbis (SE/voice members are small, BGM is a file). */
static error_t ch_open_vorbis(garlic_mp3_channel *c)
{
    size_t cap = 0;
    if (c->file) {
        if (fseek(c->file, 0, SEEK_END) != 0) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
        long len = ftell(c->file);
        if (len <= 0 || len > (long)GARLIC_AUDIO_FILE_MAX)
            return CPYMO_ERR_BAD_FILE_FORMAT;
        fseek(c->file, 0, SEEK_SET);
        cap = (size_t)len;
        c->mem = (unsigned char *)malloc(cap);
        if (!c->mem) return CPYMO_ERR_OUT_OF_MEM;
        c->mem_len = fread(c->mem, 1, cap, c->file);
        fclose(c->file);
        c->file = NULL;
    }
    else {
        cap = 32u * 1024u;
        c->mem = (unsigned char *)malloc(cap);
        if (!c->mem) return CPYMO_ERR_OUT_OF_MEM;
        for (;;) {
            if (c->mem_len == cap) {
                if (cap >= GARLIC_AUDIO_FILE_MAX) return CPYMO_ERR_BAD_FILE_FORMAT;
                unsigned char *bigger = (unsigned char *)realloc(c->mem, cap * 2);
                if (!bigger) return CPYMO_ERR_OUT_OF_MEM;
                c->mem = bigger;
                cap *= 2;
            }
            size_t got = cpymo_package_stream_reader_read(
                (char *)c->mem + c->mem_len, cap - c->mem_len, &c->reader);
            if (got == 0) break;
            c->mem_len += got;
        }
        cpymo_package_stream_reader_close(&c->reader);
        c->has_reader = false;
    }

    int err = 0;
    c->vor = stb_vorbis_open_memory(c->mem, (int)c->mem_len, &err, NULL);
#ifdef GARLIC_AUDIO_DEBUG
    fprintf(stderr, "VOR mem=%p len=%zu vor=%p err=%d\n", (void *)c->mem,
        c->mem_len, (void *)c->vor, err);
#endif
    if (!c->vor) return CPYMO_ERR_BAD_FILE_FORMAT;
    stb_vorbis_info info = stb_vorbis_get_info(c->vor);
    c->channels = (int)info.channels;
    c->rate = (int)info.sample_rate;
    return CPYMO_ERR_SUCC;
}

/* Next decoded source frame. Returns false when the stream ends. */
static bool ch_frame_mp3(garlic_mp3_channel *c, int16_t *l, int16_t *r)
{
    for (;;) {
        if (c->pcm_cur < c->pcm_frames) {
            const size_t ch = (size_t)c->channels;
            *l = c->pcm[c->pcm_cur * ch];
            *r = ch > 1 ? c->pcm[c->pcm_cur * ch + 1] : *l;
            c->pcm_cur++;
            return true;
        }
        if (ch_decode(c) == 0) return false;
    }
}

static bool ch_frame_wav(garlic_mp3_channel *c, int16_t *l, int16_t *r)
{
    const size_t need = (size_t)c->wav_align;
    while (c->ring_len - c->ring_pos < need) {
        if (c->eof) return false;
        if (!ch_refill(c) && c->eof) return false;
    }
    const unsigned char *p = c->ring + c->ring_pos;
    *l = (int16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
    if (c->channels > 1) *r = (int16_t)((unsigned)p[2] | ((unsigned)p[3] << 8));
    else *r = *l;
    c->ring_pos += need;
    return true;
}

static bool ch_frame_vorbis(garlic_mp3_channel *c, int16_t *l, int16_t *r)
{
    if (c->pcm_cur >= c->pcm_frames) {
        /* One whole Vorbis block at a time; the single-sample API returns 0
         * for small requests. */
        int n = stb_vorbis_get_frame_short_interleaved(c->vor, c->channels,
            c->vorbuf, 4096);
        if (n <= 0) return false;
        c->decoded_frames += (size_t)n;
        c->pcm_frames = (size_t)n;
        c->pcm_cur = 0;
    }
    const size_t ch = (size_t)c->channels;
    *l = c->vorbuf[c->pcm_cur * ch];
    *r = ch > 1 ? c->vorbuf[c->pcm_cur * ch + 1] : *l;
    c->pcm_cur++;
    return true;
}

static bool ch_frame(garlic_mp3_channel *c, int16_t *l, int16_t *r)
{
    if (c->kind == garlic_dec_wav) return ch_frame_wav(c, l, r);
    if (c->kind == garlic_dec_vorbis) return ch_frame_vorbis(c, l, r);
    return ch_frame_mp3(c, l, r);
}

/* Build the polyphase kernel for the current step. A Blackman-windowed sinc
 * rejects the images a cubic interpolator leaves behind (a 12 kHz test tone
 * imaged at -21 dB with Catmull-Rom); the table is per play, not per sample. */
static bool ch_build_resampler(garlic_mp3_channel *c)
{
    const int taps = GARLIC_AUDIO_RES_TAPS;
    const int phases = GARLIC_AUDIO_RES_PHASES;
    const int half = taps / 2;
    if (c->res_tab && c->res_step == c->step) return true;
    if (!c->res_tab) {
        c->res_tab = (int16_t *)malloc((size_t)(phases + 1) * taps * sizeof(int16_t));
        if (!c->res_tab) return false;
    }

    for (int p = 0; p <= phases; ++p) {
        double t = (double)p / (double)phases;
        double coef[GARLIC_AUDIO_RES_TAPS];
        double sum = 0;
        for (int k = 0; k < taps; ++k) {
            /* The kernel is centred taps/2-1 samples into the window, so
             * half of it looks at future samples (latency taps/2 samples). */
            double d = (double)k - (double)(taps / 2 - 1) - t;
            double v = 0;
            if (fabs(d) < (double)half) {
                double x = d * 3.14159265358979323846;
                double sinc = (fabs(d) < 1e-9) ? 1.0 : sin(x) / x;
                double w = 0.42 + 0.5 * cos(3.14159265358979323846 * d / half)
                    + 0.08 * cos(2.0 * 3.14159265358979323846 * d / half);
                v = sinc * w;
            }
            coef[k] = v;
            sum += v;
        }
        if (sum == 0) sum = 1;
        /* Scale to 32767 (not 32768) so a delta phase cannot overflow int16. */
        int32_t acc = 0;
        for (int k = 0; k < taps; ++k) {
            int32_t q = (int32_t)lrint(coef[k] / sum * 32767.0);
            if (q > 32767) q = 32767;
            if (q < -32767) q = -32767;
            c->res_tab[p * taps + k] = (int16_t)q;
            acc += q;
        }
        /* Absorb the rounding remainder in the centre tap so DC stays unity. */
        int32_t centre = c->res_tab[p * taps + taps / 2] + (32767 - acc);
        if (centre > 32767) centre = 32767;
        if (centre < -32767) centre = -32767;
        c->res_tab[p * taps + taps / 2] = (int16_t)centre;
    }
#ifdef GARLIC_AUDIO_DEBUG
    for (int p = 0; p < phases; p += 64) {
        fprintf(stderr, "KERNEL p=%d:", p);
        for (int k = 0; k < taps; ++k) fprintf(stderr, " %d", c->res_tab[p*taps+k]);
        fprintf(stderr, "\n");
    }
#endif
    c->res_step = c->step;
    return true;
}

static void ch_trace_bgm(const char *event, garlic_mp3_channel *c,
    uint64_t output_frames, const cpymo_engine *e)
{
    if (c != &garlic_ch[CPYMO_AUDIO_CHANNEL_BGM]) return;
    uint64_t cursor = c->decoded_frames - c->pcm_frames + c->pcm_cur;
    if (c->kind == garlic_dec_wav && c->wav_align > 0)
        cursor = ((size_t)c->wav_data_len - c->wav_bytes_left -
            (c->ring_len - c->ring_pos)) / (size_t)c->wav_align;
    garlic_audio_trace(event, c->name, output_frames, cursor,
        c->file ? ftell(c->file) : -1, e);
}

/* Restart a looping stream without dropping a frame: repeat the last sample
 * into the window, rewind, and fade the new pass in over 4 ms. */
static bool ch_loop_restart(garlic_mp3_channel *c, uint64_t output_frames)
{
    ch_trace_bgm("loop", c, output_frames, NULL);
    c->decoded_frames = 0;
    ch_stream_rewind(c);
    mp3dec_init(&c->dec);
    c->pcm_frames = c->pcm_cur = 0;
    c->eof = false;
    c->tried_reload = false;
    c->fade = 0;
    for (int i = 0; i < GARLIC_AUDIO_RES_TAPS; ++i) {
        if (!ch_frame(c, &c->win[i][0], &c->win[i][1])) return false;
    }
    return true;
}

static void ch_mix(garlic_mp3_channel *c, int32_t *out, size_t frames)
{
    size_t produced = 0;
    if (!c->active) return;

    if (!c->primed) {
        for (int i = 0; i < GARLIC_AUDIO_RES_TAPS; ++i) {
            if (!ch_frame(c, &c->win[i][0], &c->win[i][1])) {
                if (!c->loop || !ch_loop_restart(c, c->output_frames + produced)) { c->active = false; return; }
            }
        }
        c->primed = true;
        c->fade_len = (uint32_t)(GARLIC_AUDIO_OUT_RATE * 0.004f);
        c->fade = 0;
    }

    const int16_t *tab = c->res_tab;
    while (produced < frames) {
        /* Preserve v0.24.4 PCM for ordinary sources, while rejecting the
           invalid fractional state left by high-rate EOF before table access. */
        if (c->frac >= 65536u) break;
        int gain_q = (int)(c->volume * 4096.0f);
        if (c->fade < c->fade_len && c->fade_len)
            gain_q = gain_q * (int)c->fade / (int)c->fade_len;

        /* Interpolate between two table phases with the leftover fraction, so
         * the phase table does not quantise the delay. */
        size_t ph = (size_t)(c->frac >> GARLIC_AUDIO_RES_SHIFT);
        size_t ph2 = ph + 1;
        int sub = (int)(c->frac & ((1u << GARLIC_AUDIO_RES_SHIFT) - 1u));
        const int16_t *c0 = tab + ph * GARLIC_AUDIO_RES_TAPS;
        const int16_t *c1 = tab + ph2 * GARLIC_AUDIO_RES_TAPS;
        int32_t l = 0, r = 0;
        for (int k = 0; k < GARLIC_AUDIO_RES_TAPS; ++k) {
            int32_t cl = c0[k]
                + (((c1[k] - c0[k]) * sub) >> GARLIC_AUDIO_RES_SHIFT);
            l += (int32_t)c->win[k][0] * cl;
            r += (int32_t)c->win[k][1] * cl;
        }
        /* Q15 kernel, then the Q12 volume, back into int16 range. */
        out[produced * 2] += (int32_t)((l >> 15) * gain_q >> 12);
        out[produced * 2 + 1] += (int32_t)((r >> 15) * gain_q >> 12);

        produced++;
        if (c->fade < c->fade_len) c->fade++;
        c->frac += c->step;
        while (c->frac >= 65536u) {
            c->frac -= 65536u;
            for (int k = 1; k < GARLIC_AUDIO_RES_TAPS; ++k) {
                c->win[k - 1][0] = c->win[k][0];
                c->win[k - 1][1] = c->win[k][1];
            }
            if (!ch_frame(c, &c->win[GARLIC_AUDIO_RES_TAPS - 1][0],
                    &c->win[GARLIC_AUDIO_RES_TAPS - 1][1])) {
                if (!c->loop || !ch_loop_restart(c, c->output_frames + produced)) { c->active = false; break; }
            }
        }
    }
    c->output_frames += produced;
}

static void cpymo_audio_copy_mixed_samples_unlocked(void *dst, size_t len, cpymo_audio_system *s)
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

        /* Ordinary music needs no envelope processing. Keep that common path
           to a peak scan and one linear scale, with the same rounding. */
        float master = (float)garlic_mixer_output_percent / 100.0f;
        bool linear = garlic_limiter_gain == 1.0f && garlic_limiter_hold == 0;
        if (linear) for (size_t i = 0; i < n * 2; ++i)
            if (chunk[i] < -32767 || chunk[i] > 32767) { linear = false; break; }
        if (linear) {
            for (size_t i = 0; i < n * 2; ++i)
                out[done * 2 + i] = (int16_t)((float)chunk[i] * master);
            done += n;
            continue;
        }

        /* Leave ordinary music linear. Only a true sum above full scale
           engages the linked stereo limiter. Immediate peak protection and
           an 80 ms release preserve the existing output ceiling without the
           old always-on -3 dB soft-knee distortion or per-sample expf(). */
        for (size_t i = 0; i < n; ++i) {
            float left = (float)chunk[i * 2], right = (float)chunk[i * 2 + 1];
            float peak = fabsf(left), right_peak = fabsf(right);
            if (right_peak > peak) peak = right_peak;
            float target = peak > 32767.0f ? 32767.0f / peak : 1.0f;
            if (target < garlic_limiter_gain) {
                garlic_limiter_gain = target;
                garlic_limiter_hold = 240;
            } else if (peak > 32767.0f) garlic_limiter_hold = 240;
            else if (garlic_limiter_hold) --garlic_limiter_hold;
            else garlic_limiter_gain += (1.0f - garlic_limiter_gain) * (1.0f / 3840.0f);
            if (!garlic_limiter_hold && garlic_limiter_gain > .9999f) garlic_limiter_gain = 1.0f;
            float gain = garlic_limiter_gain * master;
            out[(done + i) * 2] = (int16_t)(left * gain);
            out[(done + i) * 2 + 1] = (int16_t)(right * gain);
        }
        done += n;
    }
}

static bool cpymo_audio_channel_get_samples_unlocked(
    void **samples, size_t *len, size_t cid, cpymo_audio_system *s)
{
    (void)s; (void)cid;
    *samples = NULL;
    *len = 0;
    return false;
}

/* --- engine API ------------------------------------------------------- */

static void cpymo_audio_init_unlocked(cpymo_audio_system *s)
{
    if (garlic_audio_ready) cpymo_audio_free_unlocked(s);
    for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
        garlic_mp3_channel *c = &garlic_ch[i];
        memset(c, 0, sizeof(*c));
        c->volume = 1.0f;
    }
    garlic_limiter_gain = 1.0f;
    garlic_limiter_hold = 0;
    garlic_audio_ready = 1;
}

static void cpymo_audio_free_unlocked(cpymo_audio_system *s)
{
    (void)s;
    for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
        ch_stop(&garlic_ch[i]);
        free(garlic_ch[i].name);
        garlic_ch[i].name = NULL;
        free(garlic_ch[i].ring);
        garlic_ch[i].ring = NULL;
        free(garlic_ch[i].res_tab);
        garlic_ch[i].res_tab = NULL;
        garlic_ch[i].res_step = 0;
    }
    garlic_audio_ready = 0;
}

static bool cpymo_audio_enabled_unlocked(struct cpymo_engine *e)
{
    (void)e;
    return garlic_audio_ready != 0;
}

static float cpymo_audio_get_channel_volume_unlocked(
    size_t cid, const cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS ? garlic_ch[cid].volume : 0.0f;
}

static void cpymo_audio_set_channel_volume_unlocked(
    size_t cid, cpymo_audio_system *s, float volume)
{
    (void)s;
    if (cid >= CPYMO_AUDIO_MAX_CHANNELS) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    garlic_ch[cid].volume = volume;
}

static bool cpymo_audio_channel_is_playing_unlocked(size_t cid, cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS && garlic_ch[cid].active;
}

static bool cpymo_audio_channel_is_looping_unlocked(size_t cid, cpymo_audio_system *s)
{
    (void)s;
    return cid < CPYMO_AUDIO_MAX_CHANNELS && garlic_ch[cid].active &&
        garlic_ch[cid].loop;
}

static bool cpymo_audio_wait_se_unlocked(struct cpymo_engine *e, float d)
{
    (void)d;
    /* B must be able to cut a long SE short, as in the original engine. */
    if (cpymo_input_foward_key_just_pressed(e)) return true;
    return !garlic_ch[CPYMO_AUDIO_CHANNEL_SE].active;
}

static error_t cpymo_audio_bgm_play_unlocked(cpymo_engine *e, cpymo_str name, bool loop)
{
    char *path = NULL;
    error_t err = cpymo_assetloader_get_bgm_path(&path, name, &e->assetloader);
    if (err != CPYMO_ERR_SUCC) return err;
    error_t played = ch_play(CPYMO_AUDIO_CHANNEL_BGM, path, NULL, name, loop);
    ch_trace_bgm(played == CPYMO_ERR_SUCC ? "play" : "failed",
        &garlic_ch[CPYMO_AUDIO_CHANNEL_BGM], 0, e);
    return played;
}

static error_t cpymo_audio_se_play_unlocked(cpymo_engine *e, cpymo_str name, bool loop)
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

static error_t cpymo_audio_vo_play_unlocked(cpymo_engine *e, cpymo_str name)
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

static void cpymo_audio_bgm_stop_unlocked(struct cpymo_engine *e)
{
    (void)e;
    ch_trace_bgm("stop", &garlic_ch[CPYMO_AUDIO_CHANNEL_BGM],
        garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].output_frames, e);
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_BGM]);
}

static void cpymo_audio_se_stop_unlocked(struct cpymo_engine *e)
{
    (void)e;
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_SE]);
}

static void cpymo_audio_vo_stop_unlocked(struct cpymo_engine *e)
{
    (void)e;
    ch_stop(&garlic_ch[CPYMO_AUDIO_CHANNEL_VO]);
}

static const char * cpymo_audio_get_bgm_name_unlocked(struct cpymo_engine *e)
{
    (void)e;
    return garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].active && garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].name
        ? garlic_ch[CPYMO_AUDIO_CHANNEL_BGM].name : "";
}

static const char * cpymo_audio_get_se_name_unlocked(struct cpymo_engine *e)
{
    (void)e;
    /* The legacy save slot stores only a persistent, looping SE name; load
       restores that field with loop=true. Never serialize a one-shot (even
       while it is playing) or a retained name after EOF/stop. This matches
       the generic backend without changing old slot layouts. */
    garlic_mp3_channel *c = &garlic_ch[CPYMO_AUDIO_CHANNEL_SE];
    return c->active && c->loop && c->name ? c->name : "";
}

static error_t cpymo_audio_play_video_unlocked(struct cpymo_engine *e, const char *path)
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

/* Frontend callbacks may run while the main thread changes channels or frees
   an engine. Only audio state is locked; menu layout/rendering never owns it. */
void garlic_audio_set_output_percent(unsigned percent)
{
    garlic_audio_lock();
    garlic_mixer_output_percent = percent > 100 ? 100 : percent;
    garlic_audio_unlock();
}

void cpymo_audio_copy_mixed_samples(void *dst, size_t len, cpymo_audio_system *s)
{
    garlic_audio_lock();
    cpymo_audio_copy_mixed_samples_unlocked(dst, len, s);
    garlic_audio_unlock();
}

bool cpymo_audio_channel_get_samples(
    void **samples, size_t *len, size_t cid, cpymo_audio_system *s)
{
    garlic_audio_lock();
    bool result = cpymo_audio_channel_get_samples_unlocked(samples, len, cid, s);
    garlic_audio_unlock();
    return result;
}

void cpymo_audio_init(cpymo_audio_system *s)
{
    garlic_audio_lock();
    cpymo_audio_init_unlocked(s);
    garlic_audio_unlock();
}

void cpymo_audio_free(cpymo_audio_system *s)
{
    garlic_audio_lock();
    cpymo_audio_free_unlocked(s);
    garlic_audio_unlock();
}

bool cpymo_audio_enabled(struct cpymo_engine *e)
{
    garlic_audio_lock();
    bool result = cpymo_audio_enabled_unlocked(e);
    garlic_audio_unlock();
    return result;
}

float cpymo_audio_get_channel_volume(
    size_t cid, const cpymo_audio_system *s)
{
    garlic_audio_lock();
    float result = cpymo_audio_get_channel_volume_unlocked(cid, s);
    garlic_audio_unlock();
    return result;
}

void cpymo_audio_set_channel_volume(
    size_t cid, cpymo_audio_system *s, float volume)
{
    garlic_audio_lock();
    cpymo_audio_set_channel_volume_unlocked(cid, s, volume);
    garlic_audio_unlock();
}

bool cpymo_audio_channel_is_playing(size_t cid, cpymo_audio_system *s)
{
    garlic_audio_lock();
    bool result = cpymo_audio_channel_is_playing_unlocked(cid, s);
    garlic_audio_unlock();
    return result;
}

bool cpymo_audio_channel_is_looping(size_t cid, cpymo_audio_system *s)
{
    garlic_audio_lock();
    bool result = cpymo_audio_channel_is_looping_unlocked(cid, s);
    garlic_audio_unlock();
    return result;
}

bool cpymo_audio_wait_se(struct cpymo_engine *e, float d)
{
    garlic_audio_lock();
    bool result = cpymo_audio_wait_se_unlocked(e, d);
    garlic_audio_unlock();
    return result;
}

error_t cpymo_audio_bgm_play(cpymo_engine *e, cpymo_str name, bool loop)
{
    garlic_audio_lock();
    error_t result = cpymo_audio_bgm_play_unlocked(e, name, loop);
    garlic_audio_unlock();
    return result;
}

error_t cpymo_audio_se_play(cpymo_engine *e, cpymo_str name, bool loop)
{
    garlic_audio_lock();
    error_t result = cpymo_audio_se_play_unlocked(e, name, loop);
    garlic_audio_unlock();
    return result;
}

error_t cpymo_audio_vo_play(cpymo_engine *e, cpymo_str name)
{
    garlic_audio_lock();
    error_t result = cpymo_audio_vo_play_unlocked(e, name);
    garlic_audio_unlock();
    return result;
}

void cpymo_audio_bgm_stop(struct cpymo_engine *e)
{
    garlic_audio_lock();
    cpymo_audio_bgm_stop_unlocked(e);
    garlic_audio_unlock();
}

void cpymo_audio_se_stop(struct cpymo_engine *e)
{
    garlic_audio_lock();
    cpymo_audio_se_stop_unlocked(e);
    garlic_audio_unlock();
}

void cpymo_audio_vo_stop(struct cpymo_engine *e)
{
    garlic_audio_lock();
    cpymo_audio_vo_stop_unlocked(e);
    garlic_audio_unlock();
}

const char * cpymo_audio_get_bgm_name(struct cpymo_engine *e)
{
    garlic_audio_lock();
    const char * result = cpymo_audio_get_bgm_name_unlocked(e);
    garlic_audio_unlock();
    return result;
}

const char * cpymo_audio_get_se_name(struct cpymo_engine *e)
{
    garlic_audio_lock();
    const char * result = cpymo_audio_get_se_name_unlocked(e);
    garlic_audio_unlock();
    return result;
}

error_t cpymo_audio_play_video(struct cpymo_engine *e, const char *path)
{
    garlic_audio_lock();
    error_t result = cpymo_audio_play_video_unlocked(e, path);
    garlic_audio_unlock();
    return result;
}

#endif
