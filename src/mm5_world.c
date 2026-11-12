/* MM5's cartridge-native block/metatile decoder and conformance check. */
#include "mm5_world.h"
#include "mm5_cart.h"
#include "mm5_pool.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
static FILE *s_audit;
static int s_audit_detail;
#define R(a) cyc_mod_peek((uint16_t)(a))

/* Retail retains two screen parities. Independent cameras retain each actual
 * screen instead; the native collision and metatile readers still own their
 * original calculations, including hazards and dynamic tagged overrides. */
typedef struct {char magic[4];uint8_t enabled,stage,bits[0x4F][32];} Terrain;
static Terrain T;
static uint8_t s_saved_bits[0x4F][32];
static int s_shadow;
static uint8_t bitmap(int screen,int index,uint8_t fallback) {
    return s_shadow && screen>=0 && screen<0x4F?T.bits[screen][index&31]:fallback;
}
static uint8_t terrain_read(uint16_t pc,uint16_t address,uint8_t value) {
    if(!s_shadow || address<0x680 || address>=0x6C0 || R(0x26)>=16)return value;
    int screen=-1;
    if(pc==0xC79B)screen=R(0x13);
    else if(pc==0xD74B)screen=R(0x47);
    else if(pc==0xD8E2 || (pc==0xA20A && R(0xF4)==5)) {
        CycModRegs r;cyc_mod_regs(&r);if(r.x<24)screen=R(0x348+r.x);
    }
    return bitmap(screen,address,value);
}
static int terrain_write(uint16_t address) {
    if(!s_shadow || R(0x26)>=16)return 0;
    CycModRegs r;cyc_mod_regs(&r);
    if(r.x<24 && r.y<64 && (address!=0xA20F || R(0xF4)==5)) {
        int screen=R(0x348+r.x);if(screen<0x4F)T.bits[screen][r.y&31]=r.a;
    }
    return 0;
}
static int terrain_reset(uint16_t address) {
    (void)address;if(s_shadow && !cyc_mod_isolated()){memset(T.bits,0,sizeof T.bits);T.stage=R(0x26)<16?R(0x26):255;}return 0;
}
void mm5_world_power(void) {memset(&T,0,sizeof T);T.stage=255;}
void mm5_world_shadow_set(int enabled) {
    enabled=enabled!=0;
    if(s_shadow && !enabled && R(0x26)<16)for(int page=R(0xF9);page<R(0xF9)+2 && page<0x4F;page++)
        for(int n=0;n<32;n++)cyc_mod_poke((uint16_t)(0x680+(page&1)*32+n),T.bits[page][n]);
    int stage=R(0x26)==16 && R(0x59)?R(0x7A3):R(0x26);
    if(enabled && !s_shadow && T.stage!=stage) {
        memset(T.bits,0,sizeof T.bits);
        T.stage=(uint8_t)(stage<16?stage:255);
        int page=R(0xF9);
        for(int i=0;i<2;i++)if(page+i<0x4F)for(int n=0;n<32;n++)T.bits[page+i][n]=R(0x680+((page+i)&1)*32+n);
    }
    s_shadow=enabled;cyc_mod_set_ram_read_hook(s_shadow?terrain_read:NULL);
    nes_mod_set_function_hook_enabled("mm5.world.break",s_shadow);
    nes_mod_set_function_hook_enabled("mm5.world.restore",s_shadow);
    nes_mod_set_function_hook_enabled("mm5.world.reset",s_shadow);
}
void mm5_world_private_begin(void) {if(s_shadow)memcpy(s_saved_bits,T.bits,sizeof T.bits);mm5_pool_private_begin();}
void mm5_world_private_end(int commit) {if(s_shadow && !commit)memcpy(T.bits,s_saved_bits,sizeof T.bits);mm5_pool_private_end(commit);}
static int terrain_get(uint8_t *out,int cap) {
    if(cap<sizeof T)return -1;memcpy(T.magic,"MT02",4);T.enabled=(uint8_t)s_shadow;memcpy(out,&T,sizeof T);return sizeof T;
}
static int terrain_valid(const uint8_t *in,int len) {
    return len==0?!s_shadow:in && len==sizeof T && !memcmp(in,"MT02",4) && in[4]==s_shadow && (in[5]<16 || in[5]==255);
}
static int terrain_set(const uint8_t *in,int len) {
    if(!terrain_valid(in,len))return 0;if(len)memcpy(&T,in,sizeof T);else mm5_world_power();return 1;
}
void mm5_world_register(void) {
    nes_mod_register_function_entry_plugin("mm5.world.break",0xD8E7,terrain_write);
    nes_mod_register_function_entry_plugin("mm5.world.restore",0xA20F,terrain_write);
    nes_mod_register_function_entry_plugin("mm5.world.reset",0xC3B8,terrain_reset);
    nes_mod_register_savestate_hook("mm5.terrain",terrain_get,terrain_set);nes_mod_register_savestate_validator("mm5.terrain",terrain_valid);
}

