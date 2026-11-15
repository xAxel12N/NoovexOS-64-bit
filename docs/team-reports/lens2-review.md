# Lens review 2 — USB HID mouse/keyboard diff d2feee70..main (drivers/usb)

Scope: drivers/usb/xhci/controller_init.c (+13/-11), drivers/usb/xhci/hid_keyboard_mouse.c (+8/-7). Read-only static review, nothing built or run.

Verdict: no blocking regression for QEMU usb-kbd / usb-mouse / usb-tablet or for plain boot keyboards. All the buffer indexing checked is in bounds. Three real risks for wired hardware that worked before, ranked below.

## Findings (ranked)

### 1. MEDIUM — Transfer length can now exceed MPS because of reports the driver never decodes, which merges reports on mps-8 devices
- Where: controller_init.c:145 (hid_inlen is the max over every Input report ID) and controller_init.c:322 (`rlen = max(mps, inl)`, capped at 64).
- Issue: inl counts every input report in the descriptor, including vendor and other reports that hid_parse drops (kind 0, or layouts removed by the filter at :146). If the descriptor has any input report longer than MPS, rlen ends up larger than MPS for the whole interface.
- Failure: an interrupt TD only finishes on a short packet or when the full length has arrived. Take a mouse or keyboard with MPS 8 whose real report is exactly 8 bytes (for example ID + 7 bytes; Logitech-style receivers and gaming mice do this). That report is a full packet, so the TD does not finish and the next report is appended to it. In practice every other report is lost or shifted, and the second report's ID byte lands mid-buffer. The result is a jerky or dead mouse and dropped keys. Before this change rlen was exactly MPS, so these devices worked.
- Fix: compute inl only over report IDs that have a kept layout (track a bit length per kept `out[k].id` and take the max after the filter at :146). Even then, prefer rlen = mps whenever every kept report is <= mps.

### 2. MEDIUM — The GET_PROTOCOL fallback sends report-mode mice down the raw boot path, which reads stale DMA bytes
- Where: controller_init.c:305 (new fallback), hid_keyboard_mouse.c:84-87 (raw mouse decode, unchanged but now reached by more devices).
- Issue A: the raw mouse path ignores `got`. It always reads db[0..2], and reads db[3] whenever xhid_rlen >= 4. The boot path's rlen is `mps<8?mps:8`, which is 8 for most mice. A boot mouse sends 3 or 4 bytes, so db[3..] holds whatever was left in that 64-byte slot. Slot 0 of region d is cur_dbuf (DATABUF(d) == the EP0 data buffer), so on the first pass it still holds the report descriptor and config bytes from enumeration. Byte 3 of a typical mouse descriptor is 0x02, which gives phantom scroll. A ZLP (got = 0) replays stale buttons and motion. The keyboard path got a `got>=3` guard at :83; the mouse path did not.
- Issue B: firmware that answers GET_PROTOCOL with 0 while still sending report-protocol data (with a report ID prefix) is now decoded as boot. Byte 0 is the report ID, so buttons stick and motion is offset. Linux never issues GET_PROTOCOL; it trusts SET_PROTOCOL. Also, a successful zero-length data stage (cc 13) makes xh_ctrl copy stale DB[0] into `pr`.
- Fix: in hid_xfer's mouse branch, return if got < 3 and read the wheel only if got >= 4. Zero the 16x64 slot area (XHID_DATABUF(e)..+0x400) before the first xhid_arm. For the fallback, also require that the GET_PROTOCOL data stage returned 1 byte, or drop the fallback and keep report mode (old behaviour) unless SET_PROTOCOL(1) stalled.

