#ifndef NVX_SYS_BPF_H
#define NVX_SYS_BPF_H

#ifdef BPF_HOST
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
#else
#include "include/nvx/base.h"
#endif

#define LX_BPFD 52
#define BPF_MAXINSNS 4096
#define BPF_STACK 512
#define BPF_MAXMAPS 64
#define BPF_MAXPROGS 64
#define BPF_PROG_MAPS 16
#define BPF_COMPLEXITY 200000
#define BPF_NCPU 8

struct bpf_insn { u8 code; u8 dst:4; u8 src:4; short off; int imm; };

#define BPF_CLASS(c) ((c)&0x07)
#define BPF_LD 0x00
#define BPF_LDX 0x01
#define BPF_ST 0x02
#define BPF_STX 0x03
#define BPF_ALU 0x04
#define BPF_JMP 0x05
#define BPF_JMP32 0x06
#define BPF_ALU64 0x07
#define BPF_SIZE(c) ((c)&0x18)
#define BPF_W 0x00
#define BPF_H 0x08
#define BPF_B 0x10
#define BPF_DW 0x18
#define BPF_MODE(c) ((c)&0xe0)
#define BPF_IMM 0x00
#define BPF_ABS 0x20
#define BPF_IND 0x40
#define BPF_MEM 0x60
#define BPF_ATOMIC 0xc0
#define BPF_OP(c) ((c)&0xf0)
#define BPF_SRC(c) ((c)&0x08)
#define BPF_K 0x00
#define BPF_X 0x08
#define BPF_ADD 0x00
#define BPF_SUB 0x10
#define BPF_MUL 0x20
#define BPF_DIV 0x30
#define BPF_OR 0x40
#define BPF_AND 0x50
#define BPF_LSH 0x60
#define BPF_RSH 0x70
#define BPF_NEG 0x80
#define BPF_MOD 0x90
#define BPF_XOR 0xa0
#define BPF_MOV 0xb0
#define BPF_ARSH 0xc0
#define BPF_END 0xd0
#define BPF_JA 0x00
#define BPF_JEQ 0x10
#define BPF_JGT 0x20
#define BPF_JGE 0x30
#define BPF_JSET 0x40
#define BPF_JNE 0x50
#define BPF_JSGT 0x60
#define BPF_JSGE 0x70
#define BPF_CALL 0x80
#define BPF_EXIT 0x90
#define BPF_JLT 0xa0
#define BPF_JLE 0xb0
#define BPF_JSLT 0xc0
#define BPF_JSLE 0xd0
#define BPF_TO_LE 0x00
#define BPF_TO_BE 0x08
#define BPF_FETCH 0x01
#define BPF_PSEUDO_MAP_FD 1
#define BPF_PSEUDO_MAP_IDX 5

#define BPF_R0 0
#define BPF_R1 1
#define BPF_R2 2
#define BPF_R3 3
#define BPF_R4 4
#define BPF_R5 5
#define BPF_R6 6
#define BPF_R7 7
#define BPF_R8 8
#define BPF_R9 9
#define BPF_R10 10

