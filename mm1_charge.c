/* MM4 charging; MM1 owns the projectile movement, collision and damage path. */
#include "mm1_charge.h"
#include "mm1_proto.h"
#include "mm1_reflect.h"
#include "mm1_slide.h"
#include "mm1_coop.h"
#include "mm1_net.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_mm4_donor.h"
#include "cyc_mm4_audio.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum {HALF=1,FULL=2,HALF_AT=32,FULL_AT=92,LOOP_AT=100};
typedef struct {uint8_t kind,age,animation,frame,tick,right;} Shot;
typedef struct {
    char magic[4];uint8_t on,counter,flashing,last_counter,pending,keep_slot,keep_y,keep;
    Shot shot[3];uint32_t full_hits,override_offset;uint8_t override_old,override_value,overriding,pad;
    uint64_t half,full,passes,canceled;
} Charge;
static Charge charges[2];
#define C (charges[mm1_coop_seat()])
static int enabled,loaded;
static char donor_path[1024];
static CycMm4ChargeData data;
static FILE *report;
static const char PACKAGE[]="mega-man-usa.backport.mm4";
static const char *hooks[]={"mm1.charge.geometry","mm1.charge.enemy-hit","mm1.charge.preserve-shot",
    "mm1.charge.boss-damage","mm1.charge.boss-after","mm1.charge.enemy-damage","mm1.charge.enemy-after",
    "mm1.charge.player-killed","mm1.charge.stage-clear"};
