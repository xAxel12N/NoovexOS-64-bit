# R3 review: w-c3 + w-c5 (v11.03..)
Merge: w-c3 and w-c5 merge cleanly (git merge-tree: 0 conflicts). w-c3 does not touch graphics/ui/input.c or tools/check.py.

## MEDIUM
M1 (w-c5) Win+D / Win+arrows also reach the focused app on USB keyboards (the owner's receivers are USB).
 drivers/usb/xhci/hid_keyboard_mouse.c kbd_report has no Win suppression: Win+D types 'd', Win+Left moves the
 caret/selection in Editor/Writer/Terminal. PS/2 also leaks arrows: ps2.c extended path kpush(17..20) runs before
 the `if(kdown[0x5B]) return;` check.
 Patch (kbd_report, right after `if(sc)kdown[sc]=1; ...g_kbd_last_tick...}`):
   if(mod&0x88)continue;
 Patch (ps2.c kbd_feed2, before `if(sc==0x4B) kpush(17);`):
   if(kdown[0x5B]&&(sc==0x4B||sc==0x4D||sc==0x48||sc==0x50))return;

M2 (w-c5) Ctrl+Alt+U = full xhci_init()+re-enum from a single keypress. ufat FAT cache (ufc_dirty) is not flushed
 first, so a pending FAT update on the USB stick is lost -> stick corruption (same latent risk as `usb`, now one chord away).
 Also the keyboard that pressed it is reset mid-chord: kdown[0x16] stays 1 (release lost), so a second Ctrl+Alt+U
 is ignored until U is tapped; Ctrl/Alt self-heal on next report.
 Patch in input.c Ctrl+Alt+U block and in the `usb` shell command, before xhci_mouse_autoinit():
   { extern void ufat_flush(void); ufat_flush(); }
 and after it:  kdown[0x16]=0; kdown[0x1D]=0; kdown[0x38]=0;
 (ufat_flush must be made non-static/exported if it is static.)

M3 (w-c3) files_vol_tick now rescans volumes every 3 s unconditionally while Files is open (old code only scanned on
 request/USB-gen change). Return condition is now `!req && gen same && <3s`, so after 3 s it always scans. Also a
 hotplug storm (gen changes) bypasses the 3 s limit. vol_scan can probe USB/MSD -> periodic stutter.
 Patch: restore intent "events immediate, otherwise no polling":
   if(!fx_vs_req&&g_usbdev_gen==fx_vs_gen)return 0;
   if(fx_vs_ns&&now-fx_vs_ns<500000000ULL)return 0;   /* 0.5 s debounce for storms */

## LOW
L1 (w-c5) Ctrl+Alt+U also delivers Ctrl+U to the focused app (Writer underline / terminal line-kill) - same as Ctrl+Alt+F. Accept or swallow.
L2 (w-c5) Win+Left/Right also fire on numpad 4/6 (PS/2 non-extended sets kdown[0x4B]/[0x4D]); harmless.
L3 (w-c3) hid boot fallback `id==M->id&&!fits&&len<=4&&(b[1]||b[2])` decodes b[0] (=report id) as buttons. Intended
 for boot-mode left-drag (btn==id==1); a short non-fitting real report with that id would give a phantom left click.
 Acceptable; note in handoff.
L4 (w-c3) ed_load_usb truncates at 15999 bytes without setting ed_ro (k==len). Saving now goes to a new Untitled.txt
 (ed_file<0), so no overwrite, but the user is not told it was cut. Patch: `if(n>=(int)sizeof(ubuf)-1){ editor_open_buf(ubuf,n); ed_ro=1; toast(ED_RO_MSG); return; }`
L5 (w-c3) Writer do_save: verify save_docx sets wp_slot for a new doc; if not, every second Ctrl+S on a new document
 finds its own file and reopens the rename dialog. Patch if needed: after successful save, `wp_slot=fs_find_name(wp_name);`
L6 (w-c5) Win+D restore uses focus_win on minimized windows - confirm focus_win un-minimizes (if it only raises, restore is a no-op).

## Checked OK
- report: 6 captured cmds x <=8 KB + 8 KB klog < 64 KB CAP; rb[CAP+1]; rn bounds checked everywhere; g_capture guard
  against pipes; blocking = usbinfo/crash/lag (fast) + fs save + nvx3_sync_all (one disk sync) - acceptable.
- log: klog_ring never writes past cap and adds no NUL; kb 16385 + kb[n]=0 safe; line-walk correct.
- usbdev_gone toast: nm[40] bounded, scopy into b[80] fits ("USB device removed: "+39).
- apply_snap saves sx/sy only when !max -> Win+Down/unsnap restores the pre-snap rect. Correct.
- Editor read-only: all mutators (ins/enter/bs/cut/paste via ins, save, py_save) gated; double-press discard works.
- Files USB copy no-overwrite naming bounded (nm[64], p<nm+62); fx_drop perm check; crash.c bkl_held guard;
  xhid_recpend set on cmd timeout; draw_app_marked nesting restore - all fine.
