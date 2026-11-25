# rbtl review: Intel / MediaTek / Broadcom BT loaders in main (8f0ec653)

Scope: drivers/bluetooth/firmware/intelbt_fw.c, mtkbt_fw.c, bcmbt_fw.c, hooks in hci.c bt_up and btusb.c, NOTICE/build.sh.
Read-only; nothing built or committed.

## Verdict for the owner's Realtek 0BDA:C123
The Realtek path is functionally unchanged:
- **hci.c:68**: the Intel hook is gated by `intelbt_match`, which needs VID 8087.
- **btusb.c:178**: `mtkbt_attach` returns at once, because `mtkbt_match` needs 0E8D or a listed OEM pair, none of them 0BDA.
- **hci.c:77**: the Broadcom hook is an `else if` behind the Realtek branch, and also needs manuf 0x000F or VID 0a5c.
- **btu_parse**: the new 0xFF clause needs `bcmbt_usb_vendor(btu.vid)`. `btu.vid` is set at btusb.c:120, before parse at :125, and 0BDA is not in the list.
- **btu_xfer**: bulk completions now only set `btk` flags. Realtek never arms bulk TRBs.
- **Bulk buffers**: evbase+0x400 and +0x800 (1 KiB each) sit inside the evbase page, after the 8x64-byte event buffers. No overlap.

The one shared-helper change that can still alter `bt up` on the owner's machine is M1: the root-port reset of unrelated devices.

## HIGH

### H1. Intel 8260/8265 legacy path can never work: the Boot Params length and the limited_cce offset are both wrong
**Where:** drivers/bluetooth/firmware/intelbt_fw.c:260-261.

**What is wrong:** FreeBSD `struct iwmbt_boot_params` is 23 bytes. I fetched iwmbt_fw.h to confirm: limited_cce is at offset 21 and unlocked_state at 22. The code has two errors:
- It requires `ib_rp(m,&v)<24`, so a correct 23-byte reply is rejected with "Read Boot Params failed". Every 0a2b/0aaa/0025/0026/0029 device fails here.
- It tests `v[22]`, which is unlocked_state, not limited_cce.

It fails cleanly, so there is no hang, and it is Intel-only. But it is a false implementation: no host test covers ib_8260.

**Patch:**
```
-    m=ib_cmd(0xFC0D,0,0,2000); if(m<=0||ib_ev[0]!=0x0E||ib_rp(m,&v)<24||v[0]){ ib_msg(msg,cap,"Read Boot Params failed",0,0); return -1; }
-    u16 rev=ib_g16(v+4); if(v[22]){ ib_msg(msg,cap,"unsupported loading method (limited CCE)",0,0); return -1; }
+    m=ib_cmd(0xFC0D,0,0,2000); if(m<=0||ib_ev[0]!=0x0E||ib_rp(m,&v)<23||v[0]){ ib_msg(msg,cap,"Read Boot Params failed",0,0); return -1; }
+    u16 rev=ib_g16(v+4); if(v[21]){ ib_msg(msg,cap,"unsupported loading method (limited CCE)",0,0); return -1; }
```

## MEDIUM

### M1. The widened claim rule can root-reset unrelated vendor-class devices during `bt up`, before the owner's E0 adapter is reached
**Where:**
- drivers/bluetooth/transport/btusb.c:151 (`btu_candidate`)
- btusb.c:170-172 (probe loop)

**What is wrong:** `g_usbdev[].cls` is the class of the first interface (storage_fat_and_hubs.c:98). Any device whose first interface is 0xFF and whose VID is one of 0489, 13d3, 04ca, 0b05, 413c, 0930, 050d, 105b, 0bb4 or 19ff now becomes a candidate. Examples: Foxconn/IMC/Lite-On fingerprint readers, Dell/ASUS OEM USB NICs (r8152 vendor mode), WWAN modems. The probe then runs `xh_root_reset(p)` on it. Parse fails ("no Bluetooth HCI interface"), the slot is disabled and a setup-failed line is logged.

The loop takes ports in order. If such a device sits on a lower root port than the internal Realtek BT, it gets reset every time the adapter is probed. The owner's BT is still claimed afterwards, but `bt up` now has a side effect it did not have before. Linux avoids this because it matches subclass/protocol 1/1 at interface level. Here that information is not available until after the reset.

**Fix:** probe E0 adapters first, and fall back to vendor-class Broadcom only when no E0 adapter exists. With this, the owner's path is exactly the old one.

