#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/drivers.h"
/* =========================================================================
   Networking  -  faithful port of the proven Noovex9 (32-bit) stack.
   PCnet (Am79C970/973) NIC over I/O ports, polled RX.  DMA rings live in
   .bss (identity-mapped, phys == virt, < 4 GiB as the 32-bit PCnet needs).
   In VirtualBox set the adapter to "PCnet-FAST III (Am79C973)", NAT mode.
   ========================================================================= */
u16 pit_read(void){ outb(0x43,0x00); u8 lo=inb(0x40); u8 hi=inb(0x40); return ((u16)hi<<8)|lo; }
/* visible network log (since this can only be tested on real hardware) */
char NETLOG[18][52]; int netlog_n=0;
void netlog(const char*s){ int i=netlog_n%18; int k=0; while(s[k]&&k<51){NETLOG[i][k]=s[k];k++;} NETLOG[i][k]=0; netlog_n++; { char b[72]; int o=0; const char* pf="net: "; while(*pf)b[o++]=*pf++; for(int j=0;s[j]&&o<71;j++)b[o++]=s[j]; b[o]=0; klog(b); } }

void be16(u8*b,u16 v){ b[0]=v>>8; b[1]=(u8)v; }
void be32(u8*b,u32 v){ b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=(u8)v; }
u16 rd16(const u8*b){ return ((u16)b[0]<<8)|b[1]; }
u32 rd32(const u8*b){ return ((u32)b[0]<<24)|((u32)b[1]<<16)|((u32)b[2]<<8)|b[3]; }
u16 cksum(const u8*d,int n){ u32 s=0; for(int i=0;i+1<n;i+=2)s+=((u32)d[i]<<8)|d[i+1]; if(n&1)s+=(u32)d[n-1]<<8; while(s>>16)s=(s&0xFFFF)+(s>>16); return (u16)~s; }
static u32 net_rng=0;
u32 net_rand(void){ u32 a,d; { u64 t_=cpu_tsc(); a=(u32)t_; d=(u32)(t_>>32); }; if(!net_rng)net_rng=a^d^0x9E3779B9u; u32 x=net_rng; x^=x<<13;x^=x>>17;x^=x<<5; net_rng=x; return x^a; }

/* ---- PCnet DMA memory (in .bss, aligned) ---- */


u16 nic_io=0; int nic_bus=0,nic_dev=0,nic_present=0,nic_init=0,net_ok=0;
int nic_kind=0; struct nvx_nic g_mnic;
int nic_rx_idx=0,nic_tx_idx=0;
u8  my_mac[6];
u32 my_ip=0x0A00020Fu, gw_ip=0x0A000202u, dns_ip=0x0A000203u, my_mask=0xFFFFFF00u;
int g_dhcp_done=0, g_dhcp_ok=0;

u8  gw_mac[6]; int gw_known=0;
u16 ping_ms=0;

