#!/usr/bin/env python3
# mkboot.py - assemble the custom NoovexOS bootloader (mbr + stage2) and build a
# raw bootable disk image:  [MBR @LBA0][stage2 @LBA1..32][kernel64.bin @LBA33..]
import subprocess, struct, os, sys

STAGE2_LBA   = 1
STAGE2_MAX   = 32 * 512            # 16 KiB reserved for stage2
KERNEL_LBA   = 33                  # matches kern_lba in stage2.S
OUT = sys.argv[1] if len(sys.argv) > 1 else "boot.img"

def run(*a):
    r = subprocess.run(a, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr); raise SystemExit(f"failed: {' '.join(a)}")
    return r.stdout

# --- assemble MBR (flat binary @0x7C00) ---
run("gcc", "-m32", "-c", "boot/mbr.S", "-o", "boot/mbr.o")
run("ld", "-m", "elf_i386", "-Ttext", "0x7C00", "-e", "_start", "-o", "boot/mbr.elf", "boot/mbr.o")
run("objcopy", "-O", "binary", "-j", ".text", "boot/mbr.elf", "boot/mbr.bin")
mbr = open("boot/mbr.bin", "rb").read()
assert len(mbr) == 512, f"MBR is {len(mbr)} bytes (must be 512)"
assert mbr[510:512] == b"\x55\xAA", "MBR missing boot signature"

# --- assemble stage2 (flat binary @0x8000) ---
run("gcc", "-m32", "-c", "boot/stage2.S", "-o", "boot/stage2.o")
run("ld", "-m", "elf_i386", "-Ttext", "0x8000", "-e", "_start2", "-o", "boot/stage2.elf", "boot/stage2.o")
# find kern_secs symbol offset within the binary
syms = run("nm", "boot/stage2.elf")
addr = {}
for line in syms.splitlines():
    p = line.split()
    if len(p) == 3: addr[p[2]] = int(p[0], 16)
run("objcopy", "-O", "binary", "-j", ".text", "boot/stage2.elf", "boot/stage2.bin")
stage2 = bytearray(open("boot/stage2.bin", "rb").read())
assert len(stage2) <= STAGE2_MAX, f"stage2 is {len(stage2)} bytes > {STAGE2_MAX}"

# --- patch kern_secs (and confirm kern_lba) into stage2 ---
kern = open("kernel64.bin", "rb").read()
ksecs = (len(kern) + 511) // 512
ks_off = addr["kern_secs"] - 0x8000
kl_off = addr["kern_lba"]  - 0x8000
struct.pack_into("<I", stage2, ks_off, ksecs)
struct.pack_into("<I", stage2, kl_off, KERNEL_LBA)

# --- assemble the disk image ---
img = bytearray()
img += mbr                                        # LBA 0
img += bytes(stage2) + b"\x00" * (STAGE2_MAX - len(stage2))   # LBA 1..32
assert len(img) == KERNEL_LBA * 512
img += kern                                       # LBA 33..
# round up to a whole MiB
if len(img) % (1024*1024): img += b"\x00" * (1024*1024 - len(img) % (1024*1024))
open(OUT, "wb").write(img)
print(f"  MBR 512 B | stage2 {len(stage2)} B (kernel @LBA{KERNEL_LBA}, {ksecs} sectors) | kernel {len(kern)} B")
print(f"  -> {OUT}  ({len(img)} bytes, {len(img)//(1024*1024)} MiB)")
