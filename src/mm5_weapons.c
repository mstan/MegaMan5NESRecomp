/* Optional L/R actions use native equipment tables from the loaded cartridge. */
#include "mm5_weapons.h"
#include "mm5_cart.h"
#include "mm5_proto.h"
#include "mm5_coop.h"
#include "cyc_input.h"
#include "cyc_mod.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include "mod_function_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_on;
typedef struct {
    char magic[4];
    uint8_t enabled, previous, pending, reserved;
    uint64_t changes;
} WeaponState;
static WeaponState s_players[2];
#define W (s_players[mm5_coop_seat()])
#define READ(a) cyc_mod_peek((uint16_t)(a))
#define WRITE(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))

static int cartridge(void) {
    return mm5_cart_ready();
}
static uint8_t source(int bank, int address) {
    return mm5_cart_prg(bank, address);
}
static int safe(void) {
    /* $28 retains 1/2 during normal horizontal scrolling, including rest.
     * Its vertical transition directions are 4/8. */
    return !mm5_proto_swapping() && READ(0x26) < 16 && READ(0x30) <= 3 && (READ(0xB0) & 31) &&
        !READ(0x54) && !READ(0x59) && !READ(0x1B) && READ(0x28) < 4 && !READ(0x5A) &&
        !READ(0x390);
}
static int owned(int id) { return !id || (READ(0xB0 + id) & 128); }

void mm5_weapons_set(int enabled) {
    s_on = enabled != 0;
    for(int i=0;i<2;i++)s_players[i].previous=s_players[i].pending=0;
    nes_mod_set_function_hook_enabled("mm5.player", s_on);
    nes_mod_set_local_only("MM5 weapon switching (unqualified netplay)", s_on);
}
void mm5_weapons_power(void) {
    memset(s_players,0,sizeof s_players);
}
void mm5_weapons_input(void) {
    int allowed=mm5_coop_enabled()?(READ(0x26)<16 && !READ(0x54) && !READ(0x59) && !READ(0x1B) && READ(0x28)<4 && !READ(0x5A)):safe();
    for(int seat=0;seat<2;seat++) {
        unsigned held=seat && !mm5_coop_enabled()?0:nes_input_game_buttons(seat+1)&(CYC_GAME_L|CYC_GAME_R);
        unsigned fresh=held&~s_players[seat].previous;
        s_players[seat].pending=(uint8_t)((s_on && allowed && held!=(CYC_GAME_L|CYC_GAME_R))?fresh:0);
        s_players[seat].previous=(uint8_t)held;
    }
}
void mm5_weapons_end(void) { s_players[0].pending=s_players[1].pending=0; }

int mm5_weapons_dispatch(uint16_t address) {
    (void)address;
    unsigned fresh = W.pending;
    W.pending = 0;
    if (!s_on || (cyc_mod_isolated() && !mm5_coop_native_scope()) || !safe() || !cartridge() ||
        (fresh != CYC_GAME_L && fresh != CYC_GAME_R)) return 0;
    int current = READ(0x32), next = current, step = fresh == CYC_GAME_L ? -1 : 1;
    if (current > 12) return 0;
    do { next = (next + step + 13) % 13; } while (next != current && !owned(next));
    if (next == current) return 0;
    int cursor;
    for (cursor = 0; cursor < 16; ++cursor)
        if (cursor != 7 && cursor != 15 && source(1, 0x85E1 + cursor) == next) break;
    if (cursor == 16) return 0;

    /* The native player owns slots 1..7; world actors start at slot 8.
     * Retiring a helper clears its carrier reference without touching enemies. */
    for (int base = 0x300; base <= 0x5B8; base += 0x18)
        for (int slot = 1; slot <= 7; ++slot) WRITE(base + slot, 0);
    WRITE(0x37, 0); WRITE(0x38, 0); WRITE(0x5B, 0);
    int offset = READ(0x34), pose = READ(0x558);
    if (offset && pose >= offset) WRITE(0x558, pose - offset);
    WRITE(0x33, 0); WRITE(0x34, 0);
    /* Charge Kick and the ordinary slide have different native poses. */
    if (READ(0x558) == 0xB0 && next != 8) {
        WRITE(0x558, 0x10); WRITE(0x540, 0); WRITE(0x570, 0);
    }
    WRITE(0x32, next); WRITE(0x2E, next ? next | 128 : 0); WRITE(0x50, cursor);
    WRITE(0xED, source(1, 0x85F1 + cursor));
    /* Same swatch and backup used by the original inventory and stage reload. */
    for (int i = 0; i < 3; ++i) {
        uint8_t color = source(1, 0x854B + cursor * 4 + i);
        WRITE(0x611 + i, color); WRITE(0x631 + i, color);
    }
    WRITE(0x610, READ(0x600)); WRITE(0x18, 255);
    int q = READ(0xDA) & 7;
    if (READ(0xDC + q) == 0x88) { WRITE(0xDC + q, 0x27); WRITE(0xDA, (q + 1) & 7); }
    W.changes++;
    return 0;
}

static int get(uint8_t *out, int cap) {
    if (cap < sizeof s_players) return -1;
    for(int i=0;i<2;i++){memcpy(s_players[i].magic,"MW02",4);s_players[i].enabled=(uint8_t)s_on;}
    memcpy(out,s_players,sizeof s_players);return sizeof s_players;
}
static int valid(const uint8_t *in, int len) {
    if (!len) return !s_on;
    if (!in || len != sizeof s_players) return 0;
    WeaponState next[2];memcpy(next,in,sizeof next);
    for(int i=0;i<2;i++)if(memcmp(next[i].magic,"MW02",4)||next[i].enabled!=s_on||next[i].previous>3||next[i].pending>3)return 0;
    return 1;
}
static int set(const uint8_t *in, int len) {
    if (!valid(in, len)) return 0;
    if (len) memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);
    return 1;
}
void mm5_weapons_register(void) {
    nes_mod_register_savestate_hook("mm5.weapons", get, set);
    nes_mod_register_savestate_validator("mm5.weapons", valid);
}
int mm5_weapons_json(char *out, int cap) {
    return snprintf(out, cap, "\"weapons\":{\"on\":%d,\"id\":%u,\"held\":%u,\"pending\":%u,\"changes\":%llu,\"chr\":%u,\"palette\":[%u,%u,%u]},\"stage\":%u,\"state\":%u,\"hp\":%u,\"charge\":%u",
        s_on, READ(0x32), W.previous, W.pending, (unsigned long long)W.changes,
        READ(0xED), READ(0x611), READ(0x612), READ(0x613),
        READ(0x26), READ(0x30), READ(0xB0) & 31, READ(0x38));
}
