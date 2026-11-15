# I5 - easy bug reporting for the owner (read-only brainstorm)

## What already exists (do NOT re-propose)
- `crash` (core/crash.c:377 crash_cmd): shows crash-report.txt or kept freeze report; `crash test panic|freeze|stack|smash|heap|msr`.
- Crash/freeze reports auto-saved: CRASH.TXT on USB + crash-report.txt in Files (crash.c:163, :290 freeze save after ~30 s, :313-314 save after recovery). CRASHNEW.TXT/RECNEXT.TXT flags. So a "report button in the freeze box" is redundant - it already saves by itself and the screen says where.
- `lag` (core/init/main.c:192 lag_report): slowest main-loop stages, last 16 runs >=50 ms, service times. Screen only, no save.
- `usbinfo` (shell/commands/network_and_apps.c:213), `usbdevs`/`lsusb` (:103), `usbhid` (shell/commands/usbhid.c), `usb`/`xhci` rescan. Screen only.
- `tracelog on|save|off` (network_and_apps.c:104): saves tracelog.txt + TRACELOG.TXT (Linux syscalls/app output only, not klog).
- `nvgsp log` -> NVGSPLOG.TXT; `ver`/`about` (accounts_files_info.c:78).
- Redirection `cmd > file` works (prompt_and_dispatch.c:259) but writes via VFS (Files), not the USB stick, and needs typing.
- Terminal capture: g_cap[8192]/g_capture (shell/terminal.c:36), sh_capture() (prompt_and_dispatch.c:107).
- klog ring 16 KB: klog_ring(out,cap,mode) core/klog.c:33 (mode 1 = non-destructive copy). Only user: Linux syslog syscall (sys/linux/syscalls/more_syscalls.c:520).
- Logs app (apps/system/logs.c): shows CR3/RSP/kernel hexdump - NOT the klog. Useless to the owner.
- No `report`, `bugreport`, `log`, `klog`, `dmesg` shell command; no REPORT.TXT/KLOG.TXT anywhere (grep -rn over shell/, core/, apps/, docs/).

## Top 3 NEW ideas (~10 min each)

### 1. `report` command -> REPORT.TXT on the stick + report.txt in Files + short on-screen summary
- File: shell/commands/system_disks_packages_gpu.c (next to `lag`, line 200) or new shell/commands/report.c (add to units.list).
- kmalloc 64 KB buffer; for each of "ver","usbdevs","usbinfo","usbhid","lag","crash": header line "== usbinfo ==", set g_capture=1,g_cap_n=0, run_cmd(copy), append g_cap (8 KB per command cap - note truncation), g_capture=0. Then "== kernel log (last 8 KB) ==" via klog_ring(tmp,8192,1).
- Save: fs_save_bin("report.txt",...) + nvx3_sync_all() if disk_ok; if msd_ok&&ufat_ok ufat_write_file("REPORT.TXT",...) (same pattern as tracelog save, network_and_apps.c:112).
- Print 4-6 big readable lines: version, "USB: N devices, M ports NOT set up", "slowest: <stage> X ms", "crash report: yes/no", "saved to REPORT.TXT on the USB stick". Owner photographs one screen or hands over the stick.
- Proof: grep -rn '"report"\|REPORT.TXT' shell core -> nothing.
- Risk: must not be run inside a pipeline (g_capture already set) - bail if g_capture. Nested run_cmd of "crash" with no args is safe (no test).

### 2. `log [N]` / `log save` - kernel log in the terminal
- File: shell/commands/system_disks_packages_gpu.c (same dispatcher).
- `log` = last 30 lines (default) from klog_ring(buf,16384,1), walk back N newlines, term_puts. `log save` -> klog.txt in Files + KLOG.TXT on USB. Also alias `dmesg` only if /bin dmesg does not shadow it (check /bin list line 187: no dmesg there).
- Proof: no seq(cmd,"log"|"klog"|"dmesg") in shell/commands; klog_ring only used by the syslog syscall.

### 3. Logs app shows the kernel log tail + "Save report" key
- File: apps/system/logs.c logs_draw(): replace the hexdump block (8 rows) with the last ~20 klog lines from klog_ring(buf,4096,1) (draw with dstr, clipped to window width), and show a hint "R = save bug report to USB stick". In the Logs key handler, 'r' calls run_cmd("report") (idea 1) and shows the result line.
- Proof: logs.c has no klog_ring / ufat / save (grep -n "save\|ufat\|klog" apps/system/logs.c -> only help strings).
- Gives the non-programmer a mouse/one-key path; depends on idea 1.

Rejected: freeze-box report button (already auto-saves, crash.c:289-314); QR code (owner wants readable text).
