# amp report: A-MPDU receive for the RTL8821CE

Branch p-amp, commit 78f97bc0. Worktree /home/claude/wt/amp.

## What I did
- **net/wifi/wifi_ampdu.c + include/nvx/net/wifi/wifi_ampdu.h (new, my own code).** This is the per-TID RX reorder buffer. It allows up to 2 concurrent block-ack sessions, a window of up to 64, and 128 slots of 1520 bytes each (kmalloc'd once per session, about 190 KB). It covers:
  - the 12-bit sequence space, with old/duplicate detection (d >= 2048)
  - shifting the window when a frame lands past it, and a full flush when the jump is 2x the window or more
  - a 100 ms timeout flush (`AMPDU_TMO` = 10 ticks at 100 Hz)
  - DELBA, which releases everything buffered
  - zero-length placeholders, so frames we don't deliver (EAPOL, MIC failure, non-LLC) still move the window

  Released frames go into a ready queue that `wnd_rx_eth` always empties before it reads new descriptors.
- **net/wifi/wifi_netdev.c**
  - **ADDBA:** the old "always decline (37)" is now `wnd_addba`. It accepts with status 0, immediate policy, A-MSDU not allowed, buffer min(requested, 64), and echoes the timeout. It still declines with 37 when the kill switch is off, the link has no QoS, the policy is delayed, there is no free session, or the frame is protected.
  - **DELBA:** a DELBA from the AP (initiator bit set) ends the session.
  - **Unicast QoS data on a BA TID:** CCMP is verified without the PN check, and the frame goes through the reorder buffer. The per-TID PN replay check happens when frames are released in order, which is what mac80211 does. The open-network path is handled the same way.
  - `ampdu_reset()` runs on `wnd_setup` and `wnd_down`.
- **drivers/net/wifi/rtw88/rtw8821ce.c (a one-byte change).** The HT capabilities A-MPDU parameters byte goes from 0x17 to 0x13: max length 64K (exponent 3) and MPDU density 2 µs (`IEEE80211_HT_MPDU_DENSITY_2`), matching rtw88's 8821c `ampdu_density`. Max A-MSDU stays at 3839.
- **RX descriptor / RXDMA aggregation (facts from rtw88).** rtw88 turns on `REG_RXDMA_AGG_PG_TH` / `BIT_RXDMA_AGG_EN` only for USB and SDIO. On PCIe every MPDU arrives in its own RX buffer descriptor, so the existing descriptor loop already handles aggregated frames. rtw88's `ampdu_action` does nothing for RX_START, because the MAC sends the BlockAck in hardware. Our MAC init and RCR already match rtw88, so I made no register change.
- **Shell (system_disks_packages_gpu.c):**
  - `wifi ampdu on|off` (default on). Off resets the sessions and declines new requests.
  - A new `wifi diag` line with: sessions now, accepted, declined, ended by AP, frames reordered, flushed after timeout, old/duplicate.
  - The help text is updated.
- The new unit is wired into units.list and kernel64.c, and the header is added to kernel.h and shell/commands.c.
- **Host test:** tools/tests/wifi/ampdu/ampdu_test.c. It compiles the real wifi_ampdu.c with ASan/UBSan and covers in-order release, holes, the window shift, the 4095 to 0 wrap, the timeout flush, placeholders, DELBA, session limits, the kill switch, and 200 random reorder rounds. It is registered in `tools/check.py --only wifi`.

## How I tested it
- ccheck on every C file I touched, including the whole of kernel64.c: no new warnings (wifi_netdev.c actually has one fewer).
- `python3 tools/check.py --only wifi`: 15 of 15 passed.
- Not run on real hardware.

## Untested and known limits
- **Not tested on the owner's AP.** Once connected, run `wifi diag` and look at "accepted" and "reordered". If downloads get worse, use `wifi ampdu off` and reconnect.
- **Protected ADDBA is ignored, as before.** With PMF/MFP (always on WPA3/SAE, optional on WPA2), the AP sends a protected ADDBA and gets no reply, so the link stays non-aggregated. Follow-up: decrypt robust action frames (the deauth path already does CCMP for management frames) and protect the response.
- **No BAR handling.** Control frames are filtered by RCR/RXFLTMAP1, so a gap is only closed by the 100 ms timeout. Follow-up: enable BAR-only receive (RXFLTMAP1 bit 8 plus RCR ACF) and move the window to the BAR's SSN.
- **A-MSDU on a BA TID.** If an AP sends one anyway (our response disallows it), it bypasses reordering and the PN check.
- **TX aggregation not done.** It needs us to send our own ADDBA, track the response, and set TX desc w2 AGG_EN bit 12, AMPDU_DEN bits 20-22 and w3 MAX_AGG_NUM, plus a QoS TID in the TX path, which currently always uses TID 0. I left it for later to stay safe.

## Merge note
The only rtw8821ce.c change is one byte in `rtw_assoc_ies_mid`, so it should merge cleanly with main's new net/wifi/wifi_hw.c. Everything else is in net/wifi and the shell. I did not change rtw_chip.h.
