# hwops report (p1)

Branch `p-hwops`, commit 0647276b, worktree /home/claude/wt/hwops.

## What I did
- Added a small Wi-Fi hardware ops table: `include/nvx/net/wifi/wifi_hw.h` and `net/wifi/wifi_hw.c` (added to units.list).
- rtw88 (`drivers/net/wifi/realtek/rtw88/rtw8821ce.c`) now registers through it as a **native** driver (`WIFI_HW_NATIVE`). Native means wifi_mgr keeps using its existing inline rtw path, so nothing changes for the owner's RTL8821CE. The generic path runs only when `rtw_find_mmio()==0`, which used to mean "No Wi-Fi adapter", and only if a non-native driver has registered.
- I split `rtw_scan_frame` into a descriptor wrapper plus a generic `wifi_scan_parse(fr,flen,ch,rssi,onch)`. The logic is the same; the scan host test now pulls in the new function.
- I split the per-frame data/CCMP code in `wnd_rx_eth` into `wnd_rx_frame(fr,len,hwhint,out,max)`. The rtw ring loop calls it with the same hw-decrypt hint taken from the descriptor.
- In `wnd_tx_eth`, a generic driver gets the finished 802.11 frame (CCMP done in software) through `ops->tx`. Management frames sent from netdev (ADDBA decline) go to rtw or the generic driver.
- New generic branches in wifi_mgr: `wifi_up` -> `init`, scan -> `scan_start` plus `poll`/`scan_done` (8 s cap), join (open networks only) -> `join`, and `leave` on disconnect, lost link and radio off. I added guards so `rtw_ra_connect`/`rtw_ra_rssi`/`rtw_ra_disconnect` are never called with m=0. Info text shows the generic driver's name.

## Contract for the iwx coder (wifi_hw.h)
```c
struct wifi_hw_ops { const char* name; u32 flags;            /* flags=0 for iwx (NOT WIFI_HW_NATIVE) */
  int  (*init)(void* ctx);                                    /* power up, load fw, ready to scan; 0 ok, <0 fail */
  int  (*scan_start)(void* ctx,const u8* ch,int n);           /* start a scan over these channels; return at once */
  int  (*scan_done)(void* ctx);                               /* !=0 when finished (or failed); host caps at 8 s */
  int  (*join)(void* ctx,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht); /* auth+assoc, blocking with timeouts; 0 ok, 1..4 = wm_err codes */
  void (*leave)(void* ctx);                                   /* deauth/disassoc, drop the station */
  int  (*tx)(void* ctx,const u8* frame,int len);              /* send one complete 802.11 frame (no FCS), already encrypted by the host; <0 = full/fail */
  int  (*set_key)(void* ctx,int idx,int type,const u8* key,int len,int pairwise); /* optional, not called yet */
  int  (*get_rssi)(void* ctx);                                /* dBm or -127 */
  void (*poll)(void* ctx);                                    /* drain the RX ring and call wifi_hw_rx() per frame */
  int  (*get_mac)(void* ctx,u8* mac); };                      /* required for join */
int  wifi_hw_register(const struct wifi_hw_ops*,void* ctx);
void wifi_hw_rx(const u8* frame,int len,int rssi);
```
Rules:
- Register from a probe function only when your PCI ID matches. Add a call to it in `whw_probe()` in net/wifi/wifi_hw.c, after `rtw_wifi_hw_probe()`. The lead merges this one line. Probing runs once, on first use. The first driver registered is the active one, and rtw always goes first.
- Call `wifi_hw_rx` from `poll` only, never from an IRQ handler. The frame is the raw 802.11 frame with the FCS stripped, and protected frames must still be encrypted (CCMP header and MIC intact), because the host does CCMP in software. Pass rssi in dBm, or -127 if unknown. Management frames are parsed straight away (scan list, beacons for our BSS, deauth). Data frames are queued (16 x 2400 bytes) and picked up by `wnd_rx_eth`.
- The host calls `poll` during scans and from the netdev RX path while connected. Every wait inside your ops needs a timeout.
- `scan_ap`/`AKM_*` still live in rtw8821ce.h. I did not move them, to keep the change small.

## Tests
- `/home/claude/ccheck.sh` on all touched files: no new warnings (warning counts are the same as before).
- `python3 tools/check.py --only wifi`: 13/13 passed, including the scan parser test (200000 random frames) and the manager test.

## Not tested / follow-ups
- The generic path has not been run at all: there is no second driver yet and I could not boot the kernel here. It needs iwx to exercise it.
- WPA2/WPA3/EAP on a generic driver returns error 4 ("rejected this device (security type)") and logs "this adapter can only join open networks so far". The next step is to pull the 4-way handshake in `rtw_wpa2_join` (rtw_sec.c) out to go over `wifi_hw_tx` and an EAPOL wait on the wifi_hw RX queue, and to call `set_key` if the hardware decrypts.
- There is no new host test for `wifi_scan_parse(...,onch=1)` or for the generic RX queue. Worth adding.
- Shell commands (`network_and_apps.c`), the desktop and syscalls still call rtw directly. They are guarded by `rtw_find_mmio()`, so they stay inactive for a generic driver, but their UI will not show it.
- rtw's own ops (scan_start/tx/leave/...) are thin wrappers that wifi_mgr does not call today, because native keeps the old path.
- I did not touch `rtw_chip.h` or any NOTICE file. No outside code was copied.
