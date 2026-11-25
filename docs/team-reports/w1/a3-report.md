# a3 — Porting guide: internal driver interfaces (NoovexOS 64, v11.06)

Read-only analysis of /home/claude/src/NoovexOS-64-bit-main. All paths are relative to that root.
Line numbers are v11.06. Most source lines are very long (many statements per line), so "file:line" points at
the line that starts the function or struct.

## 0. Ground rules every driver lives with

- **Everything is polled.** No driver uses IRQs or MSI (grep for msi/ioapic/irq handlers in drivers/: nothing).
  The work runs from the main loop in core/init/main.c: ~307 `xhci_mouse_poll()` (only `if(xhid_n)`),
  ~346 `usb_hotplug_poll(); nic_watch();`, `snd_poll()` and `wifi_mgr_tick()` in the same loop. Nested UI waits
  call `ui_pump()` / graphics/ui/desktop/present_and_pump.c:123-150 (that file polls xHCI and `snd_poll`).
  A driver that needs servicing must either be called from that loop or pump itself from its own send/recv path
  (this is what USB networking does: `usn_events()` inside `usn_send`/`usn_recv`).
- **DMA memory**: `dma_alloc(n)` / `dma_alloc_pages(n)` (mem/pmm_vmm.c:49-50, decl include/nvx/mem/pmm_vmm.h:56-57).
  Bump allocator between DMA_ARENA_BASE and `DMA_ARENA_TOP=0x0C000000` (pmm_vmm.h:11). Zeroed, **never freed**:
  allocate once and reuse on re-init (rtw rings do this: shell/commands/processes_devices_wifi_bringup.c:597-599).
  Identity-mapped, below 4 GiB, so `phys == virt` and drivers store `(u32)(u64)ptr` straight into descriptors.
  x86 is cache-coherent: no flushes; use `cpu_wmb()` / `kapi_mb()` before ringing a doorbell.
- **Fixed device window** `DMW(off)` (include/nvx/mem/dmawin.h, 2 MiB at `g_devbuf`): legacy fixed buffers
  (PCnet rings in nic.h:11-15, AHCI_BUF gdisk.h:7, xHCI controller memory and the 10 per-device regions
  `XHID_BASE(r)=DMW(0x80000)+r*0x8000`, drivers/usb/xhci/controller_init.c:75-80). New drivers should NOT carve
  new DMW offsets; use `dma_alloc_pages`.
- **Delays/time**: `rtw_delay_us`/`hr_delay_us`, `hr_ns()`, `g_ticks` (100 Hz). `kapi_delay_us`, `kapi_ns` for modules.
- **Logging**: `klog(s)` (kernel), `kapi_log(s)` (module). User-visible Wi-Fi status: `wm_status_set()`.
- Never spin forever: v11.06 freezes came from unbounded loops with the heartbeat alive. Every wait needs a
  `g_ticks`/`hr_ns` bound (copy the `for(t<N){...; delay}` style used everywhere).

## 1. PCI probing and DMA helpers

### Kernel side (drivers/bus/pci.c, include/nvx/drivers/bus/pci.h)
| Function | Where | Notes |
|---|---|---|
| `pci_cfg/pci_cfgw(bus,slot,fn,off)` | pci.c:8-9 | port 0xCF8/0xCFC config access |
| `pci_scan()` | pci.c:21 | fills `pci_ven/dev/cls/sub/bus/slot[]`, `pci_n`. **Only buses 0-3, function 0, max PCI_MAX=24 entries** (pci.h:6) |
| `pci_find(cls,sub,prog,..)` | pci.c:11 | |
| `pci_find_cap(b,s,f,id)` | pci.c:39 | capability walk (PM=0x01, PCIe=0x10) |
| `pci_parent_bridge`, `pci_enable_upstream`, `pci_aspm_off` | pci.c:45,56,63 | used by `wifi_pci_prepare` (net/wifi/wifi_mgr.c:93) as a model for D0 wake, bus master on bridges, ASPM off, MPS clamp |
| `vtd_init()` | include/nvx/drivers/iommu/vtd.h | call before DMA if VT-d may be on (wifi does) |

