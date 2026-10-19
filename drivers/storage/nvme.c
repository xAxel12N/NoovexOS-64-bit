#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/drivers/storage/gdisk.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/lib/base/fmt.h"
/* ================= NVMe driver ================= */
#define NVME_ASQ  DMW(0x1B0000u)
#define NVME_ACQ  DMW(0x1B1000u)
#define NVME_IOSQ DMW(0x1B2000u)
#define NVME_IOCQ DMW(0x1B3000u)
#define NVME_ID   DMW(0x1B4000u)
#define NVME_BUF  DMW(0x1B5000u)
static volatile u8* nvme_regs=0; int nvme_ok=0; static u32 nvme_sectors=0, nvme_dstrd=0, nvme_nsid=1, nvme_q=64, nvme_to=10000000;
static u32 nasq_t=0, nacq_h=0, nacq_p=1, niosq_t=0, niocq_h=0, niocq_p=1, nvme_cid=1;
int nvme_bad=0, nvme_lasterr=0;
static void nvme_db(int qid,int is_cq,u32 val){ u32 off=0x1000u+(u32)((2*qid+(is_cq?1:0)))*(4u<<nvme_dstrd); *(volatile u32*)((u64)nvme_regs+off)=val; }
static u32 nvme_csts(void){ return *(volatile u32*)((u64)nvme_regs+0x1C); }
static int nvme_wait(u32 cqb,u32* h,u32* ph,int qid,u16 cid){
    for(int t=0;t<20000000;t++){ volatile u32* c=(volatile u32*)((u64)cqb+(u64)(*h)*16);
        if(((c[3]>>16)&1)==*ph){ u32 st=(c[3]>>17)&0x7FF; u16 ccid=(u16)(c[3]&0xFFFF); *h=(*h+1)%nvme_q; if(*h==0)*ph^=1; nvme_db(qid,1,*h); if(ccid==cid)return (int)st; continue; }
        if(nvme_csts()&2u){ nvme_bad=1; nvme_lasterr=2; return -1; }
        io_wait(); }
    nvme_bad=1; nvme_lasterr=1; return -1;
}
static u16 nvme_next_cid(void){ u16 c=(u16)nvme_cid++; if(nvme_cid>0xFFF0u)nvme_cid=1; return c; }
static int nvme_admin(u8 op,u32 nsid,u32 prp1,u32 prp2,u32 d10,u32 d11){
    volatile u32* e=(volatile u32*)((u64)NVME_ASQ+(u64)nasq_t*64); for(int i=0;i<16;i++)e[i]=0;
    u16 cid=nvme_next_cid(); e[0]=op|((u32)cid<<16); e[1]=nsid; e[6]=prp1; e[7]=0; e[8]=prp2; e[9]=0; e[10]=d10; e[11]=d11;
    nasq_t=(nasq_t+1)%nvme_q; nvme_db(0,0,nasq_t);
    return nvme_wait(NVME_ACQ,&nacq_h,&nacq_p,0,cid);
}
static int nvme_init(void);
static int nvme_cmd(u8 op,u32 slba,u32 buf){
    if(nvme_bad){ nvme_bad=0; if(!nvme_init())return -1; }
    volatile u32* e=(volatile u32*)((u64)NVME_IOSQ+(u64)niosq_t*64); for(int i=0;i<16;i++)e[i]=0;
    u16 cid=nvme_next_cid(); e[0]=op|((u32)cid<<16); e[1]=nvme_nsid; e[6]=buf; e[7]=0; e[8]=0; e[9]=0;
    e[10]=slba; e[11]=0; e[12]=0;
    niosq_t=(niosq_t+1)%nvme_q; nvme_db(1,0,niosq_t);
    return nvme_wait(NVME_IOCQ,&niocq_h,&niocq_p,1,cid);
}
static int nvme_io(u8 op,u32 slba,u32 buf){ return nvme_cmd(op,slba,buf); }
static u32 nvme_ratio=1; static u32 nvme_cblk=0xFFFFFFFFu; u32 g_nvme_bsz=512;
static int nvme_load(u32 blk){ if(blk==nvme_cblk)return 0; nvme_cblk=0xFFFFFFFFu; if(nvme_io(0x02,blk,NVME_BUF))return -1; nvme_cblk=blk; return 0; }
static int nvme_read1(u32 lba,u8* out){ if(!nvme_ok)return -1; u32 blk=lba/nvme_ratio, off=(lba%nvme_ratio)*512;
    if(nvme_load(blk))return -1; volatile u8* s=(volatile u8*)(u64)NVME_BUF+off; for(int i=0;i<512;i++)out[i]=s[i]; return 0; }
