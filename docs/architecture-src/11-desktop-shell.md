# Part 11: Desktop, applications, shell and scripting

This part covers everything above the kernel services: the window manager and compositor, the desktop shell (top bar, dock, launcher, notifications), the full-screen "live" terminal, the setup/login flow, every built-in application, the `nsh` shell and its complete command set, and the four ways to run code that is not compiled into the kernel (the integer JavaScript interpreter, Lua 5.5, Rune-compiled objects, and `.MOD` relocatable modules).

Each GUI and app file is its own compilation unit with a header under `include/nvx/gui/` or `include/nvx/apps/`. The table lists them in link order (`units.list`):

| File | Contents |
|---|---|
| `assets/font.c`, `assets/fontaa.c`, `lib/js/js.c` | bitmap font, anti-aliased font, the small integer JS interpreter (the web engine and its QuickJS are in Part 13) |
| `lang/rune/rune_gfx.h` | prototypes for the Rune-compiled object |
| `graphics/theme/effects.c` | glass/blur panels |
| `graphics/theme/theme.c` | colour constants, `g_state`, cursor drawing |
| `graphics/ui/wm.c` | `WN[]`, open/close/focus, notifications, window frame |
| `shell/terminal.c` | terminal grid, scrollback, prompt, `neofetch` text |
| `shell/commands.c` | parser, pipes, scripting, `run_cmd_one`, live terminal |
| `apps/*.c` | editor, calculator, browser, settings, files_classic, (fs/pkg.c), file_explorer, small_apps, doom, logs |
| `graphics/ui/desktop.c`, `graphics/ui/input.c` | compositor, dock, launcher, `process_input` |
| `core/init/splash.c`, `bootmenu.c`, `installer.c`, `setup.c` | boot UI, installer, OOBE and login |

Note: the file names do not always match what is inside. `graphics/theme/theme.c` also holds the mouse-cursor code (`ARROW[]`, `draw_cursor`). `graphics/theme/effects.c` holds `glass_panel`/`glass_liquid`. `apps/system/logs.c` holds the central `draw_app`/`app_click` dispatchers. The Lua drawing canvas is in `graphics/ui/input.c`.

---

## 11.1 UI states and the main loop

`graphics/theme/theme.c` defines the global UI state machine:

```c
enum { ST_SETUP, ST_LOGIN, ST_DESKTOP, ST_BOOTMENU, ST_RECOVERY, ST_DISKMGR, ST_INSTALL, ST_LIVE };
static int g_state=ST_DESKTOP;
```

The default is `ST_DESKTOP`, but `core/init/main.c` changes the state to `ST_LIVE` before the main loop starts:

```c
g_state=ST_LIVE; live_init(); ... live_present();
```

The OS therefore always boots into the full-screen live terminal. Each pass of the main loop in `core/init/main.c` (lines ~173-227) sends input to the handler for the current state:

| `g_state` | input handler | redraw |
|---|---|---|
| `ST_LIVE` | `live_input()` (commands.c) | `live_present()` |
| `ST_DESKTOP` | `process_input()` (gui/input.c) | `compose_present()` / `present_cursor()` / `present_topbar()` |
| `ST_BOOTMENU` | `bootmenu_input()` | `bootmenu_present()` |
| `ST_RECOVERY` | `recovery_input()` | `recovery_present()` |
| `ST_DISKMGR` | `dm_input()` | `dm_present()` |
| `ST_INSTALL` | `ip_input()` | `ip_present()` |
| `ST_SETUP` | `setup_input()` (init/setup.c) | `setup_present()` |
| otherwise (`ST_LOGIN`) | `login_input()` | `login_present()` |

Other things the loop does in desktop mode:

- Session start. It calls `ux_desktop_start(g_user)` (fs/unixfs.c) once per boot. That function ensures the Unix account exists, logs the user in, and starts `init` with `jc_start_init()`.
- Job stepping. It calls `jc_step()` every pass.
- Hardware polling. It runs `battery_tick()` and the USB hot-plug check. When a USB stick arrives, Files opens in USB mode and the user sees the notification "USB drive / Flash drive connected".
- Frame pacing. It paces frames to `g_refresh_hz`, which Settings to Display sets to 30, 60 or 120.

### Presentation policy (init/main.c)

| Condition | Call | Cost |
|---|---|---|
| `ch` (an input handler reported a change) | `compose_present()` | full scene rebuild + full blit |
| ~0.5 s heartbeat (`g_ticks-last_frame>=50`) with DOOM open or a toast pending | `compose_present()` | full |
| ~0.5 s heartbeat otherwise | `present_topbar()` | 28-px strip only (clock/battery) |
| only the pointer moved, at frame rate | `present_cursor()` | two 44x44 rectangles |
| pointer moved while near the dock (`my > H-96`) or a menu/drag/launcher is open | `compose_present()` | full (dock magnification and menus follow the pointer) |
| `/dev/fb0` is open by a user program (`g_fb0_open>0`) | `blit_all()` on `g_fb0_dirty` | the compositor stands aside |

---

## 11.2 Window manager (`graphics/ui/wm.c`)

### Fixed window table

The OS has no dynamic window list. Every app has exactly one slot in `WN[NWIN]`:

```c
enum { W_WELCOME, W_TERM, W_FILES, W_EDITOR, W_CALC, W_BROWSER, W_CLOCK, W_SYSMON, W_CALENDAR,
       W_SNAKE, W_FLAPPY, W_NEOFETCH, W_PAINT, W_DOOM, W_SETTINGS, W_ABOUT, W_CHATGPT, W_PPTX,
       W_LOGS, W_TFIND, NWIN };
typedef struct{ int x,y,w,h,open,z,min,max,sx,sy,sw,sh,ws; } Win;
static Win WN[NWIN];
```

`APPNAME[NWIN]` holds the display names, for example `W_SYSMON` to "Task Manager" and `W_TFIND` to "Terminal Finder". The fields of `Win` are:

- `z`: a stacking counter. Each focus takes `++ztop`.
- `sx,sy,sw,sh`: the saved geometry used by restore.
- `ws`: the workspace (0-3, `NWS 4`) the window belongs to.

| Function | Behaviour |
|---|---|
| `open_win(i)` | Sets `open=1, min=0, ws=g_ws`, gives the window focus and raises it. On a first open, runs the app's reset hook (`snake_reset`, `flappy_reset`, `cal_open`) and calls `win_anim(i,1)`. |
| `close_win(i)` | Clears `open`/`min` and drops focus. Closing DOOM also releases the mouse (`g_doom_mcap=0`). |
| `focus_win(i)` | Opens the window if it is closed; otherwise un-minimises and raises it. |
| `win_min(i)` / `win_max(i)` | `win_max` toggles between the saved geometry and the full work area `(6,32,W-12,H-32-86)`. |
| `apply_snap(i,s)` | Snap targets: 1 = left half, 2 = right half, 3 = full, 4/5 = top-left/top-right quarter, 6/7 = bottom quarters. |
| `win_ctl_hit(i,px,py)` | Hit-tests the three 18 x 16 caption buttons (0 = min, 1 = max, 2 = close) placed by `wtbtn_geom` in the 30-px title bar (`TITLEH 30`). |
| `win_frame(i,title)` | Draws classic chrome with `wbevel`: a raised gray frame, a navy-to-blue horizontal gradient title bar (gray when the window is not focused) with bold white text, square 3D buttons that press in while the mouse button is down, and a 3D resize grip. |

Window animations are disabled. `win_anim()` in desktop.c returns immediately ("animations off: windows open/close instantly"). The old scale-and-fade code is kept as `win_anim_unused()`.

### Notifications and toasts

There are two separate mechanisms:

- Notifications. `notify(title, body)` writes into a 32-entry ring, `g_notif[NOTIF_MAX]`, and sets `g_toast_cur`. That toast is drawn on the next frame by `notif_toast_draw()`. The history panel (`notif_panel_draw`) opens from the bell in the top bar, or by tapping the Win key alone.
- Toasts. `toast(msg)` is a single-line status message (`toast_msg`, timestamp `toast_t`) drawn by `toast_draw()`. The main loop keeps redrawing for 300 ticks after one appears. Apps use it for short feedback such as "Saved", "Moved to Trash" and "Download complete".

---

## 11.3 Desktop shell and compositor (`graphics/ui/desktop.c`)

### Scene buffers

| Buffer | Role |
|---|---|
| `BG` | Wallpaper only. `build_bg()` draws it with `draw_wallpaper(g_theme)` and copies it. |
| `SCENE` | The composed frame. It is kept cursor-free between frames. |
| `SCENE_NC[MAXW*MAXH]` | A cursor-free snapshot used for dirty-rectangle cursor updates. |
| `WANIM` | 768x600 buffer, used only by the disabled animation. |

`compose_build()` assembles a full frame in this order:

```c
memcpy(SCENE,BG,...); desk_draw();                       /* wallpaper + desktop icons/files   */
/* open, non-minimised windows of the current workspace, sorted by z */
if(drag>=0&&g_snap) snap_preview_draw();
for(...) draw_app(order[a]);                             /* apps/logs.c dispatcher           */
marq_draw(); draw_dock();                                 /* taskbar on top of windows */
launcher, quick settings, clock popover, context menu, image viewer, alt-tab,
rename modal, properties, wifi panel, notification panel, toasts, workspace overlay
night_tint(0,H);                                         /* Night light, if on        */
```

`compose_present()` builds the frame, optionally overlays the FPS counter (`g_fps_overlay`), snapshots `SCENE` into `SCENE_NC`, draws the cursor, and calls `blit_all()`. It then restores the 44x44 box under the cursor (`CURBB`) from `SCENE_NC` with `nc_restore()`, so `SCENE` stays cursor-free.

`present_cursor()` is the cheap path for pointer movement:

```c
if(cxlast>=0) blit_region(cxlast-CURBB/2, cylast-CURBB/2, CURBB, CURBB);   /* erase old */
draw_cursor();  blit_region(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);          /* show new  */
nc_restore(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);
```

`blit_region()` chooses the copy routine by framebuffer format:

- `copy_swap32` for 32-bpp RGB-ordered framebuffers (`g_fb_rgb`);
- `sse_copy32` for 32-bpp BGR framebuffers;
- `sse_pack24` for 24-bpp framebuffers.

`ui_pump()` is installed as `g_wpa_pump`, so long-running operations such as Wi-Fi association can keep the mouse alive. It polls xHCI and calls `present_cursor()` every 2 ticks.

### Taskbar, start menu, workspaces

`g_dstyle` is 1 (classic); the modern dock code is still in the file but is not reachable.

- `draw_dock()` calls `draw_classic_bar()`, which draws a 30-px gray taskbar at the bottom (`g_tby`, `g_tbh`). From left to right:
  - the start button: a raised `bevel` with the NoovexOS logo and bold "NoovexOS", sunken while the menu is open;
  - one button per open window on the current workspace (`g_tbtn_*`, up to 16, at most 160 px wide) with a small icon (`mini_icon`, colour from `APPCOL[]`) and the name; the focused window's button is sunken and dithered;
  - a sunken box with the tray icons and the clock (`HH:MM`).
- Clicks on the taskbar are routed in `on_click()`: the tray (`classic_tray_click`), the start button (`px < g_lbx_end`) toggles the menu, the clock opens `clockpop_draw`, and anything else goes to `classic_taskbtn_click`, which minimises the focused window or calls `focus_win`.
- `present_topbar()` refreshes only the taskbar strip each second for the clock.
- Start menu (`classic_menu_draw`, `classic_menu_click`): a raised panel with a gray icon strip. Items: About NoovexOS, then four categories with submenus, then Exit. Hovering a category sets `g_csub` and draws the submenu to the right; it stays open while the pointer is in the menu or the submenu.

| Category | Apps (`CM_APPS`) |
|---|---|
| Development | Terminal, Editor, Terminal Finder, Logs |
| Games | Snake, Flappy Bird, DOOM |
| Graphics | Paint |
| Utilities | Files, Browser, Calculator, Calendar, Clock, Task Manager, neofetch, Settings |

  About opens `W_ABOUT`; Exit goes to the lock screen (`launcher_power(0)`), which offers Shut Down, Restart and Sleep. Typing while the menu is open still fills the launcher search, but the classic menu does not show results.
- Workspaces. `ws_switch(w)` wraps around 0-3 and shows `ws_overlay_draw()` for 120 ticks. Ctrl+1...4 and trackpad swipes still switch workspaces; the taskbar shows only the current workspace's windows.

### Tray and Quick Settings

The tray holds five 22-px icons left of the clock (`g_tray_x0`): show more (^), Wi-Fi (an airplane when Airplane is on), volume (muted, low or high from `g_snd_vol`), battery (charge bar, or a plug when there is no battery), and the bell (a red dot for unread notifications, a blue mark while Do not disturb is on). The bell toggles the notification panel; the other four toggle Quick Settings (`g_qs`).

Quick Settings (`cqs_draw`, `classic_qs_click`) is a 360 x 290 raised panel above the tray. Tiles are sunken and navy when on:

| Tile | Effect |
|---|---|
| Wi-Fi | closes the panel and opens the Wi-Fi list (`g_wifipop`), scanning if the list is empty |
| Airplane | `g_airplane` (shown in the tray) |
| Night light | `g_nightlight`: `night_tint()` scales green by 15/16 and blue by 5/8 over the composed frame and the taskbar strip |
| Battery saver | `g_battsave`: sets `g_refresh_hz` to 30 (back to 60 when off) |
| Do not disturb | `g_dnd`: `notif_toast_draw()` shows no pop-ups |
| All settings | opens Settings |

Below the tiles are a brightness slider (`bl_set`, only when `g_bl.ok`) and a volume slider (`g_snd_vol`, `g_snd.volume()`), then the battery state and a gear that opens Settings. The toggles are not saved across reboots.

