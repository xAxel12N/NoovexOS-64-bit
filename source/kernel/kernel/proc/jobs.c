extern int k_setjmp(u64*) __attribute__((returns_twice));
extern void jc_resume(regs_pt* ctx) __attribute__((noreturn));

#define JMAX 16
#define S_FREE 0
#define S_RUN 1
#define S_WAIT 2
#define S_SLEEP 3
#define S_STOP 4
#define S_ZOMB 5
#define JS_STK_PAGES 16
#define PIPE_MAX 12
#define JOBMAX 16

struct jproc {
    int state, pid, ppid, pgid, sid, uid, gid, cwd, umask;
    u64 cr3, brk, mmnext, wake;
    regs_pt ctx;
    int fd[FDMAX]; u8 cloexec[FDMAX];
    u32 sigpend, sigmask, sigamask[32]; u64 sighand[32], sigrest[32];
    int exitst, stopnote;
    char cmd[48];
    u8 fx[512] __attribute__((aligned(16)));
};
static struct jproc g_jp[JMAX];
static int g_jcur=-1, g_next_pid=2, g_js_dirty=0;
static volatile int g_js_running=0;
static u64 g_js_kern[8], g_js_slice=0;
static u8 g_jkstack[16384] __attribute__((aligned(16)));
static int g_tty_fg=0, g_gid=0, g_umask=022;
static int g_con_vi=-1;

struct jpipe { int used, r, w, n, cap, readers, writers; char* buf; };
static struct jpipe g_pipe[PIPE_MAX];

struct job { int used, id, pgid, state, bg, notified_stop; char cmd[64]; };
static struct job g_jobs2[JOBMAX];

static char g_tin[1024]; static int g_tin_n=0, g_tin_eof=0;
static char g_ted[256]; static int g_ted_n=0;

static int* fdt(void){ return (g_jcur>=0)?g_jp[g_jcur].fd:g_fd; }
static u8 g_kcloexec[FDMAX];
static u8* fdce(void){ return (g_jcur>=0)?g_jp[g_jcur].cloexec:g_kcloexec; }

static int jc_idx(int pid){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].pid==pid)return i; return -1; }
static int jc_uok(u64 p,u64 n){ return p>=0x40000000000ULL && p+n<0x80000000000ULL && p+n>=p; }

static int jc_con(void){
    fd_init();
    if(g_con_vi>=0&&g_vf[g_con_vi].used)return g_con_vi;
    for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){ g_vf[i].used=1; g_vf[i].ref=1; g_vf[i].fs_idx=-1; g_vf[i].off=0; g_vf[i].flags=O_RDWR; g_vf[i].dev=5; g_vf[i].vfs=0; g_con_vi=i; return i; }
    return -1;
}
static void vf_ref(int vi){ if(vi>=0&&vi<VMAXOPEN&&g_vf[vi].used)g_vf[vi].ref++; }
static void vf_unref(int vi){
    if(vi<0||vi>=VMAXOPEN||!g_vf[vi].used)return;
    if(--g_vf[vi].ref>0)return;
    struct vfile* v=&g_vf[vi];
    if(v->dev==6||v->dev==7){ struct jpipe* p=&g_pipe[v->fs_idx]; if(v->dev==6)p->readers--; else p->writers--;
        if(p->readers<=0&&p->writers<=0){ if(p->buf)kfree(p->buf); p->buf=0; p->used=0; } }
    if(vi==g_con_vi){ v->ref=1; return; }
    if(v->dev==9&&g_fb0_open>0)g_fb0_open--; if(v->dev==10&&g_mouse_open>0)g_mouse_open--;
    v->used=0;
}
static int jc_newvf(int dev,int idx,int flags){
    for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){ g_vf[i].used=1; g_vf[i].ref=1; g_vf[i].fs_idx=idx; g_vf[i].off=0; g_vf[i].flags=flags; g_vf[i].dev=dev; g_vf[i].vfs=0; return i; }
    return -1;
}
static int jc_pipe_new(int cap,int* rvi,int* wvi){
    int pi=-1; for(int i=0;i<PIPE_MAX;i++) if(!g_pipe[i].used){pi=i;break;} if(pi<0)return -1;
    char* b=(char*)kmalloc((unsigned long)cap); if(!b)return -1;
    int r=jc_newvf(6,pi,O_RDONLY); if(r<0){ kfree(b); return -1; }
    int w=jc_newvf(7,pi,O_WRONLY); if(w<0){ g_vf[r].used=0; kfree(b); return -1; }
    struct jpipe* p=&g_pipe[pi]; p->used=1; p->r=0; p->w=0; p->n=0; p->cap=cap; p->readers=1; p->writers=1; p->buf=b;
    *rvi=r; *wvi=w; return pi;
}
static int jc_pipe_read(struct vfile* v,char* buf,int len){
    struct jpipe* p=&g_pipe[v->fs_idx];
    if(p->n==0) return p->writers>0?JS_BLOCK:0;
    int c=0; while(c<len&&p->n>0){ buf[c++]=p->buf[p->r]; p->r=(p->r+1)%p->cap; p->n--; }
    return c;
}
static void jc_signal_proc(int i,int sig);
static int jc_pipe_write(struct vfile* v,const char* buf,int len){
    struct jpipe* p=&g_pipe[v->fs_idx];
    if(p->readers<=0){ if(g_jcur>=0)jc_signal_proc(g_jcur,13); return -1; }
    if(p->n>=p->cap) return JS_BLOCK;
    int c=0; while(c<len&&p->n<p->cap){ p->buf[p->w]=buf[c++]; p->w=(p->w+1)%p->cap; p->n++; }
    return c;
}

static void jc_tty_out(const char* s,int n){ u32 sv=tcur; tcol(WHT); for(int i=0;i<n;i++){ char c=s[i]; if(c=='\r')continue; if(c=='\t'){ term_putc(' '); term_putc(' '); continue; } if((unsigned char)c<32&&c!='\n'&&c!='\b')continue; term_putc(c); } tcol(sv); g_js_dirty=1; }
static int jc_tty_read(char* buf,int len){
    if(g_jcur<0)return 0;
    struct jproc* p=&g_jp[g_jcur];
    if(p->sid!=1)return -1;
    if(p->pgid!=g_tty_fg){ jc_signal_proc(g_jcur,21); return JS_BLOCK; }
    if(g_tin_n>0){ int c=0; while(c<len&&c<g_tin_n){ char ch=g_tin[c]; buf[c++]=ch; if(ch=='\n')break; }
        for(int i=c;i<g_tin_n;i++)g_tin[i-c]=g_tin[i]; g_tin_n-=c; return c; }
    if(g_tin_eof){ g_tin_eof=0; return 0; }
    return JS_BLOCK;
}

