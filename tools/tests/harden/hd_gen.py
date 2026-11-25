import re, sys
src = open(sys.argv[1] + '/arch/x86_64/cpu/harden.c').read()
out = ['typedef unsigned long long u64;', 'typedef unsigned int u32;', 'enum { HD_NOCPU=-1, HD_OFF=0, HD_ON=1 };']
for name in ('hd_cr4_bits', 'hd_guard_mix'):
    m = re.search(r'^(int|u64) ' + name + r'\(.*?\n(?=\S)', src, re.S | re.M)
    if not m: sys.exit('missing ' + name)
    out.append(m.group(0))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
