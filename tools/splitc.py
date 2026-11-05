import os, re, sys

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INC = re.compile(r'^#include "([^"]+_parts/[^"]+\.c)"\s*$')


def scan(lines):
    info = []
    depth = 0
    pp = 0
    in_c = False
    for ln in lines:
        start = (depth, pp, in_c)
        s = ln.lstrip()
        if not in_c and s.startswith('#'):
            w = s[1:].lstrip().split(None, 1)[0] if s[1:].strip() else ''
            if w in ('if', 'ifdef', 'ifndef'):
                pp += 1
            elif w == 'endif':
                pp -= 1
            pre = True
        else:
            pre = False
        i = 0
        n = len(ln)
        while i < n:
            c = ln[i]
            if in_c:
                if c == '*' and i + 1 < n and ln[i + 1] == '/':
                    in_c = False
                    i += 2
                    continue
                i += 1
                continue
            if c == '/' and i + 1 < n and ln[i + 1] == '/':
                break
            if c == '/' and i + 1 < n and ln[i + 1] == '*':
                in_c = True
                i += 2
                continue
            if c == '"' or c == "'":
                q = c
                i += 1
                while i < n and ln[i] != q:
                    if ln[i] == '\\':
                        i += 1
                    i += 1
                i += 1
                continue
            if c == '{' and not pre:
                depth += 1
            elif c == '}' and not pre:
                depth -= 1
            i += 1
        info.append((start, depth, ln.rstrip().endswith('\\')))
    return info


def last_code_char(ln):
    t = re.sub(r'//.*$', '', ln).rstrip()
    return t[-1:] if t else ''


def candidates(lines, info):
    out = []
    for k in range(1, len(lines)):
        (d0, pp0, c0), _, _ = info[k]
        prev = lines[k - 1]
        if c0 or pp0 != 0 or info[k - 1][2]:
            continue
        lc = last_code_char(prev)
        s = lines[k].lstrip()
        if d0 == 0 and (lc in ('}', ';', '') or prev.lstrip().startswith('#')):
            if lines[k].strip() and not s.startswith('else'):
                out.append((k, 0))
        elif d0 == 1 and lc in ('}', ';') and re.match(r'\s+else if\s*\(', lines[k]):
            out.append((k, 1))
    return out


def name_for(lines, a, b):
    for ln in lines[a:b]:
        m = re.match(r'^(?:static\s+|inline\s+|const\s+|unsigned\s+|struct\s+\w+\s*\*?\s*)*[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(', ln)
        code = re.sub(r'//.*$', '', ln).rstrip()
        if m and not ln.startswith((' ', '\t', '#', 'extern')) and not code.endswith(';') and m.group(1) not in ('if', 'for', 'while', 'switch', 'return', 'sizeof'):
            return m.group(1)
        m = re.match(r'\s+else if\s*\(\s*seq\(\s*\w+\s*,\s*"([a-z0-9_-]+)"', ln)
        if m:
            return 'cmd_' + m.group(1).replace('-', '_')
    return 'part'


def split(path, target):
    rel = os.path.relpath(path, TOP)
    if '_parts' + os.sep in rel:
        print('%s: is a part, not split' % rel)
        return 0
    lines = open(path).read().split('\n')
    if any(INC.match(l) for l in lines):
        print('%s: already split' % rel)
        return 0
    info = scan(lines)
    if info[-1][1] != 0:
        print('%s: unbalanced braces (%d), not split' % (rel, info[-1][1]))
        return 0
    cands = candidates(lines, info)
    if not cands:
        return 0
    first_fn = next((k for k, d in cands if d == 0 and re.match(r'^[A-Za-z_].*\(', lines[k])), cands[0][0])
    cuts = [first_fn]
    last = first_fn
    for k, d in cands:
        if k <= last:
            continue
        if k - last >= target:
            cuts.append(k)
            last = k
    if len(lines) - cuts[-1] < target // 4 and len(cuts) > 1:
        cuts.pop()
    cuts.append(len(lines))
    base = os.path.splitext(os.path.basename(path))[0]
    pdir = os.path.join(os.path.dirname(path), base + '_parts')
    os.makedirs(pdir, exist_ok=True)
    head = lines[:cuts[0]]
    incs = []
    for i in range(len(cuts) - 1):
        a, b = cuts[i], cuts[i + 1]
        chunk = lines[a:b]
        if i == len(cuts) - 2 and chunk and chunk[-1] == '':
            chunk = chunk[:-1]
        nm = '%02d_%s.c' % (i + 1, name_for(lines, a, b))
        open(os.path.join(pdir, nm), 'w').write('\n'.join(chunk) + '\n')
        incs.append('#include "%s"' % os.path.relpath(os.path.join(pdir, nm), TOP))
    if head and head[-1] != '':
        head.append('')
    open(path, 'w').write('\n'.join(head + incs) + '\n')
    print('%s: %d lines -> %d parts in %s' % (rel, len(lines), len(incs), os.path.relpath(pdir, TOP)))
    return len(incs)


def expand(path, seen=None):
    out = []
    for l in open(path).read().split('\n'):
        m = INC.match(l)
        if m:
            p = os.path.join(TOP, m.group(1))
            t = expand(p)
            out.append(t[:-1] if t.endswith('\n') else t)
        else:
            out.append(l)
    return '\n'.join(out)


if __name__ == '__main__':
    if sys.argv[1] == 'expand':
        sys.stdout.write(expand(sys.argv[2]))
    else:
        t = int(sys.argv[1])
        for p in sys.argv[2:]:
            split(os.path.join(TOP, p), t)
