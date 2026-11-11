#pragma once
#include <stdint.h>
void mm5_cart_power(void);
int mm5_cart_ready(void);
uint8_t mm5_cart_prg(int bank, int address);
uint8_t mm5_cart_chr(int bank, int address);
