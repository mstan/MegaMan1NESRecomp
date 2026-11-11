#pragma once
#include "cyc_host_extras.h"
#include <stdint.h>
int mm1_coop_available(void);
int mm1_coop_seat(void);
int mm1_coop_anchor(void);
int mm1_coop_present_begin(void);
void mm1_coop_present_end(int previous);
void mm1_coop_draw_begin(void);
void mm1_coop_frame_begin(void);
int mm1_coop_native_pool(void);
int mm1_coop_total(void);
int mm1_coop_inflight(void);
int mm1_coop_controls_paused(void);
void mm1_coop_record_hits(int owner,uint16_t before,uint16_t after);
int mm1_coop_character(void);
int mm1_coop_dispatch(void);
int mm1_coop_read(int seat,uint16_t address);
void mm1_coop_stage_enter(void);
int mm1_coop_drawing(void);
int mm1_coop_insert_slot(void);
void mm1_coop_primary_begin(void);
void mm1_coop_primary_body(void);
void mm1_coop_video_latch(void);
int mm1_coop_picture(void);
void mm1_coop_paint(uint32_t *out,int width,int x0,const uint8_t *coverage);
int mm1_world_prepare(void);
int mm1_world_probe(int x,int y);
const CycHostExtras *mm1_coop_extras(void);
