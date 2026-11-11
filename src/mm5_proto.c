/* Native MM5 physics/charge/slide remain authoritative for either character. */
#include "mm5_proto.h"
#include "mm5_proto_sheet.h"
#include "mm5_cart.h"
#include "mm5_coop.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>

#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a), (uint8_t)(v))
static CycProtoAtlas s_atlas;
static int s_loaded, s_on, s_mode = 1, s_start = 1;
typedef struct {
    char magic[4];
    uint8_t enabled, mode, selected, teleport, saved_stun;
    uint8_t reflected[3];
    uint8_t damage_active, damage_weapon, damage_charge;
    uint8_t extent_active, extent_weapon, extent_charge;
    uint64_t switches, blocks, reflections, limited;
} ProtoState;
static ProtoState s_players[2];
#define S (s_players[mm5_coop_seat()])

static uint8_t animation_byte(int bank, int address) {
    return mm5_cart_prg(bank + (address >= 0xA000), address);
}
static int drawn_frame(void) {
    if (!mm5_cart_ready()) return -1;
    int bank = mm5_cart_prg(31, 0xE33B + R(0x300)), subtype = R(0x558);
    if (bank >= 31) return -1;
    int pointer = animation_byte(bank, 0x8600 + subtype) | (animation_byte(bank, 0x8700 + subtype) << 8);
    if (pointer < 0x8000 || pointer >= 0xBFFE) return -1;
    return animation_byte(bank, pointer + 2 + (R(0x540) & 0x7F));
}

