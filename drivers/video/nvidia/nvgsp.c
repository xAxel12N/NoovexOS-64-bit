#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"

#ifndef NV_BOOTINFO
#define NV_BOOTINFO 0x5000ULL
#endif
extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern u64 acpi_hi_map(u64 pa);
extern const u8 _binary_nvfw_gsp_xz_start[], _binary_nvfw_gsp_xz_end[];
extern const u8 _binary_nvfw_bootloader_xz_start[], _binary_nvfw_bootloader_xz_end[];
extern const u8 _binary_nvfw_booter_load_xz_start[], _binary_nvfw_booter_load_xz_end[];
extern const u8 _binary_nvfw_booter_unload_xz_start[], _binary_nvfw_booter_unload_xz_end[];
extern const u8 _binary_nvfw_acr_bl_xz_start[], _binary_nvfw_acr_bl_xz_end[];

typedef struct { u8* d; u32 n; const char* src; } nvblob;
static nvblob g_fw_gsp, g_fw_bl, g_fw_bload, g_fw_bunload, g_fw_acr;

static struct {
    int state; u32 chip;
    u8* elf; u64 elf_size; u8* sig; u8* boot; u32 app_ver;
    u64* r3l0; u64* r3l1; u64* r3l2; u32 r3l2_pages;
    u8* log[3]; u8* rmargs; u8* libos; u8* shm; u8* meta; u8* flush;
    u32 cmd_seq, rpc_seq, rx_seq;
    u32 hclient, hdevice, hsubdev;
    u64 bar1_pde, bar2_pde; u32 gpc_mask, tpcs; char name[64];
    struct { u64 base, limit, reserved; u32 prot; } fbr[16]; u32 nfbr;
    u64 bar1, bar1_len, bar3, bar3_len;
    u32 mthd_size;
    struct { u32 type, runl; } eng[32]; u32 neng;
    u32 err_count, seq_count, ev_count;
} G;

static u8* sa(u64 size,u64 align){ u8* r=(u8*)kmalloc((unsigned long)(size+align)); if(!r)return 0; u8* p=(u8*)(((u64)r+align-1)&~(align-1)); for(u64 i=0;i<size;i++)p[i]=0; return p; }
static u64 PA(const void* p){ return (u64)p; }
static void cflush(const void* p,u64 n){ for(u64 a=(u64)p&~63ULL;a<(u64)p+n;a+=64)cpu_clflush(a); cpu_mb(); }

static int blob_get(nvblob* b,const char* file,const u8* s,const u8* e,u32 cap){
    if(b->d)return 1;
    int k=fs_find_name(file); if(k>=0&&FS[k].len>0){ b->n=(u32)FS[k].len; b->d=(u8*)kmalloc(b->n+16); if(!b->d)return 0; const u8* q=(const u8*)fsd(k); for(u32 i=0;i<b->n;i++)b->d[i]=q[i]; b->src="from Files"; return 1; }
    b->d=(u8*)kmalloc(cap); if(!b->d)return 0; long n=xzk_decode_into(s,(long)(e-s),b->d,(long)cap); if(n<=0){ kfree(b->d); b->d=0; return 0; } b->n=(u32)n; b->src="built in"; return 1; }
static int fw_load(void){
    if(!blob_get(&g_fw_acr,"bl.bin",_binary_nvfw_acr_bl_xz_start,_binary_nvfw_acr_bl_xz_end,0x10000))return 0;
    if(!blob_get(&g_fw_bload,"booter_load-535.113.01.bin",_binary_nvfw_booter_load_xz_start,_binary_nvfw_booter_load_xz_end,0x20000))return 0;
    if(!blob_get(&g_fw_bunload,"booter_unload-535.113.01.bin",_binary_nvfw_booter_unload_xz_start,_binary_nvfw_booter_unload_xz_end,0x20000))return 0;
    if(!blob_get(&g_fw_bl,"bootloader-535.113.01.bin",_binary_nvfw_bootloader_xz_start,_binary_nvfw_bootloader_xz_end,0x10000))return 0;
    if(!blob_get(&g_fw_gsp,"gsp-535.113.01.bin",_binary_nvfw_gsp_xz_start,_binary_nvfw_gsp_xz_end,24u<<20))return 0;
    return 1; }

static int str_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int elf_sect(const u8* f,u32 n,const char* name,const u8** out,u64* sz){
    if(n<64||f[0]!=0x7F||f[1]!='E'||f[4]!=2)return 0;
    u64 sho=*(const u64*)(f+0x28); u16 shn=*(const u16*)(f+0x3C), shs=*(const u16*)(f+0x3E);
    if(sho+(u64)shn*64>n||shs>=shn)return 0;
    u64 sto=*(const u64*)(f+sho+(u64)shs*64+0x18), stz=*(const u64*)(f+sho+(u64)shs*64+0x20); if(sto>=n||stz>n-sto||!stz||f[sto+stz-1])return 0;
    const u8* str=f+sto;
    for(u16 i=0;i<shn;i++){ const u8* s=f+sho+(u64)i*64; if(*(const u32*)s>=stz)continue; if(str_eq((const char*)str+*(const u32*)s,name)){ u64 o=*(const u64*)(s+0x18), z=*(const u64*)(s+0x20); if(o+z>n)return 0; *out=f+o; *sz=z; return 1; } }
    return 0; }

#define CMDQ 0x1000u
#define MSGQ 0x41000u
#define QSZ  0x40000u
#define QCNT 63u
static volatile u32* q32(u32 off){ return (volatile u32*)(G.shm+off); }
static u32 g_rpc_buf[0x4000];

static int cmdq_push(u32 fn,const void* pl,u32 plen,int noseq){
    u32 rlen=0x20+plen; u32 len=(0x30+rlen+0xFFFu)&~0xFFFu; if(len>16*0x1000u)return 0;
    u8* m=(u8*)g_rpc_buf; for(u32 i=0;i<len;i++)m[i]=0;
    *(u32*)(m+0x24)=G.cmd_seq++; *(u32*)(m+0x28)=len/0x1000u;
    *(u32*)(m+0x30)=0x03000000u; *(u32*)(m+0x34)=0x43505256u; *(u32*)(m+0x38)=rlen; *(u32*)(m+0x3C)=fn; *(u32*)(m+0x40)=0xFFFFFFFFu; *(u32*)(m+0x44)=0xFFFFFFFFu; *(u32*)(m+0x48)=noseq?0:G.rpc_seq++;
    for(u32 i=0;i<plen;i++)m[0x50+i]=((const u8*)pl)[i];
    u64 cs=0; for(u32 i=0;i<len;i+=8)cs^=*(u64*)(m+i); *(u32*)(m+0x20)=(u32)(cs>>32)^(u32)cs;
    u32 w=*q32(CMDQ+0x10), off=0; u64 t0=hr_ns();
    while(off<len){ u32 rp=*q32(MSGQ+0x20); u32 fr=rp+QCNT-w-1; if(fr>=QCNT)fr-=QCNT;
        if(fr<1){ if(hr_ns()-t0>2000000000ULL)return 0; cpu_pause(); continue; }
        u32 step=QCNT-w; if(step>fr)step=fr; u32 sz=len-off; if(sz>step*0x1000u)sz=step*0x1000u;
        u8* dst=G.shm+CMDQ+0x1000u+w*0x1000u; for(u32 i=0;i<sz;i++)dst[i]=m[off+i];
        w+=(sz+0xFFFu)/0x1000u; if(w>=QCNT)w-=QCNT; off+=sz; }
    cpu_mb(); *q32(CMDQ+0x10)=w; cpu_mb(); nvw32(0x110C00,0); return 1; }

