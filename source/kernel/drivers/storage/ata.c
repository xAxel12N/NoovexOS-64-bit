#include "include/nvx/base.h"
#include "include/nvx/drivers/storage/ata.h"
/* ============================ ATA PIO + FAT16/32 (read-only) ============================ */
#define ATA_IO  0x1F0
#define ATA_CTL 0x3F6
int g_ata_drv=-1;                                   /* 0=master 1=slave, -1 none */
u32 g_disk_sectors=0;                               /* total sectors from IDENTIFY */
static int ata_poll(void){ for(int i=0;i<4000000;i++){ u8 s=inb(ATA_IO+7); if(s&0x01)return -1; if(s&0x20)return -1; if(!(s&0x80)&&(s&0x08))return 0; } return -1; }
static void ata_setup(u32 lba,int c,int write){
    if((u64)lba+(u64)c<=0x0FFFFFFFULL){ outb(ATA_IO+6,(u8)(0xE0|(g_ata_drv<<4)|((lba>>24)&0x0F)));
        outb(ATA_IO+1,0); outb(ATA_IO+2,(u8)c); outb(ATA_IO+3,(u8)(lba&0xFF)); outb(ATA_IO+4,(u8)((lba>>8)&0xFF)); outb(ATA_IO+5,(u8)((lba>>16)&0xFF));
        outb(ATA_IO+7,write?0x30:0x20); return; }
    outb(ATA_IO+6,(u8)(0x40|(g_ata_drv<<4)));
    outb(ATA_IO+1,0); outb(ATA_IO+2,(u8)((c>>8)&0xFF)); outb(ATA_IO+3,(u8)((lba>>24)&0xFF)); outb(ATA_IO+4,0); outb(ATA_IO+5,0);
    outb(ATA_IO+1,0); outb(ATA_IO+2,(u8)(c&0xFF)); outb(ATA_IO+3,(u8)(lba&0xFF)); outb(ATA_IO+4,(u8)((lba>>8)&0xFF)); outb(ATA_IO+5,(u8)((lba>>16)&0xFF));
    outb(ATA_IO+7,write?0x34:0x24); }
int ata_read(u32 lba,int count,u8* buf){
    if(g_ata_drv<0)return -1; if(count<=0)return -1;
    while(count>0){ int c=count>255?255:count;
        for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
        ata_setup(lba,c,0);
        for(int s=0;s<c;s++){ if(ata_poll()<0)return -1; for(int w=0;w<256;w++){ u16 d=inw(ATA_IO); buf[w*2]=(u8)(d&0xFF); buf[w*2+1]=(u8)(d>>8); } buf+=512; }
        lba+=c; count-=c; }
    return 0;
}
static int ata_u[2], ata_n=0; static u32 ata_s[2];
int ata_detect(void){
    ata_n=0;
    for(int drv=0;drv<2;drv++){
        outb(ATA_IO+6,(u8)(0xA0|(drv<<4))); io_wait(); io_wait();
        outb(ATA_IO+2,0); outb(ATA_IO+3,0); outb(ATA_IO+4,0); outb(ATA_IO+5,0);
        outb(ATA_IO+7,0xEC);
        u8 st=inb(ATA_IO+7); if(st==0||st==0xFF)continue;
        int ok=1; for(int i=0;i<2000000;i++){ st=inb(ATA_IO+7); if(!(st&0x80))break; if(i>1999998)ok=0; }
        if(!ok)continue; if(inb(ATA_IO+4)||inb(ATA_IO+5))continue;          /* ATAPI/other */
        for(int i=0;i<2000000;i++){ st=inb(ATA_IO+7); if(st&0x01){ok=0;break;} if(st&0x08)break; }
        if(!ok)continue; u16 idw[256]; for(int w=0;w<256;w++)idw[w]=inw(ATA_IO);  /* capture IDENTIFY */
        { u32 s28=(u32)idw[60]|((u32)idw[61]<<16); u64 s48=(u64)idw[100]|((u64)idw[101]<<16)|((u64)idw[102]<<32)|((u64)idw[103]<<48);
          u32 sc = s48? (s48>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)s48) : s28; ata_u[ata_n]=drv; ata_s[ata_n]=sc; ata_n++; }
    }
    if(!ata_n){ g_ata_drv=-1; return 0; }
    g_ata_drv=ata_u[0]; g_disk_sectors=ata_s[0]; return 1;
}
int ata_count(void){ return ata_n; }
int ata_unit(int k){ return (k>=0&&k<ata_n)?ata_u[k]:-1; }
u32 ata_unit_secs(int k){ return (k>=0&&k<ata_n)?ata_s[k]:0; }
void ata_select(int k){ if(k>=0&&k<ata_n){ g_ata_drv=ata_u[k]; g_disk_sectors=ata_s[k]; outb(ATA_IO+6,(u8)(0xA0|(g_ata_drv<<4))); io_wait(); io_wait(); } }
/* write one 512-byte sector (LBA28) */
int ata_write(u32 lba,const u8* buf){
    if(g_ata_drv<0)return -1;
    for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
    ata_setup(lba,1,1);
    if(ata_poll()<0)return -1;
    for(int w=0;w<256;w++) outw(ATA_IO,(u16)(buf[w*2]|(buf[w*2+1]<<8)));
    outb(ATA_IO+7,0xE7);                                  /* flush cache */
    for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
    return 0;
}
int ata_writen(u32 lba,int count,const u8* buf){
    if(g_ata_drv<0||count<=0)return -1;
    while(count>0){ int c=count>255?255:count;
        for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
        ata_setup(lba,c,1);
        for(int s=0;s<c;s++){ if(ata_poll()<0)return -1; for(int w=0;w<256;w++) outw(ATA_IO,(u16)(buf[w*2]|(buf[w*2+1]<<8))); buf+=512; }
        for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
        lba+=(u32)c; count-=c; }
    outb(ATA_IO+7,0xE7);
    for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
    return 0;
}
