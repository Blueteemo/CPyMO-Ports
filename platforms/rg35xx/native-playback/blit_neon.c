#include "blit.h"
#include <arm_neon.h>

static inline int16x8_t center_byte(uint8x8_t values)
{
    return vreinterpretq_s16_u16(vsubl_u8(values, vdup_n_u8(128)));
}

static inline uint8x8_t finish_color(int16x8_t shifted)
{
    /* Half-scale fixed point. Restore the luma bias while clamping at
       zero, then divide by 128 and clamp at 255. */
    enum { luma_bias = (128-16)*149 + 64 };
    uint16x8_t biased = veorq_u16(vreinterpretq_u16_s16(shifted), vdupq_n_u16(32768));
    return vqshrn_n_u16(vqsubq_u16(biased, vdupq_n_u16(32768-luma_bias)), 7);
}

static inline uint16x8_t convert_eight(uint8x8_t y, uint8x8_t u, uint8x8_t v)
{
    int16x8_t luma = vmulq_n_s16(center_byte(y), 149);
    int16x8_t cb = center_byte(u), cr = center_byte(v);
    /* Floor(409*cr/2) = 204*cr + floor(cr/2). All preliminary
       terms fit in int16. Saturation of the last addition can only
       affect values outside the representable RGB channel range. */
    int16x8_t red = vmlaq_n_s16(vshrq_n_s16(cr, 1), cr, 204);
    int16x8_t green = vmlaq_n_s16(vmulq_n_s16(cb, -50), cr, -104);
    int16x8_t blue_luma = vaddq_s16(luma, vshlq_n_s16(cb, 1));
    uint8x8_t r = finish_color(vqaddq_s16(luma, red));
    uint8x8_t g = finish_color(vqaddq_s16(luma, green));
    uint8x8_t b = finish_color(vqaddq_s16(blue_luma, vshlq_n_s16(cb, 8)));
    uint16x8_t rr = vshll_n_u8(vand_u8(r, vdup_n_u8(248)), 8);
    uint16x8_t gg = vshll_n_u8(vand_u8(g, vdup_n_u8(252)), 3);
    uint16x8_t bb = vmovl_u8(vshr_n_u8(b, 3));
    return vorrq_u16(vorrq_u16(rr, gg), bb);
}

void native_rgb565_neon(uint16_t *target, const uint8_t *y, const uint8_t *u,
    const uint8_t *v, unsigned width)
{
    unsigned x = 0;
    for (; x+8 <= width; x += 8)
        vst1q_u16(target+x, convert_eight(vld1_u8(y+x), vld1_u8(u+x), vld1_u8(v+x)));
    if (x != width) native_rgb565_scalar(target+x, y+x, u+x, v+x, width-x);
}

void native_rgb565_half_nv12_neon(uint16_t *target, const uint8_t *y,
    const uint8_t *uv, unsigned width)
{
    /* Both rows are already in CPU-owned memory. Width is a multiple of
       eight. Deinterleave both 16-byte inputs and retain even luma. */
    for (unsigned x = 0; x < width; x += 8) {
        uint8x8x2_t luma = vld2_u8(y+2*x), chroma = vld2_u8(uv+2*x);
        vst1q_u16(target+x, convert_eight(luma.val[0], chroma.val[0], chroma.val[1]));
    }
}
