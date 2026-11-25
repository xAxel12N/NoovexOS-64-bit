#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
# Converts rtw88 conditional PHY tables (<chip>_table.c) into the raw {addr,data} pair arrays rtw_apply_cond walks.
import re, sys

def arrays(src, names):
    s = open(src).read()
    out = []
    for n in names:
        m = re.search(r'static const u32 ' + re.escape(n) + r'\[\] = \{(.*?)\};', s, re.S)
        if not m:
            sys.exit('array %s not found' % n)
        v = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))]
        if len(v) % 2:
            sys.exit('array %s has odd length' % n)
        out.append((n, v))
    return out

def check(name, v):
    depth = 0
    for i in range(0, len(v), 2):
        a = v[i]
        if a >> 31:
            br = (a >> 28) & 3
            if br == 0: depth += 1
            elif br == 3: depth -= 1
        if depth < 0 or depth > 1:
            sys.exit('%s: unbalanced IF/ENDIF at pair %d' % (name, i // 2))
    if depth:
        sys.exit('%s: missing ENDIF' % name)

def main():
    if len(sys.argv) != 5:
        sys.exit('usage: rtw_condgen.py <rtwXXXX_table.c> <prefix> <out.c> <out.h>   e.g. rtw8822b_table.c rtw8822b chips/rtw8822b_tables.c chips/rtw8822b_tables.h')
    src, pre, oc, oh = sys.argv[1:]
    kinds = [('mac', 'cmac'), ('agc', 'cagc'), ('bb', 'cbb'), ('rf_a', 'crf_a'), ('rf_b', 'crf_b')]
    got = arrays(src, [pre + '_' + k for k, _ in kinds])
    hdr = '/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */\n/* Copyright(c) 2018-2019  Realtek Corporation */\n'
    guard = pre.upper() + '_TABLES_H'
    h = [hdr, '#ifndef %s\n#define %s\n\n' % (guard, guard)]
    c = [hdr, '#include "drivers/net/wifi/realtek/rtw88/chips/%s_tables.h"\n' % pre]
    for (k, cn), (n, v) in zip(kinds, got):
        check(n, v)
        an = pre + '_' + cn
        h.append('extern const unsigned int %s[%d];\nextern const int %s_n;\n' % (an, len(v), an))
        c.append('\nconst unsigned int %s[%d]={\n' % (an, len(v)))
        for i in range(0, len(v), 8):
            c.append(' ' + ','.join('0x%X' % x for x in v[i:i + 8]) + ',\n')
        c.append('};\nconst int %s_n=%d;\n' % (an, len(v) // 2))
        print('  %-16s %6d pairs' % (an, len(v) // 2))
    h.append('\n#endif\n')
    open(oc, 'w').write(''.join(c))
    open(oh, 'w').write(''.join(h))

main()
