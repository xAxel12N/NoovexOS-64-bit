#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_sta.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/net/wifi/wifi_hw.h"
const struct rtw89_hci_ops* rtw89_hci;
void klog(const char* m){ (void)m; }
void* dma_alloc_pages(unsigned long pg){ void* p=mmap(0,pg*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); return p==MAP_FAILED?0:p; }
int rtw89_write_xtal_si(struct rtw89_softc* sc,u8 off,u8 val,u8 mask){ (void)sc;(void)off;(void)val;(void)mask; return 0; }
static int whw_n; static u8 whw_last[64];
void wifi_hw_rx(const u8* f,int len,int rssi){ (void)rssi; whw_n++; memcpy(whw_last,f,len<64?len:64); }
static const u8 RSN[]={48,20,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,2,0,0};
int wifi_hw_assoc_ie(u8* out,int cap){ if(cap<(int)sizeof RSN)return 0; memcpy(out,RSN,sizeof RSN); return sizeof RSN; }
struct h2c { u8 cls,func,rack,dack; u32 len; u8 p[80]; };
static struct h2c hl[64]; static int hn, hfail_cls=-1;
int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* p,u32 len){ (void)sc; assert(cat==1&&len<=80);
    if(cls==hfail_cls)return -1;
    if(hn<64){ hl[hn].cls=cls; hl[hn].func=func; hl[hn].rack=(u8)rack; hl[hn].dack=(u8)dack; hl[hn].len=len; memcpy(hl[hn].p,p,len); } hn++; return 0; }
static int c2h_n; int rtw89_c2h_handle(struct rtw89_softc* sc,const u8* c,u32 len){ (void)sc;(void)c;(void)len; c2h_n++; return 0; }
int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 us){ (void)sc;(void)cls;(void)func;(void)us; return 0; }
static u8 regs[0x20000];
static u32 rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; u32 v=0; for(int i=0;i<w;i++)v|=(u32)regs[o+i]<<(8*i); return v; }
static void wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; for(int i=0;i<w;i++)regs[o+i]=(u8)(v>>(8*i)); }
static u64 dl; static void dly(u32 us){ dl+=us; }
static const struct rtw89_io IO={ rd,wr,dly };
static u32 le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

static const u8 AP[6]={0x02,0x11,0x22,0x33,0x44,0x55}, ME[6]={0x00,0xE0,0x4C,0x88,0x52,0xB0}, OTHER[6]={2,9,9,9,9,9};
static u8 q[4][64]; static int ql[4], qn;
static int ap_mode, auth_status, assoc_status, deauth_in_assoc, wrong_dst, c2h_once;
static void ap_frame(u8 fc,const u8* body,int bl,const u8* da){ if(qn>=4)return; u8* f=q[qn]; memset(f,0,64); f[0]=fc; memcpy(f+4,da,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); memcpy(f+24,body,bl); ql[qn++]=24+bl; }
static int c2h_rx(struct rtw89_softc* sc,u8* buf,u32 cap){ (void)sc; (void)cap;
    if(c2h_once){ c2h_once=0; memset(buf,0,16); return 16; }
    for(int i=0;i<qn;i++)rtw89_rx_frame(q[i],ql[i],-40); qn=0; return 0; }
static const struct rtw89_hci_ops HCI={ 0,c2h_rx,0 };
static int txn, txl[16], txm[16]; static u8 txr[16], txf[16][256];
static int tx_hook(struct rtw89_softc* sc,const u8* f,u32 len,int mgmt,u8 rate,u8 macid){ (void)sc; assert(macid==0&&len<=256);
    if(txn<16){ memcpy(txf[txn],f,len); txl[txn]=(int)len; txm[txn]=mgmt; txr[txn]=rate; } txn++;
    if(!ap_mode)return 0;
    if(f[0]==0xB0){ u8 b[6]={0,0,2,0,(u8)auth_status,0}; ap_frame(0xB0,b,6,wrong_dst?OTHER:ME); }
    if(f[0]==0x00){ if(deauth_in_assoc){ u8 r[2]={7,0}; ap_frame(0xC0,r,2,ME); } else { u8 b[6]={0x11,0,(u8)assoc_status,0,5,0xC0}; ap_frame(0x10,b,6,ME); } }
    return 0; }
static int chan_last; static int chan_hook(struct rtw89_softc* sc,u8 ch){ (void)sc; chan_last=ch; return ch==13?-1:0; }
static int find_h2c(int from,u8 cls,u8 func){ for(int i=from;i<hn&&i<64;i++) if(hl[i].cls==cls&&hl[i].func==func)return i; return -1; }
static void reset(void){ hn=0; txn=0; qn=0; whw_n=0; dl=0; ap_mode=1; auth_status=0; assoc_status=0; deauth_in_assoc=0; wrong_dst=0; }

