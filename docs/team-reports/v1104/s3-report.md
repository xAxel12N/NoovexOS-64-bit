# S3 - network / Wi-Fi receive-path safety sweep (v11.04, read-only)

Scope covered: net/stack/tcpip.c, sys/linux/lxsock.c (lxs_frame/tcp_in/udp_in/icmp_in), net/http/http.c (tcps_*, cookies, json),
net/wifi/{wifi_netdev,wpa2,eap}.c, drivers/net/wifi/rtw88/{rtw8821ce.c scan/IE parse, rtw_sec.c EAPOL/4-way/GTK},
drivers/net/ethernet/* rx paths.

## Bottom line
No memory-corruption bug found that packets from the network or a nearby AP can trigger. Every receive buffer copy is clamped.
What is left is logic and spoofing weaknesses. Ranked:

### 1. MEDIUM - PEAP first connect sends the MSCHAPv2 answer to any AP (evil twin)
net/wifi/eap.c:104-110. The first time you join an enterprise network (no pin yet) the TLS tunnel runs with g_tls_insecure=1 and
no CA check. The pin is only compared when g_eap_pin_set. A nearby fake AP with the same SSID (hostapd-wpe style) gets the
NT-Response before it has to prove anything. That is enough to crack the password offline.
Patch (needs a small UI confirm; smallest safe version):
```
-    if(g_eap_pin_set){ ... return -12; } }
+    if(g_eap_pin_set){ ... return -12; } }
+    else if(!g_eap_trust_first){ g_eap_why="new network certificate - confirm its fingerprint in Wi-Fi settings, then connect again"; return -13; }
```
Add `int g_eap_trust_first=0;`, set it to 1 when the user confirms the shown fingerprint (g_eap_pin_new). In rtw_sec.c:205 map
`er==-13` to a new join code (for example 12, "confirm certificate"). If no UI time this round, at least log the fingerprint and
document the risk.

### 2. LOW-MED - legacy TCP/ICMP receive loops do not check source/dest IP; RTL8139 runs promiscuous
- net/stack/tcpip.c:300-318 (tcp_get), net/http/http.c:81-82 + 90-92 (tcps_open/tcps_rd), tcpip.c:46-48 (do_ping) only match
  ports or the ICMP id. They do not check ip src==dip, ip dst==my_ip, or ihl>=20/thl>=20 (tcp_get only).
  A LAN host can inject RST/FIN/data into plain HTTP fetches, or fake ping replies.
  For TLS (tcps_rd) this only causes a DoS.
- drivers/net/ethernet/rtl8139.c:61 `kapi_outl(io+R_RCR,0x0000E78Fu)`: bit0 = AAP (accept all physical), so every unicast frame on
  the segment enters these loops.
Patch:
```
rtl8139.c:61   0x0000E78Fu  ->  0x0000E78Eu        (keep APM|AM|AB, drop AAP)
tcpip.c tcp_get both loops, after `u8* ip=rx+14; if(ip[9]!=6)continue;` add:
    int ihl0=(ip[0]&0x0F)*4; if(ihl0<20||rd32(ip+12)!=dip||rd32(ip+16)!=my_ip)continue;
  and in the data loop after computing thl:  if(thl<20||14+ihl+thl>n)continue;
http.c tcps_open loop (line 81) and tcps_rd (line 92): add `if(rd32(ip+12)!=ts_dip)continue;` after the ip[9]!=6 test.
tcpip.c do_ping: `if(rip[9]==1&&rd32(rip+12)==tip&&(rip[0]&0x0F)>=5){`
```
Without the thl check, a frame with thl<20 gets TCP header bytes copied into the HTTP body. The reads stay inside rx[1600].

### 3. LOW - an unauthenticated M1 during the 4-way handshake makes the join fail as "wrong password"
drivers/net/wifi/rtw88/rtw_sec.c:211-242. M1 carries no MIC. If a spoofed M1 arrives before the real one, the PTK is built from the
attacker's ANonce. The real M3's ANonce then does not match (line 242), and the join returns 6, which the UI reports as a wrong
password. This is a DoS only and normal 802.11 behavior. Cheap improvement: on the line-242 mismatch, if the M3 MIC verifies against
a PTK re-derived from eap[17..48], accept it (the same re-derive the tries<3 branch already does), and only then report 6.

### 4. LOW - DNS takes the first A record without matching its owner name or class
net/stack/tcpip.c:248-250. A server, or someone on the path, can return an A record for a different name and it is cached
(dnsc_put) under the queried host. Only the configured resolver or an on-path attacker can do this, because txid (16 bits),
sport (14 bits) and src==dns are all checked. Optional: also require class==1 (`rd16(d+off-8)==1`).

### 5. INFO (by design)
- arp_resolve (tcpip.c:30-38) accepts any ARP reply for the target IP. gw_mac stays cached until the next DHCP, so LAN ARP
  spoofing works as on any OS.
- Without MFP, unprotected deauth/disassoc frames from the BSSID drop the link (wifi_netdev.c wnd_rx_mgmt). This is the 802.11
  baseline. With MFP the code checks PN and CCMP MIC correctly.
- A rogue DHCP server can set my_ip to 127.x/0/broadcast and the mask to 0 (tcpip.c:193). Optional sanity check: reject yi==0,
  yi==0xFFFFFFFF and (yi>>24)==127.

## Checked and clean (no patch needed)
- lxs_frame: ihl>=20, tot vs n, fragments dropped, thl bounds, UDP len<=plen; TCP option loop (l>=2, o+l<=thl); in-window RST
  answered with a challenge ACK; rx buffer cap checked.
- DHCP option loop: o+2+l<=dlen; dlen clamped to the frame.
- DNS: dns_skipname never follows compression pointers, so no decompression loop. Every answer read is bounded by dl, and dl is at
  most 1600-82.
- Beacon/probe IE parse (rtw8821ce.c:334-369): tag length checked against frame_end, SSID capped at 32. rtw_rsn_parse: every
  count/offset checked against l.
- rtw_rx_bad rejects bad descriptor sizes and plen beyond RTW_RXBUF. wnd_rx_eth / rtw_rx_wait_eapol: header length vs fe checked
  before every read.
- CCMP: PN replay per TID plus a group slot (17 entries); PN only advances after MIC success; pt[] size checked.
- A-MSDU: rejects the A-MSDU injection pattern (LLC-looking first subframe); ln bounds; the 8x1600 queue is bounded; EAPOL inside
  A-MSDU dropped.
- EAPOL: wpa_rx_eapol_p and wpa_gtk_extract check flen<=512, kdl<=flen-99, key-unwrap output fits kd[512], KDE loop bounded,
  MIC checked before use, replay counter checked.
- EAP/PEAP fragment reassembly: e_in 20000-byte cap; L<=n checked; inner MSCHAPv2 lengths checked.
- Ethernet rx (e1000/e1000e/igb/rtl8139/rtl8169/vmxnet3): length clamped to max; rtl8139 uses a 16 KiB buffer with WRAP for an
  8 KiB ring.
- http.c ck_parse/ck_url/json_str: bounded.
