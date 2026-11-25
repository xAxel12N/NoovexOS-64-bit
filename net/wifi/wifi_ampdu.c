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
    if(s->on){ amp_all(s); s->head=ssn&0xFFF; s->win=(bufsz&&bufsz<AMPDU_WIN)?bufsz:AMPDU_WIN; s->t0=now; g_ampdu_addba++; return s->win; }
    amp_purge();
    for(int k=0;k<AMPDU_SLOTS;k++)s->st[k]=0;
    s->npend=0; s->tid=tid; s->head=ssn&0xFFF; s->win=(bufsz&&bufsz<AMPDU_WIN)?bufsz:AMPDU_WIN; s->t0=now; s->on=1;
    g_ampdu_addba++; amp_count(); return s->win; }
void ampdu_delba(int tid){ amp_sess* s=amp_find(tid); if(!s)return; amp_all(s); s->on=0; g_ampdu_delba++; amp_count(); }
int ampdu_rx(int tid,u16 seq,const u8* f,int len,u64 pn,u64 now){
    amp_sess* s=amp_find(tid); if(!s)return 0;
    seq&=0xFFF; if(len<0||len>AMPDU_SLOT||!f)len=0;
    if(pn&&s->npend)for(int j=0;j<AMPDU_SLOTS;j++)if(s->st[j]==1&&s->pn[j]==pn){ g_ampdu_old++; return 2; }
    if(g_amp_rn){ g_ampdu_old++; return 2; }
    u16 d=(u16)((seq-s->head)&0xFFF);
    if(d>=2048){ g_ampdu_old++; return 2; }
    if(d>=2*s->win){ amp_all(s); s->head=seq; }
    else if(d>=s->win){ u16 nh=(u16)((seq-s->win+1)&0xFFF); while(s->head!=nh){ amp_rel(s,s->head); s->head=(u16)((s->head+1)&0xFFF); } }
    int k=seq%AMPDU_SLOTS; if(s->st[k]){ g_ampdu_old++; amp_inorder(s); return 2; }
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
int g_ampdu_tx_on=1, g_ampdu_tx_auto=0, g_ampdu_tx_hard=0;
u32 g_ampdu_tx_req=0, g_ampdu_tx_ok=0, g_ampdu_tx_fail=0, g_ampdu_bar=0, g_ampdu_prot=0;
static int at_st=0, at_bad=0, at_w0=0; static u32 at_wtx=0, at_wrx=0; static u64 at_wt=0; static u8 at_tok=0; static u16 at_win=0, at_seq=0; static u64 at_t=0, at_t0=0;
static void at_fail(u64 now){ g_ampdu_tx_fail++; at_st=0; at_t=now+AMPDU_TX_RETRY; if(++at_bad>=AMPDU_TX_MAXBAD){ g_ampdu_tx_on=0; g_ampdu_tx_auto=1; } }
void ampdu_tx_reset(u64 now){ at_st=0; at_bad=0; at_win=0; at_t=now+AMPDU_TX_DELAY; }
int ampdu_tx_want(u64 now){ return g_ampdu_tx_on&&at_st==0&&at_bad<AMPDU_TX_MAXBAD&&(i64)(now-at_t)>=0; }
u8 ampdu_tx_sent(u64 now){ at_st=1; at_tok=(u8)(at_tok+1); if(!at_tok)at_tok=1; at_t=now+AMPDU_TX_WAIT; g_ampdu_tx_req++; return at_tok; }
int ampdu_tx_resp(u8 tok,u16 status,u16 prm,u64 now){
    if(at_st!=1||tok!=at_tok||((prm>>2)&15)!=0)return 0;
    u16 w=(u16)(prm>>6); if(status||!(prm&2)||!g_ampdu_tx_on){ at_fail(now); return 0; }
    at_win=(w&&w<AMPDU_WIN)?w:AMPDU_WIN; at_st=2; at_t0=now; at_seq=0; at_w0=1; g_ampdu_tx_ok++; return at_win; }
void ampdu_tx_tick(u64 now){ if(at_st==1&&(i64)(now-at_t)>=0)at_fail(now); }
void ampdu_tx_peer_delba(u64 now){ if(at_st!=2)return; if(now-at_t0<AMPDU_TX_EARLY)at_fail(now); else { at_st=0; at_t=now+AMPDU_TX_RETRY; } }
int ampdu_tx_stop(void){ int a=at_st==2; at_st=0; at_t=~0ull>>1; return a; }
int ampdu_tx_active(void){ return at_st==2; }
int ampdu_tx_watch(u32 tx,u32 rx,u64 now){ if(at_st!=2){ at_w0=1; return 0; }
    if(at_w0||rx!=at_wrx){ at_w0=0; at_wtx=tx; at_wrx=rx; at_wt=now; return 0; }
    return now-at_wt>=AMPDU_TX_STALL&&tx-at_wtx>AMPDU_TX_STALLN; }
int ampdu_tx_early(u64 now){ return at_st==2&&now-at_t0<AMPDU_TX_LOSTWIN; }
void ampdu_tx_bad(u64 now){ at_fail(now); if(++g_ampdu_tx_hard>=AMPDU_TX_MAXBAD){ g_ampdu_tx_on=0; g_ampdu_tx_auto=1; } }
u16 ampdu_tx_win(void){ return at_win; }
u16 ampdu_tx_seq(void){ u16 s=at_seq; at_seq=(u16)((at_seq+1)&0xFFF); return s; }
void ampdu_tx_unseq(u16 s){ if(at_st==2&&at_seq==(u16)((s+1)&0xFFF))at_seq=s; }
int ampdu_bar(int tid,u16 ssn){ amp_sess* s=amp_find(tid); if(!s)return 0; ssn&=0xFFF; g_ampdu_bar++;
    u16 d=(u16)((ssn-s->head)&0xFFF); if(!d||d>=(u16)(2*s->win))return 0;
    while(s->head!=ssn){ amp_rel(s,s->head); s->head=(u16)((s->head+1)&0xFFF); }
    amp_inorder(s); return 1; }
