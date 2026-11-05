u64 g_segv_addr, g_segv_rip, g_segv_rsp, g_segv_stk[96]; int g_segv_n, g_segv_nstk, g_segv_ncode; u8 g_segv_code[24]; char g_segv_msg[128]; u32 g_segv_line;
static int segv_str(u64 a,char* out,int cap){
    int k=0; while(k<cap-1){ if(!jc_umapped(a+(u64)k,1))return 0; char c=*(const char*)(a+(u64)k); if(!c)break; if(c<32||c>126)return 0; out[k++]=c; }
    out[k]=0; int al=0; for(int i=0;i<k;i++) if((out[i]>='a'&&out[i]<='z')||(out[i]>='A'&&out[i]<='Z'))al++; return k>=6&&al>=4; }
static void segv_rec(regs_t* r,u64 va){
    extern void lx_sc_snap(void);
    if(g_segv_n++)return;
    g_segv_addr=va; g_segv_rip=r->rip; g_segv_rsp=r->rsp; g_segv_nstk=0; g_segv_ncode=0; g_segv_msg[0]=0; g_segv_line=0;
    if(jc_umapped(r->rip,16)){ for(int i=0;i<16;i++)g_segv_code[i]=((const u8*)r->rip)[i]; g_segv_ncode=16;
        const u8* c=g_segv_code; if(c[0]==0xC7&&c[1]==0x04&&c[2]==0x25)g_segv_line=*(const u32*)(c+7); }
    if(r->rip>=0x40000000040ULL&&jc_umapped(r->rip-96,96)){ const u8* c=(const u8*)(r->rip-96);
        for(int i=96-7;i>=0&&!g_segv_msg[0];i--){ if((c[i]==0x48||c[i]==0x4C)&&c[i+1]==0x8D&&(c[i+2]&0xC7)==0x05){ int d=*(const int*)(c+i+3); u64 t=r->rip-96+(u64)i+7+(u64)(long)d; segv_str(t,g_segv_msg,128); } } }
    for(int i=0;i<96;i++){ u64 a=r->rsp+(u64)i*8; if(!jc_umapped(a,8))break; g_segv_stk[i]=*(const u64*)a; g_segv_nstk=i+1; }
    lx_sc_snap(); }
int vm_user_pf(regs_t* r){
    if(!g_js_running||g_jcur<0)return 0;
    u64 va=cpu_cr2(); { struct jproc* fp=&g_jp[g_jcur]; fp->flt_addr=va; fp->flt_code=(r->errcode&1)?2:1; fp->flt_err=(u32)r->errcode; }
    if(!jc_uok(va,1)){ segv_rec(r,va); return 0; }
    if(r->errcode&0x10){ u64* xe=vm_walk(cpu_get_pt()&~0xFFFULL,va&~0xFFFULL,0); if(xe&&(*xe&1)&&(*xe&PTE_NX)){ g_jp[g_jcur].flt_code=2; segv_rec(r,va); return 0; } }
    int rc=vm_fault_user(cpu_get_pt(),va,(int)((r->errcode>>1)&1));
    if(rc==-1){ extern int lx_res_fault(u64,u64); if(lx_res_fault(cpu_get_pt(),va)==0)rc=vm_fault_user(cpu_get_pt(),va,(int)((r->errcode>>1)&1)); }
    if(rc==0)return 1;
    if(rc==-1)segv_rec(r,va);
    if(rc==-2){ jc_finish(g_jcur,9); jc_leave(); }
    return 0;
}
void jc_fault(regs_t* rr){
    struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx));
    int v=(int)rr->vector; int s=(v==0||v==16||v==19)?8:(v==6)?4:(v==1||v==3)?5:(v==17)?7:11;
    p->flt_sig=s; p->flt_trap=v; if(v!=14){ p->flt_err=(u32)rr->errcode; p->flt_addr=(v==13||v==12)?0:rr->rip; p->flt_code=v==0?1:v==16?7:v==19?7:v==6?2:v==3?1:v==1?2:v==17?1:0x80; }
    { static int nlog; if(p->lx&&nlog<24){ extern void klog(const char*); char b[260],*q=b; nlog++; q=scopy(q,"lx fault: pid "); q=udec(q,(u32)p->pid); q=scopy(q," sig "); q=udec(q,(u32)s); q=scopy(q," vec "); q=udec(q,(u32)v); q=scopy(q," rip "); q=uhex(q,rr->rip,12); q=scopy(q," addr "); q=uhex(q,p->flt_addr,12); q=scopy(q," err "); q=uhex(q,rr->errcode,4); q=scopy(q," rsp "); q=uhex(q,rr->rsp,12); { extern char* lx_addr_name(char*,u64,u64); q=lx_addr_name(q,p->cr3,rr->rip); } q=scopy(q,p->sighand[s]>1?" handled ":" fatal ");
        if(jc_umapped(rr->rip,8)){ for(int i=0;i<8;i++){ q=uhex(q,((const u8*)rr->rip)[i],2); *q++=' '; } } *q=0; klog(b); { extern void lx_sc_dump(u32,int); lx_sc_dump((u32)p->pid,16); } } }
    if(p->sighand[s]<=1||(p->sigmask&(1u<<s))){ jc_finish(g_jcur,s); }
    else { p->sigpend|=(1u<<s); }
    jc_leave();
}
static u64 jc_now(void){ return cpu_id()==0?g_ticks:PCPU->ticks; }
void jc_preempt(regs_t* rr){ struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx)); jc_leave(); }
void jc_timer(regs_t* rr){
    if(g_ticks-g_js_slice<2)return;
    struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx));
    pic_eoi(0); jc_leave();
}

