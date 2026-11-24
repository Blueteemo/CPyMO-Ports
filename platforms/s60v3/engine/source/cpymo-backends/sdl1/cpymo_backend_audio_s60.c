/* CPyMO S60 shared-resource audio backend.
 * MP3: fixed-point libmad (GPL-2.0-or-later).
 * Output: SDL 1.2 Symbian PCM stream; BGM, SE and voice are mixed here.
 * The decoder never writes or extracts game assets. Each PAK channel owns a
 * separate read-only FILE so engine teardown can safely close assetloader.
 */
#include "../../cpymo/cpymo_prelude.h"
#if defined(CPYMO_S60_AUDIO) && !defined(DISABLE_AUDIO)
#include <SDL/SDL.h>
#include <stdio.h>
#include "cpymo_s60_diagnostics.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_audio.h"
#include "../include/cpymo_backend_audio.h"

#ifndef FPM_ARM
#define FPM_DEFAULT
#endif
#include "../../thirdparty/libmad/fixed.h"
#include "../../thirdparty/libmad/frame.h"
#include "../../thirdparty/libmad/synth.h"

#define MP3_INPUT_BYTES 8192

#ifndef CPYMO_S60_AUDIO_RATE
#define CPYMO_S60_AUDIO_RATE 22050
#endif
#ifndef CPYMO_S60_AUDIO_SAMPLES
#define CPYMO_S60_AUDIO_SAMPLES 2048
#endif

/* All fields are protected by SDL's audio lock, except before publication. */
typedef struct {
    FILE *file;
    size_t base, length, current;
} s60_reader;

typedef struct {
    s60_reader reader;
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    unsigned char input[MP3_INPUT_BYTES + MAD_BUFFER_GUARD];
    size_t input_size, mp3_start;
    int input_eof;
    int decoder_open;
    int kind; /* 1 MP3, 2 PCM WAV */
    int rate, channels, bits;
    size_t wave_start, wave_end;
    short pcm[2304];
    size_t pcm_count, pcm_pos;
    int sample_a[2], sample_b[2];
    unsigned phase;
    int primed, playing, loop;
    unsigned long output_frames, loops;
    char *name;
} s60_channel;

typedef struct {
    s60_channel *channel[CPYMO_AUDIO_MAX_CHANNELS];
    unsigned volume[CPYMO_AUDIO_MAX_CHANNELS]; /* fixed point / 256 */
} s60_audio;

static int device_enabled;
static cpymo_backend_audio_info output_info;
static s60_audio *active_audio;
static unsigned long callback_frames, concurrent_frames, triple_frames, nonzero_frames;

static size_t read_cb(void *dst, size_t size, void *user)
{
    s60_reader *r = (s60_reader *)user;
    if (!r->file || r->current >= r->length) return 0;
    if (size > r->length-r->current) size = r->length-r->current;
    if (fseek(r->file, (long)(r->base+r->current), SEEK_SET)) return 0;
    size = fread(dst, 1, size, r->file);
    r->current += size;
    return size;
}

static int seek_cb(size_t pos, void *user)
{
    s60_reader *r = (s60_reader *)user;
    if (pos > r->length) return -1;
    r->current = (size_t)pos;
    return 0;
}

static unsigned le16(const unsigned char *b) { return b[0] | ((unsigned)b[1]<<8); }
static uint32_t le32(const unsigned char *b) { return le16(b) | ((uint32_t)le16(b+2)<<16); }

static void close_channel(s60_channel *c)
{
    if (!c) return;
    if (c->decoder_open) {
        mad_frame_finish(&c->frame);
        mad_stream_finish(&c->stream);
    }
    if (c->reader.file) fclose(c->reader.file);
    free(c->name);
    free(c);
}

