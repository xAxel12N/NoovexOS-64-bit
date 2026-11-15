# c6 report - network bugs (h7 #1-#8 + sc10 SNTP origin check)

Worktree /home/claude/wt/c6, branch x-c6, 3 commits (4a8131cd, 6f70a8b6, af917b26) on top of v11.06.

## Changed
- h7 #1 group-key replay counter: net/wifi/wifi_netdev.c:13 `g_wnd_rx_pn_grp[4]`, :28 all cleared in wnd_setup, :32 wnd_set_gtk clears only `[keyid&3]`, :179 group frames use `[keyid]`.
- h7 #4 weak signal: wifi_netdev.c:195 any CCMP-verified (or hw-decrypted) data frame from our BSSID refreshes `g_wnd_beacon_t` (put at `*pnslot=pn` so A-MSDU frames count too). wifi_mgr.c:305 beacon-loss floor 300 -> 500 ticks.
- h7 #6 TX stall watchdog: wifi_mgr.c:304. If tx_frames does not move while txfull grows by >=20 over >=5 s, klog and `wm_lost("transmit stuck")`.
- h7 #2 DHCP answers taken by other code: net/stack/tcpip.c:185-187 adds `da_stash`, `da_is_reply`, `dhcp_async_take()`. This stashes a 67->68 UDP frame only while async DHCP runs. dhcp_async_step reads the stash first (:198). Frames that are DHCP replies are no longer passed to net_pass (:204), so they cannot loop. Callers:
  - sys/linux/lxsock.c:143 lxs_frame, before the dst-IP filter. This matters: a unicast OFFER to yiaddr would otherwise be dropped there while my_ip=0. It covers lxs_pump, net_pass from arp_resolve/dns_try, and Linux sockets.
  - tcp_get (tcpip.c:316, :328) and tcps_open/tcps_rd (net/http/http.c:79, :92) on their non-TCP `continue`.
  - Prototype added in include/nvx/net/stack/tcpip.h:33.
- h7 #2 no DNS/TCP before there is an address: `!my_ip` guard in dns_try (tcpip.c), tcp_get (tcpip.c), and tcps_open (http.c:74). dns_try returns -1, which is not cached as a negative answer.
- h7 #3 renew in RENEWING form: da_send (tcpip.c:188). When da_ren is set, it leaves out options 50/54, sets ciaddr=my_ip and clears the broadcast flag. The patch was checked against dhcp_build: flags at p+10, ciaddr at p+12.
- h7 #7 stale renew delay: dhcp_async_start also clears da_rnext and da_stashn.
- h7 #5 apt http idle timeout: tcp_get `idle>1800` -> `idle>6000` (6 s). The request is still resent at 450/900/1350 while nothing has been received.
- h7 #8 https retry: fs/pkg/apt/unpack_and_prompts.c:166. ap_retryable also retries "Connection failed". This one-line edit is outside net/**. The task asked for it explicitly.
- sc10 SNTP: apps/system/services.c:116-160. The transmit timestamp is now 8 bytes from entropy_get (it used to be the clock second). A reply is accepted only if its origin field rb[24..31] matches. A mismatched reply is ignored and the client keeps waiting until the normal 3 s timeout, so a spoofed packet cannot cancel the real answer. leap=3 (unsynchronised server) is rejected. This is a small edit outside net/**, asked for by name.

## Skipped
- The sc10 HTTPS Date-header time bound (OpenNTPD constraint idea). It is a bigger feature, and only the origin check was asked for.
- h7 notes (rtw88 periodic watchdog/DIG, roaming, chunked edge case). The report marks them as not tonight's bugs.

## Tests
- /home/claude/ccheck.sh: net/wifi/wifi_netdev.c, net/wifi/wifi_mgr.c, net/stack/tcpip.c, net/http/http.c, sys/linux/lxsock.c, apps/system/services.c, fs/pkg/apt.c (parent of the fragment). All pass; the only warnings are old misleading-indentation ones.
- `python3 tools/check.py --only static`: 13/13 ok, including "kernel compiles with zero strict warnings".
- Not run on hardware or in QEMU. Untested behaviour: group-key rekey with old-key broadcasts, a weak-signal soak test, renew with a unicast ACK.

## Fix round (branch y-c6, r6-review)
- MEDIUM 1: tcpip.c:187 dhcp_async_take now stashes only replies whose BOOTP xid == da_xid. Replies with another xid still count as consumed (return 1) but no longer overwrite ours.
- MEDIUM 2: net/stack/sockets.c:23 sock_open adds a `!my_ip` guard (-2). The receive loops at :33, :53 and :76 now pass non-TCP frames to dhcp_async_take.
- LOW 3: wifi_mgr.c: the TX-watchdog state moves to file scope (g_txw_lf/lt/st, declared above scan_finish). scan_finish resets the baseline while connected.
- LOW 4: tcp_get returns -3 ("Connection timed out", retryable) when there is no address yet, instead of -1 ("No network card"). dns_try keeps -1.
- LOW 5 (Esc in tcp_get): left alone, as the review recommends (c2/c6 coordination, wave 2).
- Tests: ccheck on tcpip.c, sockets.c and wifi_mgr.c passes. Not run in QEMU or on hardware.
