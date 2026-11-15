# Anchor report: Wi-Fi auto-reconnect at boot + join back-off

Branch `anchor` (worktree /home/claude/wt/anchor). Files changed:
- net/wifi/wifi_mgr.c
- tools/tests/wifi/mgr_gen.py and mgr_main.c (new host test)
- tools/check.py (g_wifi runs the new test)
rtw_chip.h is unchanged.

## Why auto-connect at boot could fail before (found by reading the code, not by running it on the laptop)
1. **The "manual" flag never cleared.** A failed auto-join that returns code 6, 7 or 10 sets `autoc=0` on the saved network. On the RTL8821CE a flaky 4-way handshake can show up as "wrong password". After that, `wifi_saved_add` (which runs when you connect by hand) never set `autoc` back to 1. So the network stayed manual-only for good: `wm_pick` skipped it on every boot, while `wifi connect` by hand still worked. This matches the owner's symptom best.
2. **`wifi off` / `wifi on` was not saved.** `wm_useroff` lived only in RAM, so it could not be respected across a reboot.
3. **The automatic power-on was not the same as `wifi on`.** The boot path called only `wifi_up()`. `wifi on` also calls `wifi_radio_on()`, which clears TXPAUSE (0x0522) and sets CR bits 0-3. The boot path now calls it too.
4. **The radio start froze the screen.** It ran all 5 firmware commands in one go from the tick (only `ui_pump` ran in between).
5. **No "last connected" network.** With equal priorities, signal strength alone picked the network.

## What changed
- The saved file header is now `auto=X 5g=Y off=Z last=<hex ssid>`. Old files load fine (no off/last means on, no default). Older kernels ignore the new keys, because their parser only matches `to=` and `5g=`.
- `wifi_user_power()` loads the file first, then writes it only when the value changes. `wifi on` also resets the failure count and retries straight away. A scan that turns the radio on now goes through `wifi_user_power(1)`.
- Fixed `wifi_saved_save()` before the file is loaded: it no longer overwrites a just-changed auto/5g/off setting with the old values from disk.
- When a join succeeds, the network becomes `last` (default, +500 in `wm_pick`, which is less than one priority step) and its fail time is cleared. If the user connected by hand, `autoc` is turned back on. The file is written only when something changed.
- Boot start (OFF branch, same gates: not switched off by the user, auto on, saved networks, `g_ticks>=1500`) now runs **one firmware step per tick** ("Starting Wi-Fi to reconnect (k of 5)"). Then it calls `wifi_up()` and `wifi_radio_on()`, starts the scan (non-blocking), and sets the status "Looking for <name> to connect automatically". The join then sets "Connecting to <name> automatically" (also written to klog). DHCP stays in the background. The status shows in Settings > Wi-Fi, the tray popup and `wifi status`.
- Airplane mode: unchanged. The tick returns before the OFF branch, `wm_connect` refuses, and leaving airplane mode turns the radio back on only if the user had not switched it off.
- **Back-off (scope-review item 3):** a failed automatic join (any code, including timeouts) now waits `wm_fail_delay()` before the next try: 30 s, 60 s, 2 min, 4 min, then 5 min. After 8 failures in a row it waits 10 min. The status shows "... - next try in N s/min" and klog gets a line too. An automatic join that succeeds no longer resets the count. The count resets only after the link has stayed up for 2 min (WM_STABLE=12000 ticks), or after a manual connect or `wifi on`. A lost link that had been up for 2 min or more reconnects after 1 s, as before. The first quick drop also reconnects after 1 s. Further quick drops use the back-off. So a weak, flapping link now causes at most one blocking join every 30 s or more, growing to one every 5-10 min.

## Tests
- `/home/claude/ccheck.sh net/wifi/wifi_mgr.c`: same 9 -Wmisleading-indentation warnings as before the change, no new ones.
- New host test, tools/tests/wifi/mgr_gen.py + mgr_main.c, built with ASan/UBSan: 15 checks, `mgr ok`. It covers loading old-format files, `wifi off` kept across a reboot, last network kept, no rewrite when nothing changed, save-before-load, last network as default, skipping a network that just failed, never auto-joining manual-only networks, and the back-off schedule.
- Existing wifi tests: scan ok, mgmt ok, chip ok, crypto 32 PASS. The 8822b/8822c tests were not run (they do not use wifi_mgr.c).

## Not tested (needs the owner's laptop) - manual steps
1. Connect once by hand (`wifi on`, then `wifi connect "<name>" <password>`). `cat /etc/noovex/wifi/networks` as root should show `off=0 last=<hex>`.
2. Reboot. Within about 15-25 s of boot, Settings > Wi-Fi should show "Starting Wi-Fi to reconnect (1..5 of 5)", then "Looking for <name>...", then "Connecting to <name> automatically", then "Connected to <name>". The pointer and screen should stay responsive between steps.
3. `wifi off`, reboot: Wi-Fi stays off. `wifi on`: it connects again (also after the next reboot).
4. Airplane mode on, reboot: no radio start. Airplane off: it connects.
5. Back-off: change the router password (or walk out of range) and watch klog/`wifi status`. You should see "automatic connection failed N time(s) in a row, next try in 30/60/120... s", and no freezes every few seconds.
6. If an earlier failure had marked the network "manual" (`wifi saved` shows `manual`), one manual connect clears that.

## Open issues
- The join itself is still synchronous (rtw_wpa2_join is up to about 3 s, EAP longer). The back-off limits how often it happens but does not remove it. Turning the join into a step-wise state machine is still the real fix.
- Each firmware step is still one blocking `run_cmd` (a short freeze per step).
- I could not confirm the real root cause on the hardware. If it still fails, the boot klog lines "wifi: starting the radio to reconnect..." and "Connecting to ... automatically" show where it stops.
