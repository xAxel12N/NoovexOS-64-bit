#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"

nvfb_layout g_nvl;

u64 nv_vram_size(void){
    u32 d=nvr32(0x100CE0); u64 lmag=(d&0x3F0u)>>4, lsca=d&0xFu; u64 s=lmag<<(lsca+20); if(d&0x40000000u)s=s/16*15; return s; }
u64 nv_vga_workspace(u64 fb){
    u64 base=fb-0x100000ULL; u64 a=nvr32(0x625F04); if(!(a&8))return base;
    a=(a&0xFFFFFF00ULL)<<8; if(a<base)return fb-0x20000ULL; return a; }
static u64 al_dn(u64 v,u64 a){ return v&~(a-1); }
static u64 al_up(u64 v,u64 a){ return (v+a-1)&~(a-1); }
u64 nv_wpr_heap_size(u64 fb){
    u64 gb=(fb+(1ULL<<30)-1)>>30; u64 h=(8ULL<<20)+al_up((96ULL<<10)*gb,1ULL<<20)+al_up((48ULL<<10)*2048ULL,1ULL<<20); if(h<64)h=64; return h; }
int nv_fb_layout(u64 elf_size,u64 boot_size){
    nvfb_layout* L=&g_nvl; for(u32 i=0;i<sizeof(*L);i++)((u8*)L)[i]=0;
    L->fb_size=nv_vram_size(); if(!L->fb_size||L->fb_size>(64ULL<<30))return 0;
    L->vga_addr=nv_vga_workspace(L->fb_size); L->vga_size=L->fb_size-L->vga_addr;
    L->frts_size=0x100000ULL; L->frts_addr=al_dn(L->vga_addr,0x20000ULL)-L->frts_size;
    L->boot_size=boot_size; L->boot_addr=al_dn(L->frts_addr-boot_size,0x1000ULL);
    L->elf_size=elf_size; L->elf_addr=al_dn(L->boot_addr-elf_size,0x10000ULL);
    L->heap_size=nv_wpr_heap_size(L->fb_size); L->heap_addr=al_dn(L->elf_addr-L->heap_size,0x100000ULL); L->heap_size=al_dn(L->elf_addr-L->heap_addr,0x100000ULL);
    L->wpr2_addr=al_dn(L->heap_addr-256,0x100000ULL); L->wpr2_size=L->frts_addr+L->frts_size-L->wpr2_addr;
    L->nonwpr_size=0x100000ULL; L->nonwpr_addr=L->wpr2_addr-L->nonwpr_size;
    return 1; }

static u32 pr_at(u64 a){ u32 w=(u32)((a&~0xFFFFFULL)>>16); if(nvr32(0x1700)!=w){ nvw32(0x1700,w); (void)nvr32(0x1700); } return 0x700000u+(u32)(a&0xFFFFFu); }
u32 nvp_rd32(u64 a){ return nvr32(pr_at(a)); }
void nvp_wr32(u64 a,u32 v){ nvw32(pr_at(a),v); }
void nvp_wr64(u64 a,u64 v){ nvp_wr32(a,(u32)v); nvp_wr32(a+4,(u32)(v>>32)); }
u64 nvp_rd64(u64 a){ return (u64)nvp_rd32(a)|((u64)nvp_rd32(a+4)<<32); }
void nvp_fill(u64 a,u64 len,u32 v){ for(u64 i=0;i<len;i+=4)nvp_wr32(a+i,v); }

#define NVV_MAXR 256
static struct { u64 a,s; } g_nvvf[NVV_MAXR]; static int g_nvf=0;
static struct { u64 a,s; } g_nvvu[1024]; static int g_nvu=0;
static u64 g_vtot=0;
static void vf_add(u64 a,u64 s){
    if(!s)return; int i=0; while(i<g_nvf&&g_nvvf[i].a<a)i++;
    if(i>0&&g_nvvf[i-1].a+g_nvvf[i-1].s==a){ g_nvvf[i-1].s+=s; if(i<g_nvf&&g_nvvf[i-1].a+g_nvvf[i-1].s==g_nvvf[i].a){ g_nvvf[i-1].s+=g_nvvf[i].s; for(int k=i;k<g_nvf-1;k++)g_nvvf[k]=g_nvvf[k+1]; g_nvf--; } return; }
    if(i<g_nvf&&a+s==g_nvvf[i].a){ g_nvvf[i].a=a; g_nvvf[i].s+=s; return; }
    if(g_nvf>=NVV_MAXR)return; for(int k=g_nvf;k>i;k--)g_nvvf[k]=g_nvvf[k-1]; g_nvvf[i].a=a; g_nvvf[i].s=s; g_nvf++; }
