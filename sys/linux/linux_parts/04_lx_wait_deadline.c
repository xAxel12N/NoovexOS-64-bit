static int lx_wait_deadline(struct jproc* p,long ms){
    if(ms==0)return 1;
    if(!p->pollwake)p->pollwake=(ms<0)?~0ULL:g_ticks+((u64)ms+9)/10+1;
    if(g_ticks>=p->pollwake){ p->pollwake=0; return 1; }
    return 0;
}
static long lx_poll(struct sysregs* r,u64 fds,u64 n,long ms){
    struct jproc* p=&g_jp[g_jcur];
    if(n>FDMAX*4)return -LX_EINVAL; if(!lx_okw(fds,n*8))return -LX_EFAULT;
    int cnt=0;
    for(u64 i=0;i<n;i++){ int fd=*(int*)(fds+i*8); short ev=*(short*)(fds+i*8+4); short re=0;
        if(fd>=0)re=(short)lx_poll_one(fd,ev); *(short*)(fds+i*8+6)=re; if(re)cnt++; }
    if(cnt){ p->pollwake=0; return cnt; }
    if(lx_wait_deadline(p,ms))return 0;
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}
#define LX_EPOLL 113
#define EPN 128
#define EPI 256
struct lx_ep { int used, n; int fd[EPN]; int vi[EPN]; u32 gen[EPN]; u32 ev[EPN]; u64 data[EPN]; };
static int lx_ep_live(struct lx_ep* e,int i){ int f=e->fd[i]; if(f<0||f>=FDMAX)return 0; int* t=fdt(); return t[f]>=0&&t[f]==e->vi[i]&&g_vf[t[f]].gen==e->gen[i]&&lx_vf(f); }
static void lx_ep_del(struct lx_ep* e,int k){ e->n--; e->fd[k]=e->fd[e->n]; e->vi[k]=e->vi[e->n]; e->gen[k]=e->gen[e->n]; e->ev[k]=e->ev[e->n]; e->data[k]=e->data[e->n]; }
static struct lx_ep g_ep[EPI];
static void ep_close(int i){ if(i>=0&&i<EPI){ g_ep[i].used=0; g_ep[i].n=0; } }
static long lx_epoll_create(int fl){ int e=-1; for(int i=0;i<EPI;i++) if(!g_ep[i].used){ e=i; break; } if(e<0)return -LX_ENOMEM;
    g_ep[e].used=1; g_ep[e].n=0; int vi=jc_newvf(LX_EPOLL,e,O_RDWR); if(vi<0){ g_ep[e].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(fl&0x80000)?1:0,0); if(fd<0){ vf_unref(vi); g_ep[e].used=0; } return fd; }
static struct lx_ep* lx_ep_of(int fd){ struct vfile* v=lx_vf(fd); if(!v||v->dev!=LX_EPOLL)return 0; if(v->fs_idx<0||v->fs_idx>=EPI)return 0; return &g_ep[v->fs_idx]; }
static long lx_epoll_ctl(int epfd,int op,int fd,u64 uev){
    struct lx_ep* e=lx_ep_of(epfd); if(!e)return -LX_EINVAL; if(!lx_vf(fd))return -LX_EBADF;
    u32 ev=0; u64 data=0; if(op!=2){ u8 b[12]; if(lx_get(b,uev,12))return -LX_EFAULT; ev=*(u32*)b; data=*(u64*)(b+4); }
    for(int i=0;i<e->n;i++) if(!lx_ep_live(e,i)){ lx_ep_del(e,i); i--; }
    int k=-1; for(int i=0;i<e->n;i++) if(e->fd[i]==fd){ k=i; break; }
    if(op==1){ if(k>=0)return -17; if(e->n>=EPN)return -LX_ENOMEM; e->fd[e->n]=fd; e->vi[e->n]=fdt()[fd]; e->gen[e->n]=g_vf[fdt()[fd]].gen; e->ev[e->n]=ev; e->data[e->n]=data; e->n++; return 0; }
    if(op==3){ if(k<0)return -LX_ENOENT; e->ev[k]=ev; e->data[k]=data; return 0; }
    if(op==2){ if(k<0)return -LX_ENOENT; lx_ep_del(e,k); return 0; }
    return -LX_EINVAL; }