static void seq_run(const u32* p,u32 plen);
static int msg_one(u32* fn,u8* out,u32 cap,u32* olen,u32 ms){
    u64 t0=hr_ns(); u32 rp=*q32(CMDQ+0x20);
    for(;;){ u32 w=*q32(MSGQ+0x10); u32 used=w+QCNT-rp; if(used>=QCNT)used-=QCNT; if(used>=1)break; if(hr_ns()-t0>(u64)ms*1000000ULL)return 0; cpu_pause(); }
    u8* e=G.shm+MSGQ+0x1000u+rp*0x1000u; u32 rlen=*(u32*)(e+0x38); u32 total=0x30+rlen; if(rlen<0x20||total>16*0x1000u){ nvl_kv("bad status-queue element length",rlen); return -1; }
    u32 slots=(total+0xFFFu)/0x1000u; u8* m=(u8*)g_rpc_buf; u32 left=total, r=rp, o=0;
    while(left){ u32 c=left>0x1000u?0x1000u:left; u8* s=G.shm+MSGQ+0x1000u+r*0x1000u; for(u32 i=0;i<c;i++)m[o+i]=s[i]; o+=c; left-=c; r++; if(r>=QCNT)r=0; }
    u32 seq=*(u32*)(m+0x24); if(seq!=G.rx_seq){ nvl_s("  status queue sequence "); nvl_d(seq); nvl_s(" expected "); nvl_d(G.rx_seq); nvl_nl(); } G.rx_seq=seq+1;
    rp+=slots; while(rp>=QCNT)rp-=QCNT; cpu_mb(); *q32(CMDQ+0x20)=rp;
    *fn=*(u32*)(m+0x3C); u32 res=*(u32*)(m+0x40); u32 pl=rlen-0x20; if(pl>cap)pl=cap; for(u32 i=0;i<pl;i++)out[i]=m[0x50+i]; *olen=pl;
    if(res){ nvl_s("  reply to function "); nvl_d(*fn); nvl_s(" has rpc_result "); nvl_x(res); nvl_nl(); return -2; }
    return 1; }
static u8 g_evbuf[0x10000];
static int ev_handle(u32 fn,u8* p,u32 n){
    if(fn==0x1002){ G.seq_count++; seq_run((const u32*)p,n); return 1; }
    if(fn==0x1006&&n>=12){ G.err_count++; nvl_s("  GSP error log: Xid "); nvl_d(*(u32*)p); nvl_s(" chid "); nvl_d(*(u32*)(p+8)); nvl_s(": "); char t[100]; u32 i=0; for(;i<99&&i+12<n&&p[12+i];i++)t[i]=(char)p[12+i]; t[i]=0; nvl_s(t); nvl_nl(); return 1; }
    if(fn==0x1004&&n>=12){ G.err_count++; nvl_s("  channel error (RC): engine "); nvl_d(*(u32*)p); nvl_s(" chid "); nvl_d(*(u32*)(p+4)); nvl_s(" type "); nvl_d(*(u32*)(p+8)); nvl_nl(); return 1; }
    if(fn==0x1005){ G.err_count++; nvl_s("  GPU MMU fault reported"); nvl_nl(); return 1; }
    G.ev_count++; return 1; }
static int rpc_wait(u32 want,u8* out,u32 cap,u32* olen,u32 ms){
    u64 t0=hr_ns();
    for(;;){ u32 fn=0, n=0; u64 el=(hr_ns()-t0)/1000000ULL; if(el>ms)return 0; int r=msg_one(&fn,g_evbuf,sizeof(g_evbuf),&n,(u32)(ms-el));
        if(r==0)return 0; if(r<0&&fn==want)return -1; if(r<0)continue;
        if(fn==want){ u32 c=n<cap?n:cap; for(u32 i=0;i<c;i++)out[i]=g_evbuf[i]; if(olen)*olen=n; return 1; }
        ev_handle(fn,g_evbuf,n); } }

static void seq_run(const u32* p,u32 n){
    if(n<40)return; u32 cnt=p[1]; const u32* c=p+10; u32 i=0; u32 save[8]; for(int k=0;k<8;k++)save[k]=p[2+k];
    while(i<cnt&&(i+1)*4+40<=n){ u32 op=c[i];
        if(op==0){ nvw32(c[i+1],c[i+2]); i+=3; }
        else if(op==1){ nvm32(c[i+1],c[i+2],c[i+3]); i+=4; }
        else if(op==2){ u32 to=c[i+4]?c[i+4]:4000000u; u64 t0=hr_ns(); while((nvr32(c[i+1])&c[i+2])!=c[i+3]){ if(hr_ns()-t0>(u64)to*1000ULL){ nvl_kv("sequencer poll timed out on",c[i+1]); break; } cpu_pause(); } i+=6; }
        else if(op==3){ hr_delay_us(c[i+1]); i+=2; }
        else if(op==4){ if(c[i+2]<8)save[c[i+2]]=nvr32(c[i+1]); i+=3; }
        else if(op==5){ nvf_reset(NVF_GSP); nvm32(0x110624,0x80u,0x80u); nvw32(0x11010C,0); i+=1; }
        else if(op==6){ if(nvr32(0x110100)&0x40u)nvw32(0x110130,2); else nvw32(0x110100,2); i+=1; }
        else if(op==7){ nv_poll(0x110100,0x10u,0x10u,2000); i+=1; }
        else { nvl_kv("unknown sequencer opcode",op); break; } }
    (void)save; }

static int rm_alloc(u32 hc,u32 hp,u32 ho,u32 cls,const void* prm,u32 psz){
    static u8 b[0x1000]; for(u32 i=0;i<0x20+psz;i++)b[i]=0;
    *(u32*)(b+0)=hc; *(u32*)(b+4)=hp; *(u32*)(b+8)=ho; *(u32*)(b+12)=cls; *(u32*)(b+20)=psz; for(u32 i=0;i<psz;i++)b[0x20+i]=((const u8*)prm)[i];
    if(!cmdq_push(103,b,0x20+psz,0))return -1000;
    static u8 r[0x1000]; u32 n=0; int w=rpc_wait(103,r,sizeof(r),&n,5000); if(w<=0)return -1001;
    u32 st=*(u32*)(r+16); if(st){ nvl_s("  RM_ALLOC class "); nvl_x(cls); nvl_s(" handle "); nvl_x(ho); nvl_s(" failed, NV_STATUS "); nvl_x(st); nvl_nl(); return -(int)st; }
    return 0; }
static int rm_ctrl(u32 hc,u32 ho,u32 cmd,void* prm,u32 psz){
    static u8 b[0x4000]; if(psz>0x3F00)return -1002; for(u32 i=0;i<0x18+psz;i++)b[i]=0;
    *(u32*)(b+0)=hc; *(u32*)(b+4)=ho; *(u32*)(b+8)=cmd; *(u32*)(b+16)=psz; for(u32 i=0;i<psz;i++)b[0x18+i]=((const u8*)prm)[i];
    if(!cmdq_push(76,b,0x18+psz,0))return -1000;
    u32 n=0; int w=rpc_wait(76,b,sizeof(b),&n,5000); if(w<=0)return -1001;
    u32 st=*(u32*)(b+12); if(st){ nvl_s("  RM_CONTROL "); nvl_x(cmd); nvl_s(" failed, NV_STATUS "); nvl_x(st); nvl_nl(); return -(int)st; }
    for(u32 i=0;i<psz&&0x18+i<n;i++)((u8*)prm)[i]=b[0x18+i]; return 0; }
static int rm_free(u32 hc,u32 ho){ u32 b[4]={hc,0,ho,0}; if(!cmdq_push(10,b,16,0))return -1000; u32 n=0; u8 r[16]; int w=rpc_wait(10,r,sizeof(r),&n,5000); return w>0?0:-1001; }

static int build_sysinfo(u8* s){
    for(int i=0;i<664;i++)s[i]=0;
    *(u64*)(s+0x000)=g_nv_bar0; *(u64*)(s+0x008)=G.bar1; *(u64*)(s+0x010)=G.bar3;
    *(u64*)(s+0x018)=((u64)g_nv_bus<<8)|((u64)g_nv_slot<<3)|(u64)g_nv_fn; *(u64*)(s+0x038)=0x00007FFFFFFFF000ULL;
    *(u32*)(s+0x040)=0x088000u; *(u32*)(s+0x044)=0x1000u; return 664; }