### Desktop icons, launcher, context menu

- Desktop icons are switched off: `desk_draw()` returns at once and `desk_hit()` always returns -1. Apps are opened from the start menu. Files moved to the desktop folder (`FS[k].folder=2`) are still listed in Files.
- Launcher. The modern launcher (`launcher_draw` when `g_dstyle` is 0) is not shown in the classic style. `launcher_search()` still matches the query against the 17 apps in `LAUNCH[]`, the `SET_PAGES[]` entries and every `FS[]` name.
- Right-click menus. `ctx_items()` in gui/input.c chooses the item list from `g_ctxkind`:

| kind | target | items |
|---|---|---|
| 0 | desktop | View, Sort by, Refresh, New > (Text File, Markdown, Note, Code (.nvx), Config, HTML), Display settings, Personalize, Open Terminal, Change wallpaper |
| 1 | title bar | Minimize, Maximize/Restore, Close |
| 2 | file in Files | Open, Rename, Delete, Copy, Cut, then (ELF) Run / Run as administrator, (ZIP) Extract all, (image) Set as wallpaper |
| 3 | browser page | Back, Reload, Copy link, View source (saves `source.html` and opens it in the Editor) |
| 4 | classic tree | Open/Rename/Delete/New... or Open/New... or New.../Refresh |
| 5 / 6 | desktop app / desktop file | Open, Reset position, Properties / Open, Rename, Duplicate, Delete, Properties |
| 7 / 8 | explorer item / explorer background | Open, Rename, Delete / New folder, New file, Paste, List/Grid view |

"Run as administrator" builds the command `sudo /path/NAME`, opens the Terminal, and passes the command to `run_cmd()`. "New > Code (.nvx)" creates `script.nvx` containing a C-style `int main(){ printf(...); }` template.

### Themes and effects (`graphics/theme/theme.c`, `graphics/theme/effects.c`, `apps/system/settings.c`)

| Item | Where | Values |
|---|---|---|
| Accent | `ACC/ACC2`, `ACV[5]`/`ACN[5]` | Blue, Violet, Teal, Rose, Amber (`ACC2 = mix(ACC,black,60)`) |
| Wallpaper theme | `g_theme`, `THN[NWALL]` | 0-5 = Porsche, Crane, Wireframe, Peaks, Earth, Yosemite, decoded from embedded JPEGs by `wall_decode()`. The default is 1 (Crane) in `graphics/theme/theme.c`. |
| Custom wallpaper | `g_wallmode=1`, `wall_custom_slot` | any PNG/JPEG/WebP file ("Set as wallpaper"), cover-scaled by `wall_blit_cover()` with bilinear filtering (`wall_smooth()`, once per change); then `wall_enhance()` (`graphics/theme/wallfx.c`) removes colour bands, adds glow, depth blur, vignette and light. Built-in wallpapers are 1920x1080 JPEGs in `assets/wall_hd.c` (six, chosen with `g_theme` 0-5); smaller pictures are enlarged with Catmull-Rom filtering |
| Safe mode | `g_safemode` | flat gradient wallpaper, no animation |
| Cursor | `g_cursor`, `CUN[5]` | Arrow, Large, White, Cross, Dot (`draw_cursor`); hidden while DOOM holds the mouse |
| Terminal theme | `g_tth`, `TTH[6]` | Midnight, Dracula, Solarized, Paper (light), Matrix, Noovex |

`TTH[]` is defined in commands.c. The fixed shell colours (`WHT`, `GREY`, `GREEN`, `RED`, `CYAN`, and the prompt blue) are remapped per theme by `tmap()`. Light themes also darken bright custom colours.

`graphics/theme/effects.c` provides `glass_panel()` and `glass_liquid()`. Both use a running-sum box blur over `SCENE` (2 or 3 passes of radius `blurR`), a tint blend, a top highlight, and a 1-px rim. The terminal window has its own frosted background, `term_glass()`. It is a separable horizontal and vertical box blur (radius 10) into `g_blurtmp`, followed by an opacity blend `g_topa`. Settings to Terminal sets `g_topa` (Off/Low/Medium/High map to 100/90/80/<80) and `g_tblur`.

Wallpaper and accent sync with the signed-in Noovex account. `nvx_settings_save()` POSTs `{"accent":N,"theme":N}` to `/api/public/settings`, and `nvx_settings_load()` applies the returned values.

---

## 11.4 Input routing (`graphics/ui/input.c`, `process_input`)

`process_input()` returns `changed`, which tells the main loop whether a full frame is needed. Its order of operations:

1. DOOM release. Esc, or DOOM losing focus, releases DOOM's mouse capture.
2. Global hotkeys (edge-triggered on scan codes):
   - Ctrl+1...4 switches workspace;
   - Win+R opens the Terminal;
   - a Win tap on its own toggles the notification panel.
   - Print Screen (hotkey 28) opens the screenshot tool; Shift+Print Screen (30) saves the whole screen and Alt+Print Screen (29) the window under the pointer (or the focused window) at once (`graphics/ui/hotkeys.c`).
3. Trackpad gestures (`g_gesture`): swipe up opens the launcher, swipe down closes the launcher and panels, swipe left/right moves to the next/previous workspace.
4. Mouse. While the screenshot tool is open (`g_ss_on`), the pointer and buttons go only to `ss_mouse()` and every key only to `ss_key()`.
   - The packet is read with IRQs off and forwarded to user-space as `ux_mouse_push()`.
   - If DOOM holds the mouse, deltas go to `doom_mdx/mdy` and nothing else sees them.
   - Optional acceleration (`g_mouse_accel`) applies.
   - A press calls `on_click()`.
   - A release finishes desktop drags, file drags (`fx_drop`, or a drop onto the desktop), window snap, and resize.
   - Dragging a title bar moves the window and computes `g_snap` from screen edges and corners.
   - Dragging the bottom-right 18-px grip resizes the window (minimum 240x160).
   - Right-click opens a context menu (see the table above).
   - The scroll wheel scrolls whichever of Editor, Files, Terminal scrollback, or the Browser page has focus.
5. Keyboard. Keys are popped with `kpop()` and routed in priority order: rename modal to Alt+Tab (`g_alttab` list) to launcher search (`launcher_key`) to Wi-Fi password entry to popovers (Esc closes them) to the focused app's key handler.

   For the Terminal: Ctrl+C/Z/D/\ become 3, 26, 4 and 28. If a foreground job owns the terminal (`g_tty_fg`), the key goes to `tty_key()`. Otherwise Ctrl+C prints `^C` and a new prompt, and other keys go to `term_key()`.
6. Timers and redraw triggers. Snake steps every 8 ticks and Flappy every 3. A redraw is also requested when the second changes, and every 50 ticks for the caret blink.

`on_click()` resolves layers top-down:

1. properties dialog to image viewer to rename modal to launcher to quick settings to notification panel to clock popover to context menu;
2. top bar to Wi-Fi panel to dock;
3. windows in z order: caption buttons, resize grip, title-bar drag, then `app_click(i,px,py)`;
4. desktop items to rubber-band marquee (`g_marq`).

---

## 11.5 Live terminal mode vs. desktop terminal

Both modes share one text grid in shell/terminal.c:

- `TCH[TRMAX=72][TCMAX=240]` (characters), foreground colours in `TCO` and background colours in `TBG` (0 means the default background; `VT_BGSET` marks a set one). The grid was 40 x 120 before v8.97, which left most of a 1920 x 1080 screen empty;
- the cursor at `tcx,tcy`;
- a 400-line scrollback, `TSB[TSB_N]`;
- the line editor, `tline[256]` (254 typed characters; it was 128 before v8.95, which cut long commands short).

`term_putc()` bumps `g_term_gen`, which is how live mode notices new output. When `g_capture` is set, output goes into `g_cap[8192]` instead of the screen. Pipes and `$(...)` use this.

| | Live (`ST_LIVE`) | Desktop window (`W_TERM`) |
|---|---|---|
| Init | `live_init()`: `g_live=1`, AA font, `ux_core_start("root")`, `live_geom()` (grid = whole screen at 8x17 px cells), `live_banner()` ("To boot setup, type `noovexos_install`") | `term_init()`: banner "NoovexOS 64 nsh shell", grid from window size (`term_geom`) |
| User | root (uid 0) | the logged-in desktop user |
| Draw | `live_present()`: full repaint only when `g_lfull`; otherwise compares every row with the shadow copies `g_lsh/g_lsc/g_lsb` (characters, colours, backgrounds), repaints changed rows (`live_row`), blits only the changed band, and redraws just the cursor cell. A cell with a background colour gets a filled rectangle; its foreground is drawn as is instead of through the theme's `tmap` | `term_draw()`: glass background, theme colours, gradient accent line, `user@host: cwd` title, scrollback rendering, caret style (`g_tcur` block/bar/underline), status bar with uid dot, path and grid size |
| Input | `live_input()` sends keys through the same Ctrl mapping to `tty_key`/`term_key`. Mouse movement is discarded unless a program has `/dev/input/mice` open; then it moves `mx`/`my` like the desktop (the VMware/QEMU absolute pointer computes its deltas from them) and pushes movements and buttons with `ux_mouse_push` (v10.07). While `/dev/fb0` is open `live_present()` draws nothing and the cursor does not blink | `process_input()` (above) |

The two glyph paths differ:

- AA glyphs. `tglyph()` alpha-blends from `fontaa[95][112]` (DejaVu Sans Mono, 8x14).
- Pixel glyphs. With `g_tfont=0` ("Pixel"), it uses `dchar()` with the 1-bpp `FONT814[95][14]` font.

`term_key()` handles one keystroke. If a Wi-Fi password prompt is active (`g_pwprompt`), it masks the input with `*` and feeds it to `wpa_connect_saved`/`wpa_do_join`. Otherwise Enter logs the line (`klog2("cmd: ",...)`), passes it to `sh_run_line()`, and prints a new prompt unless a job took the TTY.

`term_prompt()` prints `user@host:cwd$`, or `#` for root.

Output from programs does not go through `term_putc` but through the escape-sequence interpreter `vt_out` (section 11.12). The shell's own output and the echo of typed characters still use `term_putc`.

---

## 11.6 Setup (OOBE) and login (`core/init/setup.c`)

The path to the desktop runs through two screens:

1. Setup. In live mode, `noovexos_install` (root only) calls `ip_open(ST_LIVE)` and enters `ST_INSTALL`. The installer's final action (`installer.c:482`, `id==6`) calls `setup_begin()`, which sets `g_state=ST_SETUP`. Boot-menu choices also go straight to `ST_SETUP`:
   - `RA_CONTINUE`;
   - `RA_SAFEMODE`, which also sets `g_safemode`;
   - `RA_CMDPROMPT`, which also sets `g_rec_openterm=1` so the Terminal opens after login.
2. Login. Finishing setup sets `g_state=ST_LOGIN`. On the login screen, Enter or the login button sets `g_state=ST_DESKTOP`. `login_input()` collects `login_pass` but does not compare it with anything in this code. The power-off, restart and sleep icons call `power_off/reboot/sleep`.

Setup is seven pages (`NSU 7`, titles in `SU_TITLE[]`):

1. Choose your language, 6 entries in `SU_LANGS`;
2. Keyboard layout, 6 entries in `SU_KBDS` (US, Swedish, German, French, Spanish, UK); since v10.18 the choice sets `g_kbd_layout` in `drivers/input/ps2/ps2.c`, is kept in `/etc/nvx-setup-done`, and can be changed later in Settings > Language & region, where the key repeat speed is set too;
3. Select your timezone, 7 entries in `SU_TZS`;
4. Make it yours, user name, accent and theme;
5. Sign in with Noovex, sign in, create an account, or skip; the forms call `nvx_signin`/`nvx_register`;
6. Where your files live, cloud or local;
7. You are all set.

`su_apply()` commits the choices:

- copies the name into `g_user` (default `"user"`);
- sets the accent and theme;
- enables `g_cloud_fs` only if "Cloud" was chosen and sign-in succeeded;
- rebuilds the background;
- saves settings to the account;
- if cloud is on, pulls cloud files with `nvx_files_pull()`.

---

## 11.7 Applications (`apps/`)

`draw_app(i)` and `app_click(i,...)` in `apps/system/logs.c` dispatch drawing and clicks to each app.

