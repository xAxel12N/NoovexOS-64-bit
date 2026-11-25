import sys, re
s = open(sys.argv[1]).read()
def fn(name):
    m = re.search(r'(?m)^static [\w\s\*]*?\b' + name + r'\(', s); k = s.find('{', m.start()); d = 0
    for x in range(k, len(s)):
        d += s[x] == '{'; d -= s[x] == '}'
        if d == 0: return s[m.start():x + 1]
head = '\n'.join(l for l in s.splitlines() if l.startswith(('#define HPP', 'typedef struct', 'static hpp_rcv_t hpp_r')))
names = ['hpp_vendor_iface', 'hpp_is_bolt', 'hpp_receiver_pid', 'hpp_new', 'hpp_msg', 'hpp_msgl', 'hpp_nidx', 'hpp_pair_start', 'hpp_rdone', 'hpp_dev_req', 'hpp_dev_next', 'hpp_on', 'hpp_prx', 'hpp_rx', 'hpp_step', 'hpp_scale', 'hpp_kind_name', 'hpp_fmt', 'hpp_cat', 'hpp_perr', 'hpp_pstate']
code = '#include <stdio.h>\n#include <string.h>\n#include <stdint.h>\ntypedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;\n' + head + '\n' + '\n'.join(fn(n) for n in names) + '\n'
open(sys.argv[2], 'w').write(code + open(sys.argv[3]).read())
