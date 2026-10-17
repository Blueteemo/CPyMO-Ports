#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL/SDL.h>
#include "../source/CPyMO-main/cpymo/cpymo_engine.h"

void cpymo_backend_audio_free(void);
static SDL_AudioSpec obtained;
static int opened, closed, live_wavs, mix_calls, fail_load, bad_format;
static const char *fixture;
int SDL_OpenAudio(SDL_AudioSpec *want,SDL_AudioSpec *got){obtained=*want;*got=*want;opened++;return 0;}
void SDL_PauseAudio(int paused){(void)paused;}
void SDL_CloseAudio(void){closed++;}
void SDL_LockAudio(void){}
void SDL_UnlockAudio(void){}
void SDL_MixAudio(Uint8 *d,const Uint8 *s,Uint32 sz,int v){mix_calls++; for(Uint32 i=0;i+1<sz;i+=2){int16_t a,b;memcpy(&a,d+i,2);memcpy(&b,s+i,2);int sum=a+(b*v/128);if(sum>32767)sum=32767;if(sum<-32768)sum=-32768;int16_t o=(int16_t)sum;memcpy(d+i,&o,2);}}
SDL_AudioSpec *SDL_LoadWAV(const char *path,SDL_AudioSpec *spec,Uint8 **data,Uint32 *size){
  (void)path;
  if(fail_load) return NULL;
  *spec=obtained; if(bad_format) spec->freq=44100;
  *size=16; *data=malloc(*size);assert(*data); live_wavs++;
  for(unsigned i=0;i<*size;i+=2){int16_t n=1200;memcpy(*data+i,&n,2);}return spec;
}
void SDL_FreeWAV(Uint8 *ptr){if(ptr){free(ptr);live_wavs--;}}
error_t cpymo_assetloader_get_bgm_path(char **out,cpymo_str name,const cpymo_assetloader *a){(void)name;(void)a;*out=strdup(fixture);return *out?CPYMO_ERR_SUCC:CPYMO_ERR_OUT_OF_MEM;}
error_t cpymo_assetloader_get_se_path(char **out,cpymo_str name,const cpymo_assetloader *a){return cpymo_assetloader_get_bgm_path(out,name,a);}
error_t cpymo_assetloader_get_vo_path(char **out,cpymo_str name,const cpymo_assetloader *a){return cpymo_assetloader_get_bgm_path(out,name,a);}
void cpymo_str_copy(char *dst,size_t size,cpymo_str name){assert(size>name.len);memcpy(dst,name.begin,name.len);dst[name.len]='\0';}

static cpymo_str span(const char *s) {cpymo_str x; x.begin=s; x.len=strlen(s);return x;}
static void tick(int n){Uint8 b[64];memset(b,7,sizeof(b));obtained.callback(NULL,b,n);}
int main(void){
  char path[]="/tmp/rg35xx-audio-test-XXXXXX";
  int fd=mkstemp(path);assert(fd!=-1); FILE *f=fdopen(fd,"wb");assert(f); char zeros[64]={0};assert(fwrite(zeros,1,64,f)==64);fclose(f);fixture=path;
  cpymo_engine e;memset(&e,0,sizeof(e));
  cpymo_audio_init(&e.audio);assert(opened==1 && cpymo_audio_enabled(&e));
  assert(cpymo_audio_bgm_play(&e,span("song"),1)==CPYMO_ERR_SUCC);
  assert(live_wavs==1);assert(!strcmp(cpymo_audio_get_bgm_name(&e),"song"));
  assert(cpymo_audio_channel_is_playing(0,&e.audio));tick(64);
  assert(mix_calls>0 && cpymo_audio_channel_is_looping(0,&e.audio));
  cpymo_audio_set_channel_volume(0,&e.audio,0);int before=mix_calls;tick(64);assert(mix_calls==before);
  cpymo_audio_set_channel_volume(0,&e.audio,1);tick(64);assert(mix_calls>before);
  /* Non-looped channel should end after consumed samples. */
  assert(cpymo_audio_se_play(&e,span("se"),false)==CPYMO_ERR_SUCC);
  tick(64);assert(!cpymo_audio_channel_is_playing(1,&e.audio));
  assert(cpymo_audio_wait_se(&e,0));
  /* Unsupported WAV rate must not replace playing BGM or leak. */
  bad_format=1;assert(cpymo_audio_bgm_play(&e,span("bad"),false)==CPYMO_ERR_UNSUPPORTED);
  bad_format=0;assert(live_wavs==2);assert(!strcmp(cpymo_audio_get_bgm_name(&e),"song"));
  fail_load=1;assert(cpymo_audio_bgm_play(&e,span("missing"),false)==CPYMO_ERR_CAN_NOT_OPEN_FILE);
  fail_load=0;assert(live_wavs==2);
  e.assetloader.use_pkg_se=true;
  assert(cpymo_audio_se_play(&e,span("packed"),false)==CPYMO_ERR_UNSUPPORTED);
  e.assetloader.use_pkg_se=false;
  assert(cpymo_audio_vo_play(&e,span("voice"))==CPYMO_ERR_SUCC);
  cpymo_audio_bgm_stop(&e);assert(!cpymo_audio_channel_is_playing(0,&e.audio));
  cpymo_audio_free(&e.audio);assert(live_wavs==0 && closed==1);
  cpymo_audio_init(&e.audio);assert(opened==2);
  assert(cpymo_audio_bgm_play(&e,span("startup"),false)==CPYMO_ERR_SUCC);
  cpymo_backend_audio_free();assert(closed==2 && live_wavs==0);
  unlink(path);
  puts("PASS SDL1 PCM WAV backend: BGM loop, stereo mixing, mute advancement, SE completion, VO, failures, resource cleanup");
  return 0;
}
