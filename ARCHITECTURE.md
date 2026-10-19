# NoovexOS 64 Architecture and Developer Reference

Version 9.83. x86-64, BIOS and UEFI, written from scratch.

This is the developer reference for NoovexOS 64: a from-scratch x86-64 operating system with its own boot loaders, kernel, drivers, file system, network stack, desktop and Linux-compatible user space. It was written from the source code, with file paths and symbol names so you can find the implementation. Where the code does something surprising or has a known gap, the text says so.

## At a glance

| Area | What is there | Part |
|---|---|---|
| Boot | BIOS loader (myboot) and UEFI loader, hybrid ISO/USB image, SHA-256 build signature that stops the boot if the kernel was modified | [2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| Kernel | 146 separately compiled C and C++ units with interface headers under `include/nvx/`, a 77 MB flat image (100 MB limit) with compressed programs, the web engine, glibc, GNU tools and CPython 3.13, identity-mapped kernel, demand paging and copy-on-write for programs | [1](#part-1-overview-source-layout-and-build-system), [3](#part-3-memory) |
| CPU | Up to 16 cores with per-CPU state, a ticket big kernel lock, IPIs for rescheduling, TLB shootdown and halt; cores start automatically | [4](#part-4-cpu-interrupts-timers-and-multi-core) |
| Programs | 256 processes and threads, futexes, signals, timers, job control, pipes; native `int 0x80` ABI and a Linux `syscall` ABI that runs static and dynamic musl programs (SQLite, gzip, xz, zstd, the kilo editor, the `fetch` HTTPS client) | [5](#part-5-processes-scheduling-signals-and-the-native-system-call-abi), [6](#part-6-linux-compatibility-libc-and-porting-applications) |
| Files | NVX4 (extents, two checksummed metadata copies, safe against power cuts), FAT, NTFS via ntfs-3g, Unix permissions | [7](#part-7-file-systems) |
| Storage | ATA PIO, AHCI, NVMe, VirtIO-blk, USB mass storage; graphical installer and Disk Manager | [8](#part-8-storage-drivers-pci-and-the-installer) |
| Network | PCnet, e1000, RTL8139/8169, VirtIO-net, RTL8821CE Wi-Fi with WPA2; IPv4, DHCP, DNS, TCP/UDP, loopback; client and server sockets for Linux programs; TLS 1.3 in the kernel and BearSSL TLS 1.2 for programs, with certificate checks | [9](#part-9-networking-and-wi-fi) |
| Devices | PS/2 and USB (xHCI) input, I2C touchpads, ACPI with an AML interpreter, HD Audio/AC97, framebuffer and GPU detection, an NVIDIA Turing driver through the GSP firmware (`nvgsp`) | [10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| Desktop | Compositing desktop, apps, file explorer, an xterm-style terminal, the `nsh` shell with scripting and Lua | [11](#part-11-desktop-applications-shell-and-scripting) |
| Web | A browser engine: HTML parser and DOM, CSS cascade, layout with floats, positioning, tables, flexbox and grid, QuickJS JavaScript with the browser API, a cookie jar saved to disk, and the `qjs` command | [13](#part-13-the-web-engine-javascript-and-cookies) |
| Testing | `tools/check.py` (build, static analysis, host tests under sanitizers, QEMU multi-core and network runs), boot emulation, serial log, panic screen | [12](#part-12-testing-quality-gates-debugging-and-history) |

## How to read this document

- New to the code: read "What changed in v9.83", then Parts 1 and 2 for the build and boot, then the part for the area you want to change.
- Porting a program: go to Part 6; the step-by-step guide is in section 6.9.
- Chasing a bug: start with the troubleshooting table in section 12.9 and the serial log in section 12.5.
- Paths are relative to the root of the repository (since v9.83 the source tree is flat: `arch/`, `boot/`, `core/`, `drivers/`, `fs/`, ... at the top).

## What changed in v9.83

| # | Before (v9.82) | Now (v9.83) | Where |
|---|---|---|---|
| 1 | The code sat under `source/kernel/`, `source/bootloader/`, `source/tools/` and others, with most kernel folders flat (`fs/`, `net/`, `apps/`, `lib/`, `gui/` and `arch/x86_64/` had no subfolders). | The source tree is flat at the repository root with one folder per part of the system: `arch/`, `boot/`, `core/`, `sys/`, `mem/`, `fs/`, `drivers/`, `graphics/`, `net/`, `shell/`, `apps/`, `lib/`, `web/`, `usr/`, `lang/`, `assets/`, `include/`, `third_party/`, `tools/`, `docs/`. Inside them, code is grouped in subfolders: `arch/x86_64/{boot,cpu,interrupts,smp,time,user}`, `fs/{vfs,nvx,fat,ntfs,pkg}`, `drivers/input/{ps2,touchpad,vm}`, `drivers/net/{ethernet,wifi}`, `drivers/video/{fb,intel,nvidia}`, `graphics/{ui,theme}`, `net/{stack,http,print,wifi}`, `lib/{base,cxx,compress,image,js,crypto}`, `apps/{system,office,internet,media,games,accessories,x11,graphics}`, `sys/{native,linux}`. `include/nvx/` mirrors the new tree. Section 2 of Part 1 has the full table, with the old place of every folder. | [section 1](#part-1-overview-source-layout-and-build-system) |
| 2 | - | Every top-level folder has a `README.md` that says what is in it; the repository `README.md` has the layout. | [section 1](#part-1-overview-source-layout-and-build-system) |
| 3 | `build.sh` ran from `source/kernel/`. | `build.sh` runs from the repository root (`bash build.sh`); output goes to `build/`, the boot loader is built in `boot/`. `tools/check.py` and `tools/emulate.py` work from the new root, and the static checks only look at kernel code (not the boot loader or tools). | [section 1](#part-1-overview-source-layout-and-build-system) |

The code inside the files did not change apart from paths (`#include` lines, build and test scripts) and the version number. Tested: `tools/check.py --sanitize` on an image built from the new tree, 92 of 92 checks; QEMU legacy BIOS and UEFI boot to the live session and run Gears.

## What changed in v9.82

| # | Before (v9.81) | Now (v9.82) | Where |
|---|---|---|---|
| 1 | The kernel was C and assembly only. | The kernel is C, C++ and assembly. `.cpp` units in `units.list` are compiled with `g++` (no exceptions, no RTTI, no SSE); `lib/cxx/cxxrt.cpp` gives `new`/`delete` on the kernel heap and the small C++ runtime; global constructors run from `.init_array` right after the heap starts (`cxx_run_ctors`); `include/nvx/lib/cxx.h` has `nvx::KBuf` and other helpers. | [section 1](#part-1-overview-source-layout-and-build-system) |
| 2 | - | Gears: a glxgears-style 3D demo written in C++ (Start > Drivers & Hardware, or `gears`). Its own 3D renderer (`apps/graphics/r3d/r3d.cpp`, a freestanding SSE module) draws three interlocking gears with lighting and a large FPS counter, plus three more scenes (torus knot with shadows, 12 material spheres, 192 cubes) and a wireframe mode. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | The NVIDIA copy engine was only used for a 4 KB self-test. | `nvgsp_ce_map` and `nvgsp_ce_copy2d`: any kernel buffer can be mapped into the GPU address space and copied as a 2D rectangle with a completion semaphore. The mapping goes page by page through the CPU page tables (`kva_to_pa`, now shared from `igpu.c`), so it is right even for pages that `early_remap` moved. Gears uses it to put every frame on the screen once `nvgsp all` has run (G in the window). On an Intel iGPU desktop it uses the GPU blitter instead. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: `tools/check.py --sanitize` on the v9.82 image, 92 of 92 checks, including the NVIDIA simulator with the new 2D copy check (26 checks) and 0 analyzer findings with the two C++ units in the build. QEMU legacy BIOS and UEFI (OVMF): Gears opens from the terminal, the gears turn, keys 1 to 4 switch scenes and W shows the wireframe; it runs at 1-2 frames a second there because QEMU emulates the CPU without hardware help, and the same renderer takes 8.7 ms per gears frame on the build machine. The NVIDIA copy-engine path has only run in the simulator; on a real card it needs `nvgsp all` to work first. The Intel blitter path was not tested (QEMU has no Intel iGPU).

## What changed in v9.81

| # | Before (v9.80) | Now (v9.81) | Where |
|---|---|---|---|
| 1 | The AHCI, NVMe, xHCI, USB stick, VirtualBox mouse and identify buffers sat at fixed physical addresses (0x09A10000-0x09C80000). `early_remap` moves a 2 MB page that is not usable RAM to new memory, but only for the CPU: a device that is told the old physical address writes into firmware memory, so disks and USB could fail on a laptop that reserves that range. | One 2 MB device window (`include/nvx/mem/dmawin.h`, `DMW(off)`). `dmw_init()` runs right after `early_remap_fix_mm()` and takes a 2 MB block of real RAM below 4 GB from the DMA arena (skipping bad pages with `mm_skip_bad`); only if that fails does it keep the old address 0x09A00000. All users go through `DMW()`: AHCI (`gdisk.c`), the AHCI identify buffers (`ahci.c`), NVMe, xHCI controller 0, the HID areas and the USB stick buffers (`xhci.c`), the VirtualBox request (`vmmouse.c`) and the PCnet rings and buffers (`nic.h`, the old `.bss` arrays are gone). xHCI controllers 1-3 get their own pages from `dma_alloc_pages`. The boot log says where the window is (`memory: device buffers in the checked DMA arena at 0x...`). `check.py static` checks that the regions inside the window do not overlap. | [section 3](#part-3-memory) |
| 2 | The `in`/`out` helpers in `arch/x86_64/cpu/io.h` had no memory clobber, so the compiler could move a store to a DMA buffer past the port write that starts the device. Several drivers had their own inline assembly. | Every port helper has a `"memory"` clobber. `cpu_mb`, `cpu_clflush`, `cpu_copy64` and `cpu_vmw_backdoor` in `arch/x86_64/cpu/cpu.h`; `igpu.c`, `vmmouse.c`, `bench.c`, `jobs.c` and `linux.c` use them. `core/init/early_fault.c` moved to `arch/x86_64/interrupts/early_fault.c`. | [section 4](#part-4-cpu-interrupts-timers-and-multi-core) |
| 3 | The kernel writes to user memory with CR0.WP set, but several Linux and native syscalls only checked that the user address was mapped, not that it was writable. A program that passed a read-only page (for example its own code) to `read`, `pipe`, `wait4` or `poll` made the kernel take a page fault. | Write checks (`lx_okw` in `sys/linux/linux.c`, `jc_umapped_w` in `core/proc/jobs.c`) before every kernel write to user memory: read, pread/pwrite, getdents, poll, epoll_wait, select, wait4/waitpid, recvfrom, recvmsg, pipe/pipe2, getrandom, socketpair, readlink/readlinkat, futex wake-op, the signal frame, the clear-child-tid write and the native read and pipe calls. The call returns EFAULT. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | `shmget` accepted any size and `shmat` at a fixed address silently replaced what was mapped there. | `shmget` refuses a segment larger than a quarter of the free frames (ENOMEM). `shmat` at an address that is already mapped needs `SHM_REMAP`, as on Linux. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 5 | Converting a disk to NVX4 formatted it in place; a power cut during the format lost the old first blocks. | `n4_conv_backup` copies the blocks the format overwrites to a reserved area at the end of the disk with a marker (magic, block count, FNV checksum) in the last block. `disk_init` restores them if a disk has a valid marker but is not a mountable NVX4 disk; a failed format restores at once. `n4_conv_done` clears the marker when the new file system is written. | [section 7](#part-7-file-systems) |
| 6 | The Elantech PS/2 touchpad could send a two-finger frame before the second finger's head packet, so a scroll started as a jump. | `g_etp_wait` holds a frame until every finger in the status mask has sent its head packet (`elan_ps2.c`). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 7 | Quake had no sound (`snd_null.c`). | WinQuake's mixer (`snd_dma.c`, `snd_mem.c`, `snd_mix.c`) with a NoovexOS back end (`snd_nvx.c`, a 32768-sample ring). `apps/games/quake.c` keeps about 70 ms queued in the kernel sound ring. Checked on the host with `+play weapons/guncock.wav` (peak 22196 of 32767). | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 8 | Five host test groups did not build any more (usb, nvme, memory, aml, touchpad) and the Linux harness crashed; `emulate.py` could not find `uefi_entry`. | All templates follow the current code (device window, stubs for new helpers). The Linux harness has the X damage-tracking stubs, refetches host page entries after a spurious fault, covers 16 GB and clears SysV segments before the leak check. `emulate.py` finds the entry after the CET lines. The AML test's own heap moves to 0x60000000 when 0x80000000 is taken (it is inside AddressSanitizer's shadow memory), so `--sanitize` passes too. All `check.py` groups pass. | [section 12](#part-12-testing-quality-gates-debugging-and-history) |
| 9 | Some places still said 9.80 or older (the terminal said v8.38); `uname` printed a fixed string. The source zip had no FFmpeg, glibc, CPython or Tcl/Tk sources, so `build.sh` could not run from it. | Version 9.81 everywhere. `uname` reads `/sys/kernel/osrelease` and knows `-r`. The source zip contains `third_party/ffmpeg` (n8.1.2) and `third_party_src/` (glibc 2.44, CPython 3.13, Tcl/Tk 8.6). `build.sh` needs `bash` and `gawk`. | [section 1](#part-1-overview-source-layout-and-build-system) |
| 10 | The installer's list of kernel areas used fixed indexes and an old boot stack size. | The boot stack area is 0x09800000-0x09A00000; "USB and disk buffers" is listed only while the device window is still at its fixed address; the module area is found by address. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 11 | NVIDIA cards were only probed read-only. | A GSP-RM driver for Turing (TU102-TU117) with the `nvgsp` command, see section 10.10.6: VRAM size and an allocator, the WPR2 layout, FWSEC-FRTS from the VBIOS, the signed booter on SEC2, the GSP firmware with its radix3 page tables, the message queues, BAR1 and BAR2 page tables and instance blocks, and the objects client, device, subdevice, address space, channels, `TURING_A`, `TURING_COMPUTE_A` and `TURING_DMA_COPY_A`, with a copy-engine test. The firmware (535.113.01 from linux-firmware) is built in, xz-compressed. `tools/tests/nvidia/nvsim.c` simulates the card and checks every step (25 checks). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 12 | Under UEFI the USB stick was never found (`usbls`: "no USB stick found"). The firmware (OVMF, and laptops with "Above 4G decoding") places the xHCI registers above 4 GB, and the driver only took 32-bit register addresses, so it gave up on the controller (`usbinfo`: "registers above 4 GB, cannot be used yet"). Also, a device that showed up on a port after the first scan was marked as already seen and never set up. | Register addresses are 64-bit (`xhci_base`, `xhci_op` and every register pointer in `xhci.c`); a window above 4 GB is mapped with `acpi_hi_map`, as NVMe does. `msd_autoinit` reads the port status again before it looks for a stick, and the first hot-plug scan only marks ports that were set up. Tested: the USB stick in QEMU under UEFI (65 MB FAT32, files listed and read), which failed in v9.80. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: `tools/check.py --sanitize` on the final image, all 92 checks (the qemu group, 4-core smptest, musltest 97, featest 52, dyntest 13 and the parking of cores, timed out once while eight other QEMU machines ran and passed when run alone; emulate.py 29 of 29). QEMU legacy BIOS and UEFI (OVMF): boot to the live session, the NVMe controller and disk list, the USB stick (list and read a file) and `nvgsp` without an NVIDIA card (stops cleanly at step 1). Quake sound was checked on the build host and an HD Audio tone in QEMU was recorded correctly; Quake's own sound in QEMU was not heard because no key or mouse input could start the game there. The NVIDIA driver has only run against the simulator; it is untested on a real card.

## What changed in v9.80

| # | Before (v9.79) | Now (v9.80) | Where |
|---|---|---|---|
| 1 | The X compositor compared the whole Xvfb screen with a shadow copy every 2 to 8 ticks to find what changed (about 4 MB at 1280x800, 8 MB at 1080p), even when nothing moved. | Damage tracking with the processor's page dirty bits (`apps/x11/xdisplay.c`). When the X server maps its screen file for writing, the kernel keeps a pointer to each page table entry (`xfb_track`). Every tick it reads and clears the dirty bit of each page (`xfb_collect`), sends a TLB flush to the other processors without waiting, and looks only at the pages that were written, plus the pages from the tick before to cover a write that raced with the flush. Every 3 seconds it does one full compare as a safety net. The tracking stops by itself (and the old full compare is used) when the X server unmaps the screen or exits (`xfb_untrack_range`, `xfb_untrack_cr3`). It is used in all three X modes: on the desktop, in the X Display window and full screen. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | - | `xstat` in the terminal shows the compositor numbers since the last call: tracking on or off, dirty pages, scans, partial updates, full redraws, total compositor time and the time spent reading dirty bits. `xstat off` and `xstat on` switch between the old full compare and the new tracking for comparison. Measured in QEMU (software emulation, 30 seconds each): idle desktop with X running 1180 ms of compositor time before, 246 ms now; an animating X program 3154 ms before, 1651 ms now, with more frames shown. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | Firefox rebuilt the font cache on every boot, had its disk cache off, rewrote its settings file on every start and did not restore tabs after a crash. | The `nvx-firefox` launcher keeps the font cache in `/root/.cache` and runs `fc-cache` only when a font folder has changed since the last run (`/root/.cache/nvx-fc-stamp`); the same for `nvx-xrun`. The disk cache is on (256 MB). `user.js` is written only when it changed, so Firefox's startup cache in the profile stays valid. A profile lock left by a crashed Firefox is removed. Session restore after a crash is on (`browser.sessionstore.resume_from_crash`, up to 3 crashes in a row, tabs loaded when clicked). The launcher notes in `/tmp/ff.log` when the last run did not end cleanly and how long Firefox ran. `cmp` and `readlink` were added to the system tools for this. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | The Firefox crash report listed unimplemented syscalls by number and name but not unsupported options inside supported syscalls, and the report was only on screen. | The Linux layer records every unsupported option it refuses (`lx_gap`): Unix socket types, socket families, internet socket types, ioctl requests, fcntl commands, prctl options, clone flags and socketpair types, with a count and the order they happened in. The report starts with a "Most likely cause" section: a program that is not installed (exit code 127), or the last missing syscall or unsupported option used before it stopped, skipping syscalls that programs only probe for (rseq, io_uring, landlock, clone3, openat2, faccessat2) and options that are usually harmless (IPv6 and netlink sockets, prctl options), which are still listed and marked. Each report is saved in Files as `firefox-crash-N.txt` (or `app-crash-N.txt`). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 5 | Cross-process futexes did not work: a futex was found by process group and virtual address, so a process-shared mutex, condition variable or semaphore in shared memory never woke a waiter in another process. Absolute futex timeouts on the real-time clock were cut to 100,000,000 seconds, so every wait like that timed out at once. | Futexes without the private flag are found by physical address (`lx_fx_key`), so they work between processes; thread exit (`CLONE_CHILD_CLEARTID`) wakes both kinds. Only relative timeouts are capped now. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 6 | Unix sockets only supported stream sockets; there was no `/dev/shm`; every receive moved the rest of the 256 KB buffer one byte at a time. | `SOCK_SEQPACKET` (with `socket`, `connect`/`accept` and `socketpair`) and `SOCK_DGRAM` in `socketpair`, keeping message boundaries with a 64-entry length queue and passing file descriptors with the right message. `/dev/shm` exists at boot. The receive buffer moves 8 bytes at a time. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 7 | Firefox could only run as one process. | `nvx-mptest` checks what Firefox's multi-process mode needs from the Linux layer: shared memory with memfd, passed over a socket with `SCM_RIGHTS`, `/dev/shm` files, a 64 MB buffer in two processes, process-shared mutex, condition variable and semaphore, `posix_spawn` with descriptor remapping, epoll and eventfd across processes, SIGCHLD, fork and exec from a threaded process, the parent-death signal and `SOCK_SEQPACKET`. All 12 pass. `firefox-mode multi` makes the next Firefox start use 4 content processes (`dom.ipc.processCount` 4, extensions in their own process); `firefox-mode single` goes back. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Tested: QEMU legacy BIOS with 2 processors: X on the desktop with nvxwm and xdemo (correct pictures, `xstat` numbers above), `nvx-mptest` 12 of 12 (4 failed before the fixes), `gthreads`, and the crash report with Firefox not installed (report on screen and saved in Files). `nvx-mptest` was also checked on a normal Linux system (12 of 12). Firefox itself, its start time and multi-process mode are untested until they run on the laptop, because Firefox has to be installed with apt over the network.

## What changed in v9.79

| # | Before (v9.78) | Now (v9.79) | Where |
|---|---|---|---|
| 1 | No Quake. | Quake (the original software-rendered engine) in a desktop window: `quake` in the terminal or Start menu > Games. The engine is quakegeneric (WinQuake source, GPL v2) in `third_party/quake`, built like DOOM into one relocatable object (`quake_blob.o`, compiled with SSE, only `QG_Create`, `qsh_tick_us` and the `qsh_set_*` hooks global) with its own small C library (`quake_shim.c`: heap, printf, RAM files, `setjmp`/`longjmp` in assembly, hardware `sqrtsd`). It renders at 640x480 and is scaled to fill the window. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | quakegeneric only built for 32-bit. | 64-bit fixes: QuakeC strings are stored as 32-bit offsets from the progs string block, which breaks when engine strings live far away in a 64-bit address space; every `pr_strings + n` and `p - pr_strings` now goes through `PR_Str` and `PR_SetStr`, which keep offsets inside the block and give other strings negative indexes into a hashed pointer table (the approach modern source ports use). The video buffers follow the chosen resolution and the warp buffers keep the 320x200 limit. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | - | Game data: LibreQuake 0.09 Lite (art BSD-3, QuakeC GPL-2; licences in `assets/librequake/`), `pak0.pak` and `pak1.pak` (50 MB) stored xz-compressed (20 MB, `assets/lq_pak*.xz`) and unpacked when Quake starts. A `pak0.pak` (and `pak1.pak`) from the original game in Files is used instead. The kernel image grows to about 64 MB, inside both boot loaders' 100 MB limit and below the BSS at 256 MB. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | - | Controls: WASD or arrows, mouse aim with mouse look after a click in the window (Esc frees the mouse), left click or Ctrl fires, Space jumps, Shift runs, number keys pick weapons, Esc opens the menu. Quake output goes to the kernel log with the prefix `quake:`. Sound is not connected yet. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: the engine and the freestanding blob on the build host with LibreQuake (start map, 300 frames, pictures checked); QEMU legacy BIOS and UEFI (Quake loads the start map, renders, walks with the keyboard, scales to the window). Mouse look and speed on the laptop are untested until they run there.

## What changed in v9.78

| # | Before (v9.77) | Now (v9.78) | Where |
|---|---|---|---|
| 1 | No word processor; `.docx` files could not be opened. | Writer (`apps/office/writer.c`, `writer [file]` in the terminal, start menu Productivity): A4 pages with 1-inch margins on a grey desk, word wrap, page breaks and page numbers, a blinking cursor, mouse and Shift selection (with drag and auto-scroll), the wheel, Home/End, Page Up/Down and Ctrl+arrows by word. Three font families (Sans like Arial, Serif like Times New Roman, Mono like Courier New) in 13 sizes from 8 to 48 pt, bold, italic, underline, strikethrough, 8 text colours, left/center/right/justified paragraphs, Heading 1, Heading 2 and Normal, pictures (PNG and JPEG from Files, scaled to the page), undo (24 steps), word count, Save, Save as and Open dialogs, and keyboard shortcuts (Ctrl+B/I/U, Ctrl+E/L/R/J, Ctrl+S, Ctrl+O, Ctrl+N, Ctrl+Z, Ctrl+A). | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | Only the 8x14 bitmap font. | Real fonts for Writer: Liberation Sans, Serif and Mono (metric-compatible with Arial, Times New Roman and Courier New, SIL Open Font License) pre-rendered by `tools/gen_wpfont.py` in regular, bold, italic and bold italic at 13 sizes for Latin-1 (including the Swedish letters a-ring, a-umlaut and o-umlaut), stored as 4-bit anti-aliased glyphs (`assets/wpfont.c` metrics, `assets/wpf_data.bin` 5.5 MB linked as `wpf_data.o`). | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | - | Word format: Writer saves real `.docx` files (Office Open XML in a stored ZIP: `[Content_Types].xml`, relationships, `word/document.xml` with runs, fonts, sizes, bold, italic, underline, strike, colours, alignment and inline pictures in `word/media`). It opens `.docx` files from Word and other programs (paragraphs, runs, alignment, tabs, line breaks, fonts, sizes, colours, Title/Heading/Subtitle styles, pictures through the relationships file) and plain `.txt`. Opening a `.docx` in Files opens Writer. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | Ctrl shortcuts could arrive as plain letters when Ctrl was released before the app read the key. | The key queue records Ctrl and Shift with every key at the moment it is pressed (`g_key_ctrl`, `g_key_shift`). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: QEMU legacy BIOS and UEFI (typing, formatting, alignment, selection, shortcuts, saving); the saved file passes a ZIP check in NoovexOS Python, is read correctly by python-docx and lays out the same in LibreOffice; a `.docx` made by python-docx with a heading, Times, Courier, colour, right alignment, Swedish letters and a picture was opened by Writer's code and saved again without losing anything (host test harness around `apps/office/writer.c`), and the page drawing was checked in the same harness.

## What changed in v9.77

| # | Before (v9.76) | Now (v9.77) | Where |
|---|---|---|---|
| 1 | Volume, mute, media and other special keys did nothing. | The PS/2 keyboard driver recognises the extended (E0) keys: volume up and down, mute, play/pause, next, previous, stop, calculator, web browser, mail, my computer, sleep, media select and the browser keys. They go to a 32-entry hotkey queue (`hk_push`) that the desktop handles (`graphics/ui/hotkeys.c`): volume in 6% steps, mute remembers the old volume, media keys control the Video Player, calculator, browser and Files open their apps. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Brightness and volume changes showed a small text toast. | A Windows-style on-screen display: a glass panel at the bottom with an icon (speaker with level waves or a cross when muted, sun, microphone, plane, camera, screen, touchpad) and a level bar with the value, for volume, brightness, microphone mute, airplane mode, camera, display switch and touchpad. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | On Lenovo IdeaPad laptops the special Fn keys reported through the VPC2004 ACPI device were ignored. | The IdeaPad VPC events are read like Linux `ideapad-laptop` does (`VPCW`/`VPCR`, registers 0x10 and 0x1A): display switch, camera, microphone mute, airplane mode, touchpad on/off (state from register 0x1B) and firmware brightness (registers 0x11 and 0x12) all work. ACPI video brightness events also show the new display, and display-switch events (0x80, 0x82) are handled. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | Only Win+R and Win+E did something. | Win+L locks (login screen), Win+P shows the display choice, Win+I opens Settings, Win+A opens quick settings. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 5 | No way to see what a key sends. | `keys` in the terminal lists the last 16 special keys and laptop events (E0 scan codes, unknown keys, ACPI events and Lenovo VPC bits), so a key that still does nothing can be reported and added. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: QEMU legacy BIOS with the QEMU volume up, mute and calculator keys (on-screen display, muting, Calculator opens) and the `keys` list. The Lenovo VPC events and real Fn keys are untested until they run on the laptop.

## What changed in v9.76

| # | Before (v9.75) | Now (v9.76) | Where |
|---|---|---|---|
| 1 | The HD Audio driver only played sound. | Recording. The driver finds the input path from an analog-to-digital converter to a microphone pin (built-in microphone first, then a microphone jack, then line in) through mixers and selectors, sets the connection selects, microphone bias (VRef 80% or 50%), pin boost and input amplifiers, and runs the first input stream (stream tag 2, 48 kHz, 16-bit stereo, 32 x 4 KB buffer list). `struct nvx_snd` gained `rec_start`, `rec_read`, `rec_stop`, `rec_gain` and `rec_name`. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | No capture path in the sound core. | `core/sound.c` moves recorded audio from the driver into a 1 MB buffer every 20 ms (`snd_poll`), tracks the input level and counts dropped data. `snd_rec_start`, `snd_rec_stop`, `snd_rec_gain`, `snd_cap_read`, and `audio mic <0-100>` in the terminal. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | `/dev/dsp` could only play. | `/dev/dsp` records too: `read()` returns 48 kHz 16-bit stereo, and the OSS calls `SNDCTL_DSP_GETISPACE`, `GETBLKSIZE`, `GETCAPS` (duplex), `SETTRIGGER` and `SETDUPLEX` are answered, so Linux programs can record (tested with CPython reading `/dev/dsp`). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | No recording app. | Voice Recorder (`apps/media/recorder.c`, `recorder` in the terminal): a big record button (Space also starts and stops), timer, live waveform, input level meter, microphone volume slider, play and pause with a moving play head, click the waveform to jump, and every recording saved as `recording-N.wav` (48 kHz mono) in Files. Settings > System > Sound has a new Input card with the microphone, its volume and a level test. The app is in the Internet & Media and Drivers & Hardware start menu groups. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: QEMU legacy BIOS and UEFI with an emulated HD Audio codec (line in): recording runs at the right speed, the WAV file is saved, Python reads 96,000 bytes from `/dev/dsp`, and the Settings input card works. QEMU only gives silence on its input, so the real microphone level and sound quality are untested until they run on the laptop. On laptops whose built-in microphone is wired to the Intel audio DSP instead of the HD Audio codec, the microphone is not found yet.

## What changed in v9.75

| # | Before (v9.74) | Now (v9.75) | Where |
|---|---|---|---|
| 1 | A USB camera was listed in Task Manager as "Camera" but nothing could use it. | USB Video Class (UVC) camera driver inside the xHCI driver (`drivers/usb/xhci.c`, API in `include/nvx/drivers/usb/uvc.h`). When a device has a Video interface, the full configuration descriptor (up to 16 KB) is read and parsed: Video Control and Video Streaming interfaces, YUY2, NV12 and MJPEG formats with their frame sizes, and the isochronous IN endpoints of each alternate setting (with high-bandwidth packets and SuperSpeed companion descriptors). Starting the stream: probe and commit (`VS_PROBE_CONTROL`, `VS_COMMIT_CONTROL`, 26, 34 or 48 bytes by UVC version), the smallest alternate setting that carries the camera's payload size, a Configure Endpoint command for an isochronous IN endpoint and `SET_INTERFACE`. Transfers use a 1000-entry isochronous ring with "start as soon as possible" and their completions go to a second event ring on interrupter 1, so mouse, keyboard and USB stick traffic is not disturbed. Payload headers (frame id, end of frame, error bit) assemble frames; YUY2 and NV12 are converted to RGB, MJPEG frames get the standard Huffman tables when the camera leaves them out and are decoded by the JPEG decoder. If a size is refused (for example not enough USB bandwidth) the next size is tried. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | No camera app. | Camera app (`apps/media/camera.c`, `camera` in the terminal): live picture, Start/Stop, SD (YUY2 up to 640x480) or HD (MJPEG up to 1280x720), mirror, Take photo (saves `photo-N.png`; Space also takes a photo) and a details panel with the camera name, USB id and speed, every supported size, frames per second, dropped frames, USB packet errors and the last completion code. Without a camera it lists the USB devices that were found. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | No benchmark. | Benchmark app (`apps/system/bench.c`, `bench cpu` or `bench gpu`). CPU test: four single-core passes counting primes up to 100,000 (must be 9,592), 16 million integer hash steps (checked against a known result), a 64 MB memory write, copy and read-back check, all other cores counting primes up to 150,000 at once (must be 13,848 each) with the speed-up over one core, the clock under load from APERF/MPERF and the temperature from the digital thermal sensor. GPU test: fill, blend and copy speed on a 1024x1024 GPU surface (three passes each), a correctness check of GPU-drawn pixels against the expected values, the GPU clock under load and the chip temperature. The verdict is Healthy, Works with warnings (speed changing between passes, low clock, heat, throttling since power-on, poor core scaling) or Problem found (any wrong answer, memory errors, a core not finishing, wrong GPU pixels, GPU falling back to the CPU). | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | The start menus had four groups. | A fifth group, Drivers & Hardware, with Camera, Benchmark and Task Manager, in both the modern and the classic start menu. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: QEMU legacy BIOS and UEFI boot; the CPU test (healthy, 4 cores) and the GPU test with the queue emulated on the CPU (all pixels correct); the Camera app without a camera and with a QEMU USB keyboard, which still works. QEMU has no USB camera and no Intel graphics, so the camera stream and the GPU test on the real chip are untested until they run on the laptop.

## What changed in v9.74

| # | Before (v9.73) | Now (v9.74) | Where |
|---|---|---|---|
| 1 | Nine built-in wallpapers, five of them made for NoovexOS. | Exactly six wallpapers, the pictures supplied by the owner: Porsche, Crane, Wireframe, Peaks, Earth and Yosemite (`THN[NWALL]`, `NWALL` is 6). Each is cover-cropped to 1920x1080 with Lanczos; the three small originals (Peaks, Earth, Yosemite) are lightly denoised and sharpened. The default is Crane (`g_theme=1`). `wallpaper porsche|crane|wireframe|peaks|earth|yosemite` or `wallpaper 0-5` sets one from the terminal. The depth blur toward the top is softer so photos stay sharp. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | The drifting light was off by default. | Every graphics option is on by default: GPU drawing (when the Intel driver is ready), visual effects, partial redraws, wallpaper effects and the drifting light (`g_wall_light=1`). Each can still be turned off in Settings > System > Display > Graphics. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | DOOM only started when the BIOS loader found `/doom/DOOM1.WAD` or a FAT disk held a WAD, so on the release ISO it showed "Could not load the WAD". | The shareware `DOOM1.WAD` (id Software, freely redistributable) is built into the kernel (`assets/doom1.wad`, linked by `ld -r -b binary` as `doom_wad.o`), so DOOM works on BIOS and UEFI with no disk. A full WAD you own on a FAT disk is used first (`DOOM2.WAD`, `DOOM.WAD`, `FREEDOOM.WAD`, `FREEDOO1.WAD`), then the boot module, then the built-in one; the WAD is registered under its real name so DOOM II is detected. The `doom` terminal command opens it (also from the text console) and prints the controls; the window title shows them too. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: QEMU legacy BIOS (1280x800) and UEFI (1920x1080) boot; `doom` from the console starts the game with the built-in WAD on both. QEMU has no Intel graphics, so the drifting light on the GPU is untested until it runs on the laptop.

## What changed in v9.73

| # | Before (v9.72) | Now (v9.73) | Where |
|---|---|---|---|
| 1 | On the Lenovo the taskbar was a blank strip. The frame check showed the picture in memory was right; it appeared with `gpu flip off` and with `gpu draw off`, so the GPU shader copy to the screen lost the bottom rows. | The picture is copied to the screen by the GPU blitter, only the changed area, with a CPU cache flush first when the blitter does not snoop. The shader copy is still available (`gpu present shader`, or Settings > System > Display > Graphics > Advanced graphics) and the render engine now also flushes its render cache at the end of every batch. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The built-in wallpapers were 640x360 JPEGs (about 12 KB) stretched three times, so they looked blurry and blocky. | Nine wallpapers at 1920x1080: Aurora, Mountains, Waves, Sunset and Crane glow are made for NoovexOS; Crane, Porsche, Wireframe and Peaks are the pictures supplied by the owner, scaled with Lanczos. The JPEG decoder accepts pictures up to 1920x1200. Smaller pictures (for example a custom wallpaper) are enlarged with Catmull-Rom (bicubic) filtering instead of bilinear. `wallpaper <name>` sets one from the terminal. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | Task Manager showed one process list with fixed numbers. | New Task Manager (`apps/system/taskmgr.c`) with four tabs: Processes (open apps, status, End task, kernel services), Performance (CPU from the scheduler's idle counters on cores 1 and up, memory, GPU busy time and clock, disk, network, 60-second graphs), Devices (Processor, Graphics, Memory, Power supply and battery with health and cycles, Storage, Network, Sound, USB, Display, Input and all PCI devices, opened by clicking, scrolled with the wheel) and Services. `taskmgr` opens it. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | Settings had one flat list of nine categories. | Windows-style Settings: categories (System, Personalization, Network & internet, Bluetooth & devices, Apps, Accounts, Time & language, Privacy & security, Update & recovery) open pages of tiles, tiles open sub-pages and some of those open further pages (System > Display > Graphics > Advanced graphics). A clickable path and a back button at the top. New pages: Graphics (GPU drawing, effects, partial redraws, wallpaper effects, drifting light), Advanced graphics (GPU details, screen copy engine, tear-free, frame check), Sound (volume, mute), Network details, Mouse & pointer, Installed apps, Your info, Date & time, Language & region, Recovery. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

Tested: QEMU legacy BIOS and UEFI (1920x1080) boot, the desktop, every new wallpaper, Task Manager tabs and Settings pages; host GPU emulator tests. QEMU has no Intel graphics, so the blitter screen copy with partial areas is untested until it runs on the laptop.

## What changed in v9.72

| # | Before (v9.71) | Now (v9.72) | Where |
|---|---|---|---|
| 1 | Every desktop frame rebuilt the whole screen (wallpaper copy, every window, shadows, glass) even when one line of text changed. On the Lenovo that was 21 ms per frame. | The operations that draw the screen are recorded instead of run, compared with the previous frame's list (lined up so inserted or removed operations do not shift the rest), and only the area that really changed is rebuilt, in order, clipped to that area. Scratch buffers used by the glass blur are re-run only where their result is needed; persistent buffers such as the blur cache are kept in step. A frame where nothing changed is skipped. The copy to the cursor-free buffer and the GPU screen copy cover only the changed area (with tear-free double buffering, the area changed since each buffer was last written; a full copy at least once a second). If anything touches the screen directly while it is being drawn, that frame is rebuilt in full. `gpu partial off` turns this off for comparison. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The taskbar showed as a blank light strip on the Lenovo. In QEMU it was correct, so the cause could not be found from here. | `gpu check` (and once automatically at desktop start) draws one frame on the GPU and the same frame on the CPU and compares every pixel. `gpuinfo` shows the result: how many pixels differ, where, the GPU and CPU values and the last operation that drew the first wrong pixel. If the frames differ, the check runs again with a wait between every operation; if that fixes it, drawing stays in that mode. The idle clock update on the GPU now goes through the normal (partial) frame instead of a separate taskbar path. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | Wallpapers looked the same as with CPU drawing. | Wallpaper effects, made once when the wallpaper is set: colour bands removed (smooth areas are averaged in higher precision and dithered), a soft glow where bright areas meet darker ones, a soft depth blur toward the top, a light vignette and a gentle light from the upper left. `gpu wallfx off` shows the plain picture. Optional drifting light drawn by the GPU every frame (`gpu light on`, off by default because it redraws part of the screen ten times a second). | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | Parts of the mouse pointer could stay behind on the screen; the pointer area was smaller than the pointer. | The pointer area covers the whole pointer. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 5 | Text drawn by a window in one frame was one operation, so typing one letter changed the whole window. | Text is batched in bands of at most 40 rows, so a change redraws only its band. The Welcome heading now fits its window. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: host GPU emulator (partial screen rebuild equals a full rebuild in 24,000+ random frames with glass chains, caches, layers, text and outside writes, on the CPU and the emulator with delayed writes); QEMU legacy BIOS and UEFI boot, the desktop with CPU drawing and with the queue emulated, including a build that compared every partial frame with a full rebuild during typing, scrolling, dragging and window switching. QEMU has no Intel graphics, so the partial GPU screen copy and the frame check on real hardware are untested until they run on the laptop.

## What changed in v9.71

| # | Before (v9.70) | Now (v9.71) | Where |
|---|---|---|---|
| 1 | The GPU ran at whatever clock the firmware left it at. | While the desktop is drawn by the GPU, the clock request is raised to the maximum the chip allows (read from `RP_STATE_CAP`); it is put back when GPU drawing stops. `gpuinfo` shows the current, requested and allowed frequencies. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Every text run was its own operation, with an ordering wait before most of them (284 waits per frame in the photo). | All text a window draws in one frame is collected and drawn with one masked operation (QEMU: 5 text operations for 556 characters, 0 ordering waits). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | Every window was redrawn completely every frame, and shadows and glass blur were recalculated every frame. | Each window's operations are recorded and compared with the previous frame; only the changed area is redrawn, and an unchanged window is not drawn. Shadows come from a precomputed 9-piece image (8 operations instead of one per pixel row). The glass blur is cached per window and reused until the window moves, resizes, or what is behind it changes. In QEMU a desktop frame went from 467 to about 311 operations. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | The wallpaper was scaled by picking the nearest pixel (blocky edges). | Bilinear scaling (smooth). This is done once on the CPU when the wallpaper or screen size changes, not with a new shader: a shader would need a per-pixel gather that the existing shaders do not do and that could not be verified on the hardware before release. It costs nothing per frame. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 5 | `gpuinfo` showed one "last frame" time. | The frame is split into CPU recording time, GPU drawing time and screen copy time, with submissions, operations, ordering waits, text operations, windows redrawn (full, partly, unchanged, by the CPU), blur cache hits and misses, and fast and slow shadows. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: host GPU emulator (partial redraw gives exactly the same pixels as a full redraw in 300 random cases, on the CPU, the emulator and the emulator with delayed writes; self-test with 4000 random operations); QEMU legacy BIOS and UEFI boot, desktop with CPU drawing and with the queue in emulated mode. QEMU has no Intel graphics, so the clock and the real GPU path are untested until they run on the laptop.

## What changed in v9.70

| # | Before (v9.69) | Now (v9.70) | Where |
|---|---|---|---|
| 1 | `gpuinfo` on the Lenovo showed "subslices 3 x 5 EUs" and the render context asked for 5 EUs per subslice. | The EU fuse byte per subslice (0x9134) has 8 slots; on Gemini Lake slots 3 and 7 do not exist and read as disabled (0x88). EUs are now counted as 8 minus the set bits, as Linux does: 3 x 6 = 18 EUs, all powered. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The boot self-test failed on the Lenovo ("wrong pixel: blur h op 15 146,90"): replayed on the host, op 13 (an "over" copy) had read its source 32 bytes too early, from the only test image whose rows were not a multiple of 64 bytes (544 bytes). The host emulator gave the right value, so the difference is hardware behaviour. | Test images now have rows padded to a multiple of 64 bytes (the screen and window buffers already were). Every operation is preceded by `MEDIA_STATE_FLUSH` before its constant data and interface descriptor are loaded. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | A self-test mismatch turned GPU drawing off with only the last operation of the batch named. | The self-test keeps a copy of the images before each batch of 16 operations. On a mismatch it restores them and runs the batch again one operation per submission, comparing after each. If one at a time is correct, GPU drawing stays on in a slower mode with a wait between every operation; otherwise the first wrong operation is named. `gpuinfo` prints both results (operation, image, pixel, value read and expected). The host emulator now also holds back every GPU write until the next pipeline stall, which models overlapping operations; the queue passes with it, and planted faults produce the expected diagnosis. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session and the desktop.

## What changed in v9.69

| # | Before (v9.68) | Now (v9.69) | Where |
|---|---|---|---|
| 1 | The desktop was drawn by the CPU; the GPU only copied the finished picture to the screen. | On Intel gen 9 the GPU draws everything by default: 12 drawing shaders (fill, blend, masked colour for text and rounded corners, copy, masked copy, horizontal and vertical gradients, night-light tint, horizontal and vertical blur, premultiplied "over" with and without a mask), a draw queue that batches operations and inserts ordering waits only where two operations touch the same 8x8 blocks, and a drawing layer that turns rectangles, rounded rectangles, circles, shadows, text runs, gradients, wallpaper and single pixels into queue operations. Every CPU access to the screen buffer first waits for queued GPU work, so code that still draws pixels itself stays correct. Machines without Intel gen 9 graphics keep CPU drawing exactly as before. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Moving or raising a window redrew its contents. | GPU compositor: every window draws into its own full-screen layer (premultiplied alpha). Each frame the layers are combined on the GPU with soft shadows, rounded corners, a translucent blurred title strip, transparency and glass behind the window. Dragging a window only re-combines its layer; its contents are not drawn again. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | No GPU effects. | Blurred glass behind the terminal, menus and panels, open/close/minimise animations (fade, slide, fall), smooth scrolling in the terminal and File Explorer (old and new contents slide over each other for 140 ms), all done by the GPU. `gpu fx on|off` switches the effects. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 4 | `gpuinfo` showed "3 x 5 EUs" without the fuse values; the screen was always copied by the blitter. | `gpuinfo` prints the raw fuse registers (0x9120 and 0x9134). The CPU, blitter and shader screen copies are timed at start-up and the fastest one is used. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 5 | - | Safety: a self-test at boot runs 400 random operations on all shaders and compares every pixel with the CPU result, plus tests that the CPU sees what the GPU drew and the GPU never reuses stale cached data. If any test fails, or a drawing batch does not finish within 250 ms, GPU drawing turns off for the session and the CPU draws. `gpu draw off`, `gpu draw on`, Ctrl+Alt+G and `gpu draw emu` (the same queue run by the CPU, for testing on machines without Intel graphics). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested: every shader in a host-side GPU emulator that executes the exact command batches and instruction bits (thousands of random operations, pixel-exact against the CPU); all shaders checked against Intel's assembler (IGA); QEMU legacy BIOS and UEFI boot, the desktop with CPU drawing, and the full queue and compositor in emulated mode (glass terminal, dragging, scrolling, switching modes). QEMU has no Intel graphics, so the real GPU path is untested until it runs on the laptop.

## What changed in v9.68

| # | Before (v9.67) | Now (v9.68) | Where |
|---|---|---|---|
| 1 | On the Lenovo the blitter stopped at step 9: the GPU read the same old pixel (0x005B6198) with and without a CPU cache flush, so it was reading a different copy of the screen buffer than the CPU wrote, not a stale cache line. The buffer was an 8 MB array in the kernel image, in an area that start-up can move to other memory. | The screen buffer the desktop draws into (`SCENE`) is now a pointer. On gen 9 the GPU driver allocates it from the kernel heap, maps every page into the GPU's tables itself, copies the picture over and switches `SCENE` to it. Read tests on three buffers (test buffer, new screen buffer, old kernel array), each flushed and unflushed, show in `gpuinfo` which memory the GPU reads correctly, with the physical addresses and whether the old array was moved at boot. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The render engine was started but ran no programs. | Stage 2 of GPU drawing: a small assembler for native gen 9 GPU instructions (`drivers/video/intel/gen9asm.c`) and the first two compute shaders, a gradient fill and a block copy, run on the render engine through `GPGPU_WALKER` with surface states, binding tables, interface descriptors and constant data. Results are checked pixel by pixel (256x128 test images, then a full-screen gradient that is never shown). `gpuinfo` shows "Shaders: ready" with the times, or the step, the first wrong pixel and the reason. The render context now asks for all subslices and EUs to be powered, as Linux does on Apollo Lake and Gemini Lake. Both shaders were checked bit for bit against Intel's own assembler. QEMU has no Intel graphics, so only the fallback was tested. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session and the desktop.

## What changed in v9.67

| # | Before (v9.66) | Now (v9.67) | Where |
|---|---|---|---|
| 1 | On the Lenovo (Gemini Lake) the blitter passed its fill and copy tests (steps 7 and 8) but stopped at step 9: the GPU read old memory instead of what the CPU had just drawn. `gpuinfo` showed the firmware's GPU memory types as 0x0303030303030303 (no snooping), but not whether the new values were taken. | `gpuinfo` shows the memory types before and after they are set and the wrong value read. If the GPU does not snoop, the test is repeated after flushing those lines from the CPU caches; if that works, every GPU screen copy flushes the scene first. The CPU and GPU copy times (the GPU time including the flush) decide which one draws. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | `gpu blit gpu` printed "no NVIDIA GPU found": the older NVIDIA `gpu` command came first in the shell. | `gpu`, `gpu flip ...`, `gpu blit ...` go to the Intel driver; the NVIDIA tool answers to `nvidia` and to other `gpu` subcommands when an NVIDIA card is present. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | Only the blitter engine was used. | Stage 1 of GPU drawing: the render engine is started with the same execlist submission (23-page render context, `PIPE_CONTROL` completion), runs a store test, selects the compute pipeline, sets the state base addresses and the compute front end. `gpuinfo` shows "Render engine: ready" or the step and reason where it stopped. The submission code is now shared by both engines, and after the first run a context restores the state the hardware saved. QEMU has no Intel graphics, so only the fallback was tested. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session and the desktop.

## What changed in v9.66

| # | Before (v9.65) | Now (v9.66) | Where |
|---|---|---|---|
| 1 | On the Lenovo (Intel Gemini Lake, UHD 605) the screen was copied by the CPU only (2.1 ms per frame with write-combining and tear-free drawing on, per `gpuinfo` on the laptop). | The GPU's blitter engine is started on gen 9 (Skylake to Comet Lake, Apollo Lake, Gemini Lake): forcewake, private PAT on Apollo/Gemini Lake, a page table for the GPU, a logical ring context, execlist submission, fill and copy self-tests and a test that the GPU sees the CPU's latest drawing. The CPU and GPU full-screen copies are timed and screen updates use the faster one. Every stage reports a step number and reason in `gpuinfo` if it stops; then drawing stays on the CPU. `gpu blit gpu` and `gpu blit cpu` choose by hand. QEMU has no Intel graphics, so only the fallback was tested. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The touchpad's interrupt pin was mapped directly only for Sunrise Point/Kaby Lake GPIO (`INT344B`); on Gemini Lake it fell back to learning or polling. | Apollo Lake (`INT3452`) and Gemini Lake (`INT3453`) GPIO controllers are one ACPI device per community, so the `GpioInt` pin number is the pad number inside that community; it is mapped directly and checked (GPIO mode, input enabled). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session and the desktop.

## What changed in v9.65

| # | Before (v9.64) | Now (v9.65) | Where |
|---|---|---|---|
| 1 | This PC showed one tile for the system and one for a USB stick ("USB Drive (E:)"), with sizes estimated from the files. | A This PC page modelled on Windows 11: tiles for every volume on every disk and on the USB stick with drive letters (C: is the system, then D:, E: ... in disk order), volume labels, usage bars that turn red above 90 percent, real free space for FAT12/16/32, NTFS, exFAT and ext2/3/4, a command bar (Properties, Open, Refresh, Open Settings), the drives under This PC in the side bar, item and selection counts, and a Properties dialog with a used/free ring chart. EFI, Microsoft reserved, recovery, BIOS boot and swap partitions are hidden, as in Windows. Win+E opens File Explorer, and the Win key no longer types its letter into the focused window. Tested in QEMU with a SATA disk (NTFS "Windows", ext4 "Data", FAT32), an NVMe disk with GPT (EFI system plus FAT32) and a USB stick: all volumes appear with the right labels and free space, and the EFI partition is hidden. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | Legacy BIOS boot always asked for 1280x1024 (`video=1280x1024`), so a laptop panel showed a stretched, blurry picture. | `video=auto` (now the default in the ISO and on installed disks) reads the screen's EDID through VBE 4F15h and sets the panel's native mode when the BIOS offers it (up to 1920x1080), then falls back through 1920x1080, 1600x900, 1366x768, 1280x1024 and smaller. Tested in QEMU with a 1600x900 EDID: the session started at 1600x900. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| 3 | The framebuffer was mapped with the default caching, which firmware MTRRs normally make uncached, so every pixel write was a separate bus write. | The page attribute table gets a write-combining entry on every CPU and the framebuffer pages use it, so screen writes go out in bursts. `gpuinfo` shows the mapping and the time of a full-screen copy. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | Intel graphics were only used through the firmware framebuffer. | New Intel driver (`drivers/video/intel/igpu.c`): finds the GPU (Sandy Bridge to Meteor Lake), reads the panel's native size, the current mode, whether the picture is scaled, and the display plane. On gen 8 to 12 it adds a second screen buffer in the graphics address table and switches between the two at the screen's vertical blank, so a frame is never shown half drawn (tear-free). It checks the plane format, that the framebuffer is the visible surface, that the new buffer keeps data and that the display really switches, and falls back to normal drawing otherwise; `gpu flip off`, `gpu flip on` and Ctrl+Alt+F switch it by hand. New `gpuinfo` command. QEMU has no Intel graphics, so this part was only tested for its fallback. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session and the desktop.

## What changed in v9.64

| # | Before (v9.63) | Now (v9.64) | Where |
|---|---|---|---|
| 1 | On the Lenovo (ELAN469D) the touchpad was read every 10 ms because the Intel GPIO interrupt pad was never set up (`tpinfo` had no "Intel GPIO interrupt" line). | `igp_setup` dereferences a `_CRS` that returns a reference, records why it failed (reason code in `tpinfo`), and maps the ACPI `GpioInt` pin directly to its pad for Sunrise Point/Kaby Lake (`INT344B`: communities 0-47, 48-119, 120-151, in `_CRS` order). The pad must be in GPIO mode with RX enabled (checked in PADCFG DW0); then the pin is read and reports are fetched only when the pad asserts it, up to 6 per call. Other Intel controllers still use the learning method. Without a pin, polling now reads up to 4 reports per 10 ms tick and stops at an empty or repeated report. `tpinfo` shows reports per second. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Hybrid-mode pads (one finger per report, Contact Count only in the first report) could mix two frames: the first report of a new frame was added to the unfinished one, and a lifted finger delayed the frame. | A report with Contact Count > 0 starts a frame; it needs ceil(count / fingers per report) reports; reports with count 0 continue it. An unfinished frame is sent when the next one starts (counted as incomplete). Lifted fingers count toward completion. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | Taps were recognised, but there was no tap-and-drag, no software button areas, no palm or typing rejection, and acceleration depended on the report rate. | Gesture engine (`tp_gesture.c`, used by all touchpad drivers): tap-to-click (1 finger left, 2 right, 3 middle; at most 230 ms and 3 mm); tap-and-drag and double tap (a touch within 260 ms after a tap holds the left button); clickpad zones (a press in the bottom 12 mm clicks left or right by half; a thumb resting there does not move the pointer or turn one-finger motion into scrolling); palm rejection (contacts with Confidence 0 are dropped and cancel taps for that touch); typing rejection (taps and motion are ignored for 400 ms after a key on the PS/2 or USB keyboard); pointer ballistics by speed in mm/s from the high-resolution timer (about 4 px/mm below 25 mm/s rising to 16 px/mm at 250 mm/s, scaled by the speed setting); two-finger scrolling faster on quick strokes. New `touchpad` command: `tap`, `speed 1-10`, `scroll natural/classic`, `zones`, `typing`. Checked on the host with simulated touches (tap, drag, double tap, two-finger tap and scroll, zone clicks, resting thumb, typing, palm). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | Windows could only be resized from an 18-pixel square in the bottom-right corner. | All four edges and corners resize (6 px outside and 4 px inside the frame, corners 14 px), with a two-headed arrow cursor while hovering or resizing. Minimum 240x160. Maximised windows are not resized. Tested in QEMU: dragging the left edge of the Terminal widened it from 90 to 102 columns with the right edge fixed. | [section 9](#part-11-desktop-applications-shell-and-scripting) |
| 5 | The installed boot partition held only `boot/`, `boot.cfg`, `KERNEL64.BIN` and `EFI/BOOT/`. | It is laid out like a Windows system drive, with long file names: `EFI/BOOT/BOOTX64.EFI`, `EFI/NoovexOS/` (copy of the loader and `boot.cfg`), `NoovexOS/System/` (`Drivers`, `Config`, `version.txt`), `NoovexOS/Boot/`, `NoovexOS/Fonts`, `Logs`, `Temp`, `Program Files`, `ProgramData`, `Users/Public`, `Users/Default/` (Desktop, Documents, Downloads, Pictures, Music, Videos) and `README.txt`. `boot/`, `boot.cfg` and `KERNEL64.BIN` keep their names (the loaders need them) and are marked hidden and system. The root directory now has 4 clusters. Checked on the host: the image from this code passes the repository FAT checker (`tools/tests/fatck.py`) with valid long-name checksums and no cross-linked clusters. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |

Tested in QEMU: legacy BIOS and UEFI boot to the live session.

## What changed in v9.63

| # | Before (v9.62) | Now (v9.63) | Where |
|---|---|---|---|
| 1 | On a Lenovo laptop with an ELAN469D touchpad (vendor 04F3, product 3081, 381-byte report descriptor) `tpinfo` showed "report desc not understood". `tp_parse` stopped at the first Main item with a Report Size above 64 or a Report Count above 1024, and at the first item that ran past the end of the data, so one vendor report or one damaged byte rejected the whole descriptor. | `tp_parse` skips Main items it cannot use (Report Size outside 1..32 or Report Count above 4096) and still counts their bits, stops at a truncated item and keeps what it found before it, and tracks collection nesting errors. It records the item count, the skipped items, the first skipped byte and the cut-off byte. Tested on the host: the 11 Linux self-test touchpad descriptors give the same results as before; the same descriptors with a vendor feature report of 4096 bytes in front (rejected by v9.62) and cut to two thirds are recognised. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | The report descriptor was read once at the bus speed from `_CRS`. A long read that arrived damaged could not be told apart from a descriptor the parser did not accept. | It is read twice and compared (up to 4 attempts). If no two reads match, the read fails, or no touchpad or mouse layout is found, the bus is set up again at 100 kHz and the descriptor is read and parsed again. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | The ELAN protocol fallback for HID pads with vendor 0x04F3 never ran: every later descriptor register that failed wrote its invalid values into `t->d`, so the saved vendor was no longer 0x04F3. | `ih_get_desc` writes `t->d` only after the descriptor is valid. After a valid descriptor no other register is tried at that address. Any device whose `_HID` starts with `ELAN` is a candidate and gets the ELAN protocol if HID setup fails. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | `tpinfo dump` and `tpinfo scan` printed the plain `tpinfo` report: the shell compares only the first word. The descriptor buffer could also be overwritten by the ELAN probe. | `tpinfo scan`, `tpinfo dump` and the new `tpdump` work. The last descriptor read is copied to its own buffer and printed in hex (32 bytes per line) with the vendor, product, address, register, whether the two reads matched, the bus speed, the parser counts and the layout found. `tpinfo` lines also show the read check, speed, item count, skipped items, cut-off byte and layout. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 5 | The `_CRS` address was put in front of the static resource list that already held it, so the same address was tried twice. | Duplicates of the `_CRS` entry (same address, same controller path) are removed. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU: legacy BIOS boot to the live session. QEMU has no I2C touchpad, so the new probe paths are checked on the laptop with `tpinfo` and `tpdump`.

## What changed in v9.62

| # | Before (v9.61) | Now (v9.62) | Where |
|---|---|---|---|
| 1 | Every ACPI method added in v9.54-v9.56 checked `g_aml_ok`, which nothing ever set. `_CRS`, `_DSM`, `_PS0`, power resources, `SSCN`/`FMCN`, the Intel GPIO pad table and all Lenovo/ASUS laptop functions (hotkeys, keyboard light, charge limit) therefore never ran. Found through `tpinfo` on a Lenovo laptop ("_CRS not used, _DSM HID reg none, SSCN none, FMCN none"). | `g_aml_ok` is set when the AML interpreter has started (`acpi_aml_init`). Tested in QEMU: a method call through the touchpad code returns `_HID` of `\_SB.PCI0` as "PNP0A08". | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Touchpads whose `_HID` is a method or an EISA number showed "?" and ELAN pads listed only as PNP0C50 were never driven with the ELAN protocol. | `_HID` is evaluated (string or compressed EISA ID). Any `ELAN06xx` ID matches the ELAN list. ELAN pads are tried with the ELAN I2C protocol first and HID over I2C second, as Linux does; a HID pad whose descriptor reports vendor 0x04F3 (ELAN) also gets the ELAN protocol if HID setup fails. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | After the first HID descriptor register that answered, a failed setup ended the attempt at that address. | Each descriptor register (the `_DSM` one first, then 0x20, 0x01, 0x00, 0x10, 0x30) gets the full setup; an address that does not acknowledge is skipped at once. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | `tpinfo` showed only the last failure. | One line per attempt: address, protocol, descriptor register, vendor and product, report descriptor length and the exact failure (no device, descriptor invalid, report descriptor not read, not understood, no touchpad or mouse layout, ELAN step). New `tpinfo dump` prints the report descriptor in hex. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

The HID report descriptor parser was checked on the host against the 11 precision-touchpad descriptors in the Linux kernel HID self-tests (including ELAN 04f3:300b, 04f3:3045 and 04f3:313a): all are recognised as precision touchpads with correct ranges and sizes.

## What changed in v9.61

| # | Before (v9.60) | Now (v9.61) | Where |
|---|---|---|---|
| 1 | NoovexOS uses fixed physical addresses: boot stack and disk/USB buffers at 0x09800000-0x09C80000, the module area at 0x0E000000 and `.bss` from 0x10000000 (about 107 MB). On a Lenovo laptop part of that range is not usable RAM. Evidence: under UEFI a boot page-table entry at 0x10002440 read back as all ones (page fault, error 0xB, reserved bits set); under legacy BIOS the kernel warned "the firmware uses memory inside a kernel area" and then jumped to 0xFFFFFFFFFFFFFFFF (page fault at RIP and CR2 all ones), the value memory returns when nothing keeps it. Writes there survive only while they stay in the CPU cache. | `early_remap()` runs first in `kmain`, before `.bss` is cleared. It reads the firmware map the loader left at 0x7000 (UEFI summary or E820) and checks every 2 MB page of 0x09800000-0x09E00000 and 0x0E000000 up to the end of `.bss`. Every page that is not fully inside usable RAM gets the same virtual address but new physical memory, taken from the top of the largest usable region above 4 GB: the pages are copied through a temporary window (PDPT slot 4 with a page directory in `.data`), and their page-directory entries are rewritten. The page that holds the running stack is copied and switched in one assembly sequence that does not touch the stack. The stolen range is removed from the memory map after `mm_load`, so the page allocator never hands it out. Process page tables copy the kernel's PML4 entries, so they see the same mapping. The boot log says how many MB were moved and where. Tested in QEMU (6 GB) with 0x09800000-0x09A00000 and 0x10000000-0x11000000 marked as firmware memory: UEFI and legacy BIOS boot to the live session. If there is no RAM above 4 GB to move to, the boot log says so. | [section 3](#part-3-memory) |
| 2 | The warning "the firmware uses memory inside a kernel area" did not say which area. | One line per affected area with the firmware type and the range. | [section 3](#part-3-memory) |

Fixed DMA buffers that are moved this way (disk and USB buffers at 0x09800000-0x09C80000) are still handed to devices by their old physical address, so AHCI, NVMe and USB storage may not work on a machine where that range is not RAM.

## What changed in v9.60

| # | Before (v9.59) | Now (v9.60) | Where |
|---|---|---|---|
| 1 | On a Lenovo laptop (UEFI and legacy BIOS) start-up finished ("interrupts on, entering main loop") and then nothing happened: no live screen. In QEMU it worked. | Two interrupt problems that only real firmware exposes were fixed. (a) The I/O APIC was never touched, so redirection entries left unmasked by the firmware (ACPI SCI, USB and others) could still deliver interrupts. Every I/O APIC listed in the ACPI MADT (or 0xFEC00000 if there is no MADT) now has all entries masked before interrupts are enabled; the boot log reports how many were unmasked. (b) Vectors 48-255 went to `isr_ignore`, which sent no end-of-interrupt. One stray vector kept its in-service bit set in the local APIC, which blocked the timer and every lower-priority interrupt, and the main loop then waited in `hlt` forever. Stray vectors now send the local APIC EOI (xAPIC register 0xB0 or x2APIC MSR 0x80B), and the highest in-service vector and a counter are recorded. Tested in QEMU with a self-sent vector 0x50: it was recorded and the timer kept running. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| 2 | A hang inside the main loop gave no information. | The main loop sets a stage number before each part (touchpad, PS/2 pads, VM mouse, ACPI, sound, USB mouse, input, USB hotplug) and counts passes. The timer interrupt checks the count; if it has not changed for 6 seconds it prints a screen with the stage, the interrupted RIP, the number of stray interrupts and the last stray vector, the I/O APIC count and how many lines were masked. Tested in QEMU with a deliberate endless loop. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |

## What changed in v9.59

| # | Before (v9.58) | Now (v9.59) | Where |
|---|---|---|---|
| 1 | The boot page tables (PML4, PDPT, four page directories, two framebuffer page directories) were at the start of `.bss`, physical 0x10000000. On a Lenovo laptop (UEFI) the v9.58 early fault screen showed: page fault, error code 0xB (present, write, reserved bit set), address 0x11000000, CR3 0x10000000, step 1 (clearing kernel memory). The entry that maps 0x11000000 had been overwritten after it was built, by something outside the kernel: a device still writing to memory after the firmware hand-over, or firmware memory at that address. | The boot page tables are in `.data` (about 0x1F39000 in the current build), inside the kernel image the loader copies to 1 MB; kernel code already runs from that memory on the same laptop. `kmain` clears `.bss` from `__bss_start`. The legacy BIOS path uses the same tables. Tested in QEMU: UEFI and legacy BIOS boot to the live session. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| 2 | The early fault screen started at the top and covered the UEFI loader's own messages (including its warnings about firmware memory overlapping kernel areas). | It starts a third of the way down. For a page fault it also prints the PML4, PDPT and PD entries used for the address and where the PD entry is, and for the fault address, for 0x10000000 and for the page tables it prints which UEFI memory-map entry (type and range) they fall in. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |

## What changed in v9.58

| # | Before (v9.57) | Now (v9.58) | Where |
|---|---|---|---|
| 1 | A CPU exception in the first steps of `kmain` was invisible: no interrupt table was loaded yet, so a UEFI boot froze inside the firmware's handler (all coloured boot marks except the white one) and a legacy BIOS boot triple-faulted and restarted. Reported on a Lenovo laptop in both modes. | `kmain` first loads a small early interrupt table (`core/init/early_fault.c`, stubs `eexc0`-`eexc31` in `isr64.S`, table in `.data` so clearing `.bss` does not erase it). Any exception before the normal table is loaded prints a screen with the exception number and name, error code, RIP, CR2, CR3, stack, the boot mode, and the start-up step, then halts instead of restarting. Tested in QEMU with a deliberate page fault at step 3: the screen appears under UEFI (OVMF) and legacy BIOS. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| 2 | Only one mark (white) was drawn inside `kmain`. | Four more marks after each early step (6 purple: memory cleared, 7 cyan: per-CPU set up, 8 pink: kernel snapshot taken, 9 grey: memory map read), then the white mark after the FPU setup, so a photo shows exactly where start-up stops. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| 3 | The UEFI entry kept whatever CR4 the firmware left. | CR4.CET (bit 23) is cleared on entry in case the firmware left control-flow enforcement on. | [section 2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |

## What changed in v9.57

| # | Before (v9.56) | Now (v9.57) | Where |
|---|---|---|---|
| 1 | Settings > Devices showed a fixed set of rows (keyboard, touchpad, USB list, printer, laptop). | The page has one tab per subsystem, each filled only from detected hardware: Input (PS/2 keyboard, the touchpad driver actually in use, USB keyboards and mice), USB (every xHCI controller from the PCI scan and every enumerated device with name, vendor:product, port and a NEW mark, USB drive state), Storage (every disk with size and which one is in use, plus every storage controller including RAID/Intel RST), Network (every Ethernet and Wi-Fi card by name with link state, and the IP address), Media (graphics cards by vendor and ID, the framebuffer mode, sound cards with driver state, cameras), Printers (network printer by model name and status, USB printers) and Laptop (vendor interface, keyboard light and charge limit controls). A coloured dot shows the state: working, attention or off. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | `printer <ip>` only stored the address. | It sends Get-Printer-Attributes (IPP operation 0x000B) and reads `printer-make-and-model` and `document-format-supported`. The status says whether the printer takes PWG raster (ready), only Apple raster (AirPrint, not supported yet) or neither; the model name is shown in Settings. | [section 9](#part-9-networking-and-wi-fi) |
| 3 | The Wi-Fi link state was private to the Wi-Fi code. | `g_wnd_up` is shared so Settings can show it. | [section 9](#part-9-networking-and-wi-fi) |

Cameras still have no video: streaming needs isochronous USB transfers in the xHCI driver.

## What changed in v9.56

| # | Before (v9.55) | Now (v9.56) | Where |
|---|---|---|---|
| 1 | A USB mouse or keyboard plugged in while running worked, but nothing showed which devices were connected. | Every USB device that is enumerated is recorded (`usbdev_note`: vendor and product ID, product name read from its string descriptor, kind from its interface classes: keyboard, mouse, camera, printer, audio, hub and others) and removed again when it is unplugged. A notification appears when one is connected. New Settings page "Devices" lists the keyboard, the touchpad type and every USB device with a "New" badge for 10 seconds; new command `usbdevs` (alias `lsusb`). Tested in QEMU: a USB mouse added while running appeared as "Mouse  QEMU USB Mouse  0627:0001  port 5". | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | No Lenovo or ASUS laptop extras. | New `drivers/acpi/laptop.c`, using the AML interpreter. ThinkPad (HKEY device LEN0068/IBM0068/LEN0268): hotkeys enabled with MHKM/MHKC, events read with MHKP (brightness 0x1010/0x1011, volume 0x1015-0x1017, keyboard light 0x1012, touchpad 0x1008/0x1009), keyboard light with MLCG/MLCS, charge limit 80% with BCSS. IdeaPad (VPC2004): conservation mode with SBMC 3/5, keyboard light with SALS/HALS. ASUS (WMI device PNP0C14 with _UID "ATK"): INIT, events through _WED(0xFF) (brightness 0x11-0x2E, volume 0x30-0x32, keyboard light 0xC4/0xC5/0xC7, touchpad 0x6B), keyboard light (device 0x00050021) and charge limit (device 0x00120057) through DEVS/DSTS; older ASUS ATK0100 events directly. The touchpad toggle stops all touchpad drivers. Settings > Devices shows keyboard light and charge limit controls; command `laptop`, `kbdlight 0-3`, `chargelimit on/off`. Not tested on real laptops. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | No printing. | New `net/print/ipp.c`: IPP Everywhere network printing. Text is rendered with the anti-aliased font into an A4 PWG raster (300 dpi, 8-bit grey, PackBits line compression with line repeat) and sent as a Print-Job (IPP 2.0, document-format image/pwg-raster) by HTTP POST to port 631, /ipp/print. New `sock_send_rel` sends with a window of 8 segments, waits for ACKs and resends after a timeout, so large jobs are not lost. Commands `printer <ip>`, `print` (test page) and `print <file>`; Settings > Devices has a test page button. Tested on the host: the job decodes to a valid 2480 x 3508 page (21 KB for the test page). Not tested against a real printer. | [section 9](#part-9-networking-and-wi-fi) |
| 4 | - | USB cameras (video class 0x0E) are detected and listed by name. Streaming needs isochronous USB transfers, which the xHCI driver does not have yet, so there is no picture. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

## What changed in v9.55

| # | Before (v9.54) | Now (v9.55) | Where |
|---|---|---|---|
| 1 | On Intel laptops the I2C touchpad was read every 10 ms because there was no driver for the Intel GPIO controller, whose pad numbering differs between CPU generations. | New `drivers/input/touchpad/tp_igpio.c`. It needs no per-generation table: it evaluates `_CRS` of the GPIO controller named in the touchpad's GpioInt resource, takes every Memory32Fixed window as a pin community, reads the pad table offset (PADBAR, community offset 0x0C) and the pad size (16 bytes if the revision at offset 0 is 0x94 or newer, otherwise 8), and lists every pad. While polling, it samples the RX state (PADCFG0 bit 1) of every pad before each read and compares it with whether the touchpad had a report. When exactly one pad has matched every sample (at least 24 with data and 24 without), with either polarity, that pad is the interrupt line: from then on the touchpad is read only while the pin is active, checked on every pass of the main loop instead of every 10 ms. If no pad matches, polling continues. `tpinfo` shows the learning state. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 2 | Synaptics PS/2 touchpads worked only as a basic mouse. | New `drivers/input/ps2/syn_ps2.c`. After the ELAN check, `mouse_init` sends the Synaptics identify query (sliced command 0x00, magic byte 0x47), reads the capabilities (0x02) and switches to absolute mode with W value (mode byte 0xC1). The 6-byte packets are checked and parsed (X, Y, pressure, W). W 0 means two fingers, W 1 three or more, W 3 a pass-through packet from a TrackPoint, which is sent as relative movement and buttons. The frames go through the same gesture engine as the other touchpads: tap to click, two-finger scrolling, three-finger swipes. If any step fails, the standard mouse setup runs as before. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | - | Checked the Wi-Fi changes from v9.54 again (separate transmit buffers, full-ring check, missing-card check); no change needed. | [section 9](#part-9-networking-and-wi-fi) |

Not done: ALPS absolute mode and Synaptics RMI4 over SMBus. Neither the Synaptics driver nor the GPIO learning could be tested here (QEMU has neither device); QEMU boot and the standard PS/2 mouse fallback were tested.

## What changed in v9.54

| # | Before (v9.53) | Now (v9.54) | Where |
|---|---|---|---|
| 1 | Wi-Fi used one DMA buffer for every transmit slot. When several frames were queued quickly (TCP bursts, DNS plus HTTP), a new frame overwrote one the card had not sent yet, so frames went out corrupted and connections stalled. The transmit ring was also never checked for free space. | Every ring slot has its own 2 KB DMA buffer (up to 128). Before queuing, the driver checks the card's read index; if the ring is full it waits up to about 1 ms and then drops the frame instead of overwriting one. If the card stops answering (registers read 0xFFFFFFFF, for example after a PCIe link drop), sending and receiving stop touching the rings instead of processing garbage. `wifi` status shows the new counters `ring-full` and `card-gone`. | [section 9](#part-9-networking-and-wi-fi) |
| 2 | Touchpad I2C controllers above 4 GB were rejected (`dw_present` and the PCI scan accepted only 32-bit addresses). | 64-bit BARs are read in full and mapped with `acpi_hi_map`; `dw_present` accepts addresses below 128 TB. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 3 | Intel LPSS controllers were only taken out of reset when they did not answer, and the private remap registers were never written. | For every Intel LPSS I2C controller the driver sets memory and bus-master enable, makes sure the reset register (private offset 0x204) is 7 and writes the controller's own address to the remap registers 0x240/0x244, as Linux does. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | I2C timing used whatever the BIOS left or defaults for a 100 MHz clock; the SDA hold time was never set. | `SSCN` and `FMCN` are evaluated with the AML interpreter under the controller's ACPI node and written to the high/low count registers, and their third value to the SDA hold register (0x7C). Without them, empty registers are computed from the controller clock (AMD0010 133 MHz, AMDI0010/AMDI0510 150 MHz, Intel assumed 216 MHz, which gives a slower, safe bus), and an empty SDA hold is set to 300 ns. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 5 | The touchpad's I2C address and interrupt pin came from a static scan of the tables, without running BIOS code, so a `_CRS` method that chooses between two addresses could give the wrong one. | `_CRS` is evaluated and its I2cSerialBus (address, speed, controller) and GpioInt (pin, polarity, controller) descriptors are tried first; the static scan and address guessing remain as fallbacks. The touchpad probe now runs after the AML interpreter has started. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 6 | The HID descriptor register was guessed (0x20, 0x01, 0x00, 0x10, 0x30). | `_DSM` with the HID-over-I2C UUID 3CDFF6F7-4267-4555-AD05-B30A3D8938DE, revision 1, function 1 gives the register; it is tried first, then the guesses. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 7 | Touchpads that need the OS to power them on stayed off. | Before probing, the power resources in `_PR0` are switched on (`_ON`) and `_PS0` is run, followed by a 30 ms wait. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 8 | `tpinfo` did not show any of this. | Two new lines: which ACPI methods were used (`_CRS`, `_DSM` register, `_PS0`, power resources) and the I2C timing (SSCN, FMCN, SDA hold, Intel LPSS setups). | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

Tested in QEMU only: boot, `tpinfo scan` (QEMU has no I2C touchpad, so the new ACPI paths ran without finding one). The Intel GPIO interrupt driver and Synaptics/ALPS multi-finger support are not in this version.

## What changed in v9.53

| # | Before (v9.52) | Now (v9.53) | Where |
|---|---|---|---|
| 1 | NVMe disks behind Intel VMD ("Intel RST" with NVMe on 11th-generation and newer Intel CPUs) were invisible: the VMD controller hides them in its own PCI domain, so no disk was found. | `vmd_find_nvme` (`drivers/storage/nvme.c`) finds the VMD controller (PCI class 01:04, Intel device IDs 9A0B, 467F, A77F, 7D0B, AD0B and others), enables it, reads its bus restriction (VMCAP 0x40 / VMCONFIG 0x44: first bus 0, 128 or 224), and scans its configuration window (BAR0, one MB per bus) for an NVMe device (class 01:08:02). The NVMe driver then uses that device's own configuration space through the window. If the firmware left the NVMe address empty, it is placed at the start of VMD memory window 1 (BAR2) and the parent bridge's memory window and command bits are set. The NVMe driver polls and needs no interrupts, so VMD's interrupt remapping is not needed. Not tested on real VMD hardware (QEMU has no VMD). | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 2 | The RAID-class fallback in `ahci_init` could pick the VMD controller and treat it as SATA. | VMD device IDs are skipped, and a RAID-class controller is used only if its AHCI version register reads 1.0 to 1.3.1. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 3 | `disks` printed "NVMe present - read/identify not implemented yet" even when the NVMe driver worked. | It says whether the driver is active, and prints the VMD device and what was found behind it. The installer shows the same reason when no disk is found. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |

## What changed in v9.52

| # | Before (v9.51) | Now (v9.52) | Where |
|---|---|---|---|
| 1 | When no disk was found, the installer showed "/dev/hda - 2048 MB", a made-up size. | It shows "no disk found". | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 2 | SATA controllers in RAID mode (PCI class 01:04, for example Intel RST on many laptops) were ignored, so their disks were not found. | `ahci_init` also accepts class 01:04 controllers and drives them through their AHCI registers. Not tested on real RAID-mode hardware. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 3 | The installer's target page showed only disks. | Below the disks it lists the partitions of the selected disk with name, size and file system (from `dm_load`, GPT or MBR). | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 4 | The volume name was "NOOVEXOS"; MBR partitions of type 0x7F showed as "NoovexOS data". | The default volume name is "NoovexOS" (FAT stores it in capitals; NTFS keeps "NoovexOS"), and type 0x7F partitions show as "NoovexOS". | [section 8](#part-8-storage-drivers-pci-and-the-installer) |

## What changed in v9.51

| # | Before (v9.50) | Now (v9.51) | Where |
|---|---|---|---|
| 1 | Firefox opened as a window the size of the whole screen, so it looked like a full-screen black background and could not be moved. | `nvxwm` shrinks any window wider than the screen minus 40 pixels (or taller than the screen minus the title bar and 40 pixels) to 3/4 of the screen and centres it, and limits later resize requests the same way. Firefox draws no tabs in the title bar (`browser.tabs.inTitlebar` 0), so the window manager's title bar is there to drag it. Tested on a host X server: a 1280x1024 window became 960x768 at (160,106) with a 22-pixel title bar. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | `nvx-xinput` sent every mouse movement to X one by one and woke every 20 ms. | Movements in one batch are merged into one (the last position) before any click or key, and it wakes every 8 ms. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | Firefox started extra helper processes (network, media decoder, extensions, fork server) and wrote a disk cache. | `user.js` turns those processes and the disk cache off, uses software WebRender and skips the blank start window and the bookmark import. When X starts in overlay mode, the Firefox libraries are read once in the background so the first start reads less from disk. The start time was not measured. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | "This PC" showed RAM size as the size of C: when the data partition was not NVX4 (for example 2048 MB on a 2 GB machine) and never showed the real disks. | C: uses the data partition size (`g_nvx_secs`); in the live session the tile is called "Live RAM disk (C:)". A line lists every physical disk with its real size. | [section 11](#part-11-desktop-applications-shell-and-scripting) |

## What changed in v9.50

| # | Before (v9.49) | Now (v9.50) | Where |
|---|---|---|---|
| 1 | Only the first SATA port with a drive was used. A 500 GB disk on another SATA port was invisible to the installer, which then tried the small disk and said "The disk is too small." | Every SATA port with a disk is its own disk (`ahci_select`), and IDE master and slave are both listed. The installer's target page lists every disk with its size (click it or use Up/Down). Tested in QEMU: SATA ports 0, 1 and 2 plus NVMe all listed, install to the 500 GB disk on port 2, boot from it. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 2 | The Disk Manager only understood MBR. | It reads GPT (up to 256 entries) and MBR, shows partition names and file systems (NTFS, exFAT, FAT32 with label, FAT16, NVX4, ext4), deletes partitions (GPT: primary and backup tables rewritten with correct CRC32s), erases the whole disk and switches disks (Next disk). Every delete or erase asks for confirmation (Y/N). Tested on a 500 GB GPT disk with EFI, MSR, Windows data and Linux partitions: the table stays valid after a delete. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 3 | Whole-disk install used a fixed small data partition and left the backup GPT on 4K-sector disks. | The data partition uses the rest of the disk (NoovexFS up to 64 GB, FAT32 up to 32 GB, FAT16 up to 2 GB, NTFS up to 2 TB). Both GPT copies are wiped first. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 4 | "The disk is too small." gave no details. | The message names the disk, its size and the size needed, and points to Next disk. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 5 | IDE used 28-bit addresses only, so sectors above 128 GB wrapped around. | IDE reads and writes above 128 GB use LBA48 commands. Not yet tested in QEMU. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |

## What changed in v9.49

| # | Before (v9.48) | Now (v9.49) | Where |
|---|---|---|---|
| 1 | Firefox and other Linux GUI apps opened in a full-screen copy of the whole X screen (black around the window) or in the "X Display" box. | They open on the NoovexOS desktop: the X root window is painted with the key colour 0x010203 and only real X windows are drawn over the desktop (overlay mode). The launcher switches to overlay mode before starting the app, closes the X Display box if it is open and gives the keyboard to the app as soon as its window appears; clicking the desktop or a NoovexOS window takes the keyboard back. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 2 | The overlay was refreshed every 60 ms by sampling 1/24 of the pixels, and any change redrew the whole desktop. | A shadow copy of the X screen is compared pixel by pixel every 20 ms (every 80 ms after 25 quiet checks). Only the changed rectangle is copied to the screen; the whole desktop is redrawn only when part of an X window disappears (a pixel turns into the key colour) or a menu, popup or drag is active. Tested with `xdemo`: 436 partial updates against 3 full redraws. | [section 11](#part-11-desktop-applications-shell-and-scripting) |
| 3 | Keys sent to X apps had no Ctrl or Alt, so shortcuts such as Ctrl+L, Ctrl+T or Ctrl+C did nothing; `nvx-xinput` waited up to 50 ms between reads. | Every key carries the Ctrl and Alt state (byte 3 of the event record) and `nvx-xinput` presses Control_L / Alt_L around the key. It wakes at least every 20 ms. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| 4 | Every Firefox start ran `fc-cache -f` (rebuilds the whole font cache) and `ld.so --list` on libxul.so, and started with a random new profile. | The font cache is updated once per boot without `-f`; the library check only runs after a failed start. Firefox uses the fixed profile `/root/.nvx-firefox` with a `user.js` that turns off telemetry, studies, Safe Browsing downloads, Pocket, the welcome page, the default-browser check, IPv6 lookups, prefetching, smooth scrolling and animations, limits Firefox to one content process without preloading, and sets 30 frames per second. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 5 | The X server started only when an X app was opened. | 3 seconds after the desktop appears, if Firefox is installed, the X server starts in the background in overlay mode, so opening Firefox does not wait for it. The wait after starting X for a new app is 1 second (was 2). | [section 11](#part-11-desktop-applications-shell-and-scripting) |

## What changed in v9.48

| # | Before (v9.47) | Now (v9.48) | Where |
|---|---|---|---|
| 1 | The installer refused kernels larger than 15 MB ("The kernel size stamp is invalid."). The kernel is about 32 MB since the Linux sysroot is built in, so nothing could be installed. The boot-time copy of the kernel (`g_kimg_snap`, a 15 MB static array) and the boot signature check had the same limit. | Limit 64 MB everywhere. At boot only the writable part of the kernel (`.data`, about 320 KB) is copied, plus a 32-bit FNV-1a checksum of every 4 KB page of the code part; the full image is assembled from memory and that copy when the installer or `kcheck` needs it. The boot partition is 128 MB on 512-byte-sector disks (was 64 MB; two kernel copies need about 62 MB) and the installer checks that the image fits. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 2 | Writing the "installed" marker could hit the old disk. When the target disk was blank, the live system had it mounted as a scratch disk at LBA 2048, and any file sync during installation overwrote the new boot partition (found in testing: "Sector 2080 differs"). | Installation stops all writes to the previous mount first (`disk_ok=0`, pending write-back cleared); the new data partition is mounted when it is formatted. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 3 | Booting an installed disk showed the live terminal and installer every time. | The installer writes `/etc/nvx-installed`. On the first boot from the installed disk NoovexOS goes straight to setup; setup writes `/etc/nvx-setup-done` (user name, accent, theme, language, keyboard, time zone), and every later boot goes straight to the login screen. The ISO without an installed disk still starts the live session. Tested: install, first boot to setup, second boot to login. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 4 | NVMe controllers with their registers above 4 GB (64-bit BAR, common with UEFI firmware and on new laptops) were rejected. `acpi_hi_map` could only map addresses below 512 GB. | 64-bit BARs are read in full and mapped with `acpi_hi_map`, which now maps any physical address below 128 TB (except the user-space range) with uncached 2 MB pages, creating page-table levels as needed. Tested in QEMU with the NVMe registers at 0xC00000000: installation to NVMe, then UEFI boot from that NVMe disk, mount of the data partition and setup. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 5 | With several disks, NoovexOS used the first one it found (IDE, then AHCI, NVMe, VirtIO). | All disks are probed and the one with a NoovexOS data partition (type 0x7F) is used; otherwise the first one. Tested: IDE disk with the ISO plus an installed NVMe disk boots into the installed system. | [section 8](#part-8-storage-drivers-pci-and-the-installer) |
| 6 | No mouse integration in virtual machines. | New `drivers/input/vm/vmmouse.c`. Detects the hypervisor (CPUID leaf 0x40000000). VirtualBox: VMMDev (PCI 80EE:CAFE) - reports guest info, sets "guest can use absolute pointer" and reads the host pointer position every timer tick; buttons keep coming from the emulated PS/2 mouse. VMware and QEMU: VMware backdoor (port 0x5658) vmmouse protocol with absolute position, buttons and wheel. The position is turned into the usual pointer movement, so every screen and the X server see it. New command `vminfo`. Tested in QEMU: three absolute positions sent from the host moved the pointer exactly to them. VirtualBox could not be tested here. | [section 10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |

## What changed in v9.47

| # | Before (v9.46) | Now (v9.47) | Where |
|---|---|---|---|
| 1 | `arch_prctl` knew only `ARCH_SET_FS` and `ARCH_GET_FS`. Firefox runs some libraries (fonts, spelling, XML) inside an RLBox WebAssembly sandbox built with wasm2c, which points the GS segment base at the sandbox memory ("Segue") with `arch_prctl(ARCH_SET_GS)`. The call returned EINVAL, the log showed `wasm_rt_syscall_set_segue_base error: Invalid argument`, and Firefox aborted (exit code 139). | `ARCH_SET_GS` (0x1001) and `ARCH_GET_GS` (0x1004) work. The value is stored per thread (`struct jproc`, field `gsbase`) and written to `IA32_KERNEL_GS_BASE` (MSR 0xC0000102) when the scheduler switches to the thread; the kernel keeps its own per-CPU pointer in `IA32_GS_BASE` and `swapgs` exchanges the two on every user/kernel transition, so they never collide. Threads and `fork` children inherit the value, `execve` clears it. `ARCH_GET_CPUID` (0x1011) returns 1. Tested inside NoovexOS on 2 CPUs: a `mov rax, gs:[0]` instruction returns the main thread's value while another thread uses a different GS base, both before and after the switch, and a forked child reads the inherited value. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | The crash report's "Reason" line could show a fragment such as `)`. | A decoded reason needs at least 6 characters, 4 of them letters. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.45 Firefox report: `158 arch_prctl -22 x1` and the `wasm_rt_syscall_set_segue_base` log line.

## What changed in v9.46

| # | Before (v9.45) | Now (v9.46) | Where |
|---|---|---|---|
| 1 | Linux GUI apps installed with apt (for example AbiWord) started without the settings GTK needs here and wrote their errors nowhere visible. Typing their name in the terminal did not start the X server. | New launcher `/usr/bin/nvx-xrun NAME COMMAND...`: sets `DISPLAY`, `HOME`, `GTK_A11Y=none`, `NO_AT_BRIDGE=1`, `GSETTINGS_BACKEND=memory` (settings kept in memory, no dconf or D-Bus), `GDK_BACKEND=x11`, `QT_QPA_PLATFORM=xcb`, `LIBGL_ALWAYS_SOFTWARE=1`, `SDL_AUDIODRIVER=dummy` and `XDG_RUNTIME_DIR`; creates the config folders; waits for the X server; lists missing libraries; builds the font cache once per boot; writes all output to `/tmp/NAME.log` and the exit code to `/tmp/NAME.done`. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | Only Firefox had a failure report. | Every app started from its icon or by typing its name gets the same watcher: full screen when its window appears, and the report (log tail, crash address and library, failed syscalls, process list) if it exits with an error or shows no window within 2 minutes. A normal exit (code 0) just closes full screen. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | - | Typing the name of an installed GUI app in the terminal (any program with a `.desktop` file in `/usr/share/applications`, for example `abiword` or `abiword report.docx`) starts the X server if needed and runs the app through `nvx-xrun`. Shells, interpreters, `sudo` and `apt` are never redirected. If the app is missing, the report shows the `apt install` command to run. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Tested inside NoovexOS with test `.desktop` entries: `abiword` (not installed) was recognised as a GUI app and reported "not installed - run: sudo apt install abiword" with code 127; `python3` was not redirected; `xdemo` started through `nvx-xrun` and connected to the X server, with its output in `/tmp/xdemo.log`.

## What changed in v9.46

| # | Before (v9.45) | Now (v9.46) | Where |
|---|---|---|---|
| 1 | Linux GUI apps installed with apt (for example AbiWord) started without the settings GTK needs here and wrote errors nowhere visible. Typing their name in the terminal did not start the X server. | New launcher `/usr/bin/nvx-xrun NAME COMMAND...`: sets `DISPLAY`, `HOME`, `GTK_A11Y=none`, `NO_AT_BRIDGE=1`, `GSETTINGS_BACKEND=memory` (settings kept in memory, no dconf or D-Bus), `GDK_BACKEND=x11`, `QT_QPA_PLATFORM=xcb`, `LIBGL_ALWAYS_SOFTWARE=1`, `SDL_AUDIODRIVER=dummy`, `XDG_RUNTIME_DIR`; creates the config folders; waits for the X server; lists missing libraries; builds the font cache once per boot; writes all output to `/tmp/NAME.log` and the exit code to `/tmp/NAME.done`. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | Only Firefox had a failure report. | Every app started from its icon or by typing its name gets the same watcher: full screen when its window appears, and the report (log tail, crash address and library, failed syscalls, process list) if it exits with an error or shows no window within 2 minutes. A normal exit (code 0) just closes full screen. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | - | Typing the name of an installed GUI app in the terminal (any program with a `.desktop` file in `/usr/share/applications`, for example `abiword` or `abiword report.docx`) starts the X server if needed and runs it through `nvx-xrun`. Shells, interpreters, `sudo` and `apt` are never redirected. If the app is missing, the report says which `apt install` command to run. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Tested inside NoovexOS with test `.desktop` entries: `abiword` (not installed) was recognised as a GUI app and reported "not installed - run: sudo apt install abiword" with code 127; `python3` was not redirected; `xdemo` started through `nvx-xrun` and connected to the X server, with its log in `/tmp/xdemo.log`.

## What changed in v9.45

| # | Before (v9.44) | Now (v9.45) | Where |
|---|---|---|---|
| 1 | A `MAP_SHARED` mapping of a file was a one-time copy. `write()` to the file did not change the mapping, and changes made through the mapping never reached the file. SQLite (Firefox history, bookmarks, settings) reads through a shared mapping and writes with `write()`. | Shared file mappings are tracked (256 slots). `write` and `pwrite64` copy the new bytes into every shared mapping of that file at once. Changed pages go back into the file on `msync`, on `munmap` and when the process exits. `fork` copies the tracking. Tested inside NoovexOS: `pwrite` seen through the mapping, a write through the mapping read back with `pread` after `msync`. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | System V shared memory (`shmget`, `shmat`, `shmdt`, `shmctl`) returned ENOSYS. X clients use it (MIT-SHM) so the X server reads images straight from shared memory instead of through the socket. | Implemented for Linux programs: 64 segments up to 1 GB each, backed by memfd frames, private or by key, `IPC_CREAT`/`IPC_EXCL`, `SHM_RDONLY`, `SHM_RND`, `IPC_RMID` (freed after the last detach), `IPC_STAT`/`SHM_STAT`, `IPC_INFO`/`SHM_INFO`, `IPC_SET`, `SHM_LOCK`. Attachments are inherited by `fork` and detached at exit. Tested inside NoovexOS: two attachments of one segment see the same bytes. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | A file name part longer than 63 characters returned ENAMETOOLONG (the NoovexOS file system stores names in 64 bytes). | Parts up to 255 characters are accepted, as on Linux. A part longer than 63 is stored under a stable 63-character name: the first 46 characters, `~`, and a 64-bit FNV-1a hash of the full name in hex. Open, create, stat and delete with the long name all reach the same file; a directory listing shows the stored name. Tested inside NoovexOS with a 120-character name. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | When Firefox showed no window, the report could not say what each process was doing. | The report lists every running Linux process: pid, name, state (run, wait, sleep), its most recent syscall and how many syscalls it has made, so a stuck process stands out. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

## What changed in v9.44

| # | Before (v9.43) | Now (v9.44) | Where |
|---|---|---|---|
| 1 | A large anonymous `mmap` built page-table entries for the whole range up front (about 32 MB of real memory for each 16 GB) and was refused when the free-frame check failed. Firefox reserves 16 GB of address space 30 times (JavaScript and WebAssembly memory) and got ENOMEM every time. | Private anonymous mappings larger than 256 MB are stored as reserved ranges (512 slots). No page tables are built; a page gets its entry on the first touch, from the page-fault handler or when the kernel copies data into it. `munmap` and `MAP_FIXED` trim or split ranges, `fork` copies them, process exit drops them, and the gap finder skips them. Anonymous mappings up to 1 TB are accepted. Tested inside NoovexOS: 30 reservations of 16 GB each, writes at both ends, values seen by a forked child, untouched pages read as zero. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `newfstatat(fd, NULL, buf, AT_EMPTY_PATH)` returned EFAULT. | Treated as `fstat(fd)`, as on Linux 6.11 and later. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | GLib looked for an accessibility bus and tried to run `dbus-launch`, which is not installed. | The Firefox launcher sets `GTK_A11Y=none` and points `DBUS_SESSION_BUS_ADDRESS` at an unused socket, so no session bus is started. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Checked and left as is: the repeating `poll`/`read`/`recvmsg` lines of pid 5 come from `nvx-xinput`; its `poll` waits 50 ms each time (it returns 0), so it wakes 20 times a second and is not a busy loop.

## What changed in v9.43

| # | Before (v9.42) | Now (v9.43) | Where |
|---|---|---|---|
| 1 | `execve` accepted at most 32 arguments, 32 environment variables and 2 KB of text for each. Firefox has more than 32 environment variables, so every program it started failed with E2BIG ("Argument list too long"): `dbus-launch` and, more importantly, its content processes, which draw the web pages. Firefox kept running but never drew a window. | Up to 4096 arguments and 4096 environment variables with 96 KB of text each. The initial stack image is 320 KB; only the pages it uses are mapped up front, the rest of the 1 MB stack stays lazy. A request that does not fit still returns E2BIG. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `fallocate` (285) returned EOPNOTSUPP (28 times in the Firefox report). | Mode 0 grows a regular file or memfd to `offset + len` (memfd seals respected); `FALLOC_FL_KEEP_SIZE` succeeds; hole punching still returns EOPNOTSUPP. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | A `MAP_PRIVATE` mapping of a memfd wrote straight into the memfd, so other processes saw the private changes. | Pages are mapped read-only copy-on-write; the first write makes a private copy. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | The report showed only counts for failed syscalls (30 `mmap` ENOMEM with no sizes). | For `mmap`, `mremap` and `execve` the report shows the arguments of the last failing call. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.42 Firefox report: `Failed to execute child process "dbus-launch" (Argument list too long)` and `59 execve -7 x3`.

## What changed in v9.42

| # | Before (v9.41) | Now (v9.42) | Where |
|---|---|---|---|
| 1 | `fork` turned every writable page into a copy-on-write page, including `MAP_SHARED` mappings. After a fork, the first write made a private copy, so parent and child stopped sharing. The X server forks (for example to run `xkbcomp`); its screen buffer, mapped `MAP_SHARED` from `/tmp/Xvfb_screen0`, was then silently copied, and the frames NoovexOS displays were freed when the child exited. With Firefox this showed a black full-screen window and then a jump back to the desktop. | Pages of `MAP_SHARED` mappings carry a software page-table bit (`PTE_SHR`, bit 11). `fork` shares them writable (reference count +1) instead of marking them copy-on-write. Shared anonymous mappings up to 256 MB are allocated up front so that parent and child share real frames. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `MADV_DONTNEED` could drop pages of a shared mapping that only one process had mapped, and unmapping any copy of a screen frame switched the X display off. | `MADV_DONTNEED` leaves shared pages alone. The X display is switched off only when the last reference to one of its frames goes away. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | If the Firefox window appeared and full screen ended while Firefox kept running, nothing was reported. | 15 seconds after full screen ends without an exit, the report opens: it says whether the X display was lost or full screen was closed, shows the log tail and the last syscalls. Reports without a crash now list the last syscalls too. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | `/etc/machine-id` and `/var/lib/dbus/machine-id` were missing; GLib and Firefox warned "Cannot spawn a message bus without a machine-id". | Both files are in the system overlay. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.41 tests: Firefox no longer crashes; it opened a window (the screen went to full-screen X), then the desktop came back with no report.

## What changed in v9.41

| # | Before (v9.40) | Now (v9.41) | Where |
|---|---|---|---|
| 1 | Opening `/proc/self/fd/N` did not reopen file N. Every path under `/proc/` matched the procfs driver, so the open "succeeded" with a file that had no content. Firefox shares read-only copies of memory by reopening its memfd through `/proc/self/fd/N`; the copy it mapped was empty and the check `MOZ_RELEASE_ASSERT(GetHeader().mMagic == kSharedStringMapMagic)` in libxul.so failed (exit 139). | `/proc/self/fd/N`, `/proc/thread-self/fd/N`, `/proc/<own pid>/fd/N` and `/dev/fd/N` open the same object again with the new access mode: a memfd (shared frames, seals kept), a regular file, a sysroot file or a directory get a new open file with its own offset; sockets, pipes and event files are shared like `dup`. Opening read-write is refused for read-only files. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `readlink("/proc/self/fd/N")` failed. | Returns the file path, `/memfd:nvx (deleted)`, `socket:[n]`, `pipe:[n]` or `anon_inode:[eventfd]` / `anon_inode:[timerfd]`, as on Linux. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | Any unknown `/proc/...` path opened as an empty file. | Unknown paths return ENOENT. The procfs driver also serves the files from the Unix layer (`/proc/loadavg`, `/proc/filesystems`, `/proc/self/status` and others) to Linux programs. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.40 Firefox failure report: the crash stack started in a mapping named `29` (the reopened `/proc/self/fd/29`), unmapped just before the assertion.

## What changed in v9.40

| # | Before (v9.39) | Now (v9.40) | Where |
|---|---|---|---|
| 1 | `fcntl(F_ADD_SEALS)` and `fcntl(F_GET_SEALS)` returned EINVAL. Firefox seals the memfd blocks it shares between its processes; the failure ended in `MOZ_RELEASE_ASSERT(result.isOk())` in libxul.so (exit 139). | memfd seals work: `F_SEAL_SEAL`, `F_SEAL_SHRINK`, `F_SEAL_GROW`, `F_SEAL_WRITE`, `F_SEAL_FUTURE_WRITE`, `F_SEAL_EXEC`. They are enforced by `write`, `pwrite64`, `ftruncate` and writable shared `mmap`. A memfd made without `MFD_ALLOW_SEALING` starts with `F_SEAL_SEAL`, as on Linux. `MFD_NOEXEC_SEAL` and `MFD_EXEC` are accepted. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | Other `fcntl` commands returned EINVAL: open file description locks, signal owner, lease, notify and write-hint commands. | `F_OFD_GETLK`, `F_OFD_SETLK`, `F_OFD_SETLKW`, `F_SETOWN`, `F_GETOWN`, `F_SETSIG`, `F_GETSIG`, `F_SETOWN_EX`, `F_GETOWN_EX`, `F_SETLEASE`, `F_NOTIFY` and the read/write hint commands succeed. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | `prctl` knew only `PR_SET_NAME` and `PR_GET_NAME` (117 EINVAL results in the Firefox report). | Also the death signal, dumpable, keep-caps, seccomp query, capability bounding set, timer slack, child subreaper, no-new-privs, ambient caps, speculation control, `PR_SET_PTRACER` and `PR_SET_VMA` (glibc names its memory areas with it). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | `mmap(MAP_FIXED)` and `munmap` could replace the lower part of the main thread's 1 MB stack (the guard covered only the top 64 KB). | The whole stack range is protected. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 5 | The crash report listed only the first argument of each recent syscall. | It shows the first two, so an `fcntl` line names the command. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.39 Firefox failure report: two `fcntl` calls on a memfd returned -22 just before the assertion.

## What changed in v9.39

| # | Before (v9.38) | Now (v9.39) | Where |
|---|---|---|---|
| 1 | `/proc/self/maps` did not exist for Linux programs. glibc's `pthread_getattr_np()` reads it to find the main thread's stack; it failed, and Firefox's JavaScript engine treats that as fatal (`MOZ_CRASH`, a write to address 0, exit code 139). This is the most likely cause of the v9.38 crash. | `/proc/self/maps`, `/proc/thread-self/maps` and `/proc/<own pid>/maps` are generated on open: stack, program image, heap, dynamic loader and every file mapping of the process, sorted, Linux format. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `read`, `write`, `lseek`, `pread64` and `pwrite64` on a memfd failed (only `mmap` and `ftruncate` worked). | All five work; writes past the end grow the memfd. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | `pread64` on files from the system overlay (the read-only sysroot) returned ESPIPE. | Works. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | The crash line had no library name: the table of file mappings held only 96 entries and Firefox maps hundreds. | 2048 entries, adjacent segments of one file are merged, offsets are counted from the start of the library, and entries of exited processes are kept for the report but left out of `/proc/self/maps`. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 5 | The Firefox report showed only the fault and code address. | The report now also shows: whether it was `MOZ_CRASH`, the crash reason string and source line (decoded from the code around the fault), the instruction bytes, a call stack of library return addresses, the last 8 syscalls before the crash, and every syscall that returned an error (with errno and count). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

Found through the v9.38 Firefox failure report (fault at address 0, no library name).

## What changed in v9.38

| # | Before (v9.37) | Now (v9.38) | Where |
|---|---|---|---|
| 1 | Anonymous `mmap` was capped at 1 GB and also refused when the request was larger than the number of free physical frames. Firefox's JavaScript engine reserves about 2 GB of address space for compiled code at startup; the reservation failed, Firefox called `MOZ_CRASH` (a deliberate write to address 0) and exited with code 139 (SIGSEGV). | Anonymous reservations up to 64 GB are accepted. Anonymous memory is lazy, so a reservation only costs page-table space; physical frames are taken when a page is first touched. File and memfd mappings keep the free-frame check. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 2 | `madvise` (28) returned 0 but did nothing. `MADV_DONTNEED` must give back zero-filled pages on the next touch; allocators in Firefox (mozjemalloc) rely on this and read stale data otherwise. | `MADV_DONTNEED` and `MADV_REMOVE` drop private pages in the range and remap them lazily, so the next touch sees zeros. Shared frames (memfd, other processes) are left alone. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | `readahead` (187) returned ENOSYS (seen 11 times in the Firefox report). | Returns 0 (it is only a hint). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 4 | The Firefox failure report showed only the exit code and missing syscalls. | When a Linux program crashes, the report adds a Crash line: the faulting address, the instruction address, and the library it was in with the offset (for example `libxul.so+0x1a2b3c`). | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

`clone3` (435) still returns ENOSYS on purpose: glibc falls back to `clone`, which works. Found through the v9.37 Firefox failure report (exit 139).

## What changed in v9.37

| # | Before (v9.36) | Now (v9.37) | Where |
|---|---|---|---|
| 1 | `readlink("/proc/self/exe")` failed with ENOENT. Firefox uses it to find its own install folder (`/usr/lib/firefox-esr/`), so it quit with "Couldn't find the application directory" (exit code 255). Many other programs (Python's `sys.executable`, Chromium, Electron apps, anything that locates its own files) use the same call. | Every process records the real path of its program when it starts, whether launched by the NoovexOS shell or by Linux `execve`. Symlinks are followed (so `/usr/bin/firefox-esr` reports `/usr/lib/firefox-esr/firefox-esr`). `readlink` and `readlinkat` answer `/proc/self/exe`, `/proc/thread-self/exe` and `/proc/<pid>/exe` for the calling process. Verified inside NoovexOS: `/usr/bin/python3` reports `/usr/bin/python3.13`. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

The path is stored in `struct jproc` (`exe[128]`), set from `jc_cmdname()` through `lx_set_exe()` (PATH search plus symlink resolution) and inherited across `fork`. Found through the v9.35 Firefox failure report.

## What changed in v9.36

| # | Before (v9.35) | Now (v9.36) | Where |
|---|---|---|---|
| 1 | Firefox failed to start with `libgcc_s.so.1: cannot open shared object file`. apt treats `libgcc-s1` and `libcrypt1` as provided by NoovexOS and never downloads them, but the system image did not actually contain `libgcc_s.so.1` or `libcrypt.so.1`. Any C++ program from Debian (Firefox, and most GTK/Qt apps) hit this. | `libgcc_s.so.1` and `libcrypt.so.1` are in the system image (`/lib64`), so the "provided by NoovexOS" promise in apt is true. Verified inside NoovexOS in QEMU: a glibc test program linked against `libgcc_s` loads it and walks its stack. | [section 6.16](#616-debian-packages-apt-and-dpkg) |

Found through the v9.35 Firefox failure report, which printed the dynamic loader's error on screen.

## What changed in v9.35

| # | Before (v9.34) | Now (v9.35) | Where |
|---|---|---|---|
| 1 | Clicking Firefox switched straight to the full-screen X session. If Firefox crashed or was still loading, the screen just went black with no explanation. | The desktop stays visible with a "Starting Firefox" notice. The display switches to full-screen only once Firefox's window has actually been drawn in the X framebuffer. If Firefox exits, NoovexOS leaves X, opens the terminal and prints the exit code, the last lines of `/tmp/ff.log`, and every unimplemented Linux syscall Firefox called. If no window appears within 2 minutes, the same report is shown. | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |
| 2 | `eventfd`, `eventfd2`, `timerfd_create`, `timerfd_settime` and `timerfd_gettime` returned "not implemented". GLib falls back, but Rust I/O libraries inside Firefox do not. | All five are implemented (eventfd as dev 20, timerfd as dev 21), with read/write/poll/epoll support and blocking and non-blocking modes. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |
| 3 | Unimplemented syscalls failed silently. | The kernel records each syscall number that returned ENOSYS, with a count, and the Firefox report lists them by name. | [section 6](#part-6-linux-compatibility-libc-and-porting-applications) |

The launcher (`/usr/bin/nvx-firefox`) now also checks that `firefox-esr` is installed, waits for the X server, lists any shared libraries the loader cannot find for `firefox-esr` and `libxul.so`, and turns off the RDD and socket-process sandboxes and the crash reporter. Verified in QEMU: `afutest` passes 6/6 (socketpair, memfd, SCM_RIGHTS, cross-process shared memory, eventfd, timerfd), and launching Firefox with no Firefox installed produces the exit-code-127 report through the new watcher.

## What changed in v9.34

| # | Before (v9.33) | Now (v9.34) | Where |
|---|---|---|---|
| 1 | Running Firefox meant typing nine `export` lines, `fc-cache -f`, `startx f` and the launch command by hand. | Clicking the Firefox icon in the start menu (or typing `firefox` / `firefox-esr` in the terminal) does it all: starts X, switches to the full-screen X session, sets the sandbox-off and software-rendering variables, refreshes the font cache and launches `firefox-esr --no-remote --new-instance about:blank`. | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |

The settings live in `/usr/bin/nvx-firefox` (built into the system image by `build.sh`). The start menu routes any app whose `Exec=` contains `firefox` to `firefox_launch()` in `apps/x11/xapps.c`, which runs that script through `x_launch` and sets `g_xfull_pending` so the display switches to full-screen X as soon as the server is up. Firefox's output is appended to `/tmp/ff.log`.

## What changed in v9.33

| # | Before (v9.32) | Now (v9.33) | Where |
|---|---|---|---|
| 1 | The filesystem had only 1024 file slots (`FS_N`) and 512 directory slots (`DN_MAX`). A big `sudo apt install` (Firefox pulls thousands of files across hundreds of directories) ran out of *slots*, not RAM, and failed with `out of space or memory while unpacking` - which is why Task Manager showed only ~7 percent memory used. The on-disk NVX4 filesystem used the same limits, so installing to disk hit the identical wall. | `FS_N` is 16384 and `DN_MAX` is 4096, so the filesystem holds enough files and directories for Firefox and its full GTK/X dependency set. Verified in QEMU: 3000 files created where the old build stopped at 1024. | [section 3](#part-3-memory) |

The earlier out-of-memory on large installs was never raw RAM (the v9.30 high-memory heap already made all installed RAM usable, and ramfs file bytes allocate from it). The real limit was the fixed-size file and directory tables. Both the live-ISO ramfs and the on-disk NVX4 filesystem share `FS_N`/`DN_MAX`, so this is what blocked Firefox either way. Note: on the live ISO the installed file *bytes* still live in RAM, so a very large install also needs enough RAM; installing NoovexOS to the SSD/NVMe moves those bytes to disk. All the FS_N/DN_MAX arrays are static or heap-allocated (the NVX4 metadata image is `kmalloc`-backed and scales automatically), so nothing moved to the stack. A disk formatted by an older build is not read by v9.33 (the inode count in its metadata no longer matches), so re-run the installer for a fresh on-disk filesystem.

## What changed in v9.32

| # | Before (v9.31) | Now (v9.32) | Where |
|---|---|---|---|
| 1 | Only TCP and UDP sockets existed. `socket(AF_UNIX, ...)` failed, so multi-process Linux apps (Firefox, D-Bus, local X) could not talk between their processes. | Full AF_UNIX stream sockets: `socket`/`bind`/`listen`/`connect`/`accept`/`socketpair`, filesystem and abstract names, blocking and non-blocking, poll/epoll. `socketpair()` is the channel Firefox's parent/content/GPU processes rely on. | [section 9](#part-9-networking-and-wi-fi) |
| 2 | No way to pass an open file descriptor between processes. | `sendmsg`/`recvmsg` carry `SCM_RIGHTS` control messages, duplicating a descriptor from the sender into the receiver. This is how Firefox hands a shared-memory buffer to a child process. | [section 9](#part-9-networking-and-wi-fi) |
| 3 | `mmap` of a file copied bytes into private per-process pages, so two processes could not share writable memory. | `memfd_create` returns an anonymous, growable object (`ftruncate` sizes it); `mmap(MAP_SHARED)` maps its *actual* physical frames, reference-counted. Pass the memfd over `SCM_RIGHTS` and both processes map the same frames: real cross-process shared memory. | [section 3](#part-3-memory) |

These are Stages 1 and 2 of the Firefox-enablement plan: the kernel groundwork that unblocks the multi-process IPC and shared-memory handoff every GTK/X/Firefox process tree depends on. Verified in QEMU by a bundled test (`afutest`, runnable from the shell): socketpair data transfer, `memfd_create`+`ftruncate`+`mmap`, SCM_RIGHTS fd-passing, and, the Firefox-critical case, a child process receiving a memfd over a socketpair across `fork()` and writing to shared memory the parent reads back. All four pass. New kernel code lives in `sys/linux/linux.c` (dev 18 = AF_UNIX via `un_*`, dev 19 = memfd via `mfd_*`, `SCM_RIGHTS` in `lx_un_msg`) with close cleanup in `core/proc/jobs.c`. Remaining for a running Firefox: install the GTK/X stack via apt and software-render it (Steps 3-5), which run on real hardware against the Debian archive.

## What changed in v9.31

| # | Before (v9.30) | Now (v9.31) | Where |
|---|---|---|---|
| 1 | Only one display lit up. The kernel used the single UEFI GOP framebuffer the firmware handed it at `0x5000`, and the NVIDIA driver's own modeset is an unverified, disabled placeholder. A second connected monitor stayed dark. | The UEFI loader now enumerates every GOP output the firmware exposes (`LocateHandleBuffer`) and records any extra framebuffer in the boot-info block, with a per-display flag saying whether the kernel's page tables already cover it. When a second firmware-lit head is present, the compositor mirrors the desktop onto it each frame (`blit_mirror2`, nearest-neighbour scaled). | [section 3](#part-3-memory) |

Scope and limits, stated plainly: this lights the second monitor **only if your firmware initializes both outputs at power-on** (many do when both are connected at boot). If the firmware lights only the primary, `LocateHandleBuffer` returns one output and the second stays dark - that case needs a real display-engine modeset, which is a separate effort. The change is safe and additive: it writes no GPU registers, only uses framebuffers the firmware already set up, and only touches a second framebuffer the loader has confirmed is mapped, so a single-display boot (BIOS or one-GOP UEFI) behaves exactly as before. The boot log prints `display: second head WxH (mirroring)` when a second head is active. New code: loader enumeration in `boot/uefi/uefiboot.c`, extra-display table at `0x5040`, second-framebuffer globals and mirror in `drivers/video/fb/framebuffer.c`, read in `core/init/main.c`.

## What changed in v9.30

| # | Before (v9.29) | Now (v9.30) | Where |
|---|---|---|---|
| 1 | `kmalloc` served only the sub-4 GB region (capped at `0xFC000000`). On a real laptop that region is squeezed by the PCI hole, the kernel and the sysroot overlay, while gigabytes of RAM above 4 GB sat unused. A large `sudo apt install` (VLC, Firefox) exhausted the low region and failed with `E: out of space or memory while unpacking`, even with Task Manager showing about 7 percent memory used. | When the low heap is full, `kmalloc` pulls contiguous, identity-mapped pages from the high-memory pool above 4 GB (the pages the frame allocator already manages) and keeps serving allocations. All installed RAM is now usable for installs, decompression buffers and the package index, so big installs no longer run out of memory. | [section 3](#part-3-memory) |

Root cause of the v9.29 install failure: the direct-to-disk unpacker still needed transient RAM per package (download buffer, decompressed `data.tar`, the roughly 45 MB package index), and on the live ISO the writable filesystem is itself RAM-backed. All of that competed for the small sub-4 GB heap. v9.30 removes the 4 GB ceiling on the kernel heap. New primitives: `hp_alloc_run(pages)` in `mem/pmm_vmm.c` hands out a contiguous run of high frames; `kmalloc` in `mem/heap.c` adds extra heap arenas from it on demand, and `hblk_ok` validates pointers across every arena. Booted and verified at 6 GB; the high-memory pool self-test passes at startup.

## What changed in v9.29

| # | Before (v9.28) | Now (v9.29) | Where |
|---|---|---|---|
| 1 | apt built each package entirely in RAM (download + decompress + a copy of every file + a resident in-memory layer), so a big package could run the machine out of memory mid-install. | apt unpacks each file straight to the filesystem (the SSD/NVMe on an installed system). Peak memory stays small regardless of package size. Each package records a `/var/lib/nvpkg/<pkg>.list` manifest; `dpkg -L` and `apt remove` use it. | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 2 | Default sources had only Debian `main`. | `main contrib non-free non-free-firmware`, and installs resolve up to 4096 packages. A "run sudo apt update first" hint shows when no list is downloaded yet. | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 3 | Start-menu app scan only read the in-kernel image. | It also reads `/usr/share/applications` on disk, so apt-installed apps appear with their icons. | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |

Tested against a 20,000-package Debian-format mirror: apt update reads the full index, install writes real files to disk, dpkg -L and apt remove work off the manifest.

## What changed in v9.29

| # | Before (v9.28) | Now (v9.29) | Where |
|---|---|---|---|
| 1 | The default `/etc/apt/sources.list` had only Debian `main`. | It has `main contrib non-free non-free-firmware`, so `sudo apt install` can reach every package Debian offers (after `sudo apt update`). | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 2 | `apt install <pkg>` on a package not in the list just said "Unable to locate package". | When no Debian list has been downloaded yet, it adds a note telling you to run `sudo apt update` first. | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 3 | An install transaction was capped at 1024 packages. | 4096, so large dependency sets resolve. | [section 6.16](#616-debian-packages-apt-and-dpkg) |

Tested with a 20,000-package Debian-format mirror: `apt update` downloads and parses the full index, and `apt install` finds and installs any package in it. The real `deb.debian.org` download is only tested on real hardware, since this build environment has no route to Debian.

## What changed in v9.28

| # | Before (v9.27) | Now (v9.28) | Where |
|---|---|---|---|
| 1 | X programs were shown inside the X Display window. | X windows appear directly on the desktop like other apps (transparent full-screen overlay); mouse and keyboard go to them when you click them. | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |
| 2 | Start menu entries for X apps used NoovexOS icons. | IDLE shows Python's IDLE icon; apps installed with apt appear in the start menu with their own icons from their `.desktop` files (for example VLC's cone). | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |
| 3 | `.py` files opened in the NoovexOS Editor. | A double click opens them in IDLE. | [section 6.21](#621-x-programs-as-normal-desktop-apps-v928) |

## What changed in v9.27

| # | Before (v9.26) | Now (v9.27) | Where |
|---|---|---|---|
| 1 | No Tkinter. | Tcl/Tk 8.6 and CPython's `_tkinter` are built from source; `import tkinter` works and Tk windows appear on the X display. | [section 6.20](#620-tkinter-tk-and-idle) |
| 2 | `idle` and `idle3` opened the NoovexOS Python IDE. | They run the real IDLE (`idlelib`) from CPython 3.13 on X11, with its shell, editor, colouring and separate code process. **IDLE (Python 3.13)** is in the start menu. `python3-ide` (the NoovexOS IDE) keeps its own name. | [section 6.20](#620-tkinter-tk-and-idle) |
| 3 | X windows had no frames. | `nvxwm`: title bars, close buttons, moving and click-to-focus. | [section 6.20](#620-tkinter-tk-and-idle) |
| 4 | X programs needed `startx` first. | `idle3`, `wish`, `xdemo` and commands that use `tkinter` start the X server when needed. | [section 6.20](#620-tkinter-tk-and-idle) |
| 5 | The ISO held the kernel twice (76 MB). | The UEFI CD boot entry points at the main FAT32 partition, so the kernel is stored once. | [section 3.15](#315-the-100-mib-kernel-layout) |

## What changed in v9.26

| # | Before (v9.25) | Now (v9.26) | Where |
|---|---|---|---|
| 1 | X programs could not be used: keyboard and mouse did not reach them. | Mouse movement, left and right buttons, the wheel and typed keys in the X Display window reach X programs (through the new `/dev/nvxxin` and the XTEST helper `nvx-xinput`, started by `startx`). | [section 6.19](#619-x11-the-xorg-server-startx-appsx11xdisplayc) |
| 2 | X only ran in a window. | `startx full` runs a full-screen X session at the display's size, with the classic X background, `/root/.xinitrc` or `xdemo` started, all input going to X; F12 or Ctrl+Alt+Backspace returns to the desktop. | [section 6.19](#619-x11-the-xorg-server-startx-appsx11xdisplayc) |
| 3 | `xdemo` only drew. | It shows the pointer position, drops dots where you click, has a button that toggles, shows typed text, and quits with Esc. | [section 6.19](#619-x11-the-xorg-server-startx-appsx11xdisplayc) |

## What changed in v9.25

| # | Before (v9.24) | Now (v9.25) | Where |
|---|---|---|---|
| 1 | No X11. | The X.Org X server (Xvfb) runs on the glibc layer. `startx` starts it on `:0`; X programs connect over local TCP (`DISPLAY=127.0.0.1:0`) and appear in the new **X Display** window. `xdemo` is an Xlib test program. Keyboard and mouse input to X programs is not done yet. | [section 6.19](#619-x11-the-xorg-server-startx-appsx11xdisplayc) |
| 2 | No `epoll`. | `epoll_create1`, `epoll_ctl`, `epoll_wait`, `epoll_pwait` and `epoll_pwait2`. | [section 6.19](#619-x11-the-xorg-server-startx-appsx11xdisplayc) |

## What changed in v9.24

| # | Before (v9.23) | Now (v9.24) | Where |
|---|---|---|---|
| 1 | No Python IDE. | `sudo apt install python3-ide` (or `ide`, `idle3`) installs a Python IDE from a package repository built into the system. It appears in the start menu (Development, or Productivity in the modern menu) and in search; it colours Python code and its Run button runs the program with CPython 3.13 in the Terminal. | [section 6.18](#618-the-python-ide-package-python3-ide) |
| 2 | `apt` could only fetch over HTTP and HTTPS. | `file:` sources, and a local repository (`/usr/share/noovex/repo`) that is always available. | [section 6.18](#618-the-python-ide-package-python3-ide) |

## What changed in v9.23

This release adds real Python 3 and Debian package installation: `sudo apt install PKG` downloads `.deb` files from a Debian mirror, resolves their dependencies and unpacks them, and `python3` is CPython 3.13. Each item links to the section that explains it.

| # | Before (v9.22) | Now (v9.23) | Where |
|---|---|---|---|
| 1 | No Python 3 (Ubuntu's `python3` is a fixed-address program and cannot run). | CPython 3.13 built from source as a position-independent program, with its standard library, extension modules and their libraries in the sysroot. `python3`, `python3 -c`, script files and the interactive prompt work. | [section 6.17](#617-python-313) |
| 2 | `pkg install` only copied files out of gzip `.deb` files into `/pkg/NAME`. | `apt update`, `apt install`, `remove`, `autoremove`, `upgrade`, `search`, `show`, `list`, and `dpkg -i`, `-l`, `-L`, `-s`, `-r`. Packages come from `/etc/apt/sources.list` (Debian trixie by default), dependencies are resolved, every package is checked against its SHA256, and installed programs run at once. | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 3 | The kernel could decompress gzip only. | xz (xz-embedded) and zstd decoders in the kernel (`lib/compress/xzk.c`, `lib/compress/zstdk.c`, `lib/compress/unpack.c`). | [section 6.16](#616-debian-packages-apt-and-dpkg) |
| 4 | The sysroot was one fixed file tree with deflate per file, looked up by scanning every entry. | A stack of layers (the kernel's own plus one per installed package) with a hash index; the kernel's layer uses xz blocks (glibc and GNU tools: 7.0 MB before, 4.8 MB now). | [section 6.15](#615-glibc-programs) |
| 5 | glibc programs never saw a terminal (glibc 2.44 asks with `TCGETS2`). | `TCGETS2` and `TCSETS2` are answered, so prompts and line editing work. | [section 6.15](#615-glibc-programs) |
| 6 | Scripts with `#!` lines could not be started. | `#!/usr/bin/env python3`, `#!/bin/sh` and other interpreter lines work from the Terminal and from programs (`execve`); `/bin/sh` and `/bin/bash` are GNU bash. | [section 6.15](#615-glibc-programs) |
| 7 | A command line given to a program was cut at 255 characters. | 2047 characters. | [section 6.15](#615-glibc-programs) |
| 8 | Bare program names were looked up in `/opt/gnu/bin` only. | Also `/usr/local/bin`, `/usr/bin`, `/bin`, `/usr/sbin` and `/sbin` of the sysroot and installed packages. | [section 6.15](#615-glibc-programs) |

The source zip now holds the CPython 3.13 source (`third_party/src/cpython-3.13.tar.xz`); the glibc source is shipped next to it as `glibc-2.44.tar.xz`.

## What changed in v9.22

This release runs programs built for glibc, the C library of Debian, Ubuntu and most Linux systems, and raises the kernel image limit from 15 MiB to 100 MiB. Each item links to the section that explains it.

| # | Before (v9.21) | Now (v9.22) | Where |
|---|---|---|---|
| 1 | The kernel image had to end below 16 MiB (15 MiB of kernel). | Up to 100 MiB. The kernel's static data (BSS) moved to 256 MiB, the heap to 384 MiB, the fixed USB, SATA and NVMe buffers and the boot stack to 152-157 MiB. All three boot paths (BIOS, UEFI from disk or USB, UEFI from CD) load it; a 70 MiB test kernel booted on each. User programs now need 500 MiB of RAM (was 272 MiB). | [section 3.15](#315-the-100-mib-kernel-layout) |
| 2 | Only musl programs ran. | glibc 2.44 is built from source and shipped in the kernel as a compressed file tree (`/lib64`, with `/lib/x86_64-linux-gnu`, `/usr/lib/x86_64-linux-gnu` and `/usr/lib64` linking to it). Programs that ask for `/lib64/ld-linux-x86-64.so.2` run: threads, mutexes, `malloc`, `libm`, clocks and `uname` were checked. | [section 6.15](#615-glibc-programs) |
| 3 | No GNU tools. | GNU bash 5.2, coreutils, grep, sed, awk, find, xargs, diff and others are in `/opt/gnu/bin`, with the libraries they need. `PATH` starts with `/opt/gnu/bin`, so `bash` in the Terminal is real bash. | [section 6.15](#615-glibc-programs) |
| 4 | The Terminal expanded `$NAME` inside single quotes. | Text in single quotes is passed on unchanged, so `bash -c '...$i...'` works. | [section 6.15](#615-glibc-programs) |
| 5 | The CD's UEFI boot image was limited to 32 MiB. | Any size: the El Torito entry uses sector count 0 (the rest of the disc) and the FAT16 image grows its cluster size. | [section 3.15](#315-the-100-mib-kernel-layout) |
| 6 | No way to see what a Linux program asked the kernel. | `lxtrace N` logs the next N Linux system calls (number, first two arguments, result) to the kernel log. | [section 6.15](#615-glibc-programs) |

The source zip now also contains FFmpeg and the glibc 2.44 source (`third_party/src/glibc-2.44.tar.gz`).

## What changed in v9.21

| # | Before (v9.20) | Now (v9.21) | Where |
|---|---|---|---|
| 1 | Since v9.15 each script could run for 10 seconds, plus 1 second per 40 KB of source (up to 2 minutes more), and every timer or event callback for 10 seconds. A page with a few large or stuck scripts took 40 seconds or more. | Back to 6 seconds per call, plus 1 second per 200 KB of source (at most 8 seconds more). All page scripts together get at most 8 seconds during a load; scripts after that are skipped and named in `about:console`. | [section 13.30](#1330-load-time-limits) |
| 2 | Forced layouts for scripts could use 150 ms of every second. | 60 ms of every second. | [section 13.30](#1330-load-time-limits) |
| 3 | The pointer pump ran (and polled USB controllers) on every millisecond of network waiting. | At most once per timer tick (10 ms). | [section 13.30](#1330-load-time-limits) |
| 4 | No way to see where load time went. | After each page load the status bar and `about:console` show "Loaded in N ms: page A ms, F files B ms, engine C ms". | [section 13.30](#1330-load-time-limits) |

## What changed in v9.20

| # | Before (v9.19) | Now (v9.20) | Where |
|---|---|---|---|
| 1 | Since v9.17 every size or position query from a script laid the whole page out again when anything had changed. A page that adds an element and measures it 200 times took 1.4 seconds in the host harness. | Forced layouts share a budget of 150 ms per second; past it, queries get the last layout. `box_rect` no longer lays out by itself. The same page now takes 152 ms. | [section 13.29](#1329-faster-loading-and-form-controls) |
| 2 | Text inputs were always 153 px wide and ignored `size`. | The width follows `size` (20 characters by default), as in browsers: 184 px where Chromium gives 185 px. Range inputs are 16 px tall. `textarea` sizing reads `cols` and `rows`. | [section 13.29](#1329-faster-loading-and-form-controls) |
| 3 | Enter sent as code 13 did nothing in a text field. | Code 13 is treated as Enter, so it submits forms. | [section 13.29](#1329-faster-loading-and-form-controls) |

## What changed in v9.19

A probe page with 31 layout cases (box sizing, inline-block, floats, flex, grid, positioning, margins, tables, multi-column, sticky, transforms, pseudo-elements, logical properties) and 20 script checks was run in the engine and in Chromium, and the results compared number by number. 29 of the 31 layout cases and all 20 script checks already matched; the differences found are fixed below, apart from one.

| # | Before (v9.18) | Now (v9.19) | Where |
|---|---|---|---|
| 1 | `grid-template-areas` was ignored, so items placed with `grid-area: name` were auto-placed into one cell. | Named areas are resolved to row and column lines from the parent's `grid-template-areas`. | [section 13.28](#1328-new-tab-page-and-probe-fixes) |
| 2 | `scrollHeight` of an element whose content fits included its borders. | It is the padding box height, as in browsers. | [section 13.28](#1328-new-tab-page-and-probe-fixes) |
| 3 | The new tab page was a purple card design. | A Google-style page: the multicoloured Noovex logo, a rounded search box, "Noovex Search" and "I'm Feeling Lucky" buttons, round shortcut tiles, links at the top right and a footer bar. Shortcuts are hidden when the window is too short for them. | [section 13.28](#1328-new-tab-page-and-probe-fixes) |

Still different: an inline `<span>` is 19 px tall where Chromium makes it 17 px (font metrics).

## What changed in v9.18

Bugs found by rendering a modern landing page (dark theme, CSS variables, `color-mix()`, `clamp()`, auto-fit grid, inline SVG logo) in the engine and in Chromium side by side.

| # | Before (v9.17) | Now (v9.18) | Where |
|---|---|---|---|
| 1 | SVG `<text>` was not drawn, so logos and labels made of SVG text were missing. | `<text>` and nested `<tspan>` text is drawn with `x`, `y`, `font-size`, `font-weight`, `font-style`, `font-family`, `text-anchor`, `dominant-baseline`, fill and opacity, scaled by the SVG transform. | [section 13.27](#1327-bugs-found-against-a-modern-landing-page) |
| 2 | `color-mix()` with `transparent` (or any translucent colour) mixed the colour channels toward black, so muted text came out too dark. | Mixing uses premultiplied alpha, as the CSS specification requires. | [section 13.27](#1327-bugs-found-against-a-modern-landing-page) |
| 3 | `repeat(auto-fit, minmax(min(100%, 420px), 1fr))`, a common responsive grid, made one column, because the comma inside `min()` split the arguments. | Arguments are split at top-level commas only, and a `min(percent, length)` minimum uses the length. | [section 13.27](#1327-bugs-found-against-a-modern-landing-page) |
| 4 | `getComputedStyle(el).fontSize` and other values were empty strings while the page was loading. | Styles are computed on demand, as for the layout queries in v9.17. | [section 13.27](#1327-bugs-found-against-a-modern-landing-page) |

## What changed in v9.17

| # | Before (v9.16) | Now (v9.17) | Where |
|---|---|---|---|
| 1 | Scripts that asked for an element's size or position while the page was loading (`getBoundingClientRect`, `offsetWidth`, `clientHeight`, `scrollHeight` and others) got 0, because layout ran only after scripts. Sites that measure themselves at start-up built wrong layouts. | These calls now lay the page out first when it has changed, as browsers do. At most 30 forced layouts per 50 ms, so a script that reads and writes in a loop cannot freeze the page. | [section 13.26](#1326-layout-on-demand-for-scripts) |
| 2 | After 64 contained engine crashes the error screen came back, even across many pages. | The count is lowered at each new page load, so only a page that keeps crashing in a loop reaches it. | [section 13.22](#1322-crash-containment) |

## What changed in v9.16

This release stops the browser from freezing the desktop. Each item links to the section that explains it.

| # | Before (v9.15) | Now (v9.16) | Where |
|---|---|---|---|
| 1 | The browser engine runs inside the desktop's main loop, so a long script, a slow download or a large layout froze the whole screen, including the mouse pointer. | While the engine or the network is busy, the kernel keeps reading the mouse and redrawing the pointer about every 10 ms (`busy_pump`). This is called from the script interrupt check, from the style and layout loops, and from every network wait. | [section 13.25](#1325-staying-responsive-while-a-page-works) |
| 2 | A page stuck in a loop could only be stopped by the time limit. | Pressing Esc while one engine call has run for more than 1.5 seconds stops the page and shows "This page stopped". | [section 13.25](#1325-staying-responsive-while-a-page-works) |
| 3 | One engine tick ran up to 400 due timers whatever the cost, and every growth of the timer list leaked the old list. | A tick stops running timers after 40 ms and continues on the next one; at most 50,000 timers; the old list is freed. | [section 13.25](#1325-staying-responsive-while-a-page-works) |
| 4 | `data:` addresses were looked up as host names. | `data:` addresses (plain or base64) open in the browser. | [section 13.25](#1325-staying-responsive-while-a-page-works) |
| 5 | Missing script APIs. | `scheduler.postTask` and `scheduler.yield`, `AbortSignal.timeout`, `AbortSignal.any` and `AbortSignal.abort`, `navigator.scheduling.isInputPending`, `navigator.getBattery`, `MediaMetadata`, `document.timeline`, `Path2D`, `createImageBitmap`, `XPathResult` and `document.evaluate` (empty results), and `reportError`. | [section 13.25](#1325-staying-responsive-while-a-page-works) |

## What changed in v9.15

This release works toward running the real youtube.com in the browser. The built-in YouTube pages from v9.14 are switched off, so YouTube addresses load the real site. This could not be tested against youtube.com from the development machine.

| # | Before (v9.14) | Now (v9.15) | Where |
|---|---|---|---|
| 1 | YouTube addresses opened a built-in player page instead of the site. | They load the real site. The player page code is kept but not used. | [section 13.23](#1323-youtube-player-page) |
| 2 | There was no `Intl` object (QuickJS does not have one), so scripts that format numbers, dates or plurals stopped with a ReferenceError. | `Intl` with `NumberFormat` (decimal, percent, currency, compact), `DateTimeFormat` (with `formatToParts`, `dateStyle` and `timeStyle`), `PluralRules`, `Collator`, `RelativeTimeFormat`, `ListFormat`, `Segmenter`, `Locale` and `DisplayNames`, plus `toLocaleString` and `localeCompare` that use it. English rules only. | [section 13.24](#1324-limits-raised-for-large-web-apps) |
| 3 | Each script had 6 seconds and the JavaScript heap at most about 96 MiB, too little for multi-megabyte application bundles. | Scripts get 15 seconds plus 1 second per 40 KB of source (at most 2 minutes more). The engine arena can be up to 768 MiB when the kernel heap has room for it plus 320 MiB, the JavaScript heap 70 percent of the arena, the JavaScript stack 3 MiB and the engine stack 6 MiB. | [section 13.24](#1324-limits-raised-for-large-web-apps) |

## What changed in v9.14

This release lets NoovexOS use up to 128 GiB of RAM, finds every partition on every disk, keeps the system running when a web page crashes the engine, and plays YouTube videos through a lightweight player page. Each item links to the section that explains it.

| # | Before (v9.13) | Now (v9.14) | Where |
|---|---|---|---|
| 1 | At most 4032 MiB of RAM was recognised, and only memory below 4 GiB was mapped and used. BIOS boots had no memory map at all. | BIOS boots pass the E820 map to the kernel (through Multiboot or the native loaders). All RAM is counted, everything above 4 GiB (up to 512 GiB) is identity-mapped with 2 MiB pages, and it becomes a second frame pool that user programs use first. Tested at 8, 16 and 128 GiB with BIOS and UEFI. | [section 3.14](#314-memory-above-4-gib) |
| 2 | Partitions were found only on the install disk, only as four MBR primaries or the first 128 GPT entries, and only when they held FAT or NTFS. | A partition scanner reads MBR primaries, extended and logical partitions, GPT (up to 1024 entries, partition names, backup header) and whole-disk file systems, on every IDE drive, every SATA port, the NVMe or virtio disk and USB sticks. It names 13 file systems. `parts` (also `lsblk`) lists them; the root file system search uses it, so a NoovexOS install on a logical partition is found. | [section 8.11](#811-partition-scanner-fsvfspartsc) |
| 3 | A CPU exception inside the web engine (a bad pointer while rendering a page) stopped the whole system with the red error screen. | The exception is caught: the engine call is unwound, the browser shows "This page crashed", and the browser and the desktop keep working. `about:crash` triggers this on purpose. | [section 13.22](#1322-crash-containment) |
| 4 | YouTube's own site is too large for the engine, so watch pages did not play. | `youtube.com/watch`, `youtu.be`, `/shorts/`, `/embed/` and `/live/` addresses open a player page that asks YouTube's player API for the video (trying four clients) and plays the best progressive MP4 or WebM up to 720p in the browser's `<video>`. | [section 13.23](#1323-youtube-player-page) |

## What changed in v9.13

This release makes the web engine draw and move pages the way modern sites expect: real 2D transforms, CSS animations and transitions, downloadable fonts including icon fonts, Shadow DOM for web components, and a much faster style system. Each item links to the section that explains it.

| # | Before (v9.12) | Now (v9.13) | Where |
|---|---|---|---|
| 1 | Only `translate` was applied; `rotate`, `skew`, `scale` and `matrix` were ignored. | Every 2D transform is drawn: the element and everything in it is painted into an off-screen buffer and mapped with bilinear filtering, with `transform-origin` and the `translate`, `rotate` and `scale` properties. | [section 13.18](#1318-transforms-animations-and-transitions) |
| 2 | Animations and transitions jumped to the end state, and their events never fired. | `@keyframes` with every `animation-*` property and timing function (`ease`, `cubic-bezier()`, `steps()`), and `transition` on opacity, colours, transforms, sizes, insets, margins, paddings and visibility. `animationstart`, `animationiteration`, `animationend`, `transitionrun`, `transitionstart` and `transitionend` are dispatched. | [section 13.18](#1318-transforms-animations-and-transitions) |
| 3 | `@font-face` was ignored; pages used the three built-in families. | Web fonts in TTF, WOFF and WOFF2 (with a new Brotli decompressor and the WOFF2 glyph-table rebuild), chosen by family, weight, style and `unicode-range`. Ligatures from the font's `GSUB` table are applied, so icon fonts that turn words into icons work, as do icon fonts that use private code points. | [section 13.19](#1319-web-fonts) |
| 4 | `attachShadow` returned the element itself, so a component's template replaced its children. | Real shadow roots: the shadow tree is rendered in place of the children, `<slot>` elements show the assigned children (or their fallback), slotted content inherits from the slot, styles inside a shadow root are scoped to that component, `:host`, `:host()` and `::slotted()` work, events bubble from the shadow tree to the host, plus `assignedNodes`, `adoptedStyleSheets` and constructable `CSSStyleSheet`. | [section 13.20](#1320-shadow-dom) |
| 5 | Every new `<style>` element made the engine re-parse and re-index every stylesheet on the page, and each element was checked against every rule with a matching tag. | Parsed stylesheets are cached, the rule index is extended instead of rebuilt when sheets are only added, an ancestor filter skips rules whose ancestors cannot match, and `@media` results are cached per style pass. A test page that adds 60 style elements to 18,000 rules runs 8.6 times faster. | [section 13.21](#1321-style-system-speed) |

## What changed in v9.12

This release puts video everywhere: a media service in the kernel, a Video Player window on the desktop, and `<video>`, `<audio>` and MediaSource Extensions (MSE) in the browser, so pages that stream video in segments, as YouTube does, can play. Each item links to the section that explains it.

| # | Before (v9.11) | Now (v9.12) | Where |
|---|---|---|---|
| 1 | `play` could only take over the whole screen, and only for a local file. | A kernel media service (`core/media.c`, `/dev/media`) runs one `play --embed` process per video. The process reads its data from a local file, from an HTTP or HTTPS URL (with Range requests and an 8 MB cache), or from data the kernel was handed (one or two streams); it sends decoded frames, its status and takes commands through `ioctl`. | [section 6.13](#613-the-media-service-devmedia) |
| 2 | No video window. | A Video Player window: play and pause, a seek bar, time, mute, loop and maximize; keyboard control (space, arrows, j/l, m, f, 0-9). Double-click a video or audio file in Files, or type `video FILE` or `video URL`. | [section 6.14](#614-the-video-player-window-appsmediavideoplayerc) |
| 3 | `<video>` and `<audio>` were grey boxes with the word "Video", and the media API was a stub. | Real playback with the standard API: `play()`, `pause()`, `currentTime`, `duration`, `volume`, `muted`, `loop`, `buffered`, `readyState`, `videoWidth`, `canPlayType`, the media events (`loadedmetadata`, `canplay`, `playing`, `timeupdate`, `seeking`, `seeked`, `ended` and others), `<source>` selection, `autoplay`, native controls drawn by the engine, and the element's size taken from the video. | [section 13.17](#1317-video-and-audio-in-the-browser) |
| 4 | No MediaSource. | `MediaSource` and `SourceBuffer` with one or two buffers (split video and audio), `isTypeSupported` (H.264, H.265, VP8, VP9, AAC, MP3, Opus, Vorbis; AV1 is refused so sites fall back), `appendBuffer`, `remove`, `endOfStream`, `duration`, and `buffered` ranges computed from the fragmented MP4 (`tfdt`/`trun`) and WebM (`Cluster`) timestamps. | [section 13.17](#1317-video-and-audio-in-the-browser) |
| 5 | `fetch` and `XMLHttpRequest` returned every body as text, so binary data (video segments, images, protobuf) was corrupted, and `Blob` held only text. | Bodies are binary-safe: `fetch` gets the bytes as an `ArrayBuffer` and decodes text only when asked; `XHR` with `responseType` `arraybuffer` or `blob` gets the raw bytes; request bodies may be `ArrayBuffer`s or typed arrays; `Blob` holds bytes. `TextEncoder` and `TextDecoder` run in C, which is much faster. | [section 13.8](#138-javascript-webjsc-webpreludejs) |
| 6 | A full-screen request was refused. | `requestFullscreen` and `exitFullscreen` make the element cover the page and fire `fullscreenchange`. | [section 13.17](#1317-video-and-audio-in-the-browser) |
| 7 | While any program ran, the desktop redrew the whole screen after every scheduler slice, which left programs about a third of the CPU. | The desktop redraws when a program marks the screen dirty, the terminal changes, or twice a second. New video frames are copied only into their rectangle when the window is on top (`present_rect`). | [section 6.14](#614-the-video-player-window-appsmediavideoplayerc) |
| 8 | Four checker groups had not compiled or run since earlier releases (USB, NVMe and xHCI test templates used the old memory map; the AML test allocated above 4 GiB, which the interpreter's pointer check rejects). | All updated; the checker passes 106 of 106. | [section 12.2](#122-toolscheckpy-the-automatic-bug-checker) |

## What changed in v9.11

This release adds video and audio playback with FFmpeg, raises the kernel image limit from 8 MiB to 15 MiB, and fixes the HD Audio driver on controllers that pace codec responses. Each item links to the section that explains it.

| # | Before (v9.10) | Now (v9.11) | Where |
|---|---|---|---|
| 1 | No way to play video or compressed audio. | `play FILE` plays MP4, MKV/WebM, Ogg, MP3 and WAV fullscreen with sound: H.264, H.265, VP9, VP8, AAC, MP3, Opus and Vorbis, decoded in software on all cores with FFmpeg's SIMD code. Space pauses, the arrow keys seek 10 or 60 seconds, `q` quits; `play -b` benchmarks the decoder and `play -s SECONDS out.ppm` saves a frame. | [section 6.12](#612-ffmpeg-and-the-play-command) |
| 2 | The kernel image had to end below `0x900000` (8 MiB); the boot stack was at `0x980000`, the USB/AHCI/NVMe buffers at `0x990000`-`0xC00000` and `.bss` at `0xC00000`. | The image may use `0x100000`-`0x1000000` (15 MiB). Everything above moves up by 7 MiB: boot stack top `0x1080000`, xHCI memory `0x1090000`, mass-storage rings `0x1150000`, AHCI `0x1200000`, NVMe `0x1230000`, `.bss` at `0x1300000`. stage2 reads up to 15 MiB, and the UEFI loader, installer snapshot, integrity check and linker scripts use the same limit. | [section 2.13](#213-size-limits-along-the-chain) |
| 3 | The HD Audio driver never set the RIRB interrupt-enable bit. On controllers that count responses against it (QEMU's `intel-hda` does), the codec answered only the first verb, so no output path was found ("hda: no usable output path"). | `RIRBCTL` is set to `0x03`; the global interrupt stays off, so nothing fires, but the response counter is reset each time the driver acknowledges a response. The error message now lists what was found. | [section 10.9.3](#1093-intel-hd-audio-driverssoundhdac) |
| 4 | `/dev/fb0` had no way to tell a program the screen size, assumed a stride equal to the width, and copied byte by byte. | `FBIOGET_VSCREENINFO` (`0x4600`) and `FBIOGET_FSCREENINFO` (`0x4602`) report the size, 32 bpp and the real line length; reads and writes use the stride and copy with `memcpy`. When a fullscreen program closes `/dev/fb0`, the live terminal repaints completely. | [section 6.12](#612-ffmpeg-and-the-play-command) |

## What changed in v9.10

This release makes the web engine handle more of the modern web: rich-text editors, streamed responses, modern colour syntax, nested CSS and several layout fixes. Each item links to the section that explains it.

| # | Before (v9.09) | Now (v9.10) | Where |
|---|---|---|---|
| 1 | `margin: 0 auto` (and `margin-inline: auto`) never centred a block: the parent placed the box before its auto margins were computed. | `lay()` moves the box by the auto margin that `block_width` computes, so centred page wrappers are centred. | [section 13.6](#136-layout-weblayoutc-weblayout2c) |
| 2 | `contenteditable` elements could not be focused or typed into, and `Selection`, `Range` and `MutationObserver` were stubs. | Editing hosts take focus, a click places the caret at the clicked character, and keys go through `beforeinput`, a DOM edit (insert text, delete, split paragraph) and `input`. `Selection` and `Range` are real (anchor and focus, `setBaseAndExtent`, `deleteContents`, `insertNode`, `getBoundingClientRect`), `MutationObserver` reports child, attribute and text changes, and `document.execCommand('insertText')` works. The caret is drawn by the engine. | [section 13.8](#138-javascript-webjsc-webpreludejs) |
| 3 | `response.body` was always `null` and there were no streams. | `ReadableStream`, `WritableStream`, `TransformStream`, `TextDecoderStream` and `TextEncoderStream`; `Response.body` is a stream, and a `Response` can be built from one. | [section 13.8](#138-javascript-webjsc-webpreludejs) |
| 4 | `oklch()`, `oklab()`, `lab()`, `lch()`, `light-dark()`, `color()` and system colours were not understood, so sites built with Tailwind v4 lost most of their colours. | All of them are parsed, including inside `color-mix()` and gradients. | [section 13.5](#135-css-webcssparsec-webcssstylec) |
| 5 | Nested CSS rules (`.a { & .b {} }`) were dropped. | Nested rules, `&`, and nested `@media`/`@supports`/`@layer` are flattened while the sheet is parsed. | [section 13.5](#135-css-webcssparsec-webcssstylec) |
| 6 | Visually hidden elements (`clip: rect(0 0 0 0)`, `clip-path: inset(50%)`, 1-px boxes) were painted, for example a "Choose File" box over ChatGPT's message field. | They are not painted. | [section 13.7](#137-painting-fonts-and-images) |
| 7 | `text-overflow: ellipsis`, `-webkit-line-clamp`, `position: sticky`, `-webkit-background-clip: text` and `-webkit-text-fill-color` had no effect, and auto grid rows did not stretch to the container height. | All implemented (gradient text is drawn in the gradient's first colour); `place-items: center` in a fixed-height grid centres vertically. | [section 13.6](#136-layout-weblayoutc-weblayout2c) |
| 8 | Text inputs sent `input` events without `data` and never sent `beforeinput`. | `beforeinput` (cancelable) and `input` carry `inputType` and `data`. | [section 13.8](#138-javascript-webjsc-webpreludejs) |

## What changed in v9.09

| Before (v9.08) | Now (v9.09) | Where |
|---|---|---|
| The web engine's allocator (`web/krt.c`) served requests of 60,593 to 65,536 bytes from its largest size class, which is only 60,592 bytes. Up to about 5 KB was written past the block, which corrupted QuickJS data (seen as a page fault in `__JS_NewAtom` with `RAX = 0xF000FF53`, a value read from the real-mode interrupt table at address 8). | Only requests that fit the largest class use the size classes; everything else uses the large-block path. A host test checks every size up to 300,000 bytes. | [section 13.3](#133-memory) |

## What changed in v9.08

This release replaces the browser with a real web engine: an HTML parser and DOM, the CSS cascade, a layout engine with flexbox and grid, the QuickJS JavaScript engine with the browser API, and a cookie jar that is saved to disk. It also adds the `qjs` command. Each item links to the section that explains it.

| # | Before (v9.07) | Now (v9.08) | Where |
|---|---|---|---|
| 1 | The browser was a single-pass tag scanner that turned a page into lines of text with some colours. | A layout engine in `web/`: HTML parser with the usual implied-tag and table rules, a DOM, selectors up to `:has()`, the full cascade with `!important`, custom properties and `calc()`, block and inline layout, floats, positioning, `z-index`, overflow and scrolling boxes, tables, flexbox and grid, anti-aliased TrueType text, gradients, shadows, rounded corners, SVG. | [Part 13](#part-13-the-web-engine-javascript-and-cookies) |
| 2 | An integer-only JavaScript subset with `document.write` and no DOM. | QuickJS-ng (ES2023) with DOM bindings and a browser API written in JavaScript: events, `classList`, `style`, `fetch`, `XMLHttpRequest`, `localStorage`, `history`, timers, observers, modules. React 19 and jQuery 3.6 run. Scripts are limited to 6 s per run and to 60% of the engine arena. | [section 13.8](#138-javascript-webjsc-webpreludejs) |
| 3 | Cookies were kept in a small table in `net/http/http.c`. | A cookie jar (`net/http/cookies.c`) with domain and path matching, expiry, Secure, HttpOnly and SameSite, saved to `cookies.txt`; `localStorage` saved to `webstorage.txt`; an about:cookies page and a Cookies button. | [section 13.10](#1310-cookies-and-web-storage-nethttpcookiesc) |
| 4 | No way to run modern JavaScript from the shell. | `qjs`, the QuickJS command-line interpreter, as an embedded musl program. | [section 13.13](#1313-the-qjs-command) |
| 5 | `browse` did not exist, and opening the browser from the live terminal left the window hidden behind the terminal. | `browse <url>` (also `www`) opens the browser and, in the live terminal, switches to the desktop first; `desktop` switches without opening anything. | [section 13.11](#1311-the-browser-window-appsinternetbrowserc) |
| 6 | The boot stack started at `0x700000`, directly above a kernel that could be at most 5.75 MB. The 6.7 MB kernel was overwritten by its own stack and the integrity check stopped the boot. | The stack top is `0x980000`, the kernel may be 8 MiB (`0x100000-0x900000`), the UEFI loader's `KERNEL_MAX` matches, and both linker scripts fail the build if the image is too large. | [section 13.12](#1312-memory-layout-change-in-v908) |
| 7 | ChaCha20-Poly1305 used a fixed scratch buffer at `0x900000`. | It uses a heap buffer (`aead_mac`), so it no longer depends on that memory being free. | [section 13.12](#1312-memory-layout-change-in-v908) |

Other changes:

| Change | Why |
|---|---|
| The browser's string copies now always end the string (`sset`). | The tab title showed the start of the new host name followed by the end of the old title, and Back went to the wrong page. |
| Page fonts include about 80 symbols from DejaVu Sans (check marks, arrows, stars, menu and warning icons), used by all faces. | Liberation has none of them, so they showed as `?`. |
| The stack-switch trampoline moved to `arch/x86_64/user/stack_call.c`. | The static checker allows inline assembly only under `arch/`. |
| The UEFI simulator (`tools/tests/pe_run.c`) emulates 32-bit port I/O. | The loader's `quiet_dma` reads PCI configuration space with `in`/`out` on `eax`, which the simulator treated as a crash. |
| The `mhello` check accepts any version number. | It looked for "8.97" in the program's output. |

## What changed in v8.98 to v9.07

These releases fix the installer crash on real hardware, speed up USB mice, give the browser better CSS and images, and replace the desktop with a classic taskbar and start menu. Each item links to the section that explains it.

| # | Before (v8.97) | Now (v9.07) | Where |
|---|---|---|---|
| 1 | On some UEFI laptops the kernel was already corrupted when it started: `kcheck` said `code DIFFERS`, the installer refused to copy it, and the system could page-fault (seen in `anode_child` with a pointer of all ones). The firmware's USB controller kept writing into memory that the loader had just filled with the kernel. | The UEFI loader stops DMA right after `ExitBootServices` and before it copies the kernel: it halts every xHCI controller (clears `USBCMD.RS`, waits for `USBSTS.HCH`) and clears Bus-Master Enable on storage, network, multimedia, wireless and serial-bus devices (`quiet_dma`). | [section 2.4](#26-uefi-path-bootuefiuefibootc) |
| 2 | `ahci_init` and `nvme_init` turned on bus mastering before checking the BAR and before stopping the controller, so a rejected or still-running controller could resume DMA into reclaimed memory. | Both enable memory space first. AHCI stops every implemented port and clears its interrupts; NVMe disables the controller. Only then is bus mastering switched on. | [section 8.3](#83-ahci-driversstoragegdiskc) |
| 3 | A corrupt AML namespace link crashed the kernel. | `anode_child` checks every node pointer (below 4 GiB, above 1 MiB, 8-byte aligned) and stops the walk instead of faulting. | [section 10.7.3](#1073-aml-interpreter-driversacpiamlc) |
| 4 | A USB mouse or keyboard had one read queued at a time. A 1000 Hz gaming dongle lost most reports during a redraw, so the pointer was slow and lagged. | 16 reads are queued per HID endpoint, each with its own 64-byte buffer; every completed report is decoded from its own buffer and immediately re-queued. | [section 10.6.5](#1065-hid-data-path) |
| 5 | The browser understood hex colours and a few names, one page background colour, PNG/JPEG/WebP/BMP, and 600 lines per page. | About 140 CSS colour names, `rgb()`/`rgba()`/`hsl()`/`hsla()`, 4- and 8-digit hex; `linear-gradient` page backgrounds; block backgrounds, `text-align` and `font-size` from rules, inline `style` and `bgcolor`/`align`; paragraph spacing; GIF images; alpha blending; 2400 lines. | [section 11.7](#browser-appsinternetbrowserc-698-lines) |
| 6 | The JavaScript interpreter had six string methods and small tables. | Many more string methods (`split`, `replace`, `trim`, `slice`, `padStart` and others), array `shift`, `unshift`, `reverse`, `slice`, `includes`, `concat`, `Math.pow/sign/trunc/hypot`, and larger limits. | [section 11.9](#javascript--c-script-interpreter-libjsjsc) |
| 7 | A top bar, a floating dock, desktop icons and a centred launcher. | A classic desktop: a gray 3D taskbar with a NoovexOS start button, one button per open window, a tray and a clock; a classic start menu with Development, Games, Graphics and Utilities submenus and Exit; no desktop icons. | [section 11.3](#113-desktop-shell-and-compositor-graphicsuidesktopc) |
| 8 | Windows had rounded light frames and a 40-px title bar. | Classic windows: a raised gray frame, a navy-to-blue gradient title bar (gray when inactive), square 3D caption buttons, and a 30-px title bar (`TITLEH 30`). | [section 11.2](#112-window-manager-graphicsuiwmc) |
| 9 | The tray had Wi-Fi, quick settings (wallpaper and accent) and a bell. | A Windows 11 style tray (show more, Wi-Fi, volume, battery, notifications) and a Quick Settings panel with Wi-Fi, Airplane, Night light, Battery saver, Do not disturb and All settings, plus brightness and volume sliders. | [section 11.3](#tray-and-quick-settings) |
| 10 | Most text used the 1-bit pixel font. | `dchar` draws the anti-aliased `fontaa` glyphs at the same 8-px width, so every app gets smooth text without layout changes. | [section 11.11](#1111-fonts-and-assets-assets) |
| 11 | Aurora was the default wallpaper. | Crane is the default on every boot and is preselected in setup. | [section 11.3](#themes-and-effects-graphicsthemethemec-graphicsthemeeffectsc-appssystemsettingsc) |

Other changes:

| Change | Why |
|---|---|
| Settings opens larger (up to 1000 x 800, sized to the screen), with a wider sidebar, more space between cards, smooth text, bigger toggles, rounded buttons and a softer segmented control. | The control center was cramped. |
| The `kcheck` investigation found the Lenovo boot copy differed from the build while nothing had changed since boot. | That pointed at the loader, not the kernel (item 1). |

## What changed in v8.97

This release adds threads, dynamic linking, a real terminal, timers, server sockets and TLS for programs, and raises the old fixed limits. Each item links to the section that explains it.

| # | Before (v8.96) | Now (v8.97) | Where |
|---|---|---|---|
| 1 | `clone` with `CLONE_THREAD` returned ENOSYS and `futex` never waited, so SQLite, xz and zstd were built single-threaded. | Real threads: a shared address space, descriptor table and signal handlers, threads running in parallel on all cores, futex wait and wake (with requeue, wake-op and bitsets), `gettid`, `tgkill`, `set_tid_address`, `exit` versus `exit_group`, `execve` from a threaded program. musl's pthreads work unchanged; SQLite is thread-safe and `xz -T` and `zstd -T` compress in parallel. | [section 5.15](#515-threads-and-futexes) |
| 2 | Only static programs ran. | Dynamic linking: the kernel loads `PT_INTERP`, the shared musl `libc.so` is the dynamic linker `/lib/ld-musl-x86_64.so.1`, and `dlopen` loads libraries from disk. The ports are now dynamic programs. | [section 6.10](#610-dynamic-linking) |
| 3 | The console dropped escape sequences, so colours and cursor movement vanished, and termios settings were ignored. | An xterm-style terminal: colours (16, 256 and true colour), cursor movement, erase, insert and delete, scroll regions, the alternate screen, cursor position reports, cursor hiding; termios raw mode, echo control, `VMIN`/`VTIME`, arrow and function keys as escape sequences. The grid is now up to 240 x 72 cells. The kilo editor is included. | [section 11.12](#1112-escape-sequences-termios-and-full-screen-programs) |
| 4 | `alarm` and `setitimer` were stubs. | `alarm`, `setitimer` and `getitimer` with SIGALRM (`ITIMER_REAL`). | [section 5.16](#516-interval-timers-and-alarm) |
| 5 | 32 MiB of user memory for everything, 32 descriptors per process, 32 open files and 12 pipes system-wide, 64 processes, a 64 KiB stack. | The frame pool grows with RAM (32 MiB plus half the free heap, up to 2 GiB more), 256 descriptors per process, 1024 open files, 128 pipes, 256 process/thread slots, 128 sockets, a 1 MiB stack, and `select` on any descriptor. | [section 3.7](#37-frame-pool-pframe_), [section 5.9](#59-file-descriptors-and-pipes) |
| 6 | `listen` and `accept` returned EOPNOTSUPP, and loopback traffic was refused. | Server sockets (`listen`, `accept`, `accept4`, a listen queue with backlog) over the network card and over a new loopback path for 127.0.0.1 and the machine's own address. | [section 9.7.3](#973-loopback), [section 9.7.4](#974-listening-sockets-listen-accept) |
| 7 | No TLS in user space. | BearSSL (upstream, MIT) for musl programs, a helper that loads the kernel's 120-root store (written to `/etc/ssl/nvx-anchors.pem` at boot) and `/etc/ssl/local-ca.pem`, and `fetch`, an HTTP/HTTPS downloader with certificate checks. | [section 6.11](#611-tls-for-programs-bearssl-fetch-and-the-root-store) |

Other changes:

| Change | Why |
|---|---|
| Embedded programs and the shared C library are stored compressed (raw DEFLATE) and inflated on first use. | The kernel went from 5.46 MB to 3.86 MB, which made room for the new programs; it is now about 4.28 MB, 1.75 MB under the UEFI limit ([section 6.6](#66-how-programs-are-embedded-and-found)). |
| `getrandom`, `/dev/random` and `/dev/urandom` return output of the kernel's ChaCha20 generator. | They were an xorshift or LCG sequence, not suitable for TLS keys ([section 9.8](#98-http-entropy-and-https-nethttphttpc)). |
| The kernel's random generator checks CPUID before using RDSEED or RDRAND. | It used RDSEED unconditionally, which crashes on CPUs without it (found when programs started asking for random bytes in QEMU). |
| TLB shootdowns that wait for the other cores, for copy-on-write, fork and `munmap` in a process with threads. | Threads of one address space run on several cores at once ([section 4.15](#415-tlb-coherence)). |
| User memory is limited to PML4 slot 8 (`0x40000000000`-`0x47FFFFFFFFF`) everywhere; `mmap` searches the whole slot for a gap. | `jc_uok` allowed addresses up to `0x7FFFFFFFFFF`, but only slot 8 is cloned on fork and freed on exit. |
| Native programs' writes no longer sync the file to disk on every `write`; the file is written back when it is closed. | `seq 1 30000 >file` took more than 10 s in QEMU ([section 5.12](#512-the-native-int-0x80-system-call-abi)). |
| The program table had room for 80 entries but 81 were listed. | The compiler dropped the end marker; the table's size now comes from its contents. |
| The system-call fuzzer no longer calls `futex`, `accept`, `alarm` and `setitimer`. | With real waiting these can legitimately block the fuzzer forever or end it with SIGALRM. |

New tests: `featest` (52 checks for descriptors, memory, timers, server sockets, threads and the terminal), `dyntest` (13 checks for the dynamic linker and `dlopen`), the `fetch` checker group (BearSSL and `fetch` on the host under sanitizers against local TLS 1.2 servers), and in the `net` group `fetch` against the test peer and a connection from the host into a musl server program ([section 12.2](#122-toolscheckpy-the-automatic-bug-checker)).

## What changed in v8.96

The kernel is now built from separate modules instead of one giant source file.

| Before (v8.95) | Now (v8.96) |
|---|---|
| `kernel64.c` included every kernel `.c` file into one translation unit; nearly everything was `static` and visible to everything after it. | 104 compilation units listed in `units.list`, each compiled on its own. |
| No interfaces: the include order in `kernel64.c` decided what a file could use. | One interface header per unit under `include/nvx/`, with shared types, constants, macros, `extern` variables, prototypes and small inline helpers. About 1,850 names are shared; the other 2,400 stay `static` in their file. |
| Every build recompiled the whole kernel (about 20 s). | `tools/kbuild.py` compiles only changed units, in parallel, using `gcc -MMD` dependency files. A full build takes about 12 s on 2 cores; changing one `.c` file recompiles one unit. |
| Units silently depended on whatever came earlier. | A unit includes only the headers it uses (10 on average), and each header includes its own dependencies, so a missing include is a compile error. |
| `.h` files that really held code or data (`include/percpu.h`, `arch/x86_64/simd.h`, `lib/js.h`, `lib/crypto/*.h`, the fonts). | They are units now: `arch/x86_64/cpu/percpu.c`, `arch/x86_64/cpu/simd.c`, `lib/js/js.c`, `lib/crypto/*.c`, `assets/font.c`, `assets/fontaa.c`. |

The behaviour of the system is unchanged: the full checker passes 86 of 86 checks (including the QEMU multi-core, socket and HTTPS runs), and the image boots on BIOS and UEFI. The kernel is about 24 KB larger because the compiler no longer inlines across files. Part 1, section 3 explains the layout and how to add a unit; section 12.2 explains how the test generators read the split sources.

## What changed in v8.95

This release fixes the problems found in the v8.94 review and adds the features those fixes needed. Each item links to the part that explains it in full.

| # | Problem in v8.94 | What v8.95 does | Where |
|---|---|---|---|
| 1 | The installer copied the running kernel into a 3 MB buffer, but the kernel is about 5 MB, so the copy failed and every installation stopped. | The boot-time snapshot holds up to 8 MB (the build limit), and the check works on a heap copy instead of a second static buffer. `kcheck` reports `code MATCHES   whole file MATCHES`. | [section 2.12](#212-kernel-signatures-and-integrity), [section 8.7](#87-the-installer-coreinitinstallerc) |
| 2 | `disks` re-pointed the AHCI port's command list and FIS buffers at its own probe buffers and never pointed them back, so disk access failed afterwards. | `ahci_identify` saves the port registers and restores them (stopping and restarting the port) after the probe. | [section 8.3](#83-ahci-driversstoragegdiskc) |
| 3 | The integrity check only warned. | A hash mismatch stops the boot at the "WRONG VERSION" screen. R restarts, C continues at your own risk. Unstamped developer builds are not blocked. | [section 2.12](#212-kernel-signatures-and-integrity) |
| 4 | Linux programs could not use the network: `socket()` returned `EAFNOSUPPORT`. | New socket layer (`sys/linux/lxsock.c`, device 14): TCP and UDP over IPv4 with `connect`, `send/recv(from/msg)`, `bind`, `shutdown`, `poll`/`select`, non-blocking mode, retransmission and flow control. musl's `getaddrinfo` works without configuration. | [section 9.7](#97-sockets-netstacksocketsc), [section 6.9](#69-step-by-step-porting-guide) |
| 5 | Sockets and HTTPS read only from the wired card and checked a wired-only flag, so they could not work over Wi-Fi. | Every receive path goes through the active link (`g_rx`), every sender checks `nd_up()`, and `nic_up()` reports the Wi-Fi link when Wi-Fi is active. | [section 9.5](#95-the-netdev-indirection-ethernet-vs-wi-fi) |
| 6 | TLS did not verify the server at all. | Full server authentication: X.509 chain to one of 120 built-in Mozilla roots (or your own in `/etc/ssl/local-ca.pem`), host name (SAN, wildcards, IP), validity dates, the CertificateVerify signature (ECDSA P-256/P-384, RSA-PSS) and the server Finished. `https -k` skips the checks for one request. | [section 9.9](#99-tls-13-client-and-crypto-library) |
| 7 | The Disk Manager could not be reached. | `diskmgr` (or `partitions`) opens it, and P opens it from the installer. | [section 8.8](#88-disk-manager-dm_-coreinitinstallerc-lines-1-104) |
| 8 | `smp_tlb_shootdown` and `smp_kick_idle` were never called, and a stale comment in `kmain` said the cores start only on demand. | High-MMIO mapping triggers a TLB shootdown, new processes wake idle cores at once, and the comment is gone. | [section 4.14](#414-ap-runtime-archx86_64smpsmp_schedc), [section 4.15](#415-tlb-coherence) |

Problems found and fixed while testing these changes:

| Problem | Fix |
|---|---|
| The kernel never answered ARP requests, so after the gateway's entry for us expired it dropped our SYN-ACKs. Every second HTTPS connection failed. | `arp_auto_reply` in every receive loop ([section 9.6.3](#963-arp)) |
| Wired TCP/UDP sent nothing until `wifi eth` had been run once (`nd_tx` had no fallback). | `nd_tx`/`nd_rx` fall back to the wired card ([section 9.5](#95-the-netdev-indirection-ethernet-vs-wi-fi)) |
| The TLS transport acknowledged a whole TCP segment even when it had copied only part of it, losing data. | `tcps_rd` acknowledges only what it copied ([section 9.6.7](#967-tcp)) |
| No MSS option and a fixed 8 KiB window made downloads slow (300 KB in about 6 s in QEMU). | SYNs carry MSS 1400; Linux sockets advertise their free buffer space (300 KB in about 0.05 s) |
| The terminal cut commands at 127 characters. | 254 characters ([section 11.5](#115-live-terminal-mode-vs-desktop-terminal)) |
| `>>file`, `<file` and `2>file` written without a space were passed to programs as arguments; `>>` on built-in commands truncated. | Both fixed ([section 11.8](#118-the-shell-nsh)) |
| SHA-256 shifted a byte left by 24 bits as a signed `int` (undefined behaviour found by UBSan). | Cast to `u32` first |

New tests: the `tls` checker group (117 Mozilla root signatures plus 31 generated chain and signature cases, under ASan/UBSan) and the `net` group (a QEMU guest on a tap device against a scripted peer: `nettest`, HTTP, and HTTPS with good, unknown, wrong-name and `-k` cases). See [section 12.2](#122-toolscheckpy-the-automatic-bug-checker).

Note: The Wi-Fi changes were checked by reading the code and by the wired tests, which use the same paths; they have not been run on an RTL8821CE yet. Please report what `netlog` shows if HTTPS over Wi-Fi fails on your laptop.

## Contents

- [Part 1: Overview, source layout and build system](#part-1-overview-source-layout-and-build-system)
  - [1. What NoovexOS 64 is](#1-what-noovexos-64-is)
  - [2. Repository layout](#2-repository-layout)
  - [3. Modules and interface headers](#3-modules-and-interface-headers)
  - [4. `build.sh`, step by step](#4-buildsh-step-by-step)
  - [5. Linker scripts](#5-linker-scripts)
  - [6. Bootloader build and image tools](#6-bootloader-build-and-image-tools)
  - [7. Toolchain requirements](#7-toolchain-requirements)
  - [8. Versioning](#8-versioning)
- [Part 2: Boot chain, kernel entry and kernel signatures](#part-2-boot-chain-kernel-entry-and-kernel-signatures)
  - [2.1 The boot paths at a glance](#21-the-boot-paths-at-a-glance)
  - [2.2 Boot media: the hybrid image (`boot/mkhybrid.py`)](#22-boot-media-the-hybrid-image-bootmkhybridpy)
  - [2.3 BIOS path: myboot (`boot/src`)](#23-bios-path-myboot-bootsrc)
  - [2.4 `boot/myboot/` (stand-alone variant)](#24-bootmyboot-stand-alone-variant)
  - [2.5 Legacy custom loaders in `arch/x86_64/boot/`](#25-legacy-custom-loaders-in-archx86_64boot)
  - [2.6 UEFI path (`boot/uefi/uefiboot.c`)](#26-uefi-path-bootuefiuefibootc)
  - [2.7 The boot-info contract (fixed low memory)](#27-the-boot-info-contract-fixed-low-memory)
  - [2.8 Kernel entry: `arch/x86_64/boot/boot64.S`](#28-kernel-entry-archx86_64bootboot64s)
  - [2.9 `kmain()` step by step (`core/init/main.c`)](#29-kmain-step-by-step-coreinitmainc)
  - [2.10 Main loop and state machine](#210-main-loop-and-state-machine)
  - [2.11 Boot menu, recovery, setup and splash](#211-boot-menu-recovery-setup-and-splash)
  - [2.12 Kernel signatures and integrity](#212-kernel-signatures-and-integrity)
  - [2.13 Size limits along the chain](#213-size-limits-along-the-chain)
  - [2.14 Gotchas](#214-gotchas)
- [Part 3: Memory](#part-3-memory)
  - [3.1 Boot order](#31-boot-order)
  - [3.2 Physical memory map (firmware map)](#32-physical-memory-map-firmware-map)
  - [3.3 Fixed physical regions](#33-fixed-physical-regions)
  - [3.4 Boot page tables](#34-boot-page-tables)
  - [3.5 Kernel heap (`mem/heap.c`, `mem/heap_api.c`)](#35-kernel-heap-memheapc-memheap_apic)
  - [3.6 DMA arena (`dma_alloc`, `dma_alloc_pages`)](#36-dma-arena-dma_alloc-dma_alloc_pages)
  - [3.7 Frame pool (`pframe_*`)](#37-frame-pool-pframe_)
  - [3.8 Virtual memory (`mem/pmm_vmm.c`, `core/proc/jobs.c`)](#38-virtual-memory-mempmm_vmmc-coreprocjobsc)
  - [3.9 User address space](#39-user-address-space)
  - [3.10 `mmap`, `munmap`, `brk` (`sys/linux/linux.c`)](#310-mmap-munmap-brk-syslinuxlinuxc)
  - [3.11 Page-fault path and OOM](#311-page-fault-path-and-oom)
  - [3.12 File-system memory (`fs/vfs/ramfs.c`)](#312-file-system-memory-fsvfsramfsc)
  - [3.13 Summary of limits](#313-summary-of-limits)
  - [3.14 Memory above 4 GiB](#314-memory-above-4-gib)
  - [3.15 The 100 MiB kernel layout](#315-the-100-mib-kernel-layout)
- [Part 4: CPU, interrupts, timers and multi-core](#part-4-cpu-interrupts-timers-and-multi-core)
  - [4.1 `arch/x86_64/cpu/cpu.h`, the CPU interface](#41-archx86_64cpucpuh-the-cpu-interface)
  - [4.2 `arch/x86_64/cpu/cpu_init.c`](#42-archx86_64cpucpu_initc)
  - [4.3 GDT, selectors, TSS and IST stacks (`arch/x86_64/user/ring3.c`)](#43-gdt-selectors-tss-and-ist-stacks-archx86_64userring3c)
  - [4.4 IDT gates and the vector map (`arch/x86_64/interrupts/interrupts.c`)](#44-idt-gates-and-the-vector-map-archx86_64interruptsinterruptsc)
  - [4.5 Interrupt frame and `isr_common`](#45-interrupt-frame-and-isr_common)
  - [4.6 `isr_dispatch` and `isr_body`](#46-isr_dispatch-and-isr_body)
  - [4.7 PIC and PIT](#47-pic-and-pit)
  - [4.8 High-resolution timers (`arch/x86_64/time/hrtimer.c`)](#48-high-resolution-timers-archx86_64timehrtimerc)
  - [4.9 Per-CPU state (`arch/x86_64/cpu/percpu.c`)](#49-per-cpu-state-archx86_64cpupercpuc)
  - [4.10 The big kernel lock (`arch/x86_64/cpu/percpu.c`)](#410-the-big-kernel-lock-archx86_64cpupercpuc)
  - [4.11 System calls: `int 0x80` and `syscall`](#411-system-calls-int-0x80-and-syscall)
  - [4.12 Multi-core discovery (`arch/x86_64/smp/smp.c`)](#412-multi-core-discovery-archx86_64smpsmpc)
  - [4.13 AP trampoline (`arch/x86_64/boot/ap_trampoline.S`)](#413-ap-trampoline-archx86_64bootap_trampolines)
  - [4.14 AP runtime (`arch/x86_64/smp/smp_sched.c`)](#414-ap-runtime-archx86_64smpsmp_schedc)
  - [4.15 TLB coherence](#415-tlb-coherence)
  - [4.16 The `smp` shell commands (`shell/commands.c`)](#416-the-smp-shell-commands-shellcommandsc)
- [Part 5: Processes, scheduling, signals and the native system-call ABI](#part-5-processes-scheduling-signals-and-the-native-system-call-abi)
  - [5.1 Per-CPU scheduling state](#51-per-cpu-scheduling-state)
  - [5.2 The process table: `struct jproc`](#52-the-process-table-struct-jproc)
  - [5.3 Process states](#53-process-states)
  - [5.4 Life cycle: create, build, fork, exec, finish, wait](#54-life-cycle-create-build-fork-exec-finish-wait)
  - [5.5 The setjmp/longjmp scheduling model](#55-the-setjmplongjmp-scheduling-model)
  - [5.6 Blocking and restartable system calls](#56-blocking-and-restartable-system-calls)
  - [5.7 Signals](#57-signals)
  - [5.8 Job control and the terminal](#58-job-control-and-the-terminal)
  - [5.9 File descriptors and pipes](#59-file-descriptors-and-pipes)
  - [5.10 The ELF loader (`core/proc/elf.c`)](#510-the-elf-loader-coreprocelfc)
  - [5.11 Embedded programs: `JS_BIN` and `jc_find_prog`](#511-embedded-programs-js_bin-and-jc_find_prog)
  - [5.12 The native `int 0x80` system-call ABI](#512-the-native-int-0x80-system-call-abi)
  - [5.13 The legacy scheduler and demos (`core/proc/sched.c`)](#513-the-legacy-scheduler-and-demos-coreprocschedc)
  - [5.14 init (PID 1) and `/etc/rc`](#514-init-pid-1-and-etcrc)
  - [5.15 Threads and futexes](#515-threads-and-futexes)
  - [5.16 Interval timers and alarm](#516-interval-timers-and-alarm)
- [Part 6: Linux compatibility, libc and porting applications](#part-6-linux-compatibility-libc-and-porting-applications)
  - [6.1 How a Linux system call reaches the kernel](#61-how-a-linux-system-call-reaches-the-kernel)
  - [6.2 File-related details](#62-file-related-details)
  - [6.3 poll/select, time, memory, processes, signals](#63-pollselect-time-memory-processes-signals)
  - [6.4 Complete Linux system-call table](#64-complete-linux-system-call-table)
  - [6.5 The C libraries](#65-the-c-libraries)
  - [6.6 How programs are embedded and found](#66-how-programs-are-embedded-and-found)
  - [6.7 The ported programs](#67-the-ported-programs)
  - [6.8 Test programs](#68-test-programs)
  - [6.9 Step-by-step porting guide](#69-step-by-step-porting-guide)
  - [6.10 Dynamic linking](#610-dynamic-linking)
  - [6.11 TLS for programs: BearSSL, `fetch` and the root store](#611-tls-for-programs-bearssl-fetch-and-the-root-store)
  - [6.12 FFmpeg and the `play` command](#612-ffmpeg-and-the-play-command)
  - [6.13 The media service (`/dev/media`)](#613-the-media-service-devmedia)
  - [6.14 The Video Player window (`apps/media/videoplayer.c`)](#614-the-video-player-window-appsmediavideoplayerc)
  - [6.15 glibc programs](#615-glibc-programs)
  - [6.16 Debian packages: apt and dpkg](#616-debian-packages-apt-and-dpkg)
  - [6.17 Python 3.13](#617-python-313)
  - [6.18 The Python IDE package (`python3-ide`)](#618-the-python-ide-package-python3-ide)
  - [6.19 X11: the X.Org server (`startx`, `apps/x11/xdisplay.c`)](#619-x11-the-xorg-server-startx-appsx11xdisplayc)
  - [6.20 Tkinter, Tk and IDLE](#620-tkinter-tk-and-idle)
  - [6.21 X programs as normal desktop apps (v9.28)](#621-x-programs-as-normal-desktop-apps-v928)
- [Part 7: File systems](#part-7-file-systems)
  - [7.1 The in-memory file table (`fs/vfs/ramfs.c`)](#71-the-in-memory-file-table-fsvfsramfsc)
  - [7.2 The directory tree (`fs/vfs/dirtree.c`)](#72-the-directory-tree-fsvfsdirtreec)
  - [7.3 Unix semantics (`fs/vfs/unixfs.c`)](#73-unix-semantics-fsvfsunixfsc)
  - [7.4 The VFS layer (`fs/vfs/vfs.c`)](#74-the-vfs-layer-fsvfsvfsc)
  - [7.5 Locating the data area (`fs/nvx/nvxfs.c`)](#75-locating-the-data-area-fsnvxnvxfsc)
  - [7.6 The NVX4 on-disk format (`fs/nvx/nvx4.c`)](#76-the-nvx4-on-disk-format-fsnvxnvx4c)
  - [7.7 Old formats and conversion](#77-old-formats-and-conversion)
  - [7.8 NTFS and FAT root file systems (`fs/ntfs/ntfsk.c`)](#78-ntfs-and-fat-root-file-systems-fsntfsntfskc)
  - [7.9 FAT drivers](#79-fat-drivers)
  - [7.10 Packages (`fs/pkg/pkg.c`)](#710-packages-fspkgpkgc)
- [Part 8: Storage drivers, PCI and the installer](#part-8-storage-drivers-pci-and-the-installer)
  - [8.1 PCI (`drivers/bus/pci.c`)](#81-pci-driversbuspcic)
  - [8.2 ATA PIO (`drivers/storage/ata.c`)](#82-ata-pio-driversstorageatac)
  - [8.3 AHCI (`drivers/storage/gdisk.c`)](#83-ahci-driversstoragegdiskc)
  - [8.4 NVMe (`drivers/storage/nvme.c`)](#84-nvme-driversstoragenvmec)
  - [8.5 Generic disk layer (`drivers/storage/nvme.c`, after the NVMe driver)](#85-generic-disk-layer-driversstoragenvmec-after-the-nvme-driver)
  - [8.6 VirtIO-blk module (`drivers/virtio/virtio_blk.c`)](#86-virtio-blk-module-driversvirtiovirtio_blkc)
  - [8.7 The installer (`core/init/installer.c`)](#87-the-installer-coreinitinstallerc)
  - [8.8 Disk Manager (`dm_*`, `core/init/installer.c` lines 1-104)](#88-disk-manager-dm_-coreinitinstallerc-lines-1-104)
  - [8.9 USB mass storage (`drivers/usb/xhci.c`)](#89-usb-mass-storage-driversusbxhcic)
  - [8.10 Shell commands (`shell/commands.c`)](#810-shell-commands-shellcommandsc)
  - [8.11 Partition scanner (`fs/vfs/parts.c`)](#811-partition-scanner-fsvfspartsc)
- [Part 9: Networking and Wi-Fi](#part-9-networking-and-wi-fi)
  - [9.1 Where the code lives](#91-where-the-code-lives)
  - [9.2 The driver module API (`kapi.h`, `drivers.h`)](#92-the-driver-module-api-kapih-driversh)
  - [9.3 NIC bring-up and probe order](#93-nic-bring-up-and-probe-order)
  - [9.4 Wired NIC drivers](#94-wired-nic-drivers)
  - [9.5 The netdev indirection (Ethernet vs Wi-Fi)](#95-the-netdev-indirection-ethernet-vs-wi-fi)
  - [9.6 TCP/IP stack (`net/stack/tcpip.c`)](#96-tcpip-stack-netstacktcpipc)
  - [9.7 Sockets (`net/stack/sockets.c`)](#97-sockets-netstacksocketsc)
  - [9.8 HTTP, entropy and HTTPS (`net/http/http.c`)](#98-http-entropy-and-https-nethttphttpc)
  - [9.9 TLS 1.3 client and crypto library](#99-tls-13-client-and-crypto-library)
  - [9.10 Wi-Fi: Realtek RTL8821CE](#910-wi-fi-realtek-rtl8821ce)
  - [9.11 Network shell commands](#911-network-shell-commands)
- [Part 10: Input, USB, ACPI, sound and graphics drivers](#part-10-input-usb-acpi-sound-and-graphics-drivers)
  - [10.1 Where the drivers start and how they are polled](#101-where-the-drivers-start-and-how-they-are-polled)
  - [10.2 Shared input state and the key queue](#102-shared-input-state-and-the-key-queue)
  - [10.3 PS/2 keyboard and mouse (`drivers/input/ps2/ps2.c`, `ps2_mouse.c`)](#103-ps2-keyboard-and-mouse-driversinputps2ps2c-ps2_mousec)
  - [10.4 Elan PS/2 touchpad (`drivers/input/ps2/elan_ps2.c`)](#104-elan-ps2-touchpad-driversinputps2elan_ps2c)
  - [10.5 I2C HID touchpads](#105-i2c-hid-touchpads)
  - [10.6 xHCI USB (`drivers/usb/xhci.c`)](#106-xhci-usb-driversusbxhcic)
  - [10.7 ACPI and power](#107-acpi-and-power)
  - [10.8 RTC (`drivers/rtc/cmos.c`)](#108-rtc-driversrtccmosc)
  - [10.9 Sound (`core/sound.c`, `drivers/sound/*`)](#109-sound-coresoundc-driverssound)
  - [10.10 Video](#1010-video)
  - [10.11 CPU information (`arch/x86_64/cpu/cpuinfo.c`)](#1011-cpu-information-archx86_64cpucpuinfoc)
  - [10.12 Summary of bounded waits](#1012-summary-of-bounded-waits)
- [Part 11: Desktop, applications, shell and scripting](#part-11-desktop-applications-shell-and-scripting)
  - [11.1 UI states and the main loop](#111-ui-states-and-the-main-loop)
  - [11.2 Window manager (`graphics/ui/wm.c`)](#112-window-manager-graphicsuiwmc)
  - [11.3 Desktop shell and compositor (`graphics/ui/desktop.c`)](#113-desktop-shell-and-compositor-graphicsuidesktopc)
  - [11.4 Input routing (`graphics/ui/input.c`, `process_input`)](#114-input-routing-graphicsuiinputc-process_input)
  - [11.5 Live terminal mode vs. desktop terminal](#115-live-terminal-mode-vs-desktop-terminal)
  - [11.6 Setup (OOBE) and login (`core/init/setup.c`)](#116-setup-oobe-and-login-coreinitsetupc)
  - [11.7 Applications (`apps/`)](#117-applications-apps)
  - [11.8 The shell (`nsh`)](#118-the-shell-nsh)
  - [11.9 Scripting languages](#119-scripting-languages)
  - [11.10 Kernel modules: `usr/mod/hello_mod.c` and the `.MOD` format](#1110-kernel-modules-usrmodhello_modc-and-the-mod-format)
  - [11.11 Fonts and assets (`assets/`)](#1111-fonts-and-assets-assets)
  - [11.12 Escape sequences, termios and full-screen programs](#1112-escape-sequences-termios-and-full-screen-programs)
  - [Task Manager and Settings (v9.73)](#task-manager-and-settings-v973)
- [Part 12: Testing, quality gates, debugging and history](#part-12-testing-quality-gates-debugging-and-history)
  - [12.1 Overview of the test approach](#121-overview-of-the-test-approach)
  - [12.2 `tools/check.py`, the automatic bug checker](#122-toolscheckpy-the-automatic-bug-checker)
  - [12.2a Typical workflows](#122a-typical-workflows)
  - [12.3 `tools/emulate.py`, boot-path emulation](#123-toolsemulatepy-boot-path-emulation)
  - [12.4 Host harnesses in `tools/tests`](#124-host-harnesses-in-toolstests)
  - [12.5 The serial debug log (`core/klog.c`)](#125-the-serial-debug-log-coreklogc)
  - [12.6 Panic and the Red Screen of Death (`core/panic.c`)](#126-panic-and-the-red-screen-of-death-corepanicc)
  - [12.7 Diagnostic shell commands](#127-diagnostic-shell-commands)
  - [12.8 Running NoovexOS in a VM](#128-running-noovexos-in-a-vm)
  - [12.9 Troubleshooting guide](#129-troubleshooting-guide)
  - [12.10 Glossary of internal names](#1210-glossary-of-internal-names)
  - [12.11 Version history](#1211-version-history)
- [Part 13: The web engine, JavaScript and cookies](#part-13-the-web-engine-javascript-and-cookies)
  - [13.1 Build and link](#131-build-and-link)
  - [13.2 The kernel interface (`nwe_api.h`)](#132-the-kernel-interface-nwe_apih)
  - [13.3 Memory](#133-memory)
  - [13.4 HTML parsing and the DOM (`web/dom.c`)](#134-html-parsing-and-the-dom-webdomc)
  - [13.5 CSS (`web/cssparse.c`, `web/cssstyle.c`)](#135-css-webcssparsec-webcssstylec)
  - [13.6 Layout (`web/layout.c`, `web/layout2.c`)](#136-layout-weblayoutc-weblayout2c)
  - [13.7 Painting, fonts and images](#137-painting-fonts-and-images)
  - [13.8 JavaScript (`web/js.c`, `web/prelude.js`)](#138-javascript-webjsc-webpreludejs)
  - [13.9 Networking and the HTTP client (`apps/internet/webglue.c`)](#139-networking-and-the-http-client-appsinternetwebgluec)
  - [13.10 Cookies and web storage (`net/http/cookies.c`)](#1310-cookies-and-web-storage-nethttpcookiesc)
  - [13.11 The Browser window (`apps/internet/browser.c`)](#1311-the-browser-window-appsinternetbrowserc)
  - [13.12 Memory layout change in v9.08](#1312-memory-layout-change-in-v908)
  - [13.13 The `qjs` command](#1313-the-qjs-command)
  - [13.14 Testing the engine](#1314-testing-the-engine)
  - [13.15 Known gaps](#1315-known-gaps)
  - [13.16 Extending the engine](#1316-extending-the-engine)
  - [13.17 Video and audio in the browser](#1317-video-and-audio-in-the-browser)
  - [13.18 Transforms, animations and transitions](#1318-transforms-animations-and-transitions)
  - [13.19 Web fonts](#1319-web-fonts)
  - [13.20 Shadow DOM](#1320-shadow-dom)
  - [13.21 Style system speed](#1321-style-system-speed)
  - [13.22 Crash containment](#1322-crash-containment)
  - [13.23 YouTube player page](#1323-youtube-player-page)
  - [13.24 Limits raised for large web apps](#1324-limits-raised-for-large-web-apps)
  - [13.25 Staying responsive while a page works](#1325-staying-responsive-while-a-page-works)
  - [13.26 Layout on demand for scripts](#1326-layout-on-demand-for-scripts)
  - [13.27 Bugs found against a modern landing page](#1327-bugs-found-against-a-modern-landing-page)
  - [13.28 New tab page and probe fixes](#1328-new-tab-page-and-probe-fixes)
  - [13.29 Faster loading and form controls](#1329-faster-loading-and-form-controls)
  - [13.30 Load time limits](#1330-load-time-limits)

## Part 1: Overview, source layout and build system

All paths below are relative to the root of the repository, which is also the directory `build.sh` runs from.

### 1. What NoovexOS 64 is

NoovexOS 64 is a from-scratch x86-64 operating system: a freestanding C kernel plus a few assembly stubs, its own BIOS and UEFI bootloaders, its own C library for user programs, and a set of ported third-party programs (Lua, Doom, ntfs-3g, musl, SQLite, gzip, bzip2, xz, zstd). The kernel reports itself as `"NoovexOS 64 v8.95 (from-scratch, Unix-like)"` (`fs/vfs/vfs.c`, the `version` leaf).

#### 1.1 Design principles, as they appear in the code

| Principle | Where it shows up |
|---|---|
| Separately compiled modules. Each of the 105 kernel `.c` files is its own translation unit with a generated interface header under `include/nvx/`; a unit includes only the headers it uses. | `units.list`, `include/nvx/`, `tools/kbuild.py`; `build.sh` step `[3/6]` compiles changed units in parallel into `build/units/`. |
| Flat image at 1 MiB. The linked ELF is turned into a raw binary and loaded at physical `0x100000`. `_start` sits at offset 0 because `.text.entry` is placed first. | `arch/x86_64/kernel64.ld`, `arch/x86_64/boot/boot64.S` |
| Identity-mapped kernel. `boot64.S` builds page tables that identity-map 0-4 GiB with 2 MiB pages (`pml4`, `pdpt`, `pd0`..`pd3`) and calls `kmain()`. The kernel runs at its physical addresses and uses fixed physical regions directly (for example `PF_BASE 0x0C000000` / `PF_TOP 0x0E000000` in `mem/pmm_vmm.c`). | `boot64.S` ("fill PD0..PD3: 2048 entries * 2 MiB = identity map 0 .. 4 GiB"), `mem/pmm_vmm.c` ("Identity-mapped, so the kernel can build page tables in these frames directly") |
| Ring-3 user programs. User ELF programs are linked at `0x40000000000` and run in ring 3 in their own address space; the kernel enters them via `enter_ring3`/`iretq` and takes system calls on an `INT 0x80` gate and (for Linux-ABI programs) the `syscall` instruction. | `usr/src/user.ld`, `arch/x86_64/user/ring3.c`, `arch/x86_64/boot/userspace.S`, `core/proc/elf.c`, `sys/linux/linux.c` |
| SMP under a big kernel lock. Up to `NCPU 16` cores, per-CPU `struct pcpu`, and a ticket lock `struct bkl { next, serving, owner, depth }` that is recursive for its owner. | `arch/x86_64/cpu/percpu.c` (`g_bkl`, `bkl_lock`), `arch/x86_64/smp/smp.c`, `arch/x86_64/smp/smp_sched.c`; the shell prints "big kernel lock, preemptive user mode" (`shell/commands.c`). |
| No SSE in kernel code generation. The kernel is compiled `-mno-mmx -mno-sse -mno-sse2`; explicit SSE is used only through inline asm in `arch/x86_64/cpu/simd.c` and its header ("SSE on at boot; kernel is -mno-sse so xmm is untouched by codegen"). Lua and Doom are compiled separately *with* SSE. | `build.sh` `CFLAGS`, `LUAF`, `third_party/doom/build_doom.sh` |
| Everything embedded. Firmware, installer boot files, the UEFI loader and every user program are linked into the kernel image as binary blobs; there is no initrd. | `build.sh` steps `[3a2]`, `[3a3]`, `[efi]`, `[u]`, `[u2]`, `[musl]`, `[ports]` |
| Self-verifying image. A `NVX64SIG` block in section `.sig` is patched after linking with a timestamp, size, UEFI entry address and two SHA-256 hashes; the kernel re-checks it at boot. | `core/buildsig.c`, `build.sh` step `[6/6]`, `sig_self_check()` in `core/init/main.c` |

### 2. Repository layout

Since v9.83 the source tree is flat, with one top-level folder per part of the system (like many hobby and research kernels). The repository root holds `README.md`, `LICENSE`, `ARCHITECTURE.md`, `.gitignore` (ignores `build/`, `boot/build/*.o`, `boot/stage/kernel64.bin`, `__pycache__/`), `build.sh`, `units.list` and `kernel64.c`, and these folders:

| Folder | What is in it | Was (v9.82 and older) |
|---|---|---|
| `arch/x86_64/` | `boot/` (assembly entry, ISR stubs, user entry, AP trampoline), `cpu/` (`cpu.h`, `io.h`, CPU start-up, CPUID, per-CPU data, SSE), `interrupts/`, `smp/`, `time/` (HPET/TSC/LAPIC timers), `usr/` (ring 3, stack calls, setjmp for games), the linker scripts | `source/kernel/arch/x86_64/` (no subfolders) |
| `boot/` | myboot BIOS loader, UEFI loader (`boot/uefi/`), hybrid ISO tools; `boot/myboot/` is the stand-alone dual-firmware variant | `source/bootloader/`, `source/myboot-dualfirmware/` |
| `core/` | `core/init/` (`kmain`, boot menu, splash, setup, installer), `proc/` (ELF, processes, scheduler), log, panic, power, sound, media, build signature | `source/kernel/core/`, `source/kernel/init/` |
| `sys/` | `native/` (INT 0x80 system calls, `kapi.c`), `linux/` (Linux system-call layer, Linux sockets) | `source/kernel/core/syscall.c`, `core/kapi.c`, `core/linux.c`, `net/lxsock.c` |
| `mem/` | page tables and frames, heap, slab | `source/kernel/mm/` |
| `fs/` | `vfs/`, `nvx/` (NVX4), `fat/`, `ntfs/`, `pkg/` (packages and apt) | `source/kernel/fs/` (no subfolders) |
| `drivers/` | `acpi/`, `bus/`, `i2c/`, `input/` (`ps2/`, `touchpad/`, `vm/`), `net/` (`ethernet/`, `wifi/`), `rtc/`, `sound/`, `storage/`, `usb/`, `video/` (`fb/`, `intel/`, `nvidia/`), `virtio/` | `source/kernel/drivers/` (fewer subfolders), Wi-Fi in `source/kernel/drivers/wifi/` |
| `graphics/` | `ui/` (window manager, desktop and start menu, input routing, hot keys), `theme/` (themes, effects, wallpapers) | `source/kernel/gui/` |
| `net/` | `stack/` (TCP/IP, sockets), `http/` (HTTP client, cookies), `print/` (IPP), `wifi/` (WPA2, Wi-Fi netdev) | `source/kernel/net/` (no subfolders) |
| `shell/` | terminal, VT, built-in commands | `source/kernel/shell/` |
| `apps/` | `system/`, `office/`, `internet/`, `media/`, `games/`, `accessories/`, `x11/`, `graphics/` (Gears and `r3d/`) | `source/kernel/apps/` (no subfolders) |
| `lib/` | `base/`, `cxx/`, `compress/`, `image/`, `js/`, `crypto/` | `source/kernel/lib/` |
| `web/` | the web engine | `source/kernel/web/` |
| `usr/` | user space: `src/`, `libc/`, `musl/`, `glibc/`, `x11/`, `bin/`, `usr/packages/`, `usr/mod/` | `source/kernel/user/`, `source/kernel/packages/`, `source/kernel/mod/` |
| `lang/rune/` | Rune code built into the kernel | `source/kernel/rune/` |
| `assets/` | fonts, wallpapers, game data, installer images, `nvidia/` and `assets/firmware/` | `source/kernel/assets/`, `source/kernel/firmware/` |
| `include/` | interface headers; `include/nvx/` mirrors the folders above (the header for `drivers/usb/xhci.c` is `include/nvx/drivers/usb/xhci.h`) | `source/kernel/include/` |
| `third_party/` | other projects' code; `third_party/src/` holds the glibc, CPython and Tcl/Tk archives | `source/kernel/third_party/`, `source/third_party_src/` |
| `tools/` | build helpers (`kbuild.py`, `nvz.py`, `mcc`, `rune`, ...), `check.py`, `emulate.py`, `tests/`, `wifi-tables/` | `source/kernel/tools/`, `source/tools/`, `wifi-tables/` |
| `docs/` | `architecture-src/`, the parts this document is assembled from | same |

Every top-level folder has a short `README.md` saying what is in it. The build runs from the root: `bash build.sh`, output in `build/`.

#### 2.1 `boot/`

```
boot/
+-- Makefile               builds build/boot.bin, build/stage2.bin and an example disk image
+-- linker.ld              stage2 link script: ENTRY(stage2_start), . = 0x8000
+-- mkhybrid.py            builds the hybrid CD/USB ISO (MBR + stage2 + ISO9660 + FAT32 + FAT16 EFI image)
+-- mkimage.py             builds a plain MBR disk image with one FAT32 partition (used by `make`)
+-- README.md              myboot documentation
+-- example/               boot.cfg, kernel.c, kernel.ld, kernel_entry.S: a sample kernel for `make`
+-- stage/                 files packed into the ISO: kernel64.bin, boot.cfg, README.txt
+-- src/                   stage1.S (512-byte MBR: INT 13h LBA read of stage2 to 0x8000); stage2_entry.S (real-mode
|                          entry `stage2_start`, mode switches, raw_boot, enter_long_mode); bios.S (real-mode BIOS call
|                          wrappers); stage2.c (config, VBE mode, menu, kernel load, hand-off); disk.c (bounce buffer
|                          DISK_BOUNCE 0x20000); fat.c (read-only FAT, fat_read_file); config.c (boot.cfg, 16 entries);
|                          elf.c/elf64.c (ELF32/ELF64 loaders); multiboot.c (Multiboot 1 info, E820); vbe.c, fb.c, vga.c,
|                          term.c (video, text console, boot terminal); kbd.c, pit.c, rtc.c, serial.c, string.c, io.h, types.h
+-- uefi/
    +-- uefiboot.c         UEFI loader: reads \KERNEL64.BIN or \boot\kernel64.bin, sets a GOP mode, jumps to uefi_entry
    +-- dummyreloc.S       one empty .reloc block so the PE has a relocation section
    +-- efi.ld             PE-oriented link script (ENTRY(efi_main), .text at 0x1000, .reloc)
    +-- BOOTX64.EFI        the built EFI application (checked in; rebuilt by build.sh)
    +-- test/              pe_run.c, firmware_sim.c, pecheck.py, fatck.py, isocheck.py, README.md
```

#### 2.2 Kernel source folders

The kernel is every C, C++ and assembly unit listed in `units.list` (146 units, in link order, see section 3) plus the separately built modules in `build.sh` (`MODS`). The table in section 2 lists the folders; the files in them are described in the Part for each area. Two folders hold code that is not kernel units: `usr/` (programs and C libraries built for user space) and `tools/` (programs that run on the build machine). `kernel64.c` includes every unit in one file and is used only by host test harnesses and the analyzer.

The old `tools/mkboot.py` and `tools/mkiso.py` scripts assemble `boot/mbr.S`, `boot/stage2.S` and `boot/cdboot.S` relative to the current directory; those files live in `arch/x86_64/boot/`, so the scripts do not run unchanged. `tools/splice.py` hard-codes `/mnt/user-data/...` paths.

#### 2.3 `tools/`

| File | Purpose |
|---|---|
| `tools/check.py` | Automatic checker. `--build` runs `build.sh`, copies `build/kernel64.bin` to `boot/stage/`, runs `make build/boot.bin build/stage2.bin`, then `mkhybrid.py` into `tools/out/check.iso`. Groups: `build`, `static`, `analyze`, `boot`, `usb`, `acpi`, `gpu`, `libc`, `coreutils` (plus Linux-ABI tests). |
| `tools/emulate.py` | Boot emulation of an ISO: the BIOS path (builds the real `fat.c`/`config.c`/`string.c` as a 32-bit host program), the UEFI path (El Torito EFI entry, PE check, runs `BOOTX64.EFI` against simulated services) and `uefi_entry`. |
| `tools/tests/` | Host harnesses and templates: `aml/` (ASL/AML samples), `drivers/drvsim.c`, `fs/`, `libc/` (`ltest.c`, `pfuzz.c`, `sfile.c`), `linux/` (`hh.c`, `lxfuzz.c`, host `cpu.h`/`io.h` stand-ins), `*_template.txt` for USB/ACPI/NVMe/HR/TP/MM tests, `fatck.py`, `isocheck.py`, `pecheck.py`, `bios_emu.c`, `entry_t.c`, `mark_t.c`, `gpu_t.c`. |

### 3. Modules and interface headers

Since v8.96 the kernel is built as separate compilation units (105 since v8.97, which added `shell/vt.c`). Before that, `kernel64.c` included every `.c` file into one translation unit, so every function and variable was `static` and visible to everything after it. The split keeps the same code but gives each file a real interface.

#### 3.1 The pieces

| Piece | What it is |
|---|---|
| A unit | One kernel `.c` file, listed in `units.list`. It starts with its own includes and is compiled on its own. |
| Its interface header | `include/nvx/<path of the unit>.h`, for example `net/stack/tcpip.c` has `include/nvx/net/stack/tcpip.h`. It holds what other units may use: the unit's shared types, constants and macros, `extern` declarations of its shared variables, prototypes of its shared functions, and the small `static inline` helpers others call. |
| `include/nvx/base.h` | Included first everywhere: the `u8`..`i64` types, `include/kapi.h`, `include/drivers.h`, `arch/x86_64/cpu/cpu.h`, `arch/x86_64/cpu/io.h`, `lang/rune/rune_gfx.h`, `third_party/ntfs/nvxntfs.h`, and forward declarations (`struct x;`) of every shared struct so pointers to them need no other header. |
| `include/nvx/kernel.h` | `base.h` plus every interface header, in dependency order. Handy for experiments; kernel units do not use it. |
| `kernel64.c` | Includes `kernel.h` and every unit. It is no longer part of the kernel build; the host harness (`tools/tests/linux/hk_glue.c`) and the strict-warnings check compile it as one file. |

What stays private: anything a unit does not share remains `static` in the `.c` file. About 2,400 of the 4,300 file-scope names in the kernel are private; the rest are shared through headers.

#### 3.2 Rules for headers and units

- A unit includes `include/nvx/base.h`, its own interface header and the interface headers of every unit whose names it uses. On average a unit includes 10 module headers; `shell/commands.c` includes the most (70), small library units such as `lib/base/mem.c` only their own.
- An interface header includes `base.h` and the headers it depends on, so each header compiles on its own. The dependency graph has no cycles.
- Shared variables are defined once, in their unit, and declared `extern` in the header. A variable that two old files both declared with a tentative `static` definition (for example `W` and `H` in `mem/pmm_vmm.c` and `drivers/video/fb/framebuffer.c`) now has one definition, in the unit that used to complete it.
- Arrays whose size came from their initializer get an explicit size in the `extern` declaration (for example `extern const struct nvx_FTREE_t FTREE[20];`), so `sizeof` works in other units.
- Anonymous structs used for shared variables got a tag (`struct nvx_g_acpi_t`) so the definition and the `extern` declaration have the same type.
- Files that were code or data but named `.h` became units: `include/percpu.h` is now `arch/x86_64/cpu/percpu.c`, `arch/x86_64/simd.h` is `simd.c`, `lib/js.h` is `lib/js/js.c`, the crypto files are `lib/crypto/*.c`, and the fonts are `assets/font.c` and `assets/fontaa.c`. Headers that only declare things (`include/kapi.h`, `include/drivers.h`, `arch/x86_64/cpu/cpu.h`, `arch/x86_64/cpu/io.h`) are unchanged.

#### 3.3 Groups of units

| Directory | Units | Role |
|---|---|---|
| `arch/x86_64/` | 10 | per-CPU data and the BKL (`percpu.c`), SSE helpers, ring 3, interrupts, timers, SMP, CPU init |
| `mem/` | 4 | heap, frames and page tables, slab, malloc aliases |
| `lib/` | 10 | memory and string functions, formatting, JavaScript, crypto, TLS, X.509, root store |
| `core/` | 11 | processes and jobs, ELF, scheduler, system calls (native and Linux), log, panic, power, sound, build signature, driver API |
| `drivers/` | 29 | PCI, storage, network cards, Wi-Fi, USB, input, touchpads, ACPI, GPU, RTC, framebuffer |
| `net/` | 6 | TCP/IP, native and Linux sockets, HTTP/TLS, WPA2, Wi-Fi netdev |
| `fs/` | 10 | RAM FS, VFS, Unix layer, NVX4, directory tree, FAT, NTFS glue, packages |
| `gui/`, `shell/`, `apps/`, `core/init/`, `assets/` | 25 | desktop and window manager, terminal, escape-sequence interpreter and commands, applications, boot UI, installer, fonts |

#### 3.4 What this changes for you

- Build speed: a full kernel compile takes about 12 s on 2 cores instead of 20 s, and an incremental one recompiles only what changed. Editing `sys/linux/lxsock.c` recompiles 1 unit; editing its header recompiles 4; editing `include/nvx/arch/x86_64/cpu/percpu.h` recompiles 20.
- Adding a function another unit should call: write it in your unit without `static`, add its prototype to your unit's header, and include that header in the caller.
- Adding a new unit: create the `.c` file, give it `#include "include/nvx/base.h"` plus the headers it needs, create its header under `include/nvx/`, and add the file to `units.list` (and to `kernel64.c` if the host harness should see it). The order in `units.list` is the link order.
- Missing declarations are compile errors (`-Werror=implicit-function-declaration`), so a missing include shows up at once.
- The kernel image grew by about 24 KB in v8.96, because the compiler can no longer inline across files. Since v8.97 the embedded programs are compressed (section 4.4). v9.08 added the web engine (`build/web_all.o`, about 2.4 MB, Part 13), and the image is now about 6.7 MB, which leaves about 1.3 MB under the 8 MiB limit.

The separately compiled `MODS` drivers (section 4.3) still see only `include/kapi.h` and `include/drivers.h`. They call the non-static `kapi_*` functions defined in `sys/native/kapi.c` and export probe functions (`e1000_probe`, `virtio_net_probe`, `hda_probe`, ...) that fill in `struct nvx_nic { name; mac[6]; send; recv; link; }` or `struct nvx_snd { name; rate; start; free_bytes; write; poll; stop; volume; }`, plus `virtio_blk_read`/`_write`/`_flush`. Lua, Doom and NTFS are also compiled on their own, each with its own flags (section 4.6).

### 4. `build.sh`, step by step

`build.sh` is a POSIX `sh` script with `set -e`. It runs from the repository root and writes everything under `B=build`. It first creates `build/`, `build/user/src`, `build/user/libc`, `build/lua/src` and `build/ntfs`.

#### 4.1 Kernel compiler flags

```sh
CFLAGS="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
        -mcmodel=small -DNOOVEX64 -Wall -Wextra -I. -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -Werror=return-type -Werror=shift-negative-value -Werror=empty-body -Werror=duplicated-branches -Werror=logical-op -Wduplicated-branches -Wlogical-op"
```

| Flag | Reason in this kernel |
|---|---|
| `-ffreestanding`, `-fno-stack-protector` | No hosted libc or stack-guard runtime; `lib/base/mem.c` and `lib/base/klibc.c` provide the memory and string functions. |
| `-fno-pie`, `-mcmodel=small` | Absolute addresses; the image is linked at `0x100000` and everything sits in the low 2 GiB. |
| `-mno-red-zone` | Interrupts and exceptions run on the current kernel stack, so code below `%rsp` must not be live. |
| `-mno-mmx -mno-sse -mno-sse2` | The compiler must not use vector registers in kernel code (`arch/x86_64/cpu/simd.c`). |
| `-fno-asynchronous-unwind-tables` | No `.eh_frame`; the linker script also discards `.eh_frame*`. |
| `-I.` | All includes are relative to the repository root (for example `#include "include/nvx/net/stack/tcpip.h"`). |
| `-Werror=...` | Implicit declarations, int/pointer mix-ups, missing returns, empty bodies, duplicated branches and suspicious logical ops all fail the build. `-Wduplicated-branches` and `-Wlogical-op` are GCC-specific. |

#### 4.2 Stages

| Stage label | What it does | Output |
|---|---|---|
| `[1/6]`, `[2/6]` | `gcc $CFLAGS -c` on `arch/x86_64/boot/boot64.S`, `isr64.S`, `userspace.S`, `ap_trampoline.S` | `build/boot64.o`, `isr64.o`, `userspace.o`, `ap_trampoline.o` |
| `[3/6]` | `python3 tools/kbuild.py build/units`: compiles every unit in `units.list` whose `.o` is older than the unit or any header it uses (from `gcc -MMD` dependency files), in parallel. A change of `CFLAGS` or `KBUILD_ARGS=--all` recompiles everything. Prints `N of 105 units compiled` | `build/units/*.o`, `build/units/objects.txt` |
| `[3m]` | Compiles each file in `MODS` separately with the same `CFLAGS`; on failure prints `MODULE BUILD FAILED` | `build/mod_<name>.o` to `$MODOBJS` |
| `[3a]` | `tools/rune build lang/rune/rune_gfx.rn --freestanding --target=x86_64-none` | `build/rune_gfx.o` |
| `[3a2]` | `ld -r -b binary` on `assets/firmware/rtw8821c_fw.bin` | `build/rtw_fw.o` |
| `[3a3]` | `ld -r -b binary` on `assets/installer/inst_boot.bin`, `inst_stage2.bin`, `inst_boot.cfg` | `build/inst_boot.o`, `inst_stage2.o`, `inst_cfg.o` |
| `[efi]` | Rebuilds `boot/uefi/BOOTX64.EFI` (falls back to the checked-in file on failure), copies it to `build/inst_efi.bin` and wraps it | `build/inst_efi.o` |
| `[u]` | Freestanding demo programs with `user_crt0.S` | `build/user/user_*.elf`, `build/user_*_elf.o` |
| `[lua]` | Lua sources plus `nvxlibc.c`, `nvxlua.c`, merged with `ld -r` | `build/lua_all.o` |
| `[u2]` | nvxlibc `libc.a` and programs linked against it | `build/user/libc/libc.a`, `build/user/<p>.elf`, `build/user_<p>_bin.o` |
| `[musl]` | musl 1.2.6 static (built once), then the static musl test programs | `build/musl/`, `build/user/{mhello,musltest,smptest,nettest,featest}.elf`, `build/user_*_bin.o` |
| `[musl-shared]` | musl 1.2.6 shared (built once), the embedded `libc.so`, the demo library `libnvxdemo.so` and the dynamic test `dyntest` | `build/musl-sh/`, `build/user_libc_so_bin.o`, `build/user_dyntest_bin.o` |
| `[ports]` | SQLite, gzip, bzip2, xz, zstd, kilo, qjs (cached) and fetch (always) as dynamic programs via `tools/mcc-dyn`; BearSSL once through `tools/build_bearssl.sh` | `build/ports-dyn/<n>`, `build/bearssl/libbearssl.a`, `build/user/<n>.elf`, `build/user_<n>_bin.o` |
| `[web]` | `tools/js2c.py` for the prelude, QuickJS (cached in `build/web/`), musl `libm` (`libmk.a`), the `web/*.c` engine files, merged with `ld -r` and reduced to one global symbol, `nwe_entry` (Part 13) | `build/web_all.o` |
| `[3b]` | `third_party/doom/build_doom.sh` | `build/doom_blob.o` |
| `[3c]` | ntfs-3g library, glue and `mkntfs` | `build/ntfs/*.o` to `$NTFSOBJ` |
| `[4/6]` | Final link with `arch/x86_64/kernel64.ld` | `build/kernel64.elf` |
| `[5/6]` | `objcopy -O binary` | `build/kernel64.bin` |
| `[6/6]` | Python post-processing: 8 MiB size check, size stamp at offset 24, build signature | `build/kernel64.bin` (patched in place) |

#### 4.3 Separately compiled drivers (`MODS`)

```sh
MODS="drivers/net/e1000.c drivers/net/rtl8139.c drivers/net/rtl8169.c drivers/virtio/virtio.c drivers/virtio/virtio_net.c drivers/virtio/virtio_blk.c drivers/sound/ac97.c drivers/sound/hda.c"
```

Each is compiled with the kernel `CFLAGS` into `build/mod_$(basename .c).o` and linked statically into the kernel at `[4/6]`. They are not listed in `units.list`, include no `include/nvx/` headers, and are not loaded at runtime. `tools/check.py` reads the `MODS=` line from `build.sh` to run `clang-tidy` on the same files.

#### 4.4 Blob embedding

Every binary payload goes through GNU ld's binary input format. Running `ld -r -b binary -o X.o path/file.ext` creates a data section with the symbols `_binary_<path_with_underscores>_start` / `_end`. The helper is `emb(){ ( cd $B && ld -r -b binary -o $1 usr/$2.elf ); }`. Because it runs inside `build/` on `usr/<name>.elf`, the symbols are named `_binary_user_<name>_elf_start` (for example `_binary_user_nvxbox_elf_start`, `_binary_user_user_demo_elf_start`). The kernel references them from `core/proc/jobs.c`, `core/proc/sched.c`, `core/proc/elf.c`, `shell/commands.c`, `core/init/installer.c`, `drivers/storage/nvme.c` and `graphics/ui/input.c`. Firmware and installer blobs are wrapped from their own directories, which gives `_binary_rtw8821c_fw_bin_start`, `_binary_inst_boot_bin_start`, `_binary_inst_stage2_bin_start`, `_binary_inst_boot_cfg_start` and `_binary_inst_efi_bin_start`.

The `[musl]`, `[musl-shared]` and `[ports]` programs and the shared C library are wrapped with `embz` instead, which first compresses the file with `tools/nvz.py` (`NVZ1` header, original size, raw DEFLATE) into `build/zz/user/<name>.elf`; the kernel inflates it the first time it is run (section 6.6). For the `[u2]`, `[musl]`, `[musl-shared]` and `[ports]` programs the wrapped object is then rewritten with `objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents`. That moves the blob into `.rodata`, which the linker script places before `__immutable_end`. The `[u]` demo blobs are not renamed and stay in `.data`.

#### 4.5 User programs

`[u]` freestanding demos. These are built with `UFLAGS="-m64 -ffreestanding -nostdlib -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -Os -Iusr/src"`. Programs: `user_demo`, `user_fork`, `user_app`, `user_thread`, `user_preempt`, `user_ipc`, `user_wmapp`, `user_compdemo`, `user_posix`, `user_mem`. Each is linked as `ld -m elf_x86_64 -nostdlib -no-pie -T usr/src/user.ld` with `user_crt0.o`. `user.ld` places `.text`, `.rodata`, `.data`, `.bss` from `0x40000000000`, which is why `-mcmodel=large` is needed. The demo program is embedded as `user_elf.o` and the others as `user_<n>_elf.o`.

`[u2]` nvxlibc programs. `LFLAGS` adds `-nostdinc -isystem usr/libc/include -isystem $(gcc -print-file-name=include) -ffunction-sections -fdata-sections -w`. `libc.a` is archived from `libc.o printf.o stdlib2.o stdio2.o sys_nvx.o`, and `crt0.S` is built separately. Programs: `count spin sigdemo upper greet pipetest nvxbox init libc8`. `nvxbox` also links `coreutils.c`. They are linked with `--gc-sections -T usr/src/user.ld` as `crt0.o prog.o [coreutils.o] libc.a`.

`[musl]` Linux-ABI programs. If `build/musl/lib/libc.a` or `rcrt1.o` is missing, `third_party/musl/configure --prefix=$M --disable-shared --disable-wrapper CFLAGS="-fPIE -O2"` runs in `build/musl-obj`, followed by `make -j$(nproc)` and `make install`. Then `mhello`, `musltest`, `smptest`, `nettest` and `featest` are compiled with `-fPIE -O2 -nostdinc -isystem $M/include` linked as static PIE, then `strip`ped and embedded:

```sh
gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o
```

`[musl-shared]`. If `build/musl-sh/lib/libc.so` is missing, musl is configured a second time with `--enable-shared --disable-static --syslibdir=/lib` in `build/musl-shobj` and installed with `make install-libs`. The stripped `libc.so` is embedded compressed as `user_libc_so_bin.o`; it is the dynamic linker the kernel loads for dynamic programs (section 6.10). `usr/musl/libnvxdemo.c` is built as `libnvxdemo.so`, turned into a C array by `tools/bin2c.py`, and compiled into `dyntest`, which is linked with `-pie -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1`.

`[ports]`. `NVX_MUSL` and `NVX_MUSL_SH` are exported and `tools/mcc-dyn` is used as the compiler. It passes through to `gcc -fPIE -nostdinc -isystem $NVX_MUSL/include` for `-c/-S/-E/-M/-MM`, and otherwise links a dynamic PIE with `Scrt1.o crti.o ... -lc libgcc crtn.o` from the shared musl. (`tools/mcc` is the static-PIE equivalent and is still there.) The `port()` helper builds `build/ports-dyn/<name>` only if it does not exist yet, then strips it. Delete `build/ports-dyn/<name>` to force a rebuild. BearSSL is compiled once into `build/bearssl/libbearssl.a`; `fetch` is relinked on every build because its sources are part of the tree.

| Port | Sources / notable defines |
|---|---|
| `sqlite3` | `sqlite/shell.c sqlite3.c`, `-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0` |
| `gzip` | `zlib/test/minigzip.c` + zlib sources |
| `bzip2` | bzip2 library + `bzip2.c`, `-D_FILE_OFFSET_BITS=64` |
| `xz` | explicit list of liblzma (with the multi-threaded coder) + `xz/` sources, `-std=gnu11 -include third_party/xz/nvx_config.h` |
| `zstd` | `lib/{common,compress,decompress}/*.c` + `programs/*`, `-DZSTD_MULTITHREAD -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM ...` |
| `kilo` | `third_party/kilo/kilo.c`, `-std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE` |
| `fetch` | `usr/musl/fetch.c`, `nvxtls.c`, `build/bearssl/libbearssl.a` |

#### 4.6 Lua, Doom, the web engine and NTFS

- Lua (`[lua]`). `LUAF` is the kernel flag set *with* `-msse -msse2 -mfpmath=sse`, plus `-w -Ithird_party/lua/inc -Ithird_party/lua/src`. Every `src/*.c` plus `nvxlibc.c` and `nvxlua.c` is merged with `ld -r` into `lua_all.o`. The kernel reaches it through `extern const char* nvx_lua_run(const char*)` (declared in `mem/pmm_vmm.c`).
- Doom (`[3b]`). `build_doom.sh` compiles every `*.c` except `test_host.c`, plus `webp_stub.c`, with `-O2 -ffreestanding -mno-red-zone -nostdinc -Ishim -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400`. SSE is not disabled. The objects are combined with `ld -r`, and `objcopy --keep-global-symbol` leaves only `doomgeneric_Create`, `doomgeneric_Tick`, `DG_ScreenBuffer`, `dsh_set_output`, `dsh_set_file`, `dsh_set_exit`, `dsh_set_heap` and `webp_decode` global. `build.sh` moves `doom_blob.o` into `build/` and deletes `obj/` and `doom_blob_all.o`.
- Web engine (`[web]`). The engine files are compiled with `WEBF`: the kernel flag set with SSE allowed, `-O2`, freestanding, and musl's headers for C library declarations. QuickJS and the double and float functions of musl's `libm` are compiled the same way. After `ld -r`, `objcopy --keep-global-symbol=nwe_entry` hides everything else, and the step prints any symbol that is still undefined. Details are in section 13.1.
- NTFS (`[3c]`). `NTFS_CFLAGS` is the kernel flag set plus `-nostdinc -isystem third_party/ntfs/compat -DHAVE_CONFIG_H -Ulinux -U__linux__ -U__linux -U__gnu_linux__ -fmacro-prefix-map=third_party/= -w`. It compiles `ntfs/lib/*.c`, `nvxntfs.c` and `ntfslibc.c`, then `mkntfs/*.c` with `-Dmain=mkntfs_main` (as `mk_*.o`).

#### 4.7 Link, flat binary, size stamp and signature

```sh
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack -T arch/x86_64/kernel64.ld \
   -o $B/kernel64.elf <OBJS> $MODOBJS $NTFSOBJ <BLOBS>
```

`OBJS` is `boot64.o isr64.o userspace.o ap_trampoline.o`, followed by the 108 unit objects from `build/units/objects.txt`; `BLOBS` is `rune_gfx.o rtw_fw.o inst_boot.o inst_stage2.o inst_cfg.o inst_efi.o`, the 10 `[u]` blobs, the 9 `[u2]` blobs, the 4 musl blobs, the port blobs (including `user_qjs_bin.o`), `lua_all.o doom_blob.o web_all.o`.

`objcopy -O binary build/kernel64.elf build/kernel64.bin` emits the bytes from `0x100000` up to `__file_end`. `.bss` is `NOLOAD`, so it is not in the file.

Step `[6/6]` is an inline Python script that edits `build/kernel64.bin`:

1. `assert len(b)<=8388608` ("kernel ... > 8 MiB (myboot FAT load limit)").
2. `struct.pack_into('<I', b, 24, len(b))` writes the file size into the Multiboot header's `load_end_addr` slot. `boot64.S` explains that this lets a size-aware loader read exactly the right number of sectors. `core/init/installer.c` reads the same word (`*(u32*)(0x100000+24)`).
3. It finds `NVX64SIG` and resolves `uefi_entry` and `__immutable_end` with `nm build/kernel64.elf`.
4. It patches the signature fields: `build_epoch = time.time()`, `image_size = len(b)`, `uefi_entry`. It zeroes both hash fields, computes `sha256_full` over the whole file (with `sha256` zeroed) and stores it. Then it computes `sha256` over `[0 .. __immutable_end-0x100000)` (with the `sha256` field still zero) and stores that.
5. It prints `build signature stamped @ <off>: sha256=...`; `check.py` treats this string as the success marker of the build.

The signature block (`core/buildsig.c`):

```c
__attribute__((aligned(64), used, section(".sig")))
static volatile const struct {
    char     magic[8];       /* "NVX64SIG" */
    char     version[16];    /* e.g. "8.38" */
    unsigned int  build_epoch;    /* unix time, patched post-link */
    unsigned int  image_size;     /* flat image size, patched post-link */
    unsigned char sha256[32];     /* SHA-256 of the image (with these 32 bytes zeroed), patched */
    char     builder[16];    /* "NoovexOS build" */
    unsigned long long uefi_entry; /* physical addr of uefi_entry, patched post-link */
    unsigned char sha256_full[32];
    unsigned long long bss_end;
} g_buildsig = { ..., "8.96", ..., (unsigned long long)__bss_end };
```

| Offset in block | Field | Consumer |
|---|---|---|
| 0 | `magic` | `uefiboot.c` scans the file for it; `sig_self_check()` checks `magic[0]`/`magic[7]` |
| 8 | `version` | `kcheck_run()` in `core/init/installer.c` prints it |
| 32 | `sha256` (immutable part) | `sig_self_check()` (`hash_off = sig_off+8+16+4+4`), `kimg_check()` (`ho=so+32`) |
| 80 | `uefi_entry` | `uefiboot.c` (`k+i+8+16+4+4+32+16`) jumps here |
| 88 | `sha256_full` | `kimg_check()` (`fo=so+88`) |
| 120 | `bss_end` | `uefiboot.c` (`k+i+120`) protects `[0x1300000, bss_end)`; it falls back to `0x0B000000` if the value is out of range |

At boot, `sig_self_check()` (`core/init/main.c`) re-hashes `[0x100000, __immutable_end)` with the hash field zeroed and compares. It hashes only `.text`/`.rodata`/`.sig` because "writable globals in .data change the instant the kernel starts". The result goes in `g_sig_ok`; `g_sig_enforce` (0 by default) decides whether a mismatch halts on the skull screen.

#### 4.8 C++ in the kernel

Since v9.82 the kernel is written in C, C++ and assembly. A unit in `units.list` that ends in `.cpp` is compiled by `tools/kbuild.py` with `g++` and `CXXFLAGS`. `build.sh` makes `CXXFLAGS` from `CFLAGS` (the C-only `-Werror=` options removed) plus `-std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables`, so C++ kernel code has no exceptions, no RTTI and, like the C units, no SSE.

| Piece | Where | What it does |
|---|---|---|
| Runtime | `lib/cxx/cxxrt.cpp` | `operator new`/`delete` (all sizes) on `kmalloc`/`kfree`, `__cxa_pure_virtual`, `__cxa_atexit`, `__dso_handle`, `cxx_run_ctors()` |
| Global constructors | `kernel64.ld`, `kernel64_mb.ld`, `core/init/main.c` | `.init_array` sits after `.rodata` (inside the signed part of the image) between `__init_array_start` and `__init_array_end`; `kmain` calls `cxx_run_ctors()` right after `heap_init`, so constructors may allocate |
| Helpers | `include/nvx/lib/cxx.h` | placement new, `nvx::KBuf<T>` (a `kmalloc` buffer freed by its destructor), `nvx::min`/`max`/`clamp`, `nvx::EdgeKeys` |
| C headers | `include/nvx/...` | compile unchanged as C++ inside `extern "C" { }` |

SSE code in C++ (floating point) goes into a separate object like the Quake one: `apps/graphics/r3d/build_r3d.sh` builds `r3d_blob.o` freestanding with SSE, `-mstackrealign` and `-nostdinc`, keeps only the `r3d_*` symbols global, and fails the build if it needs anything from the kernel except `memset`, `memcpy` or `memmove`. Its interface (`apps/graphics/r3d/r3d.h`) uses only integers, because kernel code cannot pass floats.

### 5. Linker scripts

#### 5.1 `arch/x86_64/kernel64.ld`

```
ENTRY(_start)
SECTIONS {
  . = 0x100000;
  .text : {
    *(.text.entry)
    *(.text .text.* .text.unlikely)
  }
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(16);
  __immutable_end = .;
  .data   : { *(.data .data.*) }
  . = ALIGN(16);
  __file_end = .;
  . = 0x00C00000;
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
  . = ALIGN(8);
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.eh_frame_hdr) }
}
ASSERT(__bss_end <= 0x09800000, "kernel bss is too large: move big buffers to the heap")
```

| Symbol / section | Meaning and users |
|---|---|
| `.text.entry` | `boot64.S` puts `_start` here, so it is the first byte of the image (offset 0 = `0x100000`). |
| `.aptramp` | Not named in the script. `ap_trampoline.S` emits it as an `"ax"` section, and ld places it as an orphan right after `.text` (in the current build `.text` is at `0x100000`, `.aptramp` at `0x214f80`). It contains `ap_tramp_start`..`ap_tramp_end`, which are copied to physical `0x8000` before the SIPI. |
| `.sig` | Also an orphan section: it lands after `.rodata` and before `__immutable_end` (current build: `0x594900`, size `0x80`), so the signature is inside the hashed region. |
| `__immutable_end` | End of `.text` + `.rodata` + `.sig`. Used by the build signature step, `sig_self_check()`, `inst_snapshot_data()`, `kimg_check()`, `kcheck_run()`. |
| `__file_end` | End of `.data` = end of the flat file. Used by `inst_snapshot_data()`. |
| `__bss_start` = `0x01300000` | `.bss` starts at 19 MiB, above the loaded image, the boot stack and the fixed DMA buffers (it was `0xC00000` before v9.11). The early 32-bit code builds its page tables at the start of `.bss` before paging is on. |
| `boot_pt_end` | Defined in `boot64.S` after `pml4`, `pdpt`, `pd0`..`pd3` (4 KiB each) and `pdfb` (8 KiB) at the start of `.bss` (current build: `0xC08000`). `kmain()` zeroes `.bss` from `boot_pt_end` to `__bss_end` so the live boot page tables survive. `uefi_entry` instead zeroes all of `[__bss_start, __bss_end)` itself before building its tables. |
| `__bss_end` | Must be at most `0x09800000` (link-time `ASSERT`). Also stored in `g_buildsig.bss_end`. |

The script's header comment ("well clear of the <=1 MiB kernel") predates the current size; the image is now about 5 MB (`build/kernel64.bin` here: 5,298,992 bytes, `__immutable_end = 0x5c1380`, `__bss_end = 0x6beb4a0`).

#### 5.2 `arch/x86_64/kernel64_mb.ld`

This script uses the same addresses and symbols, but it is meant to stay an ELF so that myboot can load the program segments, with `__bss_start` exported for `boot64_mb.S` to zero `.bss`. It has no `/DISCARD/` and no `ASSERT`. Neither it nor `boot64_mb.S` is referenced by `build.sh`, `check.py` or any other script in the tree.

#### 5.3 Size limits that interact with the layout

| Limit | Value | Source |
|---|---|---|
| Flat kernel file | at most 8 MiB | `build.sh` assert; `stage2.c` `fat_read_file(e->kernel, scratch, 0x800000, &ksz)` |
| Kernel via UEFI | at most `0x1000000-0x100000` (`KERNEL_MAX`, 15 MiB; 8 MiB in v9.08 to v9.10, `0x5C0000` before) | `boot/uefi/uefiboot.c`; both entry paths set `RSP = 0x1080000` |
| Kernel end at link time | `__file_end` at most `0x1000000` | `kernel64.ld`, `kernel64_mb.ld` `ASSERT` |
| Immutable region for self-check | at most `0x800000` | `sig_self_check()` |
| Installer boot snapshot | `KSNAP_MAX 0x800000` (matches the 8 MiB file limit; was 3 MiB before v8.95) | `core/init/installer.c` |
| `.bss` end | at most `0x09800000` | `kernel64.ld` `ASSERT` |

### 6. Bootloader build and image tools

#### 6.1 `boot/Makefile`

| Target | Recipe |
|---|---|
| `build/%.o` (C) | `gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -fcf-protection=none -Wall -Wextra -Os -march=i386 -mno-sse -mno-mmx -mno-80387 -Isrc` |
| `build/stage1.o`, `stage2_entry.o`, `bios.o` | `gcc -m32 -c` |
| `build/boot.bin` | `ld -m elf_i386 -nostdlib -Ttext 0x7c00 -e _start --oformat binary` from `stage1.o` |
| `build/stage2.elf` | `ld -m elf_i386 -nostdlib --no-warn-rwx-segments -T linker.ld stage2_entry.o bios.o <all src/*.c objects>` |
| `build/stage2.bin` | `objcopy -O binary` |
| `build/kernel.elf` | the `example/` 64-bit test kernel (`-m64 ... -mno-red-zone -mcmodel=small`) |
| `all` to `build/disk.img` | `python3 mkimage.py build/boot.bin build/stage2.bin build/disk.img build/kernel.elf example/boot.cfg` |
| `run` | `qemu-system-i386 -drive format=raw,file=build/disk.img -m 128 -serial stdio` |

`linker.ld` puts `.text.boot` first at `0x8000` and keeps `.rodata`/`.data` inside `.text`. The NoovexOS pipeline only needs `make build/boot.bin build/stage2.bin` (this is what `check.py` runs). `mkhybrid.py` requires `boot.bin` to be exactly 512 bytes and `stage2.bin` at most 63 sectors.

For a flat (non-ELF) kernel, `stage2.c` loads the file to scratch `0x1000000` and builds a Multiboot info block with the framebuffer. If `/doom/DOOM1.WAD` exists and is larger than 1,000,000 bytes, stage2 loads it to `0x0E000000` as module 0. It then calls `raw_boot(src, size, mbi)`. `boot.cfg` (`boot/stage/boot.cfg`) uses `timeout=2`, `default=0`, `video=1280x1024`, `bootlog=4`, `entry=NoovexOS 64`, `kernel=/boot/kernel64.bin`, `cmdline=noovexos`.

#### 6.2 UEFI loader (built by `build.sh` step `[efi]`)

```sh
gcc -I. -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -m64 -Os -fno-jump-tables -Werror=implicit-function-declaration -c uefiboot.c -o uefiboot.o
gcc -ffreestanding -c dummyreloc.S -o dummyreloc.o
ld -nostdlib -znocombreloc -shared -Bsymbolic -T efi.ld uefiboot.o dummyreloc.o -o uefiboot.so
objcopy -j .text -j .data -j .reloc --target=pei-x86-64 --subsystem=10 uefiboot.so BOOTX64.EFI
```

`efi_main` uses `EFIAPI __attribute__((ms_abi))` and opens `\KERNEL64.BIN` or `\boot\kernel64.bin`. It finds `uefi_entry` and `bss_end` through the `NVX64SIG` block, picks a GOP mode, calls `ExitBootServices` (up to 8 tries), copies the kernel to `0x100000` and the boot info to `0x5000`/`0x6000`/`0x7000`, and calls `uefi_entry`.

#### 6.3 `mkhybrid.py`: the hybrid ISO

Usage: `mkhybrid.py boot.bin stage2.bin out.iso [VOLID] fatpath=hostfile ...` (see section 7 for the exact call `check.py` makes). If `uefi/BOOTX64.EFI` exists next to the script, it is added as `EFI/BOOT/BOOTX64.EFI`.

| 512-byte LBA | Content |
|---|---|
| 0 | MBR: myboot stage 1 + one partition entry (bootable `0x80`, type `0x0C` FAT32 LBA, start `PART_LBA = 2048`) |
| 1..63 | stage2 (ISO "system area", unused by ISO9660) |
| 2K-sectors 16..23 | ISO9660 PVD (16), El Torito boot record (17), terminator (18), boot catalog (19), path tables (20, 21), root dir (22), `README.TXT` (23) |
| 2048.. | FAT32 partition (`build_fat32`): 1 sector/cluster, 32 reserved, 2 FATs, OEM `MYBOOT  `, label = VOLID, real subdirectories |
| after FAT32 | FAT16 EFI image (`build_fat16`), present when both `BOOTX64.EFI` and `boot/kernel64.bin` are in the tree; contains `EFI/BOOT/BOOTX64.EFI` and `KERNEL64.BIN` |

- The FAT32 partition is sized to its contents + 8 MiB, with a minimum of 69632 sectors. The script asserts at least 65525 clusters ("UEFI would read it as FAT16").
- The boot catalog's default entry is `0x88`, media `0x04` (hard-disk emulation of the whole image from RBA 0), system type `0x0C`. A final section header (`0x91`, platform `0xEF`) holds one no-emulation EFI entry that points at the FAT16 image (or, without it, at the FAT32 partition).
- `build_fat16` asserts the image fits in an El Torito sector count (`<= 0xFFFF`) and that its cluster count is in the FAT16 range.

On CD, the result boots through El Torito (BIOS: HDD emulation to MBR to stage2; UEFI: the EFI entry). Written raw to USB, it boots through the MBR, and the stick shows the FAT32 partition with folders.

#### 6.4 `mkimage.py` and the other image scripts

| Script | Output |
|---|---|
| `boot/mkimage.py boot.bin stage2.bin disk.img [file ...]` | Raw MBR disk: stage1 at LBA 0, stage2 from LBA 1 (at most 64 sectors), one FAT32 partition at LBA 2048 of `PART_SECTORS = 131072` sectors (64 MiB) with CHS fields from `chs()`. Files go in the root by basename. Used by `boot/Makefile`. |
| `boot/myboot/mkefi.py efi.elf out` | Hand-rolled PE32+ (`MZ`, machine `0x8664`, image base `0x400000`) from `objcopy -O binary` of `.text/.rodata/.data`, using `efi_main` and `__image_end`. |
| `boot/myboot/mkimage.py ... --efi BOOTX64.EFI` | That variant's disk image with an EFI loader (Makefile target `build/disk.img`; `run-uefi` uses `qemu-system-x86_64 -bios OVMF.fd`). |
| `tools/mkvdi.py raw.img out.vdi [MB]` | Fixed VirtualBox VDI (signature `0xBEDA107F`, version 1.1, type 2 = fixed, default 16 MB). |
| `mkuefi.py` | Not in the repository; it exists only as a document in the attached "noovex" project. It reads `BOOTX64.EFI` and writes `noovex7-uefi.img`: a GPT disk (protective MBR, primary + backup headers) with a 16 MiB FAT16 ESP at LBA 2048 holding `/EFI/BOOT/BOOTX64.EFI`, then re-parses the FAT to verify. |

### 7. Toolchain requirements

Derived from the commands the scripts run (`tools/README.md` lists Linux with gcc, binutils, Python 3 and GNU coreutils for the checker):

| Tool | Used for |
|---|---|
| `gcc` (GCC specifically) | Kernel and modules (`-m64`), bootloader (`-m32 -march=i386`), UEFI loader, musl `configure`, user programs. `-Werror=duplicated-branches` / `-Wlogical-op` are GCC warnings; `gcc -print-file-name=include` and `-print-libgcc-file-name` are used for freestanding headers and libgcc. |
| binutils: `ld`, `objcopy`, `ar`, `nm`, `strip` | Linking, binary blob wrapping (`-b binary`), flat/PE conversion (`--target=pei-x86-64`), `libc.a`, symbol lookup in the signature step. `readelf`/`objdump` are also used by `check.py`. |
| `make` | Bootloader, musl. |
| `python3` | Signature/size step, `mkhybrid.py`, `mkimage.py`, `check.py`, `emulate.py`. |
| POSIX `sh` | `build.sh`, `build_doom.sh`, `tools/mcc`; `nproc` is optional (fallback 4). |
| x86-64 Linux host | `tools/rune` is a dynamically linked x86-64 Linux ELF executable. |
| optional | `clang-tidy` (`check.py` analyze group), `qemu-system-i386` / `qemu-system-x86_64` + OVMF (Makefile `run` targets). |

Full build sequence (what `check.py --build` does):

```sh
bash build.sh
cp build/kernel64.bin boot/stage/kernel64.bin
cd ../bootloader && make build/boot.bin build/stage2.bin
python3 mkhybrid.py build/boot.bin build/stage2.bin out.iso NOOVEXOS \
  boot/kernel64.bin=stage/kernel64.bin boot.cfg=stage/boot.cfg README.txt=stage/README.txt
```

### 8. Versioning

There is no central version macro; the version is a literal in several places. Current occurrences of `8.96`:

| File | Use |
|---|---|
| `core/buildsig.c` | `g_buildsig.version = "8.96"` (printed by `kcheck_run()`) |
| `fs/vfs/vfs.c` | `version` leaf: `"NoovexOS 64 v8.95 (from-scratch, Unix-like)\n"` |
| `fs/vfs/unixfs.c` | `/sys/kernel/osrelease` to `"8.96\n"` |
| `sys/linux/linux.c` | `uname` for Linux-ABI programs: sysname `NoovexOS`, release `8.95`, version `NoovexOS 64 v8.95`, machine `x86_64` |
| `apps/system/files_classic.c` | About text `"Version 8.96"` |
| `apps/system/file_explorer.c` | header comment and `"Version 8.96  -  from scratch"` |
| `apps/accessories/small_apps.c` | neofetch-style lines `"NoovexOS 64 v8.95 (x86-64)"`, `"v8.95"` |
| `tools/tests/linux/hh.c` | expects `"Hello from musl libc on NoovexOS 8.96 (x86_64)"` from `mhello` |

Older strings still say `8.38`: `shell/terminal.c` (`"OS: NoovexOS 64 v8.38"`), `usr/src/nvxbox.c` (`"NoovexOS %s 8.38 nvx64 x86_64\n"`) and the example comment in `core/buildsig.c`. A version bump means editing every row of the table above (and ideally these three) together; `tools/tests/linux/hh.c` will fail if `sys/linux/linux.c` and the test disagree.

## Part 2: Boot chain, kernel entry and kernel signatures

This part follows a NoovexOS 64 boot from firmware to the main loop, then covers how the kernel image is signed and checked. Every statement is taken from the source tree. Paths are relative to `source/` unless noted. Numbers from the shipped build come from `build/kernel64.elf` / `kernel64.bin` (5,110,512 bytes).

### 2.1 The boot paths at a glance

| Path | Firmware loads | Loader | Kernel file | Kernel entered at | CPU mode at entry |
|---|---|---|---|---|---|
| BIOS (production) | MBR at LBA 0 of the hybrid image | myboot `boot/src` (stage1 + stage2) | `/boot/kernel64.bin` (flat binary) | `0x100000` (`_start`) | 32-bit protected mode, `EAX=0x2BADB002`, `EBX`=Multiboot info |
| BIOS from CD | El Torito default entry, HDD emulation of the whole image | the same myboot | same | same | same |
| UEFI (production) | `\EFI\BOOT\BOOTX64.EFI` | `boot/uefi/uefiboot.c` | `\KERNEL64.BIN` or `\boot\kernel64.bin` | `uefi_entry` (address read from the NVX64SIG block) | 64-bit long mode |
| Third-party Multiboot (e.g. GRUB) | n/a | any Multiboot 1 loader | flat binary with Multiboot header | `0x100000` | 32-bit PM, `EAX=0x2BADB002` |
| Legacy custom loaders | `arch/x86_64/boot/mbr.S`, `stage2.S`, `cdboot.S` | own asm | raw sectors | `0x100000` with `EAX=0` | 32-bit PM (see 2.5: no longer layout-compatible) |

All working paths converge on one contract: a framebuffer description at physical `0x5000`, the kernel image at `0x100000`, then `kmain()` in `core/init/main.c`.

The repository has no `bootx64.c` or `mkuefi.py`. The UEFI loader is `boot/uefi/uefiboot.c`, built by `build.sh` step `[efi]`. The dual-firmware myboot variant has its own `boot/myboot/src/efi.c` and `mkefi.py`.

### 2.2 Boot media: the hybrid image (`boot/mkhybrid.py`)

`tools/check.py` (`g_build`) builds the release image like this:

```
bash build.sh                                # build/kernel64.bin, BOOTX64.EFI
cp build/kernel64.bin boot/stage/kernel64.bin
make build/boot.bin build/stage2.bin         # in boot/
python3 mkhybrid.py build/boot.bin build/stage2.bin OUT.iso NOOVEXOS \
    boot/kernel64.bin=stage/kernel64.bin boot.cfg=stage/boot.cfg README.txt=stage/README.txt
```

Layout written by `mkhybrid.py` (512-byte LBAs):

| LBA | Content |
|---|---|
| 0 | MBR: myboot `stage1` code plus one partition entry: active (`0x80`), type `0x0C` (FAT32 LBA), start `2048` |
| 1..63 | myboot stage2 (assert: `len(stage2) <= 63*512`) |
| 64.. (2 KiB sector 16..23) | ISO9660 PVD, El Torito boot record, terminator, boot catalog, path tables, root dir, `README.TXT` |
| 2048.. | FAT32 partition: `boot/kernel64.bin`, `boot.cfg`, `README.txt`, and `EFI/BOOT/BOOTX64.EFI` if `uefi/BOOTX64.EFI` exists |
| after the partition | FAT16 image with only `EFI/BOOT/BOOTX64.EFI` and `KERNEL64.BIN`, used by the CD's EFI entry |

Rules the script enforces:

- The FAT32 partition is at least 69,632 sectors and must have at least 65,525 clusters. UEFI decides the FAT type by cluster count, so a smaller "FAT32" would be read as FAT16.
- An El Torito entry can describe at most `0xFFFF` sectors, so the CD EFI entry points at the separate FAT16 image (`build_fat16`, which asserts 4085 at most clusters < 65525).
- The El Torito default entry uses boot indicator `0x88`, media `0x04` (hard-disk emulation), load RBA 0. A BIOS booting from CD therefore sees the whole image as a hard disk and runs the same MBR to stage2 to FAT32 path as a USB stick.
- The EFI section has header `0x91`, platform `0xEF`, one no-emulation entry.

### 2.3 BIOS path: myboot (`boot/src`)

#### Stage 1 (`stage1.S`, 512 bytes, linked at `0x7C00`)

Stage 1 sets DS/ES/SS to 0, puts SP at `0x7C00` and saves DL (the boot drive). It then issues one `INT 13h AH=42h` with a DAP for 64 sectors from LBA 1 to `0000:8000` and far-jumps to `0x8000` with DL preserved. If the read fails it prints `myboot: disk error` and halts.

#### Stage 2 entry (`stage2_entry.S`, linked at `0x8000` by `linker.ld`)

`stage2_start` runs in real mode and does the following:

1. Saves DL to `g_boot_drive`.
2. Enables A20 in four steps, re-checking after each one with `check_a20`, a wrap-around test at `0000:0500` vs `FFFF:0510`. The order is already-on check, `INT 15h AX=2401h`, fast-A20 port `0x92`, then the keyboard controller (`a20_kbc`). If all four fail it prints `myboot: A20 line error`.
3. Collects the E820 memory map (`do_e820`): up to 64 entries of 24 bytes at `0x1000`, with the count stored at `0x0FF0`.
4. Loads a 5-entry GDT: null, 32-bit code `0x08`, 32-bit data `0x10`, and 16-bit code/data for the real-mode thunks. It then sets `CR0.PE` and jumps to `pm_entry`.

`pm_entry` loads the data selectors, sets ESP to `0x80000`, zeroes stage2's own `__bss_start..__bss_end` and calls `stage2_main()`. Disk reads from protected mode go through `bios.S`, which drops to real mode, issues `INT 13h` into a bounce buffer at `0x20000` and returns. The DAP is kept at `0x0500`.

#### `stage2_main()` (`stage2.c`)

1. Initialises the serial port (COM1) and the VGA console, then prints the boot drive.
2. Reads the MBR and picks a partition in three passes. The first pass takes an active (`0x80`) entry of type `0x0B`/`0x0C`. The second takes any entry of those types. The third takes any entry with a non-zero LBA. If none matches it fails with `no partition found in MBR`.
3. Calls `fat_mount(part_lba)`, then reads `/boot.cfg` into an 8 KiB buffer (8191 bytes plus NUL) and parses it with `cfg_parse`. A config with no entries fails with `boot.cfg has no entries`.
4. If the config sets `video=WxH` or `video=auto`, it calls `bios_get_font()`, which copies the VGA ROM 8x16 font to `0x5000` for `fb.c`. With `video=auto` it first reads the screen's EDID through VBE function 4F15h (buffer at `0x4400`, header and checksum checked) and tries the size of the first detailed timing, the panel's native mode, if it is at most 1920x1080. Otherwise, or if that mode is not in the VBE list, it tries `vbe_set_mode(W,H)` (1920x1080 for `auto`), falling back to 1920x1080, 1600x900, 1366x768, 1280x1024, 1280x800, 1024x768, 800x600 and 640x480 (skipping the size it already tried). On success it switches the console to the framebuffer (`fb_init`, `vga_set_graphics(1)`).
5. Runs `menu_run()` and reads the chosen kernel file to a scratch buffer at `0x1000000` (16 MiB), maximum `0x800000` bytes. `fat_read_file` fails if the file is larger than that.
6. Branches on the first four bytes (see "Kernel format dispatch" below).

#### `boot.cfg` format (`config.c` / `config.h`)

The file is plain text with one `key=value` per line. Blank lines and lines starting with `#` are skipped. Keys and values are trimmed of spaces, tabs and CR/LF, and unknown keys are ignored.

| Key | Scope | Parsing | Default | Meaning |
|---|---|---|---|---|
| `timeout` | global | `str_to_int` | 0 | Menu countdown in seconds. At at most 0 there is no auto-boot and the menu waits for a key. |
| `default` | global | `str_to_int` | 0 | Index of the preselected entry. Reset to 0 if out of range after parsing. |
| `video` | global | `W` then the part after `x`/`X` | 0x0 (text mode) | VBE mode request (see step 4 above). |
| `bootlog` | global | `str_to_int` | 5 | Length in seconds of `boot_animation()`. At at most 0 the animation is skipped. |
| `entry` | starts a new entry | title, max 64 chars | - | Up to `CFG_MAX_ENTRIES` = 16 entries. |
| `kernel` | last entry | path, max 128 chars | - | FAT path of the kernel file. |
| `cmdline` | last entry | max 128 chars | - | Copied into the Multiboot `cmdline` field. |

`kernel` and `cmdline` lines that come before any `entry` are ignored. The shipped `boot/stage/boot.cfg` (identical to `assets/installer/inst_boot.cfg`, which the disk installer embeds) is:

```
timeout=2
default=0
video=auto
bootlog=4

entry=NoovexOS 64
kernel=/boot/kernel64.bin
cmdline=noovexos
```

The NoovexOS kernel never reads the Multiboot `cmdline`. `boot64.S` only takes the framebuffer and module fields, so `cmdline=noovexos` has no effect today.

For NoovexOS, `video=` is effectively mandatory. Without a successful VBE mode, stage2 does not call `mb_set_fb`, so the framebuffer fields of the Multiboot info stay zero. `boot64.S` copies them anyway, without checking flag bit 12. `kmain` then sees `FBB==0` and halts (`if(!W||!H||!FBB){ for(;;)cpu_halt(); }`).

#### The menu (`menu_run`)

The menu draws a box titled `myboot 0.5` with one row per entry. Before it accepts keys it waits for two RTC second changes while flushing the keyboard, and a key is only accepted after the keyboard has been seen idle once (`armed`).

| Key | Action |
|---|---|
| Up / Down | Moves the selection and cancels the countdown. |
| Enter | Boots the selected entry. |
| `1`-`9` | Boots that entry directly. |
| `c` | Opens the loader terminal (`term.c`, see below). |

The terminal (`term.c`) supports `help`, `ls`, `cat`, `mem` (prints the E820 map from `0x0FF0`/`0x1000`), `clear`, `reboot`, `ver`, `exit` and `boot`. The countdown decrements on each RTC second change and boots the selection when it reaches 0.

#### Kernel format dispatch and validation

| First bytes | Branch | Validation | Handoff |
|---|---|---|---|
| not `7F 'E' 'L' 'F'` | flat binary (NoovexOS) | none beyond the FAT read size limit | `raw_boot()` |
| ELF with `e_ident[EI_CLASS]==2` | ELF64 | `cpu_supports_64()` checks that CPUID exists (EFLAGS.ID toggles), that leaf 1 EDX bit 6 (PAE) is set, that leaf `0x80000000` at least `0x80000001`, and that leaf `0x80000001` EDX bit 29 (LM) is set. `elf64_load()` requires `len >= 64`, the ELF magic, class 2, `e_machine==0x3E` and a program header table inside the file. Each `PT_LOAD` is copied to its `p_paddr` with `memsz>filesz` zero-filled. | `enter_long_mode()` |
| other ELF | 32-bit Multiboot | `mb_load_kernel()`: scans the first 8 KiB on 4-byte steps for `0x1BADB002` with `magic+flags+checksum==0`. If flag bit 16 (`MB_FLAG_ADDR`) is set it uses the address fields. Otherwise it uses `elf_load()`. | `mb_jump()` |

The NoovexOS kernel is a flat binary, so it always takes the first branch. Before the jump, stage2 builds a Multiboot info block (`mb_build_info` in `multiboot.c`):

| `mb_info` field (offset) | Value set by myboot | flag bit |
|---|---|---|
| `mem_lower` (+4), `mem_upper` (+8) | 640; KiB of the E820 type-1 region containing 1 MiB, above 1 MiB | 0 (`0x01`) |
| `cmdline` (+16) | entry's `cmdline` (static 128-byte buffer) | 2 (`0x04`) |
| `mods_count` (+20), `mods_addr` (+24) | 1 module if `/doom/DOOM1.WAD` loaded to `0x0E000000` (at most 32 MiB, must be >1,000,000 bytes) | 3 (`0x08`) |
| `mmap_length` (+44), `mmap_addr` (+48) | E820 copied into 64 static `mb_mmap_entry` (size=20) | 6 (`0x40`) |
| `boot_loader_name` (+64) | `"myboot"` | 9 (`0x200`) |
| `framebuffer_addr` (+88, u64), `pitch` (+96), `width` (+100), `height` (+104), `bpp` (+108), `type` (+109)=1, RGB positions/sizes | from `vbe_set_mode` | 12 (`0x1000`) |

Before handing over, `boot_animation()` prints the kernel path, size and cmdline, followed by a fixed list of 23 `[ OK ]` lines spread over `bootlog` seconds using `pit_wait_ms`. The lines are cosmetic; they do not reflect real work.

`raw_boot(src, size, mbi)` (`stage2_entry.S`) does `rep movsb` from the scratch buffer to `0x100000`, loads `EAX=0x2BADB002` and `EBX=mbi`, executes `cli` and does `ljmp $0x08,$0x100000`. The kernel is therefore entered in 32-bit protected mode with myboot's flat GDT.

`enter_long_mode()` (the ELF64 branch, unused by NoovexOS) stores the entry and mbi at `0x76000`. It builds a PML4 at `0x70000`, a PDPT at `0x71000` and four PDs at `0x72000..0x75FFF` that identity-map 0-4 GiB with 2 MiB pages. It then sets PAE and `EFER.LME`, enables paging, loads a 64-bit GDT and jumps to the entry with `RDI=RAX=0x2BADB002` and `RSI=RBX=mbi`.

### 2.4 `boot/myboot/` (stand-alone variant)

This is myboot 0.6 with a UEFI application (`src/efi.c`, PE built by `mkefi.py`). Compared with `boot/src/stage2.c` it differs in four ways:

- Partition selection takes the first entry of type `0x0B`, `0x0C` or `0xEF`.
- There is no VBE fallback list.
- It has no flat-binary branch: a non-ELF kernel fails with `kernel is not an ELF image`.
- The UEFI side opens `\KERNEL.ELF` (max 64 MiB), requires ELF64/x86-64, `AllocatePages`-reserves the PT_LOAD span at its physical address, builds a Multiboot info block from GOP and the UEFI memory map, retries `ExitBootServices` up to 8 times, and jumps with `RDI/RAX=0x2BADB002`, `RSI/RBX=mbi`.

`arch/x86_64/boot/boot64_mb.S` is the matching kernel entry. It copies the mbi framebuffer fields to `0x5000` before zeroing `.bss`, zeroes `0x5010`/`0x5018`, zeroes `.bss`, switches to a 128 KiB stack in `.bss` and calls `kmain`. `build.sh` does not assemble it, so the shipped kernel cannot use this contract.

### 2.5 Legacy custom loaders in `arch/x86_64/boot/`

`README_CUSTOMBOOT.txt` documents an earlier v0.50 experiment:

- `mbr.S` stores DL at `0x0500` and reads 32 sectors from LBA 1 to `0x8000`. On failure it prints `MBR disk err`.
- `stage2.S` enables fast A20 and tries VBE modes `0x118`, `0x115` and `0x112`, keeping the first one with a linear framebuffer (`vinfo` attribute bit 7). It then enters unreal mode, streams `kern_secs` sectors from `kern_lba` (33) in 64-sector chunks through a buffer at `0x20000` to `0x100000`, and jumps to `0x08:0x100000` in 32-bit PM with `EAX=0` and ESP `0x9F000`.
- `cdboot.S` does the same with 2048-byte CD sectors. Its `kern_lba`/`kern_secs` are patched by `tools/mkiso.py`.

`tools/mkboot.py` assembles `boot/mbr.S` + `boot/stage2.S` (paths that no longer match the tree) into a raw disk with the kernel at LBA 33. `tools/splice.py` targets an older container and asserts a 1 MiB kernel limit, which the current 5 MB kernel exceeds.

These loaders are not compatible with the current kernel. They write the *old* `0x5000` layout:

| Address | old layout (`stage2.S`/`cdboot.S`) | current layout read by `kmain` |
|---|---|---|
| `0x5000` | u32 LFB address | u64 LFB address |
| `0x5004` | u16 pitch | (high half of LFB address) |
| `0x5006` / `0x5008` | u16 width / u16 height |, / u32 pitch |
| `0x500A` | u8 bpp | - |
| `0x500C` / `0x500E` / `0x5020` | - | u16 width / u16 height / u8 bpp |

Because they enter with `EAX=0`, `boot64.S` skips its Multiboot branch and does not rewrite the block. Only myboot (Multiboot handoff) and `uefiboot.c` produce the layout the kernel expects.

### 2.6 UEFI path (`boot/uefi/uefiboot.c`)

`build.sh` step `[efi]` compiles `uefiboot.c` with `-fpic -fshort-wchar -mno-red-zone -fno-jump-tables` and links it with `efi.ld` and `dummyreloc.S` into a shared object. `objcopy --target=pei-x86-64 --subsystem=10` then produces `BOOTX64.EFI`. The result is copied to `build/inst_efi.bin` and linked into the kernel as a blob for the disk installer. Every firmware call uses `EFIAPI` = `ms_abi`. `check.py` verifies there are no relocations.

`efi_main()` in order:

1. Disables the watchdog (`SetWatchdogTimer(0,...)`), clears the screen and prints `NoovexOS UEFI loader 2.0`.
2. Finds a file system: the Simple File System of the loaded image's device, or else the first one from `LocateProtocol`.
3. Opens `\KERNEL64.BIN` (installed disks), falling back to `\boot\kernel64.bin` (ISO/USB).
4. Gets the file size from `EFI_FILE_INFO` (offset 8). It must not exceed `KERNEL_MAX = 0x1000000-0x100000` = 0xF00000 (15,728,640 bytes; 8 MiB in v9.08 to v9.10, 0x5C0000 before). The file is read in 1 MiB chunks into `AllocatePages(AnyPages, LoaderData)`.
5. Locates the signature block. It searches the loaded bytes for `NVX64SIG` and reads `uefi_entry` at +80 and `bss_end` at +120. `bss_end` is clamped to `0x0B000000` if it is outside `[0x1300000, 0x0B000000]`. If `uefi_entry` is 0 or outside `[0x100000, 0x100000+size)`, the loader fails with "this file is not a NoovexOS 64 kernel (no UEFI entry point)". This is the only kernel validation on the UEFI path; the hashes are not checked here.
6. GOP: takes the console's GOP or else `LocateProtocol`, then scores every mode as `w*h*4 + (BGR?2:0) + (current?1:0)`. Only modes that are BGR (format 1, or bitmask with R=`FF0000`, G=`FF00`, B=`FF`) or RGB (format 0) and at most 1920x1080 count. It switches to the best mode. A Blt-only mode (format 3) is fatal.
7. Finds the ACPI RSDP in the configuration table, preferring the ACPI 2.0 GUID over 1.0.
8. Reads the memory map and computes `ram_mb`. It walks contiguous "usable after exit" descriptors (types 1, 2, 3, 4, 7) upward from 256 MiB, or from 1 MiB if nothing is usable at 256 MiB. `ram_mb` is the end of that run, in MiB.
9. Overlap checks. It warns, then waits 5 s, if any non-usable firmware range intersects a fixed kernel area: `0x5000-0x8000`, `0x100000-0x1080000`, `0x1080000-0x1300000`, `0x1300000-bss_end`, `0x10000000-0x11000000`. It also warns if the RSDP lies inside `[0xC00000, bss_end)`. It fails outright if the loader image overlaps the kernel destination or `0x5000-0x8000`, or if its own stack lies inside the kernel destination.
10. Builds the handoff blocks in local buffers (see 2.7).
11. Page tables (`build_tables`): allocates 36 pages, retrying up to 24 times until the pages fall outside every kernel area (`alloc_safe`). The PML4, one PDPT and 32 PDs identity-map 0-64 GiB with 2 MiB pages. Two more PDs map a framebuffer located between 64 and 512 GiB. If the stack or image is above 64 GiB, or allocation fails, the loader keeps the firmware page tables (`g_pt=0`).
12. Calls `ExitBootServices`, re-reading the map each try, up to 8 attempts. It then summarises the final map into `g_mmsum`.
13. Runs `cli`, loads CR3 with its own tables, and calls `quiet_interrupts()`: masks both PICs, masks the LAPIC LVT timer and zeroes its initial count. It then calls `quiet_dma()`, which walks every PCI function through ports `0xCF8`/`0xCFC`. For each xHCI controller (class `0C/03/30`) whose registers are reachable it clears `USBCMD.RS` and waits up to 2 M iterations for `USBSTS.HCH`. For every function of class storage, network, multimedia, wireless or serial bus (except SMBus) it clears Bus-Master Enable. Before v8.99 the firmware's USB controller kept writing event and transfer data into its old buffers, which on some laptops lie inside the kernel area (`0x100000-0x980000`), and so overwrote the kernel copy made in step 14. The kernel re-enables bus mastering for each driver it starts.
14. Draws progress square 0, copies the kernel to `0x100000`, then the 64-byte block to `0x5000`, the 136-byte mbi to `0x6000` and the memory summary to `0x7000`. Draws square 1 and calls `uentry()`.

Every failure goes through `fail()`, which prints "NoovexOS cannot start: ..." with the status, waits up to 60 s for a key, and returns the error to the firmware.

Progress markers: `mark()` in the loader and `uefi_mark` in `boot64.S` each draw a 24x24 square at `y = height-36`, `x = 16 + idx*28`. `kmain` draws a white square at index 5.

| idx | colour | drawn by | meaning |
|---|---|---|---|
| 0 | `0xE03030` red | loader | boot services exited, own CR3 active |
| 1 | `0xFF9020` orange | loader | kernel + boot blocks copied |
| 2 | `0xFFD700` yellow | `uefi_entry` | kernel entry reached, GDT reloaded |
| 3 | `0x30C030` green | `uefi_entry` | `.bss` cleared |
| 4 | `0x3080FF` blue | `uefi_entry` | kernel page tables loaded |
| 5 | white | `kmain` | `kmain` running, UEFI marker seen |

### 2.7 The boot-info contract (fixed low memory)

`0x5000` framebuffer / boot block (read by `kmain`, `recovery_act(RA_RESETDISP)`):

| Addr | Type | BIOS (myboot to `boot64.S` start32) | UEFI (`uefiboot.c` `blk[]`) | Reader |
|---|---|---|---|---|
| `0x5000` | u64 | LFB address (mbi+88/+92) | `FrameBufferBase` | `FBB` |
| `0x5008` | u32 | pitch (mbi+96) | `PixelsPerScanLine*4` | `PITCHB` |
| `0x500C` | u16 | width (mbi+100) | horizontal res | `W` |
| `0x500E` | u16 | height (mbi+104) | vertical res | `H` |
| `0x5010` | u32 | WAD address (`0x0E000000` or 0) | 0 | DOOM, `mm_area_conflicts` |
| `0x5018` | u32 | WAD size | 0 | DOOM |
| `0x501C` | u32 | `'WAD!'` (`0x21444157`) is checked to preserve `0x5010/0x5018`; no loader in the tree writes it | 0 | `boot64.S` |
| `0x5020` | u8 | bpp (mbi+108) | 32 | `BPP` |
| `0x5021` | u8 | - | 1 = RGB order, 0 = BGR | `g_fb_rgb` (UEFI only) |
| `0x5028` | u32 | - | usable RAM in MiB | `g_uefi_ram_mb` (UEFI only) |
| `0x502C` | u32 | - | `0x49464555` = `"UEFI"` | UEFI marker |

On the BIOS path the rest of the `0x5000` page holds myboot's VGA font copy (`bios_get_font` writes to `0x5000`). `boot64.S` overwrites only the fields above, so `0x502C` is font data there, never the `UEFI` magic.

`0x6000` Multiboot-style block (UEFI only, 136 bytes): the framebuffer at +88..+108 as in Multiboot, the RSDP (u64) at +120, and `0x50445352` (`"RSDP"`) at +128 when present. `acpi_find_rsdp()` (`core/power.c`) uses it first, then falls back to the EBDA and `0xE0000-0xFFFFF` scans.

`0x7000` memory summary (UEFI only, `mm_summarize`): u32 magic `0x4D58564E` (`"NVXM"`), then u32 `count | overflow<<31`, then up to `MM_MAX`=170 records of 24 bytes `{u64 start, u64 end, u32 type, u32 0}`. Types are collapsed: 7 stays 7 (conventional), other usable-after-exit types (1, 2, 3, 4) become 4, and the rest keep their UEFI type. Adjacent records of the same type are merged. `mm_load()` (`mem/pmm_vmm.c`) reads it only when `0x502C=="UEFI"` and sorts it into `g_mmr[]`. On BIOS, the kernel has no firmware memory map. The E820 map in the Multiboot info is not consumed.

Other fixed regions the kernel assumes (`kareas()` in `core/init/installer.c`, `in_kernel_area()` in `uefiboot.c`):

| Range | Use |
|---|---|
| `0x5000-0x8000` | boot information |
| `0x100000-0x1000000` | kernel image (at most 15 MiB) |
| `0x1000000-0x1080000` | boot stack, top at `0x1080000` |
| `0x1080000-0x1300000` | xHCI, USB HID, USB mass storage, AHCI and NVMe buffers |
| `0x1300000-__bss_end` | boot page tables + `.bss` (`__bss_end` = `0x7D7ADB0` in this build; link-time `ASSERT(__bss_end <= 0x09800000)`) |
| `0x0B000000-0x0C000000` | DMA arena |
| `0x0C000000-0x0E000000` | page-frame pool (`PF_BASE..PF_TOP`) |
| `0x0E000000-0x10000000` | module area (DOOM WAD) |
| `0x10000000-` | heap (relocated by `heap_init` when a UEFI map is known) |

### 2.8 Kernel entry: `arch/x86_64/boot/boot64.S`

#### Image layout

`arch/x86_64/kernel64.ld` links the kernel at `0x100000`, with `.text.entry` first so `_start == 0x100000`. `__immutable_end` is defined after `.rodata` (aligned to 16), `.data` follows, `__file_end` closes the loadable part, and `.bss (NOLOAD)` is placed at `0x01300000`. `objcopy -O binary` emits `0x100000..__file_end` as the flat file. `.bss` is not in the file. The linker script names no `.sig` or `.aptramp` section, so `ld` places these orphans itself. In the shipped ELF the result is:

| Section | VMA | Size | Notes |
|---|---|---|---|
| `.text` | `0x100000` | `0x2323C8` | `_start` at `0x100000`, `uefi_entry` at `0x100198`, `kmain` at `0x199374` |
| `.aptramp` | `0x3323D0` | `0xBE` | AP trampoline |
| `.rodata` | `0x3324A0` | `0x738ECA` | includes the embedded programs (FFmpeg's `play` alone is 3.4 MB compressed) |
| `.sig` | `0xA6B380` | `0x80` | `g_buildsig` (64-byte aligned) |
| `.data` | `0xA6B400` | `0x4DB80` | `__immutable_end` = `0xA6B400` (end of `.sig`) |
| `.bss` | `0x1300000` | `0x6A7ADAC` | `pml4` at `0x1300000`, `pdfb` at `0x1306000`, `boot_pt_end` = `0x1308000` |

The immutable region is everything from `.text` through `.sig`. That `__immutable_end` lands after `.sig` is a property of this link (orphan placement), not something the script guarantees. Signature checking depends on it (2.12).

#### Multiboot header (offset 4)

`_start` is a `jmp start32` followed by a 4-aligned Multiboot 1 header:

| Offset | Value | Field |
|---|---|---|
| +4 | `0x1BADB002` | magic |
| +8 | `0x00000007` | flags: page-align, mem info, video mode |
| +12 | `-(magic+flags)` | checksum |
| +16/+20 | 0 | header_addr / load_addr (unused, flag 16 is clear) |
| +24 | file size | `load_end_addr` slot, stamped by `build.sh` with `len(kernel64.bin)` |
| +28/+32 | 0 | bss_end_addr / entry_addr |
| +36..+48 | 0, 1024, 768, 32 | linear framebuffer, preferred 1024x768x32 |

The size at +24 is read back by `inst_snapshot_data()` and `ip_run()` in the installer, and is asserted by `splice.py`.

#### `start32` (BIOS / Multiboot entry)

1. `cli; cld`. If `0x501C != 'WAD!'`, zero `0x5010` and `0x5018`.
2. If `EAX == 0x2BADB002`: copy mbi +88/+92 to `0x5000/0x5004`, +96 to `0x5008`, +100 to `0x500C` (u16), +104 to `0x500E` (u16) and +108 to `0x5020`. If `mods_count` (mbi+20) is non-zero, copy the first module (`mod_start..mod_end`) to `0x0E000000` with `rep movsl/movsb`, then store the size at `0x5018` and `0x0E000000` at `0x5010`.
3. Zero 6 pages from `pml4` (`pml4`, `pdpt`, `pd0..pd3`). Set `PML4[0]->pdpt` and `PDPT[0..3]->pd0..pd3` (present|writable). Fill 2048 PDEs with `phys|0x83`, which identity-maps 0-4 GiB with 2 MiB pages.
4. Set `CR4.PAE`, load CR3=`pml4`, set `EFER.LME` (MSR `0xC0000080` bit 8), `lgdt gdt64_ptr`, set `CR0.PG|PE`, then `ljmp $0x08, long_entry`.
5. `long_entry` loads DS/ES/SS/FS/GS = `0x10`, sets RSP = `0x1080000`, clears RBP and calls `kmain`. If `kmain` returns, the CPU halts.

`.bss` other than the page tables is not cleared here. `kmain` does that.

#### `uefi_entry` (64-bit entry from `BOOTX64.EFI`)

1. `lgdt gdt64_ptr64(%rip)`, then reload CS with `pushq $0x08; pushq label; lretq`. Load data selectors `0x10`, set RSP=`0x1080000`, `cli; cld`. Draw marker 2.
2. Zero all of `__bss_start..__bss_end` with `rep stosq`. The CPU is still on the loader's tables, which live outside the kernel areas. Draw marker 3.
3. Build `PML4[0]->pdpt`, `PDPT[0..3]->pd0..pd3`, and 2048 2 MiB PDEs for 0-4 GiB.
4. If the framebuffer at `0x5000` is in `[4 GiB, 512 GiB)`, map two GiB starting at its 1 GiB slot through `pdfb` (2 pages).
5. Load CR3=`pml4`, draw marker 4 and call `kmain`.

`tools/emulate.py` (`emulate_kernel_entry`) extracts this code from `boot64.S` text and runs it against test memory. `check.py` (`gdt64_check`) disassembles `uefi_entry` and verifies three things: the `lgdt` operand is a 10-byte pointer whose base is a `gdt*` symbol, the limit is at least `0x17`, and selector `0x08` is a present L=1 code segment.

#### GDT

`gdt64`: null; `0x08` = `0x00209B0000000000` (64-bit code, P, DPL0); `0x10` = `0x0000930000000000` (data). There are two pseudo-descriptors: `gdt64_ptr` (6 bytes, used from 32-bit code) and `gdt64_ptr64` (10 bytes, used from 64-bit code). The per-CPU GDT/TSS (`PCPU->gdt`, `g_tss`) is installed later by the kernel.

### 2.9 `kmain()` step by step (`core/init/main.c`)

| # | Code | What it does |
|---|---|---|
| 1 | zero loop | Zeroes `boot_pt_end..__bss_end` (8 bytes at a time). Keeps `pml4..pdfb` (`0x1300000-0x1308000`) because CR3 still points there. |
| 2 | `percpu_init_bsp()` | `percpu_setup(0,0)` fills `g_cpu[0]` (self pointer, ids, umask 022, user brk/mmap bases, kernel stack top). `cpu_set_percpu()` writes MSR `0xC0000101` (GS base) and zeroes `0xC0000102`. Marks the BSP online. |
| 3 | `inst_snapshot_data()` | Copies the loaded image from `0x100000` into `g_kimg_snap` for later integrity checks by the installer (2.12). |
| 4 | `mm_load()` | Imports the UEFI memory summary from `0x7000` (UEFI only). |
| 5 | `arch_fpu_init()` | Clears `CR0.EM`, sets `CR0.MP` and bit 16 (`WP`), sets `CR4.OSFXSR|OSXMMEXCPT`, records `g_ssse3`, then `fninit` and `ldmxcsr 0x1F80`. |
| 6 | read `0x5000` | `FBB`, `PITCHB`. If `0x502C=="UEFI"` it sets `g_fb_rgb`, `g_uefi_ram_mb` and draws the white marker. Then reads `W`, `H`, `BPP`, sets `BYPP`=4 if `BPP>=32` else 3, clamps to `MAXW/MAXH`, and derives `PITCHB` if it is 0. Halts forever if `W`, `H` or `FBB` is 0. |
| 7 | `g_klog_hook=bootlog_line` | `klog` lines are also drawn on screen from y=200 until the splash starts. |
| 8 | `detect_ram_mb()` | UEFI: `g_uefi_ram_mb`, capped at 4032. BIOS: write/read-back probe every 64 MiB from 128 to 4032 MiB. |
| 9 | `klog_init()`, `power_init()` | COM1 at 115200 if the scratch register works. ACPI: finds the RSDP, walks RSDT/XSDT for the FADT and up to 8 SSDTs. |
| 10 | `heap_init(g_ram_mb)` | Heap at `0x10000000`. With a UEFI map it moves to the largest clean (type 7) region of at least 128 MiB between 256 MiB and `0xFC000000` (`g_heap_mapped=2`), or is clipped to the usable run (`=1`). |
| 11 | `slab_init()`, `pframe_init()` | Slab caches on the heap. Frame pool `0x0C000000-0x0E000000`, enabled only with `g_ram_mb >= 272` (`pf_ready`). |
| 12 | memory log | Reports firmware-map use and `mm_area_conflicts()` (firmware memory inside a kernel area). |
| 13 | `get_brand()` | CPUID brand string. |
| 14 | `pci_scan()` | Function 0 of buses 0-3, slots 0-31, into `pci_ven/dev/cls/sub`. |
| 15 | `pci_quiesce()` | Scans all 256 buses. For each type-0 function of class storage (`01`), network (`02`), multimedia (`04`) or wireless (`0D`), and for serial-bus (`0C`) functions other than SMBus (`0C/05`) and xHCI (`0C/03/30`), it clears the Bus-Master bit in the PCI command register. An xHCI controller is also stopped if its BAR0 is 64-bit and BAR1 is non-zero (located above 4 GiB). This stops DMA the firmware left running. |
| 16 | `init_windows(); fs_init(); disk_init(); env_init(); vfs_init(); term_init();` | Default window rectangles. Seed RAM files (`readme.txt`, `hello.nvx`, `notes.md`, `noovex.cfg`, `demo.pptx`). `disk_init()` (`fs/nvx/nvxfs.c`) locates the system disk and mounts NVX4 when it finds magic `0x3458564E`. Default env (`PATH=/bin`, `HOME`, `USER=root`, `SHELL=/bin/nsh`, ...), procfs, terminal. AHCI/NVMe (`gdisk`) are probed lazily. |
| 17 | `notify()`, `build_bg()` | Welcome toast; wallpaper rendered into `BG`. |
| 18 | `arch_idt_init()` | Vectors 0-47 go to `isr_stubs`, 48-255 to `isr_ignore`, then `lidt`. |
| 19 | `apic_virtual_wire()` | UEFI only. Software-enables the LAPIC (or x2APIC via MSRs `0x808/0x80F/0x835/0x836`), sets LINT0=ExtINT and LINT1=NMI, masks the LVT timer. |
| 20 | `pic_remap(); pit_init(100)` | 8259 remap; PIT at 100 Hz. |
| 21 | `set_gate(HR_LAPIC_VEC,...); hr_init()` | Precise clock: invariant TSC, else HPET, else TSC, else PIT. Also calibrates the LAPIC timer. |
| 22 | PS/2 | If port `0x64` reads `0xFF`, there is no PS/2 controller. Otherwise drains up to 64 bytes and calls `mouse_init()`. |
| 23 | `xhci_mouse_autoinit(); tp_init()` | USB HID and touchpad probing. |
| 24 | `acpi_aml_init(); battery_init()` | Registers the power-button callback, AML namespace, battery via EC. |
| 25 | `splash_boot()` | Unhooks on-screen klog. Black screen, centred wordmark, held about 5 RTC seconds (bounded to 6000 iterations; aborts if CMOS reads `0xFF`). |
| 26 | `g_sig_ok = sig_self_check()` | Integrity check, report-only (2.12). |
| 27 | `live_init(); live_present()` | `g_state=ST_LIVE`. Starts a root session (`ux_core_start("root")`), clears the terminal, prints "To boot setup, type `noovexos_install`", draws the first frame. |
| 28 | `cpu_irq_on()` + timer probe | Waits up to 3,000,000 `pause` iterations for `g_ticks` to move. If it never does, sets `g_ticks_live=0` and the loop polls the PIT counter (`pit_poll_tick`). |
| 29 | `smp_boot_auto_c()` | Reads `/etc/smp`. `off` keeps one core. `starting` means the previous attempt never finished, so it writes `off` and notifies the user. If MADT/ACPI report more than one core, it writes `starting`, runs `smp_enable()`, then writes `on`. The comment above the call in `kmain` ("started on demand ... Boot stays single-core") is out of date. |
| 30 | main loop | See 2.10. |

### 2.10 Main loop and state machine

`g_state` is declared in `graphics/theme/theme.c` with initial value `ST_DESKTOP`, but `kmain` sets `ST_LIVE` before the loop.

```c
enum { ST_SETUP, ST_LOGIN, ST_DESKTOP, ST_BOOTMENU, ST_RECOVERY, ST_DISKMGR, ST_INSTALL, ST_LIVE };
```

Each iteration of `for(;;)` in `kmain`:

1. If timer interrupts are dead, poll the PIT counter.
2. Log a heartbeat every 500 ticks.
3. Run `tp_poll(); etp_poll()`, and every 2 ticks `acpi_poll(); snd_poll()`.
4. If USB HID devices exist, run `xhci_mouse_poll()`. Otherwise, when no job is runnable and ticks are live, drop the big kernel lock and `hlt`. On the desktop with a moved cursor and a precise clock, it first arms a one-shot LAPIC wake at the next frame deadline (`hr_wake_at`).
5. Take the BKL (`bkl_lock`/`bkl_yield`). Flush NVX3 write-back when due (`g_fs_wb_pending`).
6. Dispatch input by state:

| State | Input | Present |
|---|---|---|
| `ST_LIVE` | `live_input` | `live_present` (dirty-row text console) |
| `ST_DESKTOP` | `process_input` | `compose_present` / `present_topbar` / `present_cursor` |
| `ST_BOOTMENU` | `bootmenu_input` | `bootmenu_present` |
| `ST_RECOVERY` | `recovery_input` | `recovery_present` |
| `ST_DISKMGR` | `dm_input` | `dm_present` |
| `ST_INSTALL` | `ip_input` | `ip_present` (+cursor-only path) |
| `ST_SETUP` | `setup_input` | `setup_present` |
| otherwise (`ST_LOGIN`) | `login_input` | `login_present` |

7. On the desktop, `ux_desktop_start(g_user)` and `jc_step()` run. In live mode, `jc_step()` runs and terminal generation changes force a redraw.
8. While `/dev/fb0` is open by a program, presentation is skipped except for `g_fb0_dirty` blits.
9. `usb_hotplug_poll()`. On the desktop, `battery_tick()` runs, and a newly attached USB drive opens Files.
10. Desktop frame pacing is set by `g_refresh_hz` (`frame_ns = 1e9/hz`). A real change gives a full `compose_present`. Every 50 ticks there is a heartbeat: a full frame when DOOM or a toast is active, otherwise only the top bar. A pointer-only move gives `present_cursor` dirty-rect updates, or a full frame over the dock, menus or drags.

State transitions found in the code:

- `ST_LIVE` to `ST_INSTALL` via the `noovexos_install` shell command (`ip_open(ST_LIVE)`, root required).
- Installer page action 6 to `setup_begin()` to `ST_SETUP`.
- Setup finishes in `su_advance` to `su_apply()` to `ST_LOGIN`.
- `login_input` (click or Enter) to `ST_DESKTOP`, opening a terminal if `g_rec_openterm`.
- Launcher power item 0 to `ST_LOGIN`.
- `install` command or Files to `ip_open(ST_DESKTOP)`.

`bootmenu_activate` leads to `ST_SETUP`, `ST_RECOVERY` or the installer, but no code in the tree assigns `ST_BOOTMENU`. The boot menu is only reachable as the installer's or disk manager's return state (`ip_ret`/`g_dm_ret` default `ST_BOOTMENU`).

### 2.11 Boot menu, recovery, setup and splash

- `core/init/bootmenu.c` holds the recovery environment. It has a menu stack (`g_recstack`, depth 5) over four menus: `RM_MAIN` (Continue, Restart, Troubleshoot, Startup Settings, Command Prompt, Turn off), `RM_TROUBLE`, `RM_ADVANCED` and `RM_STARTUP`, with actions `RA_*` handled in `recovery_act()`. Continue to `ST_SETUP`. Safe Mode sets `g_safemode=1`. Command Prompt sets safe mode and `g_rec_openterm=1`. Startup Diagnostics (`g_recmode=1`) lists CPU, RAM, display, IDE, NIC and USB. Reset Display Mode re-reads the `0x5000` block. Restart and shutdown call `power_reboot()`/`power_off()`. Startup Repair, System Restore, Reset PC and UEFI Settings only show info panels (`g_recmode=2`). Startup Repair prints "Kernel image: valid" unconditionally; it does not consult `g_sig_ok`.
- The synthwave boot menu (`bootmenu_present/input/activate`, in `core/init/installer.c`) has five items: Desktop to `ST_SETUP`, Safe Mode to `ST_SETUP` with `g_safemode`, Recovery to `ST_RECOVERY`, Install to `ip_open(ST_BOOTMENU)`, and Boot Configuration (shows resolution, CPU and NIC, and toggles safe mode).
- `core/init/setup.c` is the first-run wizard with `NSU`=7 pages (language, keyboard, timezone, personalisation, Noovex account, storage, done). `su_apply()` commits the user name, accent, theme and locale choices, enables cloud files if signed in, and calls `nvx_settings_save()`. It also contains the login screen (`login_present/input`).
- `core/init/splash.c`: `splash_boot()` is the black wordmark hold called from `kmain`. `synthwave_scene()` (gradient, grid, sun, wordmark) is the background for the boot menu.

### 2.12 Kernel signatures and integrity

#### The NVX64SIG block (`core/buildsig.c`)

```c
__attribute__((aligned(64), used, section(".sig")))
static volatile const struct {
    char     magic[8];        /* "NVX64SIG" */
    char     version[16];     /* "8.96" */
    unsigned int  build_epoch;     /* patched post-link */
    unsigned int  image_size;      /* patched post-link */
    unsigned char sha256[32];      /* immutable-region hash, patched */
    char     builder[16];     /* "NoovexOS build" */
    unsigned long long uefi_entry; /* patched post-link */
    unsigned char sha256_full[32];
    unsigned long long bss_end;    /* = (u64)__bss_end, set by the linker */
} g_buildsig;
```

| Offset | Size | Field | Filled by | Shipped value |
|---|---|---|---|---|
| 0 | 8 | `magic` | source | `NVX64SIG` |
| 8 | 16 | `version` | source | `8.96` (shown by `ver`, Settings) |
| 24 | 4 | `build_epoch` | `build.sh` (`time.time()`) | - |
| 28 | 4 | `image_size` | `build.sh` = flat file length | 5,298,992 (v8.96) |
| 32 | 32 | `sha256` | `build.sh` | immutable-region digest |
| 64 | 16 | `builder` | source | `NoovexOS build` |
| 80 | 8 | `uefi_entry` | `build.sh` from `nm` | `0x100198` |
| 88 | 32 | `sha256_full` | `build.sh` | whole-file digest |
| 120 | 8 | `bss_end` | linker relocation | `0x69CB9E0` |

The block is 128 bytes (`.sig` size `0x80`) at file offset 4,802,816, VMA `0x594900`.

#### Stamping (`build.sh`, step `[6/6]`)

After `objcopy` produces `build/kernel64.bin`, an inline Python script:

1. Asserts `len(b) <= 8388608` ("myboot FAT load limit") and writes `len(b)` at offset 24.
2. Finds the first `NVX64SIG` in the file (`off`). It runs `nm build/kernel64.elf` to get `uefi_entry` and `__immutable_end`, and sets `imm_sz = __immutable_end - 0x100000`. If the symbol is missing, `imm_sz` falls back to `len(b)`; it is also capped at `len(b)`.
3. Writes `build_epoch` (off+24), `image_size` (off+28) and `uefi_entry` (off+80).
4. Zeroes both `sha256` (off+32) and `sha256_full` (off+88).
5. `sha256_full = SHA256(entire file)`. This covers the file with both hash fields zero, but with the size stamp, epoch, image size, `uefi_entry`, `bss_end` and all of `.data` in place.
6. `sha256 = SHA256(b[0:imm_sz])`, computed after `sha256_full` was written, with only the `sha256` field zero.
7. Prints `build signature stamped @ off: sha256=... imm_size=... file=... uefi_entry=0x...`. `check.py` requires this text for its build check.

#### Exactly which bytes are hashed, and why

```
file offset 0                      imm_sz (= __immutable_end-0x100000)        len(file)
|-- .text | .aptramp | .rodata | .sig ----------------|-- .data -----------------|
|<--------------- sha256 (sha256 field = 0) --------->|
|<-------------- sha256_full (sha256 and sha256_full fields = 0) -------------->|
```

- `sha256` covers the immutable region: code, read-only data and the signature block itself. Because `sha256_full` sits inside `.sig` and was filled before this hash was taken, `sha256` also commits to the whole-file digest. The kernel can recompute it at runtime because these bytes never change after load. The comment in `sig_self_check` explains why `.data` is left out: "writable globals in .data change the instant the kernel starts, so hashing them would always fail."
- `sha256_full` covers the whole file as written to disk. It can only be checked against an unmodified copy of the file (offline tools, or the installer's boot-time snapshot), not against live memory.
- `.bss` is never hashed; it is not in the file.

#### Runtime check: `sig_self_check()`, `g_sig_ok`, `g_sig_enforce` (`core/init/main.c`)

`sig_self_check()` returns 0 in any of these cases:

- the magic is unstamped (checks `magic[0]=='N'` and `magic[7]=='G'`);
- `__immutable_end <= 0x100000`;
- the size `sz = __immutable_end-0x100000` is outside `[64, 0x800000]`;
- `&g_buildsig < 0x100000`;
- `hash_off+32 > sz`.

Here `hash_off = (&g_buildsig - 0x100000) + 8+16+4+4`. Otherwise it saves the 32 live bytes at `hash_off`, zeroes them in memory, computes `sha256_hash(0x100000, sz)` (`lib/crypto/nvxcrypto.c`), restores the bytes and compares. Writing into the `const` `.sig` works because the boot page tables map everything writable (PDE flags `0x83`).

`kmain` calls it once, after the splash: `g_sig_ok = sig_self_check();`, and logs `boot: integrity check passed` or `boot: INTEGRITY CHECK FAILED - kernel code was modified`. An unstamped build (`return 0` before hashing) is not treated as tampered: only a real hash mismatch sets `g_sig_tampered=1`.

Since v8.95 a mismatch blocks the boot (`g_sig_enforce = 1`):

1. Just before `smp_boot_auto_c()`, while still on one core, `kmain` shows `skull_screen()`: a binary skull, "WRONG VERSION", "build signature invalid - kernel modified or corrupted", and "Press R to restart, or C to start anyway (not safe)". The log says `boot: waiting for R (restart) or C (continue)`.
2. It then polls the keyboard (`kpop`) and the USB mouse/keyboard (`xhci_mouse_poll`).
   - R calls `power_reboot()`.
   - C logs `boot: user chose to continue with a modified kernel`, redraws the desktop (`live_invalidate`, `live_present`) and continues the boot. Settings then shows the mismatch, and `sig` reports `failed`.

Warning: "Continue" exists so a developer can still boot a locally patched kernel. On a machine you did not change yourself, a mismatch means the kernel file was altered or damaged: choose R and reinstall from known-good media.

Other places that report the result:

- Settings (`apps/system/settings.c`) shows "Integrity check reported a mismatch at boot" with a "Mismatch" badge, instead of "Verified".
- The `sig` shell command prints the block (magic, version, size, epoch, sha256, builder). It re-runs `sig_self_check()` live, printing `integrity: OK` or `MISMATCH`, and adds `boot check: passed` / `failed`. `arch/x86_64/interrupts/interrupts.c` forward-declares `sig_self_check`, `g_sig_ok` and `g_sig_tampered` for this.
- `kcheck` prints `passed`, `FAILED - kernel code modified` or `not checked (unstamped build)`.

Tested in QEMU (BIOS and UEFI): an unmodified image boots with `integrity check passed`; an image with one code byte flipped (offset 0x30000) stops at the skull screen, and C continues to the desktop.

#### Installer integrity check (`core/init/installer.c`)

- `inst_snapshot_data()` (step 3 of `kmain`) takes `klen` from the size stamp at `0x100000+24`. If `klen` is outside `[4096, KSNAP_MAX]` it uses `__file_end-0x100000` instead, and if that is still above `KSNAP_MAX` it sets `klen=0`. It then copies `klen` bytes into `g_kimg_snap`. `KSNAP_MAX` is `0x800000` (8 MiB), the same as the build limit, so any kernel that builds can be captured. (Before v8.95 it was 3 MiB; the 5 MB kernel did not fit, `g_kimg_len` stayed 0 and the installer always stopped.)
- `kimg_check()` redoes both build-time hashes on the snapshot. It works on a temporary copy from `kmalloc(klen)` (it used to be a second static 3 MiB buffer), zeroes `sha256` and hashes `[0, imm)`, compared with `g_buildsig.sha256`. It then also zeroes `sha256_full` and hashes `[0, klen)`, compared with `g_buildsig.sha256_full`. It returns 1 (no hashes) if `sha256_full` is all zero, and -1 if the allocation fails.
- `ip_run()` refuses to install if the size stamp is outside `[4096, 0x800000]`, if `g_kimg_len` is 0 or differs from the stamp ("The kernel image could not be captured at boot."), or if either hash differs ("...kernel code / data loaded at boot is not the original build.").
- The `kcheck` command (`kcheck_run`) prints the stamp, the captured length and the code size, plus the hash results or `boot image: not captured`. It also lists up to 12 4 KiB pages of the immutable region whose live bytes differ from the snapshot. The hash line also goes to the kernel log (`kcheck: boot image: code MATCHES whole file MATCHES` on a good v8.95 boot).

#### Offline verification (`tools/emulate.py`, `tools/check.py`)

```python
def signature_ok(k):
    i = k.find(b'NVX64SIG')
    hash_off, full_off = i + 32, i + 88
    t = bytearray(k); stored_imm = t[hash_off:hash_off+32]; stored_full = t[full_off:full_off+32]
    t[hash_off:hash_off+32] = bytes(32)
    imm = immutable_size_for(k)
    a = None if imm is None else sha256(t[:imm]) == stored_imm
    t[full_off:full_off+32] = bytes(32)
    b = sha256(t) == stored_full
    return (b and a is not False), ...
```

- `immutable_size_for(k)` returns `__immutable_end - 0x100000` from `nm build/kernel64.elf`, but only if `build/kernel64.bin` is byte-identical to `k`. Otherwise it returns `None` and only the full-file hash is checked; the immutable boundary cannot be known without the matching ELF.
- `emulate_bios()` builds the real `fat.c`/`config.c`/`string.c` as a 32-bit program, pasting in the partition-selection code from `stage2.c`. It loads the kernel from the ISO's FAT32 partition and checks that the image is flat, `multiboot_header()` finds a valid header in the first 8 KiB, and `signature_ok()` passes. `emulate_uefi()` checks that the kernel on the CD and USB ESPs is identical to the BIOS-path kernel and runs the real `BOOTX64.EFI` in a PE/firmware simulator.
- `check.py g_static()` runs `signature_ok()` on `build/kernel64.bin`, where both hashes are checked because the ELF matches.

#### Summary: who checks what

| Checker | When | Immutable hash | Full hash | On failure |
|---|---|---|---|---|
| `sig_self_check()` | every boot, `sig` command | yes (live memory) | no | skull screen; R restarts, C continues with `g_sig_ok=0` |
| `kimg_check()` | installer, `kcheck` | yes (snapshot) | yes (snapshot) | installer aborts with a message |
| `uefiboot.c` | UEFI load | no | no | only requires a valid `uefi_entry` |
| myboot stage2 | BIOS load | no | no | - |
| `emulate.py` / `check.py` | host test | if matching ELF | yes | test FAIL |

### 2.13 Size limits along the chain

| Limit | Value | Where |
|---|---|---|
| myboot kernel read | `0xF00000` (15 MiB) | `stage2.c` `fat_read_file(e->kernel, scratch, 0xF00000, ...)`; the scratch buffer at 16 MiB ends below `.bss` use and never overlaps the destination |
| build assertion | 15,728,640 bytes | `build.sh` step 6 |
| UEFI loader | `0xF00000` (15,728,640 bytes) | `uefiboot.c` `KERNEL_MAX` |
| link-time image end | `0x1000000` | `kernel64.ld` `ASSERT(__file_end <= 0x01000000)` |
| self-check region | `0xF00000` | `sig_self_check()` |
| installer snapshot | `0xF00000` | `installer.c` `KSNAP_MAX` |
| `.bss` end | `0x09800000` | `kernel64.ld` `ASSERT` |
| stage2 size | 63 sectors | `mkhybrid.py` assert (stage1 reads 64) |
| `boot.cfg` | 8191 bytes, 16 entries | `stage2.c`, `config.h` |

Every limit is now 15 MiB, and the 10.2 MB v9.11 kernel fits with about 4.8 MB to spare. v9.11 raised it from 8 MiB by moving the boot stack, the fixed DMA buffers and `.bss` up by 7 MiB, and replaced the fixed TLS scratch buffers at `0x908000`-`0x930000` with static ones. Before v9.08 the UEFI loader allowed only 0x5C0000 bytes and the boot stack started at `0x700000`, directly above the image; a bigger kernel was silently overwritten by the stack, which the integrity check then reported as "kernel code was modified" (section 13.12).

### 2.14 Gotchas

- The header comment of `boot64.S` still documents the old `0x5000` layout (pitch at `0x5004`, bpp at `0x500A`). The code and `kmain` use the layout in 2.7.
- `stage2_main` prints `myboot 0.3` while the menu title says `myboot 0.5`.

## Part 3: Memory

NoovexOS 64 has no general-purpose physical page allocator covering all of RAM. Memory is a set of fixed physical regions with one allocator each. The low 4 GiB is identity-mapped with 2 MiB pages, so the kernel uses physical addresses as pointers. User processes get their own PML4 with a private window at PML4 slot 8.

| Allocator | Region | Granularity | Freed? | Source |
|---|---|---|---|---|
| `kmalloc` / `kfree` (heap) | largest clean run at least 128 MiB from `0x10000000` | 16 B (+24 B header) | yes (coalescing) | `mem/heap.c` |
| `kslab_alloc` (slab) | 8 KiB slabs carved from the heap | 16 B ... 4 KiB classes | to its free list | `mem/slab.c` |
| `dma_alloc` / `dma_alloc_pages` | DMA arena `0x0B000000-0x0C000000` | 64 B / 4 KiB | never | `mem/pmm_vmm.c` |
| `pframe_alloc` / `pframe_free` | frame pool `0x0C000000-0x0E000000` | 4 KiB | yes (refcounted) | `mem/pmm_vmm.c` |
| fixed `#define` buffers | `0x1090000-0x1240000` | per driver | n/a | drivers |
| `.bss` statics | `0x00C00000-__bss_end` | n/a | n/a | `arch/x86_64/kernel64.ld` |

The memory code is five units: `lib/base/mem.c`, `mem/heap.c`, `mem/pmm_vmm.c`, `mem/slab.c` and `mem/heap_api.c`. Other units reach the allocators through their interface headers (`include/nvx/mem/heap.h`, `include/nvx/mem/pmm_vmm.h` and so on).

---

### 3.1 Boot order

`kmain` (`core/init/main.c`) sets memory up in this order:

```c
{ extern char __bss_end[]; extern char boot_pt_end[];          /* zero .bss but keep the boot page tables */
  for(volatile u64* z=(u64*)boot_pt_end; (u64)z<(u64)__bss_end; z++) *z=0; }
percpu_init_bsp(); inst_snapshot_data();
mm_load();                       /* firmware memory map from 0x7000 (UEFI only) */
...
g_ram_mb=detect_ram_mb();        /* UEFI value from 0x5028, else a 64 MiB-step write probe */
heap_init((int)g_ram_mb);
slab_init();
pframe_init();                   /* frame pool; enabled only if g_ram_mb >= 272 */
if(mm_known()){ ... if(mm_area_conflicts()) klog("memory: WARNING ...run memmap"); }
```

`detect_ram_mb` (`apps/system/files_classic.c`) returns `g_uefi_ram_mb`, capped at 4032, when the UEFI loader supplied one. Otherwise it writes and reads back a pattern at every 64 MiB step from 128 MiB to 4032 MiB and stops at the first step that fails. The result never exceeds 4032 MiB, which matches the 4 GiB identity map.

---

### 3.2 Physical memory map (firmware map)

#### 3.2.1 Producer: UEFI loader

`boot/uefi/uefiboot.c` calls `mm_summarize()` after `ExitBootServices` succeeds. The function reduces the EFI map to at most `MM_MAX = 170` regions:

- Type 7 (`EfiConventionalMemory`) stays 7.
- Types 1-4 (loader code/data, boot-services code/data; see `usable_after_exit`) become 4, meaning "usable, but the firmware used it before boot".
- Every other type keeps its EFI number (0 reserved, 9 ACPI tables, 10 ACPI NVS, 11 MMIO, and so on).
- Adjacent regions of the same type are merged. Bit 31 of the count is set when the list overflowed.

The loader copies this summary to physical `0x7000`:

```
0x7000  u32 magic 'NVXM' (0x4D58564E)
0x7004  u32 count | (overflow << 31)
0x7008  entries[count], 24 bytes each: u64 start, u64 end (exclusive), u32 type, u32 pad
```

The loader also works out how much RAM is usable. It starts at `0x10000000`, or at 1 MiB if nothing is usable there, and extends through contiguous usable regions. The end of that run, in MiB, goes to `0x5028` as `g_uefi_ram_mb`. The loader warns when non-usable firmware memory overlaps a kernel area (`{0x5000,0x8000}`, `{0x100000,0x980000}`, `{0x980000,0xC00000}`, `{0xC00000,kbss}`, `{0x10000000,0x11000000}`). It refuses to boot if the loader image or its stack lies where the kernel or the boot information must go.

#### 3.2.2 Consumer: `mm_load` and queries (`mem/pmm_vmm.c`)

```c
typedef struct { u64 s,e; u32 t; } mmreg;
#define MMR_MAX 170
static mmreg g_mmr[MMR_MAX]; static int g_mmr_n=0, g_mmr_over=0;
```

`mm_load()` does nothing unless `*(u32*)0x502C == 'UEFI'` (`0x49464555`) and `0x7000` holds `NVXM`. Otherwise it insertion-sorts the entries by start into `g_mmr`, skipping empty ones. After a BIOS or Multiboot boot `g_mmr_n` stays 0, `mm_known()` returns false, and every check below treats memory as acceptable.

| Function | Meaning |
|---|---|
| `mm_ok_type(t)` | `t==7 \|\| t==4` |
| `mm_run_end(c, clean)` | Follows overlapping or adjacent regions from `c` (only type 7 when `clean`, types 7 and 4 otherwise) and returns the end of the contiguous run, or 0 if `c` is not in one |
| `mm_usable_end(c)` | `mm_run_end(c,0)` |
| `mm_clean_region(lo,hi,&bs,&be)` | Largest type-7-only run inside `[lo,hi)`, with its start rounded up to 1 MiB |
| `mm_first_bad_ex(s,e,clean,...)` | First sub-range of `[s,e)` that is not usable (or not clean), with its type; `0xFFFFFFFF` means "not in map" |
| `mm_is_clean(s,e)` / `mm_is_usable(s,e)` | Whole range is type 7 / type 7 or 4 |
| `mm_skip_bad(c,n,top)` | Moves `c` forward, page-aligned, past non-clean regions until `[c,c+n)` is clean. Gives up after 256 steps and returns `top` |
| `mm_tname(t)` | Display name used by `memmap` |

`mm_skip_bad` matters most: the DMA arena uses it so that no buffer lands in memory the firmware has used (section 3.6). `pframe_init` uses `mm_is_clean` per frame for the same reason.

#### 3.2.3 Diagnostics (`core/init/installer.c`)

`kareas()` lists the kernel's own areas: boot information, kernel image and stack, USB and disk buffers, page tables and kernel data, DMA arena, page pool, module area, and heap. The DMA arena and page pool are marked `skips`, because they route around bad ranges instead of failing on them. `mm_area_conflicts()` counts non-`skips` areas that overlap non-usable firmware memory; `kmain` logs a warning if any do, and the installer refuses to run. The `memmap` shell command prints the firmware map below 4 GiB, each area with `ok`, `OVERLAPS` or `skips`, the heap range and its sizing mode, `dma_cur`, and the free frame count. `memtest` walks the heap block headers (checking `HBLK_MAGIC`), then fills free DMA-arena space, free frame-pool runs, the module area if unused, and up to 16 x 16 MiB `kmalloc` blocks with an address hash. It reads them back immediately and again after 3 s.

---

### 3.3 Fixed physical regions

All addresses are physical, and also virtual through the identity map.

| Range | Use | Defined in |
|---|---|---|
| `0x5000-0x5040` | Boot information block (64 B, details below) | `boot64.S`, `uefiboot.c`, `core/init/main.c` |
| `0x6000-0x6088` | Multiboot-style info from the UEFI loader; RSDP at `+120`, `'RSDP'` magic at `+128` | `uefiboot.c`, `core/power.c` (`acpi` RSDP lookup) |
| `0x7000-0x7FF8` | Firmware memory-map summary (`NVXM`, 8 + 170x24 B) | `uefiboot.c`, `mm_load` |
| `0x8000-...` | AP start-up trampoline (copied from `ap_tramp_start`) | `arch/x86_64/smp/smp.c`, `ap_trampoline.S` |
| `0x8FD8` / `0x8FE0` / `0x8FF0` / `0x8FF8` | AP parameter block: CPU index, stack top, CR3, `ap_main` | `smp.c` |
| `0xE0000-0x100000` | Read only: scanned in 16-byte steps for the BIOS MP table | `smp.c` line 5 |
| `0x100000` | Kernel load address and `_start` (`. = 0x100000` in `kernel64.ld`); byte 24 holds the size stamp | `kernel64.ld`, `boot64.S` |
| `0x100000-0x1000000` | Kernel image, at most 15 MiB (`KERNEL_MAX` in the UEFI loader, `ASSERT(__file_end <= 0x01000000)` in the linker scripts). It was 8 MiB in v9.08 to v9.10 | `kernel64.ld`, `uefiboot.c` |
| `0x1000000-0x1080000` | Boot stack, `rsp = 0x1080000`, grows down (512 KiB). Before v9.08 the top was `0x700000`, in v9.08 to v9.10 `0x980000` | `boot64.S` |
| `0x1090000-0x10B6000` | xHCI controller 0 memory (`g_xmem`: DCBAA, command ring, ...). Controllers 1-3 use `g_xhc_mem[3][0x26000]` in `.bss` | `drivers/usb/xhci.c` |
| `0x1100000-0x1150000` | USB HID entries: `XHID_BASE(r) = 0x1100000 + r*0x8000`, `XHID_MAX = 10` (input ctx, device ctx, EP0 ring, ...) | `xhci.c` |
| `0x1150000-0x1166000` | USB mass storage: `MSD_INPUTCTX`, `MSD_DEVCTX`, EP0/IN/OUT rings, CBW `0x1155000`, CSW `0x1155100`, 64 KiB data buffer `0x1156000` | `xhci.c` |
| `0x1200000-0x1203000+` | AHCI: command list `AHCI_CLB`, FIS `0x1201000`, command table `0x1202000`, data `AHCI_BUF 0x1203000` | `drivers/storage/gdisk.c` |
| `0x1230000-0x1235000+` | NVMe: ASQ, ACQ, I/O SQ `0x1232000`, I/O CQ `0x1233000`, Identify `0x1234000`, data `NVME_BUF 0x1235000` | `drivers/storage/nvme.c` |
| `0x1300000` | `__bss_start`. First come the boot page tables `pml4`, `pdpt`, `pd0-pd3` and `pdfb` (8 KiB), ending at `boot_pt_end = 0x1308000`. Then all kernel statics, including `SCENE`/`BG` (1920x1080x4 B each), `FS[]`, `pf_bitmap`/`pf_ref`, `g_ap_stacks`, `g_pstack`, and since v9.11 the TLS scratch buffers (`g_tls_inner`, `g_tls_rec`, `g_tls_rb`, `g_tls_mch`) and the HTTPS `g_tls_io`, which used to sit at fixed addresses `0x908000`-`0x930000` | `boot64.S`, `kernel64.ld`, `lib/crypto/nvxtls.c` |
| `__bss_end` (must be at most `0x09800000`) | The linker `ASSERT` fails the build if `.bss` grows past this; in the v9.11 `build/kernel64.elf`, `__bss_end = 0x7D7ADB0` | `kernel64.ld` |
| `0x0B000000-0x0C000000` | DMA arena, 16 MiB (`DMA_ARENA_BASE/TOP`) | `mem/pmm_vmm.c` |
| `0x0C000000-0x0E000000` | Frame pool, 32 MiB = 8192 frames (`PF_BASE/PF_TOP`) | `mem/pmm_vmm.c` |
| `0x0E000000-0x10000000` | Module area. The Multiboot path copies the first module (the Doom WAD) here and records its address and size at `0x5010`/`0x5018` | `boot64.S`, `apps/games/doom.c` |
| `0x10000000-...` | Default heap base; the actual range depends on the firmware map (section 3.5) | `mem/heap.c` |
| Framebuffer (`*(u64*)0x5000`) | Linear framebuffer; mapped explicitly when above 4 GiB (section 3.4) | `boot64.S` |
| LAPIC `0xFEE00000` (default), HPET, ECAM, ... | MMIO below 4 GiB through the identity map; above 4 GiB through `acpi_hi_map` | `smp.c`, `hrtimer.c`, `acpi_dev.c` |

Note: the comment at the top of `mem/pmm_vmm.c` says the frame pool covers "160 MB .. 256 MB". The code defines 192-224 MiB.

Boot information block at `0x5000`, as read by `kmain` and written by `uefiboot.c` and `boot64.S`. The header comment in `boot64.S` describes an older layout and is out of date.

| Offset | Size | Field |
|---|---|---|
| `0x5000` | u64 | framebuffer physical address |
| `0x5008` | u32 | pitch in bytes |
| `0x500C` / `0x500E` | u16 | width / height |
| `0x5010` / `0x5018` | u32 | module (WAD) address / size; cleared unless `0x501C == 'WAD!'` |
| `0x5020` | u8 | bits per pixel |
| `0x5021` | u8 | 1 = RGB framebuffer (UEFI) |
| `0x5028` | u32 | usable RAM in MiB (UEFI) |
| `0x502C` | u32 | `'UEFI'` (`0x49464555`); gates `mm_load`, RAM size and `apic_virtual_wire` |

---

### 3.4 Boot page tables

`boot64.S` reserves its tables at the start of `.bss`. They are 4 KiB-aligned, and `kmain` does not clear them:

```
pml4 (0x1300000)  pdpt (0x1301000)  pd0..pd3 (0x1302000..0x1305FFF)  pdfb (0x1306000, 2 pages)
```

Both entry paths build the same identity map of 0-4 GiB with 2 MiB pages:

```
CR3 -> pml4[0] -(P|RW)-> pdpt[0..3] -(P|RW)-> pd0..pd3, 4 x 512 PDEs
                                               PDE[i] = (i * 2 MiB) | 0x83   (P | RW | PS)
pml4[1..511] = 0            (kernel touches nothing else; slot 8 becomes the user window)
```

- BIOS/stage2 or Multiboot (`start32`): the tables are built in 32-bit code. The code then sets CR4.PAE, loads CR3, sets EFER.LME, sets CR0.PG|PE, far-jumps to `long_entry` with `rsp = 0x1080000`, and calls `kmain`.
- UEFI (`uefi_entry`): the loader has already switched to its own 0-64 GiB identity tables after `ExitBootServices`. The kernel loads its GDT, zeroes the whole `.bss`, and builds the same 4 GiB map. If the GOP framebuffer lies between 4 GiB and 512 GiB, it also fills the two `pdfb` pages and points `pdpt[fb>>30]` and the next entry at them. This maps 2 GiB of framebuffer with `0x83`. It then loads CR3 with `pml4`.

Note: CR3 is never changed for the kernel after boot. `g_kern_cr3` is just the boot `pml4`, saved when the scheduler starts (`core/proc/sched.c`).

#### `acpi_hi_map` (`drivers/acpi/acpi_dev.c`)

```c
static u64 acpi_hi_map(u64 pa){
    if(pa<0x100000000ULL)return pa; if(pa>=0x8000000000ULL)return 0;
    ... u64* pdpt=(u64*)(pml4[0]&PTE_ADDR); int i3=(int)(pa>>30);
    if(!(pdpt[i3]&1)){ u64* pd=(u64*)dma_alloc_pages(1); ...
        for(int k=0;k<512;k++)pd[k]=(((u64)i3<<30)+((u64)k<<21))|0x9BULL;
        pdpt[i3]=(u64)pd|3ULL; cpu_set_pt(cr3); }
    return pa; }
```

`acpi_hi_map` identity-maps MMIO between 4 GiB and 512 GiB by adding a whole 1 GiB slot to the shared slot-0 PDPT. The PD comes from the DMA arena. Its pages are 2 MiB with `0x9B` = P|RW|PWT|PCD|PS, which makes them uncached. The function returns `pa` unchanged, because the mapping is an identity map. The HPET (`hrtimer.c`) and the Intel backlight BAR use it. Every process PML4 copies `pml4[0]` (section 3.8), so the new mapping appears in all address spaces at once.

---

### 3.5 Kernel heap (`mem/heap.c`, `mem/heap_api.c`)

#### Sizing: `heap_init(ram_mb)`

The rules are applied in order. `g_heap_mapped` records which one won, and `memmap` prints it.

| # | Condition | `heap_base` | `heap_top` | `g_heap_mapped` |
|---|---|---|---|---|
| 0 | default (no firmware map) | `0x10000000` | `ram - 1 MiB` if `ram > base + 128 MiB`, else `base + 128 MiB` | 0 "size from RAM probe" |
| 1 | `mm_clean_region(0x10000000, 0xFC000000)` finds a type-7 run at least 128 MiB | start of the largest run (1 MiB-aligned) | `end - 1 MiB` | 2 "memory the firmware never used" |
| 2 | else `mm_usable_end(0x10000000)` (types 7 and 4, capped at `0xFC000000`) is at least `base + 64 MiB` | `0x10000000` | `ue - 1 MiB` | 1 "limited to firmware RAM" |
| 3 | else scan `c = 0x10000000 ... 0xF0000000` in 1 MiB steps for a usable run at least 128 MiB | `c` | `end - 1 MiB` | 1 |

The initial heap arena always lies below 4 GiB (cap `0xFC000000`), inside the boot identity map. Type-7-only placement (rule 1) means DMA left running by firmware cannot target heap memory. When that arena is exhausted, the heap extends into RAM above 4 GiB on demand (see "High-memory extension" below), so the sub-4 GiB cap bounds only the first arena, not the total heap.

#### Allocator

```c
typedef struct hblk { unsigned long size; struct hblk* next; int used; unsigned int magic; } hblk;   /* 24 bytes */
#define HBLK_MAGIC 0x4E56484Bu   /* 'NVHK' */
static hblk* heap_head; static unsigned long long heap_cur, heap_top, heap_base;
```

The heap is a singly linked list of blocks in address order, plus a bump pointer `heap_cur`:

```
heap_base                                   heap_cur                 heap_top
|[hdr|payload][hdr|payload (free)][hdr|payload]|<----- never used ----->|
 +- next ------>+- next ---------->+- next=0
```

- `kmalloc(n)`: rounds `n` up to 16. It first searches the list first-fit for a free block with `size >= n`. If the leftover is at least `n + 24 + 32`, the block is split and the tail becomes a new free block. If no block fits, it bumps `heap_cur` by `24 + n` and walks the list to append the new block, so this path is O(blocks). It returns 0 when `heap_cur + need > heap_top`. Payloads start 24 bytes after a 16-aligned header, so they are only 8-byte aligned.
- `kfree(p)`: checks the header with `hblk_ok`: a valid arena address (the low arena or any high-memory arena), 8-aligned, magic matches. A bad pointer is ignored without any message. It marks the block free, then walks the whole list and merges each free block with physically adjacent free successors. `heap_cur` never moves back, so freed memory is reused only through the list.
- `krealloc(p,n)`: returns `p` if the old size is already at least `n` (it never shrinks). Otherwise it does `kmalloc`, copies, and `kfree`s the old block.
- `kcalloc(a,b)`: `kmalloc(a*b)` plus zeroing. It does not check `a*b` for overflow.
- Every list walk is bounded at 1,000,000 iterations so that a damaged list cannot hang the kernel.

`mem/heap_api.c` exports `malloc`, `free`, `realloc` and `calloc` as non-static aliases, so that ported C libraries (Lua, ntfs-3g, zstd, ...) link unchanged. It also defines `heap_used()`, the sum of used block sizes; unlike the allocator, it walks without `hblk_ok` and without a bound. The `mem` command prints the heap base, total size, bytes in use, bytes reserved (`heap_cur - heap_base`), and the result of a 1 MiB allocate/write/free self-test. Large consumers allocate on first use; for example `apps/games/doom.c` takes `kmalloc(24 MiB)` for Doom's private heap.

#### High-memory extension

The low arena is bounded below 4 GiB, but a laptop's usable RAM mostly sits above 4 GiB, and the sub-4 GiB window is further eaten by the PCI hole, the kernel image and the sysroot overlay. A large `sudo apt install` needs transient RAM per package (the downloaded `.deb`, the decompressed `data.tar`, the package index), and on the live ISO the writable filesystem is RAM-backed as well. Confined to the low arena, these exhausted it and failed with `E: out of space or memory while unpacking` even though gigabytes above 4 GiB were free.

`kmalloc` therefore keeps a small table of extra arenas. When neither the free list nor the low arena can satisfy a request, it rounds the request up (at least a 64 MiB chunk, 2 MiB-aligned) and calls `hp_alloc_run(pages)` in `mem/pmm_vmm.c`, which scans the high frame pool for a contiguous run of free frames within one region, marks them used, zeroes them, and returns the base. Because `mm_map_high` identity-maps every high RAM region with 2 MiB pages at boot, that physical base is directly usable as a kernel pointer. The run becomes a new bump arena; later allocations fill the low arena and every high arena that has room. `hblk_ok` (used by `kfree`, `krealloc` and the list walks) accepts a pointer in the low arena or in any high arena, so frees and coalescing work uniformly across them. Coalescing never merges across arenas, since the physical-adjacency test in `kfree` only joins neighbours within one contiguous run.

The extension depends on the high frame pool being up (`pf_ready`, RAM at least 500 MiB), which `hp_init` validates at boot with a three-point write/read probe before the pool is used. Drivers that need a buffer below 4 GiB for 32-bit DMA use `dma_alloc` (section 3.6), not `kmalloc`, and their init-time allocations run while the low arena is still plentiful, so they are unaffected.

#### Slab allocator (`mem/slab.c`)

```c
#define SLAB_BACKING 8192ULL
static const unsigned long SLAB_SIZES[9]={16,32,64,128,256,512,1024,2048,4096};
```

Each `slab_cache` keeps a LIFO free list of objects. `slab_grow` takes one 8 KiB block from `kmalloc`, links it into `pages` for statistics, and cuts the rest (`(8192 - 8) / objsize` objects) into the free list.

| Call | Behaviour |
|---|---|
| `kslab_alloc(n)` | Smallest class at least `n`; pops the free list in O(1); `kmalloc(n)` if `n > 4096` or the slab cannot grow |
| `kslab_free_sz(p,n)` | Caller passes the size; pushes onto that class's list, or `kfree` if `n > 4096` |
| `kslab_talloc` / `kslab_tfree` | Stores `n+8` in an 8-byte prefix so the free does not need the size |
| `slab_stats` | Backs the `slabinfo` command: size, live objects, slabs per class |

Backing slabs are never given back to the heap. The cache has no lock; it relies on the big kernel lock that `isr_dispatch` takes (`bkl_lock`). Nothing else in the tree calls `kslab_*` apart from `slabinfo`.

---

### 3.6 DMA arena (`dma_alloc`, `dma_alloc_pages`)

```c
static unsigned long long dma_cur=DMA_ARENA_BASE;   /* 0x0B000000 */
static void* dma_alloc(unsigned long n){ n=(n+63)&~63UL; u64 c=mm_skip_bad(dma_cur,n,DMA_ARENA_TOP);
    if(c+n>DMA_ARENA_TOP)return 0; dma_cur=c; void* p=(void*)dma_cur; dma_cur+=n; /* zero */ return p; }
static void* dma_alloc_pages(unsigned long pages){ /* align dma_cur to 4 KiB, skip bad, bump, zero */ }
```

The arena is a bump allocator over 16 MiB of identity-mapped memory below 4 GiB, with no free. Allocations are zeroed and 64-byte aligned, or page-aligned for `dma_alloc_pages`. `mm_skip_bad(..., clean)` steps over any range the firmware map does not list as type 7. Callers allocate once and cache the pointer in a `static`.

| Caller | Purpose |
|---|---|
| `drivers/net/wifi/rtw8821ce.c`, `net/wifi/*.c` | Wi-Fi TX/beacon packet buffers |
| `shell/commands.c` (`RXNUM`/`BENUM` rings) | Wi-Fi RX and beacon rings |
| `drivers/usb/xhci.c` | xHCI scratchpad buffers when a controller needs more than 32 |
| `drivers/video/nvidia/nvdisp.c` | display push buffer |
| `drivers/acpi/acpi_dev.c` | `acpi_hi_map` page directories |
| `core/proc/elf.c` | `nvx_dlload` module images and `nvx_alloc` for modules |
| `sys/native/kapi.c` | `kapi_dma_pages` for loadable driver modules |

---

### 3.7 Frame pool (`pframe_*`)

```c
#define PF_BASE 0x0C000000ULL
#define PF_TOP  0x0E000000ULL
#define PF_N    ((u32)((PF_TOP-PF_BASE)/4096ULL))     /* 8192: the fixed 32 MiB part */
#define PF_MAXN (PF_N+524288u)                        /* plus at most 2 GiB taken from the heap */
u8  pf_bitmap[(PF_MAXN+7)/8];  static u16 pf_ref[PF_MAXN];
u32 pf_n;  u64 pf_x_base, pf_x_top;                    /* frames in use by the pool, extension range */
```

The pool has two parts. Frame numbers below `PF_N` are the fixed 32 MiB at `PF_BASE`. Frame numbers from `PF_N` up to `pf_n` are an extension range `[pf_x_base, pf_x_top)` carved out of the top of the kernel heap at boot, so the pool grows with installed RAM. `pf_idx(pa)` maps a physical address to a frame number (or -1), `pf_pa(i)` maps back, and `pf_owned(pa)` tells whether an address belongs to the pool.

- `pframe_init` sets `pf_ready = (g_ram_mb >= 272)` and then calls `pframe_grow_from_heap`. If the heap has at least 256 MiB free, half of the free heap (rounded down to 2 MiB, at most 2 GiB) is taken from its top: `heap_top` is lowered, and at least 128 MiB of heap stays above `heap_cur`. With 1 GiB of RAM the pool is about 330 MiB; with 8 GiB it is the full 2 GiB plus 32 MiB.
- The bitmap is then cleared for `pf_n` frames. Every frame that is not clean (`!mm_is_clean`) is marked used up front, so firmware-used frames are never handed out. With less than 272 MiB the pool stays off, and ring-3 programs cannot run; `kmain` logs this.
- `pframe_alloc` scans the bitmap first-fit starting at a moving hint (`pf_hint`), sets `pf_ref = 1`, zeroes the 4 KiB, and returns the physical address, or 0 if none is free.
- `pframe_free(pa)` ignores addresses outside the pool and frames not marked used. If `pf_ref > 1` it only decrements; otherwise it clears the bit.
- `pframe_ref(pa)` adds a reference to an allocated frame, saturating at `0xFFFF`.
- `pframe_refs(pa)` returns the reference count. It returns 1 for any address outside the pool, so the copy-on-write code treats those pages as unshared.
- `pframe_free_count()` returns `pf_n - pf_used`, or 0 when the pool is off. It is used for the ENOMEM checks in `mmap` and `brk`, and as `freeram` in Linux `sysinfo` (`sys/linux/linux.c`).

The pool supplies every user page, every user page-table level (PML4/PDPT/PD/PT), and shared-memory segments (`shm_get_k` in `core/proc/sched.c`). Code that walks frames by number (the installer's memory test, the `mem` command) uses `pf_n` and `pf_pa`; code that asks "is this a pool frame" (copy-on-write in `vm_clone_user`) uses `pf_owned`.

Before v8.97 the pool was only the fixed 32 MiB, whatever the RAM size, so a single large `malloc` in a musl program failed with ENOMEM. `featest` now allocates and touches 96 MiB with `malloc` and two 64 MiB `mmap`s in one process.

---

### 3.8 Virtual memory (`mem/pmm_vmm.c`, `core/proc/jobs.c`)

#### PTE format

| Bit(s) | Mask | Meaning in NoovexOS |
|---|---|---|
| 0 | `0x001` | Present |
| 1 | `0x002` | Writable |
| 2 | `0x004` | User |
| 7 | `0x080` | PS (2 MiB/1 GiB page) in PDE/PDPTE; `vm_walk` refuses to descend through one |
| 9 | `0x200` | `PTE_COW`: software bit, present but read-only, copy on write |
| 10 | `0x400` | `PTE_LAZY`: software bit, not present, allocate a zeroed frame on first touch |
| 12-51 | `PTE_ADDR = 0x000FFFFFFFFFF000` | frame address |

User mappings use flags `0x7` (P|RW|US) whatever the ELF segment or `mmap` `prot` says. NX is not used. Intermediate tables are always created with `0x7`.

#### Address-space primitives

```c
static u64 vm_new_pml4(void){ u64 p=pframe_alloc(); ... u64* k=(u64*)(cpu_get_pt()&~0xFFFULL);
    for(int i=0;i<512;i++)t[i]=k[i];   /* share the kernel mappings */
    t[8]=0; return p; }
```

`vm_new_pml4` copies all 512 entries of the current PML4. Only slot 0, the identity map, is populated in the kernel's PML4, so the kernel's PDPT and PDs are shared by reference. Slot 8 is then cleared for the new space.

`vm_walk(pml4, v, create)` returns a pointer to the PT entry for `v`:

```
 v (48-bit):  | 47..39 | 38..30 | 29..21 | 20..12 | 11..0 |
                 i4       i3       i2       i1     offset
 reject v < 0x8000000000 (slot 0 = kernel) or v >= 0x800000000000
 CR3/pml4pa -> PML4[i4] -> PDPT[i3] -> PD[i2] -> PT[i1]  <-- returned pointer
                 | !P && create: pframe_alloc() | 0x7 at each level
                 | !P && !create: return 0
                 + PDPT/PD entry with PS (0x80): return 0
```

Table frames are reached through their physical addresses, which works because the pool lies inside the identity map.

| Function | Behaviour |
|---|---|
| `vm_map(pml4,v,pa,flags)` | Walk with create, `*e = pa \| flags \| P`, `invlpg v` |
| `vm_unmap(pml4,v)` | `*e = 0`, `invlpg`; does not free the frame |
| `vm_map_lazy(pml4,v)` | Creates tables; if the PTE is not present, sets it to `PTE_LAZY` |
| `vm_pte_used(e)` | `(e & P) \|\| (e & PTE_LAZY)` |
| `vm_selftest` | Maps the same VA (`0x8000000000`, slot 1) to different frames in two spaces and checks isolation |
| `vm_free_user(cr3)` (`jobs.c`) | Walks slot 8: `pframe_free` on every present leaf (refcount-aware), then each PT, PD and the PDPT; clears `pml4[8]`; frees the PML4 frame. Lazy entries need no freeing |
| `vm_clone_user(src)` (`jobs.c`) | Copy-on-write fork, below |

`invlpg` flushes only the current CPU's TLB. Nothing performs a TLB shootdown.

#### `vm_clone_user`: copy-on-write fork

For every used PTE in the parent's slot 8:

1. Lazy: the child gets `PTE_LAZY`, and both sides fault in their own zeroed page later.
2. Frame outside the pool (`!pf_owned(pa)`): copied eagerly into a new frame.
3. Writable pool frame: the parent PTE becomes `(e & ~RW) | PTE_COW`, and the child gets the same value.
4. Read-only pool frame: shared as it is.

Cases 3 and 4 then call `pframe_ref(pa)`. If any parent PTE changed and the parent is the current space, CR3 is reloaded to flush the TLB, and `smp_tlb_sync(src)` makes every other core that is running a thread of the parent flush too, so none of them can keep writing to a page that is now shared with the child. `jc_fork` calls this. The older demo scheduler's `do_fork` in `core/proc/sched.c` still copies every page eagerly.

#### CR0.WP

`arch_fpu_init` (`arch/x86_64/cpu/cpu_init.c`), which runs on the BSP from `kmain` and on each AP from `smp_sched.c`, sets CR0 bit 16 (WP). Ring-0 writes to read-only user pages therefore fault too. Before the kernel writes through a user pointer it calls `jc_umapped(p,n)` (`jobs.c`). This checks that the range lies in the user window, faults in lazy and COW pages with `vm_fault_user(cr3, v, 1)`, and requires `(e & 5) == 5` (present and user).

---

### 3.9 User address space

User space is the single PML4 slot 8, `0x400_0000_0000-0x7FF_FFFF_FFFF` (4 TiB-8 TiB). `jc_uok(p,n)` accepts exactly `0x40000000000 <= p` and `p+n < 0x80000000000`, with no wraparound.

```
0x800_0000_0000 +- end of window (jc_uok upper bound)
                |   ... mmap grows upward (g_mmap_next is a high-water mark)
0x400_0400_0000 +- mmap base       (p->mmnext / g_mmap_next initial value)
                |   brk heap, 32 MiB max (lx_brk accepts [0x40002000000, 0x40004000000))
0x400_0200_0000 +- brk start       (p->brk / g_ubrk) = ELF_IMG_TOP
                |   ... ELF segments must fit in [ELF_IMG_LO, ELF_IMG_TOP)
0x400_0060_0000 +- SHM_VADDR: shm segment id at +id*1 MiB (legacy shm_map_k)
0x400_0040_0000 +- ELF_PIE_BASE: ET_DYN (static-PIE) load bias
0x400_0020_0000 +- legacy thread stacks (g_thread_stk_next, +64 KiB each)
0x7F0_0000_0000 +- ELF_INTERP_BASE: the dynamic linker (/lib/ld-musl-x86_64.so.1) of a dynamic program
                |   ... mmap area continues below
0x400_0010_1000 +- initial stack top (USTK_V + 0x1000)
0x400_0010_0000 +- USTK_V: stack page 0 (eager)
0x400_000F_F000 +- stack page 1 (eager)
0x400_000F_1000 +- ELF_STK_LO: pages 2..15 are PTE_LAZY; the image may not reach above this
                |   pages 16..255 are also PTE_LAZY when free (JS_STK_PAGES = 256, up to 1 MiB),
                |   stopping one guard page above anything already mapped (a native program's image)
0x400_0005_0000 +- legacy demo "id byte" page (sched.c)
0x400_0000_2000 +- ring3.c self-test stack
0x400_0000_0000 +- ELF_IMG_LO / ring3.c self-test code page
```

#### ELF loading (`elf_build_space`, `core/proc/elf.c`)

- Accepts ELF64 x86-64 `ET_EXEC` (bias 0) or `ET_DYN` (bias `ELF_PIE_BASE`). The program headers must lie inside the file and number from 1 to 64.
- A `PT_INTERP` header (at most 127 bytes) makes it a dynamic program; only `ET_DYN` (PIE) programs may have one. After the program's own segments are mapped, `elf_load_interp` looks the interpreter up with `jc_find_lib` (section 6.10), checks that it is an x86-64 `ET_DYN` file, and maps its `PT_LOAD` segments at `ELF_INTERP_BASE`. The entry point becomes the interpreter's entry, `AT_BASE` is set to `ELF_INTERP_BASE`, and `AT_ENTRY` and `AT_PHDR` still describe the program, which is what musl's dynamic linker expects.
- Each `PT_LOAD` must fit in `[ELF_IMG_LO, ELF_IMG_TOP)`, must not overlap the stack range `[ELF_STK_LO, USTK_V+0x1000)`, and must have `filesz <= memsz`. The entry point must fall inside an executable segment.
- Pages are allocated eagerly with `pframe_alloc` and mapped `0x7`. Bytes are copied up to `filesz` and zeroed up to `memsz`. A page shared by two segments is reused.
- Two stack frames are mapped eagerly at `USTK_V` and `USTK_V - 0x1000`. An 8 KiB image built in `g_stkimg` is copied into them: argc, argv, envp, the auxv entries (`AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_ENTRY`, uid/gid, `AT_HWCAP`, `AT_CLKTCK = 100`, `AT_RANDOM` with 16 xorshift-of-TSC bytes, `AT_EXECFN`), and the strings.
- Any failure calls `vm_free_user(sp)` on the half-built space.

`jc_build` (`jobs.c`) then marks stack pages 1 to 255 below `USTK_V` lazy where they are still unused. From page 16 on it stops as soon as the next page down is in use, so a native program linked at `0x40000000000` keeps a guard page between its image and its stack. `jc_create` sets `p->brk = 0x40002000000` and `p->mmnext = 0x40004000000`. `jc_exec` builds the new space first, switches to `g_kern_cr3`, frees the old space, and resets `g_ubrk` and `g_mmap_next`. While a process runs, its `brk` and `mmnext` live in the per-CPU variables `g_ubrk` and `g_mmap_next`. They belong to the process, not the thread, so they are kept in the thread-group leader's `struct jproc` (section 5.15): `jc_enter` loads them from the leader, `linux_syscall` reloads them from the leader before every Linux system call and stores them back after it, and for native programs the end of `jc_enter` stores them back.

---

### 3.10 `mmap`, `munmap`, `brk` (`sys/linux/linux.c`)

| Linux nr | Handler | Behaviour |
|---|---|---|
| 9 | `lx_mmap` | See below |
| 10 `mprotect` | `return 0` | No-op; every page stays RW |
| 11 | `lx_munmap` | Page-aligned `a`, non-zero `len`, inside the window, not overlapping the stack range (EINVAL otherwise); then `lx_unmap_range` |
| 12 | `lx_brk` | Lazy growth and shrink, below |
| 25 `mremap` | `-ENOMEM` | Unsupported |
| 28 `madvise` | `return 0` | No-op |

`lx_mmap(addr,len,prot,flags,fd,off)`: `prot` is ignored.

1. Returns EINVAL if `len` is 0 or `off` is not page-aligned, and ENOMEM if `len > 1 GiB`.
2. Without `MAP_ANONYMOUS` (0x20), `fd` must refer to a RAM-file (`v->dev == 0 && fs_idx >= 0`); otherwise ENODEV.
3. Returns ENOMEM if `pages > pframe_free_count()`, even for lazy anonymous mappings.
4. With `MAP_FIXED` (0x10), `addr` must be page-aligned, inside the window and clear of the stack range. Whatever is mapped there first is removed with `lx_unmap_range`.
5. Otherwise `lx_find_gap(cr3, pages+1)`, which leaves one guard page, scans from `0x40004000000` up to `g_mmap_next`. A missing page table counts as free and skips to the next 2 MiB boundary; a used PTE resets the run. If no gap is found, the mapping goes at `g_mmap_next`, which is then raised past the new mapping.
6. Anonymous pages become `PTE_LAZY`. File pages get frames allocated eagerly and filled from `fsd(k)`, zero-padded past EOF. These are always private copies: `MAP_SHARED` writes never reach the file. If any step fails, the pages already mapped are released again.

`lx_unmap_range(a,e)` visits each page. A present page has its PTE cleared and `invlpg` run, and its frame is freed. A lazy page just has its PTE cleared. Page tables are not freed until `vm_free_user`. If other threads of the process exist, the frames are first collected (64 at a time) and `smp_tlb_sync(cr3)` runs before they are freed, so no other core can still be writing to a frame through a stale TLB entry (section 4.15).

`lx_brk(want)` returns the current break unless `want` lies in `[0x40002000000, 0x40004000000)`. To grow, it rounds up to a page, checks the free frame count, and refuses if any target page is already used, for example by a `MAP_FIXED` mapping. It then `vm_map_lazy`s each page and advances `g_ubrk`. To shrink, it calls `lx_unmap_range(top, g_ubrk)`. The exact requested value is kept in `p->lxbrk`. The native NoovexOS syscall 12 (`jobs.c`) instead maps frames eagerly, page by page, until it reaches the target or runs out of frames.

---

### 3.11 Page-fault path and OOM

```
#PF (vector 14) -> isr64.S -> isr_dispatch (bkl_lock) -> isr_body(r)
   +- vm_user_pf(r)                               core/proc/jobs.c
        +- !g_js_running || g_jcur<0     -> 0 (not handled)
        +- va = CR2; !jc_uok(va,1)        -> 0
        +- rc = vm_fault_user(CR3, va, errcode bit 1 = write)
             +- rc == 0  -> return 1: retry the instruction
             +- rc == -2 -> jc_finish(g_jcur, 9); jc_leave()   (killed: out of frames)
             +- rc == -1 -> 0
   not handled: vector < 32 && ring 3 -> jc_fault: SIGSEGV(11), or a pending signal if a handler is installed
                vector < 32 && ring 0 -> panic(r) (red screen)
```

```c
static int vm_fault_user(u64 cr3,u64 va,int write){
    u64* e=vm_walk(cr3&~0xFFFULL,va&~0xFFFULL,0); if(!e)return -1;
    if(!(*e&1)){ if(!(*e&PTE_LAZY))return -1;                       /* demand-zero */
        u64 f=pframe_alloc(); if(!f)return -2; *e=f|0x7ULL; invlpg; return 0; }
    if(!(*e&4))return -1;
    if(write&&!(*e&2)){ if(!(*e&PTE_COW))return -1;
        u64 pa=*e&PTE_ADDR;
        if(pframe_refs(pa)<=1){ *e=pa|0x7ULL; invlpg; return 0; }        /* last owner: just unprotect */
        u64 f=pframe_alloc(); if(!f)return -2; copy 4 KiB pa->f;
        *e=f|0x7ULL; invlpg; smp_tlb_sync(cr3); pframe_free(pa); return 0; } /* other threads must drop the old page first */
    return 0; }
```

The handler does not look at the faulting privilege level, so a kernel access through a user pointer with CR0.WP set is resolved in the same way. Faults outside the window or on unmapped kernel addresses still reach `panic`. OOM shows up in two forms:

- At fault time: the pool is empty, the process is finished with status 9 (SIGKILL), and the scheduler moves on.
- At syscall time: `mmap`, `brk` and `elf_build_space` return `-ENOMEM`, the old break, or failure.

No reclaim path frees frames for user memory. `fs_evict` (section 3.12) gives back heap memory only.

---

### 3.12 File-system memory (`fs/vfs/ramfs.c`)

Every file lives in `FS[FS_N = 1024]` (`FsFile`). Its contents are a single contiguous `kmalloc` buffer, `data`, holding `len` bytes plus a NUL, with `cap` the allocated size. Disk backends (NVX4 or the root file system) load contents on demand.

| Function | Rule |
|---|---|
| `fs_reserve(k, need)` | Returns at once if `cap > need`. Otherwise the new capacity is `cap * 1.5`, or 64 for an empty file, at least `need+1`, at most `FS_CAP+1` (1 GiB), rounded up to 64. If that allocation fails it retries with exactly `need+64`. It copies `min(len, nc-1)` bytes and frees the old buffer |
| `fs_alloc(k, n)` | `kmalloc`; on failure `fs_evict(n+4096, k)` and one retry |
| `fs_evict(want, keep)` | Two passes over all files. It skips `keep`, files with no data, lazy or dirty (`!clean`) files, files touched within the last 64 (pass 0) or 8 (pass 1) `fs_ensure` ticks, open files, and files that `fs_reloadable` (`fs/nvx/nvx4.c`) rejects: orphans, no disk, files in a tmp directory, or NVX4 files not fully stored. Evicted files have their buffer freed and `lazy = 1` |
| `fs_ensure(k)` | Bumps `tick` (LRU). If lazy, it reloads with `fs_backend_load` (`nk_root_read` or `n4_read_file`) and marks the file clean. If there is no data, it allocates a zero-filled buffer |
| `fsd(k)` | Returns `fs_ensure(k)` data, or the static 64 KiB `g_fs_zero` on failure |
| `fs_set` | Frees an oversized buffer (`cap > n + cap/2 + 64 KiB`) before writing |
| `fs_truncate_to(k, 0)` | Frees buffers larger than 64 KiB |
| `fs_dirty` / `fs_wb_mark` | Track the dirty byte range `[dlo, dhi)`; write-back is scheduled about 100 ticks later |

The file cache and every other `kmalloc` user share one heap. When an allocation through `fs_alloc` fails, clean, idle, reloadable files are dropped and re-read later. `lx_mmap` of a file goes through `fsd()` and copies the contents into frame-pool pages, so mapped files never pin heap buffers.

---

### 3.13 Summary of limits

| Resource | Limit | Where |
|---|---|---|
| Physical RAM recognised | all RAM in the firmware map; memory above 4 GiB is mapped and pooled up to 512 GiB (tested to 128 GiB) | `detect_ram_mb`, `mm_map_high`, `hp_init` |
| Firmware map entries | 170 (overflow flagged) | `MMR_MAX`, `MM_MAX` |
| Kernel image | 100 MiB (since v9.22; section 3.15) | `KERNEL_MAX`, linker `ASSERT`, `build.sh` |
| `.bss` | `0x10000000` - `0x18000000` (since v9.22) | `kernel64.ld` |
| DMA arena | 16 MiB, never freed | `DMA_ARENA_*` |
| Frame pool (all user memory and page tables) | 32 MiB plus half the free heap, at most 2 GiB more, plus all clean RAM above 4 GiB (up to 32 regions); needs at least 272 MiB RAM | `PF_*`, `pframe_grow_from_heap`, `hp_init` |
| Heap | at least 64-128 MiB; ends at most `0xFC000000 - 1 MiB` | `heap_init` |
| User ELF image | 32 MiB window | `ELF_IMG_LO/TOP` |
| User stack | up to 256 pages, 1 MiB (2 eager, the rest lazy) | `JS_STK_PAGES` |
| `brk` heap | 32 MiB | `lx_brk` |
| Single `mmap` | 1 GiB, and no more pages than free frames | `lx_mmap` |
| RAM file size | 1 GiB (`FS_CAP`), 1024 files | `fs/vfs/ramfs.c` |

### 3.14 Memory above 4 GiB

Before v9.14 the kernel identity-mapped only the first 4 GiB (`boot64.S`) and capped the RAM size at 4032 MiB. Three pieces now make the rest usable.

#### The memory map on BIOS boots

`mm_load` used to accept only the UEFI summary at `0x7000` (magic `NVXM`). BIOS boots now leave a second format there, magic `0x4558564E` ("NVXE"): a count at `0x7004` and up to 40 raw E820 entries (base, length, type, attributes; 24 bytes each) from `0x7008`.

| Boot path | Who writes it |
|---|---|
| myboot (the ISO and USB image) | `boot64.S`, Multiboot branch: copies `mmap_addr`/`mmap_length` from the Multiboot info when flag bit 6 is set |
| Native CD loader | `cdboot.S`, `e820_scan` (INT 15h, EAX=E820) |
| Native disk loader | `stage2.S`, `e820_scan` |

`mm_load` converts the E820 types to the UEFI numbering the rest of the code uses: 1 becomes 7 (usable), 3 becomes 9 (ACPI tables), 4 becomes 10 (ACPI NVS), 5 becomes 8 (unusable), everything else 0 (reserved).

`detect_ram_mb` now returns the sum of all usable regions (types 7 and 4) when a map is known, so `g_ram_mb`, `/proc/meminfo` and Settings show the real size. `heap_init` clamps the heap top to `0xFC000000` so a large RAM size never pushes the heap above 4 GiB.

#### Mapping: `mm_map_high` (`mem/pmm_vmm.c`)

For every 1 GiB slot from 4 GiB to 512 GiB whose PDPT entry is empty and that overlaps usable RAM, a page directory is taken from the heap (one 4 KiB page per slot, 64 KiB for 16 GiB, 512 KiB for 128 GiB). Each 2 MiB entry that overlaps usable RAM is mapped (`0x83`: present, writable, large page). Slots already in use (the framebuffer window, `acpi_hi_map`) are left alone. MMIO between RAM regions is not mapped.

#### The high frame pool (`hp_*`)

`hp_init` runs at the end of `pframe_init`. It collects the 2 MiB chunks above 4 GiB that are clean (type 7) and mapped into at most 32 ranges, then carves its bookkeeping from the start of the largest range: a bitmap (one bit per frame, in 64-bit words) and a 16-bit reference count per frame, about 2.1 bytes per 4 KiB frame (68 MiB for 128 GiB). Three frames (first, middle, last) are written and read back; if any check fails the pool is disabled.

| Function | Behaviour |
|---|---|
| `pframe_alloc` | high pool first (skips full 64-bit words with one compare), then the old pool below 4 GiB, which stays free for anything that needs low memory |
| `pf_idx`, `pf_pa` | indexes `pf_n` and above are high frames; `hp_pa` finds the range by binary search |
| `pframe_free`, `pframe_ref` | handle both pools, so copy-on-write fork works on high frames |
| `pframe_free_count` | both pools |
| `pframe_hi_count`, `mm_hi_mapped_top`, `mm_ram_total` | for diagnostics |

The boot log line `memory: N MB RAM, F free frames, H above 4 GB` shows the result.

Limits: one PDPT (512 GiB); at most 32 high ranges; the 16-bit reference count.

### 3.15 The 100 MiB kernel layout

Since v9.22 the low memory map is:

| Range | Use |
|---|---|
| 1 MiB - 101 MiB | kernel image (`ASSERT(__file_end <= 0x06500000)` in `kernel64.ld`, `assert len(b) <= 104857600` in `build.sh`) |
| 152 - 154 MiB | boot stack (top `0x09A00000`, `boot64.S`) |
| 154 - 156 MiB | fallback for the 2 MB device window (`DMW_FIXED`, `include/nvx/mem/dmawin.h`). Since v9.81 `dmw_init()` normally takes the window from the DMA arena instead; offsets inside it: xHCI rings 0x10000, HID areas 0x80000, USB stick 0xD0000, AHCI 0x180000, identify buffers 0x1A0000, VirtualBox request 0x1A1000, NVMe 0x1B0000, PCnet 0x1D0000 |
| 176 - 192 MiB | DMA arena (unchanged) |
| 192 - 224 MiB | frame pool (unchanged) |
| 224 - 256 MiB | module area for the Doom WAD (unchanged) |
| 256 - 384 MiB | kernel BSS (`. = 0x10000000`, at most `0x18000000`) |
| 384 MiB - | heap (`heap_init` starts at `0x18000000`) |

Loaders:

| Loader | Change |
|---|---|
| myboot stage2 (BIOS) | reads the kernel file into a scratch area at `0x18000000` (was `0x1000000`), up to 100 MiB, then copies it to 1 MiB |
| UEFI loader | `KERNEL_MAX` 100 MiB; the loader's own buffer is allocated outside the kernel areas (`alloc_safe`); the reserved-area table and the BSS bounds follow the new layout |
| CD image (`mkhybrid.py`) | the El Torito UEFI FAT16 image doubles its sectors per cluster until it has fewer than 65,000 clusters; when it is larger than 65,535 sectors the catalog entry's sector count is 0, which UEFI firmware reads as "to the end of the disc" |

The frame pool now needs 500 MiB of RAM, because the heap it grows from starts at 384 MiB.

## Part 4: CPU, interrupts, timers and multi-core

This part describes everything between the bare CPU and the scheduler: the inline CPU helpers, descriptor tables, the interrupt and system-call entry paths, the legacy PIC/PIT, the high-resolution timers, and the symmetric-multiprocessing (SMP) layer with its big kernel lock. All paths are relative to the repository root. Each `.c` file is its own compilation unit with an interface header under `include/nvx/` (Part 1, section 3); functions another unit calls are declared there, everything else stays `static`. The four assembly files (`boot64.S`, `isr64.S`, `userspace.S`, `ap_trampoline.S`) are separate objects as before.

Include order matters for this part. `arch/x86_64/cpu/cpu.h` and `arch/x86_64/cpu/percpu.c` come first (`kernel64.c` lines 25-26), so every later file can use `PCPU`, `cpu_id()` and the BKL. `ring3.c` and `smp.c` come early, `interrupts.c` and `core/panic.c` come in the middle, `hrtimer.c` comes late, and `smp_sched.c` and `cpu_init.c` come last, just before `core/init/main.c`.

---

### 4.1 `arch/x86_64/cpu/cpu.h`, the CPU interface

`cpu.h` is the only header portable code uses for privileged instructions. `tools/check.py` rejects inline assembly outside `arch/`, and `tools/tests/linux/cpu_host.h` provides host stand-ins for the Linux-hosted test build. Every helper is `static inline`.

| Helper | Instruction(s) | Notes / main users |
|---|---|---|
| `cpu_irq_on` / `cpu_irq_off` | `sti` / `cli` | memory clobber |
| `cpu_pause` | `pause` | spin loops (BKL, IPI busy-wait) |
| `cpu_halt` | `hlt` | main loop idle (between `bkl_drop`/`bkl_take`), panic |
| `cpu_idle` | `sti; hlt` | atomic "enable and wait": an IRQ cannot slip in between |
| `cpu_stop` | `cli; hlt` | parks a core for good (`ap_main`, `IPI_HALT`) |
| `cpu_wmb` | `sfence` | store barrier |
| `cpu_irq_save` | `pushfq; popq; cli` | returns RFLAGS |
| `cpu_irq_restore(f)` | `sti` if `f & 0x200` | restores IF only |
| `cpu_get_pt` / `cpu_set_pt` | `mov cr3` | writing CR3 flushes the TLB (no CR4.PGE is set anywhere, so there are no global pages) |
| `cpu_flush_page(v)` | `invlpg (v)` | `mem/pmm_vmm.c` `vm_map`/`vm_unmap`/COW, `sys/linux/linux.c` munmap |
| `cpu_cache_flush` | `wbinvd` | `apps/system/files_classic.c` |
| `cpu_tsc` | `rdtsc` | |
| `cpu_rand32` / `cpu_seed32` | `rdrand` / `rdseed` + `setc` | return CF; used by `net/http/http.c`, `nvxcrypto2.c` |
| `cpu_cpuid(leaf,sub,...)` | `cpuid` | |
| `cpu_sp` | `mov rsp` | |
| `cpu_fpu_save` / `cpu_fpu_restore` | `fxsave` / `fxrstor` | per-process `fx` area in `jc_enter` / `jc_leave` |
| `cpu_bios_ebda_seg` | `movw 0x40E` | BDA EBDA segment, `core/power.c` RSDP scan |
| `cpu_triple_fault` | `lidt` of a null IDT, then `int3` | last-resort reboot (`core/power.c`) |
| `cpu_rdmsr` / `cpu_wrmsr` | `rdmsr` / `wrmsr` | |
| `cpu_set_fsbase(v)` | `wrmsr 0xC0000100` (FS_BASE) | `arch_prctl(ARCH_SET_FS)` in `sys/linux/linux.c` case 158 |
| `cpu_percpu()` | `mov %gs:16, ...` | reads `pcpu.self` (see 4.9) |
| `cpu_set_percpu(p)` | `wrmsr 0xC0000101 = p`, `wrmsr 0xC0000102 = 0` | GS_BASE = per-CPU block, KERNEL_GS_BASE = 0 |
| `cpu_cr2` | `mov cr2` | page-fault address |
| `cpu_copy(d,s,n)` | `rep movsb` | `fs/vfs/ramfs.c` `kmemcpy` |

`cpu_set_percpu` puts the per-CPU pointer in the *active* GS base and zero in the *shadow* one. The first `swapgs` on the way to ring 3 swaps them, so user code always runs with GS base 0 and the kernel pointer waits in KERNEL_GS_BASE until the next entry.

---

### 4.2 `arch/x86_64/cpu/cpu_init.c`

#### `arch_fpu_init`

The BSP calls this early in `kmain` (`core/init/main.c`, after `percpu_init_bsp` and `mm_load`), and every AP calls it again in `ap_entry_c`.

```c
cr0 &= ~(1<<2);            /* EM=0: x87/SSE not emulated            */
cr0 |=  (1<<1)|(1<<16);    /* MP=1, WP=1 (supervisor honours R/O)   */
cr4 |=  (1<<9)|(1<<10);    /* OSFXSR, OSXMMEXCPT: SSE + fxsave on   */
g_ssse3 = (cpuid(1).ecx >> 9) & 1;
fninit; ldmxcsr 0x1F80;    /* all SSE exceptions masked             */
```

Bit 16 is CR0.WP (Write Protect), not NE. With WP set, kernel writes to read-only user pages also fault, which the copy-on-write logic in `mem/pmm_vmm.c` relies on. `g_ssse3` selects SIMD paths (`simd.h`). XSAVE and AVX are not enabled. Only `fxsave` state is managed.

#### `apic_virtual_wire`

This runs only on UEFI boots: `kmain` checks the boot-info magic `*(u32*)0x502C == 0x49464555` ("UEFI"). It reads IA32_APIC_BASE (MSR `0x1B`). If the APIC is globally disabled (bit 11 clear), it does nothing. Otherwise it programs the local APIC as a legacy "virtual wire", so 8259 interrupts still reach the BSP:

| Register | xAPIC MMIO offset | x2APIC MSR | Value |
|---|---|---|---|
| TPR | `0x80` | `0x808` | 0 |
| Spurious vector (SVR) | `0xF0` | `0x80F` | `0x1FF` (enable + vector 0xFF) |
| LVT LINT0 | `0x350` | `0x835` | `0x700` (ExtINT) |
| LVT LINT1 | `0x360` | `0x836` | `0x400` (NMI) |
| LVT Timer | `0x320` | `0x832` | OR `0x10000` (masked) |

If bit 10 (x2APIC) is set, the MSR path is used. Otherwise the MMIO base must lie below 4 GB.

#### `arch_idt_init`

```c
for(i=0;i<48;i++)   set_gate(i, isr_stubs[i]);   /* exceptions + PIC IRQs */
for(i=48;i<256;i++) set_gate(i, isr_ignore);     /* everything else      */
lidt {sizeof(idt)-1, idt}
```

Other gates are filled in later (see 4.4). There is one IDT, `static struct idt_entry idt[256]` in `interrupts.c`, and all cores share it. APs load it with `idt_load()` (`smp_sched.c`).

---

### 4.3 GDT, selectors, TSS and IST stacks (`arch/x86_64/user/ring3.c`)

At boot the kernel runs on the bootloader's GDT. `us_init()` installs the final GDT the first time anything needs ring 3. It is called from `ring3_run`, `elf.c`, `sched.c`, `jobs.c` (`jc_enter`), `sys/native/syscall.c`, the shell, and every AP's `ap_entry_c`. `us_init` returns early if `g_us_ready` is set. Because `g_us_ready`, `g_gdt`, `g_gdtp` and `g_tss` are all per-CPU macros (4.9), each core builds and loads its own GDT and TSS exactly once.

`gdt_set(i, base, limit, access, flags)` packs a legacy 8-byte descriptor into `g_gdt[i]`.

| Index | Selector | Access | Flags | Meaning |
|---|---|---|---|---|
| 0 | `0x00` |, |, | null |
| 1 | `0x08` | `0x9A` | `0xA` (G, L) | kernel code, 64-bit |
| 2 | `0x10` | `0x92` | `0xC` (G, D/B) | kernel data |
| 3 | `0x18` to `0x1B` with RPL 3 | `0xFA` | `0xA` | user code, DPL 3, 64-bit |
| 4 | `0x20` to `0x23` with RPL 3 | `0xF2` | `0xC` | user data, DPL 3 |
| 5-6 | `0x28` | `0x89` |, | 64-bit available TSS (16-byte descriptor; slot 6 holds base bits 63:32) |

`gdt_flush` (`userspace.S`) runs `lgdt`, reloads DS/ES/SS with `0x10`, and far-returns (`lretq`) through CS `0x08`. `tss_flush(0x28)` runs `ltr`.

The TSS is `struct tss64` in `percpu.h` (packed: `rsp0..2`, `ist[7]`, `iomap`). `us_init` zeroes it and sets these fields:

| Field | Value | Purpose |
|---|---|---|
| `rsp0` | top of `g_kstack` (per-CPU 16 KB) | ring 3 to 0 stack; `jc_enter` later points it at `g_jkstack` |
| `iomap` | `sizeof(g_tss)` | no I/O permission bitmap, so all ring-3 port I/O faults |
| `ist[0]` (IST1) | top of `g_ist_nmi` (4 KB) | NMI, vector 2 |
| `ist[1]` (IST2) | top of `g_ist_df` (8 KB) | double fault, vector 8 |

After that, `us_init` calls `idt_set128()`, `lsys_init()`, `idt_use_ist(2,1)` and `idt_use_ist(8,2)`. The IDT is shared, so the IST indices are global, but each core's TSS points them at its own stacks.

---

### 4.4 IDT gates and the vector map (`arch/x86_64/interrupts/interrupts.c`)

```c
struct idt_entry{ u16 lo; u16 sel; u8 ist; u8 attr; u16 mid; u32 hi; u32 z; };
static void set_gate(int n,void*h){ ... sel=0x08; ist=0; attr=0x8E; ... }  /* P, DPL0, interrupt gate */
static void idt_set128(void){ ... attr=0xEE; }                             /* P, DPL3, interrupt gate */
static void idt_use_ist(int n,int ist){ idt[n].ist=(u8)ist; }
```

Every gate is an *interrupt* gate (type `0xE`), so the CPU clears IF on entry. The only DPL-3 gate is `int 0x80`. A software `int n` from ring 3 to any other vector raises `#GP`.

| Vector(s) | Stub | Installed by | Handling |
|---|---|---|---|
| 0-31 | `isr0`-`isr31` | `arch_idt_init` | user-mode PF to `vm_user_pf`; other user faults to `jc_fault`; kernel faults to `panic` (RSOD) |
| 2 (NMI) | `isr2`, IST1 | `us_init` | `isr_dispatch` returns at once (ignored) |
| 8 (#DF) | `isr8`, IST2 | `us_init` | panic |
| 32-47 | `isr32`-`isr47` | `arch_idt_init` | PIC IRQ 0-15 |
| `0x40` | `isr_lapic_timer` | `kmain`: `set_gate(HR_LAPIC_VEC,...)` | BSP: `hr_lapic_isr`; AP: `ap_timer` |
| `0x80` | `isr128` (DPL 3) | `us_init` to `idt_set128` | legacy NoovexOS syscall ABI |
| `0xF0` | `isr_ipi_resched` | `smp_enable` | `IPI_RESCHED` |
| `0xF1` | `isr_ipi_tlb` | `smp_enable` | `IPI_TLB` |
| `0xF2` | `isr_ipi_halt` | `smp_enable` | `IPI_HALT` |
| all others | `isr_ignore` (pushes 255) | `arch_idt_init` | no EOI; also catches the APIC spurious vector 0xFF |

#### Stubs (`boot/isr64.S`)

`STUB_NOERR n` pushes a dummy error code 0 and then `n`. `STUB_ERR n` pushes only `n`, because the CPU already pushed an error code. The error-code vectors are 8, 10, 11, 12, 13, 14, 17 and 21. Vectors 29 (#VC) and 30 (#SX) are AMD-specific and push an error code on hardware, but this table uses `STUB_NOERR` for them, so their frame would be off by one slot. Neither vector is expected in practice. `isr_stubs[48]` is the `.rodata` table that `arch_idt_init` reads.

---

### 4.5 Interrupt frame and `isr_common`

```c
/* interrupts.c - "order must match the asm" */
typedef struct {
    u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;   /* pushed by isr_common */
    u64 vector,errcode;                                              /* pushed by the stub   */
    u64 rip,cs,rflags,rsp,ss;                                        /* pushed by the CPU    */
} regs_t;
```

| Offset | Field | Offset | Field |
|---|---|---|---|
| 0x00 | r15 | 0x60 | rcx |
| 0x08 | r14 | 0x68 | rbx |
| 0x10 | r13 | 0x70 | rax |
| 0x18 | r12 | 0x78 | vector |
| 0x20 | r11 | 0x80 | errcode |
| 0x28 | r10 | 0x88 | rip |
| 0x30 | r9 | 0x90 | cs |
| 0x38 | r8 | 0x98 | rflags |
| 0x40 | rbp | 0xA0 | rsp |
| 0x48 | rdi | 0xA8 | ss |
| 0x50 | rsi | | |
| 0x58 | rdx | | |

```
 CPU (ring3->0: loads TSS.rsp0, or IST1/IST2 for NMI/#DF)
   push ss, rsp, rflags, cs, rip [, errcode]
 stub:          [push 0] ; push vector ; jmp isr_common
 isr_common:    testb $3, 24(%rsp)      ; cs RPL at vec(0) err(8) rip(16) cs(24)
                jz 1f ; swapgs          ; only when coming from ring 3
             1: push rax..r15           ; regs_t complete, rdi = rsp
                mov rsp,rbp ; and $-16,rsp ; call isr_dispatch ; mov rbp,rsp
                pop r15..rax
                cli
                testb $3, 24(%rsp) ; jz 1f ; swapgs   ; only when returning to ring 3
             1: add $16,%rsp ; iretq
```

swapgs rule. GS is swapped only when the saved CS has RPL 3, on both entry and exit. The exit check reads the CS in the frame *after* `isr_dispatch`, and the scheduler may have rewritten that frame. So a frame changed to target another ring-3 context still gets the correct swap. Kernel-to-kernel interrupts never swap, so GS base always holds the `pcpu` pointer while kernel code runs.

`isr_common` does not run `cld`. The C code is built for SysV and expects DF clear, and DF is clear in every context this kernel creates.

---

### 4.6 `isr_dispatch` and `isr_body`

```c
void isr_dispatch(regs_t*r){
    if(r->vector==2)return;                                   /* NMI: ignore            */
    u64 v=r->vector; int user=(r->cs&3)==3;
    if(v>=0xF0&&v<=0xF2){ smp_ipi(r,user); return; }          /* IPIs: lock-free        */
    if(v==HR_LAPIC_VEC&&cpu_id()!=0){ ap_timer(r,user); return; }
    if(v==HR_LAPIC_VEC&&!user){ hr_lapic_isr(); return; }     /* BSP one-shot wake      */
    if(v==32)g_ticks++;                                       /* PIT tick, before lock  */
    bkl_lock(); isr_body(r); bkl_unlock();
}
```

`g_ticks` is incremented *before* the lock is taken, so the 100 Hz clock keeps counting while another core holds the BKL. IPIs and AP timer ticks never block on the lock up front. They take it only when they are about to preempt a user process.

`isr_body` (called with the BKL held):

1. Vector 14 and `vm_user_pf(r)` (`core/proc/jobs.c`). If a job is running, CR2 is in the user window and `vm_fault_user` resolves the fault (lazy allocation or COW), the handler returns and the instruction is retried. A result of `-2` (out of memory) kills the job with signal 9.
2. Other exceptions (< 32). If `g_js_running` is set and the fault came from ring 3, `jc_fault(r)` runs. It saves the frame into the job's `ctx` and maps the vector to a signal: 0/16/19 to SIGFPE (8), 6 to SIGILL (4), 1/3 to SIGTRAP (5), anything else to SIGSEGV (11). If the signal is unhandled or masked, the job is finished; otherwise the signal is marked pending. Then `jc_leave()` runs. Any kernel-mode exception calls `panic(r,0)`.
3. `HR_LAPIC_VEC` (BSP, user mode) to `hr_lapic_isr()`.
4. IRQ0 (PIT). If a job is in ring 3, `jc_timer(r)` runs. It preempts after two ticks (`g_ticks - g_js_slice >= 2`): it saves `ctx`, sends `pic_eoi(0)` and calls `jc_leave()`. The older demo scheduler (`g_preempt_on`, used by `preempt`/`ipc`/`compositor`) round-robins over `g_proc[]`. It copies the frame into `g_proc[cur].ctx`, sets `g_tss.rsp0`, loads the next CR3 and overwrites `*r`, so `isr_common` irets into the next process. If no process is left, it calls `k_longjmp(g_preempt_kern,1)`.
5. IRQ1 / IRQ12 to `ps2_byte()` (keyboard and mouse share one byte reader in `drivers/input/ps2/ps2.c`).
6. EOI. `pic_eoi(irq)` runs unless the vector is 255 (`isr_ignore`). EOI is sent to the slave for IRQ at least 8, then always to the master.

#### RSOD (`core/panic.c`)

`panic(regs_t*, const char*)` does the following in order:

1. Calls `smp_halt_others()`, which sends `IPI_HALT` to every other core.
2. Disables interrupts.
3. Guards against recursion with `static in_panic`. A fault during the panic just halts.
4. Fills `SCENE` with a red gradient and prints `:(`, the exception name from `EXC[32]`, the vector, error code, RIP, RSP, RFLAGS and all 16 GPRs. When called with `r==0` (or a non-exception), it prints `msg` instead.
5. Calls `blit_all()` and halts forever.

The shell `panic` command uses the message path.

---

### 4.7 PIC and PIT

```c
static void pic_remap(void){
    ICW1 0x11 -> 0x20/0xA0;  ICW2 0x20 / 0x28;  ICW3 0x04 / 0x02;  ICW4 0x01 / 0x01;
    outb(0x21,0xF8);   /* master: IRQ0 timer, IRQ1 kbd, IRQ2 cascade unmasked */
    outb(0xA1,0xEF);   /* slave:  IRQ12 PS/2 mouse only                       */
}
static void pit_init(u32 hz){ div=1193182/hz; outb(0x43,0x36); lo; hi; }  /* ch0, mode 3 */
```

`kmain` runs `pic_remap(); pit_init(100);`, so `g_ticks` advances at 100 Hz (10 ms per tick). `g_ticks` is a `volatile u64` defined in `net/http/http.c` and declared in `include/nvx/net/http/http.h`. Everything else (USB, NVMe, Wi-Fi) is polled, so only IRQ 0, 1, 2 and 12 are unmasked.

After `cpu_irq_on()`, `kmain` checks that ticks are actually arriving. If `g_ticks` does not move within 3,000,000 pauses, it clears `g_ticks_live` and the main loop calls `pit_poll_tick()` instead (`core/init/main.c`). That function latches channel 0 and counts reloads.

Boot order (`core/init/main.c`):

```
percpu_init_bsp -> arch_fpu_init -> ... -> arch_idt_init -> [UEFI] apic_virtual_wire
 -> pic_remap; pit_init(100) -> set_gate(0x40,isr_lapic_timer); hr_init()
 -> PS/2, xHCI, touchpad, ACPI AML ... -> cpu_irq_on -> tick check -> smp_boot_auto_c -> main loop
```

---

### 4.8 High-resolution timers (`arch/x86_64/time/hrtimer.c`)

All state lives in `static struct {...} g_hr`. `src` is the clock source (0 = PIT ticks, 1 = HPET, 2 = TSC). `ref` records what the calibration was measured against (0 nothing, 1 HPET, 2 PIT channel 2, 3 CPUID 0x15, 4 CPUID 0x16), and `timers_cmd` in `shell/commands.c` shows these names. `why[48]` holds the reason the HPET was rejected.

#### `hr_init` sequence

1. CPUID. `0x80000007` EDX bit 8 sets `tsc_inv` (invariant TSC). Leaf 1 EDX bit 4 sets `tsc_ok`.
2. `hr_hpet_init`. It finds `HPET` with `acpi_find_table`. It requires length at least 56 and an address space of 0 (memory), with the address at table offset 44 non-zero and 8-byte aligned, and maps it with `acpi_hi_map` (fails above 512 GB). It reads capabilities: the period in femtoseconds is `cap>>32` and must be between 1 and 10^8, `hpet_64` is bit 13, and `ntim` is bits 12:8 plus 1. It sets ENABLE_CNF (and clears LEG_RT) in config `+0x10` and checks that main counter `+0xF0` moves within 20,000 reads. Then `hpet_mult = (fs<<32)/10^6`. A 32-bit counter is extended to 64 bits in `hr_hpet_count` by detecting wraps (`hpet_hi += 2^32`) with interrupts disabled.
3. `hr_lapic_init`. It reads MSR `0x1B`. A globally disabled APIC is left alone. With x2APIC (bit 10), registers are reached as MSR `0x800 + off/16`. Otherwise MMIO at `g_lapic_base` is used, which must be below 4 GB. If the SVR enable bit is clear, it does the same virtual-wire setup as 4.2. It sets the timer LVT to `0x10000|0x40` (masked), divide `0x3E0 = 3` (/16), and initial count 0.
4. `hr_calibrate`. It takes the best of 3 runs with interrupts off. The local APIC counter starts at `0xFFFFFFFF`, and TSC and APIC current count (`0x390`) are sampled across a reference window:
   - with an HPET: wait until 20,000,000 ns of HPET time have passed;
   - without one: PIT channel 2 one-shot through port `0x61`/`0x42`, count `HR_PIT_COUNT = 59659`, which is `HR_PIT_WIN_NS = 49,999,930 ns` (59659 / 1,193,182 Hz). Wait for OUT2 (bit 5 of port `0x61`).

   The run with the smallest TSC-per-window wins. Then `tsc_hz = dt*10^9/win` and `lapic_hz = dl*10^9/win`, using `hr_muldiv`, which avoids overflow.
5. Fallback `hr_cpuid_tsc`. CPUID `0x15` gives `tsc_hz = ecx*ebx/eax`, and `lapic_hz = ecx/16` if it is still unknown. CPUID `0x16` gives `tsc_hz = eax[15:0] MHz`.
6. Accept. If `tsc_hz >= 10 MHz`, then `tsc_mult = (10^9<<32)/tsc_hz` and `tsc0 = rdtsc`. If `lapic_hz < 100 kHz`, the APIC timer is disabled.
7. Pick `src`. Invariant TSC first, then HPET, then a non-invariant TSC, then PIT ticks. The choice is logged ("timers: clock = ...").

#### API

| Function | Behaviour |
|---|---|
| `hr_ns()` | `src==2`: `((rdtsc-tsc0)*tsc_mult)>>32` (128-bit multiply); `src==1`: `hr_hpet_ns()`; else `g_ticks*10,000,000` |
| `hr_us()`, `hr_precise()` | `hr_ns()/1000`; `src!=0` |
| `hr_delay_us(us)` | busy-waits on `hr_ns()` with `pause`, bounded by an iteration limit; without a precise clock, `us` x `io_wait()` |
| `hr_delay_ms(ms)` | repeated `hr_delay_us(1000)` |
| `hr_wake_at(ns)` | arms an APIC one-shot for `min(ns-now, 1 s)` (`0x320 = 0x40`, unmasked); returns 1 if armed |
| `hr_wake_cancel()` | count 0, mask the LVT |
| `hr_lapic_periodic(hz)` | periodic mode (`bit 17`); defined but currently unused (APs program their own) |
| `hr_lapic_isr()` | `lapic_irqs++`, EOI (`0xB0`) |

The main loop uses `hr_wake_at` on the desktop when the cursor moved. It arms a wake for the next 60 Hz frame (`frame_ns = 16,666,667`), then runs `bkl_drop(); cpu_halt(); bkl_take()`, so the BSP sleeps until either the PIT tick or the precise wake. The `timers` and `timertest` shell commands report and cross-check the sources.

`hr_ns` uses the TSC base of the BSP (`tsc0`). Code on APs that calls it (`ap_udelay`, `hr_delay_us` in `smp_disable`) assumes the TSCs are synchronised. That holds on invariant-TSC hardware.

---

### 4.9 Per-CPU state (`arch/x86_64/cpu/percpu.c`)

```c
#define NCPU 16
struct pcpu {
    u64 kstack_top;          /* +0   syscall stack (lsys_entry: %gs:0)          */
    u64 user_rsp;            /* +8   user rsp scratch (lsys_entry: %gs:8)       */
    struct pcpu* self;       /* +16  read by cpu_percpu(): %gs:16               */
    u64 fsbase;              /* +24  user FS base (jc_resume: %gs:24)           */
    int id; u32 apic_id; int jcur; volatile int js_running;
    u64 js_kern[8]; u64 js_slice;
    int dcwd, uid, gid, umask; u64 ubrk, mmap_next; int us_ready;
    volatile int online, stop, parked, tlb_pending, in_user;
    volatile u64 ticks, user_ticks, idle_ticks, switches, syscalls, ipis;
    struct tss64 tss; u64 gdt[7]; struct{u16 limit; u64 base;} gdtp;
    u8 kstack[16384], jkstack[16384], ist_nmi[4096], ist_df[8192];
};
static struct pcpu g_cpu[NCPU] __attribute__((aligned(64)));
#define PCPU ((struct pcpu*)cpu_percpu())
static inline int cpu_id(void){ return PCPU->id; }
```

`_Static_assert`s fix the first four offsets (0, 8, 16, 24), because the assembly addresses them directly. `PCPU` is one `mov %gs:16` instruction. GS base points at the struct, and `self` points back at it.

The old global names are kept as macros over `PCPU->...`, so pre-SMP code became per-CPU without being rewritten:

| Macro | Field | Macro | Field |
|---|---|---|---|
| `g_jcur` | `jcur` (running job, -1 none) | `g_us_ready` | `us_ready` |
| `g_js_running` | `js_running` | `lsys_kstack` | `kstack_top` |
| `g_js_kern` | `js_kern` (setjmp buffer) | `lsys_fsbase` | `fsbase` |
| `g_js_slice` | `js_slice` | `g_tss` | `tss` |
| `g_dcwd`, `g_uid`, `g_gid`, `g_umask` | current credentials and cwd | `g_gdt`, `g_gdtp` | `gdt`, `gdtp` |
| `g_ubrk`, `g_mmap_next` | brk and mmap cursor | `g_kstack`, `g_jkstack` | stacks |
| | | `g_ist_nmi`, `g_ist_df` | IST stacks |

`percpu_setup(i, apic)` sets `self`, `id`, `apic_id`, `jcur=-1`, `umask=022`, `ubrk=0x40002000000`, `mmap_next=0x40004000000` and `kstack_top`. `percpu_init_bsp()` sets up slot 0 with APIC ID 0 (corrected later in `smp_start_aps`), loads GS and marks it online. It is the first statement after BSS clearing in `kmain`, so `PCPU` is valid everywhere afterwards.

---

### 4.10 The big kernel lock (`arch/x86_64/cpu/percpu.c`)

```c
struct bkl { volatile u32 next, serving; volatile int owner; volatile int depth; };
static struct bkl g_bkl={1,0,0,1};       /* CPU 0 owns it from boot, depth 1 */
```

- Ticket lock. `bkl_lock` takes a ticket with `__atomic_fetch_add(&next,1)` and spins (`pause`) until `serving == ticket` (acquire). Tickets make it FIFO-fair.
- Recursion. If `owner == cpu_id()`, it only increments `depth`. Nested entry is common, for example a syscall that ends up in code that locks again.
- Interrupts while spinning. `cpu_irq_save` disables interrupts during the spin and `cpu_irq_restore` restores the previous IF afterwards.
- Deferred TLB flush. After acquiring, if `PCPU->tlb_pending` is set, it clears it and reloads CR3.
- Unlock. `bkl_unlock` ignores callers that are not the owner, decrements `depth`, and at zero sets `owner=-1` and does `serving+1` (release).
- Helpers. `bkl_held()` tells whether this CPU owns it. `bkl_drop()` releases fully and returns the old depth, and `bkl_take(d)` re-acquires and restores that depth. Together they hand the lock over around blocking waits. `bkl_yield()` (only when SMP is on, the lock is held at depth 1, and someone is queued, i.e. `next != serving+1`) unlocks, pauses 64 times, and relocks.

Where it is taken or dropped.

| Site | Action |
|---|---|
| `isr_dispatch` | around `isr_body` (all PIC IRQs, exceptions, BSP-user LAPIC) |
| `lsys_c`, `isr128_c` | around `linux_syscall` / `syscall_dispatch` (also `PCPU->syscalls++`) |
| `ap_loop` | around `jc_pick` + `jc_enter` |
| `smp_ipi` (RESCHED), `ap_timer` | `bkl_lock()` just before `jc_preempt` (released by the `bkl_take` in `jc_enter`) |
| `jc_enter` (`jobs.c`) | `bd=bkl_drop()` right before `jc_resume`; `bkl_take(bd)` after the longjmp back |
| `kmain` main loop | `bkl_drop(); cpu_halt(); bkl_take()` when idle; `bkl_yield()` once per iteration |
| `smp_enable` / `smp_disable` | dropped around `smp_start_aps` and the 5 ms park waits |

The model: user code runs without the lock, kernel code runs with it. A job enters ring 3 with the BKL released. The next kernel entry on that core (interrupt, `syscall`, `int 0x80`, fault) acquires it. When a job leaves (`jc_leave` to `k_longjmp(g_js_kern)`), the code is still inside the handler that took the lock. `bkl_take(bd)` then finds `owner == me`, increments, and overwrites `depth` with the saved `bd`, so the nesting count from before `jc_enter` is restored exactly.

---

### 4.11 System calls: `int 0x80` and `syscall`

#### MSRs (`lsys_init`, run per CPU from `us_init`)

| MSR | Address | Value | Meaning |
|---|---|---|---|
| EFER | `0xC0000080` | `|= 1` | SCE: enable `syscall`/`sysret` |
| STAR | `0xC0000081` | `(0x08<<32) | (0x10<<48)` | SYSCALL CS=`0x08`, SS=`0x10` |
| LSTAR | `0xC0000082` | `lsys_entry` | 64-bit entry point |
| FMASK | `0xC0000084` | `0x47700` | clears TF, IF, DF, IOPL, NT, AC on entry |
| FS_BASE | `0xC0000100` | per job | `jc_resume`, `cpu_set_fsbase` |
| GS_BASE / KERNEL_GS_BASE | `0xC0000101/2` | `pcpu` / 0 | `cpu_set_percpu` |

`lsys_init` also sets `lsys_kstack = top of g_kstack`. STAR[63:48] = `0x10` would make `sysretq` load CS = `0x23` and SS = `0x1B`, which is the wrong way round for this GDT. That does not matter because the kernel never runs `sysret`: both syscall paths return with `iretq`.

#### `lsys_entry` (Linux ABI, `userspace.S`)

```
syscall:  rcx=user rip, r11=user rflags, CS/SS from STAR, IF=0 (FMASK)
lsys_entry:
   swapgs                        ; GS -> pcpu
   mov %rsp,%gs:8                ; pcpu.user_rsp
   mov %gs:0,%rsp                ; pcpu.kstack_top (= jkstack top while a job runs)
   push $0x23 ; push %gs:8 ; push %r11 ; push $0x1B ; push %rcx   ; fake iret frame
   push rax..r15                 ; struct sysregs
   mov %rsp,%rdi ; cld ; call lsys_c        ; bkl_lock; linux_syscall; bkl_unlock
   pop r15..rax ; cli ; swapgs ; iretq
```

The fake frame is the same shape `isr128` produces: `struct sysregs` (15 GPRs, same order as the top of `regs_t`) followed directly by the 5-word iret frame. Code that blocks, forks or delivers signals can therefore treat both entry types the same way. 20 pushes on a 16-aligned stack keep the call aligned.

#### `isr128` (legacy NoovexOS ABI, number in `rax`)

`isr128` checks `testb $3, 8(%rsp)` (CS sits at +8 because there is no vector or error code) and swaps GS when coming from ring 3. It pushes the 15 GPRs and calls `isr128_c` to `syscall_dispatch`. On the way out it pops, runs `cli`, swaps GS back if returning to ring 3, and does `iretq`. Because the gate is an interrupt gate, IF is already 0.

#### Entering and resuming ring 3

| Routine | Use |
|---|---|
| `enter_ring3(rip,rsp)` | `cli`, DS/ES=`0x23`, push SS `0x23`, RSP, RFLAGS `0x202`, CS `0x1B`, RIP; `swapgs; iretq` (`ring3_run` demo) |
| `enter_ring3_args(rip,rsp,argc,argv)` | same, with `rdi=argc`, `rsi=argv` |
| `user_trampoline` / `user_trampoline_fork` | first run of a `sched_swap`-based process (fork variant zeroes `rax`) |
| `jc_resume(regs_t* ctx)` | job-control resume (below) |
| `k_setjmp` / `k_longjmp` | save/restore rbx, rbp, r12-r15, rsp, return address; `longjmp(...,0)` returns 1 |
| `sched_swap(save,load)` | cooperative kernel-stack switch |

```
jc_resume(ctx):  cli ; DS=ES=FS=0x23
                 FS_BASE <- %gs:24 (pcpu.fsbase)    ; still kernel GS here
                 rsp <- ctx ; pop r15..rax ; add $16 (vector,errcode)
                 swapgs ; iretq                    ; iret frame = ctx->rip..ss
```

`jc_enter` (`core/proc/jobs.c`) prepares a resume this way:

1. Points `g_tss.rsp0` and `lsys_kstack` at the top of `g_jkstack`, sets `lsys_fsbase`, `g_js_slice`, `p->runcpu = cpu_id()+1` and `switches++`.
2. Disables interrupts and calls `k_setjmp(g_js_kern)`.
3. On the first return: `fxrstor`, load the job's CR3, `bkl_drop`, `jc_resume`.

Any later kernel entry that decides to leave the job calls `jc_leave()`. It saves FPU state (`fxsave`), switches back to the kernel CR3 and longjmps to `g_js_kern`.

---

### 4.12 Multi-core discovery (`arch/x86_64/smp/smp.c`)

- `acpi_count_cores()` is the older, read-only scanner. It searches `0xE0000-0xFFFFF` for `"RSD PTR "`, follows the XSDT (revision at least 2) or the RSDT (both must be below `0xF0000000`), finds `APIC`, and counts MADT type-0 entries with the enabled flag. It cannot see a UEFI RSDP outside the BIOS area. The `cores`/`cpu` shell command uses it.
- `madt_apic_ids(out,max)` is used for real startup. It reads the MADT through `acpi_find_table("APIC",0)`, which also works on UEFI, and walks the entries from offset 44 with bounds checks (`el>=2`, `p+el<=len`). It collects the APIC IDs of enabled type-0 (Local APIC, `id=m[p+3]`) and type-9 (Local x2APIC, 32-bit ID at `+4`, flags at `+8`) entries. x2APIC IDs at least 255 and duplicates are skipped.

#### `smp_start_aps()`

1. `nc = madt_apic_ids(ids, NCPU)`. If `nc <= 1`, there is nothing to do.
2. Read IA32_APIC_BASE. `g_lapic_x2` = bit 10. The base is sanity-checked, with `0xFEE00000` as the default. The APIC is globally enabled if needed, and SVR bit 8 is set.
3. Copy `ap_tramp_start..ap_tramp_end` to physical `0x8000`. The code is in section `.aptramp`, which the linker scripts do not name, so it is placed as an orphan section and copied at runtime.
4. Write the parameter block (all physical, identity-mapped):

| Address | Size | Content | Written by |
|---|---|---|---|
| `0x8FD8` | int | logical CPU index `next` | per AP, before INIT |
| `0x8FE0` | u64 | stack top `g_ap_stacks[next]+16384` | per AP |
| `0x8FF0` | u64 | BSP CR3 (shared kernel page tables) | once |
| `0x8FF8` | u64 | `ap_main` address | once |

5. Record the BSP APIC ID in `g_cpu[0].apic_id`.
6. For each other ID: `percpu_setup(next, id)`, `g_ncpu = next+1`, then the INIT-SIPI-SIPI sequence:

```
ICR = 0x4500 (INIT, assert)          ; wait 10 ms
ICR = 0x4608 (Start-up, vector 0x08) ; wait 200 us     -> CS:IP = 0x0800:0000 = 0x8000
ICR = 0x4608                         ; second SIPI
poll g_cores_online for up to 2000 x 100 us (200 ms)
```

   In x2APIC mode this is one 64-bit `wrmsr 0x830` with the destination in bits 63:32. In xAPIC mode it writes `0x310` (destination << 24) and then `0x300`. The `lapic_wr` wrapper also emulates the xAPIC ICR pair on x2APIC: a write to `0x310` is buffered in `g_lapic_icr_dest`, and a write to `0x300` becomes one `0x830` MSR write. `lapic_rd(0x20)` shifts the x2APIC ID left by 24 so callers can treat it like xAPIC. If the core does not answer, `g_ncpu` is rolled back and that slot is reused. Delays come from `ap_udelay`, which uses `hr_delay_us` when a precise clock exists and a calibrated-free pause loop otherwise.

`smp.c` also contains an older lock-protected compute queue (`g_jobs[NJOBS=16]`, `smp_lock` test-and-set spinlock, `smp_submit`, `smp_work_step`, `smp_compute` counting primes). The `smpwork`/`parallel` and `smpsched` commands use it to show isolated parallel work and per-core counters (`g_core_load`, `g_core_cycles`).

---

### 4.13 AP trampoline (`arch/x86_64/boot/ap_trampoline.S`)

```
0x8000 .code16  cli; cld; DS=ES=SS=0
                lgdtl %cs:gdtptr              ; local 5-entry GDT
                CR0.PE=1 ; ljmpl $0x08, pm32
       .code32  DS=ES=SS=0x10
                CR4 |= 0x620                  ; PAE | OSFXSR | OSXMMEXCPT
                CR3  = [0x8FF0]               ; BSP page tables (low 32 bits)
                EFER |= 0x100                 ; LME
                CR0 &= ~0xC (EM,TS) ; CR0 |= 0x80010002 (PG | WP | MP)
                ljmpl $0x18, lm64
       .code64  DS=ES=SS=0x20 ; rsp=[0x8FE0] ; rbp=0
                call *[0x8FF8]                ; ap_main
                1: cli; hlt; jmp 1b
```

The trampoline GDT has null, `0x08` 32-bit code, `0x10` 32-bit data, `0x18` 64-bit code and `0x20` 64-bit data. These selectors differ from the kernel GDT, and `us_init` replaces them shortly afterwards. CR3 is loaded with a 32-bit `movl`, so the BSP's PML4 must be below 4 GB. EFER.NXE is not set on any core, which matches the kernel's page tables (no NX bits are used).

`ap_main()` reads its index from `0x8FD8`, calls `ap_entry_c(id)`, and parks with `cpu_stop()` if that ever returns.

---

### 4.14 AP runtime (`arch/x86_64/smp/smp_sched.c`)

#### `ap_entry_c(id)`

```c
cpu_set_percpu(&g_cpu[id]);              /* GS -> own pcpu                         */
arch_fpu_init();  idt_load();            /* shared IDT                             */
APIC_BASE |= enable (+x2APIC bit if the BSP uses x2APIC)
lapic: TPR=0, SVR=0x1FF, LINT0=LINT1=masked (0x10000)  /* only the BSP takes ExtINT/NMI */
us_init();                               /* own GDT, TSS(rsp0, IST1/2), syscall MSRs */
LVT timer = 0x40 | periodic, divide 16, count = g_hr.lapic_hz/100   /* 100 Hz tick */
c->online=1; g_cores_online++ (atomic);  sti;  ap_loop();
```

The APs reuse the BSP's `lapic_hz` measurement. All cores are assumed to have the same APIC timer clock.

#### `ap_loop`

```c
for(;;){
  if(c->stop){ c->parked=1; while(c->stop){ cpu_idle(); cpu_irq_off(); } c->parked=0; }
  int ran=0;
  if(g_smp_sched){ bkl_lock(); int pick=g_smp_sched?jc_pick():-1;
                   if(pick>=0){ jc_enter(pick); ran=1; } bkl_unlock(); }
  if(!ran && !smp_work_step()){ cpu_idle(); cpu_irq_off(); }
}
```

An idle AP sleeps in `sti; hlt` and wakes on its own 100 Hz timer or an IPI. `jc_runnable` refuses a job whose `runcpu` is non-zero, so each job runs on at most one core at a time.

#### Timer and IPIs

| Vector | Name | Sender | Handler (`smp_ipi`, no BKL up front) |
|---|---|---|---|
| `0xF0` | `IPI_RESCHED` | `smp_kick(cpu)` (signal to a job running elsewhere, `jobs.c`), `ipi_others` in `smp_enable`/`smp_disable`, and `smp_kick_idle()` at the end of `jc_create` and `jc_fork`, which wakes every idle online core so a new process starts within microseconds instead of at the next 10 ms timer tick | EOI; if the core is in user mode running a job: `bkl_lock(); jc_preempt(r)` (sends the job back to the scheduler so the signal or stop is seen); otherwise just wakes the `hlt` |
| `0xF1` | `IPI_TLB` | `smp_tlb_shootdown()`, called by `acpi_hi_map` after it installs a new page directory for MMIO above 4 GiB | `tlb_pending=0`, reload CR3 |
| `0xF2` | `IPI_HALT` | `smp_halt_others()` from `panic` | `parked=1`, then `cli; hlt` forever |
| `0x40` | APIC timer | own LVT | `ap_timer`: `ticks++`, `user_ticks` or `idle_ticks` (idle means no job and no BKL), EOI; if user and `ticks - js_slice >= 2` to `bkl_lock(); jc_preempt(r)` |

`ipi_send` skips offline cores. In x2APIC mode it is a single `wrmsr 0x830`. In xAPIC mode it waits for the ICR delivery-status bit 12 to clear (up to 200,000 pauses) and then writes `0x310`/`0x300`. `lapic_eoi()` writes `0xB0`.

On APs the time slice is counted in the core's own `ticks` (`jc_now()` returns `PCPU->ticks` off-BSP and `g_ticks` on CPU 0). Both run at 100 Hz, so a slice is about 20 ms on every core.

#### `smp_enable` / `smp_disable`

- `smp_enable` returns early (with the CPU count) if SMP is already on. It returns `-1` without a calibrated APIC timer (`g_hr.lapic_ok && lapic_hz`). It installs gates `0xF0`-`0xF2`, starts the APs if they are not already up, and returns `-2` if none came online. Otherwise it clears `stop` on every AP, sets `g_smp_on = g_smp_sched = 1` and sends RESCHED to the others. APs started once are never restarted. Later `smp on` calls only unpark them.
- `smp_disable` clears `g_smp_sched`, sets `stop=1` on every online AP and sends RESCHED. It then polls `parked` for up to 400 x 5 ms (2 s), dropping the BKL during each wait so a core finishing a syscall can leave. It returns `-1` if some core did not park, and otherwise clears `g_smp_on`.

#### `smp_boot_auto` and the `/etc/smp` hang guard

`kmain` calls `smp_boot_auto_c()` just before the main loop, after the integrity check (section 2.12) has had its chance to stop the boot. (The old comment in `core/init/main.c` saying the cores start only on demand was removed in v8.95.)

```
/etc/smp == "off"       -> stay single-core
/etc/smp == "starting"  -> previous start never finished: write "off", log + notify, stay single-core
madt_apic_ids <= 1 && acpi_count_cores <= 1 -> nothing to start
write "starting" (smp_pref_set syncs the filesystem with nvx3_sync_all)
smp_enable()
write "on"
```

If the machine hangs during INIT/SIPI, the next boot reads "starting" and falls back to one core. `smp_pref_set` does nothing without a disk (`disk_ok`).

---

### 4.15 TLB coherence

Coherence relies on these properties in the current design:

- One kernel address space. APs load the BSP's CR3 (`0x8FF0`). Kernel mappings are set up before SMP starts and are not changed per core.
- Per-thread scheduling. `jc_runnable` requires `runcpu == 0`, so one thread never runs on two cores. `jc_enter` loads `p->cr3` and `jc_leave` loads the kernel CR3. With no CR4.PGE, every CR3 write flushes the whole TLB, so no stale user translation survives a switch to another core.
- Threads share page tables. Since v8.97 the threads of one process (section 5.15) have the same `cr3` and may run on several cores at the same time. `jc_enter` records the running address space in `PCPU->ucr3` while the thread is in user mode.
- Local invalidation. `vm_map`, `vm_unmap`, the COW fault path and `munmap` change the current address space and invalidate locally with `invlpg`.
- Shootdown mechanism. `smp_tlb_shootdown()` sets `tlb_pending` on every other online core and sends `IPI_TLB`; the receiver reloads CR3 in `smp_ipi`. `acpi_hi_map` calls it after adding a kernel-wide mapping for high MMIO.
- Synchronous shootdown for shared address spaces. `smp_tlb_sync(cr3)` sends `IPI_TLB` only to cores whose `ucr3` is that address space and then waits (a bounded spin) until each has cleared `tlb_pending` or stopped running it. It is called where a page that another thread may hold in its TLB is taken away or changes: the COW copy in `vm_fault_user` (before the old frame is released), `vm_clone_user` when it write-protects the parent's pages, and `lx_unmap_range` when the process has other threads (before the frames are freed). `linux_syscall` also clears a pending flag on entry, so a core that took the system call with interrupts off cannot use a stale entry.

---

### 4.16 The `smp` shell commands (`shell/commands.c`)

| Command | Effect |
|---|---|
| `smp` | Prints cores in the system (MADT, falling back to the legacy scan), cores online and the scheduling mode, then one row per online CPU: `cpu`, `apic`, state (`boot` / `parked` / `running` / `idle`), `switches`, `syscalls`, `user-ticks`, `idle-ticks`, `ipis` (from `struct pcpu`). |
| `smp on` / `smpstart` | Writes "starting", runs `smp_enable()`, writes "on". It reports "no calibrated local APIC timer" for `-1`, "no other cores came online" for at most 1, or "N cores schedule programs (big kernel lock, preemptive user mode)". |
| `smp off` | Runs `smp_disable()`. On success it writes "off" and reports "programs run on core 0 only"; on timeout, "some cores did not stop in time". |
| `cores` / `cpu` | Legacy MADT count and `g_cores_online`. |
| `smpwork` / `parallel`, `smpsched` | Prime-counting job queue demo and per-core counters (4.12). |

Other places that report the core count: `/proc/cpuinfo` (`fs/vfs/vfs.c`, one entry per online `g_cpu[]`), `/sys/devices/system/cpu/online` (`fs/vfs/unixfs.c`), and Linux `sched_getaffinity` (syscall 204 in `sys/linux/linux.c`). All of them report `g_cores_online` only while `g_smp_on` is set, and 1 otherwise. The user program `usr/musl/smptest.c` (named in the `smp` help line) forks workers, records which CPUs they ran on, and checks results as a stress test.

## Part 5: Processes, scheduling, signals and the native system-call ABI

This part covers how NoovexOS 64 runs user programs: the process table and its life cycle, the setjmp/longjmp scheduler, signals, job control on the terminal, pipes and file descriptors, the ELF loader, the program table, and the native `int 0x80` system-call ABI. Most of the code is in `core/proc/jobs.c` (the "job control" or `jc_*` layer). `core/proc/elf.c` loads programs, `sys/native/syscall.c` holds the legacy dispatcher, and `core/proc/sched.c` holds the older demo schedulers. Each of these files is its own compilation unit; they share functions through `include/nvx/core/proc/jobs.h`, `elf.h`, `sched.h` and `include/nvx/sys/native/syscall.h`. Every path below is relative to the repository root.

There are two process systems in the tree, and they should not be confused:

| System | Table | Where | Status |
|---|---|---|---|
| Job-control processes | `struct jproc g_jp[JMAX]` (256 slots, one per thread) | `core/proc/jobs.c` | The live system. It runs the shell's programs, init, the Linux ABI and SMP scheduling. |
| Legacy staged processes | `struct proc g_proc[MAXPROC]` (6 slots) | `core/proc/sched.c`, `sys/native/syscall.c` | Older demos: a cooperative scheduler, a preemption demo, IPC, a compositor demo and threads. |

`syscall_dispatch` (`sys/native/syscall.c`) chooses between them on every `int 0x80`. If a job process is running on this CPU (`g_js_running && g_jcur>=0`), the call goes to `jc_syscall` first. A call that `jc_syscall` does not handle falls through to the legacy `switch`.

---

### 5.1 Per-CPU scheduling state

Several names in `jobs.c` that look like globals are really macros for fields of the current CPU's `struct pcpu` (`arch/x86_64/cpu/percpu.c`). The `%gs` base points at that struct, and `PCPU` is defined as `((struct pcpu*)cpu_percpu())`.

| Macro | Field | Meaning |
|---|---|---|
| `g_jcur` | `PCPU->jcur` | Index into `g_jp` of the process running on this CPU, or `-1` |
| `g_js_running` | `PCPU->js_running` | 1 while this CPU is inside a job process |
| `g_js_kern` | `PCPU->js_kern[8]` | The `k_setjmp` buffer that `jc_leave` jumps back to |
| `g_js_slice` | `PCPU->js_slice` | Tick count when the current time slice started |
| `g_dcwd`, `g_uid`, `g_gid`, `g_umask` | same names | Working directory and credentials for the current context |
| `g_ubrk`, `g_mmap_next` | `ubrk`, `mmap_next` | Heap break and next anonymous-mmap address |
| `g_jkstack` | `PCPU->jkstack[16384]` | Kernel stack used while a job process runs (`TSS.rsp0`) |
| `lsys_kstack`, `lsys_fsbase` | `kstack_top`, `fsbase` | Kernel stack for the `syscall` entry, and the FS base `jc_resume` restores |

`percpu_setup` sets the defaults: `jcur=-1`, `umask=022`, `ubrk=0x40002000000`, `mmap_next=0x40004000000`.

---

### 5.2 The process table: `struct jproc`

```c
#define JMAX 256
#define S_FREE 0
#define S_RUN 1
#define S_WAIT 2
#define S_SLEEP 3
#define S_STOP 4
#define S_ZOMB 5
#define S_TDEAD 6
struct jproc {
    int state, pid, ppid, pgid, sid, uid, gid, cwd, umask;
    u64 cr3, brk, mmnext, wake;
    regs_pt ctx;
    int fd[FDMAX]; u8 cloexec[FDMAX];
    u32 sigpend, sigmask, sigamask[32], saflags[32]; u64 sighand[32], sigrest[32];
    u64 fsbase, pollwake, lxbrk; u32 suspold; int susp, lx;
    int exitst, stopnote;
    volatile int runcpu; u64 cputicks; int lastcpu;
    u64 it_next, it_int;
    int tg, tgid, gexit, tkill;
    u64 ctid, robust, fx_addr, fx_dead; int fx_wait, fx_woken, fx_tg; u32 fx_bits;
    char cmd[48];
    u8 fx[512] __attribute__((aligned(16)));
};
struct jproc g_jp[JMAX];
int g_next_pid=2, g_js_dirty=0;
```

#### Field reference

| Field | Type | Meaning, and where it is set or used |
|---|---|---|
| `state` | int | One of `S_FREE..S_TDEAD` (see 5.3) |
| `pid` | int | For a thread this is its thread ID (`gettid`). Taken from `g_next_pid++`. It starts at 2 because PID 1 is reserved for init, and `jc_start_init` writes `pid=1` after creating init |
| `ppid` | int | Parent's PID. `0` means the kernel shell, nsh. `jc_finish` changes the `ppid` of orphans to 1 (or to 0 if init is not alive) |
| `pgid` | int | Process group. `jc_create` uses the given group, or the new PID if none is given. Can change through `setpgid` (150) and `setsid` (152) |
| `sid` | int | Session. Always `1` at creation. Only session 1 owns the terminal: `jc_tty_read`, `tcgetpgrp` and `tcsetpgrp` check `sid==1` |
| `uid`, `gid`, `umask` | int | Credentials, copied from the per-CPU `g_uid/g_gid/g_umask` when the process is created or forked |
| `cwd` | int | Directory-tree node index (the `DT[]` index, same as `g_dcwd`) |
| `cr3` | u64 | Physical address of the process's PML4. User memory lives in PML4 slot 8 (`0x40000000000`-`0x7FFFFFFFFFF`, see `jc_uok`) |
| `brk`, `mmnext` | u64 | Heap break (`0x40002000000` at start) and next mmap address (`0x40004000000`). `jc_enter` copies them into `g_ubrk/g_mmap_next`, and they are written back when the process leaves the CPU |
| `wake` | u64 | Tick deadline when the state is `S_WAIT` or `S_SLEEP` |
| `ctx` | `regs_pt` | Saved user registers in the interrupt-frame layout: 15 general-purpose registers, then `vector`, `errcode`, `rip`, `cs`, `rflags`, `rsp`, `ss` (`sched.c`) |
| `fd[FDMAX]` | int[256] | Descriptor table. Each entry is an index into the global `g_vf[]`, or `-1`. Only the thread-group leader's table is used; `fdt()` returns `g_jp[g_jp[g_jcur].tg].fd` |
| `cloexec[FDMAX]` | u8[256] | Close-on-exec flag for each descriptor (leader's copy, through `fdce()`) |
| `sigpend` | u32 | Bitmap of pending signals (bit *n* = signal *n*) |
| `sigmask` | u32 | Bitmap of blocked signals. Bits 9 and 19 are always cleared |
| `sighand[32]` | u64 | `0` = `SIG_DFL`, `1` = `SIG_IGN`, anything above 1 = handler address |
| `sigrest[32]` | u64 | Restorer (trampoline) address pushed as the handler's return address |
| `sigamask[32]` | u32 | Extra mask applied while the handler runs (`sa_mask`) |
| `saflags[32]` | u32 | `sa_flags`, written only by the Linux `rt_sigaction` (`sys/linux/linux.c`). Bit 2 (`SA_SIGINFO`) makes the kernel build a siginfo block |
| `fsbase` | u64 | TLS base. `jc_enter` loads it into `lsys_fsbase`, and `jc_resume` writes it to `MSR 0xC0000100` |
| `pollwake` | u64 | Deadline for Linux `poll`/`select` (`sys/linux/linux.c`) |
| `lxbrk` | u64 | Linux-ABI program break (`sys/linux/linux.c`) |
| `susp`, `suspold` | int, u32 | `rt_sigsuspend` state: the temporary mask is active, and `suspold` holds the mask to restore |
| `lx` | int | Set when the image is static-PIE (`g_elf_pie`) or when the process has made any Linux system call (`linux_syscall` sets `p->lx=1`). After that, native `int 0x80` calls are refused (see 5.12) |
| `exitst` | int | Wait status in `wait()` encoding (see 5.4) |
| `stopnote` | int | `1` = a stop has not been reported yet, `2` = a continue has not been reported yet (for `WUNTRACED`/`WCONTINUED`) |
| `runcpu` | volatile int | `cpu_id()+1` while the process runs on a CPU, otherwise `0`. Stops two CPUs from picking the same process |
| `cputicks` | u64 | Total ticks the process has run, added up in `jc_enter` |
| `lastcpu` | int | The last CPU that ran the process |
| `it_next`, `it_int` | u64 | `ITIMER_REAL`: tick of the next SIGALRM (0 = off) and the reload interval in ticks (section 5.16). Kept in the leader |
| `tg` | int | Slot index of the thread-group leader. A process that never created a thread has `tg` equal to its own slot. `jc_lead(p)` returns `&g_jp[p->tg]` |
| `tgid` | int | PID of the process (the leader's `pid`). `getpid` returns it |
| `gexit` | int | Set in the leader while the whole group is exiting (exit_group, fatal signal); `exitst` then holds the status the last thread reports |
| `tkill` | int | Set on the other threads by `execve`; the pending SIGKILL then ends only that thread |
| `ctid` | u64 | `CLONE_CHILD_CLEARTID` / `set_tid_address` address: zeroed and futex-woken when the thread ends |
| `robust` | u64 | Head registered with `set_robust_list` (stored, not walked) |
| `fx_wait`, `fx_woken`, `fx_tg`, `fx_addr`, `fx_bits`, `fx_dead` | | Futex wait state (section 5.15): waiting flag, wake flag, group and address of the futex word, bitset, timeout tick |
| `cmd[48]` | char | Command line, shown by `ps` and in the jobs list |
| `fx[512]` | u8 | FXSAVE area. `jc_fx_init` sets FCW to `0x037F` and MXCSR to `0x1F80` (mask `0xFFFF`) |

`jc_idx(pid)` finds the slot for a PID among slots that are not free. `jc_alloc()` returns the first `S_FREE` slot.

---

### 5.3 Process states

| State | `ps` STAT | Meaning |
|---|---|---|
| `S_FREE` (0) | - | Slot is unused |
| `S_RUN` (1) | `R` | Ready to run, or running (`runcpu!=0`) |
| `S_WAIT` (2) | `S` | Blocked in a restartable call (read on a pipe or tty, waitpid). Retried on every tick |
| `S_SLEEP` (3) | `S` | Timed sleep or `pause`. Wakes at `wake`, or earlier when a signal can be delivered |
| `S_STOP` (4) | `T` | Stopped by SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU. `jc_launch` also creates pipeline members in this state while it wires them together |
| `S_ZOMB` (5) | `Z` | Has exited and is waiting to be reaped. Address space and descriptors are already freed |
| `S_TDEAD` (6) | `S` | A thread-group leader whose thread has exited while other threads still run. It is never scheduled, but it keeps the shared state (descriptors, signal handlers, brk, timers) and the process ID until the last thread exits, and then becomes `S_ZOMB` |

`ps` adds `+` after the state when `pgid==g_tty_fg` (`jc_builtin`).

```
            jc_create / jc_fork
                    |
                    v
   +---------->  S_RUN  <------------- SIGCONT (jc_signal_proc) / SIGKILL
   |             |  |  |                                      ^
   |  wake<=tick |  |  | stop signal, default action          |
   |  or signal  |  |  +------------------------------>  S_STOP
   |             |  | jc_block(S_WAIT/S_SLEEP)
   |             v  v
   +---- S_WAIT / S_SLEEP
                 |
   exit (0), fatal signal, fault --> jc_finish --> S_ZOMB --> waitpid / jc_poll --> S_FREE
```

---

### 5.4 Life cycle: create, build, fork, exec, finish, wait

#### `jc_build(args, &entry, &rsp, &argc, &argv)`
1. Takes the first word of `args` as the program name and calls `jc_find_prog` (5.11).
2. Copies `args` into `g_exec_args`. That string becomes `argv` in `elf_build_space`.
3. Calls `elf_build_space`, which returns a new CR3 (5.10).
4. Maps the rest of the 16-page stack lazily: pages `USTK_V - p*0x1000` for `p=1..15` (`JS_STK_PAGES=16`) get `vm_map_lazy`, unless a page is already mapped.
5. Returns `rsp = g_arg_rsp`, plus `argc` and `argv`.

#### `jc_create(args, ppid, pgid)`
Allocates a slot, builds the image, and zeroes the whole `jproc`. It then sets `pid`, `ppid`, `pgid` (the new PID if `pgid` is 0), `sid=1`, credentials, `cwd`, `brk=0x40002000000`, `mmnext=0x40004000000` and `lx=g_elf_pie`. `jc_ctx_fresh` builds the user context:

```c
c->rip=entry; c->cs=0x1B; c->rflags=0x202; c->rsp=rsp; c->ss=0x23; c->rdi=argc; c->rsi=argv;
```

All descriptors start as `-1`. Descriptors 0, 1 and 2 then point at the shared console vfile from `jc_con()`, which is `dev=5`, `O_RDWR`, and never freed. The state becomes `S_RUN`. It returns the slot index, `-1` if the table is full, or `-2` if the build failed.

#### `jc_fork(r)` (syscall 2)
- `vm_clone_user` copies the parent's page tables for slot 8 using copy-on-write. Writable pages lose `RW` and gain `PTE_COW` in both the parent and the child, and `pframe_ref` adds a reference to each frame. Pages that are not present but are in use are recorded as `PTE_LAZY`. Frames outside `[PF_BASE, PF_TOP)` are copied immediately. If any parent entry changed, the TLB is flushed by reloading CR3.
- The child starts as a copy of the parent's `jproc` (`*c=*par`) with an FPU snapshot. It gets a new PID, `ppid`, `cr3` and credentials from the per-CPU globals, and `brk/mmnext` from `g_ubrk/g_mmap_next`. `sigpend`, `exitst`, `stopnote`, `wake`, `runcpu` and `cputicks` are cleared.
- The reference count of every open vfile goes up by one (`vf_ref`).
- `jc_save_sys` copies the `int 0x80` frame into `c->ctx`, and `c->ctx.rax=0`, so the child sees fork return 0. The parent gets the child's PID.

#### `jc_exec(r, args)` (syscalls 4 and 157)
Builds the new image first. If that fails, it returns `-1` in `rax` and the old image keeps running. If it succeeds:
- switches to the kernel CR3, calls `vm_free_user` on the old space and installs the new `cr3`
- closes each descriptor that has `cloexec` set
- resets caught handlers (`sighand>1`) to `SIG_DFL`. Ignored signals stay ignored. All `saflags` are cleared
- clears `fsbase`, `lxbrk`, `pollwake` and `susp`, and sets `lx=g_elf_pie`
- resets the heap and mmap pointers, makes a fresh context, records the command name, and reinitializes the FPU
- does `k_longjmp(g_js_kern,1)` straight back to the scheduler. The process stays `S_RUN` and starts at the new entry point the next time it is picked.

Syscall 157 builds the command line from `path` plus `argv[1..7]`, joined by spaces.

#### `jc_finish(i, status)` and zombies
```c
jc_close_all(p);                          /* vf_unref every fd */
if (current CR3 == p->cr3) cpu_set_pt(kcr3);
vm_free_user(p->cr3); p->cr3=0;
p->exitst=status; p->state=S_ZOMB;
/* reparent children to init (1) if init is alive and we're not init, else to 0 */
jc_wake_parent(p->ppid);                  /* clear parent's wake if S_WAIT, send SIGCHLD */
```
`vm_free_user` walks PML4 slot 8 and frees every page frame, page table, page directory and the PDPT, and then the PML4 itself.

Status encoding (Linux `wait` layout):

| Event | `exitst` | Source |
|---|---|---|
| `exit(code)` | `(code & 0xFF) << 8` | syscall 0 |
| Killed by signal *s* | `s` (low 7 bits) | `jc_deliver`, `jc_fault` |
| Stopped by *s* | `(s << 8) \| 0x7F` | `jc_stop` |
| Continued (reported by `WCONTINUED`) | `0xFFFF` | `jc_waitpid` |

#### `jc_waitpid(r, pid, statusp, opts)` (syscalls 6 and 156)
Matches children (`ppid == me->pid`) using POSIX rules: `pid>0` is that child, `0` is the caller's group, `-1` is any child, and `<-1` is group `-pid`.
- A zombie that matches has its status stored and its slot set to `S_FREE`, and its PID is returned. This is the reaping step.
- With `opts&2` (`WUNTRACED`) and `stopnote==1`, the stop status is reported once.
- With `opts&8` (`WCONTINUED`) and `stopnote==2`, `0xFFFF` is reported once.
- If no child matches, it returns `-1`. With `opts&1` (`WNOHANG`) and nothing to report, it returns `0`.
- Otherwise `jc_block(r,S_WAIT,g_ticks+1)` blocks the caller. The syscall instruction runs again when the process is next scheduled (5.6).

`jc_poll` reaps zombies that nobody else will: those with `ppid==0` (children of the kernel shell), and those with `ppid==1` when init is dead. It remembers the last fatal signal, other than SIGINT and SIGPIPE, so it can print a message such as "Segmentation fault".

---

### 5.5 The setjmp/longjmp scheduling model

Job processes are not switched from one to another directly. Each CPU runs a scheduler loop on its own kernel stack. That loop enters a process with `jc_enter`, and the process leaves by `k_longjmp`ing back to the `k_setjmp` point inside `jc_enter`. Every way of leaving (blocking, exit, preemption, fault or exec) goes through `jc_leave`:

```c
static void jc_leave(void){ if(g_jcur>=0)cpu_fpu_save(g_jp[g_jcur].fx); cpu_set_pt(g_kern_cr3); k_longjmp(g_js_kern,1); }
```

`k_setjmp`/`k_longjmp` (`arch/x86_64/boot/userspace.S`) save and restore `rbx, rbp, r12-r15`, `rsp` and the return address. The buffer holds 8 qwords.

#### `jc_enter(i)`, step by step
1. If the process is `S_SLEEP` and its deadline has not passed (so a signal woke it), set `ctx.rax = -4` (EINTR). A process in `S_WAIT` or `S_SLEEP` becomes `S_RUN`.
2. Save the kernel's CR3 in `g_kern_cr3`.
3. If any signal can be delivered, run `jc_deliver(i)`. Return if the process stopped or died.
4. Restore a `susp` mask if one is set. Call `us_init()`.
5. Save the per-CPU `g_dcwd/g_uid/g_gid/g_umask/g_ubrk/g_mmap_next` in `volatile` locals, then load the process's values.
6. Set `g_jcur=i`, `TSS.rsp0 = top of g_jkstack`, `lsys_kstack`, `lsys_fsbase = p->fsbase`, `g_js_slice = jc_now()`, `runcpu = cpu_id()+1` and `lastcpu`, and increment `PCPU->switches`.
7. `cli`, then `if(k_setjmp(g_js_kern)==0)`: set `g_js_running=1`, restore the FPU, load `p->cr3`, release the big kernel lock with `bd=bkl_drop()`, and call `jc_resume(&p->ctx)`.
8. Return path (after `k_longjmp`): take the lock again with `bkl_take(bd)`, set `g_js_running=0` and `runcpu=0`, add the elapsed ticks to `cputicks`, and reload the kernel CR3. If the process is still alive, copy the per-CPU cwd, credentials, brk and mmnext back into it. Restore the kernel's own values, set `g_jcur=-1`, and run `sti`.

`jc_resume` (`userspace.S`) loads the user data segments (`0x23`) and writes `%gs:24` (the per-CPU `fsbase`) to `IA32_FS_BASE`. It then points `rsp` at the `regs_pt`, pops the 15 general-purpose registers, skips `vector/errcode`, runs `swapgs` and `iretq`.

#### Picking and running
```c
static int jc_runnable(int i){
    if(p->runcpu)return 0;                         /* already on some CPU */
    if(p->state==S_RUN)return 1;
    if((p->state==S_WAIT||p->state==S_SLEEP)&&(p->wake<=g_ticks||jc_deliverable(p))){
        if(p->state==S_SLEEP&&!jc_deliverable(p))p->ctx.rax=0; return 1; }
    if(p->state==S_STOP&&(p->sigpend&(1u<<9)))return 1;   /* SIGKILL reaches stopped procs */
    return 0;
}
```
- `jc_pick()` is a round-robin search that starts after `g_js_rr` and returns the first slot that is neither free nor a zombie and is runnable.
- `jc_run()` calls `jc_pick`/`jc_enter` up to `JMAX*2` times, and stops early once 2 ticks have passed or nothing is runnable.
- `jc_step()` is `jc_run()`, then `jc_poll()`, and returns whether anything ran or `g_js_dirty` was set. `core/init/main.c` calls `jc_step()` from the main loop in `ST_DESKTOP` and `ST_LIVE`. When `!jc_any_runnable()`, the BSP halts instead.
- On application processors, `ap_loop` (`arch/x86_64/smp/smp_sched.c`) takes the lock, calls `jc_pick()` and `jc_enter()` while `g_smp_sched` is set, and releases the lock.

#### Time slices and preemption
- The PIT runs at 100 Hz (`pit_init(100)` in `core/init/main.c`), so one tick is 10 ms.
- BSP: IRQ0 in `isr_body` calls `jc_timer(r)` when the interrupt came from ring 3. `jc_timer` does nothing for 2 ticks after `g_js_slice`. After that it copies the frame into `ctx`, sends EOI and calls `jc_leave()`. The slice is therefore about 20 ms.
- APs: `ap_timer` counts per-CPU `ticks` (the LAPIC timer at `lapic_hz/100`) and calls `jc_preempt(r)` after 2 ticks. `IPI_RESCHED` (0xF0) also calls `jc_preempt` when it lands in user mode. `jc_signal_proc` sends this IPI with `smp_kick` when the target process is running on another CPU.
- `jc_now()` returns `g_ticks` on CPU 0 and `PCPU->ticks` on the APs.

#### Faults
`isr_body` sends CPU exceptions from ring 3 to `jc_fault` while `g_js_running` is set. Before that, page faults go through `vm_user_pf`, which resolves lazy and COW pages through `vm_fault_user`. If that returns `-2`, the process is killed with status 9. `jc_fault` turns the vector into a signal:

| Vector | Signal |
|---|---|
| 0 (#DE), 16 (#MF), 19 (#XM) | 8 SIGFPE |
| 6 (#UD) | 4 SIGILL |
| 1 (#DB), 3 (#BP) | 5 SIGTRAP |
| anything else (#GP, #PF, ...) | 11 SIGSEGV |

If the process has no handler (`sighand<=1`) or the signal is blocked, `jc_finish(g_jcur, s)` ends it at once. Otherwise the signal is marked pending and delivered on the next entry. Either way, `jc_leave()` follows.

---

### 5.6 Blocking and restartable system calls

A driver or VFS read or write that cannot finish returns `JS_BLOCK` (`-1000`, defined in `arch/x86_64/interrupts/interrupts.c`). The syscall code then calls:

```c
static void jc_block(struct sysregs* r,int state,u64 wake){
    struct jproc* p=&g_jp[g_jcur]; jc_save_sys(r,&p->ctx);
    if(state==S_WAIT)p->ctx.rip-=2;          /* re-execute "int $0x80" (CD 80) / "syscall" (0F 05) */
    p->state=state; p->wake=wake; jc_leave();
}
```

- `S_WAIT` is a polling retry. The saved `rip` is moved back by the length of the 2-byte trap instruction, and `wake=g_ticks+1`. On the next tick the process is runnable again, and when it resumes it runs the same system call with the same registers. No per-object wait queues are needed: a pipe read simply returns `JS_BLOCK` again until data arrives. `jc_wake_parent` sets `wake=0` so a waiting parent rechecks at once.
- `S_SLEEP` does not rewind. The call completes when the process resumes. If the deadline passes, `jc_runnable` sets `rax=0`. If a signal wakes it early, `jc_enter` sets `rax=-4`. `pause` (158) sleeps with `wake = g_ticks+0x7FFFFFFFFFFF`.
- `jc_save_sys` copies the `struct sysregs` frame (15 registers, then the hardware iret frame at `fr[15..19]`) into `regs_pt`.
- `jc_yield_sys(r)` saves the context without changing the state, and leaves. Syscall 20 uses it, as do the paths that deliver a pending signal after a syscall.

The Linux ABI uses the same mechanism. `sys/linux/linux.c` calls `jc_block(r,S_WAIT,g_ticks+1)` for blocking I/O and poll/select, and `S_SLEEP` for nanosleep, pause and `rt_sigsuspend`. A futex wait (section 5.15) is the one `S_WAIT` that does not poll: it blocks with `wake` at its timeout (or far in the future), and `FUTEX_WAKE` sets `wake=0` on the threads it wakes, so they run at once without retrying every tick.

---

### 5.7 Signals

Signal numbers follow Linux x86-64: 2 INT, 9 KILL, 11 SEGV, 13 PIPE, 17 CHLD, 18 CONT, 19 STOP, 20 TSTP, 21 TTIN, 22 TTOU, 31 SYS. Valid numbers are 1-31.

#### Default actions (`sig_default`)

| Return | Action | Signals |
|---|---|---|
| 0 | ignore | 17 CHLD, 23 URG, 28 WINCH |
| 3 | continue | 18 CONT |
| 2 | stop | 19 STOP, 20 TSTP, 21 TTIN, 22 TTOU |
| 1 | terminate | all others |

#### Sending: `jc_signal_proc(i, sig)`
- Ignored when the target is free or a zombie, or when `sig` is outside 1-31.
- SIGCONT: a stopped process becomes `S_RUN` with `stopnote=2`, and its parent is notified. Pending STOP, TSTP, TTIN and TTOU are cleared. CONT is only queued if the process has a handler for it.
- A stop signal clears any pending SIGCONT.
- Signals that are ignored (`SIG_IGN`, or `SIG_DFL` with an "ignore" default) are dropped when sent. SIGKILL and SIGSTOP are never dropped.
- Sets `sigpend |= 1<<sig`. SIGKILL also moves a stopped process back to `S_RUN`.
- If the target is running on another CPU (`runcpu-1 != cpu_id()`), `smp_kick` interrupts that CPU.

`jc_kill(pid, sig, self)` applies the POSIX target rules (`>0`, `0`, `-1` meaning all except `self`, and `<-1`). When `g_kill_check` is set, a non-root caller cannot signal processes owned by other users; these are counted in `g_kill_denied`. It returns 0 if any process matched. `sig==0` only checks that the target exists.

#### Delivery: `jc_deliver(i)`
`jc_deliverable(p) = (sigpend & ~sigmask) | (sigpend & (SIGKILL|SIGSTOP))`. The loop runs at most 32 times and always takes the lowest pending signal first:

1. SIGKILL: `jc_finish(i,9)`. SIGSTOP: `jc_stop(i,19)`.
2. `SIG_IGN`: skip to the next signal.
3. `SIG_DFL`: ignore or continue (skip), terminate (`jc_finish(i,s)`), or stop (`jc_stop(i,s)`).
4. Handler: a process in `S_WAIT` or `S_SLEEP` becomes `S_RUN`, then `jc_push_handler`. If that fails, the process is killed with SIGSEGV (11). Only one handler is set up per entry.

Delivery happens in `jc_enter` before the process resumes, and after any system call that leaves a signal deliverable (`syscall_dispatch` checks `jc_sig_pending_cur()` and then calls `jc_yield_sys`). `kill` (17) and `sigprocmask` (142) also yield at once when they make a signal deliverable to the caller.

#### Handler frame: `jc_push_handler`
```
 high   old user rsp (top)
        [128 bytes red zone skipped]
        [128 bytes siginfo, only if SA_SIGINFO]   <- info; info[0]=signo
        saved regs_pt (whole interrupted context)  <- frame (16-byte aligned)
        u64 saved mask (suspold if in sigsuspend)
 low    u64 return address = sigrest[s]             <- new rsp
```
The code checks that the 1 KB below `rsp` and the handler address are in the user window (`jc_uok`). It then switches to the process's CR3 and checks that the frame is mapped and writable (`jc_umapped`, which can also resolve COW and lazy faults). After writing the frame, it sets `rip=sighand[s]`, `rsp=sp`, `rdi=signo`, `rsi=info or 0` and `rdx=rax=0`, and `sigmask |= (1<<s) | sigamask[s]`. KILL and STOP can never be masked.

#### Returning: `sigreturn` (143)
The user library installs a trampoline (`usr/libc/src/libc.c`, and also `legacy_posix.h`):
```asm
__nvx_sigtramp: mov %rsp,%rbx ; mov $143,%eax ; int $0x80 ; hlt
```
When the handler runs `ret`, it pops `sigrest` and lands on the trampoline with `rsp` pointing at the saved frame. Syscall 143 checks that the frame is mapped and that the saved `rip` is in the user window; if either check fails, the process gets SIGSEGV. It then forces `cs=0x1B` and `ss=0x23`, keeps only safe RFLAGS bits (`(rflags&0xCD5)|0x202`), and restores `ctx` and `sigmask`. Finally it calls `jc_leave()`, so the restored context is resumed through the scheduler. The libc `sigaction` passes the handler, `__nvx_sigtramp` and `sa_mask` to syscall 141.

#### Stop and continue
`jc_stop(i,sig)` sets the state to `S_STOP`, `exitst=(sig<<8)|0x7F` and `stopnote=1`, then wakes the parent and sends it SIGCHLD. A stopped process is not runnable unless SIGKILL is pending. SIGCONT is handled entirely when it is sent (see above).

---

### 5.8 Job control and the terminal

#### Terminal ownership
- `g_tty_fg` is the process group in the foreground. `0` means the kernel shell owns the terminal.
- Input goes through two buffers: the edit line `g_ted[256]` (`g_ted_n`) and the cooked input queue `g_tin[1024]` (`g_tin_n`, plus the `g_tin_eof` flag).
- `live_input` (`shell/commands.c`) maps Ctrl+C, Z, D and `\` to the codes 3, 26, 4 and 28. While `g_tty_fg` is set, every key goes to `tty_key`.

`tty_key(c)`:

| Key | Action |
|---|---|
| Ctrl-C (3) | Echoes `^C` and sends SIGINT (2) to the group `-g_tty_fg`. If a program-to-builtin collector is running, it is cancelled |
| Ctrl-Z (26) | Echoes `^Z` and sends SIGTSTP (20) to the foreground group |
| Ctrl-\ (28) | Echoes `^\` and sends SIGQUIT (3) to the foreground group |
| Ctrl-D (4) | On an empty edit line, sets `g_tin_eof`. Otherwise sends the partial line without a newline |
| Backspace | Deletes from `g_ted` |
| Enter | Moves `g_ted` plus `'\n'` into `g_tin` |
| Printable | Added to `g_ted` and echoed |

`jc_tty_read(buf,len)`, used by `vfs_read` on `dev==5`:
- returns `-1` if the caller is not in session 1
- if the caller's group is not in the foreground, sends SIGTTIN (21) to it and returns `JS_BLOCK`
- returns at most one line from `g_tin`. A pending EOF returns 0 once. Otherwise it returns `JS_BLOCK`.

`jc_tty_out(s,n)`, used by `vfs_write` on `dev==5` and by syscalls 1 and 3:
- prints white text on the kernel terminal
- turns UTF-8 box-drawing characters, typographic quotes and dashes, `...` and NBSP into ASCII (`jc_u8_ascii`)
- removes ANSI escape sequences (ESC `[` ... final byte)
- drops `\r`, turns `\t` into two spaces, and drops other control characters.

#### Launching from the shell: `jc_launch(line)`
`run_cmd` in `shell/commands.c` calls `jc_launch` first, unless output is being captured. The steps:

1. A trailing `&` makes the job a background job (`>&` does not count).
2. The line is split on `|`, respecting quotes, into up to 6 stages. A stage is a *program* when its first token passes `jc_is_prog`. If no stage is a program, `jc_launch` returns 0 and the shell runs its built-in pipeline.
3. Program stages must be contiguous. If built-in stages come first, they run through `run_cmd` with `g_capture=1`, and their output fills a pipe (sized to the output) that becomes the first program's stdin. If built-in stages come after the programs (the *tail*), the last program's stdout goes into a collector pipe `g_coll_vi`. `jc_coll_drain` copies that pipe into `g_coll_buf` (up to `COLL_CAP` = 64 KB). When the job ends, `jc_coll_finish` runs the remaining built-ins through `run_cmd(g_coll_rest)` with `g_run_feed` pointing at the collected text. A tail cannot be combined with `&`, and only one collector can run at a time.
4. For each program stage, the tokens are scanned for redirections: `< f`, `> f`, `>> f`, `>f`, `2> f`, `2>> f` and `2>&1`. Spaces inside a quoted argument are stored as `0x1F`, and `elf_args_from_string` turns them back into spaces.
5. `jc_create(args, 0, pgid)` creates each process. Its parent is the kernel shell (`ppid 0`), and the first process's PID becomes the group ID. Each process starts in `S_STOP` so it cannot run before it is fully wired. The previous stage's pipe read end becomes its stdin, and a new 4 KB pipe's write end becomes its stdout. `jc_openinto` opens redirections in the context of the new process and moves them onto descriptor 0, 1 or 2. `2>&1` makes descriptor 2 share descriptor 1's vfile.
6. If starting any stage fails, the stages already created get SIGKILL.
7. All stages are set to `S_RUN`, and a job entry is created. Foreground: `g_tty_fg=pgid` and the input buffers are cleared. Background: prints `[id] lastpid`.

#### The job table: `g_jobs2`
```c
#define JOBMAX 16
struct job { int used, id, pgid, state, bg, notified_stop; char cmd[64]; };
```
`state` is 1 for running and 2 for stopped. `jc_job_new` uses the smallest free job ID. On each step, `jc_poll`:
- if no process in the group is alive: for the foreground job, gives the terminal back (`g_tty_fg=0`), prints the name of the fatal signal if there was one, finishes the collector and shows the prompt again. For a background job, prints `[n]+  Done  cmd`. The entry is then freed.
- if every live process in the group is stopped: marks the job stopped, gives the terminal back if it was in the foreground, and prints `[n]+  Stopped  cmd`.

#### Builtins (`jc_builtin`)

| Command | Behaviour |
|---|---|
| `jobs` | Lists `[id]  Running/Stopped  cmd`, or "no jobs" |
| `fg [%n]` / `bg [%n]` | Picks job *n*, or the highest ID if none is given (`jc_jobarg`). Sends SIGCONT to `-pgid`. `fg` also gives that group the terminal |
| `kill [-SIG\|-n] pid\|-pgid\|%job` | Default signal is 15. Accepts names 1-22 (`HUP`...`TTOU`) with or without the `SIG` prefix. Permissions are checked |
| `ps` | Columns: PID, PPID, PGID, STAT, USER, COMMAND. Always shows a fake PID 0 row for "nsh (kernel shell)" |
| `umask [ooo]` | Shows or sets `g_umask` |
| `wait` | Only reports how many jobs are still running |

`dt_busy(d)` (also in `jobs.c`) stops a directory from being removed while it is the working directory of any live process. For a process running on another CPU, it reads that CPU's `dcwd`.

---

### 5.9 File descriptors and pipes

#### Descriptor tables
- `FDMAX=256` per process and `VMAXOPEN=1024` system-wide (`fs/vfs/ramfs.c`; 32 and 32 before v8.97). Open files are `struct vfile g_vf[VMAXOPEN]`, shared by all processes, with a reference count `ref`. The threads of a process share the leader's table.
- `fdt()` returns the descriptor table of the running process (the thread-group leader's `fd[]`, so all threads share it) while a job process runs, and the kernel shell's `g_fd` otherwise. `fdce()` does the same for the cloexec arrays (`g_kcloexec` for the kernel).
- `vf_ref`/`vf_unref` manage the count. When it reaches zero: a pipe end decrements its pipe's `readers` or `writers`, and the pipe buffer is freed once both are zero. The console vfile is never freed (its `ref` is reset to 1). Framebuffer and mouse open counts go down. A dirty file is written back (`fs_wb_mark`), and an orphaned inode is released (`fs_orphan_close`).
- `vfile.dev` codes used in this part: `0` regular file, `5` console tty, `6` pipe read end, `7` pipe write end, `9` `/dev/fb0`, `10` mouse.
- Inheritance: fork increments every reference. Exec closes descriptors marked cloexec. `dup2` (144), `F_DUPFD` and `pipe` clear the flag on the new descriptor. `fcntl` (146): `cmd 0` duplicates to the lowest free descriptor `>= rdx`, `1` reads the cloexec flag, `2` sets it (`rdx&1`), `3` returns the `flags`, and any other value returns 0.

#### Pipes
```c
#define PIPE_MAX 128
struct jpipe { int used, r, w, n, cap, readers, writers; char* buf; };
```
`jc_pipe_new(cap,&rvi,&wvi)` allocates a `kmalloc` ring buffer and two vfiles (`dev 6` with `O_RDONLY`, and `dev 7` with `O_WRONLY`), with `fs_idx` pointing at the pipe.

| Operation | Condition | Result |
|---|---|---|
| `jc_pipe_read` | `n==0`, writers > 0 | `JS_BLOCK`, so the caller blocks in `S_WAIT` |
| | `n==0`, no writers | `0` (EOF) |
| | data present | Copies `min(len, n)` bytes |
| `jc_pipe_write` | no readers | SIGPIPE (13) to the caller, returns `-1` |
| | buffer full | `JS_BLOCK` |
| | otherwise | Copies as much as fits (a short write is possible) |

Syscall 145 `pipe(int fds[2])` creates a 4096-byte pipe and puts the read and write ends in the two lowest free descriptors.

---

### 5.10 The ELF loader (`core/proc/elf.c`)

#### Address-space constants

| Constant | Value | Meaning |
|---|---|---|
| `ELF_IMG_LO` | `0x40000000000` | Lowest address for a loadable segment |
| `ELF_IMG_TOP` | `0x40002000000` | Highest end address for a segment. The heap starts here |
| `ELF_PIE_BASE` | `0x40000400000` | Load bias for `ET_DYN` (static-PIE) |
| `USTK_V` | `0x40000100000` | Top stack page. The initial stack ends at `USTK_V+0x1000` |
| `ELF_STK_LO` | `USTK_V - 15*0x1000` | Bottom of the 16-page stack area. Segments may not overlap it |
| `XA_MAX` / `XA_CAP` | 32 / 2048 | Maximum argv or envp entries, and bytes of strings |

#### `elf_build_space(elf, len, &entry)`
1. Header checks: magic `\x7FELF`, `EI_CLASS=2` (64-bit), `EI_DATA=1` (little-endian), `e_type` 2 (`ET_EXEC`) or 3 (`ET_DYN`), `e_machine=62` (x86-64). `elf_ph_ok` requires `phentsize>=56`, `1<=phnum<=64`, and a program-header table that fits inside the file.
2. Static and static-PIE: for `ET_DYN`, `base=ELF_PIE_BASE` and `g_elf_pie=1`. No dynamic linking is done. Programs must be static.
3. Segment checks for each `PT_LOAD`: `filesz<=memsz`, the file range is inside the image, the virtual range is inside `[ELF_IMG_LO, ELF_IMG_TOP)`, and the segment does not overlap the stack area. The entry point must be inside a `PT_LOAD` that has `PF_X`. `PT_PHDR` (type 6) sets `phdr_va`. Without it, the address is computed from the segment at file offset 0 plus `e_phoff`.
4. Mapping: a new PML4 from `vm_new_pml4()`. Every page of every segment gets a frame mapped with flags `0x7` (present, RW, user). Bytes are copied from the file up to `filesz`, and the rest is zero (BSS). Pages shared by two segments reuse the frame already mapped.
5. Stack: two pages are mapped right away (`USTK_V` and `USTK_V-0x1000`). The other 14 are mapped lazily by `jc_build`.
6. SysV initial stack, built in the 8 KB `g_stkimg` and copied into the two stack frames:

```
USTK_V+0x1000 -+  16 random bytes (xorshift over TSC)            <- AT_RANDOM
               |  envp strings ("NAME=value\0" ...)
               |  argv strings
               |  (16-byte alignment)
               |  auxv pairs ... AT_NULL
               |  NULL
               |  envp[0..n-1]
               |  NULL
               |  argv[0..argc-1]               <- g_arg_argv = rsp+8
     rsp ------+  argc                          <- g_arg_rsp
```

Auxiliary vector (`aux[]`):

| Tag | Name | Value |
|---|---|---|
| 3 | `AT_PHDR` | `phdr_va` |
| 4 | `AT_PHENT` | `e_phentsize` |
| 5 | `AT_PHNUM` | `e_phnum` |
| 6 | `AT_PAGESZ` | 4096 |
| 7 | `AT_BASE` | 0 (no interpreter) |
| 8 | `AT_FLAGS` | 0 |
| 9 | `AT_ENTRY` | `e_entry + base` |
| 11 / 12 | `AT_UID` / `AT_EUID` | `g_uid` |
| 13 / 14 | `AT_GID` / `AT_EGID` | `g_gid` |
| 16 | `AT_HWCAP` | 0 |
| 17 | `AT_CLKTCK` | 100 |
| 23 | `AT_SECURE` | 0 |
| 25 | `AT_RANDOM` | address of the 16 random bytes |
| 31 | `AT_EXECFN` | `argv[0]` |
| 0 | `AT_NULL` | 0 |

argv and env sources: unless a caller has already filled `g_xa_*` (`g_xa_set`, which the Linux `execve` does), `elf_args_from_string(g_exec_args)` splits the command line on spaces. Unless `g_xe_set` is set, `env_pack` fills the environment from the kernel's `g_env[24]`. `env_init` sets the defaults: `PATH=/bin`, `HOME=/home`, `PWD=/home`, `USER=root`, `SHELL=/bin/nsh`, `TERM=noovex` and `XZ_DEFAULTS=--memlimit-compress=20MiB` (`sys/native/syscall.c`). The native context also passes `argc` in `rdi` and `argv` in `rsi` (`jc_ctx_fresh`), so the native crt0 can use either the registers or the stack.

#### Other loaders in `elf.c`
- `elf_load_run(elf,len)`: a single-run path with no scheduler. It calls `k_setjmp(g_kjmp)`, loads the new CR3 and calls `enter_ring3_args`. Legacy exit (syscall 0 with no scheduler) does `k_longjmp(g_kjmp,1)` to come back. `sys_exec` uses it.
- `nvx_dlload`/`nvx_dlcall`: a relocating loader for kernel-side modules. It copies `PT_LOAD` segments into DMA memory and applies `R_X86_64_RELATIVE`, `64`, `GLOB_DAT` and `JUMP_SLOT` relocations. Symbols resolve against `NVX_KSYMS` (`nvx_puts`, `nvx_putc`, `nvx_ticks`, `nvx_alloc`). It is not used for user processes.

---

### 5.11 Embedded programs: `JS_BIN` and `jc_find_prog`

User programs are linked into the kernel as `_binary_user_<name>_elf_start/_end` blobs. `JS_BIN[]` (`jobs.c`) maps names to them:

| Name(s) | Blob |
|---|---|
| `count`, `spin`, `sigdemo`, `upper`, `greet`, `pipetest` | their own demo ELFs |
| `ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink true false tee sort nvxbox cut tr uniq seq basename dirname rev tac nl fold paste comm cmp md5sum sha256sum base64 find yes printenv` | all `nvxbox` (the multi-call binary, defined with the `NVXB()` macro) |
| `libc8`, `mhello`, `musltest`, `smptest`, `sqlite3` | their own ELFs |
| `gzip gunzip zcat` / `bzip2 bunzip2 bzcat` / `xz unxz xzcat lzma unlzma lzcat` / `zstd unzstd zstdcat` | `gzip`, `bzip2`, `xz` and `zstd` blobs |
| `+init` | `init` (the leading `+` keeps it out of `/bin` listings) |

Lookup order in `jc_find_prog(name,&len)`:
1. Remove a `/bin/`, `/usr/bin/` or `/sbin/` prefix. `/sbin/init` becomes `+init`. Remove a `.elf` suffix.
2. A base name of `init` (case-insensitive) always resolves to the `+init` entry. The condition ends in `||1`, so this always applies.
3. Compare case-insensitively (`jc_ieq`) with every `JS_BIN` name.
4. Look up the path in the Unix file system (`ux_lookup`). If the file has ELF magic and is at least 64 bytes, it is used, but a non-root user also needs an execute bit (`mode & 0111`).
5. Try `fat_read_file(name, g_spawnbuf, 64 KB)` for an ELF on the FAT volume.

`jc_is_prog` is how `jc_launch` decides whether a stage is a program. `fs/vfs/unixfs.c` (`k_readdir_extra`) lists the `JS_BIN` names in `/bin` and `init` in `/sbin`, and `ux_stat2` reports them as mode `0100755`.

---

### 5.12 The native `int 0x80` system-call ABI

Entry: `idt_set128` installs vector `0x80` with DPL 3 (`attr=0xEE`). `isr128` (`userspace.S`) runs `swapgs` if the call came from ring 3, pushes `rax...r15` (a `struct sysregs`: `r15,...,rbx,rax` from low to high addresses), and calls `isr128_c`, which takes the big kernel lock, counts `PCPU->syscalls`, and calls `syscall_dispatch`.

Convention (`usr/src/user.h`, `nvx_sys`): the number goes in `rax` and the arguments in `rbx, rcx, rdx, rsi, rdi`. The result comes back in `rax`. Errors are `-1` unless noted otherwise; the native layer has no errno.

Guards in `jc_syscall` run before its main switch:
- If `p->lx` is set (a static-PIE image, or a process that has used the Linux ABI), the kernel sends SIGSYS (31) and returns `-38`.
- 1, 4, 5: `rbx` must be a mapped string of at most 4 KB.
- 157: the path, and up to 8 `argv` pointers with their strings, must be mapped.
- 31, 32: `0 <= rdx <= 16 MB`, and the buffer `[rcx, rcx+rdx)` must be mapped. `jc_umapped` resolves COW and lazy pages as it checks.
- 145: 8 bytes at `rbx` must be mapped.

#### Calls handled by `jc_syscall` (job processes)

| # | Name | Arguments | Behaviour |
|---|---|---|---|
| 0 | exit | rbx=code | `jc_finish(cur, (code&0xFF)<<8)` and leave |
| 1 | puts | rbx=str | Writes the string (at most 4096) through `jc_tty_out` |
| 2 | fork | - | `jc_fork`. Returns the child's PID, or 0 in the child |
| 3 | putc | rbx=ch | One character through `jc_tty_out` |
| 4 | exec | rbx=cmdline | `jc_exec`. Returns `-1` on failure |
| 5 | spawn | rbx=cmdline | `jc_create(cmd, mypid, mypgid)`. Returns the child's PID |
| 6 | waitpid | rbx=pid | `jc_waitpid(pid, NULL, 0)` |
| 12 | brk | rbx=addr | 0 returns the current break. Otherwise maps frames eagerly up to the new break and returns it |
| 16, 71 | sleep_ms | rbx=ms | `S_SLEEP` for `(ms+9)/10` ticks. Returns 0, or -4 if interrupted |
| 17 | kill | rbx=pid, rcx=sig | `jc_kill` with permission check. Yields if this makes a signal deliverable to the caller |
| 20 | yield | - | `jc_yield_sys` |
| 31 | read | rbx=fd, rcx=buf, rdx=len | `vfs_read`. Blocks (`S_WAIT`, restartable) on `JS_BLOCK` |
| 32 | write | rbx=fd, rcx=buf, rdx=len | `vfs_write`. Blocks on `JS_BLOCK`. A regular file is marked dirty and written back when the last descriptor is closed (before v8.97 every write called `fs_sync`) |
| 39 | getpid | - | `p->pid` |
| 50 | getkey | - | Returns one byte from `g_tin` if the caller is in the foreground and input is waiting, else `-1` |
| 51 | waitkey | - | `jc_tty_read` one byte. Blocks |
| 131 | setuid | rbx=uid | Allowed for root, or when setting the same UID |
| 132 | getppid | - | `p->ppid` |
| 141 | signal | rbx=sig, rcx=handler, rdx=restorer, rsi=mask | Fails for 9 and 19. Returns the old handler |
| 142 | sigprocmask | rbx=how (0 block, 1 unblock, 2 set), rcx=set | Returns the old mask. Yields if a signal becomes deliverable |
| 143 | sigreturn | rbx=frame | Restores the context and mask (5.7) |
| 144 | dup2 | rbx=old, rcx=new | `jc_dup2` |
| 145 | pipe | rbx=int[2] | 4 KB pipe |
| 146 | fcntl | rbx=fd, rcx=cmd, rdx=arg | `F_DUPFD`/`GETFD`/`SETFD`/`GETFL` as 0/1/2/3 |
| 147 | umask | rbx=mask | Returns the old value |
| 148 | getgid | - | `g_gid` |
| 149 | setgid | rbx=gid | Allowed for root, or when setting the same GID |
| 150 | setpgid | rbx=pid (0 = self), rcx=pgid (0 = pid) | Only for self or a child in the same session |
| 151 | getpgid | rbx=pid (0 = self) | |
| 152 | setsid | - | Fails if the caller already leads a group. Otherwise `sid=pgid=pid` |
| 153 | getsid | rbx=pid (0 = self) | |
| 154 | tcgetpgrp | - | `g_tty_fg` (or 1) for session 1, else -1 |
| 155 | tcsetpgrp | rbx=pgid | Session 1 only |
| 156 | waitpid | rbx=pid, rcx=status*, rdx=opts | WNOHANG=1, WUNTRACED=2, WCONTINUED=8 |
| 157 | execv | rbx=path, rcx=argv | Joins `path argv[1..7]` and calls `jc_exec` |
| 158 | pause | - | Sleeps until a signal arrives. Returns -4 |
| 172 | isatty | rbx=fd | 1 if the descriptor is the console (`dev 5`) |

Any number not in this table falls through to the legacy dispatcher. For a job process, file calls there (30, 33, 36, 37, ...) still use that process's descriptor table, because the VFS calls `fdt()`.

#### Legacy dispatcher calls (`syscall_dispatch` switch)

| # | Name | Arguments | Notes |
|---|---|---|---|
| 0 | exit | rbx=status | Behaves differently in preempt mode, in the cooperative scheduler, and in single-run mode (longjmp to `g_kjmp`) |
| 1 / 3 | puts / putc | rbx | Appends to the 256-byte `g_r3out` buffer |
| 2 | fork | - | `do_fork`: full copy, legacy table only |
| 4 | exec | rbx=path | `sys_exec` to `elf_load_run` |
| 5 / 6 | spawn / waitpid | rbx | `sys_spawn` (FAT, cooperative run to completion) / `g_last_exit[pid]` |
| 7 | sysinfo | rbx=what | 0 width, 1 height, 2 RAM MB, 3 ticks, 4 disk present |
| 9 / 11 / 12 / 60 | mmap / munmap / brk / sbrk | rcx=len /, / rbx=addr / rbx=inc | Anonymous memory only. munmap is a no-op |
| 15 | rand | - | `g_ticks*2654435761+12345` |
| 16 / 71 | sleep_ms | rbx=ms | Busy-waits (`rtw_delay_us`, or `cpu_idle` until the tick deadline) |
| 17 | kill | - | Returns 0 and does nothing |
| 18 / 19 / 21 | getcwd / chdir / mkdir | rbx=buf, rcx=n / rbx=path / rbx=path | `mkdir` applies `umask` |
| 30 | open | rbx=name, rcx=flags | `flags==1` is treated as `O_CREAT\|O_RDWR` |
| 31-37 | read, write, close, fstat (size), readdir(n,buf), lseek, dup | | VFS |
| 38 | unlink | rbx=path | |
| 39 / 132 / 130 / 131 | getpid / getppid / getuid / setuid | | Legacy PIDs are slot+1 |
| 40-45 | fb_clear, fb_pixel, fb_rect, (43 unused), fb_text, fb_present | rbx... | Draws into the compositor scene |
| 50 / 51 / 52 | getkey / waitkey / getmouse | | Raw keyboard queue (`kpop`) |
| 61 | thread_create | rbx=fn | `thread_create_k` |
| 70 | ticks | - | 100 Hz tick counter |
| 80-83 | shm_get, shm_map, msg_send, msg_recv | | IPC demo (5.13) |
| 90-95 | screen_clear, blit_shm, screen_rect, screen_text, screen_present, wm_poll | | Compositor demo |
| 100-105 | socket, connect(sid,ip,port), send, recv, close, resolve(host) | | TCP stack |
| 110 | stat | rbx=path, rcx=u32[3] | `{mode,size,isdir}` |
| 120 / 121 | getenv(name,buf,cap) / setenv(name,val) | | `g_env` |
| 133 / 140 | realtime ms / monotonic ms | - | RTC / `g_ticks*10` |
| 134-139 | rename, rmdir, truncate, ftruncate, fsync, readdir_at(path,n,name,type*) | | |
| 160-163 | link, symlink, readlink, stat2(path,u32[10],follow) | | `ux_*` |
| 165-169 | chown(path,uid,gid,follow), chmod, utime, mount(src,tgt), umount | | |
| 170 / 171 | gethostname(buf,n) / sethostname(str) (root only) | | |
| default | | | `-1` |

---

### 5.13 The legacy scheduler and demos (`core/proc/sched.c`)

These "stage" and "rung" demos come from before `jobs.c` and are still in the tree.

- Table: `MAXPROC 6`, `struct proc { used, alive, cr3, ksp, ctx, started, exit_status, zombie, ppid }`, each with its own 16 KB kernel stack `g_pstack[i]`.
- Cooperative scheduling: `proc_create` builds a kernel stack holding an iret frame (`ss=0x23`, `rsp=USTK_V+0x1000`, `rflags=0x202`, `cs=0x1B`, `rip=entry`), then `user_trampoline` as the return address, then six zeroed callee-saved registers. `sched_swap(&save_ksp, load_ksp)` swaps kernel stacks. `sched_run_all` switches into the first process and comes back once all have exited. `sched_yield_now` and `sched_exit_now` pick the next process round-robin with `sched_next`. `user_trampoline` runs `swapgs; iretq`. `user_trampoline_fork` clears `eax` first.
- Preempt demo (`preempt_run`): two copies of `user_preempt`. Each has an ID byte (`'1'`, `'2'`) mapped at `0x40000050000`. While `g_preempt_on` is set, IRQ0 in `isr_body` saves the frame into `g_proc[cur].ctx`, picks the next live process, writes CR3 and copies the next process's context into the interrupt frame, so `iretq` resumes it. When none are left, it does `k_longjmp(g_preempt_kern,1)`.
- IPC: shared memory (`SHM_MAX 4`, up to 24 frames each, mapped at `0x40000600000 + id*1 MB`) and message queues (`MSGQ_MAX 6`, 32 deep, 64-byte messages).
- Compositor demo (`compositor_run`): three copies of `user_compdemo`. Messages are `struct wmsg` with types `WM_CREATE/WM_DAMAGE/WM_CLOSE`, sent on queue 0.
- Fork and threads: `do_fork` copies every user page eagerly. `thread_create_k` shares the caller's CR3 and gives each thread a 64 KB-spaced user stack from `0x40000200000`.
- `sys/native/syscall.c` helpers: `sys_spawn` loads from FAT and checks `perm_can_exec`, `sys_waitpid` returns the status stored in `g_last_exit[]`, and `sys_exec` uses `elf_load_run`.

---

### 5.14 init (PID 1) and `/etc/rc`

- Start: `jc_start_init()` (`fs/vfs/unixfs.c`) is called from `ux_desktop_start` when the main loop enters `ST_DESKTOP`. If PID 1 does not exist yet, it runs `jc_create("init",0,0)`, which resolves to `+init`, and then sets `pid=1`, `pgid=1`, `ppid=0` and `uid=gid=0`. The live terminal (`ST_LIVE`) sets up the Unix layer with `ux_core_start` but does not start init.
- `/etc/rc` is created by `ux_boot_layout` with this content:
  ```
  # /etc/rc - run by init (PID 1) at boot
  echo NoovexOS: init started
  ```
- The init program (`usr/src/init.c`):
  1. installs a SIGCHLD handler
  2. reads `/etc/rc` (up to 4 KB) line by line. `run_line` splits each line on spaces and tabs (up to 11 arguments), skips `#` comments, and treats a final `&` as background. Then it runs `fork()` and `execv(argv[0], argv)`; on failure it prints "init: cannot run ..." and exits with 127. Foreground lines are waited for with `waitpid`.
  3. appends `boot: init started, /etc/rc done at <ctime>` to `/var/log/boot.log`
  4. loops forever: `waitpid(-1,&st,WNOHANG)` until no more children, then `pause()`.
- Orphan handling: `jc_finish` gives orphans to init while it is alive. Init reaps them in its `waitpid(-1)` loop. If init has died, `jc_poll` frees zombies whose parent is 1.

---

### 5.15 Threads and futexes

Since v8.97 a process can have several threads that run at the same time on different cores. A thread is an ordinary `struct jproc` slot that shares the address space and the per-process state of its thread-group leader.

#### What is shared and where it lives

| State | Shared by | Kept in |
|---|---|---|
| Address space | all threads | the same `cr3` value in every thread's slot; freed once, when the last thread exits |
| Descriptors and cloexec flags | all threads | the leader's `fd[]` and `cloexec[]`; `fdt()`/`fdce()` always return the leader's arrays |
| Working directory, umask, `brk`, mmap high-water mark, `lxbrk` | all threads | the leader; `linux_syscall` loads them into the per-CPU variables before each call and stores them back after it |
| Signal handlers (`sighand`, `sigrest`, `sigamask`, `saflags`) | all threads | copied to every thread's slot by `rt_sigaction`, so each slot can be read directly |
| Interval timer (`it_next`, `it_int`) | the process | the leader |
| PID, parent, process group, session | the process | the leader; `jc_tg_sync_ids` copies `pgid`, `sid` and `ppid` to the other slots when they change |
| Registers, FPU state, TLS base, signal mask, pending thread signals, futex wait state | one thread | each slot |

`jc_lead(p)` returns the leader, `jc_tg_member(j, L)` tells whether slot `j` is a live thread of group `L` (not free, zombie or `S_TDEAD`), and `jc_tg_live(L, except)` counts live threads.

#### Creating a thread: `clone` with `CLONE_THREAD`

`lx_clone` (`sys/linux/linux.c`) handles the flags musl's `pthread_create` uses: `CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_DETACHED`. `CLONE_THREAD` requires `CLONE_VM` and `CLONE_SIGHAND` (EINVAL otherwise).

`jc_clone_thread(r, stack, tls, settls)` (`core/proc/jobs.c`) copies the calling slot, gives the copy a new ID from `g_next_pid`, keeps `tg` (so it joins the same group), copies `tgid`, `ppid`, `pgid` and `sid` from the leader, clears pending signals, timers, futex state and its unused descriptor array, saves the caller's registers with `rax = 0`, and sets `rsp` and the TLS base if asked. The new slot is `S_RUN` and `smp_kick_idle` wakes an idle core for it. Then `lx_clone` stores `ctid` for `CLONE_CHILD_CLEARTID`, writes the new ID through `CLONE_CHILD_SETTID` and `CLONE_PARENT_SETTID`, and returns it.

A `clone` without `CLONE_THREAD` is still a fork (copy-on-write), also when `CLONE_VM | CLONE_VFORK` is given, as musl's `posix_spawn` does; the child gets its own copy of the stack it is given, which is enough because it only calls `execve` or `_exit`.

#### IDs

`getpid` returns the leader's `pid` (the process ID), `gettid` the slot's own `pid`. `getppid`, `getpgid`, `getsid` and `setpgid`/`setsid` work on the leader. `set_tid_address` stores `ctid` and returns the thread ID. `set_robust_list` stores the head and `get_robust_list` returns it; the list is not walked when a thread dies.

#### Signals

- Process-directed signals (`kill`, the terminal's ^C/^Z, SIGCHLD, SIGALRM from the interval timer, `jc_kill`) go through `jc_signal_proc`. It delivers to the given slot unless that slot is an `S_TDEAD` leader or blocks the signal; then it picks the first live thread that does not block it. SIGKILL and SIGCONT go to every thread.
- `jc_kill` only looks at leaders (and at a thread whose own ID was named), so a signal sent to a group is delivered once per process, not once per thread.
- Thread-directed signals: `tgkill` (234) and `tkill` (200) call `jc_signal_thread`, which never redirects. `tgkill` checks that the thread belongs to the given process.
- A stop signal stops the whole process: `jc_stop` marks the thread stopped, sets SIGSTOP pending on the other threads, and records the stop in the leader, where `waitpid` sees it.

#### Ending threads and processes

| Event | What happens |
|---|---|
| `exit` (60) in a thread while other threads live | `jc_thread_exit` drops only this thread: `ctid` is set to 0 and futex-woken, a leader becomes `S_TDEAD`, any other slot is freed at once (threads are not waited for) |
| `exit` of the last thread, or `exit_group` (231) | `jc_finish` |
| Fatal signal or fault in any thread | `jc_finish` |
| `jc_finish(i, status)` with other live threads | the leader records `gexit` and the status, every other thread gets SIGKILL, and thread `i` is dropped. Each thread ends when it next enters the kernel or is picked (a thread running on another core is kicked with an IPI) |
| `jc_finish` for the last live thread | closes the leader's descriptors, frees the address space once, makes the leader `S_ZOMB` with the group status, reparents children and wakes the parent |

Because the address space is only freed by the last thread, and a thread that is still running on another core counts as live, no core can be using page tables that have been freed.

`waitpid` and `wait4` are answered for the whole process: the caller's leader is the parent that is matched, and only leader slots are reported as children. A fork from any thread makes a child whose parent is the process.

#### `execve` from a threaded process

`jc_exec` first gives every other thread `tkill` and SIGKILL, then blocks (`S_WAIT`) and restarts the call until it is the only live thread. If the caller is not the leader, the new program then takes over the leader's slot, so the process keeps its PID: the leader gets the caller's signal mask and pending signals, the caller's slot is freed, and `g_jcur` is switched to the leader before the new image starts.

#### Futexes

`lx_futex` (`sys/linux/linux.c`) implements `FUTEX_WAIT`, `FUTEX_WAKE`, `FUTEX_REQUEUE`, `FUTEX_CMP_REQUEUE`, `FUTEX_WAKE_OP`, `FUTEX_WAIT_BITSET` and `FUTEX_WAKE_BITSET`, with or without `FUTEX_PRIVATE_FLAG`, and `FUTEX_CLOCK_REALTIME` for the bitset wait. A futex is identified by the thread group and the user address (`fx_tg`, `fx_addr`), so futexes work between the threads of one process but not between processes through shared memory.

- Wait: under the big kernel lock the call reads the word; if it differs from the expected value it returns EAGAIN. Otherwise it records the wait in the thread (`fx_wait = 1`, address, bitset, and `fx_dead` for a timeout: relative for `FUTEX_WAIT`, absolute for `FUTEX_WAIT_BITSET`), and blocks in `S_WAIT` with `wake` set to the timeout tick (or far in the future).
- Because blocked calls restart (section 5.6), the thread re-enters `lx_futex` when it is woken. With `fx_wait` already set it returns 0 if `fx_woken` is set, ETIMEDOUT (110) once `fx_dead` has passed, or EINTR if a signal is deliverable; otherwise it blocks again.
- Wake: `jc_futex_wake(tg, addr, n, bits)` marks up to `n` waiting threads of the group on that address `fx_woken` and sets their `wake` to 0, so `jc_runnable` picks them at once, and kicks idle cores. Requeue moves waiters to the second address by changing `fx_addr`.
- `FUTEX_WAKE_OP` performs the operation on the second word with an atomic compare-exchange loop, because threads on other cores may change that word in user mode at the same time.
- Delivering a signal handler clears `fx_wait`, so after `sigreturn` the restarted call checks the word again.
- No wake-up can be lost: checking the word and recording the wait happen under the big kernel lock, and a waker changes the word in user mode before calling `FUTEX_WAKE`, which also takes the lock.

#### Thread-safety of the rest of the kernel

All kernel code still runs under the big kernel lock (section 4.11), so two threads in system calls at the same time are serialized. Threads only run in parallel in user mode. The changes that make that safe are the TLB rules in section 4.15 and the leader-held process state above.

#### Tests

`featest threads` (section 12.2) checks a mutex-protected counter in 4 threads, thread-local storage, a condition variable with one producer and two consumers, `getpid`/`gettid`, threads spread over several cores, `pthread_kill`, a process signal reaching a thread that does not block it, semaphores, a blocking `read` woken from another thread, an `mmap` seen by all threads, `fork` from a thread, `pthread_cond_timedwait` timing out, 200 short threads, `exit` ending all threads, the main thread exiting first, SIGTERM to a threaded process, and `execve` from a threaded process. The ports xz (`-T`), zstd (`-T`) and SQLite are now built with thread support.

---

### 5.16 Interval timers and alarm

`alarm` (37), `setitimer` (38) and `getitimer` (36) are implemented for `ITIMER_REAL`. `ITIMER_VIRTUAL` and `ITIMER_PROF` are accepted and reported as disarmed, but never fire.

- The timer belongs to the process: `it_next` (tick of the next expiry, 0 = off) and `it_int` (interval in ticks) live in the leader. Times are converted with `lx_tv_ticks`, rounding up to the 10 ms tick.
- `jc_itimer(i)` runs at the top of `jc_runnable`, which the scheduler calls for every slot on every pick. When `g_ticks` has reached `it_next` it sends SIGALRM (14) with `jc_signal_proc` and either reloads the timer (`it_next += it_int`, skipping missed periods) or turns it off.
- `alarm(sec)` returns the seconds left on the previous timer (rounded up) and sets a one-shot timer; `alarm(0)` cancels it.
- `fork` clears both fields in the child; `execve` keeps them, as Linux does.
- The default action of SIGALRM ends the process ("Alarm clock" in the shell).

## Part 6: Linux compatibility, libc and porting applications

NoovexOS 64 runs two kinds of user programs side by side:

| Kind | System-call gate | C library | Link type | Loaded at | Source |
|---|---|---|---|---|---|
| Native ("legacy") | `int $0x80`, number in `rax`, args in `rbx rcx rdx rsi rdi` | nvxlibc (`usr/libc`) or the header-only `usr/src/user.h` | static `ET_EXEC`, `usr/src/user.ld` | `0x40000000000` | `usr/src/*.c` |
| Linux ABI, static | `syscall` instruction, Linux x86-64 numbers and registers | musl 1.2.6 (`third_party/musl`), `libc.a` | static-PIE `ET_DYN` | `0x40000400000` (`ELF_PIE_BASE`) | `usr/musl/*.c` test programs |
| Linux ABI, dynamic (v8.97) | same | musl 1.2.6 shared, `/lib/ld-musl-x86_64.so.1` | PIE `ET_DYN` with `PT_INTERP` | program at `0x40000400000`, dynamic linker at `0x7F000000000` | `third_party/*` ports, `dyntest` |

Both run under the same job scheduler (`core/proc/jobs.c`, `struct jproc`), share the same VFS file table and the same signal machinery. This part documents the Linux layer (`sys/linux/linux.c`), the two libcs, the ported programs and how to add your own.

---

### 6.1 How a Linux system call reaches the kernel

`arch/x86_64/user/ring3.c: lsys_init()` programs the `syscall` MSRs:

```c
cpu_wrmsr(0xC0000080,cpu_rdmsr(0xC0000080)|1ULL);
cpu_wrmsr(0xC0000081,(0x08ULL<<32)|(0x10ULL<<48));
cpu_wrmsr(0xC0000082,(u64)lsys_entry);
cpu_wrmsr(0xC0000084,0x47700ULL);
```

EFER.SCE is set, STAR selects the kernel/user segments, LSTAR points at `lsys_entry`, and FMASK `0x47700` clears IF, TF, DF, AC and NT on entry.

`lsys_entry` (`arch/x86_64/boot/userspace.S`) does `swapgs`, saves the user `rsp` in `%gs:8`, loads the per-CPU kernel stack from `%gs:0`, then pushes a fake interrupt frame (`ss=0x23`, user rsp, `r11`=rflags, `cs=0x1B`, `rcx`=rip) followed by all general registers. The result is the same `struct sysregs` layout that `int 0x80` produces, so blocking, fork, signal delivery and preemption reuse one code path. It calls `lsys_c` and returns with `iretq` (not `sysretq`).

```c
void lsys_c(struct sysregs* r){ bkl_lock(); PCPU->syscalls++; linux_syscall(r); bkl_unlock(); }
```

Every Linux system call runs under the big kernel lock (`arch/x86_64/interrupts/interrupts.c`).

`linux_syscall()` (end of `sys/linux/linux.c`):

```c
void linux_syscall(struct sysregs* r){
    if(!g_js_running||g_jcur<0){
        if(r->rax==60||r->rax==231){ r->rbx=r->rdi; r->rax=0; syscall_dispatch(r); return; }
        if(r->rax==1&&(r->rdi==1||r->rdi==2)&&r->rdx<4096&&jc_umapped(r->rsi,r->rdx)){ ... g_r3out ... }
        r->rax=(u64)-LX_ENOSYS; return; }
    struct jproc* p=&g_jp[g_jcur]; p->lx=1;
    long v=lx_dispatch(r);
    r->rax=(u64)v;
    if(jc_deliverable(p))jc_yield_sys(r);
}
```

* Outside the job scheduler (the old single-run `elf_load_run` path) only `exit`/`exit_group` and `write` to fd 1/2 work.
* Inside it, the process is marked `p->lx=1`. From then on `jc_syscall()` (`core/proc/jobs.c`) answers any `int 0x80` from that process with SIGSYS (31) and `-38`: a process uses one ABI or the other, never both. A static-PIE image gets `p->lx=g_elf_pie=1` already at load time (`jc_create`, `jc_exec`).
* After the call, a pending deliverable signal makes the process yield so `jc_enter()` to `jc_deliver()` can run the handler.

#### Return values and errno

`lx_dispatch()` returns a `long`. Success is a non-negative value; failure is `-errno` using Linux numbers, which musl turns into `errno`. The constants are at the top of `linux.c` (`LX_EPERM` 1 ... `LX_ENOTSUP` 95). A few call sites use raw numbers: `-23` (ENFILE, global open-file table full in `lx_open`), `-27` (EFBIG, write past `FS_CAP`), `-97` (EAFNOSUPPORT, `socket`), `-110` (ETIMEDOUT, `futex` wait with timeout).

#### Blocking and restarting

Blocking calls never sleep inside the kernel. They call `jc_block(r,state,wake)` (`jobs.c`), which saves the registers into `p->ctx` and longjmps back to the scheduler:

* `S_WAIT` (pipe/tty/mouse empty, `wait4`, `poll`): `jc_block` sets `ctx.rip-=2`, so the 2-byte `syscall` instruction is executed again when the process is next picked. The handler simply re-evaluates.
* `S_SLEEP` (`nanosleep`, `pause`, `rt_sigsuspend`): the return value is pre-loaded into `rax`; `jc_enter()` replaces it by `-4` (EINTR) if the process is woken before `p->wake` because a signal arrived.

Ticks are 10 ms (`lx_sleep_ns` rounds up to `10000000` ns units), so sleep and poll timeouts have 10 ms granularity.

#### User pointer checking

```c
static int lx_ok(u64 p,u64 n){ return jc_umapped(p,n); }
```

`jc_umapped()` (`jobs.c`) first requires `jc_uok()`: the range must lie in the user window `[0x40000000000, 0x80000000000)` without wrapping. It then walks the current page tables page by page; a non-present lazy page or a read-only copy-on-write page is resolved through `vm_fault_user(cr3,v,1)` (so a later kernel write cannot fault), and the final PTE must be present and user (`(*e&5)==5`). Helpers built on it:

| Helper | Purpose |
|---|---|
| `lx_str(p,out,cap)` | copy a NUL-terminated user string, re-checking at each page boundary; `-EFAULT` / `-ENAMETOOLONG` |
| `lx_put(p,src,n)` / `lx_get(dst,p,n)` | checked copy out / in |
| `lx_vf(fd)` | fd to `struct vfile*` through the per-process table `fdt()`; NULL if closed |
| `lx_fd_install(vi,cloexec,min)` | lowest free fd at least `min`, sets the per-fd close-on-exec byte in `fdce()` |

#### Path resolution and dirfd

`lx_path(dirfd,up,out,empty_ok)` turns every path into an absolute one before it reaches the file system:

1. Copy the string with `lx_str` (max `LX_PATH` = 256). An empty string is `-ENOENT` unless `empty_ok`.
2. Any component longer than 63 characters to `-ENAMETOOLONG` (names in `FsFile.name[64]`).
3. Absolute paths are copied as-is. Otherwise the base node is the current directory `g_dcwd`, or, if `dirfd != AT_FDCWD (-100)`, the directory node of that fd, which must be a directory vfile (`dev == LX_DIRDEV == 11`, else `-ENOTDIR`; closed fd to `-EBADF`).
4. `dt_path(node,...)` gives the base path; the relative part is appended.

`lx_noent(p)` refines a failed lookup: it walks the prefixes and returns `-ENOTDIR` if a prefix is a regular file (e.g. `open("file.txt/x")`), else `-ENOENT`. `musltest` checks both.

### 6.2 File-related details

#### `open` / `openat` (`lx_open`)

Flags decoded: access mode `flags&3`, `O_CREAT 0x40`, `O_EXCL 0x80`, `O_TRUNC 0x200`, `O_APPEND 0x400`, `O_NONBLOCK 0x800` (kept in `vfile.flags`), `O_DIRECTORY 0x10000`, `O_CLOEXEC 0x80000`. Device paths (`dev_id()` in `fs/vfs/ramfs.c`) and `/proc` (`vfs_lookup`) bypass the file checks. A directory opened read-only becomes a `LX_DIRDEV` vfile whose `fs_idx` is the directory node. Permission checks use `ux_perm_ok(k,2)` for write/truncate and `ux_perm_ok(k,4)` for read. New files get `0100000|(mode&07777&~umask)`. `O_TRUNC` on disk triggers `fs_sync`.

Limits from `fs/vfs/ramfs.c` and `jobs.h`: `FDMAX 256` fds per process, `VMAXOPEN 1024` vfile slots for the whole system (the console shares one slot), and `PIPE_MAX 128` pipes system-wide, each with a 4096-byte buffer (`jc_pipe_new(4096,...)`). Before v8.97 these were 32, 32 and 12.

#### Read and write

`lx_read`/`lx_write` cap one transfer at `LX_IOMAX` (1 MiB), check the buffer with `lx_ok`, then call `vfs_read`/`vfs_write`. A `JS_BLOCK` result (`-1000`) means "would block": `-EAGAIN` for `O_NONBLOCK`, otherwise `jc_block(S_WAIT)`. `O_APPEND` moves `v->off` to the file end before each write. Writes that start at or past `FS_CAP-1` (`FS_CAP 0x40000000`, 1 GiB) fail with EFBIG. Successful writes only mark the file dirty (`lx_persist` to `g_fs_dirty`); it is written back when the last descriptor closes (`vf_unref` to `fs_wb_mark`), on `fsync`/`fdatasync`, or on `sync` (162 to `nvx3_sync_all`).

`readv`/`writev` (`lx_rwv`, up to 1024 iovecs) stop early instead of blocking once some data has moved. `pread64`/`pwrite64` (`lx_prw`) only work on regular files (`-ESPIPE` otherwise) and temporarily swap `v->off`. `sendfile` (`lx_sendfile`) needs a regular-file source and copies through a 64 KiB static buffer when source and destination are the same file.

#### `struct stat` (`lx_stat_out`)

The kernel fills the 144-byte Linux x86-64 `struct stat` from the 10-word array `ux_stat2()` returns (`o[0]` mode, `o[1]` uid, `o[2]` gid, `o[3]` nlink, `o[4]` size, `o[5]` inode, `o[6]` mtime, `o[7]` atime, `o[8]` ctime, `o[9]` is-directory):

| Offset | Field | Value |
|---|---|---|
| 0 | `st_dev` | `0x4e5658` ("NVX") |
| 8 | `st_ino` | `o[5]` (+ `dev_ino`) |
| 16 | `st_nlink` | `o[3]` |
| 24 / 28 / 32 | `st_mode` / `st_uid` / `st_gid` | `o[0]` / `o[1]` / `o[2]` |
| 40 | `st_rdev` | char devices only: `(1<<8) \| (ino & 0xFF)` |
| 48 | `st_size` | `o[4]` |
| 56 | `st_blksize` | 4096 |
| 64 | `st_blocks` | `(size+511)/512` |
| 72 / 88 / 104 | `st_atime` / `st_mtime` / `st_ctime` | seconds only, nanoseconds are 0 |

`lx_fstat` synthesises values for non-file descriptors: pipes are FIFOs `0010600` with inode `5000+pipe`, `/proc` files `0100444` with their generated size, other devices `0020666` (tty `0020620`) with inode `20000+dev`, regular files use inode `fs_idx+1` and `nlink 0` once unlinked (orphan).

#### `getdents64` (`lx_getdents`)

Only `LX_DIRDEV` fds (`-ENOTDIR` otherwise). Each record is a `linux_dirent64`: `d_ino` (u64), `d_off` (= index+1), `d_reclen` = `(19+len+1+7)&~7`, `d_type`, `d_name`. Entries 0 and 1 are synthesised `.` and `..` (`d_type 4`, DT_DIR); the rest come from `k_readdir_at`. The inode is taken from `ux_stat2` so it matches `stat()`. `v->off` is the entry index, which is why `lseek(dirfd, n, SEEK_SET)` is accepted on directories (rewinddir/seekdir). The old `getdents` (78) returns ENOSYS.

#### `ioctl` (`lx_ioctl`)

| Request | Name | Devices | Behaviour |
|---|---|---|---|
| `0x5451` / `0x5450` | FIOCLEX / FIONCLEX | any | set/clear close-on-exec |
| `0x5421` | FIONBIO | any | toggle `0x800` in `vfile.flags` |
| `0x541B` | FIONREAD | any | bytes in pipe / tty line buffer / file remainder; on a socket, bytes buffered (TCP) or the size of the next datagram (UDP) |
| `0x5401` | TCGETS | tty (dev 5) | the console's 36-byte termios `g_tios` (`shell/vt.c`). Default: iflag `0x500` (ICRNL, IXON), oflag `5` (OPOST, ONLCR), cflag `0xBF`, lflag `0x8A3B` (ISIG, ICANON, ECHO, ECHOE, ECHOK, ECHOCTL, ECHOKE, IEXTEN), standard `c_cc` |
| `0x5402-0x5404` | TCSETS/TCSETSW/TCSETSF | tty | store the new termios; TCSETSF also discards typed input. Section 11.12 lists what each flag does |
| `0x540B` | TCFLSH | tty | queue 0 or 2 discards typed input |
| `0x5413` / `0x5414` | TIOCGWINSZ / TIOCSWINSZ | tty | rows `trows` (default 25), cols `tcols` (default 80) / no-op |
| `0x540F` / `0x5410` | TIOCGPGRP / TIOCSPGRP | tty | foreground group `g_tty_fg` (set needs session 1) |
| `0x5429` | TIOCGSID | tty | returns 1 |
| `0x540E` | TIOCSCTTY | tty | no-op |
| `0x5000` / `0x5001` | SNDCTL_DSP_RESET / SYNC | `/dev/dsp` (dev 12) | no-op |
| `0xC0045002` | SNDCTL_DSP_SPEED | dsp | returns the real rate (`g_snd.rate`, default 48000); the request is not applied |
| `0xC0045005` / `0x8004500B` | SNDCTL_DSP_SETFMT / GETFMTS | dsp | `0x10` (AFMT_S16_LE) |
| `0xC0045006` / `0xC0045003` | SNDCTL_DSP_CHANNELS / STEREO | dsp | 2 / 1 |
| `0x8010500C` | SNDCTL_DSP_GETOSPACE | dsp | `{free/4096, 32, 4096, free}` from `g_snd.free_bytes()` |
| `0xC004500A` | SNDCTL_DSP_SETFRAGMENT | dsp | no-op |

Any other request on the tty or dsp is `-EINVAL`; on any other device it is `-ENOTTY` (this is what makes `isatty()` false for files).

#### `fcntl` (`lx_fcntl`)

`F_DUPFD`(0) and `F_DUPFD_CLOEXEC`(1030), `F_GETFD`/`F_SETFD` (1/2, the `fdce()` byte), `F_GETFL`(3), `F_SETFL`(4, only `O_APPEND|O_NONBLOCK` change), `F_GETLK`(5, always reports `F_UNLCK`), `F_SETLK`/`F_SETLKW` (6/7, succeed without locking), 1025 and 1031 return 0, `F_GETPIPE_SZ`(1032) returns 4096. Anything else is `-EINVAL`.

### 6.3 poll/select, time, memory, processes, signals

poll / ppoll / select / pselect6. `lx_poll_one(fd,ev)` computes readiness per device: pipes (`dev 6` read end: POLLIN when data, POLLHUP when no writers; `dev 7` write end: POLLOUT if space, POLLERR without readers), tty (`dev 5`: always POLLOUT, POLLIN when input is buffered and the caller is in the foreground group), mouse (`dev 10`: POLLIN when packets are queued), everything else is always readable and writable; a closed fd gives POLLNVAL (`0x20`). With nothing ready, `lx_wait_deadline` stores a wake tick in `p->pollwake` and the call blocks with `S_WAIT`; a deliverable signal returns `-EINTR`. `select` reads up to 1024 bits per set (`lx_select` copies each set into a 128-byte array, so descriptors up to `FDMAX-1` work; before v8.97 only the first 64 were looked at) and never reports exceptional conditions. Negative timeouts are `-EINVAL`; timeouts above 10^8 s are clamped.

Time. `lx_now_ns()` anchors `hr_ns()` to `k_realtime_ms()` once. `clock_gettime` (`lx_clock`): clocks 0, 5, 8, 11 (REALTIME family) return wall time; 1, 2, 3, 4, 6, 7, 9 return `hr_ns()`; others `-EINVAL`. `clock_getres` reports 1 ns if `hr_precise()` else 10 ms. `gettimeofday` (96) and `time` (201) use the same clock; the timezone is zero. `clock_nanosleep` supports `TIMER_ABSTIME` (flag 1). `getrusage` (98) reports the tick count as user time; `times` (100) returns `g_ticks`.

Address space. The user window and the regions `linux.c` and `elf.c` assume:

| Range | Use |
|---|---|
| `0x40000000000` ... | `ET_EXEC` images (nvxlibc, link address in `user.ld`) |
| below `0x40000101000` | user stack, up to `JS_STK_PAGES` = 256 pages (1 MiB), demand-mapped (`USTK_V`; the first 16 pages are always reserved, `ELF_STK_LO`) |
| `0x40000400000`, `0x40002000000` | PIE images, static or dynamic (`ELF_PIE_BASE` ... `ELF_IMG_TOP`) |
| `0x40002000000`, `0x40004000000` | `brk` heap (`lx_brk`) |
| `0x40004000000`, `0x80000000000` | `mmap` area (`g_mmap_next`, `lx_find_gap`); shared libraries loaded by the dynamic linker and thread stacks go here |
| `0x7F000000000` | the dynamic linker of a dynamic program (`ELF_INTERP_BASE`) |

`lx_mmap` rejects `len==0`, unaligned offsets and lengths over 1 GiB. Anonymous mappings (`MAP_ANONYMOUS 0x20`) are lazy (`vm_map_lazy`), so an 8 MiB mapping only consumes the pages that are touched. File mappings must be on a regular file (`-ENODEV` otherwise, e.g. `/dev/fb0`) and are private copies filled at map time; writes never reach the file. `prot` is ignored (all pages user-RW). `MAP_FIXED` (0x10) unmaps the target first and refuses the stack region. Without it, `lx_find_gap` reuses freed holes before extending `g_mmap_next`. `mprotect`, `msync`, `madvise`, `mlock*` return 0; `mremap` returns `-ENOMEM` (musl's `realloc` then copies); `mincore` is ENOSYS. All user pages come from one frame pool shared by every process: 32 MiB at `PF_BASE` plus a range taken from the heap that grows with RAM, up to 2 GiB more (section 3.7); `pframe_free_count()` gates `mmap`/`brk`.

Processes.

```c
static long lx_clone(struct sysregs* r,u64 flags,u64 stack,u64 ptid,u64 ctid,u64 tls){
    if((flags&0x80000ULL)&&tls>=0x800000000000ULL)return -LX_EPERM;
    if(stack&&!jc_uok(stack-8,8))return -LX_EINVAL;
    if(flags&0x10000ULL){                                   /* CLONE_THREAD */
        if((flags&0x900ULL)!=0x900ULL)return -LX_EINVAL;    /* needs CLONE_VM and CLONE_SIGHAND */
        int ci=jc_clone_thread(r,stack,tls,(flags&0x80000ULL)!=0); ...
        if(flags&0x200000ULL)c->ctid=ctid;                  /* CLONE_CHILD_CLEARTID */
        ... CLONE_CHILD_SETTID, CLONE_PARENT_SETTID ...
        return tid; }
    int pid=jc_fork(r); ...                                  /* everything else is a fork */
```

With `CLONE_THREAD`, `clone` creates a thread that shares the address space, descriptors and signal handlers (section 5.15). Without it, `clone` forks: `CLONE_VM` and `CLONE_VFORK` are accepted but the child gets a copy-on-write copy from `vm_clone_user`; a child stack, `CLONE_SETTLS`, `CLONE_CHILD_CLEARTID` and `CLONE_PARENT_SETTID` are honoured. `fork` (57) and `vfork` (58) are `clone(0)`. `execve` (`lx_execve`) copies up to `XA_MAX` = 32 argv and 32 envp strings into 2048-byte buffers (`XA_CAP`), rejects names over 63 characters or containing spaces, resolves the program with `jc_find_prog` (6.6) and calls `jc_exec`, which frees the old address space, closes close-on-exec fds, resets caught signal handlers, `fsbase`, `brk` and the mmap cursor. `wait4` supports `WNOHANG|WUNTRACED|WCONTINUED` (`opts&0xB`) and zero-fills `rusage`; with no matching child it is `-ECHILD`. `exit`/`exit_group` store `(code&0xFF)<<8`.

Signals. The kernel keeps signals 1-31 in 32-bit masks where bit *s* is signal *s*; Linux uses bit *s-1*. `lx_k2l` (`k>>1`) and `lx_l2k` (`l<<1`) convert. `rt_sigaction` reads/writes the 32-byte kernel `sigaction` (`handler, flags, restorer, mask`), keeps `SA_RESTORER` (`0x04000000`) restorer addresses, refuses SIGKILL/SIGSTOP, and silently accepts real-time signals 32-64 without effect (`kill` with signal at least 32 also returns 0). `jc_push_handler` builds the frame: a copy of `regs_pt` plus the saved mask, then the restorer address as return address; with `SA_SIGINFO` (flag 4) a 128-byte `siginfo` containing only `si_signo` is passed in `rsi`. `rt_sigreturn` (`lx_sigreturn`) reads that frame from the user `rsp`, forces user segments and sanitises `rflags`, and kills the process with SIGSEGV if the frame or the return `rip` is outside the user window. `rt_sigsuspend` (130) swaps the mask and sleeps; `sigaltstack` reports `SS_DISABLE`. `alarm`, `setitimer` and `getitimer` work for `ITIMER_REAL` (section 5.16). `tgkill` and `tkill` send a signal to one thread; `kill` sends it to the process, and the kernel picks a thread that does not block it (section 5.15). `futex` is described in section 5.15.

Identity and system info. `uname` (`lx_uname`) returns `NoovexOS`, the hostname, release `8.97`, version `NoovexOS 64 v8.97`, machine `x86_64`, domain `(none)`. `sched_getaffinity` (204) returns an 8-byte mask with one bit per online core (`g_cores_online` when `g_smp_on`), which is what musl's `sysconf(_SC_NPROCESSORS_ONLN)` counts; `sched_setaffinity` is a no-op. `getcpu` (309) returns `cpu_id()` and node 0. `getrlimit`/`prlimit64` report `RLIMIT_STACK` = 1 MiB and `RLIMIT_NOFILE` = 256, everything else unlimited. `arch_prctl` supports `ARCH_SET_FS` (0x1002, also loads the MSR through `cpu_set_fsbase` and `lsys_fsbase`) and `ARCH_GET_FS` (0x1003), this is musl's TLS. `getrandom`, `/dev/random` and `/dev/urandom` return output of the kernel's ChaCha20 generator `csprng_bytes` (`lib/crypto/nvxcrypto2.c`), seeded from RDSEED or RDRAND when the CPU has them and from TSC timing jitter. Before v8.97 they were an xorshift or LCG sequence, which is not good enough for TLS keys; the seeding code also used RDSEED without checking CPUID, which faulted on CPUs without it.

### 6.4 Complete Linux system-call table

Every `case` in `lx_dispatch()`; 190 numbers are handled, anything else returns `-ENOSYS`.

| Nr | Name | Notes |
|---|---|---|
| 0 | read | `lx_read`, 1 MiB cap, blocks on pipes/tty/mouse |
| 1 | write | `lx_write`, EPIPE/ENOSPC/EACCES/EIO mapping |
| 2 | open | `lx_open(AT_FDCWD,...)` |
| 3 | close | `lx_close` |
| 4 | stat | follows symlinks |
| 5 | fstat | `lx_fstat` |
| 6 | lstat | no follow |
| 7 | poll | `lx_poll`, ms timeout |
| 8 | lseek | `-ESPIPE` on tty/pipes; dirs accept SEEK_SET only; `/dev/fb0` size is `W*H*4` |
| 9 | mmap | `lx_mmap` |
| 10 | mprotect | returns 0, no effect |
| 11 | munmap | `lx_munmap`, refuses the stack |
| 12 | brk | `lx_brk`, heap window 32 MiB |
| 13 | rt_sigaction | `lx_sigaction` |
| 14 | rt_sigprocmask | `lx_sigprocmask`, sigsetsize must be 8 |
| 15 | rt_sigreturn | `lx_sigreturn` |
| 16 | ioctl | `lx_ioctl` (table in 6.2) |
| 17 / 18 | pread64 / pwrite64 | `lx_prw`, regular files only |
| 19 / 20 | readv / writev | `lx_rwv`, at most 1024 iovecs |
| 21 | access | `lx_access` |
| 22 | pipe | `lx_pipe(...,0)` |
| 23 | select | timeval to ms |
| 24 | sched_yield | `jc_yield_sys` |
| 25 | mremap | `-ENOMEM` |
| 26 | msync | 0 |
| 27 | mincore | ENOSYS |
| 28 | madvise | 0 |
| 32 | dup | lowest free fd |
| 33 | dup2 | `lx_dup3(...,-1)` |
| 34 | pause | sleeps until a signal, returns EINTR |
| 35 | nanosleep | `lx_nanosleep` |
| 36 / 38 | getitimer / setitimer | `ITIMER_REAL` (section 5.16); the other two are accepted and never fire |
| 37 | alarm | one-shot `ITIMER_REAL`, returns the seconds left on the old timer |
| 39 | getpid | |
| 40 | sendfile | `lx_sendfile` |
| 41 | socket | `lx_socket`: `AF_INET` TCP/UDP to fd on device 14; other families `-97` EAFNOSUPPORT |
| 42 | connect | `lx_connect` to `lxs_connect` (blocking, or EINPROGRESS) |
| 43 | accept | `lx_accept` (section 9.7.4) |
| 44 / 45 | sendto / recvfrom | `lx_sendto` / `lx_recvfrom`; `MSG_PEEK`, `MSG_DONTWAIT`, `MSG_NOSIGNAL` |
| 46 / 47 | sendmsg / recvmsg | `lx_msg`, at most 1024 iovecs, 64 KiB per call, no control data |
| 48 | shutdown | `lxs_shutdown` |
| 49 | bind | `lxs_bind` (port only) |
| 50 | listen | `lxs_listen`, backlog capped at 32 |
| 51 / 52 | getsockname / getpeername | `lx_sockname` |
| 53 | socketpair | EOPNOTSUPP |
| 54 / 55 | setsockopt / getsockopt | set ignored; get `SO_ERROR`, `SO_TYPE`, buffer sizes |
| 56 | clone | `lx_clone`: threads with `CLONE_THREAD`, otherwise fork |
| 57 / 58 | fork / vfork | `lx_clone(r,0,...)` |
| 59 | execve | `lx_execve` |
| 60 | exit | ends the calling thread (`jc_thread_exit`); the last thread ends the process with status `(a1&0xFF)<<8` |
| 61 | wait4 | `lx_wait4` |
| 62 | kill | sig 0 probes; uid check via `g_kill_check` |
| 63 | uname | `lx_uname` |
| 72 | fcntl | `lx_fcntl` |
| 73 | flock | 0 for a valid fd, no locking |
| 74 / 75 | fsync / fdatasync | `k_fsync` |
| 76 / 77 | truncate / ftruncate | `k_truncate` / `k_ftruncate` (needs a writable fd) |
| 78 | getdents | ENOSYS (use 217) |
| 79 | getcwd | `-ERANGE` if too small |
| 80 / 81 | chdir / fchdir | sets `g_dcwd` |
| 82 | rename | `lx_rename` (directory-over-empty-directory supported) |
| 83 / 84 | mkdir / rmdir | `lx_mkdir` / `lx_rmdir` |
| 85 | creat | `open(O_CREAT\|O_WRONLY\|O_TRUNC)` |
| 86 | link | `ux_link`, new name at most 51 chars |
| 87 | unlink | `lx_unlink`, EISDIR for directories |
| 88 / 89 | symlink / readlink | `ux_symlink` / `ux_readlink` |
| 90 / 91 | chmod / fchmod | `ux_chmod` / `lx_fchmod` |
| 92 / 94 | chown / lchown | `ux_chown` |
| 93 | fchown | validates fd only |
| 95 | umask | |
| 96 | gettimeofday | tz zero |
| 97 | getrlimit | `lx_rlimit` |
| 98 | getrusage | ticks as user time |
| 99 | sysinfo | uptime, RAM, free frames, process count |
| 100 | times | returns `g_ticks` |
| 102 / 107 | getuid / geteuid | `g_uid` |
| 104 / 108 | getgid / getegid | `g_gid` |
| 105 / 106 | setuid / setgid | only root may change to another id |
| 109 | setpgid | same session, self or child |
| 110 / 111 | getppid / getpgrp | |
| 112 | setsid | EPERM for a group leader |
| 113 / 117 | setreuid / setresuid | uses the effective id argument |
| 114 / 119 | setregid / setresgid | uses the effective id argument |
| 115 | getgroups | 0 groups |
| 116 | setgroups | root only, no effect |
| 118 / 120 | getresuid / getresgid | all three = current id |
| 121 / 124 | getpgid / getsid | |
| 127 | rt_sigpending | pending & blocked |
| 130 | rt_sigsuspend | |
| 131 | sigaltstack | reports SS_DISABLE |
| 132 | utime | times argument ignored |
| 133 | mknod | EPERM |
| 135 | personality | 0 |
| 137 / 138 | statfs / fstatfs | `ux_statfs("/")` |
| 140 / 141 | getpriority / setpriority | 20 / 0 |
| 142-147 | sched_setparam ... sched_get_priority_min | 0 (143 writes priority 0) |
| 148 | sched_rr_get_interval | ENOSYS |
| 149-152 | mlock, munlock, mlockall, munlockall | 0 |
| 157 | prctl | PR_SET_NAME (15) / PR_GET_NAME (16) on `p->cmd`, else EINVAL |
| 158 | arch_prctl | ARCH_SET_FS / ARCH_GET_FS |
| 160 | setrlimit | 0, ignored |
| 161 | chroot | EPERM |
| 162 | sync | `nvx3_sync_all` |
| 165 / 166 | mount / umount2 | `ux_mount` / `ux_umount` (USB FAT at `/mnt/usb`, root) |
| 169 | reboot | EPERM |
| 170 | sethostname | root, at most 31 chars |
| 186 | gettid | the thread ID |
| 191-199 | getxattr ... fremovexattr | ENOTSUP (188-190 setxattr family: ENOSYS) |
| 200 / 234 | tkill / tgkill | signal one thread (`jc_signal_thread`) |
| 201 | time | |
| 202 | futex | `lx_futex`: WAIT, WAKE, REQUEUE, CMP_REQUEUE, WAKE_OP, WAIT_BITSET, WAKE_BITSET (section 5.15) |
| 203 / 204 | sched_setaffinity / sched_getaffinity | no-op / online-core mask |
| 217 | getdents64 | `lx_getdents` |
| 218 | set_tid_address | stores the clear-child-tid address, returns the thread ID |
| 221 | fadvise64 | 0 |
| 228 / 229 / 230 | clock_gettime / clock_getres / clock_nanosleep | |
| 231 | exit_group | `jc_finish`: ends every thread |
| 257 | openat | |
| 258 | mkdirat | |
| 260 | fchownat | `AT_SYMLINK_NOFOLLOW` (0x100) honoured |
| 262 | newfstatat | `AT_EMPTY_PATH` (0x1000) to fstat |
| 263 | unlinkat | `AT_REMOVEDIR` (0x200) |
| 264 / 316 | renameat / renameat2 | renameat2 flags must be 0 |
| 265 / 266 / 267 | linkat / symlinkat / readlinkat | |
| 268 / 269 / 439 | fchmodat / faccessat / faccessat2 | |
| 270 / 271 | pselect6 / ppoll | signal mask argument ignored |
| 273 / 274 | set_robust_list / get_robust_list | stored and returned, not walked |
| 280 | utimensat | UTIME_NOW/UTIME_OMIT handled; NULL path to fd check |
| 285 | fallocate | ENOTSUP |
| 288 | accept4 | `lx_accept` with `SOCK_NONBLOCK` and `SOCK_CLOEXEC` |
| 292 / 293 | dup3 / pipe2 | O_CLOEXEC, O_NONBLOCK |
| 302 | prlimit64 | own pid only, get only |
| 309 | getcpu | |
| 318 | getrandom | kernel ChaCha20 generator, at most 1 MiB |
| 334 | rseq | ENOSYS (musl falls back) |
| 436 | close_range | `CLOSE_RANGE_CLOEXEC` (4) supported |

Not present (ENOSYS): `epoll_*`, `eventfd`, `timerfd`, `signalfd`, `inotify`, `memfd_create`, `statx`, `getdents`, process-shared futexes across different processes, `ITIMER_VIRTUAL`/`ITIMER_PROF` expiry.

### 6.5 The C libraries

#### musl 1.2.6 (Linux ABI)

`third_party/musl` is upstream musl (`VERSION` = 1.2.6). `build.sh` builds it once into `build/musl` and caches it (`lib/libc.a` and `lib/rcrt1.o` must both exist to skip):

```sh
( cd $B/musl-obj && CC=gcc ../../third_party/musl/configure --prefix=$M --disable-shared --disable-wrapper CFLAGS="-fPIE -O2" ... && make ... && make install ... )
```

The in-tree programs (`usr/musl/mhello.c`, `musltest.c`, `smptest.c`) are compiled and linked explicitly:

```sh
MCFLAGS="-fPIE -O2 -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -Wall ..."
gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf \
    $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o
```

`rcrt1.o` is musl's self-relocating start file for static-PIE: the loader only maps the image at `ELF_PIE_BASE` and musl applies its own relative relocations. Nothing in the kernel resolves symbols for user programs (the `nvx_dlload` relocation loader in `elf.c` is for kernel modules).

`tools/mcc` wraps the same recipe as a drop-in `cc`:

```sh
M=${NVX_MUSL:?NVX_MUSL not set}
GI=$(gcc -print-file-name=include)
for a in "$@"; do case "$a" in -c|-S|-E|-M|-MM) exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" "$@";; esac; done
exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -L"$M/lib" "$M/lib/rcrt1.o" "$M/lib/crti.o" "$@" "$M/lib/libc.a" "$(gcc -print-libgcc-file-name)" "$M/lib/crtn.o"
```

Compile-only invocations get just the include paths; link invocations get the full static-PIE recipe. `NVX_MUSL` must point at the installed musl (`build.sh` exports it).

Since v8.97 musl is also built a second time as a shared library (step `[musl-shared]`, cached in `build/musl-sh`): `configure --enable-shared --disable-static --syslibdir=/lib`. Its `lib/libc.so` is both the C library and the dynamic linker `/lib/ld-musl-x86_64.so.1`. `tools/mcc-dyn` is the matching compiler wrapper for dynamic programs:

```sh
exec gcc -fPIE -nostdinc -isystem "$M/include" -isystem "$GI" -pie -nostdlib \
    -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-z,noexecstack -L"$MS/lib" \
    "$MS/lib/Scrt1.o" "$MS/lib/crti.o" "$@" -lc "$(gcc -print-libgcc-file-name)" "$MS/lib/crtn.o"
```

It uses the same headers as the static build and needs `NVX_MUSL_SH` (exported by `build.sh`) as well. The ports are linked with it; the test programs `musltest`, `smptest`, `nettest` and `featest` stay static so both paths are exercised. Section 6.10 explains how a dynamic program starts.

Start-up stack. `elf_build_space()` (`core/proc/elf.c`) builds a Linux start stack in an 8 KiB image (`g_stkimg`) copied into the top two stack pages: `argc`, `argv[]`, NULL, `envp[]`, NULL, then the auxiliary vector `AT_PHDR(3) AT_PHENT(4) AT_PHNUM(5) AT_PAGESZ(6)=4096 AT_BASE(7) (0, or the dynamic linker's base) AT_FLAGS(8)=0 AT_ENTRY(9) AT_UID/EUID/GID/EGID(11-14) AT_HWCAP(16)=0 AT_CLKTCK(17)=100 AT_SECURE(23)=0 AT_RANDOM(25) AT_EXECFN(31)`, with 16 random bytes for `AT_RANDOM`. The same registers also carry `argc`/`argv` in `rdi`/`rsi` (`jc_ctx_fresh`) for nvxlibc's `crt0`. When started from the shell the environment comes from `env_pack()` (`sys/native/syscall.c`, 24 entries, defaults `PATH=/bin HOME=/home PWD=/home USER=root SHELL=/bin/nsh TERM=xterm-256color XZ_DEFAULTS=--memlimit-compress=20MiB`; `TERM` was `noovex` before the terminal understood escape sequences); after `execve` it is the caller's `envp`.

#### nvxlibc (native `int 0x80`)

`usr/libc` is the native C library. Its single source of truth is `usr/libc/src/legacy_posix.h` (the former header-only `posix.h`). `usr/libc/gen.py` generates from it:

* `src/libc.c`, the implementation with top-level `static` removed and `__sys` turned into an extern;
* `include/nvx/_all.h`, `_types.h`, `_std.h` (351 standard prototypes) and `extras.h` (457 NoovexOS helpers such as `append_file`, `base64_encode`, `arr_*`), plus the standard header names (`stdio.h`, `unistd.h`, `dirent.h`, `pthread.h`, ... each just includes `<nvx/_std.h>`).

Hand-written parts: `printf.c`, `stdlib2.c`, `stdio2.c`; `sys_nvx.c` holds the gate:

```c
long __sys(long n,long a,long b,long c,long d,long e){
    long r; register long r8 __asm__("rsi")=d; register long r9 __asm__("rdi")=e;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"r"(r8),"r"(r9):"memory");
    return r;
}
```

`crt0.S` aligns the stack and does `exit(main(argc,argv))` using the `rdi`/`rsi` the kernel set. `sys_host.c` + `crt0_host.S` let the same libc run on Linux for differential testing. Notable semantics in `legacy_posix.h`: `execve` ignores `envp`; `pthread_create` maps to the legacy `thread_create` syscall 61 and `pthread_join` is a no-op; `dlopen` always fails with "dynamic loading not supported (static linking only)"; `isatty` is native syscall 172.

`build.sh` builds it with `LFLAGS` (`-ffreestanding -nostdinc -isystem usr/libc/include -fno-pie -mcmodel=large -Os -ffunction-sections -fdata-sections`), archives `libc.a`, and links each program with `ld -nostdlib -no-pie --gc-sections -T usr/src/user.ld crt0.o prog.o [extra] libc.a`.

#### `usr/src` programs

* `user.h` programs (`user_demo`, `user_fork`, `user_app`, `user_thread`, `user_preempt`, `user_ipc`, `user_wmapp`, `user_compdemo`, `user_posix`, `user_mem`, plus `hello.c`, `proc.c`): built with `user_crt0.S` and `UFLAGS`, embedded as `user_*_elf.o`, and started by shell demo commands through `elf_load_run`/`proc_create` (e.g. `shell/commands.c:758`), not through `JS_BIN`.
* `posix.h` programs (now `posix.h` just includes `<nvx/extras.h>` and `<setjmp.h>`): `count spin sigdemo upper greet pipetest nvxbox init libc8` are built against nvxlibc and listed in `JS_BIN`. Other `posix.h` files in the directory (`captest`, `devtest`, `libc2`-`libc6`, `libctest`, `mathtest`, `nettest`, `ptest`, `unixtest`, `files`) are not built by `build.sh`; prebuilt ELFs of several exist in `usr/bin/`.

#### nvxbox

`usr/src/nvxbox.c` + `coreutils.c` form one multi-call binary (65 KB). `main` dispatches on `basename(argv[0])`: first `coreutils_main()` (head, tail, wc, sort, grep, cut, tr, uniq, seq, basename, dirname, rev, tac, nl, fold, paste, comm, cmp, md5sum, sha256sum, base64, find, yes, printenv/env; returns `-1000` if not its command), then the built-ins in `nvxbox.c` (ls, cat, echo, mkdir, rmdir, rm, mv, cp, touch, ln, chmod (octal only), chown, stat, pwd, hostname, whoami, id, uname, date, sleep, mount, umount, readlink, tee, true, false). `nvxbox CMD args` re-dispatches on `argv[1]`. Every name is a `NVXB("...")` row in `JS_BIN` pointing at the same blob.

### 6.6 How programs are embedded and found

#### Embedding

```sh
emb(){ ( cd $B && ld -r -b binary -o $1 user/$2.elf ); }
...
emb user_${p}_bin.o $p
objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${p}_bin.o
```

`ld -r -b binary` wraps the file in an object whose symbols are derived from the path: `usr/sqlite3.elf` to `_binary_user_sqlite3_elf_start` / `_end`. The rename moves the blob from `.data` to `.rodata`, which `arch/x86_64/kernel64.ld` places before `__immutable_end`, i.e. inside the region whose SHA-256 is stamped into the `NVX64SIG` build signature. Every object must also be listed in `BLOBS` in step `[4/6]`.

The musl programs, the ports and the shared C library are embedded compressed (v8.97). `embz` runs `tools/nvz.py`, which writes `NVZ1`, the 32-bit little-endian original size and a raw DEFLATE stream (zlib level 9), and wraps that under the same symbol names:

```sh
embz(){ mkdir -p $B/zz/user; python3 tools/nvz.py $B/user/$2.elf $B/zz/user/$2.elf; ( cd $B/zz && ld -r -b binary -o ../$1 user/$2.elf ); }
```

`jc_unz` (`core/proc/jobs.c`) inflates such a blob with the kernel's `pz_inflate_raw` the first time it is needed and keeps the result in a 24-entry cache, so later runs cost nothing extra. This took the kernel from 5.46 MB to 3.86 MB before the shared libc, BearSSL client and the other v8.97 programs were added; the current `build/kernel64.bin` is about 4.28 MB. The UEFI loader's limit is `0x5C0000` bytes (section 3.13).

`core/proc/jobs.c` declares each pair `extern const unsigned char _binary_user_X_elf_start[], _binary_user_X_elf_end[];` and lists it in `JS_BIN[]` (name, start, end). Several names may share one blob (`gzip/gunzip/zcat`, `xz/unxz/xzcat/lzma/unlzma/lzcat`, ...); the program sees the name in `argv[0]`. `+init` is the init program. `fs/vfs/unixfs.c:351` lists every non-`+` `JS_BIN` name as a regular file in `/bin`.

#### Lookup (`jc_find_prog`)

1. Strip a leading `/bin/`, `/usr/bin/` or `/sbin/` (`/sbin/init` to `+init`), and a trailing `.elf`; compare case-insensitively against `JS_BIN`.
   A compressed blob is inflated through `jc_unz` before it is returned.
2. Otherwise `ux_lookup(name,...)` in the file system: a file of at least 64 bytes starting with `\x7fELF` is executed directly from its in-memory data (`fsd(f)`); non-root users need an execute bit (`FS[f].mode&0111`).
3. Otherwise `fat_read_file(name, g_spawnbuf, 65536)` from the IDE FAT disk (8.3 name, at most 64 KiB).

`jc_build()` then calls `elf_build_space()`: ELF64, `EM_X86_64`, `ET_EXEC` or `ET_DYN`, at most 64 program headers, every `PT_LOAD` inside the file and inside `[0x40000000000, 0x40002000000)`, not overlapping the stack, entry inside an executable segment. Pages are copied (not mapped from the file), and up to 256 stack pages are reserved lazily. A `PT_INTERP` header loads the dynamic linker as well (section 6.10).

#### Launch from the shell

`run_cmd()` first tries `jc_launch(line)` (`shell/commands.c:180`). `jc_launch` splits up to 6 `|` stages (quotes respected), finds stages whose first word `jc_is_prog`, handles `<`, `>`, `>>`, `2>`, `2>>`, `2>&1` and a trailing `&`, creates each process stopped with `jc_create`, wires pipes, then starts the group as one job (`jc_job_new`). Spaces inside quoted arguments are encoded as `0x1F` and turned back into spaces by `elf_args_from_string`. Built-ins may stand before the programs (their output is captured and fed in through a pipe) or after them (program output is collected into a 64 KiB buffer, `COLL_CAP`, and handed to the built-in), but a program may not follow a built-in that itself reads from a program.

### 6.7 The ported programs

Since v8.97 the ports are dynamic programs linked with `tools/mcc-dyn` against the shared musl, and the ones that can use threads are built with them. `port()` builds once and caches in `build/ports-dyn`:

```sh
port(){ n=$1; shift; if [ ! -f $P/$n ]; then "$@" || { echo "PORT BUILD FAILED: $n"; exit 1; }; strip $P/$n; fi; }
```

| Command(s) | Source | Build (all via `$MCC -O2 -w`) | Notes |
|---|---|---|---|
| `sqlite3` | SQLite 3.53.4 amalgamation | `-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0`, `shell.c sqlite3.c` | thread-safe build, no extensions |
| `gzip gunzip zcat` | zlib 1.3.2 + `test/minigzip.c` | all zlib `.c` files | |
| `bzip2 bunzip2 bzcat` | bzip2 1.0.8 | `-D_FILE_OFFSET_BITS=64` | |
| `xz unxz xzcat lzma unlzma lzcat` | XZ Utils 5.8.4 | `-std=gnu11 -include third_party/xz/nvx_config.h`, explicit file list | `nvx_config.h` replaces `configure`: `ASSUME_RAM 128`, `TUKLIB_CPUCORES_SCHED_GETAFFINITY`, `TUKLIB_PHYSMEM_SYSCONF`, `MYTHREAD_POSIX` (with `stream_encoder_mt.c`, `stream_decoder_mt.c`, `outqueue.c`, `hardware_cputhreads.c`, `tuklib_cpucores.c`), so `xz -T` works; `XZ_DEFAULTS` limits compression memory to 20 MiB |
| `zstd unzstd zstdcat` | Zstandard 1.5.7 | `-DZSTD_MULTITHREAD -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM -DBACKTRACE_ENABLE=0 -DZSTD_NOTRACE` | `zstd -T` works; built through `sh -c` for the globs |
| `kilo` | kilo (antirez, BSD 2-clause), `third_party/kilo` | `-std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE` | a full-screen text editor: raw mode, escape sequences, syntax colours (section 11.12) |
| `fetch` | `usr/musl/fetch.c`, `nvxtls.c` with BearSSL (`third_party/bearssl`) | `-Wall`, linked with `build/bearssl/libbearssl.a`; rebuilt every time | HTTP and HTTPS downloader (section 6.11) |
| `qjs` | QuickJS-ng, `third_party/quickjs` (MIT) | `-D_GNU_SOURCE`, `qjs.c quickjs.c libregexp.c libunicode.c dtoa.c quickjs-libc.c gen/repl.c gen/standalone.c -lm` | JavaScript interpreter with the `std` and `os` modules and a REPL; listed in the program table in `core/proc/jobs.c` (section 13.13) |

After building, each is copied to `build/user/<n>.elf`, embedded compressed (`embz`) and renamed to `.rodata` like the musl programs. Delete `build/ports-dyn/<n>` to force a rebuild. Linking dynamically made the ports about 0.5 MB smaller in total before compression (sqlite3 1.41 MB, zstd 750 KB, xz 256 KB, gzip 96 KB, bzip2 91 KB, kilo 31 KB, fetch 186 KB).

Three other third-party components are kernel code, not user programs:

* Lua 5.5.1 (`third_party/lua`): compiled with `LUAF` (freestanding, SSE enabled) against its own mini libc `nvxlibc.c` (190 lines, output through `lua_emit`), linked as `lua_all.o`; `nvxlua.c` exposes `nvx.window/clear/rect/text/present/waitkey...` and `nvx_lua_run()` backs the shell's `lua <code>` command.
* Doom (`third_party/doom`, doomgeneric): `build_doom.sh` compiles it with SSE at 640x400, `ld -r` combines it and `objcopy --keep-global-symbol` keeps only the interface (`doomgeneric_Create`, `doomgeneric_Tick`, `DG_ScreenBuffer`, `dsh_set_*`, `webp_decode`). `apps/games/doom.c` loads the WAD from a boot module or `DOOM1.WAD`/`FREEDOOM.WAD`/... on the FAT disk (32 MiB buffer).
* NTFS (ntfs-3g, `third_party/ntfs`): built with `-Ulinux -U__linux__ ...` against `compat/` headers and `ntfslibc.c`, mkntfs with `-Dmain=mkntfs_main`; used by `fs/ntfs/ntfsk.c`.

### 6.8 Test programs

`usr/musl/mhello.c`, prints `uname()` and a float; `mhello x` exits 3; `mhello env` prints `MT_VAR` and exits 4/5. `musltest` and the host harness use these exit codes to check `execve`, `posix_spawn` and environment passing.

`usr/musl/musltest.c`, a libc and kernel self-test that also runs on real Linux as the reference. It works in `$MT_DIR/mt<pid>` (default `/tmp`) and prints `ok`/`FAIL` per check:

| Group | Exercises |
|---|---|
| `t_args`, `t_printf`, `t_malloc`, `t_tls`, `t_math`, `t_setjmp` | argv/environ, formatting, 3 MiB malloc via mmap, `__thread` (needs `arch_prctl`), libm |
| `t_files`, `t_dirs`, `t_vfs` | stdio, `pread/pwrite/ftruncate`, `O_EXCL`, file mmap, `getdents64` incl. `.`/`..`, inode match, unlinked-but-open files, `O_APPEND`, directory rename, link counts |
| `t_fork`, `t_exec`, `t_cow` | pipe/fork/waitpid, ECHILD, execve, `posix_spawn` with envp, `atexit`, copy-on-write isolation, lazy 8 MiB mapping, 40 KiB stack growth |
| `t_signals`, `t_time` | SA_SIGINFO, masks and `sigpending`, `kill`+`WTERMSIG`, EINTR from `nanosleep`, `sigsuspend`, clocks, `strftime` |
| `t_sys`, `t_errors`, `t_fixes` | `uname`, `sysconf`, `getrandom`, `/dev/null`, `dup2`, poll, and ~30 exact errno checks (EFAULT on unmapped buffers, ESPIPE, EAGAIN, EISDIR, ENOTDIR, ENOTTY ...), sendfile into a full pipe, `/proc` read far past the end, permission checks after `setuid(1000)` |

`usr/musl/smptest.c`, `smptest [workers] [rounds] [spin]` (defaults 8/24/3,000,000, max 12 workers) forks workers that spin, record `getcpu` in a CPU bitmask, write/read/verify/unlink files in `/tmp`, fork children over pipes every 4th round and test copy-on-write of a 64 KiB anonymous mapping every 5th round. Reports go back through one pipe; output goes to stdout and `/dev/kmsg`. PASS needs all reports, no errors, clean exits and, with at least 2 CPUs (`sysconf(_SC_NPROCESSORS_ONLN)`), at least two CPUs used.

`usr/musl/featest.c` (v8.97), `featest [group]` runs the checks for the features added in v8.97 and prints `featest: ok ...` or `featest: FAIL ...` lines and a `featest: done N passed, M failed` summary to stdout and `/dev/kmsg`. Groups: `fds` (200 open files, `select` and `poll` on descriptor 150, 60 pipes), `mem` (96 MiB `malloc`, two 64 MiB `mmap`s), `timers` (`alarm`, `pause`, `setitimer` with a 50 ms interval, disarming, the default action), `server` (listen/accept over loopback, non-blocking accept, `accept4` flags, refused connection, UDP over loopback), `threads` (section 5.15), `tty` (termios, window size, cursor position reports, scroll region, VMIN/VTIME, alternate screen; only when stdin is the console). `featest listen` is not part of the default run: it waits up to 60 s for a connection to port 9200 from another machine and answers it (the network check uses it).

`usr/musl/dyntest.c` (v8.97) is linked dynamically. It checks that it was started through the dynamic linker (`AT_BASE`), `dl_iterate_phdr`, `malloc` and threads in the shared libc, then writes a small library (`libnvxdemo.so`, embedded in the program) to `/tmp`, loads it with `dlopen`, looks up functions and a variable with `dlsym`, calls into it, checks that its constructor ran and that it can call back into libc, and closes it.

`usr/musl/nettest.c`, `nettest [host]` (default `noovex.test`) exercises the Linux socket layer against the test peer in `tools/tests/net/netpeer.py`: `getaddrinfo`, a 300 KB HTTP download checked byte by byte, a 100 KB TCP echo with `shutdown(SHUT_WR)`, UDP `sendto`/`poll`/`recvfrom`, a non-blocking `connect` + `poll` + `SO_ERROR`, `ECONNREFUSED` on a closed port and `EAFNOSUPPORT` for IPv6. It prints `nettest: PASS` or `nettest: FAIL` to stdout and `/dev/kmsg`. `tools/check.py --only net` runs it in QEMU.

Host harness. `tools/tests/linux/hh.c` compiles the whole kernel as a Linux program (`hk_glue.c` does `#include "kernel64.c"`), traps user `syscall`/`int 0x80` with seccomp and feeds them to `linux_syscall`/`syscall_dispatch`. It runs mhello, legacy programs, every compressor round trip, the SQLite sequence, `smptest 3 3 200000`, `musltest` (requires at least 80 `ok`), optionally `lxfuzz.c` (random system calls with hostile arguments), and then checks that no frames, vfile slots or pipes leaked. Arguments with spaces are passed with `\037` exactly as the shell does.

### 6.9 Step-by-step porting guide

#### Step 1, port a portable C command-line program with musl

1. Run `./build.sh` once so `build/musl` exists.
2. Put the sources under `third_party/<name>/` (or a single file under `usr/musl/`).
3. Build by hand to see what breaks:

```sh
cd NoovexOS-64-bit
export NVX_MUSL=$(pwd)/build/musl
tools/mcc -O2 -w -o /tmp/myprog third_party/myprog/*.c
file /tmp/myprog
```

`file` must say `static-pie linked`. Typical fixes: replace `configure` output with a hand-written config header passed via `-include` (as `third_party/xz/nvx_config.h` does), disable threads (`SQLITE_THREADSAFE=0`, no `HAVE_PTHREAD`), assembly (`ZSTD_DISABLE_ASM`), and plugin loading (`SQLITE_OMIT_LOAD_EXTENSION`).

Minimal template, the same shape as `usr/musl/mhello.c`:

```c
#include <stdio.h>
#include <sys/utsname.h>
int main(int argc,char** argv){
    struct utsname u; uname(&u);
    printf("%s on %s %s, %d args\n",argv[0],u.sysname,u.release,argc);
    return 0;
}
```

#### Step 2, add it to `build.sh` and `JS_BIN`

1. In the `[ports]` block of `build.sh` add a line after the zstd port:

```sh
port myprog $MCC -O2 -w -I$TP/myprog -o $P/myprog $TP/myprog/main.c $TP/myprog/util.c
```

2. Add `myprog` to the loop `for n in sqlite3 gzip bzip2 xz zstd kilo fetch qjs; do` so it is copied, embedded with `emb user_myprog_bin.o myprog` and moved to `.rodata`.
3. Append `user_myprog_bin.o` to `BLOBS` in step `[4/6]`.
4. In `core/proc/jobs.c`:

```c
extern const unsigned char _binary_user_myprog_elf_start[], _binary_user_myprog_elf_end[];
```

and a row before `{"+init",...}`:

```c
    {"myprog",_binary_user_myprog_elf_start,_binary_user_myprog_elf_end},
```

5. Rebuild. The program appears in `/bin` and runs as `myprog args`, in pipes and in the background. Watch the 8 MiB kernel assertion. For a single test file in `usr/musl/`, add its name to `for p in mhello musltest smptest` instead of using `port`.

#### Step 3, run it from disk instead

Embedding is optional: `jc_find_prog` falls back to the file system.

1. Get the stripped ELF onto the machine, e.g. on a FAT USB stick: as root `mount usb /mnt/usb` (`ux_mount` ignores the source and only accepts the target `/mnt/usb`), then `cp /mnt/usb/MYPROG /home/myprog` (copying makes it an NVXFS file).
2. `chmod 755 /home/myprog` (needed for non-root users).
3. Run `/home/myprog args`. A bare name works only if it is in `JS_BIN`; `/bin/` is only an alias for embedded programs.

Limits: the whole image is read from memory (`FsFile.data`), so it must fit the file layer; the IDE-FAT fallback only takes 8.3 names up to 64 KiB; the PIE image must end below `0x40002000000` ( about 28 MiB of `PT_LOAD` memory).

#### Step 4, find missing system calls and add one

A missing call shows up as `ENOSYS` (`-38`). To see which one, temporarily add a `klog(...)` of `nr` before the final `return -LX_ENOSYS;` in `lx_dispatch`. Then add a `case` to the switch in `sys/linux/linux.c`. Rules that follow from the existing code:

* Read arguments from `a1...a6` (`rdi rsi rdx r10 r8 r9`); return `-LX_E...` on error.
* Never dereference a user pointer without `lx_ok`/`lx_get`/`lx_put`/`lx_str`; turn paths into absolute ones with `PATH1(dirfd,ptr,empty_ok)`.
* To wait, call `jc_block(r,S_WAIT,g_ticks+1)` and re-check the condition when the call is restarted; for a timed sleep use `lx_sleep_ns`.
* The whole handler runs under the BKL; keep it short.

Example, `syncfs` (Linux 306), modelled on `sync` (162):

```c
    case 306: if(!lx_vf((int)a1))return -LX_EBADF;
        for(int k=0;k<FS_N;k++)g_fs_dirty[k]=0; nvx3_sync_all(); return 0;
```

Then extend `musltest.c` with a `CHECK`, rebuild, and run `musltest` in NoovexOS and on Linux to compare.

#### Step 5, port a native nvxlibc program

Use this when you want the native API (`nvx/extras.h`, native window/IPC syscalls) or a tiny binary.

```c
#include "posix.h"
int main(int argc,char** argv){
    char b[256]; long n;
    while((n=read(0,b,sizeof b))>0)write(1,b,(size_t)n);
    return argc>1;
}
```

1. Save as `usr/src/mycat.c` (`upper.c` and `greet.c` are good models).
2. Add `mycat` to `for p in count spin sigdemo upper greet pipetest nvxbox init libc8; do`. The loop compiles with `LFLAGS`, links with `user.ld`, `crt0.o` and `libc.a`, embeds `user_mycat_bin.o`, renames to `.rodata`.
3. Add `user_mycat_bin.o` to `BLOBS`, the `extern` and the `JS_BIN` row as in step 2.

Constraints: the image is linked at `0x40000000000` and must end before the stack at `0x400000F1000` ( about 964 KiB, enforced by `elf_build_space`); it may not issue `syscall` (it would switch the process to Linux mode and its next `int 0x80` gets SIGSYS); `execve` drops `envp`; threads are the legacy cooperative kind.

#### Step 6, a GUI-ish program (framebuffer, mouse, sound)

There is no X11/Wayland/SDL; a Linux program can draw on the raw screen:

| Device | Kernel id | Use |
|---|---|---|
| `/proc/fb` | procfs | text `"W H 32 PITCH\n"`, the only way to learn the resolution (no `FBIOGET_*` ioctls) |
| `/dev/fb0` | 9 | `W*H*4` bytes of 0x00RRGGBB pixels (`SCENE`, stride `SW == W`); `lseek` + `write`/`read`; `mmap` is `-ENODEV` |
| `/dev/input/mouse`, `/dev/input/mice` | 10 | 3-byte PS/2 packets: `0x08\|buttons\|sign bits`, dx, dy (positive dy is up: the desktop does `my-=dy` in `graphics/ui/input.c`); blocks when empty unless `O_NONBLOCK`; works with `poll` |
| `/dev/dsp`, `/dev/audio` | 12 | 16-bit little-endian stereo PCM at `g_snd.rate` (48 kHz by default) on HDA or AC'97 |
| `/dev/kmsg` | 13 | lines go to `klog` |

While any process holds `/dev/fb0` open (`g_fb0_open > 0`), `core/init/main.c` stops drawing the desktop and blits `SCENE` whenever the program wrote to it (`g_fb0_dirty`); closing it restores the desktop. Mouse packets are only queued while the mouse device is open (`ux_mouse_push`).

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
int main(void){
    int w=0,h=0; FILE* f=fopen("/proc/fb","r"); if(!f||fscanf(f,"%d %d",&w,&h)!=2)return 1; fclose(f);
    int fb=open("/dev/fb0",O_RDWR), ms=open("/dev/input/mice",O_RDONLY|O_NONBLOCK);
    unsigned* row=malloc((size_t)w*4); int x=w/2,y=h/2;
    for(int j=0;j<h;j++){ for(int i=0;i<w;i++)row[i]=(unsigned)((i*255/w)<<16|(j*255/h)<<8); lseek(fb,(off_t)j*w*4,SEEK_SET); write(fb,row,(size_t)w*4); }
    for(;;){
        struct pollfd p={ms,POLLIN,0}; poll(&p,1,50);
        signed char m[3]; while(read(ms,m,3)==3){ if(m[0]&1){ close(fb); return 0; } x+=m[1]; y-=m[2];
            if(x<0)x=0; if(y<0)y=0; if(x>=w)x=w-1; if(y>=h)y=h-1;
            unsigned c=0xFFFFFF; lseek(fb,((off_t)y*w+x)*4,SEEK_SET); write(fb,&c,4); }
    }
}
```

Keyboard input only arrives through the tty in line mode: `tty_key()` buffers characters until Enter and `TCSETS` is ignored, so there is no raw or non-canonical mode. Programs that need per-key input (games, editors) cannot get it from the Linux layer today.

#### Step 7, a network client

Since v8.95 ordinary socket code works unchanged. This program fetches a page
over plain HTTP:

```c
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "example.com";
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "80", &hints, &ai)) { puts("dns failed"); return 1; }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, ai->ai_addr, ai->ai_addrlen)) { perror("connect"); return 1; }
    dprintf(s, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    char buf[4096]; ssize_t n;
    while ((n = read(s, buf, sizeof buf)) > 0) fwrite(buf, 1, n, stdout);
    close(s); freeaddrinfo(ai); return 0;
}
```

Things to know:
- Run `net` (or join Wi-Fi) once before starting the program. `connect` returns
  `ENETUNREACH` while there is no link.
- DNS works without `/etc/resolv.conf`: queries to musl's default `127.0.0.1:53`
  are forwarded to the DHCP DNS server.
- For HTTPS, link the program with BearSSL and `usr/musl/nvxtls.c` as `fetch`
  does (section 6.11). It then checks certificates against the same roots as
  the kernel's `https` command.
- Server programs can `bind`, `listen` and `accept` (section 9.7.4), on the
  network card and on 127.0.0.1.

#### Step 8, common pitfalls

| Pitfall | Cause in the code | What to do |
|---|---|---|
| Out of memory / SIGKILL under load | all user pages come from one frame pool (32 MiB plus up to half the free heap, section 3.7); brk heap at most 32 MiB (musl's malloc then uses mmap) | lower working sets on machines with little RAM (cf. `XZ_DEFAULTS=--memlimit-compress=20MiB`) |
| Stack overflow in deep recursion | 256 stack pages = 1 MiB (`JS_STK_PAGES`, reported as `RLIMIT_STACK`); thread stacks are what the program asks for (musl default 128 KiB) | move large arrays to the heap |
| Futexes between processes do not wake | a futex is keyed by thread group and address | use pipes or sockets between processes |
| `EMFILE`/`ENFILE` | 256 fds per process, 1024 open files system-wide, 128 pipes | close descriptors early |
| Short reads/writes | 1 MiB per call (`LX_IOMAX`) | loop until done (stdio already does) |
| `EFBIG` | writes past `FS_CAP` (1 GiB) | keep files smaller |
| Names rejected | 63 characters per component, 256 per path, `execve` path at most 63 and no spaces | shorten paths |
| Writes to a mapped file are lost | file `mmap` is a private copy, `prot` ignored | use `write`/`pwrite` |
| Box-drawing or other non-ASCII characters show as `-`, `\|`, `+` or `?` | the console font is ASCII; `vt_out` maps common UTF-8 symbols to ASCII | use ASCII where it matters |
| IPv6-only code fails | `socket(AF_INET6)` to EAFNOSUPPORT | use `AF_INET` or `getaddrinfo` with `AF_UNSPEC` (musl falls back) |
| Real-time signals have no effect | signals at least 32 are accepted and ignored | use signals 1-31 |
| ncurses programs cannot find a terminal description | there is no terminfo database in the image | build ncurses with `--with-fallbacks=xterm-256color`, or write escape sequences directly as kilo does |
| A native program dies with SIGSYS | it executed `syscall`; `jc_syscall` rejects `int 0x80` once `p->lx` is set | do not mix ABIs in one process |
| Kernel build fails at `[6/6]` | the flat image exceeds 8 MiB | run the program from disk instead of embedding it |

---

### 6.10 Dynamic linking

Since v8.97 NoovexOS runs dynamically linked musl programs, so ports can be built the way Linux distributions build them.

#### How a dynamic program starts

1. `elf_build_space` finds `PT_INTERP` (normally `/lib/ld-musl-x86_64.so.1`) in the program and maps the program at `ELF_PIE_BASE` as usual.
2. `elf_load_interp` calls `jc_find_lib(path)` (`core/proc/jobs.c`). A name ending in `ld-musl-x86_64.so.1`, `libc.so` or `libc.musl-x86_64.so.1` returns the shared musl that is embedded in the kernel (compressed, `user_libc_so_bin.o`, inflated once through `jc_unz`). Any other path is looked up in the file system, so an interpreter installed on disk also works.
3. The interpreter's `PT_LOAD` segments are copied to `ELF_INTERP_BASE` (`0x7F000000000`). The process starts at the interpreter's entry point with `AT_BASE = 0x7F000000000`, and `AT_PHDR`, `AT_PHNUM` and `AT_ENTRY` describing the program.
4. musl's dynamic linker relocates itself, finds the program through `AT_PHDR`, and satisfies `DT_NEEDED: libc.so` with itself. Other libraries are searched in `LD_LIBRARY_PATH`, `/etc/ld-musl-x86_64.path` and `/lib:/usr/local/lib:/usr/lib`; it opens them and maps them with `mmap` on the file (a private copy, section 3.10) and `MAP_FIXED` for the segments. `dlopen` uses the same path.

The library's pages are copied for every process; nothing is shared between processes yet, so dynamic linking saves space in the image and on disk, not RAM.

#### Building a dynamic program

```sh
export NVX_MUSL=$PWD/build/musl NVX_MUSL_SH=$PWD/build/musl-sh
tools/mcc-dyn -O2 -o hello hello.c
```

For a shared library, compile with `-fPIC` against the musl headers and link with `gcc -shared -nostdlib ... -L$NVX_MUSL_SH/lib -lc`, as `build.sh` does for `libnvxdemo.so`. Copy the library to `/lib` (or set `LD_LIBRARY_PATH`) on the NoovexOS side.

#### Limits

- Only PIE (`ET_DYN`) programs can have an interpreter; a non-PIE `ET_EXEC` with `PT_INTERP` is refused.
- `mprotect` is a no-op, so RELRO does not make pages read-only.
- Text relocations work (every page is writable) but are not needed by gcc's default PIE code.

---

### 6.11 TLS for programs: BearSSL, `fetch` and the root store

#### Pieces

| Piece | Where | What it is |
|---|---|---|
| BearSSL | `third_party/bearssl` | Upstream BearSSL (master, commit 3c04036, June 2024; `VERSION.txt`), MIT licence. TLS 1.0-1.2 client and server, X.509 validation, no TLS 1.3. `tools/build_bearssl.sh` compiles its 294 files with `mcc-dyn` into `build/bearssl/libbearssl.a`, with `BR_USE_GETENTROPY` and `BR_USE_URANDOM` |
| The kernel's root store | `/etc/ssl/nvx-anchors.pem` | Written at boot by `ux_write_anchors` (`fs/vfs/unixfs.c`) from the kernel's 120 Mozilla roots (`lib/crypto/nvxroots.c`), and rewritten only when the kernel's store has changed. Each root is two PEM objects, `TRUST ANCHOR NAME` (the subject name, DER) and `PUBLIC KEY` (SubjectPublicKeyInfo), which is exactly what a trust anchor needs |
| Local CAs | `/etc/ssl/local-ca.pem` | The same file the kernel's `https` command reads: ordinary PEM certificates |
| Helper | `usr/musl/nvxtls.c`, `nvxtls.h` | `nvx_anchors_load(&a, roots, local_ca)` turns both files into an array of `br_x509_trust_anchor` (RSA and EC P-256/P-384/P-521 keys); `nvx_tls_error(err)` gives a readable message for a BearSSL error code |
| `fetch` | `usr/musl/fetch.c` | Downloads a URL over HTTP or HTTPS |

#### `fetch`

```
fetch [-k] [-q] [-v] [-s] [-o FILE] URL
```

- `URL` is `http://host[:port]/path` or `https://...`; without a scheme HTTPS is used.
- HTTPS: `br_ssl_client_init_full` with the anchors, SNI with the host name, full chain validation, host name check, and the dates against the real-time clock. For a host given as an IPv4 address, a small wrapper around BearSSL's validator (`ipcheck_vt`) checks the address against the `iPAddress` entries of the certificate's subjectAltName, because BearSSL itself only matches DNS names.
- `-k` replaces the validator with one that only extracts the server key (`nocheck_vt`): encrypted, but not authenticated.
- HTTP/1.1 with `Connection: close`; `Content-Length`, chunked transfer encoding and bodies that end when the connection closes; up to 5 redirects (301, 302, 303, 307, 308; absolute or relative `Location`).
- Messages go to standard error: the TLS version and cipher suite and whether the certificate was verified, redirects, and a final line `fetch: URL -> HTTP code, N bytes`. `-s` adds the SHA-256 of the body, `-v` the response headers and the number of trust anchors loaded, `-q` silences everything.
- Exit status: 0 success, 1 usage or bad URL, 2 name lookup or connection failure, 3 TLS or certificate failure, 4 HTTP status 400 or above or too many redirects, 5 cannot write the output.
- `NVX_ROOTS` and `NVX_LOCAL_CA` override the two file names (the host test uses them).

Examples:

```
fetch https://example.org/ > page.html
fetch -o file.tar.gz https://example.org/file.tar.gz
fetch -s -o /dev/null https://noovex.test:9443/big
```

#### Using BearSSL in your own program

1. Link with `-I third_party/bearssl/inc -I usr/musl usr/musl/nvxtls.c build/bearssl/libbearssl.a`.
2. Load the anchors: `nvx_anchors a; nvx_anchors_load(&a, "/etc/ssl/nvx-anchors.pem", "/etc/ssl/local-ca.pem");`
3. Set up the client as in BearSSL's `samples/client_basic.c`: `br_ssl_client_init_full(&sc, &xc, a.v, a.n)`, `br_ssl_engine_set_buffer`, `br_ssl_client_reset(&sc, host, 0)`, then `br_sslio_init` with `read`/`write` callbacks on the socket.
4. After a failure, `nvx_tls_error(br_ssl_engine_last_error(&sc.eng))` explains it.

The kernel's own TLS 1.3 client (`https` command, section 9.9) and BearSSL in user space are separate implementations that share the root store. BearSSL speaks TLS 1.2 at most, so a server that only accepts TLS 1.3 cannot be reached with `fetch`; most servers accept both.

### 6.12 FFmpeg and the `play` command

`play` (`usr/musl/play.c`) is a video and audio player built on FFmpeg 8.1.2. The source in `third_party/ffmpeg` is the ffmpeg-rockchip fork; its Rockchip hardware decoders only run on Rockchip ARM chips, so they are left out and everything is decoded on the CPU.

Build. `build.sh` configures FFmpeg once into `build/ffmpeg` with `tools/mcc-dyn` as a cross compiler for `x86_64-linux`, then builds the static libraries and links `play` as a dynamic musl program like the other ports (section 6.9):

| Option | Value |
|---|---|
| Libraries | `libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil` (static, PIC); no programs, filters, devices or network |
| Demuxers | `mov` (MP4, M4A, MOV), `matroska` (MKV, WebM), `ogg`, `mp3`, `aac`, `wav` |
| Decoders | `h264`, `hevc`, `vp9`, `vp8`, `aac`, `aac_latm`, `mp3float`, `opus`, `vorbis`, `pcm_s16le` |
| Speed | `--enable-x86asm` (NASM; SSE2 to AVX2, chosen at run time by CPU flags) and `--enable-pthreads` (frame and slice threads on all cores) |

The stripped program is 9.6 MB, 3.4 MB after the embedded-program compression. It did not fit the 8 MiB kernel limit, which is why v9.11 raised the limit to 15 MiB (section 2.13). Delete `build/ffmpeg` to rebuild FFmpeg and `build/ports-dyn/play` to relink the player.

Using it:

```
play video.mp4                 fullscreen with sound
play -b video.mp4              decode only, print frames per second
play -s 12.5 frame.ppm a.mkv   save the frame at 12.5 s as a PPM image
```

Keys while playing: space or `p` pauses, left/right seek 10 s, up/down seek 60 s, `q` or Esc quits.

How it works:

| Part | Implementation |
|---|---|
| Demux | the main thread reads packets with `av_read_frame`; audio packets go to a queue for the audio thread |
| Video | decoded in the main thread with frame threads; each frame is converted and scaled by `libswscale` straight into a screen-sized BGRA buffer (aspect ratio kept, letterboxed, bilinear) and written to `/dev/fb0` |
| Audio | the audio thread decodes, converts to 16-bit stereo at the device rate with `libswresample` and writes to `/dev/dsp`, whose writes block while the hardware buffer is full |

| Sync | a monotonic clock started at the first frame is the master; video frames wait until their time, frames more than 80 ms late are dropped (one in eight is still shown), and when the video falls 300 ms behind the decoder skips non-reference frames until it catches up |
| Screen | `ioctl(FBIOGET_VSCREENINFO)` and `ioctl(FBIOGET_FSCREENINFO)` on `/dev/fb0` give the size and line length; while `/dev/fb0` is open the desktop or live terminal stops drawing, and repaints fully when it is closed |

Measured decode speed (`play -b`, 3 decoder threads):

| Clip | Linux test machine, NoovexOS musl build | NoovexOS in QEMU (emulated CPU) |
|---|---|---|
| H.264 1280x720 | 500 fps | 38 fps |
| H.264 1920x1080 | 234 fps | n/a |
| H.265 1280x720 | 283 fps | n/a |
| VP9 1280x720 | 504 fps | n/a |

QEMU's default CPU model has no SSSE3, so the colour conversion falls back to plain C there; run QEMU with `-cpu max` to get numbers closer to real hardware.

Limits: no subtitles and no hardware decoding. The Video Player window and the browser's `<video>` use the same program (sections 6.13 and 6.14).

Since v9.12 `play` also accepts an `http://` or `https://` URL (read with Range requests through `usr/musl/httpio.c`, which keeps the last 8 MB in a ring so the demuxer can seek back without a new request), and has an embedded mode used by the media service (section 6.13).

### 6.13 The media service (`/dev/media`)

The kernel cannot run FFmpeg, so every video is decoded by a `play` process. `core/media.c` keeps up to 8 media slots (`MEDIA_MAX`). Each slot has:

| Field | Meaning |
|---|---|
| two streams | growable byte buffers for data handed to the kernel (one for a normal file, two for split video and audio). The first append that starts with `ftyp` or the EBML magic is remembered as the init segment. Data the decoder has passed by more than 8 MB is dropped (`compact`), keeping the init segment |
| frame | the last decoded frame, BGRA, already scaled by the process to the size the viewer asked for, and a sequence number |
| status | `struct media_status`: current and total time in ms, flags (`PLAYING`, `PAUSED`, `ENDED`, `ERROR`, `VIDEO`, `AUDIO`, `WAITING`, `READY`), video width and height, percent buffered |
| commands | a 32-entry queue: `PLAY 1`, `PAUSE 2`, `SEEK 3` (ms), `VOLUME 4` (0-1000), `SIZE 5` (w, h), `QUIT 6`, `LOOP 7`, `DURATION 8` (a duration hint from the page) |

`media_start` runs `play --embed ID --gen G [--split] [--start MS] [--paused] [--size WxH] [--url]` with stdin on `/dev/null` and stdout/stderr on `/dev/kmsg`. The process opens `/dev/media` and binds it to its slot (the generation number makes a stale process unable to touch a reused slot). The `ioctl` calls:

| Request | Argument | Use |
|---|---|---|
| `MIO_BIND 0x4D01` | `id \| gen<<8 \| stream<<24` | attach this descriptor to a slot |
| `MIO_READAT 0x4D02` | `{off, buf, len, stream}` | read stream data at an offset; returns the byte count, 0 at the end, or -EAGAIN when the data is not there yet |
| `MIO_INFO 0x4D03` | `{len, complete, split, total}` | how much data there is and whether it is complete |
| `MIO_STATUS 0x4D04` | `struct media_status` | the process reports its state (about every 100 ms) |
| `MIO_GETCMD 0x4D05` | `{cmd, a, b}` | take the next command; returns 1 if there was one |
| `MIO_FRAME 0x4D06` | `{buf, w, h, pts}` | copy a frame into the kernel |
| `MIO_URL 0x4D07` | `{buf, cap}` | fetch the slot's URL or file path (it can be longer than a command line) |

Inside `play`, the input is an `AVIOContext`: a local file, `httpio`, or `ks_read`, which polls `MIO_READAT` and sleeps 8 ms when no data has arrived, checking commands while it waits. If it waits more than 150 ms, the playback clock stops (`WAITING`) and restarts when data comes. With two streams there are two demuxers; the main loop reads from the one whose last packet is earlier, so audio stays a little ahead of video. Seeking a URL or file uses `av_seek_frame` and then skips frames until the target time. Seeking in MSE mode is done by the kernel: `media_restart` kills the process, keeps the init segments (and, if the page says the target is already buffered, all data) and starts a new one with `--start`. When the input is a kernel stream that is not complete, the duration is reported as unknown and the page's `MediaSource.duration` is used instead.

`media_tick` runs in the kernel main loop and compacts the stream buffers. `media_free` sends `QUIT` and kills the process.

### 6.14 The Video Player window (`apps/media/videoplayer.c`)

`W_VIDEO` is a window like the others. `video_open_url` starts a media slot for a path or URL, sized to the window; `video_open_slot` builds the full path of a file from the directory tree. Files with the extensions mp4, m4v, mov, mkv, webm, mp3, m4a, aac, ogg, oga, ogv, opus and wav open in it from Files and the desktop, and `video` (or `player`) does the same from the terminal (in the live terminal it switches to the desktop first). Closing the window frees the slot.

`media_blit(id, x, y, w, h, bg)` draws a slot's frame into `SCENE`, keeping the aspect ratio and filling the rest with `bg`; the 1:1 case copies whole rows. `video_tick` asks for a full redraw only when the status changes (every second for the clock, or a new flag). New frames take the fast path `video_fast`: if `desk_win_exposed(W_VIDEO)` says no other window, menu, pop-up, toast or drag is over the window, the frame is drawn into `SCENE` and `present_rect` copies only that rectangle to the screen (with the night-light tint and the cursor drawn over it if needed). Otherwise a full frame is composed.

The kernel main loop calls `video_fast` and then `browser_media_fast` when nothing else changed. It also no longer recomposes the desktop after every scheduler slice in which a program ran: `jc_step` returns bit 0 when a program ran and bit 1 when one marked the screen dirty; the desktop redraws on bit 1, on terminal output, or every 50 ticks. Before v9.12 a decoding process got only the time between full redraws.

### 6.15 glibc programs

#### Building glibc

`tools/build_glibc.sh` builds glibc 2.44 from `third_party/src/glibc-2.44.tar.gz` (`--prefix=/usr`, `--libdir=/usr/lib/x86_64-linux-gnu`, `--enable-kernel=4.4`) into `build/glibc-root` and is skipped when that directory already holds `lib64/libc.so.6`. glibc's `configure` needs GNU awk.

#### The sysroot (`fs/vfs/sysroot.c`, `tools/mksysroot.py`)

`build.sh` copies into `build/sysroot-stage`:

| Path | Content |
|---|---|
| `/lib64` | the glibc loader and libraries (`libc`, `libm`, `libpthread`, `libdl`, `librt`, `libresolv`, `libutil`, `libanl`, `libnss_files`, `libnss_dns`, `libmvec`), stripped, plus every other library the programs below need (found with `ldd` on the build machine) |
| `/lib/x86_64-linux-gnu`, `/usr/lib/x86_64-linux-gnu`, `/usr/lib64` | symbolic links to `/lib64` |
| `/opt/gnu/bin` | bash, coreutils (ls, cat, sort, wc, ...), grep, sed, awk, find, xargs, diff and a glibc test program `gthreads` |
| `/etc/nsswitch.conf` | `files` for passwd and group, `files dns` for hosts |

`mksysroot.py` packs the tree into one file, format `NVS2`: a 16-byte header (magic, entry count, block count, string table offset), a block table (type, offset, compressed and original size), 24-byte entries (name and link target as string table offsets, mode, block, offset in the block, size), the string table, then the blocks. Files are grouped into blocks of up to 8 MiB; ELF files go into blocks compressed with xz and the x86 branch filter, other files into plain xz blocks. It is linked into the kernel as `sysroot_bin.o`. The old `NVSR` format (one raw-deflate stream per file) is still read.

The sysroot is a stack of layers. Layer 0 is the one in the kernel; every installed Debian package is one more layer (section 6.16). `sysroot_add_layer` and `sysroot_del_layer` add and remove layers; after each change the index is rebuilt:

- every path is a node in a hash table (FNV-1a, open addressing); directories are implicit, created from the paths of their files, and keep a list of their children;
- links in layer 0 are placed first, then all package layers (newest slot first), then the files of layer 0; the first file placed at a path wins, so a package can replace a library from layer 0;
- a file's parent directory is resolved through existing links before it is placed, so a package file `/usr/lib/x86_64-linux-gnu/libfoo.so.1` lands in `/lib64`, as on a Debian system with merged `/usr`.

A block is decompressed on the first use of any file in it and kept. Lookups walk the path one component at a time and follow links (up to 16), also through `..`. `sysroot_mkdirs` (end of `ux_boot_layout`) creates sysroot directories up to three levels deep in the directory tree; deeper ones are still listed and entered through `LX_SRDIR`.

| Kernel part | What it does with sysroot files |
|---|---|
| `jc_find_lib` | the ELF interpreter (`/lib64/ld-linux-x86-64.so.2`) comes from the sysroot |
| `jc_find_prog` | absolute paths to executable sysroot files run first; a bare name is tried in `/opt/gnu/bin`, `/usr/local/bin`, `/usr/bin`, `/bin`, `/usr/sbin` and `/sbin` |
| `jc_build`, `lx_execve` | a file starting with `#!` runs its interpreter with the script path inserted (`#!/usr/bin/env python3`, `#!/bin/sh`); `/bin/sh`, `/bin/bash`, `/usr/bin/bash` and `/usr/bin/env` are sysroot links to the GNU programs |
| `ux_stat2` | file mode, size and a per-path inode number (an FNV hash) for files and directories; the loader compares inode numbers to tell libraries apart, so they must differ. Without following links (`lstat`) a sysroot link reports mode `0120777` |
| `ux_readlink` | sysroot link targets (`readlink`, used by Python to find its prefix) |
| `lx_open` | a sysroot directory that is not in the directory tree opens as `LX_SRDIR` |
| `lx_getdents` | lists `LX_SRDIR` directories, and adds sysroot entries after the tree's own entries in mixed directories |
| `lx_path` | relative paths against an `LX_SRDIR` directory descriptor |
| `lx_mmap` | file mappings of sysroot files (how the loader maps libraries) |
| `lx_fstat` | per-path inode numbers |

#### Limits

| Limit | Reason |
|---|---|
| Only position-independent executables (PIE) | user programs live at `0x40000000000`; the low 4 GiB are the kernel's identity map, so a fixed-address program (`ET_EXEC`, for example Ubuntu's `python3` and `gcc`) cannot be placed. Debian builds nearly all programs as PIE, and the built-in Python is built as PIE. |
| `statx` returns `ENOSYS` | glibc then uses `newfstatat`; slower but correct |
| The sysroot is read-only and in memory | files written by programs go to the normal file system; decompressed blocks stay in memory |

#### Tracing

`lxtrace N` in the Terminal logs the next N Linux system calls as `sc NUMBER ARG1 ARG2 = RESULT` to the kernel log (`dmesg`, serial port).

#### Terminal ioctls

glibc 2.44 asks `isatty` with `TCGETS2` (`0x802C542A`, a 44-byte `termios2`), not `TCGETS`. `lx_ioctl` answers `TCGETS2` and the `TCSETS2`, `TCSETSW2` and `TCSETSF2` requests on the Terminal; before v9.23 glibc programs saw no terminal on standard input, so the Python prompt did not appear.

### 6.16 Debian packages: apt and dpkg

`apt` (also `apt-get`, `apt-cache`) and `dpkg` are Terminal commands in the kernel (`fs/pkg/apt.c`). Commands that change the system need root (`sudo apt install ...`).

| Command | What it does |
|---|---|
| `apt update` | for every `deb URI SUITE COMPONENT...` line in `/etc/apt/sources.list` downloads `dists/SUITE/COMPONENT/binary-amd64/Packages.xz` (or `.gz`, or plain), and saves it as `/var/lib/apt/lists/srcN.pkgs` with the source URI in its first line. A missing `sources.list` is created with `deb http://deb.debian.org/debian trixie main`. `deb URI DIR/` (flat repositories) works too. |
| `apt install [-y] PKG...` | resolves `Pre-Depends` and `Depends`, prints the plan, asks when extra packages are needed, downloads each `.deb`, checks size and SHA256 against the index, unpacks it and marks it installed |
| `apt reinstall`, `remove`, `purge`, `autoremove`, `upgrade` | as on Debian; `purge` is `remove` |
| `apt search WORD`, `show PKG`, `list [--installed|--upgradable] [PATTERN]` | read the saved lists |
| `dpkg -i FILE.deb` | installs a local package (from the file system or the FAT boot disk); dependencies are not fetched |
| `dpkg -l`, `-L PKG`, `-s PKG`, `-r PKG`, `--print-architecture` | list, files, status, remove, `amd64` |

#### Resolving

- The index is decompressed into memory and parsed into records (name, version, architecture, dependencies, provides, file name, size, SHA256, description). Only `amd64` and `all` are kept. Names are hashed; for several versions the highest wins, compared with the Debian rules (epoch, upstream, revision, `~` sorts first).
- A dependency group (`a | b (>= 1)`) is met when any alternative is installed, already planned, provided by a planned package, `Essential: yes` or `Priority: required`, or part of NoovexOS. Otherwise the first alternative found (by name or `Provides`) is planned and resolved in turn. Version limits and architecture qualifiers are read but not enforced. `Recommends` are not installed.
- Packages that NoovexOS provides itself are never installed: `libc6`, `libc-bin`, `libgcc-s1`, `python3`, `python3.13`, `python3-minimal`, `libpython3.13-stdlib` and the other Python core packages, `bash`, `coreutils`, `dpkg`, `apt`, `base-files`, `debconf` and a few more (`g_prot`). `apt install python3` answers that it is already the newest version.

#### Downloading

`ap_get` sends an HTTP/1.0 request through `tcp_get` (port from the URI, DNS through `dns_query`), follows up to five redirects, decodes chunked bodies and returns the body. `https://` sources use `https_fetch_to` (port 443). The buffer is the expected size plus 256 KiB (64 MiB for indexes). The index is not signature-checked: there is no OpenPGP code, so trust rests on the mirror and HTTPS; each package is checked against the SHA256 in the index.

#### Unpacking

- `ar` members `control.tar.*` and `data.tar.*`; compression gzip (`pz_inflate_raw`), xz (`lib/compress/xzk.c`, xz-embedded) or zstd (`lib/compress/zstdk.c`, the zstd decoder); `lib/compress/unpack.c` picks by magic bytes.
- The tar reader handles ustar prefixes, GNU long names (`L`, `K`), pax headers (`path=`, `linkpath=`), symlinks (relative targets made absolute) and hard links (copied).
- The files become one `NVS2` layer with a single stored block, written to `/var/lib/nvpkg/PKG.nvs` and added with `sysroot_add_layer("pkg:PKG", ...)`. Programs in `/usr/bin` run at once; libraries are found by the glibc loader.
- `/var/lib/dpkg/status` keeps Package, Version, Architecture, Depends, Description and Auto-Installed.
- At boot `apt_boot` (in `ux_boot_layout`) loads every `/var/lib/nvpkg/*.nvs`, so installed packages survive restarts on an installed system.

#### Limits

| Limit | Reason |
|---|---|
| Maintainer scripts (`preinst`, `postinst`, ...) are not run | no `dpkg` trigger and `debconf` machinery; packages that create files, users or `update-alternatives` links in `postinst` miss those |
| Package files are read-only | they are in the sysroot; a program that edits its own `/etc` file must use a copy |
| No OpenPGP check of `InRelease` | see Downloading |
| Fixed-address (`ET_EXEC`) programs do not run | section 6.15 |
| Up to 1024 packages per install, 255 installed layers | `g_plan`, `SR_MAXL` |

### 6.17 Python 3.13

CPython 3.13 is built from `third_party/src/cpython-3.13.tar.xz` by `tools/build_python.sh` (cached in `build/python-root`; `--prefix=/usr`, position-independent like every program the build machine's gcc makes). `tools/stage_python.sh` puts into the sysroot:

| Path | Content |
|---|---|
| `/usr/bin/python3.13` | the interpreter, stripped; `python3` and `python` link to it |
| `/usr/lib/python3.13` | the standard library without tests, `idlelib`, `tkinter`, `turtledemo` and `lib2to3`, with `.pyc` files made with `--invalidation-mode unchecked-hash` (used without comparing modification times, so imports from the read-only sysroot are fast) |
| `/usr/lib/python3.13/lib-dynload` | extension modules (`_ssl`, `_sqlite3`, `_ctypes`, `_decimal`, `_lzma`, `_bz2`, `readline`, `_curses`, ...), stripped; their libraries (OpenSSL 3, libffi, SQLite, readline, ncurses, ...) go to `/lib64` |
| `/usr/lib/python3.13/ensurepip` | with the bundled pip wheel |
| `site-packages/debian-dist-packages.pth` | adds `/usr/lib/python3/dist-packages`, where Debian's `python3-*` packages install, so modules from `apt install python3-...` import |

`PYTHON_BASIC_REPL=1` is set in the default environment: the new 3.13 prompt needs a terminfo database, the basic prompt does not. `python3 FILE.py`, `python3 -c`, scripts starting with `#!/usr/bin/env python3` and the interactive prompt work in the Terminal and in bash.

### 6.18 The Python IDE package (`python3-ide`)

The sysroot carries a small local package repository in `/usr/share/noovex/repo` (`Packages` plus `pool/*.deb`), built by `build.sh` from `usr/packages/*/` with `dpkg-deb` and `tools/mkrepo.py`. `apt` always reads it, after the downloaded lists, as source `file:/usr/share/noovex/repo/`; `file:` URIs are read from the sysroot or the file system, so it works without a network and without `apt update`.

`sudo apt install python3-ide` (also `ide`, `idle`, `idle3`, `pyide`, `python-ide`, `python3-idle`; so `sudo apt install python3 ide` works) installs the package: `/usr/share/applications/python3-ide.desktop`, an example program and a README. Debian's own `idle3` needs Tk and an X server, which NoovexOS does not have, so these names are redirected.

| Where | What changes once the package is installed (`pyide_installed`: the `.desktop` file exists) |
|---|---|
| Start menu, classic style | Development lists "Python IDE" |
| Start menu, modern style | Productivity lists "Python IDE"; searching "python" or "idle" finds it |
| Terminal | `python3-ide`, `idle`, `idle3` and `pyide` open it |

The IDE is the Editor window in Python mode (`ed_py`, `apps/office/editor.c`): the title reads "Python IDE - main.py", keywords, built-ins, strings, numbers, comments and `def`/`class` names are coloured, Save writes `/root/main.py`, and Run saves and runs `python3 /root/main.py` in the Terminal, so `input()` works. Opening the plain Editor or a file leaves Python mode. Removing the package (`sudo apt remove python3-ide`) takes the entries away again.

### 6.19 X11: the X.Org server (`startx`, `apps/x11/xdisplay.c`)

The server is X.Org's: Xvfb is the same program as Xorg built with a memory framebuffer as its output instead of a graphics card driver, and it implements the whole X11 protocol and its extensions (XTEST, XKB, RENDER, RANDR, ...).

The sysroot carries X.Org's `Xvfb` (the X server with a memory framebuffer), `xkbcomp`, the XKB keyboard data (`/usr/share/X11/xkb`), `XErrorDB`, `protocol.txt`, an Xlib test program `xdemo` (`usr/x11/xdemo.c`) and every library they need (libX11, libxcb, pixman, libXfont2, freetype, ...), copied from the build machine like the GNU tools.

`startx` in the Terminal creates `/tmp` and `/var/lib/xkb`, sets `DISPLAY=127.0.0.1:0` and starts

    Xvfb :0 -screen 0 800x600x24 -fbdir /tmp -fp built-ins -listen tcp -nolisten unix -nolisten local -ac -xkbdir /usr/share/X11/xkb > /tmp/Xvfb.log 2>&1 &

then opens the **X Display** window (also in the start menu: Development, or System in the modern menu). Clients connect over TCP to `127.0.0.1:6000` through the kernel's loopback sockets; Unix-domain sockets are not used. `-fp built-ins` uses the fonts compiled into libXfont2, so no font files are needed.

| Piece | How it works |
|---|---|
| Framebuffer | With `-fbdir` Xvfb keeps its screen in the file `/tmp/Xvfb_screen0` (an XWD image) and maps it with `MAP_SHARED`. `lx_mmap` notices a shared mapping of a file named `Xvfb_screen*` and passes the list of physical pages to `xfb_register`. Unmapping one of them (`lx_unmap_range`) calls `xfb_drop`. |
| Display window | `xdisp_draw` reads the XWD header from the first page (byte order detected from `header_size`), finds width, height, bytes per line and the pixel offset (header plus 12 bytes per colour entry), and copies 32-bit pixels page by page into the window. `xdisp_fast` (main loop, next to `video_fast`) refreshes the window every 50 ms while it is open. |
| `epoll` | The X server's main loop uses `epoll`. `epoll_create`, `epoll_create1`, `epoll_ctl`, `epoll_wait`, `epoll_pwait` and `epoll_pwait2` are implemented on top of `lx_poll_one` (up to 64 instances, 64 descriptors each, level-triggered, `EPOLLONESHOT` honoured). |

#### Keyboard and mouse (v9.26)

| Piece | How it works |
|---|---|
| Kernel queue | `xdisp_mouse` (called from the mouse handler in `graphics/ui/input.c`) and `xdisp_key` (keyboard dispatch when the X Display window has focus) push 8-byte records into a 512-entry ring in `apps/x11/xdisplay.c`: type (1 motion, 2 button, 3 key), button or character, pressed, x and y in X screen coordinates. Wheel steps become buttons 4 and 5. |
| `/dev/nvxxin` | device 16; `read` returns whole records, `poll` and `epoll` report it readable while records wait |
| `nvx-xinput` | started by `startx`; an X client (`usr/x11/nvx-xinput.c`) that reads `/dev/nvxxin` and replays the records with the XTEST extension (`XTestFakeMotionEvent`, `XTestFakeButtonEvent`, `XTestFakeKeyEvent`). Characters are turned into keysyms (Return, BackSpace, Tab, Escape and the arrow keys included); Shift is pressed around characters that need it. Log in `/tmp/nvx-xinput.log`. |

#### The full-screen X session (`startx full`)

`startx full` (also `xserver`) switches the live console to the desktop state, starts the server at the display's own size (for example 1280x1024) with the classic X background (`-retro`), starts `nvx-xinput`, and runs `/root/.xinitrc` with bash if it exists, otherwise `xdemo`. While the session is shown (`g_xfull`), `xfull_tick` copies the X framebuffer to the whole screen every 40 ms through the same path `/dev/fb0` programs use (`g_fb0_open`, `g_fb0_dirty`), all mouse input goes to X (`xfull_mouse`) and all keys go to X. F12 or Ctrl+Alt+Backspace (the keyboard drivers turn them into codes 31 and 30) returns to the NoovexOS desktop; the server keeps running, and `startx full` shows it again. If the server was started in a window first, `startx full` shows that 800x600 screen in the top-left corner.

#### Limits and next stages

| Limit | Next step |
|---|---|
| No window manager: X windows cannot be moved or resized with the mouse | ship a small window manager (twm-like) |
| Only characters the NoovexOS keyboard produces reach X (no separate key-up timing, no function keys other than the arrows) | forward scancodes instead of characters |
| GTK 3 programs are not in the sysroot | about 60 libraries plus GLib schemas, fonts and fontconfig: 2-4 h |
| Chromium | needs GTK, NSS, NSPR, CUPS, D-Bus, a replacement for its sandbox, many more system calls and much memory: 15-30 h in stages |
| One screen, 800x600 at 24 bits, no OpenGL | Xvfb renders in software |

### 6.20 Tkinter, Tk and IDLE

| Piece | Where it comes from |
|---|---|
| Tcl/Tk 8.6 | `tools/build_tcltk.sh` builds Tcl and Tk 8.6 from `third_party/src/tcltk-8.6.tar.xz` (cached in `build/tcltk-root`). `libtcl8.6.so` and `libtk8.6.so` go to `/lib64`, the script libraries to `/usr/lib/tcl8.6` and `/usr/lib/tk8.6`, `wish` to `/usr/bin`. |
| `_tkinter` | built by the same script from CPython 3.13's `Modules/_tkinter.c` against that Tk, into `lib-dynload` |
| `tkinter`, `idlelib` | the standard library packages, now staged (without `idlelib/idle_test`), with IDLE's `.def` configuration files and icons |
| `/usr/bin/idle3.13` | `from idlelib.pyshell import main`; `idle3` and `idle` link to it |
| `nvxwm` | a small window manager (`usr/x11/nvxwm.c`), started by `startx`: a title bar with the window name and a close button (sends `WM_DELETE_WINDOW`), drag the title bar to move, click to raise and focus, the new window gets the focus |

Tcl is built **without threads** (`--disable-threads`). The threaded Tcl notifier waits in `pthread_cond_timedwait` and only woke up on the next X event here, so IDLE showed its prompt, colours and output one keystroke late. Without threads, Tcl waits in `select` with a timeout, and `_tkinter` polls its interpreter every 20 ms, which is what CPython does with a non-threaded Tcl.

Tk is built without Xft, so it uses the X server's core fonts; the server only has its built-in fonts (`-fp built-ins`), so Tk text uses the `fixed` font.

#### Starting X programs

The Terminal starts the X server by itself when a command needs it: `idle`, `idle3`, `idle3.13`, `wish`, `xdemo`, and any command line that mentions `tkinter`, `idlelib` or `turtle` (`x_needs` in `shell/commands.c`). If the server is not running, `x_launch` runs `startx`, then the command through bash after waiting for `/tmp/Xvfb_screen0` plus 2 seconds, with output in `/tmp/xapp.log`. The start menu has **IDLE (Python 3.13)** (Development in the classic menu, Productivity in the modern menu, and in search for "idle", "python" or "tkinter"), which does the same.

IDLE runs in its normal mode, with the user code in a second Python process connected over a loopback TCP socket.

### 6.21 X programs as normal desktop apps (v9.28)

`startx` (and every automatic start, for example `idle3`) now runs the X server at the size of the screen in **overlay mode** (`g_xover`, `apps/x11/xdisplay.c`): there is no X Display window, X windows are drawn straight onto the NoovexOS desktop.

| Piece | How it works |
|---|---|
| Transparency | `nvxwm` paints the X root window in the key colour `0x010203` and gives it an invisible cursor. `xover_draw` (called by the desktop just before the start menu and pop-ups are drawn) copies every X pixel that is not the key colour onto the desktop, above the taskbar line. |
| Refresh | `xover_tick` (main loop, every 60 ms) takes a sampled checksum of the X framebuffer and asks for a desktop redraw when it changed. |
| Mouse | `xover_mouse`: a press on a non-transparent X pixel goes to X and is held there until all buttons are released; motion and the wheel go to X while the pointer is over an X window. Presses on transparent pixels, the taskbar and NoovexOS windows are handled by the desktop as before. |
| Keyboard | after a press on an X window, keys go to X (`g_xkbd`) until the next press elsewhere |

`startx window` keeps the old 800x600 X Display window, `startx full` the full-screen session.

#### Apps from packages in the start menu

`apps/x11/xapps.c` reads every `/usr/share/applications/*.desktop` file in the system image and installed packages (Name, Exec without `%` codes, Icon, Terminal, NoDisplay). Each appears in the start menu (Development in the classic menu, Productivity in the modern menu) and in search, with its own icon: `Icon=` is looked up as a PNG in `/usr/share/icons/hicolor/{48,64,128,32,256}x.../apps/` and `/usr/share/pixmaps/`, decoded with `png_decode` and scaled to 48x48 with its alpha channel. IDLE uses CPython's own icon (`idlelib/Icons/idle_48.png`). Launching runs the `Exec` line through `x_launch` (or the Terminal for `Terminal=true`). So `sudo apt install vlc` adds **VLC media player** with its cone icon to the menu; whether VLC itself runs is not tested (it needs Qt, which needs more of the Linux layer).

#### Opening Python files

A double click on a `.py` file (Files, desktop icons, search results) opens it in IDLE (`idle3 'path'`) instead of the NoovexOS Editor.

## Part 7: File systems

NoovexOS 64 has no block cache layer under a generic inode cache the way Linux does. Every file the system knows about lives in one fixed in-memory table, `FS[]`, and every directory in a second table, `DT[]`. The on-disk formats (NVX4, or NTFS/FAT when installed that way) are back ends that fill those tables at mount, load file contents on demand, and get the changes written back. The rest of the kernel, the shell, the desktop apps and the Linux system-call layer all work on `FS[]`/`DT[]` through a small set of helpers.

Every file here is its own compilation unit with a header under `include/nvx/fs/`. Before v8.96, `fs/vfs/unixfs.c` included `fs/nvx/nvx4.c` and `fs/ntfs/ntfsk.c` included `fs/fat/vfat.c`; they are now separate units too.

```
              shell / desktop apps / core/linux.c (Linux syscalls)
                                  |
        +-------------------------+-----------------------------+
        | vfs.c: vfs_open/read/write/lseek/close, procfs, /dev   |
        | unixfs.c: lookup, links, symlinks, permissions, stat   |
        | dirtree.c: DT[] tree, path resolution, rename/unlink   |
        +-------------------------+-----------------------------+
                                  |
              ramfs.c: FS[1024] in-memory files (heap buffers)
                                  |
        +-------------------------+-----------------------------+
        | NVX4 (nvx4.c)           |  rootfs: NTFS / FAT32 / FAT16 |
        | partition 0x7F or       |  (ntfsk.c + rfs_ops,           |
        | scratch disk @LBA 2048  |   ntfs-3g bridge, vfat.c)      |
        +-------------------------+-----------------------------+
                         gdisk_* (AHCI/NVMe) or ata_* (legacy IDE)
```

---

### 7.1 The in-memory file table (`fs/vfs/ramfs.c`)

#### Constants

| Symbol | Value | Meaning |
|---|---|---|
| `FS_N` | 1024 | Number of file slots (`FS[FS_N]`). The slot index is also the file's "inode" for every back end. |
| `FS_CAP` | `0x40000000` (1 GiB) | Largest file. `fs_reserve` refuses `need > FS_CAP`, and writers clamp to `FS_CAP-1`. |
| `FS_OLDCAP` | 2097152 (2 MiB) | The old fixed per-file size. Now only used as the read buffer size when converting NVX3 disks. |

`ramfs.c` also defines the Unix mode bits taken from V6 `ino.h` (`S_IFMT`, `S_IFDIR 0040000`, `S_IFREG 0100000`, `S_IFCHR 0020000`, `S_ISUID`, `S_ISGID`, `S_IRUSR`...). `S_IFLNK 0120000` is defined in `unixfs.c`.

#### `FsFile`

```c
typedef struct{ char name[64]; char* data; int len; int cap; int used; int folder; int dir;
    unsigned int mode; int uid, gid; u32 nlink, mtime, ctime, atime;
    u8 lazy, clean, orphan, pad; int dlo, dhi; u64 tick; } FsFile;
static FsFile FS[FS_N];
```

| Field | Meaning |
|---|---|
| `name[64]` | Leaf name (up to 63 characters plus NUL). An empty name is used for orphans. |
| `data`, `cap` | Heap buffer from `kmalloc` and its size. `data` may be `NULL` while `len > 0`, for a file that has not been loaded yet or has been evicted. The buffer always keeps one spare byte so `data[len]` can be `0`. |
| `len` | Logical file size in bytes. |
| `used` | Slot is in use. |
| `folder` | Legacy category index used by the file explorer (`apps/system/file_explorer.c` filters on `FS[k].folder`). Not part of the directory tree. |
| `dir` | Index of the parent directory in `DT[]`. `-2` marks an orphan (see 7.3). |
| `mode`, `uid`, `gid`, `nlink`, `mtime`, `ctime`, `atime` | Unix metadata. Times are seconds since the epoch from `ux_now()` (= `k_realtime_ms()/1000`). |
| `lazy` | The contents are on disk but not in memory. The next `fs_ensure` loads them. |
| `clean` | The memory copy equals the on-disk copy. |
| `orphan` | Unlinked but still open (see `fs_slot_freed`). |
| `dlo`, `dhi` | The dirty byte range `[dlo, dhi)` when `clean == 0`. `0x7FFFFFFF` as `dhi` means "to the end". |
| `tick` | Value of the global `g_fs_tick` at the last `fs_ensure`, used as an LRU clock for eviction. |

There are two separate "dirty" concepts:

* `FS[k].clean` / `dlo` / `dhi`: the memory copy differs from disk, and which bytes do. The back ends use this to decide what to write.
* `g_fs_dirty[k]` (a `u8[FS_N]`): the file was written through a descriptor since it was opened. `vf_unref` uses it on the last close to schedule background writeback (7.1.4).

#### Buffer management

* `fs_reserve(k, need)` makes sure `cap > need`. It grows the buffer by 1.5x (starting at 64 bytes), at least to `need+1`, at most to `FS_CAP+1`, rounded up to 64 bytes. If that allocation fails it retries with an exact size. The old contents are copied and `data[len] = 0`.
* `fs_alloc(k, n)` wraps `kmalloc`. On failure it calls `fs_evict(n+4096, k)` and tries once more.
* `fs_ensure(k)` is the "make it resident" call. It bumps `FS[k].tick = ++g_fs_tick`. If the file is `lazy` it calls `fs_backend_load(k)` and marks the file `clean`. If there is still no buffer it reserves `len` bytes and zero-fills them.
* `fsd(k)` returns `FS[k].data` after `fs_ensure`, or the static 64 KiB `g_fs_zero` buffer if loading fails, so callers never get a NULL pointer.
* `fs_dirty(k, lo, hi)` widens the dirty range, or starts a new one if the file was clean, and clears `clean`.
* `fs_wr(k, need)` ensures and reserves, then marks the whole file dirty. `fs_wr_range(k, lo, hi)` marks only `[lo, hi)` dirty. `vfs_write` uses the range form, so a small write into a large file only dirties a few blocks.
* `fs_set(k, src, n)` replaces the contents. If the old buffer is much larger than needed (`cap > n + cap/2 + 64K`) it is freed first. `src == NULL` means zero-fill. The whole file is marked dirty and `lazy` is cleared.
* `fs_truncate_to(k, n)`: when `n == 0`, a buffer larger than 64 KiB is freed outright, without loading the file. Otherwise the file is loaded, grown or shrunk, the gap is zero-filled, and the range from `min(n, old len)` to the end is marked dirty.
* `fs_free(k)` releases the buffer and resets `lazy`, `clean`, `dlo`, `dhi` and `orphan`. It does not clear `used`.

#### Lazy loading and eviction

At mount, the back ends fill only metadata and set `lazy = len > 0` and `clean = 1` (`n4_mount`, `nk_load_tree`). `fs_backend_load(k)` (in `nvx4.c`) allocates `len` bytes and reads the file with `nk_root_read` when `g_rootfs` is set, or otherwise with `n4_read_file`.

```c
static long fs_evict(long want,int keep){
    long got=0;
    for(int pass=0;pass<2&&got<want;pass++)
    for(int k=0;k<FS_N&&got<want;k++){
        if(k==keep||!FS[k].used||!FS[k].data||FS[k].lazy||!FS[k].clean)continue;
        if(FS[k].tick+(pass==0?64u:8u)>g_fs_tick)continue;
        if(fs_is_open(k)||!fs_reloadable(k))continue;
        got+=FS[k].cap; kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; FS[k].lazy=1; }
    return got;
}
```

A file can be evicted only if all of these hold:

* it is resident and clean;
* it is not the file being grown;
* it has not been used in the last 64 `fs_ensure` calls (8 on the second, more aggressive pass);
* it is not open in any `g_vf` entry;
* `fs_reloadable` says it can be read back.

`fs_reloadable` returns false for orphans, when there is no disk, and for anything under `/tmp`. On NVX4 it also requires that the extents already cover `len` and that no indirect block is pending (`!g_n4indd[k]`).

#### Background writeback

```c
static volatile int g_fs_wb_pending; static u64 g_fs_wb_due;
static void fs_wb_mark(int k){ (void)k; if(!g_fs_wb_pending){ g_fs_wb_pending=1; g_fs_wb_due=g_ticks+100; } }
```

When the last reference to a file `vfile` goes away (`vf_unref` in `core/proc/jobs.c`) and `g_fs_dirty[k]` is set, the flag is cleared and `fs_wb_mark` is called. Then `fs_orphan_close(k)` runs. The kernel main loop (`core/init/main.c`) checks `if(g_fs_wb_pending&&g_ticks>=g_fs_wb_due)nvx3_sync_all();`, so a burst of closes becomes one commit about 100 ticks (one second at 100 Hz) later. `nvx3_sync_all` and `nvx3_flush_meta` both clear `g_fs_wb_pending`.

Synchronous paths bypass the timer:

* `fs_sync(k)` to `nvx3_sync(k)` to `n4_sync_file` + `n4_commit`. This is used by `k_fsync`, `k_truncate_idx`, `k_rename`, `ux_mkfile`, `ux_symlink`, package extraction and `fs_save_bin` in `sys/native/syscall.c`.
* `nvx3_flush_meta()` commits metadata after link, unlink, chmod, chown and utime.

#### File descriptors (declared in `ramfs.c`)

```c
#define VMAXOPEN 1024
#define FDMAX    256
struct vfile { int used, fs_idx, off, flags, dev, ref; const vfs_fs_ops* vfs; char vpath[64]; };
static struct vfile g_vf[VMAXOPEN];
```

There is one global table of open files, `g_vf`. Per-process descriptor tables come from `fdt()`, and `fdce()` holds the close-on-exec flags. Both are defined with the process code. A descriptor slot holds `-1` when free and `-2` for the kernel's own stdin/stdout/stderr. `vfile.dev` selects the kind of object:

| `dev` | Object |
|---|---|
| 0 | regular file `FS[fs_idx]` |
| 1 `/dev/null`, 2 `/dev/zero`, 3 `/dev/random` and `/dev/urandom`, 4 `/dev/full`, 5 `/dev/tty` and `/dev/console` | character devices (`dev_id`) |
| 6 / 7 | pipe read end / write end (`jc_pipe_read` / `jc_pipe_write`) |
| 9 `/dev/fb0`, 10 `/dev/input/mouse` and `/dev/input/mice`, 12 `/dev/dsp` and `/dev/audio`, 13 `/dev/kmsg` | devices handled by `ux_dev_read`/`ux_dev_write`, `snd_dev_write` and `klog` |
| 11 | open directory (`fs_idx` = `DT` index). `dt_busy` checks it. |
| 100 | a file served by a registered `vfs_fs_ops` (procfs, usbfs). The path is kept in `vpath`. |

`fs_find_name(nm)` is a legacy flat lookup by leaf name only.

---

### 7.2 The directory tree (`fs/vfs/dirtree.c`)

```c
#define DN_MAX 512
typedef struct { char name[64]; int parent; int used; unsigned int mode; int uid,gid; u32 mtime, ctime, atime; } DNode;
static DNode DT[DN_MAX];
```

`DT[0]` is always the root, with name `"/"` and `parent = -1`. A file belongs to directory `FS[k].dir`. A directory belongs to `DT[i].parent`. Nothing on disk is kept as directory entries: listing a directory means scanning `DT[]`, `FS[]` and the hard-link table for matching parents (`k_readdir_at`, then `k_readdir_extra`).

| Function | Behaviour |
|---|---|
| `dt_init()` | Runs once (`g_dtree_init`). It clears the tree and creates the root. If an old NVX2/NVX3 tree was loaded (`g_nvx_have_tree`) it calls `dt_load_from_disk()`. Otherwise it creates `/home /bin /etc /downloads /tmp /dev` and sets the cwd to `/home`. |
| `dt_load_done()` | Called by `n4_mount` after it fills `DT[]` itself. It repairs the root, makes sure `/dev` exists and sets `g_dcwd` to `/home` (or `/`). |
| `dt_mkdir_at(parent, name)` | Returns an existing child of that name, or allocates a slot with mode `0040755` owned by `g_uid:g_gid`. It updates the parent's mtime and ctime. Names must be 1..63 characters. |
| `dt_child(parent, name)` | Linear search for a child directory. |
| `dt_resolve_dir(p)` | Walks an absolute or relative (from `g_dcwd`) path one component at a time, handling `.` and `..`. A component longer than 63 characters fails. Returns a `DT` index or -1. |
| `dt_path_n(node, out, cap)` | Builds an absolute path by climbing parents, at most 16 levels. `dt_path(n, o)` is a macro in `sys/native/syscall.c` that passes `sizeof(o)` as the capacity. |
| `dt_is_tmp(d)` | Climbs up to 32 levels. Returns true if the ancestor directly under `/` is named `tmp`. |
| `dt_save()` | Persists the tree: `nk_root_dt_save()` on NTFS/FAT, otherwise `nvx3_flush_meta()`. |
| `dt_dir_ok`, `dt_dir_empty`, `k_is_ancestor` | Validity, emptiness and ancestry tests. A directory is not empty while it holds files, subdirectories or hard links (`ux_links_in`). |
| `dt_busy(d)` (`core/proc/jobs.c`) | True if `d` is the current cwd, the cwd of any live process (read from `g_cpu[].dcwd` for processes running on another core), or open as a directory descriptor (`dev == 11`). |

#### `k_split`

`k_split(path, &dir, base)` divides a path into its parent directory index and its leaf name. It refuses leaves longer than 63 characters and prefixes longer than 127, rather than truncating them. A bare name resolves against `g_dcwd`. `k_find_file` uses it, and as a fallback also matches a file whose `dir` no longer exists (`!dt_dir_ok`) by its full string. This is the only place a "name anywhere" match is still allowed.

#### Operations

* `k_unlink(p)`: `k_find_file`, then `ux_can_unlink(dir, k)`, then `fs_drop_name(k)`.
* `k_rmdir(p)`: refuses the root, directories without `perm_dir`, ancestors of the cwd, busy directories, and non-empty ones (subdirectories, files or links). It clears `used`, touches the parent and calls `dt_save()`.
* `k_rename(from, to)`:
  1. `ux_rename_link` is tried first, in case `from` is a hard-link name (it returns -2 when it is not).
  2. For a file: the caller needs `ux_can_unlink` on the source and `perm_dir` on the destination. If `to` is an existing directory the file moves into it under its own name. A same-named subdirectory blocks the rename. An existing file or link with the target name is dropped, which needs `ux_can_unlink` on it. The file is then renamed and moved, both parents are touched, and `fs_sync` and `nvx3_flush_meta` are called.
  3. For a directory: refuses `/` and moves into its own subtree (`k_is_ancestor`). It may replace an existing empty directory that is not busy and is not the cwd. A file with the target name blocks it. On success it updates `parent` and the times, then calls `dt_save()`.
* `k_truncate(p, len)` / `k_ftruncate(fd, len)` to `k_truncate_idx`: needs write permission and `len <= FS_CAP-1`. It calls `fs_truncate_to`, `ux_touch`, then `fs_sync` when a disk is present.
* `k_fsync(fd)`: `fs_sync` on a regular file. It succeeds trivially for devices and the standard streams.

#### `/tmp` is memory-only

`dt_is_tmp` is checked in every persistence path:

* `n4_sync_file` frees any blocks a `/tmp` file still holds and writes nothing.
* `n4_serialize` skips `/tmp` files and links that point into or out of `/tmp`.
* `nvx3_sync` and `nk_root_sync_file` return early.
* `fs_reloadable` refuses eviction, because the data could not be read back.

`ux_boot_layout` additionally clears every file and link in `/tmp` at boot and sets its mode to `041777`.

#### Time

`k_realtime_ms()` reads the RTC once (`read_clock`), converts it with `k_days_from_civil`, and from then on adds `g_ticks*10` ms.

---

### 7.3 Unix semantics (`fs/vfs/unixfs.c`)

#### Lookup and symlinks

`ux_lookup_nofollow(p, &isdir)` first tries `dt_resolve_dir` and returns a `DT` index with `isdir = 1` if that succeeds. Otherwise it splits the path and searches `FS[]`, then the link table `g_lk[]`, in the parent directory. Last, it tries the orphaned-directory fallback.

`ux_lookup(p, &isdir, follow)` adds symlink resolution. A symlink is an ordinary `FS` slot with mode `S_IFLNK|0777` whose data is the target path.

* At most 8 hops are followed.
* Paths and targets are limited to 191 characters.
* A relative target is joined to `dt_path(FS[k].dir)`.

Related calls:

* `ux_symlink(target, path)` creates the slot. The caller needs `perm_dir` on the parent and the name must not be taken (`ux_name_taken` checks files, links and directories). It stores the target with `fs_set` and syncs it.
* `ux_readlink` returns the raw target.

#### Hard links: `g_lk[NLINKS]`

```c
#define NLINKS 256
struct ulink { int used, dir, ino; char name[52]; };
static struct ulink g_lk[NLINKS];
```

A file's primary name lives in `FS[k]`. Each extra name is a `g_lk` entry pointing at slot `ino`. `ux_link_count(k)` = 1 + the number of links, and it is stored in `FS[k].nlink`.

| Call | Behaviour |
|---|---|
| `ux_link(from, to)` | Source must be a file (never followed). Non-root callers who are not the owner need read and write permission on it. The target name must be free and at most 51 characters, and the caller needs `perm_dir`. |
| `ux_unlink(p)` | If `p` names a link, removes only that entry (after `ux_can_unlink`). If it is a primary name, calls `fs_drop_name(k)`. |
| `fs_drop_name(k)` | If any link points at `k`, the first one is promoted: its name and directory become the file's primary name and the link entry is freed. Otherwise the slot is released through `fs_slot_freed`. |
| `ux_rename_link(from, to)` | Moves or renames a link entry. It needs write permission on the target file. It drops an existing file or link of the target name, and refuses if a directory of that name exists or if the target is the same inode. |

#### Orphaned open files

```c
static void fs_slot_freed(int k){
    ...
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k)g_lk[i].used=0;
    g_fs_dirty[k]=0;
    if(fs_is_open(k)&&fs_ensure(k)==0){ FS[k].used=1; FS[k].orphan=1; FS[k].clean=0; FS[k].name[0]=0;
        FS[k].dir=-2; FS[k].folder=-1; FS[k].nlink=0; return; }
    for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==0&&g_vf[i].fs_idx==k){ g_vf[i].dev=1; g_vf[i].fs_idx=-1; }
    FS[k].used=0; fs_free(k);
}
```

If the file is still open, it is made resident and kept as an orphan: no name, `dir = -2`, `nlink = 0`. Its descriptors keep working. The orphan is skipped by `n4_serialize`, by `nvx3_sync`, and by the commit loop. Because it is no longer in the metadata, its blocks become free at the next commit.

`fs_orphan_close(k)`, called from `vf_unref`, frees the slot once no descriptor refers to it. If the file could not be made resident, any descriptors are redirected to `/dev/null` (`dev = 1`) instead, so a reused slot can never be reached through a stale descriptor.

#### Permissions

```c
static int ux_perm_ok(int k,int want){            /* want: 4=r 2=w 1=x */
    if(k<0||k>=FS_N||!FS[k].used)return 0;
    if(g_uid==0)return 1;
    unsigned m=FS[k].mode;
    if(FS[k].uid==g_uid) return (m & (want<<6)) ? 1 : 0;
    return (m & want) ? 1 : 0;
}
```

* The model has only two classes: owner and other. The group bits are not consulted. `perm_file` is an alias for `ux_perm_ok`.
* `perm_dir(d)` grants permission to create or modify entries in a directory if any of these is true: the caller is root, owns the directory, the directory is world-writable (`0002`, default mode `0040755` if unset), or `ux_is_admin(g_uid)`.
* `ux_can_unlink(d, k)`: root always may. Otherwise the caller must own `d` or `d` must be world-writable. The exception is an admin deleting their own file in someone else's directory (`FS[k].uid==g_uid && ux_is_admin`). If the sticky bit `01000` is set on `d`, a non-root caller must own either the file or the directory. When `d` is invalid the check falls back to `perm_file(k, 2)`.
* `ux_is_admin(uid)`: true for uid 0 and uid 1000, and for any user whose name (from `/etc/passwd` via `ux_uid_name`) appears in the comma-separated member list (fourth field) of the `admin:` line of `/etc/group`.
* `ux_chmod` and `ux_chown` require root or ownership. A non-root caller cannot give a file away. `ux_chmod` keeps the `S_IFMT` bits and fills in `S_IFDIR`/`S_IFREG` if they are missing. Directory changes call `dt_save()`, file changes call `nvx3_flush_meta()`.

#### `ux_stat2` and inode numbers

`ux_stat2(path, u32 o[10], follow)` fills `o[] = {mode, uid, gid, nlink, size, ino, mtime, atime, ctime, o[9]}` (`o[9]` is set to 1 only for directories). Inode numbers come from disjoint ranges:

| Object | `o[5]` (inode) | Other values |
|---|---|---|
| Regular file / symlink in slot `k` | `k + 1` | `nlink` from `FS[k]` (at least 1) |
| Directory `DT[k]` | `10000 + k` | size 4096; `nlink` = 2 + number of child directories; `o[9] = 1` |
| `/dev` device | `20000 + dev_id` | mode `0020666`, nlink 1 |
| `/mnt/usb` file `fi` | `21000 + fi` | mode `0100644`, size from `ufs[fi].size` |
| Built-in program (`jc_find_prog`) under `/bin`, `/sbin`, `/usr`, or a bare name | `22000` | mode `0100755`, size = image length |

`k_readdir_at` reports type 4 for directories, 10 for symlinks and 8 for files. `k_readdir_extra` then appends:

* hard links;
* the built-in `JS_BIN` programs in `/bin`;
* `init` in `/sbin`;
* USB files in `/mnt/usb`;
* the fixed device names in `/dev` (type 2).

#### Account files

`ux_boot_layout()`, run by `ux_core_start`, builds the default tree and seeds `/etc` if the files are absent (`ux_mkfile` returns an existing slot unchanged):

```
/etc  /usr/bin  /var/log  /sbin  /root  /home  /bin  /dev  /mnt/usb  /tmp (041777)
/sys/{kernel,devices,class,power}  /proc
/etc/passwd    root:x:0:0:root:/root:/bin/nsh
               user:x:1000:1000:user:/home:/bin/nsh
/etc/group     root:x:0:
               admin:x:10:user
               users:x:1000:user
/etc/hostname  noovex            (copied into g_hostname)
/etc/rc        # /etc/rc - run by init (PID 1) at boot ...
```

* `/etc/passwd` has seven colon-separated fields. `ux_pw_field` reads them, `ux_login_user` uses fields 0 (name), 2 (uid), 3 (gid) and 5 (home), and sets `g_uid`, `g_gid`, `g_dcwd`, `HOME` and `USER`.
* `ux_ensure_account(name)` renames uid 1000 to the setup account name in both `/etc/passwd` (field 0) and `/etc/group` member lists, using `ux_rewrite_names`.
* `/etc/shadow` (mode `0100600`) is written by `ux_save_localpw` as one line, `user:<hash>`. `<hash>` is the 32-bit FNV-1a of the password in lowercase hex, or `*` when there is no password. `ux_load_localpw` sets `g_has_pw` if the second field is not empty or `*`.

#### `/proc`, `/sys` and synthetic `/dev` text

`proc_gen(p, out, cap)` returns generated text for each path below. `ux_read_file` consults it before the file table, which is how the shell reads these files.

| Path | Content |
|---|---|
| `/proc/cpuinfo` | Fixed text |
| `/proc/version` | Fixed text |
| `/proc/hostname` | `g_hostname` |
| `/proc/uptime` | `g_ticks/100` seconds |
| `/proc/meminfo` | Total from `g_ram_mb`; free and available = 3/4 of total |
| `/proc/filesystems` | Fixed list |
| `/proc/loadavg` | Process count field = used `FS` slots |
| `/proc/mounts` | `nvxfs /`, `proc`, `sysfs`, plus `vfat /mnt/usb` when mounted |
| `/proc/self`, `/proc/self/status` | Name `nsh`, uid, gid |
| `/dev/null` | Empty |
| `/dev/zero` | Up to 4096 zero bytes |
| `/dev/random`, `/dev/urandom` | 64 hex characters from an xorshift seeded by `g_ticks` |
| `/dev/tty`, `/dev/console` | `console` |
| `/sys/kernel/ostype` | `NoovexOS` |
| `/sys/kernel/osrelease` | `8.96` |
| `/sys/kernel/hostname` | `g_hostname` |
| `/sys/kernel/arch` | `x86_64` |
| `/sys/devices/system/cpu/online` | `0` or `0-N` (from `g_cores_online` when SMP is on) |
| `/sys/class/net/eth0/operstate` | `down` |
| `/sys/power/state` | `mem disk` |

`ux_statfs` reports a RAM-based view: `f_type 0x4e5658`, 4 KiB blocks, `g_ram_mb*256` blocks, `FS_N` inodes, `f_namelen` 255. The file explorer and Settings use `n4_usage` for real disk numbers.

---

### 7.4 The VFS layer (`fs/vfs/vfs.c`)

```c
struct vfs_fs_ops {
    const char* name;
    int  (*match)(const char* path);
    int  (*read)(const char* path,char* buf,int off,int len);
    int  (*size)(const char* path);
};
#define VFS_MAXFS 8
```

`vfs_register` appends to `g_vfs[]`, and `vfs_lookup` returns the first back end whose `match` accepts the path. `vfs_init()` registers `PROCFS`. `ux_mount` registers `USBFS` the first time `/mnt/usb` is mounted. Registered back ends are read-only.

procfs (`procfs_build`) is the descriptor-level `/proc`. It matches `/proc` and `/proc/...` and serves:

* `meminfo`
* `uptime` (in ticks)
* `version` (`NoovexOS 64 v8.95 ...`)
* `cpuinfo` (one block per online `g_cpu[]`, plus the scheduling mode)
* `fb` (width, height, 32, pitch)
* `mounts` (registered back-end names plus `rootfs`)
* an empty leaf, which lists the entries above.

The output is built into the 4 KiB `g_procbuf` on every read. This set differs from `proc_gen`, which serves the shell's `ux_read_file` path.

#### `vfs_open(nm, flags)`

1. If a registered back end matches, allocate a `vfile` with `dev = 100` and copy the path.
2. Otherwise, if `dev_id(nm)` is non-zero, open a device. `g_fb0_open` and `g_mouse_open` count users of `/dev/fb0` and the mouse.
3. Otherwise resolve with `ux_lookup(..., follow=1)`. Directories are refused here; directory descriptors are opened elsewhere.
4. `O_CREAT|O_EXCL` (`0x40|0x80`) fails if the file exists.
5. If the file is missing and `O_CREAT` is set, `k_split` the path, require `perm_dir(parent)`, take the first free slot, and set mode `0100000 | (0666 & ~g_umask)` with owner `g_uid`.
6. For an existing file: reading (access mode not 1) needs `perm_file(k, 4)`. Writing or `O_TRUNC` needs `perm_file(k, 2)`.
7. `O_TRUNC` on a non-empty file to `fs_truncate_to(k, 0)`. Create and truncate both set `g_fs_dirty`.
8. The starting offset is `len` for `O_APPEND`, otherwise 0. Descriptors 0-2 are not handed out when no process is current (`g_jcur < 0`). `O_CLOEXEC` (`0x80000`) is recorded in `fdce()`.

Flag values: `O_RDONLY 0`, `O_WRONLY 1`, `O_RDWR 2`, `O_CREAT 0x40`, `O_EXCL 0x80`, `O_TRUNC 0x200`, `O_APPEND 0x400`, `O_CLOEXEC 0x80000`. The Linux layer (`sys/linux/linux.c`) also decodes `O_DIRECTORY` (`0x10000`).

#### `vfs_read` / `vfs_write`

* Reads from a regular file clamp to `len - off`, call `fs_ensure`, and copy the bytes out.
* For writes:
  * `O_APPEND` moves the offset to the end first.
  * The write is clamped to `FS_CAP-1`.
  * `fs_wr_range(k, min(off, len), off+n)` marks only the touched range dirty.
  * A gap past the old end is zero-filled.
  * `len` grows, `data[len] = 0`, `ux_touch(k, 1)`, and `g_fs_dirty[k] = 1`.
* Writes never commit to disk; that happens on close, on `fsync` or `sync`, or at the next commit.

| Device | Read | Write |
|---|---|---|
| null (1) | EOF | accepted and discarded |
| zero (2) | zeros | accepted and discarded |
| random (3) | bytes from the LCG `__dev_rng*1664525+1013904223` | accepted and discarded |
| full (4) | zeros | -1 (device full) |
| tty (5) | `jc_tty_read` while a process runs, else 0 | `jc_tty_out` while a process runs, else the `g_r3out` buffer |
| fb0 (9) | raw `SCENE` pixels, `W*H*4` bytes | writes pixels and sets `g_fb0_dirty`. `vfs_lseek` supports `SEEK_END` = the frame size. |
| mouse (10) | 3-byte PS/2-style packets from the `g_mq` ring that `ux_mouse_push` fills. Returns `JS_BLOCK` when empty. | - |
| dsp (12) | - | `snd_dev_write` |
| kmsg (13) | - | split into printable lines to `klog` |

`vfs_lseek` clamps negative results to 0 and refuses offsets above `FS_CAP-1`. `vfs_close` drops the descriptor and calls `vf_unref`, which runs pipe cleanup, writeback scheduling and orphan release when the reference count reaches zero. Closing descriptors 0-2 is refused when no process is current (`g_jcur < 0`). `vfs_dup` shares the `vfile` and increments `ref`. `vfs_fstat` returns the file length.

---

### 7.5 Locating the data area (`fs/nvx/nvxfs.c`)

`disk_init()` (called from `kmain` after `fs_init`) calls `nvx_locate()`, which picks the disk interface and the base LBA `g_nvx_base`. The legacy ATA driver is used when `ata_detect` finds a drive. Otherwise the generic disk is used: `gdisk_*` for AHCI or NVMe, with `g_nvx_generic = 1` and partition LBAs scaled by `dk_ratio()` on 4 KiB-sector drives.

```
read sector 0 (MBR)
 +- valid 55AA and an entry with type 0x7F (NVX_PTYPE) and size >= 2048
 |      -> base = its start, secs = its size          "NoovexOS data partition"   (2)
 +- any other non-empty partition entry               "Disk belongs to another system - not touched" (0)
 +- sector 0 non-zero and no 55AA                     "Unrecognised disk - not touched" (0)
 +- otherwise: scratch disk, base = 2048, secs = total-2048
        read LBA 2048:
          first u32 = NVX2 / NVX3 / NVX4 magic        "Scratch disk" (2)
          all zero                                    "Blank scratch disk" (1)
          anything else                               "Unrecognised data at LBA 2048 - not touched" (0)
```

When `nvx_locate` returns 0, `disk_init` tries `nk_root_try()` (NTFS/FAT root, 7.8). Otherwise it reads the first sector at `base` and dispatches on its magic:

| First u32 at base | Format | Action |
|---|---|---|
| `0x3458564E` ("NVX4") | NVX4 | `n4_mount(g_nvx_secs)`. On failure the disk is left alone (`disk_ok = 0`) and the note is `g_n4_err`. |
| `0x3358564E` ("NVX3") | NVX3 (v8.13-v8.90) | `nvx3_try_load()`, then `dt_init()`, then convert with `n4_format`. |
| `0x4E565849` (`NVX_MAGIC`) | NVX2 (before v8.13) | `nvx_mount_old()`, read each file, then convert. |
| zero, and the location said "blank" (`how == 1`) | - | `dt_init()` then `n4_format` ("Formatted as NVX4"). |
| other | - | "Unrecognised data - not touched". |

The legacy readers `nvx_rd`/`nvx_rdn` return -1 whenever `g_rootfs` is set, so the old NVXFS code cannot touch an NTFS/FAT root.

---

### 7.6 The NVX4 on-disk format (`fs/nvx/nvx4.c`)

#### Geometry

| Constant | Value |
|---|---|
| `N4_MAGIC` | `0x3458564E`, bytes `4E 56 58 34` = "NVX4" |
| `N4_MMAGIC` | `0x544D344E`, "N4MT", the metadata header magic |
| `N4_BS` / `N4_SPB` | 4096-byte blocks = 8 sectors |
| `N4_INL` | 17 extents in the inode |
| `N4_INDN` | 512 extents in one indirect block (512 x 8 bytes = 4096) |
| `N4_MAXEXT` | 529 extents per file |
| `N4_MAXBLK` | 16 Mi blocks = 64 GiB cap (`n4_layout` clamps `nblocks`) |
| `N4_METAB` | `ceil(sizeof(n4meta)/4096)` = ceil(344128/4096) = 85 blocks |

Block numbers are relative to `g_nvx_base`: `lba = g_nvx_base + blk*8` (`n4_rd`/`n4_wr`). With the fixed table sizes (`FS_N` 1024, `DN_MAX` 512, `NLINKS` 256) the layout is:

```
block 0        superblock (first 512 bytes; rest of block unused)
blocks 1..3    unused
blocks 4..88   metadata copy A   (meta_a = 4,           85 blocks)
blocks 89..173 metadata copy B   (meta_b = 4+N4_METAB)
blocks 174..   data              (data0 = 4+2*N4_METAB)  ... nblocks-1
```

`n4_layout` refuses a partition smaller than `data0 + 64` blocks ("partition too small").

#### Superblock `n4super` (512 bytes, at LBA `g_nvx_base`)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | u32 | `magic` | `N4_MAGIC` |
| 4 | u32 | `version` | 4 |
| 8 | u32 | `bsize` | 4096 |
| 12 | u32 | `nblocks` | partition blocks (at most 16 Mi) |
| 16 | u32 | `metab` | `N4_METAB` (85) |
| 20 | u32 | `meta_a` | 4 |
| 24 | u32 | `meta_b` | 89 |
| 28 | u32 | `data0` | 174 |
| 32 | u64 | `created` | seconds since epoch at format |
| 40 | u32 | `inodes` | `FS_N` (1024) |
| 44 | u32 | `dirs` | `DN_MAX` (512) |
| 48 | u32 | `links` | `NLINKS` (256) |
| 52 | u32[115] | `pad` | 0 |

#### Metadata image `n4meta` (344128 bytes, one per copy)

```c
typedef struct { n4hdr h; n4ino ino[FS_N]; n4dir dir[DN_MAX]; n4lnk lk[NLINKS]; } n4meta;
```

| Offset in image | Contents | Size |
|---|---|---|
| 0 | `n4hdr` | 64 |
| 64 | `n4ino ino[1024]` | 262144 |
| 262208 | `n4dir dir[512]` | 65536 |
| 327744 | `n4lnk lk[256]` | 16384 |
| 344128 | end; zero padding to 85 x 4096 = 348160 | |

Array index = `FS`/`DT`/`g_lk` slot index, so slot numbers (and therefore inode numbers) stay the same across reboots. Every struct size is checked with `_Static_assert`.

`n4hdr` (64 bytes)

| Off | Type | Field |
|---|---|---|
| 0 | u32 | `magic` = `N4_MMAGIC` |
| 4 | u32 | `version` = 4 |
| 8 | u64 | `gen`, generation, incremented on every commit |
| 16 | u32 | `crc`, CRC-32 (poly `0xEDB88320`) of the whole `n4meta`, computed with this field set to 0 |
| 20 | u32 | `size` = `sizeof(n4meta)` |
| 24 | u32 | `ninode` = 1024 |
| 28 | u32 | `ndir` = 512 |
| 32 | u32 | `nlink` = 256 |
| 36 | u32 | `bsize` = 4096 |
| 40 | u32[6] | `pad` |

`n4ino` (256 bytes)

| Off | Type | Field |
|---|---|---|
| 0 | u32 | `used` |
| 4 | u32 | `dir` (parent `DT` index) |
| 8 | u32 | `mode` (default written: `0100644`) |
| 12 / 16 | u32 | `uid` / `gid` |
| 20 | u32 | `nlink` |
| 24 / 28 / 32 | u32 | `mtime` / `ctime` / `atime` |
| 36 | u32 | `folder` |
| 40 | u64 | `len`, clamped on write to the allocated blocks x 4096 |
| 48 | u32 | `nex`, number of extents in use |
| 52 | u32 | `ind`, indirect block number (0 when `nex <= 17`) |
| 56 | `n4ext[17]` | `{u32 start, count}` inline extents |
| 192 | char[64] | `name` |

`n4dir` (128 bytes): `name[64]` @0, `int parent` @64, `used` @68, `mode` @72 (default `0040755`), `uid` @76, `gid` @80, `mtime` @84, `ctime` @88, `atime` @92, `pad[8]` @96.

`n4lnk` (64 bytes): `used` @0, `dir` @4, `ino` @8, `name[52]` @12.

Indirect block: 4096 bytes holding `n4ext[512]`, the extents numbered 17..528 of one file. It is rewritten to a *newly allocated* block whenever the extent list changes (`g_n4indd[k]`), and the old block goes on the pending-free list.

#### In-memory state

| Variable | Role |
|---|---|
| `g_n4_bm` | Free-block bitmap, one bit per block, never stored on disk |
| `g_n4_free`, `g_n4_hint` | Free count and the next-fit allocation cursor |
| `g_n4x[k][17]`, `g_n4nx[k]` | Inline extents and extent count per file |
| `g_n4xx[k]` | Heap array of 512 indirect extents (allocated on demand) |
| `g_n4ind[k]`, `g_n4indd[k]` | Current indirect block and its "needs rewrite" flag |
| `g_n4_img`, `g_n4_shadow` | Metadata image being built, and the last committed image |
| `g_n4_stale[2]` | Copy A/B must be rewritten in full |
| `g_n4pf`, `g_n4pfn` | Pending frees (`n4ext` list) |
| `g_n4_gen` | Generation of the last committed image |

#### Allocation and extents

* `n4_alloc(want, hint, &got)` takes the run starting at `hint` if that block is free. Otherwise it calls `n4_find_free`, which scans from the cursor to the end and then wraps to `data0`, skipping full bytes (`0xFF`) 8 blocks at a time. It then extends the run while blocks are free, up to `want`. It may return fewer blocks than asked for.
* `n4_resize(k, nb)` grows a file by appending runs. The hint is the block after the last extent, so files stay contiguous when possible. `n4_push` merges a run into the last extent when they are adjacent. If the disk is full or the file has more than 529 extents, the new blocks are released immediately (`n4_trim(k, cur, 1)`), with `g_n4_err = "disk full"`.
* Shrinking (`n4_trim(k, keep, 0)`) moves the cut blocks onto the pending-free list instead of freeing them.
* `n4_map(k, fb, &run)` turns a file block into a disk block plus the length of the contiguous run.

#### Writing a file: `n4_sync_file(k)`

1. Skip if `g_n4` is off, the slot is unused, or the file is an orphan. Files under `/tmp` drop all their blocks.
2. `nb = ceil(len/4096)`. A `lazy` file (never loaded) can only shrink; nothing is read back.
3. Otherwise, unless the file is clean with the right block count and no indirect change pending:
   * `n4_resize(k, nb)`;
   * the blocks to write are `[dlo/4096, ceil(dhi/4096))`, widened to include every newly added block (`cur < nb`);
   * `n4_write_range` writes runs of up to 256 blocks with multi-sector commands (`gdisk_writen`/`ata_writen`). The partial last block is padded with zeros through `g_n4blk`.
   * Mark the file `clean`, `dlo = dhi = 0`.
4. If `g_n4indd[k]` is set: allocate a new block for the indirect extents, write it, and put the old one on the pending list. If the file no longer needs an indirect block, the old one is queued and `ind = 0`.

#### The commit protocol: `n4_commit()`

```
 n4_commit()
 |
 +- 1. for every used, non-orphan file: n4_sync_file(k)
 |       data blocks + new indirect blocks -> only blocks that are
 |       FREE in the last committed state (old blocks are still "used")
 +-    n4_barrier()                    (gdisk_flush; no-op on legacy ATA)
 |
 +- 2. n4_serialize()  -> g_n4_img: header gen = g_n4_gen+1, CRC-32
 |
 +- 3. n4_write_meta(0)
 |       copy A: write only 4 KiB blocks that differ from g_n4_shadow
 |               (runs of <=64 blocks), or all if stale[0]
 |       n4_barrier()     stale[0]=0
 |       copy B: same comparison against the shadow, or all if stale[1]
 |       n4_barrier()     stale[1]=0
 |       (any write error -> stale[c]=1, "metadata write failed", abort)
 |
 +- 4. shadow := img ;  g_n4_gen++
 |
 +- 5. release pending frees: n4_mark(start,count,0) for each
```

Power-cut reasoning, from the code:

* Until step 3 finishes copy A, both on-disk copies still describe the old state. None of the blocks they reference were overwritten, because freed blocks are only released in step 5, after both copies hold the new generation.
* If the cut happens while copy A is partly written, A's CRC fails and B (old generation) is used.
* If it happens after A and during B, A is valid with the higher generation and wins.

`n4_mount` then marks the losing copy stale, so the next commit rewrites it in full.

The metadata write is a delta: `n4_write_meta` compares each 4 KiB block of the new image with `g_n4_shadow` (64-bit word compare). The header block always changes because of `gen` and `crc`.

Callers of the commit: `nvx3_sync` (after one file), `nvx3_flush_meta` and `nvx3_sync_all` (background writeback, `sync`).

#### Mounting: `n4_mount(secs)`

1. Read the superblock at `g_nvx_base`. Reject it ("unsupported NVX4 layout") unless magic, version, `bsize`, `metab`, `inodes`, `dirs` and `links` all match the compiled constants. Reject it ("does not fit the partition") if `nblocks*8 > secs`, `nblocks > N4_MAXBLK`, `meta_a/meta_b/data0` differ from the fixed layout, or `nblocks < data0+64`.
2. `n4_alloc_state()`: allocate the image, the shadow and the bitmap, zero the bitmap, and mark blocks `0..data0-1` used.
3. Read both copies (85 blocks each) and validate each with `n4_valid_meta`: header fields plus the CRC with `crc` zeroed during the computation.
   * Neither valid to "both metadata copies are damaged", mount fails.
   * Only B valid, or B newer (`gb > ga`) to use B and set `stale[0] = 1`.
   * Otherwise use A, and set `stale[1]` unless B is valid with the same generation.
4. Clear all `FS` slots and fill them from the inodes. For each file:
   * `nex <= 529` is checked;
   * the indirect block (if any) is checked to lie in `[data0, nblocks)` and is read;
   * every extent and the indirect block are claimed in the bitmap with `n4_claim`, which fails on any overlap or out-of-range run.
   On failure the claims made for that file are rolled back and the file is emptied (`len = 0`), so it is counted as damaged rather than failing the mount.
5. `len` is clamped to the allocated blocks x 4096 and to `FS_CAP-1`, and `lazy = len > 0`, `clean = 1`. No file data is read at mount.
6. Load `DT[]`. A directory whose parent is invalid, unused or itself is re-parented to `/`. Load `g_lk[]`, dropping links to unused slots. Files with an invalid `dir` go to `/`.
7. `g_n4 = 1`, `dt_load_done()`. If any files were damaged, `g_n4_err = "some damaged files were emptied"` and a `klog` line is written.

Because the bitmap is rebuilt from the extents on every mount, blocks leaked by a crash (written but never committed) come back automatically.

#### Formatting: `n4_format(secs)`

```c
if(n4_layout(secs))return -1;  n4_alloc_state();  ...  n4_apply_res(1);
g_n4=1; g_n4_gen=0;
for each used, non-orphan FS[k]: fs_ensure(k); mark fully dirty; n4_sync_file(k);
n4_barrier(); n4_serialize(); g_n4_stale[0]=g_n4_stale[1]=1;
n4_write_meta(1);                         /* both copies, in full, gen = 1 */
shadow = img; g_n4_gen=1;
n4_write_super(k_realtime_ms()/1000);     /* sector 0 of the partition - LAST */
n4_apply_res(0); g_n4_nres=0; g_n4pfn=0;
```

Whatever is in `FS[]` at that point is written out: the default files from `fs_init`, or the files just read from an old disk. `n4_write_super` writes one sector and then flushes. `n4_usage` reports `(nblocks - data0)*4096` total bytes and `g_n4_free*4096` free bytes.

---

### 7.7 Old formats and conversion

#### NVX2 (`NVX_MAGIC 0x4E565849`, `nvx_mount_old`)

```c
typedef struct { char name[64]; u32 start,len,folder,dir,mode,uid,gid; } nvx_ent;          /* 92 B */
typedef struct { char name[32]; int parent, used; unsigned int mode; } nvx_dnode;            /* 44 B */
typedef struct { u32 magic,count; nvx_ent e[NVX_MAX]; u32 dcount; nvx_dnode d[NVX_DN_MAX]; } nvxfs_t;
```

The directory lives at `NVX_DIR_LBA = g_nvx_base`. It holds 24 files (`NVX_MAX`), each with an absolute starting LBA in `start`, and up to 64 directory nodes with 31-character names. `disk_init` reads each file (through a 2 MiB static buffer) into a free slot. Entries starting before `base+8` are skipped. Each file's sector range is recorded with `n4_reserve_secs`.

#### NVX3 (`0x3358564E`, `nvx3_try_load` in `unixfs.c`)

```c
typedef struct { u32 used, dir, mode, uid, gid, nlink, len, mtime, ctime, atime, folder; char name[64]; } nvx3_ino;
typedef struct { u32 used, dir, ino; char name[52]; } nvx3_link;
typedef struct { u32 magic, version, ninode, nlinks, ndn; nvx3_ino ino[24]; nvx3_link lk[64]; nvx_dnode d[64]; } nvx3_t;
```

Each of the 24 inodes has a fixed 2 MiB slot at `nvx3_slot(k) = NVX_DATA0 + (k+1)*4096` sectors (`NVX_DATA0 = base+8`). The loader:

* requires `ninode == 24` and `nlinks == 64`;
* loads each file into `FS[k]` with the same index;
* copies links into `g_lk`;
* copies the directory nodes into `g_nvx.d` so `dt_init` to `dt_load_from_disk` can rebuild `DT[]`;
* reserves every data range.

#### Crash-safe conversion

```c
static u32 g_n4_res[64][2]; static int g_n4_nres;
static void n4_reserve_secs(u32 sec,u32 n){ ... g_n4_res[i] = {sec/8, blocks covering [sec, sec+n)} ... }
static void n4_apply_res(int set){ /* clamp each range to [data0, nblocks) and mark used/free */ }
```

`n4_format` marks the reserved ranges as used *before* writing anything, so new file data can only go into blocks the old format did not use. The NVX4 metadata copies occupy blocks 4..173 (sectors 32..1391 of the partition). The NVX3 header (about 9.5 KB at sectors 0-18) and NVX3 data (from sector 4104) lie outside that range. The new superblock is written into sector 0 last, replacing the old magic.

If power fails at any earlier point, sector 0 still carries the NVX2/NVX3 magic and the next boot converts again. If the format fails, `disk_init` reports "Old disk could not be converted - left unchanged" and leaves `disk_ok = 0`. Reservations are clamped to `data0` and above, so they protect old data only in the data area.

---

### 7.8 NTFS and FAT root file systems (`fs/ntfs/ntfsk.c`)

When no NoovexFS location is found, the system can run from an NTFS, FAT32 or FAT16 volume. Every back end is reached through one table:

```c
typedef struct { const char* name;
    int  (*mount)(nvxblk_rd,nvxblk_wr,unsigned long long base,unsigned long long nsect,int ro);
    void (*umount)(void);
    int  (*exists)(const char*); int (*mkdir)(const char*);
    long (*write)(const char*,const char*,long); long (*read)(const char*,char*,long);
    int  (*del)(const char*);   int (*ls)(const char*,nvxntfs_lscb,void*);
    int  (*format)(nvxblk_rd,nvxblk_wr,unsigned long long,unsigned long long,const char*); } rfs_ops;
static const rfs_ops g_rfs_ntfs  = {"NTFS",  nvxntfs_mount, ..., nvxntfs_format};
static const rfs_ops g_rfs_fat32 = {"FAT32", vf_mount, ..., vf_fmt32};
static const rfs_ops g_rfs_fat16 = {"FAT16", vf_mount, ..., vf_fmt16};
```

* Sector I/O. `nk_rd`/`nk_wr` provide 512-byte sector callbacks over `gdisk_read`/`gdisk_write`. On 4 KiB-sector drives (`dk_ratio() > 1`) they read-modify-write the containing 4 KiB sector and call `bc_invalidate`.
* Finding volumes. `nk_scan` probes, in order:
  * the whole disk;
  * the four MBR entries;
  * when an entry has type `0xEE`, up to 128 GPT entries (header "EFI " at LBA 1, entry LBA at offset 72, count at 80, entry size at 84, first and last LBA at entry offsets 32 and 40).

  `nk_probe` needs the `55AA` signature, then recognises `"NTFS "` at byte 3 (returns 1), or a FAT BPB: 512 bytes/sector, a power-of-two cluster size, FAT count non-zero, reserved sectors non-zero (returns 2). `nk_find`, used by the `ntfs` command, looks only for NTFS: MBR types `0x07/0x17/0x27`, or the GPT Microsoft basic-data GUID.
* Taking over a root (`nk_root_try`). Each candidate is mounted read-only (type 1 with the NTFS ops, type 2 with the FAT ops; `vf_mount` decides FAT16 or FAT32 from the cluster count). It then reads `/.noovexos`.
  * If the marker is missing, the volume is unmounted and logged as "left untouched", so a Windows disk is never written.
  * If it is present, the volume is remounted read-write, and the system sets `g_rfs = o`, runs `nk_load_tree()` and `nk_meta_apply()`, and sets `g_rootfs = 1`.
* Lazy tree load (`nk_load_tree`). A breadth-first walk with `g_rfs->ls` creates `DT` nodes with `dt_mkdir_at` and fills `FS` slots with name, size, `lazy = 1` and `clean = 1`. Mode defaults to `0100644` and owner to root.
  * Names beginning with `$`, `.noovexos`, `.noovexmeta` and `System Volume Information` are skipped.
  * Files larger than `FS_CAP-1` are skipped.
  * Contents are read on demand by `nk_root_read`, which fills any short read with zeros.
* Permissions file `/.noovexmeta`. Each line is `<D|F> <mode octal> <uid> <gid> <absolute path>`, for every directory except `/` and every non-orphan file, in a 128 KiB buffer.
  * `nk_meta_write` rewrites the whole file after every save, flush and tree save.
  * `nk_meta_apply` applies it at mount.
* Saving:
  * `nk_root_sync_file(k)`: when the file is clean and its path is in the known-files list `g_nk_kf`, only the meta file is rewritten. Otherwise the parent directories are created (`nk_mkdirs`), the whole file is written with `g_rfs->write`, and it is marked clean.
  * `nk_root_flush()`: deletes on the volume every known path that no longer matches a live `FS` slot (this is how deletes and renames reach the disk), then rewrites the meta file.
  * `nk_root_dt_save()`: creates the directories, then removes known directories that no longer resolve, longest path first (up to 8 per call).
  * `nvx3_sync`, `nvx3_flush_meta`, `nvx3_sync_all` and `dt_save` all route here while `g_rootfs` is set.
* Installing (`nk_install`, used by `core/init/installer.c`): format with the chosen ops (volume label `NOOVEXOS`), mount, write `/.noovexos` = `"NoovexOS\n"`, create every directory, copy every file (reporting progress), write the meta file, then unmount. The installer sets the data partition's MBR type to `0x07` (NTFS), `0x0C` (FAT32), `0x0E` (FAT16) or `0x7F` (NoovexFS).

---

### 7.9 FAT drivers

#### `fs/fat/vfat.c`, read/write FAT16/FAT32 on sector callbacks

* State. The state is `g_vfat`:
  * geometry: `spc`, `rsvd`, `nfats`, `fatsz`, `root_lba`, `data_lba`, `clusters`, `rootclus`, `fsinfo`, `cbytes`;
  * the whole FAT in memory (`fat`), up to 16 MiB, otherwise mount returns -3;
  * one dirty flag per FAT sector (`fdirty`);
  * a one-cluster buffer;
  * a one-sector directory cache (`g_vfat_sec`/`g_vfat_secno`).
* Mount. Mount requires 512-byte sectors, a power-of-two `spc` and a volume no bigger than the partition. The type is FAT16 when there are fewer than 65525 clusters. Fewer than 4085 clusters is refused (-2). The cluster count is clamped to what the FAT can index. The free count is computed by scanning the FAT. On FAT32 the FSInfo (`0x41615252`) next-free hint is used.
* Flush. `vf_flush` writes each dirty FAT sector to every FAT copy, and on FAT32 updates FSInfo's free count (offset 488) and next-free (492).
* Directories. `vf_next` walks entries and assembles long file names. Each LFN entry holds 13 UTF-16 characters at offsets `{1,3,5,7,9,14,16,18,20,22,24,28,30}`. An LFN is accepted only when its checksum matches `vf_cksum` of the 8.3 name. Otherwise the short name is used, honouring the NT lowercase flags in byte 12 (`0x08` base, `0x10` extension). `vf_walk` resolves paths case-insensitively.
* Creating entries. `vf_mkent` stores a name as a pure 8.3 entry when `vf_make83` accepts it. Otherwise it writes LFN entries plus a `~N` alias from `vf_alias`. `vf_slot` finds free directory slots and grows a subdirectory by a zeroed cluster when needed (the FAT16 root cannot grow).
* Writes. `vf_write` always writes a new cluster chain (`vf_writechain`), then points the entry at it and frees the old chain. `vf_mkdir` writes `.` and `..`. `vf_delete` refuses non-empty directories.
* Format (`vf_format`):
  * cluster sizes from the Microsoft tables;
  * FAT16 needs at least 8400 sectors and at most 4194304 (2 GiB); FAT32 needs at least 66600 sectors;
  * 32 reserved sectors on FAT32, 1 on FAT16; 2 FATs; 512 root entries on FAT16;
  * the FAT size is iterated until the clusters fit;
  * on FAT32 a backup boot sector goes in sector 6 and FSInfo in sectors 1 and 7;
  * a volume-label entry is written in the root.

#### `fs/fat/fat_ata.c`, boot-media reader

A minimal read-only FAT16/FAT32 reader on the legacy ATA drive (`ata_read`). It handles an MBR partition (types `0x01,0x04,0x06,0x0B,0x0C,0x0E`) or a superfloppy whose sector 0 starts with a jump. It supports only the root directory and 8.3 names (`fat_name83`):

* `fat_listdir(cb)` backs the shell's `fatls`;
* `fat_read_file(name, out, cap)` backs `fatcat`, the `sh` fallback and `pkg` loading.

#### USB mass storage at `/mnt/usb`

The USB FAT driver lives in `drivers/usb/xhci.c`. `ufat_mount` reads the first MBR entry or a superfloppy and caches up to 32 root-directory entries in `ufs[32]` (`name[16]`, `size`, `clus`). `ufat_read` follows the cluster chain.

On the file-system side (`unixfs.c`):

* `ux_mount("...", "/mnt/usb")` is root only (syscall 168 and Linux `mount`, number 165). It initialises the device if needed (`msd_autoinit`, `ufat_mount`), creates `/mnt/usb`, and registers the read-only `USBFS` back end once.
* `ux_usb_idx` matches `/mnt/usb/<name>` case-insensitively.
* `usbfs_load` caches one whole file (up to 1 MiB) in `g_usbbuf`.
* `ux_umount` clears `g_usb_mounted`.
* Files appear in `readdir` through `k_readdir_extra`, and in `stat` with inode `21000+fi`.

---

### 7.10 Packages (`fs/pkg/pkg.c`)

`pkg_install(file, n, extract)` (shell command in `shell/commands.c`) works from an 8 MiB raw buffer (`g_pkg_raw`) and an 8 MiB inflate buffer (`g_pkg_inf`).

`pkg_open` recognises three kinds of package:

* `.deb` (`!<arch>` header, kind 1): `ar_find` walks the 60-byte `ar` headers (size is the decimal at bytes 48..57, bodies are 2-byte aligned).
  * From `control.tar*` (gunzipped when it starts `1F 8B`), `./control` supplies `Package`, `Version`, `Architecture` and `Description`.
  * `data.tar*` becomes the payload.
* Alpine `.apk` (gzip, kind 2): the whole file is gunzipped. `.PKGINFO` supplies `pkgname`, `pkgver`, `arch` and `pkgdesc`.
* Android/ZIP `.apk` (`PK\3\4`, kind 3): recognised, but not extracted.

`xz` and `zstd` payloads are passed through unchanged and are not unpacked.

`tar_walk` handles ustar headers: name at 0 (plus the 155-byte prefix at 345 for ustar), octal mode at 100, octal size at 124, type at 156. It visits regular files (`0`, NUL, `7`) and directories (`5`).

With `extract`:

1. `/pkg` and then `/pkg/<name>` are created (`unnamed` when there is no name).
2. `pkg_extract_cb` strips `./`, creates every intermediate directory with mode `0040755`, and stores each file in a free `FS` slot. The mode is `0100000 | (mode & 0777)` (`0644` when zero) and the owner is `g_uid`.
3. Each file is synced, then `dt_save()` is called.

Without `extract`, only the file count and total size (`g_pkg_nfiles`, `g_pkg_total`) are computed.

## Part 8: Storage drivers, PCI and the installer

This part covers everything between the PCI bus and a finished installation on disk: PCI configuration access, the four block-device back ends (ATA PIO, AHCI, NVMe, VirtIO-blk), the generic `gdisk_*` layer and its buffer cache, the graphical installer and disk manager in `core/init/installer.c`, USB mass storage, and the shell commands that work with disks.

All paths are relative to the repository root. The storage code is split into units (`drivers/bus/pci.c`, `drivers/storage/ahci.c`, `ata.c`, `gdisk.c`, `nvme.c`, `fs/nvx/nvxfs.c`, `fs/fat/fat_ata.c`, `core/init/installer.c`), each with a header under `include/nvx/`. VirtIO-blk is a `MODS` driver that sees only `include/kapi.h` (see 8.6).

The file names do not match their contents. The code is not split up the way the file names suggest: | File | What it actually contains | |---|---| | `drivers/storage/ahci.c` | A standalone, read-only `ahci_identify()` probe with its own static buffers. It is used only by the `disks` command. The rest of the file is the PCnet NIC driver (`wcsr`, `nic_up_raw`, `nic_send`, ...). | | `drivers/storage/gdisk.c` | The real AHCI driver: `AHCI_*` addresses, `ahci_cmd`, `ahci_read`, `ahci_write1`, `ahci_flush` and `ahci_init`. | | `drivers/storage/nvme.c` | The NVMe driver, then the generic disk layer (`DK_*`, `gdisk_*`, buffer cache, `dk_ratio`), then the first installer helper (`ins83`, the `_binary_inst_*` externs). | | `core/init/installer.c` | The Disk Manager (`dm_*`), the kernel snapshot and checks (`inst_snapshot_data`, `kimg_check`, `kcheck_run`, `memmap_run`, `memtest_run`), the boot-partition builder (`inst_build_fs`), the installer UI and its run logic (`ip_*`), and the boot menu (`bootmenu_*`). |

---

### 8.1 PCI (`drivers/bus/pci.c`)

#### Configuration access

The kernel uses legacy configuration mechanism #1 only, through I/O ports `0xCF8`/`0xCFC`. There is no ECAM/MMCONFIG.

```c
static u32 pci_cfg(int bus,int slot,int fn,int off){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); return inl(0xCFC); }
static void pci_cfgw(int bus,int slot,int fn,int off,u32 v){ ... outl(0xCF8,a); outl(0xCFC,v); }
```

`drivers/net/ethernet/nic.c:39` defines an identical writer called `pci_wcfg`. The `disks` command and the PCnet code use that one.

#### Functions

| Symbol | Scope | Behaviour |
|---|---|---|
| `pci_scan()` | buses 0-3, function 0 only | Fills the parallel arrays `pci_ven/pci_dev/pci_cls/pci_sub/pci_bus/pci_slot`, holding at most `PCI_MAX` = 24 entries, with the count in `pci_n`. `kmain` calls it (`core/init/main.c:145`, logged as `"boot: pci scanned"`). Consumers such as `pci`, `net`, `disks`, `wifi` and Settings rescan lazily with `if(!pci_n)pci_scan()`. |
| `pci_find(cls,sub,prog,&bus,&slot,&fn)` | all 256 buses, all functions of multifunction devices (header bit `0x800000` at offset `0x0C`) | Returns the first device that matches class and subclass. `prog<0` means any programming interface. The source comment gives the reason for the full scan: "NVMe/AHCI often live on high buses". |
| `pci_quiesce()` | all 256 buses and functions, header type 0 only | Clears Bus-Master Enable (command bit 2) on devices that the firmware may have left doing DMA. It counts them in `g_pci_quiesced`, which `memmap` reports. It runs right after `pci_scan` at boot and logs `"boot: stopped DMA on devices the firmware left running"`. |

Which devices `pci_quiesce` stops:

| Class | Stopped? |
|---|---|
| `0x01` storage, `0x02` network, `0x04` multimedia, `0x0D` wireless | yes |
| `0x0C` serial bus, subclass `0x05` (SMBus) | no |
| `0x0C03` USB with prog-if other than `0x30` (UHCI/OHCI/EHCI) | yes |
| `0x0C03` prog-if `0x30` (xHCI) | only when BAR0 is a 64-bit BAR (`(b0&6)==4`) and the upper half (BAR1, offset `0x14`) is non-zero, meaning the controller lives above 4 GiB |
| other `0x0C` subclasses | yes |
| anything else | no |

The command register is written back as `(cmd&0xFFFF)&~0x04`, which leaves the memory and I/O decode bits untouched.

#### BAR handling and the `< 0xF0000000` limit

None of the storage drivers map BARs. They cast the 32-bit physical BAR value straight to a pointer, so a BAR is accepted only if it falls in a range they can reach that way:

| Driver | BAR used | Rejected when |
|---|---|---|
| AHCI (`ahci_init`, `gdisk.c`) | BAR5 (`0x24`, ABAR) | it is an I/O BAR (`bar5&1`), or `bar5<0x1000`, or `bar5>=0xF0000000` to `ahci_init` returns 0 |
| NVMe (`nvme_init`, `nvme.c`) | BAR0 (`0x10`) plus BAR1 (`0x14`) | BAR1 is non-zero (the 64-bit BAR has a high half) to `nvme_failstage=2`; it is an I/O BAR to 3; `base<0x1000` or `base>=0xF0000000` to 4 |
| VirtIO (`vio_open`, `virtio.c`) | BAR0 | it is not an I/O BAR (`!(bar&1)`). Only the legacy I/O-port interface is supported. |

Before touching registers, AHCI and NVMe set command bits `0x06` (Memory Space and Bus Master). `kapi_pci_enable` (`sys/native/kapi.c:16`), which the VirtIO module uses, ORs in `0x407` instead: I/O, Memory, Bus Master and INTx-disable.

---

### 8.2 ATA PIO (`drivers/storage/ata.c`)

The ATA driver handles the primary legacy channel only: `ATA_IO` = `0x1F0` and `ATA_CTL` = `0x3F6`. `ATA_CTL` is defined but never used. All transfers are LBA28 PIO with polling.

| State | Meaning |
|---|---|
| `g_ata_drv` | `0` = master, `1` = slave, `-1` = none |
| `g_disk_sectors` | sector count from IDENTIFY, clamped to `0xFFFFFFFF` |

#### `ata_detect()`

For drive 0 and then drive 1, `ata_detect()` does the following:

1. It selects the drive with `0xA0|(drv<<4)`, zeroes the count and LBA registers, and issues IDENTIFY DEVICE `0xEC`.
2. It skips the drive if status reads `0x00` or `0xFF` (nothing there), or if BSY never clears.
3. It treats non-zero LBA-mid or LBA-high values (`0x1F4`/`0x1F5`) as an ATAPI or other device and skips it.
4. It waits for DRQ and fails on ERR. It then reads 256 words and computes the size: if the LBA48 count in words 100-103 is non-zero it uses that (clamped to 32 bits), otherwise the LBA28 count in words 60-61.

#### Transfers

| Function | ATA command | Notes |
|---|---|---|
| `ata_read(lba,count,buf)` | `0x20` READ SECTORS | Splits the request into chunks of at most 255 sectors and calls `ata_poll()` before each 512-byte block. |
| `ata_write(lba,buf)` | `0x30` WRITE SECTORS | Writes one sector, then `0xE7` FLUSH CACHE after every sector. Only BSY is waited on, so a flush error is not checked. |
| `ata_writen(lba,count,buf)` | `0x30`, then a single `0xE7` | Multi-sector write in chunks of at most 255 sectors, with one cache flush at the end. |

`ata_poll()` returns `-1` on ERR (`0x01`) or DF (`0x20`), and `0` once BSY is clear and DRQ is set. Every loop is bounded at 4,000,000 iterations.

Two other files call `ata_detect()` directly: `fs/fat/fat_ata.c` (`fat_mount`) and `fs/nvx/nvxfs.c` (`nvx_locate`, which runs from `disk_init()` at boot). When an IDE disk exists, NVXFS uses the `ata_*` functions directly (`g_nvx_generic=0`) rather than the gdisk layer.

---

### 8.3 AHCI (`drivers/storage/gdisk.c`)

#### Fixed DMA addresses

The driver uses identity-mapped physical addresses as its DMA memory, not buffers it allocates:

| Macro | Address | Contents |
|---|---|---|
| `AHCI_CLB` | `0x00B00000` | Command list. Only slot 0 is used. |
| `AHCI_FIS` | `0x00B01000` | Received-FIS area |
| `AHCI_CT` | `0x00B02000` | Command table: the CFIS at `+0`, one PRDT entry at `+0x80` |
| `AHCI_BUF` | `0x00B03000` | Data bounce buffer |

`ahci_cmd` refuses `count>360`. Since 360 x 512 = `0x2D000`, `AHCI_BUF + 0x2D000` = `0x00B30000`, which is exactly where `NVME_ASQ` begins. The two drivers share one arena without overlapping.

#### `ahci_init()`

1. It calls `pci_find(0x01,0x06,-1,...)` to find a SATA controller on any bus, with any prog-if.
2. It validates BAR5 (see 8.1), then enables memory space only. Bus mastering stays off until every implemented port is stopped: for each bit in `PI` it clears `PxCMD.ST` and waits for `CR`, clears `FRE` and waits for `FR`, sets `PxIE` to 0 and clears `PxIS`. It also clears `GHC.IE` and `IS`. Only then does it set Bus-Master Enable. Before v8.98 bus mastering was turned on first, so a controller whose BAR was then rejected, or ports the firmware left running, could resume DMA into memory the kernel had reclaimed.
3. It sets `GHC.AE` (ABAR + `0x04`, bit 31) and reads `PI` (ABAR + `0x0C`).
4. Port selection. It walks the implemented ports in order. It skips a port unless `PxSSTS.DET == 3` and the signature `PxSIG` (`+0x24`) is `0x00000101` (SATA disk) or `0`. On the first port that qualifies it:
   - stops the port by clearing ST and FRE, then waits for CR and FR to clear,
   - points `PxCLB`/`PxFB` at `AHCI_CLB`/`AHCI_FIS` and zeroes them,
   - sets FRE and then ST,
   - sets `ahci_port`, and
   - issues IDENTIFY (`0xEC`). If IDENTIFY succeeds, it stores the size in `ahci_sectors` (LBA48 words 100-102, else LBA28 words 60-61, clamped to 32 bits) and returns 1. Otherwise it moves on to the next port.

#### `ahci_cmd(lba,count,buf,write,atacmd)`

```c
ch[0]=(5)|(write?(1u<<6):0)|((count?1u:0u)<<16); ch[1]=0; ch[2]=AHCI_CT; ch[3]=0;   /* CFL=5 dwords, W bit, PRDTL */
prdt[0]=buf; prdt[3]=(count*512u-1u);                                                /* single PRD */
fis[0]=0x27; fis[1]=0x80; fis[2]=(u8)atacmd; fis[7]=0x40;                            /* H2D, C=1, LBA mode */
fis[4..6]=lba[0..23]; fis[8]=lba[24..31]; fis[9]=fis[10]=0; fis[12..13]=count;
```

The command waits for `PxTFD` (`+0x20`) to drop BSY (`0x80`) and DRQ (`0x08`). It then writes `PxCI=1` (`+0x38`) and polls up to 20,000,000 iterations for bit 0 to clear. It fails if `PxIS` bit 30 (TFES) is set, or if `PxTFD.ERR` is set at the end. LBA bits 32-47 are always zero, so only the first 2 TiB are addressable.

| Function | ATA opcode | Path |
|---|---|---|
| `ahci_init` (IDENTIFY) | `0xEC` IDENTIFY DEVICE | data goes to `AHCI_BUF` |
| `ahci_read(lba,cnt,out)` | `0x25` READ DMA EXT | reads into `AHCI_BUF`, then copies to `out` |
| `ahci_write1(lba,in)` | `0x35` WRITE DMA EXT | copies one sector into `AHCI_BUF` |
| `gdisk_writen` (AHCI branch) | `0x35` | chunks of at most 128 sectors |
| `ahci_flush()` | `0xEA` FLUSH CACHE EXT | issued with `count=0` (PRDTL 0) |

#### The separate `ahci_identify()` probe (`drivers/storage/ahci.c`)

`ahci_identify(abar,port,model,&sectors)` is a self-contained IDENTIFY that uses static, aligned `.bss` buffers: `ahci_clb[1024]`, `ahci_fis[256]`, `ahci_ctb[256]` and `ahci_idbuf[512]`. It also returns the 40-character model string from words 27-46. Its acceptance rule is stricter than `ahci_init`'s: it requires `PxSIG == 0x101` exactly.

Only `disks` calls it. The probe itself (`ahci_identify_raw`) has to point `PxCLB`/`PxFB` at its own buffers. Since v8.95 the public wrapper `ahci_identify` saves `PxCLB`, `PxCLBU`, `PxFB`, `PxFBU` and `PxCMD` first. If the probe changed them, it stops the port (clears ST and FRE and waits for CR and FR, bits 15 and 14), writes the four address registers back and turns FRE and ST back on only if they were on before.

Note: Before v8.95 nothing pointed the port back, so after `disks` the main driver (`ahci_cmd`, which always builds its command at `AHCI_CLB`) issued commands through a list the controller was no longer reading. Disk access then failed until reboot. Checked in QEMU (q35, ICH9 AHCI): `PxCLB` of the active port reads `0x00B00000` before and after repeated `disks` calls.

---

### 8.4 NVMe (`drivers/storage/nvme.c`)

#### Memory layout

| Macro | Address | Use |
|---|---|---|
| `NVME_ASQ` | `0x00B30000` | Admin submission queue (64-byte entries) |
| `NVME_ACQ` | `0x00B31000` | Admin completion queue (16-byte entries) |
| `NVME_IOSQ` | `0x00B32000` | I/O submission queue, qid 1 |
| `NVME_IOCQ` | `0x00B33000` | I/O completion queue, qid 1 |
| `NVME_ID` | `0x00B34000` | Identify data |
| `NVME_BUF` | `0x00B35000` | One-block data buffer, used as PRP1 only |

#### Initialisation and the `nvme_failstage` codes

`nvme_init()` sets `nvme_failstage` as it goes, and the `disks` command prints it as `init: FAILED stage N`. It enables memory space only at first, and turns on bus mastering after `CC.EN` is cleared and `CSTS.RDY` reads 0, so a controller the firmware left running cannot DMA in the meantime.

| Stage | Meaning |
|---|---|
| 0 | no controller found (`pci_find(0x01,0x08,...)`, `nvme_found=0`) |
| 1 | controller found |
| 2 | BAR1 is non-zero (the 64-bit BAR has a high half) |
| 3 | BAR0 is an I/O BAR |
| 4 | the BAR base is `<0x1000` or `>=0xF0000000` |
| 5 | registers are mapped |
| 12 | `CAP.MQES+1 < 2`. The queue depth is `min(MQES+1, 64)`. |
| 13 | `CAP.MPSMIN` (`cap>>48 & 0xF`) is non-zero, i.e. the controller cannot use 4 KiB pages |
| 14 | `CSTS.RDY` did not clear after `CC=0` |
| 6 | enable failed: RDY was not set, or CFS was set |
| 7 | the controller is enabled |
| 8 | Identify Namespace failed for namespace 1 and for the first namespace in the active list |
| 15 | the LBA format is not 512 B-4 KiB (`LBADS` outside 9-12) |
| 11 | the namespace format carries metadata |
| 9 | Create I/O CQ failed |
| 10 | Create I/O SQ failed |

The enable sequence:

- Timeout. It derives the timeout from `CAP.TO` as `to*500000` loop iterations, clamped to the range 1 M-60 M.
- Queue registers. It writes `AQA=((q-1)<<16)|(q-1)`, then `ASQ` (`0x28`) and `ACQ` (`0x30`).
- Enable. It writes `CC=(4<<20)|(6<<16)|1`: IOCQES=16 B, IOSQES=64 B, EN.

Doorbells are at `0x1000 + (2*qid + is_cq) * (4 << CAP.DSTRD)` (see `nvme_db`).

#### Command opcodes

| Queue | Opcode | Command | Arguments used |
|---|---|---|---|
| admin | `0x06` | Identify | CNS 0 (namespace) into `NVME_ID`. If that fails, CNS 2 (`d10=2`, active namespace list), then the first NSID it returns. |
| admin | `0x05` | Create I/O CQ | `d10=((q-1)<<16)\|1`, `d11=1` (physically contiguous) |
| admin | `0x01` | Create I/O SQ | `d10=((q-1)<<16)\|1`, `d11=(1<<16)\|1` (CQID 1, PC) |
| I/O | `0x02` | Read | `e[10]=slba`, `e[12]=0`, so NLB=0: one native block per command |
| I/O | `0x01` | Write | same layout |
| I/O | `0x00` | Flush | `nvme_flush()` |

`nvme_wait()` matches completions by command ID (`nvme_next_cid`, which wraps at `0xFFF0`) and handles the phase tag. When the loop's 20 M iterations run out it sets `nvme_lasterr=1`; when `CSTS.CFS` is set it sets `nvme_lasterr=2`. Either way it also sets `nvme_bad=1`. The next `nvme_cmd` sees `nvme_bad` and calls `nvme_init()` again before retrying, which resets the controller.

#### 4K sectors: `nvme_ratio`

The rest of the kernel addresses everything in 512-byte LBAs. NVMe bridges the difference with these variables:

- `g_nvme_bsz` is the native block size. It is 0 when the format is unsupported.
- `nvme_ratio = bsz/512`.
- `nvme_sectors = nsze*(bsz/512)` is expressed in 512-byte units and clamped to 32 bits.
- `nvme_cblk` caches which native block currently sits in `NVME_BUF`.

| Function | Behaviour |
|---|---|
| `nvme_read1(lba,out)` | Loads native block `lba/ratio` (skipped if it is already cached) and copies out the 512-byte slice at `(lba%ratio)*512`. |
| `nvme_write1(lba,in)` | When `ratio>1`, it reads the native block first (read-modify-write), patches the slice and writes the block back. |
| `nvme_read(lba,cnt,out)` | A loop over `nvme_read1`. |
| `nvme_writen(lba,cnt,in)` | Writes whole native blocks directly when the request is aligned and long enough. Otherwise it falls back to `nvme_write1`. |

The generic layer exposes the ratio as `dk_ratio()`:

```c
static u32 dk_ratio(void){ return (g_dktype==DK_NVME&&g_nvme_bsz>512)?g_nvme_bsz/512:1u; }
```

---

### 8.5 Generic disk layer (`drivers/storage/nvme.c`, after the NVMe driver)

```c
enum { DK_NONE, DK_IDE, DK_AHCI, DK_NVME, DK_VIRTIO };
static int g_dktype=DK_NONE; static u32 g_dksectors=0;
```

`gdisk_init()` chooses exactly one disk, in this priority order:

1. IDE, if `ata_detect` has already found a drive (`g_ata_drv>=0`)
2. AHCI (`ahci_init`)
3. NVMe (`nvme_init`)
4. VirtIO (`virtio_blk_probe`)

`gdisk_ensure()` runs `gdisk_init` once and records that in `g_gdisk_probed`. The source comments call this lazy ("only when disks/installer is used"). However, `disk_init()` to `nvx_locate()` calls `gdisk_ensure()` at boot whenever no IDE drive exists (`fs/nvx/nvxfs.c:32`), so on AHCI, NVMe and VirtIO machines the probe actually happens during `kmain`.

`gdisk_name()` returns `"IDE/ATA"`, `"AHCI/SATA"`, `"NVMe"`, `"VirtIO"` or `"none"`.

| Function | Routing / semantics |
|---|---|
| `gdisk_read(lba,cnt,buf)` | `ata_read` / `ahci_read` / `nvme_read` / `virtio_blk_read` |
| `gdisk_write(lba,buf)` | one sector: `ata_write` / `ahci_write1` / `nvme_write1` / `virtio_blk_write(...,1,...)` |
| `gdisk_writen(lba,cnt,buf)` | Invalidates the cache first. Then IDE uses `ata_writen`; NVMe uses `nvme_writen`; VirtIO and AHCI write in chunks of at most 128 sectors. |
| `gdisk_readn(lba,cnt,buf)` | Invalidates the cache, then calls `gdisk_read` in chunks of at most 128 sectors. |
| `gdisk_flush()` | NVMe Flush / AHCI `0xEA` / VirtIO `T_FLUSH`. Returns 0 for IDE, because ATA writes already flush. `power_off()` and `power_reboot()` call it whenever `g_gdisk_probed` is set (`core/power.c:73,87`). |

#### Buffer cache

The cache is an LRU of single 512-byte sectors in front of `gdisk_read`:

```c
#define BC_N 64
static struct { u32 lba; u32 tick; int valid; u8 data[512]; } g_bc[BC_N];
```

- `gdisk_read_cached(lba,cnt,buf)`: when `cnt==1`, it looks the sector up and fills a slot on a miss. The victim is the first invalid slot, otherwise the slot with the lowest tick. When `cnt!=1`, it calls `bc_invalidate(lba,cnt)` and then reads straight from the disk.
- `bc_invalidate(lba,cnt)` is called from `gdisk_writen`, `gdisk_readn`, `gdisk_write_through` and `nk_wr` (`fs/ntfs/ntfsk.c`).
- `g_bc_hits` and `g_bc_miss` are the counters that `bcstat` prints.

`nvx_rd()` in `fs/nvx/nvxfs.c` is the cached reader for NVXFS in generic mode.

All LBAs and sector counts in this layer are `u32`, so every back end is limited to 2^32 x 512 B.

---

### 8.6 VirtIO-blk module (`drivers/virtio/virtio_blk.c`)

This driver is built as a separate object (`build.sh` `MODS=... drivers/virtio/virtio.c ... drivers/virtio/virtio_blk.c ...`) and linked in. It talks to the kernel only through `include/kapi.h`, and its exports are declared in `include/drivers.h`.

- Probe. `vio_open(0x1001,...)` finds the legacy (transitional) virtio-blk device, vendor `0x1AF4`. The driver resets the device and negotiates only `VIRTIO_BLK_F_FLUSH` (bit 9). It sets up virtqueue 0, then allocates two DMA buffers with `kapi_dma_pages`: a bounce buffer of `MAXSEC` (128) x 512 B, and a one-page header. It reads the capacity from the legacy device-config area at I/O offsets `0x14`/`0x18`, sets DRIVER_OK, and logs `"virtio-blk: up"`.
- Requests. `vb_req(type,lba,count,towrite)` builds a 2- or 3-descriptor chain: the 16-byte header (`type`, reserved, `sector`), an optional data buffer (flagged device-writable for reads), and a 1-byte status (device-writable). It then kicks the queue and polls `vq_used` with a 5-second timeout (`kapi_ns`). A timeout logs `"virtio-blk: request timeout"` and sets `ready=0`.

| `type` | Request |
|---|---|
| 0 | `VIRTIO_BLK_T_IN`, a read |
| 1 | `VIRTIO_BLK_T_OUT`, a write |
| 4 | `VIRTIO_BLK_T_FLUSH`. It is sent only if the device offered the flush feature; otherwise `virtio_blk_flush()` returns 0. |

Reads and writes are split into chunks of at most 128 sectors and are bounds-checked against `capacity`.

---

### 8.7 The installer (`core/init/installer.c`)

#### Entry points

| Caller | Call |
|---|---|
| Boot menu item 3, "Install NoovexOS" (`core/init/bootmenu.c` `BM_ITEMS`) | `bootmenu_activate` to `ip_open(ST_BOOTMENU)` |
| Shell `install` (requires root via `need_root`) | `ip_open(ST_DESKTOP)` |
| Disk Manager buttons "Install to selected" / "Install whole disk" | `dm_do_install` to `ip_open_preset(ret, mode, part)` |

`ip_open()` resets all state, sets `g_state=ST_INSTALL` and opens page `IP_FS`. `ip_load()` re-reads the MBR into `ip_mbr` and the arrays `ip_ptype/ip_pst/ip_psz`. Start and size are converted to 512-byte units with `*dk_ratio()`. Any entry of type `0xEE` sets `ip_gpt`.

#### Pages and flow

```text
IP_FS --Continue--> IP_WELCOME/IP_TARGET --Continue--> IP_CONFIRM --Install--> IP_RUN --> IP_DONE --Reboot--> power_reboot()
  ^  (Esc)              | Skip -> setup_begin()            | Back -> IP_TARGET            +--> IP_ERROR --Back--> IP_WELCOME
```

The `ip_act` button IDs are: 2 = Continue/Install, 1 = Back (confirm page), 3 = Back (error page), 5 = Reboot, 6 = Skip, 10 = the disk row, and 40-43 = the file-system radio buttons. Keyboard handling (`ip_input`):

- Enter activates the focused button (`g_ip_focus`).
- Left/Right and Tab (key codes 17/18 and `'\t'`) move the focus.
- Up/Down (19/20) change the file system on `IP_FS`.
- Esc goes back one page.

Install mode. "Continue" on the disk page always sets `ip_mode=0` (whole disk). On `IP_TARGET`, Up/Down changes `ip_mode`, but Continue resets it. Mode 1 (install into an existing partition) is reachable only through `ip_open_preset`, i.e. from the Disk Manager. `ip_open_preset` returns early, leaving the user on `IP_FS`, when `ip_disk_ok()` fails.

#### File-system choices and sizes

| `g_ip_fs` | UI label | Data partition type | Formatter | Minimum data size enforced in `ip_run` |
|---|---|---|---|---|
| 0 | NoovexFS ("native, recommended") | `NVX_PTYPE` = `0x7F` (`fs/nvx/nvxfs.c:10`) | `n4_format(dsz)` (`fs/nvx/nvx4.c`) | - |
| 1 | NTFS | `0x07` | `nk_install(&g_rfs_ntfs,...)` | 32768 sectors: "NTFS needs at least 16 MB - choose FAT16 instead." |
| 2 | FAT32 | `0x0C` | `nk_install(&g_rfs_fat32,...)` to `vf_fmt32` | 66600 sectors: "FAT32 needs at least 33 MB - choose FAT16 instead." |
| 3 | FAT16 ("partitions up to 2 GB") | `0x0E` | `nk_install(&g_rfs_fat16,...)` to `vf_fmt16` | 8400 sectors: "FAT16 needs at least 4 MB." The size is also capped at 4186112 sectors. |

Size constants:

```c
#define INST_BOOTP (dk_ratio()>1?557056u:131072u)          /* boot partition: 64 MiB, or 272 MiB on 4K-sector disks */
#define INST_DATAP (((NVX_REGION_SECS)+2047u)&~2047u)      /* NVX_REGION_SECS = 8+(24+1)*4096 = 102408 -> 104448 sectors */
```

- `ip_need() = INST_BOOTP + INST_DATAP`.
- `ip_disk_ok()` requires a disk of type other than `DK_NONE` with `g_dksectors >= 2048 + ip_need() + 2048`, and rejects NVMe when `g_nvme_bsz==0`.
- `ip_part_ok(e)` rejects GPT disks, partitions smaller than `ip_need()`, and any partition starting before LBA 64. It also requires a second free MBR slot for the data partition.

In whole-disk mode the data partition is fixed at `INST_DATAP` sectors ( about 51 MiB) and starts right after the boot partition. In partition mode it fills the rest of the chosen partition.

`nk_install(o,base,nsect,copy,prog)` (`fs/ntfs/ntfsk.c:168`) does the following:

1. Formats the partition with label `"NOOVEXOS"` and mounts it.
2. Writes `/.noovexos`.
3. Recreates the directory tree (`DT`).
4. If `ip_copy` is set, copies every RAM file (`FS[]`), reporting progress through `ip_nk_prog`.
5. Writes the metadata and unmounts.

Its return codes: -1 = format failed; -2 = mount failed; -3 = marker write failed; -4 = file copy failed. `ip_run` reports -1 as "Formatting the partition failed." and anything else as "Writing files to the new partition failed."

#### The kernel snapshot

`kmain` calls `inst_snapshot_data()` first, right after `.bss` is zeroed (`core/init/main.c:124`). This happens before anything can modify the loaded image. The function copies the kernel image at `0x100000` into `g_kimg_snap[KSNAP_MAX]` (`KSNAP_MAX` = `0x800000`, 8 MiB, the same as the build limit). The length comes from the size stamp at `0x100000+24`; if that is outside 4096-`KSNAP_MAX`, it falls back to `__file_end`.

Note: Up to v8.94 the buffer was 3 MiB while the kernel had grown past 5 MB, so the snapshot was always empty and every installation stopped with "The kernel image could not be captured at boot." The buffer lives in `.bss`, which starts at `0xC00000`, so the larger size does not touch the fixed low-memory regions.

`kimg_check()` verifies the snapshot against `g_buildsig` using SHA-256 twice:

- the immutable part up to `__immutable_end`, with the hash field at `sig+32` zeroed, compared with `g_buildsig.sha256`;
- the whole file, with the field at `sig+88` also zeroed, compared with `g_buildsig.sha256_full`.

The shell command `kcheck` (`kcheck_run`) prints the same comparison and writes it to the kernel log. It also lists the 4 KiB code pages that have changed since boot. On v8.96 in QEMU (BIOS and UEFI) it reports `code MATCHES   whole file MATCHES` for the 5,298,992-byte kernel.

#### The boot partition: `inst_build_fs`

This is a C port of `mkhybrid.py build_fat32` (the comment is in `nvme.c`). It builds a complete FAT32 image in RAM: `img=kmalloc(cap*bps)`, where `cap` comes from `inst_fs_layout`.

| Field | Value |
|---|---|
| bytes/sector | `512*dk_ratio()` |
| sectors/cluster | 1 |
| reserved | 32 |
| FATs | 2 |
| media | `0xF8` |
| root cluster | 2 |
| FSInfo | sector 1 |
| backup boot | sector 6 (the first `2*bps` bytes are copied there) |
| OEM / FS type | `"NOOVEXOS"` / `"FAT32   "` |
| volume ID | `0x4E56584F ^ g_ticks` |
| label | `g_dm_name` (default `"NOOVEXOS"`, editable in the Disk Manager) |
| minimum | `psec - dstart >= 65525` clusters, otherwise returns `-2` |

Contents (all directory timestamps are fixed at `0x6000`/`0x5B3B`):

```text
/            volume label entry
/boot/kernel64.bin       (copy 1 of the snapshot)
/boot.cfg                (assets/installer/inst_boot.cfg: kernel=/boot/kernel64.bin, cmdline=noovexos, ...)
/KERNEL64.BIN            (copy 2)
/EFI/BOOT/BOOTX64.EFI    (boot/uefi/BOOTX64.EFI, embedded by build.sh as inst_efi.bin)
```

The installer writes only `used = dstart + (next-2)` sectors, which ends at the last allocated cluster. The FAT, however, describes the whole partition. `ko1` and `ko2` are the byte offsets of the two kernel copies inside the image. `ip_mem_verify` uses them to compare the copies against the snapshot, both after building and after verification. If a copy differs, it rewrites the copy, waits 1 s and compares again, to tell RAM that does not hold data apart from something else overwriting it.

#### `ip_run()` stages

`ip_prog` runs from 0 to 1000 (per-mille). The banner on the running page uses its own 8 labels: "Preparing disk", "Partitioning", "Formatting", .... The `IP_STAGES[]` strings are defined but never displayed.

| `ip_stage_go(s,prog)` | Work done |
|---|---|
| 0, 0 | Validate the size stamp (4096-`0x800000`), the snapshot, `kimg_check` and `mm_area_conflicts()`. Compute the layout. |
| 1, 40 | Build the boot-partition image and run `ip_mem_verify` on both kernel copies. |
| 2, 90 | Write the MBR (below). On a GPT disk in whole-disk mode, also zero the last native block, where the backup GPT header lives. |
| 3, 110 | Write `inst_stage2.bin` to LBA 1.... This step is skipped when `dk_ratio()>1` (`s2s=0`). |
| 4, 140 to 700 | Write `used` sectors of the image to `bst`, one sector at a time with `gdisk_write`, refreshing the UI every 128 sectors. |
| 5, 700 | FS 1-3: `nk_install` (progress 760-860 via `ip_nk_prog`). |
| 6, 760 | FS 0: set `g_nvx_base`/`g_nvx_secs`, then call `n4_format(dsz)`. |
| 7, 870 to 1000 | Verify: read back the MBR, stage2 and every boot-partition sector and compare them. Recheck the kernel copies in RAM. Call `gdisk_flush()`. |
| done | `ip_prog=1000`, page `IP_DONE`, `ip_load()` |

#### MBR writing

```c
for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;   /* myboot stage1 (assets/installer/inst_boot.bin) */
for(int e=0;e<4;e++)mbr[446+e*16]=0;                                     /* clear every active flag */
boot: pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5..7]=FE FF FF; start=bst/R; size=bsz/R;
data: pe[2]=2; pe[4]=0x07|0x0C|0x0E|0x7F; ... start=dst/R; size=dsz/R;
if(ip_mode==0) *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks;                /* fresh disk signature */
```

How the two modes differ:

- Whole-disk mode (0) starts from a zeroed sector. The boot partition goes in slot 0 at LBA 2048 and the data partition in slot 1. Any existing GPT is overwritten: the protective `0xEE` entry disappears, and the backup header is zeroed.
- Partition mode (1) keeps the other entries. The chosen slot becomes the boot partition, and the first free slot becomes the data partition.

Partition entries are written in native blocks (`/R`). Everything else in the installer works in 512-byte units.

#### Installer error messages

| Message (`ip_err`) | Detail (`ip_err2`) | Cause |
|---|---|---|
| The kernel size stamp is invalid. | - | stamp outside 4096-8 MiB |
| The kernel image could not be captured at boot. | Run kcheck in the terminal and send a photo of the result. | no snapshot, length mismatch, or `kimg_check<0` |
| The kernel code / data loaded at boot is not the original build. | (same) | SHA-256 mismatch |
| The firmware uses memory that NoovexOS also uses. | Run memmap in the terminal... | `mm_area_conflicts()` |
| The chosen partition can't be used. | - | `ip_part_ok` failed |
| The disk is too small. | - | `dst+dsz > g_dksectors`, which also covers `DK_NONE` |
| The partition is too small for FAT16/NTFS/FAT32. | (minimums above) | size checks |
| Not enough memory to build the boot partition. | - | `kmalloc` failed |
| The boot partition image could not be built. | The partition is too small for FAT32 on this disk. | `inst_build_fs` returned -1 or -2 |
| Memory at *addr* does not keep data / was changed by something else / was changed once | *n* differed, ... | `ip_mem_verify` |
| Writing the partition table failed. | The disk reported an error. | MBR write |
| Writing the boot loader failed. | - | stage2 write |
| Writing the boot partition failed. | The disk reported an error. | image write |
| Formatting the partition failed. / Writing files to the new partition failed. | the FS name | `nk_install` |
| Formatting the data partition failed. | `g_n4_err` | `n4_format` |
| The disk did not confirm that the data was saved. | Try again, or use another disk. | `gdisk_flush()` failed |
| Verification failed - the disk returned different data. | `Sector N differs (NVMe, timeout)` / `Reading sector N failed (...)` | read-back mismatch or read error, with `nvme_lasterr` appended |

---

### 8.8 Disk Manager (`dm_*`, `core/init/installer.c` lines 1-104)

The Disk Manager is a graphical MBR editor. It works on `g_dm_p[4]` (`dmpart {used,type,start,size}`), loaded by `dm_load()` from LBA 0 with `*dk_ratio()` scaling.

| Action | Function | Effect |
|---|---|---|
| Create partition | `dm_create` | Uses the first empty slot. Starts at `dm_free_start()` (end of the last partition, at least 2048, rounded up to 2048), fills the rest of the disk, sets type `0x0C`, and writes LBA 0 immediately. |
| Delete partition | `dm_delete` | Zeroes the entry and writes LBA 0. |
| Install to selected / whole disk | `dm_do_install(0/1)` | `ip_open_preset(..., whole?0:1, idx)` |
| Volume name | `g_dm_name` | Up to 11 characters `[A-Za-z0-9_-]`. Used as the FAT32 boot-volume label. |
| Back / Esc | - | Returns to `g_dm_ret` |

Rows show "FAT32" (types `0x0B`/`0x0C`, with the volume label read from the BPB at offset 71), "NTFS", "Linux" or "used", plus the size, the start LBA and `[active]`. Messages include "No free partition slots (max 4).", "No free space on the disk.", "Partition created.", "Partition deleted.", "Select a partition to delete." and "Select a partition first."

`main.c` dispatches `ST_DISKMGR` to `dm_input`/`dm_present`. Since v8.95 there are two ways in, both through `dm_open(ret)` (`dm_load()`, `g_dm_open=1`, clear the selection and messages, remember `ret`, `g_state=ST_DISKMGR`):

| Entry | Where | Returns to |
|---|---|---|
| `diskmgr` or `partitions` shell command (root only) | `shell/commands.c` | the live session or the desktop |
| P on the installer's welcome or target page ("Press P to edit partitions.") | `ip_input` | the installer (Esc reloads its disk list with `ip_load()`) |

From the Disk Manager, "Install to selected" opens the installer in mode 1 (into an existing partition). Before v8.95 nothing set `ST_DISKMGR`, so the screen and installer mode 1 could not be reached.

---

### 8.9 USB mass storage (`drivers/usb/xhci.c`)

This is a Bulk-Only Transport class driver on top of the xHCI stack.

| Address | Use |
|---|---|
| `MSD_INPUTCTX` `0x00A50000` / `MSD_DEVCTX` `0x00A51000` | slot contexts |
| `MSD_EP0RING` `0x00A52000`, `MSD_INRING` `0x00A53000`, `MSD_OUTRING` `0x00A54000` | transfer rings (256 TRBs, link TRB at index 255) |
| `MSD_CBW` `0x00A55000`, `MSD_CSW` `0x00A55100` | CBW (`0x43425355` "USBC") and CSW (`0x53425355` "USBS") |
| `MSD_DATABUF` `0x00A56000` | sector buffer ("up to 64 KB") |

- Enumeration. `msd_autoinit()` to `xhci_msd_enum(port)` looks for an interface of class `0x08` with bulk IN and OUT endpoints. It then polls TEST UNIT READY up to 8 times and finally issues READ CAPACITY(10), which sets `msd_lastlba` and `msd_bsize`.
- Commands. `msd_bot()` sends one command as CBW, optional data stage, then CSW, and returns `bCSWStatus`.
- Failures. A failed step is recorded in `xhci_failstep`, which `usbdisk` prints.

| SCSI op | Function |
|---|---|
| `0x00` TEST UNIT READY | `scsi_tur` |
| `0x25` READ CAPACITY(10) | `scsi_capacity` |
| `0x28` READ(10) | `scsi_read10` to `umsd_read` |
| `0x2A` WRITE(10) | `scsi_write10` to `umsd_write` |

`ufat_*` is a FAT16/FAT32 driver for the stick, "ported from the proven Noovex9 32-bit implementation":

- `ufat_mount` uses the partition in the first MBR slot (types `0x01/04/06/0B/0C/0E`), or LBA 0 for a superfloppy. It requires 512 B/sector and loads up to 32 root-directory entries into `ufs[]`.
- `ufat_read`, `ufat_write_file` and `ufat_delete_file` cover reading and writing; write-file replaces any existing chain.
- The FAT is cached one sector at a time in `ufatcache`, and `ufat_flush` writes the cached sector to every FAT copy.
- Only the root directory is supported, with 8.3 names.

This driver is separate from `fs/fat/fat_ata.c` (`fat_mount`, `fat_listdir`, `fat_read_file`), a read-only FAT reader that works only through IDE PIO.

---

### 8.10 Shell commands (`shell/commands.c`)

| Command | Line | What it does |
|---|---|---|
| `disks`, `disk`, `lsblk` | 1498 | Runs `gdisk_ensure()`. Prints the NVMe controller's bus and slot with `init: OK` or `FAILED stage N`, the AHCI controller, and `Active disk: <gdisk_name> N MB (install target)`. Then lists every class-`0x01` entry in `pci_scan`. For AHCI controllers (`0x06`, prog-if `0x01`), it runs `ahci_identify` on each implemented port and prints the model and size. Some printed strings are stale, e.g. "NVMe present - read/identify not implemented yet" and "detection only - installing to disk is not enabled yet". |
| `mount`, `fatinfo` | 337 | `fat_mount()` over IDE only. Prints `FAT16/32 mounted drive= sec/clus= data@LBA`, or "no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)". |
| `fatls` | 338 | lists the root directory of the IDE FAT volume |
| `fatcat <NAME.EXT>` | 492 | Reads a file from the IDE FAT volume and prints up to 8000 characters. |
| `bcstat`, `bufcache` | 532 | buffer-cache hits, misses and hit rate |
| `usbdisk`, `usbls` | 719 | `msd_autoinit` + `ufat_mount`. Prints the size, FAT type and file list, or "no USB stick found (enum failed at: ...)". |
| `usbcat <FILE>` | 730 | `ufat_read`. The name match is case-insensitive. |
| `usbwrite <FILE> <text>` | 736 | `ufat_write_file`. Prints "written to USB" or "write failed". |
| `usbrm <FILE>` | 967 | `ufat_delete_file` |
| `usb`, `xhci` | 972 | rescans the controllers (`xhci_mouse_autoinit`), then runs `usbinfo` |
| `ntfs mount [ro] \| umount \| info \| ls \| cat \| write \| mv \| mkdir \| rm` | 1591 | NTFS on the gdisk disk through `nk_mount`/`nvxntfs_*`. Every subcommand except `ls`, `cat`, `info` and `help` needs root. Errors include "ntfs: no NTFS volume found on the disk" and "ntfs: not mounted - run 'ntfs mount' first". |
| `install` | 361 | `ip_open(ST_DESKTOP)` (root only) |
| `kcheck` / `memmap` / `memtest` | 1537-1541 | Installer diagnostics: snapshot integrity, the firmware memory map against kernel areas (plus the `g_pci_quiesced` count), and a fill/verify/re-verify test of free RAM. |
| `pci` | 696 | Prints `vendor:device class=`, flagging network devices. Covers only the `pci_scan` set (buses 0-3, function 0). |

### 8.11 Partition scanner (`fs/vfs/parts.c`)

#### Disks (`bd_scan`)

`bd_scan` builds `g_bd[16]`, a list of every disk the kernel can read, each with a 512-byte-sector reader (`bd_read`):

| Kind | Source | Reader |
|---|---|---|
| main | the disk `gdisk` chose (IDE, AHCI, NVMe or virtio); 4 KiB-sector NVMe keeps its ratio | `gdisk_read` |
| IDE | the other three of primary/secondary master/slave, found with IDENTIFY; ATAPI is skipped | own LBA48 PIO reader (`pio_cmd`) |
| SATA | every other AHCI port with a SATA signature; `ahci_init` is run if the main disk is not AHCI | `ahci_port_io`: stops the current port, points the chosen port at the shared command list, issues one LBA48 command, then restores the main port |
| USB | the first mass-storage device (`msd_autoinit`) | `usb_msd_read512` |

#### Partitions (`part_scan_disk`)

| Layout | What is read |
|---|---|
| GPT | protective or hybrid MBR, or no MBR; the primary header at LBA 1, the backup at the last LBA if the primary is bad; up to 1024 entries of 128 to 4096 bytes; gaps are allowed (scanning stops after 128 empty entries in a row past entry 128); the type GUID is named (27 known types) and the UTF-16 name is kept |
| MBR | the four primaries, then the extended partition's chain of EBRs (up to 128 logical partitions, numbered from 5, with loop protection) |
| none | a file system at sector 0 (a "superfloppy" USB stick or a raw volume) |

Each partition is probed for its file system: NTFS, exFAT, FAT12/16/32, BitLocker, LUKS, XFS, NVX4, ext2/3/4 (from the superblock feature bits), HFS+, Linux swap, ISO9660, btrfs and APFS.

#### Users

| Caller | Use |
|---|---|
| `parts`, `lsblk`, `partitions` | list every disk and partition (also written to the log as `parts:` lines) |
| `disks` | controller details, then the same list |
| `nk_scan` (`fs/ntfs/ntfsk.c`) | the root file system search walks `parts_scan_main` instead of its own four-entry MBR and 128-entry GPT loops |

Limits: reads only (writing is still through the main disk); LBAs above 2^32 on the main and USB disks are not reachable through their 32-bit readers; one NVMe namespace.

## Part 9: Networking and Wi-Fi

NoovexOS 64 has a complete, polled, single-threaded network path: PCI NIC drivers,
an Ethernet/ARP/IPv4/ICMP/UDP/TCP stack, a DHCP client, a DNS resolver with a cache,
an HTTP/1.0 client, a TLS 1.3 client that verifies server certificates, BSD
sockets for Linux programs, and an experimental Wi-Fi driver for the Realtek
RTL8821CE with a WPA2-PSK 4-way handshake and CCMP data path. No part of it uses
interrupts: every driver exposes "send" and "poll receive", and the stack spins
on the PIT counter while it waits. A program blocked on a socket does not spin:
it sleeps for one scheduler tick and retries (section 9.7.2).

All paths below are relative to the repository root.

### 9.1 Where the code lives

| File | Role | Compiled as |
|---|---|---|
| `include/kapi.h`, `sys/native/kapi.c` | Kernel API exported to separately compiled modules | header + unit |
| `include/drivers.h` | `struct nvx_nic`, probe prototypes (also `nvx_snd`, virtio-blk) | header |
| `drivers/net/ethernet/nic_names.c` | `nic_name()`, `nic_index()` (PCI ID to name) | unit |
| `drivers/net/ethernet/nic.c` | PCnet DMA memory, globals (`my_ip`, `gw_ip`, ...), byte helpers, `netlog` | unit |
| `drivers/storage/ahci.c` | also holds PCnet CSR access, `nic_detect`, `nic_up_raw`, `nic_send`, `nic_recv`, `arp_request`, `net_poll` | unit |
| `drivers/net/ethernet/e1000.c`, `rtl8139.c`, `rtl8169.c` | Wired NIC modules | separate `.o` |
| `drivers/virtio/virtio.c`, `virtio.h`, `virtio_net.c` | Legacy virtio-PCI transport + virtio-net | separate `.o` |
| `net/stack/tcpip.c` | netdev indirection, ARP resolve, ping, UDP, DHCP, DNS, TCP | unit |
| `net/stack/sockets.c` | 8-slot TCP socket table (native `int 0x80` sockets) | unit |
| `sys/linux/lxsock.c` | 32-slot TCP/UDP socket layer behind the Linux socket system calls (device 14) | unit |
| `net/http/http.c` | HTTP client, cookie jar, entropy, streaming TCP for TLS, HTTPS, OAuth helpers | unit |
| `lib/crypto/nvxcrypto.c`, `nvxtls.c`, `nvxcrypto2.c`, `nvxwpa.c` | Crypto primitives, TLS 1.3, WPA2 crypto | units |
| `lib/crypto/nvxx509.c` | Big-number Montgomery arithmetic, RSA (PKCS#1 v1.5 and PSS), ECDSA P-256/P-384, DER/X.509 parser, chain and host-name checks | unit |
| `lib/crypto/nvxroots.c`, `lib/crypto/roots/mozilla-cacert.pem` | 120 trusted root CAs (subject + public key), generated by `tools/mkroots.py` from the Mozilla bundle | unit |
| `drivers/net/wifi/rtw_efuse.c`, `rtw8821ce.c`, `rtw_sec.c`, `rtw_tables.h` | RTL8821CE driver | unit |
| `net/wifi/wpa2.c`, `net/wifi/wifi_netdev.c` | EAPOL TX/RX, PTK derivation, CCMP netdev | unit |
| `shell/commands.c` | `net`, `dhcp`, `ping`, `dns`, `http`, `netlog`, `wifi ...` | unit |

Each of these files is a unit with a header under `include/nvx/` (for example `include/nvx/net/stack/tcpip.h`, `include/nvx/lib/crypto/nvxtls.h`). `nvxtls.c` calls into the X.509 code (`x509_verify_chain`, `x509_leaf_cv`, `x509_reason`) and into `http.c` (`tls_prepare`, `tls_clock_now`) through those headers.

### 9.2 The driver module API (`kapi.h`, `drivers.h`)

#### 9.2.1 Separate compilation

`build.sh` step `[3m]` compiles every file in `MODS` on its own, with the same
`CFLAGS` as the kernel (`-m64 -Os -ffreestanding -mno-red-zone -mno-sse ... -I.`):

```sh
MODS="drivers/net/e1000.c drivers/net/rtl8139.c drivers/net/rtl8169.c drivers/virtio/virtio.c drivers/virtio/virtio_net.c drivers/virtio/virtio_blk.c drivers/sound/ac97.c drivers/sound/hda.c"
for m in $MODS; do o=$B/mod_$(basename $m .c).o; gcc $CFLAGS -c $m -o $o || { echo "MODULE BUILD FAILED: $m"; exit 1; }; MODOBJS="$MODOBJS $o"; done
```

`$MODOBJS` is passed to the final `ld` with `kernel64.o` (line 122). Each module
sees only `include/kapi.h` and `include/drivers.h`. It cannot touch kernel
`static` symbols. The only link between the two sides is the non-static `kapi_*`
functions, which the kernel defines, and the `*_probe` functions, which the
modules define. PCnet, the TCP/IP stack and the Wi-Fi driver are not modules.
They are compiled into `kernel64.o`.

#### 9.2.2 `kapi_*` reference (`sys/native/kapi.c`)

| Function | Implementation |
|---|---|
| `kapi_pci_rd32/wr32(bus,dev,fn,off)` | `pci_cfg` / `pci_cfgw` |
| `kapi_pci_find(vendor, ids[], nth, &b,&d,&f)` | Full scan of buses 0..255, devices 0..31; reads the header-type multifunction bit (`0x0C` bit 23) to decide whether to scan 8 functions. Matches the vendor and a 0-terminated device-ID list, and returns the `nth` match |
| `kapi_pci_find_class(cls,sub,nth,...)` | Same scan, matching class/subclass from config `0x08` |
| `kapi_pci_enable` | Command register `|= 0x407` (I/O, MEM, bus master, INTx disable) |
| `kapi_inb/inw/inl/outb/outw/outl` | Port I/O wrappers |
| `kapi_dma_pages(n)` | `dma_alloc_pages(n)` |
| `kapi_phys(p)` | `(u64)p`, because the kernel heap is identity-mapped |
| `kapi_delay_us`, `kapi_ns` | `hr_delay_us`, `hr_ns` (high-resolution timer) |
| `kapi_log` | `klog` |
| `kapi_mb` | `cpu_wmb()` |

#### 9.2.3 `struct nvx_nic`

```c
struct nvx_nic {
    const char* name;
    u8 mac[6];
    int (*send)(const u8* data,int len);
    int (*recv)(u8* out,int max);   /* 0 = nothing pending */
    int (*link)(void);
};
int e1000_probe(struct nvx_nic*); int rtl8139_probe(struct nvx_nic*);
int rtl8169_probe(struct nvx_nic*); int virtio_net_probe(struct nvx_nic*);
```

A probe returns 1 after it has filled `out`. The kernel keeps a single instance,
`g_mnic` (`drivers/net/ethernet/nic.c`).

### 9.3 NIC bring-up and probe order

`nic_up_raw()` (`drivers/storage/ahci.c`) runs once, guarded by `nic_init`:

```
nic_detect()                       PCnet 1022:2000 on buses 0..3, slot fn0 only
  + found -> nic_kind=0, program PCnet directly
  + not found -> e1000_probe || rtl8169_probe || rtl8139_probe || virtio_net_probe
                 -> nic_kind=2, my_mac = g_mnic.mac, net_ok=1
  + none -> netlog("no supported NIC (PCnet, e1000, RTL8139, RTL8111/8168, VirtIO)")
```

PCnet is tried first because the driver's comment calls it "prefer PCnet - proven".
`nic_send`/`nic_recv` dispatch on `nic_kind`: kind 2 forwards to `g_mnic.send/recv`,
and kind 0 uses the in-kernel PCnet rings. `nic_up()` (`net/stack/tcpip.c`) wraps
`nic_up_raw()` and runs `dhcp_config()` once (`g_dhcp_done`).

Helper routines in `ahci.c`:
- `nic_flush_rx()` reads at most 160 frames, stopping after 3 empty polls, to
  drain stale traffic before DHCP and TCP connects.
- `nic_kick()` applies only to PCnet. It rewrites CSR0, which clears the latched
  error bits and keeps RX running (`(c&0x7F00)|0x0002`).
- `net_poll(rx,max,ticks)` busy-polls the active link (`g_rx` when a netdev is
  set, otherwise `nic_recv`), measuring time with `pit_read()`. One tick is 1193
  PIT counts, about 1 ms. ARP requests for our address are answered inside the
  loop (`arp_auto_reply`) and never returned to the caller.

`nic_names.c` provides `nic_name(ven,dev)`, which returns the display names
"Intel e1000", "Realtek RTL8168", "Realtek RTL8139", "VirtIO-net", "AMD PCnet" or
"unknown NIC". It also provides `nic_index()`, the first PCI entry with class 2.
The `net` command uses both.

### 9.4 Wired NIC drivers

#### 9.4.1 AMD PCnet (Am79C970/973), in-kernel

The comment at the top of `nic.c` says this is a "faithful port of the proven
Noovex9 (32-bit) stack". DMA memory is static `.bss` that the code assumes is
below 4 GiB (`A32()` truncates pointers to 32 bits):

| Buffer | Size |
|---|---|
| `nic_initb` | 32-byte init block, 16-aligned |
| `nic_rxd` / `nic_rxb` | `RXR`=8 descriptors x 16 B / 8 x 2048 B |
| `nic_txd` / `nic_txb` | 2 descriptors / one shared 2048 B buffer |

The init sequence in `nic_up_raw` runs as follows:
1. Set I/O, MEM and bus master in PCI CMD, then read the MAC from `io+0..5`.
2. Reset (`inl io+0x18; inw io+0x14`), switch to DWIO mode (`outl io+0x10,0`), and stop (`CSR0=4`).
3. `BCR20=2` (SWSTYLE 2, 32-bit structures) and `BCR2 |= 2` (auto-select).
4. Build the init block: mode 0, `RLEN=3<<4` (8 RX), `TLEN=1<<4` (2 TX), the MAC, and the ring addresses.
5. Give every RX descriptor the OWN bit and BCNT -2048 (`0x8000F800`).
6. Set CSR1/CSR2 to the init block address, `CSR0=INIT`, poll for IDON (`0x0100`, 2 M spins), then `CSR0=STRT`.

`nic_send` pads the frame to 60 bytes, writes `-len` with STP|ENP|OWN, kicks with
TDMD (`CSR0=0x000A`) and spins until OWN clears.

#### 9.4.2 Intel e1000 (`drivers/net/ethernet/e1000.c`)

- IDs: 8086: 100E 100F 1004 1010 1015 1017 1019 101A 1026-1028 1013 1014 1076-107B 10D3.
- BAR: BAR0 must be MMIO and below 4 GiB, because it is used through the identity map.
- Reset: writes `CTRL.RST` (bit 26), masks interrupts (`IMC 0xD8`), then sets SLU|ASDE (`|0x60`) and clears the bits in mask `0x88080000`.
- MAC: comes from `RAL/RAH` (0x5400/0x5404). If RAH.AV is clear or the value is zero, it is read through EERD (`0x14`, 3 words). The result is written back with AV set. MTA (0x5200) is cleared.
- Rings: 32 RX and 16 TX legacy descriptors of 16 B, with 2048 B buffers. Registers RDBAL/H/LEN/HEAD/TAIL are `0x2800..0x2818` and TDBA... are `0x3800..0x3818`.
- RCTL `0x0100` = EN|UPE|MPE|BAM|SECRC (`0x02|0x08|0x10|0x8000|0x04000000`). TIPG = `0x0060200A`. TCTL = EN|PSP|CT=0x0F|COLD=0x40.
- TX: waits up to 100 ms for DD in the slot, sets `cmd=0x0B` (EOP|IFCS|RS) and writes TDT. RX: checks DD+EOP with no errors, re-arms the slot and writes RDT. Link: `STATUS & 2` (LU).

#### 9.4.3 Realtek RTL8139 (`rtl8139.c`)

- Access: port I/O (BAR0 bit 0 must be set). Powers on with `CONFIG1=0` and resets with `CR=0x10`.
- RX buffer: 4 pages (`RX_LEN 8192` wrap window), programmed into `RBSTART`. RCR=`0xE78F` (all/phys/multi/broadcast, WRAP, max DMA). TCR=`0x03000600`.
- TX: 4 round-robin buffers (`TSAD0..3`/`TSD0..3`), maximum 1792 B.
- RX parse: 4-byte header {status, len}. A bad header resets RX (`CR=0x04` then `0x0C`). `CAPR = rxoff-16` after each frame, and the offset is dword-aligned.
- Link: `!(MSR & 0x04)`.

#### 9.4.4 Realtek RTL8111/8168/8169 (`rtl8169.c`)

- IDs: 10EC: 8161 8168 8169 8136 8167 2600 2502, accessed through port I/O.
- Descriptors: `{opts1, opts2, addr}` with OWN/EOR/FS/LS bits. 32 RX and 16 TX, `BUFSZ 2048`.
- Init: unlocks config (`9346CR=0xC0`), RCR=`0xE70F`, TCR=`0x03000700`, `RMS=2047`, `MTPS=0x3B`, TNPDS at `0x20`, RDSAR at `0xE4`, then `CMD=RE|TE`.
- TX: sets OWN|FS|LS (plus EOR on the last slot) and kicks with `TPOLL=0x40`. RX: drops frames that are not FS+LS or have the RES bit (21) set, then strips the 4-byte CRC.
- Link: `PHYS & 0x02`.

#### 9.4.5 VirtIO (legacy PCI transport)

`drivers/virtio/virtio.c` implements the legacy (0.9.5) I/O-port interface:

| Offset | Register | Used by |
|---|---|---|
| 0x00 | device features | `vio_begin` returns it |
| 0x04 | driver features | `vio_features` |
| 0x08 | queue PFN (`phys>>12`) | `vq_setup` |
| 0x0C | queue size | `vq_setup` (must be a power of 2, at most 32768) |
| 0x0E | queue select | `vq_setup` |
| 0x10 | queue notify | `vq_kick` |
| 0x12 | device status (ACK=1, DRIVER=2, DRIVER_OK=4, FAILED=0x80) | `vio_begin/ready/fail` |
| 0x14+ | device config (net: MAC, then status at +6) | `virtio_net.c` |

`vq_setup` lays out the descriptor table (16*n), the avail ring (6+2n) and a
page-aligned used ring in one `kapi_dma_pages` block. It rejects physical
addresses at or above 2^44, the limit of the 32-bit PFN register. `vq_add` chains
descriptors from a free list (`VIRTQ_F_NEXT=1`, `VIRTQ_F_WRITE=2`). `vq_used`
consumes one used element and returns the whole chain to the free list.

`virtio_net.c` (device 1AF4:1000) negotiates only `VIRTIO_NET_F_MAC` (bit 5) and
`F_STATUS` (bit 16). It uses queue 0 for RX (up to 64 posted 2048 B buffers,
tracked by `rxid[]`) and queue 1 for TX (up to 32 in flight). Every frame carries
a 10-byte zeroed `virtio_net_hdr` (`HDR 10`). Without F_MAC the driver makes up a
MAC that starts `52:54:`. `vn_link` reads status bit 0, or returns 1 when the
device has no status field.

### 9.5 The netdev indirection (Ethernet vs Wi-Fi)

`net/stack/tcpip.c` routes all I/O through function pointers so that the same stack can
run over a Wi-Fi link:

```c
void (*g_tx)(const u8*,int); int (*g_rx)(u8*,int);
const u8* (*g_mac)(void);    int (*g_linkok)(void);  int (*g_bcastf)(void);
static void nd_tx(...){ if(g_tx)g_tx(d,n); else nic_send(d,n); }
void nd_tx2(...){ if(g_tx)g_tx(d,n); else nic_send(d,n); }
static int nd_rx(...){ return g_rx?g_rx(o,m):nic_recv(o,m); }
static const u8* nd_mymac(void){ return g_mac?g_mac():my_mac; }
static int nd_up(void){ return g_linkok?g_linkok():net_ok; }
```

`netdev_set()` (`net/wifi/wifi_netdev.c`) installs either the Ethernet set
(`eth_tx_ptr` to `nic_send`, `eth_rx_ptr` to `nic_recv`, `my_mac`, `net_ok`) or the
Wi-Fi set (`wnd_tx_ptr`, `wnd_rx_eth`, `g_wnd_mac`, `g_wnd_up`, `wnd_bcast_flag`).
`nd_bcast_flag()` decides the DHCP BOOTP broadcast flag. On Ethernet it is
`0x8000`. On Wi-Fi it is `0x8000` only when a GTK is installed, and otherwise 0 so
the server unicasts the reply.

Before any `netdev_set` call, `nd_tx`/`nd_rx` fall back to the wired card, so
the stack works on Ethernet without running `wifi eth` first.

`netdev_set()` also sets `g_netdev_wifi`. The whole stack now follows the active
link:

| Path | Receives through | Link test |
|---|---|---|
| `tcp_get`, `do_ping`, `dns_try`, `dhcp_wait`, `arp_resolve` | `nd_poll` to `g_rx` | `nd_up()` |
| `sock_*` (`sockets.c`), `tcps_rd` (HTTPS), `lxs_pump` (Linux sockets) | `net_poll` / `nd_rx` to `g_rx` | `nd_up()` |
| `udp_send`, `tcp_send`, `dhcp_config` | - | `nd_up()` (was the wired-only `net_ok` before v8.95) |
| `nic_up()` | - | Wi-Fi: `g_linkok()`; wired: `nic_up_raw()` plus DHCP once |

Note: Before v8.95, sockets and HTTPS read only from the wired card and the senders checked the wired-only `net_ok` flag, so they could not work over Wi-Fi. The Wi-Fi path has been checked in code review; the QEMU tests cover only wired cards.

### 9.6 TCP/IP stack (`net/stack/tcpip.c`)

#### 9.6.1 Addressing and defaults

`drivers/net/ethernet/nic.c` defaults to the QEMU/VirtualBox NAT layout: `my_ip=10.0.2.15`,
`gw_ip=10.0.2.2`, `dns_ip=10.0.2.3`, `my_mask=255.255.255.0`. DHCP overwrites them.
If DHCP fails, the previous values are restored ("DHCP failed - static ip=").
Routing is a single rule, used by every sender:
`route = ((dst & my_mask) == (my_ip & my_mask)) ? dst : gw_ip`.

Byte-order helpers are `be16/be32/rd16/rd32`. `cksum()` is the RFC 1071 one's
complement sum. `net_rand()` is xorshift32 seeded from the TSC. Received IP and
UDP checksums are not verified. Fragmented IPv4 packets are dropped by the Linux
socket layer (`lxs_frame` checks the MF bit and fragment offset).

#### 9.6.2 Frames built by the stack

| Layer | Fields written (offsets in bytes) |
|---|---|
| Ethernet (`eth_hdr`) | dst[6], src[6] = `nd_mymac()`, type (0x0800 IPv4, 0x0806 ARP) to 14 B |
| ARP request (`arp_request`) | htype 1, ptype 0x0800, hlen 6, plen 4, op 1, SHA, SPA=`my_ip`, THA=0, TPA to 28 B, padded to 60 |
| IPv4 | `0x45`, TOS 0, total len, ID (ping `0x1234`, UDP `0x4321`, TCP `ipid++` from 0x1000), DF/frag 0, TTL 64, proto (1/17/6), header checksum |
| ICMP echo (`do_ping`) | type 8, code 0, id `0xABCD`, seq 1, no payload (IP total 28) |
| UDP (`udp_send`) | sport, dport, len, checksum over pseudo-header (`udp_cksum`; a result of 0 is sent as 0xFFFF). Payload at most 1458 |
| TCP (`tcp_send`) | data offset 5, or 6 on a SYN with the MSS option (2, 4, 1400); flags; window from `g_tcp_wnd` (8192 for kernel users, the free receive space for Linux sockets); checksum over pseudo-header (`tcp_cksum`). Payload at most 1460 |

#### 9.6.3 ARP

There is no general ARP table. `arp_resolve(ip, mac)` caches only the gateway
(`gw_mac`, `gw_known`). It answers from that cache when `ip==gw_ip`. Otherwise it
broadcasts a request and polls up to 20 slices for an ARP reply (`op 2`) whose
sender IP matches. `dhcp_config` resets `gw_known=0` when it gets a new lease.

Since v8.95 the stack answers ARP requests for its own address.
`arp_auto_reply(frame, len)` (`net/stack/tcpip.c`) checks for an ARP request
(op 1, IPv4) whose target is `my_ip`, sends the reply with `nd_tx2`, and returns 1
so the caller drops the frame. `net_poll`, `nd_poll` and the Linux socket pump
all call it.

Important: Without the responder, a gateway whose ARP entry for us had gone stale probed us, got no answer and dropped our SYN-ACKs. In the QEMU tests every second HTTPS connection failed with "no SYN-ACK" for this reason.

#### 9.6.4 Timeouts

Every wait uses `nd_poll(...,1)` or `net_poll(...,1)`. Each call returns as soon as
a frame arrives, or after about 1 ms of PIT time. The budgets below are therefore
counted in "poll slices", which means about N ms when the link is quiet.

| Operation | Function | Budget |
|---|---|---|
| ARP reply | `arp_resolve` | 20 slices |
| ICMP echo reply | `do_ping` | 150 slices; RTT stored in `ping_ms` |
| DHCP OFFER / ACK | `dhcp_wait` | 900 slices each, 2 full attempts |
| DNS answer | `dns_try` | 1200 slices per server |
| TCP SYN-ACK | `tcp_get`, `sock_connect`, `tcps_open` | 4 SYNs x 300 slices |
| HTTP response idle | `tcp_get` | 1800 idle slices; request resent every 450 idle slices (max 3) while nothing has arrived |
| Socket recv idle | `sock_recv` | 900 idle slices |
| TLS stream recv idle | `tcps_rd` | 2500 idle slices |
| Linux socket connect | `lxs_timers` | SYN resent every 100 scheduler ticks (1 s), 5 retries to `ETIMEDOUT` |
| Linux socket data | `lxs_timers` | retransmit after 30 ticks, doubling up to 480; 10 retries to `ETIMEDOUT` |

#### 9.6.5 DHCP client

`dhcp_build` writes a 300-byte BOOTP message: op 1, htype 1, hlen 6, `xid`, flags
from `nd_bcast_flag()`, chaddr, magic `0x63825363`, followed by options
53 (type), 50 (requested IP), 54 (server ID), 55 (parameter list 1, 3, 6, 15)
and 255. `dhcp_parse` checks op 2, `xid`, chaddr and the magic cookie, then reads
options 53, 54, 1, 3 and 6. `dhcp_wait` accepts only UDP 67 to 68 messages of the
wanted type.

```
        +------------- attempt (x2, new xid) -------------+
 INIT --> my_ip=0, flush RX, DISCOVER -> 255.255.255.255:67
          | wait OFFER(2) 900   --timeout--> next attempt
          v
        REQUEST(3) {50=yiaddr, 54=server}
          | wait ACK(5) 900     --timeout--> next attempt
          v
        BOUND: my_ip, mask (default /24), gw, dns; gw_known=0; g_dhcp_ok=1
 both attempts fail -> restore saved static config, g_dhcp_ok=0
```

`dhcp_renew()` (the `dhcp` command) calls `dhcp_config()` again. There is no lease
timer and no RELEASE message.

#### 9.6.6 DNS resolver

`dns_build_query(host, txid, q, cap)` writes a 12-byte header (RD=`0x0100`,
QDCOUNT=1) and the QNAME, with labels clamped to 63 bytes and the host to 253.
QTYPE is A (1) and QCLASS is IN (1).

`dns_try(host, server, &ip)` does the following:
- picks a random txid and a source port in `0xC000|rand&0x3FFF`, then sends to port 53;
- accepts only replies from that server to that port, with QR set, a matching txid,
  QDCOUNT=1 and a question section byte-identical to the query;
- skips names with `dns_skipname` (which handles compression pointers) and returns
  the first answer with `type==1 && rdlen==4`.

`dns_query()` checks an 8-entry round-robin cache first (`dnsc[]`, host at most 127
chars, no TTL). On a miss it tries the DHCP server, then 8.8.8.8, 1.1.1.1 and
10.0.2.3, skipping zero entries and any later server equal to the first. A
successful answer is stored with `dnsc_put`.

#### 9.6.7 TCP

TCP is a client-only, polled state machine. There are four copies of it:
`tcp_get` (one-shot HTTP), `sock_*` (native persistent sockets), `tcps_*` (the
TLS transport) and `lxs_*` (Linux sockets, section 9.7.2). The first three handle one
connection at a time. The Linux socket layer handles up to 32 sockets at once
and is the only one with retransmission and flow control.

```
CLOSED -SYN(snd=rand)-> SYN_SENT -(SYN|ACK, ack==snd+1)-> ESTABLISHED (rcv=seq+1, send ACK)
   ^                       | RST -> error (-4)    4x300 slices -> error
   |                       v
   |   ESTABLISHED: data with seq==rcv appended, rcv+=len, ACK each segment
   |                out-of-order data: dropped, duplicate ACK sent
   |   peer FIN -> rcv+=1, send FIN|ACK, snd+=1 -> CLOSED
   +-- local close (sock_close): FIN|ACK, slot freed (no wait for final ACK)
```

The implementation has these properties:
- Ports: ephemeral ports come from `next_ephport()`, in the range 20000..59999.
- Sending: `sock_send` and `tcps_wr` split writes into 1400-byte segments and
  send them all back to back. There is no congestion control, the peer window is
  not checked, and sent data is never retransmitted, apart from the HTTP-request
  resend in `tcp_get`.
- Receiving: only in-order data is kept (`seq==rcv`). Every SYN carries an MSS
  option of 1400; the advertised window is 8192 for these three users. There is
  no TIME_WAIT.
- Partial reads: `tcps_rd` acknowledges only the bytes it copied. Before
  v8.95 it acknowledged the whole segment even when the caller's buffer was
  smaller, which silently lost data.
- Return codes from `tcp_get`: -1 no NIC, -2 ARP failure, -3 no SYN-ACK,
  -4 RST. A non-negative value is the number of bytes received. The `http`
  shell command maps these codes to messages.

### 9.7 Sockets (`net/stack/sockets.c`)

`g_sock[NSOCK=8]` holds `nsock_t {used, connected, dip, sp, dport, dmac[6], snd, rcv}`.

| Native syscall (`sys/native/syscall.c`) | Kernel function | Semantics |
|---|---|---|
| 100 `socket()` | `sock_alloc` | First free slot, zeroed; returns sid or -1 |
| 101 `connect(sid, ip, port)` | `sock_connect(rbx,rcx,rdx)` | Numeric IPv4 only (DNS is the caller's job). ARP, flush, 3-way handshake. 0 on success; -2 NIC, -3 ARP, -4 RST, -5 timeout |
| 102 `send(sid,buf,len)` | `sock_send` | 1400-byte PSH|ACK segments, returns len |
| 103 `recv(sid,buf,max)` | `sock_recv` | Returns as soon as some in-order data has arrived, or 0 after 900 idle slices or FIN |
| 104 `close(sid)` | `sock_close` | Sends FIN|ACK if connected, frees the slot |

These numbers belong to the native NoovexOS syscall ABI (`syscall_dispatch`, with
arguments in rbx/rcx/rdx). Linux programs use the separate socket layer below.

#### 9.7.1 Linux socket system calls (`sys/linux/linux.c`)

Since v8.95, Linux programs (musl or any static Linux binary) can use the network
through ordinary BSD sockets. A socket is a file descriptor whose `vfile.dev` is
14 and whose `fs_idx` is the slot in `g_lxs[]`. `read`, `write`, `readv`,
`writev`, `close`, `dup`, `fork`, `poll`, `select`, `fstat` (`S_IFSOCK`),
`fcntl(O_NONBLOCK)` and `ioctl(FIONREAD/FIONBIO)` all work on it.

| Nr | Call | Behaviour |
|---|---|---|
| 41 | `socket` | `AF_INET` only; `SOCK_STREAM` (TCP) or `SOCK_DGRAM` (UDP); honours `SOCK_NONBLOCK` and `SOCK_CLOEXEC`. Other families to `EAFNOSUPPORT` (97), so musl falls back from IPv6 to IPv4 |
| 42 | `connect` | TCP: sends a SYN and blocks until established, or returns `EINPROGRESS` when non-blocking. UDP: sets the default peer; `AF_UNSPEC` disconnects |
| 43, 288 | `accept`, `accept4` | Returns the next connection from the listen queue as a new descriptor and fills in the peer address; blocks, or `EAGAIN` when non-blocking. `accept4` honours `SOCK_NONBLOCK` and `SOCK_CLOEXEC` (section 9.7.4) |
| 44, 46 | `sendto`, `sendmsg` | TCP: queue into the 32 KiB send buffer. UDP: one datagram, at most 1472 bytes (`EMSGSIZE` otherwise) |
| 45, 47 | `recvfrom`, `recvmsg` | TCP: in-order bytes; 0 at end of stream. UDP: one datagram, truncated to the buffer; source address returned. `MSG_PEEK`, `MSG_DONTWAIT` supported |
| 48 | `shutdown` | `SHUT_WR` sends FIN once all data is acknowledged |
| 49 | `bind` | Chooses the local port (0 = ephemeral); `EADDRINUSE` on a clash |
| 50 | `listen` | Turns a TCP socket into a listening socket (an unbound one gets an ephemeral port); backlog 1 to 32 |
| 51, 52 | `getsockname`, `getpeername` | `sockaddr_in` |
| 53 | `socketpair` | `EOPNOTSUPP` |
| 54, 55 | `setsockopt`, `getsockopt` | set: accepted and ignored. get: `SO_ERROR` (clears it), `SO_TYPE`, `SO_SNDBUF`, `SO_RCVBUF`, `SO_DOMAIN`, `SO_PROTOCOL` |

Errors use Linux numbers: `ECONNREFUSED` 111, `ECONNRESET` 104, `ETIMEDOUT` 110,
`ENOTCONN` 107, `EISCONN` 106, `EALREADY` 114, `EINPROGRESS` 115,
`EHOSTUNREACH` 113 (ARP failed), `ENETUNREACH` 101 (no link), `EPIPE` 32 (with
`SIGPIPE` unless `MSG_NOSIGNAL`).

DNS for musl. musl's resolver reads `/etc/resolv.conf` and, when there is
none, asks `127.0.0.1:53`. The socket layer redirects UDP sent to `127.0.0.1:53`
to the real DNS server (`dns_ip`, from DHCP) and rewrites the replies' source
back to `127.0.0.1`, because musl only accepts answers from the address it
asked. So `getaddrinfo()` works without any configuration.

Loopback (v8.97). TCP and UDP to `127.0.0.0/8` or to the machine's own address
stay inside the machine (section 9.7.3), so a server and a client on the same
NoovexOS system can talk to each other, even with no network card.

#### 9.7.2 How the socket layer works (`sys/linux/lxsock.c`)

```
struct lxsock { type (1 TCP, 2 UDP), st (NEW, SYN, EST, DEAD, LISTEN, SYNRCVD), err,
                rip/rport/lport, rmac, iss/una/nxt/rcv, pwin,
                rx[64 KiB] + rxn, tx[32 KiB] + txn, tlast, tries,
                rd_shut, wr_shut, fin_in, fin_out, lo53,
                parent, backlog, qn, q[LXS_QMAX] }        /* listen queue, v8.97 */
```

There are `NLXS` = 128 socket slots (16 before v8.97).

- No background thread. Every socket call first runs `lxs_pump()`. It drains
  up to 128 frames from the active link, answers ARP, hands IPv4 TCP/UDP to the
  matching socket (`lxs_frame`), then runs the retransmit timers (`lxs_timers`).
- Blocking. When a call cannot finish (`connect` before the SYN-ACK, `read`
  with no data, `write` with a full send buffer), it returns the internal code
  `LXS_BLOCK`. `lx_sock_wait` turns that into `EAGAIN` for non-blocking sockets,
  `EINTR` if a signal is pending, or `jc_block(r, S_WAIT, g_ticks+1)`: the
  process sleeps one tick (10 ms) and the system call restarts from the top. The
  big kernel lock is free while it sleeps.
- TCP receive. In-order segments go into `rx` and are acknowledged at once.
  The advertised window is the free space in `rx` (up to 64 KiB). Out-of-order
  segments get a duplicate ACK and are dropped; the peer resends them. A window
  update is sent when a full buffer is drained.
- TCP send. `write` copies into `tx` and `lxs_push` sends as much as the
  peer's window allows, in segments of up to 1400 bytes. Bytes leave `tx` only
  when acknowledged. On timeout the whole unacknowledged range is resent
  (go-back-N) with exponential back-off.
- Close. `close` (through `vf_unref` to `lxs_release`) waits up to 1.5 s for
  outstanding data and the FIN to be acknowledged, resending every 300 ms, then
  frees the slot. If the peer has not closed its side by then, the connection is
  reset (RST) so the peer does not wait for ever.
- UDP. Datagrams are stored in `rx` as `[ip 4][port 2][len 2][data]`
  records. A full buffer drops new datagrams, as on Linux.

Performance in QEMU (TCG, e1000, tap): a 300 KB download takes about 0.05 s
after the MSS and window changes, compared with about 6 s with the old fixed
8 KiB window and the default 536-byte MSS.

#### 9.7.3 Loopback

`tcp_send` and `udp_send` (`net/stack/tcpip.c`) build the frame as usual. When the
destination passes `lxs_is_lo` (127.x.x.x, or `my_ip` once it is set) the
frame goes to `lxs_lo_tx` instead of the network card, and the card does not
have to be up. The source address is the destination itself for 127.x.x.x, so
replies come back to the same address.

`lxs_lo_tx` copies the frame into a 256-slot ring (1536 bytes per slot,
allocated on first use). `lxs_pump` drains the ring with `lxs_frame` before and
after it reads the card, and frames produced while draining (ACKs, data) are
processed in the same pass. A connect, accept, transfer and close between two
sockets of the same machine therefore needs no waiting at all; in one process,
a non-blocking `connect` followed by `accept` succeeds immediately.

A SYN to a loopback port nobody listens on is answered with RST, so `connect`
fails with `ECONNREFUSED` as on Linux. UDP to `127.0.0.1:53` is still forwarded
to the real DNS server when one is known.

#### 9.7.4 Listening sockets (`listen`, `accept`)

- `lxs_listen(i, backlog)` sets `st = LXS_LISTEN` and the backlog (1 to
  `LXS_QMAX` = 32). Calling it again only changes the backlog.
- A SYN for a listening port creates a child socket in `LXS_SYNRCVD`: the
  peer's MAC comes from the frame, `iss` from `net_rand`, and a SYN-ACK (with
  MSS 1400 and the receive window) is sent. Connections already queued and
  half-open children both count against the backlog; a SYN beyond it is
  dropped and the client retries.
- Established sockets are matched before listeners, so the frames of an
  accepted connection never reach the listener.
- The ACK of the SYN-ACK moves the child to `LXS_EST` and appends it to the
  listener's queue (`lxs_enqueue`); data that arrives in the same segment is
  kept. If the listener has gone, the child is reset.
- `lxs_timers` resends the SYN-ACK every second and gives up after 5 tries.
- `lxs_accept` pumps, then returns the oldest queued child or `LXS_BLOCK`.
  `lx_accept` wraps the child in a new descriptor (`jc_newvf(14, ...)`), sets
  `O_NONBLOCK`/close-on-exec for `accept4`, and writes the peer address.
- `poll`/`select` report a listening socket readable while its queue is not
  empty.
- Closing a listener resets the half-open children and closes the queued ones.

`featest server` checks this over loopback, and the network check
(section 12.2) has the host connect to `featest listen` on port 9200 through
the tap device, which tests the same path over the network card.

### 9.8 HTTP, entropy and HTTPS (`net/http/http.c`)

- `http_fetch_to(host, path, buf, cap)`: if `parse_ip` fails, the host goes
  through `dns_query`, and a DNS failure returns -100. It builds
  `GET <path> HTTP/1.0`, `Host:`, `Connection: close`, `User-Agent: Noovex64`, and
  a `Cookie:` header from the jar, then calls `tcp_get(ip, 80, ...)` into the
  256 KiB `http_buf`.
- Cookie jar: since v9.08 `ck_parse` and `ck_header` are thin wrappers around
  the shared jar in `net/http/cookies.c` (400 cookies with domain, path, expiry,
  Secure and HttpOnly, saved to `cookies.txt`), so the `http` command, the
  account code and the browser see the same cookies (section 13.10). Before
  v9.08 this was a private 32-entry table.
- Entropy: `entropy_init` hashes the TSC, CMOS bytes 0-9, 6x RDRAND (when
  CPUID.1:ECX[30] is set) and TSC jitter with SHA-256 into `ENT_POOL`.
  `entropy_get` produces `SHA256(pool||ctr||tsc)` blocks and re-hashes the pool
  after each block (`pool = SHA256(pool||0x01)`).
- `https_fetch_to`: accepts `host` or `host:port` (default 443), opens the
  streaming TCP connection (`tcps_open`), takes 64 entropy bytes, places the
  `tls_io` struct at the fixed address `0x00930000`, calls `tls_get()`, then
  zeroes the entropy. It logs `TLS result N: <reason>` to the network log and
  the kernel log.
- `tls_prepare()`: called at the start of every handshake. It loads up to 8
  extra trusted certificates from `/etc/ssl/local-ca.pem` (PEM, any line
  layout) into `g_x509_extra[]`, so you can trust your own CA or a lab server.
- `tls_clock_now()`: the wall clock (`k_realtime_ms()/1000`) when it is after
  1 Jan 2024, otherwise 0, which tells the verifier to skip the date checks
  instead of rejecting everything on a machine whose clock is not set.
- `https_request(host, path, method, xhdr, body, blen, ...)`: a general-purpose
  version that adds `Accept: application/json`, extra headers and a body with
  `Content-Length`. The mynexus.site OAuth helpers in the same file use it
  (`nvx_signin`, `nvx_register`, `nvx_userinfo_from_token`, `nvx_hist_add`).
  Their responses are read with the small `json_str` and `nvx_json_int` scanners.
- `/etc/ssl/nvx-anchors.pem`: written at boot from the same 120 roots the
  kernel verifies against, for TLS in user programs (section 6.11). The kernel
  itself does not read it.
- The kernel's general random generator `csprng_bytes` (ChaCha20,
  `lib/crypto/nvxcrypto2.c`) is separate from `entropy_get`. Since v8.97 it
  checks CPUID before using RDSEED or RDRAND (it used RDSEED unconditionally,
  which faulted on CPUs without it, for example QEMU's default CPU) and mixes in
  TSC jitter. It now also feeds `getrandom`, `/dev/random` and `/dev/urandom`.

### 9.9 TLS 1.3 client and crypto library

#### 9.9.1 Algorithms

| Header | Implements |
|---|---|
| `nvxcrypto.c` | SHA-256, HMAC-SHA256, HKDF extract/expand, `hkdf_label` (TLS 1.3 Expand-Label), ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD (`aead_seal/open`; the MAC input is built in a heap buffer by `aead_mac`, it used a fixed buffer at `0x900000` before v9.08), X25519 (`x25519`, TweetNaCl-style `car25519/pack25519/inv25519`). The header states every function is "verified bit-exact against RFC test vectors" |
| `nvxtls.c` | AES-128 (`aes128_expand/enc`), GHASH, AES-128-GCM (`aes_gcm`), TLS 1.3 key schedule, record layer, handshake, `tls_get` |
| `nvxx509.c` | Montgomery big numbers (`mont_*`, up to 4160-bit), RSA public operation, PKCS#1 v1.5 and PSS verification (`rsa_pkcs1_verify`, `rsa_pss_verify`, `mgf1`), NIST P-256 and P-384 in Jacobian coordinates (`ec_dbl`, `ec_add`, `ecdsa_verify`), DER reader (`der_get`), X.509 parser (`x509_parse`), chain builder (`x509_verify_chain`), host-name matching, `tls13_verify_cv` |
| `nvxcrypto2.c` | SHA-512/384, HMAC-SHA512, AES-256, RDRAND/RDSEED, CSPRNG (`csprng_seed/bytes/u32`) |
| `nvxwpa.c` | SHA-1, HMAC-SHA1, PBKDF2-SHA1, 802.11i PRF (`wpa_prf`), AES-CCM (`aes_ccm_auth_encrypt`, `aes_ccm_decrypt_verify`), AES-128 decrypt, RFC 3394 key unwrap (`aes_key_unwrap`) |

#### 9.9.2 Handshake (`tls_get`)

ClientHello (`build_clienthello`) contains:
- legacy version 0x0303, a 32-byte random, and a 32-byte session ID (compatibility mode);
- cipher suites `0x1301` TLS_AES_128_GCM_SHA256 and `0x1303` TLS_CHACHA20_POLY1305_SHA256;
- these extensions:

| Ext | Value |
|---|---|
| 0x0000 server_name | host |
| 0x000a supported_groups | x25519 (0x001d) |
| 0x000d signature_algorithms | 0x0403, 0x0503 (ECDSA P-256/P-384), 0x0804, 0x0805, 0x0806 (RSA-PSS), 0x0401, 0x0501, 0x0601 (RSA PKCS#1, for certificates only) |
| 0x002b supported_versions | 0x0304 |
| 0x0033 key_share | x25519, 32-byte public key |
| 0x0010 ALPN | `http/1.1` (comment: "needed by some CDNs e.g. Cloudflare") |

The handshake runs in this order:

```
priv = rnd[0..31] (clamped); pub = X25519(priv, 9)
-> ClientHello (plain record 0x16)          transcript = SHA-256
<- ServerHello: cipher in {1301,1303}, key_share x25519  (else -3/-4)
ecdhe = X25519(priv, spub); tls_key_schedule -> c/s hs traffic keys
<- encrypted records: skip CCS (0x14), decrypt with s_hs key, append to a 64 KiB
   reassembly buffer (g_tls_hs), then process complete handshake messages:
     EncryptedExtensions (8)  ignored
     CertificateRequest (13)  remembered: we will answer with an empty Certificate
     Certificate (11)         x509_verify_chain(chain, host, now)       else -10..-13
     CertificateVerify (15)   x509_leaf_cv(leaf, scheme, sig, transcript) else -14
     Finished (20)            HMAC(finished_key(s_hs), transcript) == body else -15
                              certificate + CV must have been seen        else -16
   on any failure: send an encrypted alert (bad_certificate, unknown_ca, ...) and stop
-> CCS, [empty Certificate if requested], client Finished = HMAC(finished_key(c_hs), transcript)
derive c/s ap traffic keys from master
-> application data: HTTP request (records <= 16000 B)
<- decrypt app records until close/alert (0x15) or buffer full (<=512 records)
wipe priv, ecdhe, key schedule, traffic keys
```

Record keys are 16 bytes for AES-GCM and 32 for ChaCha (`klen`). The nonce is the
IV XOR the 64-bit sequence number. The AAD is the 5-byte record header. Inner
content-type padding is removed by `tls_decrypt`.

Security notes:
- Since v8.95 the client authenticates the server: chain to a trusted root, host
  name, validity dates, the CertificateVerify signature and the server Finished
  (details in section 9.9.3). Before v8.95 none of these were checked, so an active
  man-in-the-middle could read and change HTTPS traffic.
- `https -k` (`g_tls_insecure=1`) skips the certificate and CertificateVerify
  checks for one request and prints a warning. The server Finished is still
  checked.
- Not implemented: revocation (OCSP/CRL), certificate transparency, name
  constraints, path-length limits, key update, session resumption, HelloRetryRequest,
  P-521 and Ed25519 keys.
- Scratch buffers sit at fixed physical addresses: `0x00908000` (inner),
  `0x00910000` (record), `0x00918000` (rb), `0x00920000` (ClientHello) and
  `0x00930000` (`tls_io`, including an 18 KiB `rbuf`).

Negative return codes:

| Code | Meaning |
|---|---|
| -1 | ClientHello write failed |
| -2 | No ServerHello |
| -3 | Bad ServerHello |
| -4 | Unsupported cipher |
| -5 | Bad record during handshake |
| -6 | Decrypt failure |
| -7 | No server Finished |
| -8 | Finished write failed |
| -9 | Request write failed |
| -10 | Certificate message or leaf certificate could not be parsed |
| -11 | Chain does not lead to a trusted root (or a signature in it is wrong) |
| -12 | Certificate does not match the host name |
| -13 | Certificate expired or not yet valid |
| -14 | CertificateVerify signature invalid |
| -15 | Server Finished invalid |
| -16 | Server skipped certificate authentication |

`g_tls_why` holds a readable reason for the last result; the `https` command
prints it.

#### 9.9.3 Certificate verification (`lib/crypto/nvxx509.c`)

Trust anchors. `nvxroots.c` stores, for each of 120 Mozilla roots, the raw
DER subject name and SubjectPublicKeyInfo (55 KB in total) plus a display name.
Only these two fields are needed to verify a chain. The one P-521 root in the
bundle is left out because P-521 is not implemented. Regenerate the header with:

```sh
python3 tools/mkroots.py kernel/lib/crypto/roots/mozilla-cacert.pem kernel/lib/crypto/nvxroots.c
```

Extra anchors from `/etc/ssl/local-ca.pem` are searched first.

Chain building (`x509_verify_chain(certs, lens, n, host, now)`):

```
parse every certificate (a broken extra certificate is ignored, a broken leaf fails)
leaf: now within notBefore..notAfter (skipped when now == 0)        -> -13
leaf: host matches a SAN dNSName / iPAddress, or the CN if no SAN   -> -12
cur = leaf, repeat up to 10 times:
    cur is itself an anchor (same subject and key)          -> trusted
    an anchor's subject == cur.issuer and its key verifies cur -> trusted
    else pick a not-yet-used certificate from the server's list with
      subject == cur.issuer, basicConstraints CA:TRUE, valid dates,
      and a key that verifies cur's signature             -> cur = it
    none found                                             -> -11
```

The server may send its chain in any order, include the root or omit it.

Host names. Comparison ignores case. A wildcard is accepted only as the
whole leftmost label (`*.example.com` matches `a.example.com`, not
`example.com` or `a.b.example.com`). An IPv4 literal is matched only against
iPAddress SAN entries.

Signatures supported in certificates: sha256/384/512WithRSAEncryption
(PKCS#1 v1.5, 2048-4096-bit keys) and ecdsa-with-SHA256/384/512 on P-256 and
P-384. In CertificateVerify: `ecdsa_secp256r1_sha256`, `ecdsa_secp384r1_sha384`
and `rsa_pss_rsae_sha256/384/512` (salt length = hash length, as TLS 1.3
requires); PKCS#1 v1.5 there is refused, as the RFC requires.

Arithmetic. Numbers are little-endian arrays of 32-bit limbs. `mont_mul` is
CIOS Montgomery multiplication; `mont_init` computes `-n^-^1 mod 2^3^2` by Newton
iteration and `R^2 mod n` by repeated doubling. RSA uses square-and-multiply with
the public exponent. ECDSA validates the public point (on the curve, coordinates
below p), computes `u1*G + u2*Q` with Shamir's trick in Jacobian coordinates
(`a = -3` doubling formulas), and inverts with Fermat's little theorem. Nothing
here needs to be constant-time, because verification uses only public data.

Tests. `tools/check.py --only tls` builds `tools/tests/tls/x509h.c` on the
host and checks: every root key parses; 117 Mozilla root self-signatures verify
(RSA-2048/4096, P-256, P-384); and 31 generated cases from
`tools/tests/tls/tlsvec.py` (wildcards, IP SANs, wrong names, unknown roots,
missing or non-CA intermediates, expired leaves, tampered signatures, chains out
of order, cross-root confusion, and CertificateVerify for every scheme with and
without a tampered transcript). The `net` group repeats the important cases
against a real TLS 1.3 server (Python `ssl`) through QEMU.

### 9.10 Wi-Fi: Realtek RTL8821CE

The driver is a hand transcription of Linux rtw88. It is driven step by step from
the `wifi` shell command and describes itself as EXPERIMENTAL. MMIO goes through
`rtw_r8/16/32` and `rtw_w8/16/32` on BAR addresses, using the identity map. RF
registers are written through the 3-wire SIPI port `0xC90` (`rtw_rf_write_a`) and
read at `0x2800+addr*4` (`rtw_rf_read_a`).

#### 9.10.1 Bring-up sequence (shell subcommands)

| `wifi ...` | Code | What happens |
|---|---|---|
| (none) | `commands.c` | Finds PCI class 02/80, lists BARs, prints the firmware size, signature and version, reads `SYS_CFG 0xF0` |
| `init` | inline `seqp[]` | rtw88 `trans_cardemu_to_act_8821c` power-on: masked writes and polls on 0x20, 0x05, 0x75, 0x06, ... |
| `efuse` | `rtw_efuse_dump_phy`, `rtw_efuse_logical` | Reads 512 B of physical eFuse via `0x30`, decodes the logical map (96 B protected). MAC at logical `0xD0`, crystal cap at `0xB9` |
| `allbuilt` / `g...` | `rtw_phy_power_on`, `rtw_apply_built` | Applies built-in tables from `rtw_tables.h`: MAC 138, BB 1669, AGC 386, RF-A 586 writes (size 5 = RF, offsets `0xF9..0xFE`/`0xFFE` = delays). Then programs the MAC from eFuse and runs `rtw_phy_post` (xtal cap) and `rtw_wmac_init` |
| `mac`/`bb`/`agc`/`rf` | `rtw_read_table_file`, `rtw_tbl_parse` | Same as above, but loads `RTWMAC.TXT`, `RTWBB.TXT`, `RTWAGC.TXT` or `RTWRFA.TXT` from USB-FAT or FAT ("off size val" hex lines) |
| `macinit` / `trx` | `rtw_mac_trx_init` | Queue-priority map, page allocation (HQ/LQ/NQ 16, EXQ 14, public = rest of 460), reserved boundary 460, auto-LLT, RX FIFO boundary, `RX_DRVINFO_SZ=4`, H2C queue at page 500 |
| `fw` | `rtw_fw_download_iddma` | IDDMA firmware download (below) |
| `load` | inline | Older page-write download (`FWDL_EN` 0x80, 4 KiB pages) |
| `h2c` | `rtw_h2c_send` | Sends QUERY_BT_INFO through the H2C mailboxes `0x1D0..0x1DC` and `0x1F0..0x1FC`, waits on `HMETFR 0x1CC` |
| `txbuf` | `rtw_bcnq_stage` | Test: stages a page through the beacon queue and reads it back |
| `rings` | inline | Allocates RX 128x8 B descriptors + 128x2048 B buffers, BE and MGMT TX rings of 256x16 B; programs `RXBD_DESA 0x338`/`NUM 0x382` etc. |
| `scan` | `rtw_scan_drain` | Channel hop 1, 6, 11, 2-5, 7-10, 12, 13; 3 x 60 ms per channel |
| `rx [dma] <ch>`, `probe <ch>` | `rtw_rx_poll_beacon`, `rtw_tx_probe` | Diagnostics: listen for beacons, or send a broadcast probe request and wait for a probe response |
| `open` | `rtw_scan_drain` | Lists open networks |
| `join <ssid>` | `rtw_assoc` | Open-system authentication and association |
| `wpa <ID\|ssid>` | `wpa_connect_saved`, `wpa_do_join` | Prompts for a password, then `rtw_wpa2_join` |
| `dhcp` | `netdev_set(NETDEV_WIFI)`, `dhcp_config` | Gets a lease over Wi-Fi; on failure prints rx/drop/tx counters and switches back to Ethernet |
| `eth` | `netdev_set(NETDEV_ETH)` | Makes Ethernet the active link again |
| `cryptotest` | inline | Checks SHA-1, PBKDF2 and other primitives against known vectors |
| `dump` | inline | Saves MMIO `0x000..0xFFF` to `RTWREGS.BIN` on USB |

The help line prints the order `init efuse allbuilt macinit txbuf fw h2c bb agc rf
rings load dump`. The join paths refuse to run until `wifi rings` has been done
("run 'wifi rings' first (after init/allbuilt/macinit/fw)").

#### 9.10.2 Firmware loading

`build.sh` step `[3a2]` links `assets/firmware/rtw8821c_fw.bin` into the kernel with
`ld -r -b binary`. The result is the symbols `_binary_rtw8821c_fw_bin_start/_end`.

`rtw_fw_download_iddma` reads the firmware header to locate each memory region:

| Header field | Offset |
|---|---|
| mem_usage | `0x18` |
| DMEM addr / size | `0x20` / `0x24` |
| IMEM size | `0x30` |
| EMEM size / addr | `0x34` / `0x38` |
| IMEM addr | `0x3C` |

Each region gets 8 extra bytes (checksum), and EMEM is loaded only if
`mem_usage` bit 4 is set. The download proceeds as follows:
1. Hold the CPU in reset (`0x1D`, `0x03`), enable `FWDL_EN` (`0x80`), back up the
   queue and beacon registers, and switch to beacon-queue staging.
2. For each 4 KiB chunk, `rtw_bcnq_stage_pg` writes a 48-byte TX descriptor with an
   XOR checksum (`rtw_desc_chksum`) and DMAs it into the TX buffer. Then
   `rtw_iddma_block` copies it from `0x18780000+48` to the target address through
   `0x1200/0x1204/0x1208`.
3. `rtw_fw_checksum_ok` checks `0x1208` bit 27 and sets the DMEM or IMEM checksum
   flags in `0x80`. The per-region results are recorded in `g_dmem_ck`,
   `g_imem_ck` and `g_emem_ck`.
4. Restore the registers, release the CPU, and poll `0x80` for the FW-ready bits.

#### 9.10.3 RX descriptor parsing and scanning

Every RX buffer begins with a 24-byte RX descriptor. In word 0, `plen=w0&0x3FFF`,
the CRC error is bit 14, the driver-info size is `((w0>>16)&0xF)*8`, and the shift
is `(w0>>24)&3`. The 802.11 frame begins at `hoff = 24+drvsz+shift`. All parsers
clamp to `RXBUFSZ 2048`.

The hardware write pointer is `0x3B4>>16`. The host read index is written back to
`0x3B4`, and each consumed slot is re-armed to 2048 bytes.

`rtw_scan_drain` keeps beacons (`fc&0xFC==0x80`). From each one it takes the
BSSID from addr3, the Privacy capability bit (security 1), the SSID IE 0, the
RSN IE 48, or the WPA vendor IE `00:50:F2:01` (security 2). It stores the result
in `g_aps[24]` with `scan_add`, which de-duplicates by BSSID. Channel tuning
(`rtw_set_channel_2g`) rewrites RF18, which holds the channel and the 2.4 GHz band
bits. It also switches the antenna to WLG, toggles XTALX2 and clears the MAC
channel registers `0x483`/`0x668`. Only 2.4 GHz channels 1-13 are supported.

#### 9.10.4 TX path and association

The TX buffer descriptor is 2 x 8 bytes: {48, psb pages of 128 B, &txdesc} and
{len, 0, &frame}. The 48-byte TX descriptor contains:
- w0: pkt len, offset 48, LS (bit 26); BMC (bit 24) only for broadcast probes;
- w1: QSEL (MGMT=18, BE=0) and RATE_ID 8;
- w3: USE_RATE|DISDATAFB;
- w4: rate 0 (1 Mb/s CCK);
- w8: EN_HWSEQ.

Management frames are kicked through `TXBD_IDX_MGMTQ 0x3B0`, and data and EAPOL
frames through `TXBD_IDX_BEQ 0x3A8`.

`rtw_assoc` (open networks) and the first half of `rtw_wpa2_join` work as follows:
1. Take the MAC from `0x610`, falling back to eFuse.
2. Write the BSSID to `0x618` and set the network type to infrastructure (`CR` bits 16-17 = 2).
3. Tune to the AP's channel (default 6) and re-arm the RX ring.
4. Send AUTH (`0xB0`, open, seq 1) and wait 800 or 900 ms for the response.
5. Send ASSOC_REQ (`0x00`): capability `0x21` (`0x31` with Privacy), listen interval 10, SSID, rates 1-54 Mb/s, plus the RSN IE for WPA2.
6. Wait for ASSOC_RESP (`0x10`) and check its status.

`rtw_rx_wait_mgmt` stashes any EAPOL frame it sees (`g_eap_stash`), so a message 1
that arrives before the association response is not lost.

| Return code | `rtw_assoc` | `rtw_wpa2_join` |
|---|---|---|
| 0 | associated | handshake done, keys installed |
| 1 | no AUTH response | same |
| 2 | AUTH rejected | same |
| 3 | no ASSOC response | same |
| 4 | ASSOC status not equal to 0 | same |
| 5 | - | no EAPOL msg 1 (or < 95 B) within 1500 ms |
| 6 | - | no EAPOL msg 3 within 1500 ms |

#### 9.10.5 WPA2-PSK 4-way handshake

```
PMK  = PBKDF2-HMAC-SHA1(pass, ssid, 4096, 32)          (rtw_wpa2_join)
AP  -> msg1  ANonce                                     (rtw_rx_wait_eapol)
STA : SNonce = csprng_bytes(32)
      PTK(48) = PRF-384(PMK,"Pairwise key expansion",
                min/max(AA,SPA) || min/max(ANonce,SNonce))  (wpa_derive_ptk)
      KCK=PTK[0..15] KEK=PTK[16..31] TK=PTK[32..47]
STA -> msg2  keyinfo pairwise|MIC|ver2, replay=msg1, SNonce, keydata=RSN IE,
            MIC=HMAC-SHA1(KCK)[0..15]                  (rtw_tx_data_eapol)
AP  -> msg3  -> wpa_gtk_extract: verify MIC, AES-unwrap keydata with KEK,
            find KDE 00-0F-AC:1 -> GTK (must be 16 B, CCMP)
STA -> msg4  keyinfo pairwise|MIC|secure|ver2, replay=msg3
install: rtw_sec_enable_engine; CAM[0]=TK (type 4 CCMP, pairwise);
         CAM[keyid]=GTK if keyid 1..3; wnd_setup(); wnd_set_gtk()
```

The RSN IE that the station sends (`wpa_rsn_ie`) is 20 bytes: version 1, group
CCMP, pairwise CCMP, AKM PSK, capabilities 0.

EAPOL frames travel as 802.11 data frames (`FC 08 01`, ToDS) with an LLC/SNAP
header `AA AA 03 00 00 00 88 8E`. On receive, the EAPOL parser allows for the
QoS header (+2) and the HT-control field (+4).

The hardware security CAM (`rtw_sec_write_cam`, ported from rtw88 `sec.c`) writes
8 dwords per entry through `SEC_WRITE_REG 0x674` and `SEC_CMD_REG 0x670`:
- dword 0 holds the key index, the type, the group flag, the valid bit (15) and MAC bytes 0-1;
- dword 1 holds MAC bytes 2-5;
- dwords 2-5 hold the key.

`wpa_gtk_status()` reports why a GTK was skipped: the msg3 MIC did not match,
unwrap failed, the group cipher is not CCMP, or the key index is 0.

Code observation, read from the code and not confirmed on hardware: msg1's MIC
bit and the msg3 replay counter are not checked, and the ANonce in msg3 is not
compared with the one in msg1.

#### 9.10.6 Wi-Fi netdev: CCMP bridging (`net/wifi/wifi_netdev.c`)

The CAM is programmed, but `wnd_tx_eth` and `wnd_rx_eth` do the CCMP encryption
and decryption in software.

TX (`wnd_tx_eth`): turns an Ethernet frame into an 802.11 frame.
1. Build the header: FC `0x08` or `0x88` (QoS) with flags `0x41` (ToDS, Protected); A1=BSSID, A2=own MAC, A3=Ethernet destination.
2. Append the 8-byte CCMP header (`ccmp_hdr`: PN0, PN1, 0, `0x20|keyid<<6`, PN2-5) with `PN = g_wnd_tx_pn++`, and an LLC/SNAP header carrying the EtherType.
3. Encrypt with `aes_ccm_auth_encrypt(TK, nonce, AAD)`. The nonce is priority || A2 || PN (big-endian). The AAD masks FC, includes A1-A3 and the sequence-control fragment bits, and adds the QoS control field when present. An 8-byte MIC is appended.
4. Queue the frame on the BE ring.

RX (`wnd_rx_eth`): accepts data frames with FromDS that have Protected and ExtIV set.
1. Choose the key: the GTK when A1 is a group address (the frame is dropped if no GTK is installed), otherwise the TK.
2. Replay check: the PN must be greater than the last PN for that key type (`g_wnd_rx_pn_uni` / `g_wnd_rx_pn_grp`).
3. Decrypt and verify with `aes_ccm_decrypt_verify`.
4. Remove LLC/SNAP and build `DA=A1, SA=A3, EtherType, payload`.

The counters `g_wnd_rx_frames`, `g_wnd_rx_drop` and `g_wnd_tx_frames` are printed
by `wifi dhcp` when it fails.

### 9.11 Network shell commands

| Command | Implementation | Output / behaviour |
|---|---|---|
| `net` | `nic_up()`, `nic_index()`, `nic_name()` | NIC name and `[ven:dev]`, MAC, `IP a.b.c.d/len GW ... (DHCP\|static)`, DNS. If the link is down it prints "set the VM adapter to PCnet-FAST III (NAT)" |
| `dhcp` | `dhcp_renew()` | Lease summary, or "no DHCP server answered - keeping the static address" |
| `ping A.B.C.D` | `do_ping()` | One echo request: "reply from ..." or "no reply (timeout)". Numeric IPs only |
| `dns host` | `dns_query()` | `host -> a.b.c.d` or "could not resolve" |
| `http`/`https`/`wget`/`get [-k] host[:port][/path]` | `http_fetch` / `https_fetch` | Accepts an `http(s)://` prefix. Prints the status line, the byte count and up to 1400 characters of text with tags stripped. Error codes are mapped to messages (DNS failed, ARP failed, no SYN-ACK, RST, `HTTPS/TLS failed (code -N): <reason>`). `-k` turns off certificate checks for that request; `:port` applies to HTTPS |
| `nettest [host]` | `usr/musl/nettest.c` | musl self-test of the Linux socket layer against the test peer (`tools/tests/net/netpeer.py`): DNS, 300 KB HTTP download, 100 KB TCP echo, UDP echo, non-blocking connect, refused port, IPv6 refusal |
| `netlog` | `NETLOG[18][52]` ring | The last 18 stack events (e.g. "ARP no reply", "DHCP offer ...", "TLS handshake start", "TLS result N: reason"). Since v8.95 every entry is also written to the kernel log as `net: ...`, so it shows up on the serial port |
| `wifi [sub]` | section 9.10.1 | Wi-Fi bring-up, scan, join, WPA2, and `dhcp`/`eth` link switching |

The suggested first test is printed by `net` itself: `dhcp`,
`ping 10.0.2.2`, `dns example.com`, `http example.com`. On real hardware with
Wi-Fi, the sequence the code expects is `wifi init` to `wifi allbuilt` to
`wifi macinit` to `wifi fw` to `wifi rings` to `wifi scan` to `wifi wpa <ID>` to
`wifi dhcp`.

## Part 10: Input, USB, ACPI, sound and graphics drivers

This part goes through the device drivers that are not storage, network or Wi-Fi. For each one it covers the hardware interface, how the driver starts, how data moves, where waits are bounded, which shell command shows its state, and what it does not do. All paths are relative to the repository root. Every file here is a kernel unit with a header under `include/nvx/`, except the two sound back ends. `build.sh` compiles `drivers/sound/ac97.c` and `drivers/sound/hda.c` as separate objects (`MODS=...`) against `include/kapi.h`.

### 10.1 Where the drivers start and how they are polled

All bring-up runs from `kmain` (`core/init/main.c`) in a fixed order once the PIC and PIT are up:

```c
if(inb(0x64)==0xFF){ klog("boot: no PS/2 controller (USB keyboard/mouse only)"); }
else { for(int i=0;i<64&&(inb(0x64)&1);i++) inb(0x60); mouse_init(); klog("boot: ps2 mouse ok"); }
xhci_mouse_autoinit(); klog("boot: xhci probe done");
tp_init(); klog("boot: touchpad probe done");
g_acpi_pwrbtn_cb=acpi_power_button; acpi_aml_init(); klog("boot: ACPI AML done");
battery_init();
```

`power_init()` runs much earlier, right after `klog_init()`, so the S5/reset data is ready before anything else. `get_brand()` (cpuinfo) runs just before `pci_scan()`. Sound is not started at boot. `snd_init()` probes lazily the first time something needs audio.

The main loop is the only place that polls these devices. Only PS/2 is interrupt driven (IRQ1 and IRQ12):

| Main-loop call | Rate | Driver work |
|---|---|---|
| `tp_poll(); etp_poll();` | every iteration | I2C-HID/ELAN-I2C touchpad read; drain the ELAN PS/2 packet ring |
| `acpi_poll(); snd_poll();` | every 2 ticks (20 ms) | EC queries, power button, ACPI notifications, battery; audio ring bookkeeping |
| `xhci_mouse_poll()` | every iteration while `xhid_n` is non-zero | USB HID event rings and key repeat |
| `usb_hotplug_poll()` | gated to 80 ticks (0.8 s) inside the function | root-port and hub-port connect/disconnect |
| `battery_tick()` | each loop in `ST_DESKTOP` | top-bar battery percentage (ACPI or legacy EC) |

When there is no USB HID device and no runnable JS job, the loop halts (`cpu_halt`) until the next interrupt. That is why USB input forces a busy loop: no xHCI interrupts are set up (the comment in `xhci.c` says "Poll-based (no MSI)").

### 10.2 Shared input state and the key queue

Every input driver writes into one set of globals declared in `net/http/http.c` (the include order puts them there):

```c
static volatile int mouse_dx=0, mouse_dy=0;        /* accumulated deltas     */
static volatile int mouse_btn=0, mouse_btn_prev=0; /* left button            */
static volatile int mouse_rbtn=0, mouse_rbtn_prev=0;
static volatile int mouse_scroll=0; static volatile int mouse_mid=0;
static int g_mbytes=3;                             /* 3, or 4 = IntelliMouse */
static volatile int mouse_evt=0;
#define KBUFN 64
static void kpush(char c){ int n=(kh+1)%KBUFN; if(n!=kt){kbuf[kh]=c;kh=n;} }
static volatile unsigned char kdown[128];
static int kpop(void){ if(kh==kt)return -1; char c=kbuf[kt]; kt=(kt+1)%KBUFN; return c; }
```

- `kbuf` is a 64-entry single-producer/single-consumer ring. It holds 63 characters, and when it is full new keys are dropped (`kpush` checks `n!=kt`). `kpop()` returns -1 when the ring is empty.
- Arrow keys are encoded as control codes: left 17, right 18, up 19, down 20. Both the PS/2 path (`kbd_feed`) and the USB path (`usb_key_code`) use them. Escape is 27.
- `kdown[]` is a held-key table indexed by PS/2 set-1 scancode. USB keyboards fill it through `hid2scan()`, which maps letters, digits, arrows, Enter, Esc, Backspace, Tab and Space, plus modifiers at 0x1D/0x2A/0x38/0x5B. That lets shortcuts such as Win+R (`graphics/ui/input.c`: `kdown[0x5B]` and `kdown[0x13]`) work with either keyboard.

`drivers/input/input.c` (7 lines) adds the helpers that the touchpad drivers use. Each one wraps its update in `cpu_irq_save` so it cannot race the IRQ12 handler:

| Helper | Effect |
|---|---|
| `in_rel(dx,dy)` | `mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1` |
| `in_buttons(l,r,m)` | sets the three button globals |
| `in_scroll(v,h)` | `mouse_scroll+=v; mouse_hscroll+=h` |
| `in_gesture(g)` | `g_gesture=g` (consumed once by `graphics/ui/input.c`) |

The consumer is `process_input()` in `graphics/ui/input.c`. It disables interrupts, snapshots and clears the deltas, and feeds `ux_mouse_push()` (the `/dev/input/mouse` device, VFS dev 10). It applies optional acceleration (`g_mouse_accel`) and moves the cursor. Gestures map to desktop actions there: swipe up opens the launcher, swipe down closes the launcher and notifications, and swipe left or right switches workspace. The pinch gestures are produced but not acted on in `graphics/ui/input.c`.

### 10.3 PS/2 keyboard and mouse (`drivers/input/ps2/ps2.c`, `ps2_mouse.c`)

Hardware. The i8042 data port is 0x60 and the status/command port is 0x64. `pic_remap()` unmasks IRQ0-2 on the master (`outb(0x21,0xF8)`) and IRQ12 on the slave (`outb(0xA1,0xEF)`). `isr_body` routes both IRQ1 and IRQ12 to one function:

```c
static void ps2_byte(void){
    u8 st=inb(0x64); if(!(st&1)) return;
    u8 v=inb(0x60);
    if(st&0x20) mouse_feed(v); else kbd_feed(v);   /* status bit 5 = byte came from the aux port */
}
```

Keyboard. `kbd_feed` decodes scancode set 1 with two 128-entry tables (`kmap_lo`, `kmap_up`). The state flags are `kshift` (0x2A/0x36), `kalt` (0x38), `kcaps` (toggled by 0x3A) and `kext` (the 0xE0 prefix). Extended make codes 0x4B/0x4D/0x48/0x50 become the arrow codes. Releases (bit 7) only clear `kdown[]`. Caps Lock inverts the case of letters only. No keyboard initialisation is sent: the driver relies on the firmware's translated set 1. There are no LEDs, no keymaps other than US, and no function keys (F1-F12 map to 0).

Mouse init (`mouse_init`, `ps2_mouse.c`). Every controller wait is bounded:

```c
static int pw_write(void){ int t=200000; while((inb(0x64)&2)&&--t)io_wait(); return t; }
static int pw_read (void){ int t=200000; while(!(inb(0x64)&1)&&--t)io_wait(); return t; }
static int mcmd(u8 c){ if(!pw_write())return 0; outb(0x64,0xD4); if(!pw_write())return 0; outb(0x60,c);
    if(!pw_read())return 0; inb(0x60); return 1; }      /* consumes the ACK byte */
```

The init sequence:

1. Enable the aux port (0xA8).
2. Read the config byte (0x20), set bit 1 (IRQ12) and clear bit 5 (aux clock disable), and write it back (0x60).
3. Try `etp_detect()` (section 10.4). If it finds a v3/v4 Elantech pad, set `g_mbytes=6`, enable reporting and stop.
4. Otherwise send defaults (0xF6), then the IntelliMouse "magic knock" (set rate 200, 100, 80), then Get ID (0xF2). An ID of 3 switches to 4-byte packets (`g_mbytes=4`).
5. Enable reporting (0xF4) and set `g_mouse_ok=1`.

Packet path. `mouse_feed` resyncs on bit 3 of the first byte. It drops packets with the X/Y overflow bits (0xC0) set, sign-extends the deltas from flag bits 4 and 5, and adds the wheel byte (`int8_t mb[3]`) to `mouse_scroll`. When an ELAN pad is active (`g_etp_hw>=3`), every byte goes to `etp_irq_byte` instead.

Inspect: `mousetest` prints movement, buttons and wheel for up to 3000 ticks (30 s); a key press stops it early. `usbinfo` ends with `PS/2: controller present` or `no controller`.

Limits: only the first PS/2 port gets a keyboard; there is no hot-plug and no 5-button mice.

### 10.4 Elan PS/2 touchpad (`drivers/input/ps2/elan_ps2.c`)

This is a native Elantech v3/v4 decoder. It feeds the shared gesture engine (section 10.5.4) instead of plain mouse emulation.

Detection (`etp_detect`), all through `mcmd` on the aux port:

| Step | PS/2 sequence | Check |
|---|---|---|
| Magic | `F5`, `E6 E6 E6`, `E9` (3 bytes) | `3C 03 C8` or `3C 03 00` |
| Firmware | "sliced" command: `E6`, then four `E8 <2 bits>` pairs, then `E9` | `etp_sig_ok()` rejects rate-like answers |
| Version | `hw=1` if fw<0x020030 or fw==0x020600; IC 2/4 to v2; 5 to v3; at least 6 to v4 | only `hw>=3` is accepted |
| Capabilities | `F8 02 E9` to `g_etp_cap` | |
| Range | `F8 00 E9` to `maxx`, `maxy` | both at least 16 |
| Resolution (v4) | `F8 04 E9` to units/mm via `etp_res_upmm` | default 31 |
| Absolute mode | `etp_write_reg`: reg 0x07=0x01 (v4) or reg 0x10=0x0B (v3) | |

Data path. In IRQ context, `etp_irq_byte` collects 6 bytes and classifies them with `etp_kind()`: v4 head/motion/status packets, v3 packets, the v3 debounce packet `C4 FF FF 02 FF FF`, and trackpoint packets (`(p[3]&0x0F)==6`). An invalid packet makes it shift by one byte and try again (`g_etp_bad++`). Valid packets go into a 64x6-byte ring (`g_etp_ring`); when the ring is full it drops the packet and counts `g_etp_drop`. In the main loop, `etp_poll()` drains up to 64 packets. `etp_packet` updates five finger slots (absolute position for v4 head packets, +/-deltax(1 or 5) for motion packets) and `etp_emit` calls `tpe_frame()`. On a v3 pad a 3-finger report has only one position, so it is copied into three contacts to keep swipes working. Trackpoint packets go straight to `in_rel`/`in_buttons`.

Inspect: `tpinfo` prints `ELAN PS/2 touchpad: Elantech vN firmware ... packets / bad / frames`. For a v1/v2 pad it prints `(old version, used as a basic mouse)`.

### 10.5 I2C HID touchpads

The stack has five layers, each in its own file:

```
touchpad.c   tp_init / tp_poll / tpinfo   - find controller + device via ACPI, pick protocol
i2c_hid.c    ih_*                         - HID-over-I2C protocol (descriptor, reset, features, input)
elan_i2c.c   el_*                         - native ELAN I2C protocol (alternative to HID)
tp_hid.c     tp_parse / tp_bits           - HID report-descriptor parser for PTP + mouse collections
tp_gesture.c tpe_frame                    - pointer, tap, scroll, pinch, swipe engine
dw_i2c.c     dw_*                         - Synopsys DesignWare I2C master
```

#### 10.5.1 DesignWare I2C controller (`drivers/i2c/dw_i2c.c`)

This is MMIO only. `dw_present(base)` requires a page-aligned base below 4 GB and `IC_COMP_TYPE` (0xFC) equal to `0x44570140`.

| Reg | Off | Use |
|---|---|---|
| `IC_CON` | 0x00 | `1 \| speed<<1 \| 1<<5 \| 1<<6`: master, std/fast speed, restart enable, slave disable |
| `IC_TAR` | 0x04 | 7/10-bit target address |
| `IC_DATA_CMD` | 0x10 | bit 8 read, bit 9 STOP, bit 10 RESTART |
| `SS/FS_*CNT` | 0x14-0x20 | written only if firmware left them 0 (600/705, 90/195) |
| `IC_RAW_INTR_STAT` | 0x34 | bit 6 = TX_ABRT |
| `IC_ENABLE` / `IC_ENABLE_STATUS` | 0x6C / 0x9C | toggled by `dw_enable()`, polled at most 100000 `io_wait` |
| `IC_STATUS` | 0x70 | bit 5 master activity, polled at most 100000 at the end |
| `IC_TXFLR` / `IC_RXFLR` | 0x74 / 0x78 | FIFO fill |
| `IC_TX_ABRT_SOURCE` | 0x80 | saved in `last_abort` (shown by `tpinfo`) |
| `IC_COMP_PARAM_1` | 0xF4 | FIFO depths (falls back to 8) |

`dw_xfer(c,addr,w,wn,r,rn)` does one combined write-then-read. It disables the controller, sets the target and re-enables it. It then keeps the TX FIFO full with write bytes followed by read commands (RESTART on the first read, STOP on the last byte), while keeping at most `rxd-1` reads outstanding. The loop is bounded at 400000 iterations. Return codes: `-1` bad args or not initialised, `-2` enable timeout, `-3` abort (NACK, arbitration loss), `-4` timeout. No interrupts and no DMA are used.

#### 10.5.2 Finding the touchpad (`drivers/input/touchpad/touchpad.c`)

`tp_init_ex(scan_all)` uses the raw AML pre-scanner (`aml_scan.c`, section 10.7.2), not the interpreter, because it runs before `acpi_aml_init()`:

1. `aml_load_from_acpi(); aml_scan_tables();` collect devices with `_HID`, `_ADR`, `I2cSerialBus`, `GpioInt` and `Memory32Fixed` resources. Devices with `PNP0C50`/`ACPI0C50` are marked `is_hidi2c`.
2. Controller list (`tp_list_ctrls`, at most 12):
   - ACPI devices `AMDI0010`/`AMDI0510` with a memory resource.
   - On AMD CPUs (CPUID vendor check `tp_is_amd`), fixed MMIO 0xFEDC2000-0xFEDC6000 in 4 KiB steps.
   - Intel PCI functions on bus 0 with class 0x0C80. `tp_pci_dw` enables memory decode, moves the function to D0 through its PM capability (10 ms wait), and if `COMP_TYPE` does not match, writes 7 to BAR+0x204 and checks again.
3. For each candidate device (HID-over-I2C or one of 52 `ELAN0000`-`ELAN1000` IDs), resolve its `I2cSerialBus` controller path to a base address (`tp_ctrl_base`). If the resource has no address, try `TP_ADDRS = {0x2C,0x15,0x2A,0x10,0x20,0x24,0x38,0x40,0x14,0x5D}`. If it has no speed, use 400 kHz. A device that has an ELAN ID and is not PNP0C50 tries the native ELAN protocol first, then HID.
4. HID descriptor registers are tried in the order `TP_DREGS = {0x20,0x01,0x00,0x10,0x30}`.
5. If the device's `GpioInt` controller is `AMDI0030`, the interrupt line is read directly: `gpio_base + pin*4`, bit 16, honouring active-low.

`scan_all=1` additionally brute-forces every controller x address x descriptor register. Its only caller is `tpinfo_run(1)`, reached with `tpinfo scan` (the shell checks the first letter of the argument after `tpinfo`).

Once a descriptor register returns a valid HID descriptor, no other register is tried at that address. `ih_get_desc` decodes into a local copy and writes `t->d` only after the descriptor is valid, so a later failed probe cannot overwrite a good vendor and product. The `_CRS` address that is put in front of the static resource list is removed from the rest of the list (paths compared without `_` and `\`), so it is tried once. A device whose `_HID` starts with `ELAN` is a candidate even without PNP0C50; it is tried with HID first and the ELAN protocol second, like any HID pad with vendor 0x04F3.

#### 10.5.3 HID over I2C (`drivers/input/touchpad/i2c_hid.c`) and report parsing (`tp_hid.c`)

`ih_setup` walks through numbered stages. The stage number is stored in `t->stage` and `tpinfo` prints it when a probe fails:

| Stage | Action |
|---|---|
| 1 | `SET_POWER ON` (opcode 0x08 to `wCommandRegister`), 2 ms |
| 2 | `RESET` (0x01). Poll up to 200x5 ms for the zero-length reset report |
| 3 | Read `wReportDescLength` bytes from `wReportDescRegister` into a 4 KiB buffer, then read it again into a second buffer and compare. Up to 4 attempts; if no two reads match, or the read fails, the bus is set up again at 100 kHz and read again |
| 4 | `tp_parse()` the report descriptor. If the reads never matched, or no touchpad or mouse layout was found, the descriptor is read once more at 100 kHz and parsed again |
| 5 | If it is a PTP: `SET_REPORT(Feature)` Input Mode (usage 0x0D:0x52) = 3, then Surface/Button switches (0x57/0x58) = 1 |
| 6 | Ready. `mode=1` is precision touchpad; `mode=2` is the mouse collection fallback |

The 30-byte HID descriptor is accepted only if `wHIDDescLength==30`, `bcdVersion==0x0100`, the report descriptor is 1..4096 bytes and `wMaxInputLength` is 3..512.

Hybrid reports (one finger per report) are assembled into frames: a report whose Contact Count is above 0 starts a frame that needs ceil(count / fingers per report) reports, and reports with count 0 continue it. Contacts with Confidence 0 are dropped as palms. On Intel Sunrise Point/Kaby Lake (`INT344B`), Apollo Lake (`INT3452`) and Gemini Lake (`INT3453`) the `GpioInt` pin is mapped straight to its pad (`igp_setup` in `tp_igpio.c`), so reports are read only while the pad asserts the line; other Intel controllers learn the pad, and without a pin up to 4 reports are read per 10 ms tick.

`tp_parse` is a general HID item parser with a 4-deep Push/Pop stack. It does not give up on items it cannot use: a Main item with a Report Size outside 1..32 or a Report Count above 4096 is skipped (its bits still count toward the report length), a truncated item ends the parse at that byte, and collections deeper than 16 are counted but not tracked. The item count, the number of skipped items, the byte of the first skipped item and the byte where the data was cut off are kept for `tpinfo` and `tpdump`. It records bit offsets per report ID into a `tplay`:
- under a Digitizer/Touch Pad application (0x0D:0x05), each Finger logical collection (0x0D:0x22) becomes a finger slot (max `TPF_MAX` 10) with Tip, Confidence, Contact ID, X and Y fields;
- Contact Count (0x54), Scan Time (0x56) and Button 1-3;
- the feature fields Input Mode (0x52), Contact Count Max (0x55), Button Type (0x59) and Surface/Button switch;
- a Generic Desktop Mouse application (0x01:0x02) for the fallback.

The physical size comes from Unit/Unit Exponent through `tp_unit_um` (cm or inch). A PTP is rejected if no finger has Tip, X and Y, or if the logical range is empty.

`tp_poll` reads one input report (`dw_xfer` with no write phase, `wMaxInputLength` bytes):
- With a GPIO, it reads while the line is asserted, up to 4 reports per call.
- Without one, it reads at most once per 10 ms tick and drops a report identical to the previous one (`dup++`).

`ih_process` checks the 2-byte length and the report ID, and gathers contacts whose Tip (and Confidence, if present) are set. Hybrid-mode reports that are split across frames (Contact Count > fingers per report) are buffered in `pend[]` until complete.

#### 10.5.4 Gesture engine (`drivers/input/touchpad/tp_gesture.c`)

`tpe_frame(E, contacts, n, button, ms, &out)` is shared by I2C-HID, ELAN-I2C and ELAN-PS/2. All its thresholds are in micrometres, converted using `upmm_x/upmm_y` (units per mm):

| Behaviour | Rule in code |
|---|---|
| Pointer (1 finger, or any count while the clickpad is pressed) | gain 8.0x up to 200 um/frame, rising linearly to 30.0x at 2000 um, with sub-pixel remainder kept in `fx/fy` |
| Tap | touch lifted within 220 ms, total movement at most 2500 um, no button used: 1 finger to left, 2 to right, at least 3 to middle. The release is sent next frame (`pulse`) |
| Clickpad right button | button pressed with at least 2 fingers, or one finger at x>55 %, y>80 % |
| Two-finger scroll | one wheel step per 2000 um; natural scrolling on by default (`E->natural=1`) |
| Pinch | finger distance changes by >8000 um and by more than half the accumulated movement to `GES_ZOOM_IN/OUT` |
| 3+ finger swipe | 15000 um along the dominant axis to one `GES_SWIPE_*` per touch |

#### 10.5.5 ELAN native I2C (`drivers/input/touchpad/elan_i2c.c`)

`el_setup` writes `0x0100` (reset) to register 0x0005 and waits 100 ms. It then reads the 30-byte descriptor (all-0xFF means no device) and the 158-byte report descriptor. Next it reads pattern (0x0100), product (0x0101), firmware (0x0102), IC type (0x0103 or 0x0110 depending on pattern), SM version (0x0104; bit 4 means clickpad), max X/Y (0x0106/0x0107) and resolution (0x0108). It selects absolute mode (0x0300 = 1), wakes the device (0x0005 = 0x0800) and clears the power-off bit in 0x0307. Reports are 34 bytes (pattern at most 1, ID 0x5D) or 39 bytes (high-precision, ID 0x60), with five 5-byte finger records gated by `touch` bits 3..7. Report IDs 0x5E/0x5F carry trackpoint data.

Inspect: `tpinfo` lists the ACPI touchpad candidates (path, HID, I2C address/controller/speed, interrupt pin), the DesignWare controller bases, the ELAN PS/2 status, and the active protocol with vendor/product, finger range, size in mm, read/frame/tap counters, last gesture and the fingers currently down. Boot log lines start with `touchpad:`.

### 10.6 xHCI USB (`drivers/usb/xhci.c`)

#### 10.6.1 Controllers and memory

`xhci_init()` scans every bus/device/function for class 0x0C0330, up to `XHC_MAX` 4 controllers. Per-controller state is swapped into the driver's globals with `xhc_select(i)` (`xhc_save`/`xhc_load`), so all the code below works on "the current controller". All rings and contexts sit at fixed identity-mapped addresses below 16 MiB (the controller DMAs to them, and phys==virt):

| Region | Address | Size |
|---|---|---|
| Controller 0 base (`g_xmem`) | 0x00990000 | DCBAA +0, command ring +0x1000, event ring +0x2000, ERST +0x3000, scratchpad array +0x4000, buffers +0x5000 |
| Controllers 1-3 | `g_xhc_mem[k-1]` (static, 0x26000 each, page aligned) | same layout |
| HID/hub device region r (0..9) | `0x00A00000 + r*0x8000` | input ctx, device ctx, EP0 ring, interrupt-IN ring, data buffer (4 KiB each) |
| Mass storage | 0x00A50000-0x00A66000 | contexts, EP0/IN/OUT rings, CBW/CSW, 64 KiB sector buffer |

Every ring is 256 TRBs with a Link TRB (type 6, Toggle Cycle) in slot 255. Scratchpads: up to 32 buffers fit in the fixed area. Larger requests use `dma_alloc_pages(spb+1)`, and if that fails the controller is not used.

#### 10.6.2 Controller start (`xhci_init_one`)

1. Read BAR0. A 64-bit BAR whose high half is non-zero sets `xhci_hi4g` and the controller is skipped (`usbinfo`: "registers above 4 GB, cannot be used yet"). Enable MEM + bus master.
2. On Intel, copy `USB3PRM` to `USB3_PSSEN` (0xDC to 0xD8) and `XUSB2PRM` to `XUSB2PR` (0xD4 to 0xD0) to route the shared ports to xHCI.
3. BIOS handoff: find extended capability ID 1 and set the OS-owned byte. Wait up to 400000 `io_wait` for BIOS-owned (bit 16) to clear; if it does not, clear it by force (klog "firmware did not release the controller, taking it"). Then clear the SMI enables in USBLEGCTLSTS.
4. Wait for CNR to clear, clear Run/Stop and wait for HCHalted, then HCRESET and wait (200000/400000-step bounds).
5. `CONFIG.MaxSlotsEn = HCSPARAMS1.MaxSlots`, set DCBAAP, `CRCR = cmdring | RCS`. Interrupter 0 gets ERSTSZ=1, ERDP and ERSTBA.
6. Run, power every port (PORTSC.PP, bit 9), wait ~40000 delays, then record CCS and speed for each port.

#### 10.6.3 Commands, events and control transfers

`xh_cmd_s` enqueues a command TRB, rings doorbell 0 and waits for a Command Completion event (type 33). Success is completion code 1. `xh_event_t` polls the event ring for at most 400000 iterations, advances ERDP with the EHB bit, and parks transfer events that belong to other endpoints in `xh_q[32]`, so a control transfer during enumeration does not lose HID reports. `xh_ctrl` builds Setup/Data/Status TRBs on the current EP0 ring. A STALL (cc 6) is recovered with Reset Endpoint (14) and Set TR Dequeue Pointer (16). Short packet (cc 13) counts as success.

#### 10.6.4 Enumeration (`xhci_enum_loc`)

Enable Slot (9) to Address Device (11) with EP0 max packet size 8/64/512 by speed. For full-speed devices, `xh_fix_mps0` reads 8 bytes of the device descriptor and runs Evaluate Context (13) when bMaxPacketSize0 is 16/32/64. Then GET_DESCRIPTOR device (18 bytes), config (9 bytes, then the full descriptor, capped at 256 bytes). After that:

- Interface class 9 to hub (`xhub_attach`, section 10.6.6).
- Interface class 3 to HID. `hid_pick` keeps interfaces that have an interrupt-IN endpoint (max 6). Then for each interface:
  - subclass 1 / protocol 1 (boot keyboard): use the boot protocol, 8-byte reports;
  - anything else: GET_DESCRIPTOR(Report, 0x22) and `hid_parse` it into up to 4 `hidlay_t` layouts. If that succeeds the interface uses the report protocol, with report IDs if present. If it fails, a boot mouse (1/2) falls back to boot protocol.
  - SET_PROTOCOL (0x0B) is sent to boot-subclass interfaces, and SET_IDLE (0x0A) to keyboards.
- Each kept interface gets its own region and interrupt-IN ring. All of them share one slot, and a single Configure Endpoint (12) programs every endpoint (type 7, interval from `xh_interval`). The first Normal TRB is armed with IOC (`xhid_arm`).

That per-interface layout is what makes multi-function devices work, such as a combined keyboard + mouse, or a wireless receiver exposing both on one device. There is no vendor-specific receiver code: such devices work if their interfaces are standard HID with report descriptors `hid_parse` understands (keyboard bitmap or array, mouse buttons/X/Y/wheel).

Failures stop at a named step stored in `xhci_failstep`, for example `"Address Device"` or `"no keyboard or mouse interface"`, together with `xhci_failcc`.

#### 10.6.5 HID data path

`xhci_mouse_poll()` to for each controller, `xhci_poll_one()`. It first replays parked events, then handles up to 128 new events. For each transfer event, `hid_xfer` finds the endpoint, copies up to 16 bytes into `xhid_last` (shown by `usbinfo`) and decodes:

- boot mouse: `btn, int8 dx, int8 dy[, int8 wheel]`;
- boot keyboard, or a report-protocol keyboard turned into an 8-byte boot report: `kbd_report` diffs against the previous report, pushes ASCII through `usb_key_code` and updates `kdown[]`. Caps Lock (usage 0x39) toggles the shared `kcaps`. Reports with ErrorRollOver (`d[2]==1`) are ignored;
- report protocol: `hid_report_decode` uses the parsed bit offsets.

Each HID endpoint has 16 transfers in flight (`XHID_INFLIGHT`). `xhid_arm` gives each Normal TRB its own 64-byte slot in the region's data buffer, rotating through `xhid_bs[]`, so reports that arrive while the desktop is redrawing are not overwritten. The transfer event carries the TRB address; `hid_xfer` reads the buffer pointer back from that TRB and decodes the report from its own slot. Parked events (`xh_q`) now store the TRB address too. After decoding, one new TRB is queued, which keeps 16 outstanding. Before v9.05 only one TRB was queued, so a 1000 Hz wireless mouse lost most of its reports and the backlog in the dongle made the pointer lag.

Key repeat is done in software (`usb_kbd_repeat`): the first repeat comes after 50 ticks (500 ms), then every 3 ticks (30 ms).

#### 10.6.6 Hubs

`XHUB_MAX` 6 hubs, tiers up to depth 5, and up to 15 ports per hub. In order:

1. Read the hub descriptor (0x29, or 0x2A for SuperSpeed).
2. Mark the slot context as a hub (Hub bit, port count, TT think time).
3. For SuperSpeed hubs send SET_HUB_DEPTH (request 12).
4. Power every port (feature 8) and wait `max(100, bPwrOn2PwrGood*2)` ms.
5. Enumerate connected ports.

`xhub_port_reset` sets PORT_RESET (feature 4) and polls up to 100x5 ms, then clears C_PORT_RESET (20) and C_PORT_CONNECTION (16). Child devices get a route string (`route | port<<(4*depth)`). A low- or full-speed device behind a high-speed hub also gets TT slot/port. A port behind a hub tries mass storage first, then HID. Removal (`xhub_remove_below`) disables every slot whose route lies under the detached port.

#### 10.6.7 Mass storage and hot-plug

Mass storage uses Bulk-Only Transport. The 31-byte CBW has signature `USBC` (0x43425355) and the 13-byte CSW has `USBS`. Commands: TEST UNIT READY (up to 8 tries with 40000-delay waits), READ CAPACITY(10), READ(10) and WRITE(10) on 512-byte blocks. Only one stick is handled at a time (global `msd_ok`). On top of it, `ufat_*` implements FAT16/FAT32 for the root directory only: at most 32 entries, 8.3 names, no long file names. The partition is taken from MBR entry 1 if its type is 0x01/04/06/0B/0C/0E, otherwise it is a superfloppy. Cluster allocation scans at most 65536 clusters. A write reallocates the whole file. The FAT cache is one sector, written to all FAT copies.

`usb_hotplug_poll` (every 0.8 s) compares each root port's CCS with `usb_seen[]`. On a new device it tries mass storage first, auto-mounts it and sets `g_usb_arrived`; the desktop then opens Files and shows a notification. Otherwise it enumerates HID. On removal it tears down that port's devices. Hub ports are polled by `xhub_poll_ports`.

Shell: `usb`/`xhci` (rescan everything, then `usbinfo`); `usbinfo` (per controller: PCI location, ready/failed, scratchpad count; per port: speed and what was set up; per HID endpoint: keyboard/mouse, boot or report protocol, report count and last bytes; hubs with their connected ports; last failure step); `usbdisk`/`usbls`, `usbcat <F>`, `usbwrite <F> <text>`, `usbrm <F>`; `mousetest`.

Limits: no interrupts or MSI; no isochronous or other device classes; no USB storage beyond one stick and one FAT root; registers above 4 GB are unsupported; MSD transfers are limited to the 64 KiB buffer.

### 10.7 ACPI and power

#### 10.7.1 Static tables and power states (`core/power.c`)

`acpi_find_rsdp()` tries, in order:
- the bootloader handoff (signature `RSDP` at 0x6000+128, pointer at 0x6000+120);
- the EBDA (first 1 KiB);
- 0xE0000-0xFFFFF.

The RSDP must pass its 20-byte checksum. `acpi_tbl_ok` rejects tables above 4 GB, shorter than 36 bytes, longer than 4 MiB, or with a bad checksum. `power_init` reads the FADT (SMI_CMD, ACPI_ENABLE, PM1a/b EVT/CNT including X_ GAS fallbacks, the reset register, and the hardware-reduced sleep control register) and finds `\_S5`/`\_S1` by byte-pattern search for `NameOp "_S5_" PackageOp` in the DSDT and up to 8 SSDTs. This does not need the interpreter.

| Function | Sequence |
|---|---|
| `power_off()` | flush gdisk; unmount NTFS and VFAT; HW-reduced: write `SLP_TYP<<2 \| SLP_EN(0x20)` to SLEEP_CONTROL. Otherwise `acpi_enable_mode()`, then `PM1a_CNT = SLP_TYP<<10 \| 1<<13` (and PM1b). Fallbacks `outw(0x604,0x2000)`, `outw(0xB004,0x2000)`, `outw(0x4004,0x3400)`, then the "It is now safe to turn off your computer." screen and `cpu_stop()` |
| `power_reboot()` | FADT reset register (I/O or memory) to 0xCF9 (0x02 then 0x06) to 8042 `0xFE` to triple fault |
| `power_sleep()` | S1 only (not on HW-reduced): clear WAK_STS, enable PWRBTN_EN, write SLP_TYP\|SLP_EN, spin at most 200,000,000 `cpu_pause` for WAK_STS. Without S1: black screen until a key is pressed |
| `acpi_enable_mode()` | `outb(SMI_CMD, ACPI_ENABLE)`, wait for SCI_EN at most 3,000,000 `io_wait` or 1 s |

Shell: `shutdown`/`poweroff`/`halt`, `reboot`/`restart`, `suspend`.

#### 10.7.2 AML pre-scanner (`drivers/acpi/aml_scan.c`)

This is a non-executing walker for the touchpad code. It copies pointers to the DSDT and all SSDTs (max 33), finds every Scope/Device (`0x10`, `0x5B 0x82`) up to 768 nodes, and builds full paths. For each device (max 160) it scans raw bytes for:
- `_HID` (string or EISA ID, decoded by `aml_eisa`) and `_ADR`;
- the `PNP0C50`/`ACPI0C50` HID-over-I2C IDs;
- `I2cSerialBus` (0x8E, type 1): address, speed and controller path;
- `GpioInt` (0x8C): pin, polarity and controller path;
- `Memory32Fixed` (0x86).

Child devices are skipped (`aml_in_child`). Because it never runs methods, a `_CRS` that is built at runtime (`Method(_CRS)` returning a computed buffer) is still found only if the resource bytes appear literally in the device body.

#### 10.7.3 AML interpreter (`drivers/acpi/aml.c`)

Namespace lookups (`anode_child`) check every node pointer before using it: the address must be at least 1 MiB, below 4 GiB and 8-byte aligned. A bad link ends the walk with "not found" instead of a page fault.

This is a full tree-walking interpreter. Values are `aobj`s (Integer, String, Buffer, Package, Reference). Namespace nodes are `anode`s covering Scope, Device, Name, Method, OperationRegion, Field, IndexField, BankField, BufferField, Mutex, Event, Processor, PowerResource, ThermalZone, Alias and native methods. Integer width follows the DSDT revision (`g_aml_rev`, 32-bit when <2). The predefined namespace contains `\_SB`, `\_GPE`, `\_PR`, `\_TZ`, `\_SI`, `_GL`, `_OS = "Microsoft Windows NT"`, `_REV = 2`, and a native `_OSI` that answers true for the Windows version strings.

Every execution is bounded:

| Guard | Value |
|---|---|
| Terms per `aml_eval` / per table load | 2,000,000 / 4,000,000 (`g_aml_budget`) |
| Wall time per evaluation | 1000 ms outside a batch, `g_aml_time_ms` in a batch, checked every 256 terms |
| Hard batch deadline | 15 s for `acpi_aml_init`, 500 ms for each `acpi_poll` |
| `Sleep()` | taken from a shared pool (`g_aml_sleep_pool`: 5000 ms at init, 300 ms per poll) |
| `Stall()` | capped at 100 us per call, charged to the same pool |
| Call depth / expression nesting | 32 / 192 |
| Buffer size / field size | 0x40000 bytes / 2^18 bits |
| Bad terms per table | the loader skips a failed package and gives up after 64 |

Method-local named objects are tracked (at most 64 per frame) and unlinked on return. `Notify` pushes into a 32-entry ring that `acpi_drain_notify` reads. A Field with LockRule takes the Global Lock through `aml_gl_hook`.

#### 10.7.4 Region access, EC, battery, backlight (`drivers/acpi/acpi_dev.c`)

`acpi_aml_init` installs the hooks in `aml.c`:

| Space | Handler (`acpi_rd`/`acpi_wr`) |
|---|---|
| 0 SystemMemory | 0x1000 at most addr < 4 GB. Writes go only to RAM the firmware map marks usable for it (`mm_first_bad`), or, without a map, never below 256 MiB or into the heap. Blocked writes are counted in `g_acpi_memblock` |
| 1 SystemIO | `in/out` b/w/l, 64-bit as two dwords |
| 2 PCI_Config | 0xCF8/0xCFC; bus/dev/fn from `_BBN`/`_ADR` up the tree, following bridge secondary bus numbers |
| 3 EmbeddedControl | `aec_read`/`aec_write` |
| 5 CMOS | 0x70/0x71 (low 128 bytes), 0x72/0x73 (high) |

The Global Lock is the FACS dword at +16, taken with a compare-and-swap. Waits are capped at 50 ms. Releasing a lock that had the pending bit set writes GBL_RLS (PM1a_CNT bit 2).

Start-up order in `acpi_aml_init`:
1. Load copies of the DSDT and up to 16 SSDTs.
2. Enable ACPI mode and clear PWRBTN_STS.
3. Run `_REG` for spaces 0/1/2.
4. Set up the EC and run `_REG(3,1)`.
5. Run `_INI` on every device whose `_STA` says present, recursing into children when present or functional.
6. Find `PNP0C0A` batteries (max 2) and the `ACPI0003` adapter.
7. Read the battery, set up the backlight, drain notifications.

Embedded controller. Ports come from the ECDT, or from a `PNP0C09` device's `_CRS` (default 0x62/0x66), plus `_GPE` and `_GLK`. Commands: 0x80 read, 0x81 write, 0x84 query. Each IBF/OBF wait is at most 20000 `io_wait`. After 8 consecutive timeouts a circuit breaker disables the EC for 5 s (`trips`). `acpi_poll` runs up to 8 queries per call while SCI_EVT (status bit 5) is set and calls `_Qxx`.

Power button. `acpi_poll` reads PM1a_STS bit 8 (PWRBTN_STS), clears it, debounces for 100 ticks and calls `acpi_power_button()` to `power_off()`. This works only in polled, non-HW-reduced mode. No SCI interrupt handler is used.

Battery. It reads `_BIX` (at least 20 elements) or `_BIF` (at least 13) for design/full capacity, model, serial, type and OEM, and `_BST` for state, rate, remaining and voltage. Percentage = sumremaining/sumfull, rounded. Minutes to empty or full come from the rate (discarded above 99 h). It is refreshed every 500 ticks, and also when a battery or AC device gets a notification (0x81 forces a full `_BIX` re-read). `graphics/ui/desktop.c` prefers this data (`bat_via_ec=2`). With no ACPI battery it falls back to an older fixed-port EC reader on 0x62/0x66 that guesses the percentage from offsets {0x2A,0x32,0x42,0x90,0xB0,0x14,0x24} and the charging state from offset 0x01 bit 0.

Backlight (`bl_*`).
- `bl_find` looks for a device with both `_BCM` and `_BCL`. Its levels are the de-duplicated `_BCL` entries from index 2 on, in the range 0-100.
- `bl_intel_probe` checks for an Intel display device at 00:02.0. BAR0 is mapped even above 4 GB: `acpi_hi_map` builds 2 MiB pages (flags 0x9B) for addresses below 512 GiB. It then works out which PWM register pair is live (offsets 0xC8250/0xC8254/0xC8258 or 0x48250/0x48254 to `pwm_kind` 1-3).
- `bl_set` calls `_BCM` with the nearest level and also writes the PWM duty.
- `_DOS(4)` is called on the output's parent. Notifications 0x86/0x87 step the brightness and show a toast.
- The minimum level is the smallest non-zero `_BCL` level, or 5 % for PWM only.

Shell.
- `acpi`: tables, objects, KB of AML, set-up time, ACPI mode, AML errors, EC ports/GPE/events/timeouts, batteries/AC, backlight, notifications and blocked RAM writes.
- `acpi ls [path]`, `acpi eval <path> [int args...]`, `acpi ec` (dumps 256 EC bytes).
- `battery`/`bat`: ACPI details. With no ACPI battery, or as `battery e...`, it runs the legacy EC offset scanner.
- `brightness`/`backlight [up|down|N]`.

A second `acpi` branch further down `commands.c` (S5/S1/reset summary) cannot be reached because the earlier `seq(cmd,"acpi")` matches first.

### 10.8 RTC (`drivers/rtc/cmos.c`)

```c
static u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
for(int _g=0;_g<100000&&(cmos(0x0A)&0x80)&&cmos(0x0A)!=0xFF;_g++){}   /* bounded UIP wait */
```

`read_clock` reads seconds, minutes, hours, day, month and year (registers 0, 2, 4, 7, 8, 9). It converts from BCD unless register B bit 2 is set, and converts 12-hour time unless bit 1 is set. Limits: the year is always `2000+y` (the century register is ignored), the time is not read twice to check consistency, there are no writes and no alarms. Commands: `time`, `date`. `uptime` uses `g_ticks/100`, not the RTC.

### 10.9 Sound (`core/sound.c`, `drivers/sound/*`)

#### 10.9.1 Framework

Back ends fill a `struct nvx_snd` (`include/drivers.h`): `name, rate, start, free_bytes, write, poll, stop, volume`. `snd_init()` runs once: `hda_probe()` first, then `ac97_probe()`. It records state 1 (up) or 2 (none) and applies `g_snd_vol` (80).

The output format is fixed at 48 kHz, 16-bit, stereo, little-endian. Both back ends use the same ring (`drivers/sound/ring.h`) over a DMA buffer of 32x4096 bytes:

```c
static inline void sring_hw(struct sring* r,u32 pos){        /* pos = hardware play position */
    u32 delta=(pos+r->size-r->last)%r->size;
    for(u32 i=0;i<delta;i++)r->buf[(r->rd+i)%r->size]=0;      /* zero what was played: underrun = silence */
    r->rd+=delta; r->last=pos; if(r->wr<r->rd)r->wr=r->rd; }
static inline int sring_free(const struct sring* r){ u64 used=r->wr-r->rd; u32 guard=4096;
    if(used+guard>=r->size)return 0; return (int)(r->size-guard-used); }
```

The DMA engine loops over the whole buffer forever. The driver never takes an interrupt; it learns the position by polling (`snd_poll` every 20 ms, and also inside every `write`).

`/dev/dsp` (also `/dev/audio`, VFS dev 12): `snd_dev_write` starts the stream and writes whole frames (`len&~3`). If the ring is full it retries every 1 ms for up to 2000 tries. A JS-runtime process gets `JS_BLOCK` instead and is rescheduled. `sys/linux/linux.c` answers two OSS ioctls for the same device: 0xC0045002 (`SNDCTL_DSP_SPEED`, returns the rate) and 0x8010500C (`SNDCTL_DSP_GETOSPACE`, 32 fragments of 4096).

Shell: `audio`/`sound` (back end and HDA codec/paths), `audio vol <0-100>`, `audio tone [50-8000 Hz]` (one second of a +/-6000 triangle wave from `snd_tone`).

#### 10.9.2 AC'97 (`drivers/sound/ac97.c`)

The device is PCI class 04/01 with I/O BAR0 = NAM (mixer) and BAR1 = NABM (bus master). Probe sequence:
1. Cold reset through Global Control (NABM+0x2C = 2).
2. Wait for codec-ready (NABM+0x30 bit 8), at most 2000x100 us.
3. Mixer reset (NAM+0).
4. If the codec has VRA (NAM+0x28 bit 0), enable it and set the front DAC to 48000. The rate read back is kept; below 8000 it is replaced by 48000.

Volume uses 5-bit attenuation on Master (0x02) and Headphone (0x04), with PCM-out (0x18) at 0x0808, or mute bit 0x8000 at 0 %. PCM-out registers: BDBAR 0x10, CIV 0x14, LVI 0x15, SR 0x16, PICB 0x18, CR 0x1B. The position is `CIV*4096 + (4096 - PICB*2)`. Each poll moves LVI to `CIV-1` so the 32-entry BDL never ends, and clears the status bits (and restarts the engine on DCH). The PCM buffer must lie below 0xFFFF0000.

#### 10.9.3 Intel HD Audio (`drivers/sound/hda.c`)

The device is PCI class 04/03 with a 32-bit MMIO BAR (BARs above 4 GB are refused: "hda: BAR above 4 GB, not mapped").

1. Controller: reset with GCTL.CRST (0x08), with every wait at most 1000x10 us. Pick the first codec from STATESTS (0x0E). The first output stream descriptor index is ISS (from GCAP).
2. CORB/RIRB: use the largest supported sizes (2/16/256), reset the read pointers, then run both. RIRBCTL is set to `0x03` (DMA run plus the response-interrupt enable) while the controller's global interrupt enable stays off: some controllers, QEMU's `intel-hda` among them, only reset their response counter when the driver acknowledges RIRBSTS, which requires the interrupt bit, and before v9.11 they answered just the first verb. `verb()` writes the CORB and polls RIRBWP for up to 20000x10 us (200 ms), then clears RIRBSTS. When no output path is found, the log line lists every widget with its type and connection count.
3. Graph walk (`scan`): find the Audio Function Group (node type 1) and power it to D0 (0x705). For every widget (max 128 NIDs), read the capabilities (param 9) and the connection list (param 0x0E plus verb 0xF02, at most 16 entries), and power it up. Then make three passes over pin widgets that can output (PINCAP bit 4), skip pins whose port connectivity is 1 ("no connection"), and prefer default device speaker (1), then headphone (2), then line-out (0). `dfs()` searches from the pin through mixers (type 2) and selectors (type 3) to an output converter (type 0), depth at most 8. At most 4 paths are kept.
4. Route: unmute output amps (gain = offset step), set selector indices (0x701), unmute mixer input amps, and on each DAC set format 0x0011 and stream tag 1 / channel 0 (0x706 = 0x10). Each pin gets PIN_WIDGET_CONTROL 0x40, or 0xC0 for headphones (0x707), and EAPD (0x70C = 2) if the pin supports it (PINCAP bit 16).
5. Stream: SDnCTL reset/unreset, a 32-entry BDL without IOC, CBL = 128 KiB, LVI = 31, FMT 0x0011, stream tag 1 (SDnCTL byte 2 = 0x10), clear status, RUN. The position comes from SDnLPIB (+0x04).

`hda_volume` scales each path's output-amp gain from 0 to the offset step, and mutes at 0 %.

Host-side tests for both probes, including codec timeout and muted-codec cases, are in `tools/tests/drivers/drvsim.c`.

Sound limits: playback only, no capture; one stream; a fixed format; no jack detection or switching (all found paths are routed at the same time); no HDMI audio codecs beyond what the path search happens to find; the first matching PCI device only.

### 10.10 Video

#### 10.10.1 Boot framebuffer (`drivers/video/fb/framebuffer.c`)

There is no display-mode driver. The kernel draws into the linear framebuffer that the bootloader handed over at 0x5000:

| Handoff | Offset | Meaning |
|---|---|---|
| `FBB` | 0x5000 (u64) | framebuffer address |
| `PITCHB` | 0x5008 (u32) | bytes per line |
| `W`, `H` | 0x500C, 0x500E (u16) | resolution, clamped to `MAXWxMAXH` = 1920x1080 |
| `BPP` | 0x5020 (u8) | 24 or 32 (`BYPP` 3 or 4) |
| RGB flag | 0x5021 (u8, valid if 0x502C == `'UEFI'`) | 1 = RGBX byte order to `g_fb_rgb` swaps R/B on blit |

All drawing goes into `SCENE[MAXW*MAXH]` (32-bit xRGB). `BG` caches the wallpaper and icons. Primitives include `fill_rect`, anti-aliased `fill_round` and `fill_circle` (coverage from `isqrt32`), `shadow_soft`, the 8x14 bitmap font (`dchar/dstr`) and proportional anti-aliased text from `fontaa` (`pdstr`). `blit_all()` copies `SCENE` to the hardware buffer row by row, honouring pitch, 24/32 bpp and the RGB swap. If `g_panel_oled` is set, the whole image shifts by 1 px through 4 positions every 3000 ticks (30 s) to protect OLED panels.

#### 10.10.2 SIMD blitting (`arch/x86_64/cpu/simd.c`)

`arch_fpu_init` enables SSE (CR0.MP, clears EM, CR4.OSFXSR/OSXMMEXCPT) and sets `g_ssse3` from CPUID.1:ECX bit 9. The kernel itself is compiled with `-mno-sse`, so these inline-asm routines are the only code that touches XMM registers:

| Routine | Work |
|---|---|
| `sse_fill32(d,c,n)` | broadcast with `pshufd`, 64 bytes per iteration |
| `sse_copy32(d,s,n)` | 4x`movdqu` per 16 pixels |
| `sse_pack24(d,s,n)` | 32 to 24 bpp with one `pshufb` per 4 pixels (needs SSSE3), scalar tail |

`g_accel2d` (default 1, toggled in Settings > "2D acceleration") chooses between these and plain loops.

#### 10.10.3 Effects (`graphics/theme/effects.c`)

`glass_panel` and `glass_liquid` do in-place frosted-glass effects on `SCENE`: two or three passes of a horizontal running-sum box blur of radius `blurR` (one row buffer `g_glasstmp[MAXW]`), a tint blend, top highlight gradients, and 1 px rim lines that follow the rounded-corner mask (`cut()`). `soft_ring` draws a blended annulus. Everything runs on the CPU.

#### 10.10.4 Write-combining and Intel graphics (`drivers/video/intel/igpu.c`)

`igpu_early()` runs right after the framebuffer is known. `pat_cpu_init()` programs IA32_PAT (MSR 0x277) as `0x0007040600070106`: entry 1 (PWT only) becomes write-combining, the others keep their defaults (nothing else in the kernel uses PWT alone). Every CPU runs it, the application processors in `ap_entry_c`. `wc_map()` then sets PWT and clears PCD and the PAT bit on the 2 MB (or 4 KB) page-table entries that cover the framebuffer, flushes the caches and reloads CR3. Firmware MTRRs normally make the framebuffer uncached; MTRR UC combined with PAT WC gives WC, so screen writes are combined into bursts. It is only done when the framebuffer is at least 2 MB, so the 2 MB pages stay inside the display BAR. `gpuinfo` shows "write-combining" when it was applied.

`igpu_init()` (before the touchpad probe) looks at PCI 00:02.0. For an Intel display controller it reads the device ID (generation and name: Sandy Bridge to Meteor Lake), BAR0 (registers, GGTT at +8 MB on gen 8+), BAR2 (aperture) and the stolen-memory size from GGC (config 0x50). It reads `PIPESRC` (0x6001C) for the current mode, `TRANS_DDI_FUNC_CTL_EDP` (0x6F400) and the eDP or pipe A transcoder timings (0x6F000/0x6F00C or 0x60000/0x6000C) for the panel's native size, the scaler (`PS_CTRL_1_A` 0x68180, gen 9+) or panel fitter (0x68080), and the primary plane (`PLANE_CTL` 0x70180, stride 0x70188, surface 0x7019C).

Tear-free drawing (gen 8 to 12): if the plane is on, linear and 32-bit and the framebuffer is the aperture view of the scanned-out surface, a second buffer of the same size is placed in the GGTT after the first (256 KB aligned, inside the first 256 MB). Its GGTT entries point to pages from `pframe_alloc()` and copy the flag bits of the visible surface's entry; the GGTT TLB is flushed through `GFX_FLSH_CNTL` (0x101008). A test pattern written through the aperture must read back, the current screen is copied in, and the plane must report the new surface in `PLANE_SURFLIVE` (0x701AC) within 100 ms; otherwise the original surface is restored. While on, `blit_all()` waits for the previous flip to finish (up to 40 ms), draws into the hidden buffer and writes its offset to `PLANE_SURF`, which the hardware switches at the next vertical blank. `blit_region()` writes to both buffers. Three late flips turn it off. `gpu flip off`, `gpu flip on` and Ctrl+Alt+F switch it by hand. QEMU has no Intel graphics, so only the checks and the fallback were tested there.

`gpuinfo` prints the GPU, panel and current mode (native or scaled), the plane, the framebuffer (size, address, caching, boot mode), the time of a full-screen copy (average of 10 `blit_all()`), the tear-free state and the blitter state.

2D blitter (gen 9, `igpu_blt_init()`, run after tear-free drawing has started). Each stage sets a step number that `gpuinfo` reports with the reason if it stops:

| Step | Action |
|---|---|
| 1 | Checks: Intel gen 9, tear-free drawing on |
| 2 | Forcewake for the render, blitter and media domains (request 0xA278/0xA188/0xA270, acknowledge 0x0D84/0x130044/0x0D88); held while the blitter is used, released on failure |
| 3 | On Apollo Lake and Gemini Lake (no shared last-level cache) the private PAT (0x40E0/0x40E4) is set like Linux does for these chips: entry 0 and 4-7 snooped, 1-3 not. The old values are shown |
| 4 | GGTT objects after the second screen buffer, each also mapped at the same address in the per-context page table: the page directory, a test buffer (16 pages), the new GPU-shared scene buffer (`MAXW`x`MAXH` pixels from the kernel heap, `gobj(...,1)`), the old kernel scene array `SCENE_STATIC` (for the read test only), the shader state buffer (4 pages) and two 256x128 test images. Engine objects (status page, 4-page ring, context) are added when each engine is set up |
| 5 | The per-context page table (legacy 32-bit: one page directory used for all four PDPs) also maps the two screen buffers (uncached, PAT index 3); every other object is snooped (index 0). Everything is mapped before any engine runs, so no GPU TLB invalidation is needed |
| 6 | Context image for the blitter engine (base 0x22000): two `MI_LOAD_REGISTER_IMM` blocks with context control (restore inhibit), ring head/tail/start/control, batch-buffer registers, timestamp and the four PDPs. Engine set up: interrupts masked, status page address (0x80), STOP_RING cleared (0x9C), execlists enabled (`RING_MODE` 0x29C), context status buffer read pointer reset |
| 7 | First submission through the execlist submit port (0x230, descriptor: valid, legacy 32-bit addressing, privileged): `XY_COLOR_BLT` of 64x64 pixels into the test buffer, then `MI_FLUSH_DW` writing a sequence number into the status page. Waits up to 50 ms for the number, then for an idle event in the context status buffer |
| 8 | Checks the filled pixels, then an `XY_SRC_COPY_BLT` inside the test buffer and checks the copy |
| 9 | Read tests (`rd_tests()`): for the test buffer (DMA arena), the new scene buffer (heap) and the old kernel array, the CPU writes 64 pixels and the blitter copies them into the test buffer, first after a `clflush` of those lines, then without. The flushed test runs first so a line the GPU read earlier cannot hide a wrong address. `gpuinfo` shows ok, wrong (with the value read) or no answer for each of the six tests. The new scene buffer must pass the flushed test; if it also passes the unflushed one the GPU snoops, otherwise every GPU screen copy first flushes the scene from the CPU caches |
| 10 | Blitter usable; the render engine and the shaders are started (below), then `scene_swap()` copies the picture into the new buffer and points `SCENE` at it |
| 11 | Ready. Five full-screen copies by the CPU and five by the GPU are timed; screen updates use whichever is faster |

Why the scene moved: on the Lenovo (v9.67) the GPU read the same old pixel (0x005B6198) with and without a cache flush, so the problem was the address, not caching. `SCENE` was a 8 MB array in the kernel's `.bss` above 0x10000000, where `early_remap()` can move 2 MB pages that the firmware reports as not RAM. `SCENE` is now a pointer (`u32* SCENE`, initially `SCENE_STATIC`), and on gen 9 it is switched to a heap buffer whose pages the GPU driver mapped itself. `gpuinfo` shows the physical address of both buffers and whether the old one was moved at boot. Nothing else in the kernel keeps a copy of the pointer.

Every submission resets the ring head to 0 and sets the tail in the saved context, so the ring never wraps. After the first run the context's restore-inhibit bit is cleared, so later runs restore the engine state the hardware saved.

Render engine (base 0x2000, started after the blitter is ready, `rcs_init()`): the same submission code with a 23-page context (status page plus 22 pages of render state) whose image adds `BB_PER_CTX_PTR`, `INDIRECT_CTX`, `INDIRECT_CTX_OFFSET` and a third `MI_LOAD_REGISTER_IMM` for `R_PWR_CLK_STATE` (0x20C8). On Apollo Lake and Gemini Lake the value is computed like Linux does: the enabled subslices from `FUSE2` (0x9120, bits 22:20 are disable bits) and the enabled EUs per subslice from 0x9134 (8 bits per subslice, 6 used), giving enable (bit 31), subslice count enable (bit 11), the subslice count (bits 10:8) and the minimum and maximum EU count (bits 3:0 and 7:4); a full UHD 605 gives 0x80000B66. Without this request power gating can leave EUs switched off. Other chips keep 0. Completion is signalled by `PIPE_CONTROL` (CS stall, render-target and data-cache flush, post-sync write of the sequence number to the status page through the GGTT). Steps: 7 `MI_STORE_DATA_IMM` into the status page and read back; 8 `PIPE_CONTROL` flush then `PIPELINE_SELECT` GPGPU (0x69040302); 9 `STATE_BASE_ADDRESS` (all bases 0, sizes maximal); 10 `MEDIA_VFE_STATE` (32 threads, 4 URB entries); 11 ready; then the shaders below are started. While the GPU path is used, `blit_all()` has the blitter copy `SCENE` into the hidden buffer and then flips; three failed copies turn it off and the CPU copy takes over. `gpu blit gpu` and `gpu blit cpu` choose by hand. QEMU has no Intel graphics, so only the fallback was tested.

GPU instruction assembler (`drivers/video/intel/gen9asm.c`, stage 2). It encodes native 128-bit gen 9 EU instructions in align1 mode, never compacted, with NoMask: `mov`, `and`, `or`, `shr`, `shl`, `add` with register regions (`<8;8,1>`, scalars `<0;1,0>`) and 32-bit or packed-vector (`:uv`) immediates, and `send` with an immediate message descriptor and the end-of-thread bit. `g9_kernel_fill()` and `g9_kernel_copy()` build the two first shaders. Both were checked bit for bit against Intel's own assembler (IGA, platform gen 9) and its semantic checks (`-Wall`) during development.

| Shader | What one thread does (one 8x8 pixel block, SIMD8, one thread per thread group) |
|---|---|
| fill | Block position from the thread group IDs in r0.1 and r0.6. Lane numbers from `0x76543210:uv`. Pixel = red `(x >> sx) & 255`, green `(y >> sy) & 255`, blue from the constant data in r1.0 (`sx` and `sy` in r1.1 and r1.2). Eight rows in r11-r18 are written with one media block write (32 bytes x 8 rows, header in r10, data port 1, binding table entry 0) |
| copy | Same header; one media block read from entry 1 into r11-r18, then a media block write of those registers to entry 0 |

Both end by copying r0 to r127 and sending it to the thread spawner with the end-of-thread bit.

Shader state (`sh_init()`, one 16 KB buffer that is the surface, dynamic and instruction base): constant data at 0x40 and 0x80, binding tables at 0x100/0x120/0x140, surface states (2D, linear, B8G8R8A8, pitch and size of the image) at 0x200/0x240/0x280, interface descriptors (kernel offset, single program flow, binding table, one constant register, one thread) at 0x400/0x440/0x480, the fill kernel at 0x1000 and the copy kernel at 0x1800.

One dispatch (`sh_run()`): `PIPE_CONTROL` (CS stall, render-target and data-cache flush), `PIPELINE_SELECT` GPGPU, `STATE_BASE_ADDRESS` (all bases at the state buffer), `PIPE_CONTROL` invalidating the instruction, texture, constant and state caches, `MEDIA_VFE_STATE` (64 threads, 2 URB entries, 2 registers of constant space), `MEDIA_CURBE_LOAD` (64 bytes), `MEDIA_INTERFACE_DESCRIPTOR_LOAD` (one descriptor), `GPGPU_WALKER` (SIMD8, width/8 x height/8 thread groups, right mask 0xFF), `MEDIA_STATE_FLUSH`, a `PIPE_CONTROL` with CS stall and data-cache flush, then the usual completion `PIPE_CONTROL`.

| Step | Shader start-up |
|---|---|
| 12 | Kernels assembled into the state buffer, surfaces, binding tables and descriptors written |
| 13 | The fill shader runs over a 256x128 test image (512 thread groups) |
| 14 | Every pixel of the image is checked against the same formula on the CPU; the first wrong pixel is reported with its position, value and expected value |
| 15 | The copy shader copies the image into a second one; every pixel is checked |
| 16 | The fill shader draws a full-screen gradient into the new scene buffer before it is used and is timed; every 7th pixel of every row is checked. The picture is then replaced by the real screen contents, so it is never shown |
| 17 | Ready: `gpuinfo` shows "Shaders: ready" with the times |

If a shader does not finish within 50 ms the render engine is marked stopped and the scene buffer is not switched, so a late GPU write cannot reach the screen. The blitter and CPU drawing are not affected. QEMU has no Intel graphics, so only the fallback was tested.

GPU drawing (stages 3 to 5: `drivers/video/intel/gpukern.c`, `gq.c`, `gfx.c`). `gpukern.c` builds 12 shaders with the assembler; all share a prologue that computes the 8x8 block, a per-pixel mask for the rectangle edges and the media block reads, and an epilogue that writes the block. Constant data per operation holds the block origin, the negated rectangle, colours, alpha, source and mask offsets, gradient step and colour key.

| Shader | Pixel result |
|---|---|
| fill, blend | colour; colour mixed with alpha |
| mask | colour mixed with alpha times a coverage value read from a mask surface (text, rounded corners, circles) |
| copy, copy with mask | source pixel mixed with alpha (times mask); with colour key, transparent source pixels are skipped |
| gradient H/V | two colours mixed along x or y |
| tint | night light (green 15/16, blue 5/8) |
| blur H/V | average of 8 neighbours along x or y |
| over, over with mask | premultiplied source over destination (window layers) |

Draw queue (`gq.c`): surfaces are registered with their CPU pointer, GPU address, size and pitch (one surface state each). An operation is clipped to the destination, then recorded: binding table, interface descriptor and constant data in the state buffer, `MEDIA_CURBE_LOAD`, `MEDIA_INTERFACE_DESCRIPTOR_LOAD`, `GPGPU_WALKER`, `MEDIA_STATE_FLUSH`. Written and read rectangles (block-aligned, reads clamped to the source surface, blur +-4) are tracked per surface; when a new operation overlaps an earlier write, or writes where an earlier operation reads, a `PIPE_CONTROL` CS stall is inserted. A batch holds up to 1600 operations or about 12 million pixels. `gq_sync()` submits and waits; if a batch fails, the queue switches to CPU execution at once and GPU drawing stops. A CPU executor with exactly the same arithmetic is used in emulated mode, as the reference in the self-test, and after a failure.

Drawing layer (`gfx.c`): text is collected in runs and pixels in a batch; both are written by the CPU into staging surfaces (coverage and colour) and drawn by one masked operation. Rounded corners and circles come from a mask cache. `SCENE` is a macro that waits for queued GPU work before the CPU touches the screen buffer; `putpx` and `pblend` go into the pixel batch. Window layers: one full-screen surface per window, cleared to transparent; the window's drawing code runs with the layer as its target; the touched area is remembered. The compositor draws the shadow, glass behind (blurred scene), the title strip (blurred, slightly transparent, top corners rounded) and the body (rounded corners, alpha), then any part the window drew outside its frame. While a window is dragged without resizing, only this step runs. Smooth scrolling keeps a snapshot of the scrolled area; for 140 ms the new contents slide in and the snapshot fills only the uncovered strip. Closing and minimising keep the layer as a fading ghost.

Fewer operations per frame (v9.71): text from anywhere in a window (any line, any colour) goes into one batch with a bounding box and is drawn with one masked operation when the window finishes, when the batch fills (4096 glyphs) or when another operation overlaps it. The staging area is rewound at the start of every frame. Each window's operations are recorded into a list (`gq_cap_begin` / `gq_cap_end`); text and pixel batches carry a key computed from their contents. The next frame's list is compared with the previous one: if the window did not move or resize, only the union of the operations that differ is cleared and redrawn (clipped), and an unchanged window is not drawn at all. If code inside the window touches the screen buffer directly, recording is abandoned and the window is redrawn in full for that frame. Shadows are drawn from a precomputed 9-piece shadow image (4 corners and 4 edge strips) with 8 masked operations; very large, very rounded or translucent windows use the old per-pixel shadow. The blurred glass behind a window is kept in a per-window cache keyed by a hash of every operation drawn into the scene before it and the window position; while the key is the same it is copied instead of blurred again. Wallpaper scaling now interpolates between pixels (bilinear); this runs once on the CPU when the wallpaper or screen size changes, not as a shader (a shader would need a per-pixel gather from computed addresses, which the existing block-read shaders do not do and which could not be checked against the hardware before release).

GPU clock (`igpu.c`): on Gemini Lake and Apollo Lake the allowed range is read from `RP_STATE_CAP` (0x138170, units of 16.67 MHz). When GPU drawing starts, the frequency request (`RPNSWREQ`, 0xA008) is set to the maximum; when drawing stops for any reason the original request is restored. `gpuinfo` shows the current frequency (`RPSTAT1`, 0xA01C), the requested frequency and the range.

Partial screen rebuild (v9.72, `gq_srec_begin` / `gq_srec_end` in `gq.c`): during a desktop frame every operation that writes the screen, or reads a buffer written that way, is recorded instead of run; operations on window layers still run at once. At the end of the frame the list is lined up with the previous frame's list (operation hashes, look-ahead of 96). A forward pass finds what changed per buffer: unmatched operations, and matched operations whose inputs changed (changes to layers and other buffers since they were last read are tracked, reads past an edge count as reads of the edge). The screen area to rebuild is the bounding rectangle of the changes plus anything written outside a frame. A backward pass marks which operations on scratch buffers are needed; any operation that reads the screen must have its whole read area inside the rebuilt area, which is grown until that holds, and later writes to a buffer whose earlier write was re-run are re-run too. Then the marked operations run in order, screen operations clipped to the area. Anything that reads or writes the screen with the CPU during the frame, a buffer that is changed after a recorded operation read it, or more than 4096 operations make the frame a full one. `gpuinfo` shows partial, full and unchanged frames, skipped frames, the last area and the reason for the last full rebuild.

Frame check (`gpu check`, also run once when the desktop starts with GPU drawing): interrupts are paused, the frame is drawn on the GPU with every screen operation logged, then drawn again by the CPU executor, and the two are compared (colour only). The result and, on a difference, the first wrong pixel and the operation that last wrote it are shown in `gpuinfo`; a second run with a wait between operations decides whether the slower mode is kept.

Frame breakdown in `gpuinfo`: CPU time spent recording the frame, time waiting for the GPU to finish drawing, time for the screen copy, submissions, operations, ordering waits, text operations and characters, windows redrawn (full, partly, unchanged, by the CPU, with the reason for the last full CPU-style redraw), blur cache hits and misses, fast and slow shadows.

Start-up (`q_init()` and `q_start()` in `igpu.c`): step 18 shaders assembled; 19 four test images (rows padded to 64 bytes); first fill, a read test (the GPU must see two different values the CPU wrote to the same place, so no stale GPU cache) and a write test (the CPU must see what the GPU drew); 20 the self-test (400 random operations, every pixel compared with the CPU reference; on a mismatch the batch is restored and rerun one operation per submission: if that is correct, drawing continues with a CS stall between every operation (`g_gq_serial`), otherwise the first wrong operation is reported in `gpuinfo`); 21 screen and display surfaces; 22 drawing buffers (wallpaper, cursor cache, two blur buffers, staging, mask cache); 23 the CPU, blitter and shader screen copies are timed and the fastest is used. `gpuinfo` shows the step and reason if it stops, the last frame time, operations, ordering waits and CPU waits, layers and their memory, and the raw fuse registers.

#### 10.10.5 NVIDIA (`drivers/video/nvidia/`)

None of this code runs at boot. It is reached only through the `gpu`/`nvidia`, `gpumon` and `gpuedid` commands.

| Implemented | Where |
|---|---|
| PCI probe for vendor 0x10DE class 03; BAR0/BAR1/BAR3 with 64-bit BAR1 handling; BAR0 size by sizing write; enable MEM+BM | `nv_probe` |
| `PMC_BOOT_0` read to architecture name (Fermi ... Ada) | `nv_read_id`, `nv_arch_name2` |
| VBIOS copy from PROM (BAR0+0x300000, up to 64 KiB) with the ROM shadow bit in 0x088050 cleared during the read | `nv_vbios_from_prom` |
| BIT signature search; DCB 4.x located by signature 0x4EDCBDCB; DCB entries to connector types | `nv_find_bit`, `nv_find_dcb`, `nv_parse_dcb` |
| Output list from the DCB plus connector table plus I2C table (type, heads, SOR, connector type, DDC port) | `nvmon.c: nvm_outputs` |
| Read-only state of the display engine already set up by firmware: heads from 0x610060/0x610074, per-head timings from 0x682064...+0x8000+h*0x400 to active mode and Hz, SOR to head protocol from 0x680300 | `gpu` command, `nvm_decode_head` |
| EDID by bit-banged DDC on the PMGR I2C port `0xD014 + port*0x20`, with clock stretching bounded at 2200 us | `nvm_read_edid`, `nvm_edid_parse` (`gpumon`) |

| Not implemented | Detail |
|---|---|
| Mode setting | `nv_modeset_scaffold()` pushes placeholder method numbers into a buffer that is never submitted. It and `g_nv_modeset_enable` are never referenced. `gpu` prints "modeset: disp907d scaffold present but disabled" |
| DisplayPort AUX | `gpumon` reports "DisplayPort monitors need the AUX channel - not supported yet" |
| Acceleration, memory management, firmware (GSP/falcon) loading | for Turing in v9.81: section 10.10.6 |
| Power management | absent |
| Older EDID path | `gpuedid` uses `nvdisp.c`'s bit-bang at the unverified register 0x00E138 and warns that it is experimental |
| `nv_init()` | defined but never called |

#### 10.10.6 NVIDIA GSP-RM for Turing (`nvflcn.c`, `nvmem.c`, `nvgsp.c`)

Added in v9.81. It follows the boot order of the Linux nouveau r535 code and uses NVIDIA's signed firmware 535.113.01 from linux-firmware (`assets/nvidia/`, xz-compressed and linked in by `build.sh`; files with the same names in Files, for example `booter_load-535.113.01.bin`, are used first). Nothing runs at boot; the `nvgsp` command drives it, writes each step to a log and saves the log as `nvgsp-log.txt` in Files.

| Command | What it does |
|---|---|
| `nvgsp` or `nvgsp status` | state, last step reached, the reason for a failure |
| `nvgsp vram` | finds the card, sizes the BARs, waits for GFW_BOOT, reads the VRAM size (0x100CE0) and the VGA workspace (0x625F04) and prints the WPR2 layout; touches nothing |
| `nvgsp boot` | steps 1-10 below: GSP-RM running, BAR2 and BAR1 working |
| `nvgsp objects` | steps 11-15: the objects and the copy-engine test |
| `nvgsp all` | boot, then objects |
| `nvgsp unload` | UNLOADING to GSP-RM, then booter_unload on SEC2, which clears WPR2 |
| `nvgsp log` | the log of the last command |

| Step | Work | Where |
|---|---|---|
| 1 | PCI probe (class 03, vendor 0x10DE), memory and bus master on, legacy interrupt off, Turing check from PMC_BOOT_0 | `probe` |
| 2 | GFW_BOOT wait, VRAM size, WPR2 layout: FRTS (1 MB below the VGA workspace), GSP bootloader, the GSP ELF, the GSP-RM heap, WPR2 and the non-WPR area under it | `nv_gfw_wait`, `nv_fb_layout` |
| 3 | GSP firmware: ELF sections `.fwimage` and the chip's signature section, radix3 page tables for the 24 MB image in system memory, LibOS arguments, the RM arguments, the log buffers and the shared message queues (command queue at 0x1000, message queue at 0x41000, 63 slots each, XOR checksum); SET_SYSTEM_INFO and SET_REGISTRY are queued before the boot | `prepare` |
| 4 | VBIOS ROM read (1 MB, IFR header skipped), FWSEC found through the BIT 'p' table (application 0x85), patched for FRTS (or SB) and run on the GSP falcon (0x110000) through `acr/bl.bin`; then WPR2 must be where FRTS was asked for | `nvb_find_fwsec`, `nv_run_fwsec` |
| 5 | booter_load on SEC2 (0x840000) with the WPR metadata address in the mailboxes | `nv_booter` |
| 6 | GSP RISC-V core running | `boot_gsp` |
| 7 | GSP_INIT_DONE event (the GSP sequencer events are run on the way) | `rpc_wait`, `seq_run` |
| 8 | GET_GSP_STATIC_INFO: the usable VRAM regions and BAR1 page directory base; the VRAM allocator gets those regions with the console framebuffer reserved | `vram_alloc_init` |
| 9 | BAR2: page tables in VRAM, UPDATE_BAR_PDE, write and read back a test page through the window | `bars` |
| 10 | BAR1: page tables under the base GSP-RM reports, the console framebuffer mapped 1:1, read back through the window | `bars` |
| 11 | FIFO device info: which runlists the GR engine and the copy engine use | `fifo_info` |
| 12 | Golden context: a first GR channel with COPY_SERVER_RESERVED_PDES and PROMOTE_CTX, as GSP-RM needs | `golden` |
| 13 | client (NV01_ROOT, 0x0000), device (0x0080), subdevice (0x2080), address space FERMI_VASPACE_A (0x90F1) with its page directory | `objects` |
| 14 | channels (TURING_CHANNEL_GPFIFO_A, 0xC46F) for GR and the copy engine, context buffers promoted, then `TURING_A` (0xC597), `TURING_COMPUTE_A` (0xC5C0) and `TURING_DMA_COPY_A` (0xC5B5) | `user_chan`, `objects` |
| 15 | copy-engine test: a push buffer with SET_OBJECT and LAUNCH_DMA copies a 4 KB pattern in VRAM, kicked through the work-submit token and the doorbell | `ce_test` |

Memory: `nvv_*` is an extent allocator over VRAM (256 free ranges, 1024 allocations, 4 KB granularity, never returns address 0, clears what it hands out). `nvm_*` builds GPU page tables in the version 2 format (PD3, PD2, PD1, dual PD0, 4 KB small page tables), joins an address space to an instance block (page directory at instance+0x200) and flushes the GPU TLB (0xB830A0). VRAM is reached through the PRAMIN window (0x1700, 0x700000).

Copy engine for other code (v9.82): `nvgsp_ce_ready()` says whether the channels exist; `nvgsp_ce_map(kva, len)` maps kernel memory into the GPU address space page by page through the CPU page tables (`kva_to_pa`, so it also works for pages `early_remap` moved); `nvgsp_ce_copy2d(src, src_pitch, dst, dst_pitch, bytes_per_line, lines)` pushes one pitched multi-line copy with a semaphore release and waits up to 200 ms for it. The Gears app uses it to put frames on the screen. The simulator checks a 300x170 copy into a pitched frame and that nothing outside it changes.

Limits: untested on a real card; only the simulator (`tools/tests/nvidia/nvsim.c`, `check.py nvidia`) has run it. The firmware has no TU117 booter, so a TU117 uses the TU116 one unless `booter_load-535.113.01.bin` for TU117 is put in Files. FWSEC cannot run twice: if WPR2 is still set from an earlier try the driver stops and asks for a full power-off. After `nvgsp boot` a monitor on the NVIDIA card may go dark, because GSP-RM takes over the display engine and there is no mode setting yet. No interrupts are used; every wait polls with a time limit.

### 10.11 CPU information (`arch/x86_64/cpu/cpuinfo.c`)

The file defines the `cpuid()` and `rdmsr()` inline wrappers that the rest of the kernel uses (`tp_is_amd`, `arch_fpu_init`, timers). `get_brand()` reads leaves 0x80000002-4 into `g_brand[52]` and strips leading spaces, or uses "x86-64 CPU" when the extended leaves are missing. It sets `g_lma` from IA32_EFER (MSR 0xC0000080) bit 10. The `cpu`/`cpuid` command prints these two values. The later `cores`/`cpu` command branch (SMP view) responds only to `cores`, because the earlier `seq(cmd,"cpu")` takes `cpu`. Frequency, cache and feature-flag reporting are not in this file. TSC/HPET calibration lives in `hrtimer.c` (`timers` command).

### 10.12 Summary of bounded waits

| Driver | Wait | Bound |
|---|---|---|
| PS/2 | controller IBF/OBF | 200000 `io_wait` per byte |
| DW I2C | enable/disable; transfer; bus idle | 100000; 400000 iterations; 100000 |
| I2C-HID | reset completion | 200x5 ms |
| xHCI | handoff; CNR/halt/reset; any event | 400000; 200000-400000; 400000 polls |
| USB hub | port reset | 100x5 ms |
| CMOS | update-in-progress | 100000 reads |
| ACPI | ACPI-mode switch; Global Lock; EC byte; whole AML batch | 1 s; 50 ms; 20000 `io_wait` plus a 5 s breaker; 15 s init / 500 ms per poll |
| AC'97 / HDA | codec ready; CRST and stream reset; verb | 200 ms; 10 ms; 200 ms |
| `/dev/dsp` | ring full | 2000x1 ms |
| NVIDIA DDC | SCL stretch | 2200 us per bit |

## Part 11: Desktop, applications, shell and scripting

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

### 11.1 UI states and the main loop

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

#### Presentation policy (init/main.c)

| Condition | Call | Cost |
|---|---|---|
| `ch` (an input handler reported a change) | `compose_present()` | full scene rebuild + full blit |
| ~0.5 s heartbeat (`g_ticks-last_frame>=50`) with DOOM open or a toast pending | `compose_present()` | full |
| ~0.5 s heartbeat otherwise | `present_topbar()` | 28-px strip only (clock/battery) |
| only the pointer moved, at frame rate | `present_cursor()` | two 44x44 rectangles |
| pointer moved while near the dock (`my > H-96`) or a menu/drag/launcher is open | `compose_present()` | full (dock magnification and menus follow the pointer) |
| `/dev/fb0` is open by a user program (`g_fb0_open>0`) | `blit_all()` on `g_fb0_dirty` | the compositor stands aside |

---

### 11.2 Window manager (`graphics/ui/wm.c`)

#### Fixed window table

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

#### Notifications and toasts

There are two separate mechanisms:

- Notifications. `notify(title, body)` writes into a 32-entry ring, `g_notif[NOTIF_MAX]`, and sets `g_toast_cur`. That toast is drawn on the next frame by `notif_toast_draw()`. The history panel (`notif_panel_draw`) opens from the bell in the top bar, or by tapping the Win key alone.
- Toasts. `toast(msg)` is a single-line status message (`toast_msg`, timestamp `toast_t`) drawn by `toast_draw()`. The main loop keeps redrawing for 300 ticks after one appears. Apps use it for short feedback such as "Saved", "Moved to Trash" and "Download complete".

---

### 11.3 Desktop shell and compositor (`graphics/ui/desktop.c`)

#### Scene buffers

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

#### Taskbar, start menu, workspaces

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

#### Tray and Quick Settings

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

#### Desktop icons, launcher, context menu

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

#### Themes and effects (`graphics/theme/theme.c`, `graphics/theme/effects.c`, `apps/system/settings.c`)

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

### 11.4 Input routing (`graphics/ui/input.c`, `process_input`)

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

### 11.5 Live terminal mode vs. desktop terminal

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

### 11.6 Setup (OOBE) and login (`core/init/setup.c`)

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

### 11.7 Applications (`apps/`)

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

#### Browser (`apps/internet/browser.c`, 698 lines)

Since v9.08 the Browser window is a front end for the web engine in `web/`, which is described in Part 13. `apps/internet/browser.c` keeps the window, the toolbar (back, reload, bookmark star, address bar, Go, Save, Home, Find, Cookies), up to 6 tabs, bookmarks and history (saved with `bm_save_fs` and `hist_save_fs`, history also pulled from the account by `nvx_hist_fetch`), downloads, the image viewer and the about: pages (`about:blank`, `about:js`, `about:cookies`, `about:console`, `about:history`, `about:bookmarks`). It decodes images for the engine (`nwk_image`: PNG, baseline JPEG, WebP, GIF, BMP, ICO) and routes mouse, wheel and keys into it (section 13.11).

`browser_go(push)` loads the page in `g_url`. Input that is not URL-like becomes a DuckDuckGo query; a URL without a scheme tries HTTPS first and falls back to HTTP; the back stack holds 16 entries. Network errors show an error page with the reason. The engine and its HTTP client are described in sections 13.2 and 13.9.

Account sync. The same file contains cloud-filesystem sync with the Noovex account: `nvx_file_put()` (base64 upload) and `nvx_files_pull()`.

#### Files

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

#### Editor and the `.nvx` app format

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

#### ChatGPT (`apps/internet/chatgpt.c`)

`cg_send()` builds `{"message":"...","source":"noovexos"}` with minimal escaping and calls:

```c
https_request(CG_HOST,"/api/public/chat","POST","Content-Type: application/json\r\n",body,bl,http_buf,...)
```

`CG_HOST` is a `*.lovable.app` preview host. The client takes `"reply"` (or `"error"`) from the JSON body with `json_str`. The conversation keeps the last 24 messages (`CG_MAXMSG`), each up to 400 characters, with role 0 = user, 1 = assistant, 2 = system note.

#### DOOM (`apps/games/doom.c`)

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

#### Gears (`apps/graphics/gpu3d.cpp`, `apps/graphics/r3d/r3d.cpp`)

Added in v9.82 and written in C++: a 3D demo like glxgears, under Start > Drivers & Hardware, or `gears` (`gpu3d`, `glgears`) in the terminal. `apps/graphics/r3d/r3d.cpp` is a software 3D renderer: vector and matrix classes, meshes built at start (glxgears-style gears, a torus knot, a sphere, a cube, a floor), near-plane clipping, a triangle rasterizer with edge functions, a depth buffer, perspective-correct normals and positions, Blinn-Phong light with a sky/ground ambient term, rim light, fog, planar shadows and a wireframe mode. `apps/graphics/gpu3d.cpp` is the window: it renders into a frame buffer, shows a large FPS counter and an information bar, and hands the frame to the screen in one of three ways:

| Path | When | How |
|---|---|---|
| NVIDIA copy engine | `nvgsp all` has run (press G in the window, or run it in the terminal) | the frame and the screen buffer are mapped into the GPU address space page by page (`nvgsp_ce_map`), and the copy engine copies the window rectangle line by line (`nvgsp_ce_copy2d`, a pitched multi-line `LAUNCH_DMA` with a semaphore the CPU waits for); after three failed copies it falls back to the CPU |
| Intel GPU blitter | the Intel iGPU compositor is on (`g_gfx`) | the frame is rendered straight into a GPU surface (`gfx_app_surf`) and blitted into the window with `gq_copy` |
| CPU copy | otherwise, and for half resolution (2x scaled) | rows are copied into the scene buffer |

Keys: 1 gears, 2 3 4 other scenes (showcase, materials, 192 cubes), W wireframe, S shadows, H half resolution, Space pause, arrows and + - for the camera, R reset, G start the NVIDIA card. Speed: a gears frame at 996x586 takes 8.7 ms on one core of the build machine (about 115 frames a second); in QEMU without hardware acceleration it is 1-2 frames a second.

#### Settings (`apps/system/settings.c`)

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

#### Small apps (`apps/accessories/small_apps.c`)

- Clock: an analog face drawn with the integer `isin`/`icos` and `clock_hand`.
- Task Manager (`sysmon_draw`): one row per open window. Each row shows a per-app memory figure from `tm_app_kb()`, which the source calls "representative" (for example Browser 149504 KB and DOOM 33792 KB). Seven background rows (`TM_BG_N`: System, Compositor, Input, Network stack, NVXFS, TLS engine, Heap manager) use fixed `TM_BG_KB` values. "End task" calls `close_win()` on the selected row.
- Calendar: `cal_dow`, `cal_dim`, month navigation with arrows or the buttons.
- Snake: a 21x15 grid with an xorshift RNG; step speed is set in `process_input`.
- Flappy Bird.
- neofetch: a graphical page that is separate from the text `neofetch()` in terminal.c.
- Paint: a 540x360 `paint_buf` with a 12-colour `PPAL`, three brush radii (2/4/8) and Clear.

#### Logs and Terminal Finder (`apps/system/logs.c`)

`logs_draw()` shows the kernel base/size, DMA arena, `CR3`, kernel `RSP`, tick count, and a live 8-row hexdump of memory at `0x00100000`. The dump offset advances with `g_ticks/30`.

`tfind_draw()` lists the `TF_CMD[]` pairs: wifi init/scan/wpa, wifi rx, wifi cryptotest, sig, crypto, run, spawn, coreutils, chmod, su/whoami/id, env/export, `ls /dev`, `cat /dev/random`, fatls/fatcat, gdisk/install, h2c/efuse/macinit.

---

### 11.8 The shell (`nsh`)

#### Line processing pipeline

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

#### Users, sudo and permissions

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

#### Environment variables

`sys/native/syscall.c` keeps `g_env[NENV=24]` entries, each with a 24-byte name and a 128-byte value.

- `env_init()` seeds `PATH=/bin HOME=/home PWD=/home USER=root SHELL=/bin/nsh TERM=xterm-256color XZ_DEFAULTS=--memlimit-compress=20MiB`.
- `env_set()` is called by `su`, `cd` (sets `PWD`), the `setuid` syscall (131), and login (`HOME`, `USER` in fs/unixfs.c).
- `env_pack()` serialises the table into a new process's `envp`.
- `env` prints the table.

The `export` built-in in `run_cmd_one` calls `env_set`. Note that an interactive `export X=1` never reaches it: `sh_run_line` strips `export ` first and stores `X` as a shell variable. `set` with no argument lists the shell variables.

#### Complete command reference

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

### 11.9 Scripting languages

#### JavaScript / C-script interpreter (`lib/js/js.c`)

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

#### Lua 5.5 (`third_party/lua/nvxlua.c`)

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

#### Rune (`lang/rune/`)

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

#### HexLang

There is no HexLang code in this repository. A case-insensitive search for "hexlang" in `*.c`/`*.h` under the repo root finds nothing. The 32-bit Noovex9 project has a `hexlang.h`, but it is not part of NoovexOS 64.

---

### 11.10 Kernel modules: `usr/mod/hello_mod.c` and the `.MOD` format

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

### 11.11 Fonts and assets (`assets/`)

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

### 11.12 Escape sequences, termios and full-screen programs

Since v8.97 the console understands the escape sequences of an xterm-style terminal, and programs can switch it to raw mode. Before, `jc_tty_out` dropped every escape sequence and carriage return, so colours vanished and full-screen programs could not draw.

#### Output: `vt_out` (`shell/vt.c`)

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

#### Input and termios

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

#### When a program ends

When the foreground job finishes or stops, `jc_tty_restore` resets the terminal: cooked mode with echo, default colours, cursor shown, scroll region and wrap reset, and the alternate screen left (with a full repaint). If the program left the cursor in the middle of a line that has text on it, the prompt starts on a new line. A program that crashes in raw mode therefore cannot leave the shell unusable.

#### Full-screen programs

`kilo` (a small editor, `third_party/kilo`) is built as a port and is a real-world test: it uses raw mode, `VMIN = 0, VTIME = 1` reads, the cursor position report to find the window size when `TIOCGWINSZ` is not available, cursor hiding, colours for syntax highlighting and reverse video for the status bar. `featest tty` checks the sequences and modes above automatically (section 12.2).

`TERM` is `xterm-256color`. Programs that write escape sequences directly work. ncurses-based programs need a terminfo entry; the image has no terminfo database, so build ncurses with `--with-fallbacks=xterm-256color` to compile the entry in.

### Task Manager and Settings (v9.73)

Task Manager (`apps/system/taskmgr.c`) has a left rail with Processes, Performance, Devices and Services. Performance samples once a second: CPU use is 100% minus the share of idle ticks counted by the scheduler on cores 1 and up (core 0 runs the desktop and is not sampled), memory is the kernel heap in use against installed memory, GPU busy is the GPU drawing plus screen copy time of the last frame against one refresh period. Devices builds its lists from the PCI scan, `igpu_info()`, the ACPI battery objects, `gdisk_*`, the USB device table and the sound driver; categories open on click and the list scrolls with the mouse wheel.

Settings (`apps/system/settings.c`) keeps the current page in `g_stpg`. Pages 0-8 are the category pages (tiles), higher numbers are sub-pages; `pg_parent()` gives each page its parent, which builds the path at the top and the back button. The previous per-category pages are drawn by `old_leaf()` and reused as sub-pages. `settings <page>` opens a page from the terminal.

## Part 12: Testing, quality gates, debugging and history

This part covers how NoovexOS 64 is tested and debugged: the automatic checker `tools/check.py`, the boot emulator `tools/emulate.py`, the host-side harnesses in `tools/tests/`, the serial log and crash screen in the kernel, the shell commands for diagnosis, running in a VM, a troubleshooting table, a glossary and a version history. Paths starting with `tools/`, the repository root or `boot/` are relative to `source/`; kernel paths without a prefix (`core/klog.c`, `fs/vfs/vfs.c`) are relative to the repository root.

---

### 12.1 Overview of the test approach

The test tools do not emulate a CPU (`tools/README.md`: "There is no CPU emulator here, so BIOS 16-bit code and the kernel itself are not executed instruction by instruction"). They use four techniques instead:

| Technique | How it works | Used by groups |
|---|---|---|
| Generated tests | `check.py` cuts functions out of the current driver source (with `defn()` or by `find()` markers), swaps hardware accessors for simulator calls with exact string replacements, adds a template from `tools/tests/*_template.txt`, then compiles and runs the result on the host | `usb`, `acpi`, `nvme`, `touchpad`, `memory`, `aml`, `timers`, `gpu` |
| Whole kernel as a host object | `kernel64.c` (every kernel unit included into one file, kept for this purpose) is compiled for Linux with host versions of `arch/x86_64/cpu/cpu.h` and `io.h`, then linked with a harness | `linux`, `fs` |
| Separately built modules | the `MODS` drivers (they include only `include/kapi.h` and `include/drivers.h`) are linked against a device simulator that implements `kapi_*` | `drivers` |
| Differential testing | the same program is built against nvxlibc and glibc, or nvxbox is compared with GNU coreutils, and the outputs must be identical | `libc`, `coreutils` |

Because the generated tests are rebuilt from the source on every run, a test breaks loudly when a driver is refactored: every group first records "... code located" or "test builds against the current ..." and stops if that fails.

---

### 12.2 `tools/check.py`, the automatic bug checker

Run from `source/`: `python3 tools/check.py [options]`. It needs Linux with gcc, binutils (`ld`, `nm`, `readelf`, `objdump`), Python 3 and GNU coreutils (`tools/README.md`); `analyze` also needs `clang-tidy`, `qemu` needs `qemu-system-x86_64`.

#### Command line (`main()`)

| Option | Effect |
|---|---|
| `--build` | runs group `build` first (rebuild kernel, libc, user programs, UEFI loader, BIOS loader and the ISO). Without it, `build` is skipped even if listed in `--only` |
| `--iso IMAGE` | image for `boot` and `qemu`; default `tools/out/check.iso` (the one `--build` writes) |
| `--quick` | smaller random sizes: `pfuzz` loop `i<100000` to `i<20000`, strtod inputs 30 000 to 8 000 |
| `--sanitize` | sets `SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g` for the generated driver tests and `drivers`, and `LIBC_SAN = -fsanitize=undefined -fsanitize-trap=all` for the host libc |
| `--only a,b,c` | comma list from `build,static,boot,usb,acpi,gpu,nvme,touchpad,memory,aml,timers,linux,fs,tls,fetch,qemu,net,drivers,analyze,libc,coreutils` |

Environment variables:

| Variable | Effect |
|---|---|
| `NVX_KEEP_WORK` | if set, the temporary work directory (`nvx_check_*`) with every generated `*_t.c`, executable and `build.log` is kept and printed ("work dir kept:") instead of deleted |
| `NVX_QEMU` | path to `qemu-system-x86_64` for group `qemu` (else `shutil.which`) |
| `NVX_QEMU_BIOS` | passed to QEMU as `-L <dir>` (firmware/ROM directory) |
| `NVX_NET_TAP` | `1` enables group `net` (needs root and `/dev/net/tun`) |
| `NVX_TAP_NAME` | tap interface name for `net` (default `nvxtap0`) |
| `NVX_TAP_CREATE` | `0` makes `netpeer.py` use an existing, already configured tap instead of creating one |

Groups run in a fixed order (build, static, boot, usb, acpi, gpu, nvme, touchpad, memory, aml, timers, linux, fs, tls, fetch, qemu, net, drivers, analyze, libc, coreutils). Each check is a `record(group, name, ok, detail)`; at the end the script prints `==== N of M checks passed in Ts ====`, lists every `FAIL [group] name: detail` and exits with 1 if anything failed. Note that `tools/README.md` still lists only the older subset of groups for `--only`; the authoritative list is the `argparse` help string.

Most generated tests print lines ending in ` ok` / ` FAIL` and a final `ALL PASS` or `FAILURES`; `check.py` counts those lines for the "(N checks)" in the summary and shows the first failing lines.

#### Group `build` (`g_build`)
1. `sh build.sh` in the repository root (timeout 30 min, log saved as `build.log`); passes only if the exit code is 0 and the output contains `signature stamped` (printed by the stamping step in `build.sh`).
2. "no implicit function declarations": any build-output line containing `implicit declaration` fails it.
3. Copies `build/kernel64.bin` to `boot/stage/kernel64.bin`, runs `make build/boot.bin build/stage2.bin` in `boot/`.
4. `mkhybrid.py build/boot.bin build/stage2.bin <iso> NOOVEXOS boot/kernel64.bin=... boot.cfg=... README.txt=...` must succeed (its own FAT32-size and FAT16-EFI-image asserts must hold).

#### Group `static` (`g_static`)
| Check | How |
|---|---|
| kernel signature | `emulate.signature_ok()` on `build/kernel64.bin` (see 12.3) |
| UEFI loader has no relocations | `readelf -r uefiboot.so` must say `no relocations` |
| `efi_main` takes SystemTable from RDX | regex over `objdump -d` of `efi_main` for `mov %rdx,...(%rip) <ST>` (Microsoft x64 ABI) |
| BOOTX64.EFI PE structure | `tests/pecheck.py` |
| GDT pointer in `uefi_entry` | `gdt64_check()`: disassembles 64 bytes at `uefi_entry`, finds the RIP-relative `lgdt`, reads the 10-byte pointer from `kernel64.bin`, requires the base to be a symbol starting with `gdt`, limit at least 0x17, and selector 0x08 to be a present 64-bit code segment (bits 47 and 53). Catches a 6-byte (32-bit) GDT pointer used in long mode |
| no raw keyboard-controller resets | regex `outb(0x64, 0xFE)` anywhere except `core/power.c` |
| no inline assembly outside `arch/` | regex `__asm__` / `asm(` in `.c`/`.h` outside `arch, third_party, user, build, mod, tools`; portable code must use `arch/x86_64/cpu/cpu.h` |
| zero strict warnings | `gcc` with the `CFLAGS` parsed from `build.sh` plus `STRICT` (`-Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wshift-negative-value -Wint-conversion -Wempty-body -Wmaybe-uninitialized -Warray-bounds`, with a few noisy warnings turned off) on `kernel64.c` and every file in `MODS`; any `warning:`/`error:` fails |
| no fixed regions inside `.bss` | `fixed_regions_in_bss()`: scans kernel `.c/.h` (not ntfs, musl, sqlite, zlib, bzip2, xz, zstd, user, wifi, doom), `boot64.S` and `boot/src/stage2.c` for page-aligned constants such as `(u8*)0x...` or `#define X 0x...` between `0xC06000` and `__bss_end` (from `nm kernel64.elf`); `stage2.c`'s `0x1000000` is whitelisted as transient (`STAGE2_TRANSIENT`) |
| no unbounded hardware polling loops | `unbounded_hw_loops()`: every `while(...)` whose condition calls `inb/inw/inl/kapi_in*/cmos/XMR` and has no `&&`/`||` (i.e. no counter or timeout) is reported with file:line |
| USB HID regions below the USB-stick buffers | parses `XHID_MAX`, `XHID_BASE(r)` and `MSD_INPUTCTX` from `drivers/usb/xhci.c` and requires `base + max*stride <= MSD_INPUTCTX` |

`build.sh` itself already turns real type errors into errors (`-Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -Werror=return-type -Werror=shift-negative-value -Werror=empty-body -Werror=duplicated-branches -Werror=logical-op`, `build.sh` line 6); `static` is the stricter second pass.

#### Group `analyze` (`g_analyze`)
Runs `clang-tidy` on `kernel64.c` and every module with `-m64 -ffreestanding -DNOOVEX64` and the checks `clang-analyzer-core.*`, `clang-analyzer-unix.Malloc`, `bugprone-sizeof-expression`, `bugprone-suspicious-memset-usage`, `bugprone-misplaced-widening-cast`, `bugprone-suspicious-string-compare`, `bugprone-infinite-loop`, `bugprone-too-small-loop-variable`. Any `warning:` or `error:` fails; the first 10 are printed. If `clang-tidy` is missing, a passing "skipped" record is written.

#### Group `boot` (`g_boot`)
Runs `tools/emulate.py <iso>` and records "emulate.py: N of M checks passed". Fails with "image ... exists (use --build or --iso)" if there is no image.

#### Group `usb` (`g_usb` + `g_xhci_init`)
- HID/hub test: extracts ~40 functions from `xhci.c` (`hid_pick`, `hid_parse`, `xh_interval`, `xhci_hid_enum`, `xhub_*`, `hid2ascii`, `kbd_report`, `usb_kbd_repeat`, `hid_report_decode`, `xhci_poll_one`, `xhid_remove_port`, `xhc_save/load/select`, ...), renames `xh_ctrl` to `real_xh_ctrl` so the template can intercept control transfers, and runs `usb_template.txt` against recorded devices (Logitech Unifying receiver, a vendor-first receiver, a USB stick, per `tools/README.md`). Covers receivers, routing, key repeat, key mapping, hot-unplug and hubs; also non-boot devices through their report descriptors.
- Controller set-up: copies `xhci.c` from `#define XHC_MAX` to `/* mouse state` plus the memory-map code of `mem/pmm_vmm.c`, built with UBSan; checks that every scratchpad page the simulated controller asks for is valid memory.

#### Group `acpi` (`g_acpi`)
All of `core/power.c` with `acpi_find_rsdp` replaced by a test RSDP, plus `acpi_template.txt`: S5 parsing, ACPI enable through SMI_CMD, the reset register, hardware-reduced sleep, and malformed tables.

#### Group `gpu` (`g_gpu`)
`tests/gpu_t.c` includes `drivers/video/nvidia/nvmon.c` verbatim (marker `#include "NVMON_C"`) and simulates the DDC port register (`nvr32`/`nvw32` with SCL/SDA lines, clock stretching) and an EDID EEPROM: DCB outputs, EDID over simulated I2C, head timings.

#### Group `nvidia` (`g_nvidia`)
`tests/nvidia/nvsim.c` includes `nvflcn.c`, `nvmem.c` and `nvgsp.c` and runs them against a simulated TU116 with 6 GB: registers, the PRAMIN window over a sparse VRAM, falcon IMEM/DMEM ports, a synthetic VBIOS with a BIT table and an FWSEC image, a GSP that checks the checksum and sequence number of every queue element and answers the RPCs, BAR1/BAR2 windows that walk the GPU page tables, and a copy engine that runs the push buffer (single copies, pitched multi-line copies and semaphore releases). The firmware comes from `assets/nvidia` (unpacked with Python) or from `NVX_NVFW`. 26 checks.

#### Group `nvme` (`g_nvme`)
The driver part of `drivers/storage/nvme.c` (from `#define NVME_ASQ` to `/* ---- generic disk layer`) against a simulated controller with UBSan: init, 4 KB sectors, queue limits, queue wrap, stray completions, flush, timeout recovery, namespaces.

#### Group `touchpad` (`g_touchpad`)
Concatenates `aml_scan.c`, `dw_i2c.c` (register accessors swapped for `sim_rd/sim_wr`), `tp_hid.c`, `tp_gesture.c`, `input.c`, `i2c_hid.c` (GPIO pin read swapped for `g_gpio_reg[]`), `elan_i2c.c`, parts of `ps2.c`/`ps2_mouse.c`, `elan_ps2.c` and `touchpad.c` with `tp_template.txt`: discovery from hand-made ACPI tables, I2C, HID over I2C, ELAN I2C with trackpoint, Elantech v3/v4 PS/2, gestures and fuzzing.

#### Group `memory` (`g_memory`)
The memory-map code of `mem/pmm_vmm.c` (without `mm_load`) and the heap of `mem/heap.c` (`typedef struct hblk` to `krealloc`) against an AMD-style firmware map: reserved/ACPI-NVS detection, DMA arena skipping, heap bounds, block headers.

#### Group `aml` (`g_aml`)
`drivers/acpi/aml.c` + selected `power.c` table helpers + `pci_cfg` + `acpi_dev.c` (SystemMemory access redirected to `sim_mem`). Data embedded as C arrays from `tests/aml/`: `calc.aml` with `calc.expected` (results recorded from ACPICA `acpiexec`; `ae_parse.py` parses that output), `lenovo.aml`, `hp.aml`, `hp_ssdt.aml`, `edge.aml`. The `.aml` files are compiled with `iasl` and committed (old doc section 21), so the checker does not need ACPICA. Checks interpreter results against ACPICA, laptop battery/AC/backlight/EC events, and corrupted tables.

#### Group `timers` (`g_timers`)
`arch/x86_64/time/hrtimer.c` with `hr_rdtsc`, `hr_cpuid`, `hr_rdmsr/wrmsr`, `hr_rd32/rd64/wr64`, `hr_irqsave/restore` and the LAPIC write replaced by simulator functions, plus `madt_apic_ids` from `smp.c`: HPET/PIT/CPUID calibration, 32-bit HPET wrap, x2APIC, APIC one-shot and periodic modes, MADT parsing.

#### Group `linux` (`g_linux`), see 12.4.1
Requires `build/musl/lib/libc.a`, `build/user/musltest.elf`, `build/user/mhello.elf` and the `build/user_*_bin.o` blobs ("run check.py --build first"). Records: kernel compiles as a host object; seccomp harness links; `lxfuzz` builds against musl; `musltest` passes on real Linux (the reference); then three harness runs with fuzz seeds 1, 2, 3 (`/tmp/lxfuzz 60000 <trace-off> <seed>`), each must end in `ALL PASS`.

#### Group `fs` (`g_fs`), see 12.4.2

#### Group `qemu` (`g_qemu`)
Silently skipped if no QEMU binary or no image. Starts:

```
qemu-system-x86_64 [-L $NVX_QEMU_BIOS] -m 1024 -smp 4 -accel tcg,thread=multi \
  -drive file=disk.img,format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide.0,unit=0 \
  -drive file=boot.img,format=raw,if=none,id=c0 -device ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0 \
  -serial file:serial.log -display none -no-reboot -monitor unix:mon,server,nowait
```

(`disk.img` is a blank 64 MB file, `boot.img` a copy of the ISO attached as a hard disk.) It then watches the serial log and types through the monitor's `sendkey`:
1. waits up to 120 s for `entering main loop`;
2. `smp on` to expects `smp: scheduling on all cores`;
3. `smptest 8 12` to expects `smptest: PASS` and `N CPUs used` with N at least 2;
4. `musltest >/dev/kmsg` to the output reaches the serial log through `/dev/kmsg`; expects `musltest: X passed, 0 ...`;
5. `featest >/dev/kmsg` to expects `featest: done N passed, 0 failed` with N at least 60 (descriptors, memory, timers, server sockets, threads on 4 cores, terminal; section 6.8), and prints any `featest: FAIL` lines;
6. `dyntest >/dev/kmsg` to expects `dyntest: done N passed, 0 failed` (dynamic linker, shared libc, `dlopen`);
7. `smp off` to expects `smp: secondary cores parked`.

#### Group `tls` (`g_tls`)
Builds `tests/tls/x509h.c` (the kernel's `nvxcrypto.c`, `nvxtls.c`, `nvxcrypto2.c`, `nvxx509.c` and `nvxroots.c` compiled for the host, with `--sanitize` under ASan+UBSan) and checks:
1. every embedded root's public key parses (120);
2. the self-signature of every certificate in `lib/crypto/roots/mozilla-cacert.pem` verifies with the kernel's RSA/ECDSA code (117 pass; the one P-521 root is expected to fail and 3 SHA-1/PSS roots are skipped);
3. `tests/tls/tlsvec.py` (needs the Python `cryptography` module) generates chains and signatures and runs 31 positive and negative cases (section 9.9.3).

#### Group `fetch` (`g_fetch`)
Tests the user-space TLS client on the host (needs the Python `cryptography` module). It compiles all of `third_party/bearssl/src` and links `usr/musl/fetch.c` and `nvxtls.c` with `-Wall` (any warning fails the check) and, with `--sanitize`, ASan+UBSan and leak checking. `tests/tls/fetchpeer.py` starts four TLS 1.2 servers on 127.0.0.1 with certificates from a fresh test CA (ECDSA for `localhost` and 127.0.0.1, RSA for the same names, ECDSA for `other.test` only, and an expired one) and writes the Mozilla roots in the `nvx-anchors.pem` format. The checks: the anchors file and the local CA load (121 anchors), ECDSA and RSA downloads with the body's SHA-256 matching, an IP address host, a redirect to a chunked body, an unknown root, a wrong name and an expired certificate refused with exit status 3, `-k`, HTTP 404 giving exit status 4, a bad URL giving 1, and no sanitizer or leak report in any run.

#### Group `net` (`g_net`)
Runs only when `NVX_NET_TAP=1` and QEMU is available. `tests/net/netpeer.py` creates the tap device `nvxtap0` with the QEMU NAT addresses (10.0.2.2 gateway, 10.0.2.3 DNS), generates a test CA with ECDSA and RSA server certificates for `noovex.test`, and serves DNS (every name to 10.0.2.2), HTTP on 80 and 8080 (with `/big`, a 300 KB pattern, `/redir`, a redirect to it, and `/chunked`, the pattern with chunked encoding), TCP echo on 7777, UDP echo on 7778, TLS 1.3 only on 443 (ECDSA) and 8443 (RSA), and TLS 1.2 on 9443 (ECDSA) and 9444 (RSA). It also tries to connect to 10.0.2.15:9200 every second and logs the answer. QEMU boots with `-netdev tap -device e1000`. The group then types:
1. `net` (DHCP times out, the static 10.0.2.15 is used), then `nettest >/dev/kmsg` to expects `nettest: PASS`;
2. `http noovex.test/` to the peer must log a request on port 80;
3. `https noovex.test/` to must fail with -11 (unknown root); `fetch -v ... https://noovex.test:9443/` must report at least 120 trust anchors read from the file the kernel wrote and refuse the unknown root;
4. writes the test CA to `/etc/ssl/local-ca.pem` with `echo ... >>` lines, then `https noovex.test/` and `https noovex.test:8443/` to `connected, certificate verified`;
5. `https 10.0.2.2/` to verified through the IP SAN; `https wrong.test/` to -12; `https -k wrong.test/` to connects `WITHOUT certificate checks`;
6. `fetch` with the test CA installed: ECDSA on 9443 and RSA on 9444 (`/chunked`) must say `certificate verified` and print the expected SHA-256, plain HTTP `/redir` must follow the redirect, `https://10.0.2.2:9443/` must verify through the IP SAN, `wrong.test` must be refused;
7. `featest listen >/dev/kmsg` to the peer's log must show `inbound pong from noovex`, i.e. a musl server accepted a connection that came in through the network card.

#### Group `drivers` (`g_drivers`)
Compiles each file in `MODS` (`build.sh` line 21: `drivers/net/ethernet/e1000.c rtl8139.c rtl8169.c drivers/virtio/virtio.c virtio_net.c virtio_blk.c drivers/sound/ac97.c hda.c`) for the host, links them with `tests/drivers/drvsim.c`, see 12.4.3.

#### Groups `libc` and `coreutils`
- `host_libc()` builds `usr/libc/src/{libc.c, printf.c, stdlib2.c, stdio2.c, sys_host.c, crt0_host.S}` with `-ffreestanding -nostdinc` against nvxlibc's own headers into `libc.a`; `sys_host.c` maps NoovexOS syscall numbers to Linux ones, so real nvxlibc programs run on Linux.
- `libc`: `tests/libc/pfuzz.c` (random printf formats and doubles; 100 000 iterations, 20 000 with `--quick`) and `ltest.c` (strtol/strtoul bases and edge values, strtod, sscanf, qsort, ctype, malloc, files, strftime, setjmp) are built once with glibc and once with nvxlibc; every output line and the exit code must match. `sfile.c` reads inputs generated by `gen_strtod_inputs()` (exact halfway points between adjacent doubles printed with up to 120 digits, random decimal strings, 300-700-digit denormal cases, hex floats, overflow/underflow and malformed strings) and prints the bit pattern and end offset; 0 differences required.
- `coreutils`: links `usr/src/nvxbox.c` + `coreutils.c` against the host libc and runs the 65-entry `COREUTILS` table (cat, wc, head, tail, grep, sort, cut, tr, uniq, seq, basename, dirname, rev, tac, nl, fold, paste, comm, cmp, md5sum, sha256sum, base64, find, yes, echo, true/false, mkdir/rmdir, cp, ln/readlink, tee, ls) in a fixture folder. stdout, exit status and "stderr empty or not" must equal GNU's (`sort` runs with `LC_ALL=C`).

---

#### Test templates and the memory map

Several generators paste driver code into a template that maps the driver's fixed buffers at the same addresses on the host (`tools/tests/*_template.txt`). When a fixed address moves, the template must move too. v9.12 updated `usb_template.txt` (xHCI memory `0x1090000`, second controller `0x10D0000`, HID memory below `0x1150000`, 16 queued reads per endpoint and room for 4096 logged doorbells), `xhci_init_template.txt` (`0x1090000`) and `nvme_template.txt` (`0x1230000`), and the USB generator now copies `XHID_INFLIGHT` and `xhid_bs`. The AML template allocates below 4 GiB from a fixed arena, because `anode_child` rejects node pointers above 4 GiB (a pointer check added in v9.07; host `malloc` returns higher addresses, so every lookup failed).

### 12.2a Typical workflows

```
cd source
python3 tools/check.py --build                 # everything, from a fresh build (needed once for linux/fs/qemu)
python3 tools/check.py --only static,analyze   # fast source checks, no image needed
python3 tools/check.py --only usb,touchpad --sanitize
NVX_KEEP_WORK=1 python3 tools/check.py --only nvme   # keep nvme_t.c and the binary for gdb
NVX_QEMU=/usr/bin/qemu-system-x86_64 python3 tools/check.py --only qemu --iso tools/out/check.iso
python3 tools/check.py --only libc,coreutils --quick
python3 tools/emulate.py tools/out/check.iso
```

Prerequisites per group, from the code:
- `static` reads `build/kernel64.elf`/`kernel64.bin` and `boot/uefi/uefiboot.so`/`BOOTX64.EFI` when they exist; the signature, relocation, PE, GDT and `.bss` checks need a previous build (`fixed_regions_in_bss` takes `__bss_end` from `nm`).
- `boot` and `qemu` need an image (`--build` or `--iso`).
- `linux` needs `build/musl` and the musl programs from `build.sh`.
- `coreutils` needs the host libc that `libc` builds; asking for `coreutils` alone still runs the `libc` build step (`want('libc') or want('coreutils')`).

#### Adding a test for a new driver

The existing groups show the pattern:
1. If the driver can be written against `include/kapi.h` only, add it to `MODS` in `build.sh`; it is then automatically compiled with strict warnings in `static`, analysed in `analyze` and compiled for the host in `drivers`. Add a simulated device and `rec(...)` checks to `tools/tests/drivers/drvsim.c`, ending with the existing `ALL PASS`/`FAILURES` summary.
2. Otherwise write a `g_<name>(work)` function in `check.py` that cuts the driver code out of its file by stable markers (`defn(src, name)` returns one `static` function by brace matching), replaces each hardware accessor with an exact `str.replace` guarded by `assert a in src` (as `g_touchpad` and `g_timers` do), prepends a `*_template.txt` `HDR` part with the simulator and appends a `TEST` part, compiles with `SAN`, and records "... builds against the current ..." and the result line.
3. Add the group name to the `--only` help string and call it from `main()`.
4. For code that needs the whole kernel (file system, syscalls), use the `fs`/`linux` approach: copy the source tree, swap in `cpu_host.h`/`io_host.h`, and add `hk_*`/`fk_*` accessor functions to the glue file.

#### Reading kernel sources from a generator (since v8.96)

The generators were written when every kernel file was one big translation unit. To keep them working with separate units, `check.py` has four helpers:

| Helper | Returns |
|---|---|
| `kread(path)` | The unit's source as the generators expect it: the unit's shared types, macros and inline helpers from its header first, then the source with the `include/nvx/` lines removed and `static` put back on every shared function and variable. `kread(path, hdr=False)` leaves out the header part. |
| `khdr(path)` | Only the header part: types, macros and inline helpers, without prototypes or `extern` lines. |
| `kfwd(text, own)` | `static` forward declarations for every shared function used in `text`, and for every shared variable that `text` both uses and defines. Generators that paste several units together put this after the headers, as the old single file had them. |
| `kmissing(code, template)` | `static` definitions for shared variables that the pasted code uses but neither it nor the test template defines (for example `g_gdisk_probed` in the ACPI test). |

`defn(src, name)` finds a function with or without `static`. When you move code between units, run `check.py --only usb,acpi,touchpad,memory,aml,timers,gpu,nvme` to see which generators need new markers.

---

### 12.3 `tools/emulate.py`, boot-path emulation

`python3 tools/emulate.py IMAGE.iso` prints sections and ends with `N of M checks passed`; exit 1 on failure.

BIOS (`emulate_bios`)
- MBR has `0x55AA` and non-empty boot code; stage2 at sector 1 is byte-identical to `boot/build/stage2.bin`.
- Copies the partition-selection block out of `boot/src/stage2.c` (from `    uint32_t part_lba = 0;` to `if(!part_lba) fail`) into `tests/bios_emu.c` (`@@PARTSEL@@`) and builds it with the real `fat.c`, `config.c`, `string.c` as a 32-bit freestanding program (`-m32 -march=i386 -mno-sse`); its `disk_read` uses raw `int $0x80` Linux i386 syscalls on the image.
- The program must print `PART`, `MOUNT ok`, parse `/boot.cfg` (`CFG entries=... default=...`), load the default kernel (`format=flat`).
- The loaded kernel needs a Multiboot header (`0x1BADB002`, checksum OK) in its first 8 KB and a valid build signature.

Build signature (`signature_ok`): finds `NVX64SIG`; the immutable-region SHA-256 is at +32, the full-file SHA-256 at +88. Both hash fields are zeroed before hashing; the immutable hash covers `__immutable_end - 0x100000` bytes and is only checked when `build/kernel64.elf` matches the image.

UEFI media (`extract`, `emulate_uefi`)
- CD path: El Torito catalog, platform `0xEF` bootable entry to FAT image; USB path: MBR partition type `0x0B/0x0C/0xEF` to FAT32. `tests/fatck.py` (`Fat` class) decides FAT12/16/32 by cluster count and checks FAT copies, backup boot sector, FSInfo signatures, FAT[0] media byte and cluster chains.
- Each path must contain `\EFI\BOOT\BOOTX64.EFI` (passing `pecheck`) and the kernel (`/KERNEL64.BIN` or `/boot/kernel64.bin`) identical to the BIOS-path kernel.
- `tests/pe_run.c` loads the real `BOOTX64.EFI` like firmware does and runs it with simulated UEFI services to the kernel entry ("all scenarios").
- `uefiboot.c` built with `-DUEFI_TEST` and linked with `tests/firmware_sim.c` runs firmware scenarios (missing kernel, RGB framebuffer, `ExitBootServices` retry, ...).

Kernel entry (`emulate_kernel_entry`): cuts the `uefi_entry` body from `boot64.S` (from `cli; cld; movl $2,%edi` to `5: movabsq $pml4`), wraps it as `run_entry` and runs it from `tests/entry_t.c` against test memory: BSS cleared, identity map 0-4 GB, framebuffer above 4 GB mapped, markers. `tests/mark_t.c` checks `uefi_mark` draws the progress squares exactly in place.

Other helpers: `tests/pecheck.py` (MZ/PE, x86-64, PE32+, subsystem 10, section alignment/overlap with headers, entry in an executable section, relocation blocks); `tests/isocheck.py ISO EFI KERNEL` (standalone: ISO9660 PVD, El Torito validation checksum, no-emulation entry at most 65 535 sectors, FAT12/16 CD image, FAT32 USB partition, byte-exact files; writes `/tmp/extracted_BOOTX64.EFI`).

---

### 12.4 Host harnesses in `tools/tests`

#### 12.4.1 `linux/`, the seccomp harness (`hh.c`, `hk_glue.c`, `cpu_host.h`, `io_host.h`, `lxfuzz.c`)

Build. `g_linux` copies the source tree (without `build/`, musl, doom, lua) to a work dir, replaces `arch/x86_64/cpu/cpu.h` with `cpu_host.h` and `io.h` with `io_host.h`, and compiles `hk_glue.c` (which is `#include "kernel64.c"` plus accessor functions) with the kernel's own flags. Kernel symbols that clash with libc (`malloc, free, memcpy, strlen, ..., panic`) are renamed with `-D<name>=kh_<name>`. `hh.c` is linked with this object and the embedded user-program blobs, with `--unresolved-symbols=ignore-all`.

Shims.
- `cpu_host.h`: interrupts on/off and halt are no-ops; `cpu_get_pt/cpu_set_pt/cpu_flush_page` call `h_get_pt/h_set_pt/h_flush_page` in the harness; FPU save/restore go to the signal context; `cpu_stop`/`cpu_triple_fault` call `h_fatal`; MSRs read 0; the per-CPU pointer is a plain variable.
- `io_host.h`: port I/O returns all-ones except a fake CMOS at ports 0x70/0x71 (fixed date/time).

Memory. `hh.c` creates a `memfd` ("nvxframes") the size of the frame pool and maps it at its real physical address `PF_BASE 0x0C000000`-`0x0E000000`; the heap is an anonymous map at `0x10000000` (64 MB). `hk_setup()` initialises per-CPU, heap, frame bitmap, fds, dirtree, env and VFS. When the kernel loads a user CR3 (`h_set_pt`), `sync_all()` walks the guest page tables at PML4 slot 8 and `mmap(MAP_FIXED|MAP_SHARED)`s every present guest page from the memfd at its user virtual address in the window `0x40000000000` + 1 GB (read-only if the PTE is not writable). So user programs run natively at their real addresses on the same physical frames the kernel manipulates.

Trapping syscalls. A seccomp BPF filter loads the upper 32 bits of the instruction pointer (`seccomp_data` offset 12) and returns `SECCOMP_RET_TRAP` when it is in `[0x400, 0x800)`, i.e. the syscall came from the user window `0x40000000000-0x80000000000`; everything else (the harness, the kernel code) is allowed. The `SIGSYS` handler `on_sys` copies the registers into a kernel-style frame and calls `syscall_dispatch` if `si_arch == 0x40000003` (i386 audit arch, i.e. legacy `int 0x80`) or `linux_syscall` otherwise, then writes the registers back. When the kernel switches to a process it calls `jc_resume`, which the harness implements with an `iretq` frame (`hh_iret`, CS 0x33/SS 0x2b) after restoring the FS base. `SIGSEGV/SIGBUS` inside the window first go to the kernel's demand-paging/COW handler (`hk_pf` to `vm_fault_user`); other guest faults are delivered through `jc_fault`. A fault with RIP outside the window is a harness crash (exit 91); `alarm(120)` gives `HARNESS TIMEOUT` (exit 92). `HH_DEBUG=1` prints guest faults with the last syscall and a 16-entry syscall-number ring.

What it checks (`main`): `mhello` prints `Hello from musl libc on NoovexOS 8.97` and exits 0; argv and environment reach musl; legacy `int 0x80` programs (`pipetest`, `libc8`, nvxbox `echo`, `seq`); gzip/bzip2/xz/zstd round-trips of a ~300 KB file and `zcat`; sqlite3 create/insert/query, persistence, `integrity_check`, ASCII table boxes; the DNS query builder refuses over-long host names without overrunning a guarded buffer (`hk_dns_build` to `dns_build_query`); `udp_send` with a 4000-byte payload (`hk_udp_big`); `smptest 3 3 200000`; `musltest` (each `FAIL` line becomes a failed check, at least 80 `ok` lines required); then `lxfuzz`; finally no leaked page frames, open-file slots or pipes, and a summary of syscalls, address-space switches and page faults.

`lxfuzz.c` (musl static-PIE): a forked child makes `iters` random syscalls (numbers 0-334 plus 436/439, minus a deny list such as exit, fork, execve, kill, mmap-type calls that would destroy the fuzzer) with arguments drawn from 16 classes: 0, -1, small ints, pointers into a buffer, pointers just before an unmapped page (`g_edge`), kernel addresses (`0x100000`, `0x0C000000`), a non-canonical address, path strings (`/dev/zero`, `..`, `/nope/x`, ...), `AT_FDCWD` (-100), huge values. Then the parent runs `mem_phase`: random `brk` growth/shrink (new memory must read zero), anonymous `mmap`/`munmap` with tag checks, `MAP_FIXED` remap of a hole, `malloc/realloc/free` with content checks, and pipe round-trips. Corruption prints a mask; success prints `lxfuzz done: ...`.

#### 12.4.2 `fs/`, NVX4 against a simulated disk (`fsh.c`, `fs_glue.c`)

`fs_glue.c` includes `kernel64.c` and exposes `fk_*` wrappers (`vfs_open/read/write`, `ux_unlink`, `k_rename`, `nvx3_sync_all`, ...). `fk_boot()` resets every in-memory table (`FS[]`, `DT[]`, links, `g_vf[]`, buffer cache) and forces `g_dktype = DK_VIRTIO`, so `disk_init()` mounts the disk through the VirtIO-blk entry points, which `fsh.c` implements on an anonymous memory "disk". `virtio_blk_write` honours a write budget: when it reaches 0 the "power is cut" (`g_cut`) and every later write and flush fails. `fk_bitmap_check()` rebuilds the free-block bitmap from inode extents and compares it with the kernel's (`bm_get`, `g_n4_free`), catching double use.

Tests (42 checks per the old doc): `t_format` (blank disk to NVX4, remount finds defaults), `t_basic` (sizes 0...3 MB, long names, hard/sym links, chmod, rename, truncate, append, unlink; all identical after remount; files loaded on demand), `t_many` (300 folders, 900 files, delete half, delete all to every block returned), `t_big` (40 MB file, 8 KB rewrite in the middle, eviction and reload), `t_full` (full disk returns an error, earlier files intact), `t_unix` (unlinked-but-open file, `O_EXCL`, rename over a file, `rmdir` of non-empty, sticky `/tmp`, `O_APPEND`, directory inode numbers), `t_metacopy` (damaged copy A falls back to B and is rewritten; both damaged to mount refused, nothing written), `t_crash` (150 rounds: record the sector writes of an operation, restore the snapshot, cut power at a random write; must always mount, keep old data, new file all-or-nothing), `t_migrate3` (synthesised NVX3 disk "v8.13-v8.90 format" converted, plus 40 power cuts during conversion), `t_migrate2` (NVX2 "pre-v8.13"), `t_foreign` (a disk with someone else's partition is never written; a type-0x7F partition is formatted inside its bounds).

#### 12.4.3 `drivers/drvsim.c`, simulated hardware for the `MODS` modules

Implements the `kapi_*` API: DMA pages from a 64 MB arena at `0x20000000` (any device access outside it aborts with "DMA outside arena"), a virtual clock (`kapi_ns`/`kapi_delay_us` advance `g_now` and run the device's `g_tick`), a one-device PCI space, port I/O routed to per-device callbacks, and a `wire[]` capture of transmitted frames. `SIM_LOG=1` prints `kapi_log` lines. Checks: with no device every probe returns 0; RTL8139 (TX slots, RX ring wrap), RTL8111/8168 (16-entry rings, CRC strip), VirtIO-net (legacy handshake negotiating only MAC+STATUS, header stripped, link down, buffer recycling), VirtIO-blk (FLUSH negotiated, random I/O split into 128-sector requests, read past end refused), AC97 (cold reset, VRA 48 kHz, 600 KB streamed in order, mute, dead codec given up within 0.5 s), HD Audio (CORB/RIRB, codec graph finds speaker and headphones, EAPD, stream format, 700 KB streamed, mute, dead codec within 2 s, no codecs refused), e1000 (EEPROM MAC, rings, BAR above 4 GB refused).

#### 12.4.4 Templates and other files

| File | Used by |
|---|---|
| `usb_template.txt`, `xhci_init_template.txt` | `usb` |
| `acpi_template.txt` | `acpi` |
| `aml_template.txt`, `aml/*.asl/.aml`, `calc.expected`, `ae_parse.py` | `aml` |
| `tp_template.txt` | `touchpad` |
| `nvme_template.txt` | `nvme` |
| `mm_template.txt` | `memory` |
| `hr_template.txt` | `timers` |
| `gpu_t.c` | `gpu` |
| `bios_emu.c`, `pe_run.c`, `firmware_sim.c`, `entry_t.c`, `mark_t.c`, `fatck.py`, `pecheck.py` | `emulate.py` |
| `isocheck.py` | standalone ISO checker |
| `libc/pfuzz.c`, `ltest.c`, `sfile.c` | `libc` |

Templates are split into named parts with `@@NAME@@` lines (`template()` in `check.py`).

#### 12.4.5 Harness exit codes and debug switches

| Harness | Code / switch | Meaning |
|---|---|---|
| `linux/hh.c` | exit 90 `HARNESS FATAL: <msg>` | `h_fatal`: setup failure (memfd, frame-pool or heap mmap, seccomp), guest page outside the 1 GB window, PTE outside the frame pool, `jc_fault` returned, `cpu_stop`/triple fault in kernel code |
| `linux/hh.c` | exit 91 `HARNESS CRASH: signal N at rip=...` | a fault whose RIP is not in the user window, i.e. a bug in the kernel code itself |
| `linux/hh.c` | exit 92 `HARNESS TIMEOUT` | the 120 s `alarm` fired |
| `linux/hh.c` | `HH_DEBUG=1` | print each guest fault with the last syscall and the syscall-number ring |
| `fs/fsh.c` | exit 90 `FATAL ...`, exit 2 `heap mmap failed` | `h_fatal` from kernel code; the 1 GB heap at `0x10000000` could not be mapped |
| `drivers/drvsim.c` | exit 3 `DMA outside arena ...` | a driver gave the device an address it never got from `kapi_dma_pages` |
| `drivers/drvsim.c` | `SIM_LOG=1` | show `kapi_log` output of the modules |
| all generated tests | last line `ALL PASS` / `FAILURES` | `check.py` requires exit 0 and `ALL PASS` |

---

#### 12.4.6 `tools/webhost.c`, the web engine on the host

The web engine (Part 13) builds for Linux unchanged except for `web/krt.c`, whose job glibc does. Compile every `web/*.c` file except `krt.c`, the QuickJS sources and `tools/webhost.c` with `gcc -O1 -g -D_GNU_SOURCE -I.` from the repository root and link with `-lm`. Then:

```
webhost page.html out.ppm [width] [height] [maxheight] [javascript]
```

`webhost` serves `file://` URLs for subresources, decodes no real images (it returns a gradient of the right size), keeps cookies and storage in memory, prints every fetch, console message and script error, and reports load and paint times. The optional JavaScript is evaluated after the page has loaded and 20 timer ticks have run; its result is printed and the page is painted afterwards, so a test can click buttons (`document.getElementById('x').click()`) or set input values and check the outcome in the picture.

### 12.5 The serial debug log (`core/klog.c`)

- `klog_init()` probes COM1 by writing `0xA5` to the scratch register (`0x3F8+7`) and reading it back; if it differs, logging stays off. Otherwise it programs 115200 baud (divisor 1), 8N1, FIFO on.
- `klog_c()` waits at most 100 000 polls for THR-empty (`LSR` bit 5); on timeout it sets `g_klog_ok = 0` so a dead UART never slows the system down again.
- `klog(s)` writes `[<g_ticks>] <s>\r\n` (ticks are 10 ms PIT ticks). `klog2(a, b)` joins two strings (max 159 chars).
- `g_klog_hook`: early in `kmain` it points at `bootlog_line()` (`core/init/main.c`), which also draws each log line on screen from y = 200 (32-bpp framebuffers only). It is cleared just before `splash_boot()`, so on-screen boot logging ends at "boot: splash start".
- `kapi_log()` (`sys/native/kapi.c`) forwards module logging to `klog`.
- `/dev/kmsg` (vfile device 13, `fs/vfs/vfs.c`): writes are split at newlines, non-printable bytes dropped, and each line goes to `klog`. So `prog > /dev/kmsg` sends a program's output to the serial port (the `qemu` group uses this for `musltest`; `usr/musl/smptest.c` writes its reports there).

Boot lines (in order, `core/init/main.c`): `boot: kmain entered, fb ok`, `boot: heap ok`, memory-map lines (`memory: firmware map received...`, `memory: WARNING the firmware uses memory inside a kernel area - run memmap`), `boot: frame pool ON (>=272MB RAM)` / `OFF`, `boot: pci scanned`, `boot: stopped DMA on devices...`, `boot: windows/fs/disk/vfs/term ok`, `boot: pic+pit ok`, timer lines (`timers: clock = ...`), `boot: no PS/2 controller...` or `boot: ps2 mouse ok`, `usb: ... xHCI controller(s) found`, `boot: xhci probe done`, `touchpad: ...`, `boot: touchpad probe done`, `acpi: ...`, `boot: ACPI AML done`, `boot: splash start/done`, `boot: integrity check done`, `boot: live_init start/done`, `boot: first frame presented`, possibly `boot: timer interrupts are not arriving - using a polled timer`, `smp: ...`, and finally `boot: interrupts on, entering main loop`.

Runtime lines. `cmd: <line>` for every live-terminal command (`shell/commands.c`), `live: job took the terminal` / `live: job finished, prompt back`, `nvx4: mounted, files=N` / `nvx4: mount failed: ...`, `rootfs: ...`, `ntfs: ...`, `usb: ...`, `acpi: power button pressed, shutting down`, `power: power off did not take effect`.

Heartbeat. Every 500 ticks (5 s) the main loop logs `alive: main loop running` (or `... (live)` in the live terminal). If the screen freezes but `alive:` lines continue, the main loop is fine and the problem is drawing or input; if they stop, the last line shows where it hung (old doc section 13).

The BIOS loader mirrors its own text to COM1 at 115200 8N1 too (`boot/README.md`).

---

### 12.6 Panic and the Red Screen of Death (`core/panic.c`)

`panic(regs_t* r, const char* msg)`:
1. `smp_halt_others()` sends the halt IPI (`IPI_HALT`, `0xF2`) to the other cores; interrupts off.
2. A second panic while panicking just halts (no triple fault). With no framebuffer it halts silently.
3. Paints a red gradient with ":(" and "NoovexOS ran into a problem and needs to stop."
4. With a register frame for an exception (`vector < 32`): `KERNEL PANIC - <name> (vector N)` from the `EXC[32]` table, `error=`, `RIP=`, `RSP=`, `RFLAGS=` and all 16 general registers. Without one: `KERNEL PANIC` and `msg`.
5. `blit_all()` and halt forever. It does not write to the serial log, so the last `klog` line before the RSOD is the last thing on COM1.

Who calls it: `isr_body` (`arch/x86_64/interrupts/interrupts.c`) for a CPU exception in kernel mode, after `vm_user_pf` had a chance to resolve a page fault; exceptions in user mode go to `jc_fault` instead, which turns the vector into a signal (SIGFPE 8, SIGILL 4, SIGTRAP 5, else SIGSEGV 11) and kills the process unless it has a handler. The shell command `panic` and Settings' crash test call it deliberately. Double faults run on their own IST stack and show the crash screen; NMIs are ignored (old doc, Linux ABI "Safety").

A separate "WRONG VERSION" screen exists in `core/init/main.c` for a failed build-signature check, but `g_sig_enforce = 0`: `sig_self_check()` only reports (see `kcheck`).

---

### 12.7 Diagnostic shell commands

All in `shell/commands.c` (`run_cmd_one`), some implemented elsewhere:

| Command | What it shows / does |
|---|---|
| `memmap` (`core/init/installer.c`) | firmware memory map (UEFI loader only, regions below 4 GB), each kernel area (boot info, kernel image, USB/disk buffers, page tables + .bss, DMA arena, page pool, module area, heap) with `ok`, `OVERLAPS <type>` or `skips`; heap placement; devices whose DMA was stopped; DMA arena and free frames |
| `memtest` (`core/init/installer.c`) | walks heap block headers (`HBLK_MAGIC`), then fills free DMA arena, page pool, module area and up to 16 x 16 MB heap blocks with an address pattern, re-reads, waits 3 s and reads again; reports words that did not hold and words "changed by something else within 3 s" (leftover device DMA) |
| `kcheck` (`core/init/installer.c`) | build version, size stamp, boot-image snapshot vs. its own hashes (code / whole file MATCHES/DIFFERS), and up to 12 code pages changed in memory since boot |
| `timers` / `timertest` | clock source, TSC frequency and reference used, HPET address/frequency/width, LAPIC timer rate and interrupt count, uptime by clock vs. ticks / 1-s cross-check of PIT, HPET and TSC (ppm), five 5-ms APIC one-shots, 250 us delay |
| `usbinfo`, `usb`/`xhci` | USB state; `usb` rescans all controllers then runs `usbinfo` |
| `tpinfo` (`drivers/input/touchpad/touchpad.c`), `tpinfo scan`, `tpdump` | ACPI touchpad candidates with I2C address, controller, speed and GPIO pin; DesignWare controllers; ELAN PS/2 state and packet counters; the touchpad's protocol, layout and last gesture, or the stage/abort code of the last failed try |
| `smp`, `smp on`, `smp off`, `smpstart` | per-core table; start/park the other cores (`/etc/smp`) |
| `cores`/`cpu`, `cpuid` | CPU brand and core information |
| `pci`, `disks`/`lsblk`, `mem`, `slabinfo`, `bcstat`/`bufcache` | PCI list, disks, memory, slab caches, buffer-cache hit rate |
| `netlog`, `net`, `dns`, `ping` | last 18 network log lines and network tools |
| `acpi ...`, `battery`, `brightness`, `gpu`, `gpumon` | ACPI namespace / EC, power, NVIDIA read-only probes |
| `ver`/`about` | version from `g_buildsig.version` |
| `panic` | deliberate RSOD |
| test programs | `utest`/`userspace`, `fdtest`/`posix`, `forktest`, `vmtest`, `nvxtest`, `smptest`, `musltest` |

There is no `logs` command; the kernel log only goes to COM1 (and to the boot screen until the splash).

---

### 12.8 Running NoovexOS in a VM

QEMU (tested configuration). The only scripted run is `g_qemu` (12.2): `-m 1024 -smp 4 -accel tcg,thread=multi`, the hybrid ISO attached as an IDE hard disk with `bootindex=0`, a blank raw disk on `ide.0`, `-serial file:serial.log`. For interactive use, replace `-display none` with a display and use `-serial stdio`. Notes from the code:
- Give at least 272 MB RAM, otherwise `pframe_init` turns the frame pool off and user programs cannot run (`boot: frame pool OFF (<272MB RAM)`).
- `-smp N` works; cores start automatically unless `/etc/smp` says `off` (12.10).
- `power_off` writes the QEMU (`0x604`), Bochs (`0xB004`) and VirtualBox (`0x4004`) shutdown ports after ACPI as a fallback (`core/power.c`).
- `NVX_QEMU_BIOS` adds `-L <dir>` for a custom firmware directory. The repository has no OVMF recipe; the UEFI path (`BOOTX64.EFI`, El Torito EFI entry and FAT32 ESP) is verified by `emulate.py`, not by a scripted QEMU UEFI boot. (Manual UEFI runs use `-machine q35` with OVMF as two `-drive if=pflash` entries; on q35 the disks sit on the ICH9 AHCI controller, which makes it the easiest way to exercise the AHCI driver.)
- Networking: QEMU builds without slirp have no user-mode network. Use a tap device (`-netdev tap,id=n0,ifname=nvxtap0,script=no,downscript=no -device e1000,netdev=n0`); `tools/tests/net/netpeer.py` sets one up together with test servers.
- Old doc section 11.2: "use the AHCI + xHCI + standard-VGA/virtio-gpu combination the drivers already handle"; VirtIO-net/blk are also probed (old doc v8.90).

VirtualBox. Boot the hybrid ISO as a CD (the El Torito EFI entry is the "CD/DVD/VirtualBox" path in `emulate.py`), or wrap a raw disk image as a VDI with `tools/mkvdi.py RAW VDI [MB]` (fixed VDI, attaches without `VBoxManage convert`). Serial log: Settings > Serial Ports > Port 1 > Enable, mode "Raw File" (old doc section 13).

---

### 12.9 Troubleshooting guide

| Symptom | Where to look |
|---|---|
| Black screen, no serial output | Loader stage: BIOS loader prints to COM1; for UEFI watch the bottom-left progress squares (1 red ... 6 white = `kmain` reached, old doc section 19). Run `emulate.py` on the image |
| Serial stops at a `boot:` line | The next step in `kmain` hangs; compare with the list in 12.5 |
| `boot: timer interrupts are not arriving - using a polled timer` | PIC/APIC routing; `pit_poll_tick` keeps time. Check `timers` |
| `alive:` continues but screen frozen | Drawing/input, not a hang (12.5) |
| Red screen with vector and RIP | `core/panic.c` output; map RIP with `nm build/kernel64.elf` / `objdump` |
| User program killed with SIGSEGV | `jc_fault`; on the host reproduce with the `linux` harness and `HH_DEBUG=1` |
| User programs do not start | `boot: frame pool OFF` (RAM < 272 MB) |
| `boot: INTEGRITY CHECK FAILED - kernel code was modified` right after the kernel grew | The boot stack or a fixed buffer overlaps the image. Dump memory from QEMU (`pmemsave 0x100000 <size>`) and compare with `build/kernel64.bin`; a changed band just below the stack top is the stack. The image must end below `0x900000` (section 13.12) |
| Browser shows "This page stopped working (engine error)" | The engine hit `nwe_fail` (arena full or an internal check). Render the page with `webhost` (section 12.4.6) to see the error; the next page load starts from a clean arena |
| Browser window opens but stays blank from the live terminal | Fixed in v9.08: `browse` switches to the desktop. On older versions run it from the desktop terminal |
| Random corruption on real hardware | `memmap` (OVERLAPS), `memtest` ("changed by something else" = DMA), `boot: stopped DMA on devices...` |
| `kcheck` shows DIFFERS / changed pages | Kernel image modified in memory after boot |
| Hang when starting cores | Next boot logs `smp: the last start did not finish - multi-core turned off`; use `smp off` / `smp on` |
| USB keyboard/mouse missing | `usbinfo`, serial `usb:` lines (firmware hand-over, scratchpad memory), `check.py --only usb` |
| Touchpad dead | `tpinfo`, `tpinfo scan`, serial `touchpad: ACPI lists an I2C touchpad but it did not answer` |
| Battery/backlight/power button | `acpi`, `acpi ec`, serial `acpi:` lines, `check.py --only aml,acpi` |
| Disk not mounted | serial `nvx4: mount failed: ...` (`g_n4_err`), `rootfs: ...`, `disks` |
| No network | `netlog` (also on serial as `net: ...`), `net`, `check.py --only drivers,net` |
| `https` fails with code -11 | The server's chain does not reach one of the 120 built-in roots. For your own CA, put its PEM in `/etc/ssl/local-ca.pem`; `https -k` skips the check for one request |
| `fetch` says "certificate chain ends at an unknown root" | As for `https -11`: the CA is not among the roots in `/etc/ssl/nvx-anchors.pem`; add it to `/etc/ssl/local-ca.pem`. `fetch -v` shows how many roots were loaded (at least 120) |
| `fetch` says "the server does not support TLS 1.0 to 1.2" | The server only speaks TLS 1.3, which BearSSL does not implement; the kernel's `https` command can reach it |
| A program in raw mode left the terminal strange | The shell resets it when the job ends (section 11.12); if a background job keeps changing it, bring it to the foreground and quit it |
| A threaded program hangs | `ps` shows each thread; a thread in `S` with a futex wait is waiting for a wake-up from another thread of the same process (futexes do not work between processes) |
| `https` fails with code -13 | The clock is wrong (check `date`) or the certificate has expired. With an unset clock (before 2024) dates are not checked at all |
| Boot stops at the skull "WRONG VERSION" | The kernel's code hash does not match its build signature (section 2.12). R restarts; C continues at your own risk |
| A `check.py` group fails with "code located" / "builds against the current ..." | The driver source was refactored and the extraction markers or replacement strings in `check.py` no longer match; update them |
| Hard-to-read generated-test failure | `NVX_KEEP_WORK=1` and inspect `*_t.c` in the kept work dir; add `--sanitize` |

---

### 12.10 Glossary of internal names

| Name | Meaning (file) |
|---|---|
| `g_jp[JMAX]` | process table, `struct jproc` (state, pid, pgid, sid, uid, cwd, cr3, brk, fds, signals, FS base, `runcpu`, FPU area); `JMAX` 256, one slot per thread (`core/proc/jobs.c`) |
| `S_FREE...S_ZOMB` | process states 0 free, 1 run, 2 wait, 3 sleep, 4 stop, 5 zombie |
| `g_jcur`, `g_js_running` | current process index and "a user process is running" (per-CPU macros) |
| `JS_BLOCK` (-1000) | return value of pipe/device reads and writes meaning "would block, reschedule" (`interrupts.c`, `jobs.c`, `unixfs.c`, `sound.c`) |
| `jc_*` | job control / scheduler (`jc_fault`, `jc_pick`, `jc_resume`, `jc_step`) |
| `g_proc[MAXPROC]` | older ring-3 demo scheduler (`core/proc/sched.c`, `MAXPROC` 6) |
| `FS[FS_N]` | in-memory files, `FsFile`, `FS_N` 1024, `FS_CAP` 1 GB (`fs/vfs/ramfs.c`) |
| `DT[DN_MAX]` | directories, `DNode`, `DN_MAX` 512 (`fs/vfs/dirtree.c`) |
| `g_lk[NLINKS]` | extra hard links, 256 (`fs/vfs/unixfs.c`) |
| `g_vf[VMAXOPEN]` | open-file (vfile) slots, 1024 (`fs/vfs/ramfs.c`) |
| NVX2 / NVX3 / NVX4 | on-disk formats: before v8.13, v8.13-v8.90, from v8.91 (`fs/nvx/nvx4.c`) |
| `g_n4`, `g_n4_err`, `g_nvx_note` | NVX4 mounted flag, last error, mount/convert note |
| BKL, `g_bkl` | big kernel lock: recursive ticket lock `bkl_lock/unlock/drop/take/yield` (`arch/x86_64/cpu/percpu.c`) |
| `struct pcpu`, `g_cpu[NCPU]`, `PCPU` | per-CPU state, `NCPU` 16, pointer in GS base |
| `PF_BASE/PF_TOP` | page-frame pool `0x0C000000-0x0E000000` (`mem/pmm_vmm.c`) |
| `DMA_ARENA_BASE` | `0x0B000000` |
| `PTE_LAZY` / `PTE_COW` | software PTE bits 0x400 / 0x200 |
| `HBLK_MAGIC` | heap block header magic `0x4E56484B` (`mem/heap.c`) |
| `HR_LAPIC_VEC` | 0x40, local APIC timer vector |
| `g_ticks` | 100 Hz PIT tick counter |
| `g_state`, `ST_*` | top-level state machine (`ST_LIVE`, `ST_DESKTOP`, ...) |
| `FBB`, `SCENE`, `W/H/PITCHB` | framebuffer, off-screen scene buffer, geometry |
| `NVX64SIG` | build-signature block checked by `sig_self_check`, `kcheck`, `emulate.py` |
| `MODS` | separately compiled driver modules (`build.sh`) |
| `kapi_*` | module API (`include/kapi.h`, `sys/native/kapi.c`) |
| `hk_*` / `fk_*` | host accessors in `hk_glue.c` / `fs_glue.c` |
| `xhc_t`, `xhid_*`, `xhub_*` | xHCI controller state, HID entries, hub code |

---

### 12.11 Version history

Compiled from version tags in the old `ARCHITECTURE.md` and in the sources. Only versions with evidence are listed; the current version string is `9.29` (`core/buildsig.c`, `fs/vfs/vfs.c` `/proc/version`, `sys/linux/linux.c` `uname`, `/sys/kernel/osrelease`).

| Version | Evidence | Change |
|---|---|---|
| v8.08 | `usr/libc/src/legacy_posix.h` section headers | nvxlibc: more math, file/path helpers, crypto, general utilities |
| v8.13 | `fs/vfs/dirtree.c` comment; old doc NVX4 "Conversion" | path-aware kernel file-management helpers; on-disk format NVX3 begins (NVX2 before) |
| v8.38 | `sys/native/syscall.c` comment, `shell/terminal.c` | identity, real-time and file-management syscalls |
| (undated) | old doc "Linux system-call ABI and musl libc" | Linux `syscall` ABI and musl 1.2.6 static-PIE programs (`mhello`, `musltest`); already present when the v8.88 items below were added. No v8.87 tag exists in the old doc or code |
| v8.88 | old doc, same section | file permissions via `perm_file`/`perm_dir`; `fs_drop_name`/`fs_slot_freed` deletion; name length limits and no "file name anywhere" lookup |
| (undated) | old doc "Ported programs" | sqlite3 3.53.4, zlib 1.3.2, bzip2 1.0.8, XZ 5.8.4, zstd 1.5.7 via `tools/mcc`. No v8.89 tag found |
| v8.90 | old doc sections | demand paging + copy-on-write; kernel modules and driver API (`kapi.h`, `MODS`, `drvsim`); e1000/RTL8111/RTL8139/VirtIO drivers; HD Audio/AC97 and `/dev/dsp` |
| v8.91 | old doc "NVX4 file system" | NVX4 on-disk format (extents, two CRC'd metadata copies, power-cut safety), growable files up to 1 GB, Unix semantics, NVX2/NVX3 conversion, `fs` checker group |
| v8.92 | old doc "Multi-core (SMP) scheduling" | per-CPU state, big kernel lock, scheduling on up to 16 cores, IPIs 0xF0-0xF2, `/dev/kmsg`, tmpfs-like `/tmp`, background writeback, 64-process table, `smptest`, `qemu` group |
| v8.93 | old doc "Since v8.93..." | cores start automatically at boot (`smp_boot_auto`), `/etc/smp` on/off/starting fallback |
| v8.94 | `core/buildsig.c` and other version strings; `hh.c` | The `linux` harness checks that the DNS query builder refuses over-long host names without overrunning its buffer and that `udp_send` refuses a payload larger than one Ethernet frame; the sources do not tag these fixes with a version number |
| v8.95 | this document, "What changed in v8.95" | Installer works again with a 5 MB kernel; `disks` no longer breaks AHCI; integrity check blocks the boot; Linux sockets (TCP/UDP, DNS); sockets and HTTPS over Wi-Fi; TLS certificate verification with 120 roots; Disk Manager reachable; TLB shootdown and idle-core wake-up in use; ARP responder; TCP MSS/window; shell `>>`/attached redirection fixes; `tls` and `net` checker groups |
| v8.96 | this document, "What changed in v8.96" | The kernel is split into 104 separately compiled units with interface headers under `include/nvx/`; incremental parallel builds with `tools/kbuild.py`; code-bearing `.h` files renamed to `.c` |
| v8.97 | this document, "What changed in v8.97" | Threads and futexes, dynamic linking with the shared musl, an escape-sequence terminal with termios, interval timers, limits that scale with RAM, server sockets and loopback, BearSSL and `fetch` for HTTPS in programs, compressed embedded programs; `featest`, `dyntest` and the `fetch` checker group |
| v8.98 to v9.07 | this document, "What changed in v8.98 to v9.07" | UEFI loader stops firmware DMA before copying the kernel; AHCI/NVMe bus-master order; 16 queued USB HID reads; classic taskbar, start menu and window frames; better CSS and images in the old browser |
| v9.08 | this document, "What changed in v9.08" | Web engine in `web/` with QuickJS, the cookie jar in `net/http/cookies.c`, the `qjs`, `browse` and `desktop` commands, boot stack moved to `0x980000` and the kernel limit raised to 8 MiB (Part 13) |
| v9.09 | this document, "What changed in v9.09" | Fix for a heap overflow in the web engine allocator that crashed the kernel in QuickJS |
| v9.10 | this document, "What changed in v9.10" | contenteditable editing, Selection/Range, MutationObserver, Streams, oklch and other colour functions, CSS nesting, auto-margin centring fix, ellipsis, line-clamp, sticky |
| v9.11 | this document, "What changed in v9.11" | FFmpeg and the `play` command, kernel image limit raised to 15 MiB, HD Audio response fix |
| v9.13 | this document, "What changed in v9.13" | 2D transforms, CSS animations and transitions, web fonts (TTF, WOFF, WOFF2 with a Brotli decoder, ligature icon fonts), Shadow DOM, faster style system |
| v9.12 | this document, "What changed in v9.12" | Media service and `/dev/media`, Video Player window, `<video>`/`<audio>` and MediaSource in the browser, binary-safe `fetch`/`XHR`/`Blob`, cheaper desktop redraws, checker templates updated |
| v9.14 | this document, "What changed in v9.14" | RAM up to 128 GiB (memory above 4 GiB mapped and pooled, E820 on BIOS boots), partition scanner for every disk, web engine crash containment, YouTube player page |
| v9.15 | this document, "What changed in v9.15" | Real youtube.com is loaded again, `Intl`, larger script time, heap and stack limits |
| v9.16 | this document, "What changed in v9.16" | The pointer keeps moving while the browser works, Esc stops a stuck page, timer budget, `data:` addresses, more script APIs |
| v9.17 | this document, "What changed in v9.17" | Layout on demand for scripts, crash counter reset per page |
| v9.18 | this document, "What changed in v9.18" | SVG text, `color-mix()` alpha, `min()` in grid tracks, `getComputedStyle` during load |
| v9.19 | this document, "What changed in v9.19" | `grid-template-areas`, `scrollHeight`, Google-style new tab page |
| v9.20 | this document, "What changed in v9.20" | Forced-layout budget (10x faster on measuring pages), input sizes, Enter as code 13 |
| v9.21 | this document, "What changed in v9.21" | Script time limits back to 6 s with an 8 s page total, load time report |
| v9.29 | this document, "What changed in v9.29" | current version. apt: all Debian components, update hint, bigger install plan |
| v9.28 | this document, "What changed in v9.28" | X apps on the desktop, app icons, .py opens in IDLE |
| v9.27 | this document, "What changed in v9.27" | Tkinter, Tk 8.6, IDLE, window manager |
| v9.26 | this document, "What changed in v9.26" | Keyboard and mouse for X programs, full-screen X session |
| v9.25 | this document, "What changed in v9.25" | X.Org X server and the X Display window |
| v9.24 | this document, "What changed in v9.24" | Python IDE package in the start menu |
| v9.23 | this document, "What changed in v9.23" | CPython 3.13, apt and dpkg for Debian packages, layered sysroot |
| v9.22 | this document, "What changed in v9.22" | glibc 2.44 and GNU tools, 100 MiB kernel limit |

## Part 13: The web engine, JavaScript and cookies

Since v9.08 the browser is a real layout engine with a full JavaScript runtime. This part describes it: how it is built and linked into the kernel, how the kernel talks to it, the HTML parser and DOM, the CSS cascade, layout (block, inline, floats, positioning, tables, flexbox and grid), painting and fonts, the QuickJS bindings and the JavaScript prelude, networking and the cookie jar, the browser window around it, the `qjs` command, and how to test and extend it.

Paths are relative to the repository root. The engine lives in `web/`; the kernel side is `apps/internet/webglue.c`, `apps/internet/browser.c` and `net/http/cookies.c`.

| File | Lines | Contents |
|---|---|---|
| `include/nvx/apps/nwe_api.h` | 77 | The only interface between kernel and engine: op codes, `nwe_req`, `nwk_resp`, the `nwk_*` callbacks |
| `web/nwe.h` | 314 | Engine types: `node`, `style`, `box`, `frag`, images, the global document `G` |
| `web/krt.c` | 386 | Freestanding C runtime for the engine and QuickJS: arena `malloc`, `printf` family, `strtod`, time, stubs |
| `web/util.c` | 246 | Pools, string buffers, UTF-8, URL resolution, merge sort |
| `web/dom.c` | 659 | Tag table, nodes and attributes, the HTML parser, entities, serializer |
| `web/css.h`, `web/cssparse.c` | 66, 876 | Selector structures and matching, style sheet parser, `@media` and friends, rule buckets |
| `web/cssstyle.c` | 1803 | Property values, the cascade, inheritance, `var()`, `calc()`, shorthands, the UA style sheet |
| `web/layout.h`, `web/layout.c` | 29, 1539 | Box tree, block and inline formatting, floats, positioning, hit testing |
| `web/layout2.c` | 800 | Intrinsic sizes, flexbox, grid, tables |
| `web/paint.c` | 836 | Painting, stacking, clipping, borders, shadows, gradients, form controls |
| `web/svg.c` | 444 | Inline and image SVG |
| `web/font.c`, `web/fontdata.c` | 275, 31 (generated, about 850 KB of source) | TrueType outline rasterizer and the embedded Liberation fonts |
| `web/js.c` | 1274 | QuickJS runtime, DOM bindings, timers, module loader |
| `web/prelude.js` (to `web/prelude_gen.c`) | 895 | The browser API written in JavaScript on top of the bindings |
| `web/engine.c` | 1209 | Images, scripts, navigation, forms, focus and editing, events, `nwe_entry` |
| `apps/internet/webglue.c` | 289 | Engine stack, HTTP client `nwk_fetch`, the `nwk_*` callbacks, console ring |
| `net/http/cookies.c` | 454 | Cookie jar and `localStorage`/`sessionStorage`, both saved to disk |
| `apps/internet/browser.c` | 698 | The Browser window: tabs, toolbar, about: pages, input routing, image decoding for the engine |
| `third_party/quickjs/` | | QuickJS-ng, unmodified |
| `tools/mkwebfont.py`, `tools/js2c.py`, `tools/webhost.c` | | Font converter, prelude embedder, host test harness |

---

### 13.1 Build and link

`build.sh` has a `[web]` step that runs before the Doom step:

1. `tools/js2c.py web/prelude.js web/prelude_gen.c nwe_prelude` turns the prelude into a C string.
2. The QuickJS sources (`quickjs.c`, `libregexp.c`, `libunicode.c`, `dtoa.c` and friends) are compiled once into `build/web/` and reused while they are unchanged.
3. The double and float functions of musl's `libm` are compiled into `libmk.a` (the `long double` variants ending in `l` are skipped).
4. The engine files are compiled with the `WEBF` flags: freestanding, `-O2`, SSE allowed, musl headers for the C library declarations.
5. Everything is combined with `ld -r` into one object, and `objcopy --keep-global-symbol=nwe_entry` hides every other symbol. The result is `build/web_all.o` (about 2.4 MB). The step prints any symbol that is still undefined, so a missing runtime function shows up at build time.

Because only `nwe_entry` is exported, the engine cannot clash with kernel names (QuickJS defines `free`, `malloc`, `printf` and many others) and the kernel can only reach it through one door.

The kernel image grew from about 4.3 MB to about 6.7 MB. That needed a new memory layout (section 13.12).

### 13.2 The kernel interface (`nwe_api.h`)

The kernel calls `long nwe_entry(int op, nwe_req* r)`. The ops are:

| Op | Value | What it does |
|---|---|---|
| `NWE_OP_INIT` | 1 | One-time setup: arena, fonts, QuickJS runtime |
| `NWE_OP_LOAD` | 2 | Parse and load a document (`url`, `data`, `len`, viewport `w`/`h`; `btn=1` means plain text) |
| `NWE_OP_LAYOUT` | 3 | Re-layout for a new viewport size |
| `NWE_OP_PAINT` | 4 | Paint into a framebuffer rectangle (`fb`, `stride`, `fbw`, `fbh`, `x`, `y`, `w`, `h`) |
| `NWE_OP_SCROLL` | 5 | Scroll by `y` pixels |
| `NWE_OP_MOVE` | 6 | Mouse moved (hover, cursor shape in `cursor`, link in `href`) |
| `NWE_OP_DOWN`, `NWE_OP_UP` | 7, 8 | Mouse button; a click is a down and an up |
| `NWE_OP_KEY` | 9 | Key press (`key`; arrows and editing keys are `NWE_K_UP` = 19 ... `NWE_K_END` = 26) |
| `NWE_OP_TICK` | 10 | Run due timers, animation frames, network callbacks, pending images |
| `NWE_OP_FIND` | 11 | Find in page |
| `NWE_OP_EVAL` | 12 | Evaluate a JavaScript expression; the result is in `out` |
| `NWE_OP_INFO` | 13 | Fill in title, scroll position and document height |
| `NWE_OP_WHEEL` | 14 | Mouse wheel at `x`,`y` by `key` pixels; scrolls the innermost scrollable box first |

Every op returns the page title, scroll position, document height (`doch`), viewport height and a `dirty` flag. When script or a click wants to navigate, `nav`, `navmethod`, `navbody` and `navblen` are set and the kernel performs the request (the engine never fetches a top-level page itself).

The engine calls back into the kernel only through `nwk_*` functions: `nwk_fetch`, `nwk_free`, `nwk_image` (decode PNG, JPEG, WebP, GIF, BMP or ICO to ARGB), `nwk_cookie_get`/`nwk_cookie_set`, the `nwk_store_*` storage functions, `nwk_log`, `nwk_status`, `nwk_alert`, `nwk_ms`, `nwk_epoch_ms`, `nwk_tz_min`, `nwk_pump` (keeps the network stack running during long fetches) and `nwk_alloc_arena`.

#### The engine stack

`web_call(op, r)` in `apps/internet/webglue.c` runs every op on a separate 2 MiB stack through the assembly trampoline `nwe_call_stack` (`arch/x86_64/user/stack_call.c`). QuickJS and the recursive layout code need more stack than the kernel's boot stack can spare. A guard (`g_in_engine`) refuses re-entry: if a callback such as a fetch pumps the GUI and something tries to call the engine again, `web_busy()` is true and the browser draws "Loading..." instead.

#### Failure handling

`nwe_entry` sets a recovery point with `__builtin_setjmp`. `abort()`, an arena that is completely full, or an internal check calls `nwe_fail`, which jumps back and makes the op return -1. The browser then shows "This page stopped working (engine error)" and the next load starts from a clean arena. The rest of the system is not affected.

### 13.3 Memory

All engine and JavaScript memory comes from one arena that `nwk_alloc_arena` takes from the kernel heap at INIT. It tries 160 MiB, then 96, 64 and 32 MiB. `web/krt.c` implements `malloc`, `free`, `realloc`, `calloc` and `malloc_usable_size` on it: size classes from 16 bytes to 60,592 bytes with free lists, and a list for larger blocks (page-rounded, reused when a free block is at most twice the request). Before v9.09 requests between the largest class and 64 KiB were given the largest class and overflowed it.

The whole arena is reset on every page load, so nothing a page allocates can leak into the next one.

Within a page, three scratch pools are reset on every pass:

| Pool (`g_pool`) | Reset by | Holds |
|---|---|---|
| 1 | each restyle (`style_all`) | computed styles |
| 2 | each layout (`layout_all`) | boxes, line fragments |
| 3 | each paint (`paint_all`) | paint lists and temporary buffers |

`nfree` ignores pointers that belong to a pool. Anything that must survive a pass (font and glyph caches, decoded images, parsed inline `style` declarations, rebuilt style sheets) is allocated between `pool_suspend()` and `pool_resume()`.

QuickJS gets a memory limit of 60% of the arena (`JS_SetMemoryLimit`) and an 800 KiB stack limit. A page that runs out gets a JavaScript `InternalError`, not a crash.

### 13.4 HTML parsing and the DOM (`web/dom.c`)

The parser is a single-pass tokenizer with the tree-building rules that real pages rely on:

- implied `html`, `head` and `body` elements, and implied end tags (`p`, `li`, `dt`/`dd`, `option`, table parts);
- foster parenting of stray content in tables;
- raw text for `script`, `style`, `textarea` and `title`;
- named and numeric character references;
- `document.write` during parsing (output goes into `G.wbuf` and is parsed in place).

Nodes (`node` in `web/nwe.h`) carry the tag, attributes, children, a pointer to the JavaScript wrapper (`jsobj`), form state (`value`, `caret`, `checked`, `selidx`), a decoded image and the scroll offset of scrollable boxes. `parse_fragment` (used by `innerHTML`, `insertAdjacentHTML` and `outerHTML`) parses into a holder element of the right context, so table fragments work. `serialize` and `clone_node` implement `innerHTML`/`outerHTML` reads and `cloneNode`.

### 13.5 CSS (`web/cssparse.c`, `web/cssstyle.c`)

#### Selectors and rules

- Compound selectors with type, `#id`, `.class` and `*`; the combinators descendant, `>`, `+` and `~`.
- Attribute selectors: `[a]`, `=`, `~=`, `|=`, `^=`, `$=`, `*=`, with the `i` flag.
- Pseudo-classes: `:hover`, `:focus`, `:focus-within`, `:active`, `:checked`, `:disabled`, `:enabled`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`, `:first-of-type`, `:last-of-type`, `:empty`, `:root`, `:link`, `:visited`, `:not()`, `:is()`, `:where()`, `:has()`, and more. `::before` and `::after` generate content; `::marker` and `::placeholder` are styled.
- At-rules: `@media` (width, height, orientation, `prefers-color-scheme` (light), and the usual `and`/`not`/`,` logic), `@supports`, `@import`, `@layer`, `@container` (treated as matching), `@font-face` and `@keyframes` (parsed and skipped).

Nested rules are flattened while parsing (`parse_body` and `nest_combine` in `web/cssparse.c`): each `&` is replaced by the parent selector, a child selector without `&` becomes a descendant of the parent, and nested `@media`, `@supports`, `@layer` and `@container` blocks apply to the parent selector.

Rules are put into buckets by the rightmost compound's id, class or tag, plus a universal bucket, so matching a node only looks at rules that can apply.

#### The cascade

`compute()` applies declarations in this order: the UA style sheet (`css_ua_text`), presentational attributes (`width`, `bgcolor`, `align` and so on), author rules by specificity and order, inline `style`, then `!important`. Inherited properties come from the parent's computed style. There are 177 properties in the `PROPS` X-macro in `web/css.h`; custom properties (`--x`, id `P_CUSTOM` = 1000) are stored per element and resolved by `var()` with fallbacks.

Lengths are stored as `L {px, pct, lo, hi, k}`, which can represent `calc()`, `min()`, `max()` and `clamp()` with mixed units; `lres()` resolves one against a reference size. Units: `px`, `em`, `rem`, `%`, `vw`, `vh`, `vmin`, `vmax`, `pt`, `pc`, `in`, `cm`, `mm`, `ch`, `ex`.

Values also cover colours (named, hex with 3/4/6/8 digits, `rgb()`/`hsl()`/`hwb()` in both syntaxes, `oklch()`, `oklab()`, `lab()`, `lch()`, `color(srgb ...)`, `light-dark()` (light side), `color-mix()`, system colours such as `Canvas` and `CanvasText`, `currentColor`, `transparent`), linear, radial and conic gradients, multiple box shadows, text shadows, `transform` (translate, scale, rotate is ignored, matrix), `opacity`, `filter` (ignored), and the shorthands `margin`, `padding`, `border*`, `background`, `font`, `flex`, `flex-flow`, `grid-template`, `grid-area`, `gap`, `inset`, `list-style`, `overflow`, `place-*`.

### 13.6 Layout (`web/layout.c`, `web/layout2.c`)

`layout_all` builds a box tree from the styled DOM (with anonymous block and inline wrappers where needed) and lays it out:

| Feature | Notes |
|---|---|
| Block flow | `box-sizing`, min/max sizes, auto margins, margin collapsing between siblings and through parents (first and last child), `aspect-ratio` |
| Inline flow | text runs and inline boxes are broken into lines; `white-space` (normal, nowrap, pre, pre-wrap, pre-line), `word-break`/`overflow-wrap`, `text-align` including `justify`, `vertical-align`, `line-height`, `letter-spacing`, `text-indent`, `text-transform`; inline-block and replaced elements sit on the baseline |
| Floats | left and right floats in a block formatting context, text flows around them, `clear` |
| Positioning | `relative` (and `transform` translate) offsets after layout; `absolute` and `fixed` boxes are laid out after the main pass against their containing block; `sticky` with `top` sticks while its containing block is in view (applied at paint time) |
| Stacking | `z-index` and positioned elements are painted in stacking order |
| Overflow | `hidden`, `scroll` and `auto` clip; scrollable boxes keep their own scroll offset and take the wheel |
| Tables | an occupancy grid with `colspan` and `rowspan`, automatic column widths from min/max content, `border-collapse`, `border-spacing`, captions |
| Flexbox | the full algorithm: direction, wrap, grow/shrink/basis, `order`, `justify-content`, `align-items`/`align-self`/`align-content`, `gap`, auto margins |
| Grid | `grid-template-columns`/`rows` with px, %, `fr`, `auto`, `minmax()`, `repeat()` including `auto-fill`/`auto-fit`; line placement, `span`, named areas, auto placement, `gap`, alignment |
| Text overflow | `text-overflow: ellipsis` on overflow-hidden boxes, and `-webkit-line-clamp` (with `display: -webkit-box`) cuts the box after N lines and ends the last line with an ellipsis |
| Lists | `list-style-type` (disc, circle, square, decimal, roman, alpha, none) and `list-style-position` |
| Forms | `input` (text, password, checkbox, radio, range, color, number, search, email, submit, button, reset, file, hidden), `textarea`, `select`, `button`, `progress`, `meter` |

`intrinsic()` gives min-content and max-content widths for shrink-to-fit boxes, table columns and flex bases. `hit_test` maps a point to the deepest node for mouse events.

The engine paints only what the window shows: layout produces the document height (`doch`), and the kernel scrolls the viewport.

### 13.7 Painting, fonts and images

`paint_all` walks the boxes in stacking order and draws backgrounds (colours, gradients, images with `background-size`, `-position` and `-repeat`), borders (solid, dashed, dotted, double, inset, outset; rounded corners with anti-aliasing via `rr_cov`), box shadows, text with decorations and shadows, list markers, form controls, the focus ring, the open `select` list, and find-in-page highlights. Clipping follows `overflow` and the viewport; fixed elements use the viewport clip. Boxes hidden with `clip: rect(...)` of zero area, `clip-path: inset(50%)` or a 1-px overflow-hidden box are skipped. `-webkit-background-clip: text` paints the text in the gradient's first colour instead of painting the background.

Fonts. `tools/mkwebfont.py` converts Liberation Sans, Serif and Mono (regular, bold, italic) into outline data in `web/fontdata.c`. Composite glyphs are decomposed, and glyphs are sorted by code point for binary search. Sans and Sans Bold also carry Greek, Cyrillic, Latin Extended-A and about 80 symbols taken from DejaVu Sans (check marks, arrows, stars, card suits, the hamburger menu, warning sign, gear, envelope and so on); other faces fall back to them, and a code point that no face has is drawn as `?`. `web/font.c` rasterizes outlines with an accumulation buffer (exact area coverage, as in font-rs) and caches glyphs by (face, size in quarter pixels, code point). `font-family` maps generic and common names (Arial, Helvetica, system-ui, Georgia, Times, Courier, monospace ...) to the three families.

Images. `<img>`, `<picture>`/`srcset` (the best candidate for the viewport), CSS background images and `data:` URLs are fetched and decoded by the kernel (`nwk_image`) and scaled with bilinear filtering. SVG files and inline `<svg>` are rendered by `web/svg.c`: paths (all commands including arcs), basic shapes, `use`/`symbol`, `viewBox`, transforms, fill and stroke. `loading="lazy"` images load when they come near the viewport.

### 13.8 JavaScript (`web/js.c`, `web/prelude.js`)

The JavaScript engine is QuickJS-ng (ES2023: classes, modules, async/await, generators, Proxy, BigInt, typed arrays, regular expressions with Unicode). Each page gets a fresh runtime.

Two layers implement the browser API:

1. C bindings in `web/js.c`. DOM nodes are objects of one native class that holds a pointer to the `node`. `node_funcs`, `elem_funcs` and `doc_funcs` provide tree access and mutation, attributes, `textContent`, `innerHTML`/`outerHTML`, `value`, `checked`, geometry (`getBoundingClientRect`, `offset*`, `client*`, `scroll*`), `querySelector`/`querySelectorAll`/`matches`/`closest`, `getComputedStyle`, focus, and form submission. Internal helpers sit on a hidden `__n` object.
2. The prelude, about 900 lines of JavaScript that run before the page. It defines `EventTarget`, `Event` and its subclasses, the event dispatcher (capture, target and bubble phases, `once`, `passive`, `stopPropagation`, `preventDefault`, inline `on...` attributes), `DOMTokenList` (`classList`), `dataset`, the `style` object (backed by inline declarations), the HTML element constructors and prototypes (`HTMLInputElement` and about 60 others, set per tag with `N.setProto`), `window`, `location`, `history` (`pushState`, `replaceState`, `popstate`), `navigator`, `screen`, `localStorage`/`sessionStorage`, `document.cookie`, `fetch` with `Response`/`Headers`, `XMLHttpRequest`, `URL`/`URLSearchParams`, `FormData`, `Blob`, `TextEncoder`/`TextDecoder`, `atob`/`btoa`, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`, `customElements`, `matchMedia`, `requestAnimationFrame`, `queueMicrotask`, `structuredClone`, `performance.now`, and `console`.

Editing. Elements with `contenteditable` (and `plaintext-only`) are focusable. A click places the caret at the clicked character (`ce_place` in `web/engine.c`), and each key goes to `__ceKey` in the prelude, which fires a cancelable `beforeinput` with `inputType` and `data`, edits the DOM (insert text, delete backward or forward and merge blocks, split the paragraph on Enter), moves the selection and fires `input`. `Selection` and `Range` are full objects (anchor, focus, direction, `setBaseAndExtent`, `extend`, `modify`, `deleteContents`, `insertNode`, `getBoundingClientRect` through the native `caretRect`). The engine draws the caret from the selection's focus (`caret_pos` in `web/layout.c`). `MutationObserver` is real: `web/dom.c` calls a hook for every child, attribute and text change while an observer exists (never during parsing), and records are delivered in a microtask. This is what editors like ProseMirror, which ChatGPT uses, rely on.

Streams. `ReadableStream` (readers, async iteration, `pipeThrough`, `pipeTo`, `tee`), `WritableStream`, `TransformStream`, `TextDecoderStream` and `TextEncoderStream` are implemented in the prelude. `Response.body` is a stream holding the downloaded body, and `new Response(stream)` reads a stream. Responses still arrive complete (section 13.15), so a streamed answer appears all at once.

Every element, the document and window have real `on...` properties for about 110 events (`onclick`, `oninput`, `onload` ...), and `HTMLInputElement.prototype` (and the textarea, select and option prototypes) own the `value`/`checked` accessors. Libraries test both: React checks `'oninput' in document` and installs its value tracker from the prototype's `value` descriptor.

Scripts. Classic scripts run in document order; `defer` and `type="module"` scripts run after parsing; `async` scripts run when they arrive. Modules are loaded through a QuickJS module loader that fetches `import`ed URLs. Scripts inserted by script (`createElement('script')`) run and fire `load` or `error`. `DOMContentLoaded` and `load` fire in the right order.

Timers and the event loop. `setTimeout`, `setInterval` and `requestAnimationFrame` are driven by `NWE_OP_TICK`, which the browser sends about every 20 ms while its window is open. Microtasks (promises) run after every script and every event.

Limits. A script that runs longer than 6 seconds without returning is interrupted (the interrupt handler compares `nwk_ms()` with a deadline that is armed before each entry into JavaScript). Uncaught errors go to the console (`about:console`) with a stack trace.

Redrawing. A DOM or style change only marks the document dirty (`dirty_style`, `dirty_layout`, `dirty_paint`); restyle and layout happen once, before the next paint or the next geometry query from script.

Tested with: React 19.2 (bundled todo app, state, effects, controlled inputs), jQuery 3.6 (DOM building, `.css`, `.addClass`, `fadeIn`), and the 290 KB Go language specification (laid out in about 57 ms on the host).

Binary data (v9.12): `N.fetch(url, method, body, ctype, bin)` returns the body as an `ArrayBuffer` when `bin` is true, and the body argument may be a string or any `ArrayBuffer`/typed array/`DataView` (read with `jbytes` in `js.c`). `fetch` always asks for bytes; `Response` keeps them in `_u8` and decodes text only for `text()`/`json()`. `XMLHttpRequest` asks for bytes when `responseType` is `arraybuffer` or `blob`. `Blob` keeps either a string or bytes. `TextDecoder` (UTF-8) and `TextEncoder` call the natives `utf8dec` and `utf8enc` instead of looping in JavaScript.

### 13.9 Networking and the HTTP client (`apps/internet/webglue.c`)

`nwk_fetch(url, method, ctype, body, blen, referer, resp)` is the only HTTP client the engine uses:

- HTTP/1.1 with `Connection: close`: plain HTTP over the kernel TCP stream (`tcps_*`), HTTPS over the kernel TLS 1.3 client (`tls_get`) with certificate checks;
- up to 7 redirects (301, 302, 303, 307, 308; a 303, or a POST answered with 301/302, continues as GET);
- chunked transfer and `gzip` content encoding (`Accept-Encoding: gzip`; a `deflate` body is also decoded);
- `Cookie` headers from the jar and `Set-Cookie` back into it, on every request including subresources and `fetch()`;
- a Chrome-like `User-Agent` (`WEB_UA`, ends in `Noovex/9.08`) so sites send their normal pages;
- `Referer`, `Accept` and `Accept-Language: en-US` headers.

Typed addresses without a scheme try HTTPS first and fall back to HTTP.

### 13.10 Cookies and web storage (`net/http/cookies.c`)

The jar holds up to 400 cookies (`CK_MAX`). For each cookie it keeps name, value (511 bytes), domain, path, expiry, creation time and the flags secure, HttpOnly, host-only and SameSite.

| Rule | Implementation |
|---|---|
| Domain | Without `Domain` the cookie is host-only. `Domain` must domain-match the request host and contain a dot (or be `localhost`); a leading dot is ignored |
| Path | Default path is the request path up to its last `/`; matching follows RFC 6265 |
| Expiry | `Max-Age` wins over `Expires`; `Max-Age=0` or a date in the past deletes the cookie. Expired cookies are dropped when they are next looked at |
| Secure | Only sent over HTTPS, and only set from HTTPS |
| HttpOnly | Sent with requests but hidden from `document.cookie` (the `forjs` flag), and JavaScript cannot overwrite it |
| SameSite | Parsed, stored and shown in about:cookies. There is no cross-site request context yet, so it does not change what is sent |
| Order | Longer paths first, as browsers do |

Persistence. Cookies with an expiry are saved to `cookies.txt` in the user's file system (`fs_save_bin`), at most every 3 seconds while something changed. Session cookies live until reboot. `localStorage` (keyed by origin; 600 entries in total) is saved to `webstorage.txt` the same way; `sessionStorage` entries are kept in memory only.

The old `ck_*` functions in `net/http/http.c` (used by the `http` command and the account code) are now wrappers around the same jar.

### 13.11 The Browser window (`apps/internet/browser.c`)

The window keeps the toolbar from earlier versions (back, reload, bookmark star, address bar, Go, Save, Home, Find, and a new Cookies button) and up to 6 tabs. Switching tabs reloads the page of that tab.

| Page | Contents |
|---|---|
| `about:blank` | empty page |
| `about:js` | a JavaScript demo page |
| `about:cookies` | every stored cookie by site with its flags; buttons to clear everything (`?clear`) or remove one site (`?del=`) |
| `about:console` | the last console messages and script errors |
| `about:history`, `about:bookmarks` | history and bookmarks |

Downloads (`Content-Disposition: attachment` or a file extension like `.zip`) go to Downloads, and a direct link to an image opens the image viewer. Network errors show an error page with the reason (DNS, connect, timeout, TLS).

Input. Mouse moves send `MOVE` (hover styles, link cursor, status-bar URL); a click sends `DOWN` and `UP`; the wheel goes to `browser_wheel`; keys are mapped by `map_key` (the kernel's arrow, Home, End, Page and Delete codes become `NWE_K_*`). `browser_tick` sends `TICK` from the main loop (`graphics/ui/input.c`) and asks for a redraw when the page changed.

Shell. `browse <url>` (or `www <url>`) opens the browser at a URL; in the live terminal it first switches to the desktop. `desktop` switches from the live terminal to the desktop without opening anything.

### 13.12 Memory layout change in v9.08

Note: v9.11 changed the layout again (15 MiB image, stack at `0x1080000`, `.bss` at `0x1300000`); section 2.7 has the current map. This section records why v9.08 had to move the stack.

The 6.7 MB kernel no longer fit below the boot stack at `0x700000`. Nothing reported it: the stack simply overwrote the end of `.rodata`, and the boot integrity check failed ("kernel code was modified"). The layout is now:

| Range | Use |
|---|---|
| `0x100000-0x900000` | kernel image, at most 8 MiB (`ASSERT(__file_end <= 0x00900000)` in both linker scripts) |
| `0x900000-0x980000` | boot stack, top at `0x980000` (`boot64.S`, both entry paths) |
| `0x980000-0xC00000` | USB and disk buffers (xHCI from `0x990000`) |

The UEFI loader's `KERNEL_MAX` is now `0x900000-0x100000` (8 MiB), and its kernel-area table, the installer's memory test and the docs use the same boundaries. The ChaCha20-Poly1305 code used a fixed scratch buffer at `0x900000`, which is now inside the stack area; it uses a heap buffer instead (`aead_mac` in `lib/crypto/nvxcrypto.c`).

### 13.13 The `qjs` command

`qjs` is QuickJS-ng's own command-line interpreter, built as a musl program (with `std` and `os` modules, `gen/repl.c` and `gen/standalone.c`), compressed and embedded like the other ports (section 6.6). Examples:

```
qjs -e "print(1+2, [1,2,3].map(x=>x*x).join(','))"
qjs script.js
qjs                      (interactive REPL)
```

It is separate from the browser's copy of QuickJS: it runs as a user process with the Linux system-call layer, while the browser's copy runs inside the kernel on the engine stack.

### 13.14 Testing the engine

Host harness. `tools/webhost.c` links the engine objects (built for Linux with `-D_GNU_SOURCE`, without `krt.c`) and renders a file to a PPM image:

```
webhost page.html out.ppm [width] [height] [maxheight] [javascript-to-eval]
```

It prints load and paint times, the title, the document height, every fetch and every console message. The optional last argument is evaluated after the page has loaded and its result is printed, which makes it easy to click buttons or read the DOM from a test. Link it with `-lm -lz` (zlib provides `nwk_inflate` for WOFF fonts). With `WH_MS=N` in the environment it keeps ticking the engine in real time for N milliseconds after loading, so timers, animations, transitions and the simulated media backend run before the picture is taken.

In QEMU. The test site used for v9.08 runs a small HTTP server on a tap device that answers every DNS name (so `browse noovex.test` works) and has pages for cookies (`/cookie/set`, `/cookie/show`), a form echo (`/echo`), a JSON API (`/api/data`) and a redirect. Checked in QEMU: rendering, `fetch()`, images, clicks, form POST, Back, server cookies (including HttpOnly hidden from script), about:cookies, a React app with real keyboard input, and jQuery.

### 13.15 Known gaps

- No `<canvas>` drawing, `<iframe>` content, Web Workers, WebSockets or WebAssembly. `<video>` has no subtitles, no encrypted media (EME) and plays at speed 1 only (section 13.17).
- Web fonts with CFF outlines (`.otf` files that are not TrueType inside) are not used; only ligature substitution is taken from `GSUB`, and there is no kerning (`GPOS`).
- 3D transforms are flattened to their 2D part, and transformed elements are hit-tested at their translated position only. Animations of layout properties relayout the page on every frame, which is slow on large pages.
- Gradient text is drawn in one colour, not as a gradient.
- Editing has no text selection by mouse drag, no clipboard and no undo; Shift and Ctrl combinations are not passed to pages.
- One HTTP connection per request (no keep-alive, no HTTP/2), and requests are synchronous, so a slow server stalls the browser until it answers or times out. Fonts and stylesheets are also fetched synchronously when they are first needed.
- Shadow DOM: styles are scoped by the host's tag name, so two different shadow roots on the same kind of element share their styles; `:host-context()`, `slotchange` and closed-mode retargeting are not implemented.
- SameSite is not enforced, and there is no public-suffix list (a cookie for `co.uk` would be accepted).
- Right-to-left text, complex scripts and emoji are not shaped.

### 13.16 Extending the engine

- A new CSS property: add it to `PROPS` in `web/css.h`, parse it in `apply()` in `web/cssstyle.c`, add a field to `style` in `web/nwe.h`, and use it in layout or paint. If it is inherited, add it to the inheritance copy in `compute()`.
- A new DOM method: if it needs engine internals, add a native function to the right table in `web/js.c`; otherwise write it in `web/prelude.js` (it is rebuilt by `build.sh`).
- A new kernel service for the engine: add an `nwk_` function to `nwe_api.h`, implement it in `apps/internet/webglue.c` (and a stub in `tools/webhost.c`), and remember that it runs on the engine stack.
- After any change, render a few pages with `webhost` before booting: it is much faster than QEMU and errors print to the terminal.

### 13.17 Video and audio in the browser

Every `<video>` or `<audio>` that has a source gets a media slot (section 6.13). The pieces:

| Piece | Where | What it does |
|---|---|---|
| `N.mediaOp(op, el, ...)` | `web/js.c`, native 36 | 0 create (URL or empty for kernel data, split flag, start ms, paused), 1 command, 2 status as `[cur_ms, dur_ms, flags, w, h, buffered%]`, 3 append bytes to stream 0 or 1, 4 end of a stream, 5 restart at a time, 6 free, 7 stream length |
| registry | `web/wmedia.c` | up to 8 elements with their slot, the rectangle and clip they were last painted at, the last frame sequence, and the video size |
| painting | `wm_paint` from `paint.c` | draws the frame with `object-fit: contain` and black bars, a waiting shade, a big play button before the first play, and, with the `controls` attribute, a bar with play/pause, time and a seek line (an `<audio controls>` is a 300 x 54 bar). A size change sends `SIZE` so the decoder scales to the element |
| fast path | `NWE_OP_MEDIA`, `wm_fast` | when nothing else on the page is dirty, only elements with a new frame are redrawn into the window's part of `SCENE`; the browser copies that rectangle to the screen (`browser_media_fast`). Anything painted over a video is restored at the next full paint |
| tick | `wm_tick` in `NWE_OP_TICK` | asks for a repaint when a status flag changes, every second while controls are shown, and when the first frame arrives; when the video size becomes known it asks for a new layout, because `replaced_size` uses it as the intrinsic size |
| clicks | `wm_click` in `NWE_OP_UP` | a click on a video with controls calls `__mediaUI`: toggle play, or seek to the clicked point of the bar; it runs after the page's own click handlers and only if they did not cancel the event |

The element API is in `web/prelude.js` (search for `class TimeRanges`). State lives in a hidden `__ms` object. `load()` picks `src` or the first playable `<source>`; a `blob:` URL is looked up in the table that `URL.createObjectURL` now keeps, so a `MediaSource` attaches and a `Blob` is handed over as kernel data; `data:` URLs are decoded; anything else is resolved and passed as a URL, which `play` downloads itself (no cookies are sent). One 100 ms interval polls every active element and turns status changes into events: `durationchange`, `loadedmetadata`, `loadeddata`, `canplay`, `canplaythrough`, `playing`, `waiting`, `timeupdate` (every 250 ms), `seeked`, `ended`, `error`, `resize`. A 700 ms scan picks up elements whose `src` was set as an attribute, and starts `autoplay`.

MediaSource: `addSourceBuffer` allows two buffers. The decoder starts when every buffer has received its init segment (or after a few appends); stream 0 is the first buffer. `appendBuffer` is asynchronous like in browsers (`updatestart`, `update`, `updateend`). Each append is scanned for timestamps so `buffered` is right: for MP4, `mdhd` timescale and `trex` default duration from the init segment, then `tfdt` and the `trun` sample durations of each `moof`; for WebM, `TimecodeScale` and `Duration` from `Info`, then each `Cluster` time code plus the largest block offset. A seek inside the buffered ranges restarts the decoder with all data kept; a seek outside drops everything but the init segments, and the page then appends segments for the new time. `isTypeSupported` and `canPlayType` accept MP4, WebM, Ogg, MP3, WAV and AAC with the codecs FFmpeg was built with, and refuse AV1, high-bit-depth VP9 profiles, AC-3/E-AC-3, more than 2 channels, and video larger than 1920 x 1088 or faster than 60 fps, so adaptive players choose a stream the decoder can handle.

Full screen: `requestFullscreen` gives the element a fixed, full-viewport style (the old style is saved), sets `document.fullscreenElement` and fires `fullscreenchange`; `exitFullscreen` restores it.

Testing: `tools/webhost.c` has a simulated media backend (a moving clock, a test pattern frame, and a log of every command and append), so the element API and MSE can be tested on the host with `WH_MS=1500 webhost page.html out.ppm`; `WH_MS` keeps ticking the engine in real time for that many milliseconds. In QEMU, a page with a progressive MP4 and a page that feeds fragmented MP4 video and audio through MediaSource both play to the end.

YouTube itself was not tested: the build machine cannot reach it, and it may need more (for example its own streaming protocol, signature code and checks that the browser is real). Reports of what the browser log shows on youtube.com help to close the gaps.

### 13.18 Transforms, animations and transitions

Transforms. `transform` is parsed into a list of functions (`tfn`: translate with lengths, scale, rotate, skew, matrix; `matrix3d` and `rotate3d` keep their 2D part) stored on the style, together with the `translate`, `rotate` and `scale` properties and `transform-origin`. `tf_matrix(style, w, h, m)` multiplies them into one 2D matrix `x' = a x + c y + e, y' = b x + d y + f` about the origin. Layout moves a transformed box by `e, f` (stored as `box.btx/bty`), so hit-testing and `getBoundingClientRect` follow translations. A transformed element is a stacking context. In `paint_stack`, an element whose matrix has a non-identity linear part (or that is animated) goes through `paint_transformed`: the bounds of its subtree are measured (`sub_bounds`, at most 1800 px around the origin and 1.6 million pixels), the subtree is painted twice into off-screen buffers, once over black and once over white, and the two results give colour and coverage for every pixel. That buffer is then drawn through the inverse matrix with bilinear filtering. No primitive needs to know about alpha. A video inside a transformed element is drawn but not registered for the fast frame path. A translation-only change (a sliding element) is painted with an offset and needs no buffer.

Animations and transitions live in `web/anim.c`.

| Piece | What it does |
|---|---|
| `@keyframes` | parsed in `cssparse.c` (`parse_keyframes`; `from`, `to`, percentages, lists) into `kfset` records; cleared when the sheets are rebuilt |
| properties | `animation` and its 8 longhands, `transition` and its 4 longhands, parsed into `animspec`/`transspec` arrays on the style (up to 8 animations and 12 transitions per element) |
| keyframe styles | after each style pass `anim_after_style` links every element that names a known `@keyframes` to a run record (start time kept across style passes, keyed by element and name) and computes one style per keyframe with `css_style_with` (the element's style plus that keyframe's declarations) |
| transitions | `anim_before_style` saves the current value of every transitioned property before a style pass; after it, a changed value starts a transition from the saved value (which may be the middle of a running transition) to the new one |
| values | opacity, colours (`color`, `background-color`, border and outline colours, `fill`, `stroke`), lengths (`width`, `height`, min/max, `top/right/bottom/left`, margins, paddings, border widths, `font-size`, `letter-spacing`), `border-radius`, transforms (function by function when both lists have the same functions, otherwise as matrices) and `visibility` |
| timing | `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier()` (solved with Newton steps), `steps()`, `step-start`, `step-end`; per-keyframe segments, `delay`, `iteration-count` (including `infinite` and fractions), `direction`, `fill-mode`, `play-state` |
| frames | the engine tick calls `anim_tick` at most every 33 ms; it writes the current values straight into the computed styles, asks for a repaint, and for a relayout if a layout property is animating. It then dispatches the queued `animation*` and `transition*` events |

### 13.19 Web fonts

`@font-face` rules are collected by `fontface_add` (`web/webfont.c`): family, weight or weight range, style, `unicode-range` (a face whose range covers Latin letters is preferred) and the `src` list, ranked woff2, woff, truetype, opentype. When `face_for` meets a family that has `@font-face` rules, `wf_face` picks the closest face and loads it the first time (`eng_fetch_text`, which also handles `data:` URLs); a URL that fails is not tried again on that page.

`web/ttf.c` turns the file into a dynamic face (up to 32 per page, numbered after the 9 built-in faces; `nwe_face(face)` returns the record for either kind):

| Format | Handling |
|---|---|
| TTF / TrueType OTF | tables read in place |
| WOFF 1 | every table inflated with the kernel's zlib (`nwk_inflate`) |
| WOFF 2 | `web/brotli.c` decompresses the table stream (RFC 7932, with the static dictionary and transforms in `web/brotli_tab.c`, extracted from the reference library), then `web/woff2.c` rebuilds `glyf`/`loca` from the transformed streams (triplet-coded points, 255UInt16 counts, composite records, bounding-box bitmap) and `hmtx` if transformed, and writes a normal sfnt |

`cmap` formats 4 and 12 give the code points; advances come from `hmtx`, metrics from `head`, `hhea` and `OS/2`. Glyph outlines are decoded from `glyf` only when a glyph is first drawn (`ttf_outline`), including composite glyphs with offsets and scales, into the same op list the built-in faces use, and cached. A missing glyph falls back to the built-in sans face. Ligatures: lookup type 4 (and type 7 extensions) of the `liga`, `rlig` and `clig` features become ligature records; each result glyph gets a private code point from U+F0000. `ttf_shape` replaces matching runs (longest match first) before text is measured and drawn, so a word such as "home" in an icon font becomes one icon.

Tests: the Brotli decoder was checked against the reference encoder on 800 random inputs (all qualities, windows 10 to 24, generic, text and font modes), and WOFF2 files made with fontTools from DejaVu Sans (6,253 glyphs) and Liberation Serif (2,602) rebuild to exactly the original outlines and advances.

### 13.20 Shadow DOM

`node.shadow` points from a host to its shadow root (a fragment) and `node.host` back. `attachShadow` (native 39) creates it; the JavaScript object gets the `ShadowRoot` prototype (`host`, `mode`, `innerHTML`, `getElementById`, `activeElement`, `adoptedStyleSheets`). Closed roots are hidden from `shadowRoot`.

| Area | Behaviour |
|---|---|
| rendering | `build_children` uses the shadow root's children instead of the host's. A `<slot>` inside a shadow tree is replaced by the host's children whose `slot` attribute matches its `name` (the default slot takes the rest, including text), or by its own children when nothing is assigned |
| style | the shadow tree inherits from the host; light-DOM children inherit from the slot they are shown in. Stylesheets inside a shadow root are collected with the host's tag name as their scope: they match only elements inside shadow roots of that tag, `:host` and `:host(selector)` match the host, `::slotted(selector)` matches its assigned children. Page stylesheets do not reach into shadow trees (the browser's default sheet does) |
| events | the event path continues from a shadow root to its host |
| DOM | `getRootNode({composed})`, `assignedNodes()`, `assignedElements()`, `document.adoptedStyleSheets`, `new CSSStyleSheet()` with `replaceSync`/`replace`/`insertRule` |

Insertions into a shadow tree count as document changes (`in_composed_doc`), so they restyle the page.

### 13.21 Style system speed

| Change | Effect |
|---|---|
| Stylesheet cache (`parse_cached`): parsed rules, keyframes and font faces are kept per sheet, keyed by an FNV hash of the text, base URL, media and shadow scope | a rebuild re-uses unchanged sheets instead of parsing them again |
| Incremental index: when the sequence of sheets of the last rebuild is a prefix of the new one, only the new rules are added to the id/class/tag buckets | adding a `<style>` element costs only its own rules |
| Ancestor filter: `style_tree` keeps counts of the tags, ids and classes of the current ancestors (4096 slots); each selector remembers up to 4 hashes of compounds that must be ancestors (descendant or child combinators) and is skipped at once if one is missing | most tag-bucket rules with descendant selectors are rejected without walking up the tree; it is only used during a style pass |
| `@media` results are cached per rule for each style pass | the media query string is no longer parsed for every element |

On the host, a page with 18,000 rules that adds 60 `<style>` elements and forces a style pass after each went from 2,436 ms to 282 ms.

### 13.22 Crash containment

The engine runs on its own 2 MiB stack (`g_estack`, `apps/internet/webglue.c`). When a CPU exception happens in kernel mode, `isr_body` (`arch/x86_64/interrupts/interrupts.c`) first calls `web_fault_redirect`. If the browser is inside an engine call and the faulting stack pointer is in the engine stack (or just below it, for an overflow), the interrupt frame is changed so that `iretq` lands in `web_fault_land` on a safe part of that stack. `web_fault_land` calls `nwe_fail`, which unwinds to the `setjmp` in `nwe_entry`; the engine marks the page dead and returns -1.

`web_call` then sets `g_web_failed`. `browser_tick` sees it and shows "This page crashed" (`browser_crashed`); the next load resets the engine. NMI, double fault and machine check are never redirected, and after 64 redirected faults the normal error screen is shown again, so a broken engine cannot loop forever.

`about:crash` writes to a non-canonical address inside the engine to test this path.

### 13.23 YouTube player page

Since v9.15 `yt_try` is no longer called, so YouTube addresses load the real site. The description below is kept for the code that is still in `apps/internet/browser.c`.

YouTube's desktop site needs far more JavaScript and layout than the engine handles, so `yt_try` (`apps/internet/browser.c`) recognises watch addresses before any network request: `youtube.com`, `www.`, `m.`, `music.` and `youtube-nocookie.com` with `/watch?v=`, `/shorts/`, `/embed/` or `/live/`, and `youtu.be/ID`. The video ID (letters, digits, `-` and `_`) is put in a built-in page.

The page's script posts to `https://www.youtube.com/youtubei/v1/player` with the clients `ANDROID_VR`, `IOS`, `TVHTML5_SIMPLY_EMBEDDED_PLAYER` and `WEB_EMBEDDED_PLAYER` in turn, and takes the first answer with `streamingData`. From `formats` (progressive streams that carry video and audio together) it picks the highest one up to 720p in MP4 or WebM, never AV1, sets it as the `src` of a `<video>`, and shows the title, channel, view count and the chosen quality. Playback goes through the media service and `play` with HTTP Range requests (section 13.17).

Limits: this could not be tested against youtube.com from the development machine; YouTube changes these clients often and may ask for sign-in or proof-of-origin tokens, and then the page shows the reason it was given. Adaptive-only videos (most above 720p), live streams and age-restricted videos do not play.

### 13.24 Limits raised for large web apps

| Limit | Value | Where |
|---|---|---|
| Script time | 15 s per call, plus 1 s per 40 KB of script source (at most 120 s more) | `arm`, `js_run_script` (`web/js.c`) |
| Engine arena | first of 768, 512, 384, 256, 160, 96, 64, 32 MiB that the heap can give while keeping 320 MiB free | `nwe_arena_init` (`web/krt.c`), `nwk_alloc_arena` (`apps/internet/webglue.c`) |
| JavaScript heap | 70 percent of the arena | `js_init` |
| JavaScript stack | 3 MiB | `JS_SetMaxStackSize` |
| Engine stack | 6 MiB | `ESTACK_SZ` |

`Intl` is defined at the start of `web/prelude.js` when the runtime has none. It follows English formatting rules for every locale.

### 13.25 Staying responsive while a page works

The engine still runs on the desktop thread. Instead of a second thread, the long-running places call back into the kernel:

| Caller | How often |
|---|---|
| QuickJS interrupt handler (`interrupt_cb`, `web/js.c`) | on every check, rate-limited to one pump per 10 ms in `nwk_busy` |
| `style_tree` (`web/cssstyle.c`) and `layout_blocks` (`web/layout.c`) | `nwe_check_stop` every 128 elements or blocks |
| `net_poll` (`drivers/storage/ahci.c`) and `nd_poll` (`net/stack/tcpip.c`) | `g_wait_pump` once per millisecond of waiting |

`busy_pump` (`graphics/ui/desktop.c`) polls USB mice, takes the PS/2 movement accumulated by the interrupt handler, moves the pointer with the same acceleration as the desktop and redraws only the pointer (`present_cursor`). Button presses are left for the main loop. It does nothing outside the desktop and is not re-entered.

If Esc is held while one engine call has been running for more than 1.5 seconds, `nwk_busy` returns 1. The interrupt handler then stops the running script and sets `g_nwe_user_stop`; in style or layout `nwe_check_stop` calls `nwe_fail` directly. `nwe_entry` unwinds, and the browser shows "This page stopped" (section 13.22).

Timers: `js_tick` runs due timers for at most 40 ms per tick. `setTimeout` and `setInterval` return 0 when 50,000 timers exist.

`data:` addresses are decoded in `browser_go` (percent-encoded or `;base64`) and loaded as HTML.

### 13.26 Layout on demand for scripts

`js_flush_n` (`web/js.c`) runs `eng_layout_if_dirty` before `getBoundingClientRect` and the geometry getters (`offset*`, `client*`, `scroll*`). If the element is in the document but has no box yet (the page is still loading and has never been laid out), it marks the styles dirty first. A counter allows 30 forced layouts per 50 ms; after that the old values are returned until the window passes. Re-entry from inside style or layout is ignored.

Measured in the host harness on a test page: before, every element reported 0x0 during load; after, `aspect-ratio`, flex `gap`, `clamp()`, `min()`, `display: contents`, `inset`, `:is()`, `:has()`, grid `repeat(3, 1fr)`, `calc()`, `margin-inline: auto`, `padding-block` and `fit-content` all reported the expected sizes and positions.

### 13.27 Bugs found against a modern landing page

A test page in the style of a current product landing page was rendered by the host harness (`tools/webhost.c`) and by Chromium at 1280x860, and the two images were compared.

| Bug | Fix |
|---|---|
| SVG `<text>` ignored | `svg_text` (`web/svg.c`): the element's text (whitespace collapsed, up to 1023 bytes, nested elements included) is drawn with `font_draw` at the transformed `x`/`y`. The size is `font-size` times the square root of the transform's determinant. The face comes from `css_face_for` (`web/cssstyle.c`, formerly the static `face_for`). |
| `color-mix()` and alpha | channels are mixed as premultiplied values and divided by the mixed alpha |
| comma inside `min()` in `repeat()` or `minmax()` | `top_comma` finds the separating comma at nesting depth 0 |
| `min(100%, 420px)` as a grid track | when a length has a percentage and an upper bound from `min()`, the track uses the bound in pixels |
| empty `getComputedStyle` during load | native 6 calls `js_flush_n` first |

After the fixes the header, hero, buttons, SVG logo with its text, separator and two-column section matched the Chromium rendering in position and size. Still different: `text-wrap: balance` is ignored, so lines break greedily.

### 13.28 New tab page and probe fixes

`resolve_area` (`web/cssstyle.c`) runs at the end of `compute`: when the element has a `grid-area` name and its parent has `grid-template-areas` (kept in `style.gta`), the rows and columns where the name appears give `grid-row-start/end` and `grid-column-start/end`.

`scrollHeight` returns the larger of the content height plus padding and the box height minus borders.

The new tab page (`g_page == 0` in `apps/internet/browser.c`) is drawn by the browser itself, not by the web engine. "I'm Feeling Lucky" opens a random Wikipedia article. Up to 16 clickable areas are kept per page.

### 13.29 Faster loading and form controls

`js_flush_n` keeps the time spent in forced layouts per second of wall time. It lays out only while that total is under 150 ms, or never when nothing is dirty. Engine-internal callers that need fresh boxes (`scrollIntoView`, focus navigation) call `eng_layout_if_dirty` themselves.

The host harness now takes `WH_ACT`, a list of actions separated by semicolons: `c X Y` (move, press and release), `t TEXT` (type characters), `k CODE` (one key). A form test page (text field, textarea, button, a div with `role=button`, checkbox with label, select, a link styled as a button, a button with a `pointer-events: none` child, a search field and a form) gave the expected values, clicks, `input` and `change` events and submit.

### 13.30 Load time limits

| Limit | Value | Where |
|---|---|---|
| One script call, timer or event | 6 s | `arm` (`web/js.c`) |
| Extra for a large script | 1 s per 200 KB, at most 8 s | `js_run_script` |
| All page scripts during a load | 8 s, counted from `js_init`; later scripts are skipped | `g_load_js_ms` |
| Forced layouts for scripts | 60 ms per second | `js_flush_n` |
| Pointer pump during network waits | once per 10 ms tick | `busy_pump` (`graphics/ui/desktop.c`) |

`nwk_fetch` (`apps/internet/webglue.c`) adds up the time and number of requests in `g_fetch_ms` and `g_fetch_n`. `browser_go` reports the time to fetch the page, the files fetched while the engine built it, and the engine's own time. The report also goes to the kernel log.
