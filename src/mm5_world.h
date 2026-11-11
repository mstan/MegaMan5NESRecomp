#pragma once
#include <stdint.h>
typedef struct { uint8_t tile, palette, metatile, art_bank; } Mm5WorldTile;
/* Decode a world screen's native 8x8 tile, including live tile replacements.
 * x/y are local 0..255 coordinates. No mapper or machine state is changed. */
int mm5_world_tile(int screen, int x, int y, Mm5WorldTile *out);
void mm5_world_audit(const uint32_t *background);
int mm5_world_audit_file(const char *path);