### Module side: the kapi (include/kapi.h, sys/native/kapi.c:7-36; ARCHITECTURE.md §9.2 line 9521)
`kapi_pci_rd32/wr32`, `kapi_pci_find(vendor, ids[] 0-terminated, nth, &b,&d,&f)` (full 256-bus, multi-function scan,
kapi.c:9), `kapi_pci_find_class`, `kapi_pci_enable` (cmd |= 0x407: IO+MEM+busmaster+INTx off), port I/O,
`kapi_dma_pages(n)`, `kapi_phys(p)` (= p), `kapi_delay_us`, `kapi_ns`, `kapi_log`, `kapi_mb`, `kapi_mmio(pa,len)`
(maps BARs above 4 GiB via `acpi_hi_map`, kapi.c:36).

### Recipe: new PCI driver
1. Prefer a **module**: a .c that includes only `include/kapi.h` + `include/drivers.h`, add it to `MODS` in
   build.sh:27 (compiled separately, linked with kernel64.o). Template: drivers/net/ethernet/e1000.c:31 (`e1000_probe`).
2. Find the device with `kapi_pci_find`/`kapi_pci_find_class` (not `pci_scan`, which misses bus>3 and fn>0).
3. Read BAR0 (handle 64-bit BARs: `((lo>>1)&3)==2`), `kapi_pci_enable`, map with `kapi_mmio(bar,len)`.
4. Rings/buffers from `kapi_dma_pages`, addresses via `kapi_phys`, `kapi_mb()` before tail/doorbell writes.
5. Expose a `*_probe(struct ...)` and declare it in include/drivers.h; the kernel registry calls it (sections 4/6/7).
   A kernel-side (non-module) driver must also be listed in units.list.

## 2. Netdev interface

- Adapter contract: `struct nvx_nic {name, mac[6], send, recv, link}` include/drivers.h:3. `recv` returns 0 when empty.
- Registry: drivers/net/ethernet/nic.c:40-90. `nic_add(&n)` :42 (copies struct, up to NIC_MAX), `nic_use(i)` :47
  (makes it `g_mnic`, sets `nic_kind=2`, `my_mac`, resets DHCP), `nic_offer(i)` :57 (switch only if current has no
  link), `nic_drop(i)` :63 (hot-unplug), `nic_probe_all()` :73 (calls the PCI probe list, then weak `usbnet_scan`),
  `nic_watch()` :80 (cable/failover, main loop).
- Low-level send/recv used by the stack: `nic_send`/`nic_recv` live in **drivers/storage/ahci.c:90/102** (historic
  location) and forward to `g_mnic.send/recv` when `nic_kind==2`.
- Stack hooks: include/nvx/net/stack/tcpip.h:13-17 `g_tx`, `g_rx`, `g_mac`, `g_linkok`, `g_bcastf`; defined
  net/stack/tcpip.c:7-11 (`nd_tx` falls back to `nic_send`). `netdev_set(NETDEV_WIFI|NETDEV_ETH)`
  net/wifi/wifi_netdev.c:236 switches those pointers between Wi-Fi (`wnd_*`) and Ethernet (`nic_*`).
  RX pump: `net_idle_poll()` tcpip.c:29 (pulls up to 32 frames via `nd_rx`).
- Frames are plain Ethernet II (14-byte header, max 1514, no FCS).

### Recipe: new Ethernet NIC (PCI)
Write `foo_probe(struct nvx_nic* out)` as a module (section 1), fill name/mac/send/recv/link, declare in
include/drivers.h, add to the `pr[]` array in `nic_probe_all` (nic.c:75) and to `MODS`. One device per driver
(static state). A hot-plug device (USB) instead calls `nic_add` + `nic_offer` on attach and `nic_drop` on removal
(see drivers/usb/xhci/usb_network.c:208-213, 223-229).

## 3. Wi-Fi layer (net/wifi + drivers/net/wifi/realtek/rtw88)

**Key finding: there is no chip-agnostic Wi-Fi driver interface.** net/wifi calls rtw88 functions and RTW registers
directly. A non-Realtek chip (MT7921, ath, iwlwifi...) cannot "plug in" today; it would need a `wifi_ops` vtable
first. What exists:

