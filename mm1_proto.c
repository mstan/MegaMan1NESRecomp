/* Character simulation follows MM1's own player state and damage path. */
#include "mm1_proto.h"
#include "mm1_proto_sheet.h"
#include "mm1_proto_video.h"
#include "mm1_reflect.h"
#include "mm1_slide.h"
#include "mm1_coop.h"
#include "mm1_net.h"
#ifdef NESRECOMP_NET
#include "nes_netplay_identity.h"
#endif
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "cyc_tcp.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>

#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
static CycProtoAtlas atlas;
static int loaded,enabled,requested,coop_required,real_mode=1,start_proto=1;
static FILE *report;
typedef struct {
    char magic[4];
    uint8_t on,mode,selected,teleport,previous,pending,invulnerability,dispatched;
    uint8_t health[2],health_ready,health_owner;
    uint64_t swaps,damage_hits,shot_limit;
} ProtoState;
static ProtoState states[2];
#define state (states[mm1_coop_seat()])
/* Renderer and dispatcher own their current page; immutable atlas stays here. */
void mm1_video_character_changed(void);
void mm1_controls_refresh(void);
int mm1_controls_safe(void);
int mm1_controls_gameplay(void);

int mm1_proto_available(void){return enabled;}
void mm1_proto_settings(int *wanted,int *mode,int *start){*wanted=requested;*mode=real_mode;*start=start_proto;}
int mm1_proto_source_hash(uint8_t out[32]) {
#ifdef NESRECOMP_NET
    uint8_t both[64];nes_sha256(MM1_PROTO_PNG,sizeof MM1_PROTO_PNG,both);nes_sha256(MM1_PROTO_JUMP_PNG,sizeof MM1_PROTO_JUMP_PNG,both+32);nes_sha256(both,sizeof both,out);return 1;
#else
    (void)out;return 0;
#endif
}
int mm1_proto_selected(void){return enabled&&(mm1_coop_available()?mm1_coop_character():state.selected);}
int mm1_proto_teleporting(void){return enabled&&state.teleport;}
int mm1_proto_real_traits(void){return mm1_proto_selected()&&real_mode;}
const CycProtoAtlas *mm1_proto_atlas(void){return loaded?&atlas:NULL;}
static int playable(void) {
    return mm1_controls_safe();
}
/* The native HP register belongs to the visible solo character. Co-op keeps
 * its existing per-player ownership and never uses these reserve banks. */
