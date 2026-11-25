#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/mediatek/mt7921/mcu/rate.c"
struct mt7921_softc g_mt7921;
void mt_put16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
void mt_put32(u8* p,u32 v){ for(int i=0;i<4;i++)p[i]=(u8)(v>>(8*i)); }
u16 mt_get16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
u32 mt_get32(const u8* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((u32)p[3]<<24); }
void mt_zero(void* d,u32 n){ memset(d,0,n); }
void mt_copy(void* d,const void* s,u32 n){ memcpy(d,s,n); }
static u64 now; u64 mt_ms(void){ return now; }
static int mcu_n,mcu_fail; static u8 mcu_last[256],mcu_prev[256]; static u32 mcu_len,mcu_plen;
int mt7921_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* d,u32 l,int w){ (void)sc;(void)w; assert(cmd==MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE)); assert(l<=256); memcpy(mcu_prev,mcu_last,mcu_len); mcu_plen=mcu_len; memcpy(mcu_last,d,l); mcu_len=l; mcu_n++; return mcu_fail?-1:0; }
static u8 tx_last[256]; static u32 tx_len; static int tx_n;
int mt7921_tx_data(struct mt7921_softc* sc,const u8* f,u32 l){ u32 d3; mt_ba_tx_hook(sc,f,l,&d3); memcpy(tx_last,f,l); tx_len=l; tx_n++; return 0; }
u32 mt_sta_req_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 st,u16 aid,u8* out){ memset(out,0,28); out[0]=sc->bss_idx; out[1]=wcid; mt_put16(out+2,1); out[4]=1; mt_put16(out+8,STA_REC_BASIC); mt_put16(out+10,20); out[16]=st; mt_put16(out+18,aid); memcpy(out+20,peer,6); return 28; }
static int walk(const u8* b,u32 l,u16* tags){ u32 o=8; int n=0; while(o+4<=l){ u16 len=mt_get16(b+o+2); assert(len>=4&&o+len<=l); tags[n++]=mt_get16(b+o); o+=len; } assert(o==l); assert(mt_get16(b+2)==n); return n; }
static int has(const u16* t,int n,u16 v){ for(int i=0;i<n;i++)if(t[i]==v)return 1; return 0; }
int main(void){ mt_ba_on=1; mt_batx_on=1;
    struct mt7921_softc* sc=&g_mt7921; sc->bss_idx=0; sc->omac_idx=0; sc->state=MT_ST_READY; memcpy(sc->bssid,"\x02\x11\x22\x33\x44\x55",6); memcpy(sc->mac,"\x00\x0c\xe7\x01\x02\x03",6);
    u8 ie[128]; int n=0;
    static const u8 r[]={1,8,0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24,50,4,0x30,0x48,0x60,0x6c}; memcpy(ie,r,sizeof r); n=sizeof r;
    ie[n++]=45; ie[n++]=26; memset(ie+n,0,26); ie[n]=0x6f; ie[n+1]=0x01; ie[n+2]=0x17; ie[n+3]=0xff; ie[n+4]=0xff; ie[n+5]=0xff; n+=26;
    ie[n++]=61; ie[n++]=22; memset(ie+n,0,22); ie[n]=6; ie[n+1]=5; n+=22;
    ie[n++]=255; ie[n++]=22; ie[n++]=35; memset(ie+n,0,21); ie[n+3]=0x10; ie[n+6]=0x02; ie[n+7]=0x60; ie[n+17]=0xfa; ie[n+18]=0xff; ie[n+19]=0xfa; ie[n+20]=0xff; n+=21;
    struct mt_peer_caps c; sc->chan=6;
    assert(mt_peer_parse(ie,n,6,&c)==0);
    assert(c.legacy==0xfff&&c.basic==0xf&&c.ht&&c.ht_cap==0x016f&&c.ampdu==0x17&&c.mcs[0]==0xff&&c.mcs[1]==0xff&&c.mcs[2]==0&&c.nss==2&&c.bw==1&&c.he&&!c.vht&&c.he_mcs[0]==0xfffa);
    static u8 b[MT_STA_RATE_MAX]; u16 tg[16];
    u32 l=mt_sta_rate_build(sc,MT_WCID_AP,sc->bssid,5,&c,-50,b,sizeof b); assert(l>28&&l<=MT_STA_RATE_MAX);
    int k=walk(b,l,tg); assert(k==7&&has(tg,k,STA_REC_HT)&&has(tg,k,STA_REC_HE)&&has(tg,k,STA_REC_HE_V2)&&has(tg,k,STA_REC_PHY)&&has(tg,k,STA_REC_RA)&&has(tg,k,STA_REC_STATE)&&!has(tg,k,STA_REC_VHT));
    u32 o=8; while(mt_get16(b+o)!=STA_REC_RA)o+=mt_get16(b+o+2); assert(mt_get16(b+o+4)==((0xffu<<6)|0xfu)&&b[o+6]==0xff&&b[o+7]==0xff&&b[o+8]==0);
    o=8; while(mt_get16(b+o)!=STA_REC_PHY)o+=mt_get16(b+o+2); assert(b[o+6]==(MT_PHY_HR_DSSS|MT_PHY_ERP|MT_PHY_HT|MT_PHY_HE)&&b[o+7]==0x17&&b[o+9]==120&&mt_get16(b+o+4)==0xf);
    o=8; while(mt_get16(b+o)!=STA_REC_HE)o+=mt_get16(b+o+2); assert(!(mt_get32(b+o+4)&(1u<<18))&&(mt_get32(b+o+4)&(1u<<13))&&(mt_get32(b+o+4)&(1u<<6))&&b[o+9]==2&&b[o+10]==1&&mt_get16(b+o+20)==0xfffa);
    o=8; while(mt_get16(b+o)!=STA_REC_HE_V2)o+=mt_get16(b+o+2); assert(b[o+21]==2&&b[o+10]==0x02&&b[o+11]==0x60&&mt_get16(b+o+22)==0xfffa);
    { static const u8 ref[][3]={{0,0x01,0},{2,0x08,2},{3,0x02,3},{4,0x40,4},{4,0x04,1},{6,0x20,18},{6,0x40,18},{7,0x20,6},{7,0x40,13},{8,0x02,17},{8,0x04,9},{8,0x08,10},
          {12,0x20,8},{13,0x04,14},{13,0x40,11},{13,0x80,12},{14,0x01,16},{14,0x10,15},{15,0x02,7},{15,0x04,19},{15,0x08,20}};
      for(u32 i=0;i<sizeof ref/sizeof ref[0];i++){ struct mt_peer_caps h; memset(&h,0,sizeof h); h.he=1; if(ref[i][0]<6)h.he_mac[ref[i][0]]=ref[i][1]; else h.he_phy[ref[i][0]-6]=ref[i][1]; u8 t[28]; memset(t,0,28); mt_he_cap_v1(&h,t); assert(mt_get32(t+4)==(1u<<ref[i][2])); }
      for(int by=0;by<17;by++) for(int bit=0;bit<8;bit++){ int hit=0; for(u32 i=0;i<sizeof ref/sizeof ref[0];i++) if(ref[i][0]==by&&ref[i][1]==(1<<bit))hit=1; if(hit)continue; struct mt_peer_caps h; memset(&h,0,sizeof h); if(by<6)h.he_mac[by]=(u8)(1<<bit); else h.he_phy[by-6]=(u8)(1<<bit); u8 t[28]; memset(t,0,28); mt_he_cap_v1(&h,t); assert(mt_get32(t+4)==0); } }
    { struct mt_peer_caps h; memset(&h,0,sizeof h); h.he_mac[1]=0x0c; h.he_mac[3]=0x18; h.he_phy[0]=0xfe; h.he_phy[1]=0x1f; h.he_phy[3]=0x3f; h.he_phy[8]=0xc0; u8 t[28]; memset(t,0,28); mt_he_cap_v1(&h,t);
      assert(t[8]==3&&t[9]==3&&t[10]==0x7f&&t[11]==1&&t[12]==3&&t[13]==1&&t[14]==3&&t[15]==1&&t[16]==3&&t[17]==0x0f&&t[18]==2); }
    n=0; static const u8 r5[]={1,8,0x8c,0x12,0x98,0x24,0xb0,0x48,0x60,0x6c}; memcpy(ie,r5,sizeof r5); n=sizeof r5;
    ie[n++]=45; ie[n++]=26; memset(ie+n,0,26); ie[n+3]=0xff; n+=26;
    ie[n++]=191; ie[n++]=12; memset(ie+n,0,12); mt_put32(ie+n,0x0f8001b2); mt_put16(ie+n+4,0xffea); mt_put16(ie+n+8,0xffea); n+=12;
    ie[n++]=192; ie[n++]=5; ie[n]=1; ie[n+1]=42; ie[n+2]=0; ie[n+3]=0; ie[n+4]=0; n+=5;
    sc->chan=36; assert(mt_peer_parse(ie,n,36,&c)==0);
    assert(c.legacy==0xff&&c.basic==0x15&&c.vht&&c.bw==2&&c.vht_rx==0xfffa&&c.nss==2&&!c.he);
    l=mt_sta_rate_build(sc,MT_WCID_AP,sc->bssid,1,&c,-127,b,sizeof b); k=walk(b,l,tg); assert(k==6&&has(tg,k,STA_REC_VHT));
    o=8; while(mt_get16(b+o)!=STA_REC_STATE)o+=mt_get16(b+o+2); assert(b[o+8]==MT_STA_INFO_ASSOC&&b[o+9]==(2|(1<<4)));
    o=8; while(mt_get16(b+o)!=STA_REC_RA)o+=mt_get16(b+o+2); assert(mt_get16(b+o+4)==(0xffu<<6));
    assert(mt_peer_parse(ie,3,36,&c)<0&&!c.ok); assert(mt_sta_rate_build(sc,1,sc->bssid,1,&c,0,b,sizeof b)==0);
    ie[0]=1; ie[1]=200; assert(mt_peer_parse(ie,10,36,&c)<0);
    u8 ac[80]; int an=mt_assoc_caps(sc,6,ac); assert(an==52&&ac[0]==45&&!(ac[2]&2)&&ac[28]==255&&ac[30]==35);
    an=mt_assoc_caps(sc,36,ac); assert(an==66&&(ac[2]&2)&&ac[28]==191&&ac[42]==255);
    mt_he_on=0; assert(mt_assoc_caps(sc,36,ac)==42); mt_he_on=1;
    l=mt_ba_wtbl_build(sc,3,100,64,1,0,b); assert(l==48&&mt_get16(b+2)==1&&mt_get16(b+8)==STA_REC_WTBL&&mt_get16(b+10)==40&&b[13]==WTBL_SET&&mt_get16(b+14)==1&&mt_get16(b+20)==WTBL_BA&&mt_get16(b+22)==28);
    assert(b[24]==3&&b[25]==MT_BA_TYPE_RECIPIENT&&mt_get16(b+32)==64&&!memcmp(b+34,sc->bssid,6)&&b[40]==3&&b[42]==1);
    l=mt_ba_wtbl_build(sc,0,7,32,1,1,b); assert(b[25]==MT_BA_TYPE_ORIGINATOR&&mt_get16(b+28)==7&&b[30]==1&&mt_get16(b+32)==32);
    l=mt_ba_sta_build(sc,5,9,64,1,1,0,b); assert(l==20&&mt_get16(b+8)==STA_REC_BA&&mt_get16(b+10)==12&&b[12]==5&&b[13]==MT_BA_TYPE_ORIGINATOR&&b[15]==(1<<5)&&mt_get16(b+16)==9&&mt_get16(b+18)==64);
    sc->connected=1; mt_ba_port=1;
    u8 f[64]; memset(f,0,64); f[0]=0xD0; memcpy(f+4,sc->mac,6); memcpy(f+10,sc->bssid,6); memcpy(f+16,sc->bssid,6);
    f[24]=3; f[25]=0; f[26]=7; mt_put16(f+27,(u16)(2|(0<<2)|(64<<6))); mt_put16(f+31,(u16)(1234<<4));
    mt_ba_rx_action(sc,f,33); assert(mt_ba[0].rx_ssn==1234);
    u8 rsp[64]; memset(rsp,0,64); rsp[0]=0xD0; rsp[24]=3; rsp[25]=1; rsp[26]=7; mt_put16(rsp+27,0); mt_put16(rsp+29,(u16)(2|(64<<6)));
    mcu_n=0; u32 d3=0; assert(mt_ba_tx_hook(sc,rsp,33,&d3)==0&&mcu_n==2&&mt_ba[0].rx_on&&mt_ba_rx_sess==1);
    assert(mcu_last[13]==MT_BA_TYPE_RECIPIENT&&mt_get16(mcu_last+16)==1234&&mt_get16(mcu_last+18)==64&&mcu_last[15]==1);
    memset(f+24,0,40); f[24]=3; f[25]=2; mt_put16(f+26,0x0800|(0<<12)); mcu_n=0; mt_ba_rx_action(sc,f,30); assert(!mt_ba[0].rx_on&&mt_ba_rx_sess==0&&mcu_n==2);
    u8 q[64]; memset(q,0,64); q[0]=0x88; q[1]=0x01; memcpy(q+4,sc->bssid,6); memcpy(q+10,sc->mac,6); q[24]=0; q[26]=0xAA; q[27]=0xAA; q[28]=3; q[32]=0x08; q[33]=0x00;
    tx_n=0; d3=0; int t=mt_ba_tx_hook(sc,q,40,&d3); assert(t==1&&(d3&(1u<<31))&&((d3>>16)&0xfff)==0&&tx_n==1&&mt_ba[0].tx_st==1&&tx_last[24]==3&&tx_last[25]==0&&mt_ba_tx_req==1);
    u8 tok=tx_last[26]; assert(((mt_get16(tx_last+27)>>6)==64)&&(mt_get16(tx_last+27)&2)&&mt_get16(tx_last+31)==(1u<<4));
    d3=0; t=mt_ba_tx_hook(sc,q,40,&d3); assert(t==1&&((d3>>16)&0xfff)==1&&tx_n==1);
    u8 ar[64]; memcpy(ar,f,24); ar[24]=3; ar[25]=1; ar[26]=(u8)(tok+1); mt_put16(ar+27,0); mt_put16(ar+29,(u16)(2|(32<<6))); mt_put16(ar+31,0);
    mt_ba_rx_action(sc,ar,33); assert(mt_ba[0].tx_st==1);
    ar[26]=tok; mcu_n=0; mt_ba_rx_action(sc,ar,33); assert(mt_ba[0].tx_st==2&&mt_ba_tx_sess==1&&mcu_n==2&&mcu_last[13]==MT_BA_TYPE_ORIGINATOR&&mt_get16(mcu_last+16)==1&&mt_get16(mcu_last+18)==32);
    q[32]=0x88; q[33]=0x8E; mt_ba[2].tx_st=0; q[24]=2; tx_n=0; t=mt_ba_tx_hook(sc,q,40,&d3); assert(t==3&&tx_n==0&&mt_ba[2].tx_st==0);
    memcpy(ar+24,"\x03\x02\x00\x00\x25\x00",6); mt_ba_rx_action(sc,ar,30); assert(mt_ba[0].tx_st==0&&mt_ba_tx_sess==0);
    tx_n=0; mt_ba[1].tx_st=0; q[32]=0x08; q[33]=0; q[24]=1; mt_ba_tx_hook(sc,q,40,&d3); assert(tx_n==1&&mt_ba[1].tx_st==1); now=2000; mt_ba_tx_hook(sc,q,40,&d3); assert(mt_ba[1].tx_st==0&&mt_ba[1].tx_fail==1);
    mcu_fail=1; for(int i=0;i<3;i++)assert(mt7921_ba(sc,4,0,64,1,0,0)<0); assert(!mt_ba_on); assert(mt7921_ba(sc,4,0,64,1,0,0)<0); mcu_fail=0;
    char out[512]; mt_rate_cmd("ba on",out,sizeof out); assert(mt_ba_on&&strstr(out,"block-ack on"));
    mt_rate_cmd("ra off",out,sizeof out); assert(!mt_ra_on&&strstr(out,"firmware RA off")); mt_peer.ok=1; assert(mt_sta_rate_send(sc,1)<0); mt_rate_cmd("ra on",out,sizeof out);
    ie[1]=8; mt_peer_parse(ie,n,36,&mt_peer); assert(mt_peer.ok); mcu_fail=1; for(int i=0;i<3;i++)mt_sta_rate_send(sc,1); assert(!mt_ra_on); mcu_fail=0;
    mt_rate_cmd("rate",out,sizeof out); printf("%s",out); assert(strstr(out,"VHT, 80 MHz, 2 streams"));
    mt_rate_cmd("he bogus",out,sizeof out); assert(strstr(out,"on|off")); mt_rate_cmd("rate",out,8); assert(strlen(out)<8);
    mt_ba[3].rx_on=1; mt_ba_rx_sess=1; mt_ba_port=1; mcu_n=0; mt_ba_reset(sc,0); assert(!mt_ba[3].rx_on&&!mt_ba_port&&mt_ba_rx_sess==0&&mcu_n==2);
    printf("mt7921 rate/ba: all passed\n"); return 0;
}
