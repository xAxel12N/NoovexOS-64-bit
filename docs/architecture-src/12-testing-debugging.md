# Part 12: Testing, quality gates, debugging and history

This part covers how NoovexOS 64 is tested and debugged: the automatic checker `tools/check.py`, the boot emulator `tools/emulate.py`, the host-side harnesses in `tools/tests/`, the serial log and crash screen in the kernel, the shell commands for diagnosis, running in a VM, a troubleshooting table, a glossary and a version history. Paths starting with `tools/`, the repository root or `boot/` are relative to `source/`; kernel paths without a prefix (`core/klog.c`, `fs/vfs/vfs.c`) are relative to the repository root.

---

## 12.1 Overview of the test approach

The test tools do not emulate a CPU (`tools/README.md`: "There is no CPU emulator here, so BIOS 16-bit code and the kernel itself are not executed instruction by instruction"). They use four techniques instead:

| Technique | How it works | Used by groups |
|---|---|---|
| Generated tests | `check.py` cuts functions out of the current driver source (with `defn()` or by `find()` markers), swaps hardware accessors for simulator calls with exact string replacements, adds a template from `tools/tests/*_template.txt`, then compiles and runs the result on the host | `usb`, `acpi`, `nvme`, `touchpad`, `memory`, `aml`, `timers`, `gpu` |
| Whole kernel as a host object | `kernel64.c` (every kernel unit included into one file, kept for this purpose) is compiled for Linux with host versions of `arch/x86_64/cpu/cpu.h` and `io.h`, then linked with a harness | `linux`, `fs` |
| Separately built modules | the `MODS` drivers (they include only `include/kapi.h` and `include/drivers.h`) are linked against a device simulator that implements `kapi_*` | `drivers` |
| Differential testing | the same program is built against nvxlibc and glibc, or nvxbox is compared with GNU coreutils, and the outputs must be identical | `libc`, `coreutils` |

Because the generated tests are rebuilt from the source on every run, a test breaks loudly when a driver is refactored: every group first records "... code located" or "test builds against the current ..." and stops if that fails.

---

## 12.2 `tools/check.py`, the automatic bug checker

Run from `source/`: `python3 tools/check.py [options]`. It needs Linux with gcc, binutils (`ld`, `nm`, `readelf`, `objdump`), Python 3 and GNU coreutils (`tools/README.md`); `analyze` also needs `clang-tidy`, `qemu` needs `qemu-system-x86_64`.

### Command line (`main()`)

| Option | Effect |
|---|---|
| `--build` | runs group `build` first (rebuild kernel, libc, user programs, UEFI loader, BIOS loader and the ISO). Without it, `build` is skipped even if listed in `--only` |
| `--iso IMAGE` | image for `boot` and `qemu`; default `tools/out/check.iso` (the one `--build` writes) |
| `--quick` | smaller random sizes: `pfuzz` loop `i<100000` to `i<20000`, strtod inputs 30 000 to 8 000 |
| `--sanitize` | sets `SAN = -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g` for the generated driver tests and `drivers`, and `LIBC_SAN = -fsanitize=undefined -fsanitize-trap=all` for the host libc |
| `--only a,b,c` | comma list from `build,static,boot,usb,acpi,gpu,nvme,touchpad,memory,aml,timers,linux,fs,tls,fetch,qemu,net,drivers,analyze,libc,coreutils` |

Environment variables:

| Variable | Effect |
|---|---|
| `NVX_KEEP_WORK` | if set, the temporary work directory (`nvx_check_*`) with every generated `*_t.c`, executable and `build.log` is kept and printed ("work dir kept:") instead of deleted |
| `NVX_QEMU` | path to `qemu-system-x86_64` for group `qemu` (else `shutil.which`) |
| `NVX_QEMU_BIOS` | passed to QEMU as `-L <dir>` (firmware/ROM directory) |
| `NVX_NET_TAP` | `1` enables group `net` (needs root and `/dev/net/tun`) |
| `NVX_TAP_NAME` | tap interface name for `net` (default `nvxtap0`) |
| `NVX_TAP_CREATE` | `0` makes `netpeer.py` use an existing, already configured tap instead of creating one |

Groups run in a fixed order (build, static, boot, usb, acpi, gpu, nvme, touchpad, memory, aml, timers, linux, fs, tls, fetch, qemu, net, drivers, analyze, libc, coreutils). Each check is a `record(group, name, ok, detail)`; at the end the script prints `==== N of M checks passed in Ts ====`, lists every `FAIL [group] name: detail` and exits with 1 if anything failed. Note that `tools/README.md` still lists only the older subset of groups for `--only`; the authoritative list is the `argparse` help string.

Most generated tests print lines ending in ` ok` / ` FAIL` and a final `ALL PASS` or `FAILURES`; `check.py` counts those lines for the "(N checks)" in the summary and shows the first failing lines.

### Group `build` (`g_build`)
1. `sh build.sh` in the repository root (timeout 30 min, log saved as `build.log`); passes only if the exit code is 0 and the output contains `signature stamped` (printed by the stamping step in `build.sh`).
2. "no implicit function declarations": any build-output line containing `implicit declaration` fails it.
3. Copies `build/kernel64.bin` to `boot/stage/kernel64.bin`, runs `make build/boot.bin build/stage2.bin` in `boot/`.
4. `mkhybrid.py build/boot.bin build/stage2.bin <iso> NOOVEXOS boot/kernel64.bin=... boot.cfg=... README.txt=...` must succeed (its own FAT32-size and FAT16-EFI-image asserts must hold).

### Group `static` (`g_static`)
| Check | How |
|---|---|
| kernel signature | `emulate.signature_ok()` on `build/kernel64.bin` (see 12.3) |
| UEFI loader has no relocations | `readelf -r uefiboot.so` must say `no relocations` |
| `efi_main` takes SystemTable from RDX | regex over `objdump -d` of `efi_main` for `mov %rdx,...(%rip) <ST>` (Microsoft x64 ABI) |
| BOOTX64.EFI PE structure | `tests/pecheck.py` |
| GDT pointer in `uefi_entry` | `gdt64_check()`: disassembles 64 bytes at `uefi_entry`, finds the RIP-relative `lgdt`, reads the 10-byte pointer from `kernel64.bin`, requires the base to be a symbol starting with `gdt`, limit at least 0x17, and selector 0x08 to be a present 64-bit code segment (bits 47 and 53). Catches a 6-byte (32-bit) GDT pointer used in long mode |
| no raw keyboard-controller resets | regex `outb(0x64, 0xFE)` anywhere except `core/power.c` |
| no inline assembly outside `arch/` | regex `__asm__` / `asm(` in `.c`/`.h` outside `arch, third_party, user, build, mod, tools`; portable code must use `arch/x86_64/cpu/cpu.h` |
| zero strict warnings | `gcc` with the `CFLAGS` parsed from `build.sh` plus `STRICT` (`-Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wshift-negative-value -Wint-conversion -Wempty-body -Wmaybe-uninitialized -Warray-bounds`, with a few noisy warnings turned off) on `kernel64.c` and every file in `MODS`; any `warning:`/`error:` fails |
| no fixed regions inside `.bss` | `fixed_regions_in_bss()`: scans kernel `.c/.h` (not ntfs, musl, sqlite, zlib, bzip2, xz, zstd, user, wifi, doom), `boot64.S` and `boot/src/stage2.c` for page-aligned constants such as `(u8*)0x...` or `#define X 0x...` between `0xC06000` and `__bss_end` (from `nm kernel64.elf`); `stage2.c`'s `0x1000000` is whitelisted as transient (`STAGE2_TRANSIENT`) |
| no unbounded hardware polling loops | `unbounded_hw_loops()`: every `while(...)` whose condition calls `inb/inw/inl/kapi_in*/cmos/XMR` and has no `&&`/`||` (i.e. no counter or timeout) is reported with file:line |
| USB HID regions below the USB-stick buffers | parses `XHID_MAX`, `XHID_BASE(r)` and `MSD_INPUTCTX` from `drivers/usb/xhci.c` and requires `base + max*stride <= MSD_INPUTCTX` |