static int selected(void) { int character=mm5_coop_character();return s_on && (character>=0?character:S.selected); }
int mm5_proto_selected(void) { return selected(); }
int mm5_proto_swapping(void) { return s_on && !mm5_coop_enabled() && S.teleport; }
int mm5_proto_active(void) { return selected() || mm5_proto_swapping(); }
void mm5_proto_cancel_swap(void) { if(S.teleport){P(0x5B8,S.saved_stun);S.teleport=0;} }
const CycProtoAtlas *mm5_proto_atlas(void) { return s_loaded ? &s_atlas : NULL; }
static int safe(void) {
    int state = R(0x30);
    return R(0x26) < 16 && (state == 0 || state == 1 || state == 3) &&
        !R(0x1B) && !R(0xFA) && !R(0x54) && !R(0x59) && !R(0x5A) &&
        !R(0x390) && (R(0xB0) & 31);
}
static void sound(int id) {
    int q = R(0xDA) & 7;
    if (R(0xDC + q) == 0x88) { P(0xDC + q, id); P(0xDA, (q + 1) & 7); }
}
static int shield_up(void) {
    return selected() && s_mode && !S.teleport && R(0x30) == 1 &&
        !R(0x34) && !R(0x54) && !R(0xFA) && !R(0x390);
}
static int controller(uint16_t address) {
    (void)address;
    if (!s_on || mm5_coop_enabled() || cyc_mod_isolated()) return 0;
    if (!S.teleport && (R(0x14) & 0x20) && safe()) {
        S.teleport = 1; S.saved_stun = R(0x5B8); S.switches++;
        P(0x38, 0); P(0x5B, 0); sound(0x35);
    }
    if (!S.teleport) return 0;
    P(0x5B8, 0x80);
    return 1;
}
static int shot(uint16_t address) {
    (void)address;
    if (!selected() || (cyc_mod_isolated() && !mm5_coop_native_scope())) return 0;
    CycModRegs regs; cyc_mod_regs(&regs);
    if (regs.y >= 1 && regs.y <= 3) S.reflected[regs.y - 1] = 0;
    /* MM5 begins release processing at $10 but keeps the small Buster tier
     * until the cartridge's first size threshold, $28. */
    if (!s_mode || R(0x38) >= 0x28) return 0;
    int count = 0;
    for (int slot = 1; slot <= 3; ++slot)
        count += R(0x300 + slot) == 0x70 && !S.reflected[slot - 1] && R(0x468 + slot) < 0x28;
    if (count < 2) return 0;
    S.limited++; return 1;
}
static int muzzle_y(void) {
    int state = R(0x30), offset = state == 1 ? -5 : state == 3 ? -4 : 3;
    int pose = R(0x558) - R(0x34);
    if (state == 0 && pose == 4 && (R(0x540) & 3) == 1) offset--;
    return R(0x378) + offset;
}
static int muzzle(uint16_t address) {
    (void)address;
    if (!selected() || (cyc_mod_isolated() && !mm5_coop_native_scope())) return 0;
    CycModRegs r; cyc_mod_regs(&r);
    int y = muzzle_y();
    if (r.y >= 1 && r.y <= 3 && y >= 0 && y < 240) P(0x378 + r.y, y);
    return 0;
}
static int damage(uint16_t address) {
    (void)address;
    if (!selected() || !s_mode || S.teleport || (cyc_mod_isolated() && !mm5_coop_native_scope())) return 0;
    CycModRegs r; cyc_mod_regs(&r);
    int amount = R(0x85C3 + r.y);
    /* SEC has run. The native SBC and its death/immunity paths still decide. */
    r.a = (uint8_t)(r.a > amount ? r.a - amount : 0);
    cyc_mod_set_regs(&r); return 0;
}
static int reflected_slot(int slot) {
    return slot >= 1 && slot <= 3 && S.reflected[slot - 1] && R(0x300 + slot) == 0x70;
}
static int reflected_damage(uint16_t address) {
    if (cyc_mod_isolated() && !mm5_coop_native_scope()) return 0;
    if (address == 0x809D) {
        if (!s_on || !reflected_slot(R(0x10))) return 0;
        S.damage_active = 1; S.damage_weapon = R(0x32); S.damage_charge = R(0x5B);
        P(0x32, 0); P(0x5B, 0);
    } else if (S.damage_active) {
        P(0x32, S.damage_weapon); P(0x5B, S.damage_charge); S.damage_active = 0;
    }
    return 0;
}
static int reflected_extent(uint16_t address) {
    if (cyc_mod_isolated() && !mm5_coop_native_scope()) return 0;
    if (address == 0xF081) {
        if (!s_on || !reflected_slot(R(0x02))) return 0;
        S.extent_active = 1; S.extent_weapon = R(0x32); S.extent_charge = R(0x5B);
        P(0x32, 0); P(0x5B, 0);
    } else if (S.extent_active) {
        P(0x32, S.extent_weapon); P(0x5B, S.extent_charge); S.extent_active = 0;
    }
    return 0;
}
static void reflect(int enemy) {
    int slot;
    for (slot = 1; slot <= 3; ++slot) if (!R(0x300 + slot)) break;
    if (slot <= 3 && cyc_mod_isolate_begin()) {
        CycModRegs r; cyc_mod_regs(&r); r.a = 0x1B;
        int ok = cyc_mod_call(0xFF3D, &r);
        P(0x38, 0); P(0x32, 0); r.x = 0; r.y = (uint8_t)slot;
        if (ok) ok = cyc_mod_call(0x918B, &r);
        uint8_t data[30]; int n = 0;
        for (int base = 0x300; base <= 0x5B8; base += 0x18) data[n++] = R(base + slot);
        cyc_mod_isolate_end();
        if (ok) {
            n = 0;
            for (int base = 0x300; base <= 0x5B8; base += 0x18) P(base + slot, data[n++]);
            P(0x330 + slot, R(0x330 + enemy)); P(0x348 + slot, R(0x348 + enemy));
            P(0x378 + slot, R(0x378 + enemy)); P(0x390 + slot, 0);
            S.reflected[slot - 1] = 1; S.reflections++;
        }
    }
    P(0x300 + enemy, 0); P(0x5A0 + enemy, 0); P(0x528 + enemy, 0);
    S.blocks++; sound(0x1E);
}
int mm5_proto_try_shield(int enemy) {
    if (!shield_up() || cyc_mod_isolated()) return 0;
    if (enemy < 8 || enemy >= 24 || !(R(0x408 + enemy) & 128)) return 0;
    int type = R(0x300 + enemy);
    /* Verified straight projectiles; bosses, bodies and large attacks stay dangerous. */
    if (type != 0x55 && type != 0x82 && type != 0x94 && type != 0x97) return 0;
    int x = (R(0x348) << 8) | R(0x330), ex = (R(0x348 + enemy) << 8) | R(0x330 + enemy);
    int dx = ex - x, dy = (int)R(0x378 + enemy) - R(0x378);
    int right = (R(0x528) & 32) != 0;
    if ((right ? dx < 0 || dx > 20 : dx > 0 || dx < -20) || dy < -16 || dy > 12) return 0;
    reflect(enemy); return 1;
}
static int shield(uint16_t address) {
    (void)address;
    CycModRegs r; cyc_mod_regs(&r);
    if (!mm5_proto_try_shield(r.x)) return 0;
    r.p |= 1; cyc_mod_set_regs(&r); return 1;
}

