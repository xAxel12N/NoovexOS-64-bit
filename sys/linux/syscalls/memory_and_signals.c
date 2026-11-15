extern const vfs_fs_ops SYSROOTFS;
u64 g_lx_fmaps, g_lx_fmap_pages;
static long lx_mmap(u64 addr,u64 len,int prot,int flags,int fd,u64 off){
    struct jproc* p=&g_jp[g_jcur]; u64 pb=vm_prot_bits(prot&7);
    if(!len||(off&0xFFF))return -LX_EINVAL;
    if(len>((flags&0x20)?0x10000000000ULL:0x40000000ULL))return -LX_ENOMEM;
    u64 pages=(len+0xFFFULL)>>12;
    int anon=(flags&0x20)!=0;
    struct vfile* v=0;
    if(!anon){ v=lx_vf(fd); if(!v)return -LX_EBADF; if(!((v->dev==0&&v->fs_idx>=0)||(v->dev==100&&v->vfs)||v->dev==LX_MFD))return -LX_ENODEV; }
    if(!anon&&v->dev==LX_MFD&&(g_mfd[v->fs_idx].seals&0x18)&&(flags&3)==1&&(prot&2))return -LX_EPERM;
    int srf=!anon&&v->dev==100&&v->vfs==&SYSROOTFS;
    if(srf&&(flags&3)==1&&(prot&2))return -LX_EACCES;
    if(!anon&&!srf&&v->dev!=LX_MFD&&pages>pframe_free_count())return -LX_ENOMEM;
    if(anon&&pages<=65536&&(pages>>9)+64>pframe_free_count())return -LX_ENOMEM;
    u64 base;
    if(flags&0x10){ if(addr&0xFFF)return -LX_EINVAL; if(!jc_uok(addr,pages<<12))return -LX_ENOMEM; base=addr;
        if(base+(pages<<12)>USTK_V-(u64)(JS_STK_PAGES-1)*0x1000ULL&&base<USTK_V+0x1000ULL)return -LX_EINVAL;
        lx_unmap_range(base,base+(pages<<12)); }
    else { base=lx_find_gap(p->cr3,pages+1); if(!base||!jc_uok(base,(pages+1)<<12))return -LX_ENOMEM;
        if(base+((pages+1)<<12)>g_mmap_next)g_mmap_next=base+((pages+1)<<12); }
    if(v&&v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; u64 pbase=off>>12;
        for(u64 i=0;i<pages;i++){ u64 va=base+(i<<12);
            if((long)(pbase+i)<m->nframes&&m->frames[pbase+i]){ u64 f=m->frames[pbase+i]; pframe_ref(f); if(vm_map(p->cr3,va,f,vm_pflags(((flags&3)==1)?(0x7ULL|PTE_SHR):(0x5ULL|PTE_COW),pb))){ pframe_free(f); lx_unmap_range(base,va); return -LX_ENOMEM; } }
            else if(vm_map_lazy_p(p->cr3,va,pb)){ lx_unmap_range(base,va); return -LX_ENOMEM; } }
        return (long)base; }
    if(srf){ extern int sysroot_pc_handle(const char*,u32*); extern u32 sysroot_pc_pages(int);
        u32 flen=0; int h=sysroot_pc_handle(v->vpath,&flen);
        if(h>=0){ u64 np=sysroot_pc_pages(h), pbase=off>>12; u64 shr=((flags&3)==1)?PTE_SHR:0;
            for(u64 i=0;i<pages;i++){ u64 va=base+(i<<12);
                if(pbase+i<np){ u64* e=vm_walk(p->cr3,va,1); if(!e){ lx_unmap_range(base,va); return -LX_ENOMEM; } *e=vm_file_marker((u32)h,(u32)(pbase+i),pb)|shr; }
                else if(vm_map_lazy_p(p->cr3,va,pb)){ lx_unmap_range(base,va); return -LX_ENOMEM; } }
            { const char* q=v->vpath; for(int z=0;v->vpath[z];z++) if(v->vpath[z]=='/')q=v->vpath+z+1; lx_note_map(p->cr3,base,base+(pages<<12),q); }
            g_lx_fmaps++; g_lx_fmap_pages+=pages; return (long)base; } }
    u8* rb=0; u64 rbo=0, rbn=0; int rbl=0;
    if(v&&v->dev==100&&v->vfs)rb=(u8*)kmalloc(262144);
    if(!v&&(flags&3)!=1&&pages>65536){ if(lx_res_addp(p->cr3,base,base+(pages<<12),pb)==0)return (long)base; }
    for(u64 i=0;i<pages;i++){
        if(!v&&!((flags&3)==1&&pages<=65536)){ if(vm_map_lazy_p(p->cr3,base+(i<<12),pb)){ if(rb)kfree(rb); lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; } continue; }
        u64 f=pframe_alloc(); if(!f||vm_map(p->cr3,base+(i<<12),f,vm_pflags(((flags&3)==1)?(0x7ULL|PTE_SHR):0x7ULL,pb))){ if(f)pframe_free(f); if(rb)kfree(rb); lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; }
        if(v&&v->dev==100){ u64 fo=off+(i<<12); u8* d=(u8*)f; int got;
            if(rb){ if(!rbl||((fo<rbo||fo+4096>rbo+rbn)&&!(rbn<262144&&fo>=rbo))){ rbo=fo; rbl=1; int r=v->vfs->read(v->vpath,(char*)rb,(int)fo,262144); rbn=r>0?(u64)r:0; }
                got=fo<rbo+rbn?(int)(rbo+rbn-fo):0; if(got>4096)got=4096; for(int b=0;b<got;b++)d[b]=rb[fo-rbo+(u64)b]; }
            else { got=v->vfs->read(v->vpath,(char*)f,(int)fo,4096); if(got<0)got=0; }
            for(int b=got;b<4096;b++)d[b]=0; }
        else if(v){ int k=v->fs_idx; u64 fo=off+(i<<12); u8* d=(u8*)f; const char* sd=fsd(k); for(int b=0;b<4096;b++){ u64 q=fo+(u64)b; d[b]=(q<(u64)FS[k].len)?(u8)sd[q]:0; } } }
    if(rb)kfree(rb);
    if(v&&v->dev==0&&v->fs_idx>=0)lx_note_map(p->cr3,base,base+(pages<<12),FS[v->fs_idx].name);
    int xscr=0; if(v&&v->dev==0&&v->fs_idx>=0){ const char* nm=FS[v->fs_idx].name; const char* x="Xvfb_screen"; xscr=1; for(int q=0;x[q];q++) if(nm[q]!=x[q]){ xscr=0; break; } }
    if(v&&v->dev==0&&v->fs_idx>=0&&(flags&3)==1&&!(xscr&&(flags&1)&&off==0&&(prot&2)))lx_fm_add(p->cr3,base,off,v->fs_idx,pages);
    if(v&&v->dev==100&&v->vfs){ const char* q=v->vpath; for(int z=0;v->vpath[z];z++) if(v->vpath[z]=='/')q=v->vpath+z+1; lx_note_map(p->cr3,base,base+(pages<<12),q); }
    if(v&&v->dev==0&&(flags&1)&&off==0){ const char* nm=FS[v->fs_idx].name; const char* x="Xvfb_screen"; int m=1; for(int q=0;x[q];q++) if(nm[q]!=x[q]){ m=0; break; }
        if(m&&pages<=65536&&(prot&2)){ extern void xfb_register(u64* frames,int n); u64* fr=(u64*)kmalloc(sizeof(u64)*pages); if(fr){ for(u64 i=0;i<pages;i++){ u64* e=vm_walk(p->cr3,base+(i<<12),0); fr[i]=e?(*e&0x000FFFFFFFFFF000ULL):0; } xfb_register(fr,(int)pages); kfree(fr); if(prot&2){ extern void xfb_track(u64,u64,int); xfb_track(p->cr3,base,(int)pages); } } } }
    return (long)base;
}
static long lx_munmap(u64 a,u64 len){
    if((a&0xFFF)||!len)return -LX_EINVAL;
    u64 e=(a+len+0xFFFULL)&~0xFFFULL; if(!jc_uok(a,e-a))return -LX_EINVAL;
    if(a<USTK_V+0x1000ULL&&e>USTK_V-(u64)(JS_STK_PAGES-1)*0x1000ULL)return -LX_EINVAL;
    lx_unmap_range(a,e); return 0;
}
static long lx_brk(u64 want){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    u64 cur=p->lxbrk?p->lxbrk:g_ubrk;
    if(want<0x40002000000ULL||want>=0x40004000000ULL)return (long)cur;
    u64 top=(want+0xFFFULL)&~0xFFFULL;
    if(top>g_ubrk){ u64 need=(top-g_ubrk)>>12; if(need>pframe_free_count())return (long)cur;
        for(u64 v=g_ubrk;v<top;v+=0x1000ULL){ u64* e=vm_walk(p->cr3,v,0); if(e&&vm_pte_used(*e))return (long)cur; }
        u64 start=g_ubrk; while(g_ubrk<top){ if(vm_map_lazy(p->cr3,g_ubrk)){ lx_unmap_range(start,g_ubrk); g_ubrk=start; return (long)cur; } g_ubrk+=0x1000ULL; } }
    else if(top<g_ubrk){ lx_unmap_range(top,g_ubrk); g_ubrk=top; }
    p->lxbrk=want; return (long)want;
}

