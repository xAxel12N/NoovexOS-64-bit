/*
 * Copyright 2021-2023 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Ada (AD102/AD103/AD104/AD106/AD107) GSP boot, after nouveau
 * nvkm/subdev/gsp/ad102.c, ga102.c, fwsec.c and nvkm/falcon/ga102.c, ga100.c.
 * Ada class numbers after nouveau nvkm/subdev/gsp/rm/ad10x.c, Copyright (c) 2025 NVIDIA CORPORATION (MIT).
 */
#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/nvidia/nvada.h"
#include "include/nvx/drivers/video/nvidia/nvgsp_gen.h"

extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern const u8 _binary_nvfw_ad102_booter_load_xz_start[], _binary_nvfw_ad102_booter_load_xz_end[];
extern const u8 _binary_nvfw_ad102_booter_unload_xz_start[], _binary_nvfw_ad102_booter_unload_xz_end[];
extern const u8 _binary_nvfw_ad102_bootloader_xz_start[], _binary_nvfw_ad102_bootloader_xz_end[];
extern const u8 _binary_nvfw_ga102_gsp_xz_start[] __attribute__((weak)), _binary_nvfw_ga102_gsp_xz_end[] __attribute__((weak));

int g_nv_ada_on=0;

static const struct { u16 id; const char* name; } g_ada_ids[]={{0x192,"AD102"},{0x193,"AD103"},{0x194,"AD104"},{0x196,"AD106"},{0x197,"AD107"}};
const char* nvada_chip(u32 boot0){ u32 a=(boot0>>20)&0x1FFu; for(u32 i=0;i<sizeof(g_ada_ids)/sizeof(g_ada_ids[0]);i++) if(g_ada_ids[i].id==a)return g_ada_ids[i].name; return 0; }

