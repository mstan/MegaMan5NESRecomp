/* Personal native execution contexts; the real world engine runs once. */
#include "mm5_coop.h"
#include "mm5_cart.h"
#include "mm5_proto.h"
#include "mm5_weapons.h"
#include "mm5_world.h"
#include "mm5_hud.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=384, PART_CAP=96, TELE_NONE=0, TELE_OUT, TELE_HIDDEN, TELE_IN };
typedef struct {
    uint8_t ram[PERSONAL_CAP],previous,dead,teleport,arrival_floor;
    uint8_t arrival_follow,entry_state,entry_subtype,entry_ladder,entry_phase,entry_flags;
} Player;
typedef struct { int16_t x,y;uint8_t owner,attr,colors[4],pixels[64]; } Part;
typedef struct { uint8_t valid,seat;uint16_t count,camera;Mm5ProtoBody body;Part parts[PART_CAP]; } Picture;
typedef struct {
    char magic[4];uint8_t enabled,initialized,stage,anchor,current,menu,pauser,pending_pause,roster;
    uint8_t raw[2],raw_previous[2],fresh[2];
    uint8_t ai_active,ai_actor,ai_common,ai_pad[2],wiping,retry;
    uint8_t withdrawn,select_hold[2],select_armed[2],checkpoint,revive_pending;
    uint8_t scene_active,follow,section,scene_page,scene_vertical,scene_scroll_y;
    uint32_t last_step[2];
    Player player[2];Picture build,frame;
    uint64_t updates[2],world_updates,failures,hits[2],contacts[2],deaths[2],handoffs,wipes,targets[2],checks;
    uint64_t withdrawals[2],rejoins[2],revivals[2];
    uint64_t transitions,arrivals[2];
} Coop;
static Coop C;
static int s_on,s_roster,s_scope,s_personal_bhv;
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
static int s_count;
static int checkpoint_at(int screen);
static void scene_tick(void);
static void follow_arrivals(void);
static void scene_shift(void);
static void reset_select(void) {
    memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
}
static void sound(int id) {
    int q=R(0xDA)&7;if(R(0xDC+q)==0x88){P(0xDC+q,id);P(0xDA,(q+1)&7);}
}
typedef struct {uint32_t frame;uint16_t address;uint8_t seat,value;} TestPoke;
static TestPoke s_test_pokes[64];
static int s_test_poke_count;
typedef struct {const char *id;int enabled;} HookSetting;
static HookSetting s_hooks[128];
static int s_hook_count;

