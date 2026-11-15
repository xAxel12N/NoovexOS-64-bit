# r3 review of c3 (branch x-c3, merged into main as v11.07)

Scope: `git log -p v11.06..x-c3` (764f985f, 221febb8), c3-report.md, ctasks/c3.md. All line numbers are in main (01c10690). x-c3 merged cleanly. The only other changes to the same files are c2's nvxlc_waitkey and the Calculator key route in input.c, and neither touches c3's hunks.

Verdict: most of the patch set is correct. The buffer sizes, sh_arith, the le_push leak, the taskmgr pid check, the ctx submenu fix, the workspace focus, the Alt+Tab MRU order and the sysroot replace order all check out (details at the end). One fix is a **false fix (HIGH)**, the editor guard missed **one caller (MEDIUM)**, and there are several LOW items.

---

## HIGH 1 - "Restore after title-bar click on a maximized window" is a false fix
graphics/ui/input.c:159 (c3's change) and :207-210 (not touched).

c3 now saves sx/sy/sw/sh only when `!WN[i].max`. But the drag-move block runs on **every poll while the button is held, even with zero movement**. It clears `max` there and leaves w/h at full screen size:
```
207  if(drag>=0&&btn){ int nx=mx-dox, ny=my-doy;
210      WN[drag].x=nx; WN[drag].y=ny; if(WN[drag].max)WN[drag].max=0;
```
Failure scenario: maximize a window, then click (no drag) its title bar. A real click lasts more than one poll, so `max` becomes 0 and the window stays full size. The context menu now says "Maximize". Pressing Maximize/Restore calls win_max (wm.c:59), and because `!max` it **overwrites sx/sw with the full-screen geometry**. The restore size is lost exactly as before. Dragging a maximized window also keeps it full size with max=0, and the sizes c3 preserved are never used. Nothing was run in QEMU, so this went unseen.

Patch (input.c, replace line 207 and the end of 210):
```
-        if(drag>=0&&btn){ int nx=mx-dox, ny=my-doy;
+        if(drag>=0&&btn&&WN[drag].max&&(dx||dy)){ WN[drag].max=0; if(WN[drag].sw>0&&WN[drag].sh>0){ if(dox>=WN[drag].sw)dox=WN[drag].sw/2; WN[drag].w=WN[drag].sw; WN[drag].h=WN[drag].sh; } }
+        if(drag>=0&&btn){ int nx=mx-dox, ny=my-doy;
@@
-            WN[drag].x=nx; WN[drag].y=ny; if(WN[drag].max)WN[drag].max=0;
+            if(!WN[drag].max){ WN[drag].x=nx; WN[drag].y=ny; }
```
How it behaves after the patch:
- A click without movement keeps the window maximized, and Restore works.
- A real drag un-maximizes to the saved size, Windows-style, keeping the cursor on the title bar.
- `dx`/`dy` are this poll's mouse delta, already used at :206.
- `g_snap` cannot be set while the window is still max, because the title bar of a maximized window never touches x<=3, x>=W-4 or y<=3.

## MEDIUM 2 - Editor guard: Files "Open" path still runs the user's unsaved buffer
apps/system/file_explorer/clicks_and_drops.c:233. c3 fixed the same pattern in trash_and_sort.c:184 but missed this one:
```
else { editor_open(g_fsel); if(fext_eq(FS[g_fsel].name,"nvx"))editor_run(); }
```
Failure scenario: the Editor has unsaved changes and the user opens a `.nvx` from the Files toolbar/Open. The guard blocks the open, but `editor_run()` still runs the **unsaved document** as a program. If the buffer is the Python IDE (`ed_py=1`), it goes through pyide_run, then py_save, which **writes the unsaved buffer to /root/main.py** and runs it in the Terminal. The guard's toast says nothing was opened.

Patch:
```
-        else { editor_open(g_fsel); if(fext_eq(FS[g_fsel].name,"nvx"))editor_run(); } } return; }
+        else { editor_open(g_fsel); if(ed_file==g_fsel&&fext_eq(FS[g_fsel].name,"nvx"))editor_run(); } } return; }
```
`ed_file` comes from editor.h and is already used the same way in trash_and_sort.c, which is in the same unit.

## LOW 3 - Guard callers that act as if the open succeeded
- graphics/ui/input.c:355 `new_note`: when the guard blocks, it still creates `note.txt` / `noteN.txt` on every click and toasts "New note". :360 `view_source` does the same with source.html. Patch for :355 (add as the first line of new_note; :360 is the same):
  ```
  +    { extern int ed_ro, ed_warned; if(ed_dirty&&!ed_ro&&!ed_warned&&WN[W_EDITOR].open){ editor_open_buf("",0); return; } }
  ```
  editor_open_buf then shows the guard toast and sets ed_warned, so the second click goes through.
- shell/commands/accounts_files_info.c:210: `edit` always prints "opened in editor." Patch:
  ```
  -else if(k>=0&&FS[k].used){editor_open(k);term_puts("opened in editor.\n");}
  +else if(k>=0&&FS[k].used){editor_open(k);term_puts(ed_file==k?"opened in editor.\n":"editor has unsaved changes - run edit again to discard\n");}
  ```

## LOW 4 - ed_guard shares `ed_warned` with the Editor's own New button
editor.c:37 and :207. Failure scenario: the user presses New in the Editor and gets "press again to discard" (ed_warned=1). They change their mind and, without typing, double-click a file in Files. ed_guard sees ed_warned=1 and **silently discards** the edits. It also works the other way: a guard warning arms New. Patch: give the guard its own flag.
```
-static int ed_guard(void){ if(ed_dirty&&!ed_ro&&!ed_warned&&WN[W_EDITOR].open){ ed_warned=1; ... return 1; } ed_warned=0; return 0; }
+static int ed_gw=0;
+static int ed_guard(void){ if(ed_dirty&&!ed_ro&&!ed_gw&&WN[W_EDITOR].open){ ed_gw=1; toast("Editor has unsaved changes - open again to discard, or Save first"); focus_win(W_EDITOR); return 1; } ed_gw=0; return 0; }
@@ editor_key
-    ed_warned=0;
+    ed_warned=0; ed_gw=0;
```

## LOW 5 - Stale-slot check compares only the name
editor.c:89. There are two problems:
- If the file is deleted and its slot is reused by a **different file with the same name in another directory**, Save overwrites that file.
- If the user renames the open file in Files, Save quietly goes to "Untitled.txt". That is a behaviour change, but safe.

Patch (also check dir):
```
-static char ed_fname[64];
+static char ed_fname[64]; static int ed_fdir=-1;
@@ ed_open_slot
-    editor_reset(); ed_file=fsidx; scopy(ed_fname,FS[fsidx].name)[0]=0;
+    editor_reset(); ed_file=fsidx; scopy(ed_fname,FS[fsidx].name)[0]=0; ed_fdir=FS[fsidx].dir;
@@ editor_save
-    if(ed_file>=0&&(!FS[ed_file].used||!seq(FS[ed_file].name,ed_fname)))ed_file=-1;
+    if(ed_file>=0&&(!FS[ed_file].used||!seq(FS[ed_file].name,ed_fname)||FS[ed_file].dir!=ed_fdir))ed_file=-1;
@@ save-as branch
-ed_file=s; scopy(ed_fname,nm)[0]=0;
+ed_file=s; scopy(ed_fname,nm)[0]=0; ed_fdir=FS[s].dir;
```

## LOW 6 - screenshot.c:94 t[176] can still overflow by 1 byte
Worst realistic case:
- "screenshot: " is 12 bytes.
- "3840x2160" is 9 bytes.
- ", " is 2 bytes.
- a 5-digit KB count is 5 bytes.
- " KB, " is 5 bytes.
- g_ss_last at its maximum (g_home[96] holds up to 95 chars, + "/Pictures/" + a ~35-char name) is 143 bytes.

That is 176 bytes, plus the NUL makes 177. Patch: `char t[176]` -> `char t[224]`.

## LOW 7 - Older bugs found next to the changed lines (one-liners)
- prompt_and_dispatch.c:103 sh_arith: `$((4611686018427387904*-2/-1))` gives LONG_MIN/-1, which raises **#DE in the kernel shell**. Patch: `else if(o=='/'){ if(terms[k]&&!(terms[k]==-1&&v[nv-1]==(long)(1UL<<63)))v[nv-1]/=terms[k]; } else if(o=='%'){ if(terms[k]&&terms[k]!=-1)v[nv-1]%=terms[k]; else if(terms[k]==-1)v[nv-1]=0; }`
- regedit.c:90: if the parent key is 63 chars long, L=64, re_cp writes only the NUL (c3 made this in bounds), and `reg_add_key("parent\\")` is called with an empty leaf. Patch: after `int L=(int)(p-nk);` add `if(L>=63){ re_say("Key path too long",1); re_mode=RM_NONE; return; }`.
- USB `kdown[0x38]` is set from `m&0x44`, which includes **Right Alt = AltGr** (hid_keyboard_mouse.c:34). On the owner's Swedish layout, AltGr+Tab now acts as Alt+Tab, while PS/2 routes AltGr to kaltgr. This is minor and the file is outside c3's set. If wanted: in hid_keyboard_mouse.c:34 also set `kalt=(m&0x04)?1:0;`. That makes left Alt the USB Alt everywhere, and the `||kdown[0x38]` in input.c:303/306/337 can then be dropped.

---

## Checked and OK
- Alt+Tab: `kalt||kdown[0x38]` is used in all 3 places (pass-through, open, release). The z-sort only covers native entries [s0,s1) and never sorts an X window index into WN[].
- ctx menu: the r==2 return keeps g_ctx=1. All other exits clear g_submenu. :431 (outside click while the submenu is open) returns 1, so the menu is cleared.
- ws_switch picks the topmost window of the new workspace. focus_win now pulls the window to the current workspace, which is what h6 intended, also when it is reached via Alt+Tab or the taskbar.
- Writer: `wr_open_slot` is used by the internal Open dialog, so there is no triple click. The public guard (`D&&wp_mod&&!wp_warned&&open`) is correct. On -1 the shell `writer` command still prints "opened in Writer", which is cosmetic.
- taskmgr: tm_jpid is captured at selection, and both Priority and End task are pid-checked.
- sysroot: parse-first-then-swap is correct:
  - the layer structs and the g_e layer ids are swapped symmetrically, and sr_del_idx(li) removes the old entries and remaps g_pcf.
  - busy is checked before anything changes.
  - when parsing fails, the old layer stays and the callers (deb_archive.c:389, update.c:669) kfree only the new blob.
  - g_e grows dynamically, so the temporary double entries are fine.
  - Bonus: the old delete-then-add order was a use-after-free if a caller ever re-added the old owned blob.
- Buffers:
  - chmod: 77 of 96 bytes used.
  - redirect: 95 of 128.
  - Trash restore: 204 of 208.
  - Run as admin: 261 of 272.
  - g_ss_last: 141 of 144.
  - regedit nk: 65 of 72.
- sh_arith: `ops[nt]` is stored after `terms[nt++]`, so `ops[k]` is the operator before term k. Correct, and the host tests are valid. le_push is `unused` dead code, but the fix is right.
- Tests: only ccheck and the usb group were run, so nothing exercised the UI logic. That is why HIGH 1 was missed. Suggest a QEMU check: maximize, click the title, press Restore.