| Window | Source | Key functions | What it does |
|---|---|---|---|
| Terminal | shell/* | `term_draw`, `term_key` | nsh (section 11.8) |
| Files | `file_explorer.c` (+ `files_classic.c`) | `files_draw`, `fx_draw`, `fx_click`, `files_key`, `fx_build`, `fx_open_file` | Explorer over the real Unix tree (`DT[]` dirs + `FS[]` files) |
| Editor | `editor.c` | `editor_open`, `editor_open_buf`, `ed_load_usb`, `editor_key`, `editor_save`, `editor_run` | 200x200-char text editor with Save/New/Run |
| Calculator | `calculator.c` | `calc_press`, `calc_apply` | 64-bit integer four-function calculator; 20 buttons `CBL[]`, `<` = backspace, divide-by-zero sets `calc_err` |
| Browser | `browser.c` | `browser_go`, `web_load_html`, `web_paint`, `browser_draw`, `browser_click`, `browser_key`, `browser_tick` | front end for the web engine (Part 13) |
| DOOM | `doom.c` | `doom_init`, `doom_draw`, `DG_*` hooks | doomgeneric + FreeDoom |
| Settings | `settings.c` | `settings_draw`, `settings_click`, `settings_title_click`, `settings_key`, `nvx_settings_save/load` | 9 categories, search |
| Logs | `logs.c` | `logs_draw` | live kernel info and a scrolling hexdump |
| Terminal Finder | `logs.c` | `tfind_draw`, `TF_CMD[]` | cheat-sheet of hidden developer commands |
| Presentation | `files_classic.c` | `pptx_load`, `pptx_draw`, `pptx_key`, `pptx_click` | `.pptx` viewer |
| Clock, Calendar, Snake, Flappy Bird, neofetch, Paint | `small_apps.c` | `clock_draw`, `calendar_draw/cal_key`, `snake_*`, `flappy_*`, `neofetch_draw`, `paint_*` | small utilities and games |
| Welcome, About | `file_explorer.c` | `welcome_draw`, `about_draw` | static pages |

### Browser (`apps/internet/browser.c`, 698 lines)

Since v9.08 the Browser window is a front end for the web engine in `web/`, which is described in Part 13. `apps/internet/browser.c` keeps the window, the toolbar (back, reload, bookmark star, address bar, Go, Save, Home, Find, Cookies), up to 6 tabs, bookmarks and history (saved with `bm_save_fs` and `hist_save_fs`, history also pulled from the account by `nvx_hist_fetch`), downloads, the image viewer and the about: pages (`about:blank`, `about:js`, `about:cookies`, `about:console`, `about:history`, `about:bookmarks`). It decodes images for the engine (`nwk_image`: PNG, baseline JPEG, WebP, GIF, BMP, ICO) and routes mouse, wheel and keys into it (section 13.11).

`browser_go(push)` loads the page in `g_url`. Input that is not URL-like becomes a DuckDuckGo query; a URL without a scheme tries HTTPS first and falls back to HTTP; the back stack holds 16 entries. Network errors show an error page with the reason. The engine and its HTTP client are described in sections 13.2 and 13.9.

Account sync. The same file contains cloud-filesystem sync with the Noovex account: `nvx_file_put()` (base64 upload) and `nvx_files_pull()`.

### Files

There are two implementations.

`file_explorer.c` (header "v8.95") browses the directory tree.

- This PC (`fx_mode`), modelled on Windows 11: an address bar with "This PC", "Search This PC", a command bar (Properties, Open, Refresh, Open Settings), a "Devices and drives" group with one tile per volume (drive icon, name with drive letter, a usage bar that turns red above 90 percent, "X free of Y"), selection on click and open on double-click, the drives listed under This PC in the side bar, an item count in the status bar, and a Properties dialog (General tab: label, type, file system, location, used, free and capacity in bytes and short form, a used/free ring chart). The volume list comes from `vol_scan()` in `fs/vfs/volumes.c`: C: is the running system (NoovexOS on disk or the live RAM disk); then every partition of every disk (`gdisk_read_on()` reads another disk without changing the current one) and of the USB stick, in MBR (with logical partitions) or GPT (EFI, Microsoft reserved, recovery, BIOS boot and swap types and entries with the hidden or no-drive-letter attribute are left out). Each partition is identified as FAT12/16/32, NTFS, exFAT, ext2/3/4 or NVX4 and gets its label (FAT root-directory label or boot-sector label, NTFS `$Volume` name, exFAT label entry, ext volume name) and free space (FAT32 FSInfo or a count of free FAT entries up to a 16 MB FAT, the NTFS `$Bitmap` data runs, the exFAT allocation bitmap, the ext superblock). Letters follow from C: in that order. The list is rebuilt on Refresh, when the USB stick changes and when the USB device list changes; a USB stick that was plugged in before boot is set up on the first scan, skipping ports that already belong to a keyboard, mouse, camera or printer (`msd_autoinit_safe`). Double-clicking C: opens the folder tree, a FAT USB stick opens the USB view and other drives open Properties. Win+E opens File Explorer; the `explorer`, `files` and `thispc` commands do the same from the kernel console.
- Views: browse mode, list and grid views, sort and filter-as-you-type (`fx_filter`), back/forward/up history (`fx_hist`), and a preview pane (`fx_preview`).
- Clipboard: copy/cut/paste (`fx_clip`, `fx_paste`).
- Rename and new items: `fx_start_input` modes 0-3 cover renaming a file or folder and creating a new folder or file.
- Opening (`fx_open_file`):
  - `.pptx` to Presentation;
  - images to full-screen viewer (`g_imgview`);
  - HTML to `browser_show_html()`;
  - archives, binaries and ELF files to preview pane only ("Program - run it from the Terminal");
  - anything else to Editor. A `.nvx` file is also run immediately (`editor_run()`).
- Trash:
  - `fx_trash()` creates a hidden `/.trash` directory (mode `040700`, owned by the user).
  - Deleting outside the Trash moves the item there (`fx_trash_file`/`fx_trash_folder`). The item gets a unique name, and a record holding kind (`'F'`/`'D'`), new name, original directory and original name is appended to `/.trash/.trashinfo` (`fx_info_add`).
  - `fx_restore_sel()` looks the item up in that record and restores it. If the original folder is gone it is recreated (`fx_mkpath`); if no record exists, the item goes to `/home`.
  - Inside the Trash, Delete removes the item permanently (`fx_delete_file`). Deleting a non-empty folder there first asks for confirmation (`fx_confirm`).
  - `fx_empty_trash()` deletes everything with `fx_rmtree()`.
  - All deletions check `perm_file(k,2)`.

`files_classic.c` holds the older Windows-style tree and the shared viewers.

- Classic tree: `FTREE[]` ("Local Disk (C:)", System, Programs, Users, Documents, Downloads ...), used with `g_files_tree`. USB mode (`g_files_usb`) is a separate view.
- ZIP: `zip_open()` finds the end-of-central-directory record (`PK\5\6`) and lists up to `ZIP_MAXE=128` entries. `zip_extract()` supports stored (method 0) and deflate (method 8) entries; deflate goes through `pz_inflate_raw`.
- Images: `ensure_img()` decodes PNG (`png_decode` into the 64 MiB `PNG_RAW` scratch area), JPEG (`jpeg_decode`) or WebP (`webp_decode`) into `viewbuf` (6 MiB). `blit_img_fit()` scales to fit with the aspect ratio kept, and `img_viewer_draw()` shows the result as a dimmed modal.
- File typing: `file_type_name()` gives a human-readable type. `file_kind()` classifies by name (0 text, 1 image, 2 HTML, 3 binary, 4 archive). `fkind()` checks magic bytes first (PNG, RIFF/WEBP, JPEG, `PK\3\4`).
- PPTX: `pptx_load()` unzips the deck, extracts the `<a:t>` text of up to 48 slides, and places up to 4 images per slide (`pptx_decode_cur`, `pptx_blit`). The kernel's FS ships `demo.pptx` (`assets/demo_pptx.h`, written by `fs_init()` in core/syscall.c). `assets/nyc_pptx.h` (~898 KB) exists, but nothing in the tree includes it.

### Editor and the `.nvx` app format

The editor keeps text in `ED[200][200]` and `ed_ll[]`.

Look (since v10.09, `editor_draw`): a 48 px header bar in the style of Settings and Task Manager with rounded New (+), Save and Run buttons on the left, the file name in bold in the middle with a grey line under it ("Modified" while there are unsaved changes, otherwise the folder, "Not saved yet" or "Python IDE"), and round minimize, maximize and close buttons on the right. Below it a line-number gutter (the current number dark), a light blue current-line band, an anti-aliased monospace font (`mono_glyph` in shell/commands.c, 8x14 glyphs, 20 px lines), a 2 px blinking caret in the accent colour and an overlay scroll bar. A 28 px status bar shows "Ln X, Col Y" on the left and the language (Python, JavaScript or Plain Text), the line count and "UTF-8" on the right. Run is shown only for `.js` and `.nvx` files and in the Python IDE. The output panel is light grey with a bold "Output" heading and a round close button. Clicks in the header go through `editor_title_click` (hook in graphics/ui/input.c), the rest through `editor_click` (apps/system/logs.c); a click in the text puts the cursor there.

- Keys: arrow keys arrive as codes 17-20. Ctrl+C/X/V copy, cut and paste the current line through `g_clip[1024]`.
- Save: `editor_save()` writes back to the `FS` slot through `fs_wr` and calls `fs_sync`, or "Not enough memory to save" is shown.
- Run: `editor_run()` joins the buffer and calls `js_run(ed_src,n,ed_run_cb)` with `js_console` set, so `console.log`, `console.info`, `console.warn` and `console.error` print their arguments separated by spaces and end the line (elsewhere `console.*` is still ignored). Output goes to an output panel covering 38% of the editor height. If nothing is printed, the pane shows "(no output)" or "[program error]".

There is no separate `.nvx` binary format in this tree. A `.nvx` file is a source text file for the built-in interpreter (section 11.9) that is auto-run when opened from Files. The default `hello.nvx` (core/syscall.c `fs_init`) is C-flavoured:

```c
/* NoovexOS program - edit and press Run,
   or save as .nvx to make an app. */
