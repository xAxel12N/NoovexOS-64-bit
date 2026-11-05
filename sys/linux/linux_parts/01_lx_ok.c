static int lx_ok(u64 p,u64 n){ return jc_umapped(p,n); }
static int lx_okw(u64 p,u64 n){ extern int jc_umapped_w(u64,u64); return jc_umapped_w(p,n); }
static int lx_str(u64 p,char* out,int cap){
    if(!p)return -LX_EFAULT;
    for(int i=0;i<cap;i++){ u64 a=p+(u64)i; if(i==0||(a&0xFFFULL)==0){ if(!lx_ok(a,1))return -LX_EFAULT; }
        out[i]=*(const char*)a; if(!out[i])return i; }
    out[cap-1]=0; return -LX_ENAMETOOLONG;
}
static int lx_put(u64 p,const void* src,u64 n){ if(!lx_okw(p,n))return -LX_EFAULT; const u8* s=(const u8*)src; u8* d=(u8*)p; for(u64 i=0;i<n;i++)d[i]=s[i]; return 0; }
static int lx_get(void* dst,u64 p,u64 n){ if(!lx_ok(p,n))return -LX_EFAULT; u8* d=(u8*)dst; const u8* s=(const u8*)p; for(u64 i=0;i<n;i++)d[i]=s[i]; return 0; }
static void lx_zero(void* p,u64 n){ u8* d=(u8*)p; for(u64 i=0;i<n;i++)d[i]=0; }
static int lx_slen(const char* s){ int n=0; while(s[n])n++; return n; }

static void lx_fm_sync(int k,u64 off,u64 n);
static struct vfile* lx_vf(int fd){ int* t=fdt(); if(fd<0||fd>=FDMAX||t[fd]<0)return 0; struct vfile* v=&g_vf[t[fd]]; return v->used?v:0; }
static int lx_fd_install(int vi,int cloexec,int min){
    int* t=fdt(); for(int f=min;f<FDMAX;f++) if(t[f]==-1){ t[f]=vi; fdce()[f]=(u8)(cloexec?1:0); return f; }
    return -LX_EMFILE;
}
static int lx_path0(int dirfd,u64 up,char* out,int empty_ok){
    char rel[LX_PATH]; int n=lx_str(up,rel,LX_PATH); if(n<0)return n;
    if(n==0){ if(!empty_ok)return -LX_ENOENT; out[0]=0; return 0; }
    { int c=0, lng=0; for(int i=0;i<n;i++){ if(rel[i]=='/')c=0; else { if(++c>255)return -LX_ENAMETOOLONG; if(c>63)lng=1; } }
      if(lng){ char mg[LX_PATH]; int o=0, i=0; while(i<n){ if(rel[i]=='/'){ if(o>=LX_PATH-1)return -LX_ENAMETOOLONG; mg[o++]=rel[i++]; continue; }
          int j=i; while(j<n&&rel[j]!='/')j++; int L=j-i;
          if(L<=63){ if(o+L>=LX_PATH-1)return -LX_ENAMETOOLONG; for(int k=0;k<L;k++)mg[o++]=rel[i+k]; }
          else { if(o+63>=LX_PATH-1)return -LX_ENAMETOOLONG; u64 h=0xcbf29ce484222325ULL; for(int k=0;k<L;k++){ h^=(u8)rel[i+k]; h*=0x100000001b3ULL; }
              for(int k=0;k<46;k++)mg[o++]=rel[i+k]; mg[o++]='~'; for(int k=15;k>=0;k--)mg[o++]="0123456789abcdef"[(h>>(k*4))&15]; }
          i=j; }
        mg[o]=0; for(int k=0;k<=o;k++)rel[k]=mg[k]; n=o; } }
    if(rel[0]=='/'){ for(int i=0;i<=n;i++)out[i]=rel[i]; return n; }
    int node=g_dcwd; const char* srb=0;
    if(dirfd!=LX_AT_FDCWD){ struct vfile* v=lx_vf(dirfd); if(!v)return -LX_EBADF; if(v->dev==LX_SRDIR)srb=v->vpath; else if(v->dev!=LX_DIRDEV)return -LX_ENOTDIR; else node=v->fs_idx; }
    char dp[LX_PATH]; if(srb){ int q=0; for(;srb[q]&&q<LX_PATH-1;q++)dp[q]=srb[q]; dp[q]=0; } else dt_path(node,dp); int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)out[o++]=dp[i];
    if(o>1)out[o++]='/'; for(int i=0;rel[i];i++){ if(o>=LX_PATH-1)return -LX_ENAMETOOLONG; out[o++]=rel[i]; }
    out[o]=0; return o;
}
static int lx_exists(const char* p){ u32 o[10]; return ux_stat2(p,o,1)==0; }
static int lx_isdir(const char* p){ u32 o[10]; if(ux_stat2(p,o,1))return 0; return o[9]||(o[0]&0170000u)==0040000u; }
static int lx_path(int dirfd,u64 up,char* out,int empty_ok){
    int n=lx_path0(dirfd,up,out,empty_ok); if(n<=1)return n;
    int t=0; while(n>1&&out[n-1]=='/'){ out[--n]=0; t=1; }
    if(t&&lx_exists(out)&&!lx_isdir(out))return -LX_ENOTDIR;
    return n;
}
static int lx_noent(const char* p){
    char q[LX_PATH]; int n=lx_slen(p); if(n>=LX_PATH)return -LX_ENOENT;
    for(int i=1;i<n;i++){ if(p[i]!='/')continue; for(int k=0;k<i;k++)q[k]=p[k]; q[i]=0;
        if(!lx_exists(q))return -LX_ENOENT; if(!lx_isdir(q))return -LX_ENOTDIR; }
    return -LX_ENOENT;
}
extern int vfs_materialize_parent(const char* p);
static int lx_parent_ok(const char* p){ int d; char b[64]; if(k_split(p,&d,b)==0)return 1; return vfs_materialize_parent(p)==0; }

