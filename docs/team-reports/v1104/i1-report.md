# I1 - stability and self-healing ideas (read-only)

## Already exists (do NOT propose)
- Ctrl+Shift+Esc opens Task Manager (graphics/ui/input.c:248); Task Manager End task, service Stop/Start (apps/system/taskmgr.c).
- Freeze watchdog in timer IRQ, red box after 5 s, saved report, "The system recovered after N s" (core/crash.c:313); `crash`, `lag`, `usbhid` commands.
- Crash screen -> auto-restart into Recovery (RECNEXT.TXT / rec-next.flag); Recovery menu: Continue, Safe Mode, Command Prompt, Startup Repair, System Restore, Reset (core/init/bootmenu.c).
- Start-up driver "did not finish last time" -> skipped once (core/crash.c:346-363, drv_begin/drv_skipped for acpi, amdgpio, battery, cputemp, igpu, laptop, smp, touchpad, vmmouse).
- USB HID endpoint halt recovery + re-arm watchdog, per-slot EP0 back-off (v11.02/v11.03); hub port reset for mass storage only (drivers/usb/xhci/storage_fat_and_hubs.c:34).
- Wi-Fi dead-card latch, firmware restart limited to 3/boot (v10.94); browser "Page unresponsive" Wait/Stop box; Linux app crash watcher/report; NVIDIA GPU draw watchdog -> CPU fallback; update rollback ("Go back").

## Top 3 NEW ideas

### 1. Make Safe Mode actually safe (today it is cosmetic)
- For the owner: when Wi-Fi, the GPU or a background service causes trouble, Safe Mode really skips them, so he can still reach the desktop and fix things.
- Why it does not exist: `g_safemode` is read only in graphics/ui/desktop/app_tiles_and_icons.c:165 (plain wallpaper) and graphics/ui/desktop/present_and_pump.c:156 (no animations). Nothing else checks it.
- Code (~10 min): `extern int g_safemode;` then
  - net/wifi/wifi_mgr.c `wifi_mgr_tick()` (line 281): `if(g_safemode && !wm_pending) return;` (no auto-load/auto-connect; a manual scan still works).
  - `svc_tick()`: skip auto-started services when g_safemode (Task Manager Start still works).
  - NVIDIA GSP auto start (wherever the desktop starts it): skip when g_safemode, stay on the firmware framebuffer.
  - Add a small "Safe Mode" text in a desktop corner (app_tiles_and_icons.c next to line 165) so he knows.
- Note: start-up drivers run before the boot menu (core/init/main.c:262-275), so they cannot see g_safemode; a follow-up could write a `safe-next` flag like rec-next.flag and make `drv_skipped()` return 1 for igpu/amdgpio/laptop/vmmouse/cputemp when it is set.

### 2. "Restart USB input" without rebooting (hotkey + command)
- For the owner: when a wireless receiver (062A:4101 / 248A:16BB) goes dead, press Ctrl+Alt+U (or type `usb restart`) and the mouse/keyboard come back instead of a reboot.
- Why it does not exist: recovery today is automatic per endpoint (Reset Endpoint / Set TR Dequeue) and is held off 10 s after a timeout; there is no user-triggered disable-slot + port-reset + re-enumerate for HID. Port reset exists only for mass storage (xhub_port_reset, storage_fat_and_hubs.c:34).
- Code: add `xhci_hid_restart()` in drivers/usb/xhci.c: for each xhid entry, clear its back-off/recovery counters (xhid_recpend/recn/rect), `xh_disable_slot`, then mark its root/hub port as "changed" so `usb_hotplug_poll()` re-enumerates it on the next loop (do it outside any wait: check `xh_wdepth==0`). Hotkey next to Ctrl+Alt+F at graphics/ui/input.c:259 (`kdown[0x1D]&&kdown[0x38]&&kdown[0x16]`), toast "Restarting USB input". Shell: `usb restart` in shell/commands/system_disks_packages_gpu.c.

### 3. After a freeze recovers, name the culprit and offer to close it
- For the owner: if the desktop hangs for 5+ s and then comes back, a toast says "Camera was not responding (8 s). End it?" and one click closes that app, instead of him guessing or rebooting.
- Why it does not exist: the recovered-freeze path (core/crash.c:313) only appends "The system recovered after N s" to the saved report; nothing is shown on the desktop and the focused app / main-loop stage is not acted on.
- Code: at crash.c:313 set `g_frz_recovered_s=s` and record the last main-loop stage + focused window. In the main loop (core/init/main.c, after ml_mark(18)) or input.c: if `g_frz_recovered_s` and the stage was the app/draw stage, `toast("<app title> was not responding (N s) - Ctrl+Shift+Esc to end it")` and pre-select that app in Task Manager (taskmgr_tab). Never auto-kill; just tell and offer.
