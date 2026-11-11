#include <stdint.h>
#include "widescreen_symbols.h"
#define WS_PLAYFIELD_HEIGHT 240
#define WS_SCROLL_PERIOD 512
#define WS_PACKAGE "mega-man.enhancement.widescreen"
#define WS_PLUGIN "mm1.widescreen"
static const char *hook_ids[]={"mm1.widescreen.draw","mm1.widescreen.emit","mm1.widescreen.build","mm1.widescreen.cull","mm1.widescreen.minor_cull","mm1.widescreen.spawn","mm1.widescreen.dma0"};
static const uint16_t hook_addrs[]={0xD174,0xD299,0xD137,0x9951,0xBE81,0xD8A8,MM_DMA0};
#include "widescreen_common.h"

static uint8_t level[0x4000],graphics[0x4000];
static int cached_stage=-1,graphics_ready;
static uint8_t repair[256*240];
static int draw_pending,draw_first,draw_camera;
static unsigned wrapped_oam,hud_oam;
static int game_camera(void) {return R(0x1A)|(R(0x1B)<<8);}
static int actor_x(int slot) {return R(0x480+slot)|(R(0x460+slot)<<8);}
static int game_valid(void) {
    return R(0x31)<10 && R(0x54)==32 && R(0x600)<248 && R(0x28)>=R(0x27) &&
           R(0x28)<48 && R(0x1B)>=R(0x27) && R(0x1B)<=R(0x28) &&
           !R(0x1E) && !(R(0x26)&1);
}
static int game_world_y(int y) {return y;}
static int game_present_valid(void) {return game_valid() && R(0x42)==5;}
static uint8_t game_chr(uint16_t addr) {return cyc_render_chr(addr);}
static void game_frame_end(void) {
    if(!game_valid()){ws.activation_scene=UINT32_MAX;return;}
    /* Fit can temporarily return to native width, where the wider loader is
     * bypassed. Retire records that have left the view during that interval. */
    if(!extended || cyc_video_width()>256 || !graphics_ready || cached_stage!=R(0x31))return;
    int stage=R(0x31),ptr=graphics[0x2452+stage*2]|(graphics[0x2453+stage*2]<<8);
    if(ptr<0x8000 || ptr>=0xC000)return;
    int cam=game_camera();
    for(int i=0;i<256 && ptr-0x8000+i*4+3<0x4000;i++) {
        uint8_t *v=&graphics[ptr-0x8000+i*4];if(v[0]>=48)break;
        int x=v[0]*256+v[1];if(x<cam-16 || x>=cam+272)ws.activated[i]=0;
    }
}
static void game_reset(void) {cached_stage=-1;graphics_ready=0;draw_pending=0;}
static int game_repair_pixel(int x,int y) {return repair[y*256+x];}
static int game_native_bg(int x,int y,int *tile,int *pal) {
    int px=(cyc_render_line_scroll_x(y)+x)&511,py=cyc_render_line_scroll_y(y);
    int nt=0x2000+(px/256)*1024+(py/240)*2048,cx=(px&255)/8,cy=(py%240)/8;
    *tile=cyc_render_nametable((uint16_t)(nt+cy*32+cx));
    int attr=cyc_render_nametable((uint16_t)(nt+960+(cy/4)*8+cx/4));
    *pal=(attr>>((cy&2)*2+(cx&2)))&3;return 1;
}
static void game_prepare_render(int width,int x0) {
    (void)width;(void)x0;memset(repair,0,sizeof repair);wrapped_oam=hud_oam=0;
    const uint8_t *oam=cyc_render_oam();
    for(int i=0;i<64;i++) {
        const uint8_t *o=oam+i*4;int kind=ws.display_oam_kind[i];
        int moved=kind==2 || (kind==1 && ws.display_oam_x[i]-camera_x!=o[3]);
        if(!moved || o[0]>=239)continue;
        wrapped_oam+=kind==1;hud_oam+=kind==2;
        int rows=cyc_render_line_sprite16(o[0]+1)?16:8;
        for(int y=o[0]+1;y<o[0]+1+rows && y<240;y++)for(int x=o[3];x<o[3]+8 && x<256;x++)repair[y*256+x]=1;
    }
}
static void game_draw_native(uint32_t *out,int width,int x0) {
    const uint8_t *oam=cyc_render_oam();
    for(int i=63;i>=0;i--) {
        const uint8_t *o=oam+i*4;if(o[0]>=239)continue;
        int kind=ws.display_oam_kind[i],x=o[3],force=0;
        if(kind==1){x=ws.display_oam_x[i]-camera_x;force=x!=o[3];}
        if(kind==2){x=ws.display_oam_x[i]-x0;force=1;}
        draw_tile(out,width,x0,x0+x,o[0]+1,o[1],o[2],force);
    }
}
static void game_dma(void) {
    /* Native HUD tiles are screen-space, never world-space actors. */
    for(int i=0;i<64;i++) {
        int a=0x200+i*4,x=R(a+3),y=R(a),tile=R(a+1),attr=R(a+2);
        if(y<24 || y>72)continue;
        if((tile>=0xFA && tile<=0xFE && ((x==24 && attr==1) || (x==40 && attr==2))) || (tile>=0xDB && tile<=0xDF && x==16 && attr==0)) {
            ws.build_oam_kind[i]=2;ws.build_oam_x[i]=(int16_t)(x-16);
        }
    }
}
static void capture_native_tile(void) {
    if(!enabled || !draw_pending)return;
    CycModRegs regs;cyc_mod_regs(&regs);
    int slot=regs.x/4+1,a=0x200+slot*4;
    if(slot>=64 || R(a)>=239)return;
    for(int n=draw_first;n<ws.build.count;n++) {
        WsSprite *s=&ws.build.tile[n];
        if(s->y==R(a)+1 && s->tile==R(a+1) && s->attr==R(a+2) && ((s->x-draw_camera)&255)==R(a+3)) {
            ws.build_oam_x[slot]=s->x;ws.build_oam_kind[slot]=1;break;
        }
    }
}
static int cache_rom(void) {
    int stage=R(0x31);
    if(cached_stage!=stage)graphics_ready=0;
    if(!graphics_ready && R(0x42)==6) {for(int i=0;i<0x4000;i++)graphics[i]=R(0x8000+i);graphics_ready=1;}
    if(cached_stage==stage)return 1;
    CycModRegs regs;cyc_mod_regs(&regs);
    if(!cyc_mod_isolate_begin())return 0;
    int ok=cyc_mod_call(0xC39C,&regs); /* SwitchBankStage */
    if(ok)for(int i=0;i<0x4000;i++)level[i]=R(0x8000+i);
    cyc_mod_isolate_end();
    if(ok){cached_stage=stage;}else failures++;
    return ok;
}
static int game_bg(int wx,int wy,int *tile,int *pal) {
    if(wx<0 || wy<0 || wy>=240 || wx/256<R(0x27) || wx/256>R(0x28) || !cache_rom())return 0;
    int room=level[0xC00+wx/256],ptr=level[0xC30+room*2]|(level[0xC31+room*2]<<8);
    if(ptr<0x8000 || ptr+64>0xC000)return 0;
    int block=level[ptr-0x8000+((wx&255)/32)*8+wy/32];
    int quad=((wx&16)?2:0)+((wy&16)?1:0);
    *tile=(level[block*4+quad]*4+((wx&8)?2:0)+((wy&8)?1:0))&255;
    *pal=(level[0x300+block]>>(((wy&16)?4:0)+((wx&16)?2:0)))&3;
    return 1;
}