static int open_wave(s60_channel *c)
{
    unsigned char h[40];
    int fmt=0;
    if (seek_cb(12,&c->reader)) return -1;
    while (read_cb(h,8,&c->reader)==8) {
        size_t size = le32(h+4), start = c->reader.current;
        if (size > c->reader.length-start) return -1;
        if (!memcmp(h,"fmt ",4)) {
            if (size<16 || read_cb(h,16,&c->reader)!=16 || le16(h)!=1) return -1;
            c->channels=(int)le16(h+2); c->rate=(int)le32(h+4); c->bits=(int)le16(h+14);
            if ((c->channels!=1 && c->channels!=2) || (c->bits!=8 && c->bits!=16) || c->rate<4000 || c->rate>96000) return -1;
            fmt=1;
        } else if (!memcmp(h,"data",4) && fmt) {
            c->kind=2; c->wave_start=start; c->wave_end=start+size;
            return seek_cb(start,&c->reader);
        }
        if (seek_cb(start+size+(size&1),&c->reader)) return -1;
    }
    return -1;
}

static int reset_mp3(s60_channel *c)
{
    if (c->decoder_open) {
        mad_frame_finish(&c->frame);
        mad_stream_finish(&c->stream);
    }
    mad_stream_init(&c->stream);
    mad_frame_init(&c->frame);
    mad_synth_init(&c->synth);
    c->decoder_open = 1;
    c->input_size = 0;
    c->input_eof = 0;
    return seek_cb(c->mp3_start, &c->reader);
}

static int refill_mp3_input(s60_channel *c)
{
    size_t remain = 0;
    if (c->input_eof) return 0;
    const unsigned char *next = c->stream.next_frame;
    if (next && next >= c->input && next <= c->input + c->input_size) {
        remain = c->input_size - (size_t)(next - c->input);
        memmove(c->input, next, remain);
    }
    if (remain == MP3_INPUT_BYTES) return 0;
    size_t count = read_cb(c->input + remain, MP3_INPUT_BYTES - remain, &c->reader);
    if (!remain && !count) return 0;
    c->input_size = remain + count;
    c->input_eof = count < MP3_INPUT_BYTES - remain || c->reader.current == c->reader.length;
    if (c->input_eof) memset(c->input + c->input_size, 0, MAD_BUFFER_GUARD);
    mad_stream_buffer(&c->stream, c->input, c->input_size + (c->input_eof ? MAD_BUFFER_GUARD : 0));
    return 1;
}

static short mad_pcm16(mad_fixed_t sample)
{
    if (sample >= MAD_F_ONE) return 32767;
    if (sample <= -MAD_F_ONE) return -32768;
    return (short)(sample >> (MAD_F_FRACBITS - 15));
}

static size_t decode_mp3(s60_channel *c)
{
    if (!c->stream.buffer && !refill_mp3_input(c)) return 0;
    /* Bound resynchronization work for corrupt input in the audio callback. */
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        if (mad_frame_decode(&c->frame, &c->stream) == 0) {
            if (c->frame.header.samplerate == output_info.freq * 2)
                c->frame.options |= MAD_OPTION_HALFSAMPLERATE;
            mad_synth_frame(&c->synth, &c->frame);
            const struct mad_pcm *pcm = &c->synth.pcm;
            if (!pcm->length || pcm->channels < 1 || pcm->channels > 2) return 0;
            if (c->rate && (c->rate != (int)pcm->samplerate || c->channels != pcm->channels)) return 0;
            c->rate = (int)pcm->samplerate;
            c->channels = pcm->channels;
            for (unsigned i = 0; i < pcm->length; ++i) {
                c->pcm[i * c->channels] = mad_pcm16(pcm->samples[0][i]);
                if (c->channels == 2) c->pcm[2 * i + 1] = mad_pcm16(pcm->samples[1][i]);
            }
            return pcm->length * c->channels;
        }
        if (c->stream.error == MAD_ERROR_BUFLEN) {
            if (!refill_mp3_input(c)) return 0;
        } else if (!MAD_RECOVERABLE(c->stream.error)) return 0;
    }
    return 0;
}

