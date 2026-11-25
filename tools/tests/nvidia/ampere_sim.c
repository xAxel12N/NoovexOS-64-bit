#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "include/nvx/base.h"
static u32* R; static u32 wlog_a[4096], wlog_v[4096]; static int wn=0; static u32 boot0=0x172000A1u; static int stuck=0;
static void st(u32 o,u32 v){ R[o>>2]=v; }
u32 nvr32(u32 o){ if(o==0)return boot0; return R[o>>2]; }
void nvw32(u32 o,u32 v){ if(wn<4096){ wlog_a[wn]=o; wlog_v[wn]=v; wn++; }
    u32 b=o&~0xFFFu, r=o&0xFFFu;
    if(r==0x118&&(b==0x110000u||b==0x840000u)){ R[o>>2]=v|2u; return; }
    if(r==0x100&&(v&2u)){ R[o>>2]=v|0x10u; return; }
    if((o==0x111668u||o==0x841668u)&&!v){ R[o>>2]=1; return; }
    if(r==0x3C0){ R[o>>2]=v; if(!(v&1u))R[(b+0x0F4)>>2]&=~0x1000u; return; }
    if(o==0x000600u&&stuck){ R[o>>2]=v&~(1u<<9); return; }
    R[o>>2]=v; }
void* kmalloc(unsigned long n){ return calloc(1,n?n:1); }
void kfree(void* p){ (void)p; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; return p; }
char* hx(char* p,u32 v,int dg){ static const char H[]="0123456789ABCDEF"; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&15]; return p; }
void klog(const char* s){ (void)s; }
u64 hr_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec; }
void hr_delay_us(u32 us){ (void)us; }
u64 kva_to_pa(u64 va){ return va; }
u8 g_nv_vbios[65536]; int g_nv_vbios_len=0;
#include "drivers/video/nvidia/nvmem.c"
#include "drivers/video/nvidia/nvflcn.c"
#include "drivers/video/nvidia/nvgen.c"
int nvd_ampere(void){ return g_nv_ampere_on; }

