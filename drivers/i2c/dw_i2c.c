#include "include/nvx/base.h"
#include "include/nvx/drivers/i2c/dw_i2c.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#define DW_SDA_HOLD 0x7C
#define DW_FS_SPKLEN 0xA0
u16 g_dw_ss[3], g_dw_fs[3];
u32 g_dw_clk_mhz=0, g_dw_hold=0;
#define DW_CON 0x00
#define DW_TAR 0x04
#define DW_DATA 0x10
#define DW_SS_HCNT 0x14
#define DW_SS_LCNT 0x18
#define DW_FS_HCNT 0x1C
#define DW_FS_LCNT 0x20
#define DW_INTR_MASK 0x30
#define DW_RAW_INTR 0x34
#define DW_RX_TL 0x38
#define DW_TX_TL 0x3C
#define DW_CLR_INTR 0x40
#define DW_CLR_TX_ABRT 0x54
#define DW_ENABLE 0x6C
#define DW_STATUS 0x70
#define DW_TXFLR 0x74
#define DW_RXFLR 0x78
#define DW_ABRT_SRC 0x80
#define DW_EN_STATUS 0x9C
#define DW_PARAM1 0xF4
#define DW_COMP_TYPE 0xFC
#define DW_TYPE_ID 0x44570140u

static u32 dw_r(dwi2c* c,u32 o){ return *(volatile u32*)(c->base+o); }
static void dw_w(dwi2c* c,u32 o,u32 v){ *(volatile u32*)(c->base+o)=v; }
int dw_present(u64 base){ if(!base||base>=0x800000000000ULL||(base&0xFFF))return 0; if(base>=0x100000000ULL&&!acpi_hi_map(base))return 0; return *(volatile u32*)(base+DW_COMP_TYPE)==DW_TYPE_ID; }
static int dw_enable(dwi2c* c,int on){ dw_w(c,DW_ENABLE,on?1u:0u); for(int t=0;t<100000;t++){ if((dw_r(c,DW_EN_STATUS)&1u)==(on?1u:0u))return 0; io_wait(); } return -1; }
int dw_init(dwi2c* c,u64 base,u32 speed){
    c->base=base; c->ok=0; c->xfers=0; c->errs=0; c->last_abort=0;
    if(!dw_present(base))return -1;
    if(dw_enable(c,0))return -2;
    u32 p1=dw_r(c,DW_PARAM1); c->txd=((p1>>16)&0xFFu)+1u; c->rxd=((p1>>8)&0xFFu)+1u;
    if(p1==0||c->txd<2||c->txd>256)c->txd=8; if(p1==0||c->rxd<2||c->rxd>256)c->rxd=8;
    c->fast=speed>=400000u;
    u32 ck=g_dw_clk_mhz?g_dw_clk_mhz:216u; u32 hold=0;
    if(g_dw_ss[0]&&g_dw_ss[1]){ dw_w(c,DW_SS_HCNT,g_dw_ss[0]); dw_w(c,DW_SS_LCNT,g_dw_ss[1]); if(!c->fast&&g_dw_ss[2])hold=g_dw_ss[2]; }
    else if(!dw_r(c,DW_SS_HCNT)||!dw_r(c,DW_SS_LCNT)){ dw_w(c,DW_SS_HCNT,ck*4u+8u); dw_w(c,DW_SS_LCNT,ck*47u/10u+8u); }
    if(g_dw_fs[0]&&g_dw_fs[1]){ dw_w(c,DW_FS_HCNT,g_dw_fs[0]); dw_w(c,DW_FS_LCNT,g_dw_fs[1]); if(c->fast&&g_dw_fs[2])hold=g_dw_fs[2]; }
    else if(!dw_r(c,DW_FS_HCNT)||!dw_r(c,DW_FS_LCNT)){ dw_w(c,DW_FS_HCNT,ck*6u/10u+8u); dw_w(c,DW_FS_LCNT,ck*13u/10u+8u); }
    if(!hold&&!(dw_r(c,DW_SDA_HOLD)&0xFFFFu))hold=ck*3u/10u;
    if(hold){ dw_w(c,DW_SDA_HOLD,hold&0xFFFFu); } g_dw_hold=dw_r(c,DW_SDA_HOLD)&0xFFFFu;
    dw_w(c,DW_CON,1u|((c->fast?2u:1u)<<1)|(1u<<5)|(1u<<6));
    dw_w(c,DW_INTR_MASK,0); dw_w(c,DW_RX_TL,0); dw_w(c,DW_TX_TL,0);
    (void)dw_r(c,DW_CLR_INTR);
    c->ok=1; return 0; }
int dw_xfer(dwi2c* c,u16 addr,const u8* w,int wn,u8* r,int rn){
    if(!c->ok)return -1; if(wn<0||rn<0||(wn==0&&rn==0))return -1;
    c->xfers++;
    if(dw_enable(c,0)){ c->errs++; return -2; }
    dw_w(c,DW_TAR,addr&0x3FFu);
    (void)dw_r(c,DW_CLR_INTR); (void)dw_r(c,DW_CLR_TX_ABRT);
    if(dw_enable(c,1)){ c->errs++; return -2; }
    int wi=0, rq=0, rd=0;
    for(int t=0;t<400000;t++){
        while(dw_r(c,DW_TXFLR)<c->txd&&(wi<wn||(rq<rn&&rq-rd<(int)c->rxd-1))){
            u32 cmd;
            if(wi<wn){ cmd=w[wi]; if(wi==wn-1&&rn==0)cmd|=1u<<9; wi++; }
            else { cmd=0x100u; if(rq==0&&wn>0)cmd|=1u<<10; if(rq==rn-1)cmd|=1u<<9; rq++; }
            dw_w(c,DW_DATA,cmd); }
        while(rd<rn&&dw_r(c,DW_RXFLR)>0)r[rd++]=(u8)dw_r(c,DW_DATA);
        if(dw_r(c,DW_RAW_INTR)&(1u<<6)){ c->last_abort=dw_r(c,DW_ABRT_SRC); (void)dw_r(c,DW_CLR_TX_ABRT); (void)dw_r(c,DW_CLR_INTR); dw_enable(c,0); c->errs++; return -3; }
        if(wi==wn&&rq==rn&&rd==rn){
            for(int q=0;q<100000;q++){ if(!(dw_r(c,DW_STATUS)&(1u<<5)))break; io_wait(); }
            if(dw_r(c,DW_RAW_INTR)&(1u<<6)){ c->last_abort=dw_r(c,DW_ABRT_SRC); (void)dw_r(c,DW_CLR_TX_ABRT); dw_enable(c,0); c->errs++; return -3; }
            return 0; }
        io_wait(); }
    dw_enable(c,0); c->errs++; return -4; }
