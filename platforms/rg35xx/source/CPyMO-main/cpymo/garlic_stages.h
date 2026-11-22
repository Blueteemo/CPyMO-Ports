#ifndef GARLIC_STAGES_H
#define GARLIC_STAGES_H
#include <stdint.h>
#include <stdbool.h>
enum { GARLIC_STAGE_CONFIG,GARLIC_STAGE_PACKAGE,GARLIC_STAGE_FONT,
    GARLIC_STAGE_SCRIPT,GARLIC_STAGE_DECODE,GARLIC_STAGE_SCALE,
    GARLIC_STAGE_SCAN,GARLIC_STAGE_CLEANUP,GARLIC_STAGE_IMAGE_IO,GARLIC_STAGE_JPEG,GARLIC_STAGE_PNG,GARLIC_STAGE_IMAGE_OTHER,GARLIC_STAGE_COUNT };
#ifdef GARLIC_NO_FFMPEG
uint64_t garlic_stage_begin(void);
void garlic_stage_end(unsigned,uint64_t);
void garlic_stage_enable(bool);
void garlic_stage_reset(void);
void garlic_stage_report(const char *);
#else
static inline uint64_t garlic_stage_begin(void){return 0;}
static inline void garlic_stage_end(unsigned s,uint64_t t){(void)s;(void)t;}
#endif
#endif
