/* RG35XX Stock CFW, SDL 1.2 fallback audio (bounded PCM WAV only).
 * Opt-in: -DCPYMO_RG35XX_WAV_AUDIO -DDISABLE_FFMPEG_AUDIO
 * Audio files MUST be uncompressed PCM WAV, 22050 Hz, stereo, signed 16-bit.
 * No MP3 decoding; requires an offline conversion of PyMO sound assets.
 */
#if defined(CPYMO_RG35XX_WAV_AUDIO) && defined(DISABLE_AUDIO)
#error "PCM audio cannot be enabled with DISABLE_AUDIO"
#endif
#ifdef CPYMO_RG35XX_WAV_AUDIO
#include "../../cpymo/cpymo_engine.h"
#include "../../cpymo/cpymo_assetloader.h"
#include "../../cpymo/cpymo_str.h"
#include "../include/cpymo_backend_audio.h"
#include <SDL/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RG_WAV_FREQ 22050
#define RG_WAV_CHANNELS 2
#define RG_WAV_SAMPLES 2048
#define RG_WAV_FILE_MAX (32u * 1024u * 1024u)

typedef struct {
    Uint8 *data;
    Uint32 length, offset;
    int loop;
    float volume;
    char *name;
} rg_wav_channel;
static rg_wav_channel ch[CPYMO_AUDIO_MAX_CHANNELS];
static int ready;
static const cpymo_backend_audio_info rg_info = {RG_WAV_FREQ,cpymo_backend_audio_s16,RG_WAV_CHANNELS};
static void lock_audio(void) { if (ready) SDL_LockAudio(); }
static void unlock_audio(void) { if (ready) SDL_UnlockAudio(); }

static void audio_callback(void *unused, Uint8 *out, int len)
{
    (void)unused;
    if (len <= 0) return;
    memset(out, 0, (size_t)len);
    for (int i = 0; i < CPYMO_AUDIO_MAX_CHANNELS; ++i) {
        rg_wav_channel *c = &ch[i];
        if (!c->data || c->length < 4 || c->volume <= 0) {
            /* Silent tracks must advance too, otherwise wait/loop semantics stall. */
            if (!c->data || c->length < 4) continue;
        }
        int written = 0;
        while (written < len && c->data) {
            if (c->offset >= c->length) {
                if (c->loop) c->offset = 0;
                else break;
            }
            Uint32 left = c->length - c->offset;
            Uint32 amount = (Uint32)(len-written) < left ? (Uint32)(len-written) : left;
            amount &= ~3u; /* 16-bit stereo: never split a sample frame. */
            if (!amount) break;
            if (c->volume > 0) {
                int vol = (int)(c->volume * SDL_MIX_MAXVOLUME);
                if (vol > SDL_MIX_MAXVOLUME) vol = SDL_MIX_MAXVOLUME;
                if (vol > 0) SDL_MixAudio(out+written, c->data+c->offset, amount, vol);
            }
            c->offset += amount;
            written += (int)amount;
        }
    }
}

static void channel_release(rg_wav_channel *c)
{
    if (c->data) SDL_FreeWAV(c->data);
    free(c->name);
    memset(c,0,sizeof(*c));
    c->volume = 1.0f;
}
static int playable_spec(const SDL_AudioSpec *s, Uint32 size)
{
    return s->freq == RG_WAV_FREQ && s->format == AUDIO_S16SYS &&
      s->channels == RG_WAV_CHANNELS && size >= 4 && size % 4 == 0 &&
      size <= RG_WAV_FILE_MAX;
}

static error_t play_path(size_t cid, char *path, cpymo_str name, bool loop)
{
    if (!path) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    if (!ready) { free(path); return CPYMO_ERR_UNSUPPORTED; }
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < 44 ||
        (uint64_t)st.st_size > RG_WAV_FILE_MAX) {
        free(path); return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    }
    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 size = 0;
    SDL_AudioSpec *loaded = SDL_LoadWAV(path, &spec, &data, &size);
    free(path);
    if (!loaded) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
    if (!playable_spec(&spec, size)) {
        SDL_FreeWAV(data);
        return CPYMO_ERR_UNSUPPORTED;
    }
    char *trackname = malloc(name.len+1);
    if (!trackname) { SDL_FreeWAV(data); return CPYMO_ERR_OUT_OF_MEM; }
    cpymo_str_copy(trackname, name.len+1, name);
    lock_audio();
    float oldvol = ch[cid].volume;
    channel_release(&ch[cid]);
    ch[cid].data = data;
    ch[cid].length = size;
    ch[cid].loop = loop ? 1 : 0;
    ch[cid].volume = oldvol;
    ch[cid].name = trackname;
    unlock_audio();
    return CPYMO_ERR_SUCC;
}

