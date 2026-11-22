#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_utils.h"
#include "../include/cpymo_backend_masktrans.h"
#include "cpymo_backend_software.h"
#include <stddef.h>
#include <stdlib.h>

extern void cpymo_backend_image_scale_on_load(
    void **pixels, int *width, int *height, size_t channels);

#ifdef GARLIC_NO_FFMPEG
/* Immutable mask plus reusable coordinate maps and an exact byte shade table.
 * The table preserves the former float blend for every mask/destination byte;
 * floating-point work is per palette entry rather than per screen pixel. */
typedef struct {
    cpymo_backend_software_image image;
    size_t *lookup, target_w, target_h;
    uint8_t shade[256][256], state[256];
    float table_t;
    bool table_valid, table_fade_in;
} garlic_masktrans;
static bool garlic_masktrans_maps(garlic_masktrans *m,size_t w,size_t h)
{
    if(m->lookup && m->target_w==w && m->target_h==h)return true;
    if(!w || !h || w>2048 || h>2048)return false;
    size_t *lookup=malloc((w+h)*sizeof(*lookup));if(!lookup)return false;
    for(size_t x=0;x<w;x++)lookup[x]=(size_t)(((float)x/(float)w)*((float)m->image.w-1));
    for(size_t y=0;y<h;y++)lookup[w+y]=(size_t)(((float)y/(float)h)*((float)m->image.h-1));
    free(m->lookup);m->lookup=lookup;m->target_w=w;m->target_h=h;return true;
}
#endif

error_t cpymo_backend_masktrans_create(
    cpymo_backend_masktrans *out,
    void *mask_singlechannel_moveinto,
    int w, int h)
{
#ifdef GARLIC_NO_FFMPEG
    garlic_masktrans *owner=calloc(1,sizeof(*owner));
    cpymo_backend_software_image *img=owner ? &owner->image : NULL;
#else
    cpymo_backend_software_image *img=malloc(sizeof(*img));
#endif
    /* Allocate ownership before resizing can consume the caller's pixels. */
    if(!img)return CPYMO_ERR_OUT_OF_MEM;
    img->source_w=w;img->source_h=h;img->references=1;
    cpymo_backend_image_scale_on_load(&mask_singlechannel_moveinto,&w,&h,1);
    img->r_offset = 0;
    img->g_offset = 0;
    img->b_offset = 0;
    img->a_offset = 0;
    img->w = w;
    img->h = h;
    img->has_alpha_channel = true;
    img->line_stride = w;
    img->pixel_stride = 1;
    img->pixels = (uint8_t *)mask_singlechannel_moveinto;

    *out = img;
    return CPYMO_ERR_SUCC;
}

void cpymo_backend_masktrans_free(cpymo_backend_masktrans m)
{
    cpymo_backend_software_image *img =
        (cpymo_backend_software_image *)m;
#ifdef GARLIC_NO_FFMPEG
    free(((garlic_masktrans*)m)->lookup);
#endif
    free(img->pixels);
    free(img);
}

