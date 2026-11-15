# h6 report: graphics/ui + apps/system + apps/office + Terminal (v11.06, read-only)

Sorted by importance. All line numbers are against the v11.06 tree. None of these is in handoff-v11.0x "fixed".
(Already-known items I skipped: "Win keys also reach the focused app" (v11.04), terminal runs commands under the BKL.)

## 1. Alt+Tab never works on a USB keyboard (the owner's keyboard is USB). MEDIUM, 95%
`graphics/ui/input.c:306` and `:336` test `kalt`. `kalt` is only ever written in `drivers/input/ps2/ps2.c` (lines 64/74/75).
The USB path (`drivers/usb/xhci/hid_keyboard_mouse.c:34 usbk_mods_sync`) only sets `kdown[0x38]`.
Result on USB: Alt+Tab never opens the switcher. The Tab goes to the focused app (a tab in Terminal/Editor) instead.
Patch (input.c):
- line 306: `if(c==9 && kalt){` → `if(c==9 && (kalt||kdown[0x38])){`
- line 336: `if(!kalt && alt_prev && g_alttab){ ... } alt_prev=kalt;`
  → `{ int ka=kalt||kdown[0x38]; if(!ka && alt_prev && g_alttab){ ...unchanged... } alt_prev=ka; }`
  (keep the inner block as it is and just swap `kalt` for `ka` twice)

## 2. Clicking the title bar of a maximized/snapped window breaks Restore (window stays full size). MEDIUM, 90%
`graphics/ui/input.c:159`: a title-bar press always runs `WN[i].sx=WN[i].x; ...sy/sw/sh` before it starts the drag, even when `WN[i].max==1`.
Scenario: maximize Files. Click its title bar to focus it (no movement, so `max` stays 1). Click Maximize/Restore: `win_max` restores to sx..sh, which now holds the maximized geometry, so nothing changes. Win+Down does the same (it calls win_max). Snapped windows have the same problem (apply_snap sets max=1).
The save is not needed when max=0 either, because apply_snap already saves geometry when `!max`.
Patch:
`if(py<WN[i].y+TITLEH){ WN[i].sx=WN[i].x;WN[i].sy=WN[i].y;WN[i].sw=WN[i].w;WN[i].sh=WN[i].h; drag=i; ...`
→ `if(py<WN[i].y+TITLEH){ if(!WN[i].max){ WN[i].sx=WN[i].x;WN[i].sy=WN[i].y;WN[i].sw=WN[i].w;WN[i].sh=WN[i].h; } drag=i; ...`

## 3. Editor: opening a file from Files, the desktop or Start silently throws away unsaved edits. MEDIUM (data loss), 90%
`apps/office/editor.c:55 editor_open` and `:65 editor_open_buf` call `editor_reset()` with no `ed_dirty` check. Only the in-app "New" button warns (line 199).
Scenario: type in the Editor without saving. Double-click another .txt in Files. The edits are gone.
Patch: add as the first statement of both editor_open and editor_open_buf:
`if(ed_dirty&&!ed_ro&&!ed_warned&&WN[W_EDITOR].open){ ed_warned=1; toast("Editor has unsaved changes - open again to discard, or Save first"); focus_win(W_EDITOR); return; } ed_warned=0;`
(editor_key already resets ed_warned=0 on every key, so the "press again" logic matches the New button.)
Same bug in Writer: `apps/office/writer.c:229 writer_open_slot` (reached from Files, trash_and_sort.c:180, and the `writer` command) resets the document with no `wp_mod` check. Patch, first line of writer_open_slot after the range check:
`if(D&&wp_mod&&!wp_warned&&WN[W_WRITER].open){ wp_warned=1; toast("Writer has unsaved changes - open again to discard, or Save first"); focus_win(W_WRITER); return -1; } wp_warned=0;`
Note: the internal Open dialog (cmd 2) already warned before, so a second check there is harmless.

## 4. Editor Save can overwrite a different file (stale slot index). MEDIUM (data loss), 80%
`editor.c:79 editor_save` writes to `FS[ed_file]` and never checks that this slot still holds the opened file. Nothing outside editor.c updates `ed_file`, so deleting the file in Files leaves it stale.
Scenario: open note.txt (slot 5). Delete note.txt in Files. Download or create another file, which takes slot 5. Ctrl+S / Save in the Editor overwrites that new file with the editor text. Or, if the slot is still free, it writes into an unused slot and the save is lost while the toast says "Saved".
Patch: add `static char ed_fname[64];`. In editor_open after `ed_file=fsidx;` add `scopy(ed_fname,FS[fsidx].name)[0]=0;` (scopy returns the end pointer). In editor_save, before the `if(ed_file<0)` block, add:
`if(ed_file>=0&&(!FS[ed_file].used||!seq(FS[ed_file].name,ed_fname)))ed_file=-1;`
The save then falls back to "Saved as Untitled N.txt". After the save-as branch sets `ed_file=s;`, add `scopy(ed_fname,nm)[0]=0;`
(Side effect: after a rename in Files, the next save becomes a new Untitled file. That is much safer than overwriting.)

