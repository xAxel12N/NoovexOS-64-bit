#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/acpi/aml_scan.h"
#include "include/nvx/drivers/i2c/dw_i2c.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/input/touchpad/elan_i2c.h"
#include "include/nvx/drivers/input/ps2/elan_ps2.h"
#include "include/nvx/drivers/input/touchpad/touchpad.h"
#include "include/nvx/drivers/input/touchpad/tp_acpi.h"
static const u16 TP_ADDRS[]={0x2C,0x15,0x2A,0x10,0x20,0x24,0x38,0x40,0x14,0x5D};
static const u16 TP_DREGS[]={0x20,0x01,0x00,0x10,0x30};
static int g_tp_probed=0, g_tp_cands=0, g_tp_ctrls=0; static u64 g_tp_ctrl_list[12]; static int g_tp_ctrl_n=0;
static int tp_is_amd(void){ u32 a,b,c,d; cpuid(0,&a,&b,&c,&d); return b==0x68747541u&&d==0x69746E65u&&c==0x444D4163u; }
typedef struct { u8 dev, fn, pm0, why; u16 did; u64 bar; u32 caps, comp; } tpcdiag;
static tpcdiag g_tpc[12]; static int g_tpc_n=0;
static u64 tp_pci_dw(int bus,int dev,int fn){
    u32 id=pci_cfg(bus,dev,fn,0);
    tpcdiag* dg=0; for(int k=0;k<g_tpc_n;k++) if(g_tpc[k].dev==dev&&g_tpc[k].fn==fn)dg=&g_tpc[k]; if(!dg&&g_tpc_n<12){ dg=&g_tpc[g_tpc_n++]; dg->dev=(u8)dev; dg->fn=(u8)fn; }
    tpcdiag dd; if(!dg)dg=&dd; dg->did=(u16)(id>>16); dg->bar=0; dg->caps=0; dg->comp=0; dg->pm0=0xFF; dg->why=0;
    if((id&0xFFFF)==0xFFFF){ dg->why=7; return 0; }
    u32 cmd=pci_cfg(bus,dev,fn,0x04);
    u32 cp=(pci_cfg(bus,dev,fn,0x34)&0xFC); for(int g=0;cp&&g<16;g++){ u32 v=pci_cfg(bus,dev,fn,(int)cp); if((v&0xFF)==0x01){ u32 pm=pci_cfg(bus,dev,fn,(int)cp+4); dg->pm0=(u8)(pm&3); if(pm&3){ pci_cfgw(bus,dev,fn,(int)cp+4,pm&~3u); ih_delay_ms(10); } break; } cp=(v>>8)&0xFC; }
    u32 b0=pci_cfg(bus,dev,fn,0x10); if(b0&1){ dg->why=1; return 0; } u64 base=b0&0xFFFFFFF0u; if((b0&6)==4)base|=(u64)pci_cfg(bus,dev,fn,0x14)<<32; dg->bar=base; if(!base){ dg->why=2; return 0; } if(!tpa_map(base)){ dg->why=3; return 0; }
    pci_cfgw(bus,dev,fn,0x04,(cmd&0xFFFFu)|0x06u);
    if((id&0xFFFF)==0x8086){ u32 caps=*(volatile u32*)(base+0x2FC); dg->caps=caps; if(caps!=0xFFFFFFFFu&&((caps>>4)&0xF)!=0){ dg->why=4; return 0; } if(dev==0x1F){ dg->why=4; return 0; } tpa_lpss_init(base); }
    else if(!dw_present(base)){ *(volatile u32*)(base+0x204)=0x7u; ih_delay_ms(2); }
    dg->comp=*(volatile u32*)(base+0xFC); if(!dw_present(base)){ dg->why=5; return 0; }
    dg->why=6; return base; }
static void tp_add_ctrl(u64 b){ if(!b)return; for(int k=0;k<g_tp_ctrl_n;k++) if(g_tp_ctrl_list[k]==b)return; if(g_tp_ctrl_n<12)g_tp_ctrl_list[g_tp_ctrl_n++]=b; }
static int g_tp_cwhy=0;
static u64 tp_ctrl_base(const char* path){
    g_tp_cwhy=0; if(!path||!path[0]||(path[0]=='N'&&path[1]=='U'&&path[2]=='L'&&path[3]=='L')){ g_tp_cwhy=1; return 0; }
    int d=aml_find_dev(path);
    if(d>=0&&g_amld[d].has_mem&&dw_present(g_amld[d].mem_base))return g_amld[d].mem_base;
    if(d>=0&&g_amld[d].has_adr){ int dev=(int)((g_amld[d].adr>>16)&0x1F), fn=(int)(g_amld[d].adr&7); u64 b=tp_pci_dw(0,dev,fn); if(b)return b; }
    tpa_ctl c; if(tpa_ctrl(path,&c)==0){
        if(c.adr_ok){ u64 b=tp_pci_dw(0,(int)((c.adr>>16)&0x1F),(int)(c.adr&7)); if(b)return b; }
        if(c.mem&&!(c.mem&0xFFF)&&tpa_map(c.mem)){ if(!dw_present(c.mem)&&!tp_is_amd()){ u32 caps=*(volatile u32*)(c.mem+0x2FC); if(caps==0xFFFFFFFFu||((caps>>4)&0xF)==0)tpa_lpss_init(c.mem); } if(dw_present(c.mem))return c.mem; }
        g_tp_cwhy=c.adr_ok||c.mem?3:2; return 0; }
    g_tp_cwhy=d>=0?3:2; return 0; }