void mm5_proto_body(Mm5ProtoBody *body) {
    memset(body, 0, sizeof *body);
    body->active = (uint8_t)(mm5_proto_active() && R(0x26) < 16); body->state = R(0x30);
    body->x = R(0x13); body->y = R(0x12);
    body->right = (R(0x528) & 32) != 0; body->flipped = (R(0x528) & 64) != 0;
    body->behind = (R(0x528) & 16) != 0; body->weapon = R(0x32); body->charge = R(0x38);
    for (int i = 0; i < 8; ++i) { body->palette[i] = R(0x610 + i); body->master[i] = R(0x630 + i); }
    int state = body->state, firing = R(0x34) != 0;
    int throwing = R(0x34) >= 2 && body->weapon != 0;
    int base = R(0x558) - R(0x34), phase = R(0x540) & 3;
    int native_frame = drawn_frame();
    /* Frameset1A is the blinking READY word (frames39/40), owned by
     * actor0 before its actual frameset13 arrival. It is native scene text. */
    if(state==8 && base==0x1A) {body->active=0;return;}
    static const int run[] = {0, 1, 2, 1};
    if (S.teleport) {
        body->pose = CYC_PROTO_TELEPORT;
        body->y -= (S.teleport <= 16 ? S.teleport - 1 : 32 - S.teleport) * 16;
    } else if (state == 8 || state == 0x10 || state == 0x15 || state == 0x16) {
        body->pose = (uint8_t)(native_frame == 11 ? CYC_PROTO_TELEPORT_PARTIAL : native_frame == 12 ? CYC_PROTO_TELEPORT_FULL : CYC_PROTO_TELEPORT);
    } else if (state == 6) {
        /* MM5's native hurt frameset alternates body frame19 and sparks23.
         * Preserve its actual effect rather than replacing sparks with a body. */
        if (native_frame == 23) body->active = 0;
        body->pose = CYC_PROTO_HURT;
    }
    else if (state == 2) body->pose = CYC_PROTO_SLIDE;
    else if (state == 3) {
        body->pose = firing ? CYC_PROTO_LADDER_FIRE0 : base == 0x14 ? CYC_PROTO_LADDER_TOP0 : CYC_PROTO_LADDER0;
        if (!firing) body->right = (uint8_t)(phase & 1);
    } else if (state == 1) body->pose = firing ? (throwing ? CYC_PROTO_THROW_JUMP : CYC_PROTO_JUMP_FIRE) :
        shield_up() ? CYC_PROTO_JUMP_GUARD : CYC_PROTO_JUMP_OPEN;
    else if (base == 4) body->pose = (uint8_t)((throwing ? CYC_PROTO_THROW_RUN0 : firing ? CYC_PROTO_FIRE_RUN0 : CYC_PROTO_RUN0) + run[phase]);
    else if (base == 0x0D) body->pose = (uint8_t)(CYC_PROTO_STEP0 + (phase & 1));
    else body->pose = (uint8_t)(throwing ? CYC_PROTO_THROW_IDLE : firing ? CYC_PROTO_FIRE_IDLE0 : CYC_PROTO_IDLE0 + ((R(0x92) >> 3) & 3));
}
void mm5_proto_power(void) {
    memset(s_players, 0, sizeof s_players); s_players[0].selected = (uint8_t)s_start;s_players[1].selected=(uint8_t)!s_start;
    mm5_view_power();
}
void mm5_proto_end(void) {
    for(int seat=0;seat<(mm5_coop_enabled()?2:1);seat++)for(int slot=1;slot<=3;slot++)
        if(mm5_coop_read(seat,(uint16_t)(0x300+slot))!=0x70)s_players[seat].reflected[slot-1]=0;
    if(mm5_coop_enabled())return;
    if (!s_on || !S.teleport) return;
    if (S.teleport == 16) { S.selected ^= 1; sound(0x14); }
    if (++S.teleport > 32) { S.teleport = 0; P(0x5B8, S.saved_stun); }
}
static const char *const HOOKS[] = {
    "mm5.proto.controller", "mm5.proto.shot", "mm5.proto.muzzle", "mm5.proto.damage", "mm5.proto.shield",
    "mm5.proto.reflected-damage", "mm5.proto.reflected-damage-end", "mm5.proto.reflected-extent", "mm5.proto.reflected-extent-end"
};
bool mm5_proto_set(int enabled, int mode, int start) {
    if (enabled && !s_loaded) {
        char why[160];
        if (!cyc_protoman_atlas_load(&s_atlas, MM5_PROTO_PNG, sizeof MM5_PROTO_PNG,
            MM5_PROTO_JUMP_PNG, sizeof MM5_PROTO_JUMP_PNG, why, sizeof why)) {
            fprintf(stderr, "[Proto Man] %s\n", why); return false;
        }
        s_loaded = 1;
    }
    s_start = start != 0; s_mode = mode != 0;
    if (enabled && (!s_on || !cyc_ring_frame)) S.selected = (uint8_t)s_start;
    if (!enabled && S.teleport) { P(0x5B8, S.saved_stun); S.teleport = 0; }
    if (!enabled && S.damage_active) {
        P(0x32, S.damage_weapon); P(0x5B, S.damage_charge); S.damage_active = 0;
    }
    if (!enabled && S.extent_active) {
        P(0x32, S.extent_weapon); P(0x5B, S.extent_charge); S.extent_active = 0;
    }
    s_on = enabled != 0;
    for (int i = 0; i < sizeof HOOKS / sizeof *HOOKS; ++i) nes_mod_set_function_hook_enabled(HOOKS[i], s_on);
    nes_mod_set_local_only("MM5 Proto Man (unqualified netplay)", s_on);
    mm5_view_enable(s_on); return true;
}
static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_players) return -1;
    for(int i=0;i<2;i++){memcpy(s_players[i].magic,"MP02",4);s_players[i].enabled=(uint8_t)s_on;s_players[i].mode=(uint8_t)s_mode;}
    memcpy(out,s_players,sizeof s_players);return sizeof s_players;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !s_on;
    if (!in || len != sizeof s_players) return 0;
    ProtoState p[2];memcpy(p,in,sizeof p);
    for(int i=0;i<2;i++)if(memcmp(p[i].magic,"MP02",4)||p[i].enabled!=s_on||p[i].mode!=s_mode||p[i].selected>1||p[i].teleport>32||
        p[i].damage_active>1||p[i].extent_active>1||p[i].damage_weapon>12||p[i].extent_weapon>12||p[i].reflected[0]>1||p[i].reflected[1]>1||p[i].reflected[2]>1)return 0;
    return 1;
}
static int set(const uint8_t *in, int len) {
    if (!valid(in, len)) return 0;
    if (len) memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);
    return 1;
}
void mm5_proto_register(void) {
    nes_mod_register_function_entry_plugin(HOOKS[0], 0x8000, controller);
    nes_mod_register_function_entry_plugin(HOOKS[1], 0x918B, shot);
    nes_mod_register_function_entry_plugin(HOOKS[2], 0x922D, muzzle);
    nes_mod_register_function_entry_plugin(HOOKS[3], 0x82D6, damage);
    nes_mod_register_function_entry_plugin(HOOKS[4], 0xEF87, shield);
    nes_mod_register_function_entry_plugin(HOOKS[5], 0x809D, reflected_damage);
    nes_mod_register_function_entry_plugin(HOOKS[6], 0x8225, reflected_damage);
    nes_mod_register_function_entry_plugin(HOOKS[7], 0xF081, reflected_extent);
    nes_mod_register_function_entry_plugin(HOOKS[8], 0xF0B0, reflected_extent);
    nes_mod_register_savestate_hook("mm5.proto", get, set); nes_mod_register_savestate_validator("mm5.proto", valid);
    mm5_view_register();
}
int mm5_proto_json(char *out, int cap) {
    return snprintf(out, cap, "\"proto\":{\"on\":%d,\"mode\":%d,\"selected\":%u,\"teleport\":%u,\"switches\":%llu,\"blocks\":%llu,\"reflections\":%llu,\"limited\":%llu,\"shots\":[%u,%u,%u],\"shot_charge\":[%u,%u,%u],\"player\":[%u,%u,%u],\"animation\":[%u,%u,%u,%d]}",
        s_on, s_mode, selected(), S.teleport, (unsigned long long)S.switches, (unsigned long long)S.blocks,
        (unsigned long long)S.reflections, (unsigned long long)S.limited,
        R(0x301), R(0x302), R(0x303), R(0x469), R(0x46A), R(0x46B), R(0x330), R(0x378), R(0x5B8),
        R(0x558), R(0x540), R(0x570), drawn_frame());
}
