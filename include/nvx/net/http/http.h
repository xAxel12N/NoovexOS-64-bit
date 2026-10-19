#ifndef NVX_NET_HTTP_H
#define NVX_NET_HTTP_H

#include "include/nvx/base.h"

#define NVX_HOST   "mynexus.site"
#define NVX_CID    "nvx_77a5f6669ec49183c5d2a9b2"          /* NoovexOS app (public) */
#define NVX_SECRET "123456789012"

extern u8 http_buf[262144];
extern char g_nvx_token[320] , g_nvx_user[64] , g_nvx_email[96] , g_nvx_name[64] , g_nvx_state[10];
extern int g_nvx_signedin;
extern char su_msg[96];
extern int g_cloud_fs , g_fs_pulling;
extern volatile u64 g_ticks;
extern volatile int mouse_dx , mouse_dy;
extern volatile int mouse_btn , mouse_btn_prev;
extern volatile int mouse_rbtn , mouse_rbtn_prev;
extern volatile int mouse_scroll;
extern volatile int mouse_mid;
extern int g_mbytes;
extern volatile int mouse_evt;
extern volatile int kh ,kt;
extern volatile unsigned char kdown[128];

int parse_ip(const char*s,u32*out);
int http_fetch_to(const char*host,const char*path,u8*buf,int cap);
int http_fetch(const char*host,const char*path);
void entropy_get(u8* out,int n);
void secure_zero(void*p,int n);
int tcps_open(u32 dip,u16 dport);
int tcps_wr(void* c,const u8* buf,int len);
int tcps_rd(void* c,u8* buf,int max);
void tls_prepare(void);
i64 tls_clock_now(void);
int https_fetch_to(const char*host0,const char*path,u8*buf,int cap);
int https_fetch(const char*host,const char*path);
int https_request(const char*host,const char*path,const char*method,const char*xhdr,const char*body,int blen,u8*buf,int cap);
int json_str(const u8* j,int n,const char* key,char* out,int cap);
int nvx_json_int(const u8* j,int n,const char* key,int def);
int nvx_signin(const char* email,const char* pwd);
int nvx_register(const char* uname,const char* pwd);
void nvx_hist_add(const char* url);
void kpush(char c);
int kpop(void);

#endif