static int build_registry(u8* r){
    static const char* nm[3]={"RMSecBusResetEnable","RMForcePcieConfigSave","RMDevidCheckIgnore"};
    for(int i=0;i<128;i++)r[i]=0; u32 off=8+3*16;
    for(int k=0;k<3;k++){ u8* e=r+8+k*16; *(u32*)e=off; e[4]=1; *(u32*)(e+8)=1; *(u32*)(e+12)=4; const char* s=nm[k]; while(*s)r[off++]=(u8)*s++; r[off++]=0; }
    *(u32*)r=off; *(u32*)(r+4)=3; return (int)off; }

static void put_libos(u8* a,u64 id,u64 pa,u64 sz){ *(u64*)(a+0)=id; *(u64*)(a+8)=pa; *(u64*)(a+16)=sz; a[24]=1; a[25]=1; }

static int prepare(void){
    nvl_s("[3] firmware"); nvl_nl();
    if(!fw_load())return nv_fail(3,"could not unpack the NVIDIA firmware");
    nvl_s("  gsp "); nvl_d(g_fw_gsp.n); nvl_s(" bytes ("); nvl_s(g_fw_gsp.src); nvl_s("), booter_load "); nvl_d(g_fw_bload.n); nvl_s(" ("); nvl_s(g_fw_bload.src); nvl_s("), bootloader "); nvl_d(g_fw_bl.n); nvl_s(", acr bl "); nvl_d(g_fw_acr.n); nvl_nl();
    const u8* img=0; u64 isz=0; const u8* sg=0; u64 ssz=0;
    if(!elf_sect(g_fw_gsp.d,g_fw_gsp.n,".fwimage",&img,&isz))return nv_fail(3,"no .fwimage section in the GSP firmware");
    const char* sname=(G.chip==0x167||G.chip==0x168)?".fwsignature_tu11x":".fwsignature_tu10x";
    if(!elf_sect(g_fw_gsp.d,g_fw_gsp.n,sname,&sg,&ssz))return nv_fail(3,"signature section for this chip is missing");
    nvl_kv(".fwimage size",isz); nvl_s("  signature section "); nvl_s(sname); nvl_nl();
    G.elf_size=isz; u64 pages=(isz+0xFFFULL)>>12;
    G.elf=sa(pages<<12,0x1000); if(!G.elf)return nv_fail(3,"no memory for the GSP image (24 MB)");
    for(u64 i=0;i<isz;i++)G.elf[i]=img[i];
    G.r3l2_pages=(u32)(((pages*8)+0xFFFULL)>>12); G.r3l2=(u64*)sa((u64)G.r3l2_pages<<12,0x1000); G.r3l1=(u64*)sa(0x1000,0x1000); G.r3l0=(u64*)sa(0x1000,0x1000);
    if(!G.r3l2||!G.r3l1||!G.r3l0)return nv_fail(3,"no memory for the radix3 tables");
    for(u64 j=0;j<pages;j++)G.r3l2[j]=PA(G.elf)+(j<<12);
    for(u32 j=0;j<G.r3l2_pages;j++)G.r3l1[j]=PA(G.r3l2)+((u64)j<<12);
    G.r3l0[0]=PA(G.r3l1);
    G.sig=sa(0x1000,0x1000); if(!G.sig)return nv_fail(3,"no memory"); for(u64 i=0;i<ssz&&i<0x1000;i++)G.sig[i]=sg[i];
    const u8* bf=g_fw_bl.d; if(g_fw_bl.n<0x80||*(const u32*)bf!=0x10DEu)return nv_fail(3,"GSP bootloader file damaged");
    u32 bho=*(const u32*)(bf+0x0C), bdo=*(const u32*)(bf+0x10), bds=*(const u32*)(bf+0x14);
    G.app_ver=*(const u32*)(bf+bho+0x1C); u32 mco=*(const u32*)(bf+bho+0x30), mdo=*(const u32*)(bf+bho+0x28), mfo=*(const u32*)(bf+bho+0x20);
    G.boot=sa(0x1000,0x1000); if(!G.boot||bds>0x1000||bdo+bds>g_fw_bl.n)return nv_fail(3,"GSP bootloader payload too large"); for(u32 i=0;i<bds;i++)G.boot[i]=bf[bdo+i];
    nvl_kv("bootloader payload",bds); nvl_kv("bootloader appVersion",G.app_ver);
    for(int k=0;k<3;k++){ G.log[k]=sa(0x10000,0x1000); if(!G.log[k])return nv_fail(3,"no memory for log buffers"); for(int p=0;p<16;p++)*(u64*)(G.log[k]+8+p*8)=PA(G.log[k])+((u64)p<<12); }
    G.shm=sa(0x81000,0x1000); G.rmargs=sa(0x1000,0x1000); G.libos=sa(0x1000,0x1000); G.meta=sa(0x1000,0x1000); G.flush=sa(0x1000,0x1000);
    if(!G.shm||!G.rmargs||!G.libos||!G.meta||!G.flush)return nv_fail(3,"no memory for GSP shared buffers");
    for(int i=0;i<129;i++)*(u64*)(G.shm+i*8)=PA(G.shm)+((u64)i<<12);
    *q32(CMDQ+0x00)=0; *q32(CMDQ+0x04)=QSZ; *q32(CMDQ+0x08)=0x1000; *q32(CMDQ+0x0C)=QCNT; *q32(CMDQ+0x10)=0; *q32(CMDQ+0x14)=1; *q32(CMDQ+0x18)=32; *q32(CMDQ+0x1C)=0x1000;
    *(u64*)(G.rmargs+0x00)=PA(G.shm); *(u32*)(G.rmargs+0x08)=129; *(u64*)(G.rmargs+0x10)=CMDQ; *(u64*)(G.rmargs+0x18)=MSGQ;
    put_libos(G.libos+0,0x4C4F47494E4954ULL,PA(G.log[0]),0x10000); put_libos(G.libos+32,0x4C4F47494E5452ULL,PA(G.log[1]),0x10000);
    put_libos(G.libos+64,0x4C4F47524DULL,PA(G.log[2]),0x10000); put_libos(G.libos+96,0x524D41524753ULL,PA(G.rmargs),0x1000);
    G.cmd_seq=0; G.rpc_seq=0; G.rx_seq=0;
    static u8 tmp[1024]; int n=build_sysinfo(tmp); if(!cmdq_push(72,tmp,(u32)n,1))return nv_fail(3,"could not queue SET_SYSTEM_INFO");
    n=build_registry(tmp); if(!cmdq_push(73,tmp,(u32)n,1))return nv_fail(3,"could not queue SET_REGISTRY");
    if(!nv_fb_layout(G.elf_size,0x1000))return nv_fail(2,"VRAM size register looks wrong");
    u8* m=G.meta; nvfb_layout* L=&g_nvl;
    *(u64*)(m+0x00)=0xDC3AAE21371A60B3ULL; *(u64*)(m+0x08)=1; *(u64*)(m+0x10)=PA(G.r3l0); *(u64*)(m+0x18)=G.elf_size;
    *(u64*)(m+0x20)=PA(G.boot); *(u64*)(m+0x28)=0x1000; *(u64*)(m+0x30)=mco; *(u64*)(m+0x38)=mdo; *(u64*)(m+0x40)=mfo;
    *(u64*)(m+0x48)=PA(G.sig); *(u64*)(m+0x50)=0x1000; *(u64*)(m+0x58)=L->nonwpr_addr; *(u64*)(m+0x60)=L->nonwpr_addr; *(u64*)(m+0x68)=L->nonwpr_size;
    *(u64*)(m+0x70)=L->wpr2_addr; *(u64*)(m+0x78)=L->heap_addr; *(u64*)(m+0x80)=L->heap_size; *(u64*)(m+0x88)=L->elf_addr; *(u64*)(m+0x90)=L->boot_addr;
    *(u64*)(m+0x98)=L->frts_addr; *(u64*)(m+0xA0)=L->frts_size; *(u64*)(m+0xA8)=L->vga_addr&~0x1FFFFULL; *(u64*)(m+0xB0)=L->fb_size;
    *(u64*)(m+0xB8)=L->vga_addr; *(u64*)(m+0xC0)=L->vga_size;
    cflush(G.elf,pages<<12); cflush(G.r3l2,(u64)G.r3l2_pages<<12); cflush(G.r3l1,0x1000); cflush(G.r3l0,0x1000); cflush(G.sig,0x1000); cflush(G.boot,0x1000);
    cflush(G.shm,0x81000); cflush(G.rmargs,0x1000); cflush(G.libos,0x1000); cflush(G.meta,0x1000);
    nvl_kv("radix3 level 0",PA(G.r3l0)); nvl_kv("GspFwWprMeta",PA(G.meta)); nvl_kv("LibOS arguments",PA(G.libos)); nvl_kv("message queues",PA(G.shm));
    return 1; }

