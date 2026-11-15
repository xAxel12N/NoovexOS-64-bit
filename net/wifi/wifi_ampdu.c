#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/net/wifi/wifi_ampdu.h"
typedef struct { int on, tid, npend, win; u16 head; u64 t0; u8* buf; u8 st[AMPDU_SLOTS]; u16 len[AMPDU_SLOTS]; u64 pn[AMPDU_SLOTS]; } amp_sess;
static amp_sess g_amp[AMPDU_SESS];
static u8 g_amp_rq[2*AMPDU_SLOTS][2]; static int g_amp_rh=0, g_amp_rn=0;
int g_ampdu_on=1;
u32 g_ampdu_sess=0, g_ampdu_addba=0, g_ampdu_delba=0, g_ampdu_decl=0, g_ampdu_reord=0, g_ampdu_flush=0, g_ampdu_old=0;
static void amp_count(void){ u32 n=0; for(int i=0;i<AMPDU_SESS;i++)if(g_amp[i].on)n++; g_ampdu_sess=n; }
static amp_sess* amp_find(int tid){ for(int i=0;i<AMPDU_SESS;i++) if(g_amp[i].on&&g_amp[i].tid==tid)return &g_amp[i]; return 0; }
static void amp_purge(void){ g_amp_rh=g_amp_rn=0; for(int i=0;i<AMPDU_SESS;i++)for(int k=0;k<AMPDU_SLOTS;k++)if(g_amp[i].st[k]==2)g_amp[i].st[k]=0; }
static void amp_rel(amp_sess* s,u16 seq){ int k=seq%AMPDU_SLOTS; if(s->st[k]!=1)return; s->npend--;
    if(g_amp_rn>=2*AMPDU_SLOTS){ s->st[k]=0; return; }
    s->st[k]=2; int t=(g_amp_rh+g_amp_rn)%(2*AMPDU_SLOTS); g_amp_rq[t][0]=(u8)(s-g_amp); g_amp_rq[t][1]=(u8)k; g_amp_rn++; }
static void amp_inorder(amp_sess* s){ while(s->st[s->head%AMPDU_SLOTS]==1){ amp_rel(s,s->head); s->head=(s->head+1)&0xFFF; } }
static int amp_all(amp_sess* s){ int n=0; u16 last=s->head;
    for(int k=0;k<s->win&&s->npend;k++){ u16 q=(u16)((s->head+k)&0xFFF); if(s->st[q%AMPDU_SLOTS]==1){ amp_rel(s,q); last=q; n++; } }
    if(n){ s->head=(u16)((last+1)&0xFFF); } return n; }
void ampdu_reset(void){ for(int i=0;i<AMPDU_SESS;i++){ amp_sess* s=&g_amp[i]; s->on=0; s->npend=0; for(int k=0;k<AMPDU_SLOTS;k++)s->st[k]=0; } g_amp_rh=g_amp_rn=0; amp_count(); }
int ampdu_active(int tid){ return amp_find(tid)!=0; }
int ampdu_addba(int tid,u16 ssn,u16 bufsz,u64 now){
    if(!g_ampdu_on||tid<0||tid>15){ g_ampdu_decl++; return 0; }
    amp_sess* s=amp_find(tid);
    if(!s)for(int i=0;i<AMPDU_SESS;i++)if(!g_amp[i].on){ s=&g_amp[i]; break; }
    if(!s){ g_ampdu_decl++; return 0; }
    if(!s->buf)s->buf=(u8*)kmalloc((unsigned long)AMPDU_SLOTS*AMPDU_SLOT);
    if(!s->buf){ g_ampdu_decl++; return 0; }
    amp_purge();
    for(int k=0;k<AMPDU_SLOTS;k++)s->st[k]=0;
    s->npend=0; s->tid=tid; s->head=ssn&0xFFF; s->win=(bufsz&&bufsz<AMPDU_WIN)?bufsz:AMPDU_WIN; s->t0=now; s->on=1;
    g_ampdu_addba++; amp_count(); return s->win; }
void ampdu_delba(int tid){ amp_sess* s=amp_find(tid); if(!s)return; amp_all(s); s->on=0; g_ampdu_delba++; amp_count(); }
int ampdu_rx(int tid,u16 seq,const u8* f,int len,u64 pn,u64 now){
    amp_sess* s=amp_find(tid); if(!s)return 0;
    seq&=0xFFF; if(len<0||len>AMPDU_SLOT||!f)len=0;
    if(g_amp_rn){ g_ampdu_old++; return 1; }
    u16 d=(u16)((seq-s->head)&0xFFF);
    if(d>=2048){ g_ampdu_old++; return 1; }
    if(d>=2*s->win){ amp_all(s); s->head=seq; }
    else if(d>=s->win){ u16 nh=(u16)((seq-s->win+1)&0xFFF); while(s->head!=nh){ amp_rel(s,s->head); s->head=(u16)((s->head+1)&0xFFF); } }
    int k=seq%AMPDU_SLOTS; if(s->st[k]){ g_ampdu_old++; amp_inorder(s); return 1; }
    int had=s->npend; u16 h0=s->head;
    u8* b=s->buf+(u32)k*AMPDU_SLOT; for(int i=0;i<len;i++)b[i]=f[i];
    s->len[k]=(u16)len; s->pn[k]=pn; s->st[k]=1; s->npend++;
    if(seq!=s->head)g_ampdu_reord++;
    amp_inorder(s);
    if(s->npend&&(!had||s->head!=h0))s->t0=now;
    return 1; }
void ampdu_tick(u64 now){ for(int i=0;i<AMPDU_SESS;i++){ amp_sess* s=&g_amp[i]; if(s->on&&s->npend&&now-s->t0>=AMPDU_TMO)g_ampdu_flush+=(u32)amp_all(s); } }
int ampdu_pop(u8* out,int max,u64* pn,int* tid){
    while(g_amp_rn){ u8 si=g_amp_rq[g_amp_rh][0], k=g_amp_rq[g_amp_rh][1]; g_amp_rh=(g_amp_rh+1)%(2*AMPDU_SLOTS); g_amp_rn--;
        amp_sess* s=&g_amp[si]; if(s->st[k]!=2)continue; s->st[k]=0; *pn=s->pn[k]; *tid=s->tid;
        int L=s->len[k]; if(L>max)return 0; const u8* b=s->buf+(u32)k*AMPDU_SLOT; for(int i=0;i<L;i++)out[i]=b[i]; return L; }
    return -1; }
