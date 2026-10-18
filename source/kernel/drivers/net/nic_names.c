#include "include/nvx/base.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/nic_names.h"
const char* nic_name(u16 v,u16 d){
    if(v==0x8086&&(d==0x100E||d==0x100F||d==0x10D3)) return "Intel e1000";
    if(v==0x10EC&&(d==0x8168||d==0x8169||d==0x8161||d==0x8136)) return "Realtek RTL8168";
    if(v==0x10EC&&d==0x8139) return "Realtek RTL8139";
    if(v==0x1AF4&&(d==0x1000||d==0x1041)) return "VirtIO-net";
    if(v==0x1022&&d==0x2000) return "AMD PCnet";
    return "unknown NIC";
}
int nic_index(void){ for(int k=0;k<pci_n;k++) if(pci_cls[k]==2) return k; return -1; }
