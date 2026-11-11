/* MM4 slide steering around MM1's original movement, collision and camera. */
#include "mm1_slide.h"
#include "mm1_proto.h"
#include "mm1_coop.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_mm4_donor.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {
    char magic[4];uint8_t on,active,timer,right,stamp,hurt,hurt_age,dust,dust_age,dust_right;
    uint16_t before_x,dust_x;uint8_t dust_y,inputs_open,held,fresh,old,pad;
    uint64_t starts,timed,walls,falls,jumps,reversals,blocked,hits;
} Slide;
static Slide slides[2];
#define S (slides[mm1_coop_seat()])
static int enabled,loaded;
static Mm1SlidePose poses[3];
static uint8_t palettes[2][4],dust_tiles[3][16];
static FILE *report;
static const char *hooks[]={"mm1.slide.player","mm1.slide.after","mm1.slide.speed","mm1.slide.right-width",
    "mm1.slide.left-width","mm1.slide.side-probe","mm1.slide.feet","mm1.slide.contact","mm1.slide.player-killed","mm1.slide.stage-clear"};
int mm1_controls_gameplay(void);
int mm1_mm4_load(const char *feature);
void mm1_controls_refresh(void);
int mm1_slide_available(void){return enabled;}
int mm1_slide_active(void){return enabled&&S.active;}
int mm1_slide_pose_index(void){return S.hurt?1+(((S.hurt_age?S.hurt_age-1:0)/3)&1):0;}
const Mm1SlidePose *mm1_slide_pose(int index){return loaded&&index>=0&&index<3?&poses[index]:NULL;}
const uint8_t *mm1_slide_palette(int index){return palettes[index&1];}
const uint8_t *mm1_slide_dust_tile(int age){return dust_tiles[age<9?0:age<18?1:2];}
int mm1_slide_dust(int *x,int *y,int *right,int *age) {
    if(!enabled||!S.dust)return 0;*x=S.dust_x;*y=S.dust_y;*right=S.dust_right;*age=S.dust_age;return 1;
}
static int player_x(void){return R(0x460)*256+R(0x480);}
static uint8_t donor(unsigned address){return cyc_mm4_byte(address<0xA000?0x0C:0x0D,address);}
static unsigned pointer(unsigned low,unsigned high,unsigned index) {
    return cyc_mm4_byte(0x0D,low+index)|(cyc_mm4_byte(0x0D,high+index)<<8);
}
static int load_art(void) {
    memset(poses,0,sizeof poses);
    for(int which=0;which<3;which++) {
        unsigned id=which==0?0x1B:which==1?0x0D:0x0E;
        unsigned p=pointer(0xB800,0xBA00,id),offset=donor(p+1);
        Mm1SlidePose *pose=&poses[which];unsigned count=donor(p)+1u;if(count>12)return 0;pose->count=(uint8_t)count;
        for(unsigned i=0;i<count;i++) {
            Mm1SlidePart *part=&pose->parts[i];unsigned tile=donor(p+2+i*2);part->attr=donor(p+3+i*2);
            for(int b=0;b<16;b++)part->pixels[b]=cyc_mm4_byte(0,0x8000+tile*16+b);
            for(int side=0;side<2;side++) {
                unsigned o=pointer(0xBC00,0xBD00,offset+side);
                part->y[side]=(int8_t)donor(o+i*2);part->x[side]=(int8_t)donor(o+i*2+1);
            }
        }
    }
    static const uint8_t ids[]={0x6C,0x78,0x79};
    for(int t=0;t<3;t++)for(int b=0;b<16;b++)dust_tiles[t][b]=cyc_mm4_byte(0,0x8000+ids[t]*16+b);
    for(int i=0;i<8;i++)palettes[i/4][i%4]=cyc_mm4_byte(0x3E,0xC853+i);
    return poses[0].count==11&&cyc_mm4_byte(0x3C,0x8185)==26&&palettes[0][2]==0x2C&&palettes[1][2]==0x30;
}
/* Use the current room's real decoder, including mutable active blocks. */
static int probe(int x,int y) {
    if(x<0||x>=0x3000||y<0||y>=240)return 1;
    if(cyc_mod_isolated())return mm1_world_probe(x,y);
    CycModRegs r;cyc_mod_regs(&r);if(!cyc_mod_isolate_begin())return -1;
    W(0x2F,0);W(0x0C,x>>8);W(0x0D,x);W(0x0E,y);
    int ok=cyc_mod_call(0xC39C,&r)&&cyc_mod_call(0xCCB7,&r),result=ok?r.a:-1;
    cyc_mod_isolate_end();return result;
}
static int solid(int c){if(c<0)return 1;if(c&128)return c!=0x84;return c==1||c==4;}
static int stand_free(int jump) {
    static const int offsets[]={-14,0,14};
    for(int i=0;i<3;i++){int c=probe(player_x()+offsets[i],R(0x600)-12);if(c<0||solid(c)||c==3||(jump&&c!=0))return 0;}
    return 1;
}
static int wall_ahead(int right){return solid(probe(player_x()+(right?15:-15),R(0x600)+4));}
static void end_slide(int stand) {
    S.active=S.stamp=S.hurt=0;
    if(stand){W(0x400,0);W(0x440,0);W(0x640,0);}
}
int mm1_slide_cancel_for_switch(void) {
    if(S.active&&!stand_free(0))return 0;
    if(S.active)end_slide(1);return 1;
}
void mm1_slide_restore_input(void) {
    if(S.inputs_open){W(0x14,S.held);W(0x18,S.fresh);W(0x16,S.old);S.inputs_open=0;}
}
void mm1_slide_stage_enter(void){mm1_slide_restore_input();end_slide(0);S.dust=0;}
static int start(void) {
    int type=R(0x400);
    if(!mm1_controls_gameplay()||(R(0x420)&31)||!(R(0x14)&32)||!(R(0x18)&1)||
        !(type==0||type==3||type==6||type==12||type==15))return 0;
    int held=R(0x14),right=(held&128)?1:(held&64)?0:(R(0x420)&64)!=0;
    if(wall_ahead(right))return 0;
    S.active=1;S.timer=26;S.right=(uint8_t)right;S.hurt=0;S.starts++;
    if(!S.dust){S.dust=1;S.dust_age=0;S.dust_right=(uint8_t)((R(0x420)&64)!=0);S.dust_x=(uint16_t)player_x();S.dust_y=(uint8_t)(R(0x600)+2);}
    return 1;
}
static int player(uint16_t address) {
    (void)address;if(!enabled)return 0;
    if(!S.active&&!start())return 0;
    if(!mm1_controls_gameplay()){end_slide(0);return 0;}
    if((R(0x420)&15)==3&&!S.hurt) {
        if(mm1_proto_selected()){end_slide(0);return 0;}
        S.hurt=27;S.hurt_age=0;S.hits++;
    }
    if(S.hurt) {
        W(0x420,S.right?64:0);W(0x400,6);W(0x640,0);W(0x4C0,0);W(0x4E0,0);W(0x680,0);W(0x660,0);
        S.hurt_age++;if(--S.hurt==0)S.timer=0;S.stamp=0;return 1;
    }
    if((R(0x420)&31)||mm1_proto_teleporting()){end_slide(0);return 0;}
    int held=R(0x14),fresh=R(0x18),forward=S.right?128:64,opposite=S.right?64:128;
    if((fresh&1)&&!(held&32)&&stand_free(1)){S.jumps++;end_slide(0);return 0;}
    if((held&opposite)&&!(held&forward)){S.right^=1;S.timer=0;S.reversals++;forward=S.right?128:64;}
    S.held=(uint8_t)held;S.fresh=(uint8_t)fresh;S.old=R(0x16);S.inputs_open=1;
    held=(held&~(0xF3))|forward;fresh&=~0xF3;
    W(0x14,held);W(0x18,fresh);W(0x16,(R(0x16)&~0xC0)|forward);
    W(0x420,(R(0x420)&~0x5F)|(S.right?64:0));W(0x400,6);W(0x640,0);
    S.before_x=(uint16_t)player_x();S.stamp=1;return 0;
}
static int after(uint16_t address) {
    (void)address;if(!S.active||!S.stamp||S.hurt)return 0;S.stamp=0;
    if(R(0x95))return 0; /* Native water/stall frames do not advance movement. */
    if(R(0x26)||R(0x1E)){end_slide(1);return 0;}
    if(R(0x400)==9){S.falls++;end_slide(0);return 0;}
    if(R(0x420)&31){end_slide(0);return 0;}
    int wall=player_x()==S.before_x;
    if(!wall&&S.timer)S.timer--;
    if((wall||!S.timer)&&stand_free(0)){if(wall)S.walls++;else S.timed++;end_slide(1);}
    else if(wall||!S.timer)S.blocked++;
    return 0;
}
void mm1_slide_after_run(void){after(0);}
static int speed(uint16_t address) {
    (void)address;if(S.active&&!S.hurt){CycModRegs r;cyc_mod_regs(&r);if(!r.x){r.a=0x82;r.p=(uint8_t)((r.p&~0x82)|0x80);cyc_mod_set_regs(&r);}}return 0;
}
static int width(uint16_t address) {
    (void)address;if(S.active&&!R(0x2F)){CycModRegs r;cyc_mod_regs(&r);r.a=15;r.p&=(uint8_t)~0x82;cyc_mod_set_regs(&r);}return 0;
}
static int side_probe(uint16_t address) {
    (void)address;if(S.active&&!R(0x2F)){CycModRegs r;cyc_mod_regs(&r);static const uint8_t delta[]={12,13,0};if(r.x<3){r.a=(uint8_t)(r.a+delta[r.x]);cyc_mod_set_regs(&r);}}return 0;
}
static int feet(uint16_t address) {
    (void)address;if(S.active&&!R(0x2F)) {
        CycModRegs r;cyc_mod_regs(&r);if(r.x!=1&&r.x!=3)return 0;
        unsigned old=R(0),add=r.x==1?14:242,sum=old+add;r.a=(uint8_t)sum;
        r.p=(uint8_t)((r.p&~0xC3)|(sum>255)|(!r.a?2:0)|(r.a&128)|((~(old^add)&(old^r.a)&128)?64:0));
        cyc_mod_set_regs(&r);
    }return 0;
}
static int contact(uint16_t address) {
    (void)address;if(S.active){W(3,R(0x600)-1);W(2,R(0x600)+5);}return 0;
}
static int stage_exit(uint16_t address){(void)address;mm1_slide_stage_enter();return 0;}
static void set_enabled(int on) {
#ifndef MM1_PROFILE_USA
    on=0;
#endif
    enabled=on&&loaded;if(!enabled)mm1_slide_stage_enter();
    for(int i=0;i<10;i++)nes_mod_set_function_hook_enabled(hooks[i],enabled);
    mm1_controls_refresh();mm1_video_character_changed();
}
static void power_on(void *ctx){(void)ctx;memset(slides,0,sizeof slides);}
static void frame_end(void *ctx) {
    (void)ctx;for(int i=0;i<2;i++)if(slides[i].dust&&++slides[i].dust_age>=26)slides[i].dust=0;
    if(report)fprintf(report,"{\"frame\":%llu,\"active\":%u,\"timer\":%u,\"hurt\":%u,\"x\":%d,\"y\":%u,\"starts\":%llu,\"timed\":%llu,\"walls\":%llu,\"falls\":%llu,\"jumps\":%llu,\"reverse\":%llu,\"blocked\":%llu}\n",
        (unsigned long long)cyc_ring_frame,S.active,S.timer,S.hurt,player_x(),R(0x600),
        (unsigned long long)S.starts,(unsigned long long)S.timed,(unsigned long long)S.walls,
        (unsigned long long)S.falls,(unsigned long long)S.jumps,(unsigned long long)S.reversals,(unsigned long long)S.blocked);
}
static int get(uint8_t *out,int cap) {
    if(cap<(int)sizeof slides)return -1;
    for(int i=0;i<2;i++){memcpy(slides[i].magic,"MS02",4);slides[i].on=(uint8_t)enabled;}
    memcpy(out,slides,sizeof slides);return sizeof slides;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !enabled;if(!in||len!=sizeof slides)return 0;Slide records[2];memcpy(records,in,sizeof records);
    for(int i=0;i<2;i++){Slide *n=&records[i];if(memcmp(n->magic,"MS02",4)||n->on!=enabled||n->active>1||
        n->timer>26||n->right>1||n->stamp>1||n->hurt>27||n->hurt_age>27||n->dust>1||n->dust_age>26||
        n->dust_right>1||n->inputs_open>1)return 0;}return 1;
}
static int put(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;memset(slides,0,sizeof slides);if(len)memcpy(slides,in,sizeof slides);return 1;
}
static void reset(void){set_enabled(0);}
static void activate(void){if(mm1_mm4_load("slide")){loaded=load_art();set_enabled(1);}}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(!strcmp(name,"--slide-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--slide"))return false;
    if(strcmp(value,"on")&&strcmp(value,"off"))return false;
    if(!strcmp(value,"on")){if(!mm1_mm4_load("slide"))return false;loaded=load_art();}
    set_enabled(!strcmp(value,"on"));return enabled==!strcmp(value,"on");
}
const CycHostExtras *mm1_slide_extras(void) {
    static const CycHostOption options[]={{"--slide",true,"on|off: MM4 slide (set --mm4-rom first)"},
        {"--slide-report",true,"FILE: slide observations"}};
    static const CycHostExtras extras={.power_on=power_on,.frame_end=frame_end,.options=options,.option_count=2,.option=option};
    return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_slide) {
#ifdef MM1_PROFILE_USA
    static const uint16_t addresses[]={0x94A8,0x918D,0xC567,0x9E42,0x9EC2,0xCB83,0xCC44,0xA1A8,0xC219,0xC05A};
    NESModFunctionEntryCallback functions[]={player,after,speed,width,width,side_probe,feet,contact,stage_exit,stage_exit};
    for(int i=0;i<10;i++)nes_mod_register_function_entry_plugin(hooks[i],addresses[i],functions[i]);
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mm1.backport.slide",activate);
    nes_mod_register_savestate_hook("mm1.slide",get,put);nes_mod_register_savestate_validator("mm1.slide",valid);
#endif
}
