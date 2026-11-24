#include "video.h"
#include "shared.h"
#include "timing.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { MAX_PACKET = 2 * 1024 * 1024, PACKET_PADDING = 4096, MAX_OUTPUTS = 12 };

/* The 32-bit Actions video plug-in interface used in the hardware probe. */
struct packet_header {
    uint32_t type, length, offset, timestamp, reserved1, reserved2;
    uint8_t eof, parser_format, seek_reset, padding;
};
struct stream_buffer {
    uint8_t *virtual_address, *physical_address;
    unsigned length;
    void *reserved;
};
struct video_info {
    unsigned width, height, coded_width, coded_height, x, y, references, extra_size, unsupported;
};
struct frame_info {
    unsigned display, use, timestamp, width, height, format, reserved1, reserved2;
};
struct frame {
    struct frame_info *info;
    uint8_t *virtual_address, *physical_address;
    unsigned size;
};
struct frame_port {
    void *(*get_buffer)(struct frame_port *, unsigned);
    void *(*try_buffer)(struct frame_port *, unsigned);
};
struct plugin {
    char extension[8];
    void *(*open)(void *, void *, void *);
    int (*decode)(void *, struct stream_buffer *);
    int (*dispose)(void *);
    int (*control)(void *, int, unsigned);
    int (*probe)(void *, struct stream_buffer *, struct video_info *);
};

struct video_decoder {
    struct frame_port port;
    void *library, *handle;
    const struct plugin *plugin;
    void *(*allocate)(int, void *);
    void (*release)(void *);
    struct stream_buffer stream;
    struct video_info info;
    struct frame output[MAX_OUTPUTS];
    struct frame_info metadata[MAX_OUTPUTS];
    unsigned output_count, output_size;
    unsigned decode_calls;
    uint64_t decode_total_us, decode_max_us;
};

_Static_assert(sizeof(struct packet_header) == 28, "Actions packet layout");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(struct stream_buffer) == 16, "Actions stream layout");
_Static_assert(sizeof(struct frame) == 16, "Actions frame layout");
_Static_assert(sizeof(struct plugin) == 28, "Actions plugin layout");
#endif

static void *get_buffer(struct frame_port *port, unsigned bytes)
{
    struct video_decoder *v = (struct video_decoder *)port;
    if (bytes > v->output_size) return NULL;
    for (unsigned i = 0; i < v->output_count; ++i) {
        if (!v->metadata[i].display && !v->metadata[i].use)
            return &v->output[i];
    }
    if (v->output_count == MAX_OUTPUTS) {
        fprintf(stderr, "vde_buffer_pool_full=%u\n", v->output_count);
        return NULL;
    }
    unsigned i = v->output_count;
    uint32_t bus = 0;
    uint8_t *memory = v->allocate(v->output_size, &bus);
    if (!memory || memory == (void *)-1) return NULL;
    if (!bus) {
        v->release(memory);
        return NULL;
    }
    v->output[i] = (struct frame){ &v->metadata[i], memory,
        (uint8_t *)(uintptr_t)bus, v->output_size };
    ++v->output_count;
    return &v->output[i];
}

static void fill_packet(struct video_decoder *v, const uint8_t *bytes, unsigned size,
    unsigned pts)
{
    struct packet_header header = { .type = 0x200, .length = size,
        .timestamp = pts, .eof = bytes == NULL };
    memcpy(v->stream.virtual_address, &header, sizeof(header));
    if (size) memcpy(v->stream.virtual_address + sizeof(header), bytes, size);
    memset(v->stream.virtual_address + sizeof(header) + size, 0, PACKET_PADDING);
    v->stream.length = sizeof(header) + size;
}

static int emit_frames(struct video_decoder *v, video_sink sink, void *arg, unsigned *emitted)
{
    for (;;) {
        unsigned chosen = v->output_count;
        for (unsigned i = 0; i < v->output_count; ++i) {
            if (v->metadata[i].display &&
                (chosen == v->output_count || v->metadata[i].timestamp < v->metadata[chosen].timestamp))
                chosen = i;
        }
        if (chosen == v->output_count) return 0;
        const uint8_t *pixels = v->output[chosen].virtual_address;
        struct frame_info *meta = &v->metadata[chosen];
        const struct video_info *info = &v->info;
        unsigned stride = info->coded_width;
        if (meta->format > 1 || meta->timestamp > NATIVE_MAX_MS ||
            (meta->width && meta->width != info->width && meta->width != stride) ||
            (meta->height && meta->height != info->height && meta->height != info->coded_height)) {
            fprintf(stderr, "invalid_vde_frame format=%u size=%ux%u pts=%u\n",
                meta->format, meta->width, meta->height, meta->timestamp);
            return -1;
        }
        size_t y_size = stride * info->coded_height;
        unsigned chroma_stride = meta->format == 0 ? stride : stride / 2;
        unsigned chroma_step = meta->format == 0 ? 2 : 1;
        const uint8_t *uv = pixels + y_size +
            (info->y / 2) * chroma_stride + (info->x / 2)*chroma_step;
        struct video_image image = {
            pixels + info->y*stride + info->x, uv,
            uv + (meta->format == 0 ? 1 : y_size/4),
            stride, chroma_stride, chroma_step, info->width, info->height, meta->timestamp
        };
        int rc = sink(arg, &image);
        meta->display = 0;
        if (rc) return rc;
        ++*emitted;
    }
}

