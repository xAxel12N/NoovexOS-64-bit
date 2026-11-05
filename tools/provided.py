import sys
seen = {}
for line in sys.stdin:
    line = line.rstrip('\n')
    if '|' not in line: continue
    head, provides = line.split('|', 1)
    parts = head.split(' ', 1)
    if len(parts) != 2: continue
    name, ver = parts
    seen.setdefault(name, ver)
    for p in provides.split(','):
        p = p.strip()
        if not p: continue
        if '(' in p:
            pn, pv = p.split('(', 1)
            pn = pn.strip(); pv = pv.rstrip(')').strip().lstrip('=').strip()
        else:
            pn, pv = p, ver
        pn = pn.split(':')[0]
        seen.setdefault(pn, pv)
for k in sorted(seen): print(k, seen[k])
