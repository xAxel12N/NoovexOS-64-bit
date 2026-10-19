#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/mem/dmawin.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
/* ---- xHCI USB controller: stage 1 (init + port detection), ported from Noovex9 32-bit ---- */

static u32 g_xmem=DMW_FIXED+0x10000u; int g_xcur=0, g_xhc_n=0, msd_ctl=0, xhci_pci_fn=0, xhci_hi4g=0;
static u32 g_xhc_mem[XHC_MAX-1];
static u32 xhc_mem_for(int i){ if(!g_xhc_mem[i]){ void* p=dma_alloc_pages(0x26); g_xhc_mem[i]=p?(u32)(u64)p:0u; } return g_xhc_mem[i]; }
 static void xhci_init(void);
static u64 g_xspb_mem[XHC_MAX]; int g_xspb_n[XHC_MAX], g_xspb_need[XHC_MAX];
#define XHCI_DCBAA   (g_xmem+0x0000u)
#define XHCI_CMDRING (g_xmem+0x1000u)
#define XHCI_EVTRING (g_xmem+0x2000u)
#define XHCI_ERST    (g_xmem+0x3000u)
#define XHCI_SCRATCH (g_xmem+0x4000u)

#define XMW(a,v) (*(volatile u32*)(u64)(a)=(u32)(v))
int xhci_present=0, xhci_init_ok=0, xhci_slots=0, xhci_ports=0; u64 xhci_base=0, xhci_op=0;
static u8 xhci_port_conn[32], xhci_port_speed[32]; int xhci_ports_hw=0, xhci_pci_bus=-1, xhci_pci_slot=-1; static u16 xhci_vendor=0;
static void xdelay(int n){ for(int i=0;i<n;i++) io_wait(); }
static const char* xhci_spd(int s){ switch(s){case 1:return "FULL";case 2:return "LOW";case 3:return "HIGH";case 4:return "SUPER";default:return "-";} }
static void xhci_init_one(int bus,int slot,int fn){
    xhci_present=0; xhci_init_ok=0; xhci_slots=0; xhci_ports=0; xhci_hi4g=0; for(int i=0;i<32;i++){xhci_port_conn[i]=0;xhci_port_speed[i]=0;}
    xhci_present=1; xhci_pci_bus=bus; xhci_pci_slot=slot; xhci_pci_fn=fn;
    xhci_vendor=(u16)(pci_cfg(bus,slot,fn,0)&0xFFFF);
    u32 bar=pci_cfg(bus,slot,fn,0x10); if(bar&1)return; u64 base=bar&0xFFFFFFF0u; if((bar&6)==4)base|=(u64)pci_cfg(bus,slot,fn,0x14)<<32; if(!base)return;
    if(base>=0x100000000ULL){ extern u64 acpi_hi_map(u64); if(!acpi_hi_map(base)||((base+0xFFFFULL)>>30)!=(base>>30)){ xhci_hi4g=1; return; } }
    u32 cmd=pci_cfg(bus,slot,fn,0x04); pci_cfgw(bus,slot,fn,0x04,cmd|0x06);
    if(xhci_vendor==0x8086){ u32 u3=pci_cfg(bus,slot,fn,0xDC); pci_cfgw(bus,slot,fn,0xD8,u3); u32 u2=pci_cfg(bus,slot,fn,0xD4); pci_cfgw(bus,slot,fn,0xD0,u2); }
    xhci_base=base;
    { u32 hcc=XMR(base+0x10); u32 x=(hcc>>16)<<2; int g=0;
      while(x&&g++<64){ u64 a=base+x; u32 v=XMR(a);
        if((v&0xFF)==1){ *(volatile u8*)(u64)(a+3)=1;
            for(int t=0;t<400000&&(XMR(a)&(1u<<16));t++)io_wait();
            if(XMR(a)&(1u<<16)){ *(volatile u8*)(u64)(a+2)=0; klog("usb: firmware did not release the controller, taking it"); } else klog("usb: controller handed over by firmware");
            u32 c=XMR(a+4); XMW(a+4,(c&~0x0000E011u)|0xE0000000u); break; }
        u32 nx=(v>>8)&0xFF; if(!nx)break; x+=nx<<2; } }
    u8 caplen=*(volatile u8*)(u64)base;
    u32 hcs1=XMR(base+0x04); int maxslots=hcs1&0xFF; int maxports=(hcs1>>24)&0xFF; xhci_ports_hw=maxports; if(maxports>32)maxports=32; if(maxports<1)maxports=1; xhci_ports=maxports; xhci_slots=maxslots;
    u32 hcs2=XMR(base+0x08); int spb=(((hcs2>>21)&0x1F)<<5)|((hcs2>>27)&0x1F); g_xspb_need[g_xcur]=spb;
    u32 rtsoff=XMR(base+0x18)&~0x1Fu;
    u64 op=base+caplen;
    volatile u32* usbcmd=(volatile u32*)(u64)(op+0x00);
    volatile u32* usbsts=(volatile u32*)(u64)(op+0x04);
    for(int t=0;t<200000;t++){ if(!(*usbsts&(1u<<11)))break; xdelay(1); }
    *usbcmd &= ~1u; for(int t=0;t<200000;t++){ if(*usbsts&1u)break; xdelay(1); }
    *usbcmd |= (1u<<1); int rok=0;
    for(int t=0;t<400000;t++){ if(!(*usbcmd&(1u<<1)) && !(*usbsts&(1u<<11))){ rok=1; break; } xdelay(1); }
    XMW(op+0x38,(u32)maxslots);
    { volatile u32* dc=(volatile u32*)(u64)XHCI_DCBAA; for(int i=0;i<(maxslots+1)*2;i++)dc[i]=0; }
    if(spb>0){ u64 arr=XHCI_SCRATCH, bufs=XHCI_SCRATCH+0x1000u;
        if(spb>32){ if(!g_xspb_mem[g_xcur]||g_xspb_n[g_xcur]<spb){ u64 m=(u64)dma_alloc_pages((unsigned long)spb+1); if(m){ g_xspb_mem[g_xcur]=m; g_xspb_n[g_xcur]=spb; } }
            if(!g_xspb_mem[g_xcur]){ klog("usb: not enough memory for the controller's scratchpad, controller not used"); return; }
            arr=g_xspb_mem[g_xcur]; bufs=arr+0x1000u; }
        volatile u32* sa=(volatile u32*)arr; for(int i=0;i<1024;i++)sa[i]=0;
        for(int i=0;i<spb;i++){ u64 b=bufs+(u64)i*0x1000u; sa[i*2]=(u32)b; sa[i*2+1]=(u32)(b>>32); volatile u64* bb=(volatile u64*)b; for(int k=0;k<512;k++)bb[k]=0; }
        *(volatile u32*)(u64)XHCI_DCBAA=(u32)arr; *(volatile u32*)(u64)(XHCI_DCBAA+4)=(u32)(arr>>32); }
    XMW(op+0x30,XHCI_DCBAA); XMW(op+0x34,0);
    { volatile u32* cr=(volatile u32*)(u64)XHCI_CMDRING; for(int i=0;i<256*4;i++)cr[i]=0; int li=255*4; cr[li]=XHCI_CMDRING; cr[li+1]=0; cr[li+2]=0; cr[li+3]=(6u<<10)|(1u<<1)|1u; }
    XMW(op+0x18,XHCI_CMDRING|1u); XMW(op+0x1C,0);
    { volatile u32* er=(volatile u32*)(u64)XHCI_EVTRING; for(int i=0;i<256*4;i++)er[i]=0; }
    { volatile u32* erst=(volatile u32*)(u64)XHCI_ERST; erst[0]=XHCI_EVTRING; erst[1]=0; erst[2]=256; erst[3]=0; }
    { u64 rt=base+rtsoff+0x20; XMW(rt+0x08,1); XMW(rt+0x18,XHCI_EVTRING); XMW(rt+0x1C,0); XMW(rt+0x10,XHCI_ERST); XMW(rt+0x14,0); }
    *usbcmd |= 1u; for(int t=0;t<200000;t++){ if(!(*usbsts&1u))break; xdelay(1); }
    for(int p=0;p<maxports;p++){ u64 pa=op+0x400+(u32)p*0x10; u32 v=XMR(pa); if(!(v&(1u<<9))){ XMW(pa,(v&0x0E00C3E0u)|(1u<<9)); } }
    for(int t=0;t<40000;t++) xdelay(1);
    for(int p=0;p<maxports;p++){ u32 v=XMR(op+0x400+(u32)p*0x10); xhci_port_conn[p]=(u8)(v&1); xhci_port_speed[p]=(u8)((v>>10)&0xF); }
    xhci_op=op; xhci_init_ok=rok;
}
/* mouse state (defined later, near the PS/2 handler) */

int g_cursor_moved=0;   /* pure pointer motion (cheap redraw, no full rebuild) */
/* keyboard input path (defined later): held-key array + char push */
 
    /* EC battery (defined near topbar) */
    /* battery (defined near topbar) */
/* ================= xHCI USB mouse: enumeration + HID + interrupt transfers =================
   Reasoned from the xHCI spec + Linux drivers/usb. Contexts/rings live at fixed identity-mapped
   low addresses (the controller DMAs to them; phys==virt here). Poll-based (no MSI). */
/* per-entry HID state: one entry per keyboard/mouse interface; region r holds contexts + rings (32 KB) */

#define XHID_BASE(r)     (DMW(0x80000u) + (u32)(r)*0x8000u)
#define XHID_INPUTCTX(r) (XHID_BASE(r))
#define XHID_DEVCTX(r)   (XHID_BASE(r)+0x1000u)
#define XHID_EP0RING(r)  (XHID_BASE(r)+0x2000u)
#define XHID_EPINRING(r) (XHID_BASE(r)+0x3000u)
#define XHID_DATABUF(r)  (XHID_BASE(r)+0x4000u)
static int xhci_csz=0; static u32 xhci_dboff=0, xhci_rtoff=0;
static u32 cmd_enq=0, cmd_cyc=1, ev_deq=0, ev_cyc=1;
static int cur_slot=0; u32 cur_ep0=0, cur_dbuf=0, e0_enq=0, e0_cyc=1;
int xhid_n=0, xhci_mouse_ok=0;
int xhid_used[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_rlen[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_port[XHID_MAX];
static u32 xhid_epin[XHID_MAX], xhid_ein_enq[XHID_MAX], xhid_ein_cyc[XHID_MAX], xhid_dbuf[XHID_MAX];
static u8 xhid_kprev[XHID_MAX][8];
int xhid_nlay[XHID_MAX], xhid_rep[XHID_MAX], xhid_hasid[XHID_MAX], xhid_ctl[XHID_MAX];
typedef struct { u8 sl, ep, cc, ctl; u32 res, ptr; } xhq_t; static xhq_t xh_q[32]; static int xh_qn=0, xh_want_slot=-1, xh_want_ep=-1;
u32 xhid_cnt[XHID_MAX]; u8 xhid_last[XHID_MAX][16]; int xhid_lastn[XHID_MAX];
typedef struct { int root, depth, hub, hport, speed; u32 route; int tt_slot, tt_port; } usbloc_t;
static int uvc_reg_hold=-1; static int uvc_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm); static int uvc_has_video(const u8* c,int n); static void uvc_reset(void); static void uvc_unplug(int port);

xhub_t xhub[XHUB_MAX]; static int xreg_hub[XHID_MAX]; u32 xhid_route[XHID_MAX]; int xhid_depth[XHID_MAX];
static int xhub_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval);
static void xhid_reset(void){ uvc_reset(); for(int h=0;h<XHUB_MAX;h++)xhub[h].used=0; for(int i=0;i<XHID_MAX;i++){ xreg_hub[i]=0; xhid_route[i]=0; xhid_depth[i]=0; xhid_cnt[i]=0; xhid_lastn[i]=0; xhid_rep[i]=0; xhid_nlay[i]=0; xhid_used[i]=0; xhid_slot[i]=0; for(int k=0;k<8;k++)xhid_kprev[i][k]=0; } xhid_n=0; xhci_mouse_ok=0; }
typedef struct { u8 iface, proto, sub, ep; u16 mps; u8 intv; u16 rdlen; } hidif_t;

hidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX];
static int hid_parse(const u8* d,int n,hidlay_t* out,int max,int* hasid){
    u32 upage=0, rsize=0, rcount=0, rid=0; u32 usages[32]; int nus=0; u32 umin=0, umax=0; int hasrange=0;
    u32 gst[4][4]; int gsp=0; int depth=0, appkind[8]={0}; int nl=0; *hasid=0;
    int bitpos[256]; for(int i=0;i<256;i++)bitpos[i]=0;
    for(int i=0;i<n;){
        u8 b=d[i]; if(b==0xFE){ if(i+2>=n)break; i+=3+d[i+1]; continue; }
        int sz=b&3; if(sz==3)sz=4; int type=(b>>2)&3, tag=(b>>4)&0xF; if(i+1+sz>n)break;
        u32 v=0; for(int k=0;k<sz;k++)v|=(u32)d[i+1+k]<<(8*k); i+=1+sz;
        if(type==1){ if(tag==0)upage=v; else if(tag==7)rsize=v; else if(tag==8){ rid=v&0xFF; *hasid=1; } else if(tag==9)rcount=v;
            else if(tag==10&&gsp<4){ gst[gsp][0]=upage; gst[gsp][1]=rsize; gst[gsp][2]=rcount; gst[gsp][3]=rid; gsp++; } else if(tag==11&&gsp>0){ gsp--; upage=gst[gsp][0]; rsize=gst[gsp][1]; rcount=gst[gsp][2]; rid=gst[gsp][3]; } }
        else if(type==2){ u32 u=(sz==4)?v:((upage<<16)|v); if(tag==0&&nus<32)usages[nus++]=u; else if(tag==1){ umin=u; hasrange=1; } else if(tag==2){ umax=u; hasrange=1; } }
        else if(type==0){
            if(tag==10){ int kind=appkind[depth>0?depth-1:0]; if(v==1){ u32 u=nus?usages[0]:0; kind=(u==0x10002u||u==0x10001u)?2:(u==0x10006u||u==0x10007u)?1:0; } if(depth<7){ depth++; appkind[depth-1]=kind; } }
            else if(tag==12){ if(depth>0)depth--; }
            else if(tag==8){
                int kind=depth>0?appkind[depth-1]:0; int flags=(int)v; int pos=bitpos[rid];
                if(kind&&!(flags&1)){
                    hidlay_t* L=0; for(int k=0;k<nl;k++) if(out[k].id==rid&&out[k].kind==kind)L=&out[k];
                    if(!L&&nl<max){ L=&out[nl++]; for(int k=0;k<(int)(sizeof(hidlay_t)/sizeof(short));k++)((short*)L)[k]=-1; L->id=(u8)rid; L->kind=(u8)kind; L->btn_cnt=0; L->arr_cnt=0; L->bm_cnt=0; }
                    if(L){ int var=(flags&2)!=0;
                        if(var){ for(u32 c=0;c<rcount;c++){ u32 u; if(nus) u=usages[c<(u32)nus?c:(u32)nus-1]; else if(hasrange) u=umin+c; else u=0; if(hasrange&&!nus&&umin+c>umax)u=0;
                                int at=pos+(int)(c*rsize); u32 pg=u>>16, us=u&0xFFFF;
                                if(pg==9&&rsize==1){ if(L->btn_cnt==0)L->btn_off=(short)at; if(L->btn_cnt<16)L->btn_cnt++; }
                                else if(pg==1&&us==0x30){ L->x_off=(short)at; L->x_sz=(short)rsize; }
                                else if(pg==1&&us==0x31){ L->y_off=(short)at; L->y_sz=(short)rsize; }
                                else if(pg==1&&us==0x38){ L->w_off=(short)at; L->w_sz=(short)rsize; }
                                else if(pg==7&&rsize==1&&us>=0xE0&&us<=0xE7){ if(us==0xE0)L->mod_off=(short)at; }
                                else if(pg==7&&rsize==1&&us<0xE0){ if(L->bm_cnt==0){ L->bm_off=(short)at; L->bm_min=(short)us; } L->bm_cnt++; } } }
                        else if((upage==7||(hasrange&&(umin>>16)==7))&&rsize==8&&L->arr_cnt==0){ L->arr_off=(short)pos; L->arr_cnt=(short)(rcount>6?6:rcount); } }
                }
                bitpos[rid]=pos+(int)(rsize*rcount);
            }
            nus=0; hasrange=0; umin=umax=0;
        }
    }
    int k=0; for(int j=0;j<nl;j++){ hidlay_t* L=&out[j]; int ok=(L->kind==2)?(L->x_off>=0&&L->y_off>=0):(L->kind==1)?(L->arr_cnt>0||L->bm_cnt>0):0; if(ok)out[k++]=*L; }
    return k;
}
static int hid_bits(const u8* b,int len,int off,int sz,int sgn){ if(off<0||sz<=0||sz>32||off+sz>len*8)return 0; u32 v=0; for(int i=0;i<sz;i++){ int p=off+i; if(b[p>>3]&(1<<(p&7)))v|=1u<<i; } if(sgn&&sz<32&&(v&(1u<<(sz-1))))v|=~0u<<sz; return (int)v; }
static int hid_pick(const u8* f,int len,hidif_t* out,int max){
    int n=0, cur=-1;
    for(int i=0;i+1<len;){ int bl=f[i], bt=f[i+1]; if(bl<2||i+bl>len)break;
        if(bt==4&&bl>=9){ cur=-1; if(f[i+3]==0&&f[i+5]==3&&n<max){ cur=n; out[n].iface=f[i+2]; out[n].proto=f[i+7]; out[n].sub=f[i+6]; out[n].ep=0; out[n].mps=0; out[n].intv=0; out[n].rdlen=0; n++; } }
        else if(bt==0x21&&bl>=9&&cur>=0&&f[i+6]==0x22){ out[cur].rdlen=(u16)(f[i+7]|(f[i+8]<<8)); }
        else if(bt==5&&bl>=7&&cur>=0&&!out[cur].ep){ u8 a=f[i+2]; if((a&0x80)&&(f[i+3]&3)==3){ out[cur].ep=a; out[cur].mps=(u16)((f[i+4]|(f[i+5]<<8))&0x7FF); out[cur].intv=f[i+6]; } }
        i+=bl; }
    int k=0; for(int j=0;j<n;j++) if(out[j].ep&&out[j].mps) out[k++]=out[j];
    return k;
}
static u32 xh_interval(int speed,int bint){
    if(bint<1)bint=1;
    if(speed>=3){ int v=bint-1; if(v>15)v=15; return (u32)v; }
    int v=3; while(v<10&&(1<<(v+1))<=bint*8)v++; return (u32)v;
}
char xhci_failstep[48]=""; int xhci_failcc=0;
static void xh_db(int slot,u32 target){ *(volatile u32*)(u64)(xhci_base+xhci_dboff+(u32)slot*4)=target; }
static int xh_event_t(u32 want_type,u32* p0,u8* cc,u8* slot,u8* epid){
    for(int t=0;t<400000;t++){ volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)==ev_cyc){ u32 ty=(e[3]>>10)&0x3F; u8 c=(u8)((e[2]>>24)&0xFF), sl=(u8)((e[3]>>24)&0xFF), ep=(u8)((e[3]>>16)&0x1F); u32 pp=e[0];
            ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
            u64 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
            if(ty==32&&(want_type!=32||(xh_want_slot>=0&&(sl!=(u8)xh_want_slot||ep!=(u8)xh_want_ep)))){ if(xh_qn<32){ xh_q[xh_qn].ctl=(u8)g_xcur; xh_q[xh_qn].sl=sl; xh_q[xh_qn].ep=ep; xh_q[xh_qn].cc=c; xh_q[xh_qn].res=e[2]&0xFFFFFF; xh_q[xh_qn].ptr=e[0]; xh_qn++; } continue; }
            if(ty==want_type){ if(p0)*p0=pp; if(cc)*cc=c; if(slot)*slot=sl; if(epid)*epid=ep; return 1; }
        } else io_wait();
    }
    return 0;
}
static int xh_cmd_s(u32 p0,u32 p1,u32 p2,u32 type,u32 slotid,u8* ccode,u8* slotret){
    volatile u32* tr=(volatile u32*)(u64)(XHCI_CMDRING+cmd_enq*16);
    tr[0]=p0; tr[1]=p1; tr[2]=p2; tr[3]=(type<<10)|((slotid&0xFF)<<24)|cmd_cyc;
    cmd_enq++; if(cmd_enq>=255){ volatile u32* lk=(volatile u32*)(u64)(XHCI_CMDRING+255*16); lk[3]=(lk[3]&~1u)|cmd_cyc; cmd_enq=0; cmd_cyc^=1; }
    xh_db(0,0);
    u8 cc=0,sl=0; if(!xh_event_t(33,0,&cc,&sl,0))return 0; if(ccode)*ccode=cc; if(slotret)*slotret=sl;
    return cc==1;
}
/* control transfer on the CURRENT device's EP0 */
static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    u32 R=cur_ep0, DB=cur_dbuf; volatile u32* r;
    r=(volatile u32*)(u64)(R+e0_enq*16);
    r[0]=(u32)rt|((u32)req<<8)|((u32)val<<16); r[1]=(u32)idx|((u32)len<<16);
    r[2]=8; r[3]=(2u<<10)|(1u<<6)|((len?(in?3u:2u):0u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    if(len){ r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=DB; r[1]=0; r[2]=len; r[3]=(3u<<10)|((in?1u:0u)<<16)|e0_cyc;
        e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;} }
    r=(volatile u32*)(u64)(R+e0_enq*16); r[0]=0;r[1]=0;r[2]=0; r[3]=(4u<<10)|(1u<<5)|(((len&&in)?0u:1u)<<16)|e0_cyc;
    e0_enq++; if(e0_enq>=255){volatile u32* lk=(volatile u32*)(u64)(R+255*16); lk[3]=(lk[3]&~1u)|e0_cyc; e0_enq=0; e0_cyc^=1;}
    xh_db(cur_slot,1);
    u8 cc=0; xh_want_slot=cur_slot; xh_want_ep=1; int ok=xh_event_t(32,0,&cc,0,0); xh_want_slot=-1; xh_want_ep=-1;
    if(!ok)return 0;
    if(cc==6){ u8 c2=0,s2=0; xh_cmd_s(0,0,0,14u|(1u<<6),(u32)cur_slot,&c2,&s2);
        xh_cmd_s((R+e0_enq*16)|e0_cyc,0,0,16u|(1u<<6),(u32)cur_slot,&c2,&s2); return 0; }
    if(in&&data){ volatile u8* db=(volatile u8*)(u64)DB; for(int i=0;i<len;i++)data[i]=db[i]; }
    return cc==1||cc==13;
}
static u32 ctx_sz(void){ return xhci_csz?64u:32u; }
static void ctx_zero(u32 base,int n){ volatile u32* c=(volatile u32*)(u64)base; for(int i=0;i<n;i++)c[i]=0; }
/* enumerate one HID device on a port into the next free device index; detect kbd vs mouse */
static int xhid_free_region(int skip0,int skip1){ for(int r=0;r<XHID_MAX;r++) if(!xhid_used[r]&&!xreg_hub[r]&&r!=skip0&&r!=skip1&&r!=uvc_reg_hold)return r; return -1; }
static void xh_disable_slot(int slot){ if(slot<=0)return; u8 cc=0,sl=0; xh_cmd_s(0,0,0,10,(u32)slot,&cc,&sl); *(volatile u32*)(u64)(XHCI_DCBAA+slot*8)=0; }
#define XHID_INFLIGHT 16
static u8 xhid_bs[64];
static void xhid_arm(int e){
    volatile u32* tr=(volatile u32*)(u64)(xhid_epin[e]+xhid_ein_enq[e]*16);
    u32 buf=xhid_dbuf[e]+(u32)(xhid_bs[e&63]&(XHID_INFLIGHT-1))*64u; xhid_bs[e&63]++;
    tr[0]=buf; tr[1]=0; tr[2]=(u32)xhid_rlen[e]; tr[3]=(1u<<10)|(1u<<5)|xhid_ein_cyc[e];
    xhid_ein_enq[e]++; if(xhid_ein_enq[e]>=255){ volatile u32* lk=(volatile u32*)(u64)(xhid_epin[e]+255*16); lk[3]=(lk[3]&~1u)|xhid_ein_cyc[e]; xhid_ein_enq[e]=0; xhid_ein_cyc[e]^=1; }
    xh_db(xhid_slot[e],(u32)xhid_dci[e]);
}
static void xh_set_fail(const char* m){ int i=0; while(m[i]&&i<47){ xhci_failstep[i]=m[i]; i++; } xhci_failstep[i]=0; }
static int xh_root_reset(int port){
    xhci_csz=(XMR(xhci_base+0x10)>>2)&1; xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu;
    u64 pa=xhci_op+0x400+(u32)port*0x10; u32 pv=XMR(pa);
    if(!(pv&1))return 0;
    XMW(pa,(pv&0x0E00C3E0u)|(1u<<4));
    for(int t=0;t<200000;t++){ if(XMR(pa)&(1u<<21))break; io_wait(); }
    for(volatile int dl=0;dl<50000;dl++) io_wait();
    pv=XMR(pa); int sp=(int)((pv>>10)&0xF); return sp?sp:1; }
static void xh_slot_ctx(volatile u32* sc,const usbloc_t* L,int entries){ sc[0]=((u32)entries<<27)|((u32)L->speed<<20)|(L->route&0xFFFFFu); sc[1]=(u32)(L->root+1)<<16; sc[2]=(u32)(L->tt_slot&0xFF)|((u32)(L->tt_port&0xFF)<<8); sc[3]=0; }
static int xh_fix_mps0(u32 inctx,const usbloc_t* L,u32 ring){
    if(L->speed!=1)return 1;
    if(!xh_ctrl(0x80,6,0x0100,0,8,1,0))return 0;
    int m=((volatile u8*)(u64)cur_dbuf)[7]; if(m!=16&&m!=32&&m!=64)return 1;
    ctx_zero(inctx,(int)ctx_sz()*3/4); volatile u32* ic=(volatile u32*)(u64)inctx; ic[1]=1u<<1;
    volatile u32* e0=(volatile u32*)(u64)(inctx+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)m<<16); e0[2]=ring|1u; e0[4]=8;
    u8 cc=0,sl=0; xh_cmd_s(inctx,0,0,13,(u32)cur_slot,&cc,&sl); return 1; }