static int g_js_rr=0;
static void jc_itimer(int i){
    struct jproc* p=&g_jp[i];
    { extern void lx_ptimer_tick(int); lx_ptimer_tick(i); }
    if(!p->it_next||g_ticks<p->it_next)return;
    if(p->it_int){ while(p->it_next<=g_ticks)p->it_next+=p->it_int; } else p->it_next=0;
    jc_signal_proc(i,14);
}
static int jc_runnable(int i){
    struct jproc* p=&g_jp[i];
    jc_itimer(i);
    if(p->runcpu)return 0;
    if(p->state==S_RUN)return 1;
    if((p->state==S_WAIT||p->state==S_SLEEP)&&(p->wake<=g_ticks||jc_deliverable(p))){ if(p->state==S_SLEEP&&!jc_deliverable(p))p->ctx.rax=0; return 1; }
    if(p->state==S_STOP&&(p->sigpend&(1u<<9)))return 1;
    return 0;
}
int jc_any_runnable(void){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB&&jc_runnable(i))return 1; return 0; }
void jc_enter(int i){
    struct jproc* p=&g_jp[i];
    if(p->state==S_SLEEP&&p->wake>g_ticks)p->ctx.rax=(u64)-4;
    if(p->state==S_WAIT||p->state==S_SLEEP)p->state=S_RUN;
    g_kern_cr3=cpu_get_pt();
    if(jc_deliverable(p)){ jc_deliver(i); if(p->state!=S_RUN)return; }
    if(p->state!=S_RUN)return;
    if(p->susp){ p->sigmask=p->suspold; p->susp=0; }
    us_init();
    volatile int kd=g_dcwd, ku=g_uid, kg=g_gid, km=g_umask; volatile u64 kb=g_ubrk, kmm=g_mmap_next;
    { struct jproc* l=jc_lead(p); g_dcwd=l->cwd; g_uid=p->uid; g_gid=p->gid; g_umask=l->umask; g_ubrk=l->brk; g_mmap_next=l->mmnext; }
    g_jcur=i; g_tss.rsp0=(u64)(g_jkstack+sizeof(g_jkstack)); lsys_kstack=g_tss.rsp0; lsys_fsbase=p->fsbase; cpu_wrmsr(0xC0000102u,p->gsbase); g_js_slice=jc_now();
    p->runcpu=cpu_id()+1; p->lastcpu=cpu_id(); PCPU->switches++;
    volatile u64 t0=jc_now();
    cpu_irq_off();
    volatile int bd=0;
    if(k_setjmp(g_js_kern)==0){
        g_js_running=1;
        cpu_fpu_restore(p->fx);
        cpu_set_pt(p->cr3);
        PCPU->ucr3=p->cr3;
        bd=bkl_drop();
        jc_resume(&p->ctx);
    }
    bkl_take(bd);
    g_js_running=0; PCPU->ucr3=0;
    g_jp[g_jcur].runcpu=0; g_jp[g_jcur].cputicks+=jc_now()-t0;
    cpu_set_pt(g_kern_cr3);
    struct jproc* q=&g_jp[g_jcur];
    if(q->state!=S_FREE&&q->state!=S_ZOMB){ q->uid=g_uid; q->gid=g_gid; if(!q->lx){ struct jproc* ql=jc_lead(q); ql->cwd=g_dcwd; ql->umask=g_umask; ql->brk=g_ubrk; ql->mmnext=g_mmap_next; } }
    g_dcwd=kd; g_uid=ku; g_gid=kg; g_umask=km; g_ubrk=kb; g_mmap_next=kmm; g_jcur=-1;
    cpu_irq_on();
}
int jc_pick(void){ for(int k=1;k<=JMAX;k++){ int j=(g_js_rr+k)%JMAX; if(g_jp[j].state!=S_FREE&&g_jp[j].state!=S_ZOMB&&jc_runnable(j)){ g_js_rr=j; return j; } } return -1; }
static int jc_run(void){
    if(!pf_ready)return 0;
    u64 start=g_ticks; int ran=0;
    for(int n=0;n<JMAX*2;n++){
        int pick=jc_pick();
        if(pick<0)break; jc_enter(pick); ran=1;
        if(g_ticks-start>=2)break;
    }
    return ran;
}

