# h7 report - net/, net/wifi, rtw88 runtime, apt downloads (long uptime, router reboot, weak signal)

Read-only. Not already fixed in handoff-v11.00..06 or the v1104 B3/C2 reports. Ticks are 100 Hz. Most important first.

## 1. HIGH - after a group-key renewal, broadcast traffic can be dropped for about one rekey interval (DHCP renew fails, then the address is lost)
- Where: net/wifi/wifi_netdev.c:13 (`g_wnd_rx_pn_grp` is ONE counter), :32 (wnd_set_gtk resets it), :179 (group frames use it no matter which key id), :191/194 (`hwdec` path).
- How it fails: rtw_sec.c:94 writes the new GTK to CAM slot = key id (1 or 2). The old key id's CAM entry stays, and the chip decrypts broadcasts sent with the old key (RX_BC_USE_DK). After the router renews the group key, wnd_set_gtk sets the counter to 0. hostapd keeps sending broadcasts with the OLD key until every station has answered. Phones that are asleep make this take seconds. One old-key broadcast in that gap is decrypted by hardware (`hwdec`) and sets `g_wnd_rx_pn_grp` to the old key's packet number (thousands after an hour). When the router switches to the new key, its packet numbers start near 0, so `pn<=*pnslot` (line 181) drops EVERY broadcast and multicast frame until the new key catches up, which takes about as long as the last rekey interval. Then the next rekey repeats it.
- What the owner sees: after 10-60 min (the router's rekey interval), DHCP ACKs stop arriving, because they are broadcast: dhcp_build sets the broadcast flag while a GTK is installed. The renew at T1 fails every time, and at lease expiry dhcp_async_start drops the address, so the internet dies with "Connected, but no address". Routers that refresh ARP by broadcast cannot reach the PC either.
- Confidence 55%. The logic is clear. How often an old-key frame lands in the gap depends on how many other devices are on the network.
- Patch (one replay counter per key id, as the standard says):
  - line 13: `static u64 g_wnd_rx_pn_uni[17], g_wnd_rx_pn_grp=0;` → `static u64 g_wnd_rx_pn_uni[17], g_wnd_rx_pn_grp[4];`
  - line 28: `g_wnd_rx_pn_grp=0;` → `for(int i=0;i<4;i++)g_wnd_rx_pn_grp[i]=0;`
  - line 32: `if(!same)g_wnd_rx_pn_grp=0;` → `if(!same)g_wnd_rx_pn_grp[keyid&3]=0;`
  - line 179: `pnslot=&g_wnd_rx_pn_grp;` → `pnslot=&g_wnd_rx_pn_grp[keyid];` (keyid is already worked out at line 173)

## 2. HIGH - other network code takes the DHCP answers, so DHCP fails after a reconnect or router reboot, and renew fails while the browser or a Linux app is busy
- Where: net/stack/tcpip.c:194-200 (dhcp_async_step only runs every 200 ms from wifi_mgr_tick). Meanwhile every other RX consumer reads the same ring:
  - arp_resolve and dns_try pass foreign frames to net_pass → lxs_frame, and lxs_udp_in drops port 68 because there is no socket.
  - lxs_pump (browser pc_wait, any Linux-ABI socket app, services `nwk_net_step`) drops them the same way.
  - tcp_get, tcps_open/tcps_rd and sock_* simply `continue` past them.
- How it fails:
  - After "no signal" or a router reboot the PC rejoins, and dhcp_async_start sets my_ip=0. The browser (startup.c:274) or apt then calls dns_query. dns_try still runs with my_ip=0 (nic_up is only "associated"). It ARPs for the gateway and polls the ring for up to about 0.8 s + 1.5 s per server × 3 servers. The OFFER/ACK is eaten.
  - The negative DNS cache has one entry, so a page with 5 hosts starves DHCP for about 30 s. All 4 tries (about 16 s) fail → "Connected, but no address" → retry after 30, 60, 120 ... 480 s (g_wm_dgap).
  - During the T1 renew window (1.5 s), any lxs_pump eats the ACK. The renew then fails and the next try is 10% of the lease later.
- Confidence 70%.
- Patch:
  - tcpip.c, after line 179 (`static int da_st=0, ...`):
    `static u8 da_stash[1600]; static int da_stashn=0;`
    `void dhcp_async_stash(const u8* f,int n){ if(!da_st||n<=0||n>1600)return; for(int i=0;i<n;i++)da_stash[i]=f[i]; da_stashn=n; }`
  - tcpip.c:194: `for(int i=0;i<32;i++){ int n=nd_rx(rx,1600); if(n<=0)break;` →
    `for(int i=0;i<32;i++){ int n; if(da_stashn){ n=da_stashn; da_stashn=0; for(int j=0;j<n;j++)rx[j]=da_stash[j]; } else n=nd_rx(rx,1600); if(n<=0)break;`
  - tcpip.c:200: `net_pass(rx,n); }` → `if(!(n>=42&&rd16(rx+12)==0x0800&&rx[23]==17&&rd16(rx+14+(rx[14]&0x0F)*4+2)==68))net_pass(rx,n); }`
    This is needed: without it a stale DHCP frame would be stashed again and loop.
  - sys/linux/lxsock.c lxs_frame, UDP branch: `if(ip[9]==17){ if(plen<8)return;` → `if(ip[9]==17){ if(plen<8)return; if(rd16(p)==67&&rd16(p+2)==68){ extern void dhcp_async_stash(const u8*,int); dhcp_async_stash(f,n); return; }`
  - Do not use the network before there is an address. This also stops queries going out with source 0.0.0.0:
    - tcpip.c:243 `if(!nic_up())return -1;` → `if(!nic_up()||!my_ip)return -1;` (dns_try; -1 is not cached as a negative answer)
    - tcpip.c:305 `if(!nic_up())return -1;` → `if(!nic_up()||!my_ip)return -1;` (tcp_get)
    - net/http/http.c:74 `if(!nic_up())return 0;` → `if(!nic_up()||!my_ip)return 0;` (tcps_open)

## 3. MEDIUM - DHCP renew asks for a broadcast answer (lossy over Wi-Fi) and is not an RFC renew
- Where: tcpip.c:185 da_send, used by dhcp_lease_tick:207.
- How it fails: the renew REQUEST copies the SELECTING form (options 50+54, ciaddr=0, broadcast flag on while a GTK exists). The ACK comes back as an 802.11 broadcast: no link-layer retry, sent at the lowest rate, and exposed to #1. On a weak signal one lost frame means the next try is 10% of the lease later. Five losses mean expiry and a full restart, which drops all connections.
- Patch (RENEWING form, RFC 2131 4.3.2, so the server answers by unicast to ciaddr and the frame is retried by the AP):
  `static void da_send(u8 type){ static const u8 bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; u8 pkt[300]; int n=dhcp_build(pkt,da_xid,type,type==3?da_yi:0,type==3?da_sid:0); udp_send(...); da_t=g_ticks; }` →
  `static void da_send(u8 type){ static const u8 bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; u8 pkt[300]; int ren=da_ren&&type==3; int n=dhcp_build(pkt,da_xid,type,(type==3&&!ren)?da_yi:0,(type==3&&!ren)?da_sid:0); if(ren){ be16(pkt+10,0); be32(pkt+12,my_ip); } udp_send(0xFFFFFFFFu,68,67,pkt,n,bc); da_t=g_ticks; }`
  - dhcp_lease_tick already sets da_ren=1 before da_send(3). dhcp_frame does not check the destination IP, and lxs_frame accepts my_ip, so a unicast ACK is caught.
- Confidence 60% (that it matters).

## 4. MEDIUM - weak signal: 3 s without OUR beacons drops the whole connection, even while data from the AP is still arriving
- Where: net/wifi/wifi_mgr.c:303-304 (`lim` is at least 300 ticks, and only g_wnd_beacon_t counts). g_wnd_beacon_t is refreshed only by beacons (wifi_netdev.c:120).
- How it fails: at -80..-88 dBm, or next to a microwave or Bluetooth, a 3 s run of lost beacons is common while unicast data (which gets retries) still flows. Linux resets its connection monitor on ANY frame from the AP and probes before giving up. Here it is wm_lost → a 5.5 s full scan → rejoin → DHCP DISCOVER with my_ip=0. Downloads and websockets break, and the tray toast "Wi-Fi connection lost" keeps coming back.
- Patch: count successfully decrypted data from our AP as proof of life.
  - wifi_netdev.c:207 `outlen=need; g_wnd_rx_frames++;` → `outlen=need; g_wnd_rx_frames++; g_wnd_beacon_t=g_ticks;`
  - Optional: wifi_mgr.c:303 `if(lim<300)lim=300;` → `if(lim<500)lim=500;`
- Confidence 50%.

## 5. MEDIUM - apt over plain http (the default deb.debian.org) gives up after 1.8 s of silence; on weak Wi-Fi big .debs fail all 3 retries
- Where: tcpip.c:324 `if(++idle>1800)break;`. B3 suggested raising it, but it was not changed. Each retry restarts the whole file from 0 (deb_archive.c:177).
- How it fails: one lost segment makes the server back off its RTO (0.2 → 0.4 → 0.8 → 1.6 s). Two losses in a row exceed 1.8 s → "Connection dropped" → start over. A 50+ MB package on a weak signal may never complete in 3 tries.
- Patch: tcpip.c:324 `if(++idle>1800)break;` → `if(++idle>6000)break;`
- Confidence 70%.

## 6. LOW-MEDIUM - no transmit-stall watchdog: if the BE TX ring stops, the tray says "Connected" forever while nothing goes out
- Where: wifi_netdev.c:53-54 counts `g_wnd_txfull`, but nothing reads it except `wifi diag`. Beacons still arrive, so the "no signal" check never fires.
- Patch: in wifi_mgr.c connected branch, after `if(!wnd_is_up()){ ... return; }` add:
  `{ extern u32 g_wnd_txfull, g_wnd_tx_frames; static u32 lf=0, lt=0; static u64 st=0; if(g_wnd_tx_frames!=lt){ lt=g_wnd_tx_frames; lf=g_wnd_txfull; st=g_ticks; } else if(g_wnd_txfull-lf>=20&&g_ticks-st>=500){ lf=g_wnd_txfull; st=g_ticks; klog("wifi: transmit queue stuck - reconnecting"); wm_lost("transmit stuck"); return; } }`
  - The rejoin goes through rtw_assoc → rtw_rx_ring_up, which writes 0x039C=0xFFFFFFFF and g_beidx=0, so the ring is reset.
- Confidence 40% that the owner's chip ever stalls. The patch is cheap and safe.

## 7. LOW - stale renew back-off carries over to the next network or lease
- tcpip.c:201 sets static `da_rnext` (10% of the OLD lease) when a renew gets no answer, and nothing clears it.
  - After a rejoin with a shorter lease, T1 renew is skipped until the old deadline. The lease then expires, which forces a full restart (address 0, connections drop).
- Patch: tcpip.c:187 `void dhcp_async_start(void){ da_st=1; da_try=0; da_ren=0; g_dhcp_ok=0; }` → `void dhcp_async_start(void){ da_st=1; da_try=0; da_ren=0; da_rnext=0; g_dhcp_ok=0; }`

## 8. LOW - https downloads (vendor debs such as VS Code) have no TCP retransmit on send, and TLS failures are not retried
- net/http/http.c:87 tcps_wr sends once and never resends. A lost ClientHello or request segment on weak Wi-Fi means 2.5 s idle (tcps_rd) → TLS error.
  - ap_get maps that error to "Connection failed", which ap_retryable (unpack_and_prompts.c:166) does not retry.
- Small patch: in ap_retryable, also return 1 when g_fetch_err starts with "Connection failed".

## Notes (not bugs to patch tonight)
- rtw88 runtime: there is no periodic watchdog like Linux rtw_watch_dog_work: no DIG/IGI tracking, no thermal power tracking, no periodic IQK.
  - After the card warms up for hours, 5 GHz range and throughput can slowly get worse. This is a feature gap.
- No roaming between APs of the same SSID (mesh). It only moves to another AP after "no signal".
- PTK rekey (rtw_sec.c:90) is handled by a full disconnect and reconnect ("new keys"). That is acceptable, because most routers rekey only the GTK.
- Checked, no bug found:
  - DNS cache expiry and TTL clamp
  - cookie jar caps
  - lease T1/expiry arithmetic
  - wm_lost back-off
  - EAPOL M3 retransmit (g_m4)
  - GTK M2 format
  - RX ring index handling
  - net_idle_poll beacon refresh after long blocking work
- ap_get chunked decoder: a body cut exactly at a chunk boundary is accepted as complete (sz parses as 0 when r==n).
  - Not reachable today, because apt sends HTTP/1.0, so servers do not send chunked replies.
