/* Widen the native authored world, retaining its actors and spawn identities. */
#include "mm5_wide.h"
#include "mm5_world.h"
#include "mm5_cart.h"
#include "mm5_proto.h"
#include "mm5_coop.h"
#include "mm5_net.h"
#include "mm5_pool.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
/* One native render call can emit63 pieces; retain all24 actors, rather than
 * imposing a second scene-wide OAM limit in the host packet. */
enum { MAX_PARTS = MM5_RENDER_OWNERS * 63 };
typedef struct { int16_t x,y; uint8_t owner,attr,pixels[64]; } WidePart;
typedef struct {
    uint8_t valid,rank[MM5_RENDER_OWNERS],next_rank,extended;
    uint16_t count,width,camera;
    int16_t left;
    uint16_t room_left,room_right;
    Mm5ProtoBody body;
    WidePart parts[MAX_PARTS];
} WideFrame;
typedef struct {
    char magic[4]; uint8_t enabled,noflicker,reserved[2];
    uint64_t spawns,rendered,full,failed;
    WideFrame build,frame;
    uint8_t seen_stage,seen[128];
} WideState;
static WideState S;
static int s_on,s_no_flicker;

static int in_views(int x,int radius) {
    int left[2];if(!mm5_coop_independent() || !mm5_coop_view_rects(left))return 0;
    return (x+radius>=left[0] && x-radius<left[0]+256) || (x+radius>=left[1] && x-radius<left[1]+256);
}

