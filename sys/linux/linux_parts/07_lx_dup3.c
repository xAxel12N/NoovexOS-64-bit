static long lx_dup3(int o,int n,int fl){
    if(!lx_vf(o))return -LX_EBADF; if(n<0||n>=FDMAX)return -LX_EBADF;
    if(o==n)return fl<0?n:-LX_EINVAL;
    jc_dup2(o,n); if(fl>0&&(fl&0x80000))fdce()[n]=1; return n;
}
static long lx_close(int fd){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(v->dev==0||v->dev==100){ lk_proc_close(v); if(v->dev==0)ino_ev_fd(fd,(v->flags&3)?0x8u:0x10u); }
    int* t=fdt(); vf_unref(t[fd]); t[fd]=-1; fdce()[fd]=0; return 0;
}
static long lx_getrandom(u64 buf,u64 n){
    if(n>LX_IOMAX)n=LX_IOMAX; if(!lx_okw(buf,n))return -LX_EFAULT;
    csprng_bytes((u8*)buf,(int)n);
    return (long)n;
}
static long lx_sysinfo(u64 out){
    u8 s[112]; lx_zero(s,112);
    *(u64*)(s+0)=g_ticks/100; *(u64*)(s+32)=(u64)g_ram_mb<<20; *(u64*)(s+40)=(u64)pframe_free_count()*4096ULL;
    int n=0; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE)n++; *(u16*)(s+80)=(u16)n; *(u32*)(s+104)=1;
    return lx_put(out,s,112);
}
static long lx_statfs(u64 out){
    unsigned long o[8]; ux_statfs("/",o); u64 s[15]; lx_zero(s,sizeof(s));
    s[0]=o[0]; s[1]=o[1]; s[2]=o[2]; s[3]=o[3]; s[4]=o[4]; s[5]=o[5]; s[6]=o[6]; s[8]=o[7]; s[9]=o[1];
    return lx_put(out,s,120);
}
static long lx_sendfile(struct sysregs* r,int out,int in,u64 offp,u64 cnt){
    struct vfile* vi=lx_vf(in); struct vfile* vo=lx_vf(out); if(!vi||!vo)return -LX_EBADF;
    if((vi->flags&3)==1)return -LX_EBADF;
    if(vo->dev==0&&(vo->flags&3)==0)return -LX_EBADF;
    if(vo->dev==LX_DIRDEV||vo->dev==6||vo->dev==100)return -LX_EBADF;
    if(vi->dev!=0||vi->fs_idx<0)return -LX_EINVAL;
    long off=vi->off; if(offp){ if(lx_get(&off,offp,8))return -LX_EFAULT; }
    if(off<0)return -LX_EINVAL;
    int k=vi->fs_idx; long avail=FS[k].len-off; if(avail<=0)return 0; if(cnt>(u64)avail)cnt=(u64)avail; if(cnt>LX_IOMAX)cnt=LX_IOMAX;
    if(fs_ensure(k))return -LX_EIO;
    int n; if(vo->dev==0&&vo->fs_idx==k){ static char sfb[65536]; if(cnt>sizeof sfb)cnt=sizeof sfb; kmemcpy(sfb,FS[k].data+off,(unsigned long)cnt); n=vfs_write(out,sfb,(int)cnt); } else n=vfs_write(out,FS[k].data+off,(int)cnt);
    if(n==JS_BLOCK){ if(lx_nonblock(vo))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0)return vo->dev==7?-LX_EPIPE:-LX_EIO;
    if(n>0)lx_persist(out);
    if(offp){ long no=off+n; lx_put(offp,&no,8); } else vi->off+=n;
    return n;
}
static long lx_utimens(const char* p,u64 times){
    u32 at=0,mt=0;
    if(times){ u64 t[4]; if(lx_get(t,times,32))return -LX_EFAULT;
        if(t[1]==0x3ffffffeULL&&t[3]==0x3ffffffeULL)return 0;
        if(t[1]!=0x3fffffffULL&&t[1]!=0x3ffffffeULL)at=(u32)t[0]; if(t[3]!=0x3fffffffULL&&t[3]!=0x3ffffffeULL)mt=(u32)t[2]; }
    if(!lx_exists(p))return -LX_ENOENT;
    return ux_utime(p,at,mt)?-LX_EACCES:0;
}
static long lx_chdir_node(int d){ if(d<0)return -LX_ENOENT; if(!ux_dir_ok(d,1))return -LX_EACCES; g_dcwd=d; return 0; }
static long lx_getcwd(u64 buf,u64 sz){
    char pb[256]; dt_path(g_dcwd,pb); u64 L=(u64)lx_slen(pb)+1;
    if(sz<L)return -LX_ERANGE; if(lx_put(buf,pb,L))return -LX_EFAULT; return (long)L;
}
static long lx_fchmod(int fd,int mode){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(v->dev==LX_DIRDEV){ char dp[LX_PATH]; dt_path(v->fs_idx,dp); return ux_chmod(dp,(unsigned)mode)?-LX_EPERM:0; }
    if(v->dev!=0||v->fs_idx<0)return 0; int k=v->fs_idx;
    if(g_uid!=0&&g_uid!=FS[k].uid)return -LX_EPERM;
    FS[k].mode=(FS[k].mode&0170000u)|((unsigned)mode&07777u); if(!(FS[k].mode&0170000u))FS[k].mode|=0100000u; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
static long lx_setid(int* cur,int nv){ if(nv==-1)return 0; if(g_uid==0||nv==*cur){ *cur=nv; return 0; } return -LX_EPERM; }

struct lx_scr { u32 nr,pid; u64 a1,a2; long ret; };
#define LX_SCR 512
static struct lx_scr g_scr0[LX_SCR]; static struct lx_scr* g_scr=g_scr0; static int g_scrn=LX_SCR; static int g_scr_i;
static void lx_scr_grow(void){ if(g_scrn>LX_SCR)return; struct lx_scr* b=(struct lx_scr*)kmalloc(sizeof(struct lx_scr)*65536UL); if(!b)return; g_scr_i=0; g_scr=b; g_scrn=65536; }
struct lx_shm { int used,key,rm,natt,mi; u64 size; u32 cpid; };
static struct lx_shm g_lxshm[64];
struct lx_sha { u64 cr3,va,len; int id; };
static struct lx_sha g_sha[256];
static void lx_shm_free(int id){ struct lx_shm* s=&g_lxshm[id]; if(!s->used)return; mfd_close(s->mi); s->used=0; }
static void lx_sha_dead(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_sha* a=&g_sha[i]; if(!a->len||a->cr3!=cr3)continue; struct lx_shm* s=&g_lxshm[a->id]; a->len=0; if(s->used&&--s->natt<=0){ s->natt=0; if(s->rm)lx_shm_free(a->id); } } }
static void lx_sha_dup(u64 src,u64 dst){ src&=~0xFFFULL; int n=0; for(int i=0;i<256;i++) if(g_sha[i].len&&g_sha[i].cr3==src)n++;
    for(int i=0;i<256&&n>0;i++) if(g_sha[i].len&&g_sha[i].cr3==src){ n--; for(int j=0;j<256;j++) if(!g_sha[j].len){ g_sha[j]=g_sha[i]; g_sha[j].cr3=dst&~0xFFFULL; g_lxshm[g_sha[i].id].natt++; break; } } }
static long lx_shmget(int key,u64 size,int fl){
    if(key!=0){ for(int i=0;i<64;i++){ struct lx_shm* s=&g_lxshm[i]; if(s->used&&!s->rm&&s->key==key){ if((fl&0x200)&&(fl&0x400))return -LX_EEXIST; if(size>s->size)return -LX_EINVAL; return i; } } if(!(fl&0x200))return -LX_ENOENT; }
    if(size<1||size>0x40000000ULL)return -LX_EINVAL;
    { u64 pg=(size+0xFFFULL)>>12; if(pg>(u64)(pframe_free_count()/4u))return -LX_ENOMEM; }
    int id=-1; for(int i=0;i<64;i++) if(!g_lxshm[i].used){ id=i; break; } if(id<0)return -28;
    int mi=mfd_new(); if(mi<0)return -28; if(mfd_truncate(mi,(size+0xFFFULL)&~0xFFFULL)){ mfd_close(mi); return -LX_ENOMEM; }
    struct lx_shm* s=&g_lxshm[id]; s->used=1; s->key=key; s->rm=0; s->natt=0; s->mi=mi; s->size=size; s->cpid=(u32)jc_lead(&g_jp[g_jcur])->pid; return id; }
