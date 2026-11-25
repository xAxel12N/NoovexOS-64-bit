#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "drivers/video/amd/amd.h"
const char* gpu_name(u16 ven,u16 dev);


#define AC_mmMM_INDEX 0x0
#define AC_mmMM_DATA 0x1
#define AC_mmMM_INDEX_HI 0x6
#define AC_mmRCC_CONFIG_MEMSIZE 0xde3
#define AC_mmDRIVER_SCRATCH_0 0x94
#define AC_mmDRIVER_SCRATCH_1 0x95
#define AC_mmDRIVER_SCRATCH_2 0x96
#define AC_DISC_SIG 0x28211407u
#define AC_DISC_TBL_SIG 0x53445049u
#define AC_DISC_TMR_OFF (64u<<10)
#define AC_DISC_SIZE (10u<<10)

static u8 g_amd_disc[AC_DISC_SIZE];
u32 g_amd_disc_len, g_amd_vram_mb, g_amd_disc_ver, g_amd_disc_nips;
u64 g_amd_disc_off;

static void ac_cpy(char* d,const char* s,int n){ int i=0; for(;i<n-1&&s[i];i++)d[i]=s[i]; d[i]=0; }
static u16 ac_rd16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 ac_rd32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

static int ac_is_apu(u16 d){
    static const u16 apu[]={0x15DD,0x15D8,0x1636,0x164C,0x1638,0x15E7,0x163F,0x1681,0x164D,0x164E,0x1506,0x15BF,0x15C8,0x150E,0x1586,0x1114,0x13C0};
    for(u32 i=0;i<sizeof apu/sizeof apu[0];i++){ if(apu[i]==d)return 1; }
    return 0;
}

static u64 ac_bar_len(int b,int s,int f,int off,int is64){
    u32 cmd=pci_cfg(b,s,f,0x04), lo=pci_cfg(b,s,f,off), hi=is64?pci_cfg(b,s,f,off+4):0;
    u64 fl=cpu_irq_save(); pci_cfgw(b,s,f,0x04,cmd&~3u);
    pci_cfgw(b,s,f,off,0xFFFFFFFFu); u32 ml=pci_cfg(b,s,f,off), mh=0xFFFFFFFFu; if(is64){ pci_cfgw(b,s,f,off+4,0xFFFFFFFFu); mh=pci_cfg(b,s,f,off+4); pci_cfgw(b,s,f,off+4,hi); }
    pci_cfgw(b,s,f,off,lo); pci_cfgw(b,s,f,0x04,cmd); cpu_irq_restore(fl);
    u64 m=((u64)mh<<32)|(ml&0xFFFFFFF0u); if(!is64)m|=0xFFFFFFFF00000000ULL; if(!(ml&0xFFFFFFF0u)&&!(is64&&mh))return 0;
    return ~m+1; }
static void ac_bars(int b,int s,int f){
    for(int i=0;i<6;){
        int off=0x10+i*4; u32 v=pci_cfg(b,s,f,off);
        if(v&1){ i++; continue; }
        int is64=((v>>1)&3)==2; u64 pa=v&0xFFFFFFF0u; if(is64&&i<5)pa|=(u64)pci_cfg(b,s,f,off+4)<<32;
        if(i==0&&pa){ g_amd.fb_pa=pa; g_amd.fb_len=ac_bar_len(b,s,f,off,is64); }
        else if(i==2&&pa&&!g_amd.doorbell_pa)g_amd.doorbell_pa=pa;
        else if(i==5&&pa){ g_amd.mmio_pa=pa; g_amd.mmio_len=0x80000u; }
        i+=is64?2:1;
    }
}

static u32 ac_vram_rd(u64 pos){ amd_wreg(AC_mmMM_INDEX,((u32)pos)|0x80000000u); amd_wreg(AC_mmMM_INDEX_HI,(u32)(pos>>31)); return amd_rreg(AC_mmMM_DATA); }

static int ac_ipidx(u16 hw){
    switch(hw){ case 11: return AMD_IP_GC; case 271: return AMD_IP_DCN; case 255: return AMD_IP_MP0; case 1: return AMD_IP_MP1;
    case 108: return AMD_IP_NBIO; case 42: return AMD_IP_SDMA0; case 34: return AMD_IP_MMHUB; case 35: return AMD_IP_ATHUB;
    case 41: return AMD_IP_HDP; case 40: return AMD_IP_OSSSYS; case 4: return AMD_IP_SMUIO; case 3: return AMD_IP_THM;
    case 6: return AMD_IP_CLK; case 46: return AMD_IP_DF; case 150: return AMD_IP_UMC; case 12: return AMD_IP_VCN; }
    return -1;
}

