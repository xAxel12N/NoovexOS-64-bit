# B3 report - daily network use (Wi-Fi reconnect, DHCP, DNS, apt, clock)

Read-only. Ticks are 100 Hz (wifi_mgr.c:274 divides by 100 to get seconds). Ranked by how likely the owner is to hit each one.

## 1. HIGH - one lost EAPOL frame turns auto-connect off for good (shows "password not accepted")
- drivers/net/wifi/realtek/rtw88/rtw_sec.c:237: if key message 3/4 does not arrive within the 1500 wait, the join returns 6 ("wrong password").
  A lost frame on a weak signal or a router that is still booting gives the same 6.
- net/wifi/wifi_mgr.c:301: on any single auto-join result of 6, 7 or 10 the code sets `autoc=2`, saves it, and notifies "Wi-Fi password not accepted".
  wm_pick (line 270) skips networks with autoc!=1, and only a manual connect clears it (handoff v11.00).
- **What the owner sees:** after a router reboot or a weak-signal drop, the home Wi-Fi never reconnects by itself. He gets a false
  "password not accepted" notice and has to reconnect by hand every time.
- **Patch** (wifi_mgr.c): only turn auto-join off after 3 password-type failures in a row. Error 10 (certificate changed) still turns it off at once.
  - line 37: add `static u8 wm_pwbad[WIFI_MAXNET];`
  - line 301: replace `if(r==6||r==7||r==10){ if(g_wnets[s].autoc==1)g_wnets[s].autoc=2; wifi_saved_save(); notify_from(...); }` with
    `if(r==6||r==7)wm_pwbad[s]++; else wm_pwbad[s]=0; if(r==10||((r==6||r==7)&&wm_pwbad[s]>=3)){ wm_pwbad[s]=0; if(g_wnets[s].autoc==1)g_wnets[s].autoc=2; wifi_saved_save(); notify_from("Wi-Fi","Wi-Fi password not accepted",g_wnets[s].ssid); }`
  - line 245: after `wm_failt[g_wm_cur]=0;` add `wm_pwbad[g_wm_cur]=0;`
  - line 83 (forget loop): add `wm_pwbad[i]=wm_pwbad[i+1];`, and after the loop `wm_pwbad[g_wnet_n]=0;`
  - line 81 (new slot): add `wm_pwbad[k]=0;`
  - The retry timing does not change. wm_fail_delay is 3 s, then 6, 12, 24, 30 s. With more than one saved network, the failed one is skipped for 180 s.

## 2. HIGH - DHCP lease is never renewed
- net/stack/tcpip.c:71-90: dhcp_parse ignores option 51 (lease time). Nothing anywhere renews (grep: no lease or T1 code).
  The DHCP retry at wifi_mgr.c:299 only runs while `!g_dhcp_ok`.
- **What the owner sees:** after hours of uptime (the lease runs out, often 1-24 h, and guest networks are shorter), the router gives the address
  to another device or stops forwarding. The internet silently dies while the tray still says "Connected". Disconnecting and reconnecting fixes it.
- **Patch:**
  - tcpip.c, before dhcp_parse: `u32 g_dhcp_lease=0; u64 g_dhcp_got=0;`
  - In the option loop: `else if(t==51&&l>=4) g_dhcp_lease=rd32(v);`
  - Line 196 (async ACK) and line 163 (sync ACK): add `g_dhcp_got=g_ticks;`
  - Add a renew mode that keeps the current address if the router does not answer:
    ```
    static int da_ren=0;
    void dhcp_async_renew(void){ if(da_st||!g_dhcp_ok||!my_ip)return; da_xid=net_rand()|1u; da_yi=my_ip; da_ren=1; da_send(3); da_st=3; }
    ```
  - In dhcp_async_step, change the timeout line (200) to:
    `if(g_ticks-da_t>=150ULL+100ULL*(u64)da_try){ if(da_ren){ da_ren=0; da_st=0; g_dhcp_got=g_ticks-(u64)g_dhcp_lease*40; return 1; } da_st=1; }`
    This keeps the old address and tries again in about 10% of the lease.
  - On ACK (line 196) set `da_ren=0;`
  - wifi_mgr.c:299 (connected branch): add
    `{ extern u32 g_dhcp_lease; extern u64 g_dhcp_got; extern void dhcp_async_renew(void); if(g_dhcp_ok&&g_dhcp_lease&&!dhcp_async_running()&&g_ticks-g_dhcp_got>(u64)g_dhcp_lease*50)dhcp_async_renew(); }`
    `*50` is half the lease in ticks. wm_dhcp_tick takes over while it runs, and frames still go to net_pass.
  - Caveat: the wired path (dhcp_config) gets the timestamp but no renew trigger. To cover it, call `dhcp_config()` from nic_up on the same condition.

