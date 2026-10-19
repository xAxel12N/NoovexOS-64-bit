#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/sys/linux/lxsock.h"
int (*g_bcastf)(void)=0; static u16 nd_bcast_flag(void){ return g_bcastf?(u16)(g_bcastf()):0x8000; }
void (*g_tx)(const u8*,int)=0; int (*g_rx)(u8*,int)=0;
const u8* (*g_mac)(void)=0; int (*g_linkok)(void)=0;
static void nd_tx(const u8* d,int n){ if(g_tx)g_tx(d,n); else nic_send(d,n); }
void nd_tx2(const u8* d,int n){ if(g_tx)g_tx(d,n); else nic_send(d,n); }
int nd_rx(u8* o,int m){ return g_rx?g_rx(o,m):nic_recv(o,m); }
const u8* nd_mymac(void){ return g_mac?g_mac():my_mac; }
static int nd_up(void){ return g_linkok?g_linkok():net_ok; }
int arp_auto_reply(const u8* f,int n){
    if(n<42||f[12]!=0x08||f[13]!=0x06)return 0; const u8* a=f+14;
    if(rd16(a)!=1||rd16(a+2)!=0x0800||rd16(a+6)!=1||!my_ip||rd32(a+24)!=my_ip)return 0;
    u8 o[60]; for(int i=0;i<60;i++)o[i]=0; int h=eth_hdr(o,a+8,0x0806); u8* r=o+h;
    be16(r,1); be16(r+2,0x0800); r[4]=6; r[5]=4; be16(r+6,2);
    const u8* mm=nd_mymac(); for(int i=0;i<6;i++){ r[8+i]=mm[i]; r[18+i]=a[8+i]; }
    be32(r+14,my_ip); for(int i=0;i<4;i++)r[24+i]=a[14+i];
    nd_tx2(o,60); return 1;
}
extern void (*g_wait_pump)(void);
static int nd_poll(u8* rx,int max,int ticks){ for(int k=0;k<ticks;k++){ if(g_wait_pump)g_wait_pump(); u16 a=pit_read(); for(;;){ int n=nd_rx(rx,max); if(n>0&&arp_auto_reply(rx,n))continue; if(n>0)return n; u16 b=pit_read(); if(((u16)(a-b))>=1193)break; } } return 0; }
int arp_resolve(u32 ip,u8* outmac){
    if(ip==gw_ip&&gw_known){ for(int i=0;i<6;i++)outmac[i]=gw_mac[i]; return 1; }
    arp_request(ip); u8 rx[1600];
    for(int tries=0;tries<20;tries++){ int n=nd_poll(rx,1600,1);
        if(n>=42&&rd16(rx+12)==0x0806&&rd16(rx+20)==2){ if(rd32(rx+28)==ip){ for(int i=0;i<6;i++)outmac[i]=rx[22+i];
            if(ip==gw_ip){ for(int i=0;i<6;i++)gw_mac[i]=outmac[i]; gw_known=1; } netlog("ARP reply received"); return 1; } } }
    netlog("ARP no reply"); return 0;
}
int do_ping(u32 tip){
    if(!nic_up())return -1;
    u32 route=((tip&my_mask)==(my_ip&my_mask))?tip:gw_ip;
    u8 dst[6]; if(!arp_resolve(route,dst))return 0;
    u8 f[98]; for(int i=0;i<98;i++)f[i]=0; int o=eth_hdr(f,dst,0x0800); u8* ip=f+o;
    ip[0]=0x45; ip[1]=0; be16(ip+2,28); be16(ip+4,0x1234); be16(ip+6,0); ip[8]=64; ip[9]=1;
    be32(ip+12,my_ip); be32(ip+16,tip); u16 ick=cksum(ip,20); ip[10]=ick>>8; ip[11]=(u8)ick;
    u8* ic=ip+20; ic[0]=8; ic[1]=0; be16(ic+4,0xABCD); be16(ic+6,1); u16 cck=cksum(ic,8); ic[2]=cck>>8; ic[3]=(u8)cck;
    nd_tx(f,o+28); u8 rx[1600]; u16 t0=pit_read();
    for(int tries=0;tries<150;tries++){ int n=nd_poll(rx,1600,1);
        if(n>=34&&rd16(rx+12)==0x0800){ u8* rip=rx+14; if(rip[9]==1){ u8* ric=rip+(rip[0]&0x0F)*4;
            if(ric[0]==0&&rd16(ric+4)==0xABCD){ u16 t1=pit_read(); ping_ms=(u16)(((u16)(t0-t1))/1193); netlog("ICMP reply received"); return 1; } } } }
    netlog("ICMP no reply"); return 0;
}
static u16 udp_cksum(u32 sip,u32 dip,const u8* udp,int udplen){
    u32 s=0; s+=(sip>>16)&0xFFFF; s+=sip&0xFFFF; s+=(dip>>16)&0xFFFF; s+=dip&0xFFFF; s+=17; s+=udplen;
    for(int i=0;i+1<udplen;i+=2)s+=((u32)udp[i]<<8)|udp[i+1]; if(udplen&1)s+=(u32)udp[udplen-1]<<8;
    while(s>>16)s=(s&0xFFFF)+(s>>16); u16 r=(u16)~s; if(r==0)r=0xFFFF; return r;
}
void udp_send(u32 dip,u16 sport,u16 dport,const u8* pl,int plen,const u8* dstmac){
    int lo=lxs_is_lo(dip); u32 sip=lo&&(dip>>24)==127?dip:my_ip;
    if((!lo&&!nd_up())||plen<0||plen>1500-14-20-8)return; u8 f[1500]; for(int i=0;i<14+20+8+plen&&i<1500;i++)f[i]=0;
    int o=eth_hdr(f,dstmac,0x0800); u8* ip=f+o; int iplen=20+8+plen;
    ip[0]=0x45; ip[1]=0; be16(ip+2,iplen); be16(ip+4,0x4321); be16(ip+6,0); ip[8]=64; ip[9]=17; be16(ip+10,0);
    be32(ip+12,sip); be32(ip+16,dip); u16 ick=cksum(ip,20); ip[10]=ick>>8; ip[11]=(u8)ick;
    u8* udp=ip+20; be16(udp+0,sport); be16(udp+2,dport); be16(udp+4,8+plen); be16(udp+6,0);
    for(int i=0;i<plen;i++)udp[8+i]=pl[i]; u16 uck=udp_cksum(sip,dip,udp,8+plen); udp[6]=uck>>8; udp[7]=(u8)uck;
    if(lo)lxs_lo_tx(f,o+iplen); else nd_tx(f,o+iplen);
}

