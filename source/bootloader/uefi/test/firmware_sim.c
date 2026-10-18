#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
static void map_fbs(void){ mmap((void*)0x4000000000ULL,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); mmap((void*)0x80000000ULL,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); }
#define EFIAPI __attribute__((ms_abi))
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64; typedef u64 UINTN; typedef u64 ST_;
#define E(n) (0x8000000000000000ULL|(n))
typedef struct { u32 a; u16 b,c; u8 d[8]; } GUID;
static int geq(const GUID* g,u32 a){ return g->a==a; }
u8* g_test_phys; static u64 g_jumped; static int g_jumpcount;
void test_jump(u64 e){ g_jumped=e; g_jumpcount++; }
static char g_out[16384]; static int g_on;
typedef struct { u32 Type,Pad; u64 PS,VS,NP,Attr; } MD;
typedef struct { u32 Version,W,H,PF; u32 RM,GM,BM,XM; u32 PPSL; } GINFO;
typedef struct { u32 MaxMode,Mode; GINFO* Info; UINTN SizeOfInfo; u64 FB; UINTN FBS; } GMODE;
struct SC { int iso, fail_exit_first, kernel_missing, only_rgb, hole_and_overlap, no_bootfs, blt_only, bad_kernel; } S;
static GINFO modes[8]; static u64 fbs[8]; static int nmodes; static GMODE gm;
static int g_exits, g_exited, g_watchdog_off, g_setmode=-1; static UINTN g_mapkey=100; static long g_stalled;
static EFIAPI ST_ out_str(void* t,u16* s){ (void)t; while(*s){ if(*s!='\r'&&g_on<16000)g_out[g_on++]=(char)*s; s++; } g_out[g_on]=0; return 0; }
static EFIAPI ST_ out_attr(void* t,UINTN a){ (void)t;(void)a; return 0; }
static EFIAPI ST_ out_clear(void* t){ (void)t; return 0; }
static EFIAPI ST_ in_reset(void* t,u8 x){ (void)t;(void)x; return 0; }
static EFIAPI ST_ in_key(void* t,void* k){ (void)t; memset(k,0,4); return 0; }
static void* conout_tbl[7]={0,(void*)out_str,0,0,0,(void*)out_attr,(void*)out_clear};
static void* conin_tbl[3]={(void*)in_reset,(void*)in_key,0};
static EFIAPI ST_ gop_query(void* g,u32 m,UINTN* sz,GINFO** inf){ (void)g; if(m>=(u32)nmodes)return E(2); *sz=sizeof(GINFO); *inf=&modes[m]; return 0; }
static EFIAPI ST_ gop_set(void* g,u32 m){ (void)g; if(m>=(u32)nmodes)return E(3); gm.Mode=m; gm.Info=&modes[m]; gm.FB=fbs[m]; g_setmode=(int)m; return 0; }
static void* gop_tbl[4]={(void*)gop_query,(void*)gop_set,0,&gm};
struct F { u64 Rev; void* fn[9]; const u8* data; u64 size, pos; };
static struct F root, kfile; static u8* kdata; static u64 ksize;
static int u16eq(const u16* a,const char* b){ while(*a&&*b){ if(*a!=(u16)(u8)*b)return 0; a++; b++; } return !*a&&!*b; }
static EFIAPI ST_ f_open(struct F* d,struct F** out,u16* path,u64 m,u64 at){ (void)d;(void)m;(void)at; if(S.kernel_missing)return E(14);
    if(S.iso?u16eq(path,"\\boot\\kernel64.bin"):u16eq(path,"\\KERNEL64.BIN")){ kfile.data=kdata; kfile.size=ksize; kfile.pos=0; *out=&kfile; return 0; } return E(14); }
