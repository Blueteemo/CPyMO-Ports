#include "cpymo_prelude.h"
#ifdef GARLIC_NO_FFMPEG
#include "garlic_image_cache.h"
#include "../cpymo-backends/software/cpymo_backend_software.h"
#include <stdio.h>
#include <string.h>
/* Cache owns one reference; engine/UI owners keep their own references.
 * No disk cache. Cleared after engine release at every game boundary. */
#define CACHE_BUDGET (12u*1024u*1024u)
typedef struct { char key[512]; cpymo_backend_image image; int w,h; uint64_t age; size_t bytes; } entry;
static entry entries[24];
static size_t owned;
static unsigned hits, misses;
static uint64_t clock_age;
static bool key_for(char out[512],const char *dir,const char *type,cpymo_str name,bool mask)
{
    if(name.len>200)return false;
    int n=snprintf(out,512,"%s/%s/%.*s:%u",dir,type,(int)name.len,name.begin,(unsigned)mask);
    return n>0 && n<512;
}
static void evict(entry *e)
{
    if(e->image){owned-=e->bytes;cpymo_backend_image_free(e->image);}
    memset(e,0,sizeof(*e));
}
bool garlic_image_cache_get(cpymo_backend_image *image,int *w,int *h,
    const char *dir,const char *type,cpymo_str name,bool mask)
{
    char key[512];if(!key_for(key,dir,type,name,mask))return false;
    for(unsigned i=0;i<24;i++)if(entries[i].image && !strcmp(entries[i].key,key)){
        entry *e=entries+i;*image=e->image;*w=e->w;*h=e->h;e->age=++clock_age;
        ((cpymo_backend_software_image*)e->image)->references++;hits++;return true;
    }
    misses++;return false;
}
void garlic_image_cache_put(cpymo_backend_image image,int w,int h,
    const char *dir,const char *type,cpymo_str name,bool mask)
{
    char key[512];if(!image || !key_for(key,dir,type,name,mask))return;
    cpymo_backend_software_image *im=image;
    size_t bytes=im->line_stride*im->h+sizeof(*im);
    if(bytes>CACHE_BUDGET)return;
    entry *slot=NULL;
    while(!slot || owned+bytes>CACHE_BUDGET){
        entry *old=NULL;
        for(unsigned i=0;i<24;i++){
            if(!entries[i].image && !slot)slot=entries+i;
            if(entries[i].image && (!old || entries[i].age<old->age))old=entries+i;
        }
        if(slot && owned+bytes<=CACHE_BUDGET)break;
        if(!old)return;
        evict(old);slot=old;
    }
    strcpy(slot->key,key);slot->image=image;slot->w=w;slot->h=h;
    slot->bytes=bytes;slot->age=++clock_age;owned+=bytes;im->references++;
}
void garlic_image_cache_counts(size_t *bytes,unsigned *h,unsigned *m)
{*bytes=owned;*h=hits;*m=misses;}
void garlic_image_cache_clear(void)
{for(unsigned i=0;i<24;i++)evict(entries+i);hits=misses=0;clock_age=0;}
#endif
