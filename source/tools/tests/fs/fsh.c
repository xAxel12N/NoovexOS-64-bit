#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8;
extern u32 fk_disk_secs;
unsigned fk_metab(void); void fk_heap(void); int fk_boot(int); const char* fk_note(void); const char* fk_err(void); int fk_n4(void);
int fk_open(const char*,int); int fk_write(int,const void*,int); int fk_read(int,void*,int); int fk_close(int); int fk_lseek(int,int,int);
int fk_mkdir(const char*); int fk_rmdir(const char*); int fk_unlink(const char*); int fk_rename(const char*,const char*);
int fk_link(const char*,const char*); int fk_symlink(const char*,const char*); int fk_readlink(const char*,char*,int); int fk_truncate(const char*,long);
int fk_stat(const char*,u32*); int fk_chmod(const char*,unsigned); int fk_sync(void); int fk_fsync(int);
int fk_used_files(void); int fk_loaded_files(void); unsigned long long fk_free_bytes(void); unsigned long long fk_total_bytes(void);
int fk_readdir(const char*,int,char*,int*); void fk_setuid(int); void fk_layout(void); int fk_free_vfiles(void); long fk_evict(long); int fk_bitmap_check(void);

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_EXCL 0x80
#define O_TRUNC 0x200
#define O_APPEND 0x400

void h_set_pt(u64 v){ (void)v; } u64 h_get_pt(void){ return 0; } void h_flush_page(u64 v){ (void)v; }
void h_fpu_save(void* p){ (void)p; } void h_fpu_restore(const void* p){ (void)p; }
int nvxntfs_mount(void* a,void* b,unsigned long long c,unsigned long long d,int e){ (void)a;(void)b;(void)c;(void)d;(void)e; return -1; }
void nvxntfs_set_logger(void* f){ (void)f; }
void h_fatal(const char* m){ fprintf(stderr,"FATAL %s\n",m); _exit(90); }

static u8* g_disk; static u64 g_secs; static long g_budget=-1; static int g_cut; static unsigned long g_wsec;
int virtio_blk_probe(u64* n){ *n=g_secs; return 1; }
int virtio_blk_read(u64 lba,int c,u8* b){ if(c<0||lba+(u64)c>g_secs)return -1; memcpy(b,g_disk+lba*512,(size_t)c*512); return 0; }
int virtio_blk_write(u64 lba,int c,const u8* b){
    if(c<0||lba+(u64)c>g_secs)return -1;
    for(int i=0;i<c;i++){ if(g_cut)return -1; if(g_budget>=0){ if(g_budget==0){ g_cut=1; return -1; } g_budget--; } memcpy(g_disk+(lba+i)*512,b+(size_t)i*512,512); g_wsec++; }
    return 0; }
int virtio_blk_flush(void){ return g_cut?-1:0; }

static int g_fail, g_pass;
static void rec(int ok,const char* name,const char* det){ if(ok)g_pass++; else g_fail++; printf("  %-66s %s%s%s\n",name,ok?"ok":"FAIL",(!ok&&det)?"   ":"",(!ok&&det)?det:""); fflush(stdout); }
static unsigned rs=1; static unsigned rnd(void){ rs=rs*1103515245u+12345u; return rs>>8; }
static void fill(u8* b,long n,unsigned seed){ unsigned x=seed*2654435761u+1; for(long i=0;i<n;i++){ x^=x<<13; x^=x>>17; x^=x<<5; b[i]=(u8)x; } }

static void disk_new(u64 secs){ if(g_disk)munmap(g_disk,g_secs*512); g_secs=secs; g_disk=mmap(0,secs*512,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); fk_disk_secs=(u32)secs; g_budget=-1; g_cut=0; }
static int boot(int defaults){ g_budget=-1; g_cut=0; return fk_boot(defaults); }
static int putfile(const char* p,const u8* d,long n){ int fd=fk_open(p,O_WRONLY|O_CREAT|O_TRUNC); if(fd<0)return -1; long o=0; while(o<n){ int c=(int)(n-o>65536?65536:n-o); if(fk_write(fd,d+o,c)!=c){ fk_close(fd); return -2; } o+=c; } int s=fk_fsync(fd); fk_close(fd); return s; }
static long getfile(const char* p,u8* d,long cap){ int fd=fk_open(p,O_RDONLY); if(fd<0)return -1; long o=0; for(;;){ int c=fk_read(fd,d+o,(int)(cap-o>65536?65536:cap-o)); if(c<=0)break; o+=c; if(o>=cap)break; } fk_close(fd); return o; }
static int same(const char* p,const u8* d,long n){ static u8* b; if(!b)b=malloc(64<<20); long g=getfile(p,b,64<<20); return g==n&&!memcmp(b,d,(size_t)n); }

