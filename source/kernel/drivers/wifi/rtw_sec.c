/* ===== hardware security CAM (WPA2 key install), ported from rtw88 sec.c ===== */
static void wnd_setup(u32 m,const u8* bssid,const u8* mac,const u8* tk,int qos);
static void wnd_set_gtk(const u8* gtk,int keyid);
#define SEC_CMD_REG    0x670u
#define SEC_WRITE_REG  0x674u
#define SEC_CONFIG     0x680u
#define SEC_CMD_WRITE_ENABLE (1u<<16)
#define SEC_CMD_POLLING      (1u<<31)
#define SEC_CAM_ENTRY_SHIFT  3
/* hw_key_type: 0=none 1=WEP40 2=TKIP 4=AES(CCMP) */
static u8 g_gtk[32]; static int g_gtk_state=0, g_gtk_keyid=0, g_gtk_len=0;
static const char* wpa_gtk_status(void){ switch(g_gtk_state){ case 1: return "group key (GTK) installed"; case 2: return "no GTK in msg3"; case 3: return "GTK unwrap failed";
    case 4: return "msg3 MIC mismatch - GTK skipped"; case 5: return "group cipher is not CCMP - GTK skipped"; case 6: return "GTK key index 0 clashes with the pairwise slot - skipped"; default: return "GTK not processed"; } }
