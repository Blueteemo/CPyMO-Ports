#define _GNU_SOURCE
#include "../../source/CPyMO-main/cpymo-backends/libretro/garlic_audio_observer.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <assert.h>
static atomic_bool done;
static void *writer(void *unused){
 (void)unused;int16_t pcm[1024];for(unsigned i=0;i<1024;i++)pcm[i]=1000;
 for(unsigned i=0;i<1000;i++){
  garlic_audio_observe(pcm,512,true,i*1000,i*1000+100,i*1000+900,i%17?512:511);
  assert(pcm[0]==1000 && pcm[1023]==1000);usleep(1000);
 }
 atomic_store(&done,true);return NULL;
}
int main(void){
 setenv("CPYMO_AUDIO_REPORT","test",1);setenv("CPYMO_AUDIO_CAPTURE","test",1);garlic_audio_observer_configure();
 char path[]="/tmp/cpymo-capture-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
 pthread_t t;assert(!pthread_create(&t,NULL,writer,NULL));unsigned snapshots=0;
 while(!atomic_load(&done)){
  uint32_t n=__atomic_load_n(&garlic_capture_frames,__ATOMIC_ACQUIRE);
  if(n){
   assert(garlic_audio_write_capture(path,n));FILE *f=fopen(path,"rb");assert(f);unsigned char head[44];assert(fread(head,1,44,f)==44);assert(!memcmp(head,"RIFF",4) && !memcmp(head+8,"WAVE",4));
   uint32_t bytes=head[40]|(head[41]<<8)|(head[42]<<16)|(head[43]<<24);assert(bytes==n*4);
   for(unsigned i=0;i<n*2;i++){int16_t v;assert(fread(&v,2,1,f)==1 && v==1000);}assert(fgetc(f)==EOF);fclose(f);snapshots++;
  }
  usleep(10000);
 }
 assert(!pthread_join(t,NULL));assert(garlic_capture_frames==480000 && garlic_audio_observed_calls==1000 && garlic_audio_partial_batches==59);
 assert(garlic_audio_max_mix_us==100 && garlic_audio_max_submit_us==800);
 assert(!garlic_audio_write_capture(path,GARLIC_CAPTURE_FRAMES+1));
 garlic_audio_previous_begin=UINT32_MAX-50;garlic_audio_previous_active=1;garlic_audio_max_start_gap_us=0;
 int16_t pcm[2]={1,2};garlic_audio_observe(pcm,1,true,20,21,22,1);assert(garlic_audio_max_start_gap_us==71);
 unlink(path);printf("OBSERVER PASS snapshots=%u capture_frames=480000 bounded_wav=1 input_unchanged=1 timer_wrap=1\n",snapshots);
}
