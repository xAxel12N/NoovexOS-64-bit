#!/usr/bin/env python3
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nak_sm70_fields as nak

ALU = {0x002, 0x007, 0x00c, 0x010, 0x011, 0x012, 0x016, 0x017, 0x019, 0x024, 0x025}
UALU = {0x082, 0x090, 0x092, 0x099}
FIXED = {0xab9: 'ULDC', 0xb82: 'LDC', 0x919: 'S2R', 0x918: 'NOP', 0x947: 'BRA', 0x94d: 'EXIT',
         0x945: 'BSSY', 0x941: 'BSYNC', 0x355: 'BMOV', 0x381: 'LDG', 0x386: 'STG'}
UNVERIFIED = {0x949: 'BRX'}
MEM = {0x381, 0x386}
SM80_ORDER = {(0, None): 0x4, (1, None): 0x0, (2, 0): 0x5, (2, 2): 0x7, (2, 3): 0xa}
STALL_FLOOR = 15
NAK_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nvsass_nak.txt')
IMAD_CARRY = {0x024, 0x025}
M32 = 0xffffffff


def fld(v, a, b): return (v >> a) & ((1 << (b - a)) - 1)
def put(v, a, b, x): return (v & ~(((1 << (b - a)) - 1) << a)) | ((x & ((1 << (b - a)) - 1)) << a)
def sext(x, n): return x - (1 << n) if x >> (n - 1) else x
def guard(v): return fld(v, 12, 15), fld(v, 15, 16)
def bra_target(v, i): return i + 1 + sext(fld(v, 34, 82), 48) // 4


def mk_isetp(ctl, reg, imm, cmp):
    v = 0x80c | (7 << 12) | (reg << 24) | (imm << 32) | (7 << 68) | (cmp << 76) | (7 << 84) | (7 << 87)
    return put(v, 105, 128, fld(ctl, 105, 128))


def mk_bra(ctl, i, t):
    return put(0x947 | (((t - i - 1) * 4 & (2**48 - 1)) << 34) | (7 << 87), 105, 128, fld(ctl, 105, 128))


def mk_nop(ctl): return put(0x918 | (7 << 12), 105, 128, fld(ctl, 105, 128))


def neg_exits(w, t, lo, hi, reg):
    st, p, n = {reg: (lo, hi)}, {7: True}, 0
    while n < 32:
        v, n = w[t], n + 1
        g, gn = guard(v)
        if g not in p: return False
        on = p[g] != bool(gn)
        op = v & 0xfff
        if op == 0x94d:
            if on: return True
            t += 1; continue
        if op == 0x947:
            if fld(v, 32, 33) or fld(v, 87, 91) != 7: return False
            t = bra_target(v, t) if on else t + 1; continue
        if g != 7 or gn: return False
        if op == 0x810 and fld(v, 24, 32) in st and fld(v, 64, 72) == 0xff and not fld(v, 72, 73) and not fld(v, 75, 76):
            a, b = st[fld(v, 24, 32)]; k = fld(v, 32, 64)
            if (a + k) >> 32 != (b + k) >> 32: return False
            st[fld(v, 16, 24)] = ((a + k) & M32, (b + k) & M32); t += 1; continue
        if op == 0x80c and fld(v, 24, 32) in st and not fld(v, 73, 76) and fld(v, 87, 91) == 7 and fld(v, 68, 72) == 7:
            a, b = st[fld(v, 24, 32)]; k, c = fld(v, 32, 64), fld(v, 76, 79)
            f = {1: lambda x: x < k, 2: lambda x: x == k, 3: lambda x: x <= k, 4: lambda x: x > k, 5: lambda x: x != k, 6: lambda x: x >= k}.get(c)
            if f is None: return False
            ra, rb = f(a), f(b)
            if c in (2, 5) and a != b and a <= k <= b: return False
            if ra != rb: return False
            p[fld(v, 81, 84)] = ra; t += 1; continue
        return False
    return False


def reads(v, r):
    if (v & 0xfff) in (0x947, 0x94d, 0x918, 0x945, 0x941): return False
    f = (v >> 9) & 7 if (v & 0x1ff) in ALU | UALU else 1
    return fld(v, 24, 32) == r or (f in (1,) and fld(v, 32, 40) == r) or (f in (1, 2, 3, 4, 5) and fld(v, 64, 72) == r) or ((v & 0xfff) in MEM and fld(v, 32, 40) == r)


