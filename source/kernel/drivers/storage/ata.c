/* ============================ ATA PIO + FAT16/32 (read-only) ============================ */
#define ATA_IO  0x1F0
#define ATA_CTL 0x3F6
static int g_ata_drv=-1;                                   /* 0=master 1=slave, -1 none */
static u32 g_disk_sectors=0;                               /* total sectors from IDENTIFY */
static int ata_poll(void){ for(int i=0;i<4000000;i++){ u8 s=inb(ATA_IO+7); if(s&0x01)return -1; if(s&0x20)return -1; if(!(s&0x80)&&(s&0x08))return 0; } return -1; }
static int ata_read(u32 lba,int count,u8* buf){
    if(g_ata_drv<0)return -1; if(count<=0)return -1;
    while(count>0){ int c=count>255?255:count;
        for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
        outb(ATA_IO+6,(u8)(0xE0|(g_ata_drv<<4)|((lba>>24)&0x0F)));
        outb(ATA_IO+1,0); outb(ATA_IO+2,(u8)c); outb(ATA_IO+3,(u8)(lba&0xFF)); outb(ATA_IO+4,(u8)((lba>>8)&0xFF)); outb(ATA_IO+5,(u8)((lba>>16)&0xFF));
        outb(ATA_IO+7,0x20);
        for(int s=0;s<c;s++){ if(ata_poll()<0)return -1; for(int w=0;w<256;w++){ u16 d=inw(ATA_IO); buf[w*2]=(u8)(d&0xFF); buf[w*2+1]=(u8)(d>>8); } buf+=512; }
        lba+=c; count-=c; }
    return 0;
}
static int ata_detect(void){
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
          g_disk_sectors = s48? (s48>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)s48) : s28; }
        g_ata_drv=drv; return 1;
    }
    g_ata_drv=-1; return 0;
}
/* write one 512-byte sector (LBA28) */
static int ata_write(u32 lba,const u8* buf){
    if(g_ata_drv<0)return -1;
    for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
    outb(ATA_IO+6,(u8)(0xE0|(g_ata_drv<<4)|((lba>>24)&0x0F)));
    outb(ATA_IO+1,0); outb(ATA_IO+2,1); outb(ATA_IO+3,(u8)(lba&0xFF)); outb(ATA_IO+4,(u8)((lba>>8)&0xFF)); outb(ATA_IO+5,(u8)((lba>>16)&0xFF));
    outb(ATA_IO+7,0x30);
    if(ata_poll()<0)return -1;
    for(int w=0;w<256;w++) outw(ATA_IO,(u16)(buf[w*2]|(buf[w*2+1]<<8)));
    outb(ATA_IO+7,0xE7);                                  /* flush cache */
    for(int i=0;i<4000000;i++){ if(!(inb(ATA_IO+7)&0x80))break; }
    return 0;
}
