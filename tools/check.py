#!/usr/bin/env python3
import argparse, os, random, re, shutil, struct, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
KDIR = SRC
BDIR = os.path.join(SRC, 'boot')
TESTS = os.path.join(HERE, 'tests')
sys.path.insert(0, HERE); sys.path.insert(0, TESTS)
import emulate

RESULTS = []
SAN = []
LIBC_SAN = []
def sh(cmd, cwd=None, timeout=900):
    p = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True, text=True, errors='replace', timeout=timeout)
    return p.returncode, p.stdout + p.stderr
def record(group, name, ok, detail=''):
    RESULTS.append((group, name, bool(ok), detail))
    print(f"  {name:66s} {'ok' if ok else 'FAIL'}" + (f"   ({detail[:160]})" if detail and not ok else ''))
    return ok
def head(t): print(f"\n######## {t} ########")

def khdr(path, protos=False):
    rel = os.path.relpath(path, KDIR)
    h = os.path.join(KDIR, 'include', 'nvx', rel[:-2] + '.h')
    if not os.path.exists(h): return ''
    out = []
    for l in open(h).read().split('\n'):
        if re.match(r'#(ifndef|endif)\b', l) or re.match(r'#define NVX_\w+_H$', l) or l.startswith('#include "include/nvx/'): continue
        if not protos and l.startswith('extern '): continue
        if not protos and re.match(r'^[A-Za-z_][\w\s\*]*\b\w+\s*\([^;{]*\)[^;{]*;\s*$', l) and not l.startswith(('typedef', '#')): continue
        out.append(l)
    return '\n'.join(out) + '\n'

_KDECL = None
def kdecls():
    global _KDECL
    if _KDECL is not None: return _KDECL
    fn, vn = {}, {}
    for root, _, files in os.walk(os.path.join(KDIR, 'include/nvx')):
        for f in files:
            if not f.endswith('.h'): continue
            for l in open(os.path.join(root, f)).read().split('\n'):
                if l.startswith('extern '):
                    m = re.match(r'extern\s+[^;(]*?\b(\w+)\s*\(', l)
                    if m and '(*' not in l: fn[m.group(1)] = 'static ' + l[7:].strip(); continue
                    body = re.sub(r'\[[^\]]*\]|\([^)]*\)', '', l[7:].rstrip(' ;'))
                    for part in body.split(','):
                        mm = re.findall(r'(\w+)\s*$', part.strip())
                        if mm: vn[mm[0]] = l
                    continue
                m = re.match(r'^(?!typedef|#|struct|union|enum)[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{]*\)[^;{]*;\s*$', l)
                if m: fn[m.group(1)] = 'static ' + l.strip()
    _KDECL = (fn, vn)
    return _KDECL

def kfwd(text, own=''):
    fn, vn = kdecls()
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'"(\\.|[^"\\])*"', '', text)))
    out = []
    for n in sorted(ids):
        if n in fn: out.append(fn[n])
        elif n in vn and re.search(r'(?:^|[;\n])\s*static\s[^;{}()]*\b' + n + r'\b', own): out.append('static ' + re.sub(r'\s*=[^,;]*', '', vn[n][7:].strip()))
    return '\n'.join(out) + '\n'

def kmissing(code, tmpl=''):
    fn, vn = kdecls()
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'"(\\.|[^"\\])*"', '', code)))
    out = []
    allt = code + tmpl
    for n in sorted(ids):
        if n in vn and not re.search(r'(?:^|[;\n{])\s*(?:static\s+)?(?:volatile\s+|const\s+|unsigned\s+)*(?:\w+[\s\*]+)+(?:\w+\s*(?:\[[^\]]*\])?\s*(?:=[^,;]*)?,\s*)*' + n + r'\b', allt):
            out.append('static ' + re.sub(r'\s*=[^,;]*', '', vn[n][7:].strip()))
    return '\n'.join(out) + '\n'

def kread(path, hdr=True):
    t = open(path, errors='ignore').read()
    rel = os.path.relpath(path, KDIR)
    if rel.startswith('..') or not rel.endswith('.c'): return t
    t = re.sub(r'(?m)^#include "include/nvx/[^"]+"\n', '', t)
    h = os.path.join(KDIR, 'include', 'nvx', rel[:-2] + '.h')
    if os.path.exists(h):
        ht = open(h).read()
        fn = set(re.findall(r'^\w[\w\s\*]*?\b(\w+)\s*\([^;{]*\)[^;{]*;\s*$', ht, re.M))
        vn = set()
        for l in ht.split('\n'):
            if l.startswith('extern '):
                for part in re.sub(r'\[[^\]]*\]|\([^)]*\)', '', l[7:].rstrip(' ;')).split(','):
                    m = re.findall(r'(\w+)\s*$', part.strip())
                    if m: vn.add(m[0])
        def fix(m):
            line = m.group(0)
            if line.startswith(('struct ', 'union ', 'enum ')) and '{' not in line.split(';')[0]: pass
            elif line.startswith(('static', 'typedef', 'struct', 'union', 'enum', '#', 'extern', ' ', '\t', '{', '}')): return line
            nm = re.match(r'[\w\s\*]*?\b(\w+)\s*\(', line)
            if nm and nm.group(1) in fn: return 'static ' + line
            st0 = re.split(r'[;=]', line, 1)[0]
            if '(' not in re.sub(r'\[[^\]]*\]', '', st0).replace('(*', '') and any(w in vn for w in re.findall(r'\b\w+\b', st0)):
                parts = []; d = 0; cur = ''
                for ch in line:
                    cur += ch
                    if ch in '({[': d += 1
                    elif ch in ')}]': d -= 1
                    elif ch == ';' and d == 0: parts.append(cur); cur = ''
                parts.append(cur)
                outp = []
                for st in parts:
                    lead = st[:len(st) - len(st.lstrip())]; body = st.lstrip()
                    decl = re.sub(r'=[^,;]*', '', body)
                    if body and not body.startswith(('static', 'extern')) and any(w in vn for w in re.findall(r'\b\w+\b', decl)): body = 'static ' + body
                    outp.append(lead + body)
                return ''.join(outp)
            return line
        t = re.sub(r'(?m)^[^\n]+', fix, t)
        if hdr: t = khdr(path) + t
    return t

def kview(path):
    t = kread(path)
    return kfwd(t, t) + t

def defn(src, name):
    m = re.search(r'(?m)^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b' + name + r'\s*\([^;{]*\)\s*\{', src)
    if not m: raise KeyError(name)
    k = src.find('{', m.start()); d = 0
    for x in range(k, len(src)):
        if src[x] == '{': d += 1
        elif src[x] == '}':
            d -= 1
            if d == 0: return src[m.start():x + 1]

def template(path, keys):
    t = open(path).read(); out = {}
    for i, k in enumerate(keys):
        a = t.find(f'@@{k}@@\n') + len(f'@@{k}@@\n')
        b = t.find(f'\n@@{keys[i + 1]}@@\n') if i + 1 < len(keys) else len(t)
        out[k] = t[a:b]
    return out

def g_build(work, iso):
    head('build')
    rc, out = sh(['sh', 'build.sh'], cwd=KDIR, timeout=1800)
    open(os.path.join(work, 'build.log'), 'w').write(out)
    record('build', 'kernel, libc, user programs and UEFI loader build', rc == 0 and 'signature stamped' in out, out[-300:])
    impl = [l for l in out.splitlines() if 'implicit declaration' in l]
    record('build', 'no implicit function declarations', not impl, impl[0] if impl else '')
    shutil.copy(os.path.join(KDIR, 'build/kernel64.bin'), os.path.join(BDIR, 'stage/kernel64.bin'))
    rc, out = sh(['make', 'build/boot.bin', 'build/stage2.bin'], cwd=BDIR, timeout=600)
    record('build', 'BIOS boot sector and stage2 rebuild from source', rc == 0, out[-300:])
    rc, out = sh(['python3', 'mkhybrid.py', 'build/boot.bin', 'build/stage2.bin', iso, 'NOOVEXOS',
                  'boot/kernel64.bin=stage/kernel64.bin', 'boot.cfg=stage/boot.cfg', 'README.txt=stage/README.txt'], cwd=BDIR)
    record('build', 'hybrid ISO builds (FAT32 size and FAT16 EFI image asserts hold)', rc == 0 and os.path.exists(iso), out[-300:])

def gdt64_check(elf, binf):
    if not (os.path.exists(elf) and os.path.exists(binf)): return False, 'kernel64.elf/bin missing'
    syms = {}
    for l in sh(['nm', elf])[1].splitlines():
        p = l.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    if 'uefi_entry' not in syms: return False, 'no uefi_entry'
    k = open(binf, 'rb').read(); start = syms['uefi_entry']
    rc, out = sh(['objdump', '-d', elf, f'--start-address={start:#x}', f'--stop-address={start + 64:#x}'])
    hits = re.findall(r'lgdt\s+\S+\s+#\s*([0-9a-f]+)', out)
    if not hits: return False, 'no RIP-relative lgdt in uefi_entry'
    for h in hits:
        a = int(h, 16) - 0x100000; lim = struct.unpack_from('<H', k, a)[0]; base = struct.unpack_from('<Q', k, a + 2)[0]
        gname = next((n for n, v in syms.items() if v == base and n.startswith('gdt')), None)
        if not gname: return False, f'lgdt base {base:#x} is not a GDT symbol (6-byte pointer used in 64-bit mode?)'
        if lim < 0x17: return False, f'GDT limit {lim:#x} too small'
        code = struct.unpack_from('<Q', k, base - 0x100000 + 8)[0]
        if not (code >> 53) & 1 or not (code >> 47) & 1: return False, f'selector 0x08 is not a present 64-bit code segment ({code:#x})'
    return True, ''

def kwalk():
    for root, ds, files in os.walk(KDIR):
        rel = os.path.relpath(root, KDIR).split(os.sep)
        if rel[0] in ('boot', 'tools', 'docs', '.git'): ds[:] = []; continue
        yield root, ds, files

def unbounded_hw_loops():
    hits = []; hw = re.compile(r'\b(inb|inw|inl|kapi_inb|kapi_inw|kapi_inl|cmos|XMR)\s*\(')
    for root, _, files in kwalk():
        if '/ntfs' in root or os.sep + 'usr' in root: continue
        for f in files:
            if not f.endswith('.c'): continue
            p = os.path.join(root, f); t = open(p, errors='ignore').read()
            for m in re.finditer(r'\bwhile\s*\(', t):
                j = m.end(); d = 1
                while j < len(t) and d:
                    if t[j] == '(': d += 1
                    elif t[j] == ')': d -= 1
                    j += 1
                cond = t[m.end():j - 1]
                if cond.strip() == '0' or '&&' in cond or '||' in cond or not hw.search(cond): continue
                hits.append(f"{os.path.relpath(p, KDIR)}:{t.count(chr(10), 0, m.start()) + 1}")
    return hits

