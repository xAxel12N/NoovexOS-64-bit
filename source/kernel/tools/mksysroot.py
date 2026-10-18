import sys, os, lzma, struct

BLK = 8 << 20
out = sys.argv[1]
items = []
for spec in sys.argv[2:]:
    dst, src = spec.split('=', 1)
    if os.path.isdir(src):
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for d in list(dirs):
                if os.path.islink(os.path.join(root, d)):
                    files.append(d)
            for f in sorted(files):
                p = os.path.join(root, f)
                rel = os.path.relpath(p, src).replace(os.sep, '/')
                items.append(((dst.rstrip('/') + '/' + rel) if dst.strip('/') else '/' + rel, p))
    else:
        items.append((dst, src))

links, elfs, others = [], [], []
for name, path in items:
    if os.path.islink(path):
        tgt = os.readlink(path)
        if not tgt.startswith('/'):
            tgt = os.path.normpath(os.path.join(os.path.dirname(name), tgt)).replace(os.sep, '/')
        links.append((name, tgt))
        continue
    data = open(path, 'rb').read()
    mode = 1 if os.access(path, os.X_OK) else 0
    (elfs if data[:4] == b'\x7fELF' else others).append((name, mode, data))

blocks = []
entries = []

def pack(group, x86):
    cur = bytearray()
    pend = []
    def flush():
        if not pend:
            return
        filt = [{'id': lzma.FILTER_X86}] if x86 else []
        filt.append({'id': lzma.FILTER_LZMA2, 'preset': 9 | lzma.PRESET_EXTREME, 'dict_size': max(1 << 16, min(BLK, len(cur)))})
        z = lzma.compress(bytes(cur), format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32, filters=filt)
        bi = len(blocks)
        blocks.append((2, z, len(cur)))
        for name, mode, off, ln in pend:
            entries.append((name, '', mode, bi, off, ln))
        cur.clear()
        pend.clear()
    for name, mode, data in group:
        if cur and len(cur) + len(data) > BLK:
            flush()
        pend.append((name, mode, len(cur), len(data)))
        cur += data
    flush()

pack(elfs, True)
pack(others, False)
for name, tgt in links:
    entries.append((name, tgt, 2, 0, 0, 0))

strtab = bytearray()
soff = {}
def s(x):
    if x not in soff:
        soff[x] = len(strtab)
        strtab.extend(x.encode() + b'\0')
    return soff[x]
s('')
erecs = [(s(n), s(t), m, b, o, l) for n, t, m, b, o, l in entries]
str_off = 16 + len(blocks) * 16 + len(erecs) * 24
data_off = (str_off + len(strtab) + 15) & ~15
btab = bytearray()
blob = bytearray()
for t, z, ul in blocks:
    btab += struct.pack('<IIII', t, data_off + len(blob), len(z), ul)
    blob += z
    blob += b'\0' * ((-len(blob)) & 15)
res = bytearray(b'NVS2') + struct.pack('<III', len(erecs), len(blocks), str_off) + btab
for r in erecs:
    res += struct.pack('<IIIIII', *r)
res += strtab
res += b'\0' * (data_off - len(res))
res += blob
open(out, 'wb').write(bytes(res))
print(f"  sysroot: {len(entries)} entries in {len(blocks)} xz blocks, {len(res)} bytes, {sum(len(d) for _, _, d in elfs + others)} bytes unpacked")
