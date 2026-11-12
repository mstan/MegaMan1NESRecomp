#include "cyc_host_extras.h"
#include "cyc_video.h"
#include "mm1_net.h"
#include "mm1_coop.h"
#include "mm1_proto.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "mm1_sprites.h"
#include "mm1_weapon_gfx.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"
#endif
#include <string.h>

const CycHostExtras *mm1_widescreen_extras(void);
const CycHostExtras *mm1_weapons_extras(void);
const CycHostExtras *mm1_proto_extras(void);
const CycHostExtras *mm1_charge_extras(void);
const CycHostExtras *mm1_slide_extras(void);
const CycHostExtras *mm1_coop_extras(void);
const CycHostExtras *mm1_slowdown_extras(void);
const CycHostExtras *mm1_sprites_extras(void);
enum {PARTS=8};
static const CycHostExtras *parts[PARTS];
static CycHostExtras host;
static CycHostOption options[32];
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
static void state_loaded(void *ctx) {
    (void)ctx;
    for(int i=0;i<PARTS;i++)if(parts[i]->state_loaded)parts[i]->state_loaded(parts[i]->ctx);
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
    if(mm1_net_locked()&&strcmp(name,"--coop-view"))return false;
    mm1_net_note_option(name);
    for(int i=0;i<PARTS;i++)for(size_t k=0;k<parts[i]->option_count;k++)
        if(!strcmp(parts[i]->options[k].name,name))return parts[i]->option(parts[i]->ctx,name,value);
    return false;
}
int mm1_widescreen_enabled(void);
int mm1_widescreen_extended(void);
int mm1_widescreen_aspect(void);
unsigned mm1_slowdown_lines(void);
Mm1Settings mm1_host_settings(void) {
    int wanted,mode,start;mm1_proto_settings(&wanted,&mode,&start);
    Mm1Settings c={mm1_coop_available(),mm1_coop_independent(),mm1_coop_roster(),mm1_coop_wait_both(),wanted,mode,start,
        mm1_weapon_cycle_available(),mm1_widescreen_enabled(),mm1_widescreen_extended(),mm1_sprites_enabled(),
        (int)mm1_slowdown_lines(),mm1_charge_available(),mm1_slide_available()};return c;
}
int mm1_host_revalidate_donor(int charge,int slide) {
    return (!charge||mm1_mm4_load("charge"))&&(!slide||mm1_mm4_load("slide"));
}
int mm1_host_apply_settings(Mm1Settings c,int local_seat) {
    if((c.charge||c.slide)&&!mm1_host_revalidate_donor(c.charge,c.slide))return 0;
    int view=mm1_coop_view_mode(),aspect=mm1_widescreen_aspect();
    const char *ratio=aspect==NES_VIDEO_16_9?"16:9":aspect==NES_VIDEO_21_9?"21:9":aspect==NES_VIDEO_32_9?"32:9":"fit";
    const char *layout=c.independent?(view==1?"split":view==2?"p1":view==3?"p2":local_seat==1?"p2":"p1"):"shared";
    return option(NULL,"--coop",c.coop?"on":"off")&&
        option(NULL,"--coop-p1",c.roster?"protoman":"megaman")&&option(NULL,"--coop-view",layout)&&
        option(NULL,"--coop-wait-both",c.wait_both?"on":"off")&&mm1_proto_configure(c.proto,c.mode,c.start)&&
        option(NULL,"--weapon-cycle",c.weapons?"on":"off")&&option(NULL,"--widescreen",c.wide?ratio:"off")&&
        option(NULL,"--widescreen-enemies",c.extended?"viewport":"classic")&&option(NULL,"--no-flicker",c.flicker?"on":"off")&&
        option(NULL,"--slowdown",c.lines?"on":"off")&&option(NULL,"--charge",c.charge?"on":"off")&&option(NULL,"--slide",c.slide?"on":"off");
}
#ifdef CYC_WITH_RECOMP_UI
static const char *const views[]={"Shared screen","Independent split screen","Independent: player one","Independent: player two"};
static const RecompRuntimeUiItem menu_items[]={{.key="mm1.coop.view",.section="Game",.label="Two-player view",
    .description="Independent views can change during play.",
    .type=RECOMP_RUNTIME_UI_CHOICE,.minimum=0,.maximum=3,.step=1,.choices=views,.choice_count=4}};
static int menu_get(void *ctx,const RecompRuntimeUiItem *item,int *out) {
    (void)ctx;if(!item||!item->key||strcmp(item->key,"mm1.coop.view"))return 0;*out=mm1_coop_view_mode();return 1;
}
static int menu_set(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;if(!item||!item->key||strcmp(item->key,"mm1.coop.view")||!mm1_coop_available()||!mm1_coop_view_can_set(value))return 0;
    return option(NULL,"--coop-view",value==1?"split":value==2?"p1":value==3?"p2":"shared");
}
static int menu_enabled(void *ctx,const RecompRuntimeUiItem *item) {
    (void)ctx;return item&&item->key&&!strcmp(item->key,"mm1.coop.view")&&mm1_coop_available()&&
        (mm1_coop_independent()||mm1_coop_view_can_set(1));
}
static bool menu_netplay_allowed(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;return item&&item->key&&!strcmp(item->key,"mm1.coop.view")&&item->type==RECOMP_RUNTIME_UI_CHOICE&&
        mm1_coop_available()&&mm1_coop_independent()&&value>=1&&value<=3&&mm1_coop_view_can_set(value);
}
static const RecompRuntimeUiCallbacks menu_callbacks={.get_value=menu_get,.set_value=menu_set,.is_enabled=menu_enabled};
#endif
const CycHostExtras *cyc_host_extras(void) {
    if(!initialized) {
        initialized=1;parts[0]=mm1_widescreen_extras();parts[1]=mm1_weapons_extras();
        parts[2]=mm1_proto_extras();
        parts[3]=mm1_charge_extras();
        parts[4]=mm1_slide_extras();
        parts[5]=mm1_coop_extras();
        parts[6]=mm1_slowdown_extras();
        parts[7]=mm1_sprites_extras();
        host.power_on=power_on;host.frame_end=frame_end;host.input=input;
        host.frame_begin=frame_begin;host.state_loaded=state_loaded;
        host.audio_mix=audio_mix;host.audio_rate=48000;
        host.option=option;host.tcp_setup=tcp_setup;host.options=options;
        host.input_profile=parts[1]->input_profile;
#ifdef CYC_WITH_RECOMP_UI
        host.menu_items=menu_items;host.menu_item_count=1;host.menu_callbacks=&menu_callbacks;
#ifdef CYC_HOST_EXTRAS_HAS_NETPLAY_VALUE_POLICY
        host.menu_netplay_value_allowed=menu_netplay_allowed;
#endif
#endif
        for(int i=0;i<PARTS;i++)for(size_t k=0;k<parts[i]->option_count;k++)
            options[host.option_count++]=parts[i]->options[k];
    }
    return &host;
}
