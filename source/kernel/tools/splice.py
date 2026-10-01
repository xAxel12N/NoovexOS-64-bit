#!/usr/bin/env python3
# NoovexOS 64 - splice the flat 64-bit kernel into the proven v6.87 boot
# container (MBR + stage2 + ISO9660 structure reused unchanged).
import sys, struct
KREGION = 1048576
ISO_OFF = 45568
USB_OFF = 2560

kern = open('kernel64.bin','rb').read()
assert len(kern) <= KREGION, f"kernel {len(kern)} > {KREGION//1048576} MiB region"
# sanity: size stamp at offset 24 matches
assert struct.unpack_from('<I', kern, 24)[0] == len(kern), "size@24 mismatch"
pad  = kern + b'\x00' * (KREGION - len(kern))

def splice(ref, out, off):
    d = bytearray(open(ref,'rb').read())
    # confirm we're overwriting the OLD kernel (multiboot magic at off+4) BEFORE growing
    old = struct.unpack_from('<I', d, off+4)[0]
    assert old == 0x1BADB002, f"{out}: no kernel magic at splice point (got 0x{old:08X})"
    if off + KREGION > len(d):          # grow the container to hold the bigger region
        d.extend(b'\x00' * (off + KREGION - len(d)))
    d[off:off+KREGION] = pad
    open(out,'wb').write(d)
    print(f"  {out}: spliced {len(kern)} B kernel + zero-pad into {KREGION} B region @ {off}  (image {len(d)} B)")

splice("/mnt/user-data/uploads/Noovex9_v6_87.iso",
       "/mnt/user-data/outputs/Noovex64 v0.74.iso", ISO_OFF)
splice("/mnt/user-data/uploads/Noovex9_v6_87_USB.img",
       "/mnt/user-data/outputs/Noovex64 v0.74 USB.img", USB_OFF)