static long lx_epoll_wait(struct sysregs* r,int epfd,u64 out,int max,long ms){
    struct jproc* p=&g_jp[g_jcur]; struct lx_ep* e=lx_ep_of(epfd); if(!e)return -LX_EINVAL; if(max<=0)return -LX_EINVAL; if(!lx_okw(out,(u64)max*12))return -LX_EFAULT;
    int cnt=0;
    for(int i=0;i<e->n&&cnt<max;i++){ if(!lx_ep_live(e,i)){ lx_ep_del(e,i); i--; continue; }
        int re=lx_poll_one(e->fd[i],(int)(e->ev[i]&0x3F)); if(!re)continue;
        u8* o=(u8*)(out+(u64)cnt*12); *(u32*)o=(u32)re; *(u64*)(o+4)=e->data[i]; cnt++;
        if(e->ev[i]&0x40000000u)e->ev[i]&=~0x3Fu; }
    if(cnt){ p->pollwake=0; return cnt; }
    if(lx_wait_deadline(p,ms))return 0;
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}
static u64 lx_tv_ticks(const u64* tv){ u64 us=tv[0]*1000000ULL+tv[1]; return (us+9999ULL)/10000ULL; }
static void lx_ticks_tv(u64 t,u64* tv){ tv[0]=t/100ULL; tv[1]=(t%100ULL)*10000ULL; }
static long lx_getitimer(int which,u64 out){
    struct jproc* p=jc_lead(&g_jp[g_jcur]); u64 v[4]={0,0,0,0};
    if(which<0||which>2)return -LX_EINVAL;
    if(which==0){ lx_ticks_tv(p->it_int,v); if(p->it_next>g_ticks)lx_ticks_tv(p->it_next-g_ticks,v+2); else if(p->it_next)v[3]=1; }
    return lx_put(out,v,32);
}
static long lx_setitimer(int which,u64 in,u64 out){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    if(which<0||which>2)return -LX_EINVAL;
    if(out){ long e=lx_getitimer(which,out); if(e)return e; }
    if(!in)return 0;
    u64 v[4]; if(lx_get(v,in,32))return -LX_EFAULT;
    if(v[1]>=1000000ULL||v[3]>=1000000ULL)return -LX_EINVAL;
    if(which!=0)return 0;
    u64 val=lx_tv_ticks(v+2), itv=lx_tv_ticks(v);
    p->it_int=itv; p->it_next=val?g_ticks+val:0;
    return 0;
}
static long lx_alarm(unsigned sec){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    u64 left=p->it_next>g_ticks?p->it_next-g_ticks:0;
    p->it_int=0; p->it_next=sec?g_ticks+(u64)sec*100ULL:0;
    return (long)((left+99ULL)/100ULL);
}
static long lx_select(struct sysregs* r,int n,u64 rd,u64 wr,u64 ex,long ms){
    struct jproc* p=&g_jp[g_jcur];
    if(n<0||n>1024)return -LX_EINVAL;
    int nb=(n+7)/8; u8 R[128],Wm[128],oR[128],oW[128];
    lx_zero(R,128); lx_zero(Wm,128); lx_zero(oR,128); lx_zero(oW,128);
    if(rd&&!lx_okw(rd,(u64)nb))return -LX_EFAULT; if(wr&&!lx_okw(wr,(u64)nb))return -LX_EFAULT; if(rd&&lx_get(R,rd,(u64)nb))return -LX_EFAULT; if(wr&&lx_get(Wm,wr,(u64)nb))return -LX_EFAULT; if(ex&&!lx_okw(ex,(u64)nb))return -LX_EFAULT;
    int cnt=0;
    for(int fd=0;fd<n;fd++){ int ir=(R[fd>>3]>>(fd&7))&1, iw=(Wm[fd>>3]>>(fd&7))&1; if(!ir&&!iw)continue;
        if(fd>=FDMAX||!lx_vf(fd))return -LX_EBADF;
        int re=lx_poll_one(fd,(ir?1:0)|(iw?4:0));
        if(ir&&(re&0x19)){ oR[fd>>3]|=(u8)(1u<<(fd&7)); cnt++; }
        if(iw&&(re&0x0C)){ oW[fd>>3]|=(u8)(1u<<(fd&7)); cnt++; } }
    if(cnt||lx_wait_deadline(p,ms)){ p->pollwake=0; if(rd)lx_put(rd,oR,(u64)nb); if(wr)lx_put(wr,oW,(u64)nb); if(ex){ u8 z[128]; lx_zero(z,128); lx_put(ex,z,(u64)nb); } return cnt; }
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}

struct lx_fm { u64 cr3,va,off,pages; int k; };
static struct lx_fm g_fm[256];
static void lx_fm_add(u64 cr3,u64 va,u64 off,int k,u64 pages){ for(int i=0;i<256;i++) if(!g_fm[i].pages){ g_fm[i].cr3=cr3&~0xFFFULL; g_fm[i].va=va; g_fm[i].off=off; g_fm[i].k=k; g_fm[i].pages=pages; return; } }
static void lx_fm_wb(struct lx_fm* m,u64 a,u64 e){
    int k=m->k; if(k<0||k>=FS_N||!FS[k].used)return; u64 me=m->va+(m->pages<<12); if(a<m->va)a=m->va; if(e>me)e=me;
    for(u64 v=a&~0xFFFULL;v<e;v+=0x1000ULL){ u64* pt=vm_walk(m->cr3,v,0); if(!pt||!(*pt&1))continue; u64 fo=m->off+(v-m->va); if(fo>=(u64)FS[k].len)break;
        u64 n=(u64)FS[k].len-fo; if(n>4096)n=4096; const u8* f=(const u8*)(*pt&PTE_ADDR); char* d=fsd(k); if(!d)return; d+=fo; int ch=0;
        for(u64 b=0;b<n;b++) if((u8)d[b]!=f[b]){ d[b]=(char)f[b]; ch=1; } if(ch){ g_fs_dirty[k]=1; g_fs_wb_pending=1; } } }
static void lx_fm_sync(int k,u64 off,u64 n){
    if(n==0)return; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(!m->pages||m->k!=k)continue; u64 me=m->off+(m->pages<<12); if(off+n<=m->off||off>=me)continue;
        u64 a=off>m->off?off:m->off, e=off+n<me?off+n:me; const char* d=fsd(k); if(!d)return;
        for(u64 q=a;q<e;){ u64 v=m->va+(q-m->off); u64* pt=vm_walk(m->cr3,v&~0xFFFULL,0); u64 pe=(q|0xFFFULL)+1; if(pe>e)pe=e;
            if(pt&&(*pt&1)){ u8* f=(u8*)(*pt&PTE_ADDR); for(u64 b=q;b<pe;b++)f[(m->va+(b-m->off))&0xFFF]=(u8)d[b]; } q=pe; } } }
