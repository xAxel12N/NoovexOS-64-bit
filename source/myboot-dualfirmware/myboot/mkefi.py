import sys, subprocess, struct

def main():
    elf = sys.argv[1]
    out = sys.argv[2]
    syms = {}
    for line in subprocess.check_output(['nm', elf]).decode().splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    subprocess.check_call(['objcopy', '-O', 'binary', '-j', '.text', '-j', '.rodata', '-j', '.data', elf, out + '.blob'])
    blob = open(out + '.blob', 'rb').read()
    image_start = 0x1000
    entry = syms['efi_main'] - image_start
    vsize = syms['__image_end'] - image_start
    blob_size = len(blob)
    if vsize < blob_size:
        vsize = blob_size

    FA = 0x200
    SA = 0x1000
    IMAGEBASE = 0x400000
    SEC_RVA = 0x1000

    dos = bytearray(0x80)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 0x80)

    coff = bytearray(24)
    coff[0:4] = b'PE\x00\x00'
    struct.pack_into('<H', coff, 4, 0x8664)
    struct.pack_into('<H', coff, 6, 1)
    struct.pack_into('<H', coff, 20, 240)
    struct.pack_into('<H', coff, 22, 0x0022)

    raw = (blob_size + FA - 1) & ~(FA - 1)
    headers_size = 0x80 + 24 + 240 + 40
    size_headers = (headers_size + FA - 1) & ~(FA - 1)
    size_image = (SEC_RVA + vsize + SA - 1) & ~(SA - 1)

    opt = bytearray(240)
    struct.pack_into('<H', opt, 0, 0x20B)
    struct.pack_into('<I', opt, 4, raw)
    struct.pack_into('<I', opt, 16, SEC_RVA + entry)
    struct.pack_into('<I', opt, 20, SEC_RVA)
    struct.pack_into('<Q', opt, 24, IMAGEBASE)
    struct.pack_into('<I', opt, 32, SA)
    struct.pack_into('<I', opt, 36, FA)
    struct.pack_into('<I', opt, 56, size_image)
    struct.pack_into('<I', opt, 60, size_headers)
    struct.pack_into('<H', opt, 68, 10)
    struct.pack_into('<Q', opt, 72, 0x100000)
    struct.pack_into('<Q', opt, 80, 0x1000)
    struct.pack_into('<Q', opt, 88, 0x100000)
    struct.pack_into('<Q', opt, 96, 0x1000)
    struct.pack_into('<I', opt, 108, 16)

    sec = bytearray(40)
    sec[0:5] = b'.text'
    struct.pack_into('<I', sec, 8, vsize)
    struct.pack_into('<I', sec, 12, SEC_RVA)
    struct.pack_into('<I', sec, 16, raw)
    struct.pack_into('<I', sec, 20, size_headers)
    struct.pack_into('<I', sec, 36, 0xE0000020)

    o = bytearray()
    o += dos + coff + opt + sec
    o += b'\x00' * (size_headers - len(o))
    o += blob
    o += b'\x00' * (raw - blob_size)
    open(out, 'wb').write(bytes(o))
    print("wrote %s (%d bytes, EFI application, entry rva %#x)" % (out, len(o), SEC_RVA + entry))

if __name__ == '__main__':
    main()
