# r6 review - c6 (network bugs), branch x-c6 merged into main as v11.07

Scope: `git log -p v11.06..x-c6` (4a8131cd, 6f70a8b6, af917b26), c6-report.md, ctasks/c6.md, h7-report.md. I checked the merged files in main. The only other changes in c6's files are c2's kidle line in net/http/http.c, c7's netlink bound in sys/linux/lxsock.c, c3's apt le_push and the v11.07 comment bumps. None of them conflicts with c6.

Verdict: **no HIGH.** Nothing in c6 changes how the PC joins a network or how it gets its first DHCP address. The DISCOVER and REQUEST packets are the same, and the wired synchronous dhcp_config is not touched. The two link-loss changes (5 s beacon floor, data frames count as proof of life) only make disconnects less likely. Two fixes are incomplete (MEDIUM). Three small issues are LOW.

## Checked and correct
- **#1 group key** (wifi_netdev.c:13, :28, :32, :179). The fix is right. The hardware decrypts old-keyid broadcasts from its own key slot (CAM), and each key id now has its own replay counter. A software-decrypted frame with a stale key id still fails the MIC and is dropped, same as before. wnd_set_gtk resets only the new key id's slot when the key bytes change, which also covers routers that alternate key ids 1/2. keyid is `(cc[3]>>6)&3`, so the index is always inside [4]. Not a false fix.
- **#4** wifi_netdev.c:195. `g_wnd_beacon_t` is refreshed only after a frame passes CCMP (or hardware decrypt) and comes from our BSSID (checked at :160). Nothing else reads it except the "no signal" check and scan_finish. wifi_mgr.c:304 floor 500: it only loosens the check.
- **#3 renew** tcpip.c:188. Checked against dhcp_build: flags are at p+10, ciaddr at p+12. With ciaddr set and no options 50/54 this is a legal RFC 2131 REQUEST. Because it goes to 255.255.255.255 it is strictly the REBINDING form, but servers answer it the same way, by unicast to ciaddr. The unicast ACK arrives with dst=my_ip and is caught by the take in lxs_frame before the dst filter, or by the step loop. The first DHCP after joining (da_ren=0) is unchanged. Worst case, a router ignores it: the renew times out ("keeping the address") and the lease ends with a full DISCOVER. That is today's failure mode, not a new one.
- **#2 loop safety** tcpip.c:203. Stashed replies are never passed to net_pass, so they cannot be stashed again. The DHCP-reply check runs before the dst-IP filter in lxs_frame (:143), so a unicast OFFER sent while my_ip=0 is no longer dropped. dns_try: a reply from a router that is also the DNS server still reaches net_pass at :255, because its source port is 67, not 53.
- **my_ip guards** in dns_try, tcp_get and tcps_open. dns_query returns -1 before the negative-cache line, so nothing is cached. Wired setups always have my_ip≠0, either from DHCP or because dhcp_config restores save_ip.
- **#5** nd_poll(…,1) waits 1 ms (1193 PIT counts), so 6000 = 6 s. `idle` is reset on every data segment, so this is a silence timeout, not a total-time limit. Correct.
- **#7, #8, sc10** are correct. entropy_get exists (http.c:62, declared in http.h:31). The origin field rb[24..31] is only read when k>=48. A reply with the wrong origin is ignored and the client keeps waiting until the 3 s timeout. leap==3 is rejected. ap_retryable matches by prefix and works when g_fetch_err is shorter than the prefix.

## MEDIUM 1 - the stash has one slot and accepts any DHCP reply, so our OFFER/ACK can be overwritten before dhcp_async_step reads it
- Where: net/stack/tcpip.c:187 `dhcp_async_take`.
- What goes wrong: every 67→68 frame is copied into `da_stash`, whatever its xid. dhcp_async_step runs only every 200 ms (wifi_mgr tick). Several things can arrive in that gap and overwrite our reply:
  - another device's broadcast OFFER/ACK (phones joining, a second DHCP server, a mesh node);
  - a duplicate reply for an older xid;
  - many frames pulled by one long tcp_get, tcps_rd or lxs_pump (128 frames per call) during a renew.
  When that happens the round is lost and DHCP waits for the next retry (or the next 10%-of-lease renew). The fix still works, just less reliably.
- Patch (tcpip.c:187):
```c
int dhcp_async_take(const u8* f,int n){ if(!da_st||n>1600||!da_is_reply(f,n))return 0; { const u8* ip=f+14; int ihl=(ip[0]&0x0F)*4; if(14+ihl+8+8>n||rd32(ip+ihl+8+4)!=da_xid)return 1; } for(int i=0;i<n;i++)da_stash[i]=f[i]; da_stashn=n; return 1; }
```
  A reply with another xid still counts as consumed (returns 1), so lxs and the step loop drop it as they do today, but it no longer overwrites ours. da_xid is valid in both da_st 2/3 and in renew (set in dhcp_lease_tick).

