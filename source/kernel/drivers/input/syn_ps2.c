#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/http.h"
#include "include/nvx/drivers/input/ps2_mouse.h"
#include "include/nvx/drivers/input/tp_gesture.h"
#include "include/nvx/drivers/input/input.h"

int g_syn_ready=0; u32 g_syn_pkts=0, g_syn_bad=0, g_syn_id=0, g_syn_caps=0;
static volatile u8 g_syn_ring[64][6]; static volatile int g_syn_rh, g_syn_rt; static u8 g_syn_cur[6]; static int g_syn_ci; static tpeng g_syn_eng; static int g_syn_btn;
static int syn_resp(u8* out){ if(!mcmd(0xE9))return 0; for(int i=0;i<3;i++){ if(!pw_read())return 0; out[i]=inb(0x60); } return 1; }
static int syn_slice(u8 c){ if(!mcmd(0xE6))return 0; for(int i=6;i>=0;i-=2){ if(!mcmd(0xE8)||!mcmd((u8)((c>>i)&3)))return 0; } return 1; }
static int syn_query(u8 c,u8* out){ if(!syn_slice(c))return 0; return syn_resp(out); }
int syn_detect(void){
    u8 id[3]; if(!mcmd(0xF6))return 0; if(!syn_query(0x00,id))return 0; if(id[1]!=0x47)return 0;
    g_syn_id=((u32)id[0]<<16)|((u32)id[1]<<8)|id[2];
    u8 cp[3]; if(syn_query(0x02,cp))g_syn_caps=((u32)cp[0]<<16)|((u32)cp[1]<<8)|cp[2];
    if(!syn_slice(0xC1)||!mcmd(0xF3)||!mcmd(0x14)){ mcmd(0xF6); return 0; }
    tpe_init(&g_syn_eng,1472,5472,1408,4448,47000u,32000u);
    g_syn_rh=g_syn_rt=0; g_syn_ci=0; g_syn_ready=1; klog("touchpad: Synaptics PS/2 touchpad in absolute mode"); return 1; }
void syn_irq_byte(u8 v){
    if(g_syn_ci==0&&(v&0xC8)!=0x80){ g_syn_bad++; return; }
    if(g_syn_ci==3&&(v&0xC8)!=0xC0){ g_syn_bad++; g_syn_ci=0; return; }
    g_syn_cur[g_syn_ci++]=v; if(g_syn_ci<6)return; g_syn_ci=0;
    int nt=(g_syn_rt+1)&63; if(nt==g_syn_rh)return;
    for(int i=0;i<6;i++)g_syn_ring[g_syn_rt][i]=g_syn_cur[i]; g_syn_rt=nt; }
static void syn_packet(const u8* p){
    g_syn_pkts++;
    int w=((p[0]&0x30)>>2)|((p[0]&0x04)>>1)|((p[3]&0x04)>>2);
    if(w==3){ int dx=(int)p[4]-((p[1]&0x10)?256:0), dy=(int)p[5]-((p[1]&0x20)?256:0); in_rel(dx,-dy); in_buttons(p[1]&1,(p[1]>>1)&1,(p[1]>>2)&1); return; }
    if(w==2)return;
    int x=((p[3]&0x10)<<8)|((p[1]&0x0F)<<8)|p[4], y=((p[3]&0x20)<<7)|((p[1]&0xF0)<<4)|p[5], z=p[2];
    y=5880-y; int btn=(p[0]&1)|((p[0]&2)?2:0);
    tpcontact cs[3]; int n=0;
    if(z>=25){ if(w==0){ n=2; } else if(w==1){ n=3; } else n=1;
        for(int i=0;i<n;i++){ cs[i].id=i; cs[i].x=x+i*300; cs[i].y=y; } }
    tpout o; tpe_frame(&g_syn_eng,cs,n,btn&1,(u32)(g_ticks*10ULL),&o);
    in_rel(o.dx,o.dy); if(o.btn_changed)in_buttons(o.btn_l,o.btn_r,o.btn_m); in_scroll(o.scroll_v,o.scroll_h); if(o.gesture)in_gesture(o.gesture);
    if((btn&2)&&!(g_syn_btn&2))in_buttons(0,1,0); else if(!(btn&2)&&(g_syn_btn&2))in_buttons(0,0,0);
    g_syn_btn=btn; }
void syn_poll(void){
    if(!g_syn_ready)return;
    { extern int g_lap_tp_off; if(g_lap_tp_off)return; }
    if(g_syn_eng.pulse){ tpout o; tpe_frame(&g_syn_eng,0,0,g_syn_btn&1,(u32)(g_ticks*10ULL),&o); if(o.btn_changed)in_buttons(o.btn_l,o.btn_r,o.btn_m); }
    for(int n=0;n<64&&g_syn_rh!=g_syn_rt;n++){ u8 p[6]; for(int i=0;i<6;i++)p[i]=g_syn_ring[g_syn_rh][i]; g_syn_rh=(g_syn_rh+1)&63; syn_packet(p); } }
