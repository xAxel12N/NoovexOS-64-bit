#include <stdio.h>
#include <sys/mman.h>
typedef unsigned long long u64; typedef unsigned char u8;
typedef void (*ac_rep_fn)(const char*,int,const char*);
int act_run(ac_rep_fn rep);
#include <string.h>
extern unsigned int fk_disk_secs;
static u8* g_disk; static u64 g_secs;
int virtio_blk_probe(u64* n){ *n=g_secs; return 1; }
int virtio_blk_read(u64 lba,int c,u8* b){ if(c<0||lba+(u64)c>g_secs)return -1; memcpy(b,g_disk+lba*512,(size_t)c*512); return 0; }
int virtio_blk_write(u64 lba,int c,const u8* b){ if(c<0||lba+(u64)c>g_secs)return -1; memcpy(g_disk+lba*512,b,(size_t)c*512); return 0; }
int virtio_blk_flush(void){ return 0; }
static int g_fail, g_n;
static void rep(const char* what,int ok,const char* det){ g_n++; if(ok)printf("  %-100s ok\n",what); else { g_fail++; printf("  %-100s FAIL %s\n",what,det?det:""); } }
int main(void){
    setvbuf(stdout,0,_IOLBF,0);
    if(mmap((void*)0x40000000ULL,0x40000000ULL,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE|MAP_NORESERVE,-1,0)!=(void*)0x40000000ULL){ printf("heap mmap failed\n"); return 2; }
    g_secs=64*2048; g_disk=mmap(0,g_secs*512,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); fk_disk_secs=(unsigned)g_secs;
    act_run(rep);
    if(g_fail)printf("%d of %d FAILED\n",g_fail,g_n); else printf("ALL PASS (%d checks)\n",g_n);
    return g_fail?1:0;
}
