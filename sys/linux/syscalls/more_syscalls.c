static long lx_close(int fd);
#define LXM_NO (-0x7FFF0000L)
#define LXM_ENXIO 6
#define LXM_EXDEV 18
#define LXM_EFBIG 27
#define LXM_ELOOP 40
#define LXM_ENOMSG 42
#define LXM_EIDRM 43
#define LXM_EMSGSIZE 90
#define LXM_EOPNOTSUPP 95
#define LXM_ETIMEDOUT 110
#define LX_MQD 25
#define LX_PIDFD 26
static u8* g_kbuf;
static long lxm_kio(struct sysregs* r,int fd,u8* kb,u64 len,int wr,long off){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    int vi=fdt()[fd]; int had=v->flags&0x800;
    v->flags|=0x800; g_kio_vi=vi; g_kio_nb=had;
    g_kb_lo=(u64)kb; g_kb_n=len;
    long n=off>=0?lx_prw(fd,(u64)kb,len,off,wr):(wr?lx_write(r,fd,(u64)kb,len):lx_read(r,fd,(u64)kb,len));
    g_kb_n=0; if(!had)g_vf[vi].flags&=~0x800; g_kio_vi=-1;
    return n;
}
static int lxm_kbuf(void){ if(!g_kbuf)g_kbuf=(u8*)kmalloc(65536); return g_kbuf!=0; }
static u64 lxm_now_s(void){ return lx_now_ns()/1000000000ULL; }
static int lxm_ipc_ok(int uid,int gid,int cuid,int cgid,u32 mode,int want){
    if(g_uid==0)return 1;
    u32 m=mode; if(g_uid==uid||g_uid==cuid)m>>=6; else if(g_gid==gid||g_gid==cgid)m>>=3;
    return ((int)(m&7)&want)==want;
}
static void lxm_perm_out(u8* o,int key,int uid,int gid,int cuid,int cgid,u32 mode,int seq){
    lx_zero(o,48); *(int*)o=key; *(u32*)(o+4)=(u32)uid; *(u32*)(o+8)=(u32)gid; *(u32*)(o+12)=(u32)cuid; *(u32*)(o+16)=(u32)cgid; *(u16*)(o+20)=(u16)mode; *(u16*)(o+24)=(u16)seq;
}
static int lxm_deadline(struct jproc* p,u64 ts,int abs_rt,long* ms){
    *ms=-1; if(!ts)return 0;
    u64 t[2]; if(lx_get(t,ts,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
    if(t[0]>0x3FFFFFFFFULL)t[0]=0x3FFFFFFFFULL;
    u64 ns=t[0]*1000000000ULL+t[1];
    if(abs_rt){ u64 now=lx_now_ns(); ns=ns>now?ns-now:0; }
    (void)p; *ms=(long)(ns/1000000ULL); if(ns&&!*ms)*ms=1; return 0;
}
#define SEMN 64
#define SEMMSL 64
struct lxsem { int used, key, nsems, seq, uid, gid, cuid, cgid; u32 mode; u64 otime, ctime; u16 val[SEMMSL]; int pid[SEMMSL]; };
static struct lxsem g_sem[SEMN]; static int g_sem_seq;
struct lxundo { int used, pid, id; short adj[SEMMSL]; };
static struct lxundo g_semun[128];
static int g_semw_id[JMAX], g_semw_num[JMAX], g_semw_z[JMAX];
static struct lxsem* lxm_sem(int id){ if(id<0)return 0; int i=id%SEMN; struct lxsem* s=&g_sem[i]; if(!s->used||s->seq!=id/SEMN)return 0; return s; }
static int lxm_sem_id(int i){ return i+g_sem[i].seq*SEMN; }
static long lxm_semget(int key,int nsems,int flg){
    if(nsems<0||nsems>SEMMSL)return -LX_EINVAL;
    if(key!=0) for(int i=0;i<SEMN;i++){ struct lxsem* s=&g_sem[i]; if(!s->used||s->key!=key)continue;
        if((flg&01000)&&(flg&02000))return -LX_EEXIST; if(nsems>s->nsems)return -LX_EINVAL;
        if(!lxm_ipc_ok(s->uid,s->gid,s->cuid,s->cgid,s->mode,((flg&0222)?2:0)|((flg&0444)?4:0)))return -LX_EACCES;
        return lxm_sem_id(i); }
    if(key!=0&&!(flg&01000))return -LX_ENOENT;
    if(nsems==0)return -LX_EINVAL;
    for(int i=0;i<SEMN;i++){ struct lxsem* s=&g_sem[i]; if(s->used)continue;
        int seq=(++g_sem_seq)&0x3FFF; if(!seq)seq=++g_sem_seq&0x3FFF; lx_zero(s,sizeof *s);
        s->used=1; s->key=key; s->nsems=nsems; s->seq=seq; s->uid=s->cuid=g_uid; s->gid=s->cgid=g_gid; s->mode=(u32)flg&0777; s->ctime=lxm_now_s();
        return lxm_sem_id(i); }
    return -LX_ENOSPC;
}
static struct lxundo* lxm_undo(int pid,int id,int make){
    for(int i=0;i<128;i++) if(g_semun[i].used&&g_semun[i].pid==pid&&g_semun[i].id==id)return &g_semun[i];
    if(!make)return 0;
    for(int i=0;i<128;i++) if(!g_semun[i].used){ lx_zero(&g_semun[i],sizeof g_semun[i]); g_semun[i].used=1; g_semun[i].pid=pid; g_semun[i].id=id; return &g_semun[i]; }
    return 0;
}
static long lxm_semop(struct sysregs* r,int id,u64 sops,u64 nsops,u64 ts){
    struct jproc* p=&g_jp[g_jcur]; int me=g_jcur;
    if(nsops<1)return -LX_EINVAL; if(nsops>64)return -LX_E2BIG;
    u8 ops[64*6]; if(lx_get(ops,sops,nsops*6))return -LX_EFAULT;
    struct lxsem* s=lxm_sem(id);
    if(!s){ int was=g_semw_id[me]==id+1; g_semw_id[me]=0; p->pollwake=0; p->wdl=0; return was?-LXM_EIDRM:-LX_EINVAL; }
    int alter=0, undo=0; for(u64 i=0;i<nsops;i++){ u16 n=*(u16*)(ops+i*6); short op=*(short*)(ops+i*6+2); short fl=*(short*)(ops+i*6+4); if(n>=s->nsems){ g_semw_id[me]=0; return -LXM_EFBIG; } if(op)alter=1; if(fl&0x1000)undo=1; }
    if(!lxm_ipc_ok(s->uid,s->gid,s->cuid,s->cgid,s->mode,alter?2:4)){ g_semw_id[me]=0; return -LX_EACCES; }
    u16 tv[SEMMSL]; for(int i=0;i<s->nsems;i++)tv[i]=s->val[i];
    int blk=-1, bz=0;
    for(u64 i=0;i<nsops&&blk<0;i++){ u16 n=*(u16*)(ops+i*6); short op=*(short*)(ops+i*6+2);
        if(op>0){ if((int)tv[n]+op>32767){ g_semw_id[me]=0; return -LX_ERANGE; } tv[n]=(u16)(tv[n]+op); }
        else if(op==0){ if(tv[n]){ blk=n; bz=1; } }
        else { if((int)tv[n]+op<0){ blk=n; bz=0; } else tv[n]=(u16)(tv[n]+op); } }
    if(blk>=0){
        short fl=0; for(u64 i=0;i<nsops;i++) if(*(u16*)(ops+i*6)==blk)fl|=*(short*)(ops+i*6+4);
        if(fl&04000){ g_semw_id[me]=0; return -LX_EAGAIN; }
        if(jc_deliverable(p)){ g_semw_id[me]=0; p->pollwake=0; p->wdl=0; return -LX_EINTR; }
        if(ts){ long ms; int e=lxm_deadline(p,ts,0,&ms); if(e){ g_semw_id[me]=0; return e; } if(lx_wait_deadline(p,ms)){ g_semw_id[me]=0; return -LX_EAGAIN; } }
        g_semw_id[me]=id+1; g_semw_num[me]=blk; g_semw_z[me]=bz;
        jc_block(r,S_WAIT,g_ticks+1); }
    g_semw_id[me]=0; p->pollwake=0; p->wdl=0;
    int tgp=jc_lead(p)->pid;
    if(undo){ struct lxundo* u=lxm_undo(tgp,id,1); if(!u)return -LX_ENOSPC;
        for(u64 i=0;i<nsops;i++){ u16 n=*(u16*)(ops+i*6); short op=*(short*)(ops+i*6+2); short fl=*(short*)(ops+i*6+4); if(!(fl&0x1000)||!op)continue; int a=u->adj[n]-op; if(a<-32768||a>32767)return -LX_ERANGE; }
        for(u64 i=0;i<nsops;i++){ u16 n=*(u16*)(ops+i*6); short op=*(short*)(ops+i*6+2); short fl=*(short*)(ops+i*6+4); if((fl&0x1000)&&op)u->adj[n]=(short)(u->adj[n]-op); } }
    for(int i=0;i<s->nsems;i++){ if(tv[i]!=s->val[i])s->pid[i]=tgp; s->val[i]=tv[i]; }
    for(u64 i=0;i<nsops;i++)s->pid[*(u16*)(ops+i*6)]=tgp;
    s->otime=lxm_now_s();
    return 0;
}
static void lxm_sem_rm(int id){ struct lxsem* s=lxm_sem(id); if(!s)return; s->used=0; for(int i=0;i<128;i++) if(g_semun[i].used&&g_semun[i].id==id)g_semun[i].used=0; }
static long lxm_semctl(int id,int num,int cmd,u64 arg){
    cmd&=~0x100;
    if(cmd==3||cmd==19){ int top=0; int ns=0; for(int i=0;i<SEMN;i++) if(g_sem[i].used){ top=i; ns+=g_sem[i].nsems; }
        int si[10]={0,SEMN,SEMN*SEMMSL,SEMN*SEMMSL,SEMMSL,64,SEMMSL,0,32767,0}; if(cmd==19){ int c=0; for(int i=0;i<SEMN;i++)if(g_sem[i].used)c++; si[7]=c; si[9]=ns; }
        if(lx_put(arg,si,40))return -LX_EFAULT; return top; }
    struct lxsem* s;
    if(cmd==18||cmd==20){ if(id<0||id>=SEMN||!g_sem[id].used)return -LX_EINVAL; s=&g_sem[id]; if(cmd==18&&!lxm_ipc_ok(s->uid,s->gid,s->cuid,s->cgid,s->mode,4))return -LX_EACCES; }
    else { s=lxm_sem(id); if(!s)return -LX_EINVAL; }
    int own=g_uid==0||g_uid==s->uid||g_uid==s->cuid;
    switch(cmd){
    case 0: if(!own)return -LX_EPERM; lxm_sem_rm(id); return 0;
    case 1: { u8 b[48]; if(lx_get(b,arg,48))return -LX_EFAULT; if(!own)return -LX_EPERM; s->uid=(int)*(u32*)(b+4); s->gid=(int)*(u32*)(b+8); s->mode=(s->mode&~0777u)|(*(u16*)(b+20)&0777u); s->ctime=lxm_now_s(); return 0; }
    case 2: case 18: case 20: { if(cmd==2&&!lxm_ipc_ok(s->uid,s->gid,s->cuid,s->cgid,s->mode,4))return -LX_EACCES; u8 b[104]; lx_zero(b,104); lxm_perm_out(b,s->key,s->uid,s->gid,s->cuid,s->cgid,s->mode,s->seq); *(u64*)(b+48)=s->otime; *(u64*)(b+64)=s->ctime; *(u64*)(b+80)=(u64)s->nsems; if(lx_put(arg,b,104))return -LX_EFAULT; return cmd==2?0:lxm_sem_id((int)(s-g_sem)); }
    }
    if(!lxm_ipc_ok(s->uid,s->gid,s->cuid,s->cgid,s->mode,(cmd==16||cmd==17)?2:4))return -LX_EACCES;
    switch(cmd){
    case 11: case 12: case 14: case 15: if(num<0||num>=s->nsems)return -LX_EINVAL;
        if(cmd==11)return s->pid[num]; if(cmd==12)return s->val[num];
        { int c=0; for(int j=0;j<JMAX;j++) if(g_semw_id[j]==id+1&&g_semw_num[j]==num&&g_semw_z[j]==(cmd==15)&&g_jp[j].state!=S_FREE&&g_jp[j].state!=S_ZOMB)c++; return c; }
    case 13: { u16 v[SEMMSL]; for(int i=0;i<s->nsems;i++)v[i]=s->val[i]; return lx_put(arg,v,(u64)s->nsems*2)?-LX_EFAULT:0; }
    case 16: { if(num<0||num>=s->nsems)return -LX_EINVAL; int v=(int)arg; if(v<0||v>32767)return -LX_ERANGE; s->val[num]=(u16)v; s->pid[num]=jc_lead(&g_jp[g_jcur])->pid; s->ctime=lxm_now_s(); for(int i=0;i<128;i++) if(g_semun[i].used&&g_semun[i].id==id)g_semun[i].adj[num]=0; return 0; }
    case 17: { u16 v[SEMMSL]; if(lx_get(v,arg,(u64)s->nsems*2))return -LX_EFAULT; for(int i=0;i<s->nsems;i++) if(v[i]>32767)return -LX_ERANGE; int tp=jc_lead(&g_jp[g_jcur])->pid; for(int i=0;i<s->nsems;i++){ s->val[i]=v[i]; s->pid[i]=tp; } s->ctime=lxm_now_s(); for(int i=0;i<128;i++) if(g_semun[i].used&&g_semun[i].id==id)lx_zero(g_semun[i].adj,sizeof g_semun[i].adj); return 0; }
    }
    return -LX_EINVAL;
}
#define MSQN 32
#define LXM_MSGMAX 8192
#define LXM_MSGMNB 16384
struct lxmsg { struct lxmsg* next; long type; int len; u8 data[]; };
struct lxmsq { int used, key, seq, uid, gid, cuid, cgid; u32 mode; u64 stime, rtime, ctime; int lspid, lrpid; u32 qbytes, cbytes, qnum; struct lxmsg *head, *tail; };
static struct lxmsq g_msq[MSQN]; static int g_msq_seq; static int g_msqw[JMAX];
static struct lxmsq* lxm_msq(int id){ if(id<0)return 0; int i=id%MSQN; struct lxmsq* q=&g_msq[i]; if(!q->used||q->seq!=id/MSQN)return 0; return q; }
static int lxm_msq_id(int i){ return i+g_msq[i].seq*MSQN; }
static long lxm_msgget(int key,int flg){
    if(key!=0) for(int i=0;i<MSQN;i++){ struct lxmsq* q=&g_msq[i]; if(!q->used||q->key!=key)continue;
        if((flg&01000)&&(flg&02000))return -LX_EEXIST;
        if(!lxm_ipc_ok(q->uid,q->gid,q->cuid,q->cgid,q->mode,((flg&0222)?2:0)|((flg&0444)?4:0)))return -LX_EACCES;
        return lxm_msq_id(i); }
    if(key!=0&&!(flg&01000))return -LX_ENOENT;
    for(int i=0;i<MSQN;i++){ struct lxmsq* q=&g_msq[i]; if(q->used)continue;
        int seq=(++g_msq_seq)&0x3FFF; if(!seq)seq=++g_msq_seq&0x3FFF; lx_zero(q,sizeof *q);
        q->used=1; q->key=key; q->seq=seq; q->uid=q->cuid=g_uid; q->gid=q->cgid=g_gid; q->mode=(u32)flg&0777; q->qbytes=LXM_MSGMNB; q->ctime=lxm_now_s();
        return lxm_msq_id(i); }
    return -LX_ENOSPC;
}
static void lxm_msq_free(struct lxmsq* q){ struct lxmsg* m=q->head; while(m){ struct lxmsg* n=m->next; kfree(m); m=n; } q->head=q->tail=0; q->used=0; q->cbytes=0; q->qnum=0; }
static long lxm_msq_wait(struct sysregs* r,int id,int flg,int nomsg){
    struct jproc* p=&g_jp[g_jcur];
    if(flg&04000){ g_msqw[g_jcur]=0; return nomsg?-LXM_ENOMSG:-LX_EAGAIN; }
    if(jc_deliverable(p)){ g_msqw[g_jcur]=0; return -LX_EINTR; }
    g_msqw[g_jcur]=id+1; jc_block(r,S_WAIT,g_ticks+1);
}
static long lxm_msgsnd(struct sysregs* r,int id,u64 mp,u64 sz,int flg){
    struct lxmsq* q=lxm_msq(id);
    if(!q){ int was=g_msqw[g_jcur]==id+1; g_msqw[g_jcur]=0; return was?-LXM_EIDRM:-LX_EINVAL; }
    if((long)sz<0||sz>LXM_MSGMAX){ g_msqw[g_jcur]=0; return -LX_EINVAL; }
    long ty; if(lx_get(&ty,mp,8)){ g_msqw[g_jcur]=0; return -LX_EFAULT; } if(ty<1){ g_msqw[g_jcur]=0; return -LX_EINVAL; }
    if(!lxm_ipc_ok(q->uid,q->gid,q->cuid,q->cgid,q->mode,2)){ g_msqw[g_jcur]=0; return -LX_EACCES; }
    if(q->cbytes+sz>q->qbytes||q->qnum+1>q->qbytes)return lxm_msq_wait(r,id,flg,0);
    if(!lx_ok(mp+8,sz)){ g_msqw[g_jcur]=0; return -LX_EFAULT; }
    struct lxmsg* m=(struct lxmsg*)kmalloc(sizeof(struct lxmsg)+sz+1); if(!m){ g_msqw[g_jcur]=0; return -LX_ENOMEM; }
    m->next=0; m->type=ty; m->len=(int)sz; if(sz&&lx_get(m->data,mp+8,sz)){ kfree(m); g_msqw[g_jcur]=0; return -LX_EFAULT; }
    if(q->tail)q->tail->next=m; else q->head=m; q->tail=m; q->cbytes+=(u32)sz; q->qnum++;
    q->lspid=jc_lead(&g_jp[g_jcur])->pid; q->stime=lxm_now_s(); g_msqw[g_jcur]=0;
    return 0;
}
static long lxm_msgrcv(struct sysregs* r,int id,u64 mp,u64 sz,long typ,int flg){
    struct lxmsq* q=lxm_msq(id);
    if(!q){ int was=g_msqw[g_jcur]==id+1; g_msqw[g_jcur]=0; return was?-LXM_EIDRM:-LX_EINVAL; }
    if((long)sz<0){ g_msqw[g_jcur]=0; return -LX_EINVAL; }
    if(!lxm_ipc_ok(q->uid,q->gid,q->cuid,q->cgid,q->mode,4)){ g_msqw[g_jcur]=0; return -LX_EACCES; }
    int copy=(flg&040000)!=0; if(copy&&(!(flg&04000)||(flg&020000)))return -LX_EINVAL;
    struct lxmsg *m=q->head, *prev=0, *best=0, *bprev=0; long k=0;
    for(;m;prev=m,m=m->next,k++){
        if(copy){ if(k==typ){ best=m; bprev=prev; break; } continue; }
        if(typ==0){ best=m; bprev=prev; break; }
        if(typ>0){ if((flg&020000)?(m->type!=typ):(m->type==typ)){ best=m; bprev=prev; break; } }
        else if(m->type<=-typ&&(!best||m->type<best->type)){ best=m; bprev=prev; } }
    if(!best)return lxm_msq_wait(r,id,flg|(copy?04000:0),1);
    g_msqw[g_jcur]=0;
    if((u64)best->len>sz&&!(flg&010000))return -LX_E2BIG;
    int n=(u64)best->len>sz?(int)sz:best->len;
    if(lx_put(mp,&best->type,8)||(n&&lx_put(mp+8,best->data,(u64)n)))return -LX_EFAULT;
    if(!copy){ if(bprev)bprev->next=best->next; else q->head=best->next; if(q->tail==best)q->tail=bprev; q->cbytes-=(u32)best->len; q->qnum--; kfree(best); q->lrpid=jc_lead(&g_jp[g_jcur])->pid; q->rtime=lxm_now_s(); }
    return n;
}
static long lxm_msgctl(int id,int cmd,u64 buf){
    cmd&=~0x100;
    if(cmd==3||cmd==12){ int top=0, c=0; u32 nb=0, nm=0; for(int i=0;i<MSQN;i++) if(g_msq[i].used){ top=i; c++; nb+=g_msq[i].cbytes; nm+=g_msq[i].qnum; }
        u8 b[32]; lx_zero(b,32); int* w=(int*)b; w[0]=cmd==12?c:LXM_MSGMNB; w[1]=cmd==12?(int)nm:LXM_MSGMNB; w[2]=LXM_MSGMAX; w[3]=LXM_MSGMNB; w[4]=MSQN; w[5]=16; w[6]=cmd==12?(int)nb:LXM_MSGMNB; *(u16*)(b+28)=0xFFFF;
        if(lx_put(buf,b,32))return -LX_EFAULT; return top; }
    struct lxmsq* q;
    if(cmd==11||cmd==13){ if(id<0||id>=MSQN||!g_msq[id].used)return -LX_EINVAL; q=&g_msq[id]; if(cmd==11&&!lxm_ipc_ok(q->uid,q->gid,q->cuid,q->cgid,q->mode,4))return -LX_EACCES; }
    else { q=lxm_msq(id); if(!q)return -LX_EINVAL; }
    int own=g_uid==0||g_uid==q->uid||g_uid==q->cuid;
    switch(cmd){
    case 0: if(!own)return -LX_EPERM; lxm_msq_free(q); return 0;
    case 1: { u8 b[120]; if(lx_get(b,buf,120))return -LX_EFAULT; if(!own)return -LX_EPERM; u64 qb=*(u64*)(b+88); if(qb>LXM_MSGMNB&&g_uid!=0)return -LX_EPERM; if(qb==0||qb>0x7FFFFFFFULL)return -LX_EINVAL;
        q->uid=(int)*(u32*)(b+4); q->gid=(int)*(u32*)(b+8); q->mode=(q->mode&~0777u)|(*(u16*)(b+20)&0777u); q->qbytes=(u32)qb; q->ctime=lxm_now_s(); return 0; }
    case 2: case 11: case 13: { if(cmd==2&&!lxm_ipc_ok(q->uid,q->gid,q->cuid,q->cgid,q->mode,4))return -LX_EACCES; u8 b[120]; lx_zero(b,120); lxm_perm_out(b,q->key,q->uid,q->gid,q->cuid,q->cgid,q->mode,q->seq);
        *(u64*)(b+48)=q->stime; *(u64*)(b+56)=q->rtime; *(u64*)(b+64)=q->ctime; *(u64*)(b+72)=q->cbytes; *(u64*)(b+80)=q->qnum; *(u64*)(b+88)=q->qbytes; *(int*)(b+96)=q->lspid; *(int*)(b+100)=q->lrpid;
        if(lx_put(buf,b,120))return -LX_EFAULT; return cmd==2?0:lxm_msq_id((int)(q-g_msq)); }
    }
    return -LX_EINVAL;
}
#define MQN 32
struct lxmqm { struct lxmqm* next; u32 prio; int len; u8 data[]; };
struct lxmq { int used, refs, unlinked, uid, gid; u32 mode; long maxmsg, msgsize, cur; char name[64]; struct lxmqm* head; int npid, nsig, nset; u64 nval; };
static struct lxmq g_pmq[MQN];
static void lxm_mq_free(struct lxmq* q){ struct lxmqm* m=q->head; while(m){ struct lxmqm* n=m->next; kfree(m); m=n; } q->head=0; q->used=0; }
static int lxm_mq_find(const char* nm){ for(int i=0;i<MQN;i++) if(g_pmq[i].used&&!g_pmq[i].unlinked&&lx_streq(g_pmq[i].name,nm))return i; return -1; }
static long lxm_mq_name(u64 up,char* nm){ int n=lx_str(up,nm,64); if(n<0)return n==-LX_ENAMETOOLONG?-LX_ENAMETOOLONG:n; if(n==0)return -LX_ENOENT; for(int i=0;i<n;i++) if(nm[i]=='/')return -LX_EACCES; if(lx_streq(nm,".")||lx_streq(nm,".."))return -LX_EINVAL; return n; }
static long lxm_mq_open(u64 up,int flg,int mode,u64 attr){
    char nm[64]; long e=lxm_mq_name(up,nm); if(e<0)return e;
    int acc=flg&3; if(acc==3)return -LX_EINVAL;
    int i=lxm_mq_find(nm);
    if(i>=0){ if((flg&0x40)&&(flg&0x80))return -LX_EEXIST; struct lxmq* q=&g_pmq[i]; int want=acc==0?4:acc==1?2:6; if(!lxm_ipc_ok(q->uid,q->gid,q->uid,q->gid,q->mode,want))return -LX_EACCES; }
    else { if(!(flg&0x40))return -LX_ENOENT;
        long mx=10, ms=8192;
        if(attr){ long a[4]; if(lx_get(a,attr,32))return -LX_EFAULT; if(a[1]<=0||a[2]<=0||a[1]>256||a[2]>65536)return -LX_EINVAL; mx=a[1]; ms=a[2]; }
        if(g_uid!=0){ long tot=mx*ms; for(int k=0;k<MQN;k++) if(g_pmq[k].used&&g_pmq[k].uid==g_uid)tot+=g_pmq[k].maxmsg*g_pmq[k].msgsize; if(tot>819200)return -LX_EMFILE; }
        for(int k=0;k<MQN;k++) if(!g_pmq[k].used){ i=k; break; } if(i<0)return -LX_ENOSPC;
        struct lxmq* q=&g_pmq[i]; lx_zero(q,sizeof *q); q->used=1; q->uid=g_uid; q->gid=g_gid; q->mode=(u32)mode&0777&~(u32)g_umask; q->maxmsg=mx; q->msgsize=ms; int k2=0; for(;nm[k2]&&k2<63;k2++)q->name[k2]=nm[k2]; q->name[k2]=0; }
    int vi=jc_newvf(LX_MQD,i,acc|(flg&0x800)); if(vi<0){ if(!g_pmq[i].refs&&!(flg&0x40))return -LX_EMFILE; if(!g_pmq[i].refs&&g_pmq[i].cur==0&&!g_pmq[i].head){ } return -LX_EMFILE; }
    g_pmq[i].refs++;
    int fd=lx_fd_install(vi,1,0); if(fd<0){ vf_unref(vi); return fd; }
    return fd;
}
static long lxm_mq_unlink(u64 up){
    char nm[64]; long e=lxm_mq_name(up,nm); if(e<0)return e;
    int i=lxm_mq_find(nm); if(i<0)return -LX_ENOENT; struct lxmq* q=&g_pmq[i];
    if(g_uid!=0&&g_uid!=q->uid)return -LX_EACCES;
    q->unlinked=1; if(!q->refs)lxm_mq_free(q); return 0;
}
static struct lxmq* lxm_mq_of(int fd,struct vfile** vo){ struct vfile* v=lx_vf(fd); if(!v||v->dev!=LX_MQD)return 0; if(vo)*vo=v; return &g_pmq[v->fs_idx]; }
static long lxm_mq_send(struct sysregs* r,int fd,u64 buf,u64 len,u32 prio,u64 ts){
    struct jproc* p=&g_jp[g_jcur]; struct vfile* v; struct lxmq* q=lxm_mq_of(fd,&v); if(!q)return -LX_EBADF;
    if((v->flags&3)==0)return -LX_EBADF;
    if(len>(u64)q->msgsize)return -LXM_EMSGSIZE; if(prio>=32768)return -LX_EINVAL;
    if(q->cur>=q->maxmsg){
        if(lx_nonblock(v))return -LX_EAGAIN;
        if(jc_deliverable(p)){ p->pollwake=0; p->wdl=0; return -LX_EINTR; }
        if(ts){ long ms; int e=lxm_deadline(p,ts,1,&ms); if(e)return e; if(lx_wait_deadline(p,ms))return -LXM_ETIMEDOUT; }
        jc_block(r,S_WAIT,g_ticks+1); }
    p->pollwake=0; p->wdl=0;
    struct lxmqm* m=(struct lxmqm*)kmalloc(sizeof(struct lxmqm)+len+1); if(!m)return -LX_ENOMEM;
    m->prio=prio; m->len=(int)len; m->next=0; if(len&&lx_get(m->data,buf,len)){ kfree(m); return -LX_EFAULT; }
    int was_empty=q->cur==0;
    struct lxmqm** pp=&q->head; while(*pp&&(*pp)->prio>=prio)pp=&(*pp)->next; m->next=*pp; *pp=m; q->cur++;
    if(was_empty&&q->nset){ q->nset=0; int j=jc_idx(q->npid); if(j>=0&&q->nsig>0){ jc_sig_set_info(-3,jc_lead(p)->pid,g_uid,q->nval); jc_signal_proc(j,q->nsig); } }
    return 0;
}
static long lxm_mq_recv(struct sysregs* r,int fd,u64 buf,u64 len,u64 uprio,u64 ts){
    struct jproc* p=&g_jp[g_jcur]; struct vfile* v; struct lxmq* q=lxm_mq_of(fd,&v); if(!q)return -LX_EBADF;
    if((v->flags&3)==1)return -LX_EBADF;
    if(len<(u64)q->msgsize)return -LXM_EMSGSIZE;
    if(!q->head){
        if(lx_nonblock(v))return -LX_EAGAIN;
        if(jc_deliverable(p)){ p->pollwake=0; p->wdl=0; return -LX_EINTR; }
        if(ts){ long ms; int e=lxm_deadline(p,ts,1,&ms); if(e)return e; if(lx_wait_deadline(p,ms))return -LXM_ETIMEDOUT; }
        jc_block(r,S_WAIT,g_ticks+1); }
    p->pollwake=0; p->wdl=0;
    struct lxmqm* m=q->head;
    if(m->len&&lx_put(buf,m->data,(u64)m->len))return -LX_EFAULT;
    if(uprio){ u32 pr=m->prio; if(lx_put(uprio,&pr,4))return -LX_EFAULT; }
    q->head=m->next; q->cur--; int n=m->len; kfree(m);
    return n;
}
static long lxm_mq_notify(int fd,u64 sev){
    struct lxmq* q=lxm_mq_of(fd,0); if(!q)return -LX_EBADF;
    int me=jc_lead(&g_jp[g_jcur])->pid;
    if(!sev){ if(q->nset&&q->npid==me)q->nset=0; return 0; }
    u8 s[64]; if(lx_get(s,sev,64))return -LX_EFAULT;
    int signo=*(int*)(s+8), how=*(int*)(s+12);
    if(q->nset&&jc_idx(q->npid)>=0)return -LX_EBUSY;
    if(how==1){ q->nset=1; q->npid=me; q->nsig=0; return 0; }
    if(how!=0)return -LX_EINVAL;
    if(signo<=0||signo>64)return -LX_EINVAL;
    q->nset=1; q->npid=me; q->nsig=signo; q->nval=*(u64*)s; return 0;
}
static long lxm_mq_attr(int fd,u64 nw,u64 old){
    struct vfile* v; struct lxmq* q=lxm_mq_of(fd,&v); if(!q)return -LX_EBADF;
    long na[8]; if(nw&&lx_get(na,nw,64))return -LX_EFAULT;
    if(old){ long a[8]; lx_zero(a,64); a[0]=lx_nonblock(v)?0x800:0; a[1]=q->maxmsg; a[2]=q->msgsize; a[3]=q->cur; if(lx_put(old,a,64))return -LX_EFAULT; }
    if(nw){ if(na[0]&0x800)v->flags|=0x800; else v->flags&=~0x800; }
    return 0;
}
void lxm_vf_close(int dev,int idx){
    if(dev==LX_MQD&&idx>=0&&idx<MQN){ struct lxmq* q=&g_pmq[idx]; if(q->used&&q->refs>0){ q->refs--; if(!q->refs&&q->unlinked)lxm_mq_free(q); } }
}
static int lxm_proc_gone(int pid){ int j=jc_idx(pid); if(j<0)return 1; int s=g_jp[j].state; return s==S_FREE||s==S_ZOMB; }
static long lxm_pidfd_open(int pid,u32 flags){
    if(flags&~(0x800u|0x80u))return -LX_EINVAL; if(pid<=0)return -LX_EINVAL;
    int j=jc_idx(pid); if(j<0||g_jp[j].state==S_FREE)return -LX_ESRCH;
    if(!(flags&0x80)&&g_jp[j].tg!=j)return -LX_ENOENT;
    int vi=jc_newvf(LX_PIDFD,pid,2|(int)(flags&0x800)); if(vi<0)return -LX_EMFILE;
    int fd=lx_fd_install(vi,1,0); if(fd<0)vf_unref(vi); return fd;
}
static int lxm_pidfd_pid(int fd){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_PIDFD)return -LX_EINVAL; return v->fs_idx; }
static long lxm_pidfd_signal(int fd,int sig,u64 info,u32 flags){
    int pid=lxm_pidfd_pid(fd); if(pid<0)return pid; if(flags)return -LX_EINVAL; if(sig<0||sig>64)return -LX_EINVAL;
    if(lxm_proc_gone(pid))return -LX_ESRCH;
    struct jproc* p=&g_jp[g_jcur];
    if(info){ u8 qi[128]; if(lx_get(qi,info,128))return -LX_EFAULT; if(*(int*)qi!=sig)return -LX_EINVAL; int code=*(int*)(qi+8); if(code>=0&&pid!=jc_lead(p)->pid)return -LX_EPERM; if(sig)jc_sig_set_info(code,jc_lead(p)->pid,g_uid,*(u64*)(qi+24)); }
    else if(sig)jc_sig_set_info(0,jc_lead(p)->pid,g_uid,0);
    g_kill_check=1; int rv=jc_kill(pid,sig,g_jcur); g_kill_check=0;
    if(rv<0)return g_kill_denied?-LX_EPERM:-LX_ESRCH; return 0;
}
static long lxm_pidfd_getfd(int fd,int tfd,u32 flags){
    int pid=lxm_pidfd_pid(fd); if(pid<0)return pid; if(flags)return -LX_EINVAL;
    int j=jc_idx(pid); if(j<0||lxm_proc_gone(pid))return -LX_ESRCH;
    struct jproc* t=jc_lead(&g_jp[j]); if(g_uid!=0&&t->uid!=g_uid)return -LX_EPERM;
    if(tfd<0||tfd>=FDMAX||t->fd[tfd]<0||!g_vf[t->fd[tfd]].used)return -LX_EBADF;
    int vi=t->fd[tfd]; vf_ref(vi);
    int nf=lx_fd_install(vi,1,0); if(nf<0)vf_unref(vi); return nf;
}
static int lxm_cmp(u64 a,u64 b){ return a==b?0:(a<b?1:2); }
static long lxm_kcmp(int p1,int p2,int type,u64 i1,u64 i2){
    int j1=jc_idx(p1), j2=jc_idx(p2); if(j1<0||j2<0||lxm_proc_gone(p1)||lxm_proc_gone(p2))return -LX_ESRCH;
    struct jproc *a=&g_jp[j1], *b=&g_jp[j2];
    if(g_uid!=0&&(jc_lead(a)->uid!=g_uid||jc_lead(b)->uid!=g_uid))return -LX_EPERM;
    switch(type){
    case 0: { struct jproc *la=jc_lead(a), *lb=jc_lead(b); if(i1>=FDMAX||i2>=FDMAX||la->fd[i1]<0||lb->fd[i2]<0)return -LX_EBADF; return lxm_cmp((u64)la->fd[i1],(u64)lb->fd[i2]); }
    case 1: return lxm_cmp(jc_lead(a)->cr3,jc_lead(b)->cr3);
    case 2: case 3: case 4: case 5: return lxm_cmp((u64)a->tg,(u64)b->tg);
    case 6: return lxm_cmp((u64)a->tg,(u64)b->tg);
    }
    return -LX_EINVAL;
}
static long lxm_splice(struct sysregs* r,int fin,u64 offin,int fout,u64 offout,u64 len,u32 flags,int tee){
    struct vfile *vi=lx_vf(fin), *vo=lx_vf(fout); if(!vi||!vo)return -LX_EBADF;
    if((vi->flags&3)==1||((vo->flags&3)==0&&vo->dev==0))return -LX_EBADF;
    int pin=vi->dev==6, pout=vo->dev==7;
    if(tee){ if(!pin||!pout)return -LX_EINVAL; if(vi->fs_idx==vo->fs_idx)return -LX_EINVAL; }
    else if(!pin&&!pout)return -LX_EINVAL;
    if((pin&&offin)||(pout&&offout))return -LX_ESPIPE;
    if(pin&&pout&&vi->fs_idx==vo->fs_idx)return -LX_EINVAL;
    if(!len)return 0;
    if(!lxm_kbuf())return -LX_ENOMEM;
    int nb=(flags&2)||lx_nonblock(vi)||lx_nonblock(vo);
    long oi=-1, oo=-1;
    if(offin){ if(lx_get(&oi,offin,8))return -LX_EFAULT; if(oi<0)return -LX_EINVAL; }
    if(offout){ if(lx_get(&oo,offout,8))return -LX_EFAULT; if(oo<0)return -LX_EINVAL; }
    u64 want=len>65536?65536:len;
    long got;
    if(pin){
        struct jpipe* pp=&g_pipe[vi->fs_idx];
        if(pp->n==0){ if(pp->writers<=0)return 0; if(nb)return -LX_EAGAIN; if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
        u64 c=(u64)pp->n<want?(u64)pp->n:want;
        if(pout){ struct jpipe* po=&g_pipe[vo->fs_idx]; if(po->readers<=0){ jc_signal_proc(g_jcur,13); return -LX_EPIPE; } u64 sp=(u64)(po->cap-po->n); if(!sp){ if(nb)return -LX_EAGAIN; if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); } if(c>sp)c=sp; }
        for(u64 i=0;i<c;i++)g_kbuf[i]=(u8)pp->buf[(pp->r+(int)i)%pp->cap];
        long w=lxm_kio(r,fout,g_kbuf,c,1,oo);
        if(w==-LX_EAGAIN&&!nb){ if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
        if(w<=0)return w;
        if(!tee){ pp->r=(pp->r+(int)w)%pp->cap; pp->n-=(int)w; }
        if(offout){ oo+=w; if(lx_put(offout,&oo,8))return -LX_EFAULT; }
        return w;
    }
    struct jpipe* po=&g_pipe[vo->fs_idx];
    if(po->readers<=0){ jc_signal_proc(g_jcur,13); return -LX_EPIPE; }
    u64 sp=(u64)(po->cap-po->n);
    if(!sp){ if(nb)return -LX_EAGAIN; if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
    if(want>sp)want=sp;
    got=lxm_kio(r,fin,g_kbuf,want,0,oi);
    if(got==-LX_EAGAIN&&!nb){ if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR; jc_block(r,S_WAIT,g_ticks+1); }
    if(got<=0)return got;
    for(long i=0;i<got;i++){ po->buf[po->w]=(char)g_kbuf[i]; po->w=(po->w+1)%po->cap; po->n++; }
    if(offin){ oi+=got; if(lx_put(offin,&oi,8))return -LX_EFAULT; }
    return got;
}
static long lxm_cfr(struct sysregs* r,int fin,u64 offin,int fout,u64 offout,u64 len,u32 flags){
    if(flags)return -LX_EINVAL;
    struct vfile *vi=lx_vf(fin), *vo=lx_vf(fout); if(!vi||!vo)return -LX_EBADF;
    if((vi->flags&3)==1||(vo->flags&3)==0)return -LX_EBADF;
    if(vo->flags&0x400)return -LX_EBADF;
    if((vi->dev!=0&&vi->dev!=100)||vo->dev!=0)return -LX_EINVAL;
    if(vi->fs_idx<0||vo->fs_idx<0)return -LX_EINVAL;
    long oi=vi->off, oo=vo->off;
    if(offin){ if(lx_get(&oi,offin,8))return -LX_EFAULT; if(oi<0)return -LX_EINVAL; }
    if(offout){ if(lx_get(&oo,offout,8))return -LX_EFAULT; if(oo<0)return -LX_EINVAL; }
    if(vi->dev==0&&vi->fs_idx==vo->fs_idx&&len&&oi<oo+(long)len&&oo<oi+(long)len)return -LX_EINVAL;
    if(!lxm_kbuf())return -LX_ENOMEM;
    long tot=0; if(len>0x7FFFF000ULL)len=0x7FFFF000ULL;
    while((u64)tot<len){ u64 c=len-(u64)tot; if(c>65536)c=65536;
        long n=lxm_kio(r,fin,g_kbuf,c,0,oi+tot); if(n<0){ if(!tot)return n; break; } if(!n)break;
        long w=lxm_kio(r,fout,g_kbuf,(u64)n,1,oo+tot); if(w<0){ if(!tot)return w; break; } tot+=w; if(w<n)break; }
    if(offin){ oi+=tot; if(lx_put(offin,&oi,8))return -LX_EFAULT; } else vi->off=(int)(oi+tot);
    if(offout){ oo+=tot; if(lx_put(offout,&oo,8))return -LX_EFAULT; } else vo->off=(int)(oo+tot);
    return tot;
}
static int lxm_islink(const char* p){ u32 o[10]; if(ux_stat2(p,o,0))return 0; return (o[0]&0170000u)==0120000u; }
static long lxm_openat2(int dfd,u64 up,u64 how,u64 size){
    if(size<24)return -LX_EINVAL; if(size>4096)return -LX_E2BIG;
    u64 h[3]; if(lx_get(h,how,24))return -LX_EFAULT;
    for(u64 i=24;i<size;i+=8){ u64 z=0; u64 c=size-i<8?size-i:8; if(lx_get(&z,how+i,c))return -LX_EFAULT; if(z)return -LX_E2BIG; }
    u64 flags=h[0], mode=h[1], res=h[2];
    if(flags>>32)return -LX_EINVAL; if(res&~0x3FULL)return -LX_EINVAL;
    if(mode&~07777ULL)return -LX_EINVAL;
    if(mode&&!(flags&0x40)&&(flags&0x410000)!=0x410000)return -LX_EINVAL;
    if(res&0x20)return -LX_EAGAIN;
    if((res&0x08)&&(res&0x10))return -LX_EINVAL;
    char rel[LX_PATH]; int n=lx_str(up,rel,LX_PATH); if(n<0)return n;
    if(res&0x18){
        if(rel[0]=='/'){ if(res&0x08)return -LXM_EXDEV; int k=0; while(rel[k]=='/')k++; int q=0; while(rel[k])rel[q++]=rel[k++]; rel[q]=0; n=q; if(!n){ rel[0]='.'; rel[1]=0; n=1; } }
        int depth=0; char out[LX_PATH]; int o=0;
        for(int i=0;i<=n;){ int j=i; while(j<n&&rel[j]!='/')j++; int L=j-i;
            if(L==2&&rel[i]=='.'&&rel[i+1]=='.'){ if(depth==0){ if(res&0x08)return -LXM_EXDEV; } else { depth--; o--; while(o>0&&out[o-1]!='/')o--; if(o>0)o--; } }
            else if(L&&!(L==1&&rel[i]=='.')){ if(o)out[o++]='/'; for(int k=0;k<L;k++)out[o++]=rel[i+k]; depth++; }
            i=j+1; if(j>=n)break; }
        if(!o)out[o++]='.'; out[o]=0; for(int i=0;i<=o;i++)rel[i]=out[i]; n=o;
    }
    if(res&0x18){ char base[LX_PATH], ab[LX_PATH]; char dot[2]={'.',0}; g_kb_lo=(u64)dot; g_kb_n=2; int bn=lx_path0(dfd,(u64)dot,base,0); g_kb_lo=(u64)rel; g_kb_n=(u64)n+1; int an=lx_path0(dfd,(u64)rel,ab,0); g_kb_n=0; if(bn<0)return bn; if(an<0)return an;
        if(bn>1&&base[bn-1]=='.'&&base[bn-2]=='/')bn-=2;
        char pre[LX_PATH]; int q=0; for(int i=0;i<=an;i++){ if((ab[i]=='/'||!ab[i])&&q>bn){ pre[q]=0; if(lxm_islink(pre))return -LXM_EXDEV; } if(!ab[i])break; pre[q++]=ab[i]; } }
    if(res&0x04){ char ab[LX_PATH]; g_kb_lo=(u64)rel; g_kb_n=(u64)n+1; int an=lx_path0(dfd,(u64)rel,ab,0); g_kb_n=0; if(an<0)return an;
        char pre[LX_PATH]; int q=0; for(int i=0;i<=an;i++){ if(ab[i]=='/'||!ab[i]){ if(q>1){ pre[q]=0; if(lxm_islink(pre))return -LXM_ELOOP; } } if(!ab[i])break; pre[q++]=ab[i]; } }
    g_kb_lo=(u64)rel; g_kb_n=(u64)n+1;
    long fd=lx_open(dfd,(u64)rel,(int)flags,(int)mode);
    g_kb_n=0;
    return fd;
}
static long lxm_execveat(struct sysregs* r,int dfd,u64 up,u64 argv,u64 envp,int flags){
    if(flags&~(0x100|0x1000))return -LX_EINVAL;
    char rel[LX_PATH]; int n=lx_str(up,rel,LX_PATH); if(n<0)return n;
    static char ab[LX_PATH];
    if(n==0){ if(!(flags&0x1000))return -LX_ENOENT; struct vfile* v=lx_vf(dfd); if(!v)return -LX_EBADF; if(v->dev!=0&&v->dev!=100)return -LX_EACCES; int k=0;
        if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N&&FS[v->fs_idx].used){ dt_path(FS[v->fs_idx].dir,ab); k=lx_slen(ab); if(k>1&&k<LX_PATH-1)ab[k++]='/'; for(int q=0;FS[v->fs_idx].name[q]&&k<LX_PATH-1;q++)ab[k++]=FS[v->fs_idx].name[q]; ab[k]=0; }
        else { for(;v->vpath[k]&&k<127;k++)ab[k]=v->vpath[k]; ab[k]=0; }
        if(!k)return -LX_ENOENT; }
    else { g_kb_lo=(u64)rel; g_kb_n=(u64)n+1; int an=lx_path(dfd,(u64)rel,ab,0); g_kb_n=0; if(an<0)return an; }
    if((flags&0x100)&&lxm_islink(ab))return -LXM_ELOOP;
    int al=lx_slen(ab);
    g_kb_lo=(u64)ab; g_kb_n=(u64)al+1;
    long v=lx_execve(r,(u64)ab,argv,envp);
    g_kb_n=0; return v;
}
#define FIFON 32
struct lxfifo { int used, k, pipe; char* buf; u32 ropens, wopens; };
static struct lxfifo g_fifo[FIFON];
static int g_fifo_vi[JMAX], g_fifo_mark[JMAX], g_fifo_k[JMAX], g_fifo_acc[JMAX];
static int lxm_fifo_pipe(int k){
    for(int i=0;i<FIFON;i++){ struct lxfifo* f=&g_fifo[i]; if(!f->used||f->k!=k)continue; struct jpipe* p=&g_pipe[f->pipe]; if(p->used&&p->buf==f->buf)return i; f->used=0; }
    int pi=-1; for(int i=0;i<PIPE_MAX;i++) if(!g_pipe[i].used){ pi=i; break; } if(pi<0)return -1;
    int fi=-1; for(int i=0;i<FIFON;i++) if(!g_fifo[i].used){ fi=i; break; } if(fi<0)return -1;
    char* b=(char*)kmalloc(65536); if(!b)return -1;
    struct jpipe* p=&g_pipe[pi]; p->used=1; p->r=0; p->w=0; p->n=0; p->cap=65536; p->readers=0; p->writers=0; p->buf=b;
    struct lxfifo* f=&g_fifo[fi]; f->used=1; f->k=k; f->pipe=pi; f->buf=b; f->ropens=0; f->wopens=0;
    return fi;
}
static int lxm_fifo_open(int k,int flags,int cloexec){
    int me=g_jcur; struct jproc* p=&g_jp[me]; int acc=flags&3; int nb=(flags&0x800)!=0;
    if(acc==3)return -LX_EINVAL;
    int pend=g_fifo_vi[me]-1;
    if(pend>=0&&(!g_vf[pend].used||(g_vf[pend].dev!=6&&g_vf[pend].dev!=7)||g_fifo_k[me]!=k||g_fifo_acc[me]!=acc)){ if(g_vf[pend].used&&(g_vf[pend].dev==6||g_vf[pend].dev==7))vf_unref(pend); g_fifo_vi[me]=0; pend=-1; }
    int fi=lxm_fifo_pipe(k); if(fi<0){ if(pend>=0){ vf_unref(pend); g_fifo_vi[me]=0; } return -LX_ENOMEM; }
    struct lxfifo* f=&g_fifo[fi]; struct jpipe* pp=&g_pipe[f->pipe];
    int vi=pend;
    if(vi<0){
        if(acc==1&&nb&&pp->readers<=0){ if(!pp->readers&&!pp->writers&&!pp->n){ kfree(pp->buf); pp->buf=0; pp->used=0; f->used=0; } return -LXM_ENXIO; }
        vi=jc_newvf(acc==1?7:6,f->pipe,(acc==2?2:acc)|(flags&0x800)); if(vi<0)return -LX_EMFILE;
        if(acc==1){ pp->writers++; f->wopens++; } else { pp->readers++; f->ropens++; if(acc==2){ pp->writers++; f->wopens++; } }
        if(acc==2||nb||(acc==0?pp->writers>0:pp->readers>0)){ int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
        g_fifo_vi[me]=vi+1; g_fifo_k[me]=k; g_fifo_acc[me]=acc; g_fifo_mark[me]=(int)(acc==0?f->wopens:f->ropens);
    }
    int ready=acc==0?(pp->writers>0||(int)f->wopens!=g_fifo_mark[me]):(pp->readers>0||(int)f->ropens!=g_fifo_mark[me]);
    if(ready){ g_fifo_vi[me]=0; int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
    if(jc_deliverable(p)){ g_fifo_vi[me]=0; vf_unref(vi); return -LX_EINTR; }
    jc_block(g_lxr,S_WAIT,g_ticks+1);
    return -LX_EINTR;
}
static long lxm_mknod(int dfd,u64 up,u32 mode,u64 dev){
    (void)dev; char pa[LX_PATH]; long e=lx_path(dfd,up,pa,0); if(e<0)return e;
    u32 t=mode&0170000u;
    if(t==0020000u||t==0060000u)return -LX_EPERM;
    if(t==0140000u)return -LX_EPERM;
    if(t!=0&&t!=0100000u&&t!=0010000u)return -LX_EINVAL;
    if(lx_exists(pa)||lxm_islink(pa))return -LX_EEXIST;
    if(!lx_parent_ok(pa))return lx_noent(pa);
    int fd=vfs_open(pa,0x41); if(fd<0)return -LX_EACCES;
    struct vfile* v=lx_vf(fd); int k=v?v->fs_idx:-1;
    if(v&&v->dev==0&&k>=0){ FS[k].mode=(t==0010000u?0010000u:0100000u)|(mode&07777u&~(u32)g_umask); }
    lx_close(fd);
    ino_ev(pa,0x100,0,0);
    return 0;
}
static long lxm_mincore(u64 a,u64 len,u64 vec){
    if(a&0xFFF)return -LX_EINVAL; if(!len)return 0;
    u64 np=(len+0xFFF)>>12; if(a+len<a||a+np*4096>0x800000000000ULL)return -LX_ENOMEM;
    if(!lx_okw(vec,np))return -LX_EFAULT;
    u64 cr3=jc_lead(&g_jp[g_jcur])->cr3;
    for(u64 i=0;i<np;i++){ u64* pte=vm_walk(cr3,a+i*4096,0); if(!pte||!*pte)return -LX_ENOMEM; ((u8*)vec)[i]=(*pte&1)?1:0; }
    return 0;
}
static long lxm_fchmodat2(int dfd,u64 up,u32 mode,int flags){
    if(flags&~(0x100|0x1000))return -LX_EINVAL;
    char pa[LX_PATH]; long e=lx_path(dfd,up,pa,(flags&0x1000)!=0); if(e<0)return e;
    if(!pa[0]){ struct vfile* v=lx_vf(dfd); if(!v)return -LX_EBADF; int k=0;
        if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N&&FS[v->fs_idx].used){ dt_path(FS[v->fs_idx].dir,pa); k=lx_slen(pa); if(k>1&&k<LX_PATH-1)pa[k++]='/'; for(int q=0;FS[v->fs_idx].name[q]&&k<LX_PATH-1;q++)pa[k++]=FS[v->fs_idx].name[q]; pa[k]=0; }
        else if(v->dev==LX_DIRDEV)dt_path(v->fs_idx,pa);
        else return -LXM_EOPNOTSUPP;
        if(!pa[0])return -LX_EBADF; }
    if((flags&0x100)&&lxm_islink(pa))return -LXM_EOPNOTSUPP;
    if(!lx_exists(pa))return -LX_ENOENT;
    return ux_chmod(pa,mode&07777u)?-LX_EPERM:0;
}
static long lxm_adjtimex(u64 tx){
    u8 b[208]; if(lx_get(b,tx,208))return -LX_EFAULT;
    u32 modes=*(u32*)b;
    if(modes&&modes!=0x8000u&&g_uid!=0)return -LX_EPERM;
    lx_zero(b+8,200); *(long*)(b+24)=16000000; *(long*)(b+32)=16000000; *(int*)(b+40)=0x40; *(long*)(b+48)=2; *(long*)(b+56)=1; *(long*)(b+64)=32768000;
    u64 ns=lx_now_ns(); *(u64*)(b+72)=ns/1000000000ULL; *(u64*)(b+80)=(ns%1000000000ULL)/1000ULL; *(long*)(b+88)=10000;
    if(lx_put(tx,b,208))return -LX_EFAULT;
    return 5;
}
static long lxm_syslog(int type,u64 buf,int len){
    if(type==0||type==1||type==6||type==7||type==8)return 0;
    if((type==2||type==4||type==5)&&g_uid!=0)return -LX_EPERM;
    if(type==9||type==10){ extern int klog_ring(char*,int,int); return type==10?16384:klog_ring(0,0,5); }
    if(type==5){ extern int klog_ring(char*,int,int); klog_ring(0,0,2); return 0; }
    if(type==2||type==3||type==4){ if(len<0)return -LX_EINVAL; if(!len)return 0; if(!lx_okw(buf,(u64)len))return -LX_EFAULT; extern int klog_ring(char*,int,int); return klog_ring((char*)buf,len,type==4?3:type==2?4:1); }
    return -LX_EINVAL;
}
#define FXVMAX 128
static int g_fxv_n[JMAX], g_fxv_hit[JMAX]; static u64 g_fxv_key[JMAX][FXVMAX]; static int g_fxv_tg[JMAX][FXVMAX];
int lxm_fxv_match(int j,int tg,u64 addr){ int n=g_fxv_n[j]; for(int k=0;k<n;k++) if(g_fxv_key[j][k]==addr&&g_fxv_tg[j][k]==tg){ g_fxv_hit[j]=k; return 1; } return 0; }
static long lxm_futex_waitv(struct sysregs* r,u64 wv,u32 nr,u32 flags,u64 ts,int clk){
    struct jproc* p=&g_jp[g_jcur]; int me=g_jcur;
    if(p->fx_wait&&g_fxv_n[me]){
        if(p->fx_woken){ int h=g_fxv_hit[me]; p->fx_wait=0; p->fx_woken=0; p->wdl=0; g_fxv_n[me]=0; return h; }
        if(p->fx_dead&&(g_ticks>=p->fx_dead||(p->wdl&&hr_ns()>=p->wdl))){ p->fx_wait=0; p->wdl=0; g_fxv_n[me]=0; return -LXM_ETIMEDOUT; }
        if(jc_deliverable(p)){ p->fx_wait=0; p->wdl=0; g_fxv_n[me]=0; return -LX_EINTR; }
        p->wdl_arm=1; jc_block(r,S_WAIT,p->fx_dead?p->fx_dead:g_ticks+0x7FFFFFFFFFFFULL); }
    g_fxv_n[me]=0;
    if(flags||!nr||nr>FXVMAX)return -LX_EINVAL;
    if(ts&&clk!=0&&clk!=1)return -LX_EINVAL;
    static u8 w[FXVMAX*24]; if(lx_get(w,wv,(u64)nr*24))return -LX_EFAULT;
    for(u32 i=0;i<nr;i++){ u32 fl=*(u32*)(w+i*24+16), rs=*(u32*)(w+i*24+20); u64 ua=*(u64*)(w+i*24+8); if(rs||(fl&~0x82u)||(fl&3)!=2||(ua&3))return -LX_EINVAL; }
    for(u32 i=0;i<nr;i++) if(*(u64*)(w+i*24)>>32)return -LX_EINVAL;
    for(u32 i=0;i<nr;i++){ u64 ua=*(u64*)(w+i*24+8); u32 cur; if(lx_get(&cur,ua,4))return -LX_EFAULT; if((u64)cur!=*(u64*)(w+i*24))return -LX_EAGAIN; }
    u64 dead=0;
    if(ts){ u64 t[2]; if(lx_get(t,ts,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL; if(t[0]>0x3FFFFFFFFULL)t[0]=0x3FFFFFFFFULL;
        u64 ns=t[0]*1000000000ULL+t[1]; u64 now=clk==0?lx_now_ns():hr_ns(); ns=ns>now?ns-now:0; if(!ns)return -LXM_ETIMEDOUT;
        dead=g_ticks+(ns+9999999ULL)/10000000ULL; p->wdl=hr_precise()?hr_ns()+ns:0; if(p->wdl)dead++; }
    else p->wdl=0;
    for(u32 i=0;i<nr;i++){ u32 fl=*(u32*)(w+i*24+16); u64 ua=*(u64*)(w+i*24+8); int tg=p->tg; u64 k=lx_fx_key(p,(fl&0x80)?0x80:0,ua,&tg); g_fxv_key[me][i]=k; g_fxv_tg[me][i]=tg; }
    g_fxv_n[me]=(int)nr; g_fxv_hit[me]=0;
    p->fx_wait=1; p->fx_woken=0; p->fx_tg=g_fxv_tg[me][0]; p->fx_addr=g_fxv_key[me][0]; p->fx_bits=0xFFFFFFFFu; p->fx_dead=dead;
    p->wdl_arm=1; jc_block(r,S_WAIT,dead?dead:g_ticks+0x7FFFFFFFFFFFULL);
}
static long lxm_pvm(int pid,u64 liov,u64 lcnt,u64 riov,u64 rcnt,u64 flags,int wr){
    if(flags)return -LX_EINVAL; if(lcnt>1024||rcnt>1024)return -LX_EINVAL;
    int j=jc_idx(pid); if(j<0||lxm_proc_gone(pid))return -LX_ESRCH;
    struct jproc* t=jc_lead(&g_jp[j]); if(g_uid!=0&&t->uid!=g_uid)return -LX_EPERM;
    if(!lcnt||!rcnt)return 0;
    if(!lx_ok(liov,lcnt*16)||!lx_ok(riov,rcnt*16))return -LX_EFAULT;
    u64 li=0, lo=0, ri=0, ro=0; long tot=0; u64 cr3=t->cr3;
    while(li<lcnt&&ri<rcnt){
        u64 lb=((u64*)liov)[li*2], ll=((u64*)liov)[li*2+1], rb=((u64*)riov)[ri*2], rl=((u64*)riov)[ri*2+1];
        if(lo>=ll){ li++; lo=0; continue; } if(ro>=rl){ ri++; ro=0; continue; }
        u64 ra=rb+ro, la=lb+lo; u64 c=ll-lo; if(rl-ro<c)c=rl-ro; u64 pg=4096-(ra&0xFFF); if(c>pg)c=pg; if(c>4096-(la&0xFFF))c=4096-(la&0xFFF);
        if(wr?!lx_ok(la,c):!lx_okw(la,c))return tot?tot:-LX_EFAULT;
        u64* pte=vm_walk(cr3,ra&~0xFFFULL,0); if(!pte||!(*pte&1)||!(*pte&4)||(wr&&!(*pte&2)))return tot?tot:-LX_EFAULT;
        u8* phys=(u8*)((*pte&0x000FFFFFFFFFF000ULL)|(ra&0xFFF));
        if(wr){ for(u64 i=0;i<c;i++)phys[i]=((u8*)la)[i]; } else { for(u64 i=0;i<c;i++)((u8*)la)[i]=phys[i]; }
        tot+=(long)c; lo+=c; ro+=c; }
    return tot;
}
static long lxm_numa(u64 nr,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5){
    if(nr==239){ if(a1){ int m=0; if(lx_put(a1,&m,4))return -LX_EFAULT; } if(a2&&a3){ u64 z=1; if(lx_put(a2,&z,8))return -LX_EFAULT; } (void)a4; (void)a5; return 0; }
    if(nr==238){ int m=(int)a1&0x3FFF; if(m<0||m>5)return -LX_EINVAL; return 0; }
    if(nr==237){ int m=(int)a3&0x3FFF; if(m<0||m>5)return -LX_EINVAL; return 0; }
    if(nr==256)return 0;
    if(nr==279){ if(a2>0x10000)return -LX_EINVAL; if(a5&&!lx_okw(a5,a2*4))return -LX_EFAULT; for(u64 i=0;i<a2&&a5;i++)((int*)a5)[i]=0; return 0; }
    if(nr==450)return 0;
    return LXM_NO;
}
struct scf { int used, prev, len, log; u64* ins; };
#define SCFN 128
static struct scf g_scf[SCFN];
static int lxm_scf_check(const u64* f,int len){
    if(len<1||len>4096)return 0;
    for(int i=0;i<len;i++){ u16 c=(u16)f[i]; u8 jt=(u8)(f[i]>>16), jf=(u8)(f[i]>>24); u32 k=(u32)(f[i]>>32);
        switch(c){
        case 0x20: if(k&3||k>=64)return 0; break;
        case 0x80: case 0x81: case 0x00: case 0x01: case 0x07: case 0x87: break;
        case 0x60: case 0x61: case 0x02: case 0x03: if(k>=16)return 0; break;
        case 0x04: case 0x14: case 0x24: case 0x44: case 0x54: case 0x64: case 0x74: case 0xa4: case 0x0c: case 0x1c: case 0x2c: case 0x3c: case 0x4c: case 0x5c: case 0x6c: case 0x7c: case 0x9c: case 0xac: case 0x84: break;
        case 0x34: case 0x94: if(!k)return 0; break;
        case 0x05: if((u64)i+1+k>=(u64)len)return 0; break;
        case 0x15: case 0x25: case 0x35: case 0x45: case 0x1d: case 0x2d: case 0x3d: case 0x4d: if(i+1+jt>=len||i+1+jf>=len)return 0; break;
        case 0x06: case 0x16: break;
        default: return 0; } }
    u16 last=(u16)f[len-1]; return last==0x06||last==0x16;
}
static u32 lxm_scf_run(const u64* f,int len,const u8* d){
    u32 A=0, X=0, M[16]; for(int i=0;i<16;i++)M[i]=0;
    for(int pc=0;pc<len;pc++){ u16 c=(u16)f[pc]; u8 jt=(u8)(f[pc]>>16), jf=(u8)(f[pc]>>24); u32 k=(u32)(f[pc]>>32);
        switch(c){
        case 0x20: A=*(const u32*)(d+k); break; case 0x80: A=64; break; case 0x81: X=64; break; case 0x00: A=k; break; case 0x01: X=k; break;
        case 0x60: A=M[k]; break; case 0x61: X=M[k]; break; case 0x02: M[k]=A; break; case 0x03: M[k]=X; break;
        case 0x04: A+=k; break; case 0x14: A-=k; break; case 0x24: A*=k; break; case 0x34: A/=k; break; case 0x94: A%=k; break; case 0x44: A|=k; break; case 0x54: A&=k; break; case 0x64: A=k>=32?0:A<<k; break; case 0x74: A=k>=32?0:A>>k; break; case 0xa4: A^=k; break; case 0x84: A=(u32)-(i64)A; break;
        case 0x0c: A+=X; break; case 0x1c: A-=X; break; case 0x2c: A*=X; break; case 0x3c: if(!X)return 0; A/=X; break; case 0x9c: if(!X)return 0; A%=X; break; case 0x4c: A|=X; break; case 0x5c: A&=X; break; case 0x6c: A=X>=32?0:A<<X; break; case 0x7c: A=X>=32?0:A>>X; break; case 0xac: A^=X; break;
        case 0x05: pc+=(int)k; break;
        case 0x15: pc+=A==k?jt:jf; break; case 0x25: pc+=A>k?jt:jf; break; case 0x35: pc+=A>=k?jt:jf; break; case 0x45: pc+=(A&k)?jt:jf; break;
        case 0x1d: pc+=A==X?jt:jf; break; case 0x2d: pc+=A>X?jt:jf; break; case 0x3d: pc+=A>=X?jt:jf; break; case 0x4d: pc+=(A&X)?jt:jf; break;
        case 0x06: return k; case 0x16: return A;
        case 0x07: X=A; break; case 0x87: A=X; break;
        default: return 0; } }
    return 0;
}
static void lxm_scf_gc(void){
    static u8 mark[SCFN]; for(int i=0;i<SCFN;i++)mark[i]=0;
    for(int j=0;j<JMAX;j++){ if(g_jp[j].state==S_FREE)continue; int h=g_jp[j].sc_head-1; for(int g=0;g<SCFN&&h>=0&&h<SCFN;g++){ if(mark[h])break; mark[h]=1; h=g_scf[h].prev; } }
    for(int i=0;i<SCFN;i++) if(g_scf[i].used&&!mark[i]){ if(g_scf[i].ins)kfree(g_scf[i].ins); g_scf[i].used=0; g_scf[i].ins=0; }
}
static long lxm_seccomp(u32 op,u32 flags,u64 args){
    struct jproc* p=&g_jp[g_jcur];
    if(op==2){ if(flags)return -LX_EINVAL; u32 a; if(lx_get(&a,args,4))return -LX_EFAULT; switch(a){ case 0x80000000u: case 0: case 0x30000: case 0x50000: case 0x7ff00000u: case 0x7ffc0000u: case 0x7fff0000u: return 0; } return -LXM_EOPNOTSUPP; }
    if(op==3){ if(flags)return -LX_EINVAL; u16 s[3]={80,24,64}; return lx_put(args,s,6)?-LX_EFAULT:0; }
    if(op==0){ if(flags||args)return -LX_EINVAL; if(p->sc_mode==2)return -LX_EINVAL; p->sc_mode=1; return 0; }
    if(op!=1)return -LX_EINVAL;
    if(flags&~(1u|2u|4u|16u))return -LX_EINVAL;
    if((flags&16)&&!(flags&1))return -LX_EINVAL;
    if(!p->nnp&&g_uid!=0)return -LX_EACCES;
    if(p->sc_mode==1)return -LX_EINVAL;
    u8 fp[16]; if(lx_get(fp,args,16))return -LX_EFAULT; int len=*(u16*)fp; u64 ua=*(u64*)(fp+8);
    if(len<1||len>4096)return -LX_EINVAL;
    u64* ins=(u64*)kmalloc((unsigned long)len*8); if(!ins)return -LX_ENOMEM;
    if(lx_get(ins,ua,(u64)len*8)){ kfree(ins); return -LX_EFAULT; }
    if(!lxm_scf_check(ins,len)){ kfree(ins); return -LX_EINVAL; }
    { long tot=len; for(int x=p->sc_head-1,g=0;x>=0&&x<SCFN&&g<SCFN;g++,x=g_scf[x].prev)tot+=g_scf[x].len+4; if(tot>32768){ kfree(ins); return -LX_ENOMEM; } }
    if(flags&1){ for(int j=0;j<JMAX;j++){ if(j==g_jcur||g_jp[j].state==S_FREE||g_jp[j].state==S_ZOMB||g_jp[j].tg!=p->tg)continue; int h=g_jp[j].sc_head-1; if(h==p->sc_head-1)continue;
            int anc=0; for(int x=p->sc_head-1,g=0;x>=0&&g<SCFN;g++,x=g_scf[x].prev) if(x==h){ anc=1; break; } if(h<0)anc=1;
            if(!anc){ kfree(ins); return (flags&16)?-LX_ESRCH:g_jp[j].pid; } } }
    int s=-1; for(int i=0;i<SCFN;i++) if(!g_scf[i].used){ s=i; break; }
    if(s<0){ lxm_scf_gc(); for(int i=0;i<SCFN;i++) if(!g_scf[i].used){ s=i; break; } }
    if(s<0){ kfree(ins); return -LX_ENOMEM; }
    struct scf* f=&g_scf[s]; f->used=1; f->prev=p->sc_head-1; f->len=len; f->ins=ins; f->log=(flags&2)!=0;
    p->sc_head=s+1; p->sc_mode=2;
    if(flags&1){ for(int j=0;j<JMAX;j++){ if(j==g_jcur||g_jp[j].state==S_FREE||g_jp[j].state==S_ZOMB||g_jp[j].tg!=p->tg)continue; g_jp[j].sc_head=s+1; g_jp[j].sc_mode=2; if(p->nnp)g_jp[j].nnp=1; } }
    return 0;
}
static long lxm_seccomp_check(struct sysregs* r,struct jproc* p){
    u64 nr=r->rax;
    if(p->sc_mode==1){ if(nr==0||nr==1||nr==60||nr==15)return LXM_NO; jc_thread_exit(g_jcur,9); jc_leave(); }
    u8 d[64]; *(u32*)d=(u32)nr; *(u32*)(d+4)=0xC000003Eu; *(u64*)(d+8)=r->rcx; *(u64*)(d+16)=r->rdi; *(u64*)(d+24)=r->rsi; *(u64*)(d+32)=r->rdx; *(u64*)(d+40)=r->r10; *(u64*)(d+48)=r->r8; *(u64*)(d+56)=r->r9;
    u32 best=0x7fff0000u; int logf=0;
    for(int h=p->sc_head-1,g=0;h>=0&&h<SCFN&&g<SCFN;g++,h=g_scf[h].prev){ struct scf* f=&g_scf[h]; if(!f->used)break; u32 v=lxm_scf_run(f->ins,f->len,d); if((int)(v&0xffff0000u)<(int)(best&0xffff0000u)){ best=v; logf=f->log; } }
    u32 act=best&0xffff0000u, dat=best&0xffffu;
    if(act==0x7fff0000u)return LXM_NO;
    if(act==0x7ffc0000u){ char b[64],*q=b; q=scopy(q,"seccomp: logged syscall "); q=udec(q,(u32)nr); *q=0; klog(b); return LXM_NO; }
    if(logf||act==0x80000000u||act==0){ char b[80],*q=b; q=scopy(q,"seccomp: pid "); q=udec(q,(u32)p->pid); q=scopy(q," syscall "); q=udec(q,(u32)nr); q=scopy(q,act==0x50000u?" errno":act==0x30000u?" trap":" killed"); *q=0; klog(b); }
    if(act==0x50000u)return -(long)(dat>4095?4095:dat);
    if(act==0x30000u){ if(p->sigmask&SB(31))p->sigmask&=~SB(31); if(p->sighand[31]==1)p->sighand[31]=0; jc_sig_set_info(1,(int)(u32)r->rcx,(int)(u32)(r->rcx>>32),(u64)(u32)nr|((u64)0xC000003Eu<<32)); jc_signal_thread(g_jcur,31); return (long)nr; }
    if(act==0x7ff00000u||act==0x7fc00000u)return -LX_ENOSYS;
    if(act==0x80000000u){ jc_finish(g_jcur,31|0x80); jc_leave(); }
    if(act==0){ jc_thread_exit(g_jcur,31|0x80); jc_leave(); }
    jc_finish(g_jcur,31|0x80); jc_leave();
    return -LX_ENOSYS;
}
static long lxm_prctl(u64 op,u64 a2,u64 a3,u64 a4,u64 a5){
    struct jproc* p=&g_jp[g_jcur];
    if(op==38){ if(a2!=1||a3||a4||a5)return -LX_EINVAL; p->nnp=1; return 0; }
    if(op==39){ if(a2||a3||a4||a5)return -LX_EINVAL; return p->nnp; }
    if(op==21)return p->sc_mode;
    if(op==22){ if(a2==1)return lxm_seccomp(0,0,0); if(a2==2)return lxm_seccomp(1,0,a3); return -LX_EINVAL; }
    return LXM_NO;
}
static long lx_more(struct sysregs* r,u64 nr,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6){
    switch(nr){
    case 219: return -LX_EINTR;
    case 237: case 238: return 0;
    case 239: { if(a1){ int z=0; long e=lx_put(a1,&z,4); if(e)return e; } if(a2&&a3){ u64 n=(a3+63)/64; if(n>16)n=16; u64 m[16]; for(u64 i=0;i<n;i++)m[i]=i==0?1:0; long e=lx_put(a2,m,n*8); if(e)return e; } return 0; }
    case 164: return g_jp[g_jcur].uid==0?0:-LX_EPERM;
    case 171: return g_jp[g_jcur].uid==0?0:-LX_EPERM;
    case 248: case 249: case 250: return -95;
    case 298: return -LX_EACCES;
    case 330: return -LX_ENOSPC;
    case 329: return (long)(int)a4==-1?lx_mprotect(a1,a2,(int)a3):-LX_EINVAL;
    case 64: return lxm_semget((int)a1,(int)a2,(int)a3);
    case 65: case 220: { long v=lxm_semop(r,(int)a1,a2,a3,nr==220?a4:0); g_jp[g_jcur].pollwake=0; g_jp[g_jcur].wdl=0; return v; }
    case 66: return lxm_semctl((int)a1,(int)a2,(int)a3,a4);
    case 68: return lxm_msgget((int)a1,(int)a2);
    case 69: return lxm_msgsnd(r,(int)a1,a2,a3,(int)a4);
    case 70: return lxm_msgrcv(r,(int)a1,a2,a3,(long)a4,(int)a5);
    case 71: return lxm_msgctl((int)a1,(int)a2,a3);
    case 240: return lxm_mq_open(a1,(int)a2,(int)a3,a4);
    case 241: return lxm_mq_unlink(a1);
    case 242: { long v=lxm_mq_send(r,(int)a1,a2,a3,(u32)a4,a5); g_jp[g_jcur].pollwake=0; g_jp[g_jcur].wdl=0; return v; }
    case 243: { long v=lxm_mq_recv(r,(int)a1,a2,a3,a4,a5); g_jp[g_jcur].pollwake=0; g_jp[g_jcur].wdl=0; return v; }
    case 244: return lxm_mq_notify((int)a1,a2);
    case 245: return lxm_mq_attr((int)a1,a2,a3);
    case 434: return lxm_pidfd_open((int)a1,(u32)a2);
    case 424: return lxm_pidfd_signal((int)a1,(int)a2,a3,(u32)a4);
    case 438: return lxm_pidfd_getfd((int)a1,(int)a2,(u32)a3);
    case 312: return lxm_kcmp((int)a1,(int)a2,(int)a3,a4,a5);
    case 275: return lxm_splice(r,(int)a1,a2,(int)a3,a4,a5,(u32)a6,0);
    case 276: return lxm_splice(r,(int)a1,0,(int)a2,0,a3,(u32)a4,1);
    case 278: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev!=6&&v->dev!=7)return -LX_EBADF; if(a3>1024)return -LX_EINVAL; return lx_rwv(r,(int)a1,a2,(int)a3,v->dev==7); }
    case 326: return lxm_cfr(r,(int)a1,a2,(int)a3,a4,a5,(u32)a6);
    case 437: return lxm_openat2((int)a1,a2,a3,a4);
    case 322: return lxm_execveat(r,(int)a1,a2,a3,a4,(int)a5);
    case 259: return lxm_mknod((int)a1,a2,(u32)a3,a4);
    case 133: return lxm_mknod(LX_AT_FDCWD,a1,(u32)a2,a3);
    case 188: case 189: case 190: return -LX_ENOTSUP;
    case 303: case 304: return -LXM_EOPNOTSUPP;
    case 27: return lxm_mincore(a1,a2,a3);
    case 452: return lxm_fchmodat2((int)a1,a2,(u32)a3,(int)a4);
    case 122: case 123: return g_uid;
    case 159: return lxm_adjtimex(a1);
    case 305: if((int)a1!=0)return -LX_EINVAL; return lxm_adjtimex(a2);
    case 103: return lxm_syslog((int)a1,a2,(int)a3);
    case 449: return lxm_futex_waitv(r,a1,(u32)a2,(u32)a3,a4,(int)a5);
    case 310: return lxm_pvm((int)a1,a2,a3,a4,a5,a6,0);
    case 311: return lxm_pvm((int)a1,a2,a3,a4,a5,a6,1);
    case 317: return lxm_seccomp((u32)a1,(u32)a2,a3);
    case 153: return g_uid==0?0:-LX_EPERM;
    case 163: return g_uid==0?(a1?-LX_ENOSYS:0):-LX_EPERM;
    }
    return lxm_numa(nr,a1,a2,a3,a4,a5);
}
void lxn_proc_exit(int pid);
void lxm_proc_exit(int pid){
    lxn_proc_exit(pid);
    for(int i=0;i<128;i++){ struct lxundo* u=&g_semun[i]; if(!u->used||u->pid!=pid)continue; struct lxsem* s=lxm_sem(u->id);
        if(s) for(int k=0;k<s->nsems;k++){ int v=(int)s->val[k]+u->adj[k]; if(v<0)v=0; if(v>32767)v=32767; s->val[k]=(u16)v; if(u->adj[k])s->pid[k]=pid; }
        u->used=0; }
}
static void lxm_fxv_reset(int j){ if(j>=0&&j<JMAX)g_fxv_n[j]=0; }
static int lxm_poll(struct vfile* v){
    if(v->dev==LX_MQD){ struct lxmq* q=&g_pmq[v->fs_idx]; int re=0; if(q->cur>0)re|=1; if(q->cur<q->maxmsg)re|=4; return re; }
    if(v->dev==LX_PIDFD)return lxm_proc_gone(v->fs_idx)?1:0;
    return -1;
}
int lxm_poll_ext(struct vfile* v){ int r=lxm_poll(v); return r<0?5:r; }
void lxm_slot_exit(int j){
    if(j<0||j>=JMAX)return;
    g_semw_id[j]=0; g_msqw[j]=0; g_fxv_n[j]=0;
    int pv=g_fifo_vi[j]-1; g_fifo_vi[j]=0;
    if(pv>=0&&pv<VMAXOPEN&&g_vf[pv].used&&(g_vf[pv].dev==6||g_vf[pv].dev==7))vf_unref(pv);
}
void lxm_pipe_freed(int pi){ for(int i=0;i<FIFON;i++) if(g_fifo[i].used&&g_fifo[i].pipe==pi)g_fifo[i].used=0; }
