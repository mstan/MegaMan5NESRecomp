/* Bounded homes for proven independent actors. Linked dripper children and
 * weapon targets keep native slots. Gravity panels have no actor references
 * and cannot be targeted; straight shots58/61 also have no external links. */
#include "mm5_pool.h"
#include "mm5_coop.h"
#include "mm5_wide.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum {CAPACITY=16,FIELDS=30,BORROW=23,TYPE=0,X=2,SCREEN=3,Y=5,SPAWN=13};
typedef struct {uint8_t byte[FIELDS];} Body;
typedef struct {Body body;uint8_t used;uint32_t serial,updated,spilled;} Actor;
typedef struct {
    char magic[4];uint32_t epoch,serial;Actor actor[CAPACITY];Body resident;
    uint8_t passed[16],borrowing,index,render_ready,draining;
    Body birth_resident;uint8_t birth_active,birth_slot,birth_index;
    uint64_t spills,births,updates,promoted,retired,unsupported,failures;uint32_t peak;
} Pool;
static Pool O;
static Pool s_saved;
static int s_private;
void mm5_pool_private_begin(void){s_saved=O;s_private=1;}
void mm5_pool_private_end(int commit){if(!commit)O=s_saved;s_private=0;}
static int live(const Actor *a){return a->used && a->body.byte[TYPE];}
static void take(Body *b,int slot){for(int i=0;i<FIELDS;i++)b->byte[i]=R(0x300+i*24+slot);}
static void put(const Body *b,int slot){for(int i=0;i<FIELDS;i++)P(0x300+i*24+slot,b->byte[i]);}
static void wipe(int slot) {
    /* Native $F2C4's free-slot contract. Child initializers deliberately
     * leave these fields alone, including the coroutine PC high byte. */
    static const uint16_t clear[]={0x5A0,0x300,0x5B8,0x450,0x408,0x468,0x480,0x498,0x4B0,0x4C8,0x4E0,0x4F8,0x510};
    P(0x438+slot,255);for(int i=0;i<sizeof clear/sizeof clear[0];i++)P(clear[i]+slot,0);
}
static int eligible(int slot) {
    int type=R(0x300+slot);
    return type==6 || type==7 || ((type==0x58 || type==0x61) && R(0x438+slot)==255);
}
static int count(void){int n=0;for(int i=0;i<CAPACITY;i++)n+=live(&O.actor[i]);return n;}
static int active(void){return mm5_coop_enabled() || O.draining;}
void mm5_pool_refresh(void) {
    static const char *const ids[]={"mm5.world.pool-x","mm5.world.pool-y","mm5.world.pool-authored","mm5.world.pool-clear","mm5.world.pool-begin","mm5.world.pool-tail","mm5.world.pool-birth"};
    for(int i=0;i<sizeof ids/sizeof *ids;i++)nes_mod_set_function_hook_enabled(ids[i],active());
}
int mm5_pool_busy(void){return O.borrowing || O.birth_active;}
void mm5_pool_disable(void){O.draining=(uint8_t)(count()!=0);mm5_pool_refresh();}
int mm5_pool_authored(int index) {
    if(!active() || index==255)return 0;
    for(int i=0;i<CAPACITY;i++)if(live(&O.actor[i]) && O.actor[i].body.byte[SPAWN]==index)return 1;
    return 0;
}
void mm5_pool_reset(void){memset(&O,0,sizeof O);mm5_pool_refresh();}
void mm5_pool_abort(void) {
    if(O.birth_active){put(&O.birth_resident,O.birth_slot);O.actor[O.birth_index].used=0;O.birth_active=0;}
    if(!O.borrowing)return;
    Actor *a=&O.actor[O.index];take(&a->body,BORROW);a->updated=O.epoch;if(!a->body.byte[TYPE])a->used=0;
    put(&O.resident,BORROW);O.borrowing=0;
}
static int contains(int x) {
    if(!mm5_coop_enabled())return mm5_wide_contains(x,24);
    int left[2];if(!mm5_coop_view_rects(left))return 0;
    if(mm5_coop_independent())return (x+24>=left[0] && x-24<left[0]+256) || (x+24>=left[1] && x-24<left[1]+256);
    return mm5_wide_contains(x,24);
}
void mm5_pool_begin(void) {
    if(!active() || cyc_mod_isolated())return;
    if(O.borrowing){O.failures++;mm5_pool_abort();}
    if(!++O.epoch){O.epoch=1;for(int i=0;i<CAPACITY;i++)O.actor[i].updated=0;}
    memset(O.passed,0,sizeof O.passed);
    for(int i=0;i<CAPACITY;i++)if(live(&O.actor[i])) {
        int x=O.actor[i].body.byte[SCREEN]*256+O.actor[i].body.byte[X];
        if(!contains(x)){O.actor[i].used=0;O.retired++;}
    }
}
int mm5_pool_make_room(void) {
    if(!mm5_coop_enabled() || (cyc_mod_isolated() && !s_private))return 0;
    int views[2];if(!mm5_coop_view_rects(views))return 0;
    for(int slot=8;slot<24;slot++)if(!R(0x300+slot))return 1;
    int index=-1;for(int i=0;i<CAPACITY;i++)if(!live(&O.actor[i])){index=i;break;}
    if(index<0){O.unsupported++;return 0;}
    int current=R(0xA6);
    /* Highest movable slot leaves every parent and target index pinned.
     * Prefer anonymous shots over authored, noncombat gravity panels. */
    for(int pass=0;pass<2;pass++)for(int slot=23;slot>=8;slot--)
        if(slot!=current && !(O.borrowing && slot==BORROW) && eligible(slot) &&
            ((R(0x300+slot)==6 || R(0x300+slot)==7)==(pass!=0))) {
            Actor *a=&O.actor[index];memset(a,0,sizeof *a);take(&a->body,slot);a->used=1;
            a->serial=++O.serial;a->spilled=O.epoch;a->updated=O.passed[slot-8]?O.epoch:O.epoch-1;
            wipe(slot);O.spills++;return 1;
        }
    O.unsupported++;return 0;
}
static int allocate(uint16_t site) {
    if(mm5_pool_make_room())return 0;
    if(site!=0xF16F || !mm5_coop_enabled() || cyc_mod_isolated() || O.borrowing || O.birth_active)return 0;
    for(int i=8;i<24;i++)if(!R(0x300+i))return 0;
    CycModRegs r;cyc_mod_regs(&r);
    unsigned caller=R(0x100|(uint8_t)(r.s+1)) | R(0x100|(uint8_t)(r.s+2))<<8;
    /* This exact native turret initializer retains no child-slot reference.
     * Let it fill one temporary slot, then restore its pinned resident before
     * the original volley loop continues. Native clocks and RNG stay intact. */
    if(caller!=0x8D3F || r.x<8 || r.x>=24 || R(0x300+r.x)!=0x36 ||
       R(0x8D3D)!=0x20 || R(0x8D3E)!=0x6F || R(0x8D3F)!=0xF1 || R(0x8D6C)!=0xC6 || R(0x8D6D)!=0x0E)return 0;
    int index=-1;for(int i=0;i<CAPACITY;i++)if(!live(&O.actor[i])){index=i;break;}if(index<0)return 0;
    int slot=r.x==23?22:23;take(&O.birth_resident,slot);
    Body empty;memset(&empty,0,sizeof empty);empty.byte[SPAWN]=255;put(&empty,slot);
    Actor *a=&O.actor[index];memset(a,0,sizeof *a);a->used=1;a->serial=++O.serial;a->spilled=O.epoch;a->updated=O.epoch-1;
    O.birth_active=1;O.birth_slot=(uint8_t)slot;O.birth_index=(uint8_t)index;
    if(O.unsupported)O.unsupported--;r.y=(uint8_t)slot;r.a=0;r.p=(uint8_t)((r.p&~0x83)|2);cyc_mod_set_regs(&r);return 1;
}
static int birth_end(uint16_t site) {
    (void)site;if(!O.birth_active || cyc_mod_isolated())return 0;
    Actor *a=&O.actor[O.birth_index];take(&a->body,O.birth_slot);
    if(a->body.byte[TYPE]!=0x58){O.failures++;a->used=0;}else O.births++;
    put(&O.birth_resident,O.birth_slot);O.birth_active=0;return 0;
}
static int authored(uint16_t site) {
    (void)site;if(!active() || cyc_mod_isolated())return 0;
    int index=R(0x06);if(!mm5_pool_authored(index))return 0;
    CycModRegs r;cyc_mod_regs(&r);r.a=(uint8_t)index;r.x=BORROW;r.p=(uint8_t)((r.p&~0x82)|3);cyc_mod_set_regs(&r);return 1;
}
static int clear(uint16_t site){(void)site;if(active() && !cyc_mod_isolated())mm5_pool_reset();return 0;}
static void promote(void) {
    for(int i=0;i<CAPACITY;i++) {
        Actor *a=&O.actor[i];if(!live(a) || a->spilled==O.epoch || a->updated!=O.epoch)continue;
        for(int slot=8;slot<24;slot++)if(!R(0x300+slot)){put(&a->body,slot);a->used=0;O.promoted++;break;}
    }
}
int mm5_pool_next(unsigned short site) {
    (void)site;if(!active() || cyc_mod_isolated())return 0;
    int slot=R(0xA6);
    if(O.borrowing) {
        Actor *a=&O.actor[O.index];take(&a->body,BORROW);a->updated=O.epoch;if(!a->body.byte[TYPE])a->used=0;
        put(&O.resident,BORROW);O.borrowing=0;O.updates++;
    } else {
        if(slot>=8 && slot<24)O.passed[slot-8]=1;
        if(slot!=BORROW)return 0;
    }
    int total=count();for(int k=8;k<24;k++)total+=R(0x300+k)!=0;if(total>O.peak)O.peak=(uint32_t)total;
    int next=-1;for(int i=0;i<CAPACITY;i++)if(live(&O.actor[i]) && O.actor[i].updated!=O.epoch &&
        (next<0 || O.actor[i].serial<O.actor[next].serial))next=i;
    if(next<0){promote();if(!mm5_coop_enabled() && !count()){O.draining=0;mm5_pool_refresh();}return 0;}
    CycModRegs r;cyc_mod_regs(&r);if(r.s<2){O.failures++;return 0;}
    take(&O.resident,BORROW);put(&O.actor[next].body,BORROW);O.borrowing=1;O.index=(uint8_t)next;
    /* The hook's RTS resumes the real per-actor dispatch, with its original
     * clocks, collisions and audio; resident actors receive no second turn. */
    unsigned target=0x8004-1;r.s-=2;P(0x100|(uint8_t)(r.s+1),target);P(0x100|(uint8_t)(r.s+2),target>>8);
    r.x=BORROW;P(0xA6,BORROW);cyc_mod_set_regs(&r);return 1;
}
void mm5_pool_render_begin(void){O.render_ready=1;}
void mm5_pool_draw(void) {
    if(!active() || O.borrowing || !O.render_ready || R(0x26)>=16 || R(0x59) || cyc_mod_isolated())return;
    O.render_ready=0;
    Body resident;take(&resident,BORROW);
    for(int i=0;i<CAPACITY;i++)if(live(&O.actor[i])) {
        put(&O.actor[i].body,BORROW);mm5_wide_capture_extra(BORROW,i);take(&O.actor[i].body,BORROW);
        if(!O.actor[i].body.byte[TYPE])O.actor[i].used=0;
    }
    put(&resident,BORROW);
}
int mm5_pool_json(char *out,int cap) {
    return snprintf(out,cap,"\"pool\":{\"extra\":%d,\"peak\":%u,\"spills\":%llu,\"births\":%llu,\"updates\":%llu,\"promoted\":%llu,\"retired\":%llu,\"unsupported\":%llu,\"failed\":%llu}",count(),O.peak,
        (unsigned long long)O.spills,(unsigned long long)O.births,(unsigned long long)O.updates,(unsigned long long)O.promoted,(unsigned long long)O.retired,(unsigned long long)O.unsupported,(unsigned long long)O.failures);
}
static int save(uint8_t *out,int cap){if(cap<sizeof O)return -1;memcpy(O.magic,"MP02",4);memcpy(out,&O,sizeof O);return sizeof O;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !mm5_coop_enabled();if(!in || len!=sizeof O || memcmp(in,"MP02",4))return 0;
    Pool p;memcpy(&p,in,sizeof p);if(p.borrowing>1 || p.index>=CAPACITY || p.render_ready>1 || p.draining>1 || p.birth_active>1 || p.birth_index>=CAPACITY || (p.birth_active && (p.birth_slot<8 || p.birth_slot>=24 || p.borrowing)))return 0;
    for(int i=0;i<CAPACITY;i++)if(p.actor[i].used>1)return 0;return 1;
}
static int load(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(&O,in,sizeof O);else mm5_pool_reset();mm5_pool_refresh();return 1;}
static int solo_begin(uint16_t site){(void)site;if(!mm5_coop_enabled() && O.draining)mm5_pool_begin();return 0;}
static int solo_tail(uint16_t site){return !mm5_coop_enabled() && O.draining?mm5_pool_next(site):0;}
void mm5_pool_register(void) {
    nes_mod_register_function_entry_plugin("mm5.world.pool-x",0xF15F,allocate);
    nes_mod_register_function_entry_plugin("mm5.world.pool-y",0xF16F,allocate);
    nes_mod_register_function_entry_plugin("mm5.world.pool-authored",0x9995,authored);
    nes_mod_register_function_entry_plugin("mm5.world.pool-clear",0xC39D,clear);
    nes_mod_register_function_entry_plugin("mm5.world.pool-begin",0x8000,solo_begin);
    nes_mod_register_function_entry_plugin("mm5.world.pool-tail",0x8039,solo_tail);
    nes_mod_register_function_entry_plugin("mm5.world.pool-birth",0x8D6C,birth_end);
    nes_mod_register_savestate_hook("mm5.pool",save,load);nes_mod_register_savestate_validator("mm5.pool",valid);
}
