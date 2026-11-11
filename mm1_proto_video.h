#pragma once
#include <stdint.h>
void mm1_proto_video_reset(void);
void mm1_proto_video_debug(char *out,int cap);
void mm1_proto_draw_begin(void);
void mm1_proto_pose_begin(void);
void mm1_proto_part_emitted(int slot);
void mm1_proto_pose_done(void);
void mm1_proto_draw_latch(void);
int mm1_proto_native_picture(uint32_t *out);
int mm1_proto_owns_slot(int slot);
void mm1_proto_paint(uint32_t *out,int width,int x0,const uint8_t *coverage);
int mm1_video_state_get(uint8_t *out,int cap);
int mm1_video_state_valid(const uint8_t *in,int len);
int mm1_video_state_set(const uint8_t *in,int len);
