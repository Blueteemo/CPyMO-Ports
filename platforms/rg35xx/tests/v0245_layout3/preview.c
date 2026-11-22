/* Produce exact 640x480 story/menu previews with the production renderer. */
#define main legacy_render_main
#include "render_check.c"
#undef main
int main(int argc,char **argv){
    CHECK(argc==4);const char *font=argv[1],*game=argv[2],*prefix=argv[3];
    char tmp[]="/tmp/cpymo-preview-XXXXXX";CHECK(mkdtemp(tmp));
    setenv("CPYMO_MENU","0",1);setenv("CPYMO_MENU_FONT",font,1);setenv("CPYMO_SAVE_ROOT",tmp,1);setenv("TZ","UTC",1);tzset();
    retro_set_environment(hw_environment);retro_set_video_refresh(hw_video);retro_set_audio_sample_batch(hw_audio);
    retro_set_input_poll(hw_poll);retro_set_input_state(hw_input);retro_init();
    struct retro_game_info info={game,NULL,0,NULL};CHECK(retro_load_game(&info));
    for(unsigned i=0;i<180;i++)retro_run();
    for(unsigned k=0;k<2;k++){
        CHECK(cpymo_config_ui_enter(&engine)==0);cpymo_config_ui *settings=cpymo_list_ui_data(&engine);
        CHECK(cpymo_config_ui_set_value(&engine,settings,ITEM_FONT_SIZE,k?1:0,false)==0);cpymo_ui_exit(&engine);
        retro_run();char path[4096];snprintf(path,sizeof(path),"%s_%s_story.png",prefix,k?"default":"small");hw_screenshot(path);
        CHECK(cpymo_rmenu_enter(&engine)==0);cpymo_rmenu *menu=cpymo_ui_data(&engine);
        garlic_rmenu_clock(&engine,menu,12*3600+34*60);menu->menu.current_selection=6;retro_run();
        snprintf(path,sizeof(path),"%s_%s_menu.png",prefix,k?"default":"small");hw_screenshot(path);
        for(unsigned i=0;i<menu->menu.all_selections;i++){
            cpymo_select_img_selection *s=&menu->menu.selections[i];if(!s->enabled)continue;
            float top,bottom;garlic_text_ink_bounds(s->or_text,&top,&bottom);
            CHECK(s->y+top>=menu->panel[1]+menu->padding+menu->clock_height+menu->clock_gap);
            CHECK(s->y+bottom<=menu->panel[1]+menu->panel[3]-menu->padding);
        }
        cpymo_ui_exit(&engine);
    }
    retro_unload_game();retro_deinit();puts("PREVIEW PASS");return 0;
}
