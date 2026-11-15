import sys, struct, hashlib
def v1(fw, rom):
    np = struct.unpack_from('<H', fw, 12)[0]; base = 14
    for i in range(np):
        if struct.unpack_from('<H', fw, base + 2*i)[0] == rom + 1:
            ln = struct.unpack_from('<H', fw, base + 2*np + 2*i)[0]; off = struct.unpack_from('<I', fw, base + 4*np + 4*i)[0]
            return fw[off:off+ln-4] + fw[8:12]
    return None
def v2(fw, rom, key):
    subs = []; o = 20; end = len(fw) - 7; nsec = struct.unpack_from('<I', fw, 16)[0]
    for _ in range(nsec):
        if end - o < 8: break
        op, sl = struct.unpack_from('<II', fw, o); o += 8
        if sl > end - o: break
        sd = fw[o:o+sl]; o += sl
        if op == 3 and not key: continue
        if op not in (1, 2, 3): continue
        num = struct.unpack_from('<H', sd, 0)[0]; q = 4
        for _ in range(num):
            if len(sd) - q < 8: break
            eco, prio, kid = sd[q], sd[q+1], sd[q+2]; ln = struct.unpack_from('<I', sd, q+4)[0]; q += 8
            if ln > len(sd) - q: break
            data = sd[q:q+ln]; q += ln
            if eco != rom + 1: continue
            if op == 3 and kid != key: continue
            at = len(subs)
            for i, s in enumerate(subs):
                if s[0] >= prio: at = i; break
            subs.insert(at, (prio, data))
    out = b''.join(d for _, d in subs)
    return out or None
fw = open(sys.argv[1], 'rb').read(); kind = sys.argv[2]
for rom in range(4):
    r = v1(fw, rom) if kind == 'v1' else v2(fw, rom, 0)
    print(rom, len(r) if r else -1, hashlib.sha256(r).hexdigest()[:16] if r else '-')