int main(){
    printf("Hello from NoovexOS!\n");
    for(int i=1;i<=5;i++){ printf("%d squared is %d\n", i, i*i); }
    return 0;
}
```

This works because `js.h` accepts C type keywords as declarations (`js_isctype`), hoists `type name(...)` functions (`js_hoist`), implements `printf` with `%d %i %u %s %c %%`, and calls `main()` automatically after the top-level code (`js_run`).

### DOOM (`apps/games/doom.c`)

The game is linked as a blob (`doomgeneric_Create/Tick`). The kernel implements the platform hooks `DG_DrawFrame` (copies the 640x400 `DG_ScreenBuffer` into `SCENE`), `DG_GetKey`, `DG_GetMouse`, `DG_GetTicksMs` (`g_ticks*10`) and `DG_SleepMs`.

Startup. `doom_init()`:

- allocates a 24 MiB heap;
- tries `DOOM2.WAD`, `DOOM.WAD`, `FREEDOOM.WAD` and `FREEDOO1.WAD` on the FAT disk, reading into a 32 MiB buffer (`g_doom_src` records where the WAD came from);
- otherwise takes the WAD from the boot module recorded at physical `0x5010/0x5018` if it is larger than 1 MB;
- otherwise uses the shareware `DOOM1.WAD` built into the kernel (`_binary_doom1_wad_start/_end`, from `assets/doom1.wad`);
- registers the WAD under its own lower-case name (`doom2.wad`, `doom1.wad`, ...) so the game mode is detected from the name.

The `doom` terminal command switches from the text console to the desktop if needed, opens the window and prints the controls.

Error handling. `I_Error`/`exit` unwind back to the desktop through `doom_setjmp`/`doom_longjmp` (`doom_exit_cb`), and the window then shows "Could not load the WAD".

Input.

- `sc_to_doomkey()` maps scan codes: WASD/arrows, Ctrl fires, E/Space use, Shift runs, Alt strafes, and 1-7 choose weapons.
- Clicking inside DOOM captures the mouse (`g_doom_mcap`); Esc releases it.

### Gears (`apps/graphics/gpu3d.cpp`, `apps/graphics/r3d/r3d.cpp`)

Added in v9.82 and written in C++: a 3D demo like glxgears, under Start > Drivers & Hardware, or `gears` (`gpu3d`, `glgears`) in the terminal. `apps/graphics/r3d/r3d.cpp` is a software 3D renderer: vector and matrix classes, meshes built at start (glxgears-style gears, a torus knot, a sphere, a cube, a floor), near-plane clipping, a triangle rasterizer with edge functions, a depth buffer, perspective-correct normals and positions, Blinn-Phong light with a sky/ground ambient term, rim light, fog, planar shadows and a wireframe mode. `apps/graphics/gpu3d.cpp` is the window: it renders into a frame buffer, shows a large FPS counter and an information bar, and hands the frame to the screen in one of three ways:

| Path | When | How |
|---|---|---|
| NVIDIA copy engine | `nvgsp all` has run (press G in the window, or run it in the terminal) | the frame and the screen buffer are mapped into the GPU address space page by page (`nvgsp_ce_map`), and the copy engine copies the window rectangle line by line (`nvgsp_ce_copy2d`, a pitched multi-line `LAUNCH_DMA` with a semaphore the CPU waits for); after three failed copies it falls back to the CPU |
| Intel GPU blitter | the Intel iGPU compositor is on (`g_gfx`) | the frame is rendered straight into a GPU surface (`gfx_app_surf`) and blitted into the window with `gq_copy` |
| CPU copy | otherwise, and for half resolution (2x scaled) | rows are copied into the scene buffer |

Keys: 1 gears, 2 3 4 other scenes (showcase, materials, 192 cubes), W wireframe, S shadows, H half resolution, Space pause, arrows and + - for the camera, R reset, G start the NVIDIA card. Speed: a gears frame at 996x586 takes 8.7 ms on one core of the build machine and 10.2 ms on the owner's desktop. Before v9.84 the window was only redrawn when something else changed on the screen unless the Intel compositor was on, which gave 2 frames a second on the NVIDIA desktop; now the main loop asks `gpu3d_live()` and draws a frame every refresh interval while Gears is open, not minimised and not paused. Since v9.86 those frames skip the desktop when they can (`gpu3d_fast`): if the Gears window is fully visible (on top, no menu, popup, drag, night light, taskbar or dock over it, pointer not over the picture) and was drawn normally less than 0.5 s ago with the same size and mode, only the 3D picture is rendered and put on the screen, by the NVIDIA 2D engine when step 17 passed (scaled 2x in half mode) or by the CPU (`present_rect`). Every 0.5 s a normal frame updates the information bar and the clock. The FPS counter is drawn into the frame itself. The frame rows are 64-byte aligned for the 2D engine. The information bar says "window frames: NVIDIA 2D engine" or "window frames: CPU". With `nvgsp all` done, the information bar also says "desktop: copy engine" when the whole screen goes through the copy engine (section 10.10.7).

### Settings (`apps/system/settings.c`)

The sidebar lists `SC_LBL[8]`. Each category holds the following:

| Category | Contents |
|---|---|
| System | show-seconds toggle, Restart, "Crash" (panic test), build integrity |
| Personalization | 6 wallpaper thumbnails (`wall_thumbs_build`), 5 accents, 5 cursors |
| Network | Ethernet NIC name and IP; Wi-Fi scan/join (shares `g_wifi_*` with the top-bar panel) |
| Display | refresh 30/60/120 Hz, panel LCD/OLED (`g_panel_oled`), pointer acceleration Off/Low/High, FPS overlay |
| Battery | `bat_pct`, charging state |
| Storage | system disk (`gdisk_name`, size, NVMe 4K), data partition, install |
| Terminal | 6 `TTH` themes, transparency, blur, font Smooth/Pixel, cursor Block/Bar/Underline, status bar |
| About | version (`g_buildsig.version`), CPU brand, RAM, display mode, kernel size |

The window opens at up to 1000 x 800, sized to the screen and centred above the taskbar. Since v10.01 it is drawn in the style of GNOME Settings (libadwaita) and does not use `win_frame`: `settings_draw` draws its own shadow, rounded border and header bar. The sidebar (260, 232 or 204 px wide, `ST_SIDE`) has the "Settings" title, a search entry, the nine categories with symbolic icons (`st_glyph`, 40-px rows, the selected row a gray pill with a bold label) and the user row at the bottom. The content pane has the page title centred in the header, a back chevron and three round window buttons (`st_winbtn`) at the same places as the frame buttons, so `win_ctl_hit` still works. Content is at most 720 px wide and centred. Pages are boxed lists (`st_card`: a bold group title above a white card with a 1-px border; `st_tiles`: 56-px rows with icon, title, subtitle and chevron). Switches (`st_toggle`) use the accent colour when on, buttons (`st_btn`) are 34 px tall with a bold label (accent, red-text or gray), the segmented control (`st_seg`) is a gray group with a white selected segment. All text uses the proportional font. When there is no battery, Power shows a status page (icon, bold title, dim text) instead of a card.

Clicks in the header zone are normally a window drag. `on_click` in `graphics/ui/input.c` first gives them to `settings_title_click`, which handles the back button, the search entry and the user row and returns 0 for everything else, so the rest of the header still drags the window.

Search: `g_st_q` holds the query. Clicking the entry focuses it, and while Settings has focus, keys go to `settings_key` (type to search, Backspace, Esc clears, Enter opens the first result). `st_search` compares a normalised form (lower case, letters and digits only, so "wifi" finds "Wi-Fi") and orders results: page titles first, then category titles, then descriptions.

### Small apps (`apps/accessories/small_apps.c`)

- Clock: an analog face drawn with the integer `isin`/`icos` and `clock_hand`.
- Task Manager: the older `sysmon` code in this file is no longer drawn; the window is `apps/system/taskmgr.c` (see "Task Manager and Settings").
- Calendar: `cal_dow`, `cal_dim`, month navigation with arrows or the buttons.
- Snake: a 21x15 grid with an xorshift RNG; step speed is set in `process_input`.
- Flappy Bird.
- neofetch: a graphical page that is separate from the text `neofetch()` in terminal.c.
- Paint: a 540x360 `paint_buf` with a 12-colour `PPAL`, three brush radii (2/4/8) and Clear. Ctrl+V (`paint_paste`) scales the image on the clipboard (`g_clip_img`, for example the last screenshot) into the canvas.

### Screenshot tool (`apps/system/screenshot.c`)

| Part | What it does |
|---|---|
| Opening | `ss_open(mode,now)`: mode 0 area, 1 screen, 2 window. With `now=1` (Shift or Alt with Print Screen) it captures on the next frame without showing anything. |
| Drawing | `ss_draw()` runs at the end of `compose_build`, after the toasts. It dims the screen, leaves the chosen area clear with a white 2 px frame, corner handles and a size label (or the window under the pointer with a 3 px accent frame), and draws a dark pill at the bottom with Area, Screen and Window, a round capture button and a close button, plus a hint pill above it. |
| Input | `ss_mouse`: drag to select an area (a second drag replaces it), click a window in window mode, click the pill buttons; right-click closes. `ss_key`: Esc closes, Enter or Space captures, `a`/`s`/`w` switch the mode. |
| Capture | When a capture is pending, `ss_draw()` waits for queued GPU drawing (`gfx_sync`) and reads the finished frame from `SCENE` instead of drawing the overlay, so the tool itself is never in the picture. |
| PNG | `png_encode()`: 8-bit RGB, per row the Sub or Up filter (whichever has the smaller sum), zlib with one fixed-Huffman deflate block, LZ77 matches found with a 32 KiB window and hash chains (3-byte hash, 32768 heads, at most 48 candidates per position, plus a direct check for runs of the previous byte), Adler-32 and CRC-32. A 1280 x 800 desktop gives about 0.2-0.7 MB, 1.2 to 1.7 times what an optimising encoder makes. |
| Saving | `/home/Pictures/Screenshot-YYYY-MM-DD-HHMMSS.png` (the folder is created if needed; the file belongs to the current user), written through the file system and synced at once; the log line is `screenshot: WxH, N KB, path`. The pixels are also copied to `g_clip_img` (`g_clip_iw` x `g_clip_ih`), and a toast says "Screenshot saved to Pictures and copied". |
| Shell | `screenshot` or `scrot` with `screen`, `window` (the window under the pointer, or the focused window), `area` (opens the tool) or `last` (prints the last file name). |

### Logs and Terminal Finder (`apps/system/logs.c`)

`logs_draw()` shows the kernel base/size, DMA arena, `CR3`, kernel `RSP`, tick count, and a live 8-row hexdump of memory at `0x00100000`. The dump offset advances with `g_ticks/30`.

`tfind_draw()` lists the `TF_CMD[]` pairs: wifi init/scan/wpa, wifi rx, wifi cryptotest, sig, crypto, run, spawn, coreutils, chmod, su/whoami/id, env/export, `ls /dev`, `cat /dev/random`, fatls/fatcat, gdisk/install, h2c/efuse/macinit.

---

## 11.8 The shell (`nsh`)

### Line processing pipeline

```
term_key('\n') -> sh_run_line(line)
   alias NAME='val' | alias            -> al_set / list   (32 aliases)
   first word is an alias              -> textual replacement
   NAME=value (identifier before '=')  -> shv_set(NAME, sh_expand(value))   (64 shell vars)
   export NAME=value                   -> same as NAME=value (recurses on ln+7)
   otherwise                           -> run_cmd(sh_expand(line))
run_cmd(line)
   if(!g_capture && jc_launch(line)) return;      <- user-space programs (core/proc/jobs.c)
   cut '>' / '>>' outfile and '<' infile (whole line), split on '|' (<=8 stages)
   per stage: g_pipein=feed; capture unless last on-screen stage;
              run_filter(cmd,arg) if piped input, else run_cmd_one(stage)
   if outfile: vfs_open(O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC)) + vfs_write, "wrote/appended N bytes to F"
run_cmd_one(s): cmd = first word (<=23 chars), arg = rest
   jc_builtin(cmd,arg) -> jobs fg bg kill ps umask wait
   else the big if/else-if chain of seq(cmd,"...")
```

Important properties:

- `$` expansion (`sh_expand`) covers `~`, `$?`, `$((arith))` (`sh_arith`: `+ - * / %` with precedence, integers), `$(command)` (`sh_capture` runs the command with `g_capture=1`), `${NAME}` and `$NAME`. Names are looked up only in the shell-variable table `shv_*`, not in the environment table `g_env[]`.
- Redirection in built-ins.
  - `>` and `>>` cut the line at the operator and write the captured output through VFS. `>>` appends (since v8.95; before, it truncated like `>`).
  - `< file` feeds the first stage from `fat_read_file()`, which reads the FAT disk.
  - `run_filter()` only acts when there is piped input. It provides `grep PAT` (substring per line), `wc` (lines/words/chars), `head [N]` (default 10) and `cat` (pass-through).
- Programs vs. built-ins. `jc_launch()` runs first. If any `|`-stage names a program in `JS_BIN[]` (or an ELF file in the FS), the whole leading run of program stages is started as ring-3 jobs with real pipes, `< > >> 2> 2>> 2>&1` per stage (written apart, `> file`, or attached, `>file`, `>>file`, `<file`, `2>file`; the attached `>>`, `<` and `2>` forms were not recognised before v8.95 and ended up as program arguments), and `&` for background. Any trailing built-in stages are fed from a collector pipe (`g_coll_*`, at most 64 KB). `JS_BIN[]` includes the nvxbox applets `ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink true false tee sort cut tr uniq seq basename dirname rev tac nl fold paste comm cmp md5sum sha256sum base64 find yes printenv`. It also includes `sqlite3`, `gzip/gunzip/zcat`, `bzip2/bunzip2/bzcat`, `xz/unxz/xzcat/lzma/unlzma/lzcat`, `zstd/unzstd/zstdcat`, and the demos `count spin sigdemo upper greet pipetest libc8 mhello musltest smptest`.

  As a result, typing `ls`, `cat`, `echo`, `mkdir`, `pwd`, `date` and so on at the prompt starts the user-space applet. The same-named `seq()` branches in `run_cmd_one` run only when `jc_launch` is skipped (`g_capture` set, as inside `$(...)` or a pipeline stage being captured).
- Scripts. `sh`, `source`, `.`, or a first word ending in `.sh` reads the file through `ux_read_file`, falling back to `fat_read_file`, and skips a `#!` line. `sh_run_script()` splits the text on newlines and `;` (at most 256 lines, 8 KB). `sh_run_block()` then implements `if ... then ... else ... fi`, `while ...; do ... done` (guarded at 1,000,000 iterations) and `for V in items; do ... done`. Conditions use `sh_truth()`:
  - `[ a OP b ]` or `test a OP b`, with `= != -eq -ne -lt -gt -le -ge`;
  - otherwise the condition runs as a command and is true when `$?==0` (`g_lastrc`).

  Block constructs exist only in scripts; the interactive path calls `sh_run_line` directly.

### Users, sudo and permissions

- `sudo_auth()` (commands.c):
  - uid 0 passes immediately;
  - a user who is not an admin (`ux_is_admin`) is refused;
  - within the grace window (`g_sudo_until`, 30000 ticks about 5 min at 100 Hz) the user passes;
  - otherwise the user's hash is read from `/etc/shadow`. If none is set, a warning is printed and the user passes. If one is set, the user gets three tries; `sh_readpw` echoes `*`, and the input is compared as the hex of the FNV-1a hash `nvx_pwhash`.
- `sudo CMD` temporarily sets `g_uid=0`, calls `run_cmd(CMD)`, and restores the uid. `sudo -k` clears the grace window.
- `need_root(what)` prints "permission denied - administrator rights needed (try: sudo ...)". It guards `install` and `noovexos_install`.
- `su [root|other]` sets `g_uid` to 0 or 1000 and updates `USER` with `env_set`. It asks for no password.
- `passwd` reads the new password twice. It stores it in `g_user_pw`, calls `ux_save_localpw()`, and clears the sudo grace window. An empty password removes it.
- File permissions: `perm_file(k,2)` (write) is checked by `cp`, the Files delete paths and `fx_delete_file`. `rm` goes through `ux_unlink()`. `kill` refuses other users' processes unless run as root (`g_kill_denied`).

### Environment variables

`sys/native/syscall.c` keeps `g_env[NENV=24]` entries, each with a 24-byte name and a 128-byte value.

- `env_init()` seeds `PATH=/bin HOME=/home PWD=/home USER=root SHELL=/bin/nsh TERM=xterm-256color XZ_DEFAULTS=--memlimit-compress=20MiB`.
- `env_set()` is called by `su`, `cd` (sets `PWD`), the `setuid` syscall (131), and login (`HOME`, `USER` in fs/unixfs.c).
- `env_pack()` serialises the table into a new process's `envp`.
- `env` prints the table.

The `export` built-in in `run_cmd_one` calls `env_set`. Note that an interactive `export X=1` never reaches it: `sh_run_line` strips `export ` first and stores `X` as a shell variable. `set` with no argument lists the shell variables.

### Complete command reference

The table lists every `seq(cmd,"...")` branch of `run_cmd_one()` (shell/commands.c, lines 327-1615) in source order, plus `jc_builtin()` (jobs.c) and `run_filter()`. Earlier branches win. Shadowed means an earlier branch with the same name always matches first. (applet) means `jc_launch` normally runs the nvxbox program of the same name instead (see above).

