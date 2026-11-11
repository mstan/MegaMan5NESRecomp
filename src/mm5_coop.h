#pragma once
#include <stdint.h>
int mm5_coop_enabled(void);
int mm5_coop_seat(void);
int mm5_coop_character(void); /* -1 in solo; otherwise current context's roster */
int mm5_coop_native_scope(void);
int mm5_coop_read(int seat, uint16_t address);
void mm5_coop_register(void);
void mm5_coop_set(int enabled, int roster);
void mm5_coop_power(void);
void mm5_coop_input(uint8_t buttons[2]);
void mm5_coop_end(void);
void mm5_coop_build(void);
void mm5_coop_latch(void);
void mm5_coop_draw(uint32_t *out, int width, int origin, const uint8_t *opaque, uint8_t *claimed);
int mm5_coop_json(char *out, int capacity);