static int xhci_enum_loc(const usbloc_t* L);
static int xhci_hid_enum(int port){
    xhci_failstep[0]=0; xhci_failcc=0;
    int sp=xh_root_reset(port); if(!sp){ xh_set_fail("no device on port"); return 0; }
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return xhci_enum_loc(&L); }
static int xhci_enum_loc(const usbloc_t* L){
    int d=xhid_free_region(-1,-1); if(d<0){ xh_set_fail("no free USB device memory"); return 0; }
    int slot_ok=0; xhci_failstep[0]=0; xhci_failcc=0;
    #define XFAIL(msg) do{ int _i=0; while(msg[_i]&&_i<47){xhci_failstep[_i]=msg[_i];_i++;} xhci_failstep[_i]=0; if(slot_ok)xh_disable_slot(cur_slot); return 0; }while(0)
    int port=L->root, speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=0; e0_cyc=1;
    { volatile u32* r0=(volatile u32*)(u64)XHID_EP0RING(d); for(int i=0;i<256*4;i++)r0[i]=0;
      int li=255*4; r0[li]=XHID_EP0RING(d); r0[li+1]=0; r0[li+2]=0; r0[li+3]=(6u<<10)|(1u<<1)|1u; }
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; XFAIL("Enable Slot");}
    cur_slot=sl; if(!cur_slot) XFAIL("no slot id"); slot_ok=1;
    ctx_zero(XHID_DEVCTX(d),(int)ctx_sz()*32/4);
    *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8)=XHID_DEVCTX(d); *(volatile u32*)(u64)(XHCI_DCBAA+cur_slot*8+4)=0;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*3/4);
    volatile u32* ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=XHID_EP0RING(d)|1u; e0[3]=0; e0[4]=8;
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,11,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_fix_mps0(XHID_INPUTCTX(d),L,XHID_EP0RING(d))) XFAIL("GET device descriptor (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0)) XFAIL("GET device descriptor");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0)) XFAIL("GET config(9)");
    u8 cfg9[9]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<9;i++)cfg9[i]=db[i]; }
    int wtot=cfg9[2]|(cfg9[3]<<8); if(wtot>256)wtot=256; u8 cfgval=cfg9[5];
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wtot,1,0)) XFAIL("GET config(full)");
    u8 full[256]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<wtot;i++)full[i]=db[i]; }
    for(int i=0;i+5<wtot;){ int bl=full[i]; if(bl<2)break; if(full[i+1]==4&&full[i+5]==9){ int r=xhub_attach(L,d,full,wtot,cfgval); if(r<0){ slot_ok=1; XFAIL("USB hub setup"); } return r; } i+=bl; }
    u16 cvid=0,cpid=0; char cnm[40]; cnm[0]=0;
    { u8 dd[18]; volatile u8* db0=(volatile u8*)(u64)cur_dbuf; (void)db0; u16 vid=0,pid=0; u8 ip=0;
      if(xh_ctrl(0x80,6,0x0100,0,18,1,0)){ for(int q=0;q<18;q++)dd[q]=((volatile u8*)(u64)cur_dbuf)[q]; vid=(u16)(dd[8]|(dd[9]<<8)); pid=(u16)(dd[10]|(dd[11]<<8)); ip=dd[15]; }
      char nm[40]; nm[0]=0;
      if(ip&&xh_ctrl(0x80,6,(u16)(0x0300|ip),0x0409,64,1,0)){ volatile u8* sb=(volatile u8*)(u64)cur_dbuf; int L=sb[0]; int o=0; for(int q=2;q+1<L&&q<64&&o<39;q+=2){ u8 c=sb[q]; if(sb[q+1]||c<32||c>126)c='?'; nm[o++]=(char)c; } nm[o]=0; }
      usbdev_note(L->root,vid,pid,full,wtot,nm); cvid=vid; cpid=pid; for(int q=0;q<40;q++)cnm[q]=nm[q]; }
    if(uvc_has_video(full,wtot)){ int tw=cfg9[2]|(cfg9[3]<<8); if(tw>16000)tw=16000; if(tw<9)tw=9;
        u8* big=(u8*)kmalloc((unsigned long)tw+16); if(!big) XFAIL("no memory for camera descriptors");
        if(!xh_ctrl(0x80,6,0x0200,0,(u16)tw,1,0)){ kfree(big); XFAIL("GET camera config"); }
        { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<tw;i++)big[i]=db[i]; }
        int ok=uvc_attach(L,d,big,tw,cfgval,cvid,cpid,cnm); kfree(big); if(!ok) XFAIL("camera setup"); uvc_reg_hold=d; return 1; }
    hidif_t hf[6]; int nh=hid_pick(full,wtot,hf,6);
    if(!nh) XFAIL("no keyboard or mouse interface");
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)) XFAIL("SET_CONFIGURATION");
    hidlay_t lays[6][HIDLAY_MAX]; int nlays[6]={0,0,0,0,0,0}, hasid[6]={0,0,0,0,0,0}, mode[6]={0,0,0,0,0,0};
    { int k=0;
      for(int j=0;j<nh;j++){ int keep=0, m=0, nl=0, hid=0;
        if(hf[j].sub==1&&hf[j].proto==1){ keep=1; m=0; }
        else {
            if(hf[j].rdlen>0&&hf[j].rdlen<=1024&&xh_ctrl(0x81,6,0x2200,(u16)hf[j].iface,hf[j].rdlen,1,0)){
                u8 rd[1024]; volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int q=0;q<hf[j].rdlen;q++)rd[q]=db[q];
                nl=hid_parse(rd,hf[j].rdlen,lays[k],HIDLAY_MAX,&hid); }
            if(nl>0){ keep=1; m=1; } else if(hf[j].sub==1&&hf[j].proto==2){ keep=1; m=0; nl=0; } }
        if(keep){ if(k!=j)hf[k]=hf[j]; mode[k]=m; nlays[k]=nl; hasid[k]=hid; k++; } }
      nh=k; }
    for(int j=0;j<nh;j++){ int kb=mode[j]?0:(hf[j].proto==1); for(int q=0;q<nlays[j];q++) if(lays[j][q].kind==1)kb=1;
        if(hf[j].sub==1) xh_ctrl(0x21,0x0B,mode[j]?1:0,(u16)hf[j].iface,0,0,0);
        if(kb) xh_ctrl(0x21,0x0A,0,(u16)hf[j].iface,0,0,0); }
    if(!nh) XFAIL("no keyboard or mouse interface");
    int reg[6]; reg[0]=d; int got=1;
    for(int j=1;j<nh;j++){ int r=-1; for(int c=0;c<XHID_MAX&&r<0;c++){ if(xhid_used[c])continue; int taken=0; for(int q=0;q<j;q++) if(reg[q]==c)taken=1; if(!taken)r=c; } if(r<0)break; reg[j]=r; got++; }
    nh=got;
    int maxdci=1; for(int j=0;j<nh;j++){ int dci=(hf[j].ep&0x0F)*2+1; if(dci>maxdci)maxdci=dci; }
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*(maxdci+2)/4);
    ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=1u;
    sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    for(int j=0;j<nh;j++){ int r=reg[j]; int dci=(hf[j].ep&0x0F)*2+1; u32 mps=hf[j].mps; ic[1]|=(1u<<dci);
        volatile u32* er=(volatile u32*)(u64)XHID_EPINRING(r); for(int i=0;i<256*4;i++)er[i]=0;
        int li=255*4; er[li]=XHID_EPINRING(r); er[li+1]=0; er[li+2]=0; er[li+3]=(6u<<10)|(1u<<1)|1u;
        volatile u32* ep=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*(u32)(dci+1));
        ep[0]=xh_interval(speed,hf[j].intv)<<16; ep[1]=(7u<<3)|(3u<<1)|(mps<<16); ep[2]=XHID_EPINRING(r)|1u; ep[3]=0; ep[4]=mps|(mps<<16); }
    if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,12,(u32)cur_slot,&cc,&sl)){xhci_failcc=cc; XFAIL("Configure Endpoint");}
    for(int j=0;j<nh;j++){ int e=reg[j]; int dci=(hf[j].ep&0x0F)*2+1; int rlen=hf[j].proto==1?8:(hf[j].mps<8?hf[j].mps:8); if(rlen<3)rlen=3;
        if(mode[j]){ rlen=hf[j].mps>64?64:hf[j].mps; if(rlen<1)rlen=8; }
        xhid_rep[e]=mode[j]; xhid_hasid[e]=hasid[j]; xhid_nlay[e]=nlays[j]; for(int q=0;q<nlays[j];q++)xhid_lay[e][q]=lays[j][q];
        xhid_ctl[e]=g_xcur; xhid_used[e]=1; xhid_slot[e]=cur_slot; xhid_dci[e]=dci; xhid_rlen[e]=rlen; xhid_iskbd[e]=mode[j]?(nlays[j]>0&&lays[j][0].kind==1):(hf[j].proto==1); xhid_port[e]=port; xhid_route[e]=L->route; xhid_depth[e]=L->depth;
        xhid_epin[e]=XHID_EPINRING(e); xhid_dbuf[e]=XHID_DATABUF(e); xhid_ein_enq[e]=0; xhid_ein_cyc[e]=1; for(int k=0;k<8;k++)xhid_kprev[e][k]=0;
        if(e+1>xhid_n)xhid_n=e+1;
        xhid_bs[e&63]=0; for(int q=0;q<XHID_INFLIGHT;q++)xhid_arm(e); }
    xhci_mouse_ok=1;
    #undef XFAIL
    return nh;
}
/* USB HID keyboard usage -> ASCII (unshifted / shifted) */
static char hid2ascii(u8 u,int shift){
    if(u>=0x04&&u<=0x1D){ char c=(char)('a'+(u-0x04)); return shift?(char)(c-32):c; }
    if(u>=0x1E&&u<=0x26){ const char* n="!@#$%^&*("; return shift?n[u-0x1E]:(char)('1'+(u-0x1E)); }
    if(u==0x27) return shift?')':'0';
    switch(u){ case 0x2C:return ' '; case 0x28:return '\n'; case 0x2A:return '\b'; case 0x2B:return '\t';
        case 0x2D:return shift?'_':'-'; case 0x2E:return shift?'+':'='; case 0x2F:return shift?'{':'[';
        case 0x30:return shift?'}':']'; case 0x31:return shift?'|':'\\'; case 0x33:return shift?':':';';
        case 0x34:return shift?'"':'\''; case 0x36:return shift?'<':','; case 0x37:return shift?'>':'.';
        case 0x38:return shift?'?':'/'; case 0x35:return shift?'~':'`'; }
    return 0;
}
/* USB HID usage -> PS/2 set-1 scancode (for kdown[]: Ctrl/Shift/Alt/arrows/letters used by shortcuts) */
static u8 hid2scan(u8 u){
    static const u8 letter[26]={0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
    if(u>=0x04&&u<=0x1D) return letter[u-0x04];
    switch(u){ case 0x28:return 0x1C; case 0x29:return 0x01; case 0x2A:return 0x0E; case 0x2B:return 0x0F; case 0x2C:return 0x39;
        case 0x4F:return 0x4D; case 0x50:return 0x4B; case 0x51:return 0x50; case 0x52:return 0x48;   /* arrows R/L/D/U */
        case 0x1E:return 0x02; case 0x1F:return 0x03; case 0x20:return 0x04; case 0x21:return 0x05; case 0x22:return 0x06;
        case 0x23:return 0x07; case 0x24:return 0x08; case 0x25:return 0x09; case 0x26:return 0x0A; case 0x27:return 0x0B; }
    return 0;
}

static int usbk_rep_code=0, usbk_rep_dev=-1; static u8 usbk_rep_usage=0; static u64 usbk_rep_t0=0, usbk_rep_last=0;
static int usb_key_code(u8 u,int shift){
    switch(u){ case 0x4F: return 18; case 0x50: return 17; case 0x51: return 20; case 0x52: return 19; case 0x4A: return 2; case 0x4D: return 5; case 0x4B: return 6; case 0x4E: return 7; case 0x4C: return 14; case 0x49: return 15; case 0x29: return 27; case 0x58: return '\n';
        case 0x54: return '/'; case 0x55: return '*'; case 0x56: return '-'; case 0x57: return '+'; case 0x62: return '0'; case 0x63: return '.'; }
    if(u>=0x59&&u<=0x61) return '1'+(u-0x59);
    int c=hid2ascii(u,shift); if(kcaps&&c>='a'&&c<='z')c-=32; else if(kcaps&&c>='A'&&c<='Z'&&shift)c+=32;
    return c;
}
static void kbd_report(int e,volatile u8* d){
    u8* prev=xhid_kprev[e]; u8 mod=d[0]; int shift=(mod&0x22)!=0;
    kdown[0x1D]=(mod&0x11)?1:0; kdown[0x2A]=(mod&0x22)?1:0; kdown[0x38]=(mod&0x44)?1:0; kdown[0x5B]=(mod&0x88)?1:0;
    if(d[2]==1)return;
    for(int i=2;i<8;i++){ u8 k=d[i]; if(!k)continue; int was=0; for(int j=2;j<8;j++) if(prev[j]==k){was=1;break;}
        if(!was){ u8 sc=hid2scan(k); if(sc)kdown[sc]=1; { extern volatile u32 g_kbd_last_tick; if(k>=4&&k<0xE0)g_kbd_last_tick=(u32)g_ticks; }
            if(k==0x39){ kcaps^=1; continue; }
            if(k==0x45){ kpush(31); continue; }
            if(k==0x2A&&(mod&0x11)&&(mod&0x44)){ kpush(30); continue; }
            int c=usb_key_code(k,shift); if(c){ kpush(c); usbk_rep_code=c; usbk_rep_usage=k; usbk_rep_dev=e; usbk_rep_t0=g_ticks; usbk_rep_last=g_ticks; } } }
    for(int i=2;i<8;i++){ u8 k=prev[i]; if(!k)continue; int still=0; for(int j=2;j<8;j++) if(d[j]==k){still=1;break;}
        if(!still){ u8 sc=hid2scan(k); if(sc)kdown[sc]=0; if(usbk_rep_dev==e&&usbk_rep_usage==k){ usbk_rep_code=0; usbk_rep_dev=-1; } } }
    for(int i=0;i<8;i++) prev[i]=d[i];
}
static void usb_kbd_repeat(void){
    if(!usbk_rep_code)return;
    if(g_ticks-usbk_rep_t0>=50&&g_ticks-usbk_rep_last>=3){ kpush(usbk_rep_code); usbk_rep_last=g_ticks; }
}
static void hid_report_decode(int e,const u8* b,int len){
    int id=0; const u8* p=b; int pl=len; if(xhid_hasid[e]){ if(len<1)return; id=b[0]; p=b+1; pl=len-1; }
    for(int q=0;q<xhid_nlay[e];q++){ hidlay_t* L=&xhid_lay[e][q]; if(L->id!=id)continue;
        if(L->kind==2){ int btn=0; for(int i=0;i<L->btn_cnt&&i<8;i++) if(hid_bits(p,pl,L->btn_off+i,1,0))btn|=1<<i;
            int dx=hid_bits(p,pl,L->x_off,L->x_sz,1), dy=hid_bits(p,pl,L->y_off,L->y_sz,1), wz=L->w_off>=0?hid_bits(p,pl,L->w_off,L->w_sz,1):0;
            if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
            mouse_btn=btn&1; mouse_rbtn=(btn>>1)&1; mouse_mid=(btn>>2)&1; if(btn)mouse_evt=1; if(wz)mouse_scroll+=wz; }
        else { u8 r[8]={0,0,0,0,0,0,0,0}; if(L->mod_off>=0) for(int i=0;i<8;i++) if(hid_bits(p,pl,L->mod_off+i,1,0))r[0]|=(u8)(1<<i);
            int n=2;
            if(L->arr_cnt>0) for(int i=0;i<L->arr_cnt&&n<8;i++){ int k=hid_bits(p,pl,L->arr_off+i*8,8,0); if(k)r[n++]=(u8)k; }
            else for(int i=0;i<L->bm_cnt&&n<8;i++) if(hid_bits(p,pl,L->bm_off+i,1,0)){ int u=L->bm_min+i; if(u>=4)r[n++]=(u8)u; }
            kbd_report(e,r); }
        return; }
}
static void hid_xfer(u8 sl,int epid,u8 cc,u32 res,u32 ptr){
    int d=-1; for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur&&xhid_slot[i]==sl&&xhid_dci[i]==epid){d=i;break;} if(d<0)return;
    if(cc==1||cc==13){
        u32 ba=xhid_dbuf[d]; if(ptr>=xhid_epin[d]&&ptr<xhid_epin[d]+256*16){ u32 b2=((volatile u32*)(u64)ptr)[0]; if(b2>=xhid_dbuf[d]&&b2<xhid_dbuf[d]+XHID_INFLIGHT*64u)ba=b2; }
        volatile u8* db=(volatile u8*)(u64)ba;
        int got=xhid_rlen[d]-(int)res; if(got<0)got=0;
        xhid_cnt[d]++; { int nn=got<16?got:16; for(int q=0;q<nn;q++)xhid_last[d][q]=db[q]; xhid_lastn[d]=nn; }
        if(xhid_rep[d]) hid_report_decode(d,(const u8*)db,got);
        else if(xhid_iskbd[d]) kbd_report(d,db);
        else { u8 btn=db[0]; int dx=(int)(int8_t)db[1]; int dy=(int)(int8_t)db[2];
            if(dx||dy){ mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }
            mouse_btn=btn&1; mouse_rbtn=(btn>>1)&1; mouse_mid=(btn>>2)&1; if(btn)mouse_evt=1;
            if(xhid_rlen[d]>=4){ int wz=(int)(int8_t)db[3]; if(wz)mouse_scroll+=wz; } }
    }
    xhid_arm(d);
}
static void xhci_poll_one(void){
    for(int i=0;i<xh_qn;){ if(xh_q[i].ctl!=(u8)g_xcur){ i++; continue; } xhq_t q=xh_q[i]; for(int k=i+1;k<xh_qn;k++)xh_q[k-1]=xh_q[k]; xh_qn--; hid_xfer(q.sl,q.ep,q.cc,q.res,q.ptr); }
    for(int guard=0; guard<128; guard++){
        volatile u32* e=(volatile u32*)(u64)(XHCI_EVTRING+ev_deq*16);
        if((e[3]&1)!=ev_cyc)return;
        u32 type=(e[3]>>10)&0x3F; u8 sl=(u8)((e[3]>>24)&0xFF); u8 cc=(u8)((e[2]>>24)&0xFF); int epid=(int)((e[3]>>16)&0x1F); u32 res=e[2]&0xFFFFFF;
        ev_deq++; if(ev_deq>=256){ev_deq=0;ev_cyc^=1;}
        u64 rt=xhci_base+xhci_rtoff+0x20; XMW(rt+0x18,(XHCI_EVTRING+ev_deq*16)|(1u<<3)); XMW(rt+0x1C,0);
        if(type==32)hid_xfer(sl,epid,cc,res,e[0]);
    }
}
void xhci_mouse_poll(void){
    if(!xhid_n)return;
    usb_kbd_repeat();
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(xhci_init_ok)xhci_poll_one(); }
}
static void xhub_remove_below(int root,u32 R,int dR);
static void xhid_remove_port(int port){ xhub_remove_below(port,0,0); }
void xhci_mouse_autoinit(void){
    xhci_init(); xhid_reset();
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue;
        for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p]) continue; if(xhid_free_region(-1,-1)<0)break; xhci_hid_enum(p); } }
    if(g_xhc_n)xhc_select(0);
}
/* ================= USB Mass Storage: Bulk-Only Transport + SCSI + FAT ================= */
#define MSD_INPUTCTX DMW(0xD0000u)
#define MSD_DEVCTX   DMW(0xD1000u)
#define MSD_EP0RING  DMW(0xD2000u)
#define MSD_INRING   DMW(0xD3000u)
#define MSD_OUTRING  DMW(0xD4000u)
#define MSD_CBW      DMW(0xD5000u)
#define MSD_CSW      DMW(0xD5100u)
#define MSD_DATABUF  DMW(0xD6000u)
int msd_ok=0, msd_slot=0, msd_in_dci=0, msd_out_dci=0;
static u32 msd_in_enq=0, msd_in_cyc=1, msd_out_enq=0, msd_out_cyc=1, msd_tag=1;
u32 msd_lastlba=0, msd_bsize=512;
static void ring_init(u32 ring){ volatile u32* r=(volatile u32*)(u64)ring; for(int i=0;i<256*4;i++)r[i]=0;
    int li=255*4; r[li]=ring; r[li+1]=0; r[li+2]=0; r[li+3]=(6u<<10)|(1u<<1)|1u; }
