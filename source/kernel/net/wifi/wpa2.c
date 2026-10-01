/* ===================== WPA2 4-way handshake ===================== */
static u8 g_ptk[48];      /* KCK(16)|KEK(16)|TK(16) */
static u8 g_pmk[32];
static int g_wpa_ok=0;

/* send an EAPOL-Key data frame to the AP (802.11 data + LLC/SNAP + EAPOL) */
static void rtw_tx_data_eapol(u32 m,const u8* bssid,const u8* mac,const u8* eapol,int elen){
    static u8* pkt=0; if(!pkt)pkt=(u8*)dma_alloc(2048); if(!pkt)return;
    u8 fr[512]; int fl=0;
    fr[fl++]=0x08; fr[fl++]=0x01;                 /* FC: data, ToDS=1 */
    fr[fl++]=0; fr[fl++]=0;                        /* duration */
    for(int i=0;i<6;i++)fr[fl++]=bssid[i];         /* addr1 = BSSID (RA) */
    for(int i=0;i<6;i++)fr[fl++]=mac[i];           /* addr2 = SA */
    for(int i=0;i<6;i++)fr[fl++]=bssid[i];         /* addr3 = DA (AP) */
    fr[fl++]=0; fr[fl++]=0;                        /* seq (hw fills) */
    /* LLC/SNAP for EAPOL (EtherType 0x888E) */
    fr[fl++]=0xAA; fr[fl++]=0xAA; fr[fl++]=0x03; fr[fl++]=0x00; fr[fl++]=0x00; fr[fl++]=0x00;
    fr[fl++]=0x88; fr[fl++]=0x8E;
    for(int i=0;i<elen;i++)fr[fl++]=eapol[i];
    u32 total=48+fl; for(u32 i=0;i<total;i++)pkt[i]=0;
    u32 w0=((u32)fl&0xFFFFu)|((48u&0xFFu)<<16)|(1u<<26);
    *(volatile u32*)(pkt+0)=w0;
    *(volatile u32*)(pkt+4)=((u32)0u<<8)|((u32)8u<<16);   /* QSEL=BE(0), RATE_ID=B */
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);            /* USE_RATE|DISDATAFB */
    *(volatile u32*)(pkt+16)=0x00u;                       /* 1M */
    *(volatile u32*)(pkt+32)=(1u<<15);                    /* EN_HWSEQ */
    for(int i=0;i<fl;i++)pkt[48+i]=fr[i];
    u32 i=g_beidx % g_benum; u8* bd=g_bering+i*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    __asm__ volatile("sfence":::"memory");
    rtw_w32(m,0x0328,(u32)(u64)g_bering); rtw_w16(m,0x0388,(u16)(g_benum&0x0FFF));
    g_beidx++; rtw_w16(m,0x03A8,(u16)(g_beidx&0x0FFF));
}

/* wait for an EAPOL-Key frame from the AP; returns body (the EAPOL payload) length */
static int rtw_rx_wait_eapol(u32 m,const u8* bssid,u8* out,int maxout,int ms){
    if(g_eap_stashlen>0){ int n=g_eap_stashlen; if(n>maxout)n=maxout; for(int c=0;c<n;c++)out[c]=g_eap_stash[c]; g_eap_stashlen=0; return n; }
    for(int t=0;t<ms*10;t++){
        int wp=rtw_rx_hw_wp(m);
        while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum)){
            u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*2048;
            u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
            u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=(rw0>>14)&1u;
            u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen; if(fe>2048u)fe=2048u;
            int got=0,elen=0;
            if(plen>=40 && !crc && hoff+32u<=fe){
                const u8* fr=base+hoff; u8 fc=fr[0];
                if((fc&0x0C)==0x08){                 /* data frame */
                    u32 hlen=24u; if(fc&0x80)hlen+=2u;   /* QoS data (subtype>=8) has 2-byte QoS ctrl */
                    /* optional 4-byte HT control if order bit set */
                    u8 fc2=fr[1]; if((fc&0x80)&&(fc2&0x80))hlen+=4u;
                    u32 llc=hoff+hlen;
                    if(llc+8u<=fe && base[llc]==0xAA && base[llc+1]==0xAA && base[llc+2]==0x03 &&
                       base[llc+6]==0x88 && base[llc+7]==0x8E){
                        int fromap=1; for(int k=0;k<6;k++) if(fr[10+k]!=bssid[k])fromap=0;  /* addr2=SA=BSSID */
                        if(fromap){ u32 ep=llc+8; elen=(int)(fe-ep); if(elen>maxout)elen=maxout;
                            for(int c=0;c<elen;c++)out[c]=base[ep+c]; got=1; }
                    }
                }
            }
            *(volatile u32*)(u64)base=0;
            *(volatile u16*)(u64)(g_rxring+ri*8)=2048; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
            g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0; rtw_w16(m,0x03B4,(u16)g_rxidx);
            if(got) return elen;
        }
        rtw_delay_us(100); if((t&7)==0)ui_pump();
    }
    return 0;
}

/* derive PTK: PRF-384(PMK, "Pairwise key expansion", min(AA,SPA)|max|min(ANonce,SNonce)|max) */
static void wpa_derive_ptk(const u8* pmk,const u8* aa,const u8* spa,const u8* anonce,const u8* snonce,u8* ptk){
    u8 data[76]; int p=0; int i;
    const u8 *a1=aa,*a2=spa; int c=0; for(i=0;i<6;i++){ if(aa[i]!=spa[i]){ c=aa[i]<spa[i]?-1:1; break; } }
    if(c>0){ a1=spa; a2=aa; }
    for(i=0;i<6;i++)data[p++]=a1[i];
    for(i=0;i<6;i++)data[p++]=a2[i];
    const u8 *n1=anonce,*n2=snonce; c=0; for(i=0;i<32;i++){ if(anonce[i]!=snonce[i]){ c=anonce[i]<snonce[i]?-1:1; break; } }
    if(c>0){ n1=snonce; n2=anonce; }
    for(i=0;i<32;i++)data[p++]=n1[i];
    for(i=0;i<32;i++)data[p++]=n2[i];
    wpa_prf(pmk,32,"Pairwise key expansion",data,76,ptk,48);
}

/* Build an RSN IE (WPA2-PSK, CCMP) into buf, return length */
static int wpa_rsn_ie(u8* b){
    int n=0;
    b[n++]=48; b[n++]=20;                 /* RSN IE, len 20 */
    b[n++]=0x01; b[n++]=0x00;             /* version 1 */
    b[n++]=0x00;b[n++]=0x0F;b[n++]=0xAC;b[n++]=0x04;  /* group: CCMP */
    b[n++]=0x01;b[n++]=0x00;              /* pairwise count 1 */
    b[n++]=0x00;b[n++]=0x0F;b[n++]=0xAC;b[n++]=0x04;  /* pairwise: CCMP */
    b[n++]=0x01;b[n++]=0x00;              /* akm count 1 */
    b[n++]=0x00;b[n++]=0x0F;b[n++]=0xAC;b[n++]=0x02;  /* akm: PSK */
    b[n++]=0x00;b[n++]=0x00;             /* RSN capabilities */
    return n;
}