static void vm_free_user(u64 cr3){
    if(!cr3)return; u64* pml4=(u64*)(cr3&~0xFFFULL);
    if(pml4[8]&1){ u64* pdpt=(u64*)(pml4[8]&~0xFFFULL);
        for(int i3=0;i3<512;i3++){ if(!(pdpt[i3]&1))continue; u64* pd=(u64*)(pdpt[i3]&~0xFFFULL);
            for(int i2=0;i2<512;i2++){ if(!(pd[i2]&1))continue; u64* pt=(u64*)(pd[i2]&~0xFFFULL);
                for(int i1=0;i1<512;i1++) if(pt[i1]&1)pframe_free(pt[i1]&~0xFFFULL);
                pframe_free((u64)pt); }
            pframe_free((u64)pd); }
        pframe_free((u64)pdpt); pml4[8]=0; }
    pframe_free(cr3&~0xFFFULL);
}
static u64 vm_clone_user(u64 src){
    u64 dst=vm_new_pml4(); if(!dst)return 0;
    u64* ppml4=(u64*)(src&~0xFFFULL);
    if(ppml4[8]&1){ u64* ppdpt=(u64*)(ppml4[8]&~0xFFFULL);
        for(int i3=0;i3<512;i3++){ if(!(ppdpt[i3]&1))continue; u64* ppd=(u64*)(ppdpt[i3]&~0xFFFULL);
            for(int i2=0;i2<512;i2++){ if(!(ppd[i2]&1))continue; u64* ppt=(u64*)(ppd[i2]&~0xFFFULL);
                for(int i1=0;i1<512;i1++){ if(!(ppt[i1]&1))continue;
                    u64 v=((u64)8<<39)|((u64)i3<<30)|((u64)i2<<21)|((u64)i1<<12);
                    u64 f=pframe_alloc(); if(!f){ vm_free_user(dst); return 0; }
                    u8* d=(u8*)f; u8* s=(u8*)(ppt[i1]&~0xFFFULL); for(int b=0;b<4096;b++)d[b]=s[b];
                    if(vm_map(dst,v,f,0x7ULL)){ vm_free_user(dst); return 0; } } } } }
    return dst;
}

extern const unsigned char _binary_user_count_elf_start[], _binary_user_count_elf_end[];
extern const unsigned char _binary_user_spin_elf_start[], _binary_user_spin_elf_end[];
extern const unsigned char _binary_user_sigdemo_elf_start[], _binary_user_sigdemo_elf_end[];
extern const unsigned char _binary_user_upper_elf_start[], _binary_user_upper_elf_end[];
extern const unsigned char _binary_user_greet_elf_start[], _binary_user_greet_elf_end[];
extern const unsigned char _binary_user_pipetest_elf_start[], _binary_user_pipetest_elf_end[];
extern const unsigned char _binary_user_nvxbox_elf_start[], _binary_user_nvxbox_elf_end[];
extern const unsigned char _binary_user_init_elf_start[], _binary_user_init_elf_end[];
extern const unsigned char _binary_user_libc8_elf_start[], _binary_user_libc8_elf_end[];
#define NVXB(n) {n,_binary_user_nvxbox_elf_start,_binary_user_nvxbox_elf_end}
static const struct { const char* name; const unsigned char* s; const unsigned char* e; } JS_BIN[]={
    {"count",_binary_user_count_elf_start,_binary_user_count_elf_end},
    {"spin",_binary_user_spin_elf_start,_binary_user_spin_elf_end},
    {"sigdemo",_binary_user_sigdemo_elf_start,_binary_user_sigdemo_elf_end},
    {"upper",_binary_user_upper_elf_start,_binary_user_upper_elf_end},
    {"greet",_binary_user_greet_elf_start,_binary_user_greet_elf_end},
    {"pipetest",_binary_user_pipetest_elf_start,_binary_user_pipetest_elf_end},
    NVXB("ls"),NVXB("cat"),NVXB("grep"),NVXB("wc"),NVXB("head"),NVXB("tail"),NVXB("echo"),NVXB("mkdir"),NVXB("rmdir"),NVXB("rm"),
    NVXB("mv"),NVXB("cp"),NVXB("touch"),NVXB("ln"),NVXB("chmod"),NVXB("chown"),NVXB("stat"),NVXB("pwd"),NVXB("hostname"),NVXB("whoami"),
    NVXB("id"),NVXB("uname"),NVXB("date"),NVXB("sleep"),NVXB("mount"),NVXB("umount"),NVXB("readlink"),NVXB("true"),NVXB("false"),NVXB("tee"),NVXB("sort"),NVXB("nvxbox"),
    {"libc8",_binary_user_libc8_elf_start,_binary_user_libc8_elf_end},
    {"+init",_binary_user_init_elf_start,_binary_user_init_elf_end},
    {0,0,0}};
static int jc_ieq(const char* a,const char* b){ while(*a&&*b){ char x=*a,y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; a++;b++; } return !*a&&!*b; }
static const u8* jc_find_prog(const char* name,u32* len){
    const char* n=name; if(n[0]=='/'&&n[1]=='b'&&n[2]=='i'&&n[3]=='n'&&n[4]=='/')n+=5; else if(n[0]=='/'&&n[1]=='u'&&n[2]=='s'&&n[3]=='r'&&n[4]=='/'&&n[5]=='b'&&n[6]=='i'&&n[7]=='n'&&n[8]=='/')n+=9;
    else if(n[0]=='/'&&n[1]=='s'&&n[2]=='b'&&n[3]=='i'&&n[4]=='n'&&n[5]=='/'){ n+=6; if(n[0]=='i'&&n[1]=='n'&&n[2]=='i'&&n[3]=='t'&&!n[4])n="+init"; }
    char base[64]; int k=0; while(n[k]&&k<63){ base[k]=n[k]; k++; } base[k]=0;
    if(k>4&&jc_ieq(base+k-4,".elf"))base[k-4]=0;
    if(jc_ieq(base,"init")&&(name[0]=='+'||g_jcur<0||1)){ for(int i=0;JS_BIN[i].name;i++) if(JS_BIN[i].name[0]=='+'){ *len=(u32)(JS_BIN[i].e-JS_BIN[i].s); return JS_BIN[i].s; } }
    for(int i=0;JS_BIN[i].name;i++) if(jc_ieq(base,JS_BIN[i].name)){ *len=(u32)(JS_BIN[i].e-JS_BIN[i].s); return JS_BIN[i].s; }
    int isd_=0; int f=ux_lookup(name,&isd_,1); if(isd_)f=-1;
    if(f>=0&&FS[f].len>=64&&(u8)FS[f].data[0]==0x7F&&FS[f].data[1]=='E'&&FS[f].data[2]=='L'&&FS[f].data[3]=='F'){
        if(g_uid!=0&&!(FS[f].mode&0111))return 0; *len=(u32)FS[f].len; return (const u8*)FS[f].data; }
    int got=fat_read_file(name,g_spawnbuf,(int)sizeof(g_spawnbuf));
    if(got>=64&&g_spawnbuf[0]==0x7F&&g_spawnbuf[1]=='E'){ *len=(u32)got; return g_spawnbuf; }
    return 0;
}
static int jc_is_prog(const char* name){ u32 l; if(!name[0])return 0; return jc_find_prog(name,&l)!=0; }

