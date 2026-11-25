#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define XHC_MAX 4
#define XHID_MAX 10
struct featguard { const char* name; const char* flag; int on, fails, maxfail, dead, trying; };
static void fg_boot(struct featguard* g){ if(!g->trying)g->trying=-1; }
static int fg_begin(struct featguard* g){ fg_boot(g); if(!g->on||g->dead)return 0; g->trying=1; return 1; }
static void fg_ok(struct featguard* g){ if(g->trying==1)g->trying=-1; }
static void fg_fail(struct featguard* g,const char* w){ (void)w; if(g->trying==1)g->trying=-1; if(++g->fails>=g->maxfail)g->dead=1; }
static void fg_enable(struct featguard* g,int on){ g->on=on; g->fails=0; if(on)g->dead=0; g->trying=-1; }
static char lastlog[200]; static void klog(const char* m){ snprintf(lastlog,sizeof lastlog,"%s",m); }
static char* scopy(char* q,const char* s){ while(*s)*q++=*s++; return q; }
static char* udec(char* q,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*q++=t[--n]; return q; }
static u64 g_ticks, g_us;
static void hr_delay_us(u32 us){ g_us+=us; }
static u32 pmcsr=0;
static int pci_find_cap(int b,int s,int f,int id){ (void)b; (void)s; (void)f; return id==1?0x50:0; }
static u32 pci_cfg(int b,int s,int f,int o){ (void)b; (void)s; (void)f; return o==0x54?pmcsr:0; }
static void pci_cfgw(int b,int s,int f,int o,u32 v){ (void)b; (void)s; (void)f; if(o==0x54){ pmcsr=(pmcsr&~0x8000u&~0x103u)|(v&0x103u); if(v&0x8000u)pmcsr&=~0x8000u; } }
static u32 g_xmem; static int g_xcur=0, g_xhc_n=1, xhci_present=1, xhci_init_ok=1, xhci_slots=8, xhci_ports=4, xhci_pci_bus=0, xhci_pci_slot=3, xhci_pci_fn=0;
static u64 xhci_base=0x40000000ULL, xhci_op=0x40000080ULL; static u32 xhci_rtoff=0x2000, xhci_dboff=0x3000, cmd_enq=7, cmd_cyc=1;
#define XHCI_DCBAA (g_xmem+0x0000u)
#define XHCI_CMDRING (g_xmem+0x1000u)
static void xhc_select(int i){ g_xcur=i; }
static int xhid_n, xhid_used[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_port[XHID_MAX], xhid_ctl[XHID_MAX], xhid_depth[XHID_MAX];
static u8 xhid_own[XHID_MAX]; static u32 xhid_e0enq[XHID_MAX], xhid_e0cyc[XHID_MAX];
static int cur_slot; static u32 cur_ep0, cur_dbuf, e0_enq, e0_cyc;
#define XHID_EP0RING(r) (g_xmem+0x8000u+(u32)(r)*0x100u)
#define XHID_DATABUF(r) (g_xmem+0x9000u+(u32)(r)*0x100u)
static int nctrl, lastreq, lastval, lastslot, ctrl_ok=1;
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){ (void)rt; (void)idx; (void)len; (void)in; (void)data; nctrl++; lastreq=req; lastval=val; lastslot=cur_slot; e0_enq+=2; return ctrl_ok; }
static int ndb, dbslot, dbtgt;
static void xh_db(int slot,u32 t){ ndb++; dbslot=slot; dbtgt=(int)t; }
static u32 R[0x4000/4];
static u32 sv_int[16]; static int sim_saved, sim_sre_save, sim_sre_restore, sim_nohalt, sim_dead;
static int pls_hist[64], npls;
#define OFF(a) ((u32)((a)-xhci_base))
static u32 sim_r(u64 a){ if(sim_dead)return 0xFFFFFFFFu; return R[OFF(a)/4]; }
static void sim_w(u64 a,u32 v){
    u32 o=OFF(a);
    if(o==0x80){ R[o/4]=v&~((1u<<8)|(1u<<9)); u32* st=&R[0x84/4];
        if(!sim_nohalt){ if(v&1)*st&=~1u; else *st|=1u; }
        if(v&(1u<<8)){ if(sim_sre_save)*st|=1u<<10; else { sim_saved=1; sv_int[0]=R[0x80/4+0x30/4]; sv_int[1]=R[0x2028/4]; sv_int[2]=R[0x2030/4]; sv_int[3]=R[0x2038/4]; } }
        if(v&(1u<<9)){ if(sim_sre_restore||!sim_saved)*st|=1u<<10; }
        return; }
    if(o==0x84){ R[o/4]&=~(v&(1u<<10)); return; }
    if(o==0x98){ R[o/4]=v; R[0x9C/4]=0xEEu; return; }
    if(o>=0x480&&o<0x480+4*0x10&&((o-0x480)&0xF)==0){ u32 cur=R[o/4]; u32 w1c=v&0x00FE0000u; cur&=~w1c;
        if(v&(1u<<16)){ int pls=(int)((v>>5)&0xF); cur=(cur&~0x1E0u)|((u32)pls<<5); if(npls<64)pls_hist[npls++]=pls; }
        R[o/4]=cur; return; }
    R[o/4]=v; }