static int jc_job_new(int pgid,const char* cmd,int bg){
    int id=1; for(int again=1;again;){ again=0; for(int j=0;j<JOBMAX;j++) if(g_jobs2[j].used&&g_jobs2[j].id==id){ id++; again=1; } }
    for(int j=0;j<JOBMAX;j++) if(!g_jobs2[j].used){ struct job* b=&g_jobs2[j]; b->used=1; b->id=id; b->pgid=pgid; b->state=1; b->bg=bg; b->notified_stop=0; int k=0; while(cmd[k]&&k<63){ b->cmd[k]=cmd[k]; k++; } b->cmd[k]=0; return j; }
    return -1;
}
static int jc_job_alive(int pgid){ int n=0; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB&&g_jp[g_jp[i].tg].pgid==pgid)n++; return n; }
static int jc_job_stopped(int pgid){ int any=0; for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE||p->state==S_ZOMB||p->state==S_TDEAD||g_jp[p->tg].pgid!=pgid)continue; if(p->state!=S_STOP)return 0; any=1; } return any; }
static void jc_prompt_again(void){ term_prompt(); u32 sv=tcur; tcol(WHT); for(int i=0;i<tlen;i++)term_putc(tline[i]); tcol(sv); }
static const char* jc_signame(int s){ switch(s){ case 1:return "Hangup"; case 2:return "Interrupt"; case 3:return "Quit"; case 4:return "Illegal instruction"; case 6:return "Aborted"; case 8:return "Floating point exception"; case 9:return "Killed"; case 11:return "Segmentation fault"; case 13:return "Broken pipe"; case 14:return "Alarm clock"; case 15:return "Terminated"; default:return "Signal"; } }
static void jc_tty_restore(void){
    int alt=g_vt_alt; vt_reset();
    if(alt)live_invalidate();
    if(tcx>0&&tcx<=tcols){ int blank=1; for(int c=0;c<tcx&&c<tcols;c++) if(TCH[tcy][c]!=' ')blank=0; if(!blank)term_putc('\n'); else tcx=0; }
    else if(tcx>tcols)term_putc('\n');
}
static void jc_poll(void){
    int lastsig=0;
    int initalive=(jc_idx(1)>=0&&g_jp[jc_idx(1)].state!=S_ZOMB);
    for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state!=S_ZOMB||(p->ppid!=0&&!(p->ppid==1&&!initalive)))continue;
        if((p->exitst&0x7F)&&(p->exitst&0x7F)!=0x7F){ int s=p->exitst&0x7F; if(s!=2&&s!=13)lastsig=s; }
        p->state=S_FREE; }
    jc_coll_drain();
    for(int j=0;j<JOBMAX;j++){ struct job* b=&g_jobs2[j]; if(!b->used)continue;
        int fg=(g_tty_fg==b->pgid);
        if(!jc_job_alive(b->pgid)){
            if(fg){ g_tty_fg=0; g_tin_n=0; g_tin_eof=0; g_ted_n=0; jc_tty_restore(); if(lastsig){ u32 sv=tcur; tcol(RED); term_puts(jc_signame(lastsig)); term_putc('\n'); tcol(sv); }
                if(g_coll_vi>=0&&b->pgid==g_coll_pgid)jc_coll_finish();
                term_prompt(); tlen=0; }
            else { char m[96],*q=m; *q++='['; q=udec(q,(u32)b->id); q=scopy(q,"]+  Done       "); q=scopy(q,b->cmd); *q=0;
                u32 sv=tcur; if(!g_tty_fg)term_putc('\n'); tcol(GREY); term_puts(m); term_putc('\n'); tcol(sv); if(!g_tty_fg)jc_prompt_again(); }
            b->used=0; g_js_dirty=1; continue; }
        if(jc_job_stopped(b->pgid)&&b->state!=2){ b->state=2;
            if(fg){ g_tty_fg=0; g_tin_n=0; g_ted_n=0; jc_tty_restore(); }
            char m[96],*q=m; if(!fg&&!g_tty_fg)term_putc('\n'); else if(fg)term_putc('\n'); *q++='['; q=udec(q,(u32)b->id); q=scopy(q,"]+  Stopped    "); q=scopy(q,b->cmd); *q=0;
            u32 sv=tcur; tcol(rgb(240,190,90)); term_puts(m); term_putc('\n'); tcol(sv);
            if(fg){ term_prompt(); tlen=0; } else if(!g_tty_fg)jc_prompt_again(); g_js_dirty=1; }
    }
}
int jc_step(void){ int a=jc_run(); jc_poll(); int d=g_js_dirty; g_js_dirty=0; return (a?1:0)|(d?2:0); }

