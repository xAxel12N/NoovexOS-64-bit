#define LXN_ENETUNREACH 101
#define LXN_EADDRNOTAVAIL 99
#define LXN_NSN 32
#define LXN_MN 16
#define LXN_MM 24
#define LXN_CGN 64
#define LXN_PL 112
#define LXN_NSFD 27
enum { LXM_BIND=1, LXM_PROC, LXM_SYS, LXM_DEV, LXM_TMP, LXM_OVL, LXM_CG, LXM_PTS, LXM_MQ };
enum { LXK_MNT=1, LXK_UTS, LXK_IPC, LXK_NET, LXK_PID, LXK_USER, LXK_CG };
struct lxn_task { int pid, mnt, uts, ipc, net, pns, kpns, usr, cg, vpid; char root[LXN_PL]; };
struct lxn_mnt { u8 used, type, ro; char at[LXN_PL], src[LXN_PL], up[LXN_PL], lo[LXN_PL], lo2[LXN_PL]; };
struct lxn_mns { int used, n, uns; struct lxn_mnt m[LXN_MM]; };
struct lxn_uts { int used, uns; char host[65], dom[65]; };
struct lxn_pns { int used, parent, next, dead; };
struct lxn_uns { int used, parent, owner; u32 ui, uo, un, gi, go, gn; u8 us, gs, sgdeny; };
struct lxn_cg { int used, parent; char name[32]; long pmax; u64 mmax, quota, period, wstart, wbase, deadns, nthr, thrns, peak, ustamp, ucache, tstamp, oom; int thr; char subctl[32]; };
static struct lxn_task g_lxt[JMAX];
static struct lxn_mns g_mns[LXN_MN];
static struct lxn_uts g_uts[LXN_NSN];
static struct lxn_pns g_pns[LXN_NSN];
static struct lxn_uns g_uns[LXN_NSN];
static int g_ipcns[LXN_NSN], g_netns[LXN_NSN];
static struct lxn_cg g_cg[LXN_CGN];
static int g_lxn_wr, g_lxn_any, g_lxn_cpul, g_lxn_tmpn;
static int g_lxn_ovm=-1; static char g_lxn_ovrel[LX_PATH];
static int g_lxn_mkopq; static char g_lxn_opq[LX_PATH];
static u64 g_lxn_sv[2]; static int g_lxn_svn;
static int g_lxn_rnwh; static char g_lxn_rnsrc[LX_PATH];

static int lxn_len(const char* s){ int n=0; while(s[n])n++; return n; }
static void lxn_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int lxn_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int lxn_pre(const char* p,const char* pre){
    if(pre[0]=='/'&&!pre[1])return 1; int i=0; while(pre[i]){ if(p[i]!=pre[i])return 0; i++; } return p[i]==0||p[i]=='/'; }
static int lxn_cat(char* d,int cap,const char* a,const char* b){
    int o=0; d[0]=0; for(int i=0;a[i];i++){ if(o>=cap-1){ d[o]=0; return -1; } d[o++]=a[i]; }
    if(b&&b[0]){ if(o>0&&d[o-1]=='/'&&b[0]=='/')o--; for(int i=0;b[i];i++){ if(o>=cap-1){ d[o]=0; return -1; } d[o++]=b[i]; } }
    if(!o)d[o++]='/'; d[o]=0; return o; }
static int lxn_whp(char* w,int cap,const char* up,const char* rel){
    int L=lxn_len(rel); int b=L-1; while(b>0&&rel[b]!='/')b--; char dir[LX_PATH]; for(int k=0;k<b&&k<LX_PATH-1;k++)dir[k]=rel[k]; dir[b<LX_PATH?b:LX_PATH-1]=0;
    if(lxn_cat(w,cap,up,dir)<0)return -1; int o=lxn_len(w); if(o+5+(L-b)>=cap)return -1; if(w[o-1]!='/')w[o++]='/'; w[o++]='.'; w[o++]='w'; w[o++]='h'; w[o++]='.'; for(int k=b+1;k<L;k++)w[o++]=rel[k]; w[o]=0; return 0; }
static char* lxn_u(char* o,u64 v){ char b[24]; int n=0; do{ b[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*o++=b[--n]; *o=0; return o; }
static long lxn_atol(const char* s,int* ok){ long v=0; int any=0; while(*s==' '||*s=='\t')s++; while(*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; any=1; if(v>0x3FFFFFFFFFFFL)break; } *ok=any; return v; }

static int lxn_norm(char* p){
    char t[LX_PATH]; int o=0; int i=0; t[o++]='/';
    while(p[i]){ while(p[i]=='/')i++; if(!p[i])break; int j=i; while(p[j]&&p[j]!='/')j++; int L=j-i;
        if(L==1&&p[i]=='.'){ i=j; continue; }
        if(L==2&&p[i]=='.'&&p[i+1]=='.'){ if(o>1){ o--; while(o>1&&t[o-1]!='/')o--; if(o>1)o--; } i=j; continue; }
        if(o>1){ if(o>=LX_PATH-1)return -1; t[o++]='/'; }
        for(int k=0;k<L;k++){ if(o>=LX_PATH-1)return -1; t[o++]=p[i+k]; } i=j; }
    t[o]=0; for(int k=0;k<=o;k++)p[k]=t[k]; return o; }

static struct lxn_task* lxn_t(int s){
    if(s<0||s>=JMAX)return 0; struct lxn_task* t=&g_lxt[s];
    if(t->pid!=g_jp[s].pid){ for(u32 i=0;i<sizeof(*t);i++)((u8*)t)[i]=0; t->pid=g_jp[s].pid; }
    return t; }
static struct lxn_task* lxn_me(void){ return lxn_t(g_jcur>=0?g_jcur:0); }
static int lxn_live(int s){ int st=g_jp[s].state; return st!=S_FREE&&st!=S_TDEAD; }
static int lxn_on(void){ if(!g_lxn_any||g_jcur<0)return 0; struct lxn_task* t=lxn_me(); return t->root[0]||g_mns[t->mnt].n>0; }
int lxn_in_container(void){ if(!g_lxn_any||g_jcur<0)return 0; struct lxn_task* t=lxn_me(); return t->root[0]!=0; }
static int lxn_uroot(void);
static int lxn_uown(int nsuns){ if(g_uid==0)return 1; struct lxn_task* t=lxn_me(); return t->usr&&nsuns==t->usr&&lxn_uroot(); }
static int lxn_uroot(void){
    if(g_uid==0)return 1; struct lxn_task* t=lxn_me(); if(!t->usr)return 0; struct lxn_uns* u=&g_uns[t->usr];
    return u->us&&(u32)g_uid>=u->uo&&(u32)g_uid<u->uo+u->un&&u->ui+((u32)g_uid-u->uo)==0; }

static int lxn_fdref(int kind,int id){ for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==LXN_NSFD&&g_vf[i].fs_idx==((kind<<8)|id))return 1; return 0; }
static int lxn_inuse(int kind,int id){
    for(int s=0;s<JMAX;s++){ if(!lxn_live(s))continue; struct lxn_task* t=&g_lxt[s]; if(t->pid!=g_jp[s].pid)continue;
        int v=kind==LXK_MNT?t->mnt:kind==LXK_UTS?t->uts:kind==LXK_IPC?t->ipc:kind==LXK_NET?t->net:kind==LXK_USER?t->usr:-1;
        if(kind==LXK_PID&&(t->pns==id||t->kpns==id))return 1; if(v==id)return 1; }
    if(kind==LXK_PID) for(int i=1;i<LXN_NSN;i++) if(g_pns[i].used&&g_pns[i].parent==id&&i!=id&&lxn_inuse(LXK_PID,i))return 1;
    if(kind==LXK_USER) for(int i=1;i<LXN_NSN;i++) if(g_uns[i].used&&g_uns[i].parent==id&&i!=id&&lxn_inuse(LXK_USER,i))return 1;
    return lxn_fdref(kind,id); }
static int lxn_alloc(int kind){
    int lim=kind==LXK_MNT?LXN_MN:LXN_NSN;
    for(int i=1;i<lim;i++){ int u=kind==LXK_MNT?g_mns[i].used:kind==LXK_UTS?g_uts[i].used:kind==LXK_PID?g_pns[i].used:kind==LXK_USER?g_uns[i].used:kind==LXK_IPC?g_ipcns[i]:g_netns[i];
        if(u&&lxn_inuse(kind,i))continue;
        if(kind==LXK_MNT){ g_mns[i].used=1; g_mns[i].n=0; for(int k=0;k<LXN_MM;k++)g_mns[i].m[k].used=0; }
        else if(kind==LXK_UTS)g_uts[i].used=1; else if(kind==LXK_PID){ g_pns[i].used=1; g_pns[i].next=0; g_pns[i].parent=0; g_pns[i].dead=0; }
        else if(kind==LXK_USER){ struct lxn_uns* x=&g_uns[i]; for(u32 q=0;q<sizeof(*x);q++)((u8*)x)[q]=0; x->used=1; }
        else if(kind==LXK_IPC)g_ipcns[i]=1; else g_netns[i]=1;
        g_lxn_any=1; return i; }
    return -1; }

static const char* lxn_src(const struct lxn_mnt* m){
    switch(m->type){ case LXM_PROC: return "/proc"; case LXM_SYS: return "/sys"; case LXM_DEV: return "/dev"; case LXM_CG: return "/sys/fs/cgroup"; case LXM_PTS: return "/dev/pts"; case LXM_MQ: return "/dev/mqueue"; default: return m->src; } }
static int lxn_best(int ns,const char* h){
    int best=-1, bl=-1; struct lxn_mns* M=&g_mns[ns]; if(!M->n)return -1;
    for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&M->m[i]; if(!m->used)continue; if(!lxn_pre(h,m->at))continue; int L=lxn_len(m->at); if(L>=bl){ bl=L; best=i; } }
    return best; }

static int lxn_pid_out_slot(int s);
static int lxn_pid_in(int v);
static int lxn_exists(const char* p){ u32 o[10]; return ux_stat2(p,o,0)==0||lx_exists(p); }
static int lxn_wh(const struct lxn_mnt* m,const char* rel,int* opq){
    char dir[LX_PATH], nm[72], w[LX_PATH]; int L=lxn_len(rel); *opq=0;
    for(int i=1;i<=L;i++){ if(rel[i]!='/'&&rel[i])continue;
        int b=i-1; while(b>0&&rel[b]!='/')b--; int ds=b; for(int k=0;k<ds;k++)dir[k]=rel[k]; dir[ds]=0;
        int nl=i-b-1; if(nl<=0||nl>60)continue; nm[0]='.'; nm[1]='w'; nm[2]='h'; nm[3]='.'; for(int k=0;k<nl;k++)nm[4+k]=rel[b+1+k]; nm[4+nl]=0;
        char d2[LX_PATH]; if(lxn_cat(d2,LX_PATH,m->up,dir)<0)return 0; if(lxn_cat(w,LX_PATH,d2,"/")<0)return 0; { int o=lxn_len(w); if(w[o-1]!='/'){ w[o++]='/'; w[o]=0; } int q=0; while(nm[q]&&o<LX_PATH-1)w[o++]=nm[q++]; w[o]=0; }
        if(lxn_exists(w))return 1;
        if(rel[i]){ char od[LX_PATH]; for(int k=0;k<i;k++)od[k]=rel[k]; od[i]=0; char op[LX_PATH]; lxn_cat(op,LX_PATH,m->up,od); int o=lxn_len(op); const char* z="/.wh..wh..opq"; for(int q=0;z[q]&&o<LX_PATH-1;q++)op[o++]=z[q]; op[o]=0; if(lxn_exists(op))*opq=1; } }
    return 0; }
static long lx_mkdir(const char* p,int mode);
static int lxn_upmk(const struct lxn_mnt* m,const char* rel){
    char cur[LX_PATH]; int L=lxn_len(rel);
    for(int i=1;i<L;i++){ if(rel[i]!='/')continue; char sub[LX_PATH]; for(int k=0;k<i;k++)sub[k]=rel[k]; sub[i]=0;
        if(lxn_cat(cur,LX_PATH,m->up,sub)<0)return -1; if(lx_isdir(cur))continue;
        int mode=0755; const char* los[2]={m->lo,m->lo2}; for(int q=0;q<2;q++){ if(!los[q][0])continue; char lp[LX_PATH]; if(lxn_cat(lp,LX_PATH,los[q],sub)<0)continue; u32 o[10]; if(!ux_stat2(lp,o,1)){ mode=(int)(o[0]&07777u); break; } }
        long e=lx_mkdir(cur,mode); if(e<0&&e!=-LX_EEXIST)return -1; }
    return 0; }
