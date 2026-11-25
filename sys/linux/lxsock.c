#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/sys/bpf/bpf_attach.h"
#include "include/nvx/net/stack/firewall.h"
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

typedef u64 __attribute__((aligned(1),may_alias)) lxs_u64u;
static void lxs_mv(u8* d,const u8* s,int n){ int i=0; if(d+8<=s||d>=s+n){ for(;i+8<=n;i+=8)*(lxs_u64u*)(d+i)=*(const lxs_u64u*)(s+i); } for(;i<n;i++)d[i]=s[i]; }
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
static int lxs_clash(struct lxsock* me,u16 p){
    for(int i=0;i<NLXS;i++){ struct lxsock* o=&g_lxs[i]; if(!o->used||o==me||o->type!=me->type||o->lport!=p||o->icmp!=me->icmp)continue;
        if(me->reuseport&&o->reuseport)continue;
        if(me->type==1&&me->reuseaddr&&o->st!=LXS_LISTEN&&(o->st!=LXS_NEW||o->reuseaddr))continue;
        if(me->type==2&&me->reuseaddr&&o->reuseaddr)continue;
        return 1; }
    return 0;
}
static u16 lxs_eph(int type){ for(int k=0;k<64;k++){ u16 p=next_ephport(); if(!lxs_port_busy(type,p))return p; } return next_ephport(); }

extern int g_tcp_syn_ws;
static void lxs_seg(struct lxsock* s,u32 seq,u8 fl,const u8* d,int n){ int fr=LXS_RXCAP-s->rxn; if(fr<0)fr=0; if(s->ws_ok&&!(fl&TF_SYN))fr>>=LXS_WS; if(fr>65535)fr=65535; g_tcp_wnd=(u16)fr; if((fl&TF_SYN)&&!(fl&TF_ACK)&&s->st==LXS_SYN)g_tcp_syn_ws=LXS_WS; tcp_send(s->rip,s->lport,s->rport,s->rmac,seq,s->rcv,fl,d,n); g_tcp_syn_ws=-1; g_tcp_wnd=8192; }
static void lxs_ack(struct lxsock* s){ lxs_seg(s,s->nxt,TF_ACK,0,0); }