static void jc_tin_put(const char* k,int n){ for(int i=0;i<n&&g_tin_n<1023;i++)g_tin[g_tin_n++]=k[i]; g_js_dirty=1; }
void tty_key(int c){
    u32 lf=g_tios.lflag; int raw=vt_raw();
    if(lf&TIO_ISIG){
        if(c==3||c==26||c==28){ if(c!=26&&g_coll_vi>=0&&g_tty_fg==g_coll_pgid)g_coll_cancel=1; if(lf&TIO_ECHO){ term_puts(c==3?"^C":c==26?"^Z":"^\\"); if(c!=26)term_putc('\n'); } g_ted_n=0; jc_kill(-g_tty_fg,c==3?2:c==26?20:3,-1); g_js_dirty=1; return; } }
    if(raw){
        char k[8]; int n=0;
        if(c>=17&&c<=20){ k[n++]=27; k[n++]=g_vt_appcur?'O':'['; k[n++]="DCAB"[c-17]; }
        else if(c==2||c==5){ k[n++]=27; k[n++]=g_vt_appcur?'O':'['; k[n++]=c==2?'H':'F'; }
        else if(c==6||c==7||c==14||c==15){ k[n++]=27; k[n++]='['; k[n++]=c==6?'5':c==7?'6':c==14?'3':'2'; k[n++]='~'; }
        else if(c=='\n'){ k[n++]=(g_tios.iflag&TIO_ICRNL)?'\n':'\r'; }
        else if(c=='\b'){ k[n++]=127; }
        else if(c&0x100){ k[n++]=(char)(c&0x1F); }
        else k[n++]=(char)c;
        jc_tin_put(k,n);
        if((lf&TIO_ECHO)&&n==1&&(unsigned char)k[0]>=32&&k[0]!=127)vt_out(k,1);
        return; }
    int echo=(lf&TIO_ECHO)!=0;
    if(c&0x100){ int v=c&0x1F; if(v==21){ while(g_ted_n>0){ g_ted_n--; if(echo)term_putc('\b'); } } return; }
    if(c==4){ if(g_ted_n==0)g_tin_eof=1; else { for(int i=0;i<g_ted_n&&g_tin_n<1023;i++)g_tin[g_tin_n++]=g_ted[i]; g_ted_n=0; } return; }
    if(c=='\b'){ if(g_ted_n>0){ g_ted_n--; if(echo)term_putc('\b'); } return; }
    if(c=='\n'){ for(int i=0;i<g_ted_n&&g_tin_n<1022;i++)g_tin[g_tin_n++]=g_ted[i]; g_tin[g_tin_n++]='\n'; g_ted_n=0; if(echo||(lf&TIO_ECHONL))term_putc('\n'); return; }
    if(c=='\t')c=' ';
    if(c>=32&&c<127&&g_ted_n<255){ g_ted[g_ted_n++]=(char)c; if(echo){ u32 sv=tcur; tcol(WHT); term_putc((char)c); tcol(sv); } }
}