static int nvme_write1(u32 lba,const u8* in){ if(!nvme_ok)return -1; u32 blk=lba/nvme_ratio, off=(lba%nvme_ratio)*512;
    if(nvme_ratio>1){ if(nvme_load(blk))return -1; } else nvme_cblk=0xFFFFFFFFu;
    volatile u8* d=(volatile u8*)(u64)NVME_BUF+off; for(int i=0;i<512;i++)d[i]=in[i];
    if(nvme_io(0x01,blk,NVME_BUF)){ nvme_cblk=0xFFFFFFFFu; return -1; } if(nvme_ratio>1)nvme_cblk=blk; return 0; }
static int nvme_read(u32 lba,int cnt,u8* out){ for(int i=0;i<cnt;i++) if(nvme_read1(lba+i,out+i*512))return -1; return 0; }
static int nvme_flush(void){ if(!nvme_ok)return -1; return nvme_cmd(0x00,0,0)?-1:0; }
int nvme_found=0, nvme_failstage=0;
static int nvme_ident_ns(u32 ns,unsigned long long* nsze,u32* bsz,int* meta){
    if(nvme_admin(0x06,ns,NVME_ID,0,0,0)!=0)return -1;
    volatile u8* id=(volatile u8*)(u64)NVME_ID; unsigned long long n=0; for(int i=0;i<8;i++)n|=((unsigned long long)id[i]<<(8*i));
    u8 flbas=id[26]&0x0F; u8 lbads=id[128+flbas*4+2]; *bsz=(lbads>=9&&lbads<=12)?(1u<<lbads):0;
    *meta=(id[128+flbas*4]|(id[128+flbas*4+1]<<8))!=0; *nsze=n; return 0; }
static volatile u8* nv_ecam=0; static int nv_b,nv_s,nv_f;
const char* g_vmd_note="no Intel VMD controller";
u32 g_vmd_dev=0; u64 g_vmd_cfgbar=0;
static u32 nvc_r(int off){ if(nv_ecam)return *(volatile u32*)(nv_ecam+(off&0xFFC)); return pci_cfg(nv_b,nv_s,nv_f,off); }
static void nvc_w(int off,u32 v){ if(nv_ecam){ *(volatile u32*)(nv_ecam+(off&0xFFC))=v; return; } pci_cfgw(nv_b,nv_s,nv_f,off,v); }
int vmd_is(u32 id){ u16 v=(u16)id, d=(u16)(id>>16); if(v!=0x8086)return 0;
    static const u16 ids[]={0x9A0B,0x467F,0x4C3D,0xA77F,0x7D0B,0xAD0B,0xB60B,0xB06F,0xB07F,0x201D,0x28C0,0x1E8B,0x9A2B,0x09AB,0x4C3E,0xA70F,0x7E7F,0x77F0};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(ids[i]==d)return 1; return 0; }
