# ramp review: A-MPDU RX (p-amp 78f97bc0, merge 0d9e987e in main)

Scope: net/wifi/wifi_ampdu.c/.h, net/wifi/wifi_netdev.c (main), p-amp log -3, p1/amp-report.md.

## Merge fidelity (lead's hand-merge into wnd_rx_frame)
Compared `git show 0d9e987e --cc` line by line against p-amp's inline version: the move is faithful.
- PN: `if(!ba&&pn<=*pnslot)` (191), deferred `*pnslot=pn` (207), and release-time check in wnd_ba_pop (159) are identical to p-amp.
- A-MSDU in BA (206), MIC-fail placeholder (222), open path (178), EAPOL in BA (wnd_ba_put 164): identical.
- hwhint expression at 244 equals p-amp's `hwdec` expression; `goto next` -> `return outlen` is equivalent.
- Mgmt path (243) still runs wnd_ba_pop after wnd_rx_mgmt (needed after DELBA). The new `(base[hoff]&0x0C)==0x08` gate on 244 is equivalent to p-amp's inner check, so a mgmt frame's popped outlen can't be overwritten.
- Timeout: ampdu_tick + BA-queue drain run at the top of every RTL wnd_rx_eth call (233), before descriptors, so a hole is flushed after 100 ms (10 ticks, pit_init(100) confirmed) even when no frames arrive, as long as the stack polls (net_idle_poll / socket polls).
- Ready-queue invariant holds on the RTL path: wnd_ba_pop returns 0 only when the queue is empty, and any non-zero return exits the loop. So ampdu_rx's `if(g_amp_rn)` drop (wifi_ampdu.c:36) can't fire on the owner's RTL8821CE.
- Buffer limits are fine: slot 1520 >= 14+1500, ampdu_rx clamps len>1520 to a placeholder, ampdu_pop drops L>max, ADDBA reads fr[24..32] = exactly the checked 33 bytes, win<=64 within 128 slots (no aliasing).
- No regression for RTL BT: the merge touches no BT code except adding bcm_run.sh to tools/tests/bluetooth/run.sh.
- Licence: wifi_ampdu.c is original code with no ported text, so no NOTICE entry is needed.

The issues below come from p-amp's design and were carried into main unchanged.

## MEDIUM 1: forged or replayed frames can hijack the BA window (remote DoS; MFP doesn't help)
net/wifi/wifi_netdev.c:222 feeds a MIC-failure frame into the reorder buffer as a placeholder. Anyone who sets A2=BSSID, A1=us, Protected=1, a TID with an active session and seq = head+200 triggers `amp_all; head=seq` (wifi_ampdu.c:39). Every legitimate frame after that has d>=2048 and is dropped as "old" until the AP's sequence numbers catch up (~1000 frames), and one frame repeats the attack. Before v11.09 forged frames were simply dropped. Line 191 also skips the PN check for BA frames, so a captured frame replayed with a changed seq (seq is not in the CCMP AAD) takes a slot, and the real frame with that seq is then dropped as a duplicate.

The early check is safe: `*pnslot` for a BA TID only grows, and it is updated only on in-order release. A frame with pn<=*pnslot is therefore always dropped later at wnd_ba_pop (159), so dropping it early never loses a legitimate frame; it just stops the frame from occupying a slot or moving the window. A 100 ms timeout closes the hole a dropped MIC-failure frame leaves.
```
--- net/wifi/wifi_netdev.c:191
-                    if(!ba&&pn<=*pnslot){ g_wnd_rx_drop++; return outlen; }
+                    if(pn<=*pnslot){ g_wnd_rx_drop++; return outlen; }
--- net/wifi/wifi_netdev.c:222
-                    } else { g_wnd_rx_drop++; if(ba){ ampdu_rx(prio&15,bseq,0,0,0,g_ticks); outlen=wnd_ba_pop(out,max); } }
+                    } else g_wnd_rx_drop++;
```

