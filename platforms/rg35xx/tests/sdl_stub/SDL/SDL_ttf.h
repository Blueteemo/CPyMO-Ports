#include "SDL.h"
typedef struct TTF_Font TTF_Font;
int TTF_Init(void);void TTF_Quit(void);TTF_Font*TTF_OpenFont(const char*,int);void TTF_CloseFont(TTF_Font*);SDL_Surface* TTF_RenderUTF8_Solid(TTF_Font*,const char*,SDL_Color);SDL_Surface*TTF_RenderUTF8_Shaded(TTF_Font*,const char*,SDL_Color,SDL_Color);SDL_Surface*TTF_RenderUTF8_Blended(TTF_Font*,const char*,SDL_Color);
int TTF_SizeUTF8(TTF_Font*,const char*,int*,int*);
