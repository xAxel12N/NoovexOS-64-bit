#include "drivers/video/amd/fw/amd_fw.h"
#ifndef AMD_FW_HOST
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
#endif

amd_dmcub_t g_amd_dmcub;

#define AFV(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))

static const struct { u32 ver; const char* name; int bundled; } amd_fw_dmcub_tab[]={
    {AFV(2,1,0),"amdgpu/renoir_dmcub.bin",1},
    {AFV(3,0,1),"amdgpu/vangogh_dmcub.bin",0},
    {AFV(3,1,2),"amdgpu/yellow_carp_dmcub.bin",1},
    {AFV(3,1,3),"amdgpu/yellow_carp_dmcub.bin",1},
    {AFV(3,1,4),"amdgpu/dcn_3_1_4_dmcub.bin",1},
    {AFV(3,1,5),"amdgpu/dcn_3_1_5_dmcub.bin",1},
    {AFV(3,1,6),"amdgpu/dcn_3_1_6_dmcub.bin",0},
    {AFV(3,0,0),"amdgpu/sienna_cichlid_dmcub.bin",0},
    {AFV(3,0,2),"amdgpu/dimgrey_cavefish_dmcub.bin",0},
    {AFV(3,0,3),"amdgpu/beige_goby_dmcub.bin",0},
    {AFV(3,5,0),"amdgpu/dcn_3_5_dmcub.bin",0},
    {AFV(3,5,1),"amdgpu/dcn_3_5_1_dmcub.bin",0},
};

static u32 afw_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u16 afw_le16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }

const char* amd_fw_dmcub_name(u32 v,u16 did){
    if(v==AFV(2,1,0)&&(did==0x1638||did==0x15E7))return "amdgpu/green_sardine_dmcub.bin";
    for(unsigned i=0;i<sizeof amd_fw_dmcub_tab/sizeof amd_fw_dmcub_tab[0];i++)if(amd_fw_dmcub_tab[i].ver==v)return amd_fw_dmcub_tab[i].name;
    return 0; }

int amd_fw_dmcub_parse(amd_dmcub_t* f,u8* b,u32 n){
    f->ok=0; f->blob=b; f->len=n;
    if(!b||n<40){ f->why="file too small"; return -1; }
    u32 sz=afw_le32(b),hs=afw_le32(b+4);
    f->hdr_maj=afw_le16(b+8); f->hdr_min=afw_le16(b+10);
    f->ucode_ver=afw_le32(b+16); f->ucode_len=afw_le32(b+20); f->ucode_off=afw_le32(b+24);
    f->inst_const=afw_le32(b+32); f->bss=afw_le32(b+36);
    if(sz!=n||hs<40||hs>n){ f->why="bad common header"; return -1; }
    if(f->hdr_maj!=1){ f->why="unknown DMCUB header version"; return -1; }
    if(f->ucode_off<hs||f->ucode_off>n||f->ucode_len>n-f->ucode_off){ f->why="ucode outside file"; return -1; }
    if(f->inst_const>f->ucode_len||f->bss>f->ucode_len-f->inst_const){ f->why="inst/bss sizes exceed ucode"; return -1; }
    if(f->inst_const<AMD_DMUB_PSP_HDR+AMD_DMUB_PSP_FTR){ f->why="inst_const smaller than PSP wrapper"; return -1; }
    f->inst=b+f->ucode_off; f->bss_init=f->bss?b+f->ucode_off+f->inst_const:0;
    f->ok=1; f->why="ok"; return 0; }

#ifndef AMD_FW_HOST
static int afw_vfs_read(const char* path,u8** out,u32* len){
    *out=0; *len=0;
    const vfs_fs_ops* fs=vfs_lookup(path); int sz=(fs&&fs->size&&fs->read)?fs->size(path):-1;
    if(sz<64||sz>2*1024*1024)return -1;
    u8* b=(u8*)kmalloc((unsigned long)sz); if(!b)return -2;
    int got=0; while(got<sz){ int r=fs->read(path,(char*)b+got,got,sz-got); if(r<=0)break; got+=r; }
    if(got!=sz){ kfree(b); return -1; }
    *out=b; *len=(u32)sz; return 0; }
int (*amd_fw_read)(const char* path,u8** out,u32* len)=afw_vfs_read;
#define afw_free(p) kfree(p)
#else
#define afw_free(p) free(p)
#endif

void amd_fw_dmcub_free(void){ if(g_amd_dmcub.blob)afw_free(g_amd_dmcub.blob); g_amd_dmcub=(amd_dmcub_t){0}; }

int amd_fw_dmcub_load(u32 v,u16 did){
    amd_fw_dmcub_free();
    const char* nm=amd_fw_dmcub_name(v,did);
    if(!nm){ g_amd_dmcub.why="no DMCUB for this DCN"; return -1; }
    char path[96]; const char* pre="/lib/firmware/"; int k=0;
    for(const char* s=pre;*s&&k<95;)path[k++]=*s++;
    for(const char* s=nm;*s&&k<95;)path[k++]=*s++;
    path[k]=0;
    g_amd_dmcub.name=nm;
    u8* b=0; u32 n=0;
    if(!amd_fw_read||amd_fw_read(path,&b,&n)<0||!b){ g_amd_dmcub.why="firmware file missing"; return -1; }
    if(amd_fw_dmcub_parse(&g_amd_dmcub,b,n)<0){ const char* w=g_amd_dmcub.why; afw_free(b); g_amd_dmcub=(amd_dmcub_t){0}; g_amd_dmcub.name=nm; g_amd_dmcub.why=w; return -1; }
    g_amd_dmcub.name=nm; return 0; }

static char* afw_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; *p=0; return p; }
static char* afw_u(char* p,char* e,u32 v){ char t[12]; int i=0; do t[i++]=(char)('0'+v%10); while((v/=10)&&i<11); while(i&&p<e)*p++=t[--i]; *p=0; return p; }
static char* afw_x(char* p,char* e,u32 v){ p=afw_s(p,e,"0x"); for(int i=28;i>=0&&p<e;i-=4)*p++="0123456789abcdef"[(v>>i)&15]; *p=0; return p; }

int amd_fw_describe(char* o,int cap){
    if(!o||cap<2)return 0;
    char* p=o; char* e=o+cap-1; *p=0;
    amd_dmcub_t* f=&g_amd_dmcub;
    p=afw_s(p,e,"dmcub: "); p=afw_s(p,e,f->name?f->name:"-"); p=afw_s(p,e," ");
    if(f->ok){ p=afw_s(p,e,"ver "); p=afw_x(p,e,f->ucode_ver); p=afw_s(p,e," inst "); p=afw_u(p,e,f->inst_const); p=afw_s(p,e," bss "); p=afw_u(p,e,f->bss); p=afw_s(p,e," bytes"); }
    else p=afw_s(p,e,f->why?f->why:"not loaded");
    return (int)(p-o); }