int vmd_find_nvme(void){
    for(int bus=0;bus<256;bus++) for(int sl=0;sl<32;sl++) for(int fn=0;fn<8;fn++){
        u32 id=pci_cfg(bus,sl,fn,0); if((id&0xFFFF)==0xFFFF){ if(fn==0)break; continue; }
        u32 cls=pci_cfg(bus,sl,fn,8)>>8; if((cls>>8)!=0x0104||!vmd_is(id))continue;
        g_vmd_dev=id>>16; u32 cmd=pci_cfg(bus,sl,fn,4); pci_cfgw(bus,sl,fn,4,(cmd&0xFFFFu)|0x6u);
        u32 b0=pci_cfg(bus,sl,fn,0x10), b1=pci_cfg(bus,sl,fn,0x14); u64 cfg=(u64)(b0&0xFFFFFFF0u)|(((b0&6)==4)?((u64)b1<<32):0);
        u32 m0=pci_cfg(bus,sl,fn,0x18), m1=pci_cfg(bus,sl,fn,0x1C); u64 mem1=(u64)(m0&0xFFFFFFF0u)|(((m0&6)==4)?((u64)m1<<32):0);
        g_vmd_cfgbar=cfg; if(!cfg){ g_vmd_note="Intel VMD found but its config window is not set"; return 0; }
        extern u64 acpi_hi_map(u64); if(cfg>=0x100000000ULL&&!acpi_hi_map(cfg)){ g_vmd_note="Intel VMD config window could not be mapped"; return 0; }
        u32 vcap=pci_cfg(bus,sl,fn,0x40)&0xFFFF, vcfg=pci_cfg(bus,sl,fn,0x44)&0xFFFF; int bstart=0;
        if(vcap&1){ u32 r=(vcfg>>8)&3; bstart=r==1?128:r==2?224:0; }
        int nb=bstart==224?32:bstart==128?128:256; if(nb>64)nb=64;
        for(int rb=0;rb<nb;rb++) for(int d=0;d<32;d++) for(int f=0;f<8;f++){
            volatile u8* e=(volatile u8*)(cfg+((u64)rb<<20)+((u64)d<<15)+((u64)f<<12)); u32 vid=*(volatile u32*)e;
            if((vid&0xFFFF)==0xFFFF||vid==0){ if(f==0)break; continue; }
            u32 c=*(volatile u32*)(e+8)>>8; if(c!=0x010802)continue;
            u32 nb0=*(volatile u32*)(e+0x10);
            if(!(nb0&0xFFFFFFF0u)&&!*(volatile u32*)(e+0x14)){
                if(!mem1){ g_vmd_note="NVMe behind Intel VMD has no address and VMD has no memory window"; return 0; }
                for(int pb=0;pb<rb;pb++) for(int pd=0;pd<32;pd++){ volatile u8* q=(volatile u8*)(cfg+((u64)pb<<20)+((u64)pd<<15)); u32 pv=*(volatile u32*)q; if((pv&0xFFFF)==0xFFFF||!pv)continue;
                    u32 bn=*(volatile u32*)(q+0x18); if(((bn>>8)&0xFF)==(u32)(rb+bstart)){ u32 lo=(u32)(mem1>>20)&0xFFF; *(volatile u32*)(q+0x20)=(lo<<4)|(lo<<20); *(volatile u32*)(q+4)|=0x6u; } }
                *(volatile u32*)(e+0x10)=(u32)mem1|((nb0&6)); if((nb0&6)==4)*(volatile u32*)(e+0x14)=(u32)(mem1>>32);
                if(mem1>=0x100000000ULL)acpi_hi_map(mem1); }
            for(int pb=0;pb<rb;pb++) for(int pd=0;pd<32;pd++){ volatile u8* q=(volatile u8*)(cfg+((u64)pb<<20)+((u64)pd<<15)); u32 pv=*(volatile u32*)q; if((pv&0xFFFF)==0xFFFF||!pv)continue; if(((*(volatile u32*)(q+8))>>16)==0x0604)*(volatile u32*)(q+4)|=0x6u; }
            nv_ecam=e; g_vmd_note="NVMe disk found behind Intel VMD (RST)"; return 1; }
        g_vmd_note="Intel VMD (RST) found but no NVMe disk behind it"; return 0; }
    return 0; }
