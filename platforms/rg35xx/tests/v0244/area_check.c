#include "../../source/CPyMO-main/cpymo-backends/garlicos/garlic_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned component(uint16_t p,int c)
{ return c==0 ? p&31 : c==1 ? (p>>5)&63 : p>>11; }
static void reference(const uint8_t *src,int sw,int sh,int x,int y,int w,int h,double out[3])
{
    double x0=(double)x*sw/w,x1=(double)(x+1)*sw/w;
    double y0=(double)y*sh/h,y1=(double)(y+1)*sh/h;
    out[0]=out[1]=out[2]=0;
    for(int j=(int)y0;j<(int)ceil(y1);j++) for(int i=(int)x0;i<(int)ceil(x1);i++) {
        double weight=(fmin(i+1,x1)-fmax(i,x0))*(fmin(j+1,y1)-fmax(j,y0));
        const uint8_t *p=src+((size_t)j*sw+i)*4;
        for(int c=0;c<3;c++) out[c]+=p[c]*weight/((x1-x0)*(y1-y0));
    }
}
int main(int argc,char **argv)
{
    const int dims[][2]={{320,240},{540,360},{640,480},{800,600},{1280,720},{2048,2048},{640,2048}};
    size_t pitch=640*2+16;
    uint8_t *storage=malloc(pitch*480),*old_storage=malloc(pitch*480);
    CHECK(storage && old_storage);
    for(size_t d=0;d<sizeof(dims)/sizeof(dims[0]);d++) {
        int sw=dims[d][0],sh=dims[d][1];
        uint8_t *src=malloc((size_t)sw*sh*4);CHECK(src);
        for(int y=0;y<sh;y++) for(int x=0;x<sw;x++) {
            uint8_t *p=src+((size_t)y*sw+x)*4;
            p[0]=(x*7+y*11)%256; p[1]=(x*13+y*17)%256; p[2]=(x*19+y*23)%256;p[3]=255;
        }
        memset(storage,0xa5,pitch*480);memset(old_storage,0xa5,pitch*480);
        CHECK(garlic_blit_rgb565_quality((uint16_t *)storage,pitch,640,480,src,sw*4,sw,sh));
        CHECK(garlic_blit_rgb565((uint16_t *)old_storage,pitch,640,480,src,sw*4,sw,sh));
        garlic_viewport v;CHECK(garlic_compute_viewport(sw,sh,640,480,&v));
        for(int y=0;y<480;y++) for(size_t k=1280;k<pitch;k++) CHECK(storage[y*pitch+k]==0xa5);
        if(sw<=v.w && sh<=v.h) CHECK(!memcmp(storage,old_storage,pitch*480));
        else for(int y=0;y<v.h;y+=13) for(int x=0;x<v.w;x+=11) {
            double rgb[3];reference(src,sw,sh,x,y,v.w,v.h,rgb);
            unsigned b=(unsigned)lround(rgb[0])>>3,g=(unsigned)lround(rgb[1])>>2,r=(unsigned)lround(rgb[2])>>3;
            uint16_t p=*(uint16_t *)(storage+(y+v.y)*pitch+(x+v.x)*2);
            CHECK(abs((int)component(p,0)-(int)b)<=1);
            CHECK(abs((int)component(p,1)-(int)g)<=1);
            CHECK(abs((int)component(p,2)-(int)r)<=1);
        }
        for(size_t k=0;k<(size_t)sw*sh;k++) {src[k*4]=31;src[k*4+1]=127;src[k*4+2]=239;}
        CHECK(garlic_blit_rgb565_quality((uint16_t *)storage,pitch,640,480,src,sw*4,sw,sh));
        uint16_t solid=(uint16_t)((239>>3)<<11|(127>>2)<<5|(31>>3));
        for(int y=0;y<480;y+=7)for(int x=0;x<640;x+=7) {
            uint16_t p=*(uint16_t *)(storage+y*pitch+x*2);
            CHECK(p==(x>=v.x&&x<v.x+v.w&&y>=v.y&&y<v.y+v.h ? solid:0));
        }
        printf("AREA canvas=%dx%d viewport=%d,%d,%d,%d PASS\n",sw,sh,v.x,v.y,v.w,v.h);
        free(src);
    }
    CHECK(!garlic_blit_rgb565_quality(NULL,pitch,640,480,storage,pitch,640,480));
    CHECK(!garlic_blit_rgb565_quality((uint16_t *)storage,1,640,480,storage,pitch,640,480));
    CHECK(!garlic_blit_rgb565_quality((uint16_t *)storage,pitch,0,480,storage,pitch,640,480));
    CHECK(!garlic_blit_rgb565_quality((uint16_t *)storage,pitch,640,480,storage,SIZE_MAX,640,480));
    if(argc==3) {
        FILE *input=fopen(argv[1],"rb");CHECK(input);
        uint8_t *src=malloc(800*600*4);CHECK(src);
        CHECK(fread(src,1,800*600*4,input)==800*600*4);fclose(input);
        for(int filter=0;filter<2;filter++) {
            CHECK((filter?garlic_blit_rgb565_quality:garlic_blit_rgb565)(
                (uint16_t *)storage,pitch,640,480,src,800*4,800,600));
            char name[2048];snprintf(name,sizeof(name),"%s_%s.ppm",argv[2],filter?"area":"nearest");
            FILE *out=fopen(name,"wb");CHECK(out);fprintf(out,"P6\n640 480\n255\n");
            for(int y=0;y<480;y++)for(int x=0;x<640;x++) {
                uint16_t p=*(uint16_t *)(storage+y*pitch+x*2);
                unsigned r=p>>11,g=(p>>5)&63,b=p&31;
                unsigned char rgb[]={(r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)};
                CHECK(fwrite(rgb,1,3,out)==3);
            }
            CHECK(!fclose(out));
        }
        free(src);
    }
    free(storage);free(old_storage);
    printf("PASS %u area/reference/identity/padding checks\n",checks);
    return 0;
}
