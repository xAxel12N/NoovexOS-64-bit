extern void jc_resume(regs_pt* ctx) __attribute__((noreturn));


#define JOBMAX 16

struct jproc g_jp[JMAX];
int g_next_pid=2, g_js_dirty=0;
int g_tty_fg=0;
static int g_con_vi=-1;

struct jpipe g_pipe[PIPE_MAX];
static int g_coll_vi=-1, g_coll_pgid=0, g_coll_n=0, g_coll_cancel=0, g_coll_trunc=0; static char g_coll_rest[200]; static char* g_coll_buf=0;
#define COLL_CAP 65536
const char* g_run_feed=0;
static void jc_coll_drain(void); static void jc_coll_finish(void);

struct job { int used, id, pgid, state, bg, notified_stop; char cmd[64]; };
static struct job g_jobs2[JOBMAX];

static char g_tin[1024]; int g_tin_n=0, g_tin_eof=0;
static char g_ted[256]; static int g_ted_n=0;

int* fdt(void){ return (g_jcur>=0)?g_jp[g_jp[g_jcur].tg].fd:g_fd; }
static u8 g_kcloexec[FDMAX];
u8* fdce(void){ return (g_jcur>=0)?g_jp[g_jp[g_jcur].tg].cloexec:g_kcloexec; }

int jc_idx(int pid){ for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].pid==pid)return i; return -1; }
int jc_uok(u64 p,u64 n){ return p>=0x40000000000ULL && p+n<=0x48000000000ULL && p+n>=p; }
static int jc_umap_chk(u64 p,u64 n,int w);
int jc_umapped(u64 p,u64 n){ return jc_umap_chk(p,n,0); }
int jc_umapped_w(u64 p,u64 n){ return jc_umap_chk(p,n,1); }
static int jc_umap_chk(u64 p,u64 n,int w){
    if(!n)return 1; if(!jc_uok(p,n))return 0;
    u64 cr3=cpu_get_pt()&~0xFFFULL;
    for(u64 v=p&~0xFFFULL;v<p+n;v+=0x1000ULL){ u64* e=vm_walk(cr3,v,0); if(!e||!vm_pte_used(*e)){ extern int lx_res_fault(u64,u64); if(lx_res_fault(cr3,v))return 0; e=vm_walk(cr3,v,0); if(!e)return 0; }
        if(!(*e&1)||((*e&PTE_COW)&&!(*e&2)&&(w||!(*e&PTE_PRO)))){ if(vm_fault_user(cr3,v,(*e&1)?1:w))return 0; e=vm_walk(cr3,v,0); if(!e||(*e&5)!=5||(w&&!(*e&2)))return 0; continue; }
        if((*e&5)!=5)return 0;
        if(w&&!(*e&2)){ if(!(*e&PTE_COW)||vm_fault_user(cr3,v,1))return 0; e=vm_walk(cr3,v,0); if(!e||(*e&7)!=7)return 0; } }
    return 1;
}

static int jc_con(void){
    fd_init();
    if(g_con_vi>=0&&g_vf[g_con_vi].used)return g_con_vi;
    for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){ g_vf[i].used=1; g_vf[i].ref=1; g_vf[i].fs_idx=-1; g_vf[i].off=0; g_vf[i].flags=O_RDWR; g_vf[i].dev=5; g_vf[i].vfs=0; g_con_vi=i; return i; }
    return -1;
}

