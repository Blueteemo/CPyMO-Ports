/* Exact former mask blend versus the production lookup path, and bounded LRU
 * ownership under eviction/clear. Included by the ASan render harness. */
#include "../../source/CPyMO-main/cpymo-backends/include/cpymo_backend_masktrans.h"
#include "../../source/CPyMO-main/cpymo/garlic_image_cache.h"
static void graphics_checks(void)
{
    cpymo_backend_software_context saved=soft_context;
    uint8_t *pixels=malloc(256*256*3),*mask=malloc(256*256);CHECK(pixels && mask);
    cpymo_backend_software_image target={0};
    target.w=target.h=256;target.line_stride=768;target.pixel_stride=3;
    target.g_offset=1;target.b_offset=2;target.pixels=pixels;
    soft_context.render_target=&target;soft_context.scale_on_load_image=false;
    for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++)mask[y*256+x]=x;
    cpymo_backend_masktrans m;CHECK(cpymo_backend_masktrans_create(&m,mask,256,256)==0);
    for(unsigned fade=0;fade<2;fade++)for(unsigned step=0;step<=64;step++){
        float t=step/64.0f,at=fade?t:1-t;
        for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){
            unsigned i=(y*256+x)*3;pixels[i]=y;pixels[i+1]=255-y;pixels[i+2]=(x+y)&255;
        }
        cpymo_backend_masktrans_draw(m,t,fade);
        for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){
            unsigned b=(unsigned)(((float)x/256)*255);
            float a=cpymo_backend_software_u8f[b];if(!fade)a=1-a;
            if(a>at+.25f)a=1;else if(a<at-.25f)a=0;else a=(a-(at-.25f))/.5f;
            unsigned original[3]={y,255-y,(x+y)&255};
            for(unsigned c=0;c<3;c++){
                uint8_t expected=a==0?original[c]:a==1?0:
                    (uint8_t)((0*a+cpymo_backend_software_u8f[original[c]]*(1-a))*255);
                CHECK(pixels[(y*256+x)*3+c]==expected);
            }
        }
    }
    cpymo_backend_masktrans_free(m);
    target.w=target.h=1;target.line_stride=3;
    soft_context.logical_screen_w=soft_context.logical_screen_h=1;
    uint8_t texel[4]={217,69,131,0};
    cpymo_backend_software_image sprite={0};
    sprite.w=sprite.h=sprite.source_w=sprite.source_h=1;
    sprite.line_stride=sprite.pixel_stride=4;sprite.g_offset=1;sprite.b_offset=2;
    sprite.a_offset=3;sprite.has_alpha_channel=true;sprite.pixels=texel;
    unsigned max_error=0;
    for(unsigned ga=0;ga<=256;ga++)for(unsigned sa=0;sa<256;sa++){
        float alpha=ga/256.0f,a=cpymo_backend_software_u8f[sa]*alpha;
        uint8_t before[3]={ga&255,sa,((ga+sa)*17)&255};
        memcpy(pixels,before,3);texel[3]=sa;
        cpymo_backend_image_draw(0,0,1,1,&sprite,0,0,1,1,alpha,cpymo_backend_image_draw_type_chara);
        for(unsigned c=0;c<3;c++){
            uint8_t expected=(uint8_t)((cpymo_backend_software_u8f[texel[c]]*a+cpymo_backend_software_u8f[before[c]]*(1-a))*255);
            unsigned error=abs((int)pixels[c]-expected);if(error>max_error)max_error=error;CHECK(error<=1);
        }
        memcpy(pixels,before,3);float rect[]={0,0,1,1};cpymo_color col={217,69,131};
        cpymo_backend_image_fill_rects(rect,1,col,alpha,cpymo_backend_image_draw_type_ui_bg);
        for(unsigned c=0;c<3;c++){
            uint8_t expected=(uint8_t)((cpymo_backend_software_u8f[texel[c]]*alpha+cpymo_backend_software_u8f[before[c]]*(1-alpha))*255);
            CHECK(abs((int)pixels[c]-expected)<=1);
        }
    }
    printf("Q16 image/fill versus former float max_error_byte=%u PASS\n",max_error);
    free(pixels);soft_context=saved;
    garlic_image_cache_clear();cpymo_backend_image first=NULL;
    for(unsigned i=0;i<40;i++){
        uint8_t *p=calloc(640*480,3);CHECK(p);
        cpymo_backend_image im;CHECK(cpymo_backend_image_load(&im,p,640,480,cpymo_backend_image_format_rgb)==0);
        char name[40];snprintf(name,sizeof(name),"ownership-%u",i);
        garlic_image_cache_put(im,640,480,"test","bg",cpymo_str_pure(name),false);
        if(i==0)first=im;else cpymo_backend_image_free(im);
        size_t bytes;unsigned hits,miss;garlic_image_cache_counts(&bytes,&hits,&miss);CHECK(bytes<=12*1024*1024);
    }
    /* First was evicted but its engine owner must still be valid. */
    CHECK(((cpymo_backend_software_image*)first)->pixels[0]==0);
    garlic_image_cache_clear();CHECK(((cpymo_backend_software_image*)first)->references==1);
    cpymo_backend_image_free(first);
    puts("MASK bit-identical across all mask/destination bytes, 65 times, both directions; cache eviction/live-owner clear PASS");
}
