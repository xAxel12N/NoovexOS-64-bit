#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#define LXS_BLOCK (-1000)

#define LXS_LO 0x7F000001u

struct lxsock g_lxs[NLXS];

#define LO_SLOTS 256
#define LO_FRAME 1536
static u8* lo_buf; static u16 lo_len[LO_SLOTS]; static u32 lo_head, lo_tail;
int lxs_is_lo(u32 ip){ return (ip>>24)==127||(my_ip&&ip==my_ip); }
void lxs_lo_tx(const u8* f,int n){
    if(n<=0||n>LO_FRAME)return;
    if(!lo_buf){ lo_buf=(u8*)kmalloc(LO_SLOTS*LO_FRAME); if(!lo_buf)return; }
    if(lo_head-lo_tail>=LO_SLOTS)return;
    u32 k=lo_head%LO_SLOTS; u8* d=lo_buf+k*LO_FRAME; for(int i=0;i<n;i++)d[i]=f[i]; lo_len[k]=(u16)n; lo_head++;
}

static void lxs_mv(u8* d,const u8* s,int n){ for(int i=0;i<n;i++)d[i]=s[i]; }
static int lxs_route_mac(u32 dip,u8* mac){
    if(dip==0xFFFFFFFFu){ for(int i=0;i<6;i++)mac[i]=0xFF; return 1; }
    u32 route=((dip&my_mask)==(my_ip&my_mask))?dip:gw_ip;
    return arp_resolve(route,mac);
}
int lxs_new(int type){
    for(int i=0;i<NLXS;i++) if(!g_lxs[i].used){
        struct lxsock* s=&g_lxs[i]; u8* z=(u8*)s; for(int k=0;k<(int)sizeof *s;k++)z[k]=0;
        s->rx=(u8*)kmalloc(LXS_RXCAP); if(!s->rx)return -12;
        if(type==1){ s->tx=(u8*)kmalloc(LXS_TXCAP); if(!s->tx){ kfree(s->rx); s->rx=0; return -12; } }
        s->used=1; s->type=type; s->pwin=8192; s->parent=-1; return i; }
    return -24;
}
static struct lxsock* lxs_get(int i){ return (i>=0&&i<NLXS&&g_lxs[i].used)?&g_lxs[i]:0; }
static int lxs_port_busy(int type,u16 p){ for(int i=0;i<NLXS;i++) if(g_lxs[i].used&&g_lxs[i].type==type&&g_lxs[i].lport==p)return 1; return 0; }
static u16 lxs_eph(int type){ for(int k=0;k<64;k++){ u16 p=next_ephport(); if(!lxs_port_busy(type,p))return p; } return next_ephport(); }

static void lxs_seg(struct lxsock* s,u32 seq,u8 fl,const u8* d,int n){ int fr=LXS_RXCAP-s->rxn; if(fr>65535)fr=65535; if(fr<0)fr=0; g_tcp_wnd=(u16)fr; tcp_send(s->rip,s->lport,s->rport,s->rmac,seq,s->rcv,fl,d,n); g_tcp_wnd=8192; }
static void lxs_ack(struct lxsock* s){ lxs_seg(s,s->nxt,TF_ACK,0,0); }

