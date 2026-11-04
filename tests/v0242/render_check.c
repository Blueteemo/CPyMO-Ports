/* Includes the exact backend to test its private RGB565 compositor. Link the
 * other core objects, excluding libretro.c and cpymo_msgbox_ui.c. Host only. */
#include "../../source/CPyMO-main/cpymo-backends/libretro/libretro.c"
#include "../../source/CPyMO-main/cpymo/cpymo_msgbox_ui.c"
static unsigned checks;
static void quiet_log(enum retro_log_level l,const char *f,...) {(void)l;(void)f;}
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
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
        float fs=26/240.0f*h*0.8f*(large?1:0.92f);
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
    cpymo_audio_init(&prefs.audio);prefs.gameconfig.garlic_font_large=true;
    CHECK(cpymo_save_config_save(&prefs)==CPYMO_ERR_SUCC);
    prefs.gameconfig.garlic_font_large=false;CHECK(cpymo_save_config_load(&prefs)==CPYMO_ERR_SUCC);
    CHECK(prefs.gameconfig.garlic_font_large && prefs.gameconfig.fontsize==26);
    char cfg[4096];snprintf(cfg,sizeof(cfg),"%s/save/config.csav",tmp);
    CHECK(stat(cfg,&sb)==0 && sb.st_size==12);
    prefs.gameconfig.garlic_font_large=false;CHECK(cpymo_save_config_save(&prefs)==CPYMO_ERR_SUCC);
    prefs.gameconfig.garlic_font_large=true;CHECK(cpymo_save_config_load(&prefs)==CPYMO_ERR_SUCC);
    CHECK(!prefs.gameconfig.garlic_font_large && prefs.gameconfig.fontsize==26);
    cpymo_audio_free(&prefs.audio);
    free(f);retro_unload_game();
    printf("PASS %u checks: exact alpha/colour, button ink centres, output gain\n",checks);
    return 0;
}