static int xh_bulk(int dci,u32 ring,u32* enq,u32* cyc,u32 buf,int len){
    volatile u32* tr=(volatile u32*)(u64)(ring+(*enq)*16);
    tr[0]=buf; tr[1]=0; tr[2]=(u32)len; tr[3]=(1u<<10)|(1u<<5)|(*cyc);   /* Normal TRB, IOC */
    (*enq)++; if(*enq>=255){ volatile u32* lk=(volatile u32*)(u64)(ring+255*16); lk[3]=(lk[3]&~1u)|(*cyc); *enq=0; *cyc^=1; }
    xh_db(msd_slot,(u32)dci);
    u8 cc=0; xh_want_slot=msd_slot; xh_want_ep=dci; int ok=xh_event_t(32,0,&cc,0,0); xh_want_slot=-1; xh_want_ep=-1; if(!ok) return -1; return cc;
}
/* one Bulk-Only Transport command: CBW -> optional data -> CSW.  returns CSW status (0=good) or -1 */
static int msd_bot(const u8* cb,int cblen,u32 dbuf,int dlen,int dir_in){
    volatile u8* w=(volatile u8*)(u64)MSD_CBW; for(int i=0;i<31;i++)w[i]=0;
    *(volatile u32*)(u64)MSD_CBW=0x43425355u; *(volatile u32*)(u64)(MSD_CBW+4)=msd_tag++;
    *(volatile u32*)(u64)(MSD_CBW+8)=(u32)dlen; w[12]=dir_in?0x80:0x00; w[13]=0; w[14]=(u8)cblen;
    for(int i=0;i<cblen&&i<16;i++)w[15+i]=cb[i];
    if(xh_bulk(msd_out_dci,MSD_OUTRING,&msd_out_enq,&msd_out_cyc,MSD_CBW,31)<0) return -1;
    if(dlen){ int cc=xh_bulk(dir_in?msd_in_dci:msd_out_dci, dir_in?MSD_INRING:MSD_OUTRING,
                             dir_in?&msd_in_enq:&msd_out_enq, dir_in?&msd_in_cyc:&msd_out_cyc, dbuf, dlen); if(cc<0)return -1; }
    if(xh_bulk(msd_in_dci,MSD_INRING,&msd_in_enq,&msd_in_cyc,MSD_CSW,13)<0) return -1;
    if(*(volatile u32*)(u64)MSD_CSW != 0x53425355u) return -1;   /* 'USBS' */
    return *(volatile u8*)(u64)(MSD_CSW+12);                      /* bCSWStatus */
}
static int scsi_tur(void){ u8 cb[6]={0,0,0,0,0,0}; return msd_bot(cb,6,0,0,1); }
static int scsi_capacity(void){ u8 cb[10]={0x25,0,0,0,0,0,0,0,0,0};
    if(msd_bot(cb,10,MSD_DATABUF,8,1)!=0)return -1; volatile u8* d=(volatile u8*)(u64)MSD_DATABUF;
    msd_lastlba=((u32)d[0]<<24)|((u32)d[1]<<16)|((u32)d[2]<<8)|d[3];
    msd_bsize=((u32)d[4]<<24)|((u32)d[5]<<16)|((u32)d[6]<<8)|d[7]; if(!msd_bsize)msd_bsize=512; return 0; }
/* read `cnt` 512-byte blocks from `lba` into `buf` */
static int scsi_read10(u32 lba,u16 cnt,u32 buf){
    u8 cb[10]={0x28,0,(u8)(lba>>24),(u8)(lba>>16),(u8)(lba>>8),(u8)lba,0,(u8)(cnt>>8),(u8)cnt,0};
    return msd_bot(cb,10,buf,cnt*512,1);
}
/* write `cnt` 512-byte blocks from `buf` to `lba` (used later for write/delete) */
static int scsi_write10(u32 lba,u16 cnt,u32 buf){
    u8 cb[10]={0x2A,0,(u8)(lba>>24),(u8)(lba>>16),(u8)(lba>>8),(u8)lba,0,(u8)(cnt>>8),(u8)cnt,0};
    return msd_bot(cb,10,buf,cnt*512,0);
}
/* enumerate a mass-storage device on `port` (separate slot from HID) */
static u32 msd_route=0; static int msd_depth=0;
static int xhci_msd_enum_loc(const usbloc_t* L);
static int xhci_msd_enum(int port){
    msd_ok=0; xhci_failstep[0]=0; xhci_failcc=0;
    int sp=xh_root_reset(port); if(!sp){ xh_set_fail("no device on port"); return 0; }
    usbloc_t L; L.root=port; L.depth=0; L.hub=-1; L.hport=0; L.speed=sp; L.route=0; L.tt_slot=0; L.tt_port=0;
    return xhci_msd_enum_loc(&L); }
static int xhci_msd_enum_loc(const usbloc_t* L){
    msd_ok=0; msd_slot=0; xhci_failstep[0]=0; xhci_failcc=0;
    #define MFAIL(m) do{ int _i=0; while(m[_i]&&_i<47){xhci_failstep[_i]=m[_i];_i++;} xhci_failstep[_i]=0; return 0; }while(0)
    int speed=L->speed; int mps0=(speed==3)?64:(speed>=4)?512:8;
    cur_ep0=MSD_EP0RING; cur_dbuf=MSD_DATABUF; e0_enq=0; e0_cyc=1; ring_init(MSD_EP0RING);
    u8 cc=0,sl=0; if(!xh_cmd_s(0,0,0,9,0,&cc,&sl)){xhci_failcc=cc; MFAIL("Enable Slot");} cur_slot=sl; msd_slot=sl; if(!sl) MFAIL("no slot");
    ctx_zero(MSD_DEVCTX,(int)ctx_sz()*8/4); *(volatile u32*)(u64)(XHCI_DCBAA+sl*8)=MSD_DEVCTX; *(volatile u32*)(u64)(XHCI_DCBAA+sl*8+4)=0;
    ctx_zero(MSD_INPUTCTX,(int)ctx_sz()*3/4); volatile u32* ic=(volatile u32*)(u64)MSD_INPUTCTX; ic[1]=(1u<<0)|(1u<<1);
    volatile u32* sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); xh_slot_ctx(sc,L,1);
    volatile u32* e0=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*2); e0[1]=(4u<<3)|(3u<<1)|((u32)mps0<<16); e0[2]=MSD_EP0RING|1u; e0[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,11,(u32)sl,&cc,&sl)){xhci_failcc=cc; MFAIL("Address Device");}
    for(volatile int dl=0;dl<20000;dl++) io_wait();
    if(!xh_fix_mps0(MSD_INPUTCTX,L,MSD_EP0RING)) MFAIL("GET device desc (8)");
    if(!xh_ctrl(0x80,6,0x0100,0,18,1,0)) MFAIL("GET device desc");
    if(!xh_ctrl(0x80,6,0x0200,0,9,1,0)) MFAIL("GET config(9)");
    u8 c9[9]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<9;i++)c9[i]=db[i]; }
    int wtot=c9[2]|(c9[3]<<8); if(wtot>256)wtot=256; u8 cfgval=c9[5];
    if(!xh_ctrl(0x80,6,0x0200,0,(u16)wtot,1,0)) MFAIL("GET config(full)");
    u8 full[256]; { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<wtot;i++)full[i]=db[i]; }
    int isMSD=0, in_ep=0,in_mps=512,out_ep=0,out_mps=512;
    for(int i=0;i+2<wtot;){ int blen=full[i],bt=full[i+1]; if(blen<2)break;
        if(bt==4){ isMSD=(full[i+5]==0x08); }                                  /* interface class 8 = mass storage */
        else if(bt==5 && isMSD){ int a=full[i+2]; int mps=full[i+4]|(full[i+5]<<8); if((full[i+3]&3)==2){ /* bulk */
            if(a&0x80){ in_ep=a; in_mps=mps; } else { out_ep=a; out_mps=mps; } } }
        i+=blen; }
    if(!in_ep||!out_ep) MFAIL("no bulk endpoints");
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)) MFAIL("SET_CONFIGURATION");
    int in_n=in_ep&0x0F, out_n=out_ep&0x0F; msd_in_dci=in_n*2+1; msd_out_dci=out_n*2;   /* IN=2n+1, OUT=2n */
    int maxdci=(msd_in_dci>msd_out_dci)?msd_in_dci:msd_out_dci;
    ctx_zero(MSD_INPUTCTX,(int)ctx_sz()*(maxdci+2)/4);
    ic=(volatile u32*)(u64)MSD_INPUTCTX; ic[1]=(1u<<0)|(1u<<msd_in_dci)|(1u<<msd_out_dci);
    sc=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()); xh_slot_ctx(sc,L,maxdci);
    ring_init(MSD_INRING); ring_init(MSD_OUTRING); msd_in_enq=0; msd_in_cyc=1; msd_out_enq=0; msd_out_cyc=1;
    volatile u32* epi=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_in_dci+1));   /* bulk IN: type 6 */
    epi[1]=(6u<<3)|(3u<<1)|((u32)in_mps<<16); epi[2]=MSD_INRING|1u; epi[3]=0;
    volatile u32* epo=(volatile u32*)(u64)(MSD_INPUTCTX+ctx_sz()*(msd_out_dci+1));  /* bulk OUT: type 2 */
    epo[1]=(2u<<3)|(3u<<1)|((u32)out_mps<<16); epo[2]=MSD_OUTRING|1u; epo[3]=0;
    if(!xh_cmd_s(MSD_INPUTCTX,0,0,12,(u32)msd_slot,&cc,&sl)){xhci_failcc=cc; MFAIL("Configure Endpoint");}
    for(int t=0;t<8;t++){ if(scsi_tur()==0)break; for(volatile int dl=0;dl<40000;dl++)io_wait(); }   /* wait ready */
    if(scsi_capacity()!=0) MFAIL("READ CAPACITY");
    msd_route=L->route; msd_depth=L->depth; msd_ok=1; return 1;
    #undef MFAIL
}
static void xhci_port_refresh(void){ if(!xhci_op)return; for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+(u32)p*0x10); if((v&1)&&!xhci_port_conn[p]){ xhci_port_conn[p]=1; xhci_port_speed[p]=(u8)((v>>10)&0xF); } } }
int msd_autoinit(void){ if(msd_ok){ xhc_select(msd_ctl); return 1; } if(!g_xhc_n){ xhci_init(); if(!g_xhc_n)return 0; }
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh(); for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p])continue; if(xhci_msd_enum(p)){ msd_ctl=c; return 1; } } } return 0; }
/* ---- FAT filesystem over the USB stick: read / write / delete
       (ported from the proven Noovex9 32-bit implementation; namespaced ufat_* to avoid the IDE FAT reader) ---- */
static int umsd_read(u32 lba,int cnt,u8* out){ if(!msd_ok)return -1; xhc_select(msd_ctl); if(scsi_read10(lba,(u16)cnt,MSD_DATABUF)!=0)return -1;
    volatile u8* s=(volatile u8*)(u64)MSD_DATABUF; for(int i=0;i<cnt*512;i++)out[i]=s[i]; return cnt*512; }
static int umsd_write(u32 lba,int cnt,const u8* in){ if(!msd_ok)return -1; xhc_select(msd_ctl); volatile u8* d=(volatile u8*)(u64)MSD_DATABUF;
    for(int i=0;i<cnt*512;i++)d[i]=in[i]; if(scsi_write10(lba,(u16)cnt,MSD_DATABUF)!=0)return -1; return cnt*512; }