### 3.1 Upper layer (chip-independent logic, but calls rtw_* directly)
| Piece | Where | What it needs from the chip |
|---|---|---|
| Manager API | include/nvx/net/wifi/wifi_mgr.h:10-31; net/wifi/wifi_mgr.c | `wifi_up` :129, `wifi_scan_start` :163, `scan_step` :184, `wm_connect` :219, `wifi_disconnect` :258, `wifi_mgr_tick` :281, `wifi_radio_on/off` :147-154 |
| Device find | `rtw_find_mmio()` graphics/ui/desktop/app_tiles_and_icons.c:249 | PCI class 02/80 via `nic_index_wifi` (:248, uses `pci_scan` table), `rtw_chip_select`, BAR -> `g_wifi_mmio`. The u32 "handle" `m` passed everywhere is the MMIO base |
| Bring-up | `wifi_up` runs 5 **shell commands** via `run_cmd` (wifi_mgr.c:127): "wifi init", "fw", "macinit", "allbuilt", "rings", implemented in shell/commands/processes_devices_wifi_bringup.c:186-640 (init -> `power_on` :632, fw -> `rtw_fw_download_iddma` :280, macinit -> `rtw_mac_trx_init` :250, allbuilt -> efuse/`phy_init`/tables :498-510, rings -> DMA ring alloc + ring base regs written with raw MMIO :595-617) |
| Scan | `rtw_set_channel_2g` rtw8821ce.c:265, `rtw_scan_drain` :368 (walks PCIe RX ring), `rtw_scan_frame` :334 (parses beacon/probe-resp -> `scan_add` :299 -> `g_aps[24]`, `scan_ap` type rtw8821ce.h:6) | channel set + raw 802.11 RX with RTW RX descriptor |
| Join | `rtw_assoc` rtw8821ce.c:631, `rtw_mgmt_xchg` rtw_sec.c:149, `rtw_wpa2_join` rtw_sec.c:162, SAE `rtw_sae_auth` rtw_sec.c:113, EAP `wifi_eap_join` rtw_sec.c:111 | mgmt TX `rtw_tx_mgmt_path` :557 / `rtw_tx_mgmt_q` :529; RX wait `rtw_rx_wait_mgmt` :583; EAPOL TX/RX net/wifi/wpa2.c:13/47 |
| Crypto | WPA2 4-way rtw_sec.c:83 (`wpa_rx_eapol_p`), PTK wpa2.c:82, WPA3 SAE net/wifi/wpa3.c, PEAP/MSCHAPv2 net/wifi/eap.c | **All in software.** Data CCMP is software in both directions (wifi_netdev.c:70-86 TX, :180-194 RX). HW CAM keys are also installed (`rtw_sec_write_cam` rtw_sec.c:45, called :265-267) and RX accepts hw-decrypted frames (`hwdec` wifi_netdev.c:191) |
| Data netdev | `wnd_setup(m,bssid,mac,tk,qos)` wifi_netdev.c:24, `wnd_tx_eth` :48 (Ethernet -> 802.11 + CCMP -> **writes the PCIe BE ring and reg 0x03A8 directly**), `wnd_rx_eth` :146 (**reads `g_rxring`/`g_rxbuf` and reg 0x03B4 directly**), `netdev_set` :236 | |

### 3.2 rtw88 chip contract (good, already multi-chip)
include/nvx/drivers/net/wifi/rtw_chip.h:6-97 documents bring-up order. `rtw_chip` (:60) gives tables, page
layout, firmware blob (`fw_start/fw_end`), callbacks `power_on, efuse_parse, mac_init, phy_init, set_channel,
set_tx_power, calibrate, coex_wifi_only, rfe_setup, phy_post, pcie_phy`. Registry drivers/net/wifi/realtek/rtw88/chip.c:15
(`rtw_chip_tab`), aliases :16, `rtw_chip_find(vendor,device,bus)` :30 (already keyed by bus), `rtw_chip_select`
:35 (**hard-codes RTW_BUS_PCIE**). Chips: chips/rtw8821c.c:250, rtw8822b.c, rtw8822c.c.
Recipe for another rtw88 PCIe chip: add chips/rtwXXXX.c filling `rtw_chip`, generate tables with
tools/wifi-tables/rtw_condgen.py, add to `rtw_chip_tab` (weak extern) and units.list, firmware blob linked as
`_binary_*_fw_bin_*` (check WHENCE license). Host test harness: tools/tests/wifi/chip_main.c (has its own `simbus`).

## 4. rtw88 bus abstraction `g_rtw_bus` — what a USB backend needs

### 4.1 What exists
- `typedef struct rtw_bus {name, kind, r8,r16,r32, w8,w16,w32}` rtw_chip.h:32-41; `RTW_BUS_PCIE=0, RTW_BUS_USB=1` :27.
- PCIe impl + global: drivers/net/wifi/realtek/rtw88/rtw_efuse.c:12-19 (`rtw_bus_pcie`, `g_rtw_bus=&rtw_bus_pcie`);
  wrappers `rtw_r8..rtw_w32` :20-25 used by all chip code, efuse, RF/BB helpers, H2C mailbox.
