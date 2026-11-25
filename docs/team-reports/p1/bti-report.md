# bti report: Intel USB Bluetooth firmware loading

Branch p-bti, commit 16e5652c (worktree /home/claude/wt/bti).

## What was done
- Ported FreeBSD usr.sbin/bluetooth/iwmbtfw. I fetched the real files and checked SPDX: BSD-2-Clause, Chadd/Kondratyev/Future Crew.
  New files: drivers/bluetooth/firmware/intelbt_fw.c and intelbt_fw.h. The full BSD-2 header is kept, and NOTICE has a new entry listing the deliberate differences from FreeBSD.
  - 7260/7265 path (`ibt-hw-*.bseq`): manufacturer mode, then replay of HCI commands with an event check, then exit with warm/no reset. Includes the fallback name.
  - 8260/8265/9260/9560 legacy path (Read Version 0xfc05 + Boot Params 0xfc0d, `ibt-<var>-<rev>[-<fwrev>].sfi`).
  - TLV path for AX200/AX201/AX210/AX211 (0xfc05 0xff, intel_version_tlv parse, `ibt-%04x-%04x.sfi` from packed CNVi/CNVr top).
  - RSA and ECDSA CSS header checks and header fragments, then the command buffer in fragments of at most 252 bytes, each 4-byte aligned (secure send 0xfc09 over bulk-out). It picks up the boot address from 0xfc0e, waits for vendor event 0x06, sends Intel Reset 0xfc01, and waits up to 5 s for boot event 0x02. Then the optional .ddc (0xfc8b) and Intel event mask 0xfc52.
  - Firmware is read only from /lib/firmware/intel/<name> through the VFS, up to 4 MiB. If it is missing, the driver logs "Intel: firmware file missing: /lib/firmware/intel/ibt-XXXX-XXXX.sfi" and bt up fails cleanly. No blobs are committed.
- Hook in hci.c bt_up(): one line before the first Read Local Version, plus one #include. It runs only when intelbt_match(vid,pid) is true: VID 8087 and a PID from FreeBSD's list (07dc 0a2a 0aa7 0a2b 0aaa 0025 0026 0029 0032 0033 0035 0036). For every other adapter, including Realtek 0BDA:C123, it does nothing.
  The hook must come before Read Local Version: an Intel controller in bootloader mode would fail that check and hci.c would give up.
- btusb.c (the merge point with btm/btb):
  - a 3-line `btk` state block above btu_xfer;
  - btu_xfer now records bulk completions. It used to drop them silently, and the return value is unchanged;
  - `btk_reset()` added on the enumeration line;
  - a new appended block with `btusb_bulk_out(p,n,ms)` and `btusb_bulk_in(out,cap,ms)`, declared in bt.h. Bulk buffers sit in the unused part of the existing evbase page (+0x400 and +0x800).
  - Every wait has a timeout. A timed-out OUT transfer blocks further OUT transfers instead of queueing behind it.
  - Realtek never calls these, so its path is byte-for-byte the same as before.
- units.list: added drivers/bluetooth/intelbt_fw.c.
- Host test: tools/tests/bluetooth/intel_main.c, run from run.sh, 38 checks under ASan/UBSan. It covers:
  - parsing a synthetic AX210-style TLV reply, including unknown, truncated, bad-length and bad-status cases;
  - identifying 7260, 8260 and TLV devices;
  - all firmware-name forms;
  - the USB ID match, including that Realtek stays inert;
  - the fragment plan for RSA and ECDSA images: header offsets, contiguity, the 252-byte limit, alignment, boot address, and refusing bad headers.

## Checks run
- ccheck.sh: intelbt_fw.c and hci.c are clean, and xhci.c (which includes btusb.c) has the same 47 warnings as before.
- tools/tests/bluetooth/run.sh: ALL PASS. `check.py --only bt`: 1/1. `check.py --only usb`: 12/12.

## Untested (no Intel hardware)
- Everything that touches the device: bulk TRBs on the BT out/in endpoints, whether secure-send acks arrive on bulk-in or the interrupt pipe (both are accepted), the boot event timing, and the 7260 bseq replay.
- If a bulk endpoint stalls, it is not recovered. The load fails cleanly, and replugging or a controller reset clears it.
- The VFS read from /lib/firmware/intel has not been run in-kernel.

## Follow-ups
- btm and btb may also add bulk helpers to btusb.c. If so, keep one shared pair (btusb_bulk_out/btusb_bulk_in) when merging.
- Possibly add the newer PIDs 0037/0038 (BE200 etc.) once they are checked against a licence-safe source.
