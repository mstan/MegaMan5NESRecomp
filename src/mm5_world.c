/* MM5's cartridge-native block/metatile decoder and conformance check. */
#include "mm5_world.h"
#include "mm5_cart.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include <stdio.h>
static FILE *s_audit;
static int s_audit_detail;
#define R(a) cyc_mod_peek((uint16_t)(a))

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
            if (R(0x680 + index) & (0x80 >> bit)) meta = 0;
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
