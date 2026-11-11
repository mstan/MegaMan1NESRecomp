/* Two personal RAM views; the original frame loop remains the sole world clock. */
#include "mm1_coop.h"
#include "mm1_proto.h"
#include "mm1_proto_video.h"
#include "mm1_reflect.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "logical_input.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum {MAX_PERSONAL=768};
typedef struct {uint8_t ram[MAX_PERSONAL],previous,held,dead,pad;} Player;
typedef struct {int16_t x;uint8_t y,attr,height,pattern[32],palette[4];} Part;
typedef struct {uint64_t mask;int16_t origin;uint16_t camera;uint8_t oam[256];Part parts[64];} Picture;
typedef struct {
    CycModRegs regs;
    uint8_t phase,done,total,reference,scratch[14],lift_count;
    uint8_t ai_active,ai_slot,ai_stack,draw_active,draw_owner,draw_stack,draw_total;
    uint8_t refill_active,draw_freeze,draw_life[16];
    uint8_t hit_owner[32];uint32_t pending;uint16_t before_hits;
} World;
typedef struct {
    char magic[4];uint8_t on,initialized,stage,current,in_guest,drawing,roster,anchor;
    Player players[2];Picture building,shown;uint8_t oam[256];
    uint64_t draw_mask;uint8_t draw_oam[256];
    uint64_t primary_build_mask;uint8_t primary_oam[256];int16_t insert_slot;uint8_t primary_cursor,primary_seen,pair_ready;
    uint32_t test_applied;
    World world;
    uint64_t updates,failures,deaths[2];
} Coop;
static Coop C;
static int enabled,roster;
static uint16_t addresses[MAX_PERSONAL],indices[2048];
static int address_count;
static FILE *report;
static struct {uint32_t frame;uint16_t address;uint8_t seat,value;} test_pokes[32];
static int test_poke_count;
static const char *world_hook_ids[]={"mm1.coop.step","mm1.coop.next","mm1.coop.return",
    "mm1.coop.weapon-begin","mm1.coop.weapon-end","mm1.coop.boss-killed","mm1.coop.stage-clear"};
