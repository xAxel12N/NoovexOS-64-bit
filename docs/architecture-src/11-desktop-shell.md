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
| `apps/*.c` | editor, calculator, chatgpt, browser, settings, files_classic, (fs/pkg.c), file_explorer, small_apps, doom, logs |
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
3. Trackpad gestures (`g_gesture`): swipe up opens the launcher, swipe down closes the launcher and panels, swipe left/right moves to the next/previous workspace.
4. Mouse.
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
| Input | `live_input()` discards mouse events and sends keys through the same Ctrl mapping to `tty_key`/`term_key` | `process_input()` (above) |

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
2. Keyboard layout, 6 entries in `SU_KBDS`;
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
| ChatGPT | `chatgpt.c` | `cg_send`, `cg_push`, `chatgpt_draw/_click/_key` | chat client for a hosted backend |
| DOOM | `doom.c` | `doom_init`, `doom_draw`, `DG_*` hooks | doomgeneric + FreeDoom |
| Settings | `settings.c` | `settings_draw`, `settings_click`, `nvx_settings_save/load` | 8 categories |
| Logs | `logs.c` | `logs_draw` | live kernel info and a scrolling hexdump |
| Terminal Finder | `logs.c` | `tfind_draw`, `TF_CMD[]` | cheat-sheet of hidden developer commands |
| Presentation | `files_classic.c` | `pptx_load`, `pptx_draw`, `pptx_key`, `pptx_click` | `.pptx` viewer |
| Clock, Task Manager, Calendar, Snake, Flappy Bird, neofetch, Paint | `small_apps.c` | `clock_draw`, `sysmon_draw/_click`, `calendar_draw/cal_key`, `snake_*`, `flappy_*`, `neofetch_draw`, `paint_*` | small utilities and games |
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

- Keys: arrow keys arrive as codes 17-20. Ctrl+C/X/V copy, cut and paste the current line through `g_clip[1024]`.
- Save: `editor_save()` writes back to the `FS` slot through `fs_wr` and calls `fs_sync`, or "Not enough memory to save" is shown.
- Run: `editor_run()` joins the buffer and calls `js_run(ed_src,n,ed_run_cb)`. Output goes to a console pane covering 42% of the editor height. If nothing is printed, the pane shows "(no output)" or "[program error]".

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

### ChatGPT (`apps/internet/chatgpt.c`)

`cg_send()` builds `{"message":"...","source":"noovexos"}` with minimal escaping and calls:

```c
https_request(CG_HOST,"/api/public/chat","POST","Content-Type: application/json\r\n",body,bl,http_buf,...)
```

`CG_HOST` is a `*.lovable.app` preview host. The client takes `"reply"` (or `"error"`) from the JSON body with `json_str`. The conversation keeps the last 24 messages (`CG_MAXMSG`), each up to 400 characters, with role 0 = user, 1 = assistant, 2 = system note.

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

Keys: 1 gears, 2 3 4 other scenes (showcase, materials, 192 cubes), W wireframe, S shadows, H half resolution, Space pause, arrows and + - for the camera, R reset, G start the NVIDIA card. Speed: a gears frame at 996x586 takes 8.7 ms on one core of the build machine (about 115 frames a second); in QEMU without hardware acceleration it is 1-2 frames a second.

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

The window opens at up to 1000 x 800, sized to the screen and centred above the taskbar. The sidebar is 190-256 px wide with 42-px items 50 px apart; the selected page is an accent-coloured pill. Content has 40-px margins and 28 px between cards. Toggles (`st_toggle`) are 48 x 26 with a shadowed knob and a hover state, buttons (`st_btn`) are 34 px tall with rounded corners, the segmented control (`st_seg`) is a soft tray with a raised white chip, and text uses the proportional font.

### Small apps (`apps/accessories/small_apps.c`)

- Clock: an analog face drawn with the integer `isin`/`icos` and `clock_hand`.
- Task Manager (`sysmon_draw`): one row per open window. Each row shows a per-app memory figure from `tm_app_kb()`, which the source calls "representative" (for example Browser 149504 KB and DOOM 33792 KB). Seven background rows (`TM_BG_N`: System, Compositor, Input, Network stack, NVXFS, TLS engine, Heap manager) use fixed `TM_BG_KB` values. "End task" calls `close_win()` on the selected row.
- Calendar: `cal_dow`, `cal_dim`, month navigation with arrows or the buttons.
- Snake: a 21x15 grid with an xorshift RNG; step speed is set in `process_input`.
- Flappy Bird.
- neofetch: a graphical page that is separate from the text `neofetch()` in terminal.c.
- Paint: a 540x360 `paint_buf` with a 12-colour `PPAL`, three brush radii (2/4/8) and Clear.

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
| `chatgpt`, `ai`, `gpt` | Open ChatGPT |
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

Task Manager (`apps/system/taskmgr.c`) has a left rail with Processes, Performance, Devices and Services. Performance samples once a second: CPU use is 100% minus the share of idle ticks counted by the scheduler on cores 1 and up (core 0 runs the desktop and is not sampled), memory is the kernel heap in use against installed memory, GPU busy is the GPU drawing plus screen copy time of the last frame against one refresh period. Devices builds its lists from the PCI scan, `igpu_info()`, the ACPI battery objects, `gdisk_*`, the USB device table and the sound driver; categories open on click and the list scrolls with the mouse wheel.

Settings (`apps/system/settings.c`) keeps the current page in `g_stpg`. Pages 0-8 are the category pages (tiles), higher numbers are sub-pages; `pg_parent()` gives each page its parent, which builds the path at the top and the back button. The previous per-category pages are drawn by `old_leaf()` and reused as sub-pages. `settings <page>` opens a page from the terminal.