static void lxs_enqueue(struct lxsock* s){
    int me=(int)(s-g_lxs); struct lxsock* L=lxs_get(s->parent);
    if(L&&L->st==LXS_LISTEN&&L->qn<LXS_QMAX){ L->q[L->qn++]=me; return; }
    lxs_seg(s,s->nxt,TF_RST|TF_ACK,0,0); s->st=LXS_DEAD; s->parent=-2;
}
static void lxs_tcp_in(struct lxsock* s,u8* t,int thl,int dl,u32 seq,u32 ack,u8 fl){
    if(fl&TF_RST){ if(s->st==LXS_SYNRCVD){ s->st=LXS_DEAD; s->parent=-2; return; } if(s->st==LXS_SYN||s->st==LXS_EST){ s->err=(s->st==LXS_SYN)?111:104; s->st=LXS_DEAD; } return; }
    if(s->st==LXS_SYNRCVD){
        if((fl&TF_SYN)&&!(fl&TF_ACK)){ lxs_seg(s,s->iss,TF_SYN|TF_ACK,0,0); return; }
        if(!(fl&TF_ACK)||ack!=s->iss+1)return;
        s->una=s->nxt=s->iss+1; s->pwin=rd16(t+14); s->st=LXS_EST; s->tries=0; s->tlast=g_ticks;
        lxs_enqueue(s); if(s->st!=LXS_EST)return; }
    if(s->st==LXS_SYN){
        if((fl&(TF_SYN|TF_ACK))==(TF_SYN|TF_ACK)&&ack==s->iss+1){ s->rcv=seq+1; s->una=s->nxt=s->iss+1; s->pwin=rd16(t+14); s->st=LXS_EST; s->tries=0; s->tlast=g_ticks; lxs_ack(s); }
        return; }
    if(s->st!=LXS_EST)return;
    if(fl&TF_SYN){ lxs_ack(s); return; }
    if(fl&TF_ACK){
        s->pwin=rd16(t+14);
        u32 adv=ack-s->una; u32 out=s->nxt-s->una;
        if(adv>0&&adv<=out){
            u32 dbytes=adv; if(s->fin_out&&ack==s->nxt)dbytes--;
            if((int)dbytes>s->txn)dbytes=(u32)s->txn;
            if(dbytes){ lxs_mv(s->tx,s->tx+dbytes,s->txn-(int)dbytes); s->txn-=(int)dbytes; }
            s->una=ack; s->tries=0; s->tlast=g_ticks; } }
    int need_ack=0;
    if(dl>0){
        if(seq==s->rcv&&!s->rd_shut&&s->rxn+dl<=LXS_RXCAP){ lxs_mv(s->rx+s->rxn,t+thl,dl); s->rxn+=dl; s->rcv+=(u32)dl; }
        else if(seq==s->rcv&&s->rd_shut){ s->rcv+=(u32)dl; }
        need_ack=1; }
    if((fl&TF_FIN)&&seq+(u32)dl==s->rcv&&!s->fin_in){ s->rcv+=1; s->fin_in=1; need_ack=1; }
    if(need_ack)lxs_ack(s);
}
static void lxs_udp_in(u32 sip,u16 sport,u16 dport,const u8* d,int dl){
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=2||s->lport!=dport)continue;
        u32 from=sip;
        if(s->lo53&&sip==dns_ip&&sport==53)from=LXS_LO;
        if(s->conn&&(s->rip!=from||s->rport!=sport))continue;
        if(s->rd_shut)return;
        if(s->rxn+8+dl>LXS_RXCAP)return;
        u8* r=s->rx+s->rxn; be32(r,from); be16(r+4,sport); be16(r+6,(u16)dl); lxs_mv(r+8,d,dl); s->rxn+=8+dl;
        return; }
}
static void lxs_frame(u8* f,int n);
static void lxs_lo_drain(void){
    u8 f[LO_FRAME];
    for(int g=0;g<LO_SLOTS*2&&lo_tail!=lo_head;g++){ u32 k=lo_tail%LO_SLOTS; int n=lo_len[k]; u8* d=lo_buf+k*LO_FRAME; for(int i=0;i<n;i++)f[i]=d[i]; lo_tail++; lxs_frame(f,n); }
}
static void lxs_frame(u8* f,int n){
    if(n<34)return; u16 et=rd16(f+12);
    if(et==0x0806){ arp_auto_reply(f,n); return; }
    if(et!=0x0800)return;
    u8* ip=f+14; int ihl=(ip[0]&0x0F)*4; if(ihl<20)return; int tot=rd16(ip+2); if(tot<ihl||14+tot>n)return;
    if(rd16(ip+6)&0x3FFF)return;
    u32 sip=rd32(ip+12), dip=rd32(ip+16);
    if(!lxs_is_lo(dip)&&dip!=0xFFFFFFFFu&&dip!=(my_ip|~my_mask))return;
    u8* p=ip+ihl; int plen=tot-ihl;
    if(ip[9]==17){ if(plen<8)return; int ul=rd16(p+4); if(ul<8||ul>plen)return; lxs_udp_in(sip,rd16(p),rd16(p+2),p+8,ul-8); return; }
    if(ip[9]!=6||plen<20)return;
    int thl=((p[12]>>4)&0xF)*4; if(thl<20||thl>plen)return;
    u16 sp=rd16(p), dp=rd16(p+2);
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=1||s->st==LXS_LISTEN||s->lport!=dp||s->rport!=sp||s->rip!=sip)continue;
        lxs_tcp_in(s,p,thl,plen-thl,rd32(p+4),rd32(p+8),p[13]); return; }
    if((p[13]&(TF_SYN|TF_ACK|TF_RST))!=TF_SYN)return;
    for(int i=0;i<NLXS;i++){ struct lxsock* L=&g_lxs[i]; if(!L->used||L->type!=1||L->st!=LXS_LISTEN||L->lport!=dp)continue;
        int pend=L->qn; for(int k=0;k<NLXS;k++) if(g_lxs[k].used&&g_lxs[k].st==LXS_SYNRCVD&&g_lxs[k].parent==i)pend++;
        if(pend>=L->backlog)return;
        int c=lxs_new(1); if(c<0)return;
        struct lxsock* s=&g_lxs[c];
        s->lport=dp; s->rport=sp; s->rip=sip; for(int k=0;k<6;k++)s->rmac[k]=f[6+k]; s->have_mac=1; s->conn=1;
        s->iss=net_rand(); s->una=s->iss; s->nxt=s->iss+1; s->rcv=rd32(p+4)+1; s->pwin=rd16(p+14);
        s->st=LXS_SYNRCVD; s->parent=i; s->tries=0; s->tlast=g_ticks;
        lxs_seg(s,s->iss,TF_SYN|TF_ACK,0,0);
        return; }
    if(lxs_is_lo(dip)) tcp_send(sip,dp,sp,f+6,0,rd32(p+4)+1+(u32)(plen-thl),TF_RST|TF_ACK,0,0);
}
static void lxs_push(struct lxsock* s){
    if(s->st!=LXS_EST)return;
    for(;;){ int sent=(int)(s->nxt-s->una); if(s->fin_out)break;
        int left=s->txn-sent; if(left<=0)break;
        int win=(int)s->pwin-sent; if(win<=0)break;
        int c=left; if(c>1400)c=1400; if(c>win)c=win;
        lxs_seg(s,s->nxt,TF_PSH|TF_ACK,s->tx+sent,c); s->nxt+=(u32)c; s->tlast=g_ticks; }
    if(s->wr_shut&&!s->fin_out&&s->txn==(int)(s->nxt-s->una)){ lxs_seg(s,s->nxt,TF_FIN|TF_ACK,0,0); s->nxt+=1; s->fin_out=1; s->tlast=g_ticks; }
}
static void lxs_timers(void){
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=1)continue;
        if(s->st==LXS_SYNRCVD&&g_ticks-s->tlast>=100){
            if(++s->tries>5){ lxs_release(i); continue; }
            lxs_seg(s,s->iss,TF_SYN|TF_ACK,0,0); s->tlast=g_ticks; continue; }
        if(s->st==LXS_DEAD&&s->parent==-2){ lxs_release(i); continue; }
        if(s->st==LXS_SYN&&g_ticks-s->tlast>=100){
            if(++s->tries>5){ s->err=110; s->st=LXS_DEAD; continue; }
            lxs_seg(s,s->iss,TF_SYN,0,0); s->tlast=g_ticks; }
        else if(s->st==LXS_EST&&s->nxt!=s->una&&g_ticks-s->tlast>=(30ULL<<(s->tries<4?s->tries:4))){
            if(++s->tries>10){ s->err=110; s->st=LXS_DEAD; continue; }
            int f=s->fin_out; s->nxt=s->una; s->fin_out=0; lxs_push(s); if(f&&!s->fin_out){ s->wr_shut=1; lxs_push(s); } s->tlast=g_ticks; }
        else if(s->st==LXS_EST)lxs_push(s); }
}
void lxs_pump(void){
    u8 f[1600];
    lxs_lo_drain();
    for(int k=0;k<128;k++){ int n=nd_rx(f,1600); if(n<=0)break; lxs_frame(f,n); }
    lxs_timers();
    lxs_lo_drain();
}
void lxs_release(int i){
    struct lxsock* s=lxs_get(i); if(!s)return;
    if(s->st==LXS_LISTEN){
        for(int k=0;k<s->qn;k++){ g_lxs[s->q[k]].parent=-1; lxs_release(s->q[k]); }
        s->qn=0;
        for(int k=0;k<NLXS;k++) if(g_lxs[k].used&&g_lxs[k].st==LXS_SYNRCVD&&g_lxs[k].parent==i){ lxs_seg(&g_lxs[k],g_lxs[k].iss+1,TF_RST,0,0); g_lxs[k].used=0; if(g_lxs[k].rx)kfree(g_lxs[k].rx); if(g_lxs[k].tx)kfree(g_lxs[k].tx); g_lxs[k].rx=g_lxs[k].tx=0; } }
    if(s->type==1&&s->st==LXS_EST){
        s->wr_shut=1;
        for(int ms=0;ms<1500&&s->st==LXS_EST&&(s->una!=s->nxt||!s->fin_out);ms++){
            if(ms&&ms%300==0){ s->nxt=s->una; s->fin_out=0; }
            lxs_pump(); lxs_push(s); u16 a=pit_read(); while((u16)(a-pit_read())<1193){} }
        if(s->st==LXS_EST&&!s->fin_in)lxs_seg(s,s->nxt,TF_RST|TF_ACK,0,0); }
    else if(s->type==1&&s->st==LXS_SYN){ tcp_send(s->rip,s->lport,s->rport,s->rmac,s->iss+1,0,TF_RST,0,0); }
    if(s->rx)kfree(s->rx); if(s->tx)kfree(s->tx); s->rx=s->tx=0; s->used=0;
}

