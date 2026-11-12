/* Retail inventory layout, packets and cursor sprites, captured in isolation. */
#include "mm5_menu.h"
#include "mm5_coop.h"
#include "mm5_cart.h"
#include "mm5_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include "mod_function_hooks.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {
    uint8_t valid,cursor,character,key[52],tiles[1024],oam[60],chr[6],palette[32];
    uint16_t bg_table,sprite_table;
} MenuPage;
typedef struct {char magic[4];MenuPage page[2];} MenuState;
static MenuState S;
static uint32_t s_panel[2][256*240];
static MenuPage *s_capture;
static int s_capture_ok;
/* $2006/$2007 writes settle on PPU dots. Unclocked native calls therefore
 * terminate their original finite write packets here, into the owned page. */
static int flush(uint16_t address) {
    (void)address;if(!s_capture || !cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int offset=0,had_packet=0;
    P(0x19,0);
    for(;;) {
        int hi=R(0x780+offset);
        if(hi&128){r.a=(uint8_t)hi;r.x=(uint8_t)offset;if(had_packet)r.y=255;r.p=(uint8_t)((r.p&~130)|(hi&128)|(hi?0:2));cyc_mod_set_regs(&r);return 1;}
        int at=(hi<<8)|R(0x781+offset),count=R(0x782+offset)+1;
        if(at<0x2000 || at>=0x3000 || count>128 || offset+count+3>256){s_capture_ok=0;return 1;}
        for(int i=0;i<count;i++)s_capture->tiles[(at+i)&1023]=R(0x783+offset+i);
        had_packet=1;offset=(offset+count+3)&255;if(!offset){r.x=0;r.y=255;r.a=R(0x87F);r.p=(uint8_t)((r.p&~130)|2);cyc_mod_set_regs(&r);return 1;}
    }
}
static uint8_t pattern(const MenuPage *p,int address) {
    int bank;address&=8191;
    if(address<0x800)bank=(p->chr[0]&254)+(address>>10);
    else if(address<0x1000)bank=(p->chr[1]&254)+((address-0x800)>>10);
    else bank=p->chr[2+((address-0x1000)>>10)];
    return mm5_cart_chr(bank,address);
}
void mm5_menu_reset(void) {memset(&S,0,sizeof S);}
int mm5_menu_capture(void) {
    if(!mm5_coop_independent() || !mm5_coop_menu_active() || R(0x26)!=16 || !R(0x59))return 1;
    for(int seat=0;seat<2;seat++) {
        MenuPage *old=&S.page[seat],next={0};int n=0;
        for(int i=0;i<13;i++)next.key[n++]=(uint8_t)mm5_coop_read(seat,(uint16_t)(0xB0+i));
        next.cursor=(uint8_t)mm5_coop_menu_cursor(seat);next.key[n++]=next.cursor;
        next.key[n++]=R(0xBD);next.key[n++]=R(0xBE);next.key[n++]=R(0xBF);next.key[n++]=R(0x6D);
        for(int i=0;i<32;i++)next.key[n++]=R(0x600+i);
        next.key[n++]=R(0x2C);next.key[n++]=R(0xFF);
        if(old->valid && !memcmp(next.key,old->key,sizeof next.key))continue;
        if(!cyc_mod_isolate_begin())return 0;
        s_capture=&next;s_capture_ok=1;nes_mod_set_function_hook_enabled("mm5.menu.flush",1);cyc_mod_allow_isolated_hooks(true);
        for(int i=0;i<13;i++)P(0xB0+i,next.key[i]);P(0x50,next.cursor);P(0x26,16);P(0x27,16);P(0x23,0);P(0x59,1);
        P(0x10,mm5_cart_prg(1,0x8522+R(0x2C)));P(0xF6,8);
        CycModRegs r;cyc_mod_regs(&r);r.a=1;int ok=cyc_mod_call(0xFF3D,&r);
        if(ok)ok=cyc_mod_call(0xDAFC,&r);
        const uint16_t packets[]={0x8403,0x8488,0x8437,0x84E9,0x8375,0xC298};
        for(int i=0;ok && i<sizeof packets/sizeof *packets;i++){r.x=0;ok=cyc_mod_call(packets[i],&r);}
        for(int i=0;i<256;i+=4)P(0x200+i,248);
        if(ok)ok=cyc_mod_call(0x82FE,&r);
        if(ok) {
            for(int i=0;i<60;i++)next.oam[i]=R(0x200+i);
            for(int i=0;i<6;i++)next.chr[i]=R(0xEA+i);
            for(int i=0;i<32;i++)next.palette[i]=R(0x600+i)&63;
            next.bg_table=cyc_render_line_bg_table(120);next.sprite_table=cyc_render_line_sprite_table(120);
            next.character=(uint8_t)mm5_coop_character_at(seat);next.valid=1;
        }
        nes_mod_set_function_hook_enabled("mm5.menu.flush",0);cyc_mod_allow_isolated_hooks(false);s_capture=NULL;
        cyc_mod_isolate_end();if(!ok || !s_capture_ok)return 0;*old=next;
    }
    return 1;
}
static void scene(uint32_t *out,int seat) {
    const MenuPage *p=&S.page[seat];
    for(int y=0;y<240;y++)for(int x=0;x<256;x++) {
        int tile=p->tiles[(y/8)*32+x/8],attribute=p->tiles[960+(y/32)*8+x/32],shift=((y/16)&1)*4+((x/16)&1)*2;
        int pal=(attribute>>shift)&3,a=p->bg_table+tile*16+(y&7),bit=7-(x&7);
        int mask=cyc_render_line_mask(y),v=0;
        if((mask&8) && (x>=8 || (mask&2)))v=((pattern(p,a)>>bit)&1)|(((pattern(p,a+8)>>bit)&1)<<1);
        out[y*256+x]=cyc_render_color_value(p->palette[v?pal*4+v:0]);
    }
    for(int slot=14;slot>=0;slot--) {
        const uint8_t *o=p->oam+slot*4;int top=o[0]+1;
        for(int y=0;y<8 && top+y<240;y++)for(int x=0;x<8 && o[3]+x<256;x++) {
            int row=(o[2]&128)?7-y:y,bit=(o[2]&64)?x:7-x,a=p->sprite_table+o[1]*16+row;
            int v=((pattern(p,a)>>bit)&1)|(((pattern(p,a+8)>>bit)&1)<<1);
            int mask=cyc_render_line_mask(top+y);
            if(v && (mask&16) && (o[3]+x>=8 || (mask&4)))out[(top+y)*256+o[3]+x]=cyc_render_color_value(p->palette[16+(o[2]&3)*4+v]);
        }
    }
    mm5_view_menu_character(out,256,0,p->cursor,p->character,p->palette+16);
}
int mm5_menu_draw(uint32_t *out,int width,int height) {
    int mode=mm5_coop_view_mode();
    if(!mode || !mm5_coop_menu_active() || R(0x26)!=16 || !R(0x59) || height!=240 || width<256 || !S.page[0].valid || !S.page[1].valid)return 0;
    if(mode==1) {
        scene(s_panel[0],0);scene(s_panel[1],1);int half=width/2;
        for(int y=0;y<240;y++)for(int x=0;x<width;x++){int seat=x>=half,local=x-(seat?half:0),span=seat?width-half:half;out[y*width+x]=s_panel[seat][y*256+local*256/span];}
    } else {
        int seat=mode==3;scene(s_panel[seat],seat);for(int y=0;y<240;y++)for(int x=0;x<width;x++)out[y*width+x]=s_panel[seat][y*256+x*256/width];
    }
    cyc_render_set_native_origin(0);return 1;
}
static int get(uint8_t *out,int cap) {if(cap<sizeof S)return -1;memcpy(S.magic,"MM01",4);memcpy(out,&S,sizeof S);return sizeof S;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !mm5_coop_independent();if(!in || len!=sizeof S || memcmp(in,"MM01",4))return 0;
    MenuState next;memcpy(&next,in,sizeof next);
    for(int seat=0;seat<2;seat++) {
        const MenuPage *p=&next.page[seat];if(p->valid>1 || p->cursor>15 || p->character>1 || (p->bg_table!=0 && p->bg_table!=4096) || (p->sprite_table!=0 && p->sprite_table!=4096))return 0;
        for(int i=0;i<32;i++)if(p->palette[i]>63)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else mm5_menu_reset();return 1;}
void mm5_menu_register(void) {
    nes_mod_register_function_entry_plugin("mm5.menu.flush",0xC298,flush);
    nes_mod_register_savestate_hook("mm5.menu",get,set);nes_mod_register_savestate_validator("mm5.menu",valid);
}