int amd_disc_parse(const u8* b,u32 len){
    if(len<20||ac_rd32(b)!=AC_DISC_SIG)return -1;
    u16 bsz=ac_rd16(b+10); if(bsz<12||bsz>len)return -2;
    u16 sum=0; for(u32 i=10;i<bsz;i++)sum=(u16)(sum+b[i]); if(sum!=ac_rd16(b+8))return -3;
    u16 vmaj=ac_rd16(b+4); u32 toff=vmaj>=2?16:12; u16 off=ac_rd16(b+toff); g_amd_disc_ver=vmaj;
    if((u32)off+80>bsz)return -4;
    const u8* h=b+off; if(ac_rd32(h)!=AC_DISC_TBL_SIG)return -5;
    u16 hver=ac_rd16(h+4), hsz=ac_rd16(h+6); if(!hsz||(u32)off+hsz>bsz)return -6;
    int b64=hver==4&&(h[78]&1); u16 nd=ac_rd16(h+12); if(nd<1||nd>16)return -7;
    for(int i=0;i<AMD_IP_MAX;i++){ g_amd.ip[i].maj=g_amd.ip[i].min=g_amd.ip[i].rev=0; g_amd.ip[i].ninst=0; }
    g_amd_disc_nips=0;
    for(int d=0;d<nd;d++){
        u32 doff=ac_rd16(h+14+d*4+2); if(doff+4>bsz)return -8;
        u16 nip=ac_rd16(b+doff+2); u32 p=doff+4;
        for(u32 k=0;k<nip;k++){
            if(p+8>bsz)return -9;
            u16 hw=ac_rd16(b+p); u8 nb=b[p+3]; u32 esz=b64?8u:4u; if(p+8+(u32)nb*esz>bsz)return -10;
            int x=ac_ipidx(hw); g_amd_disc_nips++;
            if(x>=0){ amd_ip_t* ip=&g_amd.ip[x];
                if(!ip->ninst){ ip->maj=b[p+4]; ip->min=b[p+5]; ip->rev=b[p+6];
                    for(u32 s=0;s<AMD_IP_SEGS;s++)ip->base[s]=s<nb?(ac_rd32(b+p+8+s*esz)&(b64?0x3FFFFFFFu:0xFFFFFFFFu)):0; }
                if(ip->ninst<255)ip->ninst++; }
            p+=8+(u32)nb*esz;
        }
    }
    return 0;
}

static int ac_disc_read(void){
    u32 mem=amd_rreg(AC_mmRCC_CONFIG_MEMSIZE); if(mem==0xFFFFFFFFu){ ac_cpy(g_amd.why,"MMIO reads all ones",64); return -1; }
    g_amd_vram_mb=mem; u64 off=0;
    if(mem){ off=((u64)mem<<20)-AC_DISC_TMR_OFF; if(ac_vram_rd(off)!=AC_DISC_SIG)off=0; }
    u32 tsz=AC_DISC_SIZE;
    if(!off){ u32 s2=amd_rreg(AC_mmDRIVER_SCRATCH_2);
        if(s2&&s2!=0xFFFFFFFFu&&mem){ off=((u64)amd_rreg(AC_mmDRIVER_SCRATCH_1)<<32)|amd_rreg(AC_mmDRIVER_SCRATCH_0); if(s2<tsz)tsz=s2; } }
    if(!off||!mem){ ac_cpy(g_amd.why,"no discovery table in VRAM",64); return -2; }
    tsz&=~3u; for(u32 i=0;i<tsz;i+=4){ u32 v=ac_vram_rd(off+i); g_amd_disc[i]=(u8)v; g_amd_disc[i+1]=(u8)(v>>8); g_amd_disc[i+2]=(u8)(v>>16); g_amd_disc[i+3]=(u8)(v>>24); }
    g_amd_disc_off=off; g_amd_disc_len=tsz;
    int r=amd_disc_parse(g_amd_disc,tsz); if(r){ ac_cpy(g_amd.why,"discovery table invalid",64); return -3; }
    return 0;
}

static void ac_setip(int x,int a,int b,int c){ g_amd.ip[x].maj=(u8)a; g_amd.ip[x].min=(u8)b; g_amd.ip[x].rev=(u8)c; g_amd.ip[x].ninst=1; }

static void ac_legacy(void){
    u16 d=g_amd.did; if(d!=0x15DD&&d!=0x15D8)return;
    int r2=g_amd.revid>=0x8;
    if(d==0x15D8||r2){ ac_setip(AMD_IP_GC,9,2,2); ac_setip(AMD_IP_DCN,1,0,1); ac_setip(AMD_IP_MP1,10,0,1); ac_setip(AMD_IP_NBIO,7,0,1); }
    else { ac_setip(AMD_IP_GC,9,1,0); ac_setip(AMD_IP_DCN,1,0,0); ac_setip(AMD_IP_MP1,10,0,0); ac_setip(AMD_IP_NBIO,7,0,0); }
}

