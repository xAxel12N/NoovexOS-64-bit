#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <asm/prctl.h>

typedef uint64_t u64; typedef uint32_t u32;
#define PFB 0x0C000000ULL
#define PFT 0x0E000000ULL
#define WIN 0x40000000000ULL
#define NPG (1u<<22)
#define ADDR 0x000FFFFFFFFFF000ULL

u64 hk_setup(void); int hk_spawn(const char*); int hk_step(void); int hk_state(int); int hk_exitst(int); int hk_pid(int); void hk_reap(int);
const char* hk_cap(int*); void hk_cap_reset(void); void hk_fault(void*); u64 hk_fsbase(void); u32 hk_free_frames(void); int hk_free_vfiles(void); int hk_free_pipes(void); int hk_used_files(void);
void hk_setenv(const char*,const char*); int hk_pf(u64,int); int hk_readfile(const char*,char*,int); int hk_exists(const char*); int hk_addfile(const char*,const char*,const unsigned char*,int); void hk_delfile(int);
int hk_shm_clear(void); void linux_syscall(void*); void syscall_dispatch(void*); int hk_dns_build(const char*,unsigned char*,int); void hk_udp_big(void);

static int g_memfd; static u64 g_kpml4, g_cur; static u32 g_map[NPG]; static u32 g_maxidx;
static u64 g_host_fs; static ucontext_t* g_uc; static const void* g_pending_fx;
static unsigned long g_syscalls, g_syncs, g_pfaults; static u64 g_last[7]; static u64 g_ring[16]; static int g_rn;

