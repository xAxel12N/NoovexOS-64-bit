# Scope review: reorganization check (e0e8ab2b) and stability sweep

Reviewer: Scope. Read-only, nothing in the source tree was changed. Every finding below was checked against the source at the line given.

## Part A: stale paths after the move in e0e8ab2b

Method: I took all 95 old paths from docs/MOVES.md and searched the whole repo for each one as a fixed string, skipping .git, third_party, ARCHITECTURE.md, docs/architecture-src and MOVES.md itself. I searched for `_parts` separately. I checked that every new path in MOVES.md exists, that every `#include "*.c"` resolves, that every line of units.list and every include in kernel64.c exists, and that source paths named in READMEs, build.sh, docs/PORTING-WORKLIST.md, tools/*.py|sh and tools/tests generators/templates exist.

Result: **no stale reference to any old path in MOVES.md.** All 95 new paths exist. units.list, kernel64.c, build.sh (web loop at build.sh:181-199), check.py (343/347/359) and tools/tests/wifi/*_gen.py all use the new paths.

Leftovers (none breaks the build):

| # | Where | What |
|---|---|---|
| A1 | tools/splitc.py:95, :122 | The generator still uses the old layout. It recognizes a part only by `'_parts' + os.sep` (line 95) and always writes new parts to `<base>_parts/NN_name.c` (line 122). Running it on a new-layout part (for example drivers/usb/xhci/controller_init.c) is not caught by the "is a part" guard, so the part gets split again into `drivers/usb/xhci/controller_init_parts/`. Each new split also brings back the `_parts/` naming that this commit removed. |
| A2 | tools/splitc.py:4, tools/check.py:77 | Both include regexes still accept `[^"]+_parts/`. This is harmless compatibility, but it is now dead (no `_parts/` directory exists) and it lets A1 output through. |
| A3 | include/nvx/apps/internet/browser.h, webglue.h; include/nvx/drivers/net/wifi/rtw8821ce.h, rtw_sec.h, rtw_efuse.h | The headers did not move with their sources. That breaks the convention "include/nvx mirrors the tree" (ARCHITECTURE.md:2959). check.py's `khdr()`/`kread()` (check.py:24-27, :94) look up `include/nvx/<rel>.h`, so they now find nothing for these 5 files and quietly drop the header. No current test depends on that, because the wifi tests pass the header path explicitly. The rtw88/chips files already used the flat header path before this commit, so this may be intended. Either way it should be decided and written down. |
| A4 | web/README.md:1-17 | The new module list was put above the old `# web` heading, so the file now has the description twice. Cosmetic only. |
| (checked, fine) | tools/tests/accounts/ac_glue.c:1 `#include "fs_glue.c"` | This looked broken but is not. It was already there before the move, and the file is copied next to ac_glue.c at test time (check.py:790). |

## Part B: stability sweep (freezes and crashes on real hardware)

Overall: the drivers are disciplined. check.py's `unbounded_hw_loops` only covers port I/O, so I ran a wider scan for register and MMIO polling loops with empty or tiny bodies. It found no unbounded register poll in drivers/ (xhci, acpi, sound, video, input, nvidia, wifi). Every one has a counter or an hr_ns deadline. Allocation results are NULL-checked at all ~55 kmalloc/dma_alloc_pages sites in drivers/ and net/wifi. Firmware and device lengths are clamped (i2c-hid rdlen ≤ 4096 and maxin ≤ 512 at i2c_hid.c:24; USB wTotalLength is limited to 256/16000, which fits the 16 KB XHID data buffer; ACPI table length is checked in acpi_tbl_ok at power.c:28). The real risks are **long blocking work done synchronously in the single-threaded main loop**, plus one buffer overflow.

### Ranked findings

**1. HIGH: cloud file upload overflows buffers and blocks every file save.** apps/internet/browser/images.c:14-38 (`nvx_file_put`), called from sys/native/syscall.c:218 (`fs_save_bin`)
- Problem: when cloud files are on (`g_cloud_fs && g_nvx_signedin`), every `fs_save_bin` does a DNS lookup, a TCP connect, a TLS handshake and an upload inline (images.c:16-36). `fs_save_bin` allows files up to FS_CAP-1 bytes (syscall.c:216; FS_CAP = 0x40000000 in ramfs.h:7).
  - The data is base64-encoded into `imgpool`, which is 8 MB (browser.c:27). Its size is never checked.
  - The JSON body is built at `jpeg_scratch + 5 MB` (images.c:24), but jpeg_scratch is a 10 MB kmalloc (images.c:3). Nothing checks this size either. If that kmalloc fails, jpeg_scratch is the **64 KB static `g_jscr_small`**, so `body = g_jscr_small + 5 MB` writes far outside the array for any file at all.
  - The request (`req`, from jpeg_scratch+0) grows to headers + body with no limit.
- When it fails: saving a file larger than about 3.7 MB (base64 more than 5 MB) overwrites heap memory beyond the 10 MB block. Saving over about 6 MB also overruns imgpool. Under memory pressure, any save corrupts the kernel .bss, and the machine crashes later. The base64 step also overwrites live browser images in imgpool. Separately, on a slow or broken network each save freezes the whole desktop: DNS goes through up to 8 servers, tcps_open waits up to 4×300 polls (http.c:73-76), and tcps_rd waits up to 2500 idle polls (http.c:88). `g_wait_pump` is NULL outside the updater, so not even the pointer moves.
- Fix: skip the upload (or chunk it) when `4*len/3 + 512` exceeds the real buffer capacity, using `jpeg_scratch_cap()` and IMGPOOL_SZ. Never use the 64 KB fallback for this. Better: queue the upload and run it as a step-wise background service (like `s_net` in services.c) instead of calling it from `fs_save_bin`. Use a dedicated buffer rather than imgpool.

**2. HIGH: blocking captive-portal HTTP check runs inside the Wi-Fi manager tick.** net/wifi/wifi_mgr.c:194
- Problem: `wifi_mgr_tick` (main.c:362, stage 18) calls `wm_dhcp_tick`, and when DHCP succeeds that calls `http_fetch_to("connectivitycheck.gstatic.com", ...)`. This runs `dns_query` (tcpip.c:262-266: 2 rounds × up to 4 servers), then `arp_resolve`, then a blocking `tcp_get`/`sock_connect` (sockets.c:31-37: 4 SYN attempts × 300 polls) and reads until idle. `g_wait_pump` is not set here, so nothing is drawn and no input is read. The comment at wifi_mgr.c:190 says the DHCP step was made async "so the screen and touchpad never wait", but this fetch undoes that.
- When it fails: you join a hotel or café network (a portal that drops port 80 until you sign in) or a router with no upstream internet. The desktop freezes for roughly 10-25 s right after "Connected". It happens again on every reconnect, because `wm_lost` leads to a rejoin and a new DHCP.
- Fix: turn the portal probe into a small state machine like `s_clock` in services.c:126-170 (non-blocking lxs socket, poll once per tick, give up after N ticks). Or hand it to the "net" background service.

**3. MEDIUM: Wi-Fi auto-reconnect and radio start block the main loop for seconds.** net/wifi/wifi_mgr.c:267-268 and :282, through wm_connect (:204-232), wifi_up (:124-140) and drivers/net/wifi/realtek/rtw88/rtw_sec.c:198
- Problem: from `wifi_mgr_tick`, automatic reconnect calls `wifi_connect_ap`, then `wm_connect`, then the synchronous `rtw_wpa2_join` or `wifi_eap_join` (association plus 4-way handshake, or full EAP/TLS). rtw_sec.c:198 alone waits up to 3000 TU, about 3.1 s. Automatic power-on calls `wifi_up`, which runs the five firmware-load commands through `run_cmd` (:130). During these waits only `ui_pump` runs (present_and_pump.c:147-151). It moves the pointer but does no window redraw, no keyboard handling and no other tick work: sound refill, USB hot-plug, ACPI events.
- When it fails: with a weak signal, "connection lost" happens again and again (`wm_lost` sets wm_next_try = +1 s and backoff = 300). Each reconnect freezes the UI for several seconds, and EAP networks take longer. A bad password ends auto-connect (codes 6/7/10), but a timeout does not.
- Fix: split the join into step functions driven by the tick, the same way scanning already works (scan_step), or at least run the full main-loop service set (snd_poll, acpi_poll, process_input) inside the pump. Until then, make the backoff after a failed join at least 30 s.

**4. MEDIUM: a codec that stops answering turns the HDA jack poll into a near-permanent freeze.** drivers/sound/hda.c:17-23 (`verb`), :143-151 (`hda_jack` and `hda_poll`), called from core/sound.c:49 and main.c:304 (stage 5)
- Problem: each verb busy-waits up to 20000 × 10 µs = 200 ms (`kapi_delay_us`) for a RIRB response. `hda_poll` runs `hda_jack` every 300 ms. That issues one `param` per path plus up to four `cmd` verbs, and nothing backs off if the codec is dead.
- When it fails: a codec that stops responding (codec power-gated by firmware, the RIRB DMA stopping after an overrun, a spurious codec address on some HDMI/DP-only controllers, or behaviour after S1/s2idle on some machines). Each jack poll then costs (npaths+3)×200 ms, around 1.4 s for 4 paths, and it repeats every 300 ms, so the desktop is essentially frozen.
- Fix: count consecutive verb timeouts. After 2 or 3, set `codec=-1` (or a dead flag) and stop jack polling, then retry rarely (for example every 30 s). Also drop the per-verb timeout to about 10 ms (spec response time is µs-level).

**5. MEDIUM: USB hot-plug enumerates synchronously and chains three attempts; a timed-out control transfer leaves its TRBs on the ring.** drivers/usb/xhci/storage_fat_and_hubs.c:114-116; drivers/usb/xhci/controller_init.c:165-175, :185-201
- Problem: on connect, `usb_hotplug_poll` (main.c:362, stage 8) runs `xhci_msd_enum`, then `ufat_mount`, then `usn_enum_root`, then `xhci_hid_enum`, all in the main loop. Each `xh_event_t` waits up to 400000 × io_wait (port 0x80), about 0.4-0.8 s. When `xh_ctrl` times out (:197, `if(!ok)return 0;`) the Setup/Data/Status TRBs stay queued with no Stop Endpoint and no Set TR Dequeue (that cleanup is only done for cc==6, a stall, at :198-199). Completions are matched only by slot/ep (xh_want_slot/xh_want_ep), not by TRB pointer.
- When it fails: a flaky cable or a device that is slow to answer causes a freeze of up to a few seconds per plug (MSD fails, then USB-net, then HID). Worse, if a completion for a timed-out transfer arrives late, it is taken as the completion of the next control transfer. The descriptor is then read from `cur_dbuf` before the DMA has finished, giving garbage descriptors or mis-enumeration.
- Fix: on timeout, issue Stop Endpoint and Set TR Dequeue as the stall path does, and check the event's TRB pointer against the Status TRB address. Over time, move enumeration into a step-wise state machine.

**6. LOW: `net_poll` / `nd_poll` can skip the timeout check forever.** drivers/storage/ahci.c:129-131, net/stack/tcpip.c:26
- Problem: in the inner `for(;;)`, `if(n>0&&arp_auto_reply(rx,n))continue;` jumps past the PIT timeout check.
- When it fails: only a steady stream of ARP requests aimed at our IP, faster than we drain them. That is unlikely on a normal LAN, but possible under an ARP flood or an L2 loop, and it would hang any blocking network call forever.
- Fix: check the PIT delta before the `continue`, or count iterations. (Side note: `net_poll` lives in a storage driver file, ahci.c, which is odd for anyone looking for it.)

**7. LOW: a stuck EC costs up to about 500 ms per ACPI poll before the trip-out.** drivers/acpi/acpi_dev.c:34-36, :237-244
- Problem: `aec_wait` spins 20000 × io_wait (about 20-40 ms), and each read does up to 4 waits. The per-poll hard deadline (500 ms, :238) and the "8 failures, 5 s off" trip bound this, but battery `_BST` every 5 s (:242) can still use the whole 500 ms.
- When it fails: a laptop whose EC is wedged gets a visible hitch about every 5 s.
- Fix: lower the hard deadline for periodic polls to about 50-100 ms, and lengthen the dead window as trips repeat.

### Checked and found fine (so nobody re-checks them)
- Polls with deadlines: drivers/video/nvidia (nvflcn.c:14, :28; gsp/firmware_unpack_and_memory.c:71-75, :81, :108, :135; gsp/display_modes.c:59, :124), drivers/video/intel/igpu.c:170, drivers/virtio/virtio_blk.c:18 and acpi_dev.c:19-30 (global lock) all have hr_ns deadlines.
- Allocation checks: wifi_mgr.c:158/178/127 (card-dead 0xFFFFFFFF detection) is good. All allocation sites in rtw88 (chip.c:70-72, rtw8821ce.c:219-220, :501 with NULL handled by callers at :428-429 and :534), xhci (usb_network.c:139-140, controller_init.c:273/281, uvc_webcam.c:246-247), the touchpad drivers and the igpu (q_tc checked at igpu.c:276) are NULL-checked.
- services.c: the clock/NTP and DNS paths are non-blocking (lxs + per-tick poll with timeouts). The `wm_status` and `g_wifi_status` writes stay within 96 bytes.

## Not done or out of scope
I did no build or test runs (forbidden by CONTEXT.md). The `lag` stages were mapped from main.c:188-191 and :295-362. I did not review the web engine, the Linux syscall layer or the apps beyond the network calls they make.
