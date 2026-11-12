#pragma once
#include "cyc_host_extras.h"
#include <stdint.h>
int mm1_sprites_enabled(void);
int mm1_sprites_ready(void);
void mm1_sprites_build(void);
void mm1_sprites_latch(void);
int mm1_sprites_hud_margin(int x,int y,int tile,int attr);
void mm1_sprites_draw(uint32_t *out,int width,int origin,int camera,int hud_seat,int hud_origin,const uint8_t *opaque);
const CycHostExtras *mm1_sprites_extras(void);
