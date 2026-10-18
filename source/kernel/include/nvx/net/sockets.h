#ifndef NVX_NET_SOCKETS_H
#define NVX_NET_SOCKETS_H

#include "include/nvx/base.h"

int sock_alloc(void);
int sock_connect(int sid,u32 dip,u16 dport);
int sock_send(int sid,const u8* buf,int len);
int sock_recv(int sid,u8* out,int maxout);
int sock_close(int sid);

int sock_send_rel(int sid,const u8* buf,int len);
int sock_recv(int sid,u8* out,int maxout);
int sock_close(int sid);
int sock_send(int sid,const u8* buf,int len);
#endif
