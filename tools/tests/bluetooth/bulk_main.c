#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef struct { int root, depth, hub, hport, speed; u32 route; int tt_slot, tt_port; } usbloc_t;
int g_xcur, cur_slot, xh_wdepth; u32 cur_ep0, cur_dbuf, e0_enq, e0_cyc, xhci_op; volatile int xh_poll_busy; u64 xh_poll_t0; volatile u64 g_ticks;
static u64 vt; u64 hr_ns(void){ return vt+=1000; } void hr_delay_us(u32 us){ vt+=(u64)us*1000; }
static char lastlog[96]; void klog(const char* s){ snprintf(lastlog,sizeof lastlog,"%s",s); }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* hx(char* p,u32 v,int dg){ for(int i=dg-1;i>=0;i--)*p++="0123456789abcdef"[(v>>(i*4))&15]; *p=0; return p; }
int bcmbt_usb_vendor(u16 v){ (void)v; return 0; } int rtlbt_usb_listed(u16 v,u16 p){ (void)v; (void)p; return 0; }
static u32 portsc=1; static u32 XMR(u32 a){ (void)a; return portsc; }
void xhc_select(int c){ g_xcur=c; }
void xh_disable_slot(int s){ (void)s; }
int xh_under(u32 r,int d,u32 R,int dR){ (void)r; (void)d; (void)R; (void)dR; return 0; }
static struct { u32 a, ctl, slot; } cmdlog[64]; static int ncmd;
int xh_cmd_s(u32 a,u32 b,u32 c,u32 ctl,u32 slot,u8* cc,u8* sl){ (void)b; (void)c; if(ncmd<64){ cmdlog[ncmd].a=a; cmdlog[ncmd].ctl=ctl; cmdlog[ncmd].slot=slot; ncmd++; } *cc=1; *sl=(u8)slot; return 1; }
static struct { u8 rt, req; u16 val, idx; } ctllog[16]; static int nctl;
int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,int x){ (void)len; (void)in; (void)x; if(nctl<16){ ctllog[nctl].rt=rt; ctllog[nctl].req=req; ctllog[nctl].val=val; ctllog[nctl].idx=idx; nctl++; } return 1; }
static int dbn[32], pend_dci, pend_cc, pend_res, mode_silent; static u32 pend_ptr;
static int btu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr);
void xh_db(u32 slot,u32 dci){ (void)slot; if(dci<32)dbn[dci]++; }
void xhci_poll_one(void){ if(pend_dci&&!mode_silent){ int d=pend_dci; pend_dci=0; btu_xfer(3,d,(u8)pend_cc,(u32)pend_res,pend_ptr); } }
#include BTUSB_PART
static void btu_iso_done(int ep,u8 cc,u32 res,u32 ptr){ (void)ep; (void)cc; (void)res; (void)ptr; }
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %-74s ok\n",m); else { fails++; printf("  %-74s FAIL (line %d)\n",m,__LINE__); } }while(0)
static int reset_seq(int k,int dci,u32 ring,u32 enq,u32 cyc,int stop){
    if(stop){ if((cmdlog[k].ctl&0xFC00003Fu)!=15||((cmdlog[k].ctl>>6)&0x1F)!=(u32)dci)return 0; k++; }
    if((cmdlog[k].ctl&0x3F)!=14||((cmdlog[k].ctl>>6)&0x1F)!=(u32)dci||cmdlog[k].slot!=3)return 0;
    if((cmdlog[k+1].ctl&0x3F)!=16||((cmdlog[k+1].ctl>>6)&0x1F)!=(u32)dci||cmdlog[k+1].a!=((ring+enq*16)|cyc))return 0;
    return 1; }
