# s4: Realtek rtw88/rtw89 data tables

## What we already have (drivers/net/wifi/rtw88, tools/wifi-tables)
- 8821C: MAC/BB/AGC/AGC_btg_type2/RF_A (rtw_cmac/cbb/cagc/cagc_btg2/crf in rtw_tables_cond.h); power-on/off sequences; efuse parse (logical 512, rfe_option, xtal, MAC).
- 8822B: mac/agc/bb/rf_a/rf_b (chips/rtw8822b_tables.c). 8822C: agc/bb/rf_a/rf_b/mp_cal_init.
- PCI IDs: B821, C821, B822, C822, C82F.
- MISSING: bb_pg (TX power by rate), txpwr_lmt (regulatory TX-power limits) for all chips; country->regd map; 8821CU USB IDs; anything rtw89 (8852AE/BE).

## Sources (all Linux mainline, torvalds/linux master, drivers/net/wireless/realtek/)
Every file below has header `SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause` (verified, dual-licensed, usable under BSD-3 in GPLv3 tree).

| File | Size | Contents | Use 1-5 |
|---|---|---|---|
| rtw88/rtw8821c_table.c | 166 KB | rtw8821c_mac, agc, agc_btg_type2, bb, **bb_pg_type0**, rf_a, **txpwr_lmt_type0** | 5 (bb_pg + txpwr_lmt are the gap) |
| rtw88/regd.c | 21 KB | 244 COUNTRY_REGD entries (alpha2 -> FCC/ETSI/MKK/... txpwr_regd for 2G/5G) | 5 (needed to index txpwr_lmt; SE->ETSI) |
| rtw88/rtw8821c.h / rtw8821c.c | 9 KB / 63 KB | struct rtw8821c_efuse (+ e/u/s sub-structs), pwr_seq trans_carddis_to_cardemu / cardemu_to_act / act_to_cardemu / cardemu_to_carddis | 3 (cross-check ours) |
| rtw88/rtw8821cu.c | 3 KB | USB IDs 0bda:2006,8731,8811,b820,b82b,c80c,c811,c820,c821,c82a,c82b; 2001:331d; 7392:c811,d811; 2c4e:0105 | 2 (no USB stack need now) |
| rtw88/rtw8822b_table.c | 495 KB | mac/agc/bb/bb_pg_type2/3/5/rf_a/rf_b/txpwr_lmt_type0/2/5 | 3 (pg+lmt missing) |
| rtw88/rtw8822c_table.c | 1.1 MB | incl. bb_pg_type0, txpwr_lmt_type0/5 | 2 |
| rtw88/rtw8822be.c, rtw8822ce.c, rtw8821ce.c | <1 KB | PCI IDs (already have) | 1 |
| rtw88/efuse.c, rtw89/efuse.c | 5 / 13 KB | physical->logical efuse map decode (rtw89 also OTP/dav layout) | 2 |
| rtw89/rtw8852a_table.c | 1.7 MB | phy_bb/radioa/radiob/nctl regs, txpwr_byrate, txpwr_track swing tables, txpwr_lmt 2g/5g + RU | 1 (not owner hw) |
| rtw89/rtw8852b_table.c | 749 KB | as above + bb_reg_gain, tx_shape_lmt | 1 |
| rtw89/rtw8852ae.c / rtw8852be.c | 3 KB | PCI IDs 10ec:8852, a85a / b852, b85b | 1 |
| rtw89/rtw8852a.c, rtw8852b_common.c | 77 / 73 KB | pwr_on/off seq (PWR_INTF_MSK_*), efuse struct | 1 |
| rtw89/regd.c | 40 KB | rtw89 country map incl. 6 GHz | 1 |

No GPL-2.0-only files encountered in this topic.

## Top 3
1. rtw88/rtw8821c_table.c: import rtw8821c_bb_pg_type0 + rtw8821c_txpwr_lmt_type0 via existing tools/wifi-tables/rtw_condgen.py (fills TX-power gap on owner hardware).
2. rtw88/regd.c: country -> regd mapping to select the txpwr_lmt rows (Sweden = ETSI).
3. rtw88/rtw8821c.h struct rtw8821c_efuse + pwr_seq arrays in rtw8821c.c: diff against our chips/rtw8821c.c for correctness.
