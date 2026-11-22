/* A real core's audio callback keeps running while the UI thread stalls.
 * Host-only frontend harness. No audio device, no engine state mutation. */
#define main capture_base_main
#include "../v0245/capture.c"
#undef main
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
static struct retro_audio_callback iface;
static atomic_bool audio_stop;
static atomic_ulong pcm_count;
static bool async_env(unsigned command,void *data){
    if(command==RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK){iface=*(struct retro_audio_callback*)data;return true;}
    return environment(command,data);
}
static size_t consume_pcm(const int16_t *p,size_t n){(void)p;atomic_fetch_add(&pcm_count,n);return n;}
static void show_frame(const void *p,unsigned w,unsigned h,size_t pitch){(void)p;(void)w;(void)h;(void)pitch;}
static void *audio_thread(void *unused){
    (void)unused;struct timespec gap={0,42666667};
    while(!atomic_load(&audio_stop)){iface.callback();nanosleep(&gap,NULL);}return NULL;
}
int main(int argc,char **argv){
    if(argc<4)return 2;bool expect_batch=argc>4;
    void *handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!handle){fprintf(stderr,"%s\n",dlerror());return 3;}
#define API(TYPE,NAME,SYMBOL) __typeof__((TYPE)0) NAME; *(void**)(&NAME)=dlsym(handle,SYMBOL);if(!NAME)return 4
    API(void (*)(retro_environment_t),set_env,"retro_set_environment");
    API(void (*)(retro_video_refresh_t),set_video,"retro_set_video_refresh");
    API(void (*)(retro_audio_sample_batch_t),set_audio,"retro_set_audio_sample_batch");
    API(void (*)(retro_input_poll_t),set_poll,"retro_set_input_poll");
    API(void (*)(retro_input_state_t),set_input,"retro_set_input_state");
    API(void (*)(void),init_core,"retro_init");
    API(bool (*)(const struct retro_game_info *),load_core,"retro_load_game");
    API(void (*)(void),run_core,"retro_run");
    API(void (*)(void),unload_core,"retro_unload_game");
    API(void (*)(void),deinit_core,"retro_deinit");
#undef API
    setenv("CPYMO_MENU","0",1);setenv("CPYMO_SAVE_ROOT",argv[3],1);
    quiet=true;capture_enabled=false;
    set_env(async_env);set_video(show_frame);set_audio(consume_pcm);set_poll(poll);set_input(state);init_core();
    struct retro_game_info game={argv[2],NULL,0,NULL};if(!load_core(&game))return 5;
    for(unsigned i=0;i<180;i++)run_core();
    if((iface.callback!=NULL)==expect_batch)return 6;
    pthread_t thread;bool started=iface.callback!=NULL;
    if(started){iface.set_state(true);if(pthread_create(&thread,NULL,audio_thread,NULL))return 7;}
    keys[180]=8;for(unsigned i=0;i<14;i++)keys[190+i*3]=32;
    unsigned preserved=0,stalls=0;
    for(unsigned i=0;i<80;i++){
        run_core();
        if(i%8==0){unsigned long before=atomic_load(&pcm_count);usleep(80000);
            if(atomic_load(&pcm_count)>before)preserved++;else stalls++;}
        usleep(16000);
    }
    atomic_store(&audio_stop,true);if(started)pthread_join(thread,NULL);
    if(started)iface.set_state(false);
    unsigned long total=atomic_load(&pcm_count);
    unload_core();deinit_core();dlclose(handle);
    printf("ASYNC_UI async_registered=%d simulated_ui_pauses=10 audio_advanced_during_pause=%u no_progress_pauses=%u pcm_frames=%lu\n",started,preserved,stalls,total);
    return expect_batch ? (stalls==10 ? 0 : 8) : (preserved==10 && total>48000 ? 0 : 9);
}