static EFIAPI ST_ f_close(struct F* f){ (void)f; return 0; }
static EFIAPI ST_ f_read(struct F* f,UINTN* n,void* buf){ u64 left=f->size-f->pos; if(*n>left)*n=left; memcpy(buf,f->data+f->pos,*n); f->pos+=*n; return 0; }
static EFIAPI ST_ f_info(struct F* f,GUID* g,UINTN* sz,void* buf){ if(!geq(g,0x09576e92))return E(3); if(*sz<80){ *sz=80; return E(5); } memset(buf,0,80); *(u64*)buf=80; *((u64*)buf+1)=f->size; return 0; }
struct SFS { u64 Rev; void* open; } sfs;
static EFIAPI ST_ sfs_open(void* s,struct F** r){ (void)s; *r=&root; return 0; }
struct LI { u32 Rev; void* Parent; void* ST; void* Dev; void* FP; void* Res; u32 LOS; void* LO; void* IB; u64 IS; } li;
static int IMG=1, DEV=2, CONOUTH=3;
static EFIAPI ST_ bs_handleproto(void* h,GUID* g,void** out){
    if(h==&IMG&&geq(g,0x5b1b31a1)){ *out=&li; return 0; }
    if(h==&DEV&&geq(g,0x964e5b22)&&!S.no_bootfs){ *out=&sfs; return 0; }
    if(h==&CONOUTH&&geq(g,0x9042a9de)){ *out=gop_tbl; return 0; }
    *out=0; return E(3); }
static EFIAPI ST_ bs_locate(GUID* g,void* reg,void** out){ (void)reg; if(geq(g,0x964e5b22)){ *out=&sfs; return 0; } if(geq(g,0x9042a9de)){ *out=gop_tbl; return 0; } return E(14); }
static EFIAPI ST_ bs_allocpages(u32 type,u32 mt,UINTN n,u64* a){ (void)mt; if(type!=0)return E(9); void* p=aligned_alloc(4096,n*4096); *a=(u64)p; return p?0:E(9); }
static EFIAPI ST_ bs_allocpool(u32 mt,UINTN n,void** p){ (void)mt; *p=malloc(n); return 0; }
static MD g_map[16]; static int g_nmap;
static EFIAPI ST_ bs_getmap(UINTN* sz,MD* m,UINTN* key,UINTN* dsz,u32* ver){ *dsz=sizeof(MD)+8; *ver=1; UINTN need=(UINTN)g_nmap*(*dsz); if(!m||*sz<need){ *sz=need; return E(5); }
    for(int i=0;i<g_nmap;i++)memcpy((u8*)m+i*(*dsz),&g_map[i],sizeof(MD)); *sz=need; *key=g_mapkey; return 0; }