static void capture_actor(void) {
    int slot=R(0x0C);if(slot>=32 || R(0x600+slot)>=248)return;
    int world=actor_x(slot),virtual_camera=world-128;
    CycModRegs regs;cyc_mod_regs(&regs);
    draw_first=ws.build.count;draw_camera=game_camera();
    draw_pending=1;
    if(!cyc_mod_isolate_begin())return;
    W(0x1A,virtual_camera);W(0x1B,virtual_camera>>8);W(0x0D,0);
    for(int i=0;i<64;i++)W(0x200+i*4,248);
    int ok=cyc_mod_call(0xD174,&regs);
    if(ok)for(int i=1;i<64;i++) {
        int a=0x200+i*4,y=R(a);
        if(y<239)append_sprite(virtual_camera+R(a+3),y+1,R(a+1),R(a+2));
    }
    cyc_mod_isolate_end();if(!ok)failures++;
}

/* Spawn only ordinary authored records in this horizontal strip. The native
 * initializer keeps their identity, attributes, HP and per-type setup. Special
 * generator and room-control records retain their native activation. */
static void load_visible(void) {
    if(!cache_rom() || R(0x3E))return;
    int cam=game_camera(),margin=(cyc_video_width()-256)/2;
    int left=cam-margin-16,right=cam+256+margin+16;
    int stage=R(0x31),ptr=graphics[0x2452+stage*2]|(graphics[0x2453+stage*2]<<8);
    if(ptr<0x8000 || ptr>=0xC000)return;
    uint32_t scene=(uint32_t)((stage<<12)|(R(0x27)<<6)|R(0x28));
    if(ws.activation_scene!=scene){memset(ws.activated,0,sizeof ws.activated);ws.activation_scene=scene;}
    CycModRegs regs;cyc_mod_regs(&regs);
    int indices[256],count=0;
    for(int i=0;i<256 && ptr-0x8000+i*4+3<0x4000;i++) {
        uint8_t *v=&graphics[ptr-0x8000+i*4];
        int x=v[0]*256+v[1];if(v[0]>=48)break;
        if(x<left || x>=right){ws.activated[i]=0;continue;}
        if((x>=cam && x<cam+256) || v[0]<R(0x27) || v[0]>R(0x28) || v[2]>=248 || v[3]==255)continue;
        int resident=0;
        for(int n=16;n<32;n++)if(R(0x600+n)<248 && R(0x6B+n)==i)resident=1;
        if(resident)ws.activated[i]=1;
        if(!resident && !ws.activated[i])indices[count++]=i;
    }
    if(!count || !cyc_mod_isolate_begin())return;
    int ok=1; /* This hook runs with the native object-definition bank mapped. */
    uint8_t before[0x800],after[0x800];memcpy(before,cyc_cpu_ram(),sizeof before);
    for(int k=0;k<count && ok;k++) {
        int i=indices[k];uint8_t *v=&graphics[ptr-0x8000+i*4];
        W(0,v[1]);W(1,v[2]);W(5,v[0]);regs.x=(uint8_t)i;regs.y=4;regs.a=v[3];
        ok=cyc_mod_call(0xD9B3,&regs);
    }
    memcpy(after,cyc_cpu_ram(),sizeof after);cyc_mod_isolate_end();
    if(!ok){failures++;return;}
    for(int n=16;n<32;n++)if(before[0x600+n]>=248 && after[0x600+n]<248)spawns++;
    for(int n=16;n<32;n++)if(after[0x600+n]<248)ws.activated[after[0x6B+n]]=1;
    for(int a=0;a<0x800;a++) {
        int allowed=(a>=0x7B && a<=0x8A)||(a>=0x400 && a<0x500)||(a>=0x590 && a<0x5A0)||(a>=0x600 && a<0x700);
        if(allowed && after[a]!=before[a])W(a,after[a]);
    }
}
static int hook(uint16_t addr) {
    if(!enabled)return 0;
    if(addr==MM_DMA0){dma_packet();return 0;}
    if(addr==0xD137){ws.build.count=0;memset(ws.build_oam_kind,0,sizeof ws.build_oam_kind);draw_pending=0;return 0;}
    if(!game_valid())return 0;
    if(addr==0xD174){capture_actor();return 0;}
    if(addr==0xD299){capture_native_tile();return 0;}
    if(!extended || cyc_video_width()<=256)return 0;
    if(addr==0xD8A8){load_visible();return 0;}
    if(addr==0x9951 || addr==0xBE81) {
        CycModRegs regs;cyc_mod_regs(&regs);
        int cam=game_camera(),margin=(cyc_video_width()-256)/2,x=actor_x(regs.x);
        if(regs.x<32 && x>=cam-margin-16 && x<cam+256+margin+16) {
            /* This is the BEQ after the high-byte screen subtraction, not a
             * subroutine. Change its condition and let the original branch run. */
            regs.a=0;regs.p=(uint8_t)((regs.p&~0x80)|2);cyc_mod_set_regs(&regs);retains++;
        }
    }
    return 0;
}
static void game_debug(char *out,size_t cap) {
    int actors=0,wings=0;for(int i=1;i<32;i++)if(R(0x600+i)<248){actors++;int x=actor_x(i)-game_camera();wings+=x<0||x>=256;}
    snprintf(out,cap,"\"stage\":%d,\"strip\":[%d,%d],\"bank\":%d,\"scroll_y\":%d,\"objects\":%d,\"actors\":%d,\"wing_actors\":%d,\"wrapped_oam\":%u,\"hud_oam\":%u",
             R(0x31),R(0x27),R(0x28),R(0x42),R(0x1E),R(0x54),actors,wings,wrapped_oam,hud_oam);
}
