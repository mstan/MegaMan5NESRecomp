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
int mm5_coop_test_poke(const char *value);
int mm5_coop_character_at(int seat);
int mm5_coop_view_mode(void);
int mm5_coop_independent(void);
void mm5_coop_view_set(int mode);
int mm5_coop_wait_both(void);
void mm5_coop_wait_both_set(int enabled);
int mm5_coop_view_rects(int left[2]);
int mm5_coop_scene_locked(void);
int mm5_coop_menu_active(void);
int mm5_coop_menu_cursor(int seat);
int mm5_coop_shared_left(int proposed,int width,int lo,int hi);
