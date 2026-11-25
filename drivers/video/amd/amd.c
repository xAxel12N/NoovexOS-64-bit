#include "drivers/video/amd/amd.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
const char* gpu_name(u16 ven,u16 dev);

amd_dev_t g_amd;

extern int amd_probe(void) __attribute__((weak));
extern int amd_vbios_fetch(void) __attribute__((weak));
extern int amd_temp_c(void) __attribute__((weak));
extern int amd_sclk_mhz(void) __attribute__((weak));
extern int amd_fclk_mhz(void) __attribute__((weak));
extern int amd_gpu_busy_pct(void) __attribute__((weak));
extern const char* amd_smu_name(void) __attribute__((weak));
extern int amd_smu_status(char* buf,int n) __attribute__((weak));
extern int amd_dc_scan(void) __attribute__((weak));
extern int amd_dc_edid(u8* out,int max) __attribute__((weak));
extern int amd_dc_info(char* buf,int n) __attribute__((weak));
extern int amd_atom_info(char* buf,int n) __attribute__((weak));
extern int amd_backlight_set(int pct) __attribute__((weak));
extern int amd_backlight_get(void) __attribute__((weak));
extern int amd_flip_test(void) __attribute__((weak));
extern void amdflip_boot(void) __attribute__((weak));
extern void amdsdma_boot(void) __attribute__((weak));
extern int amdsdma_describe(char* o,int cap) __attribute__((weak));
extern int amdflip_setting(int on) __attribute__((weak));
extern int amdflip_describe(char* o,int cap) __attribute__((weak));
extern int g_amdflip_live;

static const char* const AMD_IPNAMES[AMD_IP_MAX]={"GC","DCN","MP0","MP1","NBIO","SDMA0","MMHUB","ATHUB","HDP","OSSSYS","SMUIO","THM","CLK","DF","UMC","VCN"};

static int amd_pci_find(void){
    if(!pci_n)pci_scan();
    for(int k=0;k<pci_n;k++) if(pci_ven[k]==0x1002&&pci_cls[k]==0x03){ g_amd.bus=pci_bus[k]; g_amd.slot=pci_slot[k]; g_amd.fn=0; g_amd.did=pci_dev[k]; return 1; }
    return 0; }

static int amd_basic_probe(void){
    if(!amd_pci_find()){ scopy(g_amd.why,"no AMD graphics on PCI")[0]=0; return -1; }
    g_amd.found=1; int b=g_amd.bus,s=g_amd.slot,f=g_amd.fn;
    g_amd.revid=(u8)pci_cfg(b,s,f,8); u32 ss=pci_cfg(b,s,f,0x2C); g_amd.ssvid=(u16)ss; g_amd.ssdid=(u16)(ss>>16);
    u32 b0=pci_cfg(b,s,f,0x10),b1=pci_cfg(b,s,f,0x14),b2=pci_cfg(b,s,f,0x18),b3=pci_cfg(b,s,f,0x1C),b5=pci_cfg(b,s,f,0x24);
    g_amd.fb_pa=(u64)(b0&0xFFFFFFF0u)|(((b0&6)==4)?((u64)b1<<32):0); g_amd.doorbell_pa=(u64)(b2&0xFFFFFFF0u)|(((b2&6)==4)?((u64)b3<<32):0);
    g_amd.mmio_pa=(u64)(b5&0xFFFFFFF0u);
    if(b5&1||!g_amd.mmio_pa){ scopy(g_amd.why,"register BAR5 not assigned")[0]=0; return -1; }
    if(!(pci_cfg(b,s,f,4)&2)){ scopy(g_amd.why,"memory decoding off")[0]=0; return -1; }
    if(!acpi_hi_map(g_amd.mmio_pa)){ scopy(g_amd.why,"registers not reachable")[0]=0; return -1; }
    g_amd.mmio=(volatile u8*)g_amd.mmio_pa; if(!g_amd.mmio_len)g_amd.mmio_len=0x80000;
    const char* n=gpu_name(0x1002,g_amd.did); g_amd.asic_name=n;
    u16 d=g_amd.did; g_amd.apu=(d>=0x1300&&d<=0x16FF)||d==0x15BF||d==0x15C8||d==0x1900||d==0x1901;
    return 0; }

