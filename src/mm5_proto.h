#pragma once
#include "cyc_protoman_atlas.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int16_t x, y;
    uint8_t pose, right, flipped, behind, active, state, weapon, charge;
    uint8_t palette[8], master[8];
} Mm5ProtoBody;
bool mm5_proto_set(int enabled, int mode, int start);
void mm5_proto_register(void);
void mm5_proto_power(void);
void mm5_proto_end(void);
int mm5_proto_json(char *out, int capacity);
const CycProtoAtlas *mm5_proto_atlas(void);
int mm5_proto_active(void);
int mm5_proto_selected(void);
void mm5_proto_body(Mm5ProtoBody *body);
void mm5_view_power(void);
void mm5_view_register(void);
void mm5_view_enable(int enabled);
