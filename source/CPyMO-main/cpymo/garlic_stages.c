#include "cpymo_prelude.h"
#ifdef GARLIC_NO_FFMPEG
#include "garlic_stages.h"
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
static bool enabled;
static struct {uint64_t total,max;unsigned count;} stages[GARLIC_STAGE_COUNT];
void garlic_stage_enable(bool e){enabled=e;}
uint64_t garlic_stage_begin(void)
{
    if(!enabled)return 0;
    struct timeval t;if(gettimeofday(&t,NULL))return 0;
    return (uint64_t)t.tv_sec*1000000+t.tv_usec;
}
void garlic_stage_end(unsigned s,uint64_t start)
{
    if(!enabled || !start || s>=GARLIC_STAGE_COUNT)return;
    uint64_t end=garlic_stage_begin(),elapsed=end>=start ? end-start : 0;
    stages[s].count++;stages[s].total+=elapsed;
    if(elapsed>stages[s].max)stages[s].max=elapsed;
}
void garlic_stage_reset(void){memset(stages,0,sizeof(stages));}
void garlic_stage_report(const char *event)
{
    if(!enabled)return;
    static const char *names[]={"config","package_index","font","script","image_read_decode","image_scale","scan_sort","cleanup","image_encoded_read","jpeg_decode","png_decode","other_image_decode"};
    extern void garlic_movie_log(const char *);
    char line[240];
    for(unsigned s=0;s<GARLIC_STAGE_COUNT;s++){
        snprintf(line,sizeof(line),"CPYMO_STAGE event=%s stage=%s count=%u total_us=%llu max_us=%llu",event,names[s],stages[s].count,
            (unsigned long long)stages[s].total,(unsigned long long)stages[s].max);
        garlic_movie_log(line);
    }
}
#endif