int usb_msd_read512(u32 lba,u8* out){ return umsd_read(lba,1,out)==512?0:-1; }
u64 usb_msd_sectors(void){ return msd_ok?(u64)msd_lastlba+1:0; }
int usb_msd_read(u32 lba,int cnt,u8* out){ return umsd_read(lba,cnt,out)==cnt*512?0:-1; }
int ufat_ok=0, ufat_type=0, ufat_nfats=2; static u32 ufat_part_lba=0, ufat_fatstart=0, ufat_dataclus=0, ufat_rootclus=0, ufat_rootlba=0, ufat_rootsecs=0, ufat_spc=1, ufat_fatsz=0, ufat_resv=0;
struct nvx_ufs_t ufs[32]; int ufs_n=0;
static u8 ufatcache[512]; static u32 ufc_si=0xFFFFFFFFu; static int ufc_valid=0, ufc_dirty=0;
static void ufat_flush(void){ if(ufc_valid&&ufc_dirty&&ufat_ok){ for(int f=0;f<ufat_nfats;f++) umsd_write(ufat_fatstart+f*ufat_fatsz+ufc_si,1,ufatcache); ufc_dirty=0; } }
static int ufat_load(u32 si){ if(ufc_valid&&ufc_si==si)return 0; ufat_flush(); if(umsd_read(ufat_fatstart+si,1,ufatcache)<0){ufc_valid=0;return -1;} ufc_si=si; ufc_valid=1; ufc_dirty=0; return 0; }
static u32 ufat_next(u32 clus){
    if(ufat_type==16){ u32 off=clus*2; if(ufat_load(off/512)<0)return 0x0FFFFFFFu; u32 o=off%512; u32 v=ufatcache[o]|(ufatcache[o+1]<<8); return (v>=0xFFF8)?0x0FFFFFFFu:v; }
    u32 off=clus*4; if(ufat_load(off/512)<0)return 0x0FFFFFFFu; u32 o=off%512; u32 v=(ufatcache[o]|(ufatcache[o+1]<<8)|(ufatcache[o+2]<<16)|((u32)ufatcache[o+3]<<24))&0x0FFFFFFFu; return (v>=0x0FFFFFF8u)?0x0FFFFFFFu:v;
}
static int ufat_dir_sector(u8* sec){
    for(int e=0;e<16;e++){ u8* d=sec+e*32; if(d[0]==0)return 1; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F||(d[0x0B]&0x08))continue;
        if(ufs_n>=32)return 1;
        char nm[16]; int o=0; for(int k=0;k<8;k++) if(d[k]!=' ')nm[o++]=(char)d[k]; if(d[8]!=' '){nm[o++]='.'; for(int k=8;k<11;k++) if(d[k]!=' ')nm[o++]=(char)d[k];} nm[o]=0;
        u32 sz=(u32)d[0x1C]|((u32)d[0x1D]<<8)|((u32)d[0x1E]<<16)|((u32)d[0x1F]<<24);
        u32 clus=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16);
        int i=ufs_n,k=0; while(nm[k]&&k<15){ufs[i].name[k]=nm[k];k++;} ufs[i].name[k]=0; ufs[i].size=sz; ufs[i].clus=clus; ufs_n++;
    }
    return 0;
}
void ufat_mount(void){
    ufat_ok=0; ufs_n=0; ufc_valid=0; ufc_dirty=0; if(!msd_ok)return; u8 sec[512];
    if(umsd_read(0,1,sec)<0)return;
    u32 plba=0;
    if(sec[510]==0x55&&sec[511]==0xAA){ u8 ty=sec[450]; if(ty==0x0B||ty==0x0C||ty==0x0E||ty==0x06||ty==0x04||ty==0x01){ plba=sec[454]|(sec[455]<<8)|(sec[456]<<16)|((u32)sec[457]<<24); } }
    if(umsd_read(plba,1,sec)<0)return;
    int bps=sec[0x0B]|(sec[0x0C]<<8); if(bps!=512)return;
    ufat_spc=sec[0x0D]; if(ufat_spc==0)ufat_spc=1;
    ufat_resv=sec[0x0E]|(sec[0x0F]<<8); ufat_nfats=sec[0x10]; if(ufat_nfats==0)ufat_nfats=2;
    int rootent=sec[0x11]|(sec[0x12]<<8); u32 fatsz16=sec[0x16]|(sec[0x17]<<8);
    u32 fatsz32=sec[0x24]|(sec[0x25]<<8)|(sec[0x26]<<16)|((u32)sec[0x27]<<24);
    ufat_fatsz=fatsz16?fatsz16:fatsz32; u32 rootsecs=((rootent*32)+(bps-1))/bps;
    ufat_part_lba=plba; ufat_fatstart=plba+ufat_resv; ufat_dataclus=plba+ufat_resv+ufat_nfats*ufat_fatsz+rootsecs; ufat_rootsecs=rootsecs;
    if(fatsz16){ ufat_type=16; ufat_rootlba=plba+ufat_resv+ufat_nfats*ufat_fatsz; }
    else { ufat_type=32; ufat_rootclus=sec[0x2C]|(sec[0x2D]<<8)|(sec[0x2E]<<16)|((u32)sec[0x2F]<<24); }
    ufat_ok=1;
    if(ufat_type==16){ for(u32 s=0;s<rootsecs&&ufs_n<32;s++){ if(umsd_read(ufat_rootlba+s,1,sec)<0)break; if(ufat_dir_sector(sec))break; } }
    else { u32 clus=ufat_rootclus; int guard=0; while(clus>=2&&clus<0x0FFFFFF8u&&ufs_n<32&&guard++<256){ u32 base=ufat_dataclus+(clus-2)*ufat_spc; int stop=0; for(u32 s=0;s<ufat_spc&&ufs_n<32;s++){ if(umsd_read(base+s,1,sec)<0){stop=1;break;} if(ufat_dir_sector(sec)){stop=1;break;} } if(stop)break; clus=ufat_next(clus); } }
}
int ufat_read(int fi,char* out,int max){
    if(fi<0||fi>=ufs_n)return 0; u32 clus=ufs[fi].clus; int left=(int)ufs[fi].size; if(left>max)left=max; int got=0,guard=0; u8 sec[512];
    while(clus>=2&&clus<0x0FFFFFF8u&&left>0&&guard++<1048576){ u32 base=ufat_dataclus+(clus-2)*ufat_spc; int stop=0;
        for(u32 s=0;s<ufat_spc&&left>0;s++){ if(umsd_read(base+s,1,sec)<0){stop=1;break;} int n=(left<512)?left:512; for(int k=0;k<n;k++)out[got++]=(char)sec[k]; left-=n; }
        if(stop)break; clus=ufat_next(clus); }
    return got;
}
static void ufat_set(u32 clus,u32 val){
    if(ufat_type==16){ u32 off=clus*2; if(ufat_load(off/512)<0)return; u32 o=off%512; ufatcache[o]=val&0xFF; ufatcache[o+1]=(val>>8)&0xFF; ufc_dirty=1; }
    else { u32 off=clus*4; if(ufat_load(off/512)<0)return; u32 o=off%512; u32 cur=(ufatcache[o]|(ufatcache[o+1]<<8)|(ufatcache[o+2]<<16)|((u32)ufatcache[o+3]<<24)); cur=(cur&0xF0000000u)|(val&0x0FFFFFFFu); ufatcache[o]=cur&0xFF;ufatcache[o+1]=(cur>>8)&0xFF;ufatcache[o+2]=(cur>>16)&0xFF;ufatcache[o+3]=(cur>>24)&0xFF; ufc_dirty=1; }
}
static u32 ufat_alloc(void){ u32 maxc=(ufat_type==16)?(ufat_fatsz*256u):(ufat_fatsz*128u); if(maxc>65536)maxc=65536;
    for(u32 c=2;c<maxc;c++){ u32 v=ufat_next(c); if(v==0){ ufat_set(c, ufat_type==16?0xFFFFu:0x0FFFFFFFu); return c; } } return 0; }
static void ufat_free_chain(u32 clus){ int g=0; while(clus>=2&&clus<0x0FFFFFF8u&&g++<4096){ u32 nx=ufat_next(clus); ufat_set(clus,0); clus=nx; } }
static void ufat_make83(const char* nm,char out[11]){ for(int i=0;i<11;i++)out[i]=' ';
    int i=0,o=0; while(nm[i]&&nm[i]!='.'&&o<8){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[o++]=c; i++; }
    while(nm[i]&&nm[i]!='.')i++; if(nm[i]=='.'){ i++; int e=8; while(nm[i]&&e<11){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[e++]=c; i++; } } }
static int ufat_dirent_scan(const char* n83,int findfree,u32* olba,int* ooff){ u8 sec[512];
    if(ufat_type==16){ for(u32 s=0;s<ufat_rootsecs;s++){ u32 lba=ufat_rootlba+s; if(umsd_read(lba,1,sec)<0)return 0;
        for(int e=0;e<16;e++){ u8* d=sec+e*32;
            if(findfree){ if(d[0]==0x00||d[0]==0xE5){ *olba=lba; *ooff=e*32; return 1; } }
            else { if(d[0]==0x00)return 0; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F)continue; int m=1; for(int k=0;k<11;k++)if(d[k]!=(u8)n83[k]){m=0;break;} if(m){ *olba=lba; *ooff=e*32; return 1; } } } } return 0; }
    u32 clus=ufat_rootclus; int guard=0;
    while(clus>=2&&clus<0x0FFFFFF8u&&guard++<4096){ u32 base=ufat_dataclus+(clus-2)*ufat_spc;
        for(u32 s=0;s<ufat_spc;s++){ u32 lba=base+s; if(umsd_read(lba,1,sec)<0)return 0;
            for(int e=0;e<16;e++){ u8* d=sec+e*32;
                if(findfree){ if(d[0]==0x00||d[0]==0xE5){ *olba=lba; *ooff=e*32; return 1; } }
                else { if(d[0]==0x00)return 0; if(d[0]==0xE5||(d[0x0B]&0x0F)==0x0F)continue; int m=1; for(int k=0;k<11;k++)if(d[k]!=(u8)n83[k]){m=0;break;} if(m){ *olba=lba; *ooff=e*32; return 1; } } } }
        clus=ufat_next(clus); } return 0; }