static void tp_list_ctrls(void){
    g_tp_ctrl_n=0;
    for(int i=0;i<g_amld_n&&g_tp_ctrl_n<12;i++){ const char* h=g_amld[i].hid; int dwh=(h[0]=='A'&&h[1]=='M'&&h[2]=='D'&&h[3]=='I'&&h[4]=='0'&&(h[5]=='0'||h[5]=='5')&&h[6]=='1'&&h[7]=='0');
        if(dwh&&g_amld[i].has_mem&&dw_present(g_amld[i].mem_base)){ int dup=0; for(int k=0;k<g_tp_ctrl_n;k++) if(g_tp_ctrl_list[k]==g_amld[i].mem_base)dup=1; if(!dup)g_tp_ctrl_list[g_tp_ctrl_n++]=g_amld[i].mem_base; } }
    if(tp_is_amd()) for(u64 a=0xFEDC2000ULL;a<=0xFEDC6000ULL&&g_tp_ctrl_n<12;a+=0x1000){ if(dw_present(a)){ int dup=0; for(int k=0;k<g_tp_ctrl_n;k++) if(g_tp_ctrl_list[k]==a)dup=1; if(!dup)g_tp_ctrl_list[g_tp_ctrl_n++]=a; } }
    for(int dev=0;dev<32&&g_tp_ctrl_n<12;dev++){ if(dev==0x1F)continue; int nf=(pci_cfg(0,dev,0,0x0C)&0x800000u)?8:1; for(int fn=0;fn<nf&&g_tp_ctrl_n<12;fn++){ u32 id=pci_cfg(0,dev,fn,0); if((id&0xFFFF)!=0x8086)continue; u32 cl=pci_cfg(0,dev,fn,0x08); if((cl>>16)!=0x0C80u)continue;
        tp_add_ctrl(tp_pci_dw(0,dev,fn)); } }
    g_tp_ctrls=g_tp_ctrl_n; }
static int g_tp_dsm=-1;
typedef struct { u16 addr, reg; int kind, res, stage; u16 rdlen, vid, pid; u32 abort; int same, slow, tries, items, skip, skipoff, stop, ptp, mouse, nfing; } tpdiag;
static tpdiag g_tpd[20]; static int g_tpd_n=0;
extern int g_ih_rd_tries, g_ih_rd_same, g_ih_rd_slow;
extern int g_tpp_items, g_tpp_skip, g_tpp_skip_off, g_tpp_stop, g_tpp_depth_err;
static u8* g_tpk; static int g_tpk_n=0, g_tpk_res=0; static u16 g_tpk_vid, g_tpk_pid, g_tpk_addr, g_tpk_reg;
static int g_tpk_same, g_tpk_slow, g_tpk_items, g_tpk_skip, g_tpk_skipoff, g_tpk_stop, g_tpk_depth, g_tpk_ptp, g_tpk_mouse, g_tpk_nfing;
static void tp_diag(tpdev* t,int kind,u16 reg,int res){ if(g_tpd_n>=20)return; tpdiag* d=&g_tpd[g_tpd_n++]; d->addr=t->addr; d->reg=reg; d->kind=kind; d->res=res; d->stage=t->stage; d->rdlen=t->d.rdlen; d->vid=t->d.vid; d->pid=t->d.pid; d->abort=t->bus.last_abort;
    d->same=g_ih_rd_same; d->slow=g_ih_rd_slow; d->tries=g_ih_rd_tries; d->items=g_tpp_items; d->skip=g_tpp_skip; d->skipoff=g_tpp_skip_off; d->stop=g_tpp_stop; d->ptp=t->lay.ptp; d->mouse=t->lay.mouse; d->nfing=t->lay.nfing; }
static void tp_keep(tpdev* t,u16 reg,int res){
    if(!t->rdesc||!t->d.rdlen)return; if(!g_tpk)g_tpk=(u8*)kmalloc(4096); if(!g_tpk)return;
    int n=t->d.rdlen; if(n>4096)n=4096; for(int i=0;i<n;i++)g_tpk[i]=t->rdesc[i]; g_tpk_n=n; g_tpk_res=res; g_tpk_vid=t->d.vid; g_tpk_pid=t->d.pid; g_tpk_addr=t->addr; g_tpk_reg=reg;
    g_tpk_same=g_ih_rd_same; g_tpk_slow=g_ih_rd_slow; g_tpk_items=g_tpp_items; g_tpk_skip=g_tpp_skip; g_tpk_skipoff=g_tpp_skip_off; g_tpk_stop=g_tpp_stop; g_tpk_depth=g_tpp_depth_err; g_tpk_ptp=t->lay.ptp; g_tpk_mouse=t->lay.mouse; g_tpk_nfing=t->lay.nfing; }
static int tp_hid_at(tpdev* t,u16 reg){
    t->stage=0; t->bus.last_abort=0; g_ih_rd_tries=0; g_ih_rd_same=0; g_ih_rd_slow=0; g_tpp_items=0; g_tpp_skip=0; g_tpp_skip_off=-1; g_tpp_stop=-1;
    int r=ih_get_desc(t,reg); if(r){ tp_diag(t,0,reg,r); return -2; }
    r=ih_setup(t); if(t->stage>=4)tp_keep(t,reg,r); tp_diag(t,1,reg,r); return r==-2?-3:r; }
