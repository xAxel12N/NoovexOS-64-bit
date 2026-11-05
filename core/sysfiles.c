#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/heap_api.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/gpio/amd_gpio.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/registry.h"
#include "include/nvx/core/progfiles.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/theme/theme.h"

int ux_write_text(const char* path,const char* body,int len,unsigned mode);
int ux_read_file(const char* p,char* out,int cap);
int ux_symlink(const char* target,const char* path);
int taskmgr_devdump(int cat,char* out,int cap); const char* taskmgr_devcat(int k);
int svc_count(void); int svc_info(int i,const char** name,const char** desc,const char** status,int* on,int* required);
const char* svc_id(int i); u64 svc_us(int i); u32 svc_every(int i); u64 svc_runs(int i);
extern char g_brand[52]; extern u32 g_ram_mb; extern volatile u64 g_ticks;
extern int ahci_ok, g_ata_drv, xhci_present, msd_ok, g_wnd_up, g_syn_ready, g_etp_ready, bat_present, g_vm_kind;
extern const char* g_vm_name;
extern u32 g_remap_n, g_remap_restored; extern u64 g_remap_lo, g_remap_hi;

#define SF_LOG_CAP (192*1024)
static char g_sf_log[SF_LOG_CAP]; static int g_sf_logn; static volatile int g_sf_lock; static volatile int g_sf_dirty; static u32 g_sf_lines;
static int g_sf_built; static u64 g_sf_next;

void sysfiles_logline(const char* s){
    if(__sync_lock_test_and_set(&g_sf_lock,1))return;
    char t[24]; int tn=0; u64 v=g_ticks; char r[24]; int rn=0; if(!v)r[rn++]='0'; while(v&&rn<20){ r[rn++]=(char)('0'+v%10); v/=10; }
    t[tn++]='['; while(rn)t[tn++]=r[--rn]; t[tn++]=']'; t[tn++]=' ';
    int L=0; while(s[L]&&L<400)L++; int need=tn+L+1;
    if(g_sf_logn+need>SF_LOG_CAP){ int cut=SF_LOG_CAP/2; while(cut<g_sf_logn&&g_sf_log[cut-1]!='\n')cut++; int k=0; for(int i=cut;i<g_sf_logn;i++)g_sf_log[k++]=g_sf_log[i]; g_sf_logn=k; }
    for(int i=0;i<tn;i++)g_sf_log[g_sf_logn++]=t[i];
    for(int i=0;i<L;i++){ char c=s[i]; g_sf_log[g_sf_logn++]=(c<32||c>126)?'.':c; }
    g_sf_log[g_sf_logn++]='\n'; g_sf_lines++; g_sf_dirty=1;
    __sync_lock_release(&g_sf_lock); }

static int sf_eq(const char* a,const char* b);
static char* sf_stamp(char* p,u32 ep);
static int sf_has(const char* s,int n,const char* w){ int L=0; while(w[L])L++; for(int i=0;i+L<=n;i++){ int k=0; while(k<L&&((s[i+k]|32)==(w[k]|32)))k++; if(k==L)return 1; } return 0; }
static int sf_pre(const char* s,int n,const char* w){ int k=0; while(w[k]&&k<n&&s[k]==w[k])k++; return !w[k]; }
static int sf_cat(const char* m,int n){
    static const char* const SEC[]={"auth","sudo","su:","login","faceid","password","sign-in","signin","lock:","account",0};
    static const char* const APP[]={"cmd:","app","xapp","apt","jobs","programs:","x11","xrun","lx","linux:","browser","pkg","editor","video","doom","camera","recorder","writer","settings:",0};
    for(int i=0;SEC[i];i++) if(sf_has(m,n,SEC[i]))return 1;
    for(int i=0;APP[i];i++) if(sf_pre(m,n,APP[i]))return 2;
    return 0; }
static const char* sf_lvl(const char* m,int n){
    static const char* const E[]={"fail","error","panic","fault","cannot","could not","denied","refused",0};
    static const char* const Wn[]={"warn","retry","timeout","timed out","not found","missing","slow","stuck","dropped",0};
    if(sf_pre(m,n,"cmd:"))return "Info   ";
    for(int i=0;E[i];i++) if(sf_has(m,n,E[i]))return "Error  ";
    for(int i=0;Wn[i];i++) if(sf_has(m,n,Wn[i]))return "Warning";
    return "Info   "; }
static char* sf_logfmt(char* o,u32 t0,u64 tk,const char* m,int n){
    o=sf_stamp(o,t0+(u32)(tk/100)); *o++=' '; *o++=' '; o=scopy(o,sf_lvl(m,n)); *o++=' '; *o++=' ';
    int c=-1; for(int i=0;i<n&&i<16;i++){ if(m[i]==':'){ c=i; break; } if(m[i]==' ')break; }
    int sl=0; if(c>0){ for(int i=0;i<c;i++)*o++=m[i]; sl=c; } else { o=scopy(o,"kernel"); sl=6; }
    for(;sl<10;sl++)*o++=' '; *o++=' ';
    int st=c>0?c+1:0; while(st<n&&m[st]==' ')st++; for(int i=st;i<n;i++)*o++=m[i]; *o++='\n'; *o=0; return o; }
static void sf_write(const char* path,const char* b,int n);
static void sf_flush_log(void){
    if(!g_sf_built||!g_sf_dirty)return;
    int cap=g_sf_logn+4096; if(cap>SF_LOG_CAP)cap=SF_LOG_CAP; char* b=(char*)kmalloc((unsigned long)cap+1); if(!b)return;
    while(__sync_lock_test_and_set(&g_sf_lock,1)){} int n=g_sf_logn; if(n>cap)n=cap; for(int i=0;i<n;i++)b[i]=g_sf_log[i]; g_sf_dirty=(n<g_sf_logn); __sync_lock_release(&g_sf_lock);
    int oc=n*2+8192; char* o[3]; for(int k=0;k<3;k++){ o[k]=(char*)kmalloc((unsigned long)oc); }
    char* fl=(char*)kmalloc((unsigned long)oc);
    if(!o[0]||!o[1]||!o[2]||!fl){ for(int k=0;k<3;k++) if(o[k])kfree(o[k]); if(fl)kfree(fl); kfree(b); return; }
    static const char* const NM[3]={"System","Security","Application"};
    char* q[3]; for(int k=0;k<3;k++){ q[k]=o[k]; q[k]=scopy(q[k],"NVG1 "); q[k]=scopy(q[k],NM[k]); q[k]=scopy(q[k]," log - NoovexOS\nTime                 Level    Source      Message\n"); }
    char* f=fl; f=scopy(f,"NVF1 flight recorder - the last 60 seconds\n");
    u32 now=ux_now(); u32 t0=now-(u32)(g_ticks/100); u64 from=g_ticks>6000?g_ticks-6000:0;
    int i=0; while(i<n){ int e=i; while(e<n&&b[e]!='\n')e++; int a=i; u64 tk=0; if(b[a]=='['){ a++; while(a<e&&b[a]>='0'&&b[a]<='9'){ tk=tk*10+(u64)(b[a]-'0'); a++; } if(a<e&&b[a]==']')a++; if(a<e&&b[a]==' ')a++; }
        int ml=e-a; int c=sf_cat(b+a,ml); if(q[c]-o[c]<oc-600)q[c]=sf_logfmt(q[c],t0,tk,b+a,ml);
        if(tk>=from&&c!=1&&f-fl<oc-600)f=sf_logfmt(f,t0,tk,b+a,ml);
        i=e+1; }
    { static char ab[16384]; int an=ux_read_file("/var/log/auth.log",ab,(int)sizeof(ab)-1); if(an>0&&q[1]-o[1]<oc-an-200){ q[1]=scopy(q[1],"\n--- /var/log/auth.log (kept on the disk) ---\n"); for(int k=0;k<an;k++)*q[1]++=ab[k]; *q[1]=0; } }
    sf_write("/System/Logs/System.nvg",o[0],(int)(q[0]-o[0])); ux_write_text("/System/Logs/Security.nvg",o[1],(int)(q[1]-o[1]),0100640u); { int isd=0; int k2=ux_lookup("/System/Logs/Security.nvg",&isd,0); if(k2>=0&&!isd){ FS[k2].uid=0; FS[k2].gid=0; } } { int isd=0; int k=ux_lookup("/System/Logs/Security.nvg",&isd,0); if(k>=0&&!isd)FS[k].mode=0100640u; } sf_write("/System/Logs/Application.nvg",o[2],(int)(q[2]-o[2]));
    f=scopy(f,"\nState now\n"); f=sf_stamp(f,now); f=scopy(f,"  heap in use "); f=udec(f,(u32)(heap_used()/1024)); f=scopy(f," KB, cores online "); f=udec(f,(u32)g_cores_online); f=scopy(f,", uptime "); f=udec(f,(u32)(g_ticks/100)); f=scopy(f," s\n");
    sf_write("/System/Crash/last.nvf",fl,(int)(f-fl));
    for(int k=0;k<3;k++)kfree(o[k]); kfree(fl); kfree(b); }