static int nvme_init(void){
    nvme_ok=0; nvme_found=0; nvme_failstage=0; nvme_bad=0;
    int bus,slot,fn; nv_ecam=0; if(pci_find(0x01,0x08,-1,&bus,&slot,&fn)){ nv_b=bus; nv_s=slot; nv_f=fn; } else if(!vmd_find_nvme())return 0;
    nvme_found=1; nvme_failstage=1;
    u32 cmd=nvc_r(0x04); nvc_w(0x04,(cmd|0x02)&~0x04u);
    u32 bar0=nvc_r(0x10), bar1=nvc_r(0x14);
    if(bar0&1){ nvme_failstage=3; return 0; }
    u64 base=bar0&0xFFFFFFF0u; if((bar0&6)==4)base|=(u64)bar1<<32;
    if(base>=0x100000000ULL){ extern u64 acpi_hi_map(u64); if(!acpi_hi_map(base)||((base+0x3FFFULL)>>30)!=(base>>30)){ nvme_failstage=2; return 0; } }
    else if(base<0x1000u||base>=0xFEC00000u){ nvme_failstage=4; return 0; }
    nvme_regs=(volatile u8*)base; nvme_failstage=5;
    unsigned long long cap=*(volatile u32*)((u64)nvme_regs+0x00)|((unsigned long long)*(volatile u32*)((u64)nvme_regs+0x04)<<32);
    nvme_dstrd=(u32)((cap>>32)&0xF);
    { u32 mq=(u32)(cap&0xFFFF)+1u; nvme_q=mq<64u?mq:64u; if(nvme_q<2u){ nvme_failstage=12; return 0; } }
    if((cap>>48)&0xF){ nvme_failstage=13; return 0; }
    { u32 to=(u32)((cap>>24)&0xFF); if(!to)to=1; nvme_to=to*500000u; if(nvme_to<1000000u)nvme_to=1000000u; if(nvme_to>60000000u)nvme_to=60000000u; }
    *(volatile u32*)((u64)nvme_regs+0x14)=0;
    for(u32 t=0;t<nvme_to;t++){ if(!(nvme_csts()&1))break; io_wait(); }
    if(nvme_csts()&1){ nvme_failstage=14; return 0; }
    nvc_w(0x04,cmd|0x06);
    { volatile u8* z=(volatile u8*)(u64)NVME_ASQ; for(int i=0;i<4096;i++)z[i]=0; z=(volatile u8*)(u64)NVME_ACQ; for(int i=0;i<4096;i++)z[i]=0;
      z=(volatile u8*)(u64)NVME_IOSQ; for(int i=0;i<4096;i++)z[i]=0; z=(volatile u8*)(u64)NVME_IOCQ; for(int i=0;i<4096;i++)z[i]=0; }
    nasq_t=0;nacq_h=0;nacq_p=1;niosq_t=0;niocq_h=0;niocq_p=1;nvme_cid=1;
    *(volatile u32*)((u64)nvme_regs+0x24)=((nvme_q-1u)<<16)|(nvme_q-1u);
    *(volatile u32*)((u64)nvme_regs+0x28)=NVME_ASQ; *(volatile u32*)((u64)nvme_regs+0x2C)=0;
    *(volatile u32*)((u64)nvme_regs+0x30)=NVME_ACQ; *(volatile u32*)((u64)nvme_regs+0x34)=0;
    *(volatile u32*)((u64)nvme_regs+0x14)=(4u<<20)|(6u<<16)|1u;
    for(u32 t=0;t<nvme_to;t++){ u32 s=nvme_csts(); if((s&1)||(s&2))break; io_wait(); }
    if(!(nvme_csts()&1)||(nvme_csts()&2)){ nvme_failstage=6; return 0; }
    nvme_failstage=7;
    unsigned long long nsze=0; u32 bsz=0; int meta=0; u32 ns=1;
    if(nvme_ident_ns(1,&nsze,&bsz,&meta)!=0||!nsze){
        if(nvme_admin(0x06,0,NVME_ID,0,2,0)!=0){ nvme_failstage=8; return 0; }
        ns=*(volatile u32*)(u64)NVME_ID; if(!ns||nvme_ident_ns(ns,&nsze,&bsz,&meta)!=0||!nsze){ nvme_failstage=8; return 0; } }
    if(!bsz){ g_nvme_bsz=0; nvme_failstage=15; return 0; }
    g_nvme_bsz=bsz; if(meta){ g_nvme_bsz=0; nvme_failstage=11; return 0; }
    nvme_ratio=bsz/512; if(!nvme_ratio)nvme_ratio=1; nvme_cblk=0xFFFFFFFFu;
    { unsigned long long sec512=nsze*(bsz/512); nvme_sectors=(sec512>0xFFFFFFFFULL)?0xFFFFFFFFu:(u32)sec512; }
    if(nvme_admin(0x05,0,NVME_IOCQ,0,((nvme_q-1u)<<16)|1u,0x1)!=0){ nvme_failstage=9; return 0; }
    if(nvme_admin(0x01,0,NVME_IOSQ,0,((nvme_q-1u)<<16)|1u,(1u<<16)|0x1)!=0){ nvme_failstage=10; return 0; }
    nvme_nsid=ns; nvme_ok=1; return 1;
}
/* ---- generic disk layer: routes to IDE / AHCI / NVMe ---- */

