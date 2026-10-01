/* ---- NVXFS: persistent filesystem on the attached ATA disk (ported from Noovex9) ---- */
static u32 g_nvx_base=2048; static int g_nvx_generic=0; static const char* g_nvx_note="";
#define NVX_DIR_LBA  g_nvx_base
#define NVX_DATA0    (NVX_DIR_LBA+8)
#define NVX_SECPF    (FS_CAP/512)                         /* sectors per file = FS_CAP */
#define NVX_MAX      FS_N
#define NVX_MAGIC    0x4E565849u   /* bumped: v2 format with directory tree */
#define NVX_DN_MAX   64
#define NVX_PTYPE    0x7F
#define NVX3_MAGIC_K 0x3358564Eu
#define NVX_REGION_SECS (8u+(u32)(NVX_MAX+1)*NVX_SECPF)
typedef struct { char name[64]; u32 start,len,folder,dir,mode,uid,gid; } nvx_ent;
typedef struct { char name[32]; int parent, used; unsigned int mode; } nvx_dnode;
typedef struct { u32 magic,count; nvx_ent e[NVX_MAX]; u32 dcount; nvx_dnode d[NVX_DN_MAX]; } nvxfs_t;
static nvxfs_t g_nvx; static u8 nvx_sec[512]; static int disk_ok=0; static int g_nvx_have_tree=0;
static int nvx_rd(u32 lba,u8* b){ return g_nvx_generic?gdisk_read(lba,1,b):ata_read(lba,1,b); }
static int nvx_wr(u32 lba,const u8* b){ return g_nvx_generic?gdisk_write(lba,b):ata_write(lba,b); }
static void nvx_flush(void){ nvx3_flush_meta(); }
static void nvx_format(void){ g_n3_conv=0; g_nvx.magic=NVX_MAGIC; g_nvx.count=0; for(int i=0;i<NVX_MAX;i++){ g_nvx.e[i].name[0]=0; g_nvx.e[i].start=0; g_nvx.e[i].len=0; g_nvx.e[i].folder=0; g_nvx.e[i].dir=1; } g_nvx.dcount=0; for(int i=0;i<NVX_DN_MAX;i++)g_nvx.d[i].used=0; nvx3_sync_all(); }
static int nvx_mount(int allow_format){ u8* p=(u8*)&g_nvx; int total=(int)sizeof(g_nvx),secs=(total+511)/512;
    for(int se=0;se<secs;se++){ if(nvx_rd(NVX_DIR_LBA+se,nvx_sec))return -1; for(int i=0;i<512;i++){ int off=se*512+i; if(off<total)p[off]=nvx_sec[i]; } }
    if(*(u32*)&g_nvx==NVX3_MAGIC_K){ g_nvx.count=0; g_nvx.dcount=0; return 2; }
    if(g_nvx.magic==NVX_MAGIC)g_n3_conv=1;
    if(g_nvx.magic!=NVX_MAGIC){ if(!allow_format)return -1; nvx_format(); } if(g_nvx.count>NVX_MAX)g_nvx.count=NVX_MAX; if(g_nvx.dcount>NVX_DN_MAX)g_nvx.dcount=NVX_DN_MAX; return 0; }
static int nvx_find(const char*name){ for(u32 i=0;i<g_nvx.count;i++){ if(seq(g_nvx.e[i].name,name))return (int)i; } return -1; }
static int nvx_read(int idx,u8*buf,int max){ if(idx<0||(u32)idx>=g_nvx.count)return 0; int len=(int)g_nvx.e[idx].len; if(len>max)len=max; int got=0,se=0;
    while(got<len){ if(nvx_rd(g_nvx.e[idx].start+se,nvx_sec))break; int n=len-got; if(n>512)n=512; for(int i=0;i<n;i++)buf[got+i]=nvx_sec[i]; got+=n; se++; } return got; }
static int nvx_write(const char*name,const u8*buf,int len,int folder){ if(!disk_ok)return -1; (void)len; (void)folder;
    if((const char*)buf>=FS[0].data&&(const char*)buf<(const char*)&FS[FS_N]){ int k=(int)(((const char*)buf-(const char*)&FS[0])/(long)sizeof(FsFile)); if(k>=0&&k<FS_N&&FS[k].used)return nvx3_sync(k); }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,(char*)name))return nvx3_sync(k);
    return -1; }