static void lxs_enqueue(struct lxsock* s){
    int me=(int)(s-g_lxs); struct lxsock* L=lxs_get(s->parent);
    if(L&&L->st==LXS_LISTEN&&L->qn<LXS_QMAX){ L->q[L->qn++]=me; return; }
    lxs_seg(s,s->nxt,TF_RST|TF_ACK,0,0); s->st=LXS_DEAD; s->parent=-2;
}
static void lxs_tcp_in(struct lxsock* s,u8* t,int thl,int dl,u32 seq,u32 ack,u8 fl){
    if(fl&TF_RST){
        if(s->st==LXS_SYN){ if((fl&TF_ACK)&&ack==s->iss+1){ s->err=111; s->st=LXS_DEAD; } return; }
        if(s->st!=LXS_SYNRCVD&&s->st!=LXS_EST)return;
        if(seq!=s->rcv){ u32 wnd=(u32)(LXS_RXCAP-s->rxn); if(s->st==LXS_EST&&seq-s->rcv<wnd)lxs_ack(s); return; }
        if(s->st==LXS_SYNRCVD){ s->st=LXS_DEAD; s->parent=-2; return; }
        s->err=104; s->st=LXS_DEAD; return; }
    if(s->st==LXS_SYNRCVD){
        if((fl&TF_SYN)&&!(fl&TF_ACK)){ lxs_seg(s,s->iss,TF_SYN|TF_ACK,0,0); return; }
        if(!(fl&TF_ACK)||ack!=s->iss+1)return;
        s->una=s->nxt=s->iss+1; s->pwin=rd16(t+14); s->st=LXS_EST; s->tries=0; s->tlast=g_ticks;
        lxs_enqueue(s); if(s->st!=LXS_EST)return; }
    if(s->st==LXS_SYN){
        if((fl&(TF_SYN|TF_ACK))==(TF_SYN|TF_ACK)&&ack==s->iss+1){ for(int o=20;o+1<thl;){ u8 k=t[o]; if(k==0)break; if(k==1){ o++; continue; } u8 l=t[o+1]; if(l<2||o+l>thl)break; if(k==3&&l==3){ s->ws_ok=1; s->pws=t[o+2]>14?14:t[o+2]; } o+=l; } s->rcv=seq+1; s->una=s->nxt=s->iss+1; s->pwin=rd16(t+14); s->st=LXS_EST; s->tries=0; s->tlast=g_ticks; lxs_ack(s); }
        return; }
    if(s->st!=LXS_EST)return;
    if(fl&TF_SYN){ lxs_ack(s); return; }
    if(fl&TF_ACK){
        s->pwin=s->ws_ok?((u32)rd16(t+14)<<s->pws):rd16(t+14);
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
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=2||s->icmp||s->lport!=dport)continue;
        u32 from=sip;
        if(s->lo53&&sip==dns_ip&&sport==53)from=LXS_LO;
        if(s->conn&&(s->rip!=from||s->rport!=sport))continue;
        if(s->rd_shut)return;
        if(s->rxn+8+dl>LXS_RXCAP)return;
        u8* r=s->rx+s->rxn; be32(r,from); be16(r+4,sport); be16(r+6,(u16)dl); lxs_mv(r+8,d,dl); s->rxn+=8+dl;
        return; }
}
static void lxs_frame(u8* f,int n);
static int lxs_in_lo;
static void lxs_lo_drain(void){
    u8 f[LO_FRAME];
    for(int g=0;g<LO_SLOTS*2&&lo_tail!=lo_head;g++){ u32 k=lo_tail%LO_SLOTS; int n=lo_len[k]; u8* d=lo_buf+k*LO_FRAME; for(int i=0;i<n;i++)f[i]=d[i]; lo_tail++; lxs_in_lo=1; lxs_frame(f,n); lxs_in_lo=0; }
}
static void lxs_icmp_in(u8* f,int n,u8* ip,int ihl,u8* p,int plen,u32 sip,u32 dip){
    if(plen<8)return;
    if(p[0]==8&&p[1]==0&&lxs_in_lo){
        u8 g[LO_FRAME]; if(n>LO_FRAME)return; for(int i=0;i<n;i++)g[i]=f[i];
        u8* gi=g+14; be32(gi+12,dip); be32(gi+16,sip); gi[10]=gi[11]=0; u16 ick=cksum(gi,ihl); gi[10]=(u8)(ick>>8); gi[11]=(u8)ick;
        u8* gp=gi+ihl; gp[0]=0; gp[2]=gp[3]=0; u16 c=cksum(gp,plen); gp[2]=(u8)(c>>8); gp[3]=(u8)c;
        lxs_lo_tx(g,14+ihl+plen); return; }
    if(p[0]!=0)return;
    u16 id=rd16(p+4);
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=2||!s->icmp||s->lport!=id)continue;
        if(s->conn&&s->rip!=sip)continue;
        if(s->rd_shut||s->rxn+8+plen>LXS_RXCAP)return;
        u8* r=s->rx+s->rxn; be32(r,sip); be16(r+4,0); be16(r+6,(u16)plen); lxs_mv(r+8,p,plen); s->rxn+=8+plen; return; }
}
static int lxs_icmp_send(struct lxsock* s,const u8* b,int n,u32 dip){
    if(n<8)return -22; if(n>1472)return -90; if(b[0]!=8||b[1]!=0)return -22;
    if(!dip)return -22;
    int lo=lxs_is_lo(dip); u8 mac[6]={0,0,0,0,0,0};
    if(!lo){ if(!nic_up())return -101; if(!lxs_route_mac(dip,mac))return -113; }
    if(!s->lport){ for(int k=0;k<64;k++){ u16 q=next_ephport(); int busy=0; for(int j=0;j<NLXS;j++) if(g_lxs[j].used&&g_lxs[j].icmp&&g_lxs[j].lport==q)busy=1; if(!busy){ s->lport=q; break; } } if(!s->lport)s->lport=next_ephport(); }
    u8 f[1514]; for(int i=0;i<34+n;i++)f[i]=0;
    int o=eth_hdr(f,mac,0x0800); u8* ip=f+o; u32 sip=lo&&(dip>>24)==127?dip:my_ip;
    ip[0]=0x45; be16(ip+2,(u16)(20+n)); be16(ip+4,(u16)net_rand()); ip[8]=64; ip[9]=1;
    be32(ip+12,sip); be32(ip+16,dip); u16 ick=cksum(ip,20); ip[10]=(u8)(ick>>8); ip[11]=(u8)ick;
    u8* ic=ip+20; lxs_mv(ic,b,n); be16(ic+4,s->lport); ic[2]=ic[3]=0; u16 c=cksum(ic,n); ic[2]=(u8)(c>>8); ic[3]=(u8)c;
    lxs_pump();
    if(lo)lxs_lo_tx(f,o+20+n); else nd_tx2(f,o+20+n);
    return n;
}
static void lxs_frame(u8* f,int n){
    if(n<34)return; u16 et=rd16(f+12);
    if(et==0x0806){ arp_auto_reply(f,n); return; }
    if(et!=0x0800)return;
    { extern int dhcp_async_take(const u8*,int); if(dhcp_async_take(f,n))return; }
    u8* ip=f+14; int ihl=(ip[0]&0x0F)*4; if(ihl<20)return; int tot=rd16(ip+2); if(tot<ihl||14+tot>n)return;
    if(rd16(ip+6)&0x3FFF)return;
    u32 sip=rd32(ip+12), dip=rd32(ip+16);
    if(!lxs_is_lo(dip)&&dip!=0xFFFFFFFFu&&dip!=(my_ip|~my_mask))return;
    u8* p=ip+ihl; int plen=tot-ihl;
    if(ip[9]==1){ lxs_icmp_in(f,n,ip,ihl,p,plen,sip,dip); return; }
    if(ip[9]==17){ if(plen<8)return; int ul=rd16(p+4); if(ul<8||ul>plen)return; if(!fw_allow_in(17,sip,rd16(p),rd16(p+2),lxs_in_lo))return; lxs_udp_in(sip,rd16(p),rd16(p+2),p+8,ul-8); return; }
    if(ip[9]!=6||plen<20)return;
    int thl=((p[12]>>4)&0xF)*4; if(thl<20||thl>plen)return;
    u16 sp=rd16(p), dp=rd16(p+2);
    for(int i=0;i<NLXS;i++){ struct lxsock* s=&g_lxs[i]; if(!s->used||s->type!=1||s->st==LXS_LISTEN||s->lport!=dp||s->rport!=sp||s->rip!=sip)continue;
        lxs_tcp_in(s,p,thl,plen-thl,rd32(p+4),rd32(p+8),p[13]); return; }
    if((p[13]&(TF_SYN|TF_ACK|TF_RST))!=TF_SYN)return;
    if(!fw_allow_in(6,sip,sp,dp,lxs_in_lo))return;
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
    int got=0; for(int k=0;k<128;k++){ int n=nd_rx(f,1600); if(n<=0)break; lxs_frame(f,n); got++; }
    if(got){ extern void jc_kick_waiters(void); jc_kick_waiters(); }
    lxs_timers();
    lxs_lo_drain();
}
void lxs_release(int i){
    struct lxsock* s=lxs_get(i); if(!s)return;
    if(g_bpf_sock_any)bpf_sock_detach(i);
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

void lxs_frame_ext(u8* f,int n){ lxs_frame(f,n); }
void lxs_keepalive(int i){ struct lxsock* s=lxs_get(i); if(s&&s->type==1&&s->st==LXS_EST&&s->una==s->nxt&&!s->txn) lxs_seg(s,s->nxt-1,TF_ACK,0,0); }
void lxs_resyn(int i){ struct lxsock* s=lxs_get(i); if(s&&s->type==1&&s->st==LXS_SYN){ lxs_seg(s,s->iss,TF_SYN,0,0); s->tlast=g_ticks; } }
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
    if(s->type==16)return lxs_nl_send(i,b,n);
    if(s->type==2&&s->icmp){ if(s->wr_shut)return -32; if(!has_to&&!s->conn)return -89; return lxs_icmp_send(s,b,n,has_to?dip:s->rip); }
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
        fw_note_out(17,s->lport,real,tp);
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
    if(s->type==2||s->type==16){
        if(!s->rxn)return s->rd_shut?0:LXS_BLOCK;
        u8* r=s->rx; int dl=rd16(r+6);
        if(g_bpf_sock_any){ int fl=bpf_sock_filter(i,r+8,dl); if(fl<=0){ int t=8+dl; lxs_mv(s->rx,s->rx+t,s->rxn-t); s->rxn-=t; return lxs_recv(i,b,n,peek,from,fport); } if(fl<dl){ int c=fl<n?fl:n; if(from)*from=rd32(r); if(fport)*fport=rd16(r+4); if(c>0)lxs_mv(b,r+8,c); if(!(peek&1)){ int t=8+dl; lxs_mv(s->rx,s->rx+t,s->rxn-t); s->rxn-=t; } return (peek&2)?fl:c; } } if(from)*from=rd32(r); if(fport)*fport=rd16(r+4);
        int c=dl<n?dl:n; if(c>0)lxs_mv(b,r+8,c);
        if(!(peek&1)){ int t=8+dl; lxs_mv(s->rx,s->rx+t,s->rxn-t); s->rxn-=t; }
        return (peek&2)?dl:c; }
    if(from)*from=s->rip; if(fport)*fport=s->rport;
    if(s->rxn>0){ int c=s->rxn<n?s->rxn:n; lxs_mv(b,s->rx,c);
        if(!(peek&1)){ int was_full=s->rxn+1600>LXS_RXCAP||(s->ws_ok&&s->rxn>LXS_RXCAP/2); lxs_mv(s->rx,s->rx+c,s->rxn-c); s->rxn-=c; if(was_full&&s->st==LXS_EST)lxs_ack(s); }
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
    if(s->type==2||s->type==16){ re|=4; if(s->rxn||s->rd_shut)re|=1; return re; }
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
    if(port){ if(lxs_clash(s,port))return -98; s->lport=port; }
    else s->lport=lxs_eph(s->type);
    return 0;
}
int lxs_listen(int i,int backlog){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->type!=1)return -95;
    if(s->st==LXS_LISTEN){ s->backlog=backlog<1?1:backlog>LXS_QMAX?LXS_QMAX:backlog; return 0; }
    if(s->st!=LXS_NEW)return -22;
    if(s->lport) for(int k=0;k<NLXS;k++){ struct lxsock* o=&g_lxs[k]; if(o->used&&o!=s&&o->type==1&&o->st==LXS_LISTEN&&o->lport==s->lport&&!(o->reuseport&&s->reuseport))return -98; }
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
int lxs_ifinfo(int idx,char* name,u32* ip,u32* mask,u8* mac,u32* flags,int* mtu){
    if(idx==1){ const char* n="lo"; int k=0; while(n[k]){ name[k]=n[k]; k++; } name[k]=0; *ip=LXS_LO; *mask=0xFF000000u; for(int i=0;i<6;i++)mac[i]=0; *flags=0x49; *mtu=65536; return 0; }
    { extern int lxn_net_isolated(void) __attribute__((weak)); if(idx==2&&lxn_net_isolated&&lxn_net_isolated())return -1; }
    if(idx==2&&(nic_present||my_ip||g_netdev_wifi)){ const char* n=g_netdev_wifi?"wlan0":"eth0"; int k=0; while(n[k]){ name[k]=n[k]; k++; } name[k]=0;
        *ip=my_ip; *mask=my_ip?my_mask:0; const u8* m=nd_mymac(); for(int i=0;i<6;i++)mac[i]=m?m[i]:my_mac[i];
        *flags=0x1003|(my_ip?0x10040u:0); *mtu=1500; return 0; }
    return -1;
}
int lxs_nl_listeners(int proto){ for(int k=0;k<NLXS;k++) if(g_lxs[k].used&&g_lxs[k].type==16&&g_lxs[k].nlproto==proto&&g_lxs[k].nlgroups)return 1; return 0; }
int lxs_nl_bcast(int proto,u32 group,const u8* b,int n){ int sent=0; u32 gm=1u<<(group-1);
    for(int k=0;k<NLXS;k++){ struct lxsock* s=&g_lxs[k]; if(!s->used||s->type!=16||s->nlproto!=proto||!(s->nlgroups&gm)||s->rd_shut)continue; if(s->rxn+8+n>LXS_RXCAP){ s->err=105; continue; }
        u8* r=s->rx+s->rxn; be32(r,group); be16(r+4,0); be16(r+6,(u16)n); lxs_mv(r+8,b,n); s->rxn+=8+n; sent++; }
    if(sent){ extern void jc_kick_waiters(void); jc_kick_waiters(); } return sent; }
int lxs_new_nl(int proto){ int i=lxs_new(16); if(i<0)return i; g_lxs[i].nlproto=proto; return i; }
struct nlb { u8* b; int n, cap; };
static void nl_raw(struct nlb* o,const void* d,int n){ const u8* s=(const u8*)d; for(int i=0;i<n&&o->n<o->cap;i++)o->b[o->n++]=s?s[i]:0; while((o->n&3)&&o->n<o->cap)o->b[o->n++]=0; }
static int nl_hdr(struct nlb* o,u16 type,u16 flags,u32 seq,u32 pid){ int st=o->n; u32 h[4]; h[0]=16; h[1]=(u32)type|((u32)flags<<16); h[2]=seq; h[3]=pid; nl_raw(o,h,16); return st; }
static void nl_attr(struct nlb* o,u16 t,const void* d,int n){ u16 h[2]; h[0]=(u16)(4+n); h[1]=t; nl_raw(o,h,4); nl_raw(o,d,n); }
static void nl_u32(struct nlb* o,u16 t,u32 v){ nl_attr(o,t,&v,4); }
static void nl_end(struct nlb* o,int st){ if(st+16>o->cap||o->n>o->cap)return; u32 L=(u32)(o->n-st); u8* p=o->b+st; p[0]=(u8)L; p[1]=(u8)(L>>8); p[2]=(u8)(L>>16); p[3]=(u8)(L>>24); }
static int nl_strlen(const char* s){ int k=0; while(s[k])k++; return k; }
static int nl_prefix(u32 m){ int k=0; while(m&0x80000000u){ k++; m<<=1; } return k; }
static void nl_link(struct nlb* o,int idx,u16 fl,u32 seq,u32 pid){
    char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu; if(lxs_ifinfo(idx,nm,&ip,&mk,mac,&f,&mtu))return;
    int st=nl_hdr(o,16,fl,seq,pid);
    u8 ifi[16]; for(int i=0;i<16;i++)ifi[i]=0; u16 ty=idx==1?772:1; ifi[2]=(u8)ty; ifi[3]=(u8)(ty>>8); ifi[4]=(u8)idx; ifi[8]=(u8)f; ifi[9]=(u8)(f>>8); ifi[10]=(u8)(f>>16); ifi[11]=(u8)(f>>24);
    nl_raw(o,ifi,16);
    nl_attr(o,3,nm,nl_strlen(nm)+1); nl_u32(o,4,(u32)mtu); nl_u32(o,13,idx==1?0:1000);
    u8 os=idx==1?0:(f&0x40)?6:2; nl_attr(o,16,&os,1); u8 lm=0; nl_attr(o,17,&lm,1);
    nl_attr(o,1,mac,6); u8 bc[6]; for(int i=0;i<6;i++)bc[i]=idx==1?0:0xFF; nl_attr(o,2,bc,6);
    nl_end(o,st);
}
static void nl_addr(struct nlb* o,int idx,int fam,u16 fl,u32 seq,u32 pid){
    char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu; if(lxs_ifinfo(idx,nm,&ip,&mk,mac,&f,&mtu))return;
    if((fam==0||fam==2)&&ip){
        int st=nl_hdr(o,20,fl,seq,pid); u8 ifa[8]={2,(u8)nl_prefix(mk),0x80,(u8)(idx==1?254:0),(u8)idx,0,0,0}; nl_raw(o,ifa,8);
        u8 a[4]={(u8)(ip>>24),(u8)(ip>>16),(u8)(ip>>8),(u8)ip}; nl_attr(o,1,a,4); nl_attr(o,2,a,4);
        if(idx!=1){ u32 b=ip|~mk; u8 ba[4]={(u8)(b>>24),(u8)(b>>16),(u8)(b>>8),(u8)b}; nl_attr(o,4,ba,4); }
        nl_attr(o,3,nm,nl_strlen(nm)+1); nl_end(o,st); }
    if((fam==0||fam==10)&&idx==1){
        int st=nl_hdr(o,20,fl,seq,pid); u8 ifa[8]={10,128,0x80,254,1,0,0,0}; nl_raw(o,ifa,8);
        u8 a[16]; for(int i=0;i<16;i++)a[i]=0; a[15]=1; nl_attr(o,1,a,16); nl_end(o,st); }
}
static void nl_route(struct nlb* o,u32 dst,int plen,u32 gw,u32 src,u16 fl,u32 seq,u32 pid){
    int st=nl_hdr(o,24,fl,seq,pid);
    u8 rt[12]={2,(u8)plen,0,0,254,(u8)(gw?3:2),(u8)(gw?0:253),1,0,0,0,0}; nl_raw(o,rt,12);
    nl_u32(o,15,254);
    if(plen){ u8 a[4]={(u8)(dst>>24),(u8)(dst>>16),(u8)(dst>>8),(u8)dst}; nl_attr(o,1,a,4); }
    if(gw){ u8 a[4]={(u8)(gw>>24),(u8)(gw>>16),(u8)(gw>>8),(u8)gw}; nl_attr(o,5,a,4); }
    if(src){ u8 a[4]={(u8)(src>>24),(u8)(src>>16),(u8)(src>>8),(u8)src}; nl_attr(o,7,a,4); }
    nl_u32(o,4,2);
    nl_end(o,st);
}
static void nl_err(struct nlb* o,int err,const u8* req,u32 seq,u32 pid){
    int st=nl_hdr(o,2,err?0x100:0,seq,pid); nl_raw(o,&err,4); nl_raw(o,req,16); nl_end(o,st);
}
static u32 g_nl_auto=0x80000000u;
int lxs_nl_send(int i,const u8* b,int n){
    struct lxsock* s=lxs_get(i); if(!s)return -9;
    if(s->wr_shut)return -32;
    if(n<0)return -22;
    if(!s->nlpid){ for(;;){ u32 c=g_nl_auto++; int busy=0; for(int k=0;k<NLXS;k++) if(g_lxs[k].used&&g_lxs[k].type==16&&g_lxs[k].nlproto==s->nlproto&&g_lxs[k].nlpid==c)busy=1; if(!busy){ s->nlpid=c; break; } } }
    if(s->nlproto!=0)return n;
    struct nlb o; o.cap=16384; o.n=0; o.b=(u8*)kmalloc(o.cap); if(!o.b)return -12;
    int off=0;
    while(off+16<=n&&o.n<o.cap-1024){
        const u8* m=b+off; u32 L=(u32)m[0]|((u32)m[1]<<8)|((u32)m[2]<<16)|((u32)m[3]<<24);
        if(L<16||L>(u32)(n-off))break;
        u16 ty=(u16)(m[4]|(m[5]<<8)), fl=(u16)(m[6]|(m[7]<<8)); u32 seq=(u32)m[8]|((u32)m[9]<<8)|((u32)m[10]<<16)|((u32)m[11]<<24);
        int dump=(fl&0x300)==0x300, fam=L>16?m[16]:0, ok=1;
        if(!(fl&1)){ off+=(L+3)&~3u; continue; }
        if(ty==18){
            if(dump){ for(int k=1;k<=2;k++)nl_link(&o,k,2,seq,s->nlpid); }
            else { int idx=L>=24?(int)((u32)m[20]|((u32)m[21]<<8)|((u32)m[22]<<16)|((u32)m[23]<<24)):0;
                if(!idx&&L>32){ int a=32; while(a+4<=(int)L){ int al=m[a]|(m[a+1]<<8), at=m[a+2]|(m[a+3]<<8); if(al<4||a+al>(int)L)break; if(at==3){ char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu; for(int k=1;k<=2;k++) if(!lxs_ifinfo(k,nm,&ip,&mk,mac,&f,&mtu)){ int q=0; while(q<al-4&&m[a+4+q]&&nm[q]==(char)m[a+4+q])q++; if(!nm[q]&&(q==al-4||!m[a+4+q]))idx=k; } } a+=(al+3)&~3; } }
                char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu;
                if(!idx||lxs_ifinfo(idx,nm,&ip,&mk,mac,&f,&mtu)){ nl_err(&o,-19,m,seq,s->nlpid); ok=0; } else nl_link(&o,idx,0,seq,s->nlpid); }
        } else if(ty==22){
            if(dump){ for(int k=1;k<=2;k++)nl_addr(&o,k,fam,2,seq,s->nlpid); } else { nl_err(&o,-95,m,seq,s->nlpid); ok=0; }
        } else if(ty==26){
            if(dump){ if(fam==0||fam==2){ if(my_ip&&gw_ip)nl_route(&o,0,0,gw_ip,0,2,seq,s->nlpid); if(my_ip)nl_route(&o,my_ip&my_mask,nl_prefix(my_mask),0,my_ip,2,seq,s->nlpid); } }
            else { nl_err(&o,-95,m,seq,s->nlpid); ok=0; }
        } else if(ty==30||ty==34||ty==42||ty==58){
            if(!dump){ nl_err(&o,-95,m,seq,s->nlpid); ok=0; }
        } else if(ty>=16&&ty<100&&(ty&3)==0){ nl_err(&o,-1,m,seq,s->nlpid); ok=0; }
        else { nl_err(&o,-95,m,seq,s->nlpid); ok=0; }
        if(ok&&dump){ int st=nl_hdr(&o,3,2,seq,s->nlpid); int z=0; nl_raw(&o,&z,4); nl_end(&o,st); }
        else if(ok&&(fl&4))nl_err(&o,0,m,seq,s->nlpid);
        off+=(L+3)&~3u;
    }
    if(o.n&&s->rxn+8+o.n>LXS_RXCAP){ kfree(o.b); return -105; }
    if(o.n){ u8* r=s->rx+s->rxn; be32(r,0); be16(r+4,0); be16(r+6,(u16)o.n); lxs_mv(r+8,o.b,o.n); s->rxn+=8+o.n; }
    kfree(o.b);
    return n;
}
