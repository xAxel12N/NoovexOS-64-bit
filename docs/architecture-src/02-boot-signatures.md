# Part 2: Boot chain, kernel entry and kernel signatures

This part follows a NoovexOS 64 boot from firmware to the main loop, then covers how the kernel image is signed and checked. Every statement is taken from the source tree. Paths are relative to `source/` unless noted. Numbers from the shipped build come from `build/kernel64.elf` / `kernel64.bin` (5,110,512 bytes).

## 2.1 The boot paths at a glance

| Path | Firmware loads | Loader | Kernel file | Kernel entered at | CPU mode at entry |
|---|---|---|---|---|---|
| BIOS (production) | MBR at LBA 0 of the hybrid image | myboot `boot/src` (stage1 + stage2) | `/boot/kernel64.bin` (flat binary) | `0x100000` (`_start`) | 32-bit protected mode, `EAX=0x2BADB002`, `EBX`=Multiboot info |
| BIOS from CD | El Torito default entry, HDD emulation of the whole image | the same myboot | same | same | same |
| UEFI (production) | `\EFI\BOOT\BOOTX64.EFI` | `boot/uefi/uefiboot.c` | `\KERNEL64.BIN` or `\boot\kernel64.bin` | `uefi_entry` (address read from the NVX64SIG block) | 64-bit long mode |
| Third-party Multiboot (e.g. GRUB) | n/a | any Multiboot 1 loader | flat binary with Multiboot header | `0x100000` | 32-bit PM, `EAX=0x2BADB002` |
| Legacy custom loaders | `arch/x86_64/boot/mbr.S`, `stage2.S`, `cdboot.S` | own asm | raw sectors | `0x100000` with `EAX=0` | 32-bit PM (see 2.5: no longer layout-compatible) |

All working paths converge on one contract: a framebuffer description at physical `0x5000`, the kernel image at `0x100000`, then `kmain()` in `core/init/main.c`.

The repository has no `bootx64.c` or `mkuefi.py`. The UEFI loader is `boot/uefi/uefiboot.c`, built by `build.sh` step `[efi]`. The dual-firmware myboot variant has its own `boot/myboot/src/efi.c` and `mkefi.py`.

## 2.2 Boot media: the hybrid image (`boot/mkhybrid.py`)

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

## 2.3 BIOS path: myboot (`boot/src`)

### Stage 1 (`stage1.S`, 512 bytes, linked at `0x7C00`)

Stage 1 sets DS/ES/SS to 0, puts SP at `0x7C00` and saves DL (the boot drive). It then issues one `INT 13h AH=42h` with a DAP for 64 sectors from LBA 1 to `0000:8000` and far-jumps to `0x8000` with DL preserved. If the read fails it prints `myboot: disk error` and halts.

### Stage 2 entry (`stage2_entry.S`, linked at `0x8000` by `linker.ld`)

`stage2_start` runs in real mode and does the following:

1. Saves DL to `g_boot_drive`.
2. Enables A20 in four steps, re-checking after each one with `check_a20`, a wrap-around test at `0000:0500` vs `FFFF:0510`. The order is already-on check, `INT 15h AX=2401h`, fast-A20 port `0x92`, then the keyboard controller (`a20_kbc`). If all four fail it prints `myboot: A20 line error`.
3. Collects the E820 memory map (`do_e820`): up to 64 entries of 24 bytes at `0x1000`, with the count stored at `0x0FF0`.
4. Loads a 5-entry GDT: null, 32-bit code `0x08`, 32-bit data `0x10`, and 16-bit code/data for the real-mode thunks. It then sets `CR0.PE` and jumps to `pm_entry`.

`pm_entry` loads the data selectors, sets ESP to `0x80000`, zeroes stage2's own `__bss_start..__bss_end` and calls `stage2_main()`. Disk reads from protected mode go through `bios.S`, which drops to real mode, issues `INT 13h` into a bounce buffer at `0x20000` and returns. The DAP is kept at `0x0500`.

### `stage2_main()` (`stage2.c`)

