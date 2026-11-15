# diag report (branch h-diag, commit c85529ed)

Slice: on-screen diagnostic, not a root-cause hunt. Root cause: not investigated (confidence n/a).

## What was added
- `usbhid`: for each HID endpoint it prints dev index, VID:PID, controller, port, interface, ep address + dci, boot/report protocol (IDs), subclass/protocol, MPS, rlen (xhid_rlen), report count, number of layouts, and the last nonzero decoded dx/dy.
  Then the report descriptor in hex (16 bytes per line, offset prefix; the first 512 bytes, plus the full length), the parsed layout per field (id, kind, btn/X/Y/wheel offset+size in bits after the ID byte; keyboard mod/keys/bitmap; consumer). A field gets '!' when off+size > (last received length of a report with that id - id byte)*8, the same test hid_bits uses to return 0. After that come the last 4 raw reports as `[len] hex   dx dy`.
- `usbhid live`: shows every new report from mouse endpoints (`dN [len] hex  dx dy`) for 15 s (1500 ticks) or until a key is pressed, then prints total reports and the summed dx/dy. It drains mouse_dx/dy the same way mousetest does. If more than 4 reports arrive between polls it prints "(reports skipped)".

## Driver edits (small, only additions)
- include/nvx/drivers/usb/xhci.h: `xhdiag_t` typedef + `extern xhdiag_t xhid_diag[XHID_MAX]` (about 1 KB per entry, static).
- drivers/usb/xhci/controller_init.c: globals + `xhid_diag_rx()` (ring write, fixed arrays, no allocation). At enumeration the descriptor is copied into `xhd_rd[k]` next to hid_parse and moved into `xhid_diag[e]` at registration.
- drivers/usb/xhci/hid_keyboard_mouse.c hid_xfer: 2 lines. It saves mouse_dx/dy before decoding and calls `xhid_diag_rx(d,db,got,dx,dy)` after it, so the dx/dy shown is exactly what the decode produced (boot and report paths). hid_report_decode is unchanged.
- Shell: new shell/commands/usbhid.c (included in shell/commands.c before prompt_and_dispatch.c) and a dispatch line in network_and_apps.c next to usbinfo.

## Tests
- /home/claude/ccheck.sh drivers/usb/xhci.c and shell/commands.c: compile, with the same warning count as before (48 / 38, no new ones).
- Ran only the g_usb group (imported check.g_usb into /tmp/diagw, no build, no full check.py): test build ok, 86 receiver/routing checks ok, mouse decode 26 ok, keyboard 38 ok.
- Untested: real output layout on screen (line width). Lines are under 100 characters except raw reports longer than about 25 bytes.

## Merge notes
hid_xfer is touched in 2 lines. A fixer that edits hid_xfer may get a trivial conflict: keep `int odx=mouse_dx, ody=mouse_dy;` before the decode and the `xhid_diag_rx(...)` call after the if/else chain.
