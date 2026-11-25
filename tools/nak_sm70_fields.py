#!/usr/bin/env python3
import re, sys


def _close(s, i, o='(', c=')'):
    d = 1
    while d:
        d += (s[i] == o) - (s[i] == c); i += 1
    return i


def _blocks(src):
    for m in re.finditer(r"^impl (?:SM70Op for (Op\w+)|SM70Encoder<'_>) \{", src, re.M):
        yield m.group(1), src[m.end():_close(src, m.end(), '{', '}')]


def _fns(body):
    r = {}
    for m in re.finditer(r'\bfn (\w+)\s*(?:<[^>]*>)?\(', body):
        s = body.index('{', m.end()); r[m.group(1)] = body[s + 1:_close(body, s + 1, '{', '}')]
    return r


def _args(a):
    out, d, cur = [], 0, ''
    for ch in a:
        if ch in '([{': d += 1
        if ch in ')]}': d -= 1
        if ch == ',' and d == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    return out + [cur.strip()]


def _bits(body, helpers, seen):
    b = set()
    for m in re.finditer(r'\b(?:e|self)\.(set_\w+|encode_\w+)\(', body):
        name, s = m.group(1), m.end()
        a = body[s:_close(body, s) - 1]
        for x, y in re.findall(r'\b(\d+)\.\.(\d+)\b', a): b |= set(range(int(x), int(y)))
        for x in re.findall(r'\{\s*(\d+)\s*\}', a): b.add(int(x))
        args = _args(a)
        for x in args:
            if re.fullmatch(r'\d+', x): b.add(int(x))
        if name.startswith('set_ureg') or name == 'set_udst':
            st = 16 if name == 'set_udst' else (int(args[0]) if re.fullmatch(r'\d+', args[0]) else None)
            if st is not None: b |= set(range(st, st + 6))
        if name in helpers and name not in seen: b |= _bits(helpers[name], helpers, seen | {name})
    return b


def table(src):
    helpers, ops = {}, {}
    bl = list(_blocks(src))
    for n, body in bl:
        if n is None: helpers.update(_fns(body))
    base = _bits(helpers['set_pred'], helpers, set()) | _bits(helpers['set_instr_deps'], helpers, set())
    for n, body in bl:
        if n is None: continue
        f = _fns(body)
        if 'encode' not in f: continue
        e = f['encode']
        codes, form = set(), 'set_field(9..12' in e
        for m in re.finditer(r'\b(?:e|self)\.(encode_\w*alu\w*|set_opcode)\(\s*(0x[0-9a-fA-F]+)', e):
            codes.add((int(m.group(2), 16) & (0x1ff if form else 0xfff), 9 if m.group(1) != 'set_opcode' or form else 12))
        for m in re.finditer(r'\b(0x[0-9a-fA-F]{3})\b(?!_)', e):
            v = int(m.group(1), 16)
            if not any(c == (v & 0x1ff if form else v) for c, _ in codes): codes.add((v, 12))
        if not codes: continue
        bits = _bits(e, helpers, set()) | base | set(range(0, 12))
        ops[n] = (sorted(codes), bits)
    return ops


def ranges(bits):
    out, s = [], sorted(bits)
    i = 0
    while i < len(s):
        j = i
        while j + 1 < len(s) and s[j + 1] == s[j] + 1: j += 1
        out.append('%d-%d' % (s[i], s[j] + 1)); i = j + 1
    return ','.join(out)


def dump(ops, want=None):
    lines = []
    for n in sorted(ops):
        codes, bits = ops[n]
        if want and not any((c, w) in want for c, w in codes): continue
        lines.append('%s %s %s' % (n, ','.join('%03x/%d' % c for c in codes), ranges(bits)))
    return '\n'.join(lines) + '\n'


def load(path):
    ops = {}
    for ln in open(path):
        p = ln.split()
        if len(p) != 3: continue
        codes = [(int(c.split('/')[0], 16), int(c.split('/')[1])) for c in p[1].split(',')]
        bits = set()
        for r in p[2].split(','):
            a, b = r.split('-'); bits |= set(range(int(a), int(b)))
        ops[p[0]] = (codes, bits)
    return ops


def match(ops, v):
    hit = []
    for n, (codes, bits) in ops.items():
        for c, w in codes:
            if (v & ((1 << w) - 1)) == c and (w == 12 or (v >> 9) & 7 in (1, 2, 3, 4, 5, 6, 7)): hit.append(n); break
    return hit


def check(ops, v):
    hit = match(ops, v)
    if not hit: return None, 'opcode 0x%03x is not produced by the NAK sm70 encoder' % (v & 0xfff)
    allowed = set()
    for n in hit: allowed |= ops[n][1]
    stray = [k for k in range(128) if (v >> k) & 1 and k not in allowed]
    if stray: return hit, 'bits %s are outside every field NAK writes for %s' % (stray, '/'.join(hit))
    return hit, None


def main():
    if len(sys.argv) < 3:
        print('usage: nak_sm70_fields.py sm70_encode.rs out.txt [opnames...]'); return 2
    ops = table(open(sys.argv[1]).read())
    sel = {n: ops[n] for n in sys.argv[3:]} if len(sys.argv) > 3 else ops
    missing = [n for n in sys.argv[3:] if n not in ops]
    if missing: print('not in NAK:', missing); return 1
    open(sys.argv[2], 'w').write(dump(sel)); print('%d ops written' % len(sel)); return 0


if __name__ == '__main__':
    sys.exit(main())
