# c3 report: desktop/shell fixes (branch x-c3, worktree /home/claude/wt/c3)

Commits: 764f985f (h6 desktop fixes), 221febb8 (cs3 buffers + cs5 shell/apt/sysroot). 14 files, +59/-36.
Line numbers below are in the new tree.

## h6 (all 8 applied, checked against the code first)
1. USB Alt+Tab: graphics/ui/input.c:303 (X overlay pass-through), :306, :336 now use `kalt||kdown[0x38]`. USB only sets kdown[0x38] (hid_keyboard_mouse.c:34), confirmed.
2. Restore after title-bar click: input.c:159 saves sx/sy/sw/sh only when `!WN[i].max`.
3. Unsaved edits when opening from Files/desktop/Start:
   - apps/office/editor.c:34-37 new `ed_guard()` (and `ed_fname`); public editor_open/editor_open_buf (:81-82) call the guard, then the new static `ed_open_slot`/`ed_open_mem`.
   - Deviation from the report: pyide_open (:47-50) calls ed_guard once and then the raw openers. With the report's patch as written, a blocked open would still set ed_file=-1/ed_py=1 and turn the user's unsaved document into the Python IDE buffer.
   - Callers fixed so a blocked open does no harm: trash_and_sort.c:184 runs a .nvx only if `ed_file==k`; desktop_clicks.c:60 no longer clears ed_py before editor_open.
   - apps/office/writer.c:234-237: the public writer_open_slot has the wp_mod guard and the body is now static wr_open_slot. Deviation: the report says a second check in the internal Open dialog is harmless. It is not: cmd(2) clears wp_warned, so the guard would fire again and need a third click. The dialog (:283) now calls wr_open_slot directly.
4. Stale ed_file: editor.c:89 checks that the slot is still used and has the same name (ed_fname), otherwise it falls back to Save-as-Untitled. ed_fname is set in ed_open_slot and in the save-as branch (:92).
5. Workspace focus: wm.c:57 focus_win sets `WN[i].ws=g_ws`. desktop/window_titles_and_bars.c:213 ws_switch now focuses the top window of the new workspace instead of setting focus=-1.
6. Alt+Tab MRU: input.c:306 sorts the native slice by z, highest first.
7. Context menu: input.c:125 handles r==2 (menu stays open) and clears g_submenu. :458 tree "New..." returns 2.
8. Task Manager: apps/system/taskmgr.c:34 adds tm_jpid, set at :330. End task (:327) and Priority (:326) require `jp->pid==tm_jpid`. If the pid no longer matches, End task shows "That program has already ended".

## cs3 (6 of 8 applied)
- chmod b2[40]→96 (shell/commands/accounts_files_info.c:158). Redirect b[80]→128 (shell/commands/prompt_and_dispatch.c:261).
- Trash restore b[96]→208 on both lines (trash_and_sort.c:252,257). Run as admin cl[220]→272 (input.c:444).
- g_ss_last[80]→144 (apps/system/screenshot.c:24; no extern elsewhere). Also its klog line t[120]→176 (:94): it copies g_ss_last, and at the full path length plus counters it was about 118 of 120 bytes.
- regedit nk[64]→72 (apps/system/regedit.c:90). re_cp with cap 0 only writes a NUL, which is in bounds.
- SKIPPED #5 uvc_webcam.c aud_rname and #6 apps/x11/xapps.c kb[48]: outside my file set (drivers/, apps/x11). Each is a one-number change, ready for whoever owns those files.

## cs5 (3 applied as named in my task)
- sh_arith (prompt_and_dispatch.c:102-103): `if(!nt)return 0;` and `ops[k]`. Tested on the host with a copy of the function: 2*3=6, 2-3+4=3, 10/2=5, 1+2*3=7, 7%4=3, 10-2*3-1=3.
- apt le_push (fs/pkg/apt/unpack_and_prompts.c:271): frees a/b on partial allocation failure.
- sysroot replace order (fs/vfs/sysroot.c:204-222). The report gave no patch, so I wrote one:
  - sysroot_add_layer now parses the new layer first. Only on success does it swap the layer structs and the g_e layer ids, so the package keeps its old layer index and precedence, and then it deletes the old layer through the new `sr_del_idx`.
  - If parsing fails, the old layer stays untouched. If the old layer is busy, it returns 0 before touching anything (as before).
  - If no layer slot is free (SR_MAXL), it falls back to the old order: delete first, then add.
  - sysroot_del_layer is now a wrapper around sr_del_idx.
- Not done (outside my files, not named in my task): cs5 #2/#3 uvc buffers.

## Tests
- /home/claude/ccheck.sh on input.c, wm.c, desktop.c, editor.c, writer.c, taskmgr.c, file_explorer.c, commands.c, apt.c, sysroot.c, screenshot.c, regedit.c: rc=0, no errors. The warning count for the UI/office set is 87 both before and after (all the existing -Wmisleading-indentation warnings).
- `python3 tools/check.py --only usb`: 12/12 passed.
- Not run in QEMU.

## Fix round (branch y-c3, from r3-review.md)
- HIGH 1 restore after a title click: applied the review's patch (input.c:207-211). The drag block now un-maximizes only when the mouse actually moves while the button is held. It then restores sw/sh and clamps dox. A maximized window is no longer moved or un-maximized by a plain click, so win_max can restore the saved size.
- MEDIUM 2: clicks_and_drops.c:233 runs a .nvx only if `ed_file==g_fsel` (the same pattern as trash_and_sort.c).
- LOW 3:
  - new_note and view_source (input.c:353/359) first call the new `editor_open_blocked()` (editor.c:38). It warns without clearing the flag, so the second click passes both that check and the guard inside editor_open. The review's `editor_open_buf("",0)` trick would have needed a third click.
  - The `edit` shell command says when the open was blocked.
- LOW 4: the guard has its own flag `ed_gwarn` (editor.c:34/37/38), which editor_key resets. It is not named ed_gw because that name already exists at editor.c:146 (gutter width).
- LOW 5: the stale-slot check also compares FS dir (ed_fdir), which is set on open and on save-as.
- LOW 6: screenshot klog t[176]→224.
- LOW 7:
  - sh_arith: x/-1 is computed as an unsigned negate (no #DE) and x%-1 gives 0. Host test with UBSan: `4611686018427387904*-2/-1` gives LONG_MIN with no trap, and 9/-1 gives -9.
  - regedit refuses a parent key of 63+ chars ("Key path too long").
- NOT DONE: AltGr acting as Alt on USB. Fixing it needs drivers/usb/xhci/hid_keyboard_mouse.c:34 (left Alt only, mask 0x04), which is outside my file set. Setting the global `kalt` from USB, as the review suggests, would also change the PS/2-side logic that reads kalt. I suggest whoever owns the USB driver add a separate left-Alt flag.
- Tests: ccheck shows no errors on input.c, editor.c, file_explorer.c, commands.c, screenshot.c and regedit.c. Not run in QEMU.
