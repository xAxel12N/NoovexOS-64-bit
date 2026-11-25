#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/stack/tcpip.c"
int fw_allow_in(int proto,u32 sip,u16 sport,u16 dport,int local){ (void)proto; (void)sip; (void)sport; (void)dport; (void)local; return 1; }
void fw_note_out(int proto,u16 lport,u32 rip,u16 rport){ (void)proto; (void)lport; (void)rip; (void)rport; }
volatile u64 g_ticks; volatile int g_net_rx; void (*g_wait_pump)(void);
u32 my_ip=0, my_mask=0, gw_ip=0, dns_ip=0; u8 my_mac[6]={2,0,0,0,0,1}, gw_mac[6]={2,0,0,0,0,2};
int gw_known=0, net_ok=1, nic_init=1, nic_present=1, g_dhcp_done=0, g_dhcp_ok=0; volatile int g_bpf_sock_any, g_bpf_tp_any; u16 ping_ms;
void be16(u8*b,u16 v){ b[0]=v>>8; b[1]=(u8)v; }
void be32(u8*b,u32 v){ b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=(u8)v; }
u16 rd16(const u8*b){ return ((u16)b[0]<<8)|b[1]; }
u32 rd32(const u8*b){ return ((u32)b[0]<<24)|((u32)b[1]<<16)|((u32)b[2]<<8)|b[3]; }
u16 cksum(const u8*d,int n){ u32 s=0; for(int i=0;i+1<n;i+=2)s+=((u32)d[i]<<8)|d[i+1]; if(n&1)s+=(u32)d[n-1]<<8; while(s>>16)s=(s&0xFFFF)+(s>>16); return (u16)~s; }
int eth_hdr(u8* f,const u8* dst,u16 type){ for(int i=0;i<6;i++)f[i]=dst[i]; const u8* mm=nd_mymac(); for(int i=0;i<6;i++)f[6+i]=mm[i]; be16(f+12,type); return 14; }
void arp_request(u32 tip){ (void)tip; }
char* scopy(char*p,const char*s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char*p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
int seq(const char*a,const char*b){ return strcmp(a,b)==0; }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void netlog(const char* s){ (void)s; }
u32 net_rand(void){ return 0x12345678u; }
int bpf_sock_filter(int i,const u8* d,int n){ (void)i; (void)d; return n; }
int bpf_sock_detach(int i){ (void)i; return 0; }
void jc_kick_waiters(void){} void ui_pump(void){} int sock_any_open(void){ return 0; }
void nic_flush_rx(void){} void nic_kick(void){} int nic_up_raw(void){ return 1; }
void nic_send(const u8* d,int n){ (void)d; (void)n; } int nic_recv(u8* o,int m){ (void)o; (void)m; return 0; }
int lxs_is_lo(u32 ip){ (void)ip; return 0; } void lxs_lo_tx(const u8* f,int n){ (void)f; (void)n; }
int lxs_frame(const u8* f,int n){ (void)f; (void)n; return 0; }
void lxs_lo_drain(void){}
int g_netdev_wifi;

static u64 sim_ns; static int sim_precise; static u64 step_ns=37000;
static void sim_tick(void){ sim_ns+=step_ns; g_ticks=sim_ns/10000000ULL; }
u16 pit_read(void){ sim_tick(); return (u16)(11932u-(u32)((sim_ns/838ULL)%11932ULL)); }
int hr_precise(void){ return sim_precise; }
u64 hr_ns(void){ sim_tick(); return sim_ns; }

static int fails;
#define CHECK(c,...) do{ if(c)printf("ok   " __VA_ARGS__); else { fails++; printf("FAIL " __VA_ARGS__); } printf("\n"); }while(0)

static u8 offer[400]; static int offer_n; static u64 offer_at; static int offer_sent; static u32 last_xid;
static void tx_hook(const u8* d,int n){ if(n>=42+240)last_xid=rd32(d+42+4); }
static int rx_offer(u8* o,int m){
    if(offer_sent||!offer_n||sim_ns<offer_at)return 0;
    offer_sent=1; be32(offer+42+4,last_xid); int n=offer_n<m?offer_n:m; memcpy(o,offer,n); return n;
}
static void build_offer(u8 mtype,u32 yi){
    memset(offer,0,sizeof offer); u8* f=offer; be16(f+12,0x0800); u8* ip=f+14; ip[0]=0x45; ip[9]=17; be16(ip+2,20+8+300);
    u8* udp=ip+20; be16(udp,67); be16(udp+2,68); be16(udp+4,8+300); u8* d=udp+8;
    d[0]=2; be32(d+16,yi); for(int i=0;i<6;i++)d[28+i]=my_mac[i]; be32(d+236,0x63825363u);
    int n=240; d[n++]=53; d[n++]=1; d[n++]=mtype; d[n++]=1; d[n++]=4; be32(d+n,0xFFFFFF00u); n+=4; d[n++]=255;
    offer_n=14+20+8+300;
}
static void reset_clock(int precise){ sim_precise=precise; sim_ns=5000000ULL; g_ticks=0; offer_sent=0; }

int main(void){
    u8 rx[1600];
    for(int pr=0;pr<2;pr++){
        reset_clock(pr); int bad=0; u64 worst=0;
        for(int i=0;i<2000;i++){ sim_ns+=(u64)(i*131%9000)*1000ULL; u64 a=sim_ns; nd_poll(rx,1600,1); u64 e=sim_ns-a; if(e<1000000ULL)bad++; if(e>worst)worst=e; }
        CHECK(bad==0&&worst<1300000ULL,"nd_poll 1 ms wait never ends early across PIT wraps (%s clock): %d short of 2000, longest %llu us",pr?"precise":"PIT fallback",bad,(unsigned long long)worst/1000);
    }
    for(int pr=0;pr<2;pr++){
        reset_clock(pr); g_rx=rx_offer; g_tx=tx_hook; offer_n=0;
        u32 yi,sid,mk,gw,dn; u64 a=sim_ns; int r=dhcp_wait(1,2,&yi,&sid,&mk,&gw,&dn,900); u64 e=(sim_ns-a)/1000000ULL;
        CHECK(r==0&&e>=900&&e<=930,"dhcp_wait(900) with no reply waits the whole deadline (%s clock): %llu ms",pr?"precise":"PIT fallback",(unsigned long long)e);
    }
    reset_clock(1); g_rx=rx_offer; g_tx=tx_hook; build_offer(2,0x0A00020Fu); last_xid=0x4E565831u; offer_at=sim_ns+850000000ULL;
    { u32 yi=0,sid,mk,gw,dn; int r=dhcp_wait(last_xid,2,&yi,&sid,&mk,&gw,&dn,900);
      CHECK(r==1&&yi==0x0A00020Fu,"an offer arriving after 850 ms is still accepted (yi=%08x)",yi); }
    reset_clock(1); g_rx=rx_offer; g_tx=tx_hook; build_offer(2,0x0A000210u); offer_at=sim_ns+400000000ULL;
    { u32 yi=0,sid,mk,gw,dn; int r=dhcp_wait(last_xid,5,&yi,&sid,&mk,&gw,&dn,900);
      CHECK(r==0,"an offer is not taken for an ack and the wait still ends"); }
    printf(fails?"DHCPWAIT %d failed\n":"DHCPWAIT all ok\n",fails); return fails?1:0;
}
