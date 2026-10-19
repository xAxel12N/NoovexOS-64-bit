#ifndef NVX_NET_LXSOCK_H
#define NVX_NET_LXSOCK_H

#include "include/nvx/base.h"

#define NLXS 128
#define LXS_QMAX 32
#define LXS_RXCAP 65536
#define LXS_TXCAP 32768
enum { LXS_NEW=0, LXS_SYN=1, LXS_EST=2, LXS_DEAD=3, LXS_LISTEN=4, LXS_SYNRCVD=5 };
struct lxsock {
    int used, type, st, err, nb;
    u32 rip; u16 lport, rport; u8 rmac[6]; int have_mac, conn, lo53;
    u32 iss, una, nxt, rcv; u32 pwin;
    u8* rx; int rxn; u8* tx; int txn;
    u64 tlast; int tries;
    int rd_shut, wr_shut, fin_in, fin_out;
    int parent, backlog, qn, q[LXS_QMAX];
};

extern struct lxsock g_lxs[NLXS];

int lxs_new(int type);
void lxs_pump(void);
void lxs_release(int i);
int lxs_connect(int i,u32 dip,u16 dport,int nb);
int lxs_send(int i,const u8* b,int n,u32 dip,u16 dport,int has_to);
int lxs_recv(int i,u8* b,int n,int peek,u32* from,u16* fport);
int lxs_shutdown(int i,int how);
int lxs_poll(int i);
int lxs_bind(int i,u16 port);
int lxs_listen(int i,int backlog);
int lxs_accept(int i);
int lxs_is_lo(u32 ip);
void lxs_lo_tx(const u8* f,int n);

#endif