void vf_ref(int vi){ if(vi>=0&&vi<VMAXOPEN&&g_vf[vi].used)g_vf[vi].ref++; }
void vf_unref(int vi){
    if(vi<0||vi>=VMAXOPEN||!g_vf[vi].used)return;
    if(--g_vf[vi].ref>0)return;
    struct vfile* v=&g_vf[vi];
    if(v->dev==6||v->dev==7){ struct jpipe* p=&g_pipe[v->fs_idx]; if(v->dev==6)p->readers--; else p->writers--;
        if(p->readers<=0&&p->writers<=0){ if(p->buf)kfree(p->buf); p->buf=0; p->used=0; } }
    if(vi==g_con_vi){ v->ref=1; return; }
    if(v->dev==14)lxs_release(v->fs_idx);
    { extern void un_close(int); extern void mfd_close(int); extern void evtm_close(int,int); if(v->dev==18)un_close(v->fs_idx); else if(v->dev==114||v->dev==115){ extern void pty_close(int,int); pty_close(v->dev,v->fs_idx); } else if(v->dev==116||v->dev==117){ extern void asnd_close(int,int); asnd_close(v->dev,v->fs_idx); } else if(v->dev==19)mfd_close(v->fs_idx); else if(v->dev==20||v->dev==21)evtm_close(v->dev,v->fs_idx); else if(v->dev==22||v->dev==23||v->dev==113){ extern void lxabi_close(int,int); lxabi_close(v->dev,v->fs_idx); } }
    { extern void lx_lock_vfclose(int); lx_lock_vfclose(vi); }
    if(v->dev==9&&g_fb0_open>0)g_fb0_open--; if(v->dev==10&&g_mouse_open>0)g_mouse_open--;
    if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N&&g_fs_dirty[v->fs_idx]){ int k=v->fs_idx; g_fs_dirty[k]=0; if(FS[k].used&&!FS[k].orphan&&disk_ok)fs_wb_mark(k); }
    v->gen++; v->used=0;
    if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N)fs_orphan_close(v->fs_idx);
}
int jc_newvf(int dev,int idx,int flags){
    for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){ g_vf[i].used=1; g_vf[i].ref=1; g_vf[i].fs_idx=idx; g_vf[i].off=0; g_vf[i].flags=flags; g_vf[i].dev=dev; g_vf[i].vfs=0; return i; }
    return -1;
}
int jc_pipe_new(int cap,int* rvi,int* wvi){
    int pi=-1; for(int i=0;i<PIPE_MAX;i++) if(!g_pipe[i].used){pi=i;break;} if(pi<0)return -1;
    char* b=(char*)kmalloc((unsigned long)cap); if(!b)return -1;
    int r=jc_newvf(6,pi,O_RDONLY); if(r<0){ kfree(b); return -1; }
    int w=jc_newvf(7,pi,O_WRONLY); if(w<0){ g_vf[r].used=0; kfree(b); return -1; }
    struct jpipe* p=&g_pipe[pi]; p->used=1; p->r=0; p->w=0; p->n=0; p->cap=cap; p->readers=1; p->writers=1; p->buf=b;
    *rvi=r; *wvi=w; return pi;
}
int jc_pipe_read(struct vfile* v,char* buf,int len){
    struct jpipe* p=&g_pipe[v->fs_idx];
    if(p->n==0) return p->writers>0?JS_BLOCK:0;
    int c=0; while(c<len&&p->n>0){ buf[c++]=p->buf[p->r]; p->r=(p->r+1)%p->cap; p->n--; }
    return c;
}

int jc_pipe_write(struct vfile* v,const char* buf,int len){
    struct jpipe* p=&g_pipe[v->fs_idx];
    if(p->readers<=0){ if(g_jcur>=0)jc_signal_proc(g_jcur,13); return -1; }
    if(p->n>=p->cap) return JS_BLOCK;
    int c=0; while(c<len&&p->n<p->cap){ p->buf[p->w]=buf[c++]; p->w=(p->w+1)%p->cap; p->n++; }
    return c;
}