## 3. HIGH - a dropped connection during a download is treated as a finished file (apt update writes a cut-off package list)
- tcpip.c:309-318: tcp_get ends on RST or after 1800 empty polls (about 1.8 s if nd_poll(...,1) waits 1 ms) and **returns the partial length as success**.
- fs/pkg/apt/unpack_and_prompts.c:150-160: ap_get never reads Content-Length. The chunked decoder `break`s silently on a short chunk (line 158).
- fs/pkg/apt/deb_archive.c:98-103: apt_update writes whatever came back over the good `srcN.pkgs` list.
  A cut-off uncompressed `Packages` file gives "Unable to locate package" for everything after the cut.
  .debs are safe because of the Size/SHA256 check (deb_archive.c:178), but a single drop aborts the whole install with "Size mismatch" and there is no retry.
- **What the owner sees:** Wi-Fi blips, or a mirror pauses for 2 s during `apt update`. Afterwards apt cannot find packages it found yesterday.
  `apt install` fails with "Size mismatch" and he has to start the whole install again.
- **Patch** (ap_get):
  - In the header loop, parse `content-length:` the same way as `location:` into `long clen=-1`.
  - Chunked decoder: replace `if(sz<=0||r+sz>n)break;` with `if(sz==0){ fin=1; break; } if(sz<0||r+sz>n)break;` (declare `int fin=0;`).
  - After decoding:
    `if((chunked&&!fin)||(!chunked&&clen>=0&&bl<clen)){ kfree(buf); g_fetch_err="Connection dropped during download"; return 0; }`
  - Retry: in plan_run2 (line 176) and apt_update (line 98), wrap the call as `for(int t=0;t<3&&!deb;t++){ if(t)AP("Retrying...\n"); deb=ap_get(...); }`.
    Also retry once on a size mismatch before giving up.
  - Optional: raise the idle limit in tcp_get from 1800 to 8000. 1.8 s is short for a real mirror over Wi-Fi.
  - For https: if https_fetch_to returns body bytes, the same Content-Length check covers it.

## 4. MED - DNS failure blocks the UI for up to about 12 s per host name, with no negative cache
- tcpip.c:262-268: dns_query tries 4 servers × 2 rounds, and each dns_try waits up to 150 ticks (1.5 s), plus ARP.
  Server 4 is 0x0A000203 = 10.0.2.3, the QEMU slirp DNS, which is useless on real networks (sent via the gateway).
  The NXDOMAIN answer (`anc==0`, line 250) returns 0, so a mistyped name is asked to all servers twice.
  A failure is never cached, so every browser image, script or websocket on the same dead host blocks again.
  Callers run on the main thread: apps/internet/browser/glue/startup.c:274, images.c:17, websock.c:253, net/http/http.c:37/116/140, sys/native/syscall.c:171.
- **What the owner sees:** while the router reboots (Wi-Fi still associated, or Ethernet), or when DNS is down, the browser and the whole desktop
  freeze for 10+ s, repeatedly, once per resource.
- **Patch** (dns_query):
  ```
  static char dneg[128]; static u64 dneg_exp;
  ...after the cache check:
  if(g_ticks<dneg_exp&&seq(dneg,host))return 0;
  u32 srv[4]={dns_ip,0x08080808u,0x01010101u,(my_ip>>8)==0x0A0002u?0x0A000203u:0};
  ...in the loop: if(r==2)goto neg;   (dns_try line 250: `if(anc==0){ netlog("DNS no answer"); return 2; }`)
  ...before the final return 0:
  neg: { int n=0; while(host[n]&&n<127){dneg[n]=host[n];n++;} dneg[n]=0; dneg_exp=g_ticks+1500; } return 0;
  ```
  - All callers test `<=0` or `==1`, so return value 2 never escapes dns_query.
  - Worst case drops from about 12 s to 9 s on a real network (3 servers × 2 × 1.5 s), and repeats are free for 15 s.
  - Also cut round 2 when round 1 got no reply at all: set a flag in dns_try when `seen`, skip round 2 if it never got set. Worst case is then 4.5 s.

## 5. LOW - clock sync retries forever every about 13 s on networks that block UDP 123
- apps/system/services.c:154 and 145: every failure sets `ck_next=g_ticks+1000` or `+3000` and moves to the next server, with no back-off.
  On school or guest networks this sends a DNS query and opens a socket every 13 s for the whole session, and the status flips between retry messages.
- **Patch:** `static int ck_fails;`. On each failure path: `ck_fails++; ck_next=g_ticks+(ck_fails>=3?60000:1000);`. On success (line 170): `ck_fails=0;`.

## Checked, no bug found
- Router reboot or signal loss: wm_lost "no signal" (3-15 s with no beacons) leads to IDLE, a scan, then wm_pick. With no AP, the back-off grows 300 → 6000 ticks (60 s cap). Recovery is fine once #1 is fixed.
- wm_card_dead stops for good when the card is gone (intended).
- Boot auto-reconnect (lines 304-308): one init step per tick, a 30 s retry when the card does not start, and wm_bootk is reset. OK.
- Debs are protected by Size/SHA256. NTP DNS parsing offsets are correct.
- Improvement idea (not a bug): after "no signal", the rejoin first runs a full 37-channel scan (about 5.5 s), and DHCP sends DISCOVER with my_ip=0, which kills open TCP connections.
  Requesting the old address first (INIT-REBOOT, as in #2 renew) would keep downloads alive across a short drop.
