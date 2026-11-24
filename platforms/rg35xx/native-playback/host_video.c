/* Desktop-only decoder to test the MP4 and presentation pipeline. The
   device build links vde_video.c, with no software H.264 fallback. */
#include "video.h"
#include <libavcodec/avcodec.h>
#include <stdint.h>
#include <stdlib.h>

struct video_decoder {
    AVCodecContext *codec;
    AVFrame *frame;
};

struct video_decoder *video_open(const uint8_t *bytes, unsigned size)
{
    (void)bytes; (void)size;
    struct video_decoder *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    AVCodec *h264 = avcodec_find_decoder(AV_CODEC_ID_H264);
    v->codec = avcodec_alloc_context3(h264);
    v->frame = av_frame_alloc();
    if (!h264 || !v->codec || !v->frame || avcodec_open2(v->codec, h264, NULL) < 0) {
        video_close(v);
        return NULL;
    }
    return v;
}

int video_packet(struct video_decoder *v, const uint8_t *bytes, unsigned size, unsigned pts,
    video_sink sink, void *arg)
{
    for (unsigned drain = 0; drain < 32; ++drain) {
        int got = 0;
        AVPacket packet;
        av_init_packet(&packet);
        packet.data = (uint8_t *)bytes;
        packet.size = size;
        packet.pts = bytes ? pts : AV_NOPTS_VALUE;
        av_frame_unref(v->frame);
        if (avcodec_decode_video2(v->codec, v->frame, &got, &packet) < 0)
            return -1;
        if (got) {
            AVFrame *f = v->frame;
            if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P)
                return -1;
            if (f->pkt_pts == AV_NOPTS_VALUE || f->pkt_pts < 0 || f->pkt_pts > UINT32_MAX)
                return -1;
            struct video_image image = {f->data[0], f->data[1], f->data[2],
                f->linesize[0], f->linesize[1], 1, f->width, f->height, (unsigned)f->pkt_pts};
            if (sink(arg, &image)) return -1;
        }
        if (bytes || !got) return 0;
    }
    return -1;
}

void video_close(struct video_decoder *v)
{
    if (!v) return;
    avcodec_free_context(&v->codec);
    av_frame_free(&v->frame);
    free(v);
}
