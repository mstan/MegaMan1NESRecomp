/* Native MM1 menu-exit patterns, read only from the verified local cartridge. */
#include "mm1_weapon_gfx.h"
#include "mm1_coop.h"
#include "mm1_reflect.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
static uint8_t weapons[8][256],guts[10][256],font[256];
static int loaded;
typedef struct {char magic[4];uint8_t on,pending,weapon,stage;} Graphics;
static Graphics G;
int mm1_weapon_cycle_available(void);
int mm1_controls_menu(void);
static int active(void){return mm1_weapon_cycle_available()||mm1_coop_available();}
static int load(void) {
    if(loaded)return 1;
    size_t size=cyc_mod_copy_cart_payload(NULL,0);if(size!=131072)return 0;
    uint8_t *prg=(uint8_t*)malloc(size);if(!prg)return 0;
    int ok=cyc_mod_copy_cart_payload(prg,size)==size;
    if(ok) {
        /* Native B2FC uses bank2/pageB39F, except Guts' per-stage bank/page.
         * Its sixteen 16-byte transfers cover the reserved0E00 pattern page. */
        for(int weapon=0;weapon<8;weapon++) {
            unsigned page=prg[6*16384+0x339F+weapon];if(page<0x80||page>0xBF){ok=0;break;}
            memcpy(weapons[weapon],prg+2*16384+(page-0x80)*256,256);
        }
        for(int stage=0;stage<10&&ok;stage++) {
            unsigned page=prg[6*16384+0x32E1+stage],bank=prg[6*16384+0x32EC+stage];
            if(bank>7||page<0x80||page>0xBF){ok=0;break;}
            memcpy(guts[stage],prg+bank*16384+(page-0x80)*256,256);
        }
        memcpy(font,prg+2*16384+0x2C00,256);
    }
    free(prg);loaded=ok;return ok;
}
static const uint8_t *patterns(int weapon,int stage) {
    if(weapon<0||weapon>7||stage<0||stage>9||!load())return NULL;
    return weapon==6?guts[stage]:weapons[weapon];
}
void mm1_weapon_gfx_reset(void){memset(&G,0,sizeof G);loaded=0;}
void mm1_weapon_gfx_begin(void) {
    if(!active()||R(0x31)>=10||mm1_coop_total()!=32||mm1_controls_menu())return;
    int weapon=mm1_weapon_identity();if(!patterns(weapon,R(0x31)))return;
    G.weapon=(uint8_t)weapon;G.stage=R(0x31);G.pending=1;
}
void mm1_weapon_gfx_latch(void) {
    if(!active()||!G.pending)return;const uint8_t *p=patterns(G.weapon,G.stage);G.pending=0;if(!p)return;
    /* This actual DMA publishes the corresponding built body's OAM. A lag
     * NMI before a new draw keeps the previous owner's pattern page. */
    for(int i=0;i<256;i++)if(cyc_render_chr((uint16_t)(0xE00+i))!=p[i])cyc_mod_chr_poke((uint16_t)(0xE00+i),p[i]);
}
uint8_t mm1_weapon_gfx_private(uint16_t address,uint8_t fallback) {
    if(!mm1_coop_available()||address<0xE00||address>=0xF00)return fallback;
    const uint8_t *p=patterns(mm1_weapon_identity(),R(0x31));return p?p[address-0xE00]:fallback;
}
const uint8_t *mm1_weapon_menu_font(void){return load()?font:NULL;}
static int get(uint8_t *out,int cap){if(cap<sizeof G)return -1;memcpy(G.magic,"WG01",4);G.on=(uint8_t)active();memcpy(out,&G,sizeof G);return sizeof G;}
static int valid(const uint8_t *in,int size) {
    if(!size)return !active();Graphics n;if(!in||size!=sizeof n)return 0;memcpy(&n,in,size);
    return !memcmp(n.magic,"WG01",4)&&n.on==active()&&n.pending<2&&n.weapon<8&&n.stage<10;
}
static int put(const uint8_t *in,int size){if(!valid(in,size))return 0;memset(&G,0,sizeof G);if(size)memcpy(&G,in,size);return 1;}
NES_MOD_CONSTRUCTOR(register_mm1_weapon_gfx) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_savestate_hook("mm1.weapon-gfx",get,put);nes_mod_register_savestate_validator("mm1.weapon-gfx",valid);
#endif
}
