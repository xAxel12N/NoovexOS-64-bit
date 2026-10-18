#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/storage/gdisk.h"
/* ================= AHCI (SATA) driver ================= */
#define AHCI_CLB DMW(0x180000u)
#define AHCI_FIS DMW(0x181000u)
#define AHCI_CT  DMW(0x182000u)

static volatile u32* ahci_abar=0; static volatile u32* ahci_port=0; int ahci_ok=0; u32 ahci_sectors=0;
int ahci_cmd(u32 lba,u32 count,u32 buf,int write,int atacmd){
    if(!ahci_port)return -1; volatile u32* P=ahci_port;
    P[0x10/4]=0xFFFFFFFFu;                                 /* clear PxIS */
    volatile u32* ch=(volatile u32*)(u64)AHCI_CLB;         /* command header slot 0 */
    if(count>360u)return -1;
    ch[0]=(5)|(write?(1u<<6):0)|((count?1u:0u)<<16); ch[1]=0; ch[2]=AHCI_CT; ch[3]=0;
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
u32 ahci_ports_list(u32* sig_ok){ if(!ahci_abar)return 0; u32 pi=ahci_abar[0x0C/4], m=0; for(int p=0;p<32;p++){ if(!(pi&(1u<<p)))continue; volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)p*0x80); if((P[0x28/4]&0x0F)!=3)continue; u32 sig=P[0x24/4]; if(sig!=0x00000101&&sig!=0)continue; m|=1u<<p; } if(sig_ok)*sig_ok=m; return m; }
int ahci_cur_port(void){ if(!ahci_abar||!ahci_port)return -1; return (int)(((u64)ahci_port-(u64)ahci_abar-0x100)/0x80); }
static void ahci_port_stop(volatile u32* P){ P[0x18/4]&=~1u; for(int t=0;t<1000000&&(P[0x18/4]&(1u<<15));t++){} P[0x18/4]&=~(1u<<4); for(int t=0;t<1000000&&(P[0x18/4]&(1u<<14));t++){} }
static void ahci_port_start(volatile u32* P){ P[0x00/4]=AHCI_CLB; P[0x04/4]=0; P[0x08/4]=AHCI_FIS; P[0x0C/4]=0; P[0x10/4]=0xFFFFFFFFu; P[0x18/4]|=(1u<<4); P[0x18/4]|=1u; }
static int ahci_cmd48(u64 lba,u32 count,int atacmd){
    if(!ahci_port)return -1; volatile u32* P=ahci_port; P[0x10/4]=0xFFFFFFFFu;
    volatile u32* ch=(volatile u32*)(u64)AHCI_CLB; ch[0]=5|((count?1u:0u)<<16); ch[1]=0; ch[2]=AHCI_CT; ch[3]=0;
    volatile u8* ct=(volatile u8*)(u64)AHCI_CT; for(int i=0;i<128;i++)ct[i]=0;
    volatile u32* prdt=(volatile u32*)(u64)(AHCI_CT+0x80); prdt[0]=AHCI_BUF; prdt[1]=0; prdt[2]=0; prdt[3]=(count?count*512u:512u)-1u;
    ct[0]=0x27; ct[1]=0x80; ct[2]=(u8)atacmd; ct[4]=(u8)lba; ct[5]=(u8)(lba>>8); ct[6]=(u8)(lba>>16); ct[7]=0x40; ct[8]=(u8)(lba>>24); ct[9]=(u8)(lba>>32); ct[10]=(u8)(lba>>40); ct[12]=(u8)count; ct[13]=(u8)(count>>8);
    for(int t=0;t<2000000;t++){ if(!(P[0x20/4]&(0x80|0x08)))break; }
    P[0x38/4]=1; for(int t=0;t<20000000;t++){ if(!(P[0x38/4]&1))break; if(P[0x10/4]&(1u<<30))return -1; }
    return (P[0x20/4]&1)?-1:0; }
int ahci_port_io(int port,u64 lba,u8* out,u64* sectors){
    if(!ahci_abar||port<0||port>31)return -1; volatile u32* old=ahci_port; volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)port*0x80);
    int sw=(P!=old); if(sw){ if(old)ahci_port_stop(old); ahci_port_stop(P); ahci_port_start(P); ahci_port=P; }
    int r; if(sectors){ r=ahci_cmd48(0,1,0xEC); if(!r){ volatile u16* id=(volatile u16*)(u64)AHCI_BUF; u64 s48=id[100]|((u64)id[101]<<16)|((u64)id[102]<<32)|((u64)id[103]<<48); *sectors=s48?s48:(id[60]|((u32)id[61]<<16)); } }
    else { r=ahci_cmd48(lba,1,0x25); if(!r){ volatile u8* s=(volatile u8*)(u64)AHCI_BUF; for(int i=0;i<512;i++)out[i]=s[i]; } }
    if(sw){ ahci_port_stop(P); ahci_port=old; if(old)ahci_port_start(old); }
    return r; }
