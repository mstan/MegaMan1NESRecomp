/* Regional stage compositor and current-page sprite capture. */
#pragma once
#include "cyc_host_extras.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "cyc_tcp.h"
#include "cyc_run.h"
#include "cyc_core.h"
#include "cyc_session.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include "mm1_proto.h"
#include "mm1_proto_video.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "mm1_coop.h"
#include "mm1_coop_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
#define WS_TILES 512
typedef struct { int16_t x,y; uint8_t tile,attr,owner,pad; } WsSprite;
typedef struct { int count; WsSprite tile[WS_TILES]; } WsPacket;
typedef struct {
    WsPacket build, display; uint8_t activated[256]; uint32_t activation_scene;
    /* Signed placements travel with the same DMA page as the packets. */
    int16_t build_oam_x[64],display_oam_x[64];
    uint8_t build_oam_kind[64],display_oam_kind[64];
} WsPresentation;
static WsPresentation ws;
static int enabled, extended=1, aspect=NES_VIDEO_FIT;
static uint64_t spawns, retains, sprite_overflow, failures;
static int camera_x, map_samples, map_errors;
static FILE *report;
static uint64_t report_frame;
static void frame_end(void *ctx);
static uint8_t coverage[CYC_VIDEO_MAX_WIDTH*240];
static int game_camera(void);
static int game_valid(void);
static int game_present_valid(void);
static int game_world_y(int y);
static uint8_t game_chr(uint16_t addr);
static void game_frame_end(void);
static int game_bg(int wx,int wy,int *tile,int *pal);
static int hook(uint16_t addr);
static void game_debug(char *out,size_t cap);
static void game_reset(void);
static void game_prepare_render(int width,int x0);
static int game_repair_pixel(int x,int y);
static int game_native_bg(int x,int y,int *tile,int *pal);
static void game_draw_native(uint32_t *out,int width,int x0);
static void game_dma(void);
static void enable(int on);
int mm1_widescreen_mode(void){return enabled?aspect:NES_VIDEO_STOCK;}

static int unwrap(int captured,int expected,int period) {
    int delta=(captured-expected+period/2)%period;
    if(delta<0)delta+=period;
    return expected+delta-period/2;
}
static void append_sprite(int x,int y,int tile,int attr) {
    if(ws.build.count>=WS_TILES){sprite_overflow++;return;}
    WsSprite *v=&ws.build.tile[ws.build.count++];
    v->x=(int16_t)x;v->y=(int16_t)y;v->tile=(uint8_t)tile;v->attr=(uint8_t)attr;
    v->owner=R(0x0C);v->pad=0;
}
static void dma_packet(void) {
    game_dma();ws.display=ws.build;ws.build.count=0;
    memcpy(ws.display_oam_x,ws.build_oam_x,sizeof ws.display_oam_x);
    memcpy(ws.display_oam_kind,ws.build_oam_kind,sizeof ws.display_oam_kind);
    memset(ws.build_oam_kind,0,sizeof ws.build_oam_kind);
}

/* Draw a frame's sprite through the stage's CHR mapping. Repaired native
 * pixels and the margins have no console left-column mask or byte-X wrapping. */
static void draw_tile(uint32_t *out,int width,int x0,int sx,int sy,int tile,int attr,int force) {
    if(sx+8<=0 || sx>=width || sy<0 || sy>=WS_PLAYFIELD_HEIGHT || !(cyc_render_line_mask(sy)&16))return;
    int tall=cyc_render_line_sprite16(sy),rows=tall?16:8;
    uint16_t table=cyc_render_line_sprite_table(sy);
    for(int r=0;r<rows;r++) {
        int y=sy+r;if(y>=WS_PLAYFIELD_HEIGHT)continue;
        int row=(attr&128)?rows-1-r:r;
        uint16_t pattern=tall?(uint16_t)(((tile&1)?4096:0)+(tile&254)*16+(row>=8?16:0)):(uint16_t)(table+tile*16);
        uint8_t lo=game_chr((uint16_t)(pattern+(row&7))),hi=game_chr((uint16_t)(pattern+(row&7)+8));
        for(int c=0;c<8;c++) {
            int x=sx+c;if(x<0 || x>=width)continue;
            if(!force && x>=x0 && x<x0+256 && !game_repair_pixel(x-x0,y))continue;
            int bit=(attr&64)?c:7-c,v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);
            if(!v || ((attr&32) && coverage[y*width+x]))continue;
            out[y*width+x]=cyc_render_color(16+(attr&3)*4+v);
        }
    }
}

