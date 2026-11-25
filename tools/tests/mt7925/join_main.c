#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wpa_sm.h"
extern int mt7921_sae_fail; struct mt7921_softc g_mt7921,g_mt7925; void (*mt7921_rx_deliver)(const u8*,u32,int); int mt7921_tx_token_release(struct mt7921_softc* sc,u32 id){ (void)sc;(void)id; return 0; }
u64 hr_ns(void){ static u64 t; return t+=1000000ull; }
int regdb_ch_allowed(u8 ch){ (void)ch; return 1; } int regdb_is_dfs(u8 ch){ (void)ch; return 0; } int regdb_no_ir(u8 ch){ (void)ch; return 0; } const char* regdb_country(void){ return "SE"; }
static const u8 AP[6]={0x02,0x11,0x22,0x33,0x44,0x55},ME[6]={0x00,0x0c,0xe7,0x25,0x79,0x01};
struct cmd { u8 uni,cid; u32 n; u8 p[256]; };
static struct cmd cmds[64]; static int ncmd;
static u8 txf[16][512]; static u32 txn[16]; static int ntx;
static int pend_mcu,pend_seq,pend_cid,pend_auth,pend_assoc,sae_mode,sae_calls,eht_refuse,last_eht,eht_seen,he6_seen;
static int find(int cid,int from){ for(int i=from;i<ncmd;i++)if(cmds[i].uni&&cmds[i].cid==cid)return i; return -1; }
static const u8* tlv(const struct cmd* c,u32 hdr,u16 tag){ for(u32 o=hdr;o+4<=c->n;){ u16 t=mt_get16(c->p+o),l=mt_get16(c->p+o+2); if(t==tag)return c->p+o; if(l<4)return 0; o+=l; } return 0; }
static void inject(const u8* f,u32 fl,int rcpi){
    u8 b[600]; memset(b,0,64); u32 n=32+16+fl;
    mt_put32(b,n|((u32)PKT_TYPE_NORMAL<<27)); mt_put32(b+4,MT_WCID_AP|(1u<<18)); mt_put32(b+12,36u<<8); mt_put32(b+32+12,(u32)rcpi);
    memcpy(b+48,f,fl); mt7921_rx_data(&g_mt7925,b,n);
}
void mt7921_rx_poll(struct mt7921_softc* sc){
    if(pend_mcu){ pend_mcu=0; u8 ev[128]; memset(ev,0,sizeof ev); u32 n=MT7925_RXD_SIZE; ev[n]=(u8)pend_cid; n+=8;
        mt_put32(ev,n|(7u<<27)); ev[36]=MCU_UNI_EVENT_RESULT; ev[37]=(u8)pend_seq; mt7921_mcu_rx_event(sc,ev,n); return; }
    if(pend_auth){ pend_auth=0; u8 f[30]; memset(f,0,30); f[0]=0xB0; memcpy(f+4,ME,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); f[26]=2; if(sae_mode){ f[24]=3; f[26]=(u8)sae_mode; if(sae_mode>2)return; sae_mode++; } inject(f,30,180); return; }
    if(pend_assoc){ pend_assoc=0; u8 f[30]; memset(f,0,30); f[0]=0x10; memcpy(f+4,ME,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); f[24]=1; if(eht_refuse&&last_eht)f[26]=1; mt_put16(f+28,0xc005); inject(f,30,180); return; }
}
int mt7921_dma_tx(struct mt7921_softc* sc,int q,const u8* b,u32 n){ (void)sc;
    if(q==MT_Q_MCU){ assert(ncmd<64); struct cmd* c=&cmds[ncmd++]; u32 d1=mt_get32(b+4); (void)d1;
        const struct mt_mcu_uni_txd* u=(const struct mt_mcu_uni_txd*)b; c->uni=u->pkt_type==0xa0&&n>=48&&u->s2d_index==MCU_S2D_H2N;
        c->uni=(u->option&2)!=0; c->cid=(u8)u->cid; c->n=n-48; memcpy(c->p,b+48,c->n>256?256:c->n);
        if(u->option&1){ pend_mcu=1; pend_seq=u->seq; pend_cid=u->cid; } return 0; }
    assert(q==MT_Q_TX&&ntx<16); memcpy(txf[ntx],b,n); txn[ntx]=n; ntx++;
    if(n>=32+24&&b[32]==0xB0)pend_auth=1;
    if(n>=32+24&&b[32]==0x00){ static const u8 ep[3]={255,15,108},h6[3]={255,3,59},ht[2]={45,26}; pend_assoc=1; last_eht=0; for(u32 i=56;i+3<=n;i++)if(!memcmp(b+i,ep,3))last_eht=1; eht_seen+=last_eht;
        for(u32 i=56;i+3<=n;i++)if(!memcmp(b+i,h6,3)){ he6_seen++; for(u32 j=56;j+2<=n;j++)assert(memcmp(b+j,ht,2)); } }
    return 0; }
