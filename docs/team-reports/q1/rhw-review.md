# rhw review: wifi_hw interface (p-hwops 0647276b, as merged in main 8f0ec653)

Scope: include/nvx/net/wifi/wifi_hw.h, net/wifi/wifi_hw.c, and the hwops changes to net/wifi/{wifi_mgr,wifi_netdev}.c and drivers/net/wifi/rtw88/rtw8821ce.c. I also read /home/claude/wt/iwxi (the uncommitted iwx wiring) to check the contract. I made no changes to the repo.

## Verdict on the main question: the RTL8821CE path is unchanged

**No HIGH findings.** The report's claim that "nothing changes for the owner's RTL8821CE" holds. Evidence:

1. **Gating.** Every new branch in wifi_mgr.c is behind `!m` with `m=rtw_find_mmio()`. Every new branch in wifi_netdev.c is behind `!g_wnd_mmio`.
   - `rtw_find_mmio()` (graphics/ui/desktop/app_tiles_and_icons.c:249) caches `g_wifi_mmio`. Once the card is found, it never returns 0 again.
   - `g_wnd_mmio` is set to `m` by every rtw join (open, WPA2/WPA3 in rtw_sec.c, EAP), so it is non-zero whenever the netdev is up.
   - `&&` short-circuits, so on the owner's machine `wifi_hw_ops_get()` and `whw_probe()` are never even called. This includes the boot-time `sysfiles_build -> wifi_info_text`, which takes the `m!=0` branch.
2. **Join, WPA2/WPA3, EAP, reconnect, DHCP.**
   - In `wm_connect` (wifi_mgr.c:232-247), `gen` is 0, so the old if/else chain runs unchanged.
   - `rtw_ra_connect`, `rtw_ra_disconnect` and `rtw_ra_rssi` now have an `m` guard. That only skips calls that used to be made with m=0, which wrote to MMIO address 0, so it fixes something rather than changing behaviour.
   - `wm_lost`, `wifi_disconnect`, `wifi_radio_off` and the WM_OFF auto-reconnect branch (`goto wm_off_done`) all run exactly as before when `m!=0`.
3. **Scan.** The `wifi_scan_parse` split is equivalent. I diff-fuzzed the old `rtw_scan_frame` (from 0647276b^) against the new wrapper plus parser (`/tmp/rhwd/d.c`, ASan+UBSan):
   - 3,000,000 random frames, 1,461,584 `scan_add` calls.
   - The return value, the hash of every `scan_add` argument, and `g_scan_grp` were all compared: **0 differences**.
4. **RX and TX.**
   - `wnd_rx_frame` is a line-for-line rebase of the old inline block: offsets are now relative to `fr`, `fe` is now `fe-hoff`, and each `goto next` is now `return outlen`. The hw-decrypt hint is the same expression on the same `rw0`.
   - `wnd_tx_eth`, `wnd_tx_mgmt` and `wnd_addba` are unchanged when `g_wnd_mmio!=0`.
   - Note that the merge with p-amp later changed `wnd_rx_frame` and the ring loop (A-MPDU). That is not hwops. With `ba==0` it reduces to the hwops version.
5. **Tests.** `tools/check.py --only wifi` on an exported copy of main (`/tmp/rhw`): 15/15 passed.

Out of scope, for the p-amp reviewer: the owner's association request HT A-MPDU parameter byte changed from 0x17 to 0x13 (drivers/net/wifi/rtw88/rtw8821ce.c:654). This came from the p-amp merge, not hwops.

## Findings

### MEDIUM-1: the probe runs only once and is never retried, and the iwx wiring does a heavy, blocking start inside it
- **Where:** net/wifi/wifi_hw.c:9 (`whw_probed=1` is set even when nothing registered). In iwxi, the probe calls `iwx_start()`, which loads firmware from disk and waits for the card's "alive" message (iwxi drivers/net/wifi/iwx/iwx_glue.c:52; iwx_pcie.c:253).
- **What goes wrong:** on an Intel machine the first `wifi_hw_generic()` call is `sysfiles_build -> sf_hardware -> wifi_info_text` at boot (core/sysfiles.c:346).
  - Boot blocks on the firmware load.
  - If that load fails (firmware not installed yet, disk slow), the latch means Wi-Fi never appears until reboot. The only way around it is the `iwx_up` shell command.
  - The contract says "register from probe when your PCI ID matches", and `init` is "power up, load fw". iwxi does both in probe.