#define XPM_R(a) sim_r(a)
#define XPM_W(a,v) sim_w(a,v)
#include "drivers/usb/xhci/power.c"
static int fails, checks;
#define CK(c,m) do{ checks++; if(!(c)){ fails++; printf("FAIL %s (line %d) log=%s\n",m,__LINE__,lastlog); } else printf(" ok %s\n",m); }while(0)
static u32 port(int p){ return R[(0x480+p*0x10)/4]; }
static void setport(int p,int ccs,int ped,int pls,int sp){ R[(0x480+p*0x10)/4]=(u32)ccs|((u32)ped<<1)|((u32)pls<<5)|(1u<<9)|((u32)sp<<10); }
static void reset_sim(void){
    memset(R,0,sizeof R); R[0]=0x80; R[4/4]=(4u<<24)|(2u<<8)|8u; R[0x10/4]=0; R[0x14/4]=xhci_dboff; R[0x18/4]=xhci_rtoff;
    R[0x80/4]=1|(1u<<2); R[0x84/4]=0; R[0xB0/4]=XHCI_DCBAA; R[0xB8/4]=8; R[0x2020/4]=2; R[0x2024/4]=4000; R[0x2028/4]=1; R[0x2030/4]=g_xmem+0x3000u; R[0x2038/4]=(g_xmem+0x2000u)|8u;
    setport(0,1,1,0,3); setport(1,1,1,0,4); setport(2,1,1,0,3); setport(3,0,0,5,0);
    sim_saved=0; sim_sre_save=0; sim_sre_restore=0; sim_nohalt=0; sim_dead=0; npls=0; nctrl=0; ndb=0; pmcsr=0;
    memset(g_xpm,0,sizeof g_xpm); g_xpm_why[0]=0; }
static void sim_s3(int pwr){
    u32 keep[4]={R[0x480/4],R[0x490/4],R[0x4A0/4],R[0x4B0/4]};
    for(u32 i=0x80/4;i<0x480/4;i++)R[i]=0; for(u32 i=0x2000/4;i<0x2100/4;i++)R[i]=0; R[0x84/4]=1;
    if(pwr)for(int p=0;p<4;p++)keep[p]&=~2u;
    for(int p=0;p<4;p++)R[(0x480+p*0x10)/4]=keep[p]; }
