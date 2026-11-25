#ifndef NVX_SYS_BPF_ATTACH_H
#define NVX_SYS_BPF_ATTACH_H

#include "include/nvx/sys/bpf.h"

#define BPF_TP_SYS_ENTER 0
#define BPF_TP_SYS_EXIT 1
#define BPF_TP_SCHED_SWITCH 2
#define BPF_TP_N 3
#define BPF_TP_SLOTS 8

void bpf_attach_init(void);
int bpf_tp_by_name(const char* name);
int bpf_tp_attach(int tp,struct bpf_prog* prog);
int bpf_tp_detach_id(int id);
extern volatile int g_bpf_tp_any, g_bpf_sock_any;
void bpf_tp_sys_enter_hit(u64 nr,const u64* a);
void bpf_tp_sys_exit_hit(u64 nr,long ret);
void bpf_tp_sched_switch_hit(int next);
int bpf_sock_attach_cbpf(int sid,const void* insns,u32 n);
int bpf_sock_attach_fd(int sid,int fd);
int bpf_sock_detach(int sid);
int bpf_sock_filter(int sid,const u8* pkt,int len);
int cbpf_check(const void* insns,u32 n);
u32 cbpf_run(const void* insns,u32 n,const u8* pkt,u32 len);
void bpf_cmd(const char* arg);

#endif
