/* Replace the native body's pixels without changing its animation or CHR. */
#include "mm1_proto_video.h"
#include "mm1_proto.h"
#include "mm1_reflect.h"
#include "mm1_charge.h"
#include "mm1_slide.h"
#include "mm1_coop.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <string.h>
#include <stdio.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define W(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))

typedef struct {
    uint64_t mask;
    int16_t x,y;
    uint8_t pose,right,behind,weapon,replace,proto,teleport,flash,slide,donor_pose,light,accent;
    uint8_t oam[256];
} BodyFrame;
typedef struct {uint64_t mask;int16_t x,y;uint8_t sprite,right,replace,pad;uint8_t oam[256];} ShotFrame;
typedef struct {int16_t x,y;uint8_t on,right,age,pad;} DustFrame;
typedef struct {uint8_t on,x,y,pad;} PortraitFrame;
typedef struct {
    BodyFrame building,shown;ShotFrame building_shot[3],shown_shot[3];uint8_t page[256],building_page[256];
    DustFrame building_dust,shown_dust;
    PortraitFrame portrait;
    uint8_t restoring,invulnerability;
} Presentation;
static Presentation videos[2];
#define video (videos[mm1_coop_seat()])
static int private_paint;
static const uint8_t *display_oam(void){return private_paint?video.page:cyc_render_oam();}
static int valid_picture(void);
static int valid_shot(int actor);
static int valid_portrait(void);
int mm1_controls_menu(void);
void mm1_proto_video_reset(void){memset(videos,0,sizeof videos);private_paint=0;}
void mm1_proto_video_debug(char *out,int cap) {
    BodyFrame *f=&video.shown;
    snprintf(out,(size_t)cap,"\"pose\":%u,\"mask\":%llu,\"anchor\":[%d,%d],\"picture\":%d",
        f->pose,(unsigned long long)f->mask,f->x,f->y,valid_picture());
}

