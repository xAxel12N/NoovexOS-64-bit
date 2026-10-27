# NoovexOS (64-bit)

[![CI](https://github.com/xAxel12N/NoovexOS-64-bit/actions/workflows/ci-and-setup.yml/badge.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/actions/workflows/ci-and-setup.yml) [![Contributing](https://img.shields.io/badge/contributing-guidelines-blue.svg)](CONTRIBUTING.md) [![License](https://img.shields.io/github/license/xAxel12N/NoovexOS-64-bit.svg)](LICENSE) [![Stars](https://img.shields.io/github/stars/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/stargazers) [![Forks](https://img.shields.io/github/forks/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/network/members) [![Issues](https://img.shields.io/github/issues/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/issues) [![Last commit](https://img.shields.io/github/last-commit/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/commits/main) [![Top language](https://img.shields.io/github/languages/top/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/search?l=C) [![Contributors](https://img.shields.io/github/contributors/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/graphs/contributors) [![Release](https://img.shields.io/github/v/release/xAxel12N/NoovexOS-64-bit.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/releases) [![Dependabot](https://img.shields.io/badge/dependencies-up--to--date-brightgreen.svg)](https://github.com/xAxel12N/NoovexOS-64-bit/network/dependencies)

Unix-like 64-bit operating system.

Quick links: [Getting started](#quick-start) · [Contributing](CONTRIBUTING.md) · [Roadmap](#roadmap) · [Issues](https://github.com/xAxel12N/NoovexOS-64-bit/issues)
<img width="1281" height="1018" alt="image" src="https://github.com/user-attachments/assets/fb9c9e45-67ee-4ac2-828f-868baeea6cac" />

---

## About

NoovexOS is a small, Unix-like 64-bit operating system kernel and supporting tools intended for learning OS development, experimentation, and teaching. The project focuses on clarity, small self-contained components, and approachable tasks for new contributors.

This repository contains kernel code, build scripts, and documentation to run NoovexOS in QEMU.

## Features (work in progress)

- 64-bit x86_64 kernel
- Minimal bootloader to enter long mode
- Basic serial console output for debugging
- Simple VGA text-mode output
- Educational examples and documentation for OS concepts


---

## Quick Start

Requirements (example)
- Ubuntu 22.04 or any Linux with build-essential
- qemu-system-x86_64
- nasm, gcc, ld, make

Install on Ubuntu (example):

```bash
sudo apt update
sudo apt install build-essential qemu-system-x86 nasm git -y
```

Build and run

```bash
git clone https://github.com/xAxel12N/NoovexOS-64-bit.git
cd NoovexOS-64-bit
make build        # builds kernel (or run the documented build steps)
mkdir -p build
# assume build/kernel.bin is produced
qemu-system-x86_64 -kernel build/kernel.bin -serial stdio -display none
```

If the above commands need adjusting for this repo's build system, please open an issue or improve the README with exact steps.

---

## How to help

We welcome contributions. Good ways to start:

- Look for issues labeled `good first issue` or `help wanted`.
- Improve docs: dependency list, build instructions, QEMU flags, screenshots.
- Add small, well-scoped features or drivers.

See CONTRIBUTING.md for the contribution flow and CODE_OF_CONDUCT.md for expected behavior.

---

## Roadmap

Planned short-term goals:
- Improve bootloader and multistage boot
- Implement improved memory management (paging)
- Add keyboard and basic storage drivers
- Produce release artifacts and example images

---

## Files of interest

- `kernel/` — kernel source
- `boot/` — bootloader code
- `Makefile` — build orchestration (update if different)
- `.github/` — workflows and community templates

---

## License

This project is licensed under GPL-3.0 — see the LICENSE file for details.

---

If you want I can also:
- add a small contributor/CI badge showing test status for future tests,
- create the `images/demo.gif` sample and link it,
- add the 3–6 good-first-issues now.