static int jc_tok(char** s,char* out,int cap){
    char* p=*s; while(*p==' ')p++; if(!*p){ *s=p; return 0; }
    int n=0; if(*p=='"'||*p=='\''){ char qc=*p++; while(*p&&*p!=qc&&n<cap-1)out[n++]=*p++; if(*p==qc)p++; }
    else while(*p&&*p!=' '&&n<cap-1)out[n++]=*p++;
    out[n]=0; *s=p; return 1;
}
static int jc_openinto(int i,int fd,const char* path,int flags){
    int save=g_jcur; g_jcur=i; int nf=vfs_open(path,flags); g_jcur=save; if(nf<0)return -1;
    struct jproc* p=&g_jp[i]; if(fd!=nf){ if(p->fd[fd]>=0)vf_unref(p->fd[fd]); p->fd[fd]=p->fd[nf]; p->fd[nf]=-1; }
    return 0;
}
static void jc_coll_drain(void){
    if(g_coll_vi<0)return; struct vfile* v=&g_vf[g_coll_vi]; char tmp[512];
    for(int guard=0;guard<256;guard++){ int r=jc_pipe_read(v,tmp,512); if(r<=0)break;
        for(int i=0;i<r;i++){ if(g_coll_n<COLL_CAP-1)g_coll_buf[g_coll_n++]=tmp[i]; else g_coll_trunc=1; } }
}
static void jc_coll_finish(void){
    jc_coll_drain(); vf_unref(g_coll_vi); g_coll_vi=-1; g_coll_buf[g_coll_n]=0;
    if(!g_coll_cancel){ if(g_coll_trunc){ term_puts("nsh: pipe output over 64 KB was cut off\n"); } g_run_feed=g_coll_buf; run_cmd(g_coll_rest); g_run_feed=0; }
    g_coll_pgid=0; g_coll_cancel=0;
}
int jc_launch(const char* line){
    static char work[2048]; int wl=0; while(line[wl]&&wl<2047){ work[wl]=line[wl]; wl++; } work[wl]=0;
    while(wl>0&&work[wl-1]==' ')work[--wl]=0; if(!wl)return 0;
    int bg=0; if(work[wl-1]=='&'&&(wl<2||work[wl-2]!='>')){ bg=1; work[--wl]=0; while(wl>0&&work[wl-1]==' ')work[--wl]=0; }
    char* st[6]; int ns=0; st[ns++]=work; { char qc=0; for(int k=0;work[k]&&ns<6;k++){ char c=work[k]; if(qc){ if(c==qc)qc=0; continue; } if(c=='"'||c=='\'')qc=c; else if(c=='|'){ work[k]=0; st[ns++]=&work[k+1]; } } }
    int isp[6], anyp=0, firstp=-1;
    for(int s=0;s<ns;s++){ char* q=st[s]; char t[64]; isp[s]=jc_tok(&q,t,64)&&jc_is_prog(t); if(isp[s]){ anyp=1; if(firstp<0)firstp=s; } }
    if(!anyp)return 0;
    int lastp=firstp; while(lastp+1<ns&&isp[lastp+1])lastp++;
    for(int s=lastp+1;s<ns;s++) if(isp[s]){ tcol(RED); term_puts("nsh: a program can't follow a built-in that reads from a program\n"); tcol(WHT); return 1; }
    int tail=(lastp<ns-1);
    if(tail&&bg){ tcol(RED); term_puts("nsh: pipes into built-in commands can't run in the background\n"); tcol(WHT); return 1; }
    if(tail&&g_coll_vi>=0){ tcol(RED); term_puts("nsh: another program-to-built-in pipe is still running\n"); tcol(WHT); return 1; }
    if(tail&&!g_coll_buf){ g_coll_buf=(char*)kmalloc(COLL_CAP); if(!g_coll_buf){ term_puts("nsh: out of memory\n"); return 1; } }
    char* feed=0; int feedn=0;
    if(firstp>0){
        char sub[256]; int n=0; for(int s=0;s<firstp;s++){ if(s){ sub[n++]='|'; } for(int k=0;st[s][k]&&n<250;k++)sub[n++]=st[s][k]; } sub[n]=0;
        g_capture=1; g_cap_n=0; g_cap[0]=0; run_cmd(sub); g_capture=0;
        feedn=g_cap_n; feed=(char*)kmalloc((unsigned long)feedn+1); if(feed){ for(int k=0;k<feedn;k++)feed[k]=g_cap[k]; }
    }
    int pids[6], idxs[6], np=0, pgid=0; int prevr=-1;
    if(feed){ int rv,wv; int pi=jc_pipe_new(feedn+1,&rv,&wv); if(pi>=0){ struct jpipe* pp=&g_pipe[pi]; for(int k=0;k<feedn;k++)pp->buf[k]=feed[k]; pp->n=feedn; pp->w=feedn%pp->cap; vf_unref(wv); prevr=rv; } kfree(feed); }
    char full[128]; { int n=0; for(int k=0;line[k]&&n<127;k++)full[n++]=line[k]; full[n]=0; while(n>0&&(full[n-1]==' '||full[n-1]=='&'))full[--n]=0; }
    for(int s=firstp;s<=lastp;s++){
        static char args[2048]; int an=0; char inf[64]={0}, outf[64]={0}, errf[64]={0}; int app=0, e2o=0, eapp=0;
        char* q=st[s]; static char t[2048];
        while(jc_tok(&q,t,2048)){
            if(seq(t,"<")){ jc_tok(&q,inf,64); continue; }
            if(seq(t,">")||seq(t,">>")){ app=(t[1]=='>'); jc_tok(&q,outf,64); continue; }
            if(seq(t,"2>")||seq(t,"2>>")){ eapp=(t[2]=='>'); jc_tok(&q,errf,64); continue; }
            if(seq(t,"2>&1")){ e2o=1; continue; }
            if(t[0]=='>'&&t[1]&&t[1]!='>'){ int k=0; for(;t[k+1]&&k<63;k++)outf[k]=t[k+1]; outf[k]=0; app=0; continue; }
            if(t[0]=='>'&&t[1]=='>'&&t[2]){ int k=0; for(;t[k+2]&&k<63;k++)outf[k]=t[k+2]; outf[k]=0; app=1; continue; }
            if(t[0]=='<'&&t[1]){ int k=0; for(;t[k+1]&&k<63;k++)inf[k]=t[k+1]; inf[k]=0; continue; }
            if(t[0]=='2'&&t[1]=='>'&&t[2]&&t[2]!='&'){ int ap=t[2]=='>'; int o=ap?3:2; if(t[o]){ int k=0; for(;t[k+o]&&k<63;k++)errf[k]=t[k+o]; errf[k]=0; eapp=ap; continue; } }
            if(an&&an<2046)args[an++]=' '; for(int k=0;t[k]&&an<2046;k++)args[an++]=(t[k]==' ')?(char)0x1F:t[k]; }
        args[an]=0;
        int i=jc_create(args,0,pgid);
        if(i<0){ tcol(RED); term_puts("nsh: cannot start "); term_puts(args); term_putc('\n'); tcol(WHT); for(int k=0;k<np;k++)jc_signal_proc(idxs[k],9); if(prevr>=0)vf_unref(prevr); return 1; }
        struct jproc* p=&g_jp[i]; if(!pgid)pgid=p->pid; p->pgid=pgid; p->state=S_STOP;
        if(prevr>=0){ vf_unref(p->fd[0]); p->fd[0]=prevr; prevr=-1; }
        if(s<lastp){ int rv,wv; if(jc_pipe_new(4096,&rv,&wv)>=0){ vf_unref(p->fd[1]); p->fd[1]=wv; prevr=rv; } }
        else if(tail){ int rv,wv; if(jc_pipe_new(4096,&rv,&wv)>=0){ vf_unref(p->fd[1]); p->fd[1]=wv; g_coll_vi=rv; g_coll_n=0; g_coll_trunc=0; g_coll_cancel=0;
            int n=0; for(int t2=lastp+1;t2<ns;t2++){ if(n&&n<198)g_coll_rest[n++]='|'; for(int k=0;st[t2][k]&&n<198;k++)g_coll_rest[n++]=st[t2][k]; } g_coll_rest[n]=0; } }
        if(inf[0]&&jc_openinto(i,0,inf,O_RDONLY)){ tcol(RED); term_puts("nsh: "); term_puts(inf); term_puts(": no such file\n"); tcol(WHT); }
        if(outf[0])jc_openinto(i,1,outf,O_CREAT|O_WRONLY|(app?O_APPEND:O_TRUNC));
        if(errf[0])jc_openinto(i,2,errf,O_CREAT|O_WRONLY|(eapp?O_APPEND:O_TRUNC));
        if(e2o){ vf_unref(p->fd[2]); p->fd[2]=p->fd[1]; vf_ref(p->fd[1]); }
        pids[np]=p->pid; idxs[np]=i; np++;
    }
    if(g_coll_vi>=0&&tail)g_coll_pgid=pgid;
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
int jc_builtin(const char* cmd,const char* arg){
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
        int r; g_kill_check=1; if(*a=='%'){ struct job* b=jc_jobarg(a); r=b?jc_kill(-b->pgid,sig,-1):-1; if(b&&sig==18)b->state=1; }
        else { int pid=0,neg=0; if(*a=='-'){neg=1;a++;} while(*a>='0'&&*a<='9')pid=pid*10+(*a++-'0'); r=jc_kill(neg?-pid:pid,sig,-1); } g_kill_check=0;
        if(g_kill_denied&&r<=0){ tcol(RED); term_puts("kill: permission denied - that process belongs to another user (try sudo)\n"); tcol(WHT); return 1; }
        if(r<0){ tcol(RED); term_puts("kill: no such process\n"); tcol(WHT); } g_js_dirty=1; return 1; }
    if(seq((char*)cmd,"ps")){ tcol(GREY); term_puts("  PID  PPID  PGID  STAT  USER      COMMAND\n"); tcol(WHT);
        term_puts("    0     0     0  S     root      nsh (kernel shell)\n");
        for(int i=0;i<JMAX;i++){ struct jproc* p=&g_jp[i]; if(p->state==S_FREE)continue; char m[96],*q=m;
            u32 v[3]={(u32)p->pid,(u32)p->ppid,(u32)p->pgid}; for(int k=0;k<3;k++){ char nb[12]; char* e=udec(nb,v[k]); int L=(int)(e-nb); for(int s=L;s<5;s++)*q++=' '; for(int s=0;s<L;s++)*q++=nb[s]; *q++=' '; }
            q=scopy(q," "); const char* ss=p->state==S_RUN?"R":p->state==S_STOP?"T":p->state==S_ZOMB?"Z":"S"; q=scopy(q,ss); if(p->pgid==g_tty_fg)*q++='+'; else *q++=' '; { char un[32]; if(!ux_uid_name(p->uid,un,32)){ char* z=un; z=udec(z,(u32)p->uid); *z=0; } *q++=' '; *q++=' '; *q++=' '; *q++=' '; int L=0; while(un[L]&&L<9){ *q++=un[L]; L++; } while(L<10){ *q++=' '; L++; } }
            q=scopy(q,"    "); q=scopy(q,p->cmd); *q=0; term_puts(m); term_putc('\n'); }
        return 1; }
    if(seq((char*)cmd,"umask")){ const char* a=arg; while(*a==' ')a++; if(!*a){ char m[8]; m[0]='0'; m[1]='0'+((g_umask>>6)&7); m[2]='0'+((g_umask>>3)&7); m[3]='0'+(g_umask&7); m[4]='\n'; m[5]=0; term_puts(m); }
        else { int v=0; while(*a>='0'&&*a<='7')v=v*8+(*a++-'0'); g_umask=v&0777; } return 1; }
    if(seq((char*)cmd,"wait")){ int n=0; for(int j=0;j<JOBMAX;j++) if(g_jobs2[j].used)n++; if(n){ char m[48],*q=m; q=udec(q,(u32)n); q=scopy(q," job(s) still running - use fg to wait"); *q=0; term_puts(m); term_putc('\n'); } return 1; }
    return 0;
}
int dt_busy(int d){
    if(d==g_dcwd)return 1;
    for(int i=0;i<JMAX;i++){ if(g_jp[i].state==S_FREE||g_jp[i].state==S_ZOMB||i==g_jcur)continue; int rc=g_jp[i].runcpu; int cw=rc?g_cpu[rc-1].dcwd:g_jp[i].cwd; if(cw==d)return 1; }
    for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==11&&g_vf[i].fs_idx==d)return 1;
    return 0;
}
void dt_setmode(int n,unsigned m){ if(n>=0&&n<DN_MAX)DT[n].mode=m; }