static int render_inner(uint32_t *out,int width,int height,int x0,const uint32_t *native,void *user) {
    (void)user;(void)height;
    static uint32_t character[256*240];
    int menu=mm1_coop_menu_active();
    int replaced=menu?cyc_render_background(character):mm1_proto_native_picture(character);
    if(replaced)native=character;
    if(!enabled || !game_present_valid() || !(cyc_render_line_mask(80)&8)) {
        if(!replaced)return 0;
        for(int y=0;y<240;y++)memcpy(out+y*width+x0,native+y*256,256*sizeof *out);
        return 1;
    }
    camera_x=unwrap(cyc_render_line_scroll_x(80),game_camera(),WS_SCROLL_PERIOD);
    game_prepare_render(width,x0);
    memset(coverage,0,(size_t)width*240);
    for(int y=0;y<240;y++) {
        int wy=game_world_y(y);
        uint16_t table=cyc_render_line_bg_table(y);
        int last_wx=-9999,last_wy=-9999,tile=0,pal=0,valid=0;
        uint8_t lo=0,hi=0;
        for(int x=0;x<width;x++) {
            int nx=x-x0,in_native=nx>=0 && nx<256;
            if(in_native && !game_repair_pixel(nx,y)) {
                out[y*width+x]=native[y*256+x-x0];
                coverage[y*width+x]=cyc_frame_bg_opaque()[y*256+x-x0];
                continue;
            }
            out[y*width+x]=cyc_render_color(0);
            if(y>=WS_PLAYFIELD_HEIGHT) {out[y*width+x]=native[y*256];continue;}
            int wx=camera_x+x-x0;
            if((wx>>3)!=last_wx || (wy>>3)!=last_wy) {
                last_wx=wx>>3;last_wy=wy>>3;
                valid=in_native?game_native_bg(nx,y,&tile,&pal):game_bg(wx,wy,&tile,&pal);
                if(valid) {
                    lo=game_chr((uint16_t)(table+tile*16+(wy&7)));
                    hi=game_chr((uint16_t)(table+tile*16+(wy&7)+8));
                }
            }
            if(!valid)continue;
            int bit=7-(wx&7),v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);
            if(v) {out[y*width+x]=cyc_render_color(pal*4+v);coverage[y*width+x]=1;}
        }
    }
    /* Signed packets also repaint selected native pixels when byte-X sprites
     * have wrapped or the console's column guards interrupt the wide view. */
    if(menu)return 1;
    mm1_video_paint_dust(out,width,x0,coverage);
    for(int i=ws.display.count-1;i>=0;i--) {
        WsSprite *s=&ws.display.tile[i];
        if(replaced&&mm1_video_replaces_actor(s->owner)) {
            if(s->owner&& !mm1_video_actor_has_oam(s->owner)) {
                int first=1;for(int j=0;j<i;j++)if(ws.display.tile[j].owner==s->owner){first=0;break;}
                if(first)mm1_video_paint_actor(out,width,x0,coverage,s->owner);
            }
            continue;
        }
        int sx=s->x-camera_x+x0,sy=s->y;
        draw_tile(out,width,x0,sx,sy,s->tile,s->attr,0);
    }
    game_draw_native(out,width,x0);
    return 1;
}
static int render(uint32_t *out,int width,int height,int x0,const uint32_t *native,void *user) {
    int previous=mm1_coop_present_begin();
    if(game_render_views(out,width,height,native)){mm1_coop_present_end(previous);return 1;}
    int result=render_inner(out,width,height,x0,native,user);
    if(mm1_coop_menu_active()){if(!result){memset(out,0,width*height*sizeof(*out));for(int y=0;y<240;y++)memcpy(out+y*width+x0,native+y*256,256*sizeof(*out));}mm1_coop_menu_draw(out,width,-1);result=1;}
    mm1_coop_present_end(previous);return result;
}

/* Diagnose the decoder against the PPU's actual background coverage, including
 * the native mask. Sprites do not contaminate this comparison. */
