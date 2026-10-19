# NoovexOS test tools

Run from `source/`. Needs Linux with gcc, binutils (`ld`, `nm`, `readelf`, `objdump`), Python 3 and GNU coreutils.

## `python3 tools/check.py`: automatic bug checker

Runs every check and prints a summary; exits with status 1 if anything fails.

- `--build` rebuilds the kernel, libc, user programs and UEFI loader, then a fresh ISO in `tools/out/check.iso`.
- `--iso IMAGE` tests a specific image instead.
- `--quick` uses smaller random test sizes.
- `--sanitize` runs the driver tests under AddressSanitizer + UndefinedBehaviorSanitizer and the libc/tool tests with UBSan traps.
- `--only a,b,c` runs selected groups: `build,static,boot,usb,acpi,gpu,nvme,touchpad,memory,aml,timers,linux,fs,nvidia,tls,fetch,qemu,net,drivers,analyze,libc,coreutils`.
- nvidia: builds `tests/nvidia/nvsim.c`, which includes the real NVIDIA driver files and runs them against a simulated TU116 (registers, PRAMIN and VRAM, falcon IMEM/DMEM ports, a synthetic VBIOS, a GSP that checks every queue element and answers the RPCs, and a copy engine that walks the GPU page tables). Uses the built-in firmware in `kernel/assets/nvidia` (unpacked with Python's lzma), or the linux-firmware `nvidia` folder if `NVX_NVFW` points to it.

Groups:
- build: compiles everything, rejects implicit function declarations, builds the ISO.
- static: kernel compiles with zero warnings under extra strict flags, kernel build signature, UEFI loader has no relocations and uses the Microsoft x64 ABI, PE structure, no raw keyboard-controller resets outside `core/power.c`, no inline assembly outside `arch/`, USB memory regions do not overlap.
- analyze: clang's static analyzer (null dereferences, uninitialized values, memory misuse) plus bug-prone pattern checks over the kernel; skipped if `clang-tidy` is missing.
- boot: runs `emulate.py` (below).
- usb: generates a test from the current `drivers/usb/xhci.c` and runs it against recorded USB devices (Logitech Unifying receiver, vendor-first receiver, USB stick).
- acpi: generates a test from the current `core/power.c` and runs it against synthetic firmware tables.
- libc: builds nvxlibc with a Linux syscall backend and compares printf (random formats), a broad libc test, and strtod (hard rounding cases) with glibc.
- coreutils: compares nvxbox tools with GNU coreutils on the same files.
- tls: builds the kernel's X.509/RSA/ECDSA code for the host and checks the embedded roots, every Mozilla root self-signature, and generated chains, host names, dates and CertificateVerify signatures (`tests/tls/`; the generated cases need Python `cryptography`).
- fetch: builds BearSSL and the `fetch` HTTPS client for the host (under the sanitizers with `--sanitize`) and runs it against local TLS 1.2 servers from `tests/tls/fetchpeer.py`: verified downloads, redirects, chunked bodies, unknown roots, wrong names, expired certificates.
- qemu: boots the image with `-smp 4` and runs `smp on`, `smptest`, `musltest`, `featest` (threads, timers, server sockets, terminal, limits), `dyntest` (dynamic linking) and `smp off` (needs `NVX_QEMU` or `qemu-system-x86_64` on the path; `NVX_QEMU_BIOS` sets `-L`).
- net: with `NVX_NET_TAP=1` (root and `/dev/net/tun`), starts `tests/net/netpeer.py` (tap device, DNS, HTTP, echo, TLS servers) and runs `nettest`, `http`, `https` and `fetch` checks in QEMU, and connects from the host to a server program (`featest listen`) in the guest.

The root store `kernel/lib/crypto/nvxroots.c` is generated from `kernel/lib/crypto/roots/mozilla-cacert.pem` with `python3 tools/mkroots.py kernel/lib/crypto/roots/mozilla-cacert.pem kernel/lib/crypto/nvxroots.c`. At boot the kernel also writes these roots to `/etc/ssl/nvx-anchors.pem` for programs.

## `python3 tools/emulate.py IMAGE.iso`: boot emulation

- BIOS: checks the MBR and stage2 placement, then builds the real `bootloader/src/fat.c`, `config.c` and `string.c` with stage2's own flags as a 32-bit program (partition selection is copied from `stage2.c` each run) to find the partition, mount FAT32, parse `boot.cfg` and load the kernel; verifies the Multiboot header and the build signature.
- UEFI: follows the firmware's CD (El Torito EFI entry) and USB (MBR ESP) paths, checks FAT type by cluster count, PE-checks `BOOTX64.EFI`, then loads the real `BOOTX64.EFI` like firmware does and runs it with simulated UEFI services to the kernel entry; also runs the loader's C code through firmware scenarios.
- Kernel entry: runs the `uefi_entry` code from `boot64.S` against test memory (BSS clearing, page tables, framebuffer above 4 GB, progress markers).

There is no CPU emulator here, so BIOS 16-bit code and the kernel itself are not executed instruction by instruction; the checks run the real C code paths and verify the binaries' structure.
