#pragma once
#include <stdint.h>
void mm5_menu_register(void);
void mm5_menu_reset(void);
int mm5_menu_capture(void);
int mm5_menu_draw(uint32_t *out,int width,int height);