static void lx_fm_cut(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(!m->pages||m->cr3!=cr3)continue; u64 me=m->va+(m->pages<<12); if(e<=m->va||a>=me)continue; lx_fm_wb(m,a,e); if(a<=m->va&&e>=me)m->pages=0; } }
static void lx_fm_msync(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(m->pages&&m->cr3==cr3)lx_fm_wb(m,a,e); } }
static void lx_fm_dead(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(m->pages&&m->cr3==cr3){ lx_fm_wb(m,m->va,m->va+(m->pages<<12)); m->pages=0; } } }
static void lx_fm_dup(u64 src,u64 dst){ src&=~0xFFFULL; int n=0; for(int i=0;i<256;i++) if(g_fm[i].pages&&g_fm[i].cr3==src)n++; for(int i=0;i<256&&n>0;i++) if(g_fm[i].pages&&g_fm[i].cr3==src){ lx_fm_add(dst,g_fm[i].va,g_fm[i].off,g_fm[i].k,g_fm[i].pages); n--; } }
static void lx_sha_dead(u64 cr3);
static void lx_sha_dup(u64 src,u64 dst);
struct lx_res { u64 cr3,b,e,pb; };
#define LX_RES_MAX 8192
static struct lx_res g_res[LX_RES_MAX]; static int g_res_hi, g_res_last;
static int lx_res_find(u64 cr3,u64 v){ cr3&=~0xFFFULL; int l=g_res_last; if(l<g_res_hi&&g_res[l].e&&g_res[l].cr3==cr3&&v>=g_res[l].b&&v<g_res[l].e)return l;
    for(int i=0;i<g_res_hi;i++) if(g_res[i].e&&g_res[i].cr3==cr3&&v>=g_res[i].b&&v<g_res[i].e){ g_res_last=i; return i; } return -1; }
static u64 lx_res_next(u64 cr3,u64 v){ cr3&=~0xFFFULL; u64 m=~0ULL; for(int i=0;i<g_res_hi;i++) if(g_res[i].e&&g_res[i].cr3==cr3&&g_res[i].b>v&&g_res[i].b<m)m=g_res[i].b; return m; }
static int lx_res_addp(u64 cr3,u64 b,u64 e,u64 pb){ cr3&=~0xFFFULL; if(e<=b)return 0;
    int lo=-1, hi=-1; for(int i=0;i<g_res_hi;i++){ struct lx_res* r=&g_res[i]; if(!r->e||r->cr3!=cr3||r->pb!=pb)continue; if(r->e==b)lo=i; else if(r->b==e)hi=i; }
    if(lo>=0&&hi>=0){ g_res[lo].e=g_res[hi].e; g_res[hi].e=0; return 0; }
    if(lo>=0){ g_res[lo].e=e; return 0; } if(hi>=0){ g_res[hi].b=b; return 0; }
    for(int i=0;i<LX_RES_MAX;i++) if(!g_res[i].e){ g_res[i].cr3=cr3; g_res[i].b=b; g_res[i].e=e; g_res[i].pb=pb; if(i>=g_res_hi)g_res_hi=i+1; return 0; }
    { static int nf; if(nf<4){ nf++; klog("lx: reservation table full"); } } return -1; }
static void lx_res_cut(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL;
    for(int i=0;i<g_res_hi;i++){ struct lx_res* r=&g_res[i]; if(!r->e||r->cr3!=cr3||e<=r->b||a>=r->e)continue;
        if(a<=r->b&&e>=r->e){ r->e=0; continue; }
        if(a<=r->b){ r->b=e; continue; }
        if(e>=r->e){ r->e=a; continue; }
        u64 oe=r->e, opb=r->pb; r->e=a; for(int j=0;j<LX_RES_MAX;j++) if(!g_res[j].e){ g_res[j].cr3=cr3; g_res[j].b=e; g_res[j].e=oe; g_res[j].pb=opb; if(j>=g_res_hi)g_res_hi=j+1; break; } } }
