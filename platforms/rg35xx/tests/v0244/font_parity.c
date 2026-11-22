#define STB_TRUETYPE_IMPLEMENTATION
#include "../../source/CPyMO-main/stb/stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *load(const char *path)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END))return NULL;
    long n=ftell(f);if(n<1)return NULL;
    rewind(f);unsigned char *data=malloc((size_t)n);
    if(!data||fread(data,1,(size_t)n,f)!=(size_t)n)return NULL;
    fclose(f);return data;
}
int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    unsigned char *a=load(argv[1]),*b=load(argv[2]);
    stbtt_fontinfo first,second;
    if(!a||!b||!stbtt_InitFont(&first,a,0)||!stbtt_InitFont(&second,b,0))return 3;
    int asc[2],desc[2],gap[2];
    stbtt_GetFontVMetrics(&first,asc,desc,gap);
    stbtt_GetFontVMetrics(&second,asc+1,desc+1,gap+1);
    if(asc[0]!=asc[1]||desc[0]!=desc[1]||gap[0]!=gap[1])return 4;
    const float sizes[]={24.0f,31.79f,36.98f,38.4f,41.6f,48.0f};
    FILE *codes=fopen(argv[3],"r");if(!codes)return 2;
    unsigned cp,count=0,rasters=0;
    while(fscanf(codes,"%x",&cp)==1) {
        if(!stbtt_FindGlyphIndex(&first,cp)||!stbtt_FindGlyphIndex(&second,cp))return 5;
        int aa,ab,la,lb;stbtt_GetCodepointHMetrics(&first,cp,&aa,&la);
        stbtt_GetCodepointHMetrics(&second,cp,&ab,&lb);
        if(aa!=ab||la!=lb){fprintf(stderr,"metrics U+%04X\n",cp);return 6;}
        for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++)for(int shift=0;shift<4;shift++) {
            int wa,ha,xa,ya,wb,hb,xb,yb;
            float sa=stbtt_ScaleForPixelHeight(&first,sizes[i]),sb=stbtt_ScaleForPixelHeight(&second,sizes[i]);
            unsigned char *pa=stbtt_GetCodepointBitmapSubpixel(&first,sa,sa,shift*.25f,0,cp,&wa,&ha,&xa,&ya);
            unsigned char *pb=stbtt_GetCodepointBitmapSubpixel(&second,sb,sb,shift*.25f,0,cp,&wb,&hb,&xb,&yb);
            if(wa!=wb||ha!=hb||xa!=xb||ya!=yb||
                (wa*ha>0&&(!pa||!pb||memcmp(pa,pb,(size_t)wa*ha)))) {
                fprintf(stderr,"raster U+%04X size=%.2f shift=%d\n",cp,sizes[i],shift);return 7;
            }
            stbtt_FreeBitmap(pa,NULL);stbtt_FreeBitmap(pb,NULL);rasters++;
        }
        count++;
    }
    fclose(codes);
    for(int x=32;x<127;x++)for(int y=32;y<127;y++)
        if(stbtt_GetCodepointKernAdvance(&first,x,y)!=stbtt_GetCodepointKernAdvance(&second,x,y))return 8;
    printf("PASS codepoints=%u identical_rasters=%u sizes=6 subpixel_shifts=4 ascii_kern_pairs=9025\n",count,rasters);
    free(a);free(b);return 0;
}
