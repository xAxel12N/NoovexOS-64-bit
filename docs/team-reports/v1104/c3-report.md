# C3 report (branch w-c3, commit b2583d8e)

Done (patches from docs/team-reports/v1103/l1..l4-review.md, applied as written unless noted):
- L1 M1: `u32* S=SCENE;` hoisted before loops: open_and_draw.c (both dialog dims), files_classic.c img_viewer_draw full-screen dim + draw_drive_tile border, site_permissions.c:417 border.
- L1 M2: gfx.c draw_app_marked saves/restores g_draw_win and g_draw_t0.
- L2 M1: files_vol_tick rescans on request, gen change, or every 3 s. Checked: vol_scan(0) only calls msd_autoinit_safe on the first scan and returns early when msd_ok, msd_lastlba and g_usbdev_gen are unchanged, so the 3 s poll is cheap.
- L2 M2: `int g_drag_settle` in camera.c app_io_tick (2 after drag/resize, counts down); videoplayer/browser_media_fast/xdisp_fast use it instead of their own static settle.
- L3 M1: hid_keyboard_mouse.c boot rule extended with `(id==M->id&&!fits&&len<=4&&(b[1]||b[2]))`. New test in tools/tests/usb/hidmouse_main.c: [2,5,-7,1] -> right button + motion.
- L3 M2: controller_init.c sets xhid_recpend[e]=1 on both Reset and Set-TR-Dequeue timeouts.
- L4 M1: crash.c freeze_tick: `!bkl_held()&&` before both bkl_try() calls (klog and cx_save). Trade-off: in the usual freeze (CPU 0 stuck holding the BKL) the report is no longer auto-saved to CRASH.TXT from the IRQ; it is still shown on screen.
- Editor (v1102 sweep3 M2): editor_open/editor_open_buf set ed_ro when a line >199 chars or rows past 199 would be dropped; toast "File too big for the editor ... opened read-only"; subtitle shows "Read-only (file too big to edit)"; insert/enter/backspace/cut, editor_save and py_save refuse while ed_ro. editor_reset clears it.

Tests: `python3 tools/check.py --only usb`: 12/12 passed (mouse descriptors now 51 checks). ccheck on kernel64.c, drivers/usb/xhci.c, apps/system/file_explorer.c, apps/internet/browser/browser.c: no errors; the only warnings on touched files are older misleading-indentation ones on lines I did not change (fragment files cannot be compiled on their own).
Untested on hardware. Not done: editor CRLF/control-byte handling (sweep3 M2 side note), L1 L3 cosmetic extern move.

## Round 2 (B2 findings)
- clicks_and_drops.c: the USB open copy uses a free name "name (N).ext" (B2 patch); fx_drop checks perm_dir on the target and the source.
- writer.c: wp_name_used() skips the current slot. New picks the next free DocumentN.docx. do_save refuses a name that another file uses and reopens Save as. New/Open with wp_mod need a second press (wp_warned resets on save or any non-Ctrl key).
- editor.c: Save with ed_file<0 saves a free "Untitled[ N].txt" via fs_save_bin(...,0) and toasts. New with ed_dirty needs a second press.
- ccheck: kernel64.c, file_explorer.c and writer.c show no errors, and the warning count on writer.c is the same as before. Not tested at runtime.
