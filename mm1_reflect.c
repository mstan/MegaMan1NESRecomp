/* Native shield/projectiles and a real-clock continuation for boss scripts. */
#include "mm1_reflect.h"
#include "mm1_proto.h"
#include "mm1_coop.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {
    char magic[4];
    uint8_t enabled,active,cursor,owner,weapon,reference,scratch[14],probe_done,total;
    CycModRegs regs;
    uint16_t reflected,before_hits,parent_return;
    uint32_t buster_hits;
    uint64_t blocks,reflections,passes,boss_kills;
} Reflection;
static Reflection S;
static int enabled;
static const char *hooks[]={"mm1.proto.shield","mm1.proto.reflected-pass","mm1.proto.reflected-damage",
    "mm1.proto.boss-killed","mm1.proto.player-killed","mm1.proto.stage-clear"};
static void refresh(void) {
    for(int i=0;i<6;i++)nes_mod_set_function_hook_enabled(hooks[i],enabled||S.active);
}
int mm1_weapon_identity(void){return S.active&&S.owner==mm1_coop_seat()?S.weapon:R(0x5F);}
int mm1_weapon_identity_for(int seat){return S.active&&S.owner==seat?S.weapon:mm1_coop_read(seat,0x5F);}
int mm1_reflect_busy(void){return S.active!=0;}
void mm1_reflect_configure(int on){enabled=on!=0;refresh();}
void mm1_reflect_reset(void){memset(&S,0,sizeof S);refresh();}
void mm1_reflect_stage_enter(void) {
    if(S.active){W(0x5F,S.weapon);W(0x2F,S.reference);W(0x54,S.total);}
    S.active=S.cursor=0;S.reflected=0;S.buster_hits=0;refresh();
}
static int field(int i,int slot){return(i<8?0x400+i*32:0x600+(i-8)*32)+slot;}
static void sound(int id){int n=R(0x45);if(n<16){W(0x580+n,id);W(0x45,n+1);}}
static int shield(uint16_t address) {
    (void)address;if(!enabled||!mm1_proto_guarding())return 0;
    CycModRegs r={0};cyc_mod_regs(&r);int slot=r.x;
    if(slot<16||slot>=32||R(0x600+slot)>=248||!(R(0x420+slot)&2))return 0;
    int type=R(0x6E0+slot);if(type!=0x1A&&type!=0x2D)return 0;
    int dx=(R(0x460+slot)*256+R(0x480+slot))-(R(0x460)*256+R(0x480));
    int front=(R(0x420)&64)?dx:-dx,dy=(int)R(0x600+slot)-R(0x600);
    if(front<6||front>22||dy<-20||dy>4)return 0;
    int target;for(target=14;target<16;target++)if(R(0x600+target)==248)break;
    if(target<16&&cyc_mod_isolate_begin()) {
        uint8_t actor[16];CycModRegs call=r;call.a=4;call.x=(uint8_t)target;
        int ok=cyc_mod_call(0xA913,&call)&&!(call.p&1);
        if(ok)for(int i=0;i<16;i++)actor[i]=R(field(i,target));
        cyc_mod_isolate_end();
        if(ok) {
            for(int i=0;i<16;i++)W(field(i,target),actor[i]);
            W(0x460+target,R(0x460+slot));W(0x480+target,R(0x480+slot));
            W(0x600+target,R(0x600+slot));W(0x620+target,R(0x620+slot));
            S.reflected|=(uint16_t)(1u<<target);S.reflections++;
        }
    }
    W(0x600+slot,248);sound(0x1B);S.blocks++;return 0;
}
static uint16_t hit_flags(void) {
    uint16_t bits=0;for(int i=16;i<32;i++)if(R(0x420+i)&128)bits|=(uint16_t)(1u<<(i-16));return bits;
}
static int native_pass(uint16_t address) {
    (void)address;CycModRegs r={0};cyc_mod_regs(&r);
    if(S.active) {
        /* Only our exact native RTS continuation may finish this scope. */
        unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
        if(r.s!=S.regs.s||ret!=S.parent_return)return 0;
        mm1_coop_record_hits(S.owner,S.before_hits,hit_flags());
        S.buster_hits|=(uint32_t)(hit_flags()&~S.before_hits)<<16;
        W(0x5F,S.weapon);W(0x2F,S.reference);W(0x54,S.total);
        for(int i=0;i<14;i++)W(i,S.scratch[i]);
        cyc_mod_set_regs(&S.regs);r=S.regs;
    } else {
        unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
        uint16_t live=0;for(int i=14;i<16;i++)if(R(0x600+i)<248&&R(0x400+i)==0x1A)live|=(uint16_t)(1u<<i);
        int caller=ret==0x91BB||(ret==0xC3BD&&mm1_coop_native_pool());
        if(!enabled||!live||!caller||r.s<8||cyc_mod_isolated())return 0;
        S.reflected=live;S.regs=r;S.weapon=R(0x5F);S.reference=R(0x2F);S.owner=(uint8_t)mm1_coop_seat();S.cursor=14;
        S.parent_return=(uint16_t)ret;
        S.total=R(0x54);
        for(int i=0;i<14;i++)S.scratch[i]=R(i);
    }
    int shot=S.cursor;
    while(shot<16&&(!(S.reflected&(1u<<shot))||R(0x600+shot)>=248))shot++;
    if(shot==16){S.active=0;S.cursor=0;refresh();return 0;}
    S.active=1;S.cursor=(uint8_t)(shot+1);S.before_hits=hit_flags();
    W(0x5F,0);W(0x2F,shot);W(0x54,mm1_coop_total());
    /* The hook is an actual JSR target. Its handled RTS enters the native
     * collision routine, whose real RTS comes back to this entry. The original
     * caller stays underneath. Hardware and NMIs continue normally throughout. */
    unsigned stack=r.s;
    W(0x100|stack,0x98);stack=(stack-1)&255;
    W(0x100|stack,0xE9);stack=(stack-1)&255;
    W(0x100|stack,0xC8);stack=(stack-1)&255;
    W(0x100|stack,0xB1);stack=(stack-1)&255;
#ifdef MM1_TEST_NMI_CONTINUATION
    /* A separate test build yields once through the original native wait.
     * Production builds contain no injected wait. */
    if(!S.probe_done){S.probe_done=1;W(0x100|stack,0xC0);stack=(stack-1)&255;
        W(0x100|stack,0x1A);stack=(stack-1)&255;}
#endif
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);S.passes++;return 1;
}
static int buster_damage(uint16_t address) {
    (void)address;CycModRegs r={0};cyc_mod_regs(&r);int slot=R(0x2F);
    if(slot<16||slot>=32||!(S.buster_hits&(1u<<slot)))return 0;
    /* A normal native hit is consumed later by this actor's AI. Substitute
     * only that hit's damage-table index; the player's selection stays real. */
    r.a=0;cyc_mod_set_regs(&r);S.buster_hits&=~(1u<<slot);return 0;
}
static int exit_scope(uint16_t address) {
    if(address==0xC219&&mm1_coop_personal_death())return 0;
    if(S.active&&address==0xCA2D)S.boss_kills++;
    mm1_reflect_stage_enter();return 0;
}
void mm1_reflect_frame_end(void) {
    for(int i=14;i<16;i++)if(R(0x600+i)>=248||R(0x400+i)!=0x1A)S.reflected&=(uint16_t)~(1u<<i);
    for(int i=16;i<32;i++)if(R(0x600+i)>=248||!(R(0x420+i)&128))S.buster_hits&=~(1u<<i);
}
void mm1_reflect_debug(char *out,int cap) {
    snprintf(out,(size_t)cap,"\"blocks\":%llu,\"reflections\":%llu,\"passes\":%llu,\"boss_kills\":%llu,\"continuation\":%u,\"effective_weapon\":%d",
        (unsigned long long)S.blocks,(unsigned long long)S.reflections,(unsigned long long)S.passes,
        (unsigned long long)S.boss_kills,S.active,mm1_weapon_identity());
}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof S)return -1;memcpy(S.magic,"MR01",4);S.enabled=(uint8_t)enabled;memcpy(out,&S,sizeof S);return sizeof S;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof S)return 0;
    Reflection n;memcpy(&n,in,sizeof n);
    return !memcmp(n.magic,"MR01",4)&&n.enabled==enabled&&n.active<=1&&n.owner<=1&&n.probe_done<=1&&
        (!n.active||(n.cursor>=15&&n.cursor<=16&&n.weapon<8&&n.regs.s>=8&&
            (n.parent_return==0x91BB||n.parent_return==0xC3BD)&&(n.total==16||n.total==32)))&&!(n.reflected&0x3FFF);
}
static int put(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;memset(&S,0,sizeof S);if(len)memcpy(&S,in,sizeof S);refresh();return 1;
}
NES_MOD_CONSTRUCTOR(register_mm1_reflection) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_function_entry_plugin(hooks[0],0xA1CA,shield);
    nes_mod_register_function_entry_plugin(hooks[1],0x98EA,native_pass);
    nes_mod_register_function_entry_plugin(hooks[2],0xBECA,buster_damage);
    nes_mod_register_function_entry_plugin(hooks[3],0xCA2D,exit_scope);
    nes_mod_register_function_entry_plugin(hooks[4],0xC219,exit_scope);
    nes_mod_register_function_entry_plugin(hooks[5],0xC05A,exit_scope);
    nes_mod_register_savestate_hook("mm1.protoman.reflection",get,put);
    nes_mod_register_savestate_validator("mm1.protoman.reflection",valid);
#endif
}