static int sf_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int sf_dir(const char* parent,const char* name){
    dt_init(); int d=dt_resolve_dir(parent); if(d<0)return -1; int c=dt_child(d,name); if(c<0)c=dt_mkdir_at(d,name);
    if(c>=0){ DT[c].uid=0; DT[c].gid=0; DT[c].mode=0040755u; } return c; }
static void sf_write(const char* path,const char* b,int n){
    ux_write_text(path,b,n,0100644u);
    int isd=0; int k=ux_lookup(path,&isd,0); if(k>=0&&!isd){ FS[k].uid=0; FS[k].gid=0; FS[k].mode=0100644u; } }
static void sf_writes(const char* path,const char* b){ int n=0; while(b[n])n++; sf_write(path,b,n); }
static char* sf_kv(char* p,const char* k,const char* v){ p=scopy(p,k); p=scopy(p,"="); p=scopy(p,v); *p++='\n'; *p=0; return p; }
static char* sf_kvn(char* p,const char* k,u64 v){ p=scopy(p,k); p=scopy(p,"="); char t[24]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]=(char)('0'+v%10); v/=10; } while(n)*p++=t[--n]; *p++='\n'; *p=0; return p; }
static void sf_clear(const char* dir){
    dt_init(); int d=dt_resolve_dir(dir); if(d<0)return;
    for(int k=0;k<FS_N;k++){ if(!FS[k].used||FS[k].dir!=d)continue; char p[160],*q=p; q=scopy(q,dir); q=scopy(q,"/"); q=scopy(q,FS[k].name); *q=0; k_unlink(p); } }
static char* sf_date(char* p,u32 ep){
    u32 days=ep/86400u, rem=ep%86400u; int y=1970; for(;;){ int ly=(y%4==0&&y%100!=0)||y%400==0; u32 dy=ly?366:365; if(days<dy)break; days-=dy; y++; }
    static const u8 ML[12]={31,28,31,30,31,30,31,31,30,31,30,31}; int m=0; for(;m<12;m++){ u32 ml=ML[m]+((m==1&&((y%4==0&&y%100!=0)||y%400==0))?1:0); if(days<ml)break; days-=ml; }
    p=udec(p,(u32)y); *p++='-'; p=u2(p,(u32)m+1); *p++='-'; p=u2(p,days+1); *p++=' '; p=u2(p,rem/3600); *p++=':'; p=u2(p,(rem/60)%60); p=scopy(p," UTC"); *p=0; return p; }

static char* sf_stamp(char* p,u32 ep){ char* d=sf_date(p,ep); d-=4; *d=':'; d++; d=u2(d,ep%60); *d=0; return d; }

static int sf_pci(int cls,int sub){ if(!pci_n)pci_scan(); for(int k=0;k<pci_n;k++) if(pci_cls[k]==cls&&(sub<0||pci_sub[k]==sub))return 1; return 0; }
static int sf_pciv(u16 ven,u16 dev){ if(!pci_n)pci_scan(); for(int k=0;k<pci_n;k++) if(pci_ven[k]==ven&&(!dev||pci_dev[k]==dev))return 1; return 0; }
static int sf_nic(const char* name){ return nic_present&&g_mnic.name&&sf_eq(g_mnic.name,name); }
static int sf_snd(const char* name){ return g_snd_state&&g_snd.name&&sf_eq(g_snd.name,name); }

typedef struct { const char* file; const char* name; const char* type; const char* src; const char* desc; } sf_drv;
static const sf_drv DRV[]={
    { "pci",        "PCI bus",                       "Bus",      "drivers/bus/pci.c",               "Finds every device on the PCI bus" },
    { "acpi",       "ACPI",                          "System",   "drivers/acpi/acpi_dev.c",         "Power, battery, lid and laptop buttons from the firmware tables" },
    { "battery",    "ACPI battery",                  "Power",    "drivers/acpi/laptop.c",           "Battery charge, health and power draw" },
    { "cmos_rtc",   "CMOS real-time clock",          "System",   "drivers/rtc/cmos.c",              "Date and time from the motherboard clock" },
    { "ahci",       "AHCI SATA",                     "Storage",  "drivers/storage/ahci.c",          "SATA hard disks and SSDs" },
    { "ata",        "ATA / IDE",                     "Storage",  "drivers/storage/ata.c",           "Older IDE disks (and virtual machines)" },
    { "nvme",       "NVMe",                          "Storage",  "drivers/storage/nvme.c",          "NVMe SSDs" },
    { "virtio_blk", "VirtIO disk",                   "Storage",  "drivers/virtio/virtio_blk.c",     "Disks in QEMU and KVM virtual machines" },
    { "xhci",       "USB 3 (xHCI)",                  "USB",      "drivers/usb/xhci.c",              "USB controller: keyboards, mice, sticks, cameras, sound" },
    { "usb_storage","USB storage",                   "USB",      "drivers/usb/xhci.c",              "USB sticks and USB disks" },
    { "e1000",      "Intel e1000",                   "Network",  "drivers/net/ethernet/e1000.c",    "Intel wired network (8254x)" },
    { "rtl8139",    "Realtek RTL8139",               "Network",  "drivers/net/ethernet/rtl8139.c",  "Realtek wired network (old)" },
    { "rtl8169",    "Realtek RTL8111/8168/8169",     "Network",  "drivers/net/ethernet/rtl8169.c",  "Realtek gigabit wired network" },
    { "virtio_net", "VirtIO network",                "Network",  "drivers/virtio/virtio_net.c",     "Network in QEMU and KVM virtual machines" },
    { "rtw8821ce",  "Realtek RTW8821CE Wi-Fi",       "Network",  "drivers/net/wifi/rtw8821ce.c",    "Wi-Fi" },
    { "hda",        "Intel HD Audio",                "Sound",    "drivers/sound/hda.c",             "Built-in speakers, headphones and microphone" },
    { "ac97",       "AC97 audio",                    "Sound",    "drivers/sound/ac97.c",            "Older sound cards (and virtual machines)" },
    { "framebuffer","Framebuffer",                   "Display",  "drivers/video/fb/framebuffer.c",  "Draws the screen through the firmware framebuffer" },
    { "igpu",       "Intel graphics (gen 9)",        "Display",  "drivers/video/intel/igpu.c",      "Intel GPU drawing" },
    { "nvidia",     "NVIDIA GPU (GSP-RM)",           "Display",  "drivers/video/nvidia/nvgsp.c",    "NVIDIA graphics cards" },
    { "ps2_kbd",    "PS/2 keyboard",                 "Input",    "drivers/input/ps2/ps2.c",         "Built-in laptop keyboards" },
    { "ps2_mouse",  "PS/2 mouse",                    "Input",    "drivers/input/ps2/ps2_mouse.c",   "PS/2 mice and simple touchpads" },
    { "synaptics",  "Synaptics touchpad",            "Input",    "drivers/input/ps2/syn_ps2.c",     "Synaptics touchpads over PS/2" },
    { "elan_ps2",   "ELAN touchpad (PS/2)",          "Input",    "drivers/input/ps2/elan_ps2.c",    "ELAN touchpads over PS/2" },
    { "i2c_hid",    "Precision touchpad (I2C HID)",  "Input",    "drivers/input/touchpad/i2c_hid.c","Modern laptop touchpads" },
    { "dw_i2c",     "DesignWare I2C",                "Bus",      "drivers/i2c/dw_i2c.c",            "I2C bus used by modern touchpads" },
    { "amd_gpio",   "AMD GPIO",                      "Bus",      "drivers/gpio/amd_gpio.c",         "Touchpad interrupt pin on AMD laptops" },
    { "vmmouse",    "Virtual machine mouse",         "Input",    "drivers/input/vm/vmmouse.c",      "Smooth mouse in VirtualBox and VMware" },
};
#define NDRV ((int)(sizeof(DRV)/sizeof(DRV[0])))