static EFIAPI ST_ bs_exit(void* img,UINTN key){ g_exits++; if(img!=&IMG)return E(2); if(S.fail_exit_first&&g_exits==1){ g_mapkey++; return E(2); } if(key!=g_mapkey)return E(2); g_exited=1; return 0; }
static EFIAPI ST_ bs_stall(UINTN us){ g_stalled+=(long)us; return 0; }
static EFIAPI ST_ bs_watchdog(UINTN t,u64 c,UINTN n,u16* d){ (void)c;(void)n;(void)d; if(t==0)g_watchdog_off=1; return 0; }
static void* bs[50];
struct CT { GUID g; void* t; } ct[2];
struct SYS { u8 hdr[24]; void* vendor; u32 rev; void* cinh; void* conin; void* couth; void* conout; void* eh; void* err; void* rt; void* bs; UINTN n; void* ct; } sys;
typedef ST_ (EFIAPI *efimain_t)(void*,void*);
ST_ EFIAPI efi_main(void*,void*);
static void addmode(u32 w,u32 h,u32 pf,u64 fb){ GINFO* m=&modes[nmodes]; memset(m,0,sizeof *m); m->W=w; m->H=h; m->PF=pf; m->PPSL=w+(w==1366?2:0); fbs[nmodes]=fb; nmodes++; }
static void setup(void){
    memset(g_test_phys,0xAA,0x1400000); g_on=0; g_out[0]=0; g_jumped=0; g_jumpcount=0; g_exits=0; g_exited=0; g_watchdog_off=0; g_setmode=-1; g_mapkey=100; g_stalled=0;
    nmodes=0;
    if(S.blt_only){ addmode(1024,768,3,0); }
    else if(S.only_rgb){ addmode(800,600,0,0x80000000); addmode(1280,1024,0,0x80000000); }
    else { addmode(800,600,1,0x80000000); addmode(1024,768,1,0x80000000); addmode(1920,1080,1,0x4000000000ULL); addmode(2560,1440,1,0x80000000); addmode(1920,1080,0,0x80000000); addmode(1366,768,1,0x80000000); }
    gm.MaxMode=(u32)nmodes; gm.Mode=0; gm.Info=&modes[0]; gm.SizeOfInfo=sizeof(GINFO); gm.FB=fbs[0];
    root.Rev=0x10000; root.fn[0]=(void*)f_open; root.fn[1]=(void*)f_close; root.fn[3]=(void*)f_read; root.fn[7]=(void*)f_info;
    kfile=root; sfs.Rev=0x10000; sfs.open=(void*)sfs_open;
    memset(&li,0,sizeof li); li.Dev=&DEV; li.IB=(void*)0x7E000000; li.IS=0x8000;
    memset(bs,0,sizeof bs);
    bs[2]=(void*)bs_allocpages; bs[4]=(void*)bs_getmap; bs[5]=(void*)bs_allocpool; bs[16]=(void*)bs_handleproto; bs[26]=(void*)bs_exit; bs[28]=(void*)bs_stall; bs[29]=(void*)bs_watchdog; bs[37]=(void*)bs_locate;
    ct[0].g.a=0x12345678; ct[0].t=(void*)0x1111; ct[1].g=(GUID){0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}}; ct[1].t=(void*)0x7FE14000;
    memset(&sys,0,sizeof sys); sys.conin=conin_tbl; sys.couth=&CONOUTH; sys.conout=conout_tbl; sys.bs=(u8*)bs-24; sys.n=2; sys.ct=ct;
    g_nmap=0;
    #define MAP(t,s,e) do{ g_map[g_nmap].Type=t; g_map[g_nmap].PS=s; g_map[g_nmap].NP=((e)-(s))/4096; g_nmap++; }while(0)
    MAP(7,0x0,0x9F000); MAP(0,0x9F000,0x100000);
    if(S.hole_and_overlap){ MAP(7,0x100000,0xC00000); MAP(6,0xC00000,0xC10000); MAP(4,0xC10000,0x40000000); MAP(0,0x40000000,0x40100000); MAP(7,0x40100000,0x7F000000); }
    else { MAP(7,0x100000,0x800000); MAP(2,0x800000,0x900000); MAP(4,0x900000,0x7EF00000); MAP(1,0x7EF00000,0x7F000000); MAP(9,0x7F000000,0x7F100000); MAP(10,0x7F100000,0x7F200000); }
}
static int ok=1; static void chk(const char* w,int c){ printf("    %-66s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
#include <ucontext.h>
static ucontext_t g_mainctx, g_fwctx; static ST_ g_ret; static u8* g_lowstack;
static void fw_trampoline(void){ g_ret=((efimain_t)efi_main)(&IMG,&sys); }
static ST_ run(void){ setup();
    if(!g_lowstack){ g_lowstack=mmap((void*)0x30000000ULL,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); }
    getcontext(&g_fwctx); g_fwctx.uc_stack.ss_sp=g_lowstack; g_fwctx.uc_stack.ss_size=0x100000; g_fwctx.uc_link=&g_mainctx;
    makecontext(&g_fwctx,fw_trampoline,0); g_ret=0; swapcontext(&g_mainctx,&g_fwctx); return g_ret; }
int main(void){
    FILE* f=fopen("kernel64.bin","rb"); fseek(f,0,SEEK_END); ksize=(u64)ftell(f); fseek(f,0,SEEK_SET); kdata=malloc(ksize); fread(kdata,1,ksize,f); fclose(f);
    g_test_phys=malloc(0x1400000); map_fbs();
    printf("== 1. ISO / USB layout (kernel in \\boot), several screen modes ==\n"); memset(&S,0,sizeof S); S.iso=1;
    ST_ r=run(); u8* P=g_test_phys;
    chk("returned to the kernel jump (no error)", r==0&&g_jumpcount==1);
    chk("kernel found at \\boot\\kernel64.bin", strstr(g_out,"\\boot\\kernel64.bin")!=0);
    chk("kernel copied to physical 1 MB byte-exact", memcmp(P+0x100000,kdata,ksize)==0);
    chk("jump target = kernel's uefi_entry (0x100206)", g_jumped==0x100206);
    chk("watchdog disabled", g_watchdog_off);
    chk("ExitBootServices succeeded before the jump", g_exited==1);
    chk("picked 1920x1080 BGR (not 2560x1440, not the RGB 1920x1080)", g_setmode==2);
    chk("0x5000 block: framebuffer above 4 GB passed as 64-bit", *(u64*)(P+0x5000)==0x4000000000ULL);
    chk("0x5000 block: pitch/width/height/bpp", *(u32*)(P+0x5008)==1920*4&&*(u16*)(P+0x500C)==1920&&*(u16*)(P+0x500E)==1080&&P[0x5020]==32);
    chk("0x5000 block: BGR flag 0, RAM size + UEFI magic", P[0x5021]==0&&*(u32*)(P+0x502C)==0x49464555u);
    chk("RAM = contiguous usable memory from 1 MB (2032 MB, stops at loader-code end)", *(u32*)(P+0x5028)==2032);
    chk("0x6000 block: ACPI RSDP + tag", *(u64*)(P+0x6000+120)==0x7FE14000&&*(u32*)(P+0x6000+128)==0x50445352u);
    { extern u64 g_pt; u64* t=(u64*)g_pt; int idm=t&&t[0]==((u64)(t+512)|3); u64* pdpt=t+512; for(int i=0;idm&&i<32;i++){ u64* pd=(u64*)(pdpt[i]&~0xFFFULL); if(!(pdpt[i]&1)){ idm=0; break; } for(int j=0;j<512;j++) if(pd[j]!=((((u64)i<<30)+((u64)j<<21))|0x83)){ idm=0; break; } }
      chk("loader page tables: identity map 0-64 GB, 2 MB pages, no NX bit", idm);
      chk("loader page tables lie outside every kernel memory area", g_pt>=0x11000000||g_pt+36*4096<=0x5000); }
    { u32* row=(u32*)(0x4000000000ULL+(u64)(1080-36)*1920*4); chk("progress markers 1 (red) and 2 (orange) drawn", row[16]==0x00E03030u&&row[16+28]==0x00FF9020u); }
    printf("== 2. installed disk layout (\\KERNEL64.BIN at root) ==\n"); memset(&S,0,sizeof S);
    r=run(); chk("boots from \\KERNEL64.BIN", r==0&&g_jumpcount==1&&strstr(g_out,"\\KERNEL64.BIN"));
    printf("== 3. ExitBootServices fails once (memory map changed) ==\n"); memset(&S,0,sizeof S); S.fail_exit_first=1;
    r=run(); chk("retried with a fresh map and succeeded", r==0&&g_exits==2&&g_exited&&g_jumpcount==1);
    printf("== 4. kernel file missing ==\n"); memset(&S,0,sizeof S); S.kernel_missing=1;
    r=run(); chk("returns EFI_NOT_FOUND to the firmware (no hang)", r==E(14));
    chk("clear message, no ExitBootServices, no jump", strstr(g_out,"kernel not found")&&g_exits==0&&g_jumpcount==0);
    printf("== 5. only RGB-ordered modes ==\n"); memset(&S,0,sizeof S); S.only_rgb=1;
    r=run(); chk("uses 1280x1024 and sets the RGB flag for the kernel", r==0&&g_setmode==1&&P[0x5021]==1);
    printf("== 6. reserved hole + firmware data overlapping kernel memory ==\n"); memset(&S,0,sizeof S); S.hole_and_overlap=1;
    r=run(); chk("warns about runtime data inside the kernel area", strstr(g_out,"warning: firmware runtime data memory at 0xC00000")!=0);
    chk("waits 5 s so the warning can be read, then boots", g_stalled>=5000000&&g_jumpcount==1);
    chk("RAM stops at the reserved hole (1024 MB)", *(u32*)(P+0x5028)==1024);
    printf("== 7. boot device has no file system ==\n"); memset(&S,0,sizeof S); S.no_bootfs=1;
    r=run(); chk("falls back to searching other disks and boots", r==0&&g_jumpcount==1&&strstr(g_out,"searching other disks"));
    printf("== 8. GOP without a linear framebuffer ==\n"); memset(&S,0,sizeof S); S.blt_only=1;
    r=run(); chk("refuses cleanly with a message", r==E(3)&&strstr(g_out,"no linear framebuffer")&&g_exits==0);
    printf("== 9. a file that is not a NoovexOS kernel ==\n"); memset(&S,0,sizeof S); u8* save=kdata; u64 ss=ksize; static u8 junk[4096]; kdata=junk; ksize=sizeof junk;
    r=run(); chk("rejected before taking over the machine", r==E(1)&&strstr(g_out,"not a NoovexOS 64 kernel")&&g_exits==0); kdata=save; ksize=ss;
    printf("%s\n",ok?"ALL PASS":"FAILURES");
    if(!ok){ printf("--- loader output of last run ---\n%s\n",g_out); }
    return !ok; }
