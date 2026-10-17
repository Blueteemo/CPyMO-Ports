/* 16-bit pixel format scaler for original RG35XX 640x480 SDL framebuffer.
 * Verified against v0.6 scalar reference using randomized dimensions/strides.
 * Does not reinterpret RGB565 bits: source and LCD masks must already match.
 * Uses a destination-column map so nearest-neighbor division is O(width),
 * not O(width*height). Identical upscaled rows are copied from prior rows.
 */
#include "rg35xx_scale565.h"
#include <stddef.h>
#include <stdint.h>

#define RG35XX_LCD_MAX_W 640

int rg35xx_viewport_fit(int sw,int sh,int dw,int dh,rg35xx_viewport *out){
 if(!out||sw<=0||sh<=0||dw<=0||dh<=0)return 0;
 int w,h;
 if((int64_t)sw*dh>(int64_t)dw*sh){w=dw;h=(int)((int64_t)sh*dw/sw);}
 else{h=dh;w=(int)((int64_t)sw*dh/sh);}
 if(w<1)w=1;
 if(h<1)h=1;
 out->w=w;out->h=h;out->x=(dw-w)/2;out->y=(dh-h)/2;
 return 1;
}
int rg35xx_scale16(const void *src,int sw,int sh,int sp,void *dst,int dw,int dh,int dp){
 if(!src||!dst||sw<=0||sh<=0||dw<=0||dh<=0||sp<0||dp<0)return 0;
 if((int64_t)sw*2>sp||(int64_t)dw*2>dp||sp%2||dp%2)return 0;
 rg35xx_viewport v;
 if(!rg35xx_viewport_fit(sw,sh,dw,dh,&v))return 0;
 /* RG35XX is 640px wide. If reused on a larger framebuffer, take
  * a well-defined slower path without indexing beyond this table. */
 int xmap[RG35XX_LCD_MAX_W];
 if(v.w<=RG35XX_LCD_MAX_W){
  for(int x=0;x<v.w;++x)xmap[x]=(int)((int64_t)x*sw/v.w);
 }
 int last_sy=-1;
 for(int y=0;y<dh;++y){
  uint16_t *drow=(uint16_t *)((unsigned char *)dst+(size_t)y*dp);
  if(y<v.y || y>=v.y+v.h){
   for(int x=0;x<dw;++x)drow[x]=0;
   continue;
  }
  int sy=(int)((int64_t)(y-v.y)*sh/v.h);
  if(sy==last_sy && y>v.y){
   const uint16_t *prior=(const uint16_t *)((const unsigned char *)dst+(size_t)(y-1)*dp);
   for(int x=0;x<dw;++x)drow[x]=prior[x];
   continue;
  }
  last_sy=sy;
  for(int x=0;x<v.x;++x)drow[x]=0;
  const uint16_t *srow=(const uint16_t *)((const unsigned char *)src+(size_t)sy*sp);
  if(v.w<=RG35XX_LCD_MAX_W){
   for(int x=0;x<v.w;++x)drow[v.x+x]=srow[xmap[x]];
  }else{
   for(int x=0;x<v.w;++x)drow[v.x+x]=srow[(int)((int64_t)x*sw/v.w)];
  }
  for(int x=v.x+v.w;x<dw;++x)drow[x]=0;
 }
 return 1;
}