int main(void){
    u8* mem=mmap((void*)0x20000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); if(mem==MAP_FAILED){ printf("mmap failed\n"); return 1; }
    g_xmem=(u32)(u64)mem; memset(mem,0,0x20000);
    u32 devctx=g_xmem+0x5000u; ((volatile u32*)(u64)XHCI_DCBAA)[2]=devctx; ((volatile u32*)(u64)devctx)[1]=1u<<16;
    u32 devctx2=g_xmem+0x6000u; ((volatile u32*)(u64)XHCI_DCBAA)[4]=devctx2; ((volatile u32*)(u64)devctx2)[1]=2u<<16;
    xhid_n=2; xhid_used[0]=1; xhid_slot[0]=1; xhid_dci[0]=3; xhid_iskbd[0]=1; xhid_port[0]=0; xhid_own[0]=0;
    xhid_used[1]=1; xhid_slot[1]=2; xhid_dci[1]=5; xhid_iskbd[1]=0; xhid_port[1]=1; xhid_own[1]=1;

    reset_sim();
    CK(xhci_pm_suspend(1)==0,"S3 suspend: controller state saved");
    CK(((port(0)>>5)&0xF)==3&&((port(1)>>5)&0xF)==3&&((port(2)>>5)&0xF)==3,"S3 suspend: every enabled port is in U3");
    CK(((port(3)>>5)&0xF)==5,"S3 suspend: empty port untouched");
    CK((R[0x84/4]&1)&&sim_saved,"S3 suspend: halted before CSS, CSS done");
    CK(lastreq==3&&lastval==1&&lastslot==1,"S3 suspend: keyboard asked for remote wakeup (SET_FEATURE)");
    CK((pmcsr&0x100u)!=0,"S3 suspend: PCI PME enabled for wake");
    sim_s3(0); ndb=0; npls=0;
    CK(xhci_pm_resume()==0,"S3 resume: restored without re-enumeration");
    CK(R[0xB0/4]==XHCI_DCBAA&&R[0xB8/4]==8&&R[0x2030/4]==g_xmem+0x3000u&&R[0x2028/4]==1&&R[0x2038/4]==((g_xmem+0x2000u)|8u)&&R[0x2020/4]==2&&R[0x2024/4]==4000,"S3 resume: DCBAAP, CONFIG, ERST, ERDP, IMAN, IMOD written back");
    CK(R[0x98/4]==((XHCI_CMDRING+7*16)|1u),"S3 resume: CRCR points at the command ring enqueue with its cycle bit");
    CK(!(R[0x84/4]&1)&&(R[0x80/4]&1)&&(R[0x80/4]&4),"S3 resume: controller running with interrupts enabled as before");
    CK(((port(0)>>5)&0xF)==0&&((port(1)>>5)&0xF)==0,"S3 resume: ports back in U0");
    { int u2=0; for(int i=0;i+1<npls;i++) if(pls_hist[i]==15&&pls_hist[i+1]==0)u2=1; CK(u2,"S3 resume: USB2 port driven Resume then U0"); }
    CK(ndb==2,"S3 resume: HID endpoints rung again");
    CK(lastreq==1&&lastval==1,"S3 resume: remote wakeup cleared");
    CK(!(pmcsr&0x100u),"S3 resume: PME disabled");

    reset_sim(); xhci_pm_suspend(1); sim_s3(0); sim_sre_restore=1;
    CK(xhci_pm_resume()<0,"restore error (SRE) falls back to a full restart");
    reset_sim(); xhci_pm_suspend(1); sim_s3(1);
    CK(xhci_pm_resume()<0,"device lost power (PED=0) on a used port falls back to a full restart");
    reset_sim(); xhci_pm_suspend(1); sim_s3(0); R[0x4A0/4]&=~2u;
    CK(xhci_pm_resume()==0,"unused port losing power does not force a restart");
    reset_sim(); xhci_pm_suspend(1); sim_dead=1;
    CK(xhci_pm_resume()<0,"controller not answering after sleep falls back");
    CK(xpm_fg.dead,"three failures turn the state save off");
    reset_sim(); CK(xhci_pm_suspend(1)<0&&((port(0)>>5)&0xF)==0,"state save off after failures: nothing touched");
    xhci_pm_feature(0,1); CK(xpm_on(&xpm_fg),"state save can be switched on again");
    reset_sim(); sim_sre_save=1;
    CK(xhci_pm_suspend(1)<0,"save error is reported");
    CK(xhci_pm_resume()<0,"save error makes resume restart the controller");
    reset_sim(); sim_nohalt=1;
    CK(xhci_pm_suspend(1)<0,"controller that will not halt is not saved");
    CK(xhci_pm_resume()==1&&((port(0)>>5)&0xF)==0,"ports suspended before a failed halt are resumed, caller decides");
    xhci_pm_feature(0,0); reset_sim();
    CK(xhci_pm_suspend(1)<0&&((port(0)>>5)&0xF)==0&&xhci_pm_resume()==1,"state save switched off: nothing touched, caller restarts as before");
    xhci_pm_feature(0,1);

    reset_sim(); g_ticks=100;
    xhci_pm_idle(0); CK(((port(2)>>5)&0xF)==0,"idle: unused port not suspended at once");
    g_ticks=1200; xhci_pm_idle(0);
    CK(((port(2)>>5)&0xF)==3,"idle: unused port suspended after 10 s");
    CK(((port(0)>>5)&0xF)==0&&((port(1)>>5)&0xF)==0,"idle: ports with a slot never suspended");
    R[0x4A0/4]&=~1u; xhci_pm_idle(0); CK(!(g_xpm[0].idle&4u),"idle: unplug clears the idle state");
    setport(2,1,1,0,3); xhci_pm_suspend(0); CK(((port(2)>>5)&0xF)==3&&((port(0)>>5)&0xF)==0,"light sleep: only unused ports suspended, keyboard stays live");
    xhci_pm_feature(2,0); setport(2,1,1,0,3); g_xpm[0].idle=0; g_xpm[0].idle_t[2]=0; g_ticks=5000; xhci_pm_idle(1);
    CK(((port(2)>>5)&0xF)==0,"idle suspend switch off is honoured");
    CK(xhci_lpm_apply()==0&&!xpm_on(&xpm_lfg),"link power management off by default");
    CK(xhci_pm_status()[0]!=0,"status text");
    printf("%d checks, %d failed\n%s\n",checks,fails,fails?"SOME FAIL":"ALL PASS");
    return fails?1:0; }