static void print_layout(void){
    nvfb_layout* L=&g_nvl;
    nvl_kv("VRAM size",L->fb_size); nvl_kv("VGA workspace",L->vga_addr); nvl_kv("FRTS",L->frts_addr); nvl_kv("boot binary",L->boot_addr); nvl_kv("GSP image",L->elf_addr);
    nvl_kv("WPR heap",L->heap_addr); nvl_kv("WPR heap size",L->heap_size); nvl_kv("WPR2 start",L->wpr2_addr); nvl_kv("WPR2 size",L->wpr2_size); nvl_kv("non-WPR heap (reserved from here up)",L->nonwpr_addr); }

static int probe(void){
    nvl_s("[1] PCI and chip"); nvl_nl();
    if(!nv_probe())return nv_fail(1,"no NVIDIA display controller on PCI");
    nv_read_id(); G.chip=(g_nv_boot0>>20)&0x1FFu;
    u32 b1=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x14); G.bar1=(u64)(b1&0xFFFFFFF0u); int bi=2; if((b1&6u)==4u){ G.bar1|=(u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x18)<<32; bi=3; }
    u32 b3=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x10+bi*4); G.bar3=(u64)(b3&0xFFFFFFF0u); if((b3&6u)==4u)G.bar3|=(u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x14+bi*4)<<32;
    for(int w=0;w<2;w++){ int r=w?bi:1; u32 o=0x10+(u32)r*4; u32 lo=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o); int b64=(lo&6u)==4u; u32 hi=b64?pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4):0;
        u32 c=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x04); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,c&~2u);
        pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o,0xFFFFFFFFu); if(b64)pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4,0xFFFFFFFFu);
        u64 m=(u64)(pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o)&0xFFFFFFF0u)|(b64?((u64)pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4)<<32):0xFFFFFFFF00000000ULL);
        pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o,lo); if(b64)pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,(int)o+4,hi); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,(c&0xFFFFu)|0x406u);
        u64 sz=m?(~m)+1:0; if(w)G.bar3_len=sz; else G.bar1_len=sz; }
    { u32 c=pci_cfg(g_nv_bus,g_nv_slot,g_nv_fn,0x04); pci_cfgw(g_nv_bus,g_nv_slot,g_nv_fn,0x04,(c&0xFFFFu)|0x406u); }
    nvl_kv("PCI device",g_nv_dev); nvl_kv("BOOT0",g_nv_boot0); nvl_kv("chipset",G.chip); nvl_kv("BAR0",g_nv_bar0); nvl_kv("BAR1 (VRAM aperture)",G.bar1); nvl_kv("BAR1 size",G.bar1_len); nvl_kv("BAR3 (instance aperture)",G.bar3); nvl_kv("BAR3 size",G.bar3_len);
    if(G.chip<0x162||G.chip>0x168)return nv_fail(1,"this driver supports Turing (TU102-TU117) only");
    if(G.chip==0x167){ nvl_s("  TU117: using the TU116 booter (copy booter_load-535.113.01.bin for tu117 into Files to override)"); nvl_nl(); }
    nvl_s("[2] VRAM"); nvl_nl();
    if(!nv_gfw_wait())return nv_fail(2,"the VBIOS did not finish initialising the GPU (GFW_BOOT)");
    nvl_kv("0x100ce0 (memory size register)",nvr32(0x100CE0)); nvl_kv("0x625f04 (VGA workspace)",nvr32(0x625F04));
    u64 fb=nv_vram_size(); nvl_kv("VRAM bytes",fb); nvl_s("  = "); nvl_d(fb>>20); nvl_s(" MB"); nvl_nl();
    if(!fb)return nv_fail(2,"VRAM size reads as 0");
    return 1; }

static u64 g_con_end=0;
static void vram_alloc_init(void){
    u64 lo=0, hi=g_nvl.nonwpr_addr; u64 fbpa=*(volatile u64*)(NV_BOOTINFO); u32 pitch=*(volatile u32*)(NV_BOOTINFO+8); u32 h=*(volatile u16*)(NV_BOOTINFO+14);
    g_con_end=0; if(G.bar1&&fbpa>=G.bar1&&fbpa<G.bar1+G.bar1_len){ g_con_end=((fbpa-G.bar1)+(u64)pitch*h+0xFFFFFULL)&~0xFFFFFULL; }
    if(G.nfbr){ hi=0; for(u32 i=0;i<G.nfbr;i++) if(!G.fbr[i].reserved&&!G.fbr[i].prot&&G.fbr[i].limit+1>hi){ lo=G.fbr[i].base; hi=G.fbr[i].limit+1; } }
    nvv_init(lo,hi); nvv_reserve(0,g_con_end+0x100000ULL);
    nvl_kv("VRAM allocator from",lo); nvl_kv("VRAM allocator to",hi); nvl_kv("console framebuffer reserved below",g_con_end+0x100000ULL); nvl_kv("VRAM free for the driver",nvv_free_bytes()); }

static int gsp_pre(void){
    int r=prepare(); if(r<=0)return r; print_layout();
    nvw32(0x100C10,(u32)(PA(G.flush)>>8)); nvl_kv("sysmem flush page",PA(G.flush));
    nvl_s("[4] FWSEC-FRTS on the GSP falcon"); nvl_nl();
    if(!nvb_read_rom())return nv_fail(4,"could not read the VBIOS ROM");
    r=nv_run_fwsec(g_fw_acr.d,g_fw_acr.n,0x15); if(r<=0)return r;
    nvl_s("  WPR2 is set up"); nvl_nl();
    nvm32(0x1103C0,1u,1u); hr_delay_us(10); nvm32(0x1103C0,1u,0u); nv_poll(0x11010C,6u,0u,10);
    nvw32(0x110040,(u32)PA(G.libos)); nvw32(0x110044,(u32)(PA(G.libos)>>32));
    nvl_s("[5] booter_load on SEC2"); nvl_nl();
    r=nv_booter(g_fw_bload.d,g_fw_bload.n,PA(G.meta),0); if(r<=0)return r;
    cflush(G.meta,256); nvl_kv("GspFwWprMeta.verified",*(volatile u64*)(G.meta+0xF8));
    G.state=1; return 1; }