void jc_tty_out(const char* s,int n){ if(g_capture){ vt_capture(s,n); return; } vt_out(s,n); g_js_dirty=1; }
void vt_reply(const char* s){ while(*s&&g_tin_n<1023)g_tin[g_tin_n++]=*s++; }
int jc_tty_read(char* buf,int len){
    if(g_jcur<0)return 0;
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    if(p->sid!=1)return -1;
    if(p->pgid!=g_tty_fg){ jc_signal_proc(g_jcur,21); return JS_BLOCK; }
    if(vt_raw()){
        struct jproc* t=&g_jp[g_jcur]; int vmin=g_tios.cc[VT_VMIN], vtime=g_tios.cc[VT_VTIME];
        if(g_tin_n>0){ int c=len<g_tin_n?len:g_tin_n; for(int i=0;i<c;i++)buf[i]=g_tin[i]; for(int i=c;i<g_tin_n;i++)g_tin[i-c]=g_tin[i]; g_tin_n-=c; t->pollwake=0; return c; }
        if(g_tin_eof){ g_tin_eof=0; return 0; }
        if(!vmin&&!vtime)return 0;
        if(vtime&&!vmin){ if(!t->pollwake)t->pollwake=g_ticks+(u64)vtime*10ULL; if(g_ticks>=t->pollwake){ t->pollwake=0; return 0; } }
        return JS_BLOCK; }
    if(g_tin_n>0){ int c=0; while(c<len&&c<g_tin_n){ char ch=g_tin[c]; buf[c++]=ch; if(ch=='\n')break; }
        for(int i=c;i<g_tin_n;i++)g_tin[i-c]=g_tin[i]; g_tin_n-=c; return c; }
    if(g_tin_eof){ g_tin_eof=0; return 0; }
    return JS_BLOCK;
}