static int tp_try(tpdev* t,u64 base,u32 speed,u16 addr,u16 dreg){
    if(!t->bus.ok||t->bus.base!=base||t->bus.fast!=(speed>=400000u)){ if(dw_init(&t->bus,base,speed))return -1; }
    t->addr=addr; t->ctrl_base=base;
    if(dreg!=0xFFFF)return tp_hid_at(t,dreg);
    u16 tried[8]; int nt=0;
    if(g_tp_dsm>=0){ tried[nt++]=(u16)g_tp_dsm; int r=tp_hid_at(t,(u16)g_tp_dsm); if(r==0)return 0; if(r!=-2)return r; if(t->stage==0&&(t->bus.last_abort&1u))return -2; }
    for(u32 k=0;k<sizeof(TP_DREGS)/sizeof(TP_DREGS[0]);k++){ int dup=0; for(int q=0;q<nt;q++) if(tried[q]==TP_DREGS[k])dup=1; if(dup)continue; if(nt<8)tried[nt++]=TP_DREGS[k];
        int r=tp_hid_at(t,TP_DREGS[k]); if(r==0)return 0; if(r!=-2)return r; if(t->stage==0&&(t->bus.last_abort&1u))return -2; }
    return -2; }
static int tp_try_elan(tpdev* t,u64 base,u32 speed,u16 addr){
    if(!t->bus.ok||t->bus.base!=base){ if(dw_init(&t->bus,base,speed))return -1; }
    t->addr=addr; t->ctrl_base=base; t->stage=10; t->bus.last_abort=0; int r=el_setup(t); tp_diag(t,2,0,r); return r; }
static void tp_gpio_setup(tpdev* t,int devi){
    t->gpio_ok=0; if(devi<0||!g_amld[devi].has_gpio)return;
    int g=aml_find_dev(g_amld[devi].gpio.ctrl); if(g<0||!g_amld[g].has_mem)return;
    const char* h=g_amld[g].hid; if(!(h[0]=='A'&&h[1]=='M'&&h[2]=='D'&&h[3]=='I'&&h[4]=='0'&&h[5]=='0'&&h[6]=='3'&&h[7]=='0'))return;
    t->gpio_base=g_amld[g].mem_base; t->pin=g_amld[devi].gpio.pin; t->active_low=g_amld[devi].gpio.active_low;
    { extern int (*g_tp_amd_setup)(u64,int,int); if(t->pin<256){ if(g_tp_amd_setup)g_tp_amd_setup(t->gpio_base,t->pin,t->active_low); else t->gpio_ok=1; } } }