static u32 lx_k2l(u32 k){ return k>>1; }
static u32 lx_l2k(u64 l){ return ((u32)l<<1)&~1u; }

static void lx_stat_out(u8* st,const u32* o,int dev_ino){
    lx_zero(st,144);
    *(u64*)(st+0)=0x4e5658ULL; *(u64*)(st+8)=(u64)o[5]+(u64)dev_ino; *(u64*)(st+16)=o[3];
    *(u32*)(st+24)=o[0]; *(u32*)(st+28)=o[1]; *(u32*)(st+32)=o[2];
    if((o[0]&0170000u)==0020000u)*(u64*)(st+40)=(u64)(1u<<8)|(o[5]&0xFF);
    *(u64*)(st+48)=o[4]; *(u64*)(st+56)=4096; *(u64*)(st+64)=((u64)o[4]+511)/512;
    *(u64*)(st+72)=o[7]; *(u64*)(st+88)=o[6]; *(u64*)(st+104)=o[8];
}
static int lx_pidx(int pid){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].pid==pid)return i; return -1; }
static int lx_procdir(const char* p,int* pix){
    const char* q; int me=g_jcur>=0?g_jcur:0; *pix=-1;
    if(p[0]=='/'&&p[1]=='d'&&p[2]=='e'&&p[3]=='v'&&p[4]=='/'&&p[5]=='f'&&p[6]=='d'){ q=p+7; while(*q=='/')q++; if(*q)return 0; *pix=jc_lead(&g_jp[me])->tg; return 3; }
    const char* s="/proc"; for(int i=0;s[i];i++) if(p[i]!=s[i])return 0; q=p+5; if(*q&&*q!='/')return 0; while(*q=='/')q++; if(!*q)return 1;
    int ix;
    if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&(q[4]=='/'||!q[4])){ q+=4; ix=jc_lead(&g_jp[me])->tg; }
    else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&(q[11]=='/'||!q[11])){ q+=11; ix=me; }
    else { int pid=0,d=0; while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; if(pid>10000000)return 0; } if(!d||(*q&&*q!='/'))return 0; ix=lx_pidx(pid); if(ix<0)return 0; }
    *pix=ix; while(*q=='/')q++; if(!*q)return 2;
    if(q[0]=='f'&&q[1]=='d'&&(q[2]=='/'||!q[2])){ q+=2; while(*q=='/')q++; if(*q)return 0; if(jc_lead(&g_jp[ix])!=jc_lead(&g_jp[me]))return 0; return 3; }
    if(q[0]=='t'&&q[1]=='a'&&q[2]=='s'&&q[3]=='k'&&(q[4]=='/'||!q[4])){ q+=4; while(*q=='/')q++; if(!*q)return 4;
        int tid=0,d=0; while(*q>='0'&&*q<='9'){ tid=tid*10+(*q-'0'); q++; d=1; if(tid>10000000)return 0; } while(*q=='/')q++; if(!d||*q)return 0; int t=lx_pidx(tid); if(t<0||g_jp[t].tg!=g_jp[ix].tg)return 0; *pix=t; return 5; }
    return 0; }
