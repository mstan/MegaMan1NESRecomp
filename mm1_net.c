/* The existing shared identity/capability barrier owns transport and rollback. */
#include "mm1_net.h"
#include "mm1_coop.h"
#include "cyc_core.h"
#include "cyc_video.h"
#include "cyc_session.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int sealed,applying,simulation_width=256;
static unsigned cli;
int mm1_net_locked(void){return sealed&&!applying;}
int mm1_net_simulation_width(int local){return sealed?simulation_width:local;}
void mm1_net_note_option(const char *name) {
    if(applying||sealed)return;
    if(!strcmp(name,"--coop")||!strcmp(name,"--coop-view")||!strcmp(name,"--coop-p1")||!strcmp(name,"--coop-wait-both"))cli|=1;
    if(!strcmp(name,"--protoman"))cli|=2;
    if(!strcmp(name,"--weapon-cycle"))cli|=4;
    if(!strcmp(name,"--widescreen")||!strcmp(name,"--widescreen-enemies"))cli|=8;
    if(!strcmp(name,"--no-flicker"))cli|=16;
    if(!strcmp(name,"--slowdown"))cli|=32;
    if(!strcmp(name,"--coop-test-poke"))cli|=64;
    if(!strcmp(name,"--charge"))cli|=128;
    if(!strcmp(name,"--slide"))cli|=256;
}
#ifdef NESRECOMP_NET
#include "cyc_net.h"
#include "nes_netplay.h"
#include "nes_netplay_identity.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_launcher.h"
#endif
#ifndef MM1_NETPLAY_QUALIFIED
#define MM1_NETPLAY_QUALIFIED 0
#endif
static Mm1Settings offline;
static int saved,offline_view,offline_video,offline_width;
static char offline_offer[256];
static int selected(const char *package,const char *feature){return nes_mod_feature_selected(package,feature)!=0;}
static int option_is(const char *package,const char *feature,const char *key,const char *want,const char *fallback) {
    char value[32];if(!nes_mod_option_value(package,feature,key,value,sizeof value))snprintf(value,sizeof value,"%s",fallback);return !strcmp(value,want);
}
static Mm1Settings offer(void) {
    Mm1Settings c=mm1_host_settings();
    if(cyc_session_mods_enabled()) {
        const char *proto="mega-man-usa.character.protoman",*coop="mega-man-usa.coop",*wide="mega-man-usa.enhancement.widescreen";
        if(!(cli&1)) {
            c.coop=selected(coop,"coop");c.independent=!option_is(coop,"coop","coop-view","shared","shared");
            c.roster=option_is(coop,"coop","player-one","protoman","megaman");c.wait_both=option_is(coop,"coop","wait-both","on","off");
        }
        if(!(cli&2)) {
            c.proto=selected(proto,"protoman");c.mode=option_is(proto,"protoman","mode","protoman","protoman");
            c.start=option_is(proto,"protoman","start","protoman","protoman");
        }
        if(!(cli&4))c.weapons=selected("mega-man-usa.enhancement.quick-weapons","quick-weapons");
        if(!(cli&8)){c.wide=selected(wide,"widescreen");c.extended=option_is(wide,"widescreen","enemy_activation","viewport","viewport");}
        if(!(cli&16))c.flicker=selected("mega-man-usa.enhancement.no-flicker","no-flicker");
        if(!(cli&32))c.lines=selected("mega-man-usa.enhancement.slowdown","slowdown")?262:0;
        if(!(cli&128))c.charge=selected("mega-man-usa.backport.mm4","charge");
        if(!(cli&256))c.slide=selected("mega-man-usa.backport.mm4","slide");
    }
    if(c.coop)c.proto=1;return c;
}
static int catalog_supported(void) {
    if(!cyc_session_mods_enabled())return 1;
#ifdef CYC_WITH_RECOMP_UI
    const RecompLauncherCModProvider *p=nes_mod_runtime_launcher_provider_c();
    if(!p||!p->feature_count||!p->feature_get)return 0;
    static const char *known[][2]={{"mega-man-usa.coop","coop"},{"mega-man-usa.character.protoman","protoman"},
        {"mega-man-usa.enhancement.quick-weapons","quick-weapons"},{"mega-man-usa.enhancement.widescreen","widescreen"},
        {"mega-man-usa.enhancement.no-flicker","no-flicker"},{"mega-man-usa.enhancement.slowdown","slowdown"},
        {"mega-man-usa.backport.mm4","charge"},{"mega-man-usa.backport.mm4","slide"},{"nesrecomp.developer.replay","replay-capture"}};
    for(int i=0;i<p->feature_count(p->ctx);i++) {
        RecompLauncherCModFeature f;if(!p->feature_get(p->ctx,i,&f))return 0;if(!f.enabled)continue;
        int found=0;for(unsigned j=0;j<sizeof known/sizeof *known;j++)if(!strcmp(f.package_id,known[j][0])&&!strcmp(f.id,known[j][1]))found=1;
        if(!found){fprintf(stderr,"[netplay] unqualified feature: %s/%s\n",f.package_id,f.id);return 0;}
    }return 1;
#else
    return 0;
#endif
}
static int describe(Mm1Settings c,char *out,int cap) {
    return snprintf(out,cap,"1:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d",c.coop,c.independent,c.roster,c.wait_both,c.proto,c.mode,c.start,c.weapons,c.wide,c.extended,c.flicker,c.lines,c.charge,c.slide);
}
static int settings_get(char *out,int cap){return describe(mm1_host_settings(),out,cap);}
static int settings_offer(char *out,int cap){return describe(offer(),out,cap);}
static void preserve_offline(void) {
    if(saved)return;
    offline=mm1_host_settings();offline_view=mm1_coop_view_mode();offline_video=cyc_video_mode();offline_width=cyc_video_width();
    settings_offer(offline_offer,sizeof offline_offer);saved=1;
}
static int bit(int n){return n==0||n==1;}
static int settings_apply(const char *value) {
    int v[15];const char *cursor=value;
    if((cli&64)||!catalog_supported())return 0;
    for(int i=0;i<15;i++) {
        char *end;long n=strtol(cursor,&end,10);
        if(end==cursor||n<0||n>262||(i<14?*end!=':':*end))return 0;v[i]=(int)n;cursor=end+1;
    }
    Mm1Settings c={v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14]};
    if(v[0]!=1||c.coop!=1||c.proto!=1||!bit(c.independent)||!bit(c.roster)||!bit(c.wait_both)||!bit(c.mode)||!bit(c.start)||
        !bit(c.weapons)||!bit(c.wide)||!bit(c.extended)||!bit(c.flicker)||!bit(c.charge)||!bit(c.slide)||
        (c.lines!=0&&c.lines!=262)||(c.independent&&simulation_width!=256))return 0;
    if((c.charge||c.slide)&&!mm1_host_revalidate_donor(c.charge,c.slide)) {
        fprintf(stderr,"[netplay] verify the Mega Man 4 USA donor before enabling charge or slide\n");return 0;
    }
    preserve_offline();
    applying=1;int ok=mm1_host_apply_settings(c,nes_netplay_local_slot());
    if(ok){const CycHostExtras *x=cyc_host_extras();if(x&&x->power_on)x->power_on(x->ctx);}
    applying=0;
    if(!ok){applying=1;mm1_host_apply_settings(offline,0);applying=0;return 0;}
    sealed=1;nes_netplay_require_capabilities(1);nes_netplay_require_player_count(2);
    /* The common width controls simulation; local Fit/resize remains local. */
    if(!getenv("NES_NET_INPUT_PLAYER")) {
        char binding[16]="seat";if(cyc_session_mods_enabled())nes_mod_option_value("mega-man-usa.coop","coop","online-input",binding,sizeof binding);
        nes_netplay_set_input_player(!strcmp(binding,"device1")?0:!strcmp(binding,"device2")?1:nes_netplay_local_slot());
    }
