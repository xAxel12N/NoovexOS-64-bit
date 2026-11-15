# sc10 — Firmware licensing audit + network-security pieces

Scope: (1) audit every firmware blob NoovexOS ships against linux-firmware WHENCE; (2) permissive network/Wi-Fi pieces we do NOT have yet, ranked by daily value per effort. Repo read-only; sources fetched from gitlab.com/kernel-firmware/linux-firmware (main), w1.fi hostap, OpenBSD cvsweb, GitHub.

---------------------------------------------------------------------------------------------------
## Part 1 — Firmware audit

### Blobs found (find for *.bin/*.fw/*.xz under assets, drivers, third_party; build.sh lines 34-43, 296)
| Blob in repo | Upstream linux-firmware file | Byte-identical? | WHENCE licence | Verdict |
|---|---|---|---|---|
| assets/firmware/rtw8821c_fw.bin | rtw88/rtw8821c_fw.bin | YES (sha256 2ef409bc...) | "Licence: Redistributable. See LICENCE.rtlwifi_firmware.txt" (WHENCE ~l.3618, Driver: rtw88) | OK to ship, **notice missing** |
| assets/firmware/rtw8822b_fw.bin | rtw88/rtw8822b_fw.bin | YES (a72da690...) | same | OK, notice missing |
| assets/firmware/rtw8822c_fw.bin | rtw88/rtw8822c_fw.bin | YES (3deecb31...) | same | OK, notice missing |
| assets/nvidia/nvfw_gsp.xz | nvidia/tu102/gsp/gsp-535.113.01.bin | YES after xz -d (be2c3bb3...) | "Redistributable. See LICENCE.nvidia" (SPDX LicenseRef-LICENCE.nvidia) | OK, **licence copy missing** |
| assets/nvidia/nvfw_bootloader.xz | nvidia/tu102/gsp/bootloader-535.113.01.bin | YES (12e987b6...) | LICENCE.nvidia | OK, licence copy missing |
| assets/nvidia/nvfw_booter_load.xz | nvidia/tu116/gsp/booter_load-535.113.01.bin | YES (aeca340b...) | LICENCE.nvidia | OK, licence copy missing |
| assets/nvidia/nvfw_booter_unload.xz | nvidia/tu116/gsp/booter_unload-535.113.01.bin | YES (3653ff42...) | LICENCE.nvidia | OK, licence copy missing |
| assets/nvidia/nvfw_acr_bl.xz | nvidia/tu116/acr/bl.bin | YES (01326cc9...) | LICENCE.nvidia | OK, licence copy missing |

No other firmware is shipped (no rtl_nic, rtl_bt, i915/GuC, SOF, iwlwifi, amdgpu). `third_party/nvidia-classes/*.h` are MIT headers (NVIDIA open-gpu), not firmware — fine.
Nothing is non-redistributable. Every blob is unmodified (xz is lossless; decompressed bytes match upstream exactly).

### Compliance flags (fix before the next public ISO)
1. **HIGH (licence condition unmet) — LICENCE.nvidia not shipped.** §2.1.2 "Open Source Exception": redistribution allowed only on OSI-licensed OSes, "provided that the binary files thereof are not modified, and Customer provides a copy of this license with the SOFTWARE." `assets/nvidia/README.txt` only names the file. Fix: copy `LICENSES/LICENCE.nvidia` from linux-firmware into `assets/nvidia/LICENCE.nvidia` and into the ISO (e.g. `/usr/share/licenses/firmware/`), and show it in Settings > About > Licences.
2. **HIGH — Realtek notice not reproduced.** LICENCE.rtlwifi_firmware.txt: "Redistributions must reproduce the above copyright notice and the following disclaimer in the documentation and/or other materials provided with the distribution." `assets/firmware/` has no licence file and assets/README.md says nothing. Fix: add `assets/firmware/LICENCE.rtlwifi_firmware.txt` + a copy in the ISO/About.
3. **MEDIUM (GPL hygiene) — blobs are linked into the GPLv3 kernel image.** build.sh `ld -r -b binary` puts rtw*/nvfw* into `.blob` of kernel64. Data blobs are normally treated as "mere aggregation", and the owner holds the NoovexOS copyright, but the kernel also contains third-party GPL-2.0+/GPLv3 code, and Linux distros avoid built-in non-free firmware for exactly this reason. Recommended (not urgent): load firmware from separate files on the ISO/initrd (`/lib/firmware/rtw88/...`, `/lib/firmware/nvidia/...`) — the GSP loader already accepts a file override in Files, so the mechanism exists. Also cuts ~12.6 MB from the kernel.
4. LOW — NVIDIA licence also forbids reverse-engineering/translation of the blob: only relevant to the `rehw` tool work; do not disassemble GSP/booter images, only observe register traffic.
5. INFO — README.txt says a TU117-specific booter can be supplied; WHENCE has `Link: nvidia/tu117/gsp -> ../tu116/gsp`, so the tu116 booter IS the TU117 file (GTX 1650 is fine as shipped).
6. INFO — newer GSP 570.144 exists in linux-firmware (same licence). Not needed now.

