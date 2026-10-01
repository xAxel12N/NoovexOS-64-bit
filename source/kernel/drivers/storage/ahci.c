/* ---- storage: read-only AHCI IDENTIFY (SATA disks). All polls are bounded so a probe can never hang. ---- */
static u8 ahci_clb[1024] __attribute__((aligned(1024)));   /* command list (32 headers)   */
static u8 ahci_fis[256]  __attribute__((aligned(256)));    /* received FIS                */
static u8 ahci_ctb[256]  __attribute__((aligned(128)));    /* command table (FIS + PRDT)  */
static u8 ahci_idbuf[512]__attribute__((aligned(4)));      /* 512-byte IDENTIFY response  */
static int ahci_identify(volatile u32* abar,int port,char* model,u64* sectors){
    volatile u32* P=(volatile u32*)((u8*)abar + 0x100 + port*0x80);
    if((P[0x28>>2]&0x0F)!=3) return 0;                       /* PxSSTS.DET != device present */
    if(P[0x24>>2]!=0x00000101u) return 0;                   /* PxSIG: plain SATA disk only  */
    P[0x18>>2] &= ~((1u<<0)|(1u<<4));                        /* stop: clear ST, FRE          */
    for(int t=0;t<1000000;t++) if(!(P[0x18>>2]&((1u<<14)|(1u<<15)))) break;
    for(int i=0;i<1024;i++)ahci_clb[i]=0; for(int i=0;i<256;i++){ahci_fis[i]=0;ahci_ctb[i]=0;}
    P[0x00>>2]=(u32)(u64)ahci_clb; P[0x04>>2]=0;             /* PxCLB */
    P[0x08>>2]=(u32)(u64)ahci_fis; P[0x0C>>2]=0;             /* PxFB  */
    P[0x18>>2] |= (1u<<4); P[0x18>>2] |= (1u<<0);           /* FRE, then ST */
    u32* ch=(u32*)ahci_clb; ch[0]=5u|(1u<<16); ch[1]=0; ch[2]=(u32)(u64)ahci_ctb; ch[3]=0;  /* CFL=5, PRDTL=1 */
    u8* ctb=ahci_ctb; ctb[0]=0x27; ctb[1]=0x80; ctb[2]=0xEC; ctb[7]=0;   /* H2D, C=1, IDENTIFY DEVICE */
    u32* prdt=(u32*)(ctb+0x80); prdt[0]=(u32)(u64)ahci_idbuf; prdt[1]=0; prdt[2]=0; prdt[3]=511u;  /* DBC=511 */
    for(int t=0;t<1000000;t++) if(!(P[0x20>>2]&((1u<<7)|(1u<<3)))) break;   /* wait !BSY,!DRQ */
    P[0x10>>2]=0xFFFFFFFFu; P[0x38>>2]=1u;                   /* clear PxIS, issue slot 0     */
    int ok=0; for(int t=0;t<3000000;t++){ if(!(P[0x38>>2]&1u)){ ok=1; break; } if(P[0x10>>2]&(1u<<30)) break; }
    if(!ok||(P[0x20>>2]&0x01)) return 0;
    u16* id=(u16*)ahci_idbuf;
    for(int i=0;i<20;i++){ u16 w=id[27+i]; model[i*2]=(char)(w>>8); model[i*2+1]=(char)(w&0xFF); }
    model[40]=0; for(int i=39;i>=0&&(model[i]==' '||model[i]==0);i--)model[i]=0;
    u64 s48=(u64)id[100]|((u64)id[101]<<16)|((u64)id[102]<<32)|((u64)id[103]<<48);
    u32 s28=(u32)id[60]|((u32)id[61]<<16);
    *sectors = s48?s48:(u64)s28; return 1;
}
static void wcsr(u16 i,u32 v){ outl(nic_io+0x14,i); outl(nic_io+0x10,v); }
static u32  rcsr(u16 i){ outl(nic_io+0x14,i); return inl(nic_io+0x10); }
static void wbcr(u16 i,u32 v){ outl(nic_io+0x14,i); outl(nic_io+0x1C,v); }
static u32  rbcr(u16 i){ outl(nic_io+0x14,i); return inl(nic_io+0x1C); }
static int is_e1000(u16 d){ static const u16 E[]={0x100E,0x100F,0x1004,0x1010,0x1015,0x1017,0x1019,0x101A,0x1026,0x1027,0x1028,0x10D3,0x1013,0x1014,0x1076,0x1077,0x1078,0x1079,0x107A,0x107B,0}; for(int i=0;E[i];i++) if(E[i]==d) return 1; return 0; }
static void nic_detect(void){ nic_present=0;
    for(int bus=0;bus<4;bus++)for(int slot=0;slot<32;slot++){ u32 id=pci_cfg(bus,slot,0,0); if((id&0xFFFF)==0xFFFF)continue;   /* prefer PCnet - proven */
        if((id&0xFFFF)==0x1022 && (u16)(id>>16)==0x2000){ u32 bar0=pci_cfg(bus,slot,0,0x10); nic_io=(u16)(bar0&0xFFFC); nic_bus=bus; nic_dev=slot; nic_kind=0; nic_present=1; return; } }
    for(int bus=0;bus<4;bus++)for(int slot=0;slot<32;slot++){ u32 id=pci_cfg(bus,slot,0,0); if((id&0xFFFF)==0xFFFF)continue;   /* then e1000 family */
        if((id&0xFFFF)==0x8086 && is_e1000((u16)(id>>16))){ u32 bar0=pci_cfg(bus,slot,0,0x10); e1k_bar=bar0&0xFFFFFFF0u; nic_bus=bus; nic_dev=slot; nic_kind=1; nic_present=1; return; } }
}
static inline void e1w(u32 r,u32 v){ e1k[r>>2]=v; }
static inline u32  e1r(u32 r){ return e1k[r>>2]; }
static int e1000_up(void){
    e1w(0x00D8,0xFFFFFFFFu); e1r(0x00C0);                                   /* mask IRQs, clear ICR */
    e1w(0x0000, e1r(0x0000)|0x04000000u);                                   /* CTRL.RST */
    for(volatile int d=0;d<200000;d++); for(int t=0;t<200000&&(e1r(0x0000)&0x04000000u);t++);
    e1w(0x00D8,0xFFFFFFFFu); e1r(0x00C0);
    e1w(0x0000,(e1r(0x0000)|0x60u)&~0x88080000u);                           /* SLU|ASDE, clear PHY_RST/ILOS/LRST-ish */
    u32 ral=e1r(0x5400), rah=e1r(0x5404);
    my_mac[0]=(u8)ral; my_mac[1]=(u8)(ral>>8); my_mac[2]=(u8)(ral>>16); my_mac[3]=(u8)(ral>>24); my_mac[4]=(u8)rah; my_mac[5]=(u8)(rah>>8);
    if(((ral|rah)&0xFFFFFF)==0){ for(int w=0;w<3;w++){ e1w(0x0014,((u32)w<<8)|1u); u32 v=0; for(int t=0;t<200000;t++){ v=e1r(0x0014); if(v&0x10) break; } u16 dd=(u16)(v>>16); my_mac[w*2]=(u8)dd; my_mac[w*2+1]=(u8)(dd>>8); } }
    e1w(0x5400, my_mac[0]|((u32)my_mac[1]<<8)|((u32)my_mac[2]<<16)|((u32)my_mac[3]<<24));
    e1w(0x5404, my_mac[4]|((u32)my_mac[5]<<8)|0x80000000u);
    for(int i=0;i<128;i++) e1w(0x5200+i*4,0);
    for(int i=0;i<E1K_NRX;i++){ volatile u32* d=(u32*)(e1k_rxd+i*16); d[0]=A32(e1k_rxb)+i*2048; d[1]=0; d[2]=0; d[3]=0; }
    e1w(0x2800,A32(e1k_rxd)); e1w(0x2804,0); e1w(0x2808,E1K_NRX*16); e1w(0x2810,0); e1w(0x2818,E1K_NRX-1);
    e1w(0x0100, 0x02u|0x08u|0x10u|0x8000u|0x04000000u);                     /* RCTL: EN|UPE|MPE|BAM|SECRC, 2048B */
    for(int i=0;i<E1K_NTX;i++){ volatile u32* d=(u32*)(e1k_txd+i*16); d[0]=0;d[1]=0;d[2]=0;d[3]=1; }
    e1w(0x3800,A32(e1k_txd)); e1w(0x3804,0); e1w(0x3808,E1K_NTX*16); e1w(0x3810,0); e1w(0x3818,0);
    e1w(0x0410,0x0060200Au);                                                /* TIPG */
    e1w(0x0400, 0x02u|0x08u|(0x0Fu<<4)|(0x40u<<12));                        /* TCTL: EN|PSP|CT|COLD */
    net_ok=1;
    { char b[52],*p=b; p=scopy(p,"e1000 up  MAC "); for(int i=0;i<6;i++){ p=hx(p,my_mac[i],2); if(i<5)*p++=':'; } *p=0; netlog(b); }
    netlog("e1000 is from-spec, UNTESTED on real hw");
    return 1;
}
static void e1000_send(const u8* data,int len){
    if(len<60)len=60; if(len>2048)len=2048;
    volatile u32* d=(u32*)(e1k_txd+e1k_tx_i*16);
    for(int w=0;w<400000 && !(d[3]&0x01);w++);                              /* wait slot free (DD) */
    for(int i=0;i<len;i++)((volatile u8*)e1k_txb)[e1k_tx_i*2048+i]=data[i];
    d[0]=A32(e1k_txb)+e1k_tx_i*2048; d[1]=0; d[2]=(u32)(len&0xFFFF)|(0x0Bu<<24); d[3]=0;   /* EOP|IFCS|RS */
    e1k_tx_i=(e1k_tx_i+1)%E1K_NTX; e1w(0x3818,e1k_tx_i);                    /* TDT */
    for(int w=0;w<400000 && !(d[3]&0x01);w++);
}
static int e1000_recv(u8* out,int max){
    volatile u32* d=(u32*)(e1k_rxd+e1k_rx_i*16);
    if(!(d[3]&0x01)) return 0;                                              /* DD */
    int len=(int)(d[2]&0xFFFF); if(len>max)len=max;
    volatile u8* buf=e1k_rxb+e1k_rx_i*2048; for(int i=0;i<len;i++)out[i]=buf[i];
    d[3]=0;                                                                 /* clear status, hand back */
    e1w(0x2818,e1k_rx_i);                                                   /* RDT = this slot */
    e1k_rx_i=(e1k_rx_i+1)%E1K_NRX;
    return len;
}
static int nic_up_raw(void){
    if(nic_init)return net_ok;
    nic_init=1; nic_detect();
    if(!nic_present||(nic_kind==0&&!nic_io)){ netlog("no supported NIC (PCnet or e1000)"); net_ok=0; return 0; }
    u32 cmd=pci_cfg(nic_bus,nic_dev,0,0x04); pci_wcfg(nic_bus,nic_dev,0,0x04,cmd|0x07);   /* I/O + MEM + bus master */
    if(nic_kind==1){ e1k=(volatile u32*)(u64)e1k_bar; if(!e1k_bar){ netlog("e1000 BAR0 invalid"); net_ok=0; return 0; } return e1000_up(); }
    for(int i=0;i<6;i++) my_mac[i]=inb(nic_io+i);
    inl(nic_io+0x18); inw(nic_io+0x14);
    for(volatile int d=0;d<20000;d++);
    outl(nic_io+0x10,0);
    wcsr(0,0x04);
    wbcr(20,2);                                /* SWSTYLE 2 = 32-bit structures */
    wbcr(2, rbcr(2)|0x0002);
    volatile u8* ib=nic_initb;
    for(int i=0;i<28;i++)ib[i]=0;
    ib[0]=0x00; ib[1]=0x80; ib[2]=(3<<4); ib[3]=(1<<4);
    for(int i=0;i<6;i++)ib[4+i]=my_mac[i];
    *(volatile u32*)(ib+20)=A32(nic_rxd);
    *(volatile u32*)(ib+24)=A32(nic_txd);
    for(int i=0;i<RXR;i++){ volatile u32* d=(u32*)(nic_rxd+i*16); d[0]=A32(nic_rxb)+i*2048; d[1]=0x8000F800u; d[2]=0; d[3]=0; }
    for(int i=0;i<2;i++){ volatile u32* d=(u32*)(nic_txd+i*16); d[0]=A32(nic_txb); d[1]=0; d[2]=0; d[3]=0; }
    wcsr(1,A32(nic_initb)&0xFFFF); wcsr(2,(A32(nic_initb)>>16)&0xFFFF);
    wcsr(0,0x0001);
    int t=2000000; while(!(rcsr(0)&0x0100)&&t-->0);
    if(t<=0){ netlog("PCnet INIT timeout"); net_ok=0; return 0; }
    wcsr(0,0x0002);
    net_ok=1;
    { char b[52],*p=b; p=scopy(p,"PCnet up  MAC "); for(int i=0;i<6;i++){ p=hx(p,my_mac[i],2); if(i<5)*p++=':'; } *p=0; netlog(b); }
    { char b[52],*p=b; p=scopy(p,"link up - awaiting address"); *p=0; netlog(b); }
    return 1;
}
static void nic_send(const u8* data,int len){
    if(!net_ok)return; if(nic_kind==1){ e1000_send(data,len); return; }
    if(len<60)len=60;
    volatile u32* d=(u32*)(nic_txd+nic_tx_idx*16);
    for(int w=0;(d[1]&(1u<<31))&&w<300000;w++);
    for(int i=0;i<len;i++)((volatile u8*)nic_txb)[i]=data[i];
    d[0]=A32(nic_txb); d[2]=0;
    d[1]=(0xF000u|(((u32)(~len+1))&0xFFF))|(1u<<24)|(1u<<25)|(1u<<31);
    nic_tx_idx=(nic_tx_idx+1)&1;
    wcsr(0,0x000A);
    for(int w=0;(d[1]&(1u<<31))&&w<300000;w++);
}
static int nic_recv(u8* out,int max){
    if(nic_kind==1) return e1000_recv(out,max);
    volatile u32* d=(u32*)(nic_rxd+nic_rx_idx*16);
    if(d[1]&(1u<<31))return 0;
    int len=(int)(d[2]&0xFFF); if(len>max)len=max;
    volatile u8* buf=nic_rxb+nic_rx_idx*2048;
    for(int i=0;i<len;i++)out[i]=buf[i];
    d[2]=0; d[1]=0x8000F800u;
    nic_rx_idx=(nic_rx_idx+1)%RXR;
    return len;
}
/* drain any leftover RX packets from a previous connection (tolerates small gaps) */
static void nic_flush_rx(void){ u8 rx[1600]; int empt=0; for(int z=0;z<160 && empt<3;z++){ if(nic_recv(rx,1600)<=0)empt++; else empt=0; } }
/* PCnet: clear latched error/interrupt bits (MERR/MISS/BABL/...) and make sure RX is running.
   Recovers a chip that stalled after heavy back-to-back transfers. */