int amd_present(void){ return g_amd.found&&g_amd.mmio; }

void amd_init(void){
    for(u32 i=0;i<sizeof(g_amd);i++)((u8*)&g_amd)[i]=0;
    if(!amd_pci_find())return;
    int r=amd_probe?amd_probe():amd_basic_probe();
    if(r<0&&!g_amd.found)return;
    if(!g_amd.mmio&&amd_probe){ int keep=g_amd.found; amd_basic_probe(); g_amd.found=keep|g_amd.found; }
    if(g_amd.mmio&&amd_vbios_fetch)amd_vbios_fetch();
    if(g_amd.mmio&&amd_dc_scan)amd_dc_scan();
    g_amd.ready=g_amd.mmio!=0;
    char b[200],*p=b; p=scopy(p,"amdgpu: "); p=scopy(p,g_amd.asic_name?g_amd.asic_name:"AMD graphics"); p=scopy(p," 1002:"); p=hx(p,g_amd.did,4); p=scopy(p," rev "); p=hx(p,g_amd.revid,2);
    if(g_amd.mmio){ p=scopy(p," mmio "); p=uhex(p,g_amd.mmio_pa,8); } else { p=scopy(p," - "); p=scopy(p,g_amd.why); }
    if(g_amd.disc_ok){ p=scopy(p," GC "); p=udec(p,g_amd.ip[AMD_IP_GC].maj); *p++='.'; p=udec(p,g_amd.ip[AMD_IP_GC].min); *p++='.'; p=udec(p,g_amd.ip[AMD_IP_GC].rev); p=scopy(p," DCN "); p=udec(p,g_amd.ip[AMD_IP_DCN].maj); *p++='.'; p=udec(p,g_amd.ip[AMD_IP_DCN].min); *p++='.'; p=udec(p,g_amd.ip[AMD_IP_DCN].rev); }
    p=scopy(p,", screen stays on the firmware framebuffer"); *p=0; klog(b);
    if(g_amd.mmio&&amdsdma_boot)amdsdma_boot();
    if(g_amd.mmio&&amdflip_boot)amdflip_boot(); }

static void amd_out(const char* s){ term_puts(s); term_putc('\n'); }
static char* amd_ver(char* p,const amd_ip_t* v){ p=udec(p,v->maj); *p++='.'; p=udec(p,v->min); *p++='.'; p=udec(p,v->rev); return p; }
static char* amd_sdec(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return udec(p,(u32)v); }
static void amd_lines(const char* s){ char l[160]; int i=0; for(;;s++){ if(*s=='\n'||!*s||i>=158){ l[i]=0; if(i){ term_puts("  "); amd_out(l); } i=0; if(!*s)break; if(*s!='\n')l[i++]=*s; } else l[i++]=*s; } }

