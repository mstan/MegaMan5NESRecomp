#include "mm5_cart.h"
#include "cyc_mod.h"
#include <stdlib.h>
static uint8_t *s_payload;
void mm5_cart_power(void) { free(s_payload); s_payload = NULL; }
int mm5_cart_ready(void) {
    if (s_payload) return 1;
    size_t size = cyc_mod_copy_cart_payload(NULL, 0);
    if (size != 524288) return 0;
    uint8_t *bytes = (uint8_t *)malloc(size);
    if (!bytes) return 0;
    if (cyc_mod_copy_cart_payload(bytes, size) != size) { free(bytes); return 0; }
    s_payload = bytes; return 1;
}
uint8_t mm5_cart_prg(int bank, int address) {
    if (!mm5_cart_ready() || bank < 0 || bank >= 32) return 0;
    return s_payload[(size_t)bank * 8192 + (address & 8191)];
}
uint8_t mm5_cart_chr(int bank, int address) {
    if (!mm5_cart_ready() || bank < 0 || bank >= 256) return 0;
    return s_payload[262144 + (size_t)bank * 1024 + (address & 1023)];
}
