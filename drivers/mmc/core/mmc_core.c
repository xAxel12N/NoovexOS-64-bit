#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "drivers/mmc/mmc.h"

struct mmc_host* g_mmc_hosts[MMC_MAX_HOSTS];
int g_mmc_nhosts;

static void mc_udelay(u32 us){ if(hr_precise()){ hr_delay_us(us); return; } for(volatile u32 i=0;i<us*1000u;i++){} }
static void mc_mdelay(u32 ms){ while(ms--)mc_udelay(1000); }
static u32 mc_bits(const u32* r,int start,int size){ int off=3-start/32,sh=start&31; u32 v=r[off]>>sh; if(size+sh>32)v|=r[off-1]<<((32-sh)&31); return size<32?v&((1u<<size)-1u):v; }
static void mc_log(struct mmc_host* h,const char* m){ char b[160],*p=b; p=scopy(p,"mmc: "); p=scopy(p,h->name?h->name:"host"); p=scopy(p,": "); p=scopy(p,m); *p=0; klog(b); }

const char* mmc_type_name(int t){ return t==MMC_TYPE_SD?"SD":t==MMC_TYPE_MMC?"MMC/eMMC":"none"; }

static int mc_cmd(struct mmc_host* h,u32 op,u32 arg,u32 fl,u32* resp){
    struct mmc_cmd c; u8* z=(u8*)&c; for(u32 i=0;i<sizeof c;i++)z[i]=0;
    c.opcode=op; c.arg=arg; c.flags=fl; int r=h->ops->send_cmd(h,&c); if(!r)r=c.error;
    if(resp)for(int i=0;i<4;i++)resp[i]=c.resp[i]; return r; }
static int mc_dcmd(struct mmc_host* h,u32 op,u32 arg,u32 fl,u8* buf,u32 bsz,u32 n,u32 dir,u32* resp){
    struct mmc_cmd c; u8* z=(u8*)&c; for(u32 i=0;i<sizeof c;i++)z[i]=0;
    c.opcode=op; c.arg=arg; c.flags=fl; c.data=buf; c.blksz=bsz; c.blocks=n; c.dataflags=dir;
    int r=h->ops->send_cmd(h,&c); if(!r)r=c.error; if(resp)*resp=c.resp[0]; return r; }
static int mc_acmd(struct mmc_host* h,u32 op,u32 arg,u32 fl,u32* resp){
    u32 r[4]; int e=mc_cmd(h,55,h->card.rca<<16,MMC_RSP_R1,r); if(e)return e; if(!(r[0]&(1u<<5)))return MMC_E_IO;
    return mc_cmd(h,op,arg,fl,resp); }
static int mc_wait_tran(struct mmc_host* h,u32 ms){
    u32 r[4]; for(u32 t=0;t<ms*10u;t++){ if(!mc_cmd(h,13,h->card.rca<<16,MMC_RSP_R1,r)){ if(r[0]&0xFDF90000u)return MMC_E_IO; if((r[0]&(1u<<8))&&((r[0]>>9)&15u)==4u)return 0; } mc_udelay(100); }
    return MMC_E_TIMEOUT; }

static int mc_sd_ocr(struct mmc_host* h,int v2){
    u32 r[4]; u32 arg=(h->ocr_avail&0x00FF8000u)|(v2?0x40000000u:0);
    for(int t=0;t<200;t++){ int e=mc_acmd(h,41,arg,MMC_RSP_R3,r); if(e)return e; if(r[0]&0x80000000u){ h->card.ocr=r[0]; h->card.hc=v2&&(r[0]&0x40000000u)?1:0; return 0; } mc_mdelay(5); }
    return MMC_E_TIMEOUT; }
static int mc_mmc_ocr(struct mmc_host* h){
    u32 r[4]; for(int t=0;t<200;t++){ int e=mc_cmd(h,1,(h->ocr_avail&0x00FF8080u)|0x40000000u,MMC_RSP_R3,r); if(e)return e; if(r[0]&0x80000000u){ h->card.ocr=r[0]; h->card.hc=((r[0]>>29)&3u)==2u; return 0; } mc_mdelay(5); }
    return MMC_E_TIMEOUT; }

static void mc_parse_cid(struct mmc_card* c){
    c->manf=(u8)mc_bits(c->cid,120,8); int n=c->type==MMC_TYPE_SD?5:6;
    for(int i=0;i<n;i++){ u32 ch=mc_bits(c->cid,96-8*i,8); c->name[i]=(char)(ch>=32&&ch<127?ch:'?'); } c->name[n]=0; }
static void mc_parse_csd(struct mmc_card* c){
    u32 st=mc_bits(c->csd,126,2);
    if(c->type==MMC_TYPE_SD&&st==1){ c->sectors=((u64)mc_bits(c->csd,48,22)+1ULL)*1024ULL; }
    else { u64 cs=mc_bits(c->csd,62,12),mul=mc_bits(c->csd,47,3),bl=mc_bits(c->csd,80,4); c->sectors=((cs+1ULL)<<(mul+2ULL)<<bl)>>9; }
    u32 ts=mc_bits(c->csd,96,8); static const u8 mlt[16]={0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80}; static const u32 un[4]={10000u,100000u,1000000u,10000000u};
    c->max_hz=(ts&7u)<4u?un[ts&7u]*mlt[(ts>>3)&15u]:25000000u; if(!c->max_hz||c->max_hz>52000000u)c->max_hz=c->type==MMC_TYPE_SD?25000000u:26000000u; }

