#!/usr/bin/env python3
"""mkhybrid.py  boot.bin  stage2.bin  out.iso  [VOLID]  path=file ...

Builds ONE .iso that works like an Ubuntu image:
  * burned to CD  -> boots via El Torito (hard-disk emulation of the whole image)
  * flashed to USB (dd / Rufus DD / Etcher) -> boots via the MBR in the ISO9660
    "system area" (first 32 KB, unused by ISO9660), and the stick shows a
    browsable FAT32 partition WITH FOLDERS (boot/, doom/, ...)

Layout (512-byte LBA):
  0          MBR  (myboot stage1 + partition table -> FAT32 @ LBA 2048)
  1..63      stage2 (myboot loads 64 sectors from LBA 1)   } ISO system area
  64..       ISO9660 descriptors + El Torito catalog + small ISO tree
  2048..     FAT32 partition (kernel, WAD, cfg, README in folders)

File args are  fatpath=hostfile   e.g.  boot/kernel64.bin=stage/kernel64.bin
"""
import struct, sys, os

S512, S2K = 512, 2048
PART_LBA = 2048

def to83(name):
    out = bytearray(b' ' * 11)
    base, ext = (name.split('.', 1) + [''])[:2] if '.' in name else (name, '')
    for i, ch in enumerate(base.upper()[:8]): out[i] = ord(ch)
    for i, ch in enumerate(ext.upper()[:3]):  out[8 + i] = ord(ch)
    return bytes(out)

