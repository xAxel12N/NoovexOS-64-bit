#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_txpwr.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
void klog(const char* m){ (void)m; }
int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* p,u32 len){ (void)sc;(void)cat;(void)cls;(void)func;(void)rack;(void)dack;(void)p;(void)len; return 0; }
int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 us){ (void)sc;(void)cls;(void)func;(void)us; return 0; }
static const char* cc="US"; static int maxdbm;
const char* regdb_country(void){ return cc; }
int regdb_max_dbm(u8 ch){ (void)ch; return maxdbm; }

static u8 regs[0x20000]; static unsigned long nwr, dl; static u32 rf[2][256]; static int rflog[64][4], rfn, swsi_dead, txmode;
static u32 g32(u32 o){ return (u32)regs[o]|((u32)regs[o+1]<<8)|((u32)regs[o+2]<<16)|((u32)regs[o+3]<<24); }
static u32 rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; assert(w==4&&o+4<=sizeof regs); return g32(o); }
static void p32(u32 o,u32 v){ for(int i=0;i<4;i++)regs[o+i]=(u8)(v>>(8*i)); }
static void wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; assert(w==4&&o+4<=sizeof regs); nwr++; p32(o,v);
    if(o==0x10370){ int p=(v>>28)&7; u32 a=(v>>20)&0xFF, d=v&0xFFFFF; assert(p<2); if(v>>31){ u32 m=g32(0x10374)&0xFFFFF; rf[p][a]=(rf[p][a]&~m)|(d&m); } else rf[p][a]=d; if(rfn<64){ rflog[rfn][0]=p; rflog[rfn][1]=(int)a; rflog[rfn][2]=(int)rf[p][a]; } rfn++; }
    if(o==0x10378){ int p=(v>>8)&7; u32 a=v&0xFF; assert(p<2); u32 x=rf[p][a]; if(a==0&&txmode>0){ txmode--; x=(x&~0xF0000u)|0x20000u; } p32(0x1174C,swsi_dead?0:(1u<<26)|x); } }
static void dly(u32 us){ dl+=us; }
static const struct rtw89_io IO={ rd,wr,dly };
static signed char rb(u32 a){ return (signed char)regs[a]; }

static void test_byrate(void){
    struct rtw89_byr b[2]; rtw8852b_byr_load(b);
    for(int i=0;i<4;i++)assert(b[0].cck[i]==0x50&&b[1].cck[i]==0);
    static const signed char of[8]={0x50,0x50,0x50,0x50,0x50,0x50,0x4c,0x48}, mc[12]={0x50,0x50,0x50,0x50,0x50,0x4c,0x48,0x44,0x40,0x3c,0x38,0x34};
    for(int k=0;k<2;k++){ for(int i=0;i<8;i++)assert(b[k].ofdm[i]==of[i]); for(int n=0;n<2;n++){ for(int i=0;i<12;i++)assert(b[k].mcs[n][i]==mc[i]); for(int i=0;i<4;i++)assert(b[k].hedcm[n][i]==0x50); } for(int i=0;i<5;i++)assert(b[k].offset[i]==0); } }

static void test_regd(void){
    assert(rtw89_txpwr_regd_of("US",0)==2&&rtw89_txpwr_regd_of("US",1)==2);
    assert(rtw89_txpwr_regd_of("SE",0)==1&&rtw89_txpwr_regd_of("JP",1)==3);
    assert(rtw89_txpwr_regd_of("GB",0)==1);
    assert(rtw89_txpwr_regd_of("AR",0)==9&&rtw89_txpwr_regd_of("CN",1)==12&&rtw89_txpwr_regd_of("KR",0)==6);
    assert(rtw89_txpwr_regd_of("ZZ",0)==0&&rtw89_txpwr_regd_of(0,0)==0&&rtw89_txpwr_regd_of("",1)==0&&rtw89_txpwr_regd_of("US",2)==0); }

