# s7 report: Broadcom brcmfmac + generic Wi-Fi protocol data

## What we already have (grep of tree)
- net/wifi/wpa2.c (PRF, PTK, PBKDF2-SHA1, 4-way), wpa3.c (SAE on P-256 hunting-and-pecking, KDF-SHA256, AKM 6/8), eap.c, wifi_netdev.c (CCMP), wifi_mgr.c.
- No regulatory database (only rtw88 efuse country byte 0xCB/0xCC). No SAE H2E (hash-to-element). No Broadcom Wi-Fi driver (14E4 only named in nic_names.c / lspci).
- Owner hardware is RTL8821CE: Broadcom is NOT owner hardware, so brcmfmac is low priority.

## Sources
1. wireless-regdb db.txt + LICENSE - https://git.kernel.org/pub/scm/linux/kernel/git/wens/wireless-regdb.git/plain/db.txt
   Licence: ISC ("Permission to use, copy, modify, and/or distribute this software for any purpose..."; Rodriguez/Berg/Green 2008). No SPDX line; LICENSE file text verified.
   Content: per-country frequency ranges, max bandwidth, max EIRP, DFS/NO-OUTDOOR/NO-IR flags; 2228 lines plain text, trivial to convert to a C table at build time (cf. kernel genregdb.awk). Use with rtw88 efuse country code. Usefulness 5.
2. hostap (hostapd/wpa_supplicant) src/common/ieee802_11_defs.h - https://w1.fi/cgit/hostap/plain/src/common/ieee802_11_defs.h
   Licence: BSD-3-Clause ("This software may be distributed under the terms of the BSD license", full 3-clause text in header).
   Content: 1365 lines: frame control/subtypes, status/reason codes, all WLAN_EID_* and extended EIDs, RSN/AKM/cipher suite selectors, HT/VHT/HE/EHT cap layouts, action categories. Also sae.c / wpa_common.c / ieee802_11_common.c (IE parser) in same dir (full fetch was truncated by cgit; clone repo). Reference for H2E (sae_derive_pt), SAE-PK, FT, OWE. Usefulness 5.
3. OpenBSD sys/net80211/ieee80211.h - https://raw.githubusercontent.com/openbsd/src/master/sys/net80211/ieee80211.h
   Licence: BSD-2-Clause (Onoe/Leffler header). 1204 lines: frame header structs, EID numbers, RSN/cipher defs, channel<->freq helpers. Good alternative to Linux include/linux/ieee80211.h, which is GPL-2.0-only (REJECTED as code, facts only). Usefulness 4.
4. Linux brcmfmac (brcm80211) - https://github.com/torvalds/linux/tree/master/drivers/net/wireless/broadcom/brcm80211
   Licence: "// SPDX-License-Identifier: ISC" (verified in fwil_types.h, pcie.c, msgbuf.h, commonring.h, brcm_hw_ids.h).
   Tables/layouts: include/brcm_hw_ids.h (115 lines, chip/PCI IDs); pcie.c (2799 lines, 28 BRCMF_PCIE_DEVICE IDs, shared-RAM/ring info layout, firmware name table); fwil_types.h (1239 lines, firmware IOVAR/command structs: join params, scan params, wsec keys, PMK); msgbuf.h/commonring.h (message-ring protocol); fwil.h (BRCMF_C_* dongle command numbers). Usefulness 2 (not owner hardware; needs Broadcom firmware blobs).
5. OpenBSD bwfm (sys/dev/ic/bwfm.c + bwfmreg.h/bwfmvar.h, sys/dev/pci/if_bwfm_pci.c) - https://raw.githubusercontent.com/openbsd/src/master/sys/dev/ic/bwfm.c
   Licence: ISC (Broadcom 2010-2016 + Patrick Wildt). 3324 lines; compact, non-Linux-API port of the same firmware interface (SDIO/USB/PCIe). Easier than brcmfmac to port if Broadcom is ever wanted. Usefulness 2.

## Top 3
1. wireless-regdb db.txt (ISC): adds missing regulatory/channel limits for our RTL8821CE.
2. hostap ieee802_11_defs.h + sae.c (BSD-3): canonical EID/status/AKM constants and SAE H2E, which our wpa3.c lacks (WPA3-only APs increasingly require H2E).
3. OpenBSD net80211/ieee80211.h (BSD-2): BSD frame/IE struct layouts instead of GPL-2.0-only Linux header.
(Broadcom brcmfmac/bwfm: ISC and portable, but skip unless Broadcom hardware becomes a target.)
