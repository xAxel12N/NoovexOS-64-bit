#include "drivers/mmc/mmc_blk.h"
mmcblk g_mmcblk[MMCBLK_MAX]; u32 g_mmcblk_gen=0;
static void mb_cp(char* d,const char* s,int n){ int i=0; if(s) for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static char* mb_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
static char* mb_u(char* p,char* e,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<23); while(n&&p<e)*p++=t[--n]; return p; }
int mmcblk_add(int kind,const char* model,u64 sectors,int ro,void* ctx,mmcblk_rd_fn rd,mmcblk_wr_fn wr){
    if(!rd||!sectors){ return -1; } int i=0; while(i<MMCBLK_MAX&&g_mmcblk[i].used)i++; if(i>=MMCBLK_MAX)return -1;
    mmcblk* b=&g_mmcblk[i]; b->rd=rd; b->wr=wr; b->ctx=ctx; b->sectors=sectors; b->ro=ro||!wr; b->ejected=0; b->kind=kind; b->nrd=b->nwr=b->nerr=0;
    char nm[24]; nm[0]='m'; nm[1]='m'; nm[2]='c'; nm[3]='b'; nm[4]='l'; nm[5]='k'; nm[6]=(char)('0'+i); nm[7]=0; mb_cp(b->name,nm,24); mb_cp(b->model,model&&model[0]?model:kind==MMCBLK_MMC?"eMMC":"SD card",32);
    b->used=1; b->gen=++g_mmcblk_gen; return i; }
void mmcblk_remove(int i){ if(i<0||i>=MMCBLK_MAX||!g_mmcblk[i].used)return; g_mmcblk[i].used=0; g_mmcblk[i].rd=0; g_mmcblk[i].wr=0; g_mmcblk_gen++; }
int mmcblk_present(int i){ return i>=0&&i<MMCBLK_MAX&&g_mmcblk[i].used&&!g_mmcblk[i].ejected; }
int mmcblk_count(void){ int n=0; for(int i=0;i<MMCBLK_MAX;i++) if(mmcblk_present(i))n++; return n; }
int mmcblk_read(int i,u64 lba,int n,u8* buf){
    if(!mmcblk_present(i)||n<0||!buf){ return -1; } mmcblk* b=&g_mmcblk[i]; if(lba>=b->sectors||(u64)n>b->sectors-lba)return -1;
    while(n>0){ int c=n>MMCBLK_CHUNK?MMCBLK_CHUNK:n; if(b->rd(b->ctx,lba,c,buf)){ b->nerr++; return -1; } b->nrd+=(u32)c; lba+=(u64)c; buf+=(u64)c*512; n-=c; } return 0; }
int mmcblk_write(int i,u64 lba,int n,const u8* buf){
    if(!mmcblk_present(i)||n<0||!buf){ return -1; } mmcblk* b=&g_mmcblk[i]; if(b->ro||!b->wr)return -2; if(lba>=b->sectors||(u64)n>b->sectors-lba)return -1;
    while(n>0){ int c=n>MMCBLK_CHUNK?MMCBLK_CHUNK:n; if(b->wr(b->ctx,lba,c,buf)){ b->nerr++; return -1; } b->nwr+=(u32)c; lba+=(u64)c; buf+=(u64)c*512; n-=c; } return 0; }
int mmcblk_eject(int i){ if(!mmcblk_present(i))return -1; g_mmcblk[i].ejected=1; g_mmcblk_gen++; return 0; }
int mmcblk_status(char* out,int cap){
    if(!out||cap<2){ return 0; } char* p=out; char* e=out+cap-1; int any=0;
    for(int i=0;i<MMCBLK_MAX;i++){ mmcblk* b=&g_mmcblk[i]; if(!b->used)continue; any=1;
        p=mb_s(p,e,b->name); p=mb_s(p,e,"  "); p=mb_s(p,e,b->model); p=mb_s(p,e,"  "); p=mb_s(p,e,b->kind==MMCBLK_MMC?"MMC/eMMC":b->kind==MMCBLK_SDHC?"SDHC/SDXC":"SDSC"); p=mb_s(p,e,"  ");
        u64 mb=b->sectors/2048; if(mb>=10240){ p=mb_u(p,e,mb/1024); p=mb_s(p,e," GB"); } else { p=mb_u(p,e,mb); p=mb_s(p,e," MB"); }
        if(b->ro){ p=mb_s(p,e,"  read-only"); } if(b->ejected){ p=mb_s(p,e,"  ejected"); } p=mb_s(p,e,"  read "); p=mb_u(p,e,b->nrd); p=mb_s(p,e," written "); p=mb_u(p,e,b->nwr); p=mb_s(p,e," errors "); p=mb_u(p,e,b->nerr); p=mb_s(p,e,"\n"); }
    if(!any){ p=mb_s(p,e,"no SD/MMC card found\n"); } *p=0; return (int)(p-out); }
static int mbh_rd(void* c,u64 lba,int n,u8* buf){ return mmc_read_blocks((struct mmc_host*)c,lba,(u32)n,buf)?-1:0; }
static int mbh_wr(void* c,u64 lba,int n,const u8* buf){ return mmc_write_blocks((struct mmc_host*)c,lba,(u32)n,buf)?-1:0; }
int mmcblk_attach_host(struct mmc_host* h){
    if(!h||!h->ready||!h->card.sectors){ return -1; } for(int i=0;i<MMCBLK_MAX;i++) if(g_mmcblk[i].used&&g_mmcblk[i].ctx==h)return i;
    struct mmc_card* c=&h->card; char m[32]; int k=0; const char* pre=c->type==MMC_TYPE_MMC?"eMMC ":"SD card "; while(*pre&&k<20)m[k++]=*pre++; for(int j=0;j<8&&c->name[j]&&k<31;j++)m[k++]=c->name[j]; m[k]=0;
    return mmcblk_add(c->type==MMC_TYPE_MMC?MMCBLK_MMC:c->hc?MMCBLK_SDHC:MMCBLK_SD,m,c->sectors,c->ro,h,mbh_rd,mbh_wr); }
void mmcblk_detach_host(struct mmc_host* h){ for(int i=0;i<MMCBLK_MAX;i++) if(g_mmcblk[i].used&&g_mmcblk[i].ctx==h)mmcblk_remove(i); }
void mmcblk_sync_hosts(void){
    for(int i=0;i<g_mmc_nhosts&&i<MMC_MAX_HOSTS;i++){ struct mmc_host* h=g_mmc_hosts[i]; if(!h)continue; int on=-1; for(int j=0;j<MMCBLK_MAX;j++) if(g_mmcblk[j].used&&g_mmcblk[j].ctx==h)on=j;
        if(h->ready&&h->present&&h->card.sectors){ if(on<0)mmcblk_attach_host(h); } else if(on>=0)mmcblk_remove(on); } }
