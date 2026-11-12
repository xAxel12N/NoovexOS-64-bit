#include "include/nvx/fs/vfs/volumes.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/core/sound.h"
#define SX_MAX 72
enum { SXK_MEM=1, SXK_TTY, SXK_FB, SXK_MICE, SXK_DISK, SXK_PART, SXK_USB, SXK_NET, SXK_AC, SXK_BAT, SXK_BL, SXK_TZ, SXK_SNDCARD, SXK_SNDDEV };
typedef struct { char name[24]; char sub[16]; char devtype[16]; char dpath[112]; char devname[32]; int major, minor, kind, idx, part, blkdisk; u64 start, size, sig; } sxd;
static sxd g_sx[SX_MAX], g_sxo[SX_MAX]; static int g_sxn, g_sxon, g_sx_ready; static u64 g_sx_t; u32 g_sx_seq=1, g_sx_events;
static char* sx_s(char* o,const char* s){ while(*s)*o++=*s++; *o=0; return o; }
static char* sx_u(char* o,u64 v){ char b[24]; int n=0; do{ b[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*o++=b[--n]; *o=0; return o; }
static char* sx_i(char* o,long v){ if(v<0){ *o++='-'; v=-v; } return sx_u(o,(u64)v); }
static char* sx_x(char* o,u32 v,int dg){ static const char H[]="0123456789abcdef"; for(int i=dg-1;i>=0;i--)*o++=H[(v>>(4*i))&15]; *o=0; return o; }
static int sx_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int sx_pre(const char* s,const char* p){ while(*p){ if(*s!=*p)return 0; s++; p++; } return 1; }
static int sx_len(const char* s){ int n=0; while(s[n])n++; return n; }
static void sx_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static sxd* sx_add(int kind,const char* name,const char* sub,int major,int minor){
    if(g_sxn>=SX_MAX)return 0; sxd* d=&g_sx[g_sxn++]; for(u32 i=0;i<sizeof(*d);i++)((u8*)d)[i]=0;
    d->kind=kind; sx_cpy(d->name,name,24); sx_cpy(d->sub,sub,16); d->major=major; d->minor=minor; d->idx=-1; d->blkdisk=-1; return d; }
static void sx_simple(int kind,const char* sub,const char* dir,const char* name,int maj,int min,const char* devname){
    sxd* d=sx_add(kind,name,sub,maj,min); if(!d)return; char* o=sx_s(d->dpath,dir); o=sx_s(o,name); sx_cpy(d->devname,devname?devname:name,32); }
extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_kind_of(int);
extern int gdisk_read_on(int i,u32 lba,int cnt,u8* buf); extern int usb_msd_read(u32 lba,int cnt,u8* out);
extern int msd_ok; extern u32 msd_lastlba; extern int msd_port;
extern int g_ct_ok, g_ct_temp, g_ct_pkg;
extern int lxs_ifinfo(int idx,char* name,u32* ip,u32* mask,u8* mac,u32* flags,int* mtu);
static void sx_parts(int disk,const sxd* dk,int dmaj,int* dmin,int nv){
    extern nvol g_vol[]; extern int g_vol_n;
    for(int v=0;v<g_vol_n;v++){ const nvol* vo=&g_vol[v]; if(vo->kind==VOL_SYS||vo->disk!=disk||vo->part<=0)continue;
        char nm[24],*o=sx_s(nm,dk->name); if(nv)*o++='p'; o=sx_u(o,(u64)vo->part);
        int pmaj=dmaj, pmin; if(dmaj==259)pmin=(*dmin)++; else if(vo->part<=15)pmin=dk->minor+vo->part; else { pmaj=259; pmin=(*dmin)++; } sxd* p=sx_add(SXK_PART,nm,"block",pmaj,pmin); if(!p)return;
        o=sx_s(p->dpath,dk->dpath); *o++='/'; sx_s(o,nm); sx_cpy(p->devname,nm,32); sx_cpy(p->devtype,"partition",16);
        p->part=vo->part; p->start=vo->start; p->size=vo->psz; if(dk->size&&(p->start>=dk->size||p->size>dk->size-p->start))p->size=p->start<dk->size?dk->size-p->start:0; p->blkdisk=dk->blkdisk; p->idx=dk->idx; } }
static void sx_scan(void){
    g_sxn=0;
    static const char* const MN[6]={"null","zero","full","random","urandom","kmsg"}; static const int MM[6]={3,5,7,8,9,11};
    for(int i=0;i<6;i++)sx_simple(SXK_MEM,"mem","/devices/virtual/mem/",MN[i],1,MM[i],0);
    sx_simple(SXK_TTY,"tty","/devices/virtual/tty/","tty",5,0,0); sx_simple(SXK_TTY,"tty","/devices/virtual/tty/","console",5,1,0); sx_simple(SXK_TTY,"tty","/devices/virtual/tty/","ptmx",5,2,0);
    sx_simple(SXK_FB,"graphics","/devices/platform/nvx-fb/graphics/","fb0",29,0,0);
    sx_simple(SXK_MICE,"input","/devices/virtual/input/","mice",13,63,"input/mice");
    { extern void vol_scan(int); vol_scan(0); }
    int nsd=0, nvd=0, nnv=0, nvmin=0;
    int nd=gdisk_count(); for(int i=0;i<nd;i++){ int k=gdisk_kind_of(i); char nm[24],*o; int maj, min, nv=0;
        if(k==DK_NVME){ o=sx_s(nm,"nvme"); o=sx_u(o,(u64)nnv++); sx_s(o,"n1"); maj=259; min=nvmin++; nv=1; }
        else if(k==DK_VIRTIO){ o=sx_s(nm,"vd"); *o++=(char)('a'+nvd); *o=0; maj=252; min=16*nvd; nvd++; }
        else { o=sx_s(nm,"sd"); *o++=(char)('a'+nsd); *o=0; maj=8; min=16*nsd; nsd++; }
        sxd* d=sx_add(SXK_DISK,nm,"block",maj,min); if(!d)break; o=sx_s(d->dpath,"/devices/platform/nvx-disk."); o=sx_u(o,(u64)i); o=sx_s(o,"/block/"); sx_s(o,nm);
        sx_cpy(d->devname,nm,32); sx_cpy(d->devtype,"disk",16); d->idx=i; d->blkdisk=i; d->size=gdisk_size_of(i); sxd copy=*d; sx_parts(i,&copy,maj,&nvmin,nv); }
    if(msd_ok){ char nm[24],*o=sx_s(nm,"sd"); *o++=(char)('a'+nsd); *o=0; sxd* d=sx_add(SXK_DISK,nm,"block",8,16*nsd); nsd++;
        if(d){ o=sx_s(d->dpath,"/devices/platform/nvx-usb/usb1/1-"); o=sx_u(o,((u64)msd_port+1)); o=sx_s(o,"/block/"); sx_s(o,nm); sx_cpy(d->devname,nm,32); sx_cpy(d->devtype,"disk",16); d->idx=-1; d->blkdisk=-2; d->size=(u64)msd_lastlba+1; d->part=-1; sxd copy=*d; sx_parts(-1,&copy,8,&nvmin,0); } }
    for(int k=0;k<g_usbdev_n&&k<USBDEV_MAX;k++){ usbdev_t* u=&g_usbdev[k]; if(!u->used)continue; int bus=u->ctl+1, dn=k+2; char nm[24],*o=sx_u(nm,(u64)bus); *o++='-'; sx_u(o,((u64)u->port+1));
        sxd* d=sx_add(SXK_USB,nm,"usb",189,(bus-1)*128+dn-1); if(!d)break; o=sx_s(d->dpath,"/devices/platform/nvx-usb/usb"); o=sx_u(o,(u64)bus); *o++='/'; sx_s(o,nm);
        o=sx_s(d->devname,"bus/usb/"); o=sx_x(o,0,0); { char t[8],*q=t; u32 b=(u32)bus; *q++=(char)('0'+b/100%10); *q++=(char)('0'+b/10%10); *q++=(char)('0'+b%10); *q++='/'; *q++=(char)('0'+dn/100%10); *q++=(char)('0'+dn/10%10); *q++=(char)('0'+dn%10); *q=0; sx_s(o,t); }
        sx_cpy(d->devtype,"usb_device",16); d->idx=k; d->sig=((u64)u->vid<<16)|u->pid; }
    for(int ix=1;ix<=2;ix++){ char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu; if(lxs_ifinfo(ix,nm,&ip,&mk,mac,&f,&mtu))continue;
        sxd* d=sx_add(SXK_NET,nm,"net",0,0); if(!d)break; char* o=sx_s(d->dpath,ix==1?"/devices/virtual/net/":"/devices/platform/nvx-net/net/"); sx_s(o,nm); d->idx=ix; d->major=-1; d->sig=f&0x10041u; }
    if(g_acad||g_acb_n>0){ sxd* d=sx_add(SXK_AC,"AC","power_supply",-1,0); if(d){ sx_s(d->dpath,"/devices/platform/nvx-acpi/power_supply/AC"); d->sig=((u64)g_ac_online+1); } }
    for(int b=0;b<g_acb_n&&b<2;b++){ if(!g_acb[b].present)continue; char nm[8]="BAT0"; nm[3]=(char)('0'+b); sxd* d=sx_add(SXK_BAT,nm,"power_supply",-1,0); if(!d)break; char* o=sx_s(d->dpath,"/devices/platform/nvx-acpi/power_supply/"); sx_s(o,nm); d->idx=b; d->sig=(((u64)g_acb_pct+1)<<2)|(u64)(g_acb_charging&1); }
    if(g_bl.ok){ sxd* d=sx_add(SXK_BL,"acpi_video0","backlight",-1,0); if(d)sx_s(d->dpath,"/devices/platform/nvx-acpi/backlight/acpi_video0"); }
    if(g_ct_ok){ sxd* d=sx_add(SXK_TZ,"thermal_zone0","thermal",-1,0); if(d)sx_s(d->dpath,"/devices/virtual/thermal/thermal_zone0"); }
    if(g_snd_state==1){ sxd* d=sx_add(SXK_SNDCARD,"card0","sound",-1,0); if(d)sx_s(d->dpath,"/devices/platform/nvx-sound/sound/card0");
        sx_simple(SXK_SNDDEV,"sound","/devices/platform/nvx-sound/sound/card0/","controlC0",116,0,"snd/controlC0");
        sx_simple(SXK_SNDDEV,"sound","/devices/platform/nvx-sound/sound/card0/","pcmC0D0p",116,16,"snd/pcmC0D0p");
        if(snd_rec_avail())sx_simple(SXK_SNDDEV,"sound","/devices/platform/nvx-sound/sound/card0/","pcmC0D0c",116,24,"snd/pcmC0D0c"); }
    g_sx_ready=1; g_sx_t=g_ticks; }
static void sx_fresh(void){ if(!g_sx_ready||g_ticks-g_sx_t>=50)sx_scan(); }
static sxd* sx_by_dpath(const char* dp){ for(int i=0;i<g_sxn;i++) if(sx_eq(g_sx[i].dpath,dp))return &g_sx[i]; return 0; }
static const char* sx_bus_of(const sxd* d){ return d->kind==SXK_USB?"bus/usb":0; }

static int sx_props(const sxd* d,const char* act,char* o0,int cap,char sep){
    char* o=o0; char* e=o0+cap-128;
    if(act){ o=sx_s(o,"ACTION="); o=sx_s(o,act); *o++=sep; }
    if(act){ o=sx_s(o,"DEVPATH="); o=sx_s(o,d->dpath); *o++=sep; o=sx_s(o,"SUBSYSTEM="); o=sx_s(o,d->sub); *o++=sep; }
    if(d->major>=0&&(d->major||d->minor)){ o=sx_s(o,"MAJOR="); o=sx_u(o,(u64)d->major); *o++=sep; o=sx_s(o,"MINOR="); o=sx_u(o,(u64)d->minor); *o++=sep; o=sx_s(o,"DEVNAME="); o=sx_s(o,d->devname); *o++=sep; }
    if(d->devtype[0]){ o=sx_s(o,"DEVTYPE="); o=sx_s(o,d->devtype); *o++=sep; }
    if(d->kind==SXK_PART){ o=sx_s(o,"PARTN="); o=sx_u(o,(u64)d->part); *o++=sep; }
    if(d->kind==SXK_USB){ usbdev_t* u=&g_usbdev[d->idx]; o=sx_s(o,"PRODUCT="); o=sx_x(o,u->vid,4); *o++='/'; o=sx_x(o,u->pid,4); o=sx_s(o,"/0"); *o++=sep; o=sx_s(o,"BUSNUM=00"); o=sx_u(o,((u64)u->ctl+1)); *o++=sep; o=sx_s(o,"DEVNUM="); { int dn=d->idx+2; *o++=(char)('0'+dn/100%10); *o++=(char)('0'+dn/10%10); *o++=(char)('0'+dn%10); } *o++=sep; }
    if(d->kind==SXK_NET){ o=sx_s(o,"INTERFACE="); o=sx_s(o,d->name); *o++=sep; o=sx_s(o,"IFINDEX="); o=sx_u(o,(u64)d->idx); *o++=sep; }
    if(d->kind==SXK_AC||d->kind==SXK_BAT){ o=sx_s(o,"POWER_SUPPLY_NAME="); o=sx_s(o,d->name); *o++=sep; o=sx_s(o,"POWER_SUPPLY_TYPE="); o=sx_s(o,d->kind==SXK_AC?"Mains":"Battery"); *o++=sep;
        if(d->kind==SXK_AC){ o=sx_s(o,"POWER_SUPPLY_ONLINE="); o=sx_u(o,g_ac_online==1); *o++=sep; }
        else { o=sx_s(o,"POWER_SUPPLY_CAPACITY="); o=sx_u(o,(u64)(g_acb_pct<0?0:g_acb_pct)); *o++=sep; o=sx_s(o,"POWER_SUPPLY_STATUS="); o=sx_s(o,g_acb_charging?"Charging":g_ac_online==1?"Not charging":"Discharging"); *o++=sep; } }
    if(act){ o=sx_s(o,"SEQNUM="); o=sx_u(o,g_sx_seq); *o++=sep; }
    if(o>e)o=e; *o=0; return (int)(o-o0); }

static const char* const SX_ATTR_DISK[]={"uevent","dev","subsystem","size","ro","removable","range","ext_range","capability","queue","device",0};
static const char* const SX_ATTR_PART[]={"uevent","dev","subsystem","size","ro","start","partition",0};
static const char* const SX_ATTR_USB[]={"uevent","dev","subsystem","idVendor","idProduct","product","manufacturer","busnum","devnum","speed","bDeviceClass","bNumConfigurations","version","devpath","descriptors",0};
static const char* const SX_ATTR_NET[]={"uevent","subsystem","address","broadcast","operstate","carrier","mtu","type","ifindex","flags","addr_len","tx_queue_len",0};
static const char* const SX_ATTR_AC[]={"uevent","subsystem","type","online",0};
static const char* const SX_ATTR_BAT[]={"uevent","subsystem","type","status","present","capacity","capacity_level","technology","energy_full_design","energy_full","energy_now","power_now","voltage_now","cycle_count","model_name","manufacturer","serial_number",0};
static const char* const SX_ATTR_BL[]={"uevent","subsystem","brightness","actual_brightness","max_brightness","type",0};
static const char* const SX_ATTR_TZ[]={"uevent","subsystem","type","temp",0};
static const char* const SX_ATTR_CARD[]={"uevent","subsystem","id","number",0};
static const char* const SX_ATTR_DEV[]={"uevent","dev","subsystem",0};
static const char* const SX_ATTR_QUEUE[]={"logical_block_size","physical_block_size","hw_sector_size","rotational","max_sectors_kb","read_ahead_kb",0};
static const char* const SX_ATTR_DEVICE[]={"model","vendor","rev","state",0};
static const char* const* sx_attrs(const sxd* d){
    switch(d->kind){ case SXK_DISK:return SX_ATTR_DISK; case SXK_PART:return SX_ATTR_PART; case SXK_USB:return SX_ATTR_USB; case SXK_NET:return SX_ATTR_NET; case SXK_AC:return SX_ATTR_AC; case SXK_BAT:return SX_ATTR_BAT;
        case SXK_BL:return SX_ATTR_BL; case SXK_TZ:return SX_ATTR_TZ; case SXK_SNDCARD:return SX_ATTR_CARD; default:return SX_ATTR_DEV; } }
static int sx_attr_type(const sxd* d,const char* a){ if(sx_eq(a,"subsystem"))return 10; if(d->kind==SXK_DISK&&(sx_eq(a,"queue")||sx_eq(a,"device")))return 4; return 8; }
static int sx_mac(char* o,const u8* m){ char* p=o; for(int i=0;i<6;i++){ p=sx_x(p,m[i],2); if(i<5)*p++=':'; } *p++='\n'; *p=0; return (int)(p-o); }
static int sx_attr_read(const sxd* d,const char* a,char* out,int cap){
    char* o=out;
    if(sx_eq(a,"uevent")){ int n=sx_props(d,0,out,cap,'\n'); return n; }
    if(sx_eq(a,"dev")){ if(d->major<0)return -1; o=sx_u(o,(u64)d->major); *o++=':'; o=sx_u(o,(u64)d->minor); *o++='\n'; return (int)(o-out); }
    if(d->kind==SXK_DISK||d->kind==SXK_PART){
        if(sx_eq(a,"size")){ o=sx_u(o,d->size); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"ro")){ o=sx_s(o,"0\n"); return (int)(o-out); }
        if(sx_eq(a,"removable")){ o=sx_s(o,d->blkdisk==-2?"1\n":"0\n"); return (int)(o-out); }
        if(sx_eq(a,"range")){ o=sx_s(o,"16\n"); return (int)(o-out); }
        if(sx_eq(a,"ext_range")){ o=sx_s(o,"256\n"); return (int)(o-out); }
        if(sx_eq(a,"capability")){ o=sx_s(o,d->blkdisk==-2?"51\n":"50\n"); return (int)(o-out); }
        if(sx_eq(a,"start")){ o=sx_u(o,d->start); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"partition")){ o=sx_u(o,(u64)d->part); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"queue/logical_block_size")||sx_eq(a,"queue/physical_block_size")||sx_eq(a,"queue/hw_sector_size")){ o=sx_s(o,"512\n"); return (int)(o-out); }
        if(sx_eq(a,"queue/rotational")){ o=sx_s(o,d->blkdisk>=0&&gdisk_kind_of(d->blkdisk)==DK_IDE?"1\n":"0\n"); return (int)(o-out); }
        if(sx_eq(a,"queue/max_sectors_kb")){ o=sx_s(o,"512\n"); return (int)(o-out); }
        if(sx_eq(a,"queue/read_ahead_kb")){ o=sx_s(o,"128\n"); return (int)(o-out); }
        if(sx_eq(a,"device/model")){ o=sx_s(o,d->blkdisk==-2?"USB drive":d->blkdisk>=0?gdisk_name_of(d->blkdisk):"disk"); *o++='\n'; *o=0; return (int)(o-out); }
        if(sx_eq(a,"device/vendor")){ o=sx_s(o,d->blkdisk==-2?"USB\n":"ATA\n"); return (int)(o-out); }
        if(sx_eq(a,"device/rev")){ o=sx_s(o,"1.0\n"); return (int)(o-out); }
        if(sx_eq(a,"device/state")){ o=sx_s(o,"running\n"); return (int)(o-out); }
        return -1; }
    if(d->kind==SXK_USB){ usbdev_t* u=&g_usbdev[d->idx];
        if(sx_eq(a,"idVendor")){ o=sx_x(o,u->vid,4); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"idProduct")){ o=sx_x(o,u->pid,4); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"product")){ o=sx_s(o,u->name[0]?u->name:u->kind); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"manufacturer")){ o=sx_s(o,"Unknown\n"); return (int)(o-out); }
        if(sx_eq(a,"busnum")){ o=sx_u(o,((u64)u->ctl+1)); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"devnum")){ o=sx_u(o,(u64)d->idx+2); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"speed")){ o=sx_s(o,"480\n"); return (int)(o-out); }
        if(sx_eq(a,"bDeviceClass")){ o=sx_s(o,"00\n"); return (int)(o-out); }
        if(sx_eq(a,"bNumConfigurations")){ o=sx_s(o,"1\n"); return (int)(o-out); }
        if(sx_eq(a,"version")){ o=sx_s(o," 2.00\n"); return (int)(o-out); }
        if(sx_eq(a,"devpath")){ o=sx_u(o,((u64)u->port+1)); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"descriptors")){ if(cap<18)return -1; u8 dd[18]={18,1,0x00,0x02,0,0,0,64,(u8)u->vid,(u8)(u->vid>>8),(u8)u->pid,(u8)(u->pid>>8),0,1,0,0,0,1}; for(int i=0;i<18;i++)out[i]=(char)dd[i]; return 18; }
        return -1; }
    if(d->kind==SXK_NET){ char nm[16]; u32 ip,mk,f; u8 mac[6]; int mtu; if(lxs_ifinfo(d->idx,nm,&ip,&mk,mac,&f,&mtu))return -1;
        if(sx_eq(a,"address"))return sx_mac(out,mac);
        if(sx_eq(a,"broadcast")){ u8 b[6]; for(int i=0;i<6;i++)b[i]=d->idx==1?0:0xFF; return sx_mac(out,b); }
        if(sx_eq(a,"operstate")){ o=sx_s(o,d->idx==1?"unknown\n":(f&0x40)?"up\n":"down\n"); return (int)(o-out); }
        if(sx_eq(a,"carrier")){ o=sx_s(o,(f&0x40)||d->idx==1?"1\n":"0\n"); return (int)(o-out); }
        if(sx_eq(a,"mtu")){ o=sx_u(o,(u64)mtu); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"type")){ o=sx_s(o,d->idx==1?"772\n":"1\n"); return (int)(o-out); }
        if(sx_eq(a,"ifindex")){ o=sx_u(o,(u64)d->idx); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"flags")){ o=sx_s(o,"0x"); o=sx_x(o,f&0xFFFF,4); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"addr_len")){ o=sx_s(o,"6\n"); return (int)(o-out); }
        if(sx_eq(a,"tx_queue_len")){ o=sx_s(o,"1000\n"); return (int)(o-out); }
        return -1; }
    if(d->kind==SXK_AC){ if(sx_eq(a,"type")){ o=sx_s(o,"Mains\n"); return (int)(o-out); } if(sx_eq(a,"online")){ o=sx_s(o,g_ac_online==1?"1\n":"0\n"); return (int)(o-out); } return -1; }
    if(d->kind==SXK_BAT){ acbat* b=&g_acb[d->idx]; u32 mul=b->mwh?1000u:(b->volt?b->volt:11100u);
        if(sx_eq(a,"type")){ o=sx_s(o,"Battery\n"); return (int)(o-out); }
        if(sx_eq(a,"status")){ o=sx_s(o,g_acb_charging?"Charging\n":g_ac_online==1?(g_acb_pct>=99?"Full\n":"Not charging\n"):"Discharging\n"); return (int)(o-out); }
        if(sx_eq(a,"present")){ o=sx_s(o,"1\n"); return (int)(o-out); }
        if(sx_eq(a,"capacity")){ o=sx_u(o,(u64)(g_acb_pct<0?0:g_acb_pct>100?100:g_acb_pct)); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"capacity_level")){ int p=g_acb_pct; o=sx_s(o,p<0?"Unknown\n":p<=5?"Critical\n":p<=15?"Low\n":p>=98?"Full\n":"Normal\n"); return (int)(o-out); }
        if(sx_eq(a,"technology")){ o=sx_s(o,"Li-ion\n"); return (int)(o-out); }
        if(sx_eq(a,"energy_full_design")){ o=sx_u(o,(u64)b->design*mul); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"energy_full")){ o=sx_u(o,(u64)b->full*mul); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"energy_now")){ o=sx_u(o,(u64)b->remain*mul); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"power_now")){ o=sx_u(o,(u64)b->rate*mul); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"voltage_now")){ o=sx_u(o,(u64)b->pvolt*1000u); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"cycle_count")){ o=sx_u(o,(u64)b->cycles); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"model_name")){ o=sx_s(o,b->model); *o++='\n'; *o=0; return (int)(o-out); }
        if(sx_eq(a,"manufacturer")){ o=sx_s(o,b->oem); *o++='\n'; *o=0; return (int)(o-out); }
        if(sx_eq(a,"serial_number")){ o=sx_s(o,b->serial); *o++='\n'; *o=0; return (int)(o-out); }
        return -1; }
    if(d->kind==SXK_BL){ extern int bl_cur_pct(void); int c=bl_cur_pct(); if(c<0)c=0;
        if(sx_eq(a,"brightness")||sx_eq(a,"actual_brightness")){ o=sx_u(o,(u64)c); *o++='\n'; return (int)(o-out); }
        if(sx_eq(a,"max_brightness")){ o=sx_s(o,"100\n"); return (int)(o-out); }
        if(sx_eq(a,"type")){ o=sx_s(o,"firmware\n"); return (int)(o-out); }
        return -1; }
    if(d->kind==SXK_TZ){ if(sx_eq(a,"type")){ o=sx_s(o,"x86_pkg_temp\n"); return (int)(o-out); } if(sx_eq(a,"temp")){ int t=g_ct_pkg>-1000?g_ct_pkg:g_ct_temp; o=sx_i(o,(long)t*1000); *o++='\n'; return (int)(o-out); } return -1; }
    if(d->kind==SXK_SNDCARD){ if(sx_eq(a,"id")){ o=sx_s(o,"NVX\n"); return (int)(o-out); } if(sx_eq(a,"number")){ o=sx_s(o,"0\n"); return (int)(o-out); } return -1; }
    return -1; }

