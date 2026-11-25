import re, sys
src = open(sys.argv[1]).read().split('\n')
want = ['struct pf_fd', 'static int g_pf_init', 'static u32 g_pf_used', 'static struct pf_sample g_pf_ring', 'struct pf_sample {',
        'static u32 pf_ctl', 'static u32 pf_ctr', 'static u64 pf_mask', 'static volatile u32 g_pf_defer', 'static int pf_alloc', 'static void pf_global',
        'static void pf_start', 'static void pf_stop', 'static u64 pf_rd', 'void perf_cpu_drain', 'int perf_defer_pending', 'void perf_ev_close']
out = []; i = 0; got = set()
while i < len(src):
    l = src[i]; hit = next((w for w in want if l.startswith(w)), None)
    if not hit: i += 1; continue
    got.add(hit); d = 0; j = i
    while True:
        d += src[j].count('{') - src[j].count('}'); out.append(src[j]); j += 1
        if d <= 0: break
    i = j
miss = [w for w in want if w not in got]
if miss: sys.exit('missing: ' + ', '.join(miss))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