void mm1_controls_refresh(void);
int mm1_controls_gameplay(void);
int mm1_proto_real_traits(void);
int mm1_charge_available(void){return enabled;}
int mm1_charge_flashing(void){return enabled&&C.flashing;}
int mm1_charge_kind(int slot){return slot>=2&&slot<=4?C.shot[slot-2].kind:0;}
const CycMm4ChargeData *mm1_charge_data(void){return loaded?&data:NULL;}
int mm1_charge_sprite(int slot) {
    if(!mm1_charge_kind(slot)||R(0x600+slot)==248||R(0x400+slot)!=0x1A)return -1;
    Shot *s=&C.shot[slot-2];const CycMm4ChargeAnimation *a=&data.animations[s->animation];
    int id=a->sprite[s->frame]-0x31;return id>=0&&id<7?id:-1;
}
static int full_alive(void) {
    for(int i=2;i<5;i++)if(C.shot[i-2].kind==FULL&&R(0x600+i)!=248)return 1;return 0;
}
static void palette(const uint8_t colors[2]) {
    W(0x3E1,colors[0]);W(0x3E2,colors[1]);
    W(0x38,0xE0);W(0x39,3);W(0x3A,0xE0);W(0x3B,3);W(0x34,0x18);W(0x37,1);
}
static void normal_palette(void) {
    if(!C.flashing)return;
    uint8_t colors[2]={R(0xD485),R(0xD486)};palette(colors);C.flashing=0;
}
static void flash(void) {
    if(C.counter<HALF_AT){normal_palette();return;}
    int index=((C.counter>>3)&0xFC)|((C.counter>>1)&3);
    static const uint8_t donor_normal[2]={0x2C,0x11};uint8_t colors[2];
    for(int i=0;i<2;i++){colors[i]=data.flash[i+1][index];if(colors[i]==donor_normal[i])colors[i]=R(0xD485+i);}
    palette(colors);C.flashing=1;
}
void mm1_charge_cancel(void) {
    if(C.counter)C.canceled++;C.counter=C.pending=C.last_counter=0;
    normal_palette();cyc_mm4_audio_stop(0x22);cyc_mm4_audio_stop(0x27);
}
void mm1_charge_weapon_changed(void) {
    /* The native weapon switch already installed the new palette. */
    C.flashing=0;mm1_charge_cancel();memset(C.shot,0,sizeof C.shot);
}
static void clear_override(void) {
    if(C.overriding)cyc_mod_remove_prg_read_override(C.override_offset);C.overriding=0;
}
void mm1_charge_stage_enter(void) {
    mm1_charge_cancel();clear_override();memset(C.shot,0,sizeof C.shot);C.full_hits=0;C.keep=0;
}
static int field(int i,int slot){return(i<8?0x400+i*32:0x600+(i-8)*32)+slot;}
static void fire(int kind) {
    int live=0,slot=0;for(int i=2;i<5;i++){live+=R(0x600+i)!=248;if(R(0x600+i)==248)slot=i;}
    int muzzle_y=mm1_proto_muzzle_y(-1);
    if(!slot||full_alive()||(mm1_proto_real_traits()&&live>=2)||muzzle_y<0||muzzle_y>=240)return;
    CycModRegs call;cyc_mod_regs(&call);call.a=4;call.x=(uint8_t)slot;
    if(!cyc_mod_isolate_begin())return;
    uint8_t actor[16];int ok=cyc_mod_call(0xA913,&call)&&!(call.p&1);
    if(ok)for(int i=0;i<16;i++)actor[i]=R(field(i,slot));cyc_mod_isolate_end();if(!ok)return;
    for(int i=0;i<16;i++)W(field(i,slot),actor[i]);
    int right=(R(0x420)&64)!=0,x=R(0x460)*256+R(0x480)+(right?16:-15);
    W(0x460+slot,x>>8);W(0x480+slot,x);W(0x600+slot,muzzle_y);
    W(0x4C0+slot,4);W(0x4E0+slot,0x33);W(0x640,0x1F);W(0x60,0xC0);W(0x61,0);
    Shot *s=&C.shot[slot-2];memset(s,0,sizeof *s);s->kind=(uint8_t)kind;s->right=(uint8_t)right;
    s->animation=(uint8_t)(kind==FULL?0:2);
    if(kind==FULL)C.full++;else C.half++;cyc_mm4_audio_play(kind==FULL?0x3D:0x21);
}
static void steer(void) {
    for(int slot=2;slot<5;slot++) {
        Shot *s=&C.shot[slot-2];if(!s->kind)continue;
        if(R(0x600+slot)==248||R(0x400+slot)!=0x1A){memset(s,0,sizeof *s);continue;}
        if(s->kind==FULL&&s->animation==0) {
            int index=s->frame>5?5:s->frame;W(0x4C0+slot,data.speed[index]);W(0x4E0+slot,0);
            if(index==5){s->animation=1;s->frame=s->tick=0;}
        }
        const CycMm4ChargeAnimation *a=&data.animations[s->animation];
        if((s->tick++&127)==a->duration){s->tick&=128;s->frame=(uint8_t)(s->frame==a->last?0:s->frame+1);}
        if(s->age<255)s->age++;
    }
}
void mm1_charge_dispatch(void) {
    if(!enabled)return;
    if(mm1_weapon_identity()!=0||!mm1_controls_gameplay()||!R(0x6A)||mm1_proto_teleporting()||mm1_coop_seat_blocked(mm1_coop_seat())) {
        if(mm1_weapon_identity()!=0)C.flashing=0;mm1_charge_cancel();steer();return;
    }
    /* Ordinary recoil preserves the charge without advancing it. */
    if((R(0x420)&15)==3){steer();return;}
    int held=(R(0x14)&2)!=0,fresh=(R(0x18)&2)!=0;
    if(C.counter&&held) {
        if(++C.counter>=LOOP_AT)C.counter=FULL_AT;flash();
        if(C.counter==0x32&&C.last_counter!=0x32)cyc_mm4_audio_play(0x22);
        if(C.counter==FULL_AT&&C.last_counter==LOOP_AT-1)cyc_mm4_audio_play(0x27);
    } else if(C.counter) {
        int count=C.counter;C.counter=0;normal_palette();
        cyc_mm4_audio_stop(0x22);cyc_mm4_audio_stop(0x27);
        if(mm1_slide_active())C.pending=(uint8_t)count;
        else if(count>=HALF_AT)fire(count>=FULL_AT?FULL:HALF);
    } else if(fresh&&!mm1_slide_active())C.counter=1;
    if(C.pending&&!mm1_slide_active()) {
        if(C.pending>=HALF_AT)fire(C.pending>=FULL_AT?FULL:HALF);C.pending=0;
    }
    C.last_counter=C.counter;
    if(full_alive())W(0x18,R(0x18)&~2);steer();
}
static int full_slot(void){int s=R(0x2F);return s>=2&&s<5&&C.shot[s-2].kind==FULL&&R(0x400+s)==0x1A&&mm1_weapon_identity()==0;}
static int geometry(uint16_t address) {
    (void)address;if(full_slot()){W(1,8);W(2,8);}return 0;
}
static int enemy_hit(uint16_t address) {
    (void)address;C.keep=0;if(!full_slot())return 0;
    CycModRegs r;cyc_mod_regs(&r);if(r.x<16||r.x>=32)return 0;
    C.full_hits|=1u<<r.x;
    int ptr=R(0xBF44)|(R(0xBF45)<<8),amount=R(ptr+R(0x6E0+r.x))*3;
    if(amount&&R(0x6C0+r.x)<=amount){C.keep=1;C.keep_slot=R(0x2F);C.keep_y=R(0x600+C.keep_slot);}
    return 0;
}
static int preserve_shot(uint16_t address) {
    (void)address;if(!C.keep)return 0;CycModRegs r;cyc_mod_regs(&r);
    if(r.x==C.keep_slot){r.a=C.keep_y;cyc_mod_set_regs(&r);C.passes++;}C.keep=0;return 0;
}
static void scale_operand(unsigned address) {
    clear_override();
    if(address<0xC000)return;
    int old=R(address),value=old*3;if(value>255)value=255;
    unsigned offset=7u*0x4000u+(address&0x3FFFu);
    if(cyc_mod_set_prg_read_override(offset,(uint8_t)old,(uint8_t)value)) {
        C.overriding=1;C.override_offset=offset;C.override_old=(uint8_t)old;C.override_value=(uint8_t)value;
    }
}
static int boss_damage(uint16_t address) {
    if(full_slot()){CycModRegs r;cyc_mod_regs(&r);scale_operand((R(address+1)|(R(address+2)<<8))+r.x);}return 0;
}
static int enemy_damage(uint16_t address) {
    (void)address;int slot=R(0x2F);if(slot<16||slot>=32||!(C.full_hits&(1u<<slot)))return 0;
    CycModRegs r;cyc_mod_regs(&r);scale_operand((R(4)|(R(5)<<8))+r.y);C.full_hits&=~(1u<<slot);return 0;
}
static int damage_after(uint16_t address){(void)address;clear_override();return 0;}
static int stage_exit(uint16_t address){(void)address;mm1_charge_stage_enter();return 0;}
static void set_enabled(int on) {
    if(mm1_net_locked())return;
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on&&loaded;
    if(!enabled)mm1_charge_stage_enter();
    for(int i=0;i<9;i++)nes_mod_set_function_hook_enabled(hooks[i],enabled);
    cyc_mm4_audio_configure(enabled!=0,true);
    mm1_controls_refresh();mm1_video_character_changed();
}
static int load_donor(const char *path) {
    char why[160];
    if(!cyc_mm4_donor_load(path,why,sizeof why)){fprintf(stderr,"MM4 charged Buster: %s\n",why);return 0;}
    loaded=cyc_mm4_charge_data_load(&data);
    if(!loaded)fprintf(stderr,"MM4 charged Buster: invalid donor artwork/constants\n");
    return loaded;
}
int mm1_mm4_load(const char *feature) {
    const char *path=donor_path[0]?donor_path:nes_mod_external_rom_path(PACKAGE,feature,"mm4-usa");
#ifdef NESRECOMP_NET
    if(!path&&!donor_path[0])path=nes_mod_netplay_external_rom_path(PACKAGE,feature,"mm4-usa");
#endif
    return load_donor(path);
}
static void power_on(void *ctx){
    (void)ctx;memset(charges,0,sizeof charges);cyc_mm4_audio_configure(enabled!=0,true);cyc_mm4_audio_reset();
}
static void frame_end(void *ctx) {
    (void)ctx;cyc_mm4_audio_frame_end();
    for(int seat=0;seat<(mm1_coop_available()?2:1);seat++) {
        Charge *n=&charges[seat];
        for(int i=16;i<32;i++)if(R(0x600+i)==248||!(R(0x420+i)&128))n->full_hits&=~(1u<<i);
        if(report)fprintf(report,"{\"frame\":%llu,\"seat\":%d,\"counter\":%u,\"flash\":%u,\"hurt\":%u,\"half\":%llu,\"full\":%llu,\"passes\":%llu,\"kinds\":[%u,%u,%u]}\n",
            (unsigned long long)cyc_ring_frame,seat,n->counter,n->flashing,(mm1_coop_read(seat,0x420)&15)==3,
            (unsigned long long)n->half,(unsigned long long)n->full,(unsigned long long)n->passes,
            n->shot[0].kind,n->shot[1].kind,n->shot[2].kind);
    }
}
static void mix(void *ctx,int16_t *samples,size_t count){(void)ctx;cyc_mm4_audio_mix(samples,count);}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof charges)return -1;
    for(int i=0;i<2;i++){memcpy(charges[i].magic,"MC03",4);charges[i].on=(uint8_t)enabled;}
    memcpy(out,charges,sizeof charges);return sizeof charges;
}
static int valid_record(Charge n) {
    if(memcmp(n.magic,"MC03",4)||n.on!=enabled||n.counter>=LOOP_AT||n.pending>=LOOP_AT||n.flashing>1||n.overriding>1||n.keep>1)return 0;
    if(n.overriding&&(n.override_offset<7u*0x4000u||n.override_offset>=8u*0x4000u))return 0;
    if(n.keep&&(n.keep_slot<2||n.keep_slot>4))return 0;
    for(int i=0;i<3;i++)if(n.shot[i].kind>FULL||n.shot[i].animation>2||n.shot[i].right>1||
        (n.shot[i].kind&&n.shot[i].frame>data.animations[n.shot[i].animation].last))return 0;
    return 1;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof charges)return 0;
    Charge records[2];memcpy(records,in,sizeof records);
    return valid_record(records[0])&&valid_record(records[1])&&!(records[0].overriding&&records[1].overriding);
}
static int put(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;
    for(int i=0;i<2;i++)if(charges[i].overriding)cyc_mod_remove_prg_read_override(charges[i].override_offset);
    memset(charges,0,sizeof charges);if(len)memcpy(charges,in,sizeof charges);
    for(int i=0;i<2;i++){Charge *n=&charges[i];if(n->overriding&&
        !cyc_mod_set_prg_read_override(n->override_offset,n->override_old,n->override_value))return 0;}return 1;
}
static void reset(void){set_enabled(0);}
static void activate(void) {
    if(mm1_net_locked()||!mm1_mm4_load("charge"))return;cyc_mm4_audio_reset();set_enabled(1);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(mm1_net_locked())return false;
    if(!strcmp(name,"--mm4-rom")) {
        if(strlen(value)>=sizeof donor_path||!load_donor(value))return false;
        strcpy(donor_path,value);return true;
    }
    if(!strcmp(name,"--charge-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--charge"))return false;
    if(strcmp(value,"off")&&strcmp(value,"on"))return false;
    if(!strcmp(value,"on")&&!mm1_mm4_load("charge"))return false;
    set_enabled(!strcmp(value,"on"));return enabled==!strcmp(value,"on");
}
const CycHostExtras *mm1_charge_extras(void) {
    static const CycHostOption options[]={{"--mm4-rom",true,"FILE: verify MM4 USA donor before enabling charge"},
        {"--charge",true,"on|off: MM4 charged Buster"},{"--charge-report",true,"FILE: charging observations"}};
    static const CycHostExtras extras={.power_on=power_on,.frame_end=frame_end,.audio_rate=48000,.audio_mix=mix,
        .options=options,.option_count=3,.option=option};return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_charge) {
#ifdef MM1_PROFILE_USA
    static const uint16_t addresses[]={0xC8CA,0xC99F,0xC9B9,0xC90F,0xC912,0xBEDD,0xBEDF,0xC219,0xC05A};
    NESModFunctionEntryCallback functions[]={geometry,enemy_hit,preserve_shot,boss_damage,damage_after,enemy_damage,damage_after,stage_exit,stage_exit};
    for(int i=0;i<9;i++)nes_mod_register_function_entry_plugin(hooks[i],addresses[i],functions[i]);
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mm1.backport.charge",activate);
    nes_mod_register_savestate_hook("mm1.charge",get,put);nes_mod_register_savestate_validator("mm1.charge",valid);
    nes_mod_register_savestate_hook("mm1.mm4.audio",cyc_mm4_audio_state_get,cyc_mm4_audio_state_set);
    nes_mod_register_savestate_validator("mm1.mm4.audio",cyc_mm4_audio_state_valid);
#endif
}