static int sf_drv_state(int i,const char** why){
    const char* f=DRV[i].file; *why="";
    if(sf_eq(f,"pci")||sf_eq(f,"acpi")||sf_eq(f,"cmos_rtc")||sf_eq(f,"framebuffer")||sf_eq(f,"ps2_kbd")||sf_eq(f,"ps2_mouse")){ *why="always loaded"; return 1; }
    if(sf_eq(f,"battery")){ *why=bat_present?"battery found":"no battery (desktop or virtual machine)"; return bat_present?1:0; }
    if(sf_eq(f,"ahci")){ if(ahci_ok){ *why="SATA disk in use"; return 1; } *why=sf_pci(0x01,0x06)?"SATA controller found, no disk":"no SATA controller"; return sf_pci(0x01,0x06)?2:0; }
    if(sf_eq(f,"ata")){ if(g_ata_drv>=0){ *why="IDE disk in use"; return 1; } *why=sf_pci(0x01,0x01)?"IDE controller found, no disk":"no IDE controller"; return sf_pci(0x01,0x01)?2:0; }
    if(sf_eq(f,"nvme")){ if(nvme_ok){ *why="NVMe disk in use"; return 1; } *why=nvme_found?"NVMe disk found but did not start":"no NVMe disk"; return nvme_found?2:0; }
    if(sf_eq(f,"virtio_blk")){ if(g_dktype==DK_VIRTIO){ *why="VirtIO disk in use"; return 1; } *why=sf_pciv(0x1AF4,0x1001)||sf_pciv(0x1AF4,0x1042)?"VirtIO disk found, not in use":"no VirtIO disk"; return (sf_pciv(0x1AF4,0x1001)||sf_pciv(0x1AF4,0x1042))?2:0; }
    if(sf_eq(f,"xhci")){ *why=xhci_present?"USB controller running":"no xHCI controller"; return xhci_present?1:(sf_pci(0x0C,0x03)?2:0); }
    if(sf_eq(f,"usb_storage")){ *why=msd_ok?"USB drive connected":"no USB drive"; return msd_ok?1:0; }
    if(sf_eq(f,"e1000")){ if(sf_nic("Intel e1000 (8254x)")){ *why="network adapter in use"; return 1; } *why="not in use"; return sf_pciv(0x8086,0x100E)?2:0; }
    if(sf_eq(f,"rtl8139")){ if(sf_nic("Realtek RTL8139")){ *why="network adapter in use"; return 1; } *why="not in use"; return sf_pciv(0x10EC,0x8139)?2:0; }
    if(sf_eq(f,"rtl8169")){ if(sf_nic("Realtek RTL8111/8168/8169")){ *why="network adapter in use"; return 1; } *why="not in use"; return (sf_pciv(0x10EC,0x8168)||sf_pciv(0x10EC,0x8169))?2:0; }
    if(sf_eq(f,"virtio_net")){ if(sf_nic("VirtIO network")){ *why="network adapter in use"; return 1; } *why="not in use"; return (sf_pciv(0x1AF4,0x1000)||sf_pciv(0x1AF4,0x1041))?2:0; }
    if(sf_eq(f,"rtw8821ce")){ int hw=sf_pciv(0x10EC,0xC821); if(g_wnd_up){ *why="connected"; return 1; } *why=hw?"Wi-Fi card found, not connected":"no Wi-Fi card"; return hw?2:0; }
    if(sf_eq(f,"hda")){ if(sf_snd("Intel HD Audio")){ *why="sound in use"; return 1; } *why=sf_pci(0x04,0x03)?"sound card found, driver not started":"no HD Audio device"; return sf_pci(0x04,0x03)?2:0; }
    if(sf_eq(f,"ac97")){ if(sf_snd("AC97")){ *why="sound in use"; return 1; } *why=sf_pci(0x04,0x01)?"sound card found, driver not started":"no AC97 device"; return sf_pci(0x04,0x01)?2:0; }
    if(sf_eq(f,"igpu")){ int hw=sf_pciv(0x8086,0)&&sf_pci(0x03,-1); extern int g_nvd_on; if(g_gfx==GFX_GPU&&!g_nvd_on){ *why="GPU drawing on"; return 1; } *why=hw?"Intel graphics found, drawing with the CPU":"no Intel graphics"; return hw?2:0; }
    if(sf_eq(f,"nvidia")){ int hw=sf_pciv(0x10DE,0); nvgsp_info_t ni; nvgsp_info(&ni); if(hw&&ni.state>=7&&!ni.off){ *why="running"; return 1; } *why=hw?"NVIDIA card found, firmware framebuffer only":"no NVIDIA card"; return hw?2:0; }
    if(sf_eq(f,"synaptics")){ *why=g_syn_ready?"touchpad in use":"no Synaptics touchpad"; return g_syn_ready?1:0; }
    if(sf_eq(f,"elan_ps2")){ *why=g_etp_ready?"touchpad in use":"no ELAN PS/2 touchpad"; return g_etp_ready?1:0; }
    if(sf_eq(f,"i2c_hid")){ *why=g_tp.ok?"touchpad in use":"no I2C touchpad"; return g_tp.ok?1:0; }
    if(sf_eq(f,"dw_i2c")){ *why=g_tp.ok?"used by the touchpad":"not needed"; return g_tp.ok?1:0; }
    if(sf_eq(f,"amd_gpio")){ *why=g_amdg_ok?"touchpad interrupt pin set up":"not needed"; return g_amdg_ok?1:0; }
    if(sf_eq(f,"vmmouse")){ *why=g_vm_kind?"virtual machine found":"physical machine"; return g_vm_kind?1:0; }
    return 0; }

static void sf_drivers(int clear){
    sf_dir("/System","Drivers"); if(clear)sf_clear("/System/Drivers");
    char* b=(char*)kmalloc(16384); if(!b)return; int nl=0;
    static char sum[4096]; char* s=sum; s=scopy(s,"Driver          State       Type      Name\n");
    for(int i=0;i<NDRV;i++){ const char* why; int st=sf_drv_state(i,&why); if(st==1)nl++;
        char* p=b; p=sf_kv(p,"Name",DRV[i].name); p=sf_kv(p,"Type",DRV[i].type);
        p=sf_kv(p,"State",st==1?"Loaded":st==2?"Not loaded (device found)":"Not loaded"); p=sf_kv(p,"Reason",why);
        if(st==1&&sf_eq(DRV[i].type,"Network")&&!sf_eq(DRV[i].file,"rtw8821ce")&&g_mnic.name)p=sf_kv(p,"Device",g_mnic.name);
        if(st==1&&sf_eq(DRV[i].type,"Sound")&&g_snd.name)p=sf_kv(p,"Device",g_snd.name);
        p=sf_kv(p,"Description",DRV[i].desc); p=sf_kv(p,"Source",DRV[i].src); p=sf_kv(p,"BuiltIn","yes (part of the kernel)");
        char path[96],*q=path; q=scopy(q,"/System/Drivers/"); q=scopy(q,DRV[i].file); q=scopy(q,".nvd"); *q=0; sf_write(path,b,(int)(p-b));
        int L=0; s=scopy(s,DRV[i].file); while(DRV[i].file[L])L++; for(;L<16;L++)*s++=' ';
        const char* sn=st==1?"loaded":st==2?"found":"-"; s=scopy(s,sn); L=0; while(sn[L])L++; for(;L<12;L++)*s++=' ';
        s=scopy(s,DRV[i].type); L=0; while(DRV[i].type[L])L++; for(;L<10;L++)*s++=' '; s=scopy(s,DRV[i].name); *s++='\n'; *s=0; }
    s=scopy(s,"\n"); s=udec(s,(u32)nl); s=scopy(s," of "); s=udec(s,(u32)NDRV); s=scopy(s," drivers loaded. \"found\" = the device is there but its driver is not running.\n"); *s=0;
    sf_writes("/System/Drivers/drivers.txt",sum);
    kfree(b); }