static long lx_sigaction(int s,u64 act,u64 oact,u64 sz){
    struct jproc* p=&g_jp[g_jcur];
    if(s<1||s>64||sz!=8)return -LX_EINVAL;
    u64 na[4];
    if(act&&lx_get(na,act,32))return -LX_EFAULT;
    if(act&&(s==9||s==19))return -LX_EINVAL;
    if(oact){ u64 o[4]; o[0]=p->sighand[s]; o[1]=p->saflags[s]; o[2]=p->sigrest[s]; o[3]=lx_k2l(p->sigamask[s]); if(lx_put(oact,o,32))return -LX_EFAULT; }
    if(act){ p->sighand[s]=na[0]; p->saflags[s]=(u32)na[1]; p->sigrest[s]=(na[1]&0x04000000ULL)?na[2]:0; p->sigamask[s]=lx_l2k(na[3])&~SB_KS;
        int ign=(na[0]==1||(na[0]==0&&sig_default(s)==0)); if(ign){ struct jsinfo dz; while(p->sigpend&SB(s))jc_sig_take(p,s,&dz); }
        for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(q==p||q->state==S_FREE||q->tg!=p->tg)continue;
            q->sighand[s]=p->sighand[s]; q->saflags[s]=p->saflags[s]; q->sigrest[s]=p->sigrest[s]; q->sigamask[s]=p->sigamask[s]; if(ign){ struct jsinfo dz; while(q->sigpend&SB(s))jc_sig_take(q,s,&dz); } } }
    return 0;
}
static long lx_sigprocmask(int how,u64 set,u64 oset,u64 sz){
    struct jproc* p=&g_jp[g_jcur];
    if(sz!=8)return -LX_EINVAL;
    u64 ns=0; if(set&&lx_get(&ns,set,8))return -LX_EFAULT;
    if(set&&(how<0||how>2))return -LX_EINVAL;
    if(oset){ u64 o=lx_k2l(p->sigmask); if(lx_put(oset,&o,8))return -LX_EFAULT; }
    if(set){ u64 k=lx_l2k(ns); if(how==0)p->sigmask|=k; else if(how==1)p->sigmask&=~k; else p->sigmask=k;
        p->sigmask&=~SB_KS; }
    return 0;
}
static void lx_sigreturn(struct sysregs* r) __attribute__((noreturn));
static void lx_sigreturn(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur]; u64 f=((u64*)r)[18];
    if(!lx_ok(f,304)){ jc_finish(g_jcur,11); jc_leave(); }
    const u64* uc=(const u64*)f; const u64* g=uc+5; regs_pt c=p->ctx;
    c.r8=g[0]; c.r9=g[1]; c.r10=g[2]; c.r11=g[3]; c.r12=g[4]; c.r13=g[5]; c.r14=g[6]; c.r15=g[7];
    c.rdi=g[8]; c.rsi=g[9]; c.rbp=g[10]; c.rbx=g[11]; c.rdx=g[12]; c.rax=g[13]; c.rcx=g[14]; c.rsp=g[15]; c.rip=g[16]; c.rflags=g[17];
    u64 fp=g[23]; u64 m=uc[37];
    u64 ssp=uc[2], ssz=uc[4]; int ssf=((const int*)&uc[3])[0];
    if(!jc_uok(c.rip,1)){ jc_finish(g_jcur,11); jc_leave(); }
    if(fp&&!(fp&15)&&lx_ok(fp,512)){ u8 fx[1024] __attribute__((aligned(64))); for(int k=0;k<1024;k++)fx[k]=0; cpu_fpu_save(fx); const u8* q=(const u8*)fp; extern int g_xsave_on; extern u32 g_xsave_size; extern u64 g_xcr0; u32 xs=512;
        if(g_xsave_on&&g_xsave_size>512&&g_xsave_size<=1024&&*(const u32*)(q+464)==0x46505853u&&*(const u32*)(q+480)==g_xsave_size&&lx_ok(fp,g_xsave_size+4)&&*(const u32*)(q+g_xsave_size)==0x46505845u)xs=g_xsave_size;
        for(u32 k=0;k<xs;k++)fx[k]=q[k];
        if(xs>512){ *(u64*)(fx+512)&=g_xcr0; for(int k=520;k<576;k++)fx[k]=0; }
        u32* mx=(u32*)(fx+24); *mx&=0xFFBFu; ((u32*)(fx+28))[0]=0; { extern int g_xsave_on; if(g_xsave_on)*(u64*)(fx+512)|=3; } cpu_fpu_restore(fx); }
    int onalt=p->ss_size&&c.rsp>p->ss_sp&&c.rsp<=p->ss_sp+p->ss_size;
    if(!onalt){ if(ssf&2){ p->ss_sp=0; p->ss_size=0; p->ss_flags=2; } else if(ssz>=2048){ p->ss_sp=ssp; p->ss_size=ssz; p->ss_flags=ssf&(int)0x80000000u; } }
    c.cs=0x1B; c.ss=0x23; c.rflags=(c.rflags&0xCD5)|0x202; p->ctx=c; p->sigmask=lx_l2k(m)&~SB_KS; jc_leave();
}

