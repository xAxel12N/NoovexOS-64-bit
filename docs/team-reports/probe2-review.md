# Probe review: Wi-Fi auto-connect at boot (d2feee70..main, agent Anchor)

Scope: `net/wifi/wifi_mgr.c`, `net/wifi/wifi_netdev.c`, `tools/tests/wifi/*`. Read-only, with callers checked in shell/apps/graphics.
Verdict: no blocker. The file format is compatible in both directions. Boot start respects airplane mode and a saved "wifi off". No path retries every tick. The owner's manual connect is not blocked. Below are 2 medium findings and 4 low ones.

## Findings (ranked)

### 1. MEDIUM: a manual connect silently undoes the user's "Manual" setting
- Where: `net/wifi/wifi_mgr.c:243` (`if(!wm_autoj&&!g_wnets[g_wm_cur].autoc){ g_wnets[g_wm_cur].autoc=1; ...wifi_saved_save(); }`)
- Issue: `autoc=0` has two meanings. It is set when an automatic join fails with code 6/7/10 (`:287`). The user also sets it on purpose with the Auto/Manual button in Settings (`apps/system/settings/pages.c:143,326`). The new code clears it on every manual connect.
- Failure: the user marks a network (for example a phone hotspot) as Manual, then connects to it by hand once. It turns back into Auto and is saved, so it is auto-joined on every later boot. This goes against the host test's own rule "a manual-only network is never auto-joined" (`tools/tests/wifi/mgr_main.c`, which only tests `wm_pick`, not this path).
- Fix: keep the two reasons apart. For example, use `autoc=2` for "turned off after a failure" at `:287` (save it as `'2'`; the loader already treats anything other than `'0'` as on, so old code reads it as Auto). At `:243`, clear only `autoc==2`. Or add a separate `autofail` bit that is not saved.

### 2. MEDIUM: a saved "wifi off" stays saved after the Scan button or a manual connect turns the radio on after boot
- Where: `net/wifi/wifi_mgr.c:165` (only `g_wm_state==WM_OFF&&g_wm_up` calls `wifi_user_power(1)`). `:219-220` (`wm_connect` calls `wifi_up()` with no `wifi_user_power`). `graphics/ui/desktop/app_tiles_and_icons.c:255` (`wifi_gui_scan(1)`) and `shell/commands/system_disks_packages_gpu.c:156` (`wifi scan`) call `wifi_up`/scan directly.
- Issue: after a boot with `off=1`, `g_wm_up==0`. So pressing Scan, running `wifi scan` or `wifi connect`, or clicking a network starts the radio through `wifi_up()`, but `wm_useroff` stays 1 in memory and in the file.
- Failure: Wi-Fi works and even auto-joins, because the IDLE branch at `:309` does not check `wm_useroff`. But (a) after the next reboot Wi-Fi is off again, although the user was using it. (b) After airplane mode is turned on and off, the radio stays off: `:293` needs `!wm_useroff`. Before this change `wm_useroff` was always 0 at boot, so (b) is new.
- Fix: in `wifi_scan_start(full)` and at the top of `wm_connect`, when `g_wm_state==WM_OFF` (whether or not `g_wm_up` is set) and not `g_airplane`, call `wifi_user_power(1)`. This is safe because the manager itself only scans or joins from WM_IDLE.

### 3. LOW: the boot step counter `wm_bootk` gets out of step with a manual `wifi_up()`
- Where: `net/wifi/wifi_mgr.c:306` (`wm_init_step(wm_bootk++,1)`), `:134` (`wifi_up` runs all 5 steps on its own). `wm_bootk` is reset only in `wifi_user_power` (`:153`) and after the 5th step.
- Issue: the user runs `wifi connect` or a GUI scan or connect (no `wifi_user_power`) while the boot sequence is part way through, say `wm_bootk=3`. `wifi_up()` then runs init and fw again from step 1, and the counter stays at 3. If that `wifi_up()` fails (`g_wm_up=0`, state OFF), the next manager pass runs only "wifi allbuilt" and "wifi rings", without init or fw. `g_rxnum` can then be set with no firmware running. Recovery depends on the later `!g_wm_fwok` retry in `wifi_up` (`:133`, up to 60 s later). Turning airplane mode on part way through also freezes `wm_bootk`, and the steps resume minutes later from the middle.
- Fix: set `wm_bootk=0` at the start of `wifi_up()`'s own init loop (`:134`) and whenever `g_airplane` is set. Or restart from step 0 if `g_ticks` since the last step is longer than about 1 s.