int wifi_gen_sae_auth(const struct wpa_io* io){ u8 f[40],b[400]; int bl=0; sae_calls++; memset(f,0,40); f[0]=0xB0; f[24]=3; f[26]=1; f[30]=19;
    if(io->sae_tx(io->ctx,f,32,1,1)!=0)return 9; if(io->sae_tx(io->ctx,f,32,0,1)!=3)return 5; if(!io->rx_mgmt(io->ctx,0xB0,b,&bl,600)||bl!=6||b[0]!=3||b[2]!=1)return 6;
    f[26]=2; io->sae_tx(io->ctx,f,32,-1,0); if(!io->rx_mgmt(io->ctx,0xB0,b,&bl,600)||b[2]!=2)return 7; return 0; }
int wifi_hw_assoc_ie(u8* out,int cap){ static const u8 r[]={48,20,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,8,0xC0,0,244,1,0x20}; if(cap<(int)sizeof r)return 0; memcpy(out,r,sizeof r); return (int)sizeof r; }
static int has(const u8* b,u32 n,const u8* p,u32 pn){ for(u32 i=0;i+pn<=n;i++)if(!memcmp(b+i,p,pn))return 1; return 0; }
static void t_sae(struct mt7921_softc* sc){
    mt7921_disconnect(sc); ncmd=0; ntx=0; sae_mode=1; sae_calls=0;
    assert(mt7921_connect(sc,AP,36,"wpa3",8,0)==0&&sae_calls==1);
    assert(txf[0][32]==0xB0&&txf[0][32+24]==3&&txf[1][32]==0xB0&&txf[1][32+26]==2&&txf[2][32]==0x00);
    { static const u8 s8[]={0x00,0x0F,0xAC,8,0xC0,0,244,1,0x20}; assert(has(txf[2],txn[2],s8,sizeof s8)); }
    mt7921_disconnect(sc); ncmd=0; ntx=0; sae_mode=3;
    assert(mt7921_connect(sc,AP,36,"wpa3",8,0)<0&&!strcmp(sc->err,"WPA3 SAE authentication failed")&&mt7921_sae_fail==1);
    sae_mode=0; ncmd=0; ntx=0; sc->band6=1;
    assert(mt7921_connect(sc,AP,37,"six",0,0)==0);
    { int i=0; while(i<ntx&&txf[i][32]!=0x00)i++; assert(i<ntx); static const u8 r5[]={1,8,0x8C}; static const u8 r2[]={1,8,0x82}; assert(has(txf[i],txn[i],r5,3)&&!has(txf[i],txn[i],r2,3)); }
    mt7921_disconnect(sc); sc->band6=0; ncmd=0; ntx=0;
}
static void t_txd(struct mt7921_softc* sc){
    u8 o[512]; u8 f[64]; memset(f,0,sizeof f);
    f[0]=0xB0; memcpy(f+4,AP,6); u32 n=mt7925_txd_build(sc,f,30,o);
    u32 d0=mt_get32(o),d1=mt_get32(o+4),d2=mt_get32(o+8),d3=mt_get32(o+12),d6=mt_get32(o+24);
    assert(n==62&&(d0&0xffff)==62&&((d0>>23)&3)==MT_TX_TYPE_CT&&(d0>>25)==MT_LMAC_ALTX0);
    assert((d1&0xfff)==MT_WCID_AP&&((d1>>14)&3)==MT_HDR_FORMAT_802_11&&((d1>>16)&0x1f)==12&&((d1>>21)&15)==MT7925_TX_NORMAL&&(d1&MT7925_TXD1_FIXED_RATE)&&((d1>>25)&0x3f)==0);
    assert(d2==((0u<<4)|11)&&((d3>>11)&0x1f)==15&&(d3&MT7925_TXD3_BA_DISABLE)&&!(d3&(1u<<4)));
    assert((d6&MT7925_TXD6_DAS)&&(d6&MT7925_TXD6_DIS_MAT)&&((d6>>4)&0x3f)==1&&((d6>>16)&0x3f)==MT7925_BASIC_RATES_TBL&&!mt_get32(o+28)&&!memcmp(o+32,f,30));
    memset(f,0,sizeof f); f[0]=0x88; f[1]=0x01; memcpy(f+4,AP,6); n=mt7925_txd_build(sc,f,40,o); d0=mt_get32(o); d1=mt_get32(o+4); d3=mt_get32(o+12); d6=mt_get32(o+24);
    assert(n==72&&(d0>>25)==1&&((d1>>16)&0x1f)==13&&!(d1&MT7925_TXD1_FIXED_RATE)&&!(d3&MT7925_TXD3_BA_DISABLE)&&!((d6>>16)&0x3f)&&mt_get32(o+8)==((2u<<4)|8));
    f[4]=0x01; n=mt7925_txd_build(sc,f,40,o); d1=mt_get32(o+4); d3=mt_get32(o+12);
    assert((d1&0xfff)==MT_WCID_BMC&&(d1&MT7925_TXD1_FIXED_RATE)&&(d3&(1u<<4))&&(d3&MT7925_TXD3_BA_DISABLE));
    memset(f,0,sizeof f); f[0]=0xD0; memcpy(f+4,AP,6); f[24]=3; f[25]=0; n=mt7925_txd_build(sc,f,33,o); assert(((mt_get32(o+4)>>21)&15)==MT7925_TX_ADDBA);
    sc->band_idx=1; n=mt7925_txd_build(sc,f,33,o); assert(((mt_get32(o+4)>>12)&3)==1); sc->band_idx=0;
    assert(!mt7925_txd_build(sc,f,10,o)&&!mt7925_txd_build(sc,f,MT_TXBUF_SIZE,o));
}
static void t_rxd(void){
    u8 b[512]; memset(b,0,sizeof b); u32 off,fl; int rssi; u8 ch;
    u32 n=32+16+16+8+16+96+4+40; mt_put32(b,n|(2u<<27)); mt_put32(b+4,1u|(1u<<16)|(1u<<17)|(1u<<18)|(1u<<19)|(1u<<20)); mt_put32(b+8,2u<<13); mt_put32(b+12,149u<<8);
    mt_put32(b+32+16+16+8+12,160);
    assert(!mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)&&off==32+16+16+8+16+96+4&&fl==40&&ch==149&&rssi==-30);
    mt_put32(b+4,0); mt_put32(b+8,0); assert(!mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)&&off==32&&rssi==-127);
    mt_put32(b+4,1u<<25); assert(mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)<0); mt_put32(b+4,0);
    mt_put32(b+8,1u<<7); assert(mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)<0);
    mt_put32(b+8,1u<<24); assert(mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)<0); mt_put32(b+8,0);
    mt_put32(b+12,1u<<24); assert(mt7925_rxd_parse(b,n,&off,&fl,&rssi,&ch)<0); mt_put32(b+12,0);
    mt_put32(b+4,(1u<<18)|(1u<<20)); mt_put32(b,(32+40)|(2u<<27)); assert(mt7925_rxd_parse(b,72,&off,&fl,&rssi,&ch)<0);
    assert(mt7925_rxd_parse(b,20,&off,&fl,&rssi,&ch)<0);
}
static void t_eht(void){
    u8 ie[40]; int n=0; ie[n++]=0; ie[n++]=0; ie[n++]=255; ie[n++]=1+2+9+6; ie[n++]=108; ie[n++]=0x34; ie[n++]=0x12; for(int i=0;i<9;i++)ie[n++]=(u8)(0xa0+i); for(int i=0;i<6;i++)ie[n++]=(u8)(0x10+i);
    struct mt7925_eht e; assert(!mt7925_eht_parse(ie,n,&e)&&e.ok&&e.mac_cap==0x1234&&e.nmcs==6);
    u8 t[64]; assert(mt7925_sta_eht_tlv(&e,t)==40&&mt_get16(t)==STA_REC_EHT&&mt_get16(t+2)==40&&t[4]==0xff&&mt_get16(t+6)==0x1234&&t[8]==0xa0&&t[15]==0xa7&&t[16]==0xa8&&t[28]==0x10&&t[33]==0x15&&!t[34]);
    assert(mt7925_eht_parse(ie,2,&e)<0&&!e.ok&&!mt7925_sta_eht_tlv(&e,t)); ie[3]=60; assert(mt7925_eht_parse(ie,n,&e)<0);
}
static void t_ehtadv(struct mt7921_softc* sc){
    u8 bc[64]; memset(bc,0,sizeof bc); bc[0]=0x80; memcpy(bc+16,AP,6); u8* ie=bc+36; ie[0]=255; ie[1]=13; ie[2]=108;
    u8 other[6]={9,9,9,9,9,9}; assert(!mt7925_eht_ap(AP)); bc[37]=12; mt7925_eht_note(bc,64); assert(!mt7925_eht_ap(AP)); bc[37]=13; mt7925_eht_note(bc,64); assert(mt7925_eht_ap(AP)&&!mt7925_eht_ap(other));
    u8 o[32]; assert(!mt7925_assoc_eht(o,32)); mt7925_eht_adv=1; assert(mt7925_assoc_eht(o,16)==0&&mt7925_assoc_eht(o,32)==17&&o[0]==255&&o[1]==15&&o[2]==108&&o[14]==0x22); mt7925_eht_adv=0;
    mt7925_eht_fail=0; ncmd=0; ntx=0; eht_seen=0; eht_refuse=0;
    assert(mt7925_connect(sc,AP,36,MT7925_BAND_5G,"seven",0,1)==0&&eht_seen==1&&mt_assoc_eht_res==0&&!mt7925_eht_adv); mt7921_disconnect(sc);
    for(int k=1;k<=3;k++){ ncmd=0; ntx=0; eht_seen=0; eht_refuse=1; assert(mt7925_connect(sc,AP,36,MT7925_BAND_5G,"seven",0,1)==0&&eht_seen==1&&ntx>=3&&mt_assoc_eht_res==1&&mt7925_eht_fail==k&&sc->connected); mt7921_disconnect(sc); }
    assert(sc->err&&strstr(sc->err,"HE")); ncmd=0; ntx=0; eht_seen=0;
    assert(mt7925_connect(sc,AP,36,MT7925_BAND_5G,"seven",0,1)==0&&eht_seen==0&&mt_assoc_eht_res==-1); mt7921_disconnect(sc);
    mt7925_eht_fail=0; eht_refuse=0; ncmd=0; ntx=0; eht_seen=0; mt7925_connect(sc,other,36,MT7925_BAND_5G,"six",0,1); assert(eht_seen==0&&mt_assoc_eht_res==-1);
    mt7925_has_6g=1; ncmd=0; ntx=0; he6_seen=0; assert(mt7925_connect(sc,AP,37,MT7925_BAND_6G,"six",0,1)==0&&he6_seen==1&&eht_seen==1); mt7921_disconnect(sc); sc->band6=0; mt7925_has_6g=0;
    sc->err=0; ncmd=0; ntx=0; }
