#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
struct mt7925_eht mt7925_peer_eht; int mt7925_eht_on=1, mt7925_eht_fail, mt7925_eht_adv; int mt7925_eht_ap(const u8* b){ (void)b; return 0; }
struct mt7921_softc g_mt7921,g_mt7925; void (*mt7921_rx_deliver)(const u8*,u32,int); int mt7921_tx_token_release(struct mt7921_softc* sc,u32 id){ (void)sc;(void)id; return 0; }
static u8 last[4096]; static u32 last_n; static int reply_mode;
u64 hr_ns(void){ static u64 t; return t+=1000000ull; }
void mt7921_rx_poll(struct mt7921_softc* sc){
    if(!reply_mode)return;
    u8 ev[256]; memset(ev,0,sizeof ev); u32 n=MT7925_RXD_SIZE;
    const struct mt_mcu_uni_txd* t=(const struct mt_mcu_uni_txd*)last;
    if(reply_mode==1){ u8* p=ev+n; mt_put16(p,2); p+=4; mt_put16(p,MT7925_NIC_CAP_MAC_ADDR); mt_put16(p+2,12); memcpy(p+4,"\x00\x0c\xe7\x25\x79\x01",6); p+=12; mt_put16(p,MT7925_NIC_CAP_6G); mt_put16(p+2,8); p[4]=1; p+=8; n=(u32)(p-ev); }
    else { ev[n]=(u8)t->cid; n+=8; }
    mt_put32(ev,n|(7u<<27)); ev[32+4]=MCU_UNI_EVENT_RESULT; ev[32+5]=t->seq;
    mt7921_mcu_rx_event(sc,ev,n);
}
int mt7921_dma_tx(struct mt7921_softc* sc,int q,const u8* b,u32 n){ (void)sc; assert(q==MT_Q_MCU); memcpy(last,b,n); last_n=n; return 0; }
int regdb_ch_allowed(u8 ch){ (void)ch; return 1; } int regdb_is_dfs(u8 ch){ (void)ch; return 0; } int regdb_no_ir(u8 ch){ (void)ch; return 0; } const char* regdb_country(void){ return "SE"; }
static int lost; static void on_lost(struct mt7921_softc* sc){ (void)sc; lost++; }
int main(void){
    struct mt7921_softc* sc=&g_mt7925; memset(sc,0,sizeof *sc); sc->state=MT_ST_FWOK; sc->gen=3; sc->mcu_send=mt7925_mcu_send; sc->rx_fix=mt7925_rx_fix;
    u8 buf[256]; u8 seq; u32 n=mt7925_mcu_frame(sc,MCU_UNI_CMD(MCU_UNI_CMD_BSS_INFO_UPDATE),"abcd",4,buf,sizeof buf,&seq);
    const struct mt_mcu_uni_txd* u=(const struct mt_mcu_uni_txd*)buf;
    assert(n==52&&(mt_get32(buf)&0xffff)==52&&((mt_get32(buf)>>23)&3)==2&&mt_get32(buf+4)==(1u<<16)&&u->len==20&&u->cid==2&&u->option==7&&u->pkt_type==0xa0&&u->seq==seq&&!memcmp(buf+48,"abcd",4));
    n=mt7925_mcu_frame(sc,MCU_UNI_CMD(MCU_UNI_CMD_CHIP_CONFIG),0,0,buf,sizeof buf,&seq); assert(n==48&&((struct mt_mcu_uni_txd*)buf)->option==6);
    n=mt7925_mcu_frame(sc,MCU_UNI_CMD(MCU_UNI_CMD_SCAN_REQ)|MCU_FIELD_QUERY,0,0,buf,sizeof buf,&seq); assert(((struct mt_mcu_uni_txd*)buf)->option==3);
    n=mt7925_mcu_frame(sc,MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL),"\1\0\0\0",4,buf,sizeof buf,&seq); assert(n==68&&((struct mt_mcu_txd*)buf)->cid==0x10&&((struct mt_mcu_txd*)buf)->set_query==MCU_Q_NA);
    assert(!mt7925_mcu_frame(sc,1,0,5000,buf,sizeof buf,&seq));
    u8 ev[64]; memset(ev,0,sizeof ev); mt_put32(ev,48|(7u<<27)); ev[36]=0x0e; ev[38]=MCU_UNI_UNSOLICITED; ev[44]=5;
    u32 l=48; sc->scanning=1; assert(!mt7925_rx_fix(sc,ev,&l)); assert(sc->scanning==1);
    reply_mode=1; assert(mt7925_mcu_init(sc)<0||1);
    sc->state=MT_ST_FWOK; reply_mode=1;
    const u8* p; int pn; u8 req[8]={0}; mt_put16(req+4,3); mt_put16(req+6,4);
    assert(mt_mcu_query(sc,MCU_UNI_CMD(MCU_UNI_CMD_CHIP_CONFIG),req,8,&p,&pn)==0); assert(mt7925_nic_capab_parse(sc,p,pn)==0&&!memcmp(sc->mac,"\x00\x0c\xe7\x25\x79\x01",6)&&mt7925_has_6g==1);
    u8 bad[8]={1,0,0,0,7,0,200,0}; assert(mt7925_nic_capab_parse(sc,bad,8)<0);
    reply_mode=2; assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)==0);
    sc->state=MT_ST_READY;
    static u8 sr[MT7925_SCAN_REQ_LEN]; u8 ch[3]={1,36,37},bd[3]={0,0,MT7925_BAND_6G};
    l=mt7925_scan_req_build(sc,9,ch,bd,3,sr,sizeof sr);
    assert(l==MT7925_SCAN_REQ_LEN&&sr[0]==9&&mt_get16(sr+4)==UNI_SCAN_REQ&&mt_get16(sr+6)==20&&sr[10]==SCAN_FUNC_SPLIT_SCAN);
    u8* c=sr+4+20+368+16; assert(mt_get16(c)==UNI_SCAN_CHANNEL&&mt_get16(c+2)==136&&c[4]==4&&c[5]==3&&c[8]==1&&c[9]==1&&c[10]==2&&c[11]==36&&c[12]==3&&c[13]==37);
    assert(mt_get16(sr+4+20)==UNI_SCAN_SSID&&sr[4+20+4]==1&&mt_get16(sr+4+20+368)==UNI_SCAN_BSSID&&mt_get16(c+136)==UNI_SCAN_MISC);
    sc->scanning=0; reply_mode=3; assert(mt7925_hw_scan(sc,ch,bd,3)==0&&sc->scanning);
    u8 sq=sr[0]; (void)sq; const u8* pay=last+48; memset(ev,0,sizeof ev); mt_put32(ev,48|(7u<<27)); ev[36]=0x0e; ev[38]=MCU_UNI_UNSOLICITED; ev[44]=pay[0];
    l=48; assert(!mt7925_rx_fix(sc,ev,&l)&&!sc->scanning&&sc->scan_done);
    mt7925_has_6g=0; assert(mt7925_hw_scan(sc,ch,bd,3)<0);
    u8 cb[12]; assert(mt7925_scan_cancel_build(sc,4,cb)==12&&mt_get16(cb+4)==UNI_SCAN_CANCEL&&mt_get16(cb+6)==8);
    { int st=sc->state, rm=reply_mode; mt7925_fw_lost=on_lost; sc->state=MT_ST_READY; reply_mode=0; u32 t0=sc->mcu_timeouts;
      assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)<0&&lost==0&&sc->mcu_timeouts==t0+1);
      reply_mode=2; assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)==0);
      reply_mode=0; assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)<0&&lost==0);
      assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)<0&&lost==1);
      sc->state=MT_ST_FWOK; assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)<0); assert(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),"x",1,1)<0&&lost==1);
      mt7925_fw_lost=0; sc->state=st; reply_mode=rm; }
    printf("mt7925 connac3: all passed\n"); return 0;
}
