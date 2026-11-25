#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#include XHR_SRC
static int ok=1; static void chk(const char* w,int c){ printf("  %-78s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
static volatile u32 cap[2]; static int fw_after, nwait;
static void fw_wait(void){ nwait++; if(fw_after>=0&&nwait>=fw_after)cap[0]&=~(1u<<16); }
int main(void){
  printf("== route strings and hub depth ==\n");
  chk("hub on a root port, child on port 3 -> route 0x3", xhr_route(0,0,3)==0x3);
  chk("tier 3 child on port 7 below route 0x21 -> 0x721", xhr_route(0x21,2,7)==0x721);
  chk("bits above the parent's tier are ignored", xhr_route(0xFFF21,2,1)==0x121);
  chk("port numbers above 15 are clamped to 15", xhr_route(0x1111,4,20)==0xF1111);
  chk("a sixth tier is refused (hub depth limit)", xhr_route(0x11111,5,1)==0xFFFFFFFFu&&xhr_route(0,-1,1)==0xFFFFFFFFu&&xhr_route(0,0,0)==0xFFFFFFFFu);
  printf("== hub descriptors ==\n");
  xhr_hub_t h; memset(&h,0,sizeof h);
  u8 d2[9]={9,0x29,4,0x60,0,50,0,0,0xFF};
  chk("USB 2 hub: 4 ports, think time 3, power-on delay 100 ms", xhr_hubdesc(d2,9,0,&h)&&h.nports==4&&h.ttt==3&&h.p2g==100&&!h.ss);
  u8 d3[12]={12,0x2A,7,0x60,0,10,4,0x34,0x12,0,0,0};
  chk("SuperSpeed hub: 7 ports, no think time, header latency and hub delay read", xhr_hubdesc(d3,12,1,&h)&&h.nports==7&&h.ttt==0&&h.hdrlat==4&&h.delay==0x1234&&h.ss);
  u8 dn[9]={9,0x29,0,0,0,0,0,0,0}; u8 db[9]={9,0x02,4,0,0,0,0,0,0}; u8 dbig[9]={9,0x29,40,0,0,0,0,0,0};
  chk("zero ports, wrong type, short read and NULL are refused", !xhr_hubdesc(dn,9,0,&h)&&!xhr_hubdesc(db,9,0,&h)&&!xhr_hubdesc(d2,6,0,&h)&&!xhr_hubdesc(0,9,0,&h));
  chk("more than 15 ports clamped to what a route nibble can address", xhr_hubdesc(dbig,9,0,&h)&&h.nports==15);
  printf("== transaction translators ==\n");
  int ts=-1,tp=-1;
  xhr_tt(3,5,0,0,1,3,&ts,&tp); chk("full-speed device on a high-speed hub: TT = hub slot 5 / port 3", ts==5&&tp==3);
  xhr_tt(1,9,5,3,2,2,&ts,&tp); chk("low-speed device behind a full-speed hub inherits the upstream TT", ts==5&&tp==3);
  xhr_tt(3,5,0,0,3,1,&ts,&tp); chk("high-speed device: no TT", ts==0&&tp==0);
  xhr_tt(4,5,0,0,4,1,&ts,&tp); chk("SuperSpeed device: no TT", ts==0&&tp==0);
  static const u8 MT[]={9,2,41,0,1,1,0,0xE0,0, 9,4,0,0,1,9,0,1,0, 7,5,0x81,3,1,0,12, 9,4,0,1,1,9,0,2,0, 7,5,0x81,3,1,0,12};
  static const u8 ST[]={9,2,25,0,1,1,0,0xE0,0, 9,4,0,0,1,9,0,0,0, 7,5,0x81,3,1,0,12};
  int ifc=-1; chk("multi-TT alternate setting found (interface 0, alt 1)", xhr_mtt_alt(MT,sizeof MT,&ifc)==1&&ifc==0);
  chk("single-TT hub and truncated descriptor: no multi-TT", xhr_mtt_alt(ST,sizeof ST,0)==-1&&xhr_mtt_alt(MT,30,0)==-1&&xhr_mtt_alt(MT,0,0)==-1);
  printf("== error recovery policy ==\n");
  chk("success and short packet need nothing", xhr_cc_action(1,6,0)==XHR_OK&&xhr_cc_action(13,7,0)==XHR_OK);
  chk("stall: Reset Endpoint + Set TR Dequeue", xhr_cc_action(6,4,0)==XHR_RESET_EP&&xhr_cc_action(6,6,9)==XHR_RESET_EP);
  chk("babble: reset endpoint twice, then reset the device", xhr_cc_action(3,7,0)==XHR_RESET_EP&&xhr_cc_action(3,7,1)==XHR_RESET_EP&&xhr_cc_action(3,7,2)==XHR_RESET_DEV);
  chk("transaction error: 3 soft retries, then hard reset, then device reset", xhr_cc_action(4,6,0)==XHR_SOFT&&xhr_cc_action(4,6,2)==XHR_SOFT&&xhr_cc_action(4,6,3)==XHR_RESET_EP&&xhr_cc_action(4,6,5)==XHR_RESET_DEV);
  chk("split transaction error handled like a transaction error", xhr_cc_action(36,7,0)==XHR_SOFT);
  chk("isochronous: missed service, transaction error, underrun just skip the TD", xhr_cc_action(23,5,0)==XHR_SKIP&&xhr_cc_action(4,1,0)==XHR_SKIP&&xhr_cc_action(14,1,0)==XHR_SKIP&&xhr_cc_action(15,5,0)==XHR_SKIP);
  chk("stopped endpoints skip; context state / TRB / slot errors fail", xhr_cc_action(26,6,0)==XHR_SKIP&&xhr_cc_action(19,6,0)==XHR_FAIL&&xhr_cc_action(5,6,0)==XHR_FAIL&&xhr_cc_action(11,6,0)==XHR_FAIL);
  chk("unknown codes: bounded retries, then fail", xhr_cc_action(99,6,0)==XHR_RESET_EP&&xhr_cc_action(99,6,3)==XHR_FAIL);
  printf("== hot-plug storms ==\n");
  xhr_storm_t s; memset(&s,0,sizeof s); u32 t=1000; int held=0;
  for(int i=0;i<6;i++)held|=xhr_storm(&s,t+i);
  chk("6 changes in the window are accepted", !held&&xhr_storm_n==0);
  chk("the 7th is held and the port stays held", xhr_storm(&s,t+7)==1&&xhr_storm(&s,t+100)==1&&xhr_storm_n==1);
  chk("after the hold the port is accepted again", xhr_storm(&s,t+7+XHR_STORM_HOLD+1)==0);
  memset(&s,0,sizeof s); s.t0=0xFFFFFF00u; held=0; for(int i=0;i<12;i++)held|=xhr_storm(&s,0xFFFFFF00u+(u32)i*(XHR_STORM_WIN/4+1));
  chk("slow changes never trip it, also across the tick counter wrap", !held);
  memset(&s,0,sizeof s); for(int i=0;i<7;i++)xhr_storm(&s,0xFFFFFFF0u+(u32)i); chk("a hold set just before the wrap still expires", xhr_storm(&s,0x10u)==1&&xhr_storm(&s,0xFFFFFFF7u+XHR_STORM_HOLD+2)==0);
  printf("== link state, interrupters, event ring ==\n");
  chk("SS.Inactive and Compliance need a warm reset, U0/RxDetect do not", xhr_link_bad(6u<<5)&&xhr_link_bad((10u<<5)|1)&&!xhr_link_bad(0)&&!xhr_link_bad(5u<<5)&&!xhr_link_bad(0x200|1));
  chk("MaxIntrs read from HCSPARAMS1 (8 and 1024), never 0", xhr_max_intrs(0x20000820u)==8&&xhr_max_intrs(0x40040040u)==1024&&xhr_max_intrs(0x20000040u)==1);
  chk("interrupter target outside the controller's range falls back to 0", xhr_intr_target(3,8)==3&&xhr_intr_target(8,8)==0&&xhr_intr_target(-1,8)==0);
  chk("interrupter register set address (runtime base + 0x20 + 32*i)", xhr_ir_base(0x1000,0x2005,2)==0x1000+0x2000+0x20+0x40);
  u32 f0=xhr_evfull_n, l0=xhr_evlost_n;
  chk("Event Ring Full and Event Lost host-controller events are counted", xhr_event_note(37,21)==1&&xhr_event_note(37,32)==2&&xhr_event_note(37,1)==0&&xhr_event_note(32,21)==0&&xhr_evfull_n==f0+1&&xhr_evlost_n==l0+1);
  printf("== BIOS handoff (USBLEGSUP) ==\n");
  cap[0]=0x00010001u|(0x10u<<8); cap[1]=0x0000E011u|0xE0000000u; fw_after=10; nwait=0;
  chk("firmware releases after 10 polls: handed over, OS owned", xhr_handoff(cap,fw_wait,1000)==1&&nwait==10&&(cap[0]&(1u<<24))&&!(cap[0]&(1u<<16)));
  chk("SMI enables cleared, SMI status bits written back to clear them", (cap[1]&0x0000E011u)==0&&(cap[1]&0xE0000000u)==0xE0000000u);
  cap[0]=0x00010001u; cap[1]=0; fw_after=-1; nwait=0;
  chk("firmware that never lets go: bounded wait, BIOS owned cleared, OS takes it", xhr_handoff(cap,fw_wait,500)==0&&nwait==500&&!(cap[0]&(1u<<16))&&(cap[0]&(1u<<24)));
  cap[0]=0x00000001u; cap[1]=0; fw_after=-1; nwait=0;
  chk("no firmware owner: no waiting at all", xhr_handoff(cap,fw_wait,500)==1&&nwait==0);
  printf(ok?"ALL PASS\n":"FAILURES\n"); return ok?0:1; }
