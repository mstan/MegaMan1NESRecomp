/* L/R follows the native inventory order and keeps the world's actor slots. */
#include "cyc_host_extras.h"
#include "cyc_input.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_tcp.h"
#include "mm1_proto.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "mm1_coop.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>

#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
static int enabled;
static FILE *report;
typedef struct { uint8_t magic[4],on,previous,pending,menu,ready,stage,pad[2];uint64_t changes; } WeaponState;
static WeaponState states[2];
#define state (states[mm1_coop_seat()])
static unsigned attempt_frame,attempt_reason,attempt_edge,attempt_owned,fresh_inputs;
/* Native bank6 B50D/B515, expressed in its displayed inventory order. */
static const uint8_t order[]={0,5,4,1,6,2,3,7};
static const uint8_t mask[]={0,2,4,8,16,32,64,128};

static int safe(void) {
    return state.ready && state.stage==R(0x31) && R(0x31)<10 && R(0x54)==32 && R(0x600)<248 && R(0x6A) &&
        !(R(0x420)&15) && !R(0x1E) && !R(0x26) && !R(0x41) && !R(0x95) && !state.menu;
}
int mm1_controls_safe(void){return safe();}
int mm1_controls_menu(void){return state.menu!=0;}
int mm1_controls_gameplay(void) {
    return state.ready&&state.stage==R(0x31)&&R(0x31)<10&&R(0x54)==32&&R(0x600)<248&&
        R(0x6A)&&!R(0x1E)&&!R(0x26)&&!R(0x41)&&!state.menu;
}
void mm1_controls_refresh(void) {
    int on=enabled||mm1_proto_available()||mm1_charge_available()||mm1_slide_available()||mm1_coop_available();
    nes_mod_set_function_hook_enabled("mm1.weapons.dispatch",on);
    nes_mod_set_function_hook_enabled("mm1.weapons.menu-enter",on);
    nes_mod_set_function_hook_enabled("mm1.weapons.menu-leave",on);
    nes_mod_set_function_hook_enabled("mm1.weapons.stage",on);
}
static void set_enabled(int on) {
#ifdef MM1_PROFILE_USA
    enabled=on!=0;
#else
    (void)on;enabled=0;
#endif
    state.pending=state.previous=0;
    mm1_controls_refresh();
}
static int owned_sprite(int weapon,int sprite,int slot) {
    switch(weapon) {
    case 0:return slot>=2 && slot<5 && sprite==0x1A;
    case 1:return slot==5 && sprite==0x5B;
    case 2:return slot==5 && sprite==0x5C;
    case 3:return slot>=2 && slot<14 && sprite==0x5D;
    case 4:return slot>=5 && slot<7 && (sprite==0x60 || sprite==0x61);
    case 5:return slot>=5 && slot<12 && ((sprite>=0x62 && sprite<=0x66)||sprite==0x6A||sprite==0x6B);
    case 6:return slot>=5 && slot<9 && (sprite==0 || sprite==0x6C || sprite==0x32);
    case 7:return slot>=5 && slot<10 && sprite==0x5E;
    default:return 0;
    }
}
int mm1_weapons_player_dispatch(void) {
    /* READY and native arrival scripts can have a normal-looking player RAM
     * image. Only reaching the actual gameplay loop admits a control edge. */
    state.ready=1;state.stage=R(0x31);
    unsigned fresh=state.pending;state.pending=0;
    if(fresh){attempt_frame=cyc_ring_frame;attempt_edge=R(0x18);attempt_owned=R(0x5D);attempt_reason=0;}
    if(!enabled || (fresh!=CYC_GAME_L && fresh!=CYC_GAME_R) || !safe() || (R(0x18)&12)) {
        if(fresh)attempt_reason=!enabled?1:(fresh!=1&&fresh!=2)?2:!safe()?3:4;
        return 0;
    }
    int current=R(0x5F),index=-1;
    for(int i=0;i<8;i++)if(order[i]==current)index=i;
    if(index<0){attempt_reason=5;return 0;}
    int next=index,step=fresh==CYC_GAME_L?-1:1;
    do{next=(next+step+8)%8;}while(next!=index && next && !(R(0x5D)&mask[next]));
    if(next==index){attempt_reason=6;return 0;}
    for(int slot=2;slot<16;slot++)if(owned_sprite(current,R(0x400+slot),slot))W(0x600+slot,248);
    if(current==7)for(int i=5;i<10;i++){W(0x5A0+i,0);W(0x5B0+i,0);}
    W(0x60,0);W(0x61,0);W(0x640,0);W(0x52,255);
    W(0x5F,order[next]);
    mm1_charge_weapon_changed();
    /* The same fixed-bank palette table and deferred PPU upload as Start. */
    W(0x3E1,R(0xD485+order[next]*2));W(0x3E2,R(0xD486+order[next]*2));
    W(0x38,0xE0);W(0x39,3);W(0x3A,0xE0);W(0x3B,3);W(0x34,0x18);W(0x37,1);
    state.changes++;
    return 0;
}
static int dispatch(uint16_t address) {
    (void)address;state.ready=1;state.stage=R(0x31);
    mm1_coop_dispatch();
    if(mm1_proto_dispatch()){mm1_charge_cancel();return 0;}
    mm1_charge_dispatch();return mm1_weapons_player_dispatch();
}
static int safe_seat(int seat) {
    if(seat==mm1_coop_seat())return safe();WeaponState *s=&states[seat];
#define P(a) mm1_coop_read(seat,(a))
    int result=s->ready&&s->stage==R(0x31)&&R(0x31)<10&&R(0x54)==32&&P(0x600)<248&&P(0x6A)&&
        !(P(0x420)&15)&&!R(0x1E)&&!R(0x26)&&!R(0x41)&&!P(0x95)&&!s->menu;
#undef P
    return result;
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;(void)buttons;
    for(int seat=0;seat<(mm1_coop_available()?2:1);seat++) {
        WeaponState *s=&states[seat];unsigned held=nes_input_game_buttons(seat+1)&(CYC_GAME_L|CYC_GAME_R);
        unsigned fresh=held&~s->previous;if(fresh)fresh_inputs++;
        if(!enabled||!safe_seat(seat)||(seat==mm1_coop_seat()&&mm1_proto_teleporting())||held==(CYC_GAME_L|CYC_GAME_R))s->pending=0;
        else s->pending|=(uint8_t)fresh;s->previous=(uint8_t)held;
    }
}
static void power_on(void *ctx){(void)ctx;memset(states,0,sizeof states);}
static void frame_end(void *ctx){
    (void)ctx;if(!safe())state.pending=0;
    if(report)fprintf(report,"{\"frame\":%llu,\"weapon\":%u,\"changes\":%llu,\"held\":%u,\"safe\":%d,\"hp\":%u,\"palette\":[%u,%u],\"pending\":%u,\"fresh_inputs\":%u,\"attempt\":[%u,%u,%u,%u]}\n",
        (unsigned long long)cyc_ring_frame,R(0x5F),(unsigned long long)state.changes,state.previous,safe(),R(0x6A),R(0x3E1),R(0x3E2),
        state.pending,fresh_inputs,attempt_frame,attempt_reason,attempt_edge,attempt_owned);
}
static void reset(void){set_enabled(0);}
static void activate(void){set_enabled(1);}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof states)return -1;
    for(int i=0;i<2;i++){memcpy(states[i].magic,"MW04",4);states[i].on=(uint8_t)enabled;}
    memcpy(out,states,sizeof states);return sizeof states;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;
    if(!in || len!=sizeof states)return 0;
    WeaponState next[2];memcpy(next,in,sizeof next);
    for(int i=0;i<2;i++)if(memcmp(next[i].magic,"MW04",4)||next[i].on!=enabled||next[i].previous>3||
        next[i].pending>3||next[i].menu>1||next[i].ready>1)return 0;return 1;
}
static int put(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;
    memset(states,0,sizeof states);if(len)memcpy(states,in,sizeof states);return 1;
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    if(!strcmp(name,"--weapon-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--weapon-cycle"))return false;
    if(strcmp(value,"on")&&strcmp(value,"off"))return false;
    set_enabled(!strcmp(value,"on"));return true;
}
static void tcp_state(int id,const char *line) {
    (void)line;char out[256];
    snprintf(out,sizeof out,"\"enabled\":%s,\"weapon\":%u,\"previous\":%u,\"pending\":%u,\"changes\":%llu,\"palette\":[%u,%u]",
        enabled?"true":"false",R(0x5F),state.previous,state.pending,(unsigned long long)state.changes,R(0x3E1),R(0x3E2));
    cyc_tcp_ok(id,out);
}
static void tcp_setup(void *ctx){(void)ctx;cyc_tcp_register("mm1_weapons","quick weapon selection state",tcp_state);}
static int menu_enter(uint16_t address){(void)address;state.menu=1;state.pending=0;mm1_charge_cancel();return 0;}
static int menu_leave(uint16_t address){(void)address;state.menu=0;state.pending=0;return 0;}
static int stage_enter(uint16_t address){
    (void)address;mm1_coop_stage_enter();
    for(int i=0;i<2;i++)states[i].ready=states[i].pending=0;
    mm1_proto_stage_enter();mm1_charge_stage_enter();mm1_slide_stage_enter();return 0;
}
const CycHostExtras *mm1_weapons_extras(void) {
    static const CycInputProfile profile={.id="megaman-weapons-1",
        .game_label={"Previous weapon (L)","Next weapon (R)"},.game_pad={"leftshoulder","rightshoulder"},
        .shortcut_pad={"none","leftstick+rightstick",NULL,NULL,NULL,NULL,NULL}};
    static const CycHostOption options[]={{"--weapon-cycle",true,"on|off: L/R cycles acquired weapons"},
        {"--weapon-report",true,"FILE: per-frame weapon QA observations"}};
    static const CycHostExtras extras={.input=input,.power_on=power_on,.frame_end=frame_end,.tcp_setup=tcp_setup,
#ifdef MM1_PROFILE_USA
        .input_profile=&profile,
#endif
        .option=option,.options=options,.option_count=2};
    return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_weapons) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_function_entry_plugin("mm1.weapons.dispatch",0x9165,dispatch);
    nes_mod_register_function_entry_plugin("mm1.weapons.menu-enter",0xC3BF,menu_enter);
    nes_mod_register_function_entry_plugin("mm1.weapons.menu-leave",0xC3E3,menu_leave);
    nes_mod_register_function_entry_plugin("mm1.weapons.stage",0x9081,stage_enter);
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mm1.weapons",activate);
    nes_mod_register_savestate_hook("mm1.weapons",get,put);
    nes_mod_register_savestate_validator("mm1.weapons",valid);
#endif
}
