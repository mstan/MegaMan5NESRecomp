#pragma once
#include <stdint.h>
typedef struct { uint8_t tile, palette, metatile, art_bank; } Mm5WorldTile;
/* Decode a world screen's native 8x8 tile, including live tile replacements.
 * x/y are local 0..255 coordinates. No mapper or machine state is changed. */
int mm5_world_tile(int screen, int x, int y, Mm5WorldTile *out);
int mm5_world_class(int world_x, int y); /* Native collision nibble, or -1. */
void mm5_world_audit(const uint32_t *background);
int mm5_world_audit_file(const char *path);
void mm5_world_register(void);
void mm5_world_power(void);
void mm5_world_shadow_set(int enabled);
void mm5_world_private_begin(void);
void mm5_world_private_end(int commit);
