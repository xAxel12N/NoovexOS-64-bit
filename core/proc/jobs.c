#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/apps/internet/browser.h"

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
        if(!(*e&1)||((*e&PTE_COW)&&!(*e&2))){ if(vm_fault_user(cr3,v,1))return 0; continue; }
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
    { extern void un_close(int); extern void mfd_close(int); extern void evtm_close(int,int); if(v->dev==18)un_close(v->fs_idx); else if(v->dev==19)mfd_close(v->fs_idx); else if(v->dev==20||v->dev==21)evtm_close(v->dev,v->fs_idx); }
    if(v->dev==9&&g_fb0_open>0)g_fb0_open--; if(v->dev==10&&g_mouse_open>0)g_mouse_open--;
    if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N&&g_fs_dirty[v->fs_idx]){ int k=v->fs_idx; g_fs_dirty[k]=0; if(FS[k].used&&!FS[k].orphan&&disk_ok)fs_wb_mark(k); }
    v->used=0;
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
                for(int i1=0;i1<512;i1++) if(pt[i1]&1)pframe_free(pt[i1]&~0xFFFULL);
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
                    if(!(e&1)){ *d=PTE_LAZY; continue; }
                    u64 pa=e&PTE_ADDR;
                    if(!pf_owned(pa)){ u64 f=pframe_alloc(); if(!f){ vm_free_user(dst); return 0; }
                        u8* q=(u8*)f; u8* s=(u8*)pa; for(int b=0;b<4096;b++)q[b]=s[b]; *d=f|0x7ULL; continue; }
                    if(e&PTE_SHR){ pframe_ref(pa); *d=e; continue; }
                    if(e&2){ e=(e&~2ULL)|PTE_COW; ppt[i1]=e; touched=1; }
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
    { const u8* sd=sysroot_get(path,len,0); if(sd&&*len>=64)return sd; }
    int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd&&FS[f].len>=64){ *len=(u32)FS[f].len; return (const u8*)fsd(f); }
    return 0;
}
static const u8* jc_file_head(const char* path,u32* len){ const u8* d=sysroot_get(path,len,0); if(d)return d; int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd){ *len=(u32)FS[f].len; return (const u8*)fsd(f); } return 0; }
static int jc_script(const char* name,char* full,int cap,char* interp,int icap){
    static const char* const pres[]={"/opt/gnu/bin/","/usr/local/bin/","/usr/bin/","/bin/","/usr/sbin/","/sbin/",0};
    int slash=0; for(const char* q=name;*q;q++) if(*q=='/')slash=1;
    for(int pi=-1;pres[pi<0?0:pi];pi++){ if(pi<0&&!slash)continue; if(pi>=0&&slash)break;
        char p[160]; int o=0; if(pi>=0){ const char* pr=pres[pi]; while(*pr&&o<100)p[o++]=*pr++; }
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
static int jc_push_handler(int i,int s){
    struct jproc* p=&g_jp[i];
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
    p->fsbase=0; p->gsbase=0; p->lxbrk=0; p->pollwake=0; p->susp=0; p->lx=g_elf_pie;
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
        if(av&&jc_uok((u64)av,8)) for(int k=1;k<8&&av[k];k++){ a[n++]=' '; const char* s=(const char*)av[k]; for(int q=0;s[q]&&n<126;q++)a[n++]=s[q]; }
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
        if(cmd==0){ int n=-1; for(int f=(int)r->rdx;f<FDMAX;f++) if(t[f]==-1){ n=f; break; } if(n>=0){ t[n]=t[fd]; vf_ref(t[fd]); fdce()[n]=0; } r->rax=(u64)(long)n; }
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
    case 12: { u64 addr=r->rbx; if(addr==0){ r->rax=g_ubrk; return 1; } u64 nb=(addr+0xFFF)&~0xFFFULL; while(g_ubrk<nb){ u64 f=pframe_alloc(); if(!f)break; vm_map(p->cr3,g_ubrk,f,0x7ULL); g_ubrk+=0x1000; } r->rax=g_ubrk; return 1; }
    }
    return 0;
}

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
    u64 va=cpu_cr2(); if(!jc_uok(va,1)){ segv_rec(r,va); return 0; }
    int rc=vm_fault_user(cpu_get_pt(),va,(int)((r->errcode>>1)&1));
    if(rc==-1){ extern int lx_res_fault(u64,u64); if(lx_res_fault(cpu_get_pt(),va)==0)rc=vm_fault_user(cpu_get_pt(),va,(int)((r->errcode>>1)&1)); }
    if(rc==0)return 1;
    if(rc==-1)segv_rec(r,va);
    if(rc==-2){ jc_finish(g_jcur,9); jc_leave(); }
    return 0;
}
void jc_fault(regs_t* rr){
    struct jproc* p=&g_jp[g_jcur]; memcpy(&p->ctx,rr,sizeof(p->ctx));
    int v=(int)rr->vector; int s=(v==0||v==16||v==19)?8:(v==6)?4:(v==1||v==3)?5:11;
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