void mm1_controls_refresh(void);
int mm1_controls_gameplay(void);
int mm1_controls_menu(void);
int mm1_weapons_player_dispatch(void);
int mm1_coop_available(void){return enabled;}
int mm1_coop_seat(void){return enabled?C.current:0;}
int mm1_coop_anchor(void){return enabled?C.anchor:0;}
int mm1_coop_present_begin(void){int previous=C.current;if(enabled)C.current=C.anchor;return previous;}
void mm1_coop_present_end(int previous){C.current=(uint8_t)previous;}
int mm1_coop_character(void){return roster^(C.current&1);}
int mm1_coop_drawing(void){return C.drawing!=0;}
int mm1_coop_insert_slot(void){return C.insert_slot;}
void mm1_coop_primary_begin(void){C.primary_build_mask=0;C.primary_cursor=C.primary_seen=0;C.pair_ready=1;}
void mm1_coop_primary_body(void){if(enabled&&R(0x0C)==0){C.primary_cursor=(uint8_t)(R(0x0D)/4);C.primary_seen=1;}}
static void domain(void) {
    if(address_count)return;
    /* Camera, RNG, stage, unlocks, world actors, lives and sound queue stay
     * common. Lift buffers are the native world frame's existing buffers. */
    static const uint8_t zp[]={0x14,0x16,0x18,0x2D,0x2E,0x30,0x52,0x55,0x5F,0x60,0x61,
        0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x94,0x95,0x98,0x99,
        0x9B,0x9C,0x9D,0x9E,0x9F,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xAD};
    for(unsigned i=0;i<sizeof zp;i++)addresses[address_count++]=zp[i];
    for(int field=0;field<16;field++) {
        int base=field<8?0x400+field*32:0x600+(field-8)*32;
        for(int slot=0;slot<16;slot++)if(slot!=1)addresses[address_count++]=(uint16_t)(base+slot);
    }
    /* Magnet Beam's six arrays are indexed by its personal native slots. */
    for(int base=0x5A0;base<=0x5F0;base+=16)for(int slot=0;slot<16;slot++)addresses[address_count++]=(uint16_t)(base+slot);
    addresses[address_count++]=0x3E1;addresses[address_count++]=0x3E2;
    for(int i=0;i<address_count;i++)indices[addresses[i]]=(uint16_t)(i+1);
}
static void capture(Player *p){for(int i=0;i<address_count;i++)p->ram[i]=R(addresses[i]);}
static void project(const Player *p){for(int i=0;i<address_count;i++)W(addresses[i],p->ram[i]);}
static void personal(Player *p,unsigned address,unsigned value){if(indices[address])p->ram[indices[address]-1]=(uint8_t)value;}
int mm1_coop_read(int seat,uint16_t address) {
    if(seat<0||seat>1||address>=2048)return 0;
    int i=indices[address];return enabled&&C.initialized&&seat!=C.current&&i?C.players[seat].ram[i-1]:R(address);
}
static uint8_t reverse(uint8_t n){n=(uint8_t)(((n&0x55)<<1)|((n>>1)&0x55));n=(uint8_t)(((n&0x33)<<2)|((n>>2)&0x33));return(uint8_t)((n<<4)|(n>>4));}
static void initialize(void) {
    domain();C.current=C.anchor=0;memset(&C.world,0,sizeof C.world);capture(&C.players[0]);C.players[0].dead=0;
    C.players[1]=C.players[0];C.players[1].held=C.players[1].previous=0;
    int x=R(0x460)*256+R(0x480),left=R(0x1B)*256+R(0x1A);x-=24;if(x<left+16)x=left+16;
    personal(&C.players[1],0x460,x>>8);personal(&C.players[1],0x480,x);personal(&C.players[1],0x4A0,0);
    for(int slot=2;slot<16;slot++)personal(&C.players[1],0x600+slot,248);
    C.initialized=1;C.stage=R(0x31);C.building.mask=C.shown.mask=0;
}
static int private_call(uint16_t address,CycModRegs *regs,int limit,int hooks) {
    if(!cyc_mod_isolate_begin())return 0;
    if(limit)W(0x54,16);
    C.in_guest=1;C.drawing=(uint8_t)(address==0xD131);
    if(C.drawing) {
        memset(&C.building,0,sizeof C.building);C.draw_mask=0;
        int camera=R(0x460)*256+R(0x480)-128,left=R(0x27)*256,right=R(0x28)*256;
        if(camera<left)camera=left;if(camera>right)camera=right;
        C.building.camera=(uint16_t)camera;W(0x1A,camera);W(0x1B,camera>>8);
    }
    if(hooks)cyc_mod_allow_isolated_hooks(true);
    int ok=cyc_mod_call(address,regs);
    uint8_t result[MAX_PERSONAL],sound[16];int count=0;
    if(ok) {
        if(C.drawing) {
            mm1_video_private_finish();for(int i=0;i<256;i++)C.building.oam[i]=R(0x200+i);
            for(int slot=0;slot<64;slot++)if(C.draw_mask&(1ull<<slot)) {
                const uint8_t *o=C.building.oam+slot*4;
                if(memcmp(o,C.draw_oam+slot*4,4))continue;
                C.building.mask|=1ull<<slot;Part *p=&C.building.parts[slot];p->x=o[3];p->y=o[0];p->attr=o[2];
                p->height=(R(0xFF)&32)?16:8;
                unsigned pattern=p->height==16?(o[1]&1)*0x1000+(o[1]&254)*16:
                    ((R(0xFF)&8)?0x1000:0)+o[1]*16;
                for(int b=0;b<p->height*2;b++)p->pattern[b]=cyc_render_chr((uint16_t)(pattern+b));
                for(int c=0;c<4;c++)p->palette[c]=R(0x3E0+(o[2]&3)*4+c)&63;
            }
        }
        for(int i=0;i<address_count;i++)result[i]=R(addresses[i]);count=R(0x45);for(int i=0;i<16;i++)sound[i]=R(0x580+i);
    }
    C.in_guest=C.drawing=0;cyc_mod_isolate_end();
    if(ok) {
        for(int i=0;i<address_count;i++)W(addresses[i],result[i]);
        int before=R(0x45);if(count>=before&&count<=16){for(int i=before;i<count;i++)W(0x580+i,sound[i]);W(0x45,count);}
    }
    return ok;
}
static int draw_part(uint16_t address) {
    (void)address;int actor=R(0x0C);if(actor==1||actor>=16||(!C.drawing&&actor))return 0;
    CycModRegs r={0};cyc_mod_regs(&r);int slot=r.x/4+1;if(slot>=64)return 0;
    uint8_t *oam=C.drawing?C.draw_oam:C.primary_oam;
    if(C.drawing)C.draw_mask|=1ull<<slot;else C.primary_build_mask|=1ull<<slot;
    for(int i=0;i<4;i++)oam[slot*4+i]=R(0x200+slot*4+i);return 0;
}
int mm1_coop_dispatch(void) {
    if(!enabled||C.in_guest||cyc_mod_isolated()||mm1_reflect_busy()||!mm1_controls_gameplay())return 0;
    if(!C.initialized||C.stage!=R(0x31))initialize();
    for(int i=0;i<test_poke_count;i++)if(!(C.test_applied&(1u<<i))&&cyc_ring_frame>=test_pokes[i].frame) {
        int seat=test_pokes[i].seat,address=test_pokes[i].address,value=test_pokes[i].value;
        if(seat==C.current)W(address,value);personal(&C.players[seat],address,value);C.test_applied|=1u<<i;
    }
    if(!mm1_world_prepare()){C.failures++;return 0;}
    capture(&C.players[C.anchor]);int guest=C.anchor^1;if(C.players[guest].dead)return 0;
    uint8_t original[2048],after[2048];for(int i=0;i<2048;i++)original[i]=R(i);
    CycModRegs regs={0},original_regs={0};cyc_mod_regs(&original_regs);regs=original_regs;
    C.current=(uint8_t)guest;Player *p=&C.players[guest];project(p);
    uint8_t held=(uint8_t)(p->held&~12);W(0x14,held);W(0x16,p->previous);W(0x18,held&~p->previous);p->previous=held;
    mm1_weapons_player_dispatch();mm1_charge_dispatch();
    int ok=private_call(0x94A8,&regs,0,1);
    if(ok){mm1_slide_after_run();mm1_slide_restore_input();ok=private_call(0xA35C,&regs,0,1);}
    if(ok){capture(p);C.updates++;}else C.failures++;
    for(int i=0;i<2048;i++)after[i]=R(i);
    for(int i=0;i<2048;i++)if(R(i)!=original[i])W(i,original[i]);
    if(ok&&after[0x45]>=original[0x45]&&after[0x45]<=16) {
        for(int i=original[0x45];i<after[0x45];i++)W(0x580+i,after[0x580+i]);W(0x45,after[0x45]);
    }
    C.current=C.anchor;cyc_mod_set_regs(&original_regs);return 0;
}
static uint16_t hit_flags(void) {
    uint16_t bits=0;for(int slot=16;slot<32;slot++)if(R(0x420+slot)&128)bits|=(uint16_t)(1u<<(slot-16));return bits;
}
void mm1_coop_record_hits(int owner,uint16_t before,uint16_t after) {
    if(!enabled||!C.initialized)return;
    unsigned added=after&~before;for(int slot=16;slot<32;slot++)if(added&(1u<<(slot-16))) {
        C.world.hit_owner[slot]=(uint8_t)owner;C.world.pending|=1u<<slot;
    }
}
static int weapon_begin(uint16_t address) {
    (void)address;if(enabled&&C.initialized&&!cyc_mod_isolated()&&!C.world.phase) {
        C.world.before_hits=hit_flags();C.world.pending&=(uint32_t)C.world.before_hits<<16;
    }return 0;
}
static int weapon_end(uint16_t address) {
    (void)address;if(enabled&&C.initialized&&!cyc_mod_isolated()&&!C.world.phase)
        mm1_coop_record_hits(C.anchor,C.world.before_hits,hit_flags());return 0;
}
static int native_target(uint16_t target) {
    CycModRegs r={0};cyc_mod_regs(&r);unsigned stack=r.s;
    /* A real native RTS reaches the guarded C3BE return, underneath which
     * the original EnemyAI caller remains intact. No PC assignment is used. */
    W(0x100|stack,0xC3);stack=(stack-1)&255;W(0x100|stack,0xBD);stack=(stack-1)&255;
    target--;W(0x100|stack,target>>8);stack=(stack-1)&255;W(0x100|stack,target);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);return 1;
}
static int world_step(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated())return 0;
    World *w=&C.world;int slot=R(0x2F);CycModRegs r={0};cyc_mod_regs(&r);
    if(w->phase)return 0;
    unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
    if(ret!=0x91BB||r.s<8||R(0x54)!=32)return 0;
    if(slot==2)w->done=0;
    if(slot==16&&!w->done&&!w->ai_active) {
        w->done=1;int guest=C.anchor^1;if(C.players[guest].dead)return 0;
        capture(&C.players[C.anchor]);w->regs=r;w->total=R(0x54);w->reference=R(0x2F);w->lift_count=R(0x9A);
        for(int i=0;i<14;i++)w->scratch[i]=R(i);
        C.current=(uint8_t)guest;project(&C.players[guest]);if(R(0x55))W(0x55,R(0x55)-1);
        w->phase=1;return native_target(0xA18B);
    }
    /* Deferred native enemy damage reads the attacker's equipment and charge
     * state. Keep that personal view until this exact actor reaches NextObject. */
    if(slot>=16&&slot<32&&!w->ai_active&&(w->pending&(1u<<slot))&&(R(0x420+slot)&128)) {
        int owner=w->hit_owner[slot];if(owner!=C.anchor) {
            capture(&C.players[C.anchor]);w->ai_active=1;w->ai_slot=(uint8_t)slot;w->ai_stack=r.s;
            C.current=(uint8_t)owner;project(&C.players[owner]);
        }
    }return 0;
}
static int world_next(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated())return 0;
    World *w=&C.world;if(!w->ai_active)return 0;CycModRegs r={0};cyc_mod_regs(&r);
    if(r.s!=w->ai_stack||R(0x2F)!=w->ai_slot)return 0;
    capture(&C.players[C.current]);w->ai_active=0;C.current=C.anchor;project(&C.players[C.anchor]);return 0;
}
int mm1_coop_native_pool(void){return enabled&&C.world.phase==3;}
int mm1_coop_total(void){return enabled&&C.world.phase?C.world.total:R(0x54);}
int mm1_coop_inflight(void){return enabled&&C.initialized&&(C.world.phase||C.world.ai_active||C.world.draw_active||C.world.refill_active);}
int mm1_coop_controls_paused(void) {
    return enabled&&C.initialized&&(C.world.refill_active||mm1_coop_read(0,0xAD)||mm1_coop_read(1,0xAD));
}
static int refill_begin(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated()||C.world.phase||C.world.ai_active||C.world.refill_active)return 0;
    int guest=C.anchor^1;if(R(0xAD)||C.players[guest].dead||!mm1_coop_read(guest,0xAD))return 0;
    capture(&C.players[C.anchor]);C.world.refill_active=1;C.current=(uint8_t)guest;project(&C.players[guest]);return 0;
}
void mm1_coop_frame_begin(void) {
    if(!enabled||!C.world.refill_active)return;
    capture(&C.players[C.current]);C.current=C.anchor;project(&C.players[C.anchor]);C.world.refill_active=0;
}
void mm1_coop_draw_begin(void) {
    if(!enabled||!C.initialized||C.drawing||cyc_mod_isolated()||C.current==C.anchor||C.world.draw_active)return;
    CycModRegs r={0};cyc_mod_regs(&r);C.world.draw_active=1;C.world.draw_owner=C.current;C.world.draw_stack=r.s;
    C.world.draw_total=R(0x54);W(0x54,mm1_coop_total());
    capture(&C.players[C.current]);C.current=C.anchor;project(&C.players[C.anchor]);
    if(C.world.refill_active) {
        C.world.draw_freeze=1;for(int slot=0;slot<16;slot++)if(slot!=1) {
            C.world.draw_life[slot]=R(0x6A0+slot);W(0x6A0+slot,R(0x6A0+slot)+1);
        }
    }
}
static int native_return(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated())return 0;
    World *w=&C.world;CycModRegs r={0};cyc_mod_regs(&r);
    if(w->draw_active&&r.s==w->draw_stack) {
        if(w->draw_freeze){for(int slot=0;slot<16;slot++)if(slot!=1)W(0x6A0+slot,w->draw_life[slot]);w->draw_freeze=0;}
        capture(&C.players[C.anchor]);C.current=w->draw_owner;project(&C.players[C.current]);W(0x54,w->draw_total);w->draw_active=0;return 0;
    }
    if(!w->phase||r.s!=w->regs.s)return 0;
    if(w->phase==1&&!C.players[C.current].dead) {w->before_hits=hit_flags();w->phase=2;return native_target(0xA277);}
    if(w->phase==2) {
        mm1_coop_record_hits(C.current,w->before_hits,hit_flags());w->phase=3;W(0x54,16);return native_target(0x98EA);
    }
    W(0x54,w->total);W(0x9A,w->lift_count);
    if(!C.players[C.current].dead) {
        CycModRegs draw=r;if(!private_call(0xD131,&draw,1,1))C.failures++;
    }
    capture(&C.players[C.current]);C.current=C.anchor;project(&C.players[C.anchor]);
    W(0x2F,w->reference);for(int i=0;i<14;i++)W(i,w->scratch[i]);r=w->regs;w->phase=0;
    unsigned stack=r.s;W(0x100|stack,0x98);stack=(stack-1)&255;W(0x100|stack,0xF1);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);return 1;
}
static int script_owner(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated())return 0;
    World *w=&C.world;if(w->phase||w->ai_active) {
        /* BossKilled discards the native caller stack at CAD7. Keep the real
         * finisher as the main native player before that nonreturning script. */
        if(w->phase)W(0x54,w->total);capture(&C.players[C.current]);C.anchor=C.current;
        w->phase=w->ai_active=w->draw_active=0;C.building.mask=C.shown.mask=0;
    }return 0;
}
void mm1_coop_stage_enter(void){C.initialized=0;C.current=C.anchor=0;memset(&C.world,0,sizeof C.world);C.building.mask=C.shown.mask=0;}
void mm1_coop_video_latch(void) {
    if(!enabled)return;
    /* A lag NMI before the main native draw must retain both old bodies.
     * Publish a private draw only once the corresponding main draw began. */
    if(C.pair_ready){C.shown=C.building;mm1_video_private_latch(C.anchor^1);C.pair_ready=0;}
    C.shown.origin=(int16_t)(C.shown.camera-(R(0x1B)*256+R(0x1A)));
    C.insert_slot=C.primary_cursor;if(!C.primary_seen)C.shown.mask=0;
    for(int slot=0;slot<64;slot++)if(C.primary_build_mask&(1ull<<slot)) {
        int match=1;for(int i=0;i<4;i++)if(C.primary_oam[slot*4+i]!=R(0x200+slot*4+i))match=0;
        if(match)C.insert_slot=(int16_t)slot;
    }
    for(int i=0;i<256;i++)C.oam[i]=R(0x200+i);
}
int mm1_coop_picture(void) {
    return enabled&&C.initialized&&C.shown.mask&&!mm1_controls_menu()&&
        mm1_proto_atlas()&&!memcmp(C.oam,cyc_render_oam(),256);
}
void mm1_coop_paint(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    if(!mm1_coop_picture())return;Picture *f=&C.shown;int previous=C.current;C.current=C.anchor^1;
    int offset=x0+f->origin;mm1_video_private_dust(out,width,offset,coverage);
    for(int slot=63;slot>=0;slot--)if(f->mask&(1ull<<slot)) {
        if(mm1_video_private_place(out,width,offset,coverage,slot))continue;
        const Part *p=&f->parts[slot];if(p->y>=239)continue;
        for(int row=0;row<p->height;row++)for(int col=0;col<8;col++) {
            int y=p->y+1+row,x=offset+p->x+col;if(x<0||x>=width||y<0||y>=240||!(cyc_render_line_mask(y)&16))continue;
            if(width==256&&x>=0&&x<8&&!(cyc_render_line_mask(y)&4))continue;
            if((p->attr&32)&&coverage&&coverage[y*width+x])continue;
            int r=(p->attr&128)?p->height-1-row:row,bit=(p->attr&64)?col:7-col,at=(r/8)*16+(r&7);
            int value=((p->pattern[at]>>bit)&1)|(((p->pattern[at+8]>>bit)&1)<<1);
            if(value)out[y*width+x]=cyc_render_color_value(p->palette[value]);
        }
    }
    mm1_video_private_after(out,width,offset,coverage);C.current=(uint8_t)previous;
}
static int death(uint16_t address) {
    (void)address;if(!enabled||(!C.in_guest&&!C.world.phase))return 0;
    Player *p=&C.players[C.current];if(!p->dead)C.deaths[C.current]++;p->dead=1;
    W(0x6A,0);W(0x600,248);W(0x420,1);C.building.mask=0;return 1;
}
static void set_enabled(int on) {
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on!=0;if(enabled)mm1_proto_coop_enable();
    nes_mod_set_function_hook_enabled("mm1.coop.death",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.draw-part",enabled);
    for(int i=0;i<7;i++)nes_mod_set_function_hook_enabled(world_hook_ids[i],enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.refill",enabled);
    nes_mod_set_local_only("Mega Man 1 co-op development",enabled);
    C.initialized=0;C.current=C.anchor=0;memset(&C.world,0,sizeof C.world);mm1_controls_refresh();mm1_video_character_changed();
}
static void power_on(void *ctx){(void)ctx;memset(&C,0,sizeof C);domain();}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;if(!enabled)return;
    for(int i=0;i<2;i++)C.players[i].held=reverse(nes_input_seat(i+1));
    buttons[0]=nes_input_seat(C.anchor+1)&(uint8_t)~0x20;buttons[1]=0;
}
static void frame_end(void *ctx) {
    (void)ctx;if(enabled&&C.initialized)capture(&C.players[C.current]);
    if(report)fprintf(report,"{\"frame\":%llu,\"enabled\":%u,\"ready\":%u,\"updates\":%llu,\"failures\":%llu,\"anchor\":%u,\"owner\":%u,\"phase\":%u,\"ai\":%u,\"refill\":%u,\"x\":[%d,%d],\"y\":[%d,%d],\"hp\":[%d,%d],\"invul\":[%d,%d],\"weapon\":[%d,%d],\"energy\":[%d,%d],\"lives\":%d,\"picture\":%d}\n",
        (unsigned long long)cyc_ring_frame,enabled,C.initialized,(unsigned long long)C.updates,(unsigned long long)C.failures,
        C.anchor,C.current,C.world.phase,C.world.ai_active,C.world.refill_active,
        mm1_coop_read(0,0x460)*256+mm1_coop_read(0,0x480),mm1_coop_read(1,0x460)*256+mm1_coop_read(1,0x480),
        mm1_coop_read(0,0x600),mm1_coop_read(1,0x600),mm1_coop_read(0,0x6A),mm1_coop_read(1,0x6A),
        mm1_coop_read(0,0x55),mm1_coop_read(1,0x55),mm1_coop_read(0,0x5F),mm1_coop_read(1,0x5F),
        mm1_coop_read(0,0x6A+mm1_coop_read(0,0x5F)),mm1_coop_read(1,0x6A+mm1_coop_read(1,0x5F)),R(0xA6),mm1_coop_picture());
}
static int get(uint8_t *out,int cap){if(cap<(int)sizeof C)return -1;memcpy(C.magic,"MCO3",4);C.on=(uint8_t)enabled;C.roster=(uint8_t)roster;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof C)return 0;Coop n;memcpy(&n,in,sizeof n);
    if(memcmp(n.magic,"MCO3",4)||n.on!=enabled||n.roster!=roster||n.initialized>1||n.current>1||n.anchor>1||n.in_guest||n.drawing||
        n.players[0].dead>1||n.players[1].dead>1||n.primary_seen>1||n.insert_slot< -1||n.insert_slot>=64)return 0;
    if(n.world.phase>3||n.world.done>1||n.world.ai_active>1||n.world.draw_active>1||n.world.draw_owner>1||
       n.world.refill_active>1||n.world.draw_freeze>1||n.pair_ready>1||
       (n.world.phase&&(n.world.total!=32||n.world.regs.s<8))||
       (n.world.ai_active&&(n.world.ai_slot<16||n.world.ai_slot>=32)))return 0;
    for(int i=16;i<32;i++)if(n.world.hit_owner[i]>1)return 0;
    const Picture *frames[]={&n.building,&n.shown};
    for(int f=0;f<2;f++)for(int i=0;i<64;i++)if(frames[f]->mask&(1ull<<i)) {
        const Part *p=&frames[f]->parts[i];if(p->height!=8&&p->height!=16)return 0;
        for(int c=0;c<4;c++)if(p->palette[c]>63)return 0;
    }return 1;
}
static int put(const uint8_t *in,int len){if(!valid(in,len))return 0;memset(&C,0,sizeof C);if(len)memcpy(&C,in,sizeof C);return 1;}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(!strcmp(name,"--coop-report")){report=fopen(value,"w");return report!=NULL;}
    if(!strcmp(name,"--coop-test-poke")) {
        unsigned frame,seat,address,v;int n=0;domain();
        if(test_poke_count==32||sscanf(value,"%u:%u:%x:%x%n",&frame,&seat,&address,&v,&n)!=4||
            value[n]||seat<1||seat>2||address>=2048||!indices[address]||v>255)return false;
        test_pokes[test_poke_count].frame=frame;test_pokes[test_poke_count].seat=(uint8_t)(seat-1);
        test_pokes[test_poke_count].address=(uint16_t)address;test_pokes[test_poke_count++].value=(uint8_t)v;
        nes_mod_set_local_only("Mega Man 1 scripted test inputs",true);return true;
    }
    if(!strcmp(name,"--coop-p1")){if(strcmp(value,"megaman")&&strcmp(value,"protoman"))return false;
        roster=!strcmp(value,"protoman");C.initialized=0;return true;}
    if(strcmp(name,"--coop"))return false;if(strcmp(value,"on")&&strcmp(value,"off"))return false;
    set_enabled(!strcmp(value,"on"));return enabled==!strcmp(value,"on");
}
const CycHostExtras *mm1_coop_extras(void) {
    static const CycHostOption options[]={{"--coop",true,"on|off: development co-op controller slice"},
        {"--coop-p1",true,"megaman|protoman: complementary character roster"},
        {"--coop-test-poke",true,"FRAME:SEAT:ADDRESS:VALUE: bounded personal RAM precondition"},
        {"--coop-report",true,"FILE: personal controller observations"}};
    static const CycHostExtras extras={.power_on=power_on,.input=input,.frame_end=frame_end,
        .option=option,.options=options,.option_count=4};return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_coop) {
#ifdef MM1_PROFILE_USA
    if(!nes_mod_register_function_entry_plugin("mm1.coop.death",0xC219,death)||
       !nes_mod_register_function_entry_plugin("mm1.coop.draw-part",0xD293,draw_part)||
       !nes_mod_register_savestate_hook("mm1.coop",get,put)||
       !nes_mod_register_savestate_validator("mm1.coop",valid))abort();
    static const uint16_t pcs[]={0x98F2,0x998A,0xC3BE,0xA277,0x91B9,0xCA2D,0xC05A};
    NESModFunctionEntryCallback functions[]={world_step,world_next,native_return,weapon_begin,weapon_end,script_owner,script_owner};
    for(int i=0;i<7;i++)if(!nes_mod_register_function_entry_plugin(world_hook_ids[i],pcs[i],functions[i]))abort();
    if(!nes_mod_register_function_entry_plugin("mm1.coop.refill",0x915E,refill_begin))abort();
#endif
}