static int open_decoder(s60_channel *c)
{
    unsigned char h[12];
    if (read_cb(h, 12, &c->reader) < 12 || seek_cb(0, &c->reader)) return -1;
    if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) return open_wave(c);
    if (!memcmp(h, "ID3", 3) && !(0x80 & (h[6] | h[7] | h[8] | h[9]))) {
        c->mp3_start = 10 + ((size_t)h[6] << 21) + ((size_t)h[7] << 14) +
            ((size_t)h[8] << 7) + h[9];
        if (h[3] == 4 && (h[5] & 0x10)) c->mp3_start += 10;
    }
    c->kind = 1;
    if (reset_mp3(c)) return -1;
    c->pcm_count = decode_mp3(c);
    return c->pcm_count ? 0 : -1;
}

static size_t refill(s60_channel *c)
{
    if (c->kind==1) return decode_mp3(c);
    if (c->kind==2) {
        unsigned char bytes[4096];
        size_t remain=c->reader.current<c->wave_end ? c->wave_end-c->reader.current : 0;
        size_t count=sizeof(c->pcm)/sizeof(c->pcm[0]);
        size_t bytes_count=count*(unsigned)c->bits/8;
        size_t frame_bytes=(unsigned)c->channels*(unsigned)c->bits/8;
        size_t i;
        if (bytes_count>remain) bytes_count=remain;
        bytes_count-=bytes_count%frame_bytes;
        bytes_count=read_cb(bytes,bytes_count,&c->reader);
        count=bytes_count*8/(unsigned)c->bits;
        for(i=0;i<count;++i) c->pcm[i]=(short)(c->bits==16 ? (int16_t)le16(bytes+2*i) : ((int)bytes[i]-128)*256);
        return count;
    }
    return 0;
}

static int restart_channel(s60_channel *c)
{
    c->pcm_count=0; c->pcm_pos=0;
    if(c->kind==1) {
        if(reset_mp3(c)) return 0;
    } else if(seek_cb(c->wave_start,&c->reader)) return 0;
    ++c->loops;
    return 1;
}

static int source_frame(s60_channel *c, int frame[2])
{
    if(c->pcm_pos+(size_t)c->channels>c->pcm_count) {
        c->pcm_count=refill(c); c->pcm_pos=0;
        if(c->pcm_count<(size_t)c->channels && c->loop && restart_channel(c)) c->pcm_count=refill(c);
        if(c->pcm_count<(size_t)c->channels) return 0;
    }
    frame[0]=c->pcm[c->pcm_pos++];
    frame[1]=c->channels==2 ? c->pcm[c->pcm_pos++] : frame[0];
    return 1;
}

static int render_frame(s60_channel *c, int frame[2])
{
    unsigned rate=(unsigned)output_info.freq;
    if(!c->playing) return 0;
    if(!c->primed) {
        if(!source_frame(c,c->sample_a)) { c->playing=0; return 0; }
        if(!source_frame(c,c->sample_b)) { c->sample_b[0]=c->sample_a[0]; c->sample_b[1]=c->sample_a[1]; }
        c->primed=1;
    }
    frame[0]=c->sample_a[0];
    frame[1]=c->sample_a[1];
    if (c->phase) {
        int fraction = (int)(c->phase * 256 / rate);
        frame[0] += ((c->sample_b[0] - frame[0]) * fraction) / 256;
        frame[1] += ((c->sample_b[1] - frame[1]) * fraction) / 256;
    }
    c->phase+=(unsigned)c->rate;
    while(c->phase>=rate) {
        c->phase-=rate; c->sample_a[0]=c->sample_b[0]; c->sample_a[1]=c->sample_b[1];
        if(!source_frame(c,c->sample_b)) { c->playing=0; break; }
    }
    ++c->output_frames;
    return 1;
}

static void mix_pcm(void *dst, size_t bytes)
{
    short *out=(short *)dst;
    size_t n, frames=bytes/(sizeof(short)*output_info.channels);
    memset(dst,0,bytes);
    if(!active_audio) return;
    for(n=0;n<frames;++n) {
        int32_t left=0,right=0;
        unsigned cid,playing=0;
        for(cid=0;cid<CPYMO_AUDIO_MAX_CHANNELS;++cid) {
            s60_channel *c=active_audio->channel[cid];
            int frame[2];
            if(c && render_frame(c,frame)) {
                left+=(frame[0]*(int)active_audio->volume[cid])/256;
                right+=(frame[1]*(int)active_audio->volume[cid])/256;
                ++playing;
            }
        }
        ++callback_frames;
        if(playing>1) ++concurrent_frames;
        if(playing==3) ++triple_frames;
        if(output_info.channels==1) left=(left+right)/2;
        if(left>32767)left=32767; if(left<-32768)left=-32768;
        if(right>32767)right=32767; if(right<-32768)right=-32768;
        if(left || right)++nonzero_frames;
        *out++=(short)left;
        if(output_info.channels==2)*out++=(short)right;
    }
}

