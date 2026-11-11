#pragma once
#include <stdint.h>
void mm1_weapon_gfx_reset(void);
void mm1_weapon_gfx_begin(void);
void mm1_weapon_gfx_latch(void);
uint8_t mm1_weapon_gfx_private(uint16_t address,uint8_t fallback);
const uint8_t *mm1_weapon_menu_font(void);
int mm1_weapon_cycle_available(void);