int lx_res_fault(u64 cr3,u64 va){ int ri=lx_res_find(cr3,va); if(ri<0)return -1; u64* e=vm_walk(cr3&~0xFFFULL,va&~0xFFFULL,0); if(e&&vm_pte_used(*e))return -1; return vm_map_lazy_p(cr3&~0xFFFULL,va&~0xFFFULL,g_res[ri].pb)?-1:0; }
void lx_res_dup(u64 src,u64 dst){ lx_fm_dup(src,dst); lx_sha_dup(src,dst); src&=~0xFFFULL; int n=0; int hi=g_res_hi; for(int i=0;i<hi;i++) if(g_res[i].e&&g_res[i].cr3==src)n++; for(int i=0;i<hi&&n>0;i++) if(g_res[i].e&&g_res[i].cr3==src){ lx_res_addp(dst,g_res[i].b,g_res[i].e,g_res[i].pb); n--; } }
void lx_res_drop(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<g_res_hi;i++) if(g_res[i].cr3==cr3)g_res[i].e=0; while(g_res_hi>0&&!g_res[g_res_hi-1].e)g_res_hi--; }
static void lx_unmap_range(u64 a,u64 e){
    struct jproc* p=&g_jp[g_jcur]; { extern void xfb_untrack_range(u64,u64,u64); xfb_untrack_range(p->cr3,a,e); } lx_res_cut(p->cr3,a,e); lx_fm_cut(p->cr3,a,e); int shared=jc_tg_live(p->tg,g_jcur)>0; u64 nf=0; u64 fr[64];
    for(u64 v=a;v<e;v+=0x1000ULL){ u64* pt=vm_walk(p->cr3,v,0); if(!pt)continue;
        if(*pt&1){ u64 pa=*pt&PTE_ADDR; *pt=0; cpu_flush_page(v); { extern int g_xfb_live; extern void xfb_drop(u64); extern u32 pframe_refcount(u64); if(g_xfb_live&&pframe_refcount(pa)<=1)xfb_drop(pa); }
            if(!shared){ pframe_free(pa); continue; }
            fr[nf++]=pa; if(nf==64){ smp_tlb_sync(p->cr3); for(u64 k=0;k<nf;k++)pframe_free(fr[k]); nf=0; } }
        else if(*pt&PTE_LAZY)*pt=0; }
    if(nf){ smp_tlb_sync(p->cr3); for(u64 k=0;k<nf;k++)pframe_free(fr[k]); }
}
static u64 lx_find_gap(u64 cr3,u64 pages){
    u64 need=pages<<12, v=0x40004000000ULL, start=v, run=0;
    for(int guard=0;v+need<=0x48000000000ULL&&guard<4000000;guard++){
        { int ri=lx_res_find(cr3,v); if(ri>=0){ v=g_res[ri].e; run=0; start=v; continue; } }
        u64* e=vm_walk(cr3,v,0);
        if(!e){ u64 nv=(v+0x200000ULL)&~0x1FFFFFULL; u64 rn=lx_res_next(cr3,v); if(rn<nv){ run+=rn-v; v=rn; if(run>=need)return start; continue; } run+=nv-v; v=nv; }
        else if(vm_pte_used(*e)){ v+=0x1000ULL; run=0; start=v; }
        else { v+=0x1000ULL; run+=0x1000ULL; }
        if(run>=need)return start; }
    return 0;
}
struct lx_mapname { u64 cr3,b,e; char nm[40]; };
struct lx_mapname g_mapn[2048]; int g_mapn_i;
char* lx_addr_name(char* o,u64 cr3,u64 a){ cr3&=~0xFFFULL; int f=-1; for(int i=0;i<2048;i++){ struct lx_mapname* m=&g_mapn[i]; if((m->cr3&~0xFFFULL)==cr3&&m->cr3&&a>=m->b&&a<m->e){ f=i; break; } }
    if(f<0)return o; u64 base=g_mapn[f].b; for(int i=0;i<2048;i++){ struct lx_mapname* m=&g_mapn[i]; if((m->cr3&~0xFFFULL)==cr3&&m->cr3&&m->b<base){ int eq=1; for(int k=0;k<40;k++){ if(m->nm[k]!=g_mapn[f].nm[k]){ eq=0; break; } if(!m->nm[k])break; } if(eq)base=m->b; } }
    *o++=' '; *o++='('; for(int k=0;k<40&&g_mapn[f].nm[k];k++)*o++=g_mapn[f].nm[k]; *o++='+'; o=uhex(o,a-base,1); *o++=')'; *o=0; return o; }
static void lx_note_map(u64 cr3,u64 b,u64 e,const char* nm){ if(g_mapn_i){ struct lx_mapname* l=&g_mapn[(g_mapn_i-1)%2048]; if(l->cr3==cr3&&l->e==b){ int same=1; for(int k=0;k<39&&(nm[k]||l->nm[k]);k++)if(nm[k]!=l->nm[k]){ same=0; break; } if(same){ l->e=e; return; } } } struct lx_mapname* m=&g_mapn[g_mapn_i%2048]; g_mapn_i++; m->cr3=cr3; m->b=b; m->e=e; int k=0; for(;nm[k]&&k<39;k++)m->nm[k]=nm[k]; m->nm[k]=0; }
void lx_mapn_dead(u64 cr3){ lx_res_drop(cr3); lx_fm_dead(cr3); lx_sha_dead(cr3); cr3&=~0xFFFULL; for(int i=0;i<2048;i++) if((g_mapn[i].cr3&~0xFFFULL)==cr3)g_mapn[i].cr3=0; }
static char* lx_hx(char* o,u64 v,int mn){ char t[20]; int n=0; do{ t[n++]="0123456789abcdef"[v&15]; v>>=4; }while(v||n<mn); while(n)*o++=t[--n]; return o; }
struct lx_me { u64 b,e; const char* nm; const char* pm; };
static int lx_fdpath(const char* pa){
    const char* q;
    if(pa[0]=='/'&&pa[1]=='d'&&pa[2]=='e'&&pa[3]=='v'&&pa[4]=='/'&&pa[5]=='f'&&pa[6]=='d'&&pa[7]=='/')q=pa+8;
    else { const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -1; q=pa+6; struct jproc* L=jc_lead(&g_jp[g_jcur]);
        if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&q[4]=='/')q+=4;
        else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&q[11]=='/')q+=11;
        else { int pid=0,d=0; while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; } if(!d||(pid!=L->pid&&pid!=g_jp[g_jcur].pid))return -1; }
        if(!(q[0]=='/'&&q[1]=='f'&&q[2]=='d'&&q[3]=='/'))return -1; q+=4; }
    if(*q<'0'||*q>'9')return -1; int n=0; while(*q>='0'&&*q<='9'){ n=n*10+(*q-'0'); q++; if(n>100000)return -1; } if(*q)return -1; return n; }
