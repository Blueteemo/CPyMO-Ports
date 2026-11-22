/* Standalone host test of the REAL RG35XX cache implementation, with fake
   SDL_ttf allocation hooks. Never use this fake TTF provider on ARM. */
#include <SDL/SDL_ttf.h>
#include "rg35xx_ttf_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct TTF_Font { int pt; unsigned magic; };
static int opens, closes, fail_pt = -1;
TTF_Font *TTF_OpenFont(const char *path, int ptsize) {
    if (!path || !strcmp(path,"/missing") || ptsize == fail_pt) return NULL;
    TTF_Font *f=malloc(sizeof *f);if (!f)return NULL;
    *f=(struct TTF_Font){ptsize,0x35AACC}; opens++;return f;
}
void TTF_CloseFont(TTF_Font*f){if(!f||f->magic!=0x35AACC)abort();f->magic=0;free(f);closes++;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(2);}}while(0)
int main(void){
 rg35xx_ttf_cache c={0};
 CHECK(rg35xx_ttf_cache_size(&c,16)==NULL);
 TTF_Font *a=rg35xx_ttf_cache_load(&c,"/game1/system/default.ttf",16);
 CHECK(a && opens==1 && closes==0);
 CHECK(rg35xx_ttf_cache_load(&c,"/missing",20)==NULL);
 CHECK(rg35xx_ttf_cache_size(&c,16)==a);
 TTF_Font *b=rg35xx_ttf_cache_size(&c,24);
 CHECK(b && opens==2);
 for(int i=0;i<10000;i++){
  CHECK(rg35xx_ttf_cache_size(&c,16)==a);
  CHECK(rg35xx_ttf_cache_size(&c,24)==b);
 }
 CHECK(opens==2 && closes==0);
 TTF_Font *d=rg35xx_ttf_cache_size(&c,28);
 CHECK(d && opens==3);
 fail_pt=35;
 CHECK(rg35xx_ttf_cache_size(&c,35)==NULL);
 CHECK(opens==3 && closes==0 && rg35xx_ttf_cache_size(&c,28)==d);
 fail_pt=-1;
 TTF_Font *e=rg35xx_ttf_cache_size(&c,35);
 CHECK(e && opens==4 && closes==1);
 CHECK(rg35xx_ttf_cache_size(&c,35)==e);
 CHECK(rg35xx_ttf_cache_size(&c,24)==b); /* recently hit */
 CHECK(rg35xx_ttf_cache_size(&c,28)==d); /* recently hit */
 /* New game must close all old handles, not reuse wrong game's glyphs. */
 TTF_Font *g=rg35xx_ttf_cache_load(&c,"/game2/system/default.otf",16);
 CHECK(g && opens==5 && closes==4);
 CHECK(!strcmp(c.path,"/game2/system/default.otf"));
 rg35xx_ttf_cache_clear(&c);
 CHECK(opens==closes && c.path==NULL);
 rg35xx_ttf_cache_clear(&c);
 CHECK(opens==closes);
 puts("PASS: bounded three-size cache, 20k alternate requests -> 2 font opens, LRU replacement, non-destructive failures and game-font invalidation");
 return 0;
}
