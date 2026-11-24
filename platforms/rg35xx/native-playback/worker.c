#include "shared.h"
#include "video.h"
#include "blit.h"
#include "timing.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

struct track {
    AVFormatContext *file;
    AVStream *stream;
    unsigned index;
};

struct job {
    const char *path;
    struct native_playback *shared;
    unsigned pictures, decoded, late_drops, last_pts, width, height, chroma_step;
    struct native_blitter blitter;
    uint64_t blit_total_us, blit_max_us;
};

static int stop(const struct job *job)
{
    return native_load(&job->shared->stop) ||
        native_load(&job->shared->state) == NATIVE_FAILED;
}

static int track_open(struct track *track, const char *path, enum AVCodecID codec)
{
    AVInputFormat *mov = av_find_input_format("mov");
    if (!mov || avformat_open_input(&track->file, path, mov, NULL) < 0)
        return -1;
    for (unsigned i = 0; i < track->file->nb_streams; ++i) {
        if (track->file->streams[i]->codec->codec_id == codec) {
            track->stream = track->file->streams[i];
            track->index = i;
            return 0;
        }
    }
    avformat_close_input(&track->file);
    return -1;
}

static int64_t track_time(const struct track *track, int64_t pts, int scale)
{
    if (pts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
    int64_t origin = track->file->start_time == AV_NOPTS_VALUE ? 0 : track->file->start_time;
    return av_rescale_q(pts, track->stream->time_base, (AVRational){1, scale}) -
        av_rescale(origin, scale, AV_TIME_BASE);
}

static int audio_write(struct job *job, const int16_t *samples, unsigned count)
{
    struct native_playback *s = job->shared;
    uint32_t cursor = native_load(&s->audio_write);
    while (count && !stop(job)) {
        uint32_t used = cursor - native_load(&s->audio_read);
        if (used == NATIVE_AUDIO_SAMPLES) {
            usleep(2000);
            continue;
        }
        unsigned offset = cursor % NATIVE_AUDIO_SAMPLES;
        unsigned length = NATIVE_AUDIO_SAMPLES - offset;
        if (length > NATIVE_AUDIO_SAMPLES - used) length = NATIVE_AUDIO_SAMPLES - used;
        if (length > count) length = count;
        if (samples) {
            memcpy(s->audio + offset*2, samples, length*4);
            samples += length*2;
        } else {
            memset(s->audio + offset*2, 0, length*4);
        }
        count -= length;
        cursor += length;
        native_store(&s->audio_write, cursor);
    }
    return count ? -1 : 0;
}

struct audio_track {
    struct track input;
    AVFrame *frame;
    SwrContext *resample;
    int sample_rate, sample_format, channels;
    int64_t first_sample, decoded_samples;
    unsigned max_timing_error;
    unsigned trim;
    int started;
};

static int audio_output(struct job *job, struct audio_track *audio, int drain)
{
    AVFrame *frame = audio->frame;
    if (!audio->resample) {
        int64_t layout = frame->channel_layout;
        if (!layout) layout = av_get_default_channel_layout(frame->channels);
        if (!layout || frame->sample_rate <= 0) return -1;
        audio->sample_rate = frame->sample_rate;
        audio->sample_format = frame->format;
        audio->channels = frame->channels;
        audio->resample = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16,
            NATIVE_AUDIO_RATE, layout, frame->format, frame->sample_rate, 0, NULL);
        if (!audio->resample || swr_init(audio->resample) < 0) return -1;
    }
    if (!drain && (audio->sample_rate != frame->sample_rate ||
        audio->sample_format != frame->format || audio->channels != frame->channels))
        return -1;

    if (!audio->started) {
        int64_t timestamp = av_frame_get_best_effort_timestamp(frame);
        if (timestamp == AV_NOPTS_VALUE) timestamp = frame->pkt_pts;
        int64_t first = track_time(&audio->input, timestamp, NATIVE_AUDIO_RATE);
        if (first == AV_NOPTS_VALUE) first = 0;
        if (first < -NATIVE_AUDIO_RATE || first > NATIVE_AUDIO_RATE*10) return -1;
        audio->first_sample = first;
        audio->trim = first < 0 ? (unsigned)-first : 0;
        if (first > 0 && audio_write(job, NULL, (unsigned)first)) return -1;
        audio->started = 1;
        fprintf(stderr, "aac_start_sample=%lld source_rate=%d\n", (long long)first,
            audio->sample_rate);
    }
    if (!drain) {
        int64_t pts = track_time(&audio->input, av_frame_get_best_effort_timestamp(frame), NATIVE_AUDIO_RATE);
        int64_t expected = audio->first_sample +
            av_rescale(audio->decoded_samples, NATIVE_AUDIO_RATE, audio->sample_rate);
        if (pts != AV_NOPTS_VALUE) {
            int64_t drift = llabs(pts-expected);
            if (drift > NATIVE_AUDIO_RATE/100) {
                fprintf(stderr, "aac_discontinuous_pts=%lld expected=%lld\n",
                    (long long)pts, (long long)expected);
                return -1;
            }
            if ((unsigned)drift > audio->max_timing_error) audio->max_timing_error = (unsigned)drift;
        }
        audio->decoded_samples += frame->nb_samples;
    }

    int16_t output[8192 * 2];
    uint8_t *bytes = (uint8_t *)output;
    int count = swr_convert(audio->resample, &bytes, 8192,
        drain ? NULL : (const uint8_t **)frame->extended_data, drain ? 0 : frame->nb_samples);
    if (count < 0) return -1;
    unsigned discard = audio->trim < (unsigned)count ? audio->trim : (unsigned)count;
    audio->trim -= discard;
    if (audio_write(job, output + discard*2, (unsigned)count-discard)) return -1;
    return count;
}