const char* amd_asic_name(u32 gc,u16 did){
    switch(gc){
    case AMD_IPV(9,1,0): return "Raven";
    case AMD_IPV(9,2,2): return did==0x15D8?"Picasso":"Raven2";
    case AMD_IPV(9,3,0): return did==0x164C?"Lucienne":did==0x1638?"Cezanne":did==0x15E7?"Barcelo":"Renoir";
    case AMD_IPV(10,1,10): return "Navi 10"; case AMD_IPV(10,1,1): return "Navi 12"; case AMD_IPV(10,1,2): return "Navi 14";
    case AMD_IPV(10,3,0): return "Navi 21"; case AMD_IPV(10,3,2): return "Navi 22"; case AMD_IPV(10,3,4): return "Navi 23";
    case AMD_IPV(10,3,5): return "Navi 24"; case AMD_IPV(10,3,1): return "Van Gogh"; case AMD_IPV(10,3,3): return "Rembrandt";
    case AMD_IPV(10,3,6): return "Raphael"; case AMD_IPV(10,3,7): return "Mendocino";
    case AMD_IPV(11,0,0): return "Navi 31"; case AMD_IPV(11,0,2): return "Navi 33"; case AMD_IPV(11,0,3): return "Navi 32";
    case AMD_IPV(11,0,1): return "Phoenix"; case AMD_IPV(11,0,4): return "Phoenix2";
    case AMD_IPV(11,5,0): return "Strix Point"; case AMD_IPV(11,5,1): return "Strix Halo"; case AMD_IPV(11,5,2): return "Krackan";
    case AMD_IPV(12,0,0): return "Navi 44"; case AMD_IPV(12,0,1): return "Navi 48";
    }
    return "unknown AMD GPU";
}

static int ac_family(u32 gc){
    if(gc>=AMD_IPV(9,0,0)&&gc<AMD_IPV(10,0,0))return AMD_FAM_RV;
    switch(gc){ case AMD_IPV(10,3,1): return AMD_FAM_VGH; case AMD_IPV(10,3,3): return AMD_FAM_YC; case AMD_IPV(10,3,6): return AMD_FAM_GC_10_3_6;
    case AMD_IPV(10,3,7): return AMD_FAM_GC_10_3_7; case AMD_IPV(11,0,1): case AMD_IPV(11,0,4): return AMD_FAM_GC_11_0_1;
    case AMD_IPV(11,5,0): case AMD_IPV(11,5,1): case AMD_IPV(11,5,2): return AMD_FAM_GC_11_5_0; }
    if(gc>=AMD_IPV(10,0,0))return AMD_FAM_NV;
    return AMD_FAM_UNKNOWN;
}

static void ac_smn(void){
    amd_ip_t* n=&g_amd.ip[AMD_IP_NBIO]; if(!n->ninst||n->maj!=7)return;
    u32 o=n->min==11?0x800Eu:(n->min==0||n->min==2||n->min==7)?0x000Eu:0; if(!o)return;
    g_amd.smn_index=n->base[0]+o; g_amd.smn_data=n->base[0]+o+1;
}

int amd_probe(void){
    if(g_amd.found&&g_amd.mmio)return 0;
    extern int pci_n; extern void pci_scan(void); if(!pci_n)pci_scan();
    int got=0;
    for(int k=0;k<pci_n&&!got;k++){
        if(pci_ven[k]!=0x1002||pci_cls[k]!=0x03)continue;
        int b=pci_bus[k],s=pci_slot[k],f=0;
        for(f=0;f<8;f++){ u32 id=pci_cfg(b,s,f,0); if((id&0xFFFF)==0x1002&&((pci_cfg(b,s,f,0x08)>>24)&0xFF)==0x03)break; }
        if(f==8)continue;
        u32 id=pci_cfg(b,s,f,0); g_amd.bus=(u8)b; g_amd.slot=(u8)s; g_amd.fn=(u8)f; g_amd.did=(u16)(id>>16);
        g_amd.revid=(u8)pci_cfg(b,s,f,0x08); u32 ss=pci_cfg(b,s,f,0x2C); g_amd.ssvid=(u16)ss; g_amd.ssdid=(u16)(ss>>16);
        g_amd.apu=ac_is_apu(g_amd.did); got=1;
        ac_bars(b,s,f);
    }
    if(!got){ ac_cpy(g_amd.why,"no AMD display controller",64); return -1; }
    g_amd.found=1; g_amd.asic_name=gpu_name(0x1002,g_amd.did);
    if(!g_amd.mmio_pa||g_amd.mmio_len<0x80000u){ ac_cpy(g_amd.why,"register BAR not found",64); return -1; }
    if(!(pci_cfg(g_amd.bus,g_amd.slot,g_amd.fn,0x04)&2)){ ac_cpy(g_amd.why,"memory decoding off",64); return -1; }
    if(!acpi_hi_map(g_amd.mmio_pa)){ ac_cpy(g_amd.why,"register BAR not reachable",64); return -1; }
    g_amd.mmio=(volatile u8*)g_amd.mmio_pa;
    if(g_amd.did==0x15DD||g_amd.did==0x15D8)ac_legacy();
    else g_amd.disc_ok=ac_disc_read()==0;
    u32 gc=amd_ipver(AMD_IP_GC);
    if(gc)g_amd.asic_name=amd_asic_name(gc,g_amd.did);
    g_amd.family=ac_family(gc);
    if(g_amd.family>=AMD_FAM_VGH||(g_amd.family==AMD_FAM_RV&&(gc==AMD_IPV(9,1,0)||gc==AMD_IPV(9,2,2)||gc==AMD_IPV(9,3,0))))g_amd.apu=1;
    ac_smn();
    if(gc)g_amd.why[0]=0;
    return 0;
}
