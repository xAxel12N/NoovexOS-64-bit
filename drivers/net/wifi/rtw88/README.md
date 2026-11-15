Realtek rtw88-family Wi-Fi driver (hand port of Linux rtw88, GPL-2.0 OR BSD-3-Clause upstream).

- chip.c chip registry (PCI ID -> rtw_chip), power sequences, H2C packet ring, calibration guard
- chips/ one file per chip behind include/nvx/drivers/net/wifi/rtw_chip.h (rtw8821c.c, rtw8822b.c, rtw8822c.c)
- rtw8821ce.c shared core: firmware download, MAC TRX init, rings, scan, TX/RX
- rtw_sec.c join, keys, H2C mailbox, EDCA, conditional table loader
- rtw_efuse.c bus access (g_rtw_bus), RF/BB register helpers, eFuse
- rtw_tables*.h generated chip tables
- chips/rtw8822b.c RTL8822BE chip (rtw_chip contract), chips/rtw8822b_tables.c from tools/wifi-tables/rtw_condgen.py

The Wi-Fi manager, WPA2/WPA3/EAP and the netdev live in net/wifi/.
