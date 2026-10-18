# Part 1: Overview, source layout and build system

All paths below are relative to `source/` unless stated otherwise. Kernel paths (`init/main.c`, `core/buildsig.c`, ...) are relative to `source/kernel/`, which is also the directory `build.sh` runs from.

## 1. What NoovexOS 64 is

NoovexOS 64 is a from-scratch x86-64 operating system: a freestanding C kernel plus a few assembly stubs, its own BIOS and UEFI bootloaders, its own C library for user programs, and a set of ported third-party programs (Lua, Doom, ntfs-3g, musl, SQLite, gzip, bzip2, xz, zstd). The kernel reports itself as `"NoovexOS 64 v8.95 (from-scratch, Unix-like)"` (`fs/vfs.c`, the `version` leaf).

### 1.1 Design principles, as they appear in the code

| Principle | Where it shows up |
|---|---|
| Separately compiled modules. Each of the 105 kernel `.c` files is its own translation unit with a generated interface header under `include/nvx/`; a unit includes only the headers it uses. | `units.list`, `include/nvx/`, `tools/kbuild.py`; `build.sh` step `[3/6]` compiles changed units in parallel into `build/units/`. |
| Flat image at 1 MiB. The linked ELF is turned into a raw binary and loaded at physical `0x100000`. `_start` sits at offset 0 because `.text.entry` is placed first. | `arch/x86_64/kernel64.ld`, `arch/x86_64/boot/boot64.S` |
| Identity-mapped kernel. `boot64.S` builds page tables that identity-map 0-4 GiB with 2 MiB pages (`pml4`, `pdpt`, `pd0`..`pd3`) and calls `kmain()`. The kernel runs at its physical addresses and uses fixed physical regions directly (for example `PF_BASE 0x0C000000` / `PF_TOP 0x0E000000` in `mm/pmm_vmm.c`). | `boot64.S` ("fill PD0..PD3: 2048 entries * 2 MiB = identity map 0 .. 4 GiB"), `mm/pmm_vmm.c` ("Identity-mapped, so the kernel can build page tables in these frames directly") |
| Ring-3 user programs. User ELF programs are linked at `0x40000000000` and run in ring 3 in their own address space; the kernel enters them via `enter_ring3`/`iretq` and takes system calls on an `INT 0x80` gate and (for Linux-ABI programs) the `syscall` instruction. | `user/src/user.ld`, `arch/x86_64/ring3.c`, `arch/x86_64/boot/userspace.S`, `core/proc/elf.c`, `core/linux.c` |
| SMP under a big kernel lock. Up to `NCPU 16` cores, per-CPU `struct pcpu`, and a ticket lock `struct bkl { next, serving, owner, depth }` that is recursive for its owner. | `arch/x86_64/percpu.c` (`g_bkl`, `bkl_lock`), `arch/x86_64/smp.c`, `arch/x86_64/smp_sched.c`; the shell prints "big kernel lock, preemptive user mode" (`shell/commands.c`). |
| No SSE in kernel code generation. The kernel is compiled `-mno-mmx -mno-sse -mno-sse2`; explicit SSE is used only through inline asm in `arch/x86_64/simd.c` and its header ("SSE on at boot; kernel is -mno-sse so xmm is untouched by codegen"). Lua and Doom are compiled separately *with* SSE. | `build.sh` `CFLAGS`, `LUAF`, `third_party/doom/build_doom.sh` |
| Everything embedded. Firmware, installer boot files, the UEFI loader and every user program are linked into the kernel image as binary blobs; there is no initrd. | `build.sh` steps `[3a2]`, `[3a3]`, `[efi]`, `[u]`, `[u2]`, `[musl]`, `[ports]` |
| Self-verifying image. A `NVX64SIG` block in section `.sig` is patched after linking with a timestamp, size, UEFI entry address and two SHA-256 hashes; the kernel re-checks it at boot. | `core/buildsig.c`, `build.sh` step `[6/6]`, `sig_self_check()` in `init/main.c` |

## 2. Repository layout

The repository root holds `README.md`, `LICENSE`, `ARCHITECTURE.md`, `.gitignore` (ignores `source/kernel/build/`, `source/bootloader/build/*.o`, `__pycache__/`), `wifi-tables/` (RTL8821CE register tables `RTWAGC.TXT`, `RTWBB.TXT`, `RTWMAC.TXT`, `RTWRFA.TXT` and the host tool `rtwgen.c`), and `source/`. `source/` contains `bootloader/` (myboot BIOS loader, UEFI loader, hybrid ISO tooling), `myboot-dualfirmware/` (a separate myboot variant with its own EFI app, `src/efi.c`, and a nested duplicate `myboot/`; not used by `build.sh` or `check.py`), `kernel/` and `tools/`.

### 2.1 `bootloader/`

