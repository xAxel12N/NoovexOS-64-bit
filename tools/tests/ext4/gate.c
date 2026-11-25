#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "fs/ext4/ext4.h"
static int g_fd; static long g_wrs,g_crash=-1;
static int d_rd(unsigned long long lba,int n,void* b){ return pread(g_fd,b,(size_t)n*512,(off_t)lba*512)==(ssize_t)n*512?0:-1; }
static int d_wr(unsigned long long lba,int n,const void* b){ if(g_crash>=0&&g_wrs>=g_crash) return -1; g_wrs++; return pwrite(g_fd,b,(size_t)n*512,(off_t)lba*512)==(ssize_t)n*512?0:-1; }
int main(int argc,char** argv){
    setvbuf(stdout,0,_IONBF,0);
    if(argc<3) return 2;
    g_fd=open(argv[1],O_RDWR); if(g_fd<0) return 2;
    if(argc>3) g_crash=atol(argv[3]);
    ext4_fs* fs=calloc(1,sizeof *fs); off_t sz=lseek(g_fd,0,SEEK_END);
    int r=ext4_mount(fs,d_rd,d_wr,0,(u64)(sz/512),0); printf("mount %d ro %d jnl %d\n",r,fs->ro,fs->jnl!=0);
    if(r) return 1;
    if(!strcmp(argv[2],"mount")){ r=ext4_umount(fs); printf("umount %d\n",r); free(fs); return r?1:0; }
    if(fs->ro){ printf("read-only\n"); return 3; }
    int bad=0; char buf[20000]; for(int i=0;i<(int)sizeof buf;i++) buf[i]=(char)(i*7);
    r=ext4_mkdir_p(fs,"/gate/sub/deep",0755); if(r){ bad=1; printf("mkdir %d\n",r); }
    for(int i=0;i<40&&!bad;i++){ char p[64]; u32 ino; snprintf(p,sizeof p,"/gate/f%02d.txt",i); if(ext4_create(fs,p,0100644,&ino)||ext4_pwrite(fs,ino,0,buf,(long)(i*500+1))!=(long)(i*500+1)){ bad=1; printf("write %s failed\n",p); } }
    if(!bad) bad|=ext4_rename(fs,"/gate/f01.txt","/gate/sub/moved.txt")!=0;
    if(!bad) bad|=ext4_unlink(fs,"/gate/f02.txt")!=0;
    if(!bad){ u32 ino; bad|=ext4_lookup(fs,"/gate/f03.txt",&ino)||ext4_truncate(fs,ino,10); }
    if(!bad){ u32 ino; char rb[20000]; bad|=ext4_lookup(fs,"/gate/f39.txt",&ino)||ext4_pread(fs,ino,0,rb,39*500+1)!=39*500+1||memcmp(rb,buf,39*500+1); }
    printf("ops %s writes %ld\n",bad?"FAILED":"ok",g_wrs);
    r=ext4_umount(fs); printf("umount %d\n",r); free(fs);
    return bad||r?1:0;
}
