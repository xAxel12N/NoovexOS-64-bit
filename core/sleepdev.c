#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/core/sleepsm.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/storage/gdisk.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/power.h"

extern volatile int g_disk_busy;
extern int g_s3_woke;
extern int g_acpi_have_s3, g_lps0_state;
extern int g_lid_open, g_screen_off;
extern u32 g_rxnum;
u32 rtw_find_mmio(void);
int wifi_hw_generic(void);
int wifi_up(void);
void wifi_radio_on(void);
int wifi_pci_prepare(u32 m);
int bt_hci_up(void);
void bl_screen(int on);
void fb_shadow_invalidate(void);
int power_lps0_present(void);
int s3_supported(void);
const char* s3_status(void);
int pci_d3hot_class(int cls,int sub,int on);
int sleep_mode(void);

static struct featguard slp_fg={"Sleep","feat-sleep.cfg",1,0,3,0,0};
static struct slp_sm g_slp;
static int slp_inited, slp_wifi_was, slp_bt_was;
char g_sleep_why[160];

static int sd_disk_p(void){ return g_dktype!=DK_NONE; }
static const char* sd_disk_b(void){ return g_disk_busy?"the disk is busy, try again in a moment":0; }
static int sd_disk_s(void){ return gdisk_flush()<0?-1:0; }
static int sd_disk_r(void){ if(g_s3_woke){ int r=g_dktype==DK_NVME?nvme_resume():g_dktype==DK_AHCI?ahci_resume():0; if(r<0){ klog("sleep: disk controller did not restart after S3"); return -1; } } return gdisk_flush()<0?-1:0; }

static u32 sd_usbsts(void){ return xhci_op?*(volatile u32*)(xhci_op+4):0xFFFFFFFFu; }
static int sd_usb_p(void){ return xhci_init_ok&&xhci_op; }
static const char* sd_usb_b(void){ u32 s=sd_usbsts(); if(s==0xFFFFFFFFu)return "the USB controller is not answering"; if(s&((1u<<2)|(1u<<12)))return "the USB controller reported an error"; return 0; }
static int sd_usb_s(void){ xhci_pm_suspend(sleep_mode()==SLP_MODE_S3); return 0; }
static int sd_usb_r(void){ int k=xhci_pm_resume(); if(k<0||(k>0&&g_s3_woke)){ xhci_mouse_autoinit(); if(!xhci_init_ok){ klog("sleep: USB controller did not restart after S3"); return -1; } } u32 s=sd_usbsts(); return (s==0xFFFFFFFFu||(s&((1u<<0)|(1u<<2)|(1u<<12))))?-1:0; }

static int sd_wifi_p(void){ return rtw_find_mmio()||wifi_hw_generic(); }
static int sd_wifi_s(void){ slp_wifi_was=g_wm_up&&g_wm_state!=WM_OFF; if(slp_wifi_was)wifi_radio_off(); if(slp_wifi_was&&rtw_find_mmio()&&!pci_d3hot_class(0x02,0x80,1))klog("sleep: Wi-Fi card put into D3hot"); return 0; }
static int sd_wifi_r(void){
    pci_d3hot_class(0x02,0x80,0);
    if(!slp_wifi_was)return 0;
    slp_wifi_was=0;
    u32 m=rtw_find_mmio();
    if(m){
        if(rtw_r32(m,0)==0xFFFFFFFFu){ wifi_pci_prepare(m); if(rtw_r32(m,0)==0xFFFFFFFFu){ klog("sleep: Wi-Fi card lost power and did not come back"); return -1; } }
        u16 fc=rtw_r16(m,0x0080);
        if((fc&(3u<<14))!=(3u<<14)){ klog("sleep: Wi-Fi firmware stopped during sleep - loading it again"); g_rxnum=0; if(wifi_up()<0)return -1; }
    }
    wifi_radio_on(); return 0; }

static int sd_bt_p(void){ return bt_hci_up(); }
static int sd_bt_s(void){ slp_bt_was=1; return 0; }
static int sd_bt_r(void){ int w=slp_bt_was; slp_bt_was=0; return (w&&!bt_hci_up())?-1:0; }

static int sd_snd_p(void){ return g_snd_state==1; }
static int sd_snd_s(void){ snd_rec_stop(); if(g_snd.stop)g_snd.stop(); return 0; }
static int sd_snd_r(void){ if(g_s3_woke&&snd_resume()<0){ klog("sleep: audio did not restart after S3"); return -1; } if(g_snd_state==1&&g_snd.volume)g_snd.volume(g_snd_vol); return 0; }