static void sx_emit(const sxd* d,const char* act){
    static char msg[1536]; char* o=msg; o=sx_s(o,act); *o++='@'; o=sx_s(o,d->dpath); *o++=0;
    int n=sx_props(d,act,o,(int)(msg+sizeof(msg)-o),0); o+=n;
    extern int lxs_nl_bcast(int proto,u32 group,const u8* b,int n); lxs_nl_bcast(15,1,(const u8*)msg,(int)(o-msg));
    static u8 um[1600]; for(int i=0;i<40;i++)um[i]=0; const char* pre="libudev"; for(int i=0;pre[i];i++)um[i]=(u8)pre[i];
    u32 magic=0xfeedcafeu; um[8]=(u8)(magic>>24); um[9]=(u8)(magic>>16); um[10]=(u8)(magic>>8); um[11]=(u8)magic;
    int pn=sx_props(d,act,(char*)um+40,(int)sizeof(um)-40,0); u32 hs=40, po=40, pl=(u32)pn+1;
    um[12]=(u8)hs; um[16]=(u8)po; um[17]=(u8)(po>>8); um[20]=(u8)pl; um[21]=(u8)(pl>>8);
    lxs_nl_bcast(15,2,um,(int)(40+pl));
    g_sx_seq++; g_sx_events++; }
