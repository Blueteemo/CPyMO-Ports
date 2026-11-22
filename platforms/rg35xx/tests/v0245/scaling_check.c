/* Actual RG35XX load-time BOX filter versus independent double area integral. */
#include "../../source/CPyMO-main/cpymo-backends/libretro/libretro.c"
#include "../../source/CPyMO-main/cpymo-backends/include/cpymo_backend_image.h"
extern void cpymo_backend_image_scale_on_load(void **,int *,int *,size_t);
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static void ppm(const char *file,const uint8_t *p){FILE *f=fopen(file,"wb");fprintf(f,"P6\n640 480\n255\n");fwrite(p,3,640*480,f);fclose(f);}
int main(int argc,char **argv){
 REQUIRE(argc==2);FILE *f=fopen(argv[1],"rb");REQUIRE(f);
 uint8_t *src=malloc(800*600*3);REQUIRE(src);REQUIRE(fread(src,3,800*600,f)==800*600);fclose(f);
 REQUIRE(garlic_apply_canvas_scaled(800,600,1));
 uint8_t *candidate=malloc(800*600*3);REQUIRE(candidate);memcpy(candidate,src,800*600*3);int w=800,h=600;
 cpymo_backend_image_scale_on_load((void**)&candidate,&w,&h,3);REQUIRE(w==640&&h==480);
 double sum=0;unsigned max=0;size_t count=0;
 for(int y=0;y<480;y++)for(int x=0;x<640;x++)for(int c=0;c<3;c++){
  double x0=x*1.25,x1=(x+1)*1.25,y0=y*1.25,y1=(y+1)*1.25,value=0;
  for(int yy=y0;yy<(int)ceil(y1);yy++)for(int xx=x0;xx<(int)ceil(x1);xx++)
   value+=src[(yy*800+xx)*3+c]*(fmin(xx+1,x1)-fmax(xx,x0))*(fmin(yy+1,y1)-fmax(yy,y0))/1.5625;
  unsigned difference=abs((int)candidate[(y*640+x)*3+c]-(int)lround(value));if(difference>max)max=difference;sum+=difference;count++;
 }
 printf("BOX independent area max_error_byte=%u mean_error_byte=%.8f samples=%zu\n",max,sum/count,count);REQUIRE(max<=1);
 ppm("scale_area.png.ppm",candidate);
 uint8_t *linear=malloc(640*480*3),*nearest=malloc(640*480*3);REQUIRE(linear&&nearest);
 REQUIRE(stbir_resize_uint8_generic(src,800,600,0,linear,640,480,0,3,STBIR_ALPHA_CHANNEL_NONE,0,STBIR_EDGE_CLAMP,STBIR_FILTER_TRIANGLE,STBIR_COLORSPACE_LINEAR,NULL));
 for(int y=0;y<480;y++)for(int x=0;x<640;x++)memcpy(nearest+(y*640+x)*3,src+(((y*600/480)*800+x*800/640)*3),3);
 ppm("scale_linear.png.ppm",linear);ppm("scale_nearest.png.ppm",nearest);
 /* Transparent pixels contain magenta; alpha-aware filtering must not bleed it into a white silhouette. */
 uint8_t *rgba=malloc(800*600*4);REQUIRE(rgba);
 for(int y=0;y<600;y++)for(int x=0;x<800;x++){
  uint8_t *p=rgba+(y*800+x)*4;bool inside=abs(x-400)+abs(y-300)<230;
  p[0]=255;p[1]=inside?255:0;p[2]=255;p[3]=inside?255:0;
 }
 w=800;h=600;cpymo_backend_image_scale_on_load((void**)&rgba,&w,&h,4);unsigned edge=0;
 for(int i=0;i<w*h;i++)if(rgba[i*4+3]>1){REQUIRE(rgba[i*4]==255&&rgba[i*4+1]==255&&rgba[i*4+2]==255);if(rgba[i*4+3]<254)edge++;}
 REQUIRE(edge>100);printf("RGBA alpha-aware edge colour PASS edge_pixels=%u\n",edge);
 free(src);free(candidate);free(linear);free(nearest);free(rgba);retro_unload_game();return 0;
}