STRICT = ['-Wduplicated-cond', '-Wduplicated-branches', '-Wlogical-op', '-Wnull-dereference', '-Wshift-negative-value', '-Wint-conversion',
          '-Wempty-body', '-Wmaybe-uninitialized', '-Warray-bounds', '-Wno-missing-field-initializers', '-Wno-unused-function', '-Wno-unused-variable',
          '-Wno-unused-but-set-variable', '-Wno-unused-parameter', '-Wno-misleading-indentation', '-Wno-sign-compare']
def kernel_modules():
    m = re.search(r'^MODS="(.*?)"', kread(os.path.join(KDIR, 'build.sh')), re.M)
    return m.group(1).split() if m else []

def kernel_cflags():
    m = re.search(r'^CFLAGS="(.*?)"', kread(os.path.join(KDIR, 'build.sh')), re.S | re.M)
    return m.group(1).replace('\\\n', ' ').split() if m else []

def g_analyze(work):
    head('static analysis (clang analyzer + bug-prone patterns)')
    if not shutil.which('clang-tidy'): record('analyze', 'clang-tidy installed (skipped)', True); return
    checks = '-*,clang-analyzer-core.*,clang-analyzer-unix.Malloc,bugprone-sizeof-expression,bugprone-suspicious-memset-usage,bugprone-misplaced-widening-cast,bugprone-suspicious-string-compare,bugprone-infinite-loop,bugprone-too-small-loop-variable'
    rc, out = sh(['clang-tidy', 'kernel64.c', '-header-filter=.*', '--checks=' + checks, '--', '-m64', '-ffreestanding', '-DNOOVEX64', '-I.', '-w'], cwd=KDIR, timeout=1800)
    for m in kernel_modules():
        r2, o2 = sh(['clang-tidy', m, '-header-filter=.*', '--checks=' + checks, '--', '-m64', '-ffreestanding', '-DNOOVEX64', '-I.', '-w'], cwd=KDIR, timeout=600); out += o2
    w = [l for l in out.splitlines() if 'warning:' in l or ' error:' in l]
    for l in w[:10]: print('    ' + l[:200])
    record('analyze', f'kernel: no analyzer findings ({len(w)} found)', not w, w[0][:200] if w else '')

STAGE2_TRANSIENT = {('stage2.c', 0x1000000)}

def fixed_regions_in_bss():
    syms = {}
    for l in sh(['nm', os.path.join(KDIR, 'build/kernel64.elf')])[1].splitlines():
        p = l.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    lo, hi = syms.get('__bss_start', 0xC00000) + 0x6000, syms.get('__bss_end', 0)
    pat = re.compile(r'(\(\s*(?:u8|u16|u32|u64|int|char|unsigned char|void)\s*\*\s*\)\s*\(?\s*0x0*([0-9A-Fa-f]{6,8})|#define\s+\w+\s+\(?\s*0x0*([0-9A-Fa-f]{6,8})(?:ULL|UL|u|U)?\b)')
    hits = []
    for root, _, files in kwalk():
        if any(x in root for x in ('/ntfs', '/musl', '/sqlite', '/zlib', '/bzip2', '/xz', '/zstd', '/ffmpeg', '/quickjs', '/build')) or os.sep + 'usr' in root or '/wifi' in root or root.endswith(os.sep + 'doom'): continue
        for f in files:
            if not f.endswith(('.c', '.h')): continue
            path = os.path.join(root, f); t = open(path, errors='ignore').read()
            for m in pat.finditer(t):
                v = int(m.group(2) or m.group(3), 16)
                if lo < v < hi and v % 0x1000 == 0: hits.append(f"{os.path.relpath(path, KDIR)}:{t.count(chr(10), 0, m.start()) + 1} ({v:#x})")
    asm = re.compile(r'\$0x0*([0-9A-Fa-f]{6,8})\b|\(uint8_t\s*\*\)\s*0x0*([0-9A-Fa-f]{6,8})|\b0x0*([0-9A-Fa-f]{6,8})u,')
    for path in [os.path.join(KDIR, 'arch/x86_64/boot/boot64.S'), os.path.join(BDIR, 'src/stage2.c')]:
        if not os.path.exists(path): continue
        t = open(path, errors='ignore').read()
        for m in asm.finditer(t):
            v = int(m.group(1) or m.group(2) or m.group(3), 16)
            if lo < v < hi and v % 0x1000 == 0 and (os.path.basename(path), v) not in STAGE2_TRANSIENT: hits.append(f"{os.path.relpath(path, os.path.dirname(KDIR))}:{t.count(chr(10), 0, m.start()) + 1} ({v:#x})")
    return hits, hi

def g_static():
    head('static checks')
    kb = os.path.join(KDIR, 'build/kernel64.bin')
    if os.path.exists(kb):
        ok, d = emulate.signature_ok(open(kb, 'rb').read()); record('static', f'kernel signature ({d})', ok, d)
    so = os.path.join(BDIR, 'uefi/uefiboot.so')
    if os.path.exists(so):
        rc, out = sh(['readelf', '-r', so]); record('static', 'UEFI loader has no relocations', 'no relocations' in out, out[:200])
        rc, out = sh(['objdump', '-d', so]); body = out[out.find('<efi_main>:'):][:4000]
        record('static', 'efi_main takes SystemTable from RDX (Microsoft x64 ABI)', re.search(r'mov\s+%rdx,0x[0-9a-f]+\(%rip\)\s+#\s*[0-9a-f]+ <ST>', body) is not None)
    efi = os.path.join(BDIR, 'uefi/BOOTX64.EFI')
    if os.path.exists(efi):
        import pecheck; errs, _ = pecheck.check(efi); record('static', 'BOOTX64.EFI passes PE structure checks', not errs, '; '.join(errs[:2]))
    ok_gdt, d_gdt = gdt64_check(os.path.join(KDIR, 'build/kernel64.elf'), kb)
    record('static', 'uefi_entry lgdt reads a full 64-bit GDT pointer with the right base', ok_gdt, d_gdt)
    bad = []
    for root, _, files in kwalk():
        if '/ntfs' in root or '/usr' in root: continue
        for f in files:
            if not f.endswith('.c'): continue
            p = os.path.join(root, f)
            if p.endswith(os.path.join('core', 'power.c')): continue
            t = open(p, errors='ignore').read()
            if re.search(r'outb\(\s*0x64\s*,\s*0xFE\s*\)', t): bad.append(os.path.relpath(p, KDIR))
    record('static', 'no raw keyboard-controller resets outside the power module', not bad, ', '.join(bad))
    asm = []
    for root, _, files in kwalk():
        rel = os.path.relpath(root, KDIR).split(os.sep)
        if rel[0] in ('arch', 'third_party', 'usr', 'build', 'tools') or rel[:3] == ['include', 'nvx', 'arch']: continue
        for f in files:
            if not f.endswith(('.c', '.h')): continue
            p = os.path.join(root, f); t = open(p, errors='ignore').read()
            for m in re.finditer(r'\b__asm__\b|\basm\s*\(', t): asm.append(f"{os.path.relpath(p, KDIR)}:{t.count(chr(10), 0, m.start()) + 1}")
    record('static', 'no inline assembly outside arch/ (portable code uses arch/x86_64/cpu/cpu.h)', not asm, ', '.join(asm[:6]))
    rc, out = sh(['gcc'] + kernel_cflags() + STRICT + ['-c', 'kernel64.c', '-o', os.devnull], cwd=KDIR)
    for m in kernel_modules():
        r2, o2 = sh(['gcc'] + kernel_cflags() + STRICT + ['-c', m, '-o', os.devnull], cwd=KDIR); rc |= r2; out += o2
    ws = [l for l in out.splitlines() if 'warning:' in l or 'error:' in l]
    record('static', f'kernel compiles with zero strict warnings ({len(ws)})', rc == 0 and not ws, ws[0][:200] if ws else '')
    fr, be = fixed_regions_in_bss(); record('static', f'no hard-coded memory regions inside kernel .bss (ends {be:#x})', not fr, ', '.join(fr[:6]))
    ub = unbounded_hw_loops(); record('static', 'no unbounded hardware polling loops (hang on missing hardware)', not ub, ', '.join(ub[:6]))
    x = kread(os.path.join(KDIR, 'drivers/usb/xhci.c'))
    mx = int(re.search(r'#define XHID_MAX (\d+)', x).group(1)); hm = re.search(r'XHID_BASE\(r\)\s+\(DMW\(0x([0-9A-Fa-f]+)u\) \+ \(u32\)\(r\)\*0x([0-9A-Fa-f]+)u\)', x)
    base, stride = int(hm.group(1), 16), int(hm.group(2), 16)
    msd = int(re.search(r'#define MSD_INPUTCTX DMW\(0x([0-9A-Fa-f]+)u\)', x).group(1), 16)
    record('static', 'USB HID regions end below the USB-stick buffers', base + mx * stride <= msd, f'{base + mx * stride:#x} > {msd:#x}')
    win = int(re.search(r'#define DMW_SIZE\s+0x([0-9A-Fa-f]+)u', kread(os.path.join(KDIR, 'include/nvx/mem/dmawin.h'))).group(1), 16)
    regs = [('xHCI controller 0', 0x10000, 0x26000), ('USB HID', base, mx * stride), ('USB stick', msd, 0x16000)]
    g = kread(os.path.join(KDIR, 'drivers/storage/gdisk.c')) + kread(os.path.join(KDIR, 'include/nvx/drivers/storage/gdisk.h'))
    a0 = int(re.search(r'#define AHCI_CLB DMW\(0x([0-9A-Fa-f]+)u\)', g).group(1), 16); ab = int(re.search(r'#define AHCI_BUF DMW\(0x([0-9A-Fa-f]+)u\)', g).group(1), 16)
    regs.append(('AHCI', a0, ab - a0 + 128 * 512))
    a2 = int(re.search(r'#define ahci_clb\s+\(\(u8\*\)\(u64\)DMW\(0x([0-9A-Fa-f]+)u\)\)', kread(os.path.join(KDIR, 'drivers/storage/ahci.c'))).group(1), 16)
    regs.append(('AHCI identify', a2, 0x800))
    vb = int(re.search(r'#define g_vbox_req \(\(u8\*\)\(u64\)DMW\(0x([0-9A-Fa-f]+)u\)\)', kread(os.path.join(KDIR, 'drivers/input/vm/vmmouse.c'))).group(1), 16)
    regs.append(('VirtualBox mouse request', vb, 64))
    nb = int(re.search(r'#define nic_initb \(\(u8\*\)\(u64\)DMW\(0x([0-9A-Fa-f]+)u\)\)', kread(os.path.join(KDIR, 'include/nvx/drivers/net/ethernet/nic.h'))).group(1), 16)
    regs.append(('PCnet rings and buffers', nb, 0x5800))
    n = kread(os.path.join(KDIR, 'drivers/storage/nvme.c'))
    n0 = int(re.search(r'#define NVME_ASQ\s+DMW\(0x([0-9A-Fa-f]+)u\)', n).group(1), 16); nb = int(re.search(r'#define NVME_BUF\s+DMW\(0x([0-9A-Fa-f]+)u\)', n).group(1), 16)
    regs.append(('NVMe', n0, nb - n0 + 0x10000))
    regs.sort(key=lambda r: r[1]); bad = [f'{p[0]} overlaps {q[0]}' for p, q in zip(regs, regs[1:]) if p[1] + p[2] > q[1]] + [f'{r[0]} ends past the window' for r in regs if r[1] + r[2] > win]
    record('static', 'device buffers fit the DMA window without overlapping', not bad, ', '.join(bad))

