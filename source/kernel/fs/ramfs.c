/* =========================================================================
   RAM file system
   ========================================================================= */
#define FS_N 24
#define FS_CAP 2097152
/* Unix mode bits (from V6 ino.h) */
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFCHR  0020000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100
typedef struct{ char name[64]; char data[FS_CAP]; int len; int used; int folder; int dir; unsigned int mode; int uid, gid; u32 nlink, mtime, ctime, atime; } FsFile;
static FsFile FS[FS_N];
/* ================= POSIX RUNG 1: fd table + VFS ================= */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400
#define VMAXOPEN 32
#define FDMAX    32
typedef struct vfs_fs_ops vfs_fs_ops;
struct vfile { int used, fs_idx, off, flags, dev, ref; const vfs_fs_ops* vfs; char vpath[64]; };
static int* fdt(void); static u8* fdce(void); static void vf_unref(int vi); static void vf_ref(int vi);
static int jc_tty_read(char* buf,int len); static void jc_tty_out(const char* s,int n); static int jc_pipe_read(struct vfile* v,char* buf,int len); static int jc_pipe_write(struct vfile* v,const char* buf,int len);
static int perm_can_write(unsigned int mode); static int perm_can_exec(unsigned int mode);   /* dev: 0=file 1=null 2=zero 3=random 4=full 5=tty/console */
static struct vfile g_vf[VMAXOPEN];
static int g_fd[FDMAX]; static int g_fd_init=0;   /* fd -> vfile index; -1 free, -2 = std stream (0/1/2) */
extern char g_r3out[]; extern int g_r3out_n;
static void fd_init(void){ if(g_fd_init)return; g_fd_init=1; for(int i=0;i<FDMAX;i++)g_fd[i]=-1; for(int i=0;i<VMAXOPEN;i++)g_vf[i].used=0; g_fd[0]=-2; g_fd[1]=-2; g_fd[2]=-2; }
static int fs_find_name(const char* nm){ for(int k=0;k<FS_N;k++) if(FS[k].used){ int eq=1; for(int c=0;c<64;c++){ char a=FS[k].name[c],b=nm[c]; if(a!=b){eq=0;break;} if(!a&&!b)break; } if(eq)return k; } return -1; }
static int dev_id(const char* nm){
    if(nm[0]=='/'&&nm[1]=='d'&&nm[2]=='e'&&nm[3]=='v'&&nm[4]=='/'){ const char* d=nm+5;
        if(seq((char*)d,"null"))return 1; if(seq((char*)d,"zero"))return 2;
        if(seq((char*)d,"random")||seq((char*)d,"urandom"))return 3; if(seq((char*)d,"full"))return 4;
        if(seq((char*)d,"tty")||seq((char*)d,"console"))return 5;
        if(seq((char*)d,"fb0"))return 9; if(seq((char*)d,"input/mouse")||seq((char*)d,"input/mice"))return 10; }
    return 0;
}
