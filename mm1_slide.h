#pragma once
#include "cyc_host_extras.h"
typedef struct {int8_t x[2],y[2];uint8_t attr,pixels[16];} Mm1SlidePart;
typedef struct {uint8_t count;Mm1SlidePart parts[12];} Mm1SlidePose;
int mm1_slide_available(void);
int mm1_slide_active(void);
int mm1_slide_cancel_for_switch(void);
int mm1_slide_pose_index(void);
const Mm1SlidePose *mm1_slide_pose(int index);
const uint8_t *mm1_slide_palette(int index);
const uint8_t *mm1_slide_dust_tile(int age);
int mm1_slide_dust(int *world_x,int *y,int *right,int *age);
void mm1_slide_stage_enter(void);
void mm1_slide_restore_input(void);
void mm1_slide_after_run(void);
const CycHostExtras *mm1_slide_extras(void);
