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

def build_fat32(part_lba, part_sectors, root_files, root_dirs):
    bps = SECTOR
    spc = 1
    reserved = 32
    nfats = 2
    root_clus = 2
    tmp1 = part_sectors - reserved
    tmp2 = (256 * spc + nfats) // 2
    fatsz = (tmp1 + (tmp2 - 1)) // tmp2
    data_start = reserved + nfats * fatsz
    cluster_bytes = bps * spc
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
    bs[71:82] = b'MYBOOT ESP '
    bs[82:90] = b'FAT32   '
    struct.pack_into('<H', bs, 510, 0xAA55)

    fsi = 1 * bps
    struct.pack_into('<I', img, fsi + 0, 0x41615252)
    struct.pack_into('<I', img, fsi + 484, 0x61417272)
    struct.pack_into('<I', img, fsi + 488, 0xFFFFFFFF)
    struct.pack_into('<I', img, fsi + 492, 0xFFFFFFFF)
    struct.pack_into('<I', img, fsi + 508, 0xAA550000)
    img[6 * bps:6 * bps + bps] = bytes(bs[0:bps])

    def set_fat(c, val):
        for f in range(nfats):
            off = (reserved + f * fatsz) * bps + c * 4
            struct.pack_into('<I', img, off, val & 0x0FFFFFFF)

    def cluster_off(c):
        return (data_start + (c - 2) * spc) * bps

    set_fat(0, 0x0FFFFFF8)
    set_fat(1, 0x0FFFFFFF)

    next_free = [3]
    def alloc_cluster():
        c = next_free[0]
        next_free[0] += 1
        set_fat(c, 0x0FFFFFFF)
        return c

    def write_chain(data):
        if len(data) == 0:
            return 0
        nclus = (len(data) + cluster_bytes - 1) // cluster_bytes
        clus = [alloc_cluster() for _ in range(nclus)]
        for i in range(nclus):
            set_fat(clus[i], 0x0FFFFFFF if i == nclus - 1 else clus[i + 1])
        for i, c in enumerate(clus):
            chunk = data[i * cluster_bytes:(i + 1) * cluster_bytes]
            o = cluster_off(c)
            img[o:o + len(chunk)] = chunk
        return clus[0]

    def dir_entry(name11, attr, first, size):
        ent = bytearray(32)
        ent[0:11] = name11
        ent[11] = attr
        struct.pack_into('<H', ent, 20, (first >> 16) & 0xFFFF)
        struct.pack_into('<H', ent, 26, first & 0xFFFF)
        struct.pack_into('<I', ent, 28, size)
        return bytes(ent)

    def write_dir(first_cluster, entries):
        epc = cluster_bytes // 32
        needed = (len(entries) + 1 + epc - 1) // epc
        if needed < 1:
            needed = 1
        chain = [first_cluster]
        while len(chain) < needed:
            c = alloc_cluster()
            set_fat(chain[-1], c)
            set_fat(c, 0x0FFFFFFF)
            chain.append(c)
        idx = 0
        for c in chain:
            o = cluster_off(c)
            for e in range(epc):
                if idx < len(entries):
                    img[o + e * 32:o + e * 32 + 32] = entries[idx]
                    idx += 1

    def build_dir(files, dirs, dir_cluster, parent_cluster, is_root):
        entries = []
        if not is_root:
            entries.append(dir_entry(b'.          ', 0x10, dir_cluster, 0))
            entries.append(dir_entry(b'..         ', 0x10, parent_cluster, 0))
        for name, sub in dirs.items():
            sub_cluster = alloc_cluster()
            sub_parent = 0 if is_root else dir_cluster
            build_dir(sub.get('files', []), sub.get('dirs', {}), sub_cluster, sub_parent, False)
            entries.append(dir_entry(to83(name), 0x10, sub_cluster, 0))
        for name, data in files:
            first = write_chain(data)
            entries.append(dir_entry(to83(name), 0x20, first, len(data)))
        write_dir(dir_cluster, entries)

    set_fat(2, 0x0FFFFFFF)
    build_dir(root_files, root_dirs, 2, 0, True)
    return img

def main():
    args = sys.argv[1:]
    efi_file = None
    if '--efi' in args:
        i = args.index('--efi')
        efi_file = args[i + 1]
        del args[i:i + 2]
    if len(args) < 3:
        print("usage: mkimage.py <boot.bin> <stage2.bin> <disk.img> [file ...] [--efi BOOTX64.EFI]")
        sys.exit(1)
    boot = open(args[0], 'rb').read()
    stage2 = open(args[1], 'rb').read()
    outpath = args[2]
    root_files = []
    for path in args[3:]:
        root_files.append((os.path.basename(path), open(path, 'rb').read()))

    root_dirs = {}
    if efi_file:
        data = open(efi_file, 'rb').read()
        root_dirs = {'EFI': {'files': [], 'dirs': {'BOOT': {'files': [('BOOTX64.EFI', data)], 'dirs': {}}}}}

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
    disk[pe + 4] = 0xEF
    disk[pe + 5] = c_end[0]
    disk[pe + 6] = c_end[1]
    disk[pe + 7] = c_end[2]
    struct.pack_into('<I', disk, pe + 8, PART_LBA)
    struct.pack_into('<I', disk, pe + 12, PART_SECTORS)

    disk[SECTOR:SECTOR + len(stage2)] = stage2

    fat = build_fat32(PART_LBA, PART_SECTORS, root_files, root_dirs)
    disk[PART_LBA * SECTOR:PART_LBA * SECTOR + len(fat)] = fat

    open(outpath, 'wb').write(disk)
    print("wrote %s (%d bytes)" % (outpath, len(disk)))
    print("  MBR partition type 0xEF (EFI System), LBA %d" % PART_LBA)
    for name, data in root_files:
        print("  /%s: %d bytes" % (to83(name).decode().strip(), len(data)))
    if efi_file:
        print("  /EFI/BOOT/BOOTX64.EFI: %d bytes" % len(open(efi_file, 'rb').read()))

if __name__ == '__main__':
    main()
