#ifndef CPYMO_GARLIC_COMPAT_FFMPEG_H
#define CPYMO_GARLIC_COMPAT_FFMPEG_H

/* Two small compatibility fixes for the FFmpeg that the MiyooCFW uClibc SDK
 * ships (3.4 / libavformat 57.40), included ahead of the FFmpeg headers:
 *
 *  1. `avio_context_free()` arrived in FFmpeg 4.0. The CPyMO call sites free
 *     the caller-owned buffer themselves right after, so the shim frees only
 *     the AVIOContext, which is the contract of the newer call.
 *
 *      sdl2/host builds keep their own real avio_context_free: the macro is
 *      only defined when the header really lacks it.
 *
 *  2. FFmpeg 3.4 deprecates some names the core still uses; suppressing the
 *     deprecation warnings keeps this build's -Werror-ready output clean without
 *     touching the call sites.
 *
 * Include this with `-include` (see tools/build_arm_audio.sh) or first in a
 * translation unit; it pulls in the version macros it needs and is idempotent.
 */

#include <libavformat/version.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

/* --- 1. avio_context_free on libavformat < 58 ---------------------------- */
#if LIBAVFORMAT_VERSION_MAJOR < 58
#ifndef avio_context_free
static inline void cpymo_compat_avio_context_free(AVIOContext **p)
{
    if (p && *p) {
        av_free(*p);
        *p = NULL;
    }
}
#define avio_context_free cpymo_compat_avio_context_free
#endif
#endif

/* --- 2. the 3.4 -> 4.x renames the core still spells the old way -------- */
#if LIBAVFORMAT_VERSION_MAJOR < 58
#define CPYMO_COMPAT_FFMPEG_34 1
#endif

#endif
