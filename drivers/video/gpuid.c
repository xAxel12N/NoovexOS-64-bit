#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/video/gpuid.h"

typedef struct { u16 lo, hi; const char* name; } gid_t;

static const gid_t NV_EXACT[]={
    {0x1F82,0x1F82,"GeForce GTX 1650 (TU117)"},{0x1F91,0x1F91,"GeForce GTX 1650 Mobile (TU117)"},{0x1F99,0x1F99,"GeForce GTX 1650 Mobile (TU117)"},{0x1F95,0x1F95,"GeForce GTX 1650 Ti Mobile (TU117)"},{0x2187,0x2187,"GeForce GTX 1650 SUPER (TU116)"},
    {0x2184,0x2184,"GeForce GTX 1660 (TU116)"},{0x21C4,0x21C4,"GeForce GTX 1660 SUPER (TU116)"},{0x2182,0x2182,"GeForce GTX 1660 Ti (TU116)"},{0x1F08,0x1F08,"GeForce RTX 2060 (TU106)"},{0x1F06,0x1F06,"GeForce RTX 2060 SUPER (TU106)"},
    {0x1F02,0x1F02,"GeForce RTX 2070 (TU106)"},{0x1E84,0x1E84,"GeForce RTX 2070 SUPER (TU104)"},{0x1E82,0x1E82,"GeForce RTX 2080 (TU104)"},{0x1E81,0x1E81,"GeForce RTX 2080 SUPER (TU104)"},{0x1E07,0x1E07,"GeForce RTX 2080 Ti (TU102)"},
    {0x2204,0x2204,"GeForce RTX 3090 (GA102)"},{0x2206,0x2206,"GeForce RTX 3080 (GA102)"},{0x2484,0x2484,"GeForce RTX 3070 (GA104)"},{0x2486,0x2486,"GeForce RTX 3060 Ti (GA104)"},{0x2503,0x2503,"GeForce RTX 3060 (GA106)"},{0x2504,0x2504,"GeForce RTX 3060 (GA106)"},{0x2507,0x2507,"GeForce RTX 3050 (GA106)"},
    {0x2684,0x2684,"GeForce RTX 4090 (AD102)"},{0x2704,0x2704,"GeForce RTX 4080 (AD103)"},{0x2782,0x2782,"GeForce RTX 4070 Ti (AD104)"},{0x2786,0x2786,"GeForce RTX 4070 (AD104)"},{0x2803,0x2803,"GeForce RTX 4060 Ti (AD106)"},{0x2882,0x2882,"GeForce RTX 4060 (AD107)"},
    {0x1B80,0x1B80,"GeForce GTX 1080 (GP104)"},{0x1B81,0x1B81,"GeForce GTX 1070 (GP104)"},{0x1C03,0x1C03,"GeForce GTX 1060 6GB (GP106)"},{0x1C02,0x1C02,"GeForce GTX 1060 3GB (GP106)"},{0x1C82,0x1C82,"GeForce GTX 1050 Ti (GP107)"},{0x1C81,0x1C81,"GeForce GTX 1050 (GP107)"},
    {0,0,0}};
static const gid_t NV_FAMILY[]={
    {0x1E00,0x1FFF,"GeForce RTX 20 / GTX 16 (Turing)"},{0x2180,0x21FF,"GeForce GTX 16 (Turing TU116)"},{0x2200,0x25FF,"GeForce RTX 30 (Ampere)"},{0x2600,0x28FF,"GeForce RTX 40 (Ada Lovelace)"},{0x2900,0x2FFF,"GeForce RTX 50 (Blackwell)"},
    {0x1B00,0x1DFF,"GeForce 10 (Pascal)"},{0x1340,0x17FF,"GeForce 900 / 700 (Maxwell)"},{0x0FC0,0x133F,"GeForce 700 / 600 (Kepler)"},{0,0,0}};