int nvv_init(u64 lo,u64 hi){ g_nvf=0; g_nvu=0; g_vtot=0; if(lo<0x1000)lo=0x1000; if(hi<=lo)return 0; vf_add(lo,hi-lo); g_vtot=hi-lo; return 1; }
void nvv_reserve(u64 lo,u64 hi){
    for(int i=0;i<g_nvf;i++){ u64 a=g_nvvf[i].a, e=a+g_nvvf[i].s; if(hi<=a||lo>=e)continue;
        u64 l2=lo>a?lo:a, h2=hi<e?hi:e; g_vtot-=h2-l2;
        if(l2==a&&h2==e){ for(int k=i;k<g_nvf-1;k++)g_nvvf[k]=g_nvvf[k+1]; g_nvf--; i--; continue; }
        if(l2==a){ g_nvvf[i].a=h2; g_nvvf[i].s=e-h2; continue; }
        if(h2==e){ g_nvvf[i].s=l2-a; continue; }
        g_nvvf[i].s=l2-a; vf_add(h2,e-h2); } }
u64 nvv_alloc(u64 size,u64 align){
    if(!size||g_nvu>=1024)return 0; if(align<0x1000)align=0x1000; size=al_up(size,0x1000);
    for(int i=0;i<g_nvf;i++){ u64 a=al_up(g_nvvf[i].a,align), e=g_nvvf[i].a+g_nvvf[i].s; if(a+size>e)continue;
        u64 fa=g_nvvf[i].a; g_nvvf[i].a=a+size; g_nvvf[i].s=e-(a+size); if(!g_nvvf[i].s){ for(int k=i;k<g_nvf-1;k++)g_nvvf[k]=g_nvvf[k+1]; g_nvf--; }
        if(a>fa)vf_add(fa,a-fa);
        g_nvvu[g_nvu].a=a; g_nvvu[g_nvu].s=size; g_nvu++; nvp_fill(a,size,0); return a; }
    return 0; }
void nvv_free(u64 addr){ for(int i=0;i<g_nvu;i++) if(g_nvvu[i].a==addr){ u64 s=g_nvvu[i].s; g_nvvu[i]=g_nvvu[g_nvu-1]; g_nvu--; vf_add(addr,s); return; } }
u64 nvv_free_bytes(void){ u64 t=0; for(int i=0;i<g_nvf;i++)t+=g_nvvf[i].s; return t; }
u64 nvv_total(void){ return g_vtot; }
int nvv_count(void){ return g_nvu; }

static u64 pde_vram(u64 t){ return (t>>4)|(1ULL<<1); }
static u64 pde_addr(u64 e){ return (e&~0xFFULL)<<4; }
static u64 tbl_get(u64 slot,int dual_small){
    u64 e=nvp_rd64(slot+(dual_small?8:0)); if(((e>>1)&3)==1)return pde_addr(e);
    u64 t=nvv_alloc(0x1000,0x1000); if(!t)return 0; nvp_wr64(slot+(dual_small?8:0),pde_vram(t)); return t; }
int nvm_vmm_new(nvvmm* m,u64 limit){ m->ok=0; m->limit=limit; m->pd3=nvv_alloc(0x1000,0x1000); if(!m->pd3)return 0; m->ok=1; return 1; }
int nvm_map(nvvmm* m,u64 va,u64 pa,u64 len,int sysmem,int priv,int ro){
    for(u64 o=0;o<len;o+=0x1000){ u64 v=va+o;
        u64 pd2=tbl_get(m->pd3+((v>>47)&3)*8,0); if(!pd2)return 0;
        u64 pd1=tbl_get(pd2+((v>>38)&511)*8,0); if(!pd1)return 0;
        u64 pd0=tbl_get(pd1+((v>>29)&511)*8,0); if(!pd0)return 0;
        u64 spt=tbl_get(pd0+((v>>21)&255)*16,1); if(!spt)return 0;
        u64 p=pa+o; u64 pte=(p>>4)|1ULL|((u64)(sysmem?2:0)<<1)|((u64)(sysmem?1:0)<<3)|((u64)(priv?1:0)<<5)|((u64)(ro?1:0)<<6);
        nvp_wr64(spt+((v>>12)&511)*8,pte); }
    return 1; }
u64 nvm_pd3_entry0(nvvmm* m){ return nvp_rd64(m->pd3); }
int nvm_inst_join(nvvmm* m,u64 inst){
    u64 base=m->pd3|(1ULL<<10)|(1ULL<<11);
    nvp_wr64(inst+0x200,base); nvp_wr64(inst+0x208,m->limit-1); nvp_wr32(inst+0x21C,0); nvp_wr32(inst+0x298,1); nvp_wr32(inst+0x29C,0);
    for(int i=0;i<64;i++){ u64 o=inst+0x2A0+(u64)i*16; if(i==0){ nvp_wr32(o,(u32)base); nvp_wr32(o+4,(u32)(base>>32)); } else { nvp_wr32(o,1); nvp_wr32(o+4,1); } nvp_wr32(o+8,0); }
    return 1; }
int nvm_tlb_flush(u64 pdb,int bar){
    nvw32(0xB830A0,(u32)(pdb>>8)); nvw32(0xB830A4,0); nvw32(0xB830B0,0x80000000u|(bar?7u:1u));
    return nv_poll(0xB830B0,0x80000000u,0,2000); }