static void test_limit(void){
    assert(rtw8852b_txpwr_limit(2,0,0,0,0,0,1)==39&&rtw8852b_txpwr_limit(1,0,0,0,0,0,1)==29&&rtw8852b_txpwr_limit(3,0,0,0,0,0,6)==34);
    assert(rtw8852b_txpwr_limit(0,0,0,0,0,0,14)==38&&rtw8852b_txpwr_limit(3,0,0,0,0,0,14)==38);
    assert(rtw8852b_txpwr_limit(2,0,0,0,1,0,1)==38&&rtw8852b_txpwr_limit(2,0,0,0,1,0,6)==39&&rtw8852b_txpwr_limit(3,0,0,0,2,0,1)==38);
    assert(rtw8852b_txpwr_limit(2,0,0,0,0,0,14)==38);
    assert(rtw8852b_txpwr_limit(2,0,0,0,1,0,14)==25);
    assert(rtw8852b_txpwr_limit(2,1,0,0,1,0,36)==39&&rtw8852b_txpwr_limit(1,1,0,0,1,0,36)==29&&rtw8852b_txpwr_limit(0,1,0,0,1,0,36)==21);
    assert(rtw8852b_txpwr_limit(2,1,0,1,2,0,149)==39&&rtw8852b_txpwr_limit(1,1,0,1,2,0,149)==8);
    assert(rtw8852b_txpwr_limit(8,1,0,0,1,0,36)==rtw8852b_txpwr_limit(0,1,0,0,1,0,36)||rtw8852b_txpwr_limit(8,1,0,0,1,0,36)>0);
    assert(rtw8852b_txpwr_limit(2,1,1,0,1,0,36)==0&&rtw8852b_txpwr_limit(2,0,2,0,1,0,1)==0&&rtw8852b_txpwr_limit(2,1,0,0,1,0,37)==0&&rtw8852b_txpwr_limit(99,0,0,0,0,0,1)==28);
    assert(rtw8852b_txpwr_limit_ru(2,0,0,0,1)==34&&rtw8852b_txpwr_limit_ru(1,0,0,0,1)==16&&rtw8852b_txpwr_limit_ru(2,0,2,0,1)==36&&rtw8852b_txpwr_limit_ru(0,0,2,0,6)==28);
    assert(rtw8852b_txpwr_limit_ru(2,1,0,0,36)==26&&rtw8852b_txpwr_limit_ru(1,1,0,0,36)==12&&rtw8852b_txpwr_limit_ru(2,1,0,0,149)==42&&rtw8852b_txpwr_limit_ru(2,1,3,0,36)==0);
    maxdbm=10; assert(rtw8852b_txpwr_limit(2,0,0,0,0,0,1)==20&&rtw8852b_txpwr_limit_ru(2,1,0,0,149)==20); maxdbm=0;
    assert(rtw8852b_txpwr_ref_val(0,0)==((0xACu<<18)|(0x138u<<9))); }