void cpymo_backend_masktrans_draw(
    cpymo_backend_masktrans m,
    float t, bool is_fade_in)
{
    extern cpymo_backend_software_context
        *cpymo_backend_software_cur_context;

    cpymo_backend_software_image *render_target =
        cpymo_backend_software_cur_context->render_target;

    if (!is_fade_in) t = 1.0f - t;

    const float radius = 0.25f;
    float t_top = t + radius;
	float t_bottom = t - radius;

    /* The full-screen fade touches every pixel of an 800x600 canvas, and v0.21
     * recomputed `(float)x / (float)width` (and the sample's own index
     * mapping) inside the loop: at least two float divisions per pixel. The
     * mask is a single-channel image, so one table per source column and one
     * per source row covers every lookup here. */
    const cpymo_backend_software_image *mask_img =
        (cpymo_backend_software_image *)m;

#ifdef GARLIC_NO_FFMPEG
    {
        garlic_masktrans *owned=m;
        if(!garlic_masktrans_maps(owned,render_target->w,render_target->h))return;
        if(!owned->table_valid || owned->table_t!=t || owned->table_fade_in!=is_fade_in){
            for(unsigned b=0;b<256;b++){
                float a=cpymo_backend_software_u8f[b];
                if(!is_fade_in)a=1.0f-a;
                if(a>t_top)a=1.0f;
                else if(a<t_bottom)a=0.0f;
                else a=(a-t_bottom)/(2*radius);
                owned->state[b]=a==0 ? 0 : a==1 ? 1 : 2;
                if(owned->state[b]==2)for(unsigned v=0;v<256;v++)
                    owned->shade[b][v]=(uint8_t)((cpymo_backend_software_u8f[v]*(1.0f-a))*255.0f);
            }
            owned->table_valid=true;owned->table_t=t;owned->table_fade_in=is_fade_in;
        }
        size_t *col=owned->lookup,*row=col+render_target->w;
        for(size_t y=0;y<render_target->h;y++){
            const uint8_t *maskrow=mask_img->pixels+row[y]*mask_img->line_stride;
            uint8_t *dst=render_target->pixels+y*render_target->line_stride;
            for(size_t x=0;x<render_target->w;x++,dst+=render_target->pixel_stride){
                unsigned b=maskrow[col[x]],kind=owned->state[b];
                if(!kind)continue;
                if(kind==1){dst[render_target->r_offset]=dst[render_target->g_offset]=dst[render_target->b_offset]=0;continue;}
                const uint8_t *shade=owned->shade[b];
                dst[render_target->r_offset]=shade[dst[render_target->r_offset]];
                dst[render_target->g_offset]=shade[dst[render_target->g_offset]];
                dst[render_target->b_offset]=shade[dst[render_target->b_offset]];
            }
        }
        return;
    }
#endif
    size_t *col = (size_t *)malloc(render_target->w * sizeof(*col));
    size_t *row = (size_t *)malloc(render_target->h * sizeof(*row));
    if (!col || !row) {
        free(col);
        free(row);
        return;
    }

    for (size_t x = 0; x < render_target->w; ++x) {
        float u = (float)x / (float)render_target->w;
        u = cpymo_utils_clampf(u, 0.0f, 1.0f);
        col[x] = (size_t)(u * ((float)mask_img->w - 1.0f));
    }
    for (size_t y = 0; y < render_target->h; ++y) {
        float v = (float)y / (float)render_target->h;
        v = cpymo_utils_clampf(v, 0.0f, 1.0f);
        row[y] = (size_t)(v * ((float)mask_img->h - 1.0f));
    }

    const uint8_t *mask_pixels = mask_img->pixels;
    const size_t mask_stride = mask_img->line_stride;
    /* CPYMO_BACKEND_SOFTWARE_IMAGE_PIXEL steps x by pixel_stride and then adds
     * a_offset; for a 1-channel mask both are 1 and 0. */
    const size_t mask_pitch = mask_img->pixel_stride;
    const size_t mask_ao = mask_img->a_offset;

    for (size_t y = 0; y < render_target->h; ++y) {
        const uint8_t *mask_row = mask_pixels + row[y] * mask_stride;
        for (size_t x = 0; x < render_target->w; ++x) {
            /* v0.21 read this through cpymo_backend_software_image_sample_nearest
             * with a 1-channel image, i.e. the same byte divided by 255.0f,
             * so the table read is identical. */
            float mask = cpymo_backend_software_u8f[
                mask_row[col[x] * mask_pitch + mask_ao]];

            if (!is_fade_in) mask = 1.0f - mask;

            if (mask > t_top) mask = 1.0f;
			else if (mask < t_bottom) mask = 0.0f;
			else mask = (mask - t_bottom) / (2 * radius);

            /* mask == 1.0f is the most common state of a fade (and mask == 0
             * leaves the destination alone), so skip the per-channel work. */
            if (mask == 0.0f) continue;
            if (mask == 1.0f) {
                cpymo_backend_software_image_write_rgb_opaque(
                    render_target, x, y, 0, 0, 0);
                continue;
            }

            cpymo_backend_software_image_write_blend(
                render_target, x, y, 0, 0, 0, mask);
        }
    }

    free(col);
    free(row);
}
