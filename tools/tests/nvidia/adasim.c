#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>

#include "include/nvx/base.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/nvidia/nvada.h"

static u32* R; static u32 boot0=0x192000A1u;
static u8 fimem[2][0x40000], fdmem[2][0x40000];
static u64 wpr_lo=0, wpr_hi=0; static u32 fuse_c0=0, fuse_100=0, fuse_140=0; static int fwsec_runs=0, booter_runs=0, bad_sig=0, dma_xfers=0;
static const u8* exp_sig[2]; static u32 exp_sig_len[2], exp_sign_off[2]; static u32 exp_mb[2];
static int fi(u32 f){ return f==NVF_SEC2?1:0; }
nvfb_layout g_nvl; char g_nv_log[65536]; int g_nv_log_n=0; int g_nv_step=0; char g_nv_why[160];
u32 g_nv_boot0=0; FsFile FS[FS_N];
static int fails=0; static void chk(const char* w,int c){ printf("  %-78s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }

static void run_fw(u32 f){
    int k=fi(f); u32 a2=f+NVADA_ADDR2;
    u32 so=R[(a2+0x210)/4];
    if(exp_sig[k]&&memcmp(fdmem[k]+exp_sign_off[k],exp_sig[k],exp_sig_len[k]))bad_sig++;
    (void)so;
    if(!k){ fwsec_runs++; u8* dm=fdmem[0]; u32 mo=0; for(u32 i=0;i+8<0x400;i+=4) if(*(u32*)(dm+i)==0x4D4D4150u){ mo=i; break; }
        u32 cin=*(u32*)(dm+mo+8), cmd=*(u32*)(dm+mo+0x2C); u32* c=(u32*)(dm+cin);
        if(cmd==0x15&&c[6]==1&&c[8]){ wpr_lo=(u64)c[8]<<4; wpr_hi=((u64)(c[8]+c[9]))<<4; R[0x40/4+f/4]=0; } else R[(f+0x40)/4]=0x55; }
    else { booter_runs++; R[(f+0x40)/4]=(R[(f+0x40)/4]==exp_mb[1])?0:0xBAD; }
    R[(f+0x100)/4]|=0x10u; }

u32 nvr32(u32 o){
    if(o==0)return boot0; if(o==0x118128)return 1; if(o==0x118234)return 0xFF;
    if(o==0x1FA824)return (u32)wpr_lo; if(o==0x1FA828)return (u32)wpr_hi; if(o==0x1438)return 0;
    if(o>=0x8241C0&&o<0x824200)return fuse_c0; if(o>=0x824100&&o<0x824140)return fuse_100; if(o>=0x824140&&o<0x824180)return fuse_140;
    if((o&0xFFF)==0x0F4&&(o>>12==0x110||o>>12==0x840))return 0x80000000u;
    if((o&0xFFF)==0x118)return R[o/4]|2u;
    return R[o/4]; }
void nvw32(u32 o,u32 v){ R[o/4]=v;
    u32 f=o&~0xFFFu;
    if((f==NVF_GSP||f==NVF_SEC2)&&(o&0xFFF)==0x118){ int k=fi(f); u64 pa=((u64)R[(f+0x110)/4]<<8)+R[(f+0x11C)/4]; u32 dst=R[(f+0x114)/4];
        u8* m=(v&0x10u)?fimem[k]:fdmem[k]; if(dst+256<=0x40000)memcpy(m+dst,(void*)(uintptr_t)pa,256); dma_xfers++; }
    if((f==NVF_GSP||f==NVF_SEC2)&&(o&0xFFF)==0x100&&(v&2u)){ R[o/4]&=~0x10u; run_fw(f); } }
void nvm32(u32 o,u32 m,u32 v){ nvw32(o,(nvr32(o)&~m)|v); }
u64 hr_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec; }
void hr_delay_us(u32 us){ (void)us; }
int nv_poll(u32 off,u32 mask,u32 val,u32 ms){ u64 t0=hr_ns(); for(;;){ if((nvr32(off)&mask)==val)return 1; if(hr_ns()-t0>(u64)ms*1000000ULL)return 0; } }
void nvl_s(const char* s){ while(*s&&g_nv_log_n<65000)g_nv_log[g_nv_log_n++]=*s++; g_nv_log[g_nv_log_n]=0; }
void nvl_x(u64 v){ char b[24]; snprintf(b,24,"0x%llx",(unsigned long long)v); nvl_s(b); }
void nvl_d(u64 v){ char b[24]; snprintf(b,24,"%llu",(unsigned long long)v); nvl_s(b); }
void nvl_nl(void){ nvl_s("\n"); }
void nvl_kv(const char* k,u64 v){ nvl_s("  "); nvl_s(k); nvl_s(" = "); nvl_x(v); nvl_nl(); }
void nvl_reset(void){ g_nv_log_n=0; g_nv_log[0]=0; g_nv_step=0; g_nv_why[0]=0; }
int nv_fail(int step,const char* why){ g_nv_step=step; snprintf(g_nv_why,160,"%s",why); nvl_s("FAILED: "); nvl_s(why); nvl_nl(); return -step; }
int nv_probe(void){ return 1; } void nv_read_id(void){ g_nv_boot0=boot0; }
int nv_gfw_wait(void){ return 1; }
int nv_fb_layout(u64 e,u64 b){ (void)e; (void)b; memset(&g_nvl,0,sizeof(g_nvl)); g_nvl.fb_size=12ULL<<30; g_nvl.frts_size=0x100000; g_nvl.frts_addr=g_nvl.fb_size-0x300000; return 1; }
static u8 rom[0x8000]; static u32 rom_len=0;
int nvb_read_rom(void){ return rom_len>0; }
int nvb_rom_get(const u8** r,u32* n,u32* i0,u32* id){ *r=rom; *n=rom_len; *i0=rom_len; *id=0; return 1; }
static u8* arena=0; static unsigned long arena_n=0;
void* kmalloc(unsigned long n){ if(!arena){ arena=mmap((void*)0x40000000ULL,1UL<<28,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if(arena==MAP_FAILED)return 0; } n=(n+63)&~63UL; if(arena_n+n>(1UL<<28))return 0; void* p=arena+arena_n; arena_n+=n; return p; }
void kfree(void* p){ (void)p; }
u64 kva_to_pa(u64 va){ return va; }
long xzk_decode_into(const u8* in,long n,u8* out,long cap){ (void)in; (void)n; (void)out; (void)cap; return -1; }
static int nfs=0;
int fs_find_name(const char* nm){ for(int i=0;i<nfs;i++) if(!strcmp(FS[i].name,nm))return i; return -1; }
char* fsd(int k){ return FS[k].data; }
static int fs_add(const char* nm,u8* d,long n){ if(nfs>=FS_N||!d)return 0; snprintf(FS[nfs].name,64,"%s",nm); FS[nfs].data=(char*)d; FS[nfs].len=(int)n; nfs++; return 1; }
static u8* slurp(const char* dir,const char* f,long* n){ char p[512]; snprintf(p,512,"%s/%s",dir,f); FILE* q=fopen(p,"rb"); if(!q)return 0; fseek(q,0,SEEK_END); *n=ftell(q); fseek(q,0,SEEK_SET); u8* d=malloc(*n+16); if(fread(d,1,*n,q)!=(size_t)*n){ fclose(q); free(d); return 0; } fclose(q); return d; }

const u8 _binary_nvfw_ad102_booter_load_xz_start[1], _binary_nvfw_ad102_booter_load_xz_end[1], _binary_nvfw_ad102_booter_unload_xz_start[1], _binary_nvfw_ad102_booter_unload_xz_end[1], _binary_nvfw_ad102_bootloader_xz_start[1], _binary_nvfw_ad102_bootloader_xz_end[1];
#include "drivers/video/nvidia/nvada.c"

static u8 fake_elf[4096];
static void mk_elf(void){
    u8* f=fake_elf; memset(f,0,sizeof(fake_elf)); f[0]=0x7F; f[1]='E'; f[2]='L'; f[3]='F'; f[4]=2;
    const char st[]="\0.shstrtab\0.fwsignature_ad10x\0"; memcpy(f+0x200,st,sizeof(st));
    *(u64*)(f+0x28)=0x400; *(u16*)(f+0x3C)=3; *(u16*)(f+0x3E)=1;
    u8* s1=f+0x400+64; *(u32*)s1=1; *(u64*)(s1+0x18)=0x200; *(u64*)(s1+0x20)=sizeof(st);
    u8* s2=f+0x400+128; *(u32*)s2=11; *(u64*)(s2+0x18)=0x300; *(u64*)(s2+0x20)=0x1000-0x300-0x100; }
static u32 mk_rom(u32 cnt,u16 sv){
    memset(rom,0,sizeof(rom)); u32 bit=0x100; rom[0]=0x55; rom[1]=0xAA;
    rom[bit]=0xFF; rom[bit+1]=0xB8; rom[bit+2]='B'; rom[bit+3]='I'; rom[bit+4]='T'; rom[bit+9]=6; rom[bit+10]=1;
    u32 e=bit+12; rom[e]='p'; rom[e+1]=2; rom[e+2]=4; rom[e+4]=0x40; rom[e+5]=0x01;
    *(u32*)(rom+0x140)=0x180; u32 t=0x180; rom[t]=1; rom[t+1]=4; rom[t+2]=6; rom[t+3]=1; rom[t+4]=0x85; *(u32*)(rom+t+6)=0x200;
    u32 d=0x200, sz=0x2C+cnt*384; *(u32*)(rom+d)=1u|(3u<<8)|(sz<<16);
    u32 il=0x200, dl=0x400; *(u32*)(rom+d+8)=0x200; *(u32*)(rom+d+12)=0x40; *(u32*)(rom+d+16)=0; *(u32*)(rom+d+20)=il; *(u32*)(rom+d+28)=0; *(u32*)(rom+d+32)=dl;
    *(u16*)(rom+d+36)=0x400; rom[d+38]=7; rom[d+39]=(u8)cnt; *(u16*)(rom+d+40)=sv;
    for(u32 i=0;i<cnt;i++) memset(rom+d+0x2C+i*384,0xA0+i,384);
    u8* im=rom+d+sz; for(u32 i=0;i<il;i++)im[i]=(u8)(i*13+1);
    u8* dm=im+il; dm[0x40]=1; dm[0x41]=4; dm[0x42]=8; dm[0x43]=1; *(u32*)(dm+0x44)=4; *(u32*)(dm+0x48)=0x80;
    *(u32*)(dm+0x80)=0x4D4D4150u; *(u32*)(dm+0x88)=0x100;
    rom_len=d+sz+il+dl+0x100; return d; }

#include "include/nvx/drivers/video/nvidia/nvgsp_gen.h"
int nvf_boot(u32 f,u32 bv,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms){ nvw32(f+0x040,mb0); nvw32(f+0x044,mb1); nvw32(f+0x104,bv); nvw32(f+0x100,2u); int ok=nv_poll(f+0x100,0x10u,0x10u,ms); if(o0)*o0=nvr32(f+0x040); if(o1)*o1=nvr32(f+0x044); return ok; }
static void gen_checks(void){
    const nvgsp_gen* A=&nvgsp_gen_ad10x; const nvgsp_cls* C=A->cls;
    chk("nvgen table: AD102/AD103/AD104/AD106/AD107 map to the Ada entry, 0x195 does not",nvgsp_gen_for(0x192)==A&&nvgsp_gen_for(0x193)==A&&nvgsp_gen_for(0x194)==A&&nvgsp_gen_for(0x196)==A&&nvgsp_gen_for(0x197)==A&&!nvgsp_gen_for(0x195));
    chk("nvgen table: Turing and Ampere entries unchanged",nvgsp_gen_for(0x168)==&nvgsp_gen_tu11x&&nvgsp_gen_for(0x172)==&nvgsp_gen_ga10x&&!nvgsp_gen_for(0x175)&&!nvgsp_gen_for(0x198));
    g_nv_ada_on=0; int off=!nvgsp_gen_pci_ok(0x2684)&&!nvgsp_gen_pci_ok(0x28E0);
    g_nv_ada_on=1; int on=nvgsp_gen_pci_ok(0x2684)&&nvgsp_gen_pci_ok(0x28E0)&&!nvgsp_gen_pci_ok(0x2600); g_nv_ada_on=0;
    chk("RTX 40 PCI ids 2680-28FF are claimed only with gpu ada on; Turing ids always",off&&on&&nvgsp_gen_pci_ok(0x2184)&&nvgsp_gen_pci_ok(0x1E07));
    chk("Ada entry: booters from ad102, GSP-RM image shared with ga102 (linux-firmware ad10x/gsp links), .fwsignature_ad10x",!strcmp(A->booter_load,"ad102-booter_load-535.113.01.bin")&&!strcmp(A->booter_unload,"ad102-booter_unload-535.113.01.bin")&&!strcmp(A->bootloader,"ad102-bootloader-535.113.01.bin")&&!strcmp(A->gsp,nvgsp_gen_ga10x.gsp)&&!strcmp(A->sig_section,".fwsignature_ad10x")&&A->gsp_cap==nvgsp_gen_ga10x.gsp_cap);
    chk("Ada entry: WPR heap as GA10x, every falcon hook set, built-in ad102 booters wired",A->heap_os==nvgsp_gen_ga10x.heap_os&&A->heap_min==nvgsp_gen_ga10x.heap_min&&A->flcn_reset&&A->flcn_load&&A->flcn_boot&&A->booter_sig&&A->fwsec_sig&&A->gsp_reset&&A->riscv_active&&A->blob[0]==_binary_nvfw_ad102_booter_load_xz_start&&A->blob[2]==_binary_nvfw_ad102_booter_unload_xz_start&&A->blob[4]==_binary_nvfw_ad102_bootloader_xz_start);
    chk("Ada classes (nouveau rm/ad10x.c): GPFIFO c56f, usermode c561, CE c7b5, 2D 902d, ADA_A c997, ADA_COMPUTE_A c9c0",C&&C->chan==0xC56F&&C->user==0xC561&&C->ce==0xC7B5&&C->twod==0x902D&&C->threed==0xC997&&C->compute==0xC9C0&&C->sm==89);
    chk("Ada display classes: AD102_DISP c770, core c77d, window c67e, wimm c67b, cursor c67a, doorbell as Turing/Ampere",C->disp==0xC770&&C->core==0xC77D&&C->wndw==0xC67E&&C->wimm==0xC67B&&C->curs==0xC67A&&C->doorbell==0xBB0090);
    u8* img=(u8*)(((uintptr_t)kmalloc(0x1100)+255)&~(uintptr_t)255); for(int i=0;i<0x1000;i++)img[i]=(u8)(i*29+5);
    nvgsp_ldr l; memset(&l,0,sizeof l); l.img=img; l.pa=(u64)(uintptr_t)img; l.imem_off=0x100; l.imem_size=0x300; l.dmem_off=0x400; l.dmem_base=0; l.dmem_size=0x200; l.dmem_sign=0x40; l.engine_id=0x400; l.ucode_id=7; l.bootvec=0x100;
    memset(fimem,0,sizeof fimem); memset(fdmem,0,sizeof fdmem); int a=A->flcn_load(NVF_GSP,&l); u8 ai[0x400], ad[0x200]; memcpy(ai,fimem[0],0x400); memcpy(ad,fdmem[0],0x200);
    memset(fimem,0,sizeof fimem); memset(fdmem,0,sizeof fdmem); int g=nvgsp_gen_ga102_load(NVF_GSP,&l);
    chk("Ada falcon DMA load matches the shared GA102 loader byte for byte (nouveau ad102 uses ga102_gsp_flcn)",a&&g&&!memcmp(ai,fimem[0],0x400)&&!memcmp(ad,fdmem[0],0x200)&&!memcmp(fimem[0],img+0x100,0x300)&&!memcmp(fdmem[0],img+0x400,0x200));
    fuse_c0=0x2; int s1=A->fwsec_sig(3,&l), s2=nvgsp_gen_ga102_fwsec_sig(3,&l); l.fuse_ver=7; s1=A->fwsec_sig(3,&l); s2=nvgsp_gen_ga102_fwsec_sig(3,&l); fuse_c0=0;
    chk("Ada FWSEC signature choice equals the shared GA102 rule",s1==s2&&s1==2); }

int main(int argc,char** argv){
    R=calloc(0x1000000/4*4,1); if(!R)return 1;
    const char* dir=argc>1?argv[1]:0;
    printf("Ada GSP boot (nvada.c) against a simulated AD102\n");
    chk("AD102..AD107 recognised, TU116/GA102 not",nvada_chip(0x192000A1u)&&nvada_chip(0x197000A1u)&&!strcmp(nvada_chip(0x194000A1u),"AD104")&&!nvada_chip(0x168000A1u)&&!nvada_chip(0x172000A1u)&&!nvada_chip(0x195000A1u));
    gen_checks();
    chk("off by default: boot refused at step 1 until gpu ada on",g_nv_ada_on==0&&nvada_boot()==-1&&strstr(g_nv_why,"gpu ada on"));
    nvada_cmd("on"); chk("gpu ada on turns it on",g_nv_ada_on==1);
    boot0=0x168000A1u; chk("a Turing TU116 is left alone",nvada_boot()==-1&&strstr(g_nv_why,"not an Ada")); boot0=0x192000A1u;
    long nl=0,nu=0,nb=0; u8* bl=dir?slurp(dir,"booter_load-535.113.01.bin",&nl):0; u8* bu=dir?slurp(dir,"booter_unload-535.113.01.bin",&nu):0; u8* bb=dir?slurp(dir,"bootloader-535.113.01.bin",&nb):0;
    if(!bl||!bu||!bb){ printf("  real ad102 firmware skipped (no directory given)\n"); printf(fails?"SOME FAILED\n":"ALL PASS\n"); return fails?1:0; }
    nvada_fw f; chk("real ad102 booter_load parses (HS v2 header, GA102 layout)",nvada_booter_parse(bl,(u32)nl,&f)&&f.imem_size&&f.dmem_size&&f.sig_cnt>=1&&f.sig_size>=96&&f.boot_addr==f.imem_off);
    printf("    imem %x+%x dmem %x+%x sign@%x fuse %u engine %x ucode %u sigs %u x %u\n",f.imem_off,f.imem_size,f.dmem_off,f.dmem_size,f.dmem_sign,f.fuse_ver,f.engine_id,f.ucode_id,f.sig_cnt,f.sig_size);
    chk("booter_load targets SEC2 (engine mask bit 0) with a ucode id",(f.engine_id&1u)&&f.ucode_id>=1);
    nvada_fw u; chk("real ad102 booter_unload parses",nvada_booter_parse(bu,(u32)nu,&u)&&u.imem_size&&u.sig_cnt>=1);
    fuse_140=0; chk("no fuse bits: last signature is picked",ga100_sig(&f)==(int)f.sig_cnt-1);
    fuse_140=1; chk("fuse version 1: signature fuse_ver-1",ga100_sig(&f)==(int)f.fuse_ver-1||((int)f.fuse_ver<1&&ga100_sig(&f)==-1));
    fuse_140=0xFFFFFFFFu; chk("fuse newer than the booter: refused",f.fuse_ver>=32||ga100_sig(&f)==-1); fuse_140=0;
    nvada_fw x; u32 bad=(u32)nl; u8* cut=malloc(bad); memcpy(cut,bl,bad); *(u32*)(cut+0x14)=bad*2; chk("booter with a data size past the file end is refused",!nvada_booter_parse(cut,bad,&x));
    *(u32*)(cut+0x14)=*(u32*)(bl+0x14); *(u32*)cut=0x10DF; chk("booter without the 0x10DE magic is refused",!nvada_booter_parse(cut,bad,&x));
    chk("truncated booter is refused",!nvada_booter_parse(bl,0x80,&x));
    mk_elf(); u32 ss=0; chk("ELF .fwsignature_ad10x lookup",nvada_elf_has(fake_elf,sizeof(fake_elf),".fwsignature_ad10x",&ss)&&ss==0xC00&&!nvada_elf_has(fake_elf,sizeof(fake_elf),".fwsignature_ga10x",0));
    u32 d=mk_rom(3,0x7); nvada_fw w; chk("FWSEC v3 descriptor parses from a VBIOS",nvada_fwsec_parse(rom,rom_len,d,&w)&&w.imem_size==0x200&&w.dmem_size==0x400&&w.sig_cnt==3&&w.engine_id==0x400&&w.ucode_id==7);
    chk("BIT 'p' table walk finds FWSEC (app 0x85)",bit_falcon_desc(rom,rom_len,rom_len,0)==d);
    fuse_c0=0x1; chk("FWSEC fuse 1 with versions 0x7: signature 1",ga102_fwsec_sig(&w)==1);
    fuse_c0=0x2; chk("FWSEC fuse 2 with versions 0x7: signature 2",ga102_fwsec_sig(&w)==2);
    fuse_c0=0x8; chk("FWSEC fuse newer than every signature: refused",ga102_fwsec_sig(&w)==-1);
    fuse_c0=0x0; chk("FWSEC unfused: signature 0",ga102_fwsec_sig(&w)==0);
    chk("FWSEC DMEMMAPPER patched with FRTS command 0x15",nvada_fwsec_patch(&w,0x15,0x2FFD00000ULL,0x100000)&&*(u32*)(w.img+w.dmem_off+0x80+0x2C)==0x15&&*(u32*)(w.img+w.dmem_off+0x100+0x20)==(u32)(0x2FFD00000ULL>>12));
    u8 big[0x8000]; memcpy(big,rom,rom_len); rom[d+39]=60; chk("FWSEC descriptor with too many signatures is refused",!nvada_fwsec_parse(rom,rom_len,d,&x)); memcpy(rom,big,rom_len);
    fs_add("ad102-booter_load-535.113.01.bin",bl,nl); fs_add("ad102-booter_unload-535.113.01.bin",bu,nu); fs_add("ad102-bootloader-535.113.01.bin",bb,nb);
    chk("no GSP-RM image: clear message naming the file",nvada_boot()==-3&&strstr(g_nv_why,"ga102-gsp-535.113.01.bin"));
    fs_add("ga102-gsp-535.113.01.bin",fake_elf,sizeof(fake_elf));
    fuse_c0=0x2; fuse_140=0;
    nvada_fw es; mk_rom(3,0x7); nvada_fwsec_parse(rom,rom_len,d,&es); exp_sig[0]=es.sigs+2*384; exp_sig_len[0]=384; exp_sign_off[0]=es.dmem_sign;
    int r=nvada_boot();
    if(r<=0)printf("%s",g_nv_log);
    chk("boot runs steps 1-5: chip, GFW, firmware, VBIOS FWSEC v3, FWSEC-FRTS",r==1);
    chk("FWSEC ran once on the GSP falcon and set WPR2 at the FRTS region",fwsec_runs==1&&(wpr_lo>>4)==(u32)(g_nvl.frts_addr>>12)&&wpr_hi);
    chk("FWSEC IMEM arrived by DMA byte for byte",!memcmp(fimem[0],es.img,0x200)&&dma_xfers>=6);
    chk("FWSEC DMEM carries the fuse-selected signature at PKCDataOffset",bad_sig==0&&!memcmp(fdmem[0]+es.dmem_sign,es.sigs+2*384,384));
    chk("GA102 boot registers: dmem sign, engine id, ucode id",R[(NVF_GSP+0x1210)/4]==es.dmem_sign&&R[(NVF_GSP+0x119C)/4]==0x400&&R[(NVF_GSP+0x1198)/4]==7);
    chk("second boot is refused while WPR2 is set",nvada_boot()==-5&&strstr(g_nv_why,"WPR2"));
    exp_mb[1]=0x12345000u; exp_sig[1]=0;
    int br=nvada_booter_run(0x12345000ULL,0);
    chk("booter_load on SEC2: DMA, WPR meta address in mailboxes, clean halt",br==1&&booter_runs==1&&!memcmp(fimem[1],g_ada_bload.img+g_ada_bload.imem_off,g_ada_bload.imem_size));
    chk("booter_load DMEM holds its signature at the patch location",!memcmp(fdmem[1]+g_ada_bload.dmem_sign,g_ada_bload.sigs+(u32)ga100_sig(&g_ada_bload)*g_ada_bload.sig_size,g_ada_bload.sig_size));
    exp_mb[1]=0xFF; chk("booter_unload on SEC2",nvada_booter_run(0,1)==1&&booter_runs==2);
    nvada_cmd("off"); chk("gpu ada off",g_nv_ada_on==0&&nvada_boot()==-1);
    printf(fails?"SOME FAILED\n":"ALL PASS\n"); return fails?1:0; }