int mm5_world_tile(int screen, int x, int y, Mm5WorldTile *out) {
    int stage = R(0x26), art = R(0x27);
    if (!out || stage >= 16 || screen < 0 || screen >= 0x4F || x < 0 || x > 255 || y < 0 || y > 255 || !mm5_cart_ready()) return 0;
    if (stage == 14) art = mm5_cart_prg(30, 0xD793 + screen);
    if (art >= 32) return 0;
    int layout = mm5_cart_prg(stage, 0xA900 + screen), block_pos = (y / 32) * 8 + x / 32;
    /* D758 maps the art bank before reading the layout through ($20),Y. */
    int block = mm5_cart_prg(art, 0xB600 + layout * 64 + block_pos);
    int quadrant = ((x & 16) ? 1 : 0) | ((y & 16) ? 2 : 0);
    int meta = mm5_cart_prg(art, 0xB200 + block * 4 + quadrant);
    int replaced = 0;
    if (!R(0x59)) {
        for (int i = R(0x43) - 4; i >= 0; i -= 4) {
            if (R(0x6C0 + i) == screen && R(0x6C1 + i) == block_pos && R(0x6C2 + i) == quadrant) {
                meta = R(0x6C3 + i); replaced = 1; break;
            }
        }
        if (!replaced && !R(0x1E)) {
            int bit = ((block_pos & 1) << 2) | quadrant;
            int index = (block_pos >> 1) | ((screen & 1) ? 32 : 0);
            if (bitmap(screen,index,R(0x680 + index)) & (0x80 >> bit)) meta = 0;
        }
    }
    /* D50B emits buffer columns: AD/AE are top-left/top-right;
     * AF/B0 are bottom-left/bottom-right (both levels are row-major). */
    int plane = ((x & 8) ? 1 : 0) | ((y & 8) ? 2 : 0);
    out->tile = mm5_cart_prg(art, 0xAD00 + plane * 256 + meta);
    out->palette = mm5_cart_prg(art, 0xB100 + meta) & 3;
    out->metatile = (uint8_t)meta; out->art_bank = (uint8_t)art;
    return 1;
}
int mm5_world_class(int world_x,int y) {
    Mm5WorldTile tile;
    if(world_x<0 || y<0 || y>=240 || !mm5_world_tile(world_x/256,world_x&255,y,&tile))return -1;
    return mm5_cart_prg(tile.art_bank,0xB100+tile.metatile)&0xF0;
}
int mm5_world_audit_file(const char *path) {
    if (s_audit) fclose(s_audit);
    s_audit = fopen(path, "w");
    s_audit_detail = 0;
    if (s_audit) fputs("frame,stage,camera,compared,mismatched\n", s_audit);
    return s_audit != NULL;
}
void mm5_world_audit(const uint32_t *bg) {
    if (!s_audit || !bg || R(0x26) >= 16 || R(0x59) || R(0x1B) || !(R(0x29) & 32) || R(0xFA)) return;
    int camera = (R(0xF9) << 8) | R(0xFC), compared = 0, mismatched = 0;
    for (int y = 0; y < 240; ++y) {
        int delta = (cyc_render_line_scroll_x(y) - (camera & 511) + 256) & 511;
        int actual = camera + delta - 256, sy = cyc_render_line_scroll_y(y) % 240;
        for (int x = 0; x < 256; ++x) {
            int wx = actual + x; Mm5WorldTile t;
            if (wx < 0 || !mm5_world_tile(wx / 256, wx & 255, sy, &t)) continue;
            int value = 0, mask = cyc_render_line_mask(y);
            if ((mask & 8) && (x >= 8 || (mask & 2))) {
                int addr = cyc_render_line_bg_table(y) + t.tile * 16 + (sy & 7), bit = 7 - (wx & 7);
                value = ((cyc_render_chr((uint16_t)addr) >> bit) & 1) | (((cyc_render_chr((uint16_t)(addr + 8)) >> bit) & 1) << 1);
            }
            uint32_t color = cyc_render_color(value ? t.palette * 4 + value : 0);
            compared++;
            if (color != bg[y * 256 + x]) {
                mismatched++;
                if (!s_audit_detail && mismatched <= 16)
                    fprintf(stderr, "terrain mismatch: x=%d y=%d wx=%d sy=%d tile=%02x pal=%u decoded=%08x native=%08x\n",
                        x, y, wx, sy, t.tile, t.palette, color, bg[y * 256 + x]);
            }
        }
    }
    fprintf(s_audit, "%u,%u,%d,%d,%d\n", cyc_ring_frame, R(0x26), camera, compared, mismatched);
    s_audit_detail = 1;
}
