import struct, sys

def check(img, crash=False):
    errs = []
    b = img[:512]
    if b[3:11] != b'EXFAT   ' or b[510:512] != b'\x55\xaa': return ['not an exFAT boot sector']
    fatoff, fatlen, heap, cnt, root = struct.unpack_from('<IIIII', b, 80)
    vflags = struct.unpack_from('<H', b, 106)[0]; bss = b[108]; csh = b[109]; bs = 1 << bss; cb = bs << csh
    if vflags & 2 and not crash: errs.append('VolumeDirty set')
    s = 0
    for i in range(11 * bs):
        if i in (106, 107, 112): continue
        s = (((s & 1) << 31) | (s >> 1)) + img[i] & 0xffffffff
    for k in range(bs // 4):
        if struct.unpack_from('<I', img, 11 * bs + 4 * k)[0] != s: errs.append('boot checksum sector mismatch'); break
    fat = lambda c: struct.unpack_from('<I', img, fatoff * bs + 4 * c)[0]
    sec = lambda c: (heap + ((c - 2) << csh)) * bs
    used = {}
    def chain(c, nofat, length, what):
        n = (length + cb - 1) // cb; out = []
        for i in range(n):
            if not 2 <= c < cnt + 2: errs.append('%s: bad cluster %d' % (what, c)); break
            if c in used: errs.append('%s: cluster %d also used by %s' % (what, c, used[c]))
            used[c] = what; out.append(c)
            nx = c + 1 if nofat else fat(c)
            if not nofat and i == n - 1 and nx != 0xffffffff: errs.append('%s: chain not terminated' % what)
            c = nx
        return out
    def rdc(cl, length): return b''.join(img[sec(c):sec(c) + cb] for c in cl)[:length]
    rc = []; c = root
    while 2 <= c < cnt + 2 and len(rc) <= cnt: rc.append(c); c = fat(c)
    chain(root, 0, len(rc) * cb, 'root')
    up = None; bm = None
    def walk(data, path):
        nonlocal up, bm
        i = 0; names = set()
        while i + 32 <= len(data):
            t = data[i]
            if t == 0: break
            if t == 0x81:
                cl, ln = struct.unpack_from('<IQ', data, i + 20); bm = rdc(chain(cl, 0, ln, 'bitmap'), ln)
            elif t == 0x82:
                cl, ln = struct.unpack_from('<IQ', data, i + 20); up = rdc(chain(cl, 0, ln, 'upcase'), ln); us = 0
                for x in up: us = (((us & 1) << 31) | (us >> 1)) + x & 0xffffffff
                if us != struct.unpack_from('<I', data, i + 4)[0]: errs.append('up-case table checksum')
            elif t == 0x85:
                sc = data[i + 1]; st = data[i:i + 32 * (sc + 1)]
                if len(st) < 32 * (sc + 1) or st[32] != 0xC0: errs.append('%s: broken entry set' % path); i += 32; continue
                h = 0
                for k, x in enumerate(st):
                    if k in (2, 3): continue
                    h = (((h & 1) << 15) | (h >> 1)) + x & 0xffff
                if h != struct.unpack_from('<H', st, 2)[0]: errs.append('%s: set checksum' % path)
                for k in range(1, sc + 1):
                    if st[32 * k] < 0xC0: errs.append('%s: unused entry inside a set' % path)
                attr = struct.unpack_from('<H', st, 4)[0]; fl = st[33]; nl = st[35]; nh = struct.unpack_from('<H', st, 36)[0]
                vdl, = struct.unpack_from('<Q', st, 40); cl, ln = struct.unpack_from('<IQ', st, 52)
                nm = []
                for k in range(2, sc + 1): nm += list(struct.unpack_from('<15H', st, 32 * k + 2))
                nm = nm[:nl]; name = struct.pack('<%dH' % nl, *nm).decode('utf-16-le', 'replace')
                if sc != 1 + (nl + 14) // 15: errs.append('%s/%s: secondary count' % (path, name))
                if up:
                    tb = {}; q = 0; at = 0
                    while q + 1 < len(up):
                        v = struct.unpack_from('<H', up, q)[0]
                        if v == 0xffff and q + 3 < len(up): at += struct.unpack_from('<H', up, q + 2)[0]; q += 4; continue
                        tb[at] = v; at += 1; q += 2
                    hh = 0
                    for ch in nm:
                        u = tb.get(ch, ch)
                        for byte in (u & 0xff, u >> 8): hh = (((hh & 1) << 15) | (hh >> 1)) + byte & 0xffff
                    if hh != nh: errs.append('%s/%s: name hash' % (path, name))
                    key = tuple(tb.get(ch, ch) for ch in nm)
                    if key in names: errs.append('%s/%s: duplicate name' % (path, name))
                    names.add(key)
                if vdl > ln: errs.append('%s/%s: valid data length > size' % (path, name))
                cls = chain(cl, (fl >> 1) & 1, ln, path + '/' + name) if ln else []
                if attr & 0x10:
                    if ln % cb or not ln: errs.append('%s/%s: directory size %d' % (path, name, ln))
                    walk(rdc(cls, ln), path + '/' + name)
                i += 32 * sc
            i += 32
    walk(rdc(rc, len(rc) * cb), '')
    if bm is None: return errs + ['no allocation bitmap']
    for c in range(2, cnt + 2):
        bit = (bm[(c - 2) >> 3] >> ((c - 2) & 7)) & 1
        if bit and c not in used and not crash: errs.append('cluster %d marked used but not referenced' % c)
        if not bit and c in used: errs.append('cluster %d used by %s but free in bitmap' % (c, used[c]))
        if len(errs) > 40: break
    return errs

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); off = int(sys.argv[2]) * 512 if len(sys.argv) > 2 else 0
    e = check(d[off:])
    print('\n'.join(e) if e else 'exfat_verify: clean'); sys.exit(1 if e else 0)
