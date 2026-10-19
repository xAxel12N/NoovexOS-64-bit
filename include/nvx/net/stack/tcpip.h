#ifndef NVX_NET_TCPIP_H
#define NVX_NET_TCPIP_H

#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"

#define TF_FIN 0x01
#define TF_SYN 0x02
#define TF_RST 0x04
#define TF_PSH 0x08
#define TF_ACK 0x10

extern int (*g_bcastf)(void);
extern void (*g_tx)(const u8*,int);
extern int (*g_rx)(u8*,int);
extern const u8* (*g_mac)(void);
extern int (*g_linkok)(void);
extern int g_netdev_wifi;
extern u16 g_tcp_wnd;

void nd_tx2(const u8* d,int n);
int nd_rx(u8* o,int m);
const u8* nd_mymac(void);
int arp_auto_reply(const u8* f,int n);
int arp_resolve(u32 ip,u8* outmac);
int do_ping(u32 tip);
void udp_send(u32 dip,u16 sport,u16 dport,const u8* pl,int plen,const u8* dstmac);
char* ipstr(char* p,u32 v);
int dhcp_config(void);
int nic_up(void);
int dhcp_renew(void);
int dns_query(const char* host,u32* out_ip);
void tcp_send(u32 dip,u16 sp,u16 dp,const u8* dmac,u32 seq,u32 ack,u8 fl,const u8* data,int dl);
u16 next_ephport(void);
int tcp_get(u32 dip,u16 dport,const char* req,int reqlen,u8* out,int maxout);

#endif
