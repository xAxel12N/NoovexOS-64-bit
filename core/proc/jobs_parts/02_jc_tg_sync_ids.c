void jc_tg_sync_ids(int lead){ struct jproc* l=&g_jp[lead]; for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(j==lead||q->state==S_FREE||q->state==S_ZOMB||q->tg!=lead)continue; q->pgid=l->pgid; q->sid=l->sid; q->ppid=l->ppid; } }
static void jc_wake_parent(int ppid){ int pi=jc_idx(ppid); if(pi<0)return; int L=g_jp[pi].tg;
    for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(q->state==S_WAIT&&q->tg==L&&!q->fx_wait)q->wake=0; }
    jc_signal_proc(pi,17); }
static void jc_close_all(struct jproc* p){ for(int f=0;f<FDMAX;f++) if(p->fd[f]>=0){ vf_unref(p->fd[f]); p->fd[f]=-1; } }
int jc_futex_wake(int tg,u64 addr,int n,u32 bits){
    int c=0; for(int j=0;j<JMAX&&c<n;j++){ struct jproc* q=&g_jp[j]; if(q->state==S_FREE||q->state==S_ZOMB)continue;
        if(q->fx_wait&&!q->fx_woken&&q->fx_tg==tg&&q->fx_addr==addr&&(q->fx_bits&bits)){ q->fx_woken=1; q->wake=0; c++; } }
    if(c)smp_kick_idle();
    return c;
}
int jc_futex_requeue(int tg,u64 a1,u64 a2,int nwake,int nmove){
    int c=jc_futex_wake(tg,a1,nwake,0xFFFFFFFFu), m=0;
    for(int j=0;j<JMAX&&m<nmove;j++){ struct jproc* q=&g_jp[j]; if(q->state==S_FREE||q->state==S_ZOMB)continue;
        if(q->fx_wait&&!q->fx_woken&&q->fx_tg==tg&&q->fx_addr==a1){ q->fx_addr=a2; m++; } }
    return c+m;
}
static void jc_clear_ctid(struct jproc* p){
    if(!p->ctid||!p->cr3)return; u64 a=p->ctid; p->ctid=0;
    u64 cur=cpu_get_pt(); int sw=(cur&~0xFFFULL)!=(p->cr3&~0xFFFULL); if(sw)cpu_set_pt(p->cr3);
    u64 pk=0; if(jc_umapped_w(a,4)){ *(volatile int*)a=0; u64* e=vm_walk(p->cr3&~0xFFFULL,a&~0xFFFULL,0); if(e&&(*e&1))pk=(*e&0x000FFFFFFFFFF000ULL)|(a&0xFFFULL); }
    if(sw)cpu_set_pt(cur);
    jc_futex_wake(p->tg,a,1,0xFFFFFFFFu);
    if(pk)jc_futex_wake(-77,pk,0x7FFFFFFF,0xFFFFFFFFu);
}
static void jc_thread_drop(int i){
    struct jproc* p=&g_jp[i];
    jc_clear_ctid(p); p->fx_wait=0; p->fx_woken=0; p->tkill=0; p->sigpend=0;
    p->state=(i==p->tg)?S_TDEAD:S_FREE; g_js_dirty=1;
}
void jc_finish(int i,int status){
    struct jproc* p=&g_jp[i]; int L=p->tg; struct jproc* l=&g_jp[L];
    if(jc_tg_live(L,i)>0){
        if(!l->gexit){ l->gexit=1; l->exitst=status; for(int j=0;j<JMAX;j++) if(j!=i&&jc_tg_member(j,L))jc_signal_thread(j,9); }
        jc_thread_drop(i); return; }
    if(l->gexit)status=l->exitst;
    l->gexit=0;
    if(i!=L){ p->state=S_FREE; p->fx_wait=0; p->cr3=0; }
    jc_close_all(l);
    u64 kcr3=g_kern_cr3; u64 cur; cur=cpu_get_pt();
    if((cur&~0xFFFULL)==(l->cr3&~0xFFFULL)) cpu_set_pt(kcr3);
    vm_free_user(l->cr3); l->cr3=0;
    l->exitst=status; l->state=S_ZOMB; l->fx_wait=0; l->it_next=0; l->it_int=0;
    { int np=(jc_idx(1)>=0&&l->pid!=1)?1:0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].ppid==l->pid&&g_jp[j].tg==j){ g_jp[j].ppid=np; jc_tg_sync_ids(j); } }
    jc_wake_parent(l->ppid); g_js_dirty=1;
}
void jc_thread_exit(int i,int status){
    struct jproc* p=&g_jp[i];
    if(!g_jp[p->tg].gexit&&jc_tg_live(p->tg,i)>0){ jc_thread_drop(i); return; }
    jc_finish(i,status);
}
static void jc_stop(int i,int sig){
    struct jproc* p=&g_jp[i]; int L=p->tg; struct jproc* l=&g_jp[L]; int first=1;
    for(int j=0;j<JMAX;j++){ if(j==i||!jc_tg_member(j,L))continue; struct jproc* q=&g_jp[j];
        if(q->state==S_STOP){ first=0; continue; }
        q->sigpend|=(1u<<19); if(q->runcpu&&q->runcpu-1!=cpu_id())smp_kick(q->runcpu-1); }
    p->state=S_STOP;
    if(first){ l->exitst=(sig<<8)|0x7F; l->stopnote=1; jc_wake_parent(l->ppid); }
    g_js_dirty=1;
}
static void jc_sig_one(int i,int sig){
    struct jproc* p=&g_jp[i];
    if(sig==18){ if(p->state==S_STOP){ struct jproc* l=jc_lead(p); p->state=S_RUN; l->stopnote=2; jc_wake_parent(l->ppid); }
        p->sigpend&=~((1u<<19)|(1u<<20)|(1u<<21)|(1u<<22)); if(p->sighand[18]>1)p->sigpend|=(1u<<18); return; }
    if(sig==19||sig==20||sig==21||sig==22) p->sigpend&=~(1u<<18);
    if(sig!=9&&sig!=19&&(p->sighand[sig]==1||(p->sighand[sig]==0&&sig_default(sig)==0)))return;
    p->sigpend|=(1u<<sig);
    if(sig==9&&p->state==S_STOP)p->state=S_RUN;
    if(p->runcpu&&p->runcpu-1!=cpu_id())smp_kick(p->runcpu-1);
}
void jc_signal_thread(int i,int sig){
    if(i<0||i>=JMAX||sig<=0||sig>=32)return; struct jproc* p=&g_jp[i];
    if(p->state==S_FREE||p->state==S_ZOMB||p->state==S_TDEAD)return;
    jc_sig_one(i,sig);
}
void jc_signal_proc(int i,int sig){
    if(i<0||i>=JMAX||sig<=0||sig>=32)return; struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB)return;
    int L=p->tg;
    if(sig==9||sig==18){ for(int j=0;j<JMAX;j++) if(jc_tg_member(j,L))jc_sig_one(j,sig); return; }
    int t=i;
    if(p->state==S_TDEAD||(p->sigmask&(1u<<sig))){
        int alt=-1; for(int j=0;j<JMAX;j++){ if(!jc_tg_member(j,L))continue; if(!(g_jp[j].sigmask&(1u<<sig))){ alt=j; break; } if(alt<0&&p->state==S_TDEAD)alt=j; }
        if(alt<0){ if(p->state==S_TDEAD)return; } else t=alt; }
    jc_sig_one(t,sig);
}
int g_kill_check=0, g_kill_denied=0;
int jc_kill(int pid,int sig,int self){
    int hit=0; g_kill_denied=0;
    for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB)continue;
        if(p->tg!=i&&!(pid>0&&p->pid==pid))continue;
        int m=0; if(pid>0)m=(p->pid==pid); else if(pid==0)m=(self>=0&&p->pgid==jc_lead(&g_jp[self])->pgid); else if(pid==-1)m=(self<0||i!=g_jp[self].tg); else m=(p->pgid==-pid);
        if(!m)continue; if(g_kill_check&&g_uid!=0&&p->uid!=g_uid){ g_kill_denied++; continue; } hit++; if(sig)jc_signal_proc(i,sig); }
    return hit?0:-1;
}
u32 jc_deliverable(struct jproc* p){ return (p->sigpend&~p->sigmask)|(p->sigpend&((1u<<9)|(1u<<19))); }
static u64 jc_l2k_mask(u64 m){ return m<<1; }
static int jc_push_lx(int i,int s){
    struct jproc* p=&g_jp[i];
    u32 fl=p->saflags[s];
    if(!jc_uok(p->sighand[s],1))return -1;
    u64 usp=p->ctx.rsp; int onalt=p->ss_size&&usp>p->ss_sp&&usp<=p->ss_sp+p->ss_size;
    u64 sp=usp-128;
    if((fl&0x08000000u)&&p->ss_size&&!(p->ss_flags&2)&&!onalt){ sp=p->ss_sp+p->ss_size; onalt=1; if(p->ss_flags&(int)0x80000000u){ p->ss_size=0; p->ss_sp=0; p->ss_flags=2; } }
    u64 top=sp;
    sp-=512; sp&=~63ULL; u64 fpst=sp;
    sp-=8+304+128; sp&=~15ULL; sp-=8; u64 frame=sp;
    if(!jc_uok(sp,top-sp))return -1;
    cpu_set_pt(p->cr3);
    if(!jc_umapped_w(sp,top-sp)){ cpu_set_pt(g_kern_cr3); return -1; }
    u8* fb=(u8*)frame; for(u64 k=0;k<top-sp;k++)fb[k]=0;
    for(int k=0;k<512;k++)((u8*)fpst)[k]=p->fx[k];
    u64* pr=(u64*)frame; pr[0]=p->sigrest[s];
    u64* uc=(u64*)(frame+8);
    uc[0]=0; uc[1]=0; uc[2]=p->ss_sp; ((int*)&uc[3])[0]=p->ss_size?(onalt?1:0):2; uc[4]=p->ss_size;
    u64* g=uc+5; regs_pt* c=&p->ctx;
    g[0]=c->r8; g[1]=c->r9; g[2]=c->r10; g[3]=c->r11; g[4]=c->r12; g[5]=c->r13; g[6]=c->r14; g[7]=c->r15;
    g[8]=c->rdi; g[9]=c->rsi; g[10]=c->rbp; g[11]=c->rbx; g[12]=c->rdx; g[13]=c->rax; g[14]=c->rcx; g[15]=c->rsp; g[16]=c->rip; g[17]=c->rflags;
    g[18]=(c->cs&0xFFFF)|((u64)(c->ss&0xFFFF)<<48);
    int flt=(s==p->flt_sig); u64 oldm=p->susp?p->suspold:p->sigmask;
    g[19]=flt?p->flt_err:0; g[20]=flt?(u64)p->flt_trap:0; g[21]=oldm>>1; g[22]=(flt&&p->flt_trap==14)?p->flt_addr:0; g[23]=fpst;
    uc[37]=oldm>>1;
    int* si=(int*)(frame+8+304); si[0]=s; si[1]=0;
    if(flt){ si[2]=p->flt_code; *(u64*)(si+4)=p->flt_addr; p->flt_sig=0; }
    else { si[2]=p->sicode[s]; p->sicode[s]=0; }
    cpu_set_pt(g_kern_cr3);
    if(p->susp){ p->sigmask=p->suspold; p->susp=0; }
    p->fx_wait=0; p->fx_woken=0;
    p->ctx.rip=p->sighand[s]; p->ctx.rsp=frame; p->ctx.rdi=(u64)s; p->ctx.rsi=frame+8+304; p->ctx.rdx=frame+8; p->ctx.rax=0; p->ctx.rflags&=~0x400ULL;
    if(!(fl&0x40000000u))p->sigmask|=(1u<<s); p->sigmask|=p->sigamask[s]; p->sigmask&=~((1u<<9)|(1u<<19));
    if(fl&0x80000000u){ p->sighand[s]=0; p->saflags[s]&=~4u; }
    (void)jc_l2k_mask;
    return 0;
}
static int jc_push_handler(int i,int s){
    struct jproc* p=&g_jp[i];
    if(p->lx)return jc_push_lx(i,s);
    u64 sp=p->ctx.rsp; u64 top=sp; if(!jc_uok(sp-1024,1024))return -1;
    if(!jc_uok(p->sighand[s],1))return -1;
    int si=(p->saflags[s]&4)!=0;
    sp-=128; if(si)sp-=128; u64 info=sp; sp-=sizeof(regs_pt)+16; sp&=~0xFULL; u64 frame=sp; sp-=8;
    cpu_set_pt(p->cr3);
    if(!jc_umapped_w(sp,top-sp)){ cpu_set_pt(g_kern_cr3); return -1; }
    regs_pt* sv=(regs_pt*)frame; *sv=p->ctx; *(u64*)(frame+sizeof(regs_pt))=p->susp?p->suspold:p->sigmask; *(u64*)sp=p->sigrest[s];
    if(p->susp){ p->sigmask=p->suspold; p->susp=0; }
    p->fx_wait=0; p->fx_woken=0;
    if(si){ for(int k=0;k<32;k++)((u32*)info)[k]=0; ((u32*)info)[0]=(u32)s; }
    cpu_set_pt(g_kern_cr3);
    p->ctx.rip=p->sighand[s]; p->ctx.rsp=sp; p->ctx.rdi=(u64)s; p->ctx.rsi=si?info:0; p->ctx.rdx=0; p->ctx.rax=0;
    p->sigmask|=(1u<<s)|p->sigamask[s]; p->sigmask&=~((1u<<9)|(1u<<19));
    return 0;
}
static void jc_deliver(int i){
    struct jproc* p=&g_jp[i];
    for(int guard=0;guard<32;guard++){
        u32 d=jc_deliverable(p); if(!d)return;
        int s=0; while(!(d&(1u<<s)))s++; p->sigpend&=~(1u<<s);
        if(s==9){ if(p->tkill){ jc_thread_drop(i); return; } jc_finish(i,9); return; }
        if(s==19){ jc_stop(i,19); return; }
        u64 h=p->sighand[s];
        if(h==1)continue;
        if(h==0){ int a=sig_default(s); if(a==0||a==3)continue; if(a==1){ jc_finish(i,s); return; } jc_stop(i,s); return; }
        if(p->state==S_WAIT||p->state==S_SLEEP)p->state=S_RUN;
        if(jc_push_handler(i,s)){ jc_finish(i,11); }
        return;
    }
}

