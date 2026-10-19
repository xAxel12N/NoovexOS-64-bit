import re, sys
def parse(text):
    res = {}; cur = None; lines = text.splitlines(); i = 0
    while i < len(lines):
        l = lines[i]
        m = re.match(r'Evaluating \\(\w+)', l)
        if m: cur = m.group(1); res[cur] = ('ERR', ''); i += 1; continue
        if cur and re.match(r'Evaluation of .* failed', l): res[cur] = ('ERR', ''); i += 1; continue
        if cur and 'did not return' in l: res[cur] = ('NONE', ''); i += 1; continue
        m = re.match(r'\s+\[Integer\] = ([0-9A-Fa-f]+)', l)
        if cur and m: res[cur] = ('INT', '%x' % int(m.group(1), 16)); i += 1; continue
        m = re.match(r'\s+\[String\] Length \w+ = "(.*)"$', l)
        if cur and m: res[cur] = ('STR', m.group(1)); i += 1; continue
        m = re.match(r'\s+\[Buffer\] Length (\w+) =', l)
        if cur and m:
            n = int(m.group(1), 16); b = []
            seg = l[l.find('=') + 1:]
            while True:
                mm = re.match(r'\s*[0-9A-Fa-f]{4}: ((?:[0-9A-Fa-f]{2} ?)+)', seg)
                if not mm: break
                b += mm.group(1).split()
                i += 1
                if len(b) >= n or i >= len(lines): break
                seg = lines[i]
            res[cur] = ('BUF', ''.join(b[:n]).lower()); i += 1 if len(b) < n else 0; continue
        m = re.match(r'\s+\[Package\] Contains (\w+) Elements', l)
        if cur and m: res[cur] = ('PKG', str(int(m.group(1), 16))); i += 1; continue
        i += 1
    return res
if __name__ == '__main__':
    for k, v in sorted(parse(open(sys.argv[1]).read()).items()): print(k, v[0], v[1])