struct video_decoder *video_open(const uint8_t *bytes, unsigned size)
{
    if (!size || size > MAX_PACKET) return NULL;
    struct video_decoder *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->library = dlopen("vd_h264.so", RTLD_NOW | RTLD_LOCAL);
    if (!v->library) {
        fprintf(stderr, "vde_dlopen=%s\n", dlerror());
        goto fail;
    }
    const struct plugin *(*get_info)(void) = dlsym(v->library, "get_plugin_info");
    v->allocate = dlsym(v->library, "actal_malloc_uncache");
    v->release = dlsym(v->library, "actal_free_uncache");
    v->plugin = get_info ? get_info() : NULL;
    if (!v->plugin || strcmp(v->plugin->extension, "h264") || !v->allocate || !v->release)
        goto fail;
    uint32_t bus = 0;
    v->stream.virtual_address = v->allocate(MAX_PACKET + PACKET_PADDING + sizeof(struct packet_header), &bus);
    if (!v->stream.virtual_address || v->stream.virtual_address == (void *)-1 || !bus)
        goto fail;
    v->stream.physical_address = (uint8_t *)(uintptr_t)bus;
    fill_packet(v, bytes, size, 0);
    struct video_info *info = &v->info;
    if (v->plugin->probe(NULL, &v->stream, info) || info->unsupported ||
        !info->width || !info->height || info->coded_width > 1920 || info->coded_height > 1088 ||
        info->x > info->coded_width || info->y > info->coded_height ||
        info->width > info->coded_width - info->x || info->height > info->coded_height - info->y ||
        (info->coded_width | info->coded_height | info->x | info->y) & 1 ||
        info->extra_size > MAX_PACKET)
        goto fail;
    v->port = (struct frame_port){ get_buffer, get_buffer };
    v->output_size = info->coded_width*info->coded_height*3/2 + info->extra_size + 65536;
    v->handle = v->plugin->open(NULL, NULL, &v->port);
    if (!v->handle) goto fail;
    fprintf(stderr, "vde_format=%ux%u coded=%ux%u refs=%u buffer_bytes=%u\n",
        info->width, info->height, info->coded_width, info->coded_height, info->references, v->output_size);
    return v;
fail:
    video_close(v);
    return NULL;
}

int video_packet(struct video_decoder *v, const uint8_t *bytes, unsigned size, unsigned pts,
    video_sink sink, void *arg)
{
    if (size > MAX_PACKET) return -1;
    fill_packet(v, bytes, size, pts);
    for (unsigned retry = 0; retry < 16; ++retry) {
        uint64_t start = native_time_us();
        alarm(8);
        int rc = v->plugin->decode(v->handle, &v->stream);
        alarm(0);
        uint64_t elapsed = native_time_us() - start;
        ++v->decode_calls;
        v->decode_total_us += elapsed;
        if (elapsed > v->decode_max_us) v->decode_max_us = elapsed;
        if (rc != 0 && rc != 3 && !(rc == 1 && !bytes)) {
            fprintf(stderr, "vde_decode_error=%d pts=%u\n", rc, pts);
            return -1;
        }
        unsigned emitted = 0;
        if (emit_frames(v, sink, arg, &emitted)) return -1;
        if (rc == 3) continue; /* The plug-in has not consumed the current packet. */
        if (bytes || rc == 1 || !emitted) return 0;
    }
    fprintf(stderr, "vde_packet_retry_limit pts=%u\n", pts);
    return -1;
}

void video_close(struct video_decoder *v)
{
    if (!v) return;
    if (v->decode_calls)
        fprintf(stderr, "vde_decode_calls=%u avg_us=%llu max_us=%llu dma_frames=%u\n",
            v->decode_calls, (unsigned long long)(v->decode_total_us/v->decode_calls),
            (unsigned long long)v->decode_max_us, v->output_count);
    if (v->handle) {
        alarm(8);
        v->plugin->dispose(v->handle);
        alarm(0);
    }
    for (unsigned i = 0; i < v->output_count; ++i)
        v->release(v->output[i].virtual_address);
    if (v->stream.virtual_address && v->stream.virtual_address != (void *)-1)
        v->release(v->stream.virtual_address);
    if (v->library) dlclose(v->library);
    free(v);
}