char* ipstr(char* p,u32 v){ p=udec(p,(v>>24)&0xFF); *p++='.'; p=udec(p,(v>>16)&0xFF); *p++='.'; p=udec(p,(v>>8)&0xFF); *p++='.'; p=udec(p,v&0xFF); return p; }

static int dhcp_parse(const u8* d,int dlen,u32 xid,u8* mtype,u32* yi,u32* sid,u32* mk,u32* gw,u32* dn){
    if(dlen<240)return 0;
    if(d[0]!=2)return 0;
    if(rd32(d+4)!=xid)return 0;
    { const u8* mm=nd_mymac(); for(int i=0;i<6;i++) if(d[28+i]!=mm[i])return 0; }
    if(rd32(d+236)!=0x63825363u)return 0;
    *mtype=0; *yi=rd32(d+16); *sid=0; *mk=0; *gw=0; *dn=0;
    int o=240;
    while(o+1<dlen){
        u8 t=d[o];
        if(t==255)break;
        if(t==0){ o++; continue; }
        int l=d[o+1]; if(o+2+l>dlen)break;
        const u8* v=d+o+2;
        if(t==53&&l>=1) *mtype=v[0];
        else if(t==54&&l>=4) *sid=rd32(v);
        else if(t==1&&l>=4)  *mk=rd32(v);
        else if(t==3&&l>=4)  *gw=rd32(v);
        else if(t==6&&l>=4)  *dn=rd32(v);
        o+=2+l;
    }
    return 1;
}

