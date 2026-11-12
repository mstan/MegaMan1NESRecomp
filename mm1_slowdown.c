/* Select the engine's existing blank-line CPU budget, as in the MM2 port. */
#include "cyc_host_extras.h"
#include "cyc_core.h"
#include "cyc_ring.h"
#include "mod_runtime.h"
#include "mm1_net.h"
#include <stdio.h>
#include <string.h>

static unsigned selected;
static FILE *report;
unsigned mm1_slowdown_lines(void){return selected;}
static void apply(void) {
    if(cyc_extra_scanlines()!=selected&&!cyc_set_extra_scanlines(selected))
        fprintf(stderr,"[Mods] slowdown reduction: extra CPU time is unavailable for this cartridge\n");
}
static void select_lines(unsigned lines){if(mm1_net_locked())return;selected=lines;apply();}
static void power_on(void *ctx){(void)ctx;apply();}
static void state_loaded(void *ctx){(void)ctx;apply();}
static void frame_end(void *ctx) {
    (void)ctx;if(!report)return;
    const uint8_t *ram=cyc_cpu_ram();
    /* C029 clears1F only when ready to wait; D4AD skips graphics when set. */
    fprintf(report,"{\"frame\":%llu,\"selected\":%u,\"lines\":%u,\"wait\":%u,\"stage\":%u,\"render\":%u}\n",
        (unsigned long long)cyc_ring_frame,selected,cyc_extra_scanlines(),ram[0x1F],ram[0x31],(ram[0xFE]&24)!=0);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;if(!strcmp(name,"--slowdown-report")){report=fopen(value,"w");return report!=NULL;}
    if(strcmp(name,"--slowdown")||(strcmp(value,"on")&&strcmp(value,"off")))return false;
#ifndef MM1_PROFILE_USA
    if(!strcmp(value,"on"))return false;
#endif
    select_lines(!strcmp(value,"on")?262:0);return true;
}
static void reset(void){select_lines(0);}
static void activate(void){select_lines(262);}
const CycHostExtras *mm1_slowdown_extras(void) {
    static const CycHostOption options[]={{"--slowdown",true,"on|off: extra CPU time to reduce native slowdown"},
        {"--slowdown-report",true,"FILE: native timing observations"}};
    static const CycHostExtras extras={.power_on=power_on,.frame_end=frame_end,.state_loaded=state_loaded,
        .option=option,.options=options,.option_count=2};return &extras;
}
NES_MOD_CONSTRUCTOR(register_mm1_slowdown) {
#ifdef MM1_PROFILE_USA
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mm1.slowdown",activate);
#endif
}
