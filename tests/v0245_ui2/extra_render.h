#include "../../source/CPyMO-main/cpymo/cpymo_select_img.c"
#include "../../source/CPyMO-main/cpymo/cpymo_rmenu.c"
static bool hw_environment(unsigned command,void *data){
    if(command==RETRO_ENVIRONMENT_GET_LOG_INTERFACE){((struct retro_log_callback*)data)->log=quiet_log;return true;}
    if(command==RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY){*(const char**)data="/nonexistent";return true;}
    if(command==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)return *(enum retro_pixel_format*)data==RETRO_PIXEL_FORMAT_RGB565;
    return false;
}
static void hw_video(const void *p,unsigned w,unsigned h,size_t pitch){(void)p;(void)w;(void)h;(void)pitch;}
static size_t hw_audio(const int16_t *p,size_t n){(void)p;return n;}
static void hw_poll(void){}
static int16_t hw_input(unsigned p,unsigned d,unsigned i,unsigned id){(void)p;(void)d;(void)i;(void)id;return 0;}
static void hw_screenshot(const char *name){
    unsigned char *rgb=malloc(640*480*3);CHECK(rgb);
    for(unsigned i=0;i<640*480;i++){
        uint16_t p=garlic_rgb565_frame[i];unsigned r=p>>11,g=(p>>5)&63,b=p&31;
        rgb[i*3]=(r<<3)|(r>>2);rgb[i*3+1]=(g<<2)|(g>>4);rgb[i*3+2]=(b<<3)|(b>>2);
    }
    CHECK(stbi_write_png(name,640,480,3,rgb,640*3));free(rgb);
}
static void hw_extra(const char *font_path){
    const char *game=getenv("CPYMO_QA_GAME_PATH");CHECK(game);
    char tmp[]="/tmp/cpymo-hw-ui-XXXXXX";CHECK(mkdtemp(tmp));
    setenv("CPYMO_MENU","0",1);setenv("CPYMO_MENU_FONT",font_path,1);setenv("CPYMO_SAVE_ROOT",tmp,1);
    retro_set_environment(hw_environment);retro_set_video_refresh(hw_video);retro_set_audio_sample_batch(hw_audio);
    retro_set_input_poll(hw_poll);retro_set_input_state(hw_input);retro_init();
    struct retro_game_info game_info={game,NULL,0,NULL};CHECK(retro_load_game(&game_info));
    for(unsigned i=0;i<180;i++)retro_run();
    while(cpymo_ui_enabled(&engine))cpymo_ui_exit(&engine);
    CHECK(cpymo_save_config_save(&engine)==0);
    const char *configs[]={"CPYMO_UI_1\nfont=large\n","CPYMO_UI_2\nfont=small\nread_gray=0\n","CPYMO_UI_2\nfont=large\nread_gray=1\n","CPYMO_UI_3\nfont=small\nread_gray=0\n","CPYMO_UI_3\nfont=default\nread_gray=1\n","CPYMO_UI_3\nfont=large\nread_gray=0\n"};
    const unsigned choices[]={0,0,0,2,0,0},gray[]={1,0,1,0,1,0};
    for(unsigned i=0;i<6;i++){
        FILE *f=cpymo_backend_write_save(engine.assetloader.gamedir,"rg35xx-ui.cfg");CHECK(f);
        CHECK(fwrite(configs[i],1,strlen(configs[i]),f)==strlen(configs[i]));CHECK(!fclose(f));
        CHECK(cpymo_save_config_load(&engine)==0);CHECK(engine.gameconfig.garlic_font_large==choices[i]);CHECK(engine.gameconfig.garlic_read_gray==gray[i]);
        CHECK(cpymo_save_config_save(&engine)==0);engine.gameconfig.garlic_font_large=99;
        CHECK(cpymo_save_config_load(&engine)==0);CHECK(engine.gameconfig.garlic_font_large==choices[i]);
    }
    const unsigned presets[]={2,0};const char *names[]={"small","default"};int heights[2]={0};
    for(unsigned dim=0;dim<3;dim++)for(unsigned k=0;k<2;k++){
        unsigned w=dim==0?320:dim==1?540:800,h=dim==0?240:dim==1?360:600;
        CHECK(garlic_apply_canvas_scaled(w,h,1));engine.gameconfig.imagesize_w=w;engine.gameconfig.imagesize_h=h;
        engine.gameconfig.garlic_font_large=presets[k];engine.gameconfig.fontsize=26;
        cpymo_backend_text t;float width,top,bottom;
        CHECK(cpymo_backend_text_create(&t,&width,cpymo_str_pure("国水仙文字 ABC"),cpymo_gameconfig_font_size(&engine.gameconfig))==0);
        garlic_text_ink_bounds(t,&top,&bottom);int physical=(int)lroundf((bottom-top)*soft_context.text_scale);
        if(dim==1)heights[k]=physical;
        printf("HW_FONT canvas=%ux%u preset=%s ink_height=%d\n",w,h,names[k],physical);cpymo_backend_text_free(t);
        CHECK(cpymo_rmenu_enter(&engine)==0);cpymo_rmenu *menu=cpymo_ui_data(&engine);
        for(unsigned i=0;i<menu->menu.all_selections;i++){
            cpymo_select_img_selection *s=&menu->menu.selections[i];if(!s->enabled)continue;
            garlic_text_ink_bounds(s->or_text,&top,&bottom);
            if(!(s->y+top>=0 && s->y+bottom<h))printf("BAD_ROW canvas=%ux%u preset=%s row=%u top=%.2f bottom=%.2f h=%d\n",w,h,names[k],i,s->y+top,s->y+bottom,s->h);
            CHECK(s->y+top>=0 && s->y+bottom<h);
            CHECK((bottom-top+4)<=s->h+0.01f);
        }
        menu->menu.current_selection=6;retro_run();
        if(dim==1){char name[100];snprintf(name,sizeof(name),"ui2_start_%s.png",names[k]);hw_screenshot(name);}
        cpymo_ui_exit(&engine);
    }
    CHECK(heights[0]<heights[1]);
    for(unsigned k=0;k<2;k++){
        CHECK(garlic_apply_canvas_scaled(540,360,1));
        engine.gameconfig.imagesize_w=540;engine.gameconfig.imagesize_h=360;
        engine.gameconfig.garlic_font_large=presets[k];
        CHECK(cpymo_config_ui_enter(&engine)==0);
        cpymo_config_ui *settings=cpymo_list_ui_data(&engine);
        CHECK(settings->items[ITEM_FONT_SIZE].min_value==0 && settings->items[ITEM_FONT_SIZE].max_value==1);
        CHECK(settings->items[ITEM_FONT_SIZE].value==(k==0?0:1));
        cpymo_backend_text reference;float rw;
        CHECK(cpymo_backend_text_create(&reference,&rw,cpymo_str_pure(k==0?"小":"默认"),settings->font_size)==0);
        cpymo_backend_text_impl *actual=settings->items[ITEM_FONT_SIZE].show_value,*expected=reference;
        CHECK(actual->w==expected->w && actual->h==expected->h);
        CHECK(actual->baseline==expected->baseline && fabsf(settings->items[ITEM_FONT_SIZE].show_value_width-rw)<0.001f);
        CHECK(memcmp(actual->px,expected->px,actual->w*actual->h)==0);
        cpymo_backend_text_free(reference);
        float dx0=-1,ix0=-1;
        for(unsigned row=0;row<CONFIG_ITEMS;row++){
            const cpymo_config_ui_item *item;float y,lx,vx,rect[4],dx,sy;
            cpymo_config_ui_calc_layout(&engine,settings,row,(float)row*360/CONFIG_ITEMS,&item,&y,&lx,&vx,rect,&dx,&sy);
            CHECK(dx0<0 || (fabsf(dx-dx0)<.001f && fabsf(rect[0]-ix0)<.001f));dx0=dx;ix0=rect[0];
            CHECK(fabsf(vx+item->show_value_width/2-(dx+rect[2]+rect[0])/2)<.01f);
        }
        retro_run();char name[80];snprintf(name,sizeof(name),"ui2_settings_%s.png",names[k]);hw_screenshot(name);
        cpymo_ui_exit(&engine);
    }
    float black_rect[]={0,0,540,360};
    soft_context.frame_painted=false;
    cpymo_backend_image_fill_rects(black_rect,1,cpymo_color_black,0,cpymo_backend_image_draw_type_bg);
    CHECK(!soft_context.frame_painted);
    cpymo_backend_image_fill_rects(black_rect,1,cpymo_color_black,1,cpymo_backend_image_draw_type_bg);
    CHECK(soft_context.frame_painted); /* intentional black is a real first frame */
    puts("HW UI_1/UI_2/UI_3 migration, two distinct font sizes, 3x2 START-menu bounds PASS");
    retro_unload_game();unsetenv("CPYMO_SAVE_ROOT");
}