static int dhcp_wait(u32 xid,u8 want,u32* yi,u32* sid,u32* mk,u32* gw,u32* dn,int ms){
    u8 rx[1600];
    for(int t=0;t<ms;t++){
        int n=nd_poll(rx,1600,1);
        if(n<282)continue;
        if(rd16(rx+12)!=0x0800)continue;
        const u8* ip=rx+14; if(ip[9]!=17)continue;
        int ihl=(ip[0]&0x0F)*4; const u8* udp=ip+ihl;
        if(rd16(udp+0)!=67||rd16(udp+2)!=68)continue;
        int ul=(int)rd16(udp+4); if(ul<8)continue;
        int dlen=ul-8; if(dlen>n-14-ihl-8)dlen=n-14-ihl-8;
        u8 mt=0;
        if(!dhcp_parse(udp+8,dlen,xid,&mt,yi,sid,mk,gw,dn))continue;
        if(mt!=want)continue;
        return 1;
    }
    return 0;
}

static int dhcp_build(u8* p,u32 xid,u8 type,u32 req_ip,u32 srvid){
    for(int i=0;i<300;i++)p[i]=0;
    p[0]=1; p[1]=1; p[2]=6; p[3]=0;
    be32(p+4,xid); be16(p+10,nd_bcast_flag());
    { const u8* mm=nd_mymac(); for(int i=0;i<6;i++)p[28+i]=mm[i]; }
    be32(p+236,0x63825363u);
    int n=240;
    p[n++]=53; p[n++]=1; p[n++]=type;
    if(req_ip){ p[n++]=50; p[n++]=4; be32(p+n,req_ip); n+=4; }
    if(srvid){ p[n++]=54; p[n++]=4; be32(p+n,srvid); n+=4; }
    p[n++]=55; p[n++]=4; p[n++]=1; p[n++]=3; p[n++]=6; p[n++]=15;
    p[n++]=255;
    while(n<300)p[n++]=0;
    return n;
}

int dhcp_config(void){
    if(!nd_up())return 0;
    static const u8 bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    u32 save_ip=my_ip, save_gw=gw_ip, save_dns=dns_ip, save_mask=my_mask;
    u8 pkt[300];
    for(int attempt=0;attempt<2;attempt++){
        u32 xid=net_rand(); if(!xid)xid=0x4E565831u;
        u32 yi=0,sid=0,mk=0,gw=0,dn=0;
        my_ip=0; my_mask=0;
        nic_flush_rx();
        int n=dhcp_build(pkt,xid,1,0,0);
        udp_send(0xFFFFFFFFu,68,67,pkt,n,bc);
        netlog("DHCP discover sent");
        if(!dhcp_wait(xid,2,&yi,&sid,&mk,&gw,&dn,900)){ netlog("DHCP no offer"); continue; }
        if(!yi){ netlog("DHCP offer without address"); continue; }
        { char b[52],*q=b; q=scopy(q,"DHCP offer "); q=ipstr(q,yi); *q=0; netlog(b); }
        u32 oyi=yi,omk=mk,ogw=gw,odn=dn;
        n=dhcp_build(pkt,xid,3,yi,sid);
        udp_send(0xFFFFFFFFu,68,67,pkt,n,bc);
        netlog("DHCP request sent");
        u32 ayi=0,asid=0,amk=0,agw=0,adn=0;
        if(!dhcp_wait(xid,5,&ayi,&asid,&amk,&agw,&adn,900)){ netlog("DHCP no ack"); continue; }
        if(ayi) oyi=ayi;
        if(amk) omk=amk;
        if(agw) ogw=agw;
        if(adn) odn=adn;
        my_ip=oyi;
        my_mask=omk?omk:0xFFFFFF00u;
        if(ogw) gw_ip=ogw;
        if(odn) dns_ip=odn;
        gw_known=0;
        g_dhcp_ok=1;
        { char b[52],*q=b; q=scopy(q,"DHCP ok ip="); q=ipstr(q,my_ip); q=scopy(q," gw="); q=ipstr(q,gw_ip); *q=0; netlog(b); }
        { char b[52],*q=b; q=scopy(q,"DHCP dns="); q=ipstr(q,dns_ip); *q=0; netlog(b); }
        nic_flush_rx();
        return 1;
    }
    my_ip=save_ip; gw_ip=save_gw; dns_ip=save_dns; my_mask=save_mask;
    g_dhcp_ok=0;
    { char b[52],*q=b; q=scopy(q,"DHCP failed - static ip="); q=ipstr(q,my_ip); *q=0; netlog(b); }
    return 0;
}