int main(void){
    struct mt7921_softc* sc=&g_mt7925; memset(sc,0,sizeof *sc);
    sc->state=MT_ST_READY; sc->gen=3; sc->mcu_send=mt7925_mcu_send; sc->rx_fix=mt7925_rx_fix; mt7925_mac_attach(sc);
    sc->wlan_idx=MT_WCID_AP; memcpy(sc->mac,ME,6);
    t_txd(sc); t_rxd(); t_eht();
    assert(mt7921_connect(sc,AP,36,"home",2,0)==0&&sc->connected&&sc->chan==36);
    for(int i=0;i<ncmd;i++)assert(cmds[i].uni);
    int b=find(MCU_UNI_CMD_BSS_INFO_UPDATE,0); assert(b>=0);
    const u8* t=tlv(&cmds[b],4,UNI_BSS_INFO_BASIC); assert(t&&mt_get16(t+2)==32&&t[4]==1&&mt_get32(t+8)==CONNECTION_INFRA_STA&&t[12]==0&&!memcmp(t+14,AP,6)&&mt_get16(t+26)==MT_WCID_AP&&mt_get16(t+28)==MT7925_PHY_TYPE_OFDM_INDEX&&t[25]==(MT7925_PHY_MODE_A|MT7925_PHY_MODE_AN|MT7925_PHY_MODE_AC));
    assert(cmds[b].p[4]==0&&cmds[b].p[5]==0);
    t=tlv(&cmds[b],4,UNI_BSS_INFO_RLM); assert(t&&t[4]==36&&t[5]==36&&t[7]==0&&t[12]==MT7925_BAND_5G);
    assert(tlv(&cmds[b],4,UNI_BSS_INFO_SEC)&&tlv(&cmds[b],4,UNI_BSS_INFO_RATE)&&tlv(&cmds[b],4,UNI_BSS_INFO_QBSS)&&tlv(&cmds[b],4,UNI_BSS_INFO_MLD)&&tlv(&cmds[b],4,UNI_BSS_INFO_IFS_TIME));
    t=tlv(&cmds[b],4,UNI_BSS_INFO_RATE); assert(mt_get16(t+6)==0x0540&&t[12]==0&&t[13]==MT7925_BASIC_RATES_TBL);
    int s=find(MCU_UNI_CMD_STA_REC_UPDATE,b); assert(s>b&&cmds[s].p[1]==MT_WCID_AP&&mt_get16(cmds[s].p+2)==4&&cmds[s].p[4]==1);
    t=tlv(&cmds[s],8,STA_REC_BASIC); assert(t&&mt_get32(t+4)==CONNECTION_INFRA_AP&&t[8]==CONN_STATE_PORT_SECURE&&!memcmp(t+12,AP,6)&&mt_get16(t+18)==(EXTRA_INFO_VER|EXTRA_INFO_NEW));
    t=tlv(&cmds[s],8,STA_REC_STATE); assert(t&&t[4]==MT7925_STA_STATE_NONE);
    t=tlv(&cmds[s],8,STA_REC_PHY); assert(t&&t[6]==MT7925_MODE_OFDM&&mt_get16(t+4)==0x15);
    t=tlv(&cmds[s],8,STA_REC_HDR_TRANS); assert(t&&t[5]==1&&t[6]==1);
    assert(!tlv(&cmds[s],8,STA_REC_EHT));
    assert(ntx>=2&&txf[0][32]==0xB0&&txf[1][32]==0x00);
    int a=find(MCU_UNI_CMD_STA_REC_UPDATE,s+1); assert(a>s);
    t=tlv(&cmds[a],8,STA_REC_STATE); assert(t&&t[4]==MT7925_STA_STATE_ASSOC);
    t=tlv(&cmds[a],8,STA_REC_BASIC); assert(mt_get16(t+10)==5&&mt_get16(t+18)==EXTRA_INFO_VER);
    assert(find(MCU_UNI_CMD_STA_REC_UPDATE,a+1)<0);
    int c0=ncmd; u8 k[16]; for(int i=0;i<16;i++)k[i]=(u8)i;
    assert(mt7921_set_key(sc,0,WIFI_KEY_CCMP,k,16,1)==0);
    t=tlv(&cmds[c0],8,STA_REC_KEY_V3); assert(t&&mt_get16(t+2)==68&&t[4]==1&&t[5]==1&&t[6]==1&&!memcmp(t+8,AP,6)&&t[15]==MT7925_CIPHER_AES_CCMP&&t[16]==0&&t[17]==16&&t[18]==MT_WCID_AP&&!memcmp(t+20,k,16));
    t=tlv(&cmds[c0+1],8,STA_REC_BASIC); assert(t&&t[8]==CONN_STATE_PORT_SECURE);
    c0=ncmd; u8 g[16]; memset(g,0x5a,16);
    assert(mt7921_set_key(sc,4,WIFI_KEY_BIP,g,16,0)==0);
    t=tlv(&cmds[c0],8,STA_REC_KEY_V3); assert(t&&cmds[c0].p[1]==MT_WCID_BMC&&t[5]==0&&t[15]==MT7925_CIPHER_BIP_CMAC_128&&t[16]==4&&t[17]==32&&!memcmp(t+20,k,16)&&!memcmp(t+36,g,16));
    assert(mt7921_set_key(sc,1,WIFI_KEY_CCMP,g,13,0)<0&&mt7921_set_key(sc,1,99,g,16,0)<0);
    c0=ncmd; int tx0=ntx;
    mt7921_disconnect(sc); assert(!sc->connected&&ntx==tx0+1&&txf[tx0][32]==0xC0);
    s=find(MCU_UNI_CMD_STA_REC_UPDATE,c0); assert(s>=0&&mt_get16(cmds[s].p+2)==2&&tlv(&cmds[s],8,STA_REC_REMOVE)&&tlv(&cmds[s],8,STA_REC_MLD_OFF)&&!tlv(&cmds[s],8,STA_REC_BASIC));
    b=find(MCU_UNI_CMD_BSS_INFO_UPDATE,s); assert(b>s&&tlv(&cmds[b],4,UNI_BSS_INFO_BASIC)[12]==1&&!tlv(&cmds[b],4,UNI_BSS_INFO_RLM));
    ncmd=0; ntx=0; sc->band6=1;
    assert(mt7921_connect(sc,AP,37,"six",0,0)==0);
    b=find(MCU_UNI_CMD_BSS_INFO_UPDATE,0); t=tlv(&cmds[b],4,UNI_BSS_INFO_RLM); assert(t[12]==MT7925_BAND_6G&&tlv(&cmds[b],4,UNI_BSS_INFO_BASIC)[30]==MT7925_PHY_MODE_AX_6G);
    mt7921_disconnect(sc); sc->band6=0;
    ncmd=0; ntx=0;
    assert(mt7921_connect(sc,AP,6,"two",0,0)==0);
    b=find(MCU_UNI_CMD_BSS_INFO_UPDATE,0); t=tlv(&cmds[b],4,UNI_BSS_INFO_BASIC); assert(mt_get16(t+28)==MT7925_PHY_TYPE_ERP_INDEX&&tlv(&cmds[b],4,UNI_BSS_INFO_RLM)[12]==MT7925_BAND_2G);
    s=find(MCU_UNI_CMD_STA_REC_UPDATE,b); t=tlv(&cmds[s],8,STA_REC_PHY); assert(t[6]==(MT7925_MODE_CCK|MT7925_MODE_OFDM)&&mt_get16(t+4)==0xf);
    t_sae(sc); t_ehtadv(sc);
    printf("mt7925 join: all passed\n"); return 0;
}
