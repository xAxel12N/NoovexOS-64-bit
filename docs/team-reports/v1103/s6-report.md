# S6 - whole-OS background/service hang search (shell, sys, fs, net, web, per-frame ticks)
Verdict: no unbounded loop in the background services that is tied to drag release. Nothing in these areas runs on mouse-up:
input.c:197 only does apply_snap + clears drag/resize/snap/marq, and nothing saves window layout to disk (grep fs_wb_mark/cfg_save: no WM caller).
Ranked by link to "freeze when a drag ends" (all low; probably timing coincidence):

1. sys/native/syscall.c:125 (waitkey, sc 51) `while((k=kpop())<0) kidle();` with g_frz_hold=1 - waits FOREVER for a KEY; kidle only polls xHCI mouse, no UI redraw.
   If a native app/thread runs this on the BSP during the desktop session, the screen stops updating; only a key press ends it (mouse does nothing). Matches "mouse works then everything froze" if the app starts waiting around mouse-up. Conf 15%.
   Fix: in the loop call g_wait_pump() (if set) or bkl_drop() + return -EAGAIN after ~1 tick so jc_step yields; minimum: add `if(g_wait_pump)g_wait_pump();` before kidle().
2. sys/native/syscall.c:205 (sleep, sc 71) `while(g_ticks<until)cpu_idle();` clamp is 3,600,000 ms (1 h), no pump. Same freeze if it runs on the BSP. Conf 8%.
   Fix: clamp to 10 s and pump: `while(g_ticks<until){ if(g_wait_pump)g_wait_pump(); cpu_idle(); }` (or run only on AP threads).
3. net/wifi/wifi_mgr.c:287-289 auto-join runs wifi_connect_ap()/wm_connect() SYNCHRONOUSLY from main loop (wifi_mgr_tick, core/init/main.c ~line 351) once saved-network list loads (g_ticks>300 = ~3 s, then retry with backoff 5-60 s). A blocking connect (auth/assoc/EAPOL/DHCP) stalls the desktop for seconds; on the real RTL8821CE a stuck card could make it feel like a freeze. Pure timing coincidence with drag. Conf 8%.
   Fix: check user is not dragging/idle (`if(drag>=0||g_resize>=0) defer wm_next_try+=100`) and verify wm_connect pumps g_wait_pump; better make it async.
4. core/init/main.c:316 `if(g_fs_wb_pending&&g_ticks>=g_fs_wb_due) nvx3_sync_all();` (fs/nvx/nvx4.c:363) synchronous disk flush on the main loop 1 s after any write; if root is on the USB stick behind xHCI, a stalled BOT transfer blocks UI for its timeout. Conf 5%.
5. apps/internet/browser/websock.c:39/265/416 and browser glue/startup.c:281 wait loops on g_ticks (bounded by deadlines, pump g_wait_pump) - not a hang unless g_ticks stops. core/update.c:400 3 s wait (bounded). Conf <3%.

Checked and OK (bounded): drivers/rtc/cmos.c:15 read_clock (100k guard), focus_tick, sysfiles_tick (every 2 s), svc_tick, nic_watch, net/wifi/wifi_netdev.c:152 rx ring (breaks on empty desc, wp sanity check), net/stack/tcpip.c:220 dns_skipname, http/cookie parsers, hr_delay_us (iteration-limited), main-loop idle (core/init/main.c:308: cpu_idle does sti;hlt/mwait, LAPIC one-shot only wakes, PIT tick vector 32 unaffected; xhid_n>0 always arms 1.5 ms wake).
Note: all g_ticks waits depend on IRQ0 arriving; v11.02 kmalloc IRQ-off uses save/restore (mem/heap.c:65,90) so it cannot leave IF=0. If g_ticks stops (IRQ masked at PIC/IOAPIC), items 2/5 hang forever - worth asking owner if the clock still ticks.
