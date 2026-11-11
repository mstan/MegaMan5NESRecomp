#include "mm5_weapons.h"
#include "mm5_proto.h"
#include "mm5_cart.h"
#include "mm5_world.h"
#include "mm5_wide.h"
#include "mm5_coop.h"
#include "cyc_host_extras.h"
#include "cyc_input.h"
#include "cyc_ring.h"
#include "cyc_tcp.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <string.h>
static FILE *s_log;
static int s_proto_on, s_proto_mode = 1, s_proto_start = 1;
static int s_coop_roster;

static void reset(void) { mm5_coop_set(0,s_coop_roster);mm5_weapons_set(0);s_proto_on=0;mm5_proto_set(0,s_proto_mode,s_proto_start);mm5_wide_set(0,"fit"); }
static void activate_wide(void) {
    char aspect[24]="fit";
    nes_mod_option_value("mega-man-5-usa.enhancement.widescreen","widescreen","aspect",aspect,sizeof aspect);
    mm5_wide_set(1,aspect);
}
static void activate_weapons(void) { mm5_weapons_set(1); }
static void activate_proto(void) {
    const char *package = "mega-man-5-usa.character.protoman";
    char mode[16] = "protoman", start[16] = "protoman";
    nes_mod_option_value(package, "protoman", "mode", mode, sizeof mode);
    nes_mod_option_value(package, "protoman", "start", start, sizeof start);
    s_proto_mode = !strcmp(mode, "protoman"); s_proto_start = !strcmp(start, "protoman");
    s_proto_on = mm5_proto_set(1, s_proto_mode, s_proto_start);
}
NES_MOD_CONSTRUCTOR(register_mm5) {
    mm5_weapons_register();
    mm5_proto_register();
    mm5_wide_register();
    mm5_coop_register();
    nes_mod_register_function_entry_plugin("mm5.player", 0xDEB1, mm5_weapons_dispatch);
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mega-man-5-usa.weapons", activate_weapons);
    nes_mod_register_activation_plugin("mega-man-5-usa.protoman", activate_proto);
    nes_mod_register_activation_plugin("mega-man-5-usa.widescreen", activate_wide);
}
static void power(void *ctx) { (void)ctx; mm5_cart_power();mm5_coop_power();mm5_weapons_power();mm5_proto_power();mm5_wide_power(); }
static void input(void *ctx, uint8_t buttons[2]) { (void)ctx;mm5_coop_input(buttons);mm5_weapons_input(); }
static void end(void *ctx) {
    (void)ctx;mm5_coop_end();mm5_weapons_end();mm5_proto_end();
    if (s_log) {
        char fields[2048]; int n = mm5_weapons_json(fields, sizeof fields); fields[n++] = ',';
        mm5_proto_json(fields + n, sizeof fields - n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_wide_json(fields+n,sizeof fields-n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_coop_json(fields+n,sizeof fields-n);
        fprintf(s_log, "{\"frame\":%u,%s}\n", cyc_ring_frame, fields);
    }
}
static bool option(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if(!strcmp(name,"--coop")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        if(!strcmp(value,"on")){s_proto_on=1;if(!mm5_proto_set(1,s_proto_mode,s_proto_start))return false;}
        mm5_coop_set(!strcmp(value,"on"),s_coop_roster);return true;
    }
    if(!strcmp(name,"--coop-roster")) {
        if(strcmp(value,"megaman") && strcmp(value,"protoman"))return false;
        s_coop_roster=!strcmp(value,"protoman");mm5_coop_set(mm5_coop_enabled(),s_coop_roster);return true;
    }
    if (!strcmp(name, "--world-audit")) return mm5_world_audit_file(value);
    if (!strcmp(name, "--widescreen")) {
        if (strcmp(value,"on") && strcmp(value,"off")) return false;
        mm5_wide_set(!strcmp(value,"on"),"fit"); return true;
    }
    if (!strcmp(name, "--mm5-log")) { if (s_log) fclose(s_log); s_log = fopen(value, "w"); return s_log != NULL; }
    if (!strcmp(name, "--protoman")) {
        if (strcmp(value, "on") && strcmp(value, "off")) return false;
        s_proto_on = !strcmp(value, "on"); return mm5_proto_set(s_proto_on, s_proto_mode, s_proto_start);
    }
    if (!strcmp(name, "--proto-mode") || !strcmp(name, "--proto-start")) {
        if (strcmp(value, "protoman") && strcmp(value, "megaman")) return false;
        if (!strcmp(name, "--proto-mode")) s_proto_mode = !strcmp(value, "protoman");
        else s_proto_start = !strcmp(value, "protoman");
        return mm5_proto_set(s_proto_on, s_proto_mode, s_proto_start);
    }
    if (strcmp(name, "--weapon-cycle") || (strcmp(value, "on") && strcmp(value, "off"))) return false;
    mm5_weapons_set(!strcmp(value, "on")); return true;
}
static void state(int id, const char *line) {
    (void)line; char fields[2048]; int n = mm5_weapons_json(fields, sizeof fields); fields[n++] = ',';
    mm5_proto_json(fields + n, sizeof fields - n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_wide_json(fields+n,sizeof fields-n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_coop_json(fields+n,sizeof fields-n);
    cyc_tcp_ok(id, fields);
}
static void tcp(void *ctx) { (void)ctx; cyc_tcp_register("mm5_state", "MM5 native player and weapon adapter state", state); }
const CycHostExtras *cyc_host_extras(void) {
    static const CycInputProfile profile = {
        .id = "megaman-weapons-1", .game_label = {"Previous weapon (L)", "Next weapon (R)"},
        .game_pad = {"leftshoulder", "rightshoulder"},
        .shortcut_pad = {"none", "leftstick+rightstick", NULL, NULL, NULL, NULL, NULL},
    };
    static const CycHostOption options[] = {
        {"--weapon-cycle", true, "on|off: L/R cycles acquired weapons and helpers"},
        {"--mm5-log", true, "FILE: native player/adapter diagnostics for QA"},
        {"--protoman", true, "on|off: solo Proto Man character and Select swapping"},
        {"--proto-mode", true, "protoman|megaman: actual traits (default) or normal-stat reskin"},
        {"--proto-start", true, "protoman|megaman: solo starting character"},
        {"--world-audit", true, "FILE: native terrain-decoder comparison for QA"},
        {"--widescreen", true, "on|off: adaptive Fit and native widened actor activation"},
        {"--coop", true, "on|off: personal local co-op development controls"},
        {"--coop-roster", true, "megaman|protoman: character assigned to player one"},
    };
    static const CycHostExtras extras = {
        .power_on = power, .input = input, .frame_end = end, .tcp_setup = tcp,
        .option = option, .options = options, .option_count = 9, .input_profile = &profile,
    };
    return &extras;
}