int g_dktype=DK_NONE; u32 g_dksectors=0;
static char g_dk_nm[24];
const char* gdisk_name(void){ switch(g_dktype){ case DK_IDE:{ extern int g_ata_drv; return g_ata_drv==1?"IDE slave":"IDE master"; } case DK_AHCI:{ extern int ahci_cur_port(void); char* q=scopy(g_dk_nm,"SATA port "); q=udec(q,(u32)ahci_cur_port()); *q=0; return g_dk_nm; } case DK_NVME:return "NVMe"; case DK_VIRTIO:return "VirtIO"; default:return "none"; } }
int gdisk_read(u32 lba,int cnt,u8* buf){ if(g_dktype==DK_IDE)return ata_read(lba,cnt,buf); if(g_dktype==DK_AHCI)return ahci_read(lba,cnt,buf); if(g_dktype==DK_NVME)return nvme_read(lba,cnt,buf); if(g_dktype==DK_VIRTIO)return virtio_blk_read(lba,cnt,buf); return -1; }

/* ---- buffer cache: LRU of recently-read 512-byte sectors ----
   Repeat reads of the same LBA hit RAM instead of the disk. Single-sector
   reads go through the cache; multi-sector reads bypass it (and invalidate
   any cached sector they overlap, so stale data is never returned). */
#define BC_N 64
static struct { u32 lba; u32 tick; int valid; u8 data[512]; } g_bc[BC_N];
static u32 g_bc_clock=0; unsigned long g_bc_hits=0, g_bc_miss=0;
static int bc_find(u32 lba){ for(int i=0;i<BC_N;i++) if(g_bc[i].valid&&g_bc[i].lba==lba)return i; return -1; }
static int bc_victim(void){ int v=0; u32 oldest=0xFFFFFFFFu; for(int i=0;i<BC_N;i++){ if(!g_bc[i].valid)return i; if(g_bc[i].tick<oldest){oldest=g_bc[i].tick;v=i;} } return v; }
void bc_invalidate(u32 lba,int cnt){ for(int i=0;i<BC_N;i++) if(g_bc[i].valid && g_bc[i].lba>=lba && g_bc[i].lba<lba+(u32)cnt) g_bc[i].valid=0; }
int gdisk_read_cached(u32 lba,int cnt,u8* buf){
    if(cnt!=1){ bc_invalidate(lba,cnt); return gdisk_read(lba,cnt,buf); }   /* multi-sector: bypass + invalidate */
    int hit=bc_find(lba);
    if(hit>=0){ for(int i=0;i<512;i++)buf[i]=g_bc[hit].data[i]; g_bc[hit].tick=++g_bc_clock; g_bc_hits++; return 0; }
    if(gdisk_read(lba,1,buf)) { g_bc_miss++; return -1; }
    int v=bc_victim(); g_bc[v].lba=lba; g_bc[v].valid=1; g_bc[v].tick=++g_bc_clock; for(int i=0;i<512;i++)g_bc[v].data[i]=buf[i];
    g_bc_miss++; return 0;
}
static void gdisk_write_through(u32 lba){ bc_invalidate(lba,1); }   /* call after any write so the cache stays coherent */

