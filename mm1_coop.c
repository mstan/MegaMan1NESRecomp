/* Two personal RAM views; the original frame loop remains the sole world clock. */
#include "mm1_coop.h"
#include "mm1_proto.h"
#include "mm1_proto_video.h"
#include "mm1_reflect.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "mm1_weapon_gfx.h"
#include "mm1_coop_menu.h"
#include "cyc_mod.h"
#include "cyc_mm4_audio.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "cyc_video.h"
#include "cyc_tcp.h"
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
enum {TELE_NONE,TELE_OUT,TELE_HIDDEN,TELE_IN,TELE_OPEN};
typedef struct {uint8_t ram[MAX_PERSONAL],previous,held,dead,blast_ticks,death_y,death_screen,death_x,
    death_sprite,death_flags,death_anim,death_fire,teleport,teleport_tick,transport_dispatched;
    int16_t beam_y,beam_floor;} Player;
typedef struct {int16_t x;uint8_t y,attr,height,actor,pattern[32],palette[4];} Part;
typedef struct {uint64_t mask;int16_t origin;uint16_t camera;uint8_t oam[256];Part parts[64];} Picture;
typedef struct {
    CycModRegs regs;
    uint8_t phase,done,total,reference,scratch[14],lift_count;
    uint8_t ai_active,ai_slot,ai_stack,draw_active,draw_owner,draw_stack,draw_total;
    uint8_t refill_active,draw_freeze,draw_life[16];
    uint8_t hit_owner[32];uint32_t pending;uint16_t before_hits;
} World;
typedef struct {
    char magic[4];uint8_t on,initialized,stage,current,in_guest,drawing,roster,anchor,independent;
    Player players[2];Picture building,shown,view_build[2],view_shown[2];uint8_t oam[256];
    uint8_t view_ready,view_drawing,view_visible,room_follow,skip_controller;
    uint8_t wait_mode,waiting,wait_direction[2],wait_screen[2],wait_y[2],wait_flags[2];uint16_t wait_camera[2];
    uint64_t draw_mask;uint8_t draw_oam[256];
    uint64_t primary_build_mask;uint8_t primary_oam[256];int16_t insert_slot;uint8_t primary_cursor,primary_seen,pair_ready;
    uint32_t test_applied;
    World world;
    uint8_t wiping,restarting,game_over,checkpoint,pending_wipe,wipe_owner;
    uint8_t build_owner,shown_owner,building_guest_owner,shown_guest_owner,intro_picture;
    uint8_t withdrawn,revive_pending,select_hold[2],select_armed[2],select_previous[2];
    uint64_t withdrawals[2],rejoins[2],revivals[2];
    uint64_t handoffs,wipes,retries;
    uint64_t updates,failures,deaths[2];
} Coop;
static Coop C;
static int enabled,roster,view_mode,wait_both;
static uint16_t addresses[MAX_PERSONAL],indices[2048];
static int address_count;
static FILE *report;
static struct {uint32_t frame;uint16_t address;uint8_t seat,value;} test_pokes[32];
static int test_poke_count;
static int private_camera,private_full;
static int16_t private_x[64];
static uint8_t private_actor[64];
static int private_exit,private_exit_camera;
static int begin_team_death(void);
static void enforce_dead(void);
static void hide_dead_body(void);
static void particle_step(Player *p);
static void lifecycle_tick(void);
static void follow_partner(void);
static int wait_for_partner(int seat,int direction);
static int unavailable(int seat){Player *p=&C.players[seat];return p->dead||((C.withdrawn&(1u<<seat))&&p->teleport!=TELE_IN&&p->teleport!=TELE_OPEN);}
int mm1_coop_seat_blocked(int seat){return enabled&&C.initialized&&(C.players[seat].dead||C.players[seat].teleport||C.players[seat].transport_dispatched||((C.room_follow|C.waiting)&(1u<<seat)));}
int mm1_coop_controller_blocked(void){return mm1_coop_seat_blocked(C.current)&&!(C.in_guest&&C.players[C.current].teleport==TELE_OPEN);}
int mm1_coop_transporting(void){return enabled&&C.initialized&&C.players[C.current].teleport&&C.players[C.current].teleport!=TELE_HIDDEN;}
int mm1_coop_view_active(void){
    if(!enabled||!C.initialized||C.wiping)return 0;
    for(int seat=0;seat<2;seat++)if(!C.players[seat].dead&&C.players[seat].teleport!=TELE_HIDDEN&&mm1_coop_read(seat,0x600)<239)return 1;
    return 0;
}
int mm1_coop_independent(void){return enabled&&view_mode!=0;}
int mm1_coop_independent_scene(void){return mm1_coop_independent()&&C.initialized&&!C.wiping&&C.stage==R(0x31)&&R(0x31)<10;}
int mm1_coop_draw_suppressed(void){return mm1_coop_independent_scene()&&C.view_drawing;}
int mm1_coop_draw_camera(void){return C.drawing==2?private_camera:R(0x1B)*256+R(0x1A);}
int mm1_coop_view_mode(void){return view_mode;}
int mm1_coop_view_locked(void){return C.wiping||R(0x1E)||R(0x26)||R(0x68)||R(0x3E)||(!C.initialized);}
int mm1_widescreen_mode(void);
void mm1_world_views_reset(void);
void mm1_coop_view_policy(void){
    cyc_video_override_width(mm1_coop_independent()?(view_mode==1?512:256):0);
}
static void state_loaded(void *ctx){(void)ctx;cyc_video_set_mode(mm1_widescreen_mode());mm1_coop_view_policy();mm1_video_character_changed();}
int mm1_coop_view_rects(int left[2]) {
    if(!mm1_coop_independent()||!C.initialized||C.stage!=R(0x31)||R(0x31)>=10)return 0;
    int camera=R(0x1B)*256+R(0x1A),lo=R(0x27)*256,hi=R(0x28)*256;
    for(int seat=0;seat<2;seat++) {
        int who=mm1_coop_seat_blocked(seat)||!mm1_coop_read(seat,0x6A)?C.anchor:seat;
        int x=mm1_coop_read(who,0x460)*256+mm1_coop_read(who,0x480)-128;
        if(x<lo)x=lo;if(x>hi)x=hi;
        left[seat]=mm1_coop_view_locked()?camera:x;
    }return 2;
}
static void restore_death_body(const Player *p) {
    W(0x600,p->death_y);W(0x460,p->death_screen);W(0x480,p->death_x);
    W(0x400,p->death_sprite);W(0x420,p->death_flags);W(0x440,p->death_anim);W(0x640,p->death_fire);
}
static const char *world_hook_ids[]={"mm1.coop.step","mm1.coop.next","mm1.coop.return",
    "mm1.coop.weapon-begin","mm1.coop.weapon-end","mm1.coop.boss-killed","mm1.coop.stage-clear",
    "mm1.coop.contact-local","mm1.coop.weapon-local"};