static void sf_services(int clear){
    sf_dir("/System","Services"); if(clear)sf_clear("/System/Services");
    static char sum[2048]; char* s=sum; s=scopy(s,"Background services (Task Manager > Processes, Settings > Services)\n\n");
    for(int i=0;i<svc_count();i++){ const char *nm,*ds,*st; int on,req; if(!svc_info(i,&nm,&ds,&st,&on,&req))continue;
        char b[600],*p=b; p=sf_kv(p,"Id",svc_id(i)); p=sf_kv(p,"Name",nm); p=sf_kv(p,"Description",ds); p=sf_kv(p,"Running",on?"yes":"no");
        p=sf_kv(p,"Status",st); p=sf_kv(p,"Required",req?"yes (cannot be stopped)":"no"); p=sf_kvn(p,"EveryTicks",svc_every(i));
        p=sf_kvn(p,"Runs",svc_runs(i)); p=sf_kvn(p,"CpuTimeMicroseconds",svc_us(i));
        char path[96],*q=path; q=scopy(q,"/System/Services/"); q=scopy(q,svc_id(i)); q=scopy(q,".nvs"); *q=0; sf_write(path,b,(int)(p-b));
        s=scopy(s,on?"  running  ":"  stopped  "); s=scopy(s,svc_id(i)); s=scopy(s,"  - "); s=scopy(s,nm); *s++='\n'; *s=0; }
    s=scopy(s,"\nStart or stop: Task Manager, Settings > Services, or the \"svc\" command.\n"); *s=0;
    sf_writes("/System/Services/services.txt",sum); }

static void sf_font(const char* file,const char* name,const char* fam,const char* sizes,const char* use,const char* src){
    char b[600],*p=b; p=sf_kv(p,"Name",name); p=sf_kv(p,"Family",fam); p=sf_kv(p,"Sizes",sizes); p=sf_kv(p,"UsedFor",use); p=sf_kv(p,"Source",src); p=sf_kv(p,"BuiltIn","yes (part of the kernel)");
    char path[96],*q=path; q=scopy(q,"/System/Fonts/"); q=scopy(q,file); *q=0; sf_write(path,b,(int)(p-b)); }
static void sf_fonts(void){
    sf_dir("/System","Fonts"); sf_clear("/System/Fonts");
    sf_font("Inter.nvo","Inter Regular","Inter","13, 15, 18 px","Window text, menus, Settings","assets/uifont.c (tools/mkuifont.py)");
    sf_font("Inter-Medium.nvo","Inter Medium","Inter","15 px","Buttons and titles","assets/uifont.c");
    sf_font("Inter-SemiBold.nvo","Inter SemiBold","Inter","24, 26, 40 px","Headings, sign-in screen","assets/uifont.c");
    sf_font("Inter-Light.nvo","Inter Light","Inter","84 px","Lock screen clock","assets/uifont.c");
    sf_font("NoovexSmooth.nvo","Noovex Smooth","Noovex","16 px (anti-aliased)","Terminal and older apps","assets/fontaa.c");
    sf_font("Noovex8x14.nvo","Noovex 8x14","Noovex","8x14 px bitmap","Boot screen, crash screen, text mode","assets/font.c");
    sf_font("LiberationSans.nvo","Liberation Sans","Liberation (Arial size)","8-48 pt, regular/bold/italic","Noovex Writer","assets/wpfont.c");
    sf_font("LiberationSerif.nvo","Liberation Serif","Liberation (Times size)","8-48 pt, regular/bold/italic","Noovex Writer","assets/wpfont.c");
    sf_font("LiberationMono.nvo","Liberation Mono","Liberation (Courier size)","8-48 pt, regular/bold/italic","Noovex Writer, Editor","assets/wpfont.c");
    sf_font("Emoji.nvo","Noovex Emoji","Emoji","32 px colour","Emoji in text and the browser","assets/emoji32.bin"); }

static void sf_config(void){
    sf_dir("/System","Config"); sf_clear("/System/Config");
    char nm[80]; int t=0; int nh=0;
    int su=g_uid; g_uid=0;
    for(int i=0;i<64&&k_readdir_at("/etc/noovex/registry",i,nm,&t)==1;i++){ int n=0; while(nm[n])n++; if(t==4||n<5||!sf_eq(nm+n-4,".reg"))continue;
        char tg[120],*q=tg; q=scopy(q,"/etc/noovex/registry/"); q=scopy(q,nm); *q=0;
        char ln[120]; q=ln; q=scopy(q,"/System/Config/"); if(sf_eq(nm,"system.reg"))q=scopy(q,"SYSTEM.nvr"); else { for(int k=0;k<n-4;k++)*q++=nm[k]; q=scopy(q,".nvr"); } *q=0;
        ux_symlink(tg,ln); nh++; }
    g_uid=su;
    char b[700],*p=b; p=scopy(p,"Registry files (hives)\n\n  SYSTEM.nvr       -> /etc/noovex/registry/system.reg  (settings for every account)\n  user-<name>.nvr  -> /etc/noovex/registry/user-<name>.reg  (one per account)\nDouble-click a .nvr file to open Registry Editor.\n\n");
    p=udec(p,(u32)nh); p=scopy(p," hive files found. Open them with Registry Editor (Win+Shift+R) or the \"reg\" command.\nEdit the registry with Registry Editor, not by hand: it checks every value.\n"); *p=0;
    sf_writes("/System/Config/README.txt",b); }

static void sf_kernel(void){
    sf_dir("/System","Kernel");
    static char b[2048]; char *p=b; char t[80],*q;
    q=t; q=scopy(q,"NoovexOS 64 v"); *q=0; { int L=(int)(q-t); for(int i=0;i<15&&g_buildsig.version[i]&&L<70;i++)t[L++]=g_buildsig.version[i]; t[L]=0; } p=sf_kv(p,"Kernel",t);
    q=t; q=sf_date(q,g_buildsig.build_epoch); p=sf_kv(p,"Built",t);
    p=sf_kvn(p,"ImageBytes",g_buildsig.image_size);
    p=scopy(p,"SHA256="); for(int i=0;i<32;i++)p=hx(p,g_buildsig.sha256[i],2); *p++='\n'; *p=0;
    p=sf_kv(p,"Architecture","x86-64");
    p=sf_kv(p,"Boot",g_uefi_ram_mb?"UEFI":"Legacy BIOS");
    p=sf_kv(p,"Processor",g_brand[0]?g_brand:"unknown");
    p=sf_kvn(p,"Cores",(u64)(g_ncpu>0?g_ncpu:1)); p=sf_kvn(p,"CoresOnline",(u64)g_cores_online);
    p=sf_kvn(p,"MemoryMB",g_ram_mb); p=sf_kvn(p,"KernelHeapUsedKB",(u64)heap_used()/1024ULL);
    p=sf_kv(p,"Screen",""); p--; p=udec(p,W); *p++='x'; p=udec(p,H); *p++='x'; p=udec(p,BPP); *p++='\n'; *p=0;
    p=sf_kv(p,"Graphics",g_gfx==GFX_GPU?"GPU":g_gfx==GFX_EMU?"CPU (GPU emulated)":"CPU");
    p=sf_kv(p,"VirtualMachine",g_vm_name?g_vm_name:"none");
    p=sf_kv(p,"ComputerName",g_hostname);
    p=sf_kvn(p,"TimerHz",100);
    p=sf_kvn(p,"MovedFirmwarePages",g_remap_n); p=sf_kvn(p,"RestoredFirmwareBytes",g_remap_restored);
    if(g_remap_n){ p=scopy(p,"MovedRange="); p=uhex(p,g_remap_lo,9); p=scopy(p," - "); p=uhex(p,g_remap_hi,9); *p++='\n'; *p=0; }
    p=sf_kvn(p,"UptimeSecondsAtRefresh",g_ticks/100);
    p=sf_kvn(p,"LogLinesSinceStart",g_sf_lines);
    sf_write("/System/Kernel/kernel.nvi",b,(int)(p-b)); }


