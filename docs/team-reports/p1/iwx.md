Task iwx (two coders share worktree /home/claude/wt/iwx, branch p-iwx; iwx1 owns drivers/net/wifi/iwx/iwx_pcie.c iwx_fw.c iwx.h,
iwx2 owns drivers/net/wifi/iwx/iwx_cmd.c iwx_scan.c iwx_data.c; agree on iwx.h through iwx1, iwx2 may append to iwx.h at the end only;
commit only your own files with `git add <files>`; pull the other's work by just reading the shared tree).
Port OpenBSD sys/dev/pci/if_iwx.c + if_iwxreg.h + if_iwxvar.h (ISC; verify the header) for Intel AX200/AX201/AX210/AX211 (and BE200
IDs from FreeBSD iwlwifi dual GPL-2.0 OR BSD-3 if trivial). Strip net80211/ifnet glue; target our kernel.
iwx1: PCI ID match + probe hook (find how drivers/net/wifi/rtw88/rtw8821ce.c is probed and add iwx next to it, inert unless ID matches),
  PCIe transport (gen2 context-info / ctxt-info-gen3 for AX210), TX/RX queues, interrupts or polling (the tree polls; polling is fine),
  firmware file parsing (TLV, magic 0x0a4c5749), loading from /lib/firmware/iwlwifi-<name>.ucode, wait for ALIVE. `iwx status` command.
iwx2: host commands (sync with timeout), NVM read, PHY/MAC context, scan (UMAC scan) producing results, auth/assoc, station, key
  install, data TX/RX. Plug into the wifi_hw_ops contract being written in worktree hwops (read /home/claude/wt/hwops/ for the header as
  it appears; if not ready, write an adapter file iwx_glue.c the lead can wire). 
Firmware licence: Intel iwlwifi firmware is redistributable (linux-firmware LICENCE.iwlwifi_firmware) but do NOT add blobs this round;
load from disk and print the exact filename needed. It is fine if this round ends at "scan works in theory": report honestly how far it got.