void mm1_controls_refresh(void);
int mm1_controls_gameplay(void);
int mm1_controls_menu(void);
int mm1_weapons_player_dispatch(void);
void mm1_weapons_equip(int weapon);
int mm1_coop_available(void){return enabled;}
int mm1_coop_seat(void){return enabled?C.current:0;}
int mm1_coop_anchor(void){return enabled?C.anchor:0;}
int mm1_coop_present_begin(void){int previous=C.current;if(enabled)C.current=C.shown_owner;return previous;}
int mm1_coop_dma_begin(void){int previous=C.current;if(enabled)C.current=C.shown_owner=C.build_owner;return previous;}
int mm1_coop_primary_publish(void){return !enabled||C.pair_ready;}
void mm1_coop_present_end(int previous){C.current=(uint8_t)previous;}
int mm1_coop_character(void){return roster^(C.current&1);}
int mm1_coop_character_for(int seat){return roster^(seat&1);}
int mm1_coop_drawing(void){return C.drawing!=0;}
int mm1_coop_insert_slot(void){return C.insert_slot;}
void mm1_coop_primary_begin(void){
    /* Changing the CPU anchor can precede the next actual main draw. Keep
     * the previous displayed colors until this body is built, then let the
     * native NMI upload its own personal armor palette with the new OAM. */
    if(enabled&&C.initialized&&C.build_owner!=C.current) {
        W(0x38,0xE0);W(0x39,3);W(0x3A,0xE0);W(0x3B,3);W(0x34,0x18);W(0x37,1);
    }
    C.primary_build_mask=0;C.primary_cursor=C.primary_seen=0;C.pair_ready=1;C.build_owner=C.current;
}
void mm1_coop_primary_body(void){if(enabled&&R(0x0C)==0){C.primary_cursor=(uint8_t)(R(0x0D)/4);C.primary_seen=1;}}
static void domain(void) {
    if(address_count)return;
    /* Camera, RNG, stage, unlocks, world actors, lives and sound queue stay
     * common. Lift buffers and their 9C/9D speed belong to the native world
     * frame; 9B/9E retain each rider's personal attachment state. */
    /* Bomb bounce damping and Cut's return acceleration live outside their
       native actors. Keep those original weapon variables with each pool. */
    static const uint8_t zp[]={0x14,0x16,0x18,0x2D,0x2E,0x30,0x52,0x55,0x5F,0x60,0x61,0x62,0x63,0x64,0x65,0x66,
        0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x94,0x95,0x98,0x99,
        0x9B,0x9E,0x9F,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xAD};
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
static int ai_personal_color(const Player *p,int address) {
    return C.world.ai_active&&p==&C.players[C.anchor^1]&&(address==0x3E1||address==0x3E2);
}
/* Native NMI can upload armor while an enemy is using the guest's combat
   image. Nested draw/frame callbacks must neither capture the anchor's colors
   into that guest nor replace the physical anchor's pending palette. */
static void capture(Player *p){for(int i=0;i<address_count;i++)if(!ai_personal_color(p,addresses[i]))p->ram[i]=R(addresses[i]);}
static void project(const Player *p){for(int i=0;i<address_count;i++)if(!ai_personal_color(p,addresses[i]))W(addresses[i],p->ram[i]);}
int mm1_coop_spawn_project(int seat) {
    if(!cyc_mod_isolated()||!enabled||!C.initialized||seat<0||seat>1)return 0;
    if(seat!=C.current)project(&C.players[seat]);return 1;
}
static void personal(Player *p,unsigned address,unsigned value){if(indices[address])p->ram[indices[address]-1]=(uint8_t)value;}
int mm1_coop_read(int seat,uint16_t address) {
    if(seat<0||seat>1||address>=2048)return 0;
    int i=indices[address];return enabled&&C.initialized&&seat!=C.current&&i?C.players[seat].ram[i-1]:R(address);
}
int mm1_coop_menu_ready(int seat) {
    if(!enabled||!C.initialized||C.wiping||seat<0||seat>1||mm1_coop_seat_blocked(seat)||R(0x31)>=10||mm1_coop_total()!=32)return 0;
    int flag=mm1_coop_read(seat,0x420)&15;
    return mm1_coop_read(seat,0x6A)&&mm1_coop_read(seat,0x600)<248&&!R(0x1E)&&!R(0x26)&&!R(0x41)&&
        !mm1_coop_read(seat,0xAD)&&flag!=1&&(mm1_coop_menu_active()||!mm1_coop_read(seat,0x60));
}
int mm1_coop_menu_scene(void){return enabled&&C.initialized&&!C.wiping&&R(0x31)<10;}
int mm1_coop_menu_project(int seat) {
    if(C.current!=C.anchor||mm1_coop_inflight()||mm1_reflect_busy()||cyc_mod_isolated())return 0;
    capture(&C.players[C.anchor]);C.current=(uint8_t)seat;project(&C.players[seat]);return 1;
}
void mm1_coop_menu_restore(void){capture(&C.players[C.current]);C.current=C.anchor;project(&C.players[C.anchor]);}
void mm1_coop_menu_begin(void) {
    capture(&C.players[C.current]);for(int seat=0;seat<2;seat++) {
        C.current=(uint8_t)seat;project(&C.players[seat]);mm1_slide_restore_input();mm1_charge_cancel();capture(&C.players[seat]);
    }
    C.current=C.anchor;project(&C.players[C.anchor]);C.select_hold[0]=C.select_hold[1]=C.select_armed[0]=C.select_armed[1]=0;
}
int mm1_coop_menu_equip(int seat,int weapon) {
    static const uint8_t index[]={0,5,4,1,6,2,3,7};
    if(!mm1_coop_menu_ready(seat)||weapon<0||weapon>7||(weapon&&!(R(0x5D)&(1u<<index[weapon]))))return 0;
    capture(&C.players[C.current]);C.current=(uint8_t)seat;project(&C.players[seat]);mm1_weapons_equip(weapon);capture(&C.players[seat]);
    C.current=C.anchor;project(&C.players[C.anchor]);return 1;
}
static uint8_t reverse(uint8_t n){n=(uint8_t)(((n&0x55)<<1)|((n>>1)&0x55));n=(uint8_t)(((n&0x33)<<2)|((n>>2)&0x33));return(uint8_t)((n<<4)|(n>>4));}
static void initialize(void) {
    domain();Player old[2],base;memcpy(old,C.players,sizeof old);
    int retain=C.restarting&&C.stage==R(0x31);C.current=C.anchor=0;memset(&C.world,0,sizeof C.world);memset(&base,0,sizeof base);capture(&base);
    for(int seat=0;seat<2;seat++) {
        C.players[seat]=base;Player *p=&C.players[seat];p->held=old[seat].held;p->previous=p->held&~12;
        if(retain) {
            for(int a=0x6B;a<=0x71;a++)personal(p,a,old[seat].ram[indices[a]-1]);
            for(int a=0x9E;a<=0xA5;a++)personal(p,a,old[seat].ram[indices[a]-1]);
        }
        personal(p,0x6A,28);personal(p,0x5F,0);personal(p,0x18,0);
    }
    if(retain)C.retries++;else C.checkpoint=0;
    int x=R(0x460)*256+R(0x480),left=R(0x1B)*256+R(0x1A);x-=24;if(x<left+16)x=left+16;
    personal(&C.players[1],0x460,x>>8);personal(&C.players[1],0x480,x);personal(&C.players[1],0x4A0,0);
    for(int slot=2;slot<16;slot++)personal(&C.players[1],0x600+slot,248);
    C.initialized=1;C.stage=R(0x31);C.building.mask=0;if(!C.intro_picture)C.shown.mask=0;C.pair_ready=0;C.intro_picture=0;
    C.build_owner=C.shown_owner=0;C.building_guest_owner=C.shown_guest_owner=1;
    C.wiping=C.restarting=C.game_over=C.pending_wipe=0;project(&C.players[0]);
    C.withdrawn=C.revive_pending=0;memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
    memset(C.select_previous,0,sizeof C.select_previous);
    C.view_ready=C.view_drawing=C.view_visible=0;memset(C.view_build,0,sizeof C.view_build);memset(C.view_shown,0,sizeof C.view_shown);
}
enum {WEAPON_WORLD_BYTES=1+0xD0+38};
static unsigned weapon_world_address(int i) {
    /* Native Guts/Elec firing changes active types and appends 21-byte TSA
       transfers. Coordinates, palette RAM and controller scratch stay scoped. */
    if(!i)return 0x1C;
    if(i<=0xD0)return 0x2FF+i;
    return 0x721+(i-0xD1)*6;
}
static int private_call(uint16_t address,CycModRegs *regs,int limit,int hooks) {
    if(address==0x94A8)private_exit=0;
    if(!cyc_mod_isolate_begin())return 0;
    if(address==0x94A8&&enabled&&C.initialized) {
        int camera=R(0x460)*256+R(0x480)-128,lo=R(0x27)*256,hi=R(0x28)*256;
        if(camera<lo)camera=lo;if(camera>hi)camera=hi;
        W(0x1A,camera);W(0x1B,camera>>8);
        if(!cyc_mod_call(0xCD72,regs)){cyc_mod_isolate_end();return 0;}
    }
    if(limit)W(0x54,16);
    /* The retail controller leaves actor zero as the collision reference for
       A35C. The isolated controller's caller scratch is restored on return. */
    int shooting=address==0xA35C,weapon_world=shooting||address==0x94A8;if(shooting)W(0x2F,0);
    C.in_guest=1;C.drawing=(uint8_t)(address==0xD131);
    if(C.drawing) {
        C.building_guest_owner=C.current;
        memset(&C.building,0,sizeof C.building);C.draw_mask=0;
        int camera=R(0x460)*256+R(0x480)-128,left=R(0x27)*256,right=R(0x28)*256;
        if(camera<left)camera=left;if(camera>right)camera=right;
        C.building.camera=(uint16_t)camera;W(0x1A,camera);W(0x1B,camera>>8);
    }
    if(hooks)cyc_mod_allow_isolated_hooks(true);
    int ok=cyc_mod_call(address,regs);
    if(ok&&address==0x94A8&&R(0x26)) {
        int direction=R(0x26),camera=R(0x1A)+R(0x1B)*256;
        if(mm1_world_exit_allowed(direction,camera,R(0x420))){
            if(!wait_both||!wait_for_partner(C.current,direction)){private_exit=direction;private_exit_camera=camera;}
            else W(0x26,0);
        }
        else if(direction==3)ok=cyc_mod_call(0xC219,regs);
    }
    uint8_t result[MAX_PERSONAL],sound[16],world[WEAPON_WORLD_BYTES];int count=0;
    if(ok) {
        if(C.drawing) {
            mm1_video_private_finish();for(int i=0;i<256;i++)C.building.oam[i]=R(0x200+i);
            for(int slot=0;slot<64;slot++)if(C.draw_mask&(1ull<<slot)) {
                const uint8_t *o=C.building.oam+slot*4;
                if(memcmp(o,C.draw_oam+slot*4,4))continue;
                C.building.mask|=1ull<<slot;Part *p=&C.building.parts[slot];p->x=o[3];p->y=o[0];p->attr=o[2];p->actor=private_actor[slot];
                p->height=(R(0xFF)&32)?16:8;
                unsigned pattern=p->height==16?(o[1]&1)*0x1000+(o[1]&254)*16:
                    ((R(0xFF)&8)?0x1000:0)+o[1]*16;
                for(int b=0;b<p->height*2;b++)p->pattern[b]=mm1_weapon_gfx_private((uint16_t)(pattern+b),cyc_render_chr((uint16_t)(pattern+b)));
                for(int c=0;c<4;c++)p->palette[c]=R(0x3E0+(o[2]&3)*4+c)&63;
            }
        }
        for(int i=0;i<address_count;i++)result[i]=R(addresses[i]);count=R(0x45);for(int i=0;i<16;i++)sound[i]=R(0x580+i);
        if(weapon_world)for(int i=0;i<WEAPON_WORLD_BYTES;i++)world[i]=R(weapon_world_address(i));
    }
    C.in_guest=C.drawing=0;cyc_mod_isolate_end();
    if(ok) {
        for(int i=0;i<address_count;i++)W(addresses[i],result[i]);
        if(weapon_world)for(int i=0;i<WEAPON_WORLD_BYTES;i++)W(weapon_world_address(i),world[i]);
        int before=R(0x45);if(count>=before&&count<=16){for(int i=before;i<count;i++)W(0x580+i,sound[i]);W(0x45,count);}
    }
    return ok;
}
static int private_oam_full(uint16_t address) {
    (void)address;if(C.drawing!=2||!cyc_mod_isolated())return 0;
    private_full=1;W(0x0D,252);return 1;
}
static void draw_view_player(int seat,int freeze) {
    int previous=C.current;capture(&C.players[previous]);C.current=(uint8_t)seat;project(&C.players[seat]);
    Picture *f=&C.view_build[seat];memset(f,0,sizeof *f);
    private_camera=R(0x460)*256+R(0x480)-128;f->camera=(uint16_t)private_camera;
    uint8_t result[MAX_PERSONAL];CycModRegs r={0};cyc_mod_regs(&r);
    int ok=cyc_mod_isolate_begin();
    if(ok) {
        C.in_guest=1;C.drawing=2;C.draw_mask=0;private_full=0;cyc_mod_allow_isolated_hooks(true);
        ok=cyc_mod_call(0xC413,&r);W(0x54,16);W(0x0D,0);
        W(0x1A,private_camera);W(0x1B,private_camera>>8);
        /* AI projection deliberately leaves physical armor with the anchor;
           this isolated native art capture needs the requested owner's colors. */
        W(0x3E1,C.players[seat].ram[indices[0x3E1]-1]);W(0x3E2,C.players[seat].ram[indices[0x3E2]-1]);
        for(int i=0;i<64;i++)W(0x200+i*4,248);
        mm1_proto_draw_begin();
        uint8_t life[16];for(int slot=0;slot<16;slot++)life[slot]=R(0x6A0+slot);
        if(freeze)for(int slot=0;slot<16;slot++)if(slot!=1)W(0x6A0+slot,life[slot]+1);
        for(int n=0;n<16&&ok&&!private_full;n++) {
            int actor=(R(0x23)&1)?n:15-n;if(actor==1||R(0x600+actor)>=248)continue;
            int camera=R(0x460+actor)*256+R(0x480+actor)-128;
            W(0x1A,camera);W(0x1B,camera>>8);W(0x0C,actor);ok=cyc_mod_call(0xD16E,&r);
            if(private_full)mm1_proto_pose_done();
        }
        if(freeze)for(int slot=0;slot<16;slot++)if(slot!=1)W(0x6A0+slot,life[slot]);
        if(ok) {
            mm1_video_private_finish();for(int i=0;i<256;i++)f->oam[i]=R(0x200+i);
            f->mask=C.draw_mask;
            for(int slot=0;slot<64;slot++)if(f->mask&(1ull<<slot)) {
                const uint8_t *o=f->oam+slot*4;if(memcmp(o,C.draw_oam+slot*4,4)){f->mask&=~(1ull<<slot);continue;}
                Part *p=&f->parts[slot];p->x=private_x[slot];p->y=o[0];p->attr=o[2];p->actor=private_actor[slot];p->height=(R(0xFF)&32)?16:8;
                unsigned pattern=p->height==16?(o[1]&1)*0x1000+(o[1]&254)*16:((R(0xFF)&8)?0x1000:0)+o[1]*16;
                for(int b=0;b<p->height*2;b++)p->pattern[b]=mm1_weapon_gfx_private((uint16_t)(pattern+b),cyc_render_chr((uint16_t)(pattern+b)));
                for(int c=0;c<4;c++)p->palette[c]=R(0x3E0+(o[2]&3)*4+c)&63;
            }
            for(int i=0;i<address_count;i++)result[i]=R(addresses[i]);
        }
        C.in_guest=C.drawing=0;cyc_mod_isolate_end();
    }
    if(ok){for(int i=0;i<address_count;i++)W(addresses[i],result[i]);capture(&C.players[seat]);}
    else {f->mask=0;C.failures++;}
    C.current=(uint8_t)previous;project(&C.players[previous]);
}
void mm1_coop_views_draw(void) {
    C.view_drawing=C.view_ready=0;if(!mm1_coop_independent_scene())return;
    CycModRegs r={0};cyc_mod_regs(&r);
    unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
    capture(&C.players[C.current]);
    for(int seat=0;seat<2;seat++)draw_view_player(seat,seat!=C.anchor&&ret!=0x91C1);
    C.view_drawing=C.view_ready=1;
}
static void prepare_guest_picture(int freeze) {
    int owner=C.current,guest=C.anchor^1;if((C.players[guest].dead&&!C.players[guest].blast_ticks)||C.players[guest].teleport==TELE_HIDDEN){C.building.mask=0;C.building_guest_owner=(uint8_t)guest;return;}
    capture(&C.players[owner]);C.current=(uint8_t)guest;project(&C.players[guest]);
    uint8_t life[16];if(freeze)for(int slot=0;slot<16;slot++)if(slot!=1){life[slot]=R(0x6A0+slot);W(0x6A0+slot,life[slot]+1);}
    CycModRegs r={0};cyc_mod_regs(&r);if(!private_call(0xD131,&r,1,1))C.failures++;
    if(freeze)for(int slot=0;slot<16;slot++)if(slot!=1)W(0x6A0+slot,life[slot]);capture(&C.players[guest]);
    C.current=(uint8_t)owner;project(&C.players[owner]);
}
static void queue_sound(int sound){int n=R(0x45);if(n<16){W(0x580+n,sound);W(0x45,n+1);}}
static void clear_combat(void) {
    mm1_charge_stage_enter();mm1_slide_stage_enter();
    for(int slot=2;slot<16;slot++)W(0x600+slot,248);
    for(int a=0x5A0;a<0x600;a++)W(a,0);
    W(0x60,0);W(0x61,0);W(0x640,0);W(0xAD,0);W(0x14,0);W(0x16,0);W(0x18,0);
}
static void finish_wait(void) {
    for(int seat=0;seat<2;seat++)if(C.waiting&(1u<<seat)) {
        personal(&C.players[seat],0x600,C.wait_y[seat]);personal(&C.players[seat],0x420,C.wait_flags[seat]);
        if(seat==C.current){W(0x600,C.wait_y[seat]);W(0x420,C.wait_flags[seat]);}
    }
    C.waiting=0;
}
static int wait_for_partner(int seat,int direction) {
    int other=seat^1,screen=R(0x460);
    if(unavailable(other)){finish_wait();return 0;}
    if(C.waiting&(1u<<other)) {
        if(C.wait_direction[other]==direction&&C.wait_screen[other]==screen){finish_wait();return 0;}
        /* Keep an opposite exit inside the same stripe so it can turn back. */
        int x=screen*256+R(0x480);
        if(direction==2)x=(R(0x28)+1)*256-17;else if(direction==4)x=R(0x27)*256+16;
        W(0x460,x>>8);W(0x480,x);W(0x4A0,0);W(0x26,0);
        if(direction==1||direction==3){W(0x600,direction==1?24:216);W(0x420,R(0x420)&64);W(0x30,0);W(0x680,0);W(0x660,0);}
        return 1;
    }
    if(direction==2||direction==4) {
        int edge=direction==2?232:24,x=mm1_coop_read(other,0x480);
        if(!mm1_coop_seat_blocked(other)&&mm1_coop_read(other,0x460)==screen&&(direction==2?x>=edge:x<=edge))return 0;
        W(0x480,edge);W(0x4A0,0);return 1;
    }
    C.wait_direction[seat]=(uint8_t)direction;C.wait_screen[seat]=(uint8_t)screen;
    C.wait_camera[seat]=(uint16_t)(R(0x1A)+R(0x1B)*256);C.wait_y[seat]=R(0x600);C.wait_flags[seat]=R(0x420);
    clear_combat();W(0x600,248);W(0x420,R(0x420)&64);capture(&C.players[seat]);
    C.waiting|=(uint8_t)(1u<<seat);C.select_hold[seat]=C.select_armed[seat]=0;return 1;
}
static int room_gate(uint16_t address) {
    (void)address;if(!enabled||!wait_both||!C.initialized||C.wiping||cyc_mod_isolated())return 0;
    int direction=R(0x26),camera=R(0x1A)+R(0x1B)*256;
    if(!mm1_world_exit_allowed(direction,camera,R(0x420))||!wait_for_partner(C.current,direction))return 0;
    /* This is a native branch target, with no JSR caller. Resume its real
       end-of-frame path through one balanced RTS rather than returning out. */
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s<2){C.failures++;return 0;}
    unsigned stack=r.s;W(0x100|stack,0x91);stack=(stack-1)&255;W(0x100|stack,0xC8);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);W(0x26,0);return 1;
}
static int waiting_survivor(void) {
    for(int seat=0;seat<2;seat++)if((C.waiting&(1u<<seat))&&unavailable(seat^1)) {
        int direction=C.wait_direction[seat],camera=C.wait_camera[seat];
        if(!mm1_world_rebase(camera)){C.failures++;return 0;}
        finish_wait();
        if(C.anchor!=seat){C.anchor=(uint8_t)seat;C.handoffs++;}
        C.current=C.anchor;project(&C.players[seat]);C.skip_controller=1;W(0x26,direction);return 1;
    }return 0;
}
static void follow_partner(void) {
    if(!C.room_follow||C.current!=C.anchor)return;
    /* The real scroll owns the entry position, ladder and animation. Keep
       each follower's health, equipment, ammo and colors in its own domain. */
    static const uint16_t fields[]={0x2D,0x2E,0x30,0x94,0x95,0x98,0x99,0x9B,
        0x400,0x420,0x440,0x460,0x480,0x4A0,0x4C0,0x4E0,0x600,0x620,0x640,0x660,0x680,0x6A0};
    for(int seat=0;seat<2;seat++)if(C.room_follow&(1u<<seat)) {
        Player *p=&C.players[seat];if(p->dead||p->teleport||(C.withdrawn&(1u<<seat)))continue;
        for(int i=0;i<sizeof fields/sizeof *fields;i++)personal(p,fields[i],R(fields[i]));
    }
}
static int room_begin(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||C.wiping||cyc_mod_isolated())return 0;
    int other=C.anchor^1;Player *p=&C.players[other];if(p->dead||p->teleport||(C.withdrawn&(1u<<other)))return 0;
    capture(&C.players[C.anchor]);C.current=(uint8_t)other;project(p);
    mm1_charge_stage_enter();mm1_slide_stage_enter();clear_combat();capture(p);
    C.current=C.anchor;project(&C.players[C.anchor]);
    C.room_follow|=(uint8_t)(1u<<other);follow_partner();return 0;
}
static int controller_completed(uint16_t address) {
    (void)address;if(!enabled||!C.skip_controller||cyc_mod_isolated())return 0;
    C.skip_controller=0;CycModRegs r={0};cyc_mod_regs(&r);if(r.s<2){C.failures++;return 0;}
    /* The initiating guest's original controller already ran. Enter its
       caller's real918D continuation through an RTS, with no extra frame. */
    unsigned stack=r.s;W(0x100|stack,0x91);stack=(stack-1)&255;W(0x100|stack,0x8C);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);return 1;
}
static int pit_continuation(uint16_t address) {
    (void)address;if(cyc_mod_isolated()||!mm1_coop_personal_death())return 0;
    /*9251 jumps to the nonreturning retail death script from the main loop,
      unlike an ordinary contact's nested call. Personal death needs the
      real end-of-frame continuation beneath the intercepted death RTS. */
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s<2){C.failures++;return 0;}
    unsigned stack=r.s;W(0x100|stack,0x91);stack=(stack-1)&255;W(0x100|stack,0xC8);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);W(0x26,0);return 0;
}
static int private_scroll(uint16_t address) {
    (void)address;
    /* The guest controller owns native movement/collision, but its temporary
       camera must not stream a second terrain ring into the common PPU.
       Native Guts/Elec mutation packets remain on their original paths. */
    return enabled&&C.in_guest&&!C.drawing&&cyc_mod_isolated();
}
static int living_visible(int seat) {
    Player *p=&C.players[seat];if(p->dead||p->teleport||(C.withdrawn&(1u<<seat))||!mm1_coop_read(seat,0x6A))return 0;
    int x=mm1_coop_read(seat,0x460)*256+mm1_coop_read(seat,0x480),y=mm1_coop_read(seat,0x600),left=R(0x1B)*256+R(0x1A);
    return y>=16&&y<=222&&x+12>left&&x-12<=left+255;
}
static int safe_position(int peer,int *x,int *y) {
    if(!living_visible(peer)||mm1_coop_read(peer,0x420)&31||mm1_coop_read(peer,0x95)||mm1_coop_read(peer,0xAD))return 0;
    int type=mm1_coop_read(peer,0x400),px=mm1_coop_read(peer,0x460)*256+mm1_coop_read(peer,0x480),py=mm1_coop_read(peer,0x600);
    /* Native standing keeps its gravity velocity; the actual floor probes
     * below establish support instead of requiring a zero velocity value. */
    if(!(type==0||type==3||type==6||type==12||type==15))return 0;
    static const int offsets[]={24,-24,0,8,-8,16,-16,32,-32};
    for(int i=0;i<9;i++) {
        int cx=px+offsets[i],ok=1;if(cx<R(0x27)*256+8||cx>(R(0x28)+1)*256-9)continue;
        for(int dx=-7;dx<=7;dx+=7)for(int dy=-12;dy<=9;dy+=dy==0?9:12) {
            int c=mm1_world_probe(cx+dx,py+dy);if(c!=0&&c!=2)ok=0;
        }
        for(int dx=-6;dx<=6;dx+=12){int c=mm1_world_probe(cx+dx,py+12);if(c!=1&&c!=4&&c!=0x81)ok=0;}
        if(ok){*x=cx;*y=py;return 1;}
    }return 0;
}
static void departure(int seat) {
    Player *p=&C.players[seat];C.current=(uint8_t)seat;project(p);clear_combat();
    p->teleport=TELE_OUT;p->teleport_tick=0;p->beam_y=R(0x600);p->beam_floor=p->beam_y;
    W(0x400,20);W(0x440,49);W(0x420,(R(0x420)&64)|1);W(0x55,2);W(0x4C0,0);W(0x4E0,0);W(0x680,0);W(0x660,0);
    queue_sound(0x20);capture(p);C.withdrawn|=(uint8_t)(1u<<seat);C.withdrawals[seat]++;
    C.select_hold[seat]=0;C.select_armed[seat]=0;
}
static void arrival(int seat,int x,int y,int heal) {
    Player *p=&C.players[seat];C.current=(uint8_t)seat;project(p);int hp=R(0x6A),weapon=R(0x5F),face=R(0x420)&64;
    clear_combat();if(heal)weapon=0;
    W(0x6A,heal?28:hp);W(0x5F,weapon);W(0x3E1,R(0xD485+weapon*2));W(0x3E2,R(0xD486+weapon*2));
    W(0x460,x>>8);W(0x480,x);W(0x4A0,0);W(0x620,0);W(0x4C0,0);W(0x4E0,0);W(0x680,0);W(0x660,0);
    W(0x400,20);W(0x440,1);W(0x420,face|1);W(0x55,2);W(0x95,0);W(0x30,0);W(0x9B,0);W(0x9C,0);W(0x9D,0);
    p->dead=p->blast_ticks=0;p->teleport=TELE_IN;p->beam_floor=(int16_t)y;p->beam_y=(int16_t)(y&15);p->teleport_tick=0;
    W(0x600,p->beam_y);p->previous=p->held&~12;capture(p);C.select_armed[seat]=0;C.select_hold[seat]=0;
    if(heal){C.revivals[seat]++;C.revive_pending&=(uint8_t)~(1u<<seat);}
}
static void teleport_step(int seat) {
    Player *p=&C.players[seat];if(!p->teleport||p->teleport==TELE_HIDDEN)return;
    C.current=(uint8_t)seat;project(p);p->transport_dispatched=1;W(0x55,2);W(0x14,0);W(0x16,0);W(0x18,0);
    if(p->teleport==TELE_OUT) {
        int tick=p->teleport_tick++;if(tick<9)W(0x440,(3-tick/3)*16+1);
        else {p->beam_y-=8;W(0x440,1);if(p->beam_y<0){p->teleport=TELE_HIDDEN;W(0x600,248);C.building.mask=0;C.building_guest_owner=(uint8_t)seat;}else W(0x600,p->beam_y);}
    } else if(p->teleport==TELE_IN) {
        p->beam_y+=16;if(p->beam_y>=p->beam_floor){p->beam_y=p->beam_floor;p->teleport=TELE_OPEN;queue_sound(0x20);}
        W(0x600,p->beam_y);W(0x440,1);
    } else {
        CycModRegs r={0};cyc_mod_regs(&r);if(!private_call(0x94A8,&r,0,1))C.failures++;
        if(!(R(0x420)&15)&&R(0x400)!=20) {
            p->teleport=TELE_NONE;W(0x55,60);p->previous=p->held&~12;
            if(C.withdrawn&(1u<<seat)){C.withdrawn&=(uint8_t)~(1u<<seat);C.rejoins[seat]++;}
            C.select_hold[0]=C.select_hold[1]=0;C.select_armed[seat]=0;
        }
    }capture(p);
}
static void lifecycle_tick(void) {
    int original=C.anchor;CycModRegs regs={0};cyc_mod_regs(&regs);
    for(int seat=0;seat<2;seat++){C.players[seat].transport_dispatched=0;teleport_step(seat);}
    C.current=C.anchor;project(&C.players[C.anchor]);
    int gameplay=!R(0x1E)&&!R(0x26)&&!R(0x41)&&!R(0x95)&&!mm1_controls_menu()&&!mm1_coop_controls_paused();
    for(int seat=0;seat<2;seat++) {
        Player *p=&C.players[seat];int held=(p->held&4)!=0,fresh=held&&!C.select_previous[seat];C.select_previous[seat]=(uint8_t)held;
        if(!gameplay||p->dead||(p->teleport&&p->teleport!=TELE_HIDDEN)){C.select_hold[seat]=0;continue;}
        if(!held){C.select_armed[seat]=1;C.select_hold[seat]=0;}
        if(!living_visible(seat^1)){C.select_hold[seat]=0;continue;}
        if(C.withdrawn&(1u<<seat)) {
            int x,y;if(fresh&&C.select_armed[seat]&&safe_position(seat^1,&x,&y))arrival(seat,x,y,0);
        } else if(held&&C.select_armed[seat]&&!(mm1_coop_read(seat,0x420)&31)&&!mm1_coop_read(seat,0x95)) {
            if(++C.select_hold[seat]>=90) {
                C.current=(uint8_t)seat;project(p);
                if(mm1_slide_cancel_for_switch()){departure(seat);if(seat==C.anchor){C.anchor^=1;C.handoffs++;}}
                else C.select_hold[seat]=0;
            }
        } else C.select_hold[seat]=0;
        C.current=C.anchor;project(&C.players[C.anchor]);
    }
    if(gameplay&&C.revive_pending)for(int seat=0;seat<2;seat++)if((C.revive_pending&(1u<<seat))&&C.players[seat].dead&&!(C.withdrawn&(1u<<seat))) {
        int x,y;if(safe_position(seat^1,&x,&y))arrival(seat,x,y,1);
    }
    C.current=C.anchor;project(&C.players[C.anchor]);
    if(original!=C.anchor) {
        Player *p=&C.players[C.anchor];int held=p->held&~12;W(0x14,held);W(0x16,p->previous);W(0x18,held&~p->previous);p->previous=(uint8_t)held;
        C.building.mask=0;prepare_guest_picture(0);
    }
    cyc_mod_set_regs(&regs);
}
static int actor_field(int i,int slot){return (i<8?0x400+i*32:0x600+(i-8)*32)+slot;}
static void particle_step(Player *p) {
    if(!p->blast_ticks)return;int age=172-p->blast_ticks;
    if(age==0||age==4||age==8) {
        int slot=2;while(slot<16&&R(0x600+slot)!=248)slot++;
        if(slot<16&&cyc_mod_isolate_begin()) {
            restore_death_body(p);W(0x2F,0);CycModRegs r={0};cyc_mod_regs(&r);r.a=248;r.x=(uint8_t)slot;
            uint8_t actor[16];int ok=cyc_mod_call(0xF67B,&r);
            if(ok) {
                int index=3-age/4,x=p->death_screen*256+p->death_x+(int8_t)R(0xC30D+index);
                W(0x460+slot,x>>8);W(0x480+slot,x);W(0x600+slot,p->death_y+(int8_t)R(0xC313+index));
                W(0x400+slot,0x7D);W(0x420+slot,8);W(0x440+slot,16);r.y=8;ok=cyc_mod_call(0xF533,&r);
            }
            if(ok)for(int i=0;i<16;i++)actor[i]=R(actor_field(i,slot));cyc_mod_isolate_end();
            if(ok)for(int i=0;i<16;i++)W(actor_field(i,slot),actor[i]);else C.failures++;
        }
    } else if(age==12) {
        /* The native ring entry contains no wait. Its own initializers and
         * speed tables fill the bounded personal pool exactly as usual. */
        int old59=R(0x59),old5A=R(0x5A);restore_death_body(p);W(0x59,0);W(0x5A,0x7D);
        CycModRegs r={0};cyc_mod_regs(&r);if(!private_call(0xC37C,&r,1,0))C.failures++;
        W(0x59,old59);W(0x5A,old5A);hide_dead_body();
    }
    if(!--p->blast_ticks) {
        for(int slot=2;slot<16;slot++)W(0x600+slot,248);
        C.building.mask=0;C.building_guest_owner=C.current;
    }
}
void mm1_coop_intro_draw(void) {
    /* The native transform branch skips98EA, hence the ordinary private
     * draw there. Continue the partner's exact opening frames on that same
     * main draw rather than retaining its last arrival beam. */
    if(enabled&&C.initialized&&!C.wiping&&!C.drawing&&!cyc_mod_isolated()&&C.current==C.anchor&&
       !C.world.phase&&R(0x54)==32&&R(0x400)==20&&(R(0x420)&15)==1){prepare_guest_picture(0);return;}
    if(!enabled||C.initialized||C.wiping||C.drawing||cyc_mod_isolated()||R(0x31)>=10||R(0x54)!=32||R(0x400)!=20)return;
    uint8_t original[2048];for(int i=0;i<2048;i++)original[i]=R(i);
    CycModRegs regs={0};cyc_mod_regs(&regs);int previous=C.current;C.current=1;
    int x=R(0x460)*256+R(0x480)-24,left=R(0x1B)*256;if(x<left+16)x=left+16;
    W(0x460,x>>8);W(0x480,x);CycModRegs call=regs;
    if(!private_call(0xD131,&call,1,1))C.failures++;
    for(int i=0;i<2048;i++)if(R(i)!=original[i])W(i,original[i]);C.current=(uint8_t)previous;cyc_mod_set_regs(&regs);
    C.intro_picture=1;
}
static int draw_part(uint16_t address) {
    (void)address;int actor=R(0x0C);if(actor==1||actor>=16||(!C.drawing&&actor))return 0;
    CycModRegs r={0};cyc_mod_regs(&r);int slot=r.x/4+1;if(slot>=64)return 0;
    uint8_t *oam=C.drawing?C.draw_oam:C.primary_oam;
    if(C.drawing)C.draw_mask|=1ull<<slot;else C.primary_build_mask|=1ull<<slot;
    for(int i=0;i<4;i++)oam[slot*4+i]=R(0x200+slot*4+i);
    if(C.drawing)private_actor[slot]=(uint8_t)actor;
    if(C.drawing==2)private_x[slot]=(int16_t)(R(0x460+actor)*256+R(0x480+actor)-private_camera+(int8_t)(R(0x203+slot*4)-128));
    return 0;
}
int mm1_coop_dispatch(void) {
    if(!enabled||C.wiping||C.in_guest||cyc_mod_isolated()||mm1_reflect_busy())return 0;
    if(C.room_follow&&!R(0x26)&&!R(0x1E)&&!R(0x68)) {
        follow_partner();for(int seat=0;seat<2;seat++)if(C.room_follow&(1u<<seat))C.players[seat].previous=C.players[seat].held;
        C.room_follow=0;
    }
    if(!mm1_controls_gameplay()&&(!C.initialized||R(0x31)>=10||R(0x54)!=32||R(0x1E)||R(0x26)||R(0x41)||mm1_controls_menu()))return 0;
    if(!C.initialized||C.stage!=R(0x31))initialize();
    for(int i=0;i<test_poke_count;i++)if(!(C.test_applied&(1u<<i))&&cyc_ring_frame>=test_pokes[i].frame) {
        int seat=test_pokes[i].seat,address=test_pokes[i].address,value=test_pokes[i].value;
        if(seat==C.current)W(address,value);personal(&C.players[seat],address,value);C.test_applied|=1u<<i;
    }
    if(!mm1_world_prepare()){C.failures++;return 0;}
    capture(&C.players[C.anchor]);if(waiting_survivor())return 0;lifecycle_tick();int guest=C.anchor^1;
    if(C.players[guest].dead||C.players[guest].teleport||C.players[guest].transport_dispatched||(C.waiting&(1u<<guest)))return 0;
    uint8_t original[2048],after[2048];for(int i=0;i<2048;i++)original[i]=R(i);
    CycModRegs regs={0},original_regs={0};cyc_mod_regs(&original_regs);regs=original_regs;
    C.current=(uint8_t)guest;Player *p=&C.players[guest];project(p);
    uint8_t held=(uint8_t)(p->held&~12);W(0x14,held);W(0x16,p->previous);W(0x18,held&~p->previous);p->previous=held;
    mm1_weapons_player_dispatch();mm1_charge_dispatch();
    int ok=private_call(0x94A8,&regs,0,1);
    int lead=ok&&!p->dead&&private_exit;
    if(ok&&!p->dead){mm1_slide_after_run();mm1_slide_restore_input();if(!lead)ok=private_call(0xA35C,&regs,0,1);}
    if(p->dead)enforce_dead();
    if(ok){capture(p);C.updates++;}else C.failures++;
    for(int i=0;i<2048;i++)after[i]=R(i);
    for(int i=0;i<2048;i++)if(R(i)!=original[i])W(i,original[i]);
    if(ok)for(int i=0;i<WEAPON_WORLD_BYTES;i++){unsigned a=weapon_world_address(i);if(after[a]!=original[a])W(a,after[a]);}
    if(ok&&after[0x45]>=original[0x45]&&after[0x45]<=16) {
        for(int i=original[0x45];i<after[0x45];i++)W(0x580+i,after[0x580+i]);W(0x45,after[0x45]);
    }
    C.current=C.anchor;cyc_mod_set_regs(&original_regs);
    if(C.pending_wipe)return begin_team_death();
    if(lead&&ok) {
        if(!mm1_world_rebase(private_exit_camera)){C.failures++;return 0;}
        C.current=C.anchor=(uint8_t)guest;project(&C.players[guest]);C.skip_controller=1;C.handoffs++;W(0x26,private_exit);
    }
    return 0;
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
        if(mm1_coop_seat_blocked(C.current))return 1;
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
int mm1_coop_nearest(int ex,int ey) {
    if(!enabled||!C.initialized)return C.anchor;
    int chosen=-1,best=0x7FFFFFFF;
    for(int seat=0;seat<2;seat++) {
        if(mm1_coop_seat_blocked(seat)||!mm1_coop_read(seat,0x6A))continue;
        int x=mm1_coop_read(seat,0x460)*256+mm1_coop_read(seat,0x480),y=mm1_coop_read(seat,0x600);
        int distance=abs(ex-x)+abs(ey-y);if(distance<best){best=distance;chosen=seat;}
    }return chosen;
}
static int enemy_owner(int slot) {
    World *w=&C.world;
    if((w->pending&(1u<<slot))&&(R(0x420+slot)&128))return w->hit_owner[slot];
    return mm1_coop_nearest(R(0x460+slot)*256+R(0x480+slot),R(0x600+slot));
}
static int collision_local(uint16_t address) {
    if(!enabled||!C.initialized||C.wiping||C.stage!=R(0x31)||R(0x31)>=10||cyc_mod_isolated())return 0;
    int subject=address==0xC9BE?0:R(0x2F);if(subject>=16)return 0;
    int camera=R(0x1B)*256+R(0x1A),x=R(0x460+subject)*256+R(0x480+subject);
    if(x>=camera&&x<camera+256)return 0;
    /* C9BE/C9C2 is a bounded native overlap test. Its caller already built
       the subject's 00/01 interval relative to the common camera. Translate
       that interval and the camera together, then retain the native result
       and documented scratch only. No devices, actors or caller stack leave
       the isolated test; real damage/refill/death continues in its caller. */
    int local=x-128;if(local<0)local=0;
    CycModRegs original={0},r={0};cyc_mod_regs(&original);r=original;
    uint8_t scratch[3];int ok=cyc_mod_isolate_begin();if(!ok){C.failures++;return 0;}
    W(0,camera-local+R(0));W(1,camera-local+R(1));W(0x1A,local);W(0x1B,local>>8);
    ok=cyc_mod_call(address,&r);if(ok)for(int i=0;i<3;i++)scratch[i]=R(0x0C+i);
    cyc_mod_isolate_end();if(!ok){C.failures++;return 0;}
    for(int i=0;i<3;i++)W(0x0C+i,scratch[i]);r.s=original.s;cyc_mod_set_regs(&r);return 1;
}
static int world_step(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||C.wiping||cyc_mod_isolated())return 0;
    World *w=&C.world;int slot=R(0x2F);CycModRegs r={0};cyc_mod_regs(&r);
    if(w->phase)return 0;
    unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
    if(ret!=0x91BB||r.s<8||R(0x54)!=32)return 0;
    if(slot==2){w->done=0;if(C.players[C.anchor].dead)particle_step(&C.players[C.anchor]);}
    if(slot==16&&!w->done&&!w->ai_active) {
        w->done=1;int guest=C.anchor^1;
        if((C.players[guest].dead&&!C.players[guest].blast_ticks)||C.players[guest].teleport==TELE_HIDDEN||(C.waiting&(1u<<guest)))return 0;
        capture(&C.players[C.anchor]);w->regs=r;w->total=R(0x54);w->reference=R(0x2F);w->lift_count=R(0x9A);
        for(int i=0;i<14;i++)w->scratch[i]=R(i);
        C.current=(uint8_t)guest;project(&C.players[guest]);if(R(0x55))W(0x55,R(0x55)-1);
        if(C.players[guest].dead){particle_step(&C.players[guest]);w->phase=3;W(0x54,16);return native_target(0x98EA);}
        if(C.players[guest].teleport||C.players[guest].transport_dispatched){w->phase=3;W(0x54,16);return native_target(0x98EA);}
        w->phase=1;return native_target(0xA18B);
    }
    /* One native AI update uses the nearest living player's position. Pending
       damage retains its actual attacker's equipment/charge identity first.
       Close only when this exact native actor/depth reaches NextObject. */
    if(slot>=16&&slot<32&&R(0x600+slot)!=248&&!w->ai_active) {
        int owner=enemy_owner(slot);if(owner>=0&&owner!=C.anchor) {
            capture(&C.players[C.anchor]);w->ai_active=1;w->ai_slot=(uint8_t)slot;w->ai_stack=r.s;
            C.current=(uint8_t)owner;project(&C.players[owner]);
        }
    }return 0;
}
static int world_next(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||cyc_mod_isolated())return 0;
    World *w=&C.world;CycModRegs r={0};cyc_mod_regs(&r);
    if(w->ai_active&&r.s==w->ai_stack&&R(0x2F)==w->ai_slot) {
        capture(&C.players[C.current]);w->ai_active=0;C.current=C.anchor;project(&C.players[C.anchor]);
    }
    unsigned ret=R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8);
    if(R(0x2F)==31&&ret==0x91BB&&!C.wiping&&!R(0x1E)&&!R(0x26)&&R(0x31)<10) {
        for(int seat=0;seat<2;seat++)if(!C.players[seat].dead&&!C.players[seat].teleport) {
            int screen=mm1_coop_read(seat,0x460),y=mm1_coop_read(seat,0x600);
            if(screen<R(0x27)||screen>R(0x28)||y<16||y>222)continue;
            int at=screen>=R(0xC2E0+R(0x31))?2:screen>=R(0xC2D4+R(0x31))?1:0;
            if(at>C.checkpoint){C.checkpoint=(uint8_t)at;for(int other=0;other<2;other++)if(C.players[other].dead)C.revive_pending|=(uint8_t)(1u<<other);}
        }
    }return 0;
}
static int magnet_ride(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||C.wiping||C.world.phase||C.world.ai_active||
        mm1_coop_seat_blocked(C.current)||(cyc_mod_isolated()&&!C.in_guest))return 0;
    /* Retail 9634 tests only the rider's own beam when firing C7. Apply its
       exact 9691 landing contract to both personal pools, keeping native lifts
       on their original path when no beam is crossed. */
    int before=(R(0x600)+14)&255,after=(before-R(0x680)-(R(0x620)<R(0x660)))&255;
    int x=R(0x460)*256+R(0x480);
    for(int pass=0;pass<2;pass++) {
        int owner=C.current^pass;if(mm1_coop_seat_blocked(owner))continue;
        for(int slot=0;slot<16;slot++) {
            if(!mm1_coop_read(owner,(uint16_t)(0x5B0+slot)))continue;
            int screen=mm1_coop_read(owner,(uint16_t)(0x5D0+slot)),bx=mm1_coop_read(owner,(uint16_t)(0x5C0+slot));
            int y=mm1_coop_read(owner,(uint16_t)(0x5F0+slot));
            int width=((mm1_coop_read(owner,(uint16_t)(0x5A0+slot))&124)*2+16)&255;
            int distance=abs(x-screen*256-bx);
            if(before>=y||after<y||distance>width)continue;
            W(0x04,distance);W(0x06,bx);W(0x07,screen);W(0x08,y);W(0x09,width);
            CycModRegs r={0};cyc_mod_regs(&r);r.a=(uint8_t)(y-16);r.x=(uint8_t)slot;
            r.p=(uint8_t)((r.p&~0xC3)|1|(r.a&128)|(r.a?0:2)|(((y^16)&(y^r.a)&128)?64:0));
            cyc_mod_set_regs(&r);return 1;
        }
    }return 0;
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
    if(enabled&&C.initialized&&!C.drawing&&!cyc_mod_isolated()&&C.current==C.anchor)follow_partner();
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
    if(w->phase==1&&C.players[C.current].dead&&C.players[C.current].blast_ticks) {
        particle_step(&C.players[C.current]);w->phase=3;W(0x54,16);return native_target(0x98EA);
    }
    if(w->phase==1&&!C.players[C.current].dead) {w->before_hits=hit_flags();w->phase=2;return native_target(0xA277);}
    if(w->phase==2) {
        mm1_coop_record_hits(C.current,w->before_hits,hit_flags());w->phase=3;W(0x54,16);return native_target(0x98EA);
    }
    W(0x54,w->total);W(0x9A,w->lift_count);
    if(!mm1_coop_independent_scene()&&(!C.players[C.current].dead||C.players[C.current].blast_ticks)) {
        CycModRegs draw=r;if(!private_call(0xD131,&draw,1,1))C.failures++;
    }
    capture(&C.players[C.current]);
    if(mm1_coop_seat_blocked(C.anchor)&&!mm1_coop_seat_blocked(C.current)) {
        C.anchor=C.current;C.handoffs++;C.building.mask=0;C.building_guest_owner=C.anchor^1;
        prepare_guest_picture(0);
    }
    C.current=C.anchor;project(&C.players[C.anchor]);
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
        w->phase=w->ai_active=w->draw_active=0;project(&C.players[C.anchor]);C.building.mask=0;
    }
    prepare_guest_picture(1);return 0;
}
void mm1_coop_stage_enter(void) {
    mm1_coop_menu_cancel();
    mm1_world_cancel_rebase();C.skip_controller=0;
    C.restarting=(uint8_t)(C.wiping&&!C.game_over);C.initialized=0;C.wiping=C.pending_wipe=0;
    if(enabled)for(int seat=0;seat<2;seat++){C.current=(uint8_t)seat;mm1_charge_stage_enter();mm1_slide_stage_enter();}
    C.current=C.anchor=C.build_owner=C.shown_owner=0;C.building_guest_owner=C.shown_guest_owner=1;C.intro_picture=0;
    memset(&C.world,0,sizeof C.world);C.building.mask=C.shown.mask=0;C.pair_ready=0;
    C.view_ready=C.view_drawing=C.view_visible=C.room_follow=C.waiting=0;memset(C.view_build,0,sizeof C.view_build);memset(C.view_shown,0,sizeof C.view_shown);
}
void mm1_coop_video_latch(void) {
    if(!enabled)return;
    /* A lag NMI before the main native draw must retain both old bodies.
     * Publish a private draw only once the corresponding main draw began. */
    if(C.pair_ready&&C.view_ready&&mm1_coop_independent_scene()) {
        for(int seat=0;seat<2;seat++) {
            C.view_shown[seat]=C.view_build[seat];C.view_shown[seat].origin=(int16_t)(C.view_shown[seat].camera-(R(0x1B)*256+R(0x1A)));
            mm1_video_private_latch(seat);
        }
        C.view_visible=C.primary_seen;C.view_ready=0;
    }
    if(C.pair_ready){C.shown=C.building;C.shown_guest_owner=C.building_guest_owner;
        if(C.shown_guest_owner==C.shown_owner)C.shown.mask=0;
        if(C.shown.mask)mm1_video_private_latch(C.shown_guest_owner);C.pair_ready=0;}
    C.shown.origin=(int16_t)(C.shown.camera-(R(0x1B)*256+R(0x1A)));
    C.insert_slot=C.primary_cursor;if(!C.primary_seen)C.shown.mask=0;
    for(int slot=0;slot<64;slot++)if(C.primary_build_mask&(1ull<<slot)) {
        int match=1;for(int i=0;i<4;i++)if(C.primary_oam[slot*4+i]!=R(0x200+slot*4+i))match=0;
        if(match)C.insert_slot=(int16_t)slot;
    }
    for(int i=0;i<256;i++)C.oam[i]=R(0x200+i);
}
int mm1_coop_picture(void) {
    int mask=mm1_coop_independent_scene()?C.view_visible&&(C.view_shown[0].mask||C.view_shown[1].mask):C.shown.mask!=0;
    return enabled&&(C.initialized||C.intro_picture)&&!C.wiping&&mask&&!mm1_controls_menu()&&
        mm1_proto_atlas()&&!memcmp(C.oam,cyc_render_oam(),256);
}
static void paint_player(uint32_t *out,int width,int x0,const uint8_t *coverage,Picture *f,int owner,int actor) {
    int previous=C.current;C.current=(uint8_t)owner;
    int offset=x0+f->origin;if(actor<=0)mm1_video_private_dust(out,width,offset,coverage);
    for(int slot=63;slot>=0;slot--)if(f->mask&(1ull<<slot)) {
        if(actor>=0&&f->parts[slot].actor!=actor)continue;
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
    if(actor<=0)mm1_video_private_after(out,width,offset,coverage);C.current=(uint8_t)previous;
}
void mm1_coop_paint(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    if(!mm1_coop_picture())return;
    if(mm1_coop_independent_scene()) {
        paint_player(out,width,x0,coverage,&C.view_shown[C.shown_owner^1],C.shown_owner^1,-1);
        paint_player(out,width,x0,coverage,&C.view_shown[C.shown_owner],C.shown_owner,-1);
    }else paint_player(out,width,x0,coverage,&C.shown,C.shown_guest_owner,-1);
}
void mm1_coop_paint_actor(uint32_t *out,int width,int x0,const uint8_t *coverage,int actor) {
    if(!mm1_coop_independent_scene()||!mm1_coop_picture())return;
    paint_player(out,width,x0,coverage,&C.view_shown[C.shown_owner^1],C.shown_owner^1,actor);
    paint_player(out,width,x0,coverage,&C.view_shown[C.shown_owner],C.shown_owner,actor);
}
static int death(uint16_t address) {
    (void)address;if(!enabled||!C.initialized||(cyc_mod_isolated()&&!C.in_guest)||C.wiping)return 0;
    Player *p=&C.players[C.current];int fresh=!p->dead;
    if(fresh) {
        p->death_y=R(0x600);p->death_x=R(0x480);p->death_screen=R(0x460);p->dead=1;
        p->death_sprite=R(0x400);p->death_flags=R(0x420);p->death_anim=R(0x440);p->death_fire=R(0x640);
        C.deaths[C.current]++;mm1_charge_stage_enter();mm1_slide_stage_enter();cyc_mm4_audio_reset();
        p->blast_ticks=172;
    }
    enforce_dead();capture(p);C.building.mask=0;
    if(unavailable(C.current^1)) {
        C.wipe_owner=C.current;
        if(cyc_mod_isolated()){C.pending_wipe=1;return 1;}
        /* This real native entry owns the sole life decrement and retry. */
        if(C.world.phase)W(0x54,C.world.total);
        C.anchor=C.current;memset(&C.world,0,sizeof C.world);C.wiping=1;C.pending_wipe=0;C.wipes++;
        C.players[0].blast_ticks=C.players[1].blast_ticks=0;
        C.building.mask=C.shown.mask=0;C.pair_ready=0;restore_death_body(p);return 0;
    }
    if(fresh){int count=R(0x45);if(count<16){W(0x580+count,0x31);W(0x45,count+1);}}
    return 1;
}
static void enforce_dead(void) {
    hide_dead_body();for(int slot=2;slot<16;slot++)W(0x600+slot,248);
}
static void hide_dead_body(void) {
    /* Transform flag1 bypasses the entire native world loop at91A5. A dead
     * anchor must leave that loop running while its living partner arrives;
     * the controller/contact hooks already protect the hidden body. */
    W(0x6A,0);W(0x600,248);W(0x420,R(0x420)&64);W(0x55,255);W(0x14,0);W(0x16,0);W(0x18,0);W(0xAD,0);
    W(0x60,0);W(0x61,0);W(0x640,0);
}
int mm1_coop_personal_death(void){return enabled&&C.initialized&&!C.wiping&&!unavailable(C.current^1);}
static int begin_team_death(void) {
    C.current=C.wipe_owner;project(&C.players[C.current]);enforce_dead();capture(&C.players[C.current]);
    if(C.world.phase)W(0x54,C.world.total);C.anchor=C.current;memset(&C.world,0,sizeof C.world);
    C.wiping=1;C.pending_wipe=0;C.wipes++;C.building.mask=C.shown.mask=0;C.pair_ready=0;
    C.players[0].blast_ticks=C.players[1].blast_ticks=0;
    restore_death_body(&C.players[C.current]);
    CycModRegs r={0};cyc_mod_regs(&r);unsigned stack=r.s;
    W(0x100|stack,0xC2);stack=(stack-1)&255;W(0x100|stack,0x18);stack=(stack-1)&255;
    r.s=(uint8_t)stack;cyc_mod_set_regs(&r);return 1;
}
static int pre_shoot(uint16_t address) {
    (void)address;if(enabled&&C.initialized&&!C.wiping&&!cyc_mod_isolated()&&C.players[C.current].dead) {
        hide_dead_body();capture(&C.players[C.current]);
    }return 0;
}
static int contact(uint16_t address){(void)address;return mm1_coop_seat_blocked(C.current)!=0;}
static int game_over(uint16_t address) {
    (void)address;if(enabled&&!cyc_mod_isolated()){C.game_over=1;C.initialized=0;C.building.mask=C.shown.mask=0;}return 0;
}
static int checkpoint_restart(uint16_t address) {
    (void)address;if(!enabled||!C.wiping||!C.checkpoint||cyc_mod_isolated())return 0;
    CycModRegs r={0};cyc_mod_regs(&r);int at=R((C.checkpoint==2?0xC2E0:0xC2D4)+R(0x31));
    if(r.a<at){r.a=(uint8_t)at;cyc_mod_set_regs(&r);}return 0;
}
static void set_enabled(int on) {
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on!=0;if(enabled)mm1_proto_coop_enable();
    nes_mod_set_function_hook_enabled("mm1.coop.death",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.room-next",enabled);nes_mod_set_function_hook_enabled("mm1.coop.room-previous",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.room-gate",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.controller-completed",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.pit-continuation",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.private-scroll-left",enabled);nes_mod_set_function_hook_enabled("mm1.coop.private-scroll-right",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.draw-part",enabled);
    for(int i=0;i<sizeof world_hook_ids/sizeof *world_hook_ids;i++)nes_mod_set_function_hook_enabled(world_hook_ids[i],enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.refill",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.pre-shoot",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.game-over",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.checkpoint",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.contact",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.magnet-ride",enabled);
    nes_mod_set_function_hook_enabled("mm1.coop.private-oam-full",mm1_coop_independent());
    nes_mod_set_local_only("Mega Man 1 co-op development",enabled);
    mm1_coop_menu_refresh();
    C.initialized=0;C.current=C.anchor=0;C.view_ready=C.view_drawing=C.view_visible=0;memset(&C.world,0,sizeof C.world);
    mm1_world_views_reset();mm1_controls_refresh();mm1_video_character_changed();mm1_coop_view_policy();
}
static void power_on(void *ctx){(void)ctx;memset(&C,0,sizeof C);domain();mm1_coop_menu_reset();}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;if(!enabled)return;
    for(int i=0;i<2;i++)C.players[i].held=reverse(nes_input_seat(i+1));
    buttons[0]=(C.wiping?(nes_input_seat(1)|nes_input_seat(2)):nes_input_seat(C.anchor+1))&(uint8_t)~0x20;buttons[1]=0;
    mm1_coop_menu_input(buttons);
}
static void frame_end(void *ctx) {
    (void)ctx;if(enabled&&C.initialized)capture(&C.players[C.current]);
    if(report) {
    fprintf(report,"{\"frame\":%llu,\"enabled\":%u,\"ready\":%u,\"updates\":%llu,\"failures\":%llu,\"anchor\":%u,\"owner\":%u,\"phase\":%u,\"ai\":%u,\"refill\":%u,\"x\":[%d,%d],\"y\":[%d,%d],\"hp\":[%d,%d],\"invul\":[%d,%d],\"weapon\":[%d,%d],\"energy\":[%d,%d],\"lives\":%d,\"picture\":%d,\"dead\":[%u,%u],\"wiping\":%u,\"checkpoint\":%u,\"deaths\":[%llu,%llu],\"wipes\":%llu,\"handoffs\":%llu,\"retries\":%llu,\"ammo\":[",
        (unsigned long long)cyc_ring_frame,enabled,C.initialized,(unsigned long long)C.updates,(unsigned long long)C.failures,
        C.anchor,C.current,C.world.phase,C.world.ai_active,C.world.refill_active,
        mm1_coop_read(0,0x460)*256+mm1_coop_read(0,0x480),mm1_coop_read(1,0x460)*256+mm1_coop_read(1,0x480),
        mm1_coop_read(0,0x600),mm1_coop_read(1,0x600),mm1_coop_read(0,0x6A),mm1_coop_read(1,0x6A),
        mm1_coop_read(0,0x55),mm1_coop_read(1,0x55),mm1_coop_read(0,0x5F),mm1_coop_read(1,0x5F),
        mm1_coop_read(0,0x6A+mm1_coop_read(0,0x5F)),mm1_coop_read(1,0x6A+mm1_coop_read(1,0x5F)),R(0xA6),mm1_coop_picture(),
        C.players[0].dead,C.players[1].dead,C.wiping,C.checkpoint,(unsigned long long)C.deaths[0],(unsigned long long)C.deaths[1],
        (unsigned long long)C.wipes,(unsigned long long)C.handoffs,(unsigned long long)C.retries);
    for(int seat=0;seat<2;seat++) {
        fputs(seat?",[":"[",report);for(int a=0x6B;a<=0x71;a++)fprintf(report,"%s%d",a==0x6B?"":",",mm1_coop_read(seat,(uint16_t)a));fputc(']',report);
    }fputs("],\"special\":[",report);
    for(int seat=0;seat<2;seat++) {
        fprintf(report,"%s{\"age\":%u,\"scratch\":[",seat?",":"",mm1_coop_read(seat,0x61));
        for(int a=0x62;a<=0x66;a++)fprintf(report,"%s%u",a==0x62?"":",",mm1_coop_read(seat,(uint16_t)a));
        fputs("],\"actor\":[",report);
        for(int field=0;field<16;field++)fprintf(report,"%s%u",field?",":"",mm1_coop_read(seat,(uint16_t)actor_field(field,5)));
        fputs("]}",report);
    }char menu[512];mm1_coop_menu_debug(menu,sizeof menu);
    fprintf(report,"],\"shown_owner\":%u,\"guest_owner\":%u,\"intro\":%u,\"blast\":[%u,%u],\"withdrawn\":%u,\"teleport\":[%u,%u],\"hold\":[%u,%u],\"withdrawals\":[%llu,%llu],\"rejoins\":[%llu,%llu],\"revivals\":[%llu,%llu],\"wait_both\":%d,\"waiting\":%u,%s}\n",
        C.shown_owner,C.shown_guest_owner,C.intro_picture,C.players[0].blast_ticks,C.players[1].blast_ticks,C.withdrawn,
        C.players[0].teleport,C.players[1].teleport,C.select_hold[0],C.select_hold[1],
        (unsigned long long)C.withdrawals[0],(unsigned long long)C.withdrawals[1],(unsigned long long)C.rejoins[0],(unsigned long long)C.rejoins[1],
        (unsigned long long)C.revivals[0],(unsigned long long)C.revivals[1],wait_both,C.waiting,menu);
    }
}
static int get(uint8_t *out,int cap){if(cap<(int)sizeof C)return -1;memcpy(C.magic,"MCOB",4);C.on=(uint8_t)enabled;C.roster=(uint8_t)roster;C.independent=(uint8_t)mm1_coop_independent();C.wait_mode=(uint8_t)wait_both;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof C)return 0;Coop n;memcpy(&n,in,sizeof n);
    if(memcmp(n.magic,"MCOB",4)||n.on!=enabled||n.roster!=roster||n.independent!=mm1_coop_independent()||n.wait_mode!=wait_both||n.waiting>2||n.initialized>1||n.current>1||n.anchor>1||n.in_guest||n.drawing||n.room_follow>3||n.skip_controller>1||
        n.players[0].dead>1||n.players[1].dead>1||n.primary_seen>1||n.insert_slot< -1||n.insert_slot>=64)return 0;
    if(n.wiping>1||n.restarting>1||n.game_over>1||n.pending_wipe>1||n.wipe_owner>1||n.checkpoint>2)return 0;
    if(n.build_owner>1||n.shown_owner>1||n.building_guest_owner>1||n.shown_guest_owner>1||n.intro_picture>1)return 0;
    if(n.withdrawn>=3||n.revive_pending>3)return 0;
    if(n.waiting&&!wait_both)return 0;
    for(int seat=0;seat<2;seat++)if((n.waiting&(1u<<seat))&&
        ((n.wait_direction[seat]!=1&&n.wait_direction[seat]!=3)||n.wait_screen[seat]>=48||n.wait_camera[seat]>47*256||n.players[seat].dead))return 0;
    for(int seat=0;seat<2;seat++)if(n.players[seat].teleport>TELE_OPEN||n.players[seat].transport_dispatched>1||
        n.select_hold[seat]>90||n.select_armed[seat]>1||n.select_previous[seat]>1||n.players[seat].beam_floor<0||n.players[seat].beam_floor>239||
        n.players[seat].beam_y< -16||n.players[seat].beam_y>248)return 0;
    if(n.world.phase>3||n.world.done>1||n.world.ai_active>1||n.world.draw_active>1||n.world.draw_owner>1||
       n.world.refill_active>1||n.world.draw_freeze>1||n.pair_ready>1||n.view_ready>1||n.view_drawing>1||n.view_visible>1||
       (n.world.phase&&(n.world.total!=32||n.world.regs.s<8))||
       (n.world.ai_active&&(n.world.ai_slot<16||n.world.ai_slot>=32)))return 0;
    for(int i=16;i<32;i++)if(n.world.hit_owner[i]>1)return 0;
    const Picture *frames[]={&n.building,&n.shown,&n.view_build[0],&n.view_build[1],&n.view_shown[0],&n.view_shown[1]};
    for(int f=0;f<6;f++)for(int i=0;i<64;i++)if(frames[f]->mask&(1ull<<i)) {
        const Part *p=&frames[f]->parts[i];if((p->height!=8&&p->height!=16)||p->actor>=16||p->actor==1)return 0;
        for(int c=0;c<4;c++)if(p->palette[c]>63)return 0;
    }return 1;
}
static int put(const uint8_t *in,int len){if(!valid(in,len))return 0;memset(&C,0,sizeof C);if(len)memcpy(&C,in,sizeof C);return 1;}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(!strcmp(name,"--coop-report")){report=fopen(value,"w");return report!=NULL;}
    if(!strcmp(name,"--coop-wait-both")){if(strcmp(value,"on")&&strcmp(value,"off"))return false;wait_both=!strcmp(value,"on");return true;}
    if(!strcmp(name,"--coop-view")) {
        if(strcmp(value,"shared")&&strcmp(value,"split")&&strcmp(value,"p1")&&strcmp(value,"p2"))return false;
        int was_independent=mm1_coop_independent();
        view_mode=!strcmp(value,"split")?1:!strcmp(value,"p1")?2:!strcmp(value,"p2")?3:0;
        nes_mod_set_function_hook_enabled("mm1.coop.private-oam-full",mm1_coop_independent());
        if(was_independent!=mm1_coop_independent()){C.view_ready=C.view_drawing=C.view_visible=0;mm1_world_views_reset();}
        mm1_video_character_changed();mm1_coop_view_policy();return true;
    }
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
static void tcp_view(int id,const char *line) {
    char value[16];if(!cyc_tcp_str(line,"view",value,sizeof value)){cyc_tcp_err(id,"coop_view: view shared|split|p1|p2");return;}
    if(C.initialized&&((strcmp(value,"shared")!=0)!=mm1_coop_independent())){cyc_tcp_err(id,"The running game keeps its shared/separate simulation profile");return;}
    if(!option(NULL,"--coop-view",value)){cyc_tcp_err(id,"coop_view: invalid view");return;}
    char out[64];snprintf(out,sizeof out,"\"view\":%d",view_mode);cyc_tcp_ok(id,out);
}
static void tcp_setup(void *ctx){(void)ctx;cyc_tcp_register("coop_view","local co-op layout: view shared|split|p1|p2",tcp_view);}
const CycHostExtras *mm1_coop_extras(void) {
    static const CycHostOption options[]={{"--coop",true,"on|off: development co-op controller slice"},
        {"--coop-p1",true,"megaman|protoman: complementary character roster"},
        {"--coop-view",true,"shared|split|p1|p2: co-op view"},
        {"--coop-wait-both",true,"on|off: wait for both players at native room links (default off)"},
        {"--coop-test-poke",true,"FRAME:SEAT:ADDRESS:VALUE: bounded personal RAM precondition"},
        {"--coop-report",true,"FILE: personal controller observations"}};
    static const CycHostExtras extras={.power_on=power_on,.input=input,.frame_end=frame_end,.state_loaded=state_loaded,.tcp_setup=tcp_setup,
        .option=option,.options=options,.option_count=6};return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_coop) {
#ifdef MM1_PROFILE_USA
    if(!nes_mod_register_function_entry_plugin("mm1.coop.death",0xC219,death)||
       !nes_mod_register_function_entry_plugin("mm1.coop.draw-part",0xD293,draw_part)||
       !nes_mod_register_savestate_hook("mm1.coop",get,put)||
       !nes_mod_register_savestate_validator("mm1.coop",valid))abort();
    static const uint16_t pcs[]={0x98F2,0x998A,0xC3BE,0xA277,0x91B9,0xCA2D,0xC05A,0xC9BE,0xC9C2};
    NESModFunctionEntryCallback functions[]={world_step,world_next,native_return,weapon_begin,weapon_end,script_owner,script_owner,collision_local,collision_local};
    for(int i=0;i<sizeof pcs/sizeof *pcs;i++)if(!nes_mod_register_function_entry_plugin(world_hook_ids[i],pcs[i],functions[i]))abort();
    if(!nes_mod_register_function_entry_plugin("mm1.coop.refill",0x915E,refill_begin))abort();
    if(!nes_mod_register_function_entry_plugin("mm1.coop.pre-shoot",0x91A7,pre_shoot)||
       !nes_mod_register_function_entry_plugin("mm1.coop.game-over",0x9038,game_over)||
       !nes_mod_register_function_entry_plugin("mm1.coop.checkpoint",0xC26B,checkpoint_restart)||
       !nes_mod_register_function_entry_plugin("mm1.coop.contact",0xA18B,contact)||
       !nes_mod_register_function_entry_plugin("mm1.coop.magnet-ride",0x9634,magnet_ride)||
       !nes_mod_register_function_entry_plugin("mm1.coop.private-oam-full",0xD2A6,private_oam_full)||
       !nes_mod_register_function_entry_plugin("mm1.coop.room-next",0x92A6,room_begin)||
       !nes_mod_register_function_entry_plugin("mm1.coop.room-previous",0x925B,room_begin)||
       !nes_mod_register_function_entry_plugin("mm1.coop.room-gate",0x9207,room_gate)||
       !nes_mod_register_function_entry_plugin("mm1.coop.controller-completed",0x918A,controller_completed)||
       !nes_mod_register_function_entry_plugin("mm1.coop.pit-continuation",0x9251,pit_continuation)||
       !nes_mod_register_function_entry_plugin("mm1.coop.private-scroll-left",0x9F9E,private_scroll)||
       !nes_mod_register_function_entry_plugin("mm1.coop.private-scroll-right",0x9F6D,private_scroll))abort();
#endif
}
