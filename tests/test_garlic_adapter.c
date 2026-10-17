#include "../source/CPyMO-main/cpymo-backends/garlicos/garlic_adapter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_viewports(void)
{
    garlic_viewport v;
    assert(garlic_compute_viewport(320,240,640,480,&v));
    assert(v.x==0 && v.y==0 && v.w==640 && v.h==480);
    assert(garlic_compute_viewport(800,600,640,480,&v));
    assert(v.x==0 && v.y==0 && v.w==640 && v.h==480);
    assert(garlic_compute_viewport(540,360,640,480,&v));
    assert(v.x==0 && v.y==26 && v.w==640 && v.h==427);
    assert(garlic_compute_viewport(100,300,640,480,&v));
    assert(v.h==480 && v.w==160 && v.x==240);
    assert(!garlic_compute_viewport(0,300,640,480,&v));
    assert(!garlic_compute_viewport(320,240,640,480,NULL));
}

static void test_buttons(void)
{
    garlic_actions a=garlic_map_buttons(GARLIC_BUTTON_A|GARLIC_BUTTON_X, false);
    assert(a.ok && a.skip && !a.cancel);
    a=garlic_map_buttons(GARLIC_BUTTON_B|GARLIC_BUTTON_START, false);
    assert(a.cancel && !a.ok);
    a=garlic_map_buttons(GARLIC_BUTTON_SELECT, true);
    assert(a.up && !a.cancel);
    a=garlic_map_buttons(GARLIC_BUTTON_SELECT, false);
    assert(!a.up);
    a=garlic_map_buttons(GARLIC_BUTTON_R|GARLIC_BUTTON_Y, false);
    assert(a.hide_window);
    a=garlic_map_buttons(GARLIC_BUTTON_L|GARLIC_BUTTON_DOWN, false);
    assert(a.skip && a.down);
}

static void test_blit(void)
{
    /* Four 32-bit BGRA colors => 4 pixel RGB565, exact. */
    const uint8_t src[]={0,0,255,0, 0,255,0,0, 255,0,0,0, 255,255,255,0};
    uint16_t dst[4]={0};
    assert(garlic_blit_rgb565(dst,4,2,2,src,8,2,2));
    assert(dst[0] == 0xf800 && dst[1] == 0x07e0);
    assert(dst[2] == 0x001f && dst[3] == 0xffff);
    uint16_t boxed[4*4];
    for(int i=0;i<16;i++)boxed[i]=0xabcd;
    assert(garlic_blit_rgb565(boxed,4*sizeof(uint16_t),4,4,src,8,2,2));
    assert(boxed[0]==0xf800 && boxed[3]==0x07e0);
    assert(boxed[12]==0x001f && boxed[15]==0xffff);
    uint16_t letter[16*16];
    memset(letter,0xff,sizeof(letter));
    assert(garlic_blit_rgb565(letter,16*2,16,16,src,8,2,2));
    /* Offsets: protection of destination row padding. */
    uint8_t pitchbuf[24*2]; memset(pitchbuf,0x7d,sizeof(pitchbuf));
    assert(garlic_blit_rgb565((uint16_t*)pitchbuf,24,2,2,src,8,2,2));
    assert(pitchbuf[23]==0x7d && pitchbuf[4]==0x7d);
    assert(!garlic_blit_rgb565(dst,2,2,2,src,8,2,2));
    assert(!garlic_blit_rgb565(dst,4,2,2,src,4,2,2));
}

int main(void)
{
    test_viewports(); test_buttons(); test_blit();
    puts("PASS: viewport, physical keys, RGB565 and pitch safety");
    return 0;
}
