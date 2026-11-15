# pointer report (h-pointer) - post-decode pointer pipeline

Result: NO Y-only defect found after USB decode. Confidence the bug is NOT in this slice: ~90%.
No code change; branch h-pointer has no commit (nothing to fix).

Audited (every writer/consumer of mouse_dx/mouse_dy, mx/my):
- Producers: drivers/usb/xhci/hid_keyboard_mouse.c:64,85 (mouse_dx+=dx; mouse_dy-=dy), drivers/input/input.c:9 in_rel (same),
  drivers/input/ps2/ps2.c:110 (+= raw, PS/2 Y is already up-positive; overflow drop at :105 is X|Y together, PS/2-only),
  drivers/input/vm/vmmouse.c:52 (VMware backdoor only, cpuid hypervisor gate; symmetric). Touchpad (i2c_hid tp_poll, elan/syn PS/2)
  never touch mouse_dy directly. All += / -= accumulators; no '=' overwrite of mouse_dy by any tick handler.
- Consumers: graphics/ui/input.c:266-270 (process_input, snapshot+zero under cpu_irq_off, button-event queue replay with
  in_mq_rebase symmetric for dx/dy), mouse_step input.c:160-170, present_and_pump.c:127,135-137, setup.c, bootmenu.c, installer,
  terminal live_input, usbinfo live view. All do mx+=dx; my-=dy and clamp my to [0,H-1] exactly like mx to [0,W-1].
- Acceleration (input.c:168, present_and_pump.c:136): identical for both axes. ux_mouse_push (fs/vfs/unixfs.c:184): clamps both to +-127.
- No jump filter, big-delta rejection, axis lock, straight-line helper, Y-only cap or Y inversion exists anywhere post-decode.
  DOOM/Quake capture zero both axes together. git log -S mouse_dy: only 79641e37 / fb3e4e7d (decode-side) touched it.
- hid_bits (controller_init.c:148) handles non-byte-aligned 12-bit X/Y correctly (Unifying-style test already passes).

Conclusion: if X arrives and Y does not, Y is lost in decode/transfer (hid_parse y_off/y_sz, report length, which endpoint/report ID
is decoded), i.e. the decoder hunters' slice. The lead's hid_bits off+sz>len*8 theory stays the best candidate.
Tests: none run (no change to test). Compile-check not needed.
