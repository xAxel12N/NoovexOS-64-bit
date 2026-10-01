import sys, os, struct

SECTOR = 512
PART_LBA = 2048
PART_SECTORS = 131072

def to83(name):
    out = bytearray(b' ' * 11)
    if '.' in name:
        base, ext = name.split('.', 1)
    else:
        base, ext = name, ''
    base = base.upper()[:8]
    ext = ext.upper()[:3]
    for i, ch in enumerate(base):
        out[i] = ord(ch)
    for i, ch in enumerate(ext):
        out[8 + i] = ord(ch)
    return bytes(out)

def build_fat32(part_lba, part_sectors, files):
    bps = SECTOR
    spc = 1
    reserved = 32
    nfats = 2
    root_clus = 2

    tmp1 = part_sectors - reserved
    tmp2 = (256 * spc + nfats) // 2
    fatsz = (tmp1 + (tmp2 - 1)) // tmp2

    data_start = reserved + nfats * fatsz
    img = bytearray(part_sectors * bps)

    bs = img
    bs[0:3] = b'\xEB\x58\x90'
    bs[3:11] = b'MYBOOT  '
    struct.pack_into('<H', bs, 11, bps)
    bs[13] = spc
    struct.pack_into('<H', bs, 14, reserved)
    bs[16] = nfats
    struct.pack_into('<H', bs, 17, 0)
    struct.pack_into('<H', bs, 19, 0)
    bs[21] = 0xF8
    struct.pack_into('<H', bs, 22, 0)
    struct.pack_into('<H', bs, 24, 32)
    struct.pack_into('<H', bs, 26, 64)
    struct.pack_into('<I', bs, 28, part_lba)
    struct.pack_into('<I', bs, 32, part_sectors)
    struct.pack_into('<I', bs, 36, fatsz)
    struct.pack_into('<H', bs, 40, 0)
    struct.pack_into('<H', bs, 42, 0)
    struct.pack_into('<I', bs, 44, root_clus)
    struct.pack_into('<H', bs, 48, 1)
    struct.pack_into('<H', bs, 50, 6)
    bs[64] = 0x80
    bs[66] = 0x29
    struct.pack_into('<I', bs, 67, 0x12345678)
    bs[71:82] = b'NO NAME    '
    bs[82:90] = b'FAT32   '
    struct.pack_into('<H', bs, 510, 0xAA55)

    fsi = 1 * bps
    struct.pack_into('<I', img, fsi + 0, 0x41615252)
    struct.pack_into('<I', img, fsi + 484, 0x61417272)
    struct.pack_into('<I', img, fsi + 488, 0xFFFFFFFF)
    struct.pack_into('<I', img, fsi + 492, 0xFFFFFFFF)
    struct.pack_into('<I', img, fsi + 508, 0xAA550000)

    img[6 * bps:6 * bps + bps] = bytes(bs[0:bps])

    def set_fat(cluster, value):
        for f in range(nfats):
            off = (reserved + f * fatsz) * bps + cluster * 4
            struct.pack_into('<I', img, off, value & 0x0FFFFFFF)

    def cluster_off(c):
        return (data_start + (c - 2) * spc) * bps

    set_fat(0, 0x0FFFFFF8)
    set_fat(1, 0x0FFFFFFF)
    set_fat(2, 0x0FFFFFFF)

    next_free = 3
    root_chain = [2]
    dir_entries = []

    def add_file(name, data):
        nonlocal next_free
        n83 = to83(name)
        size = len(data)
        if size == 0:
            first = 0
        else:
            nclus = (size + bps * spc - 1) // (bps * spc)
            clus = []
            for i in range(nclus):
                clus.append(next_free)
                next_free += 1
            for i in range(nclus):
                val = 0x0FFFFFFF if i == nclus - 1 else clus[i + 1]
                set_fat(clus[i], val)
            for i, c in enumerate(clus):
                chunk = data[i * bps * spc:(i + 1) * bps * spc]
                o = cluster_off(c)
                img[o:o + len(chunk)] = chunk
            first = clus[0]
        ent = bytearray(32)
        ent[0:11] = n83
        ent[11] = 0x20
        struct.pack_into('<H', ent, 20, (first >> 16) & 0xFFFF)
        struct.pack_into('<H', ent, 26, first & 0xFFFF)
        struct.pack_into('<I', ent, 28, size)
        dir_entries.append(bytes(ent))

    for name, data in files:
        add_file(name, data)

    epc = (bps * spc) // 32
    needed = (len(dir_entries) + 1 + epc - 1) // epc
    if needed < 1:
        needed = 1
    while len(root_chain) < needed:
        c = next_free
        next_free += 1
        set_fat(root_chain[-1], c)
        set_fat(c, 0x0FFFFFFF)
        root_chain.append(c)

    idx = 0
    for c in root_chain:
        o = cluster_off(c)
        for e in range(epc):
            if idx < len(dir_entries):
                img[o + e * 32:o + e * 32 + 32] = dir_entries[idx]
                idx += 1

    return img

def chs(lba):
    spt = 32
    heads = 64
    if lba >= 1024 * heads * spt:
        return (0xFE, 0xFF, 0xFF)
    cyl = lba // (heads * spt)
    rem = lba % (heads * spt)
    head = rem // spt
    sect = (rem % spt) + 1
    return (head & 0xFF, ((cyl >> 2) & 0xC0) | (sect & 0x3F), cyl & 0xFF)

def main():
    if len(sys.argv) < 4:
        print("usage: mkimage.py <boot.bin> <stage2.bin> <disk.img> [file ...]")
        sys.exit(1)
    boot = open(sys.argv[1], 'rb').read()
    stage2 = open(sys.argv[2], 'rb').read()
    outpath = sys.argv[3]
    files = []
    for path in sys.argv[4:]:
        files.append((os.path.basename(path), open(path, 'rb').read()))

    if len(boot) > SECTOR:
        raise SystemExit("boot sector too large: %d bytes" % len(boot))
    if len(stage2) > 64 * SECTOR:
        raise SystemExit("stage2 too large: %d bytes (max %d)" % (len(stage2), 64 * SECTOR))

    total = PART_LBA + PART_SECTORS
    disk = bytearray(total * SECTOR)

    disk[0:len(boot)] = boot
    struct.pack_into('<H', disk, 510, 0xAA55)

    pe = 0x1BE
    c_start = chs(PART_LBA)
    c_end = chs(PART_LBA + PART_SECTORS - 1)
    disk[pe + 0] = 0x80
    disk[pe + 1] = c_start[0]
    disk[pe + 2] = c_start[1]
    disk[pe + 3] = c_start[2]
    disk[pe + 4] = 0x0C
    disk[pe + 5] = c_end[0]
    disk[pe + 6] = c_end[1]
    disk[pe + 7] = c_end[2]
    struct.pack_into('<I', disk, pe + 8, PART_LBA)
    struct.pack_into('<I', disk, pe + 12, PART_SECTORS)

    disk[SECTOR:SECTOR + len(stage2)] = stage2

    fat = build_fat32(PART_LBA, PART_SECTORS, files)
    disk[PART_LBA * SECTOR:PART_LBA * SECTOR + len(fat)] = fat

    open(outpath, 'wb').write(disk)
    print("wrote %s (%d bytes, %d sectors)" % (outpath, len(disk), total))
    print("  stage2: %d bytes" % len(stage2))
    for name, data in files:
        print("  /%s: %d bytes" % (to83(name).decode().strip(), len(data)))

if __name__ == '__main__':
    main()
