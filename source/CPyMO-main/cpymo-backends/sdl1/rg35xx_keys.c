#define _POSIX_C_SOURCE 200809L
#include "rg35xx_keys.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* This mapping covers primary GPIO controls; volume buttons intentionally excluded. */
static const unsigned char code_to_bit[][2] = {
 {0x5a,0},{0x5b,1},{0x5c,2},{0x5d,3},{0x5e,4},{0x5f,5},
 {0x60,7},{0x61,6},{0x62,8},{0x63,9},{0x64,10},{0x65,11},
 {0x66,12},{0x67,13},{0x68,16}
};
uint32_t rg35xx_key_event(uint32_t held,unsigned code,int value){
 for(unsigned i=0;i<sizeof(code_to_bit)/sizeof(code_to_bit[0]);++i){
  if(code_to_bit[i][0] != code)continue;
  uint32_t bit=1u<<code_to_bit[i][1];
  if(value == 1 || value == 2) held |= bit;
  else if(value == 0) held &=~bit;
  break;
 }
 /* Keep physically held state; normalize opposing directions in the
    CPyMO adapter, so releasing either side does not lose the other. */
 return held;
}
static int fd=-1;
static uint32_t keys;
void rg35xx_keys_initialize(void){
 if(fd>=0)return;
 DIR *dir=opendir("/dev/input"); if(!dir)return;
 struct dirent *ent;
 while((ent=readdir(dir))){
  if(strncmp(ent->d_name,"event",5))continue;
  char path[128];snprintf(path,sizeof path,"/dev/input/%.80s",ent->d_name);
  int candidate=open(path,O_RDONLY|O_NONBLOCK);
  if(candidate<0)continue;
  char name[128]={0};
  if(ioctl(candidate,EVIOCGNAME(sizeof name),name)>=0 && !strcmp(name,"gpio-keys-polled")){
   fd=candidate;break;
  }
  close(candidate);
 }
 closedir(dir);
}
uint32_t rg35xx_keys_snapshot(void){
 static unsigned scan_interval;
 if(fd<0 && (scan_interval++ % 120u)==0)rg35xx_keys_initialize();
 if(fd<0)return 0;
 struct input_event ev;
 for(int i=0;i<96;i++){
  ssize_t n=read(fd,&ev,sizeof ev);
  if(n!=(ssize_t)sizeof ev){
   if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))break;
   close(fd);fd=-1;keys=0;break;
  }
  if(ev.type==EV_KEY)keys=rg35xx_key_event(keys,ev.code,ev.value);
 }
 return keys;
}
void rg35xx_keys_shutdown(void){if(fd>=0)close(fd);fd=-1;keys=0;}
