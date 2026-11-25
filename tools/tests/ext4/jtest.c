#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "fs/ext4/journal.h"
typedef struct { int fd; uint32_t bs; uint64_t* map; uint32_t nmap; long wrs,crash; } hio;
static int h_rd(void* c,uint64_t b,void* buf){ hio* h=c; return pread(h->fd,buf,h->bs,(off_t)(b*h->bs))==(ssize_t)h->bs?0:-5; }
static int h_wr(void* c,uint64_t b,const void* buf){ hio* h=c; if(h->crash>=0&&h->wrs>=h->crash) return -5; h->wrs++; return pwrite(h->fd,buf,h->bs,(off_t)(b*h->bs))==(ssize_t)h->bs?0:-5; }
static int h_fl(void* c){ (void)c; return 0; }
static int h_map(void* c,uint32_t l,uint64_t* p){ hio* h=c; if(l>=h->nmap) return -5; *p=h->map[l]; return 0; }
static int h_rec(void* c,int on){
    hio* h=c; uint8_t sb[1024]; if(pread(h->fd,sb,1024,1024)!=1024) return -5;
    uint32_t inc=sb[0x60]|sb[0x61]<<8|sb[0x62]<<16|(uint32_t)sb[0x63]<<24; inc=on?(inc|4):(inc&~4u);
    sb[0x60]=inc; sb[0x61]=inc>>8; sb[0x62]=inc>>16; sb[0x63]=inc>>24;
    uint32_t ro=sb[0x64]|sb[0x65]<<8|sb[0x66]<<16|(uint32_t)sb[0x67]<<24;
    if(ro&0x400){ uint32_t c2=ext4_jcrc32c(0xFFFFFFFFu,sb,0x3FC); sb[0x3FC]=c2; sb[0x3FD]=c2>>8; sb[0x3FE]=c2>>16; sb[0x3FF]=c2>>24; }
    return pwrite(h->fd,sb,1024,1024)==1024?0:-5;
}
static int run(int argc,char** argv,hio* hp,ext4_journal* jp);
int main(int argc,char** argv){ hio h={0}; ext4_journal j; memset(&j,0,sizeof j); int r=run(argc,argv,&h,&j); ext4_jclose(&j); free(h.map); if(h.fd>0) close(h.fd); return r; }
static int run(int argc,char** argv,hio* hp,ext4_journal* jp){
    if(argc<4){ fprintf(stderr,"usage: jtest img blocklist info|replay|commit crash [blk:byte[:m]]...\n"); return 2; }
    setvbuf(stdout,0,_IONBF,0); hp->crash=-1; hp->fd=open(argv[1],O_RDWR); if(hp->fd<0){ perror("img"); return 2; }
    uint8_t sb[1024]; if(pread(hp->fd,sb,1024,1024)!=1024) return 2;
    hp->bs=1024u<<(sb[0x18]|sb[0x19]<<8); uint64_t nb=sb[4]|sb[5]<<8|sb[6]<<16|(uint64_t)sb[7]<<24|((uint64_t)(sb[0x150]|sb[0x151]<<8|sb[0x152]<<16|(uint32_t)sb[0x153]<<24)<<32);
    if(!((sb[0x60]|sb[0x61]<<8)&0x80)) nb&=0xFFFFFFFFu;
    FILE* f=fopen(argv[2],"r"); if(!f) return 2; hp->map=malloc(sizeof(uint64_t)*1u<<20); unsigned long long v; while(hp->nmap<(1u<<20)&&fscanf(f,"%llu",&v)==1) hp->map[hp->nmap++]=v; fclose(f);
    ext4_jio io={hp,hp->bs,nb,h_rd,h_wr,h_fl,h_map,h_rec};
    int r=ext4_jopen(jp,&io); if(r){ printf("open failed %d\n",r); return 1; }
    printf("journal v%u bs %u maxlen %u first %u seq %u start %u incompat %#x tagsz %u csum %d\n",jp->ver,jp->bsize,jp->maxlen,jp->first,jp->seq,jp->start,jp->fincompat,jp->tagsz,jp->csum);
    if(!strcmp(argv[3],"info")) return 0;
    if(!strcmp(argv[3],"replay")){
        if(!ext4_jneeds_recovery(jp)){ printf("clean\n"); return 0; }
        r=ext4_jreplay(jp); printf("replay %d replayed %u revoked %u skipped %u bad %d end %u\n",r,jp->replayed,jp->revoked,jp->skipped,jp->bad,jp->lasttid);
        if(r) return 1;
        return h_rec(hp,0)?1:0;
    }
    if(!strcmp(argv[3],"commit")&&argc>=5){
        hp->crash=atol(argv[4]); if((r=ext4_jtx_begin(jp))){ printf("begin %d\n",r); return 1; }
        uint8_t* buf=malloc(hp->bs);
        for(int i=5;i<argc;i++){
            unsigned long long b; unsigned x; char m=0; if(sscanf(argv[i],"%llu:%x:%c",&b,&x,&m)<2) return 2;
            memset(buf,(int)x,hp->bs); for(uint32_t k=0;k<hp->bs;k+=64) buf[k]=(uint8_t)(b+k/64);
            if(m=='m'){ buf[0]=0xC0; buf[1]=0x3B; buf[2]=0x39; buf[3]=0x98; }
            if((r=ext4_jtx_add(jp,b,buf))){ printf("add %d\n",r); return 1; }
        }
        free(buf); r=ext4_jtx_commit(jp); printf("commit %d ok %d seq %u writes %ld\n",r,jp->ok,jp->seq,hp->wrs);
        return r&&hp->crash<0?1:0;
    }
    return 2;
}