1. Initialises the serial port (COM1) and the VGA console, then prints the boot drive.
2. Reads the MBR and picks a partition in three passes. The first pass takes an active (`0x80`) entry of type `0x0B`/`0x0C`. The second takes any entry of those types. The third takes any entry with a non-zero LBA. If none matches it fails with `no partition found in MBR`.
3. Calls `fat_mount(part_lba)`, then reads `/boot.cfg` into an 8 KiB buffer (8191 bytes plus NUL) and parses it with `cfg_parse`. A config with no entries fails with `boot.cfg has no entries`.
4. If the config sets `video=WxH` or `video=auto`, it calls `bios_get_font()`, which copies the VGA ROM 8x16 font to `0x5000` for `fb.c`. With `video=auto` it first reads the screen's EDID through VBE function 4F15h (buffer at `0x4400`, header and checksum checked) and tries the size of the first detailed timing, the panel's native mode, if it is at most 1920x1080. Otherwise, or if that mode is not in the VBE list, it tries `vbe_set_mode(W,H)` (1920x1080 for `auto`), falling back to 1920x1080, 1600x900, 1366x768, 1280x1024, 1280x800, 1024x768, 800x600 and 640x480 (skipping the size it already tried). On success it switches the console to the framebuffer (`fb_init`, `vga_set_graphics(1)`).
5. Runs `menu_run()` and reads the chosen kernel file to a scratch buffer at `0x1000000` (16 MiB), maximum `0x800000` bytes. `fat_read_file` fails if the file is larger than that.
6. Branches on the first four bytes (see "Kernel format dispatch" below).

### `boot.cfg` format (`config.c` / `config.h`)

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

### The menu (`menu_run`)

The menu draws a box titled `myboot 0.5` with one row per entry. Before it accepts keys it waits for two RTC second changes while flushing the keyboard, and a key is only accepted after the keyboard has been seen idle once (`armed`).

| Key | Action |
|---|---|
| Up / Down | Moves the selection and cancels the countdown. |
| Enter | Boots the selected entry. |
| `1`-`9` | Boots that entry directly. |
| `c` | Opens the loader terminal (`term.c`, see below). |

The terminal (`term.c`) supports `help`, `ls`, `cat`, `mem` (prints the E820 map from `0x0FF0`/`0x1000`), `clear`, `reboot`, `ver`, `exit` and `boot`. The countdown decrements on each RTC second change and boots the selection when it reaches 0.

### Kernel format dispatch and validation

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

## 2.4 `boot/myboot/` (stand-alone variant)

This is myboot 0.6 with a UEFI application (`src/efi.c`, PE built by `mkefi.py`). Compared with `boot/src/stage2.c` it differs in four ways:

- Partition selection takes the first entry of type `0x0B`, `0x0C` or `0xEF`.
- There is no VBE fallback list.
- It has no flat-binary branch: a non-ELF kernel fails with `kernel is not an ELF image`.
- The UEFI side opens `\KERNEL.ELF` (max 64 MiB), requires ELF64/x86-64, `AllocatePages`-reserves the PT_LOAD span at its physical address, builds a Multiboot info block from GOP and the UEFI memory map, retries `ExitBootServices` up to 8 times, and jumps with `RDI/RAX=0x2BADB002`, `RSI/RBX=mbi`.

`arch/x86_64/boot/boot64_mb.S` is the matching kernel entry. It copies the mbi framebuffer fields to `0x5000` before zeroing `.bss`, zeroes `0x5010`/`0x5018`, zeroes `.bss`, switches to a 128 KiB stack in `.bss` and calls `kmain`. `build.sh` does not assemble it, so the shipped kernel cannot use this contract.

## 2.5 Legacy custom loaders in `arch/x86_64/boot/`

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

## 2.6 UEFI path (`boot/uefi/uefiboot.c`)

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

## 2.7 The boot-info contract (fixed low memory)

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

## 2.8 Kernel entry: `arch/x86_64/boot/boot64.S`

### Image layout

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

### Multiboot header (offset 4)

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

### `start32` (BIOS / Multiboot entry)

1. `cli; cld`. If `0x501C != 'WAD!'`, zero `0x5010` and `0x5018`.
2. If `EAX == 0x2BADB002`: copy mbi +88/+92 to `0x5000/0x5004`, +96 to `0x5008`, +100 to `0x500C` (u16), +104 to `0x500E` (u16) and +108 to `0x5020`. If `mods_count` (mbi+20) is non-zero, copy the first module (`mod_start..mod_end`) to `0x0E000000` with `rep movsl/movsb`, then store the size at `0x5018` and `0x0E000000` at `0x5010`.
3. Zero 6 pages from `pml4` (`pml4`, `pdpt`, `pd0..pd3`). Set `PML4[0]->pdpt` and `PDPT[0..3]->pd0..pd3` (present|writable). Fill 2048 PDEs with `phys|0x83`, which identity-maps 0-4 GiB with 2 MiB pages.
4. Set `CR4.PAE`, load CR3=`pml4`, set `EFER.LME` (MSR `0xC0000080` bit 8), `lgdt gdt64_ptr`, set `CR0.PG|PE`, then `ljmp $0x08, long_entry`.
5. `long_entry` loads DS/ES/SS/FS/GS = `0x10`, sets RSP = `0x1080000`, clears RBP and calls `kmain`. If `kmain` returns, the CPU halts.