static int lxn_copyup(const char* from,const char* to){
    u32 o[10]; if(ux_stat2(from,o,0))return -1;
    if((o[0]&0170000u)==0120000u){ char t[256]; int n=ux_readlink(from,t,255); if(n<0)return -1; t[n]=0; return ux_symlink(t,to)?-1:0; }
    if(o[9]||(o[0]&0170000u)==0040000u){ long e=lx_mkdir(to,(int)(o[0]&07777u)); return (e<0&&e!=-LX_EEXIST)?-1:0; }
    u32 len=0; const u8* d=sysroot_get(from,&len,0);
    if(!d){ int isd=0; int k=ux_lookup(from,&isd,1); if(k<0||isd)return -1; d=(const u8*)fsd(k); len=(u32)FS[k].len; }
    return ux_write_text(to,(const char*)d,(int)len,(o[0]&0170000u)?o[0]:(0100000u|(o[0]&07777u)))<0?-1:0; }
static int lxn_ovl(int ns,int mi,const char* rel,char* out,int cap){
    struct lxn_mnt* m=&g_mns[ns].m[mi]; char up[LX_PATH]; if(lxn_cat(up,LX_PATH,m->up,rel)<0)return -LX_ENAMETOOLONG;
    g_lxn_ovm=mi; lxn_cpy(g_lxn_ovrel,rel,LX_PATH);
    int opq=0; int wh=(rel[0]=='/'&&rel[1])?lxn_wh(m,rel,&opq):0;
    if(lxn_exists(up)){ lxn_cpy(out,up,cap); return 0; }
    if(wh){ if(g_lxn_wr==2){ char w[LX_PATH]; if(lxn_whp(w,LX_PATH,m->up,rel)<0)return -LX_ENAMETOOLONG;
            if(lxn_exists(w)){ ux_unlink(w); g_lxn_mkopq=1; lxn_cpy(g_lxn_opq,up,LX_PATH); } lxn_upmk(m,rel); }
        lxn_cpy(out,up,cap); return 0; }
    if(!opq){ const char* los[2]={m->lo,m->lo2};
        for(int q=0;q<2;q++){ if(!los[q][0])continue; char lp[LX_PATH]; if(lxn_cat(lp,LX_PATH,los[q],rel)<0)continue; if(!lxn_exists(lp))continue;
            if(g_lxn_wr){ if(lxn_upmk(m,rel)<0)return -LX_EIO; if(lxn_copyup(lp,up)<0)return -LX_EIO; lxn_cpy(out,up,cap); return 0; }
            lxn_cpy(out,lp,cap); return 0; } }
    if(g_lxn_wr){ if(lxn_upmk(m,rel)<0)return -LX_EIO; }
    lxn_cpy(out,up,cap); return 0; }
static int lxn_procpid(const char* rest,char* out,int cap){
    struct lxn_task* t=lxn_me(); if(!t->pns||rest[0]!='/'||rest[1]<'0'||rest[1]>'9'){ return lxn_cat(out,cap,"/proc",rest)<0?-LX_ENAMETOOLONG:0; }
    int v=0, i=1; while(rest[i]>='0'&&rest[i]<='9'){ v=v*10+(rest[i]-'0'); i++; if(v>10000000)break; }
    if(rest[i]&&rest[i]!='/')return lxn_cat(out,cap,"/proc",rest)<0?-LX_ENAMETOOLONG:0;
    int g=lxn_pid_in(v); char b[32]; char* o=b; *o++='/'; o=lxn_u(o,(u64)(g>0?g:0x7FFFFFF0)); *o=0;
    char t2[LX_PATH]; if(lxn_cat(t2,LX_PATH,"/proc",b)<0)return -LX_ENAMETOOLONG; return lxn_cat(out,cap,t2,rest+i)<0?-LX_ENAMETOOLONG:0; }
static int lxn_map(const char* view,char* out,int cap){
    struct lxn_task* t=lxn_me(); char h[LX_PATH];
    if(t->root[0]){ if(lxn_cat(h,LX_PATH,t->root,(view[0]=='/'&&!view[1])?"":view)<0)return -LX_ENAMETOOLONG; } else lxn_cpy(h,view,LX_PATH);
    g_lxn_ovm=-1;
    for(int it=0;it<6;it++){
        int mi=lxn_best(t->mnt,h); if(mi<0)break;
        struct lxn_mnt* m=&g_mns[t->mnt].m[mi]; const char* rest=h+lxn_len(m->at); if(m->at[0]=='/'&&!m->at[1])rest=h;
        if(m->ro&&g_lxn_wr)return -30;
        char rel[LX_PATH]; if(rest[0]=='/')lxn_cpy(rel,rest,LX_PATH); else { rel[0]='/'; lxn_cpy(rel+1,rest,LX_PATH-1); }
        if(m->type==LXM_BIND){ char h2[LX_PATH]; if(lxn_cat(h2,LX_PATH,m->src,(rel[0]=='/'&&!rel[1])?"":rel)<0)return -LX_ENAMETOOLONG; if(lxn_eq(h2,h))break; lxn_cpy(h,h2,LX_PATH); continue; }
        if(m->type==LXM_OVL)return lxn_ovl(t->mnt,mi,rel,out,cap);
        if(m->type==LXM_PROC)return lxn_procpid(rel[1]?rel:"",out,cap);
        if(lxn_cat(out,cap,lxn_src(m),(rel[0]=='/'&&!rel[1])?"":rel)<0)return -LX_ENAMETOOLONG; return 0; }
    lxn_cpy(out,h,cap); return 0; }
static void lxn_unmap(const char* host,char* out,int cap){
    struct lxn_task* t=lxn_me(); struct lxn_mns* M=&g_mns[t->mnt]; char cand[LX_PATH]; lxn_cpy(cand,host,LX_PATH); int bl=-1;
    for(int i=0;M->n&&i<LXN_MM;i++){ struct lxn_mnt* m=&M->m[i]; if(!m->used||m->type==LXM_BIND)continue; const char* ss[3]={m->type==LXM_OVL?m->up:lxn_src(m),m->type==LXM_OVL?m->lo:"",m->type==LXM_OVL?m->lo2:""};
        for(int q=0;q<3;q++){ if(!ss[q][0]||!lxn_pre(host,ss[q]))continue; int L=lxn_len(ss[q]); if(L<=bl)continue; bl=L; const char* rest=(ss[q][0]=='/'&&!ss[q][1])?host:host+L; lxn_cat(cand,LX_PATH,m->at,rest); } }
    for(int it=0;it<4&&t->root[0]&&!lxn_pre(cand,t->root);it++){ int best=-1; bl=-1;
        for(int i=0;M->n&&i<LXN_MM;i++){ struct lxn_mnt* m=&M->m[i]; if(!m->used||m->type!=LXM_BIND||!lxn_pre(cand,m->src))continue; int L=lxn_len(m->src); if(L>bl){ bl=L; best=i; } }
        if(best<0)break; struct lxn_mnt* m=&M->m[best]; const char* rest=(m->src[0]=='/'&&!m->src[1])?cand:cand+bl; char c2[LX_PATH]; lxn_cat(c2,LX_PATH,m->at,rest); lxn_cpy(cand,c2,LX_PATH); }
    if(t->root[0]){ if(lxn_pre(cand,t->root)){ const char* r=cand+lxn_len(t->root); lxn_cpy(out,r[0]?r:"/",cap); return; } lxn_cpy(out,"/",cap); return; }
    lxn_cpy(out,cand,cap); }
static int lxn_path0(int dirfd,const char* rel,char* out){
    char v[LX_PATH];
    if(rel[0]=='/')lxn_cpy(v,rel,LX_PATH);
    else { char hb[LX_PATH]; int node=g_dcwd; const char* srb=0;
        if(dirfd!=LX_AT_FDCWD){ struct vfile* vf=lx_vf(dirfd); if(!vf)return -LX_EBADF; if(vf->dev==LX_SRDIR)srb=vf->vpath; else if(vf->dev!=LX_DIRDEV)return -LX_ENOTDIR; else node=vf->fs_idx; }
        if(srb)lxn_cpy(hb,srb,LX_PATH); else dt_path(node,hb);
        char vb[LX_PATH]; lxn_unmap(hb,vb,LX_PATH); if(lxn_cat(v,LX_PATH,vb,"/")<0)return -LX_ENAMETOOLONG; int o=lxn_len(v); for(int i=0;rel[i];i++){ if(o>=LX_PATH-1)return -LX_ENAMETOOLONG; v[o++]=rel[i]; } v[o]=0; }
    if(lxn_norm(v)<0)return -LX_ENAMETOOLONG;
    int e=lxn_map(v,out,LX_PATH); if(e<0)return e; return lxn_len(out); }
static int lxn_resolve(const char* s,char* out){
    u64 slo=g_kb_lo, sn=g_kb_n; g_kb_lo=(u64)s; g_kb_n=(u64)lxn_len(s)+1; int e=lx_path(LX_AT_FDCWD,(u64)s,out,0); g_kb_lo=slo; g_kb_n=sn; return e; }
static int lxn_view(const char* s,char* out){
    if(s[0]=='/')lxn_cpy(out,s,LX_PATH); else { char hb[LX_PATH],vb[LX_PATH]; dt_path(g_dcwd,hb); if(lxn_on())lxn_unmap(hb,vb,LX_PATH); else lxn_cpy(vb,hb,LX_PATH); if(lxn_cat(out,LX_PATH,vb,"/")<0)return -1; int o=lxn_len(out); for(int i=0;s[i];i++){ if(o>=LX_PATH-1)return -1; out[o++]=s[i]; } out[o]=0; }
    return lxn_norm(out); }
static int lxn_atkey(const char* s,char* out){
    char v[LX_PATH]; if(lxn_view(s,v)<0)return -LX_ENAMETOOLONG; struct lxn_task* t=lxn_me();
    if(t->root[0]&&lxn_on()){ if(lxn_cat(out,LX_PATH,t->root,(v[0]=='/'&&!v[1])?"":v)<0)return -LX_ENAMETOOLONG; } else lxn_cpy(out,v,LX_PATH);
    return 0; }
int lxn_absmap(const char* in,char* out,int cap){ if(!lxn_on())return -1; char v[LX_PATH]; lxn_cpy(v,in,LX_PATH); if(lxn_norm(v)<0)return -1; char o[LX_PATH]; int sw=g_lxn_wr, sm=g_lxn_ovm; static char sr[LX_PATH]; static int depth; if(depth>3)return -1; depth++; lxn_cpy(sr,g_lxn_ovrel,LX_PATH); char keep[LX_PATH]; lxn_cpy(keep,sr,LX_PATH); g_lxn_wr=0; int e=lxn_map(v,o,LX_PATH); g_lxn_wr=sw; g_lxn_ovm=sm; lxn_cpy(g_lxn_ovrel,keep,LX_PATH); depth--; if(e<0||lxn_len(o)>=cap)return -2; lxn_cpy(out,o,cap); return 0; }
int lxn_relmap(const char* dirhost,const char* tgt,char* out,int cap){ if(!lxn_on())return -1; char vb[LX_PATH], v[LX_PATH]; lxn_unmap(dirhost,vb,LX_PATH); if(lxn_cat(v,LX_PATH,vb,"/")<0)return -2; int o=lxn_len(v); for(int i=0;tgt[i];i++){ if(o>=LX_PATH-1)return -2; v[o++]=tgt[i]; } v[o]=0; return lxn_absmap(v,out,cap); }
int lxn_libmap(const char* in,char* out,int cap){ if(!in||in[0]!='/')return -1; return lxn_absmap(in,out,cap); }

static int lxn_pid_out_slot(int s){
    struct lxn_task* me=lxn_me(); if(!me->pns)return g_jp[s].pid; struct lxn_task* t=lxn_t(s);
    if(t->pns==me->pns)return t->vpid;
    for(int n=t->pns,g=0;n&&g<LXN_NSN;g++,n=g_pns[n].parent) if(g_pns[n].parent==me->pns)return g_jp[s].pid;
    return 0; }
