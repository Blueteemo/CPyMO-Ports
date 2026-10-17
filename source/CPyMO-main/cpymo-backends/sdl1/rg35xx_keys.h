#ifndef CPYMO_RG35XX_KEYS_H
#define CPYMO_RG35XX_KEYS_H
#include <stdint.h>

enum rg35xx_key_mask {
 RG_UP=1u<<0,RG_DOWN=1u<<1,RG_LEFT=1u<<2,RG_RIGHT=1u<<3,
 RG_A=1u<<4,RG_B=1u<<5,RG_Y=1u<<6,RG_X=1u<<7,
 RG_START=1u<<8,RG_SELECT=1u<<9,
 RG_L1=1u<<10,RG_R1=1u<<11,RG_L2=1u<<12,RG_R2=1u<<13,
 RG_MENU=1u<<16
};
/* Source: RG35XX Stock menu's gpio-keys-polled map; individual MOD needs hardware verification. */
uint32_t rg35xx_key_event(uint32_t held, unsigned code, int value);
void rg35xx_keys_initialize(void);
uint32_t rg35xx_keys_snapshot(void);
void rg35xx_keys_shutdown(void);
#endif