static long raw5(long n,long a,long b){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory"); return r; }
static void set_fs(u64 v){ raw5(SYS_arch_prctl,ARCH_SET_FS,(long)v); }

void h_fatal(const char* m){ set_fs(g_host_fs); fprintf(stderr,"HARNESS FATAL: %s\n",m); fflush(stderr); _exit(90); }
static u64* pte_of(u64 cr3,u64 va){
    u64* l4=(u64*)(cr3&ADDR); u64 e=l4[(va>>39)&511]; if(!(e&1))return 0;
    u64* l3=(u64*)(e&ADDR); e=l3[(va>>30)&511]; if(!(e&1)||(e&0x80))return 0;
    u64* l2=(u64*)(e&ADDR); e=l2[(va>>21)&511]; if(!(e&1)||(e&0x80))return 0;
    u64* l1=(u64*)(e&ADDR); return &l1[(va>>12)&511];
}
static void map_one(u64 va,u64 pte){
    if(va<WIN)return; u64 idx=(va-WIN)>>12; if(idx>=NPG)h_fatal("guest page outside the 16 GB test window");
    u32 want=0;
    if(pte&1){ u64 fr=pte&ADDR; if(fr<PFB||fr>=PFT)h_fatal("PTE points outside the frame pool"); want=(u32)((fr-PFB)>>12)+1; if(!(pte&2))want|=0x80000000u; }
    if(g_map[idx]==want)return;
    if(want){ int prot=(want&0x80000000u)?(PROT_READ|PROT_EXEC):(PROT_READ|PROT_WRITE|PROT_EXEC); if(mmap((void*)va,4096,prot,MAP_SHARED|MAP_FIXED,g_memfd,(off_t)((want&0x7FFFFFFFu)-1)*4096)==MAP_FAILED)h_fatal("mmap"); if(idx+1>g_maxidx)g_maxidx=(u32)idx+1; }
    else munmap((void*)va,4096);
    g_map[idx]=want;
}
static void sync_all(u64 cr3){
    g_syncs++;
    for(u32 i=0;i<g_maxidx;i++) if(g_map[i]){ u64 va=WIN+((u64)i<<12); u64* p=pte_of(cr3,va); map_one(va,p?*p:0); }
    u64* l4=(u64*)(cr3&ADDR); u64 e4=l4[8]; if(!(e4&1))return;
    u64* l3=(u64*)(e4&ADDR);
    for(int a=0;a<512;a++){ if(!(l3[a]&1))continue; u64* l2=(u64*)(l3[a]&ADDR);
        for(int b=0;b<512;b++){ if(!(l2[b]&1))continue; u64* l1=(u64*)(l2[b]&ADDR);
            for(int c=0;c<512;c++) if(l1[c]&1) map_one(WIN|((u64)a<<30)|((u64)b<<21)|((u64)c<<12),l1[c]); } }
}
void h_set_pt(u64 v){ g_cur=v; if((v&ADDR)!=(g_kpml4&ADDR))sync_all(v); }
u64 h_get_pt(void){ return g_cur; }
void h_flush_page(u64 va){ if(va<WIN||va>=WIN+((u64)NPG<<12))return; if((g_cur&ADDR)==(g_kpml4&ADDR))return; u64* p=pte_of(g_cur,va); map_one(va,p?*p:0); }
void h_fpu_save(void* p){ if(g_uc&&g_uc->uc_mcontext.fpregs)memcpy(p,g_uc->uc_mcontext.fpregs,512); else __asm__ volatile("fxsave (%0)"::"r"(p):"memory"); }
void h_fpu_restore(const void* p){ g_pending_fx=p; }

__asm__(".text\n.globl k_setjmp\nk_setjmp:\n mov %rbx,0(%rdi)\n mov %rbp,8(%rdi)\n mov %r12,16(%rdi)\n mov %r13,24(%rdi)\n mov %r14,32(%rdi)\n mov %r15,40(%rdi)\n lea 8(%rsp),%rax\n mov %rax,48(%rdi)\n mov (%rsp),%rax\n mov %rax,56(%rdi)\n xor %eax,%eax\n ret\n"
        ".globl k_longjmp\nk_longjmp:\n mov 0(%rdi),%rbx\n mov 8(%rdi),%rbp\n mov 16(%rdi),%r12\n mov 24(%rdi),%r13\n mov 32(%rdi),%r14\n mov 40(%rdi),%r15\n mov 48(%rdi),%rsp\n mov %esi,%eax\n test %eax,%eax\n jnz 1f\n mov $1,%eax\n1: jmp *56(%rdi)\n"
        ".globl hh_iret\nhh_iret:\n mov %rdi,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %r11\n pop %r10\n pop %r9\n pop %r8\n pop %rbp\n pop %rdi\n pop %rsi\n pop %rdx\n pop %rcx\n pop %rbx\n pop %rax\n iretq\n");
void hh_iret(u64* f) __attribute__((noreturn));
static u64 g_frame[20] __attribute__((aligned(16)));
void jc_resume(u64* ctx) __attribute__((noreturn));
void jc_resume(u64* ctx){
    for(int i=0;i<15;i++)g_frame[i]=ctx[i];
    g_frame[15]=ctx[17]; g_frame[16]=0x33; g_frame[17]=(ctx[19]&0xCD5)|0x202; g_frame[18]=ctx[20]; g_frame[19]=0x2b;
    if(g_frame[15]<WIN||g_frame[15]>=WIN*2)h_fatal("resume to a non-user rip");
    g_uc=0;
    if(g_pending_fx){ __asm__ volatile("fxrstor (%0)"::"r"(g_pending_fx)); g_pending_fx=0; }
    set_fs(hk_fsbase());
    hh_iret(g_frame);
}

static const int GR[15]={REG_R15,REG_R14,REG_R13,REG_R12,REG_R11,REG_R10,REG_R9,REG_R8,REG_RBP,REG_RDI,REG_RSI,REG_RDX,REG_RCX,REG_RBX,REG_RAX};
static void on_sys(int sig,siginfo_t* si,void* ucv){
    (void)sig; set_fs(g_host_fs);
    ucontext_t* uc=(ucontext_t*)ucv; greg_t* g=uc->uc_mcontext.gregs;
    u64 sr[20]; for(int i=0;i<15;i++)sr[i]=(u64)g[GR[i]];
    sr[15]=(u64)g[REG_RIP]; sr[16]=0x1B; sr[17]=(u64)g[REG_EFL]; sr[18]=(u64)g[REG_RSP]; sr[19]=0x23;
    g_syscalls++; g_uc=uc; g_ring[g_rn++&15]=sr[14]; g_last[0]=sr[14]; g_last[1]=sr[9]; g_last[2]=sr[10]; g_last[3]=sr[11]; g_last[4]=sr[5]; g_last[5]=sr[7]; g_last[6]=sr[6];
    if(si->si_arch==0x40000003u)syscall_dispatch(sr); else linux_syscall(sr);
    g_uc=0;
    for(int i=0;i<15;i++)g[GR[i]]=(greg_t)sr[i];
    g[REG_RIP]=(greg_t)sr[15]; g[REG_RSP]=(greg_t)sr[18];
    set_fs(hk_fsbase());
}
static void on_fault(int sig,siginfo_t* si,void* ucv){
    set_fs(g_host_fs);
    ucontext_t* uc=(ucontext_t*)ucv; greg_t* g=uc->uc_mcontext.gregs; u64 rip=(u64)g[REG_RIP];
    if((sig==SIGSEGV||sig==SIGBUS)&&(u64)si->si_addr>=WIN&&(u64)si->si_addr<WIN*2){
        g_pfaults++;
        if(hk_pf((u64)si->si_addr,(int)((g[REG_ERR]>>1)&1))==0){ u64 va=(u64)si->si_addr&~0xFFFULL; if(((va-WIN)>>12)<NPG){ u64* pp=pte_of(g_cur,va); map_one(va,pp?*pp:0); } if(rip>=WIN&&rip<WIN*2)set_fs(hk_fsbase()); return; } }
    if(rip<WIN||rip>=WIN*2){ fprintf(stderr,"HARNESS CRASH: signal %d at rip=%llx addr=%p\n",sig,(unsigned long long)rip,si->si_addr); fflush(stderr); _exit(91); }
    if(getenv("HH_DEBUG"))fprintf(stderr,"guest fault: signal %d code %d pid %d rip=%llx addr=%p rsp=%llx last syscall %llu(%llx,%llx,%llx,%llx,%llx,%llx)\n",sig,si->si_code,(int)si->si_pid,(unsigned long long)rip,si->si_addr,(unsigned long long)g[REG_RSP],(unsigned long long)g_last[0],(unsigned long long)g_last[1],(unsigned long long)g_last[2],(unsigned long long)g_last[3],(unsigned long long)g_last[4],(unsigned long long)g_last[5],(unsigned long long)g_last[6]);
    if(getenv("HH_DEBUG")){ for(int i=0;i<16;i++)fprintf(stderr," %llu",(unsigned long long)g_ring[(g_rn+i)&15]); fprintf(stderr,"\n"); }
    u64 r[22]; for(int i=0;i<15;i++)r[i]=(u64)g[GR[i]];
    r[15]=sig==SIGSEGV||sig==SIGBUS?14:sig==SIGILL?6:sig==SIGFPE?0:3; r[16]=0; r[17]=rip; r[18]=0x1B; r[19]=(u64)g[REG_EFL]; r[20]=(u64)g[REG_RSP]; r[21]=0x23;
    g_uc=uc; hk_fault(r);
    h_fatal("jc_fault returned");
}
static void on_alarm(int s){ (void)s; set_fs(g_host_fs); fprintf(stderr,"HARNESS TIMEOUT\n"); _exit(92); }

static int g_fail, g_pass;
static void rec(int ok,const char* name,const char* det){ if(ok)g_pass++; else g_fail++; printf("  %-64s %s%s%s\n",name,ok?"ok":"FAIL",(!ok&&det)?"   ":"",(!ok&&det)?det:""); fflush(stdout); }
static int run_prog(const char* args,int* status,char* out,int cap){
    hk_cap_reset();
    int i=hk_spawn(args); if(i<0){ *status=-1; out[0]=0; return 0; }
    int pid=hk_pid(i);
    for(long it=0;it<2000000;it++){ if(!hk_step())break; }
    *status=(hk_state(i)==5&&hk_pid(i)==pid)?hk_exitst(i):-2; hk_reap(i);
    for(int k=0;k<16;k++) if(hk_state(k)==5)hk_reap(k);
    int n; const char* c=hk_cap(&n); if(n>cap-1)n=cap-1; memcpy(out,c,(size_t)n); out[n]=0;
    return 1;
}
int main(int argc,char** argv){
    setvbuf(stdout,0,_IOLBF,0);
    signal(SIGALRM,on_alarm); alarm(120);
    raw5(SYS_arch_prctl,ARCH_GET_FS,(long)&g_host_fs);
    g_memfd=memfd_create("nvxframes",0); if(g_memfd<0||ftruncate(g_memfd,(off_t)(PFT-PFB)))h_fatal("memfd");
    if(mmap((void*)PFB,PFT-PFB,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED_NOREPLACE,g_memfd,0)!=(void*)PFB)h_fatal("frame pool mmap");
    if(mmap((void*)0x10000000ULL,0x4000000ULL,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=(void*)0x10000000ULL)h_fatal("heap mmap");
    stack_t ss; ss.ss_sp=malloc(1<<17); ss.ss_size=1<<17; ss.ss_flags=0; sigaltstack(&ss,0);
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_NODEFER; sigemptyset(&sa.sa_mask);
    sa.sa_sigaction=on_sys; sigaction(SIGSYS,&sa,0);
    sa.sa_sigaction=on_fault; sigaction(SIGSEGV,&sa,0); sigaction(SIGBUS,&sa,0); sigaction(SIGILL,&sa,0); sigaction(SIGFPE,&sa,0); sigaction(SIGTRAP,&sa,0);
    g_kpml4=hk_setup(); g_cur=g_kpml4; if(!g_kpml4)h_fatal("setup");
    struct sock_filter f[]={
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,12),
        BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K,0x400,0,2),
        BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K,0x800,1,0),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_TRAP),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW) };
    struct sock_fprog fp={(unsigned short)(sizeof f/sizeof f[0]),f};
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&fp))h_fatal("seccomp");
    u32 fr0=hk_free_frames(); int vf0=hk_free_vfiles(), pp0=hk_free_pipes(), fs0=hk_used_files();
    static char out[16384]; int st;
    run_prog("mhello",&st,out,sizeof out);
    printf("---- mhello ----\n%s",out);
    rec(st==0&&strstr(out,"Hello from musl libc on NoovexOS ")&&strstr(out," (x86_64)")&&strstr(out,"argc=1 pi=3.14159"),"mhello: static-PIE musl program loads, prints and exits 0",out);
    run_prog("mhello x",&st,out,sizeof out);
    rec(st==(3<<8),"mhello x: argv reaches main, exit status 3",0);
    hk_setenv("MT_VAR","viaenv");
    run_prog("mhello env",&st,out,sizeof out);
    rec(st==(4<<8)&&strstr(out,"viaenv"),"environment from the kernel reaches getenv()",out);
    run_prog("pipetest",&st,out,sizeof out);
    rec(st==0&&strstr(out,"parent read: hello through a pipe, from child")&&strstr(out,"exited=1 status=7"),"legacy int 0x80 program (pipetest: fork, pipe, waitpid) still runs",out);
    run_prog("libc8",&st,out,sizeof out);
    rec(st==0&&strstr(out,"sha256(hello)=2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"),"legacy libc program (libc8) still runs",out);
    run_prog("echo hello legacy world",&st,out,sizeof out);
    rec(st==0&&strstr(out,"hello legacy world"),"legacy nvxbox echo still runs",out);
    run_prog("seq 3",&st,out,sizeof out);
    rec(st==0&&strstr(out,"1\n2\n3\n"),"legacy nvxbox seq still runs",out);
    { static unsigned char data[300000], back[300000]; unsigned s=12345; int L=0;
        for(int i=0;i<6000;i++){ s=s*1103515245u+12345u; L+=snprintf((char*)data+L,sizeof data-L,"line %d value %u %s\n",i,s>>8,(s>>20)&1?"alpha":"beta"); }
        int f0=hk_used_files();
        static const char* T[][4]={ {"gzip /tmp/data.txt","/tmp/data.txt.gz","gunzip /tmp/data.txt.gz","gzip/gunzip (zlib 1.3.2)"},
                                    {"bzip2 /tmp/data.txt","/tmp/data.txt.bz2","bunzip2 /tmp/data.txt.bz2","bzip2/bunzip2 1.0.8"},
                                    {"xz /tmp/data.txt","/tmp/data.txt.xz","unxz /tmp/data.txt.xz","xz/unxz 5.8.4 (default level scaled to the memory limit)"},
                                    {"zstd -q --rm /tmp/data.txt","/tmp/data.txt.zst","unzstd -q --rm /tmp/data.txt.zst","zstd/unzstd 1.5.7"} };
        for(int t=0;t<4;t++){
            int k=hk_addfile("/tmp","data.txt",data,L);
            int s1,s2; char o1[4096],o2[4096];
            run_prog(T[t][0],&s1,o1,sizeof o1); int made=hk_exists(T[t][1]), gone=!hk_exists("/tmp/data.txt");
            static unsigned char cz[300000]; int czn=hk_readfile(T[t][1],(char*)cz,sizeof cz);
            run_prog(T[t][2],&s2,o2,sizeof o2); int n=hk_readfile("/tmp/data.txt",(char*)back,sizeof back);
            char det[200]; snprintf(det,sizeof det,"st=%x/%x made=%d gone=%d size=%d->%d back=%d %s %s",s1,s2,made,gone,L,czn,n,o1,o2);
            char nm[120]; snprintf(nm,sizeof nm,"%s: compresses and restores the file exactly",T[t][3]);
            rec(k>=0&&s1==0&&s2==0&&made&&gone&&czn>0&&czn<L/2&&n==L&&!memcmp(back,data,(size_t)L)&&!hk_exists(T[t][1]),nm,det);
            run_prog("rm /tmp/data.txt",&s1,o1,sizeof o1); }
        int k=hk_addfile("/tmp","small.txt",(const unsigned char*)"hello zcat\n",11); (void)k;
        run_prog("gzip /tmp/small.txt",&st,out,sizeof out); run_prog("zcat /tmp/small.txt.gz",&st,out,sizeof out);
        rec(st==0&&!strcmp(out,"hello zcat\n"),"zcat prints a .gz file to the terminal",out);
        run_prog("rm /tmp/small.txt.gz",&st,out,sizeof out);
        run_prog("sqlite3 -list /tmp/t.db create\037table\037t(a,b);\037insert\037into\037t\037values(1,'x'),(2,'y');\037select\037sum(a)||'|'||group_concat(b)\037from\037t;",&st,out,sizeof out);
        rec(st==0&&!strcmp(out,"3|x,y\n"),"sqlite3 3.53.4: creates a database, inserts and queries",out);
        run_prog("sqlite3 -list /tmp/t.db insert\037into\037t\037values(3,'z');\037select\037count(*),sqlite_version()\037from\037t;",&st,out,sizeof out);
        rec(st==0&&!strcmp(out,"3|3.53.4\n")&&!hk_exists("/tmp/t.db-journal"),"sqlite3: data survives between runs, journal cleaned up",out);
        run_prog("sqlite3 -list /tmp/t.db pragma\037integrity_check;",&st,out,sizeof out);
        rec(st==0&&!strcmp(out,"ok\n"),"sqlite3: integrity_check passes",out);
        run_prog("sqlite3 /tmp/t.db select\037a,b\037from\037t;",&st,out,sizeof out);
        { int hi=0; for(int i=0;out[i];i++) if((unsigned char)out[i]>=0x80)hi=1; rec(st==0&&!hi&&strchr(out,'+')&&strchr(out,'|')&&strstr(out,"| 3 | z |"),"sqlite3 table boxes are drawn with ASCII on the NoovexOS terminal",out); }
        run_prog("rm /tmp/t.db",&st,out,sizeof out);
        rec(hk_used_files()==f0,"compression and SQLite tests leave no files behind",0); }
    { static char h[600]; static unsigned char qb[300+64]; for(int i=0;i<599;i++)h[i]=(i%60==59)?'.':'a'; h[599]=0;
      for(int i=0;i<64;i++)qb[300+i]=0xA5; int r1=hk_dns_build(h,qb,300); int guard=1; for(int i=0;i<64;i++)if(qb[300+i]!=0xA5)guard=0;
      h[280]=0; int r2=hk_dns_build(h,qb,300); for(int i=0;i<64;i++)if(qb[300+i]!=0xA5)guard=0;
      int r3=hk_dns_build("www.example.com",qb,300);
      char d[96]; snprintf(d,sizeof d,"r1=%d r2=%d r3=%d guard=%d",r1,r2,r3,guard);
      rec(r1<0&&r2<0&&r3==33&&guard,"DNS query builder refuses over-long host names without overrunning its buffer",d);
      hk_udp_big(); rec(1,"udp_send refuses a payload larger than one Ethernet frame",0); }
    run_prog("smptest 3 3 200000",&st,out,sizeof out);
    rec(st==0&&strstr(out,"smptest: PASS")&&strstr(out,"0 errors"),"smptest: forks, pipes, files and copy-on-write from several processes",out);
    fs0=hk_used_files();
    run_prog("musltest",&st,out,sizeof out);
    printf("---- musltest ----\n%s",out);
    char* l=strtok(out,"\n"); int np=0;
    while(l){ if(!strncmp(l,"FAIL ",5))rec(0,l+5,0); else if(!strncmp(l,"ok   ",5))np++; l=strtok(0,"\n"); }
    rec(st==0&&np>=80,"musltest: every libc self-test passes under the NoovexOS kernel",0);
    rec(hk_used_files()==fs0,"musltest cleans up its files",0);
    if(argc>1){ FILE* f=fopen(argv[1],"rb"); static unsigned char fb[1<<20]; int n=f?(int)fread(fb,1,sizeof fb,f):0; if(f)fclose(f);
        int k=hk_addfile("/tmp","lxfuzz",fb,n);
        run_prog(argc>2?argv[2]:"/tmp/lxfuzz 30000",&st,out,sizeof out);
        rec(k>=0&&st==0&&strstr(out,"lxfuzz done"),"random Linux system calls with hostile arguments never crash the kernel",out);
        hk_delfile(k);
        run_prog("mhello",&st,out,sizeof out);
        rec(st==0&&strstr(out,"Hello from musl"),"kernel still healthy after the fuzz run",out); }
    { int nshm=hk_shm_clear(); char sd[64]; snprintf(sd,sizeof sd,"%u frames missing after removing %d SysV segments",fr0-hk_free_frames(),nshm); rec(hk_free_frames()==fr0,"no page frames leaked after all programs exit",hk_free_frames()==fr0?0:sd); }
    rec(hk_free_vfiles()==vf0-1||hk_free_vfiles()==vf0,"no open-file slots leaked",0);
    rec(hk_free_pipes()==pp0,"no pipes leaked",0);
    printf("  (%lu Linux system calls handled, %lu address-space switches, %lu page faults resolved)\n",g_syscalls,g_syncs,g_pfaults);
    printf("%s\n",g_fail?"FAILURES":"ALL PASS");
    return g_fail?1:0;
}
