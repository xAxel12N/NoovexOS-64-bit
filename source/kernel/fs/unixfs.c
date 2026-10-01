#define NVX3_MAGIC 0x3358564Eu
#define NLINKS 64
#define S_IFLNK 0120000u
typedef struct { u32 used, dir, mode, uid, gid, nlink, len, mtime, ctime, atime, folder; char name[64]; } nvx3_ino;
typedef struct { u32 used, dir, ino; char name[52]; } nvx3_link;
typedef struct { u32 magic, version, ninode, nlinks, ndn; nvx3_ino ino[FS_N]; nvx3_link lk[NLINKS]; nvx_dnode d[NVX_DN_MAX]; } nvx3_t;
static nvx3_t g_n3;
struct ulink { int used, dir, ino; char name[52]; };
static struct ulink g_lk[NLINKS];
static int g_n3_conv=0;
static u32 ux_now(void){ return (u32)(k_realtime_ms()/1000ULL); }
static void ux_touch(int k,int data){ if(k<0||k>=FS_N)return; u32 t=ux_now(); FS[k].ctime=t; if(data)FS[k].mtime=t; }

static void nvx3_serialize(void){
    for(int i=0;i<DN_MAX&&i<NVX_DN_MAX;i++){ g_nvx.d[i].used=DT[i].used; g_nvx.d[i].parent=DT[i].parent; int k=0; while(DT[i].name[k]&&k<31){ g_nvx.d[i].name[k]=DT[i].name[k]; k++; } g_nvx.d[i].name[k]=0; g_nvx.d[i].mode=DT[i].mode?DT[i].mode:0040755u; }
    g_n3.magic=NVX3_MAGIC; g_n3.version=3; g_n3.ninode=FS_N; g_n3.nlinks=NLINKS; g_n3.ndn=NVX_DN_MAX;
    for(int k=0;k<FS_N;k++){ nvx3_ino* e=&g_n3.ino[k]; e->used=FS[k].used?1:0; if(!e->used)continue;
        e->dir=(u32)FS[k].dir; e->mode=FS[k].mode?FS[k].mode:0100644u; e->uid=(u32)FS[k].uid; e->gid=(u32)FS[k].gid; e->nlink=FS[k].nlink?FS[k].nlink:1;
        e->len=(u32)FS[k].len; e->mtime=FS[k].mtime; e->ctime=FS[k].ctime; e->atime=FS[k].atime; e->folder=(u32)FS[k].folder;
        int q=0; while(FS[k].name[q]&&q<63){ e->name[q]=FS[k].name[q]; q++; } e->name[q]=0; }
    for(int i=0;i<NLINKS;i++){ nvx3_link* l=&g_n3.lk[i]; l->used=g_lk[i].used?1:0; l->dir=(u32)g_lk[i].dir; l->ino=(u32)g_lk[i].ino; int q=0; while(g_lk[i].name[q]&&q<51){ l->name[q]=g_lk[i].name[q]; q++; } l->name[q]=0; }
    for(int i=0;i<NVX_DN_MAX;i++)g_n3.d[i]=g_nvx.d[i];
}
static void nvx3_flush_meta(void){
    if(!disk_ok)return; nvx3_serialize();
    u8* p=(u8*)&g_n3; int total=(int)sizeof(g_n3), secs=(total+511)/512;
    for(int se=0;se<secs;se++){ for(int i=0;i<512;i++){ int off=se*512+i; nvx_sec[i]=(off<total)?p[off]:0; } nvx_wr(NVX_DIR_LBA+se,nvx_sec); }
}
static u32 nvx3_slot(int k){ return NVX_DATA0+(u32)(k+1)*NVX_SECPF; }
static int nvx3_write_data(int k){
    if(!disk_ok||k<0||k>=FS_N||!FS[k].used)return -1;
    int len=FS[k].len; if(len>FS_CAP-1)len=FS_CAP-1; int wr=0,se=0;
    while(wr<len){ int n=len-wr; if(n>512)n=512; for(int i=0;i<512;i++)nvx_sec[i]=(i<n)?(u8)FS[k].data[wr+i]:0; if(nvx_wr(nvx3_slot(k)+se,nvx_sec))return -1; wr+=n; se++; }
    return 0;
}
static int nvx3_sync(int k){ if(nvx3_write_data(k))return -1; nvx3_flush_meta(); return k; }
static void nvx3_sync_all(void){ for(int k=0;k<FS_N;k++) if(FS[k].used)nvx3_write_data(k); nvx3_flush_meta(); }
static int nvx3_try_load(void){
    u8* p=(u8*)&g_n3; int total=(int)sizeof(g_n3), secs=(total+511)/512;
    if(nvx_rd(NVX_DIR_LBA,nvx_sec))return -1; if(*(u32*)nvx_sec!=NVX3_MAGIC)return 1;
    for(int se=0;se<secs;se++){ if(nvx_rd(NVX_DIR_LBA+se,nvx_sec))return -1; for(int i=0;i<512;i++){ int off=se*512+i; if(off<total)p[off]=nvx_sec[i]; } }
    for(int k=0;k<FS_N;k++)FS[k].used=0;
    for(int k=0;k<FS_N;k++){ nvx3_ino* e=&g_n3.ino[k]; if(!e->used)continue;
        int len=(int)e->len; if(len>FS_CAP-1)len=FS_CAP-1; int got=0,se=0;
        while(got<len){ if(nvx_rd(nvx3_slot(k)+se,nvx_sec))break; int n=len-got; if(n>512)n=512; for(int i=0;i<n;i++)FS[k].data[got+i]=(char)nvx_sec[i]; got+=n; se++; }
        FS[k].data[got]=0; FS[k].len=got; FS[k].used=1; int q=0; while(e->name[q]&&q<63){ FS[k].name[q]=e->name[q]; q++; } FS[k].name[q]=0;
        FS[k].dir=(int)e->dir; if(FS[k].dir<0||FS[k].dir>=NVX_DN_MAX)FS[k].dir=1; FS[k].mode=e->mode; FS[k].uid=(int)e->uid; FS[k].gid=(int)e->gid;
        FS[k].nlink=e->nlink?e->nlink:1; FS[k].mtime=e->mtime; FS[k].ctime=e->ctime; FS[k].atime=e->atime; FS[k].folder=(int)e->folder; }
    for(int i=0;i<NLINKS;i++){ nvx3_link* l=&g_n3.lk[i]; g_lk[i].used=(int)l->used&&(int)l->ino<FS_N&&FS[l->ino].used; g_lk[i].dir=(int)l->dir; g_lk[i].ino=(int)l->ino; int q=0; while(l->name[q]&&q<51){ g_lk[i].name[q]=l->name[q]; q++; } g_lk[i].name[q]=0; }
    int c=0; for(int i=0;i<NVX_DN_MAX;i++){ g_nvx.d[i]=g_n3.d[i]; if(g_nvx.d[i].used)c=i+1; } g_nvx.dcount=(u32)c;
    return 0;
}

