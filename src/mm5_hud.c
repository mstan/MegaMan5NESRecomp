/* Native E27E meter construction, captured separately for each player. */
#include "mm5_hud.h"
#include "mm5_cart.h"
#include "mm5_coop.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { METER_PIXELS=8*56 };
typedef struct {
    char magic[4];uint8_t valid,boss,character[2];
    uint8_t meters[2][2][METER_PIXELS],boss_meter[METER_PIXELS];
    uint8_t palette[2][8],boss_palette[4];
} Hud;
static Hud s_build,s_frame;
static uint8_t pattern(const uint8_t banks[6],int address) {
    int bank;address&=8191;
    if(address<0x800)bank=(banks[0]&254)+(address>>10);
    else if(address<0x1000)bank=(banks[1]&254)+((address-0x800)>>10);
    else bank=banks[2+((address-0x1000)>>10)];
    return mm5_cart_chr(bank,address);
}
static int meter(uint8_t *out,int which,const uint8_t banks[6]) {
    P(0x9F,4);CycModRegs r;cyc_mod_regs(&r);r.x=(uint8_t)which;
    if(!cyc_mod_call(0xE27E,&r) || R(0x9F)!=32)return 0;
    for(int i=0;i<7;i++) {
        int at=0x204+i*4,tile=R(at+1),attr=R(at+2),top=R(at)+1;
        if(top<25 || top>73)return 0;
        for(int y=0;y<8;y++) {
            int row=(attr&128)?7-y:y,a=cyc_render_line_sprite_table(top)+tile*16+row;
            int low=pattern(banks,a),high=pattern(banks,a+8),dy=top-25+y;
            if(dy>=56)return 0;
            for(int x=0;x<8;x++) {
                int bit=(attr&64)?x:7-x,v=((low>>bit)&1)|(((high>>bit)&1)<<1);
                out[dy*8+x]=(uint8_t)(v?((attr&3)*4+v):0);
            }
        }
    }
    return 1;
}
void mm5_hud_reset(void) {memset(&s_build,0,sizeof s_build);memset(&s_frame,0,sizeof s_frame);}
int mm5_hud_capture(void) {
    memset(&s_build,0,sizeof s_build);
    /* The native script clears the meter-enable bit for story/ending scenes.
     * Do not manufacture personal meters when the game hides its own HUD. */
    if(!mm5_coop_enabled() || cyc_mod_isolated() || !(R(0x2D)&128))return 1;
    Hud next={0};int boss=(R(0x2F)&128) && R(0x56)<24 && R(0x450+R(0x56));
    for(int seat=0;seat<2;seat++) {
        uint8_t inventory[13],banks[6];
        for(int i=0;i<13;i++)inventory[i]=(uint8_t)mm5_coop_read(seat,(uint16_t)(0xB0+i));
        for(int i=0;i<8;i++)next.palette[seat][i]=(uint8_t)(mm5_coop_read(seat,(uint16_t)(0x610+i))&63);
        for(int i=0;i<6;i++)banks[i]=R(0xEA+i);
        banks[3]=(uint8_t)mm5_coop_read(seat,0xED);
        int weapon=mm5_coop_read(seat,0x32);next.character[seat]=(uint8_t)mm5_coop_character_at(seat);
        if(!cyc_mod_isolate_begin())return 0;
        for(int i=0;i<13;i++)P(0xB0+i,inventory[i]);
        P(0x2D,128);P(0x2E,weapon?128|weapon:0);
        int ok=meter(next.meters[seat][0],0,banks);
        if(ok && weapon)ok=meter(next.meters[seat][1],1,banks);
        if(ok && !seat && boss) {
            ok=meter(next.boss_meter,2,banks);next.boss=1;
            for(int i=0;i<4;i++)next.boss_palette[i]=R(0x618+i)&63;
        }
        cyc_mod_isolate_end();if(!ok)return 0;
    }
    next.valid=1;s_build=next;return 1;
}
void mm5_hud_latch(void) {s_frame=s_build;}
int mm5_hud_native_meter(const uint8_t *o) {
    if(!mm5_coop_enabled() || !s_frame.valid || !o)return 0;
    return o[0]>=24 && o[0]<=72 && !(o[0]&7) && o[1]>=0x60 && o[1]<=0x64 &&
        ((o[3]==24 && o[2]==1) || (o[3]==16 && o[2]==0) || (o[3]==40 && o[2]==2));
}
static void draw_meter(uint32_t *out,int width,int height,int left,int top,const uint8_t *pixels,const uint8_t *palette,int tint) {
    for(int y=0;y<56;y++)for(int x=0;x<8;x++) {
        int v=pixels[y*8+x],px=left+x,py=top+y;
        if(!v || px<0 || px>=width || py<0 || py>=height)continue;
        uint32_t color=cyc_render_color_value(palette[v]);
        if(tint && (color&0xFFFFFF)) {
            unsigned r=color>>16&255,g=color>>8&255,b=color&255;
            color=0xFF000000u|((r*3+255)/4<<16)|((g*3+64)/4<<8)|(b*3+64)/4;
        }
        out[py*width+px]=color;
    }
}
void mm5_hud_draw(uint32_t *out,int width,int height,int own) {
    if(!mm5_coop_enabled() || !s_frame.valid)return;
    for(int seat=0;seat<2;seat++) {
        if(own>=0 && own!=seat)continue;
        int top=20+(own<0?seat*76:0);
        draw_meter(out,width,height,8,top,s_frame.meters[seat][0],s_frame.palette[seat],s_frame.character[seat]);
        draw_meter(out,width,height,24,top,s_frame.meters[seat][1],s_frame.palette[seat],0);
    }
    if(s_frame.boss) {
        uint8_t palette[16]={0};for(int i=0;i<4;i++)palette[8+i]=s_frame.boss_palette[i];
        draw_meter(out,width,height,width-16,20,s_frame.boss_meter,palette,0);
    }
}
static int get(uint8_t *out,int cap) {
    if(cap<sizeof s_build+sizeof s_frame)return -1;
    memcpy(s_build.magic,"MH01",4);memcpy(s_frame.magic,"MH01",4);
    memcpy(out,&s_build,sizeof s_build);memcpy(out+sizeof s_build,&s_frame,sizeof s_frame);return sizeof s_build+sizeof s_frame;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !mm5_coop_enabled();if(!in || len!=sizeof s_build+sizeof s_frame)return 0;
    Hud h[2];memcpy(h,in,sizeof h);
    for(int f=0;f<2;f++) {
        if(memcmp(h[f].magic,"MH01",4) || h[f].valid>1 || h[f].boss>1 || h[f].character[0]>1 || h[f].character[1]>1)return 0;
        for(int seat=0;seat<2;seat++) {
            for(int i=0;i<8;i++)if(h[f].palette[seat][i]>63)return 0;
            for(int m=0;m<2;m++)for(int i=0;i<METER_PIXELS;i++)if(h[f].meters[seat][m][i]>7)return 0;
        }
        for(int i=0;i<METER_PIXELS;i++)if(h[f].boss_meter[i]>11)return 0;
        for(int i=0;i<4;i++)if(h[f].boss_palette[i]>63)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len) {
    if(!valid(in,len))return 0;if(!len)mm5_hud_reset();else {memcpy(&s_build,in,sizeof s_build);memcpy(&s_frame,in+sizeof s_build,sizeof s_frame);}return 1;
}
void mm5_hud_register(void) {nes_mod_register_savestate_hook("mm5.hud",get,set);nes_mod_register_savestate_validator("mm5.hud",valid);}