static void sf_kfile(void){
    char b[1024],*p=b; char t[80],*q;
    p=scopy(p,"NVK1 NoovexOS kernel\n");
    q=t; q=scopy(q,"NoovexOS 64 v"); *q=0; { int L=(int)(q-t); for(int i=0;i<15&&g_buildsig.version[i]&&L<70;i++)t[L++]=g_buildsig.version[i]; t[L]=0; } p=sf_kv(p,"Kernel",t);
    q=t; q=sf_date(q,g_buildsig.build_epoch); p=sf_kv(p,"Built",t);
    p=sf_kvn(p,"ImageBytes",g_buildsig.image_size);
    p=scopy(p,"SHA256="); for(int i=0;i<32;i++)p=hx(p,g_buildsig.sha256[i],2); *p++='\n'; *p=0;
    p=scopy(p,"UefiEntry="); p=uhex(p,g_buildsig.uefi_entry,9); *p++='\n'; *p=0;
    p=sf_kv(p,"Signature","build signature (checked at every start)");
    p=sf_kv(p,"Image","/boot/kernel64.bin on the USB stick or disk");
    p=sf_kv(p,"Format","ELF64 x86-64, loaded at 0x100000");
    sf_write("/System/Kernel/noovex.nvk",b,(int)(p-b)); }

typedef struct { const char* file; const char* name; const char* desc; const char* src[3]; const char* drv[3]; } sf_mod;
static const sf_mod MODS[]={
    { "net",    "Network module", "Wired network cards", { "drivers/net/ethernet/e1000.c", "drivers/net/ethernet/rtl8139.c", "drivers/net/ethernet/rtl8169.c" }, { "e1000", "rtl8139", "rtl8169" } },
    { "virtio", "VirtIO module",  "Disks and network in QEMU and KVM virtual machines", { "drivers/virtio/virtio.c", "drivers/virtio/virtio_net.c", "drivers/virtio/virtio_blk.c" }, { "virtio_net", "virtio_blk", 0 } },
    { "sound",  "Sound module",   "Sound cards", { "drivers/sound/hda.c", "drivers/sound/ac97.c", 0 }, { "hda", "ac97", 0 } },
};
static int sf_drv_index(const char* f){ for(int i=0;i<NDRV;i++) if(sf_eq(DRV[i].file,f))return i; return -1; }
static void sf_modules(void){
    sf_dir("/System","Modules");
    static char sum[1600]; char* s=sum; s=scopy(s,"Kernel modules (built separately, linked into the kernel image)\n\n");
    for(int i=0;i<(int)(sizeof(MODS)/sizeof(MODS[0]));i++){ int any=0, found=0;
        char b[900],*p=b; p=sf_kv(p,"Name",MODS[i].name); p=sf_kv(p,"Description",MODS[i].desc);
        for(int k=0;k<3&&MODS[i].drv[k];k++){ const char* why; int di=sf_drv_index(MODS[i].drv[k]); int st=di>=0?sf_drv_state(di,&why):0; if(st==1)any=1; if(st==2)found=1;
            p=scopy(p,"Driver="); p=scopy(p,MODS[i].drv[k]); p=scopy(p,".nvd  "); p=scopy(p,st==1?"loaded":st==2?"device found, not loaded":"not loaded"); *p++='\n'; *p=0; }
        p=sf_kv(p,"State",any?"Loaded (in use)":found?"Not in use (device found)":"Not in use");
        for(int k=0;k<3&&MODS[i].src[k];k++)p=sf_kv(p,"Source",MODS[i].src[k]);
        char path[96],*q=path; q=scopy(q,"/System/Modules/"); q=scopy(q,MODS[i].file); q=scopy(q,".nvm"); *q=0; sf_write(path,b,(int)(p-b));
        s=scopy(s,any?"  in use      ":"  not in use  "); s=scopy(s,MODS[i].file); s=scopy(s,".nvm  - "); s=scopy(s,MODS[i].name); *s++='\n'; *s=0; }
    sf_writes("/System/Modules/modules.txt",sum); }

static int sf_is_so(const char* n){ for(int i=0;n[i];i++) if(n[i]=='.'&&n[i+1]=='s'&&n[i+2]=='o'&&(n[i+3]==0||n[i+3]=='.'))return 1; return 0; }
static void sf_libraries(void){
    sf_dir("/System","Libraries"); sf_clear("/System/Libraries");
    static const char* const LD[]={"/lib/x86_64-linux-gnu","/usr/lib/x86_64-linux-gnu","/lib64","/usr/lib","/usr/local/lib"};
    char* sum=(char*)kmalloc(65536); if(!sum)return; char* s=sum; char* se=sum+65000; s=scopy(s,"Shared libraries (one .nvl file each)\n\n"); int nl=0; u64 tot=0;
    extern int sysroot_list(const char*,int,char*,int,int*); extern int sysroot_fsize(const char*); extern int sysroot_islink(const char*);
    for(int d=0;d<5;d++){ char nm[128]; int isd=0;
        for(int i=0;i<8192&&nl<600&&sysroot_list(LD[d],i,nm,128,&isd)==1;i++){ if(isd||!sf_is_so(nm))continue;
            char full[200],*q=full; q=scopy(q,LD[d]); q=scopy(q,"/"); q=scopy(q,nm); *q=0;
            int lnk=sysroot_islink(full); int fsz=lnk?0:sysroot_fsize(full); u32 sz=fsz>0?(u32)fsz:0;
            char b[400],*p=b; p=sf_kv(p,"Name",nm); p=sf_kv(p,"Path",full); p=sf_kvn(p,"Bytes",sz); p=sf_kv(p,"Kind",lnk?"link to another version":"Linux shared library (ELF64)");
            char path[160]; q=path; q=scopy(q,"/System/Libraries/"); int L=0; while(nm[L]&&L<55){ *q++=nm[L]; L++; } q=scopy(q,".nvl"); *q=0; sf_write(path,b,(int)(p-b));
            if(s<se){ s=scopy(s,"  "); s=scopy(s,full); s=scopy(s,"  "); s=udec(s,sz); s=scopy(s," bytes\n"); }
            nl++; tot+=sz; } }
    s=scopy(s,"\n"); s=udec(s,(u32)nl); s=scopy(s," libraries, "); s=udec(s,(u32)(tot/1024)); s=scopy(s," KB\n"); *s=0;
    sf_writes("/System/Libraries/libraries.txt",sum); kfree(sum); }

static const char NVX_HELLO[]="int main(){\n    printf(\"Hello from NoovexOS!\\n\");\n    return 0;\n}\n";
static const char NVX_TABLE[]="int main(){\n    printf(\"Times table\\n\");\n    for(int i=1;i<=10;i++){\n        printf(\"%d x 7 = %d\\n\", i, i*7);\n    }\n    return 0;\n}\n";
static const char NVX_PRIMES[]="int main(){\n    printf(\"Prime numbers below 100:\\n\");\n    for(int n=2;n<100;n++){\n        int p=1;\n        for(int d=2;d*d<=n;d++){ if(n%d==0){ p=0; } }\n        if(p){ printf(\"%d \", n); }\n    }\n    printf(\"\\n\");\n    return 0;\n}\n";
static void sf_programs(void){
    sf_dir("/System","Programs");
    sf_writes("/System/Programs/hello.nvx",NVX_HELLO); sf_writes("/System/Programs/table.nvx",NVX_TABLE); sf_writes("/System/Programs/primes.nvx",NVX_PRIMES);
    sf_writes("/System/Programs/README.txt","NoovexOS programs (.nvx). Double-click one in Files: it opens in Editor and runs.\nCopy one to your home folder to change it.\n"); }


