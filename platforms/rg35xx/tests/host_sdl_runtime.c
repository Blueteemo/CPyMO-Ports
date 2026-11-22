/* HOST-ONLY interface simulator. No SDL runtime, ARM ABI, or gameplay evidence. */
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <cpymo_engine.h>
#include <cpymo_save_global.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
static SDL_Surface *physical;
static Uint32 clock_ms;
static unsigned flips;
static unsigned logical_allocs;
static unsigned logical_frees;
static unsigned display_modes;
static unsigned text_calls;
static unsigned ttf_font_opens;
static unsigned ttf_font_closes;
static unsigned ttf_init_calls;
static unsigned ttf_quit_calls;
static unsigned flip_attempts;
static unsigned lock_count;
static unsigned unlock_count;
static int save_tested;
static int fail_one_save_write;
static int fail_one_save_close;
/* HOST-ONLY linker fault injection. These are never compiled for ARM. */
extern size_t __real_fwrite(const void*,size_t,size_t,FILE*);
size_t __wrap_fwrite(const void *buf,size_t sz,size_t n,FILE *file){
 if(fail_one_save_write){fail_one_save_write=0;return 0;}
 return __real_fwrite(buf,sz,n,file);
}
extern int __real_fclose(FILE*);
int __wrap_fclose(FILE *file){
 int rc=__real_fclose(file);
 if(fail_one_save_close){fail_one_save_close=0;return EOF;}
 return rc;
}

static unsigned wrong_dims;
static int quitting;
struct TTF_Font { int pt; };

static SDL_Surface *mk(int w,int h,int bits,Uint32 rm,Uint32 gm,Uint32 bm,Uint32 am){
 if(w<=0||h<=0||w>3000||h>3000||(bits!=16&&bits!=24&&bits!=32))return NULL;
 SDL_Surface *s=calloc(1,sizeof *s);if(!s)return NULL;
 s->format=calloc(1,sizeof *s->format);if(!s->format){free(s);return NULL;}
 s->format->BitsPerPixel=(Uint8)bits;s->format->BytesPerPixel=(Uint8)(bits/8);
 s->format->Rmask=rm;s->format->Gmask=gm;s->format->Bmask=bm;s->format->Amask=am;
 s->w=w;s->h=h;s->pitch=(Uint16)(w*(bits/8));s->pixels=calloc((size_t)h,s->pitch);
 if(!s->pixels){free(s->format);free(s);return NULL;}
 s->clip_rect=(SDL_Rect){0,0,(Uint16)w,(Uint16)h}; logical_allocs++;return s;
}
int SDL_Init(Uint32 f){(void)f;return 0;}
void SDL_Quit(void){
 if(getenv("HOST_SIM_ASSERT_TTF_CACHE")){
  fprintf(stderr,"HOST_SIM: TTF init=%u quit=%u open=%u close=%u text=%u\n",ttf_init_calls,ttf_quit_calls,ttf_font_opens,ttf_font_closes,text_calls);
  if(ttf_font_opens!=ttf_font_closes || ttf_init_calls!=ttf_quit_calls || ttf_font_opens>10){
   fputs("HOST_SIM: FAIL TTF cache resource lifecycle\n",stderr);exit(81);
  }
  puts("HOST_SIM: PASS TTF cache resource lifecycle");
 }

 if(getenv("HOST_SIM_TEST_SAVE") && !save_tested){fputs("HOST_SIM: FAIL save test never executed\n",stderr);exit(57);}
 if(getenv("HOST_SIM_EXPECT_PRESENT_FAILURE")){
  if(!physical || display_modes!=1 || logical_allocs!=logical_frees+1 ||
     unlock_count>lock_count || flip_attempts>1){
   fprintf(stderr,"HOST_SIM: FAIL present cleanup: physical=%p modes=%u alloc=%u free=%u text=%u locks=%u unlock=%u fliptry=%u\n",(void*)physical,display_modes,logical_allocs,logical_frees,text_calls,lock_count,unlock_count,flip_attempts);exit(41);
  }
  /* A failed LCD lock must release the preceding logical lock. */
  if(getenv("HOST_SIM_FAIL_LCD_LOCK") && (lock_count!=unlock_count+1)){
   fputs("HOST_SIM: FAIL unbalanced LCD-failure locks\n",stderr);exit(42);
  }
  if(getenv("HOST_SIM_FAIL_FLIP") && lock_count!=unlock_count){
   fputs("HOST_SIM: FAIL unbalanced flip-failure locks\n",stderr);exit(43);
  }
  if(getenv("HOST_SIM_FAIL_LOGICAL_LOCK") && (lock_count!=unlock_count+1)){
   fputs("HOST_SIM: FAIL unbalanced logical-failure locks\n",stderr);exit(44);
  }
  printf("HOST_SIM: PASS present failure detected, clean SDL exit (locks %u/%u flips_attempted %u)\n",lock_count,unlock_count,flip_attempts);
  SDL_FreeSurface(physical);physical=NULL;return;
 }
 if(getenv("HOST_SIM_EXPECT_FAILURE")){
 if(display_modes || text_calls || flips){fputs("HOST_SIM: FAIL expected startup failure before video init\n",stderr);exit(35);}
 puts("HOST_SIM: PASS early failure returned before display initialization");return;}
 fprintf(stderr,"HOST_SIM: flips=%u, lcd_modes=%u, allocs=%u, frees=%u, text=%u, dims=%u, ticks=%u\n",flips,display_modes,logical_allocs,logical_frees,text_calls,wrong_dims,clock_ms);
 if(flips==0||display_modes!=1||text_calls==0||wrong_dims||logical_allocs<2||logical_allocs!=logical_frees+1){fputs("HOST_SIM: FAIL smoke assertions\n",stderr);exit(34);}
 SDL_FreeSurface(physical); physical=NULL;
 puts("HOST_SIM: PASS full linked CPyMO entry/main loop/render/cleanup under fake SDL (HOST ONLY)");}
