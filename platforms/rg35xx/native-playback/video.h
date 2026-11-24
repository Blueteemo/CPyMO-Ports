#ifndef RG35XX_NATIVE_PLAYBACK_VIDEO_H
#define RG35XX_NATIVE_PLAYBACK_VIDEO_H

#include <stdint.h>

struct video_decoder;
struct video_image {
    const uint8_t *y, *u, *v;
    unsigned y_stride, chroma_stride, chroma_step;
    unsigned width, height;
    unsigned pts_ms;
};

typedef int (*video_sink)(void *, const struct video_image *);

struct video_decoder *video_open(const uint8_t *, unsigned);
int video_packet(struct video_decoder *, const uint8_t *, unsigned, unsigned,
    video_sink, void *);
void video_close(struct video_decoder *);

#endif
