#pragma once
#include "cyc_host_extras.h"
#include "cyc_protoman_atlas.h"
int mm1_proto_available(void);
int mm1_proto_selected(void);
int mm1_proto_teleporting(void);
int mm1_proto_dispatch(void);
void mm1_proto_stage_enter(void);
void mm1_video_character_changed(void);
int mm1_proto_guarding(void);
int mm1_proto_pose_id(void);
int mm1_proto_right(void);
int mm1_proto_beam_offset(void);
const CycProtoAtlas *mm1_proto_atlas(void);
uint32_t mm1_proto_color(unsigned index);
const CycHostExtras *mm1_proto_extras(void);