#define BPF_INSN(c,d,s,o,i) ((struct bpf_insn){(u8)(c),(u8)(d),(u8)(s),(short)(o),(int)(i)})
#define BPF_ALU64_REG(op,d,s) BPF_INSN(BPF_ALU64|BPF_OP(op)|BPF_X,d,s,0,0)
#define BPF_ALU64_IMM(op,d,i) BPF_INSN(BPF_ALU64|BPF_OP(op)|BPF_K,d,0,0,i)
#define BPF_ALU32_REG(op,d,s) BPF_INSN(BPF_ALU|BPF_OP(op)|BPF_X,d,s,0,0)
#define BPF_ALU32_IMM(op,d,i) BPF_INSN(BPF_ALU|BPF_OP(op)|BPF_K,d,0,0,i)
#define BPF_MOV64_REG(d,s) BPF_INSN(BPF_ALU64|BPF_MOV|BPF_X,d,s,0,0)
#define BPF_MOV64_IMM(d,i) BPF_INSN(BPF_ALU64|BPF_MOV|BPF_K,d,0,0,i)
#define BPF_MOV32_IMM(d,i) BPF_INSN(BPF_ALU|BPF_MOV|BPF_K,d,0,0,i)
#define BPF_LD_IMM64_RAW(d,s,v) BPF_INSN(BPF_LD|BPF_DW|BPF_IMM,d,s,0,(u32)(u64)(v)), BPF_INSN(0,0,0,0,(u32)((u64)(v)>>32))
#define BPF_LD_IMM64(d,v) BPF_LD_IMM64_RAW(d,0,v)
#define BPF_LD_MAP_FD(d,fd) BPF_LD_IMM64_RAW(d,BPF_PSEUDO_MAP_FD,fd)
#define BPF_LD_MAP_IDX(d,ix) BPF_LD_IMM64_RAW(d,BPF_PSEUDO_MAP_IDX,ix)
#define BPF_LDX_MEM(sz,d,s,o) BPF_INSN(BPF_LDX|BPF_SIZE(sz)|BPF_MEM,d,s,o,0)
#define BPF_STX_MEM(sz,d,s,o) BPF_INSN(BPF_STX|BPF_SIZE(sz)|BPF_MEM,d,s,o,0)
#define BPF_ST_MEM(sz,d,o,i) BPF_INSN(BPF_ST|BPF_SIZE(sz)|BPF_MEM,d,0,o,i)
#define BPF_ATOMIC_ADD(sz,d,s,o) BPF_INSN(BPF_STX|BPF_SIZE(sz)|BPF_ATOMIC,d,s,o,BPF_ADD)
#define BPF_LD_ABS(sz,i) BPF_INSN(BPF_LD|BPF_SIZE(sz)|BPF_ABS,0,0,0,i)
#define BPF_LD_IND(sz,s,i) BPF_INSN(BPF_LD|BPF_SIZE(sz)|BPF_IND,0,s,0,i)
#define BPF_JMP_REG(op,d,s,o) BPF_INSN(BPF_JMP|BPF_OP(op)|BPF_X,d,s,o,0)
#define BPF_JMP_IMM(op,d,i,o) BPF_INSN(BPF_JMP|BPF_OP(op)|BPF_K,d,0,o,i)
#define BPF_JMP32_IMM(op,d,i,o) BPF_INSN(BPF_JMP32|BPF_OP(op)|BPF_K,d,0,o,i)
#define BPF_JMP_A(o) BPF_INSN(BPF_JMP|BPF_JA,0,0,o,0)
#define BPF_CALL_HELPER(id) BPF_INSN(BPF_JMP|BPF_CALL,0,0,0,id)
#define BPF_EXIT_INSN() BPF_INSN(BPF_JMP|BPF_EXIT,0,0,0,0)

enum { BPF_MAP_CREATE=0, BPF_MAP_LOOKUP_ELEM=1, BPF_MAP_UPDATE_ELEM=2, BPF_MAP_DELETE_ELEM=3, BPF_MAP_GET_NEXT_KEY=4, BPF_PROG_LOAD=5, BPF_OBJ_GET_INFO_BY_FD=15, BPF_RAW_TRACEPOINT_OPEN=17 };
enum { BPF_MAP_TYPE_HASH=1, BPF_MAP_TYPE_ARRAY=2, BPF_MAP_TYPE_PERCPU_ARRAY=6, BPF_MAP_TYPE_RINGBUF=27 };
enum { BPF_PROG_TYPE_SOCKET_FILTER=1, BPF_PROG_TYPE_KPROBE=2, BPF_PROG_TYPE_TRACEPOINT=5, BPF_PROG_TYPE_RAW_TRACEPOINT=17, BPF_PROG_TYPE_MAX=32 };
enum { BPF_ANY=0, BPF_NOEXIST=1, BPF_EXIST=2 };

enum { BPF_FUNC_map_lookup_elem=1, BPF_FUNC_map_update_elem=2, BPF_FUNC_map_delete_elem=3, BPF_FUNC_probe_read=4, BPF_FUNC_ktime_get_ns=5,
    BPF_FUNC_trace_printk=6, BPF_FUNC_get_prandom_u32=7, BPF_FUNC_get_smp_processor_id=8, BPF_FUNC_get_current_pid_tgid=14,
    BPF_FUNC_get_current_uid_gid=15, BPF_FUNC_get_current_comm=16, BPF_FUNC_probe_read_kernel=113, BPF_FUNC_ringbuf_output=130, BPF_FUNC_MAX=256 };

enum { BPF_RET_INT=0, BPF_RET_VOID, BPF_RET_MAP_VALUE_OR_NULL };
enum { BPF_ARG_NONE=0, BPF_ARG_ANY, BPF_ARG_CONST_MAP, BPF_ARG_MAP_KEY, BPF_ARG_MAP_VALUE, BPF_ARG_MEM_RD, BPF_ARG_MEM_WR, BPF_ARG_SIZE, BPF_ARG_SIZE_OR_ZERO, BPF_ARG_CTX };