static void SDLCALL audio_callback(void *unused, Uint8 *stream, int len)
{
    (void)unused;
    mix_pcm(stream,(size_t)len);
}

void cpymo_backend_audio_init(void)
{
    SDL_AudioSpec want,got;
    if(device_enabled) return;
    memset(&want,0,sizeof(want)); memset(&got,0,sizeof(got));
    want.freq=CPYMO_S60_AUDIO_RATE; want.format=AUDIO_S16SYS;
    want.channels=1; want.samples=CPYMO_S60_AUDIO_SAMPLES;
    want.callback=audio_callback;
    if(SDL_OpenAudio(&want,&got)<0) { printf("[S60 audio] SDL_OpenAudio failed: %s\n",SDL_GetError()); return; }
    if(got.format!=AUDIO_S16SYS || (got.channels!=1 && got.channels!=2) || got.freq<4000) {
        printf("[S60 audio] unsupported PCM output spec\n"); SDL_CloseAudio(); return;
    }
    output_info.freq=(size_t)got.freq; output_info.channels=(size_t)got.channels; output_info.format=cpymo_backend_audio_s16;
    device_enabled=1;
    CPYMO_S60_TRACE("[S60 audio] fixed-point libmad; SDL PCM opened: %d Hz, %d channel(s), %d samples\n",got.freq,got.channels,got.samples);
    SDL_PauseAudio(0);
}

void cpymo_backend_audio_free(void)
{
    if(device_enabled) {
        SDL_PauseAudio(1);
        SDL_LockAudio(); active_audio=NULL; SDL_UnlockAudio();
        SDL_CloseAudio(); device_enabled=0;
        CPYMO_S60_TRACE("[S60 audio] output closed: frames=%lu nonzero=%lu concurrent=%lu triple=%lu\n",callback_frames,nonzero_frames,concurrent_frames,triple_frames);
    }
}

void cpymo_s60_movie_resume_audio(cpymo_engine *e)
{
    cpymo_backend_audio_init();
    cpymo_backend_audio_lock();
    active_audio = (s60_audio *)e->audio;
    cpymo_backend_audio_unlock();
}

const cpymo_backend_audio_info *cpymo_backend_audio_get_info(void) { return device_enabled?&output_info:NULL; }
void cpymo_backend_audio_lock(void) { if(device_enabled)SDL_LockAudio(); }
void cpymo_backend_audio_unlock(void) { if(device_enabled)SDL_UnlockAudio(); }