static int chans[]={1,2,6,11,13,14,36,40,48,52,64,100,120,140,144,149,157,165,177};
static void test_apply(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO;
    static const char* ccs[]={"US","SE","JP","GB","CN","KR","AU","BR","ZZ","TW","CA","IN"};
    for(unsigned c=0;c<sizeof ccs/sizeof*ccs;c++)for(unsigned k=0;k<sizeof chans/sizeof*chans;k++){ u8 ch=(u8)chans[k], band=ch>14; cc=ccs[c];
        memset(regs,0,sizeof regs); regs[0xD200]=0xFF; regs[0xD201]=0x03; regs[0xD203]=0x0F; regs[0xD206]=0xF0; nwr=0;
        assert(rtw8852b_set_txpwr(&sc,ch)==0&&g_r89tx.ok&&g_r89tx.ch==ch);
        u8 rg=rtw89_txpwr_regd_of(cc,band); assert(g_r89tx.regd==rg);
        for(int n=0;n<2;n++){ u32 a=0xD2EC+n*40;
            for(int bf=0;bf<2;bf++){ assert(rb(a+bf)==rtw8852b_txpwr_limit(rg,band,0,n,0,bf,ch)); assert(rb(a+2+bf)==rtw8852b_txpwr_limit(rg,band,1,n,0,bf,ch));
                assert(rb(a+4+bf)==rtw8852b_txpwr_limit(rg,band,0,n,1,bf,ch)); assert(rb(a+6+bf)==rtw8852b_txpwr_limit(rg,band,0,n,2,bf,ch)); }
            for(int i=8;i<40;i++)assert(regs[a+i]==0);
            u32 r=0xD33C+n*24; for(int i=0;i<24;i++)assert(rb(r+i)==((i%8)?0:rtw8852b_txpwr_limit_ru(rg,band,(u8)(i/8),n,ch))); }
        signed char lc=rb(0xD2EC), lo=rb(0xD2F0);
        for(int i=0;i<4;i++){ assert(rb(0xD2C0+i)<=lc); assert(rb(0xD2C0+i)==(40<lc?40:lc)); }
        for(int i=0;i<8;i++){ assert(rb(0xD2C4+i)<=lo); assert(rb(0xD2C4+i)<=40); }
        for(int n=0;n<2;n++){ u32 a=0xD2CC+n*16; signed char lm=rb(0xD2EC+n*40+6);
            for(int i=0;i<16;i++){ assert(rb(a+i)<=lm); assert(rb(a+i)<=40); } }
        assert(rb(0xD2CC+8)==(32<rb(0xD2EC+6)?32:rb(0xD2EC+6)));
        assert((g32(0xD204)&0xFFFFF)==0&&(g32(0xD204)>>20)==0xF);
        assert((g32(0xD200)&0x0FFFFE00u)==0&&(g32(0xD200)&0x1FF)==0x1FF&&(g32(0xD200)>>28)==0);
        for(int p=0;p<2;p++){ assert((g32(0x10000+(p?0x7800:0x5800)+4)&0x7FFFFFF)==((0xACu<<18)|(0x138u<<9))); assert((g32(0x10000+(p?0x7800:0x5800)+8)&0x7FFFFFF)==((0xACu<<18)|(0x138u<<9))); }
        assert(nwr==11+1+20+12+1+4); }
    cc="US"; memset(regs,0,sizeof regs); assert(rtw8852b_set_txpwr(&sc,6)==0);
    assert(rb(0xD2C0)==39&&rb(0xD2C4)==39&&rb(0xD2CA)==38&&rb(0xD2CB)==36&&rb(0xD2D4)==32&&rb(0xD2D7)==26);
    nwr=0; assert(rtw8852b_set_txpwr(&sc,37)<0&&rtw8852b_set_txpwr(&sc,0)<0&&nwr==0); assert(rtw8852b_set_txpwr(0,6)<0);
    }

static void test_rxdck(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; memset(regs,0,sizeof regs); memset(rf,0,sizeof rf);
    for(int p=0;p<2;p++){ rf[p][0]=0x10000|p; rf[p][5]=0xABCD1|p; rf[p][0x92]=0x2|(p<<4); rf[p][0x93]=0xF; }
    rfn=0; dl=0; txmode=3; assert(rtw8852b_rfk_channel(&sc,36)==0&&g_r89tx.rfk_on&&strstr(g_r89tx.rfk_off,"IQK"));
    for(int p=0;p<2;p++){ assert(rf[p][5]==(0xABCD1u|(u32)p)&&rf[p][0x92]==(3u|((u32)p<<4))&&(rf[p][0x93]&0xF)==0&&((rf[p][0]>>16)&0xF)==3); }
    assert(rfn==16&&rflog[0][1]==5&&rflog[0][2]==(0xABCD0)&&rflog[1][1]==0x92&&rflog[1][2]==0&&rflog[2][1]==0&&rflog[4][1]==0x92&&(rflog[4][2]&1)==0&&(rflog[5][2]&1)==1&&rflog[6][1]==0x92&&rflog[6][2]==3&&rflog[7][1]==5);
    assert(dl>=2000&&dl<3000);
    rfn=0; dl=0; txmode=100000; assert(rtw8852b_rx_dck(&sc)==0&&dl<=25000);
    rfn=0; dl=0; txmode=0; swsi_dead=1; assert(rtw8852b_rfk_channel(&sc,36)<0&&!g_r89tx.rfk_on&&strstr(g_r89tx.rfk_off,"skipped")&&rfn==0&&dl<1000); swsi_dead=0; }

int main(void){ test_byrate(); test_regd(); test_limit(); test_apply(); test_rxdck(); printf("rtw89t host ok\n"); return 0; }
