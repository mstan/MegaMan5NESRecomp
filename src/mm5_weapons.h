#pragma once
#include <stdint.h>
void mm5_weapons_set(int enabled);
int mm5_weapons_enabled(void);
int mm5_weapons_equip_cursor(int cursor);
void mm5_weapons_register(void);
void mm5_weapons_power(void);
void mm5_weapons_input(void);
void mm5_weapons_end(void);
int mm5_weapons_dispatch(uint16_t address);
int mm5_weapons_json(char *out, int capacity);