static long lx_proc_fd(const char* pa,int flags){
    int n=lx_fdpath(pa); if(n<0)return -9999;
    int* t=fdt(); if(n>=FDMAX||t[n]<0)return -LX_ENOENT;
    int ov=t[n]; struct vfile* v=&g_vf[ov]; int cloexec=(flags&0x80000)!=0;
    if(v->dev==LX_MFD||v->dev==0||v->dev==100||v->dev==LX_DIRDEV||v->dev==LX_SRDIR){
        if(v->dev==0&&v->fs_idx>=0&&(flags&3)!=0){ if(!ux_perm_ok(v->fs_idx,2))return -LX_EACCES; }
        if((v->dev==100||v->dev==LX_SRDIR||v->dev==LX_DIRDEV)&&(flags&3)!=0)return -LX_EACCES;
        int vi=jc_newvf(v->dev,v->fs_idx,flags&~(0x80000|0x40|0x80|0x200)); if(vi<0)return -LX_EMFILE;
        g_vf[vi].vfs=v->vfs; for(int k=0;k<128;k++)g_vf[vi].vpath[k]=v->vpath[k];
        if(v->dev==LX_MFD)g_mfd[v->fs_idx].ref++;
        if(v->dev==LX_MFD&&(flags&0x200)&&(flags&3)!=0){ long e=mfd_truncate(v->fs_idx,0); if(e){ vf_unref(vi); return e; } }
        int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
    int fd=lx_fd_install(ov,cloexec,0); if(fd>=0)vf_ref(ov); return fd; }
static int lx_fdlink(int n,char* out){
    int* t=fdt(); if(n<0||n>=FDMAX||t[n]<0)return -LX_ENOENT; struct vfile* v=&g_vf[t[n]]; char* o=out;
    const char* pre=0; if(v->dev==LX_MFD)pre="/memfd:nvx (deleted)"; else if(v->dev==14||v->dev==LX_AFU)pre="socket:["; else if(v->dev==5)pre="pipe:["; else if(v->dev==LX_EVFD||v->dev==LX_TMFD)pre=v->dev==LX_EVFD?"anon_inode:[eventfd]":"anon_inode:[timerfd]";
    if(pre){ while(*pre)*o++=*pre++; if(o[-1]=='['){ int x=t[n]+1000; char b[8]; int k=0; while(x){ b[k++]=(char)('0'+x%10); x/=10; } while(k)*o++=b[--k]; *o++=']'; } return (int)(o-out); }
    if(v->vpath[0]){ for(int k=0;v->vpath[k]&&k<127;k++)*o++=v->vpath[k]; return (int)(o-out); }
    if(v->dev==0&&v->fs_idx>=0){ const char* z=FS[v->fs_idx].name; if(*z!='/')*o++='/'; while(*z&&o-out<120)*o++=*z++; return (int)(o-out); }
    const char* z="/dev/null"; if(n<=2)z="/dev/tty"; while(*z)*o++=*z++; return (int)(o-out); }
static long lx_text_fd(const char* txt,u64 len,int flags){
    int mi=mfd_new(); if(mi<0)return -LX_EMFILE;
    if(mfd_truncate(mi,len?len:1)){ mfd_close(mi); return -LX_ENOMEM; }
    g_mfd[mi].size=len; for(u64 k=0;k<len;k++)((u8*)g_mfd[mi].frames[k>>12])[k&4095]=(u8)txt[k];
    int vi=jc_newvf(LX_MFD,mi,0); if(vi<0){ mfd_close(mi); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int lx_ncpu_on(void){ int n=0; for(int i=0;i<NCPU;i++) if(i==0||(g_smp_on&&g_cpu[i].online))n++; return n; }
static char* lx_u64s(char* o,u64 v){ char t[24]; int k=0; if(!v)t[k++]='0'; while(v){ t[k++]=(char)('0'+v%10); v/=10; } while(k)*o++=t[--k]; return o; }
static int lx_proc_extra_gen(const char* pa,char* out,int cap){
    char* o=out; const char* q=0; int who=-1;
    if(lx_streq(pa,"/proc/stat")){
        u64 tu=0,ti=0,ts=0; for(int i=0;i<NCPU;i++) if(i==0||g_cpu[i].online){ tu+=g_cpu[i].user_ticks; ti+=g_cpu[i].idle_ticks; u64 tt=g_cpu[i].ticks; u64 k=tt>g_cpu[i].user_ticks+g_cpu[i].idle_ticks?tt-g_cpu[i].user_ticks-g_cpu[i].idle_ticks:0; ts+=k; }
        const char* z="cpu  "; while(*z)*o++=*z++; o=lx_u64s(o,tu); z=" 0 "; while(*z)*o++=*z++; o=lx_u64s(o,ts); *o++=' '; o=lx_u64s(o,ti); z=" 0 0 0 0 0 0\n"; while(*z)*o++=*z++;
        int c=0; for(int i=0;i<NCPU;i++) if(i==0||(g_smp_on&&g_cpu[i].online)){ u64 tt=g_cpu[i].ticks, u=g_cpu[i].user_ticks, id=g_cpu[i].idle_ticks; u64 k=tt>u+id?tt-u-id:0; z="cpu"; while(*z)*o++=*z++; o=lx_u64s(o,(u64)c); *o++=' '; o=lx_u64s(o,u); z=" 0 "; while(*z)*o++=*z++; o=lx_u64s(o,k); *o++=' '; o=lx_u64s(o,id); z=" 0 0 0 0 0 0\n"; while(*z)*o++=*z++; c++; }
        u64 sw=0; for(int i=0;i<NCPU;i++)sw+=g_cpu[i].switches;
        z="intr 0\nctxt "; while(*z)*o++=*z++; o=lx_u64s(o,sw); z="\nbtime 1760000000\nprocesses "; while(*z)*o++=*z++; o=lx_u64s(o,(u64)g_next_pid); z="\nprocs_running 1\nprocs_blocked 0\nsoftirq 0 0 0 0 0 0 0 0 0 0 0\n"; while(*z)*o++=*z++;
        return (int)(o-out); }
    if(lx_streq(pa,"/sys/devices/system/cpu/online")||lx_streq(pa,"/sys/devices/system/cpu/possible")||lx_streq(pa,"/sys/devices/system/cpu/present")){ int n=lx_ncpu_on(); *o++='0'; if(n>1){ *o++='-'; o=lx_u64s(o,(u64)(n-1)); } *o++='\n'; return (int)(o-out); }
    if(lx_streq(pa,"/sys/devices/system/cpu/kernel_max")){ o=lx_u64s(o,(u64)(NCPU-1)); *o++='\n'; return (int)(o-out); }
    { const char* s="/proc/"; int i=0; for(;s[i];i++) if(pa[i]!=s[i])return -1; q=pa+6; }
    struct jproc* me=&g_jp[g_jcur];
    if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&q[4]=='/'){ who=me->tg; q+=5; }
    else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&q[11]=='/'){ who=g_jcur; q+=12; }
    else if(*q>='0'&&*q<='9'){ int pid=0; while(*q>='0'&&*q<='9')pid=pid*10+(*q++-'0'); if(*q!='/')return -1; q++; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].pid==pid){ who=i; break; } if(who<0)return -1; }
    else return -1;
    if(q[0]=='t'&&q[1]=='a'&&q[2]=='s'&&q[3]=='k'&&q[4]=='/'){ q+=5; int pid=0; if(*q<'0'||*q>'9')return -1; while(*q>='0'&&*q<='9')pid=pid*10+(*q++-'0'); if(*q!='/')return -1; q++; int t=-1; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].pid==pid&&g_jp[i].tg==g_jp[who].tg){ t=i; break; } if(t<0)return -1; who=t; }
    struct jproc* P=&g_jp[who]; struct jproc* L=jc_lead(P);
    if(lx_streq(q,"stat")){
        int nt=0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_TDEAD&&g_jp[j].tg==P->tg)nt++;
        const char* nm=L->exe[0]?L->exe:L->cmd; int sl=-1; for(int k=0;nm[k];k++) if(nm[k]=='/')sl=k; nm+=sl+1;
        o=lx_u64s(o,(u64)P->pid); *o++=' '; *o++='('; for(int k=0;nm[k]&&nm[k]!=' '&&k<15;k++)*o++=nm[k]; *o++=')'; *o++=' ';
        *o++=(P->state==S_ZOMB)?'Z':(P->runcpu>=0?'R':'S'); *o++=' ';
        o=lx_u64s(o,(u64)L->ppid); *o++=' '; o=lx_u64s(o,(u64)L->pgid); *o++=' '; o=lx_u64s(o,(u64)L->sid); const char* z=" 0 -1 4194304 0 0 0 0 "; while(*z)*o++=*z++;
        o=lx_u64s(o,P->cputicks); z=" 0 0 0 20 0 "; while(*z)*o++=*z++; o=lx_u64s(o,(u64)nt); z=" 0 0 "; while(*z)*o++=*z++;
        u64 vs=L->brk>0x40002000000ULL?L->brk-0x40000000000ULL:0x2000000ULL; o=lx_u64s(o,vs); *o++=' '; o=lx_u64s(o,vs/4096/4); z=" 18446744073709551615 4194304 33554432 140737488351232 0 0 0 0 0 0 0 0 0 17 "; while(*z)*o++=*z++;
        o=lx_u64s(o,(u64)(P->lastcpu>0?P->lastcpu:0)); z=" 0 0 0 0 0 0 0 0 0 0 0 0 0\n"; while(*z)*o++=*z++;
        return (int)(o-out); }
    if(lx_streq(q,"status")){
        int nt=0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_TDEAD&&g_jp[j].tg==P->tg)nt++;
        const char* nm=L->exe[0]?L->exe:L->cmd; int sl=-1; for(int k=0;nm[k];k++) if(nm[k]=='/')sl=k; nm+=sl+1;
        const char* z="Name:\t"; while(*z)*o++=*z++; for(int k=0;nm[k]&&nm[k]!=' '&&k<15;k++)*o++=nm[k];
        z=P->state==S_ZOMB?"\nState:\tZ (zombie)\nTgid:\t":(P->runcpu>=0?"\nState:\tR (running)\nTgid:\t":"\nState:\tS (sleeping)\nTgid:\t"); while(*z)*o++=*z++; o=lx_u64s(o,(u64)L->pid);
        z="\nNgid:\t0\nPid:\t"; while(*z)*o++=*z++; o=lx_u64s(o,(u64)P->pid); z="\nPPid:\t"; while(*z)*o++=*z++; o=lx_u64s(o,(u64)L->ppid);
        z="\nTracerPid:\t0\nUid:\t"; while(*z)*o++=*z++; for(int k=0;k<4;k++){ o=lx_u64s(o,(u64)L->uid); *o++=k<3?'\t':'\n'; }
        z="Gid:\t"; while(*z)*o++=*z++; for(int k=0;k<4;k++){ o=lx_u64s(o,(u64)L->gid); *o++=k<3?'\t':'\n'; }
        u64 vk=(L->brk>0x40002000000ULL?L->brk-0x40000000000ULL:0x2000000ULL)/1024;
        z="FDSize:\t256\nGroups:\t\nVmPeak:\t"; while(*z)*o++=*z++; o=lx_u64s(o,vk); z=" kB\nVmSize:\t"; while(*z)*o++=*z++; o=lx_u64s(o,vk); z=" kB\nVmRSS:\t"; while(*z)*o++=*z++; o=lx_u64s(o,vk/4); z=" kB\nThreads:\t"; while(*z)*o++=*z++; o=lx_u64s(o,(u64)nt);
        z="\nSigQ:\t0/63432\nSigPnd:\t"; while(*z)*o++=*z++; o=lx_hx(o,(u64)P->sigpend>>1,16); z="\nShdPnd:\t0000000000000000\nSigBlk:\t"; while(*z)*o++=*z++; o=lx_hx(o,(u64)P->sigmask>>1,16);
        z="\nSigIgn:\t0000000000000000\nSigCgt:\t0000000000000000\nCapInh:\t0000000000000000\nCapPrm:\t000001ffffffffff\nCapEff:\t000001ffffffffff\nCapBnd:\t000001ffffffffff\nCapAmb:\t0000000000000000\nNoNewPrivs:\t0\nSeccomp:\t0\nSeccomp_filters:\t0\nCpus_allowed_list:\t0-"; while(*z)*o++=*z++;
        o=lx_u64s(o,(u64)(lx_ncpu_on()-1)); z="\nvoluntary_ctxt_switches:\t0\nnonvoluntary_ctxt_switches:\t0\n"; while(*z)*o++=*z++;
        return (int)(o-out); }
    if(lx_streq(q,"mountinfo")){ const char* z="1 0 0:1 / / rw,relatime - nvxfs nvxfs rw\n2 1 0:2 / /proc rw,relatime - proc proc rw\n3 1 0:3 / /sys rw,relatime - sysfs sysfs rw\n4 1 0:4 / /dev/shm rw,relatime - tmpfs tmpfs rw\n"; while(*z)*o++=*z++; return (int)(o-out); }
    if(lx_streq(q,"statm")){ u64 vs=(L->brk>0x40002000000ULL?L->brk-0x40000000000ULL:0x2000000ULL)/4096; o=lx_u64s(o,vs); *o++=' '; o=lx_u64s(o,vs/4); const char* z=" 0 1 0 "; while(*z)*o++=*z++; o=lx_u64s(o,vs/4); z=" 0\n"; while(*z)*o++=*z++; return (int)(o-out); }
    if(lx_streq(q,"cmdline")){ const char* c=L->cmd; int k=0; for(;c[k]&&k<(int)sizeof L->cmd;k++)*o++=(c[k]==' ')?0:c[k]; *o++=0; return (int)(o-out); }
    if(lx_streq(q,"comm")){ const char* nm=L->exe[0]?L->exe:L->cmd; int sl=-1; for(int k=0;nm[k];k++) if(nm[k]=='/')sl=k; nm+=sl+1; for(int k=0;nm[k]&&nm[k]!=' '&&k<15;k++)*o++=nm[k]; *o++='\n'; return (int)(o-out); }
    (void)cap; return -1; }