static void jc_save_sys(struct sysregs* r,regs_pt* c){
    c->r15=r->r15; c->r14=r->r14; c->r13=r->r13; c->r12=r->r12; c->r11=r->r11; c->r10=r->r10; c->r9=r->r9; c->r8=r->r8;
    c->rbp=r->rbp; c->rdi=r->rdi; c->rsi=r->rsi; c->rdx=r->rdx; c->rcx=r->rcx; c->rbx=r->rbx; c->rax=r->rax;
    u64* fr=(u64*)r; c->rip=fr[15]; c->cs=fr[16]; c->rflags=fr[17]; c->rsp=fr[18]; c->ss=fr[19]; c->vector=0; c->errcode=0;
}

void jc_leave(void){ if(g_jcur>=0)cpu_fpu_save(g_jp[g_jcur].fx); cpu_set_pt(g_kern_cr3); k_longjmp(g_js_kern,1); for(;;){} }

void jc_block(struct sysregs* r,int state,u64 wake){
    struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx);
    if(state==S_WAIT)p->ctx.rip-=2;
    p->state=state; p->wake=wake; jc_leave();
}

void jc_yield_sys(struct sysregs* r){ struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx); jc_leave(); }

int jc_waitpid(struct sysregs* r,int pid,u64 stp,int opts){
    struct jproc* me=jc_lead(&g_jp[g_jcur]); int any=0;
    for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(q->state==S_FREE||q->ppid!=me->pid||q->tg!=j)continue;
        int m=(pid>0)?(q->pid==pid):(pid==0)?(q->pgid==me->pgid):(pid==-1)?1:(q->pgid==-pid); if(!m)continue; any=1;
        if(q->state==S_ZOMB){ int st=q->exitst, qp=q->pid; if(stp&&jc_umapped_w(stp,4))*(int*)stp=st; q->state=S_FREE; return qp; }
        if((opts&2)&&q->stopnote==1){ q->stopnote=0; if(stp&&jc_umapped_w(stp,4))*(int*)stp=q->exitst; return q->pid; }
        if((opts&8)&&q->stopnote==2){ q->stopnote=0; if(stp&&jc_umapped_w(stp,4))*(int*)stp=0xFFFF; return q->pid; } }
    if(!any)return -1;
    if(opts&1)return 0;
    jc_block(r,S_WAIT,g_ticks+1); return 0;
}
int jc_fork(struct sysregs* r){
    int i=jc_alloc(); if(i<0)return -1; struct jproc* par=&g_jp[g_jcur]; struct jproc* L=jc_lead(par);
    u64 cr3=vm_clone_user(par->cr3); if(!cr3)return -1;
    struct jproc* c=&g_jp[i]; *c=*par;
    cpu_fpu_save(c->fx);
    for(int f=0;f<FDMAX;f++){ c->fd[f]=L->fd[f]; c->cloexec[f]=L->cloexec[f]; }
    c->tg=i; c->gexit=0; c->tkill=0; c->ctid=0; c->robust=0; c->fx_wait=0; c->fx_woken=0; c->pgid=L->pgid; c->sid=L->sid;
    c->pid=g_next_pid++; c->tgid=c->pid; c->ppid=L->pid; c->cr3=cr3; c->cwd=g_dcwd; c->uid=g_uid; c->gid=g_gid; c->umask=g_umask; c->brk=g_ubrk; c->mmnext=g_mmap_next;
    c->sigpend=0; c->exitst=0; c->stopnote=0; c->wake=0; c->runcpu=0; c->cputicks=0; c->it_next=0; c->it_int=0;
    for(int f=0;f<FDMAX;f++) if(c->fd[f]>=0)vf_ref(c->fd[f]);
    jc_save_sys(r,&c->ctx); c->ctx.rax=0; c->state=S_RUN;
    smp_kick_idle();
    return c->pid;
}
int jc_clone_thread(struct sysregs* r,u64 stack,u64 tls,int settls){
    int i=jc_alloc(); if(i<0)return -1; struct jproc* par=&g_jp[g_jcur]; struct jproc* L=jc_lead(par);
    struct jproc* c=&g_jp[i]; *c=*par;
    cpu_fpu_save(c->fx);
    c->pid=g_next_pid++; c->tg=par->tg; c->tgid=L->pid; c->ppid=L->ppid; c->pgid=L->pgid; c->sid=L->sid;
    c->sigpend=0; c->exitst=0; c->stopnote=0; c->wake=0; c->runcpu=0; c->cputicks=0; c->it_next=0; c->it_int=0;
    c->gexit=0; c->tkill=0; c->ctid=0; c->robust=0; c->fx_wait=0; c->fx_woken=0; c->susp=0; c->pollwake=0;
    c->ss_sp=0; c->ss_size=0; c->ss_flags=2; c->flt_sig=0; for(int k=0;k<32;k++)c->sicode[k]=0;
    for(int f=0;f<FDMAX;f++){ c->fd[f]=-1; c->cloexec[f]=0; }
    jc_save_sys(r,&c->ctx); c->ctx.rax=0; if(stack)c->ctx.rsp=stack; if(settls)c->fsbase=tls;
    c->state=S_RUN;
    smp_kick_idle();
    return i;
}
void jc_exec(struct sysregs* r,const char* args){
    struct jproc* p=&g_jp[g_jcur]; int L=p->tg;
    if(jc_tg_live(L,g_jcur)>0){ for(int j=0;j<JMAX;j++) if(j!=g_jcur&&jc_tg_member(j,L)){ g_jp[j].tkill=1; jc_signal_thread(j,9); } jc_block(r,S_WAIT,g_ticks+1); }
    char a[256]; int k=0; while(args[k]&&k<255){ a[k]=args[k]; k++; } a[k]=0;
    u64 entry,rsp,argc,argv; u64 cr3=jc_build(a,&entry,&rsp,&argc,&argv);
    if(!cr3){ r->rax=(u64)-1; return; }
    cpu_set_pt(g_kern_cr3);
    if(g_jcur!=L){ struct jproc* l=&g_jp[L];
        l->sigmask=p->sigmask; l->sigpend|=p->sigpend; l->uid=p->uid; l->gid=p->gid; l->lx=p->lx;
        l->runcpu=p->runcpu; l->lastcpu=p->lastcpu; l->cputicks+=p->cputicks; l->tkill=0; l->gexit=0;
        p->runcpu=0; p->state=S_FREE; g_jcur=L; l->state=S_RUN; p=l; }
    p->ctid=0; p->robust=0; p->fx_wait=0;
    vm_free_user(p->cr3); p->cr3=cr3;
    for(int f=0;f<FDMAX;f++) if(p->cloexec[f]&&p->fd[f]>=0){ vf_unref(p->fd[f]); p->fd[f]=-1; p->cloexec[f]=0; }
    for(int s=0;s<32;s++){ if(p->sighand[s]>1){ p->sighand[s]=0; p->sigrest[s]=0; p->sigamask[s]=0; } p->saflags[s]=0; }
    p->fsbase=0; p->gsbase=0; p->lxbrk=0; p->pollwake=0; p->susp=0; p->lx=g_elf_pie; p->ss_sp=0; p->ss_size=0; p->ss_flags=2; p->flt_sig=0;
    g_ubrk=0x40002000000ULL; g_mmap_next=0x40004000000ULL; p->brk=g_ubrk; p->mmnext=g_mmap_next; p->lxbrk=0;
    jc_ctx_fresh(&p->ctx,entry,rsp,argc,argv); jc_cmdname(p,a); jc_fx_init(p->fx);
    k_longjmp(g_js_kern,1);
}
int jc_dup2(int o,int n){
    int* t=fdt(); if(o<0||o>=FDMAX||n<0||n>=FDMAX||t[o]<0)return -1; if(o==n)return n;
    if(t[n]>=0)vf_unref(t[n]); t[n]=t[o]; vf_ref(t[o]); fdce()[n]=0; return n;
}

