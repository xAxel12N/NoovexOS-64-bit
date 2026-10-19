#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/stack/sockets.h"
/* ================= BSD socket layer (persistent TCP connections) ================= */
#define NSOCK 8
typedef struct {
    int used, connected;
    u32 dip; u16 sp, dport; u8 dmac[6];
    u32 snd, rcv;
} nsock_t;
static nsock_t g_sock[NSOCK];

int sock_alloc(void){ for(int i=0;i<NSOCK;i++) if(!g_sock[i].used){ for(int j=0;j<(int)sizeof(nsock_t);j++)((char*)&g_sock[i])[j]=0; g_sock[i].used=1; return i; } return -1; }

/* connect: DNS-resolve host if needed done by caller; here dip is numeric */
int sock_connect(int sid,u32 dip,u16 dport){
    if(sid<0||sid>=NSOCK||!g_sock[sid].used)return -1;
    if(!nic_up())return -2;
    u32 route=((dip&my_mask)==(my_ip&my_mask))?dip:gw_ip;
    u8 dmac[6]; if(!arp_resolve(route,dmac))return -3;
    nic_kick(); nic_flush_rx();
    nsock_t* s=&g_sock[sid];
    for(int i=0;i<6;i++)s->dmac[i]=dmac[i];
    s->dip=dip; s->dport=dport; s->sp=next_ephport(); s->snd=net_rand(); s->rcv=0;
    u8 rx[1600]; int est=0;
    for(int att=0;att<4&&!est;att++){ tcp_send(dip,s->sp,dport,dmac,s->snd,0,TF_SYN,0,0);
        for(int k=0;k<300;k++){ int n=net_poll(rx,1600,1);
            if(n<54)continue; if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=6)continue;
            int ihl=(ip[0]&0x0F)*4; u8* t=ip+ihl; if(rd16(t+0)!=dport||rd16(t+2)!=s->sp)continue; u8 fl=t[13];
            if((fl&(TF_SYN|TF_ACK))==(TF_SYN|TF_ACK)&&rd32(t+8)==s->snd+1){ s->rcv=rd32(t+4)+1; s->snd+=1; est=1; break; }
            if(fl&TF_RST)return -4; } }
    if(!est)return -5;
    tcp_send(dip,s->sp,dport,dmac,s->snd,s->rcv,TF_ACK,0,0);
    s->connected=1; return 0;
}
int sock_send(int sid,const u8* buf,int len){
    if(sid<0||sid>=NSOCK||!g_sock[sid].used||!g_sock[sid].connected)return -1;
    nsock_t* s=&g_sock[sid]; int off=0;
    while(off<len){ int chunk=len-off; if(chunk>1400)chunk=1400;
        tcp_send(s->dip,s->sp,s->dport,s->dmac,s->snd,s->rcv,TF_PSH|TF_ACK,buf+off,chunk); s->snd+=(u32)chunk; off+=chunk; }
    return len;
}
int sock_recv(int sid,u8* out,int maxout){
    if(sid<0||sid>=NSOCK||!g_sock[sid].used||!g_sock[sid].connected)return -1;
    nsock_t* s=&g_sock[sid]; u8 rx[1600]; int outlen=0,idle=0;
    while(outlen<maxout){ int n=net_poll(rx,1600,1);
        if(n<=0){ if(++idle>900)break; continue; }
        if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=6)continue;
        int ihl=(ip[0]&0x0F)*4; u8* t=ip+ihl; if(rd16(t+0)!=s->dport||rd16(t+2)!=s->sp)continue;
        int iptot=rd16(ip+2); int thl=((t[12]>>4)&0xF)*4; int dl=iptot-ihl-thl; if(dl<0)dl=0;
        u32 seq=rd32(t+4); u8 fl=t[13];
        if(fl&TF_RST){ s->connected=0; break; }
        if(dl>0){ if(seq==s->rcv){ int cp=dl; if(outlen+cp>maxout)cp=maxout-outlen; for(int i=0;i<cp;i++)out[outlen+i]=t[thl+i]; outlen+=cp; s->rcv+=dl; }
            tcp_send(s->dip,s->sp,s->dport,s->dmac,s->snd,s->rcv,TF_ACK,0,0); idle=0;
            if(outlen>0)break; }
        if(fl&TF_FIN){ s->rcv+=1; tcp_send(s->dip,s->sp,s->dport,s->dmac,s->snd,s->rcv,TF_FIN|TF_ACK,0,0); s->snd+=1; s->connected=0; break; } }
    return outlen;
}
int sock_close(int sid){
    if(sid<0||sid>=NSOCK||!g_sock[sid].used)return -1;
    nsock_t* s=&g_sock[sid];
    if(s->connected){ tcp_send(s->dip,s->sp,s->dport,s->dmac,s->snd,s->rcv,TF_FIN|TF_ACK,0,0); s->snd+=1; }
    s->used=0; s->connected=0; return 0;
}int sock_send_rel(int sid,const u8* buf,int len){
    if(sid<0||sid>=NSOCK||!g_sock[sid].used||!g_sock[sid].connected)return -1;
    nsock_t* s=&g_sock[sid]; u32 base=s->snd, una=base, nxt=base, end=base+(u32)len; u8 rx[1600]; int idle=0, tries=0;
    while((int)(una-end)<0){
        while((int)(nxt-end)<0&&(int)(nxt-una)<8*1400){ int off=(int)(nxt-base); int ch=len-off; if(ch>1400)ch=1400; tcp_send(s->dip,s->sp,s->dport,s->dmac,nxt,s->rcv,TF_PSH|TF_ACK,buf+off,ch); nxt+=(u32)ch; }
        int n=net_poll(rx,1600,1);
        if(n<54){ if(++idle>250){ idle=0; nxt=una; if(++tries>24){ s->snd=una; return -1; } } continue; }
        if(rd16(rx+12)!=0x0800)continue; u8* ip=rx+14; if(ip[9]!=6)continue;
        int ihl=(ip[0]&0x0F)*4; u8* t=ip+ihl; if(rd16(t+0)!=s->dport||rd16(t+2)!=s->sp)continue;
        u8 fl=t[13]; if(fl&TF_RST){ s->connected=0; return -2; }
        int iptot=rd16(ip+2); int thl=((t[12]>>4)&0xF)*4; int dl=iptot-ihl-thl; if(dl<0)dl=0;
        if(fl&TF_ACK){ u32 a=rd32(t+8); if((int)(a-una)>0&&(int)(a-nxt)<=0){ una=a; idle=0; tries=0; } }
        if(dl>0&&rd32(t+4)==s->rcv){ s->rcv+=(u32)dl; tcp_send(s->dip,s->sp,s->dport,s->dmac,nxt,s->rcv,TF_ACK,0,0); } }
    s->snd=end; return len; }
