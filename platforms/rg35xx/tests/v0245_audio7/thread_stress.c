#define _GNU_SOURCE
#include "../../source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c"
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
static _Atomic bool finished;
static _Atomic unsigned blocks;
static cpymo_engine test_engine;
void garlic_audio_trace(const char*a,const char*b,uint64_t c,uint64_t d,long e,const cpymo_engine*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void *render(void *arg){
 (void)arg;int16_t pcm[800*2];
 while(!atomic_load(&finished)){
  cpymo_audio_copy_mixed_samples(pcm,sizeof(pcm),&test_engine.audio);
  for(unsigned i=0;i<1600;i++)assert(pcm[i]>=-16384 && pcm[i]<=16384);
  atomic_fetch_add(&blocks,1);
 }
 return NULL;
}
int main(int argc,char **argv){
 assert(argc==2);pthread_t worker;cpymo_audio_init(&test_engine.audio);
 assert(!pthread_create(&worker,NULL,render,NULL));
 for(unsigned i=0;i<1000;i++){
  if(i%25==0){cpymo_audio_free(&test_engine.audio);cpymo_audio_init(&test_engine.audio);}
  assert(cpymo_audio_play_video(&test_engine,argv[1])==0);
  cpymo_audio_set_channel_volume(0,&test_engine.audio,(float)(i%10)/10);
  cpymo_audio_channel_is_playing(0,&test_engine.audio);
  cpymo_audio_get_channel_volume(0,&test_engine.audio);
  cpymo_audio_get_bgm_name(&test_engine);
  if(i%50==0){unsigned before=atomic_load(&blocks);usleep(20000);assert(atomic_load(&blocks)>before);}
  cpymo_audio_bgm_stop(&test_engine);
 }
 atomic_store(&finished,true);assert(!pthread_join(worker,NULL));
 cpymo_audio_free(&test_engine.audio);printf("THREAD PASS blocks=%u lifecycle_resets=40 play_stop=1000 main_thread_stalls=20\n",atomic_load(&blocks));
}