void vm_free_user(u64 cr3){
    if(!cr3)return; { extern void lx_mapn_dead(u64); lx_mapn_dead(cr3); extern void xfb_untrack_cr3(u64); xfb_untrack_cr3(cr3); } u64* pml4=(u64*)(cr3&~0xFFFULL);
    if(pml4[8]&1){ u64* pdpt=(u64*)(pml4[8]&~0xFFFULL);
        for(int i3=0;i3<512;i3++){ if(!(pdpt[i3]&1))continue; u64* pd=(u64*)(pdpt[i3]&~0xFFFULL);
            for(int i2=0;i2<512;i2++){ if(!(pd[i2]&1))continue; u64* pt=(u64*)(pd[i2]&~0xFFFULL);
                for(int i1=0;i1<512;i1++) if(pt[i1]&1)pframe_free(pt[i1]&PTE_ADDR);
                pframe_free((u64)pt); }
            pframe_free((u64)pd); }
        pframe_free((u64)pdpt); pml4[8]=0; }
    pframe_free(cr3&~0xFFFULL);
}
static u64 vm_clone_user(u64 src){
    u64 dst=vm_new_pml4(); if(!dst)return 0; { extern void lx_res_dup(u64,u64); lx_res_dup(src,dst); }
    u64* ppml4=(u64*)(src&~0xFFFULL); int touched=0;
    if(ppml4[8]&1){ u64* ppdpt=(u64*)(ppml4[8]&PTE_ADDR);
        for(int i3=0;i3<512;i3++){ if(!(ppdpt[i3]&1))continue; u64* ppd=(u64*)(ppdpt[i3]&PTE_ADDR);
            for(int i2=0;i2<512;i2++){ if(!(ppd[i2]&1))continue; u64* ppt=(u64*)(ppd[i2]&PTE_ADDR);
                for(int i1=0;i1<512;i1++){ u64 e=ppt[i1]; if(!vm_pte_used(e))continue;
                    u64 v=((u64)8<<39)|((u64)i3<<30)|((u64)i2<<21)|((u64)i1<<12);
                    u64* d=vm_walk(dst,v,1); if(!d){ vm_free_user(dst); return 0; }
                    if(!(e&1)){ *d=PTE_LAZY|(e&PTE_PBITS); continue; }
                    u64 pa=e&PTE_ADDR;
                    if(!pf_owned(pa)){ u64 f=pframe_alloc(); if(!f){ vm_free_user(dst); return 0; }
                        u8* q=(u8*)f; u8* s=(u8*)pa; for(int b=0;b<4096;b++)q[b]=s[b]; *d=vm_live(f,e&~PTE_SHR); continue; }
                    if(e&PTE_SHR){ pframe_ref(pa); *d=e; continue; }
                    if((e&2)||((e&PTE_PRO)&&!(e&PTE_COW))){ e=(e&~2ULL)|PTE_COW; ppt[i1]=e; touched=1; }
                    pframe_ref(pa); *d=e; } } } }
    if(touched&&(cpu_get_pt()&PTE_ADDR)==(src&PTE_ADDR))cpu_set_pt(cpu_get_pt());
    if(touched)smp_tlb_sync(src);
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
extern const unsigned char _binary_user_mhello_elf_start[], _binary_user_mhello_elf_end[];
extern const unsigned char _binary_user_musltest_elf_start[], _binary_user_musltest_elf_end[];
extern const unsigned char _binary_user_smptest_elf_start[], _binary_user_smptest_elf_end[];
extern const unsigned char _binary_user_nettest_elf_start[], _binary_user_nettest_elf_end[];
extern const unsigned char _binary_user_featest_elf_start[], _binary_user_featest_elf_end[];
extern const unsigned char _binary_user_afutest_elf_start[], _binary_user_afutest_elf_end[];
extern const unsigned char _binary_user_kfeat_elf_start[], _binary_user_kfeat_elf_end[];
extern const unsigned char _binary_user_sqlite3_elf_start[], _binary_user_sqlite3_elf_end[];
extern const unsigned char _binary_user_gzip_elf_start[], _binary_user_gzip_elf_end[];
extern const unsigned char _binary_user_bzip2_elf_start[], _binary_user_bzip2_elf_end[];
extern const unsigned char _binary_user_xz_elf_start[], _binary_user_xz_elf_end[];
extern const unsigned char _binary_user_zstd_elf_start[], _binary_user_zstd_elf_end[];
extern const unsigned char _binary_user_qjs_elf_start[], _binary_user_qjs_elf_end[];
extern const unsigned char _binary_user_play_elf_start[], _binary_user_play_elf_end[];
extern const unsigned char _binary_user_kilo_elf_start[], _binary_user_kilo_elf_end[];
extern const unsigned char _binary_user_fetch_elf_start[], _binary_user_fetch_elf_end[];
extern const unsigned char _binary_user_dyntest_elf_start[], _binary_user_dyntest_elf_end[];
extern const unsigned char _binary_user_libc_so_elf_start[], _binary_user_libc_so_elf_end[];

const struct nvx_JS_BIN_t JS_BIN[]={
    {"count",_binary_user_count_elf_start,_binary_user_count_elf_end},
    {"spin",_binary_user_spin_elf_start,_binary_user_spin_elf_end},
    {"sigdemo",_binary_user_sigdemo_elf_start,_binary_user_sigdemo_elf_end},
    {"upper",_binary_user_upper_elf_start,_binary_user_upper_elf_end},
    {"greet",_binary_user_greet_elf_start,_binary_user_greet_elf_end},
    {"pipetest",_binary_user_pipetest_elf_start,_binary_user_pipetest_elf_end},
    NVXB("ls"),NVXB("cat"),NVXB("grep"),NVXB("wc"),NVXB("head"),NVXB("tail"),NVXB("echo"),NVXB("mkdir"),NVXB("rmdir"),NVXB("rm"),
    NVXB("mv"),NVXB("cp"),NVXB("touch"),NVXB("ln"),NVXB("chmod"),NVXB("chown"),NVXB("stat"),NVXB("pwd"),NVXB("hostname"),NVXB("whoami"),
    NVXB("id"),NVXB("uname"),NVXB("date"),NVXB("sleep"),NVXB("mount"),NVXB("umount"),NVXB("readlink"),NVXB("true"),NVXB("false"),NVXB("tee"),NVXB("sort"),NVXB("nvxbox"),
    NVXB("cut"),NVXB("tr"),NVXB("uniq"),NVXB("seq"),NVXB("basename"),NVXB("dirname"),NVXB("rev"),NVXB("tac"),NVXB("nl"),NVXB("fold"),
    NVXB("paste"),NVXB("comm"),NVXB("cmp"),NVXB("md5sum"),NVXB("sha256sum"),NVXB("base64"),NVXB("find"),NVXB("yes"),NVXB("printenv"),
    {"libc8",_binary_user_libc8_elf_start,_binary_user_libc8_elf_end},
    {"mhello",_binary_user_mhello_elf_start,_binary_user_mhello_elf_end},
    {"musltest",_binary_user_musltest_elf_start,_binary_user_musltest_elf_end},
    {"smptest",_binary_user_smptest_elf_start,_binary_user_smptest_elf_end},
    {"nettest",_binary_user_nettest_elf_start,_binary_user_nettest_elf_end},
    {"featest",_binary_user_featest_elf_start,_binary_user_featest_elf_end},
    {"afutest",_binary_user_afutest_elf_start,_binary_user_afutest_elf_end},
    {"kfeat",_binary_user_kfeat_elf_start,_binary_user_kfeat_elf_end},
    {"sqlite3",_binary_user_sqlite3_elf_start,_binary_user_sqlite3_elf_end},
    {"gzip",_binary_user_gzip_elf_start,_binary_user_gzip_elf_end},
    {"gunzip",_binary_user_gzip_elf_start,_binary_user_gzip_elf_end},
    {"zcat",_binary_user_gzip_elf_start,_binary_user_gzip_elf_end},
    {"bzip2",_binary_user_bzip2_elf_start,_binary_user_bzip2_elf_end},
    {"bunzip2",_binary_user_bzip2_elf_start,_binary_user_bzip2_elf_end},
    {"bzcat",_binary_user_bzip2_elf_start,_binary_user_bzip2_elf_end},
    {"xz",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"unxz",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"xzcat",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"lzma",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"unlzma",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"lzcat",_binary_user_xz_elf_start,_binary_user_xz_elf_end},
    {"zstd",_binary_user_zstd_elf_start,_binary_user_zstd_elf_end},
    {"unzstd",_binary_user_zstd_elf_start,_binary_user_zstd_elf_end},
    {"zstdcat",_binary_user_zstd_elf_start,_binary_user_zstd_elf_end},
    {"qjs",_binary_user_qjs_elf_start,_binary_user_qjs_elf_end},
    {"play",_binary_user_play_elf_start,_binary_user_play_elf_end},
    {"kilo",_binary_user_kilo_elf_start,_binary_user_kilo_elf_end},
    {"fetch",_binary_user_fetch_elf_start,_binary_user_fetch_elf_end},
    {"dyntest",_binary_user_dyntest_elf_start,_binary_user_dyntest_elf_end},
    {"+init",_binary_user_init_elf_start,_binary_user_init_elf_end},
    {0,0,0}};
static int jc_ieq(const char* a,const char* b){ while(*a&&*b){ char x=*a,y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; a++;b++; } return !*a&&!*b; }
static struct { const u8* s; u8* d; u32 n; } g_unz[24];
static const u8* jc_unz(const u8* s,u32* len){
    if(*len<12||s[0]!='N'||s[1]!='V'||s[2]!='Z'||s[3]!='1')return s;
    for(int k=0;k<24;k++) if(g_unz[k].s==s){ *len=g_unz[k].n; return g_unz[k].d; }
    u32 n=(u32)s[4]|((u32)s[5]<<8)|((u32)s[6]<<16)|((u32)s[7]<<24);
    u8* d=(u8*)kmalloc(n); if(!d)return 0;
    int r=pz_inflate_raw(s+8,(int)(*len-8),d,(int)n); if(r!=(int)n){ kfree(d); return 0; }
    for(int k=0;k<24;k++) if(!g_unz[k].s){ g_unz[k].s=s; g_unz[k].d=d; g_unz[k].n=n; break; }
    *len=n; return d;
}
const u8* jc_find_prog(const char* name,u32* len){
    char rl[192]; { extern int ux_resolve_links(const char*,char*,int); if(name[0]=='/'&&ux_resolve_links(name,rl,(int)sizeof(rl))==0)name=rl; }
    if(name[0]=='/'){ int sx=0; const u8* sd=sysroot_get(name,len,&sx); if(sd&&sx&&*len>=64&&sd[0]==0x7F&&sd[1]=='E')return sd; }
    const char* n=name; if(n[0]=='/'&&n[1]=='b'&&n[2]=='i'&&n[3]=='n'&&n[4]=='/')n+=5; else if(n[0]=='/'&&n[1]=='u'&&n[2]=='s'&&n[3]=='r'&&n[4]=='/'&&n[5]=='b'&&n[6]=='i'&&n[7]=='n'&&n[8]=='/')n+=9;
    else if(n[0]=='/'&&n[1]=='s'&&n[2]=='b'&&n[3]=='i'&&n[4]=='n'&&n[5]=='/'){ n+=6; if(n[0]=='i'&&n[1]=='n'&&n[2]=='i'&&n[3]=='t'&&!n[4])n="+init"; }
    char base[64]; int k=0; while(n[k]&&k<63){ base[k]=n[k]; k++; } base[k]=0;
    if(k>4&&jc_ieq(base+k-4,".elf"))base[k-4]=0;
    if(jc_ieq(base,"init")&&(name[0]=='+'||g_jcur<0||1)){ for(int i=0;JS_BIN[i].name;i++) if(JS_BIN[i].name[0]=='+'){ *len=(u32)(JS_BIN[i].e-JS_BIN[i].s); return JS_BIN[i].s; } }
    for(int i=0;JS_BIN[i].name;i++) if(jc_ieq(base,JS_BIN[i].name)){ *len=(u32)(JS_BIN[i].e-JS_BIN[i].s); return jc_unz(JS_BIN[i].s,len); }
    int isd_=0; int f=ux_lookup(name,&isd_,1); if(isd_)f=-1;
    if(f>=0&&FS[f].len>=64){ const char* fd0=fsd(f); if((u8)fd0[0]==0x7F&&fd0[1]=='E'&&fd0[2]=='L'&&fd0[3]=='F'){
        if(g_uid!=0&&!(FS[f].mode&0111))return 0; *len=(u32)FS[f].len; return (const u8*)fd0; } }
    { int slash=0; for(const char* q=name;*q;q++) if(*q=='/')slash=1; if(!slash&&name[0]){ static const char* const pres[]={"/opt/gnu/bin/","/usr/local/bin/","/usr/bin/","/bin/","/usr/sbin/","/sbin/",0};
        for(int pi=0;pres[pi];pi++){ char gp[128]; int o=0; const char* pre=pres[pi]; while(*pre)gp[o++]=*pre++; for(int q=0;name[q]&&o<127;q++)gp[o++]=name[q]; gp[o]=0; int sx=0; const u8* sd=sysroot_get(gp,len,&sx); if(sd&&sx&&*len>=64&&sd[0]==0x7F&&sd[1]=='E')return sd; } } }
    int got=fat_read_file(name,g_spawnbuf,(int)sizeof(g_spawnbuf));
    if(got>=64&&g_spawnbuf[0]==0x7F&&g_spawnbuf[1]=='E'){ *len=(u32)got; return g_spawnbuf; }
    return 0;
}
const u8* jc_find_lib(const char* path,u32* len){
    const char* b=path; for(const char* q=path;*q;q++) if(*q=='/')b=q+1;
    if(jc_ieq(b,"ld-musl-x86_64.so.1")||jc_ieq(b,"libc.so")||jc_ieq(b,"libc.musl-x86_64.so.1")){ *len=(u32)(_binary_user_libc_so_elf_end-_binary_user_libc_so_elf_start); return jc_unz(_binary_user_libc_so_elf_start,len); }
    { int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd&&FS[f].len>=64){ const char* d=fsd(f); if((u8)d[0]==0x7F&&d[1]=='E'){ *len=(u32)FS[f].len; return (const u8*)d; } } }
    { const u8* sd=sysroot_get(path,len,0); if(sd&&*len>=64)return sd; }
    return 0;
}
static const u8* jc_file_head(const char* path,u32* len){ const u8* d=sysroot_get(path,len,0); if(d)return d; int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd){ *len=(u32)FS[f].len; return (const u8*)fsd(f); } return 0; }
static int jc_script(const char* name,char* full,int cap,char* interp,int icap){
    static const char* const pres[]={"/opt/gnu/bin/","/usr/local/bin/","/usr/bin/","/bin/","/usr/sbin/","/sbin/",0};
    int slash=0; for(const char* q=name;*q;q++) if(*q=='/')slash=1;
    for(int pi=-1;pi<=6;pi++){ if(pi<0&&!slash)continue; if(pi>=0&&slash)break;
        char p[160]; int o=0; if(pi>=0&&pi<6){ const char* pr=pres[pi]; while(*pr&&o<100)p[o++]=*pr++; }
        else if(pi==6){ extern const char* env_get(const char*); const char* h=env_get("HOME"); if(!h||h[0]!='/')continue; if(g_uid==0&&!(h[1]=='r'&&h[2]=='o'&&h[3]=='o'&&h[4]=='t'&&!h[5]))continue; while(*h&&o<90)p[o++]=*h++; const char* lb="/.local/bin/"; while(*lb)p[o++]=*lb++; }
        else if(name[0]!='/'){ char cw[128]; dt_path_n(g_dcwd,cw,128); for(int k=0;cw[k]&&o<100;k++)p[o++]=cw[k]; if(o>1)p[o++]='/'; if(name[0]=='.'&&name[1]=='/')name+=2; }
        for(int k=0;name[k]&&o<159;k++)p[o++]=name[k]; p[o]=0;
        u32 L=0; const u8* d=jc_file_head(p,&L); if(!d||L<3||d[0]!='#'||d[1]!='!')continue;
        int a=2; while(a<(int)L&&a<200&&(d[a]==' '||d[a]=='\t'))a++; int k=0; while(a<(int)L&&a<200&&d[a]!='\n'&&d[a]!='\r'&&k<icap-1)interp[k++]=(char)d[a++]; while(k>0&&(interp[k-1]==' '||interp[k-1]=='\t'))k--; interp[k]=0;
        if(!k)return 0; int q=0; for(;p[q]&&q<cap-1;q++)full[q]=p[q]; full[q]=0; return 1; }
    return 0; }