static long lx_shmat(int id,u64 addr,int fl){
    if(id<0||id>=64||!g_lxshm[id].used)return -LX_EINVAL; struct lx_shm* s=&g_lxshm[id]; struct jproc* p=&g_jp[g_jcur]; struct memfd* m=&g_mfd[s->mi];
    u64 pages=(s->size+0xFFFULL)>>12, base;
    if(addr){ if(fl&0x2000)addr&=~0xFFFULL; if(addr&0xFFF)return -LX_EINVAL; if(!jc_uok(addr,pages<<12))return -LX_EINVAL; if(!(fl&0x4000)){ for(u64 v=addr;v<addr+(pages<<12);v+=0x1000ULL){ u64* e=vm_walk(p->cr3,v,0); if(e&&vm_pte_used(*e))return -LX_EINVAL; } } lx_unmap_range(addr,addr+(pages<<12)); base=addr; }
    else { base=lx_find_gap(p->cr3,pages+1); if(!base)return -LX_ENOMEM; }
    int slot=-1; for(int i=0;i<256;i++) if(!g_sha[i].len){ slot=i; break; } if(slot<0)return -LX_ENOMEM;
    u64 pf=(fl&0x1000)?(0x5ULL|PTE_SHR):(0x7ULL|PTE_SHR);
    for(u64 i=0;i<pages;i++){ u64 f=m->frames[i]; pframe_ref(f); if(vm_map(p->cr3,base+(i<<12),f,pf)){ pframe_free(f); lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; } }
    g_sha[slot].cr3=p->cr3&~0xFFFULL; g_sha[slot].va=base; g_sha[slot].len=pages<<12; g_sha[slot].id=id; s->natt++; return (long)base; }
static long lx_shmdt(u64 addr){ struct jproc* p=&g_jp[g_jcur]; u64 cr3=p->cr3&~0xFFFULL;
    for(int i=0;i<256;i++){ struct lx_sha* a=&g_sha[i]; if(a->len&&a->cr3==cr3&&a->va==addr){ u64 len=a->len; int id=a->id; a->len=0; lx_unmap_range(addr,addr+len); struct lx_shm* s=&g_lxshm[id]; if(s->used&&--s->natt<=0){ s->natt=0; if(s->rm)lx_shm_free(id); } return 0; } }
    return -LX_EINVAL; }
static long lx_shmctl(int id,int cmd,u64 buf){ cmd&=0xFF;
    if(cmd==3||cmd==14){ if(buf){ u8 z[128]; lx_zero(z,128); if(lx_put(buf,z,cmd==3?64:56))return -LX_EFAULT; } return 63; }
    if(id<0||id>=64||!g_lxshm[id].used)return -LX_EINVAL; struct lx_shm* s=&g_lxshm[id];
    if(cmd==0){ s->rm=1; if(s->natt<=0)lx_shm_free(id); return 0; }
    if(cmd==2||cmd==13){ if(!buf)return -LX_EFAULT; u8 d[112]; lx_zero(d,112); *(int*)(d+0)=s->key; *(u32*)(d+4)=(u32)g_uid; *(u32*)(d+8)=(u32)g_gid; *(u32*)(d+12)=(u32)g_uid; *(u32*)(d+16)=(u32)g_gid; *(u16*)(d+20)=0777; *(u64*)(d+48)=s->size; *(u32*)(d+80)=s->cpid; *(u64*)(d+88)=(u64)s->natt; if(lx_put(buf,d,112))return -LX_EFAULT; return cmd==13?id:0; }
    if(cmd==1||cmd==11||cmd==12)return 0;
    return -LX_EINVAL; }
struct lx_scr g_scr_snap[32]; int g_scr_sn;
u32 g_scf_nr[40], g_scf_err[40], g_scf_ct[40]; u64 g_scf_a1[40], g_scf_a2[40]; int g_scf_n;
void lx_sc_snap(void){ int n=g_scr_i<32?g_scr_i:32; for(int i=0;i<n;i++)g_scr_snap[i]=g_scr[(g_scr_i-n+i)%g_scrn]; g_scr_sn=n; }
void lx_sc_dump(u32 pid,int max){ int n=g_scr_i<g_scrn?g_scr_i:g_scrn; int idx[24]; int c=0; for(int i=0;i<n&&c<max&&c<24;i++){ struct lx_scr* e=&g_scr[(g_scr_i-1-i)%g_scrn]; if(e->pid==pid)idx[c++]=(g_scr_i-1-i)%g_scrn; }
    for(int j=c-1;j>=0;j--){ struct lx_scr* e=&g_scr[idx[j]]; char b[120],*q=b; q=scopy(q,"  sc "); q=udec(q,e->nr); *q++='('; *q++='0'; *q++='x'; q=lx_hx(q,e->a1,1); *q++=','; *q++=' '; *q++='0'; *q++='x'; q=lx_hx(q,e->a2,1); q=scopy(q,") = "); if(e->ret<0){ *q++='-'; q=udec(q,(u32)(-e->ret)); } else { *q++='0'; *q++='x'; q=lx_hx(q,(u64)e->ret,1); } *q=0; klog(b); } }
u16 g_lastsc[JMAX]; u32 g_sccount[JMAX]; u16 g_cursc[JMAX]; u64 g_cura1[JMAX];
void lx_ps_dump(void){ for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_TDEAD||!p->lx)continue; if(p->tg!=i)continue;
        int nt=0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_TDEAD&&g_jp[j].tg==i)nt++;
        char b[240],*q=b; q=scopy(q,"ps "); q=udec(q,(u32)p->pid); q=scopy(q," ppid "); q=udec(q,(u32)p->ppid); q=scopy(q," st "); q=udec(q,(u32)p->state); q=scopy(q," thr "); q=udec(q,(u32)nt); q=scopy(q," last "); q=udec(q,(u32)g_lastsc[i]); *q++=' '; for(int k=0;p->cmd[k]&&k<47;k++)*q++=p->cmd[k]; *q=0; klog(b);
        for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_TDEAD&&g_jp[j].tg==i){ q=b; q=scopy(q,"   t"); q=udec(q,(u32)g_jp[j].pid); q=scopy(q," st "); q=udec(q,(u32)g_jp[j].state); q=scopy(q," in "); q=udec(q,(u32)g_cursc[j]); q=scopy(q," a1 "); q=udec(q,(u32)g_cura1[j]);
            u32 sc=g_cursc[j]; if(sc==0||sc==1||sc==7||sc==23||sc==44||sc==45||sc==46||sc==47||sc==232||sc==281||sc==270||sc==271){ int fd=(int)g_cura1[j]; struct jproc* L=jc_lead(&g_jp[j]); if(sc!=7&&sc!=23&&sc!=270&&sc!=271&&fd>=0&&fd<FDMAX&&L->fd[fd]>=0){ struct vfile* v=&g_vf[L->fd[fd]]; q=scopy(q," dev "); q=udec(q,(u32)v->dev); q=scopy(q," idx "); q=udec(q,(u32)v->fs_idx); if(v->dev==LX_AFU){ q=scopy(q," "); q=un_desc(q,v->fs_idx); } } }
            *q=0; klog(b); } } }
