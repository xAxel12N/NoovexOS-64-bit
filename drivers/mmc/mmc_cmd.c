#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/fs/vfs/volumes.h"
#include "drivers/mmc/mmc_blk.h"
static int sdc_is(const char* a,const char* w){ int i=0; for(;w[i];i++) if(a[i]!=w[i])return 0; return a[i]==0||a[i]==' '; }
static void sdc_vols(void){ char b[160]; int n=0;
    for(int i=0;i<g_vol_n;i++){ nvol* v=&g_vol[i]; if(v->kind!=VOL_SD)continue; n++; char nm[64]; vol_name(v,nm,64); char* p=b; p=scopy(p,"  "); p=scopy(p,nm); p=scopy(p,"  "); p=scopy(p,v->fs[0]?v->fs:"unknown"); p=scopy(p,"  ");
        p=udec(p,(u32)(v->total>>20)); p=scopy(p," MB"); if(v->has_free){ p=scopy(p,", "); p=udec(p,(u32)(v->freeb>>20)); p=scopy(p," MB free"); } p=scopy(p,"  ["); p=scopy(p,v->dev); p=scopy(p,"]\n"); *p=0; term_puts(b); }
    if(!n)term_puts("  no readable volume on the card (FAT12/16/32, exFAT, NTFS and ext2/3/4 are recognised)\n"); }
void sd_cmd(const char* arg){
    static char ob[512]; while(*arg==' ')arg++;
    if(!arg[0]||sdc_is(arg,"status")){ int r=mmc_poll_hosts(); (void)r; mmcblk_sync_hosts(); char b[96],*p=b; p=scopy(p,"SD/MMC hosts: "); p=udec(p,(u32)(g_mmc_nhosts<0?0:g_mmc_nhosts)); p=scopy(p,"\n"); *p=0; term_puts(b);
        for(int i=0;i<g_mmc_nhosts&&i<MMC_MAX_HOSTS;i++){ struct mmc_host* h=g_mmc_hosts[i]; if(!h)continue; p=b; p=scopy(p,"  "); p=scopy(p,h->name?h->name:"host"); p=scopy(p,h->present?(h->ready?"  card ready (":"  card present, not initialised\n"):"  no card\n");
            if(h->present&&h->ready){ p=scopy(p,mmc_type_name(h->card.type)); p=scopy(p,", "); p=udec(p,(u32)h->card.width); p=scopy(p,"-bit, "); p=udec(p,h->card.max_hz/1000000u); p=scopy(p," MHz)\n"); } *p=0; term_puts(b); }
        mmcblk_status(ob,sizeof ob); term_puts(ob); return; }
    if(sdc_is(arg,"mount")){ mmc_poll_hosts(); mmcblk_sync_hosts(); for(int i=0;i<MMCBLK_MAX;i++) if(g_mmcblk[i].used)g_mmcblk[i].ejected=0; g_mmcblk_gen++;
        if(!mmcblk_count()){ tcol(RED); term_puts("no SD/MMC card found\n"); tcol(WHT); return; } vol_scan(1); term_puts("SD card volumes (also in Files > This PC):\n"); sdc_vols(); return; }
    if(sdc_is(arg,"eject")){ int n=0; for(int i=0;i<MMCBLK_MAX;i++) if(!mmcblk_eject(i))n++; vol_scan(1);
        if(n){ tcol(GREEN); term_puts("SD card ejected: it is safe to remove it\n"); tcol(WHT); } else term_puts("no SD/MMC card to eject\n"); return; }
    term_puts("usage: sd status | sd mount | sd eject\n"); }