static long lx_sigaltstack(struct sysregs* r,u64 nss,u64 oss){
    struct jproc* p=&g_jp[g_jcur]; u64 usp=((u64*)r)[18];
    int onalt=p->ss_size&&usp>p->ss_sp&&usp<=p->ss_sp+p->ss_size;
    if(oss){ u64 o[3]; o[0]=p->ss_size?p->ss_sp:0; o[1]=p->ss_size?(u64)(u32)((onalt?1:0)|(p->ss_flags&(int)0x80000000u)):2; o[2]=p->ss_size; if(lx_put(oss,o,24))return -LX_EFAULT; }
    if(nss){ u64 n[3]; if(lx_get(n,nss,24))return -LX_EFAULT; int fl=(int)(u32)n[1];
        if(onalt)return -LX_EPERM;
        if(fl&~(2|(int)0x80000000u))return -LX_EINVAL;
        if(fl&2){ p->ss_sp=0; p->ss_size=0; p->ss_flags=2; return 0; }
        if(n[2]<2048)return -LX_ENOMEM;
        if(!jc_uok(n[0],n[2]))return -LX_EFAULT;
        p->ss_sp=n[0]; p->ss_size=n[2]; p->ss_flags=fl&(int)0x80000000u; }
    return 0;
}
static void lxn_child(int ci,u64 flags);
int lxn_tid_user(int s);
int lxn_in_container(void);
static int lxn_resolve(const char* s,char* out);
static long lx_clone(struct sysregs* r,u64 flags,u64 stack,u64 ptid,u64 ctid,u64 tls){
    if((flags&0x80000ULL)&&tls>=0x800000000000ULL)return -LX_EPERM;
    if(stack&&!jc_uok(stack-8,8))return -LX_EINVAL;
    if(flags&0x10000ULL){
        if((flags&0x900ULL)!=0x900ULL){ lx_gap(7,flags); return -LX_EINVAL; }
        int ci=jc_clone_thread(r,stack,tls,(flags&0x80000ULL)!=0); if(ci<0)return -LX_EAGAIN;
        struct jproc* c=&g_jp[ci]; lxn_child(ci,flags); int tid=lxn_tid_user(ci);
        if(flags&0x200000ULL)c->ctid=ctid;
        if((flags&0x1000000ULL)&&ctid)lx_put(ctid,&tid,4);
        if((flags&0x100000ULL)&&ptid)lx_put(ptid,&tid,4);
        return tid; }
    if(flags&0x800ULL&&!(flags&0x100ULL))return -LX_EINVAL;
    int pid=jc_fork(r); if(pid<0)return -LX_EAGAIN;
    int ci=jc_idx(pid); struct jproc* c=&g_jp[ci]; lxn_child(ci,flags);
    if(stack)c->ctx.rsp=stack;
    if(flags&0x80000ULL)c->fsbase=tls;
    c->pollwake=0; c->susp=0;
    if(flags&0x200000ULL)c->ctid=ctid;
    if((flags&0x100000ULL)&&ptid){ int up2=lxn_tid_user(ci); lx_put(ptid,&up2,4); }
    return pid;
}
static u64 lx_now_ns(void);
static int lx_futex_op(u32* uaddr,int op,u32 arg,u32* oldv){
    if(op&8){ op&=7; arg=1u<<(arg&31); }
    u32 old=__atomic_load_n(uaddr,__ATOMIC_SEQ_CST), nv;
    for(;;){
        switch(op){ case 0: nv=arg; break; case 1: nv=old+arg; break; case 2: nv=old|arg; break; case 3: nv=old&~arg; break; case 4: nv=old^arg; break; default: return -LX_ENOSYS; }
        if(__atomic_compare_exchange_n(uaddr,&old,nv,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))break; }
    *oldv=old; return 0;
}
#define FX_SHARED_TG (-77)
static u64 lx_fx_key(struct jproc* p,int op,u64 ua,int* tg){
    *tg=p->tg; if(op&128)return ua;
    u64* e=vm_walk(p->cr3&~0xFFFULL,ua&~0xFFFULL,0); if(!e||!(*e&1)){ u32 t; if(lx_get(&t,ua,4))return ua; e=vm_walk(p->cr3&~0xFFFULL,ua&~0xFFFULL,0); if(!e||!(*e&1))return ua; }
    *tg=FX_SHARED_TG; return (*e&PTE_ADDR)|(ua&0xFFFULL); }