```
bootloader/
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

### 2.2 `kernel/`

```
kernel/
+-- units.list             the 104 kernel compilation units, in link order (see section 3)
+-- kernel64.c             all units in one file, used only by the host test harness
+-- build.sh               the whole kernel build (see section 4)
+-- tools/kbuild.py        incremental, parallel unit compiler used by build.sh
+-- include/nvx/           one interface header per unit, plus base.h and kernel.h
+-- arch/x86_64/
|   +-- boot/boot64.S      _start: Multiboot header, 32-bit entry, page tables, long mode, kmain; also uefi_entry
|   +-- boot/isr64.S       64-bit interrupt stubs (error code + vector, common path)
|   +-- boot/userspace.S   GDT with ring-3 segments + TSS, INT 0x80 gate, iretq entry, k_setjmp/k_longjmp
|   +-- boot/ap_trampoline.S  AP startup code in section .aptramp, copied to 0x8000 and started by SIPI
|   +-- boot/boot64_mb.S   alternative entry for myboot's long-mode ELF handoff (RDI=0x2BADB002, RSI=MBI); not used by build.sh
|   +-- boot/mbr.S, stage2.S, cdboot.S, README_CUSTOMBOOT.txt   older custom BIOS/CD boot path (tools/mkboot.py, tools/mkiso.py)
|   +-- kernel64.ld        the linker script used by build.sh
|   +-- kernel64_mb.ld     ELF variant of the same layout for boot64_mb.S; not used by build.sh
|   +-- cpu.h, io.h, simd.h   inline CPU primitives (cpu_irq_on/off, cpu_pause...), port I/O, SSE fill/copy in asm
|   +-- cpu_init.c, cpuinfo.c, interrupts.c   APIC virtual wire + CPU init, cpuid/rdmsr, IDT/PIC/PIT
|   +-- hrtimer.c          HPET, TSC and local-APIC timer (HR_LAPIC_VEC 0x40)
|   +-- ring3.c, smp.c, smp_sched.c   GDT/TSS + ring-3 entry, MADT core detection + AP start, SMP scheduling + IPIs
|   +-- doom_jmp.c         doom_setjmp/doom_longjmp in top-level asm
+-- include/
|   +-- kapi.h             the driver API (kapi_pci_*, kapi_in*/out*, kapi_dma_pages, kapi_phys, kapi_delay_us, kapi_ns, kapi_log, kapi_mb)
|   +-- drivers.h          struct nvx_nic, struct nvx_snd and the *_probe functions of separately built drivers
|   +-- percpu.h           NCPU, struct tss64, struct pcpu, struct bkl / g_bkl
+-- core/
|   +-- buildsig.c, kapi.c g_buildsig (NVX64SIG block in .sig); kapi_* implementations over pci.c and io.h
|   +-- klog.c, panic.c    COM1 serial log; "Red Screen of Death" exception screen
|   +-- power.c, sound.c   ACPI tables + power off/reboot/sleep; snd_init (hda_probe, then ac97_probe)
|   +-- syscall.c          INT 0x80 system calls and fork+exec+waitpid spawning
|   +-- linux.c            Linux x86-64 system-call ABI (used by musl programs); included from shell/commands.c
|   +-- proc/elf.c, jobs.c, sched.c   ELF loader, process table (JMAX 256), threads, futexes and job control, cooperative scheduler
+-- mm/heap.c, heap_api.c, pmm_vmm.c, slab.c   kmalloc free-list heap, malloc/free aliases, frame allocator + page tables, slab caches
+-- lib/
|   +-- mem.c, klibc.c, fmt.c, js.h, png.h, jpeg.h   memset/memcpy, string functions, formatters, integer JS interpreter, image decoders
|   +-- crypto/nvxcrypto.h, nvxtls.c, nvxcrypto2.c, nvxwpa.c   SHA/HMAC/HKDF/ChaCha20/Poly1305/X25519, TLS 1.3 client, SHA-512 etc., SHA-1 + WPA helpers
+-- drivers/
|   +-- acpi/aml_scan.c, aml.c, acpi_dev.c   AML scanning, AML interpreter, ACPI devices (EC, battery, ...)
|   +-- bus/pci.c, i2c/dw_i2c.c, rtc/cmos.c  PCI config scan, DesignWare I2C, CMOS clock
|   +-- input/ps2.c, ps2_mouse.c, input.c, touchpad.c, tp_hid.c, tp_gesture.c, i2c_hid.c, elan_i2c.c, elan_ps2.c
|   +-- net/nic.c, nic_names.c               built-in NIC path and PCI-ID names
|   +-- net/e1000.c, rtl8139.c, rtl8169.c    separately compiled NIC drivers (MODS)
|   +-- sound/ac97.c, hda.c, ring.h          separately compiled audio drivers (MODS) and their ring buffer
|   +-- storage/ata.c, ahci.c, gdisk.c, nvme.c   ATA PIO, AHCI identify, AHCI driver, NVMe driver
|   +-- usb/xhci.c                           xHCI controller, HID and mass storage
|   +-- video/framebuffer.c                  framebuffer and back buffers
|   +-- video/nvidia/nvgpu.c, nvdisp.c, nvmon.c  NVIDIA probe, display state, EDID
|   +-- video/nvidia/nvflcn.c, nvmem.c, nvgsp.c  NVIDIA falcons and VBIOS, VRAM and GPU page tables, GSP-RM (v9.81)
|   +-- virtio/virtio.c/.h, virtio_net.c, virtio_blk.c   VirtIO transport, net and block (MODS)
|   +-- wifi/rtw8821ce.c, rtw_efuse.c, rtw_sec.c, rtw_tables.h   Realtek RTL8821CE Wi-Fi
+-- net/tcpip.c, sockets.c, http.c, wifi/wpa2.c, wifi/wifi_netdev.c   TCP/IP, socket layer, HTTP(S) client, WPA2 handshake, Wi-Fi netdev
+-- fs/
|   +-- ramfs.c, vfs.c, dirtree.c          RAM file system, VFS + mount table, directory tree
|   +-- nvxfs.c, fat_ata.c                 NVXFS on the ATA disk, FAT reader
|   +-- unixfs.c -> nvx4.c                  Unix-semantics layer and the NVX4 on-disk format (included from shell/commands.c)
|   +-- ntfsk.c -> vfat.c                   NTFS (via ntfs-3g) and VFAT glue (included from shell/commands.c)
|   +-- pkg.c                              package handling (8 MiB g_pkg_raw buffer)
+-- gui/theme.c, wm.c, effects.c, desktop.c, input.c   theming, window manager, glass effects, desktop/compositor, input dispatch
+-- shell/terminal.c, vt.c, commands.c     terminal grid, escape-sequence interpreter and termios (vt.c), shell commands
+-- apps/                                  browser.c, calculator.c, chatgpt.c, doom.c, editor.c, file_explorer.c, files_classic.c, logs.c, settings.c, small_apps.c
+-- init/splash.c, bootmenu.c, installer.c, setup.c, main.c   boot splash, boot menu, disk manager/installer, first-run setup/login, kmain
+-- rune/rune_gfx.rn, rune_gfx.h           graphics routine written in Rune and its C prototypes (rune_render, rune_fib)
+-- assets/font.c, fontaa.h, nvx_assets.h, demo_pptx.h, nyc_pptx.h   bitmap and anti-aliased fonts, wallpapers, sample presentations
+-- assets/installer/inst_boot.bin, inst_stage2.bin, inst_boot.cfg   myboot files the installer writes to disk
+-- firmware/rtw8821c_fw.bin               RTL8821CE firmware blob
+-- mod/hello_mod.c, HELLO.MOD             sample runtime-loadable PIE module for the `dlmod` shell command (not built by build.sh)
+-- tools/
|   +-- rune, mcc, mcc-dyn prebuilt Rune compiler (x86-64 Linux ELF); gcc wrappers for static-PIE and dynamic musl programs
|   +-- nvz.py, bin2c.py   compress a program for embedding; turn a file into a C array
|   +-- build_bearssl.sh   compiles BearSSL into build/bearssl/libbearssl.a
|   +-- mkboot.py, mkiso.py   older: MBR+stage2+kernel raw disk (KERNEL_LBA 33); ISO9660 El Torito image with cdboot
|   +-- mkvdi.py, splice.py   raw image -> fixed VirtualBox VDI; older: splice kernel64.bin into a Noovex9 v6.87 container
+-- user/
|   +-- src/               user programs, user_crt0.S, user.h, posix.h, user.ld
|   +-- libc/              nvxlibc: include/ (standard headers, nvx/, sys/, arpa/, netinet/), src/ (libc.c, printf.c, stdlib2.c, stdio2.c, sys_nvx.c, crt0.S; sys_host.c + crt0_host.S for host tests), gen.py
|   +-- musl/              mhello.c, musltest.c, smptest.c, nettest.c, featest.c (static musl); dyntest.c, libnvxdemo.c (dynamic); fetch.c, nvxtls.c (HTTPS client with BearSSL)
|   +-- bin/               prebuilt ELF files (CAPTEST.ELF, LIBCTEST.ELF, ...); not referenced by build.sh
+-- third_party/           bearssl (master 3c04036), bzip2 1.0.8, doom (doomgeneric + shim/), kilo, lua (Lua 5.5 + nvxlibc.c, nvxlua.c), musl 1.2.6, ntfs (ntfs-3g + nvxntfs.c, ntfslibc.c, mkntfs/), sqlite 3.53.4, xz 5.8, zlib 1.3.2, zstd 1.5
+-- build/                 all generated files (git-ignored)
```

The `kernel/tools/mkboot.py` and `mkiso.py` scripts assemble `boot/mbr.S`, `boot/stage2.S` and `boot/cdboot.S` relative to the current directory. Those files now live in `arch/x86_64/boot/`, so the scripts do not run unchanged from `kernel/`. `splice.py` hard-codes `/mnt/user-data/...` paths.

### 2.3 `tools/`

| File | Purpose |
|---|---|
| `tools/check.py` | Automatic checker. `--build` runs `build.sh`, copies `kernel/build/kernel64.bin` to `bootloader/stage/`, runs `make build/boot.bin build/stage2.bin`, then `mkhybrid.py` into `tools/out/check.iso`. Groups: `build`, `static`, `analyze`, `boot`, `usb`, `acpi`, `gpu`, `libc`, `coreutils` (plus Linux-ABI tests). |
| `tools/emulate.py` | Boot emulation of an ISO: the BIOS path (builds the real `fat.c`/`config.c`/`string.c` as a 32-bit host program), the UEFI path (El Torito EFI entry, PE check, runs `BOOTX64.EFI` against simulated services) and `uefi_entry`. |
| `tools/tests/` | Host harnesses and templates: `aml/` (ASL/AML samples), `drivers/drvsim.c`, `fs/`, `libc/` (`ltest.c`, `pfuzz.c`, `sfile.c`), `linux/` (`hh.c`, `lxfuzz.c`, host `cpu.h`/`io.h` stand-ins), `*_template.txt` for USB/ACPI/NVMe/HR/TP/MM tests, `fatck.py`, `isocheck.py`, `pecheck.py`, `bios_emu.c`, `entry_t.c`, `mark_t.c`, `gpu_t.c`. |

## 3. Modules and interface headers

Since v8.96 the kernel is built as separate compilation units (105 since v8.97, which added `shell/vt.c`). Before that, `kernel64.c` included every `.c` file into one translation unit, so every function and variable was `static` and visible to everything after it. The split keeps the same code but gives each file a real interface.

### 3.1 The pieces

| Piece | What it is |
|---|---|
| A unit | One kernel `.c` file, listed in `units.list`. It starts with its own includes and is compiled on its own. |
| Its interface header | `include/nvx/<path of the unit>.h`, for example `net/tcpip.c` has `include/nvx/net/tcpip.h`. It holds what other units may use: the unit's shared types, constants and macros, `extern` declarations of its shared variables, prototypes of its shared functions, and the small `static inline` helpers others call. |
| `include/nvx/base.h` | Included first everywhere: the `u8`..`i64` types, `include/kapi.h`, `include/drivers.h`, `arch/x86_64/cpu.h`, `arch/x86_64/io.h`, `rune/rune_gfx.h`, `third_party/ntfs/nvxntfs.h`, and forward declarations (`struct x;`) of every shared struct so pointers to them need no other header. |
| `include/nvx/kernel.h` | `base.h` plus every interface header, in dependency order. Handy for experiments; kernel units do not use it. |
| `kernel64.c` | Includes `kernel.h` and every unit. It is no longer part of the kernel build; the host harness (`tools/tests/linux/hk_glue.c`) and the strict-warnings check compile it as one file. |

What stays private: anything a unit does not share remains `static` in the `.c` file. About 2,400 of the 4,300 file-scope names in the kernel are private; the rest are shared through headers.

### 3.2 Rules for headers and units

- A unit includes `include/nvx/base.h`, its own interface header and the interface headers of every unit whose names it uses. On average a unit includes 10 module headers; `shell/commands.c` includes the most (70), small library units such as `lib/mem.c` only their own.
- An interface header includes `base.h` and the headers it depends on, so each header compiles on its own. The dependency graph has no cycles.
- Shared variables are defined once, in their unit, and declared `extern` in the header. A variable that two old files both declared with a tentative `static` definition (for example `W` and `H` in `mm/pmm_vmm.c` and `drivers/video/framebuffer.c`) now has one definition, in the unit that used to complete it.
- Arrays whose size came from their initializer get an explicit size in the `extern` declaration (for example `extern const struct nvx_FTREE_t FTREE[20];`), so `sizeof` works in other units.
- Anonymous structs used for shared variables got a tag (`struct nvx_g_acpi_t`) so the definition and the `extern` declaration have the same type.
- Files that were code or data but named `.h` became units: `include/percpu.h` is now `arch/x86_64/percpu.c`, `arch/x86_64/simd.h` is `simd.c`, `lib/js.h` is `lib/js.c`, the crypto files are `lib/crypto/*.c`, and the fonts are `assets/font.c` and `assets/fontaa.c`. Headers that only declare things (`include/kapi.h`, `include/drivers.h`, `arch/x86_64/cpu.h`, `arch/x86_64/io.h`) are unchanged.

### 3.3 Groups of units

| Directory | Units | Role |
|---|---|---|
| `arch/x86_64/` | 10 | per-CPU data and the BKL (`percpu.c`), SSE helpers, ring 3, interrupts, timers, SMP, CPU init |
| `mm/` | 4 | heap, frames and page tables, slab, malloc aliases |
| `lib/` | 10 | memory and string functions, formatting, JavaScript, crypto, TLS, X.509, root store |
| `core/` | 11 | processes and jobs, ELF, scheduler, system calls (native and Linux), log, panic, power, sound, build signature, driver API |
| `drivers/` | 29 | PCI, storage, network cards, Wi-Fi, USB, input, touchpads, ACPI, GPU, RTC, framebuffer |
| `net/` | 6 | TCP/IP, native and Linux sockets, HTTP/TLS, WPA2, Wi-Fi netdev |
| `fs/` | 10 | RAM FS, VFS, Unix layer, NVX4, directory tree, FAT, NTFS glue, packages |
| `gui/`, `shell/`, `apps/`, `init/`, `assets/` | 25 | desktop and window manager, terminal, escape-sequence interpreter and commands, applications, boot UI, installer, fonts |

### 3.4 What this changes for you

- Build speed: a full kernel compile takes about 12 s on 2 cores instead of 20 s, and an incremental one recompiles only what changed. Editing `net/lxsock.c` recompiles 1 unit; editing its header recompiles 4; editing `include/nvx/arch/x86_64/percpu.h` recompiles 20.
- Adding a function another unit should call: write it in your unit without `static`, add its prototype to your unit's header, and include that header in the caller.
- Adding a new unit: create the `.c` file, give it `#include "include/nvx/base.h"` plus the headers it needs, create its header under `include/nvx/`, and add the file to `units.list` (and to `kernel64.c` if the host harness should see it). The order in `units.list` is the link order.
- Missing declarations are compile errors (`-Werror=implicit-function-declaration`), so a missing include shows up at once.
- The kernel image grew by about 24 KB in v8.96, because the compiler can no longer inline across files. Since v8.97 the embedded programs are compressed (section 4.4). v9.08 added the web engine (`build/web_all.o`, about 2.4 MB, Part 13), and the image is now about 6.7 MB, which leaves about 1.3 MB under the 8 MiB limit.

The separately compiled `MODS` drivers (section 4.3) still see only `include/kapi.h` and `include/drivers.h`. They call the non-static `kapi_*` functions defined in `core/kapi.c` and export probe functions (`e1000_probe`, `virtio_net_probe`, `hda_probe`, ...) that fill in `struct nvx_nic { name; mac[6]; send; recv; link; }` or `struct nvx_snd { name; rate; start; free_bytes; write; poll; stop; volume; }`, plus `virtio_blk_read`/`_write`/`_flush`. Lua, Doom and NTFS are also compiled on their own, each with its own flags (section 4.6).

## 4. `build.sh`, step by step

`kernel/build.sh` is a POSIX `sh` script with `set -e`. It runs from `kernel/` and writes everything under `B=build`. It first creates `build/`, `build/user/src`, `build/user/libc`, `build/lua/src` and `build/ntfs`.

### 4.1 Kernel compiler flags

```sh
CFLAGS="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
        -mcmodel=small -DNOOVEX64 -Wall -Wextra -I. -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -Werror=return-type -Werror=shift-negative-value -Werror=empty-body -Werror=duplicated-branches -Werror=logical-op -Wduplicated-branches -Wlogical-op"
```

| Flag | Reason in this kernel |
|---|---|
| `-ffreestanding`, `-fno-stack-protector` | No hosted libc or stack-guard runtime; `lib/mem.c` and `lib/klibc.c` provide the memory and string functions. |
| `-fno-pie`, `-mcmodel=small` | Absolute addresses; the image is linked at `0x100000` and everything sits in the low 2 GiB. |
| `-mno-red-zone` | Interrupts and exceptions run on the current kernel stack, so code below `%rsp` must not be live. |
| `-mno-mmx -mno-sse -mno-sse2` | The compiler must not use vector registers in kernel code (`arch/x86_64/simd.c`). |
| `-fno-asynchronous-unwind-tables` | No `.eh_frame`; the linker script also discards `.eh_frame*`. |
| `-I.` | All includes are relative to `kernel/` (for example `#include "include/nvx/net/tcpip.h"`). |
| `-Werror=...` | Implicit declarations, int/pointer mix-ups, missing returns, empty bodies, duplicated branches and suspicious logical ops all fail the build. `-Wduplicated-branches` and `-Wlogical-op` are GCC-specific. |

### 4.2 Stages

| Stage label | What it does | Output |
|---|---|---|
| `[1/6]`, `[2/6]` | `gcc $CFLAGS -c` on `arch/x86_64/boot/boot64.S`, `isr64.S`, `userspace.S`, `ap_trampoline.S` | `build/boot64.o`, `isr64.o`, `userspace.o`, `ap_trampoline.o` |
| `[3/6]` | `python3 tools/kbuild.py build/units`: compiles every unit in `units.list` whose `.o` is older than the unit or any header it uses (from `gcc -MMD` dependency files), in parallel. A change of `CFLAGS` or `KBUILD_ARGS=--all` recompiles everything. Prints `N of 105 units compiled` | `build/units/*.o`, `build/units/objects.txt` |
| `[3m]` | Compiles each file in `MODS` separately with the same `CFLAGS`; on failure prints `MODULE BUILD FAILED` | `build/mod_<name>.o` to `$MODOBJS` |
| `[3a]` | `tools/rune build rune/rune_gfx.rn --freestanding --target=x86_64-none` | `build/rune_gfx.o` |
| `[3a2]` | `ld -r -b binary` on `firmware/rtw8821c_fw.bin` | `build/rtw_fw.o` |
| `[3a3]` | `ld -r -b binary` on `assets/installer/inst_boot.bin`, `inst_stage2.bin`, `inst_boot.cfg` | `build/inst_boot.o`, `inst_stage2.o`, `inst_cfg.o` |
| `[efi]` | Rebuilds `../bootloader/uefi/BOOTX64.EFI` (falls back to the checked-in file on failure), copies it to `build/inst_efi.bin` and wraps it | `build/inst_efi.o` |
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

### 4.3 Separately compiled drivers (`MODS`)

```sh
MODS="drivers/net/e1000.c drivers/net/rtl8139.c drivers/net/rtl8169.c drivers/virtio/virtio.c drivers/virtio/virtio_net.c drivers/virtio/virtio_blk.c drivers/sound/ac97.c drivers/sound/hda.c"
```

Each is compiled with the kernel `CFLAGS` into `build/mod_$(basename .c).o` and linked statically into the kernel at `[4/6]`. They are not listed in `units.list`, include no `include/nvx/` headers, and are not loaded at runtime. `tools/check.py` reads the `MODS=` line from `build.sh` to run `clang-tidy` on the same files.

### 4.4 Blob embedding

Every binary payload goes through GNU ld's binary input format. Running `ld -r -b binary -o X.o path/file.ext` creates a data section with the symbols `_binary_<path_with_underscores>_start` / `_end`. The helper is `emb(){ ( cd $B && ld -r -b binary -o $1 user/$2.elf ); }`. Because it runs inside `build/` on `user/<name>.elf`, the symbols are named `_binary_user_<name>_elf_start` (for example `_binary_user_nvxbox_elf_start`, `_binary_user_user_demo_elf_start`). The kernel references them from `core/proc/jobs.c`, `core/proc/sched.c`, `core/proc/elf.c`, `shell/commands.c`, `init/installer.c`, `drivers/storage/nvme.c` and `gui/input.c`. Firmware and installer blobs are wrapped from their own directories, which gives `_binary_rtw8821c_fw_bin_start`, `_binary_inst_boot_bin_start`, `_binary_inst_stage2_bin_start`, `_binary_inst_boot_cfg_start` and `_binary_inst_efi_bin_start`.

The `[musl]`, `[musl-shared]` and `[ports]` programs and the shared C library are wrapped with `embz` instead, which first compresses the file with `tools/nvz.py` (`NVZ1` header, original size, raw DEFLATE) into `build/zz/user/<name>.elf`; the kernel inflates it the first time it is run (section 6.6). For the `[u2]`, `[musl]`, `[musl-shared]` and `[ports]` programs the wrapped object is then rewritten with `objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents`. That moves the blob into `.rodata`, which the linker script places before `__immutable_end`. The `[u]` demo blobs are not renamed and stay in `.data`.

### 4.5 User programs

`[u]` freestanding demos. These are built with `UFLAGS="-m64 -ffreestanding -nostdlib -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -Os -Iuser/src"`. Programs: `user_demo`, `user_fork`, `user_app`, `user_thread`, `user_preempt`, `user_ipc`, `user_wmapp`, `user_compdemo`, `user_posix`, `user_mem`. Each is linked as `ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld` with `user_crt0.o`. `user.ld` places `.text`, `.rodata`, `.data`, `.bss` from `0x40000000000`, which is why `-mcmodel=large` is needed. The demo program is embedded as `user_elf.o` and the others as `user_<n>_elf.o`.

`[u2]` nvxlibc programs. `LFLAGS` adds `-nostdinc -isystem user/libc/include -isystem $(gcc -print-file-name=include) -ffunction-sections -fdata-sections -w`. `libc.a` is archived from `libc.o printf.o stdlib2.o stdio2.o sys_nvx.o`, and `crt0.S` is built separately. Programs: `count spin sigdemo upper greet pipetest nvxbox init libc8`. `nvxbox` also links `coreutils.c`. They are linked with `--gc-sections -T user/src/user.ld` as `crt0.o prog.o [coreutils.o] libc.a`.

`[musl]` Linux-ABI programs. If `build/musl/lib/libc.a` or `rcrt1.o` is missing, `third_party/musl/configure --prefix=$M --disable-shared --disable-wrapper CFLAGS="-fPIE -O2"` runs in `build/musl-obj`, followed by `make -j$(nproc)` and `make install`. Then `mhello`, `musltest`, `smptest`, `nettest` and `featest` are compiled with `-fPIE -O2 -nostdinc -isystem $M/include` linked as static PIE, then `strip`ped and embedded:

```sh
gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o
```

`[musl-shared]`. If `build/musl-sh/lib/libc.so` is missing, musl is configured a second time with `--enable-shared --disable-static --syslibdir=/lib` in `build/musl-shobj` and installed with `make install-libs`. The stripped `libc.so` is embedded compressed as `user_libc_so_bin.o`; it is the dynamic linker the kernel loads for dynamic programs (section 6.10). `user/musl/libnvxdemo.c` is built as `libnvxdemo.so`, turned into a C array by `tools/bin2c.py`, and compiled into `dyntest`, which is linked with `-pie -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1`.

`[ports]`. `NVX_MUSL` and `NVX_MUSL_SH` are exported and `tools/mcc-dyn` is used as the compiler. It passes through to `gcc -fPIE -nostdinc -isystem $NVX_MUSL/include` for `-c/-S/-E/-M/-MM`, and otherwise links a dynamic PIE with `Scrt1.o crti.o ... -lc libgcc crtn.o` from the shared musl. (`tools/mcc` is the static-PIE equivalent and is still there.) The `port()` helper builds `build/ports-dyn/<name>` only if it does not exist yet, then strips it. Delete `build/ports-dyn/<name>` to force a rebuild. BearSSL is compiled once into `build/bearssl/libbearssl.a`; `fetch` is relinked on every build because its sources are part of the tree.

| Port | Sources / notable defines |
|---|---|
| `sqlite3` | `sqlite/shell.c sqlite3.c`, `-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0` |
| `gzip` | `zlib/test/minigzip.c` + zlib sources |
| `bzip2` | bzip2 library + `bzip2.c`, `-D_FILE_OFFSET_BITS=64` |
| `xz` | explicit list of liblzma (with the multi-threaded coder) + `xz/` sources, `-std=gnu11 -include third_party/xz/nvx_config.h` |
| `zstd` | `lib/{common,compress,decompress}/*.c` + `programs/*`, `-DZSTD_MULTITHREAD -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM ...` |
| `kilo` | `third_party/kilo/kilo.c`, `-std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE` |
| `fetch` | `user/musl/fetch.c`, `nvxtls.c`, `build/bearssl/libbearssl.a` |

### 4.6 Lua, Doom, the web engine and NTFS

- Lua (`[lua]`). `LUAF` is the kernel flag set *with* `-msse -msse2 -mfpmath=sse`, plus `-w -Ithird_party/lua/inc -Ithird_party/lua/src`. Every `src/*.c` plus `nvxlibc.c` and `nvxlua.c` is merged with `ld -r` into `lua_all.o`. The kernel reaches it through `extern const char* nvx_lua_run(const char*)` (declared in `mm/pmm_vmm.c`).
- Doom (`[3b]`). `build_doom.sh` compiles every `*.c` except `test_host.c`, plus `webp_stub.c`, with `-O2 -ffreestanding -mno-red-zone -nostdinc -Ishim -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400`. SSE is not disabled. The objects are combined with `ld -r`, and `objcopy --keep-global-symbol` leaves only `doomgeneric_Create`, `doomgeneric_Tick`, `DG_ScreenBuffer`, `dsh_set_output`, `dsh_set_file`, `dsh_set_exit`, `dsh_set_heap` and `webp_decode` global. `build.sh` moves `doom_blob.o` into `build/` and deletes `obj/` and `doom_blob_all.o`.
- Web engine (`[web]`). The engine files are compiled with `WEBF`: the kernel flag set with SSE allowed, `-O2`, freestanding, and musl's headers for C library declarations. QuickJS and the double and float functions of musl's `libm` are compiled the same way. After `ld -r`, `objcopy --keep-global-symbol=nwe_entry` hides everything else, and the step prints any symbol that is still undefined. Details are in section 13.1.
- NTFS (`[3c]`). `NTFS_CFLAGS` is the kernel flag set plus `-nostdinc -isystem third_party/ntfs/compat -DHAVE_CONFIG_H -Ulinux -U__linux__ -U__linux -U__gnu_linux__ -fmacro-prefix-map=third_party/= -w`. It compiles `ntfs/lib/*.c`, `nvxntfs.c` and `ntfslibc.c`, then `mkntfs/*.c` with `-Dmain=mkntfs_main` (as `mk_*.o`).

### 4.7 Link, flat binary, size stamp and signature

```sh
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack -T arch/x86_64/kernel64.ld \
   -o $B/kernel64.elf <OBJS> $MODOBJS $NTFSOBJ <BLOBS>
```

`OBJS` is `boot64.o isr64.o userspace.o ap_trampoline.o`, followed by the 108 unit objects from `build/units/objects.txt`; `BLOBS` is `rune_gfx.o rtw_fw.o inst_boot.o inst_stage2.o inst_cfg.o inst_efi.o`, the 10 `[u]` blobs, the 9 `[u2]` blobs, the 4 musl blobs, the port blobs (including `user_qjs_bin.o`), `lua_all.o doom_blob.o web_all.o`.

`objcopy -O binary build/kernel64.elf build/kernel64.bin` emits the bytes from `0x100000` up to `__file_end`. `.bss` is `NOLOAD`, so it is not in the file.

Step `[6/6]` is an inline Python script that edits `build/kernel64.bin`:

1. `assert len(b)<=8388608` ("kernel ... > 8 MiB (myboot FAT load limit)").
2. `struct.pack_into('<I', b, 24, len(b))` writes the file size into the Multiboot header's `load_end_addr` slot. `boot64.S` explains that this lets a size-aware loader read exactly the right number of sectors. `init/installer.c` reads the same word (`*(u32*)(0x100000+24)`).
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
| 8 | `version` | `kcheck_run()` in `init/installer.c` prints it |
| 32 | `sha256` (immutable part) | `sig_self_check()` (`hash_off = sig_off+8+16+4+4`), `kimg_check()` (`ho=so+32`) |
| 80 | `uefi_entry` | `uefiboot.c` (`k+i+8+16+4+4+32+16`) jumps here |
| 88 | `sha256_full` | `kimg_check()` (`fo=so+88`) |
| 120 | `bss_end` | `uefiboot.c` (`k+i+120`) protects `[0x1300000, bss_end)`; it falls back to `0x0B000000` if the value is out of range |

At boot, `sig_self_check()` (`init/main.c`) re-hashes `[0x100000, __immutable_end)` with the hash field zeroed and compares. It hashes only `.text`/`.rodata`/`.sig` because "writable globals in .data change the instant the kernel starts". The result goes in `g_sig_ok`; `g_sig_enforce` (0 by default) decides whether a mismatch halts on the skull screen.

### 4.8 C++ in the kernel

Since v9.82 the kernel is written in C, C++ and assembly. A unit in `units.list` that ends in `.cpp` is compiled by `tools/kbuild.py` with `g++` and `CXXFLAGS`. `build.sh` makes `CXXFLAGS` from `CFLAGS` (the C-only `-Werror=` options removed) plus `-std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables`, so C++ kernel code has no exceptions, no RTTI and, like the C units, no SSE.

| Piece | Where | What it does |
|---|---|---|
| Runtime | `lib/cxxrt.cpp` | `operator new`/`delete` (all sizes) on `kmalloc`/`kfree`, `__cxa_pure_virtual`, `__cxa_atexit`, `__dso_handle`, `cxx_run_ctors()` |
| Global constructors | `kernel64.ld`, `kernel64_mb.ld`, `init/main.c` | `.init_array` sits after `.rodata` (inside the signed part of the image) between `__init_array_start` and `__init_array_end`; `kmain` calls `cxx_run_ctors()` right after `heap_init`, so constructors may allocate |
| Helpers | `include/nvx/lib/cxx.h` | placement new, `nvx::KBuf<T>` (a `kmalloc` buffer freed by its destructor), `nvx::min`/`max`/`clamp`, `nvx::EdgeKeys` |
| C headers | `include/nvx/...` | compile unchanged as C++ inside `extern "C" { }` |

SSE code in C++ (floating point) goes into a separate object like the Quake one: `apps/r3d/build_r3d.sh` builds `r3d_blob.o` freestanding with SSE, `-mstackrealign` and `-nostdinc`, keeps only the `r3d_*` symbols global, and fails the build if it needs anything from the kernel except `memset`, `memcpy` or `memmove`. Its interface (`apps/r3d/r3d.h`) uses only integers, because kernel code cannot pass floats.

## 5. Linker scripts

### 5.1 `arch/x86_64/kernel64.ld`

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

### 5.2 `arch/x86_64/kernel64_mb.ld`

This script uses the same addresses and symbols, but it is meant to stay an ELF so that myboot can load the program segments, with `__bss_start` exported for `boot64_mb.S` to zero `.bss`. It has no `/DISCARD/` and no `ASSERT`. Neither it nor `boot64_mb.S` is referenced by `build.sh`, `check.py` or any other script in the tree.

### 5.3 Size limits that interact with the layout

| Limit | Value | Source |
|---|---|---|
| Flat kernel file | at most 8 MiB | `build.sh` assert; `stage2.c` `fat_read_file(e->kernel, scratch, 0x800000, &ksz)` |
| Kernel via UEFI | at most `0x1000000-0x100000` (`KERNEL_MAX`, 15 MiB; 8 MiB in v9.08 to v9.10, `0x5C0000` before) | `bootloader/uefi/uefiboot.c`; both entry paths set `RSP = 0x1080000` |
| Kernel end at link time | `__file_end` at most `0x1000000` | `kernel64.ld`, `kernel64_mb.ld` `ASSERT` |
| Immutable region for self-check | at most `0x800000` | `sig_self_check()` |
| Installer boot snapshot | `KSNAP_MAX 0x800000` (matches the 8 MiB file limit; was 3 MiB before v8.95) | `init/installer.c` |
| `.bss` end | at most `0x09800000` | `kernel64.ld` `ASSERT` |

## 6. Bootloader build and image tools

### 6.1 `bootloader/Makefile`

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

For a flat (non-ELF) kernel, `stage2.c` loads the file to scratch `0x1000000` and builds a Multiboot info block with the framebuffer. If `/doom/DOOM1.WAD` exists and is larger than 1,000,000 bytes, stage2 loads it to `0x0E000000` as module 0. It then calls `raw_boot(src, size, mbi)`. `boot.cfg` (`bootloader/stage/boot.cfg`) uses `timeout=2`, `default=0`, `video=1280x1024`, `bootlog=4`, `entry=NoovexOS 64`, `kernel=/boot/kernel64.bin`, `cmdline=noovexos`.

### 6.2 UEFI loader (built by `build.sh` step `[efi]`)

```sh
gcc -I. -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -m64 -Os -fno-jump-tables -Werror=implicit-function-declaration -c uefiboot.c -o uefiboot.o
gcc -ffreestanding -c dummyreloc.S -o dummyreloc.o
ld -nostdlib -znocombreloc -shared -Bsymbolic -T efi.ld uefiboot.o dummyreloc.o -o uefiboot.so
objcopy -j .text -j .data -j .reloc --target=pei-x86-64 --subsystem=10 uefiboot.so BOOTX64.EFI
```

`efi_main` uses `EFIAPI __attribute__((ms_abi))` and opens `\KERNEL64.BIN` or `\boot\kernel64.bin`. It finds `uefi_entry` and `bss_end` through the `NVX64SIG` block, picks a GOP mode, calls `ExitBootServices` (up to 8 tries), copies the kernel to `0x100000` and the boot info to `0x5000`/`0x6000`/`0x7000`, and calls `uefi_entry`.

### 6.3 `mkhybrid.py`: the hybrid ISO

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

### 6.4 `mkimage.py` and the other image scripts

| Script | Output |
|---|---|
| `bootloader/mkimage.py boot.bin stage2.bin disk.img [file ...]` | Raw MBR disk: stage1 at LBA 0, stage2 from LBA 1 (at most 64 sectors), one FAT32 partition at LBA 2048 of `PART_SECTORS = 131072` sectors (64 MiB) with CHS fields from `chs()`. Files go in the root by basename. Used by `bootloader/Makefile`. |
| `myboot-dualfirmware/mkefi.py efi.elf out` | Hand-rolled PE32+ (`MZ`, machine `0x8664`, image base `0x400000`) from `objcopy -O binary` of `.text/.rodata/.data`, using `efi_main` and `__image_end`. |
| `myboot-dualfirmware/mkimage.py ... --efi BOOTX64.EFI` | That variant's disk image with an EFI loader (Makefile target `build/disk.img`; `run-uefi` uses `qemu-system-x86_64 -bios OVMF.fd`). |
| `kernel/tools/mkvdi.py raw.img out.vdi [MB]` | Fixed VirtualBox VDI (signature `0xBEDA107F`, version 1.1, type 2 = fixed, default 16 MB). |
| `mkuefi.py` | Not in the repository; it exists only as a document in the attached "noovex" project. It reads `BOOTX64.EFI` and writes `noovex7-uefi.img`: a GPT disk (protective MBR, primary + backup headers) with a 16 MiB FAT16 ESP at LBA 2048 holding `/EFI/BOOT/BOOTX64.EFI`, then re-parses the FAT to verify. |

## 7. Toolchain requirements

Derived from the commands the scripts run (`tools/README.md` lists Linux with gcc, binutils, Python 3 and GNU coreutils for the checker):

| Tool | Used for |
|---|---|
| `gcc` (GCC specifically) | Kernel and modules (`-m64`), bootloader (`-m32 -march=i386`), UEFI loader, musl `configure`, user programs. `-Werror=duplicated-branches` / `-Wlogical-op` are GCC warnings; `gcc -print-file-name=include` and `-print-libgcc-file-name` are used for freestanding headers and libgcc. |
| binutils: `ld`, `objcopy`, `ar`, `nm`, `strip` | Linking, binary blob wrapping (`-b binary`), flat/PE conversion (`--target=pei-x86-64`), `libc.a`, symbol lookup in the signature step. `readelf`/`objdump` are also used by `check.py`. |
| `make` | Bootloader, musl. |
| `python3` | Signature/size step, `mkhybrid.py`, `mkimage.py`, `check.py`, `emulate.py`. |
| POSIX `sh` | `build.sh`, `build_doom.sh`, `tools/mcc`; `nproc` is optional (fallback 4). |
| x86-64 Linux host | `kernel/tools/rune` is a dynamically linked x86-64 Linux ELF executable. |
| optional | `clang-tidy` (`check.py` analyze group), `qemu-system-i386` / `qemu-system-x86_64` + OVMF (Makefile `run` targets). |

Full build sequence (what `check.py --build` does):

```sh
cd source/kernel && sh build.sh
cp build/kernel64.bin ../bootloader/stage/kernel64.bin
cd ../bootloader && make build/boot.bin build/stage2.bin
python3 mkhybrid.py build/boot.bin build/stage2.bin out.iso NOOVEXOS \
  boot/kernel64.bin=stage/kernel64.bin boot.cfg=stage/boot.cfg README.txt=stage/README.txt
```

## 8. Versioning

There is no central version macro; the version is a literal in several places. Current occurrences of `8.96`:

| File | Use |
|---|---|
| `core/buildsig.c` | `g_buildsig.version = "8.96"` (printed by `kcheck_run()`) |
| `fs/vfs.c` | `version` leaf: `"NoovexOS 64 v8.95 (from-scratch, Unix-like)\n"` |
| `fs/unixfs.c` | `/sys/kernel/osrelease` to `"8.96\n"` |
| `core/linux.c` | `uname` for Linux-ABI programs: sysname `NoovexOS`, release `8.95`, version `NoovexOS 64 v8.95`, machine `x86_64` |
| `apps/files_classic.c` | About text `"Version 8.96"` |
| `apps/file_explorer.c` | header comment and `"Version 8.96  -  from scratch"` |
| `apps/small_apps.c` | neofetch-style lines `"NoovexOS 64 v8.95 (x86-64)"`, `"v8.95"` |
| `tools/tests/linux/hh.c` | expects `"Hello from musl libc on NoovexOS 8.96 (x86_64)"` from `mhello` |

Older strings still say `8.38`: `shell/terminal.c` (`"OS: NoovexOS 64 v8.38"`), `user/src/nvxbox.c` (`"NoovexOS %s 8.38 nvx64 x86_64\n"`) and the example comment in `core/buildsig.c`. A version bump means editing every row of the table above (and ideally these three) together; `tools/tests/linux/hh.c` will fail if `core/linux.c` and the test disagree.