static char* lx_utoa(char* o,unsigned v){ char b[12]; int q=0; do{ b[q++]=(char)('0'+v%10); v/=10; }while(v); while(q)*o++=b[--q]; *o=0; return o; }
static int lx_procent(const char* dp,int idx,char* name,int* type){
    int ix; int k=lx_procdir(dp,&ix); *type=8;
    if(k==3){ int* t=fdt(); int c=0; for(int i=0;i<FDMAX;i++) if(t[i]>=0){ if(c==idx){ lx_utoa(name,(unsigned)i); *type=10; return 1; } c++; } return 0; }
    if(k==1){ static const char* const f[]={"self","thread-self","cpuinfo","meminfo","version","uptime","loadavg","mounts","filesystems","stat",0};
        int n=0; while(f[n])n++; if(idx<n){ const char* z=f[idx]; int o=0; while(z[o]){ name[o]=z[o]; o++; } name[o]=0; *type=idx<2?10:8; return 1; }
        int c=n; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].tg==i){ if(c==idx){ lx_utoa(name,(unsigned)g_jp[i].pid); *type=4; return 1; } c++; } return 0; }
    if(k==2||k==5){ static const char* const f[]={"fd","task","exe","cmdline","comm","maps","stat","status",0}; static const char* const g[]={"comm","stat","status",0};
        const char* const* l=k==2?f:g; for(int i=0;l[i];i++) if(i==idx){ const char* z=l[i]; int o=0; while(z[o]){ name[o]=z[o]; o++; } name[o]=0; *type=(k==2&&i<2)?4:(k==2&&i==2)?10:8; return 1; } return 0; }
    if(k==4){ int c=0, tg=g_jp[ix].tg; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].state!=S_ZOMB&&g_jp[i].tg==tg){ if(c==idx){ lx_utoa(name,(unsigned)g_jp[i].pid); *type=4; return 1; } c++; } return 0; }
    return 0; }
static int lx_fdpath(const char* pa);
static int lx_fstat(int fd,u64 ubuf);
static int lx_proc_extra_gen(const char* pa,char* out,int cap);
static int lx_streq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
#define LX_PTM 114
#define LX_PTS 115
#define PTY_MAX 32
#define PTY_BUF 16384
struct lx_pty { int used, nm, ns, locked, opened, eof; u8* in; int in_r, in_n; u8* out; int out_r, out_n; u8 line[1024]; int ln; u32 iflag, oflag, cflag, lflag; u8 cc[19]; u16 ws[4]; int pgrp, sid; };
static struct lx_pty g_pty[PTY_MAX];
static void pty_put(u8* b,int* r,int* n,u8 c){ if(*n>=PTY_BUF)return; b[(*r+*n)%PTY_BUF]=c; (*n)++; }
static int pty_take(u8* b,int* r,int* n,u8* dst,int len){ int c=0; while(c<len&&*n>0){ dst[c++]=b[*r]; *r=(*r+1)%PTY_BUF; (*n)--; } return c; }
static void pty_outc(struct lx_pty* t,u8 c){
    if((t->oflag&1)&&(t->oflag&4)&&c=='\n')pty_put(t->out,&t->out_r,&t->out_n,'\r');
    pty_put(t->out,&t->out_r,&t->out_n,c); }
static void pty_echo(struct lx_pty* t,u8 c){
    if(!(t->lflag&8)){ if(c=='\n'&&(t->lflag&0x40))pty_outc(t,c); return; }
    if(c<32&&c!='\n'&&c!='\t'&&(t->lflag&0x200)){ pty_outc(t,'^'); pty_outc(t,(u8)(c+64)); return; }
    if(c==127&&(t->lflag&0x200)){ pty_outc(t,'^'); pty_outc(t,'?'); return; }
    pty_outc(t,c); }
static void pty_sig(struct lx_pty* t,int sig){ if(t->pgrp>0)jc_kill(-t->pgrp,sig,-1); }
static void pty_flush_line(struct lx_pty* t){ for(int i=0;i<t->ln;i++)pty_put(t->in,&t->in_r,&t->in_n,t->line[i]); t->ln=0; }
static void pty_input(struct lx_pty* t,u8 c){
    if(t->iflag&0x20)c&=0x7f;
    if(c=='\r'){ if(t->iflag&0x80)return; if(t->iflag&0x100)c='\n'; } else if(c=='\n'&&(t->iflag&0x40))c='\r';
    if(t->lflag&1){
        int sig=0; if(c&&c==t->cc[0])sig=2; else if(c&&c==t->cc[1])sig=3; else if(c&&c==t->cc[10])sig=20;
        if(sig){ if(!(t->lflag&0x80)){ t->ln=0; t->in_n=0; } pty_echo(t,c); pty_sig(t,sig); return; } }
    if(t->lflag&2){
        if(c&&c==t->cc[2]){ if(t->ln>0){ t->ln--; if((t->lflag&8)&&(t->lflag&0x10)){ pty_outc(t,'\b'); pty_outc(t,' '); pty_outc(t,'\b'); } } return; }
        if(c&&c==t->cc[3]){ while(t->ln>0){ t->ln--; if((t->lflag&8)&&(t->lflag&0x10)){ pty_outc(t,'\b'); pty_outc(t,' '); pty_outc(t,'\b'); } } return; }
        if(c&&c==t->cc[14]){ while(t->ln>0&&t->line[t->ln-1]==' '){ t->ln--; if(t->lflag&8){ pty_outc(t,'\b'); pty_outc(t,' '); pty_outc(t,'\b'); } } while(t->ln>0&&t->line[t->ln-1]!=' '){ t->ln--; if(t->lflag&8){ pty_outc(t,'\b'); pty_outc(t,' '); pty_outc(t,'\b'); } } return; }
        if(c&&c==t->cc[4]){ if(t->ln==0)t->eof++; pty_flush_line(t); return; }
        if(t->ln<(int)sizeof t->line-1)t->line[t->ln++]=c;
        pty_echo(t,c);
        if(c=='\n'||(c&&(c==t->cc[11]||c==t->cc[16])))pty_flush_line(t);
        return; }
    pty_put(t->in,&t->in_r,&t->in_n,c);
    pty_echo(t,c); }
