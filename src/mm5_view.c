/* Replace player-owned OAM with immutable art; CHR-ROM and guest timing stay native. */
#include "mm5_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>

typedef struct {
    char magic[4];
    uint8_t valid, owners[64];
    Mm5ProtoBody body;
} ViewFrame;
static ViewFrame s_build, s_frame;
static int s_enabled;
static uint32_t s_bg[256 * 240];
static uint8_t s_opaque[256 * 240], s_allowed[64][240];

static int begin(uint16_t address) {
    (void)address;
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
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
    memcpy(&s_frame, &s_build, sizeof s_frame);
    return 0;
}
static int background_pixel(int x, int y) {
    if (!(cyc_render_line_mask(y) & 8) || (x < 8 && !(cyc_render_line_mask(y) & 2))) return 0;
    int sx = cyc_render_line_scroll_x(y) + x, sy = cyc_render_line_scroll_y(y);
    sx &= 511; sy %= 480;
    int nx = sx / 256, ny = sy / 240, tx = (sx & 255) / 8, ty = (sy % 240) / 8;
    uint16_t nt = (uint16_t)(0x2000 + nx * 1024 + ny * 2048);
    int tile = cyc_render_nametable((uint16_t)(nt + ty * 32 + tx));
    uint16_t a = (uint16_t)(cyc_render_line_bg_table(y) + tile * 16 + (sy % 240) % 8);
    int bit = 7 - (sx & 7);
    return ((cyc_render_chr(a) >> bit) & 1) | (((cyc_render_chr(a + 8) >> bit) & 1) << 1);
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
        if (px < 0 || px >= 256 || py < 0 || py >= 240 ||
            !(cyc_render_line_mask(py) & 16) || (px < 8 && !(cyc_render_line_mask(py) & 4)) ||
            (b->behind && s_opaque[py * 256 + px])) continue;
        out[py * W + origin + px] = color(index, b);
    }
}
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
static int inventory(uint32_t *out, int W, int H, const uint32_t *native) {
    const CycProtoAtlas *atlas = mm5_proto_atlas();
    if (!atlas || !mm5_proto_selected() || W < 256 || H != 240 || !native) return 0;
    int origin = (W - 256) / 2;
    for (int i = 0; i < W * H; ++i) out[i] = 0xFF000000u;
    for (int y = 0; y < H; ++y) memcpy(out + y * W + origin, native + y * 256, 256 * sizeof *out);
    /* The retail menu's fixed 16x16 life icon, above its original life count. */
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        uint32_t c = atlas->portrait[y * 16 + x];
        out[(192 + y) * W + origin + 208 + x] = c ? menu_color(c) : 0xFF000000u;
    }
    int cursor = cyc_mod_peek(0x50);
    /* Rush/Beat keep their own native previews. The other rows preview the
     * selected character on the same bottom-panel grid and foot baseline. */
    if (cursor != 6 && cursor != 12 && cursor != 13 && cursor != 14) {
        const CycProtoPose *p = &atlas->poses[CYC_PROTO_IDLE0];
        for (int y = 184; y < 217; ++y) for (int x = 140; x < 178; ++x) out[y * W + origin + x] = 0xFF000000u;
        Mm5ProtoBody body = {0}; body.weapon = cursor != 0;
        for (int i = 0; i < 8; ++i) body.palette[i] = cyc_mod_peek((uint16_t)(0x610 + i));
        for (int y = 0; y < p->height; ++y) for (int x = 0; x < p->width; ++x) {
            int index = p->pixels[y * 32 + x]; if (!index) continue;
            int px = 160 + x - p->origin_x, py = 205 + p->top_y + y;
            out[py * W + origin + px] = menu_color(color(index, &body));
        }
    }
    cyc_render_set_native_origin(origin); return 1;
}
static void sprite(uint32_t *out, int W, int origin, int slot, const uint8_t *oam) {
    const uint8_t *o = oam + slot * 4;
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
            if (!v || px >= 256 || (px < 8 && !(cyc_render_line_mask(py) & 4)) ||
                ((o[2] & 32) && s_opaque[py * 256 + px])) continue;
            out[py * W + origin + px] = cyc_render_color(16 + (o[2] & 3) * 4 + v);
        }
    }
}
static int render(uint32_t *out, int W, int H, int native_x0, const uint32_t *native, void *ctx) {
    (void)ctx; (void)native_x0;
    /* The inventory swaps to a pseudo-stage without rebuilding world OAM.
     * Never paint the last latched gameplay actor over its native panels. */
    if (s_enabled && cyc_mod_peek(0x26) == 16 && cyc_mod_peek(0x59)) return inventory(out, W, H, native);
    if (cyc_mod_peek(0x26) >= 16 || cyc_mod_peek(0x59) || cyc_mod_peek(0x1B)) return 0;
    if (!s_enabled || !s_frame.valid || !s_frame.body.active || W < 256 || H != 240 ||
        !cyc_render_background(s_bg)) return 0;
    int origin = (W - 256) / 2;
    for (int i = 0; i < W * H; ++i) out[i] = 0xFF000000u;
    for (int y = 0; y < H; ++y) {
        memcpy(out + y * W + origin, s_bg + y * 256, 256 * sizeof *out);
        for (int x = 0; x < 256; ++x) s_opaque[y * 256 + x] = (uint8_t)(background_pixel(x, y) != 0);
    }
    const uint8_t *oam = cyc_render_oam();
    uint8_t counts[240] = {0};
    memset(s_allowed, 0, sizeof s_allowed);
    int first = 64;
    for (int slot = 0; slot < 64; ++slot) {
        int top = oam[slot * 4] + 1, height = cyc_render_line_sprite16(top < 240 ? top : 239) ? 16 : 8;
        for (int y = top; y < top + height && y < 240; ++y) s_allowed[slot][y] = counts[y]++ < 8;
        if (s_frame.owners[slot] == 0 && slot < first) first = slot;
    }
    for (int slot = 63; slot >= 0; --slot) {
        if (s_frame.owners[slot] == 0) { if (slot == first) character_body(out, W, origin, &s_frame.body); }
        else sprite(out, W, origin, slot, oam);
    }
    cyc_render_set_native_origin(origin); return 1;
}
void mm5_view_power(void) {
    memset(&s_build, 0, sizeof s_build); memset(s_build.owners, 255, sizeof s_build.owners);
    memset(&s_frame, 0, sizeof s_frame); memset(s_frame.owners, 255, sizeof s_frame.owners);
}
void mm5_view_enable(int enabled) {
    s_enabled = enabled != 0;
    nes_mod_set_function_hook_enabled("mm5.view.begin", s_enabled);
    nes_mod_set_function_hook_enabled("mm5.view.part", s_enabled);
    nes_mod_set_function_hook_enabled("mm5.view.dma", s_enabled);
    cyc_render_set_compositor(s_enabled ? render : NULL, NULL);
    cyc_render_set_stock_compositing(s_enabled != 0);
    cyc_render_capture_background(s_enabled != 0);
}
static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_frame + sizeof s_build) return -1;
    memcpy(s_build.magic, "MV01", 4); memcpy(s_frame.magic, "MV01", 4);
    memcpy(out, &s_build, sizeof s_build); memcpy(out + sizeof s_build, &s_frame, sizeof s_frame);
    return sizeof s_build + sizeof s_frame;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !s_enabled;
    if (!in || len != sizeof s_build + sizeof s_frame) return 0;
    ViewFrame frames[2]; memcpy(frames, in, sizeof frames);
    for (int i = 0; i < 2; ++i) {
        const Mm5ProtoBody *b = &frames[i].body;
        if (memcmp(frames[i].magic, "MV01", 4) || frames[i].valid > 1 || b->pose >= CYC_PROTO_POSE_COUNT ||
            b->active > 1 || b->right > 1 || b->flipped > 1 || b->behind > 1 || b->weapon > 12) return 0;
        for (int j = 0; j < 64; ++j) if (frames[i].owners[j] != 255 && frames[i].owners[j] >= 24) return 0;
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
