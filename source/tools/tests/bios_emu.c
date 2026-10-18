#include "types.h"
#include "fat.h"
#include "config.h"
#include "string.h"
static int sys3(int n,int a,int b,int c){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c):"memory"); return r; }
static int sys5(int n,int a,int b,int c,int d,int e){ int r; __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e):"memory"); return r; }
static int g_fd;
static void out(const char* s){ int n=0; while(s[n])n++; sys3(4,1,(int)s,n); }
static void outdec(uint32_t v){ char b[12]; int n=11; b[n]=0; do{ b[--n]=(char)('0'+v%10); v/=10; }while(v); out(b+n); }
static void fail(const char* m){ out("FAIL "); out(m); out("\n"); sys3(1,1,0,0); for(;;){} }
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
int disk_read(uint32_t lba,uint32_t count,uint8_t* dest){
    unsigned long long off=(unsigned long long)lba*512ULL; unsigned int res[2];
    if(sys5(140,g_fd,(int)(off>>32),(int)(off&0xFFFFFFFFu),(int)res,0)<0)return -1;
    uint32_t want=count*512u, got=0; while(got<want){ int n=sys3(3,g_fd,(int)(dest+got),(int)(want-got)); if(n<=0)return -1; got+=(uint32_t)n; }
    return 0;
}
static uint8_t mbr[512]; static char cfgbuf[4096]; static uint8_t kbuf[0x6400000]; static struct cfg cfg;
int emu_main(int argc,char** argv){
    if(argc<3)fail("usage: bios_emu IMAGE KERNEL_OUT");
    g_fd=sys3(5,(int)argv[1],0,0); if(g_fd<0)fail("cannot open image");
    if(disk_read(0, 1, mbr) != 0) fail("disk read (MBR) failed");
@@PARTSEL@@
    out("PART "); outdec(part_lba); out("\n");
    if(fat_mount(part_lba) != 0) fail("FAT32 mount failed");
    out("MOUNT ok\n");
    uint32_t cfgsz=0; if(fat_read_file("/boot.cfg",(uint8_t*)cfgbuf,sizeof(cfgbuf)-1,&cfgsz)!=0) fail("cannot read /boot.cfg");
    cfgbuf[cfgsz]=0; cfg_parse(cfgbuf,&cfg);
    out("CFG entries="); outdec((uint32_t)cfg.count); out(" default="); outdec((uint32_t)cfg.def); out(" timeout="); outdec((uint32_t)cfg.timeout); out(" video="); outdec((uint32_t)cfg.video_w); out("x"); outdec((uint32_t)cfg.video_h); out("\n");
    for(int i=0;i<cfg.count;i++){ out("ENTRY "); out(cfg.entries[i].title); out(" | "); out(cfg.entries[i].kernel); out("\n"); }
    if(cfg.count<1||cfg.def<0||cfg.def>=cfg.count) fail("boot.cfg has no usable default entry");
    struct cfg_entry* e=&cfg.entries[cfg.def];
    uint32_t ksz=0; if(fat_read_file(e->kernel,kbuf,sizeof kbuf,&ksz)!=0) fail("cannot read kernel file");
    int elf=(kbuf[0]==0x7F&&kbuf[1]=='E'&&kbuf[2]=='L'&&kbuf[3]=='F');
    out("KERNEL "); out(e->kernel); out(" size="); outdec(ksz); out(elf?" format=elf\n":" format=flat\n");
    int fo=sys3(5,(int)argv[2],0x241,0644); if(fo<0)fail("cannot write kernel copy");
    uint32_t w=0; while(w<ksz){ int n=sys3(4,fo,(int)(kbuf+w),(int)(ksz-w)); if(n<=0)fail("write failed"); w+=(uint32_t)n; } sys3(6,fo,0,0);
    out("OK\n"); return 0;
}
__asm__(".globl _start\n_start:\n mov (%esp),%eax\n lea 4(%esp),%ecx\n and $-16,%esp\n sub $8,%esp\n push %ecx\n push %eax\n call emu_main\n mov %eax,%ebx\n mov $1,%eax\n int $0x80\n");
