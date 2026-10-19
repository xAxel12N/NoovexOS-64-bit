#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/fs/pkg/pkg.h"
u8 g_pkg_raw[8u*1024u*1024u];
static u8 g_pkg_inf[8u*1024u*1024u];
char g_pkg_name[64], g_pkg_ver[48], g_pkg_arch[24], g_pkg_desc[128];
int g_pkg_nfiles=0; long g_pkg_total=0;

static long pkg_octal(const char* s,int n){ long v=0; for(int i=0;i<n&&s[i];i++){ if(s[i]<'0'||s[i]>'7')break; v=(v<<3)+(s[i]-'0'); } return v; }
static int pkg_streq(const char* a,const char* b){ while(*a&&*b){ if(*a!=*b)return 0; a++;b++; } return *a==*b; }

/* ---- tar (ustar) walker: calls cb(name,mode,data,len,is_dir,ctx) ---- */
int tar_walk(const u8* t,long n,void(*cb)(const char*,unsigned,const u8*,long,int,void*),void* ctx){
    long off=0; int count=0;
    while(off+512<=n){
        const char* h=(const char*)(t+off);
        int empty=1; for(int i=0;i<512;i++) if(h[i]){ empty=0; break; } if(empty)break;
        char name[160]; int nn=0;
        const char* pre=h+345; if(pre[0]&&h[257]=='u'&&h[258]=='s'){ for(int i=0;i<155&&pre[i];i++)name[nn++]=pre[i]; if(nn)name[nn++]='/'; }
        for(int i=0;i<100&&h[i];i++)name[nn++]=h[i]; name[nn]=0;
        long sz=pkg_octal(h+124,12);
        unsigned mode=(unsigned)pkg_octal(h+100,8);
        char type=h[156];
        const u8* data=t+off+512;
        int isdir=(type=='5')||(nn>0&&name[nn-1]=='/');
        if(type==0||type=='0'||type=='5'||type=='7'){ if(cb)cb(name,mode,data,sz,isdir,ctx); count++; }
        off+=512+((sz+511)/512)*512;
    }
    return count;
}

/* ---- ar (.deb) walker: entries are 60-byte headers ---- */
static int ar_find(const u8* a,long n,const char* wantpfx,const u8** out,long* olen){
    if(n<8||a[0]!='!'||a[1]!='<'||a[2]!='a'||a[3]!='r'||a[4]!='c'||a[5]!='h'||a[6]=='\0')return 0;
    long off=8;
    while(off+60<=n){
        const char* h=(const char*)(a+off);
        char nm[20]; int k=0; for(int i=0;i<16&&h[i]&&h[i]!=' '&&h[i]!='/';i++)nm[k++]=h[i]; nm[k]=0;
        long sz=0; for(int i=48;i<58&&h[i]>='0'&&h[i]<='9';i++)sz=sz*10+(h[i]-'0');
        const u8* body=a+off+60;
        int m=1; for(int i=0;wantpfx[i];i++){ if(nm[i]!=wantpfx[i]){ m=0; break; } }
        if(m){ *out=body; *olen=sz; return 1; }
        off+=60+sz+(sz&1);
    }
    return 0;
}

/* ---- gzip detect + decompress into g_pkg_inf ---- */
static long pkg_maybe_gunzip(const u8* d,long n,const u8** out){
    if(n>2&&d[0]==0x1F&&d[1]==0x8B){ int r=gunzip(d,(int)n,g_pkg_inf,(int)sizeof(g_pkg_inf)); if(r<=0)return -1; *out=g_pkg_inf; return r; }
    *out=d; return n;   /* already plain tar (or unsupported xz/zstd) */
}

/* ---- control-file field parse (Debian control / APKINDEX / PKGINFO) ---- */
static void pkg_field(const char* ctrl,long n,const char* key,char* out,int cap){
    out[0]=0; int kl=0; while(key[kl])kl++;
    for(long i=0;i<n;i++){ int bol=(i==0)||ctrl[i-1]=='\n'; if(!bol)continue;
        int m=1; for(int k=0;k<kl;k++) if(ctrl[i+k]!=key[k]){ m=0; break; }
        if(m){ long p=i+kl; while(p<n&&(ctrl[p]==' '||ctrl[p]==':'||ctrl[p]=='='))p++;
            int o=0; while(p<n&&ctrl[p]!='\n'&&o<cap-1)out[o++]=ctrl[p++]; out[o]=0; return; } }
}

/* ---- listing callback ---- */
static void pkg_count_cb(const char* name,unsigned mode,const u8* data,long len,int isdir,void* ctx){
    (void)mode;(void)data;(void)ctx; if(isdir)return; g_pkg_nfiles++; g_pkg_total+=len;
}
void pkg_print_cb(const char* name,unsigned mode,const u8* data,long len,int isdir,void* ctx){
    (void)mode;(void)data; struct pkg_list_ctx* c=(struct pkg_list_ctx*)ctx; if(isdir)return; if(c->shown>=c->max)return;
    char b[96],*p=b; p=scopy(p,"    "); int nn=0; for(const char* q=name;*q&&nn<52;q++,nn++)*p++=*q; if(!isdir){ *p++=' '; *p++=' '; p=udec(p,(u32)len); } *p=0;
    tcol(rgb(150,158,176)); term_puts(b); term_putc('\n'); tcol(WHT); c->shown++;
}