static int nvx_write_old(const char*name,const u8*buf,int len,int folder){ if(!disk_ok)return -1; int idx=nvx_find(name);
    if(idx<0){ if(g_nvx.count>=NVX_MAX)return -1; idx=(int)g_nvx.count; int k=0; while(name[k]&&k<63){g_nvx.e[idx].name[k]=name[k];k++;} g_nvx.e[idx].name[k]=0;
        { int slot=1; for(;slot<=NVX_MAX;slot++){ u32 st=NVX_DATA0+slot*NVX_SECPF; int taken=0; for(u32 q=0;q<g_nvx.count;q++) if(g_nvx.e[q].start==st){taken=1;break;} if(!taken)break; } g_nvx.e[idx].start=NVX_DATA0+slot*NVX_SECPF; } g_nvx.count++; }
    g_nvx.e[idx].folder=(u32)folder;
    { int di=1; unsigned int md=0100644u; for(int f=0;f<FS_N;f++) if(FS[f].used&&seq(FS[f].name,(char*)name)){di=FS[f].dir;md=FS[f].mode?FS[f].mode:0100644u;break;} g_nvx.e[idx].dir=(u32)di; g_nvx.e[idx].mode=md; g_nvx.e[idx].uid=0; g_nvx.e[idx].gid=0; }
    int mb=NVX_SECPF*512; if(len>mb)len=mb; g_nvx.e[idx].len=len;
    int wr=0,se=0; while(wr<len){ for(int i=0;i<512;i++)nvx_sec[i]=0; int n=len-wr; if(n>512)n=512; for(int i=0;i<n;i++)nvx_sec[i]=buf[wr+i]; if(nvx_wr(g_nvx.e[idx].start+se,nvx_sec))return -1; wr+=n; se++; } nvx_flush(); return idx; }
static void nvx_delete_name(const char*name){ (void)name; nvx3_flush_meta(); }
static void nvx_delete_name_old(const char*name){ int idx=nvx_find(name); if(idx<0)return; for(u32 i=idx;i+1<g_nvx.count;i++)g_nvx.e[i]=g_nvx.e[i+1]; g_nvx.count--; nvx_flush(); }   /* entries keep their own data sectors */
static int nvx_is_zero(const u8* b){ for(int i=0;i<512;i++) if(b[i])return 0; return 1; }
static int nvx_locate(void){
    u8 mbr[512];
    if(g_ata_drv<0) ata_detect();
    if(g_ata_drv>=0){ g_nvx_generic=0; if(ata_read(0,1,mbr))return 0; }
    else { gdisk_ensure(); if(g_dktype==DK_NONE||!g_dksectors)return 0; if(g_dktype==DK_NVME&&!g_nvme_bsz)return 0; g_nvx_generic=1; if(gdisk_read(0,1,mbr))return 0; }
    int valid=(mbr[510]==0x55&&mbr[511]==0xAA), anyp=0;
    if(valid) for(int e=0;e<4;e++){ u8* pe=mbr+446+e*16; u32 st=*(u32*)(pe+8)*(g_nvx_generic?dk_ratio():1u), sz=*(u32*)(pe+12)*(g_nvx_generic?dk_ratio():1u);
        if(pe[4]&&sz){ anyp=1; if(pe[4]==NVX_PTYPE&&sz>=NVX_REGION_SECS){ g_nvx_base=st; g_nvx_note="NoovexOS data partition"; return 2; } } }
    if(anyp){ g_nvx_note="Disk belongs to another system - not touched"; return 0; }
    if(!nvx_is_zero(mbr)&&!valid){ g_nvx_note="Unrecognised disk - not touched"; return 0; }
    g_nvx_base=2048; u8 d[512]; if(nvx_rd(g_nvx_base,d))return 0;
    if(*(u32*)d==NVX_MAGIC||*(u32*)d==NVX3_MAGIC_K){ g_nvx_note="Scratch disk"; return 2; }
    if(nvx_is_zero(d)){ g_nvx_note="Blank scratch disk"; return 1; }
    g_nvx_note="Unrecognised data at LBA 2048 - not touched"; return 0;
}
static int disk_init(void){
    int how=nvx_locate();
    if(!how){ disk_ok=0; return 0; }
    disk_ok=1; int mr=nvx_mount(1); if(mr<0){ disk_ok=0; return 0; }
    if(mr==2){ if(nvx3_try_load()){ disk_ok=0; return 0; } g_nvx_have_tree=(g_nvx.dcount>0); return 1; }
    for(u32 i=0;i<g_nvx.count && i<FS_N;i++){
        int len=(int)g_nvx.e[i].len; if(len>FS_CAP-1)len=FS_CAP-1;
        int slot=-1; for(int j=0;j<FS_N;j++) if(FS[j].used&&seq(FS[j].name,g_nvx.e[i].name)){slot=j;break;}
        if(slot<0) for(int j=0;j<FS_N;j++) if(!FS[j].used){slot=j;break;}
        if(slot<0)break;
        int k=0; while(g_nvx.e[i].name[k]&&k<63){FS[slot].name[k]=g_nvx.e[i].name[k];k++;} FS[slot].name[k]=0;
        int got=nvx_read((int)i,(u8*)FS[slot].data,len); FS[slot].data[got]=0; FS[slot].len=got; FS[slot].used=1; FS[slot].folder=(int)g_nvx.e[i].folder;
        FS[slot].dir=(int)g_nvx.e[i].dir; if(FS[slot].dir<0||FS[slot].dir>=NVX_DN_MAX)FS[slot].dir=1;
        FS[slot].mode=g_nvx.e[i].mode?g_nvx.e[i].mode:0100644u; FS[slot].uid=(int)g_nvx.e[i].uid; FS[slot].gid=(int)g_nvx.e[i].gid;
    }
    g_nvx_have_tree=(g_nvx.dcount>0);
    for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].nlink)FS[k].nlink=1;
    if(g_n3_conv){ dt_init(); nvx3_sync_all(); g_nvx_note="Converted to the inode format"; }
    return 1;
}
