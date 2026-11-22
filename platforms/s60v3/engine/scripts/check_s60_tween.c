#include <assert.h>
#include <stdio.h>
#include "../source/cpymo/cpymo_tween.h"

int main(void)
{
    cpymo_tween a = cpymo_tween_create(0.120f);
    for (unsigned i = 1; i <= 3; ++i) {
        cpymo_tween_update(&a, 0.400f);
        const float value = cpymo_tween_progress(&a);
        assert(value > 0.0f && value < 1.0f);
        printf("120 ms effect after stall, intermediate step %u: %.2f\n", i, value);
    }
    cpymo_tween_update(&a, 0.400f);
    assert(cpymo_tween_finished(&a));

    cpymo_tween b = cpymo_tween_create(0.001f);
    cpymo_tween_update(&b, 0.016f);
    assert(cpymo_tween_finished(&b));
    puts("1 ms script cut finished on first update");

    cpymo_tween c = cpymo_tween_create(0.500f);
    const float step = cpymo_tween_visual_step(&c, 0.016f);
    assert(step > 0.015f && step < 0.017f);
    puts("Normal 16 ms frame timing retained");
    return 0;
}