int ufat_write_file(const char* name,const char* data,int len){
    if(!ufat_ok||!msd_ok)return -1; char n83[11]; ufat_make83(name,n83); u32 elba; int eoff; u8 sec[512];
    int exists=ufat_dirent_scan(n83,0,&elba,&eoff);
    if(exists){ if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff; u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc); }
    else { if(!ufat_dirent_scan(n83,1,&elba,&eoff))return -1; }
    int spb=ufat_spc*512; int nclus=(len+spb-1)/spb; if(nclus<1)nclus=1; u32 first=0,prev=0; int wr=0;
    for(int c=0;c<nclus;c++){ u32 cl=ufat_alloc(); if(cl==0)return -1; if(!first)first=cl; if(prev)ufat_set(prev,cl); prev=cl;
        u32 base=ufat_dataclus+(cl-2)*ufat_spc;
        for(u32 s=0;s<ufat_spc;s++){ u8 ds[512]; for(int k=0;k<512;k++)ds[k]=0; int n=len-wr; if(n>512)n=512; if(n<0)n=0; for(int k=0;k<n;k++)ds[k]=(u8)data[wr+k]; wr+=n; if(umsd_write(base+s,1,ds)<0)return -1; } }
    if(prev)ufat_set(prev, ufat_type==16?0xFFFFu:0x0FFFFFFFu);
    if(umsd_read(elba,1,sec)<0)return -1; u8* d=sec+eoff;
    for(int i=0;i<11;i++)d[i]=(u8)n83[i]; d[0x0B]=0x20; for(int i=0x0C;i<0x20;i++)d[i]=0;
    d[0x1A]=first&0xFF;d[0x1B]=(first>>8)&0xFF;d[0x14]=(first>>16)&0xFF;d[0x15]=(first>>24)&0xFF;
    d[0x1C]=len&0xFF;d[0x1D]=(len>>8)&0xFF;d[0x1E]=(len>>16)&0xFF;d[0x1F]=(len>>24)&0xFF;
    if(umsd_write(elba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
int ufat_delete_file(int idx){
    if(idx<0||idx>=ufs_n||!ufat_ok)return -1; char n83[11]; ufat_make83(ufs[idx].name,n83); u32 lba; int off; u8 sec[512];
    if(!ufat_dirent_scan(n83,0,&lba,&off))return -1; if(umsd_read(lba,1,sec)<0)return -1; u8* d=sec+off;
    u32 oc=(d[0x1A]|(d[0x1B]<<8))|((u32)(d[0x14]|(d[0x15]<<8))<<16); if(oc>=2)ufat_free_chain(oc);
    d[0]=0xE5; if(umsd_write(lba,1,sec)<0)return -1; ufat_flush(); ufat_mount(); return 0; }
/* ---- USB hotplug: auto-mount + auto-open the file manager when a stick is inserted ---- */
int g_files_usb=0, g_usb_arrived=0, msd_port=-1;static u8 usb_seen[32]={0}; static u64 usb_poll_t=0; static int usb_seen_init=0;
static u8* xhub_db(xhub_t* h){ return (u8*)(u64)XHID_DATABUF(h->reg); }
static int xhub_ctrl(xhub_t* h,u8 rt,u8 req,u16 val,u16 idx,u16 len,int in){
    int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
    cur_slot=h->slot; cur_ep0=XHID_EP0RING(h->reg); cur_dbuf=XHID_DATABUF(h->reg); e0_enq=h->e0_enq; e0_cyc=h->e0_cyc;
    int r=xh_ctrl(rt,req,val,idx,len,in,0);
    h->e0_enq=e0_enq; h->e0_cyc=e0_cyc;
    cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc; return r; }
static int xhub_status(xhub_t* h,int p,u16* st,u16* ch){ if(!xhub_ctrl(h,0xA3,0,0,(u16)p,4,1))return 0; u8* b=xhub_db(h); *st=(u16)(b[0]|(b[1]<<8)); *ch=(u16)(b[2]|(b[3]<<8)); return 1; }
static void xh_delay_ms(int ms){ for(int i=0;i<ms;i++) for(volatile int k=0;k<1000;k++) io_wait(); }
static int xh_under(u32 route,int depth,u32 R,int dR){ if(depth<dR)return 0; u32 m=dR>=5?0xFFFFFu:((1u<<(4*dR))-1u); return (route&m)==(R&m); }
static void xhub_remove_below(int root,u32 R,int dR){
    for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==g_xcur&&xhid_port[i]==root&&xh_under(xhid_route[i],xhid_depth[i],R,dR)){ int sl=xhid_slot[i];
        for(int k=0;k<xhid_n;k++) if(xhid_used[k]&&xhid_ctl[k]==g_xcur&&xhid_slot[k]==sl){ xhid_used[k]=0; if(usbk_rep_dev==k){ usbk_rep_code=0; usbk_rep_dev=-1; } for(int z=0;z<8;z++)xhid_kprev[k][z]=0; }
        xh_disable_slot(sl); }
    for(int h=0;h<XHUB_MAX;h++) if(xhub[h].used&&xhub[h].ctl==g_xcur&&xhub[h].root==root&&xh_under(xhub[h].route,xhub[h].depth,R,dR)){ xh_disable_slot(xhub[h].slot); xreg_hub[xhub[h].reg]=0; xhub[h].used=0; }
    if(msd_ok&&msd_ctl==g_xcur&&msd_port==root&&xh_under(msd_route,msd_depth,R,dR)){ xh_disable_slot(msd_slot); msd_ok=0; ufat_ok=0; ufs_n=0; msd_port=-1; g_files_usb=0; }
    int any=0; for(int i=0;i<xhid_n;i++) if(xhid_used[i])any=1; if(!any)xhci_mouse_ok=0; }
static int xhub_port_reset(int hi,int p){
    xhub_t* h=&xhub[hi]; u16 st=0,ch=0;
    if(!xhub_ctrl(h,0x23,3,4,(u16)p,0,0))return 0;
    for(int t=0;t<100;t++){ xh_delay_ms(5); if(!xhub_status(h,p,&st,&ch))return 0; if((ch&(1u<<4))||(!(st&(1u<<4))&&(st&2u)))break; }
    xhub_ctrl(h,0x23,1,20,(u16)p,0,0); if(ch&1u)xhub_ctrl(h,0x23,1,16,(u16)p,0,0);
    xh_delay_ms(10);
    if(!xhub_status(h,p,&st,&ch))return 0; if(!(st&1u)||!(st&2u))return 0;
    if(h->ss)return 4; if(st&(1u<<9))return 2; if(st&(1u<<10))return 3; return 1; }
static void xhub_child_loc(int hi,int p,int sp,usbloc_t* C){ xhub_t* h=&xhub[hi]; C->root=h->root; C->depth=h->depth+1; C->hub=hi; C->hport=p; C->speed=sp;
    C->route=h->route|((u32)(p>15?15:p)<<(4*h->depth));
    if(h->speed==3&&(sp==1||sp==2)){ C->tt_slot=h->slot; C->tt_port=p; } else { C->tt_slot=h->tt_slot; C->tt_port=h->tt_port; } }
static int xhub_enum_port(int hi,int p){
    if(xhub[hi].depth+1>5)return 0;
    int sp=xhub_port_reset(hi,p); if(!sp)return 0;
    usbloc_t C; xhub_child_loc(hi,p,sp,&C);
    if(!msd_ok){ if(xhci_msd_enum_loc(&C)){ msd_ctl=g_xcur; msd_port=C.root; ufat_mount(); if(ufat_ok)g_usb_arrived=1; klog("usb: USB stick behind a hub"); return 0; }
        xh_disable_slot(msd_slot); msd_slot=0; sp=xhub_port_reset(hi,p); if(!sp)return 0; xhub_child_loc(hi,p,sp,&C); }
    int r=xhci_enum_loc(&C); if(r>0)klog("usb: keyboard/mouse behind a hub set up"); return r>0?r:0; }
static int xhub_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval){
    int hi=-1; for(int h=0;h<XHUB_MAX;h++) if(!xhub[h].used){ hi=h; break; }
    if(hi<0||L->depth>=5){ xh_set_fail("too many USB hubs"); return -1; }
    int ep=0,mps=1,intv=12,inhub=0;
    for(int i=0;i+2<wtot;){ int bl=full[i]; if(bl<2)break; if(full[i+1]==4&&i+5<wtot)inhub=(full[i+5]==9); else if(full[i+1]==5&&inhub&&i+6<wtot&&(full[i+2]&0x80)&&(full[i+3]&3)==3&&!ep){ ep=full[i+2]; mps=full[i+4]|(full[i+5]<<8); intv=full[i+6]; } i+=bl; }
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)){ xh_set_fail("hub SET_CONFIGURATION"); return -1; }
    int ss=L->speed>=4;
    if(!xh_ctrl(0xA0,6,ss?0x2A00:0x2900,0,ss?12:9,1,0)){ xh_set_fail("hub descriptor"); return -1; }
    volatile u8* db=(volatile u8*)(u64)cur_dbuf; int np=db[2]; if(np>15)np=15; u16 chr=(u16)(db[3]|(db[4]<<8)); int p2g=db[5]*2;
    if(np<1){ xh_set_fail("hub reports no ports"); return -1; }
    xhub_t* h=&xhub[hi]; for(u32 i=0;i<sizeof(*h);i++)((u8*)h)[i]=0;
    h->ctl=g_xcur; h->reg=d; h->slot=cur_slot; h->root=L->root; h->depth=L->depth; h->speed=L->speed; h->route=L->route; h->tt_slot=L->tt_slot; h->tt_port=L->tt_port; h->parent=L->hub; h->pport=L->hport; h->nports=np; h->ttt=(chr>>5)&3; h->ss=ss;
    int dci=ep?(ep&0x0F)*2+1:1;
    ctx_zero(XHID_INPUTCTX(d),(int)ctx_sz()*(dci+2)/4);
    volatile u32* ic=(volatile u32*)(u64)XHID_INPUTCTX(d); ic[1]=1u|(ep?(1u<<dci):0u);
    volatile u32* sc=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()); xh_slot_ctx(sc,L,dci); sc[0]|=1u<<26; sc[1]|=(u32)np<<24; if(L->speed==3)sc[2]|=(u32)h->ttt<<16;
    if(ep){ volatile u32* er=(volatile u32*)(u64)XHID_EPINRING(d); for(int i=0;i<256*4;i++)er[i]=0; int li=255*4; er[li]=XHID_EPINRING(d); er[li+3]=(6u<<10)|(1u<<1)|1u;
        volatile u32* e=(volatile u32*)(u64)(XHID_INPUTCTX(d)+ctx_sz()*(u32)(dci+1)); e[0]=xh_interval(L->speed,intv)<<16; e[1]=(7u<<3)|(3u<<1)|((u32)mps<<16); e[2]=XHID_EPINRING(d)|1u; e[4]=(u32)mps|((u32)mps<<16); }
    u8 cc=0,sl=0; if(!xh_cmd_s(XHID_INPUTCTX(d),0,0,12,(u32)cur_slot,&cc,&sl)){ xhci_failcc=cc; xh_set_fail("hub Configure Endpoint"); return -1; }
    if(ss)xh_ctrl(0x20,12,(u16)L->depth,0,0,0,0);
    h->e0_enq=e0_enq; h->e0_cyc=e0_cyc; h->used=1; xreg_hub[d]=1;
    for(int p=1;p<=np;p++)xhub_ctrl(h,0x23,3,8,(u16)p,0,0);
    xh_delay_ms(p2g<100?100:p2g);
    klog("usb: hub set up");
    int got=0;
    for(int p=1;p<=np;p++){ u16 st=0,ch=0; if(!xhub_status(&xhub[hi],p,&st,&ch))continue; if(ch&1u)xhub_ctrl(&xhub[hi],0x23,1,16,(u16)p,0,0);
        if(st&1u){ xhub[hi].conn|=1u<<p; got+=xhub_enum_port(hi,p); } }
    return got; }
static void xhub_poll_ports(void){
    for(int hi=0;hi<XHUB_MAX;hi++){ if(!xhub[hi].used||xhub[hi].ctl!=g_xcur)continue;
        for(int p=1;p<=xhub[hi].nports;p++){ xhub_t* h=&xhub[hi]; u16 st=0,ch=0; if(!xhub_status(h,p,&st,&ch))break;
            if(ch&1u)xhub_ctrl(h,0x23,1,16,(u16)p,0,0);
            int c=st&1u, was=(int)((h->conn>>p)&1u);
            u32 R=h->route|((u32)(p>15?15:p)<<(4*h->depth));
            if(was&&(!c||(ch&1u))){ h->conn&=~(1u<<p); xhub_remove_below(h->root,R,h->depth+1); }
            if(c&&!((xhub[hi].conn>>p)&1u)){ xhub[hi].conn|=1u<<p; xhub_enum_port(hi,p); }
            if(!xhub[hi].used)break; } } }
usbdev_t g_usbdev[USBDEV_MAX]; int g_usbdev_n=0; u32 g_usbdev_gen=0;
static const char* usb_cls_name(int c,int proto){ switch(c){ case 1: return "Audio device"; case 2: return "Network adapter"; case 3: return proto==1?"Keyboard":proto==2?"Mouse":"Input device"; case 6: return "Camera (still image)"; case 7: return "Printer"; case 8: return "USB drive"; case 9: return "USB hub"; case 0x0B: return "Card reader"; case 0x0E: return "Camera"; case 0xE0: return "Bluetooth adapter"; case 0xEF: return "Device"; case 0xFF: return "Vendor device"; } return "USB device"; }
void usbdev_note(int port,u16 vid,u16 pid,const u8* cfg,int n,const char* name){
    int kind=-1, kbd=0, mouse=0, cam=0, prn=0;
    for(int i=0;i+8<n;){ int bl=cfg[i]; if(bl<2)break; if(cfg[i+1]==4){ int c=cfg[i+5], pr=cfg[i+7]; if(c==3&&pr==1)kbd=1; else if(c==3&&pr==2)mouse=1; else if(c==0x0E)cam=1; else if(c==7)prn=1; if(kind<0)kind=c; } i+=bl; }
    int slot=-1; for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==g_xcur&&g_usbdev[k].port==port&&g_usbdev[k].vid==vid&&g_usbdev[k].pid==pid){ slot=k; break; }
    if(slot<0) for(int k=0;k<USBDEV_MAX;k++) if(!g_usbdev[k].used){ slot=k; break; } if(slot<0)return;
    usbdev_t* d=&g_usbdev[slot]; int fresh=!d->used; d->used=1; d->ctl=g_xcur; d->port=port; d->vid=vid; d->pid=pid;
    const char* kn=cam?"Camera":prn?"Printer":(kbd&&mouse)?"Keyboard and mouse":kbd?"Keyboard":mouse?"Mouse":usb_cls_name(kind,0); int q=0; for(;kn[q]&&q<31;q++)d->kind[q]=kn[q]; d->kind[q]=0;
    q=0; for(;name&&name[q]&&q<39;q++)d->name[q]=name[q]; d->name[q]=0; d->cam=cam; d->prn=prn; d->hid=(kbd||mouse); d->t=g_ticks;
    if(slot>=g_usbdev_n)g_usbdev_n=slot+1; g_usbdev_gen++;
    if(fresh&&usb_seen_init){ extern void notify(const char*,const char*); char b[80],*p=b; p=scopy(p,d->kind); p=scopy(p," connected"); *p=0; notify(b,d->name[0]?d->name:"USB"); } }
void usbdev_gone(int port){ for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==g_xcur&&g_usbdev[k].port==port){ g_usbdev[k].used=0; g_usbdev_gen++; } }
int msd_autoinit_safe(void){ if(msd_ok)return 1; if(!g_xhc_n)return 0;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
        for(int p=0;p<xhci_ports;p++){ if(!xhci_port_conn[p])continue; int claimed=0; for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p&&(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn))claimed=1; if(claimed)continue;
            if(xhci_msd_enum(p)){ msd_ctl=c; msd_port=p; ufat_mount(); return 1; } xh_disable_slot(msd_slot); msd_slot=0; } }
    return 0; }
static void usb_hotplug_poll_one(void){
    if(!xhci_present||!xhci_init_ok)return;
    if(!usb_seen_init){ for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); usb_seen[p]=(u8)((v&1)&&xhci_port_conn[p]); } usb_seen_init=1; return; }
    for(int p=0;p<xhci_ports;p++){ u32 v=XMR(xhci_op+0x400+p*0x10); int cc=v&1;
        if(cc && !usb_seen[p]){ usb_seen[p]=1; xhci_port_conn[p]=1; int done=0;
            if(!msd_ok){ if(xhci_msd_enum(p)){ msd_ctl=g_xcur; ufat_mount(); if(ufat_ok){ g_usb_arrived=1; msd_port=p; } done=1; } else { xh_disable_slot(msd_slot); msd_slot=0; } }
            if(!done&&xhid_free_region(-1,-1)>=0){ if(xhci_hid_enum(p))klog("usb: keyboard/mouse connected"); } }
        else if(!cc && usb_seen[p]){ usb_seen[p]=0; xhci_port_conn[p]=0;
            if(msd_ok && msd_ctl==g_xcur && msd_port==p){ msd_ok=0; ufat_ok=0; ufs_n=0; msd_port=-1; g_files_usb=0; }
            xhid_remove_port(p); usbdev_gone(p); uvc_unplug(p); }
    }
    xhub_poll_ports();
}
typedef struct { int present, init_ok, slots, ports, ports_hw, pci_bus, pci_slot, pci_fn, hi4g, csz, seen_init; u16 vendor; u64 base, op; u32 dboff, rtoff, mem, cmd_enq, cmd_cyc, ev_deq, ev_cyc; u8 conn[32], speed[32], seen[32]; } xhc_t;
static xhc_t g_xhc[XHC_MAX];
static void xhc_save(void){ xhc_t* c=&g_xhc[g_xcur]; c->present=xhci_present; c->init_ok=xhci_init_ok; c->slots=xhci_slots; c->ports=xhci_ports; c->ports_hw=xhci_ports_hw; c->pci_bus=xhci_pci_bus; c->pci_slot=xhci_pci_slot; c->pci_fn=xhci_pci_fn; c->hi4g=xhci_hi4g; c->csz=xhci_csz; c->seen_init=usb_seen_init;
    c->vendor=xhci_vendor; c->base=xhci_base; c->op=xhci_op; c->dboff=xhci_dboff; c->rtoff=xhci_rtoff; c->mem=g_xmem; c->cmd_enq=cmd_enq; c->cmd_cyc=cmd_cyc; c->ev_deq=ev_deq; c->ev_cyc=ev_cyc;
    for(int i=0;i<32;i++){ c->conn[i]=xhci_port_conn[i]; c->speed[i]=xhci_port_speed[i]; c->seen[i]=usb_seen[i]; } }
static void xhc_load(int k){ xhc_t* c=&g_xhc[k]; xhci_present=c->present; xhci_init_ok=c->init_ok; xhci_slots=c->slots; xhci_ports=c->ports; xhci_ports_hw=c->ports_hw; xhci_pci_bus=c->pci_bus; xhci_pci_slot=c->pci_slot; xhci_pci_fn=c->pci_fn; xhci_hi4g=c->hi4g; xhci_csz=c->csz; usb_seen_init=c->seen_init;
    xhci_vendor=c->vendor; xhci_base=c->base; xhci_op=c->op; xhci_dboff=c->dboff; xhci_rtoff=c->rtoff; g_xmem=c->mem; cmd_enq=c->cmd_enq; cmd_cyc=c->cmd_cyc; ev_deq=c->ev_deq; ev_cyc=c->ev_cyc;
    for(int i=0;i<32;i++){ xhci_port_conn[i]=c->conn[i]; xhci_port_speed[i]=c->speed[i]; usb_seen[i]=c->seen[i]; } }
