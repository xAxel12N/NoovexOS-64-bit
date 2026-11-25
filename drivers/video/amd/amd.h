#ifndef NVX_DRIVERS_VIDEO_AMD_H
#define NVX_DRIVERS_VIDEO_AMD_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

enum { AMD_IP_GC, AMD_IP_DCN, AMD_IP_MP0, AMD_IP_MP1, AMD_IP_NBIO, AMD_IP_SDMA0, AMD_IP_MMHUB, AMD_IP_ATHUB, AMD_IP_HDP, AMD_IP_OSSSYS,
       AMD_IP_SMUIO, AMD_IP_THM, AMD_IP_CLK, AMD_IP_DF, AMD_IP_UMC, AMD_IP_VCN, AMD_IP_MAX };
#define AMD_IP_SEGS 6

enum { AMD_FAM_UNKNOWN, AMD_FAM_RV, AMD_FAM_NV, AMD_FAM_VGH, AMD_FAM_YC, AMD_FAM_GC_10_3_6, AMD_FAM_GC_10_3_7, AMD_FAM_GC_11_0_1, AMD_FAM_GC_11_5_0 };
enum { AMD_VBIOS_NONE, AMD_VBIOS_VFCT, AMD_VBIOS_ROMBAR, AMD_VBIOS_LEGACY };

typedef struct { u8 maj, min, rev, ninst; u32 base[AMD_IP_SEGS]; } amd_ip_t;

typedef struct {
    int found, ready, apu, family;
    u8 bus, slot, fn, revid;
    u16 did, ssvid, ssdid;
    u64 mmio_pa, fb_pa, fb_len, doorbell_pa;
    volatile u8* mmio; u32 mmio_len;
    const char* asic_name;
    amd_ip_t ip[AMD_IP_MAX];
    int disc_ok;
    u8* vbios; u32 vbios_len; int vbios_src;
    u32 smn_index, smn_data;
    char why[64];
} amd_dev_t;

extern amd_dev_t g_amd;

static inline u32 amd_rd(u32 byte_off){ if(!g_amd.mmio||(u64)byte_off+4>g_amd.mmio_len)return 0xFFFFFFFFu; return *(volatile u32*)(g_amd.mmio+byte_off); }
static inline void amd_wr(u32 byte_off,u32 v){ if(!g_amd.mmio||(u64)byte_off+4>g_amd.mmio_len)return; *(volatile u32*)(g_amd.mmio+byte_off)=v; }
static inline u32 amd_rreg(u32 dw){ return dw>=0x40000000u?0xFFFFFFFFu:amd_rd(dw<<2); }
static inline void amd_wreg(u32 dw,u32 v){ if(dw<0x40000000u)amd_wr(dw<<2,v); }
static inline void amd_wreg_mask(u32 dw,u32 mask,u32 v){ amd_wreg(dw,(amd_rreg(dw)&~mask)|(v&mask)); }
static inline u32 amd_ipreg(int ip,int seg,u32 off){ if(ip<0||ip>=AMD_IP_MAX||seg<0||seg>=AMD_IP_SEGS)return 0; return g_amd.ip[ip].base[seg]+off; }
static inline u32 amd_ip_rreg(int ip,int seg,u32 off){ return amd_rreg(amd_ipreg(ip,seg,off)); }
static inline void amd_ip_wreg(int ip,int seg,u32 off,u32 v){ amd_wreg(amd_ipreg(ip,seg,off),v); }
static inline u32 amd_ipver(int ip){ if(ip<0||ip>=AMD_IP_MAX)return 0; return ((u32)g_amd.ip[ip].maj<<16)|((u32)g_amd.ip[ip].min<<8)|g_amd.ip[ip].rev; }
#define AMD_IPV(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))

static inline int amd_smn_cfg_ok(void){ return g_amd.found==1&&g_amd.apu&&(pci_cfg(0,0,0,0)&0xFFFF)==0x1022; }
static inline u32 amd_smn_rd(u32 addr){
    if(g_amd.smn_index&&g_amd.smn_data&&g_amd.mmio){ amd_wreg(g_amd.smn_index,addr); (void)amd_rreg(g_amd.smn_index); return amd_rreg(g_amd.smn_data); }
    if(!amd_smn_cfg_ok())return 0xFFFFFFFFu; pci_cfgw(0,0,0,0x60,addr); return pci_cfg(0,0,0,0x64); }
static inline void amd_smn_wr(u32 addr,u32 v){
    if(g_amd.smn_index&&g_amd.smn_data&&g_amd.mmio){ amd_wreg(g_amd.smn_index,addr); (void)amd_rreg(g_amd.smn_index); amd_wreg(g_amd.smn_data,v); (void)amd_rreg(g_amd.smn_data); return; }
    if(!amd_smn_cfg_ok())return; pci_cfgw(0,0,0,0x60,addr); pci_cfgw(0,0,0,0x64,v); }

static inline int amd_wait(u32 dw,u32 mask,u32 val,u32 us){ u64 t0=hr_ns(); for(;;){ u32 r=amd_rreg(dw); if(r==0xFFFFFFFFu)return -2; if((r&mask)==val)return 0; if(hr_ns()-t0>(u64)us*1000ULL)return -1; __builtin_ia32_pause(); } }

int amd_probe(void);
int amd_vbios_fetch(void);
void amd_init(void);
int amd_present(void);
int amd_temp_c(void);
int amd_gpu_busy_pct(void);
int amd_sclk_mhz(void);
int amd_backlight_set(int pct);
int amd_backlight_get(void);
int amd_fclk_mhz(void);
const char* amd_smu_name(void);
int amd_smu_status(char* buf,int n);
int amd_dc_scan(void);
int amd_dc_edid(u8* out,int max);
int amd_dc_info(char* buf,int n);
int amd_atom_info(char* buf,int n);
int amd_flip_test(void);
void amd_cmd(const char* arg);
int amd_tm_tile(char* p,int n);
int amd_tm_row(int i,char* label,char* val);

#endif
