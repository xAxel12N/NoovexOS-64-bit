# I2 — easier daily desktop use (brainstorm, read-only)

## What already exists (checked: ARCHITECTURE.md, graphics/ui/*, graphics/ui/desktop/*, shell)
- Win key combos (graphics/ui/input.c:249-258): Win+T Terminal, Win+Shift+T ChatGPT, Win+E Files, Win+L lock, Win+P display OSD, Win+I Settings, Win+A quick settings, Win alone = notification panel. Win+Shift+R Regedit, Win+R (docs).
- Ctrl+1..4 workspaces, Ctrl+Shift+Esc Task Manager, Ctrl+Alt+Esc (kill hung), Ctrl+Alt+F/N/G/P, Alt+Tab (incl. X11 windows).
- Media/volume/brightness/mic/airplane/touchpad/camera hotkeys + OSD (graphics/ui/hotkeys.c), Print Screen tool (+Shift/Alt).
- Drag-to-edge snapping: halves, quarters, top=maximize (input.c:203, wm.c:62 apply_snap, desktop_clicks.c:251 preview).
- Title double-click maximize, lock after idle, night light, do-not-disturb, touchpad gestures (swipe/pinch), middle-click in browser, taskbar badges, desktop icons + context menu.

## Missing (grep proof, ARCHITECTURE.md + code): "Win+D"=0, "Show desktop"=0, "Alt+F4"=0, "Win+Left"/"Win+arrow"=0, no kdown[0x20]/[0x3E]/arrows used in input.c Win block.

## Top 3 NEW ideas (~10 min each)

### 1. Win+D — show desktop / restore (owner: one key to get to desktop icons and back)
- File: graphics/ui/input.c, Win block (line ~257). Add `if(kdown[0x20] && !winR){ ... winR=1; changed=1; }` (0x20 = D; USB maps D via hid2scan letter table).
- Logic: static u32 mask; if any WN[i].open&&!WN[i].min -> record bit i, call win_min(i) (wm.c:58); else for bits in mask focus_win(i) (wm.c:57), mask=0. Optional: also xwin_minimize(k) for X windows (apps/x11/xwin.c:45).
- Proof: `grep -c "Win+D\|Show desktop" ARCHITECTURE.md` = 0; no kdown[0x20] anywhere in graphics/ui.

### 2. Win+Left/Right/Up/Down — keyboard snapping of the focused window
- File: graphics/ui/input.c Win block. Arrows already set kdown: PS/2 ext 0x4B/0x4D/0x48/0x50 (ps2.c:63), USB hid2scan (hid_keyboard_mouse.c:17).
- Win+Left -> apply_snap(focus,1); Win+Right -> apply_snap(focus,2); Win+Up -> win_max(focus) if !max; Win+Down -> if max restore (win_max toggles) else win_min(focus). Guard focus>=0 && WN[focus].open.
- BUG to fix at the same time (wm.c:62): apply_snap sets max=1 without saving sx/sy/sw/sh, so "restore" after a snap (title double-click, max button) jumps to stale geometry. Add at top: `if(!WN[i].max){ WN[i].sx=WN[i].x; WN[i].sy=WN[i].y; WN[i].sw=WN[i].w; WN[i].sh=WN[i].h; }`. Also fixes drag-snap restore.
- Caveat: arrow still kpush'es 17-20 into the focused app; acceptable, or swallow in kpop loop when kdown[0x5B].
- Proof: "Win+Left"/"Win+arrow" = 0 in ARCHITECTURE.md; Win block only checks 0x13,0x12,0x26,0x19,0x17,0x1E.

### 3. Alt+F4 — close the focused window
- File: graphics/ui/input.c shell-hotkey block (next to cafl, line ~259): `static int af4=0; if(kalt&&kdown[0x3E]){ if(!af4){ if(focus>=0&&WN[focus].open) close_win(focus); else { for(k<xwin_count()) if(xwin_active(k)) xwin_close(k); } changed=1; } af4=1; } else af4=0;` X window first if one is active (xwin_active, xwin.c:43) since it holds keyboard.
- USB needs one line: drivers/usb/xhci/hid_keyboard_mouse.c hid2scan add `case 0x3D:return 0x3E;` (F4). PS/2 already sets kdown[0x3E] (ps2.c:63 non-ext path).
- Do not trigger when g_launcher/lock screen/acct dialog active (check g_state==ST_DESKTOP).
- Proof: "Alt+F4" = 0 in ARCHITECTURE.md; no 0x3E in input.c; hid2scan has no F-keys.
