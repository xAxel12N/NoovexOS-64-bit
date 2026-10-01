#!/usr/bin/env python3
# mkvdi.py - wrap a raw disk image in a fixed VirtualBox VDI so it attaches
# and boots directly (no VBoxManage convert needed).
import struct, uuid, sys
raw_path = sys.argv[1]; vdi_path = sys.argv[2]
disk_mb  = int(sys.argv[3]) if len(sys.argv) > 3 else 16

BLOCK = 1024 * 1024
raw = open(raw_path, "rb").read()
disk_size = disk_mb * BLOCK
if len(raw) > disk_size:
    disk_size = ((len(raw) + BLOCK - 1) // BLOCK) * BLOCK
raw = raw + b"\x00" * (disk_size - len(raw))
nblocks = disk_size // BLOCK

blocks_offset = 0x200
data_offset = (blocks_offset + nblocks * 4 + 0x1FF) & ~0x1FF

h = bytearray(blocks_offset)
h[0:40] = b"<<< Oracle VM VirtualBox Disk Image >>>\n"
struct.pack_into("<I", h, 0x40, 0xBEDA107F)     # signature
struct.pack_into("<I", h, 0x44, 0x00010001)     # version 1.1
struct.pack_into("<I", h, 0x48, 0x00000190)     # header size
struct.pack_into("<I", h, 0x4C, 2)              # image type: 2 = fixed
struct.pack_into("<I", h, 0x50, 0)              # flags
struct.pack_into("<I", h, 0x154, blocks_offset) # offset to block map
struct.pack_into("<I", h, 0x158, data_offset)   # offset to data
struct.pack_into("<I", h, 0x15C, 0)             # cylinders (auto)
struct.pack_into("<I", h, 0x160, 0)             # heads
struct.pack_into("<I", h, 0x164, 0)             # sectors
struct.pack_into("<I", h, 0x168, 512)           # sector size
struct.pack_into("<Q", h, 0x170, disk_size)     # disk size
struct.pack_into("<I", h, 0x178, BLOCK)         # block size
struct.pack_into("<I", h, 0x180, nblocks)       # blocks in image
struct.pack_into("<I", h, 0x184, nblocks)       # blocks allocated
h[0x188:0x198] = uuid.uuid4().bytes             # image uuid
h[0x1A8:0x1B8] = uuid.uuid4().bytes             # link uuid

bmap = bytearray(data_offset - blocks_offset)
for i in range(nblocks):
    struct.pack_into("<I", bmap, i * 4, i)      # fixed: block i -> slot i

with open(vdi_path, "wb") as f:
    f.write(h); f.write(bmap); f.write(raw)
print(f"  {vdi_path}  ({disk_size//BLOCK} MiB fixed VDI, {nblocks} blocks)")