static int jc_ustr(u64 a,int max){ for(int i=0;i<max;i++){ u64 q=a+(u64)i; if((i==0||(q&0xFFFULL)==0)&&!jc_umapped(q,1))return -1; if(!*(const char*)q)return i; } return -1; }
int jc_syscall(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur];
    if(p->lx){ jc_signal_proc(g_jcur,31); r->rax=(u64)-38; return 1; }
    switch(r->rax){
    case 1: case 4: case 5: if(jc_ustr(r->rbx,4096)<0){ r->rax=(u64)-1; return 1; } break;
    case 157: { if(jc_ustr(r->rbx,4096)<0){ r->rax=(u64)-1; return 1; } u64 av=r->rcx; if(av){ for(int k=0;k<8;k++){ if(!jc_umapped(av+(u64)k*8,8)){ r->rax=(u64)-1; return 1; } u64 s=*(u64*)(av+(u64)k*8); if(!s)break; if(k&&jc_ustr(s,4096)<0){ r->rax=(u64)-1; return 1; } } } break; }
    case 31: case 32: if((long)r->rdx<0||(long)r->rdx>(1L<<24)||!(r->rax==31?jc_umapped_w(r->rcx,r->rdx):jc_umapped(r->rcx,r->rdx))){ r->rax=(u64)-1; return 1; } break;
    case 145: if(!jc_umapped_w(r->rbx,8)){ r->rax=(u64)-1; return 1; } break;
    }
    switch(r->rax){
    case 0: jc_finish(g_jcur,((int)r->rbx&0xFF)<<8); jc_leave();
    case 1: { const char* s=(const char*)r->rbx; int n=0; while(s[n]&&n<4096)n++; jc_tty_out(s,n); return 1; }
    case 3: { char c=(char)r->rbx; jc_tty_out(&c,1); return 1; }
    case 2: r->rax=(u64)(long)jc_fork(r); return 1;
    case 4: jc_exec(r,(const char*)r->rbx); return 1;
    case 157: { u64* av=(u64*)r->rcx; char a[128]; int n=0; const char* path=(const char*)r->rbx;
        for(int k=0;path[k]&&n<120;k++)a[n++]=path[k];
        if(av&&jc_uok((u64)av,8)) for(int k=1;k<8&&av[k];k++){ if(n>=125)break; a[n++]=' '; const char* s=(const char*)av[k]; for(int q=0;s[q]&&n<126;q++)a[n++]=s[q]; }
        a[n]=0; jc_exec(r,a); return 1; }
    case 5: { int i=jc_create((const char*)r->rbx,p->pid,p->pgid); r->rax=(u64)(long)(i<0?-1:g_jp[i].pid); if(i>=0){ g_jp[i].cwd=g_dcwd; g_jp[i].uid=g_uid; } return 1; }
    case 6: r->rax=(u64)(long)jc_waitpid(r,(int)r->rbx,0,0); if((long)r->rax>0){ } return 1;
    case 156: r->rax=(u64)(long)jc_waitpid(r,(int)r->rbx,r->rcx,(int)r->rdx); return 1;
    case 16: case 71: { u64 ms=r->rbx; r->rax=0; jc_block(r,S_SLEEP,g_ticks+(ms+9)/10); }
    case 158: r->rax=(u64)-1; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL);
    case 20: r->rax=0; jc_yield_sys(r);
    case 172: { int fd=(int)r->rbx; int vi=(fd>=0&&fd<FDMAX)?p->fd[fd]:-1; r->rax=(u64)((vi>=0&&g_vf[vi].used&&g_vf[vi].dev==5)?1:0); return 1; }
    case 131: { int nu=(int)r->rbx; if(p->uid==0||nu==p->uid){ p->uid=nu; g_uid=nu; r->rax=0; } else r->rax=(u64)-1; return 1; }
    case 17: g_kill_check=1; r->rax=(u64)(long)jc_kill((int)r->rbx,(int)r->rcx,g_jcur); g_kill_check=0; if(jc_deliverable(p))jc_yield_sys(r); return 1;
    case 39: r->rax=(u64)(long)p->pid; return 1;
    case 132: r->rax=(u64)(long)p->ppid; return 1;
    case 31: { int n=vfs_read((int)r->rbx,(char*)r->rcx,(int)r->rdx); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1); r->rax=(u64)(long)n; return 1; }
    case 32: { int fd=(int)r->rbx; int n=vfs_write(fd,(const char*)r->rcx,(int)r->rdx); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1);
        r->rax=(u64)(long)n; return 1; }
    case 50: { char c; if(g_tin_n>0&&p->pgid==g_tty_fg){ jc_tty_read(&c,1); r->rax=(u64)(unsigned char)c; } else r->rax=(u64)-1; return 1; }
    case 51: { char c; int n=jc_tty_read(&c,1); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1); r->rax=(n>0)?(u64)(unsigned char)c:(u64)-1; return 1; }
    case 141: { int s=(int)r->rbx; if(s<=0||s>=32||s==9||s==19){ r->rax=(u64)-1; return 1; } u64 old=p->sighand[s]; p->sighand[s]=r->rcx; p->sigrest[s]=r->rdx; p->sigamask[s]=(u32)r->rsi; r->rax=old; return 1; }
    case 142: { u32 old=p->sigmask, set=(u32)r->rcx; int how=(int)r->rbx; if(how==0)p->sigmask|=set; else if(how==1)p->sigmask&=~set; else if(how==2)p->sigmask=set; p->sigmask&=~((1u<<9)|(1u<<19)); r->rax=old; if(jc_deliverable(p))jc_yield_sys(r); return 1; }
    case 143: { u64 f=r->rbx; if(!jc_umapped(f,sizeof(regs_pt)+8)){ jc_finish(g_jcur,11); jc_leave(); }
        regs_pt c=*(regs_pt*)f; u32 m=(u32)*(u64*)(f+sizeof(regs_pt));
        if(!jc_uok(c.rip,1)){ jc_finish(g_jcur,11); jc_leave(); }
        c.cs=0x1B; c.ss=0x23; c.rflags=(c.rflags&0xCD5)|0x202; p->ctx=c; p->sigmask=m&~((1u<<9)|(1u<<19)); jc_leave(); }
    case 144: r->rax=(u64)(long)jc_dup2((int)r->rbx,(int)r->rcx); return 1;
    case 145: { int* out=(int*)r->rbx; if(!jc_uok((u64)out,8)){ r->rax=(u64)-1; return 1; } int rv,wv; if(jc_pipe_new(4096,&rv,&wv)<0){ r->rax=(u64)-1; return 1; }
        int* t=fdt(); int a=-1,b=-1; for(int f=0;f<FDMAX;f++) if(t[f]==-1){ if(a<0)a=f; else { b=f; break; } }
        if(a<0||b<0){ vf_unref(rv); vf_unref(wv); r->rax=(u64)-1; return 1; }
        t[a]=rv; t[b]=wv; fdce()[a]=0; fdce()[b]=0; out[0]=a; out[1]=b; r->rax=0; return 1; }
    case 146: { int fd=(int)r->rbx, cmd=(int)r->rcx; int* t=fdt(); if(fd<0||fd>=FDMAX||t[fd]<0){ r->rax=(u64)-1; return 1; }
        if(cmd==0){ int n=-1; int f0=(int)r->rdx; if(f0<0)f0=0; for(int f=f0;f<FDMAX;f++) if(t[f]==-1){ n=f; break; } if(n>=0){ t[n]=t[fd]; vf_ref(t[fd]); fdce()[n]=0; } r->rax=(u64)(long)n; }
        else if(cmd==1) r->rax=fdce()[fd]; else if(cmd==2){ fdce()[fd]=(u8)(r->rdx&1); r->rax=0; } else if(cmd==3) r->rax=(u64)g_vf[t[fd]].flags; else r->rax=0; return 1; }
    case 147: { int old=g_umask; g_umask=(int)r->rbx&0777; r->rax=(u64)old; return 1; }
    case 148: r->rax=(u64)g_gid; return 1;
    case 149: if(g_uid==0||(int)r->rbx==g_gid){ g_gid=(int)r->rbx; r->rax=0; } else r->rax=(u64)-1; return 1;
    case 150: { int pid=(int)r->rbx?(int)r->rbx:p->pid, pg=(int)r->rcx?(int)r->rcx:pid; int t=jc_idx(pid);
        if(t<0||(t!=g_jcur&&g_jp[t].ppid!=p->pid)||g_jp[t].sid!=p->sid){ r->rax=(u64)-1; return 1; } g_jp[t].pgid=pg; jc_tg_sync_ids(t); r->rax=0; return 1; }
    case 151: { int pid=(int)r->rbx?(int)r->rbx:p->pid; int t=jc_idx(pid); r->rax=(u64)(long)(t<0?-1:g_jp[t].pgid); return 1; }
    case 152: if(p->pgid==p->pid){ r->rax=(u64)-1; return 1; } p->sid=p->pid; p->pgid=p->pid; r->rax=(u64)p->pid; return 1;
    case 153: { int pid=(int)r->rbx?(int)r->rbx:p->pid; int t=jc_idx(pid); r->rax=(u64)(long)(t<0?-1:g_jp[t].sid); return 1; }
    case 154: r->rax=(u64)(long)(p->sid==1?(g_tty_fg?g_tty_fg:1):-1); return 1;
    case 155: if(p->sid==1){ g_tty_fg=(int)r->rbx; r->rax=0; } else r->rax=(u64)-1; return 1;
    case 12: { u64 addr=r->rbx; if(addr==0){ r->rax=g_ubrk; return 1; } u64 nb=(addr+0xFFF)&~0xFFFULL; if(nb>g_ubrk+0x10000000ULL){ r->rax=g_ubrk; return 1; } while(g_ubrk<nb){ u64 f=pframe_alloc(); if(!f)break; if(vm_map(p->cr3,g_ubrk,f,0x7ULL)<0){ pframe_free(f); break; } g_ubrk+=0x1000; } r->rax=g_ubrk; return 1; }
    }
    return 0;
}