static long lx_proc_extra(const char* pa,int flags){
    static char buf[4096]; int n=lx_proc_extra_gen(pa,buf,4096); if(n<0)return -9999;
    if((flags&3)!=0)return -LX_EACCES;
    return lx_text_fd(buf,(u64)n,flags); }
static long lx_proc_maps(const char* pa,int flags){
    const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -9999;
    const char* q=pa+6; struct jproc* P=jc_lead(&g_jp[g_jcur]);
    if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&q[4]=='/')q+=5;
    else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&q[11]=='/')q+=12;
    else { if(*q<'0'||*q>'9')return -9999; int pid=0; while(*q>='0'&&*q<='9')pid=pid*10+(*q++-'0'); if(*q!='/')return -9999; q++; if(pid!=P->pid&&pid!=g_jp[g_jcur].pid)return -9999; }
    if(!(q[0]=='m'&&q[1]=='a'&&q[2]=='p'&&q[3]=='s'&&!q[4]))return -9999;
    if((flags&3)!=0)return -LX_EACCES;
    static struct lx_me me[400]; int n=0; u64 cr3=P->cr3&~0xFFFULL;
    me[n].b=0x40000001000ULL; me[n].e=0x40000101000ULL; me[n].nm="[stack]"; me[n].pm="rw-p"; n++;
    me[n].b=0x40000400000ULL; me[n].e=0x40002000000ULL; me[n].nm=P->exe[0]?P->exe:"/proc/self/exe"; me[n].pm="r-xp"; n++;
    if(P->brk>0x40002000000ULL){ me[n].b=0x40002000000ULL; me[n].e=(P->brk+0xFFFULL)&~0xFFFULL; me[n].nm="[heap]"; me[n].pm="rw-p"; n++; }
    me[n].b=0x40030000000ULL; me[n].e=0x40030040000ULL; me[n].nm="/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"; me[n].pm="r-xp"; n++;
    int cnt=g_mapn_i<2048?g_mapn_i:2048;
    for(int j=0;j<cnt&&n<400;j++){ struct lx_mapname* m=&g_mapn[(g_mapn_i-cnt+j)%2048]; if((m->cr3&~0xFFFULL)!=cr3||!m->e)continue; me[n].b=m->b; me[n].e=m->e; me[n].nm=m->nm; me[n].pm="r-xp"; n++; }
    for(int a=1;a<n;a++){ struct lx_me t=me[a]; int b=a-1; while(b>=0&&me[b].b>t.b){ me[b+1]=me[b]; b--; } me[b+1]=t; }
    char* out=(char*)kmalloc(65536); if(!out)return -LX_ENOMEM; char* o=out; u64 last=0;
    for(int a=0;a<n&&o-out<65000;a++){ if(me[a].b<last)continue; last=me[a].e;
        o=lx_hx(o,me[a].b,8); *o++='-'; o=lx_hx(o,me[a].e,8); *o++=' '; for(int k=0;k<4;k++)*o++=me[a].pm[k];
        const char* mid=" 00000000 00:00 0"; while(*mid)*o++=*mid++; for(int k=0;k<7;k++)*o++=' ';
        const char* z=me[a].nm; while(*z&&o-out<65400)*o++=*z++; *o++='\n'; }
    u64 len=(u64)(o-out);
    int mi=mfd_new(); if(mi<0){ kfree(out); return -LX_EMFILE; }
    if(mfd_truncate(mi,len?len:1)){ mfd_close(mi); kfree(out); return -LX_ENOMEM; }
    g_mfd[mi].size=len;
    for(u64 k=0;k<len;k++)((u8*)g_mfd[mi].frames[k>>12])[k&4095]=(u8)out[k];
    kfree(out);
    int vi=jc_newvf(LX_MFD,mi,0); if(vi<0){ mfd_close(mi); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static long lx_madvise(u64 a,u64 len,int adv){
    if(a&0xFFF)return -LX_EINVAL; if(!len)return 0; u64 e=(a+len+0xFFFULL)&~0xFFFULL; if(!jc_uok(a,e-a))return -LX_ENOMEM;
    if(adv!=4&&adv!=9)return 0;
    extern u32 pframe_refcount(u64 pa); struct jproc* p=&g_jp[g_jcur];
    for(u64 v=a;v<e;v+=0x1000ULL){ u64* pt=vm_walk(p->cr3,v,0); if(!pt||!(*pt&1))continue; u64 pa=*pt&PTE_ADDR; if((*pt&PTE_SHR)||pframe_refcount(pa)>1)continue;
        lx_unmap_range(v,v+0x1000ULL); vm_map_lazy(p->cr3,v); }
    return 0; }
static long lx_mprotect(u64 a,u64 len,int prot){
    if(a&0xFFF)return -LX_EINVAL; if(prot&~0x7)return -LX_EINVAL; if(!len)return 0;
    u64 e=(a+len+0xFFFULL)&~0xFFFULL; if(e<a||!jc_uok(a,e-a))return -LX_ENOMEM;
    struct jproc* p=&g_jp[g_jcur]; u64 cr3=p->cr3&~0xFFFULL; u64 bits=vm_prot_bits(prot); int any=0;
    for(u64 v=a;v<e;){
        int ri=lx_res_find(cr3,v); u64* pt=vm_walk(cr3,v,0);
        if(ri>=0&&!(pt&&vm_pte_used(*pt))){ u64 re=g_res[ri].e<e?g_res[ri].e:e; u64 n=v+0x1000ULL; while(n<re){ u64* q=vm_walk(cr3,n,0); if(q&&vm_pte_used(*q))break; n+=0x1000ULL; }
            if(g_res[ri].b>=v&&g_res[ri].e<=n)g_res[ri].pb=bits; else { u64 ob=g_res[ri].pb; lx_res_cut(cr3,v,n); if(lx_res_addp(cr3,v,n,bits)){ lx_res_addp(cr3,v,n,ob); } }
            any=1; v=n; continue; }
        if(!pt||!vm_pte_used(*pt)){ v+=0x1000ULL; if(!pt)v=(v+0x1FFFFFULL)&~0x1FFFFFULL; continue; }
        any=1; u64 x=*pt;
        if(!(x&1)){ *pt=PTE_LAZY|(x&PTE_SHR)|bits; }
        else { u64 n2=(x&~(2ULL|4ULL|PTE_PBITS))|4ULL|bits; if(!(bits&PTE_PRO)&&!(x&PTE_COW))n2|=2ULL; *pt=vm_pflags(n2&~(PTE_PBITS),bits); }
        cpu_flush_page(v); v+=0x1000ULL; }
    if(!any)return -LX_ENOMEM;
    smp_tlb_sync(cr3);
    return 0; }