static void restore_invulnerability(void) {
    if(video.restoring){W(0x55,video.invulnerability);video.restoring=0;}
}
void mm1_proto_draw_begin(void) {
    restore_invulnerability();memset(&video.building,0,sizeof video.building);
    memset(video.building_shot,0,sizeof video.building_shot);
    memset(&video.building_dust,0,sizeof video.building_dust);
    int x,y,right,age;if(mm1_slide_dust(&x,&y,&right,&age)) {
        DustFrame *d=&video.building_dust;d->on=1;d->x=(int16_t)(x-(R(0x1B)*256+R(0x1A)));
        d->y=(int16_t)y;d->right=(uint8_t)right;d->age=(uint8_t)age;
    }
}
void mm1_proto_pose_begin(void) {
    int actor=R(0x0C);
    if(mm1_charge_available()&&actor>=2&&actor<=4) {
        ShotFrame *f=&video.building_shot[actor-2];int sprite=mm1_charge_sprite(actor);
        f->replace=(uint8_t)(sprite>=0);f->sprite=(uint8_t)(sprite<0?0:sprite);
        f->x=(int16_t)((R(0x460+actor)*256+R(0x480+actor))-(R(0x1B)*256+R(0x1A)));
        f->y=(int16_t)(R(0x600+actor)+1);f->right=(uint8_t)((R(0x420+actor)&64)!=0);return;
    }
    if((!mm1_proto_available()&&!mm1_slide_available())||R(0x0C)||R(0x31)>=10||
        (R(0x54)!=32&&!(mm1_coop_drawing()&&R(0x54)==16)))return;
    int pose=mm1_proto_pose_id();BodyFrame *f=&video.building;
    f->replace=(uint8_t)((mm1_proto_selected()||mm1_proto_teleporting())&&pose>=0);
    f->slide=(uint8_t)(mm1_slide_active()&&!mm1_proto_selected());
    f->donor_pose=(uint8_t)mm1_slide_pose_index();
    if(f->slide)f->replace=1;
    /* Native hurt animation also emits separate sparks. Leave those alone. */
    if(!f->slide&&R(0x400)+(R(0x640)>>4)==0x12) {
        int type=0x12,ptr=R(0x8186+type*2)|(R(0x8187+type*2)<<8);
        if(R(ptr+(R(0x440)>>4)+1)==0x19)f->replace=0;
    }
    f->pose=(uint8_t)(pose<0?0:pose);
    f->x=(int16_t)((R(0x460)*256+R(0x480))-(R(0x1B)*256+R(0x1A)));
    f->y=(int16_t)(R(0x600)+1+mm1_proto_beam_offset());
    f->right=(uint8_t)mm1_proto_right();f->weapon=(uint8_t)mm1_weapon_identity();
    f->proto=(uint8_t)mm1_proto_selected();f->teleport=(uint8_t)mm1_proto_teleporting();
    f->flash=(uint8_t)mm1_charge_flashing();
    f->light=R(0x3E1);f->accent=R(0x3E2);
    /* The simulation keeps its native damage guard. Only this drawing call
     * gets visible parts so the transition has an actual OAM ownership record. */
    if(f->teleport&&f->replace){video.invulnerability=R(0x55);video.restoring=1;W(0x55,0);}
}
void mm1_proto_part_emitted(int slot) {
    int actor=R(0x0C);
    if(actor>=2&&actor<=4&&slot>=0&&slot<64&&video.building_shot[actor-2].replace) {
        video.building_shot[actor-2].mask|=1ull<<slot;return;
    }
    if(R(0x0C)||!video.building.replace||slot<0||slot>=64)return;
    BodyFrame *f=&video.building;f->mask|=1ull<<slot;
    /* Native poses can put an individual part behind terrain. The custom
     * whole body inherits the stage's actor policy, never its last part. */
    f->behind=(uint8_t)((R(0x13)&32)!=0);
}
void mm1_proto_pose_done(void) {
    int actor=R(0x0C);
    if(actor>=2&&actor<=4) {
        ShotFrame *f=&video.building_shot[actor-2];
        for(int slot=0;slot<64;slot++)if(f->mask&(1ull<<slot))
            for(int k=0;k<4;k++)f->oam[slot*4+k]=R(0x200+slot*4+k);
        return;
    }
    if(R(0x0C))return;
    for(int slot=0;slot<64;slot++)if(video.building.mask&(1ull<<slot))
        for(int k=0;k<4;k++)video.building.oam[slot*4+k]=R(0x200+slot*4+k);
    restore_invulnerability();
}
void mm1_proto_draw_latch(void) {
    restore_invulnerability();
    if(mm1_coop_primary_publish()) {
        video.shown=video.building;memcpy(video.shown_shot,video.building_shot,sizeof video.shown_shot);
        video.shown_dust=video.building_dust;
    }
    for(int k=0;k<256;k++)video.page[k]=R(0x200+k);
    /* The retail inventory writes its four face sprites directly. Keep its
     * exact 16x16 grid, X scroll correction and surrounding life digits. */
    PortraitFrame *p=&video.portrait;memset(p,0,sizeof *p);
    if(mm1_proto_selected()&&mm1_controls_menu()) {
        static const uint8_t tiles[]={0xED,0xED,0xEE,0xEE},attrs[]={0,64,1,65};
        int x=R(0x2EB),ok=1;
        for(int i=0;i<4;i++)if(R(0x2E8+i*4)!=160+(i/2)*8||R(0x2E9+i*4)!=tiles[i]||
            R(0x2EA+i*4)!=attrs[i]||R(0x2EB+i*4)!=x+(i&1)*8)ok=0;
        if(ok){p->on=1;p->x=(uint8_t)x;p->y=161;}
    }
    for(int slot=0;slot<64;slot++)if(video.shown.mask&(1ull<<slot))
        for(int k=0;k<4;k++)if(video.shown.oam[slot*4+k]!=R(0x200+slot*4+k)){
            video.shown.mask=0;return;
        }
}
static int valid_picture(void) {
    BodyFrame *f=&video.shown;
    if(!f->replace||!f->mask||mm1_controls_menu())return 0;
    if(f->slide){if(!mm1_slide_available()||!mm1_slide_pose(f->donor_pose))return 0;}
    else if(!mm1_proto_available()||!mm1_proto_atlas())return 0;
    const uint8_t *oam=display_oam();
    for(int slot=0;slot<64;slot++)if(f->mask&(1ull<<slot))
        if(memcmp(f->oam+slot*4,oam+slot*4,4))return 0;
    return 1;
}
static int valid_shot(int actor) {
    if(actor<2||actor>4||!mm1_charge_available()||!mm1_charge_data()||mm1_controls_menu())return 0;
    ShotFrame *f=&video.shown_shot[actor-2];if(!f->replace)return 0;
    const uint8_t *oam=display_oam();
    if(memcmp(video.page,oam,256))return 0;
    for(int slot=0;slot<64;slot++)if(f->mask&(1ull<<slot))
        if(memcmp(f->oam+slot*4,oam+slot*4,4))return 0;
    return 1;
}
static int valid_portrait(void) {
    return video.portrait.on&&mm1_proto_available()&&mm1_proto_atlas()&&
        !memcmp(video.page,display_oam(),256);
}
int mm1_proto_owns_slot(int slot) {
    if(slot<0||slot>=64)return 0;
    if(valid_picture()&&(video.shown.mask&(1ull<<slot)))return 1;
    if(valid_portrait()&&slot>=58&&slot<=61)return 1;
    for(int i=2;i<5;i++)if(valid_shot(i)&&(video.shown_shot[i-2].mask&(1ull<<slot)))return 1;
    return 0;
}
static uint32_t color(unsigned index,int weapon,int proto,int flash,int light,int accent) {
    const CycProtoAtlas *atlas=mm1_proto_atlas();
    uint32_t c=atlas->colors[index-1];
    if(weapon||!proto||flash) {
        unsigned r=c>>16&255,g=c>>8&255,b=c&255;
        if(r==g&&g==b&&r>32&&r<240)c=private_paint?cyc_render_color_value((uint8_t)light):cyc_render_color(17);
        else if(r>64&&r>g+b)c=private_paint?cyc_render_color_value((uint8_t)accent):cyc_render_color(18);
    }
    return c;
}
static int pixel_visible(int width,int nx,int y) {
    return (cyc_render_line_mask(y)&16)&&
        (width!=256||nx<0||nx>=8||(cyc_render_line_mask(y)&4));
}
static void paint_portrait(uint32_t *out,int width,int x0) {
    if(!valid_portrait())return;
    PortraitFrame *p=&video.portrait;const uint32_t *art=mm1_proto_atlas()->portrait;
    for(int row=0;row<16;row++)for(int col=0;col<16;col++) {
        int nx=p->x+col,x=nx+x0,y=p->y+row;uint32_t color=art[row*16+col];
        if(color&&x>=0&&x<width&&y<240&&pixel_visible(width,nx,y))out[y*width+x]=color;
    }
}
/* Donor tiles stay immutable host data; no native CHR slots are borrowed. */
static void paint_tile(uint32_t *out,int width,int x0,const uint8_t *coverage,
                       const uint8_t *pixels,int px,int py,int attr,const uint32_t colors[4]) {
    for(int row=0;row<8;row++)for(int col=0;col<8;col++) {
        int y=py+row,nx=px+col,x=nx+x0;if(x<0||x>=width||y<0||y>=240)continue;
        if(!pixel_visible(width,nx,y))continue;
        int r=(attr&128)?7-row:row,bit=(attr&64)?col:7-col;
        int value=((pixels[r]>>bit)&1)|(((pixels[r+8]>>bit)&1)<<1);
        if(!value||((attr&32)&&coverage&&coverage[y*width+x]))continue;
        out[y*width+x]=colors[value];
    }
}
static void donor_colors(uint32_t colors[4],int index,int body) {
    const uint8_t *p=mm1_slide_palette(index);
    for(int i=0;i<4;i++)colors[i]=cyc_render_color_value(p[i]);
    if(body&&!index){colors[2]=cyc_render_color(17);colors[3]=cyc_render_color(18);}
}
void mm1_proto_paint(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    if(!valid_picture())return;
    BodyFrame *f=&video.shown;
    if(f->slide) {
        const Mm1SlidePose *p=mm1_slide_pose(f->donor_pose);
        for(int i=p->count-1;i>=0;i--) {
            const Mm1SlidePart *part=&p->parts[i];uint32_t colors[4];donor_colors(colors,part->attr&1,1);
            if(private_paint&&!(part->attr&1)){colors[2]=cyc_render_color_value(f->light);colors[3]=cyc_render_color_value(f->accent);}
            paint_tile(out,width,x0,coverage,part->pixels,f->x+part->x[f->right],f->y+2+part->y[f->right],
                       part->attr^(f->right?64:0)|(f->behind?32:0),colors);
        }return;
    }
    const CycProtoPose *p=&mm1_proto_atlas()->poses[f->pose];
    for(int sy=0;sy<p->height;sy++)for(int sx=0;sx<p->width;sx++) {
        unsigned index=p->pixels[sy*32+sx];if(!index)continue;
        int nx=f->x+(f->right?p->origin_x-sx-1:sx-p->origin_x);
        int x=nx+x0,y=f->y+p->top_y+sy;
        if(x<0||x>=width||y<0||y>=240)continue;
        if(!pixel_visible(width,nx,y))continue;
        if(f->behind&&coverage&&coverage[y*width+x])continue;
        out[y*width+x]=color(index,f->weapon,f->proto&&!f->teleport,f->flash,f->light,f->accent);
    }
}
typedef struct {uint32_t *out;int pending,insert;} Paint;
static int first_slot(uint64_t mask){for(int i=0;i<64;i++)if(mask&(1ull<<i))return i;return -1;}
int mm1_video_replaces_actor(int actor) {
    if(!actor)return valid_picture();return valid_shot(actor);
}
int mm1_video_actor_has_oam(int actor) {
    return actor>=2&&actor<=4&&valid_shot(actor)&&video.shown_shot[actor-2].mask!=0;
}
static void paint_shot(uint32_t *out,int width,int x0,const uint8_t *coverage,int actor) {
    if(!valid_shot(actor))return;
    ShotFrame *f=&video.shown_shot[actor-2];const CycMm4ChargeData *d=mm1_charge_data();
    const CycMm4ChargeSprite *s=&d->sprites[f->sprite];
    for(int part=s->count-1;part>=0;part--) {
        int key=0;while(key<6&&cyc_mm4_charge_tile_ids[key]!=s->tile[part])key++;if(key==6)continue;
        int attr=s->attr[part]^(f->right?64:0),px=f->x+s->x[f->right][part],py=f->y+s->y[f->right][part];
        uint32_t colors[4];for(int i=0;i<4;i++)colors[i]=cyc_render_color_value(d->palette[attr&1][i]);
        paint_tile(out,width,x0,coverage,d->tiles[key],px,py,attr,colors);
    }
}
int mm1_video_dust_visible(void) {
    return video.shown_dust.on&&mm1_slide_available()&&!mm1_controls_menu()&&
        !memcmp(video.page,display_oam(),256);
}
void mm1_video_paint_dust(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    if(!mm1_video_dust_visible())return;
    DustFrame *d=&video.shown_dust;uint32_t colors[4];donor_colors(colors,1,0);
    paint_tile(out,width,x0,coverage,mm1_slide_dust_tile(d->age),d->x+(d->right?-12:4),d->y+5,
               1^(d->right?64:0),colors);
}
void mm1_video_paint_actor(uint32_t *out,int width,int x0,const uint8_t *coverage,int actor) {
    if(!actor)mm1_proto_paint(out,width,x0,coverage);else paint_shot(out,width,x0,coverage,actor);
}
void mm1_video_paint_slot(uint32_t *out,int width,int x0,const uint8_t *coverage,int slot) {
    if(slot==58)paint_portrait(out,width,x0);
    if(valid_picture()&&first_slot(video.shown.mask)==slot)mm1_proto_paint(out,width,x0,coverage);
    for(int i=2;i<5;i++)if(valid_shot(i)&&first_slot(video.shown_shot[i-2].mask)==slot)
        paint_shot(out,width,x0,coverage,i);
}
static int place(int slot,int x,int y,int *out_x,void *context) {
    (void)y;Paint *p=(Paint*)context;*out_x=x;
    if(p->pending&&slot<=p->insert){mm1_coop_paint(p->out,256,0,cyc_frame_bg_opaque());p->pending=0;}
    if(!mm1_proto_owns_slot(slot))return 1;
    mm1_video_paint_slot(p->out,256,0,cyc_frame_bg_opaque(),slot);
    return 0;
}
int mm1_proto_native_picture(uint32_t *out) {
    int any=valid_picture()||mm1_video_dust_visible()||valid_portrait()||mm1_coop_picture();for(int i=2;i<5;i++)any|=valid_shot(i);
    if(!any||!cyc_render_background(out))return 0;
    mm1_video_paint_dust(out,256,0,cyc_frame_bg_opaque());
    Paint p={out,mm1_coop_picture(),mm1_coop_insert_slot()};cyc_render_sprites(out,256,240,0,cyc_frame_bg_opaque(),place,&p);
    if(p.pending)mm1_coop_paint(out,256,0,cyc_frame_bg_opaque());
    for(int i=2;i<5;i++)if(valid_shot(i)&&!video.shown_shot[i-2].mask)
        paint_shot(out,256,0,cyc_frame_bg_opaque(),i);
    return 1;
}
void mm1_video_private_finish(void) {
    restore_invulnerability();for(int i=0;i<256;i++)video.building_page[i]=R(0x200+i);
    for(int slot=0;slot<64;slot++)if(video.building.mask&(1ull<<slot))
        if(memcmp(video.building.oam+slot*4,video.building_page+slot*4,4)){video.building.mask=0;break;}
}
void mm1_video_private_latch(int seat) {
    Presentation *v=&videos[seat&1];v->shown=v->building;v->shown_dust=v->building_dust;
    memcpy(v->shown_shot,v->building_shot,sizeof v->shown_shot);memcpy(v->page,v->building_page,256);v->portrait.on=0;
}
int mm1_video_private_place(uint32_t *out,int width,int x0,const uint8_t *coverage,int slot) {
    int saved=private_paint;private_paint=1;int owned=mm1_proto_owns_slot(slot);
    if(owned)mm1_video_paint_slot(out,width,x0,coverage,slot);private_paint=saved;return owned;
}
void mm1_video_private_dust(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    int saved=private_paint;private_paint=1;mm1_video_paint_dust(out,width,x0,coverage);private_paint=saved;
}
void mm1_video_private_after(uint32_t *out,int width,int x0,const uint8_t *coverage) {
    int saved=private_paint;private_paint=1;
    for(int i=2;i<5;i++)if(valid_shot(i)&&!video.shown_shot[i-2].mask)paint_shot(out,width,x0,coverage,i);
    private_paint=saved;
}
int mm1_video_state_get(uint8_t *out,int cap) {
    if(cap<(int)sizeof videos)return -1;memcpy(out,videos,sizeof videos);return sizeof videos;
}
static int valid_record(Presentation v) {
    if(v.restoring>1||v.building.pose>=CYC_PROTO_POSE_COUNT||v.shown.pose>=CYC_PROTO_POSE_COUNT||
        v.building.right>1||v.shown.right>1||v.building.replace>1||v.shown.replace>1||
        v.building.slide>1||v.shown.slide>1||v.building.donor_pose>2||v.shown.donor_pose>2||
        v.building_dust.on>1||v.shown_dust.on>1||v.building_dust.right>1||v.shown_dust.right>1||
        v.building_dust.age>26||v.shown_dust.age>26||v.portrait.on>1)return 0;
    for(int i=0;i<3;i++)if(v.building_shot[i].sprite>=7||v.shown_shot[i].sprite>=7||
        v.building_shot[i].right>1||v.shown_shot[i].right>1||
        v.building_shot[i].replace>1||v.shown_shot[i].replace>1)return 0;
    return 1;
}
int mm1_video_state_valid(const uint8_t *in,int len) {
    if(!len)return !mm1_proto_available()&&!mm1_charge_available()&&!mm1_slide_available();
    if(!in||len!=sizeof videos)return 0;Presentation records[2];memcpy(records,in,sizeof records);
    return valid_record(records[0])&&valid_record(records[1]);
}
int mm1_video_state_set(const uint8_t *in,int len) {
    if(!mm1_video_state_valid(in,len))return 0;
    if(len)memcpy(videos,in,sizeof videos);else memset(videos,0,sizeof videos);private_paint=0;return 1;
}
NES_MOD_CONSTRUCTOR(register_mm1_proto_video) {
    nes_mod_register_savestate_hook("mm1.protoman.video",mm1_video_state_get,mm1_video_state_set);
    nes_mod_register_savestate_validator("mm1.protoman.video",mm1_video_state_valid);
}