void xhc_select(int i){ if(i<0||i>=XHC_MAX||i==g_xcur)return; xhc_save(); g_xcur=i; xhc_load(i); }
static int xhci_scan_ctrls(int* bus,int* slot,int* fn,int max){
    int n=0;
    for(int b=0;b<256&&n<max;b++) for(int d=0;d<32&&n<max;d++){
        u32 id=pci_cfg(b,d,0,0); if((id&0xFFFF)==0xFFFF)continue;
        int multi=(pci_cfg(b,d,0,0x0C)>>16)&0x80;
        for(int f=0;f<(multi?8:1)&&n<max;f++){
            if(f){ u32 i2=pci_cfg(b,d,f,0); if((i2&0xFFFF)==0xFFFF)continue; }
            u32 cl=pci_cfg(b,d,f,0x08); if((cl>>8)==0x0C0330u){ bus[n]=b; slot[n]=d; fn[n]=f; n++; } } }
    return n;
}
static void xhci_init(void){
    int bus[XHC_MAX], slot[XHC_MAX], fn[XHC_MAX]; int n=xhci_scan_ctrls(bus,slot,fn,XHC_MAX);
    if(g_xcur!=0){ xhc_save(); g_xcur=0; xhc_load(0); }
    for(int k=0;k<XHC_MAX;k++){ g_xhc[k].present=0; g_xhc[k].init_ok=0; g_xhc[k].seen_init=0; }
    for(int k=0;k<n;k++){ xhc_select(k); g_xmem=k==0?DMW(0x10000u):xhc_mem_for(k-1); usb_seen_init=0; if(!g_xmem){ klog("usb: no DMA memory left for another xHCI controller"); continue; }
        xhci_init_one(bus[k],slot[k],fn[k]); cmd_enq=0; cmd_cyc=1; ev_deq=0; ev_cyc=1;
        if(xhci_init_ok){ xhci_csz=(XMR(xhci_base+0x10)>>2)&1; xhci_dboff=XMR(xhci_base+0x14)&~3u; xhci_rtoff=XMR(xhci_base+0x18)&~0x1Fu; } }
    g_xhc_n=n; xhc_select(0); if(!n){ xhci_present=0; xhci_init_ok=0; }
    klog(n>1?"usb: several xHCI controllers found":n?"usb: one xHCI controller found":"usb: no xHCI controller found");
}
void usb_hotplug_poll(void){
    if(!g_xhc_n)return;
    if(g_ticks-usb_poll_t<80)return; usb_poll_t=g_ticks;
    for(int c=0;c<g_xhc_n;c++){ xhc_select(c); usb_hotplug_poll_one(); }
}
typedef struct { u8 alt, ep, intv, burst, mult; u16 mps; u32 esit; } uvc_alt_t;
typedef struct { u8 kind, fi, ri; u16 w, h; u32 defint; } uvc_fr_t;
static struct { int ok, ctl, reg, slot, speed, root, vc_if, vs_if, bcd, plen, bulk, nalt, nfr, cur_alt, dci, maxp, burst, mult, esit, ss, ntd, bufsz; u32 e0_enq, e0_cyc; uvc_alt_t alt[24]; uvc_fr_t fr[64]; } cam;
int uvc_state=0, uvc_w=0, uvc_h=0, uvc_kind=0, uvc_fi=0, uvc_ri=0, uvc_altsel=0, uvc_lastcc=0, uvc_cfgcc=0, uvc_maxintr=0;
u32 uvc_maxpay=0, uvc_maxfs=0, uvc_interval=0, uvc_events=0, uvc_tds=0, uvc_tderr=0, uvc_frames=0, uvc_badframes=0, uvc_short=0, uvc_bytes=0, uvc_seq=0, uvc_fps10=0, uvc_jlen=0;
u16 uvc_vid=0, uvc_pid=0; char uvc_name[40]="", uvc_msg[96]="";
u32* uvc_rgb=0; u8* uvc_jpg=0; volatile int uvc_jnew=0;
static u8 *uvc_fb=0, *uvc_bufs=0, *uvc_ermem=0; static volatile u32 *uvc_ring=0, *uvc_er=0, *uvc_erst=0;
static u32 uvc_enq=0, uvc_deq=0, uvc_cyc=1, uvc_out=0, uvc_ev1=0, uvc_ev1c=1, uvc_fill=0, uvc_fcap=0; static int uvc_fbad=0, uvc_lfid=-1; static u64 uvc_ft0=0; static u32 uvc_fc0=0;
static void uvc_m(const char* s){ int i=0; while(s[i]&&i<95){ uvc_msg[i]=s[i]; i++; } uvc_msg[i]=0; }
static u32 uvc_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    if(!cam.ok)return 0; int sv=g_xcur; xhc_select(cam.ctl);
    u32 s0=cur_ep0,s1=cur_dbuf,s2=e0_enq,s3=e0_cyc; int s4=cur_slot;
    cur_ep0=XHID_EP0RING(cam.reg); cur_dbuf=XHID_DATABUF(cam.reg); e0_enq=cam.e0_enq; e0_cyc=cam.e0_cyc; cur_slot=cam.slot;
    if(!in&&len&&data){ volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<len;i++)db[i]=data[i]; }
    int r=xh_ctrl(rt,req,val,idx,len,in,in?data:0);
    cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; cur_ep0=s0; cur_dbuf=s1; e0_enq=s2; e0_cyc=s3; cur_slot=s4; xhc_select(sv); return r; }
static int uvc_has_video(const u8* c,int n){ for(int i=0;i+7<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&c[i+5]==0x0E)return 1; if(c[i+1]==0x0B&&c[i+4]==0x0E)return 1; i+=bl; } return 0; }
static void uvc_parse(const u8* c,int n){
    int cif=-1,calt=0,ccls=0,csub=0,fk=-1,fidx=0,lastalt=-1;
    cam.vc_if=-1; cam.vs_if=-1; cam.bcd=0x0100; cam.nalt=0; cam.nfr=0; cam.bulk=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4&&bl>=9){ cif=c[i+2]; calt=c[i+3]; ccls=c[i+5]; csub=c[i+6]; lastalt=-1;
            if(ccls==14&&csub==1&&cam.vc_if<0)cam.vc_if=cif;
            if(ccls==14&&csub==2&&cam.vs_if<0)cam.vs_if=cif; }
        else if(t==0x24&&ccls==14&&cif==cam.vc_if&&cam.vc_if>=0&&bl>=5&&c[i+2]==1) cam.bcd=c[i+3]|(c[i+4]<<8);
        else if(t==0x24&&ccls==14&&cif==cam.vs_if&&cam.vs_if>=0&&calt==0){ int st=c[i+2];
            if(st==4&&bl>=21){ fidx=c[i+3]; const u8* g=c+i+5; fk=(g[0]=='Y'&&g[1]=='U'&&g[2]=='Y'&&g[3]=='2')?0:(g[0]=='N'&&g[1]=='V'&&g[2]=='1'&&g[3]=='2')?2:-1; }
            else if(st==6&&bl>=11){ fidx=c[i+3]; fk=1; }
            else if((st==5||st==7)&&bl>=26&&fk>=0&&cam.nfr<64){ uvc_fr_t* f=&cam.fr[cam.nfr++]; f->kind=(u8)fk; f->fi=(u8)fidx; f->ri=c[i+3]; f->w=(u16)(c[i+5]|(c[i+6]<<8)); f->h=(u16)(c[i+7]|(c[i+8]<<8)); f->defint=uvc_r32(c+i+21); }
            else if(st==4||st==6||st==0x10||st==0x0C) fk=-1; }
        else if(t==5&&bl>=7&&ccls==14&&cif==cam.vs_if&&cam.vs_if>=0){ int ep=c[i+2], at=c[i+3]&3; u16 mps=(u16)(c[i+4]|(c[i+5]<<8));
            if((ep&0x80)&&at==1&&calt>0&&cam.nalt<24){ uvc_alt_t* a=&cam.alt[cam.nalt]; a->alt=(u8)calt; a->ep=(u8)ep; a->intv=c[i+6]; a->mps=mps; a->burst=(u8)((mps>>11)&3); a->mult=0; a->esit=(u32)(mps&0x7FF)*(u32)(((mps>>11)&3)+1); lastalt=cam.nalt; cam.nalt++; }
            else if((ep&0x80)&&at==2)cam.bulk=1; }
        else if(t==0x30&&bl>=6&&lastalt>=0&&cam.speed>=4){ uvc_alt_t* a=&cam.alt[lastalt]; a->burst=c[i+2]; a->mult=(u8)(c[i+3]&3); u32 b=(u32)(c[i+4]|(c[i+5]<<8)); a->esit=b?b:(u32)(a->mps&0x7FF)*(a->burst+1u)*(a->mult+1u); }
        i+=bl; }
    cam.plen=cam.bcd>=0x0150?48:cam.bcd>=0x0110?34:26; }
static int uvc_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm){
    cam.ok=0; cam.ctl=g_xcur; cam.reg=d; cam.slot=cur_slot; cam.speed=L->speed; cam.root=L->root; cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; cam.cur_alt=0; cam.dci=0;
    uvc_vid=vid; uvc_pid=pid; int q=0; for(;nm&&nm[q]&&q<39;q++)uvc_name[q]=nm[q]; uvc_name[q]=0; if(!uvc_name[0]){ const char* g="USB camera"; for(q=0;g[q];q++)uvc_name[q]=g[q]; uvc_name[q]=0; }
    uvc_parse(full,wtot);
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)){ uvc_m("camera: SET_CONFIGURATION failed"); return 0; }
    cam.e0_enq=e0_enq; cam.e0_cyc=e0_cyc; cam.ok=1; uvc_state=1;
    if(cam.vs_if>=0) uvc_req(0x01,0x0B,0,(u16)cam.vs_if,0,0,0);
    char b[96],*p=b; p=scopy(p,"camera: "); p=scopy(p,uvc_name); p=scopy(p,", "); p=udec(p,(u32)cam.nfr); p=scopy(p," sizes, "); p=udec(p,(u32)cam.nalt); p=scopy(p," stream settings"); *p=0; klog(b); uvc_m("Camera found");
    return 1; }
int uvc_found(void){ return cam.ok; }
int uvc_nfr(void){ return cam.nfr; }
int uvc_frame_info(int i,int* kind,int* w,int* h,u32* defint){ if(i<0||i>=cam.nfr)return 0; *kind=cam.fr[i].kind; *w=cam.fr[i].w; *h=cam.fr[i].h; *defint=cam.fr[i].defint; return 1; }
int uvc_speed(void){ return cam.speed; }
int uvc_nalt(void){ return cam.nalt; }
int uvc_is_bulk(void){ return cam.bulk&&!cam.nalt; }
void uvc_gone(void){ cam.ok=0; uvc_state=0; uvc_m("Camera disconnected"); }
static u8* uvc_al(u32 n,u32 al){ u8* p=(u8*)kmalloc((unsigned long)n+(unsigned long)al); if(!p)return 0; u64 a=((u64)p+al-1)&~(u64)(al-1); if(a+n>0xFFFFFFFFULL){ kfree(p); return 0; } return (u8*)a; }
static int uvc_cfg_ep(int add,const uvc_alt_t* a){
    u32 ic0=XHID_INPUTCTX(cam.reg); ctx_zero(ic0,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)ic0;
    int dci=add?((a->ep&0x0F)*2+1):cam.dci; if(dci<2)return 1;
    ic[0]=cam.cur_alt?(1u<<cam.dci):0u; ic[1]=1u|(add?(1u<<dci):0u);
    volatile u32* os=(volatile u32*)(u64)XHID_DEVCTX(cam.reg); volatile u32* sc=(volatile u32*)(u64)(ic0+ctx_sz());
    for(int k=0;k<4;k++)sc[k]=os[k]; u32 ent=(sc[0]>>27)&31u; if(add&&(u32)dci>ent)ent=(u32)dci; sc[0]=(sc[0]&~(31u<<27))|(ent<<27); sc[3]=0;
    if(add){ volatile u32* ep=(volatile u32*)(u64)(ic0+ctx_sz()*(u32)(dci+1));
        u32 iv; if(cam.speed>=3){ int v=a->intv-1; if(v<0)v=0; if(v>15)v=15; iv=(u32)v; } else { int v=a->intv+2; if(v<3)v=3; if(v>18)v=18; iv=(u32)v; }
        u32 maxp=(u32)(a->mps&0x7FF), es=a->esit;
        ep[0]=(iv<<16)|((es>>16)<<24)|(cam.speed>=4?((u32)a->mult<<8):0u);
        ep[1]=(5u<<3)|((u32)a->burst<<8)|(maxp<<16);
        ep[2]=(u32)(u64)uvc_ring|1u; ep[3]=0; ep[4]=(es&0xFFFFu)|((es&0xFFFFu)<<16); }
    u8 cc=0,sl=0; int sv=g_xcur; xhc_select(cam.ctl); xh_cmd_s(ic0,0,0,12,(u32)cam.slot,&cc,&sl); xhc_select(sv);
    uvc_cfgcc=cc; if(cc!=1)return 0;
    if(add){ cam.cur_alt=a->alt; cam.dci=dci; } else { cam.cur_alt=0; }
    return 1; }
static void uvc_td(void){
    volatile u32* t=uvc_ring+uvc_enq*4; u32 b=(u32)(u64)(uvc_bufs+(u64)uvc_enq*cam.bufsz); u32 len=(u32)cam.esit; u32 mp=(u32)cam.maxp; if(!mp)mp=1;
    u32 tdpc=(len+mp-1)/mp; if(!tdpc)tdpc=1; u32 tbc=0, tl=tdpc-1;
    if(cam.ss){ u32 mb=(u32)cam.burst+1u; tbc=(tdpc+mb-1)/mb-1u; u32 r=tdpc%mb; tl=r?r-1u:mb-1u; }
    t[0]=b; t[1]=0; t[2]=len|(1u<<22); __sync_synchronize();
    t[3]=uvc_cyc|(1u<<2)|(1u<<5)|(5u<<10)|((tbc&3u)<<7)|((tl&15u)<<16)|(1u<<31);
    uvc_enq++; if(uvc_enq>=(u32)cam.ntd){ volatile u32* lk=uvc_ring+(u32)cam.ntd*4; lk[3]=(lk[3]&~1u)|uvc_cyc; uvc_enq=0; uvc_cyc^=1; }
    uvc_out++; }