static u64 jc_build(const char* args,u64* entry,u64* rsp,u64* argc,u64* argv){
    char nm[64]; int k=0; while(args[k]&&args[k]!=' '&&k<63){ nm[k]=args[k]; k++; } nm[k]=0;
    u32 len=0; const u8* elf=jc_find_prog(nm,&len); if(!elf)return 0;
    { int j=0; while(args[j]&&j<127){ g_exec_args[j]=args[j]; j++; } g_exec_args[j]=0; }
    u64 cr3=elf_build_space(elf,len,entry); if(!cr3)return 0;
    for(int p=1;p<JS_STK_PAGES;p++){ u64 f=pframe_alloc(); if(!f||vm_map(cr3,USTK_V-(u64)p*0x1000ULL,f,0x7ULL)){ vm_free_user(cr3); return 0; } }
    *rsp=g_arg_rsp?g_arg_rsp:USTK_V+0x1000ULL; *argc=(u64)g_arg_argc; *argv=g_arg_argv; return cr3;
}
static void jc_fx_init(u8* fx){ for(int i=0;i<512;i++)fx[i]=0; *(u16*)fx=0x037F; *(u32*)(fx+24)=0x1F80; *(u32*)(fx+28)=0xFFFF; }
static void jc_ctx_fresh(regs_pt* c,u64 entry,u64 rsp,u64 argc,u64 argv){
    for(int i=0;i<(int)(sizeof(*c)/8);i++)((u64*)c)[i]=0;
    c->rip=entry; c->cs=0x1B; c->rflags=0x202; c->rsp=rsp; c->ss=0x23; c->rdi=argc; c->rsi=argv;
}
static void jc_cmdname(struct jproc* p,const char* args){ int i=0; while(args[i]&&i<47){ p->cmd[i]=args[i]; i++; } p->cmd[i]=0; }
static int jc_alloc(void){ for(int i=0;i<JMAX;i++) if(g_jp[i].state==S_FREE)return i; return -1; }
static int jc_create(const char* args,int ppid,int pgid){
    int i=jc_alloc(); if(i<0)return -1;
    u64 entry,rsp,argc,argv; u64 cr3=jc_build(args,&entry,&rsp,&argc,&argv); if(!cr3)return -2;
    struct jproc* p=&g_jp[i]; u8* raw=(u8*)p; for(u32 q=0;q<sizeof(*p);q++)raw[q]=0;
    p->pid=g_next_pid++; p->ppid=ppid; p->pgid=pgid?pgid:p->pid; p->sid=1; p->uid=g_uid; p->gid=g_gid; p->cwd=g_dcwd; p->umask=g_umask;
    p->cr3=cr3; p->brk=0x40002000000ULL; p->mmnext=0x40004000000ULL;
    jc_ctx_fresh(&p->ctx,entry,rsp,argc,argv);
    for(int f=0;f<FDMAX;f++)p->fd[f]=-1;
    int con=jc_con(); for(int f=0;f<3;f++){ p->fd[f]=con; vf_ref(con); }
    jc_cmdname(p,args); jc_fx_init(p->fx); p->state=S_RUN;
    return i;
}

