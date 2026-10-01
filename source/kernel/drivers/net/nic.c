/* =========================================================================
   Networking  -  faithful port of the proven Noovex9 (32-bit) stack.
   PCnet (Am79C970/973) NIC over I/O ports, polled RX.  DMA rings live in
   .bss (identity-mapped, phys == virt, < 4 GiB as the 32-bit PCnet needs).
   In VirtualBox set the adapter to "PCnet-FAST III (Am79C973)", NAT mode.
   ========================================================================= */
static u16 pit_read(void){ outb(0x43,0x00); u8 lo=inb(0x40); u8 hi=inb(0x40); return ((u16)hi<<8)|lo; }
/* visible network log (since this can only be tested on real hardware) */
static char NETLOG[18][52]; static int netlog_n=0;
static void netlog(const char*s){ int i=netlog_n%18; int k=0; while(s[k]&&k<51){NETLOG[i][k]=s[k];k++;} NETLOG[i][k]=0; netlog_n++; }

static void be16(u8*b,u16 v){ b[0]=v>>8; b[1]=(u8)v; }
static void be32(u8*b,u32 v){ b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=(u8)v; }
static u16 rd16(const u8*b){ return ((u16)b[0]<<8)|b[1]; }
static u32 rd32(const u8*b){ return ((u32)b[0]<<24)|((u32)b[1]<<16)|((u32)b[2]<<8)|b[3]; }
static u16 cksum(const u8*d,int n){ u32 s=0; for(int i=0;i+1<n;i+=2)s+=((u32)d[i]<<8)|d[i+1]; if(n&1)s+=(u32)d[n-1]<<8; while(s>>16)s=(s&0xFFFF)+(s>>16); return (u16)~s; }
static u32 net_rng=0;
static u32 net_rand(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); if(!net_rng)net_rng=a^d^0x9E3779B9u; u32 x=net_rng; x^=x<<13;x^=x>>17;x^=x<<5; net_rng=x; return x^a; }

/* ---- PCnet DMA memory (in .bss, aligned) ---- */
#define RXR 8
static u8 nic_initb[32] __attribute__((aligned(16)));
static u8 nic_rxd[RXR*16] __attribute__((aligned(16)));
static u8 nic_txd[2*16]   __attribute__((aligned(16)));
static u8 nic_rxb[RXR*2048] __attribute__((aligned(16)));
static u8 nic_txb[2048]   __attribute__((aligned(16)));
/* ---- e1000 DMA memory (in .bss, aligned, identity-mapped, <4GB) ---- */
#define E1K_NRX 16
#define E1K_NTX 8
static u8 e1k_rxd[E1K_NRX*16] __attribute__((aligned(16)));
static u8 e1k_txd[E1K_NTX*16] __attribute__((aligned(16)));
static u8 e1k_rxb[E1K_NRX*2048] __attribute__((aligned(16)));
static u8 e1k_txb[E1K_NTX*2048] __attribute__((aligned(16)));
static inline u32 A32(void*p){ return (u32)(u64)p; }

static u16 nic_io=0; static int nic_bus=0,nic_dev=0,nic_present=0,nic_init=0,net_ok=0;
static int nic_kind=0;                    /* 0 = PCnet (proven), 1 = e1000 (from spec) */
static u32 e1k_bar=0; static volatile u32* e1k=0; static int e1k_rx_i=0,e1k_tx_i=0;
static int nic_rx_idx=0,nic_tx_idx=0;
static u8  my_mac[6];
static u32 my_ip=0x0A00020Fu, gw_ip=0x0A000202u, dns_ip=0x0A000203u, my_mask=0xFFFFFF00u;
static int g_dhcp_done=0, g_dhcp_ok=0;
static int nic_up(void);
static u8  gw_mac[6]; static int gw_known=0;
static u16 ping_ms=0;

static void pci_wcfg(int bus,int slot,int fn,int off,u32 v){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); outl(0xCFC,v); }