#include "include/nvx/base.h"
#include "include/nvx/fs/parts.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvxfs.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/fs/unixfs.h"
#include "include/nvx/fs/vfat.h"
#include "include/nvx/fs/ntfsk.h"


static int vf_fmt32(nvxblk_rd r,nvxblk_wr w,unsigned long long b,unsigned long long n,const char* l){ return vf_format(r,w,b,n,l,32); }
static int vf_fmt16(nvxblk_rd r,nvxblk_wr w,unsigned long long b,unsigned long long n,const char* l){ return vf_format(r,w,b,n,l,16); }
const rfs_ops g_rfs_ntfs ={"NTFS", nvxntfs_mount,nvxntfs_umount,nvxntfs_exists,nvxntfs_mkdir,nvxntfs_write,nvxntfs_read,nvxntfs_delete,nvxntfs_ls,nvxntfs_format};
const rfs_ops g_rfs_fat32={"FAT32",vf_mount,vf_umount,vf_exists,vf_mkdir,vf_write,vf_read,vf_delete,vf_ls,vf_fmt32};
const rfs_ops g_rfs_fat16={"FAT16",vf_mount,vf_umount,vf_exists,vf_mkdir,vf_write,vf_read,vf_delete,vf_ls,vf_fmt16};
const rfs_ops* g_rfs=0;
static u8 g_nk_blk[4096];
static int nk_rd(unsigned long long lba,int n,void* buf){
    u32 r=dk_ratio(); u8* o=(u8*)buf;
    if(r<=1){ while(n>0){ int k=n>8?8:n; if(lba+(unsigned long long)k>0xFFFFFFFFULL)return -1; if(gdisk_read((u32)lba,k,o))return -1; lba+=(unsigned long long)k; n-=k; o+=k*512; } return 0; }
    for(int i=0;i<n;i++){ unsigned long long L=lba+(unsigned long long)i; u32 blk=(u32)(L/r); int off=(int)(L%r)*512;
        if(gdisk_read(blk,1,g_nk_blk))return -1; for(int j=0;j<512;j++)o[i*512+j]=g_nk_blk[off+j]; }
    return 0;
}
static int nk_wr(unsigned long long lba,int n,const void* buf){
    u32 r=dk_ratio(); const u8* in=(const u8*)buf;
    for(int i=0;i<n;i++){ unsigned long long L=lba+(unsigned long long)i;
        if(r<=1){ if(L>0xFFFFFFFFULL)return -1; if(gdisk_write((u32)L,in+i*512))return -1; bc_invalidate((u32)L,1); }
        else { u32 blk=(u32)(L/r); int off=(int)(L%r)*512; if(gdisk_read(blk,1,g_nk_blk))return -1;
            for(int j=0;j<512;j++)g_nk_blk[off+j]=in[i*512+j]; if(gdisk_write(blk,g_nk_blk))return -1; bc_invalidate(blk,1); } }
    return 0;
}
static int nk_is_ntfs(unsigned long long lba){ u8 b[512]; if(nk_rd(lba,1,b))return 0; return b[3]=='N'&&b[4]=='T'&&b[5]=='F'&&b[6]=='S'&&b[7]==' '; }
static int nk_find(unsigned long long* base,unsigned long long* nsect){
    gdisk_ensure(); if(g_dktype==DK_NONE)return -1;
    u32 r=dk_ratio(); unsigned long long total=(unsigned long long)g_dksectors*r;
    if(nk_is_ntfs(0)){ *base=0; *nsect=total; return 0; }
    u8 m[512]; if(nk_rd(0,1,m))return -1; if(m[510]!=0x55||m[511]!=0xAA)return -1;
    int gpt=0;
    for(int e=0;e<4;e++){ u8* pe=m+446+e*16; u8 t=pe[4]; u32 st=*(u32*)(pe+8), sz=*(u32*)(pe+12);
        if(t==0xEE)gpt=1;
        if((t==0x07||t==0x17||t==0x27)&&sz){ unsigned long long b=(unsigned long long)st*r; if(nk_is_ntfs(b)){ *base=b; *nsect=(unsigned long long)sz*r; return 0; } } }
    if(!gpt)return -1;
    static const u8 msdata[16]={0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    u8 h[512]; if(nk_rd(1ULL*r,1,h))return -1; if(h[0]!='E'||h[1]!='F'||h[2]!='I'||h[3]!=' ')return -1;
    unsigned long long elba=*(unsigned long long*)(h+72); u32 ne=*(u32*)(h+80), esz=*(u32*)(h+84); if(ne>256)ne=256; if(esz<128||esz>512)return -1;
    u8 s[512]; unsigned long long last=~0ULL;
    for(u32 i=0;i<ne;i++){ unsigned long long byte=(unsigned long long)i*esz; unsigned long long l512=elba*r+byte/512; int off=(int)(byte%512);
        if(l512!=last){ if(nk_rd(l512,1,s))return -1; last=l512; }
        int match=1; for(int k=0;k<16;k++) if(s[off+k]!=msdata[k]){ match=0; break; }
        if(match){ unsigned long long f=*(unsigned long long*)(s+off+32), lz=*(unsigned long long*)(s+off+40);
            if(nk_is_ntfs(f*r)){ *base=f*r; *nsect=(lz-f+1)*r; return 0; } } }
    return -1;
}
static void nk_log(const char* s){ klog2("ntfs: ",s); }
int nk_mount(int ro){
    unsigned long long b=0,n=0; if(nk_find(&b,&n))return -2;
    nvxntfs_set_logger(nk_log); return nvxntfs_mount(nk_rd,nk_wr,b,n,ro);
}
void nk_lscb(const char* name,int isdir,unsigned long long size,void* ctx){
    (void)ctx; char b[40],*p=b;
    if(isdir){ tcol(rgb(120,200,255)); term_puts(name); term_putc('/'); tcol(WHT); }
    else { term_puts(name); tcol(GREY); p=scopy(p,"  "); p=udec(p,(u32)(size>0xFFFFFFFFULL?0xFFFFFFFFULL:size)); *p=0; term_puts(b); tcol(WHT); }
    term_putc('\n');
}

#define NK_KMAX (FS_N+16)
static char g_nk_kf[NK_KMAX][192]; static int g_nk_nkf=0;
static char g_nk_kd[DN_MAX][192]; static int g_nk_nkd=0;
static void nk_cat(char* d,const char* a,const char* b){ int i=0; while(a[i]&&i<180){d[i]=a[i];i++;} if(!(i==1&&d[0]=='/')&&i<190)d[i++]='/'; int j=0; while(b[j]&&i<191)d[i++]=b[j++]; d[i]=0; }
static void nk_fpath(int k,char* out){ char p[192]; dt_path(FS[k].dir,p); nk_cat(out,p,FS[k].name); }
static int nk_streq(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return *a==*b; }
static void nk_mkdirs(const char* path){ char b[192]; int n=0; for(int i=0;path[i]&&i<191;i++){ b[i]=path[i]; n=i+1; if(i>0&&(path[i+1]=='/'||path[i+1]==0)){ b[n]=0; if(!g_rfs->exists(b))g_rfs->mkdir(b); } } }
static void nk_parent(const char* path,char* out){ int s=0; for(int i=0;path[i];i++) if(path[i]=='/')s=i; if(s==0){ out[0]='/'; out[1]=0; return; } for(int i=0;i<s;i++)out[i]=path[i]; out[s]=0; }
static int nk_known(char (*L)[192],int n,const char* p){ for(int i=0;i<n;i++) if(nk_streq(L[i],p))return i; return -1; }
static char* nk_num(char* p,unsigned v,int base){ char t[16]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]="0123456789"[v%(unsigned)base]; v/=(unsigned)base; } while(n)*p++=t[--n]; *p=0; return p; }
static char g_nk_meta[131072];
static void nk_meta_write(void){
    char* o=g_nk_meta; char* end=g_nk_meta+sizeof(g_nk_meta)-240;
    for(int i=1;i<DN_MAX&&o<end;i++) if(DT[i].used){ char p[192]; dt_path(i,p); *o++='D'; *o++=' '; o=nk_num(o,DT[i].mode?DT[i].mode:040755u,8); *o++=' '; o=nk_num(o,(unsigned)DT[i].uid,10); *o++=' '; o=nk_num(o,(unsigned)DT[i].gid,10); *o++=' '; for(int j=0;p[j];j++)*o++=p[j]; *o++='\n'; }
    for(int k=0;k<FS_N&&o<end;k++) if(FS[k].used&&!FS[k].orphan){ char p[192]; nk_fpath(k,p); *o++='F'; *o++=' '; o=nk_num(o,FS[k].mode?FS[k].mode:0100644u,8); *o++=' '; o=nk_num(o,(unsigned)FS[k].uid,10); *o++=' '; o=nk_num(o,(unsigned)FS[k].gid,10); *o++=' '; for(int j=0;p[j];j++)*o++=p[j]; *o++='\n'; }
    g_rfs->write("/.noovexmeta",g_nk_meta,(long)(o-g_nk_meta));
}
static unsigned nk_atou(const char** s,int base){ unsigned v=0; while(**s==' ')(*s)++; while(**s>='0'&&**s<='9'){ v=v*(unsigned)base+(unsigned)(**s-'0'); (*s)++; } return v; }
static void nk_meta_apply(void){
    long n=g_rfs->read("/.noovexmeta",g_nk_meta,(long)sizeof(g_nk_meta)-1); if(n<=0)return; g_nk_meta[n]=0;
    const char* s=g_nk_meta;
    while(*s){ char t=*s++; unsigned md=nk_atou(&s,8), uid=nk_atou(&s,10), gid=nk_atou(&s,10); while(*s==' ')s++;
        char p[192]; int i=0; while(*s&&*s!='\n'&&i<191)p[i++]=*s++; p[i]=0; if(*s=='\n')s++;
        if(t=='D'){ int d=dt_resolve_dir(p); if(d>0){ DT[d].mode=md; DT[d].uid=(int)uid; DT[d].gid=(int)gid; } }
        else if(t=='F'){ for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan){ char q[192]; nk_fpath(k,q); if(nk_streq(q,p)){ FS[k].mode=md; FS[k].uid=(int)uid; FS[k].gid=(int)gid; break; } } } }
}
int nk_root_sync_file(int k){
    if(k<0||k>=FS_N||!FS[k].used||FS[k].orphan||!g_rootfs)return -1;
    if(dt_is_tmp(FS[k].dir))return k;
    char p[192],par[192]; nk_fpath(k,p);
    if(FS[k].clean&&nk_known(g_nk_kf,g_nk_nkf,p)>=0){ nk_meta_write(); return k; }
    if(fs_ensure(k))return -1;
    nk_parent(p,par); nk_mkdirs(par);
    if(g_rfs->write(p,FS[k].data,FS[k].len)!=FS[k].len){ klog2("ntfs: save failed: ",p); return -1; }
    FS[k].clean=1; FS[k].dlo=FS[k].dhi=0;
    if(nk_known(g_nk_kf,g_nk_nkf,p)<0&&g_nk_nkf<NK_KMAX){ int i=0; for(;p[i];i++)g_nk_kf[g_nk_nkf][i]=p[i]; g_nk_kf[g_nk_nkf][i]=0; g_nk_nkf++; }
    nk_meta_write(); return k;
}
void nk_root_flush(void){
    if(!g_rootfs)return;
    for(int i=0;i<g_nk_nkf;i++){ int live=0; for(int k=0;k<FS_N&&!live;k++) if(FS[k].used&&!FS[k].orphan){ char q[192]; nk_fpath(k,q); if(nk_streq(q,g_nk_kf[i]))live=1; }
        if(!live){ g_rfs->del(g_nk_kf[i]); for(int j=i;j+1<g_nk_nkf;j++){ for(int c=0;c<192;c++)g_nk_kf[j][c]=g_nk_kf[j+1][c]; } g_nk_nkf--; i--; } }
    nk_meta_write();
}
void nk_root_dt_save(void){
    if(!g_rootfs)return;
    for(int i=1;i<DN_MAX;i++) if(DT[i].used){ char p[192]; dt_path(i,p); nk_mkdirs(p); if(nk_known(g_nk_kd,g_nk_nkd,p)<0&&g_nk_nkd<DN_MAX){ int c=0; for(;p[c];c++)g_nk_kd[g_nk_nkd][c]=p[c]; g_nk_kd[g_nk_nkd][c]=0; g_nk_nkd++; } }
    for(int pass=0;pass<8;pass++){ int best=-1,bl=0;
        for(int i=0;i<g_nk_nkd;i++){ if(dt_resolve_dir(g_nk_kd[i])>0)continue; int l=0; while(g_nk_kd[i][l])l++; if(l>bl){ bl=l; best=i; } }
        if(best<0)break; g_rfs->del(g_nk_kd[best]); for(int j=best;j+1<g_nk_nkd;j++){ for(int c=0;c<192;c++)g_nk_kd[j][c]=g_nk_kd[j+1][c]; } g_nk_nkd--; }
    nk_meta_write();
}
typedef struct { char name[64]; int isdir; unsigned long long size; } nk_ent;
static nk_ent g_nk_ents[256]; static int g_nk_nent;
static int nk_skipname(const char* n){ if(n[0]=='$')return 1; if(nk_streq(n,".noovexos")||nk_streq(n,".noovexmeta")||nk_streq(n,"System Volume Information"))return 1; return 0; }
static void nk_collect(const char* name,int isdir,unsigned long long size,void* ctx){ (void)ctx; if(g_nk_nent>=256||nk_skipname(name))return; int i=0; for(;name[i]&&i<63;i++)g_nk_ents[g_nk_nent].name[i]=name[i]; g_nk_ents[g_nk_nent].name[i]=0; g_nk_ents[g_nk_nent].isdir=isdir; g_nk_ents[g_nk_nent].size=size; g_nk_nent++; }
static int nk_load_tree(void){
    int q[DN_MAX]; int qh=0,qt=0,files=0; q[qt++]=0; dt_init();
    while(qh<qt){ int d=q[qh++]; char p[192]; dt_path(d,p); g_nk_nent=0; if(g_rfs->ls(p,nk_collect,0)<0)continue;
        int ne=g_nk_nent; static nk_ent ents[256]; for(int i=0;i<ne;i++)ents[i]=g_nk_ents[i];
        for(int i=0;i<ne;i++){ char cp[192]; nk_cat(cp,p,ents[i].name);
            if(ents[i].isdir){ int l=0; while(ents[i].name[l])l++; if(l>63)continue; int nd=dt_mkdir_at(d,ents[i].name); if(nd>0&&qt<DN_MAX)q[qt++]=nd;
                if(nk_known(g_nk_kd,g_nk_nkd,cp)<0&&g_nk_nkd<DN_MAX){ int c=0; for(;cp[c];c++)g_nk_kd[g_nk_nkd][c]=cp[c]; g_nk_kd[g_nk_nkd][c]=0; g_nk_nkd++; } }
            else { int slot=-1; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d&&nk_streq(FS[k].name,ents[i].name)){ slot=k; break; }
                if(slot<0) for(int k=0;k<FS_N;k++) if(!FS[k].used){ slot=k; break; }
                if(slot<0){ klog2("ntfs: no free file slot for ",cp); continue; }
                if(ents[i].size>(unsigned long long)(FS_CAP-1)){ klog2("ntfs: file too large, skipped: ",cp); continue; }
                fs_newslot(slot);
                int c=0; for(;ents[i].name[c]&&c<63;c++)FS[slot].name[c]=ents[i].name[c]; FS[slot].name[c]=0;
                FS[slot].len=(int)ents[i].size; FS[slot].lazy=FS[slot].len>0; FS[slot].clean=1; FS[slot].used=1; FS[slot].dir=d; FS[slot].folder=0; FS[slot].mode=0100644u; FS[slot].uid=0; FS[slot].gid=0; FS[slot].nlink=1; files++;
                if(nk_known(g_nk_kf,g_nk_nkf,cp)<0&&g_nk_nkf<NK_KMAX){ int x=0; for(;cp[x];x++)g_nk_kf[g_nk_nkf][x]=cp[x]; g_nk_kf[g_nk_nkf][x]=0; g_nk_nkf++; } } } }
    return files;
}
int nk_root_read(int k,char* buf,long len){ if(!g_rootfs||!g_rfs)return -1; char p[192]; nk_fpath(k,p); long got=g_rfs->read(p,buf,len); if(got<0)return -1; for(long i=got;i<len;i++)buf[i]=0; return 0; }
static int nk_probe(unsigned long long lba){ u8 b[512]; if(nk_rd(lba,1,b))return 0; if(b[510]!=0x55||b[511]!=0xAA)return 0;
    if(b[3]=='N'&&b[4]=='T'&&b[5]=='F'&&b[6]=='S'&&b[7]==' ')return 1;
    if(b[11]==0&&b[12]==2&&b[13]&&!(b[13]&(b[13]-1))&&b[16]&&(b[14]|b[15]))return 2;
    return 0; }