void pci_wcfg(int bus,int slot,int fn,int off,u32 v){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); outl(0xCFC,v); }
#define NIC_MAX 8
static struct nvx_nic g_nics[NIC_MAX]; static int g_nic_used[NIC_MAX]; int g_nic_cur=-1, g_nic_pref=-1; static int g_nic_was_link=-1; static u64 g_nic_wt=0;
extern volatile u64 g_ticks; extern int g_netdev_wifi;
int nic_add(const struct nvx_nic* n){ for(int i=0;i<NIC_MAX;i++) if(!g_nic_used[i]){ g_nics[i]=*n; g_nic_used[i]=1; return i; } return -1; }
int nic_count(void){ int c=0; for(int i=0;i<NIC_MAX;i++) if(g_nic_used[i])c++; return c; }
const char* nic_list(int i,int* link,int* cur){ if(i<0||i>=NIC_MAX||!g_nic_used[i])return 0; if(link)*link=g_nics[i].link?g_nics[i].link():1; if(cur)*cur=(i==g_nic_cur&&net_ok&&nic_kind==2); return g_nics[i].name; }
const char* nic_active_name(void){ if(!net_ok)return 0; if(nic_kind==2)return g_mnic.name; if(nic_kind==0&&nic_present)return "AMD PCnet"; return 0; }
int nic_pick(void){ int first=-1; for(int i=0;i<NIC_MAX;i++) if(g_nic_used[i]){ if(first<0)first=i; if(!g_nics[i].link||g_nics[i].link())return i; } return first; }
void nic_use(int i){
    if(i<0||i>=NIC_MAX||!g_nic_used[i])return;
    int change=g_nic_cur>=0&&g_nic_cur!=i;
    g_mnic=g_nics[i]; g_nic_cur=i; nic_present=1; nic_kind=2; nic_init=1; net_ok=1;
    for(int k=0;k<6;k++)my_mac[k]=g_mnic.mac[k];
    if(change||g_dhcp_done){ g_dhcp_done=0; g_dhcp_ok=0; gw_known=0; }
    g_nic_was_link=-1;
    { extern void dhcp_retry_reset(void); dhcp_retry_reset(); }
    { char b[52],*p=b; p=scopy(p,"NIC up  MAC "); for(int k=0;k<6;k++){ p=hx(p,my_mac[k],2); if(k<5)*p++=':'; } *p=0; netlog(g_mnic.name?g_mnic.name:"network adapter"); netlog(b); }
}
void nic_offer(int i){
    if(i<0||i>=NIC_MAX||!g_nic_used[i])return;
    if(!nic_init||i==g_nic_cur)return;
    if(nic_kind==0&&net_ok)return;
    if(!net_ok||g_nic_cur<0||(g_mnic.link&&!g_mnic.link())){ nic_use(i); g_nic_pref=i; }
}
void nic_drop(int i){
    if(i<0||i>=NIC_MAX||!g_nic_used[i])return;
    g_nic_used[i]=0;
    if(g_nic_pref==i)g_nic_pref=-1;
    if(g_nic_cur!=i)return;
    g_nic_cur=-1;
    int k=nic_pick(); if(k>=0){ nic_use(k); return; }
    net_ok=0; nic_present=0; g_dhcp_ok=0; g_dhcp_done=0; gw_known=0; g_mnic.name=0; g_mnic.send=0; g_mnic.recv=0; g_mnic.link=0;
    netlog("network adapter removed");
}
int nic_probe_all(void){
    struct nvx_nic t; int n=0;
    int (*pr[])(struct nvx_nic*)={e1000_probe,e1000e_probe,igb_probe,rtl8169_probe,rtl8139_probe,vmxnet3_probe,virtio_net_probe,0};
    for(int i=0;pr[i];i++){ for(int k=0;k<(int)sizeof t;k++)((u8*)&t)[k]=0; if(pr[i](&t)){ if(nic_add(&t)>=0)n++; } }
    { extern int usbnet_scan(void) __attribute__((weak)); int (*volatile us)(void)=usbnet_scan; if(us)us(); n=nic_count(); }
    return n;
}
void nic_watch(void){
    if(nic_kind!=2||!nic_init||g_netdev_wifi)return;
    if(g_ticks-g_nic_wt<200)return;
    g_nic_wt=g_ticks;
    if(g_nic_cur<0||!g_mnic.link)return;
    int l=g_mnic.link();
    if(g_nic_pref>=0&&g_nic_pref!=g_nic_cur&&g_nic_used[g_nic_pref]&&g_nics[g_nic_pref].link&&g_nics[g_nic_pref].link()){ netlog("link back on the first adapter - switching back"); nic_use(g_nic_pref); return; }
    if(!l){ for(int i=0;i<NIC_MAX;i++) if(g_nic_used[i]&&i!=g_nic_cur&&g_nics[i].link&&g_nics[i].link()){ netlog("no link - switching adapter"); nic_use(i); return; } }
    if(g_nic_was_link==0&&l){ netlog("cable connected"); if(!g_dhcp_ok)g_dhcp_done=0; }
    if(g_nic_was_link==1&&!l)netlog("cable disconnected");
    g_nic_was_link=l;
}