### Non-firmware data blobs (noticed in passing)
- `lib/crypto/roots/mozilla-cacert.pem` (121 roots, also `/etc/ssl/certs/ca-certificates.crt`): Mozilla certdata is **MPL-2.0 — GPLv3-compatible** (MPL-2.0 §3.3 Secondary Licenses; certdata carries no "Incompatible With Secondary Licenses" notice). Our copy has **no MPL header/date** (curl's cacert.pem starts with one). Fix: keep the curl/Mozilla header ("This file is ... MPL 2.0 ... Certificate data from Mozilla as of <date>") — the PEM is its own source form, so nothing else is needed. No update mechanism and no distrust-after dates: consider a periodic refresh in the updater.
- `assets/emoji32.bin` is rendered from NotoColorEmoji.ttf (tools/mkemoji.py) — OFL-1.1, needs the OFL text shipped; none found outside ARCHITECTURE.md. LOW.
- `assets/doom1.wad` shareware: redistributable unmodified, not GPL — separate data, already documented. OK.

---------------------------------------------------------------------------------------------------
## Part 2 — What we already have (NOT proposing)
WPA2-PSK (+PSK-SHA256), WPA3-SAE (Hunting-and-Pecking only, net/wifi/wpa3.c), PMF flag for unicast deauth (wifi_netdev.c:122), WPA2-Enterprise PEAP-MSCHAPv2 with cert pinning (net/wifi/eap.c), captive-portal check via generate_204 + notification + "Sign in" tag (wifi_mgr.c:197-204, settings/pages.c:128), SNTP "Clock sync" service (apps/system/services.c), Mozilla CA store, kernel TLS 1.3 + BearSSL/mbedTLS, IP-SAN cert checks (nvxx509.c:376), ssh/scp/sshd via Dropbear 2026.94, /dev/ptmx, curl/git HTTPS. So: **ssh client, NTP, CA store and captive-portal detection are already done** — not ranked below.

## Part 3 — Candidates (only things we lack), ranked by daily value / effort

| # | Item | Source + licence proof | Size / effort | Value |
|---|---|---|---|---|
| 1 | **SNTP anti-spoof + HTTPS time constraint** | own code; design from OpenNTPD `constraint.c` — header "Copyright (c) 2015 Reyk Floeter ... Permission to use, copy, modify, and distribute this software for any purpose..." = **ISC** | ~150 lines, **1-2 h** | High (security) |
| 2 | **SAE Hash-to-Element (H2E, RFC 9380 SSWU) + RSNXE** | hostapd `src/common/sae.c` (2.5k lines) "This software may be distributed under the terms of the BSD license"; hostap README: "BSD license (the one with advertisement clause removed)" = **BSD-3** | ~300-400 lines own on our p256, **4-6 h** | High |
| 3 | **mDNS / DNS-SD** (`*.local` lookups, printer discovery for net/print/ipp.c, `ssh pc.local`) | mjansson/mdns `mdns.h` single header, 1.6k lines, LICENSE: "released into the public domain" (Unlicense) | ~1.6k + 200 glue, **4-6 h** | Medium-high |
| 4 | **OWE (Enhanced Open, AKM 18) + OWE transition mode** | hostapd `wpa_supplicant/sme.c`, `src/common/wpa_common.c` OWE parts, BSD-3 (as above) | ~400 lines own (P-256 ECDH + HKDF exist), **4-6 h** | Medium |
| 5 | **DNS-over-HTTPS** (RFC 8484, opt-in, to 1.1.1.1 / 9.9.9.9 by IP) | own (no port needed); kernel TLS + IP-SAN check already present | ~300 lines, **4-5 h** | Medium |
| 6 | EAP-TTLS (PAP / MSCHAPv2 inner) | hostapd `src/eap_peer/eap_ttls.c` BSD-3; reuse our PEAP TLS tunnel | ~300 lines, 3-4 h | Medium-low for owner (eduroam users: high) |
| 7 | IGTK/BIP (protected broadcast mgmt frames) + GTK rollover with 2 keys | hostapd `src/common/wpa_common.c` / own AES-CMAC (already in wpa3.c) | ~200 lines, 2-3 h | Low-medium |
| 8 | Captive portal: auto-open the portal page in the browser on click of the notification | own | ~30 lines, 0.5 h | Medium (tiny) |
| - | EAP-TLS (client certs, PKCS#12 import) | hostapd eap_tls.c BSD-3 | 8-12 h with cert UI | Low for owner |
| - | WPS PBC/PIN | hostapd `src/wps/` BSD-3, ~10k lines | 8-12 h | **Low — skip**: PIN mode is insecure and deprecated, PBC rarely needed |
| - | SAE-PK | hostapd `sae_pk.c` BSD-3 | 4 h after H2E | **Low — skip**: almost no deployments |

### Details
**1. SNTP hardening (do first).** `ck_ntp_send` puts only the current second in the transmit timestamp (pk[40..43]) and the reply handler never compares the origin timestamp (rb[24..31]) with it, nor checks the source port. Any off-path attacker on the LAN/hotspot can set the clock, which defeats certificate validity checks (TLS, PEAP pinning stays OK, but expiry checks don't). Fix: 8 random bytes from csprng in the transmit field, require them echoed in origin, reject leap=3 / root-dispersion huge, and (OpenNTPD idea) bound the result by the `Date:` header of one HTTPS HEAD request to a pinned host (our nwk HTTP client already exists) — reject an NTP answer more than ~±60 s away from the TLS-authenticated date. Also handles first boot with a dead RTC battery. Maps onto apps/system/services.c `s_clock` state machine.

**2. SAE H2E.** wpa3.c only does Hunting-and-Pecking (`sae_derive_pwe`, 40-round loop). Routers in WPA3-only / Wi-Fi 6E mode (and all 6 GHz) advertise `sae_pwe=1` (H2E only) and RSNXE bit 5; we then fail with an opaque join error. H2E is also the Dragonblood-resistant path (constant-time SSWU). Needs: SSWU map for P-256 (RFC 9380 §6.6.2, z=-10), PT derivation `HKDF(ssid, password)` cached per network, status code 126 (SAE_HASH_TO_ELEMENT) in the commit, RSNXE in assoc request, parse RSNXE in scan (rtw_rsn_parse). Test vectors: RFC 9380 + hostapd `tests/test-sae.c`. No firmware impact.

**3. mDNS.** No .local resolution and no printer discovery exist (grep: no 5353/mdns). mjansson/mdns is public domain, single header, no deps beyond sockets (UDP multicast 224.0.0.251 — check our stack joins multicast; IGMP join may be needed in tcpip.c, +1 h). Gives "printer found automatically" in the IPP printer dialog and `ssh name.local`.

**4. OWE.** Public "Enhanced Open" hotspots (airports, some cafés, Wi-Fi Alliance mandates for new APs offering open SSIDs in transition mode). In transition mode the open SSID points to a hidden OWE SSID; without OWE we fall back to plain open (works, unencrypted). Needs DH Parameter element (ext ID 32, group 19) in assoc req/resp, PMK = HKDF-SHA256 of ECDH secret, then the normal 4-way with AKM 18 and MFP required (we have MFP). Value: privacy on hotspots; nothing breaks without it.

**5. DoH.** Opt-in setting "Secure DNS" in Settings > Network. POST application/dns-message to https://1.1.1.1/dns-query or https://9.9.9.9/dns-query (both certs carry IP SANs; our x509 checks IP SANs). Must stay off while `g_wm_portal` is set (captive portals intercept DNS), and fall back to DHCP DNS on failure with a visible state. Reuse dns_build_query/parse in net/stack/tcpip.c. Note the cache already exists (15 s failure cache).

### REJECTED / not applicable
- None of the candidates is GPLv2-only: hostapd/wpa_supplicant BSD-3 (README proof above), OpenNTPD ISC, mjansson/mdns Unlicense/public domain, Mozilla CA MPL-2.0 (compatible).
- OpenSSH / dropbear: already shipped (Dropbear MIT, v10.07). Not proposed.
- OpenNTPD as a full daemon port: rejected as unnecessary — our SNTP service + item 1 covers it at 1/20 the size.
- WPS, SAE-PK: low value (see table).

## Recommended order
Firmware licence files (flags 1+2, 15 min, no code) -> SNTP hardening (1-2 h) -> captive-portal auto-open (0.5 h) -> SAE H2E (4-6 h) -> mDNS (4-6 h) -> OWE (4-6 h) -> DoH (4-5 h). Later: load firmware from files instead of the kernel image (flag 3).