void sx_poll(void){
    static u64 last; if(g_ticks-last<50)return; last=g_ticks;
    extern int lxs_nl_listeners(int proto);
    static int based; if(!lxs_nl_listeners(15)){ based=0; return; } sx_scan(); if(!based){ for(int i=0;i<g_sxn;i++)g_sxo[i]=g_sx[i]; g_sxon=g_sxn; based=1; return; }
    for(int i=g_sxon-1;i>=0;i--){ if(!sx_by_dpath(g_sxo[i].dpath))sx_emit(&g_sxo[i],"remove"); }
    for(int i=0;i<g_sxn;i++){ int f=-1; for(int j=0;j<g_sxon;j++) if(sx_eq(g_sxo[j].dpath,g_sx[i].dpath)){ f=j; break; }
        if(f<0)sx_emit(&g_sx[i],"add"); else if(g_sxo[f].sig!=g_sx[i].sig)sx_emit(&g_sx[i],"change"); }
    for(int i=0;i<g_sxn;i++)g_sxo[i]=g_sx[i]; g_sxon=g_sxn; }

static const char* const SX_TOP[]={"block","bus","class","dev","devices","firmware","fs","kernel","module","power",0};
static int sx_depth(const char* rel){ int n=0; for(int i=0;rel[i];i++) if(rel[i]=='/')n++; return n; }
static int sx_link(const char* rel,char* tgt,int cap){
    char t[160]; t[0]=0;
    if(sx_pre(rel,"class/")){ const char* q=rel+6; const char* sl=q; while(*sl&&*sl!='/')sl++; if(!*sl)return 0; char sub[16]; int k=0; for(const char* c=q;c<sl&&k<15;c++)sub[k++]=*c; sub[k]=0; const char* nm=sl+1; if(!*nm||sx_len(nm)>23)return 0; for(const char* c=nm;*c;c++) if(*c=='/')return 0;
        for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind!=SXK_USB&&sx_eq(d->sub,sub)&&sx_eq(d->name,nm)){ char* o=sx_s(t,"../.."); sx_s(o,d->dpath); sx_cpy(tgt,t,cap); return 1; } } return 0; }
    if(sx_pre(rel,"block/")){ const char* nm=rel+6; for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind==SXK_DISK&&sx_eq(d->name,nm)){ char* o=sx_s(t,".."); sx_s(o,d->dpath); sx_cpy(tgt,t,cap); return 1; } } return 0; }
    if(sx_pre(rel,"bus/usb/devices/")){ const char* nm=rel+16; for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind==SXK_USB&&sx_eq(d->name,nm)){ char* o=sx_s(t,"../../.."); sx_s(o,d->dpath); sx_cpy(tgt,t,cap); return 1; } } return 0; }
    if(sx_pre(rel,"dev/block/")||sx_pre(rel,"dev/char/")){ int blk=rel[4]=='b'; const char* q=rel+(blk?10:9); int ma=0,mi=0; while(*q>='0'&&*q<='9')ma=ma*10+(*q++-'0'); if(*q++!=':')return 0; int any=0; while(*q>='0'&&*q<='9'){ mi=mi*10+(*q++-'0'); any=1; } if(*q||!any)return 0;
        for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; int isb=d->kind==SXK_DISK||d->kind==SXK_PART; if(d->major>=0&&isb==blk&&d->major==ma&&d->minor==mi&&(d->major||d->minor)){ char* o=sx_s(t,"../.."); sx_s(o,d->dpath); sx_cpy(tgt,t,cap); return 1; } } return 0; }
    if(sx_pre(rel,"devices/")){ int L=sx_len(rel); if(L>10&&sx_eq(rel+L-10,"/subsystem")){ char dp[128]; int k=0; dp[k++]='/'; for(int i=0;i<L-10&&k<127;i++)dp[k++]=rel[i]; dp[k]=0; sxd* d=sx_by_dpath(dp); if(!d)return 0;
            int dep=sx_depth(rel); char* o=t; for(int i=0;i<dep;i++){ o=sx_s(o,i?"/..":".."); } const char* bus=sx_bus_of(d); if(bus){ o=sx_s(o,"/"); sx_s(o,bus); } else { o=sx_s(o,"/class/"); sx_s(o,d->sub); } sx_cpy(tgt,t,cap); return 1; } }
    return 0; }