static int geometry(int width, int *camera, int *left, int *lo, int *hi) {
    int independent=mm5_coop_independent(),coop=mm5_coop_enabled();
    if ((!s_on && !coop) || (!coop && width<=256) || width > CYC_VIDEO_MAX_WIDTH || R(0x26) >= 16 || R(0x59) || R(0x1B) ||
        !(R(0x29) & 32) || R(0xFA)) return 0;
    int page = R(0xF9), start = page - R(0x2B);
    if (page>=0x4F || start < 0 || R(0x2A) > 31) return 0;
    *camera = page * 256 + R(0xFC); *lo = start * 256; *hi = (start + R(0x2A) + 1) * 256;
    if(independent) {int views[2];if(!mm5_coop_view_rects(views))return 0;*left=views[0]<views[1]?views[0]:views[1];return 1;}
    *left = *camera - (width - 256) / 2;
    if(*hi-*lo<width){*left=*lo-(width-(*hi-*lo))/2;return 1;}
    *left=mm5_coop_shared_left(*left,width,*lo,*hi);
    if (*left < *lo) *left = *lo;
    if (*hi - *lo >= width && *left + width > *hi) *left = *hi - width;
    return 1;
}
void mm5_wide_begin(void) {
    memset(&S.build,0,sizeof S.build); memset(S.build.rank,255,sizeof S.build.rank); S.build.next_rank = 1;
    int camera,left,lo,hi,width = mm5_coop_independent()?256:mm5_net_simulation_width(cyc_video_width());
    int extended=geometry(width,&camera,&left,&lo,&hi);
    if(!extended) {
        if((!s_no_flicker && !mm5_coop_independent()) || width<256 || width>CYC_VIDEO_MAX_WIDTH || R(0x26)>=16 || R(0x59) || R(0x1B))return;
        camera=(R(0xF9)<<8)|R(0xFC);left=camera;lo=camera;hi=camera+256;
    }
    S.build.valid=1; S.build.width=(uint16_t)width; S.build.camera=(uint16_t)camera;
    S.build.extended=(uint8_t)extended;
    S.build.left=(int16_t)left; S.build.room_left=(uint16_t)lo; S.build.room_right=(uint16_t)hi;
}
int mm5_wide_contains(int x,int radius) {
    int camera,left,lo,hi,width=mm5_net_simulation_width(cyc_video_width());
    if(!geometry(width,&camera,&left,&lo,&hi)){left=R(0xF9)*256+R(0xFC);width=256;}
    return x+radius>=left && x-radius<left+width;
}
void mm5_wide_latch(void) { S.frame=S.build; }
static uint8_t pattern(const uint8_t banks[6], int address) {
    int bank;
    address &= 8191;
    if (address < 0x800) bank=(banks[0]&254)+(address>>10);
    else if (address < 0x1000) bank=(banks[1]&254)+((address-0x800)>>10);
    else bank=banks[2+((address-0x1000)>>10)];
    return mm5_cart_chr(bank,address);
}
static int capture_actor(int slot,int owner,int extra) {
    if (!S.build.valid || cyc_mod_isolated()) return 0;
    CycModRegs regs; cyc_mod_regs(&regs);
    if (slot>=24) return 0;
    int complete=s_no_flicker || mm5_coop_independent();
    S.build.rank[owner]=extra?(uint8_t)owner:complete?(uint8_t)slot:S.build.next_rank++;
    int wx=(R(0x348+slot)<<8)|R(0x330+slot), dx=wx-S.build.camera;
    int visible=mm5_coop_independent()?in_views(wx,32):(wx+32>=S.build.left && wx-32<S.build.left+S.build.width);
    int widened=S.build.extended && slot && !(dx>=32 && dx<224) && visible;
    if(!R(0x300+slot) || (!complete && !widened && !extra))return 0;
    if (!cyc_mod_isolate_begin()) { S.failed++; return 0; }
    int saved_count=S.build.count;
    int camera=wx-128; P(0xFC,camera); P(0xF9,camera>>8); P(0x9E,slot); P(0x9F,4);
    for (int i=0;i<256;i+=4) P(0x200+i,248);
    /* Give the scratch renderer independent CHR ownership. Captured pattern
     * pixels remain stable when the real frame later selects another bank. */
    P(0xEC,255); P(0xEE,255); P(0xEF,255);
    regs.x=(uint8_t)slot;
    int ok=cyc_mod_call(0xDFC3,&regs), count=R(0x9F), n=0;
    uint8_t fields[30],banks[6];
    for (int base=0x300;base<=0x5B8;base+=0x18) fields[n++]=R(base+slot);
    for (int i=0;i<6;i++) banks[i]=R(0xEA+i);
    if(complete && !slot) {
        mm5_proto_body(&S.build.body);S.build.body.x+=(int16_t)(camera-S.build.camera);
    }
    if (!count) count=256;
    for (int offset=4;ok && offset<count;offset+=4) {
        int top=R(0x200+offset)+1;
        if (top>=240) continue;
        if (S.build.count==MAX_PARTS) { S.full++; break; }
        WidePart *p=&S.build.parts[S.build.count++];
        p->x=(int16_t)(wx+R(0x203+offset)-128); p->y=(int16_t)top;
        p->owner=(uint8_t)owner; p->attr=R(0x202+offset);
        int tile=R(0x201+offset), table=cyc_render_line_sprite_table(top);
        for (int y=0;y<8;y++) {
            int row=(p->attr&128)?7-y:y, a=table+tile*16+row;
            int low=pattern(banks,a),high=pattern(banks,a+8);
            for (int x=0;x<8;x++) { int bit=(p->attr&64)?x:7-x; p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1)); }
        }
    }
    cyc_mod_isolate_end();
    if (!ok) { S.build.count=(uint16_t)saved_count; S.failed++; return 0; }
    S.rendered++;
    /* Widened offscreen actors replace the cartridge's culling pass. For
     * ordinary actors, the real call still owns animation, palette and death
     * effects exactly once; the isolated call only retained complete art. */
    if(widened || extra) {n=0;for(int base=0x300;base<=0x5B8;base+=0x18)P(base+slot,fields[n++]);return extra?0:1;}
    return 0;
}
static int actor(uint16_t address){(void)address;CycModRegs r;cyc_mod_regs(&r);return capture_actor(r.x,r.x,0);}
void mm5_wide_capture_extra(int slot,int index){if(index>=0 && index<16)capture_actor(slot,24+index,1);}
static void seen_stage(int stage) {
    if(S.seen_stage!=stage){memset(S.seen,0,sizeof S.seen);S.seen_stage=(uint8_t)stage;}
}
static int authored(uint16_t address) {
    (void)address;if(cyc_mod_isolated() || R(0x26)>=16)return 0;
    int camera,left,lo,hi,width=mm5_net_simulation_width(cyc_video_width());
    if(!geometry(width,&camera,&left,&lo,&hi) || !mm5_cart_ready())return 0;
    int index=R(6);if(index>=128)return 0;seen_stage(R(0x26));
    int wx=mm5_cart_prg(R(0x26),0xAA00+index)*256+mm5_cart_prg(R(0x26),0xAA80+index);
    int visible=mm5_coop_independent()?in_views(wx,24):(wx+24>=left && wx-24<left+width);
    if(!visible){S.seen[index]=0;return 0;}if(!S.seen[index])return 0;
    CycModRegs r;cyc_mod_regs(&r);r.a=(uint8_t)index;r.x=23;r.p=(uint8_t)((r.p&~0x82)|3);cyc_mod_set_regs(&r);return 1;
}
static int born(uint16_t address) {
    (void)address;if(cyc_mod_isolated() || R(0x26)>=16)return 0;
    CycModRegs r;cyc_mod_regs(&r);if(r.x<8 || r.x>=24)return 0;int index=R(0x438+r.x);
    if(R(0x300+r.x) && index<128){seen_stage(R(0x26));S.seen[index]=1;}return 0;
}
static int clear(uint16_t address) {
    (void)address;if(!cyc_mod_isolated()){memset(S.seen,0,sizeof S.seen);S.seen_stage=255;}return 0;
}
static int spawn(uint16_t address) {
    (void)address;
    if (cyc_mod_isolated()) return 0;
    int camera,left,lo,hi;
    int width=mm5_net_simulation_width(cyc_video_width());
    if (!geometry(width,&camera,&left,&lo,&hi) || !mm5_cart_ready()) return 0;
    int stage=R(0x26);seen_stage(stage);
    for (int index=0;index<128;index++) {
        int screen=mm5_cart_prg(stage,0xAA00+index);
        if (screen>=0x80) break; /* native sorted-list terminator; tail is other data */
        int wx=screen*256+mm5_cart_prg(stage,0xAA80+index);
        int code=mm5_cart_prg(stage,0xAB80+index), y=mm5_cart_prg(stage,0xAB00+index);
        if (code>=0xC0 || y>=240)continue;
        int visible=wx>=lo && wx<hi && (mm5_coop_independent()?in_views(wx,24):(wx+24>=left && wx-24<left+width));
        if(!visible){S.seen[index]=0;continue;}
        int active=mm5_pool_authored(index),free_slot=0;
        for (int slot=8;slot<24;slot++) { if (!R(0x300+slot)) free_slot=1; else if (R(0x438+slot)==index) active=1; }
        if(active){S.seen[index]=1;continue;}
        if (wx>=camera && wx<camera+256) continue; /* native traversal owns its rectangle */
        if (S.seen[index] || (R(0x100+(index>>3))&(1<<(index&7)))) continue;
        if (!free_slot && !mm5_pool_make_room()) { S.full++; continue; }
        if (!cyc_mod_isolate_begin()) { S.failed++; continue; }
        CycModRegs r; cyc_mod_regs(&r); r.a=0x1B;
        int ok=cyc_mod_call(0xFF3D,&r); P(0xF6,stage);
        if (ok) ok=cyc_mod_call(0xFF43,&r);
        P(0x06,index); r.y=(uint8_t)index;
        if (ok) ok=cyc_mod_call(0x9995,&r);
        int found=-1,n=0;uint8_t fields[30];
        for (int slot=8;ok && slot<24;slot++) if (R(0x300+slot) && R(0x438+slot)==index) { found=slot; break; }
        if (found>=0) for (int base=0x300;base<=0x5B8;base+=0x18) fields[n++]=R(base+found);
        cyc_mod_isolate_end();
        if (!ok) S.failed++;
        else if (found>=0) { n=0;for (int base=0x300;base<=0x5B8;base+=0x18) P(base+found,fields[n++]); S.seen[index]=1;S.spawns++; }
    }
    return 0;
}
int mm5_wide_background(uint32_t *out,int width,int height,const uint32_t *native,uint8_t *opaque,int *origin) {
    if ((!s_on && !mm5_coop_enabled()) || !S.frame.valid || !S.frame.extended || (width<=256 && !mm5_coop_enabled()) || width>CYC_VIDEO_MAX_WIDTH || height!=240 || !native) return 0;
    int camera=S.frame.camera,left=S.frame.left;
    if (width!=S.frame.width) { int lo,hi;if (!geometry(width,&camera,&left,&lo,&hi)) return 0; }
    *origin=camera-left;
    Mm5WorldTile tiles[CYC_VIDEO_MAX_WIDTH/8+2];
    uint8_t valid_tiles[CYC_VIDEO_MAX_WIDTH/8+2];
    int cache_x=-100000,cache_y=-1,tile_count=width/8+2;
    for (int y=0;y<240;y++) {
        int delta=(cyc_render_line_scroll_x(y)-(camera&511)+256)&511;
        int actual=camera+delta-256,sy=cyc_render_line_scroll_y(y)%240;
        int first_world=actual-*origin,first_tile=first_world>=0?first_world/8:(first_world-7)/8;
        if (cache_x!=first_tile || cache_y!=sy/8) {
            cache_x=first_tile;cache_y=sy/8;
            for (int i=0;i<tile_count;i++) {
                int wx=(first_tile+i)*8;
                valid_tiles[i]=(uint8_t)(wx>=0 && mm5_world_tile(wx/256,wx&255,sy,&tiles[i]));
            }
        }
        uint8_t low[CYC_VIDEO_MAX_WIDTH/8+2],high[CYC_VIDEO_MAX_WIDTH/8+2];
        for (int i=0;i<tile_count;i++) if (valid_tiles[i]) {
            int a=cyc_render_line_bg_table(y)+tiles[i].tile*16+(sy&7);
            low[i]=cyc_render_chr((uint16_t)a);high[i]=cyc_render_chr((uint16_t)(a+8));
        }
        for (int x=0;x<width;x++) {
            int nx=x-*origin,wx=actual+nx,value=0;
            if (nx>=0 && nx<256) { out[y*width+x]=native[y*256+nx];opaque[y*width+x]=0;continue; }
            uint32_t color=0xFF000000u;
            int cell=wx/8-first_tile;
            if (wx>=S.frame.room_left && wx<S.frame.room_right && cell>=0 && cell<tile_count && valid_tiles[cell]) {
                int bit=7-(wx&7);
                if (cyc_render_line_mask(y)&8) value=((low[cell]>>bit)&1)|(((high[cell]>>bit)&1)<<1);
                color=cyc_render_color(value?tiles[cell].palette*4+value:0);
            }
            out[y*width+x]=color;
            opaque[y*width+x]=(uint8_t)(value!=0);
        }
    }
    return 1;
}
int mm5_wide_rank(int owner) { return owner>=0 && owner<MM5_RENDER_OWNERS && S.frame.valid?S.frame.rank[owner]:255; }
int mm5_wide_camera(void) {return S.frame.camera;}
void mm5_wide_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed,int rank) {
    int complete=s_no_flicker || mm5_coop_independent();
    if (!(s_on || complete || mm5_coop_enabled()) || !S.frame.valid || (!complete && width<=256 && !mm5_coop_enabled())) return;
    int left=S.frame.camera-origin;
    if(complete && !rank && S.frame.body.active)mm5_view_draw_character(out,width,origin,&S.frame.body);
    for (int i=0;i<S.frame.count;i++) {
        const WidePart *p=&S.frame.parts[i];if (mm5_wide_rank(p->owner)!=rank) continue;
        if(complete && !p->owner && S.frame.body.active && !mm5_proto_native_part(&S.frame.body,p->attr))continue;
        for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
            int v=p->pixels[y*8+x],px=p->x-left+x,py=p->y+y;
            if (!v || px<0 || px>=width || py<0 || py>=240) continue;
            if(!S.frame.extended && !mm5_coop_independent() && (px<origin || px>=origin+256))continue;
            int at=py*width+px;
            if (claimed[at]) continue;
            claimed[at]=1;
            if (!(p->attr&32) || !opaque[at]) out[at]=cyc_render_color(16+(p->attr&3)*4+v);
        }
    }
}
int mm5_wide_enabled(void) {return s_on;}
void mm5_wide_set(int enabled,const char *aspect) {
    s_on=enabled!=0;
    int mode=NES_VIDEO_FIT;
    if (aspect && !strcmp(aspect,"16-9")) mode=NES_VIDEO_16_9;
    else if (aspect && !strcmp(aspect,"21-9")) mode=NES_VIDEO_21_9;
    else if (aspect && !strcmp(aspect,"32-9")) mode=NES_VIDEO_32_9;
    else if (aspect && !strcmp(aspect,"stock")) mode=NES_VIDEO_STOCK;
    cyc_video_set_mode(s_on?mode:NES_VIDEO_STOCK);
    mm5_wide_views_changed();
    nes_mod_set_local_only("MM5 widened world (unqualified netplay)",s_on);
    mm5_view_wide(s_on);
}
void mm5_no_flicker_set(int enabled) {
    s_no_flicker=enabled!=0;mm5_wide_views_changed();
    nes_mod_set_local_only("MM5 complete sprites (unqualified netplay)",s_no_flicker);mm5_view_no_flicker(s_no_flicker);
}
int mm5_no_flicker_enabled(void) {return s_no_flicker;}
void mm5_wide_views_changed(void) {
    nes_mod_set_function_hook_enabled("mm5.wide.actor",s_on || s_no_flicker || mm5_coop_enabled());
    nes_mod_set_function_hook_enabled("mm5.wide.spawn",s_on || mm5_coop_enabled());
    nes_mod_set_function_hook_enabled("mm5.wide.authored",s_on || mm5_coop_enabled());
    nes_mod_set_function_hook_enabled("mm5.wide.born",s_on || mm5_coop_enabled());
    nes_mod_set_function_hook_enabled("mm5.wide.clear",1);
}
void mm5_wide_power(void) { memset(&S,0,sizeof S);S.seen_stage=255; }
static int get(uint8_t *out,int cap) { if (cap<sizeof S)return -1;memcpy(S.magic,"MX04",4);S.enabled=(uint8_t)s_on;S.noflicker=(uint8_t)s_no_flicker;memcpy(out,&S,sizeof S);return sizeof S; }
static int valid(const uint8_t *in,int len) {
    if (!len)return !(s_on || s_no_flicker);
    if (!in || len!=sizeof S)return 0;
    static WideState p;memcpy(&p,in,sizeof p);
    if (memcmp(p.magic,"MX04",4) || p.enabled!=s_on || p.noflicker!=s_no_flicker || (p.seen_stage>=16 && p.seen_stage!=255))return 0;
    for(int i=0;i<128;i++)if(p.seen[i]>1)return 0;
    const WideFrame *f[2]={&p.build,&p.frame};
    for(int j=0;j<2;j++) {
        if(f[j]->valid>1 || f[j]->extended>1 || f[j]->count>MAX_PARTS || f[j]->width>CYC_VIDEO_MAX_WIDTH ||
            f[j]->body.pose>=CYC_PROTO_POSE_COUNT || f[j]->body.active>1 || f[j]->body.right>1 || f[j]->body.flipped>1 || f[j]->body.behind>1)return 0;
        for(int i=0;i<f[j]->count;i++) { if(f[j]->parts[i].owner>=MM5_RENDER_OWNERS)return 0;for(int k=0;k<64;k++)if(f[j]->parts[i].pixels[k]>3)return 0; }
    }
    return 1;
}
static int set(const uint8_t *in,int len) { if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else memset(&S,0,sizeof S);return 1; }
void mm5_wide_register(void) {
    nes_mod_register_function_entry_plugin("mm5.wide.actor",0xDFC3,actor);nes_mod_register_function_entry_plugin("mm5.wide.spawn",0xDED4,spawn);
    nes_mod_register_function_entry_plugin("mm5.wide.authored",0x9995,authored);
    nes_mod_register_function_entry_plugin("mm5.wide.born",0x9A3B,born);
    nes_mod_register_function_entry_plugin("mm5.wide.clear",0xC39D,clear);
    nes_mod_register_savestate_hook("mm5.wide",get,set);nes_mod_register_savestate_validator("mm5.wide",valid);
}
int mm5_wide_json(char *out,int cap) { return snprintf(out,cap,"\"wide\":{\"on\":%d,\"noflicker\":%d,\"width\":%u,\"left\":%d,\"camera\":%u,\"parts\":%u,\"spawns\":%llu,\"rendered\":%llu,\"full\":%llu,\"failed\":%llu}",s_on,s_no_flicker,S.frame.width,S.frame.left,S.frame.camera,S.frame.count,(unsigned long long)S.spawns,(unsigned long long)S.rendered,(unsigned long long)S.full,(unsigned long long)S.failed); }