static long lx_futex(struct sysregs* r,u64 ua,int op,u32 val,u64 ts,u64 ua2,u32 val3){
    struct jproc* p=&g_jp[g_jcur]; int cmd=op&127; int tg=p->tg;
    if(ua&3)return -LX_EINVAL;
    u64 ka=lx_fx_key(p,op,ua,&tg); int tg2=tg; u64 ka2=(cmd==3||cmd==4||cmd==5)&&!(ua2&3)?lx_fx_key(p,op,ua2,&tg2):ua2;
    if(cmd==0||cmd==9){
        u32 bits=(cmd==9)?val3:0xFFFFFFFFu; if(!bits)return -LX_EINVAL;
        if(p->fx_wait){
            if(p->fx_woken){ p->fx_wait=0; p->wdl=0; p->fx_woken=0; return 0; }
            if(p->fx_dead&&(g_ticks>=p->fx_dead||(p->wdl&&hr_ns()>=p->wdl))){ p->fx_wait=0; p->wdl=0; return -110; }
            if(jc_deliverable(p)){ p->fx_wait=0; p->wdl=0; return -LX_EINTR; }
            p->wdl_arm=1; jc_block(r,S_WAIT,p->fx_dead?p->fx_dead:g_ticks+0x7FFFFFFFFFFFULL); }
        u32 cur; if(lx_get(&cur,ua,4))return -LX_EFAULT;
        if(cur!=val)return -LX_EAGAIN;
        u64 dead=0;
        if(ts){ u64 t[2]; if(lx_get(t,ts,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
            if(cmd!=9&&t[0]>100000000ULL)t[0]=100000000ULL;
            if(t[0]>0x3FFFFFFFFULL)t[0]=0x3FFFFFFFFULL;
            u64 ns=t[0]*1000000000ULL+t[1];
            if(cmd==9){ u64 now=(op&256)?lx_now_ns():hr_ns(); ns=ns>now?ns-now:0; if(ns>100000000ULL*1000000000ULL)ns=100000000ULL*1000000000ULL; }
            if(!ns)return -110;
            dead=g_ticks+(ns+9999999ULL)/10000000ULL; p->wdl=hr_precise()?hr_ns()+ns:0; if(p->wdl)dead++; }
        else p->wdl=0;
        lxm_fxv_reset(g_jcur); p->fx_wait=1; p->fx_woken=0; p->fx_tg=tg; p->fx_addr=ka; p->fx_bits=bits; p->fx_dead=dead;
        p->wdl_arm=1; jc_block(r,S_WAIT,dead?dead:g_ticks+0x7FFFFFFFFFFFULL); }
    if(cmd==1||cmd==10){ if(cmd==10&&!val3)return -LX_EINVAL; return jc_futex_wake(tg,ka,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),cmd==10?val3:0xFFFFFFFFu); }
    if(cmd==3||cmd==4){
        if(ua2&3)return -LX_EINVAL;
        if(cmd==4){ u32 cur; if(lx_get(&cur,ua,4))return -LX_EFAULT; if(cur!=val3)return -LX_EAGAIN; }
        return jc_futex_requeue(tg,ka,ka2,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),(int)((u32)ts>0x7FFFFFFFu?0x7FFFFFFF:(u32)ts)); }
    if(cmd==5){
        if(ua2&3)return -LX_EINVAL; if(!jc_umapped_w(ua2,4))return -LX_EFAULT;
        int opk=(int)((val3>>28)&0xF), cmp=(int)((val3>>24)&0xF); int oparg=(int)((val3>>12)&0xFFF), cmparg=(int)(val3&0xFFF);
        if(oparg&0x800)oparg|=~0xFFF; if(cmparg&0x800)cmparg|=~0xFFF;
        u32 old; int e=lx_futex_op((u32*)ua2,opk,(u32)oparg,&old); if(e)return e;
        int n=jc_futex_wake(tg,ka,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),0xFFFFFFFFu);
        int o=(int)old, c=cmparg, hit;
        switch(cmp){ case 0: hit=o==c; break; case 1: hit=o!=c; break; case 2: hit=o<c; break; case 3: hit=o<=c; break; case 4: hit=o>c; break; case 5: hit=o>=c; break; default: return -LX_ENOSYS; }
        if(hit)n+=jc_futex_wake(tg2,ka2,(int)((u32)ts>0x7FFFFFFFu?0x7FFFFFFF:(u32)ts),0xFFFFFFFFu);
        return n; }
    return -LX_ENOSYS;
}
static void lx_norm(char* p){ char out[LX_PATH]; int o=0, i=0;
    while(p[i]){ while(p[i]=='/')i++; if(!p[i])break; int st=i; while(p[i]&&p[i]!='/')i++; int L=i-st;
        if(L==1&&p[st]=='.')continue;
        if(L==2&&p[st]=='.'&&p[st+1]=='.'){ while(o>0&&out[o-1]!='/')o--; if(o>0)o--; continue; }
        if(o+1+L>=LX_PATH-1)break; out[o++]='/'; for(int k=0;k<L;k++)out[o++]=p[st+k]; }
    if(!o)out[o++]='/'; out[o]=0; for(int k=0;k<=o;k++)p[k]=out[k]; }
