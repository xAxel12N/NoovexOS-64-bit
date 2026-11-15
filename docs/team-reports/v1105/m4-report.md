# M4 report - blocking handlers reachable from process_input (v11.05)

## Fixed (branch v5-m4)
Heavy work now runs one main-loop pass later from app_io_tick (apps/media/camera.c), after a frame showing "Working..." / "Connecting...". That still happens while the BKL is held (stage 6 instead of 7). So this makes the delay visible and moves it out of process_input. It does not make the work shorter.
- graphics/ui/desktop/app_tiles_and_icons.c: wifi_gui_connect() (Settings Wi-Fi list click, tray Wi-Fi popup, Enter in password field) and the hidden-SSID Enter path (wifi_connect_ssid) no longer run inline. New wifi_gui_tick() runs them. wm_connect -> rtw_wpa2_join / wifi_eap_join / rtw_assoc are synchronous: auth + assoc + 4-way/EAP take seconds, worst case tens of seconds on timeouts, with only ui_pump nesting.
- apps/system/settings/pages.c: new st_heavy() + settings_defer_tick() for id 134 prn_test_page (IPP network print, net timeouts), 333 nvgsp on/off, 335 nvgsp_monitors (GSP-RM RPC mon_scan), 336/337 nvgsp_cmd("mode"/"mode off") (GSP RPCs, ~1-10 s), 2300+ app uninstall via apt_cmd("remove -y") (file I/O, seconds).
- Compile check: same 49 old warnings before and after, no new ones and no errors.

## Not fixed - ranked (outside my scope or needs owner design)
1. Terminal: shell/commands/prompt_and_dispatch.c:204 run_cmd runs synchronously from term_key (input.c:305). Every slow command blocks process_input while holding the BKL: nvgsp start/mode/monitors, apt install/remove, wifi connect/scan-blocking (wifi_mgr.c:194 wifi_scan_blocking loops until the scan ends, about 15 ticks per channel), usb, disk/format/clone, net fetches. **This is the most likely match for the freeze report (Terminal open).** The fix belongs in shell/ (out of scope): run the command as a jc job, or at least add a per-command deadline.
2. Settings 312 igpu_flip_on/off, 300 igpu_draw_mode (drivers/video/intel/igpu.c:86/324): register polls; check for unbounded waits on vblank/flip-done.
3. Settings 136 laptop_charge_set, 130-135 laptop_kbd_set (drivers/acpi/laptop.c), 120 bl_set (acpi_dev.c:197): these run ACPI/EC methods. An EC IBF/OBF wait without a timeout can hang forever. Worth checking the EC wait loops in drivers/acpi.
4. Settings 1460 Wi-Fi power: wifi_up() + wifi_radio_on() (net/wifi/wifi_mgr.c:129/154). Firmware download plus power-on polls, inline, can take seconds.
5. Settings 401-403 upd_install / upd_use_new / upd_go_back (core/update.c:443/514): disk copy, inline, seconds or more.
6. Settings 313 frame_check_cmd, 400-419 svc_set, 95/197 inst/clone launch: probably short. Not verified.
7. Settings System page itself: its draw (st_sys_hero) and clicks (tiles, update chip) only change page state. Nothing blocks there.
8. Main loop Ctrl+Alt+N (core/init/main.c) calls nvgsp_on inline: the same issue as 333, but outside process_input.
Note: busy_pump/desk_nested_step (present_and_pump.c) keep g_ml_beat going but never drop the BKL. Any long inline handler therefore starves the other CPUs ("CPU 6 waits for the kernel lock"). A bkl_drop/bkl_take around the nested pump would be the systemic fix (core owner).

## Untested
Not run on hardware or QEMU. Small risk: if the user changes Settings page in the single pass between the click and the tick, wifi_entry_cancel clears the password before the deferred connect runs.
