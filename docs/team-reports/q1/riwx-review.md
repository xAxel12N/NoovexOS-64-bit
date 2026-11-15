# riwx review: Intel iwx port (/home/claude/wt/iwxi/drivers/net/wifi/iwx)

Reference: OpenBSD if_iwx.c 1.230, if_iwxreg.h 1.75, if_iwxvar.h 1.50 (fetched from github openbsd/src master; revisions match what the coders cite).
Snapshot: files as of 23:41 (iwx_pnvm.c appeared at 23:40 and is untracked/unwired; integration diff in kernel64.c, wifi_hw.c, shell is uncommitted).
Struct sizes were checked with a host compile: ctxt_info 1792, prph_scratch 1660, ctxt_info_gen3 104, tfh_tfd 256, rx_transfer_desc 16, cmd_header_wide 8, rx_mpdu_desc 64 (v1 48), nvm_get_info_rsp v4 468, scan_req_umac_v14 1940. All match OpenBSD **except nvm_get_info_rsp_v3** (130, should be 128), which is M3 below.

## Priority 1: inert on the owner's machine (RTL8821CE + GTX 1650): PASS
- `whw_probe()` (net/wifi/wifi_hw.c:10) runs `rtw_wifi_hw_probe()` first and calls `iwx_wifi_hw_probe()` only `if(!whw_n)`. On the owner's box rtw registers, so iwx code is never reached.
- `iwx_probe()` (iwx_pcie.c:80-97) touches nothing (no BAR read, no config write) unless vendor 0x8086 plus one of the 18 IDs matches. `iwx status` with no card only prints a line.
- With no Intel card the only extra cost is about 6 KB of BSS for g_iwx. NVIDIA and Realtek paths are untouched.

## Priority 2: bounded waits: mostly PASS, one MEDIUM (M1)
- Bounded:
  - nic_lock: 150 ms
  - prepare_card_hw: 1.75 s worst case (OpenBSD is about 0.4 s because it does not reset `t` per try)
  - apm clock: 25 ms
  - ALIVE: 2 s
  - each host command: 1 to 2 s
  - PNVM: 2 s
  - scan_done: bounded by wifi_mgr's 800-tick fallback
- Worst case on an Intel laptop with a misbehaving card: about 4 s in `iwx_start` plus about 8 s in `iwx_init_hw_cmds`, inside the first `wifi_hw_ops_get()`. That is bounded, but it is blocking.

## Findings

### HIGH

**H1. The RX ring size given to the firmware is half the real size (gen2 and gen3).** iwx_pcie.c:163 and iwx_pcie.c:181.
- OpenBSD uses `IWX_RX_QUEUE_CB_SIZE(512)` = fls(512)-1 = **9**. The port writes 8, which describes a 256-entry ring, while the driver indexes 512 entries (`IWX_RX_RING`, `&511`).
- Effect: closed_rb_num wraps at 256 while rx_cur runs to 511. After the first 256 RX buffers the driver re-parses stale buffers, a batch of up to 64 per poll, and RX is garbage from then on. ALIVE still passes, so this would look like it works at first.
- Patch:
```
-    c->control_flags=(1u<<8)|(8u<<4)|(4u<<9);
+    c->control_flags=(1u<<8)|(9u<<4)|(4u<<9);
-    c->mtr_base_addr=sc->txq[IWX_CMD_QID].desc.pa; c->mcr_base_addr=sc->rx_used.pa; c->mtr_size=5; c->mcr_size=8;
+    c->mtr_base_addr=sc->txq[IWX_CMD_QID].desc.pa; c->mcr_base_addr=sc->rx_used.pa; c->mtr_size=5; c->mcr_size=9;
```

**H2. Group-0 ("narrow") host commands are sent with group_id 0, and firmware API 50 and later answers that with a BAD_COMMAND assert.** iwx_pcie.c:214.
- OpenBSD iwx_send_cmd (if_iwx.c:5872-5879) rewrites any group-0 id to `IWX_WIDE_ID(IWX_LONG_GROUP, code)`.
- All firmware this port loads is API 50 or later (59 or later for AX210). So TX_ANT_CONFIGURATION, BT_CONFIG, POWER_TABLE, MCC_UPDATE and BEACON_FILTERING each crash the firmware.
- `iwx_init_hw_cmds` then fails on the first command, with a 1 s timeout.
- The reply comes back with hdr.flags=1, so the matcher in iwx_cmd.c:53 also has to accept group 1.
- Patch:
```
iwx_pcie.c:214
-    struct iwx_cmd_header_wide* h=(struct iwx_cmd_header_wide*)buf; h->opcode=(u8)id; h->group_id=(u8)(id>>8); h->idx=(u8)idx;
+    struct iwx_cmd_header_wide* h=(struct iwx_cmd_header_wide*)buf; h->opcode=(u8)id; h->group_id=(u8)(id>>8)?(u8)(id>>8):IWX_LONG_GROUP; h->idx=(u8)idx;
iwx_cmd.c:53
-    if(!(pkt->hdr.qid&0x80)&&iwx_w.pending&&!iwx_w.done&&(iwx_w.id==id||(pkt->hdr.code==iwx_w.id&&pkt->hdr.flags==0)||pkt->hdr.code==IWX_REPLY_ERROR)){
+    if(!(pkt->hdr.qid&0x80)&&iwx_w.pending&&!iwx_w.done&&(iwx_w.id==id||(iwx_w.id<0x100&&pkt->hdr.code==iwx_w.id&&pkt->hdr.flags<=IWX_LONG_GROUP)||pkt->hdr.code==IWX_REPLY_ERROR)){
```