void cpymo_audio_init(cpymo_audio_system *s)
{
    s60_audio *a=(s60_audio *)calloc(1,sizeof(s60_audio));
    unsigned cid;
    *s=a;
    if(!a) return;
    for(cid=0;cid<CPYMO_AUDIO_MAX_CHANNELS;++cid)a->volume[cid]=256;
    cpymo_backend_audio_lock(); active_audio=a; cpymo_backend_audio_unlock();
}
void cpymo_audio_free(cpymo_audio_system *s)
{
    s60_audio *a=(s60_audio *)*s;
    unsigned cid;
    if(!a) return;
    cpymo_backend_audio_lock();
    if(active_audio==a)active_audio=NULL;
    for(cid=0;cid<CPYMO_AUDIO_MAX_CHANNELS;++cid)close_channel(a->channel[cid]);
    free(a); *s=NULL;
    cpymo_backend_audio_unlock();
    CPYMO_S60_TRACE("[S60 audio] game audio released\n");
}
float cpymo_audio_get_channel_volume(size_t cid,const cpymo_audio_system *s)
{
    s60_audio *a=(s60_audio *)*s;
    return a && cid<CPYMO_AUDIO_MAX_CHANNELS ? (float)a->volume[cid]/256.0f : 0;
}
void cpymo_audio_set_channel_volume(size_t cid,cpymo_audio_system *s,float vol)
{
    s60_audio *a=(s60_audio *)*s;
    if(!a || cid>=CPYMO_AUDIO_MAX_CHANNELS)return;
    if(vol<0)vol=0; if(vol>1)vol=1;
    cpymo_backend_audio_lock(); a->volume[cid]=(unsigned)(vol*256.0f); cpymo_backend_audio_unlock();
}
static void stop_channel(cpymo_audio_system *s,unsigned cid)
{
    s60_audio *a=(s60_audio *)*s;
    s60_channel *c;
    if(!a)return;
    cpymo_backend_audio_lock(); c=a->channel[cid]; a->channel[cid]=NULL;
    if(c)CPYMO_S60_TRACE("[S60 audio] channel %u stopped: %s frames=%lu loops=%lu\n",cid,c->name?c->name:"",c->output_frames,c->loops);
    close_channel(c); cpymo_backend_audio_unlock();
}
bool cpymo_audio_channel_is_playing(size_t cid,cpymo_audio_system *s)
{
    s60_audio *a=(s60_audio *)*s; bool result=false;
    if(!a || cid>=CPYMO_AUDIO_MAX_CHANNELS)return false;
    cpymo_backend_audio_lock(); result=a->channel[cid] && a->channel[cid]->playing; cpymo_backend_audio_unlock(); return result;
}
bool cpymo_audio_channel_is_looping(size_t cid,cpymo_audio_system *s)
{
    s60_audio *a=(s60_audio *)*s; bool result=false;
    if(!a || cid>=CPYMO_AUDIO_MAX_CHANNELS)return false;
    cpymo_backend_audio_lock(); result=a->channel[cid] && a->channel[cid]->loop; cpymo_backend_audio_unlock(); return result;
}
bool cpymo_audio_enabled(cpymo_engine *e) { return device_enabled && e->audio!=NULL; }
bool cpymo_audio_wait_se(cpymo_engine *e,float dt)
{
    (void)dt;
    if(cpymo_audio_channel_is_looping(CPYMO_AUDIO_CHANNEL_SE,&e->audio)) { printf("[S60 audio] cannot wait for looping SE\n"); return true; }
    return !cpymo_audio_channel_is_playing(CPYMO_AUDIO_CHANNEL_SE,&e->audio);
}

