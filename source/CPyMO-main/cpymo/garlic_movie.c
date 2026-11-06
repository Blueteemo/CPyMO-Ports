/* Bounded MJPEG/PCM AVI playback for original RG35XX. No FFmpeg runtime.
 * Only the documented converter profile is accepted; original files are
 * never edited. JPEG decoding reuses CPyMO's existing stb_image. The 48kHz
 * libretro output clock drives video, including when frontend audio is muted.
 */
#include "cpymo_prelude.h"
#ifdef GARLIC_MP3_AUDIO
#include "garlic_movie.h"
#include "cpymo_engine.h"
#include "cpymo_ui.h"
#include "../cpymo-backends/software/cpymo_backend_software.h"
#include "../stb/stb_image.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#define MOVIE_MAX_FILE (512u * 1024u * 1024u)
#define MOVIE_MAX_PACKETS 65536u
#define MOVIE_MAX_JPEG (2u * 1024u * 1024u)
typedef struct { uint32_t offset, size; } movie_packet;
typedef struct {
    FILE *file;
    cpymo_engine *engine;
    movie_packet *video, *audio;
    size_t nv, na, cv, ca, audio_index, audio_offset, max_jpeg;
    unsigned width, height, rate, scale, streams;
    bool video_header, audio_header, pcm_header, failed, skipped, completed;
    uint64_t audio_total, video_duration, emitted, underruns;
    unsigned char *compressed, *rgb;
    uint32_t *decode_times;
    size_t decoded, dropped, shown;
    uint64_t decode_total_us, max_av_error_us;
    uint32_t decode_max_us;
} garlic_movie;
static garlic_movie *active_movie;

static uint32_t le32(const unsigned char *b)
{ return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24; }
static unsigned le16(const unsigned char *b) { return b[0] | (unsigned)b[1]<<8; }
static uint64_t micros(void)
{ struct timeval t; gettimeofday(&t, NULL); return (uint64_t)t.tv_sec*1000000u+t.tv_usec; }
static bool read_at(FILE *f, uint32_t off, void *buf, size_t n)
{ return !fseek(f, (long)off, SEEK_SET) && fread(buf, 1, n, f) == n; }

static bool append_packet(movie_packet **out, size_t *n, size_t *cap, uint32_t off, uint32_t size)
{
    if (*n >= MOVIE_MAX_PACKETS) return false;
    if (*n == *cap) {
        size_t c = *cap ? *cap * 2 : 128;
        movie_packet *p = realloc(*out, c * sizeof(*p));
        if (!p) return false;
        *out = p; *cap = c;
    }
    (*out)[(*n)++] = (movie_packet){off, size};
    return true;
}

static bool scan_chunks(garlic_movie *m, uint32_t begin, uint32_t end,
                       unsigned depth, bool movi, int stream)
{
    if (depth > 5) return false;
    uint32_t pos = begin;
    while (pos < end) {
        unsigned char h[64];
        if (end - pos < 8 || !read_at(m->file, pos, h, 8)) return false;
        uint32_t size = le32(h+4), data = pos+8;
        if (size > end-data || (uint64_t)data+size+(size&1u) > end) return false;
        if (!memcmp(h, "LIST", 4)) {
            if (size < 4 || !read_at(m->file, data, h, 4)) return false;
            bool child_movi = movi || !memcmp(h, "movi", 4);
            int child_stream = stream;
            if (!memcmp(h, "strl", 4)) {
                if (m->streams >= 2) return false;
                child_stream = (int)m->streams++;
            }
            if (!scan_chunks(m, data+4, data+size, depth+1, child_movi, child_stream)) return false;
        } else if (!memcmp(h, "avih", 4)) {
            if (size < 40 || !read_at(m->file, data, h, 40)) return false;
            m->width=le32(h+32); m->height=le32(h+36);
        } else if (!memcmp(h, "strh", 4)) {
            if (size < 32 || !read_at(m->file, data, h, 32)) return false;
            if (stream == 0 && !memcmp(h,"vidsMJPG",8)) {
                m->scale=le32(h+20); m->rate=le32(h+24); m->video_header=true;
            } else if (stream == 1 && !memcmp(h,"auds",4)) m->audio_header=true;
            else return false;
        } else if (!memcmp(h, "strf", 4) && stream == 1) {
            if (size < 16 || !read_at(m->file, data, h, 16)) return false;
            m->pcm_header = le16(h)==1 && le16(h+2)==2 && le32(h+4)==48000 &&
                le32(h+8)==192000 && le16(h+12)==4 && le16(h+14)==16;
        } else if (movi && (!memcmp(h,"00dc",4) || !memcmp(h,"00db",4))) {
            if (size < 4 || size > MOVIE_MAX_JPEG ||
                !append_packet(&m->video,&m->nv,&m->cv,data,size)) return false;
            if (size > m->max_jpeg) m->max_jpeg=size;
        } else if (movi && !memcmp(h,"01wb",4)) {
            if ((size&3u) || !append_packet(&m->audio,&m->na,&m->ca,data,size)) return false;
            m->audio_total += size/4;
        }
        pos = data+size+(size&1u);
    }
    return pos == end;
}

