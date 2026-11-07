static float name_plate[4],cursor_rect[4];
static unsigned name_calls,cursor_calls;
void __real_cpymo_backend_image_draw(float,float,float,float,cpymo_backend_image,int,int,int,int,float,enum cpymo_backend_image_draw_type);
void __wrap_cpymo_backend_image_draw(float x,float y,float w,float h,cpymo_backend_image im,int sx,int sy,int sw,int sh,float a,enum cpymo_backend_image_draw_type type){
 if(im==engine.say.namebox){
  if(!name_calls){name_plate[0]=x;name_plate[1]=y;name_plate[2]=w;name_plate[3]=h;}
  else{float right=fmaxf(name_plate[0]+name_plate[2],x+w);name_plate[0]=fminf(name_plate[0],x);name_plate[2]=right-name_plate[0];}
  name_calls++;
 }
 if(im==engine.say.msg_cursor){cursor_rect[0]=x;cursor_rect[1]=y;cursor_rect[2]=w;cursor_rect[3]=h;cursor_calls++;}
 __real_cpymo_backend_image_draw(x,y,w,h,im,sx,sy,sw,sh,a,type);
}
static uint64_t slot_hash(unsigned slot){
 FILE *f=cpymo_save_open_read(&engine,slot);if(!f)return 0;
 uint64_t h=1469598103934665603ull;int c;while((c=fgetc(f))!=EOF){h^=(unsigned)c;h*=1099511628211ull;}fclose(f);return h;
}
static void save11_checks(const char *font){
 char tmp[]="/tmp/cpymo-save11-XXXXXX";CHECK(mkdtemp(tmp));
 setenv("CPYMO_SAVE_ROOT",tmp,1);garlic_save_root_known=false;setenv("CPYMO_MENU_FONT",font,1);setenv("CPYMO_MENU","0",1);
 const char *game=getenv("CPYMO_QA_GAME_PATH");struct retro_game_info info={game,NULL,0,NULL};hw_buttons=0;CHECK(retro_load_game(&info));for(unsigned i=0;i<180;i++)retro_run();
 float common_width[3][2]={{0}};
 const char *names[]={"三个字","四字人名","五个字人名","六个字的人名"};
 for(unsigned dim=0;dim<3;dim++)for(unsigned preset=0;preset<2;preset++)for(unsigned n=0;n<4;n++){
  unsigned w=dim==0?320:dim==1?540:800,h=dim==0?240:dim==1?360:600;
  CHECK(garlic_apply_canvas_scaled(w,h,1));engine.gameconfig.imagesize_w=w;engine.gameconfig.imagesize_h=h;engine.gameconfig.garlic_font_large=preset?0:2;
  CHECK(cpymo_say_start(&engine,cpymo_str_pure(names[n]),cpymo_str_pure("正文的左右边界一起微移，人名框应容纳更长姓名。"))==0);cpymo_textbox_finalize(&engine.say.textbox);engine.say.textbox.draw_cursor=true;engine.say.auto_mode=false;name_calls=cursor_calls=0;
  memset(soft_image.pixels,0,soft_image.line_stride*soft_image.h);
  memset(garlic_text_layer,0,640*480*4);soft_context.text_dirty_y0=480;soft_context.text_dirty_y1=0;cpymo_say_draw(&engine);
  CHECK(name_calls==3 && cursor_calls);
  printf("NAME_GEOMETRY canvas=%ux%u preset=%u chars=%u plate=%.6f name=%.6f font=%.6f\n",w,h,preset,n+3,name_plate[2],engine.say.name_width,cpymo_gameconfig_font_size(&engine.gameconfig));fflush(stdout);
  if(!n)common_width[dim][preset]=name_plate[2];
  if(n==1)CHECK(fabsf(name_plate[2]-common_width[dim][preset])<.001f);float fs=cpymo_gameconfig_font_size(&engine.gameconfig);
  CHECK(name_plate[2]>=engine.say.name_width+fs-.001f);CHECK(name_plate[0]>=0 && name_plate[0]+name_plate[2]<=w);
  float right=cursor_rect[0]+engine.say.msg_cursor_ink[2]*cursor_rect[2]/engine.say.msg_cursor_w;
  float bottom=cursor_rect[1]+engine.say.msg_cursor_ink[3]*cursor_rect[3]/engine.say.msg_cursor_h;
  CHECK(fabsf((w-right)-(h-bottom))<.001f);CHECK(fabsf(w-right-(h*.02f+3.6f/soft_context.text_scale))<.001f);
  CHECK(fabsf(engine.say.textbox.x-(w-engine.say.textbox.w)*.5f)<.001f);
  if(dim==2 && preset==1 && n==1){
   memset(garlic_rgb565_frame,0,640*480*2);CHECK(garlic_blit_rgb565_quality(garlic_rgb565_frame,1280,640,480,soft_image.pixels,soft_image.line_stride,soft_image.w,soft_image.h));garlic_text_layer_composite(garlic_rgb565_frame);hw_screenshot("save11_four_character_name.png");
  }
 }
 hw_buttons=0;retro_unload_game();CHECK(retro_load_game(&info));for(unsigned i=0;i<180;i++)retro_run();
 uint64_t auto1=slot_hash(0);CHECK(auto1 && !slot_hash(1));
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_R2;retro_run();CHECK(!slot_hash(1));hw_buttons=0;retro_run();
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_R2;retro_run();CHECK(slot_hash(1));hw_buttons=0;retro_run();uint64_t quick=slot_hash(1);
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run();hw_buttons=0;for(unsigned i=0;i<120;i++)retro_run();
 CHECK(slot_hash(0)!=auto1 && slot_hash(1)==quick); /* autosave advances without touching quicksave */
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_R2;retro_run();usleep(670000);retro_run();CHECK(slot_hash(1)!=quick);hw_buttons=0;retro_run();
 char *saved_text=strdup(engine.say.current_text);CHECK(saved_text);
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run();hw_buttons=0;for(unsigned i=0;i<120;i++)retro_run();CHECK(strcmp(engine.say.current_text,saved_text));
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_L2;retro_run();hw_buttons=0;retro_run();CHECK(strcmp(engine.say.current_text,saved_text));usleep(360000);
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_L2;retro_run();CHECK(strcmp(engine.say.current_text,saved_text));usleep(670000);retro_run();for(unsigned i=0;i<180;i++)retro_run();CHECK(!strcmp(engine.say.current_text,saved_text));CHECK(garlic_toast_left<=0);
 usleep(700000);retro_run();CHECK(garlic_toast_left<=0); /* holding through a load never retriggers */
 hw_buttons=0;retro_run();hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run();hw_buttons=0;for(unsigned i=0;i<120;i++)retro_run();CHECK(strcmp(engine.say.current_text,saved_text));
 hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_L2;retro_run();hw_buttons=0;retro_run();hw_buttons=1u<<RETRO_DEVICE_ID_JOYPAD_L2;retro_run();hw_buttons=0;for(unsigned i=0;i<120;i++)retro_run();CHECK(!strcmp(engine.say.current_text,saved_text));free(saved_text);
 puts("SAVE11 nameplates 3..6 chars, two sizes, 3 canvases; cursor equal inset; translated text; single press rejected, double/hold save and load, no held-load repeats, independent autosave PASS");
 retro_unload_game();unsetenv("CPYMO_SAVE_ROOT");garlic_save_root_known=false;
}
