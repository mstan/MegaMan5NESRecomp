#pragma once
#include <stdint.h>
void mm5_hud_register(void);
void mm5_hud_reset(void);
int mm5_hud_capture(void);
void mm5_hud_latch(void);
int mm5_hud_native_meter(const uint8_t *oam);
void mm5_hud_draw(uint32_t *out,int width,int height,int own);
