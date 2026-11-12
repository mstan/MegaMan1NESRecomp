/* Native MM1 panel phases and personal choices around one real frame wait. */
#include "mm1_coop_menu.h"
#include "mm1_coop.h"
#include "mm1_proto.h"
#include "mm1_weapon_gfx.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "logical_input.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum {PW=96,PH=160,ENTRY=0xC3BF,WAIT=0xC01B};
typedef struct {
    char magic[4];uint8_t enabled,active,owner,pending,checking,cursor[2],held[2],previous[2],ready[2],portrait[2],blink[2],saved[6];
    uint8_t image[2][PW*PH],palette[2][32];uint32_t ticks;
    uint64_t opens[2],selections[2],failures;
} Menu;
static Menu M;
static int capture;
static struct {const char *id;int enabled;} hooks[128];
static const uint8_t index_to_weapon[]={0,3,5,6,2,1,4,7};
static const uint8_t weapon_to_index[]={0,5,4,1,6,2,3,7};
int mm1_coop_menu_ready(int seat);
int mm1_coop_menu_scene(void);
int mm1_coop_menu_equip(int seat,int weapon);
int mm1_coop_character_for(int seat);
void mm1_coop_menu_begin(void);
void mm1_weapons_menu_state(int active);
int mm1_coop_menu_project(int seat);
void mm1_coop_menu_restore(void);
int mm1_coop_menu_active(void){return mm1_coop_available()&&M.active;}
static const uint8_t paused_addresses[]={0x23,0x47,0xB4,0x94,0x95,0xB1};
void mm1_coop_menu_cancel(void){
    if(M.checking)mm1_coop_menu_restore();
    if(M.active)for(int i=0;i<6;i++)W(paused_addresses[i],M.saved[i]);
    M.checking=M.active=M.pending=0;mm1_weapons_menu_state(0);
}
void mm1_coop_menu_reset(void){memset(&M,0,sizeof M);}
static void sound(int id){int n=R(0x45);if(n<16){W(0x580+n,id);W(0x45,n+1);}}
static int phase_end(uint16_t address){return capture==1&&address==0xB14A||capture==2&&address==0xB26C||capture==3&&address==0xB250;}
static int suspend(int on) {
    int count=nes_mod_function_hook_count();if(count>128)return 0;
    if(on) {
        for(int i=0;i<count;i++) {
            nes_mod_function_hook_at(i,&hooks[i].id,NULL,&hooks[i].enabled,NULL);
            if(hooks[i].enabled&&strncmp(hooks[i].id,"mm1.coop-menu.end-",18))nes_mod_set_function_hook_enabled(hooks[i].id,0);
        }
        cyc_mod_allow_isolated_hooks(true);
    } else {
        cyc_mod_allow_isolated_hooks(false);for(int i=0;i<count;i++)nes_mod_set_function_hook_enabled(hooks[i].id,hooks[i].enabled);
    }return 1;
}
static void inventory(int seat) {
    for(int a=0x6A;a<=0x71;a++)W(a,mm1_coop_read(seat,(uint16_t)a));
    W(0x5F,index_to_weapon[M.cursor[seat]]);W(0x1A,0);W(0x1B,0);
}
static void capture_panel(int seat) {
    uint8_t nt[2048],opaque[PW*PH];CycModRegs r={0};cyc_mod_regs(&r);
    const uint8_t *font=mm1_weapon_menu_font();if(!font||!cyc_mod_isolate_begin()){M.failures++;return;}
    for(int i=0;i<2048;i++)nt[i]=cyc_render_nametable((uint16_t)(0x2000+i));
    int ok=suspend(1)&&cyc_mod_call(0xC413,&r);inventory(seat);
    for(int i=0;i<256;i+=4)W(0x200+i,248);
    W(0x5B,0);W(0x380,0x0E);
    capture=1;
    for(int line=0;line<5&&ok;line++)for(int column=0;column<3&&ok;column++) {
        W(0x04,0x84+line*4+column*32);W(0x05,0);W(0x3C,line);W(0x381,(line*3+column)*16);r.x=(uint8_t)column;
        ok=cyc_mod_call(0xB12D,&r);
        if(ok) {
            unsigned address=(R(0x300)<<8)|R(0x301),attr=(R(0x312)<<8)|R(0x313);
            if(address<0x2000||address>=0x2800||attr<0x23C0||attr>=0x2800){ok=0;break;}
            for(int i=0;i<16;i++)nt[(address+(i%4)*32+i/4)&2047]=R(0x302+i);
            nt[attr&2047]=R(0x314);
        }
    }
    capture=2;if(ok)ok=cyc_mod_call(0xB183,&r);capture=0;
    if(ok){r.a=M.blink[seat];r.p=(uint8_t)((r.p&~2)|(M.blink[seat]?0:2));ok=cyc_mod_call(0xB3A7,&r);}
    if(ok) {
        for(int i=0;i<16;i++){M.palette[seat][i]=cyc_render_palette(i);M.palette[seat][i+16]=R(0x3E0+i)&63;}
        M.palette[seat][17]=R(0xD485);M.palette[seat][18]=R(0xD486);
        for(int y=0;y<PH;y++)for(int x=0;x<PW;x++) {
            int sx=128+x,sy=32+y,row=sy/8,col=sx/8,tile=nt[row*32+col];
            int attr=nt[0x3C0+row/4*8+col/4],pal=attr>>((row&2)*2+(col&2))&3,bit=7-(sx&7);
            unsigned at=cyc_render_line_bg_table(sy)+tile*16+(sy&7);
            int v=(cyc_render_chr((uint16_t)at)>>bit&1)|(cyc_render_chr((uint16_t)(at+8))>>bit&1)<<1;
            M.image[seat][y*PW+x]=(uint8_t)(v?pal*4+v:0);opaque[y*PW+x]=(uint8_t)(v!=0);
        }
        M.portrait[seat]=(uint8_t)mm1_coop_character_for(seat);
        for(int slot=63;slot>=0;slot--) {
            if(M.portrait[seat]&&slot>=58&&slot<=61)continue;
            int at=0x200+slot*4,sy=R(at),tile=R(at+1),attr=R(at+2),sx=R(at+3);if(sy>=239)continue;
            for(int y=0;y<8;y++)for(int x=0;x<8;x++) {
                int dx=sx+x-128,dy=sy+1+y-32;if(dx<0||dx>=PW||dy<0||dy>=PH||((attr&32)&&opaque[dy*PW+dx]))continue;
                int row=attr&128?7-y:y,bit=attr&64?x:7-x;unsigned pattern=tile*16+row;
                int lo=pattern>=0xE00&&pattern<0xF00?font[pattern-0xE00]:cyc_render_chr((uint16_t)pattern);
                int hi=pattern+8>=0xE00&&pattern+8<0xF00?font[pattern+8-0xE00]:cyc_render_chr((uint16_t)(pattern+8));
                int v=(lo>>bit&1)|(hi>>bit&1)<<1;if(v)M.image[seat][dy*PW+dx]=(uint8_t)(16+(attr&3)*4+v);
            }
        }
    }
    suspend(0);cyc_mod_isolate_end();M.ready[seat]=(uint8_t)ok;if(!ok)M.failures++;
}
static void move_cursor(int seat,int up) {
    CycModRegs r={0};cyc_mod_regs(&r);uint8_t sounds[16];int count=0;
    if(!cyc_mod_isolate_begin()){M.failures++;return;}
    int ok=suspend(1)&&cyc_mod_call(0xC413,&r);W(0x5F,M.cursor[seat]);capture=3;
    if(ok)ok=cyc_mod_call(up?0xB291:0xB2A5,&r);capture=0;
    if(ok){M.cursor[seat]=R(0x5F);count=R(0x45);for(int i=0;i<16;i++)sounds[i]=R(0x580+i);}
    suspend(0);cyc_mod_isolate_end();if(!ok){M.failures++;return;}M.blink[seat]=1;
    for(int i=R(0x45);i<count&&i<16;i++)W(0x580+i,sounds[i]);if(count>=R(0x45)&&count<=16)W(0x45,count);
}
static int request(uint16_t address) {
    (void)address;if(!mm1_coop_available()||M.active||!M.pending||M.checking||cyc_mod_isolated())return 0;
    int owner=M.pending-1;if(!mm1_coop_menu_ready(owner)||!mm1_coop_menu_project(owner)){
        /* Start was routed before lifecycle dispatch. If that seat has since
           withdrawn or become locked, resume after the entire native pause
           check rather than letting its Start open the anchor's inventory. */
        CycModRegs r={0};cyc_mod_regs(&r);unsigned s=r.s;
        W(0x100|s,0x91);s=(s-1)&255;W(0x100|s,0x86);s=(s-1)&255;
        r.s=(uint8_t)s;cyc_mod_set_regs(&r);M.pending=0;return 1;
    }
    M.checking=1;return 0;
}
static int after_pause(uint16_t address) {
    (void)address;if(M.checking){mm1_coop_menu_restore();M.checking=M.pending=0;}return 0;
}
static int menu(uint16_t address) {
    if(!mm1_coop_available()||cyc_mod_isolated()||(!M.active&&!mm1_coop_menu_scene()))return 0;
    if(!M.active) {
        int owner=M.pending?M.pending-1:mm1_coop_anchor();
        if(M.checking){mm1_coop_menu_restore();M.checking=0;}M.pending=0;
        if(!mm1_coop_menu_ready(owner)){mm1_weapons_menu_state(0);return 1;}
        M.active=1;M.owner=(uint8_t)owner;M.opens[owner]++;mm1_coop_menu_begin();mm1_weapons_menu_state(1);sound(0x22);
        for(int i=0;i<6;i++){M.saved[i]=R(paused_addresses[i]);if(i)W(paused_addresses[i],0);}
        for(int seat=0;seat<2;seat++){M.cursor[seat]=weapon_to_index[mm1_coop_read(seat,0x5F)&7];M.previous[seat]=M.held[seat];M.blink[seat]=1;}
    } else {
        M.ticks++;int close=0;
        for(int seat=0;seat<2;seat++) {
            unsigned fresh=M.held[seat]&~M.previous[seat];
            if((seat==M.owner||mm1_coop_independent())&&mm1_coop_menu_ready(seat)) {
                if(fresh&12)move_cursor(seat,(fresh&8)!=0);
                if(fresh&(0x80|0x10)) {
                    if(mm1_coop_menu_equip(seat,index_to_weapon[M.cursor[seat]]))M.selections[seat]++;
                    if((fresh&0x10)&&seat==M.owner)close=1;
                }
            }
            M.previous[seat]=M.held[seat];
        }
        if(close){mm1_coop_menu_cancel();sound(0x22);return 1;}
    }
    for(int seat=0;seat<2;seat++){if(!(R(0x23)&7))M.blink[seat]=(uint8_t)((R(0x23)&8)!=0);capture_panel(seat);}
    CycModRegs r={0};cyc_mod_regs(&r);if(r.s<4){M.failures++;mm1_coop_menu_cancel();return 1;}
    unsigned sp=(uint8_t)(r.s-4),returns[]={WAIT-1,address-1};
    for(int i=0;i<2;i++){W(0x100|(uint8_t)(sp+1+i*2),returns[i]);W(0x100|(uint8_t)(sp+2+i*2),returns[i]>>8);}
    r.s=(uint8_t)sp;cyc_mod_set_regs(&r);return 1;
}
void mm1_coop_menu_input(uint8_t buttons[2]) {
    if(!mm1_coop_available())return;
    for(int seat=0;seat<2;seat++) {
        uint8_t held=nes_input_seat(seat+1);
        if(!M.active&&!M.pending&&(held&~M.held[seat]&0x10)&&mm1_coop_menu_ready(seat))M.pending=(uint8_t)(seat+1);
        M.held[seat]=held;
    }
    if(!M.active&&!mm1_coop_menu_scene()){M.pending=0;return;}
    if(M.active)buttons[0]=buttons[1]=0;else if(M.pending)buttons[0]|=0x10;else buttons[0]&=(uint8_t)~0x10;
}
int mm1_coop_menu_draw(uint32_t *out,int width,int own) {
    if(!mm1_coop_menu_active())return 0;int seat=own<0?M.owner:own;
    if(seat<0||seat>1||!M.ready[seat])return 0;
    {
        int x0=(width-256)/2+128,y0=32;
        for(int y=0;y<PH;y++)for(int x=0;x<PW;x++)if(x0+x>=0&&x0+x<width)
            out[(y0+y)*width+x0+x]=cyc_render_color_value(M.palette[seat][M.image[seat][y*PW+x]]);
        if(M.portrait[seat]&&mm1_proto_atlas())for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
            uint32_t c=mm1_proto_atlas()->portrait[y*16+x];if(c)out[(y0+129+y)*width+x0+40+x]=c;
        }
    }return 1;
}
void mm1_coop_menu_refresh(void) {
    const char *ids[]={"mm1.coop-menu.entry","mm1.coop-menu.end-phase","mm1.coop-menu.end-sprites","mm1.coop-menu.end-cursor",
        "mm1.coop-menu.request","mm1.coop-menu.after-pause"};
    for(int i=0;i<6;i++)nes_mod_set_function_hook_enabled(ids[i],mm1_coop_available());
    if(!mm1_coop_available())mm1_coop_menu_cancel();
}
void mm1_coop_menu_debug(char *out,int cap) {
    snprintf(out,(size_t)cap,"\"menu\":%u,\"menu_owner\":%u,\"cursor\":[%u,%u],\"menu_failures\":%llu,\"menu_opens\":[%llu,%llu],\"menu_choices\":[%llu,%llu]",
        M.active,M.owner,M.cursor[0],M.cursor[1],(unsigned long long)M.failures,(unsigned long long)M.opens[0],(unsigned long long)M.opens[1],
        (unsigned long long)M.selections[0],(unsigned long long)M.selections[1]);
}
static int get(uint8_t *out,int cap){if(cap<sizeof M)return -1;memcpy(M.magic,"MC01",4);M.enabled=(uint8_t)mm1_coop_available();memcpy(out,&M,sizeof M);return sizeof M;}
static int valid(const uint8_t *in,int size) {
    if(!size)return !mm1_coop_available();Menu n;if(!in||size!=sizeof n)return 0;memcpy(&n,in,size);
    if(memcmp(n.magic,"MC01",4)||n.enabled!=mm1_coop_available()||n.active>1||n.owner>1||n.pending>2||n.checking>1)return 0;
    for(int s=0;s<2;s++){if(n.cursor[s]>7||n.ready[s]>1||n.portrait[s]>1||n.blink[s]>1)return 0;
        for(int i=0;i<PW*PH;i++)if(n.image[s][i]>31)return 0;for(int i=0;i<32;i++)if(n.palette[s][i]>63)return 0;}
    return 1;
}
static int put(const uint8_t *in,int size){if(!valid(in,size))return 0;memset(&M,0,sizeof M);if(size)memcpy(&M,in,size);return 1;}
NES_MOD_CONSTRUCTOR(register_mm1_coop_menu) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_function_entry_plugin("mm1.coop-menu.entry",ENTRY,menu);
    nes_mod_register_function_entry_plugin("mm1.coop-menu.end-phase",0xB14A,phase_end);
    nes_mod_register_function_entry_plugin("mm1.coop-menu.end-sprites",0xB26C,phase_end);
    nes_mod_register_function_entry_plugin("mm1.coop-menu.end-cursor",0xB250,phase_end);
    nes_mod_register_function_entry_plugin("mm1.coop-menu.request",0x916F,request);
    nes_mod_register_function_entry_plugin("mm1.coop-menu.after-pause",0x9180,after_pause);
    nes_mod_register_savestate_hook("mm1.coop-menu",get,put);nes_mod_register_savestate_validator("mm1.coop-menu",valid);
#endif
}