static void mc_sd_tune(struct mmc_host* h){
    struct mmc_card* c=&h->card; static u8 scr[8] __attribute__((aligned(8))); static u8 sw[64] __attribute__((aligned(8)));
    if(!mc_cmd(h,55,c->rca<<16,MMC_RSP_R1,0)&&!mc_dcmd(h,51,0,MMC_RSP_R1,scr,8,1,MMC_DATA_READ,0)){
        c->scr[0]=(u32)scr[0]<<24|(u32)scr[1]<<16|(u32)scr[2]<<8|scr[3]; c->scr[1]=(u32)scr[4]<<24|(u32)scr[5]<<16|(u32)scr[6]<<8|scr[7]; }
    if((h->caps&MMC_CAP_4BIT)&&(c->scr[0]&(4u<<16))&&!mc_acmd(h,6,2,MMC_RSP_R1,0)&&!h->ops->set_width(h,MMC_BUS_4))c->width=4;
    u32 hz=c->max_hz;
    if((h->caps&MMC_CAP_HS)&&((c->scr[0]>>24)&15u)>=1u&&h->ops->set_hs){
        if(!mc_dcmd(h,6,0x00FFFFF0u,MMC_RSP_R1,sw,64,1,MMC_DATA_READ,0)&&(sw[13]&2u)&&!mc_dcmd(h,6,0x80FFFFF1u,MMC_RSP_R1,sw,64,1,MMC_DATA_READ,0)&&(sw[16]&15u)==1u){
            mc_udelay(1000); if(!h->ops->set_hs(h,1))hz=50000000u; } }
    if(h->f_max&&hz>h->f_max)hz=h->f_max; h->ops->set_clock(h,hz); }

static int mc_mmc_switch(struct mmc_host* h,u32 idx,u32 val){
    int e=mc_cmd(h,6,(3u<<24)|(idx<<16)|(val<<8),MMC_RSP_R1B,0); if(e)return e;
    u32 r[4]; for(int t=0;t<1000;t++){ if(!mc_cmd(h,13,h->card.rca<<16,MMC_RSP_R1,r)){ if(r[0]&(1u<<7))return MMC_E_IO; if(((r[0]>>9)&15u)==4u&&(r[0]&(1u<<8)))return 0; } mc_udelay(100); }
    return MMC_E_TIMEOUT; }
static void mc_mmc_tune(struct mmc_host* h){
    struct mmc_card* c=&h->card; static u8 ext[512] __attribute__((aligned(8))); int have=0;
    if(mc_bits(c->csd,122,4)>=4u&&!mc_dcmd(h,8,0,MMC_RSP_R1,ext,512,1,MMC_DATA_READ,0)){ have=1;
        u32 sc=(u32)ext[212]|(u32)ext[213]<<8|(u32)ext[214]<<16|(u32)ext[215]<<24; if(c->hc&&sc)c->sectors=sc; }
    u32 hz=c->max_hz;
    if(have&&(h->caps&MMC_CAP_HS)&&(ext[196]&3u)&&h->ops->set_hs&&!mc_mmc_switch(h,185,1)&&!h->ops->set_hs(h,1))hz=(ext[196]&2u)?52000000u:26000000u;
    if(have){ if((h->caps&MMC_CAP_8BIT)&&!mc_mmc_switch(h,183,2)&&!h->ops->set_width(h,MMC_BUS_8))c->width=8;
        else if((h->caps&MMC_CAP_4BIT)&&!mc_mmc_switch(h,183,1)&&!h->ops->set_width(h,MMC_BUS_4))c->width=4; }
    if(h->f_max&&hz>h->f_max)hz=h->f_max; h->ops->set_clock(h,hz); }