static void release_movie(garlic_movie *m)
{
    if (m->file) fclose(m->file);
    free(m->video); free(m->audio); free(m->compressed);
    stbi_image_free(m->rgb); free(m->decode_times);
    memset(m,0,sizeof(*m));
}

static error_t decode_frame(garlic_movie *m, size_t index)
{
    movie_packet p=m->video[index];
    uint64_t start=micros();
    if (!read_at(m->file,p.offset,m->compressed,p.size)) return CPYMO_ERR_BAD_FILE_FORMAT;
    int w,h,c;
    if (m->compressed[0]!=255 || m->compressed[1]!=216 ||
        !stbi_info_from_memory(m->compressed,(int)p.size,&w,&h,&c) ||
        w!=(int)m->width || h!=(int)m->height) return CPYMO_ERR_BAD_FILE_FORMAT;
    unsigned char *rgb=stbi_load_from_memory(m->compressed,(int)p.size,&w,&h,&c,3);
    if (!rgb) return CPYMO_ERR_BAD_FILE_FORMAT;
    stbi_image_free(m->rgb); m->rgb=rgb;
    uint64_t us=micros()-start;
    uint32_t bounded=us>UINT32_MAX ? UINT32_MAX : (uint32_t)us;
    m->decode_times[m->decoded++]=bounded;
    m->decode_total_us+=us;
    if (bounded>m->decode_max_us) m->decode_max_us=bounded;
    m->shown=index;
    return CPYMO_ERR_SUCC;
}

static int compare_u32(const void *a,const void *b)
{ uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b; return (x>y)-(x<y); }

static void movie_delete(cpymo_engine *e, void *data)
{
    (void)e;
    garlic_movie *m=data;
    if (active_movie==m) active_movie=NULL;
    qsort(m->decode_times,m->decoded,sizeof(uint32_t),compare_u32);
    uint32_t p95=m->decoded ? m->decode_times[(m->decoded-1)*95/100] : 0;
    char log[512];
    snprintf(log,sizeof(log),"MOVIE end frames=%zu decoded=%zu dropped=%zu audio48k=%llu "
        "audio_total=%llu avg_us=%llu p95_us=%u max_us=%u av_error_max_us=%llu "
        "read_errors=%llu owned_bytes=%zu skipped=%d failed=%d completed=%d",
        m->nv,m->decoded,m->dropped,(unsigned long long)m->emitted,
        (unsigned long long)m->audio_total,
        (unsigned long long)(m->decoded?m->decode_total_us/m->decoded:0),p95,m->decode_max_us,
        (unsigned long long)m->max_av_error_us,(unsigned long long)m->underruns,
        sizeof(*m)+(m->cv+m->ca)*sizeof(movie_packet)+m->max_jpeg+
            m->width*m->height*3u+m->nv*sizeof(uint32_t),m->skipped,m->failed,m->completed);
    garlic_movie_log(log);
    release_movie(m);
}

static error_t movie_update(cpymo_engine *e,void *data,float dt)
{
    (void)dt;
    garlic_movie *m=data;
    if (m->failed) return CPYMO_ERR_BAD_FILE_FORMAT;
    if ((e->input.ok && !e->prev_input.ok) || (e->input.cancel && !e->prev_input.cancel)) {
        m->skipped=true; cpymo_ui_exit(e); return CPYMO_ERR_SUCC;
    }
    uint64_t duration=m->video_duration>m->audio_total ? m->video_duration:m->audio_total;
    if (m->emitted>=duration) { m->completed=true; cpymo_ui_exit(e); return CPYMO_ERR_SUCC; }
    size_t target=(size_t)(m->emitted*m->rate/((uint64_t)m->scale*48000));
    if (target>=m->nv) target=m->nv-1;
    if (!m->rgb || target!=m->shown) {
        if (m->rgb && target>m->shown+1) m->dropped+=target-m->shown-1;
        error_t err=decode_frame(m,target);
        if (err) { m->failed=true; return err; }
        cpymo_engine_request_redraw(e);
    }
    uint64_t clock_us=m->emitted*1000000/48000;
    uint64_t pts_us=(uint64_t)m->shown*m->scale*1000000/m->rate;
    if (clock_us>=pts_us && clock_us-pts_us>m->max_av_error_us)
        m->max_av_error_us=clock_us-pts_us;
    return CPYMO_ERR_SUCC;
}

static void movie_draw(const cpymo_engine *e,const void *data)
{
    const garlic_movie *m=data;
    float rect[]={0,0,e->gameconfig.imagesize_w,e->gameconfig.imagesize_h};
    cpymo_backend_image_fill_rects(rect,1,cpymo_color_black,1,
        cpymo_backend_image_draw_type_ui_element_bg);
    if (!m->rgb) return;
    float scale=rect[2]/m->width;
    if (scale*m->height>rect[3]) scale=rect[3]/m->height;
    float w=m->width*scale,h=m->height*scale;
    cpymo_backend_software_image image={0};
    image.w=m->width; image.h=m->height; image.line_stride=m->width*3;
    image.pixel_stride=3; image.r_offset=0; image.g_offset=1; image.b_offset=2;
    image.pixels=m->rgb;
    cpymo_backend_image_draw((rect[2]-w)/2,(rect[3]-h)/2,w,h,&image,0,0,
        m->width,m->height,1,cpymo_backend_image_draw_type_ui_element);
}