## MEDIUM 2 - DHCP fix incomplete: the sock_* receive loops still throw away DHCP replies and still run with no address
- Where: net/stack/sockets.c:23 (sock_open), :33 (sock_open SYN loop), :53 (sock_recv), :76 (sock_send_rel). These are used by net/print/ipp.c and the native socket syscalls (sys/native/syscall.c). h7 #2 lists "sock_* simply continue past them". c6 patched tcp_get and tcps_* but not these loops.
- What goes wrong: if a native-socket app or printing is running during the reconnect DHCP or the T1 renew, the OFFER/ACK is eaten here. The result is the same "Connected, but no address" / lost renew that h7 #2 describes.
- Patch:
  - sockets.c:23 `if(!nic_up())return -2;` → `if(!nic_up()||!my_ip)return -2;`
  - sockets.c:33, :53, :76 `if(ip[9]!=6)continue;` → `if(ip[9]!=6){ dhcp_async_take(rx,n); continue; }`
  - The prototype is already in tcpip.h, which sockets.c includes.

## LOW 3 - the TX-stall watchdog keeps its old starting point across a scan, so it can fire right after a scan
- Where: net/wifi/wifi_mgr.c:303. The block's statics `lf/lt/st` are not reset by scan_finish (:172). While `sc_on`, wifi_mgr_tick returns at :285, so the connected branch does not run during a scan.
- What goes wrong: a scan while connected lasts several seconds on 2.4+5 GHz, and the radio is off our channel. Suppose tx_frames does not move during the scan while the ring reports full ≥20 times. Then the first connected tick after the scan sees `g_ticks-st>=500` and calls wm_lost("transmit stuck"), even though the ring is about to drain back on our channel. That is an unnecessary disconnect plus a 5.5 s rejoin. It needs heavy TX during the scan, so it is unlikely.
- Patch: move the statics to file scope and reset them when the scan ends.
  - wifi_mgr.c, before `void wifi_mgr_tick`: `static u32 g_txw_lf=0, g_txw_lt=0; static u64 g_txw_st=0;`
  - :303 → `{ extern u32 g_wnd_txfull, g_wnd_tx_frames; if(g_wnd_tx_frames!=g_txw_lt){ g_txw_lt=g_wnd_tx_frames; g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; } else if(g_wnd_txfull-g_txw_lf>=20&&g_ticks-g_txw_st>=500){ g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; klog("wifi: transmit queue stuck - reconnecting"); wm_lost("transmit stuck"); return; } }`
  - scan_finish (:172), inside `if(g_wm_state==WM_CONNECTED){ ... }`: add `{ extern u32 g_wnd_txfull; g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; }`. The statics must be declared above scan_finish (around :158).
- Otherwise the watchdog is sound. Any successful enqueue resets it. An idle link never trips it, because txfull only counts real attempts. wnd_setup resets tx_frames to 0, which differs from the stale `lt`, so the baseline resets on every reconnect.

## LOW 4 - while DHCP is running, apt and the updater say "No network card"
- Where: tcpip.c:308. tcp_get returns -1 when my_ip==0, and unpack_and_prompts.c:142 turns -1 into "No network card". That error is not retryable. core/update.c:236/401 also treats err[0]=='N' as "call dhcp_renew()", which starts a blocking synchronous DHCP while the async one is still running. update.c already did this for -2 before c6, so this is old behaviour, but the message is now wrong more often.
- Patch: tcp_get `if(!nic_up()||!my_ip)return -1;` → `if(!nic_up())return -1; if(!my_ip)return -3;`. -3 shows "Connection timed out", which is retryable. dns_try keeps -1, because dns_query does not cache it.

## LOW 5 - the 6 s HTTP silence timeout is longer than c2's 5 s busy box, and Esc does not stop tcp_get
- Where: tcpip.c:327. c2's busy box (core/crash.c:388, still>=500) says "Press Esc ... works for network waits", but tcp_get never checks `g_busy_esc`. A dead http server now shows the busy box for about 1 s and ignores Esc.
- The cosmetic fix is to clear g_busy_esc at the start of tcp_get and break on it in the idle branch. That is a c2/c6 coordination question: g_busy_esc is reset only by browser code today, so blindly adding the break could abort later fetches. Recommend: leave it now, note it for wave 2.

## Tests claimed
c6 ran ccheck on every touched unit and `check.py --only static`. That is reasonable, and none of it runs the code. Nothing above has been tested at runtime. QEMU uses wired networking with the synchronous dhcp_config, so it cannot exercise the async Wi-Fi DHCP, the stash or the renew. Only the owner's hardware can confirm them. Things to watch: `wifi diag` GroupKeyRenewals going up with no "Connected, but no address", and a "DHCP renewed" line in the netlog after half the lease time.
