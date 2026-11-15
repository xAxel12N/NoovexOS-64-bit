# Sentinel security review: NoovexOS 64 network surface and Linux system-call boundary

Reviewer: Sentinel (read-only). Tree: /home/claude/src/NoovexOS-64-bit-main. Time box: about 35 minutes.
Method: I followed attacker-controlled bytes from the Wi-Fi RX ring and the NIC receive path through every parser they reach (802.11 management, data, CCMP, A-MSDU, EAPOL, PEAP, SAE, IP, UDP, TCP, DHCP, DNS, TLS records and handshake, HTTP and cookies). I then sampled the Linux system-call helpers (user-pointer checks, path copy, fd bounds, socket and msghdr paths). I checked each finding against the code and give the length math.

Overall: the network-facing parsers are bounds-checked well. I found **no remote memory-corruption bug** in the code I read. The top issue is a local kernel hang. The rest are protocol and design weaknesses that a malicious network could use.

## Ranked findings

### 1. MEDIUM: any local program can hang the kernel through the `sendmsg` SCM_RIGHTS control-message loop
- **Where:** `sys/linux/syscalls/files_dirs.c:193-195` (`lx_un_msg`, write path, `AF_UNIX`)
  ```c
  if(ctl&&ctllen>=16&&lx_ok(ctl,ctllen)){ u64 off=0; while(off+16<=ctllen){ u64 cl=*(u64*)(ctl+off); ...
      if(cl<16||off+cl>ctllen)break; ... int nfd=(int)((cl-16)/4); ...
      off+=(cl+7)&~7ULL; } }
  ```
- **Length math:** `cl` is a u64 that the user controls. Take `ctllen=32`. The first cmsg has `cl=16`, so `off` becomes 16. The second cmsg has `cl = 2^64-16`:
  - `off+cl` wraps to 0, which is not greater than `ctllen`, so the bounds check passes.
  - `nfd = (int)((2^64-32)/4)` = `(int)0x3FFFFFFFFFFFFFF8` = -8, so no fd is read.
  - `off += 2^64-16` wraps `off` back to 0.

  The loop never ends, and it runs inside a system call under the big kernel lock, so the whole OS freezes.
- **Attacker:** any user program (a downloaded binary, a container, a sandboxed tool) calls `sendmsg` on a Unix socket with a 32-byte control buffer.
- **Fix:** reject `cl > ctllen - off` without forming the sum, for example `if(cl<16||cl>ctllen-off)break;`. Also stop if `(cl+7)&~7` is 0 or wraps. The inet path (`lx_msg`) does not parse control messages, so it is not affected.

### 2. MEDIUM: an evil twin on first contact can capture PEAP/MSCHAPv2 credentials (trust on first use, no CA check)
- **Where:** `net/wifi/eap.c:106-110` (`g_tls_insecure=1; g_tls_pin=1;` then `tls_connect_sess`). The pin is stored in `net/wifi/wifi_mgr.c:229` only after the first successful join.
- **Issue:** the RADIUS server certificate is never checked against a CA or a server name. It is only pinned after the first connection. On the first join to an enterprise SSID (eduroam, a company network), any access point that broadcasts that SSID gets the MSCHAPv2 exchange (`eap.c:118-124`). That exchange can be cracked offline to recover the NT hash and password. The pin is checked when a saved pin exists (`eap.c:110`), and a later certificate change is correctly refused.
- **Attacker:** a malicious Wi-Fi network named like the victim's enterprise SSID, met before the owner first joins the real one (for example at an airport).
- **Fix:** offer optional CA plus domain checking for 802.1X networks (reuse `x509_verify_chain` with a user-chosen CA or the system store and a `domain_suffix_match`). At least show the fingerprint and require explicit approval before sending the password the first time.

### 3. LOW-MEDIUM: a cookie `Domain` attribute can name a public suffix (no public suffix list)
- **Where:** `net/http/cookies.c` `cookie_set`, `domain` attribute branch (about lines 225-235): `if(!domain_match(host,d))return; ... if(!dots&&!streq(d,"localhost"))return;`
- **Issue:** the only rule is that the domain must contain at least one dot. A page on `evil.co.uk` (or `x.github.io`) can set `Domain=co.uk` (or `github.io`). The cookie is then sent to every `*.co.uk` site. This allows cookie tossing and session fixation against other sites, and a cross-site tracking supercookie. `ck_site()` also treats the last two labels as the "site" (line 69), so the per-site limit is shared across all of `co.uk`.
- **Attacker:** any web server the owner visits.
- **Fix:** add a compact public suffix list (Mozilla PSL, MPL-2.0; a list of facts, so data-only use is fine) and refuse `Domain=` values that are public suffixes. Use the same list in `ck_site`.