static int gsp_post(void){
    nvl_s("[6] starting GSP-RM"); nvl_nl();
    nvw32(0x110080,G.app_ver);
    nvl_kv("GSP RISC-V status (0x111240)",nvr32(0x111240));
    if(!(nvr32(0x111240)&1u)){ nvf_dump(NVF_GSP,"GSP falcon"); return nv_fail(6,"GSP RISC-V core is not running after booter"); }
    nvl_s("[7] waiting for GSP_INIT_DONE"); nvl_nl();
    u64 t0=hr_ns(); int got=0;
    while(hr_ns()-t0<15000000000ULL){ u32 fn=0,n=0; int m=msg_one(&fn,g_evbuf,sizeof(g_evbuf),&n,500); if(m==0)continue; if(m<0&&m!=-2)break; if(fn==0x1001){ got=1; break; } if(m>0)ev_handle(fn,g_evbuf,n); }
    nvl_kv("LOGINIT put pointer",*(volatile u64*)G.log[0]); nvl_kv("LOGRM put pointer",*(volatile u64*)G.log[2]); nvl_kv("GSP mailbox0",nvr32(0x110040)); nvl_kv("sequencer runs",G.seq_count);
    if(!got){ nvf_dump(NVF_GSP,"GSP falcon"); nvl_kv("cmdq write/read",((u64)*q32(CMDQ+0x10)<<32)|*q32(MSGQ+0x20)); nvl_kv("msgq write/read",((u64)*q32(MSGQ+0x10)<<32)|*q32(CMDQ+0x20)); return nv_fail(7,"GSP-RM did not report GSP_INIT_DONE within 15 s"); }
    G.state=2; nvl_s("  GSP-RM is running"); nvl_nl();
    nvl_s("[8] GET_GSP_STATIC_INFO"); nvl_nl();
    static u8 si[2168]; for(int i=0;i<2168;i++)si[i]=0;
    if(!cmdq_push(65,si,2168,0))return nv_fail(8,"could not send GET_GSP_STATIC_INFO");
    u32 n=0; if(rpc_wait(65,si,sizeof(si),&n,5000)<=0)return nv_fail(8,"no reply to GET_GSP_STATIC_INFO");
    G.gpc_mask=*(u32*)(si+0x124); G.tpcs=0; for(int g=0;g<32;g++) if(G.gpc_mask&(1u<<g)){ u32 tm=*(u32*)(si+0x128+g*8+4); while(tm){ G.tpcs+=tm&1u; tm>>=1; } }
    G.nfbr=*(u32*)(si+0x348); if(G.nfbr>16)G.nfbr=16;
    for(u32 i=0;i<G.nfbr;i++){ u8* e=si+0x348+8+i*48; G.fbr[i].base=*(u64*)e; G.fbr[i].limit=*(u64*)(e+8); G.fbr[i].reserved=*(u64*)(e+16); G.fbr[i].prot=e[30];
        nvl_s("  FB region "); nvl_d(i); nvl_s(": "); nvl_x(G.fbr[i].base); nvl_s(" - "); nvl_x(G.fbr[i].limit); nvl_s(G.fbr[i].reserved?" reserved":""); nvl_s(G.fbr[i].prot?" protected":""); nvl_nl(); }
    G.bar1_pde=*(u64*)(si+0x828); G.bar2_pde=*(u64*)(si+0x830); G.hclient=*(u32*)(si+0x868); G.hdevice=*(u32*)(si+0x86C); G.hsubdev=*(u32*)(si+0x870);
    for(int i=0;i<63;i++)G.name[i]=(char)si[0x71C+i]; G.name[63]=0;
    nvl_s("  GPU name: "); nvl_s(G.name); nvl_nl(); nvl_kv("GPC mask",G.gpc_mask); nvl_kv("TPCs",G.tpcs); nvl_kv("BAR1 root PD",G.bar1_pde); nvl_kv("BAR2 root PD",G.bar2_pde); nvl_kv("internal client",G.hclient);
    nvw32(0x110004,0x40);
    vram_alloc_init();
    G.state=3; return 1; }
static int boot_gsp(void){
    if(G.state>=2){ nvl_s("  GSP is already running"); nvl_nl(); return 1; }
    if(G.state<1){ int r=gsp_pre(); if(r<=0)return r; }
    return gsp_post(); }

static int bar_test(volatile u32* cpu,u64 vram,const char* what){
    nvp_wr32(vram,0x4E565831u); nvp_wr32(vram+4,0xA5A55A5Au); u32 a=cpu[0], b=cpu[1];
    cpu[2]=0x13572468u; cpu_mb(); (void)cpu[2]; u32 c=nvp_rd32(vram+8);
    nvl_s("  "); nvl_s(what); nvl_s(": read "); nvl_x(a); nvl_s(" "); nvl_x(b); nvl_s(", write seen in VRAM as "); nvl_x(c); nvl_nl();
    return a==0x4E565831u&&b==0xA5A55A5Au&&c==0x13572468u; }
static nvvmm g_bar2; static u64 g_bt2, g_bt1, g_bva1;
static int bars(void){
    nvl_s("[9] BAR2 (instance memory window)"); nvl_nl();
    u64 lim=G.bar3_len?G.bar3_len/2:0x1000000ULL;
    if(!nvm_vmm_new(&g_bar2,lim))return nv_fail(9,"no VRAM for the BAR2 page directory");
    u64 tv=nvv_alloc(0x10000,0x10000); if(!tv)return nv_fail(9,"no VRAM for the BAR2 test page");
    g_bt2=tv; if(!nvm_map(&g_bar2,0,tv,0x10000,0,0,0))return nv_fail(9,"could not build BAR2 page tables");
    struct { u32 type, pad; u64 val, shift; } up={1,0,nvm_pd3_entry0(&g_bar2),47};
    nvl_kv("BAR2 PD3[0]",up.val);
    if(!cmdq_push(70,&up,24,0))return nv_fail(9,"could not send UPDATE_BAR_PDE"); u8 rr[64]; u32 n=0; if(rpc_wait(70,rr,sizeof(rr),&n,5000)<=0)return nv_fail(9,"UPDATE_BAR_PDE was refused");
    nvm_tlb_flush(G.bar2_pde,1);
    u64 va3=acpi_hi_map(G.bar3); int ok2=va3?bar_test((volatile u32*)va3,tv,"BAR2 window"):0; if(!va3){ nvl_s("  BAR3 aperture cannot be mapped by the CPU"); nvl_nl(); }
    nvl_s("[10] BAR1 (VRAM window)"); nvl_nl();
    nvvmm b1; b1.pd3=G.bar1_pde; b1.limit=G.bar1_len; b1.ok=1;
    for(int i=0;i<4;i++)nvp_wr64(G.bar1_pde+(u64)i*8,0);
    int ok1=1;
    if(g_con_end){ if(!nvm_map(&b1,0,0,g_con_end,0,0,0))ok1=0; nvl_kv("console framebuffer mapped through BAR1, bytes",g_con_end); }
    u64 t1=nvv_alloc(0x10000,0x10000); u64 bva=G.bar1_len>0x2000000ULL?G.bar1_len-0x1000000ULL:g_con_end;
    g_bt1=t1; g_bva1=bva; if(!t1||!nvm_map(&b1,bva,t1,0x10000,0,0,0))ok1=0;
    nvm_tlb_flush(G.bar1_pde,1);
    u64 va1=acpi_hi_map(G.bar1); if(!va1){ nvl_s("  BAR1 aperture cannot be mapped by the CPU"); nvl_nl(); ok1=0; }
    else if(ok1)ok1=bar_test((volatile u32*)(va1+bva),t1,"BAR1 window");
    if(!ok2)return nv_fail(9,"BAR2 window does not reach VRAM");
    if(!ok1)return nv_fail(10,"BAR1 window does not reach VRAM");
    G.state=4; return 1; }

#define HC(c) (0xC1D00000u|(c))
#define HDEV 0xDE1D0000u
#define HSUB 0x5D1D0000u
#define HVAS 0x90F10000u
#define HCH(c) (0xF1F00000u|(c))
static int client_dev(u32 c){
    u8 p[108]; for(int i=0;i<108;i++)p[i]=0; *(u32*)p=HC(c); *(u32*)(p+4)=0xFFFFFFFFu;
    if(rm_alloc(HC(c),HC(c),HC(c),0x0000,p,108))return 0;
    u8 d[56]; for(int i=0;i<56;i++)d[i]=0; *(u32*)(d+4)=HC(c); if(rm_alloc(HC(c),HC(c),HDEV,0x0080,d,56))return 0;
    u32 s=0; if(rm_alloc(HC(c),HDEV,HSUB,0x2080,&s,4))return 0; return 1; }
