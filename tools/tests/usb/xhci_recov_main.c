#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define XHC_MAX 4
#define XHID_MAX 10
static volatile u64 g_ticks=1000; static int g_xcur; static int cur_slot; static u32 cur_ep0, cur_dbuf, e0_enq, e0_cyc=1;
static int xhid_depth[XHID_MAX], xhid_ctl[XHID_MAX], xhid_port[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_used[XHID_MAX], xhid_n;
static u8 xhid_own[XHID_MAX], xhid_recpend[XHID_MAX]; static u32 xhid_e0enq[XHID_MAX], xhid_e0cyc[XHID_MAX]; static volatile int xh_poll_busy;
static int msd_slot=3, msd_depth;
#define MSD_EP0RING 0xD2000u
#define MSD_DATABUF 0xD6000u
#define XHID_EP0RING(d) (0x100000u+(u32)(d)*0x10000u+0x2000u)
#define XHID_DATABUF(d) (0x100000u+(u32)(d)*0x10000u+0x4000u)
int msd_port=2;
static u64 xhci_op;
static u32 portsc[8]; static int bounce_in_delay, unplug_in_delay, ndelay, nklog, npoll;
#define XMR(a) portsc[((a)-0x400)/0x10]
#define XMW(a,v) sim_portw((a),(v))
static void sim_portw(u64 a,u32 v){ u32* r=&portsc[(a-0x400)/0x10]; *r=(*r&1u)|(*r&0x00FE0000u&~v)|(v&~0x00FE0001u); }
static void klog(const char* m){ (void)m; nklog++; }
static void xh_delay_ms(int ms){ ndelay+=ms; if(bounce_in_delay)portsc[1]|=1u<<17; if(unplug_in_delay)portsc[1]&=~1u; }
static void xhci_poll_one(void){ npoll++; }
static int sst[2][32], dev_halt[256], host_tog[32], dev_tog[32], lost, ncmd[32], nclear, clear_slot, clear_ep, clear_ring, nbulk, lastdeq;
static int script[16], sn, si;
#define XRC_EPSTATE(s,d) ((u32)sst[(s)==msd_slot?0:1][d])
static int sim_ep(int slot){ return slot==msd_slot?0:1; }
static int xh_cmd_s(u32 p0,u32 p1,u32 p2,u32 type,u32 slotid,u8* cc,u8* sl){ (void)p1; (void)p2; (void)sl;
  int ty=(int)(type&0x3F), dci=(int)((type>>6)&0x1F), e=sim_ep((int)slotid); ncmd[ty]++;
  if(ty==14){ if(sst[e][dci]!=2){ *cc=19; return 0; } sst[e][dci]=3; host_tog[dci]=0; *cc=1; return 1; }
  if(ty==15){ if(sst[e][dci]!=1){ *cc=19; return 0; } sst[e][dci]=3; *cc=1; return 1; }
  if(ty==16){ if(sst[e][dci]!=3){ *cc=19; return 0; } lastdeq=(int)p0; *cc=1; return 1; }
  *cc=5; return 0; }
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){ (void)val; (void)len; (void)in; (void)data;
  e0_enq+=2; if(rt==2&&req==1){ nclear++; clear_slot=cur_slot; clear_ep=idx; clear_ring=(int)cur_ep0; dev_halt[idx]=0; dev_tog[idx&15]=0; } return 1; }
static int xh_bulk(int dci,u32 ring,u32* enq,u32* cyc,u32 buf,int len){ (void)ring; (void)cyc; (void)buf; (void)len; nbulk++; (*enq)++;
  int e=0, ep=(dci&1)?0x80|(dci>>1):dci>>1; if(sst[e][dci]==3)sst[e][dci]=1; if(sst[e][dci]!=1)return -1;
  int r=si<sn?script[si++]:1;
  if(r==1&&dev_halt[ep])r=6;
  if(r==1){ if(host_tog[dci]!=dev_tog[dci>>1])lost++; host_tog[dci]^=1; dev_tog[dci>>1]^=1; return 1; }
  if(r<0)return -1;
  sst[e][dci]=2; if(r==6)dev_halt[ep]=1; return r; }