static int lxn_slot_of_pid(int pid){ for(int s=0;s<JMAX;s++) if(lxn_live(s)&&g_jp[s].pid==pid)return s; return -1; }
static int lxn_pid_out(int gp){ if(gp<=0)return gp; struct lxn_task* me=lxn_me(); if(!me->pns)return gp; int s=lxn_slot_of_pid(gp); if(s>=0)return lxn_pid_out_slot(s);
    for(int j=0;j<JMAX;j++){ struct lxn_task* t=&g_lxt[j]; if(t->pid!=gp)continue; if(t->pns==me->pns)return t->vpid; for(int n=t->pns,g=0;n&&g<LXN_NSN;g++,n=g_pns[n].parent) if(g_pns[n].parent==me->pns)return gp; return 0; }
    return 0; }
static int lxn_pid_in(int v){
    struct lxn_task* me=lxn_me(); if(!me->pns||v<=0)return v;
    for(int s=0;s<JMAX;s++){ if(!lxn_live(s))continue; struct lxn_task* t=lxn_t(s); if(t->pns==me->pns&&t->vpid==v)return g_jp[s].pid; }
    return -1; }
int lxn_tid_user(int s){ if(!g_lxn_any)return g_jp[s].pid; struct lxn_task* t=lxn_t(s); return t->pns?t->vpid:g_jp[s].pid; }
int lxn_proc_vis(int s){ if(!g_lxn_any||g_jcur<0)return g_jp[s].pid; return lxn_pid_out_slot(s); }

static void lxn_child(int ci,u64 flags){
    if(!g_lxn_any)return; struct lxn_task* t=lxn_me(); struct lxn_task* c=&g_lxt[ci]; *c=*t; c->pid=g_jp[ci].pid;
    if(flags&0x10000ULL){ if(c->pns)c->vpid=++g_pns[c->pns].next; return; }
    c->pns=t->kpns?t->kpns:t->pns;
    if(flags&0x20000000ULL){ int n=lxn_alloc(LXK_PID); if(n>0){ g_pns[n].parent=c->pns; c->pns=n; } }
    c->kpns=c->pns; c->vpid=c->pns?++g_pns[c->pns].next:0;
    if(flags&0x20000ULL){ int n=lxn_alloc(LXK_MNT); if(n>0){ g_mns[n]=g_mns[t->mnt]; g_mns[n].used=1; g_mns[n].uns=c->usr; c->mnt=n; } }
    if(flags&0x04000000ULL){ int n=lxn_alloc(LXK_UTS); if(n>0){ if(t->uts)g_uts[n]=g_uts[t->uts]; else { lxn_cpy(g_uts[n].host,g_hostname,65); lxn_cpy(g_uts[n].dom,"(none)",65); } g_uts[n].used=1; g_uts[n].uns=c->usr; c->uts=n; } }
    if(flags&0x08000000ULL){ int n=lxn_alloc(LXK_IPC); if(n>0)c->ipc=n; }
    if(flags&0x40000000ULL){ int n=lxn_alloc(LXK_NET); if(n>0)c->net=n; }
    if(flags&0x10000000ULL){ int n=lxn_alloc(LXK_USER); if(n>0){ g_uns[n].parent=t->usr; g_uns[n].owner=g_uid; c->usr=n; } } }
static const u64 LXN_NSMASK=0x20000ULL|0x04000000ULL|0x08000000ULL|0x10000000ULL|0x20000000ULL|0x40000000ULL;
static long lxn_unshare(u64 f){
    if(f&~(0x7E000000ULL|0x20000ULL|0x200ULL|0x400ULL|0x40000ULL|0x80000000ULL|0x100ULL|0x10000ULL))return -LX_EINVAL;
    if((f&LXN_NSMASK&~0x10000000ULL)&&!(f&0x10000000ULL)&&!lxn_uroot())return -LX_EPERM;
    struct lxn_task* t=lxn_me(); g_lxn_any=1; lxn_me();
    if(f&0x10000000ULL){ int n=lxn_alloc(LXK_USER); if(n<0)return -LX_ENOSPC; g_uns[n].parent=t->usr; g_uns[n].owner=g_uid; t->usr=n; }
    if(f&0x20000ULL){ int n=lxn_alloc(LXK_MNT); if(n<0)return -LX_ENOSPC; g_mns[n]=g_mns[t->mnt]; g_mns[n].used=1; g_mns[n].uns=t->usr; t->mnt=n; }
    if(f&0x04000000ULL){ int n=lxn_alloc(LXK_UTS); if(n<0)return -LX_ENOSPC; if(t->uts)g_uts[n]=g_uts[t->uts]; else { lxn_cpy(g_uts[n].host,g_hostname,65); lxn_cpy(g_uts[n].dom,"(none)",65); } g_uts[n].used=1; g_uts[n].uns=t->usr; t->uts=n; }
    if(f&0x08000000ULL){ int n=lxn_alloc(LXK_IPC); if(n<0)return -LX_ENOSPC; t->ipc=n; }
    if(f&0x40000000ULL){ int n=lxn_alloc(LXK_NET); if(n<0)return -LX_ENOSPC; t->net=n; }
    if(f&0x20000000ULL){ if(t->pns||t->kpns)return -LX_EINVAL; int n=lxn_alloc(LXK_PID); if(n<0)return -LX_ENOSPC; g_pns[n].parent=t->kpns?t->kpns:t->pns; t->kpns=n; }
    return 0; }

static int lxn_newmnt(int ns,const char* at,int type){
    struct lxn_mns* M=&g_mns[ns]; for(int i=0;i<LXN_MM;i++) if(!M->m[i].used){ struct lxn_mnt* m=&M->m[i]; for(u32 q=0;q<sizeof(*m);q++)((u8*)m)[q]=0; m->used=1; m->type=(u8)type; lxn_cpy(m->at,at,LXN_PL); M->n++; M->used=1; g_lxn_any=1; return i; }
    return -1; }
static int lxn_kstr(u64 p,char* out,int cap){ if(!p){ out[0]=0; return 0; } int n=lx_str(p,out,cap); return n; }
static int lxn_opt(const char* d,const char* key,char* out,int cap){
    int kl=lxn_len(key); for(int i=0;d[i];){ int j=i; while(d[j]&&d[j]!=',')j++; if(j-i>kl&&d[i+kl]=='='){ int ok=1; for(int k=0;k<kl;k++) if(d[i+k]!=key[k])ok=0; if(ok){ int o=0; for(int k=i+kl+1;k<j&&o<cap-1;k++)out[o++]=d[k]; out[o]=0; return 1; } } i=d[j]?j+1:j; }
    out[0]=0; return 0; }
static long lxn_mount(u64 usrc,u64 utgt,u64 utype,u64 flags,u64 udata,int* handled){
    char src[LX_PATH], tgt[LX_PATH], ty[32], data[512]; *handled=1;
    if(lxn_kstr(utype,ty,32)<0)ty[0]=0; if(lxn_kstr(usrc,src,LX_PATH)<0)return -LX_EFAULT; int tn=lx_str(utgt,tgt,LX_PATH); if(tn<0)return tn;
    data[0]=0; if(udata&&!(flags&4096ULL)){ int dn=lx_str(udata,data,512); if(dn<0)data[0]=0; }
    struct lxn_task* t=lxn_me();
    if(flags&((1ULL<<17)|(1ULL<<18)|(1ULL<<19)|(1ULL<<20))){ if(!lxn_uroot())return -LX_EPERM; return 0; }
    int known=(flags&4096ULL)||(flags&8192ULL)||lxn_eq(ty,"proc")||lxn_eq(ty,"sysfs")||lxn_eq(ty,"devtmpfs")||lxn_eq(ty,"tmpfs")||lxn_eq(ty,"overlay")||lxn_eq(ty,"cgroup2")||lxn_eq(ty,"devpts")||lxn_eq(ty,"mqueue")||lxn_eq(ty,"ramfs");
    if(!known){ if(t->mnt||t->root[0]){ return -LX_ENODEV; } *handled=0; return 0; }
    if(!(g_uid==0||(t->mnt&&lxn_uown(g_mns[t->mnt].uns))))return -LX_EPERM;
    if(g_uid!=0&&!(lxn_eq(ty,"proc")||lxn_eq(ty,"tmpfs")||lxn_eq(ty,"sysfs")||lxn_eq(ty,"devpts")||lxn_eq(ty,"mqueue")||lxn_eq(ty,"ramfs")))return -LX_EPERM;
    if(g_uid!=0&&(flags&(4096ULL|8192ULL)))return -LX_EPERM;
    if(lx_slen(tgt)>=LXN_PL-8)return -LX_ENAMETOOLONG;
    char at[LX_PATH]; if(lxn_atkey(tgt,at)<0)return -LX_ENAMETOOLONG;
    { char hp[LX_PATH]; if(lxn_resolve(tgt,hp)<0||!lxn_exists(hp))return -LX_ENOENT; }
    if(flags&32ULL){ int mi=-1; for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&g_mns[t->mnt].m[i]; if(m->used&&lxn_eq(m->at,at))mi=i; } if(mi<0)return -LX_EINVAL; g_mns[t->mnt].m[mi].ro=(flags&1ULL)?1:0; return 0; }
    if(flags&8192ULL){ char from[LX_PATH]; if(lxn_atkey(src,from)<0)return -LX_ENAMETOOLONG; int mi=-1; for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&g_mns[t->mnt].m[i]; if(m->used&&lxn_eq(m->at,from))mi=i; } if(mi<0)return -LX_EINVAL; lxn_cpy(g_mns[t->mnt].m[mi].at,at,LXN_PL); return 0; }
    int type=(flags&4096ULL)?LXM_BIND:lxn_eq(ty,"proc")?LXM_PROC:lxn_eq(ty,"sysfs")?LXM_SYS:lxn_eq(ty,"devtmpfs")?LXM_DEV:lxn_eq(ty,"overlay")?LXM_OVL:lxn_eq(ty,"cgroup2")?LXM_CG:lxn_eq(ty,"devpts")?LXM_PTS:lxn_eq(ty,"mqueue")?LXM_MQ:LXM_TMP;
    char hs[LX_PATH]; hs[0]=0; char up[LX_PATH], lo[LX_PATH], lo2[LX_PATH]; up[0]=lo[0]=lo2[0]=0;
    if(type==LXM_BIND){ char hx[LX_PATH]; if(lxn_resolve(src,hx)<0||!lxn_exists(hx))return -LX_ENOENT; if(lxn_atkey(src,hs)<0)return -LX_ENAMETOOLONG; if(lxn_eq(hs,at))return 0; }
    if(type==LXM_TMP){ char b[64]; char* o=b; const char* z="/tmp/.nvx-tmpfs"; while(*z)*o++=*z++; *o=0; if(!lx_isdir(b))lx_mkdir(b,0755);
        for(int tries=0;tries<1000;tries++){ o=b+15; *o++='/'; o=lxn_u(o,(u64)++g_lxn_tmpn); *o=0; if(!lxn_exists(b))break; }
        if(lx_mkdir(b,01777)<0)return -LX_ENOSPC; lxn_cpy(hs,b,LX_PATH); }
    if(type==LXM_OVL){ char l[LX_PATH], u[LX_PATH]; if(!lxn_opt(data,"lowerdir",l,LX_PATH))return -LX_EINVAL; int hasu=lxn_opt(data,"upperdir",u,LX_PATH);
        char l1[LX_PATH], l2[LX_PATH]; int c=0; while(l[c]&&l[c]!=':')c++; for(int i=0;i<c;i++)l1[i]=l[i]; l1[c]=0; l2[0]=0; if(l[c]){ int o=0; for(int i=c+1;l[i]&&l[i]!=':'&&o<LX_PATH-1;i++)l2[o++]=l[i]; l2[o]=0; }
        if(lxn_resolve(l1,lo)<0||!lx_isdir(lo))return -LX_ENOENT; if(l2[0]&&(lxn_resolve(l2,lo2)<0||!lx_isdir(lo2)))return -LX_ENOENT;
        if(hasu){ if(lxn_resolve(u,up)<0||!lx_isdir(up))return -LX_ENOENT; } else { char b[64]; char* o=b; const char* z="/tmp/.nvx-tmpfs"; while(*z)*o++=*z++; *o=0; if(!lx_isdir(b))lx_mkdir(b,0755); o=b+15; *o++='/'; o=lxn_u(o,(u64)++g_lxn_tmpn); *o=0; lx_mkdir(b,0755); lxn_cpy(up,b,LX_PATH); } }
    if(lxn_len(at)>=LXN_PL||lxn_len(hs)>=LXN_PL||lxn_len(up)>=LXN_PL||lxn_len(lo)>=LXN_PL||lxn_len(lo2)>=LXN_PL)return -LX_ENAMETOOLONG;
    int mi=lxn_newmnt(t->mnt,at,type); if(mi<0)return -LX_ENOSPC; struct lxn_mnt* m=&g_mns[t->mnt].m[mi];
    lxn_cpy(m->src,hs,LXN_PL); lxn_cpy(m->up,up,LXN_PL); lxn_cpy(m->lo,lo,LXN_PL); lxn_cpy(m->lo2,lo2,LXN_PL); m->ro=(flags&1ULL)?1:0;
    return 0; }