### 3. LOW-MEDIUM — Boot keyboards now get a report-descriptor control transfer they never had before
- Where: controller_init.c:293-299 (the `bk` case now goes through the descriptor fetch at :295-298).
- Issue: before, sub 1 / proto 1 keyboards skipped GET_DESCRIPTOR(0x22) and went straight to boot mode. Now every boot keyboard does the fetch. A STALL is handled (xh_ctrl :200-201 resets EP0 and returns 0, giving nl=0 and boot mode, which is fine). A timeout (`!ok`, :199) is not handled: the TD stays on the EP0 ring and the next xh_ctrl (SET_PROTOCOL/SET_IDLE) can pick up the stale completion. That desyncs EP0 and can fail Configure Endpoint for a keyboard that used to work. The same exposure already existed for mice; this extends it to all keyboards.
- Also: keyboards with report IDs now move to report mode (m=1). There is no GET_PROTOCOL sanity check for keyboards (:305 covers proto 2 only), so we rely on hid_parse being correct for these keyboards. A wrong arr_off or mod_off now breaks a keyboard that boot mode would have handled.
- Fix: on an xh_ctrl timeout, issue Stop Endpoint and Set TR Dequeue on EP0, as the stall path does. Optionally keep bk interfaces in boot mode unless the parsed kind==1 layout also yields mod_off >= 0 and arr_cnt or bm_cnt > 0 (already partly checked by `ik`).

### 4. LOW — Errors on the interrupt endpoint never recover
- Where: hid_keyboard_mouse.c:88 and the xhid_arm that follows.
- Issue: any cc other than 1/13 (stall 6, babble 3, transaction error 4) only clears key repeat (new, correct) and then re-arms. A halted endpoint ignores the doorbell until Reset Endpoint and Set TR Dequeue, so the device goes silent until replug. This existed before. With rlen capped at 64 and HS HID MPS above 64, a packet larger than 64 bytes produces babble and triggers it.
- Fix: on cc 3/4/6, queue a Reset Endpoint (cmd 14) plus Set TR Dequeue for (slot, dci) before re-arming.

### 5. LOW — Modifier state is shared across keyboard layouts and devices
- Where: hid_keyboard_mouse.c:32-35.
- Issue: the per-layout prev buffers (new) are correct, but kbd_report still writes the global kdown[0x1D/0x2A/0x38/0x5B] from each report. On a device with two kind 1 layouts (now possible via kprevx), a report from layout 2 clears Shift or Ctrl held on layout 1, and the same happens between two keyboards. The behaviour already existed; the new design makes multi-layout keyboards a supported case.
- Fix: keep a per-(e, layout) modifier byte and set kdown from the OR across all of them.

## Verified OK (no action)
- DMA bounds: slots are 64 B x XHID_INFLIGHT(16) = 0x400 at DATABUF (+0x4000, region 0x8000). rlen is capped at 64 (:322), so no TD overruns its slot. The hid_xfer ptr/ba check (:78) is limited to XHID_INFLIGHT*64.
- Decode bounds: hid_bits (controller_init.c:149) checks off+sz <= len*8, and got <= rlen <= 64. The xhid_last copy is capped at 16. The boot keyboard path copies into the zero-padded kb[8] and requires got >= 3 (:83). This fixes the old code's direct db[0..7] read on short packets.
- Per-keyboard state: xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8] = [10][3][8]. kq counts kind 1 layouts before q, with q < HIDLAY_MAX = 4, so kq-1 <= 2 and the clamp at :71 is correct. It is cleared on enum (:325). xhid_reset (:103) does not clear it, but enum always does before use.
- Report ID handling: decode strips byte 0 when hasid (hid_keyboard_mouse.c:54) and matches L->id. hid_parse masks rid to 8 bits (:117), so bitpos[256] is safe. inl adds the +1 ID byte (:145). bitpos only advances on Input items (tag 8), so Output and Feature reports do not inflate inl (but see finding 1 for dropped input reports).
- Enumeration merge (:293-302): lays[k] is written with k <= j, so already-compacted entries are not clobbered. A discarded bk parse leaves stale lays[k] with nlays=0, which is harmless. inl[k] is carried with the entry. keep=1 for bk is the same as before.
- QEMU: usb-kbd has no report IDs, so hid=0, nl=0, and it stays in boot mode, same as before plus one harmless descriptor fetch. usb-mouse/usb-tablet: QEMU answers GET_PROTOCOL with s->protocol, which is 1 after SET_PROTOCOL(1), so they stay in report mode. Their inl (3-6 B) <= mps (8), so rlen = mps as before.
- Key repeat cleared on a transfer error for that device (:88): correct.