static int sig_default(int s){ switch(s){ case 17: case 28: case 23: return 0; case 18: return 3; case 19: case 20: case 21: case 22: return 2; default: return 1; } }
static void jc_wake_parent(int ppid){ int pi=jc_idx(ppid); if(pi>=0){ if(g_jp[pi].state==S_WAIT)g_jp[pi].wake=0; jc_signal_proc(pi,17); } }
static void jc_close_all(struct jproc* p){ for(int f=0;f<FDMAX;f++) if(p->fd[f]>=0){ vf_unref(p->fd[f]); p->fd[f]=-1; } }
static void jc_finish(int i,int status){
    struct jproc* p=&g_jp[i];
    jc_close_all(p);
    u64 kcr3=g_kern_cr3; u64 cur; __asm__ volatile("mov %%cr3,%0":"=r"(cur));
    if((cur&~0xFFFULL)==(p->cr3&~0xFFFULL)) __asm__ volatile("mov %0,%%cr3"::"r"(kcr3):"memory");
    vm_free_user(p->cr3); p->cr3=0;
    p->exitst=status; p->state=S_ZOMB;
    { int np=(jc_idx(1)>=0&&p->pid!=1)?1:0; for(int j=0;j<JMAX;j++) if(g_jp[j].state!=S_FREE&&g_jp[j].ppid==p->pid)g_jp[j].ppid=np; }
    jc_wake_parent(p->ppid); g_js_dirty=1;
}
static void jc_stop(int i,int sig){ struct jproc* p=&g_jp[i]; p->state=S_STOP; p->exitst=(sig<<8)|0x7F; p->stopnote=1; jc_wake_parent(p->ppid); g_js_dirty=1; }
static void jc_signal_proc(int i,int sig){
    if(i<0||sig<=0||sig>=32)return; struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB)return;
    if(sig==18){ if(p->state==S_STOP){ p->state=S_RUN; p->stopnote=2; jc_wake_parent(p->ppid); }
        p->sigpend&=~((1u<<19)|(1u<<20)|(1u<<21)|(1u<<22)); if(p->sighand[18]>1)p->sigpend|=(1u<<18); return; }
    if(sig==19||sig==20||sig==21||sig==22) p->sigpend&=~(1u<<18);
    if(sig!=9&&sig!=19&&(p->sighand[sig]==1||(p->sighand[sig]==0&&sig_default(sig)==0)))return;
    p->sigpend|=(1u<<sig);
    if(sig==9&&p->state==S_STOP)p->state=S_RUN;
    if(p->state==S_WAIT||p->state==S_SLEEP)p->wake=0;
}
static int jc_kill(int pid,int sig,int self){
    int hit=0;
    for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB)continue;
        int m=0; if(pid>0)m=(p->pid==pid); else if(pid==0)m=(self>=0&&p->pgid==g_jp[self].pgid); else if(pid==-1)m=(i!=self); else m=(p->pgid==-pid);
        if(!m)continue; hit++; if(sig)jc_signal_proc(i,sig); }
    return hit?0:-1;
}
static u32 jc_deliverable(struct jproc* p){ return (p->sigpend&~p->sigmask)|(p->sigpend&((1u<<9)|(1u<<19))); }
static int jc_push_handler(int i,int s){
    struct jproc* p=&g_jp[i];
    u64 lo=USTK_V-(u64)(JS_STK_PAGES-1)*0x1000ULL, hi=USTK_V+0x1000ULL;
    u64 sp=p->ctx.rsp; if(sp<lo+512||sp>hi)return -1;
    sp-=128; sp-=sizeof(regs_pt)+16; sp&=~0xFULL; u64 frame=sp; sp-=8;
    __asm__ volatile("mov %0,%%cr3"::"r"(p->cr3):"memory");
    regs_pt* sv=(regs_pt*)frame; *sv=p->ctx; *(u64*)(frame+sizeof(regs_pt))=p->sigmask; *(u64*)sp=p->sigrest[s];
    __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory");
    p->ctx.rip=p->sighand[s]; p->ctx.rsp=sp; p->ctx.rdi=(u64)s; p->ctx.rsi=0; p->ctx.rdx=0; p->ctx.rax=0;
    p->sigmask|=(1u<<s)|p->sigamask[s]; p->sigmask&=~((1u<<9)|(1u<<19));
    return 0;
}
static void jc_deliver(int i){
    struct jproc* p=&g_jp[i];
    for(int guard=0;guard<32;guard++){
        u32 d=jc_deliverable(p); if(!d)return;
        int s=0; while(!(d&(1u<<s)))s++; p->sigpend&=~(1u<<s);
        if(s==9){ jc_finish(i,9); return; }
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
static void jc_leave(void) __attribute__((noreturn));
static void jc_leave(void){ if(g_jcur>=0)__asm__ volatile("fxsave %0":"=m"(g_jp[g_jcur].fx)); __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory"); k_longjmp(g_js_kern,1); for(;;){} }
static void jc_block(struct sysregs* r,int state,u64 wake) __attribute__((noreturn));
static void jc_block(struct sysregs* r,int state,u64 wake){
    struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx);
    if(state==S_WAIT)p->ctx.rip-=2;
    p->state=state; p->wake=wake; jc_leave();
}
static void jc_yield_sys(struct sysregs* r) __attribute__((noreturn));
static void jc_yield_sys(struct sysregs* r){ struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx); jc_leave(); }

static int jc_waitpid(struct sysregs* r,int pid,u64 stp,int opts){
    struct jproc* me=&g_jp[g_jcur]; int any=0;
    for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(q->state==S_FREE||q->ppid!=me->pid)continue;
        int m=(pid>0)?(q->pid==pid):(pid==0)?(q->pgid==me->pgid):(pid==-1)?1:(q->pgid==-pid); if(!m)continue; any=1;
        if(q->state==S_ZOMB){ int st=q->exitst, qp=q->pid; if(stp&&jc_uok(stp,4))*(int*)stp=st; q->state=S_FREE; return qp; }
        if((opts&2)&&q->stopnote==1){ q->stopnote=0; if(stp&&jc_uok(stp,4))*(int*)stp=q->exitst; return q->pid; }
        if((opts&8)&&q->stopnote==2){ q->stopnote=0; if(stp&&jc_uok(stp,4))*(int*)stp=0xFFFF; return q->pid; } }
    if(!any)return -1;
    if(opts&1)return 0;
    jc_block(r,S_WAIT,g_ticks+1); return 0;
}
static int jc_fork(struct sysregs* r){
    int i=jc_alloc(); if(i<0)return -1; struct jproc* par=&g_jp[g_jcur];
    u64 cr3=vm_clone_user(par->cr3); if(!cr3)return -1;
    struct jproc* c=&g_jp[i]; *c=*par;
    __asm__ volatile("fxsave %0":"=m"(c->fx));
    c->pid=g_next_pid++; c->ppid=par->pid; c->cr3=cr3; c->cwd=g_dcwd; c->uid=g_uid; c->gid=g_gid; c->umask=g_umask; c->brk=g_ubrk; c->mmnext=g_mmap_next;
    c->sigpend=0; c->exitst=0; c->stopnote=0; c->wake=0;
    for(int f=0;f<FDMAX;f++) if(c->fd[f]>=0)vf_ref(c->fd[f]);
    jc_save_sys(r,&c->ctx); c->ctx.rax=0; c->state=S_RUN;
    return c->pid;
}
static void jc_exec(struct sysregs* r,const char* args){
    struct jproc* p=&g_jp[g_jcur];
    char a[128]; int k=0; while(args[k]&&k<127){ a[k]=args[k]; k++; } a[k]=0;
    u64 entry,rsp,argc,argv; u64 cr3=jc_build(a,&entry,&rsp,&argc,&argv);
    if(!cr3){ r->rax=(u64)-1; return; }
    __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory");
    vm_free_user(p->cr3); p->cr3=cr3;
    for(int f=0;f<FDMAX;f++) if(p->cloexec[f]&&p->fd[f]>=0){ vf_unref(p->fd[f]); p->fd[f]=-1; p->cloexec[f]=0; }
    for(int s=0;s<32;s++) if(p->sighand[s]>1){ p->sighand[s]=0; p->sigrest[s]=0; p->sigamask[s]=0; }
    g_ubrk=0x40002000000ULL; g_mmap_next=0x40004000000ULL;
    jc_ctx_fresh(&p->ctx,entry,rsp,argc,argv); jc_cmdname(p,a); jc_fx_init(p->fx);
    k_longjmp(g_js_kern,1);
}
static int jc_dup2(int o,int n){
    int* t=fdt(); if(o<0||o>=FDMAX||n<0||n>=FDMAX||t[o]<0)return -1; if(o==n)return n;
    if(t[n]>=0)vf_unref(t[n]); t[n]=t[o]; vf_ref(t[o]); fdce()[n]=0; return n;
}

