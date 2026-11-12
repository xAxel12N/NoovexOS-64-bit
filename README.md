# NoovexOS (64-bit)

[![CI](https://github.com/xAxel12N/NoovexOS-64-bit/actions/workflows/ci-and-setup.yml/badge.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/actions/workflows/ci-and-setup.yml) [![Contributing](https://img.shields.io/badge/contributing-welcome-brightgreen)](CONTRIBUTING.md)

NoovexOS is a Unix-like 64-bit operating system focused on learning, experimentation, and teaching OS development. The project includes a custom x86_64 kernel, a bootloader, a graphical desktop environment, device drivers, filesystem support, networking, and a set of built-in applications.

This repository is a broad OS codebase rather than a single small toy kernel: it includes kernel subsystems, userland components, package concepts, and a boot flow that can be assembled into a bootable disk image.

## Project status

NoovexOS is actively under development and is best understood as an educational operating system platform with a large and evolving kernel tree. The codebase shows ongoing work in:

- x86_64 CPU and interrupt handling
- memory management and paging-related structures
- filesystem and VFS layers
- networking and socket support
- graphics and desktop UI
- drivers for storage, PCI, USB, Wi-Fi, sound, and input
- built-in apps and a lightweight desktop shell

## Repository layout

- `arch/` — architecture-specific x86_64 code: boot, CPU, interrupts, SMP, timers, user-mode entry
- `apps/` — built-in desktop apps by category: system, office, internet, media, games, accessories
- `boot/` — BIOS bootloader pipeline and image generation
- `boot/myboot/` — EFI-capable branch of the bootloader with `BOOTX64.EFI`
- `core/` — kernel core, scheduler, init, crash handling, process management, accounts
- `drivers/` — device drivers for AHCI/NVMe, USB, PCI, Wi-Fi, framebuffer, audio, input, and more
- `fs/` — filesystem implementations and VFS infrastructure
- `graphics/` — desktop theme, effects, UI, and rendering pieces
- `include/` — public kernel headers and interfaces
- `lib/` — base libraries, crypto, compression, JS, and runtime helpers
- `mem/` — heap, paging, physical memory management, slab allocators, swapping
- `net/` — TCP/IP stack, sockets, HTTP, Wi-Fi networking
- `shell/` — terminal and shell functionality
- `sys/` — native syscall interfaces and Linux compatibility layers
- `third_party/` — vendored libraries and utilities used by the OS
- `tools/` — build and utility scripts
- `web/` — web-related code and infrastructure
- `assets/` — fonts, firmware blobs, and embedded assets

## Features in the current tree

- 64-bit x86_64 kernel
- custom boot flow and disk image generation
- interrupt and CPU startup code
- serial and framebuffer console support
- desktop UI and window manager concepts
- input support for PS/2, touchpads, and HID devices
- filesystem abstractions including FAT, NTFS, ramfs, and VFS
- networking stack with TCP/IP, sockets, and HTTP support
- Linux compatibility code and syscall layer
- built-in apps including settings, terminal, browser, editor, calculator, games, and a classic file explorer
- embedded third-party code for crypto, compression, and networking libraries

## Requirements

The project is designed for a Linux host. A typical build environment is:

- Ubuntu/Debian-based Linux
- `build-essential`
- `gcc`, `ld`, `objcopy`, `make`
- `python3`
- `nasm`
- `qemu-system-x86` or `qemu-system-x86_64`

Install example:

```bash
sudo apt update
sudo apt install build-essential nasm python3 qemu-system-x86 git -y
```

## Build

The repository has a main build script and bootloader-specific makefiles. The general pattern is:

```bash
git clone https://github.com/xAxel12N/NoovexOS-64-bit.git
cd NoovexOS-64-bit
bash build.sh
```

The build script orchestrates the kernel, userland modules, drivers, and generated boot assets. The result is expected under a `build/` directory and may be run with QEMU using a raw disk image or the boot targets in `boot/` and `boot/myboot/`.

Common boot-related targets in the tree:

```bash
cd boot
make run

cd boot/myboot
make run
make run-uefi
```

The Makefiles in `boot/` and `boot/myboot/` create a raw disk image and invoke QEMU with a BIOS or UEFI configuration, respectively.

## Running

Example using the generated image from the boot directory:

```bash
qemu-system-i386 -drive format=raw,file=build/disk.img -m 128 -serial stdio
```

Example with UEFI support:

```bash
qemu-system-x86_64 -bios OVMF.fd -drive format=raw,file=build/disk.img -m 256 -serial stdio
```

## Notes for contributors

We welcome improvements to the kernel, drivers, documentation, and tooling. Good starting areas include:

- documenting exact build steps for each host environment
- improving the bootstrapping and installation flow
- refining the userland package and filesystem tools
- adding tests for drivers and hardware support
- polishing the desktop and application layer

See `CONTRIBUTING.md` for the project contribution workflow and `CODE_OF_CONDUCT.md` for the expected community standards.

## License

This project is licensed under the GNU General Public License v3.0. See `LICENSE` for details.

---

NoovexOS is a large, educational operating-system project with a kernel-first architecture and a growing desktop/userland stack. The current repository reflects that breadth: boot code, x86_64 CPU support, drivers, networking, graphics, and user-facing applications all live together in one codebase.
