/* Host-only integration harness. Intentionally does not execute on RG35XX. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libretro.h>
static bool shutdown_flag=false, got_format=false; static unsigned frames=0,polls=0, rgb_nonzero=0;

static void log_cb(enum retro_log_level level,const char *fmt,...) { (void)level;va_list va;va_start(va,fmt);vfprintf(stderr,fmt,va);va_end(va); }
static bool environment(unsigned cmd,void *data) {
 switch(cmd){
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log=log_cb;return true;
 case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: return false;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: got_format=*(enum retro_pixel_format*)data==RETRO_PIXEL_FORMAT_RGB565;return got_format;
 case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: (void)data; return false; /* Real frontends may not implement it. */
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: { const char **p=data;*p="/nonexistent";return true; }
 case RETRO_ENVIRONMENT_SHUTDOWN: shutdown_flag=true;return true;
 default: return false;
 }
}
static void video(const void *data,unsigned w,unsigned h,size_t pitch) {
 if(w!=640||h!=480||pitch!=1280||!data){fprintf(stderr,"BAD VIDEO %u %u %zu\n",w,h,pitch);exit(15);}frames++;
 const uint16_t *pix=data;for(size_t i=0;i<(size_t)w*h;i+=53)if(pix[i]){rgb_nonzero++;break;}
}
static size_t audio_batch(const int16_t *buf,size_t samples) {(void)buf;return samples;}
static void input_poll(void){polls++;}
static int16_t input_state(unsigned p,unsigned d,unsigned i,unsigned k){(void)p;(void)d;(void)i;(void)k;return 0;}
#define RESOLVE(name) do{*(void**)(&name)=dlsym(lib,#name);if(!name){fprintf(stderr,"missing %s\n",#name);return 2;}}while(0)
int main(int argc,char**argv){
 if(argc!=4){fprintf(stderr,"usage: harness CORE.so GAMECONFIG.txt BADFONT_GAMECONFIG.txt BADFONT_GAMECONFIG.txt\n");return 2;}
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 3;}
 unsigned (*retro_api_version)(void);void (*retro_set_environment)(retro_environment_t);
 void (*retro_set_video_refresh)(retro_video_refresh_t);void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
 void (*retro_set_input_poll)(retro_input_poll_t);void (*retro_set_input_state)(retro_input_state_t);
 void (*retro_get_system_av_info)(struct retro_system_av_info*); void (*retro_init)(void);
 bool (*retro_load_game)(const struct retro_game_info*);void (*retro_run)(void);void (*retro_unload_game)(void);
 RESOLVE(retro_api_version);RESOLVE(retro_set_environment);RESOLVE(retro_set_video_refresh);
 RESOLVE(retro_set_audio_sample_batch);RESOLVE(retro_set_input_poll);RESOLVE(retro_set_input_state);
 RESOLVE(retro_get_system_av_info);RESOLVE(retro_init);RESOLVE(retro_load_game);RESOLVE(retro_run);RESOLVE(retro_unload_game);
 if(retro_api_version()!=RETRO_API_VERSION)return 4;
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio_batch);
 retro_set_input_poll(input_poll);retro_set_input_state(input_state);retro_init();
 struct retro_system_av_info info={0};retro_get_system_av_info(&info);
 if(info.geometry.base_width!=640||info.geometry.base_height!=480||!got_format)return 5;
 struct retro_game_info game={argv[2],NULL,0,NULL};
 char original_pwd[4096]; if(!getcwd(original_pwd,sizeof(original_pwd)))return 10;
 if(!retro_load_game(&game)){fprintf(stderr,"retro_load_game failed\n");return 6;}
 for(unsigned i=0;i<180&&!shutdown_flag;i++) retro_run();
 if (!shutdown_flag) {
    fprintf(stderr, "FAIL: missing frame-time callback stalled #wait script after %u frames\n", frames);
    return 18;
 }
 printf("PASS: no-frame-callback, timed #wait script finished; frames=%u polls=%u\n", frames, polls);
 retro_unload_game();
 dlclose(lib);
 return 0;
}