static void setup(void){
    static u8* mem; if(!mem)mem=mmap(0,8*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    memset(mem,0,8*4096); memset(&btu,0,sizeof btu); btk_reset();
    btu.mem=(u32)(u64)mem; btu.inctx=btu.mem; btu.devctx=btu.mem+0x1000u; btu.ep0=btu.mem+0x2000u; btu.intring=btu.mem+0x3000u; btu.binring=btu.mem+0x4000u; btu.boutring=btu.mem+0x5000u; btu.dbuf=btu.mem+0x6000u; btu.evbase=btu.mem+0x7000u;
    btu.ok=1; btu.slot=3; btu.ctl=0; btu.int_ep=0x81; btu.bin_ep=0x82; btu.bout_ep=0x02; btu.int_dci=3; btu.bin_dci=5; btu.bout_dci=4; btu.int_mps=16; btu.int_cyc=1; btu.L.depth=1; }
int main(void){
    setup(); u8 pk[12]={0x40,0x20,8,0,4,0,1,0,1,2,3,4}, in[64];
    ncmd=nctl=0; pend_dci=4; pend_cc=1; pend_ptr=btu.boutring; int r=btusb_bulk_out(pk,12,100);
    CHECK(r==12&&dbn[4]==1&&ncmd==0&&btk.oenq==1,"bulk OUT success: one TRB, doorbell on DCI 4, no recovery commands");
    ncmd=nctl=0; pend_dci=4; pend_cc=6; r=btusb_bulk_out(pk,12,100);
    CHECK(r==-1&&btu.nerr==1,"bulk OUT stall (CC 6) returns -1 and counts an error");
    CHECK(ncmd==3&&reset_seq(0,4,btu.boutring,2,1,1),"OUT stall: Stop EP, Reset EP (DCI 4, slot 3), Set TR Dequeue to ring+2*16|DCS=1");
    CHECK(nctl==1&&ctllog[0].rt==0x02&&ctllog[0].req==1&&ctllog[0].val==0&&ctllog[0].idx==0x02,"OUT stall: CLEAR_FEATURE(ENDPOINT_HALT) to endpoint 0x02");
    ncmd=nctl=0; pend_dci=4; pend_cc=1; r=btusb_bulk_out(pk,12,100);
    CHECK(r==12&&!btk.obusy&&btk.oenq==3,"bulk OUT works again after the stall");
    ncmd=nctl=0; mode_silent=1; pend_dci=4; pend_cc=1; r=btusb_bulk_out(pk,12,5); mode_silent=0; pend_dci=0;
    CHECK(r==-1&&!btk.obusy&&!btk.odone&&ncmd==3&&reset_seq(0,4,btu.boutring,4,1,1)&&nctl==1,"bulk OUT timeout (no completion): bounded wait, endpoint stopped/reset, busy cleared");
    ncmd=nctl=0; pend_dci=4; pend_cc=1; r=btusb_bulk_out(pk,12,100);
    CHECK(r==12,"bulk OUT works after the timeout recovery");
    ncmd=nctl=0; pend_dci=5; pend_cc=4; pend_res=0; r=btusb_bulk_in(in,64,100);
    CHECK(r==-1&&ncmd==3&&reset_seq(0,5,btu.binring,1,1,1)&&nctl==1&&ctllog[0].idx==0x82&&!btk.iarm,"bulk IN transaction error (CC 4): reset DCI 5, CLEAR_FEATURE to 0x82, re-arm next call");
    { volatile u8* d=(volatile u8*)(u64)(btu.evbase+0x800u); for(int i=0;i<10;i++)d[i]=(u8)(0x50+i); }
    ncmd=nctl=0; pend_dci=5; pend_cc=13; pend_res=1024-10; r=btusb_bulk_in(in,64,100);
    CHECK(r==10&&in[0]==0x50&&in[9]==0x59&&ncmd==0&&btk.ienq==2,"bulk IN short packet (CC 13) after recovery delivers 10 bytes");
    ncmd=nctl=0; mode_silent=1; pend_dci=0; r=btusb_bulk_in(in,64,3); mode_silent=0;
    CHECK(r==0&&btk.iarm==1&&ncmd==0,"bulk IN with no data: bounded wait returns 0, TRB stays armed (no reset)");
    pend_dci=5; pend_cc=1; pend_res=1024-4; r=btusb_bulk_in(in,64,100);
    CHECK(r==4&&btk.ienq==3,"armed IN TRB completes on the next call without a second TRB");
    for(int i=0;i<BTU_NARM;i++)btu_arm(btu.evbase+(u32)(i*BTU_BUFSZ));
    { volatile u8* d=(volatile u8*)(u64)btu.evbase; d[0]=0x0E; d[1]=4; d[2]=1; d[3]=3; d[4]=0x0C; d[5]=0; } pend_dci=0; btu_xfer(3,3,1,0,btu.intring);
    { u8 e[16]; CHECK(btusb_evt(e,16)==6&&e[0]==0x0E&&e[4]==0x0C&&btu.int_enq==BTU_NARM+1,"event endpoint: completed TRB queues the HCI event and is re-armed"); }
    ncmd=nctl=0; u32 e0=btu.int_enq; btu_xfer(3,3,6,0,btu.intring+16); CHECK(btu.halt==1,"event endpoint stall marks the endpoint halted, no re-arm");
    btu_recover(); CHECK(ncmd==0,"event endpoint recovery waits 50 ms before resetting");
    vt+=60000000ULL; btu_recover();
    CHECK(!btu.halt&&ncmd==2&&reset_seq(0,3,btu.intring,e0,1,0)&&nctl==1&&ctllog[0].idx==0x81&&btu.int_enq==e0+BTU_NARM,"event endpoint recovered: Reset EP, Set TR Dequeue, CLEAR_FEATURE 0x81, 8 TRBs re-armed");
    portsc=0; btu.L.depth=0; btu_recover(); CHECK(!btu.ok&&!strcmp(lastlog,"bluetooth: adapter removed"),"root port without connect status: adapter released");
    CHECK(btusb_bulk_out(pk,12,10)==-1&&btusb_bulk_in(in,64,10)==-1,"bulk calls fail fast once the adapter is gone");
    printf("%d of %d checks passed\n",checks-fails,checks);
    if(!fails)printf("BT BULK RECOVERY TEST: ALL PASS\n");
    return fails!=0; }
