#include <stdint.h>
#include "widescreen_symbols.h"
#define WS_PLAYFIELD_HEIGHT 240
#define WS_SCROLL_PERIOD 512
#define WS_PLUGIN "mm1.widescreen"
#include "mm1_weapon_gfx.h"
static int game_render_views(uint32_t *out,int width,int height,const uint32_t *native);
static void game_views_build(void);
static void game_views_latch(void);
static void game_views_reset(void);
static void game_shared_hud_paint(uint32_t *out,int width,int origin);
static void game_hud_begin(void);
static void game_hud_done(void);
static int game_shared_camera(int width,int *source);
static int game_sprite_camera(void);
static const char *hook_ids[]={"mm1.widescreen.draw","mm1.widescreen.emit","mm1.widescreen.build","mm1.widescreen.cull","mm1.widescreen.minor_cull","mm1.widescreen.spawn","mm1.widescreen.dma0"};
static const uint16_t hook_addrs[]={MM_DRAW,MM_EMIT,MM_BUILD,0x9951,0xBE81,MM_SPAWN,MM_DMA0};
#include "widescreen_common.h"

static uint8_t level[0x4000],graphics[0x4000];
typedef struct {uint8_t pending,stage;uint16_t camera;uint8_t nt[2048];} CameraRebase;
static CameraRebase camera_rebase;
static int cached_stage=-1,graphics_ready;
static uint8_t repair[256*240];
static int draw_pending,draw_first,draw_camera;
static unsigned wrapped_oam,hud_oam;
static int game_camera(void) {return R(0x1A)|(R(0x1B)<<8);}
static int actor_x(int slot) {return R(0x480+slot)|(R(0x460+slot)<<8);}
static int in_player_views(int x,int pad) {
    int left[2];if(!mm1_coop_view_rects(left))return 0;
    return (x+pad>=left[0]&&x-pad<left[0]+256)||(x+pad>=left[1]&&x-pad<left[1]+256);
}
static void shared_window(int *left,int *right) {
    int width=extended?mm1_net_simulation_width(cyc_video_width()):256;
    if(!enabled)width=256;
    int native=game_camera(),fallback=native-(width-256)/2;
    *left=mm1_coop_shared_left(fallback,width,R(0x27)*256,(R(0x28)+1)*256);*right=*left+width;
}
static int game_valid(void) {
    return R(0x31)<10 && mm1_coop_total()==32 && (mm1_coop_read(mm1_coop_anchor(),0x600)<248||mm1_coop_view_active()) && R(0x28)>=R(0x27) &&
           R(0x28)<48 && R(0x1B)>=R(0x27) && R(0x1B)<=R(0x28) &&
           !R(0x1E) && !(R(0x26)&1);
}
static int game_world_y(int y) {return y;}
static int game_present_valid(void) {return game_valid() && (R(0x42)==5||mm1_coop_inflight());}
static uint8_t game_chr(uint16_t addr) {return cyc_render_chr(addr);}
static void game_frame_end(void) {
    if(!game_valid()){ws.activation_scene=UINT32_MAX;return;}
    /* Fit can temporarily return to native width, where the wider loader is
     * bypassed. Retire records that have left the view during that interval. */
    if((!mm1_coop_independent()&&!mm1_coop_available()&&(!extended||mm1_net_simulation_width(cyc_video_width())>256))||!graphics_ready||cached_stage!=R(0x31))return;
    int stage=R(0x31),ptr=graphics[0x2452+stage*2]|(graphics[0x2453+stage*2]<<8);
    if(ptr<0x8000 || ptr>=0xC000)return;
    int cam=game_camera(),left=cam,right=cam+256;if(mm1_coop_available())shared_window(&left,&right);
    for(int i=0;i<256 && ptr-0x8000+i*4+3<0x4000;i++) {
        uint8_t *v=&graphics[ptr-0x8000+i*4];if(v[0]>=48)break;
        int x=v[0]*256+v[1];if(mm1_coop_independent()?!in_player_views(x,16):(x<left-16||x>=right+16))ws.activated[i]=0;
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
        int moved=kind==2 || (kind==1 && ws.display_oam_x[i]-game_sprite_camera()!=o[3]);
        if(!moved || o[0]>=239)continue;
        wrapped_oam+=kind==1;hud_oam+=kind==2;
        int rows=cyc_render_line_sprite16(o[0]+1)?16:8;
        for(int y=o[0]+1;y<o[0]+1+rows && y<240;y++)for(int x=o[3];x<o[3]+8 && x<256;x++)repair[y*256+x]=1;
    }
}
static void game_draw_native(uint32_t *out,int width,int x0) {
    const uint8_t *oam=cyc_render_oam();
    int source=camera_x;game_shared_camera(width,&source);int origin=x0+source-camera_x;
    int first=-1;for(int i=0;i<64;i++)if(mm1_proto_owns_slot(i)){first=i;break;}
    int pending=mm1_coop_picture(),insert=mm1_coop_insert_slot();
    for(int i=63;i>=0;i--) {
        if(pending&&i<=insert){mm1_coop_paint(out,width,origin,coverage);pending=0;}
        if(mm1_coop_hud_owns_slot(i))continue;
        if(mm1_proto_owns_slot(i)) {
            mm1_video_paint_slot(out,width,origin,coverage,i);
            continue;
        }
        const uint8_t *o=oam+i*4;if(o[0]>=239)continue;
        int kind=ws.display_oam_kind[i],x=o[3]+source-camera_x,force=source!=camera_x;
        if(kind==1){x=ws.display_oam_x[i]-game_sprite_camera();force=x!=o[3];}
        if(kind==2){x=ws.display_oam_x[i]-x0;force=1;}
        draw_tile(out,width,x0,x0+x,o[0]+1,o[1],o[2],force||first>=0||mm1_video_dust_visible()||mm1_coop_picture());
    }
    if(pending)mm1_coop_paint(out,width,origin,coverage);
}
static void game_dma(void) {
    /* Native HUD tiles are screen-space, never world-space actors. */
    for(int i=0;i<64;i++) {
        int a=0x200+i*4,x=R(a+3),y=R(a),tile=R(a+1),attr=R(a+2);
        if(mm1_sprites_hud_margin(x,y,tile,attr)) {
            ws.build_oam_kind[i]=2;ws.build_oam_x[i]=(int16_t)(x-16);
        }
    }
}
static void capture_native_tile(void) {
    if((!enabled&&!mm1_coop_available())||!draw_pending)return;
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
#ifdef MM1_PROFILE_USA
    /* Native-size co-op also needs authored object IDs for a distant room
       initiator. Its immutable bank cache cannot depend on a wide draw. */
    if(!graphics_ready) {
        CycModRegs r={0};cyc_mod_regs(&r);if(!cyc_mod_isolate_begin())return 0;
        int ok=cyc_mod_call(0xC413,&r);
        if(ok)for(int i=0;i<0x4000;i++)graphics[i]=R(0x8000+i);
        cyc_mod_isolate_end();if(!ok){failures++;return 0;}graphics_ready=1;
    }
#endif
    if(cached_stage==stage)return 1;
    CycModRegs regs;cyc_mod_regs(&regs);
    if(!cyc_mod_isolate_begin())return 0;
    int ok=cyc_mod_call(0xC39C,&regs); /* SwitchBankStage */
    if(ok)for(int i=0;i<0x4000;i++)level[i]=R(0x8000+i);
    cyc_mod_isolate_end();
    if(ok){cached_stage=stage;}else failures++;
    return ok;
}
int mm1_world_prepare(void){return cache_rom();}
int mm1_world_probe(int x,int y) {
    if(cached_stage!=R(0x31)||x<0||x>=48*256||y<0||y>=240)return -1;
    if(!R(0x68))for(int index=R(0x8E);index<256;index+=6) {
        int a=0x720+index,screen=R(a+2);if(screen>(x>>8))break;
        if(R(a+1)&&screen==(x>>8)&&(x&255)>=R(a+3)&&(!R(a+5)||(x&255)<R(a+5))&&
            y>=R(a+4)&&(!R(a+6)||y<R(a+6)))return R(a+1)|128;
    }
    int room=level[0xC00+(x>>8)],ptr=level[0xC30+room*2]|(level[0xC31+room*2]<<8);
    if(ptr<0x8000||ptr+64>0xC000)return -1;
    int block=level[ptr-0x8000+((x&255)/32)*8+y/32],quad=((x&16)?2:0)+((y&16)?1:0);
    return R(0xCD46+R(0x31)*4+(level[block*4+quad]>>6));
}
static int game_bg(int wx,int wy,int *tile,int *pal) {
    if(wx<0 || wy<0 || wy>=240 || wx/256<R(0x27) || wx/256>R(0x28) || !cache_rom())return 0;
    int room=level[0xC00+wx/256],ptr=level[0xC30+room*2]|(level[0xC31+room*2]<<8);
    if(ptr<0x8000 || ptr+64>0xC000)return 0;
    int block=level[ptr-0x8000+((wx&255)/32)*8+wy/32];
#ifdef MM1_PROFILE_USA
    if(!R(0x68))for(int index=0;index<256;index+=6) {
        int a=0x720+index,screen=R(a+2);if(screen>(wx>>8))break;
        if(R(a+1)==4&&screen==(wx>>8)&&(wx&255)>=R(a+3)&&(!R(a+5)||(wx&255)<R(a+5))&&
            wy>=R(a+4)&&(!R(a+6)||wy<R(a+6))) {
            block=R(((wx>>8)==R(0xCE49+R(0x31))?0xCE31:0xCE3D)+R(0x31));break;
        }
    }
#endif
    int quad=((wx&16)?2:0)+((wy&16)?1:0);
    *tile=(level[block*4+quad]*4+((wx&8)?2:0)+((wy&8)?1:0))&255;
    *pal=(level[0x300+block]>>(((wy&16)?4:0)+((wx&16)?2:0)))&3;
    return 1;
}
int mm1_world_exit_allowed(int direction,int camera,int flags) {
    if(direction<1||direction>4||(direction==1&&!(flags&16))||(camera&255)||!cache_rom())return 0;
    int order=R(0x29),screen=camera>>8;
    if(screen==R(0x27)&&order&&level[0xC70+order]&R(0x938B+direction))return 1;
    return screen==R(0x28)&&(level[0xC71+order]&R(0x938F+direction))!=0;
}
void mm1_world_cancel_rebase(void){memset(&camera_rebase,0,sizeof camera_rebase);}
int mm1_world_rebase(int camera) {
    if(camera==game_camera())return 1;if(camera<0||camera>47*256||!cache_rom()||!graphics_ready)return 0;
    CameraRebase next={0};next.pending=1;next.stage=R(0x31);next.camera=(uint16_t)camera;
    /* Keep the same rolling512 columns that native horizontal streaming
       prefetches128 pixels before the view. Commit at actual DMA, so an old
       physical OAM/background frame survives a logical owner change intact. */
    int start=(camera-128)&~31;
    for(int col=0;col<64;col++)for(int row=0;row<30;row++) {
        int wx=start+col*8,tile=0,pal=0;if(!game_bg(wx,row*8,&tile,&pal))continue;
        int nt=(wx>>8)&1,x=(wx>>3)&31;next.nt[nt*1024+row*32+x]=(uint8_t)tile;
        int at=nt*1024+960+(row/4)*8+x/4,shift=((row&2)?4:0)+((x&2)?2:0);
        next.nt[at]=(uint8_t)((next.nt[at]&~(3<<shift))|(pal<<shift));
    }
    CycModRegs r={0};cyc_mod_regs(&r);if(!cyc_mod_isolate_begin())return 0;
    W(0x1A,camera);W(0x1B,camera>>8);int ok=cyc_mod_call(0xCD72,&r),active=R(0x8E);cyc_mod_isolate_end();if(!ok)return 0;
    int stage=R(0x31),ptr=(graphics[0x2452+stage*2]|graphics[0x2453+stage*2]<<8)-0x8000,first=0;
    if(ptr<0||ptr>=0x4000)return 0;
    for(;first<255&&ptr+first*4+3<0x4000;first++) {
        const uint8_t *v=graphics+ptr+first*4;if(v[0]>=48||v[0]*256+v[1]>=camera)break;
    }
    camera_rebase=next;W(0x1A,camera);W(0x1B,camera>>8);W(0x8E,active);W(0x8B,first-1);W(0x8C,first);W(0x8D,1);return 1;
}
static void apply_camera_rebase(void) {
    if(!camera_rebase.pending)return;
    if(camera_rebase.stage==R(0x31)&&!cyc_mod_ciram_upload(0,camera_rebase.nt,sizeof camera_rebase.nt))failures++;
    camera_rebase.pending=0;
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
    int ok=cyc_mod_call(MM_DRAW,&regs);
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
    int cam=game_camera(),margin=(mm1_net_simulation_width(cyc_video_width())-256)/2;
    int left=cam-margin,right=cam+256+margin;if(mm1_coop_available())shared_window(&left,&right);left-=16;right+=16;
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
        if((!mm1_coop_available()&&x>=cam&&x<cam+256)||v[0]<R(0x27)||v[0]>R(0x28)||v[2]>=248||v[3]==255)continue;
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
        ok=cyc_mod_call(MM_SPAWN_OBJECT,&regs);
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
static void load_independent(void) {
    if(!cache_rom()||!graphics_ready||R(0x3E))return;
    int stage=R(0x31),ptr=graphics[0x2452+stage*2]|(graphics[0x2453+stage*2]<<8);
    if(ptr<0x8000||ptr>=0xC000)return;
    uint32_t scene=(uint32_t)((stage<<12)|(R(0x27)<<6)|R(0x28));
    if(ws.activation_scene!=scene){memset(ws.activated,0,sizeof ws.activated);ws.activation_scene=scene;}
    for(int id=0;id<256&&ptr-0x8000+id*4+3<0x4000;id++) {
        const uint8_t *v=&graphics[ptr-0x8000+id*4];if(v[0]>=48)break;
        int x=v[0]*256+v[1];
        if(!in_player_views(x,16)){ws.activated[id]=0;continue;}
        if(v[0]<R(0x27)||v[0]>R(0x28)||v[2]>=248||v[3]==255||ws.activated[id])continue;
        int reserved=0,free_slot=0;
        for(int slot=16;slot<32;slot++) {
            if(R(0x6B+slot)==id)reserved=1;
            if(R(0x600+slot)>=248)free_slot=1;
        }
        if(reserved){ws.activated[id]=1;continue;}if(!free_slot)continue;
        int owner=mm1_coop_nearest(x,v[2]);CycModRegs r={0};cyc_mod_regs(&r);
        if(!cyc_mod_isolate_begin()){failures++;continue;}
        if(owner>=0)mm1_coop_spawn_project(owner);
        W(0,v[1]);W(1,v[2]);W(5,v[0]);r.x=(uint8_t)id;r.y=4;r.a=v[3];
        int ok=cyc_mod_call(MM_SPAWN_OBJECT,&r),found=-1;uint8_t fields[16],identity=0,sound_state=0;
        for(int slot=16;ok&&slot<32;slot++)if(R(0x600+slot)<248&&R(0x6B+slot)==id){found=slot;break;}
        if(found>=0) {
            for(int f=0;f<16;f++)fields[f]=R((f<8?0x400+f*32:0x600+(f-8)*32)+found);
            identity=R(0x6B+found);sound_state=R(0x580+found);
        }
        cyc_mod_isolate_end();
        if(!ok){failures++;continue;}
        if(found>=0) {
            for(int f=0;f<16;f++)W((f<8?0x400+f*32:0x600+(f-8)*32)+found,fields[f]);
            W(0x6B+found,identity);W(0x580+found,sound_state);ws.activated[id]=1;spawns++;
        }
    }
}
static int hook(uint16_t addr) {
    if(addr==0xD36E&&!cyc_mod_isolated()) {
        if(mm1_coop_independent()){int left[2];if(mm1_coop_view_rects(left))return 1;}
        game_hud_begin();
    }
    if(addr==0xD3E9&&!cyc_mod_isolated())game_hud_done();
    if(mm1_coop_drawing()) {
        if(addr==MM_BUILD)mm1_proto_draw_begin();
        if(addr==MM_DRAW)mm1_proto_pose_begin();
        if(addr==MM_EMIT){CycModRegs r={0};cyc_mod_regs(&r);mm1_proto_part_emitted(r.x/4+1);}
        return 0;
    }
    if(addr==MM_DMA0){apply_camera_rebase();int previous=mm1_coop_dma_begin();mm1_weapon_gfx_latch();if(!mm1_coop_independent_scene())mm1_proto_draw_latch();mm1_coop_video_latch();game_views_latch();mm1_sprites_latch();
        mm1_coop_present_end(previous);if(enabled||mm1_coop_available())dma_packet();return 0;}
    if(addr==MM_BUILD){
        mm1_coop_intro_draw();
        mm1_coop_draw_begin();
        mm1_coop_primary_begin();
        mm1_weapon_gfx_begin();
        mm1_slide_restore_input();
        if(mm1_coop_independent_scene())mm1_coop_views_draw();else mm1_proto_draw_begin();
        game_views_build();
        mm1_sprites_build();
        ws.build.count=0;memset(ws.build_oam_kind,0,sizeof ws.build_oam_kind);draw_pending=0;return 0;
    }
    if(addr==MM_DRAW){mm1_coop_primary_body();if(mm1_coop_draw_suppressed()&&(R(0x0C)==0||(R(0x0C)>=2&&R(0x0C)<16)))return 1;
        if((enabled||mm1_coop_available())&&game_valid())capture_actor();mm1_proto_pose_begin();return 0;}
    if(addr==MM_EMIT){
        if((enabled||mm1_coop_available())&&game_valid())capture_native_tile();
        CycModRegs regs;cyc_mod_regs(&regs);mm1_proto_part_emitted(regs.x/4+1);return 0;
    }
    if(addr==MM_SPAWN_OBJECT&&mm1_coop_available()&&!cyc_mod_isolated()&&game_valid()) {
        CycModRegs r={0};cyc_mod_regs(&r);if(r.a==255||R(1)>=248)return 0;
        int x=R(5)*256+R(0),left,right;shared_window(&left,&right);
        return (mm1_coop_independent()?!in_player_views(x,16):(x<left-16||x>=right+16))||ws.activated[r.x];
    }
    if(!enabled&&!mm1_coop_available())return 0;
    if(!game_valid())return 0;
    if(!mm1_coop_available()&&(!extended||mm1_net_simulation_width(cyc_video_width())<=256))return 0;
    if(addr==MM_SPAWN){if(mm1_coop_independent())load_independent();else load_visible();return 0;}
    if(addr==0x9951 || addr==0xBE81) {
        CycModRegs regs;cyc_mod_regs(&regs);
        int cam=game_camera(),margin=(mm1_net_simulation_width(cyc_video_width())-256)/2,x=actor_x(regs.x),left=cam-margin,right=cam+256+margin;
        if(mm1_coop_available())shared_window(&left,&right);
        if(regs.x<32 && (mm1_coop_independent()?in_player_views(x,16):(x>=left-16&&x<right+16))) {
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

typedef struct {uint8_t x,y,attr,height,pattern[32],palette[4];} ViewHud;
typedef struct {
    uint8_t active,native_scene,odd,stage,ready,hud_count[2],native_script,hud_shared,shared_camera,temporary_bg;
    int16_t camera,left[2];uint8_t tile[2][30][33],palette[2][30][33];ViewHud hud[2][32];
    int16_t focus,room_left,room_right;
    uint64_t hud_mask;uint8_t hud_oam[256];
} ViewFrame;
static ViewFrame view_build,view_shown;
static int game_sprite_camera(void) {
    /* Wily moves his large background through B2/B3. Native sprite placement
       still subtracts $1A, so that visual scroll must not drag either player. */
    return view_shown.stage==R(0x31)&&view_shown.temporary_bg?view_shown.camera:camera_x;
}
static int game_shared_camera(int width,int *source) {
    if(!mm1_coop_available()||mm1_coop_independent()||!view_shown.shared_camera||view_shown.stage!=R(0x31))return 0;
    *source=unwrap(cyc_render_line_scroll_x(80),view_shown.camera,WS_SCROLL_PERIOD);
    int left=view_shown.room_left,right=view_shown.room_right;
    if(right-left<width)return left+(right-left-width)/2;
    int view=view_shown.focus-width/2;if(view<left)view=left;if(view>right-width)view=right-width;return view;
}
static int16_t hud_cursor=-1;
static uint8_t hud_draw;
static uint32_t view_native[256*240],view_picture[2][256*240];
static void game_views_reset(void){memset(&view_build,0,sizeof view_build);memset(&view_shown,0,sizeof view_shown);hud_cursor=-1;hud_draw=0;}
static void game_hud_begin(void){hud_cursor=R(0x0D);hud_draw=(uint8_t)(R(0xBB)<128);}
static void game_hud_done(void) {
    if(hud_cursor<0)return;
    CycModRegs r={0};cyc_mod_regs(&r);int last=r.y/4+(r.y==248);
    if(hud_draw)for(int i=hud_cursor/4+1;i<=last&&i<64;i++) {
        view_build.hud_mask|=1ull<<i;
        for(int b=0;b<4;b++)view_build.hud_oam[i*4+b]=R(0x200+i*4+b);
    }
    hud_cursor=-1;hud_draw=0;
}
int mm1_coop_hud_active(void){return mm1_coop_available()&&!mm1_coop_independent()&&view_shown.hud_shared&&view_shown.stage==R(0x31)&&R(0x31)<10&&!mm1_coop_menu_active();}
int mm1_coop_hud_owns_slot(int slot) {
    return slot>=0&&slot<64&&mm1_coop_hud_active()&&(view_shown.hud_mask&(1ull<<slot))&&
        !memcmp(cyc_render_oam()+slot*4,view_shown.hud_oam+slot*4,4);
}
void mm1_world_views_reset(void){game_views_reset();memset(ws.activated,0,sizeof ws.activated);ws.activation_scene=UINT32_MAX;}
static void view_hud(int seat) {
    int weapon=mm1_weapon_identity_for(seat);CycModRegs r={0};cyc_mod_regs(&r);
    if(!cyc_mod_isolate_begin()){failures++;return;}
    for(int a=0x6A;a<=0x71;a++)W(a,mm1_coop_read(seat,(uint16_t)a));
    W(0x5F,weapon);W(0x3E1,mm1_coop_read(seat,0x3E1));W(0x3E2,mm1_coop_read(seat,0x3E2));
    W(0x0D,0);for(int i=0;i<64;i++)W(0x200+i*4,248);
    int ok=cyc_mod_call(0xC413,&r)&&cyc_mod_call(0xD36E,&r);
    if(ok) {
        int end=R(0x0D)/4;
        if(end>32)ok=0;
        for(int i=1;ok&&i<=end;i++) {
            const int a=0x200+i*4;ViewHud *p=&view_build.hud[seat][view_build.hud_count[seat]++];
            int tile=R(a+1);p->y=R(a);p->x=R(a+3);p->attr=R(a+2);p->height=R(0xFF)&32?16:8;
            unsigned pattern=p->height==16?(tile&1)*0x1000+(tile&254)*16:((R(0xFF)&8)?0x1000:0)+tile*16;
            for(int b=0;b<p->height*2;b++)p->pattern[b]=cyc_render_chr((uint16_t)(pattern+b));
            for(int c=0;c<4;c++)p->palette[c]=R(0x3E0+(p->attr&3)*4+c)&63;
        }
    }
    cyc_mod_isolate_end();if(!ok){view_build.hud_count[seat]=0;failures++;}
}
static void game_views_build(void) {
    memset(&view_build,0,sizeof view_build);view_build.ready=1;hud_cursor=-1;hud_draw=0;
    view_build.stage=R(0x31);view_build.camera=(int16_t)game_camera();view_build.temporary_bg=(uint8_t)(R(0xB4)!=0);
    if(!mm1_coop_independent()) {
        if(mm1_coop_view_active()&&R(0x31)<10&&R(0x54)==32) {
            view_build.stage=R(0x31);view_build.hud_shared=1;
            int focus;if(!mm1_coop_view_locked()&&mm1_coop_shared_focus(&focus)) {
                view_build.shared_camera=1;view_build.focus=(int16_t)focus;
                view_build.camera=(int16_t)game_camera();view_build.room_left=R(0x27)*256;view_build.room_right=(R(0x28)+1)*256;
            }
            for(int seat=0;seat<2;seat++)view_hud(seat);
        }return;
    }
    int left[2];if(!mm1_coop_view_rects(left)||!cache_rom()||cached_stage!=R(0x31))return;
    view_build.active=1;view_build.native_scene=(uint8_t)mm1_coop_view_locked();view_build.odd=R(0x23)&1;view_build.stage=R(0x31);
    view_build.native_script=(uint8_t)!mm1_coop_independent_scene();
    view_build.camera=(int16_t)game_camera();
    for(int seat=0;seat<2;seat++) {
        view_build.left[seat]=(int16_t)left[seat];
        if(!view_build.native_scene)for(int y=0;y<30;y++)for(int x=0;x<33;x++) {
            int tile=0,palette=0;game_bg((left[seat]&~7)+x*8,y*8,&tile,&palette);
            view_build.tile[seat][y][x]=(uint8_t)tile;view_build.palette[seat][y][x]=(uint8_t)palette;
        }
        if(!view_build.native_script)view_hud(seat);
    }
}
static void game_views_latch(void){if(view_build.ready){view_shown=view_build;view_build.ready=0;}}
static void hud_part_paint(uint32_t *out,int width,const ViewHud *p,int dx,int dy,int red) {
        for(int y=0;y<p->height;y++)for(int x=0;x<8;x++) {
            int py=p->y+1+y+dy,px=p->x+x+dx;if(py<0||py>=240||px<0||px>=width||!(cyc_render_line_mask(py)&16))continue;
            int row=p->attr&128?p->height-1-y:y,bit=p->attr&64?x:7-x,at=(row/8)*16+(row&7);
            int v=((p->pattern[at]>>bit)&1)|(((p->pattern[at+8]>>bit)&1)<<1);
            if(v&&(!(p->attr&32)||!coverage[py*width+px])) {
                uint32_t color=cyc_render_color_value(p->palette[v]);
                if(red&&(color&0xFFFFFFu)) {
                    unsigned r=(color>>16)&255,g=(color>>8)&255,b=color&255;
                    color=0xFF000000u|((r*3+255)/4<<16)|((g*3+64)/4<<8)|(b*3+64)/4;
                }out[py*width+px]=color;
            }
        }
}
static void view_hud_paint(uint32_t *out,int seat) {
    for(int i=view_shown.hud_count[seat]-1;i>=0;i--)hud_part_paint(out,256,&view_shown.hud[seat][i],0,0,0);
}
static void game_shared_hud_paint(uint32_t *out,int width,int origin) {
    if(!mm1_coop_hud_active())return;
    for(int seat=0;seat<2;seat++)for(int i=view_shown.hud_count[seat]-1;i>=0;i--) {
        const ViewHud *p=&view_shown.hud[seat][i];
        int life=p->x==24&&p->attr==1&&p->y>=24&&p->y<=72;
        int weapon=p->x==16&&p->attr==0&&p->y>=24&&p->y<=72;
        int boss=p->x==40&&p->attr==2&&p->y>=24&&p->y<=72;
        if(life||weapon)hud_part_paint(out,width,p,life?-16:8,-5+seat*76,life&&(seat^mm1_coop_roster()));
        else if(!seat)hud_part_paint(out,width,p,boss?width-56:origin,boss?-5:0,0);
    }
}
static void view_scene(uint32_t *out,int seat) {
    int left=view_shown.left[seat],phase=left&7;
    for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
        int at=y*256+x;
        if(view_shown.native_scene){out[at]=view_native[at];coverage[at]=cyc_frame_bg_opaque()[at];continue;}
        int row=cyc_render_line_scroll_y(y)%240,cell=(phase+x)/8;
        int tile=view_shown.tile[seat][row/8][cell],pattern=cyc_render_line_bg_table(y)+tile*16+(row&7),bit=7-((phase+x)&7);
        int v=((game_chr((uint16_t)pattern)>>bit)&1)|(((game_chr((uint16_t)(pattern+8))>>bit)&1)<<1);
        if(!(cyc_render_line_mask(y)&8))v=0;
        out[at]=cyc_render_color(v?view_shown.palette[seat][row/8][cell]*4+v:0);coverage[at]=(uint8_t)(v!=0);
    }
    if(mm1_coop_menu_active()){mm1_coop_menu_draw(out,256,seat);return;}
    if(mm1_sprites_ready()){mm1_sprites_draw(out,256,0,left,seat,0,coverage);return;}
    if(!view_shown.odd)view_hud_paint(out,seat);
    for(int n=0;n<32;n++) {
        int actor=view_shown.odd?31-n:n;
        if(actor==0||(actor>=2&&actor<16))mm1_coop_paint_actor(out,256,view_shown.camera-left,coverage,actor);
        else for(int i=ws.display.count-1;i>=0;i--) {
            const WsSprite *p=&ws.display.tile[i];if(p->owner!=actor)continue;
            draw_tile(out,256,0,p->x-left,p->y,p->tile,p->attr,1);
        }
    }
    if(view_shown.odd)view_hud_paint(out,seat);
}
static int game_render_views(uint32_t *out,int width,int height,const uint32_t *native) {
    int mode=mm1_coop_view_mode();
    if(!mm1_coop_independent()||!view_shown.active||view_shown.stage!=R(0x31)||height!=240||width!=(mode==1?512:256))return 0;
    /* A team death belongs to the real native coroutine. Its puffs/ring and
       hidden HUD replace the personal gameplay packets together at DMA. */
    if(view_shown.native_script) {
        for(int y=0;y<240;y++)for(int panel=0;panel<(mode==1?2:1);panel++)
            memcpy(out+y*width+panel*256,native+y*256,256*sizeof *out);
        cyc_render_set_native_origin(0);return 1;
    }
    if(!cyc_render_background(view_native))return 0;
    if(mode==1) {
        view_scene(view_picture[0],0);view_scene(view_picture[1],1);
        for(int y=0;y<240;y++)for(int seat=0;seat<2;seat++)memcpy(out+y*512+seat*256,view_picture[seat]+y*256,256*sizeof *out);
    }else view_scene(out,mode==3);
    cyc_render_set_native_origin(0);return 1;
}
typedef struct {char magic[4];ViewFrame build,shown;CameraRebase rebase;int16_t hud_cursor;uint8_t hud_draw;} ViewsState;
static int views_get(uint8_t *out,int cap) {
    if(cap<(int)sizeof(ViewsState))return -1;ViewsState s={0};memcpy(s.magic,"MV06",4);s.build=view_build;s.shown=view_shown;s.rebase=camera_rebase;s.hud_cursor=hud_cursor;s.hud_draw=hud_draw;
    memcpy(out,&s,sizeof s);return sizeof s;
}
static int views_valid(const uint8_t *in,int size) {
    if(!size)return 1;ViewsState s;if(!in||size!=sizeof s)return 0;memcpy(&s,in,sizeof s);if(memcmp(s.magic,"MV06",4)||s.rebase.pending>1||s.hud_draw>1||s.hud_cursor< -1||s.hud_cursor>248||(s.hud_cursor>=0&&(s.hud_cursor&3))||
        (s.rebase.pending&&(s.rebase.stage>=10||s.rebase.camera>47*256)))return 0;
    const ViewFrame *v[]={&s.build,&s.shown};for(int k=0;k<2;k++) {
        if(v[k]->active>1||v[k]->native_scene>1||v[k]->native_script>1||v[k]->hud_shared>1||v[k]->shared_camera>1||v[k]->temporary_bg>1||v[k]->odd>1||v[k]->ready>1||((v[k]->active||v[k]->hud_shared)&&v[k]->stage>=10))return 0;
        if(v[k]->shared_camera&&(v[k]->focus<0||v[k]->focus>=48*256||v[k]->room_left<0||v[k]->room_right>48*256||v[k]->room_right<=v[k]->room_left))return 0;
        for(int seat=0;seat<2;seat++) {
            if(v[k]->hud_count[seat]>32||v[k]->left[seat]<0||v[k]->left[seat]>47*256)return 0;
            for(int y=0;y<30;y++)for(int x=0;x<33;x++)if(v[k]->palette[seat][y][x]>3)return 0;
            for(int i=0;i<v[k]->hud_count[seat];i++) {
                const ViewHud *p=&v[k]->hud[seat][i];if(p->height!=8&&p->height!=16)return 0;
                for(int c=0;c<4;c++)if(p->palette[c]>63)return 0;
            }
        }
    }return 1;
}
static int views_set(const uint8_t *in,int size) {
    if(!views_valid(in,size))return 0;if(size){ViewsState s;memcpy(&s,in,size);view_build=s.build;view_shown=s.shown;camera_rebase=s.rebase;hud_cursor=s.hud_cursor;hud_draw=s.hud_draw;}
    else {game_views_reset();mm1_world_cancel_rebase();}return 1;
}
NES_MOD_CONSTRUCTOR(register_mm1_views) {
    nes_mod_register_savestate_hook("mm1.coop.views",views_get,views_set);nes_mod_register_savestate_validator("mm1.coop.views",views_valid);
}
