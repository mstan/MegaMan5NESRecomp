/* Replace player-owned OAM with immutable art; CHR-ROM and guest timing stay native. */
#include "mm5_proto.h"
#include "mm5_world.h"
#include "mm5_wide.h"
#include "mm5_coop.h"
#include "mm5_hud.h"
#include "mm5_pool.h"
#include "mm5_menu.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char magic[4];
    uint8_t valid, owners[64],panels,native_scene;
    int16_t left[2];
    uint8_t tiles[2][30][33],palettes[2][30][33],present[2][30][33];
    Mm5ProtoBody body;
} ViewFrame;
static ViewFrame s_build, s_frame;
static int s_enabled, s_wide, s_coop, s_no_flicker;
static uint32_t s_bg[256 * 240];
static uint8_t s_opaque[864 * 240], s_allowed[64][240];
static uint8_t s_claimed[864 * 240];
static uint32_t s_panel[2][256*240];
int mm5_view_json(char *out,int capacity) {
    return snprintf(out,capacity,"\"view\":{\"mode\":%d,\"panels\":%u,\"scene\":%u,\"left\":[%d,%d]}",
        mm5_coop_view_mode(),s_frame.panels,s_frame.native_scene,s_frame.left[0],s_frame.left[1]);
}

static void panels_build(void) {
    if(!mm5_coop_independent() || cyc_mod_peek(0x26)>=16 || cyc_mod_peek(0x59))return;
    int left[2];if(!mm5_coop_view_rects(left))return;
    /* Big Pets draws its body through IRQ background splits. Native player
     * scripts also replace the stage's tiles/CHR during story and ending
     * scenes; retain that picture instead of decoding ordinary terrain. */
    s_build.panels=1;s_build.native_scene=(uint8_t)(mm5_coop_scene_locked() || cyc_mod_peek(0xFA) || cyc_mod_peek(0x1B));
    int lo=(cyc_mod_peek(0xF9)-cyc_mod_peek(0x2B))*256,hi=lo+(cyc_mod_peek(0x2A)+1)*256;
    for(int seat=0;seat<2;seat++) {
        s_build.left[seat]=(int16_t)left[seat];
        for(int y=0;y<30;y++)for(int x=0;x<33;x++) {
            int wx=(left[seat]&~7)+x*8;Mm5WorldTile t;
            if(wx<lo || wx>=hi || !mm5_world_tile(wx/256,wx&255,y*8,&t))continue;
            s_build.tiles[seat][y][x]=t.tile;s_build.palettes[seat][y][x]=t.palette;s_build.present[seat][y][x]=1;
        }
    }
}

