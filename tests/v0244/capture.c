/* Libretro integration harness. Real inputs and read-only engine observations.
   Audio is measured in memory; only requested frames are retained. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <libretro.h>
#include "cpymo_engine.h"
#include <dlfcn.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

static char outdir[2048];
static unsigned frames, polls, captures;
static uint32_t motion_hashes[512];
static unsigned motion_samples, motion_unique, motion_black;
static bool shutdown_requested;
static bool capture_enabled=true;
static uint16_t keys[12001];
static unsigned max_frames=600, capture_every=30;
static cpymo_engine *observed;
static float (*observed_volume)(size_t,const cpymo_audio_system *);
static unsigned last_line=~0u; static int last_ui=-1, last_read=-1;
static void inspect_state(void) {
    if (!observed) return;
    unsigned line = observed->interpreter ? observed->interpreter->script_parser.cur_line : 0;
    int ui=observed->ui!=NULL, read=observed->say.current_say_is_already_read;
    if(line!=last_line || ui!=last_ui || read!=last_read || polls%60==0) {
        printf("STATE frame=%u line=%u ui=%d say=%d read=%d alpha=%.4f font=%.4f choice=%d auto=%d hidden=%d\n",
            polls,line,ui,observed->say.active,read,observed->say.textbox_usable ?
            observed->say.textbox.alpha : -1,cpymo_gameconfig_font_size(&observed->gameconfig),
            observed->select_img.selections!=NULL,observed->say.auto_mode,observed->say.hide_window);
        printf("DETAIL frame=%u tbfont=%.5f chars=%zu lines=%zu history=%zu onlyread=%d large=%d remaining=%zu source=%zu\n",polls,observed->say.textbox.char_size,observed->say.textbox.chars_pool_size,observed->say.textbox.max_lines,observed->backlog.next_record_to_write,observed->config_skip_already_read_only,observed->gameconfig.garlic_font_large,observed->say.textbox.remain_text.len,observed->say.textbox.page_text.len);
        printf("GAME frame=%u dir=%s gray=%d vols=%.2f,%.2f,%.2f\n",polls,
            observed->assetloader.gamedir ? observed->assetloader.gamedir : "LIBRARY",
            observed->gameconfig.garlic_read_gray,
            observed_volume ? observed_volume(0,&observed->audio) : -1,
            observed_volume ? observed_volume(1,&observed->audio) : -1,
            observed_volume ? observed_volume(2,&observed->audio) : -1);
        last_line=line;last_ui=ui;last_read=read;
    }
}
static bool (*se_playing)(size_t, cpymo_audio_system *);
static bool (*se_looping)(size_t, cpymo_audio_system *);
static const char *(*se_name)(struct cpymo_engine *);
static FILE *wav;
static long wav_frames;
static float peak; static double sumsq; static long nonnull;

static void logger(enum retro_log_level level,const char *fmt,...)
{ (void)level; va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap); }
static bool environment(unsigned cmd,void *data) {
    switch(cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            ((struct retro_log_callback *)data)->log=logger;return true;
        case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: return false;
        case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: return false;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data="/nonexistent";return true;
        case RETRO_ENVIRONMENT_SHUTDOWN: shutdown_requested=true;return true;
        default:return false;
    }
}
static void video(const void *pixels,unsigned w,unsigned h,size_t pitch) {
    if (!pixels || w!=640 || h!=480 || pitch<w*2) { fputs("empty frame\n",stderr); exit(3); }
    ++frames;
    if (!(frames%30)) {
        uint32_t hash=2166136261u; bool black=true;
        for(unsigned y=0;y<h;y+=16)for(unsigned x=0;x<w;x+=16) {
            uint16_t pixel;memcpy(&pixel,(const uint8_t*)pixels+y*pitch+x*2,2);
            hash=(hash^pixel)*16777619u;if(pixel)black=false;
        }
        bool known=false;for(unsigned j=0;j<motion_unique;j++)if(motion_hashes[j]==hash)known=true;
        if(!known&&motion_unique<512)motion_hashes[motion_unique++]=hash;
        motion_samples++;if(black)motion_black++;
    }
    if (!capture_enabled) return;
    if (frames%capture_every && frames!=max_frames) return;
    char name[2200]; snprintf(name,sizeof(name),"%s/frame_%04u.ppm",outdir,frames);
    FILE *f=fopen(name,"wb"); if(!f) { perror("frame open");exit(30); }
    static unsigned char frame[640*480*3];
    fprintf(f,"P6\n%u %u\n255\n",w,h);
    for(unsigned y=0;y<h;y++){const uint8_t*l=(const uint8_t*)pixels+y*pitch;
        for(unsigned x=0;x<w;x++){uint16_t p;memcpy(&p,l+x*2,2);
            unsigned r=(p>>11)&31u,g=(p>>5)&63u,b=p&31u;
            size_t off=((size_t)y*w+x)*3;
            frame[off]=(unsigned char)((r<<3)|(r>>2));
            frame[off+1]=(unsigned char)((g<<2)|(g>>4));
            frame[off+2]=(unsigned char)((b<<3)|(b>>2));
        }
    }
    if(fwrite(frame,1,(size_t)w*h*3,f)!=(size_t)w*h*3) { perror("frame write");exit(31); }
    if(fclose(f)) { perror("frame close");exit(32); }
    ++captures;
}
static void poll(void) {
    ++polls;
    static unsigned phase, marker;
    if (getenv("CPYMO_TEST_TRAIN") && observed && observed->interpreter) {
        unsigned line=observed->interpreter->script_parser.cur_line;
        if (!phase) {
            if (line>=93 && line<=105 && observed->say.active) {
                keys[polls]=1u<<RETRO_DEVICE_ID_JOYPAD_R2;
                phase=1; marker=polls;
                printf("TRAIN save frame=%u line=%u\n",polls,line);
            } else if (!(polls%8)) keys[polls]=1u<<RETRO_DEVICE_ID_JOYPAD_A;
        } else if (phase==1 && polls-marker==120) {
            keys[polls]=1u<<RETRO_DEVICE_ID_JOYPAD_L2;
            phase=2; marker=polls;
            printf("TRAIN load frame=%u\n",polls);
        }
    }
    if (observed && se_name && !(polls%60))
        printf("SE frame=%u playing=%d looping=%d saved_name=%s\n",polls,
            se_playing(CPYMO_AUDIO_CHANNEL_SE,&observed->audio),
            se_looping(CPYMO_AUDIO_CHANNEL_SE,&observed->audio),se_name(observed));
    if (observed && se_playing && !(polls%60))
        printf("CHANNELS frame=%u bgm=%d se=%d voice=%d\n",polls,
            se_playing(CPYMO_AUDIO_CHANNEL_BGM,&observed->audio),
            se_playing(CPYMO_AUDIO_CHANNEL_SE,&observed->audio),
            se_playing(CPYMO_AUDIO_CHANNEL_VO,&observed->audio));
}
static int16_t state(unsigned port,unsigned device,unsigned index,unsigned id) {
    if (port||index||device!=RETRO_DEVICE_JOYPAD) return 0;
    return polls < 12001 && id < 16 && (keys[polls] & (1u<<id)) ? 1 : 0;
    return 0;
}
static size_t audio_batch(const int16_t *buf,size_t n) {
    if (!wav || n==0) return n;
    /* No raw PCM artifact: sample counters and energy prove output continuity. */
    wav_frames += (long)n;
    for (size_t i=0;i<n*2;i++){float v=fabsf(buf[i]/32768.0f);
        if(v>peak)peak=v; sumsq+=(double)v*v; if(buf[i])nonnull++;}
    return n;
}
#define SYMBOL(name) do { *(void**)(&name)=dlsym(handle,#name); \
    if (!name){fprintf(stderr,"missing %s\n",#name);return 5;} } while(0)
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if (argc!=5){fprintf(stderr,"usage: %s so gameconfig outdir timeline\n",argv[0]);return 2;}
    const char *mf=getenv("CPYMO_TEST_FRAMES"), *ce=getenv("CPYMO_TEST_CAPTURE_EVERY");
    capture_enabled=getenv("CPYMO_TEST_NO_CAPTURE")==NULL;
    if(mf) max_frames=(unsigned)strtoul(mf,NULL,10);
    if(ce) capture_every=(unsigned)strtoul(ce,NULL,10);
    if(!capture_every || max_frames>12000) return 2;
    FILE *timeline=fopen(argv[4],"r"); if(!timeline) return 2;
    unsigned fr,mask; while(fscanf(timeline,"%u %u",&fr,&mask)==2) {
        if(fr<12001) keys[fr]=(uint16_t)mask;
    } fclose(timeline);
    strncpy(outdir,argv[3],sizeof(outdir)-32); mkdir(outdir,0755);
    char wp[2200]; snprintf(wp,sizeof(wp),"%s/audio.wav",outdir);
    wav=tmpfile(); if(!wav) return 35;
    /* 44 byte placeholder, fixed up at the end */
    unsigned char hdr[44]={0}; fwrite(hdr,1,44,wav);
    void *handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!handle){fprintf(stderr,"dlopen: %s\n",dlerror());return 3;}
    unsigned(*retro_api_version)(void);
    void(*retro_set_environment)(retro_environment_t);
    void(*retro_set_video_refresh)(retro_video_refresh_t);
    void(*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void(*retro_set_input_poll)(retro_input_poll_t);
    void(*retro_set_input_state)(retro_input_state_t);
    void(*retro_init)(void);
    bool(*retro_load_game)(const struct retro_game_info*);
    void(*retro_run)(void); void(*retro_unload_game)(void); void(*retro_deinit)(void);
    SYMBOL(retro_api_version);SYMBOL(retro_set_environment);SYMBOL(retro_set_video_refresh);
    SYMBOL(retro_set_audio_sample_batch);SYMBOL(retro_set_input_poll);SYMBOL(retro_set_input_state);
    SYMBOL(retro_init);SYMBOL(retro_load_game);SYMBOL(retro_run);SYMBOL(retro_unload_game);SYMBOL(retro_deinit);
    if(retro_api_version()!=RETRO_API_VERSION)return 5;
    Dl_info di; const char *eo=getenv("CPYMO_TEST_ENGINE_OFFSET");
    if(eo && dladdr((void*)retro_run,&di))
        observed=(cpymo_engine*)((char*)di.dli_fbase+strtoul(eo,NULL,16));
    if (eo && dladdr((void*)retro_run,&di)) {
        const char *o=getenv("CPYMO_TEST_VOLUME_OFFSET");
        if(o) *(void**)(&observed_volume)=(char*)di.dli_fbase+strtoul(o,NULL,16);
        o=getenv("CPYMO_TEST_SE_NAME_OFFSET");
        if(o) *(void**)(&se_name)=(char*)di.dli_fbase+strtoul(o,NULL,16);
        o=getenv("CPYMO_TEST_SE_PLAYING_OFFSET");
        if(o) *(void**)(&se_playing)=(char*)di.dli_fbase+strtoul(o,NULL,16);
        o=getenv("CPYMO_TEST_SE_LOOPING_OFFSET");
        if(o) *(void**)(&se_looping)=(char*)di.dli_fbase+strtoul(o,NULL,16);
    }
    retro_set_environment(environment);retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio_batch);retro_set_input_poll(poll);
    retro_set_input_state(state);retro_init();
    struct retro_game_info g={argv[2],NULL,0,NULL};
    if(!retro_load_game(&g)){fprintf(stderr,"load failed\n");return 6;}
    for(unsigned i=0;i<max_frames && !shutdown_requested;i++) { retro_run(); inspect_state(); }
    retro_unload_game();retro_deinit();dlclose(handle);
    long bytes=wav_frames*4;
    hdr[0]='R';hdr[1]='I';hdr[2]='F';hdr[3]='F';
    unsigned riff=(unsigned)(36+bytes);memcpy(hdr+4,&riff,4);
    hdr[8]='W';hdr[9]='A';hdr[10]='V';hdr[11]='E';hdr[12]='f';hdr[13]='m';hdr[14]='t';hdr[15]=' ';
    unsigned v=16;memcpy(hdr+16,&v,4);unsigned short pcm=1,ch=2;memcpy(hdr+20,&pcm,2);
    memcpy(hdr+22,&ch,2);unsigned rate=48000;memcpy(hdr+24,&rate,4);
    unsigned br=48000*4;memcpy(hdr+28,&br,4);unsigned short ba=4;memcpy(hdr+32,&ba,2);
    unsigned short bps=16;memcpy(hdr+34,&bps,2);
    hdr[36]='d';hdr[37]='a';hdr[38]='t';hdr[39]='a';unsigned ds=(unsigned)bytes;memcpy(hdr+40,&ds,4);
    if(fseek(wav,0,SEEK_SET) || fwrite(hdr,1,44,wav)!=44 || fclose(wav)) { perror("audio close");return 34; }
    double rms = wav_frames? sqrt(sumsq/(double)(wav_frames*2)) : 0;
    printf("VIDEO samples=%u unique=%u black=%u\n",motion_samples,motion_unique,motion_black);
    printf("AUDIO frames=%ld peak=%.3f rms=%.4f pct=%ld video_frames=%u caps=%u shutdown=%d\n",
        wav_frames,peak,rms,wav_frames?nonnull*100/(wav_frames*2):0,frames,captures,shutdown_requested);
    return 0;
}