typedef u64 (*bpf_helper_fn)(u64,u64,u64,u64,u64);
struct bpf_helper { u32 id; u32 types; bpf_helper_fn fn; u8 ret; u8 arg[5]; };
#define BPF_TYPES_ALL 0xFFFFFFFFu
#define BPF_TYPE_BIT(t) (1u<<(t))

struct bpf_ctx_desc { u32 size; u32 rd_size; u32 pkt; };

union bpf_attr {
    struct { u32 map_type, key_size, value_size, max_entries, map_flags, inner_map_fd, numa_node; char map_name[16]; } mc;
    struct { u32 map_fd, pad; u64 key, value, flags; } me;
    struct { u32 prog_type, insn_cnt; u64 insns, license; u32 log_level, log_size; u64 log_buf; u32 kern_version, prog_flags; char prog_name[16]; } pl;
    struct { u32 bpf_fd, info_len; u64 info; } info;
    struct { u64 name; u32 prog_fd, pad; } rtp;
    u8 raw[128];
};

struct bpf_map_info_out { u32 type, id, key_size, value_size, max_entries, map_flags; char name[16]; };
struct bpf_prog_info_out { u32 type, id; u8 tag[8]; u32 jited_prog_len, xlated_prog_len; u64 jited_prog_insns, xlated_prog_insns; u64 load_time; u32 created_by_uid, nr_map_ids; u64 map_ids; char name[16]; };

struct bpf_map {
    int used, refs, id; u32 type, key_size, value_size, max_entries, flags; char name[16];
    u8* data; u32 elem_size; u32 count;
    u64 rb_head, rb_tail; u32 rb_size; u32 lock;
};
struct bpf_prog {
    int used, refs, id; u32 type; u32 len; struct bpf_insn* insns; struct bpf_map* maps[BPF_PROG_MAPS]; int nmaps; char name[16];
    u64 run_cnt, run_ns; u32 uid;
};

struct bpf_trace_ctx { u64 nr; u64 args[6]; u64 ret; u64 ts; };
struct bpf_sched_ctx { u32 prev_pid, next_pid, prev_state, cpu; char prev_comm[16], next_comm[16]; };
struct bpf_skb_ctx { u32 len, protocol, ifindex, pkt_type; const u8* data; };

int bpf_helper_register(const struct bpf_helper* h);
const struct bpf_helper* bpf_helper_get(u32 id,u32 prog_type);
void bpf_ctx_register(u32 prog_type,u32 size,u32 rd_size,u32 pkt);
const struct bpf_ctx_desc* bpf_ctx_get(u32 prog_type);

int bpf_verify(u32 prog_type,struct bpf_insn* insns,u32 n,struct bpf_map** maps,int nmaps,char* log,u32 logsz);
u64 bpf_prog_run(struct bpf_prog* p,void* ctx);
u64 bpf_run_insns(const struct bpf_insn* insns,u32 n,struct bpf_map** maps,void* ctx,u32 prog_type);

struct bpf_map* bpf_map_new(u32 type,u32 ks,u32 vs,u32 max,u32 flags,int* err);
void bpf_map_get(struct bpf_map* m);
void bpf_map_put(struct bpf_map* m);
void* bpf_map_lookup(struct bpf_map* m,const void* key);
int bpf_map_update(struct bpf_map* m,const void* key,const void* val,u64 flags);
int bpf_map_delete(struct bpf_map* m,const void* key);
int bpf_map_next_key(struct bpf_map* m,const void* key,void* next);
int bpf_ringbuf_output(struct bpf_map* m,const void* data,u32 len);
int bpf_ringbuf_read(struct bpf_map* m,void* buf,u32 cap);

struct bpf_prog* bpf_prog_new(u32 type,const struct bpf_insn* insns,u32 n,struct bpf_map** maps,int nmaps,char* log,u32 logsz,int* err);
void bpf_prog_get(struct bpf_prog* p);
void bpf_prog_put(struct bpf_prog* p);

int bpf_slot_new(struct bpf_map* m,struct bpf_prog* p);
struct bpf_map* bpf_slot_map(int slot);
struct bpf_prog* bpf_slot_prog(int slot);
int bpf_slot_get(int slot,struct bpf_map** m,struct bpf_prog** p);
void bpf_fd_close(int slot);
void bpf_link_hook(int (*put)(int));
int bpf_slot_set_link(int slot,int id);
int bpf_allowed(void);
extern int g_bpf_unpriv_disabled;

struct bpf_prog* bpf_prog_by_fd(int fd);
struct bpf_map* bpf_map_by_fd(int fd);

#endif