static int tp_elan_name(const char* h){ return h[0]=='E'&&h[1]=='L'&&h[2]=='A'&&h[3]=='N'; }
static int tp_is_cand(amldev* D){ return D->is_hidi2c||elan_acpi_match(D->hid)||tp_elan_name(D->hid); }
static int tp_same_path(const char* a,const char* b){ for(;;){ while(*a=='_'||*a=='\\')a++; while(*b=='_'||*b=='\\')b++; if(*a!=*b)return 0; if(!*a)return 1; a++; b++; } }
static void tp_i2c_dedup(amldev* D){ for(int r=1;r<D->ni2c;r++) if(D->i2c[r].addr==D->i2c[0].addr&&tp_same_path(D->i2c[r].ctrl,D->i2c[0].ctrl)){ for(int q=r;q<D->ni2c-1;q++)D->i2c[q]=D->i2c[q+1]; D->ni2c--; r--; } }
typedef struct { char path[48]; char hid[12]; int sta, ini, cwhy; u16 addr; char ctrl[40]; u64 base; } tpcand;
static tpcand g_tpk2[8]; static int g_tpk2_n=0;
static void tp_parent(const char* nm,char* out,int cap){ int n=0; while(nm[n]&&n<cap-1){ out[n]=nm[n]; n++; } out[n]=0; while(n>0&&out[n-1]!='.')n--; if(n>0)out[n-1]=0; else out[0]=0; }
static int tp_bad_ctrl(const char* c){ return !c[0]||(c[0]=='N'&&c[1]=='U'&&c[2]=='L'&&c[3]=='L'); }
static void tp_init_ex(int scan_all){
    tpdev* t=&g_tp; g_tp_probed=1; g_tp_cands=0; g_tp_ctrl_n=0; g_tp_ctrls=0; g_tpc_n=0; g_tpk2_n=0;
    u8* keep=t->rdesc; for(u32 i=0;i<sizeof(*t);i++)((u8*)t)[i]=0; t->rdesc=keep;
    aml_load_from_acpi(); aml_scan_tables();
    { int any=0; for(int i=0;i<g_amld_n;i++) if(tp_is_cand(&g_amld[i]))any=1; if(any||scan_all)tp_list_ctrls(); }
    g_tpd_n=0;
    for(int pass=0;pass<2&&!t->ok;pass++) for(int i=0;i<g_amld_n&&!t->ok;i++){ amldev* D=&g_amld[i]; const char* nm=g_amln[D->node].path;
        if((!D->hid[0]||D->hid[0]=='?'||(D->hid[0]=='X'&&D->hid[1]=='X'))&&(D->is_hidi2c||D->ni2c)){ char hb[12]; if(tpa_hid(nm,hb)==0){ int k=0; for(;hb[k]&&k<11;k++)D->hid[k]=hb[k]; D->hid[k]=0; } }
        int el=elan_acpi_match(D->hid), elx=tp_elan_name(D->hid); if(!D->is_hidi2c&&!el&&!elx)continue;
        int sta=tpa_sta(nm); if((pass==0)==(sta==0))continue;
        if(D->hid[0]=='X'&&D->hid[1]=='X'&&D->hid[2]=='X'&&D->hid[3]=='X'&&sta<=0)continue;
        if(pass==0)g_tp_cands++;
        tpcand* C=g_tpk2_n<8?&g_tpk2[g_tpk2_n++]:&g_tpk2[7]; { int k=0; for(;nm[k]&&k<47;k++)C->path[k]=nm[k]; C->path[k]=0; for(k=0;D->hid[k]&&k<11;k++)C->hid[k]=D->hid[k]; C->hid[k]=0; } C->sta=sta; C->addr=0; C->ctrl[0]=0; C->base=0; C->cwhy=0;
        tpa_power_on(nm); C->ini=sta==0?-3:tpa_ini(nm); g_tp_dsm=tpa_dsm_hid(nm); if(g_tp_dsm==0)g_tp_dsm=-1;
        { tpa_res R; if(tpa_crs(nm,&R)==0&&R.addr){ int sh=D->ni2c<6?D->ni2c:5; for(int q=sh;q>0;q--)D->i2c[q]=D->i2c[q-1]; D->i2c[0].addr=R.addr; D->i2c[0].speed=R.speed; int k=0; for(;R.ctrl[k]&&k<71;k++)D->i2c[0].ctrl[k]=R.ctrl[k]; D->i2c[0].ctrl[k]=0; if(D->ni2c<6)D->ni2c++; tp_i2c_dedup(D);
            if(R.has_gpio){ D->has_gpio=1; D->gpio.pin=R.pin; D->gpio.active_low=R.active_low; for(k=0;R.gctrl[k]&&k<71;k++)D->gpio.ctrl[k]=R.gctrl[k]; D->gpio.ctrl[k]=0; } } }
        { char par[72]; tp_parent(nm,par,72); int has=0; for(int r=0;r<D->ni2c;r++){ if(tp_bad_ctrl(D->i2c[r].ctrl)){ int k=0; for(;par[k]&&k<71;k++)D->i2c[r].ctrl[k]=par[k]; D->i2c[r].ctrl[k]=0; } if(tp_same_path(D->i2c[r].ctrl,par))has=1; }
          if(!has&&D->ni2c<6&&par[0]){ aml_i2cres* q=&D->i2c[D->ni2c++]; q->addr=0; q->speed=0; int k=0; for(;par[k]&&k<71;k++)q->ctrl[k]=par[k]; q->ctrl[k]=0; } tp_i2c_dedup(D); }
        if(D->ni2c){ C->addr=D->i2c[0].addr; int k=0; for(;D->i2c[0].ctrl[k]&&k<39;k++)C->ctrl[k]=D->i2c[0].ctrl[k]; C->ctrl[k]=0; }
        for(int r=0;r<D->ni2c&&!t->ok;r++){ u64 base=tp_ctrl_base(D->i2c[r].ctrl); if(r==0||!C->base){ C->cwhy=g_tp_cwhy; C->base=base; } if(!base&&g_tp_ctrl_n==1)base=g_tp_ctrl_list[0]; if(!base)continue; tp_add_ctrl(base);
            { u16 ss[3]={0,0,0}, fs[3]={0,0,0}; int got=tpa_timing(D->i2c[r].ctrl,ss,fs); for(int q=0;q<3;q++){ g_dw_ss[q]=(got&1)?ss[q]:0; g_dw_fs[q]=(got&2)?fs[q]:0; } t->bus.ok=0;
              int ci=aml_find_dev(D->i2c[r].ctrl); const char* ch=ci>=0?g_amld[ci].hid:""; g_dw_clk_mhz=(ch[0]=='A'&&ch[1]=='M'&&ch[2]=='D')?(ch[3]=='0'?133u:150u):(tp_is_amd()?150u:0u); }
            u32 sp=D->i2c[r].speed?D->i2c[r].speed:400000u;
            u16 order[12]; int na=0; if(D->i2c[r].addr)order[na++]=D->i2c[r].addr; else { if(el||elx)order[na++]=0x15; for(u32 a=0;a<sizeof(TP_ADDRS)/sizeof(TP_ADDRS[0])&&na<12;a++){ if((el||elx)&&TP_ADDRS[a]==0x15)continue; order[na++]=TP_ADDRS[a]; } }
            for(int a=0;a<na&&!t->ok;a++){ u16 ad=order[a];
                for(int ps=0;ps<2&&!t->ok;ps++){ int native=(ps==0)==(el!=0);
                    if(native){ if(!el&&!elx&&t->d.vid!=0x04F3)continue; if(tp_try_elan(t,base,sp,ad)==0)t->from_acpi=1; }
                    else if(tp_try(t,base,sp,ad,0xFFFF)==0)t->from_acpi=1; } }
            if(t->ok){ int k=0; for(;nm[k]&&k<71;k++)t->path[k]=nm[k]; t->path[k]=0; for(k=0;D->hid[k]&&k<11;k++)t->hid[k]=D->hid[k]; t->hid[k]=0; for(k=0;D->i2c[r].ctrl[k]&&k<71;k++)t->ctrl[k]=D->i2c[r].ctrl[k]; t->ctrl[k]=0; tp_gpio_setup(t,i);
                if(!t->gpio_ok&&D->has_gpio&&!tp_is_amd()){ extern int igp_setup(const char*,int,int); igp_setup(D->gpio.ctrl,D->gpio.pin,D->gpio.active_low); } } } }
    int auto_scan=scan_all||(!t->ok&&g_tp_cands>0);
    if(auto_scan) for(int c=0;c<g_tp_ctrl_n&&!t->ok;c++) for(u32 a=0;a<sizeof(TP_ADDRS)/sizeof(TP_ADDRS[0])&&!t->ok;a++) for(u32 k=0;k<sizeof(TP_DREGS)/sizeof(TP_DREGS[0])&&!t->ok;k++)
        if(tp_try(t,g_tp_ctrl_list[c],400000u,TP_ADDRS[a],TP_DREGS[k])==0){ t->path[0]=0; }
    if(auto_scan) for(int c=0;c<g_tp_ctrl_n&&!t->ok;c++) if(tp_try_elan(t,g_tp_ctrl_list[c],400000u,0x15)==0){ t->path[0]=0; }
    if(!t->ok&&auto_scan){ for(int q=0;q<3;q++){ g_dw_ss[q]=0; g_dw_fs[q]=0; } }
    if(t->ok)klog(t->proto==1?"touchpad: ELAN touchpad ready (ELAN I2C protocol)":t->mode==1?"touchpad: precision touchpad ready (HID over I2C)":"touchpad: HID over I2C touchpad ready in mouse mode");
    else if(g_tp_cands)klog("touchpad: ACPI lists an I2C touchpad but it did not answer - run tpinfo"); }