static void wpa_gtk_extract(const u8* eap,int el){
    g_gtk_state=0; g_gtk_len=0;
    int flen=4+((eap[2]<<8)|eap[3]); if(flen>el)flen=el; if(flen<99){ g_gtk_state=2; return; }
    static u8 cp[512]; if(flen>512){ g_gtk_state=2; return; } for(int i=0;i<flen;i++)cp[i]=eap[i]; for(int i=81;i<97;i++)cp[i]=0;
    u8 mic[20]; hmac_sha1(g_ptk,16,cp,flen,mic); for(int i=0;i<16;i++) if(mic[i]!=eap[81+i]){ g_gtk_state=4; return; }
    u16 ki=(u16)((eap[5]<<8)|eap[6]); int kdl=(eap[97]<<8)|eap[98]; if(kdl<=0||99+kdl>flen){ g_gtk_state=2; return; }
    static u8 kd[512]; int n=kdl;
    if(ki&(1u<<12)){ n=aes_key_unwrap(&g_ptk[16],eap+99,kdl,kd); if(n<0){ g_gtk_state=3; return; } }
    else for(int i=0;i<kdl;i++)kd[i]=eap[99+i];
    for(int p=0;p+2<=n;){ u8 t=kd[p], l=kd[p+1]; if(t==0xDD&&l==0)break;
        if(t==0xDD&&l>=6&&kd[p+2]==0x00&&kd[p+3]==0x0F&&kd[p+4]==0xAC&&kd[p+5]==1&&p+2+l<=n){
            g_gtk_keyid=kd[p+6]&3; g_gtk_len=l-6; if(g_gtk_len!=16){ g_gtk_state=5; return; }
            for(int i=0;i<16;i++)g_gtk[i]=kd[p+8+i]; g_gtk_state=1; return; }
        p+=2+l; }
    g_gtk_state=2;
}
static void rtw_sec_write_cam(u32 m,int hw_key_idx,int keyidx,int hw_key_type,
                              int group,const u8* addr6,const u8* key16){
    u32 write_cmd=SEC_CMD_WRITE_ENABLE|SEC_CMD_POLLING;
    u32 addr=(u32)hw_key_idx<<SEC_CAM_ENTRY_SHIFT;
    for(int i=7;i>=0;i--){
        u32 content;
        if(i==0){
            content=((u32)(keyidx&3)) | (((u32)hw_key_type&7)<<2) | ((u32)(group&1)<<6) |
                    (1u<<15) | ((u32)addr6[0]<<16) | ((u32)addr6[1]<<24);
        } else if(i==1){
            content=(u32)addr6[2] | ((u32)addr6[3]<<8) | ((u32)addr6[4]<<16) | ((u32)addr6[5]<<24);
        } else if(i==6||i==7){
            content=0;
        } else {
            int j=(i-2)<<2;
            content=(u32)key16[j] | ((u32)key16[j+1]<<8) | ((u32)key16[j+2]<<16) | ((u32)key16[j+3]<<24);
        }
        u32 command=write_cmd | (addr + (u32)i);
        rtw_w32(m,SEC_WRITE_REG,content);
        rtw_w32(m,SEC_CMD_REG,command);
        rtw_delay_us(100);
    }
}
static void rtw_sec_enable_engine(u32 m){
    u16 cr=rtw_r16(m,0x0100); cr|=(1u<<9); rtw_w16(m,0x0100,cr);   /* SEC_ENGINE_EN */
    u16 cfg=rtw_r16(m,SEC_CONFIG);
    cfg|=(1u<<2)|(1u<<3);                    /* TX_DEC_EN | RX_DEC_EN */
    cfg|=(1u<<0)|(1u<<1)|(1u<<6)|(1u<<7);    /* default-key search: TX/RX uni+bc use DK */
    rtw_w16(m,SEC_CONFIG,cfg);
}
static int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass){
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    { int z=1; for(int i=0;i<6;i++) if(mac[i])z=0; if(z&&rtw_efuse_valid){ for(int i=0;i<6;i++){mac[i]=rtw_logmap[0xD0+i]; rtw_w8(m,0x0610+i,mac[i]);} } }
    /* PMK = PBKDF2(pass, ssid, 4096, 32) */
    int slen=0; while(ssid[slen])slen++; int plen2=0; while(pass[plen2])plen2++;
    pbkdf2_sha1((const u8*)pass,plen2,(const u8*)ssid,slen,4096,g_pmk,32);
    for(int i=0;i<6;i++)rtw_w8(m,0x0618+i,ap->bssid[i]);
    u32 nt=rtw_r32(m,0x0100); nt=(nt&~0x30000u)|(2u<<16); rtw_w32(m,0x0100,nt);
    rtw_set_channel_2g(m,ap->ch?ap->ch:6);
    rtw_rx_ring_up(m); g_eap_stashlen=0;
    u8 f[256]; int fl; u8 body[256]; int blen=0;
    /* AUTH (open) */
    fl=0; f[fl++]=0xB0;f[fl++]=0x00;f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4);f[fl++]=(u8)(g_seq>>4);g_seq++;
    f[fl++]=0;f[fl++]=0; f[fl++]=1;f[fl++]=0; f[fl++]=0;f[fl++]=0;
    rtw_tx_mgmt(m,f,fl);
    if(!rtw_rx_wait_mgmt(m,0xB0,ap->bssid,body,&blen,900)) return 1;
    if(blen>=6 && (body[4]|(body[5]<<8))!=0) return 2;
    /* ASSOC with RSN IE */
    fl=0; f[fl++]=0x00;f[fl++]=0x00;f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i]; for(int i=0;i<6;i++)f[fl++]=mac[i]; for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4);f[fl++]=(u8)(g_seq>>4);g_seq++;
    f[fl++]=0x31;f[fl++]=0x00;   /* capability: ESS + Privacy + short preamble */
    f[fl++]=0x0A;f[fl++]=0x00;
    f[fl++]=0; f[fl++]=(u8)slen; for(int i=0;i<slen;i++)f[fl++]=(u8)ssid[i];
    f[fl++]=1; f[fl++]=8; f[fl++]=0x82;f[fl++]=0x84;f[fl++]=0x8B;f[fl++]=0x96;f[fl++]=0x0C;f[fl++]=0x12;f[fl++]=0x18;f[fl++]=0x24;
    { u8 rsn[32]; int rl=wpa_rsn_ie(rsn); for(int i=0;i<rl;i++)f[fl++]=rsn[i]; }
    rtw_tx_mgmt(m,f,fl);
    if(!rtw_rx_wait_mgmt(m,0x10,ap->bssid,body,&blen,900)) return 3;
    if(blen>=4 && (body[2]|(body[3]<<8))!=0) return 4;
    /* ---- 4-way handshake ---- */
    u8 eap[512];
    int el=rtw_rx_wait_eapol(m,ap->bssid,eap,512,1500);   /* msg 1: ANonce */
    if(el<95) return 5;
    /* EAPOL-Key: [0]=type(2) [1..2]=desc [5..6]=keyinfo [13..44]=nonce(actually offset 17) */
    /* 802.1X hdr: ver(1) type(1) len(2) = 4 bytes; then EAPOL-Key: desc_type(1) key_info(2) key_len(2) replay(8) key_nonce(32)... */
    const u8* kf=eap;                 /* eap points at 802.1X header */
    u8 anonce[32]; for(int i=0;i<32;i++)anonce[i]=kf[4+1+2+2+8+i];   /* nonce at 802.1X+ (1+2+2+8) */
    /* our SNonce (derive from tsc/rand) */
    u8 snonce[32]; csprng_bytes(snonce,32);
    wpa_derive_ptk(g_pmk,ap->bssid,mac,anonce,snonce,g_ptk);
    /* build msg 2: copy key_info (clear install/ack, set MIC+pairwise), our SNonce, RSN IE, MIC=HMAC-SHA1(KCK,frame) */
    u8 m2[128]; int i,ml=0;
    m2[ml++]=0x01; m2[ml++]=0x03;                     /* 802.1X ver=1 type=3(key) */
    int lenpos=ml; m2[ml++]=0; m2[ml++]=0;            /* length filled later */
    m2[ml++]=0x02;                                    /* desc type: RSN */
    u16 ki;
    ki = (1u<<3)|(1u<<8)|2u;   /* pairwise|MIC|keyver2(CCMP) */
    m2[ml++]=(u8)(ki>>8); m2[ml++]=(u8)ki;
    m2[ml++]=0x00; m2[ml++]=0x00;                     /* key length 0 in msg2 (spec) */
    for(i=0;i<8;i++)m2[ml++]=kf[4+1+2+2+i];           /* replay counter = echo msg1 */
    for(i=0;i<32;i++)m2[ml++]=snonce[i];              /* key nonce = SNonce */
    for(i=0;i<16;i++)m2[ml++]=0;                      /* key IV */
    for(i=0;i<8;i++)m2[ml++]=0;                       /* key RSC */
    for(i=0;i<8;i++)m2[ml++]=0;                       /* key ID */
    int micpos=ml; for(i=0;i<16;i++)m2[ml++]=0;       /* MIC (zeroed for calc) */
    u8 rsn[32]; int rl=wpa_rsn_ie(rsn);
    m2[ml++]=(u8)(rl>>8); m2[ml++]=(u8)rl;            /* key data length */
    for(i=0;i<rl;i++)m2[ml++]=rsn[i];                 /* key data = our RSN IE */
    int body2=ml-4; m2[lenpos]=(u8)(body2>>8); m2[lenpos+1]=(u8)body2;
    u8 mic[20]; hmac_sha1(g_ptk,16,m2,ml,mic);        /* KCK=ptk[0..15] */
    for(i=0;i<16;i++)m2[micpos+i]=mic[i];
    rtw_tx_data_eapol(m,ap->bssid,mac,m2,ml);
    /* msg 3 from AP (GTK) */
    el=rtw_rx_wait_eapol(m,ap->bssid,eap,512,1500);
    if(el<95) return 6;
    wpa_gtk_extract(eap,el);
    /* msg 4 ack: key_info pairwise|MIC|secure, no key data */
    u8 m4[128]; ml=0;
    m4[ml++]=0x01;m4[ml++]=0x03; int lp=ml; m4[ml++]=0;m4[ml++]=0;
    m4[ml++]=0x02;
    u16 ki4=(1u<<3)|(1u<<8)|(1u<<9)|2u;               /* pairwise|MIC|secure|keyver2 */
    m4[ml++]=(u8)(ki4>>8);m4[ml++]=(u8)ki4;
    m4[ml++]=0x00;m4[ml++]=0x10;
    for(i=0;i<8;i++)m4[ml++]=eap[4+1+2+2+i];          /* replay = echo msg3 */
    for(i=0;i<32;i++)m4[ml++]=0;                      /* nonce 0 */
    for(i=0;i<16;i++)m4[ml++]=0; for(i=0;i<8;i++)m4[ml++]=0; for(i=0;i<8;i++)m4[ml++]=0;
    int mp=ml; for(i=0;i<16;i++)m4[ml++]=0;
    m4[ml++]=0;m4[ml++]=0;                            /* key data len 0 */
    int b4=ml-4; m4[lp]=(u8)(b4>>8); m4[lp+1]=(u8)b4;
    hmac_sha1(g_ptk,16,m4,ml,mic); for(i=0;i<16;i++)m4[mp+i]=mic[i];
    rtw_tx_data_eapol(m,ap->bssid,mac,m4,ml);
    /* install the pairwise temporal key (TK = ptk[32..47]) into the hw security CAM (CCMP=4) */
    rtw_sec_enable_engine(m);
    rtw_sec_write_cam(m, 0 /*cam idx*/, 0 /*keyidx*/, 4 /*AES/CCMP*/, 0 /*pairwise*/, ap->bssid, &g_ptk[32]);
    if(g_gtk_state==1){ if(g_gtk_keyid>0&&g_gtk_keyid<4) rtw_sec_write_cam(m, g_gtk_keyid, g_gtk_keyid, 4, 1, ap->bssid, g_gtk); else g_gtk_state=6; }
    wnd_setup(m,ap->bssid,mac,&g_ptk[32],0);
    if(g_gtk_state==1) wnd_set_gtk(g_gtk,g_gtk_keyid);
    g_wpa_ok=1;
    return 0;   /* handshake complete - keys derived AND installed in hw */
}
static int rtw_h2c_send(u32 m,int box,u32 w0,u32 w1){
    u32 boxreg,boxex;
    switch(box){ case 0:boxreg=0x01D0;boxex=0x01F0;break; case 1:boxreg=0x01D4;boxex=0x01F4;break;
                 case 2:boxreg=0x01D8;boxex=0x01F8;break; default:boxreg=0x01DC;boxex=0x01FC;break; }
    int ok=0; for(int t=0;t<300;t++){ u8 st=rtw_r8(m,0x01CC); if(!((st>>box)&1)){ok=1;break;} rtw_delay_us(100); }
    if(!ok)return -1;
    rtw_w32(m,boxex,w1);
    rtw_w32(m,boxreg,w0);
    return 0;
}
static void rtw_phy_power_on(u32 m){
    u8 v=rtw_r8(m,0x0002);
    v|=(u8)(1u<<6); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v&=(u8)~((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    rtw_w8(m,0x001F,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w8(m,0x00EF,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)&~((1u<<28)|(1u<<29)));
}

static void rtw_wmac_init(u32 m){
    rtw_w8(m,0x0455,0x70);
    rtw_w8(m,0x045E,(u8)(rtw_r8(m,0x045E)|(1u<<2)));
    u16 pre=(u16)(0x1E4u|(1u<<11));
    rtw_w8(m,0x04E5,(u8)(pre&0xFF)); rtw_w8(m,0x04E6,(u8)(pre>>8));
    rtw_w32(m,0x04C8,0xFFu|(0x08u<<8)|(0x20u<<16)|(0x20u<<24));
    rtw_w16(m,0x04CE,(u16)(0x01u|(0x08u<<8)));
    rtw_w8(m,0x1448,0x06); rtw_w8(m,0x144A,0x06);
    rtw_w8(m,0x144C,0x06); rtw_w8(m,0x144E,0x06);
    rtw_w8(m,0x0480,(u8)(rtw_r8(m,0x0480)|(1u<<5)));
    rtw_w8(m,0x05B4,(u8)(rtw_r8(m,0x05B4)&~((1u<<4)|(1u<<5)|(1u<<6))));
    rtw_w16(m,0x0522,0);
    rtw_w8(m,0x051B,0x09);
    rtw_w8(m,0x0512,0x19);
    rtw_w32(m,0x0514,(0x0Au|(0x0Eu<<8)|(0x10u<<16)|(0x10u<<24)));
    rtw_w16(m,0x0502,0x186);
    rtw_w16(m,0x0506,0x3BC);
    rtw_w32(m,0x0544,(0x05u|(0x1Bu<<16)));
    rtw_w16(m,0x055E,(u16)(0x30u|(0x30u<<8)));
    rtw_w8(m,0x0550,(u8)(rtw_r8(m,0x0550)|(1u<<3)));
    rtw_w32(m,0x0540,(0x04u|(0x064u<<8)));
    rtw_w8(m,0x0558,0x04);
    rtw_w8(m,0x0559,0x02);
    rtw_w8(m,0x0521,(u8)(rtw_r8(m,0x0521)&~((1u<<12)>>8)));
    rtw_w16(m,0x06A0,0xFFFF);
    rtw_w16(m,0x06A2,0x0FFF);
    rtw_w16(m,0x06A4,0xFFFF);
    rtw_w32(m,0x0608,0xE400220Eu);
    rtw_w8(m,0x060C,(u8)(12288u>>9));
    rtw_w8(m,0x0606,0x30);
    rtw_w8(m,0x0605,0x30);
    rtw_w8(m,0x0639,0x40);
    rtw_w8(m,0x066D,(u8)(rtw_r8(m,0x066D)|(1u<<1)));
    rtw_w8(m,0x0718,(u8)(rtw_r8(m,0x0718)|(1u<<6)));
    rtw_w32(m,0x07D8,0xb0810041u);
    rtw_w8(m,0x07D4,0x98);
    rtw_w8(m,0x060F,4);
    rtw_w8(m,0x060C,4);
    rtw_w8(m,0x0290,(u8)(rtw_r8(m,0x0290)&~(1u<<2)));
    rtw_w32(m,0x0608,rtw_r32(m,0x0608)|(1u<<28));
}
static void rtw_phy_post(u32 m,int have_xtal,u8 xtal){
    if(have_xtal){
        u32 cap=(u32)(xtal&0x3Fu);
        u32 a=rtw_r32(m,0x0024); a=(a&~0x7E000000u)|((cap<<25)&0x7E000000u); rtw_w32(m,0x0024,a);
        u32 b2=rtw_r32(m,0x0028); b2=(b2&~0x0000007Eu)|((cap<<1)&0x0000007Eu); rtw_w32(m,0x0028,b2);
    }
    rtw_w32(m,0x0A2C,rtw_r32(m,0x0A2C)&~((1u<<18)|(1u<<22)));
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)|((1u<<28)|(1u<<29)));
}
static int rtw_apply_built(u32 m,const rtw_bent* t,int n,const char* lbl,int verify){
    char b[96],*p;
    p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] applying built-in table (");p=udec(p,(u32)n);p=scopy(p," writes)");*p=0;
    tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
    int mism=0,delays=0,rfw=0;
    for(int i=0;i<n;i++){
        u32 off=t[i].off, val=t[i].val; u8 sz=t[i].sz;
        int wasdelay=rtw_apply_one(m,off,sz,val);
        if(wasdelay){ delays++; continue; }
        if(sz==5){ rfw++; continue; }
        for(volatile int d=0;d<200;d++) io_wait();
        if(verify){ u32 rb=rtw_rdsz(m,off,sz);
            if(rb!=val){ mism++;
                if(mism<=8){ tcol(rgb(230,160,60));
                    p=b;p=scopy(p,"    0x");p=hx(p,off,4);p=scopy(p," wrote 0x");p=hx(p,val,8);p=scopy(p," read 0x");p=hx(p,rb,8);*p=0;
                    term_puts(b); term_putc('\n'); tcol(WHT); } } }
    }
    p=b;p=scopy(p,"  applied ");p=udec(p,(u32)n);
    if(rfw){p=scopy(p,"  rf ");p=udec(p,(u32)rfw);}
    if(delays){p=scopy(p,"  delays ");p=udec(p,(u32)delays);}
    if(verify){p=scopy(p,"  mismatch ");p=udec(p,(u32)mism);}
    *p=0;
    tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
    return mism;
}