static void pty_defaults(struct lx_pty* t){
    t->iflag=0x4500; t->oflag=5; t->cflag=0xbf; t->lflag=0x8a3b;
    static const u8 cc[19]={3,0x1c,0x7f,0x15,4,0,1,0,0x11,0x13,0x1a,0,0x12,0xf,0x17,0x16,0,0,0};
    for(int i=0;i<19;i++)t->cc[i]=cc[i]; t->ws[0]=24; t->ws[1]=80; t->ws[2]=0; t->ws[3]=0; }
static void pty_name(int i,char* o){ const char* z="/dev/pts/"; while(*z)*o++=*z++; if(i>=10)*o++=(char)('0'+i/10); *o++=(char)('0'+i%10); *o=0; }
static int pty_index(const char* p){ const char* z="/dev/pts/"; int i=0; for(;z[i];i++) if(p[i]!=z[i])return -1; if(!p[i])return -1; int v=0; for(;p[i];i++){ if(p[i]<'0'||p[i]>'9')return -1; v=v*10+(p[i]-'0'); if(v>=PTY_MAX)return -1; } return v; }
static int pty_open_master(int flags){
    int k=-1; for(int i=0;i<PTY_MAX;i++) if(!g_pty[i].used){ k=i; break; } if(k<0)return -LX_ENOSPC;
    struct lx_pty* t=&g_pty[k]; u8* a=(u8*)kmalloc(PTY_BUF); u8* b=(u8*)kmalloc(PTY_BUF); if(!a||!b){ if(a)kfree(a); if(b)kfree(b); return -LX_ENOMEM; }
    lx_zero((u8*)t,sizeof *t); t->used=1; t->in=a; t->out=b; t->locked=1; pty_defaults(t);
    int vi=jc_newvf(LX_PTM,k,(flags&~0x80000)|2); if(vi<0){ kfree(a); kfree(b); t->used=0; return -LX_EMFILE; }
    const char* z="/dev/ptmx"; int q=0; for(;z[q];q++)g_vf[vi].vpath[q]=z[q]; g_vf[vi].vpath[q]=0; t->nm=1;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int pty_open_slave(int k,int flags){
    if(k<0||k>=PTY_MAX||!g_pty[k].used||g_pty[k].nm<=0)return -LX_EIO; struct lx_pty* t=&g_pty[k]; if(t->locked)return -LX_EIO;
    int vi=jc_newvf(LX_PTS,k,flags&~0x80000); if(vi<0)return -LX_EMFILE; pty_name(k,g_vf[vi].vpath); t->ns++; t->opened=1;
    struct jproc* l=jc_lead(&g_jp[g_jcur]); if(!(flags&0x100)&&t->sid==0&&l->sid==l->pid){ t->sid=l->sid; t->pgrp=l->pgid; }
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
void pty_close(int dev,int k){
    if(k<0||k>=PTY_MAX||!g_pty[k].used)return; struct lx_pty* t=&g_pty[k];
    if(dev==LX_PTM){ t->nm--; if(t->nm<=0&&t->ns>0)pty_sig(t,1); } else { t->ns--; }
    if(t->nm<=0&&t->ns<=0){ kfree(t->in); kfree(t->out); t->used=0; } }
static int pty_read(struct vfile* v,u8* buf,int len){
    struct lx_pty* t=&g_pty[v->fs_idx];
    if(v->dev==LX_PTM){ if(t->out_n>0)return pty_take(t->out,&t->out_r,&t->out_n,buf,len); if(t->opened&&t->ns<=0)return -LX_EIO; return JS_BLOCK; }
    if(t->in_n>0){ if(t->lflag&2){ int c=0; while(c<len&&t->in_n>0){ u8 x; pty_take(t->in,&t->in_r,&t->in_n,&x,1); buf[c++]=x; if(x=='\n')break; } return c; } return pty_take(t->in,&t->in_r,&t->in_n,buf,len); }
    if(t->eof){ t->eof--; return 0; }
    if(t->nm<=0)return 0;
    return JS_BLOCK; }
static int pty_write(struct vfile* v,const u8* buf,int len){
    struct lx_pty* t=&g_pty[v->fs_idx];
    if(v->dev==LX_PTM){ if(t->in_n>=PTY_BUF-8)return JS_BLOCK; int c=0; while(c<len&&t->in_n<PTY_BUF-8)pty_input(t,buf[c++]); return c?c:JS_BLOCK; }
    if(t->nm<=0)return -LX_EIO;
    if(t->out_n>=PTY_BUF-2)return JS_BLOCK; int c=0; while(c<len&&t->out_n<PTY_BUF-2)pty_outc(t,buf[c++]); return c; }
static int pty_poll(struct vfile* v){
    struct lx_pty* t=&g_pty[v->fs_idx]; int re=0;
    if(v->dev==LX_PTM){ if(t->out_n>0)re|=1; if(t->opened&&t->ns<=0)re|=0x18; if(t->in_n<PTY_BUF-8)re|=4; }
    else { if(t->in_n>0||t->eof||t->nm<=0)re|=1; if(t->nm<=0)re|=0x10; if(t->out_n<PTY_BUF-2)re|=4; }
    return re; }
static long pty_ioctl(struct vfile* v,int fd,u32 req,u64 arg){
    struct lx_pty* t=&g_pty[v->fs_idx]; struct jproc* l=jc_lead(&g_jp[g_jcur]);
    switch(req){
    case 0x5401: case 0x802C542A: { u8 tb[44]; lx_zero(tb,44); *(u32*)(tb+0)=t->iflag; *(u32*)(tb+4)=t->oflag; *(u32*)(tb+8)=t->cflag; *(u32*)(tb+12)=t->lflag; for(int i=0;i<19;i++)tb[17+i]=t->cc[i]; *(u32*)(tb+36)=38400; *(u32*)(tb+40)=38400; return lx_put(arg,tb,req==0x5401?36:44); }
    case 0x5402: case 0x5403: case 0x5404: case 0x402C542B: case 0x402C542C: case 0x402C542D: { u8 tb[36]; if(lx_get(tb,arg,36))return -LX_EFAULT; int was=t->lflag&2;
        t->iflag=*(u32*)(tb+0); t->oflag=*(u32*)(tb+4); t->cflag=*(u32*)(tb+8); t->lflag=*(u32*)(tb+12); for(int i=0;i<19;i++)t->cc[i]=tb[17+i];
        if(was&&!(t->lflag&2))pty_flush_line(t);
        if(req==0x5404||req==0x402C542D){ t->in_n=0; t->ln=0; } return 0; }
    case 0x5413: return lx_put(arg,t->ws,8);
    case 0x5414: { u16 w[4]; if(lx_get(w,arg,8))return -LX_EFAULT; int ch=w[0]!=t->ws[0]||w[1]!=t->ws[1]; for(int i=0;i<4;i++)t->ws[i]=w[i]; if(ch)pty_sig(t,28); return 0; }
    case 0x80045430: { int n=v->fs_idx; return lx_put(arg,&n,4); }
    case 0x40045431: { int lk=0; if(lx_get(&lk,arg,4))return -LX_EFAULT; t->locked=lk!=0; return 0; }
    case 0x80045439: { int lk=t->locked; return lx_put(arg,&lk,4); }
    case 0x5441: { if(v->dev!=LX_PTM)return -LX_EINVAL; int was=t->locked; t->locked=0; int r=pty_open_slave(v->fs_idx,(int)arg|0x100); t->locked=was; return r; }
    case 0x540E: t->sid=l->sid; t->pgrp=l->pgid; return 0;
    case 0x5422: if(t->sid==l->sid){ t->sid=0; } return 0;
    case 0x540F: { int g=t->pgrp>0?t->pgrp:l->pgid; return lx_put(arg,&g,4); }
    case 0x5410: { int g; if(lx_get(&g,arg,4))return -LX_EFAULT; t->pgrp=g; return 0; }
    case 0x5429: { int s=t->sid>0?t->sid:l->sid; return lx_put(arg,&s,4); }
    case 0x541B: { int n=v->dev==LX_PTM?t->out_n:t->in_n; return lx_put(arg,&n,4); }
    case 0x5411: { int n=0; return lx_put(arg,&n,4); }
    case 0x540B: { if(arg==0||arg==2){ t->in_n=0; t->ln=0; } if(arg==1||arg==2)t->out_n=0; return 0; }
    case 0x5409: case 0x540A: case 0x5425: case 0x5427: case 0x5428: return 0;
    }
    (void)fd; return -LX_EINVAL; }
static int asnd_open(const char* p,int flags);
static int asnd_stat(const char* p,u32* o,u64* rd);
static int asnd_list(int n,char* name,int* type);
static int lx_shm_name(const char* p);
static int lx_shm_stato(const char* p,int st,u32* o);
static long lx_shm_open(const char* p,int st,int flags);
static int lx_stat_path(const char* p,u64 ubuf,int follow){
    u32 o[10];
    { int st=lx_shm_name(p); if(st>=0){ if(lx_shm_stato(p,st,o))return -LX_ENOENT; u8 st2[144]; lx_stat_out(st2,o,0); return lx_put(ubuf,st2,144); } }
    { u64 rd=0; if(asnd_stat(p,o,&rd)){ u8 st2[144]; lx_stat_out(st2,o,0); *(u64*)(st2+40)=rd; return lx_put(ubuf,st2,144); } }
    { int pk=pty_index(p); int pm=lx_streq(p,"/dev/ptmx"), pd=lx_streq(p,"/dev/pts"); if(pk>=0||pm||pd){ if(pk>=0&&(!g_pty[pk].used||g_pty[pk].locked))return -LX_ENOENT; for(int i=0;i<10;i++)o[i]=0; o[3]=1; o[6]=o[7]=o[8]=1760000000u;
        u64 rd=0; if(pd){ o[0]=0040755u; o[3]=2; o[5]=0x36200000u; } else if(pm){ o[0]=0020666u; o[5]=0x36100000u; rd=(5u<<8)|2u; } else { o[0]=0020620u; o[5]=0x36000000u+(u32)pk; o[2]=5; rd=(136u<<8)|(u32)pk; }
        u8 st2[144]; lx_stat_out(st2,o,0); *(u64*)(st2+40)=rd; return lx_put(ubuf,st2,144); } }
    { int fdn=lx_fdpath(p); if(fdn>=0){ if(fdn>=FDMAX||fdt()[fdn]<0)return -LX_ENOENT; if(follow)return lx_fstat(fdn,ubuf); for(int i=0;i<10;i++)o[i]=0; o[0]=0120700u; o[3]=1; o[4]=64; o[5]=0x32000000u+(u32)fdn; u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144); } }
    { int ix; int k=lx_procdir(p,&ix); if(k){ for(int i=0;i<10;i++)o[i]=0; o[0]=k==3?0040500u:0040555u; o[3]=2; if(k==4){ int tg=g_jp[ix].tg; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].state!=S_ZOMB&&g_jp[i].tg==tg)o[3]++; } else if(k==2)o[3]=4; else if(k==1){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_TDEAD&&g_jp[i].tg==i)o[3]++; } o[5]=0x30000000u|(u32)(k<<16)|(u32)((ix<0?0:ix)&0xFFFF); o[6]=o[7]=o[8]=1760000000u; o[9]=1; u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144); } }
    if(ux_stat2(p,o,follow)){ extern int procfs_size(const char*); int n=-1; if(p[0]=='/'&&p[1]=='p'&&p[2]=='r'&&p[3]=='o'&&p[4]=='c'&&p[5]=='/')n=procfs_size(p); if(n<0){ static char eb[4096]; n=lx_proc_extra_gen(p,eb,4096); } if(n<0)return lx_noent(p);
        for(int i=0;i<10;i++)o[i]=0; o[0]=0100444u; o[3]=1; o[4]=0; o[5]=0x34000000u|(u32)(n&0xFFFF); o[6]=o[7]=o[8]=1760000000u; }
    if(o[9])o[0]=(o[0]&0170000u)?o[0]:(0040000u|(o[0]&07777u));
    u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144);
}
#define LX_AFU 18
#define LX_MFD 19
#define MFD_MAX 1024
struct memfd { int used,ref,nframes,ncap,seals; u64 size; u64* frames; };
static struct memfd g_mfd[MFD_MAX];
static int lx_fstat(int fd,u64 ubuf){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    u32 o[10]; for(int i=0;i<10;i++)o[i]=0; o[3]=1;
    if(v->dev==0&&v->fs_idx>=0){ int k=v->fs_idx; o[0]=FS[k].mode?FS[k].mode:0100644u; o[1]=(u32)FS[k].uid; o[2]=(u32)FS[k].gid; o[3]=FS[k].orphan?0:(FS[k].nlink?FS[k].nlink:1); o[4]=(u32)FS[k].len; o[5]=(u32)k+1; o[6]=FS[k].mtime; o[7]=FS[k].atime; o[8]=FS[k].ctime; }
    else if(v->dev==LX_DIRDEV){ char dp[LX_PATH]; dt_path(v->fs_idx,dp); return lx_stat_path(dp,ubuf,1); }
    else if(v->dev==LX_SRDIR){ return lx_stat_path(v->vpath,ubuf,1); }
    else if(v->dev==14||v->dev==LX_AFU){ o[0]=0140777u; o[5]=7000u+(u32)v->fs_idx; }
    else if(v->dev==LX_MFD){ o[0]=0100600u; o[3]=1; o[4]=(u32)g_mfd[v->fs_idx].size; o[5]=30000u+(u32)v->fs_idx; }
    else if(v->dev==6||v->dev==7){ o[0]=0010600u; o[5]=5000u+(u32)v->fs_idx; }
    else if(v->dev==100&&v->vfs){ o[0]=0100444u; int n=v->vfs->size(v->vpath); o[4]=n>0?(u32)n:0; u32 h=sysroot_ino(v->vpath,1); if(!h){ h=2166136261u; for(int q=0;v->vpath[q];q++)h=(h^(u8)v->vpath[q])*16777619u; } o[5]=0x10000000u|(h&0x0FFFFFFFu); }
    else if(v->dev==LX_SNDCTL||v->dev==LX_SNDPCM){ u64 rd=0; if(asnd_stat(v->vpath,o,&rd)){ u8 st2[144]; lx_stat_out(st2,o,0); *(u64*)(st2+40)=rd; return lx_put(ubuf,st2,144); } }
    else if(v->dev==LX_PTM||v->dev==LX_PTS){ int pk=v->fs_idx; o[6]=o[7]=o[8]=1760000000u; u64 rd; if(v->dev==LX_PTM){ o[0]=0020666u; o[5]=0x36100000u; rd=(5u<<8)|2u; } else { o[0]=0020620u; o[2]=5; o[5]=0x36000000u+(u32)pk; rd=(136u<<8)|(u32)pk; } u8 st2[144]; lx_stat_out(st2,o,0); *(u64*)(st2+40)=rd; return lx_put(ubuf,st2,144); }
    else { o[0]=0020666u; if(v->dev==5)o[0]=0020620u; o[5]=20000u+(u32)v->dev; }
    u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144);
}