/* ---- detect + parse metadata; returns 1 deb, 2 apk-tar, 3 zip-apk, 0 unknown ---- */
int pkg_open(const u8* file,long n,const u8** tar,long* tarlen){
    g_pkg_name[0]=g_pkg_ver[0]=g_pkg_arch[0]=g_pkg_desc[0]=0; g_pkg_nfiles=0; g_pkg_total=0;
    if(n>8&&file[0]=='!'&&file[1]=='<'&&file[2]=='a'&&file[3]=='r'){
        const u8* ctrl; long cl;
        if(ar_find(file,n,"control.tar",&ctrl,&cl)){ const u8* ct; long ctn=pkg_maybe_gunzip(ctrl,cl,&ct);
            if(ctn>0){ static u8 ctlbuf[65536]; long got=0;
                /* find ./control inside the control tar */
                long off=0; while(off+512<=ctn){ const char* h=(const char*)(ct+off); int empty=1; for(int i=0;i<512;i++)if(h[i]){empty=0;break;} if(empty)break;
                    char nm[120]; int k=0; for(int i=0;i<100&&h[i];i++)nm[k++]=h[i]; nm[k]=0; long sz=pkg_octal(h+124,12);
                    if(pkg_streq(nm,"./control")||pkg_streq(nm,"control")){ got=sz>65535?65535:sz; for(long i=0;i<got;i++)ctlbuf[i]=ct[off+512+i]; break; }
                    off+=512+((sz+511)/512)*512; }
                if(got){ pkg_field((char*)ctlbuf,got,"Package",g_pkg_name,64); pkg_field((char*)ctlbuf,got,"Version",g_pkg_ver,48);
                    pkg_field((char*)ctlbuf,got,"Architecture",g_pkg_arch,24); pkg_field((char*)ctlbuf,got,"Description",g_pkg_desc,128); } } }
        const u8* dt; long dl;
        if(ar_find(file,n,"data.tar",&dt,&dl)){ long tn=pkg_maybe_gunzip(dt,dl,tar); if(tn>0){ *tarlen=tn; return 1; } }
        return 1;
    }
    if(n>4&&file[0]=='P'&&file[1]=='K'&&file[2]==3&&file[3]==4){ scopy(g_pkg_name,"(Android/ZIP apk)"); return 3; }
    if(n>2&&file[0]==0x1F&&file[1]==0x8B){ long tn=pkg_maybe_gunzip(file,n,tar); if(tn>0){ *tarlen=tn;
        /* Alpine .apk: look for .PKGINFO in the tar */
        long off=0; while(off+512<=tn){ const char* h=(const char*)(*tar+off); int empty=1; for(int i=0;i<512;i++)if(h[i]){empty=0;break;} if(empty)break;
            char nm[120]; int k=0; for(int i=0;i<100&&h[i];i++)nm[k++]=h[i]; nm[k]=0; long sz=pkg_octal(h+124,12);
            if(pkg_streq(nm,".PKGINFO")||pkg_streq(nm,"//.PKGINFO")){ static u8 pib[8192]; long g=sz>8191?8191:sz; for(long i=0;i<g;i++)pib[i]=(*tar)[off+512+i];
                pkg_field((char*)pib,g,"pkgname",g_pkg_name,64); pkg_field((char*)pib,g,"pkgver",g_pkg_ver,48); pkg_field((char*)pib,g,"arch",g_pkg_arch,24); pkg_field((char*)pib,g,"pkgdesc",g_pkg_desc,128); break; }
            off+=512+((sz+511)/512)*512; }
        return 2; } }
    return 0;
}

/* ---- extraction into /pkg/<name>/ ---- */
static int g_pkg_extract_dir=1;
static void pkg_extract_cb(const char* name,unsigned mode,const u8* data,long len,int isdir,void* ctx){
    (void)ctx; const char* nm=name; if(nm[0]=='.'&&nm[1]=='/')nm+=2; if(!*nm)return;
    /* build directories along the path under g_pkg_extract_dir */
    char comp[64]; int ci=0; int dir=g_pkg_extract_dir;
    const char* p=nm;
    for(;;){ if(*p=='/'||*p==0){ comp[ci]=0;
            if(ci){ if(*p==0&&!isdir){ /* final = file */
                    int slot=-1; for(int i=0;i<FS_N;i++) if(!FS[i].used){ slot=i; break; }
                    if(slot>=0){ fs_newslot(slot); int k=0; while(comp[k]&&k<63){FS[slot].name[k]=comp[k];k++;} FS[slot].name[k]=0;
                        long L=len; if(L>FS_CAP-1)L=FS_CAP-1; if(fs_set(slot,(const char*)data,L))return;
                        FS[slot].used=1; FS[slot].dir=dir; FS[slot].folder=0; FS[slot].mode=0100000u|(mode?(mode&0777):0644u); FS[slot].uid=g_uid; FS[slot].gid=g_uid; FS[slot].nlink=1; if(disk_ok)nvx3_sync(slot); }
                } else { int nd=dt_mkdir_at(dir,comp); if(nd>=0){ dt_setmode(nd,0040755u); dir=nd; } } }
            if(*p==0)break; ci=0; p++; continue; }
        if(ci<63)comp[ci++]=*p; p++; }
}
int pkg_install(const u8* file,long n,int extract){
    const u8* tar=0; long tarlen=0;
    int kind=pkg_open(file,n,&tar,&tarlen);
    if(!kind)return -1;
    if(tar&&tarlen>0){ tar_walk(tar,tarlen,pkg_count_cb,0);
        if(extract){ dt_init(); int root=dt_resolve_dir("/pkg"); if(root<0)root=dt_mkdir_at(0,"pkg");
            const char* pn=g_pkg_name[0]?g_pkg_name:"unnamed"; int pd=dt_mkdir_at(root,pn); if(pd<0)pd=root;
            g_pkg_extract_dir=pd; tar_walk(tar,tarlen,pkg_extract_cb,0); dt_save(); } }
    return kind;
}
