/* Complete native art packets; pure captures leave the original OAM/AI intact. */
#include "mm1_sprites.h"
#include "mm1_coop.h"
#include "mm1_net.h"
#include "mm1_proto_video.h"
#include "mm1_weapon_gfx.h"
#include "cyc_host_extras.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "cyc_video.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum {PART_CAP=1024,OVERLAY_CAP=8};
typedef struct {int16_t x;uint8_t y,height,attr,actor,seat,edge,colors[4],pattern[32];} Part;
typedef struct {int16_t x,y;uint8_t width,height,actor,seat,behind;uint32_t pixels[64*64];} Overlay;
typedef struct {
    char magic[4];uint16_t count,native_parts[2];uint8_t valid,ready,stage,anchor,players,overlays;
    Part parts[PART_CAP],hud[2][64];uint8_t hud_count[2];Overlay overlay[OVERLAY_CAP];
} Frame;
static Frame B,F;
static int enabled,failed;
static FILE *report;
static uint8_t video_backup[8192],claimed[CYC_VIDEO_MAX_WIDTH*240];
static uint32_t picture[256*240];
int mm1_sprites_enabled(void){return enabled;}
int mm1_sprites_ready(void){return enabled&&F.valid&&F.stage==R(0x31);}
int mm1_sprites_hud_margin(int x,int y,int tile,int attr) {
    return y>=24&&y<=72&&((tile>=0xFA&&tile<=0xFE&&((x==24&&attr==1)||(x==40&&attr==2)))||(tile>=0xDB&&tile<=0xDF&&x==16&&attr==0));
}
static void read_part(Part *p,int slot,int actor,int seat,int camera,int personal) {
    int at=0x200+slot*4;memset(p,0,sizeof *p);
    p->x=(int16_t)(R(at+3)+(actor==32?0:camera));p->y=R(at);p->height=(R(0xFF)&32)?16:8;
    p->attr=R(at+2);p->actor=(uint8_t)actor;p->seat=(uint8_t)seat;
    p->edge=(uint8_t)(actor==32&&mm1_sprites_hud_margin(R(at+3),R(at),R(at+1),R(at+2)));
    unsigned tile=R(at+1),pattern=p->height==16?(tile&1)*0x1000+(tile&254)*16:((R(0xFF)&8)?0x1000:0)+tile*16;
    for(int i=0;i<p->height*2;i++)p->pattern[i]=personal?mm1_weapon_gfx_private((uint16_t)(pattern+i),cyc_render_chr((uint16_t)(pattern+i))):cyc_render_chr((uint16_t)(pattern+i));
    for(int i=0;i<4;i++)p->colors[i]=R(0x3E0+(p->attr&3)*4+i)&63;
}
static int capture_overlay(int actor,int seat,int camera) {
    if(actor!=0&&(actor<2||actor>4))return 0;
    memset(picture,0,sizeof picture);mm1_video_private_finish();mm1_video_private_latch(seat);
    if(!actor)mm1_video_private_dust(picture,256,0,NULL);
    int owned=0;for(int slot=1;slot<64;slot++)if(R(0x200+slot*4)<239)
        owned|=mm1_video_private_place(picture,256,0,NULL,slot);
    if(!owned)return 0;
    int left=256,right=-1,top=240,bottom=-1;
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)if(picture[y*256+x]>>24) {
        if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;
    }
    if(right<left)return 0;
    if(right-left>=64||bottom-top>=64||B.overlays==OVERLAY_CAP){failed=1;return 0;}
    Overlay *o=&B.overlay[B.overlays++];o->x=(int16_t)(camera+left);o->y=(int16_t)top;
    o->width=(uint8_t)(right-left+1);o->height=(uint8_t)(bottom-top+1);o->actor=(uint8_t)actor;o->seat=(uint8_t)seat;o->behind=R(0x13)&32;
    for(int y=0;y<o->height;y++)memcpy(o->pixels+y*64,picture+(top+y)*256+left,o->width*sizeof(uint32_t));return 1;
}
static void capture_actor(int actor,int seat,CycModRegs *r) {
    if(R(0x600+actor)>=248)return;
    int camera=R(0x460+actor)*256+R(0x480+actor)-128;
    W(0x1A,camera);W(0x1B,camera>>8);W(0x0D,0);W(0x0C,actor);
    for(int i=0;i<256;i+=4)W(0x200+i,248);
    mm1_proto_draw_begin();mm1_proto_pose_begin();
    if(!cyc_mod_call(0xD16E,r)){failed=1;return;}
    for(int slot=1;slot<64;slot++)if(R(0x200+slot*4)<239){mm1_proto_part_emitted(slot);B.native_parts[seat]++;}
    mm1_proto_pose_done();
    if(capture_overlay(actor,seat,camera))return;
    for(int slot=1;slot<64;slot++)if(R(0x200+slot*4)<239) {
        if(B.count==PART_CAP){failed=1;return;}
        read_part(&B.parts[B.count++],slot,actor,seat,camera,actor!=1&&actor<16);
    }
}
static void capture_hud(int seat,CycModRegs *r) {
    for(int i=0;i<256;i+=4)W(0x200+i,248);W(0x0D,0);
    if(!cyc_mod_call(0xD36E,r)){failed=1;return;}
    for(int slot=0;slot<64;slot++)if(R(0x200+slot*4)<239)
        read_part(&B.hud[seat][B.hud_count[seat]++],slot,32,seat,0,0);
}
void mm1_sprites_build(void) {
    memset(&B,0,sizeof B);B.ready=1;
    if(!enabled||cyc_mod_isolated()||R(0x31)>=10||R(0x54)!=32)return;
    CycModRegs r={0};cyc_mod_regs(&r);
    unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
    /* Menus and blocking native scripts retain their own rendering policy. */
    if(ret!=0x91C1)return;
    int saved=mm1_video_state_get(video_backup,sizeof video_backup);if(saved<0)return;
    int previous=mm1_coop_seat();B.stage=R(0x31);B.anchor=(uint8_t)previous;B.players=(uint8_t)(mm1_coop_view_active()?2:1);failed=0;
    if(!cyc_mod_isolate_begin())return;
    int ok=cyc_mod_call(0xC413,&r);
    for(int order=0;order<B.players&&ok&&!failed;order++) {
        int seat=B.anchor^order;
        if(mm1_coop_available()&&mm1_coop_art_project(seat)<0){failed=1;break;}
        W(0x3E1,mm1_coop_read(seat,0x3E1));W(0x3E2,mm1_coop_read(seat,0x3E2));
        capture_hud(seat,&r);
        for(int actor=0;actor<16&&!failed;actor++)if(actor!=1)capture_actor(actor,seat,&r);
    }
    if(mm1_coop_available())mm1_coop_art_project(B.anchor);
    for(int actor=1;actor<32&&ok&&!failed;actor++)if(actor==1||actor>=16)capture_actor(actor,B.anchor,&r);
    cyc_mod_isolate_end();mm1_coop_present_end(previous);
    if(!mm1_video_state_set(video_backup,saved))failed=1;
    B.valid=(uint8_t)(ok&&!failed);
}
void mm1_sprites_latch(void){if(B.ready){F=B;B.ready=0;}}
static void pixel(uint32_t *out,int width,int x,int y,int attr,uint32_t color,const uint8_t *opaque) {
    if(x<0||x>=width||y<0||y>=240||!(cyc_render_line_mask(y)&16))return;
    if(width==256&&x<8&&!(cyc_render_line_mask(y)&4))return;
    int at=y*width+x;if(claimed[at])return;claimed[at]=1;if(!(attr&32)||!opaque||!opaque[at])out[at]=color;
}
static void paint_part(const Part *p,uint32_t *out,int width,int offset,const uint8_t *opaque) {
    for(int y=0;y<p->height;y++)for(int x=0;x<8;x++) {
        int row=p->attr&128?p->height-1-y:y,bit=p->attr&64?x:7-x,at=(row/8)*16+(row&7);
        int v=((p->pattern[at]>>bit)&1)|(((p->pattern[at+8]>>bit)&1)<<1);
        if(v)pixel(out,width,offset+p->x+x,p->y+1+y,p->attr,cyc_render_color_value(p->colors[v]),opaque);
    }
}
void mm1_sprites_draw(uint32_t *out,int width,int origin,int camera,int hud_seat,int hud_origin,const uint8_t *opaque) {
    if(!mm1_sprites_ready()||width>CYC_VIDEO_MAX_WIDTH)return;memset(claimed,0,(size_t)width*240);
    int hud=hud_seat<0?F.anchor:hud_seat;
    for(int i=0;i<F.hud_count[hud];i++)paint_part(&F.hud[hud][i],out,width,F.hud[hud][i].edge?hud_origin:origin,opaque);
    for(int actor=0;actor<32;actor++)for(int order=0;order<(actor!=1&&actor<16?F.players:1);order++) {
        int seat=F.anchor^order;
        for(int i=0;i<F.overlays;i++) {
            const Overlay *o=&F.overlay[i];if(o->actor!=actor||o->seat!=seat)continue;
            for(int y=0;y<o->height;y++)for(int x=0;x<o->width;x++) {
                uint32_t c=o->pixels[y*64+x];if(c>>24)pixel(out,width,origin-camera+o->x+x,o->y+y,o->behind,c,opaque);
            }
        }
        for(int i=0;i<F.count;i++)if(F.parts[i].actor==actor&&F.parts[i].seat==seat)
            paint_part(&F.parts[i],out,width,origin-camera,opaque);
    }
}
static void set_enabled(int on){
    if(mm1_net_locked())return;
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on;extern void mm1_video_character_changed(void);mm1_video_character_changed();
}
static void reset(void){set_enabled(0);}
static void activate(void){set_enabled(1);}
static void power_on(void *ctx){(void)ctx;memset(&B,0,sizeof B);memset(&F,0,sizeof F);}
static void frame_end(void *ctx){(void)ctx;if(report)fprintf(report,"{\"frame\":%llu,\"on\":%d,\"ready\":%d,\"parts\":%u,\"overlays\":%u,\"native_parts\":[%u,%u],\"hud\":[%u,%u],\"anchor\":%u}\n",(unsigned long long)cyc_ring_frame,enabled,mm1_sprites_ready(),F.count,F.overlays,F.native_parts[0],F.native_parts[1],F.hud_count[0],F.hud_count[1],F.anchor);}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(!strcmp(name,"--flicker-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--no-flicker")||(strcmp(value,"on")&&strcmp(value,"off")))return false;
    set_enabled(!strcmp(value,"on"));return enabled==!strcmp(value,"on");
}
const CycHostExtras *mm1_sprites_extras(void) {
    static const CycHostOption options[]={{"--no-flicker",true,"on|off: draw complete native sprites"},{"--flicker-report",true,"FILE: sprite packet observations"}};
    static const CycHostExtras extras={.power_on=power_on,.frame_end=frame_end,.option=option,.options=options,.option_count=2};return &extras;
}
static int get(uint8_t *out,int cap){if(cap<sizeof B+sizeof F)return -1;memcpy(B.magic,"M1S1",4);memcpy(F.magic,"M1S1",4);memcpy(out,&B,sizeof B);memcpy(out+sizeof B,&F,sizeof F);return sizeof B+sizeof F;}
static int valid_part(const Part *p,int hud) {
    if(p->seat>1||p->y>=239||p->edge>1||(p->height!=8&&p->height!=16)||(hud?p->actor!=32:p->actor>=32))return 0;
    for(int i=0;i<4;i++)if(p->colors[i]>63)return 0;return 1;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof B+sizeof F)return 0;static Frame frames[2];memcpy(frames,in,sizeof frames);
    for(int f=0;f<2;f++) {
        const Frame *p=&frames[f];if(memcmp(p->magic,"M1S1",4)||p->count>PART_CAP||p->overlays>OVERLAY_CAP||p->valid>1||p->ready>1||p->anchor>1||p->players>2||p->hud_count[0]>64||p->hud_count[1]>64)return 0;
        if(p->valid&&(p->stage>=10||!p->players))return 0;
        for(int i=0;i<p->count;i++)if(!valid_part(&p->parts[i],0))return 0;
        for(int seat=0;seat<2;seat++)for(int i=0;i<p->hud_count[seat];i++)if(!valid_part(&p->hud[seat][i],1))return 0;
        for(int i=0;i<p->overlays;i++)if(p->overlay[i].actor>4||p->overlay[i].actor==1||p->overlay[i].seat>1||
            !p->overlay[i].width||!p->overlay[i].height||p->overlay[i].width>64||p->overlay[i].height>64||
            (p->overlay[i].behind&&p->overlay[i].behind!=32))return 0;
    }return 1;
}
static int put(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len){memcpy(&B,in,sizeof B);memcpy(&F,in+sizeof B,sizeof F);}else power_on(NULL);return 1;}
NES_MOD_CONSTRUCTOR(register_mm1_sprites) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mm1.no-flicker",activate);
    nes_mod_register_savestate_hook("mm1.sprites",get,put);nes_mod_register_savestate_validator("mm1.sprites",valid);
#endif
}