static int begin(uint16_t address) {
    (void)address;
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    mm5_wide_begin();
    mm5_pool_render_begin();
    mm5_coop_build();
    panels_build();
    return 0;
}
static int part(uint16_t address) {
    (void)address;
    CycModRegs r; cyc_mod_regs(&r);
    int slot = ((r.x + 252) & 255) / 4, owner = cyc_mod_peek(0x9E);
    s_build.owners[slot] = (uint8_t)owner;
    if (!owner && !s_build.valid) { mm5_proto_body(&s_build.body); s_build.valid = 1; }
    return 0;
}
static int dma(uint16_t address) {
    (void)address;
    mm5_pool_draw();
    memcpy(&s_frame, &s_build, sizeof s_frame);
    mm5_wide_latch();
    mm5_coop_latch();
    return 0;
}
static uint32_t color(int index, const Mm5ProtoBody *body) {
    const CycProtoAtlas *atlas = mm5_proto_atlas();
    uint32_t c = atlas->colors[index - 1];
    unsigned r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    if (body->weapon || body->charge >= 0x10) {
        if (!r && !g && !b) c = cyc_render_color_value(body->palette[1]);
        else if (r == g && g == b && r > 32 && r < 240) c = cyc_render_color_value(body->palette[2]);
        else if (r > 64 && r > g + b) c = cyc_render_color_value(body->palette[3]);
    }
    if (body->palette[1] == 0x0F && body->palette[2] == 0x0F && body->palette[3] == 0x0F)
        return 0xFF000000u;
    return c;
}
static void character_body(uint32_t *out, int W, int origin, const Mm5ProtoBody *b) {
    const CycProtoAtlas *atlas = mm5_proto_atlas();
    if (!atlas || b->pose >= CYC_PROTO_POSE_COUNT) return;
    const CycProtoPose *p = &atlas->poses[b->pose];
    for (int y = 0; y < p->height; ++y) for (int x = 0; x < p->width; ++x) {
        int index = p->pixels[y * 32 + x];
        if (!index) continue;
        int dx = x - p->origin_x, dy = y + p->top_y;
        int px = b->x + (b->right ? -dx - 1 : dx), py = b->y + (b->flipped ? -dy : dy + 1);
        if (origin + px < 0 || origin + px >= W || py < 0 || py >= 240 ||
            !(cyc_render_line_mask(py) & 16) || (px < 8 && !(cyc_render_line_mask(py) & 4)) ||
            s_claimed[py * W + origin + px]) continue;
        int at = py * W + origin + px;
        s_claimed[at] = 1;
        if (!b->behind || !s_opaque[at]) out[at] = color(index, b);
    }
}
void mm5_view_draw_character(uint32_t *out,int width,int origin,const Mm5ProtoBody *body) { character_body(out,width,origin,body); }
static uint32_t menu_color(uint32_t c) {
    /* Follow the native inventory's fade instead of leaving bright host art. */
    uint32_t light = cyc_render_color(1), white = cyc_render_color_value(0x20);
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        unsigned v = c >> shift & 255, l = light >> shift & 255, w = white >> shift & 255;
        v = w ? v * l / w : 0; if (v > 255) v = 255;
        out |= v << shift;
    }
    return out;
}
void mm5_view_menu_character(uint32_t *out,int W,int origin,int cursor,int character,const uint8_t palette[8]) {
    const CycProtoAtlas *atlas = mm5_proto_atlas();
    if(!atlas || !character)return;
    /* The retail menu's fixed 16x16 life icon, above its original life count. */
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        uint32_t c = atlas->portrait[y * 16 + x];
        out[(192 + y) * W + origin + 208 + x] = c ? menu_color(c) : 0xFF000000u;
    }
    /* Rush/Beat keep their own native previews. The other rows preview the
     * selected character on the same bottom-panel grid and foot baseline. */
    if (cursor != 6 && cursor != 12 && cursor != 13 && cursor != 14) {
        const CycProtoPose *p = &atlas->poses[CYC_PROTO_IDLE0];
        for (int y = 184; y < 217; ++y) for (int x = 140; x < 178; ++x) out[y * W + origin + x] = 0xFF000000u;
        Mm5ProtoBody body = {0}; body.weapon = cursor != 0;
        memcpy(body.palette,palette,sizeof body.palette);
        for (int y = 0; y < p->height; ++y) for (int x = 0; x < p->width; ++x) {
            int index = p->pixels[y * 32 + x]; if (!index) continue;
            int px = 160 + x - p->origin_x, py = 205 + p->top_y + y;
            out[py * W + origin + px] = menu_color(color(index, &body));
        }
    }
}
static int inventory(uint32_t *out, int W, int H, const uint32_t *native) {
    if(!mm5_proto_atlas() || !mm5_proto_selected() || W<256 || H!=240 || !native)return 0;
    int origin=(W-256)/2;
    for(int i=0;i<W*H;i++)out[i]=0xFF000000u;
    for(int y=0;y<H;y++)memcpy(out+y*W+origin,native+y*256,256*sizeof *out);
    uint8_t palette[8];for(int i=0;i<8;i++)palette[i]=cyc_mod_peek((uint16_t)(0x610+i));
    mm5_view_menu_character(out,W,origin,cyc_mod_peek(0x50),1,palette);
    cyc_render_set_native_origin(origin);return 1;
}
static void sprite(uint32_t *out, int W, int origin, int slot, const uint8_t *oam) {
    const uint8_t *o = oam + slot * 4;
    if(s_frame.owners[slot]==255 && mm5_hud_native_meter(o))return;
    int top = o[0] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
    for (int y = 0; y < height; ++y) {
        int py = top + y;
        if (py >= 240 || !s_allowed[slot][py] || !(cyc_render_line_mask(py) & 16)) continue;
        int row = (o[2] & 128) ? height - y - 1 : y;
        uint16_t a = height == 16 ? (uint16_t)((o[1] & 1) * 4096 + ((o[1] & 254) + row / 8) * 16 + row % 8) :
            (uint16_t)(cyc_render_line_sprite_table(py) + o[1] * 16 + row);
        int lo = cyc_render_chr(a), hi = cyc_render_chr(a + 8);
        for (int x = 0; x < 8; ++x) {
            int px = o[3] + x, bit = (o[2] & 64) ? x : 7 - x;
            int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (!v || origin + px >= W || (px < 8 && !(cyc_render_line_mask(py) & 4))) continue;
            int at = py * W + origin + px;
            if (s_claimed[at]) continue;
            s_claimed[at] = 1;
            if (!(o[2] & 32) || !s_opaque[at]) out[at] = cyc_render_color(16 + (o[2] & 3) * 4 + v);
        }
    }
}
static void panel_scene(uint32_t *out,int seat) {
    int left=s_frame.left[seat],phase=left&7;
    memset(s_claimed,0,256*240);memset(s_opaque,0,256*240);
    const uint8_t *native_opaque=cyc_frame_bg_opaque();
    for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
        int at=y*256+x,v=0,cell=(phase+x)/8,bit=7-((phase+x)&7);
        if(s_frame.native_scene) {
            out[at]=s_bg[at];s_opaque[at]=native_opaque[at];continue;
        }
        if(s_frame.present[seat][y/8][cell] && (cyc_render_line_mask(y)&8)) {
            int tile=s_frame.tiles[seat][y/8][cell],a=cyc_render_line_bg_table(y)+tile*16+(y&7);
            v=((cyc_render_chr((uint16_t)a)>>bit)&1)|(((cyc_render_chr((uint16_t)(a+8))>>bit)&1)<<1);
        }
        out[at]=cyc_render_color(v?s_frame.palettes[seat][y/8][cell]*4+v:0);s_opaque[at]=(uint8_t)(v!=0);
    }
    int native_camera=mm5_wide_camera(),origin=native_camera-left;
    for(int rank=0;rank<MM5_RENDER_OWNERS;rank++) {
        mm5_wide_draw(out,256,origin,s_opaque,s_claimed,rank);
        if(!rank)mm5_coop_draw(out,256,origin,s_opaque,s_claimed);
    }
    mm5_hud_draw(out,256,240,seat);
}
static int panels(uint32_t *out,int width,int height) {
    int mode=mm5_coop_view_mode();
    if(!mode || !s_frame.panels || width<256 || height!=240)return 0;
    if(mode==1) {
        panel_scene(s_panel[0],0);panel_scene(s_panel[1],1);
        int half=width/2;
        for(int y=0;y<240;y++)for(int x=0;x<width;x++) {
            int seat=x>=half,local=x-(seat?half:0),span=seat?width-half:half;
            out[y*width+x]=s_panel[seat][y*256+local*256/span];
        }
    } else {
        int seat=mode==3;panel_scene(s_panel[seat],seat);
        for(int y=0;y<240;y++)for(int x=0;x<width;x++)out[y*width+x]=s_panel[seat][y*256+x*256/width];
    }
    cyc_render_set_native_origin(0);return 1;
}
static int render(uint32_t *out, int W, int H, int native_x0, const uint32_t *native, void *ctx) {
    (void)ctx; (void)native_x0;
    if (cyc_render_background(s_bg)) mm5_world_audit(s_bg);
    if(mm5_menu_draw(out,W,H))return 1;
    if(!mm5_coop_menu_active() && cyc_mod_peek(0x26)<16 && !cyc_mod_peek(0x59) && panels(out,W,H))return 1;
    /* The inventory swaps to a pseudo-stage without rebuilding world OAM.
     * Never paint the last latched gameplay actor over its native panels. */
    if (s_enabled && cyc_mod_peek(0x26) == 16 && cyc_mod_peek(0x59)) return inventory(out, W, H, native);
    if (cyc_mod_peek(0x26) >= 16 || cyc_mod_peek(0x59) || cyc_mod_peek(0x1B)) return 0;
    if (!(s_enabled || s_wide || s_coop || s_no_flicker) || W < 256 || W > 864 || H != 240 ||
        !cyc_render_background(s_bg)) return 0;
    int origin = (W - 256) / 2;
    int wide = mm5_wide_background(out, W, H, s_bg, s_opaque, &origin);
    int replace = s_enabled && s_frame.valid && s_frame.body.active;
    if (!wide) {
        if (!replace && !s_coop && !s_no_flicker) return 0;
        memset(s_opaque, 0, sizeof s_opaque);
        for (int i = 0; i < W * H; ++i) out[i] = 0xFF000000u;
        for (int y = 0; y < H; ++y) memcpy(out + y * W + origin, s_bg + y * 256, 256 * sizeof *out);
    }
    const uint8_t *native_opaque = cyc_frame_bg_opaque();
    for (int y = 0; y < H; ++y) for (int x = 0; x < 256; ++x)
        if (origin+x>=0 && origin+x<W) s_opaque[y*W+origin+x]=native_opaque[y*256+x];
    const uint8_t *oam = cyc_render_oam();
    uint8_t counts[240] = {0};
    memset(s_allowed, 0, sizeof s_allowed);
    int first = 64;
    for (int slot = 0; slot < 64; ++slot) {
        int top = oam[slot * 4] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
        for (int y = top; y < top + height && y < 240; ++y) s_allowed[slot][y] = s_no_flicker || counts[y]++ < 8;
        if (s_frame.owners[slot] == 0 && slot < first) first = slot;
    }
    cyc_render_set_native_origin(origin);
    memset(s_claimed, 0, (size_t)W * H);
    int partner=0;
    /* An opaque higher-priority sprite claims its pixel even behind terrain.
     * Otherwise a lower-priority foreground sprite can leak through it. */
    for (int rank = 0; rank <= ((wide || s_no_flicker) ? 255 : 0); ++rank) {
        for (int slot = 0; slot < 64; ++slot) {
            int owner = s_frame.owners[slot];
            if(s_no_flicker && owner<24)continue;
            if ((wide || s_no_flicker) && (owner == 255 ? 0 : mm5_wide_rank(owner)) != rank) continue;
            if (replace && owner == 0) { if (slot == first) character_body(out, W, origin, &s_frame.body); }
            else sprite(out, W, origin, slot, oam);
            if(owner==0 && slot==first) {mm5_coop_draw(out,W,origin,s_opaque,s_claimed);partner=1;}
        }
        if (wide || s_no_flicker) mm5_wide_draw(out, W, origin, s_opaque, s_claimed, rank);
        if(s_no_flicker && !rank && !partner){mm5_coop_draw(out,W,origin,s_opaque,s_claimed);partner=1;}
    }
    if(!partner)mm5_coop_draw(out,W,origin,s_opaque,s_claimed);
    mm5_hud_draw(out,W,H,-1);
    cyc_render_set_native_origin(origin); return 1;
}
void mm5_view_power(void) {
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    memset(&s_frame, 0, sizeof s_frame); memset(s_frame.owners, 255, sizeof s_frame.owners);
}
static void refresh(void) {
    int active = s_enabled || s_wide || s_coop || s_no_flicker;
    nes_mod_set_function_hook_enabled("mm5.view.begin", active);
    nes_mod_set_function_hook_enabled("mm5.view.part", active);
    nes_mod_set_function_hook_enabled("mm5.view.dma", active);
    cyc_render_set_compositor(active ? render : NULL, NULL);
    cyc_render_set_stock_compositing((s_enabled || s_coop || s_no_flicker) != 0);
    cyc_render_capture_background(active != 0);
}
void mm5_view_enable(int enabled) { s_enabled = enabled != 0; refresh(); }
void mm5_view_wide(int enabled) { s_wide = enabled != 0; refresh(); }
void mm5_view_coop(int enabled) { s_coop = enabled != 0; refresh(); }
void mm5_view_no_flicker(int enabled) {s_no_flicker=enabled!=0;refresh();}
static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_frame + sizeof s_build) return -1;
    memcpy(s_build.magic, "MV02", 4); memcpy(s_frame.magic, "MV02", 4);
    memcpy(out, &s_build, sizeof s_build); memcpy(out + sizeof s_build, &s_frame, sizeof s_frame);
    return sizeof s_build + sizeof s_frame;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !(s_enabled || s_wide || s_coop || s_no_flicker);
    if (!in || len != sizeof s_build + sizeof s_frame) return 0;
    ViewFrame frames[2]; memcpy(frames, in, sizeof frames);
    for (int i = 0; i < 2; ++i) {
        const Mm5ProtoBody *b = &frames[i].body;
        if (memcmp(frames[i].magic, "MV02", 4) || frames[i].valid > 1 || frames[i].panels>1 || frames[i].native_scene>1 || b->pose >= CYC_PROTO_POSE_COUNT ||
            b->active > 1 || b->right > 1 || b->flipped > 1 || b->behind > 1 || b->weapon > 12) return 0;
        for (int j = 0; j < 64; ++j) if (frames[i].owners[j] != 255 && frames[i].owners[j] >= 24) return 0;
        for(int seat=0;seat<2;seat++)for(int y=0;y<30;y++)for(int x=0;x<33;x++)
            if(frames[i].palettes[seat][y][x]>3 || frames[i].present[seat][y][x]>1)return 0;
    }
    return 1;
}
static int set(const uint8_t *in, int len) {
    if (!valid(in, len)) return 0;
    if (!len) mm5_view_power();
    else { memcpy(&s_build, in, sizeof s_build); memcpy(&s_frame, in + sizeof s_build, sizeof s_frame); }
    return 1;
}
void mm5_view_register(void) {
    nes_mod_register_function_entry_plugin("mm5.view.begin", 0xDF5E, begin);
    nes_mod_register_function_entry_plugin("mm5.view.part", 0xE1F7, part);
    nes_mod_register_function_entry_plugin("mm5.view.dma", 0xC049, dma);
    nes_mod_register_savestate_hook("mm5.view", get, set); nes_mod_register_savestate_validator("mm5.view", valid);
}