int main(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852B; memcpy(sc.mac,ME,6);
    u8 b[80];
    assert(rtw89_build_role_maintain(b,0,RTW89_ROLE_CREATE)==4&&le32(b)==(1u<<13));
    assert(rtw89_build_role_maintain(b,3,RTW89_ROLE_REMOVE)==4&&le32(b)==(3u|(1u<<10)|(1u<<13)));
    assert(rtw89_build_join_info(b,0,0,RTW89_NET_TYPE_INFRA)==4&&le32(b)==((2u<<24)|(1u<<26)));
    assert(rtw89_build_join_info(b,0,1,RTW89_NET_TYPE_NO_LINK)==4&&le32(b)==((1u<<8)|(1u<<26)));
    struct rtw89_sta st; memset(&st,0,sizeof st); memcpy(st.me,ME,6); memcpy(st.bssid,AP,6); st.aid=0x1C5; st.sec_mode=2; st.sec_valid=1; st.sec_keyid[2]=1; st.sec_ent[2]=2;
    u8 hs=0,ht=0; for(int i=0;i<6;i++){ hs^=ME[i]; ht^=AP[i]; }
    assert(rtw89_build_addr_cam(b,&st,RTW89_NET_TYPE_INFRA,1)==60&&le32(b)==0&&le32(b+4)==(0x40u<<16));
    assert(le32(b+8)==(1u|(2u<<1)|((u32)hs<<16)|((u32)ht<<24))&&le32(b+16)==0x884CE000u&&le32(b+20)==(0x52u|(0xB0u<<8)|(0x02u<<16)|(0x11u<<24))&&le32(b+24)==0x55443322u);
    assert(le32(b+36)==(0x1C5u|(2u<<16)|(1u<<22))&&le32(b+40)==(1u|(2u<<24))&&le32(b+48)==(8u<<16)&&le32(b+52)==(1u|(0x3Fu<<2)|(0x02u<<16)|(0x11u<<24))&&le32(b+56)==0x55443322u);
    assert(rtw89_build_addr_cam(b,&st,RTW89_NET_TYPE_NO_LINK,0)==60&&(le32(b+8)&7)==0&&(le32(b+36)&0xFFF)==0&&!(le32(b+52)&1));
    u8 key[16]; for(int i=0;i<16;i++)key[i]=(u8)(0xA0+i);
    assert(rtw89_build_sec_cam(b,4,RTW89_SEC_KEY_TYPE_CCMP128,key)==24&&le32(b)==(4u|(20u<<16))&&le32(b+4)==6&&le32(b+8)==0xA3A2A1A0u&&le32(b+20)==0xAFAEADACu);
    assert(rtw89_build_cmac_default(b,0)==68&&le32(b)==0x80&&le32(b+24)==((3u<<16)|(1u<<22))&&le32(b+56)==0x00FF0000u&&le32(b+36)==(1u<<21)&&le32(b+4)==0);
    printf("rtw89 station H2C builders ok\n");

    rtw89_hci=&HCI; rtw89_tx_hook=tx_hook; reset();
    sc.state=RTW89_ST_FWOK; assert(rtw89_sta_join(&sc,AP,36,"Net",2,0)==-1&&hn==0&&txn==0);
    sc.state=RTW89_ST_READY; assert(rtw89_sta_join(&sc,AP,200,"Net",2,0)==2);
    assert(rtw89_sta_join(&sc,AP,36,"Net",2,0)==-1&&strstr(g_r89s.why,"channel switch")&&hn==0);
    rtw89_chan_hook=chan_hook; assert(rtw89_sta_join(&sc,AP,13,"Net",2,0)==-1&&chan_last==13&&hn==0);
    reset(); assert(rtw89_sta_join(&sc,AP,36,"HomeNet",2,0)==0&&g_r89s.state==R89S_UP&&g_r89s.aid==5&&chan_last==36);
    assert(rtw89_rx_frame==rtw89_sta_rx&&whw_n==2);
    int i0=find_h2c(0,8,4), i1=find_h2c(0,6,0), i2=find_h2c(0,5,2), i3=find_h2c(0,8,0), i4=find_h2c(i1+1,6,0);
    assert(i0==0&&i1==1&&i2==2&&i3==3&&i4==4&&hn==5);
    assert(le32(hl[0].p)==(1u<<13)&&hl[0].dack&&!hl[0].rack&&hl[1].len==60&&(le32(hl[1].p+8)&7)==1&&hl[2].len==68&&le32(hl[3].p)==((2u<<24)|(1u<<26)));
    assert((le32(hl[4].p+8)&7)==(1u|(2u<<1))&&(le32(hl[4].p+36)&0xFFF)==5&&((le32(hl[4].p+36)>>16)&3)==0);
    assert(txn==2&&txm[0]&&txm[1]&&txr[0]==RTW89_HW_RATE_OFDM6&&txl[0]==30&&txf[0][0]==0xB0&&!memcmp(txf[0]+4,AP,6)&&!memcmp(txf[0]+10,ME,6)&&txf[0][26]==1);
    { u8* a=txf[1]; int n=txl[1]; assert(a[0]==0x00&&a[24]==1&&a[26]==10&&a[28]==0&&a[29]==7&&!memcmp(a+30,"HomeNet",7)&&a[37]==1&&a[39]==0x8C);
      assert(n==24+4+9+10+(int)sizeof RSN&&!memcmp(a+n-sizeof RSN,RSN,sizeof RSN)&&((a[22]|(a[23]<<8))>>4)==1); }
    u8 df[40]; memset(df,0,40); df[0]=0x08; df[1]=0x01;
    assert(rtw89_sta_tx(&sc,df,40)==0&&txn==3&&!txm[2]&&txr[2]==RTW89_HW_RATE_OFDM6);
    assert(rtw89_sta_set_key(&sc,0,WIFI_KEY_CCMP,key,16,1)==0&&hn==5);
    assert(rtw89_sta_set_key(&sc,0,1,key,16,1)<0&&rtw89_sta_set_key(&sc,0,WIFI_KEY_CCMP,key,5,1)<0);
    c2h_once=1; int c0=c2h_n; rtw89_sta_poll(&sc); assert(c2h_n==c0+1);
    rtw89_sta_leave(&sc); assert(g_r89s.state==R89S_IDLE&&txn==4&&txf[3][0]==0xC0&&txf[3][24]==3);
    { int j=find_h2c(5,8,0), c=find_h2c(5,6,0), r=find_h2c(5,8,4); assert(j==5&&c==6&&r==7&&le32(hl[5].p)==((1u<<8)|(1u<<26))&&!(le32(hl[6].p+8)&1)&&le32(hl[7].p)==((1u<<10)|(1u<<13))); }
    assert(rtw89_sta_tx(&sc,df,40)<0);
    printf("rtw89 station join/leave ok\n");

    reset(); ap_mode=0; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==1&&txn==3&&txr[0]==RTW89_HW_RATE_CCK1&&dl<=3u*301u*1000u&&g_r89s.state==R89S_IDLE);
    assert(find_h2c(0,8,4)>=0&&le32(hl[find_h2c(3,8,4)].p)==((1u<<10)|(1u<<13))&&find_h2c(0,8,0)<0);
    reset(); wrong_dst=1; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==1&&txn==3);
    reset(); auth_status=17; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==2&&txn==1);
    reset(); assoc_status=17; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==2&&txn==2&&!g_r89s.aid_ok);
    reset(); deauth_in_assoc=1; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==2&&g_r89s.deauths>=1);
    reset(); hfail_cls=5; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==-1&&txn==0&&g_r89s.state==R89S_IDLE); hfail_cls=-1;
    reset(); sc.chip=RTW89_CHIP_8852C; assert(rtw89_sta_join(&sc,AP,6,"HomeNet",2,0)==0&&find_h2c(0,5,2)<0); rtw89_sta_leave(&sc); sc.chip=RTW89_CHIP_8852B;
    printf("rtw89 station failure paths ok\n");

    reset(); g_r89s.hwcrypt=1; assert(rtw89_sta_set_key(&sc,0,WIFI_KEY_CCMP,key,16,1)<0);
    assert(rtw89_sta_join(&sc,AP,36,"HomeNet",2,0)==0); hn=0;
    assert(rtw89_sta_set_key(&sc,0,WIFI_KEY_CCMP,key,16,1)==0&&hn==2&&hl[0].cls==0xA&&hl[0].func==1&&hl[0].rack&&!hl[0].dack&&hl[0].len==24&&le32(hl[0].p)==(0u|(20u<<16))&&le32(hl[0].p+4)==6&&!memcmp(hl[0].p+8,key,16));
    assert(hl[1].cls==6&&((le32(hl[1].p+36)>>16)&3)==2&&(le32(hl[1].p+40)&0xFF)==1);
    assert(rtw89_sta_set_key(&sc,1,WIFI_KEY_CCMP,key,16,0)==0&&le32(hl[2].p)==(2u|(20u<<16))&&((le32(hl[3].p+36)>>22)&3)==1&&(le32(hl[3].p+40)&0xFF)==5&&((le32(hl[3].p+40)>>24)&0xFF)==2);
    assert(rtw89_sta_set_key(&sc,4,WIFI_KEY_BIP,key,16,0)==0&&le32(hl[4].p)==(5u|(20u<<16))&&le32(hl[4].p+4)==10&&(le32(hl[5].p+40)&0xFF)==0x25&&((le32(hl[5].p+44)>>16)&0xFF)==5);
    rtw89_sta_leave(&sc); assert(g_r89s.sec_valid==0); g_r89s.hwcrypt=0;
    printf("rtw89 station hardware keys ok\n");
    printf("rtw89d host ok\n");
    return 0; }
