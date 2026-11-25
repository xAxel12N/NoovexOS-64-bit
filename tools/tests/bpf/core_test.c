#define BPF_HOST 1
#include <stdio.h>
#include <string.h>
#include "sys/bpf/bpf.c"
#include "sys/bpf/verifier.c"

static int fails, checks;
#define CHECK(c,m) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",m); } }while(0)
static char lg[512];
static int ver(u32 t,struct bpf_insn* p,u32 n,struct bpf_map** m,int nm){ return bpf_verify(t,p,n,m,nm,lg,sizeof lg); }
#define N(a) (u32)(sizeof(a)/sizeof(a[0]))
#define ACC(a,m,nm,msg) CHECK(ver(BPF_PROG_TYPE_TRACEPOINT,a,N(a),m,nm)==0,msg)
#define REJ_(a,m,nm,msg) CHECK(ver(BPF_PROG_TYPE_TRACEPOINT,a,N(a),m,nm)!=0,msg)

int main(void){
    int err=0;
    struct bpf_map* arr=bpf_map_new(BPF_MAP_TYPE_ARRAY,4,8,16,0,&err);
    struct bpf_map* hm=bpf_map_new(BPF_MAP_TYPE_HASH,8,8,4,0,&err);
    struct bpf_map* pc=bpf_map_new(BPF_MAP_TYPE_PERCPU_ARRAY,4,8,4,0,&err);
    struct bpf_map* rb=bpf_map_new(BPF_MAP_TYPE_RINGBUF,0,0,4096,0,&err);
    CHECK(arr&&hm&&pc&&rb,"map create");
    CHECK(!bpf_map_new(BPF_MAP_TYPE_RINGBUF,0,0,5000,0,&err)&&err==-22,"ringbuf size power of two");
    CHECK(!bpf_map_new(BPF_MAP_TYPE_ARRAY,8,8,4,0,&err),"array key size 4");
    u64 k=1,v=10,o=0;
    for(k=0;k<4;k++)CHECK(bpf_map_update(hm,&k,&v,BPF_ANY)==0,"hash insert");
    k=9; CHECK(bpf_map_update(hm,&k,&v,BPF_ANY)==-7,"hash full");
    k=2; CHECK(bpf_map_update(hm,&k,&v,BPF_NOEXIST)==-17,"hash noexist");
    CHECK(bpf_map_delete(hm,&k)==0&&!bpf_map_lookup(hm,&k),"hash delete");
    k=9; CHECK(bpf_map_update(hm,&k,&v,BPF_ANY)==0,"hash reuse tombstone");
    { int cnt=0; u64 cur,nx; int r=bpf_map_next_key(hm,0,&nx); while(r==0&&cnt<10){ cnt++; cur=nx; r=bpf_map_next_key(hm,&cur,&nx); } CHECK(cnt==4,"hash iterate"); }
    u32 ak=3; CHECK(bpf_map_update(arr,&ak,&v,BPF_ANY)==0&&*(u64*)bpf_map_lookup(arr,&ak)==10,"array update/lookup");
    ak=16; CHECK(!bpf_map_lookup(arr,&ak),"array oob");
    { char buf[64]; CHECK(bpf_ringbuf_output(rb,"hello",5)==0&&bpf_ringbuf_read(rb,buf,64)==5&&!memcmp(buf,"hello",5),"ringbuf roundtrip"); int n=0; while(bpf_ringbuf_output(rb,buf,48)==0)n++; CHECK(n==4096/56,"ringbuf full"); while(bpf_ringbuf_read(rb,buf,64)>0)n--; CHECK(n==0,"ringbuf drain"); }

    struct bpf_insn ok1[]={ BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; ACC(ok1,0,0,"trivial");
    struct bpf_insn r1[]={ BPF_EXIT_INSN() }; REJ_(r1,0,0,"uninit r0");
    struct bpf_insn r2[]={ BPF_MOV64_IMM(0,0), BPF_JMP_A(-2), BPF_EXIT_INSN() }; REJ_(r2,0,0,"back-edge");
    struct bpf_insn r3[]={ BPF_MOV64_IMM(0,0) }; REJ_(r3,0,0,"fall off end");
    struct bpf_insn r4[]={ BPF_LDX_MEM(BPF_DW,0,10,-8), BPF_EXIT_INSN() }; REJ_(r4,0,0,"uninit stack read");
    struct bpf_insn r5[]={ BPF_ST_MEM(BPF_DW,10,-520,0), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; REJ_(r5,0,0,"stack oob");
    struct bpf_insn r6[]={ BPF_MOV64_REG(0,10), BPF_EXIT_INSN() }; REJ_(r6,0,0,"return pointer");
    struct bpf_insn r7[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,1), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    struct bpf_map* ms[]={arr,hm,pc,rb}; REJ_(r7,ms,4,"null deref after lookup");
    struct bpf_insn ok2[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JEQ,0,0,3), BPF_MOV64_IMM(1,1), BPF_ATOMIC_ADD(BPF_DW,0,1,0), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    struct bpf_insn ok2b[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV64_IMM(1,1), BPF_ATOMIC_ADD(BPF_DW,0,1,0), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    ACC(ok2,ms,4,"null branch returns scalar 0");
    ACC(ok2b,ms,4,"lookup + null check + atomic add");
    CHECK(bpf_run_insns(ok2b,N(ok2b),ms,0,BPF_PROG_TYPE_TRACEPOINT)==11,"run lookup+add");
    struct bpf_insn r8[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_LDX_MEM(BPF_DW,0,0,8), BPF_EXIT_INSN() }; REJ_(r8,ms,4,"map value oob");
    struct bpf_insn r9[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_STX_MEM(BPF_DW,0,10,0), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; REJ_(r9,ms,4,"pointer leak into map");
    struct bpf_insn ok3[]={ BPF_LDX_MEM(BPF_DW,2,1,8), BPF_MOV64_REG(3,10), BPF_ALU64_IMM(BPF_ADD,3,-16), BPF_JMP_IMM(BPF_JGT,2,8,4), BPF_ALU64_REG(BPF_ADD,3,2), BPF_ST_MEM(BPF_B,3,0,1), BPF_MOV64_IMM(0,1), BPF_EXIT_INSN(), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() };
    REJ_(ok3,0,0,"variable stack offset rejected");
    struct bpf_insn ok4[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_LDX_MEM(BPF_DW,6,1,0), BPF_MOV64_REG(6,0), BPF_LDX_MEM(BPF_DW,2,6,0), BPF_ALU64_IMM(BPF_AND,2,7),
        BPF_ALU64_REG(BPF_ADD,6,2), BPF_LDX_MEM(BPF_B,0,6,0), BPF_EXIT_INSN() };
    REJ_(ok4,ms,4,"uses clobbered r1");
    struct bpf_insn ok5[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV64_REG(6,0), BPF_LDX_MEM(BPF_DW,2,6,0), BPF_ALU64_IMM(BPF_AND,2,7),
        BPF_ALU64_REG(BPF_ADD,6,2), BPF_LDX_MEM(BPF_B,0,6,0), BPF_EXIT_INSN() };
    ACC(ok5,ms,4,"bounded variable map offset");
    struct bpf_insn r10[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem),
        BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV64_REG(6,0), BPF_LDX_MEM(BPF_DW,2,6,0), BPF_ALU64_IMM(BPF_AND,2,15),
        BPF_ALU64_REG(BPF_ADD,6,2), BPF_LDX_MEM(BPF_B,0,6,0), BPF_EXIT_INSN() };
    REJ_(r10,ms,4,"variable map offset past end");
    struct bpf_insn ok6[]={ BPF_MOV64_REG(6,1), BPF_STX_MEM(BPF_DW,10,6,-8), BPF_LDX_MEM(BPF_DW,1,10,-8), BPF_LDX_MEM(BPF_DW,0,1,0), BPF_EXIT_INSN() }; ACC(ok6,0,0,"spill/fill ctx pointer");
    struct bpf_insn r11[]={ BPF_STX_MEM(BPF_DW,10,1,-8), BPF_LDX_MEM(BPF_W,0,10,-8), BPF_EXIT_INSN() }; REJ_(r11,0,0,"partial read of spilled pointer");
    struct bpf_insn r12[]={ BPF_ST_MEM(BPF_W,1,0,0), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; REJ_(r12,0,0,"ctx write");
    struct bpf_insn r13[]={ BPF_LDX_MEM(BPF_DW,0,1,sizeof(struct bpf_trace_ctx)), BPF_EXIT_INSN() }; REJ_(r13,0,0,"ctx oob");
    struct bpf_insn r14[]={ BPF_MOV64_IMM(0,0), BPF_JMP_IMM(BPF_JEQ,0,0,1), BPF_MOV64_IMM(0,1), BPF_EXIT_INSN() }; REJ_(r14,0,0,"dead code rejected");
    struct bpf_insn r15[]={ BPF_MOV64_IMM(0,0), BPF_CALL_HELPER(999), BPF_EXIT_INSN() }; REJ_(r15,0,0,"unknown helper");
    struct bpf_insn r16[]={ BPF_LD_MAP_IDX(1,3), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; REJ_(r16,ms,4,"lookup on ringbuf");
    struct bpf_insn ok7[]={ BPF_ST_MEM(BPF_DW,10,-8,42), BPF_LD_MAP_IDX(1,3), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_MOV64_IMM(3,8), BPF_MOV64_IMM(4,0), BPF_CALL_HELPER(BPF_FUNC_ringbuf_output), BPF_EXIT_INSN() };
    ACC(ok7,ms,4,"ringbuf_output from stack");
    CHECK(bpf_run_insns(ok7,N(ok7),ms,0,BPF_PROG_TYPE_TRACEPOINT)==0,"run ringbuf_output");
    { u64 got=0; CHECK(bpf_ringbuf_read(rb,&got,8)==8&&got==42,"ringbuf got record"); }
    struct bpf_insn r17[]={ BPF_ST_MEM(BPF_W,10,-8,42), BPF_LD_MAP_IDX(1,3), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_MOV64_IMM(3,8), BPF_MOV64_IMM(4,0), BPF_CALL_HELPER(BPF_FUNC_ringbuf_output), BPF_EXIT_INSN() };
    REJ_(r17,ms,4,"ringbuf_output reads uninit stack");
    struct bpf_insn r18[]={ BPF_MOV64_IMM(0,1), BPF_ALU64_IMM(BPF_DIV,0,0), BPF_EXIT_INSN() }; REJ_(r18,0,0,"div by const zero");
    struct bpf_insn ok8[]={ BPF_MOV64_IMM(0,7), BPF_MOV64_IMM(1,0), BPF_ALU64_REG(BPF_DIV,0,1), BPF_EXIT_INSN() }; ACC(ok8,0,0,"div by reg");
    CHECK(bpf_run_insns(ok8,N(ok8),0,0,BPF_PROG_TYPE_TRACEPOINT)==0,"div by zero yields 0");
    struct bpf_insn r19[]={ BPF_MOV64_IMM(0,0), BPF_ALU32_REG(BPF_ADD,10,0), BPF_EXIT_INSN() }; REJ_(r19,0,0,"write r10");
    struct bpf_insn r20[]={ BPF_LD_ABS(BPF_B,0), BPF_EXIT_INSN() }; REJ_(r20,0,0,"ld_abs in tracepoint");
    struct bpf_insn sf[]={ BPF_MOV64_REG(6,1), BPF_LD_ABS(BPF_H,12), BPF_JMP_IMM(BPF_JNE,0,0x0800,2), BPF_MOV64_IMM(0,0xffff), BPF_EXIT_INSN(), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() };
    CHECK(ver(BPF_PROG_TYPE_SOCKET_FILTER,sf,N(sf),0,0)==0,"socket filter ld_abs");
    { u8 pkt[20]={0}; pkt[12]=8; struct bpf_skb_ctx s={20,0,0,0,pkt}; CHECK(bpf_run_insns(sf,N(sf),0,&s,BPF_PROG_TYPE_SOCKET_FILTER)==0xffff,"socket filter run"); s.len=10; CHECK(bpf_run_insns(sf,N(sf),0,&s,BPF_PROG_TYPE_SOCKET_FILTER)==0,"ld_abs oob aborts"); }
    struct bpf_insn r21[]={ BPF_LDX_MEM(BPF_DW,0,1,16), BPF_EXIT_INSN() };
    CHECK(ver(BPF_PROG_TYPE_SOCKET_FILTER,r21,N(r21),0,0)!=0,"skb data pointer field hidden");
    struct bpf_insn r22[]={ BPF_MOV64_REG(0,1), BPF_JMP_IMM(BPF_JEQ,0,5,0), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; REJ_(r22,0,0,"pointer comparison");
    struct bpf_insn r23[]={ BPF_MOV64_IMM(0,0), BPF_LD_IMM64(1,1), BPF_EXIT_INSN() }; r23[2]=BPF_JMP_A(-2); { struct bpf_insn x[]={ BPF_JMP_A(1), BPF_LD_IMM64(0,1), BPF_EXIT_INSN() }; REJ_(x,0,0,"jump into ld_imm64"); }
    struct bpf_insn ok9[]={ BPF_LDX_MEM(BPF_DW,2,1,8), BPF_JMP_IMM(BPF_JGT,2,100,2), BPF_MOV64_REG(0,2), BPF_EXIT_INSN(), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN() }; ACC(ok9,0,0,"branch on ctx value");
    { struct bpf_trace_ctx tc={0}; tc.args[0]=55; CHECK(bpf_run_insns(ok9,N(ok9),0,&tc,BPF_PROG_TYPE_TRACEPOINT)==55,"run ctx read"); }
    { int e2=0; struct bpf_prog* p=bpf_prog_new(BPF_PROG_TYPE_TRACEPOINT,ok2b,N(ok2b),ms,4,lg,sizeof lg,&e2); CHECK(p!=0,"prog_new"); int s=bpf_slot_new(0,p); bpf_prog_put(p); CHECK(bpf_slot_prog(s)==p&&bpf_prog_run(p,0)==12,"slot + run"); bpf_fd_close(s); CHECK(!bpf_slot_prog(s),"slot freed"); }
    { int e2=0; CHECK(!bpf_prog_new(BPF_PROG_TYPE_TRACEPOINT,r2,N(r2),0,0,lg,sizeof lg,&e2)&&e2==-13&&strstr(lg,"back-edge"),"reject sets log + EACCES"); }
    struct bpf_insn r24[]={ BPF_STX_MEM(BPF_DW,10,1,-8), BPF_LD_MAP_IDX(1,3), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_MOV64_IMM(3,8), BPF_MOV64_IMM(4,0), BPF_CALL_HELPER(BPF_FUNC_ringbuf_output), BPF_EXIT_INSN() };
    REJ_(r24,ms,4,"spilled pointer leaked through helper");
    struct bpf_insn r25[]={ BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-8), BPF_ST_MEM(BPF_W,2,0,3), BPF_STX_MEM(BPF_DW,10,1,-16), BPF_MOV64_REG(3,10), BPF_ALU64_IMM(BPF_ADD,3,-16), BPF_LD_MAP_IDX(1,0), BPF_MOV64_IMM(4,0), BPF_CALL_HELPER(BPF_FUNC_map_update_elem), BPF_EXIT_INSN() };
    REJ_(r25,ms,4,"spilled pointer leaked through map update");
    struct bpf_insn r26[]={ BPF_ST_MEM(BPF_W,10,-4,0), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-4), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem), BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV32_IMM(3,-1), BPF_MOV64_IMM(4,40), BPF_ALU32_REG(BPF_RSH,3,4), BPF_ALU64_REG(BPF_ADD,0,3), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    REJ_(r26,ms,4,"alu32 rsh by register >= 32 then map oob");
    struct bpf_insn r27[]={ BPF_ST_MEM(BPF_W,10,-4,0), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-4), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem), BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV64_IMM(3,-1), BPF_MOV64_IMM(4,72), BPF_ALU64_REG(BPF_RSH,3,4), BPF_ALU64_REG(BPF_ADD,0,3), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    REJ_(r27,ms,4,"alu64 rsh by register >= 64 then map oob");
    struct bpf_insn r28[]={ BPF_ST_MEM(BPF_W,10,-4,0), BPF_MOV64_REG(2,10), BPF_ALU64_IMM(BPF_ADD,2,-4), BPF_LD_MAP_IDX(1,0), BPF_CALL_HELPER(BPF_FUNC_map_lookup_elem), BPF_JMP_IMM(BPF_JNE,0,0,2), BPF_MOV64_IMM(0,0), BPF_EXIT_INSN(), BPF_MOV64_IMM(3,1), BPF_MOV64_IMM(4,33), BPF_ALU32_REG(BPF_LSH,3,4), BPF_ALU64_REG(BPF_ADD,0,3), BPF_LDX_MEM(BPF_DW,0,0,0), BPF_EXIT_INSN() };
    REJ_(r28,ms,4,"alu32 lsh by register >= 32 then map oob");
    { struct bpf_insn x[]={ BPF_MOV32_IMM(0,-1), BPF_MOV64_IMM(4,40), BPF_ALU32_REG(BPF_RSH,0,4), BPF_EXIT_INSN() }; CHECK(ver(BPF_PROG_TYPE_TRACEPOINT,x,N(x),0,0)==0&&bpf_run_insns(x,N(x),0,0,BPF_PROG_TYPE_TRACEPOINT)==0xffffffu,"alu32 rsh by 40 masks like the interpreter"); }
    (void)o; (void)r23;
    printf("bpf core: %d checks, %d failed\n",checks,fails);
    { int e2=0; struct bpf_prog* p=bpf_prog_new(BPF_PROG_TYPE_TRACEPOINT,ok2b,N(ok2b),ms,4,lg,sizeof lg,&e2); int s=bpf_slot_new(0,p); bpf_prog_put(p); struct bpf_map* gm; struct bpf_prog* gp;
      CHECK(bpf_slot_get(s,&gm,&gp)==0&&gp==p&&!gm,"slot_get returns prog"); bpf_fd_close(s); CHECK(p->used&&p->insns,"slot_get ref keeps prog alive after close"); bpf_prog_put(gp); CHECK(!p->used,"last ref frees prog");
      CHECK(bpf_slot_get(s,&gm,&gp)==-9&&!gm&&!gp,"slot_get on closed slot"); CHECK(bpf_slot_get(-1,&gm,&gp)==-9&&bpf_slot_get(100000,&gm,&gp)==-9,"slot_get bounds"); }
    if(!fails)printf("bpf core: all ok\n");
    return fails!=0;
}
