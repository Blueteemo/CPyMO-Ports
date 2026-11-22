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
static unsigned hw_buttons;
static int16_t hw_input(unsigned p,unsigned d,unsigned i,unsigned id){(void)p;(void)d;(void)i;return (hw_buttons>>id)&1;}
static void hw_screenshot(const char *name){
    unsigned char *rgb=malloc(640*480*3);CHECK(rgb);
    for(unsigned i=0;i<640*480;i++){
        uint16_t p=garlic_rgb565_frame[i];unsigned r=p>>11,g=(p>>5)&63,b=p&31;
        rgb[i*3]=(r<<3)|(r>>2);rgb[i*3+1]=(g<<2)|(g>>4);rgb[i*3+2]=(b<<3)|(b>>2);
    }
    CHECK(stbi_write_png(name,640,480,3,rgb,640*3));free(rgb);
}
static void body_raise_checks(cpymo_engine *e){
    const char *words="一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十";
    for(unsigned dim=0;dim<3;dim++)for(unsigned preset=0;preset<2;preset++){
        unsigned w=dim==0?320:dim==1?540:800,h=dim==0?240:dim==1?360:600;
        CHECK(garlic_apply_canvas_scaled(w,h,1));e->gameconfig.imagesize_w=w;e->gameconfig.imagesize_h=h;
        e->gameconfig.garlic_font_large=preset==0?2:0;float fs=cpymo_gameconfig_font_size(&e->gameconfig);
        cpymo_textbox tb;CHECK(cpymo_textbox_init(&tb,16,h*.64f,fs*10.25f,h*.34f,fs,cpymo_color_white,1,cpymo_str_pure(words),NULL)==0);
        cpymo_textbox_finalize(&tb);tb.draw_cursor=false;CHECK(tb.active_line==2);
        size_t chars=tb.chars_pool_size,remaining=tb.remain_text.len;float y=tb.y,ys[3]={tb.lines[0].y,tb.lines[1].y,tb.lines[2].y};
        int y0[2]={480,480},y1[2]={0,0};uint16_t *frames=calloc(640*480*2,2);CHECK(frames);
        for(unsigned pass=0;pass<2;pass++){
            memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);memset(garlic_rgb565_frame,0,640*480*2);
            memset(garlic_text_layer,0,640*480*4);soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;
            cpymo_textbox_draw(e,&tb,pass?cpymo_backend_image_draw_type_text_say:cpymo_backend_image_draw_type_ui_element);
            CHECK(garlic_blit_rgb565_quality(garlic_rgb565_frame,1280,640,480,soft_image.pixels,soft_image.line_stride,soft_image.w,soft_image.h));
            garlic_text_layer_composite(garlic_rgb565_frame);memcpy(frames+pass*640*480,garlic_rgb565_frame,640*480*2);
            for(int yy=0;yy<480;yy++)for(int xx=0;xx<640;xx++)if(garlic_rgb565_frame[yy*640+xx]){if(yy<y0[pass])y0[pass]=yy;if(yy>y1[pass])y1[pass]=yy;}
        }
        CHECK(y0[0]-y0[1]>=3 && y0[0]-y0[1]<=4);CHECK(y1[0]-y1[1]>=3 && y1[0]-y1[1]<=4);
        if(dim!=1)CHECK(memcmp(frames+4*640,frames+640*480,(480-4)*640*2)==0);
        CHECK(tb.y==y && tb.chars_pool_size==chars && tb.remain_text.len==remaining);
        for(unsigned i=0;i<3;i++)CHECK(tb.lines[i].y==ys[i]);
        printf("BODY_RAISE canvas=%ux%u preset=%u output_top_shift=%d output_bottom_shift=%d pages_unchanged=1\n",w,h,preset,y0[0]-y0[1],y1[0]-y1[1]);
        free(frames);cpymo_textbox_free(&tb,NULL);
    }
}
static void toast_checks(cpymo_engine *e){
    const char *messages[]={"已快速保存到存档 1","快速保存失败","已快速读取存档 1"};
    for(unsigned dim=0;dim<3;dim++){
        unsigned w=dim==0?320:dim==1?540:800,h=dim==0?240:dim==1?360:600;
        CHECK(garlic_apply_canvas_scaled(w,h,1));e->gameconfig.imagesize_w=w;e->gameconfig.imagesize_h=h;
        for(unsigned i=0;i<3;i++){
            garlic_toast(messages[i]);CHECK(garlic_toast_text);float top,bottom;garlic_text_ink_bounds(garlic_toast_text,&top,&bottom);
            float x=w*.04f,y=h*.035f,pad=8/soft_context.text_scale;
            float box[]={x-pad,y-pad,garlic_toast_w+2*pad,garlic_toast_h+2*pad};
            float baseline=garlic_text_centered_baseline(garlic_toast_text,y+garlic_toast_h/2);
            CHECK(box[0]>=0 && box[1]>=0 && box[0]+box[2]<=w && box[1]+box[3]<=h);
            CHECK(baseline+top>=box[1] && baseline+bottom+1/soft_context.text_scale<=box[1]+box[3]);
            memset(garlic_text_layer,0,640*480*4);soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;
            garlic_toast_draw();
            for(int yy=0;yy<480;yy++)for(int xx=0;xx<640;xx++)if(garlic_text_layer[yy*640+xx]>>24){
                float lx=(xx-soft_context.text_off_x)/soft_context.text_scale,ly=(yy-soft_context.text_off_y)/soft_context.text_scale;
                CHECK(lx>=box[0] && lx<=box[0]+box[2] && ly>=box[1] && ly<=box[1]+box[3]);
            }
            memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);memset(garlic_rgb565_frame,0,640*480*2);
            memset(garlic_text_layer,0,640*480*4);soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;garlic_toast_draw();
            CHECK(garlic_blit_rgb565_quality(garlic_rgb565_frame,1280,640,480,soft_image.pixels,soft_image.line_stride,soft_image.w,soft_image.h));garlic_text_layer_composite(garlic_rgb565_frame);
            if(dim==1 && i==0)hw_screenshot("boot4_save_toast.png");
            garlic_toast_clear();
        }
    }
    puts("TOAST measured ink and shadow inside box, 3 canvases PASS");
}
static void boot_checks(const char *game,const char *font_path){
    for(unsigned phase=0;phase<2;phase++){
        hw_buttons=0;retro_unload_game();setenv("CPYMO_MENU_FONT",font_path,1);
        struct retro_game_info info={game,NULL,0,NULL};CHECK(retro_load_game(&info));
        CHECK(!strstr(engine.interpreter->script->script_content,"__garlic_boot_end"));
        CHECK(strstr(engine.interpreter->script->script_content,"#wait 50\n#bg logo1\n#wait 300\n#bg logo2\n#wait 300\n"));
        bool reached=false;const char *logo=phase?"logo2":"logo1";
        for(unsigned f=0;f<180;f++){
            retro_run();if(engine.interpreter->script->script_name[0])break;
            if(engine.bg.current_bg_name && !strcmp(engine.bg.current_bg_name,logo) && engine.bg.transform_next_bg){reached=true;break;}
        }
        CHECK(reached);hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run();
        CHECK(engine.interpreter->script->script_name[0]==0); /* A finishes only this transition */
        hw_buttons=0;
        for(unsigned f=0;f<180;f++)retro_run();
        CHECK(!strcmp(engine.interpreter->script->script_name,"start") && engine.say.active);
        printf("BOOT upstream timing retained, A during %s does not jump the entire sequence PASS\n",logo);
    }
}
#include "inset_checks.h"
static void hw_extra(const char *font_path){
    const char *game=getenv("CPYMO_QA_GAME_PATH");CHECK(game);
    char tmp[]="/tmp/cpymo-hw-ui-XXXXXX";CHECK(mkdtemp(tmp));
    setenv("CPYMO_MENU","0",1);setenv("CPYMO_MENU_FONT",font_path,1);setenv("CPYMO_SAVE_ROOT",tmp,1);garlic_save_root_known=false;
    retro_set_environment(hw_environment);retro_set_video_refresh(hw_video);retro_set_audio_sample_batch(hw_audio);
    retro_set_input_poll(hw_poll);retro_set_input_state(hw_input);retro_init();
    struct retro_game_info game_info={game,NULL,0,NULL};CHECK(retro_load_game(&game_info));
    for(unsigned i=0;i<180;i++)retro_run();
    while(cpymo_ui_enabled(&engine))cpymo_ui_exit(&engine);
    CHECK(cpymo_save_config_save(&engine)==0);
    const char *configs[]={"CPYMO_UI_1\nfont=large\n","CPYMO_UI_2\nfont=small\nread_gray=0\n","CPYMO_UI_2\nfont=large\nread_gray=1\n","CPYMO_UI_3\nfont=small\nread_gray=0\n","CPYMO_UI_3\nfont=default\nread_gray=1\n","CPYMO_UI_3\nfont=large\nread_gray=0\n","CPYMO_UI_4\nfont=small\nread_gray=0\n","CPYMO_UI_4\nfont=default\nread_gray=1\n"};
    const unsigned choices[]={0,0,0,0,0,0,2,0},gray[]={1,0,1,0,1,0,0,1};
    for(unsigned i=0;i<8;i++){
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
        CHECK(menu->panel[0]>=0 && menu->panel[1]>=0);
        CHECK(menu->panel[0]+menu->panel[2]<=w && menu->panel[1]+menu->panel[3]<=h);
        CHECK(menu->clock_font_size*soft_context.text_scale>=19.0f);
        CHECK(menu->header_height>=menu->panel[3]*.16f);
        CHECK(menu->clock_text && strlen(menu->clock_label)==5 && menu->clock_label[2]==':');
        setenv("TZ","UTC",1);tzset();garlic_rmenu_clock(&engine,menu,0);
        CHECK(!strcmp(menu->clock_label,"00:00"));cpymo_backend_text old_clock=menu->clock_text;
        engine.redraw=false;garlic_rmenu_clock(&engine,menu,0);CHECK(old_clock==menu->clock_text && !engine.redraw);
        garlic_rmenu_clock(&engine,menu,60);CHECK(!strcmp(menu->clock_label,"00:01") && engine.redraw);
        garlic_rmenu_clock(&engine,menu,23*3600+59*60);CHECK(!strcmp(menu->clock_label,"23:59"));
        garlic_rmenu_clock(&engine,menu,24*3600);CHECK(!strcmp(menu->clock_label,"00:00"));
        garlic_rmenu_clock(&engine,menu,-1);CHECK(!strcmp(menu->clock_label,"--:--"));
        menu->clock_poll=1;CHECK(cpymo_rmenu_update(&engine,menu,0)==0);CHECK(strcmp(menu->clock_label,"--:--"));
        for(unsigned i=0;i<menu->menu.all_selections;i++){
            cpymo_select_img_selection *s=&menu->menu.selections[i];if(!s->enabled)continue;
            garlic_text_ink_bounds(s->or_text,&top,&bottom);
            if(!(s->y+top>=0 && s->y+bottom<h))printf("BAD_ROW canvas=%ux%u preset=%s row=%u top=%.2f bottom=%.2f h=%d\n",w,h,names[k],i,s->y+top,s->y+bottom,s->h);
            printf("MENU_BOUNDS canvas=%ux%u preset=%s row=%u ink=%.5f..%.5f content=%.5f..%.5f bottom_padding=%.5f\n",w,h,names[k],i,s->y+top,s->y+bottom,menu->panel[1]+menu->header_height+menu->clock_gap,menu->panel[1]+menu->panel[3]-menu->bottom_padding,menu->bottom_padding);
            CHECK(s->y+top+.001f>=menu->panel[1]+menu->header_height+menu->clock_gap);
            CHECK(s->y+bottom<=menu->panel[1]+menu->panel[3]-menu->bottom_padding+.001f);
            CHECK((bottom-top)<=s->h+0.01f);
        }
        CHECK(menu->bottom_padding*soft_context.text_scale>=16.0f);
        const time_t clock_samples[]={0,12*3600+34*60,23*3600+59*60};
        for(unsigned sample=0;sample<3;sample++){
            garlic_rmenu_clock(&engine,menu,clock_samples[sample]);
            memset(garlic_text_layer,0,640*480*4);soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;
            cpymo_rmenu_draw(&engine,menu);
            int clock_left=640,clock_right=-1,clock_top=480;
            int header_end=(int)floorf((menu->panel[1]+menu->header_height)*soft_context.text_scale+soft_context.text_off_y);
            for(int yy=0;yy<header_end;yy++)for(int xx=0;xx<640;xx++)if(garlic_text_layer[yy*640+xx]>>24){
                if(xx<clock_left)clock_left=xx;if(xx>clock_right)clock_right=xx;
                if(yy<clock_top)clock_top=yy;
            }
            float left_inset=clock_left-(menu->panel[0]*soft_context.text_scale+soft_context.text_off_x);
            float top_inset=clock_top-(menu->panel[1]*soft_context.text_scale+soft_context.text_off_y);
            CHECK(clock_right>clock_left && fabsf(left_inset-top_inset)<=1.1f);
            CHECK(top_inset>=12 && left_inset>=12);
            printf("CLOCK_INSETS canvas=%ux%u preset=%s label=%s left=%.3f top=%.3f delta=%.3fpx\n",w,h,names[k],menu->clock_label,left_inset,top_inset,fabsf(left_inset-top_inset));
        }
        menu->menu.current_selection=9;retro_run();
        if(dim==1){char name[100];snprintf(name,sizeof(name),"boot4_start_%s.png",names[k]);hw_screenshot(name);}
        cpymo_ui_exit(&engine);
    }
    CHECK(heights[0]<=heights[1]);
    for(unsigned k=0;k<2;k++){
        CHECK(garlic_apply_canvas_scaled(540,360,1));
        engine.gameconfig.imagesize_w=540;engine.gameconfig.imagesize_h=360;
        engine.gameconfig.garlic_font_large=presets[k];
        CHECK(cpymo_config_ui_enter(&engine)==0);
        cpymo_config_ui *settings=cpymo_list_ui_data(&engine);
        CHECK(settings->items[ITEM_FONT_SIZE].min_value==0 && settings->items[ITEM_FONT_SIZE].max_value==1);
        CHECK(settings->items[ITEM_FONT_SIZE].value==(int)k);
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
        retro_run();char name[80];snprintf(name,sizeof(name),"boot4_settings_%s.png",names[k]);hw_screenshot(name);
        cpymo_ui_exit(&engine);
    }
    float black_rect[]={0,0,540,360};
    soft_context.frame_painted=false;
    cpymo_backend_image_fill_rects(black_rect,1,cpymo_color_black,0,cpymo_backend_image_draw_type_bg);
    CHECK(!soft_context.frame_painted);
    cpymo_backend_image_fill_rects(black_rect,1,cpymo_color_black,1,cpymo_backend_image_draw_type_bg);
    CHECK(soft_context.frame_painted); /* intentional black is a real first frame */
    body_raise_checks(&engine);
    inset_checks(&engine);
    puts("HW UI_1/UI_2/UI_3 migration, two font sizes, 3x2 START-menu bounds PASS");
    toast_checks(&engine);
    boot_checks(game,font_path);
    retro_unload_game();unsetenv("CPYMO_SAVE_ROOT");garlic_save_root_known=false;
}