## 5. A window open on another workspace gets keyboard focus but stays invisible. MEDIUM, 85%
`graphics/ui/wm.c focus_win` sets focus and z but not `ws`. Callers: dock icons (input.c:146), classic quick-launch Files/Terminal/Browser (input.c:126), Alt+Tab (input.c:336), and the Alt+Tab list, which includes every open window on every workspace (input.c:306).
Scenario: Terminal is open on workspace 1. On workspace 2, click the Terminal quick-launch or dock icon. Nothing appears, but focus==W_TERM, so typing goes into the invisible terminal (commands run blind). The classic taskbar hides it too (it filters by ws).
Patch (wm.c focus_win):
`if(WN[i].min)gfx_win_event(i,0); WN[i].min=0; focus=i;` → `if(WN[i].min)gfx_win_event(i,0); WN[i].min=0; WN[i].ws=g_ws; focus=i;`
Related LOW: `ws_switch` (graphics/ui/desktop/window_titles_and_bars.c:213) sets `focus=-1` instead of focusing the top window of the new workspace, so keyboard input is dropped after Ctrl+1..4 until you click. Patch: replace `if(focus>=0&&WN[focus].ws!=g_ws)focus=-1;` with
`if(focus<0||WN[focus].ws!=g_ws){ int b=-1,bz=-1; for(int k=0;k<NWIN;k++) if(WN[k].open&&!WN[k].min&&WN[k].ws==g_ws&&WN[k].z>bz){bz=WN[k].z;b=k;} focus=b; }`

## 6. Alt+Tab order is by app index, not most-recently-used. LOW-MEDIUM, 95% (behaviour)
`input.c:306` fills g_altlist in WN index order and preselects entry 1. A quick Alt+Tab goes to whichever open window has the next index, not to the previous window. It also lists minimized windows and windows on other workspaces (made safe by #5). Patch: right after the `for(int i=0;i<NWIN;i++) if(WN[i].open)g_altlist[g_altn++]=i;` loop, sort that native slice by z descending:
`{ int s0=(xa>=0)?1:0, s1=g_altn; for(int a=s0;a<s1;a++)for(int b=a+1;b<s1;b++) if(WN[g_altlist[b]].z>WN[g_altlist[a]].z){int t=g_altlist[a];g_altlist[a]=g_altlist[b];g_altlist[b]=t;} }`
(Only matters after #1 makes Alt+Tab work on USB.)

## 7. Context menu: the click that closes it leaves g_submenu=1 stuck, and Files-tree "New..." can never be used. LOW-MEDIUM, 85%
`input.c:125`: `r=ctx_click(px,py); g_ctx=0; if(r) return;`. g_submenu is not cleared.
- Clicking a row that has a submenu (desktop "View"/"Sort by"/"New", which a user clicks naturally) runs ctx_click: no action, return 1, and `g_submenu` stays 1 (it was set by hovering). With g_ctx=0 nothing redraws the menu, so g_submenu stays 1 until the next right-click. While stuck, `desk_win_exposed()` and `desk_popups_clear()` return 0, so the Video player, Browser fast path and X overlay fall back to full-screen composes (slower and choppier video).
- `ctx_click` line 458 (tree "New..."): `g_submenu=1; g_ctx=1; return 0;` is meant to keep the menu open, but the caller then sets g_ctx=0, and r=0 lets the click fall through into the Files window under the menu (a stray click in Files).
Patch: line 458 `else if(lbl[0]=='N'){ g_submenu=1; g_ctx=1; return 0; }` → `else if(lbl[0]=='N'){ g_submenu=1; return 2; }`
and line 125 → `if(g_ctx){ int r; INM("context menu click",-1,0,0,r=ctx_click(px,py)); if(r==2) return; g_ctx=0; g_submenu=0; if(r) return; }`

## 8. Task Manager "End task" can kill the wrong process (stale job index). LOW, 60%
`apps/system/taskmgr.c:327` kills `g_jp[tm_jsel].pid` with no check that the slot still holds the process the user selected. If it exited and the slot was reused, a different program is killed. Patch: save the pid when the row is selected (`tm_jpid=g_jp[k].pid` next to where tm_jsel is set) and require `jp->pid==tm_jpid` before `jc_kill`.

## Notes on the owner's taskbar/Start freeze (not solved, for whoever is on it)
- `busy_pump()` (graphics/ui/desktop/present_and_pump.c:132) bumps `g_ml_beat` BEFORE its re-entry/`lastp` guard. Any wait loop that calls it keeps the freeze box quiet even when it does nothing else. That matches "no red box". Consider moving the beat after the guard, or rate-limiting it to once per tick.
- What differs from a desktop click: the Start toggle calls `launcher_search()`, and each launcher frame calls `lgrp_refresh()`→`xapps_count()`→`xapps_refresh()` (apps/x11/xapps.c:68). That does `sysroot_stats` + `k_readdir_at("/usr/share/applications")` and, when submenu icons are drawn, `load_png_scaled` (a 4 MB kmalloc + PNG decode per app). If k_readdir_at or the PNG path reaches a disk/USB driver wait that pumps busy_pump, you get exactly ticks + heartbeat alive with a stuck UI. Worth a crumb() around xapps_refresh and find_icon.
- Dock/taskbar-button clicks go through `focus_win`→`open_win`→`focus_app_started`/`gfx_win_event` (no nested loop there; win_anim is a no-op).