static int rtw_read_table_file(const char* name,u8* out,int cap){
    if(msd_autoinit()){
        ufat_mount();
        if(ufat_ok){
            char up[16]; int k=0; while(name[k]&&k<15){ char c=name[k]; if(c>='a'&&c<='z')c-=32; up[k]=c; k++; } up[k]=0;
            for(int i=0;i<ufs_n;i++){
                char fn[16]; int j=0; while(ufs[i].name[j]&&j<15){ char c=ufs[i].name[j]; if(c>='a'&&c<='z')c-=32; fn[j]=c; j++; } fn[j]=0;
                int eq=1; for(int z=0;z<16;z++){ if(fn[z]!=up[z]){eq=0;break;} if(!fn[z]&&!up[z])break; }
                if(eq){ int r=ufat_read(i,(char*)out,cap); if(r>0)return r; }
            }
        }
    }
    return fat_read_file(name,out,cap);
}
static u8 fat_filebuf[65536];
static void fatls_cb(const u8* ent){ char nm[14]; int o=0; for(int i=0;i<8;i++) if(ent[i]!=' ')nm[o++]=(char)ent[i]; if(ent[8]!=' '){ nm[o++]='.'; for(int i=8;i<11;i++) if(ent[i]!=' ')nm[o++]=(char)ent[i]; } nm[o]=0;
    u32 sz=fr32(ent+28); int dir=(ent[11]&0x10)?1:0; char b[64],*p=b; p=scopy(p,"  "); p=scopy(p,nm); while((p-b)<26)*p++=' '; if(dir)p=scopy(p,"<DIR>"); else { p=udec(p,sz); p=scopy(p," B"); } *p=0; tcol(dir?ACC:WHT); term_puts(b); tcol(WHT); term_putc('\n'); }
