#include "blit.h"
#include <stdbool.h>
#include <string.h>

#if defined(NATIVE_NEON) && defined(__arm__) && defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

static int clamp_byte(int value)
{
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

void native_rgb565_scalar(uint16_t *target, const uint8_t *y, const uint8_t *u,
    const uint8_t *v, unsigned width)
{
    for (unsigned x = 0; x < width; ++x) {
        int luma = 298*((int)y[x]-16);
        int cb = (int)u[x]-128;
        int cr = (int)v[x]-128;
        int r = clamp_byte((luma + 409*cr + 128) >> 8);
        int g = clamp_byte((luma - 100*cb - 208*cr + 128) >> 8);
        int b = clamp_byte((luma + 516*cb + 128) >> 8);
        target[x] = (uint16_t)((r & 248)*256 + (g & 252)*8 + b/8);
    }
}

void native_blit_setup(struct native_blitter *b, const struct video_image *source)
{
    if (NATIVE_WIDTH*source->height <= NATIVE_HEIGHT*source->width) {
        b->width = NATIVE_WIDTH;
        b->height = (NATIVE_WIDTH*source->height + source->width/2)/source->width;
    } else {
        b->height = NATIVE_HEIGHT;
        b->width = (NATIVE_HEIGHT*source->width + source->height/2)/source->height;
    }
    if (!b->width) b->width = 1;
    if (!b->height) b->height = 1;
    b->x = (NATIVE_WIDTH-b->width)/2;
    b->y = (NATIVE_HEIGHT-b->height)/2;
    for (unsigned x = 0; x < b->width; ++x) {
        unsigned column = x*source->width/b->width;
        b->columns[x] = column;
        b->chroma_columns[x] = column/2*source->chroma_step;
    }
    for (unsigned y = 0; y < b->height; ++y)
        b->rows[y] = y*source->height/b->height;
    b->convert_row = native_rgb565_scalar;
    b->backend = "cached_scalar";
    b->half_nv12 = 0;
#ifdef NATIVE_NEON
#if defined(__arm__) && defined(__linux__)
    if (!(getauxval(AT_HWCAP) & HWCAP_NEON)) return;
#endif
    b->convert_row = native_rgb565_neon;
    b->backend = "cached_neon";
    /* With exact 2:1 scaling, each output column maps to an even Y sample
       and the next UV pair. Use that mapping directly for interleaved UV. */
    b->half_nv12 = source->width == 2*b->width && source->height == 2*b->height &&
        b->width % 8 == 0 && source->chroma_step == 2 && source->v == source->u+1;
    if (b->half_nv12) b->backend = "cached_neon_half_nv12";
#endif
}

static void load_chroma(struct native_blitter *b, const struct video_image *source,
    unsigned row)
{
    size_t offset = row*source->chroma_stride;
    unsigned width = (source->width+1)/2;
    const uint8_t *cached_u = b->source_u, *cached_v = b->source_v;
    if (source->chroma_step == 2) {
        bool vu = source->u == source->v+1;
        memcpy(b->source_u, (vu ? source->v : source->u)+offset, width*2);
        cached_u = b->source_u+vu;
        cached_v = b->source_u+!vu;
    } else {
        memcpy(b->source_u, source->u+offset, width);
        memcpy(b->source_v, source->v+offset, width);
    }
    for (unsigned x = 0; x < b->width; ++x) {
        unsigned index = b->chroma_columns[x];
        b->row_u[x] = cached_u[index];
        b->row_v[x] = cached_v[index];
    }
}

void native_blit_frame(struct native_blitter *b, const struct video_image *source,
    uint16_t *target)
{
    /* DMA allocations can be uncached. Copy contiguous source rows before
       indexing them. Reuse scaled chroma for adjacent rows with the same
       source chroma row. The pixel loops only read cacheable memory. */
    memset(target, 0, b->y*NATIVE_WIDTH*sizeof(*target));
    unsigned bottom = b->y+b->height;
    memset(target + bottom*NATIVE_WIDTH, 0,
        (NATIVE_HEIGHT-bottom)*NATIVE_WIDTH*sizeof(*target));
#ifdef NATIVE_NEON
    if (b->half_nv12) {
        for (unsigned y = 0; y < b->height; ++y) {
            uint16_t *line = target + (b->y+y)*NATIVE_WIDTH;
            if (b->width < NATIVE_WIDTH) {
                memset(line, 0, b->x*sizeof(*line));
                memset(line+b->x+b->width, 0, (NATIVE_WIDTH-b->x-b->width)*sizeof(*line));
            }
            memcpy(b->source_y, source->y + 2*y*source->y_stride, source->width);
            memcpy(b->source_u, source->u + y*source->chroma_stride, source->width);
            native_rgb565_half_nv12_neon(line+b->x, b->source_y, b->source_u, b->width);
        }
        return;
    }
#endif
    unsigned previous_y = source->height, previous_uv = source->height;
    for (unsigned y = 0; y < b->height; ++y) {
        unsigned source_row = b->rows[y];
        uint16_t *line = target + (b->y+y)*NATIVE_WIDTH;
        if (source_row == previous_y) {
            memcpy(line, line-NATIVE_WIDTH, NATIVE_WIDTH*sizeof(*target));
            continue;
        }
        previous_y = source_row;
        memcpy(b->source_y, source->y + source_row*source->y_stride, source->width);
        for (unsigned x = 0; x < b->width; ++x)
            b->row_y[x] = b->source_y[b->columns[x]];
        if (source_row/2 != previous_uv) {
            previous_uv = source_row/2;
            load_chroma(b, source, previous_uv);
        }
        if (b->width < NATIVE_WIDTH) {
            memset(line, 0, b->x*sizeof(*line));
            memset(line+b->x+b->width, 0, (NATIVE_WIDTH-b->x-b->width)*sizeof(*line));
        }
        b->convert_row(line+b->x, b->row_y, b->row_u, b->row_v, b->width);
    }
}
