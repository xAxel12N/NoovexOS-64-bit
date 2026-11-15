# V4 - devil's advocate: the drag is a coincidence, the freeze is a timed task

Premise: the main loop (core/init/main.c:296-370) is the only thing that reads xHCI HID (polled, no USB IRQ),
runs process_input and draws. Anything that runs long inside one main-loop pass looks like "the whole OS froze,
keyboard too", and while the user is dragging it looks like "the window stopped following, then it was frozen".
So the drag-end report fits ANY long synchronous task that starts during the drag.
Owner's machine per earlier sessions: desktop with an NVIDIA GTX 1650 (Turing, dev 0x1F8x) - this matters below.

## Ranked timed tasks (worst case read in code)

### 1. NVIDIA nd_auto -> `nvgsp_cmd("all")` 3 s after the desktop appears - 45%
- drivers/video/nvidia/nvdraw.c:98-109, called every pass from nvd_poll (main.c:349, stage 9->12).
  nd_t_desk is set on the first desktop pass; at +300 ticks (3 s) it runs, once per boot, if nv_probe()&&nd_turing()
  (GTX 1650 = yes) and nvgsp-auto.cfg != '0' (default g_nvd_auto=1).
- Before the GPU work it does nd_flag_set (fs_save_bin + nvx3_sync_all = disk flush) and nd_usb_flag/nd_usb_set
  (msd_autoinit_safe + ufat_read/ufat_write_file "NVSTART.TXT" to the USB stick): USB bulk I/O from the main loop.
- Then the full GSP-RM boot (`nvgsp all`: firmware load, GSP boot, RPCs, channels, 2D/compute self-test, nvd_desktop
  -> gfx_start(GFX_GPU) re-allocating every window layer in VRAM) runs synchronously. Many seconds even when it works.
- During it no process_input, no xhci_mouse_poll from the main loop: keyboard AND clicks dead, pointer frozen
  (desk_nv_progress present_and_pump.c:174 only draws a progress box, and only when g_gfx==0; it reads no input).
- desk_nv_progress does `g_ml_beat++` (present_and_pump.c:175), so the freeze watchdog (core/crash.c freeze_tick
  keyed on g_ml_beat) is fed for as long as progress callbacks keep coming: a stuck/slow GSP step between callbacks
  plus the BKL-blind watchdog (s5 #1) = silent total freeze with no "not responding" screen.
- Timing fit: Start click + open app + start drag in about 3-6 s after the desktop draws is realistic; the window
  stops following mid-drag, the user lets go, "it froze". Also any app open re-composes, no reset of the 3 s timer.
- Tell-tales: if it hung for good, the next boot logs "nvgsp: the NVIDIA start-up did not finish last time - skipped"
  and NVSTART.TXT on the stick contains '1'. Ask the owner, or ask him to wait 15 s on the empty desktop before
  touching anything: if the freeze then happens with no drag at all, this is it.
- Fix (v11.03): nvdraw.c:86 `int g_nvd_auto=0` (opt-in via Gears / `nvgsp all`). Lower-risk alternative: in nd_auto
  require `g_ticks-g_last_input_t>=3000` (30 s idle) and `drag<0`, and drop nd_usb_flag/nd_usb_set from the
  automatic path.

### 2. NVIDIA idle re-check (nvgsp_tick, tick_on_off.c:7-9) - 15%
- Armed only by nv_auto_need (boot.c:121) after a fence timeout / screen-copy mismatch, i.e. only after GPU drawing is
  already on (so only after #1 succeeded). Fires when input has been idle 2 s (g_ticks-g_last_input_t>=200) = right after
  a drag ends. Runs nvgsp_all_off + nvgsp_on -> twod() + nvd_selftest + nvd_request -> nvd_desktop next pass.
  Bounded by fence limits (200/500 ms) but can be seconds. This is the only timed task that genuinely keys off "input
  stopped", so it is the best fit IF the drag is not a coincidence. Fix: boot.c:120 `g_auto_tries=1`.

### 3. Wi-Fi auto-join - 12%
- wifi_mgr.c:283 loads saved networks at g_ticks>300 (3 s after BOOT, not desktop), then scan, then
  wifi_mgr.c:289 `wifi_connect_ap(...)` synchronously in the main loop (auth/assoc/WPA2 4-way, ui_pump only).
  ui_pump moves the pointer but never runs process_input: keys and clicks dead, window stops following the drag.
  Worst case = the sum of the rtw/WPA2 timeouts (seconds), then it returns; retries back off (wm_fail_delay/wm_backoff
  up to 60 s), so it can repeat. Matches "freeze N s after boot" with the pointer still moving; a hard full freeze only
  if a PCIe MMIO read to the RTL8821CE stalls (no software timeout exists for that).
- Wi-Fi power save (s4 #2): g_wm_ps defaults to 0 (wifi_mgr.c:34) and is only set via wifi_powersave(); I found no
  config load setting it, so LPS does not turn on by itself. ~2%.
- DHCP: wm_dhcp_tick is async (dhcp_async_running) - bounded per pass. ~1%.

### 4. Disk write-back flush nvx3_sync_all (main.c:316, when g_fs_wb_due passes) - 6%
- Opening an app/settings writes config -> write-back due a few s later -> synchronous NVMe/AHCI (Intel RST) or USB
  writes in the main loop. Bounded by driver timeouts; stall, not hang, unless the controller wedges.

### 5. USB hotplug every 80 ticks (storage_fat_and_hubs.c:152-156) - 5%
- Per xHCI: reads PORTSC; only on a NEW connect does it enumerate (msd/usb-net/HID, seconds) and xhub_poll_ports (hub
  status control transfers, ~0.2-0.4 s worst case each per s3). A wireless receiver re-enumerating or the hub port
  status change can cause a multi-second stall; s3 found all waits capped.

### 6. HID watchdog every 256 polls (hid_keyboard_mouse.c:122-127) - 4%
- Reads the endpoint context state; recovers only st 2/4. Bounded command waits. Cannot freeze the OS; can leave the
  mouse dead (s3 #1) - that would be "pointer dead", not "keyboard dead too".

### 7. lx_load_sample every 500 ticks (interrupts.c:90 -> proc_extra.c:3) - <1%
- Runs inside the PIT IRQ: one bounded loop over JMAX, reads only. Cannot hang.

## Verdict
Best fit for a FULL freeze (keyboard too) on this owner's GTX 1650 desktop: #1 nd_auto `nvgsp all` at desktop+3 s
(synchronous, USB-stick + disk I/O first, watchdog fed by desk_nv_progress). The cheapest discriminating test for the
owner: boot, do not touch anything for 20 s; and send the log lines starting "nvgsp:". The fix for v11.03 is a
one-liner (g_nvd_auto=0, plus g_auto_tries=1 for #2) and is harmless to ship alongside any drag-path fix.
