#include "client.h"
#include "present.h"
#include "cpymo_engine.h"
#include "cpymo_ui.h"
#include "garlic_movie.h"
#include "garlic_audio_sync.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

struct client {
    struct native_playback *shared;
    struct native_presenter present;
    pid_t pid;
    int child_status;
    char shared_path[PATH_MAX];
    float stuck_time, closing_time;
    unsigned progress, bgm_gain;
    bool closing, finished, failed;
};

static struct client *active;
static pid_t retiring;

static void reap_retired(void)
{
    if (retiring <= 0) return;
    pid_t pid = waitpid(retiring, NULL, WNOHANG);
    if (pid == retiring || (pid < 0 && errno == ECHILD)) retiring = 0;
}

static void close_client(struct client *c)
{
    if (!c) return;
    garlic_audio_lock();
    if (active == c) active = NULL;
    if (c->shared != MAP_FAILED) native_store(&c->shared->stop, 1);
    garlic_audio_unlock();
    if (c->pid > 0) retiring = c->pid;
    if (c->shared != MAP_FAILED) munmap(c->shared, sizeof(*c->shared));
    if (c->shared_path[0]) unlink(c->shared_path);
    free(c);
}

static void destroy_movie(cpymo_engine *e, void *data)
{
    (void)e;
    struct client *c = *(struct client **)data;
    unsigned frames = c->present.frames;
    unsigned dropped = c->present.dropped;
    unsigned late = c->present.max_late_ms;
    garlic_audio_lock();
    unsigned underrun = c->present.underrun_frames;
    garlic_audio_unlock();
    char message[192];
    snprintf(message, sizeof(message), "MOVIE end native=1 shown=%u dropped=%u late_ms=%u "
        "audio_underrun_frames=%u completed=%d failed=%d",
        frames, dropped, late, underrun, c->finished, c->failed);
    garlic_movie_log(message);
    close_client(c);
}

static error_t update_movie(cpymo_engine *e, void *data, float elapsed)
{
    struct client *c = *(struct client **)data;
    if (c->pid > 0) {
        pid_t pid = waitpid(c->pid, &c->child_status, WNOHANG);
        if (pid == c->pid) c->pid = 0;
        if (pid < 0 && errno == ECHILD) {
            c->pid = 0;
            c->failed = true;
        }
    }
    if (!c->closing && (CPYMO_INPUT_JUST_RELEASED(e, ok) || CPYMO_INPUT_JUST_RELEASED(e, cancel))) {
        c->closing = true;
        native_store(&c->shared->stop, 1);
    }
    unsigned state = native_load(&c->shared->state);
    unsigned write = native_load(&c->shared->video_write);
    if (!c->closing && write != c->progress) {
        c->progress = write;
        c->stuck_time = 0;
    }
    c->stuck_time += elapsed;
    if (!c->closing && (state == NATIVE_FAILED || c->stuck_time > 12.0f ||
        (!c->pid && state != NATIVE_FINISHED))) {
        c->failed = true;
        c->closing = true;
        native_store(&c->shared->stop, 1);
    }
    if (!c->closing && state == NATIVE_FINISHED && !c->pid &&
        native_load(&c->shared->video_read) == write &&
        native_load(&c->shared->audio_read) == native_load(&c->shared->audio_write)) {
        c->finished = !c->child_status;
        c->failed = !c->finished;
        c->closing = true;
    }
    if (c->closing) {
        c->closing_time += elapsed;
        if (!c->pid || c->closing_time > 6.0f)
            cpymo_ui_exit(e);
    }
    return CPYMO_ERR_SUCC;
}

enum { NATIVE_BGM_GAIN_ONE = 4096 };

static unsigned native_movie_bgm_gain(const cpymo_engine *e)
{
    float volume = cpymo_audio_get_channel_volume(CPYMO_AUDIO_CHANNEL_BGM, &e->audio);
    if (!(volume > 0.0f)) return 0;
    if (volume >= 1.0f) return NATIVE_BGM_GAIN_ONE;
    return (unsigned)(volume * NATIVE_BGM_GAIN_ONE);
}

error_t garlic_play_native_movie(cpymo_engine *e, const char *path)
{
    const char *script = getenv("CPYMO_NATIVE_VIDEO_SCRIPT");
    const char *runtime = getenv("CPYMO_NATIVE_RUNTIME");
    if (!script || !runtime || access(script, R_OK)) return CPYMO_ERR_UNSUPPORTED;
    reap_retired();
    if (active || retiring) return CPYMO_ERR_UNSUPPORTED;
    struct client *c = calloc(1, sizeof(*c));
    if (!c) return CPYMO_ERR_OUT_OF_MEM;
    c->shared = MAP_FAILED;
    /* The movie owns the UI until exit. Snapshot its BGM level before
       publishing the client to the frontend audio callback. */
    c->bgm_gain = native_movie_bgm_gain(e);
    char name[PATH_MAX];
    int len = snprintf(name, sizeof(name), "%s/native-video.shm", runtime);
    if (len < 0 || len >= (int)sizeof(name)) goto fail;
    int fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) goto fail;
    memcpy(c->shared_path, name, len+1);
    if (!ftruncate(fd, sizeof(*c->shared)))
        c->shared = mmap(NULL, sizeof(*c->shared), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (c->shared == MAP_FAILED) goto fail;
    c->shared->magic = NATIVE_MAGIC;
    c->shared->version = NATIVE_VERSION;
    char *const args[] = {"sh", (char *)script, c->shared_path, (char *)path, NULL};
    c->pid = fork();
    if (!c->pid) {
        execv("/bin/sh", args);
        _exit(127);
    }
    if (c->pid < 0) goto fail;
    struct client **ui = NULL;
    error_t error = cpymo_ui_enter((void **)&ui, e, sizeof(*ui), update_movie,
        cpymo_ui_empty_drawer, destroy_movie);
    if (error != CPYMO_ERR_SUCC) goto fail;
    *ui = c;
    cpymo_audio_bgm_stop(e);
    cpymo_audio_se_stop(e);
    cpymo_audio_vo_stop(e);
    garlic_audio_lock();
    active = c;
    garlic_audio_unlock();
    garlic_movie_log("MOVIE start native=1");
    return CPYMO_ERR_SUCC;
fail:
    close_client(c);
    return CPYMO_ERR_CAN_NOT_OPEN_FILE;
}

bool garlic_native_movie_audio(int16_t *buffer, size_t frames)
{
    garlic_audio_lock();
    struct client *c = active;
    if (c) {
        if (native_load(&c->shared->stop))
            memset(buffer, 0, frames*4);
        else {
            native_present_audio(c->shared, &c->present, buffer, frames);
            /* Drain PCM and advance the video clock even at zero gain. */
            if (c->bgm_gain < NATIVE_BGM_GAIN_ONE)
                for (size_t i = 0; i < frames * 2; ++i)
                    buffer[i] = (int16_t)((int32_t)buffer[i] *
                        (int32_t)c->bgm_gain / NATIVE_BGM_GAIN_ONE);
        }
    }
    garlic_audio_unlock();
    return c != NULL;
}

bool garlic_native_movie_video(uint16_t *pixels, float elapsed)
{
    reap_retired();
    if (!active) return false;
    if (!active->present.frames)
        memset(pixels, 0, NATIVE_WIDTH*NATIVE_HEIGHT*2);
    native_present_frame(active->shared, &active->present, pixels,
        (unsigned)(elapsed*NATIVE_AUDIO_RATE+0.5f));
    return true;
}