| Command (aliases) | What it does |
|---|---|
| `jobs` | List background and stopped jobs (`g_jobs2`) |
| `fg [%n]` / `bg [%n]` | Send SIGCONT (18) to the job's process group; `fg` gives it the TTY |
| `kill [-SIG\|-N] pid\|%job` | Send a signal (default 15; names HUP...TTOU), permission-checked |
| `ps` | Process table PID/PPID/PGID/STAT/USER/COMMAND (shadows the later `ps` branch) |
| `umask [ooo]` | Show or set `g_umask` |
| `wait` | Report how many jobs are still running |
| `grep P` / `wc` / `head [N]` / `cat` | Pipe filters, active only with piped input (`run_filter`) |
| `help` | Print the command overview |
| `fatinfo`, `mount` (applet) | Mount the IDE FAT disk and print FAT type, drive and geometry |
| `fatls` | List the FAT root directory |
| `crypto` | List the crypto suite; self-test SHA-512, SHA-384, AES-256 and CSPRNG |
| `install` | (root) Open the installer, returning to the desktop (`ip_open(ST_DESKTOP)`) |
| `noovexos_install` | (root) Open the installer from live mode (`ip_open(ST_LIVE)`) |
| `pkg info\|list\|l <f>` / `pkg install\|add\|i <f>` | Read a `.deb` (ar + tar.gz) or Alpine `.apk`; list files or extract to `/pkg/<name>/` |
| `nvidia`, `gpu` | NVIDIA probe: IDs, BARs, PMC_BOOT_0, VBIOS/BIT/DCB, active display heads, outputs |
| `gpumon` | Read EDID from each DDC port listed in the VBIOS output table |
| `gpuedid` | Experimental DDC EDID read (`nv_display_probe`) |
| `nvgsp vram\|boot\|objects\|all\|status\|log\|unload` | NVIDIA Turing through GSP-RM (section 10.10.6); the log is saved as `nvgsp-log.txt` |
| `gears`, `gpu3d`, `glgears` | open the Gears 3D window (C++ renderer) |
| `sig` | Print the build signature (magic, version, size, epoch, SHA-256, builder) and re-check integrity live |
| `spawn <ELF>` | `sys_spawn` + `sys_waitpid`; print the exit status |
| `dlmod <FILE.MOD>` | Load a PIE module, relocate it and call its entry (section 11.10) |
| `run <ELF>` | Load an ELF64 from USB or FAT (`rtw_read_table_file`) and run it with `elf_load_run` |
| `fatcat <NAME.EXT>` | Print a FAT-disk file (at most 8000 chars) |
| `clear` | `term_clear()` |
| `echo ...` (applet) | Print the arguments |
| `nvxlogin`, `signin` | No argument: open the OAuth authorize URL in the Browser. With `<code>`: exchange it at `/api/public/oauth/token`, then fetch `userinfo` |
| `bcstat`, `bufcache` | Disk buffer-cache hits, misses and hit rate |
| `df`, `statfs` | `ux_statfs("/")`: blocks, used, avail, inodes |
| `mounts` | `/proc/mounts` through `proc_gen` |
| `slabinfo` | `slab_stats()` |
| `passwd` | Set or remove the local password |
| `whoami` (applet) | Noovex account identity if signed in, otherwise Unix user |
| `nvxlogout`, `signout` | Clear the account token and identity |
| `nvxtest` | Probe the account API (`/api/public/register`, password-grant token) and print raw responses |
| `neofetch`, `fetch` | Open the graphical neofetch window |
| `ver`, `about` | Print the version string from `g_buildsig` |
| `browse URL`, `www URL` | Open the Browser at URL; from the live terminal it switches to the desktop first (section 13.11) |
| `desktop` | Switch from the live terminal to the desktop |
| `qjs [-e CODE] [FILE]` | QuickJS JavaScript interpreter (section 13.13) |
| `cpu`, `cpuid` | CPU brand and `EFER.LMA` |
| `time` / `date` (applet) | RTC time `hh:mm:ss` / date `yyyy-mm-dd` |
| `uptime` | Seconds since boot (`g_ticks/100`) |
| `pwd` (applet) | Current directory (`dt_path(g_dcwd)`) |
| `whoami` (second) | Shadowed |
| `id` (applet) | `uid=... gid=...` |
| `sudo [-k] CMD` | Run CMD as uid 0 after `sudo_auth()` |
| `sh`, `source`, `.`, `*.sh` | Run a shell script |
| `set` (no args) | List shell variables |
| `test`, `[` | Evaluate a condition into `$?` |
| `true` / `false` (applet) | `$?` = 0 / 1 |
| `su [user]` | Switch uid to 0 or 1000 without a password |
| `env` | Print the environment |
| `export N=V` | `env_set` (see note above) |
| `cd [dir\|~]` | Change directory; sets `PWD` |
| `ls [-l] [dir]` (applet) | List directory; `ls /dev` prints the device names |
| `chmod <octal> <f>` (applet) | Set file or directory mode bits |
| `mkdir` / `rmdir` / `touch` (applets) | Directory-tree operations in the cwd |
| `rm` / `cp` / `mv` (applets) | `ux_unlink` / slot copy with permission check / `k_rename` |
| `cat <f>` (applet) | `/proc` files through `proc_gen`, else file in cwd, else any dir |
| `edit <f>` | Open the FS file in the Editor |
| `pci` | List PCI `ven:dev class`, marking network devices |
| `mem` | Heap base, size, in-use, reserved; 1 MB alloc self-test |
| `rune` | Draw a 256x192 Rune-rendered texture and print `fib(20)` (section 11.9) |
| `usbdisk`, `usbls` | Enumerate a USB mass-storage device; list its FAT files |
| `usbcat <F>` / `usbwrite <F> <text>` | Read / write a file on the USB stick |
| `utest`, `userspace` | Userspace self-test stages 1-9 (paging, ring 3, syscalls, ELF, scheduler, fork) |
| `vmtest`, `vm`, `paging` | Frame allocator and paging report |
| `ring3`, `user`, `usertest` | Run a ring-3 test program |
| `sched`, `multitask` (`ps` shadowed) | Three cooperative ring-3 tasks |
| `fork`, `forktest` | fork() demo |
| `cores` (`cpu` shadowed) | Core count from MADT; cores online |
| `compositor`, `nova` | User-land compositor plus 2 app processes (`compositor_run`) |
| `wm`, `winproto` | Window-protocol demo: `wm_demo()` runs `user_wmapp`, reads `WM_CREATE` from queue 0 and blits the app's SHM pixels |
| `shmstat` | Shared-memory segments |
| `ipc`, `shm` | Two-process SHM + message demo |
| `preempt`, `multitask2` | Timer-preempted non-yielding processes |
| `smpsched` | Per-core run-queue stats |
| `smpwork`, `parallel` | Prime-counting jobs across extra cores |
| `smpstart`, `smp [on\|...]` | Bring APs online (`smp_enable`) and record the preference |
| `threads`, `thread` | Worker threads sharing memory |
| `mmap` (`mem` shadowed) | Run the `user_mem` program (mmap/brk/getpid) |
| `posix`, `fdtest` | Run the `user_posix` program (fd layer) |
| `app`, `gui` | Run the graphical ring-3 `user_app` |
| `userelf`, `exec` (`run` shadowed) | Run the embedded `user_demo` ELF |
| `battery`, `bat` | `battery_cmd()` (ACPI `_BST`/`_BIF`) when ACPI batteries exist and arg not equal to `e...` |
| `brightness`, `backlight [up\|down\|N]` | `_BCM` / Intel PWM backlight |
| `audio`, `sound [vol N\|tone HZ]` | HDA/AC97 info, volume, 1 s tone |
| `acpi [ls P\|eval P args\|ec]` | AML namespace browse/evaluate, EC register dump |
| `timers` / `timertest` | Clock source report / 1 s accuracy test |
| `ec` (+ fallback `battery`/`bat`) | Raw EC (0x62/0x66) battery read |
| `usbrm <F>` | Delete a file on the USB stick |
| `usb`, `xhci` | Rescan xHCI, then run `usbinfo` |
| `wifi [sub]` | RTL8821CE bring-up: `init efuse allbuilt macinit/trx txbuf fw h2c bb/agc/rf rings load dump scan rx join open, `pr... [ch]` (channel 1-13), wpa cryptotest dhcp eth` (parsed from the first one or two letters) |
| `net` | Bring up the wired NIC; print name, IP |
| `dhcp` | `dhcp_renew()`; print IP/mask/GW/DNS |
| `ping A.B.C.D` | ICMP echo |
| `dns host` | Resolve a name |
| `http`, `https`, `wget`, `get host[/path]` | Fetch and print the response |
| `disks`, `disk`, `lsblk` | NVMe/AHCI/ATA disks and partitions |
| `netlog` | Last 18 network log lines |
| `colors` | 8-colour swatch |
| `settings` | Open Settings |
| `lua` | Print Lua usage (see section 11.9 for the argument path) |
| `presentation`, `slides`, `pptx` | Open the first `.pptx` in FS |
| `reboot`, `restart` | `power_reboot()` |
| `shutdown`, `poweroff`, `halt` | `power_off()` |
| `suspend` | `power_sleep()` (ACPI S1) |
| `kcheck` | Kernel immutable-region check (`kcheck_run`, installer.c) |
| `memmap` | Firmware memory map (UEFI loader only) |
| `tpinfo` | Touchpad report (`tpinfo_run(0)`) |
| `tpinfo scan` | Re-probe, including every controller x address x descriptor register (`tpinfo_run(1)`) |
| `touchpad [tap on/off] [speed 1-10] [scroll natural/classic] [zones on/off] [typing on/off]` | Touchpad settings (`g_tp_*` in `tp_gesture.c`); with no argument, prints them |
| `tpdump`, `tpinfo dump` | Last report descriptor read from a touchpad, in hex, with read check, bus speed and parser result (`tpinfo_dump`) |
| `memtest` | Heap-chain integrity walk plus memory test |
| `usbinfo` | xHCI controllers and ports |
| `mousetest` | Live mouse event display until a key is pressed |
| `acpi` (second) | Shadowed |
| `ntfs info\|mount\|umount\|ls\|cat\|write\|mkdir\|rm\|mv\|help` | NTFS driver front end (fs/ntfsk.c) |
| `panic` | `panic(0,"Manual panic ...")` |
| anything else | `nsh: X: command not found`, `$?=1` |

---

## 11.9 Scripting languages

### JavaScript / C-script interpreter (`lib/js/js.c`)

This is a tree-walking interpreter that runs directly over tokens (there is no AST). Its limits are compile-time constants:

```c
#define JS_MAXTOK 40000   #define JS_ARENA 393216   #define JS_MAXG 512   #define JS_MAXL 512
#define JS_MAXDEPTH 96    #define JS_STEPMAX 8000000
#define JS_MAXARR 160 /* x JS_ARRCAP 256 */   #define JS_MAXOBJ 80 /* x JS_OBJCAP 28 keys */
```

| Area | Supported |
|---|---|
| Values | `JV_UNDEF NULL NUM BOOL STR FUNC ARR OBJ`; numbers are `int` only (no floating point) |
| Statements | `var/let/const` and C type names (`int char long short float...`), `if/else`, `while`, `for(;;)`, blocks, `function` declarations/expressions, `return`, `break`, `continue`, recursion |
| Operators | `+ - * / %`, comparisons, `== != === !==`, `&& \|\|` (short-circuit), `!`, `?:`, `= += -= *= /=`, `++ --` |
| Builtins | `document.write/writeln` to `js_out`; `console.*` ignored; `Math.floor/ceil/round/abs/max/min/sqrt/pow/sign/trunc/hypot/random` (integer; `random` returns 0..999); `parseInt/parseFloat/Number/String`; `printf` (`%d %i %u %s %c %%`), `print`, `puts`; `alert/prompt` are no-ops |
| Strings | `.length .charAt .charCodeAt .codePointAt .at .substring .substr .slice .toUpperCase .toLowerCase .indexOf .lastIndexOf .includes .startsWith .endsWith .split .replace .replaceAll .trim .trimStart .trimEnd .repeat .padStart .padEnd .concat .toString` (`string_method`) |
| Arrays | `.length`, `push pop shift unshift indexOf includes join reverse slice concat` |
| Safety | `js_steps` > `JS_STEPMAX` or arena/table overflow sets `js_err` and stops the run |

Entry point:

```c
static void js_run(const char* src,int len,void (*out_cb)(const char*,int)){
    ... js_tokenize(src,len); js_hoist(); js_tp=0;
    while(cur()->t!=JT_EOF && !js_err){ ... exec_stmt(); ... }
    if(!js_err){ JVar* mf=js_find("main",4); if(mf && mf->val.t==JV_FUNC){ js_sig=0; call_func(mf->val,0,0); } }
}
```

Callers: the Editor's Run button (`ed_run_cb`, which also runs `.nvx` files opened from Files). The Browser used it for `<script>` blocks until v9.08; it now runs page scripts with QuickJS (section 13.8), and the `qjs` command runs JavaScript from the shell (section 13.13).

### Lua 5.5 (`third_party/lua/nvxlua.c`)

`nvx_lua_run(code)` runs a script in a fresh state:

1. `luaL_newstate()` and `luaL_openlibs()`;
2. registers the `nvx` table;
3. `luaL_dostring()`;
4. on error, appends `error: ...`;
5. closes the state and returns the captured output buffer `g_lua_out[16384]`. `lua_emit` is the print sink.

The `nvx` library maps to C functions in `graphics/ui/input.c`. Those functions draw into a centred canvas window with its own chrome (`lc_chrome`) directly on the framebuffer:

| Lua | C (`graphics/ui/input.c`) | Notes |
|---|---|---|
| `nvx.window(title,w,h)` | `nvxlc_window` | clamps to 120x80 ... (W-40)x(H-100); centres |
| `nvx.clear(c)` / `nvx.rect(x,y,w,h,c)` / `nvx.pixel(x,y,c)` / `nvx.line(...)` / `nvx.text(x,y,s,c)` | `nvxlc_*` | clipped to the canvas |
| `nvx.rgb(r,g,b)` | - | returns `r<<16\|g<<8\|b` |
| `nvx.present()` | `nvxlc_present` | redraws chrome, `blit_all()` |
| `nvx.width()/height()` | `nvxlc_w/h` | canvas size |
| `nvx.key()/waitkey()` | `kpop()` / blocking `kpop()` loop | |
| `nvx.mouse()` | `nvxlc_mouse` | x, y relative to canvas, buttons |
| `nvx.ticks()/sleep(ms)` | `g_ticks` / tick wait | 100 Hz granularity |

The shell branch:

```c
else if(cmd[0]=='l'&&cmd[1]=='u'&&cmd[2]=='a'&&(cmd[3]==' '||cmd[3]==0)){
    if(cmd[3]==0){ /* usage text */ }
    else { const char* out=nvx_lua_run(cmd+4); ... } }
```