### MEDIUM

**M1. The command and ALIVE waits are bounded only by `iwx_now_ms()`.** iwx_cmd.c:71, iwx_pcie.c:264, iwx_pnvm.c:124.
- If `hr_ns()` falls back to `g_ticks*10ms` (no TSC or HPET calibration) and IF=0, or the PIT is not running yet, the clock never moves.
- `iwx_send_cmd_sync` and the PNVM wait spin with no delay and no iteration cap, so they hang forever.
- The ALIVE loop's `hr_delay_us(200)` falls back to io_wait, but its exit test is still time-based, so it hangs too.
- Patch (add iteration backstops):
```
iwx_cmd.c:71
-    while(!iwx_w.done){ iwx_rx_poll(sc); if(iwx_now_ms()-t0>tmo)break; }
+    for(u32 it=0;!iwx_w.done&&it<tmo*100u;it++){ iwx_rx_poll(sc); if(iwx_now_ms()-t0>tmo)break; hr_delay_us(10); }
   (needs #include "include/nvx/arch/x86_64/time/hrtimer.h" in iwx_cmd.c)
iwx_pcie.c:264
-    while(iwx_now_ms()-t0<2000){
+    for(int it=0;it<10000&&iwx_now_ms()-t0<2000;it++){
iwx_pnvm.c:124
-    while(!iwx_pnvm_done&&iwx_now_ms()-t0<2000)iwx_rx_poll(sc);
+    for(u32 it=0;!iwx_pnvm_done&&it<200000u&&iwx_now_ms()-t0<2000;it++){ iwx_rx_poll(sc); hr_delay_us(10); }
```

**M2. The NVM channel index map omits channels 68-96, so every 5 GHz channel after 64 is mis-mapped.** iwx_cmd.c:101.
- OpenBSD iwx_nvm_channels_8000 and _uhb (if_iwx.c:149-170) include 68,72,...,96 and 169-181.
- With the port's table, the flag at channel_profile[22] (ch 68) is applied to ch 100, and so on.
- Result: allowed channels are dropped, and channels the NVM marks invalid are scanned. With an SSID the scan is active, which means probing on channels that may be regulatorily disallowed.
- Patch:
```
-    static const u8 ch[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
+    static const u8 ch[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177,181};
```
(51 entries, which is within both v3 [51] and v4 [110]. Also raise `IWX_NUM_CHANNELS`-sized `chan[]` usage only if 6 GHz is added later.)

**M3. `iwx_nvm_get_info_rsp_v3` has an extra `n_channels` field: 130 bytes instead of 128.** iwx_cmd.h, struct `iwx_nvm_get_info_rsp_v3`.
- OpenBSD regulatory_v1 is `{u32 lar_enabled; u16 channel_profile[51]; u16 reserved;}`.
- Effect: on firmware without API bit 48, `rl!=want` always fails NVM init, and the channel offsets would be wrong in any case.
- Patch:
```
-    u32 lar_enabled,n_channels; u16 channel_profile[51];
+    u32 lar_enabled; u16 channel_profile[51]; u16 reserved;
```
Also add `_Static_assert(sizeof(struct iwx_nvm_get_info_rsp_v3)==128&&sizeof(struct iwx_nvm_get_info_rsp)==468,"iwx nvm");` to iwx_cmd.h.

**M4. Licence header is incomplete or paraphrased.** All 9 files, lines 1-35.
- (a) OpenBSD if_iwx.c also carries `Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>` under ISC. ISC requires that notice in all copies of derived code, and it is missing.
- (b) The Intel BSD-3 block drops "All rights reserved." and rewrites the disclaimer. Clause 1 of BSD-3 requires the conditions and the disclaimer to be retained as written. The real disclaimer reads "...LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING...". The port's version is shortened to "...ANY DAMAGES ARISING IN ANY WAY".
- Patch: replace lines 20-34 in every file with the verbatim BSD LICENSE block from if_iwx.c:46-75. Add these lines after line 6:
```
+ * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
```
- The NOTICE file should say `SPDX-License-Identifier: ISC AND BSD-3-Clause`.
- NOTICE also calls iwx_glue.c "own code (GPLv3)", but that file carries only the ISC header. Either drop the ISC header from iwx_glue.c and add a GPLv3 SPDX line, or delete that sentence from NOTICE.

