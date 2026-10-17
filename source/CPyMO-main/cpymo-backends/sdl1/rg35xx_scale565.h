#ifndef RG35XX_SCALE565_H
#define RG35XX_SCALE565_H
#include <stdint.h>
typedef struct {int x,y,w,h;} rg35xx_viewport;
/* Returns 0 for invalid dimensions; otherwise 1. Centered, no cropping. */
int rg35xx_viewport_fit(int src_w, int src_h, int dst_w, int dst_h, rg35xx_viewport *out);
/* RGB565-or-screen-native-16-bit pixels; formats must match. All pitches in bytes.
 * Black-fills padding as well. Caller must lock SDL surfaces before calling. */
int rg35xx_scale16(const void *src,int sw,int sh,int src_pitch,
                   void *dst,int dw,int dh,int dst_pitch);
#endif