def writes(v, r):
    op = v & 0x1ff
    if (v & 0xfff) in FIXED or op not in ALU or op == 0x00c: return False
    g, gn = guard(v)
    return g == 7 and not gn and (fld(v, 16, 24) == r or (op == 0x025 and fld(v, 16, 24) + 1 == r))


def preads(v, q):
    g, _ = guard(v)
    if g == q: return True
    if (v & 0x1ff) not in (0x00c, 0x007, 0x012, 0x010, 0x011, 0x017) and (v & 0xfff) not in (0x947, 0x945, 0x941): return False
    return fld(v, 87, 90) == q or ((v & 0x1ff) == 0x00c and fld(v, 68, 71) == q)


def pwrites(v, q):
    g, gn = guard(v)
    return (v & 0x1ff) == 0x00c and g == 7 and not gn and fld(v, 81, 84) == q


def dead(w, t, regs, preds):
    todo, seen = [(t, frozenset(regs), frozenset(preds))], set()
    while todo:
        i, rs, ps = todo.pop()
        if (i, rs, ps) in seen or not rs and not ps: continue
        seen.add((i, rs, ps))
        if i >= len(w) or len(seen) > 4096: return False
        v = w[i]
        if any(reads(v, r) for r in rs) or any(preads(v, q) for q in ps): return False
        rs = frozenset(r for r in rs if not writes(v, r)); ps = frozenset(q for q in ps if not pwrites(v, q))
        g, gn = guard(v)
        if (v & 0xfff) == 0x94d:
            if g == 7 and not gn: continue
            todo.append((i + 1, rs, ps)); continue
        if (v & 0xfff) == 0x947:
            todo.append((bra_target(v, i), rs, ps))
            if g != 7 or gn: todo.append((i + 1, rs, ps))
            continue
        todo.append((i + 1, rs, ps))
    return True