void tp_init(void){ tp_init_ex(0); }
static const char* tp_gname(int g){ switch(g){ case GES_SWIPE_UP: return "3-finger swipe up"; case GES_SWIPE_DOWN: return "3-finger swipe down"; case GES_SWIPE_LEFT: return "3-finger swipe left"; case GES_SWIPE_RIGHT: return "3-finger swipe right"; case GES_ZOOM_IN: return "pinch out (zoom in)"; case GES_ZOOM_OUT: return "pinch in (zoom out)"; default: return "none"; } }
void tpinfo_run(int rescan){ char b[280],*p; tpdev* t=&g_tp;
    if(rescan)tp_init_ex(1); else if(!g_tp_probed)tp_init();
    p=b; p=scopy(p,"ACPI: "); p=udec(p,(u32)g_amlt_n); p=scopy(p," AML tables, "); p=udec(p,(u32)g_amld_n); p=scopy(p," devices, "); p=udec(p,(u32)g_tp_cands); p=scopy(p," I2C touchpads (PNP0C50 or ELAN)"); *p=0; term_puts(b); term_putc('\n');
    if(g_aml_full){ tcol(rgb(255,205,100)); term_puts(g_aml_full&1?"ACPI: the firmware tables are larger than NoovexOS can read - some devices were left out\n":"ACPI: more devices than NoovexOS can list - some devices were left out\n"); tcol(WHT); }
    for(int k=0;k<g_tpk2_n&&k<6;k++){ tpcand* C=&g_tpk2[k]; p=b; p=scopy(p,"  "); p=scopy(p,C->path); p=scopy(p,"  "); p=scopy(p,C->hid[0]?C->hid:"?"); p=scopy(p,"  _STA ");
        if(C->sta<0)p=scopy(p,"none"); else { p=scopy(p,"0x"); p=hx(p,(u32)C->sta,2); p=scopy(p,(C->sta&1)?" (on)":" (off)"); } p=scopy(p,"  _INI "); p=scopy(p,C->ini==0?"run":C->ini==1?"none":C->ini==-2?"failed":C->ini==-3?"skipped (off)":"-"); *p=0; term_puts(b); term_putc('\n');
        p=b; p=scopy(p,"    I2C addr "); if(C->addr){ p=scopy(p,"0x"); p=hx(p,C->addr,2); } else p=scopy(p,"not given (tries 0x15, 0x2C, ...)"); p=scopy(p," on "); p=scopy(p,C->ctrl[0]?C->ctrl:"?"); p=scopy(p,"  controller ");
        if(C->base){ p=scopy(p,"0x"); if(C->base>>32)p=hx(p,(u32)(C->base>>32),4); p=hx(p,(u32)C->base,8); } else p=scopy(p,C->cwhy==1?"path missing":C->cwhy==2?"not in ACPI":"did not answer (see PCI lines)"); *p=0; term_puts(b); term_putc('\n'); }
    for(int k=0;k<g_tpc_n;k++){ tpcdiag* d=&g_tpc[k]; p=b; p=scopy(p,"  PCI 00:"); p=hx(p,d->dev,2); *p++='.'; p=udec(p,d->fn); if(d->why!=7){ p=scopy(p," 8086:"); p=hx(p,d->did,4); } if(d->why!=7){ p=scopy(p,"  BAR 0x"); if(d->bar>>32)p=hx(p,(u32)(d->bar>>32),4); p=hx(p,(u32)d->bar,8); }
        if(d->pm0!=0xFF){ p=scopy(p,"  was D"); p=udec(p,d->pm0); } if(d->caps){ p=scopy(p,"  caps 0x"); p=hx(p,d->caps,8); }
        p=scopy(p,"  "); p=scopy(p,d->why==6?"I2C controller ready":d->why==1?"I/O BAR, skipped":d->why==2?"no address assigned":d->why==3?"address not reachable":d->why==4?"not an I2C controller (UART or SPI)":d->why==5?"no DesignWare answer, reads 0x":d->why==7?"no PCI device at this address (hidden or off)":"?"); if(d->why==5)p=hx(p,d->comp,8); *p=0; term_puts(b); term_putc('\n'); }
    if(!g_tpk2_n) for(int i=0,shown=0;i<g_amld_n&&shown<6;i++){ if(!tp_is_cand(&g_amld[i]))continue; shown++; amldev* D=&g_amld[i];
        p=b; p=scopy(p,"  "); p=scopy(p,g_amln[D->node].path); p=scopy(p,"  "); p=scopy(p,D->hid[0]?D->hid:"?"); *p=0; term_puts(b); term_putc('\n');
        for(int r=0;r<D->ni2c;r++){ p=b; p=scopy(p,"    I2C addr 0x"); p=hx(p,D->i2c[r].addr,2); p=scopy(p," on "); p=scopy(p,D->i2c[r].ctrl); p=scopy(p,"  "); p=udec(p,D->i2c[r].speed/1000); p=scopy(p," kHz  controller "); u64 cb=(t->ok&&tp_same_path(D->i2c[r].ctrl,t->ctrl))?t->ctrl_base:t->ok?0:tp_ctrl_base(D->i2c[r].ctrl); if(cb){ p=scopy(p,"0x"); p=hx(p,(u32)cb,8); } else p=scopy(p,"not reachable"); *p=0; term_puts(b); term_putc('\n'); }
        if(D->has_gpio){ p=b; p=scopy(p,"    interrupt pin "); p=udec(p,D->gpio.pin); p=scopy(p,D->gpio.active_low?" (active low) on ":" (active high) on "); p=scopy(p,D->gpio.ctrl); *p=0; term_puts(b); term_putc('\n'); } }
    p=b; p=scopy(p,"ACPI methods: _CRS "); p=scopy(p,g_tpa_crs_ok?"read":"not used"); p=scopy(p,"  _DSM HID reg "); if(g_tpa_dsm_reg>=0){ p=scopy(p,"0x"); p=hx(p,(u32)g_tpa_dsm_reg,4); } else p=scopy(p,"none"); p=scopy(p,"  _PS0 "); p=scopy(p,g_tpa_ps0?"run":"none"); p=scopy(p,"  power resources on "); p=udec(p,(u32)g_tpa_pr0); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"I2C timing: SSCN "); if(g_tpa_timing&1){ p=udec(p,g_tpa_ss[0]); *p++='/'; p=udec(p,g_tpa_ss[1]); *p++='/'; p=udec(p,g_tpa_ss[2]); } else p=scopy(p,"none"); p=scopy(p,"  FMCN "); if(g_tpa_timing&2){ p=udec(p,g_tpa_fs[0]); *p++='/'; p=udec(p,g_tpa_fs[1]); *p++='/'; p=udec(p,g_tpa_fs[2]); } else p=scopy(p,"none"); p=scopy(p,"  SDA hold "); p=udec(p,g_dw_hold); p=scopy(p,"  Intel LPSS set up "); p=udec(p,(u32)g_tpa_lpss); *p=0; term_puts(b); term_putc('\n');
    for(int k=0;k<g_tpd_n;k++){ tpdiag* d=&g_tpd[k]; p=b; p=scopy(p,"  try addr 0x"); p=hx(p,d->addr,2);
        if(d->kind==2){ p=scopy(p,"  ELAN protocol: "); p=scopy(p,d->res==0?"OK":"failed"); p=scopy(p," step "); p=udec(p,(u32)d->stage); }
        else { p=scopy(p,"  HID reg 0x"); p=hx(p,d->reg,4); p=scopy(p,": ");
            if(d->kind==0)p=scopy(p,(d->abort&1u)?"no device at this address":d->res==-2?"descriptor invalid":"descriptor read failed");
            else { p=scopy(p,"vendor "); p=hx(p,d->vid,4); p=scopy(p," product "); p=hx(p,d->pid,4); p=scopy(p," report desc "); p=udec(p,d->rdlen); p=scopy(p," bytes, ");
                p=scopy(p,d->res==0?"OK":d->res==-2?"report desc read failed":d->res==-3?"report desc not understood":d->res==-4?"no touchpad or mouse layout in report desc":"setup failed");
                if(d->stage>=3){ p=scopy(p,d->same?" (reads match":" (reads differ"); if(d->slow)p=scopy(p,", 100 kHz"); p=scopy(p,", "); p=udec(p,(u32)d->items); p=scopy(p," items"); if(d->skip){ p=scopy(p,", skipped "); p=udec(p,(u32)d->skip); } if(d->stop>=0){ p=scopy(p,", cut at byte "); p=udec(p,(u32)d->stop); } p=scopy(p,d->ptp?", touchpad layout":d->mouse?", mouse layout":", no layout"); *p++=')'; } } }
        if(d->abort&&d->kind!=0){ p=scopy(p,"  abort 0x"); p=hx(p,d->abort,8); } *p=0; term_puts(b); term_putc('\n'); }
    if(g_tpd_n&&!t->ok)term_puts("  run 'tpdump' and send a photo of it\n");
    if(!g_tp_ctrl_n&&t->ok&&t->ctrl_base){ g_tp_ctrl_list[0]=t->ctrl_base; g_tp_ctrl_n=1; }
    p=b; p=scopy(p,"I2C controllers (DesignWare): "); p=udec(p,(u32)g_tp_ctrl_n); for(int c=0;c<g_tp_ctrl_n;c++){ p=scopy(p,"  0x"); if(g_tp_ctrl_list[c]>>32)p=hx(p,(u32)(g_tp_ctrl_list[c]>>32),4); p=hx(p,(u32)g_tp_ctrl_list[c],8); } *p=0; term_puts(b); term_putc('\n');
    if(g_etp_ready){ p=b; p=scopy(p,"ELAN PS/2 touchpad: Elantech v"); p=udec(p,(u32)g_etp_hw); p=scopy(p,"  firmware "); p=hx(p,g_etp_fw,6); p=scopy(p,"  X 0-"); p=udec(p,(u32)g_etp_maxx); p=scopy(p," Y 0-"); p=udec(p,(u32)g_etp_maxy); p=scopy(p,"  packets "); p=udec(p,g_etp_pkts); p=scopy(p,"  bad "); p=udec(p,g_etp_bad); p=scopy(p,"  frames "); p=udec(p,g_etp_frames); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
    else { p=b; p=scopy(p,"ELAN PS/2 touchpad: "); p=scopy(p,g_etp_fw?"Elantech firmware ":"not found"); if(g_etp_fw){ p=hx(p,g_etp_fw,6); p=scopy(p," (old version, used as a basic mouse)"); } *p=0; term_puts(b); term_putc('\n'); }
    { extern int g_syn_ready; extern u32 g_syn_pkts, g_syn_bad, g_syn_id, g_syn_caps; if(g_syn_ready){ p=b; p=scopy(p,"Synaptics PS/2 touchpad: id "); p=hx(p,g_syn_id,6); p=scopy(p,"  caps "); p=hx(p,g_syn_caps,6); p=scopy(p,"  packets "); p=udec(p,g_syn_pkts); p=scopy(p,"  bad "); p=udec(p,g_syn_bad); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); } }
    { extern int g_igp_state, g_igp_comms, g_igp_pick; extern u32 g_igp_samples, g_igp_rep, g_igp_skips; extern int g_igp_err, g_igp_direct; extern u32 g_igp_dw0; extern char g_igp_hid[12];
        if(g_igp_state||g_igp_err){ p=b; p=scopy(p,"Intel GPIO interrupt: "); p=scopy(p,g_igp_state==2?(g_igp_direct?"pin mapped from ACPI":"pad found by learning"):g_igp_state==3?"not found (polling)":g_igp_state==1?"learning - move a finger on the pad":"not set up (polling)");
            if(g_igp_hid[0]){ p=scopy(p,"  controller "); p=scopy(p,g_igp_hid); } if(g_igp_err){ p=scopy(p,"  reason "); p=udec(p,(u32)g_igp_err); } if(g_igp_dw0){ p=scopy(p,"  pad cfg 0x"); p=hx(p,g_igp_dw0,8); } p=scopy(p,"  communities "); p=udec(p,(u32)g_igp_comms); p=scopy(p,"  samples "); p=udec(p,g_igp_samples); p=scopy(p,"  with data "); p=udec(p,g_igp_rep); if(g_igp_state==2){ p=scopy(p,"  pad #"); p=udec(p,(u32)g_igp_pick); p=scopy(p,"  skipped reads "); p=udec(p,g_igp_skips); } *p=0; term_puts(b); term_putc('\n'); } }
    if(!t->ok){ tcol(rgb(255,205,100)); p=b; p=scopy(p,"touchpad: not found"); if(t->bus.ok){ p=scopy(p,"  (last try: addr 0x"); p=hx(p,t->addr,2); p=scopy(p," stage "); p=udec(p,(u32)t->stage); p=scopy(p," abort 0x"); p=hx(p,t->bus.last_abort,8); *p++=')'; } *p=0; term_puts(b); term_putc('\n'); tcol(WHT); return; }
    tcol(GREEN); p=b; p=scopy(p,"touchpad: "); p=scopy(p,t->proto==1?"ELAN I2C protocol":t->mode==1?"precision touchpad":"mouse mode"); p=scopy(p,"  vendor "); p=hx(p,t->d.vid,4); p=scopy(p," product "); p=hx(p,t->d.pid,4); p=scopy(p,"  addr 0x"); p=hx(p,t->addr,2); p=scopy(p,"  desc reg 0x"); p=hx(p,t->descreg,4); *p=0; term_puts(b); term_putc('\n'); tcol(WHT);
    if(t->proto==1){ p=b; p=scopy(p,"  ELAN product 0x"); p=hx(p,t->el_product,4); p=scopy(p,"  IC 0x"); p=hx(p,t->el_ic,4); p=scopy(p,"  firmware "); p=udec(p,t->el_fw); p=scopy(p,"  pattern "); p=udec(p,(u32)t->el_pattern); p=scopy(p,t->el_replen==39?"  high-precision reports":"  standard reports"); if(t->el_tp_pkts){ p=scopy(p,"  trackpoint packets "); p=udec(p,t->el_tp_pkts); } *p=0; term_puts(b); term_putc('\n'); }
    if(t->mode==1){ p=b; p=scopy(p,"  fingers "); p=udec(p,(u32)t->lay.nfing); p=scopy(p,"  X 0-"); p=udec(p,(u32)t->lay.xmax); p=scopy(p," Y 0-"); p=udec(p,(u32)t->lay.ymax); p=scopy(p,"  size "); p=udec(p,(t->lay.xum?t->lay.xum:100000)/1000); *p++='x'; p=udec(p,(t->lay.yum?t->lay.yum:60000)/1000); p=scopy(p," mm"); p=scopy(p,t->lay.btn[0].ok?"  clickpad button":"  no button"); *p=0; term_puts(b); term_putc('\n'); }
    p=b; p=scopy(p,"  "); { extern int igp_ready(void); { extern int (*g_tp_amd_ready)(void); p=scopy(p,(g_tp_amd_ready&&g_tp_amd_ready())?"interrupt pin read (AMD GPIO)":igp_ready()?"interrupt pin read (Intel GPIO)":"polled every 10 ms"); } } p=scopy(p,"  reports "); p=udec(p,t->reads); p=scopy(p," ("); p=udec(p,t->rate); p=scopy(p,"/s)  frames "); p=udec(p,t->frames); if(t->frames_split){ p=scopy(p," (split "); p=udec(p,t->frames_split); p=scopy(p,", incomplete "); p=udec(p,t->frames_lost); *p++=')'; } p=scopy(p,"  last gesture: "); p=scopy(p,tp_gname(t->gest_last)); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"  taps L/R/M "); p=udec(p,(u32)t->eng.tap_l); *p++='/'; p=udec(p,(u32)t->eng.tap_r); *p++='/'; p=udec(p,(u32)t->eng.tap_m); p=scopy(p,"  drags "); p=udec(p,t->eng.drags); p=scopy(p,"  zone clicks "); p=udec(p,t->eng.zones_used); p=scopy(p,"  palms "); p=udec(p,t->eng.palms); p=scopy(p,"  typing blocks "); p=udec(p,t->eng.dwt_skips);
    p=scopy(p,"  | tap "); p=scopy(p,g_tp_tap?"on":"off"); p=scopy(p," speed "); p=udec(p,(u32)g_tp_speed); p=scopy(p,g_tp_natural?" natural scroll":" classic scroll"); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"  fingers now: "); p=udec(p,(u32)t->ncont); for(int i=0;i<t->ncont&&i<5;i++){ p=scopy(p,"  #"); p=udec(p,(u32)t->cont[i].id); *p++='('; p=udec(p,(u32)t->cont[i].x); *p++=','; p=udec(p,(u32)t->cont[i].y); *p++=')'; } *p=0; term_puts(b); term_putc('\n'); }