static void nic_kick(void){ if(nic_kind==0 && net_ok){ u16 c=rcsr(0); wcsr(0,(u16)((c&0x7F00)|0x0002)); } }
static const u8* nd_mymac(void); static int eth_hdr(u8* f,const u8* dst,u16 type){ for(int i=0;i<6;i++)f[i]=dst[i]; const u8* mm=nd_mymac(); for(int i=0;i<6;i++)f[6+i]=mm[i]; be16(f+12,type); return 14; }
static void nd_tx2(const u8*,int); static void arp_request(u32 tip){
    u8 f[60]; for(int i=0;i<60;i++)f[i]=0; static const u8 bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    int o=eth_hdr(f,bc,0x0806);
    be16(f+o,1); be16(f+o+2,0x0800); f[o+4]=6; f[o+5]=4; be16(f+o+6,1);
    { const u8* mm=nd_mymac(); for(int i=0;i<6;i++)f[o+8+i]=mm[i]; } be32(f+o+14,my_ip); be32(f+o+24,tip);
    nd_tx2(f,o+28);
}
static int net_poll(u8* rx,int max,int ticks){
    for(int k=0;k<ticks;k++){ u16 a=pit_read(); for(;;){ int n=nic_recv(rx,max); if(n>0)return n; u16 b=pit_read(); if(((u16)(a-b))>=1193)break; } }
    return 0;
}