#pragma once
#include "cyc_host_extras.h"
#include <stdint.h>
int mm1_coop_available(void);
int mm1_coop_seat(void);
int mm1_coop_character(void);
int mm1_coop_dispatch(void);
int mm1_coop_read(int seat,uint16_t address);
void mm1_coop_stage_enter(void);
int mm1_coop_drawing(void);
void mm1_coop_video_latch(void);
int mm1_coop_picture(void);
void mm1_coop_paint(uint32_t *out,int width,int x0,const uint8_t *coverage);
int mm1_world_prepare(void);
int mm1_world_probe(int x,int y);
const CycHostExtras *mm1_coop_extras(void);
