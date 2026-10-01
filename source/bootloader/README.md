# myboot

A portable, 64-bit-capable Multiboot bootloader written from scratch in C and
x86 assembly.

`myboot` boots a BIOS PC, brings the CPU up through 16-bit real mode and 32-bit
protected mode into **64-bit long mode**, reads a FAT32 partition off the disk
through the BIOS, parses a text config file, draws a countdown boot menu,
loads an **ELF64 (x86-64) or ELF32** kernel, and jumps to it. A 64-bit kernel is
entered with the CPU already in long mode and paging enabled; a 32-bit Multiboot
kernel is entered under the standard [Multiboot 1](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
contract, the same handoff GRUB implements.

This is a real, growable bootloader built on the same staged architecture GRUB
uses for its BIOS build (a tiny asm stage 1, a larger stage 2 with drivers, a
menu, paging, and the long-mode switch). It is **not** a drop-in GRUB replacement
-- see [Scope](#scope-and-honest-limitations) for exactly what it does and does
not do. It is a solid foundation you can extend toward that goal.

The whole project builds with nothing but `gcc`, `binutils`, and `python3`. No
NASM, no mtools, no root, no loopback mounts.

## How it boots

Boot is staged, because the BIOS hands you only 512 bytes to start with.

1. **Stage 1** (`src/stage1.S`, 512 bytes) is the master boot record. The BIOS
   loads it to `0x7C00`. It uses BIOS `INT 13h` extended read (LBA) to pull stage
   2 off the disk into memory at `0x8000`, and jumps to it. That is the only job
   small enough to fit in a boot sector.

2. **Stage 2** starts in 16-bit real mode (`src/stage2_entry.S`). While the BIOS
   is still reachable it enables and *verifies* the A20 line (trying the BIOS,
   the fast-A20 port, and the keyboard controller in turn), queries the memory
   map with `INT 15h, EAX=E820`, loads a GDT, sets `CR0.PE`, and far-jumps into
   32-bit protected mode. From there everything is flat-model 32-bit C.

   Throughout, all loader text output is mirrored to the **COM1 serial port**
   (115200 8N1). That gives you a boot log you can capture from real hardware or
   from QEMU even when the screen shows nothing -- which is the safe way to bring
   up anything graphical later.

3. Stage 2 does its disk I/O through the **BIOS (`INT 13h`)**, not a hardware
   driver, so it reads whatever device the firmware booted from -- USB stick, IDE,
   SATA, or NVMe. Because BIOS calls need real mode and the loader runs in 32-bit
   protected mode, each read briefly drops to real mode, issues an `INT 13h`
   extended read into a low bounce buffer, and returns to protected mode
   (`src/bios.S`, wrapped by `src/disk.c`). With that it reads the MBR partition
   table, finds the first FAT32 partition, mounts it (`src/fat.c`), and reads
   `/boot.cfg`.

4. It parses the config (`src/config.c`), draws the **menu** with an RTC-driven
   timeout countdown, and reads the keyboard from the PS/2 controller. On
   selection it reads the chosen kernel file into a scratch buffer.

5. It inspects the ELF class byte and branches:
   - **ELF64 (x86-64):** it first confirms via `CPUID` that the CPU actually
     supports PAE and long mode (and fails with a clear message if not), loads
     the program segments (`src/elf64.c`), then performs the **long-mode
     transition** (`enter_long_mode` in `src/stage2_entry.S`) -- builds 4-level
     page tables that identity-map the low **4 GB** with 2 MB pages, enables PAE
     (`CR4.PAE`), sets `EFER.LME`, enables paging (`CR0.PG`), loads a 64-bit GDT,
     and far-jumps into 64-bit code. It then jumps to the kernel entry point with
     the CPU in long mode. If `boot.cfg` requested a video mode, the loader has
     already switched to a VBE linear framebuffer (right after reading the config)
     and drawn the menu on it, and it reports that framebuffer to the kernel (see
     Video below); if the mode set fails it stays in text mode and boots anyway.
   - **ELF32:** it loads the segments (`src/elf.c`), builds the Multiboot info
     structure, sets `EAX=0x2BADB002`, and jumps in 32-bit protected mode -- the
     standard Multiboot 1 contract.

In both cases it builds the same Multiboot **information structure** (memory map,
command line, loader name) and passes it to the kernel.

## The handoff contract

**32-bit kernels** are entered exactly as Multiboot 1 specifies: 32-bit protected
mode, paging off, `EAX = 0x2BADB002`, `EBX` = physical address of the Multiboot
information structure.

**64-bit kernels** are entered in long mode (paging on, low 1 GB identity-mapped)
with:

- `RDI` = `0x2BADB002` (the Multiboot magic)
- `RSI` = physical address of the Multiboot information structure

which is the System V AMD64 calling convention for `kmain(magic, mbi)`. For code
that prefers to read them Multiboot-style, `RAX` also holds the magic and `RBX`
holds the info pointer. The information structure itself uses the standard 32-bit
Multiboot layout -- all of its addresses point into low memory, which is valid
because that memory is identity-mapped.

Note this 64-bit entry is *not* literal Multiboot 1: that specification mandates a
32-bit machine state, so no bootloader can hand a kernel a 64-bit entry and still
call it Multiboot 1 compliant. `myboot` carries the same magic and the same
information structure into long mode, which is the practical thing a 64-bit kernel
wants.

## Memory map

While stage 2 runs, physical memory is laid out like this:

| Address       | Contents                                              |
|---------------|-------------------------------------------------------|
| `0x00000500`  | BIOS disk address packet (INT 13h reads)              |
| `0x00004000`  | VBE controller + mode info blocks                     |
| `0x00004300`  | Real-mode register block (INT 10h thunk)              |
| `0x00005000`  | 8x16 VGA ROM font copy (graphics text)                |
| `0x00000FF0`  | E820 entry count (dword)                              |
| `0x00001000`  | E820 entries (24 bytes each)                          |
| `0x00007C00`  | Real-mode stack during BIOS calls                     |
| `0x00008000`  | Stage 2 image (code + data + BSS)                     |
| `0x00020000`  | BIOS read bounce buffer (64 sectors / 32 KB)          |
| `0x00070000`  | PML4 (4-level page-table root)                        |
| `0x00071000`  | PDPT (4 entries)                                       |
| `0x00072000`  | 4 page directories (2048 x 2 MB = 4 GB identity map)  |
| `0x00076000`  | Handoff scratch (kernel entry + info pointer)         |
| `0x00080000`  | Protected-mode / long-mode stack top (grows down)     |
| `0x00100000`  | Kernel load address (segments land at/above 1 MB)     |
| `0x01000000`  | Kernel file read scratch (raw file, before parsing)   |

The 32-bit GDT is flat (null, ring-0 code `0x08`, ring-0 data `0x10`). The 64-bit
GDT adds a long-mode code segment (`L` bit set) and a data segment, also at `0x08`
and `0x10`.

## Prebuilt image

If you just want to boot it, flash the provided `myboot-disk.img` directly (Rufus
in **DD Image mode**, or `dd`/balenaEtcher) -- no build step needed. The menu
title shows `myboot 0.4`; if you don't see that version, you're booting an older
image. On the menu the loader also prints a `config:` line to the serial port
showing the parsed `timeout` (0 = waits for you; it never auto-boots unless you set
`timeout=N`).

## Building

You need `gcc` able to target both 32-bit and 64-bit (`gcc -m32` and `gcc -m64`),
GNU `ld`, `objcopy`, and `python3`. On Debian/Ubuntu:

```
sudo apt install build-essential gcc-multilib
```

Then:

```
make
```

That produces `build/disk.img`, a 68 MB raw disk image containing the MBR, stage
2, and a FAT32 partition holding the example 64-bit kernel and `boot.cfg`.

## Running

The image is a raw hard disk. Boot it in an emulator:

```
make run
```

which runs:

```
qemu-system-i386 -drive format=raw,file=build/disk.img -m 128 -serial stdio
```

The `-serial stdio` part echoes the loader's serial log to your terminal. You
should see the `myboot` menu and countdown, then the example kernel printing
that it was booted into 64-bit long mode, along with the upper-memory figure
reported to it.

It also runs under Bochs, VirtualBox, and VMware, and on real 64-bit hardware --
see the next section for booting from a USB stick. Give the machine at least
~32 MB of RAM (the kernel scratch buffer sits at 16 MB).

## Booting from a USB stick (real hardware)

Because all disk I/O goes through the BIOS, `myboot` reads whatever device the
firmware hands it, so a USB stick works the same as a hard disk. Two things have
to be right, and both are firmware settings rather than code:

**1. Write the raw image to the stick (this erases it).**

- **Linux/macOS:** `sudo dd if=build/disk.img of=/dev/sdX bs=4M conv=fsync`
  (replace `/dev/sdX` with the stick; double-check the device name).
- **Windows, Rufus:** select `disk.img`. When Rufus asks, choose **DD Image
  mode**, *not* ISO mode -- this is a raw disk image with its own MBR, and ISO
  mode would rewrite the layout. Then Start.
- **Any OS:** balenaEtcher or Win32 Disk Imager also work; they write raw images
  by default.

**2. Boot the machine in legacy/BIOS mode, not UEFI.**

`myboot` is a BIOS/MBR bootloader. On a UEFI-only machine it will not run at all,
which is a very common reason a homemade bootloader "does nothing" off USB. In
your firmware setup:

- Enable **CSM** / **Legacy Boot** / **Legacy USB** (wording varies by vendor).
- Disable **Secure Boot**.
- In the boot menu, pick the USB entry *without* a `UEFI:` prefix (the legacy
  entry).

When it boots you will briefly see `myboot 0.2`, the BIOS boot-drive number, and
the FAT partition LBA, then the menu. If it stops on one of those lines with an
error, that tells you exactly which step failed (drive read, partition scan, FAT
mount, or kernel load).

## Configuration

`boot.cfg` lives in the root of the FAT32 partition. It is a simple line-based
`key=value` format. Blank lines and lines starting with `#` are ignored.

```
timeout=5
default=0

entry=Example Kernel
kernel=/KERNEL.ELF
cmdline=quiet loglevel=3

entry=Example Kernel (verbose)
kernel=/KERNEL.ELF
cmdline=verbose debug
```

- `timeout` -- `0` (or omitting the line) makes the menu **wait for you to choose**
  and never auto-boot. Set `timeout=N` (seconds) to count down and then boot the
  `default` entry automatically; any keypress cancels that countdown.
- `default` -- zero-based index of the entry to boot on timeout.
- `entry` -- begins a new menu entry; the value is its display title.
- `kernel` -- path to the kernel file for the current entry (absolute, 8.3 name).
- `cmdline` -- command line passed to the kernel via the Multiboot info structure.
- `video` -- optional, e.g. `video=1024x768`. Requests a VBE linear-framebuffer
  graphics mode (32- or 24-bpp) and hands it to the kernel. Omit it (the default)
  to boot in VGA text mode. This is a global key, not per-entry.

The menu is a clean centered box: a title, a bordered frame with the entry titles
in bright white, the selected one shown as a blue highlight bar, and a single help
line beneath. It **waits for you to choose** -- it does not boot anything until you
press Enter (or a number key to pick and boot an entry directly); set `timeout=N`
if you want it to auto-boot the default instead. The keyboard buffer is flushed
before the menu waits, so a leftover keystroke from the BIOS boot-device menu can
no longer trigger an accidental boot. The box renders the same in text mode and on
the framebuffer.

If a machine still boots without input, check that the `boot.cfg` actually on the
disk has `timeout=0` (an older copy may still say `timeout=5`).

## Video (optional linear framebuffer)

By default `myboot` runs in VGA text mode and hands the kernel no framebuffer. If
you put `video=WIDTHxHEIGHT` in `boot.cfg`, then right before jumping to the
kernel it asks the BIOS (VBE) for a matching linear-framebuffer mode at 32 or 24
bits per pixel, sets it, and fills in the Multiboot information structure's
framebuffer fields (setting flag bit 12):

- `framebuffer_addr` -- physical address of the linear framebuffer
- `framebuffer_pitch` -- bytes per scan line
- `framebuffer_width`, `framebuffer_height` -- pixels
- `framebuffer_bpp` -- bits per pixel
- `framebuffer_type` = 1 (RGB), plus the red/green/blue field positions and sizes

The design is fail-safe: mode enumeration and setting happen only after all of
the loader's text output, and if no matching mode is found or the BIOS refuses,
`myboot` leaves the screen in text mode, does not set flag bit 12, and boots
normally. So a bad `video=` line can never leave you with a black screen -- and
because the loader mirrors its log to the serial port, you can watch the mode-set
succeed or fail.

When a mode is set, the GRUB-style boot menu and the loader's messages are drawn on
the framebuffer too, using the PC's own 8x16 VGA font -- which myboot fetches from
the video ROM at runtime (`INT 10h, AX=1130h`) rather than embedding a font. The
same centered, bordered menu box appears in text mode and in graphics, just
rendered in pixels in the latter.

## myboot terminal

Press **C** at the boot menu to drop into an interactive command line (like GRUB's
`c` shell). It works in text mode and on the framebuffer, and mirrors to serial.
Commands:

```
help          list commands
ls            list files on the boot partition
cat <file>    print a file (e.g. cat boot.cfg)
mem           show the BIOS E820 memory map
boot [n]      boot menu entry n (or the default if omitted)
clear         clear the screen
reboot        restart the machine
ver           show the version
exit          return to the boot menu
```

## Boot log

When you boot an entry, myboot shows a scrolling boot log for a few seconds before
handing off, paced by a real timer (the PIT). The duration is `bootlog` seconds
from `boot.cfg` (default 5; `bootlog=0` skips it). It's a cosmetic sequence that
reports the entry, kernel path and size, then a series of `[ OK ]` init lines.

## Writing a compatible kernel

Build your kernel as a static ELF executable, put it on the FAT partition with an
8.3 name, and point a `boot.cfg` entry at it. `myboot` picks 32-bit vs 64-bit from
the ELF class automatically.

`example/` contains a minimal, complete **64-bit** kernel:

- `example/kernel_entry.S` -- a 64-bit entry stub. `myboot` enters it in long mode
  with the magic in `RDI` and the info pointer in `RSI`, so it sets up a stack and
  calls `kmain`.
- `example/kernel.c` -- a `kmain(magic, mbi)` that verifies the magic and prints to
  VGA text memory.
- `example/kernel.ld` -- links the kernel as an ELF64 executable at 1 MB.

For a **32-bit** kernel instead, build a 32-bit ELF that carries a Multiboot 1
header within its first 8 KB (4-byte aligned); `myboot` will load it and hand off
per Multiboot 1. Either way, load your kernel at or above 1 MB.

## Project layout

```
src/stage1.S         512-byte MBR: loads stage 2 via BIOS INT 13h LBA
src/stage2_entry.S   real-mode setup, A20, E820, GDT (with 16-bit descriptors
                     for the disk thunk), PM switch, MB handoff, and the
                     long-mode transition (paging, PAE, EFER, 64-bit GDT)
src/stage2.c         orchestration: mount FAT, read config, menu, load, hand off
src/bios.S           real<->protected round trips: INT 13h reads, INT 10h, ROM-font fetch
src/disk.c/.h        sector reader over the BIOS thunk (chunking + retry)
src/serial.c/.h      COM1 serial console (mirrors all loader output, 115200 8N1)
src/vbe.c/.h         VBE mode enumeration + linear-framebuffer mode set
src/fb.c/.h          framebuffer text console (8x16 glyphs, drawn from the ROM font)
src/fat.c/.h         FAT32 reader (partition scan, cluster chains, 8.3 lookup)
src/elf.c/.h         ELF32 program-header loader
src/elf64.c/.h       ELF64 program-header loader
src/multiboot.c/.h   Multiboot header scan, a.out-kludge load, info-struct builder
src/config.c/.h      boot.cfg parser
src/vga.c/.h         VGA text console and menu drawing
src/kbd.c/.h         PS/2 keyboard polling
src/rtc.c/.h         CMOS/RTC seconds, for the menu countdown
src/io.h             port I/O inlines
src/string.c/.h      freestanding memcpy/memset/strcmp/... (no libc)
src/types.h          fixed-width integer types
linker.ld            stage 2 link script (one flat load region at 0x8000)
example/             a minimal 64-bit kernel to boot
mkimage.py           builds the FAT32 filesystem + partitioned disk image
Makefile             the whole build
```

The freestanding loader is compiled `-ffreestanding -nostdlib` and deliberately
avoids anything that would emit a call into the compiler runtime (`libgcc`) -- in
particular there is no 64-bit division or modulo anywhere, since those become
external `__udivdi3`/`__umoddi3` calls that would not link. The long-mode
trampoline lives in the 32-bit stage-2 object: the file is assembled as a 32-bit
ELF, but the landing pad after the far jump is written in a `.code64` block, so
its 64-bit instructions are encoded correctly while the object stays linkable
alongside the 32-bit C.

## Scope, and honest limitations

`myboot` implements the core architecture GRUB uses on BIOS systems -- staged
boot, a real disk driver and filesystem, a menu, the long-mode switch, and the
Multiboot handoff. It does not attempt to match GRUB's breadth. GRUB is roughly a
quarter-century and hundreds of thousands of lines of work; what is here is a
clean, correct, extensible core. The deliberate gaps:

- **BIOS only.** There is no UEFI support. `myboot` assumes a legacy BIOS with
  `INT 13h`/`INT 15h`/`INT 10h` available at start.
- **BIOS disk services required.** Reads go through `INT 13h`, so any disk the
  firmware can boot works (USB, IDE, SATA, NVMe). The flip side is that a legacy
  BIOS (or CSM) must be present -- there is no native disk driver and no UEFI
  support (see below).
- **FAT32 only.** No FAT16, ext2/3/4, or others. Boot files must use 8.3 names
  (long file names in directory entries are skipped).
- **Loads ELF kernels.** 64-bit kernels are loaded as ELF64 and entered in long
  mode; 32-bit kernels are loaded per Multiboot 1. There is no Multiboot 2, no
  Linux `bzImage` protocol, and no chainloading of other operating systems.
- **4 GB identity map.** Long mode identity-maps the low 4 GB with 2 MB pages --
  plenty for a kernel at 1 MB and the loader's own structures, but a kernel that
  wants to run above 4 GB or higher-half must build its own page tables early.
- **No boot-time modules, no networking, no scripting, no signature
  verification.** These are all things GRUB does and this does not. A linear
  framebuffer and a graphical menu *are* supported (opt-in via `video=`, rendered
  with the VGA ROM font), but there are no themes, background images, or
  TrueType/scalable fonts.
- **Modest size assumptions.** The kernel file is read to a fixed 16 MB scratch
  address, so the machine needs enough RAM to hold it there (~32 MB is plenty for
  a small kernel), and kernels are expected to load at or above 1 MB.

Everything above is a place the design leaves room to grow rather than a dead end.
Stage 2 uses about 7.5 KB of its 32 KB budget.

## Natural next steps toward GRUB-class capability

If you want to push this further, in rough order of value:

1. A higher-half mapping and more than 4 GB of identity map (1 GB pages where
   supported), for kernels that run high or need all of physical memory mapped.
2. Multiboot 2 support, and a Linux `bzImage` boot path.
3. A second filesystem backend (ext2 is the usual next one) behind the same read
   interface `stage2.c` already uses.
4. An AHCI/SATA driver, then loading kernels and modules over it.
5. Multiboot modules (loading additional files and reporting them to the kernel).
6. Menu themes: background images, custom fonts, and colours (the framebuffer and
   a graphical menu already exist; this is styling on top).
7. A UEFI stage 1 alongside the BIOS one, sharing the stage-2 core.
8. Chainloading other bootloaders/operating systems.