static void uvc_frame_done(void){
    if(!uvc_fbad&&uvc_fill>0){
        if(uvc_kind==1){ if(uvc_jpg&&!uvc_jnew){ const u8* s=uvc_fb; u32 n=uvc_fill; int dht=0; for(u32 i=0;i+1<n;i++){ if(s[i]==0xFF&&s[i+1]==0xC4){dht=1;break;} if(s[i]==0xFF&&s[i+1]==0xDA)break; }
                extern const u8 uvc_std_dht[]; extern const int uvc_std_dht_len; u32 o=0;
                if(!dht&&n>2){ uvc_jpg[0]=0xFF; uvc_jpg[1]=0xD8; o=2; for(int k=0;k<uvc_std_dht_len;k++)uvc_jpg[o++]=uvc_std_dht[k]; for(u32 i=2;i<n;i++)uvc_jpg[o++]=s[i]; }
                else { for(u32 i=0;i<n;i++)uvc_jpg[o++]=s[i]; }
                uvc_jlen=o; uvc_jnew=1; } uvc_frames++; }
        else if(uvc_kind==0){ u32 need=(u32)uvc_w*(u32)uvc_h*2u; if(uvc_fill>=need){ const u8* s=uvc_fb; u32* d=uvc_rgb; u32 np=(u32)uvc_w*(u32)uvc_h;
                for(u32 i=0;i<np;i+=2){ int y0=s[0]-16,u=s[1]-128,y1=s[2]-16,v=s[3]-128; s+=4; int rv=409*v, gv=-100*u-208*v, bv=516*u;
                    for(int k=0;k<2;k++){ int c=298*(k?y1:y0); int r=(c+rv+128)>>8, g=(c+gv+128)>>8, bb=(c+bv+128)>>8; if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(bb<0)bb=0; if(bb>255)bb=255; d[i+k]=((u32)r<<16)|((u32)g<<8)|(u32)bb; } }
                uvc_frames++; uvc_seq++; } else uvc_short++; }
        else { u32 np=(u32)uvc_w*(u32)uvc_h; if(uvc_fill>=np*3u/2u){ const u8* Y=uvc_fb; const u8* C=uvc_fb+np; u32* d=uvc_rgb;
                for(int y=0;y<uvc_h;y++) for(int x=0;x<uvc_w;x++){ int yy=Y[y*uvc_w+x]-16; const u8* cc=C+(y>>1)*uvc_w+(x&~1); int u=cc[0]-128,v=cc[1]-128; int c=298*yy;
                    int r=(c+409*v+128)>>8, g=(c-100*u-208*v+128)>>8, bb=(c+516*u+128)>>8; if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(bb<0)bb=0; if(bb>255)bb=255; d[y*uvc_w+x]=((u32)r<<16)|((u32)g<<8)|(u32)bb; }
                uvc_frames++; uvc_seq++; } else uvc_short++; } }
    else if(uvc_fill>0) uvc_badframes++;
    uvc_fill=0; uvc_fbad=0; }
static void uvc_payload(const u8* b,int n){
    if(!b||n<2)return; int hl=b[0]; if(hl<2||hl>n)return; int info=b[1]; int fid=info&1;
    if(uvc_lfid>=0&&fid!=uvc_lfid&&uvc_fill>0)uvc_frame_done(); uvc_lfid=fid;
    if(info&0x40)uvc_fbad=1;
    int dn=n-hl; if(dn>0){ if(uvc_fill+(u32)dn<=uvc_fcap){ const u8* s=b+hl; u8* d=uvc_fb+uvc_fill; for(int i=0;i<dn;i++)d[i]=s[i]; uvc_fill+=(u32)dn; uvc_bytes+=(u32)dn; } else uvc_fbad=1; }
    if(info&2)uvc_frame_done(); }
static int uvc_pref(const uvc_fr_t* f,int hd){
    if(f->w>1920||f->h>1200||f->w<16||f->h<16)return 99; int raw=f->kind!=1;
    if(hd){ if(!raw&&f->w<=1280)return 0; if(raw&&f->w<=640)return 1; if(!raw)return 2; return 3; }
    if(raw&&f->w<=640)return 0; if(!raw&&f->w<=640)return 1; if(!raw)return 2; return 3; }
static int uvc_er_done=0;
static void uvc_er_init(void){
    if(uvc_er_done){ int n=0; for(int g=0;g<8192;g++){ volatile u32* e=uvc_er+uvc_ev1*4; if((e[3]&1u)!=uvc_ev1c)break; uvc_ev1++; if(uvc_ev1>=4096){ uvc_ev1=0; uvc_ev1c^=1; } n++; }
        if(n){ u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x18,((u32)(u64)(uvc_er+uvc_ev1*4))|(1u<<3)); XMW(rt+0x1C,0); } return; }
    uvc_er_done=1;
    for(int i=0;i<4096*4;i++)uvc_er[i]=0; uvc_erst[0]=(u32)(u64)uvc_er; uvc_erst[1]=0; uvc_erst[2]=4096; uvc_erst[3]=0; uvc_ev1=0; uvc_ev1c=1;
    u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x00,0); XMW(rt+0x08,1); XMW(rt+0x18,(u32)(u64)uvc_er); XMW(rt+0x1C,0); XMW(rt+0x10,(u32)(u64)uvc_erst); XMW(rt+0x14,0); }
static void uvc_ring_init(void){
    u32 n=(u32)cam.ntd; for(u32 i=0;i<(n+1)*4;i++)uvc_ring[i]=0; volatile u32* lk=uvc_ring+n*4; lk[0]=(u32)(u64)uvc_ring; lk[1]=0; lk[2]=0; lk[3]=(6u<<10)|(1u<<1)|1u;
    uvc_enq=0; uvc_deq=0; uvc_cyc=1; uvc_out=0; }
void uvc_stop(void){
    if(!cam.ok||uvc_state!=2){ return; }
    uvc_state=1; uvc_req(0x01,0x0B,0,(u16)cam.vs_if,0,0,0);
    if(cam.cur_alt){ uvc_cfg_ep(0,0); }
    uvc_fps10=0; uvc_m("Camera stopped"); }
int uvc_start(int hd){
    if(!cam.ok){ uvc_m("No camera found"); return -1; }
    if(uvc_state==2)uvc_stop();
    if(cam.vs_if<0||!cam.nfr){ uvc_m("The camera has no supported picture format (needs YUY2, NV12 or MJPEG)"); return -1; }
    if(!cam.nalt){ uvc_m(cam.bulk?"This camera streams with bulk transfers, which this driver does not support yet":"The camera has no video stream endpoint"); return -1; }
    int sv=g_xcur; xhc_select(cam.ctl); uvc_maxintr=(int)((XMR(xhci_base+0x04)>>8)&0x7FF); xhc_select(sv);
    if(uvc_maxintr<2){ uvc_m("The USB controller has only one interrupter"); return -1; }
    u32 mes=0; for(int k=0;k<cam.nalt;k++) if(cam.alt[k].esit>mes)mes=cam.alt[k].esit; if(mes<64)mes=64;
    u32 bsz=(mes+63u)&~63u; int ntd=bsz<=3072u?1000:(bsz<=16384u?256:96);
    if(!uvc_bufs||(u32)cam.bufsz<bsz||cam.ntd!=ntd){ uvc_bufs=uvc_al(bsz*(u32)ntd,4096); if(!uvc_bufs){ uvc_m("Out of memory for camera buffers"); return -1; } cam.bufsz=(int)bsz; cam.ntd=ntd; }
    if(!uvc_ring){ uvc_ring=(volatile u32*)uvc_al(16384*2,65536); }
    if(!uvc_ermem){ uvc_ermem=uvc_al(65536+256,65536); if(uvc_ermem){ uvc_er=(volatile u32*)uvc_ermem; uvc_erst=(volatile u32*)(uvc_ermem+65536); } }
    if(!uvc_fb){ uvc_fb=uvc_al(1920*1200*2+65536,64); uvc_jpg=uvc_al(1920*1200*2+65536+1024,64); uvc_rgb=(u32*)uvc_al(1920*1200*4,64); }
    if(!uvc_ring||!uvc_ermem||!uvc_fb||!uvc_jpg||!uvc_rgb){ uvc_m("Out of memory for the camera"); return -1; }
    int order[64], no=0; for(int g=0;g<4;g++){ int idx[64], ni=0; for(int k=0;k<cam.nfr;k++) if(uvc_pref(&cam.fr[k],hd)==g)idx[ni++]=k;
        for(int a=0;a<ni;a++) for(int b2=a+1;b2<ni;b2++){ u32 aa=(u32)cam.fr[idx[a]].w*cam.fr[idx[a]].h, bb=(u32)cam.fr[idx[b2]].w*cam.fr[idx[b2]].h; int sw=(g>=2)?(bb<aa):(bb>aa); if(sw){ int t=idx[a]; idx[a]=idx[b2]; idx[b2]=t; } }
        for(int a=0;a<ni&&no<64;a++)order[no++]=idx[a]; }
    if(!no){ uvc_m("No picture size up to 1920x1200"); return -1; }
    for(int tr=0;tr<no&&tr<6;tr++){ uvc_fr_t* f=&cam.fr[order[tr]];
        u8 p[48]; for(int k=0;k<48;k++)p[k]=0; p[0]=1; p[2]=f->fi; p[3]=f->ri; p[4]=(u8)f->defint; p[5]=(u8)(f->defint>>8); p[6]=(u8)(f->defint>>16); p[7]=(u8)(f->defint>>24);
        if(!uvc_req(0x21,0x01,0x0100,(u16)cam.vs_if,(u16)cam.plen,0,p)){ uvc_m("Camera refused the picture size (probe)"); continue; }
        u8 g2[48]; for(int k=0;k<48;k++)g2[k]=p[k];
        if(uvc_req(0xA1,0x81,0x0100,(u16)cam.vs_if,(u16)cam.plen,1,g2)){ for(int k=0;k<48;k++)p[k]=g2[k]; }
        uvc_maxfs=uvc_r32(p+18); uvc_maxpay=uvc_r32(p+22); uvc_interval=uvc_r32(p+4);
        if(!uvc_req(0x21,0x01,0x0200,(u16)cam.vs_if,(u16)cam.plen,0,p)){ uvc_m("Camera refused the picture size (commit)"); continue; }
        int pick=-1; for(int k=0;k<cam.nalt;k++){ if(uvc_maxpay&&cam.alt[k].esit<uvc_maxpay)continue; if(pick<0||cam.alt[k].esit<cam.alt[pick].esit)pick=k; }
        if(pick<0) for(int k=0;k<cam.nalt;k++) if(pick<0||cam.alt[k].esit>cam.alt[pick].esit)pick=k;
        uvc_alt_t* a=&cam.alt[pick]; cam.maxp=a->mps&0x7FF; cam.burst=a->burst; cam.mult=a->mult; cam.esit=(int)a->esit; cam.ss=cam.speed>=4;
        if(cam.esit>cam.bufsz){ uvc_m("Stream packet larger than the buffers"); continue; }
        sv=g_xcur; xhc_select(cam.ctl); uvc_ring_init(); uvc_er_init(); xhc_select(sv);
        if(cam.cur_alt)uvc_cfg_ep(0,0);
        if(!uvc_cfg_ep(1,a)){ char b[96],*q=b; q=scopy(q,"USB controller refused the stream (code "); q=udec(q,(u32)uvc_cfgcc); q=scopy(q,uvc_cfgcc==8?", not enough USB bandwidth)":")"); *q=0; uvc_m(b); continue; }
        if(!uvc_req(0x01,0x0B,a->alt,(u16)cam.vs_if,0,0,0)){ uvc_cfg_ep(0,0); uvc_m("Camera refused SET_INTERFACE"); continue; }
        uvc_w=f->w; uvc_h=f->h; uvc_kind=f->kind; uvc_fi=f->fi; uvc_ri=f->ri; uvc_altsel=a->alt;
        uvc_fcap=uvc_kind==1?(1920u*1200u*2u):(u32)uvc_w*(u32)uvc_h*2u+4096u; uvc_fill=0; uvc_fbad=0; uvc_lfid=-1; uvc_jnew=0;
        for(u32 i=0;i<(u32)uvc_w*(u32)uvc_h;i++)uvc_rgb[i]=0x101418;
        uvc_events=0; uvc_tds=0; uvc_tderr=0; uvc_frames=0; uvc_badframes=0; uvc_short=0; uvc_bytes=0; uvc_fps10=0; uvc_ft0=hr_ns(); uvc_fc0=0;
        sv=g_xcur; xhc_select(cam.ctl); while(uvc_out<(u32)cam.ntd-1u)uvc_td(); xh_db(cam.slot,(u32)cam.dci); xhc_select(sv);
        uvc_state=2; { char b[96],*q=b; q=scopy(q,"Streaming "); q=udec(q,(u32)uvc_w); *q++='x'; q=udec(q,(u32)uvc_h); q=scopy(q,uvc_kind==1?" MJPEG":uvc_kind==2?" NV12":" YUY2"); *q=0; uvc_m(b); klog(b); }
        return 0; }
    return -1; }
void uvc_poll(void){
    if(!cam.ok||uvc_state!=2)return; int sv=g_xcur; xhc_select(cam.ctl);
    u32 ra=(u32)(u64)uvc_ring; int n=0;
    for(int guard=0;guard<8192;guard++){ volatile u32* e=uvc_er+uvc_ev1*4; if((e[3]&1u)!=uvc_ev1c)break;
        u32 ty=(e[3]>>10)&0x3F, cc=(e[2]>>24)&0xFF, res=e[2]&0xFFFFFF, ptr=e[0];
        uvc_ev1++; if(uvc_ev1>=4096){ uvc_ev1=0; uvc_ev1c^=1; } n++; uvc_events++;
        if(ty!=32)continue; uvc_lastcc=(int)cc;
        if(ptr>=ra&&ptr<ra+(u32)cam.ntd*16u&&uvc_out){ u32 idx=(ptr-ra)/16u;
            for(int g=0;g<cam.ntd&&uvc_out;g++){ u32 k=uvc_deq; uvc_deq++; if(uvc_deq>=(u32)cam.ntd)uvc_deq=0; uvc_out--;
                if(k==idx){ uvc_tds++; if(cc==1||cc==13){ int got=cam.esit-(int)res; if(got<0)got=0; uvc_payload(uvc_bufs+(u64)k*cam.bufsz,got); } else uvc_tderr++; break; } } }
        else if(cc!=1&&cc!=13) uvc_tderr++; }
    if(n){ u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x18,((u32)(u64)(uvc_er+uvc_ev1*4))|(1u<<3)); XMW(rt+0x1C,0); }
    int add=0; while(uvc_out<(u32)cam.ntd-1u){ uvc_td(); add=1; } if(add)xh_db(cam.slot,(u32)cam.dci);
    xhc_select(sv);
    u64 now=hr_ns(); if(now-uvc_ft0>=1000000000ULL){ uvc_fps10=(u32)((u64)(uvc_frames-uvc_fc0)*10000000000ULL/(now-uvc_ft0)); uvc_ft0=now; uvc_fc0=uvc_frames; } }
const u8 uvc_std_dht[]={0xFF,0xC4,0x01,0xA2,
0x00,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,
0x10,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d,
0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,
0x01,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,
0x11,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77,
0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
const int uvc_std_dht_len=(int)sizeof(uvc_std_dht);
static void uvc_reset(void){ cam.ok=0; uvc_state=0; uvc_reg_hold=-1; uvc_er_done=0; }
static void uvc_unplug(int port){ if(cam.ok&&cam.ctl==g_xcur&&cam.root==port){ uvc_gone(); uvc_reg_hold=-1; } }