static void lx_resolve_exe(const char* ab,char* out,int cap){ char cur[LX_PATH]; int k=0; for(;ab[k]&&k<LX_PATH-1;k++)cur[k]=ab[k]; cur[k]=0; lx_norm(cur);
    for(int hop=0;hop<8;hop++){ char t[LX_PATH]; int n=ux_readlink(cur,t,LX_PATH-1); if(n<=0)break; t[n]=0;
        char nx[LX_PATH]; int o=0; if(t[0]!='/'){ int d=0; for(int q=0;cur[q];q++) if(cur[q]=='/')d=q; for(int q=0;q<=d&&o<LX_PATH-1;q++)nx[o++]=cur[q]; }
        for(int q=0;t[q]&&o<LX_PATH-1;q++)nx[o++]=t[q]; nx[o]=0; lx_norm(nx); int q=0; for(;nx[q]&&q<LX_PATH-1;q++)cur[q]=nx[q]; cur[q]=0; }
    int L=0; while(cur[L]&&L<cap-1){ out[L]=cur[L]; L++; } out[L]=0; }
static int lx_proc_exe(const char* pa,u64 ub,u64 cap){ const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -9999;
    const char* q=pa+6; struct jproc* L=jc_lead(&g_jp[g_jcur]);
    const char* sf="self"; const char* ts="thread-self"; int m=1; for(int i=0;sf[i];i++) if(q[i]!=sf[i]){ m=0; break; }
    struct jproc* T=L; int self=1;
    if(m)q+=4; else { m=1; for(int i=0;ts[i];i++) if(q[i]!=ts[i]){ m=0; break; } if(m)q+=11; else { int pid=0,d=0; while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; if(pid>10000000)return -9999; } if(!d)return -9999;
        if(pid!=L->pid&&pid!=g_jp[g_jcur].pid){ int ix=lx_pidx(pid); if(ix<0||g_jp[ix].state==S_ZOMB)return -9999; T=jc_lead(&g_jp[ix]); self=T==L; } } }
    int lcwd=lx_streq(q,"/cwd"), lroot=lx_streq(q,"/root");
    if(!self&&!lcwd&&!lroot&&!lx_streq(q,"/exe"))return -9999;
    if(!self&&g_uid!=0&&T->uid!=g_uid)return -LX_EACCES;
    if(lcwd||lroot){ char cb[LX_PATH]; if(lroot){ cb[0]='/'; cb[1]=0; } else dt_path(self?g_dcwd:T->cwd,cb);
        int n=0; while(cb[n])n++; if((u64)n>cap)n=(int)cap; if(lx_put(ub,cb,(u64)n))return -LX_EFAULT; return n; }
    if(q[0]=='/'&&q[1]=='f'&&q[2]=='d'&&q[3]=='/'){ int n=lx_fdpath(pa); if(n<0)return -9999; char lk[160]; int L2=lx_fdlink(n,lk); if(L2<0)return L2; if((u64)L2>cap)L2=(int)cap; if(lx_put(ub,lk,(u64)L2))return -LX_EFAULT; return L2; }
    if(!(q[0]=='/'&&q[1]=='e'&&q[2]=='x'&&q[3]=='e'&&q[4]==0))return -9999;
    L=T;
    if(!L->exe[0])return -LX_ENOENT; int n=0; while(L->exe[n])n++; if((u64)n>cap)n=(int)cap; if(lx_put(ub,L->exe,(u64)n))return -LX_EFAULT; return n; }
