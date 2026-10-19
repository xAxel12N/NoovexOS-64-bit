# NoovexOS / myboot — portable 64-bit-capable bootloader

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE) [![Build Status](https://img.shields.io/badge/build-manual-lightgrey)](#) [![Stars](https://img.shields.io/github/stars/xAxel12N/NoovexOS-64-bit?style=social)](https://github.com/xAxel12N/NoovexOS-64-bit/stargazers)

NoovexOS (myboot) is a compact, staged bootloader that can boot ELF32/ELF64 kernels on BIOS and UEFI systems. It demonstrates bringing the CPU into x86-64 long mode, mounting a FAT32 partition, parsing a simple `boot.cfg`, and handing control to a kernel — all written in plain C and x86 assembly.

Highlights
- Small staged BIOS + optional UEFI path (single disk image supports both).
- ELF64 handoff into long mode (paging enabled, low 4 GB identity mapped).
- FAT32 reader, menu, config parsing, optional VBE framebuffer support.
- COM1 serial mirror for reproducible logs in QEMU and on hardware.
- Minimal example kernel included to verify the handoff.

Quick demo

- Build and run in QEMU:
  - Prereqs (Debian/Ubuntu): sudo apt install build-essential gcc-multilib qemu-system-x86 python3
  - Build: `make`
  - Run BIOS image: `make run`
  - Run UEFI (OVMF required): `make run-uefi`
- There is a prebuilt disk image at `build/disk.img` you can flash to USB (dd / Rufus/Etcher).

Why this project?
- Educational, focused, and easy to extend into a production-grade bootloader path.
- Good foundation for OS dev experiments and teaching CPU long-mode transitions.
- Clean, well-documented staged loader architecture you can fork and extend.

Quickstart (2 minutes)

1. Clone

   git clone https://github.com/xAxel12N/NoovexOS-64-bit.git
   cd NoovexOS-64-bit

2. Install (Debian/Ubuntu)

   sudo apt update
   sudo apt install build-essential gcc-multilib qemu-system-x86 python3

3. Build & Run

   make
   make run

If you want to flash a ready image instead:

- `dd if=build/disk.img of=/dev/sdX bs=4M conv=fsync`  (replace `/dev/sdX`)

What’s in this repo
- source/: bootloader source (stage1, stage2, FAT, ELF loaders, VBE, serial)
- example/: tiny 64-bit example kernel (kernel_entry.S, kernel.c, kernel.ld)
- mkimage.py, Makefile: build + packaging helpers
- build/disk.img: optional prebuilt image

Documentation & deeper reading
- source/bootloader/README.md — full bootloader design and boot sequence
- source/myboot-dualfirmware/README.md — combined BIOS+UEFI image notes

Contributing — quick
We welcome help! See CONTRIBUTING.md for setup, coding style, and PR workflow. Good first contributions:
- Add GitHub Actions to build the repo and run a smoke QEMU test.
- Add a README demo GIF and screenshots.
- Add automated image releases on GitHub Releases.

License

This project is licensed under the GNU General Public License version 3 (GPLv3). See the `LICENSE` file for details.

Thanks for checking this out — fork, star, and contribute. If you want, request a specific "good first issue" and I’ll create it for you.