static long lxn_umount(u64 utgt,int fl,int* handled){
    *handled=1; char tgt[LX_PATH]; int n=lx_str(utgt,tgt,LX_PATH); if(n<0)return n; struct lxn_task* t=lxn_me();
    char at[LX_PATH]; if(lxn_atkey(tgt,at)<0)return -LX_ENAMETOOLONG; (void)fl;
    int mi=-1; for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&g_mns[t->mnt].m[i]; if(m->used&&lxn_eq(m->at,at))mi=i; }
    if(mi<0){ if(t->mnt||t->root[0])return -LX_EINVAL; *handled=0; return 0; }
    if(!(g_uid==0||(t->mnt&&lxn_uown(g_mns[t->mnt].uns))))return -LX_EPERM;
    g_mns[t->mnt].m[mi].used=0; g_mns[t->mnt].n--; return 0; }
static long lxn_chroot(u64 up){
    if(!lxn_uroot())return -LX_EPERM; char h[LX_PATH]; int e=lx_path(LX_AT_FDCWD,up,h,0); if(e<0)return e; if(!lx_isdir(h))return lx_exists(h)?-LX_ENOTDIR:-LX_ENOENT;
    char k[LX_PATH]; char s[LX_PATH]; if(lx_str(up,s,LX_PATH)<0)return -LX_EFAULT; if(lxn_atkey(s,k)<0)return -LX_ENAMETOOLONG; if(lxn_len(k)>=LXN_PL)return -LX_ENAMETOOLONG;
    g_lxn_any=1; struct lxn_task* t=lxn_me(); int tg=g_jp[g_jcur].tg;
    (void)t; for(int j=0;j<JMAX;j++){ if(!lxn_live(j)||g_jp[j].tg!=tg)continue; struct lxn_task* q=lxn_t(j); if(k[0]=='/'&&!k[1])q->root[0]=0; else lxn_cpy(q->root,k,LXN_PL); }
    return 0; }
static long lxn_pivot(u64 unew,u64 uold){
    if(!lxn_uroot())return -LX_EPERM; struct lxn_task* t=lxn_me();
    char sn[LX_PATH], so[LX_PATH]; if(lx_str(unew,sn,LX_PATH)<0||lx_str(uold,so,LX_PATH)<0)return -LX_EFAULT;
    char hn[LX_PATH], ho[LX_PATH]; if(lxn_resolve(sn,hn)<0||!lx_isdir(hn))return -LX_EINVAL; if(lxn_resolve(so,ho)<0||!lx_isdir(ho))return -LX_EINVAL;
    char kn[LX_PATH], ko[LX_PATH]; if(lxn_atkey(sn,kn)<0||lxn_atkey(so,ko)<0)return -LX_ENAMETOOLONG; if(!lxn_pre(ko,kn))return -LX_EINVAL;
    char oldroot[LXN_PL]; lxn_cpy(oldroot,t->root[0]?t->root:"/",LXN_PL);
    if(lxn_len(ko)>=LXN_PL)return -LX_ENAMETOOLONG;
    int mi=lxn_newmnt(t->mnt,ko,LXM_BIND); if(mi<0)return -LX_ENOSPC; lxn_cpy(g_mns[t->mnt].m[mi].src,oldroot,LXN_PL);
    for(int j=0;j<JMAX;j++){ if(!lxn_live(j))continue; struct lxn_task* q=lxn_t(j); if(q->mnt!=t->mnt)continue; const char* qr=q->root[0]?q->root:"/"; if(j!=g_jcur&&!lxn_eq(qr,oldroot))continue; lxn_cpy(q->root,kn,LXN_PL); }
    int d=dt_resolve_dir(hn); if(d>=0)g_dcwd=d;
    return 0; }

static int lxn_nsfd_open(const char* hp,int flags){
    if(!lxn_pre(hp,"/proc"))return -9999; const char* q=hp+5; if(*q!='/')return -9999; q++;
    int pid=0, d=0; if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'){ pid=g_jp[g_jcur].pid; q+=4; d=1; } else while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; }
    if(!d||q[0]!='/'||q[1]!='n'||q[2]!='s'||q[3]!='/')return -9999; q+=4;
    { int s0=lxn_slot_of_pid(pid); if(s0>=0&&g_uid!=0&&g_jp[s0].uid!=g_uid)return -LX_EACCES; }
    static const char* const K[]={"","mnt","uts","ipc","net","pid","user","cgroup","pid_for_children",0}; int kind=0; for(int i=1;K[i];i++) if(lxn_eq(q,K[i]))kind=i; if(!kind)return -LX_ENOENT;
    int s=lxn_slot_of_pid(pid); if(s<0)return -LX_ESRCH; struct lxn_task* t=lxn_t(s);
    int k2=kind==8?LXK_PID:kind; int id=k2==LXK_MNT?t->mnt:k2==LXK_UTS?t->uts:k2==LXK_IPC?t->ipc:k2==LXK_NET?t->net:k2==LXK_PID?(kind==8?(t->kpns?t->kpns:t->pns):t->pns):k2==LXK_USER?t->usr:0;
    int vi=jc_newvf(LXN_NSFD,(k2<<8)|id,flags&~0x80000); if(vi<0)return -LX_EMFILE; lxn_cpy(g_vf[vi].vpath,t->root,128);
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static long lxn_nslink(const char* hp,u64 buf,u64 cap){
    if(!lxn_pre(hp,"/proc"))return -9999; const char* q=hp+5; if(*q!='/')return -9999; q++;
    int pid=0, d=0; if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'){ pid=g_jp[g_jcur].pid; q+=4; d=1; } else while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; }
    if(!d||q[0]!='/'||q[1]!='n'||q[2]!='s'||q[3]!='/')return -9999; q+=4;
    static const char* const K[]={"","mnt","uts","ipc","net","pid","user","cgroup",0}; int kind=0; for(int i=1;K[i];i++) if(lxn_eq(q,K[i]))kind=i; if(!kind)return -LX_ENOENT;
    int s=lxn_slot_of_pid(pid); if(s<0)return -LX_ESRCH; struct lxn_task* t=lxn_t(s);
    int id=kind==LXK_MNT?t->mnt:kind==LXK_UTS?t->uts:kind==LXK_IPC?t->ipc:kind==LXK_NET?t->net:kind==LXK_PID?t->pns:kind==LXK_USER?t->usr:0;
    char b[64]; char* o=b; const char* z=K[kind]; while(*z)*o++=*z++; *o++=':'; *o++='['; o=lxn_u(o,4026531835ULL+(u64)kind*64ULL+(u64)id); *o++=']'; *o=0;
    u64 L=(u64)(o-b); if(L>cap)L=cap; if(lx_put(buf,b,L))return -LX_EFAULT; return (long)L; }
static long lxn_setns(int fd,int nst){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LXN_NSFD)return -LX_EINVAL;
    int kind=v->fs_idx>>8, id=v->fs_idx&0xFF;
    static const int F[8]={0,0x20000,0x04000000,0x08000000,0x40000000,0x20000000,0x10000000,0x02000000}; if(nst&&(kind<1||kind>7||nst!=F[kind]))return -LX_EINVAL;
    if(g_uid!=0)return -LX_EPERM; struct lxn_task* t=lxn_me(); g_lxn_any=1;
    switch(kind){ case LXK_MNT: t->mnt=id; lxn_cpy(t->root,v->vpath,LXN_PL); { char h[LX_PATH]; if(lxn_map("/",h,LX_PATH)==0){ int d=dt_resolve_dir(h); if(d>=0)g_dcwd=d; } } break;
        case LXK_UTS: t->uts=id; break; case LXK_IPC: t->ipc=id; break; case LXK_NET: t->net=id; break; case LXK_PID: t->kpns=id; break; case LXK_USER: t->usr=id; break; default: break; }
    return 0; }

static int lxn_cg_path(const char* rel,int* out){
    int cur=0; int i=0; while(rel[i]=='/')i++;
    while(rel[i]){ char nm[32]; int o=0; while(rel[i]&&rel[i]!='/'){ if(o<31)nm[o++]=rel[i]; i++; } nm[o]=0; while(rel[i]=='/')i++;
        int f=-1; for(int c=1;c<LXN_CGN;c++) if(g_cg[c].used&&g_cg[c].parent==cur&&lxn_eq(g_cg[c].name,nm)){ f=c; break; }
        if(f<0){ *out=cur; return rel[i]?-2:-1; } cur=f; }
    *out=cur; return 0; }
static const char* const LXN_CGF[]={"cgroup.procs","cgroup.threads","cgroup.controllers","cgroup.subtree_control","cgroup.events","cgroup.type","cgroup.max.depth","cgroup.max.descendants","cgroup.stat","cpu.max","cpu.stat","cpu.weight","memory.max","memory.current","memory.peak","memory.events","memory.high","memory.min","memory.low","memory.swap.max","pids.max","pids.current","pids.events",0};
static int lxn_cg_file(const char* f){ for(int i=0;LXN_CGF[i];i++) if(lxn_eq(f,LXN_CGF[i]))return i; return -1; }
static int lxn_cg_of(int s){ struct lxn_task* t=lxn_t(s); return t->cg>0&&t->cg<LXN_CGN&&g_cg[t->cg].used?t->cg:0; }
static int lxn_cg_under(int c,int anc){ for(int g=0;g<LXN_CGN;g++){ if(c==anc)return 1; if(c==0)return 0; c=g_cg[c].parent; } return 0; }
static int lxn_cg_tasks(int c){ int n=0; for(int s=0;s<JMAX;s++) if(lxn_live(s)&&g_jp[s].state!=S_ZOMB&&lxn_cg_under(lxn_cg_of(s),c))n++; return n; }
static u64 lxn_pages(u64 cr3){
    if(!cr3)return 0; u64 n=0; u64* pml4=(u64*)(cr3&~0xFFFULL); if(!(pml4[8]&1))return 0; u64* pdpt=(u64*)(pml4[8]&PTE_ADDR);
    for(int a=0;a<512;a++){ if(!(pdpt[a]&1))continue; u64* pd=(u64*)(pdpt[a]&PTE_ADDR); for(int b=0;b<512;b++){ if(!(pd[b]&1))continue; if(pd[b]&0x80){ n+=512; continue; } u64* pt=(u64*)(pd[b]&PTE_ADDR); for(int c=0;c<512;c++) if(pt[c]&1)n++; } }
    return n; }
static u64 lxn_cg_mem(int c){
    struct lxn_cg* g=&g_cg[c]; if(g->ustamp==g_ticks+1)return g->ucache;
    u64 n=0; for(int s=0;s<JMAX;s++){ if(!lxn_live(s)||g_jp[s].state==S_ZOMB||g_jp[s].tg!=s)continue; if(!lxn_cg_under(lxn_cg_of(s),c))continue; n+=lxn_pages(g_jp[s].cr3); }
    n*=4096ULL; g->ucache=n; g->ustamp=g_ticks+1; if(n>g->peak)g->peak=n; return n; }