def g_boot(iso):
    head('boot emulation (BIOS + UEFI + kernel entry)')
    rc, out = sh(['python3', os.path.join(HERE, 'emulate.py'), iso], timeout=900)
    for l in out.splitlines():
        if 'FAIL' in l or 'checks passed' in l: print('    ' + l.strip())
    m = re.search(r'(\d+) of (\d+) checks passed', out)
    record('boot', f'emulate.py: {m.group(0) if m else "did not finish"}', rc == 0, out[-300:])

def g_usb(work):
    head('USB keyboard/mouse driver (generated from xhci.c)')
    s = kread(os.path.join(KDIR, 'drivers/usb/xhci.c'))
    tp = template(os.path.join(TESTS, 'usb_template.txt'), ['PRE', 'PRE2', 'MAIN'])
    decl = s[s.find('/* per-entry HID state'):s.find('static void xhid_reset(void)')]
    if 'static u8 xhid_bs[64];' in s and 'xhid_bs' not in decl: decl += '\nstatic u8 xhid_bs[64];\n'
    m_inf = re.search(r'#define XHID_INFLIGHT[^\n]*', s)
    if m_inf and 'XHID_INFLIGHT' not in decl: decl = m_inf.group(0) + '\n' + decl
    decl = decl.replace('static int xhci_csz=0; static u32 xhci_dboff=0, xhci_rtoff=0;\n', '').replace('static u32 cmd_enq=0, cmd_cyc=1, ev_deq=0, ev_cyc=1;\n', '')
    reps = s[s.find('static int kcaps;'):s.find('static int usb_key_code')] if 'static int kcaps;' in s else 'static int kcaps;\n' + s[s.find('static int usbk_rep_code'):s.find('static int usb_key_code')]
    names = ['xhid_reset', 'hid_pick', 'hid_parse', 'hid_bits', 'xh_interval', 'ctx_sz', 'ctx_zero', 'xhid_free_region', 'xh_disable_slot', 'xhid_arm', 'xh_set_fail', 'xh_root_reset', 'xh_slot_ctx', 'xh_fix_mps0', 'xhci_hid_enum', 'xhci_enum_loc',
             'xhub_db', 'xhub_ctrl', 'xhub_status', 'xh_delay_ms', 'xh_under', 'xhub_remove_below', 'xhub_port_reset', 'xhub_child_loc', 'xhub_enum_port', 'xhub_attach', 'xhub_poll_ports',
             'hid2ascii', 'hid2scan', 'usb_key_code', 'kbd_report', 'usb_kbd_repeat', 'hid_report_decode', 'hid_xfer', 'xhci_poll_one', 'xhci_mouse_poll', 'xhid_remove_port', 'xh_event_t', 'xh_ctrl']
    try: funcs = {n: defn(s, n) for n in names}
    except KeyError as e: record('usb', f'function {e} found in xhci.c', False); return
    funcs['xhci_enum_loc'] = funcs['xhci_enum_loc'].replace('#define XFAIL', '#undef XFAIL\n#define XFAIL', 1)
    funcs['xh_ctrl'] = funcs['xh_ctrl'].replace('static int xh_ctrl(', 'static int real_xh_ctrl(', 1)
    order = ['xh_event_t'] + [n for n in names if n not in ('xh_event_t', 'xh_ctrl')] + ['xh_ctrl']
    body = 'static int xhci_enum_loc(const usbloc_t* L);\n' + '\n'.join(funcs[n] if n != 'xh_fix_mps0' else tp['PRE2'] + '\n' + funcs[n] for n in order)
    ty = s[s.find('typedef struct { u8 iface'):s.find('static int hid_parse')]
    extra = ty + 'static char xhci_failstep[48]; static int xhci_failcc;\n'
    ctx = s[s.find('typedef struct { int present'):s.find('static void xhc_save(void)')] + '\n'.join(defn(s, n) for n in ('xhc_save', 'xhc_load', 'xhc_select')) + '\n'
    code = tp['PRE'].replace('@@XHC_CTX@@', '') + khdr(os.path.join(KDIR, 'drivers/usb/xhci.c')) + decl + reps.replace('static int kcaps;', 'static int kcaps;\n' + extra) + ctx + body + '\n' + tp['MAIN']
    c = os.path.join(work, 'usb_t.c'); open(c, 'w').write(code); exe = os.path.join(work, 'usb_t')
    rc, out = sh(['gcc', '-O0', '-w', '-fno-pie', '-no-pie'] + SAN + ['-o', exe, c])
    if not record('usb', 'test builds against the current driver', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('usb', f'receivers, routing, repeat, keys, hot-unplug, hubs ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_acpi(work):
    head('ACPI power off / restart (generated from power.c)')
    src = kread(os.path.join(KDIR, 'core/power.c'))
    tp = template(os.path.join(TESTS, 'acpi_template.txt'), ['HDR', 'TEST'])
    src = src.replace('#include "third_party/ntfs/nvxntfs.h"\n', '')
    a = src.find('static u64 acpi_find_rsdp(void){'); b = src.find('\n}\n', a) + 3
    src = src[:a] + 'static u64 g_test_rsdp; static u64 acpi_find_rsdp(void){ return acpi_rsdp_ok(g_test_rsdp)?g_test_rsdp:0; }\n' + src[b:]
    c = os.path.join(work, 'acpi_t.c'); open(c, 'w').write(tp['HDR'] + kmissing(src, tp['HDR'] + tp['TEST']) + src + tp['TEST']); exe = os.path.join(work, 'acpi_t')
    rc, out = sh(['gcc', '-O0', '-w'] + SAN + ['-o', exe, c])
    if not record('acpi', 'test builds against the current power module', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    record('acpi', 'S5 parsing, ACPI enable, reset register, hardware-reduced, bad tables', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_xhci_init(work):
    head('xHCI controller set-up (generated from xhci.c and pmm_vmm.c, simulated registers)')
    x = kread(os.path.join(KDIR, 'drivers/usb/xhci.c')); m = kread(os.path.join(KDIR, 'mem/pmm_vmm.c'))
    a = x.find('#define XHC_MAX'); b = x.find('/* mouse state')
    ma = m.find('#define PF_BASE'); mb = m.find('static u8  pf_bitmap')
    if not record('usb', 'controller set-up code located', a >= 0 and b > a and ma >= 0 and mb > ma): return
    mm = m[ma:mb].replace('static void mm_load(void){', 'static void mm_load_unused(void){')
    tp = template(os.path.join(TESTS, 'xhci_init_template.txt'), ['HDR', 'TEST'])
    c = os.path.join(work, 'xhci_init_t.c'); open(c, 'w').write(tp['HDR'] + '\n' + mm + x[a:b] + tp['TEST']); exe = os.path.join(work, 'xhci_init_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=undefined', '-o', exe, c])
    if not record('usb', 'set-up test builds against the current driver', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    record('usb', 'scratchpad buffers: every page the controller asks for is valid memory', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_touchpad(work):
    head('touchpad stack (ACPI scan, DesignWare I2C, HID over I2C, ELAN I2C, ELAN PS/2, gestures) against a simulated laptop')
    D = os.path.join(KDIR, 'drivers')
    rd = lambda *p: kread(os.path.join(D, *p), hdr=False)
    mods = [('acpi', 'aml_scan.c'), ('i2c', 'dw_i2c.c'), ('input', 'touchpad/tp_hid.c'), ('input', 'touchpad/tp_gesture.c'), ('input', 'input.c'), ('input', 'touchpad/i2c_hid.c'), ('input', 'touchpad/elan_i2c.c'), ('input', 'ps2/ps2.c'), ('input', 'ps2/ps2_mouse.c'), ('input', 'ps2/elan_ps2.c'), ('input', 'touchpad/touchpad.c')]
    hdrs = ''.join(khdr(os.path.join(D, *m)) for m in mods + [('input', 'touchpad/tp_acpi.c'), ('input', 'touchpad/tp_igpio.c')])
    try:
        aml = rd('acpi', 'aml_scan.c').replace('static void aml_load_from_acpi(void){', 'static void aml_load_from_acpi_real(void){')
        dw = rd('i2c', 'dw_i2c.c')
        for a, b in [('static u32 dw_r(dwi2c* c,u32 o){ return *(volatile u32*)(c->base+o); }', 'static u32 dw_r(dwi2c* c,u32 o){ return sim_rd(c->base,o); }'),
                     ('static void dw_w(dwi2c* c,u32 o,u32 v){ *(volatile u32*)(c->base+o)=v; }', 'static void dw_w(dwi2c* c,u32 o,u32 v){ sim_wr(c->base,o,v); }'),
                     ('return *(volatile u32*)(base+DW_COMP_TYPE)==DW_TYPE_ID;', 'return sim_rd(base,DW_COMP_TYPE)==DW_TYPE_ID;')]:
            assert a in dw, a; dw = dw.replace(a, b)
        inp = rd('input', 'input.c')
        ih = rd('input', 'touchpad/i2c_hid.c'); a = '*(volatile u32*)(t->gpio_base+(u64)t->pin*4u)'; assert a in ih; ih = ih.replace(a, 'g_gpio_reg[t->pin&255]')
        tp = rd('input', 'touchpad/touchpad.c'); tp = tp[:tp.find('static const char* tp_gname')]
        el = rd('input', 'touchpad/elan_i2c.c'); ep = rd('input', 'ps2/elan_ps2.c')
        ps = rd('input', 'ps2/ps2.c'); ps = ps[ps.find('static int mpk=0;'):ps.find('static void ps2_byte')]
        it = kread(os.path.join(KDIR, 'drivers/input/ps2/ps2_mouse.c'), hdr=False); i0 = max(it.find('static int pw_write(void)'), it.find('\nint pw_write(void)') + 1 if it.find('\nint pw_write(void)') >= 0 else -1); i1 = it.find('    g_mouse_ok=1;\n}\n', i0) + len('    g_mouse_ok=1;\n}\n'); it = it[i0:i1]
        assert ps and i0 >= 0 and i1 > i0, 'PS/2 code not found'
        a2 = '*(volatile u32*)(base+0x204)=0x7u;'; assert a2 in tp; tp = tp.replace(a2, '(void)0;')
    except (AssertionError, OSError) as e:
        record('touchpad', 'touchpad sources located', False, str(e)); return
    record('touchpad', 'touchpad sources located', True)
    tpl = template(os.path.join(TESTS, 'tp_template.txt'), ['HDR', 'TEST'])
    allsrc = aml + dw + rd('input', 'touchpad/tp_hid.c') + rd('input', 'touchpad/tp_gesture.c') + inp + ih + el + ps + it + ep + tp
    c = os.path.join(work, 'tp_t.c'); open(c, 'w').write(tpl['HDR'] + '\n' + hdrs + kfwd(allsrc, allsrc) + aml + dw + rd('input', 'touchpad/tp_hid.c') + rd('input', 'touchpad/tp_gesture.c') + inp + ih + el + ps + it + ep + tp + tpl['TEST']); exe = os.path.join(work, 'tp_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c])
    if not record('touchpad', 'test builds against the current touchpad code', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=300); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.strip().endswith(' ok') or l.strip().endswith('FAIL')])
    record('touchpad', f'discovery, I2C, HID, ELAN I2C + trackpoint, Elantech v3/v4 PS/2, gestures, fuzzing ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:3]) or out[-300:])

def g_nvme(work):
    head('NVMe driver (generated from nvme.c, simulated controller)')
    s = kread(os.path.join(KDIR, 'drivers/storage/nvme.c'))
    a = s.find('#define NVME_ASQ'); b = s.find('/* ---- generic disk layer')
    if not record('nvme', 'driver code located in nvme.c', a >= 0 and b > a): return
    tp = template(os.path.join(TESTS, 'nvme_template.txt'), ['HDR', 'TEST'])
    c = os.path.join(work, 'nvme_t.c'); open(c, 'w').write(tp['HDR'].replace('#include <stdint.h>', '#include <stdint.h>\n#include <stdlib.h>') + '\n' + s[a:b] + tp['TEST']); exe = os.path.join(work, 'nvme_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=undefined', '-fno-sanitize-recover=undefined', '-o', exe, c])
    if not record('nvme', 'test builds against the current NVMe driver', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    record('nvme', 'init, 4 KB sectors, queue limits, wrap, stray completions, flush, timeout recovery, namespaces', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_memory(work):
    head('memory map, heap and DMA placement (generated from pmm_vmm.c and kernel64.c)')
    s = kread(os.path.join(KDIR, 'mem/pmm_vmm.c')); k = kread(os.path.join(KDIR, 'mem/heap.c'))
    a = s.find('typedef struct { u64 s,e; u32 t; } mmreg;'); b = s.find('static unsigned long long dma_cur')
    h0 = k.find('typedef struct hblk'); h1 = k.find('static void* krealloc')
    if not record('memory', 'memory-map and heap code located', a >= 0 and b > a and h0 >= 0 and h1 > h0): return
    body = s[a:b]; body = body[:body.find('static void mm_load(void){')] + body[body.find('static int mm_known(void)'):]
    heap = k[h0:h1].replace('static int mm_known(void); static u64 mm_usable_end(u64 c);', '')
    tp = template(os.path.join(TESTS, 'mm_template.txt'), ['HDR', 'TEST'])
    c = os.path.join(work, 'mm_t.c'); open(c, 'w').write(tp['HDR'] + '\n' + body + heap + tp['TEST']); exe = os.path.join(work, 'mm_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c])
    if not record('memory', 'test builds against the current memory code', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    record('memory', 'AMD-style firmware map: reserved/ACPI NVS detection, DMA skip, heap bounds, block headers', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_gpu(work):
    head('NVIDIA display: VBIOS tables, EDID, DDC/I2C, engine state')
    src = os.path.join(KDIR, 'drivers/video/nvidia/nvmon.c')
    t = open(os.path.join(TESTS, 'gpu_t.c')).read().replace('#include "NVMON_C"', kread(src))
    c = os.path.join(work, 'gpu_t.c'); open(c, 'w').write(t); exe = os.path.join(work, 'gpu_t')
    rc, out = sh(['gcc', '-O0', '-w'] + SAN + ['-o', exe, c])
    if not record('gpu', 'test builds against the current nvmon.c', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'outputs, monitor EDID over simulated I2C, head timings ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def host_libc(work):
    L = os.path.join(SRC, 'usr/libc'); ginc = sh(['gcc', '-print-file-name=include'])[1].strip()
    hf = ['-m64', '-O1', '-ffreestanding', '-nostdinc', '-isystem', os.path.join(L, 'include'), '-isystem', ginc, '-fno-pie', '-fno-stack-protector', '-w',
          '-I', os.path.join(SRC, 'usr/src'), '-D_GNU_SOURCE'] + LIBC_SAN
    objs = []
    for f in ('libc.c', 'printf.c', 'stdlib2.c', 'stdio2.c', 'sys_host.c', 'crt0_host.S'):
        o = os.path.join(work, f + '.o'); rc, out = sh(['gcc'] + hf + ['-c', os.path.join(L, 'src', f), '-o', o])
        if rc: return None, None, out
        objs.append(o)
    crt = objs.pop(); lib = os.path.join(work, 'libc.a'); sh(['ar', 'rcs', lib] + objs)
    return hf, (crt, lib), ''

def nvx_link(work, hf, rt, srcs, exe):
    objs = []
    for s in srcs:
        o = os.path.join(work, os.path.basename(s) + '.nvx.o'); rc, out = sh(['gcc'] + hf + ['-c', s, '-o', o])
        if rc: return out
        objs.append(o)
    rc, out = sh(['ld', '-static', '-nostdlib', '-o', exe, rt[0]] + objs + [rt[1]])
    return '' if rc == 0 else out

def gen_strtod_inputs(path, n):
    from fractions import Fraction
    from decimal import Decimal, getcontext
    getcontext().prec = 900; random.seed(7); out = []
    d2f = lambda u: struct.unpack('<d', struct.pack('<Q', u))[0]
    for _ in range(n):
        k = random.randrange(6)
        if k <= 1:
            u = random.randrange(1, 0x7FEFFFFFFFFFFFFF); a, b = Fraction(d2f(u)), Fraction(d2f(u + 1)); mid = (a + b) / 2
            ex = Decimal(mid.numerator) / Decimal(mid.denominator)
            out.append(format(ex, '.%de' % random.choice([17, 20, 25, 40, 60, 120])) if k == 0 else (format(ex, 'f') if abs(ex.adjusted()) < 30 else format(ex, 'e')))
        elif k == 2:
            nd = random.randint(1, 40); s = ('-' if random.random() < .5 else '') + ''.join(random.choice('0123456789') for _ in range(nd))
            if random.random() < .6: p = random.randint(0, len(s)); s = s[:p] + '.' + s[p:]
            if random.random() < .6: s += 'e%d' % random.randint(-350, 350)
            out.append(s)
        elif k == 3:
            nd = random.randint(300, 700); out.append(''.join(random.choice('0123456789') for _ in range(nd)) + 'e-%d' % (nd + random.randint(0, 330)))
        elif k == 4: out.append(repr(d2f(random.randrange(0, 0x0010000000000000))))
        else: out.append(random.choice(["1e", "1e+", "-.5", "+.e3", "0x1.8p3", "0X.8P-2", "0x1p-1074", "0x1p-1075", "0x1.fffffffffffff8p1023", "1.7976931348623158e308",
                                        "1.7976931348623159e308", "2.4703282292062327e-324", "2.4703282292062328e-324", "nan(abc)", "infinityx", "  -0", "00000.0000001e7", ".", "-", "e5", "0x", "0xg"]))
    open(path, 'w').write('\n'.join(out) + '\n')

def g_libc(work, quick):
    head('nvxlibc vs glibc (differential)')
    hf, rt, err = host_libc(work)
    if not record('libc', 'libc builds for the host test backend', hf is not None, err[:300]): return None
    LT = os.path.join(TESTS, 'libc')
    for name, src, desc in (('pfuzz', 'pfuzz.c', 'printf family: random formats identical to glibc'), ('ltest', 'ltest.c', 'strings, strtol, sscanf, qsort, ctype, malloc, files, strftime, setjmp')):
        s = os.path.join(LT, src)
        if quick and name == 'pfuzz':
            t = open(s).read().replace('i<100000', 'i<20000'); s = os.path.join(work, 'pfuzz_q.c'); open(s, 'w').write(t)
        g = os.path.join(work, name + '_glibc'); rc1, o1 = sh(['gcc', '-O1', '-w', '-o', g, s])
        n = os.path.join(work, name + '_nvx'); e = nvx_link(work, hf, rt, [s], n)
        if rc1 or e: record('libc', desc, False, (o1 + e)[:200]); continue
        rcg, og = sh([g], cwd=work); rcn, on = sh([n], cwd=work)
        diff = sum(1 for a, b in zip(og.splitlines(), on.splitlines()) if a != b) + abs(len(og.splitlines()) - len(on.splitlines()))
        record('libc', f'{desc} ({len(og.splitlines())} lines, {diff} different)', diff == 0 and rcn == rcg)
    inp = os.path.join(work, 'strtod_inputs.txt'); gen_strtod_inputs(inp, 8000 if quick else 30000)
    s = os.path.join(LT, 'sfile.c'); g = os.path.join(work, 'sf_glibc'); sh(['gcc', '-O1', '-w', '-o', g, s]); n = os.path.join(work, 'sf_nvx'); e = nvx_link(work, hf, rt, [s], n)
    env_run = lambda exe: subprocess.run([exe], cwd=work, capture_output=True, text=True, env=dict(os.environ, STRTOD_INPUTS=inp)).stdout
    if e: record('libc', 'strtod builds', False, e[:200])
    else:
        og, on = env_run(g), env_run(n); diff = sum(1 for a, b in zip(og.splitlines(), on.splitlines()) if a != b)
        record('libc', f'strtod correctly rounded incl. exact halfway cases ({len(og.splitlines())} inputs, {diff} different)', diff == 0 and len(og) > 0 and len(og.splitlines()) == len(on.splitlines()))
    return hf, rt

COREUTILS = [
    ("cat", "{nb} cat fruit.txt nonl.txt", "cat fruit.txt nonl.txt"), ("cat missing", "{nb} cat nofile", "cat nofile"),
    ("wc", "{nb} wc fruit.txt", "wc fruit.txt"), ("wc -l", "{nb} wc -l nums.txt", "wc -l nums.txt"), ("wc -w stdin", "{nb} wc -w < fruit.txt", "wc -w < fruit.txt"),
    ("wc 2 files", "{nb} wc fruit.txt nums.txt", "wc fruit.txt nums.txt"), ("wc pipe", "cat fruit.txt | {nb} wc", "cat fruit.txt | wc"),
    ("head -n 3", "{nb} head -n 3 nums.txt", "head -n 3 nums.txt"), ("head -c 7", "{nb} head -c 7 fruit.txt", "head -c 7 fruit.txt"),
    ("head 2 files", "{nb} head -n 2 fruit.txt nums.txt", "head -n 2 fruit.txt nums.txt"), ("tail -n 4", "{nb} tail -n 4 nums.txt", "tail -n 4 nums.txt"),
    ("tail no newline", "{nb} tail -n 1 nonl.txt", "tail -n 1 nonl.txt"), ("tail -n +25", "{nb} tail -n +25 nums.txt", "tail -n +25 nums.txt"),
    ("tail stdin", "seq 1 100 | {nb} tail -n 3", "seq 1 100 | tail -n 3"), ("grep", "{nb} grep apple fruit.txt", "grep apple fruit.txt"),
    ("grep -i", "{nb} grep -i apple fruit.txt", "grep -i apple fruit.txt"), ("grep -v", "{nb} grep -v apple fruit.txt", "grep -v apple fruit.txt"),
    ("grep -c", "{nb} grep -c apple fruit.txt", "grep -c apple fruit.txt"), ("grep -in 2 files", "{nb} grep -in a fruit.txt colon.txt", "grep -in a fruit.txt colon.txt"),
    ("grep -l", "{nb} grep -l a fruit.txt nums.txt colon.txt", "grep -l a fruit.txt nums.txt colon.txt"), ("grep no match", "{nb} grep xyz fruit.txt", "grep xyz fruit.txt"),
    ("grep missing", "{nb} grep x nofile", "grep x nofile"), ("sort", "{nb} sort fruit.txt", "LC_ALL=C sort fruit.txt"), ("sort -r", "{nb} sort -r fruit.txt", "LC_ALL=C sort -r fruit.txt"),
    ("sort -n", "{nb} sort -n mixed.txt", "LC_ALL=C sort -n mixed.txt"), ("sort -u", "{nb} sort -u dup.txt", "LC_ALL=C sort -u dup.txt"), ("sort -f", "{nb} sort -f fruit.txt", "LC_ALL=C sort -f fruit.txt"),
    ("cut -f2", "{nb} cut -d: -f2 colon.txt", "cut -d: -f2 colon.txt"), ("cut -f2-", "{nb} cut -d: -f2- colon.txt", "cut -d: -f2- colon.txt"), ("cut -s", "{nb} cut -s -d: -f1 colon.txt", "cut -s -d: -f1 colon.txt"),
    ("cut -c", "{nb} cut -c2-4 fruit.txt", "cut -c2-4 fruit.txt"), ("tr", "{nb} tr a-z A-Z < fruit.txt", "tr a-z A-Z < fruit.txt"), ("tr -d", "{nb} tr -d aeiou < fruit.txt", "tr -d aeiou < fruit.txt"),
    ("tr -s", "echo 'aaabbbccc  dd' | {nb} tr -s 'a-z '", "echo 'aaabbbccc  dd' | tr -s 'a-z '"), ("tr classes", "{nb} tr '[:lower:]' '[:upper:]' < fruit.txt", "tr '[:lower:]' '[:upper:]' < fruit.txt"),
    ("uniq -c", "{nb} uniq -c dup.txt", "uniq -c dup.txt"), ("uniq -d", "{nb} uniq -d dup.txt", "uniq -d dup.txt"), ("seq", "{nb} seq 2 3 20", "seq 2 3 20"),
    ("seq -s,", "{nb} seq -s, 1 5", "seq -s, 1 5"), ("seq -w", "{nb} seq -w 8 11", "seq -w 8 11"), ("basename", "{nb} basename /a/b/file.txt .txt", "basename /a/b/file.txt .txt"),
    ("dirname", "{nb} dirname /usr/lib/libc.so", "dirname /usr/lib/libc.so"), ("rev", "{nb} rev fruit.txt", "rev fruit.txt"), ("tac", "{nb} tac fruit.txt", "tac fruit.txt"),
    ("nl", "{nb} nl fruit.txt", "nl fruit.txt"), ("fold", "{nb} fold -w 4 fruit.txt", "fold -w 4 fruit.txt"), ("paste", "{nb} paste s1.txt s2.txt", "paste s1.txt s2.txt"),
    ("paste -s", "{nb} paste -s s1.txt", "paste -s s1.txt"), ("comm", "{nb} comm s1.txt s2.txt", "comm s1.txt s2.txt"), ("cmp differ", "{nb} cmp s1.txt s2.txt", "cmp s1.txt s2.txt"),
    ("md5sum", "{nb} md5sum fruit.txt bin.dat", "md5sum fruit.txt bin.dat"), ("sha256sum", "{nb} sha256sum fruit.txt bin.dat", "sha256sum fruit.txt bin.dat"),
    ("base64", "{nb} base64 bin.dat", "base64 bin.dat"), ("base64 round trip", "{nb} base64 bin.dat | {nb} base64 -d | cmp - bin.dat", "base64 bin.dat | base64 -d | cmp - bin.dat"),
    ("find -name", "{nb} find tree -name '*.c' | sort", "find tree -name '*.c' | sort"), ("find -type d", "{nb} find tree -type d | sort", "find tree -type d | sort"),
    ("find -maxdepth", "{nb} find tree -maxdepth 1 | sort", "find tree -maxdepth 1 | sort"), ("yes | head", "{nb} yes | head -n 3", "yes | head -n 3"),
    ("echo", "{nb} echo hello  world", "echo hello world"), ("true/false", "{nb} true; echo $?; {nb} false", "true; echo $?; false"),
    ("mkdir/rmdir", "{nb} mkdir d1 && test -d d1 && {nb} rmdir d1 && test ! -e d1", "mkdir d1 && test -d d1 && rmdir d1 && test ! -e d1"),
    ("cp", "{nb} cp fruit.txt c1 && cmp fruit.txt c1 && rm c1", "cp fruit.txt c1 && cmp fruit.txt c1 && rm c1"),
    ("ln -s/readlink", "{nb} ln -s fruit.txt l1 && {nb} readlink l1 && rm l1", "ln -s fruit.txt l1 && readlink l1 && rm l1"),
    ("tee", "echo teedata | {nb} tee t1 && cat t1 && rm t1", "echo teedata | tee t1 && cat t1 && rm t1"), ("ls (pipe)", "{nb} ls", "ls"),
]

def g_coreutils(work, libc):
    head('nvxbox tools vs GNU coreutils')
    if not libc: record('coreutils', 'needs the host libc', False); return
    hf, rt = libc; u = os.path.join(SRC, 'usr/src'); nb = os.path.join(work, 'nvxbox')
    e = nvx_link(work, hf, rt, [os.path.join(u, 'nvxbox.c'), os.path.join(u, 'coreutils.c')], nb)
    if not record('coreutils', 'nvxbox builds for the host test backend', not e, e[:200]): return
    d = os.path.join(work, 'cu'); os.makedirs(os.path.join(d, 'tree', 'sub', 'deep'))
    files = {'fruit.txt': 'banana\napple\ncherry\napple pie\n\nzebra\nApple\n', 'nums.txt': ''.join(f'{i}\n' for i in range(1, 31)), 'nonl.txt': 'no newline at end',
             'colon.txt': 'a:b:c\none:two:three:four\nno delims here\n:lead\ntrail:\n', 'mixed.txt': '10\n9\n100\n-3\n2.5\nabc\n9\n', 'dup.txt': 'a\na\nb\nc\nc\nc\nd\n',
             's1.txt': 'apple\nbanana\ncherry\n', 's2.txt': 'banana\ncherry\ndate\n'}
    for k, v in files.items(): open(os.path.join(d, k), 'w').write(v)
    for f in ('tree/a.c', 'tree/b.h', 'tree/sub/c.c', 'tree/sub/deep/d.txt'): open(os.path.join(d, f), 'w').close()
    random.seed(3); open(os.path.join(d, 'bin.dat'), 'wb').write(bytes(random.randrange(256) for _ in range(3000)))
    bad = []
    for name, nv, gn in COREUTILS:
        a = subprocess.run(nv.format(nb=nb), shell=True, cwd=d, capture_output=True, text=True, errors='replace')
        b = subprocess.run(gn, shell=True, cwd=d, capture_output=True, text=True, errors='replace')
        if a.stdout != b.stdout or a.returncode != b.returncode or bool(a.stderr) != bool(b.stderr): bad.append(name)
    for n in bad: print('    differs: ' + n)
    record('coreutils', f'{len(COREUTILS) - len(bad)} of {len(COREUTILS)} tool behaviours identical to GNU', not bad, ', '.join(bad[:5]))

def c_bytes(name, data):
    return f'static const unsigned char {name}[]={{' + ','.join(str(b) for b in data) + '};\n'
def c_str(name, text):
    return f'static const char {name}[]=' + ''.join('"' + l.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n' for l in text.splitlines()) + ';\n'
def g_aml(work):
    head('ACPI AML interpreter (aml.c) against ACPICA results, and a simulated laptop (EC, battery, AC, backlight)')
    A = os.path.join(TESTS, 'aml')
    src = kread(os.path.join(KDIR, 'drivers/acpi/aml.c'))
    tp = template(os.path.join(TESTS, 'aml_template.txt'), ['HDR', 'TEST'])
    data = c_bytes('CALC_AML', open(os.path.join(A, 'calc.aml'), 'rb').read()) + c_str('CALC_EXP', open(os.path.join(A, 'calc.expected')).read())
    for nm, f in (('LENOVO_AML', 'lenovo.aml'), ('HP_AML', 'hp.aml'), ('HPS_AML', 'hp_ssdt.aml'), ('EDGE_AML', 'edge.aml')): data += c_bytes(nm, open(os.path.join(A, f), 'rb').read())
    pw = kread(os.path.join(KDIR, 'core/power.c'))
    if 'static struct { int ok; u64 rsdp;' in pw:
        gst = pw[pw.find('static struct { int ok; u64 rsdp;'):]; gst = gst[:gst.find('g_acpi;') + 7] + '\n'
    else:
        i = pw.find('struct nvx_g_acpi_t {'); gst = pw[i:pw.find('};', i) + 2] + '\nstatic struct nvx_g_acpi_t g_acpi;\n'
    pwf = gst + '\n'.join(defn(pw, n) for n in ('acpi_r16', 'acpi_r32', 'acpi_r64', 'acpi_sum', 'acpi_rsdp_ok', 'acpi_tbl_ok', 'acpi_enable_mode')) + '\nstatic u64 g_test_rsdp; static u64 acpi_find_rsdp(void){ return acpi_rsdp_ok(g_test_rsdp)?g_test_rsdp:0; }\n'
    fbc = kread(os.path.join(KDIR, 'drivers/bus/pci.c'))
    dev = kread(os.path.join(KDIR, 'drivers/acpi/acpi_dev.c'))
    dev = dev.replace(defn(dev, 'acpi_mem_rd'), 'static u64 acpi_mem_rd(u64 a,int w){ u8* q=sim_mem(a,w); u64 v=0; if(!q)return w>=8?~0ULL:((1ULL<<(8*w))-1); memcpy(&v,q,(size_t)w); return v; }')
    wd = 'if(w==1)*(volatile u8*)a=(u8)v; else if(w==2)*(volatile u16*)a=(u16)v; else if(w==4)*(volatile u32*)a=(u32)v; else *(volatile u64*)a=v; return 0; }'
    if not record('aml', 'SystemMemory access code located in acpi_dev.c', wd in dev): return
    dev = dev.replace(wd, '{ u8* q=sim_mem(a,w); if(q)memcpy(q,&v,(size_t)w); } return 0; }')
    c = os.path.join(work, 'aml_t.c'); open(c, 'w').write(tp['HDR'] + kmissing(src + pwf + dev, tp['HDR'] + tp['TEST']) + src + pwf + defn(fbc, 'pci_cfg') + '\n' + dev + data + tp['TEST'].replace('static u64 g_test_rsdp;', '')); exe = os.path.join(work, 'aml_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c])
    if not record('aml', 'test builds against the current interpreter', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=300); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in out.splitlines():
        if l.startswith('      '): print(l)
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.strip().endswith(' ok') or l.strip().endswith('FAIL')])
    record('aml', f'interpreter matches ACPICA, laptop battery/AC/backlight/EC events ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:3]) or out[-300:])

def g_timers(work):
    head('HPET, TSC and local APIC timer (generated from hrtimer.c and smp.c, simulated machines)')
    src = kread(os.path.join(KDIR, 'arch/x86_64/time/hrtimer.c'))
    rs = kread(os.path.join(KDIR, 'arch/x86_64/smp/smp.c'))
    subs = {'hr_rdtsc': 'static u64 hr_rdtsc(void){ return sim_tsc(); }', 'hr_cpuid': 'static void hr_cpuid(u32 leaf,u32 sub,u32* r){ sim_cpuid(leaf,sub,r); }',
            'hr_rdmsr': 'static u64 hr_rdmsr(u32 m){ return sim_rdmsr(m); }', 'hr_wrmsr': 'static void hr_wrmsr(u32 m,u64 v){ sim_wrmsr(m,v); }',
            'hr_rd32': 'static u32 hr_rd32(u64 a){ return sim_rd32(a); }', 'hr_rd64': 'static u64 hr_rd64(u64 a){ return sim_rd64(a); }',
            'hr_wr64': 'static void hr_wr64(u64 a,u64 v){ sim_wr64(a,v); }', 'hr_irqsave': 'static u64 hr_irqsave(void){ return 0; }', 'hr_irqrestore': 'static void hr_irqrestore(u64 f){ (void)f; }'}
    try:
        for n, b in subs.items(): src = src.replace(defn(src, n), b)
        a = '*(volatile u32*)(g_lapic_base+off)=v;'; assert a in src; src = src.replace(a, 'sim_wr32(g_lapic_base+off,v);')
        madt = defn(rs, 'madt_apic_ids')
    except (KeyError, AssertionError) as e:
        record('timers', 'timer sources located', False, str(e)); return
    tp = template(os.path.join(TESTS, 'hr_template.txt'), ['HDR', 'TEST'])
    c = os.path.join(work, 'hr_t.c'); open(c, 'w').write(tp['HDR'].replace('#include <stdlib.h>', '#include <stdlib.h>\n#include <math.h>') + '\n' + src + madt + '\n' + tp['TEST']); exe = os.path.join(work, 'hr_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c, '-lm'])
    if not record('timers', 'test builds against the current timer code', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=300); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.strip().endswith(' ok') or l.strip().endswith('FAIL')])
    record('timers', f'HPET/PIT/CPUID calibration, wrap, x2APIC, one-shot + periodic APIC timer, MADT ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:3]) or out[-300:])

def g_linux(work):
    head('Linux system-call layer + musl libc (real kernel code run on the host, guest syscalls trapped with seccomp)')
    T = os.path.join(TESTS, 'linux'); B = os.path.join(KDIR, 'build'); M = os.path.join(B, 'musl')
    blobs = sorted(os.path.join(B, f) for f in os.listdir(B) if f.startswith('user_') and f.endswith('_bin.o')) if os.path.isdir(B) else []
    need = [os.path.join(M, 'lib', 'libc.a'), os.path.join(B, 'user', 'musltest.elf'), os.path.join(B, 'user', 'mhello.elf')]
    if not record('linux', 'musl libc and musl programs are built (build.sh)', all(os.path.exists(x) for x in need) and blobs, 'run check.py --build first'): return
    k = os.path.join(work, 'lk')
    shutil.copytree(KDIR, k, ignore=lambda d, names: [n for n in names if (d == KDIR and n in ('build', 'boot', 'tools', 'docs', '.git')) or (os.path.basename(d) == 'third_party' and n in ('musl', 'doom', 'lua', 'ffmpeg', 'src'))])
    shutil.copy(os.path.join(T, 'cpu_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'cpu.h'))
    shutil.copy(os.path.join(T, 'io_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'io.h'))
    shutil.copy(os.path.join(T, 'hk_glue.c'), os.path.join(k, 'hk_glue.c'))
    ren = []
    for n in ('malloc', 'free', 'calloc', 'realloc', 'memchr', 'memcmp', 'memcpy', 'memmove', 'memset', 'strcat', 'strchr', 'strcmp', 'strcpy', 'strlen', 'strncmp', 'strncpy', 'strrchr', 'strstr', 'abs', 'atoi', 'panic'):
        ren.append(f'-D{n}=kh_{n}')
    hk = os.path.join(work, 'hk.o')
    rc, out = sh(['gcc', '-m64', '-O1', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-mno-mmx', '-mno-sse', '-mno-sse2',
                  '-mcmodel=small', '-DNOOVEX64', '-w', '-I.'] + ren + ['-c', 'hk_glue.c', '-o', hk], cwd=k, timeout=900)
    if not record('linux', 'whole kernel compiles as a host object (host cpu.h / io.h shims)', rc == 0, out[:400]): return
    hh = os.path.join(work, 'hh.o'); exe = os.path.join(work, 'lxharness')
    rc, out = sh(['gcc', '-O1', '-fno-stack-protector', '-fno-pie', '-c', os.path.join(T, 'hh.c'), '-o', hh])
    if rc == 0: rc, out = sh(['gcc', '-no-pie', '-o', exe, hh, hk] + blobs + ['-Wl,--unresolved-symbols=ignore-all', '-z', 'noexecstack'])
    if not record('linux', 'seccomp harness links against the kernel and embedded programs', rc == 0, out[:400]): return
    gi = sh(['gcc', '-print-file-name=include'])[1].strip(); lg = sh(['gcc', '-print-libgcc-file-name'])[1].strip(); L = os.path.join(M, 'lib')
    def mprog(src, dst):
        o = dst + '.o'
        r, t = sh(['gcc', '-fPIE', '-O2', '-nostdinc', '-isystem', os.path.join(M, 'include'), '-isystem', gi, '-w', '-c', src, '-o', o])
        if r: return r, t
        return sh(['gcc', '-static-pie', '-nostdlib', '-Wl,--no-dynamic-linker', '-o', dst, os.path.join(L, 'rcrt1.o'), os.path.join(L, 'crti.o'), o, os.path.join(L, 'libc.a'), lg, os.path.join(L, 'crtn.o')])
    fz = os.path.join(work, 'lxfuzz'); rc, out = mprog(os.path.join(T, 'lxfuzz.c'), fz)
    if not record('linux', 'system-call fuzzer builds against musl', rc == 0, out[:400]): return
    mtd = os.path.join(work, 'mtref'); os.makedirs(mtd, exist_ok=True)
    env = dict(os.environ, MT_HELLO=os.path.join(B, 'user', 'mhello.elf'), MT_DIR=mtd)
    p = subprocess.run([os.path.join(B, 'user', 'musltest.elf')], env=env, capture_output=True, text=True, timeout=120)
    fails = [l for l in p.stdout.splitlines() if l.startswith('FAIL')]
    record('linux', 'musltest passes on real Linux (reference for the expected results)', p.returncode == 0 and not fails, '; '.join(fails[:3]) or p.stdout[-200:])
    for seed in ('1', '2', '3'):
        rc, out = sh([exe, fz, f'/tmp/lxfuzz 60000 {1 << 60} {seed}'], timeout=600)
        if seed == '1':
            for l in out.splitlines():
                if l.startswith('  ') and (l.rstrip().endswith(' ok') or 'FAIL' in l): print('  ' + l.strip()[:150])
        bad = [l.strip() for l in out.splitlines() if l.startswith('  ') and 'FAIL' in l]
        n = re.search(r'\((\d+) Linux system calls handled', out)
        record('linux', f'kernel runs musl + legacy programs, survives fuzz seed {seed} ({n.group(1) if n else "?"} syscalls)', rc == 0 and 'ALL PASS' in out, '; '.join(bad[:3]) or out[-300:])

def g_fs(work):
    head('NVX4 file system (real kernel code on the host, simulated disk with power cuts)')
    T = os.path.join(TESTS, 'fs'); L = os.path.join(TESTS, 'linux')
    k = os.path.join(work, 'fk')
    shutil.copytree(KDIR, k, ignore=lambda d, names: [n for n in names if (d == KDIR and n in ('build', 'boot', 'tools', 'docs', '.git')) or (os.path.basename(d) == 'third_party' and n in ('musl', 'doom', 'lua', 'sqlite', 'xz', 'bzip2', 'zlib', 'ffmpeg', 'src'))])
    shutil.copy(os.path.join(L, 'cpu_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'cpu.h'))
    shutil.copy(os.path.join(L, 'io_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'io.h'))
    shutil.copy(os.path.join(T, 'fs_glue.c'), os.path.join(k, 'fs_glue.c'))
    ren = []
    for n in ('malloc', 'free', 'calloc', 'realloc', 'memchr', 'memcmp', 'memcpy', 'memmove', 'memset', 'strcat', 'strchr', 'strcmp', 'strcpy', 'strlen', 'strncmp', 'strncpy', 'strrchr', 'strstr', 'abs', 'atoi', 'panic'):
        ren.append(f'-D{n}=kh_{n}')
    fo = os.path.join(work, 'fk.o')
    rc, out = sh(['gcc', '-m64', '-O1', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-mno-mmx', '-mno-sse', '-mno-sse2',
                  '-mcmodel=small', '-DNOOVEX64', '-w', '-I.'] + ren + ['-c', 'fs_glue.c', '-o', fo], cwd=k, timeout=900)
    if not record('fs', 'kernel compiles as a host object for the file-system tests', rc == 0, out[:400]): return
    exe = os.path.join(work, 'fsharness'); ho = os.path.join(work, 'fsh.o')
    rc, out = sh(['gcc', '-O1', '-c', os.path.join(T, 'fsh.c'), '-o', ho])
    if rc == 0: rc, out = sh(['gcc', '-no-pie', '-o', exe, ho, fo, '-Wl,--unresolved-symbols=ignore-all', '-z', 'noexecstack'])
    if not record('fs', 'file-system harness links', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=900)
    for l in out.splitlines():
        if l.startswith('  ') and (l.rstrip().endswith(' ok') or 'FAIL' in l): print('  ' + l.strip()[:160])
    n = len([l for l in out.splitlines() if l.startswith('  ') and l.rstrip().endswith(' ok')])
    bad = [l.strip() for l in out.splitlines() if 'FAIL' in l]
    record('fs', f'NVX4: format, persistence, Unix semantics, power cuts and old-format conversion ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(bad[:3]) or out[-400:])

def g_nvidia(work):
    head('NVIDIA Turing GSP driver (nvflcn.c, nvmem.c, nvgsp.c) against a simulated TU116')
    exe = os.path.join(work, 'nvsim')
    rc, out = sh(['gcc', '-O1', '-w', '-I.', '-DNOOVEX64', '-fno-pie', '-no-pie', '-o', exe, os.path.join(TESTS, 'nvidia', 'nvsim.c')], cwd=KDIR)
    if not record('nvidia', 'simulator builds against the current driver', rc == 0, out[:300]): return
    fw = os.environ.get('NVX_NVFW')
    if not fw:
        import lzma
        fw = os.path.join(work, 'nvfw'); A = os.path.join(KDIR, 'assets/nvidia')
        for xz, dst in (('gsp', 'tu102/gsp/gsp-535.113.01.bin'), ('bootloader', 'tu102/gsp/bootloader-535.113.01.bin'), ('booter_load', 'tu116/gsp/booter_load-535.113.01.bin'), ('booter_unload', 'tu116/gsp/booter_unload-535.113.01.bin'), ('acr_bl', 'tu116/acr/bl.bin')):
            d = os.path.join(fw, dst); os.makedirs(os.path.dirname(d), exist_ok=True)
            with lzma.open(os.path.join(A, f'nvfw_{xz}.xz')) as s, open(d, 'wb') as o: o.write(s.read())
    rc, out = sh([exe, fw], timeout=600)
    if 'skipped' in out: record('nvidia', 'firmware files present (kernel/assets/nvidia, or NVX_NVFW set to the linux-firmware nvidia folder)', False, out[-200:]); return
    fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('nvidia', f'VRAM, layout, allocator, VBIOS/FWSEC, booter, radix3, queues, RPC, BARs, objects, copy engine ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_qemu(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('SMP in QEMU (-smp 4): boot, "smp on", stress test, musltest on all cores, "smp off"')
    import socket
    work = tempfile.mkdtemp(prefix='nvx_qemu_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); disk = os.path.join(work, 'disk.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-m', '1024', '-smp', '4', '-accel', 'tcg,thread=multi',
           '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
           '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
           '-serial', f'file:{log}', '-display', 'none', '-no-reboot', '-monitor', f'unix:{mon},server,nowait']
    pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def serial():
        try: return open(log, errors='replace').read()
        except OSError: return ''
    def wait_for(text, secs):
        t = time.time()
        while time.time() - t < secs:
            if text in serial(): return True
            time.sleep(1)
        return False
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7'}
    def typ(sock, txt):
        for ch in txt:
            sock.sendall(('sendkey ' + keys.get(ch, ch) + '\n').encode()); time.sleep(0.08)
    try:
        if not record('qemu', 'boots to the main loop', wait_for('entering main loop', 120), serial()[-300:]): return
        time.sleep(3)
        s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.5)
        typ(s, 'smp on\n')
        record('qemu', 'smp on: the other cores start and schedule programs', wait_for('smp: scheduling on all cores', 60), serial()[-300:])
        typ(s, 'smptest 8 12\n')
        ok = wait_for('smptest: PASS', 240) or wait_for('smptest: FAIL', 1)
        m = re.search(r'smptest: \d+ reports, (\d+) CPUs used.*', serial())
        record('qemu', f'smptest on 4 cores: {m.group(0) if m else "no result"}', 'smptest: PASS' in serial() and m and int(m.group(1)) >= 2, serial()[-300:])
        typ(s, 'musltest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 300 and not re.search(r'musltest: \d+ passed', serial()): time.sleep(1)
        time.sleep(2)
        m = re.search(r'musltest: (\d+) passed, (\d+)', serial())
        record('qemu', f'musltest while scheduled across cores ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0', serial()[-300:])
        typ(s, 'featest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 400 and not re.search(r'featest: done \d+ passed', serial()): time.sleep(1)
        time.sleep(2)
        m = re.search(r'featest: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'featest: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'featest: 256 fds, big memory, timers, listen/accept, threads, terminal ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0' and int(m.group(1)) >= 50, serial()[-300:])
        typ(s, 'dyntest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 120 and not re.search(r'dyntest: done \d+ passed', serial()): time.sleep(1)
        time.sleep(1)
        m = re.search(r'dyntest: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'dyntest: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'dyntest: dynamic linker, shared libc, dlopen ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0', serial()[-300:])
        typ(s, 'smp off\n')
        record('qemu', 'smp off parks the other cores', wait_for('smp: secondary cores parked', 60), serial()[-300:])
        s.close()
    finally:
        pr.kill(); pr.wait(); shutil.rmtree(work, ignore_errors=True)

def g_tls(work):
    head('TLS certificate checks: X.509 parser, RSA/ECDSA, Mozilla roots, chains, host names, CertificateVerify')
    T = os.path.join(HERE, 'tests', 'tls'); exe = os.path.join(work, 'x509h')
    rc, out = sh(['gcc', '-O2', '-w', '-I', KDIR] + SAN + ['-o', exe, os.path.join(T, 'x509h.c')])
    if not record('tls', 'X.509 harness builds', rc == 0, out[:400]): return
    rc, out = sh([exe, 'roots'])
    record('tls', 'every embedded root key parses (' + out.strip().splitlines()[-1] + ')' if out.strip() else 'roots', rc == 0, out[-300:])
    pem = kread(os.path.join(KDIR, 'lib/crypto/roots/mozilla-cacert.pem'))
    import base64
    ders = []
    for i, b in enumerate(re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----', pem, re.S)):
        pth = os.path.join(work, f'root{i:03d}.der'); open(pth, 'wb').write(base64.b64decode(''.join(b.split()))); ders.append(pth)
    rc, out = sh([exe, 'selfsig'] + ders)
    m = re.search(r'selfsig ok (\d+) bad (\d+) skipped (\d+)', out)
    fails = [l for l in out.splitlines() if l.startswith('SIGFAIL')]
    only_p521 = all(' alg 6' in l for l in fails) and len(fails) <= 1
    record('tls', f'Mozilla root self-signatures verify with our RSA/P-256/P-384 code ({m.group(0) if m else "?"}; P-521 is not supported)', bool(m) and int(m.group(1)) >= 100 and only_p521, out[-400:])
    try:
        import cryptography
    except ImportError:
        record('tls', 'python cryptography module available for generated chains', False); return
    rc, out = sh([sys.executable, os.path.join(T, 'tlsvec.py'), exe, os.path.join(work, 'tlsvec')])
    for l in out.splitlines():
        if l.startswith(('ok', 'FAIL')): print('  ' + l[:150])
    n = len([l for l in out.splitlines() if l.startswith('ok')])
    record('tls', f'generated chains, host names, dates and CertificateVerify ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])

def big_sha():
    import hashlib
    return hashlib.sha256(bytes(((i*7+(i>>8))&0xFF) for i in range(300000))).hexdigest()

def g_fetch(work):
    head('fetch (BearSSL client for musl programs) on the host under sanitizers, against local TLS 1.2 servers')
    BR = os.path.join(KDIR, 'third_party/bearssl'); M = os.path.join(KDIR, 'usr/musl')
    try:
        import cryptography
    except ImportError:
        record('fetch', 'python cryptography module available', False); return
    od = os.path.join(work, 'brobj'); os.makedirs(od, exist_ok=True)
    srcs = sorted(os.path.join(r, f) for r, _, fs in os.walk(os.path.join(BR, 'src')) for f in fs if f.endswith('.c'))
    import concurrent.futures as cf
    def comp(src):
        o = os.path.join(od, os.path.relpath(src, os.path.join(BR, 'src')).replace(os.sep, '_')[:-2] + '.o')
        return sh(['gcc', '-O1', '-w', '-I', os.path.join(BR, 'inc'), '-I', os.path.join(BR, 'src')] + SAN + ['-c', src, '-o', o])[0]
    with cf.ThreadPoolExecutor(os.cpu_count() or 4) as ex: bad = [r for r in ex.map(comp, srcs) if r]
    if not record('fetch', f'BearSSL compiles on the host ({len(srcs)} files)', not bad): return
    exe = os.path.join(work, 'fetch')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-result', '-I', os.path.join(BR, 'inc'), '-I', M] + SAN + ['-o', exe, os.path.join(M, 'fetch.c'), os.path.join(M, 'nvxtls.c')] + sorted(os.path.join(od, f) for f in os.listdir(od)))
    if not record('fetch', 'fetch builds with -Wall and sanitizers', rc == 0 and 'warning' not in out, out[:400]): return
    pw = os.path.join(work, 'fpeer'); port = 19443 + random.randint(0, 400) * 4
    peer = subprocess.Popen([sys.executable, os.path.join(TESTS, 'tls', 'fetchpeer.py'), pw, str(port), os.path.join(KDIR, 'lib/crypto/roots/mozilla-cacert.pem')], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        l1 = peer.stdout.readline(); l2 = peer.stdout.readline()
        if not record('fetch', 'test servers start', 'peer up' in l2, l1 + l2): return
        env = dict(os.environ, NVX_ROOTS=os.path.join(pw, 'anchors.pem'), NVX_LOCAL_CA=os.path.join(pw, 'ca.pem'), ASAN_OPTIONS='detect_leaks=1')
        want = big_sha(); errs = []
        def run(args, extra=None):
            e = dict(env); e.update(extra or {})
            p = subprocess.run([exe] + args, env=e, capture_output=True, text=True, errors='replace', timeout=60)
            errs.append(p.stderr); return p.returncode, p.stderr
        rc, er = run(['-v', '-s', '-o', '/dev/null', f'https://localhost:{port}/big'])
        m = re.search(r'(\d+) trust anchors loaded', er)
        record('fetch', f'loads the anchors file and the local CA ({m.group(1) if m else "?"} anchors)', bool(m) and int(m.group(1)) >= 121, er[-300:])
        record('fetch', 'ECDSA server: certificate verified, 300 KB body intact', rc == 0 and 'certificate verified' in er and want in er, er[-300:])
        rc, er = run(['-s', '-o', '/dev/null', f'https://localhost:{port+1}/big'])
        record('fetch', 'RSA server: certificate verified, body intact', rc == 0 and 'ECDHE-RSA' in er and want in er, er[-300:])
        rc, er = run(['-s', '-o', '/dev/null', f'https://127.0.0.1:{port}/big'])
        record('fetch', 'IP address host is checked against the IP in the certificate', rc == 0 and 'certificate verified' in er, er[-300:])
        rc, er = run(['-s', '-o', '/dev/null', f'https://localhost:{port}/redir'])
        record('fetch', 'redirect followed, chunked body decoded', rc == 0 and 'redirect' in er and want in er, er[-300:])
        rc, er = run(['-o', '/dev/null', f'https://localhost:{port}/big'], {'NVX_LOCAL_CA': '/nonexistent'})
        record('fetch', 'unknown root is refused', rc == 3 and 'unknown root' in er, er[-300:])
        rc, er = run(['-o', '/dev/null', f'https://localhost:{port+2}/big'])
        record('fetch', 'certificate for another name is refused', rc == 3 and 'another name' in er, er[-300:])
        rc, er = run(['-o', '/dev/null', f'https://localhost:{port+3}/big'])
        record('fetch', 'expired certificate is refused', rc == 3 and 'expired' in er, er[-300:])
        rc, er = run(['-k', '-o', '/dev/null', f'https://localhost:{port+2}/big'])
        record('fetch', '-k connects without checks', rc == 0 and 'NOT checked' in er, er[-300:])
        rc, er = run(['-o', '/dev/null', f'https://localhost:{port}/missing'])
        record('fetch', 'HTTP 404 gives exit status 4', rc == 4 and 'HTTP 404' in er, er[-300:])
        rc, er = run(['ftp://x/'])
        record('fetch', 'bad URL gives exit status 1', rc == 1, er[-300:])
        allerr = ''.join(errs)
        record('fetch', 'no sanitizer or leak reports in any run', 'Sanitizer' not in allerr and 'runtime error' not in allerr, allerr[-300:])
    finally:
        peer.kill(); peer.wait()

def g_net(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso) or os.environ.get('NVX_NET_TAP') != '1': return
    head('Network in QEMU over a tap device: Linux sockets (nettest), native HTTP, HTTPS with certificate checks')
    import socket
    work = tempfile.mkdtemp(prefix='nvx_net_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); disk = os.path.join(work, 'disk.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    peer = subprocess.Popen([sys.executable, os.path.join(HERE, 'tests', 'net', 'netpeer.py'), work], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    line = peer.stdout.readline()
    if not record('net', 'test peer starts (tap device, DNS, HTTP, echo, TLS)', 'peer up' in line, line): peer.kill(); return
    bios = os.environ.get('NVX_QEMU_BIOS'); tap = os.environ.get('NVX_TAP_NAME', 'nvxtap0')
    cmd = [q] + (['-L', bios] if bios else []) + ['-m', '1024', '-smp', '2', '-accel', 'tcg,thread=multi',
           '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
           '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
           '-netdev', f'tap,id=n0,ifname={tap},script=no,downscript=no', '-device', 'e1000,netdev=n0',
           '-serial', f'file:{log}', '-display', 'none', '-no-reboot', '-monitor', f'unix:{mon},server,nowait']
    pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def serial():
        try: return open(log, errors='replace').read()
        except OSError: return ''
    def wait_for(text, secs, start=0):
        t = time.time()
        while time.time() - t < secs:
            if text in serial()[start:]: return True
            time.sleep(1)
        return False
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7', ':': 'shift-semicolon', '+': 'shift-equal', '=': 'equal'}
    def typ(sock, txt):
        for ch in txt:
            k = keys.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)
            sock.sendall(('sendkey ' + k + '\n').encode()); time.sleep(0.06)
    def https(sock, target, secs=90):
        mark = len(serial()); typ(sock, f'https {target}\n')
        t = time.time()
        while time.time() - t < secs:
            m = re.search(r'\] TLS result (-?\d+): ([^\n]*)', serial()[mark:])
            if m: return int(m.group(1)), m.group(2).strip()
            time.sleep(1)
        return None, 'no result'
    try:
        if not record('net', 'boots to the main loop', wait_for('entering main loop', 120), serial()[-300:]): return
        time.sleep(3)
        s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.5)
        typ(s, 'net\n'); wait_for('DHCP failed', 30); time.sleep(2)
        typ(s, 'nettest >/dev/kmsg\n')
        ok = wait_for('nettest: PASS', 180) or wait_for('nettest: FAIL', 1)
        for l in re.findall(r'nettest: (?:ok|FAIL)[^\n]*', serial())[::2]: print('  ' + l[:150])
        record('net', 'musl program: DNS, TCP, UDP, non-blocking connect, errors (nettest)', 'nettest: PASS' in serial(), serial()[-400:])
        mark = len(serial()); typ(s, 'http noovex.test/\n'); time.sleep(8)
        record('net', 'native http command fetches a page on port 80', 'http 80 ' in open(os.path.join(work, 'peer.log')).read(), '')
        code, why = https(s, 'noovex.test/')
        record('net', f'https refuses a server whose root is unknown ({code}: {why})', code == -11)
        def fetch(args, secs=60):
            mark = len(serial()); typ(s, f'fetch {args} 2>/dev/kmsg\n')
            t = time.time()
            while time.time() - t < secs:
                if re.search(r'fetch: (\S+ -> HTTP \d+|TLS with \S+ failed|cannot )', serial()[mark:]): time.sleep(3); break
                time.sleep(1)
            return serial()[mark:]
        out = fetch('-v -o /dev/null https://noovex.test:9443/')
        m = re.search(r'(\d+) trust anchors loaded', out)
        record('net', f'fetch reads the root store the kernel wrote to /etc/ssl ({m.group(1) if m else "?"} roots) and refuses the unknown test CA', bool(m) and int(m.group(1)) >= 120 and 'unknown root' in out, out[-300:])
        typ(s, 'mkdir /etc/ssl\n'); time.sleep(2)
        b64 = ''.join(l for l in open(os.path.join(work, 'ca.pem')).read().splitlines() if '-----' not in l)
        typ(s, 'echo -----BEGIN CERTIFICATE----- >/etc/ssl/local-ca.pem\n'); time.sleep(2)
        for i in range(0, len(b64), 120): typ(s, f'echo {b64[i:i+120]} >>/etc/ssl/local-ca.pem\n'); time.sleep(2)
        typ(s, 'echo -----END CERTIFICATE----- >>/etc/ssl/local-ca.pem\n'); time.sleep(2)
        code, why = https(s, 'noovex.test/')
        record('net', f'https with the CA in /etc/ssl/local-ca.pem, ECDSA server ({code}: {why})', code is not None and code > 0 and 'verified' in why)
        code, why = https(s, 'noovex.test:8443/')
        record('net', f'https to an RSA server, RSA-PSS signature ({code}: {why})', code is not None and code > 0 and 'verified' in why)
        code, why = https(s, '10.0.2.2/')
        record('net', f'https by IP address matches the IP in the certificate ({code}: {why})', code is not None and code > 0)
        code, why = https(s, 'wrong.test/')
        record('net', f'https refuses a certificate for another name ({code}: {why})', code == -12)
        code, why = https(s, '-k wrong.test/')
        record('net', f'https -k connects without checks ({code}: {why})', code is not None and code > 0 and 'WITHOUT' in why)
        want = big_sha()
        out = fetch('-s -o /dev/null https://noovex.test:9443/big')
        record('net', 'fetch (BearSSL, TLS 1.2): ECDSA server, CA from local-ca.pem, 300 KB intact', 'certificate verified' in out and want in out, out[-300:])
        out = fetch('-s -o /dev/null https://noovex.test:9444/chunked')
        record('net', 'fetch: RSA server, chunked body intact', 'ECDHE-RSA' in out and want in out, out[-300:])
        out = fetch('-s -o /dev/null http://noovex.test/redir')
        record('net', 'fetch: plain HTTP with a redirect', 'redirect' in out and want in out, out[-300:])
        out = fetch('-o /dev/null https://10.0.2.2:9443/')
        record('net', 'fetch by IP address checks the IP in the certificate', 'certificate verified' in out and 'HTTP 200' in out, out[-300:])
        out = fetch('-o /dev/null https://wrong.test:9443/')
        record('net', 'fetch refuses a certificate for another name', 'another name' in out, out[-300:])
        typ(s, 'featest listen >/dev/kmsg\n')
        t = time.time(); got = False
        while time.time() - t < 90 and not got:
            got = 'inbound pong from noovex' in open(os.path.join(work, 'peer.log')).read(); time.sleep(1)
        record('net', 'a musl server accepts a connection from the host (listen/accept over the NIC)', got, serial()[-300:])
        s.close()
    finally:
        pr.kill(); pr.wait(); peer.kill(); peer.wait(); shutil.rmtree(work, ignore_errors=True)

def g_drivers(work):
    head('separately built driver modules against simulated devices (RTL8139, RTL8111, VirtIO net/blk, AC97, HD Audio)')
    objs = []
    for m in kernel_modules():
        o = os.path.join(work, 'drv_' + os.path.basename(m)[:-2] + '.o')
        rc, out = sh(['gcc', '-O1', '-g', '-w', '-I' + KDIR] + SAN + ['-c', os.path.join(KDIR, m), '-o', o])
        if not record('drivers', f'{m} compiles on the host', rc == 0, out[:300]): return
        objs.append(o)
    exe = os.path.join(work, 'drvsim')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-I' + KDIR] + SAN + [os.path.join(TESTS, 'drivers', 'drvsim.c')] + objs + ['-o', exe])
    if not record('drivers', 'device simulator links against the modules', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300)
    for l in out.splitlines():
        if l.startswith('  '): print('  ' + l.strip()[:150])
    fails = [l.strip() for l in out.splitlines() if l.startswith('  ') and 'FAIL' in l]
    n = len([l for l in out.splitlines() if l.startswith('  ')])
    record('drivers', f'all driver modules behave correctly against simulated hardware ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:3]) or out[-300:])

def main():
    ap = argparse.ArgumentParser(description='NoovexOS automatic bug checker')
    ap.add_argument('--build', action='store_true', help='rebuild kernel + ISO first')
    ap.add_argument('--iso', help='image to test (default: the one built with --build, else tools/out/check.iso)')
    ap.add_argument('--quick', action='store_true', help='smaller random test sizes')
    ap.add_argument('--sanitize', action='store_true', help='run driver tests under ASan+UBSan and libc/tool tests with UBSan traps')
    ap.add_argument('--only', help='comma list: build,static,boot,usb,acpi,gpu,nvme,touchpad,memory,aml,timers,linux,fs,nvidia,tls,fetch,qemu,net,drivers,analyze,libc,coreutils')
    a = ap.parse_args()
    if a.sanitize:
        SAN[:] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer', '-g']
        LIBC_SAN[:] = ['-fsanitize=undefined', '-fsanitize-trap=all']
    only = set(a.only.split(',')) if a.only else None
    want = lambda g: only is None or g in only
    outdir = os.path.join(HERE, 'out'); os.makedirs(outdir, exist_ok=True)
    iso = a.iso or os.path.join(outdir, 'check.iso')
    work = tempfile.mkdtemp(prefix='nvx_check_'); t0 = time.time()
    keep = os.environ.get('NVX_KEEP_WORK')
    try:
        if a.build and want('build'): g_build(work, iso)
        if want('static'): g_static()
        if want('boot'):
            if os.path.exists(iso): g_boot(iso)
            else: record('boot', f'image {iso} exists (use --build or --iso)', False)
        if want('usb'): g_usb(work); g_xhci_init(work)
        if want('acpi'): g_acpi(work)
        if want('gpu'): g_gpu(work)
        if want('nvme'): g_nvme(work)
        if want('touchpad'): g_touchpad(work)
        if want('memory'): g_memory(work)
        if want('aml'): g_aml(work)
        if want('timers'): g_timers(work)
        if want('linux'): g_linux(work)
        if want('fs'): g_fs(work)
        if want('nvidia'): g_nvidia(work)
        if want('tls'): g_tls(work)
        if want('fetch'): g_fetch(work)
        if want('qemu'): g_qemu(iso)
        if want('net'): g_net(iso)
        if want('drivers'): g_drivers(work)
        if want('analyze'): g_analyze(work)
        libc = g_libc(work, a.quick) if (want('libc') or want('coreutils')) else None
        if want('coreutils'): g_coreutils(work, libc)
    finally:
        if keep: print('work dir kept:', work)
        else: shutil.rmtree(work, ignore_errors=True)
    bad = [r for r in RESULTS if not r[2]]
    print(f"\n==== {len(RESULTS) - len(bad)} of {len(RESULTS)} checks passed in {time.time() - t0:.0f}s ====")
    for g, n, ok, d in bad: print(f"  FAIL [{g}] {n}" + (f": {d[:200]}" if d else ''))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
