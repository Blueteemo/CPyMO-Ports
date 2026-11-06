/* Calls the production decoder, resampler and mixer. Test-only raw output. */
#define _POSIX_C_SOURCE 200809L
#ifndef AUDIO_SOURCE
#define AUDIO_SOURCE "../../source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c"
#endif
#include AUDIO_SOURCE
#include <time.h>
void garlic_audio_trace(const char *a,const char *b,uint64_t c,uint64_t d,long e,const cpymo_engine *f)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
int main(int argc,char **argv)
{
    if(argc<5)return 2;
    unsigned frames=strtoul(argv[3],NULL,10),mode=strtoul(argv[4],NULL,10);
    cpymo_audio_system sys;cpymo_audio_init(&sys);
    unsigned channels=argc-5 ? argc-5 : 1;
    if(channels>3)return 2;
    for(unsigned j=0;j<channels;j++){
        const char *path=argc>5 ? argv[j+5] : argv[1];
        if(ch_play(j,strdup(path),NULL,cpymo_str_pure("signal"),getenv("PROBE_LOOP")!=NULL))return 3;
        garlic_ch[j].volume=getenv("PROBE_VOLUME") ? strtof(getenv("PROBE_VOLUME"),NULL) : 1.0f;
    }
    if(getenv("PROBE_VOLUMES")){
        const char *v=getenv("PROBE_VOLUMES");
        for(unsigned j=0;j<channels && *v;j++){
            char *next;garlic_ch[j].volume=strtof(v,&next);
            v=(*next==',') ? next+1 : next;
        }
    }
    if(!frames)frames=(mode==2 ? garlic_ch[0].rate : 48000)*8;
    FILE *f=fopen(argv[2],"wb");if(!f)return 4;
    clock_t begin=clock();
    for(unsigned at=0;at<frames;){
        unsigned n=frames-at;if(n>512)n=512;
        if(mode==2){
            int16_t buf[1024]={0};
            for(unsigned i=0;i<n;i++){
                if(!ch_frame(&garlic_ch[0],buf+i*2,buf+i*2+1))break;
            }
            if(fwrite(buf,4,n,f)!=n)return 4;
        } else if(mode){
            int16_t buf[1024];cpymo_audio_copy_mixed_samples(buf,n*4,&sys);
            for(unsigned i=0;i<n*2;i++)buf[i]=(int16_t)((int32_t)buf[i]*50/100);
            if(fwrite(buf,4,n,f)!=n)return 4;
        }else{
            int32_t buf[1024]={0};for(unsigned j=0;j<channels;j++)ch_mix(&garlic_ch[j],buf,n);
            if(fwrite(buf,8,n,f)!=n)return 4;
        }
        at+=n;
    }
    printf("CPU_SECONDS %.9f frames=%u source_rate=%d source_channels=%d frac=%u step=%u\n",
        (double)(clock()-begin)/CLOCKS_PER_SEC,frames,garlic_ch[0].rate,
        garlic_ch[0].channels,garlic_ch[0].frac,garlic_ch[0].step);
    fclose(f);cpymo_audio_free(&sys);return 0;
}