static void amd_status(void){
    char b[640],*p;
    if(!g_amd.found){ amd_out("  no AMD graphics found"); return; }
    tcol(WHT); p=b; p=scopy(p,"  "); p=scopy(p,g_amd.asic_name?g_amd.asic_name:"AMD graphics"); p=scopy(p,"  1002:"); p=hx(p,g_amd.did,4); p=scopy(p," rev "); p=hx(p,g_amd.revid,2); p=scopy(p,g_amd.apu?"  (APU)":"  (discrete)"); *p=0; amd_out(b); tcol(GREY);
    p=b; p=scopy(p,"  PCI "); p=hx(p,g_amd.bus,2); *p++=':'; p=hx(p,g_amd.slot,2); *p++='.'; p=udec(p,g_amd.fn); p=scopy(p,"  subsystem "); p=hx(p,g_amd.ssvid,4); *p++=':'; p=hx(p,g_amd.ssdid,4); *p=0; amd_out(b);
    p=b; p=scopy(p,"  registers "); if(g_amd.mmio){ p=uhex(p,g_amd.mmio_pa,8); p=scopy(p," ("); p=udec(p,g_amd.mmio_len>>10); p=scopy(p," KB)"); } else { p=scopy(p,"not mapped - "); p=scopy(p,g_amd.why); } p=scopy(p,"  framebuffer BAR "); p=uhex(p,g_amd.fb_pa,9); *p=0; amd_out(b);
    p=b; p=scopy(p,"  IP discovery: "); p=scopy(p,g_amd.disc_ok?"read":"not read"); if(g_amd.disc_ok){ p=scopy(p,"  GC "); p=amd_ver(p,&g_amd.ip[AMD_IP_GC]); p=scopy(p,"  DCN "); p=amd_ver(p,&g_amd.ip[AMD_IP_DCN]); p=scopy(p,"  MP1 "); p=amd_ver(p,&g_amd.ip[AMD_IP_MP1]); } *p=0; amd_out(b);
    p=b; p=scopy(p,"  VBIOS: "); if(g_amd.vbios&&g_amd.vbios_len){ p=udec(p,g_amd.vbios_len>>10); p=scopy(p," KB from "); p=scopy(p,g_amd.vbios_src==AMD_VBIOS_VFCT?"ACPI VFCT":g_amd.vbios_src==AMD_VBIOS_ROMBAR?"ROM BAR":"legacy ROM"); } else p=scopy(p,"not loaded"); *p=0; amd_out(b);
    p=b; p=scopy(p,"  SMU: "); { const char* n=amd_smu_name?amd_smu_name():0; p=scopy(p,n?n:"not available"); } if(amd_temp_c){ int t=amd_temp_c(); if(t>=0){ p=scopy(p,"  "); p=udec(p,(u32)t); p=scopy(p," C"); } } if(amd_sclk_mhz){ int m=amd_sclk_mhz(); if(m>0){ p=scopy(p,"  GFX "); p=udec(p,(u32)m); p=scopy(p," MHz"); } } *p=0; amd_out(b);
    p=b; p=scopy(p,"  backlight: "); if(amd_backlight_get){ int v=amd_backlight_get(); if(v>=0){ p=udec(p,(u32)v); p=scopy(p," %"); } else p=scopy(p,"not controllable"); } else p=scopy(p,"no driver"); *p=0; amd_out(b);
    if(amdflip_describe&&amdflip_describe(b,sizeof b)>0){ amd_out("  driver: phase 2 - firmware mode kept, scanout taken over with page flips"); amd_lines(b); }
    if(amdsdma_describe&&amdsdma_describe(b,sizeof b)>0)amd_lines(b);
    else amd_out("  driver: phase 1 (read-only); the screen stays on the UEFI framebuffer"); }

static void amd_info(void){
    char b[1024],*p;
    amd_status(); if(!g_amd.found)return;
    if(g_amd.disc_ok){ tcol(WHT); amd_out("  IP blocks"); tcol(GREY);
        for(int i=0;i<AMD_IP_MAX;i++){ const amd_ip_t* v=&g_amd.ip[i]; if(!v->maj&&!v->min&&!v->base[0])continue; p=b; p=scopy(p,"    "); p=scopy(p,AMD_IPNAMES[i]); while(p-b<12)*p++=' '; p=amd_ver(p,v); p=scopy(p,"  x"); p=udec(p,v->ninst?v->ninst:1); p=scopy(p,"  base"); for(int s=0;s<AMD_IP_SEGS;s++){ if(!v->base[s])continue; *p++=' '; p=hx(p,v->base[s],5); } *p=0; amd_out(b); } }
    if(amd_atom_info&&amd_atom_info(b,sizeof b)>0){ tcol(WHT); amd_out("  AtomBIOS"); tcol(GREY); amd_lines(b); }
    if(amd_dc_info&&amd_dc_info(b,sizeof b)>0){ tcol(WHT); amd_out("  Display (as set by firmware)"); tcol(GREY); amd_lines(b); }
    if(amd_smu_status&&amd_smu_status(b,sizeof b)>0){ tcol(WHT); amd_out("  SMU"); tcol(GREY); amd_lines(b); } }

