/* ================= NVMe driver ================= */
#define NVME_ASQ  0x00B30000u
#define NVME_ACQ  0x00B31000u
#define NVME_IOSQ 0x00B32000u
#define NVME_IOCQ 0x00B33000u
#define NVME_ID   0x00B34000u
#define NVME_BUF  0x00B35000u
static volatile u8* nvme_regs=0; static int nvme_ok=0; static u32 nvme_sectors=0, nvme_dstrd=0, nvme_nsid=1;
static u32 nasq_t=0, nacq_h=0, nacq_p=1, niosq_t=0, niocq_h=0, niocq_p=1, nvme_cid=1;
static void nvme_db(int qid,int is_cq,u32 val){ u32 off=0x1000u+(u32)((2*qid+(is_cq?1:0)))*(4u<<nvme_dstrd); *(volatile u32*)((u64)nvme_regs+off)=val; }
static int nvme_admin(u8 op,u32 nsid,u32 prp1,u32 prp2,u32 d10,u32 d11){
    volatile u32* e=(volatile u32*)((u64)NVME_ASQ+(u64)nasq_t*64); for(int i=0;i<16;i++)e[i]=0;
    u16 cid=(u16)nvme_cid++; e[0]=op|((u32)cid<<16); e[1]=nsid; e[6]=prp1; e[7]=0; e[8]=prp2; e[9]=0; e[10]=d10; e[11]=d11;
    nasq_t=(nasq_t+1)%64; nvme_db(0,0,nasq_t);
    for(int t=0;t<20000000;t++){ volatile u32* c=(volatile u32*)((u64)NVME_ACQ+(u64)nacq_h*16);
        if(((c[3]>>16)&1)==nacq_p){ u32 st=(c[3]>>17)&0x7FF; nacq_h=(nacq_h+1)%64; if(nacq_h==0)nacq_p^=1; nvme_db(0,1,nacq_h); return (int)st; } io_wait(); }
    return -1;
}
static int nvme_io(u8 op,u32 slba,u32 buf){
    volatile u32* e=(volatile u32*)((u64)NVME_IOSQ+(u64)niosq_t*64); for(int i=0;i<16;i++)e[i]=0;
    u16 cid=(u16)nvme_cid++; e[0]=op|((u32)cid<<16); e[1]=nvme_nsid; e[6]=buf; e[7]=0; e[8]=0; e[9]=0;
    e[10]=slba; e[11]=0; e[12]=0;   /* NLB = 0 => 1 block */
    niosq_t=(niosq_t+1)%64; nvme_db(1,0,niosq_t);
    for(int t=0;t<20000000;t++){ volatile u32* c=(volatile u32*)((u64)NVME_IOCQ+(u64)niocq_h*16);
        if(((c[3]>>16)&1)==niocq_p){ u32 st=(c[3]>>17)&0x7FF; niocq_h=(niocq_h+1)%64; if(niocq_h==0)niocq_p^=1; nvme_db(1,1,niocq_h); return (int)st; } io_wait(); }
    return -1;
}
static u32 nvme_ratio=1; static u32 nvme_cblk=0xFFFFFFFFu; static u32 g_nvme_bsz=512;
static int nvme_load(u32 blk){ if(blk==nvme_cblk)return 0; nvme_cblk=0xFFFFFFFFu; if(nvme_io(0x02,blk,NVME_BUF))return -1; nvme_cblk=blk; return 0; }
static int nvme_read1(u32 lba,u8* out){ if(!nvme_ok)return -1; u32 blk=lba/nvme_ratio, off=(lba%nvme_ratio)*512;
    if(nvme_load(blk))return -1; volatile u8* s=(volatile u8*)(u64)NVME_BUF+off; for(int i=0;i<512;i++)out[i]=s[i]; return 0; }
static int nvme_write1(u32 lba,const u8* in){ if(!nvme_ok)return -1; u32 blk=lba/nvme_ratio, off=(lba%nvme_ratio)*512;
    if(nvme_ratio>1){ if(nvme_load(blk))return -1; } else nvme_cblk=0xFFFFFFFFu;
    volatile u8* d=(volatile u8*)(u64)NVME_BUF+off; for(int i=0;i<512;i++)d[i]=in[i];
    if(nvme_io(0x01,blk,NVME_BUF)){ nvme_cblk=0xFFFFFFFFu; return -1; } if(nvme_ratio>1)nvme_cblk=blk; return 0; }
