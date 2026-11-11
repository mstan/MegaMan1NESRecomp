#include "cyc_host_extras.h"
#include <string.h>

const CycHostExtras *mm1_widescreen_extras(void);
const CycHostExtras *mm1_weapons_extras(void);
const CycHostExtras *mm1_proto_extras(void);
const CycHostExtras *mm1_charge_extras(void);
const CycHostExtras *mm1_slide_extras(void);
enum {PARTS=5};
static const CycHostExtras *parts[PARTS];
static CycHostExtras host;
static CycHostOption options[20];
static int initialized;

static void power_on(void *ctx) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->power_on)parts[i]->power_on(parts[i]->ctx);
}
static void frame_begin(void *ctx) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->frame_begin)parts[i]->frame_begin(parts[i]->ctx);
}
static void frame_end(void *ctx) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->frame_end)parts[i]->frame_end(parts[i]->ctx);
}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->input)parts[i]->input(parts[i]->ctx,buttons);
}
static void tcp_setup(void *ctx) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->tcp_setup)parts[i]->tcp_setup(parts[i]->ctx);
}
static void audio_mix(void *ctx,int16_t *samples,size_t count) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->audio_mix)parts[i]->audio_mix(parts[i]->ctx,samples,count);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)for(size_t k=0;k<parts[i]->option_count;k++)
        if(!strcmp(parts[i]->options[k].name,name))return parts[i]->option(parts[i]->ctx,name,value);
    return false;
}
const CycHostExtras *cyc_host_extras(void) {
    if(!initialized) {
        initialized=1;parts[0]=mm1_widescreen_extras();parts[1]=mm1_weapons_extras();
        parts[2]=mm1_proto_extras();
        parts[3]=mm1_charge_extras();
        parts[4]=mm1_slide_extras();
        host.power_on=power_on;host.frame_end=frame_end;host.input=input;
        host.frame_begin=frame_begin;
        host.audio_mix=audio_mix;host.audio_rate=48000;
        host.option=option;host.tcp_setup=tcp_setup;host.options=options;
        host.input_profile=parts[1]->input_profile;
        for(int i=0;i<PARTS;i++)for(size_t k=0;k<parts[i]->option_count;k++)
            options[host.option_count++]=parts[i]->options[k];
    }
    return &host;
}
