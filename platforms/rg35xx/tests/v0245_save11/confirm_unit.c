#include "../../source/CPyMO-main/cpymo-backends/garlicos/garlic_confirm.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 garlic_confirm_key s={0};
 assert(!garlic_confirm_update(&s,1,1,0));assert(!garlic_confirm_update(&s,0,1,100000));assert(garlic_confirm_update(&s,1,1,300000));assert(!garlic_confirm_update(&s,1,1,1000000));assert(!garlic_confirm_update(&s,0,1,1100000));assert(!garlic_confirm_update(&s,1,1,1200000));assert(garlic_confirm_update(&s,1,1,1850000));
 assert(!garlic_confirm_update(&s,1,0,1900000));assert(!garlic_confirm_update(&s,1,1,3000000));assert(!garlic_confirm_update(&s,0,1,3100000));assert(!garlic_confirm_update(&s,1,1,3200000));assert(!garlic_confirm_update(&s,0,1,3250000));assert(!garlic_confirm_update(&s,1,1,3650001));assert(garlic_confirm_update(&s,1,1,4300001));
 assert(!garlic_confirm_update(&s,1,1,1));assert(!garlic_confirm_update(&s,1,1,10000000));assert(!garlic_confirm_update(&s,0,1,10000001));
 puts("CONFIRM single/double/hold/debounce/context-latch/backward-clock PASS");
}