void lx_set_exe(char* out,const char* args){ char w[LX_PATH]; int k=0; while(args[k]==' ')args++; while(args[k]&&args[k]!=' '&&k<LX_PATH-1){ w[k]=args[k]; k++; } w[k]=0; if(!k)return;
    char ab[LX_PATH]; ab[0]=0;
    if(w[0]=='/'){ for(int q=0;q<=k;q++)ab[q]=w[q]; }
    else { int slash=0; for(int q=0;w[q];q++) if(w[q]=='/')slash=1; if(slash)return;
        static const char* const pres[]={"/opt/gnu/bin/","/usr/local/bin/","/usr/bin/","/bin/","/usr/sbin/","/sbin/",0};
        for(int pi=0;pres[pi]&&!ab[0];pi++){ char gp[LX_PATH]; int o=0; const char* pre=pres[pi]; while(*pre)gp[o++]=*pre++; for(int q=0;w[q]&&o<LX_PATH-1;q++)gp[o++]=w[q]; gp[o]=0;
            u32 l=0; int sx=0; if(sysroot_meta(gp,&l,&sx)||lx_exists(gp)){ for(int q=0;q<=o;q++)ab[q]=gp[q]; } } }
    if(ab[0])lx_resolve_exe(ab,out,128); }
static long lx_execve(struct sysregs* r,u64 upath,u64 uargv,u64 uenv){
    char path[LX_PATH]; int n=lx_str(upath,path,LX_PATH); if(n<0)return n; if(!n)return -LX_ENOENT; int exehost=0;
    { const char* e4="/exe"; int m=n>=10&&path[0]=='/'&&path[1]=='p'&&path[2]=='r'&&path[3]=='o'&&path[4]=='c'&&path[5]=='/'; for(int i=0;m&&i<4;i++) if(path[n-4+i]!=e4[i])m=0;
      if(m){ char np[LX_PATH]; for(int i=0;i<n-4;i++)np[i]=path[i]; np[n-4]=0; int ix; if(lx_procdir(np,&ix)==2){ struct jproc* L=jc_lead(&g_jp[ix]); if(!L->exe[0])return -LX_ENOENT; int k=0; for(;L->exe[k]&&k<LX_PATH-1;k++)path[k]=L->exe[k]; path[k]=0; n=k; exehost=1; } } }
    if(n>63)return -LX_ENAMETOOLONG;
    int used=0; g_xa_n=0;
    if(uargv) for(int i=0;;i++){ u64 sp; if(lx_get(&sp,uargv+(u64)i*8,8))return -LX_EFAULT; if(!sp)break;
        if(g_xa_n>=XA_MAX)return -LX_E2BIG; int L=lx_str(sp,g_xa_buf+used,XA_CAP-used); if(L<0)return L==-LX_ENAMETOOLONG?-LX_E2BIG:L;
        g_xa_off[g_xa_n++]=used; used+=L+1; }
    if(!g_xa_n){ for(int i=0;i<=n;i++)g_xa_buf[i]=path[i]; g_xa_off[0]=0; g_xa_n=1; }
    used=0; g_xe_n=0;
    if(uenv) for(int i=0;;i++){ u64 sp; if(lx_get(&sp,uenv+(u64)i*8,8))return -LX_EFAULT; if(!sp)break;
        if(g_xe_n>=XA_MAX)return -LX_E2BIG; int L=lx_str(sp,g_xe_buf+used,XA_CAP-used); if(L<0)return L==-LX_ENAMETOOLONG?-LX_E2BIG:L;
        g_xe_off[g_xe_n++]=used; used+=L+1; }
    for(int i=0;i<n;i++) if(path[i]==' ')return -LX_ENOENT;
    int inct=lxn_on(), mapped=exehost; char vpath[LX_PATH]; vpath[0]=0;
    for(int hop=0;hop<3;hop++){ if(inct&&!mapped){ char hp[LX_PATH]; lxn_cpy(vpath,path,LX_PATH); if(lxn_resolve(path,hp)<0)return -LX_ENOENT; int q=0; for(;hp[q]&&q<LX_PATH-1;q++)path[q]=hp[q]; path[q]=0; n=q; mapped=1; }
        const u8* hd=0; u32 hl=0; hd=sysroot_get(path,&hl,0);
        if(!hd){ int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd){ hd=(const u8*)fsd(f); hl=(u32)FS[f].len; } }
        if(!hd||hl<3||hd[0]!='#'||hd[1]!='!')break;
        char ip[64], ia[64]; int a=2, il=0, al=0; while(a<(int)hl&&a<200&&(hd[a]==' '||hd[a]=='\t'))a++;
        while(a<(int)hl&&a<200&&hd[a]!=' '&&hd[a]!='\t'&&hd[a]!='\n'&&hd[a]!='\r'&&il<63)ip[il++]=(char)hd[a++]; ip[il]=0;
        while(a<(int)hl&&a<200&&(hd[a]==' '||hd[a]=='\t'))a++;
        while(a<(int)hl&&a<200&&hd[a]!='\n'&&hd[a]!='\r'&&al<63)ia[al++]=(char)hd[a++]; while(al>0&&(ia[al-1]==' '||ia[al-1]=='\t'))al--; ia[al]=0;
        if(!il)return -LX_ENOEXEC;
        static char nb[XA_CAP]; static int no[XA_MAX]; int nu=0, nn=0;
        const char* pre[3]={ip,al?ia:0,(inct&&vpath[0])?vpath:path};
        for(int q=0;q<3;q++){ if(!pre[q])continue; if(nn>=XA_MAX)return -LX_E2BIG; int L=0; while(pre[q][L])L++; if(nu+L+1>XA_CAP)return -LX_E2BIG; no[nn++]=nu; for(int z=0;z<=L;z++)nb[nu+z]=pre[q][z]; nu+=L+1; }
        for(int q=1;q<g_xa_n;q++){ const char* sa=g_xa_buf+g_xa_off[q]; int L=0; while(sa[L])L++; if(nn>=XA_MAX||nu+L+1>XA_CAP)return -LX_E2BIG; no[nn++]=nu; for(int z=0;z<=L;z++)nb[nu+z]=sa[z]; nu+=L+1; }
        for(int q=0;q<nu;q++)g_xa_buf[q]=nb[q]; for(int q=0;q<nn;q++)g_xa_off[q]=no[q]; g_xa_n=nn;
        for(int q=0;q<=il;q++)path[q]=ip[q]; n=il; mapped=0; }
    if(inct&&!mapped){ char hp[LX_PATH]; if(lxn_resolve(path,hp)<0)return -LX_ENOENT; int q=0; for(;hp[q]&&q<LX_PATH-1;q++)path[q]=hp[q]; path[q]=0; n=q; }
    { int b=n; while(b>0&&path[b-1]!='/')b--; const char* bn=path+b; static const char* cn[]={"chrome","chromium","chromium-browser","chromium-bin","brave","msedge","vivaldi-bin","opera",0};
      int hit=0; for(int q=0;cn[q]&&!hit;q++){ const char* c=cn[q]; int z=0; while(c[z]&&bn[z]==c[z])z++; if(!c[z]&&!bn[z])hit=1; }
      if(!hit&&b>0&&b<LX_PATH-40){ int bad=0; for(int q=0;bn[q]&&!bad;q++){ const char* w1="crashpad"; const char* w2="sandbox"; int z=0; while(w1[z]&&bn[q+z]==w1[z])z++; if(!w1[z])bad=1; z=0; while(w2[z]&&bn[q+z]==w2[z])z++; if(!w2[z])bad=1; }
        static char vp[LX_PATH]; static const char* need[]={"v8_context_snapshot.bin","resources.pak",0}; static const char* any[]={"resources/app.asar","resources/app",0};
        int ok=!bad; for(int k=0;ok&&need[k];k++){ int z=0; for(int q=0;q<b;q++)vp[q]=path[q]; while(need[k][z]){ vp[b+z]=need[k][z]; z++; } vp[b+z]=0; if(!lx_exists(vp))ok=0; }
        if(ok){ ok=0; for(int k=0;!ok&&any[k];k++){ int z=0; for(int q=0;q<b;q++)vp[q]=path[q]; while(any[k][z]){ vp[b+z]=any[k][z]; z++; } vp[b+z]=0; if(lx_exists(vp))ok=1; } }
        if(ok)hit=1; }
      for(int q=1;hit&&q<g_xa_n;q++){ const char* sa=g_xa_buf+g_xa_off[q]; const char* t1="--no-sandbox"; const char* t2="--type="; int z=0; while(t1[z]&&sa[z]==t1[z])z++; if(!t1[z]&&!sa[z])hit=0; z=0; while(t2[z]&&sa[z]==t2[z])z++; if(!t2[z])hit=0; }
      if(hit&&g_xa_n<XA_MAX&&g_xa_n>=1){ int u=g_xa_off[g_xa_n-1]; while(g_xa_buf[u])u++; u++; const char* t1="--no-sandbox"; if(u+13<=XA_CAP){ for(int z=0;z<13;z++)g_xa_buf[u+z]=t1[z]; for(int q=g_xa_n;q>1;q--)g_xa_off[q]=g_xa_off[q-1]; g_xa_off[1]=u; g_xa_n++; } } }
    u32 L0; if(!jc_find_prog(path,&L0)){ if(lx_isdir(path))return -LX_EACCES; return lx_exists(path)?-LX_EACCES:-LX_ENOENT; }
    { char ab[LX_PATH]; ab[0]=0; if(path[0]=='/'){ int k=0; for(;path[k]&&k<LX_PATH-1;k++)ab[k]=path[k]; ab[k]=0; } else if(lx_path(LX_AT_FDCWD,upath,ab,0)<0)ab[0]=0;
      if(ab[0])lx_resolve_exe(ab,jc_lead(&g_jp[g_jcur])->exe,128); }
    g_xa_set=1; g_xe_set=1;
    jc_exec(r,path);
    g_xa_set=0; g_xe_set=0;
    return -LX_ENOEXEC;
}
static long lx_wait4(struct sysregs* r,int pid,u64 st,int opts,u64 ru){
    if(st&&!lx_okw(st,4))return -LX_EFAULT;
    if(ru){ if(!lx_okw(ru,144))return -LX_EFAULT; lx_zero((void*)ru,144); }
    int v=jc_waitpid(r,pid,st,opts&0xB);
    return v<0?-LX_ECHILD:v;
}

