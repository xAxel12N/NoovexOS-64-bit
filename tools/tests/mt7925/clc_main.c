#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
void mt_put16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
void mt_put32(u8* p,u32 v){ for(int i=0;i<4;i++)p[i]=(u8)(v>>(8*i)); }
void mt_zero(void* d,u32 n){ memset(d,0,n); }
u16 mt_get16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
void mt_copy(void* d,const void* s,u32 n){ memmove(d,s,n); }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void klog2(const char* a,const char* b){ printf("klog: %s%s\n",a,b); }
static u8* g_fw; static u32 g_fwn; static int g_fw_ok=1, g_encap=1, g_q_ok=1, g_send_fail;
static int fwread(const char* p,u8** o,u32* n){ (void)p; if(!g_fw_ok)return -1; *o=malloc(g_fwn); memcpy(*o,g_fw,g_fwn); *n=g_fwn; return 0; }
int (*mt7921_firmware_read)(const char*,u8**,u32*)=fwread;
static u8 qev[80];
int mt_mcu_query(struct mt7921_softc* sc,u32 cmd,const void* d,u32 l,const u8** o,int* ol){ (void)sc; const u8* r=d;
    assert(cmd==(MCU_UNI_CMD(MCU_UNI_CMD_EFUSE_CTRL)|MCU_FIELD_QUERY)&&l==28&&r[4]==1&&r[6]==24&&r[8]==0x70&&r[9]==0x0a);
    if(!g_q_ok)return -1; memset(qev,0,80); qev[48+1]=(u8)(0xf0|g_encap); *o=qev; *ol=80; return 0; }
static u8 sent[8][2048]; static u32 sentn[8]; static int nsent;
int mt7925_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* d,u32 l,int w){ (void)sc;(void)w; assert(cmd==MCU_UNI_CMD(MCU_UNI_CMD_SET_POWER_LIMIT)); if(g_send_fail)return -1; assert(nsent<8); memcpy(sent[nsent],d,l); sentn[nsent++]=l; return 0; }
static u32 rec(u8* o,u8 idx,u8 ver,u8 enc,const char* cc,int ncc){
    u32 ns=2; u8* d=o+16; memset(o,0,512); o[4]=idx; o[5]=ver; o[6]=(u8)ncc; o[7]=enc; o[8]=(u8)ns;
    u32 rules=16*ns, segs=rules+8u*(u32)ncc;
    for(u32 s=0;s<ns;s++){ u8* g=d+16*s; g[0]=(u8)s; mt_put32(g+4,segs+s*6); mt_put32(g+8,6); }
    mt_put32(d+4,segs);
    for(int i=0;i<ncc;i++){ u8* r=d+rules+8*i; r[0]=(u8)cc[2*i]; r[1]=(u8)cc[2*i+1]; r[2]=(u8)('a'+i); r[3]=0; r[4]=(u8)(1+i%2); r[5]=(u8)(0x10+i); }
    for(u32 s=0;s<ns;s++)for(int k=0;k<6;k++)d[segs+s*6+k]=(u8)(0xA0+s*16+k);
    u32 len=16+segs+ns*6; mt_put32(o,len); return len; }
static void build(void){
    static u8 f[4096]; u8 c[1024]; u32 cl=0; cl+=rec(c,0,2,1,"SEDEUSSEXX",5); c[16+32+32+4]=9; cl+=rec(c+cl,0,2,0,"JP",1); cl+=rec(c+cl,1,1,1,"SE00",2); cl+=rec(c+cl,3,1,1,"",0); mt_put32(c+cl,16); c[cl+4]=4; cl+=16;
    u32 o=0; memset(f,0,sizeof f); memset(f,0x11,100); o=100; memcpy(f+o,c,cl); o+=cl;
    struct mt_fw_region* r=(struct mt_fw_region*)(f+o); memset(r,0,80); r[0].len=100; r[1].len=cl; r[1].feature_set=FW_FEATURE_NON_DL; r[1].type=MT7925_FW_TYPE_CLC; o+=80;
    struct mt_fw_trailer* t=(struct mt_fw_trailer*)(f+o); memset(t,0,sizeof *t); t->n_region=2; o+=sizeof *t; g_fw=f; g_fwn=o; }
