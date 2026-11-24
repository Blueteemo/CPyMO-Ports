#ifndef RG35XX_NATIVE_PLAYBACK_TIMING_H
#define RG35XX_NATIVE_PLAYBACK_TIMING_H

#include <stdint.h>
#include <time.h>

static inline uint64_t native_time_us(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec*1000000 + value.tv_nsec/1000;
}

#endif