const char*SDL_GetError(void){return "fake-host-sdl";}
SDL_Surface* SDL_SetVideoMode(int w,int h,int bpp,Uint32 flags){(void)flags;display_modes++; if(bpp!=16||w!=640||h!=480){wrong_dims++;return NULL;} physical=mk(w,h,bpp,0xf800,0x07e0,0x001f,0);return physical;}
SDL_Surface* SDL_CreateRGBSurface(Uint32 flags,int w,int h,int bpp,Uint32 rm,Uint32 gm,Uint32 bm,Uint32 am){(void)flags;return mk(w,h,bpp,rm,gm,bm,am);}
SDL_Surface* SDL_CreateRGBSurfaceFrom(void* pixels,int w,int h,int bpp,int pitch,Uint32 rm,Uint32 gm,Uint32 bm,Uint32 am){(void)pitch;SDL_Surface*s=mk(w,h,bpp,rm,gm,bm,am);if(s&&pixels)memcpy(s->pixels,pixels,(size_t)s->pitch*h);return s;}
SDL_Surface*SDL_DisplayFormat(SDL_Surface *src){if(!src)return NULL;SDL_Surface*x=mk(src->w,src->h,src->format->BitsPerPixel,src->format->Rmask,src->format->Gmask,src->format->Bmask,src->format->Amask);if(x)memcpy(x->pixels,src->pixels,(size_t)src->pitch*src->h);return x;}
SDL_Surface*SDL_DisplayFormatAlpha(SDL_Surface *src){return SDL_DisplayFormat(src);}
void SDL_FreeSurface(SDL_Surface*s){if(s){free(s->pixels);free(s->format);free(s);logical_frees++;}}
int SDL_LockSurface(SDL_Surface *s){
 lock_count++;
 if(!s || !s->pixels) return -1;
 if(s==physical && getenv("HOST_SIM_FAIL_LCD_LOCK"))return -1;
 if(s!=physical && getenv("HOST_SIM_FAIL_LOGICAL_LOCK"))return -1;
 return 0;
}
void SDL_UnlockSurface(SDL_Surface*s){(void)s;unlock_count++;}
void SDL_SetClipRect(SDL_Surface*s,const SDL_Rect *r){if(s) s->clip_rect=r?*r:(SDL_Rect){0,0,s->w,s->h};}
void SDL_GetClipRect(SDL_Surface*s,SDL_Rect *r){if(r&&s)*r=s->clip_rect;}
int SDL_FillRect(SDL_Surface*s,SDL_Rect*r,Uint32 color){if(!s)return -1;SDL_Rect c=r?*r:(SDL_Rect){0,0,s->w,s->h};int bpp=s->format->BytesPerPixel;for(int y=c.y;y<c.y+c.h&&y<s->h;y++) for(int x=c.x;x<c.x+c.w&&x<s->w;x++){if(x>=0&&y>=0){unsigned char*p=(unsigned char*)s->pixels+y*s->pitch+x*bpp;if(bpp==2)*(uint16_t*)p=(uint16_t)color;else if(bpp==3){p[0]=color;p[1]=color>>8;p[2]=color>>16;}else *(uint32_t*)p=color;}}return 0;}
int SDL_Flip(SDL_Surface*s){if(s==physical){flip_attempts++;if(getenv("HOST_SIM_FAIL_FLIP"))return -1;flips++;}return 0;}
int SDL_BlitSurface(SDL_Surface*s,SDL_Rect*sr,SDL_Surface*d,SDL_Rect*dr){if(!s||!d)return -1;(void)sr;(void)dr;return 0;}
Uint32 SDL_MapRGBA(SDL_PixelFormat*f,Uint8 r,Uint8 g,Uint8 b,Uint8 a){(void)a;return f->BitsPerPixel==16?((Uint32)(r&0xf8)<<8)|((Uint32)(g&0xfc)<<3)|(b>>3):((Uint32)r<<16)|((Uint32)g<<8)|b;}
void SDL_GetRGB(Uint32 p,SDL_PixelFormat*f,Uint8*r,Uint8*g,Uint8*b){if(f->BitsPerPixel==16){*r=((p>>11)&31)<<3;*g=((p>>5)&63)<<2;*b=(p&31)<<3;}else{*r=p>>16;*g=p>>8;*b=p;}}
int SDL_SetAlpha(SDL_Surface*s,Uint32 f,Uint8 a){(void)s;(void)f;(void)a;return 0;}
Uint32 SDL_GetTicks(void){return clock_ms+=16;}
void SDL_Delay(Uint32 ms){(void)ms;}
int SDL_PollEvent(SDL_Event*e){if(!quitting&&clock_ms>=2500){quitting=1;if(e)e->type=SDL_QUIT;return 1;}return 0;}
void SDL_ShowCursor(int enabled){(void)enabled;}
void SDL_WM_SetCaption(const char*a,const char*b){(void)a;(void)b;}
void SDL_WM_SetIcon(SDL_Surface*s,Uint8*mask){(void)s;(void)mask;}
Uint8*SDL_GetKeyState(int *num){static Uint8 k[400];if(num)*num=400;return k;}
Uint8 SDL_GetMouseState(int*x,int*y){if(x)*x=0;if(y)*y=0;return 0;}
SDL_Joystick*SDL_JoystickOpen(int i){(void)i;return NULL;}
Sint16 SDL_JoystickGetAxis(SDL_Joystick*j,int i){(void)j;(void)i;return 0;}
Uint8 SDL_JoystickGetButton(SDL_Joystick*j,int i){(void)j;(void)i;return 0;}
SDL_RWops*SDL_RWFromFP(FILE*f,int close){(void)f;(void)close;return NULL;}
SDL_RWops*SDL_RWFromConstMem(const void*b,int n){(void)b;(void)n;return NULL;}
int SDL_RWclose(SDL_RWops*x){(void)x;return 0;}
void SDL_FreeRW(SDL_RWops*x){(void)x;}
int SDL_OpenAudio(SDL_AudioSpec*w,SDL_AudioSpec*g){if(g&&w)*g=*w;return 0;}
void SDL_CloseAudio(void){}
void SDL_PauseAudio(int x){(void)x;}
void SDL_MixAudio(Uint8*a,const Uint8*b,Uint32 c,int d){(void)a;(void)b;(void)c;(void)d;}
void SDL_LockAudio(void){}
void SDL_UnlockAudio(void){}
SDL_Overlay*SDL_CreateYUVOverlay(int w,int h,Uint32 f,SDL_Surface*s){(void)w;(void)h;(void)f;(void)s;return NULL;}
void SDL_FreeYUVOverlay(SDL_Overlay*x){(void)x;}
int SDL_LockYUVOverlay(SDL_Overlay*x){(void)x;return -1;}
void SDL_UnlockYUVOverlay(SDL_Overlay*x){(void)x;}
int SDL_DisplayYUVOverlay(SDL_Overlay*x,SDL_Rect*r){(void)x;(void)r;return -1;}
int TTF_Init(void){ttf_init_calls++;return 0;}
void TTF_Quit(void){ttf_quit_calls++;}
TTF_Font*TTF_OpenFont(const char*path,int ptsize){(void)path;if(getenv("HOST_SIM_TTF_FAIL"))return NULL;TTF_Font*f=malloc(sizeof *f);if(f){f->pt=ptsize;ttf_font_opens++;}return f;}
void TTF_CloseFont(TTF_Font*f){if(f)ttf_font_closes++;free(f);}
SDL_Surface*TTF_RenderUTF8_Blended(TTF_Font*f,const char*s,SDL_Color c){
 (void)c;
 if(!f||!s)return NULL;
 if(getenv("HOST_SIM_TEST_SAVE") && !save_tested){
  save_tested=1;
  extern cpymo_engine engine;
  const char *game_dir=engine.assetloader.gamedir;
  engine.assetloader.gamedir="/__rg35xx_nonexistent_mount__/game";
  engine.vars.globals_dirty=true;
  engine.flags.dirty=true;
  error_t err=cpymo_save_global_save(&engine);
  engine.assetloader.gamedir=game_dir;
  if(err!=CPYMO_ERR_CAN_NOT_OPEN_FILE || !engine.vars.globals_dirty || !engine.flags.dirty){
   fputs("HOST_SIM: FAIL dirty save state lost after unwritable path\n",stderr);exit(62);
  }
  fail_one_save_write=1;
  err=cpymo_save_global_save(&engine);
  if(err==CPYMO_ERR_SUCC || !engine.vars.globals_dirty || !engine.flags.dirty){
   fputs("HOST_SIM: FAIL fwrite failure cleared dirty flags\n",stderr);exit(65);
  }
  fail_one_save_close=1;
  err=cpymo_save_global_save(&engine);
  if(err==CPYMO_ERR_SUCC || !engine.vars.globals_dirty || !engine.flags.dirty){
   fputs("HOST_SIM: FAIL fclose failure cleared dirty flags\n",stderr);exit(66);
  }
  err=cpymo_save_global_save(&engine);
  if(err!=CPYMO_ERR_SUCC || engine.vars.globals_dirty || engine.flags.dirty){
   fputs("HOST_SIM: FAIL retry write did not succeed/clear flags\n",stderr);exit(63);
  }
  err=cpymo_save_global_load(&engine);
  if(err!=CPYMO_ERR_SUCC){
   fputs("HOST_SIM: FAIL retry-created global.csav could not be loaded\n",stderr);exit(64);
  }
  puts("HOST_SIM: PASS real global save: fopen/fwrite/fclose failures preserve dirty; retry+reload succeed");
 }
 text_calls++;
 return mk((int)strlen(s)*8+1,f->pt+4,32,0xff0000,0xff00,0xff,0xff000000);
}
SDL_Surface*TTF_RenderUTF8_Solid(TTF_Font*f,const char*s,SDL_Color c){return TTF_RenderUTF8_Blended(f,s,c);}
SDL_Surface*TTF_RenderUTF8_Shaded(TTF_Font*f,const char*s,SDL_Color c,SDL_Color b){(void)b;return TTF_RenderUTF8_Blended(f,s,c);}
int TTF_SizeUTF8(TTF_Font*f,const char*s,int*w,int*h){if(w)*w=(int)strlen(s)*8;if(h)*h=f?f->pt:20;return 0;}
