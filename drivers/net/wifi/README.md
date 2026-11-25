# Wi-Fi chip drivers

One folder per vendor. Every driver plugs into the shared Wi-Fi layer in `net/wifi/` through `include/nvx/net/wifi/wifi_hw.h`.

| Folder | Chips |
|---|---|
| `realtek/rtw88/` | RTL8821CE, RTL8822BE, RTL8822CE (PCIe) |
| `intel/iwx/` | Intel AX200, AX201, AX210, AX211 (PCIe). `trans/` PCIe transport, `fw/` firmware and PNVM, `mvm/` commands, scan and data, `iwx_glue.c` wifi_hw glue |
| `mediatek/` | MediaTek MT7921/MT7922 family |