### 4. LOW: a TCP RST is accepted without a sequence-number check (blind reset)
- **Where:** `sys/linux/lxsock.c:62` (`lxs_tcp_in`): `if(fl&TF_RST){ ... s->st=LXS_DEAD; }` with no test of `seq` against `rcv`.
- **Issue:** any segment with a matching 4-tuple kills an established connection. RFC 5961 requires an exact match on `rcv`, or a challenge ACK. An off-path attacker needs to guess the ephemeral port (`next_ephport`, 40,000 values). An on-LAN attacker sees the 4-tuple anyway.
- **Fix:** accept a RST only if `seq==s->rcv`. If it is merely inside the window, send a challenge ACK.

### 5. LOW: IHL, TCP data offset and source IP are not checked on the legacy kernel TCP/ICMP paths (stale reads, no out-of-bounds)
- **Where:** `net/stack/tcpip.c:49, 300, 312`, `net/stack/sockets.c:34, 54, 77`.
- **Issue:** `ihl=(ip[0]&0x0F)*4` is used without `ihl>=20` or `14+ihl+20<=n`. For example `ip[0]=0x4F` with `n=54` puts `t` at offset 74, past the frame. These are reads only, and they stay inside the 1600-byte stack buffer: the worst case is offset 14+60+60 = 134 bytes, plus `thl`. So they read stale bytes and are not exploitable. The copy length is clamped (`av=n-14-ihl-thl`), so there is no overflow. The source IP is not compared with `dip` either, so data from a forged source is accepted if ports and seq match. `tcps_rd` (`net/http/http.c:93`) and `lxs_frame` (`sys/linux/lxsock.c:138, 146`) do the full checks.
- **Fix:** apply the `lxs_frame` checks (`ihl>=20`, `tot` within `n`, `thl` within `plen`, `rd32(ip+12)==dip`) to `tcp_get`, `sock_connect`, `sock_recv`, `sock_send_rel`, `tcps_open` and `do_ping`.

### 6. INFO
- **No RSN IE downgrade check:** WPA2 4-way message 3 is not compared with the beacon's RSNE (`drivers/net/wifi/rtw88/rtw_sec.c:236-243`). Only cipher 4 (CCMP) and a fixed RSNE are ever used, so the practical impact is low.
- **First ARP reply wins:** `arp_resolve` (`net/stack/tcpip.c:35-36`) takes the first matching ARP reply and caches the gateway. ARP spoofing on a shared Wi-Fi is possible, as on most OSes. TLS and certificate checks remain the protection.
- **Weak DNS spoofing resistance:** only a 16-bit transaction ID and a 14-bit port (`net/stack/tcpip.c:237`) protect the kernel resolver. It accepts the first answer from the server IP. There is no 0x20 case randomisation and no DNSSEC. Acceptable, given that TLS verifies certificates.

## Areas checked and found OK (lengths verified)
- **Wi-Fi RX** (`net/wifi/wifi_netdev.c` `wnd_rx_eth`):
  - `fe` is clamped to `RTW_RXBUF` (8192).
  - Mgmt frames need `len>=24`. Beacon reads need `len>=36`, the MFP path `dl` 2..64, ADDBA `len>=33`.
  - CCMP: `sec+8<=fe`, `dlen>=8`, `pt` sized `RTW_RXBUF`, MIC read ends at `fe-1`.
  - The PN replay check is per TID plus group, and a GTK reinstall does not reset the PN (KRACK-safe).
  - Open-network and decrypted copies check `need<=max`.
  - A-MSDU: subframe bounds, `14+ln-8<=1600`, queue of 8. It drops frames whose first subframe looks like LLC/SNAP, which is the FragAttacks A-MSDU mitigation.