`build.sh` itself already turns real type errors into errors (`-Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -Werror=return-type -Werror=shift-negative-value -Werror=empty-body -Werror=duplicated-branches -Werror=logical-op`, `build.sh` line 6); `static` is the stricter second pass.

### Group `analyze` (`g_analyze`)
Runs `clang-tidy` on `kernel64.c` and every module with `-m64 -ffreestanding -DNOOVEX64` and the checks `clang-analyzer-core.*`, `clang-analyzer-unix.Malloc`, `bugprone-sizeof-expression`, `bugprone-suspicious-memset-usage`, `bugprone-misplaced-widening-cast`, `bugprone-suspicious-string-compare`, `bugprone-infinite-loop`, `bugprone-too-small-loop-variable`. Any `warning:` or `error:` fails; the first 10 are printed. If `clang-tidy` is missing, a passing "skipped" record is written.

### Group `boot` (`g_boot`)
Runs `tools/emulate.py <iso>` and records "emulate.py: N of M checks passed". Fails with "image ... exists (use --build or --iso)" if there is no image.

### Group `usb` (`g_usb` + `g_xhci_init`)
- HID/hub test: extracts ~40 functions from `xhci.c` (`hid_pick`, `hid_parse`, `xh_interval`, `xhci_hid_enum`, `xhub_*`, `hid2ascii`, `kbd_report`, `usb_kbd_repeat`, `hid_report_decode`, `xhci_poll_one`, `xhid_remove_port`, `xhc_save/load/select`, ...), renames `xh_ctrl` to `real_xh_ctrl` so the template can intercept control transfers, and runs `usb_template.txt` against recorded devices (Logitech Unifying receiver, a vendor-first receiver, a USB stick, per `tools/README.md`). Covers receivers, routing, key repeat, key mapping, hot-unplug and hubs; also non-boot devices through their report descriptors.
- Controller set-up: copies `xhci.c` from `#define XHC_MAX` to `/* mouse state` plus the memory-map code of `mem/pmm_vmm.c`, built with UBSan; checks that every scratchpad page the simulated controller asks for is valid memory.

### Group `acpi` (`g_acpi`)
All of `core/power.c` with `acpi_find_rsdp` replaced by a test RSDP, plus `acpi_template.txt`: S5 parsing, ACPI enable through SMI_CMD, the reset register, hardware-reduced sleep, and malformed tables.

### Group `gpu` (`g_gpu`)
`tests/gpu_t.c` includes `drivers/video/nvidia/nvmon.c` verbatim (marker `#include "NVMON_C"`) and simulates the DDC port register (`nvr32`/`nvw32` with SCL/SDA lines, clock stretching) and an EDID EEPROM: DCB outputs, EDID over simulated I2C, head timings.

### Group `nvidia` (`g_nvidia`)
`tests/nvidia/nvsim.c` includes `nvflcn.c`, `nvmem.c` and `nvgsp.c` and runs them against a simulated TU116 with 6 GB: registers, the PRAMIN window over a sparse VRAM, falcon IMEM/DMEM ports, a synthetic VBIOS with a BIT table and an FWSEC image, a GSP that checks the checksum and sequence number of every queue element and answers the RPCs, BAR1/BAR2 windows that walk the GPU page tables, and a copy engine that runs the push buffer (single copies, pitched multi-line copies and semaphore releases). The firmware comes from `assets/nvidia` (unpacked with Python) or from `NVX_NVFW`. 26 checks.

### Group `nvme` (`g_nvme`)
The driver part of `drivers/storage/nvme.c` (from `#define NVME_ASQ` to `/* ---- generic disk layer`) against a simulated controller with UBSan: init, 4 KB sectors, queue limits, queue wrap, stray completions, flush, timeout recovery, namespaces.

### Group `touchpad` (`g_touchpad`)
Concatenates `aml_scan.c`, `dw_i2c.c` (register accessors swapped for `sim_rd/sim_wr`), `tp_hid.c`, `tp_gesture.c`, `input.c`, `i2c_hid.c` (GPIO pin read swapped for `g_gpio_reg[]`), `elan_i2c.c`, parts of `ps2.c`/`ps2_mouse.c`, `elan_ps2.c` and `touchpad.c` with `tp_template.txt`: discovery from hand-made ACPI tables, I2C, HID over I2C, ELAN I2C with trackpoint, Elantech v3/v4 PS/2, gestures and fuzzing.

### Group `memory` (`g_memory`)
The memory-map code of `mem/pmm_vmm.c` (without `mm_load`) and the heap of `mem/heap.c` (`typedef struct hblk` to `krealloc`) against an AMD-style firmware map: reserved/ACPI-NVS detection, DMA arena skipping, heap bounds, block headers.

### Group `aml` (`g_aml`)
`drivers/acpi/aml.c` + selected `power.c` table helpers + `pci_cfg` + `acpi_dev.c` (SystemMemory access redirected to `sim_mem`). Data embedded as C arrays from `tests/aml/`: `calc.aml` with `calc.expected` (results recorded from ACPICA `acpiexec`; `ae_parse.py` parses that output), `lenovo.aml`, `hp.aml`, `hp_ssdt.aml`, `edge.aml`. The `.aml` files are compiled with `iasl` and committed (old doc section 21), so the checker does not need ACPICA. Checks interpreter results against ACPICA, laptop battery/AC/backlight/EC events, and corrupted tables.

### Group `timers` (`g_timers`)
`arch/x86_64/time/hrtimer.c` with `hr_rdtsc`, `hr_cpuid`, `hr_rdmsr/wrmsr`, `hr_rd32/rd64/wr64`, `hr_irqsave/restore` and the LAPIC write replaced by simulator functions, plus `madt_apic_ids` from `smp.c`: HPET/PIT/CPUID calibration, 32-bit HPET wrap, x2APIC, APIC one-shot and periodic modes, MADT parsing.

### Group `linux` (`g_linux`), see 12.4.1
Requires `build/musl/lib/libc.a`, `build/user/musltest.elf`, `build/user/mhello.elf` and the `build/user_*_bin.o` blobs ("run check.py --build first"). Records: kernel compiles as a host object; seccomp harness links; `lxfuzz` builds against musl; `musltest` passes on real Linux (the reference); then three harness runs with fuzz seeds 1, 2, 3 (`/tmp/lxfuzz 60000 <trace-off> <seed>`), each must end in `ALL PASS`.

### Group `fs` (`g_fs`), see 12.4.2

### Group `qemu` (`g_qemu`)
Silently skipped if no QEMU binary or no image. Starts:

```
qemu-system-x86_64 [-L $NVX_QEMU_BIOS] -m 1024 -smp 4 -accel tcg,thread=multi \
  -drive file=disk.img,format=raw,if=none,id=d0 -device ide-hd,drive=d0,bus=ide.0,unit=0 \
  -drive file=boot.img,format=raw,if=none,id=c0 -device ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0 \
  -serial file:serial.log -display none -no-reboot -monitor unix:mon,server,nowait
```

(`disk.img` is a blank 64 MB file, `boot.img` a copy of the ISO attached as a hard disk.) It then watches the serial log and types through the monitor's `sendkey`:
1. waits up to 120 s for `entering main loop`;
2. `smp on` to expects `smp: scheduling on all cores`;
3. `smptest 8 12` to expects `smptest: PASS` and `N CPUs used` with N at least 2;
4. `musltest >/dev/kmsg` to the output reaches the serial log through `/dev/kmsg`; expects `musltest: X passed, 0 ...`;
5. `featest >/dev/kmsg` to expects `featest: done N passed, 0 failed` with N at least 60 (descriptors, memory, timers, server sockets, threads on 4 cores, terminal; section 6.8), and prints any `featest: FAIL` lines;
6. `dyntest >/dev/kmsg` to expects `dyntest: done N passed, 0 failed` (dynamic linker, shared libc, `dlopen`);
7. `smp off` to expects `smp: secondary cores parked`.

