static long lx_mkdir(const char* p,int mode){
    if(lx_exists(p))return -LX_EEXIST;
    if(lx_slen(p)>200)return -LX_ENAMETOOLONG;
    { int L=lx_slen(p), c=0; while(L>0&&p[L-1]!='/'){ L--; c++; } if(c>63)return -LX_ENAMETOOLONG; }
    int d; char b[64]; vfs_materialize_parent(p); if(k_split(p,&d,b)<0)return lx_noent(p); if(!b[0])return -LX_EEXIST;
    if(b[0]=='.'&&(!b[1]||(b[1]=='.'&&!b[2])))return -LX_EEXIST;
    if(!perm_dir(d))return -LX_EACCES;
    int nd=dt_mkdir_at(d,b); if(nd<0)return -LX_ENOSPC;
    dt_setmode(nd,0040000u|((unsigned)mode&0777u&~(unsigned)g_umask)); DT[nd].uid=g_uid; DT[nd].gid=g_gid; dt_save(); return 0;
}
static long lx_rmdir(const char* p){
    int d=dt_resolve_dir(p); if(d<0)return lx_exists(p)?-LX_ENOTDIR:-LX_ENOENT;
    if(d==0)return -LX_EBUSY;
    if(!perm_dir(d))return -LX_EACCES;
    if(dt_busy(d))return -LX_EBUSY;
    if(k_rmdir(p))return k_is_ancestor(d,g_dcwd)?-LX_EBUSY:-LX_ENOTEMPTY;
    return 0;
}
static long lx_unlink(const char* p){
    { int st=lx_shm_name(p); if(st>0)return lx_shm_unlink(p,st); if(st==0)return -LX_EISDIR; }
    if(lx_isdir(p))return -LX_EISDIR;
    u32 o[10]; if(ux_stat2(p,o,0))return -LX_ENOENT;
    return ux_unlink(p)?-LX_EACCES:0;
}
static long lx_rename(const char* a,const char* b){
    if(!lx_exists(a)){ int isd; if(ux_lookup(a,&isd,0)<0)return -LX_ENOENT; }
    vfs_materialize_parent(b);
    if(lx_isdir(b)&&!lx_isdir(a))return -LX_EISDIR;
    if(lx_isdir(a)&&lx_exists(b)&&!lx_isdir(b))return -LX_ENOTDIR;
    if(lx_isdir(a)&&lx_isdir(b)){ int db=dt_resolve_dir(b); int da=dt_resolve_dir(a); if(da==db)return 0;
        if(!perm_dir(da)||!perm_dir(db))return -LX_EACCES; if(dt_busy(db)||k_is_ancestor(db,g_dcwd))return -LX_EBUSY;
        for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==db)return -LX_ENOTEMPTY;
        for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==db)return -LX_ENOTEMPTY;
        if(k_is_ancestor(da,db))return -LX_EINVAL;
        DT[db].used=0; if(k_rename(a,b)){ DT[db].used=1; return -LX_EINVAL; } return 0; }
    else if(lx_isdir(b)){ return -LX_EISDIR; }
    if(lx_isdir(a)){ int da=dt_resolve_dir(a); if(!perm_dir(da))return -LX_EACCES; }
    else { int isd; int k=ux_lookup(a,&isd,0); if(k>=0&&!isd&&!ux_can_unlink(FS[k].dir,k))return -LX_EACCES; }
    return k_rename(a,b)?-LX_EINVAL:0;
}
static int lx_sockfd(int fd,struct vfile** vo){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=14)return -88; *vo=v; return v->fs_idx; }
static long lx_sock_wait(struct sysregs* r,int n,int nb){
    if(n!=JS_BLOCK)return n;
    if(nb)return -LX_EAGAIN;
    if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR;
    jc_block(r,S_WAIT,g_ticks+1);
}
static int lx_sa_in(u64 a,u64 len,u32* ip,u16* port){
    if(!a||len<2)return -LX_EINVAL; u8 sa[16]; lx_zero(sa,16); if(lx_get(sa,a,len<16?len:16))return -LX_EFAULT;
    u16 fam=(u16)(sa[0]|(sa[1]<<8));
    if(fam==0){ *ip=0; *port=0; return 0; }
    if(fam!=2)return -97; if(len<16)return -LX_EINVAL;
    *port=(u16)((sa[2]<<8)|sa[3]); *ip=((u32)sa[4]<<24)|((u32)sa[5]<<16)|((u32)sa[6]<<8)|sa[7]; return 0;
}
static int lx_sa_out(u64 a,u64 lenp,u32 ip,u16 port){
    if(!a||!lenp)return 0; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT;
    u8 sa[16]; lx_zero(sa,16); sa[0]=2; sa[2]=(u8)(port>>8); sa[3]=(u8)port; sa[4]=(u8)(ip>>24); sa[5]=(u8)(ip>>16); sa[6]=(u8)(ip>>8); sa[7]=(u8)ip;
    if(lx_put(a,sa,L<16?L:16))return -LX_EFAULT; L=16; return lx_put(lenp,&L,4);
}
static int lx_un_key(u64 a,u64 len,u8* key,int* kl){
    if(!a||len<2)return -LX_EINVAL; if(len>110)len=110; u8 sa[112]; lx_zero(sa,112); if(lx_get(sa,a,len))return -LX_EFAULT;
    u16 fam=(u16)(sa[0]|(sa[1]<<8)); if(fam!=1)return -97;
    int pl=(int)len-2; if(pl<=0)return -LX_EINVAL; const u8* p=sa+2;
    int kn; if(p[0]==0)kn=pl; else { kn=0; while(kn<pl&&kn<108&&p[kn])kn++; }
    if(kn<=0||kn>108)return -LX_EINVAL; for(int i=0;i<kn;i++)key[i]=p[i]; *kl=kn; return 0;
}
static int lx_un_keyout(u64 a,u64 lenp,const u8* key,int kl){
    if(!a||!lenp)return 0; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT;
    u8 sa[112]; lx_zero(sa,112); sa[0]=1; int kn=kl>108?108:kl; for(int i=0;i<kn;i++)sa[2+i]=key[i];
    u32 tot=(u32)(2+kn+(key&&kl&&key[0]?1:0)); if(lx_put(a,sa,L<tot?L:tot))return -LX_EFAULT; return lx_put(lenp,&tot,4);
}
static long lx_socket(int dom,int type,int proto){
    int tu=type&0xF;
    if(dom==1){ if(tu!=1&&tu!=5){ lx_gap(1,(u64)tu); return -93; } int ui=un_new(); if(ui<0)return ui; g_un[ui].seq=tu==5; int vi=jc_newvf(LX_AFU,ui,O_RDWR|(type&0x800)); if(vi<0){ un_close(ui); return -LX_EMFILE; } int fd=lx_fd_install(vi,(type&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    if(dom!=2){ lx_gap(2,(u64)dom); return -97; }
    int t=type&0xF; if(t!=1&&t!=2){ lx_gap(3,(u64)t); return -93; }
    if(t==1&&proto!=0&&proto!=6)return -93; if(t==2&&proto!=0&&proto!=17)return -93;
    int sid=lxs_new(t); if(sid<0)return sid;
    int vi=jc_newvf(14,sid,O_RDWR|(type&0x800)); if(vi<0){ lxs_release(sid); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(type&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd;
}
static long lx_connect(struct sysregs* r,int fd,u64 a,u64 len){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ u8 key[108]; int kl; int e=lx_un_key(a,len,key,&kl); if(e)return e; int n=un_connect(vu->fs_idx,key,kl); return lx_sock_wait(r,n,lx_nonblock(vu)); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u32 ip; u16 port; int e=lx_sa_in(a,len,&ip,&port); if(e)return e;
    int n=lxs_connect(sid,ip,port,lx_nonblock(v));
    return lx_sock_wait(r,n,0);
}
static long lx_accept(struct sysregs* r,int fd,u64 a,u64 lenp,int flags){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ int c=un_accept(vu->fs_idx); if(c<0)return lx_sock_wait(r,c,lx_nonblock(vu));
        int vi=jc_newvf(LX_AFU,c,O_RDWR|(flags&0x800)); if(vi<0){ un_close(c); return -LX_EMFILE; }
        int nf=lx_fd_install(vi,(flags&0x80000)!=0,0); if(nf<0){ vf_unref(vi); return nf; }
        struct unsock* cs=un_get(c); if(cs&&a)lx_un_keyout(a,lenp,cs->key,cs->klen); return nf; }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(g_lxs[sid].type!=1)return -95; if(g_lxs[sid].st!=LXS_LISTEN)return -LX_EINVAL;
    int c=lxs_accept(sid); if(c<0)return lx_sock_wait(r,c,lx_nonblock(v));
    int vi=jc_newvf(14,c,O_RDWR|(flags&0x800)); if(vi<0){ lxs_release(c); return -LX_EMFILE; }
    int nf=lx_fd_install(vi,(flags&0x80000)!=0,0); if(nf<0){ vf_unref(vi); return nf; }
    lx_sa_out(a,lenp,g_lxs[c].rip,g_lxs[c].rport); return nf;
}
static long lx_sendto(struct sysregs* r,int fd,u64 buf,u64 len,int fl,u64 a,u64 alen){
    { struct vfile* vu=lx_vf(fd); if(vu&&vu->dev==LX_AFU){ (void)a; (void)alen; if(len>65536)len=65536; if(len&&!lx_ok(buf,len))return -LX_EFAULT;
        int n=un_send(vu->fs_idx,(const u8*)buf,(int)len,0,0); if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13); return lx_sock_wait(r,n,lx_nonblock(vu)||(fl&0x40)); } }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(len>65536)len=65536; if(len&&!lx_ok(buf,len))return -LX_EFAULT;
    u32 ip=0; u16 port=0; int has=0; if(a&&g_lxs[sid].type==2){ int e=lx_sa_in(a,alen,&ip,&port); if(e)return e; has=1; }
    int n=lxs_send(sid,(const u8*)buf,(int)len,ip,port,has);
    if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
    return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
}
static long lx_recvfrom(struct sysregs* r,int fd,u64 buf,u64 len,int fl,u64 a,u64 alenp){
    { struct vfile* vu=lx_vf(fd); if(vu&&vu->dev==LX_AFU){ if(len>LX_IOMAX)len=LX_IOMAX; if(len&&!lx_okw(buf,len))return -LX_EFAULT;
        long rv=lx_sock_wait(r,un_recv(vu->fs_idx,(u8*)buf,(int)len,(fl&2)!=0,0,0,0),lx_nonblock(vu)||(fl&0x40));
        if(rv>=0&&a&&alenp){ struct unsock* s=un_get(vu->fs_idx); struct unsock* pr=s?un_get(s->peer):0; if(pr)lx_un_keyout(a,alenp,pr->key,pr->klen); else { u32 L=2; u16 fam=1; if(lx_put(a,&fam,2)||lx_put(alenp,&L,4))return -LX_EFAULT; } }
        return rv; } }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(len>LX_IOMAX)len=LX_IOMAX; if(len&&!lx_okw(buf,len))return -LX_EFAULT;
    u32 ip=0; u16 port=0;
    int n=lxs_recv(sid,(u8*)buf,(int)len,(fl&2)!=0,&ip,&port);
    long rv=lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    if(rv>=0&&a){ int e=lx_sa_out(a,alenp,ip,port); if(e)return e; }
    return rv;
}
static long lx_un_msg(struct sysregs* r,struct vfile* v,u64 msg,int fl,int wr){
    int sid=v->fs_idx;
    u64 m[7]; if(lx_get(m,msg,56))return -LX_EFAULT;
    u64 iov=m[2], cnt=m[3]; if(cnt>1024)return -LX_EINVAL; if(cnt&&!lx_ok(iov,cnt*16))return -LX_EFAULT;
    u64 tot=0; for(u64 i=0;i<cnt;i++){ u64 l=((u64*)iov)[i*2+1]; if(l&&!(wr?lx_ok(((u64*)iov)[i*2],l):lx_okw(((u64*)iov)[i*2],l)))return -LX_EFAULT; tot+=l; }
    if(tot>65536)tot=65536;
    u64 ctl=m[4], ctllen=m[5];
    u8* k=(u8*)kmalloc(tot?tot:1); if(!k)return -12;
    if(wr){
        u64 o=0; for(u64 i=0;i<cnt&&o<tot;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<tot;j++)k[o++]=b[j]; }
        int vfs[64]; int nf=0;
        if(ctl&&ctllen>=16&&lx_ok(ctl,ctllen)){ u64 off=0; while(off+16<=ctllen){ u64 cl=*(u64*)(ctl+off); int lvl=*(int*)(ctl+off+8), typ=*(int*)(ctl+off+12);
            if(cl<16||off+cl>ctllen)break; if(lvl==1&&typ==1){ int nfd=(int)((cl-16)/4); u8* dp=(u8*)(ctl+off+16); for(int q=0;q<nfd;q++){ if(nf>=64){ nf=-2; break; } int ufd=*(int*)(dp+q*4); if(ufd<0||ufd>=FDMAX||fdt()[ufd]<0){ nf=-1; break; } vfs[nf++]=fdt()[ufd]; } if(nf<0)break; }
            off+=(cl+7)&~7ULL; } }
        if(nf<0){ kfree(k); return nf==-2?-109:-LX_EBADF; }
        { struct unsock* me=un_get(sid); if(me&&g_jcur>=0)me->cpid=jc_lead(&g_jp[g_jcur])->pid; }
        int n=un_send(sid,k,(int)tot,vfs,nf); kfree(k);
        if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
        return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    }
    int vfs[64]; int nf=0; int maxfit=(ctl&&ctllen>=16)?(int)((ctllen-16)/4):0; if(maxfit>64)maxfit=64;
    int n=un_recv(sid,k,(int)tot,(fl&2)!=0,vfs,maxfit,&nf);
    if(n<0){ kfree(k); return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40)); }
    u64 o=0; for(u64 i=0;i<cnt&&o<(u64)n;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<(u64)n;j++)b[j]=k[o++]; }
    kfree(k);
    struct unsock* me=un_get(sid); u64 used=0; u32 mf=0;
    if(nf>0){ u64 cl=16+(u64)nf*4; if(ctl&&ctllen>=cl&&lx_okw(ctl,ctllen)){ *(u64*)(ctl+0)=cl; *(int*)(ctl+8)=1; *(int*)(ctl+12)=1;
            for(int q=0;q<nf;q++){ int newfd=lx_fd_install(vfs[q],(fl&0x40000000)!=0,0); *(int*)(ctl+16+q*4)=newfd<0?-1:newfd; if(newfd<0)vf_unref(vfs[q]); }
            used=(cl+7)&~7ULL; if(used>ctllen)used=ctllen; }
        else { for(int q=0;q<nf;q++)vf_unref(vfs[q]); mf|=8; } }
    if(me&&me->passcred){ if(ctl&&ctllen>=used+28&&lx_okw(ctl,ctllen)){ u8* c=(u8*)(ctl+used); struct unsock* pr=un_get(me->peer); u32 cr[3]={(u32)(pr?pr->cpid:me->cpid),(u32)g_uid,(u32)g_gid};
            *(u64*)c=28; *(int*)(c+8)=1; *(int*)(c+12)=2; for(int q=0;q<12;q++)c[16+q]=((u8*)cr)[q]; used+=ctllen-used>=32?32:28; }
        else mf|=8; }
    if(lx_put(msg+40,&used,8))return -LX_EFAULT;
    if(m[0]&&m[1]){ struct unsock* pr=me?un_get(me->peer):0; if(pr)lx_un_keyout(m[0],msg+8,pr->key,pr->klen); }
    if(lx_put(msg+48,&mf,4))return -LX_EFAULT;
    return n;
}
static long lx_msg(struct sysregs* r,int fd,u64 msg,int fl,int wr){
    { struct vfile* vu=lx_vf(fd); if(vu&&vu->dev==LX_AFU)return lx_un_msg(r,vu,msg,fl,wr); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u64 m[7]; if(lx_get(m,msg,56))return -LX_EFAULT;
    u64 iov=m[2], cnt=m[3]; if(cnt>1024)return -LX_EINVAL; if(cnt&&!lx_ok(iov,cnt*16))return -LX_EFAULT;
    u64 tot=0; for(u64 i=0;i<cnt;i++){ u64 l=((u64*)iov)[i*2+1]; if(l&&!(wr?lx_ok(((u64*)iov)[i*2],l):lx_okw(((u64*)iov)[i*2],l)))return -LX_EFAULT; tot+=l; }
    if(tot>65536)tot=65536;
    u8* k=(u8*)kmalloc(tot?tot:1); if(!k)return -12;
    long rv;
    if(wr){
        u64 o=0; for(u64 i=0;i<cnt&&o<tot;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<tot;j++)k[o++]=b[j]; }
        u32 ip=0; u16 port=0; int has=0;
        if(m[0]&&g_lxs[sid].type==2){ int e=lx_sa_in(m[0],(u32)m[1],&ip,&port); if(e){ kfree(k); return e; } has=1; }
        int n=lxs_send(sid,k,(int)tot,ip,port,has); kfree(k);
        if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
        return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    }
    u32 ip=0; u16 port=0;
    int n=lxs_recv(sid,k,(int)tot,(fl&2)!=0,&ip,&port);
    if(n<0){ kfree(k); return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40)); }
    u64 o=0; for(u64 i=0;i<cnt&&o<(u64)n;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<(u64)n;j++)b[j]=k[o++]; }
    kfree(k); rv=n;
    if(m[0]&&m[1]){ u8 sa[16]; lx_zero(sa,16); sa[0]=2; sa[2]=(u8)(port>>8); sa[3]=(u8)port; sa[4]=(u8)(ip>>24); sa[5]=(u8)(ip>>16); sa[6]=(u8)(ip>>8); sa[7]=(u8)ip;
        if(lx_put(m[0],sa,m[1]<16?m[1]:16))return -LX_EFAULT; u32 L=16; if(lx_put(msg+8,&L,4))return -LX_EFAULT; }
    u64 z=0; if(lx_put(msg+40,&z,8))return -LX_EFAULT; u32 f0=0; if(lx_put(msg+48,&f0,4))return -LX_EFAULT;
    return rv;
}
static long lx_sockname(int fd,u64 a,u64 lenp,int peer){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ struct unsock* s=un_get(vu->fs_idx); if(!s)return -LX_EBADF;
        if(peer){ struct unsock* pr=un_get(s->peer); if(!pr)return -107; return lx_un_keyout(a,lenp,pr->key,pr->klen); }
        return lx_un_keyout(a,lenp,s->key,s->klen); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    struct lxsock* s=&g_lxs[sid];
    if(peer){ if(s->type==1?s->st!=LXS_EST:!s->conn)return -107; return lx_sa_out(a,lenp,s->rip,s->rport); }
    return lx_sa_out(a,lenp,s->lport?my_ip:0,s->lport);
}
static long lx_getsockopt(int fd,int lvl,int opt,u64 val,u64 lenp){
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    struct lxsock* s=&g_lxs[sid]; int x=0;
    if(lvl==1){ if(opt==4){ lxs_pump(); x=s->err; s->err=0; } else if(opt==3)x=s->type; else if(opt==7)x=LXS_TXCAP; else if(opt==8)x=LXS_RXCAP; else if(opt==39)x=2; else if(opt==38)x=s->type==1?6:17; }
    if(!val||!lenp)return -LX_EFAULT; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT; if(L>4)L=4;
    if(lx_put(val,&x,L))return -LX_EFAULT; return lx_put(lenp,&L,4);
}
static long lx_bind(int fd,u64 a,u64 len){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ u8 key[108]; int kl; int e=lx_un_key(a,len,key,&kl); if(e)return e;
        char sp[112]; int fsn=key[0]=='/'&&kl<108; if(fsn){ for(int i=0;i<kl;i++)sp[i]=(char)key[i]; sp[kl]=0; if(lx_exists(sp))return -98; if(!lx_parent_ok(sp))return lx_noent(sp); }
        e=un_bind(vu->fs_idx,key,kl); if(e||!fsn)return e;
        int fd=vfs_open(sp,O_CREAT|O_TRUNC|O_WRONLY); if(fd>=0){ struct vfile* v=lx_vf(fd); if(v&&v->dev==0&&v->fs_idx>=0)FS[v->fs_idx].mode=0140000u|(0777u&~(unsigned)g_umask); vfs_close(fd); ino_ev(sp,0x100,0,0); }
        return 0; }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u32 ip; u16 port; int e=lx_sa_in(a,len,&ip,&port); if(e)return e;
    return lxs_bind(sid,port);
}
static int a_is_in(int q){ return q==0||q==2; }
static long lx_ioctl(int fd,u64 req,u64 arg){
    req=(u32)req;
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    struct jproc* p=&g_jp[g_jcur];
    if(req==0x5451){ fdce()[fd]=1; return 0; }
    if(v->dev==14){ if(req==0x541B){ lxs_pump(); int n=g_lxs[v->fs_idx].type==2?(g_lxs[v->fs_idx].rxn?rd16(g_lxs[v->fs_idx].rx+6):0):g_lxs[v->fs_idx].rxn; return lx_put(arg,&n,4); } if(req==0x5421){ int on=0; if(lx_get(&on,arg,4))return -LX_EFAULT; if(on)v->flags|=0x800; else v->flags&=~0x800; return 0; } if(req==0x5450){ fdce()[fd]=0; return 0; } return -25; }
    if(req==0x5450){ fdce()[fd]=0; return 0; }
    if(req==0x5421){ int on; if(lx_get(&on,arg,4))return -LX_EFAULT; if(on)v->flags|=0x800; else v->flags&=~0x800; return 0; }
    if(v->dev==LX_PTM||v->dev==LX_PTS)return pty_ioctl(v,fd,(u32)req,arg);
    if(v->dev==LX_SNDCTL)return asnd_ctl_ioctl((u32)req,arg);
    if(req==0x541B){ int n=0; if(v->dev==6)n=g_pipe[v->fs_idx].n; else if(v->dev==5)n=g_tin_n; else if(v->dev==0&&v->fs_idx>=0)n=FS[v->fs_idx].len-v->off; if(n<0)n=0; return lx_put(arg,&n,4); }
    if(v->dev==12){
        if(req==0x5000||req==0x5001)return 0;
        if(req==0xC0045002u){ int r=g_snd_state==1&&g_snd.rate>0?g_snd.rate:48000; return lx_put(arg,&r,4); }
        if(req==0xC0045005u){ int f=0x10; return lx_put(arg,&f,4); }
        if(req==0xC0045006u){ int c=2; return lx_put(arg,&c,4); }
        if(req==0xC0045003u){ int c=1; return lx_put(arg,&c,4); }
        if(req==0x8004500Bu){ int m=0x10; return lx_put(arg,&m,4); }
        if(req==0x8010500Cu){ int bi[4]; int fr=snd_init()?(g_snd.start(),g_snd.free_bytes()):0; bi[0]=fr/4096; bi[1]=32; bi[2]=4096; bi[3]=fr; return lx_put(arg,bi,16); }
        if(req==0xC004500Au){ return 0; }
        if(req==0x8010500Du){ int bi[4]; int av=snd_rec_start()?snd_cap_avail():0; bi[0]=av/4096; bi[1]=256; bi[2]=4096; bi[3]=av; return lx_put(arg,bi,16); }
        if(req==0x80045004u||req==0xC0045004u){ int bs=4096; return lx_put(arg,&bs,4); }
        if(req==0x8004500Fu){ int c=0x1100; return lx_put(arg,&c,4); }
        if(req==0x40045010u||req==0x5016u){ return 0; }
        return -LX_EINVAL; }
    if(v->dev==15){ extern long media_ioctl(int vfidx,u64 req,u64 arg); return media_ioctl((int)(v-g_vf),req,arg); }
    if(v->dev==9){
        if(req==0x4600){ u32 vi[40]; lx_zero((u8*)vi,160); vi[0]=W; vi[1]=H; vi[2]=SW; vi[3]=H; vi[6]=32; vi[8]=16; vi[9]=8; vi[11]=8; vi[12]=8; vi[14]=0; vi[15]=8; vi[17]=24; vi[18]=8; return lx_put(arg,vi,160); }
        if(req==0x4602){ u8 fx[80]; lx_zero(fx,80); const char* id="noovexfb"; for(int i=0;id[i];i++)fx[i]=(u8)id[i]; *(u32*)(fx+24)=SW*H*4; *(u32*)(fx+36)=2; *(u32*)(fx+48)=SW*4; return lx_put(arg,fx,80); }
        return -LX_EINVAL; }
    if(v->dev==10){ if(req==0x80084E50ull){ extern int mx,my; int pp[2]; pp[0]=mx; pp[1]=my; return lx_put(arg,pp,8); } return -LX_ENOTTY; }
    if(v->dev!=5)return -LX_ENOTTY;
    if(req==0x802C542Aull){ vt_raw(); u8 t[44]; lx_zero(t,44); *(u32*)(t+0)=g_tios.iflag; *(u32*)(t+4)=g_tios.oflag; *(u32*)(t+8)=g_tios.cflag; *(u32*)(t+12)=g_tios.lflag; t[16]=g_tios.line;
        for(int i=0;i<19;i++)t[17+i]=g_tios.cc[i]; *(u32*)(t+36)=38400; *(u32*)(t+40)=38400; return lx_put(arg,t,44); }
    if(req==0x402C542Bull||req==0x402C542Cull||req==0x402C542Dull){ u8 t[44]; if(lx_get(t,arg,44))return -LX_EFAULT; vt_raw();
        g_tios.iflag=*(u32*)(t+0); g_tios.oflag=*(u32*)(t+4); g_tios.cflag=*(u32*)(t+8)|0x30; g_tios.lflag=*(u32*)(t+12); g_tios.line=t[16]; for(int i=0;i<19;i++)g_tios.cc[i]=t[17+i];
        if(req==0x402C542Dull){ g_tin_n=0; g_tin_eof=0; }
        return 0; }
    switch(req){
    case 0x5401: { vt_raw(); u8 t[36]; lx_zero(t,36); *(u32*)(t+0)=g_tios.iflag; *(u32*)(t+4)=g_tios.oflag; *(u32*)(t+8)=g_tios.cflag; *(u32*)(t+12)=g_tios.lflag; t[16]=g_tios.line;
        for(int i=0;i<19;i++)t[17+i]=g_tios.cc[i];
        return lx_put(arg,t,36); }
    case 0x5402: case 0x5403: case 0x5404: { u8 t[36]; if(lx_get(t,arg,36))return -LX_EFAULT; vt_raw();
        g_tios.iflag=*(u32*)(t+0); g_tios.oflag=*(u32*)(t+4); g_tios.cflag=*(u32*)(t+8)|0x30; g_tios.lflag=*(u32*)(t+12); g_tios.line=t[16]; for(int i=0;i<19;i++)g_tios.cc[i]=t[17+i];
        if(req==0x5404){ g_tin_n=0; g_tin_eof=0; }
        return 0; }
    case 0x540B: if(a_is_in((int)arg)){ g_tin_n=0; g_tin_eof=0; } return 0;
    case 0x5413: { u16 w[4]; w[0]=(u16)(trows>0?trows:25); w[1]=(u16)(tcols>0?tcols:80); w[2]=0; w[3]=0; return lx_put(arg,w,8); }
    case 0x5414: return 0;
    case 0x540F: { int g=g_tty_fg?g_tty_fg:jc_lead(p)->pgid; return lx_put(arg,&g,4); }
    case 0x5410: { int g; if(lx_get(&g,arg,4))return -LX_EFAULT; if(jc_lead(p)->sid!=1)return -LX_ENOTTY; g_tty_fg=g; return 0; }
    case 0x5429: { int s=1; return lx_put(arg,&s,4); }
    case 0x540E: return 0;
    }
    lx_gap(4,req); return -LX_EINVAL;
}
static long lx_fcntl(int fd,int cmd,u64 arg){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    int* t=fdt();
    switch(cmd){
    case 0: case 1030: { if(arg>=FDMAX)return -LX_EINVAL; int n=lx_fd_install(t[fd],cmd==1030,(int)arg); if(n>=0)vf_ref(t[fd]); return n; }
    case 1: return fdce()[fd]&1;
    case 2: fdce()[fd]=(u8)(arg&1); return 0;
    case 3: return (long)(v->flags&~0x80000);
    case 4: v->flags=(v->flags&~(0x400|0x800))|(int)(arg&(0x400|0x800)); return 0;
    case 5: { if(!arg)return -LX_EFAULT; short ty=2; return lx_put(arg,&ty,2); }
    case 6: case 7: return 0;
    case 1025: case 1031: return 0;
    case 1032: return 4096;
    case 1033: { if(v->dev!=LX_MFD)return -LX_EINVAL; struct memfd* m=&g_mfd[v->fs_idx]; if(arg&~0x3FULL)return -LX_EINVAL; if(m->seals&1)return -LX_EPERM; m->seals|=(int)arg; return 0; }
    case 1034: { if(v->dev!=LX_MFD)return -LX_EINVAL; return g_mfd[v->fs_idx].seals; }
    case 36: { if(!arg)return -LX_EFAULT; short ty=2; return lx_put(arg,&ty,2); }
    case 37: case 38: return 0;
    case 8: case 10: case 15: case 1024: case 1026: case 1036: case 1037: case 1038: case 1039: return 0;
    case 9: case 11: return 0;
    case 16: { if(!arg)return -LX_EFAULT; u32 z[2]={0,0}; return lx_put(arg,z,8); }
    }
    lx_gap(5,(u64)cmd); return -LX_EINVAL;
}
static long lx_pipe(u64 out,int flags){
    if(!lx_okw(out,8))return -LX_EFAULT;
    int rv,wv; if(jc_pipe_new(4096,&rv,&wv)<0)return -LX_EMFILE;
    int a=lx_fd_install(rv,flags&0x80000,0); if(a<0){ vf_unref(rv); vf_unref(wv); return a; }
    int b=lx_fd_install(wv,flags&0x80000,0); if(b<0){ fdt()[a]=-1; vf_unref(rv); vf_unref(wv); return b; }
    if(flags&0x800){ g_vf[rv].flags|=0x800; g_vf[wv].flags|=0x800; }
    ((int*)out)[0]=a; ((int*)out)[1]=b; return 0;
}
