#pragma once
#include <stdint.h>
typedef struct {int coop,independent,roster,proto,mode,weapons,wide,flicker,lines,wait_both;} Mm5Settings;
int mm5_net_locked(void);
int mm5_net_simulation_width(int local_width);
void mm5_net_note_option(const char *name);
Mm5Settings mm5_host_settings(void);
int mm5_host_apply_settings(Mm5Settings settings,int local_seat);
int mm5_proto_source_hash(uint8_t out[32]);