`.bss` other than the page tables is not cleared here. `kmain` does that.

### `uefi_entry` (64-bit entry from `BOOTX64.EFI`)

1. `lgdt gdt64_ptr64(%rip)`, then reload CS with `pushq $0x08; pushq label; lretq`. Load data selectors `0x10`, set RSP=`0x1080000`, `cli; cld`. Draw marker 2.
2. Zero all of `__bss_start..__bss_end` with `rep stosq`. The CPU is still on the loader's tables, which live outside the kernel areas. Draw marker 3.
3. Build `PML4[0]->pdpt`, `PDPT[0..3]->pd0..pd3`, and 2048 2 MiB PDEs for 0-4 GiB.
4. If the framebuffer at `0x5000` is in `[4 GiB, 512 GiB)`, map two GiB starting at its 1 GiB slot through `pdfb` (2 pages).
5. Load CR3=`pml4`, draw marker 4 and call `kmain`.

`tools/emulate.py` (`emulate_kernel_entry`) extracts this code from `boot64.S` text and runs it against test memory. `check.py` (`gdt64_check`) disassembles `uefi_entry` and verifies three things: the `lgdt` operand is a 10-byte pointer whose base is a `gdt*` symbol, the limit is at least `0x17`, and selector `0x08` is a present L=1 code segment.

### GDT

`gdt64`: null; `0x08` = `0x00209B0000000000` (64-bit code, P, DPL0); `0x10` = `0x0000930000000000` (data). There are two pseudo-descriptors: `gdt64_ptr` (6 bytes, used from 32-bit code) and `gdt64_ptr64` (10 bytes, used from 64-bit code). The per-CPU GDT/TSS (`PCPU->gdt`, `g_tss`) is installed later by the kernel.

## 2.9 `kmain()` step by step (`core/init/main.c`)

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

## 2.10 Main loop and state machine

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

## 2.11 Boot menu, recovery, setup and splash

- `core/init/bootmenu.c` holds the recovery environment. It has a menu stack (`g_recstack`, depth 5) over four menus: `RM_MAIN` (Continue, Restart, Troubleshoot, Startup Settings, Command Prompt, Turn off), `RM_TROUBLE`, `RM_ADVANCED` and `RM_STARTUP`, with actions `RA_*` handled in `recovery_act()`. Continue to `ST_SETUP`. Safe Mode sets `g_safemode=1`. Command Prompt sets safe mode and `g_rec_openterm=1`. Startup Diagnostics (`g_recmode=1`) lists CPU, RAM, display, IDE, NIC and USB. Reset Display Mode re-reads the `0x5000` block. Restart and shutdown call `power_reboot()`/`power_off()`. Startup Repair, System Restore, Reset PC and UEFI Settings only show info panels (`g_recmode=2`). Startup Repair prints "Kernel image: valid" unconditionally; it does not consult `g_sig_ok`.
- The synthwave boot menu (`bootmenu_present/input/activate`, in `core/init/installer.c`) has five items: Desktop to `ST_SETUP`, Safe Mode to `ST_SETUP` with `g_safemode`, Recovery to `ST_RECOVERY`, Install to `ip_open(ST_BOOTMENU)`, and Boot Configuration (shows resolution, CPU and NIC, and toggles safe mode).
- `core/init/setup.c` is the first-run wizard with `NSU`=7 pages (language, keyboard, timezone, personalisation, Noovex account, storage, done). `su_apply()` commits the user name, accent, theme and locale choices, enables cloud files if signed in, and calls `nvx_settings_save()`. It also contains the login screen (`login_present/input`).
- `core/init/splash.c`: `splash_boot()` is the black wordmark hold called from `kmain`. `synthwave_scene()` (gradient, grid, sun, wordmark) is the background for the boot menu.

## 2.12 Kernel signatures and integrity

### The NVX64SIG block (`core/buildsig.c`)

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

### Stamping (`build.sh`, step `[6/6]`)

After `objcopy` produces `build/kernel64.bin`, an inline Python script:

