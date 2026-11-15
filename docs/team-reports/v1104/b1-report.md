# B1 report: xhci HID / input / USB stick hot-unplug (v11.01-v11.03)

## F1 (HIGH, real) USB stick with non-FAT filesystem wedges USB storage until reboot
drivers/usb/xhci/storage_fat_and_hubs.c:114 (root-port hot-plug). After xhci_msd_enum() succeeds msd_ok=1, but
msd_port is only set `if(ufat_ok)`. For an exFAT/NTFS/unformatted stick (or a FAT mount that fails) msd_port stays -1.
On unplug, line 118 (`msd_port==p`) and xhub_remove_below line 31 (`msd_port==root`) never match, so msd_ok stays 1
and the dead slot is never disabled. Every later stick insert skips `if(!msd_ok)`, so no stick is ever detected again;
usb_network.c:228 also compares msd_port. Lines 49 and 107 (hub / boot paths) already set msd_port unconditionally.
How the owner notices: plug in a phone-formatted/exFAT stick, unplug it, plug in a normal FAT stick: Files never shows it
until reboot.
Patch (line 114):
-            if(!msd_ok){ if(xhci_msd_enum(p)){ msd_ctl=g_xcur; ufat_mount(); if(ufat_ok){ g_usb_arrived=1; msd_port=p; } done=1; } else { xh_disable_slot(msd_slot); msd_slot=0; } }
+            if(!msd_ok){ if(xhci_msd_enum(p)){ msd_ctl=g_xcur; msd_port=p; ufat_mount(); if(ufat_ok)g_usb_arrived=1; done=1; } else { xh_disable_slot(msd_slot); msd_slot=0; } }

## F2 (MEDIUM, real) Unplugging a receiver while a key is held leaves that key stuck in kdown[]
drivers/usb/xhci/storage_fat_and_hubs.c:28 (xhub_remove_below, used for root-port unplug via xhid_remove_port and
hub ports). It zeroes xhid_kprev/kprevx and calls usbk_mods_sync() (fixes Ctrl/Shift/Alt/Win only), but never clears
kdown[] for the non-modifier keys held in prev[2..7] (kbd_report sets kdown[hid2scan(k)]=1 at hid_keyboard_mouse.c:41,
cleared only on a later report at :48 which never comes). Same for a receiver that drops off a hub port.
How the owner notices: hold an arrow key / WASD in a game or Ctrl-free shortcut, pull the receiver (or it browns out):
the key stays "held" (cursor/player keeps moving, kdown-based shortcuts misfire) until the same key is pressed and released
on another keyboard.
Patch (line 28, inside the inner `for k` loop, before the zeroing loop):
-            ...xhid_used[k]=0; if(usbk_rep_dev==k){ usbk_rep_code=0; usbk_rep_dev=-1; } for(int z=0;z<8;z++){
+            ...xhid_used[k]=0; if(usbk_rep_dev==k){ usbk_rep_code=0; usbk_rep_dev=-1; } for(int z=2;z<8;z++){ u8 s1=hid2scan(xhid_kprev[k][z]); if(s1)kdown[s1]=0; for(int y=0;y<HIDLAY_MAX-1;y++){ u8 s2=hid2scan(xhid_kprevx[k][y][z]); if(s2)kdown[s2]=0; } } for(int z=0;z<8;z++){
(hid2scan/kdown are visible here: usbk_mods_sync from hid_keyboard_mouse.c is already called in this function. Check
hid2scan(0) returns 0; if not, guard with `if(xhid_kprev[k][z])`.) Same clearing is advisable in the error branch at
hid_keyboard_mouse.c:102 is NOT needed (endpoint recovers and next report fixes it).

## F3 (LOW, design) Auto-repeat runs forever after a lost release report without unplug
hid_keyboard_mouse.c:51-55 usb_kbd_repeat. If the 2.4 GHz link drops only the key-up report (receiver stays plugged,
transfers keep NAKing, no error cc), usbk_rep_code is never cleared: the character repeats until any key is pressed.
Keyboards do not resend while held, so a "no report for N ticks" timeout is not usable; a hard cap is the safe fix.
How the owner notices: a letter or Backspace keeps streaming into an editor after the receiver had a radio dropout.
Patch (line 54, cap at ~30 s; g_ticks assumed 100 Hz like the rest of the driver):
-    if(g_ticks-usbk_rep_t0>=(u64)g_krep_delay&&...
+    if(g_ticks-usbk_rep_t0>3000){ usbk_rep_code=0; usbk_rep_dev=-1; return; }
+    if(g_ticks-usbk_rep_t0>=(u64)g_krep_delay&&...

## Checked, no bug found (in 8 min)
- Two receivers: modifiers are now OR-ed across devices (usbk_mods_sync); repeat is per-device id; slot reuse clears
  recovery counters (controller_init.c enum + xh_bo_clear on Enable Slot).
- Mid-transfer unplug: late events for a disabled slot hit hid_xfer, find no xhid_used entry, and return; xhid_recpend on
  an unused entry is ignored by the poll loop (checks xhid_used).
- Files open during stick unplug: unplug path clears ufat_ok/ufs_n/g_files_usb; did not audit the Files app side.
Compile-check not run (read-only role).
