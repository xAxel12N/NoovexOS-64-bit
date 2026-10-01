/* ---- Stage 3+: a real directory tree with arbitrary nesting ---- */
#define DN_MAX 64
typedef struct { char name[32]; int parent; int used; unsigned int mode; int uid,gid; } DNode;
static DNode DT[DN_MAX];
int g_dcwd=0;                   /* current directory node (0 = root) */
static int g_dtree_init=0;
static int dt_mkdir_at(int parent,const char* name){
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==parent&&seq(DT[i].name,(char*)name))return i; /* exists */
    for(int i=0;i<DN_MAX;i++) if(!DT[i].used){ int k=0; while(name[k]&&k<31){DT[i].name[k]=name[k];k++;} DT[i].name[k]=0; DT[i].parent=parent; DT[i].used=1; return i; }
    return -1;
}
extern int g_nvx_have_tree; extern nvxfs_t g_nvx;
static void dt_load_from_disk(void); static int dt_child(int parent,const char* name);
static void dt_init(void){
    if(g_dtree_init)return; g_dtree_init=1;
    for(int i=0;i<DN_MAX;i++)DT[i].used=0;
    DT[0].used=1; DT[0].name[0]='/'; DT[0].name[1]=0; DT[0].parent=-1;   /* root */
    if(g_nvx_have_tree){ dt_load_from_disk(); }
    else { int home=dt_mkdir_at(0,"home"); dt_mkdir_at(0,"bin"); dt_mkdir_at(0,"etc");
           dt_mkdir_at(0,"downloads"); dt_mkdir_at(0,"tmp"); dt_mkdir_at(0,"dev"); g_dcwd=home; }
}
/* persist the in-memory tree to the NVXFS structure and flush to disk */
static void nvx_flush(void);
static void dt_save(void){
    if(!disk_ok)return;
    int c=0; for(int i=0;i<DN_MAX&&i<NVX_DN_MAX;i++){
        g_nvx.d[i].used=DT[i].used; g_nvx.d[i].parent=DT[i].parent;
        int k=0; while(DT[i].name[k]&&k<31){g_nvx.d[i].name[k]=DT[i].name[k];k++;} g_nvx.d[i].name[k]=0; g_nvx.d[i].mode=DT[i].mode?DT[i].mode:0040755u;
        if(DT[i].used)c=i+1;
    }
    g_nvx.dcount=(u32)c; nvx_flush();
}
static void dt_load_from_disk(void){
    for(int i=0;i<DN_MAX;i++)DT[i].used=0;
    int n=(int)g_nvx.dcount; if(n>NVX_DN_MAX)n=NVX_DN_MAX;
    for(int i=0;i<n;i++){ DT[i].used=g_nvx.d[i].used; DT[i].parent=g_nvx.d[i].parent;
        int k=0; while(g_nvx.d[i].name[k]&&k<31){DT[i].name[k]=g_nvx.d[i].name[k];k++;} DT[i].name[k]=0; DT[i].mode=g_nvx.d[i].mode?g_nvx.d[i].mode:0040755u; }
    if(!DT[0].used){ DT[0].used=1; DT[0].name[0]='/'; DT[0].name[1]=0; DT[0].parent=-1; }
    /* set cwd to /home if present, else root */
    if(dt_child(0,"dev")<0)dt_mkdir_at(0,"dev"); int home=dt_child(0,"home"); g_dcwd=(home>=0)?home:0;
}
/* find a child directory node by name under parent; -1 if none */
static char* mode_str(unsigned int m,char* o){
    o[0] = (m&S_IFDIR)?'d':((m&S_IFCHR)?'c':'-');
    o[1]=(m&0400)?'r':'-'; o[2]=(m&0200)?'w':'-'; o[3]=(m&0100)?'x':'-';
    o[4]=(m&040)?'r':'-';  o[5]=(m&020)?'w':'-';  o[6]=(m&010)?'x':'-';
    o[7]=(m&04)?'r':'-';   o[8]=(m&02)?'w':'-';   o[9]=(m&01)?'x':'-';
    o[10]=0; return o;
}
static unsigned int dt_mode(int node){ (void)node; return 0040755u; }
static int dt_child(int parent,const char* name){
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==parent&&seq(DT[i].name,(char*)name))return i;
    return -1;
}
/* resolve a path string to a directory node; -1 on failure. Handles / . .. and relative. */
static int dt_resolve_dir(const char* p){
    dt_init();
    int cur = (p[0]=='/') ? 0 : g_dcwd;
    char comp[32]; int ci=0; int i=(p[0]=='/')?1:0;
    for(;;){
        char c=p[i];
        if(c=='/'||c==0){
            if(ci>0){ comp[ci]=0;
                if(seq(comp,".")){}
                else if(seq(comp,"..")){ if(DT[cur].parent>=0)cur=DT[cur].parent; }
                else { int nx=dt_child(cur,comp); if(nx<0)return -1; cur=nx; }
                ci=0;
            }
            if(c==0)break;
        } else { if(ci<31)comp[ci++]=c; }
        i++;
    }
    return cur;
}
/* ---- v8.13 kernel file-management helpers (path aware) ---- */
static int k_split(const char* p,int* dir,char* base){
    int last=-1; for(int i=0;p[i];i++) if(p[i]=='/')last=i;
    if(last<0){ *dir=g_dcwd; int k=0; while(p[k]&&k<63){base[k]=p[k];k++;} base[k]=0; return 0; }
    char pre[128]; int n=last; if(n>127)n=127; for(int i=0;i<n;i++)pre[i]=p[i]; pre[n]=0;
    *dir = n==0 ? 0 : dt_resolve_dir(pre);
    int k=0; for(int i=last+1;p[i]&&k<63;i++)base[k++]=p[i]; base[k]=0;
    return *dir>=0 ? 0 : -1;
}
static int k_find_file(const char* p){
    dt_init(); int d; char base[64];
    if(k_split(p,&d,base)==0 && base[0]){ for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d&&seq(FS[k].name,base))return k; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,(char*)p))return k;   /* legacy: flat name anywhere */
    return -1;
}
static int k_unlink(const char* p){
    int k=k_find_file(p); if(k<0)return -1;
    if(!perm_can_write(FS[k].mode))return -1;
    char nm[64]; scopy(nm,FS[k].name); FS[k].used=0; if(disk_ok)nvx_delete_name(nm); return 0;
}
static int k_is_ancestor(int anc,int node){ while(node>=0){ if(node==anc)return 1; node=DT[node].parent; } return 0; }
static int k_rename(const char* from,const char* to){
    dt_init(); if(!from||!to||!from[0]||!to[0])return -1;
    int k=k_find_file(from);
    if(k>=0){
        int td=dt_resolve_dir(to);
        if(td>=0){ FS[k].dir=td; }                                   /* move into existing directory */
        else { int nd; char nb[64]; if(k_split(to,&nd,nb)<0||!nb[0])return -1;
            for(int j=0;j<FS_N;j++) if(j!=k&&FS[j].used&&FS[j].dir==nd&&seq(FS[j].name,nb)){ if(disk_ok)nvx_delete_name(FS[j].name); FS[j].used=0; }   /* replace target */
            char on[64]; scopy(on,FS[k].name); int m=0; while(nb[m]&&m<63){FS[k].name[m]=nb[m];m++;} FS[k].name[m]=0; FS[k].dir=nd;
            if(disk_ok)nvx_delete_name(on); }
        if(disk_ok)nvx_write(FS[k].name,(const u8*)FS[k].data,FS[k].len,FS[k].folder);
        return 0; }
    int d=dt_resolve_dir(from); if(d<=0)return -1;                   /* directory rename/move (never root) */
    int td=dt_resolve_dir(to);
    if(td>=0){ if(k_is_ancestor(d,td))return -1; DT[d].parent=td; }
    else { int nd; char nb[64]; if(k_split(to,&nd,nb)<0||!nb[0])return -1; if(k_is_ancestor(d,nd))return -1;
        int m=0; while(nb[m]&&m<31){DT[d].name[m]=nb[m];m++;} DT[d].name[m]=0; DT[d].parent=nd; }
    dt_save(); return 0;
}
static int k_rmdir(const char* p){
    dt_init(); int d=dt_resolve_dir(p); if(d<=0)return -1;           /* no such dir, or root */
    if(k_is_ancestor(d,g_dcwd))return -1;                           /* busy: cwd is inside it */
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==d)return -1;   /* not empty */
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d)return -1;
    DT[d].used=0; dt_save(); return 0;
}
static int k_truncate_idx(int k,long len){
    if(k<0||!FS[k].used||len<0)return -1; if(!perm_can_write(FS[k].mode))return -1;
    if(len>FS_CAP-1)len=FS_CAP-1;
    for(long i=FS[k].len;i<len;i++)FS[k].data[i]=0;
    FS[k].len=(int)len; FS[k].data[len]=0;
    if(disk_ok)nvx_write(FS[k].name,(const u8*)FS[k].data,FS[k].len,FS[k].folder);
    return 0;
}
static int k_truncate(const char* p,long len){ return k_truncate_idx(k_find_file(p),len); }
static int k_ftruncate(int fd,long len){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX||T[fd]<0)return -1; struct vfile* v=&g_vf[T[fd]]; if(v->dev||v->fs_idx<0)return -1; return k_truncate_idx(v->fs_idx,len); }
static int k_fsync(int fd){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX)return -1; if(T[fd]==-2)return 0; if(T[fd]<0)return -1; struct vfile* v=&g_vf[T[fd]];
    if(v->dev||v->fs_idx<0)return 0; if(!disk_ok)return 0;
    return nvx_write(FS[v->fs_idx].name,(const u8*)FS[v->fs_idx].data,FS[v->fs_idx].len,FS[v->fs_idx].folder)<0?-1:0; }