int gdisk_flush(void){ if(g_dktype==DK_NVME)return nvme_flush(); if(g_dktype==DK_AHCI)return ahci_flush(); if(g_dktype==DK_VIRTIO)return virtio_blk_flush(); return 0; }
int gdisk_write(u32 lba,const u8* buf){ if(g_dktype==DK_IDE)return ata_write(lba,buf); if(g_dktype==DK_AHCI)return ahci_write1(lba,buf); if(g_dktype==DK_NVME)return nvme_write1(lba,buf); if(g_dktype==DK_VIRTIO)return virtio_blk_write(lba,1,buf); return -1; }
static int nvme_writen(u32 lba,int cnt,const u8* in){
    while(cnt>0){
        if(nvme_ratio>1&&lba%nvme_ratio==0&&(u32)cnt>=nvme_ratio){ u32 blk=lba/nvme_ratio; volatile u8* d=(volatile u8*)(u64)NVME_BUF; for(u32 i=0;i<nvme_ratio*512u;i++)d[i]=in[i];
            nvme_cblk=0xFFFFFFFFu; if(nvme_io(0x01,blk,NVME_BUF))return -1; nvme_cblk=blk; lba+=nvme_ratio; in+=nvme_ratio*512u; cnt-=(int)nvme_ratio; continue; }
        if(nvme_write1(lba,in))return -1; lba++; in+=512; cnt--; }
    return 0;
}
int gdisk_writen(u32 lba,int cnt,const u8* buf){
    if(cnt<=0)return 0; bc_invalidate(lba,cnt);
    if(g_dktype==DK_IDE)return ata_writen(lba,cnt,buf);
    if(g_dktype==DK_NVME)return nvme_writen(lba,cnt,buf);
    if(g_dktype==DK_VIRTIO){ while(cnt>0){ int c=cnt>128?128:cnt; if(virtio_blk_write(lba,c,buf))return -1; lba+=(u32)c; buf+=c*512; cnt-=c; } return 0; }
    if(g_dktype==DK_AHCI){ if(!ahci_ok)return -1; while(cnt>0){ int c=cnt>128?128:cnt; volatile u8* d=(volatile u8*)(u64)AHCI_BUF; for(int i=0;i<c*512;i++)d[i]=buf[i]; if(ahci_cmd(lba,(u32)c,AHCI_BUF,1,0x35))return -1; lba+=(u32)c; buf+=c*512; cnt-=c; } return 0; }
    return -1;
}
int gdisk_readn(u32 lba,int cnt,u8* buf){
    bc_invalidate(lba,cnt);
    while(cnt>0){ int c=cnt>128?128:cnt; if(gdisk_read(lba,c,buf))return -1; lba+=(u32)c; buf+=c*512; cnt-=c; }
    return 0;
}
static int gdisk_has_nvx(void){ u8 m[4096]; if(!g_dksectors)return 0; if(g_dktype==DK_NVME&&!g_nvme_bsz)return 0; if(gdisk_read(0,1,m))return 0; if(m[510]!=0x55||m[511]!=0xAA)return 0;
    for(int e=0;e<4;e++){ const u8* pe=m+446+e*16; if(pe[4]==0x7F&&*(const u32*)(pe+12))return 1; } return 0; }
