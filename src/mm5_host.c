#include "mm5_weapons.h"
#include "mm5_proto.h"
#include "mm5_cart.h"
#include "mm5_world.h"
#include "mm5_wide.h"
#include "mm5_coop.h"
#include "mm5_net.h"
#include "mm5_pool.h"
#include "cyc_host_extras.h"
#include "cyc_core.h"
#include "cyc_input.h"
#include "cyc_ring.h"
#include "cyc_tcp.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"
#endif
#include <stdio.h>
#include <string.h>
static FILE *s_log;
static int s_proto_on, s_proto_mode = 1, s_proto_start = 1;
static int s_coop_roster;
static unsigned s_extra_lines;
static void timing(unsigned lines) {
    s_extra_lines=lines;
    if(!lines || cyc_extra_scanlines_supported())cyc_set_extra_scanlines(lines);
    if(!mm5_net_locked())nes_mod_set_local_only("MM5 CPU budget (unqualified netplay)",lines!=0);
}
static void activate_slowdown(void) {if(!mm5_net_locked())timing(262);}
static void activate_noflicker(void) {if(!mm5_net_locked())mm5_no_flicker_set(1);}
static void loaded(void *ctx) {(void)ctx;timing(s_extra_lines);}
Mm5Settings mm5_host_settings(void) {
    Mm5Settings c={mm5_coop_enabled(),mm5_coop_independent(),s_coop_roster,s_proto_on,s_proto_mode,mm5_weapons_enabled(),mm5_wide_enabled(),mm5_no_flicker_enabled(),(int)s_extra_lines,mm5_coop_wait_both()};return c;
}
int mm5_host_apply_settings(Mm5Settings c,int local_seat) {
    s_proto_on=c.proto;s_proto_mode=c.mode;s_proto_start=1;s_coop_roster=c.roster;
    if(!mm5_proto_set(c.proto,c.mode,1))return 0;
    mm5_weapons_set(c.weapons);mm5_wide_set(c.wide,"fit");mm5_no_flicker_set(c.flicker);timing((unsigned)c.lines);
    int view=mm5_coop_view_mode();mm5_coop_set(c.coop,c.roster);mm5_coop_wait_both_set(c.wait_both);
    mm5_coop_view_set(c.independent?(view?view:(local_seat==1?3:2)):0);return 1;
}