- So **register access is the only thing abstracted.** First arg `u32 h` is the MMIO base for PCIe; for USB it can be
  any non-zero cookie (but see "dead card" checks below).

### 4.2 PCIe-only paths a USB port must abstract (bypass g_rtw_bus today)
| # | Path | Where | USB replacement (Linux rtw88 usb.c, GPL-2.0 OR BSD-3: OK) |
|---|---|---|---|
| 1 | Device discovery | `rtw_find_mmio` app_tiles_and_icons.c:249, `nic_index_wifi` :248, shell `wifi` cmd :186-200 | USB attach (section 5 recipe) sets a cookie + `g_rtw_bus=&rtw_bus_usb`, `g_rtw_chip=rtw_chip_find(vid,pid,RTW_BUS_USB)`. `rtw_chip_select` must try USB too |
| 2 | PCIe prep | `wifi_pci_prepare` wifi_mgr.c:93, chip `pcie_phy` | skip on USB |
| 3 | Firmware reserved-page staging | `rtw_bcnq_stage_pg` rtw8821ce.c:217 (BCN queue TX BD at reg 0x308) called by `rtw_fw_download_iddma` :91 | bulk-OUT of [48-byte TX desc, QSEL=0x10 BEACON, `rtw_desc_csum`] + payload, then the same IDDMA register sequence (`rtw_iddma_block` :35 is register-only, reusable) |
| 4 | TRX DMA init | `rtw_mac_trx_init` rtw8821ce.c:167 (mostly MAC regs, PCIe DMA bits mixed in), shell ring setup :595-617, `rtw_rx_ring_up` :277, `rtw_rx_enable` :271 | USB: RX aggregation/`REG_RXDMA_*`/USB-specific init from usb.c; no rings |
| 5 | RX consumption (4 copies!) | `rtw_scan_drain` :368, `rtw_rx_poll_beacon` :386, `rtw_rx_wait_mgmt` :583, `wnd_rx_eth` wifi_netdev.c:146, `rtw_rx_wait_eapol` wpa2.c:47 | bulk-IN buffers holding several frames, each = 24-byte RX desc (same layout: len w0&0x3FFF, drv_info (w0>>16&0xF)*8, shift) + frame, 8-byte aligned. Recommend one `rtw_rx_next(&base)` iterator so all 5 consumers share it |
| 6 | TX | `rtw_tx_mgmt_q` :529, `rtw_tx_room` :504, `rtw_tx_probe` :423, `rtw_tx_buf` :498, `wnd_tx_eth` wifi_netdev.c:48, `rtw_tx_data_eapol` wpa2.c:13 | bulk-OUT; endpoint chosen by QSEL (usb.c maps 2/3/4 bulk-OUT eps to queues); TX desc identical 48 bytes + checksum; no TX buffer descriptor |
| 7 | H2C packet ring | `rtw_h2c_ring_setup`/`rtw_h2c_pkt_send` chip.c:68/82, gate chip.c:70 (`kind!=RTW_BUS_PCIE` -> -1) | bulk-OUT with QSEL 0x13 (H2C). The 4-box mailbox `rtw_h2c_send` rtw_sec.c:273 is register-based and works unchanged |
| 8 | "Card dead" checks | `rtw_r32(m,0)==0xFFFFFFFF` wifi_mgr.c:132,183; ring regs in wnd_tx/rx | USB: control-transfer failure / unplug must map to the same state (return 0xFFFFFFFF on failed reads) |
| 9 | Power-off/on of the radio | `wifi_radio_on/off` wifi_mgr.c:147-154 write 0x0522/0x0100 (register only, OK) | fine |

### 4.3 Proposed extension (minimal)
Add to `rtw_bus`: `int (*tx)(u32 h,int qsel,const u8* desc_and_frame,int len)`, `int (*rx_next)(u32 h,u8** base)`
+ `void (*rx_done)(u32 h)`, `int (*rsvd_page)(u32 h,const u8* payload,int len)`, `int (*h2c_pkt)(u32 h,const u8*,int)`,
`int (*setup)(u32 h)` (rings on PCIe, endpoints on USB). Move the shell-side ring code (bringup.c:595-617) into
rtw8821ce.c as the PCIe `setup`. Then refactor the consumers in rows 3-7 to call `g_rtw_bus`. This is the bulk
of the work; the chip files (rtw8821c.c etc.) need no change because USB `*CU` variants share the chip tables
(Linux uses the same rtw8821c.c for CE and CU; only `rtw_chip.bus`/ids, `rqpn`/page tables per HCI differ).