int g_dk_count=0, g_dk_cur=0; const char* g_dk_why=""; static int g_dk_ct[12]; static u32 g_dk_cs[12]; static int g_dk_cu[12];
extern int ata_count(void); extern int ata_unit(int k); extern u32 ata_unit_secs(int k); extern void ata_select(int k); extern int ahci_count(void); extern int ahci_select(int k); extern int ahci_unit_port(int k); extern u32 ahci_unit_sectors(int k);
static void gdisk_set(int i){ g_dktype=g_dk_ct[i]; g_dksectors=g_dk_cs[i]; g_dk_cur=i; if(g_dktype==DK_AHCI)ahci_select(g_dk_cu[i]); if(g_dktype==DK_IDE&&ata_count())ata_select(g_dk_cu[i]); for(int k=0;k<BC_N;k++)g_bc[k].valid=0; nvme_cblk=0xFFFFFFFFu; }
static void gdisk_init(void){
    int* ct=g_dk_ct; u32* cs=g_dk_cs; int n=0;
    if(g_ata_drv>=0){ int an=ata_count(); if(an<1)an=1; for(int k=0;k<an;k++){ ct[n]=DK_IDE; cs[n]=ata_count()?ata_unit_secs(k):g_disk_sectors; g_dk_cu[n]=k; n++; } }
    if(ahci_init()){ int an=ahci_count(); for(int k=0;k<an&&n<10;k++){ ct[n]=DK_AHCI; cs[n]=ahci_unit_sectors(k); g_dk_cu[n]=k; n++; } }
    if(nvme_init()){ ct[n]=DK_NVME; cs[n]=nvme_sectors; g_dk_cu[n]=0; n++; }
    { u64 vs=0; if(virtio_blk_probe(&vs)){ ct[n]=DK_VIRTIO; cs[n]=vs>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)vs; g_dk_cu[n]=0; n++; } }
    g_dk_count=n;
    for(int i=0;i<n;i++){ gdisk_set(i); if(gdisk_has_nvx()){ g_dk_why="disk with a NoovexOS partition"; return; } }
    if(n){ gdisk_set(0); g_dk_why=n>1?"first disk (no NoovexOS partition found)":"only disk"; return; }
    g_dktype=DK_NONE; g_dksectors=0; g_dk_why="no disk";
}
u32 dk_ratio(void){ return (g_dktype==DK_NVME&&g_nvme_bsz>512)?g_nvme_bsz/512:1u; }
int g_gdisk_probed=0;
void gdisk_ensure(void){ if(g_gdisk_probed)return; g_gdisk_probed=1; gdisk_init(); }   /* lazy: only when disks/installer is used */
/* ---- disk installer: C port of mkhybrid.py build_fat32 (pure logic; caller supplies img) ---- */extern const unsigned char _binary_inst_boot_bin_start[], _binary_inst_boot_bin_end[];
extern const unsigned char _binary_inst_stage2_bin_start[], _binary_inst_stage2_bin_end[];
extern const unsigned char _binary_inst_boot_cfg_start[], _binary_inst_boot_cfg_end[];
void ins83(const char* nm,u8 out[11]){ for(int i=0;i<11;i++)out[i]=' '; int i=0,o=0;
    while(nm[i]&&nm[i]!='.'&&o<8){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[o++]=(u8)c; i++; }
    while(nm[i]&&nm[i]!='.')i++; if(nm[i]=='.'){ i++; int e=8; while(nm[i]&&e<11){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[e++]=(u8)c; i++; } } }

int gdisk_count(void){ gdisk_ensure(); return g_dk_count; }
int gdisk_current(void){ return g_dk_cur; }
int gdisk_pick(int i){ gdisk_ensure(); if(i<0||i>=g_dk_count)return -1; if(i==g_dk_cur)return 0;
    gdisk_set(i); return 1; }
int gdisk_read_on(int i,u32 lba,int cnt,u8* buf){ gdisk_ensure(); if(i<0||i>=g_dk_count)return -1; int cur=g_dk_cur; int sw=(i!=cur); if(sw)gdisk_set(i); int r=gdisk_readn(lba,cnt,buf); if(sw&&cur>=0&&cur<g_dk_count)gdisk_set(cur); return r; }
u32 gdisk_ratio_of(int i){ gdisk_ensure(); if(i<0||i>=g_dk_count)return 1; return (g_dk_ct[i]==DK_NVME&&g_nvme_bsz>512)?g_nvme_bsz/512:1u; }
int gdisk_kind_of(int i){ gdisk_ensure(); return (i>=0&&i<g_dk_count)?g_dk_ct[i]:0; }
u32 gdisk_size_of(int i){ gdisk_ensure(); return (i>=0&&i<g_dk_count)?g_dk_cs[i]:0; }
const char* gdisk_name_of(int i){ static char b[24]; gdisk_ensure(); if(i<0||i>=g_dk_count)return "none";
    switch(g_dk_ct[i]){ case DK_IDE:return ata_unit(g_dk_cu[i])==1?"IDE slave":"IDE master"; case DK_AHCI:{ char* q=scopy(b,"SATA port "); q=udec(q,(u32)ahci_unit_port(g_dk_cu[i])); *q=0; return b; } case DK_NVME:return "NVMe"; case DK_VIRTIO:return "VirtIO"; default:return "disk"; } }