Because `run_cmd_one` copies only the first word into `cmd[24]`, `cmd[3]` is always 0 for `lua ...`. As written, the shell therefore always prints the usage text and never calls `nvx_lua_run`; the code would have to use `arg` instead of `cmd+4`. `mem/pmm_vmm.c:9` declares `extern const char* nvx_lua_run(const char*)`.

### Rune (`lang/rune/`)

`lang/rune/rune_gfx.rn` is written in the user's Rune language. `build.sh` step 3a compiles it into an object, which is linked into the kernel as a blob (`rune_gfx.o` in `BLOBS`):

```
tools/rune build rune/rune_gfx.rn --freestanding --target=x86_64-none -o $B/rune_gfx.o
```

The C side is the two prototypes in `lang/rune/rune_gfx.h`: `rune_render(u32* buf,int w,int h,int t)` and `rune_fib(int n)`. The Rune source:

```rust
fn rune_render(buf: *u32, w: i32, h: i32, t: i32) -> void {
    let mut y: i32 = 0;
    while y < h { let mut x: i32 = 0;
        while x < w { let e: i32 = ((x ^ y) + t) & 255;
            *(buf + (y * w + x)) = ((e & 255) as u32 << 16) | (((e*2)&255) as u32 << 8) | ((200-e)&255) as u32;
            x = x + 1; }
        y = y + 1; }
    return;
}
fn rune_fib(n: i32) -> i32 { if n < 2 { return n; } return rune_fib(n - 1) + rune_fib(n - 2); }
```

(The colour expression is condensed here; the source computes `r,g,b` in separate `let` bindings.) The `rune` command renders the result into a 256x192 static buffer, `putpx`es it in the screen centre with the caption "Rendered by Rune (rune_gfx.rn)", and prints `fib(20)`.

### HexLang

There is no HexLang code in this repository. A case-insensitive search for "hexlang" in `*.c`/`*.h` under the repo root finds nothing. The 32-bit Noovex9 project has a `hexlang.h`, but it is not part of NoovexOS 64.

---

## 11.10 Kernel modules: `usr/mod/hello_mod.c` and the `.MOD` format

A `.MOD` file is a position-independent ELF64 shared object; `HELLO.MOD` has `e_type` = 3 (`ET_DYN`), x86-64, entry `0x1070`, 11 program headers. It imports kernel functions by name:

```c
extern int  nvx_puts(const char* s);  extern void nvx_putc(int c);  extern unsigned int nvx_ticks(void);
int module_main(void){ nvx_puts(MSG); nvx_putc('\n'); unsigned int t = nvx_ticks();
    nvx_puts("  (called kernel symbols by name after relocation)\n"); return (int)(t & 0xff); }
```

The loader is `nvx_dlload()` in `core/proc/elf.c`:

1. Check the `\x7FELF` magic, then compute the `[lo,hi)` span of all `PT_LOAD` segments.
2. Allocate one region with `dma_alloc` (identity-mapped and executable), zero it, and copy each segment to `base + (va - lo)`. `load_bias = base - lo`.
3. Parse `PT_DYNAMIC` for `DT_RELA/RELASZ/RELAENT/SYMTAB/STRTAB/SYMENT/JMPREL/PLTRELSZ`.
4. Apply relocations:
   - `R_X86_64_RELATIVE` (8) to `load_bias + addend`;
   - `R_X86_64_64`, `GLOB_DAT` and `JUMP_SLOT` (1/6/7), plus every `DT_JMPREL` entry to `ksym_find(name)`, or 0 if the name is unknown.
5. Set `mod->entry = load_bias + e_entry`. `nvx_dlcall()` calls the entry as `int (*)(void)`.

The exported kernel symbol table:

```c
static const nvx_ksym NVX_KSYMS[]={
    {"nvx_puts",(void*)ksym_puts}, {"nvx_putc",(void*)ksym_putc},
    {"nvx_ticks",(void*)ksym_ticks}, {"nvx_alloc",(void*)ksym_alloc}, {0,0} };
```

`dlmod FILE.MOD` reads the file from the USB stick or FAT disk (`rtw_read_table_file`) and loads it. It prints the load base and entry, the module's output, and "module returned N".

Separately, `build.sh` line 21 uses the word `MODS` for drivers that are compiled separately and then statically linked: e1000, rtl8139, rtl8169, virtio, virtio_net, virtio_blk, ac97, hda. These are not `.MOD` files.

---

## 11.11 Fonts and assets (`assets/`)

| File | Contents | Used by |
|---|---|---|
| `font.h` | `FONT814[95][14]`: 8x14 1-bpp ASCII 32-126, LSB-first | Pixel terminal font |
| `fontaa.h` | `fontaa[95][112]`: DejaVu Sans Mono anti-aliased 8x14 alpha atlas (`AA_CW 8`, `AA_CH 14`) | `dchar/dstr/dstr_ctr` (blended, fixed 8-px advance), `tglyph` (terminal), proportional text `pdch/pdstr` |
| `nvx_assets.h` | Wallpapers as baseline JPEG byte arrays: `wall_cobalt_jpg` (13153 B), `wall_wire_jpg`, `wall_crane_jpg`, plus 640x360 `wall_porsche_jpg`, `wall_field_jpg`, `wall_desert_jpg`; `wordmark_a` (448x184 8-bit coverage) | included by `apps/internet/browser.c:70`; decoded by `wall_decode()` |
| `demo_pptx.h` | `demo_pptx[8360]` | `fs_init()` to `demo.pptx` |
| `nyc_pptx.h` | `nyc_pptx[897654]` | not included anywhere |
| `installer/` | `inst_boot.bin` (512 B), `inst_stage2.bin`, `inst_boot.cfg` (`timeout=2`, `video=auto`, `kernel=/boot/kernel64.bin`) | linked as installer blobs (`inst_boot.o`, `inst_stage2.o`, `inst_cfg.o` in `BLOBS`) |

Proportional text works by measuring each glyph's ink bounds in `fontaa` once (`gmetrics()`: columns where alpha > 40). The results go into `g_gl[]` (left offset) and `g_gw[]` (width). `gadv()` advances by `width+1`; a blank glyph advances 4 px. `pdstr` draws the window titles in Settings, the terminal title bar, and similar UI text, and `dstr` draws everything that is monospaced.

---

## 11.12 Escape sequences, termios and full-screen programs

Since v8.97 the console understands the escape sequences of an xterm-style terminal, and programs can switch it to raw mode. Before, `jc_tty_out` dropped every escape sequence and carriage return, so colours vanished and full-screen programs could not draw.

### Output: `vt_out` (`shell/vt.c`)

`jc_tty_out` (every write by a program to the console, `vfile.dev` 5) calls `vt_out(s, n)`. It works directly on the grid (`TCH`, `TCO`, `TBG`, `tcx`, `tcy`) with a small state machine: normal text, ESC, CSI parameters (up to 16, with `?`, `>`, `=`, `<` marking private sequences), OSC strings (ignored up to BEL or ESC backslash), and character-set selections (ignored).

| Sequence | Meaning |
|---|---|
| CR, LF, BS, TAB, VT, FF | column 0; line feed (also column 0 while `OPOST|ONLCR` is set); left one column without erasing; next multiple of 8; line feed |
| `ESC [ n A/B/C/D` | cursor up/down/right/left, stopping at the scroll region when inside it |
| `ESC [ n E/F`, `G` or backquote, `d` | next/previous line, column, row |
| `ESC [ r ; c H` or `f` | cursor position (1-based) |
| `ESC [ n J` | erase below (0), above (1), all (2, 3) |
| `ESC [ n K` | erase to the end (0), start (1), whole line (2) |
| `ESC [ n L/M` | insert/delete lines inside the scroll region |
| `ESC [ n P/@/X` | delete/insert/erase characters |
| `ESC [ n S/T` | scroll up/down |
| `ESC [ n b` | repeat the last character |
| `ESC [ t ; b r` | scroll region (cursor goes home); no arguments resets it |
| `ESC [ s`, `ESC [ u`, `ESC 7`, `ESC 8` | save and restore the cursor (`ESC 7/8` also the colours) |
| `ESC [ ... m` | colours and attributes, below |
| `ESC [ 6 n` | cursor position report: `ESC [ row ; col R` is put into the input queue |
| `ESC [ 5 n`, `ESC [ c` | status report `ESC [ 0 n`, device attributes `ESC [ ? 1 ; 2 c` |
| `ESC [ ? 25 h/l` | show/hide the cursor (`g_vt_curhide`) |
| `ESC [ ? 1 h/l` | application cursor keys on/off (`g_vt_appcur`) |
| `ESC [ ? 7 h/l` | automatic wrap at the right margin on/off |
| `ESC [ ? 1049 h/l` (also 47, 1047) | switch to and from the alternate screen: the grid and cursor are saved in buffers allocated on first use, and restored on leaving |
| `ESC M`, `ESC D`, `ESC E` | reverse index (scrolls down at the top of the region), index, next line |
| `ESC c` | full reset and clear |

Colours (SGR): 0 reset, 1 bold (the 8 basic colours become their bright versions), 2 and 22 normal, 7 and 27 reverse on/off, 30-37 and 90-97 foreground, 40-47 and 100-107 background, 39 and 49 default, `38;5;n` and `48;5;n` from the 256-colour palette (16 base colours, the 6x6x6 cube, 24 greys), and `38;2;r;g;b` / `48;2;r;g;b` true colour. Erasing fills with the current background colour, as xterm does.

Wrapping is deferred: after a character is written in the last column the cursor stays there (`tcx == tcols`) until the next printable character, so a full-width line does not produce an extra blank line. Scrolling the whole screen keeps the scrollback (`term_scroll_up`); scrolling a smaller region or the alternate screen does not.

UTF-8 is decoded; since the console font has only ASCII glyphs, box-drawing characters become `-`, `|` or `+`, block elements `#`, quotes and dashes their ASCII forms, and other characters `?`.

### Input and termios

