Task amp: A-MPDU (frame aggregation) for the RTL8821CE (drivers/net/wifi/rtw88/, net/wifi/). Goal: faster downloads.
1. RX (most important): answer ADDBA Request action frames from the AP with ADDBA Response (accept, buffer size 64, block-ack policy
   immediate), track per-TID BA sessions, handle DELBA, and add a per-TID RX reorder buffer (sequence window, timeout flush ~100 ms) so
   out-of-order MPDUs from an aggregate are delivered in order. Make sure the RX descriptor path accepts aggregated frames (check rtw88
   facts: rx desc PPDU/AMPDU bits, and that RX aggregation is enabled in the chip's TRXDMA/RXDMA_AGG registers as rtw88 does).
2. Advertise HT capabilities correctly in the association request (A-MPDU params, max length exponent, min spacing) consistent with 1.
3. TX aggregation only if simple and safe (set the TX desc AGG_EN/ampdu density bits as rtw88 does after sending our own ADDBA);
   otherwise leave TX for later and say so.
Reference: Linux rtw88 (GPL-2.0 OR BSD-3-Clause, usable under BSD-3), mac80211 facts. Add `wifi ampdu on|off` (default on) as a kill
switch and counters in the existing `wifi diag` output (BA sessions, reordered, flushed). Host test for the reorder buffer.
