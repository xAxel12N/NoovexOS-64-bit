# kbd report (hunter Kbd, branch h-kbd, commit fb74cb46)

## Slice result
No keyboard bug that could explain the Biltema Y-loss. I checked the keyboard paths and found none of the cross-talk the brief worried about: no shared buffers, no wrong xhid_kprev indexing, and no keyboard report being decoded as a mouse report or the other way round.
- Each interface gets its own region e, with its own ring, its own 16x64 data slots and its own prev buffers.
- hid_xfer finds the device by (controller, slot, dci).
- The boot keyboard path is picked by xhid_iskbd. In report mode, decoding goes by report ID, and kind 3 layouts `continue`, so a consumer collection and a mouse collection can share one ID.

Bluetooth HID: there is no BT HID path. The only Bluetooth code is the class-name string in drivers/usb/xhci/storage_fat_and_hubs.c:92 ("Bluetooth adapter"). There is no hidp or l2cap code, so there is no second mouse decoder that could lose Y the same way.

## Fixes (committed)
1. Lens2 #5 (modifier state shared across keyboards and layouts). Before the fix, kbd_report (hid_keyboard_mouse.c:32-35) wrote the global kdown Ctrl/Shift/Alt/Win straight from each report. A report from layout 2, or from another keyboard, therefore released a modifier that layout 1 was holding.
   - Now each report stores its modifier byte in its own per-(device, layout) prev[0], which is xhid_kprev or xhid_kprevx. The new usbk_mods_sync() sets kdown to the OR of all connected keyboards.
   - xhub_remove_below (storage_fat_and_hubs.c:28) now also clears xhid_kprevx and calls usbk_mods_sync(), so unplugging a keyboard while a modifier is held no longer leaves it stuck.
   - An ErrorRollOver report still updates the modifiers.
2. Lens2 #3 (EP0 timeout). In xh_ctrl (controller_init.c, after xh_event_t):
   - On a timeout it now sends Stop Endpoint (cmd 15) and Set TR Dequeue to the current enqueue pointer, so the stale TD cannot complete the next SET_PROTOCOL or SET_IDLE.
   - cc 3 (babble) and cc 4 (transaction error) now get the same Reset Endpoint + Set TR Dequeue recovery as cc 6 (stall). All three halt EP0.
   - This also protects mice, which already did the descriptor fetch.
3. Scope2 #5 (array-style modifier usages 0xE0-0xE7): checked, no bug. hid2scan and usb_key_code return 0 for those usages, so the key-down path does nothing for them.

## Checked and rejected
Scope2 #2 / rlen > MPS. Linux usbhid sizes the interrupt URB to the maximum input report length (hid-core.c:1110-1164, `insize`), not to the MPS. Our max(mps, inl) behaves the same way or is safer, so I left it unchanged. xhid_arm sets IOC, so a short packet still produces an event.

## Tests
- tools/tests/usb/hidkbd_test.sh with ASan and UBSan: ALL PASS (44 checks). 6 of them are new: modifier isolation across layouts, across devices, on unplug, and on a rollover report.
- The hidmouse test still passes (26 checks).
- ccheck of drivers/usb/xhci.c: 48 warnings before and after, so no new ones.
- hidkbd_gen.py now also extracts usbk_mods_sync and defines xhid_n and xhid_used.

## Untested
The EP0 timeout and halt recovery has only been compile-checked. Testing it needs a device that times out or babbles on EP0.

Confidence: 90% that the fixes are correct; about 5% that this slice had anything to do with the mouse Y-loss.