typedef error_t (*path_getter)(char **,cpymo_str,const cpymo_assetloader *);
static error_t play(cpymo_engine *e,cpymo_str name,unsigned cid,int loop)
{
    s60_audio *a=(s60_audio *)e->audio;
    s60_channel *c;
    cpymo_package *pak=NULL;
    cpymo_package_index index;
    char *path=NULL;
    const char *pak_suffix=NULL;
    error_t err;
    if(!device_enabled || !a)return CPYMO_ERR_UNSUPPORTED;
    if(!name.len) { stop_channel(&e->audio,cid); return CPYMO_ERR_SUCC; }
    c=(s60_channel *)calloc(1,sizeof(s60_channel));
    if(!c)return CPYMO_ERR_OUT_OF_MEM;
    c->name=(char *)malloc(name.len+1);
    if(!c->name) { close_channel(c); return CPYMO_ERR_OUT_OF_MEM; }
    cpymo_str_copy(c->name,name.len+1,name);
    if(cid==CPYMO_AUDIO_CHANNEL_SE && e->assetloader.use_pkg_se) { pak=&e->assetloader.pkg_se; pak_suffix="/se/se.pak"; }
    if(cid==CPYMO_AUDIO_CHANNEL_VO && e->assetloader.use_pkg_voice) { pak=&e->assetloader.pkg_voice; pak_suffix="/voice/voice.pak"; }
    if(pak) {
        err=cpymo_package_find(&index,pak,name);
        if(err!=CPYMO_ERR_SUCC) { close_channel(c); return err; }
        path=(char *)malloc(strlen(e->assetloader.gamedir)+strlen(pak_suffix)+1);
        if(!path) { close_channel(c); return CPYMO_ERR_OUT_OF_MEM; }
        strcpy(path,e->assetloader.gamedir); strcat(path,pak_suffix);
        c->reader.file=fopen(path,"rb"); c->reader.base=index.file_offset; c->reader.length=index.file_length;
    } else {
        path_getter get=cid==CPYMO_AUDIO_CHANNEL_BGM ? cpymo_assetloader_get_bgm_path : cid==CPYMO_AUDIO_CHANNEL_SE ? cpymo_assetloader_get_se_path : cpymo_assetloader_get_vo_path;
        err=get(&path,name,&e->assetloader);
        if(err!=CPYMO_ERR_SUCC) { close_channel(c); return err; }
        c->reader.file=fopen(path,"rb");
        if(c->reader.file) { fseek(c->reader.file,0,SEEK_END); c->reader.length=(size_t)ftell(c->reader.file); }
    }
    free(path);
    if(!c->reader.file) { close_channel(c); return CPYMO_ERR_CAN_NOT_OPEN_FILE; }
    if(open_decoder(c)) { printf("[S60 audio] decode open failed: channel=%u %s\n",cid,c->name); close_channel(c); return CPYMO_ERR_BAD_FILE_FORMAT; }
    c->loop=loop; c->playing=1;
    CPYMO_S60_TRACE("[S60 audio] channel %u play %s: %d Hz %d ch, %lu bytes, pak=%d loop=%d\n",cid,c->name,c->rate,c->channels,(unsigned long)c->reader.length,pak!=NULL,loop);
    cpymo_backend_audio_lock();
    close_channel(a->channel[cid]); a->channel[cid]=c; active_audio=a;
    cpymo_backend_audio_unlock();
    return CPYMO_ERR_SUCC;
}
error_t cpymo_audio_bgm_play(cpymo_engine *e,cpymo_str n,bool loop) { return play(e,n,CPYMO_AUDIO_CHANNEL_BGM,loop); }
void cpymo_audio_bgm_stop(cpymo_engine *e) { stop_channel(&e->audio,CPYMO_AUDIO_CHANNEL_BGM); }
error_t cpymo_audio_se_play(cpymo_engine *e,cpymo_str n,bool loop) { return play(e,n,CPYMO_AUDIO_CHANNEL_SE,loop); }
void cpymo_audio_se_stop(cpymo_engine *e) { stop_channel(&e->audio,CPYMO_AUDIO_CHANNEL_SE); }
error_t cpymo_audio_vo_play(cpymo_engine *e,cpymo_str n) { return play(e,n,CPYMO_AUDIO_CHANNEL_VO,0); }
void cpymo_audio_vo_stop(cpymo_engine *e) { stop_channel(&e->audio,CPYMO_AUDIO_CHANNEL_VO); }
static const char *channel_name(cpymo_engine *e,unsigned cid)
{
    s60_audio *a=(s60_audio *)e->audio;
    return a && a->channel[cid] && a->channel[cid]->loop ? a->channel[cid]->name : "";
}
const char *cpymo_audio_get_bgm_name(cpymo_engine *e) { return channel_name(e,CPYMO_AUDIO_CHANNEL_BGM); }
const char *cpymo_audio_get_se_name(cpymo_engine *e) { return channel_name(e,CPYMO_AUDIO_CHANNEL_SE); }
error_t cpymo_audio_play_video(cpymo_engine *e,const char *p) { (void)e;(void)p; return CPYMO_ERR_UNSUPPORTED; }
void cpymo_audio_copy_mixed_samples(void *dst,size_t bytes,cpymo_audio_system *s) { (void)s; mix_pcm(dst,bytes); }
bool cpymo_audio_channel_get_samples(void **samples,size_t *len,size_t cid,cpymo_audio_system *s)
{ (void)s;(void)cid;*samples=NULL;*len=0;return false; /* custom mixer owns the SDL callback */ }
#endif