def debrx(w, cb2):
    out, notes = list(w), []
    for b, v in enumerate(w):
        if v & 0xfff != 0x949: continue
        mn, mul, ldc, shf, g1, b1 = (w[b - k] for k in (4, 3, 2, 1, 6, 5))
        k = fld(mn, 24, 32); n = fld(mn, 32, 64) + 1; ra = fld(mul, 16, 24); rt = fld(ldc, 16, 24)
        ok = (mn & 0xfff) == 0x817 and fld(mn, 16, 24) == k and not fld(mn, 73, 74) and fld(mn, 87, 91) == 7 and \
             (mul & 0xfff) == 0x824 and fld(mul, 24, 32) == k and fld(mul, 32, 64) == 4 and fld(mul, 64, 72) == 0xff and \
             (ldc & 0xfff) == 0xb82 and fld(ldc, 24, 32) == ra and fld(ldc, 54, 59) == 2 and fld(ldc, 38, 54) == 0 and fld(ldc, 73, 76) == 4 and \
             (shf & 0xfff) == 0x819 and fld(v, 24, 32) == rt and fld(shf, 16, 24) == rt + 1 and guard(v) == (7, 0) and \
             (g1 & 0xfff) == 0x80c and fld(g1, 24, 32) == k and fld(g1, 73, 74) == 1 and fld(g1, 76, 79) == 4 and fld(g1, 32, 64) == n - 2 and \
             fld(g1, 87, 91) == 7 and fld(g1, 68, 72) == 7 and not fld(g1, 74, 76) and (b1 & 0xfff) == 0x947 and guard(b1) == (fld(g1, 81, 84), 0)
        if not ok or len(cb2) < 4 * n: raise ValueError('BRX@%d is not the switch pattern this tool can rewrite' % b)
        base = b * 16 + 16 + sext(fld(v, 34, 82), 48) * 4
        tab = [(struct.unpack_from('<i', cb2, 4 * j)[0] + base) // 16 for j in range(n)]
        dflt, cases, fall = tab[-1], tab[:-1], b + 1
        if out[dflt] & 0xfff != 0x94d or guard(out[dflt]) != (7, 0): raise ValueError('BRX@%d: default case is not a plain EXIT' % b)
        if not neg_exits(w, bra_target(b1, b - 5), 0x80000000, M32, k): raise ValueError('BRX@%d: cannot prove negative indexes exit on the range path' % b)
        q = fld(g1, 81, 84)
        for t in set(cases):
            if not dead(w, t, {ra, rt, rt + 1}, {q}): raise ValueError('BRX@%d: registers changed by the rewrite are live at %d' % (b, t))
        jumps = [(j, t) for j, t in enumerate(cases) if t != fall]
        if 2 * len(jumps) > 5: raise ValueError('BRX@%d: %d cases do not fit in 5 slots' % (b, len(jumps)))
        out[b - 6] = put(g1, 73, 74, 0)
        seq = []
        for j, t in jumps: seq += [('s', j), ('b', t)]
        for s, (kind, x) in zip(range(b - 4, b + 1), seq + [('n', 0)] * (5 - len(seq))):
            out[s] = mk_isetp(g1, k, x, 2) if kind == 's' else mk_bra(b1, s, x) if kind == 'b' else mk_nop(w[b])
            if kind == 's': out[s] = put(out[s], 81, 84, q)
            if kind == 'b': out[s] = put(out[s], 12, 15, q)
        notes.append('BRX@%d -> ISETP.EQ.U32/BRA chain %s, negative index via unsigned range check at %d' % (b, ','.join('%d:%d' % jt for jt in jumps), b - 6))
    return out, notes


def opname(v):
    full = v & 0xfff
    if full in FIXED: return FIXED[full]
    if full in UNVERIFIED: return UNVERIFIED[full]
    base = v & 0x1ff
    if base in ALU and (v >> 9) & 7 in (1, 2, 3, 4, 5, 6, 7): return 'alu%03x' % base
    if base in UALU and (v >> 9) & 7 in (1, 2, 4, 6): return 'ualu%03x' % base
    return None


def port_insn(v):
    name = opname(v)
    if name is None: raise ValueError('opcode 0x%03x has no NAK-checked sm_80+ encoding' % (v & 0xfff))
    if (v & 0xfff) in MEM:
        scope, order = (v >> 77) & 3, (v >> 79) & 3
        key = (order, None) if order < 2 else (order, scope)
        if key not in SM80_ORDER: raise ValueError('memory order %d scope %d has no sm_80 form' % (order, scope))
        v = (v & ~(0xf << 77)) | (SM80_ORDER[key] << 77)
    if (v & 0x1ff) in IMAD_CARRY and name.startswith('alu') and fld(v, 87, 91) == 0xf: v = put(v, 87, 91, 0)
    stall = (v >> 105) & 0xf
    if stall < STALL_FLOOR: v = (v & ~(0xf << 105)) | (STALL_FLOOR << 105)
    return v, name


def sections(f):
    sho = struct.unpack_from('<Q', f, 0x28)[0]
    shn, sst = struct.unpack_from('<HH', f, 0x3C)
    hs = [struct.unpack_from('<IIQQQQIIQQ', f, sho + i * 64) for i in range(shn)]
    st = hs[sst]
    names = f[st[4]:st[4] + st[5]]
    return [(names[h[0]:names.index(b'\0', h[0])].decode(), h) for h in hs]


def port(src, sm, table=None):
    f = bytearray(src)
    if f[:4] != b'\x7fELF' or struct.unpack_from('<H', f, 0x12)[0] != 190: raise ValueError('not a CUDA ELF')
    fl = struct.unpack_from('<I', f, 0x30)[0]
    if fl & 0xff != 75: raise ValueError('input is not sm_75')
    struct.pack_into('<I', f, 0x30, (fl & ~0xff00ff) | sm | (sm << 16))
    ops = nak.load(table or NAK_TABLE)
    secs = dict(sections(f))
    stats, unver, bad, notes = {}, [], [], []
    for n, h in secs.items():
        if not n.startswith('.text.'): continue
        o, l = h[4], h[5]
        cb = secs.get('.nv.constant2.' + n[6:])
        w = []
        for i in range(o, o + l, 16):
            lo, hi = struct.unpack_from('<QQ', f, i); w.append(lo | (hi << 64))
        try:
            w, nt = debrx(w, bytes(f[cb[4]:cb[4] + cb[5]]) if cb else b'')
            notes += nt
        except ValueError as e:
            notes.append(str(e))
        for j, v in enumerate(w):
            v, name = port_insn(v)
            stats[name] = stats.get(name, 0) + 1
            if name in UNVERIFIED.values(): unver.append(j)
            hit, err = nak.check(ops, v)
            if err: bad.append('%d: %s' % (j, err))
            struct.pack_into('<QQ', f, o + 16 * j, v & (2**64 - 1), v >> 64)
    return bytes(f), stats, unver, bad, notes


def header(blobs, verified, dst):
    out = ['#ifndef NVX_GQK_CUBIN_SM8X_H', '#define NVX_GQK_CUBIN_SM8X_H']
    for sm, b in blobs:
        out.append('static const unsigned char g_gqk_cubin_sm%d[] __attribute__((aligned(16))) = {' % sm)
        for i in range(0, len(b), 24): out.append('    ' + ','.join('0x%02x' % c for c in b[i:i + 24]) + ',')
        out += ['};', 'static const unsigned int g_gqk_cubin_sm%d_len = %d;' % (sm, len(b))]
    out += ['static const int g_gqk_sm8x_verified = %d;' % verified, '#endif', '']
    open(dst, 'w').write('\n'.join(out))


def selftest(path):
    src, ops, r = open(path, 'rb').read(), nak.load(NAK_TABLE), []
    b, st, unver, bad, notes = port(src, 86)
    r.append(('port of %s: every instruction inside NAK sm70 encoder fields, no BRX' % path, not bad and not unver))
    secs = dict(sections(b)); h = secs['.text.gqk']
    lo, hi = struct.unpack_from('<QQ', b, h[4] + 16 * 24); v = lo | (hi << 64)
    r.append(('a bit outside the NAK ISETP fields (126) is reported', nak.check(ops, v | (1 << 126))[1] is not None and nak.check(ops, v)[1] is None))
    r.append(('BRX (0x949) is reported as not produced by NAK', nak.check(ops, (v & ~0xfff) | 0x949)[1] is not None))
    w = [0] * 8
    w[0] = 0x810 | (7 << 12) | (9 << 16) | (6 << 24) | (0xfffffffd << 32) | (0xff << 64) | (7 << 81)
    w[1] = 0x80c | (7 << 12) | (9 << 24) | (2 << 32) | (7 << 68) | (6 << 76) | (7 << 84) | (7 << 87)
    w[2] = 0x94d | (7 << 87)
    r.append(('negative switch index proven to exit on the range path', neg_exits(w, 0, 0x80000000, M32, 6)))
    w[1] = put(w[1], 32, 64, 0x90000000)
    r.append(('range path that does not exit for every negative index is refused', not neg_exits(w, 0, 0x80000000, M32, 6)))
    for n, ok in r: print('  %-90s %s' % (n, 'ok' if ok else 'FAIL'))
    print('ALL PASS' if all(ok for _, ok in r) else 'FAILED')
    return 0 if all(ok for _, ok in r) else 1


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--selftest': return selftest(sys.argv[2])
    if len(sys.argv) < 3:
        print('usage: nvsass_port.py gqk.cubin outdir [header.h]'); return 2
    src = open(sys.argv[1], 'rb').read()
    blobs, verified = [], 1
    for sm in (86, 89):
        b, st, unver, bad, notes = port(src, sm)
        open('%s/gqk_sm%d.cubin' % (sys.argv[2], sm), 'wb').write(b)
        blobs.append((sm, b))
        if unver or bad: verified = 0
        for x in notes: print('sm_%d: %s' % (sm, x))
        for x in bad: print('sm_%d: NAK mismatch at %s' % (sm, x))
        print('sm_%d: %d instructions, %d checked against the NAK sm70 encoder fields, %d mismatches, BRX left: %s' % (sm, sum(st.values()), sum(st.values()), len(bad), ', '.join('%d' % i for i in unver) or 'none'))
    if len(sys.argv) > 3: header(blobs, verified, sys.argv[3])
    return 0


if __name__ == '__main__':
    sys.exit(main())