## MEDIUM 2: an A-MSDU on a BA TID is delivered before the duplicate check, and in-window replays get through
wifi_netdev.c:206 runs wnd_amsdu (immediate delivery) first and only then ampdu_rx. If the AP retransmits an A-MSDU (its BlockAck was lost), the duplicate check in ampdu_rx comes too late, so the subframes are delivered twice. *pnslot isn't advanced until release, so the same applies to a replay while its placeholder is still pending. Before v11.09 the A-MSDU path had a strict PN check, so this is a replay-protection regression. It needs an AP that sends A-MSDUs despite our "A-MSDU not allowed" reply; the report lists it as a known limit, but the code doesn't drop the frame either. Fix: let ampdu_rx tell "stored" from "old/dup" and check that before delivering.
```
--- net/wifi/wifi_ampdu.c:36,38,41  (return 2 = not stored: old/dup/busy)
-    if(g_amp_rn){ g_ampdu_old++; return 1; }
+    if(g_amp_rn){ g_ampdu_old++; return 2; }
-    if(d>=2048){ g_ampdu_old++; return 1; }
+    if(d>=2048){ g_ampdu_old++; return 2; }
-    int k=seq%AMPDU_SLOTS; if(s->st[k]){ g_ampdu_old++; amp_inorder(s); return 1; }
+    int k=seq%AMPDU_SLOTS; if(s->st[k]){ g_ampdu_old++; amp_inorder(s); return 2; }
--- net/wifi/wifi_netdev.c:206
-                        if(ba){ if(fr[24]&0x80){ outlen=wnd_amsdu(pt,dlen,out,max); ampdu_rx(prio&15,bseq,0,0,pn,g_ticks); if(!outlen)outlen=wnd_ba_pop(out,max); return outlen; } outlen=wnd_ba_put(prio&15,bseq,fr,pt,dlen,pn,out,max); return outlen; }
+                        if(ba){ if(fr[24]&0x80){ if(ampdu_rx(prio&15,bseq,0,0,pn,g_ticks)==2){ g_wnd_rx_drop++; return wnd_ba_pop(out,max); } outlen=wnd_amsdu(pt,dlen,out,max); if(!outlen)outlen=wnd_ba_pop(out,max); return outlen; } outlen=wnd_ba_put(prio&15,bseq,fr,pt,dlen,pn,out,max); return outlen; }
--- tools/tests/wifi/ampdu/ampdu_test.c:12
-... int r=ampdu_rx(tid,(u16)seq,f,len,(u64)seq+1,0); if(r!=1){ ...
+... int r=ampdu_rx(tid,(u16)seq,f,len,(u64)seq+1,0); if(r<1){ ...
```
Verified in a /tmp copy: ampdu_test with the return-2 change and the test fix passes under ASan/UBSan ("ampdu: all ok").

## LOW 3: EAPOL inside BA is processed before the duplicate check
wifi_netdev.c:164 calls wpa_rx_eapol before ampdu_rx, so a retransmitted encrypted EAPOL (GTK rekey) is handed to WPA twice. The EAPOL replay counter makes this harmless, and MEDIUM 1 covers old PNs. With MEDIUM 2 applied:
```
-        if(et==0x888E){ if(pn)wpa_rx_eapol(llc+8,dl-8); }
+        if(et==0x888E){ if(ampdu_rx(tid,seq,0,0,pn,g_ticks)!=2&&pn)wpa_rx_eapol(llc+8,dl-8); return wnd_ba_pop(out,max); }
```

## LOW 4: an open-network A-MSDU on a BA TID leaves a hole, so every one causes a 100 ms stall
wifi_netdev.c:178: the open path sends a BA A-MSDU to wnd_amsdu without a placeholder (the encrypted path does feed one).
```
-            if((fc&0x80)&&(fr[24]&0x80)&&dl2>0){ outlen=wnd_amsdu(fr+d,dl2,out,max); return outlen; }
+            if((fc&0x80)&&(fr[24]&0x80)&&dl2>0){ if(ba&&ampdu_rx(prio&15,bseq,0,0,0,g_ticks)==2)return wnd_ba_pop(out,max); outlen=wnd_amsdu(fr+d,dl2,out,max); if(ba&&!outlen)outlen=wnd_ba_pop(out,max); return outlen; }
```

## LOW 5 (latent): the generic wifi_hw RX path never ticks or drains the BA queue
wifi_netdev.c:230 has no ampdu_tick and no wnd_ba_pop. If a session ever existed there, holes would never be flushed, and wifi_ampdu.c:36 would drop every new frame while released frames are still queued. It can't happen today because wm_gen_up passes qos=0 (wifi_mgr.c:136), so ADDBA is declined. Guard it anyway:
```
-    if(!g_wnd_mmio&&wifi_hw_generic()){ if(!g_wnd_up){ g_amqn=0; return 0; } { int r=wnd_amq_pop(out,max); if(r)return r; } wifi_hw_poll(); ...
+    if(!g_wnd_mmio&&wifi_hw_generic()){ if(!g_wnd_up){ g_amqn=0; return 0; } { int r=wnd_amq_pop(out,max); if(r)return r; ampdu_tick(g_ticks); r=wnd_ba_pop(out,max); if(r)return r; } wifi_hw_poll(); ...
```

## LOW 6: re-ADDBA on an active TID throws away buffered frames
wifi_ampdu.c:28-29: a second ADDBA for the same TID clears st[] without releasing anything, so up to 63 frames are lost (TCP recovers). Use a mac80211-style update and keep the session state:
```
-    amp_purge();
+    if(s->on){ s->win=(bufsz&&bufsz<AMPDU_WIN)?bufsz:AMPDU_WIN; g_ampdu_addba++; return s->win; }
+    amp_purge();
```

## LOW 7: protected action frames can be misparsed as ADDBA
wifi_netdev.c:138 tests fr[24]==3&&fr[25]==0 on protected frames too, where those bytes are CCMP PN0/PN1 (1 in 65536). The result is a decline carrying a garbage token. Add `&&!(fr[1]&0x40)` to the 138 condition.

## LOW 8 (outside A-MPDU scope, seen in main 53be34a4)
The version-bump commit rewrote historical `/* v11.08: ... */` comments to `v11.09` (rtw8821ce.c:447,537,546,551,597,611; rtw_sec.c:123,...), which misdates when those changes were made. Revert those comment lines.

## Not covered
No hardware test was possible. The report's claim that the 8821CE MAC sends BlockAcks in hardware on PCIe can only be confirmed on the owner's AP (`wifi diag`: accepted / reordered). The `wifi ampdu off` fallback works.
