#include "cyc_host_extras.h"
#include <string.h>

const CycHostExtras *mm1_widescreen_extras(void);
const CycHostExtras *mm1_weapons_extras(void);
static const CycHostExtras *parts[2];
static CycHostExtras host;
static CycHostOption options[16];
static int initialized;

static void power_on(void *ctx) {
    (void)ctx;
    for(int i=0;i<2;i++)if(parts[i]->power_on)parts[i]->power_on(parts[i]->ctx);
}
static void frame_end(void *ctx) {
    (void)ctx;
    for(int i=0;i<2;i++)if(parts[i]->frame_end)parts[i]->frame_end(parts[i]->ctx);
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;
    for(int i=0;i<2;i++)if(parts[i]->input)parts[i]->input(parts[i]->ctx,buttons);
}
static void tcp_setup(void *ctx) {
    (void)ctx;
    for(int i=0;i<2;i++)if(parts[i]->tcp_setup)parts[i]->tcp_setup(parts[i]->ctx);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    for(int i=0;i<2;i++)for(size_t k=0;k<parts[i]->option_count;k++)
        if(!strcmp(parts[i]->options[k].name,name))return parts[i]->option(parts[i]->ctx,name,value);
    return false;
}
const CycHostExtras *cyc_host_extras(void) {
    if(!initialized) {
        initialized=1;parts[0]=mm1_widescreen_extras();parts[1]=mm1_weapons_extras();
        host.power_on=power_on;host.frame_end=frame_end;host.input=input;
        host.option=option;host.tcp_setup=tcp_setup;host.options=options;
        host.input_profile=parts[1]->input_profile;
        for(int i=0;i<2;i++)for(size_t k=0;k<parts[i]->option_count;k++)
            options[host.option_count++]=parts[i]->options[k];
    }
    return &host;
}