void tpinfo_dump(void){ tpdev* t=&g_tp; char b[120],*p; if(!g_tp_probed)tp_init();
    int n=g_tpk_n; if(!g_tpk||n<=0){ term_puts("no report descriptor was read - run tpinfo scan first\n"); return; }
    p=b; p=scopy(p,"report descriptor "); p=udec(p,(u32)n); p=scopy(p," bytes  vendor "); p=hx(p,g_tpk_vid,4); p=scopy(p," product "); p=hx(p,g_tpk_pid,4); p=scopy(p,"  addr 0x"); p=hx(p,g_tpk_addr,2); p=scopy(p," reg 0x"); p=hx(p,g_tpk_reg,4); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"reads "); p=scopy(p,g_tpk_same?"match":"differ"); p=scopy(p,g_tpk_slow?"  speed 100 kHz":"  speed fast"); p=scopy(p,"  items "); p=udec(p,(u32)g_tpk_items); p=scopy(p,"  skipped "); p=udec(p,(u32)g_tpk_skip);
    if(g_tpk_skipoff>=0){ p=scopy(p," first at byte "); p=udec(p,(u32)g_tpk_skipoff); } if(g_tpk_stop>=0){ p=scopy(p,"  cut at byte "); p=udec(p,(u32)g_tpk_stop); } if(g_tpk_depth){ p=scopy(p,"  collection errors "); p=udec(p,(u32)g_tpk_depth); } *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"layout: "); p=scopy(p,g_tpk_ptp?"precision touchpad":g_tpk_mouse?"mouse":"none"); p=scopy(p,"  fingers "); p=udec(p,(u32)g_tpk_nfing); p=scopy(p,"  result "); p=scopy(p,g_tpk_res==0?"OK":g_tpk_res==-3?"not understood":g_tpk_res==-4?"no layout":"failed"); *p=0; term_puts(b); term_putc('\n');
    if(t->ok){ p=b; p=scopy(p,"touchpad now running: "); p=scopy(p,t->proto==1?"ELAN I2C protocol":t->mode==1?"precision touchpad":"mouse mode"); *p=0; term_puts(b); term_putc('\n'); }
    static const char H[]="0123456789ABCDEF";
    for(int o=0;o<n;o+=32){ p=b; p=hx(p,(u32)o,3); *p++=':'; for(int k=0;k<32&&o+k<n;k++){ u8 v=g_tpk[o+k]; *p++=H[v>>4]; *p++=H[v&15]; } *p=0; term_puts(b); term_putc('\n'); } }