static int sx_canon(const char* p,char* out,int follow_last){
    char cur[200]; sx_cpy(cur,p,200);
    for(int loop=0;loop<8;loop++){
        if(!sx_pre(cur,"/sys"))return 0; if(cur[4]&&cur[4]!='/')return 0;
        const char* rel=cur[4]?cur+5:""; char acc[200]; int al=0; acc[0]=0; const char* q=rel; int changed=0;
        while(*q){ const char* s=q; while(*q&&*q!='/')q++; int cl=(int)(q-s); if(cl==1&&s[0]=='.'){ while(*q=='/')q++; continue; } if(cl==2&&s[0]=='.'&&s[1]=='.'){ while(al>0&&acc[al-1]!='/')al--; if(al>0)al--; acc[al]=0; while(*q=='/')q++; continue; } if(al)acc[al++]='/'; for(int i=0;i<cl&&al<190;i++)acc[al++]=s[i]; acc[al]=0; while(*q=='/')q++;
            int last=!*q; char tg[160]; if((!last||follow_last)&&sx_link(acc,tg,160)){
                char base[200]; int bl=0; base[0]=0; { int ls=-1; for(int i=0;acc[i];i++) if(acc[i]=='/')ls=i; for(int i=0;i<ls;i++)base[bl++]=acc[i]; base[bl]=0; }
                const char* t=tg; while(sx_pre(t,"../")||sx_eq(t,"..")){ int ls=-1; for(int i=0;base[i];i++) if(base[i]=='/')ls=i; if(ls<0){ bl=0; base[0]=0; } else { bl=ls; base[bl]=0; } t+=t[2]?3:2; }
                if(4+1+bl+1+sx_len(t)+1+sx_len(q)>=200)return 0; char nw[200]; char* o=sx_s(nw,"/sys"); if(bl){ *o++='/'; o=sx_s(o,base); } if(*t){ *o++='/'; o=sx_s(o,t); } if(*q){ *o++='/'; o=sx_s(o,q); }
                sx_cpy(cur,nw,200); changed=1; break; } }
        if(!changed){ char nw[200]; char* o=sx_s(nw,"/sys"); if(al){ *o++='/'; o=sx_s(o,acc); } sx_cpy(out,nw,200); return 1; } }
    return 0; }
