import sys, os, lzma, struct, ctypes
from concurrent.futures import ProcessPoolExecutor

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

ZLEVEL = int(os.environ.get('NVX_SR_ZSTD', '19'))
try:
    ctypes.CDLL('libzstd.so.1')
except OSError:
    if ZLEVEL > 0:
        print('  WARNING: libzstd.so.1 not found on the build machine, packing programs with xz (slower start)')
    ZLEVEL = 0
_zl = None
def zstd_compress(b):
    global _zl
    if _zl is None:
        _zl = ctypes.CDLL('libzstd.so.1')
        _zl.ZSTD_compressBound.restype = ctypes.c_size_t
        _zl.ZSTD_compress.restype = ctypes.c_size_t
        _zl.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        _zl.ZSTD_isError.restype = ctypes.c_uint
        _zl.ZSTD_isError.argtypes = [ctypes.c_size_t]
    cap = _zl.ZSTD_compressBound(len(b))
    o = ctypes.create_string_buffer(cap)
    n = _zl.ZSTD_compress(o, cap, b, len(b), ZLEVEL)
    if _zl.ZSTD_isError(n):
        raise SystemExit('zstd compression failed')
    return o.raw[:n]

def compress_block(job):
    kind, data, x86 = job
    if kind == 3:
        return 3, zstd_compress(data), len(data)
    filt = [{'id': lzma.FILTER_X86}] if x86 else []
    filt.append({'id': lzma.FILTER_LZMA2, 'preset': 9 | lzma.PRESET_EXTREME, 'dict_size': max(1 << 16, min(BLK, len(data)))})
    return 2, lzma.compress(data, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32, filters=filt), len(data)

jobs = []
def pack(group, x86, kind):
    cur = bytearray()
    pend = []
    def flush():
        if not pend:
            return
        bi = len(jobs)
        jobs.append((kind, bytes(cur), x86))
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

pack(elfs, True, 3 if ZLEVEL > 0 else 2)
ZPRE = tuple(x for x in os.environ.get('NVX_SR_ZSTD_DIRS', '/opt/Vesktop/').split(':') if x)
fast = [o for o in others if ZLEVEL > 0 and o[0].startswith(ZPRE)]
others = [o for o in others if not (ZLEVEL > 0 and o[0].startswith(ZPRE))]
pack(fast, False, 3)
pack(others, False, 3 if ZLEVEL > 0 and os.environ.get('NVX_SR_ALLZSTD') else 2)
with ProcessPoolExecutor(max_workers=os.cpu_count() or 1) as ex:
    blocks = list(ex.map(compress_block, jobs))
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
print(f"  sysroot: {len(entries)} entries in {len(blocks)} blocks ({sum(1 for b in blocks if b[0] == 3)} zstd, {sum(1 for b in blocks if b[0] == 2)} xz), {len(res)} bytes, {sum(len(d) for _, _, d in elfs + others)} bytes unpacked")