static void memdesc(u8* p,u64 base,u64 size,u32 as,u32 ca){ *(u64*)p=base; *(u64*)(p+8)=size; *(u32*)(p+16)=as; *(u32*)(p+20)=ca; }
static int chan_alloc(u32 c,u32 chid,u32 et,int priv,u64 inst,u64 userd,u64 mthd,u32 mthd_as,u64 gpva,u32 gpn){
    static u8 p[360]; for(int i=0;i<360;i++)p[i]=0;
    *(u64*)(p+0x08)=gpva; *(u32*)(p+0x10)=gpn; *(u32*)(p+0x14)=((chid%8)<<8)|((chid/8)<<12)|(1u<<21)|((priv?1u:0u)<<5); *(u32*)(p+0x1C)=HVAS; *(u32*)(p+0x80)=et;
    memdesc(p+0x90,inst,0x1000,2,1); memdesc(p+0xA8,userd,0x200,2,1); memdesc(p+0xC0,inst,0x200,2,1); memdesc(p+0xD8,mthd,G.mthd_size,mthd_as,mthd_as==1?0:1);
    *(u32*)(p+0xF4)=priv?0x15u:0x14u;
    return rm_alloc(HC(c),HDEV,HCH(chid),0xC46F,p,360)==0; }

static struct { u32 idx, id, global, init, ro; u64 size, align; u64 mem; } g_cb[10]; static int g_ncb=0;
static int ctxbuf_info(void){
    static u8 p[1600]; for(int i=0;i<1600;i++)p[i]=0;
    if(rm_ctrl(G.hclient,G.hsubdev,0x20800A32u,p,1600))return 0;
    static const u32 T[][5]={{0x00,0,0,1,0},{0x10,2,0,1,0},{0x11,3,1,0,0},{0x0D,4,1,0,0},{0x13,5,1,0,0},{0x14,6,1,0,0},{0x17,9,1,1,0},{0x18,10,1,1,1},{0x18,11,1,1,1}};
    g_ncb=0; for(int i=0;i<9;i++){ u32 sz=*(u32*)(p+T[i][0]*8); if(!sz)continue; g_cb[g_ncb].idx=T[i][0]; g_cb[g_ncb].id=T[i][1]; g_cb[g_ncb].global=T[i][2]; g_cb[g_ncb].init=T[i][3]; g_cb[g_ncb].ro=T[i][4];
        u64 s=sz; if(T[i][1]==0)s=((s+0xFFFULL)&~0xFFFULL)+64*0x1000ULL; u64 pg=s>=0x200000ULL?0x200000ULL:s>=0x10000ULL?0x10000ULL:0x1000ULL; u64 al=pg;
        if(T[i][1]==5){ al=1; while(al<s)al<<=1; }
        g_cb[g_ncb].size=s; g_cb[g_ncb].align=al; g_cb[g_ncb].mem=0; g_ncb++;
        nvl_s("  context buffer id "); nvl_d(T[i][1]); nvl_s(" size "); nvl_x(s); nvl_nl(); }
    return g_ncb>0; }
static u64 g_va_next=0x200000000ULL;
static u64 va_take(u64 size,u64 align){ if(align<0x1000)align=0x1000; u64 v=(g_va_next+align-1)&~(align-1); g_va_next=v+((size+0xFFFULL)&~0xFFFULL); return v; }
static int promote(u32 c,u32 chid,nvvmm* vm,int golden){
    static u8 p[560]; for(int i=0;i<560;i++)p[i]=0; u32 n=0;
    *(u32*)p=1; *(u32*)(p+12)=HC(c); *(u32*)(p+16)=HCH(chid);
    for(int i=0;i<g_ncb&&n<16;i++){ int alloc=golden||!g_cb[i].global; if(!alloc&&g_cb[i].id==11)continue;
        u8* e=p+48+n*32; u64 mem=g_cb[i].mem;
        if(alloc){ mem=nvv_alloc(g_cb[i].size,g_cb[i].align); if(!mem){ nvl_kv("no VRAM for context buffer",g_cb[i].id); return 0; } if(g_cb[i].global)g_cb[i].mem=mem; }
        if(!mem)continue;
        *(u16*)(e+28)=(u16)g_cb[i].id; int initz=g_cb[i].init&&alloc; e[30]=(u8)initz;
        if(g_cb[i].id==10)e[31]=1;
        else { u64 va=va_take(g_cb[i].size,g_cb[i].align); if(!nvm_map(vm,va,mem,g_cb[i].size,0,1,(int)g_cb[i].ro))return 0; *(u64*)(e+8)=va; }
        if(initz){ *(u64*)e=mem; *(u64*)(e+16)=g_cb[i].size; *(u32*)(e+24)=4; }
        n++; }
    *(u32*)(p+40)=n; nvm_tlb_flush(vm->pd3,0);
    return rm_ctrl(HC(c),HSUB,0x2080012Bu,p,560)==0; }

static int golden(void){
    nvl_s("[12] GR golden context"); nvl_nl();
    if(!client_dev(0))return nv_fail(12,"golden client/device/subdevice allocation failed");
    nvvmm gv; if(!nvm_vmm_new(&gv,1ULL<<49))return nv_fail(12,"no VRAM for the golden page directory");
    u8 va[48]; for(int i=0;i<48;i++)va[i]=0; if(rm_alloc(HC(0),HDEV,HVAS,0x90F1,va,48))return nv_fail(12,"golden VA space allocation failed");
    u64 pd2=nvv_alloc(0x1000,0x1000), pd1=nvv_alloc(0x1000,0x1000); if(!pd2||!pd1)return nv_fail(12,"no VRAM");
    nvp_wr64(gv.pd3,(pd2>>4)|2ULL); nvp_wr64(pd2,(pd1>>4)|2ULL);
    static u8 cp[184]; for(int i=0;i<184;i++)cp[i]=0;
    *(u64*)(cp+8)=0x20000000ULL; *(u64*)(cp+16)=0x100000000ULL; *(u64*)(cp+24)=0x11FFFFFFFULL; *(u32*)(cp+32)=3;
    *(u64*)(cp+40)=gv.pd3; *(u64*)(cp+48)=0x20; *(u32*)(cp+56)=1; cp[60]=47;
    *(u64*)(cp+64)=pd2; *(u64*)(cp+72)=0x1000; *(u32*)(cp+80)=1; cp[84]=38;
    *(u64*)(cp+88)=pd1; *(u64*)(cp+96)=0x1000; *(u32*)(cp+104)=1; cp[108]=29;
    if(rm_ctrl(HC(0),HVAS,0x90F10106u,cp,184))return nv_fail(12,"COPY_SERVER_RESERVED_PDES failed");
    u64 gi=nvv_alloc(0x12000,0x1000); if(!gi)return nv_fail(12,"no VRAM for the golden channel");
    nvm_inst_join(&gv,gi);
    if(!chan_alloc(0,0,1,1,gi,gi+0x1000,gi+0x2000,1,0,512))return nv_fail(12,"golden channel allocation failed");
    if(!ctxbuf_info())return nv_fail(12,"could not read the GR context buffer sizes");
    if(!promote(0,0,&gv,1))return nv_fail(12,"PROMOTE_CTX for the golden channel failed");
    if(rm_alloc(HC(0),HCH(0),0x97000000u,0xC597,0,0))return nv_fail(12,"TURING_A on the golden channel failed");
    rm_free(HC(0),0x97000000u); rm_free(HC(0),HCH(0));
    nvl_s("  golden context created"); nvl_nl(); return 1; }