static int aac_packet(struct job *job, struct audio_track *audio, AVPacket *packet)
{
    AVPacket remaining = *packet;
    do {
        int got = 0;
        av_frame_unref(audio->frame);
        int consumed = avcodec_decode_audio4(audio->input.stream->codec, audio->frame, &got, &remaining);
        if (consumed < 0 || consumed > remaining.size) return -1;
        if (got && audio_output(job, audio, 0) < 0) return -1;
        if (!consumed) return got;
        remaining.data += consumed;
        remaining.size -= consumed;
        remaining.pts = remaining.dts = AV_NOPTS_VALUE;
        remaining.side_data_elems = 0;
    } while (remaining.size);
    return 0;
}

static void *audio_main(void *arg)
{
    struct job *job = arg;
    struct audio_track audio = {0};
    int result = -1;
    int codec_open = 0;
    if (track_open(&audio.input, job->path, AV_CODEC_ID_AAC)) goto done;
    AVCodecContext *codec = audio.input.stream->codec;
    AVCodec *aac = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!aac || avcodec_open2(codec, aac, NULL) < 0) goto done;
    codec_open = 1;
    audio.frame = av_frame_alloc();
    if (!audio.frame) goto done;
    int read_result = 0;
    while (!stop(job)) {
        AVPacket packet;
        read_result = av_read_frame(audio.input.file, &packet);
        if (read_result < 0) break;
        int rc = packet.stream_index == (int)audio.input.index ? aac_packet(job, &audio, &packet) : 0;
        av_free_packet(&packet);
        if (rc < 0) goto done;
    }
    if (stop(job)) goto done;
    if (read_result != AVERROR_EOF) goto done;
    for (unsigned i = 0; i < 32; ++i) {
        AVPacket empty;
        av_init_packet(&empty);
        empty.data = NULL;
        empty.size = 0;
        int rc = aac_packet(job, &audio, &empty);
        if (rc < 0) goto done;
        if (!rc) break;
        if (i == 31) goto done;
    }
    if (!audio.started) goto done;
    for (;;) {
        int rc = audio_output(job, &audio, 1);
        if (rc < 0) goto done;
        if (!rc) break;
    }
    fprintf(stderr, "aac_output_samples=%u aac_max_pts_error_samples=%u\n",
        native_load(&job->shared->audio_write), audio.max_timing_error);
    result = 0;
done:
    if (result && !stop(job)) {
        fprintf(stderr, "aac_decode_failed\n");
        native_store(&job->shared->state, NATIVE_FAILED);
    }
    if (codec_open) avcodec_close(audio.input.stream->codec);
    swr_free(&audio.resample);
    av_frame_free(&audio.frame);
    avformat_close_input(&audio.input.file);
    native_store(&job->shared->audio_done, 1);
    return NULL;
}

static void compute_layout(struct job *job, const struct video_image *image)
{
    job->width = image->width;
    job->height = image->height;
    job->chroma_step = image->chroma_step;
    struct native_blitter *b = &job->blitter;
    native_blit_setup(b, image);
    fprintf(stderr, "video_layout=%ux%u at %u,%u from %ux%u rgb565_backend=%s\n",
        b->width, b->height, b->x, b->y, job->width, job->height, b->backend);
}