static long lx_proc_maps(const char* pa,int flags);
static void ino_ev(const char* path,u32 mask,u32 cookie,int isdir);
static long lx_proc_fd(const char* pa,int flags);
static long lx_proc_extra(const char* pa,int flags);
static int lx_open(int dirfd,u64 up,int flags,int mode){
    char p[LX_PATH]; int n=lx_path(dirfd,up,p,0); if(n<0)return n;
    { int st=lx_shm_name(p); if(st>0)return lx_shm_open(p,st,flags); if(st==0){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; const char* z="/dev/shm"; int q=0; for(;z[q];q++)g_vf[vi].vpath[q]=z[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; } }
    { int L=lx_slen(p); const char* t1="/oom_score_adj"; const char* t2="/oom_adj"; int m1=L>14, m2=L>8; for(int i=0;m1&&i<14;i++) if(p[L-14+i]!=t1[i])m1=0; for(int i=0;m2&&i<8;i++) if(p[L-8+i]!=t2[i])m2=0;
      if((m1||m2)&&p[0]=='/'&&p[1]=='p'&&p[2]=='r'&&p[3]=='o'&&p[4]=='c'&&p[5]=='/'){ char np[LX_PATH]; for(int i=0;i<L-(m1?14:8);i++)np[i]=p[i]; np[L-(m1?14:8)]=0; int ix; if(lx_procdir(np,&ix)!=2)return -LX_ENOENT;
        if((flags&3)!=0){ int fd=vfs_open("/dev/null",flags&~(0x40|0x80|0x200)); return fd<0?-LX_EACCES:fd; } } }
    { int ar=asnd_open(p,flags); if(ar!=-9999)return ar; }
    if(lx_streq(p,"/dev/snd")){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; const char* z="/dev/snd"; int q=0; for(;z[q];q++)g_vf[vi].vpath[q]=z[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    if(lx_streq(p,"/dev/ptmx"))return pty_open_master(flags);
    { int k=pty_index(p); if(k>=0)return pty_open_slave(k,flags); }
    if(lx_streq(p,"/dev/pts")){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; const char* z="/dev/pts"; int q=0; for(;z[q];q++)g_vf[vi].vpath[q]=z[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    { int ix; int k=lx_procdir(p,&ix); if(k==1||k==3||k==4||(k&&(flags&0x10000))){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE;
        char cp[64]; char* o=cp; const char* z="/proc"; while(*z)*o++=*z++; if(k>=2){ *o++='/'; o=lx_utoa(o,(unsigned)g_jp[k==5?g_jp[ix].tg:ix].pid); } if(k==3){ z="/fd"; while(*z)*o++=*z++; } if(k>=4){ z="/task"; while(*z)*o++=*z++; } if(k==5){ *o++='/'; o=lx_utoa(o,(unsigned)g_jp[ix].pid); } *o=0;
        int q=0; for(;cp[q];q++)g_vf[vi].vpath[q]=cp[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; } }
    { long pm=lx_proc_maps(p,flags); if(pm!=-9999)return (int)pm; pm=lx_proc_fd(p,flags); if(pm!=-9999)return (int)pm; pm=lx_proc_extra(p,flags); if(pm!=-9999)return (int)pm; }
    int acc=flags&3, cre=(flags&0x40)!=0, excl=(flags&0x80)!=0, dir=(flags&0x10000)!=0, cloexec=(flags&0x80000)!=0;
    int created=0;
    if(!dev_id(p)&&!vfs_lookup_rw(p,flags)){
        int isd=lx_isdir(p);
        if(isd){ if(acc!=0||(flags&0x200))return -LX_EISDIR; if(cre&&excl)return -LX_EEXIST;
            int d=dt_resolve_dir(p);
            if(d<0&&sysroot_isdir(p)){ int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; int k=0; for(;p[k]&&k<127;k++)g_vf[vi].vpath[k]=p[k]; g_vf[vi].vpath[k]=0; int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
            if(d<0)return -LX_ENOENT;
            if(!ux_dir_ok(d,5))return -LX_EACCES;
            int vi=jc_newvf(LX_DIRDEV,d,flags); if(vi<0)return -LX_EMFILE;
            int k=0; for(;p[k]&&k<63;k++)g_vf[vi].vpath[k]=p[k]; g_vf[vi].vpath[k]=0;
            int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
        if(dir)return lx_exists(p)?-LX_ENOTDIR:lx_noent(p);
        int ex=lx_exists(p);
        if(ex&&cre&&excl)return -LX_EEXIST;
        if(!ex&&!cre)return lx_noent(p);
        if(!ex&&!lx_parent_ok(p))return lx_noent(p);
        created=!ex;
        if(ex&&(acc!=0||(flags&0x200))){ int isd2; int k=ux_lookup(p,&isd2,1); if(k>=0&&!isd2&&!ux_perm_ok(k,2))return -LX_EACCES; }
        if(ex&&acc!=1){ int isd2; int k=ux_lookup(p,&isd2,1); if(k>=0&&!isd2&&!ux_perm_ok(k,4))return -LX_EACCES; }
    } else if(dir) return -LX_ENOTDIR;
    int fd=vfs_open(p,flags);
    if(fd<0){ int fv=0; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used)fv++; int ff=0; int* t=fdt(); for(int i=0;i<FDMAX;i++) if(t[i]==-1)ff++;
        if(!fv)return -23; if(!ff)return -LX_EMFILE; return lx_exists(p)?-LX_EACCES:-LX_ENOSPC; }
    struct vfile* v=lx_vf(fd);
    if(v&&v->dev==0&&v->fs_idx>=0&&created){ int k=v->fs_idx; FS[k].mode=0100000u|((unsigned)mode&07777u&~(unsigned)g_umask); }
    if(v&&v->dev==0&&(flags&0x200)&&disk_ok&&v->fs_idx>=0){ int k=v->fs_idx; fs_sync(k); }
    if(created)ino_ev(p,0x100,0,0); else if(flags&0x200)ino_ev(p,0x2,0,0);
    return fd;
}

static int lx_nonblock(struct vfile* v){ return (v->flags&0x800)!=0; }
static long lx_sock_wait(struct sysregs* r,int n,int nb);
struct ivl { u32 min, max; int omin, omax, integer, empty; };
static int ivl_check(struct ivl* i){ if(i->empty)return 1; if(i->min>i->max||(i->min==i->max&&(i->omin||i->omax))){ i->empty=1; return 1; } return 0; }
static int ivl_refine(struct ivl* i,const struct ivl* v){
    int ch=0; if(i->empty)return -1; if(v->empty){ i->empty=1; return -1; }
    if(i->min<v->min){ i->min=v->min; i->omin=v->omin; ch=1; } else if(i->min==v->min&&!i->omin&&v->omin){ i->omin=1; ch=1; }
    if(i->max>v->max){ i->max=v->max; i->omax=v->omax; ch=1; } else if(i->max==v->max&&!i->omax&&v->omax){ i->omax=1; ch=1; }
    if(!i->integer&&v->integer){ i->integer=1; ch=1; }
    if(i->integer){ if(i->omin){ i->min++; i->omin=0; } if(i->omax){ i->max--; i->omax=0; } } else if(!i->omin&&!i->omax&&i->min==i->max)i->integer=1;
    if(ivl_check(i))return -1; return ch; }
static u32 sat32(u64 v){ return v>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)v; }
static void ivl_mul(const struct ivl* a,const struct ivl* b,struct ivl* c){
    lx_zero((u8*)c,sizeof *c); if(a->empty||b->empty){ c->empty=1; return; }
    c->min=sat32((u64)a->min*b->min); c->omin=a->omin||b->omin; c->max=sat32((u64)a->max*b->max); c->omax=a->omax||b->omax; c->integer=a->integer&&b->integer; }
static void ivl_div(const struct ivl* a,const struct ivl* b,struct ivl* c){
    lx_zero((u8*)c,sizeof *c); if(a->empty||b->empty){ c->empty=1; return; }
    if(b->max==0){ c->empty=1; return; }
    c->min=a->min/b->max; c->omin=(a->min%b->max)!=0||a->omin||b->omax;
    if(b->min>0){ c->max=a->max/b->min; if(a->max%b->min){ c->max++; c->omax=1; } else c->omax=a->omax||b->omin; } else { c->max=0xFFFFFFFFu; c->omax=0; } }