**Patch** (btusb.c):
```
-static int btu_candidate(int c,int p){
+static int btu_candidate(int c,int p,int vend){
     int bt=0;
-    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(g_usbdev[k].cls==0xE0||(g_usbdev[k].cls==0xFF&&bcmbt_usb_vendor(g_usbdev[k].vid)))bt=1; }
+    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&g_usbdev[k].ctl==c&&g_usbdev[k].port==p){ if(g_usbdev[k].hid||g_usbdev[k].cam||g_usbdev[k].prn)return 0; if(vend?(g_usbdev[k].cls==0xFF&&bcmbt_usb_vendor(g_usbdev[k].vid)):g_usbdev[k].cls==0xE0)bt=1; }
...
-    for(int c=0;c<g_xhc_n&&!btu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
-        for(int p=0;p<xhci_ports&&!btu.ok;p++){ if(!xhci_port_conn[p]||!btu_candidate(c,p))continue;
+    for(int vend=0;vend<2&&!btu.ok;vend++)
+    for(int c=0;c<g_xhc_n&&!btu.ok;c++){ xhc_select(c); if(!xhci_present||!xhci_init_ok)continue; xhci_port_refresh();
+        for(int p=0;p<xhci_ports&&!btu.ok;p++){ if(!xhci_port_conn[p]||!btu_candidate(c,p,vend))continue;
```
This also stops `btusb_listed` (btusb.c:162) from naming a vendor device first. Apply the same order there if wanted. That only affects a message.

### M2. ISC and BSD-2 licence texts are not shipped in the installed system
**Where:** build.sh:305.

**What is wrong:** build.sh:305 copies NOTICE plus only the rtlbtfw BSD-2 text. NOTICE holds the copyright lines but not the ISC permission notice. ISC requires the notice in all copies, and BSD-2 clause 2 requires it in the documentation of binary distributions. The rtlbt copy covers the same FreeBSD holders, but not the intelbt header. btm already listed this as a follow-up, and it was not done.

**Patch:** append to build.sh:305:
```
 && sed -n '1,18p' drivers/bluetooth/firmware/mtkbt_fw.c > $SR/usr/share/licenses/bluetooth/LICENSE.btmtk-ISC && sed -n '1,29p' drivers/bluetooth/firmware/intelbt_fw.c > $SR/usr/share/licenses/bluetooth/LICENSE.iwmbtfw-BSD-2-Clause
```

## LOW
- **L1 (MediaTek, no retry after a failed load):** btusb.c:178 calls `mtkbt_attach` only when the adapter is claimed. If the firmware is missing or the download fails, later `bt up` runs never retry, because btu stays claimed, until replug or controller reset. `bt status` also never shows the MTK message, since `bt.fwmsg` is not set. Fix: move the call into hci.c bt_up beside the Intel hook (`if(!bt.fw&&mtkbt_match(iv,ip)){ int r=mtkbt_attach(iv,ip); if(r<0){ bt_fail("MediaTek firmware load failed"); return 0; } bt.fw=r?r:3; }`) and delete btusb.c:178.
- **L2 (Intel, any 0xFF event ends a command wait):** intelbt_fw.c:164. `ib_cmd` returns on any 0xFF vendor event, whatever the opcode. A stray vendor event during Read Version (setup :288) is reported as "does not answer". Fix: `if(ib_ev[0]==0xFF)continue;` everywhere except where the vendor event is expected (ib_vendor already handles those).
- **L3 (Intel 7260 bseq, event code not compared):** intelbt_fw.c:229-233. The expected event code `f[1]` is never compared with `ib_ev[0]`. Add `||ib_ev[0]!=f[1]` (save f[1] before `f+=3`).
- **L4 (Intel, truncated last command):** intelbt_fw.c:131. A truncated last command is clipped and sent, not rejected. Every send is clamped to `len`, so there is no out-of-bounds read. Optional: `if(sent+ready>len)return -1;` after `ready+=3+c[2];`.
- **L5 (NOTICE formatting):** drivers/bluetooth/NOTICE has no blank line before the mtkbt and bcmbt entries. Cosmetic.

## Checked and OK
- **Hangs:** every wait has a bound.
  - Intel: `ib_next` 2 s per wait; boot event 5 s; vendor event 6 is 5 s.
  - btusb bulk: `btk_wait` stops at its ms limit or when the adapter goes away. A timed-out OUT sets obusy, so later sends refuse instead of queueing.
  - MediaTek: `mtk_wmt` 2-3 s; busy retry at most 20x100 ms.
  - Broadcom: `bt_cmd` 2 s. The .hcd record count is bounded by the pre-check.
  - Lock: `btu_lock` gives up after about 100 ms or under `xh_wdepth`. Every path drops the lock before it waits.
- **Bounds:**
  - Intel: the TLV parser checks `n<l`; `d[256]` is fine because `l` is at most 252; the DDC loop always advances and an oversize entry is refused by `ib_send`; bseq lengths are checked; the 4 MiB read cap is OK.
  - MediaTek: section count up to 64, `off+dl` checked against len, the 52-byte spec stays inside its 64-byte map.
  - Broadcom: the .hcd check requires vendor OGF, the exact total size and a Launch RAM.
- **Intel against FreeBSD (fetched):** the Intel Reset bytes `{0,0,0,1,boot}` match iwmbt_intel_reset. The RSA/ECDSA header fragments and the 964 payload start are right. TLV name packing equals Linux `INTEL_CNVX_TOP_PACK_SWAB`.
- **Licence headers:** BSD-2 (Intel), ISC (MediaTek) and GPL-2.0-or-later (Broadcom, compatible with GPLv3) headers are on every .c/.h file. No blobs are committed.