void cpymo_audio_init(cpymo_audio_system *sys)
{
    (void)sys;
    if (ready) cpymo_audio_free(sys); /* Re-entry from selector must not leak the old device. */
    memset(ch,0,sizeof(ch));
    for (int i=0;i<CPYMO_AUDIO_MAX_CHANNELS;++i) ch[i].volume=1.0f;
    SDL_AudioSpec want, got;
    memset(&want,0,sizeof(want)); memset(&got,0,sizeof(got));
    want.freq=RG_WAV_FREQ;
    want.format=AUDIO_S16SYS;
    want.channels=RG_WAV_CHANNELS;
    want.samples=RG_WAV_SAMPLES;
    want.callback=audio_callback;
    if (SDL_OpenAudio(&want,&got)!=0) return;
    if (!playable_spec(&got, 4)) { SDL_CloseAudio(); return; }
    ready=1;
    SDL_PauseAudio(0);
}
void cpymo_audio_free(cpymo_audio_system *sys)
{
    (void)sys;
    if (ready) SDL_LockAudio();
    for (int i=0;i<CPYMO_AUDIO_MAX_CHANNELS;++i) channel_release(&ch[i]);
    if (ready) { SDL_UnlockAudio(); SDL_CloseAudio(); }
    ready=0;
}
const cpymo_backend_audio_info *cpymo_backend_audio_get_info(void) {return ready?&rg_info:NULL;}
void cpymo_backend_audio_lock(void){lock_audio();}
void cpymo_backend_audio_unlock(void){unlock_audio();}
void cpymo_backend_audio_init(void) {} /* Initialized after SDL_Init via cpymo_audio_init. */
void cpymo_backend_audio_free(void) { if (ready) cpymo_audio_free(NULL); } /* Startup failure fallback. */
bool cpymo_audio_enabled(struct cpymo_engine *e) {(void)e;return ready!=0;}
float cpymo_audio_get_channel_volume(size_t cid,const cpymo_audio_system *sys)
{(void)sys;return cid < CPYMO_AUDIO_MAX_CHANNELS?ch[cid].volume:0;}
void cpymo_audio_set_channel_volume(size_t cid,cpymo_audio_system *sys,float volume)
{
    (void)sys;
    if(cid >= CPYMO_AUDIO_MAX_CHANNELS) return;
    if(volume<0) volume=0;
    if(volume>1) volume=1;
    lock_audio();ch[cid].volume=volume;unlock_audio();
}
bool cpymo_audio_channel_is_playing(size_t cid,cpymo_audio_system *sys)
{
    (void)sys;
    if(cid >= CPYMO_AUDIO_MAX_CHANNELS) return false;
    lock_audio();
    bool result=ch[cid].data && (ch[cid].loop || ch[cid].offset < ch[cid].length);
    unlock_audio();
    return result;
}
bool cpymo_audio_channel_is_looping(size_t cid,cpymo_audio_system *sys)
{
    (void)sys;
    return cid<CPYMO_AUDIO_MAX_CHANNELS && ch[cid].data && ch[cid].loop;
}
bool cpymo_audio_wait_se(struct cpymo_engine *e,float d)
{(void)d;return !cpymo_audio_channel_is_playing(CPYMO_AUDIO_CHANNEL_SE,&e->audio);}
error_t cpymo_audio_bgm_play(cpymo_engine *e,cpymo_str name,bool loop)
{
    char *path=NULL;
    error_t err=cpymo_assetloader_get_bgm_path(&path,name,&e->assetloader);
    if(err!=CPYMO_ERR_SUCC) return err;
    return play_path(CPYMO_AUDIO_CHANNEL_BGM,path,name,loop);
}
error_t cpymo_audio_se_play(cpymo_engine *e,cpymo_str name,bool loop)
{
    if (e->assetloader.use_pkg_se) return CPYMO_ERR_UNSUPPORTED;
    char *path=NULL;
    error_t err=cpymo_assetloader_get_se_path(&path,name,&e->assetloader);
    if(err!=CPYMO_ERR_SUCC) return err;
    return play_path(CPYMO_AUDIO_CHANNEL_SE,path,name,loop);
}
error_t cpymo_audio_vo_play(cpymo_engine *e,cpymo_str name)
{
    if (e->assetloader.use_pkg_voice) return CPYMO_ERR_UNSUPPORTED;
    char *path=NULL;
    error_t err=cpymo_assetloader_get_vo_path(&path,name,&e->assetloader);
    if(err!=CPYMO_ERR_SUCC) return err;
    return play_path(CPYMO_AUDIO_CHANNEL_VO,path,name,false);
}
static void stop_channel(size_t cid)
{
    lock_audio();
    channel_release(&ch[cid]);
    unlock_audio();
}
void cpymo_audio_bgm_stop(struct cpymo_engine *e){(void)e;stop_channel(CPYMO_AUDIO_CHANNEL_BGM);}
void cpymo_audio_se_stop(struct cpymo_engine *e){(void)e;stop_channel(CPYMO_AUDIO_CHANNEL_SE);}
void cpymo_audio_vo_stop(struct cpymo_engine *e){(void)e;stop_channel(CPYMO_AUDIO_CHANNEL_VO);}
const char *cpymo_audio_get_bgm_name(struct cpymo_engine *e)
{(void)e;return ch[CPYMO_AUDIO_CHANNEL_BGM].name?ch[CPYMO_AUDIO_CHANNEL_BGM].name:"";}
const char *cpymo_audio_get_se_name(struct cpymo_engine *e)
{(void)e;return ch[CPYMO_AUDIO_CHANNEL_SE].name?ch[CPYMO_AUDIO_CHANNEL_SE].name:"";}
error_t cpymo_audio_play_video(struct cpymo_engine *e,const char *path)
{(void)e;(void)path;return CPYMO_ERR_UNSUPPORTED;}
void cpymo_audio_copy_mixed_samples(void *dst,size_t size,cpymo_audio_system *sys)
{(void)sys;memset(dst,0,size);}
bool cpymo_audio_channel_get_samples(void **dst,size_t *size,size_t cid,cpymo_audio_system *sys)
{(void)dst;(void)cid;(void)sys;*size=0;return false;}
#endif