1. Asserts `len(b) <= 8388608` ("myboot FAT load limit") and writes `len(b)` at offset 24.
2. Finds the first `NVX64SIG` in the file (`off`). It runs `nm build/kernel64.elf` to get `uefi_entry` and `__immutable_end`, and sets `imm_sz = __immutable_end - 0x100000`. If the symbol is missing, `imm_sz` falls back to `len(b)`; it is also capped at `len(b)`.
3. Writes `build_epoch` (off+24), `image_size` (off+28) and `uefi_entry` (off+80).
4. Zeroes both `sha256` (off+32) and `sha256_full` (off+88).
5. `sha256_full = SHA256(entire file)`. This covers the file with both hash fields zero, but with the size stamp, epoch, image size, `uefi_entry`, `bss_end` and all of `.data` in place.
6. `sha256 = SHA256(b[0:imm_sz])`, computed after `sha256_full` was written, with only the `sha256` field zero.
7. Prints `build signature stamped @ off: sha256=... imm_size=... file=... uefi_entry=0x...`. `check.py` requires this text for its build check.

### Exactly which bytes are hashed, and why

```
file offset 0                      imm_sz (= __immutable_end-0x100000)        len(file)
|-- .text | .aptramp | .rodata | .sig ----------------|-- .data -----------------|
|<--------------- sha256 (sha256 field = 0) --------->|
|<-------------- sha256_full (sha256 and sha256_full fields = 0) -------------->|
```

- `sha256` covers the immutable region: code, read-only data and the signature block itself. Because `sha256_full` sits inside `.sig` and was filled before this hash was taken, `sha256` also commits to the whole-file digest. The kernel can recompute it at runtime because these bytes never change after load. The comment in `sig_self_check` explains why `.data` is left out: "writable globals in .data change the instant the kernel starts, so hashing them would always fail."
- `sha256_full` covers the whole file as written to disk. It can only be checked against an unmodified copy of the file (offline tools, or the installer's boot-time snapshot), not against live memory.
- `.bss` is never hashed; it is not in the file.

### Runtime check: `sig_self_check()`, `g_sig_ok`, `g_sig_enforce` (`core/init/main.c`)

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

### Installer integrity check (`core/init/installer.c`)

- `inst_snapshot_data()` (step 3 of `kmain`) takes `klen` from the size stamp at `0x100000+24`. If `klen` is outside `[4096, KSNAP_MAX]` it uses `__file_end-0x100000` instead, and if that is still above `KSNAP_MAX` it sets `klen=0`. It then copies `klen` bytes into `g_kimg_snap`. `KSNAP_MAX` is `0x800000` (8 MiB), the same as the build limit, so any kernel that builds can be captured. (Before v8.95 it was 3 MiB; the 5 MB kernel did not fit, `g_kimg_len` stayed 0 and the installer always stopped.)
- `kimg_check()` redoes both build-time hashes on the snapshot. It works on a temporary copy from `kmalloc(klen)` (it used to be a second static 3 MiB buffer), zeroes `sha256` and hashes `[0, imm)`, compared with `g_buildsig.sha256`. It then also zeroes `sha256_full` and hashes `[0, klen)`, compared with `g_buildsig.sha256_full`. It returns 1 (no hashes) if `sha256_full` is all zero, and -1 if the allocation fails.
- `ip_run()` refuses to install if the size stamp is outside `[4096, 0x800000]`, if `g_kimg_len` is 0 or differs from the stamp ("The kernel image could not be captured at boot."), or if either hash differs ("...kernel code / data loaded at boot is not the original build.").
- The `kcheck` command (`kcheck_run`) prints the stamp, the captured length and the code size, plus the hash results or `boot image: not captured`. It also lists up to 12 4 KiB pages of the immutable region whose live bytes differ from the snapshot. The hash line also goes to the kernel log (`kcheck: boot image: code MATCHES whole file MATCHES` on a good v8.95 boot).

### Offline verification (`tools/emulate.py`, `tools/check.py`)

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

### Summary: who checks what

| Checker | When | Immutable hash | Full hash | On failure |
|---|---|---|---|---|
| `sig_self_check()` | every boot, `sig` command | yes (live memory) | no | skull screen; R restarts, C continues with `g_sig_ok=0` |
| `kimg_check()` | installer, `kcheck` | yes (snapshot) | yes (snapshot) | installer aborts with a message |
| `uefiboot.c` | UEFI load | no | no | only requires a valid `uefi_entry` |
| myboot stage2 | BIOS load | no | no | - |
| `emulate.py` / `check.py` | host test | if matching ELF | yes | test FAIL |

## 2.13 Size limits along the chain

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

## 2.14 Gotchas

- The header comment of `boot64.S` still documents the old `0x5000` layout (pitch at `0x5004`, bpp at `0x500A`). The code and `kmain` use the layout in 2.7.
- `stage2_main` prints `myboot 0.3` while the menu title says `myboot 0.5`.
