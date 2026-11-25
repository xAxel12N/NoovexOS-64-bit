# btb report: Broadcom USB Bluetooth patchram (branch p-btb, commit 5941c61c)

## What was done
- New `drivers/bluetooth/firmware/bcmbt_fw.c` / `bcmbt_fw.h`. The patchram flow is derived from Linux `drivers/bluetooth/btbcm.c`. I fetched the file and its SPDX line is `GPL-2.0-or-later`; the Intel 2015 copyright and the GPL-2+ header are kept. The flow:
  1. HCI Reset, then a 100 ms wait.
  2. Read Local Version, then look up the chip in the USB subversion table (BCM20702A0/A1/B0, BCM43142A0, BCM4335x, BCM4354, BCM4350C5 and others).
  3. Read USB Product (0xfc5a) to get the vid/pid, and Read Local Name (logged).
  4. Find the firmware file: `/lib/firmware/brcm/<chip>-<vid>-<pid>.hcd`, then `BCM-<vid>-<pid>.hcd`, then `<chip>.hcd`.
  5. Check the whole .hcd before sending anything. Every record must be a vendor opcode (OGF 0x3F), the record lengths must add up to the file size exactly, and there must be at least one Launch RAM.
  6. Send Download Minidriver (0xfc2e) and wait 50 ms, send each record, including Write RAM 0xfc4c and Launch RAM 0xfc4e, then wait 250 ms.
  7. HCI Reset, then Read Version and Local Name again.
- Fallbacks. The chip stays on its ROM firmware, with a clear message, when:
  - there is no file (each path tried is logged)
  - the file is unreadable or bigger than 256 KiB
  - the .hcd is invalid
  - the minidriver command is refused
  - a record fails (we stop before Launch RAM and reset the controller)

  If Launch RAM gets no Command Complete, that is tolerated. Every wait goes through hci.c `bt_cmd`, which has a 2 s timeout, and through the bounded `hr_delay_us` sleeps. `bcmbt_setup` never returns an error, so `bt_up` goes on to its normal HCI Reset.
- Hook in `hci.c` `bt_up`: 1 include and 1 `else if` line after the Realtek branch. It runs only when the HCI manufacturer is 0x000F (Broadcom) or the USB vid is 0a5c. The owner's Realtek path runs first and does not change. I hooked in hci.c, not btusb.c, because the HCI command/wait helpers live there (the same place rtlbt is hooked, as the bti brief says).
- `btusb.c` (1 include, 3 lines changed). Broadcom BCM20702 dongles report the HCI interface as vendor-specific 0xFF/0x01/0x01, not 0xE0, so the old code never claimed them. Claiming now also accepts 0xFF/1/1, but only when the vid is in the Broadcom-based list taken from Linux btusb.c: 0a5c, 0b05, 0489, 04ca, 050d, 13d3, 413c, 0930, 105b, 0bb4, 19ff. The rule is unchanged for 0bda (Realtek) and 8087 (Intel).
- `units.list`: added `drivers/bluetooth/firmware/bcmbt_fw.c`.
- `drivers/bluetooth/NOTICE`: new entry (licence, origin, and that no firmware is shipped).
- Host test: `tools/tests/bluetooth/bcm_main.c` and `bcm_run.sh`, built with ASan/UBSan, 27 checks. It covers:
  - the .hcd parser: valid, truncated, trailing bytes, empty, non-vendor opcode, random bytes
  - the subversion table, the firmware names, and that matching stays inert for Realtek/Intel/MediaTek
  - the full command sequence and delays against a simulated controller
  - the ROM fallback with no file, minidriver refused, a write error, a missing Launch RAM Command Complete, a corrupt file (nothing is sent), and a dead controller (one command, then it stops)
  - the bounded message buffer

## Tests run
- `sh tools/tests/bluetooth/bcm_run.sh`: 27/27 pass.
- `sh tools/tests/bluetooth/run.sh` (Realtek): all pass.
- `python3 tools/check.py --only bt` passes 1/1; `--only usb` passes 12/12.
- `ccheck.sh` on bcmbt_fw.c, hci.c and xhci.c (which includes btusb.c): no new warnings.

## Untested
- Not tried on real Broadcom hardware; the owner has none. There was no full build.sh or kernel link, per the rules.
- Whether `vfs_open`/`vfs_read` on `/lib/firmware/brcm/...` works from the `bt_up` context, and whether files in that directory show up on the installed system.
- Real chips may take more than 2 s to answer Launch RAM; that case is tolerated.

## Follow-ups
- Add `bcm_run.sh` to the `bt` section of `tools/check.py`. I left check.py alone to avoid merge conflicts with bti/btm.
- Merge conflicts are possible with bti/btm in `hci.c` (same `else if` spot), `units.list` (next line) and `NOTICE` (append). All are trivial.
- Optional: in `bt_status_text`, show the Broadcom chip name.