### 4. LOW: if the card never starts, the manager retries forever every 30 s
- Where: `net/wifi/wifi_mgr.c:307` (`wm_next_try=g_ticks+30000`, no cap).
- Issue: if init keeps failing (`!g_rxnum` after the 5 steps), the whole 5-step firmware load runs again every 5 minutes with no limit. Each step is a blocking `run_cmd` ("wifi fw" is the long one), and the status line is overwritten each time. The old code behaved the same (`wifi_up` every 30000 ticks), so this is not a regression. But the new back-off covers only joins, not radio start.
- Fix: count start failures. Use `wm_fail_delay()`, or stop after about 3 tries until `wifi on` or the Settings toggle.

### 5. LOW: auto-scans keep running when no saved network can be auto-joined
- Where: `net/wifi/wifi_mgr.c:309` (the IDLE branch checks `g_wnet_n` but not whether any network has `autoc`), `:291` (back-off of at most 6000 ticks).
- Issue: after `:287` turns auto off for the only saved network, the manager still runs a full 37-channel scan every 60 s forever, with nothing it can join. Scans do not block (`scan_step`), so the cost is only radio time and power. This is older behaviour, but it is now more likely, because the boot path reaches IDLE on its own.
- Fix: skip the scan when no saved network has `autoc` (the loop at `:310` already computes this).

### 6. LOW (docs): historical version labels were rewritten by a bulk replace
- Where: `tools/tests/wifi/chip_ref_v1098.c:1`, `tools/tests/wifi/chip_main.c:5,106`, `tools/tests/wifi/mgmt_main.c:1`, plus the comments at `wifi_mgr.c:161,194` and `wifi_netdev.c:162`.
- Issue: "code as it was in v10.99 (before the rtw_chip split)" now says v11.00. That is the current version, so the reference file's description is wrong. The `wifi_mgr`/`wifi_netdev` comments now date old features to the wrong release.
- Fix: put back the `v10.99` labels in the test reference files. Leave feature comments with the version that introduced them.

## Checked and OK
- Old file read by the new code: a header without `off=`/`last=` loads. `wm_useroff` stays 0 and `wm_last` stays empty (`:67-68`; the host test covers this).
- New file read by the old code: the old header parser matches only `...to=` (auto) and `5g=`. `off=` (`f`,`f`) and `last=` (`t`,`s`) match neither, and hex values hold no `=`. Net lines are unchanged. When the old code saves, it drops `off`/`last`, so the result is simply "on, no last network". That is acceptable.
- Password lines: the `net ...` format and its hex encoding are unchanged. The header grows by at most about 76 bytes, and the `p<b+7600` limit is absolute, so the 8192-byte buffer still holds a worst-case last line. `last=` uses the same `wm_hex`/`wm_unhex` (cap 33) as the SSID.
- Save before load (`:56`): auto, 5g and off are kept. `wm_last` is set only after a load, through `wifi_saved_find`.
- Boot gates: the file is loaded at `g_ticks>300` (`:280`), before the boot start can run at `g_ticks>=1500`, so a saved "off" is respected. Airplane mode returns at `:293`, before the OFF branch. `wm_card_dead` is checked before each step.
- Hang or loop: one step per manager pass (20-tick limit). `ui_pump` (`graphics/ui/desktop/present_and_pump.c:147`) only polls input and the cursor and never calls `wifi_mgr_tick`, so there is no re-entry into the steps or into `wm_autoj`.
- Back-off: every failed join sets `wm_next_try` to at least now + 3000. A failed scan start waits 3000. "No candidate" waits 1000-6000. A lost link waits 100 the first time, then uses the back-off. No path retries every tick.
- Manual connect: `wm_connect` clears `wm_pending` and ends a running scan (`:221`). A manual success resets `wm_fails`/`wm_backoff`. A saved "off" does not block a manual connect after boot, because `:219` checks `g_wm_up`. The terminal `wifi on`/`off` and the Settings toggle go through `wifi_user_power`.