static void t_format(void){
    disk_new(256*2048);
    int ok=boot(1);
    rec(ok&&fk_n4()&&!strcmp(fk_note(),"Formatted as NVX4"),"blank disk is formatted as NVX4 and the boot files are saved",fk_note());
    int ok2=boot(0); u8 b[256]; long n=getfile("/home/readme.txt",b,sizeof b);
    rec(ok2&&fk_n4()&&n>10&&!memcmp(b,"Welcome to NoovexOS",19),"remount finds the default files",fk_note());
    rec(fk_bitmap_check()==0,"free-block bitmap matches the inode extents",0);
}
struct mf { char path[128]; u8* data; long len; int alive; };
static struct mf M[1200]; static int MN;
static void m_put(const char* p,const u8* d,long n){ int i; for(i=0;i<MN;i++) if(M[i].alive&&!strcmp(M[i].path,p))break; if(i==MN){ if(MN>=1200)return; MN++; strcpy(M[i].path,p); M[i].data=0; }
    M[i].data=realloc(M[i].data,(size_t)(n?n:1)); memcpy(M[i].data,d,(size_t)n); M[i].len=n; M[i].alive=1; }
static void m_del(const char* p){ for(int i=0;i<MN;i++) if(M[i].alive&&!strcmp(M[i].path,p)){ M[i].alive=0; free(M[i].data); M[i].data=0; } }
static int m_verify(char* det){ int bad=0, n=0; for(int i=0;i<MN;i++) if(M[i].alive){ n++; if(!same(M[i].path,M[i].data,M[i].len)){ if(!bad)snprintf(det,200,"%s (len %ld)",M[i].path,M[i].len); bad++; } } return bad; }
static void t_basic(void){
    disk_new(512*2048); boot(1); MN=0;
    static u8 buf[4<<20];
    rec(fk_mkdir("/home/a")==0&&fk_mkdir("/home/a/b")==0&&fk_mkdir("/home/a/b/a_directory_name_that_is_much_longer_than_31_chars")==0,"nested directories, names longer than 31 characters",0);
    long sizes[]={0,1,4095,4096,4097,100000,3<<20};
    int okw=1; for(int i=0;i<7;i++){ char p[128]; snprintf(p,sizeof p,"/home/a/b/f%d.bin",i); fill(buf,sizes[i],(unsigned)i+7); if(putfile(p,buf,sizes[i]))okw=0; m_put(p,buf,sizes[i]); }
    rec(okw,"files of 0, 1, 4095, 4096, 4097, 100000 and 3 MB bytes written",0);
    fill(buf,5000,99); putfile("/home/a/b/a_directory_name_that_is_much_longer_than_31_chars/deep.txt",buf,5000); m_put("/home/a/b/a_directory_name_that_is_much_longer_than_31_chars/deep.txt",buf,5000);
    int l=fk_link("/home/a/b/f2.bin","/home/a/hard"); int s=fk_symlink("/home/a/b/f1.bin","/home/a/sym"); fk_chmod("/home/a/b/f3.bin",0600);
    rec(l==0&&s==0,"hard link and symbolic link created",0);
    fill(buf,10000,5); putfile("/home/a/old.txt",buf,10000); fk_rename("/home/a/old.txt","/home/a/new.txt"); m_put("/home/a/new.txt",buf,10000);
    fk_truncate("/home/a/b/f5.bin",50000); { M[5].len=50000; }
    { int fd=fk_open("/home/a/b/f4.bin",O_WRONLY|O_APPEND); u8 x[3000]; fill(x,3000,77); fk_write(fd,x,3000); fk_fsync(fd); fk_close(fd); fill(buf,4097,4+7); memcpy(buf+4097,x,3000); m_put("/home/a/b/f4.bin",buf,7097); }
    fk_unlink("/home/a/b/f0.bin"); m_del("/home/a/b/f0.bin");
    fk_sync();
    char det[256]="";
    int bad=m_verify(det); rec(!bad,"contents correct before remount",det);
    boot(0); int lz=fk_loaded_files(); bad=m_verify(det);
    rec(fk_n4()&&!bad,"all contents identical after remount",det);
    u32 st[10]; fk_stat("/home/a/b/f3.bin",st); int m1=(st[0]&07777)==0600;
    fk_stat("/home/a/hard",st); int lk=st[3]==2&&st[4]==4095;
    char t[64]=""; int rl=fk_readlink("/home/a/sym",t,63); if(rl>0)t[rl]=0;
    rec(m1&&lk&&rl>0&&!strcmp(t,"/home/a/b/f1.bin")&&same("/home/a/sym",M[1].data,1),"mode, hard link count and symlink target survive remount",t);
    rec(fk_stat("/home/a/old.txt",st)!=0&&fk_stat("/home/a/b/f0.bin",st)!=0,"renamed and deleted names stay gone",0);
    rec(fk_bitmap_check()==0,"free-block bitmap consistent after remount",0);
    rec(lz==0,"files are loaded on demand, not at mount",0);
    for(int i=0;i<MN;i++) if(M[i].alive){ free(M[i].data); M[i].alive=0; }
}
static void t_many(void){
    disk_new(1024*2048); boot(1); MN=0;
    static u8 buf[1<<16]; int dirs=0, files=0, errs=0;
    for(int d=0;d<300;d++){ char p[128]; if(d<20)snprintf(p,sizeof p,"/home/d%d",d); else snprintf(p,sizeof p,"/home/d%d/s%d",d%20,d); if(fk_mkdir(p)==0)dirs++; }
    unsigned long long f0=fk_free_bytes();
    for(int i=0;i<900;i++){ char p[128]; int d=(int)(rnd()%300); if(d<20)snprintf(p,sizeof p,"/home/d%d/file%d",d,i); else snprintf(p,sizeof p,"/home/d%d/s%d/file%d",d%20,d,i);
        long n=(long)(rnd()%20000); fill(buf,n,(unsigned)i); int fd=fk_open(p,O_WRONLY|O_CREAT|O_TRUNC); if(fd<0){ errs++; continue; } fk_write(fd,buf,(int)n); fk_close(fd); m_put(p,buf,n); files++; }
    fk_sync();
    char det[256]=""; char d2[64]; snprintf(d2,sizeof d2,"dirs=%d files=%d errs=%d used=%d",dirs,files,errs,fk_used_files());
    rec(dirs==300&&files==900&&!errs,"300 directories and 900 files created",d2);
    boot(0); int bad=m_verify(det);
    rec(fk_n4()&&!bad,"900 files identical after remount",det);
    int del=0; for(int i=0;i<MN;i+=2) if(M[i].alive){ if(fk_unlink(M[i].path)==0)del++; m_del(M[i].path); }
    fk_sync(); boot(0); bad=m_verify(det);
    rec(del==450&&!bad,"half deleted, the rest intact after remount",det);
    for(int i=0;i<MN;i++) if(M[i].alive){ fk_unlink(M[i].path); m_del(M[i].path); }
    fk_sync(); boot(0);
    char d3[96]; snprintf(d3,sizeof d3,"free before %llu after %llu",f0,fk_free_bytes());
    rec(fk_free_bytes()==f0&&fk_bitmap_check()==0,"deleting every file returns every block",d3);
}
static void t_big(void){
    disk_new(512*2048); boot(1);
    long n=40L<<20; u8* b=malloc((size_t)n); fill(b,n,1234);
    int r=putfile("/home/big.bin",b,n);
    boot(0); int ld0=fk_loaded_files(); int ok=same("/home/big.bin",b,n);
    char det[96]; snprintf(det,sizeof det,"put=%d loaded-before-read=%d",r,ld0);
    rec(r==0&&ok&&ld0<=1,"40 MB file written and read back after remount",det);
    { int fd=fk_open("/home/big.bin",O_RDWR); fk_lseek(fd,20<<20,0); u8 x[8192]; fill(x,8192,55); fk_write(fd,x,8192); fk_fsync(fd); fk_close(fd); memcpy(b+(20<<20),x,8192); }
    boot(0); rec(same("/home/big.bin",b,n),"8 KB rewrite in the middle of a 40 MB file persists",0);
    long freed=fk_evict(1L<<30);
    rec(freed>=n&&same("/home/big.bin",b,n),"a clean cached file can be dropped from memory and reloaded",0);
    free(b);
}
static void t_full(void){
    disk_new(64*2048); boot(1);
    static u8 b[1<<20]; int ok=0, failed=0; char p[64];
    for(int i=0;i<200;i++){ snprintf(p,sizeof p,"/home/full%d",i); fill(b,sizeof b,(unsigned)i); if(putfile(p,b,sizeof b)==0)ok=i+1; else { failed=1; break; } }
    char det[96]; snprintf(det,sizeof det,"%d files of 1 MB fit",ok);
    rec(failed&&ok>40&&ok<64,"a full disk reports an error instead of corrupting data",det);
    boot(0); int bad=0; for(int i=0;i<ok;i++){ snprintf(p,sizeof p,"/home/full%d",i); fill(b,sizeof b,(unsigned)i); if(!same(p,b,sizeof b))bad++; }
    rec(fk_n4()&&!bad&&fk_bitmap_check()==0,"every file saved before the disk filled is intact after remount",0);
    for(int i=0;i<ok;i++){ snprintf(p,sizeof p,"/home/full%d",i); fk_unlink(p); }
    rec(putfile("/home/after",b,sizeof b)==0,"space is reusable after deleting",0);
}
static void t_unix(void){
    disk_new(128*2048); boot(1); fk_layout();
    u8 b[5000]; fill(b,5000,3); putfile("/home/open.txt",b,5000);
    int fd=fk_open("/home/open.txt",O_RDONLY); int un=fk_unlink("/home/open.txt"); u32 st[10];
    u8 r[5000]; int got=fk_read(fd,r,5000); int gone=fk_stat("/home/open.txt",st)!=0;
    int u0=fk_used_files(); fk_close(fd); int u1=fk_used_files();
    rec(un==0&&gone&&got==5000&&!memcmp(r,b,5000)&&u1==u0-1,"an unlinked file stays readable until the last close",0);
    fk_sync(); boot(0); rec(fk_stat("/home/open.txt",st)!=0,"the unlinked file is gone after remount",0);
    int e1=fk_open("/home/x",O_WRONLY|O_CREAT|O_EXCL); fk_close(e1); int e2=fk_open("/home/x",O_WRONLY|O_CREAT|O_EXCL);
    rec(e1>=0&&e2<0,"O_EXCL refuses an existing file",0);
    putfile("/home/r1",(const u8*)"one",3); putfile("/home/r2",(const u8*)"two",3);
    int rn=fk_rename("/home/r1","/home/r2"); rec(rn==0&&same("/home/r2",(const u8*)"one",3)&&fk_stat("/home/r1",st)!=0,"rename replaces an existing file",0);
    fk_mkdir("/home/da"); fk_mkdir("/home/db"); putfile("/home/da/in",(const u8*)"z",1);
    rec(fk_rmdir("/home/da")!=0,"rmdir refuses a non-empty directory",0);
    rec(fk_rename("/home/db","/home/dc")==0&&fk_stat("/home/dc",st)==0,"directory rename",0);
    fk_setuid(1234);
    int c1=fk_open("/etc/evil",O_WRONLY|O_CREAT); int c2=fk_open("/tmp/mine",O_WRONLY|O_CREAT); if(c2>=0)fk_close(c2);
    int c3=fk_unlink("/etc/passwd");
    fk_setuid(0); int t0=fk_open("/tmp/roots",O_WRONLY|O_CREAT); fk_close(t0); fk_setuid(1234); int c4=fk_unlink("/tmp/roots"); int c5=fk_unlink("/tmp/mine"); fk_setuid(0);
    char det[96]; snprintf(det,sizeof det,"etc=%d tmp=%d unlinkpw=%d sticky=%d own=%d",c1,c2,c3,c4,c5);
    rec(c1<0&&c2>=0&&c3!=0&&c4!=0&&c5==0,"directory permissions and the sticky bit on /tmp are enforced",det);
    { int a=fk_open("/home/app",O_WRONLY|O_CREAT|O_APPEND); fk_write(a,"12",2); fk_lseek(a,0,0); fk_write(a,"34",2); fk_close(a); rec(same("/home/app",(const u8*)"1234",4),"O_APPEND always writes at the end",0); }
    { fk_mkdir("/home/ts"); u32 a[10]; fk_stat("/home/ts",a); rec(a[0]==040755u&&a[5]>=10000,"directories have their own inode numbers and modes",0); }
    rec(fk_free_vfiles()>=30,"no open-file slots leaked",0);
}
static unsigned long g_ops;
static int crash_round(int round){
    static u8 snap[64*2048*512]; static u8 b[300000];
    memcpy(snap,g_disk,g_secs*512);
    char det[200];
    unsigned long w0=g_wsec;
    char p1[64],p2[64]; snprintf(p1,sizeof p1,"/home/c/new%d",round); snprintf(p2,sizeof p2,"/home/c/keep%d",round%5);
    long n1=(long)(rnd()%250000)+1; fill(b,n1,(unsigned)round+1000);
    putfile(p1,b,n1); fk_unlink(p2); fk_rename("/home/c/stable0","/home/c/stable0"); fk_sync();
    unsigned long total=g_wsec-w0;
    memcpy(g_disk,snap,g_secs*512);
    boot(0);
    long cut=(long)(rnd()%(total+1));
    g_budget=cut; g_cut=0;
    putfile(p1,b,n1); fk_unlink(p2); fk_sync();
    g_budget=-1; g_cut=0;
    int ok=boot(0);
    if(!ok){ snprintf(det,sizeof det,"round %d: mount failed after cut at %ld/%lu: %s",round,cut,total,fk_err()); rec(0,"crash consistency",det); return 0; }
    int bm=fk_bitmap_check();
    static u8 sb[200000]; fill(sb,200000,4242);
    int st=same("/home/c/stable0",sb,200000);
    static u8 rb[300000]; long g=getfile(p1,rb,sizeof rb); int nf=(g<0)||(g==n1&&!memcmp(rb,b,(size_t)n1));
    if(bm||!st||!nf){ snprintf(det,sizeof det,"round %d cut %ld/%lu: bitmap=%d stable=%d newfile=%ld/%ld",round,cut,total,bm,st,g,n1); rec(0,"crash consistency",det); return 0; }
    g_ops++;
    return 1;
}
static void t_crash(void){
    disk_new(64*2048); boot(1);
    fk_mkdir("/home/c"); static u8 sb[200000]; fill(sb,200000,4242); putfile("/home/c/stable0",sb,200000);
    for(int i=0;i<5;i++){ char p[64]; snprintf(p,sizeof p,"/home/c/keep%d",i); putfile(p,sb,1000+i); }
    fk_sync();
    int good=0; for(int r=0;r<150;r++){ if(crash_round(r))good++; else break; boot(0); char p[64]; snprintf(p,sizeof p,"/home/c/new%d",r); fk_unlink(p); for(int i=0;i<5;i++){ snprintf(p,sizeof p,"/home/c/keep%d",i); u32 st[10]; if(fk_stat(p,st))putfile(p,sb,1000+i); } fk_sync(); }
    char det[64]; snprintf(det,sizeof det,"%d of 150 power cuts",good);
    rec(good==150,"power cut at a random sector write: always mounts, old data intact, new file all-or-nothing",det);
}
static void t_metacopy(void){
    disk_new(64*2048); boot(1); u8 b[3000]; fill(b,3000,9); putfile("/home/m.txt",b,3000); fk_sync();
    u64 ma=(2048+4*8)*512ULL;
    g_disk[ma+100]^=0x55; int ok=boot(0); int s=same("/home/m.txt",b,3000);
    rec(ok&&s,"a damaged first metadata copy falls back to the second",fk_err());
    fk_sync(); boot(0); rec(same("/home/m.txt",b,3000),"the damaged copy is rewritten on the next commit",0);
    u64 mb=(2048+(4+(u64)fk_metab())*8)*512ULL; g_disk[ma+200]^=0x55; g_disk[mb+200]^=0x55;
    u8 before[512]; memcpy(before,g_disk+2048*512,512); unsigned long w=g_wsec;
    ok=boot(0);
    rec(!ok&&g_wsec==w&&!memcmp(before,g_disk+2048*512,512),"both copies damaged: mount refused and nothing is written",fk_err());
}
typedef struct { u32 used, dir, mode, uid, gid, nlink, len, mtime, ctime, atime, folder; char name[64]; } o3ino;
typedef struct { u32 used, dir, ino; char name[52]; } o3lnk;
typedef struct { char name[32]; int parent, used; unsigned mode; } odn;
typedef struct { u32 magic, version, ninode, nlinks, ndn; o3ino ino[24]; o3lnk lk[64]; odn d[64]; } o3;
typedef struct { char name[64]; u32 start,len,folder,dir,mode,uid,gid; } o2ent;
typedef struct { u32 magic,count; o2ent e[24]; u32 dcount; odn d[64]; } o2;
static u8* o3data[24]; static long o3len[24];
static void build_v3(void){
    disk_new(64*2048); memset(g_disk,0,g_secs*512);
    static o3 h; memset(&h,0,sizeof h); h.magic=0x3358564Eu; h.version=3; h.ninode=24; h.nlinks=64; h.ndn=64;
    const char* dn[]={"/","home","bin","etc","downloads","tmp","dev","projects"}; int par[]={-1,0,0,0,0,0,0,1};
    for(int i=0;i<8;i++){ strcpy(h.d[i].name,dn[i]); h.d[i].parent=par[i]; h.d[i].used=1; h.d[i].mode=040755; }
    for(int k=0;k<20;k++){ long n=k==3?2097151:(k==4?0:(long)(rnd()%300000)); o3len[k]=n; free(o3data[k]); o3data[k]=malloc((size_t)n+1); fill(o3data[k],n,(unsigned)k*31+5);
        o3ino* e=&h.ino[k]; e->used=1; e->dir=k%2?7:1; e->mode=0100644; e->nlink=1; e->len=(u32)n; e->mtime=1700000000u+(u32)k; snprintf(e->name,64,"old%d.dat",k);
        memcpy(g_disk+((u64)2048+8+(u64)(k+1)*4096)*512,o3data[k],(size_t)n); }
    h.lk[0].used=1; h.lk[0].dir=1; h.lk[0].ino=2; strcpy(h.lk[0].name,"alias");
    memcpy(g_disk+2048ULL*512,&h,sizeof h);
}
static int verify_v3(char* det){
    for(int k=0;k<20;k++){ char p[64]; snprintf(p,sizeof p,"%s/old%d.dat",k%2?"/home/projects":"/home",k); if(!same(p,o3data[k],o3len[k])){ snprintf(det,200,"%s",p); return 0; } }
    if(!same("/home/alias",o3data[2],o3len[2])){ strcpy(det,"hard link"); return 0; }
    u32 st[10]; if(fk_stat("/home/old5.dat",st)==0){ strcpy(det,"wrong dir"); return 0; }
    fk_stat("/home/projects/old1.dat",st); if(st[6]!=1700000001u){ strcpy(det,"mtime"); return 0; }
    return 1;
}
static void t_migrate3(void){
    build_v3(); char det[200]="";
    int ok=boot(0); int v=ok&&verify_v3(det);
    rec(ok&&fk_n4()&&!strcmp(fk_note(),"Converted to the NVX4 format")&&v,"NVX3 disk (v8.13-v8.90 format) converted: files, folders, link, times",det[0]?det:fk_note());
    ok=boot(0); v=ok&&verify_v3(det);
    rec(ok&&fk_n4()&&v&&fk_bitmap_check()==0,"converted disk mounts as NVX4 and the old file areas are free again",det);
    build_v3(); static u8 snap[64*2048*512]; memcpy(snap,g_disk,g_secs*512);
    unsigned long w0=g_wsec; boot(0); unsigned long total=g_wsec-w0; int good=0;
    for(int r=0;r<40;r++){ memcpy(g_disk,snap,g_secs*512); long cut=(long)(rnd()%(total+1)); g_budget=cut; g_cut=0; fk_boot(0); g_budget=-1; g_cut=0;
        int a=boot(0); det[0]=0; int b2=a&&verify_v3(det)&&fk_bitmap_check()==0; if(b2)good++; else { printf("    cut %ld/%lu: %s %s\n",cut,total,det,fk_note()); break; } }
    char d2[64]; snprintf(d2,sizeof d2,"%d of 40",good);
    rec(good==40,"power cut during conversion: the next boot converts again, nothing lost",d2);
}
static void t_migrate2(void){
    disk_new(64*2048); memset(g_disk,0,g_secs*512);
    static o2 h; memset(&h,0,sizeof h); h.magic=0x4E565849u; h.count=6; h.dcount=3;
    strcpy(h.d[0].name,"/"); h.d[0].parent=-1; h.d[0].used=1; strcpy(h.d[1].name,"home"); h.d[1].parent=0; h.d[1].used=1; strcpy(h.d[2].name,"etc"); h.d[2].parent=0; h.d[2].used=1;
    static u8 dd[6][70000];
    for(int i=0;i<6;i++){ long n=(long)(rnd()%70000); fill(dd[i],n,(unsigned)i+600); snprintf(h.e[i].name,64,"v2_%d.txt",i); h.e[i].start=2048+8+(u32)(i+1)*4096; h.e[i].len=(u32)n; h.e[i].dir=1; h.e[i].mode=0100644;
        memcpy(g_disk+(u64)h.e[i].start*512,dd[i],(size_t)n); o3len[i]=n; }
    memcpy(g_disk+2048ULL*512,&h,sizeof h);
    int ok=boot(0), bad=0; for(int i=0;i<6;i++){ char p[64]; snprintf(p,sizeof p,"/home/v2_%d.txt",i); if(!same(p,dd[i],o3len[i]))bad++; }
    boot(0); for(int i=0;i<6;i++){ char p[64]; snprintf(p,sizeof p,"/home/v2_%d.txt",i); if(!same(p,dd[i],o3len[i]))bad++; }
    rec(ok&&fk_n4()&&!bad,"NVX2 disk (pre-v8.13 format) converted and remounts",fk_note());
}
static void t_foreign(void){
    disk_new(64*2048); memset(g_disk,0,g_secs*512); g_disk[510]=0x55; g_disk[511]=0xAA; g_disk[446+4]=0x83; *(u32*)(g_disk+446+8)=2048; *(u32*)(g_disk+446+12)=100000;
    unsigned long w=g_wsec; int ok=boot(0);
    rec(!ok&&g_wsec==w,"a disk with another system's partitions is never written",fk_note());
    disk_new(64*2048); memset(g_disk,0,g_secs*512); g_disk[510]=0x55; g_disk[511]=0xAA; g_disk[446+4]=0x7F; *(u32*)(g_disk+446+8)=4096; *(u32*)(g_disk+446+12)=100000;
    ok=boot(1); int sb=*(u32*)(g_disk+4096ULL*512)==0x3458564Eu;
    rec(ok&&fk_n4()&&sb&&fk_total_bytes()<100000ULL*512,"a NoovexOS data partition is formatted inside its bounds",fk_note());
}
int main(void){
    setvbuf(stdout,0,_IOLBF,0);
    if(mmap((void*)0x10000000ULL,0x40000000ULL,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE|MAP_NORESERVE,-1,0)!=(void*)0x10000000ULL){ printf("heap mmap failed\n"); return 2; }
    fk_heap();
    t_format(); t_basic(); t_many(); t_big(); t_full(); t_unix(); t_metacopy(); t_crash(); t_migrate3(); t_migrate2(); t_foreign();
    printf("  (%lu sectors written)\n",g_wsec);
    printf("%s\n",g_fail?"FAILURES":"ALL PASS");
    return g_fail?1:0;
}
