#ifndef NVX_CORE_PROC_JOBS_H
#define NVX_CORE_PROC_JOBS_H

#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/fs/vfs/ramfs.h"

#define JMAX 256
#define S_FREE 0
#define S_RUN 1
#define S_WAIT 2
#define S_STOP 4
#define S_ZOMB 5
#define S_SLEEP 3
#define S_TDEAD 6
#define JS_STK_PAGES 256
#define PIPE_MAX 128
#define SB(s) ((s)==64?1ULL:(1ULL<<(s)))
#define SB_KS ((1ULL<<9)|(1ULL<<19))
struct jsinfo { int sig, code, pid, uid; u64 val; };
static inline u64 sig_k2l(u64 k){ return ((k>>1)&0x7FFFFFFFFFFFFFFFULL)|((k&1ULL)<<63); }
static inline u64 sig_l2k(u64 l){ return ((l<<1)&~1ULL)|((l>>63)&1ULL); }
struct jproc {
    int state, pid, ppid, pgid, sid, uid, gid, cwd, umask;
    u64 cr3, brk, mmnext, wake;
    regs_pt ctx;
    int fd[FDMAX]; u8 cloexec[FDMAX];
    u64 sigpend, sigmask, sigamask[65]; u32 saflags[65]; u64 sighand[65], sigrest[65];
    u64 fsbase, gsbase, pollwake, lxbrk, sockwake; u64 suspold; int susp, lx;
    int exitst, stopnote;
    volatile int runcpu; u64 cputicks; int lastcpu; u64 start_tick, argv_va; int argc_n;
    u64 it_next, it_int;
    int tg, tgid, gexit, tkill;
    u64 ctid, robust, fx_addr, fx_dead, wdl; int fx_wait, fx_woken, fx_tg, wdl_arm; u32 fx_bits;
    u64 ss_sp, ss_size, flt_addr; int ss_flags, flt_sig, flt_code, flt_trap; u32 flt_err; int sicode[65], sipid[65], siuid[65]; u64 sival[65]; struct { int sig, code, pid, uid; u64 val; } rtq[32]; int rtn;
    int nice, policy, rtprio; u64 vrt, runns;
    int sc_mode, sc_head, nnp;
    u32 aluid, asess; int nofc, nofm;
    char cmd[48];
    char exe[128];
    u8 fx[1024] __attribute__((aligned(64)));
};
struct jpipe { int used, r, w, n, cap, readers, writers; char* buf; };
#define NVXB(n) {n,_binary_user_nvxbox_elf_start,_binary_user_nvxbox_elf_end}
struct nvx_JS_BIN_t { const char* name; const unsigned char* s; const unsigned char* e; };

extern struct jproc g_jp[JMAX];
extern int g_next_pid , g_js_dirty;
extern int g_tty_fg;
extern struct jpipe g_pipe[PIPE_MAX];
extern const char* g_run_feed;
extern int g_tin_n , g_tin_eof;
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
extern const unsigned char _binary_user_sqlite3_elf_start[], _binary_user_sqlite3_elf_end[];
extern const unsigned char _binary_user_gzip_elf_start[], _binary_user_gzip_elf_end[];
extern const unsigned char _binary_user_bzip2_elf_start[], _binary_user_bzip2_elf_end[];
extern const unsigned char _binary_user_xz_elf_start[], _binary_user_xz_elf_end[];
extern const unsigned char _binary_user_zstd_elf_start[], _binary_user_zstd_elf_end[];
extern const struct nvx_JS_BIN_t JS_BIN[];
extern int g_kill_check , g_kill_denied;

extern int k_setjmp(u64*)__attribute__((returns_twice));
int* fdt(void);
u8* fdce(void);
int jc_idx(int pid);
int jc_uok(u64 p,u64 n);
int jc_umapped(u64 p,u64 n);
int jc_umapped_w(u64 p,u64 n);
void vf_ref(int vi);
void vf_unref(int vi);
int jc_newvf(int dev,int idx,int flags);
int jc_pipe_new(int cap,int* rvi,int* wvi);
int jc_pipe_read(struct vfile* v,char* buf,int len);
int jc_pipe_write(struct vfile* v,const char* buf,int len);
void jc_tty_out(const char* s,int n);
int jc_tty_read(char* buf,int len);
void vm_free_user(u64 cr3);
const u8* jc_find_prog(const char* name,u32* len);
const u8* jc_find_lib(const char* path,u32* len);
int jc_create(const char* args,int ppid,int pgid);
int sig_default(int s);
void jc_finish(int i,int status);
void jc_signal_proc(int i,int sig);
int jc_kill(int pid,int sig,int self);
u64 jc_deliverable(struct jproc* p);
int jc_sig_first(u64 d);
int jc_sig_take(struct jproc* w,int s,struct jsinfo* o);
void jc_sig_set_info(int code,int pid,int uid,u64 val);
extern void jc_leave(void)__attribute__((noreturn));
void jc_leave(void);
extern void jc_block(struct sysregs* r, int state, u64 wake)__attribute__((noreturn));
void jc_block(struct sysregs* r,int state,u64 wake);
extern void jc_yield_sys(struct sysregs* r)__attribute__((noreturn));
void jc_yield_sys(struct sysregs* r);
int jc_waitpid(struct sysregs* r,int pid,u64 stp,int opts);
int jc_fork(struct sysregs* r);
void jc_exec(struct sysregs* r,const char* args);
int jc_dup2(int o,int n);
int jc_tg_live(int lead,int except);
void jc_tg_sync_ids(int lead);
void jc_signal_thread(int i,int sig);
void jc_thread_exit(int i,int status);
int jc_clone_thread(struct sysregs* r,u64 stack,u64 tls,int settls);
void jc_futex_wake_tid(u64 addr,int tg);
int jc_futex_wake(int tg,u64 addr,int n,u32 bits);
int jc_futex_requeue(int tg,u64 a1,u64 a2,int nwake,int nmove);
static inline struct jproc* jc_lead(struct jproc* p){ return &g_jp[p->tg]; }
int jc_syscall(struct sysregs* r);
int vm_user_pf(regs_t* r);
void jc_fault(regs_t* rr);
void jc_preempt(regs_t* rr);
void jc_timer(regs_t* rr);
int jc_any_runnable(void);
int jc_weight(const struct jproc* p);
int jc_boost(const struct jproc* p);
extern u64 g_min_vrt;
int jc_bsp_off(void);
void jc_enter(int i);
int jc_pick(void);
int jc_step(void);
void tty_key(int c);
int jc_launch(const char* line);
int jc_builtin(const char* cmd,const char* arg);
int dt_busy(int d);
void dt_setmode(int n,unsigned m);

#endif
