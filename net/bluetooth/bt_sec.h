#ifndef NVX_NET_BLUETOOTH_BT_SEC_H
#define NVX_NET_BLUETOOTH_BT_SEC_H
#include "net/bluetooth/bt_host.h"
#define BTK_MAX 16
#define BTKF_LTK 1
#define BTKF_IRK 2
#define BTKF_CSRK 4
#define BTKF_SC 8
#define BTKF_AUTH 16
typedef struct { u8 used, le, atype, ktype, keysz, flags, rsv[2]; u8 addr[6]; u16 ediv; u8 key[16]; u8 rand[8]; u8 irk[16]; u8 csrk[16]; } btkey_t;
void bt_keys_load(void);
void bt_keys_save(void);
btkey_t* bt_keys_find(const u8* addr,int le);
btkey_t* bt_keys_find_le(const u8* addr,int atype);
btkey_t* bt_keys_add(const u8* addr,int le,int atype);
int bt_keys_forget(const u8* addr,int le);
int bt_keys_count(void);
btkey_t* bt_keys_at(int i);
int bt_rpa_match(const u8 irk[16],const u8* rpa);
void bt_sec_init(void);
void bt_sec_pairing(const u8* addr,int on);
int bt_sec_pair_ok(const u8* addr);
#define BT_BREDR_KEYSZ 16
void bt_sec_conn_down(btconn_t* c);
const char* bt_sec_msg(void);
u32 bt_sec_passkey(void);
enum { BTP_CONFIRM=1, BTP_ENTRY=2 };
#define BTP_TIMEOUT_NS 30000000000ULL
typedef void (*btp_cb)(void* ctx,const u8* addr,int yes,u32 v);
typedef struct { u8 on, kind, le, nd; u8 addr[6]; u32 value; char dig[8]; u64 t0; btp_cb cb; void* ctx; } btprompt_t;
const btprompt_t* bt_sec_prompt(void);
int bt_sec_prompt_open(int kind,int le,const u8* addr,u32 v,btp_cb cb,void* ctx);
void bt_sec_prompt_drop(void* ctx,const u8* addr);
void bt_sec_answer(int yes);
int bt_sec_prompt_key(int sc);
void bt_sec_ui(void);
void bt_sec_tick(void);
int bt_prompt_ui(const btprompt_t* p);
void smp_e(const u8 k[16],const u8 in[16],u8 out[16]);
void smp_c1(const u8 k[16],const u8 r[16],const u8 preq[7],const u8 pres[7],u8 iat,const u8 ia[6],u8 rat,const u8 ra[6],u8 out[16]);
void smp_s1(const u8 k[16],const u8 r1[16],const u8 r2[16],u8 out[16]);
void smp_f4(const u8 u[32],const u8 v[32],const u8 x[16],u8 z,u8 out[16]);
void smp_f5(const u8 w[32],const u8 n1[16],const u8 n2[16],const u8 a1[7],const u8 a2[7],u8 mackey[16],u8 ltk[16]);
void smp_f6(const u8 w[16],const u8 n1[16],const u8 n2[16],const u8 r[16],const u8 io[3],const u8 a1[7],const u8 a2[7],u8 out[16]);
u32 smp_g2(const u8 u[32],const u8 v[32],const u8 x[16],const u8 y[16]);
u32 smp_ah(const u8 k[16],const u8 r[3]);
#endif
