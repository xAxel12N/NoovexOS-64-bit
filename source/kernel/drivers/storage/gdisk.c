/* ================= AHCI (SATA) driver ================= */
#define AHCI_CLB 0x00B00000u
#define AHCI_FIS 0x00B01000u
#define AHCI_CT  0x00B02000u
#define AHCI_BUF 0x00B03000u
static volatile u32* ahci_abar=0; static volatile u32* ahci_port=0; static int ahci_ok=0; static u32 ahci_sectors=0;
static int ahci_cmd(u32 lba,u32 count,u32 buf,int write,int atacmd){
    if(!ahci_port)return -1; volatile u32* P=ahci_port;
    P[0x10/4]=0xFFFFFFFFu;                                 /* clear PxIS */
    volatile u32* ch=(volatile u32*)(u64)AHCI_CLB;         /* command header slot 0 */
    ch[0]=(5)|(write?(1u<<6):0)|(1u<<16); ch[1]=0; ch[2]=AHCI_CT; ch[3]=0;   /* CFL=5,W,PRDTL=1 */
    volatile u8* ct=(volatile u8*)(u64)AHCI_CT; for(int i=0;i<128;i++)ct[i]=0;
    volatile u32* prdt=(volatile u32*)(u64)(AHCI_CT+0x80); prdt[0]=buf; prdt[1]=0; prdt[2]=0; prdt[3]=(count*512u-1u);
    volatile u8* fis=(volatile u8*)(u64)AHCI_CT;
    fis[0]=0x27; fis[1]=0x80; fis[2]=(u8)atacmd; fis[3]=0;
    fis[4]=(u8)(lba&0xFF); fis[5]=(u8)((lba>>8)&0xFF); fis[6]=(u8)((lba>>16)&0xFF); fis[7]=0x40;
    fis[8]=(u8)((lba>>24)&0xFF); fis[9]=0; fis[10]=0; fis[11]=0;
    fis[12]=(u8)(count&0xFF); fis[13]=(u8)((count>>8)&0xFF);
    for(int t=0;t<2000000;t++){ if(!(P[0x20/4]&(0x80|0x08)))break; }   /* wait BSY|DRQ clear */
    P[0x38/4]=1;                                                        /* issue slot 0 (PxCI) */
    for(int t=0;t<20000000;t++){ if(!(P[0x38/4]&1))break; if(P[0x10/4]&(1u<<30))return -1; }
    if(P[0x20/4]&0x01)return -1;
    return 0;
}
static int ahci_read(u32 lba,int cnt,u8* out){ if(!ahci_ok)return -1; if(ahci_cmd(lba,(u32)cnt,AHCI_BUF,0,0x25))return -1;
    volatile u8* s=(volatile u8*)(u64)AHCI_BUF; for(int i=0;i<cnt*512;i++)out[i]=s[i]; return 0; }
static int ahci_write1(u32 lba,const u8* in){ if(!ahci_ok)return -1; volatile u8* d=(volatile u8*)(u64)AHCI_BUF;
    for(int i=0;i<512;i++)d[i]=in[i]; return ahci_cmd(lba,1,AHCI_BUF,1,0x35); }
static int ahci_init(void){
    ahci_ok=0;
    int bus,slot,fn; if(!pci_find(0x01,0x06,-1,&bus,&slot,&fn))return 0;   /* SATA, any bus */
    u32 cmd=pci_cfg(bus,slot,fn,0x04); pci_cfgw(bus,slot,fn,0x04,cmd|0x06);
    u32 bar5=pci_cfg(bus,slot,fn,0x24); if(bar5&1)return 0; bar5&=0xFFFFFFF0u; if(bar5<0x1000u||bar5>=0xF0000000u)return 0;
    ahci_abar=(volatile u32*)(u64)bar5;
    ahci_abar[0x04/4]|=(1u<<31);                          /* GHC.AE = AHCI enable */
    u32 pi=ahci_abar[0x0C/4];
    for(int p=0;p<32;p++){ if(!(pi&(1u<<p)))continue;
        volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)p*0x80);
        u32 ssts=P[0x28/4]; if((ssts&0x0F)!=3)continue;    /* device present + PHY comm */
        u32 sig=P[0x24/4]; if(sig!=0x00000101 && sig!=0)continue;   /* SATA drive */
        /* stop the port */
        P[0x18/4]&=~(1u|(1u<<4)); for(int t=0;t<1000000;t++){ if(!(P[0x18/4]&((1u<<14)|(1u<<15))))break; }
        P[0x00/4]=AHCI_CLB; P[0x04/4]=0; P[0x08/4]=AHCI_FIS; P[0x0C/4]=0;
        { volatile u8* z=(volatile u8*)(u64)AHCI_CLB; for(int i=0;i<1024;i++)z[i]=0; z=(volatile u8*)(u64)AHCI_FIS; for(int i=0;i<256;i++)z[i]=0; }
        P[0x18/4]|=(1u<<4); P[0x18/4]|=1u;                 /* FRE then ST */
        ahci_port=P;
        /* IDENTIFY (0xEC) */
        if(ahci_cmd(0,1,AHCI_BUF,0,0xEC)==0){ volatile u16* id=(volatile u16*)(u64)AHCI_BUF;
            u32 s28=id[60]|((u32)id[61]<<16); unsigned long long s48=id[100]|((unsigned long long)id[101]<<16)|((unsigned long long)id[102]<<32);
            ahci_sectors = s48? (s48>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)s48) : s28; ahci_ok=1; return 1; }
    }
    return 0;
}