static void verify_map(void) {
    map_samples=map_errors=0;
    if(!game_valid())return;
    int cx=unwrap(cyc_render_line_scroll_x(80),game_camera(),WS_SCROLL_PERIOD);
    for(int y=16;y<WS_PLAYFIELD_HEIGHT-16;y++)for(int x=16;x<240;x++) {
        int wx=cx+x,wy=game_world_y(y),tile,pal;
        if(!game_bg(wx,wy,&tile,&pal))continue;
        int bit=7-(wx&7),off=cyc_render_line_bg_table(y)+tile*16+(wy&7);
        int v=((game_chr((uint16_t)off)>>bit)&1)|(((game_chr((uint16_t)(off+8))>>bit)&1)<<1);
        int opaque=(v!=0) && (cyc_render_line_mask(y)&8);
        map_samples++;map_errors+=opaque!=cyc_frame_bg_opaque()[y*256+x];
    }
}
static void enable(int on) {
    enabled=on;
    mm1_video_character_changed();
    cyc_video_set_mode(on?aspect:NES_VIDEO_STOCK);
    mm1_coop_view_policy();
    if(!on)memset(&ws,0,sizeof ws);
}
void mm1_video_character_changed(void) {
    int character=mm1_proto_available()||mm1_charge_available()||mm1_slide_available()||mm1_coop_available();
    for(size_t i=0;i<sizeof(hook_ids)/sizeof(hook_ids[0]);i++)
        nes_mod_set_function_hook_enabled(hook_ids[i],enabled||mm1_coop_independent()||(character&&(i==0||i==1||i==2||i==6))||
            (mm1_weapon_cycle_available()&&(i==2||i==6)));
#ifdef MM1_PROFILE_USA
    nes_mod_set_function_hook_enabled("mm1.coop.spawn-candidate",mm1_coop_independent());
    nes_mod_set_function_hook_enabled("mm1.coop.view-hud",mm1_coop_independent());
#endif
    cyc_render_capture_background(character!=0);
    cyc_render_set_stock_compositing(character!=0);
    cyc_render_set_compositor(enabled||character?render:NULL,NULL);
    nes_mod_set_function_hook_enabled("mm1.proto.pose-done",character);
}
static void reset_mod(void) {enable(0);}
static void activate(void) {
    char a[32]="fit",e[32]="viewport";
    nes_mod_option_value(WS_PACKAGE,"widescreen","aspect",a,sizeof a);
    nes_mod_option_value(WS_PACKAGE,"widescreen","enemy_activation",e,sizeof e);
    nes_video_geometry_parse(a,&aspect);extended=!strcmp(e,"viewport");enable(1);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    if(!strcmp(name,"--widescreen")) {
        if(!strcmp(value,"off")){enable(0);return true;}
        int m;if(!nes_video_geometry_parse(value,&m)||m==NES_VIDEO_STOCK)return false;
        aspect=m;enable(1);return true;
    }
    if(!strcmp(name,"--widescreen-enemies")) {
        if(strcmp(value,"viewport") && strcmp(value,"classic"))return false;
        extended=!strcmp(value,"viewport");return true;
    }
    if(!strcmp(name,"--widescreen-report")) {report=fopen(value,"w");return report!=NULL;}
    return false;
}
static void power_on(void *ctx) {(void)ctx;memset(&ws,0,sizeof ws);report_frame=0;game_reset();game_views_reset();}
static int save_packets(uint8_t *buf,int cap) {
    if(cap<(int)sizeof ws)return -1;memcpy(buf,&ws,sizeof ws);return sizeof ws;
}
static int validate_packets(const uint8_t *buf,int len) {
    if(!buf)return len==0;
    int legacy=(int)offsetof(WsPresentation,build_oam_x);
    if(len!=(int)sizeof ws && len!=legacy)return 0;
    WsPresentation v={0};memcpy(&v,buf,(size_t)len);
    for(int i=0;i<256;i++)if(v.activated[i]>1)return 0;
    for(int i=0;i<64;i++)if(v.build_oam_kind[i]>2 || v.display_oam_kind[i]>2)return 0;
    return v.build.count>=0 && v.build.count<=WS_TILES && v.display.count>=0 && v.display.count<=WS_TILES;
}
static int load_packets(const uint8_t *buf,int len) {
    if(!validate_packets(buf,len))return 0;
    memset(&ws,0,sizeof ws);if(buf)memcpy(&ws,buf,(size_t)len);
    game_reset();return 1;
}
static void tcp_state(int id,const char *line) {
    bool verify=false;cyc_tcp_bool(line,"verify",&verify);if(verify)verify_map();
    char extra[1200],out[1800];game_debug(extra,sizeof extra);
    snprintf(out,sizeof out,"\"enabled\":%s,\"extended\":%s,\"width\":%d,\"scene\":%s,\"camera\":%d,"
             "\"sprites\":%d,\"spawns\":%llu,\"retains\":%llu,\"overflow\":%llu,\"failures\":%llu,"
             "\"terrain_samples\":%d,\"terrain_errors\":%d,%s",enabled?"true":"false",extended?"true":"false",
             cyc_video_width(),game_valid()?"true":"false",game_camera(),ws.display.count,(unsigned long long)spawns,
             (unsigned long long)retains,(unsigned long long)sprite_overflow,(unsigned long long)failures,map_samples,map_errors,extra);
    cyc_tcp_ok(id,out);
}
static void tcp_poke(int id,const char *line) {
    long addr,val;if(!cyc_tcp_long(line,"addr",&addr)||!cyc_tcp_long(line,"value",&val)||addr<0||addr>65535||val<0||val>255||!W(addr,val)) {
        cyc_tcp_err(id,"ws_poke: writable RAM addr and byte value");return;
    }
    cyc_render_frame_done();cyc_tcp_ok(id,NULL);
}
static void tcp_step(int id,const char *line) {
    long frames=1,pad=0;cyc_tcp_long(line,"frames",&frames);cyc_tcp_long(line,"pad",&pad);
    if(frames<1 || frames>600 || pad<0 || pad>255){cyc_tcp_err(id,"ws_step: frames 1..600, pad 0..255");return;}
    cyc_set_controller(0,(uint8_t)pad);
    for(long i=0;i<frames;i++){cyc_session_frame_begin();cyc_run_frame();cyc_session_frame_end();}
    cyc_set_controller(0,0);cyc_tcp_ok(id,NULL);
}
static void tcp_pixel(int id,const char *line) {
    long x=16,y=16;cyc_tcp_long(line,"x",&x);cyc_tcp_long(line,"y",&y);
    if(x<0||x>255||y<0||y>239){cyc_tcp_err(id,"ws_pixel: native x,y");return;}
    int wx=unwrap(cyc_render_line_scroll_x(y),game_camera(),WS_SCROLL_PERIOD)+x,wy=game_world_y(y),tile=0,pal=0;
    int ok=game_bg(wx,wy,&tile,&pal);char out[500];
    snprintf(out,sizeof out,"\"wx\":%d,\"wy\":%d,\"tile\":%d,\"pal\":%d,\"valid\":%d,\"captured_y\":%d,\"bg_table\":%u,\"mask\":%u,\"opaque\":%u,\"native\":%u,\"palette\":%u,\"lo\":%u,\"hi\":%u",
       wx,wy,tile,pal,ok,cyc_render_line_scroll_y(y),cyc_render_line_bg_table(y),cyc_render_line_mask(y),cyc_frame_bg_opaque()[y*256+x],cyc_frame_argb()[y*256+x],cyc_render_color(pal*4+1),game_chr(cyc_render_line_bg_table(y)+tile*16+(wy&7)),game_chr(cyc_render_line_bg_table(y)+tile*16+(wy&7)+8));
    cyc_tcp_ok(id,out);
}
static void tcp_background(int id,const char *line) {
    long x=0,y=0,w=8,h=8;cyc_tcp_long(line,"x",&x);cyc_tcp_long(line,"y",&y);
    cyc_tcp_long(line,"w",&w);cyc_tcp_long(line,"h",&h);
    if(x<0 || y<0 || w<1 || h<1 || x+w>256 || y+h>WS_PLAYFIELD_HEIGHT || w*h>2048) {
        cyc_tcp_err(id,"ws_background: native rectangle, at most 2048 pixels");return;
    }
    camera_x=unwrap(cyc_render_line_scroll_x(80),game_camera(),WS_SCROLL_PERIOD);
    char out[24000];size_t pos=(size_t)snprintf(out,sizeof out,"\"argb\":[");
    for(int r=0;r<h;r++)for(int c=0;c<w;c++) {
        int nx=(int)x+c,ny=(int)y+r,tile=0,pal=0,v=0;
        if(game_native_bg(nx,ny,&tile,&pal)) {
            int wx=camera_x+nx,wy=game_world_y(ny),bit=7-(wx&7),off=cyc_render_line_bg_table(ny)+tile*16+(wy&7);
            v=((game_chr((uint16_t)off)>>bit)&1)|(((game_chr((uint16_t)(off+8))>>bit)&1)<<1);
        }
        pos+=(size_t)snprintf(out+pos,sizeof out-pos,"%s%u",(r||c)?",":"",(unsigned)cyc_render_color(v?pal*4+v:0));
    }
    snprintf(out+pos,sizeof out-pos,"]");cyc_tcp_ok(id,out);
}
static void tcp_oam(int id,const char *line) {
    (void)line;const uint8_t *oam=cyc_render_oam();char out[24000];
    size_t pos=(size_t)snprintf(out,sizeof out,"\"camera\":%d,\"oam\":[",camera_x);
    for(int i=0;i<64;i++)pos+=(size_t)snprintf(out+pos,sizeof out-pos,"%s[%u,%u,%u,%u,%u,%d]",i?",":"",oam[i*4],oam[i*4+1],oam[i*4+2],oam[i*4+3],ws.display_oam_kind[i],ws.display_oam_x[i]);
    pos+=(size_t)snprintf(out+pos,sizeof out-pos,"],\"packets\":[");
    for(int i=0;i<ws.display.count;i++) {
        WsSprite *s=&ws.display.tile[i];
        pos+=(size_t)snprintf(out+pos,sizeof out-pos,"%s[%d,%d,%u,%u]",i?",":"",s->x,s->y,s->tile,s->attr);
    }
    snprintf(out+pos,sizeof out-pos,"]");cyc_tcp_ok(id,out);
}
static void tcp_setup(void *ctx) {
    (void)ctx;cyc_tcp_register("ws_state","adaptive renderer, actors and optional verify:true terrain check",tcp_state);
    cyc_tcp_register("ws_poke","test fixture: write a RAM byte",tcp_poke);
    cyc_tcp_register("ws_step","test fixture: advance frames while runtime menu is paused",tcp_step);
    cyc_tcp_register("ws_pixel","test fixture: decode a native background coordinate",tcp_pixel);
    cyc_tcp_register("ws_background","test fixture: background colors in a native rectangle",tcp_background);
    cyc_tcp_register("ws_oam","test fixture: latched OAM and its signed placement",tcp_oam);
}
static void frame_end(void *ctx) {
    uint64_t frame=++report_frame;(void)ctx;if(enabled||mm1_coop_independent())game_frame_end();if(!report || frame%120)return;
    verify_map();char extra[1200];game_debug(extra,sizeof extra);
    fprintf(report,"{\"frame\":%llu,\"scene\":%d,\"camera\":%d,\"sprites\":%d,\"spawns\":%llu,\"retains\":%llu,\"failures\":%llu,\"overflow\":%llu,\"terrain_samples\":%d,\"terrain_errors\":%d,%s}\n",
        (unsigned long long)frame,game_valid(),game_camera(),ws.display.count,(unsigned long long)spawns,(unsigned long long)retains,
        (unsigned long long)failures,(unsigned long long)sprite_overflow,map_samples,map_errors,extra);fflush(report);
}
static const CycHostOption options[]={
    {"--widescreen",true,"fit|16:9|21:9|32:9|off (experimental)"},
    {"--widescreen-enemies",true,"viewport|classic"},
    {"--widescreen-report",true,"FILE: periodic test diagnostics"}
};
const CycHostExtras *mm1_widescreen_extras(void) {
    static const CycHostExtras x={.power_on=power_on,.frame_end=frame_end,.options=options,.option_count=3,.option=option,.tcp_setup=tcp_setup};return &x;
}
NES_MOD_CONSTRUCTOR(register_widescreen) {
    nes_mod_register_reset_callback(reset_mod);
    nes_mod_register_activation_plugin(WS_PLUGIN,activate);
    for(size_t i=0;i<sizeof(hook_ids)/sizeof(hook_ids[0]);i++)nes_mod_register_function_entry_plugin(hook_ids[i],hook_addrs[i],hook);
#ifdef MM1_PROFILE_USA
    nes_mod_register_function_entry_plugin("mm1.coop.spawn-candidate",MM_SPAWN_OBJECT,hook);
    nes_mod_register_function_entry_plugin("mm1.coop.view-hud",0xD36E,hook);
#endif
    nes_mod_register_savestate_hook(WS_PLUGIN ".packets",save_packets,load_packets);
    nes_mod_register_savestate_validator(WS_PLUGIN ".packets",validate_packets);
}