static int find_eng(u32 type,u32* runl){ for(u32 i=0;i<G.neng;i++) if(G.eng[i].type==type){ *runl=G.eng[i].runl; return 1; } return 0; }
static int fifo_info(void){
    static u8 p[3212]; for(int i=0;i<3212;i++)p[i]=0;
    if(rm_ctrl(G.hclient,G.hsubdev,0x20801112u,p,3212))return 0;
    u32 n=*(u32*)(p+4); if(n>32)n=32; G.neng=0;
    for(u32 i=0;i<n;i++){ u8* e=p+12+i*100; G.eng[G.neng].type=*(u32*)(e+2*4); G.eng[G.neng].runl=*(u32*)(e+3*4); char nm[17]; for(int k=0;k<16;k++)nm[k]=(char)e[84+k]; nm[16]=0;
        nvl_s("  engine "); nvl_s(nm); nvl_s(" type "); nvl_d(G.eng[G.neng].type); nvl_s(" runlist "); nvl_d(G.eng[G.neng].runl); nvl_nl(); G.neng++; }
    u32 ms=0; if(rm_ctrl(G.hclient,G.hsubdev,0x20802A08u,&ms,4))return 0; G.mthd_size=ms; nvl_kv("method buffer size",ms);
    return 1; }

static struct { u64 inst, userd; u8* mthd; u8* ring; u64 ringva; u32 chid, runl, et, put, token; } g_ch[2];
static nvvmm g_uv;
static void ce_kick(u64 pbva,int n){
    u32* ring=(u32*)g_ch[1].ring; u32 put=g_ch[1].put; ring[put*2]=(u32)(pbva&0xFFFFFFFCULL); ring[put*2+1]=(u32)((pbva>>32)&0xFFu)|((u32)n<<10); cflush(ring,0x1000);
    put=(put+1)%512; g_ch[1].put=put; nvp_wr32(g_ch[1].userd+0x8C,put); (void)nvp_rd32(g_ch[1].userd+0x8C); cpu_mb(); nvw32(0xBB0090,g_ch[1].token); }
static int user_chan(nvvmm* vm,int k,u32 chid,u32 et,u32 runl){
    g_ch[k].chid=chid; g_ch[k].et=et; g_ch[k].runl=runl; g_ch[k].put=0;
    g_ch[k].inst=nvv_alloc(0x1000,0x1000); g_ch[k].userd=nvv_alloc(0x1000,0x1000); g_ch[k].mthd=sa(G.mthd_size?G.mthd_size:0x1000,0x1000); g_ch[k].ring=sa(0x1000,0x1000);
    if(!g_ch[k].inst||!g_ch[k].userd||!g_ch[k].mthd||!g_ch[k].ring)return 0;
    g_ch[k].ringva=va_take(0x1000,0x1000); if(!nvm_map(vm,g_ch[k].ringva,PA(g_ch[k].ring),0x1000,1,0,0))return 0; nvm_tlb_flush(vm->pd3,0);
    nvm_inst_join(vm,g_ch[k].inst);
    if(!chan_alloc(1,chid,et,0,g_ch[k].inst,g_ch[k].userd,PA(g_ch[k].mthd),1,g_ch[k].ringva,512))return 0;
    if(rm_ctrl(HC(1),HCH(chid),0xA06F0104u,&et,4))return 0;
    u8 sch[2]={1,0}; if(rm_ctrl(HC(1),HCH(chid),0xA06F0103u,sch,2))return 0;
    g_ch[k].token=(runl<<16)|chid; u32 tok=0; if(rm_ctrl(HC(1),HCH(chid),0xC36F0108u,&tok,4)==0){ g_ch[k].token=tok; nvl_s("  channel "); nvl_d(chid); nvl_s(" work-submit token "); nvl_x(tok); nvl_s(" (computed "); nvl_x((runl<<16)|chid); nvl_s(")"); nvl_nl(); }
    return 1; }

static int ce_test(nvvmm* vm){
    nvl_s("[15] copy-engine test (DMA 4 KB through the new channel)"); nvl_nl();
    u8* src=sa(0x1000,0x1000); u8* dst=sa(0x1000,0x1000); u32* pb=(u32*)sa(0x1000,0x1000); if(!src||!dst||!pb)return 0;
    for(int i=0;i<0x1000;i++){ src[i]=(u8)(i*7+3); dst[i]=0; }
    u64 vs=va_take(0x1000,0x1000), vd=va_take(0x1000,0x1000), vp=va_take(0x1000,0x1000);
    if(!nvm_map(vm,vs,PA(src),0x1000,1,0,0)||!nvm_map(vm,vd,PA(dst),0x1000,1,0,0)||!nvm_map(vm,vp,PA(pb),0x1000,1,0,0))return 0; nvm_tlb_flush(vm->pd3,0);
    int n=0; u32 sc=4u<<13;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0000u>>2); pb[n++]=0xC5B5u;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0400u>>2); pb[n++]=(u32)(vs>>32); pb[n++]=(u32)vs; pb[n++]=(u32)(vd>>32); pb[n++]=(u32)vd;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0410u>>2); pb[n++]=0x1000; pb[n++]=0x1000; pb[n++]=0x1000; pb[n++]=1;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0300u>>2); pb[n++]=0x186u;
    cflush(src,0x1000); cflush(dst,0x1000); cflush(pb,0x1000);
    nvl_kv("doorbell token",g_ch[1].token); ce_kick(vp,n);
    u64 t0=hr_ns(); int ok=0; while(hr_ns()-t0<2000000000ULL){ cflush(dst,0x1000); int same=1; for(int i=0;i<0x1000;i++) if(dst[i]!=src[i]){ same=0; break; } if(same){ ok=1; break; } hr_delay_us(1000); }
    nvl_kv("USERD GPGet",nvp_rd32(g_ch[1].userd+0x88)); nvl_kv("USERD GPPut",nvp_rd32(g_ch[1].userd+0x8C));
    for(int k=0;k<20;k++){ u32 fn=0,nn=0; if(msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,1)<=0)break; ev_handle(fn,g_evbuf,nn); }
    return ok; }

static int objects(void){
    if(G.state<3)return nv_fail(11,"GSP is not running - use nvgsp boot first");
    nvl_s("[11] FIFO engines"); nvl_nl();
    if(!fifo_info())return nv_fail(11,"FIFO device info control failed");
    if(G.state<5){ int r=golden(); if(r<=0)return r; G.state=5; }
    nvl_s("[13] client, device, subdevice, GPU address space"); nvl_nl();
    if(!client_dev(1))return nv_fail(13,"client/device/subdevice allocation failed");
    if(!nvm_vmm_new(&g_uv,1ULL<<49))return nv_fail(13,"no VRAM for the page directory");
    u8 va[48]; for(int i=0;i<48;i++)va[i]=0; *(u32*)(va+4)=8; if(rm_alloc(HC(1),HDEV,HVAS,0x90F1,va,48))return nv_fail(13,"FERMI_VASPACE_A allocation failed");
    u8 pd[32]; for(int i=0;i<32;i++)pd[i]=0; *(u64*)pd=g_uv.pd3; *(u32*)(pd+8)=4; *(u32*)(pd+16)=HVAS;
    if(rm_ctrl(HC(1),HDEV,0x00801813u,pd,32))return nv_fail(13,"SET_PAGE_DIRECTORY failed");
    nvl_kv("address space root PD",g_uv.pd3);
    nvl_s("[14] channels and engine objects"); nvl_nl();
    u32 grl=0, cel=0, cet=0; if(!find_eng(1,&grl))return nv_fail(14,"no GR engine in the device table");
    for(u32 t=9;t<19;t++) if(find_eng(t,&cel)){ cet=t; break; } if(!cet)return nv_fail(14,"no copy engine in the device table");
    if(!user_chan(&g_uv,0,8,1,grl))return nv_fail(14,"GR channel allocation failed");
    if(!promote(1,8,&g_uv,0))return nv_fail(14,"PROMOTE_CTX for the GR channel failed");
    if(rm_alloc(HC(1),HCH(8),0xC5970001u,0xC597,0,0))return nv_fail(14,"TURING_A (3D) object failed");
    nvl_s("  TURING_A (3D, class c597) created"); nvl_nl();
    if(rm_alloc(HC(1),HCH(8),0xC5C00001u,0xC5C0,0,0))return nv_fail(14,"TURING_COMPUTE_A object failed");
    nvl_s("  TURING_COMPUTE_A (class c5c0) created"); nvl_nl();
    if(!user_chan(&g_uv,1,9,cet,cel))return nv_fail(14,"copy-engine channel allocation failed");
    u32 cp[2]={1,cet}; if(rm_alloc(HC(1),HCH(9),0xC5B50001u,0xC5B5,cp,8))return nv_fail(14,"TURING_DMA_COPY_A object failed");
    nvl_s("  TURING_DMA_COPY_A (class c5b5) created"); nvl_nl();
    G.state=6;
    if(!ce_test(&g_uv))return nv_fail(15,"the copy engine did not copy the test buffer within 2 s");
    nvl_s("  copy engine copied 4096 bytes correctly - the GPU executes commands"); nvl_nl();
    G.state=7; return 1; }

