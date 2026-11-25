Realtek rtw88-family Wi-Fi driver (hand port of Linux rtw88, GPL-2.0 OR BSD-3-Clause upstream).

- chip.c chip registry (PCI ID -> rtw_chip), power sequences, H2C packet ring, calibration guard
- chips/ one file per chip behind include/nvx/drivers/net/wifi/rtw_chip.h (rtw8821c.c, rtw8822b.c, rtw8822c.c)
- rtw8821ce.c shared core: firmware download, MAC TRX init, rings, scan, TX/RX
- rtw_sec.c join, keys, H2C mailbox, EDCA, conditional table loader
- rtw_coex.c, rtw_coex_core.h Bluetooth coex (8821C shared antenna: BT info C2H, PTA table + TDMA per case, Wi-Fi busy from traffic, H2C 0x60/0x63/0x66/0x69 as rtw88, C2H drained while idle, `wifi coex status`); see NOTICE
- rtw_bw.c 40/80 MHz: center channel + primary index, register values, AP HT/VHT operation, RA bandwidth, 20 MHz fallback at the next join or in session (HT Notify Channel Width / VHT Operating Mode, reconnect if no data), SGI40/80 (`wifi bw`); see NOTICE
- rtw_efuse.c bus access (g_rtw_bus), RF/BB register helpers, eFuse
- rtw_tables*.h generated chip tables
- chips/rtw8723d.c RTL8723DE (10ec:d723, 2.4 GHz 1T1R, 8051 firmware), tables from rtw8723d_table.c; licence in NOTICE
- chips/rtw8822b.c RTL8822BE chip (rtw_chip contract), chips/rtw8822b_tables.c from tools/wifi-tables/rtw_condgen.py

The Wi-Fi manager, WPA2/WPA3/EAP and the netdev live in net/wifi/.
- rtw_txpwr.c also gives the 40/80 MHz efuse power bases (rtw88 phy.c) and per-width regulatory limits; chips/rtw8821c.c has the SRRC/channel 14 CCK TX filter (rtw88 rtw8821c.c)
