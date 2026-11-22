#ifndef GARLIC_CONFIRM_H
#define GARLIC_CONFIRM_H
#include <stdbool.h>
#include <stdint.h>
/* Either a second press within 350ms of release, or one 650ms hold. */
typedef struct {
    uint64_t down_at, released_at;
    bool down, fired, armed, blocked;
} garlic_confirm_key;
static inline bool garlic_confirm_update(garlic_confirm_key *s,
    bool down, bool eligible, uint64_t now)
{
    if (!eligible || (s->down && now < s->down_at) ||
        (s->armed && now < s->released_at)) {
        *s=(garlic_confirm_key){0};s->blocked=down;return false;
    }
    if(s->blocked){if(!down)s->blocked=false;return false;}
    if (down && !s->down) {
        bool second=s->armed && now-s->released_at<=350000;
        s->down=true;s->down_at=now;s->armed=false;s->fired=second;
        if(second)return true;
    } else if (!down && s->down) {
        s->down=false;
        s->armed=!s->fired && now-s->down_at<650000;
        s->released_at=now;s->fired=false;
    }
    if (s->down && !s->fired && now-s->down_at>=650000) {
        s->fired=true;s->armed=false;return true;
    }
    if (s->armed && now-s->released_at>350000)s->armed=false;
    return false;
}
#endif