static int unload(void){
    if(G.state<1){ nvl_s("  GSP was not started by this session"); nvl_nl(); return 1; }
    if(G.state>=2){ u8 u[8]={0}; cmdq_push(47,u,8,0); u8 r[64]; u32 n=0; rpc_wait(47,r,sizeof(r),&n,5000); nv_poll(0x110040,0xFFFFFFFFu,0x80000000u,2000); }
    nvf_reset(NVF_GSP);
    int r=nv_run_fwsec(g_fw_acr.d,g_fw_acr.n,0x19); if(r<=0)return r;
    if(!nvr32(0x1FA828)){ nvl_s("  WPR2 not set"); nvl_nl(); G.state=0; return 1; }
    r=nv_booter(g_fw_bunload.d,g_fw_bunload.n,0,1); if(r<=0)return r;
    nvl_kv("WPR2 hi after unload",nvr32(0x1FA828)); G.state=0; return nvr32(0x1FA828)==0?1:nv_fail(5,"WPR2 is still set after booter_unload"); }

static u32* g_cepb=0; static volatile u32* g_cesem=0; static u64 g_cepb_va=0, g_cesem_va=0; static u32 g_ce_seq=0; static u32 g_ce_copies=0, g_ce_timeouts=0;
int nvgsp_ce_ready(void){ return G.state>=7&&g_uv.ok; }
u64 nvgsp_ce_map(u64 kva,u64 len){
    u64 pa=kva;
    if(!nvgsp_ce_ready()||!len)return 0;
    u64 a=pa&~0xFFFULL, n=((pa+len+0xFFFULL)&~0xFFFULL)-a; u64 va=va_take(n,0x10000);
    for(u64 o=0;o<n;o+=0x1000){ u64 p=kva_to_pa(a+o); if(!p||!nvm_map(&g_uv,va+o,p&~0xFFFULL,0x1000,1,0,0))return 0; }
    nvm_tlb_flush(g_uv.pd3,0); return va+(pa-a); }
int nvgsp_ce_copy2d(u64 src,u32 spitch,u64 dst,u32 dpitch,u32 wbytes,u32 lines){
    if(!nvgsp_ce_ready()||!wbytes||!lines)return 0;
    if(!g_cepb){ u32* pb=(u32*)sa(0x1000,0x1000); u32* sem=(u32*)sa(0x1000,0x1000); if(!pb||!sem)return 0;
        u64 pv=va_take(0x1000,0x1000), sv=va_take(0x1000,0x1000);
        if(!nvm_map(&g_uv,pv,PA(pb),0x1000,1,0,0)||!nvm_map(&g_uv,sv,PA(sem),0x1000,1,0,0))return 0; nvm_tlb_flush(g_uv.pd3,0);
        g_cepb=pb; g_cesem=sem; g_cepb_va=pv; g_cesem_va=sv; }
    u32 seq=++g_ce_seq; if(!seq)seq=g_ce_seq=1; g_cesem[0]=0; cpu_mb();
    u32* pb=g_cepb; int n=0; u32 sc=4u<<13;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0000u>>2); pb[n++]=0xC5B5u;
    pb[n++]=(1u<<29)|(3u<<16)|sc|(0x0240u>>2); pb[n++]=(u32)(g_cesem_va>>32); pb[n++]=(u32)g_cesem_va; pb[n++]=seq;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0400u>>2); pb[n++]=(u32)(src>>32); pb[n++]=(u32)src; pb[n++]=(u32)(dst>>32); pb[n++]=(u32)dst;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0410u>>2); pb[n++]=spitch; pb[n++]=dpitch; pb[n++]=wbytes; pb[n++]=lines;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0300u>>2); pb[n++]=0x2u|0x4u|(1u<<3)|(1u<<7)|(1u<<8)|(1u<<9);
    cflush(pb,0x1000); ce_kick(g_cepb_va,n);
    u64 t0=hr_ns(); while(g_cesem[0]!=seq){ if(hr_ns()-t0>200000000ULL){ g_ce_timeouts++; return 0; } cpu_pause(); }
    g_ce_copies++; return 1; }
void nvgsp_ce_stats(u32* copies,u32* timeouts){ if(copies)*copies=g_ce_copies; if(timeouts)*timeouts=g_ce_timeouts; }
const char* nvgsp_gpu_name(void){ return G.name[0]?G.name:"NVIDIA GPU"; }

void nvgsp_status(void){
    static const char* st[]={"not started","booter ran","GSP-RM running","static info read, VRAM allocator ready","BAR1/BAR2 windows working","golden context ready","channels and engine objects created","copy engine verified"};
    nvl_s("NVIDIA GSP state: "); nvl_s(st[G.state<8?G.state:0]); nvl_nl();
    if(g_nv_why[0]){ nvl_s("last failure: step "); nvl_d((u64)g_nv_step); nvl_s(" - "); nvl_s(g_nv_why); nvl_nl(); }
    if(G.state>=3){ nvl_kv("VRAM allocator total",nvv_total()); nvl_kv("VRAM allocator free",nvv_free_bytes()); nvl_kv("VRAM allocations",(u64)nvv_count()); nvl_kv("GSP errors seen",G.err_count); } }

static void save_log(void){ extern int fs_save_bin(const char*,const u8*,int,int); fs_save_bin("nvgsp-log.txt",(const u8*)g_nv_log,g_nv_log_n,0); extern void nvx3_sync_all(void); nvx3_sync_all(); }
int nvgsp_cmd(const char* a){
    while(*a==' ')a++; int r=1;
    if(seq(a,"log")){ if(!g_nv_log_n){ nvl_s("no nvgsp log yet"); nvl_nl(); } return 1; }
    if(!a[0]||seq(a,"status")){ g_nv_log_n=0; g_nv_log[0]=0; nvgsp_status(); return 1; }
    nvl_reset(); nvl_s("nvgsp "); nvl_s(a); nvl_nl();
    if(G.state==0||seq(a,"vram")){ r=probe(); if(r<=0)goto out; }
    if(seq(a,"vram")){ u64 es=0x16A3350ULL; nv_fb_layout(es,0x1000); print_layout(); nvv_init(0,g_nvl.nonwpr_addr); nvl_kv("allocator span before GSP",nvv_total()); nvl_s("  (VRAM is not used before GSP-RM has scrubbed it; nvgsp boot hands the allocator the regions GSP reports)"); nvl_nl(); goto out; }
    if(seq(a,"boot")||seq(a,"all")){ r=boot_gsp(); if(r<=0)goto out; r=bars(); if(r<=0)goto out; }
    if(seq(a,"objects")||seq(a,"all")){ r=objects(); if(r<=0)goto out; }
    if(seq(a,"unload")){ r=unload(); goto out; }
    out: nvgsp_status(); save_log(); return r; }