static int nk_scan(unsigned long long* bs,unsigned long long* ns,int* ty,int cap){
    gdisk_ensure(); if(g_dktype==DK_NONE)return 0; u32 r=dk_ratio(); unsigned long long total=(unsigned long long)g_dksectors*r; int n=0;
    int t0=nk_probe(0); if(t0){ bs[0]=0; ns[0]=total; ty[0]=t0; return 1; }
    static partent pl[64]; int pn=parts_scan_main(pl,64);
    for(int i=0;i<pn&&n<cap;i++){ if(pl[i].mbrtype==0x05||pl[i].mbrtype==0x0F||pl[i].mbrtype==0x85)continue; int t=nk_probe(pl[i].start); if(t){ bs[n]=pl[i].start; ns[n]=pl[i].count; ty[n]=t; n++; } }
    return n;
}
int nk_root_try(void){
    unsigned long long bs[8],ns[8]; int ty[8]; int n=nk_scan(bs,ns,ty,8);
    nvxntfs_set_logger(nk_log);
    for(int i=0;i<n;i++){ const rfs_ops* o=ty[i]==1?&g_rfs_ntfs:&g_rfs_fat32;
        if(o->mount(nk_rd,nk_wr,bs[i],ns[i],1))continue;
        char m[16]; long r=o->read("/.noovexos",m,15); o->umount();
        if(r<=0){ klog2("rootfs: volume is not a NoovexOS install, left untouched: ",o->name); continue; }
        if(o->mount(nk_rd,nk_wr,bs[i],ns[i],0)){ klog2("rootfs: NoovexOS install found but read-write mount failed: ",o->name); continue; }
        g_rfs=o; g_nk_nkf=0; g_nk_nkd=0; int files=nk_load_tree(); nk_meta_apply(); g_rootfs=1;
        g_nvx_note=ty[i]==1?"NoovexOS NTFS partition":(g_vfat.type==16?"NoovexOS FAT16 partition":"NoovexOS FAT32 partition");
        { char t[64],*p=t; const char* a="rootfs: loaded "; while(*a)*p++=*a++; a=g_nvx_note; while(*a)*p++=*a++; a=", files="; while(*a)*p++=*a++; p=nk_num(p,(unsigned)files,10); klog(t); }
        return 1; }
    return 0;
}
int nk_install(const rfs_ops* o,unsigned long long base,unsigned long long nsect,int copy,void (*prog)(int done,int total)){
    if(o->format(nk_rd,nk_wr,base,nsect,"NoovexOS"))return -1;
    nvxntfs_set_logger(nk_log);
    const rfs_ops* save=g_rfs; g_rfs=o;
    if(o->mount(nk_rd,nk_wr,base,nsect,0)){ g_rfs=save; return -2; }
    int rc=0;
    if(o->write("/.noovexos","NoovexOS\n",9)!=9)rc=-3;
    if(!rc){ dt_init(); for(int i=1;i<DN_MAX;i++) if(DT[i].used){ char p[192]; dt_path(i,p); nk_mkdirs(p); } }
    int tot=0,done=0; for(int k=0;k<FS_N;k++) if(FS[k].used)tot++;
    if(!rc&&copy) for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan){ char p[192],par[192]; nk_fpath(k,p); nk_parent(p,par); nk_mkdirs(par);
        if(fs_ensure(k)||o->write(p,FS[k].data,FS[k].len)!=FS[k].len){ rc=-4; break; } done++; if(prog)prog(done,tot); }
    if(!rc)nk_meta_write();
    o->umount(); g_rfs=save; return rc;
}
