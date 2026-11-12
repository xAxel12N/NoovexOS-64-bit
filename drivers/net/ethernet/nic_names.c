#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
const char* nic_name(u16 v,u16 d){
    if(v==0x8086&&(d==0x100E||d==0x100F||d==0x10D3)) return "Intel e1000";
    if(v==0x10EC&&(d==0x8168||d==0x8169||d==0x8161||d==0x8136)) return "Realtek RTL8168";
    if(v==0x10EC&&d==0x8139) return "Realtek RTL8139";
    if(v==0x1AF4&&(d==0x1000||d==0x1041)) return "VirtIO-net";
    if(v==0x1022&&d==0x2000) return "AMD PCnet";
    if(v==0x10EC&&(d==0x8125||d==0x3000)) return "Realtek RTL8125 2.5G";
    if(v==0x8086&&(d==0x15F2||d==0x15F3||d==0x125B||d==0x125C||d==0x125D)) return "Intel I225/I226 2.5G";
    if(v==0x8086&&(d==0x1533||d==0x1539||d==0x10C9||d==0x1521)) return "Intel I210/I211/82576/I350";
    if(v==0x15AD&&d==0x07B0) return "VMware vmxnet3";
    if(v==0x8086) return "Intel Ethernet";
    if(v==0x10EC) return "Realtek Ethernet";
    if(v==0x14E4) return "Broadcom Ethernet";
    if(v==0x1969) return "Qualcomm Atheros Ethernet";
    return "unknown NIC";
}
int nic_index(void){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==2&&pci_sub[k]!=0x80) return k; for(int k=0;k<pci_n;k++) if(pci_cls[k]==2) return k; return -1; }
