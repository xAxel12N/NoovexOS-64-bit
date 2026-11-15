# trace (v6-trace) - breadcrumb trail

Committed on v6-trace (core/crash.c, core/init/main.c, graphics/ui/input.c). All three files compile cleanly with ccheck.

## API (core/crash.c)
- `void crumb(const char* s)`: static strings only. It claims a slot with one `lock xadd` (no lock, no spin, safe in IRQ/NMI context and on any CPU). Each entry is written with plain stores: TSC (inline rdtsc), s, g_in_what, g_ml_stage, and a seq stamp (0 while the entry is being written, i+1 once it is done). There is no formatting.
- Ring: 64 entries (`g_crumb[]`, `g_crumb_n`).
- `int crumb_dump(char* out,int max)`: the last 8 crumbs, newest first, one per line: `name  stage N  in: <g_in_what>  X ms ago`. A slot that is still being written shows "(being written)". It is safe to call from NMI or the red box: no locks, no allocation, output is bounded by max.
- `volatile int g_crumb_serial`: -1 = not checked yet, 0 = off, 1 = on. `crumb_serial_detect()` is called right after klog_init(). It turns serial on only if the COM1 scratch register round-trips (the same test klog uses) and LSR != 0xFF. Set it to 0 or 1 by hand to force.
- Serial mirror: non-blocking. If LSR.THRE (bit 5) is clear, the crumb is skipped. Otherwise `@name\r\n` (name cut to 13 chars) goes into the 16-byte FIFO, with no waiting. A crumb whose pointer matches one of the last 4 seen crumbs is not sent to serial, so the steady per-frame poll/input cycle does not flood the port and only new steps print. The ring always records every crumb.
- The red box and crash report (cx_build, used for both the freeze box and crash reports) now show "Last steps (newest first):" from crumb_dump.

## Crumb sites committed
- on_click> / on_click< (on_click is now a wrapper around on_click_body)
- "marquee start" (just before g_marq=1)
- process_input> / process_input<
- every in_mark() call adds a crumb with the marker name ("mouse event", "mouse click", "key", ...)
- main loop: ml:xhci_poll> / ml:xhci_poll< around xhci_mouse_poll()
- g_ml_stage is recorded in every entry

## Sites outside my files (NOT committed): /home/claude/reports/v1106/trace-sites.patch
Apply with `git apply`. Checked: it compiles with ccheck on drivers/usb/xhci.c and mem/heap.c.
- hid_keyboard_mouse.c xhci_mouse_poll: xhci_mouse_poll> / <, inside the busy guard
- controller_init.c xhid_proto_poll: "proto: xh_ctrl>" / "<" around the SET_PROTOCOL xh_ctrl
- controller_init.c xh_event_w: xh_event_w> / <
- mem/heap.c kmalloc_raw: "kmalloc slow path" (free lists missed) and "kmalloc heap grow" (before hp_alloc_run)

## Notes for NMI agent
`extern int crumb_dump(char*,int);` writes into your buffer. crumb() is NMI-safe, but if you crumb from the NMI handler, serial output can interleave with a CPU that is in the middle of a crumb.