static void polish_library_checks(const char *font_path){
    const char *game=getenv("CPYMO_QA_GAME_PATH");CHECK(game);
    char root[4096];snprintf(root,sizeof(root),"%s",game);char *slash=strrchr(root,'/');CHECK(slash);*slash=0;
    char tmp[]="/tmp/cpymo-footer-XXXXXX";CHECK(mkdtemp(tmp));
    setenv("CPYMO_SAVE_ROOT",tmp,1);garlic_save_root_known=false;setenv("CPYMO_MENU_FONT",font_path,1);setenv("CPYMO_MENU","1",1);
    struct retro_game_info info={root,NULL,0,NULL};CHECK(retro_load_game(&info));retro_run();
    CHECK(garlic_screen==GARLIC_SCREEN_MENU && garlic_library_footer);
    cpymo_backend_text reference;float width,top,bottom,old_top,old_bottom;
    CHECK(cpymo_backend_text_create(&reference,&width,cpymo_str_pure("CPyMO 0.24.5   A：启动   ↑↓：选择   B：退出"),18)==0);
    garlic_text_ink_bounds(reference,&old_top,&old_bottom);garlic_text_ink_bounds(garlic_library_footer,&top,&bottom);
    CHECK(bottom-top>old_bottom-old_top);CHECK(top+444>=416 && bottom+444<464);
    CHECK(((cpymo_backend_text_impl*)garlic_library_footer)->w<608);
    cpymo_backend_text_free(reference);hw_screenshot("polish6_library.png");
    printf("FOOTER old_ink_height=%.3f new_ink_height=%.3f bottom=%.3f PASS\n",old_bottom-old_top,bottom-top,bottom+444);
    retro_unload_game();unsetenv("CPYMO_SAVE_ROOT");garlic_save_root_known=false;
}