static u64 lxn_cg_cpu(int c){ u64 n=g_cg[c].deadns; for(int s=0;s<JMAX;s++) if(lxn_live(s)&&g_jp[s].state!=S_ZOMB&&lxn_cg_of(s)==c)n+=g_jp[s].runns; for(int k=1;k<LXN_CGN;k++) if(g_cg[k].used&&g_cg[k].parent==c&&k!=c)n+=lxn_cg_cpu(k); return n; }
static void lxn_cg_move(int pid_g,int c){ int s=lxn_slot_of_pid(pid_g); if(s<0)return; int tg=g_jp[s].tg;
    for(int j=0;j<JMAX;j++){ if(!lxn_live(j)||g_jp[j].tg!=tg)continue; struct lxn_task* t=lxn_t(j); int old=lxn_cg_of(j); if(old!=c){ g_cg[old].deadns+=0; } t->cg=c; } g_lxn_any=1; }
static int lxn_cg_gen(int c,int f,char* out,int cap){
    struct lxn_cg* g=&g_cg[c]; char* o=out; (void)cap;
    switch(f){
    case 0: case 1: for(int s=0;s<JMAX;s++){ if(!lxn_live(s)||g_jp[s].state==S_ZOMB)continue; if(f==0&&g_jp[s].tg!=s)continue; if(lxn_cg_of(s)!=c)continue; int v=lxn_pid_out_slot(s); if(!v)continue; if(o-out>cap-16)break; o=lxn_u(o,(u64)v); *o++='\n'; } *o=0; break;
    case 2: { const char* z="cpu memory pids\n"; while(*z)*o++=*z++; *o=0; break; }
    case 3: { const char* z=g->subctl; while(*z)*o++=*z++; *o++='\n'; *o=0; break; }
    case 4: { const char* z="populated "; while(*z)*o++=*z++; *o++=lxn_cg_tasks(c)?'1':'0'; z="\nfrozen 0\n"; while(*z)*o++=*z++; *o=0; break; }
    case 5: { const char* z="domain\n"; while(*z)*o++=*z++; *o=0; break; }
    case 6: case 7: { const char* z="max\n"; while(*z)*o++=*z++; *o=0; break; }
    case 8: { int nd=0; for(int k=1;k<LXN_CGN;k++) if(g_cg[k].used&&k!=c&&lxn_cg_under(k,c))nd++; const char* z="nr_descendants "; while(*z)*o++=*z++; o=lxn_u(o,(u64)nd); z="\nnr_dying_descendants 0\n"; while(*z)*o++=*z++; *o=0; break; }
    case 9: { if(g->quota==~0ULL){ const char* z="max"; while(*z)*o++=*z++; } else o=lxn_u(o,g->quota); *o++=' '; o=lxn_u(o,g->period?g->period:100000); *o++='\n'; *o=0; break; }
    case 10: { u64 us=lxn_cg_cpu(c)/1000ULL; const char* z="usage_usec "; while(*z)*o++=*z++; o=lxn_u(o,us); z="\nuser_usec "; while(*z)*o++=*z++; o=lxn_u(o,us); z="\nsystem_usec 0\nnr_periods "; while(*z)*o++=*z++; o=lxn_u(o,g->nthr); z="\nnr_throttled "; while(*z)*o++=*z++; o=lxn_u(o,g->nthr); z="\nthrottled_usec "; while(*z)*o++=*z++; o=lxn_u(o,g->thrns/1000ULL); *o++='\n'; *o=0; break; }
    case 11: { const char* z="100\n"; while(*z)*o++=*z++; *o=0; break; }
    case 12: case 16: case 19: { if(f!=12||g->mmax==~0ULL){ const char* z="max\n"; while(*z)*o++=*z++; } else { o=lxn_u(o,g->mmax); *o++='\n'; } *o=0; break; }
    case 13: o=lxn_u(o,lxn_cg_mem(c)); *o++='\n'; *o=0; break;
    case 14: lxn_cg_mem(c); o=lxn_u(o,g->peak); *o++='\n'; *o=0; break;
    case 15: { const char* z="low 0\nhigh 0\nmax "; while(*z)*o++=*z++; o=lxn_u(o,g->oom); z="\noom "; while(*z)*o++=*z++; o=lxn_u(o,g->oom); z="\noom_kill 0\n"; while(*z)*o++=*z++; *o=0; break; }
    case 17: case 18: { *o++='0'; *o++='\n'; *o=0; break; }
    case 20: { if(g->pmax<0){ const char* z="max\n"; while(*z)*o++=*z++; } else { o=lxn_u(o,(u64)g->pmax); *o++='\n'; } *o=0; break; }
    case 21: o=lxn_u(o,(u64)lxn_cg_tasks(c)); *o++='\n'; *o=0; break;
    case 22: { const char* z="max "; while(*z)*o++=*z++; o=lxn_u(o,g->oom); *o++='\n'; *o=0; break; }
    default: return -1; }
    return (int)(o-out); }
static int lxn_cg_root_file(int f){ return f<=8||f==10||f==13; }
static int lxn_cg_split(const char* p,int* c,int* f){
    if(!lxn_pre(p,"/sys/fs/cgroup"))return 0; const char* rel=p+14; int L=lxn_len(rel); while(L>0&&rel[L-1]=='/')L--;
    char r2[LX_PATH]; for(int i=0;i<L;i++)r2[i]=rel[i]; r2[L]=0;
    int cc; int e=lxn_cg_path(r2,&cc); if(e==0){ *c=cc; *f=-1; return 1; }
    if(e==-1){ int b=L; while(b>0&&r2[b-1]!='/')b--; int fi=lxn_cg_file(r2+b); if(fi<0)return -1; if(cc==0&&!lxn_cg_root_file(fi))return -1; *c=cc; *f=fi; return 1; }
    return -1; }
static long lxn_cg_mkdir(const char* p){
    if(!lxn_pre(p,"/sys/fs/cgroup"))return -9999; if(g_uid!=0)return -LX_EACCES; const char* rel=p+14;
    int L=lxn_len(rel); while(L>0&&rel[L-1]=='/')L--; int b=L; while(b>0&&rel[b-1]!='/')b--; if(b>=L)return -LX_EEXIST; if(L-b>31)return -LX_ENAMETOOLONG;
    char par[LX_PATH]; for(int i=0;i<b;i++)par[i]=rel[i]; par[b]=0; int pc; if(lxn_cg_path(par,&pc)!=0)return -LX_ENOENT;
    char nm[32]; int o=0; for(int i=b;i<L;i++)nm[o++]=rel[i]; nm[o]=0; if(lxn_cg_file(nm)>=0)return -LX_EEXIST;
    for(int c=1;c<LXN_CGN;c++) if(g_cg[c].used&&g_cg[c].parent==pc&&lxn_eq(g_cg[c].name,nm))return -LX_EEXIST;
    for(int c=1;c<LXN_CGN;c++) if(!g_cg[c].used){ struct lxn_cg* g=&g_cg[c]; for(u32 q=0;q<sizeof(*g);q++)((u8*)g)[q]=0; g->used=1; g->parent=pc; lxn_cpy(g->name,nm,32); g->pmax=-1; g->mmax=~0ULL; g->quota=~0ULL; g->period=100000; g_lxn_any=1; return 0; }
    return -LX_ENOSPC; }
static long lxn_cg_rmdir(const char* p){
    int c, f; int k=lxn_cg_split(p,&c,&f); if(!k)return -9999; if(k<0||f>=0)return -LX_ENOENT; if(c==0)return -LX_EBUSY; if(g_uid!=0)return -LX_EACCES;
    for(int k2=1;k2<LXN_CGN;k2++) if(g_cg[k2].used&&g_cg[k2].parent==c)return -LX_EBUSY;
    for(int s=0;s<JMAX;s++) if(lxn_live(s)&&g_jp[s].state!=S_ZOMB&&lxn_cg_of(s)==c)return -LX_EBUSY;
    if(g_cg[c].quota!=~0ULL&&g_lxn_cpul>0)g_lxn_cpul--; g_cg[c].used=0; return 0; }
static u64 lxn_size(const char* s,int* ok){ int o2; long v=lxn_atol(s,&o2); *ok=o2; if(!o2)return 0; const char* q=s; while(*q==' ')q++; while(*q>='0'&&*q<='9')q++; u64 m=1; if(*q=='k'||*q=='K')m=1024; else if(*q=='m'||*q=='M')m=1048576; else if(*q=='g'||*q=='G')m=1073741824ULL; return (u64)v*m; }
static int lxn_cg_write(int c,int f,const char* w,int n){
    struct lxn_cg* g=&g_cg[c]; if(g_uid!=0)return -LX_EACCES;
    if(f==0||f==1){ int ok; long v=lxn_atol(w,&ok); if(!ok)return -LX_EINVAL; int gp=v==0?g_jp[g_jcur].pid:lxn_pid_in((int)v); if(gp<=0||lxn_slot_of_pid(gp)<0)return -LX_ESRCH; lxn_cg_move(gp,c); return n; }
    if(f==3){ lxn_cpy(g->subctl,w,32); int L=lxn_len(g->subctl); while(L>0&&(g->subctl[L-1]=='\n'||g->subctl[L-1]==' '))g->subctl[--L]=0; return n; }
    if(c==0)return -LX_EACCES;
    if(f==20){ if(w[0]=='m'&&w[1]=='a'&&w[2]=='x'){ g->pmax=-1; return n; } int ok; long v=lxn_atol(w,&ok); if(!ok||v<0)return -LX_EINVAL; g->pmax=v; return n; }
    if(f==12){ if(w[0]=='m'&&w[1]=='a'&&w[2]=='x'){ g->mmax=~0ULL; return n; } int ok; u64 v=lxn_size(w,&ok); if(!ok)return -LX_EINVAL; g->mmax=v&~4095ULL; return n; }
    if(f==9){ int was=g->quota!=~0ULL; if(w[0]=='m'&&w[1]=='a'&&w[2]=='x')g->quota=~0ULL; else { int ok; long v=lxn_atol(w,&ok); if(!ok||v<1000)return -LX_EINVAL; g->quota=(u64)v; }
        const char* q=w; while(*q&&*q!=' ')q++; if(*q==' '){ int ok; long pv=lxn_atol(q,&ok); if(ok){ if(pv<1000||pv>1000000)return -LX_EINVAL; g->period=(u64)pv; } }
        int now=g->quota!=~0ULL; if(now&&!was)g_lxn_cpul++; if(!now&&was&&g_lxn_cpul>0)g_lxn_cpul--; g->wstart=0; g->thr=0; return n; }
    if(f==11||f==16||f==17||f==18||f==19)return n;
    return -LX_EINVAL; }
static int lxn_cg_pids_ok(void){
    int c=lxn_cg_of(g_jcur); for(int g=0;g<LXN_CGN&&c;g++){ if(g_cg[c].pmax>=0&&lxn_cg_tasks(c)>=g_cg[c].pmax){ g_cg[c].oom++; return 0; } c=g_cg[c].parent; }
    return 1; }
static int lxn_cg_mem_ok(void){
    int c=lxn_cg_of(g_jcur); for(int g=0;g<LXN_CGN&&c;g++){ if(g_cg[c].mmax!=~0ULL&&lxn_cg_mem(c)>=g_cg[c].mmax){ g_cg[c].oom++; return 0; } c=g_cg[c].parent; }
    return 1; }
static int lxn_cg_thr(int c){
    struct lxn_cg* g=&g_cg[c]; if(g->quota==~0ULL)return 0; if(g->tstamp==g_ticks+1)return g->thr;
    u64 now=hr_ns(), per=(g->period?g->period:100000)*1000ULL, used=lxn_cg_cpu(c);
    if(used<g->wbase)g->wbase=used;
    if(!g->wstart||now-g->wstart>=per){ if(g->thr&&g->wstart){ g->thrns+=now-g->wstart>per?per:now-g->wstart; } g->wstart=now; g->wbase=used; g->thr=0; }
    if(!g->thr&&used-g->wbase>=g->quota*1000ULL){ g->thr=1; g->nthr++; }
    g->tstamp=g_ticks+1; return g->thr; }
