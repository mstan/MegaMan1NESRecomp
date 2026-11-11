/* Replace the native body's pixels without changing its animation or CHR. */
#include "mm1_proto_video.h"
#include "mm1_proto.h"
#include "mm1_reflect.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <string.h>
#include <stdio.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))

typedef struct {
    uint64_t mask;
    int16_t x,y;
    uint8_t pose,right,behind,weapon,replace,proto,teleport,pad;
    uint8_t oam[256];
} BodyFrame;
typedef struct {BodyFrame building,shown;uint8_t restoring,invulnerability;} Presentation;
static Presentation video;
static int valid_picture(void);
void mm1_proto_video_reset(void){memset(&video,0,sizeof video);}
void mm1_proto_video_debug(char *out,int cap) {
    BodyFrame *f=&video.shown;
    snprintf(out,(size_t)cap,"\"pose\":%u,\"mask\":%llu,\"anchor\":[%d,%d],\"picture\":%d",
        f->pose,(unsigned long long)f->mask,f->x,f->y,valid_picture());
}

static void restore_invulnerability(void) {
    if(video.restoring){W(0x55,video.invulnerability);video.restoring=0;}
}
void mm1_proto_draw_begin(void) {
    restore_invulnerability();memset(&video.building,0,sizeof video.building);
}
void mm1_proto_pose_begin(void) {
    if(!mm1_proto_available()||R(0x0C)||R(0x31)>=10||R(0x54)!=32)return;
    int pose=mm1_proto_pose_id();BodyFrame *f=&video.building;
    f->replace=(uint8_t)((mm1_proto_selected()||mm1_proto_teleporting())&&pose>=0);
    /* Native hurt animation also emits separate sparks. Leave those alone. */
    if(R(0x400)+(R(0x640)>>4)==0x12) {
        int type=0x12,ptr=R(0x8186+type*2)|(R(0x8187+type*2)<<8);
        if(R(ptr+(R(0x440)>>4)+1)==0x19)f->replace=0;
    }
    f->pose=(uint8_t)(pose<0?0:pose);
    f->x=(uint8_t)(R(0x480)-R(0x1A));
    f->y=(int16_t)(R(0x600)+1+mm1_proto_beam_offset());
    f->right=(uint8_t)mm1_proto_right();f->weapon=(uint8_t)mm1_weapon_identity();
    f->proto=(uint8_t)mm1_proto_selected();f->teleport=(uint8_t)mm1_proto_teleporting();
    /* The simulation keeps its native damage guard. Only this drawing call
     * gets visible parts so the transition has an actual OAM ownership record. */
    if(f->teleport&&f->replace){video.invulnerability=R(0x55);video.restoring=1;W(0x55,0);}
}
void mm1_proto_part_emitted(int slot) {
    if(R(0x0C)||!video.building.replace||slot<0||slot>=64)return;
    BodyFrame *f=&video.building;f->mask|=1ull<<slot;
    f->behind=(uint8_t)((R(0x200+slot*4+2)&32)!=0);
}
void mm1_proto_pose_done(void) {
    if(R(0x0C))return;
    for(int slot=0;slot<64;slot++)if(video.building.mask&(1ull<<slot))
        for(int k=0;k<4;k++)video.building.oam[slot*4+k]=R(0x200+slot*4+k);
    restore_invulnerability();
}
void mm1_proto_draw_latch(void) {
    restore_invulnerability();video.shown=video.building;
    for(int slot=0;slot<64;slot++)if(video.shown.mask&(1ull<<slot))
        for(int k=0;k<4;k++)if(video.shown.oam[slot*4+k]!=R(0x200+slot*4+k)){
            video.shown.mask=0;return;
        }
}
static int valid_picture(void) {
    BodyFrame *f=&video.shown;
    if(!mm1_proto_available()||!mm1_proto_atlas()||!f->replace||!f->mask)return 0;
    const uint8_t *oam=cyc_render_oam();
    for(int slot=0;slot<64;slot++)if(f->mask&(1ull<<slot))
        if(memcmp(f->oam+slot*4,oam+slot*4,4))return 0;
    return 1;
}
int mm1_proto_owns_slot(int slot) {
    return slot>=0&&slot<64&&valid_picture()&&(video.shown.mask&(1ull<<slot))!=0;
}
static uint32_t color(unsigned index,int weapon,int proto) {
    const CycProtoAtlas *atlas=mm1_proto_atlas();
    uint32_t c=atlas->colors[index-1];
    if(weapon||!proto) {
        unsigned r=c>>16&255,g=c>>8&255,b=c&255;
        if(r==g&&g==b&&r>32&&r<240)c=cyc_render_color(17);
        else if(r>64&&r>g+b)c=cyc_render_color(18);
    }
    return c;
}
void mm1_proto_paint(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    if(!valid_picture())return;
    BodyFrame *f=&video.shown;const CycProtoPose *p=&mm1_proto_atlas()->poses[f->pose];
    for(int sy=0;sy<p->height;sy++)for(int sx=0;sx<p->width;sx++) {
        unsigned index=p->pixels[sy*32+sx];if(!index)continue;
        int nx=f->x+(f->right?p->origin_x-sx-1:sx-p->origin_x);
        int x=nx+x0,y=f->y+p->top_y+sy;
        if(x<0||x>=width||y<0||y>=240)continue;
        if(!(cyc_render_line_mask(y)&16)||(nx>=0&&nx<8&&!(cyc_render_line_mask(y)&4)))continue;
        if(f->behind&&coverage&&coverage[y*width+x])continue;
        out[y*width+x]=color(index,f->weapon,f->proto&&!f->teleport);
    }
}
typedef struct {uint32_t *out;int anchor;} Paint;
static int place(int slot,int x,int y,int *out_x,void *context) {
    (void)y;Paint *p=(Paint*)context;*out_x=x;
    if(!(video.shown.mask&(1ull<<slot)))return 1;
    if(slot==p->anchor)mm1_proto_paint(p->out,256,0,cyc_frame_bg_opaque());
    return 0;
}
int mm1_proto_native_picture(uint32_t *out) {
    if(!valid_picture()||!cyc_render_background(out))return 0;
    int first=64;for(int i=0;i<64;i++)if(video.shown.mask&(1ull<<i)){first=i;break;}
    Paint p={out,first};cyc_render_sprites(out,256,240,0,cyc_frame_bg_opaque(),place,&p);return 1;
}
int mm1_video_state_get(uint8_t *out,int cap) {
    if(cap<(int)sizeof video)return -1;memcpy(out,&video,sizeof video);return sizeof video;
}
int mm1_video_state_valid(const uint8_t *in,int len) {
    if(!len)return !mm1_proto_available();
    if(!in||len!=sizeof video)return 0;Presentation v;memcpy(&v,in,sizeof v);
    return v.restoring<=1&&v.building.pose<CYC_PROTO_POSE_COUNT&&v.shown.pose<CYC_PROTO_POSE_COUNT&&
        v.building.right<=1&&v.shown.right<=1&&v.building.replace<=1&&v.shown.replace<=1;
}
int mm1_video_state_set(const uint8_t *in,int len) {
    if(!mm1_video_state_valid(in,len))return 0;
    if(len)memcpy(&video,in,sizeof video);else memset(&video,0,sizeof video);return 1;
}
NES_MOD_CONSTRUCTOR(register_mm1_proto_video) {
    nes_mod_register_savestate_hook("mm1.protoman.video",mm1_video_state_get,mm1_video_state_set);
    nes_mod_register_savestate_validator("mm1.protoman.video",mm1_video_state_valid);
}