static int ux_split(const char* p,int* dir,char* base){ return k_split(p,dir,base); }
static int ux_lookup_nofollow(const char* p,int* isdir){
    dt_init(); *isdir=0; if(!p||!p[0])return -1;
    int d=dt_resolve_dir(p); if(d>=0){ *isdir=1; return d; }
    int dd; char base[64]; if(ux_split(p,&dd,base)==0&&base[0]){
        for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dd&&seq(FS[k].name,base))return k;
        for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==dd&&seq(g_lk[i].name,base))return g_lk[i].ino; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,(char*)p))return k;
    return -1;
}
static int ux_lookup(const char* p,int* isdir,int follow){
    char cur[192]; int n=0; while(p[n]&&n<191){ cur[n]=p[n]; n++; } cur[n]=0;
    for(int hop=0;hop<8;hop++){
        int k=ux_lookup_nofollow(cur,isdir); if(k<0||*isdir)return k;
        if(!follow||(FS[k].mode&0170000u)!=S_IFLNK)return k;
        char t[192]; int tl=FS[k].len<191?FS[k].len:191; for(int i=0;i<tl;i++)t[i]=FS[k].data[i]; t[tl]=0;
        if(t[0]=='/'){ for(int i=0;i<=tl;i++)cur[i]=t[i]; }
        else { char dp[160]; dt_path(FS[k].dir,dp); int o=0; for(int i=0;dp[i]&&o<150;i++)cur[o++]=dp[i]; if(o>1)cur[o++]='/'; for(int i=0;t[i]&&o<190;i++)cur[o++]=t[i]; cur[o]=0; }
    }
    return -1;
}
static int ux_link_count(int k){ int n=FS[k].used?1:0; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k)n++; return n; }
static int ux_name_taken(int dir,const char* nm){
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dir&&seq(FS[k].name,(char*)nm))return 1;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==dir&&seq(g_lk[i].name,(char*)nm))return 1;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==dir&&seq(DT[i].name,(char*)nm))return 1;
    return 0;
}
static int ux_link(const char* from,const char* to){
    int isd; int k=ux_lookup(from,&isd,0); if(k<0||isd)return -1;
    int d; char b[64]; if(ux_split(to,&d,b)<0||!b[0]||ux_name_taken(d,b))return -1;
    for(int i=0;i<NLINKS;i++) if(!g_lk[i].used){ g_lk[i].used=1; g_lk[i].dir=d; g_lk[i].ino=k; int q=0; while(b[q]&&q<51){ g_lk[i].name[q]=b[q]; q++; } g_lk[i].name[q]=0;
        FS[k].nlink=ux_link_count(k); ux_touch(k,0); nvx3_flush_meta(); return 0; }
    return -1;
}
static int ux_symlink(const char* target,const char* path){
    int d; char b[64]; if(ux_split(path,&d,b)<0||!b[0]||ux_name_taken(d,b))return -1;
    for(int k=0;k<FS_N;k++) if(!FS[k].used){ int q=0; while(b[q]&&q<63){ FS[k].name[q]=b[q]; q++; } FS[k].name[q]=0;
        int L=0; while(target[L]&&L<FS_CAP-2){ FS[k].data[L]=target[L]; L++; } FS[k].data[L]=0; FS[k].len=L;
        FS[k].used=1; FS[k].dir=d; FS[k].folder=0; FS[k].mode=S_IFLNK|0777u; FS[k].uid=g_uid; FS[k].gid=g_uid; FS[k].nlink=1; ux_touch(k,1); FS[k].atime=FS[k].mtime;
        nvx3_sync(k); return 0; }
    return -1;
}
static int ux_readlink(const char* p,char* out,int cap){
    int isd; int k=ux_lookup(p,&isd,0); if(k<0||isd||(FS[k].mode&0170000u)!=S_IFLNK)return -1;
    int n=FS[k].len<cap?FS[k].len:cap; for(int i=0;i<n;i++)out[i]=FS[k].data[i]; return n;
}
static int ux_unlink(const char* p){
    int d; char b[64]; if(ux_split(p,&d,b)<0||!b[0])return k_unlink(p);
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d&&seq(g_lk[i].name,b)){ int k=g_lk[i].ino; g_lk[i].used=0; FS[k].nlink=ux_link_count(k); ux_touch(k,0); nvx3_flush_meta(); return 0; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d&&seq(FS[k].name,b)){
        if(!perm_can_write(FS[k].mode))return -1;
        for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k){ int q=0; while(g_lk[i].name[q]&&q<51){ FS[k].name[q]=g_lk[i].name[q]; q++; } FS[k].name[q]=0; FS[k].dir=g_lk[i].dir; g_lk[i].used=0; FS[k].nlink=ux_link_count(k); ux_touch(k,0); nvx3_flush_meta(); return 0; }
        FS[k].used=0; nvx3_flush_meta(); return 0; }
    return k_unlink(p);
}
static int ux_chown(const char* p,int uid,int gid,int follow){
    int isd; int k=ux_lookup(p,&isd,follow); if(k<0)return -1;
    int cu=isd?DT[k].uid:FS[k].uid; if(g_uid!=0&&(uid!=-1&&uid!=cu))return -1; if(g_uid!=0&&g_uid!=cu)return -1;
    if(isd){ if(uid!=-1)DT[k].uid=uid; if(gid!=-1)DT[k].gid=gid; dt_save(); return 0; }
    if(uid!=-1)FS[k].uid=uid; if(gid!=-1)FS[k].gid=gid; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
static int ux_chmod(const char* p,unsigned mode){
    int isd; int k=ux_lookup(p,&isd,1); if(k<0)return -1;
    int cu=isd?DT[k].uid:FS[k].uid; if(g_uid!=0&&g_uid!=cu)return -1;
    if(isd){ DT[k].mode=(DT[k].mode&0170000u)|(mode&07777u); if(!(DT[k].mode&0170000u))DT[k].mode|=0040000u; dt_save(); return 0; }
    FS[k].mode=(FS[k].mode&0170000u)|(mode&07777u); if(!(FS[k].mode&0170000u))FS[k].mode|=0100000u; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
static int ux_utime(const char* p,u32 at,u32 mt){ int isd; int k=ux_lookup(p,&isd,1); if(k<0)return -1; if(isd)return 0; u32 t=ux_now(); FS[k].atime=at?at:t; FS[k].mtime=mt?mt:t; FS[k].ctime=t; nvx3_flush_meta(); return 0; }

static int g_usb_mounted=0;
static int ux_usb_idx(const char* p){
    if(!g_usb_mounted||!msd_ok||!ufat_ok)return -1; const char* m="/mnt/usb/"; int i=0; while(m[i]&&p[i]==m[i])i++; if(m[i])return -1;
    const char* n=p+i; for(int f=0;f<ufs_n;f++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[f].name[k],y=n[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq)return f; }
    return -1;
}
static int usbfs_match(const char* p){ return ux_usb_idx(p)>=0; }
static char* g_usbbuf=0; static int g_usbbuf_fi=-1, g_usbbuf_n=0;
static int usbfs_load(const char* p){ int fi=ux_usb_idx(p); if(fi<0)return -1; if(fi==g_usbbuf_fi&&g_usbbuf)return g_usbbuf_n;
    if(!g_usbbuf)g_usbbuf=(char*)kmalloc(1048576); if(!g_usbbuf)return -1; g_usbbuf_n=ufat_read(fi,g_usbbuf,1048575); if(g_usbbuf_n<0)g_usbbuf_n=0; g_usbbuf_fi=fi; return g_usbbuf_n; }
static int usbfs_read(const char* p,char* buf,int off,int len){ int n=usbfs_load(p); if(n<0)return -1; if(off>=n)return 0; int c=n-off; if(c>len)c=len; for(int i=0;i<c;i++)buf[i]=g_usbbuf[off+i]; return c; }
static int usbfs_size(const char* p){ return usbfs_load(p); }
static const vfs_fs_ops USBFS={ "usbfs /mnt/usb", usbfs_match, usbfs_read, usbfs_size };
static int ux_mount(const char* src,const char* target){
    (void)src; if(g_uid!=0)return -1; if(!seq((char*)target,"/mnt/usb"))return -1;
    if(!msd_ok||!ufat_ok){ if(!msd_autoinit())return -1; ufat_mount(); if(!ufat_ok)return -1; }
    dt_init(); int mn=dt_mkdir_at(0,"mnt"); dt_mkdir_at(mn,"usb");
    if(!g_usb_mounted){ vfs_register(&USBFS); g_usb_mounted=1; } g_usbbuf_fi=-1; return 0;
}
static int ux_umount(const char* target){ if(g_uid!=0||!seq((char*)target,"/mnt/usb")||!g_usb_mounted)return -1; g_usb_mounted=0; g_usbbuf_fi=-1; return 0; }

static int g_fb0_open=0, g_fb0_dirty=0;
static u8 g_mq[256][3]; static int g_mq_h=0,g_mq_t=0, g_mouse_open=0;
static void ux_mouse_push(int dx,int dy,int btn){ if(!g_mouse_open)return; while(dx||dy||1){ int sx=dx>127?127:dx<-127?-127:dx, sy=dy>127?127:dy<-127?-127:dy;
        int nt=(g_mq_t+1)&255; if(nt==g_mq_h)return; g_mq[g_mq_t][0]=(u8)(0x08|(btn&7)|(sx<0?0x10:0)|(sy<0?0x20:0)); g_mq[g_mq_t][1]=(u8)sx; g_mq[g_mq_t][2]=(u8)sy; g_mq_t=nt; dx-=sx; dy-=sy; if(!dx&&!dy)break; } }
static int ux_dev_read(struct vfile* v,char* buf,int len){
    if(v->dev==9){ u64 tot=(u64)W*H*4; if((u64)v->off>=tot)return 0; int c=len; if((u64)v->off+(u64)c>tot)c=(int)(tot-(u64)v->off); u8* s=(u8*)SCENE; for(int i=0;i<c;i++)buf[i]=(char)s[v->off+i]; v->off+=c; return c; }
    if(v->dev==10){ int c=0; while(c+3<=len&&g_mq_h!=g_mq_t){ buf[c]=(char)g_mq[g_mq_h][0]; buf[c+1]=(char)g_mq[g_mq_h][1]; buf[c+2]=(char)g_mq[g_mq_h][2]; g_mq_h=(g_mq_h+1)&255; c+=3; } if(!c&&g_js_running)return JS_BLOCK; return c; }
    return 0;
}
static int ux_dev_write(struct vfile* v,const char* buf,int len){
    if(v->dev==9){ u64 tot=(u64)W*H*4; if((u64)v->off>=tot)return 0; int c=len; if((u64)v->off+(u64)c>tot)c=(int)(tot-(u64)v->off); u8* d=(u8*)SCENE; for(int i=0;i<c;i++)d[v->off+i]=(u8)buf[i]; v->off+=c; g_fb0_dirty=1; return c; }
    return -1;
}

static char g_hostname[32]="noovex";
static int ux_read_file(const char* p,char* out,int cap){ int isd; int k=ux_lookup(p,&isd,1); if(k<0||isd)return -1; int n=FS[k].len<cap-1?FS[k].len:cap-1; for(int i=0;i<n;i++)out[i]=FS[k].data[i]; out[n]=0; return n; }
static int ux_mkfile(int dir,const char* nm,const char* body,unsigned mode){
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dir&&seq(FS[k].name,(char*)nm))return k;
    for(int k=0;k<FS_N;k++) if(!FS[k].used){ int q=0; while(nm[q]&&q<63){ FS[k].name[q]=nm[q]; q++; } FS[k].name[q]=0; int L=0; while(body[L]&&L<FS_CAP-2){ FS[k].data[L]=body[L]; L++; } FS[k].data[L]=0; FS[k].len=L;
        FS[k].used=1; FS[k].dir=dir; FS[k].folder=0; FS[k].mode=mode; FS[k].uid=0; FS[k].gid=0; FS[k].nlink=1; ux_touch(k,1); FS[k].atime=FS[k].mtime; if(disk_ok)nvx3_sync(k); return k; }
    return -1;
}
static int ux_pw_field(const char* line,int idx,char* out,int cap){ int f=0,o=0; for(int i=0;line[i]&&line[i]!='\n';i++){ if(line[i]==':'){ f++; continue; } if(f==idx&&o<cap-1)out[o++]=line[i]; } out[o]=0; return f>=idx; }
static int ux_uid_name(int uid,char* out,int cap){ char buf[1024]; int n=ux_read_file("/etc/passwd",buf,sizeof(buf)); if(n<=0)return 0;
    for(char* l=buf;*l;){ char u[16]; ux_pw_field(l,2,u,16); int v=0; for(int i=0;u[i];i++)v=v*10+(u[i]-'0'); if(u[0]&&v==uid){ ux_pw_field(l,0,out,cap); return 1; } while(*l&&*l!='\n')l++; if(*l)l++; }
    return 0; }
static int ux_login_user(const char* name){
    char buf[1024]; int n=ux_read_file("/etc/passwd",buf,sizeof(buf)); if(n<=0)return -1;
    for(char* l=buf;*l;){ char nm[32]; ux_pw_field(l,0,nm,32); if(seq(nm,(char*)name)){ char u[16],g[16],home[64]; ux_pw_field(l,2,u,16); ux_pw_field(l,3,g,16); ux_pw_field(l,5,home,64);
            int uv=0; for(int i=0;u[i];i++)uv=uv*10+(u[i]-'0'); int gv=0; for(int i=0;g[i];i++)gv=gv*10+(g[i]-'0');
            g_uid=uv; g_gid=gv; int d=dt_resolve_dir(home); if(d>=0)g_dcwd=d; env_set("HOME",home); env_set("USER",nm); return uv; }
        while(*l&&*l!='\n')l++; if(*l)l++; }
    return -1;
}
static void ux_boot_layout(void){
    dt_init();
    int etc=dt_mkdir_at(0,"etc"), usr=dt_mkdir_at(0,"usr"), var=dt_mkdir_at(0,"var"), tmp=dt_mkdir_at(0,"tmp"), mnt=dt_mkdir_at(0,"mnt");
    dt_mkdir_at(usr,"bin"); dt_mkdir_at(var,"log"); dt_mkdir_at(0,"sbin"); dt_mkdir_at(0,"root"); dt_mkdir_at(0,"home"); dt_mkdir_at(0,"bin"); dt_mkdir_at(0,"dev"); dt_mkdir_at(mnt,"usb");
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&!DT[i].mode)DT[i].mode=0040755u;
    if(tmp>=0){ DT[tmp].mode=0041777u; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==tmp)FS[k].used=0; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==tmp)g_lk[i].used=0; }
    if(etc>=0){
        ux_mkfile(etc,"passwd","root:x:0:0:root:/root:/bin/nsh\nuser:x:1000:1000:user:/home:/bin/nsh\n",0100644u);
        ux_mkfile(etc,"group","root:x:0:\nusers:x:1000:user\n",0100644u);
        ux_mkfile(etc,"hostname","noovex\n",0100644u);
        ux_mkfile(etc,"rc","# /etc/rc - run by init (PID 1) at boot\necho NoovexOS: init started\nmount /dev/usb /mnt/usb\n",0100644u);
        char hb[40]; int n=ux_read_file("/etc/hostname",hb,sizeof(hb)); if(n>0){ int o=0; for(int i=0;hb[i]&&hb[i]!='\n'&&o<31;i++)g_hostname[o++]=hb[i]; g_hostname[o]=0; }
    }
    dt_save();
}
static int ux_stat2(const char* p,u32* o,int follow){
    for(int i=0;i<10;i++)o[i]=0; dt_init();
    int dv=dev_id(p); if(dv){ o[0]=0020666u; o[3]=1; o[5]=900u+(u32)dv; return 0; }
    int fi=ux_usb_idx(p); if(fi>=0){ o[0]=0100644u; o[3]=1; o[4]=ufs[fi].size; o[5]=2000u+(u32)fi; return 0; }
    int isd; int k=ux_lookup(p,&isd,follow);
    if(k<0){ u32 L; const char* b=p; if(b[0]=='/')b++; if(jc_find_prog(p,&L)&&(p[0]!='/'||(b[0]=='b'&&b[1]=='i'&&b[2]=='n')||(b[0]=='s'&&b[1]=='b')||(b[0]=='u'&&b[1]=='s'&&b[2]=='r'))){ o[0]=0100755u; o[3]=1; o[4]=L; o[5]=3000u; return 0; } return -1; }
    if(isd){ o[0]=DT[k].mode?DT[k].mode:0040755u; o[1]=(u32)DT[k].uid; o[2]=(u32)DT[k].gid; o[3]=2; o[5]=1000u+(u32)k; o[9]=1; return 0; }
    o[0]=FS[k].mode?FS[k].mode:0100644u; o[1]=(u32)FS[k].uid; o[2]=(u32)FS[k].gid; o[3]=FS[k].nlink?FS[k].nlink:1; o[4]=(u32)FS[k].len; o[5]=(u32)k+1; o[6]=FS[k].mtime; o[7]=FS[k].atime; o[8]=FS[k].ctime;
    return 0;
}
static int k_readdir_extra(int d,int n,char* name,int* type){
    int c=0;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d){ if(c==n){ int j=0; while(g_lk[i].name[j]&&j<51){ name[j]=g_lk[i].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/bin")) for(int i=0;JS_BIN[i].name;i++){ if(JS_BIN[i].name[0]=='+')continue; if(c==n){ int j=0; while(JS_BIN[i].name[j]&&j<63){ name[j]=JS_BIN[i].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/sbin")){ if(c==n){ name[0]='i';name[1]='n';name[2]='i';name[3]='t';name[4]=0; if(type)*type=8; return 1; } c++; }
    if(g_usb_mounted&&d==dt_resolve_dir("/mnt/usb")) for(int f=0;f<ufs_n;f++){ if(c==n){ int j=0; while(ufs[f].name[j]&&j<15){ name[j]=ufs[f].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/dev")){ static const char* DV[]={"null","zero","random","urandom","full","tty","console","fb0",0}; for(int i=0;DV[i];i++){ if(c==n){ int j=0; while(DV[i][j]){ name[j]=DV[i][j]; j++; } name[j]=0; if(type)*type=2; return 1; } c++; } }
    return 0;
}
static void jc_start_init(void){
    if(jc_idx(1)>=0)return;
    int i=jc_create("init",0,0); if(i<0)return;
    g_jp[i].pid=1; g_jp[i].pgid=1; g_jp[i].ppid=0; g_jp[i].uid=0; g_jp[i].gid=0; if(g_next_pid<2)g_next_pid=2;
}
static void ux_desktop_start(const char* user){
    static int done=0; if(done)return; done=1;
    ux_boot_layout(); if(user&&user[0])ux_login_user(user); jc_start_init();
}