int lxn_cpu_hold(int s){
    if(!g_lxn_cpul)return 0; struct lxn_task* t=&g_lxt[s]; if(t->pid!=g_jp[s].pid||!t->cg)return 0;
    int c=t->cg; for(int g=0;g<LXN_CGN&&c;g++){ if(c<0||c>=LXN_CGN||!g_cg[c].used)return 0; if(lxn_cg_thr(c))return 1; c=g_cg[c].parent; }
    return 0; }
static int lxn_cg_list(int c,int idx,char* name,int* type){
    int k=0; for(int i=1;i<LXN_CGN;i++) if(g_cg[i].used&&g_cg[i].parent==c&&i!=c){ if(k==idx){ lxn_cpy(name,g_cg[i].name,64); *type=4; return 1; } k++; }
    for(int i=0;LXN_CGF[i];i++){ if(c==0&&!lxn_cg_root_file(i))continue; if(k==idx){ lxn_cpy(name,LXN_CGF[i],64); *type=8; return 1; } k++; }
    return 0; }

static int lxn_pidfile(const char* p,int* slot,const char** f){
    if(!lxn_pre(p,"/proc"))return 0; const char* q=p+5; if(*q!='/')return 0; q++; int pid=0, d=0;
    if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&(q[4]=='/'||!q[4])){ pid=jc_lead(&g_jp[g_jcur])->pid; q+=4; d=1; }
    else while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; if(pid>10000000)return 0; }
    if(!d||*q!='/')return 0; q++; int s=lxn_slot_of_pid(pid); if(s<0)return 0; *slot=s; *f=q; return 1; }
static int lxn_mounts_gen(char* out,int cap,int info){
    struct lxn_task* t=lxn_me(); struct lxn_mns* M=&g_mns[t->mnt]; char* o=out; int id=20;
    { const char* z=info?"1 0 0:1 / / rw - rootfs rootfs rw\n":"rootfs / rootfs rw 0 0\n"; while(*z&&o-out<cap-2)*o++=*z++; }
    for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&M->m[i]; if(!m->used)continue; char v[LX_PATH];
        if(t->root[0]){ if(!lxn_pre(m->at,t->root))continue; const char* r=m->at+lxn_len(t->root); lxn_cpy(v,r[0]?r:"/",LX_PATH); } else lxn_cpy(v,m->at,LX_PATH);
        static const char* const TN[]={"","none","proc","sysfs","devtmpfs","tmpfs","overlay","cgroup2","devpts","mqueue"}; const char* tn=TN[m->type<=9?m->type:1];
        if(o-out>cap-300)break;
        if(info){ o=lxn_u(o,(u64)id++); *o++=' '; o=lxn_u(o,1); *o++=' '; o=lxn_u(o,0); *o++=':'; o=lxn_u(o,(u64)(20+i)); *o++=' '; *o++='/'; *o++=' '; }
        else { const char* z=m->type==LXM_BIND?m->src:tn; while(*z&&o-out<cap-200)*o++=*z++; *o++=' '; }
        for(int k=0;v[k];k++)*o++=v[k]; *o++=' ';
        if(info){ const char* z=m->ro?"ro":"rw"; while(*z)*o++=*z++; z=" - "; while(*z)*o++=*z++; }
        { const char* z=m->type==LXM_BIND?"none":tn; while(*z)*o++=*z++; *o++=' '; }
        if(info){ const char* z=m->type==LXM_BIND?m->src:tn; while(*z&&o-out<cap-60)*o++=*z++; *o++=' '; }
        { const char* z=m->ro?"ro":"rw"; while(*z)*o++=*z++; }
        if(!info){ const char* z=" 0 0"; while(*z)*o++=*z++; }
        *o++='\n'; }
    *o=0; return (int)(o-out); }
static int lxn_map_line(const struct lxn_uns* u,int g,char* o0){
    char* o=o0; u32 in=g?u->gi:u->ui, out=g?u->go:u->uo, n=g?u->gn:u->un; int set=g?u->gs:u->us;
    if(!set)return 0; char b[24]; char* e; int w;
    e=lxn_u(b,in); w=(int)(e-b); for(int i=w;i<10;i++)*o++=' '; for(int i=0;i<w;i++)*o++=b[i]; *o++=' ';
    e=lxn_u(b,out); w=(int)(e-b); for(int i=w;i<10;i++)*o++=' '; for(int i=0;i<w;i++)*o++=b[i]; *o++=' ';
    e=lxn_u(b,n); w=(int)(e-b); for(int i=w;i<10;i++)*o++=' '; for(int i=0;i<w;i++)*o++=b[i]; *o++='\n'; *o=0; return (int)(o-o0); }
static int lxn_pf_read(int s,const char* f,char* out,int cap){
    struct lxn_task* t=lxn_t(s); char* o=out;
    if(lxn_eq(f,"uid_map")||lxn_eq(f,"gid_map")){ int g=f[0]=='g'; if(!t->usr){ const char* z="         0          0 4294967295\n"; while(*z)*o++=*z++; *o=0; return (int)(o-out); } return lxn_map_line(&g_uns[t->usr],g,out); }
    if(lxn_eq(f,"setgroups")){ const char* z=(t->usr&&g_uns[t->usr].sgdeny)?"deny\n":"allow\n"; while(*z)*o++=*z++; *o=0; return (int)(o-out); }
    if(lxn_eq(f,"cgroup")){ *o++='0'; *o++=':'; *o++=':'; int c=lxn_cg_of(s); int ch[16], n=0; while(c&&n<16){ ch[n++]=c; c=g_cg[c].parent; } if(!n)*o++='/'; while(n>0){ *o++='/'; const char* z=g_cg[ch[--n]].name; while(*z)*o++=*z++; } *o++='\n'; *o=0; return (int)(o-out); }
    if((lxn_eq(f,"mounts")||lxn_eq(f,"mountinfo"))&&(t->mnt||t->root[0]))return lxn_mounts_gen(out,cap,f[5]=='i');
    return -1; }
static int lxn_pf_write(int s,const char* f,const char* w,int n){
    struct lxn_task* t=lxn_t(s); if(!t->usr)return -LX_EPERM; struct lxn_uns* u=&g_uns[t->usr];
    if(lxn_eq(f,"setgroups")){ if(u->gs)return -LX_EPERM; if(w[0]=='d')u->sgdeny=1; else if(w[0]=='a')u->sgdeny=0; else return -LX_EINVAL; return n; }
    int g=lxn_eq(f,"gid_map"); if(!g&&!lxn_eq(f,"uid_map"))return -LX_EACCES;
    if(g?u->gs:u->us)return -LX_EPERM;
    int ok1,ok2,ok3; const char* q=w; long a=lxn_atol(q,&ok1); while(*q==' ')q++; while(*q>='0'&&*q<='9')q++; long b=lxn_atol(q,&ok2); while(*q==' ')q++; while(*q>='0'&&*q<='9')q++; long c=lxn_atol(q,&ok3);
    if(!ok1||!ok2||!ok3||c<=0)return -LX_EINVAL;
    if(g_uid!=0){ if(c!=1)return -LX_EPERM; if(g){ if(!u->sgdeny)return -LX_EPERM; if(b!=g_gid)return -LX_EPERM; } else if(b!=u->owner)return -LX_EPERM; }
    if(g){ u->gi=(u32)a; u->go=(u32)b; u->gn=(u32)c; u->gs=1; } else { u->ui=(u32)a; u->uo=(u32)b; u->un=(u32)c; u->us=1; }
    return n; }
static int lxn_pf_name(const char* f){ return lxn_eq(f,"uid_map")||lxn_eq(f,"gid_map")||lxn_eq(f,"setgroups")||lxn_eq(f,"cgroup")||lxn_eq(f,"mounts")||lxn_eq(f,"mountinfo"); }
static int lxn_pf_writable(const char* f){ return lxn_eq(f,"uid_map")||lxn_eq(f,"gid_map")||lxn_eq(f,"setgroups"); }
static int lxn_mounts_path(const char* p){ return lxn_eq(p,"/proc/mounts"); }

static int lxn_sx_read(const char* p,char* out,int cap){
    if(!g_lxn_any&&!lxn_pre(p,"/sys/fs/cgroup"))return -9999;
    { int c,f; int k=lxn_cg_split(p,&c,&f); if(k>0){ if(f<0)return -1; return lxn_cg_gen(c,f,out,cap); } if(k<0)return -1; }
    if(lxn_mounts_path(p)){ struct lxn_task* t=lxn_me(); if(t->mnt||t->root[0])return lxn_mounts_gen(out,cap,0); return -9999; }
    { int s; const char* f; if(lxn_pidfile(p,&s,&f)&&lxn_pf_name(f)){ int n=lxn_pf_read(s,f,out,cap); return n<0?-9999:n; } }
    return -9999; }
static int lxn_sx_stat(const char* p,u32* mode,u64* size){
    { int c,f; int k=lxn_cg_split(p,&c,&f); if(k<0)return -1; if(k>0){ if(f<0){ *mode=0040755u; *size=0; return 1; } static char b[4096]; int n=lxn_cg_gen(c,f,b,4096); int ro=(f==2||f==4||f==5||f==8||f==10||f==13||f==14||f==15||f==21||f==22); *mode=ro?0100444u:0100644u; *size=n<0?0:(u64)n; return 1; } }
    if(!g_lxn_any)return -9999;
    { int s; const char* f; if(lxn_pidfile(p,&s,&f)&&lxn_pf_name(f)){ static char b[4096]; int n=lxn_pf_read(s,f,b,4096); if(n<0)return -9999; *mode=lxn_pf_writable(f)?0100644u:0100444u; *size=(u64)n; return 1; } }
    if(lxn_mounts_path(p)){ struct lxn_task* t=lxn_me(); if(t->mnt||t->root[0]){ *mode=0100444u; *size=0; return 1; } }
    return -9999; }
static int lxn_sx_write(const char* p,const char* in,int n){
    { int c,f; int k=lxn_cg_split(p,&c,&f); if(k<0)return -LX_ENOENT; if(k>0){ if(f<0)return -LX_EISDIR; return lxn_cg_write(c,f,in,n); } }
    { int s; const char* f; if(lxn_pidfile(p,&s,&f)&&lxn_pf_writable(f))return lxn_pf_write(s,f,in,n); }
    return -9999; }
static int lxn_sx_list(const char* p,int idx,char* name,int* type){
    int c,f; int k=lxn_cg_split(p,&c,&f); if(k<=0||f>=0)return -9999; return lxn_cg_list(c,idx,name,type); }