static int sd_fb_p(void){ return W&&H; }
static int sd_fb_s(void){ return 0; }
static int sd_fb_r(void){ fb_shadow_invalidate(); gfx_invalidate_all(); return (W&&H)?0:-1; }

static const struct slp_dev SD_DISK={"the system disk",sd_disk_p,sd_disk_b,sd_disk_s,sd_disk_r};
static const struct slp_dev SD_SND={"audio",sd_snd_p,0,sd_snd_s,sd_snd_r};
static const struct slp_dev SD_BT={"Bluetooth",sd_bt_p,0,sd_bt_s,sd_bt_r};
static const struct slp_dev SD_WIFI={"Wi-Fi",sd_wifi_p,0,sd_wifi_s,sd_wifi_r};
static const struct slp_dev SD_USB={"USB",sd_usb_p,sd_usb_b,sd_usb_s,sd_usb_r};
static const struct slp_dev SD_FB={"the display",sd_fb_p,0,sd_fb_s,sd_fb_r};

static void slp_init(void){ if(slp_inited)return; slp_inited=1; slp_add(&g_slp,&SD_DISK); slp_add(&g_slp,&SD_SND); slp_add(&g_slp,&SD_BT); slp_add(&g_slp,&SD_WIFI); slp_add(&g_slp,&SD_USB); slp_add(&g_slp,&SD_FB); }
int sleep_enabled(void){ fg_boot(&slp_fg); return slp_fg.on&&!slp_fg.dead; }
void sleep_set_enabled(int on){ fg_enable(&slp_fg,on); g_sleep_why[0]=0; }
int sleep_mode(void){ return slp_pick(sleep_enabled(),g_acpi_have_s3,s3_supported(),power_lps0_present(),g_acpi.have_s1,g_acpi.hwred); }
static void slp_say(const char* m){ char* q=g_sleep_why; for(int i=0;m[i]&&i<159;i++)*q++=m[i]; *q=0; klog(m); toast(m); }
static void slp_fallback(void){ if(g_lid_open==0){ g_screen_off=1; bl_screen(0); } }
int sleep_begin(void){
    slp_init();
    if(!fg_begin(&slp_fg)){ slp_say(slp_fg.dead?"Sleep was turned off because the computer did not wake up properly - turn it on again in Settings > Power":"Sleep is turned off in Settings > Power"); slp_fallback(); return -1; }
    int r=slp_enter(&g_slp);
    if(r==-1||r==-3){ fg_ok(&slp_fg); slp_say(g_slp.msg); slp_fallback(); return -1; }
    if(r<0){ fg_fail(&slp_fg,g_slp.msg); slp_say(g_slp.msg); slp_fallback(); return -1; }
    { char b[96],*q=b; q=scopy(q,"sleep: devices ready, entering "); q=scopy(q,slp_mode_name(sleep_mode())); *q=0; klog(b); }
    return 0; }
void sleep_end(void){
    int bad=slp_leave(&g_slp);
    if(bad){ fg_fail(&slp_fg,g_slp.msg); slp_say(g_slp.msg); return; }
    fg_ok(&slp_fg); g_sleep_why[0]=0; klog("sleep: all devices resumed"); }
const char* sleep_status_text(void){
    static char b[200]; char* q=b;
    fg_boot(&slp_fg);
    if(!slp_fg.on)q=scopy(q,"Off - closing the lid turns off the screen");
    else if(slp_fg.dead)q=scopy(q,"Off - the computer did not wake up properly last time");
    else { q=scopy(q,"On - "); int m=slp_pick(1,g_acpi_have_s3,s3_supported(),g_lps0_state==1,g_acpi.have_s1,g_acpi.hwred); q=scopy(q,slp_mode_name(m)); if(g_acpi_have_s3&&m!=SLP_MODE_S3){ const char* w=s3_status(); if(w){ q=scopy(q," (S3 not used: "); q=scopy(q,w); *q++=')'; } } }
    if(g_sleep_why[0]){ q=scopy(q," - last: "); for(int i=0;g_sleep_why[i]&&q<b+198;i++)*q++=g_sleep_why[i]; }
    *q=0; return b; }
