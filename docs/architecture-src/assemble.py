import re, sys, os
D = os.path.dirname(os.path.abspath(__file__))
PARTS = ['01-overview-build.md', '02-boot-signatures.md', '03-memory.md', '04-cpu-smp.md', '05-processes.md',
         '06-linux-porting.md', '07-filesystems.md', '08-storage-installer.md', '09-network.md', '10-devices.md',
         '11-desktop-shell.md', '12-testing-debugging.md', '13-web-engine.md']
VERSION = '9.82'

def slug(text, seen):
    t = re.sub(r'`', '', text).strip().lower()
    t = re.sub(r'[^\w\- ]', '', t, flags=re.UNICODE)
    t = t.replace(' ', '-')
    base = t; n = seen.get(base, 0)
    seen[base] = n + 1
    return base if n == 0 else f'{base}-{n}'

def demote(md):
    out = []; fence = False
    for line in md.split('\n'):
        if line.lstrip().startswith('```'): fence = not fence
        if not fence and re.match(r'^#{1,5} ', line): line = '#' + line
        out.append(line)
    return '\n'.join(out).strip('\n')

def headings(md):
    fence = False
    for line in md.split('\n'):
        if line.lstrip().startswith('```'): fence = not fence; continue
        if fence: continue
        m = re.match(r'^(#{1,6}) (.*)$', line)
        if m: yield len(m.group(1)), m.group(2).strip()

def short(title):
    return re.sub(r'^Part \d+ — ', '', title)

header = f'''# NoovexOS 64 Architecture and Developer Reference

Version {VERSION}. x86-64, BIOS and UEFI, written from scratch.

This is the developer reference for NoovexOS 64: a from-scratch x86-64 operating system with its own boot loaders, kernel, drivers, file system, network stack, desktop and Linux-compatible user space. It was written from the source code, with file paths and symbol names so you can find the implementation. Where the code does something surprising or has a known gap, the text says so.

## At a glance

| Area | What is there | Part |
|---|---|---|
| Boot | BIOS loader (myboot) and UEFI loader, hybrid ISO/USB image, SHA-256 build signature that stops the boot if the kernel was modified | [2](#part-2-boot-chain-kernel-entry-and-kernel-signatures) |
| Kernel | 146 separately compiled C and C++ units with interface headers under `include/nvx/`, a 77 MB flat image (100 MB limit) with compressed programs, the web engine, glibc, GNU tools and CPython 3.13, identity-mapped kernel, demand paging and copy-on-write for programs | [1](#part-1-overview-source-layout-and-build-system), [3](#part-3-memory) |
| CPU | Up to 16 cores with per-CPU state, a ticket big kernel lock, IPIs for rescheduling, TLB shootdown and halt; cores start automatically | [4](#part-4-cpu-interrupts-timers-and-multi-core) |
| Programs | 256 processes and threads, futexes, signals, timers, job control, pipes; native `int 0x80` ABI and a Linux `syscall` ABI that runs static and dynamic musl programs (SQLite, gzip, xz, zstd, the kilo editor, the `fetch` HTTPS client) | [5](#part-5-processes-scheduling-signals-and-the-native-system-call-abi), [6](#part-6-linux-compatibility-libc-and-porting-applications) |
| Files | NVX4 (extents, two checksummed metadata copies, safe against power cuts), FAT, NTFS via ntfs-3g, Unix permissions | [7](#part-7-file-systems) |
| Storage | ATA PIO, AHCI, NVMe, VirtIO-blk, USB mass storage; graphical installer and Disk Manager | [8](#part-8-storage-drivers-pci-and-the-installer) |
| Network | PCnet, e1000, RTL8139/8169, VirtIO-net, RTL8821CE Wi-Fi with WPA2; IPv4, DHCP, DNS, TCP/UDP, loopback; client and server sockets for Linux programs; TLS 1.3 in the kernel and BearSSL TLS 1.2 for programs, with certificate checks | [9](#part-9-networking-and-wi-fi) |
| Devices | PS/2 and USB (xHCI) input, I2C touchpads, ACPI with an AML interpreter, HD Audio/AC97, framebuffer and GPU detection, an NVIDIA Turing driver through the GSP firmware (`nvgsp`) | [10](#part-10-input-usb-acpi-sound-and-graphics-drivers) |
| Desktop | Compositing desktop, apps, file explorer, an xterm-style terminal, the `nsh` shell with scripting and Lua | [11](#part-11-desktop-applications-shell-and-scripting) |
| Web | A browser engine: HTML parser and DOM, CSS cascade, layout with floats, positioning, tables, flexbox and grid, QuickJS JavaScript with the browser API, a cookie jar saved to disk, and the `qjs` command | [13](#part-13-the-web-engine-javascript-and-cookies) |
| Testing | `tools/check.py` (build, static analysis, host tests under sanitizers, QEMU multi-core and network runs), boot emulation, serial log, panic screen | [12](#part-12-testing-quality-gates-debugging-and-history) |

## How to read this document

- New to the code: read "What changed in v{VERSION}", then Parts 1 and 2 for the build and boot, then the part for the area you want to change.
- Porting a program: go to Part 6; the step-by-step guide is in section 6.9.
- Chasing a bug: start with the troubleshooting table in section 12.9 and the serial log in section 12.5.
- Paths are relative to `source/kernel/` unless they start with `source/`, `tools/` or `bootloader/`.

'''

def main():
    whatsnew = demote(open(os.path.join(D, '00-whatsnew.md')).read())
    parts = [demote(open(os.path.join(D, p)).read()) for p in PARTS]
    seen = {}
    for h in ['NoovexOS 64 Architecture and Developer Reference', 'At a glance', 'How to read this document']: slug(h, seen)
    for lv, t in headings(whatsnew): slug(t, seen)
    slug('Contents', seen)
    toc = ['## Contents', '']
    for md in parts:
        items = []; part = None
        for lv, t in headings(md):
            s = slug(t, seen)
            if lv == 2: part = (t, s)
            elif lv == 3: items.append(f'- [{t}](#{s})')
        toc.append(f'- [{part[0]}](#{part[1]})')
        toc += ['  ' + it for it in items]
    body = header + whatsnew + '\n\n' + '\n'.join(toc) + '\n\n' + '\n\n'.join(parts) + '\n'
    body = re.sub(r'\n{3,}', '\n\n', body)
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(D, 'ARCHITECTURE.md')
    open(out, 'w').write(body)
    anchors = set(); seen2 = {}
    for lv, t in headings(body): anchors.add(slug(t, seen2))
    bad = sorted(set(m for m in re.findall(r'\]\(#([^)]+)\)', body) + re.findall(r'href="#([^"]+)"', body) if m not in anchors))
    print('lines', body.count('\n'), 'broken links', len(bad))
    for b in bad: print('  missing #' + b)

main()