#include XHR_SRC
#include XRC_SRC
static int ok=1; static void chk(const char* w,int c){ printf("  %-82s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
static void reset(void){ memset(sst,0,sizeof sst); memset(dev_halt,0,sizeof dev_halt); memset(host_tog,0,sizeof host_tog); memset(dev_tog,0,sizeof dev_tog); memset(ncmd,0,sizeof ncmd);
  for(int d=0;d<32;d++){ sst[0][d]=1; sst[1][d]=1; } lost=nclear=nbulk=si=sn=0; memset(xrc_rq,0,sizeof xrc_rq); memset(xrc_rw,0,sizeof xrc_rw); memset(xrc_msd_w,0,sizeof xrc_msd_w); memset(xrc_hid_w,0,sizeof xrc_hid_w); g_ticks+=100000; }
static void sc(int a,int b,int c,int d){ int v[4]={a,b,c,d}; sn=0; si=0; for(int i=0;i<4&&v[i];i++)script[sn++]=v[i]; }
int main(void){
  u32 enq=0,cyc=1;
  printf("== bulk endpoint (mass storage) ==\n");
  reset(); sc(6,0,0,0); int r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2);
  chk("CSW stall: Reset Endpoint + Set TR Dequeue + CLEAR_FEATURE(HALT) to 0x81, retried and good", r==1&&ncmd[14]==1&&ncmd[16]==1&&nclear==1&&clear_ep==0x81&&clear_slot==3&&nbulk==2);
  chk("Set TR Dequeue points past the failed TRB with the ring's cycle bit", lastdeq==(int)((0xD3000u+1*16u)|1u));
  chk("host and device data toggles agree afterwards (nothing silently lost)", lost==0&&xrc_retry_n==1);
  chk("the clear went over the stick's own EP0 ring, current device state restored", clear_ring==(int)MSD_EP0RING&&cur_ep0==0);
  reset(); sc(6,0,0,0); r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD6000u,512,1);
  chk("data-phase stall: halt cleared, no retry, caller goes on to the CSW (Bulk-Only rule)", r==6&&nbulk==1&&nclear==1&&dev_halt[0x81]==0);
  r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2); chk("... and the CSW read then succeeds", r==1&&lost==0);
  reset(); sc(4,4,0,0); r=xrc_msd_bulk(2,0xD4000u,&enq,&cyc,0xD5000u,31,0);
  chk("two transaction errors on bulk OUT: two resets, third try good, clear sent to 0x01", r==1&&nbulk==3&&ncmd[14]==2&&clear_ep==0x01&&lost==0);
  reset(); sc(3,3,3,3); u32 rn0=xrc_rq[0]; r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2);
  chk("persistent babble: bounded (3 tries), then port reset + re-enumeration requested", r==-1&&nbulk==3&&!rn0&&(xrc_rq[0]>>2&1u));
  reset(); sc(-1,0,0,0); r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2);
  chk("timeout: endpoint still running -> Stop Endpoint + Set TR Dequeue cancel the TRB, retried", r==1&&ncmd[14]==1&&ncmd[15]==1&&ncmd[16]==1&&nbulk==2&&!xrc_rq[0]);
  reset(); sc(-1,-1,0,0); r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2);
  chk("device stops answering (two timeouts): port reset + re-enumeration requested", r==-1&&nbulk==2&&(xrc_rq[0]>>2&1u));
  reset(); msd_depth=1; sc(-1,-1,0,0); r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2); msd_depth=0;
  chk("stick behind a hub: no root-port reset is requested", r==-1&&!xrc_rq[0]);
  reset(); sc(5,0,0,0); r=xrc_msd_bulk(3,0xD3000u,&enq,&cyc,0xD5100u,13,2);
  chk("TRB error (driver bug) is not retried", r==-1&&nbulk==1&&ncmd[14]==0);
  printf("== re-enumeration limits ==\n");
  reset(); int a=xrc_reenum_req(1,4); xrc_rq[1]=0; int b=xrc_reenum_req(1,4); xrc_rq[1]=0; int c=xrc_reenum_req(1,4); xrc_rq[1]=0; int d=xrc_reenum_req(1,4);
  chk("three re-enumerations of one port per 30 s, the fourth is refused", a&&b&&c&&!d&&!xrc_rq[1]);
  g_ticks+=XRC_REENUM_WIN+1; chk("after the window it is allowed again", xrc_reenum_req(1,4)&&(xrc_rq[1]>>4&1u));
  chk("bad controller/port numbers are ignored", !xrc_reenum_req(9,1)&&!xrc_reenum_req(0,40)&&!xrc_reenum_req(-1,0));
  printf("== root port: re-enumeration and 100 ms connect debounce ==\n");
  reset(); g_xcur=0; portsc[1]=1u|(1u<<17); xrc_rq[0]=1u<<1; u32 rb=xrc_reenum_n;
  chk("pending re-enumeration on an attached port reports it gone (drivers detach)", xrc_root_conn(1,portsc[1],1)==0&&(xrc_rq[0]&2u));
  ndelay=0; r=xrc_root_conn(1,portsc[1],0);
  chk("next poll: request consumed, debounced, reported present (drivers re-attach via port reset)", r==1&&!xrc_rq[0]&&xrc_reenum_n==rb+1&&ndelay==100);
  portsc[1]=1u|(1u<<17); ndelay=0; r=xrc_root_conn(1,portsc[1],0);
  chk("new connection: CSC cleared, 100 ms stable, accepted", r==1&&ndelay==100&&!(portsc[1]&(1u<<17)));
  u32 bb=xrc_bounce_n; bounce_in_delay=1; r=xrc_root_conn(1,portsc[1],0); bounce_in_delay=0;
  chk("contact bounce during the 100 ms (CSC set again): not accepted yet", r==0&&xrc_bounce_n==bb+1);
  portsc[1]&=~(1u<<17); unplug_in_delay=1; r=xrc_root_conn(1,portsc[1],0); unplug_in_delay=0;
  chk("unplugged during the debounce: not accepted", r==0); portsc[1]=1;
  ndelay=0; chk("already attached port: no delay, stays present", xrc_root_conn(1,portsc[1],1)==1&&ndelay==0);
  chk("empty port: absent, no delay", xrc_root_conn(2,0,0)==0&&ndelay==0);
  xrc_rq[0]=1u<<3; chk("re-enumeration request for a port that was unplugged meanwhile is dropped", xrc_root_conn(3,0,1)==0&&!xrc_rq[0]);
  printf("== interrupt endpoint (HID) ==\n");
  reset(); xhid_n=2; xhid_used[1]=1; xhid_slot[1]=7; xhid_dci[1]=3; xhid_own[1]=1; xhid_port[1]=5; xhid_ctl[1]=0; xhid_depth[1]=0; xhid_e0enq[1]=40; xhid_e0cyc[1]=0;
  cur_slot=3; cur_ep0=MSD_EP0RING; e0_enq=9; e0_cyc=1;
  xrc_hid_note(1,6); xrc_hid_cleared(1);
  chk("stall on interrupt IN: CLEAR_FEATURE(HALT) to 0x81 on the HID's slot after the endpoint reset", nclear==1&&clear_ep==0x81&&clear_slot==7&&clear_ring==(int)XHID_EP0RING(1));
  chk("HID's saved EP0 ring position advanced, current device's EP0 state untouched", xhid_e0enq[1]==42&&xhid_e0cyc[1]==0&&cur_slot==3&&cur_ep0==MSD_EP0RING&&e0_enq==9);
  xrc_hid_cleared(1); chk("no error noted: no extra control transfer", nclear==1);
  cur_ep0=XHID_EP0RING(1); e0_enq=50; xrc_hid_note(1,4); xrc_hid_cleared(1);
  chk("HID is the current device: live EP0 position used and kept", e0_enq==52&&xhid_e0enq[1]==52);
  reset(); for(int i=0;i<XRC_EP_MAX;i++)xrc_hid_note(1,6);
  chk("up to 8 halts per 10 s: endpoint recovery only", !xrc_rq[0]);
  xrc_hid_note(1,6); chk("9th halt in the window: port reset + re-enumeration requested", (xrc_rq[0]>>5)&1u);
  reset(); xrc_hid_note(1,3); xrc_hid_note(1,3); chk("babble: endpoint reset (re-enumeration only via the window)", !xrc_rq[0]);
  reset(); xhid_depth[1]=1; for(int i=0;i<12;i++)xrc_hid_note(1,6); xhid_depth[1]=0; chk("HID behind a hub: no root-port reset", !xrc_rq[0]);
  printf("== event ring full ==\n");
  xhid_used[0]=1; xhid_ctl[0]=1; xhid_recpend[0]=xhid_recpend[1]=0; npoll=0; xrc_poll(); chk("no overflow: nothing done", npoll==0&&!xhid_recpend[1]);
  xhr_event_note(37,21); xrc_poll();
  chk("Event Ring Full: ring drained, this controller's interrupt endpoints re-armed", npoll==4&&xhid_recpend[1]&&!xhid_recpend[0]&&xrc_evrec_n==1&&!xh_poll_busy);
  npoll=0; xrc_poll(); chk("handled once per overflow", npoll==0);
  xhr_event_note(37,32); xh_poll_busy=1; xrc_poll(); chk("Event Lost while the poller is busy: deferred", npoll==0); xh_poll_busy=0; xrc_poll(); chk("... and recovered on the next poll", npoll==4&&xrc_evrec_n==2);
  printf("== policy ==\n");
  xrc_win_t w; memset(&w,0,sizeof w);
  chk("success / short packet: done", xrc_plan(1,6,0,&w,1)==XRC_DONE&&xrc_plan(13,6,0,&w,1)==XRC_DONE);
  chk("stall / transaction / babble: retry", xrc_plan(6,6,0,&w,1)==XRC_RETRY&&xrc_plan(4,2,0,&w,1)==XRC_RETRY&&xrc_plan(3,7,0,&w,1)==XRC_RETRY);
  chk("retries are bounded", xrc_plan(6,6,XRC_TRIES,&w,1)==XRC_REENUM);
  chk("slot/endpoint not enabled, TRB error: give up", xrc_plan(11,6,0,&w,1)==XRC_GIVEUP&&xrc_plan(5,6,0,&w,1)==XRC_GIVEUP);
  printf(ok?"ALL PASS\n":"FAILURES\n"); return ok?0:1; }
