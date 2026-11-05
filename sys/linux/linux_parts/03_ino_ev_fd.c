static void ino_ev_fd(int fd,u32 mask){
    if(!g_ino_any)return; struct vfile* v=lx_vf(fd); if(!v||v->dev!=0||v->fs_idx<0||v->fs_idx>=FS_N)return;
    char p[LX_PATH]; dt_path(FS[v->fs_idx].dir,p); int n=lx_slen(p); if(n>1&&n<LX_PATH-2)p[n++]='/'; for(int i=0;FS[v->fs_idx].name[i]&&n<LX_PATH-1;i++)p[n++]=FS[v->fs_idx].name[i]; p[n]=0;
    ino_ev(p,mask,0,0);
}
static long lx_inotify_init(int flags){
    if(flags&~(0x800|0x80000))return -LX_EINVAL;
    int i=-1; for(int k=0;k<INO_MAX;k++) if(!g_ino[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    struct inofd* f=&g_ino[i]; f->used=1; f->nw=0; f->nextwd=1; f->qn=0; f->ovf=0; f->lwd=0; f->lmask=0; f->lname[0]=0;
    int vi=jc_newvf(LX_INOFD,i,O_RDONLY|(flags&0x800)); if(vi<0){ f->used=0; return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0){ vf_unref(vi); return fd; }
    g_ino_any=1; return fd;
}
static struct inofd* ino_of(int fd){ struct vfile* v=lx_vf(fd); if(!v||v->dev!=LX_INOFD||v->fs_idx<0||v->fs_idx>=INO_MAX)return 0; return &g_ino[v->fs_idx]; }
static long lx_inotify_add(int fd,const char* p,u32 mask){
    struct inofd* f=ino_of(fd); if(!f)return lx_vf(fd)?-LX_EINVAL:-LX_EBADF;
    if(!(mask&0xFFFu))return -LX_EINVAL;
    if(!lx_exists(p)){ int isd; if(ux_lookup(p,&isd,0)<0)return -LX_ENOENT; }
    if((mask&0x01000000u)&&!lx_isdir(p))return -LX_ENOTDIR;
    int L=lx_slen(p); while(L>1&&p[L-1]=='/')L--; if(L>=INO_PL)return -LX_ENAMETOOLONG;
    char q[INO_PL]; for(int i=0;i<L;i++)q[i]=p[i]; q[L]=0;
    u32 m=mask&(0xFFFu|0x80000000u|0x20000000u);
    for(int w=0;w<f->nw;w++) if(ino_eq(f->path[w],q)){ if(mask&0x10000000u)return -LX_EEXIST; f->mask[w]=(m&0x20000000u)?(f->mask[w]|m):m; f->mask[w]&=~0x20000000u; return f->wd[w]; }
    if(f->nw>=INO_W)return -28;
    int w=f->nw++; f->wd[w]=f->nextwd++; f->mask[w]=m&~0x20000000u; for(int i=0;i<=L;i++)f->path[w][i]=q[i];
    return f->wd[w];
}
static long lx_inotify_rm(int fd,int wd){
    struct inofd* f=ino_of(fd); if(!f)return lx_vf(fd)?-LX_EINVAL:-LX_EBADF;
    for(int w=0;w<f->nw;w++) if(f->wd[w]==wd){ ino_push(f,wd,0x8000u,0,0); f->nw--; f->wd[w]=f->wd[f->nw]; f->mask[w]=f->mask[f->nw]; for(int k=0;k<INO_PL;k++)f->path[w][k]=f->path[f->nw][k]; return 0; }
    return -LX_EINVAL;
}
static int ino_read(int i,u64 buf,u64 len){
    struct inofd* f=&g_ino[i]; if(!f->qn)return JS_BLOCK;
    u32 first=16u+*(u32*)(f->q+12); if(len<first)return -LX_EINVAL;
    int n=0; while(n<f->qn){ u32 e=16u+*(u32*)(f->q+n+12); if((u64)n+e>len)break; n+=(int)e; }
    if(lx_put(buf,f->q,(u64)n))return -LX_EFAULT;
    for(int k=n;k<f->qn;k++)f->q[k-n]=f->q[k]; f->qn-=n; if(!f->qn)f->ovf=0;
    return n;
}
static void ino_close(int i){ if(i>=0&&i<INO_MAX)g_ino[i].used=0; int a=0; for(int k=0;k<INO_MAX;k++) if(g_ino[k].used)a=1; g_ino_any=a; }

#define SFD_MAX 32
struct sigfd { int used; u32 mask; };
static struct sigfd g_sfd[SFD_MAX];
static long lx_signalfd(int fd,u64 umask,u64 sz,int flags){
    if(sz!=8)return -LX_EINVAL; if(flags&~(0x800|0x80000))return -LX_EINVAL;
    u64 m; if(lx_get(&m,umask,8))return -LX_EFAULT; u32 km=lx_l2k(m)&~((1u<<9)|(1u<<19));
    if(fd!=-1){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_SIGFD)return -LX_EINVAL; g_sfd[v->fs_idx].mask=km; return fd; }
    int i=-1; for(int k=0;k<SFD_MAX;k++) if(!g_sfd[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    g_sfd[i].used=1; g_sfd[i].mask=km;
    int vi=jc_newvf(LX_SIGFD,i,O_RDONLY|(flags&0x800)); if(vi<0){ g_sfd[i].used=0; return -LX_EMFILE; }
    int nfd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(nfd<0)vf_unref(vi); return nfd;
}
static u32 sfd_pend(int i){ struct jproc* p=&g_jp[g_jcur]; struct jproc* L=jc_lead(p); return (p->sigpend|L->sigpend)&g_sfd[i].mask; }
static int sfd_read(int i,u64 buf,u64 len){
    if(len<128)return -LX_EINVAL; struct jproc* p=&g_jp[g_jcur]; struct jproc* L=jc_lead(p);
    int n=0;
    while((u64)n+128<=len){ u32 pd=(p->sigpend|L->sigpend)&g_sfd[i].mask; if(!pd)break; int s=1; while(s<32&&!(pd&(1u<<s)))s++; if(s>=32)break;
        struct jproc* src=(p->sigpend&(1u<<s))?p:L; src->sigpend&=~(1u<<s);
        u8 si[128]; lx_zero(si,128); *(u32*)si=(u32)s; *(int*)(si+8)=src->sicode[s]; src->sicode[s]=0; *(u32*)(si+16)=(u32)g_uid;
        if(lx_put(buf+(u64)n,si,128))return n?n:-LX_EFAULT; n+=128; }
    return n?n:JS_BLOCK;
}
static void sfd_close(int i){ if(i>=0&&i<SFD_MAX)g_sfd[i].used=0; }
static void ep_close(int i);
void lxabi_close(int dev,int i){ if(dev==LX_INOFD)ino_close(i); else if(dev==LX_SIGFD)sfd_close(i); else if(dev==113)ep_close(i); }

#define LK_MAX 256
struct lxlock { int used, kind, type; u64 key; int owner; u64 start, end; };
static struct lxlock g_lxlk[LK_MAX];
static u64 lk_key(struct vfile* v){ if(v->dev==0&&v->fs_idx>=0)return 0x100000000ULL|(u32)v->fs_idx; u32 h=sysroot_ino(v->vpath,1); if(!h){ h=2166136261u; for(int q=0;v->vpath[q];q++)h=(h^(u8)v->vpath[q])*16777619u; } return ((u64)(u32)v->dev<<40)|h; }
static int lk_alive(struct lxlock* l){ if(l->kind!=0)return 1; int t=jc_idx(l->owner); return t>=0&&g_jp[t].state!=S_ZOMB&&g_jp[t].state!=S_FREE; }
static struct lxlock* lk_conflict(u64 key,int kind,int owner,int type,u64 s,u64 e){
    for(int i=0;i<LK_MAX;i++){ struct lxlock* l=&g_lxlk[i]; if(!l->used)continue; if(!lk_alive(l)){ l->used=0; continue; }
        if(l->key!=key)continue; if((kind==1)!=(l->kind==1))continue; if(l->kind==kind&&l->owner==owner)continue;
        if(l->type==0&&type==0)continue; if(l->end<s||l->start>e)continue; return l; }
    return 0;
}
static void lk_remove(u64 key,int kind,int owner,u64 s,u64 e){
    for(int i=0;i<LK_MAX;i++){ struct lxlock* l=&g_lxlk[i]; if(!l->used||l->key!=key||l->kind!=kind||l->owner!=owner)continue; if(l->end<s||l->start>e)continue;
        if(l->start<s&&l->end>e){ for(int j=0;j<LK_MAX;j++) if(!g_lxlk[j].used){ g_lxlk[j]=*l; g_lxlk[j].start=e+1; break; } l->end=s-1; }
        else if(l->start<s)l->end=s-1; else if(l->end>e)l->start=e+1; else l->used=0; }
}
static int lk_add(u64 key,int kind,int owner,int type,u64 s,u64 e){ lk_remove(key,kind,owner,s,e); for(int i=0;i<LK_MAX;i++) if(!g_lxlk[i].used){ struct lxlock* l=&g_lxlk[i]; l->used=1; l->kind=kind; l->type=type; l->key=key; l->owner=owner; l->start=s; l->end=e; return 0; } return -37; }
void lx_lock_vfclose(int vi){ for(int i=0;i<LK_MAX;i++) if(g_lxlk[i].used&&g_lxlk[i].kind!=0&&g_lxlk[i].owner==vi)g_lxlk[i].used=0; }
static void lk_proc_close(struct vfile* v){ int pid=jc_lead(&g_jp[g_jcur])->pid; u64 k=lk_key(v); for(int i=0;i<LK_MAX;i++) if(g_lxlk[i].used&&g_lxlk[i].kind==0&&g_lxlk[i].owner==pid&&g_lxlk[i].key==k)g_lxlk[i].used=0; }
static long lx_flock(struct sysregs* r,int fd,int op){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; int vi=fdt()[fd]; u64 k=lk_key(v);
    int nb=op&4; op&=~4;
    if(op==8){ lk_remove(k,1,vi,0,~0ULL); return 0; }
    if(op!=1&&op!=2)return -LX_EINVAL;
    int type=op==2?1:0;
    if(lk_conflict(k,1,vi,type,0,~0ULL)){ if(nb)return -LX_EAGAIN; if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
    return lk_add(k,1,vi,type,0,~0ULL);
}
static long lx_fcntl_lock(struct sysregs* r,int fd,int cmd,u64 arg){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(!arg)return -LX_EFAULT;
    u8 b[32]; if(lx_get(b,arg,32))return -LX_EFAULT;
    short ty=*(short*)b, wh=*(short*)(b+2); i64 st=*(i64*)(b+8), ln=*(i64*)(b+16);
    int ofd=cmd>=36; int kind=ofd?2:0; int owner=ofd?fdt()[fd]:jc_lead(&g_jp[g_jcur])->pid;
    if(ofd&&*(int*)(b+24)!=0)return -LX_EINVAL;
    i64 base=wh==0?0:wh==1?(i64)v->off:wh==2?((v->dev==0&&v->fs_idx>=0)?(i64)FS[v->fs_idx].len:0):-1; if(base<0)return -LX_EINVAL;
    i64 s=base+st; u64 e;
    if(ln>0)e=(u64)(s+ln-1); else if(ln==0)e=~0ULL; else { e=(u64)(s-1); s+=ln; }
    if(s<0)return -LX_EINVAL;
    u64 k=lk_key(v);
    if(cmd==5||cmd==36){ if(ty!=0&&ty!=1)return -LX_EINVAL; struct lxlock* c=lk_conflict(k,kind,owner,ty,(u64)s,e);
        if(!c){ *(short*)b=2; return lx_put(arg,b,2); }
        *(short*)b=(short)c->type; *(short*)(b+2)=0; *(i64*)(b+8)=(i64)c->start; *(i64*)(b+16)=c->end==~0ULL?0:(i64)(c->end-c->start+1); *(int*)(b+24)=c->kind==0?c->owner:-1; return lx_put(arg,b,32); }
    if(ty==2){ lk_remove(k,kind,owner,(u64)s,e); return 0; }
    if(ty!=0&&ty!=1)return -LX_EINVAL;
    if(ty==0&&(v->flags&3)==1)return -LX_EBADF; if(ty==1&&(v->flags&3)==0&&v->dev==0)return -LX_EBADF;
    if(lk_conflict(k,kind,owner,ty,(u64)s,e)){ if(cmd==6||cmd==37)return -LX_EAGAIN; if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
    return lk_add(k,kind,owner,ty,(u64)s,e);
}

#define LXPT_MAX 64
struct lxptimer { int used, owner, clk, notify, sig, tid, over; u64 next, ival; };
static struct lxptimer g_lxpt[LXPT_MAX];
void lx_ptimer_tick(int i){
    struct jproc* p=&g_jp[i]; if(p->tg!=i)return;
    for(int k=0;k<LXPT_MAX;k++){ struct lxptimer* t=&g_lxpt[k]; if(!t->used||t->owner!=p->pid||!t->next)continue;
        u64 now=(t->clk==0||t->clk==8)?lx_now_ns():hr_ns(); if(now<t->next)continue;
        if(t->ival){ u64 n=(now-t->next)/t->ival; t->over=n>0x7FFFFFFFULL?0x7FFFFFFF:(int)n; t->next+=(n+1)*t->ival; } else { t->next=0; t->over=0; }
        if(t->notify==1)continue;
        if(t->notify==4){ int th=jc_idx(t->tid); if(th>=0&&g_jp[th].tg==i){ g_jp[th].sicode[t->sig]=-2; jc_signal_thread(th,t->sig); } }
        else { p->sicode[t->sig]=-2; jc_signal_proc(i,t->sig); } }
}
static long lx_timer_create(int clk,u64 sev,u64 out){
    if(clk!=0&&clk!=1&&clk!=4&&clk!=7&&clk!=8&&clk!=9&&clk!=2&&clk!=3)return -LX_EINVAL;
    int notify=0, sig=14, tid=0;
    if(sev){ u8 b[24]; if(lx_get(b,sev,24))return -LX_EFAULT; sig=*(int*)(b+8); notify=*(int*)(b+12); tid=*(int*)(b+16);
        if(notify!=0&&notify!=1&&notify!=4)return -LX_EINVAL;
        if(notify!=1&&(sig<1||sig>=32))return -LX_EINVAL; }
    int pid=jc_lead(&g_jp[g_jcur])->pid;
    for(int k=0;k<LXPT_MAX;k++) if(g_lxpt[k].used){ int t=jc_idx(g_lxpt[k].owner); if(t<0||g_jp[t].state==S_ZOMB)g_lxpt[k].used=0; }
    int i=-1; for(int k=0;k<LXPT_MAX;k++) if(!g_lxpt[k].used){ i=k; break; } if(i<0)return -LX_EAGAIN;
    int id=i; if(lx_put(out,&id,4))return -LX_EFAULT;
    struct lxptimer* t=&g_lxpt[i]; t->used=1; t->owner=pid; t->clk=clk; t->notify=notify; t->sig=sig; t->tid=tid; t->over=0; t->next=0; t->ival=0;
    return 0;
}
static struct lxptimer* lxpt_of(int id){ if(id<0||id>=LXPT_MAX||!g_lxpt[id].used||g_lxpt[id].owner!=jc_lead(&g_jp[g_jcur])->pid)return 0; return &g_lxpt[id]; }
static long lx_timer_get(int id,u64 out){ struct lxptimer* t=lxpt_of(id); if(!t)return -LX_EINVAL; u64 o[4]={t->ival/1000000000ULL,t->ival%1000000000ULL,0,0};
    if(t->next){ u64 now=(t->clk==0||t->clk==8)?lx_now_ns():hr_ns(); u64 r=t->next>now?t->next-now:1; o[2]=r/1000000000ULL; o[3]=r%1000000000ULL; } return lx_put(out,o,32); }
static long lx_timer_set(int id,int fl,u64 nv,u64 ov){
    struct lxptimer* t=lxpt_of(id); if(!t)return -LX_EINVAL;
    if(ov){ long e=lx_timer_get(id,ov); if(e)return e; }
    u64 s[4]; if(lx_get(s,nv,32))return -LX_EFAULT; if(s[1]>=1000000000ULL||s[3]>=1000000000ULL)return -LX_EINVAL;
    u64 val=s[2]*1000000000ULL+s[3]; t->ival=s[0]*1000000000ULL+s[1]; t->over=0;
    if(!val){ t->next=0; return 0; }
    u64 now=(t->clk==0||t->clk==8)?lx_now_ns():hr_ns(); t->next=(fl&1)?(val>now?val:now):now+val; if(!t->next)t->next=1; return 0;
}
u32 g_enosys_nr[48], g_enosys_ct[48]; int g_enosys_n;
u32 g_gap_seq, g_enosys_last[48]; u32 g_gap_kind[40], g_gap_ct[40], g_gap_last[40]; u64 g_gap_val[40]; int g_gap_n;
static void enosys_note(u64 nr){ g_gap_seq++; for(int i=0;i<g_enosys_n;i++) if(g_enosys_nr[i]==(u32)nr){ g_enosys_ct[i]++; g_enosys_last[i]=g_gap_seq; return; } if(g_enosys_n<48){ g_enosys_nr[g_enosys_n]=(u32)nr; g_enosys_ct[g_enosys_n]=1; g_enosys_last[g_enosys_n]=g_gap_seq; g_enosys_n++; } }
void lx_gap(u32 kind,u64 val){ g_gap_seq++; for(int i=0;i<g_gap_n;i++) if(g_gap_kind[i]==kind&&g_gap_val[i]==val){ g_gap_ct[i]++; g_gap_last[i]=g_gap_seq; return; } if(g_gap_n<40){ g_gap_kind[g_gap_n]=kind; g_gap_val[g_gap_n]=val; g_gap_ct[g_gap_n]=1; g_gap_last[g_gap_n]=g_gap_seq; g_gap_n++; } }
static long lx_memfd(int flags){ if(flags&~0x1F)return -LX_EINVAL; int i=mfd_new(); if(i<0)return i; if(flags&8)g_mfd[i].seals=0x20; else if(!(flags&2))g_mfd[i].seals=1; int vi=jc_newvf(LX_MFD,i,O_RDWR); if(vi<0){ mfd_close(i); return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&1)!=0,0); if(fd<0){ vf_unref(vi); return fd; } return fd; }
#define SHMN_MAX 256
struct lx_shmn { int mi; char nm[64]; };
static struct lx_shmn g_shmn[SHMN_MAX];
static int lx_shm_name(const char* p){ const char* s="/dev/shm"; int i=0; for(;s[i];i++) if(p[i]!=s[i])return -1; if(!p[i])return 0; if(p[i]!='/')return -1; i++; if(!p[i])return 0; int st=i; for(;p[i];i++) if(p[i]=='/')return -1; if(i-st>63)return -1; return st; }
static int lx_shm_find(const char* nm){ for(int i=0;i<SHMN_MAX;i++) if(g_shmn[i].nm[0]){ int k=0; while(nm[k]&&nm[k]==g_shmn[i].nm[k])k++; if(!nm[k]&&!g_shmn[i].nm[k])return i; } return -1; }
static long lx_shm_open(const char* p,int st,int flags){ const char* nm=p+st; int e=lx_shm_find(nm);
    if(e>=0&&(flags&0x40)&&(flags&0x80))return -LX_EEXIST;
    if(e<0){ if(!(flags&0x40))return -LX_ENOENT; for(int i=0;i<SHMN_MAX;i++) if(!g_shmn[i].nm[0]){ e=i; break; } if(e<0)return -LX_ENOSPC;
        int mi=mfd_new(); if(mi<0)return mi; g_mfd[mi].seals=1; g_shmn[e].mi=mi; int k=0; for(;nm[k];k++)g_shmn[e].nm[k]=nm[k]; g_shmn[e].nm[k]=0; }
    else if((flags&0x200)&&(flags&3)){ long t=mfd_truncate(g_shmn[e].mi,0); if(t)return t; }
    int mi=g_shmn[e].mi; g_mfd[mi].ref++;
    int vi=jc_newvf(LX_MFD,mi,(flags&3)?O_RDWR:0); if(vi<0){ mfd_close(mi); return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int lx_shm_stato(const char* p,int st,u32* o){ for(int i=0;i<10;i++)o[i]=0; if(st==0){ o[0]=0041777u; o[3]=2; o[9]=1; o[5]=0x33000000u; } else { int e=lx_shm_find(p+st); if(e<0)return -1; o[0]=0100600u; o[3]=1; o[4]=(u32)g_mfd[g_shmn[e].mi].size; o[5]=30000u+(u32)g_shmn[e].mi; } o[6]=o[7]=o[8]=1760000000u; return 0; }
static long lx_shm_unlink(const char* p,int st){ int e=lx_shm_find(p+st); if(e<0)return -LX_ENOENT; int mi=g_shmn[e].mi; g_shmn[e].nm[0]=0; mfd_close(mi); return 0; }
static long lx_read(struct sysregs* r,int fd,u64 buf,u64 len){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if((v->flags&3)==1)return -LX_EBADF;
    if(v->dev==LX_DIRDEV)return -LX_EISDIR;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!len)return 0;
    if(!lx_okw(buf,len))return -LX_EFAULT;
    if(v->dev==14)return lx_sock_wait(r,lxs_recv(v->fs_idx,(u8*)buf,(int)len,0,0,0),lx_nonblock(v));
    if(v->dev==LX_AFU)return lx_sock_wait(r,un_recv(v->fs_idx,(u8*)buf,(int)len,0,0,0,0),lx_nonblock(v));
    if(v->dev==LX_EVFD)return lx_sock_wait(r,ev_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_TMFD)return lx_sock_wait(r,tm_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_INOFD)return lx_sock_wait(r,ino_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_SIGFD)return lx_sock_wait(r,sfd_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_MFD)return mfd_io(v,buf,len,-1,0);
    if(v->dev==LX_PTM||v->dev==LX_PTS)return lx_sock_wait(r,pty_read(v,(u8*)buf,(int)len),lx_nonblock(v));
    int n=vfs_read(fd,(char*)buf,(int)len);
    if(n==JS_BLOCK){ if(lx_nonblock(v))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0)return v->dev==5?-LX_EIO:-LX_EBADF;
    return n;
}
static void lx_persist(int fd){
    struct vfile* v=lx_vf(fd);
    if(v&&v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N)g_fs_dirty[v->fs_idx]=1;
}
static long lx_write(struct sysregs* r,int fd,u64 buf,u64 len){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if((v->flags&3)==0&&v->dev==0)return -LX_EBADF;
    if(v->dev==LX_DIRDEV||v->dev==6||v->dev==100)return -LX_EBADF;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!len)return 0;
    if(!lx_ok(buf,len))return -LX_EFAULT;
    if(v->dev==14){ int n=lxs_send(v->fs_idx,(const u8*)buf,(int)len,0,0,0); if(n==-32)jc_signal_proc(g_jcur,13); return lx_sock_wait(r,n,lx_nonblock(v)); }
    if(v->dev==LX_AFU){ int n=un_send(v->fs_idx,(const u8*)buf,(int)len,0,0); if(n==-32)jc_signal_proc(g_jcur,13); return lx_sock_wait(r,n,lx_nonblock(v)); }
    if(v->dev==LX_EVFD)return lx_sock_wait(r,ev_write(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_PTM||v->dev==LX_PTS)return lx_sock_wait(r,pty_write(v,(const u8*)buf,(int)len),lx_nonblock(v));
    if(v->dev==LX_TMFD||v->dev==LX_INOFD||v->dev==LX_SIGFD)return -LX_EINVAL;
    if(v->dev==LX_MFD){ if((v->flags&3)==0)return -LX_EBADF; return mfd_io(v,buf,len,-1,1); }
    if(v->dev==0&&(v->flags&0x400)&&v->fs_idx>=0)v->off=FS[v->fs_idx].len;
    if(v->dev==0&&v->fs_idx>=0&&(v->off<0||v->off>=FS_CAP-1))return -27;
    int wo=v->off;
    int n=vfs_write(fd,(const char*)buf,(int)len);
    if(n>0&&v->dev==0&&v->fs_idx>=0)lx_fm_sync(v->fs_idx,(u64)wo,(u64)n);
    if(n==JS_BLOCK){ if(lx_nonblock(v))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0){ if(v->dev==7)return -LX_EPIPE; if(v->dev==4)return -LX_ENOSPC; if(v->dev==0)return -LX_EACCES; return -LX_EIO; }
    if(n>0){ lx_persist(fd); ino_ev_fd(fd,0x2); }
    return n;
}
static long lx_rwv(struct sysregs* r,int fd,u64 iov,int cnt,int wr){
    if(cnt<0||cnt>1024)return -LX_EINVAL; if(!lx_ok(iov,(u64)cnt*16))return -LX_EFAULT;
    long tot=0;
    for(int i=0;i<cnt;i++){ u64 b=((u64*)iov)[i*2], l=((u64*)iov)[i*2+1]; if(!l)continue;
        struct vfile* v=lx_vf(fd); if(!v)return tot?tot:-LX_EBADF;
        if(tot>0){ if(!wr&&v->dev==6&&g_pipe[v->fs_idx].n==0)break; if(wr&&v->dev==7&&g_pipe[v->fs_idx].n>=g_pipe[v->fs_idx].cap)break; if(!wr&&v->dev==5&&g_tin_n==0)break; if(v->dev==14&&(wr?g_lxs[v->fs_idx].txn>=LXS_TXCAP:g_lxs[v->fs_idx].rxn==0))break; if(v->dev==LX_AFU){ struct unsock* su=un_get(v->fs_idx); if(su&&(wr?(su->peer>=0&&un_get(su->peer)&&g_un[su->peer].rxn>=UN_CAP):su->rxn==0))break; } }
        long n=wr?lx_write(r,fd,b,l):lx_read(r,fd,b,l);
        if(n<0)return tot?tot:n; tot+=n; if((u64)n<l)break; }
    return tot;
}
static long lx_prw(int fd,u64 buf,u64 len,long off,int wr){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(v->dev==LX_MFD){ if(off<0)return -LX_EINVAL; if(wr?((v->flags&3)==0):((v->flags&3)==1))return -LX_EBADF; if(len>LX_IOMAX)len=LX_IOMAX; if(!(wr?lx_ok(buf,len):lx_okw(buf,len)))return -LX_EFAULT; return mfd_io(v,buf,len,off,wr); }
    if(v->dev==100&&!wr&&v->vfs){ if(off<0)return -LX_EINVAL; if(len>LX_IOMAX)len=LX_IOMAX; if(!lx_okw(buf,len))return -LX_EFAULT; int save=v->off; v->off=(int)off; int n=vfs_read(fd,(char*)buf,(int)len); v->off=save; return n<0?-LX_EIO:n; }
    if(v->dev!=0||v->fs_idx<0)return -LX_ESPIPE; if(off<0)return -LX_EINVAL;
    if(wr?((v->flags&3)==0):((v->flags&3)==1))return -LX_EBADF;
    if(wr&&off>=FS_CAP-1)return -27;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!(wr?lx_ok(buf,len):lx_okw(buf,len)))return -LX_EFAULT;
    if(off>FS_CAP)off=FS_CAP;
    int save=v->off, fl=v->flags; v->off=(int)off; v->flags&=~0x400;
    int n=wr?vfs_write(fd,(const char*)buf,(int)len):vfs_read(fd,(char*)buf,(int)len);
    v->off=save; v->flags=fl; if(n<0)return wr?-LX_EACCES:-LX_EIO; if(wr&&n>0){ lx_fm_sync(v->fs_idx,(u64)off,(u64)n); lx_persist(fd); } return n;
}
static long lx_lseek(int fd,long off,int wh){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(wh<0||wh>2)return -LX_EINVAL;
    if(v->dev==LX_DIRDEV||v->dev==LX_SRDIR){ if(wh==0&&off>=0){ v->off=(int)off; return off; } return -LX_EINVAL; }
    if(v->dev==LX_MFD){ long b=wh==1?v->off:wh==2?(long)g_mfd[v->fs_idx].size:0; if(b+off<0||b+off>0x7FFFFFFFL)return -LX_EINVAL; v->off=(int)(b+off); return v->off; }
    if(v->dev==100){ long b=wh==1?v->off:wh==2?(long)v->vfs->size(v->vpath):0; if(b+off<0||b+off>0x7FFFFFFFL)return -LX_EINVAL; v->off=(int)(b+off); return v->off; }
    if(v->dev!=0&&v->dev!=9)return (v->dev==5||v->dev==6||v->dev==7||v->dev==14||v->dev==LX_AFU)?-LX_ESPIPE:0;
    long b=(wh==1)?v->off:(wh==2)?(v->dev==9?(long)W*H*4:FS[v->fs_idx].len):0;
    if(b+off<0)return -LX_EINVAL; if(b+off>0x7FFFFFFFL)return -LX_EINVAL;
    v->off=(int)(b+off); return v->off;
}
static long lx_getdents(int fd,u64 buf,u64 cap){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_DIRDEV&&v->dev!=LX_SRDIR)return -LX_ENOTDIR;
    if(cap>65536)cap=65536; if(!lx_okw(buf,cap))return -LX_EFAULT;
    char dp[LX_PATH]; if(v->dev==LX_SRDIR){ int q=0; for(;v->vpath[q]&&q<LX_PATH-1;q++)dp[q]=v->vpath[q]; dp[q]=0; } else dt_path(v->fs_idx,dp);
    int fdix; int fdd=v->dev==LX_SRDIR&&lx_procdir(dp,&fdix);
    u64 used=0;
    for(;;){ int n=v->off; char name[72]; int type=0;
        if(n==0){ name[0]='.'; name[1]=0; type=4; }
        else if(n==1){ name[0]='.'; name[1]='.'; name[2]=0; type=4; }
        else if(fdd){ if(!lx_procent(dp,n-2,name,&type))break; }
        else if(lx_streq(dp,"/dev/snd")){ if(!asnd_list(n-2,name,&type))break; }
        else if(lx_streq(dp,"/dev/pts")){ int c=0, f=0; for(int i=0;i<PTY_MAX;i++) if(g_pty[i].used&&!g_pty[i].locked){ if(c==n-2){ char nb[16]; pty_name(i,nb); int q=0; for(;nb[9+q];q++)name[q]=nb[9+q]; name[q]=0; f=1; break; } c++; } if(!f)break; type=2; }
        else { int got=0; int ndt=0;
            if(v->dev==LX_DIRDEV){ got=(k_readdir_at(dp,n-2,name,&type)==1); if(!got){ int lo=0, hi=n-2; while(lo<hi){ int mid=(lo+hi)/2; char t2[72]; int ty2; if(k_readdir_at(dp,mid,t2,&ty2)==1)lo=mid+1; else hi=mid; } ndt=lo; } }
            if(!got){ int si=n-2-ndt; for(;;){ int sd=0; if(!sysroot_list(dp,si,name,64,&sd))break; char ep[LX_PATH]; int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)ep[o++]=dp[i]; if(o>1)ep[o++]='/'; for(int i=0;name[i]&&o<LX_PATH-1;i++)ep[o++]=name[i]; ep[o]=0;
                    int isd2=0; if(v->dev==LX_DIRDEV&&(ux_lookup(ep,&isd2,0)>=0)){ si++; v->off++; continue; } type=sysroot_isdir(ep)?4:8; got=1; break; } }
            if(!got)break; }
        int L=lx_slen(name); u64 rl=(19+(u64)L+1+7)&~7ULL;
        if(used+rl>cap){ if(!used)return -LX_EINVAL; break; }
        u8* d=(u8*)(buf+used); lx_zero(d,rl);
        u64 ino=(u64)(n+1)*7919ULL;
        if(n==0)ino=10000u+(u32)v->fs_idx; else if(n==1)ino=10000u+(u32)(v->fs_idx>0&&v->fs_idx<DN_MAX&&DT[v->fs_idx].parent>=0?DT[v->fs_idx].parent:0);
        else if(fdd)ino=0x31000000ULL+(u64)n*977ULL+(u64)(name[0]);
        else { char ep[LX_PATH]; int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)ep[o++]=dp[i]; if(o>1&&o<LX_PATH-2)ep[o++]='/'; for(int i=0;name[i]&&o<LX_PATH-1;i++)ep[o++]=name[i]; ep[o]=0; u32 so[10]; if(ux_stat2(ep,so,0)==0)ino=so[5]; }
        *(u64*)d=ino; *(i64*)(d+8)=n+1; *(u16*)(d+16)=(u16)rl; d[18]=(u8)type;
        for(int i=0;i<=L;i++)d[19+i]=(u8)name[i];
        used+=rl; v->off++; }
    return (long)used;
}

static int lx_poll_one(int fd,int ev){
    struct vfile* v=lx_vf(fd); if(!v)return 0x20;
    int re=0;
    if(v->dev==6){ struct jpipe* pp=&g_pipe[v->fs_idx]; if(pp->n>0)re|=1; if(pp->writers<=0)re|=0x10; }
    else if(v->dev==7){ struct jpipe* pp=&g_pipe[v->fs_idx]; if(pp->readers<=0)re|=8; else if(pp->n<pp->cap)re|=4; }
    else if(v->dev==5){ re|=4; struct jproc* p=jc_lead(&g_jp[g_jcur]); if((g_tin_n>0||g_tin_eof)&&p->pgid==g_tty_fg)re|=1; }
    else if(v->dev==10){ re|=4; if(g_mq_h!=g_mq_t)re|=1; }
    else if(v->dev==16){ extern int xin_ready(void); re|=4; if(xin_ready())re|=1; }
    else if(v->dev==17){ extern int fid_dev_ready(void); re|=4; if(fid_dev_ready())re|=1; }
    else if(v->dev==LX_AFU)re=un_poll(v->fs_idx);
    else if(v->dev==LX_EVFD){ re|=4; if(g_ev[v->fs_idx].cnt)re|=1; }
    else if(v->dev==LX_TMFD){ tm_update(&g_tm[v->fs_idx]); if(g_tm[v->fs_idx].exp)re|=1; }
    else if(v->dev==LX_INOFD){ if(g_ino[v->fs_idx].qn)re|=1; }
    else if(v->dev==LX_SIGFD){ if(sfd_pend(v->fs_idx))re|=1; }
    else if(v->dev==14)re=lxs_poll(v->fs_idx);
    else if(v->dev==LX_PTM||v->dev==LX_PTS)re=pty_poll(v);
    else if(v->dev==LX_SNDCTL||v->dev==LX_SNDPCM)re=asnd_poll(v);
    else re|=5;
    return re&(ev|0x38);
}
