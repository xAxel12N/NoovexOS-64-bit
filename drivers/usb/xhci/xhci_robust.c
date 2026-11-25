#define XHR_MAXDEPTH 5
#define XHR_DEBOUNCE_MS 100
#define XHR_STORM_N 6
#define XHR_STORM_WIN 300u
#define XHR_STORM_HOLD 500u
enum { XHR_OK, XHR_SOFT, XHR_RESET_EP, XHR_SKIP, XHR_RESET_DEV, XHR_FAIL };
typedef struct { int nports, ttt, p2g, ss, hdrlat; u16 chr, delay; } xhr_hub_t;
typedef struct { u32 t0, hold; u8 n; } xhr_storm_t;
u32 xhr_evfull_n, xhr_evlost_n, xhr_storm_n, xhr_warm_n;
static u32 xhr_route(u32 pr,int pd,int port){ if(pd<0||pd>=XHR_MAXDEPTH||port<1)return 0xFFFFFFFFu; return (pr&((1u<<(4*pd))-1u))|((u32)(port>15?15:port)<<(4*pd)); }
static int xhr_hubdesc(const u8* d,int n,int ss,xhr_hub_t* h){
    if(!d||!h||n<7||d[0]<7||(d[1]!=0x29&&d[1]!=0x2A)||d[2]<1)return 0;
    h->nports=d[2]>15?15:d[2]; h->chr=(u16)(d[3]|(d[4]<<8)); h->p2g=d[5]*2; h->ss=ss; h->ttt=ss?0:(h->chr>>5)&3;
    h->hdrlat=ss?d[6]:0; h->delay=(ss&&n>=9&&d[0]>=9)?(u16)(d[7]|(d[8]<<8)):0; return 1; }
static void xhr_tt(int hsp,int hslot,int htts,int http,int csp,int port,int* ts,int* tp){
    if(csp!=1&&csp!=2){ *ts=0; *tp=0; } else if(hsp==3){ *ts=hslot; *tp=port; } else { *ts=htts; *tp=http; } }
static int xhr_mtt_alt(const u8* c,int n,int* iface){
    for(int i=0;i+7<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&bl>=9&&c[i+5]==9&&c[i+7]==2){ if(iface)*iface=c[i+2]; return c[i+3]; } i+=bl; }
    return -1; }
static int xhr_cc_action(int cc,int eptype,int tries){
    int iso=eptype==1||eptype==5;
    switch(cc){ case 1: case 13: return XHR_OK;
        case 6: return XHR_RESET_EP;
        case 3: return tries<2?XHR_RESET_EP:XHR_RESET_DEV;
        case 4: case 36: return iso?XHR_SKIP:tries<3?XHR_SOFT:tries<5?XHR_RESET_EP:XHR_RESET_DEV;
        case 2: return iso?XHR_SKIP:XHR_RESET_EP;
        case 14: case 15: case 18: case 23: case 31: return iso?XHR_SKIP:XHR_RESET_EP;
        case 26: case 27: case 28: return XHR_SKIP;
        case 5: case 11: case 12: case 17: case 19: return XHR_FAIL; }
    return tries<3?XHR_RESET_EP:XHR_FAIL; }
static int xhr_halts(int cc){ return cc==3||cc==6||xhr_cc_action(cc,4,0)==XHR_SOFT; }
static int xhr_storm(xhr_storm_t* s,u32 now){
    if(s->hold){ if((int)(s->hold-now)>0)return 1; s->hold=0; s->n=0; s->t0=now; }
    if(now-s->t0>XHR_STORM_WIN){ s->t0=now; s->n=0; }
    if(++s->n>XHR_STORM_N){ s->hold=(now+XHR_STORM_HOLD)|1u; s->n=0; xhr_storm_n++; return 1; }
    return 0; }
static int xhr_link_bad(u32 st){ u32 pls=(st>>5)&0xF; return pls==6||pls==10; }
static int xhr_max_intrs(u32 hcs1){ int n=(int)((hcs1>>8)&0x7FF); return n<1?1:n; }
static int xhr_intr_target(int want,int maxi){ return want>=0&&want<maxi&&want<1024?want:0; }
static u64 xhr_ir_base(u64 base,u32 rtsoff,int i){ return base+(rtsoff&~0x1Fu)+0x20u+(u64)i*0x20u; }
static int xhr_event_note(u32 ty,u32 cc){ if(ty!=37)return 0; if(cc==21){ xhr_evfull_n++; return 1; } if(cc==32){ xhr_evlost_n++; return 2; } return 0; }
static int xhr_handoff(volatile u32* cap,void (*w)(void),int iters){
    volatile u8* b=(volatile u8*)cap; b[3]=1;
    for(int t=0;t<iters&&(cap[0]&(1u<<16));t++)w();
    int ok=!(cap[0]&(1u<<16)); if(!ok)b[2]=0;
    cap[1]=(cap[1]&~0x0000E011u)|0xE0000000u; return ok; }