static void reset(void) {if(mm5_net_locked())return;mm5_coop_set(0,s_coop_roster);mm5_coop_wait_both_set(0);mm5_weapons_set(0);s_proto_on=0;mm5_proto_set(0,s_proto_mode,s_proto_start);mm5_wide_set(0,"fit");mm5_no_flicker_set(0);timing(0); }
static void activate_wide(void) {
    if(mm5_net_locked())return;
    char aspect[24]="fit";
    nes_mod_option_value("mega-man-5-usa.enhancement.widescreen","widescreen","aspect",aspect,sizeof aspect);
    mm5_wide_set(1,aspect);
}
static void activate_weapons(void) {if(!mm5_net_locked())mm5_weapons_set(1);}
static void activate_proto(void) {
    if(mm5_net_locked())return;
    const char *package = "mega-man-5-usa.character.protoman";
    char mode[16] = "protoman", start[16] = "protoman";
    nes_mod_option_value(package, "protoman", "mode", mode, sizeof mode);
    nes_mod_option_value(package, "protoman", "start", start, sizeof start);
    s_proto_mode = !strcmp(mode, "protoman"); s_proto_start = !strcmp(start, "protoman");
    s_proto_on = mm5_proto_set(1, s_proto_mode, s_proto_start);
}
static void activate_coop(void) {
    if(mm5_net_locked())return;
    const char *package="mega-man-5-usa.coop";char roster[16]="megaman",view[16]="shared",wait[16]="off";
    nes_mod_option_value(package,"coop","player-one",roster,sizeof roster);
    nes_mod_option_value(package,"coop","coop-view",view,sizeof view);
    nes_mod_option_value(package,"coop","wait-for-both",wait,sizeof wait);
    activate_proto();s_coop_roster=!strcmp(roster,"protoman");mm5_coop_set(1,s_coop_roster);
    mm5_coop_view_set(!strcmp(view,"split")?1:!strcmp(view,"p1")?2:!strcmp(view,"p2")?3:0);
    mm5_coop_wait_both_set(!strcmp(wait,"on"));
}
NES_MOD_CONSTRUCTOR(register_mm5) {
    mm5_weapons_register();
    mm5_world_register();
    mm5_proto_register();
    mm5_wide_register();
    mm5_coop_register();
    mm5_pool_register();
    nes_mod_register_function_entry_plugin("mm5.player", 0xDEB1, mm5_weapons_dispatch);
    nes_mod_register_reset_callback(reset);
    nes_mod_register_activation_plugin("mega-man-5-usa.weapons", activate_weapons);
    nes_mod_register_activation_plugin("mega-man-5-usa.protoman", activate_proto);
    nes_mod_register_activation_plugin("mega-man-5-usa.widescreen", activate_wide);
    nes_mod_register_activation_plugin("mega-man-5-usa.slowdown", activate_slowdown);
    nes_mod_register_activation_plugin("mega-man-5-usa.noflicker", activate_noflicker);
    nes_mod_register_activation_plugin("mega-man-5-usa.coop",activate_coop);
}
static void power(void *ctx) { (void)ctx; mm5_cart_power();mm5_world_power();mm5_coop_power();mm5_weapons_power();mm5_proto_power();mm5_wide_power();timing(s_extra_lines); }
static void input(void *ctx, uint8_t buttons[2]) { (void)ctx;mm5_coop_input(buttons);mm5_weapons_input(); }
static void end(void *ctx) {
    (void)ctx;mm5_coop_end();mm5_weapons_end();mm5_proto_end();
    if (s_log) {
        char fields[4096]; int n = mm5_weapons_json(fields, sizeof fields); fields[n++] = ',';
        mm5_proto_json(fields + n, sizeof fields - n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_wide_json(fields+n,sizeof fields-n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_coop_json(fields+n,sizeof fields-n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_view_json(fields+n,sizeof fields-n);
        n = (int)strlen(fields); fields[n++] = ','; mm5_pool_json(fields+n,sizeof fields-n);
        n = (int)strlen(fields);snprintf(fields+n,sizeof fields-n,",\"timing\":{\"extra_lines\":%u}",cyc_extra_scanlines());
        fprintf(s_log, "{\"frame\":%u,%s}\n", cyc_ring_frame, fields);
    }
}
static bool option(void *ctx, const char *name, const char *value) {
    (void)ctx;
    mm5_net_note_option(name);
    if(mm5_net_locked() && strcmp(name,"--coop-view"))return false;
    if(!strcmp(name,"--coop")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        if(!strcmp(value,"on")){s_proto_on=1;if(!mm5_proto_set(1,s_proto_mode,s_proto_start))return false;}
        mm5_coop_set(!strcmp(value,"on"),s_coop_roster);return true;
    }
    if(!strcmp(name,"--coop-roster")) {
        if(strcmp(value,"megaman") && strcmp(value,"protoman"))return false;
        s_coop_roster=!strcmp(value,"protoman");mm5_coop_set(mm5_coop_enabled(),s_coop_roster);return true;
    }
    if(!strcmp(name,"--coop-test-poke"))return mm5_coop_test_poke(value);
    if(!strcmp(name,"--coop-wait-both")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        mm5_coop_wait_both_set(!strcmp(value,"on"));return true;
    }
    if(!strcmp(name,"--coop-view")) {
        int mode=!strcmp(value,"shared")?0:!strcmp(value,"split")?1:!strcmp(value,"p1")?2:!strcmp(value,"p2")?3:-1;
        if(mode<0)return false;mm5_coop_view_set(mode);return true;
    }
    if(!strcmp(name,"--slowdown-reduction")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        timing(!strcmp(value,"on")?262:0);return true;
    }
    if(!strcmp(name,"--no-flicker")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        mm5_no_flicker_set(!strcmp(value,"on"));return true;
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
    char view[16];
    if(cyc_tcp_str(line,"view",view,sizeof view)) {
        int mode=!strcmp(view,"shared")?0:!strcmp(view,"split")?1:!strcmp(view,"p1")?2:!strcmp(view,"p2")?3:-1;
        if(mode<0 || !mm5_coop_enabled()){cyc_tcp_err(id,"view needs active co-op and shared|split|p1|p2");return;}
        mm5_coop_view_set(mode);
        if(mm5_coop_view_mode()!=mode){cyc_tcp_err(id,"the active network simulation locks this view policy");return;}
    }
    char fields[4096]; int n = mm5_weapons_json(fields, sizeof fields); fields[n++] = ',';
    mm5_proto_json(fields + n, sizeof fields - n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_wide_json(fields+n,sizeof fields-n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_coop_json(fields+n,sizeof fields-n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_view_json(fields+n,sizeof fields-n);
    n = (int)strlen(fields); fields[n++] = ','; mm5_pool_json(fields+n,sizeof fields-n);
    n = (int)strlen(fields);snprintf(fields+n,sizeof fields-n,",\"timing\":{\"extra_lines\":%u}",cyc_extra_scanlines());
    cyc_tcp_ok(id, fields);
}
static void tcp(void *ctx) { (void)ctx; cyc_tcp_register("mm5_state", "MM5 state; optional view shared|split|p1|p2 uses the native view adapter", state); }
#ifdef CYC_WITH_RECOMP_UI
static int menu_get(void *ctx,const RecompRuntimeUiItem *item,int *out) {
    (void)ctx;
    if(!strcmp(item->key,"mm5.coop_wait_both")){*out=mm5_coop_wait_both();return 1;}
    if(strcmp(item->key,"mm5.coop_view"))return 0;
    *out=mm5_coop_view_mode();return 1;
}
static int menu_set(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;
    if(!strcmp(item->key,"mm5.coop_wait_both")) {
        if(!mm5_coop_enabled() || mm5_net_locked() || value<0 || value>1)return 0;
        mm5_coop_wait_both_set(value);return mm5_coop_wait_both()==value;
    }
    if(strcmp(item->key,"mm5.coop_view") || !mm5_coop_enabled() || value<0 || value>3)return 0;
    if(mm5_net_locked() && (value!=0)!=mm5_coop_independent())return 0;
    mm5_coop_view_set(value);return mm5_coop_view_mode()==value;
}
static int menu_enabled(void *ctx,const RecompRuntimeUiItem *item) {
    (void)ctx;
    return (!strcmp(item->key,"mm5.coop_view") || !strcmp(item->key,"mm5.coop_wait_both")) && mm5_coop_enabled();
}
#ifdef CYC_HOST_EXTRAS_HAS_NETPLAY_VALUE_POLICY
static bool menu_netplay_value_allowed(void *ctx,const RecompRuntimeUiItem *item,int value) {
    (void)ctx;
    return !strcmp(item->key,"mm5.coop_view") && mm5_net_locked() &&
        mm5_coop_independent() && value>=1 && value<=3;
}
#endif
static const char *const view_choices[]={"Shared screen","Split screen","Player one","Player two"};
static const RecompRuntimeUiItem menu_items[]={
    {.key="mm5.coop_view",.section="Display",.label="Two-player view",
     .description="Choose a view. Online matches keep their camera mode.",
     .type=RECOMP_RUNTIME_UI_CHOICE,.minimum=0,.maximum=3,.step=1,.choices=view_choices,.choice_count=4},
    {.key="mm5.coop_wait_both",.section="Game",.label="Wait for both players",
     .description="Wait at room exits. A lone survivor can continue.",
     .type=RECOMP_RUNTIME_UI_BOOL,.minimum=0,.maximum=1,.step=1}
};
static const RecompRuntimeUiCallbacks menu_callbacks={.get_value=menu_get,.set_value=menu_set,.is_enabled=menu_enabled};
#endif
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
        {"--coop-test-poke", true, "FRAME:SEAT:ADDR:BYTE: scheduled personal RAM fixture (seat0 or1)"},
        {"--slowdown-reduction", true, "on|off: extra game CPU time; off restores stock timing"},
        {"--no-flicker", true, "on|off: retain complete actors beyond scanline and OAM limits"},
        {"--coop-view", true, "shared|split|p1|p2: co-op display and independent native cameras"},
        {"--coop-wait-both", true, "on|off: wait for both players at room exits (default off)"},
    };
    static const CycHostExtras extras = {
        .power_on = power, .state_loaded = loaded, .input = input, .frame_end = end, .tcp_setup = tcp,
        .option = option, .options = options, .option_count = sizeof options/sizeof options[0], .input_profile = &profile,
#ifdef CYC_WITH_RECOMP_UI
        .menu_items=menu_items,.menu_item_count=sizeof menu_items/sizeof menu_items[0],.menu_callbacks=&menu_callbacks,
#ifdef CYC_HOST_EXTRAS_HAS_NETPLAY_VALUE_POLICY
        .menu_netplay_value_allowed=menu_netplay_value_allowed,
#endif
#endif
    };
    return &extras;
}
