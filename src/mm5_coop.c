/* Personal native execution contexts; the real world engine runs once. */
#include "mm5_coop.h"
#include "mm5_cart.h"
#include "mm5_proto.h"
#include "mm5_weapons.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=384, PART_CAP=96 };
typedef struct { uint8_t ram[PERSONAL_CAP],previous,dead; } Player;
typedef struct { int16_t x,y;uint8_t owner,attr,colors[4],pixels[64]; } Part;
typedef struct { uint8_t valid,seat;uint16_t count,camera;Mm5ProtoBody body;Part parts[PART_CAP]; } Picture;
typedef struct {
    char magic[4];uint8_t enabled,initialized,stage,anchor,current,menu,pauser,pending_pause,roster;
    uint8_t raw[2],raw_previous[2],fresh[2];
    Player player[2];Picture build,frame;
    uint64_t updates[2],world_updates,failures;
} Coop;
static Coop C;
static int s_on,s_roster,s_scope,s_personal_bhv;
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
static int s_count;
typedef struct {const char *id;int enabled;} HookSetting;
static HookSetting s_hooks[128];
static int s_hook_count;

int mm5_coop_enabled(void) { return s_on; }
int mm5_coop_seat(void) { return s_on?C.current:0; }
int mm5_coop_character(void) { return s_on?(s_roster^C.current):-1; }
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
    int start=R(0xF9)-R(0x2B),camera=((R(0x348)<<8)|R(0x330))-128;
    int left=start*256,right=(start+R(0x2A))*256;
    if(start>=0 && (R(0x29)&32)) {if(camera<left)camera=left;if(camera>right)camera=right;}
    if(camera<0)camera=0;P(0xFC,camera);P(0xF9,camera>>8);return camera;
}
static int private_step(int seat) {
    int state=personal(&C.player[seat],0x30);
    if(state>8 || state==7 || C.player[seat].dead)return 1;
    uint8_t before[2048],after[2048];snapshot_ram(before);
    if(!cyc_mod_isolate_begin())return 0;
    int owner=C.current;C.current=(uint8_t)seat;project(&C.player[seat]);center_camera();
    P(0x14,C.raw[seat]&~C.player[seat].previous&~0x30);P(0x16,C.raw[seat]&~0x30);P(0x15,0);P(0x17,0);
    s_scope=1;int enabled=hooks(1),ok=enabled;CycModRegs r;cyc_mod_regs(&r);r.a=0x1B;
    if(ok)ok=cyc_mod_call(0xFF3D,&r);
    mm5_weapons_dispatch(0xDEB1);
    if(ok)ok=cyc_mod_call(0x8000,&r);
    if(ok) {r.a=0x1C;ok=cyc_mod_call(0xFF3D,&r);P(0xF6,C.stage);if(ok)ok=cyc_mod_call(0xFF43,&r);}
    s_personal_bhv=1;if(ok)ok=cyc_mod_call(0x8000,&r);s_personal_bhv=0;
    if(ok){capture(&C.player[seat]);snapshot_ram(after);C.updates[seat]++;}
    if(enabled)hooks(0);s_scope=0;cyc_mod_isolate_end();C.current=(uint8_t)owner;
    if(ok)merge_world(before,after);return ok;
}
static void initialize(void) {
    C.anchor=C.current=0;C.stage=R(0x26);C.initialized=1;C.menu=0;C.pending_pause=255;
    capture(&C.player[0]);C.player[1]=C.player[0];
    int x=(R(0x348)<<8)|R(0x330);x=x>=32?x-16:x+16;
    set_personal(&C.player[1],0x330,x);set_personal(&C.player[1],0x348,x>>8);
    for(int seat=0;seat<2;seat++)C.player[seat].previous=C.raw[seat];
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
    C.current=C.anchor;capture(&C.player[C.anchor]);
    if(!private_step(C.anchor^1))C.failures++;
    C.current=C.anchor;P(0x14,C.raw[C.anchor]&~C.player[C.anchor].previous&~0x30);P(0x16,C.raw[C.anchor]&~0x30);
    P(0x15,0);P(0x17,0);return 0;
}
static int after_controller(uint16_t address) {
    (void)address;if(s_on && C.initialized && !cyc_mod_isolated() && !C.menu) {capture(&C.player[C.anchor]);C.updates[C.anchor]++;}return 0;
}
static int personal_end(uint16_t address) {
    (void)address;
    /* Finish the native loop at its common terrain-hazard check, after slot7.
     * Returning from here would incorrectly omit the player's spike checks. */
    if(s_on && s_scope && s_personal_bhv && R(0xA6)>=7)P(0xA6,23);
    return 0;
}
static int world_begin(uint16_t address) { (void)address;if(s_on && C.initialized && !cyc_mod_isolated())C.world_updates++;return 0; }
static int pause_entry(uint16_t address) {
    (void)address;
    if(!s_on || !C.initialized || C.menu || C.pending_pause>1 || cyc_mod_isolated())return 0;
    int seat=C.pending_pause;C.pending_pause=255;
    if(R(0x26)>=16 || R(0x54) || personal(&C.player[seat],0x30)>=4 || !(personal(&C.player[seat],0xB0)&31))return 0;
    capture(&C.player[C.anchor]);C.current=C.pauser=(uint8_t)seat;project(&C.player[seat]);
    P(0x14,C.fresh[seat]);P(0x16,C.raw[seat]);C.menu=1;return 0;
}
void mm5_coop_input(uint8_t buttons[2]) {
    if(!s_on)return;
    for(int seat=0;seat<2;seat++){C.raw[seat]=buttons[seat];C.fresh[seat]=(uint8_t)(buttons[seat]&~C.raw_previous[seat]);C.raw_previous[seat]=buttons[seat];}
    if(C.menu)buttons[0]=C.raw[C.pauser];
    else {
        C.pending_pause=255;
        buttons[0]=C.raw[C.anchor];
        for(int seat=0;seat<2;seat++)if(C.fresh[seat]&16){C.pending_pause=(uint8_t)seat;buttons[0]|=16;break;}
    }
    buttons[1]=0; /* Original pad-2 Select/B are native debug controls. */
}
void mm5_coop_end(void) {
    if(!s_on)return;
    if(C.initialized && !C.menu && R(0x26)<16){C.current=C.anchor;capture(&C.player[C.anchor]);}
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
    int seat=C.anchor^1,owner=C.current;
    if(C.player[seat].dead || !cyc_mod_isolate_begin())return;
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
void mm5_coop_latch(void) { C.frame=C.build; }
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
void mm5_coop_power(void) { memset(&C,0,sizeof C);C.pending_pause=255;s_scope=s_personal_bhv=0;addresses(); }
static const char *const HOOKS[]={"mm5.coop.controller","mm5.coop.after-controller","mm5.coop.personal-end","mm5.coop.world-begin","mm5.coop.pause"};
void mm5_coop_set(int enabled,int roster) {
    if(enabled)mm5_proto_cancel_swap();
    s_on=enabled!=0;s_roster=roster!=0;C.initialized=0;C.current=C.anchor=0;C.pending_pause=255;
    for(int i=0;i<sizeof HOOKS/sizeof *HOOKS;i++)nes_mod_set_function_hook_enabled(HOOKS[i],s_on);
    nes_mod_set_local_only("MM5 personal co-op (unqualified netplay)",s_on);mm5_view_coop(s_on);
}
static int get(uint8_t *out,int cap) {if(cap<sizeof C)return -1;memcpy(C.magic,"MC02",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;
    static Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"MC02",4)||p.enabled!=s_on||p.roster!=s_roster||p.initialized>1||p.anchor>1||p.current>1||p.menu>1||p.pauser>1)return 0;
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
    addresses();nes_mod_register_function_entry_plugin(HOOKS[0],0x8000,controller);nes_mod_register_function_entry_plugin(HOOKS[1],0xDEB4,after_controller);
    nes_mod_register_function_entry_plugin(HOOKS[2],0x8039,personal_end);nes_mod_register_function_entry_plugin(HOOKS[3],0x8000,world_begin);
    nes_mod_register_function_entry_plugin(HOOKS[4],0xDE89,pause_entry);
    nes_mod_register_savestate_hook("mm5.coop",get,set);nes_mod_register_savestate_validator("mm5.coop",valid);
}
int mm5_coop_json(char *out,int cap) {
    return snprintf(out,cap,"\"coop\":{\"on\":%d,\"ready\":%u,\"anchor\":%u,\"menu\":%u,\"pauser\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"failed\":%llu,\"players\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]]}",
        s_on,C.initialized,C.anchor,C.menu,C.pauser,(unsigned long long)C.world_updates,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],(unsigned long long)C.failures,
        personal(&C.player[0],0x330),personal(&C.player[0],0x378),personal(&C.player[0],0xB0)&31,personal(&C.player[0],0x32),personal(&C.player[0],0x30),
        personal(&C.player[1],0x330),personal(&C.player[1],0x378),personal(&C.player[1],0xB0)&31,personal(&C.player[1],0x32),personal(&C.player[1],0x30));
}
