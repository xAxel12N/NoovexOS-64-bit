# m1 report — v11.05 click loss (Biltema B-1050W, 248A:16BB)
Branch v5-m1, worktree /home/claude/wt/m1.
## Changes
- include/nvx/drivers/usb/xhci.h: hidlay_t gains `u8 bseen, nil, iid[8], ilen[8]` (sticky boot latch + per-declared-input-ID expected length). Kept in the layout so it is reset automatically on (re)enumeration and needs no generator/check.py changes.
- controller_init.c hid_parse: fills iid/ilen (1+ceil(bitpos/8), up to 8 input IDs) and bseen=0 in every layout.
- hid_keyboard_mouse.c hid_report_decode (no new helper functions, so check.py/hid*_gen.py extraction still works): for ID endpoints with a mouse layout:
  - clear boot: first byte not a declared input ID and len<=4 (or byte 0, any length) -> boot; latches bseen if len<=4.
  - rule 1: declared ID, len < expected, len<=4 -> boot; latches.
  - sticky: bseen and len<=4 and len != expected length of that ID -> boot.
  - non-boot packet with declared ID and len >= expected clears bseen.
  - Boot decode: btn bits 0-4, dx/dy signed, wheel byte 3 only when len>=4.
## Deviation from brief (needed to keep existing tests)
Existing test "combo ID2: short report starting with the mouse ID never reads the ID as buttons (no phantom right click)" contradicts plain rule1/sticky. Exception kept: a zero-motion short packet whose first byte is a declared ID whose mouse layout has >8-bit X/Y (wide, e.g. combo 16-bit, Lightspeed) is still treated as ambiguous/ignored. Biltema (8-bit axes) is unaffected. A 16-bit-axis receiver stuck in boot mode would still lose a motionless click on its mouse-ID button value (pre-existing behaviour).
Also: a sticky boot packet whose length equals a declared ID's exact length (e.g. 3-byte consumer on combo) is treated as report format and clears the latch.
## Tests
tools/tests/usb/hidmouse_main.c: RD_BILTEMA (brief descriptor) + 15 checks: expected lengths, 01 00 00 left, 00 00 00 up, 02 right, 04 middle, 00 FD 01 dx-3 dy+1, wheel only with 4 bytes, report 01 01 05 FB 01 -> left dx5 dy-5 wheel+1 and latch clears, consumer/system reports inert, re-enum clears latch.
`python3 tools/check.py --only usb`: 12/12 pass (mouse decode 68 checks). Compile check of drivers/usb/xhci.c: no new warnings.
## Untested
Real hardware. Scroll on the Biltema only works if it sends 4-byte boot or 5-byte report packets; the observed 3-byte packets carry no wheel byte at all (receiver limitation — may need a SET_PROTOCOL retry/ SET_IDLE investigation, out of scope).