static int sx_devdir_child(const char* drel,int idx,char* name){
    char seen[24][28]; int ns=0; int L=sx_len(drel);
    for(int i=0;i<g_sxn;i++){ const char* dp=g_sx[i].dpath+1; if(!sx_pre(dp,drel)||(L&&dp[L]!='/'))continue; const char* c=dp+L+(L?1:0); if(!*c)continue; char nm[28]; int k=0; while(c[k]&&c[k]!='/'&&k<27){ nm[k]=c[k]; k++; } nm[k]=0;
        int dup=0; for(int j=0;j<ns;j++) if(sx_eq(seen[j],nm))dup=1; if(dup||ns>=24)continue; sx_cpy(seen[ns],nm,28); if(ns==idx){ sx_cpy(name,nm,28); return 1; } ns++; }
    return 0; }
static int sx_node(const char* p,sxd** dev,const char** attr){
    *dev=0; *attr=0; if(!sx_pre(p,"/sys"))return 0; if(p[4]&&p[4]!='/')return 0; const char* rel=p[4]?p+5:""; sx_fresh();
    if(!*rel)return 4;
    for(int i=0;SX_TOP[i];i++) if(sx_eq(rel,SX_TOP[i]))return 4;
    if(sx_eq(rel,"bus/usb")||sx_eq(rel,"bus/usb/devices")||sx_eq(rel,"bus/usb/drivers")||sx_eq(rel,"dev/block")||sx_eq(rel,"dev/char")||sx_eq(rel,"devices/system")||sx_eq(rel,"devices/system/cpu")||sx_eq(rel,"fs/cgroup"))return 4;
    if(sx_pre(rel,"class/")){ const char* q=rel+6; int hs=0; for(const char* c=q;*c;c++) if(*c=='/')hs=1; if(!hs){ for(int i=0;i<g_sxn;i++) if(g_sx[i].kind!=SXK_USB&&sx_eq(g_sx[i].sub,q))return 4; return -1; } char t[8]; return sx_link(rel,(char*)t,1)?10:-1; }
    if(sx_pre(rel,"block/")||sx_pre(rel,"bus/usb/devices/")||sx_pre(rel,"dev/block/")||sx_pre(rel,"dev/char/")){ char t[8]; return sx_link(rel,t,1)?10:-1; }
    if(sx_pre(rel,"kernel/")){ const char* f=rel+7; if(sx_eq(f,"ostype")||sx_eq(f,"osrelease")||sx_eq(f,"hostname")||sx_eq(f,"arch"))return 8; return -1; }
    if(sx_pre(rel,"power/")){ const char* f=rel+6; if(sx_eq(f,"state")||sx_eq(f,"mem_sleep"))return 8; return -1; }
    if(sx_pre(rel,"devices/system/cpu/")){ const char* f=rel+19; if(sx_eq(f,"online")||sx_eq(f,"possible")||sx_eq(f,"present")||sx_eq(f,"kernel_max"))return 8; return -1; }
    if(sx_pre(rel,"devices/")||sx_eq(rel,"devices")){ char dp[128]; dp[0]='/'; sx_cpy(dp+1,rel,127); sxd* d=sx_by_dpath(dp); if(d){ *dev=d; return 4; }
        char nm[28]; if(sx_devdir_child(rel,0,nm))return 4;
        int ls=-1; for(int i=0;rel[i];i++) if(rel[i]=='/')ls=i; for(int cut=ls;cut>0;cut--){ if(rel[cut]!='/')continue; char pd[128]; pd[0]='/'; int k=1; for(int i=0;i<cut&&k<127;i++)pd[k++]=rel[i]; pd[k]=0; sxd* q=sx_by_dpath(pd); if(!q)continue;
            const char* a=rel+cut+1; const char* const* L=sx_attrs(q);
            for(int i=0;L[i];i++){ if(sx_eq(a,L[i])){ *dev=q; *attr=L[i]; return sx_attr_type(q,a); } }
            if(q->kind==SXK_DISK){ const char* sub=sx_pre(a,"queue/")?"queue/":sx_pre(a,"device/")?"device/":0; if(sub){ const char* const* S=sub[0]=='q'?SX_ATTR_QUEUE:SX_ATTR_DEVICE; const char* f=a+sx_len(sub); for(int i=0;S[i];i++) if(sx_eq(f,S[i])){ *dev=q; *attr=a; return 8; } } }
            return -1; }
        return -1; }
    return -1; }
