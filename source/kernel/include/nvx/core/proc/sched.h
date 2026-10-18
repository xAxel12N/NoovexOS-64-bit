#ifndef NVX_CORE_PROC_SCHED_H
#define NVX_CORE_PROC_SCHED_H

#include "include/nvx/base.h"

typedef struct { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax; u64 vector,errcode,rip,cs,rflags,rsp,ss; } regs_pt;
#define MAXPROC 6
struct proc { int used, alive; u64 cr3, ksp; regs_pt ctx; int started; int exit_status; int zombie; int ppid; };
#define SHM_MAX 4
#define SHM_FRAMES 24
struct shmreg { int used, key, nframes; u64 frames[SHM_FRAMES]; };
#define MSG_SIZE 64
#define WM_CREATE 1
struct wmsg { int type,w,h,key,client,x,y,btn; };

extern volatile int g_preempt_on;
extern u64 g_preempt_kern[8];
extern struct proc g_proc[MAXPROC];
extern int g_curproc;
extern u8 g_pstack[MAXPROC][16384] __attribute__((aligned(16)));
extern u64 g_kern_ksp , g_kern_cr3;
extern const unsigned char _binary_user_user_preempt_elf_start[], _binary_user_user_preempt_elf_end[];
extern const unsigned char _binary_user_user_ipc_elf_start[], _binary_user_user_ipc_elf_end[];
extern struct shmreg g_shm[SHM_MAX];
extern const unsigned char _binary_user_user_wmapp_elf_start[], _binary_user_user_wmapp_elf_end[];
extern const unsigned char _binary_user_user_compdemo_elf_start[], _binary_user_user_compdemo_elf_end[];
extern const unsigned char _binary_user_user_posix_elf_start[], _binary_user_user_posix_elf_end[];
extern const unsigned char _binary_user_user_mem_elf_start[], _binary_user_user_mem_elf_end[];
extern u64 g_thread_stk_next;

int proc_create(const u8* elf,u32 len);
void sched_run_all(void);
void sched_yield_now(void);
void sched_exit_now(void);
void preempt_run(void);
int shm_get_k(int key,int size);
u64 shm_map_k(int id);
int msg_send_k(int q,const u8* m,int len);
int msg_recv_k(int q,u8* out,int cap);
void ipc_reset(void);
void preempt_run_elf(const u8* elf,u32 len);
void compositor_run(void);
int do_fork(struct sysregs* r);
int thread_create_k(u64 entry);

#endif
