#!/usr/bin/env python3
# mkiso.py - build a bootable ISO9660 + El Torito (no-emulation) image that
# boots the custom NoovexOS bootloader (cdboot) which then loads the kernel.
import subprocess, struct, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "boot.iso"
SS  = 2048                                   # CD sector size

def run(*a):
    r = subprocess.run(a, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr); raise SystemExit("failed: " + " ".join(a))
    return r.stdout

# --- assemble cdboot (flat @0x7C00) ---
run("gcc", "-m32", "-c", "boot/cdboot.S", "-o", "boot/cdboot.o")
run("ld", "-m", "elf_i386", "-Ttext", "0x7C00", "-e", "_start", "-o", "boot/cdboot.elf", "boot/cdboot.o")
syms = {p[2]: int(p[0], 16) for p in (l.split() for l in run("nm", "boot/cdboot.elf").splitlines()) if len(p) == 3}
run("objcopy", "-O", "binary", "-j", ".text", "boot/cdboot.elf", "boot/cdboot.bin")
cdboot = bytearray(open("boot/cdboot.bin", "rb").read())

kern = open("kernel64.bin", "rb").read()

# --- ISO layout (in 2048-byte LBAs) ---
LBA_PVD   = 16
LBA_BRVD  = 17          # El Torito boot record volume descriptor
LBA_TERM  = 18
LBA_ROOT  = 19          # root directory
LBA_CAT   = 20          # boot catalog
LBA_BOOT  = 21          # cdboot image
boot_secs = (len(cdboot) + SS - 1) // SS
LBA_KERN  = LBA_BOOT + boot_secs
kern_secs = (len(kern) + SS - 1) // SS
total     = LBA_KERN + kern_secs

# patch kernel location into cdboot (sector count in 512-byte units for El Torito
# is separate; here we patch CD-sector LBA + CD-sector count that cdboot reads)
struct.pack_into("<I", cdboot, syms["kern_lba"]  - 0x7C00, LBA_KERN)
struct.pack_into("<I", cdboot, syms["kern_secs"] - 0x7C00, kern_secs)

def both32(v):  return struct.pack("<I", v) + struct.pack(">I", v)   # LSB-MSB
def both16(v):  return struct.pack("<H", v) + struct.pack(">H", v)
def dtime():    return bytes([0]*7)

img = bytearray(total * SS)
def put(lba, data): img[lba*SS:lba*SS+len(data)] = data

# ---- root directory (one sector): '.' and '..' records ----
def dirrec(lba, size, flags, name):
    r = bytearray()
    ln = 33 + len(name) + (1 - (len(name) & 1))
    r += bytes([ln, 0]) + both32(lba) + both32(size) + dtime() + bytes([flags,0,0]) + both16(1)
    r += bytes([len(name)]) + name
    if len(r) < ln: r += b"\x00" * (ln - len(r))
    return r
root = bytearray()
root += dirrec(LBA_ROOT, SS, 0x02, b"\x00")      # .
root += dirrec(LBA_ROOT, SS, 0x02, b"\x01")      # ..
put(LBA_ROOT, root)

# ---- Primary Volume Descriptor ----
pvd = bytearray(SS)
pvd[0] = 1                                        # type = PVD
pvd[1:6] = b"CD001"; pvd[6] = 1
pvd[8:40]   = b" " * 32                           # system id
pvd[40:72]  = b"NOOVEXOS64".ljust(32)             # volume id
pvd[80:88]  = both32(total)                       # volume space size
pvd[120:124]= both16(1)                           # volume set size
pvd[124:128]= both16(1)                           # volume sequence number
pvd[128:132]= both16(SS)                          # logical block size
pvd[132:140]= both32(0)                           # path table size (empty)
struct.pack_into("<I", pvd, 140, 0)               # L path table location (none)
struct.pack_into(">I", pvd, 148, 0)               # M path table location (none)
pvd[156:190]= dirrec(LBA_ROOT, SS, 0x02, b"\x00") # root directory record
pvd[190:318]= b" " * 128
pvd[318:446]= b" " * 128
pvd[446:574]= b" " * 128
pvd[574:702]= b" " * 128
put(LBA_PVD, pvd)

# ---- El Torito Boot Record Volume Descriptor ----
brvd = bytearray(SS)
brvd[0] = 0
brvd[1:6] = b"CD001"; brvd[6] = 1
brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
struct.pack_into("<I", brvd, 71, LBA_CAT)         # pointer to boot catalog
put(LBA_BRVD, brvd)

# ---- Volume Descriptor Set Terminator ----
term = bytearray(SS)
term[0] = 0xFF; term[1:6] = b"CD001"; term[6] = 1
put(LBA_TERM, term)

# ---- Boot Catalog ----
cat = bytearray(SS)
# validation entry: header(1) platform(0) ... checksum @28-29, keys 0x55AA @30-31
val = bytearray(32)
val[0] = 1                                        # header id
val[1] = 0                                        # platform 0 = 80x86
val[30] = 0x55                                    # key byte
val[31] = 0xAA                                    # key byte
val[28:30] = b"\x00\x00"                          # checksum field starts at 0
s = sum(struct.unpack("<16H", bytes(val)))
struct.pack_into("<H", val, 28, (-s) & 0xFFFF)    # all 16 words now sum to 0
cat[0:32] = val
# default (initial) entry: no emulation
ent = bytearray(32)
ent[0] = 0x88                                     # bootable
ent[1] = 0x00                                     # no emulation
struct.pack_into("<H", ent, 2, 0)                 # load segment (0 -> 0x7C00)
ent[4] = 0                                        # system type
load_512 = (len(cdboot) + 511) // 512
struct.pack_into("<H", ent, 6, load_512)          # sector count (512-byte units)
struct.pack_into("<I", ent, 8, LBA_BOOT)          # boot image LBA
cat[32:64] = ent
put(LBA_CAT, cat)

# ---- boot image + kernel ----
put(LBA_BOOT, cdboot)
put(LBA_KERN, kern)

open(OUT, "wb").write(img)
print(f"  cdboot {len(cdboot)} B @LBA{LBA_BOOT} (load {load_512}x512) | kernel {len(kern)} B @LBA{LBA_KERN} ({kern_secs} CD-sectors)")
print(f"  -> {OUT}  ({len(img)} bytes)")
