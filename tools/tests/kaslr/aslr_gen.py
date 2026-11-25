import sys
def grab(path, want):
    src = open(path).read().split('\n'); out = []; got = set(); i = 0
    while i < len(src):
        hit = next((w for w in want if src[i].startswith(w)), None)
        if not hit: i += 1; continue
        got.add(hit); d = 0; j = i
        while True:
            d += src[j].count('{') - src[j].count('}'); out.append(src[j]); j += 1
            if d <= 0: break
        i = j
    miss = [w for w in want if w not in got]
    if miss: sys.exit(path + ' missing: ' + ', '.join(miss))
    return out
k = sys.argv[1]
o = [l for l in open(k + '/include/nvx/core/kaslr.h').read().split('\n') if l.startswith('#define KASLR_')]
o += [l for l in open(k + '/core/proc/elf.c').read().split('\n') if l.startswith('#define ELF_')]
o += [l for l in open(k + '/include/nvx/core/proc/elf.h').read().split('\n') if l.startswith('#define ') and ('USTK' in l or 'ELF_' in l)]
o += grab(k + '/core/kaslr.c', ['int g_kaslr_on', 'static u64 g_kaslr_last', 'u64 kaslr_rand', 'u64 kaslr_user_brk', 'u64 kaslr_user_mmap', 'u64 kaslr_gap_hint', 'u64 kaslr_pie_slide', 'u64 kaslr_interp_slide', 'u64 kaslr_stack_room', 'u64 kaslr_user_stack', 'int kaslr_align_shift'])
o += grab(k + '/core/proc/elf.c', ['static u64 elf_load_align', 'u64 elf_interp_base'])
o += [l for l in open(k + '/include/nvx/core/proc/jobs.h').read().split('\n') if l.startswith('#define JS_STK_PAGES')]
o += ['static u64 lx_gap_scan(u64 cr3,u64 need,u64 from,u64 to);']
o += grab(k + '/sys/linux/syscalls/poll_epoll.c', ['static void lx_stk_excl', 'static u64 lx_gap_scan_x'])
open(sys.argv[2], 'w').write('\n'.join(o) + '\n')
