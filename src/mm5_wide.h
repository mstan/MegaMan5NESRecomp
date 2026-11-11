#pragma once
#include <stdint.h>
void mm5_wide_register(void);
void mm5_wide_power(void);
void mm5_wide_set(int enabled, const char *aspect);
void mm5_wide_begin(void);
void mm5_wide_latch(void);
int mm5_wide_background(uint32_t *out, int width, int height, const uint32_t *native_bg, uint8_t *opaque, int *origin);
int mm5_wide_rank(int owner);
void mm5_wide_draw(uint32_t *out, int width, const uint8_t *opaque, int rank);
int mm5_wide_json(char *out, int cap);
