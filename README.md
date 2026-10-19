# NoovexOS-64-bit

A small, portable 64-bit-capable bootloader ("myboot") and example kernel — BIOS and UEFI paths, written in C and x86 assembly.

This repository contains a staged bootloader that can boot ELF32/ELF64 kernels. It includes a prebuilt disk image you can run in QEMU and the source for the loader and a minimal example kernel.

Quick overview

- myboot: a staged BIOS/UEFI-capable bootloader that brings the CPU to 64-bit long mode, mounts a FAT32 partition, parses a simple `boot.cfg`, shows a menu and boots an ELF kernel.
- Example kernel: a tiny freestanding 64-bit kernel that demonstrates the handoff.
- Prebuilt image: `build/disk.img` (if present) is a raw disk image you can flash to USB or boot in an emulator.

Try it (quickstart)

Prerequisites (Debian/Ubuntu):

```
sudo apt install build-essential gcc-multilib qemu-system-x86
```

Build:

```
make
```

Run the BIOS build in QEMU:

```
make run
```

Run the UEFI build in QEMU (OVMF required):

```
make run-uefi
```

If you only want to flash a prebuilt image to a USB stick, write `build/disk.img` with `dd`/Etcher or use Rufus (DD image mode).

Where to read more

- Detailed bootloader docs and design: `source/myboot-dualfirmware/README.md`
- BIOS-only bootloader notes: `source/bootloader/README.md`

Contributing

Contributions welcome. If you'd like to help, good first tasks include:

- Improve documentation and top-level README (this file)
- Add CI to build the project and run smoke tests in QEMU
- Add automated image releases

Please open issues or PRs; label contributions `good first issue` where appropriate.

License

This project is licensed under the MIT License. See the `LICENSE` file for details.

---

If you want, I can now:

- Add repository topics (tags) to improve discoverability
- Create 3 curated "good first issues" to attract contributors
- Expand this README with a short demo GIF and badges
- Set up a GitHub Actions workflow to build and run `make` on push

Tell me which of the above to do next and I'll take care of it.