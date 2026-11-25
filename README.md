# NoovexOS 64

NoovexOS is a from-scratch, Unix-like 64-bit operating system for x86-64. It combines a freestanding C kernel, a desktop environment, native applications, Linux ABI compatibility, a POSIX-like shell, and a package-oriented user-space into a bootable operating system image.

The project is primarily implemented in C, with supporting assembly, Python build tools, JavaScript, C++ components, shell scripts, and third-party source code.

> Hardware support and advanced features are developed and tested alongside QEMU-based test suites. Not every listed feature has been verified on physical hardware.

## Highlights

- **64-bit x86 kernel** with boot-time assembly entry, SMP, virtual memory, scheduling, ELF loading, interrupts, timers, and a native syscall interface
- **Unix-style environment** with virtual terminals, users and permissions, shell pipelines and redirects, `/proc`, `/sys`, and Linux ABI compatibility
- **Desktop environment** with a window manager, taskbar, Start menu, settings, file explorer, notifications, lock screen, and window snapping
- **Built-in applications** including Writer, Calculator, Browser, Files, Task Manager, Settings, Logs, Paint, Camera, Voice Recorder, games, and system utilities
- **Storage support** for NVX4, FAT/VFAT, exFAT, ext4, NTFS, RAM filesystems, AHCI, NVMe, SD/eMMC, USB storage, and volumes
- **Networking** with TCP/IP, sockets, HTTP, DHCP, DNS, firewall support, Wi-Fi, and wired and wireless device drivers
- **Hardware support** for USB, PS/2, HID, audio, PCI, ACPI, graphics, touchpads, game controllers, cameras, fingerprint readers, and laptop controls
- **Linux compatibility** with glibc, musl, ELF programs, Linux syscalls and dynamic linking, plus a staged `/lib`, `/usr`, and `/opt/gnu` sysroot
- **Application ecosystem** including musl utilities, Python, Bash, GCC, SQLite, gzip, bzip2, xz, zstd, Kilo, QuickJS, FFmpeg, NetSurf, LibreOffice, SSH, cron, man pages, and package tooling
- **Web platform** with a custom HTML/CSS layout and rendering engine, JavaScript support, media support, and a browser application
- **Security and maintenance** with kernel hardening, signed build images, password hashing, ASLR, firewalling, crash reports, update tooling, and package repositories

## Building

The main build is driven by [`build.sh`](build.sh). The script assembles the boot and kernel components, builds the user-space and third-party programs, stages the Linux-compatible sysroot, creates the kernel image, and stamps and signs the result.

Typical build requirements include:

- A Linux or Unix-like build environment
- GCC and binutils, including 64-bit x86 support
- Python 3
- Bash, Make, `ld`, `objcopy`, `ar`, and standard Unix utilities
- Optional tools for additional components such as Rust/Cargo

Run:

```sh
./build.sh
```

Build output is placed in `build/`, including `build/kernel64.elf`, `build/kernel64.bin`, and the staged system image data.

Use the repository documentation for the boot and testing workflow used by the project.

## Running

NoovexOS supports BIOS/MBR and UEFI boot paths. The source for the boot components is under [`boot/`](boot), and the installer assets are assembled by the build script.

The repository's test infrastructure and host-side tools are under [`tools/tests/`](tools/tests), while QEMU and hardware test workflows are described in [`docs/`](docs).

## Source layout

| Directory | Purpose |
|---|---|
| [`arch/`](arch) | x86-64 CPU, boot, interrupt, SMP, timer, and user-mode code |
| [`boot/`](boot) | BIOS and UEFI bootloader sources and installer assets |
| [`core/`](core) | Kernel core, processes, scheduler, initialization, power, and security |
| [`mem/`](mem) | Physical memory, virtual memory, heaps, slabs, and swapping |
| [`drivers/`](drivers) | Storage, USB, input, video, network, audio, ACPI, and platform drivers |
| [`fs/`](fs) | Virtual filesystem, NVX4, FAT, ext4, NTFS, package, and volume handling |
| [`net/`](net) | TCP/IP, sockets, HTTP, Wi-Fi, and firewall networking code |
| [`sys/`](sys) | Native and Linux-compatible interfaces, eBPF, and system calls |
| [`shell/`](shell) | Terminal, virtual terminals, command processing, and shell utilities |
| [`apps/`](apps) | Desktop applications and built-in games |
| [`graphics/`](graphics) | Framebuffer, themes, window manager, desktop, and GPU presentation code |
| [`usr/`](usr) | User-space programs, demos, libc, Linux compatibility, and packages |
| [`web/`](web) | Browser engine for HTML, CSS, layout, painting, JavaScript, and media |
| [`distro/`](distro) | Distribution profile and package configuration |
| [`ports/`](ports) | Ported software and package integration |
| [`third_party/`](third_party) | Vendored libraries and programs |
| [`docs/`](docs) | Architecture, release handoffs, porting notes, and development reports |
| [`tools/`](tools) | Build, staging, signing, image, testing, and analysis tools |

## Documentation

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — system architecture reference
- [`docs/architecture-src/`](docs/architecture-src) — architecture source documents and release changes
- [`docs/handoff-v11.10.md`](docs/handoff-v11.10.md) — the latest v11.10 development handoff retained in the repository
- [`docs/MOVES.md`](docs/MOVES.md) — source and module relocation history
- [`docs/PORTING-WORKLIST.md`](docs/PORTING-WORKLIST.md) — planned ports and licensing review
- [`apps/README.md`](apps/README.md) — application directory overview
- [`arch/README.md`](arch/README.md) — architecture directory overview
- [`assets/README.md`](assets/README.md) — bundled assets and firmware notes

## Status

NoovexOS is an actively developed experimental operating system. It includes substantial kernel, desktop, network, storage, compatibility, and application functionality, but some hardware paths remain simulator-tested or incomplete. Back up important data before testing disk-writing, installer, sleep, firmware, or hardware-driver features.

## License

NoovexOS source code is licensed under the GNU General Public License v3.0. See [`LICENSE`](LICENSE).

Redistributable firmware and third-party components may have separate licences. Their licence files are included in [`assets/`](assets) and staged into the generated system image by the build script.
