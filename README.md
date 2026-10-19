# NoovexOS-64-bit
I had another repository but that was on my other account and i got logged out from it so yeah this is my new repository for NoovexOS 64 bit os

## Source layout

| Folder | What is in it |
|---|---|
| `arch/` | x86-64 CPU code: boot entry, interrupts, SMP, timers, user mode |
| `boot/` | BIOS and UEFI boot loaders, the hybrid ISO tools, and myboot |
| `core/` | kernel start-up (`core/init/`), processes and scheduling, logging, power |
| `sys/` | system calls: native NoovexOS and the Linux layer |
| `mem/` | page tables, frames, heap, slab |
| `fs/` | VFS, NVX4, FAT, NTFS, packages |
| `drivers/` | ACPI, PCI, input, network, sound, storage, USB, video (fb, Intel, NVIDIA), VirtIO |
| `graphics/` | window manager, desktop, themes |
| `net/` | TCP/IP, HTTP, printing, Wi-Fi |
| `shell/` | terminal and the `nsh` shell |
| `apps/` | desktop apps by category |
| `lib/` | kernel libraries: base, C++ runtime, compression, images, JavaScript, crypto |
| `web/` | the web engine |
| `usr/` | user space: libc, programs, musl and glibc programs, packages |
| `lang/` | Rune code |
| `assets/` | fonts, game data, firmware |
| `include/` | interface headers, same layout as the source |
| `third_party/` | code from other projects |
| `tools/` | build helpers, `check.py` tests, emulator |
| `docs/` | sources of ARCHITECTURE.md |

Build: `bash build.sh` in this folder (needs gcc, g++, gawk, python3). The kernel image ends up in `build/kernel64.bin`; ARCHITECTURE.md Part 1 has the full steps.
