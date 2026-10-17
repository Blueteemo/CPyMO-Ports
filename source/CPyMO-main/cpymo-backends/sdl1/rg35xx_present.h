#ifndef CPYMO_RG35XX_PRESENT_H
#define CPYMO_RG35XX_PRESENT_H
#include <SDL/SDL.h>
/* Returns zero only after a complete scaler transfer and SDL_Flip.
 * Errors are fatal for this frame and propagated to the main game loop.
 * Both surfaces are unlocked on ALL exit paths after successful locks.
 */
int rg35xx_present(SDL_Surface *logical, SDL_Surface *lcd);
#endif
