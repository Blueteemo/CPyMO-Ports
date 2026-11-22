#ifndef CPYMO_S60_IMAGE_H
#define CPYMO_S60_IMAGE_H

#include "../include/cpymo_backend_image.h"
#include "../include/cpymo_backend_masktrans.h"

/* Reserve space for the clock when the skin has a separate flat title strip. */
void cpymo_s60_move_menu_title(cpymo_backend_image image, int header_height);

bool cpymo_s60_mask_crossfade(cpymo_backend_masktrans mask,
    cpymo_backend_image incoming, float dst_x, float dst_y, float progress);

#endif