static void sf_lower(char* o,const char* s,int cap){ int n=0; for(int i=0;s[i]&&n<cap-1;i++){ char c=s[i]; if(c==' '||c=='/'||c=='('||c==')')continue; if(c>='A'&&c<='Z')c+=32; o[n++]=c; } o[n]=0; }
static void sf_apps(void){
    sf_dir("/System","Apps"); sf_clear("/System/Apps");
    int n=progfiles_count(); static char sum[3000]; char* s=sum; s=scopy(s,"Built-in apps. Double-click a .nva file to open the app.\n\n");
    for(int i=0;i<n;i++){ const pf_app* a=progfiles_get(i); if(!a||a->kind!=PF_BUILTIN||a->win<0)continue;
        char fn[48]; sf_lower(fn,a->cmd[0]?a->cmd:a->folder,40);
        char b[600],*p=b; p=scopy(p,"NVA1\n"); p=sf_kv(p,"Name",a->name); p=sf_kvn(p,"Window",(u64)a->win); p=sf_kv(p,"Command",a->cmd[0]?a->cmd:"(Start menu)"); p=sf_kv(p,"Description",a->desc); p=sf_kv(p,"Version",a->ver); if(a->hidden)p=sf_kv(p,"Hidden","yes");
        char path[96],*q=path; q=scopy(q,"/System/Apps/"); q=scopy(q,fn); q=scopy(q,".nva"); *q=0; sf_write(path,b,(int)(p-b));
        if(s-sum<2900){ s=scopy(s,"  "); s=scopy(s,fn); s=scopy(s,".nva  - "); s=scopy(s,a->name); *s++='\n'; *s=0; } }
    sf_writes("/System/Apps/apps.txt",sum); }

static const char* const HWF[11]={"cpu","gpu","memory","power","storage","network","sound","usb","display","input","pci"};
static void sf_hardware(int clear){
    sf_dir("/System","Hardware"); if(clear)sf_clear("/System/Hardware");
    char* b=(char*)kmalloc(8192); if(!b)return;
    for(int c=0;c<11;c++){ char* p=b; p=scopy(p,"NVH1 "); p=scopy(p,taskmgr_devcat(c)); p=scopy(p,"\n\n"); p+=taskmgr_devdump(c,p,8000-(int)(p-b));
        char path[96],*q=path; q=scopy(q,"/System/Hardware/"); q=scopy(q,HWF[c]); q=scopy(q,".nvh"); *q=0; sf_write(path,b,(int)(p-b)); }
    { extern int camera_info_text(char*,int) __attribute__((weak)); char* p=b; p=scopy(p,"NVH1 Camera\n\n"); if(camera_info_text)p+=camera_info_text(p,7800); else p=scopy(p,"No camera information\n"); sf_write("/System/Hardware/camera.nvh",b,(int)(p-b)); }
    { extern int wifi_info_text(char*,int); char* p=b; p=scopy(p,"NVH1 Wi-Fi\n\n"); p+=wifi_info_text(p,7800); sf_write("/System/Hardware/wifi.nvh",b,(int)(p-b)); }
    { extern int mic_info_text(char*,int) __attribute__((weak)); char* p=b; p=scopy(p,"NVH1 Microphone\n\n"); if(mic_info_text)p+=mic_info_text(p,7800); else p=scopy(p,"No microphone information\n"); sf_write("/System/Hardware/microphone.nvh",b,(int)(p-b)); }
    kfree(b); }

static void sf_crash(void){
    sf_dir("/System","Crash");
    int k=fs_find_name("crash-report.txt"); if(k<0||!FS[k].used||FS[k].len<=0)return;
    char nm[64],*q=nm; char d[32]; sf_date(d,FS[k].mtime); for(int i=0;d[i]&&i<16;i++){ char c=d[i]; if(c==' ')c='-'; if(c==':')continue; *q++=c; } *q=0; q=scopy(q,".nvq"); *q=0;
    char path[96]; q=path; q=scopy(q,"/System/Crash/"); q=scopy(q,nm); *q=0;
    int n=FS[k].len; char* b=(char*)kmalloc((unsigned long)n+64); if(!b)return; char* p=b; p=scopy(p,"NVQ1 crash report\n"); const char* src=fsd(k); if(src){ for(int i=0;i<n;i++)*p++=src[i]; }
    sf_write(path,b,(int)(p-b)); kfree(b); }

static void sf_boot(void){
    sf_dir("/System","Boot");
    char* b=(char*)kmalloc(24576); if(!b)return; char* p=b; char* e=b+24000;
    p=scopy(p,"NVB1 NoovexOS boot\n");
    p=sf_kv(p,"Firmware",g_uefi_ram_mb?"UEFI":"Legacy BIOS");
    p=sf_kv(p,"Loader",g_uefi_ram_mb?"NoovexOS UEFI loader (EFI/BOOT/BOOTX64.EFI)":"myboot (boot sector + stage 2)");
    p=sf_kv(p,"Kernel","/boot/kernel64.bin  (see /System/Kernel/noovex.nvk)");
    p=sf_kv(p,"Config","/boot.cfg on the USB stick or disk");
    p=sf_kv(p,"KernelEntry","0x100000 (long mode)");
    u64 ml=0; int nsteps=0;
    while(__sync_lock_test_and_set(&g_sf_lock,1)){}
    { int i=0; while(i<g_sf_logn&&p<e-300){ int x=i; while(x<g_sf_logn&&g_sf_log[x]!='\n')x++; int a=i; u64 tk=0; if(g_sf_log[a]=='['){ a++; while(a<x&&g_sf_log[a]>='0'&&g_sf_log[a]<='9'){ tk=tk*10+(u64)(g_sf_log[a]-'0'); a++; } a+=2; }
        if(a<x&&sf_pre(g_sf_log+a,x-a,"boot")){ if(!nsteps)p=scopy(p,"\nBoot steps (in order; times in seconds once the timer runs)\n"); nsteps++; if(tk){ p=udec(p,(u32)(tk/100)); *p++='.'; p=u2(p,(u32)(tk%100)); } else { p=udec(p,(u32)nsteps); *p++='.'; } p=scopy(p,"  "); for(int k=a;k<x&&p<e-2;k++)*p++=g_sf_log[k]; *p++='\n'; *p=0;
            if(sf_has(g_sf_log+a,x-a,"main loop"))ml=tk; }
        i=x+1; } }
    __sync_lock_release(&g_sf_lock);
    if(ml){ p=scopy(p,"\nMainLoopSecondsAfterTimerStart="); p=udec(p,(u32)(ml/100)); *p++='.'; p=u2(p,(u32)(ml%100)); *p++='\n'; *p=0; }
    sf_write("/System/Boot/boot.nvb",b,(int)(p-b)); kfree(b); }

