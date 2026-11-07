/* Includes the exact backend to test its private RGB565 compositor. Link the
 * other core objects, excluding libretro.c and cpymo_msgbox_ui.c. Host only. */
#include "../../source/CPyMO-main/cpymo-backends/libretro/libretro.c"
#include "../../source/CPyMO-main/cpymo-backends/software/cpymo_backend_text.c"
#include "../../source/CPyMO-main/cpymo/cpymo_msgbox_ui.c"
#include "../../source/CPyMO-main/cpymo/cpymo_backlog.c"
#include "../../source/CPyMO-main/cpymo/cpymo_config_ui.c"
#include "../../source/CPyMO-main/cpymo/cpymo_textbox.c"
static unsigned checks;
static void quiet_log(enum retro_log_level l,const char *f,...) {(void)l;(void)f;}
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#include "graphics_checks.h"
#include "extra_render.h"
int main(int argc,char **argv) {
    CHECK(argc==2);
    CHECK(cpymo_backend_font_try_load_font(argv[1])==CPYMO_ERR_SUCC);
    CHECK(garlic_apply_canvas_scaled(540,360,1));
    uint16_t *f=calloc(640*480,2); CHECK(f);
    for(unsigned a=0;a<256;++a) for(unsigned c=0;c<256;++c) {
        unsigned d=(a*103+c*283)&65535, r=c,g=255-c,b=(c*17)&255;
        unsigned dr=(((d>>11)&31)<<3)|((d>>11)&31)>>2;
        unsigned dg=(((d>>5)&63)<<2)|((d>>5)&63)>>4;
        unsigned db=((d&31)<<3)|(d&31)>>2;
        unsigned rr=(unsigned)lround((r*a+dr*(255-a))/255.0);
        unsigned gg=(unsigned)lround((g*a+dg*(255-a))/255.0);
        unsigned bb=(unsigned)lround((b*a+db*(255-a))/255.0);
        f[0]=(uint16_t)d; garlic_text_layer[0]=(a<<24)|(b<<16)|(g<<8)|r;
        soft_context.text_dirty_y0=0;soft_context.text_dirty_y1=1;
        garlic_text_layer_composite(f);
        CHECK(f[0]==((rr>>3)<<11|(gg>>2)<<5|bb>>3));
    }
    const char *labels[]={"确定","取消","OK","Cancel"};
    const int sizes[][2]={{320,240},{540,360},{800,600}};
    for(unsigned si=0;si<3;++si) for(unsigned large=0;large<2;++large) {
        int w=sizes[si][0],h=sizes[si][1];
        CHECK(garlic_apply_canvas_scaled(w,h,1));
        float fs=26/240.0f*h*0.8f*(large?0.80f:0.72f);
        for(unsigned li=0;li<4;++li) {
            cpymo_backend_text t;float tw;
            CHECK(cpymo_backend_text_create(&t,&tw,cpymo_str_pure(labels[li]),fs)==CPYMO_ERR_SUCC);
            memset(garlic_text_layer,0,640*480*4);
            float rect[4];cpymo_msgbox_ui_get_btn_rect(rect,tw,fs,w,h*0.5f);
            cpymo_msgbox_ui_draw_btn(t,tw,tw,fs,w,h,h*0.5f,false,false);
            int miny=480,maxy=-1;
            for(int y=0;y<480;++y)for(int x=0;x<640;++x)if(garlic_text_layer[y*640+x]>>24) {
                if(y<miny)miny=y;if(y>maxy)maxy=y;
            }
            float target=soft_context.text_off_y+(rect[1]+(rect[3]-1)/2)*soft_context.text_scale;
            CHECK(maxy>=miny);CHECK(fabsf((miny+maxy)*0.5f-target)<=1.5f);
            CHECK(rect[1]==h*0.5f); /* no popup or button-group displacement */
            cpymo_backend_text_free(t);
        }
    }
    CHECK(garlic_apply_canvas_scaled(540,360,1));
    /* Actual output-resolution ink and RGB565 frame, same text and baseline. */
    for (unsigned large=0; large<2; ++large) {
        float fs=31.2f*(large?0.80f:0.72f),tw;
        cpymo_backend_text t;
        CHECK(cpymo_backend_text_create(&t,&tw,cpymo_str_pure("国水仙文字 ABC"),fs)==0);
        memset(garlic_text_layer,0,640*480*4);
        cpymo_backend_text_draw(t,10,180,cpymo_color_white,1,
            cpymo_backend_image_draw_type_text_say);
        int x0=640,y0=480,x1=0,y1=0;
        for(int y=0;y<480;y++)for(int x=0;x<640;x++)if((garlic_text_layer[y*640+x]>>24)>127){
            if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;
        }
        printf("INK large=%u bbox=%d,%d,%d,%d width=%.5f fontsize=%.5f\n",large,x0,y0,x1+1,y1+1,tw,fs);
        for (size_t k=0;k<640*480;k++) f[k]=0x1082;
        garlic_text_layer_composite(f);
        unsigned char *rgb=malloc(640*480*3); CHECK(rgb);
        for (size_t k=0;k<640*480;k++) {
            unsigned v=f[k],r=(v>>11)&31,g=(v>>5)&63,b=v&31;
            rgb[k*3]=(r<<3)|(r>>2); rgb[k*3+1]=(g<<2)|(g>>4); rgb[k*3+2]=(b<<3)|(b>>2);
        }
        CHECK(stbi_write_png(large?"ui2_font_default.png":"ui2_font_small.png",640,480,3,rgb,640*3));
        free(rgb);
        cpymo_backend_text_free(t);
    }
    /* Real glyph bounds for three explicit lines. The old baseline-at-bottom
       layout clipped descenders; paginate rather than clip when needed. */
    for(unsigned si=0;si<3;si++) for(unsigned large=0;large<2;large++) {
        int w=sizes[si][0],h=sizes[si][1];
        CHECK(garlic_apply_canvas_scaled(w,h,1));
        float size=26/240.0f*h*.8f*(large?.80f:.72f);
        cpymo_textbox box;
        const char *text="第一行 gjpq\\n第二行 gjpq\\n第三行 gjpq";
        CHECK(cpymo_textbox_init(&box,10,h-3*size,w-20,3*size,size,
            cpymo_color_white,1,cpymo_str_pure(text),NULL)==0);
        cpymo_engine draw_engine={0};unsigned pages=0,glyphs=0;
        do {
            cpymo_textbox_finalize(&box);pages++;
            memset(garlic_text_layer,0,640*480*4);
            cpymo_textbox_draw(&draw_engine,&box,cpymo_backend_image_draw_type_text_say);
            for(size_t l=0;l<=box.active_line;l++) {
                const cpymo_textbox_line *line=box.lines+l;
                for(size_t j=0;j<line->pool_slice_size;j++){
                    size_t k=line->begin_pool_index+j;glyphs++;
                    cpymo_backend_text_impl *g=box.chars_pool[k];
                    int py=(int)(soft_context.text_off_y+line->y*soft_context.text_scale-g->baseline-g->pad_y);
                    for(size_t yy=0;yy<g->h;yy++)for(size_t xx=0;xx<g->w;xx++)if(g->px[yy*g->w+xx]) {
                        CHECK(py+(int)yy>=0);CHECK(py+(int)yy+1<480);
                    }
                }
            }
            if(!box.remain_text.len)break;
            CHECK(cpymo_textbox_clear_page(&box,NULL)==0);
        }while(pages<8);
        CHECK(!box.remain_text.len && glyphs>12 && pages>=2);
        printf("LAYOUT canvas=%dx%d large=%u pages=%u glyphs=%u safe_bottom=PASS\n",w,h,large,pages,glyphs);
        cpymo_textbox_free(&box,NULL);
    }
    CHECK(garlic_apply_canvas_scaled(540,360,1));
    /* Reflowing never advances the script or appends the same page again. */
    cpymo_backlog backlog;CHECK(cpymo_backlog_init(&backlog)==0);
    const char *body="中文字体切换不会吞字。ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789中文正文续页验证。ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    cpymo_textbox tb;
    CHECK(cpymo_textbox_init(&tb,10,250,500,95,26.832f,cpymo_color_white,1,
        cpymo_str_pure(body),&backlog)==0);
    cpymo_textbox_finalize(&tb);CHECK(backlog.next_record_to_write==1);
    for(int i=0;i<100;i++){
        float fs=i%2?26.832f:31.2f;
        CHECK(cpymo_textbox_reflow(&tb,fs)==0);
        CHECK(tb.char_size==fs && backlog.next_record_to_write==1);
        CHECK(tb.page_text.begin==body);
        cpymo_textbox reference;CHECK(cpymo_textbox_init(&reference,10,250,500,95,
            fs,cpymo_color_white,1,cpymo_str_pure(body),NULL)==0);
        cpymo_textbox_finalize(&reference);
        CHECK(tb.remain_text.begin==reference.remain_text.begin);
        CHECK(tb.chars_pool_size==reference.chars_pool_size);
        cpymo_textbox_free(&reference,NULL);
    }
    while(tb.remain_text.len){
        CHECK(cpymo_textbox_clear_page(&tb,&backlog)==0);
        cpymo_textbox_finalize(&tb);
    }
    char joined[1024];size_t at=0;
    for(size_t i=0;i<backlog.next_record_to_write;i++)
        for(char *ch=backlog.records[i].text;*ch;ch++)if(*ch!='\n')joined[at++]=*ch;
    joined[at]=0;CHECK(strcmp(body,joined)==0);
    cpymo_textbox_free(&tb,NULL);cpymo_backlog_free(&backlog);
    /* Values share one axis in both skip states, with the same button slots. */
    cpymo_engine layout_engine={0};layout_engine.gameconfig.imagesize_w=540;
    layout_engine.gameconfig.imagesize_h=360;
    cpymo_config_ui layout={0};layout.font_size=31.2f;layout.label_column_w=180;
    layout.value_column_w=90;layout.dec_btn_w=20;layout.inc_btn_w=20;
    float previous_dec=-1,previous_inc=-1;
    for(int row=0;row<CONFIG_ITEMS;row++)for(int mode=0;mode<2;mode++){
        layout.items[row].show_inc_dec_button=true;
        layout.items[row].show_name_width=100;layout.items[row].show_value_width=mode?90:60;
        const cpymo_config_ui_item *item;float y,lx,vx,rect[4],dx,sy;
        cpymo_config_ui_calc_layout(&layout_engine,&layout,row,row*60,&item,&y,&lx,&vx,rect,&dx,&sy);
        CHECK(previous_dec<0 || (dx==previous_dec && rect[0]==previous_inc));
        CHECK(fabsf(vx+item->show_value_width/2-(dx+rect[2]+rect[0])/2)<0.01f);
        previous_dec=dx;previous_inc=rect[0];
    }
    int16_t audio[]={-32768,-20001,-1,0,1,20001,32767};
    garlic_output_percent=50;garlic_output_gain(audio,7);
    CHECK(audio[0]==-16384 && audio[6]==16383 && audio[3]==0);
    frontend_log=quiet_log;
    char tmp[]="/tmp/cpymo-v0242-XXXXXX";CHECK(mkdtemp(tmp));
    setenv("CPYMO_DEBUG","1",1);garlic_diagnostics_begin(tmp);
    char message[1025];memset(message,'x',1024);message[1024]=0;
    for(unsigned i=0;i<400;++i)garlic_log(RETRO_LOG_INFO,"%s",message);
    CHECK(garlic_log_bytes==262144);fclose(garlic_log_file);garlic_log_file=NULL;
    struct stat sb;CHECK(stat(garlic_log_path,&sb)==0 && sb.st_size==262144);
    unsetenv("CPYMO_DEBUG");garlic_diagnostics_begin(tmp);
    size_t logged=garlic_log_bytes;garlic_log(RETRO_LOG_INFO,"normal play");
    CHECK(!garlic_log_file && garlic_log_bytes==logged);
    cpymo_engine prefs={0};prefs.assetloader.gamedir=tmp;prefs.gameconfig.fontsize=26;
    cpymo_audio_init(&prefs.audio);prefs.gameconfig.garlic_font_large=2;
    prefs.gameconfig.garlic_read_gray=true;
    CHECK(cpymo_save_config_save(&prefs)==CPYMO_ERR_SUCC);
    prefs.gameconfig.garlic_font_large=false;CHECK(cpymo_save_config_load(&prefs)==CPYMO_ERR_SUCC);
    CHECK(prefs.gameconfig.garlic_font_large && prefs.gameconfig.fontsize==26);
    CHECK(prefs.gameconfig.garlic_read_gray);
    char cfg[4096];snprintf(cfg,sizeof(cfg),"%s/save/config.csav",tmp);
    CHECK(stat(cfg,&sb)==0 && sb.st_size==12);
    prefs.gameconfig.garlic_font_large=false;CHECK(cpymo_save_config_save(&prefs)==CPYMO_ERR_SUCC);
    prefs.gameconfig.garlic_font_large=2;CHECK(cpymo_save_config_load(&prefs)==CPYMO_ERR_SUCC);
    CHECK(!prefs.gameconfig.garlic_font_large && prefs.gameconfig.fontsize==26);
    cpymo_audio_free(&prefs.audio);
    graphics_checks();
    hw_extra(argv[1]);
    polish_library_checks(argv[1]);
    free(f);retro_unload_game();
    printf("PASS %u checks: exact alpha/colour, button ink centres, output gain\n",checks);
    return 0;
}
