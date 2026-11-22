#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpymo_gameconfig.h"
#include "cpymo_assetloader.h"
#include "cpymo_script.h"
#include "cpymo_str.h"

static void file_write(const char *p,const char *str){FILE*f=fopen(p,"wb");assert(f);assert(fwrite(str,1,strlen(str),f)==strlen(str));assert(fclose(f)==0);}
int main(int argc,char**argv){
  assert(argc==2);
  char cfgfile[2048],scriptfile[2048];
  assert(snprintf(cfgfile,sizeof cfgfile,"%s/gameconfig.txt",argv[1])<sizeof cfgfile);
  assert(snprintf(scriptfile,sizeof scriptfile,"%s/script/start.txt",argv[1])<sizeof scriptfile);
  file_write(cfgfile,"gametitle,RG35XX test\nengineversion,1.2\nplatform,pygame\nscripttype,pymo\nimagesize,320,240\nstartscript,start\nbgmformat,.mp3\nfontsize,20\n");
  file_write(scriptfile,"#textbox message,name\n#wait 10\n");
  cpymo_gameconfig cfg;
  assert(cpymo_gameconfig_parse_from_file(&cfg,cfgfile)==CPYMO_ERR_SUCC);
  assert(cfg.imagesize_w==320 && cfg.imagesize_h==240);
  assert(!strcmp(cfg.startscript,"start") && !strcmp(cfg.gametitle,"RG35XX test"));
  assert(!strcmp(cfg.bgmformat,"mp3"));
  cpymo_assetloader al={0};
  assert(cpymo_assetloader_init(&al,&cfg,argv[1])==CPYMO_ERR_SUCC);
  char *path=NULL;
  assert(cpymo_assetloader_get_bgm_path(&path,cpymo_str_pure("track01"),&al)==CPYMO_ERR_SUCC);
  assert(strstr(path,"/bgm/track01.mp3")!=NULL); free(path);
  cpymo_script *script=NULL;
  assert(cpymo_script_load(&script,cpymo_str_pure("start"),&al)==CPYMO_ERR_SUCC);
  assert(script->script_content_len>5 && strstr(script->script_content,"#wait 10")!=NULL);
  cpymo_script_free(script);
  script=NULL;
  assert(cpymo_script_create_bootloader(&script,cfg.startscript)==CPYMO_ERR_SUCC);
  assert(strstr(script->script_content,"#change start")!=NULL);
  cpymo_script_free(script);
  script=NULL;
  assert(cpymo_script_load(&script,cpymo_str_pure("missing"),&al)==CPYMO_ERR_SCRIPT_FILE_NOT_FOUND);
  cpymo_assetloader_free(&al);
  remove(cfgfile);remove(scriptfile);
  printf("PASS actual CPyMO config → assetloader → script → bootloader → missing-script checks\n");
  return 0;
}