int g_netdev_wifi=0;
int nic_up(void){
    if(g_netdev_wifi)return g_linkok&&g_linkok();
    int r=nic_up_raw();
    if(r&&!g_dhcp_done){ g_dhcp_done=1; dhcp_config(); }
    return r;
}

int dhcp_renew(void){
    if(!nic_up_raw())return -1;
    g_dhcp_done=1;
    return dhcp_config();
}

static int dns_skipname(const u8* p,int off){ while(p[off]!=0){ if((p[off]&0xC0)==0xC0)return off+2; off+=p[off]+1; } return off+1; }
static int dns_build_query(const char* host,u16 txid,u8* q,int cap){
    if(cap<17)return -1;
    { int hl=0; while(host[hl]&&hl<256)hl++; if(hl>253)return -1; }
    int qp=12; be16(q+0,txid); be16(q+2,0x0100); be16(q+4,1); be16(q+6,0); be16(q+8,0); be16(q+10,0);
    const char* p=host; while(*p){ const char* s=p; int l=0; while(*p&&*p!='.'){p++;l++;} if(l>63)l=63; if(qp+1+l+5>cap)return -1; q[qp++]=(u8)l; for(int i=0;i<l;i++)q[qp++]=(u8)s[i]; if(*p=='.')p++; }
    if(qp+5>cap)return -1;
    q[qp++]=0; be16(q+qp,1); qp+=2; be16(q+qp,1); qp+=2;
    return qp;
}
static int dns_try(const char* host,u32 dns,u32* out_ip){
    if(!nic_up())return -1;
    u32 droute=((dns&my_mask)==(my_ip&my_mask))?dns:gw_ip;
    u8 dmac[6]; if(!arp_resolve(droute,dmac))return 0;
    u16 txid=(u16)net_rand(); if(txid==0)txid=0x1234; u16 sport=(u16)(0xC000u|(net_rand()&0x3FFFu));
    u8 q[300]; int qp=dns_build_query(host,txid,q,(int)sizeof(q)); if(qp<0)return -1; int qsec=qp-12;
    udp_send(dns,sport,53,q,qp,dmac); netlog("DNS query sent"); u8 rx[1600]; int seen=0,ltx=0,lqn=0;
    for(int tries=0;tries<1200;tries++){ int n=nd_poll(rx,1600,1);
        if(n<42)continue; if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=17)continue; if(rd32(ip+12)!=dns)continue;
        int ihl=(ip[0]&0x0F)*4; u8* udp=ip+ihl; if(rd16(udp+0)!=53||rd16(udp+2)!=sport)continue; u8* d=udp+8;
        if(!seen){ netlog("DNS reply arrived"); seen=1; }
        if(rd16(d+0)!=txid){ if(!ltx){netlog("DNS txid mismatch");ltx=1;} continue; } if(!(rd16(d+2)&0x8000))continue; if(rd16(d+4)!=1)continue;
        int qm=1; for(int i=0;i<qsec;i++)if(d[12+i]!=q[12+i]){qm=0;break;} if(!qm){ if(!lqn){netlog("DNS qname mismatch");lqn=1;} continue; }
        int anc=rd16(d+6); if(anc==0){ netlog("DNS no answer"); return 0; }
        int off=12; off=dns_skipname(d,off); off+=4;
        for(int a=0;a<anc;a++){ off=dns_skipname(d,off); u16 type=rd16(d+off); off+=2; off+=2; off+=4; u16 rdl=rd16(d+off); off+=2;
            if(type==1&&rdl==4){ *out_ip=rd32(d+off); netlog("DNS resolved"); return 1; } off+=rdl; }
        return 0; }
    netlog(seen?"DNS reply invalid":"DNS no reply"); return 0;
}
#define DNSC_N 8
static struct { char host[128]; u32 ip; int used; } dnsc[DNSC_N]; static int dnsc_next=0;
static int dnsc_get(const char*host,u32*ip){ for(int i=0;i<DNSC_N;i++) if(dnsc[i].used&&seq(dnsc[i].host,host)){ *ip=dnsc[i].ip; return 1; } return 0; }
static void dnsc_put(const char*host,u32 ip){ for(int i=0;i<DNSC_N;i++) if(dnsc[i].used&&seq(dnsc[i].host,host)){ dnsc[i].ip=ip; return; }
    int s=dnsc_next; dnsc_next=(dnsc_next+1)%DNSC_N; int n=0; while(host[n]&&n<127){dnsc[s].host[n]=host[n];n++;} dnsc[s].host[n]=0; dnsc[s].ip=ip; dnsc[s].used=1; }