int mm5_coop_enabled(void) { return s_on; }
int mm5_coop_seat(void) { return s_on?C.current:0; }
int mm5_coop_character(void) { return s_on?(s_roster^C.current):-1; }
int mm5_coop_character_at(int seat) {return s_on && seat>=0 && seat<2?(s_roster^seat):0;}
int mm5_coop_native_scope(void) { return s_on && s_scope; }
static void addresses(void) {
    if(s_count)return;
    for(int base=0x300;base<=0x5B8;base+=0x18)for(int slot=0;slot<8;slot++)s_addr[s_count++]=(uint16_t)(base+slot);
    for(int a=0x30;a<=0x3F;a++)s_addr[s_count++]=(uint16_t)a;
    static const uint8_t zp[]={0x2E,0x40,0x41,0x42,0x46,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0x53,0x5B,0xED};
    for(int i=0;i<sizeof zp;i++)s_addr[s_count++]=zp[i];
    for(int a=0xB0;a<=0xBC;a++)s_addr[s_count++]=(uint16_t)a;
    for(int i=0;i<8;i++){s_addr[s_count++]=(uint16_t)(0x610+i);s_addr[s_count++]=(uint16_t)(0x630+i);}
    for(int i=0;i<s_count;i++)s_index[s_addr[i]]=(uint16_t)(i+1);
}
static void capture(Player *p) { for(int i=0;i<s_count;i++)p->ram[i]=R(s_addr[i]); }
static void project(const Player *p) { for(int i=0;i<s_count;i++)P(s_addr[i],p->ram[i]); }
static int personal(const Player *p,int a) { int i=s_index[a];return i?p->ram[i-1]:R(a); }
static void set_personal(Player *p,int a,int value) { int i=s_index[a];if(i)p->ram[i-1]=(uint8_t)value; }
int mm5_coop_read(int seat,uint16_t a) {
    if(a>=2048 || seat<0 || seat>1)return 0;
    return C.initialized && seat!=C.current?personal(&C.player[seat],a):R(a);
}
int mm5_coop_test_poke(const char *value) {
    unsigned frame,seat,address,byte;int n=0;addresses();
    if(!value || s_test_poke_count>=64 || sscanf(value,"%u:%u:%x:%x%n",&frame,&seat,&address,&byte,&n)!=4 || value[n] ||
        seat>1 || address>=2048 || !s_index[address] || byte>255)return 0;
    s_test_pokes[s_test_poke_count++]=(TestPoke){frame,(uint16_t)address,(uint8_t)seat,(uint8_t)byte};
    nes_mod_set_local_only("MM5 personal RAM QA fixture",1);return 1;
}
static int world_field(int a) {
    if(a>=0x300 && a<0x5D0)return (a-0x300)%24>=8;
    return (a>=0x100 && a<0x110) || (a>=0x600 && a<0x610) || (a>=0x620 && a<0x630) ||
        (a>=0x680 && a<0x700) || (a>=0x780 && a<0x800) || a==0x43 || a==0x19 || a==0x1A || a==0x5A;
}
static void snapshot_ram(uint8_t out[2048]) { for(int a=0;a<2048;a++)out[a]=R(a); }
static void merge_world(const uint8_t before[2048],const uint8_t after[2048]) {
    int palette=0;
    for(int a=0;a<2048;a++)if(before[a]!=after[a] && world_field(a)) {
        P(a,after[a]);if((a>=0x600 && a<0x610)||(a>=0x620 && a<0x630))palette=1;
    }
    if(!before[0x54] && after[0x54])P(0x54,after[0x54]);
    if(palette)P(0x18,255);
    /* Merge new requests in their native producer order, without replaying
     * existing requests or committing the private CPU stack/bank shadows. */
    for(int i=0;i<8;i++) {
        int q=(before[0xDA]+i)&7;
        if(before[0xDC+q]==0x88 && after[0xDC+q]!=0x88) {
            int dst=R(0xDA)&7;
            if(R(0xDC+dst)==0x88){P(0xDC+dst,after[0xDC+q]);P(0xDA,(dst+1)&7);}
        }
    }
}
static int hooks(int enable) {
    if(enable) {
        s_hook_count=nes_mod_function_hook_count();if(s_hook_count>128)return 0;
        for(int i=0;i<s_hook_count;i++) {
            HookSetting *h=&s_hooks[i];nes_mod_function_hook_at(i,&h->id,NULL,&h->enabled,NULL);
            int allowed=!strncmp(h->id,"mm5.proto.",10) && strcmp(h->id,"mm5.proto.controller") && strcmp(h->id,"mm5.proto.shield");
            allowed|=!strcmp(h->id,"mm5.coop.personal-end");
            if(h->enabled && !allowed)nes_mod_set_function_hook_enabled(h->id,0);
        }
        cyc_mod_allow_isolated_hooks(true);
    } else {
        cyc_mod_allow_isolated_hooks(false);
        for(int i=0;i<s_hook_count;i++)nes_mod_set_function_hook_enabled(s_hooks[i].id,s_hooks[i].enabled);
    }
    return 1;
}
static int center_camera(void) {
    if(!(R(0x29)&32)){int camera=R(0x348)*256;P(0xFC,0);P(0xF9,R(0x348));return camera;}
    int start=R(0xF9)-R(0x2B),camera=((R(0x348)<<8)|R(0x330))-128;
    int left=start*256,right=(start+R(0x2A))*256;
    if(start>=0 && (R(0x29)&32)) {if(camera<left)camera=left;if(camera>right)camera=right;}
    if(camera<0)camera=0;
    if(start>=0 && (R(0x29)&32))P(0x2B,(camera>>8)-start);
    P(0xFC,camera);P(0xF9,camera>>8);return camera;
}
static int private_step(int seat) {
    Player *p=&C.player[seat];int state=personal(p,0x30);
    if(p->teleport==TELE_OUT && personal(p,0x390)) {
        p->teleport=TELE_HIDDEN;set_personal(p,0x528,0);return 1;
    }
    if(p->teleport==TELE_HIDDEN || (state>8 && p->teleport!=TELE_OUT) || state==7 || p->dead)return 1;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);center_camera();
    P(0x14,C.raw[seat]&~C.player[seat].previous&~0x30);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);r.a=0x1B;
    if(ok)ok=cyc_mod_call(0xFF3D,&r);
    mm5_weapons_dispatch(0xDEB1);
    if(ok && p->teleport==TELE_IN && R(0x378)<p->arrival_floor) {
        /* Native acceleration and subpixel movement carry the returning beam
         * through overhead terrain. Open at the checked destination floor. */
        r.x=0;ok=cyc_mod_call(0xEA03,&r);if(ok){r.x=0;ok=cyc_mod_call(0xE92A,&r);}
        int y=R(0x378);if(R(0x390) || y>p->arrival_floor)y=p->arrival_floor;
        P(0x378,y);P(0x390,0);P(0x3E,y);P(0x3F,0);P(0x540,0);P(0x570,0);
    } else if(ok && p->teleport==TELE_IN && p->arrival_follow) {
        /* A room may enter in midair or on a ladder. Open the native frameset
         * without falling to a different floor, then restore that entry pose. */
        if(!R(0x540) && R(0x570)==1)sound(0x2D);
        if(R(0x540)>=4) {
            P(0x30,p->entry_state);P(0x558,p->entry_subtype);P(0x35,p->entry_ladder);
            P(0x528,p->entry_flags);P(0x540,p->entry_phase);P(0x570,0);P(0x360,0);P(0x3D8,0);P(0x3F0,0);
        }
    } else if(ok && p->teleport==TELE_IN && !R(0x54)) {
        /* Original stage entry ignores floors above Y112. A checked rejoin
         * can be higher: enter its native landing/opening tail directly. */
        r.x=0;ok=cyc_mod_call(0x859B,&r);
    } else if(ok)ok=cyc_mod_call(0x8000,&r);
    if(ok) {r.a=0x1C;ok=cyc_mod_call(0xFF3D,&r);P(0xF6,C.stage);if(ok)ok=cyc_mod_call(0xFF43,&r);}
    s_personal_bhv=1;if(ok)ok=cyc_mod_call(0x8000,&r);s_personal_bhv=0;
    if(ok) {
        if(p->teleport==TELE_IN && R(0x30)!=8) {
            p->teleport=TELE_NONE;P(0x5B8,60);
            if(C.withdrawn&(1u<<seat)){C.withdrawn&=(uint8_t)~(1u<<seat);C.rejoins[seat]++;}
            if(p->arrival_follow){p->arrival_follow=0;C.arrivals[seat]++;}
            reset_select();
        }
        capture(p);snapshot_ram(after);C.updates[seat]++;C.last_step[seat]=cyc_ring_frame;
    }
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    if(ok)merge_world(before,after);return ok;
}
static void initialize(void) {
    C.anchor=C.current=0;C.stage=R(0x26);C.initialized=1;C.menu=0;C.pending_pause=255;
    Player saved[2];memcpy(saved,C.player,sizeof saved);
    capture(&C.player[0]);C.player[1]=C.player[0];
    for(int seat=0;seat<2;seat++) {
        C.player[seat].dead=0;C.player[seat].teleport=TELE_NONE;C.player[seat].arrival_follow=0;
        if(C.retry)for(int a=0xB1;a<=0xBC;a++)set_personal(&C.player[seat],a,personal(&saved[seat],a));
    }
    C.retry=C.wiping=0;
    C.withdrawn=C.revive_pending=0;C.checkpoint=(uint8_t)checkpoint_at(R(0x69));
    C.scene_active=C.follow=C.scene_vertical=C.scene_scroll_y=0;C.section=R(0x29)&31;C.scene_page=R(0xF9);
    reset_select();
    int x=(R(0x348)<<8)|R(0x330);x=x>=32?x-16:x+16;
    set_personal(&C.player[1],0x330,x);set_personal(&C.player[1],0x348,x>>8);
    for(int seat=0;seat<2;seat++)C.player[seat].previous=C.raw[seat];
    project(&C.player[C.anchor]);
}
static void deaths(void) {
    /* Native pit death (renderer E02B) sets state7 without clearing HP. */
    for(int seat=0;seat<2;seat++)if(!C.player[seat].dead &&
        (!(personal(&C.player[seat],0xB0)&31) || personal(&C.player[seat],0x30)==7)) {
        C.player[seat].dead=1;C.deaths[seat]++;
        set_personal(&C.player[seat],0xB0,0x80);
        for(int base=0x300;base<=0x5B8;base+=24)for(int slot=1;slot<8;slot++)set_personal(&C.player[seat],base+slot,0);
        if(seat==C.current)project(&C.player[seat]);
    }
    if((C.player[0].dead || (C.withdrawn&1)) && (C.player[1].dead || (C.withdrawn&2)) && !C.wiping){C.wiping=1;C.wipes++;}
}
static int controller(uint16_t address) {
    (void)address;if(!s_on || cyc_mod_isolated())return 0;
    if(C.menu && R(0x26)<16 && !R(0x59)) {
        capture(&C.player[C.pauser]);C.current=C.anchor;project(&C.player[C.anchor]);C.menu=0;P(0x18,255);
    }
    if(C.initialized && R(0x26)<16 && R(0x26)!=C.stage)C.initialized=0;
    if(!C.initialized) {
        if(R(0x26)>=16 || (R(0x30)!=0 && (R(0x30)!=8 || R(0x558)!=0x13)) || !(R(0xB0)&31) || R(0x1B))return 0;
        initialize();
    }
    if(C.menu || R(0x26)>=16)return 0;
    C.current=C.anchor;capture(&C.player[C.anchor]);deaths();
    if(C.player[C.anchor].dead && !C.player[C.anchor^1].dead && !(C.withdrawn&(1u<<(C.anchor^1)))) {
        C.anchor^=1;C.current=C.anchor;project(&C.player[C.anchor]);C.handoffs++;
        mm5_weapons_dispatch(0xDEB1);P(0x18,255);
    }
    if(R(0x30)<=8 && !private_step(C.anchor^1))C.failures++;
    C.current=C.anchor;P(0x14,C.raw[C.anchor]&~C.player[C.anchor].previous&~0x30);P(0x16,C.raw[C.anchor]&~0x30);
    P(0x15,0);P(0x17,0);return 0;
}
static int after_controller(uint16_t address) {
    (void)address;if(s_on && C.initialized && !cyc_mod_isolated() && !C.menu) {
        capture(&C.player[C.anchor]);C.updates[C.anchor]++;C.last_step[C.anchor]=cyc_ring_frame;
    }return 0;
}
/* Native AI runs once, borrowing the nearest living player's complete personal
 * context. Its original common collision pass handles that target; the same
 * cartridge routines then check the other player's private shots and hitbox. */