bool garlic_movie_audio(int16_t *dst,size_t frames)
{
    garlic_movie *m=active_movie;
    if (!m) return false;
    memset(dst,0,frames*4);
    size_t done=0;
    while (done<frames && m->audio_index<m->na) {
        movie_packet p=m->audio[m->audio_index];
        size_t n=(p.size-m->audio_offset)/4;
        if (n>frames-done) n=frames-done;
        if (n && !read_at(m->file,p.offset+(uint32_t)m->audio_offset,dst+done*2,n*4)) {
            m->underruns++; m->failed=true; break;
        }
        done+=n; m->audio_offset+=n*4;
        if (m->audio_offset==p.size) { m->audio_index++; m->audio_offset=0; }
    }
    float volume=cpymo_audio_get_channel_volume(CPYMO_AUDIO_CHANNEL_BGM,&m->engine->audio);
    if (volume<0) volume=0;
    if (volume>1) volume=1;
    int gain=(int)(volume*4096);
    for (size_t i=0;i<frames*2;i++) dst[i]=(int16_t)((int32_t)dst[i]*gain/4096);
    m->emitted+=frames;
    return true;
}

error_t garlic_movie_play(cpymo_engine *e,cpymo_str name)
{
    if (active_movie || !name.len || name.len>200) return CPYMO_ERR_INVALID_ARG;
    /* A format sidecar is an opt-in. Keep legacy gameplay usable when the
       original codec has no decoder rather than claiming it was played. */
    char *path=NULL;
    error_t err=cpymo_assetloader_get_fs_path(&path,name,"video","rg35xx.avi",&e->assetloader);
    if (err) return err;
    garlic_movie movie={0};
    movie.file=fopen(path,"rb"); free(path);
    if (!movie.file) { garlic_movie_log("MOVIE missing conversion sidecar; original codec skipped"); return CPYMO_ERR_SUCC; }
    unsigned char h[12];
    if (fseek(movie.file,0,SEEK_END)) goto bad;
    long length=ftell(movie.file);
    if (length<12 || length>MOVIE_MAX_FILE || !read_at(movie.file,0,h,12) ||
        memcmp(h,"RIFF",4) || memcmp(h+8,"AVI ",4) || le32(h+4)!=(uint32_t)length-8)
        goto bad;
    if (!scan_chunks(&movie,12,(uint32_t)length,0,false,-1) ||
        !movie.video_header || !movie.audio_header || !movie.pcm_header ||
        !movie.width || movie.width>640 || !movie.height || movie.height>480 ||
        !movie.rate || !movie.scale || movie.rate>(uint64_t)movie.scale*60 ||
        movie.scale>1000000 || !movie.nv || !movie.audio_total || movie.audio_total>48000ULL*600 ||
        movie.nv*(uint64_t)movie.scale>movie.rate*600ULL) goto bad;
    movie.compressed=malloc(movie.max_jpeg);
    movie.decode_times=calloc(movie.nv,sizeof(uint32_t));
    if (!movie.compressed || !movie.decode_times) { release_movie(&movie); return CPYMO_ERR_OUT_OF_MEM; }
    movie.video_duration=(movie.nv*(uint64_t)movie.scale*48000+movie.rate-1)/movie.rate;
    movie.engine=e;
    garlic_movie *ui=NULL;
    err=cpymo_ui_enter((void **)&ui,e,sizeof(*ui),movie_update,movie_draw,movie_delete);
    if (err) { release_movie(&movie); return err; }
    *ui=movie; active_movie=ui;
    cpymo_audio_bgm_stop(e); cpymo_audio_se_stop(e); cpymo_audio_vo_stop(e);
    char log[384];
    snprintf(log,sizeof(log),"MOVIE start name=%.*s script=%s line=%zu size=%ux%u rate=%u/%u frames=%zu audio48k=%llu",
        (int)name.len,name.begin,e->interpreter ? e->interpreter->script->script_name : "-",
        e->interpreter ? e->interpreter->script_parser.cur_line : 0,
        movie.width,movie.height,movie.rate,movie.scale,movie.nv,(unsigned long long)movie.audio_total);
    garlic_movie_log(log);
    return CPYMO_ERR_SUCC;
bad:
    release_movie(&movie);
    /* The generic interpreter tolerates some asset errors and continues.
       A rejected conversion must instead reach libretro's recovery screen. */
    garlic_abort_game(CPYMO_ERR_BAD_FILE_FORMAT);
    return CPYMO_ERR_BAD_FILE_FORMAT;
}
#endif
