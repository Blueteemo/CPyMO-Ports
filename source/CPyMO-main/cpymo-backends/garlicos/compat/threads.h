#ifndef CPYMO_GARLIC_COMPAT_THREADS_H
#define CPYMO_GARLIC_COMPAT_THREADS_H

/* Minimal C11 <threads.h> for the MiyooCFW uClibc sysroot.
 *
 * The SDK ships pthreads but NOT C11 `threads.h`, while the Libretro core uses
 * the C11 mutex API for the audio callback (libretro.c includes <threads.h>
 * whenever the audio/video code is enabled). This header provides exactly the
 * members that core uses, over pthreads.
 *
 * Put this directory on the include path (tools/build_arm_audio.sh does) --
 * the product sources are unchanged, and this file is never used on a host
 * that already has a real <threads.h>... except by the shim user itself, so it
 * defines only the usual macros and is safe to include twice.
 */

#include <pthread.h>

typedef pthread_mutex_t mtx_t;
typedef pthread_once_t once_flag;

enum {
    mtx_plain = 0,
    mtx_recursive = 1,
    mtx_timed = 2
};

static inline int mtx_init(mtx_t *m, int type)
{
    if (type == mtx_recursive) {
        pthread_mutexattr_t a;
        if (pthread_mutexattr_init(&a) != 0) return -1;
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        int r = pthread_mutex_init(m, &a);
        pthread_mutexattr_destroy(&a);
        return r;
    }
    return pthread_mutex_init(m, 0);
}

static inline int mtx_lock(mtx_t *m) { return pthread_mutex_lock(m); }
static inline int mtx_unlock(mtx_t *m) { return pthread_mutex_unlock(m); }
static inline void mtx_destroy(mtx_t *m) { pthread_mutex_destroy(m); }

#endif