static const gid_t AMD_EXACT[]={
    {0x67DF,0x67DF,"Radeon RX 470 / 480 / 570 / 580 (Polaris)"},{0x67EF,0x67EF,"Radeon RX 460 / 560 (Polaris)"},{0x699F,0x699F,"Radeon RX 550 (Polaris)"},{0x687F,0x687F,"Radeon RX Vega 56 / 64"},{0x66AF,0x66AF,"Radeon VII (Vega 20)"},
    {0x731F,0x731F,"Radeon RX 5600 / 5700 (Navi 10)"},{0x7340,0x7340,"Radeon RX 5500 (Navi 14)"},{0x73BF,0x73BF,"Radeon RX 6800 / 6900 (Navi 21)"},{0x73DF,0x73DF,"Radeon RX 6700 (Navi 22)"},{0x73EF,0x73EF,"Radeon RX 6600 (Navi 23)"},{0x73FF,0x73FF,"Radeon RX 6600 (Navi 23)"},
    {0x743F,0x743F,"Radeon RX 6400 / 6500 (Navi 24)"},{0x744C,0x744C,"Radeon RX 7900 (Navi 31)"},{0x747E,0x747E,"Radeon RX 7700 / 7800 (Navi 32)"},{0x7480,0x7480,"Radeon RX 7600 (Navi 33)"},{0x7550,0x7550,"Radeon RX 9070 (Navi 48)"},
    {0x15DD,0x15DD,"Radeon Vega graphics (Raven Ridge APU)"},{0x15D8,0x15D8,"Radeon Vega graphics (Picasso APU)"},{0x1636,0x1636,"Radeon graphics (Renoir APU)"},{0x1638,0x1638,"Radeon graphics (Cezanne APU)"},{0x164C,0x164C,"Radeon graphics (Lucienne APU)"},
    {0x15E7,0x15E7,"Radeon graphics (Barcelo APU)"},{0x1681,0x1681,"Radeon 680M (Rembrandt APU)"},{0x15BF,0x15BF,"Radeon 780M (Phoenix APU)"},{0x164E,0x164E,"Radeon graphics (Raphael CPU)"},{0x1506,0x1506,"Radeon 610M (Mendocino APU)"},{0x150E,0x150E,"Radeon 890M (Strix Point APU)"},
    {0,0,0}};
static const gid_t AMD_FAMILY[]={{0x7300,0x73FF,"Radeon RX 5000 / 6000 (RDNA 1/2)"},{0x7400,0x74FF,"Radeon RX 6000 / 7000 (RDNA 2/3)"},{0x7500,0x75FF,"Radeon RX 9000 (RDNA 4)"},{0x6600,0x6FFF,"Radeon (GCN)"},{0x1300,0x16FF,"Radeon graphics (APU)"},{0,0,0}};
static const char* gid_find(const gid_t* t,u16 d){ for(int i=0;t[i].name;i++) if(d>=t[i].lo&&d<=t[i].hi)return t[i].name; return 0; }
const char* gpu_name(u16 ven,u16 dev){ const char* n=0;
    if(ven==0x10DE){ n=gid_find(NV_EXACT,dev); if(!n)n=gid_find(NV_FAMILY,dev); return n?n:"NVIDIA GPU"; }
    if(ven==0x1002){ n=gid_find(AMD_EXACT,dev); if(!n)n=gid_find(AMD_FAMILY,dev); return n?n:"AMD Radeon GPU"; }
    if(ven==0x8086)return "Intel graphics"; if(ven==0x1234||ven==0x1B36)return "QEMU standard VGA"; if(ven==0x15AD)return "VMware SVGA"; if(ven==0x80EE)return "VirtualBox graphics"; return "Display controller"; }
int gpu_list(gpu_t* out,int max){
    extern int pci_n; extern void pci_scan(void); if(!pci_n)pci_scan(); int n=0;
    for(int k=0;k<pci_n&&n<max;k++){ if(pci_cls[k]!=0x03)continue; gpu_t* g=&out[n++]; g->ven=pci_ven[k]; g->dev=pci_dev[k]; g->bus=pci_bus[k]; g->slot=pci_slot[k];
        g->vendor=g->ven==0x10DE?"NVIDIA":g->ven==0x1002?"AMD":g->ven==0x8086?"Intel":g->ven==0x1234||g->ven==0x1B36?"QEMU":g->ven==0x15AD?"VMware":g->ven==0x80EE?"VirtualBox":"Other";
        g->name=gpu_name(g->ven,g->dev);
        u16 d=g->dev; int turing=(d>=0x1E00&&d<=0x1FFF)||(d>=0x2180&&d<=0x21FF);
        if(g->ven==0x10DE){ g->driver=turing?"NoovexOS GSP-RM driver (2D acceleration)":"detected - only Turing (GTX 16 / RTX 20) is accelerated so far, screen through the firmware framebuffer"; g->accel=turing; }
        else if(g->ven==0x1002){ g->driver="detected - no AMD driver yet, screen through the firmware framebuffer"; g->accel=0; }
        else if(g->ven==0x8086){ g->driver="NoovexOS Intel gen 9 driver"; g->accel=1; }
        else { g->driver="firmware framebuffer"; g->accel=0; } }
    return n; }