### 4.4 USB register access (the `rtw_bus_usb` r/w ops)
Linux rtw88 usb.c: vendor control transfer, bRequest **0x05**, bmRequestType **0xC0** read / **0x40** write,
wValue = register address (16 bit), wIndex = 0, wLength = 1/2/4, little-endian data. On NoovexOS: implement
r8..w32 with a saved-context `xh_ctrl` call exactly like `usn_ctrl` (usb_network.c:47) does: swap
`cur_slot/cur_ep0/cur_dbuf/e0_enq/e0_cyc`, call `xh_ctrl(0xC0,0x05,addr,0,len,1,0)`, copy from `cur_dbuf`,
restore. Note every register access becomes a ~100 us+ blocking control transfer; bring-up does thousands, so keep
the bounded timeouts (XH_T_CTRL) and never call it from the input path.

### 4.5 Firmware for USB sticks
rtw8821c_fw.bin / rtw8822b_fw.bin / rtw8822c_fw.bin are the same blobs for CU and CE (linux-firmware rtw88/,
WHENCE: Realtek redistributable license). Already linked for the PCIe chips.

## 5. USB core API for class drivers (drivers/usb/xhci*.c)

**Key finding: there is no class-driver registration API.** drivers/usb/xhci.c includes the five files into ONE
translation unit (xhci.c:19-23) and class drivers call `static` core helpers directly. Dispatch on attach is
hard-coded. A new USB class driver = a new `drivers/usb/xhci/<name>.c` `#include`d at the end of xhci.c plus edits
to the dispatch points below. Header: include/nvx/drivers/usb/xhci.h (exports only the public bits).

### 5.1 Core primitives (all operate on the CURRENT controller `g_xcur`; switch with `xhc_select(i)` storage_fat_and_hubs.c:132)
| Primitive | Where | Use |
|---|---|---|
| `usbloc_t {root,depth,hub,hport,speed,route,tt_slot,tt_port}` | controller_init.c:141 | device location (root port or behind hub) |
| `xh_cmd_s(p0,p1,p2,type,slot,&cc,&slot)` / `xh_cmd_w(...,ticks)` | controller_init.c:276/266 | command ring: 9 Enable Slot, 11 Address Device, 12 Configure Endpoint, 14 Reset EP, 15 Stop EP, 16 Set TR Dequeue (`(dci<<6)` in type) |
| `xh_db(slot,target)` | :240 | doorbell (target = DCI, 1 = EP0) |
| `xh_ctrl(rt,req,val,idx,len,in,data)` | :290 | sync control transfer on **current** device (`cur_slot,cur_ep0,cur_dbuf,e0_enq,e0_cyc`); IN data lands in `cur_dbuf` (data arg may be 0). Back-off: `xh_bo_*` holds a device after 3 failures; `xh_ctrl_to` tells timeout(1)/held(2) |
| `xh_event_w(type,&p0,&cc,&slot,&ep,ticks,iters)` | :244 | waits on event ring of interrupter 0; foreign transfer events are routed to `usn_xfer`, then `hid_xfer`, else queued in `xh_q` (:251-256). Increments `xh_wdepth` (guards recovery/SET_PROTOCOL from nesting) |
| `xh_event_t` | :264 | same with bulk timeouts (XH_T_BULK=40 ticks) |
| `ring_init(ring)` | hid_keyboard_mouse.c:171 | 256-TRB ring with link TRB at 255 |
| `xh_bulk(dci,ring,&enq,&cyc,buf,len)` | hid_keyboard_mouse.c:174 | **synchronous** bulk (Normal TRB + IOC, waits for event). Uses `msd_slot` - copy it, do not reuse |
| `usn_trb(ring,&enq,&cyc,buf,len,dci)` | usb_network.c:30 | **asynchronous** bulk: queue TRB + doorbell, completion arrives later via event routing |
| `ctx_sz()`, `ctx_zero`, `xh_slot_ctx(sc,L,entries)`, `xh_fix_mps0` | controller_init.c:311,312,356,357 | build input/device contexts |
| `xh_root_reset(port)` | :348 | root-port reset, returns speed |
| `xh_disable_slot(slot)` | :315 | release on failure/unplug |
| `xh_interval(speed,bInterval)` | :234 | endpoint interval field |
| `usbdev_note(port,vid,pid,cfg,n,name)` / `usbdev_gone` | storage_fat_and_hubs.c:93/103 | device list shown in Settings/`lsusb` |