- **Beacon/probe scan parser** (`drivers/net/wifi/rtw88/rtw8821ce.c:334-370`, `rtw_rsn_parse`): IE walk checks `iep+2+tl<=frame_end`; RSN counts are checked before use; SSID is capped at 32.
- **`rtw_rx_wait_mgmt`** (body capped at 256, every caller's buffer is at least 256), **`rtw_rx_wait_eapol`** (capped to `maxout`), EAPOL stash (512).
- **EAPOL** (`rtw_sec.c` `wpa_rx_eapol_p`, `wpa_gtk_extract`):
  - `el>=99`; `flen` is checked against `el` and 512.
  - The MIC is verified before any key-data use.
  - The KDE walk checks `p+2+l<=n` and the GTK must be 16 bytes.
  - The AES unwrap output (512) is larger than the maximum `kdl` (413).
  - The replay counter is enforced, and a retransmitted M3 only re-sends M4 (no PTK reinstall).
- **SAE** (`net/wifi/wpa3.c`, `rtw_sae_auth`): `blen>=104`; the token is capped at 128 and the frame (256) fits in `f[320]`; scalar range is checked; the peer element is checked on-curve in `p256_load`; reflection is rejected.
- **PEAP framing** (`net/wifi/eap.c`): `e_recv_req` has `L<=n` and `dl<=1595` into 1600-byte buffers; `e_pull` stops at 20000; MSCHAPv2 parsing bounds (`pl>=22`, `k<190` in `r[200]`).
- **IPv4/UDP/TCP for programs** (`sys/linux/lxsock.c` `lxs_frame`, `lxs_tcp_in`, `lxs_udp_in`, `lxs_icmp_in`):
  - IP: `ihl>=20`, `14+tot<=n`; fragments are dropped.
  - UDP: `ul<=plen`. TCP: `thl` between 20 and `plen`; the option walk has `o+l<=thl`.
  - Receive queues check `rxn+dl<=LXS_RXCAP` and `rxn+8+dl`. ACK handling is range-checked (`adv<=out`, `dbytes<=txn`).
  - Loopback frames: `n<=LO_FRAME`.
- **Netlink requests** (`lxs_nl_send`): `L>=16`, `off+L<=n`; the attribute walk has `a+al<=L`; the name compare is bounded by the 16-byte name.
- **DHCP** (`net/stack/tcpip.c:71-114, 178-182`): `n>=282`; `dlen` is clamped to the frame; `dlen>=240`; the option walk has `o+2+l<=dlen`.
- **DNS** (`net/stack/tcpip.c:220-252`): `dns_skipname` stays under `dl`; the answer loop checks `off+10<=dl` and `off+rdl<=dl`; the query echo and txid are compared. The header reads are always inside the 1600-byte buffer, even when `dl` is negative.
- **TLS** (`lib/crypto/nvxtls.c`):
  - Records: `rl<=17000` into 32 KB buffers, and `tls_fill` cannot pass `rbuf` (18432).
  - ServerHello: the extension walk checks `o+el<=end`; key and cookie lengths are capped (65 / 512).
  - TLS 1.3 and 1.2 handshake reassembly is capped at 64 KB. Certificate list parsing is bounded. CertificateRequest context is at most 255 into 256.
  - CertificateVerify and ServerKeyExchange lengths are checked; the X25519 all-zero shared secret is rejected; Finished is compared in constant time.
  - Decryption: `tls_decrypt` needs `blen>=17`; `t12_open` needs at least 16 or 24 bytes. The KeyUpdate walk is bounded.
- **Kernel streaming TCP for TLS** (`net/http/http.c` `tcps_rd`): full IHL and THL checks, copy clamped to `max`.
- **HTTP request building** (`http_fetch_to`, `https_fetch_to`, `https_request`): every append is capped, the worst case is 942 of 960 and 1590 of 1600. Cookie header: 8 + 367 + 3 fits in 400. `json_str` is capped by `cap`.
- **Cookie parsing**: name under 256 and name plus value at most 4096; domain and path are copied with `cpyn` caps; `url_parts` is capped.
- **System-call helpers** (`sys/linux/syscalls/fifos_and_names.c`):
  - `lx_ok`/`lx_okw` → `jc_umap_chk`: range limited to user space (0x400_0000_0000 to 0x480_0000_0000) with overflow check; every page is checked present, user and writable, with lazy and COW faults handled.
  - `lx_str` checks every page crossing. `lx_path0`: `LX_PATH` bounds on every append, including the long-name hashing.
  - `lx_vf` checks `0<=fd<FDMAX`. `lx_fd_install` stays under `lx_nofile()<=FDMAX`.
  - The `g_kb_lo` kernel-buffer exception is global but only used under the BKL.
- **Socket system calls** (`files_dirs.c`): sendto/recvfrom validate `buf,len`, with `len` capped at 64 KB or `LX_IOMAX`. `lx_sa_inx` copies at most 28 bytes; `lx_sa_outs` writes `min(L,n)`. getsockopt and setsockopt cap `L` at the source size and the copy at 16 bytes.
- **msghdr handling** (`lx_msg`, inet): iovec count at most 1024, each iovec checked, total capped at 64 KB. **sendmmsg/recvmmsg** (`fds_and_system_info.c:329-331`): the vector is checked writable before the result-length stores.

## Not reviewed (time box)
- `apps/internet/webglue.c` and `web/` (HTTP/1.1 chunked and gzip parsing in the browser).
- `lib/crypto/nvxx509.c` ASN.1 parser internals.
- `drivers/net/ethernet/*` RX descriptor handling.
- `usr/musl/nvxtls.c` (BearSSL wrapper).
- `net/print/ipp.c`.
- Most of `namespaces_containers.c`, `poll_epoll.c` and `inotify.c`, which were only sampled.

The x509 parser and the browser's HTTP body decoding are the next most valuable targets.