extern int g_lxtrace;
static void lx_trace(u32 nr,u32 pid,u64 a1,u64 a2,long v){
    char b[240],*q=b; q=scopy(q,"T "); q=udec(q,pid); *q++=' '; const char* nm=0; { extern const char* lx_scname(u32); nm=lx_scname(nr); } if(nm)q=scopy(q,nm); else q=udec(q,nr); *q++='(';
    u64 sp=0; if(nr==2||nr==4||nr==6||nr==21||nr==59||nr==80||nr==83||nr==84||nr==87||nr==89||nr==90||nr==92||nr==137||nr==76||nr==82)sp=a1; else if(nr==257||nr==262||nr==263||nr==267||nr==268||nr==269||nr==332||nr==258||nr==260||nr==280||nr==439||nr==264||nr==316||nr==322)sp=a2;
    if(sp&&jc_umapped(sp,1)){ *q++='"'; for(int i=0;i<120;i++){ if(!jc_umapped(sp+i,1))break; char c=*(const char*)(sp+i); if(!c)break; *q++=(c>=32&&c<127)?c:'?'; } *q++='"'; *q++=','; *q++=' '; }
    *q++='0'; *q++='x'; q=lx_hx(q,a1,1); *q++=','; *q++=' '; *q++='0'; *q++='x'; q=lx_hx(q,a2,1); q=scopy(q,") = "); if(v<0&&v>-4096){ *q++='-'; q=udec(q,(u32)(-v)); } else { *q++='0'; *q++='x'; q=lx_hx(q,(u64)v,1); } *q=0; klog(b); }
static void lx_sc_note(u32 nr,u32 pid,u64 a1,u64 a2,long v){
    if(g_lxtrace==-1||(g_lxtrace==-2&&(nr==59||nr==322||nr==231||nr==60||(nr==257&&v==-2)||(nr==2&&v==-2)||(nr==21&&v==-2)))||(g_lxtrace==-3&&v<0&&v>-4096&&v!=-11&&v!=-4&&v!=-110&&v!=-2&&!(nr==89&&v==-22)&&nr!=61))lx_trace(nr,pid,a1,a2,v);
    if(g_jcur>=0&&g_jcur<JMAX){ g_lastsc[g_jcur]=(u16)nr; g_sccount[g_jcur]++; }
    if(g_lxtrace==-4&&g_scrn==LX_SCR)lx_scr_grow();
    struct lx_scr* s=&g_scr[g_scr_i%g_scrn]; g_scr_i++; s->nr=nr; s->pid=pid; s->a1=a1; s->a2=a2; s->ret=v;
    if(v>=0||v<-4095)return; u32 e=(u32)(-v); if(e==2||e==11||e==4||e==38||e==110||e==25||e==17||e==20)return;
    for(int i=0;i<g_scf_n;i++) if(g_scf_nr[i]==nr&&g_scf_err[i]==e){ g_scf_ct[i]++; g_scf_a1[i]=a1; g_scf_a2[i]=a2; return; }
    if(g_scf_n<40){ g_scf_nr[g_scf_n]=nr; g_scf_err[g_scf_n]=e; g_scf_ct[g_scf_n]=1; g_scf_a1[g_scf_n]=a1; g_scf_a2[g_scf_n]=a2; g_scf_n++; } }