static int jc_is_prog(const char* name){ u32 l; if(!name[0])return 0; if(jc_find_prog(name,&l))return 1; char f[160], i[96]; return jc_script(name,f,160,i,96); }

static u64 jc_build(const char* args,u64* entry,u64* rsp,u64* argc,u64* argv){
    char nm[64]; int k=0; while(args[k]&&args[k]!=' '&&k<63){ nm[k]=args[k]; k++; } nm[k]=0;
    u32 len=0; const u8* elf=jc_find_prog(nm,&len);
    if(!elf){ static int depth; char full[160], ip[96]; if(depth<2&&jc_script(nm,full,160,ip,96)){ static char na[2048]; int o=0; for(int q=0;ip[q]&&o<2000;q++)na[o++]=ip[q]; na[o++]=' '; for(int q=0;full[q]&&o<2040;q++)na[o++]=full[q]; const char* rest=args+k; for(int q=0;rest[q]&&o<2046;q++)na[o++]=rest[q]; na[o]=0; depth++; u64 r=jc_build(na,entry,rsp,argc,argv); depth--; return r; } g_xa_set=0; g_xe_set=0; return 0; }
    { int j=0; while(args[j]&&j<2047){ g_exec_args[j]=args[j]; j++; } g_exec_args[j]=0; }
    u64 cr3=elf_build_space(elf,len,entry); g_xa_set=0; g_xe_set=0; if(!cr3)return 0;
    for(int p=1;p<JS_STK_PAGES;p++){ u64 v=USTK_V-(u64)p*0x1000ULL; u64* e=vm_walk(cr3,v,0); if(e&&vm_pte_used(*e))continue;
        if(p>=16){ u64* g=vm_walk(cr3,v-0x1000ULL,0); if(g&&vm_pte_used(*g))break; }
        if(vm_map_lazy(cr3,v)){ vm_free_user(cr3); return 0; } }
    *rsp=g_arg_rsp?g_arg_rsp:USTK_V+0x1000ULL; *argc=(u64)g_arg_argc; *argv=g_arg_argv; return cr3;
}
static void jc_fx_init(u8* fx){ for(int i=0;i<512;i++)fx[i]=0; *(u16*)fx=0x037F; *(u32*)(fx+24)=0x1F80; *(u32*)(fx+28)=0xFFFF; }
static void jc_ctx_fresh(regs_pt* c,u64 entry,u64 rsp,u64 argc,u64 argv){
    for(int i=0;i<(int)(sizeof(*c)/8);i++)((u64*)c)[i]=0;
    c->rip=entry; c->cs=0x1B; c->rflags=0x202; c->rsp=rsp; c->ss=0x23; c->rdi=argc; c->rsi=argv;
}
static void jc_cmdname(struct jproc* p,const char* args){ { extern void lx_set_exe(char*,const char*); p->exe[0]=0; lx_set_exe(p->exe,args); } int i=0; while(args[i]&&i<47){ p->cmd[i]=args[i]; i++; } p->cmd[i]=0; }
static int jc_alloc(void){ for(int i=0;i<JMAX;i++) if(g_jp[i].state==S_FREE)return i; return -1; }
int jc_create(const char* args,int ppid,int pgid){
    int i=jc_alloc(); if(i<0)return -1;
    u64 entry,rsp,argc,argv; u64 cr3=jc_build(args,&entry,&rsp,&argc,&argv); if(!cr3)return -2;
    struct jproc* p=&g_jp[i]; u8* raw=(u8*)p; for(u32 q=0;q<sizeof(*p);q++)raw[q]=0;
    p->pid=g_next_pid++; p->tg=i; p->tgid=p->pid; p->ppid=ppid; p->pgid=pgid?pgid:p->pid; p->sid=1; p->uid=g_uid; p->gid=g_gid; p->cwd=g_dcwd; p->umask=g_umask;
    p->cr3=cr3; p->brk=0x40002000000ULL; p->mmnext=0x40004000000ULL; p->lx=g_elf_pie;
    jc_ctx_fresh(&p->ctx,entry,rsp,argc,argv);
    for(int f=0;f<FDMAX;f++)p->fd[f]=-1;
    int con=jc_con(); for(int f=0;f<3;f++){ p->fd[f]=con; vf_ref(con); }
    jc_cmdname(p,args); jc_fx_init(p->fx); p->state=S_RUN;
    smp_kick_idle();
    return i;
}

int sig_default(int s){ switch(s){ case 17: case 28: case 23: return 0; case 18: return 3; case 19: case 20: case 21: case 22: return 2; default: return 1; } }
static int jc_tg_member(int j,int L){ struct jproc* q=&g_jp[j]; return q->state!=S_FREE&&q->state!=S_ZOMB&&q->state!=S_TDEAD&&q->tg==L; }
int jc_tg_live(int lead,int except){ int n=0; for(int j=0;j<JMAX;j++) if(j!=except&&jc_tg_member(j,lead))n++; return n; }