static int ai_begin(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || cyc_mod_isolated() || C.menu)return 0;
    CycModRegs r;cyc_mod_regs(&r);int actor=r.x;
    if(actor<8 || actor>=24 || !R(0x300+actor))return 0;
    capture(&C.player[C.current]);
    int seat=C.anchor,ex=(R(0x348+actor)<<8)|R(0x330+actor),ey=R(0x378+actor);int64_t best=INT64_MAX;
    for(int i=0;i<2;i++)if(!C.player[i].dead && C.player[i].teleport==TELE_NONE && !(C.withdrawn&(1u<<i)) && (personal(&C.player[i],0xB0)&31)) {
        int dx=((personal(&C.player[i],0x348)<<8)|personal(&C.player[i],0x330))-ex;
        int dy=personal(&C.player[i],0x378)-ey;int64_t distance=(int64_t)dx*dx+(int64_t)dy*dy;
        if(distance<best){best=distance;seat=i;}
    }
    C.ai_active=1;C.ai_actor=(uint8_t)actor;C.ai_common=0;C.ai_pad[0]=R(0x14);C.ai_pad[1]=R(0x16);
    C.current=(uint8_t)seat;project(&C.player[seat]);
    C.targets[seat]++;
    P(0x14,C.fresh[seat]&~0x30);P(0x16,C.raw[seat]&~0x30);return 0;
}
static int ai_common(uint16_t address) {
    (void)address;if(s_on && C.ai_active && !cyc_mod_isolated())C.ai_common=1;return 0;
}
static int other_collision(int seat,int actor) {
    if(C.player[seat].dead || C.player[seat].teleport!=TELE_NONE || (C.withdrawn&(1u<<seat)) || !(personal(&C.player[seat],0xB0)&31) || !R(0x300+actor))return 1;
    C.checks++;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);
    /* Reflection allocates a native Buster in its own isolated scope. */
    int blocked=mm5_proto_try_shield(actor);capture(&C.player[seat]);C.current=(uint8_t)owner;project(&C.player[owner]);
    if(blocked)return 1;
    snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return 0;
    C.current=(uint8_t)seat;project(&C.player[seat]);P(0x15,0);P(0x17,0);
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);r.x=(uint8_t)actor;
    int kick=R(0x30)<6 && R(0x558)==0xB0;
    if(ok)ok=cyc_mod_call(kick?0xEF87:0xEFF8,&r);
    if(ok && !(r.p&1)) {
        P(0, R(0x408+actor)&64);r.x=(uint8_t)actor;
        ok=cyc_mod_call(0x809D,&r);if(ok)C.hits[seat]++;
    }
    if(ok && R(0x300+actor) && (R(0x408+actor)&128) && !R(0x5B8) && R(0x30)<6) {
        r.x=(uint8_t)actor;ok=cyc_mod_call(0xEF87,&r);
        if(ok && !(r.p&1)) {
            int hp=R(0xB0)&31;r.x=(uint8_t)actor;ok=cyc_mod_call(0x82B8,&r);
            if(ok && (R(0xB0)&31)<hp)C.contacts[seat]++;
        }
    }
    if(ok){capture(&C.player[seat]);snapshot_ram(after);}
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    if(ok)merge_world(before,after);return ok;
}
static int personal_end(uint16_t address) {
    (void)address;
    /* Finish the native loop at its common terrain-hazard check, after slot7.
     * Returning from here would incorrectly omit the player's spike checks. */
    if(s_on && s_scope && s_personal_bhv && R(0xA6)>=7)P(0xA6,23);
    if(s_on && C.ai_active && !cyc_mod_isolated()) {
        capture(&C.player[C.current]);
        if(C.ai_common && !other_collision(C.current^1,C.ai_actor))C.failures++;
        if(personal(&C.player[C.current],0x30)>=9 && !C.player[C.current].dead)C.anchor=C.current;
        C.ai_active=C.ai_common=0;C.current=C.anchor;project(&C.player[C.anchor]);
        P(0x14,C.ai_pad[0]);P(0x16,C.ai_pad[1]);deaths();
    }
    return 0;
}
static int world_begin(uint16_t address) { (void)address;if(s_on && C.initialized && !cyc_mod_isolated())C.world_updates++;return 0; }
static int stage_load(uint16_t address) {
    (void)address;if(!s_on || cyc_mod_isolated())return 0;
    if(C.initialized) {
        capture(&C.player[C.current]);C.retry=(uint8_t)(C.wiping && C.stage==R(0x26));
        C.initialized=C.ai_active=C.menu=C.scene_active=C.follow=0;C.current=C.anchor=0;C.pending_pause=255;
        memset(&C.build,0,sizeof C.build);memset(&C.frame,0,sizeof C.frame);
        mm5_hud_reset();
    }
    return 0;
}
static int pause_entry(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || C.pending_pause>1 || cyc_mod_isolated())return 0;
    int seat=C.pending_pause;C.pending_pause=255;
    if(R(0x26)>=16 || R(0x54) || C.scene_active || C.follow || C.player[seat].dead || (C.withdrawn&(1u<<seat)) || C.player[seat].teleport!=TELE_NONE || personal(&C.player[seat],0x30)>=4 || !(personal(&C.player[seat],0xB0)&31)) {
        P(0x14,R(0x14)&~16);return 0;
    }
    capture(&C.player[C.anchor]);C.current=C.pauser=(uint8_t)seat;project(&C.player[seat]);
    P(0x14,C.fresh[seat]);P(0x16,C.raw[seat]);C.menu=1;return 0;
}
void mm5_coop_input(uint8_t buttons[2]) {
    if(!s_on)return;
    for(int seat=0;seat<2;seat++){C.raw[seat]=buttons[seat];C.fresh[seat]=(uint8_t)(buttons[seat]&~C.raw_previous[seat]);C.raw_previous[seat]=buttons[seat];}
    if(C.initialized)for(int i=0;i<s_test_poke_count;i++)if(s_test_pokes[i].frame==cyc_ring_frame) {
        const TestPoke *p=&s_test_pokes[i];set_personal(&C.player[p->seat],p->address,p->value);
        if(p->seat==C.current)P(p->address,p->value);
    }
    if(C.menu)buttons[0]=C.raw[C.pauser];
    else {
        C.pending_pause=255;
        buttons[0]=C.raw[C.ai_active?C.current:C.anchor];
        if(C.scene_active || C.follow)buttons[0]&=(uint8_t)~0x30;
        else for(int seat=0;seat<2;seat++)if(C.fresh[seat]&16) {
            Player *p=&C.player[seat];
            if(C.initialized && (p->dead || (C.withdrawn&(1u<<seat)) || p->teleport!=TELE_NONE || personal(p,0x30)>=4 || !(personal(p,0xB0)&31)))continue;
            C.pending_pause=(uint8_t)seat;buttons[0]|=16;break;
        }
    }
    buttons[1]=0; /* Original pad-2 Select/B are native debug controls. */
}
static int checkpoint_at(int screen) {
    int stage=R(0x6C),result=0;
    if(stage>=16)return 0;
    for(int i=0;i<2;i++)if(screen>=mm5_cart_prg(0x17,0x9169+stage*6+i*3))result=i+1;
    return result;
}
static int living_visible(int seat) {
    Player *p=&C.player[seat];int camera=(R(0xF9)<<8)|R(0xFC);
    int x=(personal(p,0x348)<<8)|personal(p,0x330),y=personal(p,0x378);
    return !p->dead && p->teleport==TELE_NONE && !(C.withdrawn&(1u<<seat)) && (personal(p,0xB0)&31) &&
        !personal(p,0x390) && y>=16 && y<=222 && x>=camera+8 && x<=camera+247;
}
static int safe_position(int *x,int *y) {
    if(R(0x30)!=0 || R(0xAF) || R(0x390) || R(0xFA))return 0;
    static const int offset[]={24,-24,0,8,-8,16,-16,32,-32};
    int base=(R(0x348)<<8)|R(0x330),yy=R(0x378),start=R(0xF9)-R(0x2B);
    if(start<0 || yy<16 || yy>222)return 0;
    int left,right;
    if(R(0x29)&32){left=start*256+8;right=(start+R(0x2A)+1)*256-9;}
    else {left=R(0x348)*256+8;right=(R(0x348)+1)*256-9;}
    for(int i=0;i<sizeof offset/sizeof *offset;i++) {
        int xx=base+offset[i];if(xx<left || xx>right)continue;
        if(mm5_world_class(xx-7,yy-12)==0 && mm5_world_class(xx+7,yy-12)==0 && mm5_world_class(xx,yy)==0 &&
            mm5_world_class(xx-6,yy+12)==0x10 && mm5_world_class(xx+6,yy+12)==0x10){*x=xx;*y=yy;return 1;}
    }
    return 0;
}
static void start_arrival(int seat,int x,int y,int heal) {
    Player *p=&C.player[seat];
    /* Copy only the living body's native fields; inventory/CHR/palette stay
     * personal. Starting at frameset13 skips READY and its stage-specific hub
     * positioning. The native routine opens at the checked destination floor. */
    for(int base=0x300;base<=0x5B8;base+=24) {
        set_personal(p,base,R(base));for(int slot=1;slot<8;slot++)set_personal(p,base+slot,0);
    }
    for(int a=0x33;a<=0x3F;a++)set_personal(p,a,0);
    set_personal(p,0x30,8);set_personal(p,0x31,0);set_personal(p,0x37,0);set_personal(p,0x5B,0);set_personal(p,0x52,0);
    set_personal(p,0x330,x);set_personal(p,0x348,x>>8);set_personal(p,0x378,0);set_personal(p,0x390,0);
    set_personal(p,0x318,0);set_personal(p,0x360,0);set_personal(p,0x3A8,0);set_personal(p,0x3C0,0);
    set_personal(p,0x3D8,0);set_personal(p,0x3F0,8);set_personal(p,0x558,0x13);set_personal(p,0x540,0);set_personal(p,0x570,0);
    set_personal(p,0x528,0x80|(R(0x528)&0x20));set_personal(p,0x5B8,0);
    if(heal){set_personal(p,0xB0,0x9C);C.revivals[seat]++;}
    p->dead=0;p->teleport=TELE_IN;p->arrival_follow=0;p->arrival_floor=(uint8_t)y;p->previous=C.raw[seat];C.revive_pending&=(uint8_t)~(1u<<seat);
    reset_select();
}
static void depart_player(int seat,int withdraw) {
    Player *p=&C.player[seat];
    for(int base=0x300;base<=0x5B8;base+=24)for(int slot=1;slot<8;slot++)set_personal(p,base+slot,0);
    set_personal(p,0x30,0x10);set_personal(p,0x558,0x13);set_personal(p,0x540,4);set_personal(p,0x570,0);
    set_personal(p,0x3D8,0);set_personal(p,0x3F0,8);set_personal(p,0x38,0);set_personal(p,0x34,0);set_personal(p,0x33,0);
    p->teleport=TELE_OUT;reset_select();
    if(withdraw) {
        C.withdrawn|=(uint8_t)(1u<<seat);C.withdrawals[seat]++;
        if(seat==C.anchor){C.anchor=C.current=(uint8_t)(seat^1);project(&C.player[C.anchor]);P(0x18,255);C.handoffs++;}
    }
    sound(0x35);
}
static int scene_changed(void) {
    return (R(0x29)&31)!=C.section || (R(0x28)>=4 && R(0xF9)!=C.scene_page);
}
static void scene_start(void) {
    C.scene_active=1;C.transitions++;reset_select();
    int seat=C.anchor^1;
    if(!C.player[seat].dead && !(C.withdrawn&(1u<<seat))) {
        C.follow|=(uint8_t)(1u<<seat);depart_player(seat,0);C.last_step[seat]=cyc_ring_frame;
        if(C.scene_vertical)set_personal(&C.player[seat],0x348,R(0xF9));
    }
}
static void scene_shift(void) {
    if(!C.scene_active || !C.scene_vertical)return;
    int delta=R(0xFA)-C.scene_scroll_y;if(delta>120)delta-=240;if(delta< -120)delta+=240;
    C.scene_scroll_y=R(0xFA);
    for(int seat=0;seat<2;seat++)if((C.follow&(1u<<seat)) && C.player[seat].teleport==TELE_OUT) {
        Player *p=&C.player[seat];int y=personal(p,0x378)-delta;
        if(y<0){set_personal(p,0x378,y+240);set_personal(p,0x390,255);}
        else if(y>=240){set_personal(p,0x378,y-240);set_personal(p,0x390,1);}
        else set_personal(p,0x378,y);
    }
}
static int before_scroll(uint16_t address) {
    if(!s_on || !C.initialized || C.menu || C.ai_active || C.wiping || C.scene_active || cyc_mod_isolated())return 0;
    C.scene_vertical=(uint8_t)(address==0xCE82);C.scene_scroll_y=R(0xFA);scene_start();return 0;
}
/* Match C9DD's authored exit tests against the initiating body. The native
 * camera script still accepts the exit and owns every progression write. */
