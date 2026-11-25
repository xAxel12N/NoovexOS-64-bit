#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "fs/ext4/ext4.h"
static int g_fd, g_fail;
static int d_rd(unsigned long long lba,int n,void* b){ return pread(g_fd,b,(size_t)n*512,(off_t)lba*512)==(ssize_t)n*512?0:-1; }
static int d_wr(unsigned long long lba,int n,const void* b){ return pwrite(g_fd,b,(size_t)n*512,(off_t)lba*512)==(ssize_t)n*512?0:-1; }
static void ok(const char* n,int c){ printf("%s %s\n",c?"ok  ":"FAIL",n); if(!c)g_fail=1; }
static ext4_fs* mnt(void){ ext4_fs* fs=calloc(1,sizeof *fs); off_t sz=lseek(g_fd,0,SEEK_END); if(ext4_mount(fs,d_rd,d_wr,0,(u64)(sz/512),0)){ free(fs); return 0; } return fs; }
static u32 mkfile(ext4_fs* fs,const char* p,long n){ static char b[65536]; u32 ino=0; memset(b,'x',sizeof b); if(ext4_create(fs,p,0100644,&ino))return 0; if(n&&ext4_pwrite(fs,ino,0,b,n)!=n)return 0; return ino; }
static void hdr(ext4_fs* fs,u32 ino,int off,u16 v){ e4_inode in; ext4_iget(fs,ino,&in); e4_st16((u8*)in.blk+off,v); ext4_iput(fs,ino,&in); }
static int bad_all(ext4_fs* fs,const char* p,u32 ino){ char b[8192]; int e=0; e|=ext4_pread(fs,ino,0,b,sizeof b)>=0; e|=ext4_pwrite(fs,ino,0,b,10)>=0; e|=ext4_truncate(fs,ino,0)==0; e|=ext4_unlink(fs,p)==0; return !e; }
static void ls_cb(const char* n,int d,unsigned long long s,void* c){ (void)n; (void)d; (void)s; (*(int*)c)++; }
int main(int argc,char** argv){
    setvbuf(stdout,0,_IONBF,0); if(argc<3)return 2; g_fd=open(argv[1],O_RDWR); if(g_fd<0)return 2;
    if(!strcmp(argv[2],"mountfail")){ ext4_fs* fs=mnt(); ok("crafted superblock geometry refused at mount",!fs); return g_fail; }
    if(!strcmp(argv[2],"ro")){ ext4_fs* fs=mnt(); ok("crafted superblock mounts read-only",fs&&fs->ro); if(fs){ ext4_umount(fs); free(fs); } return g_fail; }
    if(!strcmp(argv[2],"sblist")){ ext4_fs* fs=mnt(); if(!fs){ printf("FAIL mount\n"); return 1; } for(u32 g=0;g<fs->ngroups;g++){ u32 l=ext4_grp_sb_len(fs,g); if(l)printf("G %u %llu %u\n",g,(unsigned long long)(fs->first_data_block+(u64)g*fs->blocks_per_group),l); }
      for(u32 i=0;i<fs->gdt_blocks;i++)printf("D %u %llu\n",i,(unsigned long long)ext4_gdt_loc(fs,i)); printf("ro %d\n",fs->ro); ext4_umount(fs); free(fs); return 0; }
    if(!strcmp(argv[2],"bkfree")||!strcmp(argv[2],"bkalloc")){ ext4_fs* fs=mnt(); if(!fs||fs->ro){ printf("FAIL mount\n"); return 1; } int al=argv[2][2]=='a'; u32 tg=0;
      for(u32 g=1;g<fs->ngroups;g++) if(ext4_grp_has_sb(fs,g)){ tg=g; if(al)break; }
      if(!tg){ printf("ext4 fuzz: all ok (no backup group)\n"); return 0; } u64 bk=fs->first_data_block+(u64)tg*fs->blocks_per_group;
      if(!al){ u32 f=mkfile(fs,"/f",4096); e4_inode in; ext4_iget(fs,f,&in); u8* r=(u8*)in.blk; e4_st16(r+18,(u16)(bk>>32)); e4_st32(r+20,(u32)bk); ext4_iput(fs,f,&in);
        ok("unlink of a file mapped onto a backup superblock refused",ext4_unlink(fs,"/f")<0&&fs->ro); }
      else { for(u32 g=0;g<fs->ngroups;g++){ u8* d=fs->gdt+(u64)g*fs->desc_size; if(g!=tg){ e4_st16(d+0xC,0); if(fs->desc_size>=64)e4_st16(d+0x2C,0); } else e4_st16(d+0x12,(u16)(e4_le16(d+0x12)&~2u)); }
        u8* d=fs->gdt+(u64)tg*fs->desc_size; u64 bb=e4_le32(d)|(fs->desc_size>=64?(u64)e4_le32(d+0x20)<<32:0); u8* bm=calloc(1,fs->bsize); ext4_bread_raw(fs,bb,bm); bm[0]&=0xFE; ext4_bwrite_raw(fs,bb,bm); free(bm);
        u32 ino=0; char z[4096]={0}; int cr=ext4_create(fs,"/n",0100644,&ino); long w=cr?-1:ext4_pwrite(fs,ino,0,z,4096); u8 sb[1024]; d_rd((bk*fs->bsize)/512+(bk?0:2),2,sb);
        ok("allocator refuses a backup superblock marked free",w<0&&fs->ro); ok("backup superblock intact",e4_le16(sb+0x38)==E4_SB_MAGIC); }
      ext4_umount(fs); free(fs); printf(g_fail?"ext4 fuzz: FAILED\n":"ext4 fuzz: all ok\n"); return g_fail; }
    ext4_fs* fs=mnt(); if(!fs||fs->ro){ printf("FAIL mount\n"); return 1; }
    u32 a=mkfile(fs,"/a",100), b=mkfile(fs,"/b",100), c=mkfile(fs,"/c",100), d=mkfile(fs,"/d",100), e=mkfile(fs,"/e",4096*3), f=mkfile(fs,"/f",4096), g=mkfile(fs,"/g",0);
    ok("setup files",a&&b&&c&&d&&e&&f&&g);
    int C=argc>3?atoi(argv[3]):0;
    if(C==0){ hdr(fs,a,4,200); ok("root eh_max 200 rejected on read/write/truncate/unlink",bad_all(fs,"/a",a)); }
    if(C==1){ hdr(fs,b,2,5); ok("root eh_entries > eh_max rejected",bad_all(fs,"/b",b)); }
    if(C==2){ hdr(fs,c,6,7); ok("root depth 7 rejected",bad_all(fs,"/c",c)); }
    if(C==3){ hdr(fs,d,0,0x1234); ok("bad extent magic rejected",bad_all(fs,"/d",d)); }
    if(C==4||C==5){ e4_inode in; ext4_iget(fs,e,&in); u8* r=(u8*)in.blk; u64 pb=((u64)e4_le16(r+18)<<32)|e4_le32(r+20); u8* blk=calloc(1,fs->bsize);
      e4_st16(blk,E4_EXT_MAGIC); e4_st16(blk+2,1); e4_st16(blk+4,(u16)((fs->bsize-12)/12)); e4_st16(blk+6,1); e4_st32(blk+12,0); e4_st32(blk+16,(u32)pb); ext4_bwrite_raw(fs,pb,blk);
      e4_st16(r+2,1); e4_st16(r+6,1); e4_st32(r+12,0); e4_st32(r+16,(u32)pb); e4_st16(r+20,0); e4_st16(r+22,0); ext4_iput(fs,e,&in);
      if(C==4)ok("self-referencing extent index rejected, no recursion",bad_all(fs,"/e",e));
      e4_st16(blk+6,0); e4_st16(blk+4,60000); e4_st16(blk+2,59999); ext4_bwrite_raw(fs,pb,blk); if(C==5)ok("child eh_max beyond block rejected",bad_all(fs,"/e",e)); free(blk); }
    if(C==6){ e4_inode in; ext4_iget(fs,f,&in); u8* r=(u8*)in.blk; e4_st16(r+16,(u16)(e4_le16(r+16)|0x8000)); ext4_iput(fs,f,&in); char z[16]={1};
      ok("read of uninit extent gives zeros",ext4_pread(fs,f,0,z,16)==16&&!z[0]); ok("write into uninit extent refused",ext4_pwrite(fs,f,0,"hi",2)<0); }
    if(C==7){ e4_inode in; ext4_iget(fs,g,&in); in.mode=E4_S_IFLNK|0777; in.flags&=~(u32)E4_IF_EXTENTS; memset(in.blk,0,60); memcpy(in.blk,"abc",3); in.size=3; ext4_iput(fs,g,&in);
      ok("truncate of fast symlink refused",ext4_truncate(fs,g,0)<0); ok("write through fast symlink refused",ext4_pwrite(fs,g,0,"zzzz",4)<0);
      e4_inode t; ext4_iget(fs,g,&t); ok("symlink target intact",!memcmp(t.blk,"abc",3)); ok("unlink of fast symlink works",ext4_unlink(fs,"/g")==0); }
    if(C==8)ok("mkdir -p creates parents",ext4_mkdir_p(fs,"/p/q/r",0755)==0&&ext4_mkdir_p(fs,"/p/q/r",0755)==0&&ext4_mkdir_p(fs,"/s1/s2",0755)==0);
    if(C==8){ u32 di; ext4_lookup(fs,"/p/q",&di); e4_inode dn; ext4_iget(fs,di,&dn); u64 pb=0; ext4_bmap(fs,&dn,0,&pb); u8* blk=calloc(1,fs->bsize); ext4_bread_raw(fs,pb,blk);
      u32 rd; ext4_lookup(fs,"/p/q/r",&rd); e4_inode rn; ext4_iget(fs,rd,&rn); u64 rb=0; ext4_bmap(fs,&rn,0,&rb); u8* r2=calloc(1,fs->bsize); ext4_bread_raw(fs,rb,r2); e4_st16(r2+4,65532); ext4_bwrite_raw(fs,rb,r2);
      ok("rename with crafted '.' rec_len refused",ext4_rename(fs,"/p/q/r","/s1/r")<0);
      u32 st; if(fs->jnl)ok("failed rename rolled back",ext4_lookup(fs,"/p/q/r",&st)==0&&ext4_lookup(fs,"/s1/r",&st)!=0); int n=0;
      u8* de=blk+e4_le16(blk+4); de=de+e4_le16(de+4); de[6]=255; ext4_bwrite_raw(fs,pb,blk); u32 x; n=0; ext4_readdir(fs,"/p/q",ls_cb,&n); ok("dirent name_len beyond rec_len skipped",ext4_lookup(fs,"/p/q/zz",&x)<0); free(blk); free(r2); }
    ext4_umount(fs); free(fs);
    printf(g_fail?"ext4 fuzz: FAILED\n":"ext4 fuzz: all ok\n"); return g_fail;
}
