#define _GNU_SOURCE
#define main legacy_main
#include "render_check.c"
#undef main
#include <pthread.h>
#include <stdatomic.h>
static struct retro_audio_callback cb;
static _Atomic bool finish_audio;
static _Atomic unsigned audio_blocks;
static bool async_env(unsigned cmd,void *data){
 if(cmd==RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK){cb=*(struct retro_audio_callback*)data;return true;}
 return hw_environment(cmd,data);
}
static size_t sink(const int16_t *p,size_t n){
 for(size_t i=0;i<n*2;i++)if(p[i]<-16384 || p[i]>16384)abort();
 atomic_fetch_add(&audio_blocks,1);return n;
}
static void *pump(void *arg){(void)arg;while(!atomic_load(&finish_audio)){cb.callback();usleep(500);}return NULL;}
int main(int argc,char **argv){
 CHECK(argc==3);char tmp[]="/tmp/cpymo-async8-XXXXXX";CHECK(mkdtemp(tmp));
 char report[4096];snprintf(report,sizeof(report),"%s/audio-status.txt",tmp);setenv("CPYMO_AUDIO_REPORT",report,1);char capture[4096];snprintf(capture,sizeof(capture),"%s/capture.wav",tmp);setenv("CPYMO_AUDIO_CAPTURE",capture,1);
 setenv("CPYMO_SAVE_ROOT",tmp,1);setenv("CPYMO_MENU_FONT",argv[1],1);setenv("CPYMO_MENU","0",1);
 retro_set_environment(async_env);retro_set_video_refresh(hw_video);retro_set_audio_sample_batch(sink);retro_set_input_poll(hw_poll);retro_set_input_state(hw_input);retro_init();
 struct retro_game_info game={argv[2],NULL,0,NULL};CHECK(retro_load_game(&game));CHECK(cb.callback && use_audio_callback);
 FILE *rf=fopen(report,"r");CHECK(rf);char status[1024]={0};CHECK(fread(status,1,sizeof(status)-1,rf)>0);fclose(rf);CHECK(strstr(status,"frontend_audio_callback=accepted") && strstr(status,"output_percent=50"));
 pthread_t thread;CHECK(!pthread_create(&thread,NULL,pump,NULL));
 for(unsigned cycle=0;cycle<12;cycle++){
  if(cycle){CHECK(retro_load_game(&game));}
  for(unsigned i=0;i<180;i++)retro_run();
  if(getenv("CPYMO_QA_PACKAGED_THREAD")){
   CHECK(cpymo_audio_bgm_play(&engine,cpymo_str_pure("n03"),true)==0);
   CHECK(cpymo_audio_se_play(&engine,cpymo_str_pure("rain_12"),true)==0);
   CHECK(cpymo_audio_vo_play(&engine,cpymo_str_pure("n200"))==0);
  }
  CHECK(cpymo_config_ui_enter(&engine)==0);cpymo_config_ui *settings=cpymo_list_ui_data(&engine);
  for(unsigned k=0;k<9;k++)CHECK(cpymo_config_ui_set_value(&engine,settings,ITEM_FONT_SIZE,k%3,false)==0);
  unsigned before=atomic_load(&audio_blocks);usleep(60000);CHECK(atomic_load(&audio_blocks)>before);
  cpymo_ui_exit(&engine);retro_unload_game();
  before=atomic_load(&audio_blocks);usleep(10000);CHECK(atomic_load(&audio_blocks)>before);
 }
 FILE *wf=fopen(capture,"rb");CHECK(wf);unsigned char wh[44];CHECK(fread(wh,1,44,wf)==44);CHECK(!memcmp(wh,"RIFF",4) && !memcmp(wh+8,"WAVE",4));CHECK(!fseek(wf,0,SEEK_END));long bytes=ftell(wf);fclose(wf);CHECK(bytes>44 && bytes<=44+480000*4);
 atomic_store(&finish_audio,true);CHECK(!pthread_join(thread,NULL));retro_deinit();
 printf("ASYNC CORE PASS blocks=%u reloads=12 settings_changes=108 deliberate_main_stalls=12 callbacks_after_unload=12\n",atomic_load(&audio_blocks));return 0;
}