static long lxn_sx_open(const char* p,int flags){
    int c=0,f=-1; int k=lxn_cg_split(p,&c,&f); int s=-1; const char* pf=0; int pk=0;
    if(!k&&g_lxn_any&&lxn_pidfile(p,&s,&pf)&&lxn_pf_name(pf))pk=1;
    if(!k&&!pk&&!(g_lxn_any&&lxn_mounts_path(p)&&(lxn_me()->mnt||lxn_me()->root[0])))return -9999;
    if(k<0)return -LX_ENOENT;
    if(k>0&&f<0){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; lxn_cpy(g_vf[vi].vpath,p,128); int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    if((flags&3)!=0){ if(pk&&!lxn_pf_writable(pf))return -LX_EACCES; if(!lxn_uroot()&&!(pk&&s>=0))return -LX_EACCES; int vi=jc_newvf(LX_SYSW,0,flags&~0x80000); if(vi<0)return -LX_EMFILE; lxn_cpy(g_vf[vi].vpath,p,128); int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    static char b[8192]; int n=lxn_sx_read(p,b,8192); if(n<0)return -LX_ENOENT; return lx_text_fd(b,(u64)n,flags); }

static int lxn_list(const char* dp,int idx,char* name,int* type){
    int ndt=0; if(dt_resolve_dir(dp)>=0){ if(k_readdir_at(dp,idx,name,type)==1)return 1; int lo=0, hi=idx; while(lo<hi){ int mid=(lo+hi)/2; char t2[72]; int ty2; if(k_readdir_at(dp,mid,t2,&ty2)==1)lo=mid+1; else hi=mid; } ndt=lo; }
    int si=idx-ndt; int seen=0;
    for(int k=0;;k++){ int sd=0; if(!sysroot_list(dp,k,name,64,&sd))return 0; char ep[LX_PATH]; if(lxn_cat(ep,LX_PATH,dp,"/")<0)continue; int o=lxn_len(ep); for(int i=0;name[i]&&o<LX_PATH-1;i++)ep[o++]=name[i]; ep[o]=0;
        int isd2=0; if(ndt||dt_resolve_dir(dp)>=0){ if(ux_lookup(ep,&isd2,0)>=0)continue; } if(seen==si){ *type=sd?4:8; return 1; } seen++; } }
static int lxn_ovdir(const char* dp,int* mi,char* rel){
    struct lxn_task* t=lxn_me(); struct lxn_mns* M=&g_mns[t->mnt]; if(!M->n)return 0;
    for(int i=0;i<LXN_MM;i++){ struct lxn_mnt* m=&M->m[i]; if(!m->used||m->type!=LXM_OVL)continue; const char* ss[3]={m->up,m->lo,m->lo2};
        for(int q=0;q<3;q++){ if(!ss[q][0]||!lxn_pre(dp,ss[q]))continue; const char* r=dp+lxn_len(ss[q]); lxn_cpy(rel,r[0]?r:"/",LX_PATH); *mi=i; return 1; } }
    return 0; }
static int lxn_ovl_ent(const char* dp,int idx,char* name,int* type){
    int mi; char rel[LX_PATH]; if(!g_lxn_any||!lxn_ovdir(dp,&mi,rel))return -9999; struct lxn_task* t=lxn_me(); struct lxn_mnt* m=&g_mns[t->mnt].m[mi];
    char ud[LX_PATH]; lxn_cat(ud,LX_PATH,m->up,(rel[0]=='/'&&!rel[1])?"":rel);
    int opq=0; if(rel[0]=='/'&&rel[1])lxn_wh(m,rel,&opq); { char op[LX_PATH]; lxn_cat(op,LX_PATH,ud,"/.wh..wh..opq"); if(lxn_exists(op))opq=1; }
    int c=0; char nm[72]; int ty;
    if(lx_isdir(ud)) for(int k=0;lxn_list(ud,k,nm,&ty);k++){ if(nm[0]=='.'&&nm[1]=='w'&&nm[2]=='h'&&nm[3]=='.')continue; if(c==idx){ lxn_cpy(name,nm,72); *type=ty; return 1; } c++; }
    if(opq)return 0;
    const char* los[2]={m->lo,m->lo2}; for(int q=0;q<2;q++){ if(!los[q][0])continue; char ld[LX_PATH]; lxn_cat(ld,LX_PATH,los[q],(rel[0]=='/'&&!rel[1])?"":rel); if(!lx_isdir(ld))continue;
        for(int k=0;lxn_list(ld,k,nm,&ty);k++){ char e[LX_PATH]; lxn_cat(e,LX_PATH,ud,"/"); int o=lxn_len(e); for(int i=0;nm[i]&&o<LX_PATH-1;i++)e[o++]=nm[i]; e[o]=0; if(lxn_exists(e))continue;
            char w[LX_PATH]; lxn_cat(w,LX_PATH,ud,"/.wh."); o=lxn_len(w); for(int i=0;nm[i]&&o<LX_PATH-1;i++)w[o++]=nm[i]; w[o]=0; if(lxn_exists(w))continue;
            if(q==1){ char e2[LX_PATH]; lxn_cat(e2,LX_PATH,m->lo,(rel[0]=='/'&&!rel[1])?"":rel); lxn_cat(e,LX_PATH,e2,"/"); o=lxn_len(e); for(int i=0;nm[i]&&o<LX_PATH-1;i++)e[o++]=nm[i]; e[o]=0; if(lxn_exists(e))continue; }
            if(c==idx){ lxn_cpy(name,nm,72); *type=ty; return 1; } c++; } }
    return 0; }
static long lxn_ovl_rm(u64 up,int dirfd,int isdir){
    char h[LX_PATH]; int e=lx_path(dirfd,up,h,0); if(e<0)return e; if(g_lxn_ovm<0)return -9999;
    struct lxn_task* t=lxn_me(); struct lxn_mnt* m=&g_mns[t->mnt].m[g_lxn_ovm]; char rel[LX_PATH]; lxn_cpy(rel,g_lxn_ovrel,LX_PATH); if(rel[0]=='/'&&!rel[1])return -LX_EBUSY;
    if(!lxn_exists(h))return -LX_ENOENT; int d=lx_isdir(h); if(isdir&&!d)return -LX_ENOTDIR; if(!isdir&&d)return -LX_EISDIR;
    if(isdir){ char nm[72]; int ty; if(lxn_ovl_ent(h,0,nm,&ty)==1)return -LX_ENOTEMPTY; }
    char up2[LX_PATH]; lxn_cat(up2,LX_PATH,m->up,rel); int inlo=0; const char* los[2]={m->lo,m->lo2}; for(int q=0;q<2;q++){ if(!los[q][0])continue; char lp[LX_PATH]; lxn_cat(lp,LX_PATH,los[q],rel); if(lxn_exists(lp))inlo=1; }
    if(lxn_exists(up2)){ if(isdir){ for(int k=0;;k++){ char nm[72]; int ty; if(!lxn_list(up2,0,nm,&ty))break; char e2[LX_PATH]; lxn_cat(e2,LX_PATH,up2,"/"); int o=lxn_len(e2); for(int i=0;nm[i]&&o<LX_PATH-1;i++)e2[o++]=nm[i]; e2[o]=0; if(ux_unlink(e2))break; if(k>4096)break; } long r=lx_rmdir(up2); if(r<0)return r; } else { long r=lx_unlink(up2); if(r<0)return r; } }
    if(inlo){ if(lxn_upmk(m,rel)<0)return -LX_EIO; char w[LX_PATH]; if(lxn_whp(w,LX_PATH,m->up,rel)<0)return -LX_ENAMETOOLONG;
        if(ux_write_text(w,"",0,0100000u)<0)return -LX_EIO; }
    return 0; }

static long lxn_sockchk(int fd,u64 sa,u64 len,int bind){
    if(!sa||len<4)return 0; u16 fam; if(lx_get(&fam,sa,2))return -LX_EFAULT;
    if(fam==2){ if(len<8)return 0; u8 a[4]; if(lx_get(a,sa+4,4))return -LX_EFAULT; if(a[0]==127)return 0; if(!a[0]&&!a[1]&&!a[2]&&!a[3])return 0; return bind?-LXN_EADDRNOTAVAIL:-LXN_ENETUNREACH; }
    if(fam==10){ if(len<24)return 0; u8 a[16]; if(lx_get(a,sa+8,16))return -LX_EFAULT; int z=1; for(int i=0;i<15;i++) if(a[i])z=0; if(z&&(a[15]==1||a[15]==0))return 0; if(a[10]==0xff&&a[11]==0xff&&a[12]==127)return 0; return bind?-LXN_EADDRNOTAVAIL:-LXN_ENETUNREACH; }
    (void)fd; return 0; }
int lxn_net_isolated(void){ if(!g_lxn_any||g_jcur<0)return 0; return lxn_me()->net!=0; }
static u32 lxn_uid_in(u32 real,int g){
    struct lxn_task* t=lxn_me(); if(!t->usr)return real; struct lxn_uns* u=&g_uns[t->usr]; u32 i=g?u->gi:u->ui, o=g?u->go:u->uo, n=g?u->gn:u->un; int set=g?u->gs:u->us;
    if(set&&real>=o&&real<o+n)return i+(real-o); return 65534; }
static int lxn_uid_ok(u32 inside,int g){
    struct lxn_task* t=lxn_me(); struct lxn_uns* u=&g_uns[t->usr]; u32 i=g?u->gi:u->ui, n=g?u->gn:u->un; int set=g?u->gs:u->us; return set&&inside>=i&&inside<i+n; }

static long lxn_hook_pre(struct sysregs* r){
    u64 nr=r->rax, a1=r->rdi, a2=r->rsi, a3=r->rdx, a4=r->r10, a5=r->r8;
    g_lxn_wr=0; g_lxn_svn=0; g_lxn_mkopq=0; g_lxn_rnwh=0;
    switch(nr){
    case 272: return lxn_unshare(a1);
    case 308: return lxn_setns((int)a1,(int)a2);
    case 161: return lxn_chroot(a1);
    case 155: return lxn_pivot(a1,a2);
    case 165: { int h; long v=lxn_mount(a1,a2,a3,a4,a5,&h); if(h)return v; return LXM_NO; }
    case 166: { int h; long v=lxn_umount(a1,0,&h); if(h)return v; return LXM_NO; }
    case 56: case 435: { u64 f=a1; if(nr==435){ u64 ca[1]; if(lx_get(ca,a1,8))return -LX_EFAULT; f=ca[0]; }
        if((f&LXN_NSMASK&~0x10000000ULL)&&!(f&0x10000000ULL)&&!lxn_uroot())return -LX_EPERM;
        if((f&0x20000000ULL)&&g_lxn_any){ struct lxn_task* t1=lxn_me(); if(t1->pns||t1->kpns)return -LX_EINVAL; }
        if(g_lxn_any&&!lxn_cg_pids_ok())return -LX_EAGAIN; if(g_lxn_any&&!(f&0x10000ULL)&&!(f&0x20000000ULL)){ struct lxn_task* t0=lxn_me(); int tn=t0->kpns?t0->kpns:t0->pns; if(tn&&g_pns[tn].dead)return -LX_ENOMEM; } if(f&LXN_NSMASK)g_lxn_any=1; return LXM_NO; }
    case 57: case 58: if(g_lxn_any&&!lxn_cg_pids_ok())return -LX_EAGAIN; if(g_lxn_any){ struct lxn_task* t0=lxn_me(); int tn=t0->kpns?t0->kpns:t0->pns; if(tn&&g_pns[tn].dead)return -LX_ENOMEM; } return LXM_NO;
    }
    if(!g_lxn_any)return LXM_NO;
    struct lxn_task* t=lxn_me();
    switch(nr){
    case 39: { struct jproc* L=jc_lead(&g_jp[g_jcur]); return t->pns?lxn_t(L->tg)->vpid:L->pid; }
    case 186: return t->pns?t->vpid:g_jp[g_jcur].pid;
    case 110: { if(!t->pns)return LXM_NO; struct jproc* L=jc_lead(&g_jp[g_jcur]); int ps=lxn_slot_of_pid(L->ppid); return ps<0?0:lxn_pid_out_slot(ps); }
    case 218: { long v=LXM_NO; (void)v; if(!t->pns)return LXM_NO; g_jp[g_jcur].ctid=a1; return t->vpid; }
    case 111: case 62: case 129: case 434: case 121: case 124: { if(!t->pns)return LXM_NO; int v=(int)a1;
        if((nr==62)&&v==-1){ int me=g_jp[g_jcur].tg; int any=0; for(int s=0;s<JMAX;s++){ if(!lxn_live(s)||g_jp[s].tg!=s||s==me||g_jp[s].state==S_ZOMB)continue; if(lxn_t(s)->pns!=t->pns)continue; any=1; if(a2)jc_signal_proc(s,(int)a2); } return any?0:-LX_ESRCH; }
        if(nr==111)return LXM_NO; int neg=v<-1; int av=neg?-v:v; if(av==0)return LXM_NO; int g=lxn_pid_in(av); if(g<0)return -LX_ESRCH; g_lxn_sv[0]=r->rdi; g_lxn_svn=1; r->rdi=(u64)(i64)(neg?-g:g); return LXM_NO; }
    case 200: { if(!t->pns)return LXM_NO; int g=lxn_pid_in((int)a1); if(g<0)return -LX_ESRCH; g_lxn_sv[0]=r->rdi; g_lxn_svn=1; r->rdi=(u64)g; return LXM_NO; }
    case 234: { if(!t->pns)return LXM_NO; int g1=lxn_pid_in((int)a1), g2=lxn_pid_in((int)a2); if(g1<0||g2<0)return -LX_ESRCH; g_lxn_sv[0]=r->rdi; g_lxn_sv[1]=r->rsi; g_lxn_svn=2; r->rdi=(u64)g1; r->rsi=(u64)g2; return LXM_NO; }
    case 61: { if(!t->pns)return LXM_NO; int v=(int)a1; int g=v; if(v>0){ g=lxn_pid_in(v); if(g<0)return -LX_ECHILD; } long w=lx_wait4(r,g,a2,(int)a3,a4); return w>0?lxn_pid_out((int)w):w; }
    case 109: { if(!t->pns)return LXM_NO; int p1=(int)a1, p2=(int)a2; int g1=p1?lxn_pid_in(p1):0, g2=p2?lxn_pid_in(p2):0; if(g1<0||g2<0)return -LX_ESRCH; g_lxn_sv[0]=r->rdi; g_lxn_sv[1]=r->rsi; g_lxn_svn=2; r->rdi=(u64)g1; r->rsi=(u64)g2; return LXM_NO; }
    case 63: { if(!t->uts)return LXM_NO; char u[390]; lx_zero(u,390); lx_cpy(u,"NoovexOS",65); lx_cpy(u+65,g_uts[t->uts].host,65); lx_cpy(u+130,"10.86",65); lx_cpy(u+195,"NoovexOS 64 v10.86",65); lx_cpy(u+260,"x86_64",65); lx_cpy(u+325,g_uts[t->uts].dom,65); return lx_put(a1,u,390); }
    case 170: case 171: { if(!t->uts)return LXM_NO; if(!lxn_uown(g_uts[t->uts].uns))return -LX_EPERM; if(a2>64)return -LX_EINVAL; char h[65]; if(lx_get(h,a1,a2))return -LX_EFAULT; h[a2]=0; lxn_cpy(nr==170?g_uts[t->uts].host:g_uts[t->uts].dom,h,65); return 0; }
    case 102: case 107: if(!t->usr)return LXM_NO; return lxn_uid_in((u32)g_uid,0);
    case 104: case 108: if(!t->usr)return LXM_NO; return lxn_uid_in((u32)g_gid,1);
    case 118: case 120: { if(!t->usr)return LXM_NO; u32 v=lxn_uid_in(nr==118?(u32)g_uid:(u32)g_gid,nr==120); if(lx_put(a1,&v,4)||lx_put(a2,&v,4)||lx_put(a3,&v,4))return -LX_EFAULT; return 0; }
    case 105: case 106: { if(!t->usr)return LXM_NO; int g=nr==106; if(!lxn_uid_ok((u32)a1,g))return -LX_EINVAL; struct lxn_uns* u=&g_uns[t->usr]; u32 real=(g?u->go:u->uo)+((u32)a1-(g?u->gi:u->ui)); if(g){ if((int)real!=g_gid&&g_uid!=0)return 0; g_gid=(int)real; g_jp[g_jcur].gid=(int)real; } else { if((int)real!=g_uid&&g_uid!=0)return 0; g_uid=(int)real; g_jp[g_jcur].uid=(int)real; } return 0; }
    case 113: case 114: case 117: case 119: { if(!t->usr)return LXM_NO; int g=nr==114||nr==119; u64 v[3]={a1,a2,a3}; int c=nr<117?2:3; for(int i=0;i<c;i++) if((u32)v[i]!=0xFFFFFFFFu&&!lxn_uid_ok((u32)v[i],g))return -LX_EINVAL; return 0; }
    case 116: if(!t->usr)return LXM_NO; if(g_uns[t->usr].sgdeny)return -LX_EPERM; return 0;
    case 29: case 64: case 68: { if(!t->ipc||!a1)return LXM_NO; g_lxn_sv[0]=r->rdi; g_lxn_svn=1; r->rdi=(u64)(u32)((u32)a1^((u32)t->ipc*0x9E3779B1u)); return LXM_NO; }
    case 240: case 241: { if(!t->ipc)return LXM_NO; static char mq[300]; int n=lx_str(a1,mq,256); if(n<0)return n; char* o=mq+n; *o++='~'; *o++='n'; *o++='s'; o=lxn_u(o,(u64)t->ipc); *o=0; g_kb_lo=(u64)mq; g_kb_n=(u64)(o-mq)+1; g_lxn_sv[0]=r->rdi; g_lxn_svn=1; r->rdi=(u64)mq; return LXM_NO; }
    case 42: case 49: { if(!t->net)return LXM_NO; long v=lxn_sockchk((int)a1,a2,a3,nr==49); return v?v:LXM_NO; }
    case 44: { if(!t->net||!a5)return LXM_NO; long v=lxn_sockchk((int)a1,a5,r->r9,0); return v?v:LXM_NO; }
    case 46: { if(!t->net)return LXM_NO; u64 mh[2]; if(lx_get(mh,a2,16))return -LX_EFAULT; if(!mh[0])return LXM_NO; long v=lxn_sockchk((int)a1,mh[0],(u32)mh[1],0); return v?v:LXM_NO; }
    case 9: { if(!(a4&0x20))return LXM_NO; return lxn_cg_mem_ok()?LXM_NO:-LX_ENOMEM; }
    case 12: { if(!a1||a1<=g_ubrk)return LXM_NO; return lxn_cg_mem_ok()?LXM_NO:(long)g_ubrk; }
    case 25: return lxn_cg_mem_ok()?LXM_NO:-LX_ENOMEM;
    case 2: case 85: case 257: { u64 up=nr==257?a2:a1; int fl=nr==85?0x241:(int)(nr==257?a3:a2); int dfd=nr==257?(int)a1:LX_AT_FDCWD;
        { char hp[LX_PATH]; if(lx_path(dfd,up,hp,0)>=0){ int v=lxn_nsfd_open(hp,fl); if(v!=-9999)return v; } }
        if(fl&0x40)g_lxn_wr=2; else if((fl&3)||(fl&0x200))g_lxn_wr=1; return LXM_NO; }
    case 89: case 267: { u64 up=nr==267?a2:a1; char hp[LX_PATH]; if(lx_path(nr==267?(int)a1:LX_AT_FDCWD,up,hp,0)>=0){ long v=lxn_nslink(hp,nr==267?a3:a2,nr==267?a4:a3); if(v!=-9999)return v;
            if(lxn_on()&&lxn_pre(hp,"/proc")){ int s; const char* f; if(lxn_pidfile(hp,&s,&f)&&(lxn_eq(f,"cwd")||lxn_eq(f,"root")||lxn_eq(f,"exe"))){ char hb[LX_PATH], vb[LX_PATH]; if(f[0]=='r')lxn_cpy(vb,"/",LX_PATH); else if(f[0]=='e'){ struct jproc* L=jc_lead(&g_jp[s]); if(!L->exe[0])return -LX_ENOENT; lxn_unmap(L->exe,vb,LX_PATH); } else { dt_path(jc_lead(&g_jp[s])->cwd,hb); lxn_unmap(hb,vb,LX_PATH); } u64 L=(u64)lxn_len(vb), cap=nr==267?a4:a3; if(L>cap)L=cap; if(lx_put(nr==267?a3:a2,vb,L))return -LX_EFAULT; return (long)L; } } }
        return LXM_NO; }
    case 76: case 77: case 90: case 91: case 92: case 93: case 94: case 132: case 188: case 189: case 197: case 198: case 268: case 260: case 280: case 235: case 261: g_lxn_wr=1; return LXM_NO;
    case 83: case 258: { g_lxn_wr=2; char hp[LX_PATH]; if(lx_path(nr==258?(int)a1:LX_AT_FDCWD,nr==258?a2:a1,hp,0)>=0){ long v=lxn_cg_mkdir(hp); if(v!=-9999)return v; } g_lxn_wr=2; return LXM_NO; }
    case 84: { char hp[LX_PATH]; if(lx_path(LX_AT_FDCWD,a1,hp,0)>=0){ long v=lxn_cg_rmdir(hp); if(v!=-9999)return v; } if(lxn_on()){ long v=lxn_ovl_rm(a1,LX_AT_FDCWD,1); if(v!=-9999)return v; } return LXM_NO; }
    case 87: if(lxn_on()){ long v=lxn_ovl_rm(a1,LX_AT_FDCWD,0); if(v!=-9999)return v; } return LXM_NO;
    case 263: { int isd=((int)a3&0x200)!=0; if(isd){ char hp[LX_PATH]; if(lx_path((int)a1,a2,hp,0)>=0){ long v=lxn_cg_rmdir(hp); if(v!=-9999)return v; } } if(lxn_on()){ long v=lxn_ovl_rm(a2,(int)a1,isd); if(v!=-9999)return v; } return LXM_NO; }
    case 88: case 266: case 86: case 265: case 133: case 259: g_lxn_wr=2; return LXM_NO;
    case 82: case 264: case 316: { if(!lxn_on())return LXM_NO; u64 up=nr==82?a1:a2; int dfd=nr==82?LX_AT_FDCWD:(int)a1; g_lxn_wr=1; char hp[LX_PATH]; if(lx_path(dfd,up,hp,0)>=0&&g_lxn_ovm>=0){ struct lxn_mnt* m=&g_mns[t->mnt].m[g_lxn_ovm]; { char u2[LX_PATH]; lxn_cat(u2,LX_PATH,m->up,g_lxn_ovrel); if(!lxn_exists(u2)){ const char* l0[2]={m->lo,m->lo2}; for(int q=0;q<2;q++){ char lp[LX_PATH]; if(!l0[q][0])continue; lxn_cat(lp,LX_PATH,l0[q],g_lxn_ovrel); if(lx_isdir(lp)){ g_lxn_wr=0; return -18; } } } } const char* los[2]={m->lo,m->lo2}; for(int q=0;q<2;q++){ if(!los[q][0])continue; char lp[LX_PATH]; lxn_cat(lp,LX_PATH,los[q],g_lxn_ovrel); if(lxn_exists(lp)){ g_lxn_rnwh=1; lxn_cpy(g_lxn_rnsrc,g_lxn_ovrel,LX_PATH); g_lxn_ovm=g_lxn_ovm; } } if(g_lxn_rnwh)g_lxn_rnwh=1+g_lxn_ovm; } g_lxn_wr=2; return LXM_NO; }
    }
    return LXM_NO; }
static long lxn_hook_post(struct sysregs* r,u64 nr,long v){
    if(g_lxn_svn==1)r->rdi=g_lxn_sv[0]; else if(g_lxn_svn==2){ r->rdi=g_lxn_sv[0]; r->rsi=g_lxn_sv[1]; } else if(g_lxn_svn==3)r->rsi=g_lxn_sv[1];
    g_lxn_svn=0;
    if(nr==240||nr==241)g_kb_n=0;
    if(g_lxn_mkopq&&v==0&&(nr==83||nr==258)){ char op[LX_PATH]; lxn_cat(op,LX_PATH,g_lxn_opq,"/.wh..wh..opq"); ux_write_text(op,"",0,0100000u); }
    if(g_lxn_rnwh&&v==0){ struct lxn_task* t=lxn_me(); int mi=g_lxn_rnwh-1; if(mi>=0&&mi<LXN_MM&&g_mns[t->mnt].m[mi].used){ struct lxn_mnt* m=&g_mns[t->mnt].m[mi]; char w[LX_PATH]; if(lxn_whp(w,LX_PATH,m->up,g_lxn_rnsrc)==0)ux_write_text(w,"",0,0100000u); } }
    g_lxn_wr=0; g_lxn_mkopq=0; g_lxn_rnwh=0;
    if(v>0&&g_lxn_any&&(nr==56||nr==57||nr==58||nr==435||nr==111||nr==121||nr==124)){ struct lxn_task* t=lxn_me(); if(t->pns)return lxn_pid_out((int)v); }
    return v; }
void lxn_proc_exit(int pid){
    if(!g_lxn_any)return; int s=lxn_slot_of_pid(pid); if(s<0)return; struct lxn_task* t=lxn_t(s);
    int c=lxn_cg_of(s); if(c){ u64 n=0; for(int j=0;j<JMAX;j++) if(lxn_live(j)&&g_jp[j].tg==g_jp[s].tg)n+=g_jp[j].runns; g_cg[c].deadns+=n; }
    if(t->pns&&t->vpid==1){ int ns=t->pns; g_pns[ns].dead=1; for(int j=0;j<JMAX;j++){ if(j==s||!lxn_live(j)||g_jp[j].state==S_ZOMB||g_jp[j].tg!=j)continue; struct lxn_task* q=lxn_t(j); int in=0; for(int n=q->pns,g=0;n&&g<LXN_NSN;g++,n=g_pns[n].parent) if(n==ns){ in=1; break; } if(in)jc_signal_proc(j,9); } } }