int sx_list_sub(const char* p,int idx,char* name,int* type);
int sx_list(const char* p,int idx,char* name,int* type){
    { int lv=lxn_sx_list(p,idx,name,type); if(lv!=-9999)return lv; }
    sxd* d; const char* a; char cp[200]; if(!sx_canon(p,cp,1))return 0; int k=sx_node(cp,&d,&a); if(k!=4)return 0; const char* rel=cp[4]?cp+5:""; int c=0;
#define SXE(nm,tp) do{ if(c==idx){ sx_cpy(name,nm,64); *type=tp; return 1; } c++; }while(0)
    if(!*rel){ for(int i=0;SX_TOP[i];i++)SXE(SX_TOP[i],4); return 0; }
    if(sx_eq(rel,"block")){ for(int i=0;i<g_sxn;i++) if(g_sx[i].kind==SXK_DISK)SXE(g_sx[i].name,10); return 0; }
    if(sx_eq(rel,"bus")){ SXE("usb",4); return 0; }
    if(sx_eq(rel,"bus/usb")){ SXE("devices",4); SXE("drivers",4); return 0; }
    if(sx_eq(rel,"bus/usb/devices")){ for(int i=0;i<g_sxn;i++) if(g_sx[i].kind==SXK_USB)SXE(g_sx[i].name,10); return 0; }
    if(sx_eq(rel,"class")){ for(int i=0;i<g_sxn;i++){ if(g_sx[i].kind==SXK_USB)continue; int dup=0; for(int j=0;j<i;j++) if(g_sx[j].kind!=SXK_USB&&sx_eq(g_sx[j].sub,g_sx[i].sub))dup=1; if(!dup)SXE(g_sx[i].sub,4); } return 0; }
    if(sx_pre(rel,"class/")){ const char* sub=rel+6; for(int i=0;i<g_sxn;i++) if(g_sx[i].kind!=SXK_USB&&sx_eq(g_sx[i].sub,sub))SXE(g_sx[i].name,10); return 0; }
    if(sx_eq(rel,"dev")){ SXE("block",4); SXE("char",4); return 0; }
    if(sx_eq(rel,"dev/block")||sx_eq(rel,"dev/char")){ int blk=rel[4]=='b'; for(int i=0;i<g_sxn;i++){ sxd* q=&g_sx[i]; int isb=q->kind==SXK_DISK||q->kind==SXK_PART; if(q->major<0||isb!=blk||(!q->major&&!q->minor))continue; char nm[24],*o=sx_u(nm,(u64)q->major); *o++=':'; sx_u(o,(u64)q->minor); SXE(nm,10); } return 0; }
    if(sx_eq(rel,"kernel")){ SXE("ostype",8); SXE("osrelease",8); SXE("hostname",8); SXE("arch",8); return 0; }
    if(sx_eq(rel,"power")){ SXE("state",8); SXE("mem_sleep",8); return 0; }
    if(sx_eq(rel,"fs")){ SXE("cgroup",4); return 0; }
    if(sx_eq(rel,"devices/system")){ SXE("cpu",4); return 0; }
    if(sx_eq(rel,"devices/system/cpu")){ SXE("online",8); SXE("possible",8); SXE("present",8); SXE("kernel_max",8); return 0; }
    if(sx_eq(rel,"devices")){ SXE("system",4); char nm[28]; for(int j=0;sx_devdir_child("devices",j,nm);j++)SXE(nm,4); return 0; }
    if(d&&a)return sx_list_sub(cp,idx,name,type);
    if(d){ const char* const* L=sx_attrs(d); for(int i=0;L[i];i++){ if(sx_eq(L[i],"dev")&&d->major<0)continue; SXE(L[i],sx_attr_type(d,L[i])); } }
    { char nm[28]; for(int j=0;sx_devdir_child(rel,j,nm);j++)SXE(nm,4); }
    return 0;
#undef SXE
}
int sx_list_sub(const char* p,int idx,char* name,int* type){
    char cp[200]; if(!sx_canon(p,cp,1))return 0; int L=sx_len(cp);
    const char* const* S=0; if(L>6&&sx_eq(cp+L-6,"/queue"))S=SX_ATTR_QUEUE; else if(L>7&&sx_eq(cp+L-7,"/device"))S=SX_ATTR_DEVICE; if(!S)return 0;
    sxd* d; const char* a; if(sx_node(cp,&d,&a)!=4||!d||!a)return 0; for(int i=0;S[i];i++) if(i==idx){ sx_cpy(name,S[i],64); *type=8; return 1; } return 0; }
int sx_readlink(const char* p,char* out,int cap){
    if(sx_eq(p,"/sys"))return -LX_EINVAL; if(!sx_pre(p,"/sys/"))return -9999; char cp[200]; if(!sx_canon(p,cp,0))return -9999; sxd* d; const char* a; int k=sx_node(cp,&d,&a); if(k==0)return -9999; if(k<0)return -LX_ENOENT; if(k!=10)return -LX_EINVAL;
    char t[160]; if(!sx_link(cp+5,t,160))return -LX_EINVAL; int n=sx_len(t); if(n>cap)n=cap; for(int i=0;i<n;i++)out[i]=t[i]; return n; }
extern int g_swappiness;
static int pv_name(const char* p){ if(sx_eq(p,"/proc/sys/vm/drop_caches"))return 1; if(sx_eq(p,"/proc/sys/vm/swappiness"))return 2; return 0; }
static int pv_read(const char* p,char* out){ int k=pv_name(p); char* o=out; if(k==2){ o=sx_u(o,(u64)g_swappiness); *o++='\n'; } else if(k!=1)return -1; return (int)(o-out); }
static int pv_write(const char* p,const char* w,int n){ int k=pv_name(p); if(!k)return -LX_ENOENT; if(g_uid!=0)return -LX_EACCES; int v=0, any=0; for(int i=0;w[i];i++){ if(w[i]<'0'||w[i]>'9')return -LX_EINVAL; v=v*10+(w[i]-'0'); any=1; if(v>1000)return -LX_EINVAL; } if(!any)return -LX_EINVAL;
    if(k==1){ if(v<1||v>3)return -LX_EINVAL; extern void sysroot_drop_caches(int what); sysroot_drop_caches(v); return n; }
    if(v>200)return -LX_EINVAL; g_swappiness=v; return n; }
