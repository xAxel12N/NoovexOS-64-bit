#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define NVX_BASE_H
volatile u64 g_ticks=1000;
static const char* drv_hub="hub"; static int drv_off;
const char* usbreg_drv_at(int ctl,int root,u32 route,int depth){ (void)ctl; (void)root; (void)route; if(drv_off)return 0; return depth==0&&root==1?drv_hub:depth==1&&route==2?"usbhid":0; }
int usbreg_hc_count(void){ return 1; }
int usbreg_hc_ports(int c){ (void)c; return 8; }
#include "drivers/usb/usbreg.c"
static int ok=1, nchk; static void chk(const char* w,int c){ nchk++; printf("  %-78s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
static void mkdd(u8* d,u16 vid,u16 pid,u8 cls,u16 usb){ memset(d,0,18); d[0]=18; d[1]=1; d[2]=(u8)usb; d[3]=(u8)(usb>>8); d[4]=cls; d[7]=64; d[8]=(u8)vid; d[9]=(u8)(vid>>8); d[10]=(u8)pid; d[11]=(u8)(pid>>8); d[12]=0x10; d[13]=0x01; d[17]=1; }
static int mkcfg(u8* c,u8 attr,u8 mp,const u8* ifc,const u8* proto,int n){ int o=9; memset(c,0,9); c[0]=9; c[1]=2; c[4]=(u8)n; c[5]=1; c[7]=attr; c[8]=mp;
    for(int i=0;i<n;i++){ u8* f=c+o; memset(f,0,9); f[0]=9; f[1]=4; f[2]=(u8)i; f[4]=1; f[5]=ifc[i]; f[6]=1; f[7]=proto?proto[i]:0; o+=9; u8* e=c+o; memset(e,0,7); e[0]=7; e[1]=5; e[2]=0x81; e[3]=3; e[4]=8; e[6]=10; o+=7; }
    c[2]=(u8)o; c[3]=(u8)(o>>8); return o; }
static int idx(usbreg_t* r){ return r?(int)(r-g_usbreg):-1; }
int main(void){
  u8 dd[18], cfg[256]; int n; char out[8192];
  printf("== registration and topology ==\n");
  u8 hc[1]={9}; mkdd(dd,0x05e3,0x0610,9,0x0210); n=mkcfg(cfg,0xE0,50,hc,0,1);
  usbreg_t* hub=usbreg_add(0,1,0,0,3,dd,cfg,n); usbreg_strings(hub,"GenesysLogic","USB2.1 Hub",0);
  chk("hub on root port 2 registered with devnum 2, class Hub, 100 mA self-powered", hub&&hub->devnum==2&&hub->cls==9&&hub->ma==100&&(hub->cfg[7]&0x40));
  u8 kc[2]={3,3}, kp[2]={1,2}; mkdd(dd,0x046d,0xc52b,0,0x0200); n=mkcfg(cfg,0xA0,49,kc,kp,2);
  usbreg_t* kb=usbreg_add(0,1,0x2,1,1,dd,cfg,n); usbreg_strings(kb,"Logitech","USB Receiver","1234");
  chk("receiver behind hub port 2: class from interface, 98 mA, full speed", kb&&kb->cls==3&&kb->ma==98&&kb->speed==1&&kb->nif==2);
  chk("receiver's parent is the hub", usbreg_parent(idx(kb))==idx(hub));
  mkdd(dd,0x1a40,0x0101,9,0x0200); n=mkcfg(cfg,0xE0,50,hc,0,1); usbreg_t* hub2=usbreg_add(0,1,0x4,1,3,dd,cfg,n);
  u8 mc[1]={3}, mpr[1]={2}; mkdd(dd,0x1bcf,0x0005,0,0x0110); n=mkcfg(cfg,0x80,50,mc,mpr,1); usbreg_t* ms=usbreg_add(0,1,0x14,2,2,dd,cfg,n);
  chk("mouse two hubs deep: parent is the second hub, low speed", usbreg_parent(idx(ms))==idx(hub2)&&ms->speed==2);
  char bn[32]; usbreg_bus_name(ms,bn,32); chk("Linux bus name 1-2.4.1 for the mouse", !strcmp(bn,"1-2.4.1"));
  usbreg_bus_name(hub,bn,32); chk("Linux bus name 1-2 for the first hub", !strcmp(bn,"1-2"));
  u8 sc[1]={8}; mkdd(dd,0x0781,0x5581,0,0x0320); n=mkcfg(cfg,0x80,112,sc,0,1); usbreg_t* st=usbreg_add(0,3,0,0,4,dd,cfg,n);
  chk("SuperSpeed stick: bMaxPower counted in 8 mA units (896 mA), 5000M", st&&st->ma==896&&!strcmp(usbreg_speed_str(st->speed),"5000M"));
  int dn[5]={hub->devnum,kb->devnum,hub2->devnum,ms->devnum,st->devnum}; int uniq=1; for(int a=0;a<5;a++) for(int b=a+1;b<5;b++) if(dn[a]==dn[b])uniq=0;
  chk("five devices get five different device numbers", uniq&&usbreg_count()==5);
  printf("== lsusb output ==\n");
  usbreg_list(out,sizeof out);
  chk("lsusb line: Bus 001 Device NNN: ID 046d:c52b Logitech USB Receiver", strstr(out,": ID 046d:c52b Logitech USB Receiver")!=0);
  chk("lsusb shows a device without strings by class name", strstr(out,"ID 1bcf:0005 Human Interface Device")!=0);
  chk("lsusb lists the root hub", strstr(out,"Bus 001 Device 001: ID 1d6b:0003")!=0);
  int lines=0; for(char* p=out;*p;p++) if(*p=='\n')lines++; chk("lsusb prints every device plus the root hub (6 lines)", lines==6);
  usbreg_tree(out,sizeof out);
  chk("lsusb -t root line names xhci_hcd with 8 ports", strstr(out,"/:  Bus 001.Port 001: Dev 001, Class=root_hub, Driver=xhci_hcd/8p")!=0);
  char* ph=strstr(out,"    |__ Port 002: Dev 002, ID 05e3:0610"); char* pk=strstr(out,"        |__ Port 002:"); char* p2=strstr(out,"        |__ Port 004:"); char* pm=strstr(out,"            |__ Port 001:"); char* ps=strstr(out,"\n    |__ Port 004: Dev");
  chk("tree: hub, then receiver indented below it, then second hub, then mouse deeper", ph&&pk&&p2&&pm&&ph<pk&&pk<p2&&p2<pm);
  chk("tree: stick on root port 4 after the hub subtree", ps&&ps>pm);
  chk("tree shows driver, speed and power per device", strstr(out,"Driver=usbhid, 12M, 98mA")&&strstr(out,"Driver=hub, 480M, self-powered 100mA")&&strstr(out,"Driver=[none], 5000M, 896mA"));
  int ord[USBREG_MAX]; int on=usbreg_order(ord,USBREG_MAX);
  chk("Settings order is depth-first: hub, receiver, hub2, mouse, stick", on==5&&ord[0]==idx(hub)&&ord[1]==idx(kb)&&ord[2]==idx(hub2)&&ord[3]==idx(ms)&&ord[4]==idx(st));
  char cd[128]; usbreg_card(idx(kb),cd,sizeof cd); chk("Settings card text has port, speed, power, driver, ID", strstr(cd,"Port 1-2.2")&&strstr(cd,"12Mbit/s")&&strstr(cd,"98 mA")&&strstr(cd,"usbhid")&&strstr(cd,"046d:c52b"));
  chk("kind: receiver with keyboard+mouse interfaces", !strcmp(usbreg_kind(kb),"Keyboard and mouse")&&!strcmp(usbreg_kind(ms),"Mouse")&&!strcmp(usbreg_kind(st),"USB drive")&&!strcmp(usbreg_kind(hub),"USB hub"));
  printf("== re-enumeration and unplug ==\n");
  int kdn=kb->devnum; mkdd(dd,0x046d,0xc52b,0,0x0200); n=mkcfg(cfg,0xA0,49,kc,kp,2); usbreg_t* kb2=usbreg_add(0,1,0x2,1,1,dd,cfg,n);
  chk("second driver probing the same device keeps entry, devnum and strings", kb2==kb&&kb->devnum==kdn&&!strcmp(kb->prod,"USB Receiver")&&usbreg_count()==5);
  mkdd(dd,0x046d,0xc534,0,0x0200); usbreg_add(0,1,0x2,1,1,dd,cfg,n); chk("another device at the same port drops the old strings", kb->pid==0xc534&&!kb->prod[0]);
  chk("route bits above the device's tier are ignored on lookup", usbreg_find(0,1,0xF4,1)==hub2);
  usbreg_gone(0,1,0x14,2); chk("unplug on hub2 port 1 removes only the mouse", !ms->used&&hub2->used&&kb->used);
  ms=usbreg_add(0,1,0x14,2,2,dd,cfg,n); usbreg_gone(0,1,0,0); chk("root port unplug removes the hub subtree, not the stick", !hub->used&&!kb->used&&!hub2->used&&!ms->used&&st->used&&usbreg_count()==1);
  usbreg_gone(1,3,0,0); chk("unplug on another controller leaves this one alone", st->used);
  printf("== bounds ==\n");
  dd[0]=8; chk("short device descriptor refused", !usbreg_add(0,5,0,0,3,dd,cfg,n)); mkdd(dd,1,2,0,0x200);
  chk("depth 6 refused", !usbreg_add(0,5,0,6,3,dd,cfg,n));
  chk("no config: power 0, still registered", usbreg_add(0,6,0,0,3,dd,0,0)&&usbreg_find(0,6,0,0)->ma==0);
  int got=0; for(int i=0;i<40;i++) if(usbreg_add(0,(i%200)+10,0,0,3,dd,cfg,n))got++; chk("registry fills to USBREG_MAX and then refuses", usbreg_count()==USBREG_MAX&&got==USBREG_MAX-2);
  char tiny[16]; memset(tiny,'X',16); usbreg_list(tiny,10); chk("output never overruns a small buffer", tiny[9]==0&&tiny[10]=='X');
  tiny[0]='X'; chk("one-byte buffer stays untouched", usbreg_tree(tiny,1)==0&&tiny[0]=='X');
  u8 bad[64]; memset(bad,0,64); bad[0]=9; bad[1]=2; bad[2]=64; bad[9]=1; usbreg_gone(0,10,0,0); chk("zero-length descriptor inside config does not hang", usbreg_add(0,10,0,0,3,dd,bad,64)!=0);
  usbreg_gone(0,10,0,0); bad[9]=9; bad[10]=4; bad[14]=3; for(int i=18;i<64;i++)bad[i]=0xFF; chk("garbage after an interface descriptor stays inside the buffer", usbreg_add(0,10,0,0,3,dd,bad,64)!=0);
  drv_off=1; chk("unbound device reports [none]", !strcmp(usbreg_driver(st),"[none]"));
  printf(ok?"usbreg: ALL PASS (%d checks)\n":"usbreg: FAILURES (%d checks)\n",nchk); return ok?0:1; }
