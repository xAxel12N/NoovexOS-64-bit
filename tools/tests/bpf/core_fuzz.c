#define BPF_HOST 1
#include <stdio.h>
#include <string.h>
#include "sys/bpf/bpf.c"
#include "sys/bpf/verifier.c"
static u64 rs=88172645463325252ull; static u64 rnd(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return rs; }
static const u8 codes[]={0x07,0x0f,0x17,0x1f,0x27,0x2f,0x37,0x3f,0x47,0x4f,0x57,0x5f,0x67,0x6f,0x77,0x7f,0x87,0x97,0x9f,0xa7,0xaf,0xb7,0xbf,0xc7,0xcf,0x04,0x0c,0xb4,0xbc,0x54,0x34,0x94,0xd4,0xdc,
 0x05,0x15,0x1d,0x25,0x2d,0x35,0x3d,0x45,0x55,0x5d,0xa5,0xad,0xb5,0xbd,0x65,0x6d,0x16,0x56,0x85,0x95,0x18,0x61,0x69,0x71,0x79,0x62,0x6a,0x72,0x7a,0x63,0x6b,0x73,0x7b,0xdb,0xc3,0x20,0x28,0x30,0x40,0x48,0x50};
int main(int argc,char**argv){
    long iters=argc>1?atol(argv[1]):200000; int err; struct bpf_map* ms[4]={bpf_map_new(BPF_MAP_TYPE_ARRAY,4,8,4,0,&err),bpf_map_new(BPF_MAP_TYPE_HASH,4,16,8,0,&err),bpf_map_new(BPF_MAP_TYPE_PERCPU_ARRAY,4,8,2,0,&err),bpf_map_new(BPF_MAP_TYPE_RINGBUF,0,0,4096,0,&err)};
    long acc=0; struct bpf_insn p[32]; static const int helpers[]={1,2,3,8,130};
    for(long it=0;it<iters;it++){
        u32 n=2+rnd()%30;
        for(u32 i=0;i<n;i++){ struct bpf_insn x; x.code=codes[rnd()%sizeof codes]; x.dst=rnd()%11; x.src=rnd()%11; x.off=(short)((int)(rnd()%40)-20); int r=rnd()%6; x.imm=r==0?(int)rnd():r<3?(int)(rnd()%16):-(int)(rnd()%16)-1;
            if(BPF_CLASS(x.code)==BPF_JMP&&BPF_OP(x.code)==BPF_CALL){ x.imm=helpers[rnd()%5]; x.src=x.dst=0; x.off=0; }
            if(BPF_CLASS(x.code)==BPF_JMP&&BPF_OP(x.code)==BPF_EXIT){ x.src=x.dst=0; x.off=0; x.imm=0; }
            if(x.code==0x18){ if(i+1<n){ x.src=rnd()%2?BPF_PSEUDO_MAP_IDX:0; if(x.src)x.imm=rnd()%4; x.off=0; p[i]=x; i++; p[i]=BPF_INSN(0,0,0,0,x.src?0:(int)rnd()); continue; } x.code=0xb7; }
            if(BPF_CLASS(x.code)!=BPF_JMP&&BPF_CLASS(x.code)!=BPF_JMP32&&BPF_CLASS(x.code)>=BPF_LDX&&rnd()%2)x.off=(short)(-(int)(rnd()%64));
            if(rnd()%3==0&&BPF_CLASS(x.code)!=BPF_JMP)x.src=10;
            if(x.code==0xdb||x.code==0xc3)x.imm=rnd()%2?BPF_ADD:BPF_ADD|BPF_FETCH;
            p[i]=x; }
        if(rnd()%2)p[n-1]=BPF_EXIT_INSN(); if(rnd()%2)p[0]=BPF_MOV64_IMM(0,0);
        u32 t=rnd()%3==0?BPF_PROG_TYPE_SOCKET_FILTER:BPF_PROG_TYPE_TRACEPOINT;
        struct bpf_insn c[32]; memcpy(c,p,sizeof p);
        if(bpf_verify(t,c,n,ms,4,0,0)==0){ acc++; u8 pkt[64]={0}; struct bpf_skb_ctx sk={64,0,0,0,pkt}; struct bpf_trace_ctx tc={0};
            bpf_run_insns(c,n,ms,t==BPF_PROG_TYPE_SOCKET_FILTER?(void*)&sk:(void*)&tc,t); u8 b[64]; while(bpf_ringbuf_read(ms[3],b,64)>0); }
    }
    printf("fuzz: %ld iters, %ld accepted, no faults\n",iters,acc); return 0;
}