- **Patch, net/wifi/wifi_hw.c:**
```c
-static void whw_probe(void){ if(whw_probed)return; whw_probed=1; rtw_wifi_hw_probe(); }
+extern volatile u64 g_ticks; static u64 whw_last=0;
+static void whw_probe(void){ if(whw_n)return; if(whw_probed&&g_ticks-whw_last<3000)return; whw_probed=1; whw_last=g_ticks; rtw_wifi_hw_probe(); }
```
  (When iwx is merged, keep `if(!whw_n)iwx_wifi_hw_probe();` after `rtw_wifi_hw_probe();`. On the owner's machine `whw_n` becomes 1, so nothing changes there.)
- **Patch, iwxi drivers/net/wifi/iwx/iwx_glue.c:**
```c
-static int iwx_g_init(void* ctx){ struct iwx_softc* sc=(struct iwx_softc*)ctx; if(!sc)return -1; iwx_rx_deliver=iwx_glue_rx; return iwx_init_hw_cmds(sc); }
+static int iwx_g_init(void* ctx){ struct iwx_softc* sc=(struct iwx_softc*)ctx; if(!sc)return -1; if(iwx_start()!=0)return -1; iwx_rx_deliver=iwx_glue_rx; return iwx_init_hw_cmds(sc); }
-void iwx_wifi_hw_probe(void){ if(!iwx_probe())return; if(iwx_start()==0)iwx_glue_register(); }
+void iwx_wifi_hw_probe(void){ if(iwx_probe())wifi_hw_register(&iwx_hw_ops,&g_iwx); }
```
  This also needs `iwx_g_init` to tolerate being called again after a failure. `wm_gen_up` already retries on each `wifi_up`.

### MEDIUM-2: the host calls `ops->tx` re-entrantly from inside the driver's own RX loop
- **Where:** net/wifi/wifi_hw.c:33-38 calls `wnd_rx_mgmt_hw` (wifi_netdev.c:140). That reaches `wnd_rx_mgmt -> wnd_addba` (wifi_netdev.c:138 -> 117), then `wnd_tx_mgmt -> wifi_hw_tx -> ops->tx`.
- **What goes wrong:** this all happens synchronously inside `ops->poll`. In iwxi it also happens inside `iwx_send_cmd_sync`, which calls `iwx_rx_poll` while waiting for a command response (iwx_cmd.c:71).
  - The driver's TX ring and command queue are touched while its RX loop is still walking the ring, or while a host command is in flight.
  - The contract does not mention this case, so the iwx coder had no reason to make tx re-entrant.
- **Patch, net/wifi/wifi_hw.c:** defer TX that arrives while inside `wifi_hw_rx`, and flush it on the next poll.
```c
+static int whw_in_rx=0, whw_txn=0; static u8 whw_txq[2][64]; static int whw_txl[2];
+static void whw_flush(const struct wifi_hw_ops* o){ int n=whw_txn; whw_txn=0; for(int i=0;i<n;i++)o->tx(whw_ctx[0],whw_txq[i],whw_txl[i]); }
-int wifi_hw_tx(const u8* frame,int len){ WHW(tx)return -1; if(!frame||len<10||len>2304)return -1; return o->tx(whw_ctx[0],frame,len); }
+int wifi_hw_tx(const u8* frame,int len){ WHW(tx)return -1; if(!frame||len<10||len>2304)return -1;
+    if(whw_in_rx){ if(len>64||whw_txn>=2)return -1; for(int i=0;i<len;i++)whw_txq[whw_txn][i]=frame[i]; whw_txl[whw_txn++]=len; return 0; }
+    return o->tx(whw_ctx[0],frame,len); }
-void wifi_hw_poll(void){ WHW(poll)return; o->poll(whw_ctx[0]); }
+void wifi_hw_poll(void){ WHW(poll)return; if(o->tx&&whw_txn)whw_flush(o); o->poll(whw_ctx[0]); if(o->tx&&whw_txn&&!whw_in_rx)whw_flush(o); }
 void wifi_hw_rx(const u8* fr,int len,int rssi){
     ...
-    if(t==0x00){ if(g_wifi_scanning&&wifi_scan_parse(fr,len,g_scan_ch,rssi,1))g_rtw_rx_bcn++; wnd_rx_mgmt_hw(fr,len,rssi); return; }
+    if(t==0x00){ if(g_wifi_scanning&&wifi_scan_parse(fr,len,g_scan_ch,rssi,1))g_rtw_rx_bcn++; whw_in_rx=1; wnd_rx_mgmt_hw(fr,len,rssi); whw_in_rx=0; return; }
```
  Also add a line to the wifi_hw.h rules: "ops->tx is never called from inside wifi_hw_rx". rtw is not affected: it is native and never calls `wifi_hw_rx`.

### LOW-1: a generic scan while connected does not drain the netdev
- **Where:** net/wifi/wifi_mgr.c:195.
- **What goes wrong:** the generic branch polls the driver but never calls `net_idle_poll()`. The rtw branch does this at line 197 when connected. For up to 8 s, data frames pile up in the 16-slot `whq` and get dropped.
```c
-    u32 m=rtw_find_mmio(); if(!m){ if(wifi_hw_generic()){ wifi_hw_poll(); if(sc_on&&
+    u32 m=rtw_find_mmio(); if(!m){ if(wifi_hw_generic()){ if(g_wm_state==WM_CONNECTED)net_idle_poll(); else wifi_hw_poll(); if(sc_on&&
```

### LOW-2: the generic RX path never runs the A-MPDU flush (latent)
- **Where:** net/wifi/wifi_netdev.c:230.
- **What goes wrong:** the generic branch never calls `ampdu_tick` or `wnd_ba_pop`. This cannot be reached today: a generic join uses qos=0, so ADDBA is declined. Once QoS or WPA2 is added for generic drivers, frames would sit in the reorder buffer.
```c
-    if(!g_wnd_mmio&&wifi_hw_generic()){ if(!g_wnd_up){ g_amqn=0; return 0; } { int r=wnd_amq_pop(out,max); if(r)return r; } wifi_hw_poll();
+    if(!g_wnd_mmio&&wifi_hw_generic()){ if(!g_wnd_up){ g_amqn=0; return 0; } { int r=wnd_amq_pop(out,max); if(r)return r; ampdu_tick(g_ticks); r=wnd_ba_pop(out,max); if(r)return r; } wifi_hw_poll();
```

### LOW-3: gaps in the contract for an Intel firmware driver (document these now, implement later)
- **No radio-off or airplane op.** `wifi_radio_off` (wifi_mgr.c:157) only calls `leave`, and only when connected. Airplane mode leaves the Intel firmware powered. Add `void (*radio)(void* ctx,int on);` and call it from `wifi_radio_off`/`wifi_radio_on` when `!m`.
- **No link-loss event.** The host detects loss only from deauth frames or missing beacons (wifi_mgr.c:316). Firmware beacon filtering must stay disabled; iwxi sends a zeroed `BEACON_FILTERING_CMD`, which is correct. Firmware "missed beacons" must be reported by injecting a deauth through `wifi_hw_rx` from the BSSID. Write both of these into the header rules.
- **No hw-decrypted flag on `wifi_hw_rx`, and `set_key` is never called.** WPA2/WPA3/EAP are unavailable on generic drivers.
  - Error 4 then shows "the network rejected this device (security type)", which blames the router. A dedicated `wm_err` code saying "this Wi-Fi adapter can only join open networks so far" would be honest.
  - So, as it stands, the contract is sufficient for open networks only, not for daily use on an Intel laptop.
- **Info text shows the wrong MAC.** It is taken from rtw's efuse only (wifi_mgr.c:335). For a generic driver it should come from `wifi_hw_mac()`.
- **The poll rule is missing from the header.** It only appears in the report: "Every wait in ops needs a timeout; poll is only called during scans and from the netdev RX path while connected." Put it in wifi_hw.h.

### Notes on iwxi against the contract (not in main yet)
- `iwx_g_leave` is a no-op, which breaks the `leave` rule. `iwx_join` currently returns -1 ("not implemented"), so there is no end-to-end generic join on Intel yet. The generic path in main is therefore still unexercised on hardware, as the hwops report says.
- `iwx_g_scan_start` ignores the host's channel list. That is fine for now.
- RX strips the header pad (`IWX_RX_MPDU_MFLG2_PAD`, iwx_data.c:47), as `wnd_rx_frame` requires.
- Licence: wifi_hw.c/.h are original code, and they have no header, the same as the other net/wifi files. Nothing to fix there.