### Group `tls` (`g_tls`)
Builds `tests/tls/x509h.c` (the kernel's `nvxcrypto.c`, `nvxtls.c`, `nvxcrypto2.c`, `nvxx509.c` and `nvxroots.c` compiled for the host, with `--sanitize` under ASan+UBSan) and checks:
1. every embedded root's public key parses (120);
2. the self-signature of every certificate in `lib/crypto/roots/mozilla-cacert.pem` verifies with the kernel's RSA/ECDSA code (117 pass; the one P-521 root is expected to fail and 3 SHA-1/PSS roots are skipped);
3. `tests/tls/tlsvec.py` (needs the Python `cryptography` module) generates chains and signatures and runs 31 positive and negative cases (section 9.9.3).

### Group `fetch` (`g_fetch`)
Tests the user-space TLS client on the host (needs the Python `cryptography` module). It compiles all of `third_party/bearssl/src` and links `usr/musl/fetch.c` and `nvxtls.c` with `-Wall` (any warning fails the check) and, with `--sanitize`, ASan+UBSan and leak checking. `tests/tls/fetchpeer.py` starts four TLS 1.2 servers on 127.0.0.1 with certificates from a fresh test CA (ECDSA for `localhost` and 127.0.0.1, RSA for the same names, ECDSA for `other.test` only, and an expired one) and writes the Mozilla roots in the `nvx-anchors.pem` format. The checks: the anchors file and the local CA load (121 anchors), ECDSA and RSA downloads with the body's SHA-256 matching, an IP address host, a redirect to a chunked body, an unknown root, a wrong name and an expired certificate refused with exit status 3, `-k`, HTTP 404 giving exit status 4, a bad URL giving 1, and no sanitizer or leak report in any run.

### Group `net` (`g_net`)
Runs only when `NVX_NET_TAP=1` and QEMU is available. `tests/net/netpeer.py` creates the tap device `nvxtap0` with the QEMU NAT addresses (10.0.2.2 gateway, 10.0.2.3 DNS), generates a test CA with ECDSA and RSA server certificates for `noovex.test`, and serves DNS (every name to 10.0.2.2), HTTP on 80 and 8080 (with `/big`, a 300 KB pattern, `/redir`, a redirect to it, and `/chunked`, the pattern with chunked encoding), TCP echo on 7777, UDP echo on 7778, TLS 1.3 only on 443 (ECDSA) and 8443 (RSA), and TLS 1.2 on 9443 (ECDSA) and 9444 (RSA). It also tries to connect to 10.0.2.15:9200 every second and logs the answer. QEMU boots with `-netdev tap -device e1000`. The group then types:
1. `net` (DHCP times out, the static 10.0.2.15 is used), then `nettest >/dev/kmsg` to expects `nettest: PASS`;
2. `http noovex.test/` to the peer must log a request on port 80;
3. `https noovex.test/` to must fail with -11 (unknown root); `fetch -v ... https://noovex.test:9443/` must report at least 120 trust anchors read from the file the kernel wrote and refuse the unknown root;
4. writes the test CA to `/etc/ssl/local-ca.pem` with `echo ... >>` lines, then `https noovex.test/` and `https noovex.test:8443/` to `connected, certificate verified`;
5. `https 10.0.2.2/` to verified through the IP SAN; `https wrong.test/` to -12; `https -k wrong.test/` to connects `WITHOUT certificate checks`;
6. `fetch` with the test CA installed: ECDSA on 9443 and RSA on 9444 (`/chunked`) must say `certificate verified` and print the expected SHA-256, plain HTTP `/redir` must follow the redirect, `https://10.0.2.2:9443/` must verify through the IP SAN, `wrong.test` must be refused;
7. `featest listen >/dev/kmsg` to the peer's log must show `inbound pong from noovex`, i.e. a musl server accepted a connection that came in through the network card.

### Group `drivers` (`g_drivers`)
Compiles each file in `MODS` (`build.sh` line 21: `drivers/net/ethernet/e1000.c rtl8139.c rtl8169.c drivers/virtio/virtio.c virtio_net.c virtio_blk.c drivers/sound/ac97.c hda.c`) for the host, links them with `tests/drivers/drvsim.c`, see 12.4.3.

### Groups `libc` and `coreutils`
- `host_libc()` builds `usr/libc/src/{libc.c, printf.c, stdlib2.c, stdio2.c, sys_host.c, crt0_host.S}` with `-ffreestanding -nostdinc` against nvxlibc's own headers into `libc.a`; `sys_host.c` maps NoovexOS syscall numbers to Linux ones, so real nvxlibc programs run on Linux.
- `libc`: `tests/libc/pfuzz.c` (random printf formats and doubles; 100 000 iterations, 20 000 with `--quick`) and `ltest.c` (strtol/strtoul bases and edge values, strtod, sscanf, qsort, ctype, malloc, files, strftime, setjmp) are built once with glibc and once with nvxlibc; every output line and the exit code must match. `sfile.c` reads inputs generated by `gen_strtod_inputs()` (exact halfway points between adjacent doubles printed with up to 120 digits, random decimal strings, 300-700-digit denormal cases, hex floats, overflow/underflow and malformed strings) and prints the bit pattern and end offset; 0 differences required.
- `coreutils`: links `usr/src/nvxbox.c` + `coreutils.c` against the host libc and runs the 65-entry `COREUTILS` table (cat, wc, head, tail, grep, sort, cut, tr, uniq, seq, basename, dirname, rev, tac, nl, fold, paste, comm, cmp, md5sum, sha256sum, base64, find, yes, echo, true/false, mkdir/rmdir, cp, ln/readlink, tee, ls) in a fixture folder. stdout, exit status and "stderr empty or not" must equal GNU's (`sort` runs with `LC_ALL=C`).

---

### Test templates and the memory map

Several generators paste driver code into a template that maps the driver's fixed buffers at the same addresses on the host (`tools/tests/*_template.txt`). When a fixed address moves, the template must move too. v9.12 updated `usb_template.txt` (xHCI memory `0x1090000`, second controller `0x10D0000`, HID memory below `0x1150000`, 16 queued reads per endpoint and room for 4096 logged doorbells), `xhci_init_template.txt` (`0x1090000`) and `nvme_template.txt` (`0x1230000`), and the USB generator now copies `XHID_INFLIGHT` and `xhid_bs`. The AML template allocates below 4 GiB from a fixed arena, because `anode_child` rejects node pointers above 4 GiB (a pointer check added in v9.07; host `malloc` returns higher addresses, so every lookup failed).

## 12.2a Typical workflows

```
cd source
python3 tools/check.py --build                 # everything, from a fresh build (needed once for linux/fs/qemu)
python3 tools/check.py --only static,analyze   # fast source checks, no image needed
python3 tools/check.py --only usb,touchpad --sanitize
NVX_KEEP_WORK=1 python3 tools/check.py --only nvme   # keep nvme_t.c and the binary for gdb
NVX_QEMU=/usr/bin/qemu-system-x86_64 python3 tools/check.py --only qemu --iso tools/out/check.iso
python3 tools/check.py --only libc,coreutils --quick
python3 tools/emulate.py tools/out/check.iso
```

Prerequisites per group, from the code:
- `static` reads `build/kernel64.elf`/`kernel64.bin` and `boot/uefi/uefiboot.so`/`BOOTX64.EFI` when they exist; the signature, relocation, PE, GDT and `.bss` checks need a previous build (`fixed_regions_in_bss` takes `__bss_end` from `nm`).
- `boot` and `qemu` need an image (`--build` or `--iso`).
- `linux` needs `build/musl` and the musl programs from `build.sh`.
- `coreutils` needs the host libc that `libc` builds; asking for `coreutils` alone still runs the `libc` build step (`want('libc') or want('coreutils')`).

### Adding a test for a new driver

The existing groups show the pattern:
1. If the driver can be written against `include/kapi.h` only, add it to `MODS` in `build.sh`; it is then automatically compiled with strict warnings in `static`, analysed in `analyze` and compiled for the host in `drivers`. Add a simulated device and `rec(...)` checks to `tools/tests/drivers/drvsim.c`, ending with the existing `ALL PASS`/`FAILURES` summary.
2. Otherwise write a `g_<name>(work)` function in `check.py` that cuts the driver code out of its file by stable markers (`defn(src, name)` returns one `static` function by brace matching), replaces each hardware accessor with an exact `str.replace` guarded by `assert a in src` (as `g_touchpad` and `g_timers` do), prepends a `*_template.txt` `HDR` part with the simulator and appends a `TEST` part, compiles with `SAN`, and records "... builds against the current ..." and the result line.
3. Add the group name to the `--only` help string and call it from `main()`.
4. For code that needs the whole kernel (file system, syscalls), use the `fs`/`linux` approach: copy the source tree, swap in `cpu_host.h`/`io_host.h`, and add `hk_*`/`fk_*` accessor functions to the glue file.

### Reading kernel sources from a generator (since v8.96)

The generators were written when every kernel file was one big translation unit. To keep them working with separate units, `check.py` has four helpers:

| Helper | Returns |
|---|---|
| `kread(path)` | The unit's source as the generators expect it: the unit's shared types, macros and inline helpers from its header first, then the source with the `include/nvx/` lines removed and `static` put back on every shared function and variable. `kread(path, hdr=False)` leaves out the header part. |
| `khdr(path)` | Only the header part: types, macros and inline helpers, without prototypes or `extern` lines. |
| `kfwd(text, own)` | `static` forward declarations for every shared function used in `text`, and for every shared variable that `text` both uses and defines. Generators that paste several units together put this after the headers, as the old single file had them. |
| `kmissing(code, template)` | `static` definitions for shared variables that the pasted code uses but neither it nor the test template defines (for example `g_gdisk_probed` in the ACPI test). |

`defn(src, name)` finds a function with or without `static`. When you move code between units, run `check.py --only usb,acpi,touchpad,memory,aml,timers,gpu,nvme` to see which generators need new markers.

---

## 12.3 `tools/emulate.py`, boot-path emulation

`python3 tools/emulate.py IMAGE.iso` prints sections and ends with `N of M checks passed`; exit 1 on failure.

BIOS (`emulate_bios`)
- MBR has `0x55AA` and non-empty boot code; stage2 at sector 1 is byte-identical to `boot/build/stage2.bin`.
- Copies the partition-selection block out of `boot/src/stage2.c` (from `    uint32_t part_lba = 0;` to `if(!part_lba) fail`) into `tests/bios_emu.c` (`@@PARTSEL@@`) and builds it with the real `fat.c`, `config.c`, `string.c` as a 32-bit freestanding program (`-m32 -march=i386 -mno-sse`); its `disk_read` uses raw `int $0x80` Linux i386 syscalls on the image.
- The program must print `PART`, `MOUNT ok`, parse `/boot.cfg` (`CFG entries=... default=...`), load the default kernel (`format=flat`).
- The loaded kernel needs a Multiboot header (`0x1BADB002`, checksum OK) in its first 8 KB and a valid build signature.

Build signature (`signature_ok`): finds `NVX64SIG`; the immutable-region SHA-256 is at +32, the full-file SHA-256 at +88. Both hash fields are zeroed before hashing; the immutable hash covers `__immutable_end - 0x100000` bytes and is only checked when `build/kernel64.elf` matches the image.

UEFI media (`extract`, `emulate_uefi`)
- CD path: El Torito catalog, platform `0xEF` bootable entry to FAT image; USB path: MBR partition type `0x0B/0x0C/0xEF` to FAT32. `tests/fatck.py` (`Fat` class) decides FAT12/16/32 by cluster count and checks FAT copies, backup boot sector, FSInfo signatures, FAT[0] media byte and cluster chains.
- Each path must contain `\EFI\BOOT\BOOTX64.EFI` (passing `pecheck`) and the kernel (`/KERNEL64.BIN` or `/boot/kernel64.bin`) identical to the BIOS-path kernel.
- `tests/pe_run.c` loads the real `BOOTX64.EFI` like firmware does and runs it with simulated UEFI services to the kernel entry ("all scenarios").
- `uefiboot.c` built with `-DUEFI_TEST` and linked with `tests/firmware_sim.c` runs firmware scenarios (missing kernel, RGB framebuffer, `ExitBootServices` retry, ...).

Kernel entry (`emulate_kernel_entry`): cuts the `uefi_entry` body from `boot64.S` (from `cli; cld; movl $2,%edi` to `5: movabsq $pml4`), wraps it as `run_entry` and runs it from `tests/entry_t.c` against test memory: BSS cleared, identity map 0-4 GB, framebuffer above 4 GB mapped, markers. `tests/mark_t.c` checks `uefi_mark` draws the progress squares exactly in place.

Other helpers: `tests/pecheck.py` (MZ/PE, x86-64, PE32+, subsystem 10, section alignment/overlap with headers, entry in an executable section, relocation blocks); `tests/isocheck.py ISO EFI KERNEL` (standalone: ISO9660 PVD, El Torito validation checksum, no-emulation entry at most 65 535 sectors, FAT12/16 CD image, FAT32 USB partition, byte-exact files; writes `/tmp/extracted_BOOTX64.EFI`).

---

## 12.4 Host harnesses in `tools/tests`

### 12.4.1 `linux/`, the seccomp harness (`hh.c`, `hk_glue.c`, `cpu_host.h`, `io_host.h`, `lxfuzz.c`)

Build. `g_linux` copies the source tree (without `build/`, musl, doom, lua) to a work dir, replaces `arch/x86_64/cpu/cpu.h` with `cpu_host.h` and `io.h` with `io_host.h`, and compiles `hk_glue.c` (which is `#include "kernel64.c"` plus accessor functions) with the kernel's own flags. Kernel symbols that clash with libc (`malloc, free, memcpy, strlen, ..., panic`) are renamed with `-D<name>=kh_<name>`. `hh.c` is linked with this object and the embedded user-program blobs, with `--unresolved-symbols=ignore-all`.

Shims.
- `cpu_host.h`: interrupts on/off and halt are no-ops; `cpu_get_pt/cpu_set_pt/cpu_flush_page` call `h_get_pt/h_set_pt/h_flush_page` in the harness; FPU save/restore go to the signal context; `cpu_stop`/`cpu_triple_fault` call `h_fatal`; MSRs read 0; the per-CPU pointer is a plain variable.
- `io_host.h`: port I/O returns all-ones except a fake CMOS at ports 0x70/0x71 (fixed date/time).

Memory. `hh.c` creates a `memfd` ("nvxframes") the size of the frame pool and maps it at its real physical address `PF_BASE 0x0C000000`-`0x0E000000`; the heap is an anonymous map at `0x10000000` (64 MB). `hk_setup()` initialises per-CPU, heap, frame bitmap, fds, dirtree, env and VFS. When the kernel loads a user CR3 (`h_set_pt`), `sync_all()` walks the guest page tables at PML4 slot 8 and `mmap(MAP_FIXED|MAP_SHARED)`s every present guest page from the memfd at its user virtual address in the window `0x40000000000` + 1 GB (read-only if the PTE is not writable). So user programs run natively at their real addresses on the same physical frames the kernel manipulates.

Trapping syscalls. A seccomp BPF filter loads the upper 32 bits of the instruction pointer (`seccomp_data` offset 12) and returns `SECCOMP_RET_TRAP` when it is in `[0x400, 0x800)`, i.e. the syscall came from the user window `0x40000000000-0x80000000000`; everything else (the harness, the kernel code) is allowed. The `SIGSYS` handler `on_sys` copies the registers into a kernel-style frame and calls `syscall_dispatch` if `si_arch == 0x40000003` (i386 audit arch, i.e. legacy `int 0x80`) or `linux_syscall` otherwise, then writes the registers back. When the kernel switches to a process it calls `jc_resume`, which the harness implements with an `iretq` frame (`hh_iret`, CS 0x33/SS 0x2b) after restoring the FS base. `SIGSEGV/SIGBUS` inside the window first go to the kernel's demand-paging/COW handler (`hk_pf` to `vm_fault_user`); other guest faults are delivered through `jc_fault`. A fault with RIP outside the window is a harness crash (exit 91); `alarm(120)` gives `HARNESS TIMEOUT` (exit 92). `HH_DEBUG=1` prints guest faults with the last syscall and a 16-entry syscall-number ring.

What it checks (`main`): `mhello` prints `Hello from musl libc on NoovexOS 8.97` and exits 0; argv and environment reach musl; legacy `int 0x80` programs (`pipetest`, `libc8`, nvxbox `echo`, `seq`); gzip/bzip2/xz/zstd round-trips of a ~300 KB file and `zcat`; sqlite3 create/insert/query, persistence, `integrity_check`, ASCII table boxes; the DNS query builder refuses over-long host names without overrunning a guarded buffer (`hk_dns_build` to `dns_build_query`); `udp_send` with a 4000-byte payload (`hk_udp_big`); `smptest 3 3 200000`; `musltest` (each `FAIL` line becomes a failed check, at least 80 `ok` lines required); then `lxfuzz`; finally no leaked page frames, open-file slots or pipes, and a summary of syscalls, address-space switches and page faults.

`lxfuzz.c` (musl static-PIE): a forked child makes `iters` random syscalls (numbers 0-334 plus 436/439, minus a deny list such as exit, fork, execve, kill, mmap-type calls that would destroy the fuzzer) with arguments drawn from 16 classes: 0, -1, small ints, pointers into a buffer, pointers just before an unmapped page (`g_edge`), kernel addresses (`0x100000`, `0x0C000000`), a non-canonical address, path strings (`/dev/zero`, `..`, `/nope/x`, ...), `AT_FDCWD` (-100), huge values. Then the parent runs `mem_phase`: random `brk` growth/shrink (new memory must read zero), anonymous `mmap`/`munmap` with tag checks, `MAP_FIXED` remap of a hole, `malloc/realloc/free` with content checks, and pipe round-trips. Corruption prints a mask; success prints `lxfuzz done: ...`.

### 12.4.2 `fs/`, NVX4 against a simulated disk (`fsh.c`, `fs_glue.c`)

`fs_glue.c` includes `kernel64.c` and exposes `fk_*` wrappers (`vfs_open/read/write`, `ux_unlink`, `k_rename`, `nvx3_sync_all`, ...). `fk_boot()` resets every in-memory table (`FS[]`, `DT[]`, links, `g_vf[]`, buffer cache) and forces `g_dktype = DK_VIRTIO`, so `disk_init()` mounts the disk through the VirtIO-blk entry points, which `fsh.c` implements on an anonymous memory "disk". `virtio_blk_write` honours a write budget: when it reaches 0 the "power is cut" (`g_cut`) and every later write and flush fails. `fk_bitmap_check()` rebuilds the free-block bitmap from inode extents and compares it with the kernel's (`bm_get`, `g_n4_free`), catching double use.

Tests (42 checks per the old doc): `t_format` (blank disk to NVX4, remount finds defaults), `t_basic` (sizes 0...3 MB, long names, hard/sym links, chmod, rename, truncate, append, unlink; all identical after remount; files loaded on demand), `t_many` (300 folders, 900 files, delete half, delete all to every block returned), `t_big` (40 MB file, 8 KB rewrite in the middle, eviction and reload), `t_full` (full disk returns an error, earlier files intact), `t_unix` (unlinked-but-open file, `O_EXCL`, rename over a file, `rmdir` of non-empty, sticky `/tmp`, `O_APPEND`, directory inode numbers), `t_metacopy` (damaged copy A falls back to B and is rewritten; both damaged to mount refused, nothing written), `t_crash` (150 rounds: record the sector writes of an operation, restore the snapshot, cut power at a random write; must always mount, keep old data, new file all-or-nothing), `t_migrate3` (synthesised NVX3 disk "v8.13-v8.90 format" converted, plus 40 power cuts during conversion), `t_migrate2` (NVX2 "pre-v8.13"), `t_foreign` (a disk with someone else's partition is never written; a type-0x7F partition is formatted inside its bounds).

### 12.4.3 `drivers/drvsim.c`, simulated hardware for the `MODS` modules

Implements the `kapi_*` API: DMA pages from a 64 MB arena at `0x20000000` (any device access outside it aborts with "DMA outside arena"), a virtual clock (`kapi_ns`/`kapi_delay_us` advance `g_now` and run the device's `g_tick`), a one-device PCI space, port I/O routed to per-device callbacks, and a `wire[]` capture of transmitted frames. `SIM_LOG=1` prints `kapi_log` lines. Checks: with no device every probe returns 0; RTL8139 (TX slots, RX ring wrap), RTL8111/8168 (16-entry rings, CRC strip), VirtIO-net (legacy handshake negotiating only MAC+STATUS, header stripped, link down, buffer recycling), VirtIO-blk (FLUSH negotiated, random I/O split into 128-sector requests, read past end refused), AC97 (cold reset, VRA 48 kHz, 600 KB streamed in order, mute, dead codec given up within 0.5 s), HD Audio (CORB/RIRB, codec graph finds speaker and headphones, EAPD, stream format, 700 KB streamed, mute, dead codec within 2 s, no codecs refused), e1000 (EEPROM MAC, rings, BAR above 4 GB refused).

### 12.4.4 Templates and other files

| File | Used by |
|---|---|
| `usb_template.txt`, `xhci_init_template.txt` | `usb` |
| `acpi_template.txt` | `acpi` |
| `aml_template.txt`, `aml/*.asl/.aml`, `calc.expected`, `ae_parse.py` | `aml` |
| `tp_template.txt` | `touchpad` |
| `nvme_template.txt` | `nvme` |
| `mm_template.txt` | `memory` |
| `hr_template.txt` | `timers` |
| `gpu_t.c` | `gpu` |
| `bios_emu.c`, `pe_run.c`, `firmware_sim.c`, `entry_t.c`, `mark_t.c`, `fatck.py`, `pecheck.py` | `emulate.py` |
| `isocheck.py` | standalone ISO checker |
| `libc/pfuzz.c`, `ltest.c`, `sfile.c` | `libc` |

Templates are split into named parts with `@@NAME@@` lines (`template()` in `check.py`).

### 12.4.5 Harness exit codes and debug switches

| Harness | Code / switch | Meaning |
|---|---|---|
| `linux/hh.c` | exit 90 `HARNESS FATAL: <msg>` | `h_fatal`: setup failure (memfd, frame-pool or heap mmap, seccomp), guest page outside the 1 GB window, PTE outside the frame pool, `jc_fault` returned, `cpu_stop`/triple fault in kernel code |
| `linux/hh.c` | exit 91 `HARNESS CRASH: signal N at rip=...` | a fault whose RIP is not in the user window, i.e. a bug in the kernel code itself |
| `linux/hh.c` | exit 92 `HARNESS TIMEOUT` | the 120 s `alarm` fired |
| `linux/hh.c` | `HH_DEBUG=1` | print each guest fault with the last syscall and the syscall-number ring |
| `fs/fsh.c` | exit 90 `FATAL ...`, exit 2 `heap mmap failed` | `h_fatal` from kernel code; the 1 GB heap at `0x10000000` could not be mapped |
| `drivers/drvsim.c` | exit 3 `DMA outside arena ...` | a driver gave the device an address it never got from `kapi_dma_pages` |
| `drivers/drvsim.c` | `SIM_LOG=1` | show `kapi_log` output of the modules |
| all generated tests | last line `ALL PASS` / `FAILURES` | `check.py` requires exit 0 and `ALL PASS` |

---

### 12.4.6 `tools/webhost.c`, the web engine on the host

The web engine (Part 13) builds for Linux unchanged except for `web/krt.c`, whose job glibc does. Compile every `web/*.c` file except `krt.c`, the QuickJS sources and `tools/webhost.c` with `gcc -O1 -g -D_GNU_SOURCE -I.` from the repository root and link with `-lm`. Then:

```
webhost page.html out.ppm [width] [height] [maxheight] [javascript]
```

`webhost` serves `file://` URLs for subresources, decodes no real images (it returns a gradient of the right size), keeps cookies and storage in memory, prints every fetch, console message and script error, and reports load and paint times. The optional JavaScript is evaluated after the page has loaded and 20 timer ticks have run; its result is printed and the page is painted afterwards, so a test can click buttons (`document.getElementById('x').click()`) or set input values and check the outcome in the picture.

## 12.5 The serial debug log (`core/klog.c`)

- `klog_init()` probes COM1 by writing `0xA5` to the scratch register (`0x3F8+7`) and reading it back; if it differs, logging stays off. Otherwise it programs 115200 baud (divisor 1), 8N1, FIFO on.
- `klog_c()` waits at most 100 000 polls for THR-empty (`LSR` bit 5); on timeout it sets `g_klog_ok = 0` so a dead UART never slows the system down again.
- `klog(s)` writes `[<g_ticks>] <s>\r\n` (ticks are 10 ms PIT ticks). `klog2(a, b)` joins two strings (max 159 chars).
- `g_klog_hook`: early in `kmain` it points at `bootlog_line()` (`core/init/main.c`), which also draws each log line on screen from y = 200 (32-bpp framebuffers only). It is cleared just before `splash_boot()`, so on-screen boot logging ends at "boot: splash start".
- `kapi_log()` (`sys/native/kapi.c`) forwards module logging to `klog`.
- `/dev/kmsg` (vfile device 13, `fs/vfs/vfs.c`): writes are split at newlines, non-printable bytes dropped, and each line goes to `klog`. So `prog > /dev/kmsg` sends a program's output to the serial port (the `qemu` group uses this for `musltest`; `usr/musl/smptest.c` writes its reports there).

Boot lines (in order, `core/init/main.c`): `boot: kmain entered, fb ok`, `boot: heap ok`, memory-map lines (`memory: firmware map received...`, `memory: WARNING the firmware uses memory inside a kernel area - run memmap`), `boot: frame pool ON (>=272MB RAM)` / `OFF`, `boot: pci scanned`, `boot: stopped DMA on devices...`, `boot: windows/fs/disk/vfs/term ok`, `boot: pic+pit ok`, timer lines (`timers: clock = ...`), `boot: no PS/2 controller...` or `boot: ps2 mouse ok`, `usb: ... xHCI controller(s) found`, `boot: xhci probe done`, `touchpad: ...`, `boot: touchpad probe done`, `acpi: ...`, `boot: ACPI AML done`, `boot: splash start/done`, `boot: integrity check done`, `boot: live_init start/done`, `boot: first frame presented`, possibly `boot: timer interrupts are not arriving - using a polled timer`, `smp: ...`, and finally `boot: interrupts on, entering main loop`.

Runtime lines. `cmd: <line>` for every live-terminal command (`shell/commands.c`), `live: job took the terminal` / `live: job finished, prompt back`, `nvx4: mounted, files=N` / `nvx4: mount failed: ...`, `rootfs: ...`, `ntfs: ...`, `usb: ...`, `acpi: power button pressed, shutting down`, `power: power off did not take effect`.

Heartbeat. Every 500 ticks (5 s) the main loop logs `alive: main loop running` (or `... (live)` in the live terminal). If the screen freezes but `alive:` lines continue, the main loop is fine and the problem is drawing or input; if they stop, the last line shows where it hung (old doc section 13).

The BIOS loader mirrors its own text to COM1 at 115200 8N1 too (`boot/README.md`).

---

## 12.6 Panic and the Red Screen of Death (`core/panic.c`)

`panic(regs_t* r, const char* msg)`:
1. `smp_halt_others()` sends the halt IPI (`IPI_HALT`, `0xF2`) to the other cores; interrupts off.
2. A second panic while panicking just halts (no triple fault). With no framebuffer it halts silently.
3. Paints a red gradient with ":(" and "NoovexOS ran into a problem and needs to stop."
4. With a register frame for an exception (`vector < 32`): `KERNEL PANIC - <name> (vector N)` from the `EXC[32]` table, `error=`, `RIP=`, `RSP=`, `RFLAGS=` and all 16 general registers. Without one: `KERNEL PANIC` and `msg`.
5. `blit_all()` and halt forever. It does not write to the serial log, so the last `klog` line before the RSOD is the last thing on COM1.

Who calls it: `isr_body` (`arch/x86_64/interrupts/interrupts.c`) for a CPU exception in kernel mode, after `vm_user_pf` had a chance to resolve a page fault; exceptions in user mode go to `jc_fault` instead, which turns the vector into a signal (SIGFPE 8, SIGILL 4, SIGTRAP 5, else SIGSEGV 11) and kills the process unless it has a handler. The shell command `panic` and Settings' crash test call it deliberately. Double faults run on their own IST stack and show the crash screen; NMIs are ignored (old doc, Linux ABI "Safety").

A separate "WRONG VERSION" screen exists in `core/init/main.c` for a failed build-signature check, but `g_sig_enforce = 0`: `sig_self_check()` only reports (see `kcheck`).

---

## 12.7 Diagnostic shell commands

All in `shell/commands.c` (`run_cmd_one`), some implemented elsewhere:

| Command | What it shows / does |
|---|---|
| `memmap` (`core/init/installer.c`) | firmware memory map (UEFI loader only, regions below 4 GB), each kernel area (boot info, kernel image, USB/disk buffers, page tables + .bss, DMA arena, page pool, module area, heap) with `ok`, `OVERLAPS <type>` or `skips`; heap placement; devices whose DMA was stopped; DMA arena and free frames |
| `memtest` (`core/init/installer.c`) | walks heap block headers (`HBLK_MAGIC`), then fills free DMA arena, page pool, module area and up to 16 x 16 MB heap blocks with an address pattern, re-reads, waits 3 s and reads again; reports words that did not hold and words "changed by something else within 3 s" (leftover device DMA) |
| `kcheck` (`core/init/installer.c`) | build version, size stamp, boot-image snapshot vs. its own hashes (code / whole file MATCHES/DIFFERS), and up to 12 code pages changed in memory since boot |
| `timers` / `timertest` | clock source, TSC frequency and reference used, HPET address/frequency/width, LAPIC timer rate and interrupt count, uptime by clock vs. ticks / 1-s cross-check of PIT, HPET and TSC (ppm), five 5-ms APIC one-shots, 250 us delay |
| `usbinfo`, `usb`/`xhci` | USB state; `usb` rescans all controllers then runs `usbinfo` |
| `tpinfo` (`drivers/input/touchpad/touchpad.c`), `tpinfo scan`, `tpdump` | ACPI touchpad candidates with I2C address, controller, speed and GPIO pin; DesignWare controllers; ELAN PS/2 state and packet counters; the touchpad's protocol, layout and last gesture, or the stage/abort code of the last failed try |
| `smp`, `smp on`, `smp off`, `smpstart` | per-core table; start/park the other cores (`/etc/smp`) |
| `cores`/`cpu`, `cpuid` | CPU brand and core information |
| `pci`, `disks`/`lsblk`, `mem`, `slabinfo`, `bcstat`/`bufcache` | PCI list, disks, memory, slab caches, buffer-cache hit rate |
| `netlog`, `net`, `dns`, `ping` | last 18 network log lines and network tools |
| `acpi ...`, `battery`, `brightness`, `gpu`, `gpumon` | ACPI namespace / EC, power, NVIDIA read-only probes |
| `ver`/`about` | version from `g_buildsig.version` |
| `panic` | deliberate RSOD |
| test programs | `utest`/`userspace`, `fdtest`/`posix`, `forktest`, `vmtest`, `nvxtest`, `smptest`, `musltest` |

There is no `logs` command; the kernel log only goes to COM1 (and to the boot screen until the splash).

---

## 12.8 Running NoovexOS in a VM

QEMU (tested configuration). The only scripted run is `g_qemu` (12.2): `-m 1024 -smp 4 -accel tcg,thread=multi`, the hybrid ISO attached as an IDE hard disk with `bootindex=0`, a blank raw disk on `ide.0`, `-serial file:serial.log`. For interactive use, replace `-display none` with a display and use `-serial stdio`. Notes from the code:
- Give at least 272 MB RAM, otherwise `pframe_init` turns the frame pool off and user programs cannot run (`boot: frame pool OFF (<272MB RAM)`).
- `-smp N` works; cores start automatically unless `/etc/smp` says `off` (12.10).
- `power_off` writes the QEMU (`0x604`), Bochs (`0xB004`) and VirtualBox (`0x4004`) shutdown ports after ACPI as a fallback (`core/power.c`).
- `NVX_QEMU_BIOS` adds `-L <dir>` for a custom firmware directory. The repository has no OVMF recipe; the UEFI path (`BOOTX64.EFI`, El Torito EFI entry and FAT32 ESP) is verified by `emulate.py`, not by a scripted QEMU UEFI boot. (Manual UEFI runs use `-machine q35` with OVMF as two `-drive if=pflash` entries; on q35 the disks sit on the ICH9 AHCI controller, which makes it the easiest way to exercise the AHCI driver.)
- Networking: QEMU builds without slirp have no user-mode network. Use a tap device (`-netdev tap,id=n0,ifname=nvxtap0,script=no,downscript=no -device e1000,netdev=n0`); `tools/tests/net/netpeer.py` sets one up together with test servers.
- Old doc section 11.2: "use the AHCI + xHCI + standard-VGA/virtio-gpu combination the drivers already handle"; VirtIO-net/blk are also probed (old doc v8.90).

VirtualBox. Boot the hybrid ISO as a CD (the El Torito EFI entry is the "CD/DVD/VirtualBox" path in `emulate.py`), or wrap a raw disk image as a VDI with `tools/mkvdi.py RAW VDI [MB]` (fixed VDI, attaches without `VBoxManage convert`). Serial log: Settings > Serial Ports > Port 1 > Enable, mode "Raw File" (old doc section 13).

---

## 12.9 Troubleshooting guide

| Symptom | Where to look |
|---|---|
| Black screen, no serial output | Loader stage: BIOS loader prints to COM1; for UEFI watch the bottom-left progress squares (1 red ... 6 white = `kmain` reached, old doc section 19). Run `emulate.py` on the image |
| Serial stops at a `boot:` line | The next step in `kmain` hangs; compare with the list in 12.5 |
| `boot: timer interrupts are not arriving - using a polled timer` | PIC/APIC routing; `pit_poll_tick` keeps time. Check `timers` |
| `alive:` continues but screen frozen | Drawing/input, not a hang (12.5) |
| Red screen with vector and RIP | `core/panic.c` output; map RIP with `nm build/kernel64.elf` / `objdump` |
| User program killed with SIGSEGV | `jc_fault`; on the host reproduce with the `linux` harness and `HH_DEBUG=1` |
| User programs do not start | `boot: frame pool OFF` (RAM < 272 MB) |
| `boot: INTEGRITY CHECK FAILED - kernel code was modified` right after the kernel grew | The boot stack or a fixed buffer overlaps the image. Dump memory from QEMU (`pmemsave 0x100000 <size>`) and compare with `build/kernel64.bin`; a changed band just below the stack top is the stack. The image must end below `0x900000` (section 13.12) |
| Browser shows "This page stopped working (engine error)" | The engine hit `nwe_fail` (arena full or an internal check). Render the page with `webhost` (section 12.4.6) to see the error; the next page load starts from a clean arena |
| Browser window opens but stays blank from the live terminal | Fixed in v9.08: `browse` switches to the desktop. On older versions run it from the desktop terminal |
| Random corruption on real hardware | `memmap` (OVERLAPS), `memtest` ("changed by something else" = DMA), `boot: stopped DMA on devices...` |
| `kcheck` shows DIFFERS / changed pages | Kernel image modified in memory after boot |
| Hang when starting cores | Next boot logs `smp: the last start did not finish - multi-core turned off`; use `smp off` / `smp on` |
| USB keyboard/mouse missing | `usbinfo`, serial `usb:` lines (firmware hand-over, scratchpad memory), `check.py --only usb` |
| Touchpad dead | `tpinfo`, `tpinfo scan`, serial `touchpad: ACPI lists an I2C touchpad but it did not answer` |
| Battery/backlight/power button | `acpi`, `acpi ec`, serial `acpi:` lines, `check.py --only aml,acpi` |
| Disk not mounted | serial `nvx4: mount failed: ...` (`g_n4_err`), `rootfs: ...`, `disks` |
| No network | `netlog` (also on serial as `net: ...`), `net`, `check.py --only drivers,net` |
| `https` fails with code -11 | The server's chain does not reach one of the 120 built-in roots. For your own CA, put its PEM in `/etc/ssl/local-ca.pem`; `https -k` skips the check for one request |
| `fetch` says "certificate chain ends at an unknown root" | As for `https -11`: the CA is not among the roots in `/etc/ssl/nvx-anchors.pem`; add it to `/etc/ssl/local-ca.pem`. `fetch -v` shows how many roots were loaded (at least 120) |
| `fetch` says "the server does not support TLS 1.0 to 1.2" | The server only speaks TLS 1.3, which BearSSL does not implement; the kernel's `https` command can reach it |
| A program in raw mode left the terminal strange | The shell resets it when the job ends (section 11.12); if a background job keeps changing it, bring it to the foreground and quit it |
| A threaded program hangs | `ps` shows each thread; a thread in `S` with a futex wait is waiting for a wake-up from another thread of the same process (futexes do not work between processes) |
| `https` fails with code -13 | The clock is wrong (check `date`) or the certificate has expired. With an unset clock (before 2024) dates are not checked at all |
| Boot stops at the skull "WRONG VERSION" | The kernel's code hash does not match its build signature (section 2.12). R restarts; C continues at your own risk |
| A `check.py` group fails with "code located" / "builds against the current ..." | The driver source was refactored and the extraction markers or replacement strings in `check.py` no longer match; update them |
| Hard-to-read generated-test failure | `NVX_KEEP_WORK=1` and inspect `*_t.c` in the kept work dir; add `--sanitize` |

---

## 12.10 Glossary of internal names

| Name | Meaning (file) |
|---|---|
| `g_jp[JMAX]` | process table, `struct jproc` (state, pid, pgid, sid, uid, cwd, cr3, brk, fds, signals, FS base, `runcpu`, FPU area); `JMAX` 256, one slot per thread (`core/proc/jobs.c`) |
| `S_FREE...S_ZOMB` | process states 0 free, 1 run, 2 wait, 3 sleep, 4 stop, 5 zombie |
| `g_jcur`, `g_js_running` | current process index and "a user process is running" (per-CPU macros) |
| `JS_BLOCK` (-1000) | return value of pipe/device reads and writes meaning "would block, reschedule" (`interrupts.c`, `jobs.c`, `unixfs.c`, `sound.c`) |
| `jc_*` | job control / scheduler (`jc_fault`, `jc_pick`, `jc_resume`, `jc_step`) |
| `g_proc[MAXPROC]` | older ring-3 demo scheduler (`core/proc/sched.c`, `MAXPROC` 6) |
| `FS[FS_N]` | in-memory files, `FsFile`, `FS_N` 1024, `FS_CAP` 1 GB (`fs/vfs/ramfs.c`) |
| `DT[DN_MAX]` | directories, `DNode`, `DN_MAX` 512 (`fs/vfs/dirtree.c`) |
| `g_lk[NLINKS]` | extra hard links, 256 (`fs/vfs/unixfs.c`) |
| `g_vf[VMAXOPEN]` | open-file (vfile) slots, 1024 (`fs/vfs/ramfs.c`) |
| NVX2 / NVX3 / NVX4 | on-disk formats: before v8.13, v8.13-v8.90, from v8.91 (`fs/nvx/nvx4.c`) |
| `g_n4`, `g_n4_err`, `g_nvx_note` | NVX4 mounted flag, last error, mount/convert note |
| BKL, `g_bkl` | big kernel lock: recursive ticket lock `bkl_lock/unlock/drop/take/yield` (`arch/x86_64/cpu/percpu.c`) |
| `struct pcpu`, `g_cpu[NCPU]`, `PCPU` | per-CPU state, `NCPU` 16, pointer in GS base |
| `PF_BASE/PF_TOP` | page-frame pool `0x0C000000-0x0E000000` (`mem/pmm_vmm.c`) |
| `DMA_ARENA_BASE` | `0x0B000000` |
| `PTE_LAZY` / `PTE_COW` | software PTE bits 0x400 / 0x200 |
| `HBLK_MAGIC` | heap block header magic `0x4E56484B` (`mem/heap.c`) |
| `HR_LAPIC_VEC` | 0x40, local APIC timer vector |
| `g_ticks` | 100 Hz PIT tick counter |
| `g_state`, `ST_*` | top-level state machine (`ST_LIVE`, `ST_DESKTOP`, ...) |
| `FBB`, `SCENE`, `W/H/PITCHB` | framebuffer, off-screen scene buffer, geometry |
| `NVX64SIG` | build-signature block checked by `sig_self_check`, `kcheck`, `emulate.py` |
| `MODS` | separately compiled driver modules (`build.sh`) |
| `kapi_*` | module API (`include/kapi.h`, `sys/native/kapi.c`) |
| `hk_*` / `fk_*` | host accessors in `hk_glue.c` / `fs_glue.c` |
| `xhc_t`, `xhid_*`, `xhub_*` | xHCI controller state, HID entries, hub code |

---

## 12.11 Version history

Compiled from version tags in the old `ARCHITECTURE.md` and in the sources. Only versions with evidence are listed; the current version string is `9.29` (`core/buildsig.c`, `fs/vfs/vfs.c` `/proc/version`, `sys/linux/linux.c` `uname`, `/sys/kernel/osrelease`).

| Version | Evidence | Change |
|---|---|---|
| v8.08 | `usr/libc/src/legacy_posix.h` section headers | nvxlibc: more math, file/path helpers, crypto, general utilities |
| v8.13 | `fs/vfs/dirtree.c` comment; old doc NVX4 "Conversion" | path-aware kernel file-management helpers; on-disk format NVX3 begins (NVX2 before) |
| v8.38 | `sys/native/syscall.c` comment, `shell/terminal.c` | identity, real-time and file-management syscalls |
| (undated) | old doc "Linux system-call ABI and musl libc" | Linux `syscall` ABI and musl 1.2.6 static-PIE programs (`mhello`, `musltest`); already present when the v8.88 items below were added. No v8.87 tag exists in the old doc or code |
| v8.88 | old doc, same section | file permissions via `perm_file`/`perm_dir`; `fs_drop_name`/`fs_slot_freed` deletion; name length limits and no "file name anywhere" lookup |
| (undated) | old doc "Ported programs" | sqlite3 3.53.4, zlib 1.3.2, bzip2 1.0.8, XZ 5.8.4, zstd 1.5.7 via `tools/mcc`. No v8.89 tag found |
| v8.90 | old doc sections | demand paging + copy-on-write; kernel modules and driver API (`kapi.h`, `MODS`, `drvsim`); e1000/RTL8111/RTL8139/VirtIO drivers; HD Audio/AC97 and `/dev/dsp` |
| v8.91 | old doc "NVX4 file system" | NVX4 on-disk format (extents, two CRC'd metadata copies, power-cut safety), growable files up to 1 GB, Unix semantics, NVX2/NVX3 conversion, `fs` checker group |
| v8.92 | old doc "Multi-core (SMP) scheduling" | per-CPU state, big kernel lock, scheduling on up to 16 cores, IPIs 0xF0-0xF2, `/dev/kmsg`, tmpfs-like `/tmp`, background writeback, 64-process table, `smptest`, `qemu` group |
| v8.93 | old doc "Since v8.93..." | cores start automatically at boot (`smp_boot_auto`), `/etc/smp` on/off/starting fallback |
| v8.94 | `core/buildsig.c` and other version strings; `hh.c` | The `linux` harness checks that the DNS query builder refuses over-long host names without overrunning its buffer and that `udp_send` refuses a payload larger than one Ethernet frame; the sources do not tag these fixes with a version number |
| v8.95 | this document, "What changed in v8.95" | Installer works again with a 5 MB kernel; `disks` no longer breaks AHCI; integrity check blocks the boot; Linux sockets (TCP/UDP, DNS); sockets and HTTPS over Wi-Fi; TLS certificate verification with 120 roots; Disk Manager reachable; TLB shootdown and idle-core wake-up in use; ARP responder; TCP MSS/window; shell `>>`/attached redirection fixes; `tls` and `net` checker groups |
| v8.96 | this document, "What changed in v8.96" | The kernel is split into 104 separately compiled units with interface headers under `include/nvx/`; incremental parallel builds with `tools/kbuild.py`; code-bearing `.h` files renamed to `.c` |
| v8.97 | this document, "What changed in v8.97" | Threads and futexes, dynamic linking with the shared musl, an escape-sequence terminal with termios, interval timers, limits that scale with RAM, server sockets and loopback, BearSSL and `fetch` for HTTPS in programs, compressed embedded programs; `featest`, `dyntest` and the `fetch` checker group |
| v8.98 to v9.07 | this document, "What changed in v8.98 to v9.07" | UEFI loader stops firmware DMA before copying the kernel; AHCI/NVMe bus-master order; 16 queued USB HID reads; classic taskbar, start menu and window frames; better CSS and images in the old browser |
| v9.08 | this document, "What changed in v9.08" | Web engine in `web/` with QuickJS, the cookie jar in `net/http/cookies.c`, the `qjs`, `browse` and `desktop` commands, boot stack moved to `0x980000` and the kernel limit raised to 8 MiB (Part 13) |
| v9.09 | this document, "What changed in v9.09" | Fix for a heap overflow in the web engine allocator that crashed the kernel in QuickJS |
| v9.10 | this document, "What changed in v9.10" | contenteditable editing, Selection/Range, MutationObserver, Streams, oklch and other colour functions, CSS nesting, auto-margin centring fix, ellipsis, line-clamp, sticky |
| v9.11 | this document, "What changed in v9.11" | FFmpeg and the `play` command, kernel image limit raised to 15 MiB, HD Audio response fix |
| v9.13 | this document, "What changed in v9.13" | 2D transforms, CSS animations and transitions, web fonts (TTF, WOFF, WOFF2 with a Brotli decoder, ligature icon fonts), Shadow DOM, faster style system |
| v9.12 | this document, "What changed in v9.12" | Media service and `/dev/media`, Video Player window, `<video>`/`<audio>` and MediaSource in the browser, binary-safe `fetch`/`XHR`/`Blob`, cheaper desktop redraws, checker templates updated |
| v9.14 | this document, "What changed in v9.14" | RAM up to 128 GiB (memory above 4 GiB mapped and pooled, E820 on BIOS boots), partition scanner for every disk, web engine crash containment, YouTube player page |
| v9.15 | this document, "What changed in v9.15" | Real youtube.com is loaded again, `Intl`, larger script time, heap and stack limits |
| v9.16 | this document, "What changed in v9.16" | The pointer keeps moving while the browser works, Esc stops a stuck page, timer budget, `data:` addresses, more script APIs |
| v9.17 | this document, "What changed in v9.17" | Layout on demand for scripts, crash counter reset per page |
| v9.18 | this document, "What changed in v9.18" | SVG text, `color-mix()` alpha, `min()` in grid tracks, `getComputedStyle` during load |
| v9.19 | this document, "What changed in v9.19" | `grid-template-areas`, `scrollHeight`, Google-style new tab page |
| v9.20 | this document, "What changed in v9.20" | Forced-layout budget (10x faster on measuring pages), input sizes, Enter as code 13 |
| v9.21 | this document, "What changed in v9.21" | Script time limits back to 6 s with an 8 s page total, load time report |
| v9.29 | this document, "What changed in v9.29" | current version. apt: all Debian components, update hint, bigger install plan |
| v9.28 | this document, "What changed in v9.28" | X apps on the desktop, app icons, .py opens in IDLE |
| v9.27 | this document, "What changed in v9.27" | Tkinter, Tk 8.6, IDLE, window manager |
| v9.26 | this document, "What changed in v9.26" | Keyboard and mouse for X programs, full-screen X session |
| v9.25 | this document, "What changed in v9.25" | X.Org X server and the X Display window |
| v9.24 | this document, "What changed in v9.24" | Python IDE package in the start menu |
| v9.23 | this document, "What changed in v9.23" | CPython 3.13, apt and dpkg for Debian packages, layered sysroot |
| v9.22 | this document, "What changed in v9.22" | glibc 2.44 and GNU tools, 100 MiB kernel limit |
