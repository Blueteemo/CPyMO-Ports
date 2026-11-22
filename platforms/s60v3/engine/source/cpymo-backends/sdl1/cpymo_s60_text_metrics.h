#ifndef CPYMO_S60_TEXT_METRICS_H
#define CPYMO_S60_TEXT_METRICS_H

#include "../include/cpymo_backend_text.h"

/* Left, top, right and bottom relative to the draw origin and baseline. */
void cpymo_s60_text_ink(cpymo_backend_text text, float bounds[4]);
void cpymo_s60_text_draw_plain(cpymo_backend_text text, float x, float baseline, cpymo_color color);
float cpymo_s60_font_baseline(float height, bool dialogue);

error_t cpymo_s60_dialogue_text_create(cpymo_backend_text *, float *, cpymo_str, float);
float cpymo_s60_dialogue_text_width(cpymo_str, float);
#endif
