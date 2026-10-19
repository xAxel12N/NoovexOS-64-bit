#include "include/nvx/base.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2_mouse.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"
#include "include/nvx/drivers/input/ps2/elan_ps2.h"
volatile int g_etp_hw; int g_etp_crc, g_etp_maxx, g_etp_maxy, g_etp_xres=31, g_etp_yres=31, g_etp_traces; u32 g_etp_fw; static u8 g_etp_cap[3];
static volatile u8 g_etp_ring[64][6]; static volatile int g_etp_rh, g_etp_rt; static u8 g_etp_cur[6]; static int g_etp_ci; u32 g_etp_bad, g_etp_pkts, g_etp_drop;
static tpeng g_etp_eng; int g_etp_ready;
static struct { int on, x, y; } g_etp_slot[5]; static int g_etp_v3x, g_etp_v3y, g_etp_btn, g_etp_wait; u32 g_etp_frames;
static int etp_resp(u8 c,u8* out,int n){ if(!mcmd(c))return 0; for(int i=0;i<n;i++){ if(!pw_read())return 0; out[i]=inb(0x60); } return 1; }
static int etp_sliced(u8 c,u8* out){ if(!mcmd(0xE6))return 0; for(int i=6;i>=0;i-=2){ if(!mcmd(0xE8)||!mcmd((u8)((c>>i)&3)))return 0; } return etp_resp(0xE9,out,3); }
static int etp_query(u8 c,u8* out){ if(!mcmd(0xF8)||!mcmd(c))return 0; return etp_resp(0xE9,out,3); }
static int etp_write_reg(int hw,u8 reg,u8 val){
    if(!mcmd(0xF8)||!mcmd(0x00)||!mcmd(0xF8)||!mcmd(reg))return 0;
    if(hw==4){ if(!mcmd(0xF8)||!mcmd(0x00))return 0; }
    if(!mcmd(0xF8)||!mcmd(val)||!mcmd(0xE6))return 0; return 1; }
static int etp_sig_ok(const u8* p){ if(p[0]==0)return 0; if(p[1]==0)return 1; if((p[0]&0x0F)>=6&&(p[1]&0xAF)==0x0F&&p[2]<40)return 1;
    static const u8 rates[7]={200,100,80,60,40,20,10}; for(int i=0;i<7;i++) if(p[2]==rates[i])return 0; return 1; }
static int etp_res_upmm(int v){ int u=((v*10)+790)*10/254; return u<1?1:u; }
int etp_detect(void){
    u8 p[3];
    g_etp_hw=0; g_etp_ready=0;
    mcmd(0xF5); mcmd(0xE6); mcmd(0xE6); mcmd(0xE6);
    if(!etp_resp(0xE9,p,3))return 0;
    if(!(p[0]==0x3C&&p[1]==0x03&&(p[2]==0xC8||p[2]==0x00)))return 0;
    if(!etp_sliced(0x01,p)||!etp_sig_ok(p))return 0;
    g_etp_fw=((u32)p[0]<<16)|((u32)p[1]<<8)|p[2];
    int ic=(int)((g_etp_fw>>16)&0x0F), hw=0;
    if(g_etp_fw<0x020030u||g_etp_fw==0x020600u)hw=1; else if(ic==2||ic==4)hw=2; else if(ic==5)hw=3; else if(ic>=6)hw=4;
    g_etp_crc=(g_etp_fw&0x4000u)!=0;
    if(hw<3)return 0;
    if(!etp_query(0x02,g_etp_cap))return 0;
    if(!etp_query(0x00,p))return 0;
    g_etp_maxx=((p[0]&0x0F)<<8)|p[1]; g_etp_maxy=((p[0]&0xF0)<<4)|p[2];
    if(g_etp_maxx<16||g_etp_maxy<16)return 0;
    g_etp_xres=31; g_etp_yres=31;
    if(hw==4){ u8 r[3]; if(etp_query(0x04,r)){ g_etp_xres=etp_res_upmm(r[1]&0x0F); g_etp_yres=etp_res_upmm((r[1]&0xF0)>>4); } g_etp_traces=g_etp_cap[1]; }
    if(!etp_write_reg(hw,hw==4?0x07:0x10,hw==4?0x01:0x0B))return 0;
    tpe_init(&g_etp_eng,0,g_etp_maxx,0,g_etp_maxy,(u32)((long long)g_etp_maxx*1000/g_etp_xres),(u32)((long long)g_etp_maxy*1000/g_etp_yres));
    for(int i=0;i<5;i++)g_etp_slot[i].on=0; g_etp_wait=0; g_etp_ci=0; g_etp_rh=g_etp_rt=0;
    g_etp_hw=hw; g_etp_ready=1; return 1; }
static int etp_kind(const u8* p){
    if(g_etp_hw==4){
        if((p[3]&0x0F)==0x06)return 9;
        if(g_etp_crc){ if((p[3]&0x08)!=0)return -1; } else if((p[0]&0x08)!=0||(p[3]&0x1C)!=0x10)return -1;
        int t=p[3]&3; return t==3?-1:t; }
    if(p[0]==0xC4&&p[1]==0xFF&&p[2]==0xFF&&p[3]==0x02&&p[4]==0xFF&&p[5]==0xFF)return 8;
    if(g_etp_crc){ if((p[3]&0x09)==0x08)return 1; if((p[3]&0x09)==0x09)return 2; return -1; }
    if((p[0]&0x0C)==0x04&&(p[3]&0xCF)==0x02)return 1;
    if((p[0]&0x0C)==0x0C&&(p[3]&0xCE)==0x0C)return 2;
    if((p[3]&0x0F)==0x06)return 9;
    return -1; }