### 5.2 How each class driver does its transfers
| Driver | Enum entry | Transfers | Memory | Event path |
|---|---|---|---|---|
| HID kbd/mouse (hid_keyboard_mouse.c, controller_init.c:370 `xhci_enum_loc`) | last resort in dispatch | interrupt IN, 16 TRBs in flight per ep (`xhid_arm` :318, XHID_INFLIGHT) | DMW regions `XHID_*` (10 max) | interrupter 0; `hid_xfer` hid_keyboard_mouse.c:94 from `xhci_poll_one` :118 |
| Mass storage (hid_keyboard_mouse.c:211-280) | `xhci_msd_enum_loc` :217, first in dispatch | sync bulk BOT: `msd_bot` :182 (CBW/data/CSW via `xh_bulk`), SCSI read10/write10 :200/205 | fixed MSD_* | interrupter 0, synchronous wait |
| USB network CDC-ECM/NCM/RNDIS (usb_network.c) | `usn_enum_loc` :150, second in dispatch | async bulk IN (8 bufs armed, re-armed in `usn_xfer` :37) + async bulk OUT (`usn_send` :81, 16 in flight); control via `usn_ctrl` :47 | own `dma_alloc_pages(96)` block, `usn_mem` :140 | interrupter 0: `usn_xfer` is called first by both `xh_event_w_in` and `hid_xfer`; self-pumps with `usn_events` :70 (takes `xh_poll_busy`, skips when `xh_wdepth>0`) |
| UVC camera (storage_fat_and_hubs.c:161-305 + uvc_webcam.c:1-100) | inside `xhci_enum_loc` when `uvc_has_video` (controller_init.c:400) | isoch or bulk IN, own ring, `uvc_cfg_ep` storage_fat_and_hubs.c:238 | own ring/buffers | **interrupter 1** (own event ring, `uvc_er_init` uvc_webcam.c:2, runtime regs +0x40) |
| USB audio UAC1/2 (uvc_webcam.c:103-290) | inside `xhci_enum_loc` when `aud_has_audio` (:407-411) | isoch IN/OUT: `aud_setup_ep` :212 (SET_INTERFACE, sample rate), `aud_cfg_ep` :159 (Configure EP type 5 in / 1 out), `aud_queue` :175 (Isoch TRB, interrupter target 2 in dword2 bits 22+) | `aud_al` buffers | **interrupter 2** (`aud_er_init` :153, rt+0x60); serviced from `ausnd_*` calls via `aud_service` :196 |
| Hubs | `xhub_attach` storage_fat_and_hubs.c:54 | control + port polling `xhub_poll_ports` :82 | | |

### 5.3 Dispatch points to edit when adding a class driver
1. Root-port hot-plug: `usb_hotplug_poll_one` storage_fat_and_hubs.c:110 - order MSD -> usbnet -> HID. On unplug
   (:121-123) add your `xxx_unplug(port)`.
2. Behind a hub: `xhub_enum_port` storage_fat_and_hubs.c:45 (same order); hub removal `xhub_remove_below` :26 /
   `usn_remove_below` pattern usb_network.c:267.
3. Boot-time scan: `xhci_mouse_autoinit` hid_keyboard_mouse.c:153; usbnet uses `usbnet_scan` usb_network.c:239
   (called from `nic_probe_all`) with the `usn_claimable` filter :230.
4. Event routing: add `if(xxx_xfer(sl,ep,cc,res,ptr))` next to `usn_xfer` in `xh_event_w_in` controller_init.c:251
   **and** in `hid_xfer` hid_keyboard_mouse.c:96 (otherwise your completions are eaten by HID or queued and lost).
   Or use a private interrupter (1 and 2 are taken; check MaxIntrs like uvc_webcam.c:252 does) for isolation.
5. `xhci_mouse_poll` hid_keyboard_mouse.c:140 returns early `if(!xhid_n&&!usn_active())` and the main loop only
   calls it `if(xhid_n)` - a new async driver must self-pump (like `usn_events`) or extend those conditions.