static void health_record(void) {
    if(cyc_mod_isolated() || mm1_coop_available() || !state.health_ready || R(0x31)>=10)return;
    int hp=R(0x6A);if(hp<=28)state.health[state.health_owner]=(uint8_t)hp;
}
static void health_sync(void) {
    if(cyc_mod_isolated())return;
    if(mm1_coop_available()){for(int i=0;i<2;i++){states[i].health_ready=0;states[i].health_owner=(uint8_t)mm1_coop_character_for(i);}return;}
    if((!enabled&&!state.health_ready)||!mm1_controls_gameplay())return;
    int owner=enabled?state.selected:0;
    if(!state.health_ready) {
        state.health[0]=state.health[1]=28;state.health_ready=1;
    }
    health_record();
    if(state.health_owner!=owner) {
        state.health_owner=(uint8_t)owner;W(0x6A,state.health[owner]);
    }
}
static int load_art(void) {
    if(loaded)return 1;
    char why[160];
    loaded=cyc_protoman_atlas_load(&atlas,MM1_PROTO_PNG,sizeof MM1_PROTO_PNG,
                                  MM1_PROTO_JUMP_PNG,sizeof MM1_PROTO_JUMP_PNG,why,sizeof why);
    if(!loaded)fprintf(stderr,"Proto Man: %s\n",why);
    return loaded;
}
static void apply_enabled(void) {
    if(mm1_net_locked())return;
    int on=requested||coop_required;
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on&&load_art();
    if(!enabled){if(state.teleport)W(0x55,state.invulnerability);state.teleport=state.pending=0;}
    nes_mod_set_function_hook_enabled("mm1.proto.player",enabled);
    nes_mod_set_function_hook_enabled("mm1.proto.damage",enabled);
    nes_mod_set_function_hook_enabled("mm1.proto.shots",enabled);
    nes_mod_set_function_hook_enabled("mm1.proto.muzzle",enabled);
    mm1_reflect_configure(enabled&&real_mode);
    mm1_controls_refresh();mm1_video_character_changed();
}
static void set_enabled(int on){requested=on!=0;apply_enabled();}
void mm1_proto_coop_enable(int on){if(mm1_net_locked())return;coop_required=on!=0;apply_enabled();}
int mm1_proto_configure(int wanted,int mode,int start) {
    if(mm1_net_locked()||wanted<0||wanted>1||mode<0||mode>1||start<0||start>1)return 0;
    real_mode=mode;start_proto=start;set_enabled(wanted);return enabled==(requested||coop_required);
}
void mm1_proto_stage_enter(void) {
    for(int i=0;i<2;i++) {
        states[i].teleport=states[i].pending=states[i].previous=states[i].health_ready=0;
        states[i].health_owner=(uint8_t)(enabled?states[i].selected:0);
    }
    mm1_reflect_stage_enter();
}
static void sound(uint8_t track) {
    int n=R(0x45);if(n<16){W(0x580+n,track);W(0x45,n+1);}
}
int mm1_proto_dispatch(void) {
    if(!enabled||mm1_coop_available())return 0;
    health_sync();
    unsigned fresh=state.pending;state.pending=0;
    if(fresh && !state.teleport && playable() && mm1_slide_cancel_for_switch()) {
        state.teleport=1;state.invulnerability=R(0x55);state.swaps++;
        W(0x640,0);sound(0x20);mm1_video_character_changed();
    }
    if(!state.teleport)return 0;
    state.dispatched=1;W(0x55,2);
    return 1;
}
static int player(uint16_t address){(void)address;return mm1_proto_teleporting()!=0||mm1_coop_controller_blocked();}
static int damage(uint16_t address) {
    (void)address;
    if(state.teleport||mm1_coop_seat_blocked(mm1_coop_seat()))return 1;
    if(mm1_proto_selected()&&real_mode) {
        int amount=R(0x0C);W(0x0C,amount>127?255:amount*2);state.damage_hits++;
    }
    return 0;
}
static int shots(uint16_t address) {
    (void)address;
    if(!mm1_proto_selected() || !real_mode)return 0;
    int count=0;for(int i=2;i<5;i++)count+=R(0x600+i)<248&&R(0x400+i)==0x1A;
    if(count<2)return 0;
    CycModRegs regs;cyc_mod_regs(&regs);regs.x=R(0x2F);cyc_mod_set_regs(&regs);
    state.shot_limit++;return 1;
}
int mm1_proto_muzzle_y(int native_offset) {
    int offset=native_offset;
    if(mm1_proto_selected()&&!mm1_weapon_identity()) {
        int type=R(0x400);
        offset=(type==9||type==10||type==0x6F)?3:
            (type>=21&&type<=24)||type==0x70?-4:3;
        if(type==6||type==7||type==12) {
            static const uint8_t phases[]={0,1,2,1};
            offset=phases[(R(0x440)>>4)&3]==1?2:3;
        }
    }
    return (int)R(0x600)+offset;
}
static int muzzle(uint16_t address) {
    (void)address;CycModRegs r;cyc_mod_regs(&r);
    if(mm1_proto_selected()&&!mm1_weapon_identity()&&r.x>=2&&r.x<=4) {
        int y=mm1_proto_muzzle_y(0);r.a=(uint8_t)(y>=0&&y<240?y:248);cyc_mod_set_regs(&r);
    }
    return 0;
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;
    if(!enabled||mm1_coop_available())return;
    unsigned select=(nes_input_seat(1)&0x20)!=0;
    if(!playable()||state.teleport||(buttons[0]&0x10))state.pending=0;
    else state.pending|=(uint8_t)(select&&!state.previous);
    state.previous=(uint8_t)select;
    buttons[0]&=(uint8_t)~0x20; /* Preserve MM1 Select pause when this package is off. */
    if(state.teleport)buttons[0]=0;
}
static void begin(void *ctx){(void)ctx;state.dispatched=0;health_sync();}
static void end(void *ctx) {
    (void)ctx;if(!playable())state.pending=0;
    mm1_reflect_frame_end();
    if(enabled&&state.teleport&&state.dispatched) {
        if(state.teleport==16){state.selected^=1;health_sync();sound(0x20);mm1_video_character_changed();}
        if(++state.teleport>32) {
            state.teleport=0;W(0x55,state.invulnerability);mm1_video_character_changed();
        }
    }
    health_record();
    if(report) {
        char fields[256],reflection[256];mm1_proto_video_debug(fields,sizeof fields);
        mm1_reflect_debug(reflection,sizeof reflection);
        unsigned shots=0;for(int i=2;i<5;i++)if(R(0x600+i)<248&&R(0x400+i)==0x1A)shots|=1u<<(i-2);
        fprintf(report,"{\"frame\":%llu,\"selected\":%u,\"teleport\":%u,\"swaps\":%llu,\"damage_hits\":%llu,\"limited\":%llu,\"hp\":%u,\"health\":[%u,%u],\"health_owner\":%u,\"native_pause\":%u,\"shots\":%u,%s,%s}\n",
            (unsigned long long)cyc_ring_frame,state.selected,state.teleport,(unsigned long long)state.swaps,
            (unsigned long long)state.damage_hits,(unsigned long long)state.shot_limit,R(0x6A),state.health[0],state.health[1],state.health_owner,R(0x41),shots,fields,reflection);
    }
}
static void power_on(void *ctx) {
    (void)ctx;memset(states,0,sizeof states);states[0].selected=(uint8_t)start_proto;states[1].selected=(uint8_t)!start_proto;
    for(int i=0;i<2;i++)states[i].health_owner=(uint8_t)(enabled?states[i].selected:0);
    mm1_proto_video_reset();
    mm1_reflect_reset();
}
int mm1_proto_guarding(void) {
    return mm1_proto_selected()&&real_mode&&!state.teleport&&R(0x400)==9&&!R(0x640)&&!(R(0x420)&31);
}
int mm1_proto_pose_id(void) {
    if(state.teleport)return CYC_PROTO_TELEPORT;
    if(mm1_slide_active())return CYC_PROTO_SLIDE;
    int type=R(0x400)+(R(0x640)>>4),frame=R(0x440)>>4;
    static const int run[]={0,1,2,1};
    switch(type) {
    case 0:return CYC_PROTO_IDLE0+((R(0x23)>>3)&3);
    case 1:return CYC_PROTO_FIRE_IDLE0+((R(0x23)>>3)&1);
    case 3:return CYC_PROTO_STEP0+((R(0x23)>>3)&1);
    case 4:return CYC_PROTO_FIRE_IDLE0;
    case 6:case 12:return CYC_PROTO_RUN0+run[frame&3];
    case 7:return CYC_PROTO_FIRE_RUN0+run[frame&3];
    case 9:return mm1_proto_guarding()?CYC_PROTO_JUMP_GUARD:CYC_PROTO_JUMP_OPEN;
    case 10:return CYC_PROTO_JUMP_FIRE;
    case 15:return CYC_PROTO_IDLE0;
    case 18:case 19:return CYC_PROTO_HURT;
    case 20:return frame?CYC_PROTO_TELEPORT_FULL:CYC_PROTO_TELEPORT;
    case 21:return CYC_PROTO_LADDER0+(frame&1);
    case 22:case 24:return CYC_PROTO_LADDER_FIRE0+(frame&1);
    case 23:return CYC_PROTO_LADDER_TOP0;
    case 0x6E:return CYC_PROTO_THROW_IDLE;
    case 0x6F:return CYC_PROTO_THROW_JUMP;
    case 0x70:return CYC_PROTO_LADDER_FIRE1;
    default:return -1;
    }
}
int mm1_proto_right(void) {
    if(R(0x400)==21 && !R(0x640))return (R(0x440)>>4)&1;
    return (R(0x420)&64)!=0;
}
int mm1_proto_beam_offset(void) {
    return !state.teleport?0:-(state.teleport<=16?state.teleport-1:32-state.teleport)*16;
}
uint32_t mm1_proto_color(unsigned index) {
    if(index<1||index>6||!loaded)return 0;
    uint32_t c=atlas.colors[index-1];
    if(R(0x5F)) {
        unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
        if(r==g&&g==b&&r>32&&r<240)c=cyc_render_color(17);
        else if(r>64&&r>g+b)c=cyc_render_color(18);
    }
    return c;
}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof states)return -1;
    health_record();
    for(int i=0;i<2;i++){memcpy(states[i].magic,"MP03",4);states[i].on=(uint8_t)enabled;states[i].mode=(uint8_t)real_mode;}
    memcpy(out,states,sizeof states);return sizeof states;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;
    if(!in || len!=sizeof states)return 0;
    ProtoState next[2];memcpy(next,in,sizeof next);
    for(int i=0;i<2;i++)if(memcmp(next[i].magic,"MP03",4)||next[i].on!=enabled||next[i].mode!=real_mode||
        next[i].selected>1||next[i].teleport>32||next[i].previous>1||next[i].pending>1||next[i].dispatched>1||
        next[i].health_ready>1||next[i].health_owner>1||next[i].health[0]>28||next[i].health[1]>28)return 0;
    return 1;
}
static int put(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;
    memset(states,0,sizeof states);if(len)memcpy(states,in,sizeof states);return 1;
}
static int pose_done(uint16_t address){(void)address;mm1_proto_pose_done();return 0;}
static void reset(void){if(mm1_net_locked())return;coop_required=0;set_enabled(0);real_mode=1;start_proto=1;}
static void activate(void) {
    if(mm1_net_locked())return;
    char value[32]="protoman";
    nes_mod_option_value("mega-man-usa.character.protoman","protoman","mode",value,sizeof value);
    real_mode=!strcmp(value,"protoman");
    strcpy(value,"protoman");
    nes_mod_option_value("mega-man-usa.character.protoman","protoman","start",value,sizeof value);
    start_proto=!strcmp(value,"protoman");set_enabled(1);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(mm1_net_locked())return false;
    if(!strcmp(name,"--proto-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--protoman"))return false;
    if(!strcmp(value,"off")){set_enabled(0);return true;}
    if(strcmp(value,"on")&&strcmp(value,"reskin"))return false;
    real_mode=strcmp(value,"reskin")!=0;start_proto=1;set_enabled(1);return enabled!=0;
}
const CycHostExtras *mm1_proto_extras(void) {
    static const CycHostOption options[]={{"--protoman",true,"off|on|reskin: optional character"},
        {"--proto-report",true,"FILE: per-frame character QA observations"}};
    static const CycHostExtras extras={.power_on=power_on,.frame_begin=begin,.frame_end=end,.input=input,
        .options=options,.option_count=2,.option=option};
    return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_proto) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_function_entry_plugin("mm1.proto.player",0x94A8,player);
    nes_mod_register_function_entry_plugin("mm1.proto.damage",0xA23D,damage);
    nes_mod_register_function_entry_plugin("mm1.proto.shots",0xA747,shots);
    nes_mod_register_function_entry_plugin("mm1.proto.muzzle",0xA970,muzzle);
    nes_mod_register_function_entry_plugin("mm1.proto.pose-done",0xD2A3,pose_done);
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mm1.protoman",activate);
    nes_mod_register_savestate_hook("mm1.protoman",get,put);nes_mod_register_savestate_validator("mm1.protoman",valid);
#endif
}
