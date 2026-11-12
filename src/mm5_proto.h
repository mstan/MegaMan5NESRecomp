#pragma once
#include "cyc_protoman_atlas.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int16_t x, y;
    uint8_t pose, right, flipped, behind, active, state, weapon, charge;
    uint8_t palette[8], master[8];
} Mm5ProtoBody;
/* Presentation flag in the native-state byte; no guest state is changed. */
#define MM5_PROTO_VEHICLE 0x80
static inline int mm5_proto_native_part(const Mm5ProtoBody *body,int attr) {
    return (body->state&MM5_PROTO_VEHICLE) && (attr&3)==3;
}
bool mm5_proto_set(int enabled, int mode, int start);
void mm5_proto_register(void);
void mm5_proto_power(void);
void mm5_proto_end(void);
int mm5_proto_json(char *out, int capacity);
const CycProtoAtlas *mm5_proto_atlas(void);
int mm5_proto_active(void);
int mm5_proto_selected(void);
int mm5_proto_swapping(void);
void mm5_proto_cancel_swap(void);
int mm5_proto_try_shield(int enemy);
void mm5_proto_body(Mm5ProtoBody *body);
void mm5_view_power(void);
void mm5_view_register(void);
void mm5_view_enable(int enabled);
void mm5_view_wide(int enabled);
void mm5_view_coop(int enabled);
void mm5_view_draw_character(uint32_t *out, int width, int origin, const Mm5ProtoBody *body);
void mm5_view_no_flicker(int enabled);
int mm5_view_json(char *out,int capacity);
void mm5_view_menu_character(uint32_t *out,int width,int origin,int cursor,int character,const uint8_t palette[8]);