static int jc_syscall(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur];
    switch(r->rax){
    case 0: jc_finish(g_jcur,((int)r->rbx&0xFF)<<8); jc_leave();
    case 1: { const char* s=(const char*)r->rbx; int n=0; while(s[n]&&n<4096)n++; jc_tty_out(s,n); return 1; }
    case 3: { char c=(char)r->rbx; jc_tty_out(&c,1); return 1; }
    case 2: r->rax=(u64)(long)jc_fork(r); return 1;
    case 4: jc_exec(r,(const char*)r->rbx); return 1;
    case 157: { u64* av=(u64*)r->rcx; char a[128]; int n=0; const char* path=(const char*)r->rbx;
        for(int k=0;path[k]&&n<120;k++)a[n++]=path[k];
        if(av&&jc_uok((u64)av,8)) for(int k=1;k<8&&av[k];k++){ a[n++]=' '; const char* s=(const char*)av[k]; for(int q=0;s[q]&&n<126;q++)a[n++]=s[q]; }
        a[n]=0; jc_exec(r,a); return 1; }
    case 5: { int i=jc_create((const char*)r->rbx,p->pid,p->pgid); r->rax=(u64)(long)(i<0?-1:g_jp[i].pid); if(i>=0){ g_jp[i].cwd=g_dcwd; g_jp[i].uid=g_uid; } return 1; }
    case 6: r->rax=(u64)(long)jc_waitpid(r,(int)r->rbx,0,0); if((long)r->rax>0){ } return 1;
    case 156: r->rax=(u64)(long)jc_waitpid(r,(int)r->rbx,r->rcx,(int)r->rdx); return 1;
    case 16: case 71: { u64 ms=r->rbx; r->rax=0; jc_block(r,S_SLEEP,g_ticks+(ms+9)/10); }
    case 158: r->rax=(u64)-1; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL);
    case 20: r->rax=0; jc_yield_sys(r);
    case 17: r->rax=(u64)(long)jc_kill((int)r->rbx,(int)r->rcx,g_jcur); if(jc_deliverable(p))jc_yield_sys(r); return 1;
    case 39: r->rax=(u64)(long)p->pid; return 1;
    case 132: r->rax=(u64)(long)p->ppid; return 1;
    case 31: { int n=vfs_read((int)r->rbx,(char*)r->rcx,(int)r->rdx); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1); r->rax=(u64)(long)n; return 1; }
    case 32: { int fd=(int)r->rbx; int n=vfs_write(fd,(const char*)r->rcx,(int)r->rdx); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1);
        int* t=fdt(); if(n>0&&fd>=0&&fd<FDMAX&&t[fd]>=0&&g_vf[t[fd]].dev==0&&disk_ok){ int idx=g_vf[t[fd]].fs_idx; if(idx>=0)nvx_write(FS[idx].name,(const u8*)FS[idx].data,FS[idx].len,FS[idx].folder); }
        r->rax=(u64)(long)n; return 1; }
    case 50: { char c; if(g_tin_n>0&&p->pgid==g_tty_fg){ jc_tty_read(&c,1); r->rax=(u64)(unsigned char)c; } else r->rax=(u64)-1; return 1; }
    case 51: { char c; int n=jc_tty_read(&c,1); if(n==JS_BLOCK)jc_block(r,S_WAIT,g_ticks+1); r->rax=(n>0)?(u64)(unsigned char)c:(u64)-1; return 1; }
    case 141: { int s=(int)r->rbx; if(s<=0||s>=32||s==9||s==19){ r->rax=(u64)-1; return 1; } u64 old=p->sighand[s]; p->sighand[s]=r->rcx; p->sigrest[s]=r->rdx; p->sigamask[s]=(u32)r->rsi; r->rax=old; return 1; }
    case 142: { u32 old=p->sigmask, set=(u32)r->rcx; int how=(int)r->rbx; if(how==0)p->sigmask|=set; else if(how==1)p->sigmask&=~set; else if(how==2)p->sigmask=set; p->sigmask&=~((1u<<9)|(1u<<19)); r->rax=old; if(jc_deliverable(p))jc_yield_sys(r); return 1; }
    case 143: { u64 f=r->rbx; if(!jc_uok(f,sizeof(regs_pt)+8)){ jc_finish(g_jcur,11); jc_leave(); }
        regs_pt c=*(regs_pt*)f; u32 m=(u32)*(u64*)(f+sizeof(regs_pt));
        c.cs=0x1B; c.ss=0x23; c.rflags=(c.rflags&0xCD5)|0x202; p->ctx=c; p->sigmask=m&~((1u<<9)|(1u<<19)); jc_leave(); }
    case 144: r->rax=(u64)(long)jc_dup2((int)r->rbx,(int)r->rcx); return 1;
    case 145: { int* out=(int*)r->rbx; if(!jc_uok((u64)out,8)){ r->rax=(u64)-1; return 1; } int rv,wv; if(jc_pipe_new(4096,&rv,&wv)<0){ r->rax=(u64)-1; return 1; }
        int* t=fdt(); int a=-1,b=-1; for(int f=0;f<FDMAX;f++) if(t[f]==-1){ if(a<0)a=f; else { b=f; break; } }
        if(a<0||b<0){ vf_unref(rv); vf_unref(wv); r->rax=(u64)-1; return 1; }
        t[a]=rv; t[b]=wv; fdce()[a]=0; fdce()[b]=0; out[0]=a; out[1]=b; r->rax=0; return 1; }
    case 146: { int fd=(int)r->rbx, cmd=(int)r->rcx; int* t=fdt(); if(fd<0||fd>=FDMAX||t[fd]<0){ r->rax=(u64)-1; return 1; }
        if(cmd==0){ int n=-1; for(int f=(int)r->rdx;f<FDMAX;f++) if(t[f]==-1){ n=f; break; } if(n>=0){ t[n]=t[fd]; vf_ref(t[fd]); fdce()[n]=0; } r->rax=(u64)(long)n; }
        else if(cmd==1) r->rax=fdce()[fd]; else if(cmd==2){ fdce()[fd]=(u8)(r->rdx&1); r->rax=0; } else if(cmd==3) r->rax=(u64)g_vf[t[fd]].flags; else r->rax=0; return 1; }
    case 147: { int old=g_umask; g_umask=(int)r->rbx&0777; r->rax=(u64)old; return 1; }
    case 148: r->rax=(u64)g_gid; return 1;
    case 149: if(g_uid==0||(int)r->rbx==g_gid){ g_gid=(int)r->rbx; r->rax=0; } else r->rax=(u64)-1; return 1;
    case 150: { int pid=(int)r->rbx?(int)r->rbx:p->pid, pg=(int)r->rcx?(int)r->rcx:pid; int t=jc_idx(pid);
        if(t<0||(t!=g_jcur&&g_jp[t].ppid!=p->pid)||g_jp[t].sid!=p->sid){ r->rax=(u64)-1; return 1; } g_jp[t].pgid=pg; r->rax=0; return 1; }
    case 151: { int pid=(int)r->rbx?(int)r->rbx:p->pid; int t=jc_idx(pid); r->rax=(u64)(long)(t<0?-1:g_jp[t].pgid); return 1; }
    case 152: if(p->pgid==p->pid){ r->rax=(u64)-1; return 1; } p->sid=p->pid; p->pgid=p->pid; r->rax=(u64)p->pid; return 1;
    case 153: { int pid=(int)r->rbx?(int)r->rbx:p->pid; int t=jc_idx(pid); r->rax=(u64)(long)(t<0?-1:g_jp[t].sid); return 1; }
    case 154: r->rax=(u64)(long)(p->sid==1?(g_tty_fg?g_tty_fg:1):-1); return 1;
    case 155: if(p->sid==1){ g_tty_fg=(int)r->rbx; r->rax=0; } else r->rax=(u64)-1; return 1;
    case 12: { u64 addr=r->rbx; if(addr==0){ r->rax=g_ubrk; return 1; } u64 nb=(addr+0xFFF)&~0xFFFULL; while(g_ubrk<nb){ u64 f=pframe_alloc(); if(!f)break; vm_map(p->cr3,g_ubrk,f,0x7ULL); g_ubrk+=0x1000; } r->rax=g_ubrk; return 1; }
    }
    return 0;
}

