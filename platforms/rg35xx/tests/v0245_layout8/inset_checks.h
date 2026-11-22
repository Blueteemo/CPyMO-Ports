/* Run with the real engine, renderer, UTF-8 parser and ASAN/UBSAN. */
static void inset_frame(cpymo_engine *e,const char *path){
    memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);
    memset(garlic_rgb565_frame,0,640*480*2);memset(garlic_text_layer,0,640*480*4);
    soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;
    cpymo_say_draw(e);
    CHECK(garlic_blit_rgb565_quality(garlic_rgb565_frame,1280,640,480,soft_image.pixels,soft_image.line_stride,soft_image.w,soft_image.h));
    garlic_text_layer_composite(garlic_rgb565_frame);hw_screenshot(path);
}
static void inset_checks(cpymo_engine *e){
    const unsigned presets[]={2,0,1};
    char text[10000]={0};const char *phrase="雨后的街道安静了下来，她说今天也一起走吧。ABC123，明天还会再见。";
    for(unsigned i=0;i<40;i++)strcat(text,phrase);
    for(unsigned dim=0;dim<3;dim++)for(unsigned k=0;k<3;k++){
        unsigned w=dim==0?320:dim==1?540:800,h=dim==0?240:dim==1?360:600;
        CHECK(garlic_apply_canvas_scaled(w,h,1));e->gameconfig.imagesize_w=w;e->gameconfig.imagesize_h=h;
        e->gameconfig.garlic_font_large=presets[k];
        float fs=cpymo_gameconfig_font_size(&e->gameconfig);
        float left=(float)e->gameconfig.msglr_l*w/540, right=w-(float)e->gameconfig.msglr_r*w/540;
        CHECK(cpymo_say_start(e,cpymo_str_pure("测试人名"),cpymo_str_pure(text))==0);
        cpymo_textbox *tb=&e->say.textbox;cpymo_textbox_finalize(tb);
        CHECK(fabsf(tb->x-left-fs*.375f)<.001f);CHECK(fabsf(tb->x+tb->w-right)<.001f);
        cpymo_backend_text name=e->say.name;float namew=e->say.name_width,y=tb->y,height=tb->h;
        /* Cycling presets rebuilds the same page boundary without cumulative drift. */
        cpymo_str page=tb->page_text;
        for(unsigned cycle=0;cycle<12;cycle++){
            unsigned preset=presets[cycle%3];e->gameconfig.garlic_font_large=preset;
            float next=cpymo_gameconfig_font_size(&e->gameconfig);
            CHECK(cpymo_say_reflow_textbox(e,next)==0);
            CHECK(tb->page_text.begin==page.begin && tb->page_text.len==page.len);
            CHECK(fabsf(tb->x-left-next*.375f)<.001f && fabsf(tb->x+tb->w-right)<.001f);
            CHECK(tb->y==y && tb->h==height && e->say.name==name && e->say.name_width==namew);
        }
        e->gameconfig.garlic_font_large=presets[k];CHECK(cpymo_say_reflow_textbox(e,fs)==0);
        if(dim==1){
            char path[100];snprintf(path,sizeof(path),"layout8_after_%u.png",k);inset_frame(e,path);
            uint16_t *after=malloc(640*480*2);CHECK(after);memcpy(after,garlic_rgb565_frame,640*480*2);
            /* Reference authored geometry, using the same renderer and speaker. */
            tb->x=left;tb->w=right-left;CHECK(cpymo_textbox_reflow(tb,fs)==0);
            snprintf(path,sizeof(path),"layout8_before_%u.png",k);inset_frame(e,path);
            unsigned name_rows=(unsigned)floorf((tb->y-(float)h/120)*soft_context.text_scale+soft_context.text_off_y);
            CHECK(name_rows<=480);CHECK(memcmp(after,garlic_rgb565_frame,name_rows*640*2)==0);free(after);
            tb->x=left+fs*.375f;tb->w=right-tb->x;CHECK(cpymo_textbox_reflow(tb,fs)==0);
        }
        size_t consumed=0;unsigned pages=0;
        do{
            size_t before=tb->remain_text.len;cpymo_textbox_finalize(tb);size_t taken=before-tb->remain_text.len;
            /* First page has already been finalized by reflow. */
            if(!pages)taken=tb->page_text.len-tb->remain_text.len;
            CHECK(taken>0);consumed+=taken;pages++;
            for(size_t i=0;i<tb->chars_pool_size;i++)CHECK(tb->chars_x_pool[i]>=tb->x && tb->chars_x_pool[i]<right);
            CHECK(fabsf(tb->x+tb->w-right)<.001f);
            if(!tb->remain_text.len)break;
            CHECK(cpymo_textbox_clear_page(tb,&e->backlog)==0);
        }while(pages<200);
        CHECK(consumed==strlen(text) && pages>1 && pages<200);
        printf("INSET canvas=%ux%u preset=%u inset_em=%.4f pages=%u UTF8_bytes=%zu name_pixels_unchanged=1 switch_drift=0\n",w,h,presets[k],fs*.375f,pages,consumed);
    }
    puts("INSET 3/8-em, fixed right edge, live reflow, pagination and name pixels PASS");
}