static int video_write(void *opaque, const struct video_image *image)
{
    struct job *job = opaque;
    struct native_playback *s = job->shared;
    if (!image->width || !image->height || image->width > NATIVE_MAX_SOURCE_WIDTH || image->height > 1088 ||
        image->pts_ms > NATIVE_MAX_MS || (job->decoded && image->pts_ms <= job->last_pts)) {
        fprintf(stderr, "video_timestamp_or_size_invalid pts=%u last=%u\n",
            image->pts_ms, job->last_pts);
        return -1;
    }
    ++job->decoded;
    job->last_pts = image->pts_ms;
    if (stop(job)) return -1;
    /* Decoder references still progress, but do not spend time scaling a
       picture that has already missed the current audio-driven playhead. */
    if (job->pictures && image->pts_ms + 120 < native_load(&s->playhead_ms)) {
        ++job->late_drops;
        return 0;
    }
    if (job->width != image->width || job->height != image->height || job->chroma_step != image->chroma_step)
        compute_layout(job, image);
    uint32_t cursor = native_load(&s->video_write);
    while (cursor - native_load(&s->video_read) == NATIVE_VIDEO_SLOTS && !stop(job))
        usleep(2000);
    if (stop(job)) return -1;
    struct native_frame *frame = &s->video[cursor % NATIVE_VIDEO_SLOTS];
    uint64_t begin = native_time_us();
    native_blit_frame(&job->blitter, image, frame->pixels);
    frame->pts_ms = image->pts_ms;
    uint64_t elapsed = native_time_us() - begin;
    job->blit_total_us += elapsed;
    if (elapsed > job->blit_max_us) job->blit_max_us = elapsed;
    ++job->pictures;
    native_store(&s->video_write, cursor+1);
    return 0;
}

static int video_main(struct job *job)
{
    struct track track = {0};
    struct video_decoder *decoder = NULL;
    AVBitStreamFilterContext *filter = NULL;
    int result = -1, read_result = 0;
    unsigned packets = 0;
    if (track_open(&track, job->path, AV_CODEC_ID_H264)) goto done;
    filter = av_bitstream_filter_init("h264_mp4toannexb");
    if (!filter) goto done;
    while (!stop(job)) {
        AVPacket packet;
        read_result = av_read_frame(track.file, &packet);
        if (read_result < 0) break;
        if (packet.stream_index != (int)track.index) {
            av_free_packet(&packet);
            continue;
        }
        int64_t pts = track_time(&track, packet.pts, 1000);
        uint8_t *bytes = NULL;
        int size = 0;
        int converted = av_bitstream_filter_filter(filter, track.stream->codec, NULL,
            &bytes, &size, packet.data, packet.size, packet.flags & AV_PKT_FLAG_KEY);
        int rc = -1;
        if (converted >= 0 && size > 0 && pts != AV_NOPTS_VALUE && pts <= NATIVE_MAX_MS) {
            if (!decoder) decoder = video_open(bytes, (unsigned)size);
            if (decoder)
                rc = video_packet(decoder, bytes, (unsigned)size, pts > 0 ? (unsigned)pts : 0,
                    video_write, job);
        }
        if (converted > 0) av_free(bytes);
        av_free_packet(&packet);
        if (rc) goto done;
        ++packets;
    }
    if (stop(job) || read_result != AVERROR_EOF || !decoder) goto done;
    if (video_packet(decoder, NULL, 0, 0, video_write, job) || !job->pictures) goto done;
    result = 0;
done:
    fprintf(stderr, "video_packets=%u video_frames=%u decoded_frames=%u late_drop=%u last_pts_ms=%u result=%d "
        "rgb565_avg_us=%llu rgb565_max_us=%llu\n", packets, job->pictures, job->decoded,
        job->late_drops, job->last_pts, result,
        (unsigned long long)(job->pictures ? job->blit_total_us/job->pictures : 0),
        (unsigned long long)job->blit_max_us);
    if (result && !stop(job)) native_store(&job->shared->state, NATIVE_FAILED);
    video_close(decoder);
    if (filter) av_bitstream_filter_close(filter);
    avformat_close_input(&track.file);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: native-video <shared-buffer> <original-mp4>\n");
        return 2;
    }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) return 3;
    struct stat size;
    if (fstat(fd, &size) || size.st_size != sizeof(struct native_playback)) {
        close(fd);
        return 3;
    }
    struct native_playback *s = mmap(NULL, sizeof(*s), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED) return 3;
    if (s->magic != NATIVE_MAGIC || s->version != NATIVE_VERSION ||
        native_load(&s->state) != NATIVE_STARTING) {
        munmap(s, sizeof(*s));
        return 3;
    }
    struct job job = { .path = argv[2], .shared = s };
    av_log_set_level(AV_LOG_ERROR);
    av_register_all();
    native_store(&s->state, NATIVE_RUNNING);
    pthread_t audio;
    if (pthread_create(&audio, NULL, audio_main, &job)) {
        native_store(&s->state, NATIVE_FAILED);
        munmap(s, sizeof(*s));
        return 4;
    }
    int result = video_main(&job);
    pthread_join(audio, NULL);
    int failed = native_load(&s->state) == NATIVE_FAILED || (result && !stop(&job));
    native_store(&s->state, failed ? NATIVE_FAILED : NATIVE_FINISHED);
    munmap(s, sizeof(*s));
    return failed ? 5 : 0;
}
