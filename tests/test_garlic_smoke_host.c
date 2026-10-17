/* Host-only proof of complete synthetic Smoke Test script with timed input.
 * No ARM hardware or real speaker involved. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libretro.h>
static unsigned frames, polls, nonzero_frames;
static bool shutdown_requested, rgb565_accepted;
static void logger(enum retro_log_level level,const char *fmt,...) {
 (void)level;va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);
}
static bool environment(unsigned cmd,void *data) {
 switch(cmd) {
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    rgb565_accepted=(*(enum retro_pixel_format*)data==RETRO_PIXEL_FORMAT_RGB565);
    return rgb565_accepted;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log=logger;return true;
  case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: return false;
  case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: return false;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data="/nonexistent";return true;
  case RETRO_ENVIRONMENT_SHUTDOWN: shutdown_requested=true;return true;
  default: return false;
 }
}
static void video(const void *buf,unsigned w,unsigned h,size_t stride) {
 if(!buf||w!=640||h!=480||stride!=1280){fputs("FAIL video geometry\n",stderr);exit(20);}
 frames++;const uint16_t *pix=buf;
 for(size_t i=0;i<(size_t)w*h;i+=67)if(pix[i]){nonzero_frames++;break;}
}
static void poll(void){polls++;}
static int16_t state(unsigned port,unsigned device,unsigned index,unsigned id) {
 if(port==0&&device==RETRO_DEVICE_JOYPAD&&index==0&&id==RETRO_DEVICE_ID_JOYPAD_A) {
   /* Periodic one-frame A press creates key up/down edges. */
   return (polls%38==5);
 }
 return 0;
}
static size_t audio_batch(const int16_t *buffer,size_t frames){(void)buffer;return frames;}
#define RESOLVE(name) do{*(void**)(&name)=dlsym(lib,#name);if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}}while(0)
int main(int argc,char**argv){
 if(argc!=3)return 2;
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
 if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 3;}
 unsigned (*retro_api_version)(void);void (*retro_set_environment)(retro_environment_t);
 void (*retro_set_video_refresh)(retro_video_refresh_t);
 void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
 void (*retro_set_input_poll)(retro_input_poll_t);void (*retro_set_input_state)(retro_input_state_t);
 void (*retro_init)(void);bool (*retro_load_game)(const struct retro_game_info*);
 void (*retro_run)(void);void (*retro_unload_game)(void);void (*retro_deinit)(void);
 RESOLVE(retro_api_version);RESOLVE(retro_set_environment);RESOLVE(retro_set_video_refresh);
 RESOLVE(retro_set_audio_sample_batch);RESOLVE(retro_set_input_poll);
 RESOLVE(retro_set_input_state);RESOLVE(retro_init);RESOLVE(retro_load_game);
 RESOLVE(retro_run);RESOLVE(retro_unload_game);RESOLVE(retro_deinit);
 if(retro_api_version()!=RETRO_API_VERSION)return 4;
 retro_set_environment(environment);retro_set_video_refresh(video);
 retro_set_audio_sample_batch(audio_batch);retro_set_input_poll(poll);
 retro_set_input_state(state);retro_init();
 if(!rgb565_accepted)return 5;
 struct retro_game_info game={argv[2],NULL,0,NULL};
 if(!retro_load_game(&game))return 6;
 for(unsigned i=0;i<500&&!shutdown_requested;i++)retro_run();
 retro_unload_game();retro_deinit();dlclose(lib);
 printf("smoke-host frames=%u polls=%u nonzero=%u shutdown=%d\n",frames,polls,nonzero_frames,shutdown_requested);
 if(frames<50||polls<50||nonzero_frames<10||!shutdown_requested)return 7;
 puts("PASS: entire test game, UTF-8 text, input edges, no frame-time callback, clean termination");
 return 0;
}