int sx_read(const char* p,char* out,int cap){
    { int lv=lxn_sx_read(p,out,cap); if(lv!=-9999)return lv; }
    if(sx_pre(p,"/proc/sys/vm/"))return pv_read(p,out);
    char cp[200]; if(!sx_canon(p,cp,1))return -1; sxd* d; const char* a; int k=sx_node(cp,&d,&a); if(k!=8)return -1; const char* rel=cp+5; char* o=out;
    if(d&&a)return sx_attr_read(d,a,out,cap);
    if(sx_eq(rel,"kernel/ostype")){ o=sx_s(o,"NoovexOS\n"); return (int)(o-out); }
    if(sx_eq(rel,"kernel/osrelease")){ o=sx_s(o,"10.86\n"); return (int)(o-out); }
    if(sx_eq(rel,"kernel/hostname")){ o=sx_s(o,g_hostname); *o++='\n'; *o=0; return (int)(o-out); }
    if(sx_eq(rel,"kernel/arch")){ o=sx_s(o,"x86_64\n"); return (int)(o-out); }
    if(sx_eq(rel,"power/state")){ o=sx_s(o,"freeze\n"); return (int)(o-out); }
    if(sx_eq(rel,"power/mem_sleep")){ o=sx_s(o,"[s2idle]\n"); return (int)(o-out); }
    if(sx_pre(rel,"devices/system/cpu/")){ extern volatile int g_cores_online; int n=g_smp_on?g_cores_online:1; const char* f=rel+19;
        if(sx_eq(f,"kernel_max")){ o=sx_u(o,(u64)(NCPU-1)); *o++='\n'; return (int)(o-out); }
        if(n<=1)o=sx_s(o,"0\n"); else { o=sx_s(o,"0-"); o=sx_u(o,(u64)(n-1)); *o++='\n'; *o=0; } return (int)(o-out); }
    return -1; }
int sx_stat(const char* p,int follow,u32* mode,u64* size){
    { int lv=lxn_sx_stat(p,mode,size); if(lv!=-9999)return lv; }
    if(sx_pre(p,"/proc/sys/vm/")){ int k=pv_name(p); if(!k)return 0; *mode=k==1?0100200u:0100644u; *size=0; return 1; }
    if(!sx_pre(p,"/sys"))return 0; if(p[4]&&p[4]!='/')return 0; char cp[200]; if(!sx_canon(p,cp,follow))return -1; sxd* d; const char* a; int k=sx_node(cp,&d,&a); if(k==0)return 0; if(k<0)return -1;
    if(k==4){ *mode=0040755u; *size=0; return 1; } if(k==10){ *mode=0120777u; *size=0; return 1; }
    static char b[4096]; int n=sx_read(cp,b,4096); int w=a&&(sx_eq(a,"uevent")||sx_eq(a,"brightness")); if(sx_eq(cp,"/sys/power/state"))w=1; *mode=w?0100644u:0100444u; *size=n<0?0:(u64)n; return 1; }
int sx_write(const char* p,const char* in,int n){
    { int lv=lxn_sx_write(p,in,n); if(lv!=-9999)return lv; }
    if(sx_pre(p,"/proc/sys/vm/")){ char w[16]; int l=0; while(l<n&&l<15&&in[l]!='\n'&&in[l]!=' '){ w[l]=in[l]; l++; } w[l]=0; return pv_write(p,w,n); }
    char cp[200]; if(!sx_canon(p,cp,1))return -LX_ENOENT; sxd* d; const char* a; int k=sx_node(cp,&d,&a); if(k!=8)return -LX_EACCES;
    char w[16]; int l=0; while(l<n&&l<15&&in[l]!='\n'&&in[l]!=' '){ w[l]=in[l]; l++; } w[l]=0;
    if(sx_eq(cp,"/sys/power/state")){ if(g_uid!=0)return -LX_EACCES; if(sx_eq(w,"freeze")||sx_eq(w,"mem")){ extern volatile int g_sleep_req; g_sleep_req=1; return n; } return -LX_EINVAL; }
    if(!d||!a)return -LX_EACCES;
    if(sx_eq(a,"uevent")){ if(g_uid!=0)return -LX_EACCES; if(sx_eq(w,"add")||sx_eq(w,"change")||sx_eq(w,"remove")||sx_eq(w,"online")||sx_eq(w,"offline")){ sx_emit(d,w); return n; } return -LX_EINVAL; }
    if(sx_eq(a,"brightness")&&d->kind==SXK_BL){ if(g_uid!=0)return -LX_EACCES; int v=0, any=0; for(int i=0;i<l;i++){ if(w[i]<'0'||w[i]>'9')return -LX_EINVAL; v=v*10+(w[i]-'0'); any=1; } if(!any||v>100)return -LX_EINVAL; bl_set(v); sx_emit(d,"change"); return n; }
    return -LX_EACCES; }
#define LX_BLKD 118
#define LX_SYSW 119
#define DXF_MAX 32
static struct { int used, sx; u64 off; sxd d; } g_dxf[DXF_MAX];
static sxd* dx_find(const char* p){ if(!sx_pre(p,"/dev/"))return 0; const char* n=p+5; sx_fresh(); for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if((d->kind==SXK_DISK||d->kind==SXK_PART||d->kind==SXK_USB)&&sx_eq(d->devname,n))return d; } return 0; }
int dx_list(const char* dir,int idx,char* name,int* type){
    sx_fresh(); int c=0;
#define DXE(nm,tp) do{ if(c==idx){ sx_cpy(name,nm,64); *type=tp; return 1; } c++; }while(0)
    if(sx_eq(dir,"/dev")){ for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind==SXK_DISK||d->kind==SXK_PART)DXE(d->devname,6); } for(int i=0;i<g_sxn;i++) if(g_sx[i].kind==SXK_USB){ DXE("bus",4); break; } DXE("input",4); DXE("pts",4); DXE("shm",4); if(g_snd_state==1)DXE("snd",4); DXE("ptmx",2); return 0; }
    if(sx_eq(dir,"/dev/bus")){ DXE("usb",4); return 0; }
    if(sx_eq(dir,"/dev/bus/usb")){ int seen=0; for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind!=SXK_USB)continue; int b=g_usbdev[d->idx].ctl+1; if(seen&(1<<b))continue; seen|=1<<b; char nm[8]; nm[0]=(char)('0'+b/100%10); nm[1]=(char)('0'+b/10%10); nm[2]=(char)('0'+b%10); nm[3]=0; DXE(nm,4); } return 0; }
    if(sx_pre(dir,"/dev/bus/usb/")&&sx_len(dir)==16){ for(int i=0;i<g_sxn;i++){ sxd* d=&g_sx[i]; if(d->kind!=SXK_USB||!sx_pre(d->devname+8,dir+13))continue; DXE(d->devname+12,2); } return 0; }
    if(sx_eq(dir,"/dev/input")){ DXE("mice",2); return 0; }
    return 0;