**M5. The CNVi AX201/AX211 IDs can never match: the driver is dead on most Intel laptops.** iwx_pcie.c:83-89, drivers/bus/pci.c:21-24.
- Integrated Wi-Fi sits at 00:14.3, but `pci_scan()` enumerates only function 0. The 14 integrated IDs (02f0 ... 7740) are unreachable, and `iwx_probe` hard-codes fn 0 in `pci_cfg`.
- This is safe today, but if someone "fixes" the matching by reading another device at fn 0, they would program the xHCI at 00:14.0. That is the owner-risk scenario to avoid.
- Patch: do a local function scan in iwx_probe and use `sc->fn` everywhere:
```
-    for(int k=0;k<pci_n;k++){ const char* nm=iwx_match(pci_ven[k],pci_dev[k]); if(!nm)continue;
-        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=0; sc->vendor=pci_ven[k]; sc->device=pci_dev[k]; sc->product=nm;
-        u32 bar=pci_cfg(sc->bus,sc->slot,0,0x10);
+    for(int k=0;k<pci_n;k++){ int nf=(pci_cfg(pci_bus[k],pci_slot[k],0,0x0C)&0x800000)?8:1; for(int fn=0;fn<nf;fn++){
+        u32 id=pci_cfg(pci_bus[k],pci_slot[k],fn,0); const char* nm=iwx_match((u16)id,(u16)(id>>16)); if(!nm)continue;
+        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=fn; sc->vendor=(u16)id; sc->device=(u16)(id>>16); sc->product=nm;
+        u32 bar=pci_cfg(sc->bus,sc->slot,sc->fn,0x10);
```
(Do the same for the `0x14` and `0x04` config accesses, using `sc->fn`, and close the extra brace.)

### LOW

- **L1. One-byte overflow of `fwver[32]`.** iwx_fw.c:52. A FW_VERSION TLV holding three max u32 values writes 33 bytes into fwver[32] and corrupts the low byte of `fwraw` (the next field). The reload path at iwx_fw.c:71 then parses from a bad pointer. This needs a crafted file on disk. Patch in iwx.h:166: `fwver[32]` -> `fwver[40]`.
- **L2. A late reply can complete the wrong command.** iwx_cmd.c:53. The sync matcher does not check `hdr.idx`, so a reply that arrives after a timeout completes the next command with wrong data, for example when NVM_GET_INFO follows. Patch:
  - Store `iwx_w.idx=iwx_tx_hcmd(...)`.
  - Add `&&pkt->hdr.idx==iwx_w.idx` to the match condition (keep REPLY_ERROR unconditional).
- **L3. Data TX can never succeed.** iwx_data.c:63/67 pass `qid=-1`, but `iwx_tx_data` (iwx_pcie.c:224) rejects `qid<1`. This is harmless because READY is never set yet. Fix:
  - In iwx_data.c, pass `IWX_DATA_QID` (1).
  - Or map `-1` to 1 in iwx_tx_data.
- **L4. `iwx_rx_poll` invalid-packet test.** iwx_pcie.c:202 checks `hdr.qid==0`. OpenBSD `iwx_rx_pkt_valid` masks with `qid&~0x80`. Patch: `(p->hdr.qid&0x7f)==0`.
- **L5. iwx_pnvm.c is incomplete.** It references `iwx_sku_id[3]` and `iwx_pnvm_done`, which are defined nowhere yet, and `iwx_load_pnvm` is not called from `iwx_start`. Do not add it to kernel64.c until iwx2 defines those symbols (SKU_ID from ALIVE, PNVM_INIT_COMPLETE notification).
  - TLV bounds in iwx_pnvm.c are correct: `len<ln` is checked before the `(ln+3)&~3` rounding, so the rounding cannot wrap.
- **L6. Blocking first probe on a machine without a working rtw card.** The first `wifi_hw_ops_get()` blocks for up to about 12 s with an Intel card present. Consider moving the `iwx_start` call out of `whw_probe` into an explicit `iwx up` or the wifi init path.

## Priority 4: firmware TLV parsing bounds: PASS (except L1)
- In iwx_fw.c:38-59:
  - the header is checked (88 bytes, zero word, magic)
  - `ln>n-off` is checked before use, and `ln` is at most 8 MB, so `(ln+3)` cannot wrap
  - SEC_RT needs ln of at least 4 and the section count is capped at 69
  - API/CAPA TLVs need ln==8 with idx<4
  - CMD_VERSIONS is capped at 128
- `iwx_init_fw_sec`: lm+um+pg+2 <= nsect <= 69, so `fwd[k]` stays in bounds. Each group is checked against 64 DRAM slots.
- Unknown TLVs are skipped. OpenBSD rejects them, but skipping is fine.

## Priority 3: DMA layout: see H1, M3
- TFD, TB, rx_transfer_desc, gen2 and gen3 context info, and prph_scratch layouts match OpenBSD.
- The DRAM table order (lmac to lmac_img, umac to umac_img, paging to virtual_img) matches iwx_init_fw_sec.
- Correct against OpenBSD:
  - control_flags in prph_scratch (RB_SIZE_4K|MTR_MODE|0xC0000)
  - mtr_size 5
  - the RX write-pointer register per family
  - the TX write-pointer wrap (0xFFFF for gen3)
  - byte-count table encoding
- The 64-buffer / 512-slot RX aliasing is safe because wptr <= hw+56.

## Priority 5: ISC header kept: partial, see M4.