int lxs_connect(int i,u32 dip,u16 dport,int nb){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->type==2){
        if(!dip&&!dport){ s->conn=0; return 0; }
        s->lo53=(dip==LXS_LO&&dport==53&&dns_ip);
        u32 real=s->lo53?dns_ip:dip; if(!real)return -111;
        if(lxs_is_lo(real)){ for(int k=0;k<6;k++)s->rmac[k]=0; }
        else { if(!nic_up())return -101; if(!lxs_route_mac(real,s->rmac))return -113; }
        s->have_mac=1;
        if(!s->lport)s->lport=lxs_eph(2);
        s->rip=dip; s->rport=dport; s->conn=1; return 0; }
    lxs_pump();
    if(s->st==LXS_EST)return s->conn?-106:(s->conn=1,0);
    if(s->st==LXS_DEAD){ int e=s->err?s->err:111; s->err=0; return -e; }
    if(s->st==LXS_SYN){ if(nb)return -114; return LXS_BLOCK; }
    if(!dip)dip=LXS_LO;
    if(lxs_is_lo(dip)){ for(int k=0;k<6;k++)s->rmac[k]=0; }
    else { if(!nic_up())return -101; if(!lxs_route_mac(dip,s->rmac))return -113; }
    s->have_mac=1;
    if(!s->lport)s->lport=lxs_eph(1);
    s->rip=dip; s->rport=dport; s->iss=net_rand(); s->una=s->nxt=s->iss; s->st=LXS_SYN; s->tries=0; s->tlast=g_ticks;
    lxs_seg(s,s->iss,TF_SYN,0,0);
    return nb?-115:LXS_BLOCK;
}
int lxs_send(int i,const u8* b,int n,u32 dip,u16 dport,int has_to){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->type==2){
        if(s->wr_shut)return -32;
        if(n>1472)return -90;
        u32 to=s->rip; u16 tp=s->rport; const u8* mac=s->rmac; u8 m2[6];
        if(has_to){ to=dip; tp=dport; } else if(!s->conn)return -89;
        int lo=(to==LXS_LO&&tp==53&&dns_ip); u32 real=lo?dns_ip:to;
        if(!real)return -111;
        if(lo)s->lo53=1;
        if(lxs_is_lo(real)){ for(int k=0;k<6;k++)m2[k]=0; mac=m2; }
        else { if(!nic_up())return -101; if(has_to||!s->have_mac){ if(!lxs_route_mac(real,m2))return -113; mac=m2; } }
        if(!s->lport)s->lport=lxs_eph(2);
        lxs_pump();
        udp_send(real,s->lport,tp,b,n,mac); return n; }
    lxs_pump();
    if(s->st==LXS_DEAD){ int e=s->err; s->err=0; return e?-e:-32; }
    if(s->st!=LXS_EST)return s->st==LXS_SYN?LXS_BLOCK:-107;
    if(s->wr_shut)return -32;
    int room=LXS_TXCAP-s->txn; if(room<=0)return LXS_BLOCK;
    if(n>room)n=room;
    lxs_mv(s->tx+s->txn,b,n); s->txn+=n; lxs_push(s);
    return n;
}
int lxs_recv(int i,u8* b,int n,int peek,u32* from,u16* fport){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    lxs_pump();
    if(s->type==2){
        if(!s->rxn)return s->rd_shut?0:LXS_BLOCK;
        u8* r=s->rx; int dl=rd16(r+6); if(from)*from=rd32(r); if(fport)*fport=rd16(r+4);
        int c=dl<n?dl:n; lxs_mv(b,r+8,c);
        if(!peek){ int t=8+dl; lxs_mv(s->rx,s->rx+t,s->rxn-t); s->rxn-=t; }
        return c; }
    if(from)*from=s->rip; if(fport)*fport=s->rport;
    if(s->rxn>0){ int c=s->rxn<n?s->rxn:n; lxs_mv(b,s->rx,c);
        if(!peek){ int was_full=s->rxn+1600>LXS_RXCAP; lxs_mv(s->rx,s->rx+c,s->rxn-c); s->rxn-=c; if(was_full&&s->st==LXS_EST)lxs_ack(s); }
        return c; }
    if(s->fin_in||s->rd_shut)return 0;
    if(s->st==LXS_DEAD){ int e=s->err; s->err=0; return e?-e:0; }
    if(s->st==LXS_NEW)return -107;
    return LXS_BLOCK;
}
int lxs_shutdown(int i,int how){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->type==1&&s->st!=LXS_EST&&s->st!=LXS_DEAD)return -107;
    if(how==0||how==2){ s->rd_shut=1; s->rxn=0; }
    if(how==1||how==2){ s->wr_shut=1; if(s->type==1){ lxs_pump(); lxs_push(s); } }
    return 0;
}
int lxs_poll(int i){
    struct lxsock* s=lxs_get(i); if(!s)return 0x20;
    lxs_pump(); int re=0;
    if(s->st==LXS_LISTEN)return s->qn?1:0;
    if(s->type==2){ re|=4; if(s->rxn||s->rd_shut)re|=1; return re; }
    if(s->rxn||s->fin_in||s->rd_shut)re|=1;
    if(s->st==LXS_EST&&!s->wr_shut&&s->txn<LXS_TXCAP)re|=4;
    if(s->st==LXS_DEAD){ re|=1|4; if(s->err)re|=8; re|=0x10; }
    if(s->fin_in&&s->wr_shut)re|=0x10;
    if(s->fin_in)re|=0x2000;
    return re;
}
int lxs_bind(int i,u16 port){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->lport)return -22;
    if(port){ if(lxs_port_busy(s->type,port))return -98; s->lport=port; }
    else s->lport=lxs_eph(s->type);
    return 0;
}
int lxs_listen(int i,int backlog){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->type!=1)return -95;
    if(s->st==LXS_LISTEN){ s->backlog=backlog<1?1:backlog>LXS_QMAX?LXS_QMAX:backlog; return 0; }
    if(s->st!=LXS_NEW)return -22;
    if(!s->lport)s->lport=lxs_eph(1);
    s->backlog=backlog<1?1:backlog>LXS_QMAX?LXS_QMAX:backlog; s->qn=0; s->st=LXS_LISTEN;
    return 0;
}
int lxs_accept(int i){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->st!=LXS_LISTEN)return -22;
    lxs_pump();
    if(!s->qn)return LXS_BLOCK;
    int c=s->q[0]; for(int k=1;k<s->qn;k++)s->q[k-1]=s->q[k]; s->qn--;
    g_lxs[c].parent=-1; g_lxs[c].conn=0;
    return c;
}