static u32 rd(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int fls32(u32 v){ int n=0; while(v){ n++; v>>=1; } return n; }
static u32 up256(u32 v){ return (v+255u)&~255u; }
static u8* dma_buf(u32 n){ u8* r=(u8*)kmalloc((unsigned long)n+0x1000u); if(!r)return 0; u8* p=(u8*)(((u64)r+0xFFFULL)&~0xFFFULL); for(u32 i=0;i<n;i++)p[i]=0; return p; }
static u64 phys(const void* p){ u64 a=kva_to_pa((u64)p); return a?a:(u64)p; }

int nvada_booter_parse(const u8* f,u32 len,nvada_fw* o){
    for(u32 i=0;i<sizeof(*o);i++)((u8*)o)[i]=0;
    if(len<0x100||rd(f)!=0x10DEu)return 0;
    u32 ho=rd(f+0x0C), dofs=rd(f+0x10), dsz=rd(f+0x14);
    if(ho>len-0x24||dofs>len||dsz>len-dofs)return 0;
    const u8* h=f+ho; u32 so=rd(h), ss=rd(h+4), pl=rd(h+8), ps=rd(h+12), mo=rd(h+16), ns=rd(h+24), lo=rd(h+28);
    if(pl>len-4||ps>len-4||ns>len-4||mo>len-12||lo>len-28)return 0;
    u32 loc=rd(f+pl), sig=rd(f+ps), cnt=rd(f+ns); const u8* m=f+mo; const u8* l=f+lo;
    u32 dofs2=rd(l+8), dsz2=rd(l+12), napp=rd(l+16), a0o=rd(l+20), a0s=rd(l+24);
    if(!cnt||cnt>16||!napp||!ss||ss%cnt||so>len||sig>len-so||ss>len-so-sig)return 0;
    if(a0o>dsz||a0s>dsz-a0o||dofs2>dsz||dsz2>dsz-dofs2||loc<dofs2||loc>dsz||ss/cnt>dsz-loc)return 0;
    u32 n=up256(dsz)+256u; u8* img=dma_buf(n); if(!img)return 0;
    for(u32 i=0;i<dsz;i++)img[i]=f[dofs+i];
    o->img=img; o->len=n;
    o->imem_off=a0o; o->imem_base=0; o->imem_size=up256(a0s);
    o->dmem_off=dofs2; o->dmem_base=0; o->dmem_size=up256(dsz2); o->dmem_sign=loc-dofs2;
    o->boot_addr=a0o; o->fuse_ver=rd(m); o->engine_id=rd(m+4); o->ucode_id=rd(m+8);
    o->sig_loc=loc; o->sig_size=ss/cnt; o->sig_cnt=cnt; o->sigs=f+so+sig;
    return 1; }

int nvada_fwsec_parse(const u8* rom,u32 rom_len,u32 d,nvada_fw* o){
    for(u32 i=0;i<sizeof(*o);i++)((u8*)o)[i]=0;
    if(d>rom_len||rom_len-d<0x2C)return 0;
    const u8* p=rom+d; u32 hdr=rd(p), sz=hdr>>16, ver=(hdr>>8)&0xFFu;
    if(!(hdr&1u)||ver!=3)return 0;
    u32 pkc=rd(p+8), ifo=rd(p+12), ib=rd(p+16), il=rd(p+20), db=rd(p+28), dl=rd(p+32);
    u32 eng=(u32)p[36]|((u32)p[37]<<8), uc=p[38], cnt=p[39], sv=(u32)p[40]|((u32)p[41]<<8);
    if(!cnt||sz<0x2C+cnt*384u||sz>rom_len-d||il>0x80000u||dl>0x80000u||il+dl>rom_len-d-sz||pkc+384u>up256(dl)||ifo>=dl)return 0;
    u32 n=up256(il)+up256(dl)+256u; u8* img=dma_buf(n); if(!img)return 0;
    for(u32 i=0;i<il+dl;i++)img[i]=p[sz+i];
    o->img=img; o->len=n;
    o->imem_off=0; o->imem_base=ib; o->imem_size=up256(il);
    o->dmem_off=il; o->dmem_base=db; o->dmem_size=up256(dl); o->dmem_sign=pkc;
    o->boot_addr=0; o->fuse_ver=sv; o->ucode_id=uc; o->engine_id=eng;
    o->sig_loc=il+pkc; o->sig_size=384; o->sig_cnt=cnt; o->sigs=p+0x2C; o->if_off=ifo;
    return 1; }

int nvada_fwsec_patch(nvada_fw* fw,u32 cmd,u64 frts_addr,u64 frts_size){
    u8* dm=fw->img+fw->dmem_off; u32 dl=fw->dmem_size;
    if(fw->if_off+4>dl)return 0;
    u8* h=dm+fw->if_off; if(h[0]!=1)return 0;
    u32 hl=h[1], el=h[2], cnt=h[3];
    for(u32 i=0;i<cnt;i++){ u32 eo=fw->if_off+hl+i*el; if(eo+8>dl)return 0;
        if(rd(dm+eo)!=4)continue;
        u32 mo=rd(dm+eo+4); if(mo+0x30>dl)return 0;
        u8* mp=dm+mo; u32 cin=rd(mp+8); if(cin+44>dl)return 0;
        mp[0x2C]=(u8)cmd; mp[0x2D]=(u8)(cmd>>8); mp[0x2E]=(u8)(cmd>>16); mp[0x2F]=(u8)(cmd>>24);
        u32* c=(u32*)(dm+cin); for(int k=0;k<11;k++)c[k]=0;
        c[0]=1; c[1]=24; c[5]=2;
        if(cmd==0x15){ c[6]=1; c[7]=20; c[8]=(u32)(frts_addr>>12); c[9]=(u32)(frts_size>>12); c[10]=2; }
        return 1; }
    return 0; }

int nvada_sign(nvada_fw* fw,int idx){
    if(idx<0||(u32)idx>=fw->sig_cnt||fw->sig_loc+fw->sig_size>fw->len)return 0;
    const u8* s=fw->sigs+(u32)idx*fw->sig_size; for(u32 i=0;i<fw->sig_size;i++)fw->img[fw->sig_loc+i]=s[i]; return 1; }

static int fuse_reg(u32 eng,u32 uc,u32* out){
    if(!uc||uc>64)return 0;
    if(eng&1u)*out=nvr32(0x824140+(uc-1)*4); else if(eng&4u)*out=nvr32(0x824100+(uc-1)*4); else if(eng&0x400u)*out=nvr32(0x8241C0+(uc-1)*4); else return 0;
    return 1; }
static int ga100_sig(const nvada_fw* fw){
    u32 r=0; if(!fuse_reg(fw->engine_id,fw->ucode_id,&r))return -1;
    if(!r)return (int)fw->sig_cnt-1;
    int v=fls32(r); if((int)fw->fuse_ver<v)return -1; return (int)fw->fuse_ver-v; }
static int ga102_fwsec_sig(const nvada_fw* fw){
    if(!(fw->engine_id&0x400u)||!fw->ucode_id||fw->ucode_id>64)return -1;
    u32 r=nvr32(0x8241C0+(fw->ucode_id-1)*4); u32 reg=1u<<fls32(r), sv=fw->fuse_ver; int idx=0;
    if(fls32(r)>=32||!(reg&sv))return -1;
    while(!(reg&sv&1u)){ idx+=(int)(sv&1u); reg>>=1; sv>>=1; }
    return idx; }

static int ga102_reset(u32 f){
    (void)nvr32(f+0x0F4); u64 t0=hr_ns(); while(!(nvr32(f+0x0F4)&0x80000000u)&&hr_ns()-t0<150000ULL)cpu_pause();
    nvm32(f+0x3C0,1u,1u); hr_delay_us(10); nvm32(f+0x3C0,1u,0u);
    if(!nv_poll(f+0x0F4,0x1000u,0u,20))return 0;
    if(nvr32(f+NVADA_ADDR2+0x668)&0x10u){ nvw32(f+NVADA_ADDR2+0x668,0); if(!nv_poll(f+NVADA_ADDR2+0x668,1u,1u,10))return 0; }
    return 1; }
static int ga102_dma(u32 f,u64 pa,u32 off,u32 dst,u32 len,int imem,int sec){
    if(!len||(len&255u))return 0;
    u32 cmd=(6u<<8)|(imem?0x10u:0u)|(sec?4u:0u); u64 base=imem?pa:pa+off; u32 src=imem?off:0;
    nvw32(f+0x110,(u32)(base>>8)); nvw32(f+0x128,0);
    for(u32 o=0;o<len;o+=256){ nvw32(f+0x114,dst+o); nvw32(f+0x11C,src+o); nvw32(f+0x118,cmd); if(!nv_poll(f+0x118,2u,2u,2000))return 0; }
    return 1; }
static int ga102_load(u32 f,const nvada_fw* fw){
    nvm32(f+0x624,0x80u,0x80u); nvw32(f+0x10C,0); nvm32(f+0x600,0x10007u,(1u<<2)|1u);
    u64 pa=phys(fw->img);
    if(!ga102_dma(f,pa,fw->imem_off,fw->imem_base,fw->imem_size,1,1))return 0;
    return ga102_dma(f,pa,fw->dmem_off,fw->dmem_base,fw->dmem_size,0,0); }
static int ga102_boot(u32 f,const nvada_fw* fw,u32 mb0,u32 mb1,u32* o0,u32* o1){
    nvw32(f+NVADA_ADDR2+0x210,fw->dmem_sign); nvw32(f+NVADA_ADDR2+0x19C,fw->engine_id); nvw32(f+NVADA_ADDR2+0x198,fw->ucode_id); nvw32(f+NVADA_ADDR2+0x180,1);
    nvw32(f+0x040,mb0); nvw32(f+0x044,mb1); nvw32(f+0x104,fw->boot_addr); nvw32(f+0x100,2u);
    int ok=nv_poll(f+0x100,0x10u,0x10u,2000); if(o0)*o0=nvr32(f+0x040); if(o1)*o1=nvr32(f+0x044); return ok; }
static int ga102_gsp_reset(void){ if(!ga102_reset(NVF_GSP))return 0; nvm32(NVF_GSP+0x1668,0x111u,0x111u); return 1; }
static int ga102_riscv(u32 f){ return (nvr32(f+NVADA_ADDR2+0x388)&0x80u)!=0; }

const nvada_hooks g_nvada_hooks={ "Ada", NVF_GSP, NVF_SEC2, ".fwsignature_ad10x", ga102_reset, ga102_load, ga102_boot, ga100_sig, ga102_fwsec_sig, ga102_gsp_reset, ga102_riscv };

static void ldr_fw(const nvgsp_ldr* l,nvada_fw* o,u32 cnt){ for(u32 i=0;i<sizeof(*o);i++)((u8*)o)[i]=0; o->img=(u8*)l->img; o->imem_off=l->imem_off; o->imem_base=l->imem_base; o->imem_size=l->imem_size; o->dmem_off=l->dmem_off; o->dmem_base=l->dmem_base; o->dmem_size=l->dmem_size; o->dmem_sign=l->dmem_sign; o->boot_addr=l->bootvec; o->engine_id=l->engine_id; o->ucode_id=l->ucode_id; o->fuse_ver=l->fuse_ver; o->sig_cnt=cnt; }
static int gen_load(u32 f,const nvgsp_ldr* l){ nvm32(f+0x624,0x80u,0x80u); nvw32(f+0x10C,0); nvm32(f+0x600,0x10007u,(1u<<2)|1u);
    return ga102_dma(f,l->pa,l->imem_off,l->imem_base,l->imem_size,1,1)&&ga102_dma(f,l->pa,l->dmem_off,l->dmem_base,l->dmem_size,0,0); }
static int gen_boot(u32 f,const nvgsp_ldr* l,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms){ (void)ms; nvada_fw w; ldr_fw(l,&w,0); return ga102_boot(f,&w,mb0,mb1,o0,o1); }
static int gen_bsig(u32 cnt,const nvgsp_ldr* l){ nvada_fw w; ldr_fw(l,&w,cnt); return ga100_sig(&w); }
static int gen_fsig(u32 cnt,const nvgsp_ldr* l){ nvada_fw w; ldr_fw(l,&w,cnt); int i=ga102_fwsec_sig(&w); return i>=0&&(u32)i<cnt?i:-1; }
static int gen_riscv(void){ return ga102_riscv(NVF_GSP); }
const nvgsp_cls nvgsp_cls_ad10x={ .chan=0xC56Fu, .user=0xC561u, .ce=0xC7B5u, .twod=0x902Du, .threed=0xC997u, .compute=0xC9C0u, .disp=0xC770u, .core=0xC77Du, .wndw=0xC67Eu, .wimm=0xC67Bu, .curs=0xC67Au, .doorbell=0xBB0090u, .wndw_storage_pitch=0x00u, .qmd_major=3, .sm=89 };
const nvgsp_gen nvgsp_gen_ad10x={ .name="Ada AD10x", .chip_lo=0x192, .chip_hi=0x197, .untested=1, .sig_section=".fwsignature_ad10x", .fw_dir="ad102",
    .booter_load="ad102-booter_load-535.113.01.bin", .booter_unload="ad102-booter_unload-535.113.01.bin", .bootloader="ad102-bootloader-535.113.01.bin", .gsp="ga102-gsp-535.113.01.bin",
    .frts_size=0x100000ULL, .heap_os=20ULL<<20, .heap_min=84ULL<<20, .gsp_cap=40u<<20,
    .flcn_reset=ga102_reset, .flcn_load=gen_load, .flcn_boot=gen_boot, .booter_sig=gen_bsig, .fwsec_sig=gen_fsig, .gsp_reset=ga102_gsp_reset, .riscv_active=gen_riscv,
    .blob={ _binary_nvfw_ad102_booter_load_xz_start,_binary_nvfw_ad102_booter_load_xz_end,_binary_nvfw_ad102_booter_unload_xz_start,_binary_nvfw_ad102_booter_unload_xz_end,_binary_nvfw_ad102_bootloader_xz_start,_binary_nvfw_ad102_bootloader_xz_end,_binary_nvfw_ga102_gsp_xz_start,_binary_nvfw_ga102_gsp_xz_end },
    .cls=&nvgsp_cls_ad10x };

int nvada_elf_has(const u8* f,u32 n,const char* name,u32* size){
    if(n<64||f[0]!=0x7F||f[1]!='E'||f[2]!='L'||f[3]!='F'||f[4]!=2)return 0;
    u64 sho=*(const u64*)(f+0x28); u32 shn=*(const u16*)(f+0x3C), shs=*(const u16*)(f+0x3E);
    if(sho>n||(u64)shn*64>n-sho||shs>=shn)return 0;
    const u8* st=f+sho+(u64)shs*64; u64 so=*(const u64*)(st+0x18), sz=*(const u64*)(st+0x20); if(so>n||sz>n-so||!sz)return 0;
    for(u32 i=0;i<shn;i++){ const u8* s=f+sho+(u64)i*64; u32 no=*(const u32*)s; if(no>=sz)continue;
        const char* a=(const char*)f+so+no; const char* b=name; u64 k=no; while(k<sz&&*a&&*a==*b){ a++; b++; k++; }
        if(k<sz&&*a==0&&*b==0){ u64 o=*(const u64*)(s+0x18), z=*(const u64*)(s+0x20); if(o>n||z>n-o)return 0; if(size)*size=(u32)z; return 1; } }
    return 0; }

typedef struct { u8* d; u32 n; const char* src; } adablob;
static adablob g_ab_load, g_ab_unload, g_ab_bl, g_ab_gsp;
static int ablob(adablob* b,const char* file,const u8* s,const u8* e,u32 cap){
    if(b->d)return 1;
    int k=fs_find_name(file); if(k>=0&&FS[k].len>0){ b->n=(u32)FS[k].len; b->d=(u8*)kmalloc(b->n+16); if(!b->d)return 0; const u8* q=(const u8*)fsd(k); for(u32 i=0;i<b->n;i++)b->d[i]=q[i]; b->src="from Files"; return 1; }
    if(!s||!e||e<=s)return 0;
    b->d=(u8*)kmalloc(cap); if(!b->d)return 0; long n=xzk_decode_into(s,(long)(e-s),b->d,(long)cap); if(n<=0){ kfree(b->d); b->d=0; return 0; } b->n=(u32)n; b->src="built in"; return 1; }

static u32 bit_falcon_desc(const u8* r,u32 n,u32 img0,u32 imgd){
    u32 bit=0; for(u32 i=0;i+12<n&&i<img0;i++) if(r[i]==0xFF&&r[i+1]==0xB8&&r[i+2]=='B'&&r[i+3]=='I'&&r[i+4]=='T'){ bit=i; break; }
    if(!bit)return 0;
    u32 esz=r[bit+9], ecnt=r[bit+10], poff=0;
    for(u32 e=0;e<ecnt;e++){ u32 o=bit+12+e*esz; if(o+6>n)return 0; if(r[o]=='p'){ if(r[o+1]!=2)return 0; poff=(u32)r[o+4]|((u32)r[o+5]<<8); break; } }
    if(!poff||poff+4>n)return 0;
    u32 t=rd(r+poff); if(imgd&&t>=img0)t=t-img0+imgd; if(t+4>n)return 0;
    u32 th=r[t+1], tl=r[t+2], tc=r[t+3];
    for(u32 i=0;i<tc;i++){ u32 o=t+th+i*tl; if(o+6>n)return 0; if(r[o]==0x85){ u32 d=rd(r+o+2); if(imgd&&d>=img0)d=d-img0+imgd; return d<n?d:0; } }
    return 0; }

static nvada_fw g_ada_bload, g_ada_bunload;
static int g_ada_state=0;

int nvada_boot(void){
    const nvada_hooks* H=&g_nvada_hooks;
    nvl_reset();
    nvl_s("[1] Ada GPU"); nvl_nl();
    if(!g_nv_ada_on)return nv_fail(1,"Ada support is off - turn it on with: gpu ada on");
    if(!nv_probe())return nv_fail(1,"no NVIDIA GPU found");
    nv_read_id(); const char* chip=nvada_chip(g_nv_boot0); nvl_kv("PMC_BOOT_0",g_nv_boot0);
    if(!chip)return nv_fail(1,"this GPU is not an Ada (AD102/AD103/AD104/AD106/AD107) chip");
    nvl_s("  chip "); nvl_s(chip); nvl_nl();
    nvl_s("[2] GPU firmware (GFW) boot"); nvl_nl();
    if(!nv_gfw_wait())return nv_fail(2,"the GPU's own firmware did not finish booting within 2 s");
    nvl_s("[3] GSP firmware 535.113.01"); nvl_nl();
    if(!ablob(&g_ab_load,"ad102-booter_load-535.113.01.bin",_binary_nvfw_ad102_booter_load_xz_start,_binary_nvfw_ad102_booter_load_xz_end,0x20000))return nv_fail(3,"booter_load for ad102 missing");
    if(!ablob(&g_ab_unload,"ad102-booter_unload-535.113.01.bin",_binary_nvfw_ad102_booter_unload_xz_start,_binary_nvfw_ad102_booter_unload_xz_end,0x20000))return nv_fail(3,"booter_unload for ad102 missing");
    if(!ablob(&g_ab_bl,"ad102-bootloader-535.113.01.bin",_binary_nvfw_ad102_bootloader_xz_start,_binary_nvfw_ad102_bootloader_xz_end,0x20000))return nv_fail(3,"bootloader for ad102 missing");
    if(!ablob(&g_ab_gsp,"ga102-gsp-535.113.01.bin",_binary_nvfw_ga102_gsp_xz_start,_binary_nvfw_ga102_gsp_xz_end,40u<<20))return nv_fail(3,"GSP-RM image missing: put nvidia/ga102/gsp/gsp-535.113.01.bin from linux-firmware into Files as ga102-gsp-535.113.01.bin");
    nvl_kv("booter_load bytes",g_ab_load.n); nvl_kv("bootloader bytes",g_ab_bl.n); nvl_kv("GSP-RM bytes",g_ab_gsp.n);
    u32 ss=0; if(!nvada_elf_has(g_ab_gsp.d,g_ab_gsp.n,H->sig_section,&ss))return nv_fail(3,"GSP-RM image has no .fwsignature_ad10x section");
    nvl_kv("Ada signature bytes",ss);
    if(!nvada_booter_parse(g_ab_load.d,g_ab_load.n,&g_ada_bload)||!nvada_booter_parse(g_ab_unload.d,g_ab_unload.n,&g_ada_bunload))return nv_fail(3,"booter headers damaged");
    int si=H->booter_sig(&g_ada_bload); nvl_kv("booter signature index",(u64)(u32)si);
    if(!nvada_sign(&g_ada_bload,si))return nv_fail(3,"no booter signature matches this GPU's fuses");
    nvl_s("[4] VBIOS FWSEC (v3)"); nvl_nl();
    if(!nvb_read_rom())return nv_fail(4,"VBIOS could not be read");
    const u8* rom=0; u32 rl=0, i0=0, id=0; if(!nvb_rom_get(&rom,&rl,&i0,&id))return nv_fail(4,"VBIOS not available");
    u32 d=bit_falcon_desc(rom,rl,i0,id); if(!d)return nv_fail(4,"FWSEC (falcon ucode 0x85) not found in the VBIOS");
    nvada_fw fs; if(!nvada_fwsec_parse(rom,rl,d,&fs))return nv_fail(4,"FWSEC descriptor is not a valid version 3 descriptor");
    if(!nv_fb_layout(g_ab_gsp.n,g_ab_bl.n))return nv_fail(4,"VRAM size unknown");
    nvl_kv("FRTS at",g_nvl.frts_addr);
    if(!nvada_fwsec_patch(&fs,0x15,g_nvl.frts_addr,g_nvl.frts_size))return nv_fail(4,"FWSEC DMEMMAPPER not found");
    int fi=H->fwsec_sig(&fs); nvl_kv("FWSEC signature index",(u64)(u32)fi);
    if(!nvada_sign(&fs,fi))return nv_fail(4,"no FWSEC signature matches this GPU's fuses");
    nvl_s("[5] FWSEC-FRTS on the GSP falcon"); nvl_nl();
    if(nvr32(0x1FA828)>>4)return nv_fail(5,"WPR2 is already set - another driver started GSP; reboot first");
    if(!H->flcn_reset(H->gsp))return nv_fail(5,"GSP falcon reset did not finish");
    if(!H->fw_load(H->gsp,&fs))return nv_fail(5,"FWSEC DMA into the GSP falcon timed out");
    u32 m0=0,m1=0; int ok=H->fw_boot(H->gsp,&fs,0,0,&m0,&m1);
    u32 err=nvr32(0x1438)>>16, lo=nvr32(0x1FA824), hi=nvr32(0x1FA828);
    nvl_kv("FWSEC mbox0",m0); nvl_kv("FRTS error",err); nvl_kv("WPR2 lo",lo); nvl_kv("WPR2 hi",hi);
    if(!ok)return nv_fail(5,"FWSEC did not halt within 2 s");
    if(m0||err)return nv_fail(5,"FWSEC reported an error");
    if(!(hi>>4)||(lo>>4)!=(u32)(g_nvl.frts_addr>>12))return nv_fail(5,"WPR2 was not set where the FRTS region should be");
    g_ada_state=1;
    nvl_s("[6] booter_load ready for SEC2 (signed); the full start (WPR meta, booter, GSP-RM) is nvgsp all with gpu ada on"); nvl_nl();
    return 1; }

int nvada_booter_run(u64 wpr_meta_pa,int unload){
    const nvada_hooks* H=&g_nvada_hooks; nvada_fw* fw=unload?&g_ada_bunload:&g_ada_bload;
    if(!fw->img)return nv_fail(6,"booter not prepared - run gpu ada boot first");
    if(unload&&!nvada_sign(fw,H->booter_sig(fw)))return nv_fail(6,"no booter_unload signature matches");
    if(!H->flcn_reset(H->sec2))return nv_fail(6,"SEC2 reset did not finish");
    if(!H->fw_load(H->sec2,fw))return nv_fail(6,"booter DMA into SEC2 timed out");
    u32 m0=0,m1=0; int ok=unload?H->fw_boot(H->sec2,fw,0xFFu,0xFFu,&m0,&m1):H->fw_boot(H->sec2,fw,(u32)wpr_meta_pa,(u32)(wpr_meta_pa>>32),&m0,&m1);
    nvl_kv("SEC2 mbox0 after booter",m0); nvl_kv("SEC2 mbox1 after booter",m1);
    if(!ok)return nv_fail(6,"booter did not halt within 2 s");
    if(m0)return nv_fail(6,"booter returned an error code in mailbox 0");
    return 1; }

int nvada_cmd(const char* a){
    while(*a==' ')a++;
    if(a[0]=='o'&&a[1]=='n'&&!a[2]){ g_nv_ada_on=1; return 1; }
    if(a[0]=='o'&&a[1]=='f'&&a[2]=='f'&&!a[3]){ g_nv_ada_on=0; return 1; }
    if(a[0]=='b'&&a[1]=='o'&&a[2]=='o'&&a[3]=='t')return nvada_boot();
    return g_ada_state; }