static int fails=0; static void chk(const char* w,int c){ printf("  %-70s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static int widx(u32 a,u32 v,int from){ for(int i=from;i<wn;i++) if(wlog_a[i]==a&&(v==0xFFFFFFFFu||wlog_v[i]==v))return i; return -1; }

int main(void){
    R=(u32*)calloc(1,0x1000000); if(!R)return 1;
    printf("Ampere GA10x GSP boot hooks (nvgen.c) against simulated registers\n");
    chk("GA102/GA103/GA104/GA106/GA107 map to the GA10x table",nvgsp_gen_for(0x172)==&nvgsp_gen_ga10x&&nvgsp_gen_for(0x173)==&nvgsp_gen_ga10x&&nvgsp_gen_for(0x174)==&nvgsp_gen_ga10x&&nvgsp_gen_for(0x176)==&nvgsp_gen_ga10x&&nvgsp_gen_for(0x177)==&nvgsp_gen_ga10x);
    chk("GA100 and the unused 0x175 are not claimed",!nvgsp_gen_for(0x170)&&!nvgsp_gen_for(0x175));
    chk("Turing chips keep the hook-less Turing tables",nvgsp_gen_for(0x164)==&nvgsp_gen_tu10x&&nvgsp_gen_for(0x168)==&nvgsp_gen_tu11x&&!nvgsp_gen_tu10x.flcn_load&&!nvgsp_gen_tu11x.flcn_load);
    chk("GA10x uses .fwsignature_ga10x and is marked untested",!strcmp(nvgsp_gen_ga10x.sig_section,".fwsignature_ga10x")&&nvgsp_gen_ga10x.untested);
    g_nv_ampere_on=0;
    chk("Ampere PCI IDs refused while gpu ampere is off",!nvgsp_gen_pci_ok(0x2204)&&!nvgsp_gen_pci_ok(0x2484)&&!nvgsp_gen_pci_ok(0x25A2));
    chk("Turing PCI IDs accepted as before",nvgsp_gen_pci_ok(0x1E07)&&nvgsp_gen_pci_ok(0x1F82)&&nvgsp_gen_pci_ok(0x21C4));
    g_nv_ampere_on=1;
    chk("Ampere PCI IDs accepted with gpu ampere on, Ada still refused",nvgsp_gen_pci_ok(0x2204)&&nvgsp_gen_pci_ok(0x2504)&&!nvgsp_gen_pci_ok(0x2684)&&!nvgsp_gen_pci_ok(0x20B0));
    chk("FWSEC fuse index: no fuse blown picks signature 0",nvgsp_gen_fuse_idx(0,0x1,1)==0);
    chk("FWSEC fuse index: fuse 0x3 with versions 0x7 picks signature 2",nvgsp_gen_fuse_idx(0x3,0x7,3)==2);
    chk("FWSEC fuse index: unsupported fuse level is refused",nvgsp_gen_fuse_idx(0x3,0x3,2)==-1&&nvgsp_gen_fuse_idx(0x80000000u,0xFFFF,16)==-1);
    g_nvgen=&nvgsp_gen_ga10x;
    chk("WPR heap on GA10x adds the LibOS3 carve-out",nv_wpr_heap_size(8ULL<<30)==(8ULL<<20)+(1ULL<<20)+(96ULL<<20)+(20ULL<<20));
    g_nvgen=0;
    chk("WPR heap on Turing unchanged",nv_wpr_heap_size(8ULL<<30)==(8ULL<<20)+(1ULL<<20)+(96ULL<<20));
    st(NVF_GSP+0x0F4,0x80001000u); st(NVF_GSP+0x1668,0x10u); wn=0;
    int ok=nvgsp_gen_ga102_reset(NVF_GSP);
    int s=widx(NVF_GSP+0x1668,0,0), a=widx(NVF_GSP+0x3C0,1,0), b=widx(NVF_GSP+0x3C0,0,a<0?0:a), c=widx(NVF_GSP+0x084,boot0,0);
    chk("falcon reset: select, engine reset pulse, scrub wait, BOOT0 copy in order",ok&&s>=0&&a>s&&b>a&&c>b&&widx(NVF_GSP+0x014,0xFFFFFFFFu,0)>=0);
    nvgsp_ldr l; memset(&l,0,sizeof(l)); l.pa=0x12345600ULL; l.imem_off=0; l.imem_base=0; l.imem_size=512; l.dmem_off=512; l.dmem_base=0; l.dmem_size=256; l.dmem_sign=0x40; l.engine_id=0x400; l.ucode_id=9; l.bootvec=0;
    wn=0; ok=nvgsp_gen_ga102_load(NVF_GSP,&l);
    int x0=widx(NVF_GSP+0x118,0x614,0), x1=widx(NVF_GSP+0x118,0x614,x0+1), x2=widx(NVF_GSP+0x118,0x600,0);
    chk("FBIF set up before DMA (0x624 bit 7, 0x10c=0, 0x600 physical)",ok&&widx(NVF_GSP+0x10C,0,0)>=0&&(R[(NVF_GSP+0x600)>>2]&0x10007u)==5u&&(R[(NVF_GSP+0x624)>>2]&0x80u));
    chk("IMEM: two secure 256-byte DMA transfers from the image base",x0>=0&&x1>x0&&widx(NVF_GSP+0x110,0x123456,0)>=0&&widx(NVF_GSP+0x11C,0x100,0)>=0);
    chk("DMEM: one 256-byte transfer from image+DMEM offset",x2>x1&&widx(NVF_GSP+0x110,(u32)((0x12345600ULL+512)>>8),0)>x1);
    l.imem_size=300; chk("DMA refuses lengths that are not 256-byte multiples",!nvgsp_gen_ga102_load(NVF_GSP,&l)); l.imem_size=512;
    wn=0; u32 m0=1,m1=1; ok=nvgsp_gen_ga102_boot(NVF_GSP,&l,0,0,&m0,&m1,100);
    int b0=widx(NVF_GSP+0x1210,0x40,0), b1=widx(NVF_GSP+0x119C,0x400,0), b2=widx(NVF_GSP+0x1198,9,0), b3=widx(NVF_GSP+0x1180,1,0), b4=widx(NVF_GSP+0x100,2,0);
    chk("boot: PKC offset, engine id, ucode id, BROM start, then CPUCTL start",ok&&b0>=0&&b1>b0&&b2>b1&&b3>b2&&b4>b3&&m0==0);
    nvgsp_ldr bl; memset(&bl,0,sizeof(bl)); bl.engine_id=1; bl.ucode_id=3; bl.fuse_ver=3;
    st(0x824140+8,0x1); chk("booter signature: SEC2 fuse 0x824148 level 1, version 3 picks index 2",nvgsp_gen_ga102_booter_sig(4,&bl)==2);
    st(0x824140+8,0x0); chk("booter signature: no fuse picks the last signature",nvgsp_gen_ga102_booter_sig(4,&bl)==3);
    st(0x824140+8,0xF); chk("booter signature: fuse newer than the image is refused",nvgsp_gen_ga102_booter_sig(4,&bl)==-1);
    bl.engine_id=2; chk("booter signature: unknown engine is refused",nvgsp_gen_ga102_booter_sig(4,&bl)==-1);
    nvgsp_ldr fl; memset(&fl,0,sizeof(fl)); fl.engine_id=0x400; fl.ucode_id=1; fl.fuse_ver=0x3; st(0x8241C0,0x1);
    chk("FWSEC signature from GSP fuse 0x8241c0",nvgsp_gen_ga102_fwsec_sig(2,&fl)==1);
    st(NVF_GSP+0x0F4,0x80001000u); st(NVF_GSP+0x1668,0); ok=nvgsp_gen_ga102_gsp_reset();
    chk("GSP reset sets 0x111668 bits 0x111",ok&&(R[(NVF_GSP+0x1668)>>2]&0x111u)==0x111u);
    st(NVF_GSP+0x1388,0); int r0=nvgsp_gen_ga102_riscv_active(); st(NVF_GSP+0x1388,0x80); int r1=nvgsp_gen_ga102_riscv_active();
    chk("RISC-V active read from 0x111388 bit 7",!r0&&r1);
    g_nvgen=0; const nvgsp_cls* cl=nvgsp_cls_cur();
    chk("no generation chosen: Turing classes (c46f, c5b5, c597, c5c0, c57d/c57e)",cl->chan==0xC46Fu&&cl->ce==0xC5B5u&&cl->threed==0xC597u&&cl->compute==0xC5C0u&&cl->core==0xC57Du&&cl->wndw==0xC57Eu&&cl->disp==0xC570u);
    g_nvgen=nvgsp_gen_for(0x164); cl=nvgsp_cls_cur();
    chk("TU104 keeps Turing classes, doorbell 0xbb0090, pitch storage bit",cl==&nvgsp_cls_tu10x&&cl->doorbell==0xBB0090u&&cl->wndw_storage_pitch==0x10u&&cl->qmd_major==2&&cl->sm==75);
    g_nvgen=nvgsp_gen_for(0x168); chk("TU116 keeps Turing classes",nvgsp_cls_cur()==&nvgsp_cls_tu10x);
    for(u32 ch=0x172;ch<=0x177;ch++){ if(ch==0x175)continue; g_nvgen=nvgsp_gen_for(ch); cl=nvgsp_cls_cur();
        if(!(cl==&nvgsp_cls_ga10x))break; }
    chk("GA102-GA107 select the GA10x class table",cl==&nvgsp_cls_ga10x);
    chk("GA10x: AMPERE_CHANNEL_GPFIFO_A c56f, AMPERE_USERMODE_A c561",cl->chan==0xC56Fu&&cl->user==0xC561u);
    chk("GA10x: AMPERE_DMA_COPY_B c7b5, FERMI_TWOD_A 902d",cl->ce==0xC7B5u&&cl->twod==0x902Du);
    chk("GA10x: AMPERE_B c797, AMPERE_COMPUTE_B c7c0",cl->threed==0xC797u&&cl->compute==0xC7C0u);
    chk("GA10x: GA102_DISP c670, core c67d, window c67e, wimm c67b, cursor c67a",cl->disp==0xC670u&&cl->core==0xC67Du&&cl->wndw==0xC67Eu&&cl->wimm==0xC67Bu&&cl->curs==0xC67Au);
    chk("GA10x: doorbell stays at 0xbb0090 (vfn 0xb80000 + user 0x30000)",cl->doorbell==0xBB0090u);
    chk("GA10x: C67E SET_STORAGE has no memory-layout bit",cl->wndw_storage_pitch==0);
    chk("GA10x: compositor/3D shaders refused (QMD 3, sm_86)",cl->qmd_major==3&&cl->sm==86);
    g_nvgen=0;
    for(u32 i=0;i<64;i++)st(0x022800+i*4,0);
    st(0x0224FC,8u<<20);
    st(0x022800,0x80000000u|(0x00u<<24)|5u); st(0x022804,0x80000000u|0x400000u|12u); st(0x022808,0x00001000u);
    st(0x02280C,0x80000000u|(0x0Du<<24)|0x1Fu); st(0x022810,0x80000000u|0x840000u|9u); st(0x022814,0);
    chk("device info: SEC2 (type 0x0d) reset id 9 -> PMC mask bit 9",nvgsp_gen_top_reset(0x0D)==(1u<<9));
    chk("device info: GR reset id 12, missing type gives no mask",nvgsp_gen_top_reset(0x00)==(1u<<12)&&nvgsp_gen_top_reset(0x14)==0);
    st(0x0224FC,0); chk("device info: empty table gives no mask",nvgsp_gen_top_reset(0x0D)==0); st(0x0224FC,8u<<20);
    st(0x000600,0xFFFFFFFFu); st(NVF_SEC2+0x0F4,0x80001000u); st(NVF_SEC2+0x1668,0x10u); wn=0;
    ok=nvgsp_gen_ga102_reset(NVF_SEC2);
    int q0=widx(NVF_SEC2+0x1668,0,0), q1=widx(NVF_SEC2+0x048,0xFFFFFFFFu,q0<0?0:q0), q2=widx(NVF_SEC2+0x014,0xFFFFFFFFu,0);
    int p0=widx(0x000600,0xFFFFFDFFu,0), e0=widx(NVF_SEC2+0x3C0,1,0), e1=widx(NVF_SEC2+0x3C0,0,e0<0?0:e0), e2=widx(NVF_SEC2+0x3C0,1,e1<0?0:e1), e3=widx(NVF_SEC2+0x3C0,0,e2<0?0:e2);
    int s2=widx(NVF_SEC2+0x1668,0,e3<0?0:e3), p1=widx(0x000600,0xFFFFFFFFu,e3<0?0:e3), bt=widx(NVF_SEC2+0x084,boot0,0);
    chk("SEC2 reset: select, IRQ mask off, PMC disable before the engine pulse",ok&&q0>=0&&q1>q0&&q2>q1&&p0>q2&&e0>p0);
    chk("SEC2 reset: two engine pulses (disable, enable), then select, then PMC enable",e1>e0&&e2>e1&&e3>e2&&p1>e3&&(s2<0||s2<p1));
    chk("SEC2 reset: BOOT0 copied to 0x084 after PMC enable, SEC2 bit back on",bt>p1&&(R[0x600>>2]&(1u<<9)));
    st(NVF_GSP+0x0F4,0x80001000u); st(NVF_GSP+0x1668,0x10u); wn=0; ok=nvgsp_gen_ga102_reset(NVF_GSP);
    chk("GSP falcon reset never touches PMC 0x600",ok&&widx(0x000600,0xFFFFFFFFu,0)<0);
    st(0x0224FC,0); st(NVF_SEC2+0x0F4,0x80001000u); wn=0; ok=nvgsp_gen_ga102_reset(NVF_SEC2);
    chk("SEC2 reset without a device-info entry skips PMC like nouveau under GSP-RM",ok&&widx(0x000600,0xFFFFFFFFu,0)<0);
    st(0x0224FC,8u<<20); st(NVF_SEC2+0x0F4,0x80001000u); wn=0; stuck=1; ok=nvgsp_gen_ga102_reset(NVF_SEC2); stuck=0;
    chk("SEC2 reset fails closed when PMC does not re-enable the engine",!ok&&widx(NVF_SEC2+0x084,boot0,0)<0);
    printf(fails?"FAILURES: %d\n":"ALL PASS\n",fails); return fails?1:0; }