static void jc_fault(regs_t* rr){
    struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx));
    int v=(int)rr->vector; int s=(v==0)?8:(v==6)?4:(v==3)?5:11;
    if(p->sighand[s]<=1||(p->sigmask&(1u<<s))){ jc_finish(g_jcur,s); }
    else { p->sigpend|=(1u<<s); }
    jc_leave();
}
static void jc_timer(regs_t* rr){
    if(g_ticks-g_js_slice<2)return;
    struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx));
    pic_eoi(0); jc_leave();
}

static int g_js_rr=0;
static int jc_runnable(int i){
    struct jproc* p=&g_jp[i];
    if(p->state==S_RUN)return 1;
    if((p->state==S_WAIT||p->state==S_SLEEP)&&(p->wake<=g_ticks||jc_deliverable(p))){ if(p->state==S_SLEEP&&!jc_deliverable(p))p->ctx.rax=0; return 1; }
    if(p->state==S_STOP&&(p->sigpend&(1u<<9)))return 1;
    return 0;
}
static int jc_any_runnable(void){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB&&jc_runnable(i))return 1; return 0; }
static void jc_enter(int i){
    struct jproc* p=&g_jp[i];
    if(p->state==S_SLEEP&&p->wake>g_ticks)p->ctx.rax=(u64)-4;
    if(p->state==S_WAIT||p->state==S_SLEEP)p->state=S_RUN;
    __asm__ volatile("mov %%cr3,%0":"=r"(g_kern_cr3));
    if(jc_deliverable(p)){ jc_deliver(i); if(p->state!=S_RUN)return; }
    if(p->state!=S_RUN)return;
    us_init();
    volatile int kd=g_dcwd, ku=g_uid, kg=g_gid, km=g_umask; volatile u64 kb=g_ubrk, kmm=g_mmap_next;
    g_dcwd=p->cwd; g_uid=p->uid; g_gid=p->gid; g_umask=p->umask; g_ubrk=p->brk; g_mmap_next=p->mmnext;
    g_jcur=i; g_tss.rsp0=(u64)(g_jkstack+sizeof(g_jkstack)); g_js_slice=g_ticks;
    __asm__ volatile("cli");
    if(k_setjmp(g_js_kern)==0){
        g_js_running=1;
        __asm__ volatile("fxrstor %0"::"m"(p->fx));
        __asm__ volatile("mov %0,%%cr3"::"r"(p->cr3):"memory");
        jc_resume(&p->ctx);
    }
    g_js_running=0;
    __asm__ volatile("mov %0,%%cr3"::"r"(g_kern_cr3):"memory");
    struct jproc* q=&g_jp[g_jcur];
    if(q->state!=S_FREE&&q->state!=S_ZOMB){ q->cwd=g_dcwd; q->uid=g_uid; q->gid=g_gid; q->umask=g_umask; q->brk=g_ubrk; q->mmnext=g_mmap_next; }
    g_dcwd=kd; g_uid=ku; g_gid=kg; g_umask=km; g_ubrk=kb; g_mmap_next=kmm; g_jcur=-1;
    __asm__ volatile("sti");
}
static int jc_run(void){
    if(!pf_ready)return 0;
    u64 start=g_ticks; int ran=0;
    for(int n=0;n<JMAX*2;n++){
        int pick=-1; for(int k=1;k<=JMAX;k++){ int j=(g_js_rr+k)%JMAX; if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_ZOMB&&jc_runnable(j)){ pick=j; break; } }
        if(pick<0)break; g_js_rr=pick; jc_enter(pick); ran=1;
        if(g_ticks-start>=2)break;
    }
    return ran;
}

static int jc_job_new(int pgid,const char* cmd,int bg){
    int id=1; for(int again=1;again;){ again=0; for(int j=0;j<JOBMAX;j++) if(g_jobs2[j].used&&g_jobs2[j].id==id){ id++; again=1; } }
    for(int j=0;j<JOBMAX;j++) if(!g_jobs2[j].used){ struct job* b=&g_jobs2[j]; b->used=1; b->id=id; b->pgid=pgid; b->state=1; b->bg=bg; b->notified_stop=0; int k=0; while(cmd[k]&&k<63){ b->cmd[k]=cmd[k]; k++; } b->cmd[k]=0; return j; }
    return -1;
}
static int jc_job_alive(int pgid){ int n=0; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB&&g_jp[i].pgid==pgid)n++; return n; }
static int jc_job_stopped(int pgid){ int any=0; for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB||p->pgid!=pgid)continue; if(p->state!=S_STOP)return 0; any=1; } return any; }
static void jc_prompt_again(void){ term_prompt(); u32 sv=tcur; tcol(WHT); for(int i=0;i<tlen;i++)term_putc(tline[i]); tcol(sv); }
static const char* jc_signame(int s){ switch(s){ case 1:return "Hangup"; case 2:return "Interrupt"; case 3:return "Quit"; case 4:return "Illegal instruction"; case 6:return "Aborted"; case 8:return "Floating point exception"; case 9:return "Killed"; case 11:return "Segmentation fault"; case 13:return "Broken pipe"; case 14:return "Alarm clock"; case 15:return "Terminated"; default:return "Signal"; } }
static void jc_poll(void){
    int lastsig=0;
    int initalive=(jc_idx(1)>=0&&g_jp[jc_idx(1)].state!=S_ZOMB);
    for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state!=S_ZOMB||(p->ppid!=0&&!(p->ppid==1&&!initalive)))continue;
        if((p->exitst&0x7F)&&(p->exitst&0x7F)!=0x7F){ int s=p->exitst&0x7F; if(s!=2&&s!=13)lastsig=s; }
        p->state=S_FREE; }
    for(int j=0;j<JOBMAX;j++){ struct job* b=&g_jobs2[j]; if(!b->used)continue;
        int fg=(g_tty_fg==b->pgid);
        if(!jc_job_alive(b->pgid)){
            if(fg){ g_tty_fg=0; g_tin_n=0; g_tin_eof=0; g_ted_n=0; if(lastsig){ u32 sv=tcur; tcol(RED); term_puts(jc_signame(lastsig)); term_putc('\n'); tcol(sv); } term_prompt(); tlen=0; }
            else { char m[96],*q=m; *q++='['; q=udec(q,(u32)b->id); q=scopy(q,"]+  Done       "); q=scopy(q,b->cmd); *q=0;
                u32 sv=tcur; if(!g_tty_fg)term_putc('\n'); tcol(GREY); term_puts(m); term_putc('\n'); tcol(sv); if(!g_tty_fg)jc_prompt_again(); }
            b->used=0; g_js_dirty=1; continue; }
        if(jc_job_stopped(b->pgid)&&b->state!=2){ b->state=2;
            if(fg){ g_tty_fg=0; g_tin_n=0; g_ted_n=0; }
            char m[96],*q=m; if(!fg&&!g_tty_fg)term_putc('\n'); else if(fg)term_putc('\n'); *q++='['; q=udec(q,(u32)b->id); q=scopy(q,"]+  Stopped    "); q=scopy(q,b->cmd); *q=0;
            u32 sv=tcur; tcol(rgb(240,190,90)); term_puts(m); term_putc('\n'); tcol(sv);
            if(fg){ term_prompt(); tlen=0; } else if(!g_tty_fg)jc_prompt_again(); g_js_dirty=1; }
    }
}
static int jc_step(void){ int a=jc_run(); jc_poll(); int d=g_js_dirty; g_js_dirty=0; return a||d; }