int main(void){
    build();
    u32 off,len; assert(mt7925_clc_find(g_fw,g_fwn,&off,&len)==0&&off==100);
    assert(mt7925_clc_find(g_fw,10,&off,&len)<0);
    const u8* rc[MT7925_CLC_NREC]; u32 rl[MT7925_CLC_NREC];
    assert(mt7925_clc_split(g_fw+off,len,1,rc,rl)==2&&rc[0]&&rc[1]&&rc[0][6]==5);
    assert(mt7925_clc_split(g_fw+off,len,0,rc,rl)==1&&rc[0][6]==1&&!rc[1]);
    assert(mt7925_clc_split(g_fw+off,len-3,1,rc,rl)<0);
    mt7925_clc_split(g_fw+off,len,1,rc,rl);
    char l[64]; assert(mt7925_clc_countries(rc[0],rl[0],l,sizeof l)==4&&!strcmp(l,"SE DE US XX"));
    assert(mt7925_clc_countries(rc[0],rl[0],l,4)==4&&!strcmp(l,"SE"));
    u8 q[256],fl=0; u32 n=mt7925_clc_req(rc[0],rl[0],0,"DE",MT7925_ENV_ANY,0,q,sizeof q,&fl);
    assert(n==MT7925_CLC_REQ+6&&mt_get16(q+4)==3&&mt_get16(q+6)==MT7925_CLC_REQ+6-4&&q[8]==2&&mt_get16(q+10)==6&&q[12]==0&&q[13]==0&&q[16]=='D'&&q[17]=='E'&&q[18]=='b'&&fl==0x11&&q[MT7925_CLC_REQ]==0xB0);
    assert(mt7925_clc_req(rc[0],rl[0],0,"SE",0,1,q,sizeof q,&fl)&&q[18]=='d'&&q[MT7925_CLC_REQ]==0xB0&&fl==0x13);
    assert(!mt7925_clc_req(rc[0],rl[0],0,"SE",0,2,q,sizeof q,&fl)&&!mt7925_clc_req(rc[0],rl[0],0,"FR",0,0,q,sizeof q,&fl));
    assert(!mt7925_clc_req(rc[0],rl[0],0,"DE",0,0,q,MT7925_CLC_REQ+5,&fl));
    assert(mt7925_clc_req(rc[1],rl[1],1,"SE",0,0,q,sizeof q,&fl)&&fl==0xff&&q[8]==1);
    struct mt7921_softc S; memset(&S,0,sizeof S); S.state=MT_ST_READY;
    g_q_ok=0; mt7925_clc_sync(&S,"SE"); assert(mt7925_clc_fail==1&&!nsent&&mt7925_clc_err); g_q_ok=1;
    mt7925_clc_sync(&S,"SE"); assert(mt7925_clc_sent==1&&nsent==3&&!strcmp(mt7925_clc_cc,"SE")&&sent[2][12]==1);
    mt7925_clc_sync(&S,"SE"); assert(nsent==3);
    nsent=0; mt7925_clc_sync(&S,"FR"); assert(nsent==1&&sent[0][12]==1&&sent[0][16]=='0'&&sent[0][13]==MT7925_ENV_INDOOR&&!strcmp(mt7925_clc_cc,"SE")&&mt7925_clc_fail==2&&mt7925_clc_err);
    nsent=0; mt7925_clc_sync(&S,"US"); assert(nsent==2&&sent[0][12]==0&&sent[0][16]=='U'&&sent[1][12]==1&&sent[1][16]=='0'&&sent[1][17]=='0'&&sent[1][13]==MT7925_ENV_INDOOR&&!strcmp(mt7925_clc_cc,"US")&&mt7925_clc_chan_conf==0xff);
    nsent=0; mt7925_clc_sync(&S,"XX"); assert(nsent==0&&mt7925_clc_fail==3&&!strcmp(mt7925_clc_cc,"US")); mt7925_clc_fail=1;
    assert(mt7925_clc_list(l,sizeof l)==4);
    g_send_fail=1; mt7925_clc_sync(&S,"DE"); mt7925_clc_sync(&S,"SE"); assert(mt7925_clc_fail==3); g_send_fail=0; nsent=0; mt7925_clc_sync(&S,"JP"); assert(!nsent);
    mt7925_clc_fail=0; mt7925_clc_sync(&S,"US"); nsent=0; mt7925_clc_sync(&S,"US"); assert(!nsent);
    mt7925_clc_reset(); assert(!mt7925_clc_sent&&!mt7925_clc_cc[0]); mt7925_clc_sync(&S,"US"); assert(nsent==2&&sent[0][16]=='U'&&mt7925_clc_sent==1&&!strcmp(mt7925_clc_cc,"US"));
    nsent=0; mt7925_clc_sync(&S,"US"); assert(!nsent); mt7925_clc_reset(); S.state=MT_ST_FWOK; mt7925_clc_sync(&S,"US"); assert(!nsent); S.state=MT_ST_READY; mt7925_clc_sync(&S,"US"); assert(nsent==2);
    u8 ch[3]={1,6,36},oc[64],ob[64]; assert(mt7925_scan_list(ch,3,oc,ob,64,0)==3&&ob[2]==MT7925_BAND_5G&&ob[0]==MT7925_BAND_2G);
    int k=mt7925_scan_list(ch,3,oc,ob,64,1); assert(k==18&&oc[3]==5&&ob[3]==MT7925_BAND_6G&&oc[17]==229);
    assert(mt7925_scan_list(ch,3,oc,ob,10,1)==10);
    puts("mt7925 clc: all passed"); return 0; }