static int nvme_read(u32 lba,int cnt,u8* out){ for(int i=0;i<cnt;i++) if(nvme_read1(lba+i,out+i*512))return -1; return 0; }
static int nvme_found=0, nvme_failstage=0;   /* diagnostics */
static int nvme_init(void){
    nvme_ok=0; nvme_found=0; nvme_failstage=0;
    int bus,slot,fn; if(!pci_find(0x01,0x08,-1,&bus,&slot,&fn))return 0;   /* NVMe, any bus */
    nvme_found=1; nvme_failstage=1;
    u32 cmd=pci_cfg(bus,slot,fn,0x04); pci_cfgw(bus,slot,fn,0x04,cmd|0x06);
    u32 bar0=pci_cfg(bus,slot,fn,0x10), bar1=pci_cfg(bus,slot,fn,0x14);
    if(bar1!=0){ nvme_failstage=2; return 0; }             /* controller mapped >4GB */
    if(bar0&1){ nvme_failstage=3; return 0; } u32 base=bar0&0xFFFFFFF0u; if(base<0x1000u||base>=0xF0000000u){ nvme_failstage=4; return 0; }
    nvme_regs=(volatile u8*)(u64)base; nvme_failstage=5;
    unsigned long long cap=*(volatile u32*)((u64)nvme_regs+0x00)|((unsigned long long)*(volatile u32*)((u64)nvme_regs+0x04)<<32);
    nvme_dstrd=(u32)((cap>>32)&0xF);
    *(volatile u32*)((u64)nvme_regs+0x14)=0;              /* CC.EN=0 */
    for(int t=0;t<10000000;t++){ if(!(*(volatile u32*)((u64)nvme_regs+0x1C)&1))break; io_wait(); }   /* wait !RDY */
    { volatile u8* z=(volatile u8*)(u64)NVME_ASQ; for(int i=0;i<4096;i++)z[i]=0; z=(volatile u8*)(u64)NVME_ACQ; for(int i=0;i<1024;i++)z[i]=0;
      z=(volatile u8*)(u64)NVME_IOSQ; for(int i=0;i<4096;i++)z[i]=0; z=(volatile u8*)(u64)NVME_IOCQ; for(int i=0;i<1024;i++)z[i]=0; }
    nasq_t=0;nacq_h=0;nacq_p=1;niosq_t=0;niocq_h=0;niocq_p=1;nvme_cid=1;
    *(volatile u32*)((u64)nvme_regs+0x24)=0x003F003F;     /* AQA: admin CQ/SQ = 64 entries */
    *(volatile u32*)((u64)nvme_regs+0x28)=NVME_ASQ; *(volatile u32*)((u64)nvme_regs+0x2C)=0;   /* ASQ */
    *(volatile u32*)((u64)nvme_regs+0x30)=NVME_ACQ; *(volatile u32*)((u64)nvme_regs+0x34)=0;   /* ACQ */
    *(volatile u32*)((u64)nvme_regs+0x14)=(4u<<20)|(6u<<16)|1u;   /* CC: IOCQES=4,IOSQES=6,EN=1 */
    for(int t=0;t<10000000;t++){ if(*(volatile u32*)((u64)nvme_regs+0x1C)&1)break; io_wait(); }   /* wait RDY */
    if(!(*(volatile u32*)((u64)nvme_regs+0x1C)&1)){ nvme_failstage=6; return 0; }   /* controller never became ready */
    nvme_failstage=7;
    /* IDENTIFY namespace 1 (CNS=0) */
    if(nvme_admin(0x06,1,NVME_ID,0,0,0)!=0){ nvme_failstage=8; return 0; }
    { volatile u8* id=(volatile u8*)(u64)NVME_ID; unsigned long long nsze=0; for(int i=0;i<8;i++)nsze|=((unsigned long long)id[i]<<(8*i));
      u8 flbas=id[26]&0x0F; u8 lbads=id[128+flbas*4+2]; u32 bsz=(lbads>=9&&lbads<=12)?(1u<<lbads):512; g_nvme_bsz=bsz;
      u16 ms=(u16)(id[128+flbas*4]|(id[128+flbas*4+1]<<8)); if(ms){ g_nvme_bsz=0; nvme_failstage=11; return 0; }
      nvme_ratio=bsz/512; if(!nvme_ratio)nvme_ratio=1; nvme_cblk=0xFFFFFFFFu;
      /* express size in 512-byte sectors for the generic layer */
      unsigned long long sec512 = nsze * (bsz/512); nvme_sectors=(sec512>0xFFFFFFFFULL)?0xFFFFFFFFu:(u32)sec512;
    }
    /* CREATE I/O CQ (qid 1) */
    if(nvme_admin(0x05,0,NVME_IOCQ,0,((63u)<<16)|1u,0x1)!=0){ nvme_failstage=9; return 0; }
    /* CREATE I/O SQ (qid 1, points at CQ 1) */
    if(nvme_admin(0x01,0,NVME_IOSQ,0,((63u)<<16)|1u,(1u<<16)|0x1)!=0){ nvme_failstage=10; return 0; }
    nvme_nsid=1; nvme_ok=1; return 1;
}
/* ---- generic disk layer: routes to IDE / AHCI / NVMe ---- */
enum { DK_NONE, DK_IDE, DK_AHCI, DK_NVME };
static int g_dktype=DK_NONE; static u32 g_dksectors=0;
static const char* gdisk_name(void){ switch(g_dktype){ case DK_IDE:return "IDE/ATA"; case DK_AHCI:return "AHCI/SATA"; case DK_NVME:return "NVMe"; default:return "none"; } }
static int gdisk_read(u32 lba,int cnt,u8* buf){ if(g_dktype==DK_IDE)return ata_read(lba,cnt,buf); if(g_dktype==DK_AHCI)return ahci_read(lba,cnt,buf); if(g_dktype==DK_NVME)return nvme_read(lba,cnt,buf); return -1; }
static int gdisk_write(u32 lba,const u8* buf){ if(g_dktype==DK_IDE)return ata_write(lba,buf); if(g_dktype==DK_AHCI)return ahci_write1(lba,buf); if(g_dktype==DK_NVME)return nvme_write1(lba,buf); return -1; }
static void gdisk_init(void){    /* IDE first (already probed), else AHCI, else NVMe */
    if(g_ata_drv>=0){ g_dktype=DK_IDE; g_dksectors=g_disk_sectors; return; }
    if(ahci_init()){ g_dktype=DK_AHCI; g_dksectors=ahci_sectors; return; }
    if(nvme_init()){ g_dktype=DK_NVME; g_dksectors=nvme_sectors; return; }
    g_dktype=DK_NONE; g_dksectors=0;
}
static u32 dk_ratio(void){ return (g_dktype==DK_NVME&&g_nvme_bsz>512)?g_nvme_bsz/512:1u; }
static int g_gdisk_probed=0;
static void gdisk_ensure(void){ if(g_gdisk_probed)return; g_gdisk_probed=1; gdisk_init(); }   /* lazy: only when disks/installer is used */
/* ---- disk installer: C port of mkhybrid.py build_fat32 (pure logic; caller supplies img) ---- */extern const unsigned char _binary_inst_boot_bin_start[], _binary_inst_boot_bin_end[];
extern const unsigned char _binary_inst_stage2_bin_start[], _binary_inst_stage2_bin_end[];
extern const unsigned char _binary_inst_boot_cfg_start[], _binary_inst_boot_cfg_end[];
static void ins83(const char* nm,u8 out[11]){ for(int i=0;i<11;i++)out[i]=' '; int i=0,o=0;
    while(nm[i]&&nm[i]!='.'&&o<8){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[o++]=(u8)c; i++; }
    while(nm[i]&&nm[i]!='.')i++; if(nm[i]=='.'){ i++; int e=8; while(nm[i]&&e<11){ char c=nm[i]; if(c>='a'&&c<='z')c-=32; out[e++]=(u8)c; i++; } } }