static void tty_key(int c){
    if(c==3||c==26||c==28){ term_puts(c==3?"^C":c==26?"^Z":"^\\"); if(c!=26)term_putc('\n'); g_ted_n=0; jc_kill(-g_tty_fg,c==3?2:c==26?20:3,-1); g_js_dirty=1; return; }
    if(c==4){ if(g_ted_n==0)g_tin_eof=1; else { for(int i=0;i<g_ted_n&&g_tin_n<1023;i++)g_tin[g_tin_n++]=g_ted[i]; g_ted_n=0; } return; }
    if(c=='\b'){ if(g_ted_n>0){ g_ted_n--; term_putc('\b'); } return; }
    if(c=='\n'){ for(int i=0;i<g_ted_n&&g_tin_n<1022;i++)g_tin[g_tin_n++]=g_ted[i]; g_tin[g_tin_n++]='\n'; g_ted_n=0; term_putc('\n'); return; }
    if(c>=32&&c<127&&g_ted_n<255){ g_ted[g_ted_n++]=(char)c; u32 sv=tcur; tcol(WHT); term_putc((char)c); tcol(sv); }
}

static int jc_tok(char** s,char* out,int cap){
    char* p=*s; while(*p==' ')p++; if(!*p){ *s=p; return 0; }
    int n=0; if(*p=='"'){ p++; while(*p&&*p!='"'&&n<cap-1)out[n++]=*p++; if(*p=='"')p++; }
    else while(*p&&*p!=' '&&n<cap-1)out[n++]=*p++;
    out[n]=0; *s=p; return 1;
}
static int jc_openinto(int i,int fd,const char* path,int flags){
    int save=g_jcur; g_jcur=i; int nf=vfs_open(path,flags); g_jcur=save; if(nf<0)return -1;
    struct jproc* p=&g_jp[i]; if(fd!=nf){ if(p->fd[fd]>=0)vf_unref(p->fd[fd]); p->fd[fd]=p->fd[nf]; p->fd[nf]=-1; }
    return 0;
}
static int jc_launch(const char* line){
    char work[256]; int wl=0; while(line[wl]&&wl<255){ work[wl]=line[wl]; wl++; } work[wl]=0;
    while(wl>0&&work[wl-1]==' ')work[--wl]=0; if(!wl)return 0;
    int bg=0; if(work[wl-1]=='&'&&(wl<2||work[wl-2]!='>')){ bg=1; work[--wl]=0; while(wl>0&&work[wl-1]==' ')work[--wl]=0; }
    char* st[6]; int ns=0; st[ns++]=work; for(int k=0;work[k]&&ns<6;k++) if(work[k]=='|'){ work[k]=0; st[ns++]=&work[k+1]; }
    int isp[6], anyp=0, firstp=-1;
    for(int s=0;s<ns;s++){ char* q=st[s]; char t[64]; isp[s]=jc_tok(&q,t,64)&&jc_is_prog(t); if(isp[s]){ anyp=1; if(firstp<0)firstp=s; } }
    if(!anyp)return 0;
    for(int s=firstp;s<ns;s++) if(!isp[s]){ tcol(RED); term_puts("nsh: built-in commands can't read from programs yet\n"); tcol(WHT); return 1; }
    char* feed=0; int feedn=0;
    if(firstp>0){
        char sub[256]; int n=0; for(int s=0;s<firstp;s++){ if(s){ sub[n++]='|'; } for(int k=0;st[s][k]&&n<250;k++)sub[n++]=st[s][k]; } sub[n]=0;
        g_capture=1; g_cap_n=0; g_cap[0]=0; run_cmd(sub); g_capture=0;
        feedn=g_cap_n; feed=(char*)kmalloc((unsigned long)feedn+1); if(feed){ for(int k=0;k<feedn;k++)feed[k]=g_cap[k]; }
    }
    int pids[6], idxs[6], np=0, pgid=0; int prevr=-1;
    if(feed){ int rv,wv; int pi=jc_pipe_new(feedn+1,&rv,&wv); if(pi>=0){ struct jpipe* pp=&g_pipe[pi]; for(int k=0;k<feedn;k++)pp->buf[k]=feed[k]; pp->n=feedn; pp->w=feedn%pp->cap; vf_unref(wv); prevr=rv; } kfree(feed); }
    char full[128]; { int n=0; for(int k=0;line[k]&&n<127;k++)full[n++]=line[k]; full[n]=0; while(n>0&&(full[n-1]==' '||full[n-1]=='&'))full[--n]=0; }
    for(int s=firstp;s<ns;s++){
        char args[128]; int an=0; char inf[64]={0}, outf[64]={0}, errf[64]={0}; int app=0, e2o=0, eapp=0;
        char* q=st[s]; char t[64];
        while(jc_tok(&q,t,64)){
            if(seq(t,"<")){ jc_tok(&q,inf,64); continue; }
            if(seq(t,">")||seq(t,">>")){ app=(t[1]=='>'); jc_tok(&q,outf,64); continue; }
            if(seq(t,"2>")||seq(t,"2>>")){ eapp=(t[2]=='>'); jc_tok(&q,errf,64); continue; }
            if(seq(t,"2>&1")){ e2o=1; continue; }
            if(t[0]=='>'&&t[1]&&t[1]!='>'){ for(int k=0;t[k+1]&&k<62;k++)outf[k]=t[k+1]; continue; }
            if(an)args[an++]=' '; for(int k=0;t[k]&&an<126;k++)args[an++]=t[k]; }
        args[an]=0;
        int i=jc_create(args,0,pgid);
        if(i<0){ tcol(RED); term_puts("nsh: cannot start "); term_puts(args); term_putc('\n'); tcol(WHT); for(int k=0;k<np;k++)jc_signal_proc(idxs[k],9); if(prevr>=0)vf_unref(prevr); return 1; }
        struct jproc* p=&g_jp[i]; if(!pgid)pgid=p->pid; p->pgid=pgid; p->state=S_STOP;
        if(prevr>=0){ vf_unref(p->fd[0]); p->fd[0]=prevr; prevr=-1; }
        if(s<ns-1){ int rv,wv; if(jc_pipe_new(4096,&rv,&wv)>=0){ vf_unref(p->fd[1]); p->fd[1]=wv; prevr=rv; } }
        if(inf[0]&&jc_openinto(i,0,inf,O_RDONLY)){ tcol(RED); term_puts("nsh: "); term_puts(inf); term_puts(": no such file\n"); tcol(WHT); }
        if(outf[0])jc_openinto(i,1,outf,O_CREAT|O_WRONLY|(app?O_APPEND:O_TRUNC));
        if(errf[0])jc_openinto(i,2,errf,O_CREAT|O_WRONLY|(eapp?O_APPEND:O_TRUNC));
        if(e2o){ vf_unref(p->fd[2]); p->fd[2]=p->fd[1]; vf_ref(p->fd[1]); }
        pids[np]=p->pid; idxs[np]=i; np++;
    }
    for(int k=0;k<np;k++){ g_jp[idxs[k]].state=S_RUN; g_jp[idxs[k]].stopnote=0; }
    int j=jc_job_new(pgid,full,bg);
    if(bg){ char m[48],*q=m; *q++='['; q=udec(q,(u32)(j>=0?g_jobs2[j].id:0)); *q++=']'; *q++=' '; q=udec(q,(u32)pids[np-1]); *q=0; tcol(GREY); term_puts(m); term_putc('\n'); tcol(WHT); }
    else { g_tty_fg=pgid; g_tin_n=0; g_tin_eof=0; g_ted_n=0; }
    g_js_dirty=1; return 1;
}
static struct job* jc_jobarg(const char* a){
    while(*a==' ')a++; int want=-1; if(*a=='%')a++; if(*a>='0'&&*a<='9'){ want=0; while(*a>='0'&&*a<='9')want=want*10+(*a++-'0'); }
    struct job* best=0; for(int j=0;j<JOBMAX;j++){ struct job* b=&g_jobs2[j]; if(!b->used)continue; if(want>=0){ if(b->id==want)return b; } else if(!best||b->id>best->id)best=b; }
    return want>=0?0:best;
}
static int jc_builtin(const char* cmd,const char* arg){
    if(seq((char*)cmd,"jobs")){ int any=0; for(int j=0;j<JOBMAX;j++){ struct job* b=&g_jobs2[j]; if(!b->used)continue; any=1; char m[120],*q=m; *q++='['; q=udec(q,(u32)b->id); q=scopy(q,"]  "); q=scopy(q,b->state==2?"Stopped    ":"Running    "); q=scopy(q,b->cmd); *q=0; term_puts(m); term_putc('\n'); }
        if(!any){ tcol(GREY); term_puts("no jobs\n"); tcol(WHT); } return 1; }
    if(seq((char*)cmd,"fg")||seq((char*)cmd,"bg")){ struct job* b=jc_jobarg(arg); if(!b){ tcol(RED); term_puts(cmd); term_puts(": no such job\n"); tcol(WHT); return 1; }
        term_puts(b->cmd); term_putc('\n'); b->state=1; jc_kill(-b->pgid,18,-1);
        if(cmd[0]=='f'){ g_tty_fg=b->pgid; b->bg=0; g_tin_n=0; g_ted_n=0; g_tin_eof=0; } else b->bg=1;
        g_js_dirty=1; return 1; }
    if(seq((char*)cmd,"kill")){ const char* a=arg; while(*a==' ')a++; int sig=15;
        if(*a=='-'){ a++; if(*a>='0'&&*a<='9'){ sig=0; while(*a>='0'&&*a<='9')sig=sig*10+(*a++-'0'); }
            else { static const char* SN[]={"HUP","INT","QUIT","ILL","TRAP","ABRT","BUS","FPE","KILL","USR1","SEGV","USR2","PIPE","ALRM","TERM","STKFLT","CHLD","CONT","STOP","TSTP","TTIN","TTOU"};
                char nm[12]; int k=0; if(a[0]=='S'&&a[1]=='I'&&a[2]=='G')a+=3; while(*a&&*a!=' '&&k<11)nm[k++]=*a++; nm[k]=0; sig=-1; for(int q=0;q<22;q++) if(seq(nm,(char*)SN[q]))sig=q+1;
                if(sig<0){ tcol(RED); term_puts("kill: unknown signal\n"); tcol(WHT); return 1; } } while(*a==' ')a++; }
        if(!*a){ term_puts("usage: kill [-SIGNAL] pid | %job\n"); return 1; }
        int r; if(*a=='%'){ struct job* b=jc_jobarg(a); r=b?jc_kill(-b->pgid,sig,-1):-1; if(b&&sig==18)b->state=1; }
        else { int pid=0,neg=0; if(*a=='-'){neg=1;a++;} while(*a>='0'&&*a<='9')pid=pid*10+(*a++-'0'); r=jc_kill(neg?-pid:pid,sig,-1); }
        if(r<0){ tcol(RED); term_puts("kill: no such process\n"); tcol(WHT); } g_js_dirty=1; return 1; }
    if(seq((char*)cmd,"ps")){ tcol(GREY); term_puts("  PID  PPID  PGID  STAT  COMMAND\n"); tcol(WHT);
        term_puts("    0     0     0  S     nsh (kernel shell)\n");
        for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE)continue; char m[96],*q=m;
            u32 v[3]={(u32)p->pid,(u32)p->ppid,(u32)p->pgid}; for(int k=0;k<3;k++){ char nb[12]; char* e=udec(nb,v[k]); int L=(int)(e-nb); for(int s=L;s<5;s++)*q++=' '; for(int s=0;s<L;s++)*q++=nb[s]; *q++=' '; }
            q=scopy(q," "); const char* ss=p->state==S_RUN?"R":p->state==S_STOP?"T":p->state==S_ZOMB?"Z":"S"; q=scopy(q,ss); if(p->pgid==g_tty_fg)*q++='+'; else *q++=' ';
            q=scopy(q,"    "); q=scopy(q,p->cmd); *q=0; term_puts(m); term_putc('\n'); }
        return 1; }
    if(seq((char*)cmd,"umask")){ const char* a=arg; while(*a==' ')a++; if(!*a){ char m[8]; m[0]='0'; m[1]='0'+((g_umask>>6)&7); m[2]='0'+((g_umask>>3)&7); m[3]='0'+(g_umask&7); m[4]='\n'; m[5]=0; term_puts(m); }
        else { int v=0; while(*a>='0'&&*a<='7')v=v*8+(*a++-'0'); g_umask=v&0777; } return 1; }
    if(seq((char*)cmd,"wait")){ int n=0; for(int j=0;j<JOBMAX;j++) if(g_jobs2[j].used)n++; if(n){ char m[48],*q=m; q=udec(q,(u32)n); q=scopy(q," job(s) still running - use fg to wait"); *q=0; term_puts(m); term_putc('\n'); } return 1; }
    return 0;
}
static void dt_setmode(int n,unsigned m){ if(n>=0&&n<DN_MAX)DT[n].mode=m; }
