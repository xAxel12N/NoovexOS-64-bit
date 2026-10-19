#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
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