int dns_query(const char* host,u32* out_ip){
    if(dnsc_get(host,out_ip)){ netlog("DNS cache hit"); return 1; }
    u32 srv[4]={dns_ip,0x08080808u,0x01010101u,0x0A000203u};
    for(int i=0;i<4;i++){ if(!srv[i])continue; if(i&&srv[i]==srv[0])continue; int r=dns_try(host,srv[i],out_ip); if(r==-1)return -1; if(r==1){ dnsc_put(host,*out_ip); return 1; } }
    return 0;
}

static u16 ipid=0x1000;
static u16 tcp_cksum(u32 sip,u32 dip,const u8* t,int len){
    u32 s=0; s+=(sip>>16)&0xFFFF; s+=sip&0xFFFF; s+=(dip>>16)&0xFFFF; s+=dip&0xFFFF; s+=6; s+=len;
    for(int i=0;i+1<len;i+=2)s+=((u32)t[i]<<8)|t[i+1]; if(len&1)s+=(u32)t[len-1]<<8;
    while(s>>16)s=(s&0xFFFF)+(s>>16); return (u16)~s;
}
u16 g_tcp_wnd=8192;
void tcp_send(u32 dip,u16 sp,u16 dp,const u8* dmac,u32 seq,u32 ack,u8 fl,const u8* data,int dl){
    int lo=lxs_is_lo(dip); u32 sip=lo&&(dip>>24)==127?dip:my_ip;
    if((!lo&&!nd_up())||dl<0||dl>1460)return; u8 f[1600]; int op=(fl&TF_SYN)?4:0; int tot=34+20+op+dl; for(int i=0;i<tot&&i<1600;i++)f[i]=0;
    int o=eth_hdr(f,dmac,0x0800); u8* ip=f+o; int iplen=20+20+op+dl;
    ip[0]=0x45; be16(ip+2,iplen); be16(ip+4,ipid++); ip[8]=64; ip[9]=6;
    be32(ip+12,sip); be32(ip+16,dip); u16 ick=cksum(ip,20); ip[10]=ick>>8; ip[11]=(u8)ick;
    u8* t=ip+20; be16(t+0,sp); be16(t+2,dp); be32(t+4,seq); be32(t+8,ack);
    t[12]=(u8)((5+op/4)<<4); t[13]=fl; be16(t+14,g_tcp_wnd);
    if(op){ t[20]=2; t[21]=4; be16(t+22,1400); }
    for(int i=0;i<dl;i++)t[20+op+i]=data[i];
    u16 ck=tcp_cksum(sip,dip,t,20+op+dl); t[16]=ck>>8; t[17]=(u8)ck; if(lo)lxs_lo_tx(f,o+iplen); else nd_tx(f,o+iplen);
}
static u32 g_ephctr=0x2B7A;
u16 next_ephport(void){ g_ephctr += (net_rand()|1u); return (u16)(20000 + (g_ephctr % 40000)); }
int tcp_get(u32 dip,u16 dport,const char* req,int reqlen,u8* out,int maxout){
    if(!nic_up())return -1;
    u32 route=((dip&my_mask)==(my_ip&my_mask))?dip:gw_ip;
    u8 dmac[6]; if(!arp_resolve(route,dmac))return -2;
    nic_kick(); nic_flush_rx();
    u16 sp=next_ephport(); u32 snd=net_rand(); u32 rcv=0;
    u8 rx[1600]; int est=0;
    for(int att=0;att<4&&!est;att++){ tcp_send(dip,sp,dport,dmac,snd,0,TF_SYN,0,0);
        for(int k=0;k<300;k++){ int n=nd_poll(rx,1600,1);
            if(n<54)continue; if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=6)continue;
            int ihl=(ip[0]&0x0F)*4; u8* t=ip+ihl; if(rd16(t+0)!=dport||rd16(t+2)!=sp)continue; u8 fl=t[13];
            if((fl&(TF_SYN|TF_ACK))==(TF_SYN|TF_ACK)&&rd32(t+8)==snd+1){ rcv=rd32(t+4)+1; snd+=1; est=1; break; }
            if(fl&TF_RST){ netlog("TCP RST"); return -4; } } }
    if(!est){ netlog("TCP no SYN-ACK"); return -3; }
    netlog("TCP established");
    tcp_send(dip,sp,dport,dmac,snd,rcv,TF_ACK,0,0);
    u32 getseq=snd; tcp_send(dip,sp,dport,dmac,getseq,rcv,TF_PSH|TF_ACK,(const u8*)req,reqlen); snd=getseq+reqlen;
    netlog("HTTP GET sent");
    int outlen=0,done=0,idle=0,rexmit=0;
    while(!done&&outlen<maxout){ int n=nd_poll(rx,1600,1);
        if(n<=0){ if(++idle>1800)break; if(outlen==0&&(idle%450)==0&&rexmit<3){ tcp_send(dip,sp,dport,dmac,getseq,rcv,TF_PSH|TF_ACK,(const u8*)req,reqlen); rexmit++; } continue; }
        if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=6)continue;
        int ihl=(ip[0]&0x0F)*4; u8* t=ip+ihl; if(rd16(t+0)!=dport||rd16(t+2)!=sp)continue;
        int iptot=rd16(ip+2); int thl=((t[12]>>4)&0xF)*4; int dl=iptot-ihl-thl; if(dl<0)dl=0;
        u32 seq=rd32(t+4); u8 fl=t[13];
        if(fl&TF_RST){ done=1; break; }
        if(dl>0){ if(seq==rcv){ int cp=dl; if(outlen+cp>maxout)cp=maxout-outlen; for(int i=0;i<cp;i++)out[outlen+i]=t[thl+i]; outlen+=cp; rcv+=dl; }
            tcp_send(dip,sp,dport,dmac,snd,rcv,TF_ACK,0,0); idle=0; }
        if(fl&TF_FIN){ rcv+=1; tcp_send(dip,sp,dport,dmac,snd,rcv,TF_FIN|TF_ACK,0,0); snd+=1; done=1; } }
    netlog("HTTP done"); return outlen;
}