static const short SF_SIN[256]={0,804,1608,2410,3212,4011,4808,5602,6393,7179,7962,8739,9512,10278,11039,11793,12539,13279,14010,14732,15446,16151,16846,17530,18204,18868,19519,20159,20787,21403,22005,22594,23170,23731,24279,24811,25329,25832,26319,26790,27245,27683,28105,28510,28898,29268,29621,29956,30273,30571,30852,31113,31356,31580,31785,31971,32137,32285,32412,32521,32609,32678,32728,32757,32767,32757,32728,32678,32609,32521,32412,32285,32137,31971,31785,31580,31356,31113,30852,30571,30273,29956,29621,29268,28898,28510,28105,27683,27245,26790,26319,25832,25329,24811,24279,23731,23170,22594,22005,21403,20787,20159,19519,18868,18204,17530,16846,16151,15446,14732,14010,13279,12539,11793,11039,10278,9512,8739,7962,7179,6393,5602,4808,4011,3212,2410,1608,804,0,-804,-1608,-2410,-3212,-4011,-4808,-5602,-6393,-7179,-7962,-8739,-9512,-10278,-11039,-11793,-12539,-13279,-14010,-14732,-15446,-16151,-16846,-17530,-18204,-18868,-19519,-20159,-20787,-21403,-22005,-22594,-23170,-23731,-24279,-24811,-25329,-25832,-26319,-26790,-27245,-27683,-28105,-28510,-28898,-29268,-29621,-29956,-30273,-30571,-30852,-31113,-31356,-31580,-31785,-31971,-32137,-32285,-32412,-32521,-32609,-32678,-32728,-32757,-32767,-32757,-32728,-32678,-32609,-32521,-32412,-32285,-32137,-31971,-31785,-31580,-31356,-31113,-30852,-30571,-30273,-29956,-29621,-29268,-28898,-28510,-28105,-27683,-27245,-26790,-26319,-25832,-25329,-24811,-24279,-23731,-23170,-22594,-22005,-21403,-20787,-20159,-19519,-18868,-18204,-17530,-16846,-16151,-15446,-14732,-14010,-13279,-12539,-11793,-11039,-10278,-9512,-8739,-7962,-7179,-6393,-5602,-4808,-4011,-3212,-2410,-1608,-804};
typedef struct { u16 hz, ms; } sf_note;
static int sf_wav(char* o,const sf_note* nt,int nn){
    const u32 R=22050; u32 tot=0; for(int i=0;i<nn;i++)tot+=R*nt[i].ms/1000; u32 db=tot*2;
    u8* h=(u8*)o; const char* hd="RIFF"; for(int i=0;i<4;i++)h[i]=(u8)hd[i]; u32 v=36+db; for(int i=0;i<4;i++)h[4+i]=(u8)(v>>(8*i)); hd="WAVEfmt "; for(int i=0;i<8;i++)h[8+i]=(u8)hd[i];
    u32 f[4]={16,0x00010001u,R,R*2}; for(int k=0;k<4;k++) for(int i=0;i<4;i++)h[16+k*4+i]=(u8)(f[k]>>(8*i)); h[32]=2; h[33]=0; h[34]=16; h[35]=0; hd="data"; for(int i=0;i<4;i++)h[36+i]=(u8)hd[i]; for(int i=0;i<4;i++)h[40+i]=(u8)(db>>(8*i));
    short* out=(short*)(o+44); u32 w=0;
    for(int i=0;i<nn;i++){ u32 n=R*nt[i].ms/1000; u32 ph=0, inc=nt[i].hz?(u32)(((u64)nt[i].hz<<24)/R):0; u32 at=n/12+1;
        for(u32 j=0;j<n;j++){ int a=j<at?(int)(j*1000/at):(int)((n-j)*1000/(n-at+1)); int sm=nt[i].hz?SF_SIN[(ph>>16)&255]:0; sm=sm*a/1000*7/10; out[w++]=(short)sm; ph+=inc; } }
    return 44+(int)db; }
static void sf_sound(const char* nm,const sf_note* nt,int nn){ char* b=(char*)kmalloc(96*1024); if(!b)return; int n=sf_wav(b,nt,nn); char path[96],*q=path; q=scopy(q,"/System/Media/"); q=scopy(q,nm); q=scopy(q,".nvn"); *q=0; sf_write(path,b,n); kfree(b); }
static void sf_media(void){
    sf_dir("/System","Media");
    static const sf_note ST[]={{523,140},{659,140},{784,140},{1047,420}}; sf_sound("startup",ST,4);
    static const sf_note NO[]={{880,110},{1319,220}}; sf_sound("notify",NO,2);
    static const sf_note ER[]={{330,160},{247,320}}; sf_sound("error",ER,2);
    static const sf_note UL[]={{659,90},{988,200}}; sf_sound("unlock",UL,2);
    static const sf_note LK[]={{988,90},{659,200}}; sf_sound("lock",LK,2);
    static const sf_note UI[]={{740,80},{0,30},{1109,140}}; sf_sound("device-in",UI,3);
    static const sf_note UO[]={{1109,80},{0,30},{740,140}}; sf_sound("device-out",UO,3);
    static const sf_note SH[]={{784,140},{659,140},{523,360}}; sf_sound("shutdown",SH,3);
    sf_writes("/System/Media/README.txt","System sounds (.nvn = WAV sound). Double-click one to play it.\n"); }

typedef struct { const char* file; const char* name; int wall, accent; const char* desc; } sf_theme;
static const sf_theme THM[]={
    { "dark", "Dark", 0, 0, "Porsche wallpaper, blue accent" },
    { "night", "Night", 4, 1, "Earth wallpaper, purple accent" },
    { "ocean", "Ocean", 1, 0, "Crane wallpaper, blue accent" },
    { "forest", "Forest", 5, 2, "Yosemite wallpaper, green accent" },
    { "sunset", "Sunset", 3, 3, "Peaks wallpaper, orange accent" },
    { "neon", "Neon", 2, 4, "Wireframe wallpaper, pink accent" },
};
static void sf_themes(void){
    sf_dir("/System","Themes");
    for(int i=0;i<(int)(sizeof(THM)/sizeof(THM[0]));i++){ char b[300],*p=b; p=scopy(p,"NVW1\n"); p=sf_kv(p,"Name",THM[i].name); p=sf_kvn(p,"Wallpaper",(u64)THM[i].wall); p=sf_kvn(p,"Accent",(u64)THM[i].accent); p=sf_kv(p,"Description",THM[i].desc);
        char path[96],*q=path; q=scopy(q,"/System/Themes/"); q=scopy(q,THM[i].file); q=scopy(q,".nvw"); *q=0; sf_write(path,b,(int)(p-b)); }
    sf_writes("/System/Themes/README.txt","Themes (.nvw). Double-click one to use it (administrator).\n"); }

#define SF_RDIR "/var/lib/noovex/restore"
static void sf_mkpath(const char* path){ dt_init(); int d=0; char c[64]; int n=0; for(int i=1;;i++){ char ch=path[i]; if(ch=='/'||!ch){ c[n]=0; if(n){ int x=dt_child(d,c); if(x<0){ x=dt_mkdir_at(d,c); if(x>=0){ DT[x].uid=0; DT[x].gid=0; DT[x].mode=0040700u; } } if(x<0)return; d=x; } n=0; if(!ch)break; } else if(n<63)c[n++]=ch; } dt_save(); }
static int sf_restore_create(const char* why){
    sf_mkpath(SF_RDIR);
    char day[32]; sf_date(day,ux_now()); day[10]=0;
    char path[96],*q=path; q=scopy(q,SF_RDIR "/"); q=scopy(q,day); q=scopy(q,".nvz"); *q=0;
    char* b=(char*)kmalloc(65536); if(!b)return -1; char* p=b; char* e=b+64000;
    p=scopy(p,"NVZ1 NoovexOS restore point\n"); p=scopy(p,"Created="); p=sf_stamp(p,ux_now()); *p++='\n'; *p=0; p=sf_kv(p,"Reason",why); p=sf_kv(p,"Contains","registry (all hives)");
    char nm[80]; int t=0;
    for(int i=0;i<64&&k_readdir_at("/etc/noovex/registry",i,nm,&t)==1;i++){ int n=0; while(nm[n])n++; if(t==4||n<5||!sf_eq(nm+n-4,".reg"))continue;
        char fp[128],*f=fp; f=scopy(f,"/etc/noovex/registry/"); f=scopy(f,nm); *f=0;
        if(e-p<2000)break; p=scopy(p,"== "); p=scopy(p,fp); p=scopy(p," ==\n"); int r=ux_read_file(fp,p,(int)(e-p)-200); if(r>0)p+=r; if(p[-1]!='\n')*p++='\n'; *p=0; }
    p=scopy(p,"== end ==\n");
    ux_write_text(path,b,(int)(p-b),0100600u); kfree(b);
    char names[32][24]; int nn=0;
    for(int i=0;i<256&&k_readdir_at(SF_RDIR,i,nm,&t)==1;i++){ if(t==4)continue; if(nn<32){ int k=0; while(nm[k]&&k<23){ names[nn][k]=nm[k]; k++; } names[nn][k]=0; nn++; } }
    while(nn>10){ int o=0; for(int i=1;i<nn;i++){ int k=0; while(names[i][k]&&names[i][k]==names[o][k])k++; if((u8)names[i][k]<(u8)names[o][k])o=i; } char dp[96],*w=dp; w=scopy(w,SF_RDIR "/"); w=scopy(w,names[o]); *w=0; k_unlink(dp); for(int i=o;i<nn-1;i++) for(int k=0;k<24;k++)names[i][k]=names[i+1][k]; nn--; }
    return 0; }