The console has one termios, `g_tios` (`struct vt_tios`, the same 36-byte layout as Linux's `struct termios` for `TCGETS`). `TCGETS` reads it, `TCSETS`, `TCSETSW` and `TCSETSF` replace it (`TCSETSF` and `TCFLSH` also discard typed input). `vt_tios_default` sets the usual cooked mode.

`tty_key` (`core/proc/jobs.c`) handles a key for the foreground job:

| Setting | Effect |
|---|---|
| `ISIG` | ^C, ^Z and ^\ send SIGINT, SIGTSTP and SIGQUIT to the foreground process group. Without `ISIG` they are passed to the program as bytes 3, 26 and 28 |
| `ICANON` set (cooked) | the line editor as before: typed characters are collected, Backspace erases, ^U erases the line, Enter or ^D hands the line over |
| `ICANON` clear (raw) | every key goes straight to the input queue: arrows as `ESC [ A..D` (or `ESC O A..D` with application cursor keys), Home/End `ESC [ H/F`, Page Up/Down `ESC [ 5~/6~`, Delete `ESC [ 3~`, Insert `ESC [ 2~`, Enter as CR (LF if `ICRNL` is set), Backspace as DEL (127), Ctrl+letter as its control code, Ctrl+[ as ESC |
| `ECHO` | typed characters are echoed (cooked: by the line editor; raw: printable bytes through `vt_out`). Clear it for password prompts |
| `ECHONL` | echo the newline even when `ECHO` is clear |
| `VMIN`, `VTIME` (raw) | `VMIN > 0`: `read` waits for input and returns what is there. `VMIN = 0, VTIME = 0`: returns at once, possibly 0 bytes. `VMIN = 0, VTIME = t`: waits up to t tenths of a second (the deadline is kept in the thread's `pollwake`) |
| `OPOST`, `ONLCR` | LF on output also returns to column 0 (the default); a program that clears `OPOST` must send CR LF itself, as full-screen programs do |

The keyboard drivers deliver the extra keys as codes 2 (Home), 5 (End), 6 (Page Up), 7 (Page Down), 14 (Delete) and 15 (Insert) besides the arrows 17-20 (PS/2 in `drivers/input/ps2/ps2.c`, USB in `drivers/usb/xhci.c`). While a job owns the terminal, Ctrl+letter is passed as `0x100 | code` so it cannot be confused with those key codes.

`TIOCGWINSZ` reports `trows` x `tcols` (for example 58 x 158 on a 1280 x 1024 screen in live mode).

### When a program ends

When the foreground job finishes or stops, `jc_tty_restore` resets the terminal: cooked mode with echo, default colours, cursor shown, scroll region and wrap reset, and the alternate screen left (with a full repaint). If the program left the cursor in the middle of a line that has text on it, the prompt starts on a new line. A program that crashes in raw mode therefore cannot leave the shell unusable.

### Full-screen programs

`kilo` (a small editor, `third_party/kilo`) is built as a port and is a real-world test: it uses raw mode, `VMIN = 0, VTIME = 1` reads, the cursor position report to find the window size when `TIOCGWINSZ` is not available, cursor hiding, colours for syntax highlighting and reverse video for the status bar. `featest tty` checks the sequences and modes above automatically (section 12.2).

`TERM` is `xterm-256color`. Programs that write escape sequences directly work. ncurses-based programs need a terminfo entry; the image has no terminfo database, so build ncurses with `--with-fallbacks=xterm-256color` to compile the entry in.



## Task Manager and Settings (v9.73)

Task Manager (`apps/system/taskmgr.c`) is drawn in the style of GNOME System Monitor since v10.03, like Settings: no `win_frame`, its own header bar with "End task" on the left (red when an app is selected), a view switcher in the middle (Processes, Resources, Devices, Services; icons only below 720 px) and three round window buttons at the frame button positions. Clicks in the title zone go to `sysmon_title_click` first (`graphics/ui/input.c`), so the switcher works there and the rest of the header still moves the window. Processes is one boxed list with column titles (Process Name, % CPU, Memory, Status), an Apps group and a Background group, and a summary line at the bottom. Resources has a list of CPU, Memory, GPU, Disk and Network with a small graph each, and a card with a 60-second graph (line plus fill, dotted grid, "60 seconds" and "now") and right-aligned details for the selected one. Devices is one boxed list of expander rows (coloured icon, name, count, chevron) whose details open inside the list; it scrolls with the mouse wheel and shows a scroll bar. Services shows each service with its description and a Running or Stopped pill. All text uses the proportional font (`tbold`, `tright`, `tclip` cuts long values with ".."). Resources samples once a second: CPU use is 100% minus the share of idle ticks counted by the scheduler on cores 1 and up (core 0 runs the desktop and is not sampled), memory is the kernel heap in use against installed memory, GPU busy is the GPU drawing plus screen copy time of the last frame against one refresh period. Devices builds its lists from the PCI scan, `igpu_info()`, the ACPI battery objects, `gdisk_*`, the USB device table and the sound driver; categories open on click and the list scrolls with the mouse wheel.

Settings (`apps/system/settings.c`) keeps the current page in `g_stpg`. Pages 0-8 are the category pages (tiles), higher numbers are sub-pages; `pg_parent()` gives each page its parent, which builds the path at the top and the back button. The previous per-category pages are drawn by `old_leaf()` and reused as sub-pages. `settings <page>` opens a page from the terminal.

## 11.13 Classic look (v10.23)

The classic style (`g_dstyle` 1, the default) draws 24-pixel title bars (`TITLEH`) with 16 by 14 buttons and a solid (80, 80, 80) desktop (`g_wallmode` 2, no vignette). `wm.c` has helpers for raised and sunken edges (`ui_bevel`), a 20-pixel menu bar (`ui_menubar`), a 16-pixel scroll bar (`ui_scrollbar`) and tabs (`ui_tabs`). `desktop.c` draws six small desktop icons (double-click opens Files, Terminal, Text Editor, Browser, Calculator or Settings), three quick launch buttons after the start button and two small CPU and memory graphs left of the tray (`sysmon_cpu_pct`, `sysmon_mem_pct`). The start menu has About, six categories with submenus, Settings, Help, Run (opens a Terminal) and Exit. The Terminal (`term_draw_classic`) shows `user@host: path` as its title, a File/Edit/View/Help menu bar, the text on black in a sunken box with a block cursor, and a scroll bar. System Monitor (`sysmon_classic`) has Processes, Performance, Storage, Network and Hardware tabs (hit ids 500 to 504), graphs in group boxes and a status bar. The browser frame uses the window face colour, raised buttons, a sunken white address field and a status bar.

v10.24: the Performance tab draws `sm_graph` (two filled series with a line on top, interpolated between the 60 one-second samples) with right-aligned legends (`sm_rtext`); kernel time is busy time minus the `user_ticks` delta; `heap_stat` returns used and total heap bytes in one walk. The classic prompt uses `term_tilde`, which replaces the home folder (`/root` for root, else `/home`) with `~`.

v10.25: `prop_draw`/`prop_click` show the classic Properties window for `g_prop_slot` (file), `g_prop_dir` (folder) or `g_prop_app` (window). Icon choices are in `g_ficon`, `g_dicon` and `g_aicon` (0 = default, else choice + 1), saved as `f<slot> <n>`, `d<dir> <n>` and `a<app> <n>` lines in `/etc/nvx-icons`; `icon_choice_draw` draws a choice, `item_icon_kind` returns the icon for a Files row, and `mini_icon`, `launch_icon` and `mini_icon_big` use the app choice. `ctx_draw_classic` draws right-click menus with 24-pixel rows. `settings_draw_classic` draws the Settings frame; `st_card`, `st_toggle`, `st_btn`, `st_seg`, `st_sep` and `st_tiles` have classic branches, so the pages themselves are unchanged.

## 11.14 Setup accounts, sign-in screen, lock and Settings > Accounts (v10.40)

Setup (`core/init/setup.c`) has ten pages (`SU_LANG` to `SU_DONE`); `SU_HOST`, `SU_ROOT` and `SU_USER` draw their fields with `acct_field` (label, then `[` black field `]` with `*` for passwords) inside the installer's `ip_box`. Enter moves to the next field and then continues; `su_check_page` validates (computer name, 4-character passwords typed twice, `acct_valid_name`). The user name follows the full name until it is edited (`su_suggest_user`). `su_finish` calls `acct_setup_primary` and `acct_login`; if the accounts cannot be written it goes back to the account page with the reason instead of finishing.

Sign-in. `installed_boot` runs `ux_core_start(0)` first, drops the live root session (`g_logged_in=0`) and shows `ST_LOGIN`. `login_present` lists `acct_list(...,1)` (accounts with uid 1000 to 59999, guest last and only when on) at the bottom left when there is more than one, or a Switch user button while locked. `lg_try` checks the password with `acct_check`; 5 failures start a 30 s wait (`lg_wait_until`); a correct password unlocks the same account or ends the current session (`acct_ui_end_session`) and calls `acct_login` for the new one, then `acct_ui_session_started` clears the Terminal and prints a new prompt. On the live system (`g_lg_installed` 0) Enter unlocks.

Lock and sessions (`core/auth/acctui.c`). `acct_ui_lock` (Win+L, launcher power button, lid close in `pm_lid_changed`, `lock`, Settings, and `acct_idle_tick` after `g_lock_min` minutes without input; an open video player counts as input) keeps the windows. `acct_ui_end_session` closes all windows, kills the account's processes, resets the browser profile (`browser_profile_switch`, `ck_profile_switch`), the Terminal (`term_session_reset`), the Files folder (`fx_session_reset`) and the `sudo` timestamp, then `acct_logout`. Settings keeps `guest=` and `lockmin=` in `/etc/nvx-setup-done`.

Dialogs. `acct_dlg_*` is one modal dialog (title, two text lines, up to five fields, an administrator check box, up to three buttons) drawn by `compose_build` after the other overlays and fed by `process_input` (keys) and `mouse_step` (clicks) while it is open; Tab and the arrows move, Enter presses the focused button or moves to the next field, Esc cancels. Settings > Accounts (`P_ACCOUNT`) uses ids 600 to 675 (`acct_ui_click`): 600 name, 601 password, 602 add, 603 guest, 604 lock, 605 sign out, 606 switch user, 610-613 lock time, 620+i administrator, 640+i password, 660+i remove.


## 11.15 Start menu submenus (v10.47)

Both start menus show categories that open a submenu. The categories and their built-in apps are in `LG_NAME` and `LG_BASE` (`graphics/ui/desktop.c`): Office, Internet, Media & Graphics, Development, Games, System, Accessories and Drivers & Hardware. `lgrp_refresh` adds the Linux programs from `/usr/share/applications` by their `Categories=` line (`xs_cat` in `apps/x11/xapps.c`): Office, WordProcessor, Spreadsheet and Presentation go to Office; Network, WebBrowser, Email, Chat and InstantMessaging to Internet; AudioVideo, Audio, Video, Graphics, Player and Music to Media & Graphics; Development, IDE and TextEditor to Development; Game to Games; System, Settings, Monitor and TerminalEmulator to System; everything else to Accessories. A category without apps is not shown. Each category holds up to 40 apps (16 before, and Linux programs were capped at 15 in total); up to 64 Linux programs are read.

| Menu | Category list | Submenu |
|---|---|---|
| Modern style (`g_dstyle` 0) | rows of 48 pixels with a coloured tile, the name and the number of apps (`LR_H`) | a panel to the right of the menu (`lsub_geom`) with the apps as a grid of 4 columns; it opens when the pointer is on a row (or with a click) and closes when the pointer leaves both |
| Classic (`g_dstyle` 1) | the category items between "About NoovexOS" and "Settings" (`CM_NC` is the number of shown categories) | the list to the right (`cm_sub_geom`); it uses two columns when one would be taller than the screen |

Search in the default menu is unchanged.

## 11.16 Sign-in screen and Face ID (v10.49)

The sign-in and lock screen (`login_present` in `core/init/setup.c`) follows the owner's chosen mockup. `lg_hills` paints the background (sky and three ellipses, scaled to the screen), the clock uses Inter Light 84 px, names and buttons Inter 13 to 40 px from `assets/uifont.c` (made by `tools/mkuifont.py` from the build machine's Inter fonts, drawn with `uf_draw`). The arrow button and Enter check the password as before (5 wrong passwords start a 30 s wait). The power icon at the bottom right opens Sleep, Restart and Shut down.

Face ID has three parts:

| Part | What it does |
|---|---|
| `/usr/bin/nvx-faceid` | A static Linux program built from `tools/faceid/nvx-faceid.cpp` with dlib (Boost license, `third_party/dlib`) by `tools/build_faceid.sh`. Models from dlib-models (CC0, `third_party/dlib-models`): `shape_predictor_5_face_landmarks.dat` and `dlib_face_recognition_resnet_model_v1.dat` in `/usr/share/noovex/faceid`. `nvx-faceid serve /dev/nvxface` loads the models once and answers requests; `nvx-faceid compare A.bmp B.bmp ...` prints distances (used for testing on the build machine). |
| `/dev/nvxface` (device 17, root only) | The kernel puts one request at a time there: a 64-byte header (`NFQ1`, sequence, command, width, height, user name) and the RGB picture (at most 640x640). The program reads it, and writes an 80-byte answer (`NFR1`, status, distance x 1000, face box, five points, number of saved prints). Commands: verify, add (setup), save, reset. |
| `core/auth/faceid.c` | `fid_begin(FID_VERIFY or FID_ENROLL, user)` starts the camera (`uvc_start`, JPEG frames decoded like the Camera app), starts the program as root with its output in the kernel log, and `fid_tick` (main loop) sends the newest frame whenever the program is free. Verify: two matches in a row succeed; the check gives up 8 s plus three frame times after the first answer (the program itself may take up to 3 minutes to start on a very slow machine or in an emulator). Setup: every usable frame adds a print as long as it is within 0.4 of the first one (otherwise setup starts over); at 5 the prints are saved (written to a new file, then renamed). Before v10.52 a print had to differ from the saved ones by at least 0.03 and 10 were needed. `faceid` in the shell: `faceid status`, `faceid enroll USER`, `faceid verify USER`, `faceid delete USER`, `faceid feed FILE.ppm` (adds a test picture used instead of the camera; `faceid feed` alone goes back to the camera). |

Screens: `fid_scan_draw` darkens the screen, shows the camera picture in a circle with a blue ring (turning while checking, filling up during setup), "Looking for you..." and a "Use password instead" (or "Cancel") button, and "Welcome back, NAME" with a green check on success. It is used by the sign-in screen, the setup step (`SU_FACE`) and Settings (dialog kinds `au_D_FACE`, which checks the password, and `au_D_FACESCAN`).

Limits: a normal camera cannot tell a face from a good photo or video of it, so Face ID is a convenience and the password stays the stronger way in; a liveness check (blink or head turn) is not part of this release. The face print file holds only numbers. The models add about 30 MB to the Linux files.

Since v10.51 the face engine only counts a picture with exactly one face whose box is at least 96 pixels wide (in the picture of at most 640 x 640 sent to it). Otherwise it answers "too small" or "more than one face", and the scan screen shows "Move a little closer" or "Only one face in view, please" (also in `faceid status` and the kernel log as "faceid: hint:"). Two faces in view never count as a match. The 1.2 MB picture buffer is freed once the face engine has stopped after a check, and the kernel resynchronizes on the answer magic if an answer arrives split or shifted.

Since v10.52 setting up is quick: the person only looks at the camera, no head turning. Five face prints are taken from consecutive pictures (`FID_TARGET` 5), each with one face at least 96 pixels wide. Every print must be within 0.4 of the first one; if not (someone else stepped in, or the first picture was bad), the prints taken so far are dropped and it starts again. The scan screen says "Look at the camera" or "Hold still" with the count. The face engine first looks for faces in a half-size picture and only searches the full picture when that finds none, which makes each picture about three times faster; on the build machine one picture takes about 0.15 s, so setting up takes a few seconds on a laptop.

Since v10.53 first-time setup ends on the sign-in screen instead of the desktop (`su_finish` sets `g_lg_installed` so the new accounts are checked even when setup ran without installing). On a live start from a USB stick, `live_lock_start` (called from `kmain` after `installed_boot`) shows the sign-in screen unless `LIVELOCK.TXT` on the stick starts with `0`; unlocking returns to the live Terminal (`g_lg_liveret`). The setting is in Settings > Accounts > Sign-in options (button id 607), only shown on a live start with a mounted stick.

## 11.17 Registry, Program Files and hidden apps in Task Manager (v10.54)

### Registry (`core/registry.c`, `include/nvx/core/registry.h`)

The registry is a tree of keys and values kept in plain text files:

| File | Contents |
|---|---|
| `/etc/noovex/registry/system.reg` | keys under `System` (whole computer) |
| `/etc/noovex/registry/user-NAME.reg` | keys under `User` for the account NAME (mode 0600) |

The format is `[Key\Path]` lines followed by `Name=Value` lines; `;` starts a comment. `HKLM\` and `HKCU\` are accepted as other names for `System\` and `User\`, and `/` works instead of `\`.

Some values are bound to settings the kernel already had, so changing them in the registry changes the setting at once (and the old settings file is written too, through `power_prefs_save`):

| Key | Values |
|---|---|
| `System\Display` | `Accent` 0-4, `Theme` 0-5, `ShowSeconds` 0-1 |
| `System\Input` | `KeyboardLayout` 0-5, `KeyRepeat` 0-2 |
| `System\Security` | `GuestAccount` 0-1, `LockMinutes` 0-240 |
| `System\Power` | `LidAction` 0-2, `PowerButtonAction` 0-2 |
| `System\SignIn` | `UsbStickSignInScreen` 0-1 (stored on the USB stick, section 11.16) |
| `User\FaceID` | `Enabled` (read only) |
| `System\Info` | `Version`, `ComputerName`, `MemoryMB`, `User` (read only) |

Any other key or value can be added; such values are text. `reg_boot` (called from `kmain` after `installed_boot`) loads the files and applies the bound values, or writes them the first time. A sign-in loads the account's user hive (`reg_user_changed`). Changing `System` keys needs root or an administrator account.

Shell: `reg list [KEY]`, `reg get KEY NAME`, `reg set KEY NAME VALUE`, `reg add KEY`, `reg delete KEY [NAME]`, `reg export FILE [KEY]`, `reg import FILE`. Read-only values and numbers outside the allowed range are refused with a message.

### Registry Editor (hidden app)

`regedit`, or Win+Shift+R, opens the Registry Editor (window slot `W_CHATGPT`, which was unused). It is not in the Start menu. Keys are on the left as a tree, values on the right with Name, Value and About. The toolbar has New key, New value, Edit, Delete, Export, Import and a search box (keys, names and values; Ctrl+F). A double click or Enter edits a value; read-only values are grey. Export and import use the same file format as `reg export`.

### Program Files (`core/progfiles.c`)

`progfiles_sync` builds `/Programs` with one folder per app and an `app.info` file in it (`Name`, `Version`, `Publisher`, `Kind`, `Package`, `Size`, `Icon`, `Command`, `Description`, `Hidden`, `DesktopFile`). It lists the built-in apps, the Linux programs from `/usr/share/applications` that come with NoovexOS (LibreOffice Writer and Calc), and programs installed with `apt`. For an `apt` program the owning package is found in `/var/lib/nvpkg/*.list`, and the size is the total of its files. Folders of removed programs are deleted. The list is rebuilt at start-up and whenever `g_apps_gen` or the Linux files change. A file is only written when its contents changed. The shell command `programs` lists the folders (`programs -a` includes hidden apps).

Settings > Apps > Installed apps shows the list with icon, kind and size. Open starts the app, and Uninstall (only for `apt` programs, administrators only) runs `apt remove -y PACKAGE`. The mouse wheel scrolls the list.

### Task Manager

The Processes tab has three sections:

- **Apps:** open windows, as before.
- **Background services:** the real services from `apps/system/services.c` (web requests, downloads, web cache writer, cookie saving, clock sync, battery and lid, Wi-Fi). Each row shows CPU use, measured from the service's run time per second, and its status. Services that can be stopped have a Stop or Start button; the others say "needed".
- **Hidden apps:** every running program without a window (the face engine, the X server and its helpers, `init`, programs started from the Terminal), with pid, CPU use and state. "Hide" or "Show hidden" switches the list. Selecting a hidden app and pressing End task ends it (`jc_kill`, never pid 1).

Ctrl+Shift+Esc opens Task Manager.

## 11.18 The System folder (v10.55)

`core/sysfiles.c` builds `/System` (like Windows' System32). `sysfiles_build()` runs from `kmain` after `progfiles_sync`, and the shell command `sysfiles` runs it again. `sysfiles_tick()` runs in the main loop: every 2 seconds it writes the live kernel log, and every 30 seconds it refreshes `kernel.info`, the drivers and the services.

| Path | Contents |
|---|---|
| `README.txt` | What the folder is |
| `Kernel/kernel.info` | `Kernel`, `Built`, `ImageBytes`, `SHA256` (from `g_buildsig`), `Boot` (UEFI or Legacy BIOS), `Processor`, `Cores`, `CoresOnline`, `MemoryMB`, `KernelHeapUsedKB`, `Screen`, `Graphics`, `VirtualMachine`, `ComputerName`, `TimerHz`, `MovedFirmwarePages`, `RestoredFirmwareBytes`, `MovedRange` (from `early_remap`), `UptimeSecondsAtRefresh`, `LogLinesSinceStart` |
| `Drivers/<name>.drv` | One per driver in the kernel (table `DRV[]`, 28): `Name`, `Type`, `State` (Loaded, Not loaded (device found), Not loaded), `Reason`, `Description`, `Source`. The state comes from the driver's own flags (`ahci_ok`, `nvme_ok`, `g_dktype`, `xhci_present`, `msd_ok`, `g_mnic.name`, `g_wnd_up`, `g_snd.name`, `g_gfx`, `nvgsp_info`, `g_tp.ok`, ...) and the PCI list. |
| `Drivers/drivers.txt`, `Drivers/devices.txt` | All drivers in one table; every device per category, from `taskmgr_devdump()` (the Task Manager > Devices lists) |
| `Services/<id>.svc`, `services.txt` | `svc_info`, `svc_every`, `svc_runs`, `svc_us` |
| `Fonts/<font>.font` | Inter (UI, `assets/uifont.c`), Noovex Smooth and 8x14, Liberation Sans/Serif/Mono (Writer), Emoji |
| `Config/SYSTEM.reg`, `Config/user-NAME.reg` | Symbolic links to the registry hives in `/etc/noovex/registry` |
| `Logs/kernel.log` | Every `klog` line since start. `klog` calls `sysfiles_logline` (weak), which appends to a 192 KB buffer under a try-lock (a line is dropped rather than waiting if another CPU holds it; the oldest half is dropped when full). |
| `Logs/network.log` | `NETLOG[]` from the network driver |

Everything is owned by root (folders 0755, files 0644), so only root can change it. `dt_is_tmp` treats `/System` like `/tmp`, so nothing in it is written to the disk.

`mkdir` now checks write permission on the parent folder (`perm_dir`) in `lx_mkdir`, the native mkdir syscall, the shell's built-in `mkdir` and `vfs_mkparents`; the Files app checks `perm_dir` before New folder, New file, Rename, folder Delete and Paste.

## 11.19 System folder layout, file types and restore points (v10.56)

`/System` now uses NoovexOS file types. Every file is text (`Name=Value` lines, first line a short tag such as `NVA1`) except `.nvn` (WAV sound).

| Folder | Files | Made from |
|---|---|---|
| `Kernel` | `noovex.nvk`, `kernel.nvi` | `g_buildsig`, boot info, `g_remap_*` |
| `Drivers` | `<driver>.nvd`, `drivers.txt` | `DRV[]` + `sf_drv_state` |
| `Modules` | `net.nvm`, `virtio.nvm`, `sound.nvm`, `modules.txt` | `MODS[]` (the `MODS` list in `build.sh`) |
| `Libraries` | `<lib>.nvl`, `libraries.txt` | `sysroot_list` over `/lib/x86_64-linux-gnu`, `/usr/lib/x86_64-linux-gnu`, `/lib64`, `/usr/lib`, `/usr/local/lib` (sizes from the new `sysroot_fsize`, no unpacking) |
| `Programs` | `hello.nvx`, `table.nvx`, `primes.nvx` | built in |
| `Apps` | `<app>.nva`, `apps.txt` | `progfiles_get` (built-in apps, `Window=` is the window slot) |
| `Services` | `<id>.nvs` | `svc_info` |
| `Config` | `SYSTEM.nvr`, `user-NAME.nvr` | links to `/etc/noovex/registry/*.reg` |
| `Logs` | `System.nvg`, `Security.nvg`, `Application.nvg` | the `klog` buffer, split by source (`sf_cat`), with a level (`sf_lvl`); Security adds `/var/log/auth.log` |
| `Crash` | `<date>.nvq`, `last.nvf` | `crash-report.txt`; the last 60 s of the log |
| `Boot` | `boot.nvb` | every `boot:` line (written 2 s after start) |
| `Hardware` | `<kind>.nvh`, `camera.nvh`, `microphone.nvh` | `taskmgr_devdump`, `camera_info_text`, `mic_info_text` |
| `Fonts` | `*.nvo` | static list |
| `Media` | `*.nvn` | WAV, 22 kHz mono, made from a sine table at start |
| `Themes` | `*.nvw` | `Wallpaper=` (0-5) and `Accent=` (0-4) |
| `Restore` | `*.nvz` | copies of `/var/lib/noovex/restore` |

`sysfiles_open(k)` is called by Files before its own open logic: `.nva` -> `open_win(Window)`, `.nvr` -> Registry Editor, `.nvw` -> `reg_set` Theme and Accent + `build_bg`. `.nvn` is in the media list of `videoplayer.c`.

Restore points: `sf_restore_create` writes all hives into one `.nvz` (`== path ==` sections) in `/var/lib/noovex/restore` (root, 0600, kept on the disk, at most 10). `sysfiles_restore` (`restore` command) lists, creates, or writes the sections back (only paths under `/etc/noovex/registry/`) and calls `reg_load(1)`; it always makes a restore point first.

Pitfall found: the build stamper finds the signature block by searching for its 8-byte magic text. A string containing that text in `.rodata` made it stamp the wrong place and every start reported "integrity check failed". Do not put the magic text in any string.

File-type logos: `FXI[]` in `file_explorer.c` maps an extension to a glyph, a colour, an optional label and a type name; `fx_ikind` returns `32 + index`, `fx_icon` draws it with `fx_xicon`. User icon choices (0-11) are unchanged.

Cameras (`xhci.c`): `uvc_parse(c,n,target)` reads one VideoStreaming interface; `uvc_choose` lists all (`cam.vs[]`) and picks the first with a colour format; `uvc_select_stream` re-parses the saved descriptors (`cam.desc`). Formats by FourCC (`uvc_fourcc`): YUY2 0, MJPEG 1, NV12 2, UYVY 3, grey 4, I420 5, YV12 6. Bulk cameras: endpoint type 6, normal TRBs of 16 KB, `uvc_bulk_chunk` joins transfers into payloads (a payload ends with a short transfer or at `dwMaxPayloadTransferSize`), stop = CLEAR_FEATURE(ENDPOINT_HALT). `cam.epon` replaces `cur_alt` as "endpoint added". Start tries up to 16 sizes in two passes; the first pass skips sizes no alt setting can carry.

Microphones: `aud_parse` reads USB Audio 2.0 (interface protocol 0x20): clock source id, AS_GENERAL channels, FORMAT_TYPE_I subslot and bits; the rate is set with SET_CUR on the clock (`(clock<<8)|ac_if`) and read back. Capture accepts 2-4 byte samples and 1-8 channels (`aud_s16` takes the top 16 bits of the first two channels). A camera with an audio function: `uvc_add_mic` attaches it on the same slot; `uvc_aud_share` keeps the two copies of the EP0 ring position equal. HDA: `scan_in` also remembers a headset/mic jack with presence detect; `pick_in` uses it when the jack reports a plug.

## 11.20 Focus modes (v10.57)

`core/focus.c` keeps up to 12 focuses (`focus_mode`): four built in (Do not disturb, Sleep, Work, Gaming) and any you create (new ones hide pop-ups). Each is a text file `/etc/noovex/focus/ID.focus` (`NVFOCUS1`, then `Name=`, `Icon=`, `Color=`, `Builtin=`, `HidePopups=`, `AllowApps=`, `Mute=`, `HideGames=`, `NightLight=`, `Brightness=`, `BatterySaver=`, `Theme=`, `LockAfter1Min=`, `Schedule=`, `From=`, `To=` (minutes), `Days=` (bit 0 = Monday), `Apps=` (window bit mask), `SignIn=`, `LowBattery=`, `Taskbar=`, `LockScreen=`), copied to `/System/Focus` with a README. Default schedule 22:00-07:00 every day; Work 08:00-17:00 Monday to Friday (schedules start switched off).

| Part | How |
|---|---|
| Turning on | `focus_set(i, why)`: restores the previous focus, saves night light, brightness, battery saver, refresh rate, volume, wallpaper mode and theme, then applies the focus (brightness at least 5%, battery saver 30 Hz). When it ends, each value is put back only if you did not change it meanwhile (volume only if still 0). The previous theme is also kept in `System\Focus\PrevTheme` / `PrevAccent` (written as the system, so it works for standard accounts) and put back after a restart if the focus did not come back. The 1-minute lock uses `g_lock_override` and never touches the saved lock time. `why`: 0 by hand, 1 schedule, 2 app, 3 sign-in, 4 low battery. Only a focus turned on by hand is stored in `User\Focus\Current` and comes back after sign-in. |
| Pop-ups | With HidePopups, `notify_from(app, title, body)` (and `notify`, app "System") still adds to the notification list but shows no toast unless `focus_allows` finds the app or title (any case) in `AllowApps`. |
| Sounds | `Mute` sets the volume to 0 while on. |
| Games | `focus_hides_games` removes the Games group from the Start menu and games from launcher search. |
| Automatic | `focus_tick` (every 5 s): schedule windows (overnight windows use the day they started; same start and end = all day), low battery (20% or less, not charging, only when no focus is on; again after charging or above 25%). `open_win` / `close_win` call `focus_app_started` / `focus_app_closed` (no app is set by default; Gaming offers DOOM and Quake as a preset); app and schedule triggers only act when no focus is on or the current one came from an app or schedule. Sign-in brings back a focus set by hand, otherwise the first focus with SignIn. A focus turned off by hand during its schedule stays off until the window ends. |
| Where | Quick Settings Focus tile (classic) or Focus button (modern) opens a list (`fpop_*` in `desktop.c`) with "Turn focus off", "Create focus" and "Focus settings"; Settings > System > Focus (`st_focus_page`, tabs Focuses / Settings / Turn on automatically, ids 1200-1399); taskbar icon in the focus colour; lock screen line; `focus [list | NAME | off | create NAME | delete NAME]`. |