static void amd_edid_cmd(void){
    u8 e[256]; char b[120],*p;
    if(!amd_dc_edid){ amd_out("  EDID reading is not in this build"); return; }
    int n=amd_dc_edid(e,256); if(n<128){ amd_out("  no EDID read from the panel (DDC/AUX)"); return; }
    static const u8 H[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; int ok=1; for(int i=0;i<8;i++)if(e[i]!=H[i])ok=0;
    u8 sum=0; for(int i=0;i<128;i++)sum+=e[i];
    p=b; p=scopy(p,"  EDID "); p=udec(p,(u32)n); p=scopy(p," bytes, header "); p=scopy(p,ok?"ok":"bad"); p=scopy(p,", checksum "); p=scopy(p,sum?"bad":"ok"); *p=0; amd_out(b);
    if(ok){ u16 m=(u16)((e[8]<<8)|e[9]); p=b; p=scopy(p,"  maker "); *p++=(char)('@'+((m>>10)&31)); *p++=(char)('@'+((m>>5)&31)); *p++=(char)('@'+(m&31)); p=scopy(p," product "); p=hx(p,(u32)(e[10]|(e[11]<<8)),4); p=scopy(p,"  EDID "); p=udec(p,e[18]); *p++='.'; p=udec(p,e[19]); *p=0; amd_out(b);
        const u8* t=e+54; if(t[0]|t[1]){ u32 pc=(u32)(t[0]|(t[1]<<8))*10; u32 ha=t[2]|((t[4]&0xF0u)<<4), hb=t[3]|((t[4]&0x0Fu)<<8), va=t[5]|((t[7]&0xF0u)<<4), vb=t[6]|((t[7]&0x0Fu)<<8); u32 tot=(ha+hb)*(va+vb);
            p=b; p=scopy(p,"  preferred "); p=udec(p,ha); *p++='x'; p=udec(p,va); if(tot){ *p++='@'; p=udec(p,(u32)((u64)pc*1000ULL/tot)); p=scopy(p," Hz"); } p=scopy(p,"  pixel clock "); p=udec(p,pc/1000); p=scopy(p," MHz"); *p=0; amd_out(b); }
        for(int d=0;d<4;d++){ const u8* q=e+54+d*18; if(q[0]||q[1]||q[3]!=0xFC)continue; p=b; p=scopy(p,"  name "); for(int i=5;i<18&&q[i]!=0x0A;i++)*p++=(char)(q[i]>=32&&q[i]<127?q[i]:'?'); *p=0; amd_out(b); } }
    for(int r=0;r<n&&r<256;r+=16){ p=b; p=scopy(p,"    "); p=hx(p,(u32)r,2); *p++=':'; for(int i=0;i<16&&r+i<n;i++){ *p++=' '; p=hx(p,e[r+i],2); } *p=0; amd_out(b); } }

void amd_cmd(const char* a){
    char b[120],*p; while(*a==' ')a++;
    if(!a[0]||seq(a,"status")){ amd_status(); return; }
    if(seq(a,"help")){ tcol(CYAN); amd_out("amdgpu: AMD Radeon (Ryzen APU) driver, phase 1 - read-only, screen stays on the UEFI framebuffer"); tcol(WHT); amd_out("  amdgpu status | info | edid | temp | bl [0-100] | flip [on|off|test]"); return; }
    if(!g_amd.found){ amd_out("  no AMD graphics found"); return; }
    if(seq(a,"info")){ amd_info(); return; }
    if(seq(a,"edid")){ amd_edid_cmd(); return; }
    if(seq(a,"temp")){ int t=amd_temp_c?amd_temp_c():-1, g=amd_sclk_mhz?amd_sclk_mhz():-1, f=amd_fclk_mhz?amd_fclk_mhz():-1, u=amd_gpu_busy_pct?amd_gpu_busy_pct():-1;
        if(t<0&&g<0&&f<0&&u<0){ amd_out("  no sensor readings (SMU not answering or not supported yet)"); return; }
        p=b; p=scopy(p,"  GPU "); if(t>=0){ p=amd_sdec(p,t); p=scopy(p," C"); } else p=scopy(p,"temp n/a"); if(g>0){ p=scopy(p,"  GFX "); p=udec(p,(u32)g); p=scopy(p," MHz"); } if(f>0){ p=scopy(p,"  fabric "); p=udec(p,(u32)f); p=scopy(p," MHz"); } if(u>=0){ p=scopy(p,"  busy "); p=udec(p,(u32)u); p=scopy(p," %"); } *p=0; amd_out(b); return; }
    if(a[0]=='b'&&a[1]=='l'&&(a[2]==0||a[2]==' ')){ const char* q=a+2; while(*q==' ')q++;
        if(!amd_backlight_get||!amd_backlight_set){ amd_out("  backlight control is not in this build"); return; }
        if(!*q){ int v=amd_backlight_get(); p=b; p=scopy(p,"  backlight "); if(v>=0){ p=udec(p,(u32)v); p=scopy(p," %"); } else p=scopy(p,"not controllable on this machine"); *p=0; amd_out(b); return; }
        int v=0,dg=0; while(*q>='0'&&*q<='9'&&dg<4){ v=v*10+(*q-'0'); q++; dg++; } if(!dg||*q||v>100){ amd_out("  usage: amdgpu bl <0-100>"); return; }
        int r=amd_backlight_set(v); p=b; if(r<0)p=scopy(p,"  backlight could not be set"); else { p=scopy(p,"  backlight "); p=udec(p,(u32)amd_backlight_get()); p=scopy(p," %"); } *p=0; amd_out(b); return; }
    if(seq(a,"flip on")||seq(a,"flip off")){ if(!amdflip_setting){ amd_out("  page flipping is not in this build"); return; } int on=a[6]=='n'; int r=amdflip_setting(on); amd_out(on?(r==0?"  tear-free page flips on":"  tear-free page flips could not start - see amdgpu flip"):"  tear-free page flips off"); return; }
    if(seq(a,"sdma")){ char d[640]; if(amdsdma_describe&&amdsdma_describe(d,sizeof d)>0)amd_lines(d); else amd_out("  SDMA is not in this build"); return; }
    if(seq(a,"flip")){ char d[320]; if(amdflip_describe&&amdflip_describe(d,sizeof d)>0)amd_lines(d); else amd_out("  page flipping is not in this build"); return; }
    if(seq(a,"flip test")){ if(!amd_flip_test){ amd_out("  flip test is not in this build"); return; } int r=amd_flip_test(); amd_out(r==0?"  flip test passed, scanout restored":"  flip test failed, scanout restored"); return; }
    amd_out("  usage: amdgpu status | info | edid | temp | bl [0-100] | flip [on|off|test]"); }

int amd_tm_tile(char* p,int n){
    if(!amd_present()||n<32)return 0;
    char* s=p; int t=amd_temp_c?amd_temp_c():-1, g=amd_sclk_mhz?amd_sclk_mhz():-1;
    p=scopy(p,"AMD"); if(t>=0){ p=scopy(p,"  "); p=udec(p,(u32)t); p=scopy(p," C"); } if(g>0){ p=scopy(p,"  "); p=udec(p,(u32)g); p=scopy(p," MHz"); } if(t<0&&g<=0)p=scopy(p," detected"); *p=0; return (int)(p-s); }

int amd_tm_row(int i,char* label,char* val){
    if(!amd_present())return 0;
    char* p=val; label[0]=0; val[0]=0;
    switch(i){
    case 0: scopy(label,"Driver")[0]=0; p=scopy(p,"NoovexOS amdgpu phase 1, 1002:"); p=hx(p,g_amd.did,4); *p=0; return 1;
    case 1: scopy(label,"Chip")[0]=0; p=scopy(p,g_amd.asic_name?g_amd.asic_name:"AMD graphics"); if(g_amd.disc_ok){ p=scopy(p,", GC "); p=amd_ver(p,&g_amd.ip[AMD_IP_GC]); } *p=0; return 1;
    case 2: scopy(label,"Temperature")[0]=0; { int t=amd_temp_c?amd_temp_c():-1; if(t>=0){ p=udec(p,(u32)t); p=scopy(p," C"); } else p=scopy(p,"not available"); } *p=0; return 1;
    case 3: scopy(label,"Graphics clock")[0]=0; { int g=amd_sclk_mhz?amd_sclk_mhz():-1; if(g>0){ p=udec(p,(u32)g); p=scopy(p," MHz"); } else p=scopy(p,"not available"); } *p=0; return 1;
    case 4: scopy(label,"Display")[0]=0; p=scopy(p,g_amdflip_live?"Firmware mode, tear-free page flips by the driver":"UEFI framebuffer"); *p=0; return 1;
    case 5: scopy(label,"Backlight")[0]=0; { int v=amd_backlight_get?amd_backlight_get():-1; if(v>=0){ p=udec(p,(u32)v); p=scopy(p," %"); } else p=scopy(p,"not controllable"); } *p=0; return 1; }
    return 0; }