#undef DXE
}
int dx_isdir(const char* p){ if(sx_eq(p,"/dev/input")||sx_eq(p,"/dev/bus")||sx_eq(p,"/dev/bus/usb"))return 1; if(sx_pre(p,"/dev/bus/usb/")&&sx_len(p)==16){ char nm[64]; int t; for(int i=0;dx_list("/dev/bus/usb",i,nm,&t);i++) if(sx_eq(nm,p+13))return 1; return 0; } return 0; }
int dx_stat(const char* p,u32* mode,u64* rdev,u64* size){
    if(dx_isdir(p)){ *mode=0040755u; *rdev=0; *size=0; return 1; }
    sxd* d=dx_find(p); if(!d)return 0;
    if(d->kind==SXK_USB){ *mode=0020664u; *rdev=((u64)d->major<<8)|(u64)(d->minor&0xFF)|((u64)(d->minor&~0xFF)<<12); *size=0; return 1; }
    *mode=0060660u; *rdev=((u64)d->major<<8)|(u64)(d->minor&0xFF)|((u64)(d->minor&~0xFF)<<12); *size=0; return 1; }
static int dx_rd512(const sxd* d,u64 lba,u8* buf){ u64 a=lba+(d->kind==SXK_PART?d->start:0); if(a>0xFFFFFFFFULL)return -1; if(d->blkdisk==-2)return usb_msd_read((u32)a,1,buf)?-1:0; if(d->blkdisk<0)return -1; return gdisk_read_on(d->blkdisk,(u32)a,1,buf)?-1:0; }
long dx_open(const char* p,int flags){
    if(dx_isdir(p)){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    sxd* d=dx_find(p); if(!d)return -9999;
    if(g_uid!=0)return -LX_EACCES;
    if((flags&3)!=0)return -30;
    if(d->kind==SXK_USB){ static char b[64]; int n=sx_attr_read(d,"descriptors",b,64); if(n<0)return -LX_ENODEV; return lx_text_fd(b,(u64)n,flags&~3); }
    int k=-1; for(int i=0;i<DXF_MAX;i++) if(!g_dxf[i].used){ k=i; break; } if(k<0)return -LX_EMFILE;
    g_dxf[k].used=1; g_dxf[k].off=0; g_dxf[k].d=*d;
    int vi=jc_newvf(LX_BLKD,k,flags&~0x80000); if(vi<0){ g_dxf[k].used=0; return -LX_EMFILE; }
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
void dx_close(int k){ if(k>=0&&k<DXF_MAX)g_dxf[k].used=0; }
long dx_pread(int k,u64 buf,u64 len,long off,int adv){
    if(k<0||k>=DXF_MAX||!g_dxf[k].used)return -LX_EBADF; sxd* d=&g_dxf[k].d; u64 o=off<0?g_dxf[k].off:(u64)off; u64 tot=d->size*512ULL;
    if(o>=tot)return 0; if(len>tot-o)len=tot-o; if(len>LX_IOMAX)len=LX_IOMAX; if(!lx_okw(buf,len))return -LX_EFAULT;
    static u8 sec[512]; u64 done=0;
    while(done<len){ u64 at=o+done; u64 lba=at/512; u32 in=(u32)(at%512); if(dx_rd512(d,lba,sec))return done?(long)done:-5; u64 c=512-in; if(c>len-done)c=len-done; u8* dst=(u8*)(buf+done); for(u64 i=0;i<c;i++)dst[i]=sec[in+i]; done+=c; }
    if(adv)g_dxf[k].off=o+done; return (long)done; }
long dx_lseek(int k,long off,int wh){ if(k<0||k>=DXF_MAX||!g_dxf[k].used)return -LX_EBADF; u64 tot=g_dxf[k].d.size*512ULL; long base=wh==0?0:wh==1?(long)g_dxf[k].off:wh==2?(long)tot:-1; if(base<0&&wh!=0)return -LX_EINVAL; long n=base+off; if(n<0)return -LX_EINVAL; g_dxf[k].off=(u64)n; return n; }
long dx_ioctl(int k,u32 cmd,u64 arg){
    if(k<0||k>=DXF_MAX||!g_dxf[k].used)return -LX_EBADF; sxd* d=&g_dxf[k].d;
    if(cmd==0x80081272u){ u64 v=d->size*512ULL; return lx_put(arg,&v,8); }
    if(cmd==0x1260u){ u64 v=d->size; if(v>0xFFFFFFFFULL)v=0xFFFFFFFFULL; unsigned long lv=(unsigned long)v; return lx_put(arg,&lv,8); }
    if(cmd==0x1268u||cmd==0x127Bu){ int v=512; return lx_put(arg,&v,4); }
    if(cmd==0x125Eu){ int v=0; return lx_put(arg,&v,4); }
    if(cmd==0x1261u||cmd==0x125Du){ return 0; }
    return -LX_ENOTTY; }
long sx_open(const char* p,int flags){
    { long lv=lxn_sx_open(p,flags); if(lv!=-9999)return lv; }
    if(sx_pre(p,"/proc/sys/vm/")){ int k=pv_name(p); if(!k)return -9999; if((flags&3)!=0){ if(g_uid!=0)return -LX_EACCES; int vi=jc_newvf(LX_SYSW,0,flags&~0x80000); if(vi<0)return -LX_EMFILE; int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
        if(k==1)return -LX_EACCES; char b[16]; int n=pv_read(p,b); return lx_text_fd(b,(u64)n,flags); }
    if(!sx_pre(p,"/sys"))return -9999; if(p[4]&&p[4]!='/')return -9999; char cp[200]; if(!sx_canon(p,cp,1))return -LX_ENOENT; sxd* d; const char* a; int k=sx_node(cp,&d,&a); if(k==0)return -9999; if(k<0)return -LX_ENOENT;
    if(k==4){ if((flags&3)!=0)return -LX_EISDIR; int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; int q=0; for(;cp[q]&&q<127;q++)g_vf[vi].vpath[q]=cp[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    if(k!=8)return -LX_ENOENT;
    if((flags&3)!=0&&g_uid!=0)return -LX_EACCES;
    if((flags&3)!=0){ int vi=jc_newvf(LX_SYSW,0,flags&~0x80000); if(vi<0)return -LX_EMFILE; int q=0; for(;cp[q]&&q<127;q++)g_vf[vi].vpath[q]=cp[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    static char b[4096]; int n=sx_read(cp,b,4096); if(n<0)return -LX_ENOENT; return lx_text_fd(b,(u64)n,flags); }
long sx_wfd_write(const char* vp,u64 buf,u64 len){ if(len>4096)len=4096; if(!lx_ok(buf,len))return -LX_EFAULT; static char t[4097]; for(u64 i=0;i<len;i++)t[i]=((const char*)buf)[i]; t[len]=0; return sx_write(vp,t,(int)len); }
long sx_wfd_read(const char* vp,u64 buf,u64 len,int* off){ static char b[4096]; int n=sx_read(vp,b,4096); if(n<0)return -LX_EIO; if(*off<0)return -LX_EINVAL; if(*off>=n)return 0; u64 c=(u64)(n-*off); if(c>len)c=len; if(!lx_okw(buf,c))return -LX_EFAULT; for(u64 i=0;i<c;i++)((char*)buf)[i]=b[*off+i]; *off+=(int)c; return (long)c; }
