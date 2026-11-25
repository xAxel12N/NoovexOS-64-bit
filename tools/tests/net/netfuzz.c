#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sanitizer/asan_interface.h>
#include "net/stack/tcpip.c"
#include "sys/linux/lxsock.c"
int fw_allow_in(int proto,u32 sip,u16 sport,u16 dport,int local){ (void)proto; (void)sip; (void)sport; (void)dport; (void)local; return 1; }
void fw_note_out(int proto,u16 lport,u32 rip,u16 rport){ (void)proto; (void)lport; (void)rip; (void)rport; }
volatile u64 g_ticks; volatile int g_net_rx; void (*g_wait_pump)(void);
u32 my_ip=0x0A00020Fu, my_mask=0xFFFFFF00u, gw_ip=0x0A000202u, dns_ip=0x0A000903u; u8 my_mac[6]={2,0,0,0,0,1}, gw_mac[6]={2,0,0,0,0,2};
int gw_known=1, net_ok=1, nic_init=1, nic_present=1, g_dhcp_done=1, g_dhcp_ok=1; volatile int g_bpf_sock_any, g_bpf_tp_any; u16 ping_ms;
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
static u32 rnd_state=1; u32 net_rand(void){ rnd_state=rnd_state*1103515245u+12345u; return rnd_state>>1; }
u16 pit_read(void){ static u16 p; g_ticks++; p-=1200; return p; }
int hr_precise(void){ return 0; } u64 hr_ns(void){ return g_ticks*10000000ULL; }
int bpf_sock_filter(int i,const u8* d,int n){ (void)i; (void)d; return n; }
int bpf_sock_detach(int i){ (void)i; return 0; }
void jc_kick_waiters(void){} void ui_pump(void){} int sock_any_open(void){ return 0; }
void nic_flush_rx(void){} void nic_kick(void){} int nic_up_raw(void){ return 1; }
void nic_send(const u8* d,int n){ (void)d; (void)n; } int nic_recv(u8* o,int m){ (void)o; (void)m; return 0; }
static u8 txl[1600]; static int txn;
static void tx_hook(const u8* d,int n){ if(n>0&&n<=1600){ memcpy(txl,d,n); txn=n; } }
static int link_ok(void){ return 1; }
static const u8* in_d; static size_t in_n, in_p; static int mode, flags, nframes;
static u8* pz; static size_t pzn;
static void unpoison(void){ if(pz)ASAN_UNPOISON_MEMORY_REGION(pz,pzn); pz=0; }
static void put16(u8* f,int n,int o,u16 v){ if(o+2<=n)be16(f+o,v); }
static void put32(u8* f,int n,int o,u32 v){ if(o+4<=n)be32(f+o,v); }
static void put8(u8* f,int n,int o,u8 v){ if(o<n)f[o]=v; }
static void fix_ip(u8* f,int n,u8 proto,u32 src){
    put16(f,n,12,0x0800); if(n>14&&!(flags&1))f[14]=0x45; put8(f,n,23,proto); put32(f,n,26,src); put32(f,n,30,my_ip);
    if(!(flags&2))put16(f,n,16,(u16)(n-14)); put16(f,n,20,0);
}
static int ihl_of(u8* f,int n){ return n>14?(f[14]&15)*4:20; }
static void fix_frame(u8* f,int n){
    const u8* tip=txl+14; int tihl=(tip[0]&15)*4; const u8* tl4=tip+tihl;
    if(mode==2){ fix_ip(f,n,17,dns_ip); int u=14+ihl_of(f,n); put16(f,n,u,53); put16(f,n,u+2,rd16(tl4)); if(!(flags&4)){ put16(f,n,u+8,rd16(tl4+8)); put16(f,n,u+10,0x8180); put16(f,n,u+12,1); }
        if(!(flags&8)){ int ql=txn-(int)(tl4+20-txl); for(int i=0;i<ql;i++)put8(f,n,u+20+i,tl4[20+i]); } }
    else if(mode==3){ fix_ip(f,n,6,0x0A000005u); int t=14+ihl_of(f,n); put16(f,n,t,80); put16(f,n,t+2,rd16(tl4)); if(nframes==0){ put8(f,n,t+13,0x12); put32(f,n,t+8,rd32(tl4+4)+1); } else if(!(flags&4))put32(f,n,t+4,rd32(tl4+8)); }
    else if(mode==4){ fix_ip(f,n,1,0x0A000005u); int c=14+ihl_of(f,n); put8(f,n,c,0); put16(f,n,c+4,0xABCD); }
    else if(mode==6){ fix_ip(f,n,17,0x0A000201u); int u=14+ihl_of(f,n); put16(f,n,u,67); put16(f,n,u+2,68); put8(f,n,u+8,2); put32(f,n,u+12,rd32(tl4+12)); for(int i=0;i<6;i++)put8(f,n,u+8+28+i,my_mac[i]); put32(f,n,u+8+236,0x63825363u); if(!(flags&4)){ put8(f,n,u+8+240,53); put8(f,n,u+8+241,1); put8(f,n,u+8+242,(u8)(nframes&1?5:2)); } }
}
static int take(u8** out){
    if(in_p+2>in_n)return -1; int n=in_d[in_p]|(in_d[in_p+1]<<8); in_p+=2; n%=1601; if((size_t)n>in_n-in_p)n=(int)(in_n-in_p);
    u8* f=malloc(n?n:1); memcpy(f,in_d+in_p,n); in_p+=n; *out=f; return n;
}
static int fake_rx(u8* o,int m){
    unpoison(); u8* f; int n=take(&f); if(n<0)return 0; if(n>m)n=m;
    fix_frame(f,n); nframes++; memcpy(o,f,n); free(f);
    if(m>n){ pz=o+n; pzn=(size_t)(m-n); ASAN_POISON_MEMORY_REGION(pz,pzn); }
    return n;
}
static void socks_reset(void){ for(int i=0;i<NLXS;i++){ free(g_lxs[i].rx); free(g_lxs[i].tx); } memset(g_lxs,0,sizeof g_lxs); lo_head=lo_tail=0; }
static void socks_setup(void){
    int a=lxs_new(1); struct lxsock* s=&g_lxs[a]; s->st=LXS_SYN; s->lport=40000; s->rport=80; s->rip=0x0A000005u; s->iss=1000; s->conn=1;
    a=lxs_new(1); s=&g_lxs[a]; s->st=LXS_EST; s->lport=40001; s->rport=80; s->rip=0x0A000005u; s->iss=5000; s->una=5001; s->nxt=5001+100; s->txn=100; s->rcv=7000; s->conn=1;
    a=lxs_new(1); s=&g_lxs[a]; s->st=LXS_LISTEN; s->lport=8080; s->backlog=4;
    a=lxs_new(2); s=&g_lxs[a]; s->lport=5353;
    a=lxs_new(2); s=&g_lxs[a]; s->icmp=1; s->lport=0x1234;
}
static void frame_fix_lx(u8* f,int n,u32 dip){
    if(n<34)return; static const u16 dps[]={40000,40001,8080,5353,0x1234}; static const u16 sps[]={80,80,9999,53,0};
    int w=flags>>4&7; if(w>4)w=1; u8 pr=w==3?17:w==4?1:6; if(flags&8){ put16(f,n,12,0x0806); return; }
    fix_ip(f,n,pr,0x0A000005u); put32(f,n,30,dip); int l=14+ihl_of(f,n);
    if(pr==1){ put16(f,n,l+4,0x1234); return; }
    put16(f,n,l,sps[w]); put16(f,n,l+2,dps[w]);
    if(pr==17){ if(!(flags&4))put16(f,n,l+4,(u16)(n-l)); return; }
    if(w==0&&!(flags&4)){ put8(f,n,l+13,0x12); put32(f,n,l+8,1001); }
    if(w==1&&!(flags&4))put32(f,n,l+4,7000);
    if(w==2&&!(flags&4))put8(f,n,l+13,0x02);
}
static void run_one(const u8* d,size_t n){
    if(n<2)return; mode=d[0]%8; flags=d[1]; in_d=d+2; in_n=n-2; in_p=0; nframes=0; rnd_state=d[0]*7u+1;
    g_rx=0; g_tx=tx_hook; g_mac=0; g_linkok=link_ok; g_netdev_wifi=1; gw_known=1; my_ip=0x0A00020Fu; my_mask=0xFFFFFF00u; dnsc_next=0; memset(dnsc,0,sizeof dnsc);
    txn=0; memset(txl,0,sizeof txl);
    if(mode==0||mode==7){ socks_reset(); socks_setup(); for(int k=0;k<8;k++){ u8* f; int m=take(&f); if(m<0)break; frame_fix_lx(f,m,mode==7?0x7F000001u:my_ip); if(mode==7)lxs_lo_tx(f,m); else lxs_frame(f,m); free(f); lxs_lo_drain(); } socks_reset(); return; }
    if(mode==1){ for(int k=0;k<8;k++){ u8* f; int m=take(&f); if(m<0)break; da_st=k&1?2:3; da_xid=0x11223344u; u32 a,b,c,e,g; arp_auto_reply(f,m); dhcp_async_take(f,m); dhcp_frame(f,m,da_xid,2,&a,&b,&c,&e,&g); if(m>14)dhcp_parse(f+14,m-14,rd32(f+18<f+m-4?f+18:f),(u8*)&a,&a,&b,&c,&e,&g); free(f); } da_st=0; da_stashn=0; return; }
    g_rx=fake_rx;
    if(mode==2){ u32 ip; static char hn[]="ex.example.com"; hn[0]='a'+(d[0]>>3)%26; dns_try(hn,dns_ip,&ip); }
    else if(mode==3){ u8 out[3000]; tcp_get(0x0A000005u,80,"GET / HTTP/1.0\r\n\r\n",18,out,(int)sizeof out); }
    else if(mode==4){ do_ping(0x0A000005u); }
    else if(mode==5){ u8 mac[6]; gw_known=0; arp_resolve(0x0A000009u,mac); }
    else if(mode==6){ g_dhcp_pump=0; dhcp_config(); my_ip=0x0A00020Fu; }
    unpoison(); g_rx=0;
}
static u8 R1[]={2,1,62,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 8,0, 0x4F,0,0,40, 0,0,0,0, 64,17,0,0, 10,0,2,3, 10,0,2,15, 0,53,0,0,0,8,0,0};
static u8 R2[]={3,1,54,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 8,0, 0x4F,0,0,40, 0,0,0,0, 64,6,0,0, 10,0,0,5, 10,0,2,15, 0,80,0,0,0,0,0,0, 0,0,0,0,0x50,0x12,0,0, 0,0,0,0};
static u8 R3[]={4,1,34,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 8,0, 0x4F,0,0,20, 0,0,0,0, 64,1,0,0, 10,0,0,5, 10,0,2,15};
static u8 R4[]={2,4,46,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 8,0, 0x45,0,0,32, 0,0,0,0, 64,17,0,0, 10,0,2,3, 10,0,2,15, 0,53,0,0,0,12,0,0, 0,0,0x81,0x80};
static u8 R5[]={2,1,54,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 8,0, 0x41,0,0,40, 0,0,0,0, 64,17,0,0, 10,0,2,3, 10,0,2,15, 0,53,0,0,0,8,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0};
static u8 R6[]={5,0,4,0, 1,2,3,4};
static void mutate(u8* b,size_t* n,size_t cap){
    int k=1+rand()%8; for(int i=0;i<k;i++){ int op=rand()%6; size_t p=*n?(size_t)rand()%*n:0;
        if(op==0&&*n)b[p]^=(u8)(1<<(rand()%8)); else if(op==1&&*n)b[p]=(u8)rand(); else if(op==2&&*n)b[p]=(u8)(rand()%3==0?0xFF:rand()%2?0:b[p]+1);
        else if(op==3&&*n<cap){ memmove(b+p+1,b+p,*n-p); b[p]=(u8)rand(); (*n)++; } else if(op==4&&*n>2){ memmove(b+p,b+p+1,*n-p-1); (*n)--; }
        else if(op==5&&*n>8){ size_t q=(size_t)rand()%*n, l=1+rand()%8; if(q+l<=*n&&p+l<=*n)memmove(b+p,b+q,l); } }
}
int main(int argc,char** argv){
    long iters=argc>1?atol(argv[1]):200000; unsigned seed=argc>2?(unsigned)atol(argv[2]):1; srand(seed);
    for(int i=3;i<argc;i++){ FILE* fp=fopen(argv[i],"rb"); if(!fp)continue; static u8 b[65536]; size_t n=fread(b,1,sizeof b,fp); fclose(fp); run_one(b,n); printf("replayed %s\n",argv[i]); }
    run_one(R1,sizeof R1); run_one(R2,sizeof R2); run_one(R3,sizeof R3); run_one(R4,sizeof R4); run_one(R5,sizeof R5); run_one(R6,sizeof R6); puts("netfuzz: regressions ok");
    static u8 b[8192]; long runs=0;
    for(long it=0;it<iters;it++){
        size_t n=2; b[0]=(u8)rand(); b[1]=(u8)rand(); int nf=1+rand()%4;
        for(int f=0;f<nf&&n<sizeof b-1700;f++){ int l=(rand()%3==0)?rand()%80:rand()%700; if(rand()%16==0)l=1500+rand()%100; b[n]=(u8)l; b[n+1]=(u8)(l>>8); n+=2;
            for(int i=0;i<l;i++)b[n+i]=(u8)(rand()%4?rand():0); if(l>14){ b[n+12]=8; b[n+13]=0; } if(l>34)b[n+14]=(u8)(0x40|(rand()%4?5:rand()%16)); n+=l; }
        int m=rand()%4; for(int j=0;j<=m;j++){ mutate(b,&n,sizeof b); run_one(b,n); runs++; }
    }
    printf("netfuzz: %ld inputs, no faults\n",runs); return 0;
}