#if MM1_NETPLAY_QUALIFIED
    nes_mod_set_local_only("Mega Man 1 co-op development",0);
#else
    nes_mod_set_local_only("Mega Man 1 unqualified simulation profile",1);
#endif
    return cyc_extra_scanlines()==(unsigned)c.lines;
}
static void settings_restore(void) {
    sealed=0;
    if(saved) {
        applying=1;int restored=mm1_host_apply_settings(offline,0);
        char view[16];snprintf(view,sizeof view,"%s",offline_view==1?"split":offline_view==2?"p1":offline_view==3?"p2":"shared");
        restored=mm1_coop_extras()->option(NULL,"--coop-view",view)&&restored;applying=0;
        if(offline_video==NES_VIDEO_STOCK&&!offline.independent)cyc_video_request_width(offline_width);else cyc_video_set_mode(offline_video);
        char expected[256],actual[256],next[256];describe(offline,expected,sizeof expected);settings_get(actual,sizeof actual);settings_offer(next,sizeof next);
        fprintf(stderr,"MM1_NET_RESTORE applied=%d expected=%s actual=%s expected_offer=%s next_offer=%s expected_view=%d actual_view=%d expected_lines=%d actual_lines=%u\n",
            restored,expected,actual,offline_offer,next,offline_view,mm1_coop_view_mode(),offline.lines,cyc_extra_scanlines());
        saved=0;
    }
    nes_netplay_require_capabilities(0);nes_netplay_require_player_count(0);nes_netplay_set_input_player(-1);
    nes_mod_set_local_only("Mega Man 1 unqualified simulation profile",0);
}
static int width_get(char *out,int cap){return snprintf(out,cap,"%d",sealed?simulation_width:(mm1_coop_independent()?256:cyc_video_width()));}
static int width_offer(char *out,int cap){Mm1Settings c=offer();int w=c.independent||!c.wide?256:cyc_video_width();if(c.wide&&!c.independent&&w<=256)w=426;return snprintf(out,cap,"%d",w);}
static int width_apply(const char *value){char *end;long n=strtol(value,&end,10);if(end==value||*end||n<256||n>CYC_VIDEO_MAX_WIDTH||(n&1))return 0;preserve_offline();simulation_width=(int)n;return 1;}
static int timing_get(char *out,int cap){return snprintf(out,cap,"%d:%u",(int)cyc_region(),cyc_extra_scanlines());}
static int timing_offer(char *out,int cap){return snprintf(out,cap,"%d:%d",(int)cyc_region(),offer().lines);}
static int timing_apply(const char *value){int region;unsigned lines;char tail;if(sscanf(value,"%d:%u%c",&region,&lines,&tail)!=2||region!=(int)cyc_region()||(lines!=0&&lines!=262))return 0;preserve_offline();return cyc_set_extra_scanlines(lines);}
static int sheet_get(char *out,int cap) {
    uint8_t digest[32];if(cap<65||!mm1_proto_source_hash(digest))return 0;
    for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);return 64;
}
static int sheet_apply(const char *value){char actual[65];return sheet_get(actual,sizeof actual)==64&&!strcmp(value,actual);}
NES_MOD_CONSTRUCTOR(register_mm1_net) {
#ifdef MM1_PROFILE_USA
    nes_runner_register_session_keys();
    nes_netplay_session_register("timing",timing_get,timing_apply,settings_restore);nes_netplay_session_set_offer("timing",timing_offer);
    nes_netplay_session_register("vw",width_get,width_apply,settings_restore);nes_netplay_session_set_offer("vw",width_offer);
    nes_netplay_session_register("mm1",settings_get,settings_apply,settings_restore);nes_netplay_session_set_offer("mm1",settings_offer);
    nes_netplay_session_register("sheet",sheet_get,sheet_apply,NULL);
#endif
}
#endif
