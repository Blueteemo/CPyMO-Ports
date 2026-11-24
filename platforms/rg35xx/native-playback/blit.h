#ifndef RG35XX_NATIVE_PLAYBACK_BLIT_H
#define RG35XX_NATIVE_PLAYBACK_BLIT_H

#include "shared.h"
#include "video.h"

enum { NATIVE_MAX_SOURCE_WIDTH = 1920 };

typedef void (*native_rgb565_row)(uint16_t *, const uint8_t *, const uint8_t *,
    const uint8_t *, unsigned);

struct native_blitter {
    unsigned width, height, x, y;
    uint16_t columns[NATIVE_WIDTH], chroma_columns[NATIVE_WIDTH], rows[NATIVE_HEIGHT];
    _Alignas(16) uint8_t source_y[NATIVE_MAX_SOURCE_WIDTH];
    _Alignas(16) uint8_t source_u[NATIVE_MAX_SOURCE_WIDTH];
    _Alignas(16) uint8_t source_v[NATIVE_MAX_SOURCE_WIDTH/2];
    _Alignas(16) uint8_t row_y[NATIVE_WIDTH], row_u[NATIVE_WIDTH], row_v[NATIVE_WIDTH];
    native_rgb565_row convert_row;
    const char *backend;
    unsigned half_nv12;
};

void native_blit_setup(struct native_blitter *, const struct video_image *);
void native_blit_frame(struct native_blitter *, const struct video_image *, uint16_t *);
void native_rgb565_scalar(uint16_t *, const uint8_t *, const uint8_t *, const uint8_t *, unsigned);
#ifdef NATIVE_NEON
void native_rgb565_neon(uint16_t *, const uint8_t *, const uint8_t *, const uint8_t *, unsigned);
void native_rgb565_half_nv12_neon(uint16_t *, const uint8_t *, const uint8_t *, unsigned);
#endif

#endif