int mmc_card_init(struct mmc_host* h){
    if(!h||!h->ops||!h->ops->send_cmd||!h->ops->set_clock||!h->ops->set_width)return MMC_E_INVAL;
    struct mmc_card* c=&h->card; u8* z=(u8*)c; for(u32 i=0;i<sizeof *c;i++)z[i]=0; h->ready=0; c->width=1;
    if(h->ops->card_present&&!h->ops->card_present(h))return MMC_E_NOCARD;
    h->present=1;
    if(!h->ocr_avail)h->ocr_avail=MMC_VDD_33; if(!h->max_blocks)h->max_blocks=128;
    if(h->ops->reset&&h->ops->reset(h))return MMC_E_IO;
    if(h->ops->set_power&&h->ops->set_power(h,h->ocr_avail))return MMC_E_IO;
    h->ops->set_width(h,MMC_BUS_1); h->ops->set_clock(h,h->f_min&&h->f_min>400000u?h->f_min:400000u); mc_mdelay(2);
    u32 r[4]; mc_cmd(h,0,0,MMC_RSP_NONE,0); mc_mdelay(1);
    int v2=!mc_cmd(h,8,0x1AAu,MMC_RSP_R7,r)&&(r[0]&0xFFFu)==0x1AAu;
    if(!mc_sd_ocr(h,v2))c->type=MMC_TYPE_SD;
    else { mc_cmd(h,0,0,MMC_RSP_NONE,0); mc_mdelay(1); if(mc_mmc_ocr(h)){ mc_log(h,"no card answered"); return MMC_E_NOCARD; } c->type=MMC_TYPE_MMC; }
    if(mc_cmd(h,2,0,MMC_RSP_R2,c->cid)){ mc_log(h,"CMD2 failed"); return MMC_E_IO; }
    if(c->type==MMC_TYPE_SD){ if(mc_cmd(h,3,0,MMC_RSP_R6,r))return MMC_E_IO; c->rca=r[0]>>16; }
    else { c->rca=1; if(mc_cmd(h,3,c->rca<<16,MMC_RSP_R1,r))return MMC_E_IO; }
    if(mc_cmd(h,9,c->rca<<16,MMC_RSP_R2,c->csd)){ mc_log(h,"CMD9 failed"); return MMC_E_IO; }
    mc_parse_cid(c); mc_parse_csd(c);
    if(mc_cmd(h,7,c->rca<<16,MMC_RSP_R1B,r)){ mc_log(h,"CMD7 failed"); return MMC_E_IO; }
    if(!c->hc)mc_cmd(h,16,512,MMC_RSP_R1,r);
    if(c->type==MMC_TYPE_SD)mc_sd_tune(h); else mc_mmc_tune(h);
    h->ready=1; h->present=1;
    char b[128],*p=b; p=scopy(p,mmc_type_name(c->type)); p=scopy(p," card "); p=scopy(p,c->name); p=scopy(p,c->hc?" (high capacity), ":", ");
    p=udec(p,(u32)(c->sectors>>11)); p=scopy(p," MB, "); p=udec(p,(u32)c->width); p=scopy(p,"-bit"); *p=0; mc_log(h,b);
    return 0; }

void mmc_card_remove(struct mmc_host* h){ if(!h)return; if(h->ready)mc_log(h,"card removed"); h->ready=0; h->present=0; h->card.type=MMC_TYPE_NONE; h->card.sectors=0; }

static int mc_xfer(struct mmc_host* h,u64 lba,u32 n,u8* buf,int wr){
    if(!h||!h->ready)return MMC_E_NOCARD; if(lba+n>h->card.sectors)return MMC_E_INVAL;
    while(n){ u32 k=n>h->max_blocks?h->max_blocks:n; u32 arg=h->card.hc?(u32)lba:(u32)(lba<<9); int e=0; u32 st=0;
        for(int tr=0;tr<3;tr++){
            e=mc_dcmd(h,wr?(k>1?25u:24u):(k>1?18u:17u),arg,MMC_RSP_R1,buf,512,k,wr?MMC_DATA_WRITE:MMC_DATA_READ,&st);
            if(k>1){ int e2=mc_cmd(h,12,0,MMC_RSP_R1B,0); if(!e)e=e2; }
            if(!e&&(st&0xFDF90000u))e=MMC_E_IO;
            if(wr||e){ int e3=mc_wait_tran(h,wr?1000u:250u); if(!e)e=e3; }
            if(!e)break; if(h->ops->card_present&&!h->ops->card_present(h)){ mmc_card_remove(h); return MMC_E_NOCARD; } }
        if(e)return e; lba+=k; n-=k; buf+=(u64)k*512u; }
    return 0; }
int mmc_read_blocks(struct mmc_host* h,u64 lba,u32 n,void* buf){ return mc_xfer(h,lba,n,(u8*)buf,0); }
int mmc_write_blocks(struct mmc_host* h,u64 lba,u32 n,const void* buf){ if(h&&h->card.ro)return MMC_E_IO; return mc_xfer(h,lba,n,(u8*)(u64)buf,1); }

int mmc_register_host(struct mmc_host* h){
    if(!h||!h->ops||g_mmc_nhosts>=MMC_MAX_HOSTS)return -1; for(int i=0;i<g_mmc_nhosts;i++)if(g_mmc_hosts[i]==h)return i;
    g_mmc_hosts[g_mmc_nhosts]=h; h->ready=0; h->present=0;
    if(!h->ops->card_present||h->ops->card_present(h))mmc_card_init(h);
    return g_mmc_nhosts++; }

int mmc_poll_hosts(void){
    { static int pr; if(!pr){ pr=1; extern int sdhci_probe(void); extern int rtsx_probe(void); sdhci_probe(); rtsx_probe(); } }
    int n=0; for(int i=0;i<g_mmc_nhosts;i++){ struct mmc_host* h=g_mmc_hosts[i]; int p=h->ops->card_present?h->ops->card_present(h):1;
        if(p&&!h->ready&&!h->present)mmc_card_init(h);
        else if(!p&&(h->ready||h->present))mmc_card_remove(h);
        if(h->ready)n++; }
    return n; }
