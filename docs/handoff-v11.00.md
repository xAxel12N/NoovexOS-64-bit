# NoovexOS 64 — handoff v11.00

Released 2026-11-14. ISO sha256 `6f26120381237b9273245de336636ca062cfecd92c6141e2e22e3fa1bc6da9bc`.
Checks: 80/80 passed (build, static, analyze, usb, wifi, drivers, acpi, qemu, linux) in 1945 s. Not tested on real hardware.

## Owner request
Wireless mice that only move in a straight line; check wireless keyboards; more stability; reconnect Wi-Fi automatically at boot. Team: 4 builders, 4 reviewers, 2 fixers.

## Team and results
- **Tracker** (mouse): the transfer length now covers the longest kept input report (max 64), so the Y axis is no longer cut off. A GET_PROTOCOL fallback is added for boot-protocol mice. Test `tools/tests/usb/hidmouse_*`, 26 checks.
- **Typist** (keyboard): report-ID keyboards and combo receivers, NKRO, rollover-error reports ignored, 0xE0–0xE7 counted as modifiers, separate state for each keyboard. Test `tools/tests/usb/hidkbd_*`, 38 checks.
- **Anchor** (Wi-Fi): the saved file header is `auto= 5g= off= last=`. Wi-Fi starts in steps at boot and joins the last network. `wifi off` persists. Failed joins back off 30 s → 10 min. Test `tools/tests/wifi/mgr_*`.
- **Keel** (stability): HDA dead-codec back-off; ACPI EC back-off; `acpi_hi_prep()` prepares PML4 slots 1–7 at boot; AHCI and ARP timeout fixes.
- **Reviewers** Lens2, Probe2, Sentinel2, Scope2: reports are in `docs/team-reports/`.
- **Patch** (fixer):
  - the mouse test builds again;
  - the transfer length counts only kept report IDs and stays at the max packet size unless a kept report is larger;
  - the boot mouse needs 3 received bytes (4 for the wheel) and data slots are zeroed before arming;
  - hidkbd_test.sh uses `$SANFLAGS` and cleans up after itself.
- **Weld** (fixer):
  - vm_selftest moved to PML4 slot 8 and now unmaps its test page;
  - acpi_hi_map reuses an existing entry only if it is an identity mapping of the requested address;
  - EC back-off capped at 30 s, with a full timeout on the first try after each back-off;
  - Wi-Fi: autoc=2 marks a network whose auto-join failed, and only a manual connect clears it;
  - scanning or connecting while the radio is off clears the saved "off";
  - airplane mode restores the radio's earlier state;
  - wm_bootk is reset;
  - the v10.99 history labels are restored.

## Release incidents
- A fixer branch committed its `build` symlink, which wiped the build cache and forced a cold rebuild of about 32 min. The symlink is untracked now, and `/build` and `/tools/out` are in .gitignore.

## Known limits / next
- Downgrading to v10.99 treats autoc=2 as Auto.
- Not yet fixed from the reviews: Lens2 #3 (boot keyboards fetch their report descriptor; an EP0 timeout leaves requests on the ring) and Scope2 #5 (modifier state is global across keyboards).
- If a mouse still moves only in a line: run `usbinfo` and send the report descriptor and a few raw reports.
