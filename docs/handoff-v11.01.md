# NoovexOS 64 — handoff v11.01

Released 2026-11-14. ISO sha256 `38568f6f03a2e756c51c14760bb36ec78d580268e5532441e1cbdcae5d5aa31e`.
Checks: build, static, analyze, drivers and qemu passed. The usb group was fixed after the main run and re-run: 12/12. Not tested on real hardware.

## Problem
Biltema B-1050W wireless mouse (Art. 24-593) moves only left/right in v10.99 and v11.00. On the owner's machine two receivers show up, 062A:4101 and 248A:16BB. Each has ep 3 boot keyboard and ep 5 mouse "report protocol (IDs)".

## 8 agents, one slice each
- **Parse:** fixed the most likely cause. When Report Count is larger than the number of usages, the repeated last usage (Y) moved Y's offset onto a trailing always-zero field, so dy was always 0. Also fixed:
  - Usage Min/Max mixed with plain Usages;
  - a Usage Page that comes after the usages;
  - Push/Pop now keeps Logical Min;
  - a depth-0 collection no longer inherits the previous kind.
  - New test `hidparse_*` (79 checks; the old code fails 12 of them).
- **Route:** boot-format 3–4 byte reports on a report-protocol endpoint are now detected (the first byte is not a declared input ID, or Y doesn't fit) and decoded as boot reports. Uses the new `idm[8]` field in hidlay_t.
- **Length:** no root cause found. Hardening: ISP on interrupt TRBs, residual masked to 24 bits, stricter TRB/buffer lookup; a ring test with 600 transfers.
- **Sim:** `hidsim_*` runs 6720 receiver combinations end to end. It found that a GET_PROTOCOL answer of "boot" switched report-protocol mice to boot decoding, which leaves a 16-bit mouse moving left/right only. The lead removed that switch; Route's detection handles real boot packets. The 280 cases still failing are the XFAIL-halt class: a report that is an exact multiple of MPS but shorter than rlen keeps the TD open, and the endpoint can babble and halt.
- **Ref:** 062A:4101 measured on real hardware: report ID 3, 12-bit X (payload bits 8–19) and 12-bit Y (bits 20–31), 7-byte reports, MPS 7. The descriptor bytes are a reconstruction (`tools/tests/usb/real_descriptors.h`).
- **Pointer:** nothing after mouse_dx/mouse_dy treats Y differently from X.
- **Kbd:** per-keyboard modifier state (`usbk_mods_sync`). EP0 is stopped and its dequeue pointer moved after a timeout, babble or transaction error. No Bluetooth HID code exists.
- **Diag:** new `usbhid` command shows the descriptor hex, the parsed X/Y offsets (with '!' when a field runs past the received length) and the last 4 raw reports. `usbhid live` shows reports as they arrive for 15 s.

## If the mouse still moves only left/right
Run `usbhid` and photograph it. Then run `usbhid live`, slide the mouse forward and back, and photograph again. Those bytes settle where Y is.

## Also seen in the owner's photos (not fixed)
- Camera 0C45:1915: "USB controller refused the stream (code 17)", an xHCI Resource Error when configuring the isochronous endpoint (bandwidth or context).

## Next
- XFAIL-halt (see Sim).
- Camera code 17.