6. Controller reset: `usn_reset_all` usb_network.c:269 pattern (called from USB recovery).

### 5.4 Recipe: USB Wi-Fi stick (rtw88 *CU) backend - copy usb_network.c
1. New file drivers/usb/xhci/rtw_usb.c, include it at the end of drivers/usb/xhci.c.
2. `rtwu_enum_loc(const usbloc_t* L)`: clone `usn_enum_loc` (usb_network.c:150) up to GET device descriptor;
   bail out early unless `rtw_chip_find(vid,pid,RTW_BUS_USB)` matches (so it never steals other devices).
   SET_CONFIGURATION, parse the vendor interface: 1 bulk IN + 2..4 bulk OUT (+ optional interrupt IN).
   Configure Endpoint with one input context entry per endpoint (type 6 bulk IN, 2 bulk OUT; see :195-197).
3. Implement `rtw_bus_usb` r8..w32 with vendor requests (section 4.4) through a saved-context `xh_ctrl` like `usn_ctrl`.
4. Async bulk IN ring with N buffers of 16-32 KiB (RX aggregation), re-armed in `rtwu_xfer`; parsed buffers feed the
   `rx_next` iterator (section 4.3). Bulk OUT via `usn_trb`-style queueing per QSEL endpoint.
5. Register in the dispatch points (5.3: 1, 2, 4, 5, 6) before HID; on unplug clear the Wi-Fi state
   (`wifi_radio_off`, `g_rtw_bus` back to PCIe, `g_wifi_mmio=0`).
6. Make `rtw_find_mmio`/`wifi_up` accept the USB device (return the cookie), skip `wifi_pci_prepare`.

## 6. Block devices + file systems

Two layers, both switch-statements on a type enum (no vtable):
- **Main/system disk layer** drivers/storage/nvme.c:158-247: `enum DK_NONE..DK_USB` include/nvx/drivers/storage/nvme.h:6;
  `gdisk_read(lba,cnt,buf)` :163, `gdisk_write` :186, `gdisk_writen` :197, `gdisk_readn` :206, `gdisk_flush` :185,
  `gdisk_read_cached` :175 (64-sector LRU), `gdisk_init` :216 (probes IDE, AHCI, NVMe, virtio; picks the disk
  with a NoovexOS partition type 0x7F), `gdisk_set` :215, `gdisk_count/gdisk_read_on(i,..)` :242/246 (other disks),
  `dk_ratio` :227 (sector size / 512). Units: 512-byte LBAs, u32 LBA (2 TiB limit).
- **All-disks list** fs/vfs/parts.c: `blkdev {kind,unit,ratio,sectors,name,label}` include/nvx/fs/vfs/parts.h:6,
  `enum BK_MAIN, BK_IDE, BK_AHCI, BK_USB` parts.c:11, `bd_read` :33 (single 512 B sector), `bd_scan` :44,
  `part_scan_disk` :83 (MBR/GPT), `fs_probe` :68. Volumes (drive letters) fs/vfs/volumes.c: `vrd` :17 (uses
  `gdisk_read_on` or USB MSD), `vprobe` :81 (FAT/NTFS/exFAT/ext detection), `vol_scan` :114.
- Drivers: AHCI `ahci_init` drivers/storage/gdisk.c:55, `ahci_port_io` :42, `ahci_cmd` :10 (bounce buffer
  AHCI_BUF); NVMe drivers/storage/nvme.c (`nvme_init`, `nvme_read`, `nvme_writen`); IDE drivers/storage/ata.c;
  virtio-blk module drivers/virtio/virtio_blk.c:21/32 (`virtio_blk_probe/read/write/flush`, declared drivers.h:17-20);
  USB MSD `usb_msd_rdn/wrn/sync/sectors` (xhci.h:49-52).
- File systems: block callbacks `typedef int (*nvxblk_rd)(u64 lba,int n,void* buf)` (+ `nvxblk_wr`)
  third_party/ntfs/nvxntfs.h:3. FS driver table `rfs_ops {name, mount(rd,wr,base,nsect,ro), umount, exists, mkdir,
  write, read, del, ls, format}` include/nvx/fs/ntfs/ntfsk.h:8; instances fs/ntfs/ntfsk.c:20-22 (NTFS, FAT32,
  FAT16), current `g_rfs`. FAT API include/nvx/fs/fat/vfat.h (`vf_mount` fs/fat/vfat.c:178). Native NVX4:
  fs/nvx/nvx4.c. Path-based read-only VFS overlay: `vfs_fs_ops {name, match, read, size}` include/nvx/fs/vfs/vfs.h:7,
  `vfs_register` fs/vfs/vfs.c:27 (procfs, sysroot, usbfs examples: vfs.c:56, fs/vfs/unixfs.c:173).