static int exit_intent(int seat) {
    const Player *p=&C.player[seat];int state=personal(p,0x30),page=personal(p,0x348);
    if(p->dead || p->teleport!=TELE_NONE || (C.withdrawn&(1u<<seat)) || state>3 || personal(p,0x390) ||
        !(personal(p,0xB0)&31) || page!=R(0xF9))return 0;
    int flags=R(0x29),section=flags&31,header=mm5_cart_prg(C.stage,0xA950+section),local=R(0x2B);
    int x=personal(p,0x330),y=personal(p,0x378),oldx=(personal(p,0x3D)<<8)|personal(p,0x3C),wx=(page<<8)|x;
    if((flags&32) && !(flags&0xC0)) {
        int next=mm5_cart_prg(C.stage,0xA951+section);
        if(local==R(0x2A) && x>240 && wx>oldx && !R(0x55) && (next&32) && !(next&0xC0))return 1;
        if(!local && x<16 && wx<oldx && section && (mm5_cart_prg(C.stage,0xA968+section)&64) &&
            (mm5_cart_prg(C.stage,0xA94F+section)&32))return 1;
    }
    if(R(0xFC))return 0;
    int direction=0;
    if(R(0xAF)) {if(y<8)direction=128;else if(y>=232 && state==3)direction=64;}
    else {if(y>=232)direction=64;else if(y<8 && (personal(p,0x37) || state==3))direction=128;}
    if(!direction)return 0;
    /* CFF0 first checks explicit per-screen vertical links. */
    for(int i=0;i<32;i+=4) {
        int screen=mm5_cart_prg(C.stage,0xA9E0+i);if(screen&128)break;
        if(screen==page) {if(mm5_cart_prg(C.stage,0xA9E1+i)==direction)return 1;break;}
    }
    if(flags&0xC0) {
        if(flags&direction)return local<=R(0x2A);
        return local>0 || section>0;
    }
    int next=mm5_cart_prg(C.stage,0xA951+section);
    if((!R(0x2A) && (next&direction)) || (local && local==R(0x2A)))return (next&direction)!=0;
    return !local && section && (header&0xC0) && (((header&0xC0)^0xC0)&direction);
}
static int before_camera(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || C.ai_active || C.wiping || C.scene_active || C.follow ||
        R(0x26)!=C.stage || R(0x54) || R(0x59) || R(0x1B) || R(0x5A) || R(0xFA) || cyc_mod_isolated())return 0;
    capture(&C.player[C.anchor]);
    /* Seat order resolves simultaneous requests without advancing the world
     * twice or borrowing either player's inventory for the other. */
    for(int seat=0;seat<2;seat++)if(exit_intent(seat)) {
        if(seat!=C.anchor){C.anchor=C.current=(uint8_t)seat;project(&C.player[seat]);P(0x18,255);C.handoffs++;}
        break;
    }
    return 0;
}
static void scene_tick(void) {
    if(!C.initialized || C.menu || C.ai_active || C.wiping || R(0x26)!=C.stage)return;
    if(!C.scene_active && scene_changed()) {
        C.scene_vertical=(uint8_t)(R(0x28)>=4);C.scene_scroll_y=0;scene_start();
    }
    scene_shift();
    C.scene_page=R(0xF9);
}
static void follow_arrivals(void) {
    if(C.scene_active || !C.follow || R(0x30)>3 || R(0x390) || R(0x378)>=240)return;
    int x=(R(0x348)<<8)|R(0x330),y=R(0x378);
    for(int seat=0;seat<2;seat++)if(C.follow&(1u<<seat)) {
        if(!C.player[seat].dead && !(C.withdrawn&(1u<<seat))) {
            start_arrival(seat,x,y,0);Player *p=&C.player[seat];p->arrival_follow=1;
            p->entry_state=R(0x30);p->entry_subtype=(uint8_t)(R(0x558)-R(0x34));p->entry_ladder=R(0x35);p->entry_phase=R(0x540);p->entry_flags=R(0x528);
        }
        C.follow&=(uint8_t)~(1u<<seat);
    }
}
static int after_scroll(uint16_t address) {
    (void)address;if(!s_on || !C.initialized || C.menu || C.ai_active || C.wiping || cyc_mod_isolated())return 0;
    if(!C.scene_active && scene_changed()) {
        C.scene_vertical=(uint8_t)(R(0x28)>=4);C.scene_scroll_y=0;scene_start();
    }
    if(C.scene_active) {
        C.scene_active=0;C.section=R(0x29)&31;C.scene_page=R(0xF9);
        capture(&C.player[C.anchor]);follow_arrivals();
    }
    return 0;
}
static void select_tick(void) {
    int gameplay=!C.menu && !C.ai_active && !C.wiping && !C.scene_active && !C.follow && R(0x26)<16 && R(0x30)<=3 &&
        !R(0x54) && !R(0x59) && !R(0x1B) && !R(0x5A) && !R(0xAF) && !R(0xFA);
    for(int seat=0;seat<2;seat++) {
        Player *p=&C.player[seat];int held=(C.raw[seat]&32)!=0;
        if(!gameplay || p->dead || (p->teleport!=TELE_NONE && p->teleport!=TELE_HIDDEN)) {C.select_hold[seat]=0;continue;}
        if(!held){C.select_armed[seat]=1;C.select_hold[seat]=0;}
        if(!living_visible(seat^1)){C.select_hold[seat]=0;continue;}
        if(C.withdrawn&(1u<<seat)) {
            int x,y;if((C.fresh[seat]&32) && C.select_armed[seat] && safe_position(&x,&y))start_arrival(seat,x,y,0);
        } else if(held && C.select_armed[seat] && personal(p,0x30)<=3 && ++C.select_hold[seat]>=90) {
            depart_player(seat,1);
        }
    }
    if(!gameplay)return;
    int checkpoint=checkpoint_at(R(0x69));
    if(checkpoint>C.checkpoint) {
        C.checkpoint=(uint8_t)checkpoint;
        for(int seat=0;seat<2;seat++)if(C.player[seat].dead)C.revive_pending|=(uint8_t)(1u<<seat);
    }
    int x,y;if(C.revive_pending && safe_position(&x,&y))for(int seat=0;seat<2;seat++)
        if((C.revive_pending&(1u<<seat)) && C.player[seat].dead)start_arrival(seat,x,y,1);
}
void mm5_coop_end(void) {
    if(!s_on)return;
    if(C.initialized && !C.menu && R(0x26)<16){capture(&C.player[C.current]);deaths();scene_tick();follow_arrivals();select_tick();}
    for(int seat=0;seat<2;seat++)C.player[seat].previous=C.raw[seat];
}
static uint8_t pattern(const uint8_t banks[6],int address) {
    int bank;address&=8191;
    if(address<0x800)bank=(banks[0]&254)+(address>>10);
    else if(address<0x1000)bank=(banks[1]&254)+((address-0x800)>>10);
    else bank=banks[2+((address-0x1000)>>10)];
    return mm5_cart_chr(bank,address);
}
void mm5_coop_build(void) {
    memset(&C.build,0,sizeof C.build);
    if(!s_on || !C.initialized || C.menu || R(0x26)>=16)return;
    if(!mm5_hud_capture())C.failures++;
    int seat=C.current^1,owner=C.current;
    scene_shift();
    if(C.scene_active && C.player[seat].teleport==TELE_OUT && C.last_step[seat]!=cyc_ring_frame)
        if(!private_step(seat))C.failures++;
    if(C.player[seat].dead || C.player[seat].teleport==TELE_HIDDEN || !cyc_mod_isolate_begin())return;
    C.build.camera=(uint16_t)((R(0xF9)<<8)|R(0xFC));C.current=(uint8_t)seat;project(&C.player[seat]);int camera=center_camera();
    P(0x9F,4);for(int i=0;i<256;i+=4)P(0x200+i,248);P(0xEC,255);P(0xEE,255);P(0xEF,255);
    uint8_t part_owner[64];memset(part_owner,255,sizeof part_owner);
    int ok=1,body=0;
    for(int slot=0;slot<8 && ok;slot++) {
        if(!R(0x300+slot))continue;
        int begin=R(0x9F);CycModRegs r;cyc_mod_regs(&r);r.x=(uint8_t)slot;P(0x9E,slot);
        ok=cyc_mod_call(0xDFC3,&r);int end=R(0x9F);if(!end)end=256;
        for(int offset=begin;offset<end;offset+=4)part_owner[offset/4]=(uint8_t)slot;
        if(!slot && end>begin){mm5_proto_body(&C.build.body);C.build.body.x+=(int16_t)(camera-C.build.camera);body=1;}
        if(end==256)break;
    }
    uint8_t banks[6];for(int i=0;i<6;i++)banks[i]=R(0xEA+i);
    int end=R(0x9F);if(!end)end=256;
    for(int offset=4;ok && offset<end && C.build.count<PART_CAP;offset+=4) {
        int top=R(0x200+offset)+1;if(top>=240)continue;
        Part *p=&C.build.parts[C.build.count++];p->x=(int16_t)(camera+R(0x203+offset));p->y=(int16_t)top;
        p->owner=part_owner[offset/4];p->attr=R(0x202+offset);
        int tile=R(0x201+offset),table=cyc_render_line_sprite_table(top);
        for(int i=0;i<4;i++)p->colors[i]=R(0x610+(p->attr&3)*4+i)&63;
        for(int y=0;y<8;y++) {
            int row=(p->attr&128)?7-y:y,a=table+tile*16+row,low=pattern(banks,a),high=pattern(banks,a+8);
            for(int x=0;x<8;x++){int bit=(p->attr&64)?x:7-x;p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1));}
        }
    }
    if(ok){capture(&C.player[seat]);C.build.valid=1;C.build.seat=(uint8_t)seat;if(!body)C.build.body.active=0;}else C.failures++;
    cyc_mod_isolate_end();C.current=(uint8_t)owner;
}
void mm5_coop_latch(void) { C.frame=C.build;mm5_hud_latch(); }
void mm5_coop_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed) {
    if(!s_on || !C.frame.valid || C.menu)return;
    int painted=0;
    for(int i=0;i<C.frame.count;i++) {
        const Part *p=&C.frame.parts[i];
        if(!p->owner && C.frame.body.active) {if(!painted){mm5_view_draw_character(out,width,origin,&C.frame.body);painted=1;}continue;}
        for(int y=0;y<8;y++)for(int x=0;x<8;x++) {
            int value=p->pixels[y*8+x],px=p->x-C.frame.camera+origin+x,py=p->y+y;
            if(!value || px<0 || px>=width || py<0 || py>=240)continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32)||!opaque[at])out[at]=cyc_render_color_value(p->colors[value]);
        }
    }
}
void mm5_coop_power(void) { memset(&C,0,sizeof C);C.pending_pause=255;s_scope=s_personal_bhv=0;addresses();mm5_hud_reset(); }
static const char *const HOOKS[]={"mm5.coop.controller","mm5.coop.after-controller","mm5.coop.personal-end","mm5.coop.world-begin","mm5.coop.pause","mm5.coop.ai","mm5.coop.common","mm5.coop.stage","mm5.coop.scroll-end","mm5.coop.advance","mm5.coop.retreat","mm5.coop.vertical","mm5.coop.camera"};
void mm5_coop_set(int enabled,int roster) {
    if(enabled)mm5_proto_cancel_swap();
    s_on=enabled!=0;s_roster=roster!=0;C.initialized=0;C.current=C.anchor=0;C.pending_pause=255;
    mm5_hud_reset();
    for(int i=0;i<sizeof HOOKS/sizeof *HOOKS;i++)nes_mod_set_function_hook_enabled(HOOKS[i],s_on);
    nes_mod_set_local_only("MM5 personal co-op (unqualified netplay)",s_on);mm5_view_coop(s_on);
}
static int get(uint8_t *out,int cap) {if(cap<sizeof C)return -1;memcpy(C.magic,"MC06",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;
    static Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"MC06",4)||p.enabled!=s_on||p.roster!=s_roster||p.initialized>1||p.anchor>1||p.current>1||p.menu>1||p.pauser>1||p.ai_active>1||p.ai_common>1||p.ai_actor>23||p.wiping>1||p.retry>1||p.withdrawn>3||p.revive_pending>3||p.checkpoint>2||p.scene_active>1||p.follow>3||p.section>31||p.scene_vertical>1||p.scene_scroll_y>=240||p.player[0].dead>1||p.player[1].dead>1||p.player[0].teleport>TELE_IN||p.player[1].teleport>TELE_IN||p.player[0].arrival_floor>=240||p.player[1].arrival_floor>=240)return 0;
    for(int seat=0;seat<2;seat++)if(p.select_hold[seat]>90 || p.select_armed[seat]>1)return 0;
    for(int seat=0;seat<2;seat++)if(p.player[seat].arrival_follow>1 || p.player[seat].entry_state>3)return 0;
    const Picture *f[2]={&p.build,&p.frame};
    for(int i=0;i<2;i++) {
        if(f[i]->valid>1||f[i]->seat>1||f[i]->count>PART_CAP||f[i]->body.pose>=CYC_PROTO_POSE_COUNT||f[i]->body.active>1||f[i]->body.right>1||f[i]->body.flipped>1||f[i]->body.behind>1)return 0;
        for(int j=0;j<f[i]->count;j++) {
            const Part *part=&f[i]->parts[j];if(part->owner>7 && part->owner!=255)return 0;
            for(int k=0;k<4;k++)if(part->colors[k]>63)return 0;
            for(int k=0;k<64;k++)if(part->pixels[k]>3)return 0;
        }
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&C,in,sizeof C);else mm5_coop_power();return 1;}
void mm5_coop_register(void) {
    mm5_hud_register();
    addresses();nes_mod_register_function_entry_plugin(HOOKS[0],0x8000,controller);nes_mod_register_function_entry_plugin(HOOKS[1],0xDEB4,after_controller);
    nes_mod_register_function_entry_plugin(HOOKS[2],0x8039,personal_end);nes_mod_register_function_entry_plugin(HOOKS[3],0x8000,world_begin);
    nes_mod_register_function_entry_plugin(HOOKS[4],0xDE89,pause_entry);
    nes_mod_register_function_entry_plugin(HOOKS[5],0x8004,ai_begin);nes_mod_register_function_entry_plugin(HOOKS[6],0x805B,ai_common);
    nes_mod_register_function_entry_plugin(HOOKS[7],0xD311,stage_load);
    nes_mod_register_function_entry_plugin(HOOKS[8],0xDEC6,after_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[9],0xCB80,before_scroll);nes_mod_register_function_entry_plugin(HOOKS[10],0xCC68,before_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[11],0xCE82,before_scroll);
    nes_mod_register_function_entry_plugin(HOOKS[12],0xDEC3,before_camera);
    nes_mod_register_savestate_hook("mm5.coop",get,set);nes_mod_register_savestate_validator("mm5.coop",valid);
}
int mm5_coop_json(char *out,int cap) {
    return snprintf(out,cap,"\"coop\":{\"on\":%d,\"ready\":%u,\"anchor\":%u,\"menu\":%u,\"pauser\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"failed\":%llu,\"combat\":[[%llu,%llu],[%llu,%llu]],\"deaths\":[%llu,%llu],\"handoffs\":%llu,\"wipes\":%llu,\"targets\":[%llu,%llu],\"checks\":%llu,\"shots\":[[%d,%d,%d],[%d,%d,%d]],\"transport\":[%u,%u,%u,%u,%u],\"withdrawals\":[%llu,%llu],\"rejoins\":[%llu,%llu],\"revivals\":[%llu,%llu],\"scene\":[%u,%u,%u,%u],\"transitions\":%llu,\"arrivals\":[%llu,%llu],\"pages\":[%d,%d],\"lives\":%u,\"tanks\":[%u,%u],\"coil\":[%d,%d],\"actor8\":[%u,%u,%u,%u,%u],\"players\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]]}",
        s_on,C.initialized,C.anchor,C.menu,C.pauser,(unsigned long long)C.world_updates,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],(unsigned long long)C.failures,
        (unsigned long long)C.hits[0],(unsigned long long)C.contacts[0],(unsigned long long)C.hits[1],(unsigned long long)C.contacts[1],
        (unsigned long long)C.deaths[0],(unsigned long long)C.deaths[1],(unsigned long long)C.handoffs,(unsigned long long)C.wipes,
        (unsigned long long)C.targets[0],(unsigned long long)C.targets[1],(unsigned long long)C.checks,
        personal(&C.player[0],0x301),personal(&C.player[0],0x302),personal(&C.player[0],0x303),personal(&C.player[1],0x301),personal(&C.player[1],0x302),personal(&C.player[1],0x303),
        C.withdrawn,C.player[0].teleport,C.player[1].teleport,C.checkpoint,C.revive_pending,
        (unsigned long long)C.withdrawals[0],(unsigned long long)C.withdrawals[1],(unsigned long long)C.rejoins[0],(unsigned long long)C.rejoins[1],
        (unsigned long long)C.revivals[0],(unsigned long long)C.revivals[1],
        C.scene_active,C.follow,C.section,C.scene_page,(unsigned long long)C.transitions,(unsigned long long)C.arrivals[0],(unsigned long long)C.arrivals[1],
        personal(&C.player[0],0x348),personal(&C.player[1],0x348),
        R(0xBF),R(0xBD)&15,R(0xBE)&15,mm5_coop_read(0,0xBA)&31,mm5_coop_read(1,0xBA)&31,R(0x308),R(0x458),R(0x338),R(0x380),R(0x428),
        personal(&C.player[0],0x330),personal(&C.player[0],0x378),personal(&C.player[0],0xB0)&31,personal(&C.player[0],0x32),personal(&C.player[0],0x30),
        personal(&C.player[1],0x330),personal(&C.player[1],0x378),personal(&C.player[1],0xB0)&31,personal(&C.player[1],0x32),personal(&C.player[1],0x30));
}