static void sf_restore_list(void){
    sf_dir("/System","Restore"); sf_clear("/System/Restore");
    char nm[80]; int t=0; int have=0; char day[32]; sf_date(day,ux_now()); day[10]=0; char want[40],*w=want; w=scopy(w,day); w=scopy(w,".nvz"); *w=0;
    for(int i=0;i<256&&k_readdir_at(SF_RDIR,i,nm,&t)==1;i++) if(t!=4&&sf_eq(nm,want))have=1;
    if(!have)sf_restore_create("daily restore point");
    char* b=(char*)kmalloc(65536); if(!b)return;
    for(int i=0;i<256&&k_readdir_at(SF_RDIR,i,nm,&t)==1;i++){ if(t==4)continue; char fp[128],*f=fp; f=scopy(f,SF_RDIR "/"); f=scopy(f,nm); *f=0; int r=ux_read_file(fp,b,65535); if(r<=0)continue;
        char dp[128]; f=dp; f=scopy(f,"/System/Restore/"); f=scopy(f,nm); *f=0; sf_write(dp,b,r); { int isd=0; int fk=ux_lookup(dp,&isd,0); if(fk>=0&&!isd)FS[fk].mode=0100600u; } }
    kfree(b);
    sf_writes("/System/Restore/README.txt","Restore points (.nvz): the registry, saved once a day (the last 10 are kept).\nList:     restore\nGo back:  sudo restore 2026-11-02\nMake one: sudo restore create\n"); }
int sysfiles_restore(const char* arg,char* msg,int cap){
    char* m=msg; m[0]=0;
    if(!arg[0]){ char nm[80]; int t=0; int n=0; m=scopy(m,"Restore points:\n"); for(int i=0;i<256&&k_readdir_at(SF_RDIR,i,nm,&t)==1;i++){ if(t==4)continue; if(m-msg<cap-80){ m=scopy(m,"  "); m=scopy(m,nm); *m++='\n'; *m=0; } n++; } if(!n)m=scopy(m,"  (none yet)\n"); return 0; }
    if(g_uid!=0){ scopy(m,"restore: only root can do that (try sudo)\n"); return -1; }
    if(sf_eq(arg,"create")){ int r=sf_restore_create("made with restore create"); sf_restore_list(); scopy(m,r==0?"restore: restore point made for today\n":"restore: could not make a restore point\n"); return r; }
    char fp[128],*f=fp; f=scopy(f,SF_RDIR "/"); int L=0; while(arg[L]&&L<40){ *f++=arg[L]; L++; } if(!(L>4&&sf_eq(arg+L-4,".nvz")))f=scopy(f,".nvz"); *f=0;
    char* b=(char*)kmalloc(65536); if(!b){ scopy(m,"restore: out of memory\n"); return -1; } int r=ux_read_file(fp,b,65535); if(r<=0){ kfree(b); scopy(m,"restore: no such restore point (type restore to see the list)\n"); return -1; } b[r]=0;
    sf_restore_create("before going back");
    int nf=0; char* p=b;
    while(*p){ char* ln=p; while(*p&&*p!='\n')p++; int ll=(int)(p-ln); if(*p)p++;
        if(ll>6&&ln[0]=='='&&ln[1]=='='&&ln[2]==' '){ char path[128]; int k=0; for(int i=3;i<ll-3&&k<127;i++)path[k++]=ln[i]; path[k]=0; if(sf_eq(path,"end"))break;
            if(!sf_pre(path,k,"/etc/noovex/registry/"))continue; char* st=p; while(*p&&!(p[0]=='='&&p[1]=='='&&p[2]==' '))p++; ux_write_text(path,st,(int)(p-st),0100644u); nf++; } }
    kfree(b); reg_load(1); { extern void build_bg(void); build_bg(); }
    m=scopy(m,"restore: went back to "); m=scopy(m,arg); m=scopy(m," ("); m=udec(m,(u32)nf); m=scopy(m," registry files). A restore point of the state before was made first.\n"); *m=0; return 0; }

static int sf_val(const char* b,int n,const char* key){ int L=0; while(key[L])L++; for(int i=0;i+L<n;i++){ if((i==0||b[i-1]=='\n')&&sf_pre(b+i,n-i,key)&&b[i+L]=='='){ int v=0,j=i+L+1; while(j<n&&b[j]>='0'&&b[j]<='9'){ v=v*10+(b[j]-'0'); j++; } return v; } } return -1; }
int sysfiles_open(int k){
    if(k<0||k>=FS_N||!FS[k].used)return 0; const char* nm=FS[k].name; int L=0; while(nm[L])L++; if(L<5||nm[L-4]!='.')return 0; const char* e=nm+L-3;
    const char* d=fsd(k); int n=FS[k].len;
    if(sf_eq(e,"nva")){ int w=d?sf_val(d,n,"Window"):-1; if(w>=0&&w<NWIN){ open_win(w); return 1; } toast("This app file is damaged"); return 1; }
    if(sf_eq(e,"nvr")){ open_win(W_CHATGPT); return 1; }
    if(sf_eq(e,"nvw")){ int wl=d?sf_val(d,n,"Wallpaper"):-1, ac=d?sf_val(d,n,"Accent"):-1; char v[8],*q;
        if(wl>=0){ q=v; q=udec(q,(u32)wl); *q=0; if(reg_set("System\\Display","Theme",v)<0){ toast("Only an administrator can change the theme"); return 1; } }
        if(ac>=0){ q=v; q=udec(q,(u32)ac); *q=0; reg_set("System\\Display","Accent",v); }
        g_wallmode=0; build_bg(); toast("Theme applied"); return 1; }
    return 0; }

static const char README[]=
"NoovexOS System folder\n"
"======================\n\n"
"The kernel makes this folder every time NoovexOS starts. It shows what the\n"
"system is running right now. Everything here belongs to root: only an\n"
"administrator (root) can change it.\n\n"
"  Kernel/     noovex.nvk (the kernel)  kernel.nvi (system information)\n"
"  Drivers/    .nvd  one file per driver, loaded or not\n"
"  Modules/    .nvm  separately built kernel modules (net, virtio, sound)\n"
"  Libraries/  .nvl  shared libraries that programs use\n"
"  Programs/   .nvx  NoovexOS programs\n"
"  Apps/       .nva  built-in apps - double-click to open\n"
"  Services/   .nvs  background services\n"
"  Config/     .nvr  the registry files - double-click opens Registry Editor\n"
"  Logs/       .nvg  System, Security and Application logs (live)\n"
"  Crash/      .nvq  crash reports   last.nvf  the last 60 seconds\n"
"  Boot/       .nvb  boot settings and boot steps\n"
"  Hardware/   .nvh  one file per kind of hardware\n"
"  Fonts/      .nvo  built-in fonts\n"
"  Media/      .nvn  system sounds - double-click to play\n"
"  Themes/     .nvw  themes - double-click to use\n"
"  Focus/      .focus focus modes (command: focus)\n"
"  Restore/    .nvz  restore points (command: restore)\n\n"
"Refresh everything now: run \"sysfiles\" in the Terminal.\n";

void sysfiles_build(void){
    int su=g_uid; g_uid=0;
    sf_dir("/","System"); sf_dir("/System","Logs"); sf_dir("/System","Crash");
    { static const char* const OLD[]={"/System/Logs/kernel.log","/System/Logs/network.log","/System/Kernel/kernel.info",0}; for(int i=0;OLD[i];i++)k_unlink(OLD[i]); }
    sf_writes("/System/README.txt",README);
    sf_kernel(); sf_kfile(); sf_drivers(1); sf_modules(); sf_libraries(); sf_programs(); sf_apps(); sf_services(1); sf_config();
    sf_hardware(1); sf_crash(); sf_boot(); sf_fonts(); sf_media(); sf_themes(); sf_restore_list();
    g_sf_built=1; g_sf_dirty=1; sf_flush_log(); g_sf_next=g_ticks+200;
    g_uid=su; klog("sysfiles: /System is ready"); }

void sysfiles_tick(void){
    if(!g_sf_built||g_ticks<g_sf_next)return; g_sf_next=g_ticks+200;
    int su=g_uid; g_uid=0; sf_flush_log();
    static u32 k; k++; if(k==1)sf_boot();
    if(k%15==0){ sf_kernel(); sf_services(0); sf_drivers(0); sf_modules(); sf_hardware(0); sf_crash(); }
    g_uid=su; }
