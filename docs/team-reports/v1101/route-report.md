# route report (v11.01 hunt, slice: which decoder each endpoint uses)

## Enumeration/indexing audit (controller_init.c ~290-330, hid_pick)
- Compaction loop: hid_parse writes straight into lays[k] (k = compacted index), and hf/mode/nlays/hasid/inl
  are copied to [k] together. reg[j] maps j->e, and every xhid_* write in the final loop uses e. Consistent; no bug.
- hid_pick pairs each interface with its first interrupt-IN endpoint and its own 0x22 length. Correct for 2-interface combos.
- SET_PROTOCOL only for subclass 1. A boot mouse whose GET_PROTOCOL says 0 drops to the old boot path (mode 0).
  On the reporter's machine usbinfo shows "report protocol (IDs)", so that endpoint uses hid_report_decode with
  hasid=1, NOT the old boot path.

## Mismatch combos checked against "X works, Y does nothing"
1. Device sends ID reports [ID,btn,X,Y] but we use the old boot path: dx=btn, dy=X. X motion moves the pointer vertically. Does NOT match.
2. Device sends boot [btn,dx,dy,wh] and we decode with IDs: id=btn, so nothing unless btn==ID. Pointer frozen. Does NOT match,
   except when a button byte equals the mouse ID (then X lands on 16-bit X = dx|dy<<8 and Y is past the end, so Y=0).
3. Device sends boot [btn,dx,dy,wh] and we decode with a no-ID 16-bit layout (btn8,X16,Y16): X = dx + dy*256, and
   Y at bit 24..39 is past the 4-byte report, so hid_bits returns 0. **This matches: X-only movement, Y always 0**
   (and vertical motion turns into large X jumps). The same happens for any ID'd mouse whose button byte equals its ID.
Confidence that this is THE Biltema cause: ~25%. It needs the receiver to ignore protocol or send boot-format packets.
I have no descriptor or raw dump from the device. `usbinfo`'s xhid_last bytes would confirm it: a 3-4 byte packet on ep 5
means this is the cause.

## Fix (hid_keyboard_mouse.c hid_report_decode, controller_init.c hid_parse, xhci.h hidlay_t)
- hid_parse records which report IDs have Input items (idm[8] bitmask, copied into each layout).
- hid_report_decode, before using the layouts, for an endpoint with a mouse layout and len>=3:
  - IDs in use and the first byte is not a declared input ID: decode as a boot mouse report [btn,dx,dy,wheel].
  - IDs in use, first byte == mouse ID, report 3-4 bytes and Y does not fit: boot decode (button byte that equals the ID).
  - No IDs, report 3-4 bytes and Y does not fit: boot decode.
  - Otherwise decode as before. Declared consumer/system/vendor IDs are never treated as a boot mouse report.

## Tests
tools/tests/usb/hidmouse_main.c: 13 new checks (boot reports on the combo ID2 16-bit, Unifying 12-bit and no-ID gaming16
layouts; consumer/system IDs untouched; normal reports still fine afterwards). 42/42 pass with ASan/UBSan. 6 of the new
checks fail on the old code. hidkbd_test.sh: 38/38 pass. ccheck drivers/usb/xhci.c: clean.
Not run: check.py g_usb usb_template (the rules forbid it). hidlay_t grew by u32 idm[8]. The template only calls hid_parse into local arrays.