# ------------------------------------------------------------ FAT32 with folders
def build_fat32(part_lba, part_sectors, tree, label='NOOVEXOS'):
    bps, spc, reserved, nfats, root_clus = S512, 1, 32, 2, 2
    tmp1 = part_sectors - reserved; tmp2 = (256 * spc + nfats) // 2
    fatsz = (tmp1 + (tmp2 - 1)) // tmp2
    data_start = reserved + nfats * fatsz
    img = bytearray(part_sectors * bps)
    bs = img
    bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'MYBOOT  '
    struct.pack_into('<H', bs, 11, bps); bs[13] = spc; struct.pack_into('<H', bs, 14, reserved); bs[16] = nfats
    bs[21] = 0xF8; struct.pack_into('<H', bs, 24, 32); struct.pack_into('<H', bs, 26, 64)
    struct.pack_into('<I', bs, 28, part_lba); struct.pack_into('<I', bs, 32, part_sectors)
    struct.pack_into('<I', bs, 36, fatsz); struct.pack_into('<I', bs, 44, root_clus)
    struct.pack_into('<H', bs, 48, 1); struct.pack_into('<H', bs, 50, 6)
    bs[64] = 0x80; bs[66] = 0x29; struct.pack_into('<I', bs, 67, 0x4E56584F)
    bs[71:82] = label.encode('ascii')[:11].ljust(11, b' '); bs[82:90] = b'FAT32   '
    struct.pack_into('<H', bs, 510, 0xAA55)
    fsi = bps
    struct.pack_into('<I', img, fsi, 0x41615252); struct.pack_into('<I', img, fsi + 484, 0x61417272)
    struct.pack_into('<I', img, fsi + 488, 0xFFFFFFFF); struct.pack_into('<I', img, fsi + 492, 0xFFFFFFFF)
    struct.pack_into('<I', img, fsi + 508, 0xAA550000)
    img[6 * bps:7 * bps] = bytes(bs[0:bps])

    def set_fat(c, v):
        for f in range(nfats):
            struct.pack_into('<I', img, (reserved + f * fatsz) * bps + c * 4, v & 0x0FFFFFFF)
    def coff(c): return (data_start + (c - 2) * spc) * bps
    set_fat(0, 0x0FFFFFF8); set_fat(1, 0x0FFFFFFF); set_fat(2, 0x0FFFFFFF)
    state = {'next': 3}
    def alloc(n):
        cl = list(range(state['next'], state['next'] + n)); state['next'] += n
        for i, c in enumerate(cl): set_fat(c, 0x0FFFFFFF if i == n - 1 else cl[i + 1])
        return cl

    # directories: path -> {'clus': [first...], 'entries': [bytes]}
    dirs = {'': {'clus': [root_clus], 'entries': []}}
    def dirent(n83, attr, first, size):
        e = bytearray(32); e[0:11] = n83; e[11] = attr
        struct.pack_into('<H', e, 20, (first >> 16) & 0xFFFF); struct.pack_into('<H', e, 26, first & 0xFFFF)
        struct.pack_into('<I', e, 28, size); return bytes(e)
    def ensure_dir(path):
        if path in dirs: return dirs[path]
        parent, _, name = path.rpartition('/')
        pd = ensure_dir(parent)
        c = alloc(1)[0]
        d = {'clus': [c], 'entries': [dirent(b'.          ', 0x10, c, 0), dirent(b'..         ', 0x10, 0 if pd['clus'][0] == root_clus else pd['clus'][0], 0)]}
        dirs[path] = d
        pd['entries'].append(dirent(to83(name), 0x10, c, 0))
        return d
    def add_file(path, data):
        dpath, _, name = path.rpartition('/')
        d = ensure_dir(dpath)
        first = 0
        if data:
            cl = alloc((len(data) + bps * spc - 1) // (bps * spc))
            for i, c in enumerate(cl):
                chunk = data[i * bps * spc:(i + 1) * bps * spc]; img[coff(c):coff(c) + len(chunk)] = chunk
            first = cl[0]
        d['entries'].append(dirent(to83(name), 0x20, first, len(data)))

    for path, data in tree: add_file(path, data)

    # write every directory (grow with extra clusters if needed)
    epc = (bps * spc) // 32
    for path, d in dirs.items():
        ents = d['entries']
        needed = max(1, (len(ents) + 1 + epc - 1) // epc)
        while len(d['clus']) < needed:
            c = alloc(1)[0]; set_fat(d['clus'][-1], c); d['clus'].append(c)
        for i, e in enumerate(ents):
            c = d['clus'][i // epc]; o = coff(c) + (i % epc) * 32; img[o:o + 32] = e
    used = state['next']
    assert used * spc + data_start <= part_sectors, "FAT partition too small for contents"
    return bytes(img), used

# ------------------------------------------------------------ ISO9660 helpers
def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)
def pad(b, n): return b + b'\0' * (n - len(b))
def strA(s, n): return s.encode('ascii')[:n].ljust(n, b' ')
def drec(ident, lba, size, is_dir):
    rec = b'\0\0' + both32(lba) + both32(size) + bytes([100, 0, 1, 0, 0, 0, 0]) + bytes([2 if is_dir else 0]) + b'\0\0' + both16(1)
    rec += struct.pack('<B', len(ident)) + ident
    if len(rec) % 2: rec += b'\0'
    return bytes([len(rec)]) + rec[1:]

def main():
    if len(sys.argv) < 4: print(__doc__); sys.exit(1)
    boot = open(sys.argv[1], 'rb').read(); stage2 = open(sys.argv[2], 'rb').read(); out = sys.argv[3]
    args = sys.argv[4:]
    volid = 'NOOVEXOS'
    if args and '=' not in args[0]: volid = args[0]; args = args[1:]
    tree = []
    for a in args:
        fp, hp = a.split('=', 1); tree.append((fp, open(hp, 'rb').read()))
    # UEFI: include the EFI bootloader on the ESP path so firmware can boot it
    import os
    _efi = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), 'uefi', 'BOOTX64.EFI')
    if os.path.exists(_efi):
        tree.append(('EFI/BOOT/BOOTX64.EFI', open(_efi, 'rb').read()))
        print('  + UEFI: /EFI/BOOT/BOOTX64.EFI added to ESP')
    assert len(boot) == 512 and len(stage2) <= 63 * S512, "boot must be 512B, stage2 <= 63 sectors"

    # --- FAT32 partition sized to contents (+ 8 MB slack), min 16 MB ---
    total_bytes = sum(len(d) for _, d in tree)
    part_sectors = max(32768, ((total_bytes + 8 * 1024 * 1024) // S512 + 2048) & ~2047)
    fat, used = build_fat32(PART_LBA, part_sectors, tree, volid)

    # --- MBR: myboot stage1 + partition entry ---
    mbr = bytearray(boot)
    mbr[0x1BE:0x1FE] = b'\0' * 64
    mbr[0x1BE] = 0x80; mbr[0x1BE + 1:0x1BE + 4] = b'\x01\x01\x00'; mbr[0x1BE + 4] = 0x0C; mbr[0x1BE + 5:0x1BE + 8] = b'\xFE\xFF\xFF'
    struct.pack_into('<I', mbr, 0x1BE + 8, PART_LBA); struct.pack_into('<I', mbr, 0x1BE + 12, part_sectors)
    struct.pack_into('<H', mbr, 510, 0xAA55)

    total_512 = PART_LBA + part_sectors
    total_2k = total_512 // 4

    # --- ISO9660 area (LBA 16..) : README lives in the ISO tree so a mounted CD shows something ---
    L_PVD, L_BR, L_TERM, L_CAT, L_PTL, L_PTM, L_ROOT, L_README = 16, 17, 18, 19, 20, 21, 22, 23
    readme = (f"{volid} hybrid image\r\n\r\nThis disc boots as a CD and, when written raw to a USB stick\r\n"
              f"(dd / Rufus DD mode / Etcher), also boots from USB. The stick then shows a\r\n"
              f"FAT32 partition with the files in folders (boot/, doom/, ...).\r\n").encode()
    root = drec(b'\x00', L_ROOT, S2K, True) + drec(b'\x01', L_ROOT, S2K, True) + drec(b'README.TXT;1', L_README, len(readme), False)
    root = pad(root, S2K)
    ptl = pad(struct.pack('<BBIH', 1, 0, L_ROOT, 1) + b'\0\0', S2K); ptm = pad(struct.pack('>BBIH', 1, 0, L_ROOT, 1) + b'\0\0', S2K)
    pvd = b'\x01CD001\x01\0' + strA('NOOVEXOS', 32) + strA(volid, 32) + b'\0' * 8 + both32(total_2k) + b'\0' * 32
    pvd += both16(1) + both16(1) + both16(S2K) + both32(10)
    pvd += struct.pack('<I', L_PTL) + b'\0\0\0\0' + struct.pack('>I', L_PTM) + b'\0\0\0\0'
    pvd += drec(b'\x00', L_ROOT, S2K, True) + strA(volid, 128) + strA('', 128) * 3 + strA('', 37) * 3
    dt = b'0' * 16 + b'\0'; pvd += dt * 4 + b'\x01\0'; pvd = pad(pvd, S2K)
    br = pad(b'\x00CD001\x01' + pad(b'EL TORITO SPECIFICATION', 32) + b'\0' * 32 + struct.pack('<I', L_CAT), S2K)
    term = pad(b'\xFFCD001\x01', S2K)
    val = struct.pack('<BBH', 1, 0, 0) + strA(volid, 24) + b'\0\0' + b'\x55\xAA'
    cs = sum(struct.unpack('<16H', val)) & 0xFFFF; val = val[:28] + struct.pack('<H', (0x10000 - cs) & 0xFFFF) + val[30:]
    # HDD emulation of the WHOLE image: load RBA 0 => emulated disk sector n == image byte n*512
    default = struct.pack('<BBHBBHI', 0x88, 0x04, 0, 0x0C, 0, 1, 0) + b'\0' * 20
    # --- EFI El Torito section: platform 0xEF, one entry pointing at the FAT partition ---
    # Section header: header_id=0x91 (final), platform=0xEF, count=1
    efi_hdr = struct.pack('<BBH', 0x91, 0xEF, 1) + b'\0' * 28
    # EFI entry: boot indicator 0x88, media 0 (no emulation), load the FAT partition.
    # sector_count in virtual 512 sectors of the ESP; load RBA in 2K CD sectors = PART_LBA/4.
    efi_sectors = min(0xFFFF, part_sectors)
    efi_entry = struct.pack('<BBHBBHI', 0x88, 0x00, 0, 0, 0, efi_sectors & 0xFFFF, PART_LBA // 4) + b'\0' * 20
    cat = pad(val + default + efi_hdr + efi_entry, S2K)

    iso = bytearray(total_512 * S512)
    iso[0:512] = mbr
    iso[512:512 + len(stage2)] = stage2
    for lba, blk in ((L_PVD, pvd), (L_BR, br), (L_TERM, term), (L_CAT, cat), (L_PTL, ptl), (L_PTM, ptm), (L_ROOT, root), (L_README, pad(readme, S2K))):
        iso[lba * S2K:lba * S2K + S2K] = blk
    iso[PART_LBA * S512:PART_LBA * S512 + len(fat)] = fat
    open(out, 'wb').write(iso)
    print(f"wrote {out}: {len(iso)} bytes ({total_2k} CD sectors); FAT32 @LBA {PART_LBA}, {part_sectors} sectors, {used} clusters used")
    for fp, d in tree: print(f"  /{fp}: {len(d)} bytes")

if __name__ == '__main__':
    main()