static long lx_dispatch(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur];
    u64 nr=r->rax, a1=r->rdi, a2=r->rsi, a3=r->rdx, a4=r->r10, a5=r->r8, a6=r->r9;
    char pa[LX_PATH], pb[LX_PATH]; long e;
    #define PATH1(fd,u,emp) do{ e=lx_path((fd),(u),pa,(emp)); if(e<0)return e; }while(0)
    #define PATH2(fd,u) do{ e=lx_path((fd),(u),pb,0); if(e<0)return e; }while(0)
    switch(nr){
    case 0: return lx_read(r,(int)a1,a2,a3);
    case 1: return lx_write(r,(int)a1,a2,a3);
    case 2: return lx_open(LX_AT_FDCWD,a1,(int)a2,(int)a3);
    case 3: return lx_close((int)a1);
    case 4: PATH1(LX_AT_FDCWD,a1,0); return lx_stat_path(pa,a2,1);
    case 5: return lx_fstat((int)a1,a2);
    case 6: PATH1(LX_AT_FDCWD,a1,0); return lx_stat_path(pa,a2,0);
    case 7: return lx_poll(r,a1,a2,(long)(int)a3);
    case 213: return lx_epoll_create(0);
    case 291: return lx_epoll_create((int)a1);
    case 233: return lx_epoll_ctl((int)a1,(int)a2,(int)a3,a4);
    case 232: case 281: case 441: return lx_epoll_wait(r,(int)a1,a2,(int)a3,nr==441?(a4?0:-1):(long)(int)a4);
    case 8: return lx_lseek((int)a1,(long)a2,(int)a3);
    case 9: return lx_mmap(a1,a2,(int)a3,(int)a4,(int)a5,a6);
    case 10: return lx_mprotect(a1,a2,(int)a3);
    case 11: return lx_munmap(a1,a2);
    case 12: return lx_brk(a1);
    case 13: return lx_sigaction((int)a1,a2,a3,a4);
    case 14: return lx_sigprocmask((int)a1,a2,a3,a4);
    case 15: lx_sigreturn(r);
    case 16: { struct vfile* iv=lx_vf((int)a1); if(iv&&iv->dev==LX_SNDPCM&&(u32)a2!=0x5451u&&(u32)a2!=0x5450u&&(u32)a2!=0x5421u)return asnd_pcm_ioctl(r,iv,(u32)a2,a3); return lx_ioctl((int)a1,a2,a3); }
    case 17: return lx_prw((int)a1,a2,a3,(long)a4,0);
    case 18: return lx_prw((int)a1,a2,a3,(long)a4,1);
    case 19: return lx_rwv(r,(int)a1,a2,(int)a3,0);
    case 20: return lx_rwv(r,(int)a1,a2,(int)a3,1);
    case 21: PATH1(LX_AT_FDCWD,a1,0); return lx_access(pa,(int)a2);
    case 22: return lx_pipe(a1,0);
    case 23: { long ms=-1; if(a5){ u64 tv[2]; if(lx_get(tv,a5,16))return -LX_EFAULT; if((i64)tv[0]<0||tv[1]>=1000000ULL)return -LX_EINVAL; if(tv[0]>100000000ULL)tv[0]=100000000ULL; ms=(long)(tv[0]*1000+tv[1]/1000); } return lx_select(r,(int)a1,a2,a3,a4,ms); }
    case 24: r->rax=0; jc_yield_sys(r);
    case 25: return -LX_ENOMEM;
    case 29: return lx_shmget((int)a1,a2,(int)a3);
    case 30: return lx_shmat((int)a1,a2,(int)a3);
    case 31: return lx_shmctl((int)a1,(int)a2,a3);
    case 67: return lx_shmdt(a1);
    case 26: { if(a1&0xFFF)return -LX_EINVAL; lx_fm_msync(p->cr3,a1,a1+a2); return 0; }
    case 27: return -LX_ENOSYS;
    case 28: return lx_madvise(a1,a2,(int)a3);
    case 187: return 0;
    case 32: { if(!lx_vf((int)a1))return -LX_EBADF; int vi=fdt()[(int)a1]; int n=lx_fd_install(vi,0,0); if(n>=0)vf_ref(vi); return n; }
    case 33: return lx_dup3((int)a1,(int)a2,-1);
    case 34: r->rax=(u64)-LX_EINTR; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL);
    case 35: return lx_nanosleep(r,a1);
    case 36: return lx_getitimer((int)a1,a2);
    case 38: return lx_setitimer((int)a1,a2,a3);
    case 37: return lx_alarm((unsigned)a1);
    case 39: return jc_lead(p)->pid;
    case 40: return lx_sendfile(r,(int)a1,(int)a2,a3,a4);
    case 41: return lx_socket((int)a1,(int)a2,(int)a3);
    case 42: return lx_connect(r,(int)a1,a2,a3);
    case 43: return lx_accept(r,(int)a1,a2,a3,0);
    case 288: return lx_accept(r,(int)a1,a2,a3,(int)a4);
    case 44: return lx_sendto(r,(int)a1,a2,a3,(int)a4,a5,a6);
    case 45: return lx_recvfrom(r,(int)a1,a2,a3,(int)a4,a5,a6);
    case 46: return lx_msg(r,(int)a1,a2,(int)a3,1);
    case 47: return lx_msg(r,(int)a1,a2,(int)a3,0);
    case 48: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU){ if(a2>2)return -LX_EINVAL; struct unsock* s=un_get(vu->fs_idx); if(!s)return -LX_EBADF; if(a2==0||a2==2)s->rd_shut=1; if(a2==1||a2==2)s->wr_shut=1; return 0; } struct vfile* v; int sid=lx_sockfd((int)a1,&v); if(sid<0)return sid; if(a2>2)return -LX_EINVAL; return lxs_shutdown(sid,(int)a2); }
    case 49: return lx_bind((int)a1,a2,a3);
    case 50: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU)return un_listen(vu->fs_idx,(int)a2); struct vfile* v; int sid=lx_sockfd((int)a1,&v); return sid<0?sid:lxs_listen(sid,(int)a2); }
    case 51: return lx_sockname((int)a1,a2,a3,0);
    case 52: return lx_sockname((int)a1,a2,a3,1);
    case 53: { if((int)a1!=1){ lx_gap(2,a1); return -97; } int sty=(int)a2&0xF; if(sty!=1&&sty!=2&&sty!=5){ lx_gap(8,(u64)sty); return -93; } if(!lx_okw(a4,8))return -LX_EFAULT;
        int A=un_new(); if(A<0)return A; int B=un_new(); if(B<0){ un_close(A); return B; }
        g_un[A].state=4; g_un[A].peer=B; g_un[B].state=4; g_un[B].peer=A; g_un[A].seq=g_un[B].seq=sty!=1;
        int cl=((int)a2&0x80000)!=0, nb=(int)a2&0x800;
        int va=jc_newvf(LX_AFU,A,O_RDWR|nb); if(va<0){ un_close(A); un_close(B); return -LX_EMFILE; }
        int vb=jc_newvf(LX_AFU,B,O_RDWR|nb); if(vb<0){ vf_unref(va); un_close(B); return -LX_EMFILE; }
        int fa=lx_fd_install(va,cl,0); if(fa<0){ vf_unref(va); vf_unref(vb); return fa; }
        int fb=lx_fd_install(vb,cl,0); if(fb<0){ fdt()[fa]=-1; vf_unref(va); vf_unref(vb); return fb; }
        int sv[2]={fa,fb}; if(lx_put(a4,sv,8)){ fdt()[fa]=-1; fdt()[fb]=-1; vf_unref(va); vf_unref(vb); return -LX_EFAULT; }
        return 0; }
    case 54: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU){ if((int)a2==1&&(int)a3==16&&a4&&a5>=4){ int x=0; if(lx_get(&x,a4,4))return -LX_EFAULT; struct unsock* us=un_get(vu->fs_idx); if(us)us->passcred=x!=0; } return 0; } struct vfile* v; int sid=lx_sockfd((int)a1,&v); return sid<0?sid:0; }
    case 55: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU){ int lvl=(int)a2,opt=(int)a3,x=0;
            if(lvl==1&&opt==17){ struct unsock* us=un_get(vu->fs_idx); struct unsock* pu=us?un_get(us->peer):0; u32 uc[3]={(u32)(pu?pu->cpid:us?us->cpid:1),(u32)g_uid,(u32)g_gid}; if(!a4||!a5)return -LX_EFAULT; u32 L; if(lx_get(&L,a5,4))return -LX_EFAULT; if(L>12)L=12; if(lx_put(a4,uc,L))return -LX_EFAULT; return lx_put(a5,&L,4); }
            if(lvl==1){ if(opt==3)x=1; else if(opt==7||opt==8)x=UN_CAP; else if(opt==39)x=1; else if(opt==30){ struct unsock* us=un_get(vu->fs_idx); x=us&&us->state==3; } }
            if(!a4||!a5)return -LX_EFAULT; u32 L; if(lx_get(&L,a5,4))return -LX_EFAULT; if(L>4)L=4; if(lx_put(a4,&x,L))return -LX_EFAULT; return lx_put(a5,&L,4); }
        return lx_getsockopt((int)a1,(int)a2,(int)a3,a4,a5); }
    case 319: return lx_memfd((int)a2);
    case 284: return lx_eventfd(a1,0);
    case 290: return lx_eventfd(a1,(int)a2);
    case 283: return lx_timerfd_create((int)a1,(int)a2);
    case 286: return lx_timerfd_set((int)a1,(int)a2,a3,a4);
    case 287: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev!=LX_TMFD)return -LX_EINVAL; return lx_timerfd_get(&g_tm[v->fs_idx],a2); }
    case 204: { u64 m=0; extern volatile int g_cores_online; int n=g_smp_on?g_cores_online:1; for(int i=0;i<n&&i<64;i++)m|=1ULL<<i; u64 cap=a2; if(cap<8)return -LX_EINVAL; if(lx_put(a3,&m,8))return -LX_EFAULT; return 8; }
    case 203: return 0;
    case 309: { u32 c=(u32)cpu_id(); if(a1&&lx_put(a1,&c,4))return -LX_EFAULT; if(a2&&lx_put(a2,&(u32){0},4))return -LX_EFAULT; return 0; }
    case 60: jc_thread_exit(g_jcur,((int)a1&0xFF)<<8); jc_leave();
    case 231: if(g_lxtrace==-4){ lx_scr_grow(); struct jproc* Lp=jc_lead(&g_jp[g_jcur]); char b[96],*q=b; q=scopy(q,"lx exit dump: pid "); q=udec(q,(u32)Lp->pid); q=scopy(q," code "); q=udec(q,(u32)(a1&0xFF)); *q=0; klog(b); for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].tg==g_jp[g_jcur].tg){ char c[40],*w=c; w=scopy(w," thread "); w=udec(w,(u32)g_jp[j].pid); *w=0; klog(c); lx_sc_dump((u32)g_jp[j].pid,24); } }
      if((a1&0xFF)!=0){ static int ne; if(ne<24){ ne++; char b[96],*q=b; q=scopy(q,"lx exit_group: pid "); q=udec(q,(u32)jc_lead(&g_jp[g_jcur])->pid); q=scopy(q," code "); q=udec(q,(u32)(a1&0xFF)); *q=0; klog(b); } } jc_finish(g_jcur,((int)a1&0xFF)<<8); jc_leave();
    case 56: return lx_clone(r,a1,a2,a3,a4,a5);
    case 435: { if(a2<64||a2>4096)return -LX_EINVAL; u64 ca[11]; lx_zero((u8*)ca,88); if(lx_get(ca,a1,a2<88?a2:88))return -LX_EFAULT;
        if((ca[0]&0x1000ULL)||(ca[0]>>32)||ca[9]||ca[4]>63)return -LX_ENOSYS;
        u64 st=ca[5]?ca[5]+ca[6]:0; return lx_clone(r,(ca[0]&~0xFFULL)|ca[4],st,ca[3],ca[2],ca[7]); }
    case 57: case 58: return lx_clone(r,0,0,0,0,0);
    case 59: return lx_execve(r,a1,a2,a3);
    case 61: return lx_wait4(r,(int)a1,a2,(int)a3,a4);
    case 200: case 234: case 297: { int tg3=nr==234||nr==297; int tid=(int)(tg3?a2:a1), sig=(int)(tg3?a3:a2);
        if(sig==6||sig==11||sig==4||sig==5||sig==7||sig==8){ static int nk; if(nk<16){ nk++; char b[96],*q=b; q=scopy(q,"lx tgkill: pid "); q=udec(q,(u32)g_jp[g_jcur].pid); q=scopy(q," -> "); q=udec(q,(u32)tid); q=scopy(q," sig "); q=udec(q,(u32)sig); *q=0; klog(b); } }
        if(sig<0||sig>64||tid<=0)return -LX_EINVAL; int t=jc_idx(tid); if(t<0||g_jp[t].state==S_ZOMB||g_jp[t].state==S_TDEAD)return -LX_ESRCH;
        if(tg3&&g_jp[t].tgid!=(int)a1)return -LX_ESRCH;
        if(g_uid!=0&&g_jp[t].uid!=g_uid)return -LX_EPERM;
        if(sig>=32||!sig)return 0;
        if(sig==9||sig==19)jc_signal_proc(t,sig); else { g_jp[t].sicode[sig]=-6; jc_signal_thread(t,sig); } return 0; }
    case 62: case 129: { int pid=(int)a1, sig=(int)a2;
        if(sig==6||sig==11||sig==4||sig==5||sig==7||sig==8||sig==9){ static int nk2; if(nk2<16){ nk2++; char b[96],*q=b; q=scopy(q,"lx kill: pid "); q=udec(q,(u32)g_jp[g_jcur].pid); q=scopy(q," -> "); q=udec(q,(u32)pid); q=scopy(q," sig "); q=udec(q,(u32)sig); *q=0; klog(b); } }
        if(sig<0||sig>64)return -LX_EINVAL; if(sig>=32)return 0;
        g_kill_check=1; int rv=jc_kill(pid,sig,g_jcur); g_kill_check=0;
        if(rv<0)return g_kill_denied?-LX_EPERM:-LX_ESRCH; return 0; }
    case 128: { static u64 stw[JMAX]; static int stwp[JMAX]; if(stwp[g_jcur]!=g_jp[g_jcur].pid){ stwp[g_jcur]=g_jp[g_jcur].pid; stw[g_jcur]=0; } if(a4!=8)return -LX_EINVAL; u64 set; if(lx_get(&set,a1,8))return -LX_EFAULT; struct jproc* p=&g_jp[g_jcur]; struct jproc* L=jc_lead(p);
        for(int sg=1;sg<32;sg++){ if(!((set>>(sg-1))&1))continue; u32 bit=1u<<sg; struct jproc* w=(p->sigpend&bit)?p:(L->sigpend&bit)?L:0; if(!w)continue;
            w->sigpend&=~bit; stw[g_jcur]=0; if(a2){ u8 si[128]; lx_zero(si,128); *(int*)si=sg; int sc=w->sicode[sg]; *(int*)(si+8)=sc<0?sc:0; if(lx_put(a2,si,128))return -LX_EFAULT; } return sg; }
        if(a3){ u64 ts[2]; if(lx_get(ts,a3,16))return -LX_EFAULT; if(!ts[0]&&!ts[1]){ stw[g_jcur]=0; return -LX_EAGAIN; }
            if(!stw[g_jcur])stw[g_jcur]=g_ticks+ts[0]*100+ts[1]/10000000ULL+1; if(g_ticks>=stw[g_jcur]){ stw[g_jcur]=0; return -LX_EAGAIN; } }
        if(jc_deliverable(p)){ stw[g_jcur]=0; return -LX_EINTR; }
        jc_block(r,S_WAIT,g_ticks+1); }
    case 63: return lx_uname(a1);
    case 72: if(a2==5||a2==6||a2==7||a2==36||a2==37||a2==38)return lx_fcntl_lock(r,(int)a1,(int)a2,a3); return lx_fcntl((int)a1,(int)a2,a3);
    case 73: return lx_flock(r,(int)a1,(int)a2);
    case 74: case 75: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N)g_fs_dirty[v->fs_idx]=0; return k_fsync((int)a1)?-LX_EIO:0; }
    case 76: PATH1(LX_AT_FDCWD,a1,0); if((long)a2<0)return -LX_EINVAL; if(!lx_exists(pa))return -LX_ENOENT; if(lx_isdir(pa))return -LX_EISDIR; return k_truncate(pa,(long)a2)?-LX_EACCES:0;
    case 77: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if((long)a2<0)return -LX_EINVAL; if(v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; if(((u64)a2>m->size&&(m->seals&4))||((u64)a2<m->size&&(m->seals&2)))return -LX_EPERM; return mfd_truncate(v->fs_idx,(u64)a2); } if(v->dev!=0||(v->flags&3)==0)return -LX_EINVAL; return k_ftruncate((int)a1,(long)a2)?-LX_EACCES:0; }
    case 78: { long n=lx_getdents((int)a1,a2,a3); if(n<=0)return n; u8* d=(u8*)a2; long o=0; while(o<n){ u16 rl=*(u16*)(d+o+16); u8 ty=d[o+18]; int L=lx_slen((char*)d+o+19); for(int i=0;i<=L;i++)d[o+18+i]=d[o+19+i]; d[o+rl-1]=ty; o+=rl; } return n; }
    case 253: return lx_inotify_init(0);
    case 294: return lx_inotify_init((int)a1);
    case 254: PATH1(LX_AT_FDCWD,a2,0); return lx_inotify_add((int)a1,pa,(u32)a3);
    case 255: return lx_inotify_rm((int)a1,(int)a2);
    case 282: return lx_signalfd((int)a1,a2,a3,0);
    case 289: return lx_signalfd((int)a1,a2,a3,(int)a4);
    case 222: return lx_timer_create((int)a1,a2,a3);
    case 223: return lx_timer_set((int)a1,(int)a2,a3,a4);
    case 224: return lx_timer_get((int)a1,a2);
    case 225: { struct lxptimer* t=lxpt_of((int)a1); return t?t->over:-LX_EINVAL; }
    case 226: { struct lxptimer* t=lxpt_of((int)a1); if(!t)return -LX_EINVAL; t->used=0; return 0; }
    case 247: { int idt=(int)a1, id=(int)a2, op=(int)a4; if(idt<0||idt>2)return -LX_EINVAL; if(!(op&(4|2|8)))return -LX_EINVAL; if(!lx_okw(a3,128))return -LX_EFAULT;
        int pid=idt==0?-1:idt==1?id:(id?-id:0); int o=(op&1)|((op&2)?2:0)|((op&8)?8:0);
        int v=jc_waitpid(r,pid,a3,o); if(v<0)return -LX_ECHILD; if(v==0){ lx_zero((void*)a3,128); return 0; }
        int st=*(int*)a3; u8 si[128]; lx_zero(si,128); *(int*)si=17; int code, val;
        if(st==0xFFFF){ code=6; val=18; } else if((st&0xFF)==0x7F){ code=5; val=(st>>8)&0xFF; } else if(st&0x7F){ code=(st&0x80)?3:2; val=st&0x7F; } else { code=1; val=(st>>8)&0xFF; }
        *(int*)(si+8)=code; *(int*)(si+16)=v; *(u32*)(si+20)=(u32)g_uid; *(int*)(si+24)=val;
        if(a5){ if(!lx_okw(a5,144))return -LX_EFAULT; lx_zero((void*)a5,144); }
        return lx_put(a3,si,128); }
    case 295: case 296: case 327: case 328: { int wr=nr==296||nr==328; i64 off=(i64)a4; if(nr>=327&&off==-1)return lx_rwv(r,(int)a1,a2,(int)a3,wr);
        if(off<0)return -LX_EINVAL; if((int)a3<0||(int)a3>1024)return -LX_EINVAL; if(!lx_ok(a2,a3*16))return -LX_EFAULT; long tot=0;
        for(int i=0;i<(int)a3;i++){ u64 b=((u64*)a2)[i*2], l=((u64*)a2)[i*2+1]; if(!l)continue; long n=lx_prw((int)a1,b,l,off+tot,wr); if(n<0)return tot?tot:n; tot+=n; if((u64)n<l)break; }
        return tot; }
    case 299: case 307: { int snd=nr==307; u32 vl=(u32)a3; if(vl>1024)vl=1024; if(!vl)return 0; if(!lx_okw(a2,(u64)vl*64))return -LX_EFAULT; int fl=(int)a4; int done=0;
        for(u32 i=0;i<vl;i++){ long n=lx_msg(r,(int)a1,a2+(u64)i*64,i?(fl|0x40):fl,snd); if(n<0){ if(done)break; return n; } *(u32*)(a2+(u64)i*64+56)=(u32)n; done++; if(!snd&&(fl&0x10000))fl|=0x40; }
        return done; }
    case 235: case 261: { if(nr==235){ PATH1(LX_AT_FDCWD,a1,0); } else { PATH1((int)a1,a2,0); } u64 tp=nr==235?a2:a3; u32 at=0, mt=0; if(tp){ u64 tv[4]; if(lx_get(tv,tp,32))return -LX_EFAULT; at=(u32)tv[0]; mt=(u32)tv[2]; } if(!lx_exists(pa))return -LX_ENOENT; return ux_utime(pa,at,mt)?-LX_EACCES:0; }
    case 125: { u32 h[2]; if(lx_get(h,a1,8))return -LX_EFAULT; if(h[0]!=0x19980330u&&h[0]!=0x20071026u&&h[0]!=0x20080522u){ h[0]=0x20080522u; lx_put(a1,h,8); return a2?-LX_EINVAL:0; } if(!a2)return 0; u32 c=g_uid==0?0xFFFFFFFFu:0; u32 d[6]={c,c,0,c,c,0}; return lx_put(a2,d,h[0]==0x19980330u?12:24); }
    case 126: return g_uid==0?0:-LX_EPERM;
    case 227: return -LX_EPERM;
    case 251: return 0;
    case 252: return 0;
    case 277: case 306: return lx_vf((int)a1)?0:-LX_EBADF;
    case 314: return 0;
    case 315: { if(a3<48)return -LX_EINVAL; u8 at[56]; lx_zero(at,56); *(u32*)at=48; return lx_put(a2,at,48); }
    case 325: return 0;
    case 79: return lx_getcwd(a1,a2);
    case 80: PATH1(LX_AT_FDCWD,a1,0); { int d=dt_resolve_dir(pa); if(d<0)return lx_exists(pa)?-LX_ENOTDIR:lx_noent(pa); return lx_chdir_node(d); }
    case 81: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev!=LX_DIRDEV)return -LX_ENOTDIR; return lx_chdir_node(v->fs_idx); }
    case 82: PATH1(LX_AT_FDCWD,a1,0); PATH2(LX_AT_FDCWD,a2); return lx_rename(pa,pb);
    case 83: PATH1(LX_AT_FDCWD,a1,0); return lx_mkdir(pa,(int)a2);
    case 84: PATH1(LX_AT_FDCWD,a1,0); return lx_rmdir(pa);
    case 85: return lx_open(LX_AT_FDCWD,a1,0x241,(int)a2);
    case 86: PATH1(LX_AT_FDCWD,a1,0); PATH2(LX_AT_FDCWD,a2); { int L=lx_slen(pb), c=0; while(L>0&&pb[L-1]!='/'){ L--; c++; } if(c>51)return -LX_ENAMETOOLONG; } if(!lx_exists(pa))return -LX_ENOENT; if(lx_exists(pb))return -LX_EEXIST; return ux_link(pa,pb)?-LX_EPERM:0;
    case 87: PATH1(LX_AT_FDCWD,a1,0); return lx_unlink(pa);
    case 88: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; PATH2(LX_AT_FDCWD,a2); if(lx_exists(pb))return -LX_EEXIST; return ux_symlink(pa,pb)?-LX_ENOSPC:0; }
    case 89: { char t[64]; if(lx_str(a1,t,64)>0){ int pe=lx_proc_exe(t,a2,a3); if(pe!=-9999)return pe; } } PATH1(LX_AT_FDCWD,a1,0); { if((long)a3<=0)return -LX_EINVAL; if(!lx_okw(a2,a3))return -LX_EFAULT; int n=ux_readlink(pa,(char*)a2,(int)(a3>LX_PATH?LX_PATH:a3)); if(n<0)return lx_exists(pa)?-LX_EINVAL:-LX_ENOENT; return n; }
    case 90: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chmod(pa,(unsigned)a2)?-LX_EPERM:0;
    case 91: return lx_fchmod((int)a1,(int)a2);
    case 92: case 94: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa)){ int isd; if(ux_lookup(pa,&isd,0)<0)return -LX_ENOENT; } return ux_chown(pa,(int)a2,(int)a3,nr==92)?-LX_EPERM:0;
    case 93: return lx_vf((int)a1)?0:-LX_EBADF;
    case 95: { int old=g_umask; g_umask=(int)a1&0777; return old; }
    case 96: { if(a1){ u64 ns=lx_now_ns(); u64 tv[2]={ns/1000000000ULL,(ns%1000000000ULL)/1000}; if(lx_put(a1,tv,16))return -LX_EFAULT; } if(a2){ u32 tz[2]={0,0}; if(lx_put(a2,tz,8))return -LX_EFAULT; } return 0; }
    case 97: return lx_rlimit((int)a1,a2);
    case 98: { u8 z[144]; lx_zero(z,144); *(u64*)z=g_ticks/100; *(u64*)(z+8)=(g_ticks%100)*10000; return lx_put(a2,z,144); }
    case 99: return lx_sysinfo(a1);
    case 100: { if(a1){ u64 t[4]={g_ticks,0,0,0}; if(lx_put(a1,t,32))return -LX_EFAULT; } return (long)g_ticks; }
    case 102: case 107: return g_uid;
    case 104: case 108: return g_gid;
    case 105: { long v=lx_setid(&g_uid,(int)a1); if(!v)p->uid=g_uid; return v; }
    case 106: { long v=lx_setid(&g_gid,(int)a1); if(!v)p->gid=g_gid; return v; }
    case 113: case 117: { long v=lx_setid(&g_uid,(int)a2); if(!v)p->uid=g_uid; return v; }
    case 114: case 119: { long v=lx_setid(&g_gid,(int)a2); if(!v)p->gid=g_gid; return v; }
    case 118: case 120: { int v=nr==118?g_uid:g_gid; int o[3]={v,v,v}; if(a1&&lx_put(a1,o,4))return -LX_EFAULT; if(a2&&lx_put(a2,o+1,4))return -LX_EFAULT; if(a3&&lx_put(a3,o+2,4))return -LX_EFAULT; return 0; }
    case 115: { extern int acct_gids(int,int*,int); int g[32]; int n=acct_gids(g_uid,g,32); if((int)a1==0)return n; if((int)a1<n)return -LX_EINVAL; if(a2&&lx_put(a2,g,n*4))return -LX_EFAULT; return n; }
    case 116: return g_uid==0?0:-LX_EPERM;
    case 109: { struct jproc* P=jc_lead(p); int pid=(int)a1?(int)a1:P->pid, pg=(int)a2?(int)a2:pid; if(pg<0)return -LX_EINVAL; int t=jc_idx(pid);
        if(t<0)return -LX_ESRCH; t=g_jp[t].tg; if(t!=p->tg&&g_jp[t].ppid!=P->pid)return -LX_ESRCH; if(g_jp[t].sid!=P->sid)return -LX_EPERM; g_jp[t].pgid=pg; jc_tg_sync_ids(t); return 0; }
    case 110: return jc_lead(p)->ppid;
    case 111: return jc_lead(p)->pgid;
    case 112: { struct jproc* P=jc_lead(p); if(P->pgid==P->pid)return -LX_EPERM; P->sid=P->pid; P->pgid=P->pid; jc_tg_sync_ids(p->tg); return P->pid; }
    case 121: { int pid=(int)a1?(int)a1:jc_lead(p)->pid; int t=jc_idx(pid); return t<0?-LX_ESRCH:jc_lead(&g_jp[t])->pgid; }
    case 124: { int pid=(int)a1?(int)a1:jc_lead(p)->pid; int t=jc_idx(pid); return t<0?-LX_ESRCH:jc_lead(&g_jp[t])->sid; }
    case 127: { u64 o=lx_k2l(p->sigpend&p->sigmask); return lx_put(a1,&o,8); }
    case 130: { if(a2!=8)return -LX_EINVAL; u64 m; if(lx_get(&m,a1,8))return -LX_EFAULT;
        p->suspold=p->sigmask; p->susp=1; p->sigmask=lx_l2k(m)&~((1u<<9)|(1u<<19));
        r->rax=(u64)-LX_EINTR; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL); }
    case 131: return lx_sigaltstack(r,a1,a2);
    case 132: PATH1(LX_AT_FDCWD,a1,0); return lx_utimens(pa,0);
    case 133: return -LX_EPERM;
    case 135: return 0;
    case 137: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa))return -LX_ENOENT; return lx_statfs(a2);
    case 138: if(!lx_vf((int)a1))return -LX_EBADF; return lx_statfs(a2);
    case 140: return 20;
    case 141: return 0;
    case 142: case 144: case 145: case 146: case 147: return 0;
    case 143: { int z=0; return lx_put(a2,&z,4); }
    case 148: return -LX_ENOSYS;
    case 149: case 150: case 151: case 152: return 0;
    case 157: if(a1==15){ int n=lx_str(a2,pa,16); if(n<0)return n; int i=0; for(;pa[i]&&i<15;i++)p->cmd[i]=pa[i]; p->cmd[i]=0; return 0; }
        if(a1==16){ char nm[16]; lx_zero(nm,16); for(int i=0;p->cmd[i]&&i<15;i++)nm[i]=p->cmd[i]; return lx_put(a2,nm,16); }
        if(a1==2||a1==37||a1==39){ int z=0; if(a1==39)return 0; return lx_put(a2,&z,4); }
        if(a1==3)return 1;
        if(a1==30)return 50000;
        if(a1==1||a1==4||a1==7||a1==8||a1==21||a1==23||a1==29||a1==36||a1==38||a1==47||a1==52||a1==0x59616d61ULL||a1==0x53564d41ULL)return 0;
        lx_gap(6,a1); return -LX_EINVAL;
    case 158: if(a1==0x1002){ if(a2>=0x800000000000ULL)return -LX_EPERM; p->fsbase=a2; lsys_fsbase=a2; cpu_set_fsbase(a2); return 0; }
        if(a1==0x1003)return lx_put(a2,&p->fsbase,8);
        if(a1==0x1001){ if(a2>=0x800000000000ULL)return -LX_EPERM; p->gsbase=a2; cpu_wrmsr(0xC0000102u,a2); return 0; }
        if(a1==0x1004)return lx_put(a2,&p->gsbase,8);
        if(a1==0x1011)return 1;
        return -LX_EINVAL;
    case 160: return 0;
    case 161: return -LX_EPERM;
    case 162: for(int k=0;k<FS_N;k++)g_fs_dirty[k]=0; nvx3_sync_all(); return 0;
    case 165: { int n=lx_str(a2,pa,LX_PATH); if(n<0)return n; return ux_mount("",pa)?-LX_EPERM:0; }
    case 166: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; return ux_umount(pa)?-LX_EINVAL:0; }
    case 169: return -LX_EPERM;
    case 170: { if(g_uid!=0)return -LX_EPERM; if(a2>31)return -LX_EINVAL; char h[32]; if(lx_get(h,a1,a2))return -LX_EFAULT; h[a2]=0; for(u64 i=0;i<=a2;i++)g_hostname[i]=h[i]; return 0; }
    case 186: return p->pid;
    case 191: case 192: case 193: case 194: case 195: case 196: case 197: case 198: case 199: return -LX_ENOTSUP;
    case 201: { u64 t=lx_now_ns()/1000000000ULL; if(a1&&lx_put(a1,&t,8))return -LX_EFAULT; return (long)t; }
    case 202: return lx_futex(r,a1,(int)a2,(u32)a3,a4,a5,(u32)a6);
    case 217: return lx_getdents((int)a1,a2,a3);
    case 218: p->ctid=a1; return p->pid;
    case 221: return 0;
    case 228: return lx_clock((int)a1,a2);
    case 229: { if(a2){ u64 t[2]={0,hr_precise()?1:10000000}; if(lx_put(a2,t,16))return -LX_EFAULT; } return 0; }
    case 230: return lx_clock_nanosleep(r,(int)a1,(int)a2,a3);
    case 257: return lx_open((int)a1,a2,(int)a3,(int)a4);
    case 258: PATH1((int)a1,a2,0); return lx_mkdir(pa,(int)a3);
    case 260: PATH1((int)a1,a2,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chown(pa,(int)a3,(int)a4,!((int)a5&0x100))?-LX_EPERM:0;
    case 262: { int fl=(int)a4; if(fl&0x1000){ if(!a2)return lx_fstat((int)a1,a3); char t1[4]; int n=lx_str(a2,t1,4); if(n==0)return lx_fstat((int)a1,a3); }
        PATH1((int)a1,a2,0); return lx_stat_path(pa,a3,!(fl&0x100)); }
    case 332: { int fl=(int)a3; long rv; if(!lx_okw(a5,256))return -LX_EFAULT;
        if(fl&~(0x1000|0x100|0x800|0x6000))return -LX_EINVAL;
        int emp=0; if(fl&0x1000){ if(!a2)emp=1; else { char t1[4]; if(lx_str(a2,t1,4)==0)emp=1; } }
        if(emp)rv=lx_fstat((int)a1,a5); else { PATH1((int)a1,a2,0); rv=lx_stat_path(pa,a5,!(fl&0x100)); }
        if(rv)return rv;
        u8 st[144]; if(lx_get(st,a5,144))return -LX_EFAULT;
        u8 x[256]; lx_zero(x,256); u64 dev=*(u64*)(st+0), rdev=*(u64*)(st+40);
        *(u32*)(x+0)=0x7FFu; *(u32*)(x+4)=(u32)*(u64*)(st+56); *(u32*)(x+16)=(u32)*(u64*)(st+16); *(u32*)(x+20)=*(u32*)(st+28); *(u32*)(x+24)=*(u32*)(st+32);
        *(u16*)(x+28)=(u16)*(u32*)(st+24); *(u64*)(x+32)=*(u64*)(st+8); *(u64*)(x+40)=*(u64*)(st+48); *(u64*)(x+48)=*(u64*)(st+64);
        *(u64*)(x+64)=*(u64*)(st+72); *(u32*)(x+72)=(u32)*(u64*)(st+80);
        *(u64*)(x+80)=*(u64*)(st+104); *(u32*)(x+88)=(u32)*(u64*)(st+112);
        *(u64*)(x+96)=*(u64*)(st+104); *(u32*)(x+104)=(u32)*(u64*)(st+112);
        *(u64*)(x+112)=*(u64*)(st+88); *(u32*)(x+120)=(u32)*(u64*)(st+96);
        *(u32*)(x+128)=(u32)((rdev>>8)&0xFFF); *(u32*)(x+132)=(u32)(rdev&0xFF); *(u32*)(x+136)=(u32)((dev>>8)&0xFFF); *(u32*)(x+140)=(u32)(dev&0xFF);
        return lx_put(a5,x,256); }
    case 324: { int cmd=(int)a1; if(a2&&cmd!=0)return -LX_EINVAL;
        if(cmd==0)return 1|2|4|8|16;
        if(cmd==1||cmd==2||cmd==8){ smp_tlb_sync(g_jp[g_jcur].cr3); return 0; }
        if(cmd==4||cmd==16)return 0;
        return -LX_EINVAL; }
    case 263: PATH1((int)a1,a2,0); return ((int)a3&0x200)?lx_rmdir(pa):lx_unlink(pa);
    case 264: case 316: if(nr==316&&a5)return -LX_EINVAL; PATH1((int)a1,a2,0); PATH2((int)a3,a4); return lx_rename(pa,pb);
    case 265: PATH1((int)a1,a2,0); PATH2((int)a3,a4); { int L=lx_slen(pb), c=0; while(L>0&&pb[L-1]!='/'){ L--; c++; } if(c>51)return -LX_ENAMETOOLONG; } if(!lx_exists(pa))return -LX_ENOENT; if(lx_exists(pb))return -LX_EEXIST; return ux_link(pa,pb)?-LX_EPERM:0;
    case 266: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; PATH2((int)a2,a3); if(lx_exists(pb))return -LX_EEXIST; return ux_symlink(pa,pb)?-LX_ENOSPC:0; }
    case 267: { char t[64]; if(lx_str(a2,t,64)>0){ int pe=lx_proc_exe(t,a3,a4); if(pe!=-9999)return pe; } } PATH1((int)a1,a2,0); { if((long)a4<=0)return -LX_EINVAL; if(!lx_okw(a3,a4))return -LX_EFAULT; int n=ux_readlink(pa,(char*)a3,(int)(a4>LX_PATH?LX_PATH:a4)); if(n<0)return lx_exists(pa)?-LX_EINVAL:-LX_ENOENT; return n; }
    case 268: PATH1((int)a1,a2,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chmod(pa,(unsigned)a3)?-LX_EPERM:0;
    case 269: case 439: PATH1((int)a1,a2,0); return lx_access(pa,(int)a3);
    case 270: { long ms=-1; if(a5){ u64 ts[2]; if(lx_get(ts,a5,16))return -LX_EFAULT; if((i64)ts[0]<0||ts[1]>=1000000000ULL)return -LX_EINVAL; if(ts[0]>100000000ULL)ts[0]=100000000ULL; ms=(long)(ts[0]*1000+ts[1]/1000000); } return lx_select(r,(int)a1,a2,a3,a4,ms); }
    case 271: { long ms=-1; if(a3){ u64 ts[2]; if(lx_get(ts,a3,16))return -LX_EFAULT; if((i64)ts[0]<0||ts[1]>=1000000000ULL)return -LX_EINVAL; if(ts[0]>100000000ULL)ts[0]=100000000ULL; ms=(long)(ts[0]*1000+(ts[1]+999999)/1000000); } return lx_poll(r,a1,a2,ms); }
    case 273: if(a2!=24)return -LX_EINVAL; p->robust=a1; return 0;
    case 274: { int t=a1?jc_idx((int)a1):g_jcur; if(t<0)return -LX_ESRCH; u64 h=g_jp[t].robust, L=24; if(lx_put(a2,&h,8)||lx_put(a3,&L,8))return -LX_EFAULT; return 0; }
    case 280: { if(!a2){ struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; return 0; } PATH1((int)a1,a2,0); return lx_utimens(pa,a3); }
    case 285: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if((long)a3<0||(long)a4<=0)return -LX_EINVAL; if(a2&~1ULL)return -LX_ENOTSUP; if(a2&1)return 0; u64 end=a3+a4;
        if(v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; if(end<=m->size)return 0; if(m->seals&4)return -LX_EPERM; return mfd_truncate(v->fs_idx,end); }
        if(v->dev==0&&v->fs_idx>=0){ if((v->flags&3)==0)return -LX_EBADF; if(end<=(u64)FS[v->fs_idx].len)return 0; if(end>=(u64)FS_CAP)return -27; return k_ftruncate((int)a1,(long)end)<0?-LX_EIO:0; }
        return -LX_ENOTSUP; }
    case 292: return lx_dup3((int)a1,(int)a2,(int)a3|0x40000000);
    case 293: return lx_pipe(a1,(int)a2);
    case 302: { if(a1&&(int)a1!=jc_lead(p)->pid&&(int)a1!=p->pid)return -LX_ESRCH; if(a4){ long v=lx_rlimit((int)a2,a4); if(v)return v; } return 0; }
    case 318: return lx_getrandom(a1,a2);
    case 334: return -LX_ENOSYS;
    case 436: { u32 lo=(u32)a1, hi=(u32)a2; if(lo>hi)return -LX_EINVAL; for(u32 f=lo;f<=hi&&f<FDMAX;f++){ if(!lx_vf((int)f))continue; if(a3&4)fdce()[f]=1; else lx_close((int)f); } return 0; }
    }
    enosys_note(nr);
    return -LX_ENOSYS;
    #undef PATH1
    #undef PATH2
}
static long lx_dispatch_ino(struct sysregs* r){
    u64 nr=r->rax, a1=r->rdi, a2=r->rsi, a3=r->rdx, a4=r->r10;
    char pa[LX_PATH], pb[LX_PATH]; int ha=0, hb=0, dirf=0; u32 m=0, mb=0;
    switch(nr){
    case 83: case 84: case 87: case 90: case 92: case 94: case 132: case 76: ha=lx_path(LX_AT_FDCWD,a1,pa,0)>=0; m=nr==83?0x100u:(nr==84||nr==87)?0x200u:nr==76?0x2u:0x4u; dirf=nr==83||nr==84; break;
    case 258: case 268: case 260: case 280: if(nr==280&&!a2)return lx_dispatch(r); ha=lx_path((int)a1,a2,pa,0)>=0; m=nr==258?0x100u:0x4u; dirf=nr==258; break;
    case 263: ha=lx_path((int)a1,a2,pa,0)>=0; m=0x200u; dirf=((int)a3&0x200)!=0; break;
    case 82: ha=lx_path(LX_AT_FDCWD,a1,pa,0)>=0; hb=lx_path(LX_AT_FDCWD,a2,pb,0)>=0; m=0x40u; mb=0x80u; break;
    case 264: case 316: ha=lx_path((int)a1,a2,pa,0)>=0; hb=lx_path((int)a3,a4,pb,0)>=0; m=0x40u; mb=0x80u; break;
    case 86: case 88: hb=lx_path(LX_AT_FDCWD,a2,pb,0)>=0; mb=0x100u; break;
    case 265: hb=lx_path((int)a3,a4,pb,0)>=0; mb=0x100u; break;
    case 266: hb=lx_path((int)a2,a3,pb,0)>=0; mb=0x100u; break;
    case 91: case 93: case 77: { long v=lx_dispatch(r); if(v==0)ino_ev_fd((int)a1,nr==77?0x2u:0x4u); return v; }
    default: return lx_dispatch(r);
    }
    if(ha&&m==0x40u)dirf=lx_isdir(pa);
    long v=lx_dispatch(r);
    if(v==0){ u32 ck=m==0x40u?++g_ino_cookie:0; if(ha&&m)ino_ev(pa,m,ck,dirf); if(hb&&mb)ino_ev(pb,mb,ck,dirf||(mb==0x80u&&lx_isdir(pb))); }
    return v;
}
int g_lxtrace=0;
void linux_syscall(struct sysregs* r){
    if(!g_js_running||g_jcur<0){
        if(r->rax==60||r->rax==231){ r->rbx=r->rdi; r->rax=0; syscall_dispatch(r); return; }
        if(r->rax==1&&(r->rdi==1||r->rdi==2)&&r->rdx<4096&&jc_umapped(r->rsi,r->rdx)){ const char* s=(const char*)r->rsi; u64 n=r->rdx; for(u64 i=0;i<n&&g_r3out_n<255;i++)g_r3out[g_r3out_n++]=s[i]; g_r3out[g_r3out_n]=0; r->rax=n; return; }
        r->rax=(u64)-LX_ENOSYS; return; }
    struct jproc* p=&g_jp[g_jcur]; p->lx=1;
    if(PCPU->tlb_pending){ PCPU->tlb_pending=0; cpu_set_pt(cpu_get_pt()); }
    struct jproc* P=jc_lead(p);
    g_dcwd=P->cwd; g_umask=P->umask; g_ubrk=P->brk; g_mmap_next=P->mmnext;
    u64 tnr=r->rax, ta1=r->rdi, ta2=r->rsi;
    g_cursc[g_jcur]=(u16)tnr; g_cura1[g_jcur]=ta1;
    long v=g_ino_any?lx_dispatch_ino(r):lx_dispatch(r);
    lx_sc_note((u32)tnr,g_jp[g_jcur].pid,ta1,ta2,v);
    if(g_lxtrace>0){ g_lxtrace--; char b[120],*q=b; q=scopy(q,"sc "); q=udec(q,(u32)tnr); q=scopy(q," "); q=uhex(q,ta1,8); q=scopy(q," "); q=uhex(q,ta2,8); q=scopy(q," = "); if(v<0){ *q++='-'; q=udec(q,(u32)(-v)); } else q=uhex(q,(u64)v,8); *q=0; klog(b); }
    p=&g_jp[g_jcur]; P=jc_lead(p);
    P->cwd=g_dcwd; P->umask=g_umask; P->brk=g_ubrk; P->mmnext=g_mmap_next;
    r->rax=(u64)v;
    if(jc_deliverable(p))jc_yield_sys(r);
}