### Recipe: new block driver (e.g. SD/MMC, UFS, a second NVMe)
1. Driver with `foo_init()`, `foo_read(lba,cnt,buf)`, `foo_write(lba,cnt,buf)`, `foo_flush()`, sector count.
2. Add `DK_FOO` to nvme.h:6 and a branch in each of `gdisk_read`, `gdisk_write`, `gdisk_writen`, `gdisk_flush`,
   `gdisk_name` and a probe line in `gdisk_init` (nvme.c:216-221).
3. That alone makes it visible in `bd_scan` (as BK_MAIN when it is the system disk) and to volumes via `gdisk_read_on`.
   For a non-system extra disk listed separately add a `BK_FOO` case in `bd_read`/`bd_scan` (parts.c:33/44).
### Recipe: new file system
Implement the `rfs_ops` set over `nvxblk_rd/wr` (base LBA + sector count) and add a `const rfs_ops g_rfs_foo`
next to ntfsk.c:20; add detection in `vprobe` (volumes.c:81) / `fs_probe` (parts.c:68). For a read-only pseudo-FS
use `vfs_register` with a `vfs_fs_ops`.

## 7. Sound interface

- Contract: `struct nvx_snd {name, rate, start, free_bytes, write(pcm,len), poll, stop, volume(%), rec_start,
  rec_read, rec_stop, rec_gain, rec_name}` include/drivers.h:21-35. PCM = 16-bit signed LE stereo (4 bytes/frame,
  see `cap_pump` core/sound.c:38), `rate` normally 48000.
- Core: core/sound.c - `g_snd` :9, `snd_init` :28 (`hda_probe || ac97_probe`), hot-plug override
  `snd_usb_attach` :12 / `snd_usb_arrived(name,mic)` :19 / `snd_usb_detach` :24 (saves and restores the main
  device), `snd_poll` :49 (main loop), `snd_dev_write/read` (/dev/dsp style), capture ring `cap_pump`.
- Drivers: HDA module drivers/sound/hda.c:226 (fills `out` at :253), AC97 drivers/sound/ac97.c:33 (:50), USB audio
  `ausnd_*` drivers/usb/xhci/uvc_webcam.c:226-243 (registered via `snd_usb_arrived` :261). Ring helper for DMA
  playback buffers: drivers/sound/ring.h (`sring_init/hw/free/write`).

### Recipe: new sound driver
PCI: module `foo_probe(struct nvx_snd* out)` (kapi), fill all playback callbacks (+ rec_* if capture), use
`sring` for the cyclic DMA buffer, declare in drivers.h, add to the `||` chain in `snd_init` (sound.c:31) and to
`MODS`. Hot-plug: build a `struct nvx_snd` and call `snd_usb_attach(&s)` on arrival, `snd_usb_detach()` on removal.

## 8. Things that will bite a porter (summary)
1. Wi-Fi bring-up lives in a shell command (bringup.c:186-640) invoked by `run_cmd`; DMA ring setup there uses raw
   MMIO macros, not `g_rtw_bus`.
2. RX parsing of the PCIe RX ring is duplicated in 5 places (section 4.2 row 5); TX in 4.
3. USB core has no registration API; event routing order (usn_xfer -> hid_xfer -> queue) must be extended or
   completions are lost; interrupters 1/2 are taken by UVC/audio.
4. `pci_scan` only sees buses 0-3, fn 0, 24 devices; use `kapi_pci_find`.
5. DMA allocator never frees and ends at 192 MiB physical; allocate once.
6. Every new wait must be time-bounded and must not run while `xh_wdepth>0` / `xh_poll_busy` is held by someone else
   (non-recursive locks since v11.06; re-entry deadlocks with the heartbeat alive).
7. Tests: `python3 tools/check.py --only usb|net|nics|drivers|fs`; host harnesses tools/tests/wifi (chip/mgmt/mgr),
   tools/tests/usbnet/proto_main.c, tools/tests/uvc, tools/tests/drivers/drvsim.c (kapi module simulator).