static int k_readdir_extra(int d,int n,char* name,int* type);
static int k_readdir_at(const char* p,int n,char* name,int* type){
    dt_init(); int d=(p&&p[0])?dt_resolve_dir(p):g_dcwd; if(d<0)return -1; int c=0;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=d&&DT[i].parent==d){ if(c==n){ int j=0; while(DT[i].name[j]&&j<63){name[j]=DT[i].name[j];j++;} name[j]=0; if(type)*type=4; return 1; } c++; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){ if(c==n){ int j=0; while(FS[k].name[j]&&j<63){name[j]=FS[k].name[j];j++;} name[j]=0; if(type)*type=((FS[k].mode&0170000u)==0120000u)?10:8; return 1; } c++; }
    return k_readdir_extra(d,n-c,name,type);
}
static u64 k_days_from_civil(int y,int m,int d){ y-= m<=2; long era=(y>=0?y:y-399)/400; unsigned yoe=(unsigned)(y-era*400); unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1; unsigned doe=yoe*365+yoe/4-yoe/100+doy; return (u64)(era*146097+(long)doe-719468); }
static u64 k_realtime_ms(void){
    static u64 base=0;
    if(!base){ int hh,mm,ss,yr,mo,dy; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
        u64 epoch=k_days_from_civil(yr,mo,dy)*86400ULL+(u64)hh*3600+(u64)mm*60+(u64)ss;
        base=epoch*1000ULL-(u64)g_ticks*10ULL; }
    return base+(u64)g_ticks*10ULL;
}
/* build the absolute path string of a node into out[] */
static void dt_path(int node,char* out){
    dt_init();
    if(node==0){ out[0]='/'; out[1]=0; return; }
    char parts[16][32]; int n=0; int c=node;
    while(c>0&&n<16){ int k=0; while(DT[c].name[k]){parts[n][k]=DT[c].name[k];k++;} parts[n][k]=0; n++; c=DT[c].parent; }
    int o=0; for(int k=n-1;k>=0;k--){ out[o++]='/'; int j=0; while(parts[k][j])out[o++]=parts[k][j++]; }
    out[o]=0;
}