static u64 g_lx_epoch0;
static u64 lx_now_ns(void){ if(!g_lx_epoch0){ u64 h=hr_ns(); g_lx_epoch0=k_realtime_ms()*1000000ULL-h; if(!g_lx_epoch0)g_lx_epoch0=1; } return g_lx_epoch0+hr_ns(); }
u64 lx_epoch_ns(void){ lx_now_ns(); return g_lx_epoch0; }
static long lx_clock(int clk,u64 ts){
    u64 ns;
    if(clk==0||clk==5||clk==8||clk==11){ ns=lx_now_ns(); }
    else if(clk==1||clk==4||clk==6||clk==7||clk==9||clk==2||clk==3)ns=hr_ns();
    else return -LX_EINVAL;
    u64 t[2]={ns/1000000000ULL,ns%1000000000ULL}; return lx_put(ts,t,16);
}
static long lx_sleep_ns(struct sysregs* r,u64 ns){
    u64 ticks=(ns+9999999ULL)/10000000ULL;
    int pr=hr_precise();
    if(!ticks||(pr&&ns<20000ULL)){ if(!pr||ns<20000ULL){ r->rax=0; jc_yield_sys(r); } }
    g_jp[g_jcur].wdl=pr?hr_ns()+ns:0; g_jp[g_jcur].wdl_arm=1;
    r->rax=0; jc_block(r,S_SLEEP,g_ticks+ticks+(pr?1:0));
}
static long lx_nanosleep(struct sysregs* r,u64 req){
    u64 t[2]; if(lx_get(t,req,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
    if(t[0]>100000000ULL)t[0]=100000000ULL;
    return lx_sleep_ns(r,t[0]*1000000000ULL+t[1]);
}
static long lx_clock_nanosleep(struct sysregs* r,int clk,int fl,u64 req){
    u64 t[2]; if(lx_get(t,req,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
    if(fl&1){ if(t[0]>0x3FFFFFFFFULL)t[0]=0x3FFFFFFFFULL; u64 want=t[0]*1000000000ULL+t[1]; u64 now=(clk==0||clk==11)?lx_now_ns():hr_ns(); if(want<=now)return 0; want-=now; if(want>100000000ULL*1000000000ULL)want=100000000ULL*1000000000ULL; return lx_sleep_ns(r,want); }
    if(t[0]>100000000ULL)t[0]=100000000ULL;
    u64 want=t[0]*1000000000ULL+t[1];
    return lx_sleep_ns(r,want);
}

static void lx_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static long lx_uname(u64 buf){
    char u[390]; lx_zero(u,390);
    lx_cpy(u,"NoovexOS",65); lx_cpy(u+65,g_hostname,65); lx_cpy(u+130,"11.10",65); lx_cpy(u+195,"NoovexOS 64 v11.10",65); lx_cpy(u+260,"x86_64",65); lx_cpy(u+325,"(none)",65);
    return lx_put(buf,u,390);
}
static long lx_rlimit_of(struct jproc* L,int res,u64 out){
    if(!out)return 0;
    u64 v[2]={~0ULL,~0ULL};
    if(res==3){ v[0]=(u64)JS_STK_PAGES*4096ULL; v[1]=v[0]; }
    else if(res==7){ v[0]=(u64)lx_nof_cur(L); v[1]=(u64)lx_nof_max(L); }
    else if(res<0||res>15)return -LX_EINVAL;
    return lx_put(out,v,16);
}
static long lx_rlimit(int res,u64 out){ return lx_rlimit_of(jc_lead(&g_jp[g_jcur]),res,out); }
static long lx_setrlimit_of(struct jproc* L,int res,u64 in){
    if(res<0||res>15||!in)return res<0||res>15?-LX_EINVAL:-LX_EFAULT;
    u64 v[2]; if(lx_get(v,in,16))return -LX_EFAULT; if(v[0]>v[1])return -LX_EINVAL;
    if(res!=7)return 0;
    if(v[0]>(u64)FDMAX)v[0]=FDMAX;
    if(v[1]>(u64)FDMAX)v[1]=FDMAX;
    if(v[1]>(u64)lx_nof_max(L)&&g_uid!=0)return -LX_EPERM;
    L->nofc=(int)v[0]+1; L->nofm=(int)v[1]+1; return 0;
}
static long lx_access(const char* p,int mode){
    { int st=lx_shm_name(p); if(st==0)return 0; if(st>0)return lx_shm_find(p+st)>=0?0:-LX_ENOENT; }
    { int ix; if(lx_procdir(p,&ix))return (mode&2)?-LX_EACCES:0; }
    { u32 m=0; u64 sz=0, rd=0; int r=sx_stat(p,1,&m,&sz); if(!r&&p[0]=='/'&&p[1]=='d'&&p[2]=='e'&&p[3]=='v'&&p[4]=='/')r=dx_stat(p,&m,&rd,&sz); if(r<0)return -LX_ENOENT; if(r>0){ if(g_uid!=0&&((mode&2)||((m&0170000u)==0060000u&&(mode&4))))return -LX_EACCES; return ((mode&2)&&!(m&0200))?-LX_EACCES:0; } }
    { int L=lx_slen(p); const char* t1="/oom_score_adj"; const char* t2="/oom_adj"; int m1=L>14, m2=L>8; for(int i=0;m1&&i<14;i++) if(p[L-14+i]!=t1[i])m1=0; for(int i=0;m2&&i<8;i++) if(p[L-8+i]!=t2[i])m2=0;
      if((m1||m2)&&p[0]=='/'&&p[1]=='p'&&p[2]=='r'&&p[3]=='o'&&p[4]=='c'&&p[5]=='/'){ char np[LX_PATH]; int k=L-(m1?14:8); for(int i=0;i<k;i++)np[i]=p[i]; np[k]=0; int ix; return lx_procdir(np,&ix)==2?0:-LX_ENOENT; } }
    u32 o[10]; if(ux_stat2(p,o,1)){ extern int procfs_size(const char*); if(p[0]=='/'&&p[1]=='p'&&p[2]=='r'&&p[3]=='o'&&p[4]=='c'&&p[5]=='/'&&procfs_size(p)>=0)return (mode&3)?-LX_EACCES:0; { static char eb[4096]; if(lx_proc_extra_gen(p,eb,4096)>=0)return (mode&3)?-LX_EACCES:0; } return lx_noent(p); }
    if((mode&2)&&sysroot_isdir(p)&&dt_resolve_dir(p)<0)return -30;
    if(mode==0||g_uid==0){ if((mode&1)&&!(o[0]&0111)&&!o[9])return -LX_EACCES; return 0; }
    u32 bits=(o[1]==(u32)g_uid)?(o[0]>>6)&7:(o[0]&7);
    if((mode&4)&&!(bits&4))return -LX_EACCES; if((mode&2)&&!(bits&2))return -LX_EACCES; if((mode&1)&&!(bits&1))return -LX_EACCES;
    return 0;
}
