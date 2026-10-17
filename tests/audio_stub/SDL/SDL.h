#ifndef RG35XX_TEST_ONLY_SDL_AUDIO
#define RG35XX_TEST_ONLY_SDL_AUDIO
/* HOST-ONLY TEST STUB; DO NOT USE FOR TARGET ARM COMPILATION. */
#include <stdint.h>
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
#define AUDIO_S16SYS 0x8010
#define SDL_MIX_MAXVOLUME 128
typedef struct SDL_AudioSpec {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;
int SDL_OpenAudio(SDL_AudioSpec *, SDL_AudioSpec *);
void SDL_PauseAudio(int);
void SDL_CloseAudio(void);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
void SDL_MixAudio(Uint8 *, const Uint8 *, Uint32, int);
SDL_AudioSpec *SDL_LoadWAV(const char *, SDL_AudioSpec *, Uint8 **, Uint32 *);
void SDL_FreeWAV(Uint8 *);
#endif
