/* Test-only full-duration probe of the production decoder; no audio mock. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
static unsigned seeks;
static int probe_seek(FILE *f,long off,int whence) {
    ++seeks;
    return fseek(f,off,whence);
}
#define fseek probe_seek
#ifndef AUDIO_SOURCE
#define AUDIO_SOURCE "../../source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c"
#endif
#include AUDIO_SOURCE
#undef fseek
#include <assert.h>
void garlic_audio_trace(const char *event,const char *name,uint64_t output_frames,
    uint64_t cursor,long off,const cpymo_engine *e) {
    (void)event;(void)name;(void)output_frames;(void)cursor;(void)off;(void)e;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    garlic_mp3_channel *c=&garlic_ch[0];
    unsigned long long lengths[2];
    for(int loop=0;loop<2;loop++){
        assert(ch_play(0,strdup(argv[1]),NULL,cpymo_str_pure("probe"),loop)==0);
        unsigned long long n=0;int16_t l,r;seeks=0;
        while(ch_frame(c,&l,&r)){
            if(loop && seeks)break;
            if(++n>48000ULL*1200) return 4;
        }
        lengths[loop]=n;
        printf("PASS loop=%d frames=%llu rate=%d seconds=%.9f filepos=%ld ring=%zu/%zu pcm=%zu/%zu eof=%d rewinds=%u\n",loop,n,c->rate,(double)n/c->rate,c->file?ftell(c->file):-1L,c->ring_pos,c->ring_len,c->pcm_cur,c->pcm_frames,c->eof,seeks);
#ifndef CPYMO_AUDIO_BASELINE
        if(loop) {
            assert(n==lengths[0]);
            assert(ch_loop_restart(c,0));
            printf("RESTART frames=%llu pcm=%zu/%zu eof=%d\n",n,c->pcm_cur,c->pcm_frames,c->eof);
        }
#endif
        ch_stop(c);
    }
    free(c->ring);free(c->name);free(c->res_tab);
    return 0;
}