void etp_irq_byte(u8 v){
    if(g_etp_ci==0&&!(v&0x04)&&g_etp_hw==3&&v!=0xC4){ g_etp_bad++; return; }
    g_etp_cur[g_etp_ci++]=v;
    if(g_etp_ci<6)return;
    if(etp_kind(g_etp_cur)<0){ g_etp_bad++; for(int i=0;i<5;i++)g_etp_cur[i]=g_etp_cur[i+1]; g_etp_ci=5; return; }
    int nt=(g_etp_rt+1)&63; if(nt==g_etp_rh){ g_etp_drop++; g_etp_ci=0; return; }
    for(int i=0;i<6;i++)g_etp_ring[g_etp_rt][i]=g_etp_cur[i]; g_etp_rt=nt; g_etp_ci=0; }
static void etp_emit(int btn,int n_override){
    tpcontact cs[5]; int n=0;
    for(int i=0;i<5;i++) if(g_etp_slot[i].on){ cs[n].id=i; cs[n].x=g_etp_slot[i].x; cs[n].y=g_etp_slot[i].y; n++; }
    if(n_override==3&&n==1){ cs[1]=cs[0]; cs[1].id=1; cs[2]=cs[0]; cs[2].id=2; n=3; }
    tpout o; int clickbtn=btn&1; if((btn&3)==2){ clickbtn=0; }
    tpe_frame(&g_etp_eng,cs,n,clickbtn,(u32)(g_ticks*10ULL),&o);
    in_rel(o.dx,o.dy); if(o.btn_changed)in_buttons(o.btn_l,o.btn_r,o.btn_m); in_scroll(o.scroll_v,o.scroll_h); if(o.gesture)in_gesture(o.gesture);
    if((btn&3)==2&&g_etp_btn!=btn)in_buttons(0,1,0); else if((g_etp_btn&3)==2&&(btn&3)!=2)in_buttons(0,0,0);
    g_etp_btn=btn; g_etp_frames++; }
static void etp_trackpoint(const u8* p){ int x=(int)p[4]-(int)(((p[1]^0x80)&0xFF)<<1), y=(int)(((p[2]^0x80)&0xFF)<<1)-(int)p[5]; in_rel(x,-y); in_buttons(p[0]&1,(p[0]>>1)&1,(p[0]>>2)&1); }
static void etp_packet(const u8* p){
    int k=etp_kind(p); g_etp_pkts++;
    if(k==9){ etp_trackpoint(p); return; }
    int btn=p[0]&3;
    if(g_etp_hw==4){
        if(k==0){ int m=p[1]&0x1F; g_etp_wait=0; for(int i=0;i<5;i++){ if(!(m&(1<<i)))g_etp_slot[i].on=0; else if(!g_etp_slot[i].on)g_etp_wait|=1<<i; } if(!g_etp_wait)etp_emit(btn,0); }
        else if(k==1){ int id=((p[3]&0xE0)>>5)-1; if(id<0||id>4)return; g_etp_slot[id].x=((p[1]&0x0F)<<8)|p[2]; g_etp_slot[id].y=g_etp_maxy-(((p[4]&0x0F)<<8)|p[5]); g_etp_slot[id].on=1; g_etp_wait&=~(1<<id); if(!g_etp_wait)etp_emit(btn,0); }
        else if(k==2){ int id=((p[0]&0xE0)>>5)-1, sid=((p[3]&0xE0)>>5)-1, w=(p[0]&0x10)?5:1;
            if(id>=0&&id<5){ g_etp_slot[id].x+=(int)(int8_t)p[1]*w; g_etp_slot[id].y-=(int)(int8_t)p[2]*w; }
            if(sid>=0&&sid<5){ g_etp_slot[sid].x+=(int)(int8_t)p[4]*w; g_etp_slot[sid].y-=(int)(int8_t)p[5]*w; }
            g_etp_wait=0; etp_emit(btn,0); }
        return; }
    if(k==8)return;
    int fingers=(p[0]&0xC0)>>6; int x=((p[1]&0x0F)<<8)|p[2], y=g_etp_maxy-(((p[4]&0x0F)<<8)|p[5]);
    for(int i=0;i<5;i++)g_etp_slot[i].on=0;
    if(fingers==0){ etp_emit(btn,0); return; }
    if(fingers==1||fingers==3){ g_etp_slot[0].on=1; g_etp_slot[0].x=x; g_etp_slot[0].y=y; etp_emit(btn,fingers); return; }
    if(k==1){ g_etp_v3x=x; g_etp_v3y=y; return; }
    g_etp_slot[0].on=1; g_etp_slot[0].x=g_etp_v3x; g_etp_slot[0].y=g_etp_v3y; g_etp_slot[1].on=1; g_etp_slot[1].x=x; g_etp_slot[1].y=y; etp_emit(btn,0); }
void etp_poll(void){
    if(!g_etp_ready)return;
    { extern int g_lap_tp_off; if(g_lap_tp_off)return; }
    if(g_etp_eng.pulse){ tpout o; tpe_frame(&g_etp_eng,0,0,g_etp_btn&1,(u32)(g_ticks*10ULL),&o); if(o.btn_changed)in_buttons(o.btn_l,o.btn_r,o.btn_m); }
    for(int n=0;n<64&&g_etp_rh!=g_etp_rt;n++){ u8 p[6]; for(int i=0;i<6;i++)p[i]=g_etp_ring[g_etp_rh][i]; g_etp_rh=(g_etp_rh+1)&63; etp_packet(p); } }
