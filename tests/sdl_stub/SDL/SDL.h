/* Compile-only shim. NOT an ABI-compatible SDL header and MUST NOT be used to link target firmware. */
#ifndef CPYMO_SYNTAX_ONLY_SDL_H
#define CPYMO_SYNTAX_ONLY_SDL_H
#if defined(__arm__) || defined(__aarch64__)
#error "Compile-only shim cannot be used for target ARM builds"
#endif
#include <stdint.h>
#include <stdio.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef int16_t Sint16; typedef uint32_t Uint32;
typedef struct SDL_PixelFormat {Uint8 BitsPerPixel,BytesPerPixel,Rloss,Gloss,Bloss,Aloss,Rshift,Gshift,Bshift,Ashift;Uint32 Rmask,Gmask,Bmask,Amask,colorkey;Uint8 alpha;} SDL_PixelFormat;
typedef struct SDL_Rect {int16_t x,y;Uint16 w,h;} SDL_Rect;
typedef struct SDL_Surface {Uint32 flags; SDL_PixelFormat *format;int w,h;Uint16 pitch;void *pixels;int offset; SDL_Rect clip_rect; Uint32 unused1,locked;void *map;Uint32 format_version;int refcount;} SDL_Surface;
typedef struct SDL_Color {Uint8 r,g,b,unused;} SDL_Color;
typedef struct SDL_Joystick SDL_Joystick;typedef struct SDL_RWops SDL_RWops;
typedef struct SDL_keysym {Uint8 scancode;int sym;Uint16 mod;Uint16 unicode;}SDL_keysym;
typedef union SDL_Event {Uint8 type; struct{Uint8 type;Uint8 gain;Uint8 state;}active; struct{Uint8 type; SDL_keysym keysym;} key;struct{Uint8 type;Uint8 button;Uint16 x,y;}button;struct{Uint8 type;int w,h;}resize;}SDL_Event;
typedef struct SDL_AudioSpec {int freq;Uint16 format;Uint8 channels,silence;Uint16 samples,padding;Uint32 size;void (*callback)(void*,Uint8*,int);void*userdata;}SDL_AudioSpec;
typedef struct SDL_Overlay {Uint32 format;int w,h;int planes;Uint16*pitches;Uint8**pixels;}SDL_Overlay;
#define SDL_SWSURFACE 0u
#define SDL_HWSURFACE 1u
#define SDL_ASYNCBLIT 4u
#define SDL_ANYFORMAT 0x10000000u
#define SDL_HWPALETTE 0x20000000u
#define SDL_DOUBLEBUF 0x40000000u
#define SDL_FULLSCREEN 0x80000000u
#define SDL_RESIZABLE 0x10u
#define SDL_SRCALPHA 0x10000u
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_JOYSTICK 0x200u
#define SDL_DISABLE 0
#define SDL_QUIT 12
#define SDL_VIDEOEXPOSE 17
#define SDL_VIDEORESIZE 16
#define SDL_KEYDOWN 2
#define SDL_MOUSEBUTTONDOWN 5
#define SDL_BUTTON_WHEELUP 4
#define SDL_BUTTON_WHEELDOWN 5
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON(x) (1u<<((x)-1))
#define SDL_BIG_ENDIAN 4321
#define SDL_LIL_ENDIAN 1234
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#define SDLK_LEFT 276
#define SDLK_RIGHT 275
#define SDLK_UP 273
#define SDLK_DOWN 274
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 271
#define SDLK_SPACE 32
#define SDLK_ESCAPE 27
#define SDLK_MENU 319
#define SDLK_LSHIFT 304
#define SDLK_RSHIFT 303
#define SDLK_LCTRL 306
#define SDLK_RCTRL 305
#define KMOD_ALT 0x300
#define AUDIO_S16SYS 0x8010
#define SDL_MIX_MAXVOLUME 128
#define SDL_YV12_OVERLAY 0x32315659u
int SDL_Init(Uint32);void SDL_Quit(void);const char*SDL_GetError(void);SDL_Surface* SDL_SetVideoMode(int,int,int,Uint32);SDL_Surface* SDL_CreateRGBSurface(Uint32,int,int,int,Uint32,Uint32,Uint32,Uint32);SDL_Surface*SDL_CreateRGBSurfaceFrom(void*,int,int,int,int,Uint32,Uint32,Uint32,Uint32);SDL_Surface*SDL_DisplayFormat(SDL_Surface*);SDL_Surface*SDL_DisplayFormatAlpha(SDL_Surface*);void SDL_FreeSurface(SDL_Surface*);int SDL_LockSurface(SDL_Surface*);void SDL_UnlockSurface(SDL_Surface*);void SDL_SetClipRect(SDL_Surface*,const SDL_Rect*);void SDL_GetClipRect(SDL_Surface*,SDL_Rect*);int SDL_FillRect(SDL_Surface*,SDL_Rect*,Uint32);int SDL_Flip(SDL_Surface*);int SDL_BlitSurface(SDL_Surface*,SDL_Rect*,SDL_Surface*,SDL_Rect*);Uint32 SDL_MapRGBA(SDL_PixelFormat*,Uint8,Uint8,Uint8,Uint8);void SDL_GetRGB(Uint32,SDL_PixelFormat*,Uint8*,Uint8*,Uint8*);int SDL_SetAlpha(SDL_Surface*,Uint32,Uint8);Uint32 SDL_GetTicks(void);void SDL_Delay(Uint32);int SDL_PollEvent(SDL_Event*);void SDL_ShowCursor(int);void SDL_WM_SetCaption(const char*,const char*);void SDL_WM_SetIcon(SDL_Surface*,Uint8*);Uint8*SDL_GetKeyState(int*);Uint8 SDL_GetMouseState(int*,int*);SDL_Joystick*SDL_JoystickOpen(int);Sint16 SDL_JoystickGetAxis(SDL_Joystick*,int);Uint8 SDL_JoystickGetButton(SDL_Joystick*,int);SDL_RWops*SDL_RWFromFP(FILE*,int);SDL_RWops*SDL_RWFromConstMem(const void*,int);int SDL_RWclose(SDL_RWops*);void SDL_FreeRW(SDL_RWops*);int SDL_OpenAudio(SDL_AudioSpec*,SDL_AudioSpec*);void SDL_CloseAudio(void);void SDL_PauseAudio(int);void SDL_MixAudio(Uint8*,const Uint8*,Uint32,int);void SDL_LockAudio(void);void SDL_UnlockAudio(void); SDL_Overlay*SDL_CreateYUVOverlay(int,int,Uint32,SDL_Surface*);void SDL_FreeYUVOverlay(SDL_Overlay*);int SDL_LockYUVOverlay(SDL_Overlay*);void SDL_UnlockYUVOverlay(SDL_Overlay*);int SDL_DisplayYUVOverlay(SDL_Overlay*,SDL_Rect*);
#endif
/* Additional host-only WAV stubs used by RG35XX experimental audio mode. */
SDL_AudioSpec *SDL_LoadWAV(const char *,SDL_AudioSpec *,Uint8 **,Uint32 *);
void SDL_FreeWAV(Uint8 *);
