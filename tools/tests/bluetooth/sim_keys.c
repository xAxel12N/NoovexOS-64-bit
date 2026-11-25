#include "include/nvx/base.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/bt_sec.h"
static btkey_t sk[8]; static int skn;
static int sk_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
void bt_keys_load(void){}
void bt_keys_save(void){}
btkey_t* bt_keys_find(const u8* a,int le){ for(int i=0;i<skn;i++) if(sk[i].le==le&&sk_eq(sk[i].addr,a))return &sk[i]; return 0; }
btkey_t* bt_keys_find_le(const u8* a,int t){ (void)t; return bt_keys_find(a,1); }
btkey_t* bt_keys_add(const u8* a,int le,int t){ btkey_t* k=bt_keys_find(a,le); if(!k){ if(skn>=8)return &sk[7]; k=&sk[skn++]; } u8* z=(u8*)k; for(int i=0;i<(int)sizeof *k;i++)z[i]=0; for(int i=0;i<6;i++)k->addr[i]=a[i]; k->le=(u8)le; k->atype=(u8)t; return k; }
int bt_keys_forget(const u8* a,int le){ btkey_t* k=bt_keys_find(a,le); if(!k)return 0; *k=sk[--skn]; return 1; }
int bt_keys_count(void){ return skn; }
btkey_t* bt_keys_at(int i){ return i>=0&&i<skn?&sk[i]:0; }
void notify(const char* t,const char* b){ (void)t; (void)b; }
void smp_init(void){}
int smp_event(const u8* e,int n){ (void)e; (void)n; return 0; }
void att_init(void){}
void bt_hogp_tick(void){}