int ahci_flush(void){ if(!ahci_ok)return -1; return ahci_cmd(0,0,AHCI_BUF,0,0xEA); }
int ahci_read(u32 lba,int cnt,u8* out){ if(!ahci_ok)return -1; if(ahci_cmd(lba,(u32)cnt,AHCI_BUF,0,0x25))return -1;
    volatile u8* s=(volatile u8*)(u64)AHCI_BUF; for(int i=0;i<cnt*512;i++)out[i]=s[i]; return 0; }
int ahci_write1(u32 lba,const u8* in){ if(!ahci_ok)return -1; volatile u8* d=(volatile u8*)(u64)AHCI_BUF;
    for(int i=0;i<512;i++)d[i]=in[i]; return ahci_cmd(lba,1,AHCI_BUF,1,0x35); }
static int ahci_pl[8]; static u32 ahci_ps[8]; static int ahci_pn=0;
int ahci_init(void){
    ahci_ok=0; ahci_pn=0;
    int bus,slot,fn; if(!pci_find(0x01,0x06,-1,&bus,&slot,&fn)&&!pci_find(0x01,0x04,-1,&bus,&slot,&fn))return 0;   /* SATA, any bus */
    { extern int vmd_is(u32); if(vmd_is(pci_cfg(bus,slot,fn,0)))return 0; }
    u32 cmd=pci_cfg(bus,slot,fn,0x04);
    u32 bar5=pci_cfg(bus,slot,fn,0x24); if(bar5&1)return 0; bar5&=0xFFFFFFF0u; if(bar5<0x1000u||bar5>=0xFEC00000u)return 0;
    pci_cfgw(bus,slot,fn,0x04,(cmd|0x02)&~0x04u);
    { u32 cls=pci_cfg(bus,slot,fn,8)>>16; if(cls==0x0104){ u32 vs=*(volatile u32*)(u64)(bar5+0x10); if(vs<0x00010000u||vs>0x00010301u)return 0; } }
    ahci_abar=(volatile u32*)(u64)bar5;
    ahci_abar[0x04/4]|=(1u<<31);                          /* GHC.AE = AHCI enable */
    u32 pi=ahci_abar[0x0C/4];
    for(int p=0;p<32;p++){ if(!(pi&(1u<<p)))continue; volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)p*0x80);
        P[0x18/4]&=~1u; for(int t=0;t<1000000&&(P[0x18/4]&(1u<<15));t++){}
        P[0x18/4]&=~(1u<<4); for(int t=0;t<1000000&&(P[0x18/4]&(1u<<14));t++){}
        P[0x14/4]=0; P[0x10/4]=0xFFFFFFFFu; }
    ahci_abar[0x04/4]&=~2u; ahci_abar[0x08/4]=0xFFFFFFFFu;
    pci_cfgw(bus,slot,fn,0x04,cmd|0x06);
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
            u32 sc = s48? (s48>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)s48) : s28; if(sc&&ahci_pn<8){ ahci_pl[ahci_pn]=p; ahci_ps[ahci_pn]=sc; ahci_pn++; } }
        ahci_port_stop(P); ahci_port=0;
    }
    if(!ahci_pn)return 0;
    { volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)ahci_pl[0]*0x80); ahci_port_stop(P); ahci_port_start(P); ahci_port=P; ahci_sectors=ahci_ps[0]; ahci_ok=1; }
    return ahci_pn;
}
int ahci_count(void){ return ahci_pn; }
int ahci_unit_port(int k){ return (k>=0&&k<ahci_pn)?ahci_pl[k]:-1; }
u32 ahci_unit_sectors(int k){ return (k>=0&&k<ahci_pn)?ahci_ps[k]:0; }
int ahci_select(int k){
    if(!ahci_abar||k<0||k>=ahci_pn)return -1; volatile u32* P=(volatile u32*)((u64)ahci_abar+0x100+(u32)ahci_pl[k]*0x80);
    if(P!=ahci_port){ if(ahci_port)ahci_port_stop(ahci_port); ahci_port_stop(P); ahci_port_start(P); ahci_port=P; }
    ahci_sectors=ahci_ps[k]; ahci_ok=1; return 0; }
