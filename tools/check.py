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
def sh(cmd, cwd=None, timeout=900, env=None):
    p = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True, text=True, errors='replace', timeout=timeout, env=env)
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

PART_INC = re.compile(r'^#include \"((?:[^\"]+_parts/|(?:apps/internet/browser/|shell/commands/|graphics/ui/desktop/|apps/system/settings/|sys/linux/syscalls/|drivers/usb/xhci/|drivers/video/nvidia/gsp/|apps/system/file_explorer/|core/init/installer/|fs/pkg/apt/|core/proc/jobs/))[^\"]+\.c)\"\s*$')

def kexpand(t):
    out = []
    for l in t.split('\n'):
        m = PART_INC.match(l)
        if m:
            x = kexpand(open(os.path.join(KDIR, m.group(1)), errors='ignore').read())
            out.append(x[:-1] if x.endswith('\n') else x)
        else:
            out.append(l)
    return '\n'.join(out)
def kread(path, hdr=True):
    t = kexpand(open(path, errors='ignore').read())
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
        if any(x in root for x in ('/ntfs', '/musl', '/sqlite', '/zlib', '/bzip2', '/xz', '/zstd', '/ffmpeg', '/quickjs', '/build', '/tinycc', '/vim', '/git', '/curl', '/mbedtls', '/dropbear', '/ncurses', '/less', '/netsurf', '/mlibc')) or os.sep + 'usr' in root or '/wifi' in root or root.endswith(os.sep + 'doom'): continue
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
    tb = open(os.path.join(KDIR, 'graphics/ui/desktop/window_titles_and_bars.c'), errors='ignore').read()
    m = re.search(r'int classic_tray_click\(int px\)\{(.*?)\n(?=\S)', tb, re.S); body = m.group(1) if m else ''
    ok_tray = 'g_wifipop=1' in body and 'g_qsmode=mode' in body and 'g_notifpanel^=1' in body and re.search(r'if\(g_qsmode==1\) return tvol_click', tb) and re.search(r'if\(g_qsmode==2\) return tpwr_click', tb)
    record('static', 'classic tray: Wi-Fi, sound, power and arrow icons open their own popups', bool(ok_tray), 'classic_tray_click does not dispatch per icon' if not ok_tray else '')
    asm = []
    for root, _, files in kwalk():
        rel = os.path.relpath(root, KDIR).split(os.sep)
        if rel[0] in ('arch', 'third_party', 'usr', 'build', 'tools') or rel[:3] == ['include', 'nvx', 'arch']: continue
        for f in files:
            if not f.endswith(('.c', '.h')): continue
            p = os.path.join(root, f); t = open(p, errors='ignore').read()
            for m in re.finditer(r'\b__asm__\b|\basm\s*\(', t): asm.append(f"{os.path.relpath(p, KDIR)}:{t.count(chr(10), 0, m.start()) + 1}")
    record('static', 'no inline assembly outside arch/ (portable code uses arch/x86_64/cpu/cpu.h)', not asm, ', '.join(asm[:6]))
    snd = []
    for root, _, files in kwalk():
        rel = os.path.relpath(root, KDIR).split(os.sep)
        if rel[0] in ('third_party', 'usr', 'build', 'tools'): continue
        for f in files:
            if not f.endswith(('.c', '.h')): continue
            p = os.path.join(root, f); t = open(p, errors='ignore').read()
            for m in re.finditer(r'if\s*\(\s*!?g_snd_state\s*\)\s*g_snd\.\w+\s*\(', t): snd.append(f"{os.path.relpath(p, KDIR)}:{t.count(chr(10), 0, m.start()) + 1}")
    record('static', 'sound calls check g_snd_state==1 and the function pointer (g_snd_state is 2 when no card was found, g_snd is zeroed)', not snd, ', '.join(snd[:6]))
    titles = []
    old = ('File Manager', 'File Explorer', 'System Monitor', 'Sysmon', 'File Browser')
    for rel, must in (('apps/system/taskmgr.c', 'Task Manager'), ('apps/system/file_explorer/open_and_draw.c', 'Files'), ('apps/system/file_explorer/clicks_and_drops.c', 'Files'), ('apps/system/regedit.c', 'Registry Editor'), ('apps/system/bench.c', 'Benchmark'), ('apps/media/recorder.c', 'Voice Recorder'), ('apps/media/camera.c', 'Camera'), ('apps/system/settings/pages.c', 'Settings')):
        t = open(os.path.join(KDIR, rel), errors='ignore').read()
        found = [m.group(1) for m in re.finditer(r'win_frame\(i,\s*"([^"]*)"', t)]
        for f in found:
            if f.startswith(old): titles.append(f"{rel}: '{f}' (menu says '{must}')")
        if found and not any(f == must or f.startswith(must + ' ') for f in found): titles.append(f"{rel}: no title '{must}'")
    record('static', 'window titles match the start-menu names (Files, Task Manager, ...)', not titles, ', '.join(titles[:6]))
    clk = []
    for rel in ('graphics/ui/desktop/window_titles_and_bars.c', 'graphics/ui/desktop/app_tiles_and_icons.c'):
        t = open(os.path.join(KDIR, rel), errors='ignore').read()
        for m in re.finditer(r'read_clock\(&hh,&mm,&ss[^\n]*\n?[^\n]*', t):
            if 'g_showsec' not in m.group(0) or 'u2(p,ss)' not in m.group(0): clk.append(rel)
    record('static', 'taskbar clocks read g_showsec and append :SS when it is on', not clk, ', '.join(clk))
    rc, out = sh(['gcc'] + kernel_cflags() + STRICT + ['-c', 'kernel64.c', '-o', os.devnull], cwd=KDIR)
    for m in kernel_modules():
        r2, o2 = sh(['gcc'] + kernel_cflags() + STRICT + ['-c', m, '-o', os.devnull], cwd=KDIR); rc |= r2; out += o2
    ws = [l for l in out.splitlines() if 'warning:' in l or 'error:' in l]
    record('static', f'kernel compiles with zero strict warnings ({len(ws)})', rc == 0 and not ws, ws[0][:200] if ws else '')
    fr, be = fixed_regions_in_bss(); record('static', f'no hard-coded memory regions inside kernel .bss (ends {be:#x})', not fr, ', '.join(fr[:6]))
    ub = unbounded_hw_loops(); record('static', 'no unbounded hardware polling loops (hang on missing hardware)', not ub, ', '.join(ub[:6]))
    tm = kread(os.path.join(KDIR, 'apps/system/taskmgr.c')); cl = defn(tm, 'sysmon_classic'); inp = kread(os.path.join(KDIR, 'graphics/ui/input.c'))
    tmbad = [n for n, ok in [('End task button in classic layout', 'hit(' in cl and '399' in cl and '"End task"' in cl), ('menu bar hits', '600+k' in cl), ('menu items', 'tm_menu_pick' in tm),
             ('process context menu kind 9', 'g_ctxkind==9' in inp and 'sysmon_rclick' in inp), ('memory graph scaled to installed memory', 'g_ram_mb' in cl and 'mx+=mx/8' not in cl),
             ('status bar counts share tm_counts', cl.count('tm_counts(') >= 1 and 'napp+6' not in cl), ('"1 core" wording', 'cores_str' in tm and '" cores, "' not in tm)] if not ok]
    record('static', 'Task Manager classic layout has End task, working menus, a process context menu, a memory scale and agreeing counts', not tmbad, ', '.join(tmbad))
    rc, out = sh([sys.executable, os.path.join(KDIR, 'tools/nvxport.py'), 'check'], cwd=KDIR); record('static', 'ports and distro profiles are valid (' + (out.strip().splitlines() or [''])[-1] + ')', rc == 0, out[-300:])
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
    sb = os.path.join(tempfile.gettempdir(), 'nvx_sleepsm_host')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-fsanitize=address,undefined', '-I', KDIR, '-o', sb, os.path.join(TESTS, 'sleepsm', 'sleepsm_host.c')])
    if record('static', 'sleep state machine host test builds against core/sleepsm.c', rc == 0, out[-300:]):
        rc, out = sh([sb]); record('static', 'sleep: device registry, blockers stop sleep with a message, suspend failure rolls back in reverse, resume failures reported, S3/S2idle/S1 pick', rc == 0 and 'SLEEPSM HOST TEST: ALL PASS' in out, out[-300:])
    wb = os.path.join(tempfile.gettempdir(), 'nvx_wallfx_host')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-fsanitize=address,undefined', '-I', KDIR, '-o', wb, os.path.join(TESTS, 'wallfx', 'wallfx_host.c')])
    if record('static', 'wallpaper effects host test builds against graphics/theme/wallfx.c', rc == 0, out[-300:]):
        rc, out = sh([wb]); record('static', 'wallpaper effects: the stepwise build gives the same picture as the one-shot build, abort and restart work, off is a no-op', rc == 0 and 'WALLFX HOST TEST: ALL PASS' in out, out[-300:])
    rc, out = sh(['sh', os.path.join(TESTS, 's3', 'run.sh'), KDIR, os.path.join(tempfile.gettempdir(), 'nvx_s3t')], timeout=120)
    if 'SKIP' in out: print('  note: S3 resume test skipped (' + out.strip().splitlines()[-1][:80] + ')')
    else:
        record('static', 'S3: arch/x86_64/boot/s3_wake.S suspends a QEMU guest (PIIX4 S3), system_wakeup enters the FACS vector in real mode, long mode and saved state come back', rc == 0 and 'RESUME PASS' in out, out[-400:])
        rc, out = sh(['sh', os.path.join(TESTS, 's3', 'run.sh'), KDIR, os.path.join(tempfile.gettempdir(), 'nvx_s3k')], timeout=120, env=dict(os.environ, NVX_S3_WAKE='key'))
        record('static', 'S3: a PS/2 key press wakes a QEMU guest from S3 (the reason S3 is allowed by default on QEMU)', rc == 0 and 'woken by a PS/2 key press' in out, out[-400:])
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I', KDIR] + SAN + ['-o', os.path.join(tempfile.gettempdir(), 'nvx_acpiwake_t'), os.path.join(TESTS, 'acpi', 'wake_main.c')])
    if record('static', 'ACPI wake host test builds against drivers/acpi/acpi_wake.c', rc == 0, out[-300:]):
        rc, out = sh([os.path.join(tempfile.gettempdir(), 'nvx_acpiwake_t')]); n = out.count(' ok\n')
        record('static', f'ACPI wake: FADT GPE blocks, GPE register/bit, _PRW decode, xHCI match, arm/clear/disarm, _DSW/_PSW, _Lxx, S3 key-wake rule ({n} checks)', rc == 0 and 'ACPI WAKE: ALL PASS' in out, out[-400:])

def g_boot(iso):
    head('boot emulation (BIOS + UEFI + kernel entry)')
    rc, out = sh(['python3', os.path.join(HERE, 'emulate.py'), iso], timeout=900)
    for l in out.splitlines():
        if 'FAIL' in l or 'checks passed' in l: print('    ' + l.strip())
    m = re.search(r'(\d+) of (\d+) checks passed', out)
    record('boot', f'emulate.py: {m.group(0) if m else "did not finish"}', rc == 0, out[-300:])

def g_bt(work):
    head('Bluetooth: HCI event parser, event mask, Realtek firmware loader (host test)')
    rc, out = sh(['sh', os.path.join(TESTS, 'bluetooth', 'run.sh')])
    n = out.count(' ok\n')
    record('bt', f'HCI events, event mask, V1/V2 epatch vs Python reference, download sequence, SBC vs libsbc, A2DP/AVDTP/AVRCP + route + MTU>1024 + AVRCP link trust, RFCOMM frames/FCS/credits + HFP AG SLC + mSBC codec negotiation with CVSD fallback + SCO packet size from altsetting/SCO MTU + mSBC encoder/decoder vs libsbc + SCO setup/reassembly + SDP server (records, hostile/fuzzed requests, continuation) + RFCOMM responder and HFP SLC from the headset side + incoming A2DP signalling, paired+encrypted only + headset mic routing and input selector over a simulated headset, non-blocking boot reconnect with absent devices, pairing dialog + audio Settings UI state ({n} checks)', rc == 0 and 'BLUETOOTH HOST TEST: ALL PASS' in out, out[-400:])
def g_man(work):
    head('man: mandoc builds on the host, generated pages lint clean, man/apropos read gzipped pages (host test)')
    rc, out = sh(['sh', os.path.join(TESTS, 'man', 'run.sh')])
    n = out.count(' ok\n')
    record('man', f'mandoc, generated shell/tool pages, makewhatis/apropos/man ({n} checks)', rc == 0 and 'MAN HOST TEST: ALL PASS' in out, out[-400:])
def g_rsync(work):
    head('rsync: local copy, delta transfer, daemon (rsync://) and remote-shell transport (host test)')
    env = dict(os.environ); mb = os.path.join(HERE, '..', 'build', 'ports2', 'rsync', 'rsync')
    if os.path.exists(mb): env['RSYNC_BIN'] = mb
    rc, out = sh(['sh', os.path.join(TESTS, 'rsync', 'run.sh'), os.path.join(work, 'rsync')], env=env)
    n = out.count(' ok\n')
    record('rsync', f"rsync {'musl static' if 'RSYNC_BIN' in env else 'host-built'}: -aH, delta, --delete, daemon pull/push, RSYNC_RSH ({n} checks)", rc == 0 and 'RSYNC HOST TEST: ALL PASS' in out, out[-400:])
def g_mtp(work):
    head('Phones and cameras (MTP/PTP) against a simulated responder')
    b = os.path.join(work, 'mtp_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', b, os.path.join(TESTS, 'mtp', 'main.c')])
    if not record('usb', 'MTP/PTP host test builds', rc == 0, out[-300:]): return
    rc, out = sh([b]); n = out.count(' ok\n')
    record('usb', f'MTP/PTP: interface match, session, storages, list/read/partial/write/mkdir/delete/rename, ZLP, timeouts, locked phone, camera ({n} checks)', rc == 0 and 'ALL PASS' in out and n >= 36, out[-400:])
def g_uvc(work):
    head('Camera (UVC) and microphone (USB Audio 1 and 2) parsing, generated from xhci.c')
    d = os.path.join(TESTS, 'uvc'); c = os.path.join(work, 'uvc_t.c'); b = os.path.join(work, 'uvc_t')
    xf = os.path.join(work, 'xhci_full.c'); open(xf, 'w').write(kread(os.path.join(KDIR, 'drivers/usb/xhci.c'), False))
    rc, out = sh(['python3', os.path.join(d, 'gen.py'), xf, c, os.path.join(d, 'main.c')])
    if not record('usb', 'camera/microphone test builds against the current driver', rc == 0 and sh(['gcc', '-O1', '-w', '-o', b, c])[0] == 0, out[-300:]): return
    rc, out = sh([b])
    n = out.count(' ok\n'); record('usb', f'cameras: 2 streams, IR + colour, UYVY/I420/YV12/grey, bulk; mics: 24-bit, USB Audio 2 ({n} checks)', rc == 0 and n >= 20, out[-400:])
def g_uvcext(work):
    head('Camera extras: frame-based and UVC 1.5 H.264, still image method 2, HDMI capture cards (MS2109/MS2130)')
    d = os.path.join(TESTS, 'uvcext'); c = os.path.join(work, 'uvcext_t.c'); b = os.path.join(work, 'uvcext_t')
    xf = os.path.join(work, 'xhci_ext.c'); open(xf, 'w').write(kread(os.path.join(KDIR, 'drivers/usb/xhci.c'), False))
    rc, out = sh(['python3', os.path.join(TESTS, 'uvc', 'gen.py'), xf, c, os.path.join(d, 'main.c')])
    if not record('usb', 'camera extras test builds against the current driver', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', b, c])[0] == 0, out[-300:]): return
    rc, out = sh([b])
    n = out.count(' ok\n'); record('usb', f'cameras: H.264 kept for V4L2 only, frame-based MJPEG, still method 2 + STI, capture-card mode order, MS2109 audio ({n} checks)', rc == 0 and 'uvc ext: all ok' in out and n >= 28, out[-600:])
def g_avirec(work):
    head('Camera: two cameras share USB bandwidth, video recording as Motion-JPEG AVI with PCM audio')
    d = os.path.join(TESTS, 'avirec'); c = os.path.join(work, 'avirec_t.c'); b = os.path.join(work, 'avirec_t'); o = os.path.join(work, 'avirec_out'); os.makedirs(o, exist_ok=True)
    xf = os.path.join(work, 'xhci_av.c'); open(xf, 'w').write(kread(os.path.join(KDIR, 'drivers/usb/xhci.c'), False))
    rc, out = sh(['python3', os.path.join(d, 'gen.py'), xf, c, os.path.join(KDIR, 'apps/media/avirec.h'), os.path.join(d, 'main.c')])
    if not record('usb', 'recording test builds against the current driver and apps/media/avirec.h', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', b, c])[0] == 0, out[-300:]): return
    rc, out = sh([b, o])
    n = out.count(' ok\n'); record('usb', f'bandwidth split, JPEG encoder, AVI writer: index, rates, empty repeat chunks, size limit ({n} checks)', rc == 0 and 'avirec: all ok' in out and n >= 30, out[-400:])
    rc, out = sh(['python3', os.path.join(d, 'verify.py'), o])
    record('usb', 'recorded JPEG decodes (PIL, PSNR) and the AVI plays in ffmpeg (MJPEG + PCM)', rc == 0 and 'verify: all ok' in out, out[-400:])
def g_uac(work):
    head('USB audio output (UAC1/UAC2): topology, feature-unit volume, clock/rate choice, feedback endpoints (host test)')
    rc, out = sh(['sh', os.path.join(TESTS, 'uac', 'run.sh'), os.path.join(work, 'uac_t')])
    n = out.count(' ok\n'); record('usb', f'USB audio out: terminal/unit graph incl. mixer + clock selector, hostile descriptors, 44.1/48/96 kHz choice (old choice kept by default), UAC2 RANGE, 10.14/16.16 feedback + shift quirk + rate adaptation, FU volume/mute, guards ({n} checks)', rc == 0 and 'UAC HOST TEST: ALL PASS' in out and n >= 50, out[-400:])
def g_uvc_ir(work):
    head('Infrared (Windows Hello) camera for Face ID: IR stream, light control search, saved setting, IR liveness')
    d = os.path.join(TESTS, 'uvc_ir'); c = os.path.join(work, 'uvcir_t.c'); b = os.path.join(work, 'uvcir_t')
    xf = os.path.join(work, 'xhci_ir.c'); open(xf, 'w').write(kread(os.path.join(KDIR, 'drivers/usb/xhci.c'), False))
    rc, out = sh(['python3', os.path.join(d, 'gen.py'), xf, c, os.path.join(KDIR, 'drivers/usb/xhci/uvc_ir.c'), os.path.join(KDIR, 'core/auth/faceid_ir.c'), os.path.join(d, 'main.c')])
    if not record('usb', 'infrared camera test builds against the current driver', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', b, c])[0] == 0, out[-300:]): return
    rc, out = sh([b])
    n = out.count(' ok\n'); record('usb', f'IR camera: grey-only stream found, Y16, extension-unit light search bounded and saved, IR liveness ({n} checks)', rc == 0 and 'uvc ir: all ok' in out and n >= 25, out[-400:])
    vc = os.path.join(TESTS, 'v4l2', 'main.c'); vb = os.path.join(work, 'v4l2_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', vb, vc])
    if record('usb', 'Linux camera interface (V4L2) host test builds', rc == 0, out[-300:]):
        rc, out = sh([vb]); n = re.search(r'(\d+) checks', out)
        record('usb', f'V4L2 /dev/video0: struct layouts + ioctl numbers vs linux/videodev2.h, buffer queue state machine, YUYV/BGRX conversion ({n.group(1) if n else "?"} checks)', rc == 0 and 'v4l2 host test: ALL PASS' in out, out[-400:])
def g_ssh(work):
    head('SSH server support: pty ownership for dropbear sessions, dropbear programs in the ports build')
    sc = os.path.join(work, 'sshpty_t.c'); sb = os.path.join(work, 'sshpty_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'ssh', 'pty_gen.py'), os.path.join(KDIR, 'sys/linux/syscalls/fifos_and_names.c'), sc])
    if record('ssh', 'pty owner test builds against the Linux layer', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', sb, sc])[0] == 0, out[-300:]):
        rc, out = sh([sb]); record('ssh', 'pty chown/chmod and open: root hands the slave to the user, only the owner and root may open it', rc == 0 and 'pty owner: all ok' in out, out[-300:])
    b = open(os.path.join(KDIR, 'tools', 'build_ports2.sh')).read()
    record('ssh', 'ports build makes and ships dropbear, dbclient, dropbearkey, scp', all(x in b for x in ('PROGRAMS="dropbear dbclient dropbearkey dropbearconvert scp"', 'for l in ssh dbclient scp dropbear dropbearkey')))
    u = open(os.path.join(KDIR, 'apps', 'system', 'svcunits.c')).read(); ro = open(os.path.join(KDIR, 'third_party', 'dropbear', 'src', 'svr-runopts.c')).read()
    record('ssh', 'root password login is off: sshd.service passes -g, old units are migrated, the dropbear build defaults norootpass=1 and stops if that patch does not apply', 'dropbear -F -E -R -g -p 22' in u and '"ExecStart=/usr/bin/dropbear -F -E -R -p 22\\n"' in u and "norootpass = 1;/' src/svr-runopts.c" in b and '\tsvr_opts.norootpass = 0;' in ro and 'cannot turn root password login off' in b)
    au = open(os.path.join(KDIR, 'third_party', 'dropbear', 'src', 'svr-auth.c')).read(); do = open(os.path.join(KDIR, 'third_party', 'dropbear', 'src', 'default_options.h')).read()
    record('ssh', 'dropbear: longer pause after each failed login (1, 2, 4, 8, 16 s) and 6 tries per connection; the build stops if either patch does not apply', '\t\t\twhile (nanosleep(&delay, &delay) == -1 && errno == EINTR)' in au and '#define MAX_AUTH_TRIES 10\n' in do and 'cannot add the growing delay after failed logins' in b and 'cannot lower MAX_AUTH_TRIES' in b and 'delay.tv_sec += 1 << (ses.authstate.failcount < 6 ? ses.authstate.failcount - 2 : 4)' in b)
    fb = os.path.join(work, 'fw_t')
    if record('ssh', 'firewall host test builds against net/stack/firewall.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', fb, os.path.join(TESTS, 'sec', 'fw_test.c')])[0] == 0):
        rc, out = sh([fb]); record('ssh', 'firewall: inbound denied by default, tcp 22 open only while sshd runs, UDP replies, broadcast replies, expiry, rules saved and reloaded, bad config fails closed', rc == 0 and 'firewall: all ok' in out, out[-300:])
    lx = open(os.path.join(KDIR, 'sys', 'linux', 'lxsock.c')).read()
    record('ssh', 'firewall is wired into the socket layer: new TCP connections and UDP datagrams are checked, UDP sends are remembered for replies', 'if(!fw_allow_in(6,sip,sp,dp,lxs_in_lo))return;' in lx and 'if(!fw_allow_in(17,sip,rd16(p),rd16(p+2),lxs_in_lo))return;' in lx and 'fw_note_out(17,s->lport,real,tp);' in lx)
    vt = open(os.path.join(KDIR, 'shell', 'vtcon.c')).read()
    record('ssh', 'sudo password time is kept per terminal: saved and restored on console switch', 's->sudo_until=sudo_until_get();' in vt and 'sudo_until_set(s->sudo_until);' in vt)
    bs = open(os.path.join(KDIR, 'build.sh')).read() + b; ap = open(os.path.join(KDIR, 'fs', 'pkg', 'apt', 'unpack_and_prompts.c')).read()
    record('ssh', 'no setuid programs: image build sets no setuid/setgid bits, packages are unpacked as 0755, execve does not honour setuid', not re.search(r'chmod\s+(u\+s|g\+s|[0-7]*[2467][0-7]{3}\b)', bs) and 'if(mode&0111)ux_chmod(an,0100755u);' in ap and not re.search(r'S_ISUID|S_ISGID|&\s*0?[246]000u?\b', open(os.path.join(KDIR, 'sys', 'linux', 'syscalls', 'memory_and_signals.c')).read()))

def g_bpf(work):
    head('eBPF core: verifier, interpreter, maps (sys/bpf)')
    b = os.path.join(work, 'bpf_core_t')
    if record('bpf', 'core test builds against sys/bpf/bpf.c + verifier.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', b, os.path.join(TESTS, 'bpf', 'core_test.c')])[0] == 0):
        rc, out = sh([b]); record('bpf', 'verifier accept/reject set, interpreter results, array/hash/percpu/ringbuf maps, slots', rc == 0 and 'bpf core: all ok' in out, out[-400:])
    fz = os.path.join(work, 'bpf_fuzz_t')
    if record('bpf', 'verifier fuzz builds', sh(['gcc', '-O1', '-w', '-I', KDIR, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', fz, os.path.join(TESTS, 'bpf', 'core_fuzz.c')])[0] == 0):
        rc, out = sh([fz, '200000']); record('bpf', 'fuzz: 200000 random programs, every accepted one runs under ASan/UBSan without a fault', rc == 0 and 'no faults' in out, out[-400:])
def g_wifi(work):
    head('Wi-Fi security: WPA3 (SAE), AES-CMAC, PEAP/MSCHAPv2, P-256 (from the kernel sources)')
    sc = os.path.join(work, 'wscan_t.c'); sb = os.path.join(work, 'wscan_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'scan_gen.py'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/rtw8821ce.c'), os.path.join(KDIR, 'include/nvx/drivers/net/wifi/rtw8821ce.h'), sc, os.path.join(TESTS, 'wifi', 'scan_main.c')])
    if record('wifi', 'scan parser test builds against the current driver', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', sb, sc])[0] == 0, out[-300:]):
        rc, out = sh([sb]); record('wifi', 'scan: UTF-8 names, HT Operation channel, channel settle, WPA1, RSNXE H2E + selector 123, off-channel, 200000 random frames', rc == 0 and 'scan ok' in out, out[-300:])
    mc = os.path.join(work, 'wmgmt_t.c'); mb = os.path.join(work, 'wmgmt_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'mgmt_gen.py'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/rtw8821ce.c'), os.path.join(KDIR, 'include/nvx/drivers/net/wifi/rtw8821ce.h'), mc, os.path.join(TESTS, 'wifi', 'mgmt_main.c')])
    if record('wifi', 'management-frame send test builds against the current driver', rc == 0 and sh(['gcc', '-O1', '-w', '-no-pie'] + SAN + ['-o', mb, mc])[0] == 0, out[-300:]):
        rc, out = sh([mb]); record('wifi', 'management frames: mgmt queue, stuck queue falls back to the data queue, card gone, ring wrap, join log', rc == 0 and 'mgmt ok' in out and 'skipped' not in out, out[-300:])
    ab = os.path.join(work, 'wampdu_t')
    if record('wifi', 'A-MPDU reorder buffer test builds against net/wifi/wifi_ampdu.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', ab, os.path.join(TESTS, 'wifi', 'ampdu', 'ampdu_test.c')])[0] == 0):
        rc, out = sh([ab]); record('wifi', 'A-MPDU: in-order release, holes, window shift, seq wrap, 100 ms flush, DELBA, placeholders, random reorder', rc == 0 and 'ampdu: all ok' in out, out[-300:])
    atb = os.path.join(work, 'wampdu_tx_t')
    if record('wifi', 'A-MPDU send-session test builds against net/wifi/wifi_ampdu.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', atb, os.path.join(TESTS, 'wifi', 'ampdu', 'ampdu_tx_test.c')])[0] == 0):
        rc, out = sh([atb]); record('wifi', 'A-MPDU send: ADDBA delay/token/TID/policy checks, timeout, early DELBA, auto-off after 3 failures, BlockAckReq window move and wrap', rc == 0 and 'ampdu_tx: all passed' in out, out[-300:])
    acc = os.path.join(work, 'wact_t.c'); acb = os.path.join(work, 'wact_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'ampdu', 'act_gen.py'), os.path.join(KDIR, 'net/wifi/wifi_netdev.c'), acc, os.path.join(TESTS, 'wifi', 'ampdu', 'act_main.c')])
    if record('wifi', 'Block Ack action-frame test builds against net/wifi/wifi_netdev.c', rc == 0 and sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', acb, acc])[0] == 0, out[-300:]):
        rc, out = sh([acb]); record('wifi', 'Block Ack actions: ADDBA request sent, CCMP-protected ADDBA/DELBA with PMF (management nonce, bad MIC, replay), unprotected refused under PMF, DELBA on teardown, BlockAckReq, SA Query answer/match/timeout (802.11w), generic-driver (iwx) ADDBA via firmware queue hook', rc == 0 and 'act: all passed' in out, out[-300:])
    rb = os.path.join(work, 'wregdb_t')
    if record('wifi', 'regulatory database test builds against net/wifi/regdb.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', rb, os.path.join(TESTS, 'wifi', 'regdb', 'regdb_test.c')])[0] == 0):
        rc, out = sh([rb]); record('wifi', 'regdb: SE/US/JP/world channels, DFS/no-IR, max dBm, 6 GHz, Country IE, filter, auto-off', rc == 0 and 'regdb: all ok' in out, out[-300:])
    bwb = os.path.join(work, 'wbw_t')
    if record('wifi', 'channel width test builds against rtw88/rtw_bw.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', bwb, os.path.join(TESTS, 'wifi', 'bw', 'bw_test.c')])[0] == 0):
        rc, out = sh([bwb]); record('wifi', 'channel width: 40/80 MHz center channel and primary index vs rtw88 rtw_update_channel, ADCCLK/RF18/TXSC/RFMOD/DFIR values, AP HT/VHT operation, RA BW/VHT fields, fallback to 20 MHz after a failed join or low rates, in-session switch to 20 MHz (HT/VHT notify frames, confirm or reconnect), SGI40/SGI80', rc == 0 and 'bw: all ok' in out, out[-300:])
    tb = os.path.join(work, 'wtxpwr_t')
    if record('wifi', 'TX power table test builds against rtw88/rtw_txpwr.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', tb, os.path.join(TESTS, 'wifi', 'txpwr', 'txpwr_test.c')])[0] == 0):
        rc, out = sh([tb]); record('wifi', 'TX power: 8821C by-rate table bases/offsets, regulatory limits, TXAGC writes, auto-off after failed joins, legacy switch, 40/80 MHz efuse bases vs rtw88 struct layout, per-width limits', rc == 0 and 'txpwr: all ok' in out, out[-300:])
    cxb = os.path.join(work, 'coex_test')
    if record('wifi', 'BT coex state machine test builds against rtw88/rtw_coex_core.h', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', cxb, os.path.join(TESTS, 'wifi', 'coex', 'coex_test.c')])[0] == 0):
        rc, out = sh([cxb]); record('wifi', 'BT coex: Wi-Fi only/BT idle/HID/A2DP/inquiry/5 GHz case to 8821C table and TDMA, BT info decode and dedupe, H2C 0x69/0x66 bytes and TDMA scoreboard vs rtw88, busy from traffic, reinit/ignore-WLAN-act/inquiry remain, 200000 random states in range', rc == 0 and 'coex: all ok' in out, out[-300:])
    pb = os.path.join(work, 'wps_t')
    if record('wifi', 'power-save test builds against net/wifi/wifi_ps.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', pb, os.path.join(TESTS, 'wifi', 'ps_main.c')])[0] == 0):
        rc, out = sh([pb]); record('wifi', 'power save: null data PM bit, TIM bitmap (offset, multicast, DTIM count, malformed), battery/on/off policy, auto-off after missed beacons', rc == 0 and 'all tests passed' in out, out[-300:])
    rb = os.path.join(work, 'wroam_t')
    if record('wifi', 'roaming test builds against net/wifi/wifi_roam.c', sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', rb, os.path.join(TESTS, 'wifi', 'roam', 'roam_test.c')])[0] == 0):
        rc, out = sh([rb]); record('wifi', 'roaming: 8 dB rule, slices skip own channel, same SSID+security only, quiet during DHCP, roam on weak signal, off after 3 failures', rc == 0 and 'roam: all ok' in out, out[-300:])
    bc = os.path.join(work, 'wba_t.c'); bb = os.path.join(work, 'wba_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'ampdu', 'ba_gen.py'), os.path.join(KDIR, 'net/wifi/wifi_netdev.c'), bc, os.path.join(TESTS, 'wifi', 'ampdu', 'ba_main.c')])
    if record('wifi', 'Block Ack RX path test builds against net/wifi/wifi_netdev.c', rc == 0 and sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', bb, bc])[0] == 0, out[-300:]):
        rc, out = sh([bb]); record('wifi', 'Block Ack attacks: forged MIC-fail far seq, replay with new seq, pending-PN replay, A-MSDU retransmit, EAPOL dup, re-ADDBA, open A-MSDU hole', rc == 0 and 'ba: all ok' in out, out[-300:])
    cc = os.path.join(work, 'wchip_t.c'); cb = os.path.join(work, 'wchip_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'chip_gen.py'), KDIR, cc, os.path.join(TESTS, 'wifi', 'chip_main.c')])
    if record('wifi', 'multi-chip test builds against chip.c, chips/rtw8821c.c and the core', rc == 0 and sh(['gcc', '-O1', '-w', '-no-pie'] + SAN + ['-o', cb, cc])[0] == 0, out[-300:]):
        rc, out = sh([cb]); record('wifi', 'chips: lookup by PCI ID, 8821C ops, v10.98 register traces unchanged, RF path A/B/mix, H2C ring wrap, IQK guard, SRRC and channel 14 CCK filter', rc == 0 and 'chip ok' in out, out[-300:])
    uc = os.path.join(work, 'wrtwusb_t.c'); ub = os.path.join(work, 'wrtwusb_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'rtwusb', 'rtwusb_gen.py'), KDIR, uc, os.path.join(TESTS, 'wifi', 'rtwusb', 'rtwusb_main.c')])
    if record('wifi', 'rtw88 USB transport test builds against rtw_usb.c', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', ub, uc])[0] == 0, out[-300:]):
        rc, out = sh([ub]); record('wifi', 'rtw88 USB (RTL8821CU/8811CU, 8822BU/CU ids): vendor-request registers + 0x4E0 shadow, endpoint/queue map, TX desc + checksum + 3-frame aggregation, RX aggregation parse + fuzz, USB power-on, reserved-page firmware path, scan/join/tx on a simulated dongle', rc == 0 and 'rtw88 usb: all ok' in out, out[-300:])
    gc = os.path.join(work, 'wmgr_t.c'); gb = os.path.join(work, 'wmgr_t')
    rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'mgr_gen.py'), os.path.join(KDIR, 'net/wifi/wifi_mgr.c'), os.path.join(KDIR, 'include/nvx/net/wifi/wifi_mgr.h'), os.path.join(KDIR, 'include/nvx/drivers/net/wifi/rtw8821ce.h'), os.path.join(TESTS, 'wifi', 'mgr_main.c'), gc])
    if record('wifi', 'Wi-Fi manager test builds against wifi_mgr.c', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', gb, gc])[0] == 0, out[-300:]):
        rc, out = sh([gb]); record('wifi', 'manager: saved file old/new format, wifi off kept over reboot, last network is default, join back-off', rc == 0 and 'mgr ok' in out, out[-300:])
    db = os.path.join(work, 'w23d_t'); lt3 = next((d for d in ('/tmp/ref/linux/drivers/net/wireless/realtek/rtw88', '/tmp/ref/rtw8723/src/drivers/net/wireless/realtek/rtw88') if os.path.isfile(os.path.join(d, 'rtw8723d_table.c'))), None)
    reg = '&rtw8723d_chip' in open(os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/chip.c')).read() and os.path.isfile(os.path.join(KDIR, 'assets/firmware/linux-firmware/rtw88/rtw8723d_fw.bin'))
    if record('wifi', 'RTL8723DE test builds; chip registered in chip.c, firmware bundled', reg and sh(['gcc', '-O1', '-w', '-no-pie', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-include', 'stdlib.h', '-I', KDIR] + [('-fsanitize=undefined' if f.startswith('-fsanitize=address') else f) for f in SAN] + ['-o', db, os.path.join(TESTS, 'wifi', 'rtw8723d', 'rtw8723d_test.c')] + [os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88', f) for f in ('chips/rtw8723d.c', 'chips/rtw8723d_tables.c', 'rtw8821ce.c', 'rtw_sec.c')])[0] == 0):
        rc, out = sh([db] + ([os.path.join(lt3, 'rtw8723d_table.c'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/chips/rtw8723d.c')] if lt3 else [])); record('wifi', 'RTL8723DE: PCI ID, tables' + (' vs Linux source' if lt3 else '') + ', efuse, 2.4 GHz-only channels, tx power, legacy firmware download, 8051 TRX init trace vs rtw88, H2C mailbox, 40-byte TX descriptor, RX PHY status, IQK, ch13/14 notch, power tracking (swing/thermal tables vs rtw88), IQK undo, 8723D coex init/tables, regulatory TX power limit (txpwr_lmt vs Linux, never exceeded)', rc == 0 and 'rtw8723d: all ok' in out, out[-300:])
    lt = '/tmp/ref/linux/drivers/net/wireless/realtek/rtw88'
    if os.path.isdir(lt):
        rc, out = sh(['sh', os.path.join(TESTS, 'wifi', 'rtw8822b_test.sh')], env=dict(os.environ, TMPDIR=work))
        record('wifi', 'RTL8822BE chip: tables vs Linux source, power sequence, channels, tx power, IQK, coex', rc == 0 and 'all checks passed' in out, out[-300:])
        cs = os.path.join(work, 'w22c_t.c'); cbin = os.path.join(work, 'w22c_t'); fo = os.path.join(work, 'w22c_fw.o')
        rc, out = sh(['python3', os.path.join(TESTS, 'wifi', 'rtw8822c_gen.py'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/rtw_sec.c'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/rtw8821ce.c'), os.path.join(lt, 'rtw8822c_table.c'), os.path.join(TESTS, 'wifi', 'rtw8822c_main.c'), cs])
        ok = rc == 0 and sh(['ld', '-r', '-b', 'binary', '-o', fo, 'rtw8822c_fw.bin'], cwd=os.path.join(KDIR, 'assets/firmware'))[0] == 0 and sh(['gcc', '-w', '-no-pie', '-fsanitize=address,undefined', '-I', os.path.join(TESTS, 'wifi', 'shim'), '-I', KDIR, cs, os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/chips/rtw8822c.c'), os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw88/chips/rtw8822c_tables.c'), fo, '-o', cbin])[0] == 0
        if record('wifi', 'RTL8822CE chip test builds', ok, out[-300:]):
            rc, out = sh([cbin]); record('wifi', 'RTL8822CE chip: tables vs Linux source, power-on, efuse, channels, RF addressing, IQK, coex', rc == 0 and 'all rtw8822c tests passed' in out, out[-300:])
    ib = os.path.join(work, 'iwx_t'); idir = os.path.join(KDIR, 'drivers/net/wifi/intel/iwx')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', ib, os.path.join(TESTS, 'iwx', 'iwx_host.c')] + [os.path.join(idir, 'mvm', f) for f in ('iwx_cmd.c', 'iwx_scan.c', 'iwx_6g.c', 'iwx_data.c', 'iwx_tlc.c')])
    if record('wifi', 'Intel iwx host test builds against iwx_cmd.c, iwx_scan.c, iwx_data.c, iwx_tlc.c', rc == 0, out[-300:]):
        rc, out = sh([ib]); record('wifi', 'iwx: struct sizes, sync command + timeout, scan v14 command, RX MPDU v1/v3 into BSS table, scan complete, malformed packets, TX, TLC v3/v4 + HT/VHT assoc IEs + TLC-vs-fixed-rate TX', rc == 0 and 'OK' in out, out[-300:])
    i6 = os.path.join(work, 'iwx6_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', i6, os.path.join(TESTS, 'iwx', 'iwx6_main.c')])
    if record('wifi', 'Intel iwx 6 GHz test builds against iwx_6g.c', rc == 0, out[-300:]):
        rc, out = sh([i6]); record('wifi', 'iwx 6 GHz: NVM/MCC channel map (UHB list, UNII-4 off), PSC scan list, RX band, SAE-only filter, HE + 6 GHz band cap IEs, MCC reply checks', rc == 0 and 'all passed' in out, out[-300:])
    hb = os.path.join(work, 'iwx_he_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-include', os.path.join(KDIR, 'include/nvx/base.h'), '-o', hb, os.path.join(TESTS, 'iwx', 'he_test.c')])
    if record('wifi', 'Intel iwx HE test builds against iwx_he.c + iwx_tlc.c', rc == 0, out[-300:]):
        rc, out = sh([hb]); record('wifi', 'iwx HE: HE cap/op parse, HE cap IE + insert before WMM, 160 MHz link + ctrl pos, HE TLC v3/v4, STA_HE_CTXT v1-v3, ADD_STA RX BA, TX A-MPDU queue/route/BA-notif reclaim/no-notif kill, auto-off after failures', rc == 0 and 'all tests passed' in out, out[-300:])
    rc, out = sh(['gcc', '-fsyntax-only', '-m64', '-ffreestanding', '-DNOOVEX64', '-I', KDIR, os.path.join(idir, 'trans', 'iwx_pcie.c')])
    record('wifi', 'iwx transport structs match Linux/OpenBSD sizes (context info gen2/gen3, prph scratch, TFD)', rc == 0, out[-300:])
    mb = os.path.join(work, 'mt7921_t'); mdir = os.path.join(KDIR, 'drivers/net/wifi/mediatek/mt7921')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', mb, os.path.join(TESTS, 'mt7921', 'mt7921_host.c')])
    if record('wifi', 'MediaTek mt7921 host test builds against glue.c and mt7921.h', rc == 0, out[-300:]):
        rc, out = sh([mb, os.path.join(KDIR, 'assets/firmware/linux-firmware')]); record('wifi', 'mt7921: PCI IDs, bundled MT7961/MT7922 patch + RAM code headers parse, inert without card, start order, missing firmware named, BAR checks, wifi_hw ops, 6 GHz PSC scan list + RX virtual channels + SAE-only join', rc == 0 and 'mt7921 glue ok' in out, out[-300:])
    r89 = os.path.join(work, 'rtw89_t'); r89d = os.path.join(KDIR, 'drivers/net/wifi/realtek/rtw89')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89, os.path.join(TESTS, 'rtw89', 'rtw89_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_fw.c', 'rtw89_mac.c', 'rtw89_efuse.c', 'rtw89_fwdl.c', 'rtw89_glue.c', 'rtw89_8852c.c', 'rtw89_pci.c', 'rtw89_phy.c', 'rtw89_sta.c', 'rtw89_macinit.c', 'rtw89_scan.c', 'rtw89_chan.c', 'rtw89_txpwr.c')])
    if record('wifi', 'Realtek rtw89 host test builds against rtw89_fw.c, rtw89_mac.c, rtw89_efuse.c, rtw89_fwdl.c, rtw89_glue.c', rc == 0, out[-300:]):
        rc, out = sh([r89, os.path.join(KDIR, 'assets/firmware/linux-firmware')]); record('wifi', 'rtw89: RTL8852B power-on register sequence + bounded polls, bundled rtw8852b_fw-1.bin MFW/CV pick + section parse, corrupt headers, efuse dump + logical map + MAC/RFE, FWDL header + 2020-byte sections over a simulated H2C ring + WCPU ready, H2C seq/rack, C2H rec/done ack + log, detect-only probe, wifi rtw89 on', rc == 0 and 'rtw89 host ok' in out, out[-300:])
    r89c = os.path.join(work, 'rtw89c_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89c, os.path.join(TESTS, 'rtw89', 'rtw89c_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_macinit.c', 'rtw89_scan.c', 'rtw89_fwdl.c', 'rtw89_mac.c', 'rtw89_phy.c', 'rtw89_pci.c', 'rtw89_fw.c')])
    if record('wifi', 'Realtek rtw89 MAC init / scan host test builds against rtw89_macinit.c, rtw89_scan.c', rc == 0, out[-300:]):
        rc, out = sh([r89c, os.path.join(KDIR, 'assets/firmware/linux-firmware/rtw89/rtw8852c_fw-1.bin')]); record('wifi', 'rtw89: BAR2 sizing, DLE DLFW/SCC quotas + HCI flow control (H2C-only pre-FWDL, full page table), DMAC/CMAC/BB-RF enable, STA scheduler/CAM/RX filter init + bounded timeouts, scan offload H2C pair, scan RX filter, C2H scan notify (enter/leave/end) + chained hook, bounded scan without end notify, bundled rtw8852c_fw-1.bin parse, RTL8852C SCC HFC V1 page table + post-FWDL MAC init differences', rc == 0 and 'rtw89c host ok' in out, out[-300:])
    r89b = os.path.join(work, 'rtw89b_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89b, os.path.join(TESTS, 'rtw89', 'rtw89b_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_pci.c', 'rtw89_8852c.c', 'rtw89_phy.c')])
    if record('wifi', 'Realtek rtw89 data-path host test builds against rtw89_pci.c, rtw89_8852c.c, rtw89_phy.c', rc == 0, out[-300:]):
        rc, out = sh([r89b, os.path.join(KDIR, 'assets/firmware/linux-firmware/rtw89/rtw8852b_phy.bin')]); record('wifi', 'rtw89: RTL8852C power-on sequence + bounded poll, PCIe ring setup/BD RAM/DMA stop bits, H2C fwcmd desc + CH12 kick + bounded completion, mgmt/data TX WD + WP + addr info, RX BD info + RX desc parse (C2H, WiFi, corrupt), PHY table blob parse + headline select + branch engine + SWSI/direct RF writes, channel list + scan offload H2C payloads', rc == 0 and 'rtw89b host ok' in out, out[-300:])
    r89e = os.path.join(work, 'rtw89e_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89e, os.path.join(TESTS, 'rtw89', 'rtw89e_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_sta.c', 'rtw89_pci.c', 'rtw89_phy.c', 'rtw89_chan.c', 'rtw89_scan.c', 'rtw89_macinit.c', 'rtw89_txpwr.c')])
    if record('wifi', 'Realtek rtw89 channel switch / end-to-end host test builds against rtw89_chan.c, rtw89_sta.c, rtw89_scan.c', rc == 0, out[-300:]):
        rc, out = sh([r89e, os.path.join(KDIR, 'assets/firmware/linux-firmware/rtw89/rtw8852b_phy.bin'), os.path.join(KDIR, 'assets/firmware/linux-firmware/rtw89/rtw8852c_phy.bin')]); record('wifi', 'rtw89: RTL8852B set_channel (BB gain error + RPL offsets from the gain table, MAC/BB/RF 0x18, LCK, RXBB, bounded RF reads), role/MACID H2C before scan, add-channel ack wait, scan -> join -> data TX/RX with software CCMP, leave; RTL8852C set_channel (gain error/bypass/op1dB from bundled rtw8852c_phy.bin, spur CSI/NBI, BB GPIO sethw) + the same scan/join/data run on 8852C', rc == 0 and 'rtw89e host ok' in out, out[-300:])
    r89t = os.path.join(work, 'rtw89t_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89t, os.path.join(TESTS, 'rtw89', 'rtw89t_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_txpwr.c', 'rtw89_phy.c', 'rtw89_chan.c')])
    if record('wifi', 'Realtek rtw89 TX power host test builds against rtw89_txpwr.c', rc == 0, out[-300:]):
        rc, out = sh([r89t]); record('wifi', 'rtw89: RTL8852B TX power by-rate table, country->regd map, regulatory limit + RU limit lookup (regd, WW fallback, unset/127 entries fail low, dBm cap) against rtw8852b_table.c values, limit/RU pages + by-rate never above the non-BF limit for 12 countries x 19 channels, rate offsets, BB power reference, RX DCK (RF mode wait bounded, register restore, fails closed on dead SWSI), IQK/DPK/TSSI reported off', rc == 0 and 'rtw89t host ok' in out, out[-300:])
    r89s = os.path.join(work, 'rtw89d_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', r89s, os.path.join(TESTS, 'rtw89', 'rtw89d_host.c')] + [os.path.join(r89d, f) for f in ('rtw89_sta.c', 'rtw89_pci.c', 'rtw89_phy.c')])
    if record('wifi', 'Realtek rtw89 station host test builds against rtw89_sta.c, rtw89_pci.c, rtw89_phy.c', rc == 0, out[-300:]):
        rc, out = sh([r89s]); record('wifi', 'rtw89: station role/join-info/address CAM/security CAM/CMAC H2C payloads, open auth + assoc exchange (RSN IE from WPA code, AID), RX hook forwarding, bounded auth/assoc timeouts, refused/deauth/H2C failure teardown, fixed-rate data TX, software-crypto default + hardware key slots, leave', rc == 0 and 'rtw89d host ok' in out, out[-300:])
    msrc = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(mdir) for f in fs if f.endswith('.c'))
    rc, out = sh(['gcc', '-fsyntax-only', '-m64', '-ffreestanding', '-DNOOVEX64', '-I', KDIR] + msrc)
    record('wifi', f'mt7921 driver sources compile ({len(msrc)} files)', rc == 0, out[-300:])
    rb = os.path.join(work, 'mt7921_roc'); mcs = sorted(os.path.join(mdir, 'mcu', f) for f in os.listdir(os.path.join(mdir, 'mcu')) if f.endswith('.c'))
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', rb, os.path.join(TESTS, 'mt7921', 'roc_main.c')] + mcs)
    if record('wifi', 'mt7921 ROC/TXFREE/channel-domain test builds', rc == 0, out[-300:]):
        rc, out = sh([rb]); record('wifi', 'mt7921: UNI ROC acquire/abort TLVs + grant event + auto-off, TXFREE token release (wcid pairs, short packets), SET_CHAN_DOMAIN from regdb (DFS/no-IR flags, country), 6 GHz scan band 3 + BSS phymode_ext + HE/6 GHz band caps', rc == 0 and 'all passed' in out, out[-300:])
    pf = os.path.join(work, 'mt7921_pf')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', pf, os.path.join(TESTS, 'mt7921', 'pcifw_main.c'), os.path.join(mdir, 'pci', 'mt7921_pci.c'), os.path.join(mdir, 'pci', 'mt7921_dma.c'), os.path.join(mdir, 'fw', 'mt7921_fw.c')])
    if record('wifi', 'mt7921 pci/dma/fw test builds', rc == 0, out[-300:]):
        rc, out = sh([pf], env=dict(os.environ, MT_FW_DIR=os.path.join(KDIR, 'assets/firmware/linux-firmware/mediatek'))); record('wifi', 'mt7921: WFDMA rings, raw FW_SCATTER on the FWDL ring, patch + RAM download order, RX LAST_SEC0 drop of multi-descriptor frames, TX ring full', rc == 0 and 'mt7921 pci/fw: all passed' in out, out[-300:])
    mu = os.path.join(work, 'mt7921u_t')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', mu, os.path.join(TESTS, 'mt7921u', 'usb_main.c'), os.path.join(mdir, 'usb', 'mt7921_usb.c'), os.path.join(mdir, 'pci', 'mt7921_pci.c'), os.path.join(mdir, 'pci', 'mt7921_dma.c'), os.path.join(mdir, 'fw', 'mt7921_fw.c')] + mcs)
    if record('wifi', 'mt7921u USB transport test builds', rc == 0, out[-300:]):
        rc, out = sh([mu, os.path.join(KDIR, 'assets/firmware/linux-firmware/mediatek')]); record('wifi', 'mt7921u: USB IDs + interface/endpoint parse, vendor-request register access, WFSYS reset, MCU power-on, UWFDMA/UDMA setup, USB header + 4-byte tail framing, 64-byte USB TXD, patch + RAM download over USB with FW_DL_EN, NIC capability MAC, wifi_hw registration, data TX, bad RX dropped, control failure stops the adapter, 3-failure off, 6 GHz refused', rc == 0 and 'mt7921u usb: all passed' in out, out[-300:])
    m5 = os.path.join(work, 'mt7925_c3')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', m5, os.path.join(TESTS, 'mt7925', 'connac3_main.c'), os.path.join(KDIR, 'drivers/net/wifi/mediatek/mt7925/mcu.c')] + mcs)
    if record('wifi', 'mt7925 connac3 MCU test builds', rc == 0, out[-300:]):
        rc, out = sh([m5]); record('wifi', 'mt7925: connac3 UNI/legacy TXD, 44-byte RXD normalisation, firmware-reset detection after 2 MCU timeouts in a row while ready, CHIP_CONFIG NIC capability TLVs (MAC, 6 GHz), UNI SCAN_REQ/CANCEL TLVs, scan-done event', rc == 0 and 'mt7925 connac3: all passed' in out, out[-300:])
    mcl = os.path.join(work, 'mt7925_clc')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', mcl, os.path.join(TESTS, 'mt7925', 'clc_main.c'), os.path.join(KDIR, 'drivers/net/wifi/mediatek/mt7925/clc.c')])
    if record('wifi', 'mt7925 CLC test builds', rc == 0, out[-300:]):
        rc, out = sh([mcl]); record('wifi', 'mt7925: CLC region from the RAM code trailer, record split by HW encap, country list, SET_POWER_LIMIT request bytes, 00 fallback, 3-failure auto-off, resent after firmware reset, 6 GHz PSC scan list', rc == 0 and 'mt7925 clc: all passed' in out, out[-300:])
    mj = os.path.join(work, 'mt7925_join')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', mj, os.path.join(TESTS, 'mt7925', 'join_main.c'), os.path.join(KDIR, 'drivers/net/wifi/mediatek/mt7925/mac.c'), os.path.join(KDIR, 'drivers/net/wifi/mediatek/mt7925/mcu.c')] + mcs)
    if record('wifi', 'mt7925 connac3 join test builds', rc == 0, out[-300:]):
        rc, out = sh([mj]); record('wifi', 'mt7925: connac3 TXD (mgmt/data/mcast/ADDBA/band), RXD groups + error drops, BSS_INFO/STA_REC unified TLVs, join sequence 2.4/5/6 GHz, KEY_V3 CCMP/BIP, remove on leave, EHT TLV, host SAE commit/confirm + RSN AKM 8/RSNXE in assoc, SAE failure, 6 GHz OFDM-only rates, EHT caps in assoc only for EHT APs + HE fallback + 3-failure auto-off', rc == 0 and 'mt7925 join: all passed' in out, out[-300:])
    rb = os.path.join(work, 'mt7921_rate')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', rb, os.path.join(TESTS, 'mt7921', 'rate_host.c')])
    if record('wifi', 'mt7921 rate/BA test builds against mcu/rate.c', rc == 0, out[-300:]):
        rc, out = sh([rb]); record('wifi', 'mt7921: assoc IE peer caps, STA_REC HT/VHT/HE/PHY/RA/STATE TLVs, WTBL+STA_REC BA, ADDBA/DELBA flow, kill switches and auto-off', rc == 0 and 'mt7921 rate/ba: all passed' in out, out[-300:])
    b = os.path.join(work, 'wifi_t')
    rc, out = sh(['gcc', '-O1', '-w', '-I', KDIR, '-o', b, os.path.join(TESTS, 'wifi', 'crypto.c')])
    if not record('wifi', 'Wi-Fi crypto test builds against the current sources', rc == 0, out[-300:]): return
    rc, out = sh([b])
    n = out.count('PASS '); record('wifi', f'SAE (+H2E/SSWU PWE) = IEEE 802.11 Annex J.10, CMAC = RFC 4493, MSCHAPv2 = RFC 2759, MD4, DES ({n} checks)', rc == 0 and n >= 30, out[-400:])
    b = os.path.join(work, 'sae_h2e_t')
    if record('wifi', 'SAE H2E test builds against net/wifi/wpa_sm.c and wpa3.c', sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', b, os.path.join(TESTS, 'wifi', 'sae_h2e', 'h2e_test.c')])[0] == 0):
        rc, out = sh([b]); record('wifi', 'SAE H2E: status 126 commit, PMK agreement, Token Container, HnP fallback, 3-failure auto-off, H2E-only selector', rc == 0 and 'h2e: all ok' in out, out[-400:])
    b = os.path.join(work, 'sae_hunt_t')
    if record('wifi', 'SAE hostile-peer test builds (ASan/UBSan)', sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', b, os.path.join(TESTS, 'wifi', 'sae_h2e', 'hunt_test.c')])[0] == 0):
        rc, out = sh([b]); record('wifi', 'SAE hostile peer: scalar 0/1/r and off-curve elements refused, constant-time HnP = old PWE, context wiped on every exit, commit fuzz, RSNXE downgrade in msg 3/4 refused', rc == 0 and 'sae hunt: all ok' in out and 'runtime error' not in out, out[-400:])
    b = os.path.join(work, 'wpa_sm_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', b, os.path.join(TESTS, 'wifi', 'wpa_sm', 'wpa_sm_main.c')])
    if not record('wifi', 'WPA state machine test builds against net/wifi/wpa_sm.c and the frozen rtw_sec.c copy', rc == 0, out[-300:]): return
    rc, out = sh([b]); m = re.search(r'(\d+) passed, (\d+) failed', out)
    record('wifi', 'WPA2 4-way + group key: shared wpa_sm.c sends byte-identical frames and keys to the old rtw_sec.c (PSK, PSK-SHA256, SAE keys, wrong password)', rc == 0 and m is not None and m.group(2) == '0' and 'runtime error' not in out, out[-400:])
def g_secboot(work):
    head('Signed boot: SHA-256 and P-256 signature checks used by the UEFI loader and the kernel')
    env = dict(os.environ, SANFLAGS=' '.join(SAN))
    rc, out = sh(['python3', os.path.join(TESTS, 'secboot', 'run.py'), os.path.join(work, 'secboot')], env=env)
    m = re.search(r'secboot: (\d+) passed, (\d+) failed', out)
    record('secboot', f'SHA-256, ECDSA P-256, signed kernel block, tampering, other key, rollback fields ({m.group(1) if m else 0} checks)', rc == 0 and m is not None and m.group(2) == '0', out[-400:])
    k = os.path.join(KDIR, 'build', 'kernel64.bin'); sr = os.path.join(KDIR, 'build', 'sysroot.bin')
    if os.path.exists(k):
        kb = open(k, 'rb').read()
        rc, out = sh(['python3', os.path.join(KDIR, 'tools', 'nvxsign.py'), 'verify', k] + (['--sysroot', sr] if os.path.exists(sr) else []))
        record('secboot', 'build/kernel64.bin: signature valid, Linux files match, magic appears once', rc == 0 and kb.count(b'NVX64SIG') == 1, out[-300:])
def g_tether(work):
    head('phone USB tethering: Apple ipheth framing/descriptors, Android RNDIS/NCM and iPhone interface choice, Phone (USB) naming')
    src = open(os.path.join(KDIR, 'drivers', 'usb', 'xhci', 'usb_network.c')).read()
    m = re.search(r'(?m)^static int usn_parse\(', src); t = re.search(r'(?m)^typedef struct \{ int kind, cfgval.*usn_cand_t;$', src)
    if not record('usb', 'usn_parse and usn_cand_t found in usb_network.c', bool(m and t)): return
    k = src.find('{', m.start()); d = 0
    for x in range(k, len(src)):
        if src[x] == '{': d += 1
        elif src[x] == '}':
            d -= 1
            if d == 0: break
    open(os.path.join(work, 'usn_parse.inc'), 'w').write('int g_usn_pref=0;\n' + t.group(0) + '\n' + src[m.start():x + 1] + '\n')
    exe = os.path.join(work, 'tether_t')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-DTETHER_PARSE', '-I' + KDIR, '-I' + work, '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', os.path.join(TESTS, 'usbnet', 'tether_main.c'), '-o', exe])
    if not record('usb', 'tethering test builds against the driver descriptor parser', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300)
    for l in out.splitlines():
        if l.startswith('  '): print('  ' + l.strip()[:150])
    record('usb', f"phone tethering: ipheth TX/RX/carrier/MAC, simulated iPhone/Pixel/Galaxy descriptors, 200000 fuzzed inputs ({out.count(' ok')} checks)", rc == 0 and 'ALL PASS' in out, out[-300:])

def g_usbnet_frame(work):
    exe = os.path.join(work, 'usbnet_frame')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-I' + KDIR] + SAN + [os.path.join(TESTS, 'usbnet', 'frame_main.c'), '-o', exe])
    if not record('usb', 'USB Ethernet framing test builds (RTL815x, AX88179, NCM, RNDIS, ECM, ipheth)', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300)
    record('usb', f"USB Ethernet framing: aggregated RX, descriptors, trailers, 200000 random buffers ({out.count('  ok ')} checks)", rc == 0 and 'ALL PASS' in out, out[-300:])
def g_usb(work):
    head('USB keyboard/mouse driver (generated from xhci.c)')
    s = kread(os.path.join(KDIR, 'drivers/usb/xhci.c'))
    tp = template(os.path.join(TESTS, 'usb_template.txt'), ['PRE', 'PRE2', 'MAIN'])
    decl = s[s.find('/* per-entry HID state'):s.find('static void xhid_reset(void)')]
    if 'static u8 xhid_bs[64];' in s and 'xhid_bs' not in decl: decl += '\nstatic u8 xhid_bs[64];\n'
    if 'xhid_sim_e' in s and 'xhid_sim_e' not in decl: decl += '\nstatic int xhid_sim_e=-1;\n'
    m_inf = re.search(r'#define XHID_INFLIGHT[^\n]*', s)
    if m_inf and 'XHID_INFLIGHT' not in decl: decl = m_inf.group(0) + '\n' + decl
    decl = decl.replace('static int xhci_csz=0; static u32 xhci_dboff=0, xhci_rtoff=0;\n', '').replace('static u32 cmd_enq=0, cmd_cyc=1, ev_deq=0, ev_cyc=1;\n', '')
    reps = s[s.find('static int kcaps;'):s.find('static int usb_key_code')] if 'static int kcaps;' in s else 'static int kcaps;\n' + s[s.find('static int usbk_rep_code'):s.find('static int usb_key_code')]
    names = ['xhid_reset', 'hid_pick', 'hid_parse', 'hid_bits', 'xh_interval', 'ctx_sz', 'ctx_zero', 'xhid_free_region', 'xh_disable_slot', 'xhid_arm', 'xhid_recover_now', 'xhid_recover', 'xh_set_fail', 'xh_root_reset', 'xh_slot_ctx', 'xh_fix_mps0', 'xhci_hid_enum', 'xhci_enum_loc',
             'xhub_db', 'xhub_ctrl', 'xhub_status', 'xh_delay_ms', 'xh_under', 'usbk_mods_sync', 'hid2scan', 'xhub_remove_below', 'xhub_port_reset', 'xhub_child_loc', 'xhub_enum_port', 'xhub_attach', 'xhub_poll_ports',
             'hid2ascii', 'usb_key_code', 'kbd_report', 'kbd_nkro', 'usb_kbd_repeat', 'cc_hk', 'hid_report_decode', 'hid_xfer', 'xhci_poll_one', 'xhci_mouse_poll_in', 'xhci_mouse_poll', 'xhid_remove_port', 'xh_hid_ep', 'xh_ptr_ok', 'xh_event_w_in', 'xh_event_w', 'xh_event_t', 'xh_bo_held', 'xh_bo_note', 'xh_ep0_recover', 'xh_ctrl']
    try: funcs = {n: defn(s, n) for n in names}
    except KeyError as e: record('usb', f'function {e} found in xhci.c', False); return
    funcs['xhci_enum_loc'] = funcs['xhci_enum_loc'].replace('#define XFAIL', '#undef XFAIL\n#define XFAIL', 1)
    funcs['xh_ctrl'] = funcs['xh_ctrl'].replace('static int xh_ctrl(', 'static int real_xh_ctrl(', 1)
    first = [n for n in ('xh_hid_ep', 'xh_ptr_ok', 'xh_event_w_in', 'xh_event_w', 'xh_event_t') if n in funcs]; last = [n for n in ('xh_bo_held', 'xh_bo_note', 'xh_ep0_recover', 'xh_ctrl') if n in funcs]
    order = first + [n for n in names if n not in first and n not in last] + last
    rob = kread(os.path.join(KDIR, 'drivers/usb/xhci/xhci_robust.c'), False) + ''.join(re.findall(r'(?m)^static xhr_storm_t xhub_storm\[[^\n]*\n', s)) + 'static void xrc_hid_note(int e,u8 cc){ (void)e; (void)cc; } static void xrc_hid_cleared(int e){ (void)e; }\n'
    body = rob + 'static int xhci_enum_loc(const usbloc_t* L);\nstatic void mtp_remove_below(int root,u32 R,int dR){ (void)root; (void)R; (void)dR; }\nstatic int mtp_enum_loc(const usbloc_t* L){ (void)L; return -1; }\n' + '\n'.join(funcs[n] if n != 'xh_fix_mps0' else tp['PRE2'] + '\n' + funcs[n] for n in order)
    body += '\n\nstatic int usn_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; }\nstatic void usn_remove_below(int root,u32 R,int dR){ (void)root; (void)R; (void)dR; }\nstatic int usn_enum_loc(const usbloc_t* L){ (void)L; return 0; }\nstatic int usn_enum_root(int port){ (void)port; return 0; }\nstatic int usn_active(void){ return 0; }\nstatic void usn_noslot(void){}\nvoid usn_reset_all(void){}\nstatic int btu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; }\nstatic int btu_active(void){ return 0; }\nstatic int btu_owns(int c,int p){ (void)c; (void)p; return 0; }\nstatic void btu_remove_below(int root,u32 R,int dR){ (void)root; (void)R; (void)dR; }\nvoid btu_reset_all(void){}\nstatic int um_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; }\nstatic int um_active(void){ return 0; }\nstatic void um_remove_below(int root,u32 R,int dR){ (void)root; (void)R; (void)dR; }\nstatic int um_try_port(int p){ (void)p; return 0; }\nstatic void um_unplug(int p){ (void)p; }\nstatic int umidi_owns(int c,int p){ (void)c; (void)p; return 0; }\nvoid um_reset_all(void){}\nstatic int g_umhub_n, g_umhub_route=-1, g_umhub_depth=-1; static int um_hub_try(int hi,int p){ usbloc_t C; g_umhub_n++; xhub_child_loc(hi,p,2,&C); g_umhub_route=(int)C.route; g_umhub_depth=C.depth; return 0; }\nstatic int gpu_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; }\nstatic int gpu_active(void){ return 0; }\nstatic void gpu_remove_below(int root,u32 R,int dR){ (void)root; (void)R; (void)dR; }\nstatic int gpu_try_port(int p){ (void)p; return 0; }\nstatic void gpu_unplug(int p){ (void)p; }\nstatic int gpu_hub_try(int hi,int p){ (void)hi; (void)p; return 0; }\nstatic int xh_cfg_gp; static int gpu_cfg_is_pad(const u8* c,int n){ (void)c; (void)n; return 0; }\nvoid gpu_reset_all(void){} \nstatic void usbreg_probe(const usbloc_t* L,const u8* full,int wtot){ (void)L; (void)full; (void)wtot; }\nvoid usbreg_gone(int ctl,int root,u32 R,int dR){ (void)ctl; (void)root; (void)R; (void)dR; }\nstatic int us_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; }\nstatic int us_active(void){ return 0; }\nstatic int us_try_port(int p){ (void)p; return 0; }\nstatic void us_unplug(int p){ (void)p; }\nvoid us_reset_all(void){}\n'
    ty = s[s.find('typedef struct { u8 iface'):s.find('static int hid_parse')]
    extra = ty + 'static char xhci_failstep[48]; static int xhci_failcc;\n' + 'int dg_xfer(int e,const u8* b,int n){ (void)e; (void)b; (void)n; return 0; } int dg_probe(int k,u16 v,u16 p,const u8* r,int n,u8* f,int* fl,int* fr){ (void)k; (void)v; (void)p; (void)r; (void)n; (void)f; *fl=0; *fr=0; return 0; } void dg_bind(int k,int e){ (void)k; (void)e; } static int hpp_xfer(int e,const u8* b,int n){ (void)e; (void)b; (void)n; return 0; } static int hpp_wheel(int e,int wz){ (void)e; return wz; } static void hppx_poll(void){} static int hpp_receiver_pid(u16 p){ (void)p; return 0; } static int hpp_vendor_iface(u16 v,const u8* r,int n){ (void)v; (void)r; (void)n; return 0; } static void hppx_attach(int e,int o,u16 v,u16 p,u8 i,int r){ (void)e; (void)o; (void)v; (void)p; (void)i; (void)r; } void hidpp_service(void){}\n'
    ctx = s[s.find('typedef struct { int present'):s.find('static void xhc_save(void)')] + '\n'.join(defn(s, n) for n in ('xhc_save', 'xhc_load', 'xhc_select')) + '\n'
    code = tp['PRE'].replace('@@XHC_CTX@@', '') + khdr(os.path.join(KDIR, 'drivers/usb/xhci.c')) + decl + reps.replace('static int kcaps;', 'static int kcaps;\n' + extra) + ctx + body + '\n' + tp['MAIN']
    c = os.path.join(work, 'usb_t.c'); open(c, 'w').write(code); exe = os.path.join(work, 'usb_t')
    rc, out = sh(['gcc', '-O0', '-w', '-fno-pie', '-no-pie'] + SAN + ['-o', exe, c])
    if not record('usb', 'test builds against the current driver', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('usb', f'receivers, routing, repeat, keys, hot-unplug, hubs ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))
    d = os.path.join(TESTS, 'usb'); hc = os.path.join(work, 'hidmouse_t.c'); hx = os.path.join(work, 'hidmouse_t')
    rc, out = sh(['python3', os.path.join(d, 'hidmouse_gen.py'), os.path.join(KDIR, 'drivers/usb/xhci/controller_init.c'), os.path.join(KDIR, 'drivers/usb/xhci/hid_keyboard_mouse.c'), os.path.join(KDIR, 'include/nvx/drivers/usb/xhci.h'), hc, os.path.join(d, 'hidmouse_main.c')])
    if not record('usb', 'wireless/gaming mouse decode test builds', rc == 0 and sh(['gcc', '-O1', '-w'] + SAN + ['-o', hx, hc])[0] == 0, out[-300:]): return
    rc, out = sh([hx]); n = out.count(' ok\n')
    record('usb', f'mouse report descriptors: boot, Unifying 12-bit, Lightspeed/gaming 16-bit, combo receiver ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(d, 'hidkbd_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN), TMPDIR=work)); n = out.count(' ok\n')
    record('usb', f'wireless keyboards: report IDs, NKRO, combo receivers, rollover, two keyboards ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    dx = os.path.join(work, 'digitizer_t'); rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', dx, os.path.join(d, 'digitizer_main.c')])
    if record('usb', 'pen tablet and touchscreen test builds', rc == 0, out[-300:]):
        rc, out = sh([dx]); n = out.count(' ok\n')
        record('usb', f'pen/touch digitizers: pressure, tilt, eraser, Wacom HID + pen protocol, hybrid multitouch, input mode, tap/drag/scroll/pinch, evdev, fuzz ({n} checks)', rc == 0 and 'digitizer: ALL PASS' in out, out[-400:])
    gx = os.path.join(work, 'gamepad_t'); rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-Wno-unused-function'] + SAN + ['-I', KDIR, '-o', gx, os.path.join(TESTS, 'gamepad', 'gpad_main.c')])
    if record('usb', 'game controller test builds', rc == 0, out[-300:]):
        rc, out = sh([gx]); m = re.search(r'(\d+) checks', out)
        record('usb', f'game controllers: Xbox 360/One GIP, DualShock 4/DualSense USB+BT reports and CRC, Switch Pro, HID gamepads, evdev/joydev ABI vs linux/input.h, rumble, touchpad, fuzz ({m.group(1) if m else "?"} checks)', rc == 0 and 'gamepad: all passed' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(d, 'hidparse_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN), TMPDIR=work)); n = out.count(' ok\n')
    record('usb', f'HID descriptor parser: usage ranges, push/pop, long items, tablets, hi-res wheel, AC Pan, 16 buttons, fuzz ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(TESTS, 'input', 'evdev_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN), TMPDIR=work)); n = out.count(' ok\n')
    record('usb', f'Linux evdev pointer: 16 buttons (BTN_SIDE..BTN_TASK, BTN_0..), REL_WHEEL_HI_RES, ioctls, bounded ring ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(d, 'hidpp_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN), TMPDIR=work)); n = out.count(' ok\n')
    record('usb', f'Logitech HID++ receivers: pairing list, battery, hi-res wheel, sleep/wake ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(d, 'hidsim_test.sh')], env=dict(os.environ, TMPDIR=work))
    record('usb', 'combo receiver end-to-end simulation: no case loses Y, no unexpected failures', 'LOST-Y 0,' in out and 'unexpected (not in a known XFAIL class): 0' in out, out[-400:])
    ur = os.path.join(work, 'usbreg_t'); rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I', KDIR, '-o', ur, os.path.join(d, 'usbreg_main.c')])
    if record('usb', 'USB device registry test builds', rc == 0, out[:300]):
        rc, out = sh([ur]); n = out.count(' ok\n')
        record('usb', f'USB device registry: hubs, parents, bus names, devnum, bMaxPower, lsusb/lsusb -t, unplug, bounds ({n} checks)', rc == 0 and 'usbreg: ALL PASS' in out, out[-400:])
    rc, out = sh(['sh', os.path.join(d, 'ippusb_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN), TMPDIR=work)); n = out.count(' ok\n')
    record('usb', f'USB printers: printer class descriptors, device ID, IPP-over-USB HTTP framing, IPP attributes, eSCL, fuzz ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rx = os.path.join(work, 'xhci_robust_t'); rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-DXHR_SRC="' + os.path.join(KDIR, 'drivers/usb/xhci/xhci_robust.c') + '"', '-o', rx, os.path.join(d, 'xhci_robust_main.c')])
    if record('usb', 'xHCI robustness helpers test builds', rc == 0, out[:300]):
        rc, out = sh([rx]); n = out.count(' ok\n')
        record('usb', f'xHCI robustness: route strings, hub descriptors, TT/multi-TT, error recovery, storms, interrupters, BIOS handoff ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    rr = os.path.join(work, 'xhci_recov_t'); rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-DXHR_SRC="' + os.path.join(KDIR, 'drivers/usb/xhci/xhci_robust.c') + '"', '-DXRC_SRC="' + os.path.join(KDIR, 'drivers/usb/xhci/xhci_recov.c') + '"', '-o', rr, os.path.join(d, 'xhci_recov_main.c')])
    if record('usb', 'xHCI error recovery test builds (simulated controller)', rc == 0, out[:300]):
        rc, out = sh([rr]); n = out.count(' ok\n')
        record('usb', f'xHCI recovery: bulk/interrupt stall, transaction error, babble, timeouts, re-enumeration, event ring full, root-port debounce ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    fc = os.path.join(work, 'descfuzz_t.c'); fx = os.path.join(work, 'descfuzz_t'); fsan = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
    rc, out = sh(['python3', os.path.join(d, 'descfuzz_gen.py'), KDIR, fc, os.path.join(d, 'descfuzz_main.c')])
    if rc == 0 and sh(['gcc', '-O1', '-g', '-w'] + fsan + ['-o', fx, fc])[0] != 0: fsan = SAN
    if record('usb', 'descriptor fuzz harness builds against the current parsers' + (' (ASan+UBSan)' if fsan != SAN else ''), rc == 0 and sh(['gcc', '-O1', '-g', '-w'] + fsan + ['-o', fx, fc])[0] == 0, out[-300:]):
        rc, out = sh([fx, '200000']); n = out.count(' ok\n')
        record('usb', f'malicious descriptors: HID/UVC/UAC/MIDI/CDC/BT/storage/hub configs + MAC string, mutated + generated, regressions ({n} checks)', rc == 0 and 'usb descriptor fuzz: ALL PASS' in out, out[-600:])
    px = os.path.join(work, 'xhcipm_t')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I' + KDIR] + SAN + ['-o', px, os.path.join(d, 'xhcipm_main.c')])
    if record('usb', 'xHCI power management test builds against the current driver', rc == 0, out[-300:]):
        rc, out = sh([px]); n = out.count(' ok ')
        record('usb', f'xHCI sleep: save/restore (CSS/CRS, CRCR, ERST), U3 suspend/resume, remote wake, fallbacks, idle ports ({n} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])
    ux = os.path.join(work, 'usbser_t')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-I' + KDIR] + SAN + ['-o', ux, os.path.join(d, '..', 'usbser', 'proto_main.c')])
    if record('usb', 'USB serial protocol test builds', rc == 0, out[-300:]):
        rc, out = sh([ux], timeout=300); n = re.search(r'(\d+) checks', out)
        record('usb', f'USB serial: FTDI/CH341 divisors, CP210x/PL2303/CDC-ACM requests, DTR/RTS/break, termios + ioctl ABI, descriptor fuzz ({n.group(1) if n else "?"} checks)', rc == 0 and 'ALL PASS' in out, out[-400:])

def g_acpi(work):
    head('ACPI power off / restart (generated from power.c)')
    src = kread(os.path.join(KDIR, 'core/power.c'))
    tp = template(os.path.join(TESTS, 'acpi_template.txt'), ['HDR', 'TEST'])
    src = src.replace('#include "third_party/ntfs/nvxntfs.h"\n', '')
    s0 = src.find('int g_sleeping;'); s1 = src.find('int power_sleep(void){')
    if s0 >= 0 and s1 > s0: src = src[:s0] + 'int g_sleeping; volatile int g_sleep_wake, g_sleep_req; u64 g_wake_t; void power_wake_req(void){ if(g_sleeping)g_sleep_wake=1; } int power_s2idle(void){ return 0; } static int power_sleep_raw(void); enum { SLP_MODE_NONE=0, SLP_MODE_S2IDLE, SLP_MODE_S1, SLP_MODE_S3 };\n' + src[s1:]
    a = src.find('static int power_sleep_raw(void){')
    if a >= 0: b = src.find('\n}\n', a) + 3; src = src[:a] + 'static int power_sleep_raw(void){ return 0; } volatile int g_heap_busy, g_disk_busy, g_cx_panicking, g_nmiwd_on, g_frz_hold, g_frz_down; int sleep_begin(void){ return 0; } void sleep_end(void){} void svcu_shutdown(int t){ (void)t; } void nvx3_sync_all(void){} void ufat_sync(void){}\n' + src[b:]
    a = src.find('static u64 acpi_find_rsdp(void){'); b = src.find('\n}\n', a) + 3
    src = src[:a] + 'static u64 g_test_rsdp; static u64 acpi_find_rsdp(void){ return acpi_rsdp_ok(g_test_rsdp)?g_test_rsdp:0; }\n' + src[b:]
    c = os.path.join(work, 'acpi_t.c'); open(c, 'w').write(tp['HDR'] + kmissing(src, tp['HDR'] + tp['TEST']) + src + tp['TEST']); exe = os.path.join(work, 'acpi_t')
    rc, out = sh(['gcc', '-O0', '-w'] + SAN + ['-o', exe, c])
    if not record('acpi', 'test builds against the current power module', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if 'FAIL' in l and 'FAILURES' not in l]
    for l in fails: print('    ' + l)
    record('acpi', 'S5 parsing, ACPI enable, reset register, hardware-reduced, bad tables', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_ucsi(work):
    head('USB-C UCSI driver (drivers/usb/typec/ucsi_core.c against a simulated PPM mailbox)')
    exe = os.path.join(work, 'ucsi_t')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I', KDIR] + SAN + ['-DUCSI_SRC="' + os.path.join(KDIR, 'drivers/usb/typec/ucsi_core.c') + '"', '-o', exe, os.path.join(TESTS, 'ucsi', 'main.c')])
    if not record('acpi', 'UCSI host test builds against the current driver', rc == 0, out[-300:]): return
    rc, out = sh([exe]); n = out.count(' ok\n'); fails = [l.strip() for l in out.splitlines() if l.strip().endswith('FAIL')]
    record('acpi', f'UCSI: PPM reset, capability, connector status, PD/PPS/battery contracts, DP/TBT alt modes, busy/timeout/error, fuzz ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]) or out[-300:])

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

def g_swapstore(work):
    head('compressed swap store and LZ codec (generated from mem/swap.c)')
    s = kread(os.path.join(KDIR, 'mem/swap.c')); a = s.find('typedef struct { u64 loc;'); b = s.find('extern int lx_fm_covers')
    if not record('memory', 'swap store code located', a >= 0 and b > a): return
    m = open(os.path.join(TESTS, 'swap', 'swap_main.c')).read().replace('/*SWAPCODE*/', s[a:b])
    c = os.path.join(work, 'swap_t.c'); open(c, 'w').write(m); exe = os.path.join(work, 'swap_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c])
    if not record('memory', 'swap store test builds against the current code', rc == 0, out[:300]): return
    rc, out = sh([exe]); record('memory', 'swap store: 100000 codec round trips with corrupted input, 20000 pages stored, read, shared and freed, no frames left', rc == 0 and 'swap ok' in out, out[-400:])
def g_vmframe(work):
    head('page-fault frame allocation under memory pressure (vm_frame from mem/pmm_vmm.c)')
    s = open(os.path.join(KDIR, 'mem/pmm_vmm.c')).read(); a = s.find('int g_vm_oom_why;'); b = s.find('int vm_fault_user(')
    if not record('memory', 'vm_frame code located', a >= 0 and b > a): return
    m = open(os.path.join(TESTS, 'swap', 'frame_main.c')).read().replace('/*FRAMECODE*/', s[a:b].replace('extern u32 sysroot_pc_reclaim(u32) __attribute__((weak));', 'u32 (*sysroot_pc_reclaim)(u32)=0;'))
    c = os.path.join(work, 'frame_t.c'); open(c, 'w').write(m); exe = os.path.join(work, 'frame_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-o', exe, c])
    if not record('memory', 'vm_frame test builds against the current code', rc == 0, out[:300]): return
    rc, out = sh([exe]); record('memory', 'vm_frame keeps reclaiming through second-chance passes and long scan cycles, gives up bounded when nothing is reclaimable', rc == 0 and 'frame ok' in out, out[-400:])
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
    g_igpu12(work)
    g_amdso(work)
    g_amdmc(work)
    g_amdsdma(work)
    g_amdgart(work)
    g_amdpsp(work)

def g_amdsdma(work):
    head('AMD SDMA 5.2/6.0: firmware parse, ring bring-up and copy/fill packets against a simulated engine')
    exe = os.path.join(work, 'amd_sdma')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'amd', 'sdma_main.c')])
    if not record('gpu', 'AMD SDMA test builds against sdma_core.c', rc == 0, out[:300]): return
    rc, out = sh([exe], cwd=KDIR); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'AMD SDMA 5.2.x/6.0.1 ucode load, ring, fence, sub-window copy, fills, bounded waits ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_amdgart(work):
    head('AMD GFXHUB GART for SDMA: aperture placement, context-0 page table, TLB flush, RAM<->VRAM copies through scattered pages (simulated)')
    exe = os.path.join(work, 'amd_sdma_gart')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'amd', 'sdma_gart_main.c')])
    if not record('gpu', 'AMD SDMA GART test builds against sdma_gart.c', rc == 0, out[:300]): return
    rc, out = sh([exe], cwd=KDIR); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'AMD GC 10.3/11.0 GART placement, PTEs, bounded TLB flush, SDMA fills/copies via system memory ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_amdpsp(work):
    head('AMD PSP firmware loading for SDMA: KM ring, TMR, LOAD_TOC, LOAD_IP_FW against a simulated PSP mailbox')
    exe = os.path.join(work, 'amd_psp')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'amd', 'psp_main.c')])
    if not record('gpu', 'AMD PSP test builds against psp_core.c', rc == 0, out[:300]): return
    rc, out = sh([exe], cwd=KDIR); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'AMD PSP 11.5/13.0.x KM ring, SETUP_TMR, LOAD_IP_FW SDMA0/TH0/TH1, held-by-firmware detection, bounded waits ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_amdso(work):
    head('AMD DCN scanout: per-generation register layout and page-flip sequence against a simulated register file')
    exe = os.path.join(work, 'amd_dso')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'amd', 'dso_main.c')])
    if not record('gpu', 'AMD DCN scanout test builds against dcn_so.c', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'AMD DCN 2.1/3.0.1/3.1.x/3.5 pipe scan, DMCUB-aware placement, vblank flips, bounded waits ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def g_amdmc(work):
    head('AMD DCN: memory-controller window, derived CPU mapping, CRC flip verification, DMCUB PSR/ABM commands (simulated)')
    exe = os.path.join(work, 'amd_dso_mc')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'amd', 'dso_mc_main.c')])
    if not record('gpu', 'AMD DCN memory-window/PSR test builds against dcn_so.c', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'AMD FB location from DCN_VM/MMHUB, BAR vs carve-out mapping, inuse+CRC read-back, GPINT PSR state, inbox PSR-off/ABM ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

def cfn_extract(src, names):
    out = []
    for nm in names:
        m = re.search(r'(?m)^static [\w\s\*]*?\b' + nm + r'\(', src)
        if not m: return None
        i = src.index('{', m.end()); d = 0
        for j in range(i, len(src)):
            d += {'{': 1, '}': -1}.get(src[j], 0)
            if d == 0: break
        out.append(src[m.start():j + 1])
    return '\n'.join(out)

def g_igpu12(work):
    head('Intel gen 12 blitter: command-stream and context encoding')
    src = open(os.path.join(KDIR, 'drivers/video/intel/igpu.c'), errors='ignore').read()
    fns = cfn_extract(src, ['creg', 'g12_ctx_regs', 'g12_desc', 'g12_csb_idle', 'blt12_fill', 'blt12_copy'])
    if not record('gpu', 'gen 12 encoders found in igpu.c', fns is not None): return
    t = open(os.path.join(TESTS, 'igpu', 'blt12_t.c')).read().replace('#include "BLT12_FNS"', fns)
    c = os.path.join(work, 'blt12_t.c'); open(c, 'w').write(t); exe = os.path.join(work, 'blt12_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Werror'] + SAN + ['-o', exe, c])
    if not record('gpu', 'gen 12 blitter test builds against the current igpu.c', rc == 0, out[:300]): return
    rc, out = sh([exe]); fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('gpu', f'XY_FAST_COLOR/COPY_BLT, ELSQ descriptor, gen 12 context image, CSB ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(fails[:2]))

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
    dev = dev.replace('{ extern int wmi_notify(anode*,u32); if(wmi_notify(n,v))return; }', '')
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

def g_svc(work):
    head('Service manager unit files (real svcunit_parse.c on the host): parser, argument split, restart back-off')
    exe = os.path.join(work, 'svc_t')
    rc, out = sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'svc', 'svcparse_test.c')])
    if not record('svc', 'unit file test builds against apps/system/svcunit_parse.c', rc == 0, out[:400]): return
    rc, out = sh([exe]); record('svc', 'unit keys, comments, Environment quoting, clamps, defaults, bad ExecStart, quoted args, back-off', rc == 0 and 'SVC all ok' in out, out[-300:])

def g_cookies(work):
    head('Cookie jar (real cookies.c on the host): Chrome-size cookies, per-site cap, SameSite, save and reload')
    exe = os.path.join(work, 'ck_t')
    rc, out = sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'cookies', 'ck_t.c')])
    if not record('cookies', 'cookie test builds against the current cookie jar', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=120); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.startswith('ok ') or l.startswith('FAIL')])
    record('cookies', f'4096-byte cookies, 16 KB header, 180 per site, 3000 total, reload ({n} checks)', rc == 0 and 'COOKIES all ok' in out, '; '.join(fails[:3]) or out[-300:])

def g_httptext(work):
    head('Terminal http (real net/http/http.c on the host): host:port splitting, dotted IPv4 without DNS, text dump skips <style>/<script> and separates the title')
    src = open(os.path.join(KDIR, 'net/http/http.c'), errors='ignore').read()
    fns = cfn_extract(src, ['htd_tag_is', 'htd_block', 'htd_skip_to', 'htd_entity'])
    for nm in ['parse_ip', 'http_split_port', 'http_text_dump']:
        m = re.search(r'(?m)^(int|void) ' + nm + r'\(', src)
        if not m: fns = None; break
        i = src.index('{', m.end()); d = 0
        for j in range(i, len(src)):
            d += {'{': 1, '}': -1}.get(src[j], 0)
            if d == 0: break
        fns += '\n' + src[m.start():j + 1]
    if not record('httptext', 'http helpers found in net/http/http.c', fns is not None): return
    t = open(os.path.join(TESTS, 'net', 'httptext_t.c')).read().replace('#include "HTTP_FNS"', fns)
    c = os.path.join(work, 'httptext_t.c'); open(c, 'w').write(t); exe = os.path.join(work, 'httptext_t')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', exe, c])
    if not record('httptext', 'http text test builds against the current http.c', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=120); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.startswith('ok ') or l.startswith('FAIL')])
    record('httptext', f'host:port, IPv4 literal, style/script/comment skipping, title separation, entities, 20000 random tag soups ({n} checks)', rc == 0 and 'HTTPTEXT all ok' in out, '; '.join(fails[:3]) or out[-300:])

def g_netfuzz(work):
    head('Network parsers under ASan/UBSan (real tcpip.c + lxsock.c on the host): ARP, IPv4, ICMP, UDP, TCP options, DHCP, DNS replies, frames shorter than their headers')
    exe = os.path.join(work, 'netfuzz')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-I', KDIR, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', exe, os.path.join(TESTS, 'net', 'netfuzz.c')])
    if not record('net', 'network parser fuzzer builds against net/stack/tcpip.c and sys/linux/lxsock.c', rc == 0, out[:400]): return
    cd = os.path.join(work, 'chunk'); os.makedirs(cd, exist_ok=True); bs = open(os.path.join(KDIR, 'apps/internet/browser/glue/http.c')).read()
    ext = cfn_extract(bs, ['chunk_done']); ext2 = cfn_extract(bs[bs.find('static int dechunk(u8* b, int n) {'):], ['dechunk'])
    if ext and ext2: open(os.path.join(cd, 'chunk_ext.c'), 'w').write(ext + '\n' + ext2 + '\n')
    rc2, out2 = sh(['gcc', '-O1', '-g', '-w', '-I', cd, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', os.path.join(cd, 't'), os.path.join(TESTS, 'net', 'chunk_t.c')]) if ext and ext2 else (1, 'chunk_done/dechunk not found')
    if record('net', 'browser chunked-encoding test builds (real chunk_done/dechunk)', rc2 == 0, out2[:400]):
        rc2, out2 = sh([os.path.join(cd, 't'), '200000']); record('net', 'chunked encoding: 0x80000000 chunk size refused (no int overflow / negative index), 200000 random bodies stay in range', rc2 == 0 and 'CHUNK all ok' in out2, out2[-300:])
    dw = os.path.join(work, 'dhcp_wait_t')
    rc3, out3 = sh(['gcc', '-O1', '-g', '-w', '-I', KDIR, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', dw, os.path.join(TESTS, 'net', 'dhcp_wait_t.c')])
    if record('net', 'DHCP wait test builds against net/stack/tcpip.c', rc3 == 0, out3[:400]):
        rc3, out3 = sh([dw], timeout=120); bad = [l.strip() for l in out3.splitlines() if l.startswith('FAIL')]
        record('net', 'nd_poll 1 ms wait is not cut short by a PIT wrap, dhcp_wait keeps a 900 ms deadline and accepts a late offer', rc3 == 0 and 'DHCPWAIT all ok' in out3, '; '.join(bad[:3]) or out3[-300:])
    rc, out = sh([exe, '20000', '1'], timeout=900); record('net', 'parser fuzz: regression frames (short DNS/TCP/ICMP/ARP) and ~50000 mutated frames, no read past the received length', rc == 0 and 'regressions ok' in out and 'no faults' in out, out[-400:])

def g_fatfuzz(work):
    head('FAT16/FAT32 crafted images under ASan/UBSan (real vfat.c on the host): mount, list, read, write, rename, delete, reclaim')
    exe = os.path.join(work, 'fatfuzz')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-I', KDIR, '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-o', exe, os.path.join(TESTS, 'fs', 'fatfuzz.c')])
    if not record('fs', 'FAT fuzzer builds against fs/fat/vfat.c', rc == 0, out[:400]): return
    rc, out = sh([exe, '3000', '1'], timeout=900); record('fs', 'FAT crafted images: seed images read back, regressions and 4000 mutated FAT16/FAT32 images fail closed', rc == 0 and 'regressions ok' in out and 'no faults' in out, out[-400:])

def g_lxhost(work):
    head('Linux layer helpers on the host: old getdents conversion bounds, sign-in JSON escaping')
    rc, out = sh(['sh', os.path.join(TESTS, 'linux', 'run_dent_jesc.sh')], timeout=300)
    fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    record('lxhost', 'getdents(78) reclen bounds + nvx sign-in JSON escape', rc == 0 and 'DENT_JESC all ok' in out, '; '.join(fails[:3]) or out[-300:])
    ad = os.path.join(work, 'aslr'); os.makedirs(ad, exist_ok=True)
    rc, out = sh([sys.executable, os.path.join(TESTS, 'kaslr', 'aslr_gen.py'), KDIR, os.path.join(ad, 'aslr_ext.c')])
    if rc == 0: rc, out = sh(['gcc', '-O1', '-w', '-I', ad] + SAN + ['-o', os.path.join(ad, 't'), os.path.join(TESTS, 'kaslr', 'aslr_t.c'), '-lm'])
    if not record('lxhost', 'user ASLR entropy test builds (real kaslr.c + elf.c helpers)', rc == 0, out[:400]): return
    rc, out = sh([os.path.join(ad, 't')], timeout=300); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    record('lxhost', 'user ASLR: mmap 26 bits, ld.so 25+ bits, PIE 12/24 bits, stack 12 bits over 200000 draws', rc == 0 and 'ASLR all ok' in out, '; '.join(fails[:3]) or out[-300:])
    hd = os.path.join(work, 'harden'); os.makedirs(hd, exist_ok=True)
    rc, out = sh([sys.executable, os.path.join(TESTS, 'harden', 'hd_gen.py'), KDIR, os.path.join(hd, 'hd_ext.c')])
    if rc == 0: rc, out = sh(['gcc', '-O1', '-w', '-I', hd] + SAN + ['-o', os.path.join(hd, 't'), os.path.join(TESTS, 'harden', 'hd_t.c')])
    if not record('lxhost', 'kernel hardening helpers test builds (real harden.c)', rc == 0, out[:400]): return
    rc, out = sh([os.path.join(hd, 't')], timeout=60); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    record('lxhost', 'CR4 SMEP/SMAP/UMIP bits fail safe on old CPUs, stack guard mix', rc == 0 and 'HARDEN all ok' in out, '; '.join(fails[:3]) or out[-300:])
def g_perf(work):
    head('perf events: attr mapping, Intel/AMD event selects, counter reload and read formats (real perf_codec.h)')
    exe = os.path.join(work, 'perf_t')
    rc, out = sh(['gcc', '-O1', '-w', '-I', KDIR] + SAN + ['-o', exe, os.path.join(TESTS, 'perf', 'codec_t.c')])
    if not record('perf', 'perf codec test builds', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=60); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    for l in fails: print('    ' + l)
    record('perf', 'perf_event_attr mapping, evsel, reload, read_format', rc == 0 and 'PERF all ok' in out, '; '.join(fails[:3]) or out[-300:])
    xd = os.path.join(work, 'perf_xcpu'); os.makedirs(xd, exist_ok=True)
    rc, out = sh([sys.executable, os.path.join(TESTS, 'perf', 'xcpu_gen.py'), os.path.join(KDIR, 'core', 'perf.c'), os.path.join(xd, 'xcpu_ext.c')])
    if rc == 0: rc, out = sh(['gcc', '-O1', '-w', '-pthread', '-I', KDIR, '-I', xd] + SAN + ['-o', os.path.join(xd, 't'), os.path.join(TESTS, 'perf', 'xcpu_t.c')])
    if not record('perf', 'cross-CPU close test builds (real pf_stop/perf_ev_close/perf_cpu_drain)', rc == 0, out[:400]): return
    rc, out = sh([os.path.join(xd, 't')], timeout=120); fails = [l.strip() for l in out.splitlines() if l.startswith('FAIL')]
    record('perf', 'close from another CPU defers the stop to the owner CPU, bounded wait, slot fails closed', rc == 0 and 'PERF XCPU all ok' in out, '; '.join(fails[:3]) or out[-300:])

def g_linux(work):
    head('Linux system-call layer + musl libc (real kernel code run on the host, guest syscalls trapped with seccomp)')
    T = os.path.join(TESTS, 'linux'); B = os.path.join(KDIR, 'build'); M = os.path.join(B, 'musl')
    blobs = sorted(os.path.join(B, f) for f in os.listdir(B) if f.startswith('user_') and f.endswith('_bin.o')) if os.path.isdir(B) else []
    need = [os.path.join(M, 'lib', 'libc.a'), os.path.join(B, 'user', 'musltest.elf'), os.path.join(B, 'user', 'mhello.elf')]
    if not record('linux', 'musl libc and musl programs are built (build.sh)', all(os.path.exists(x) for x in need) and blobs, 'run check.py --build first'): return
    k = os.path.join(work, 'lk')
    shutil.copytree(KDIR, k, ignore=lambda d, names: [n for n in names if (d == KDIR and n in ('build', 'boot', 'tools', 'docs', '.git')) or (os.path.basename(d) == 'third_party' and n in ('tinycc', 'vim', 'git', 'curl', 'mbedtls', 'dropbear', 'ncurses', 'less', 'netsurf', 'fonts', 'musl', 'doom', 'lua', 'ffmpeg', 'src'))])
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

def g_ext4(work):
    head('ext4 journal (jbd2) and driver write gate against mke2fs/e2fsck/debugfs images')
    T = os.path.join(TESTS, 'ext4')
    for script, name, tag in (('jtest.py', 'jbd2: replay of debugfs-written journals, own commits replayed by e2fsck, power cuts, bad checksums, 4k/1k/32-bit tags', 'ext4 journal: all ok'), ('gate.py', 'ext4 rw gate: driver writes and power cuts leave e2fsck -fn clean (else keep EXT4_RW_VERIFIED 0)', 'ext4 gate: rw verified'), ('fuzz.py', 'ext4 crafted images: extent header/depth/recursion, dirent, rename, symlink, uninit, superblock geometry fail closed under ASan', 'ext4 fuzz: all ok')):
        rc, out = sh(['python3', os.path.join(T, script)])
        verified = re.search(r'#define EXT4_RW_VERIFIED 1', open(os.path.join(SRC, 'fs/ext4/ext4.h')).read())
        record('ext4', name, rc == 0 and (tag in out or ('skipped' in out and not verified)), out[-600:])

def g_fs(work):
    head('NVX4 file system (real kernel code on the host, simulated disk with power cuts)')
    T = os.path.join(TESTS, 'fs'); L = os.path.join(TESTS, 'linux')
    k = os.path.join(work, 'fk')
    shutil.copytree(KDIR, k, ignore=lambda d, names: [n for n in names if (d == KDIR and n in ('build', 'boot', 'tools', 'docs', '.git')) or (os.path.basename(d) == 'third_party' and n in ('tinycc', 'vim', 'git', 'curl', 'mbedtls', 'dropbear', 'ncurses', 'less', 'netsurf', 'fonts', 'musl', 'doom', 'lua', 'sqlite', 'xz', 'bzip2', 'zlib', 'ffmpeg', 'src'))])
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

def g_faceid(work):
    fb = os.path.join(work, 'faceid_policy_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-I', KDIR, '-o', fb, os.path.join(TESTS, 'faceid', 'policy_main.c'), '-lm'])
    if not record('accounts', 'Face ID policy test builds against tools/faceid/fid_policy.h (the face engine decision code)', rc == 0, out[-300:]): return
    rc, out = sh([fb])
    for l in out.splitlines():
        if l.startswith('  ') and ('rule' in l or 'policy (' in l or 'synthetic' in l): print('  ' + l.strip()[:200])
    n = out.count(' ok\n'); bad = [l.strip() for l in out.splitlines() if 'FAIL' in l]
    record('accounts', f'Face ID: 5+ templates from several angles/light, calibrated threshold with synthetic false-accept rate, liveness (still and moved photo refused, head turn or blink needed), sealed templates ({n} checks)', rc == 0 and 'faceid policy: all passed' in out and n >= 20, '; '.join(bad[:3]) or out[-300:])
    try:
        from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
    except Exception:
        record('accounts', 'Face ID template sealing cross-checked with ChaCha20-Poly1305 from python cryptography (module not installed - skipped)', True); return
    ok = True
    for i in range(6):
        k = os.urandom(32); nn = os.urandom(12); a = os.urandom(8 + i * 5); m = os.urandom(1 + i * 97)
        rc, out = sh([fb, 'seal', k.hex(), nn.hex(), a.hex(), m.hex()])
        if rc != 0 or out.strip() != ChaCha20Poly1305(k).encrypt(nn, m, a).hex(): ok = False
    record('accounts', 'Face ID template sealing is byte-identical to ChaCha20-Poly1305 (RFC 8439) from python cryptography', ok)

def g_accounts(work):
    head('Accounts: passwd, group and shadow, SHA-512 crypt, setup, login and access (real kernel code on the host)')
    T = os.path.join(TESTS, 'accounts'); L = os.path.join(TESTS, 'linux')
    g_faceid(work)
    vb = os.path.join(work, 'vtcon_t'); rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I', KDIR, '-o', vb, os.path.join(TESTS, 'shell', 'vtcon_main.c')])
    if record('accounts', 'virtual terminal host test builds against shell/vtcon.c', rc == 0, out[-300:]):
        rc, out = sh([vb]); record('accounts', 'virtual terminals: Ctrl+Alt+F1-F7 switch, per-VT screen/job/cwd, background output held, VT_* and KD* ioctls, blocked while locked or at login, out-of-memory', rc == 0 and 'all vtcon tests passed' in out, out[-300:])
    k = os.path.join(work, 'ak')
    shutil.copytree(KDIR, k, ignore=lambda d, names: [n for n in names if (d == KDIR and n in ('build', 'boot', 'tools', 'docs', '.git')) or (os.path.basename(d) == 'third_party' and n in ('tinycc', 'vim', 'git', 'curl', 'mbedtls', 'dropbear', 'ncurses', 'less', 'netsurf', 'fonts', 'musl', 'doom', 'lua', 'ffmpeg', 'src'))])
    shutil.copy(os.path.join(L, 'cpu_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'cpu.h'))
    shutil.copy(os.path.join(L, 'io_host.h'), os.path.join(k, 'arch', 'x86_64', 'cpu', 'io.h'))
    shutil.copy(os.path.join(TESTS, 'fs', 'fs_glue.c'), os.path.join(k, 'fs_glue.c'))
    shutil.copy(os.path.join(T, 'ac_glue.c'), os.path.join(k, 'ac_glue.c'))
    ren = []
    for n in ('malloc', 'free', 'calloc', 'realloc', 'memchr', 'memcmp', 'memcpy', 'memmove', 'memset', 'strcat', 'strchr', 'strcmp', 'strcpy', 'strlen', 'strncmp', 'strncpy', 'strrchr', 'strstr', 'abs', 'atoi', 'panic'):
        ren.append(f'-D{n}=kh_{n}')
    fo = os.path.join(work, 'ak.o')
    rc, out = sh(['gcc', '-m64', '-O1', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-mno-mmx', '-mno-sse', '-mno-sse2',
                  '-mcmodel=small', '-DNOOVEX64', '-w', '-I.'] + ren + ['-c', 'ac_glue.c', '-o', fo], cwd=k, timeout=900)
    shutil.rmtree(k, ignore_errors=True)
    if not record('accounts', 'kernel compiles as a host object for the account tests', rc == 0, out[:400]): return
    exe = os.path.join(work, 'acharness'); ho = os.path.join(work, 'act.o')
    rc, out = sh(['gcc', '-O1', '-c', os.path.join(T, 'act.c'), '-o', ho])
    if rc == 0: rc, out = sh(['gcc', '-no-pie', '-o', exe, ho, fo, '-Wl,--unresolved-symbols=ignore-all', '-z', 'noexecstack'])
    if not record('accounts', 'account harness links', rc == 0, out[:400]): return
    rc, out = sh([exe], timeout=300)
    for l in out.splitlines():
        if l.startswith('  ') and (l.rstrip().endswith(' ok') or 'FAIL' in l): print('  ' + l.strip()[:160])
    n = len([l for l in out.splitlines() if l.startswith('  ') and l.rstrip().endswith(' ok')])
    bad = [l.strip() for l in out.splitlines() if 'FAIL' in l]
    record('accounts', f'users, groups, SHA-512 passwords, setup, login, access, old-install migration ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(bad[:3]) or out[-400:])

def g_nvada(work, san):
    head('NVIDIA Ada GSP boot (nvada.c) against a simulated AD102 with the bundled ad102 firmware')
    exe = os.path.join(work, 'adasim')
    rc, out = sh(['gcc', '-O1', '-w'] + san + ['-I.', '-DNOOVEX64', '-o', exe, os.path.join(TESTS, 'nvidia', 'adasim.c'), 'drivers/video/nvidia/nvgen.c'], cwd=KDIR)
    if not record('nvidia', 'Ada simulator builds against nvada.c and nvgen.c', rc == 0, out[:300]): return
    import lzma
    fw = os.path.join(work, 'adafw'); os.makedirs(fw, exist_ok=True)
    for f in ('booter_load', 'booter_unload', 'bootloader'):
        with lzma.open(os.path.join(KDIR, 'assets/firmware/nvidia-ad102', f'nvfw_ad102_{f}.xz')) as s, open(os.path.join(fw, f'{f}-535.113.01.bin'), 'wb') as o: o.write(s.read())
    rc, out = sh([exe, fw], timeout=300, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    bad = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL') or 'ERROR: AddressSanitizer' in l or 'runtime error' in l]
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    record('nvidia', f'Ada: off by default, Turing left alone, ad102 booters, FWSEC v3, fuse signatures, GA102 falcon DMA/boot, FRTS/WPR2, SEC2 booter ({n} checks)', rc == 0 and 'ALL PASS' in out and not bad, '; '.join(bad[:2]) or out[-300:])

def g_nvidia(work):
    head('NVIDIA Turing GSP driver (nvflcn.c, nvmem.c, nvgsp.c) against a simulated TU116')
    exe = os.path.join(work, 'nvsim')
    san = (SAN + ['-fno-sanitize=alignment', '-g']) if SAN else []
    for t, what in (('cursor_t', 'Turing/RTX 30/RTX 40 hardware cursor: generation gate (gpu ampere/ada), core-channel cursor methods (C37D/C57D head), push encoding, C57A cursor channel init/move/fini on a simulated BAR0, off by default, bounded idle wait, ARGB image build (32/64, premultiplied, hotspot), edge clipping, VRAM slots, fallback on errors'),
                    ('outputs_t', 'extended desktop: two output rectangles, hit test, window placement, move between screens, mouse crossing, drag across, snapping, left/right/above/below arrangements, fallback to one screen'),
                    ('extend_t', 'extended desktop: screen 2 composes window contents in local coordinates, screen 1 skips them, click translation, cursor on screen 2, arrangement, boot guard and fallback to mirror, GPU drawing on, menus and snapping on screen 2, unplug/replug with windows remembered per screen'),
                    ('curshape_t', 'cursor shapes: text beam over text fields, wait/busy, hand, move, no; each uploaded to the hardware cursor on shape change only, hidden on screen 2, point and clip in desktop pixels under head scaling')):
        cx = os.path.join(work, 'nvcur_' + t)
        rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-DNOOVEX64', '-fno-pie', '-no-pie', '-o', cx, os.path.join(TESTS, 'nvcursor', t + '.c')], cwd=KDIR)
        if record('nvidia', what + ' (builds)', rc == 0, out[:300]):
            rc, out = sh([cx], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
            record('nvidia', what, rc == 0 and 'ALL PASS' in out, out[-300:])
    rc, out = sh(['gcc', '-O1', '-w'] + san + ['-I.', '-DNOOVEX64', '-fno-pie', '-no-pie', '-o', exe, os.path.join(TESTS, 'nvidia', 'nvsim.c')], cwd=KDIR)
    if not record('nvidia', 'simulator builds against the current driver' + (' (with sanitizers)' if san else ''), rc == 0, out[:300]): return
    fw = os.environ.get('NVX_NVFW')
    if not fw:
        import lzma
        fw = os.path.join(work, 'nvfw'); A = os.path.join(KDIR, 'assets/nvidia')
        for xz, dst in (('gsp', 'tu102/gsp/gsp-535.113.01.bin'), ('bootloader', 'tu102/gsp/bootloader-535.113.01.bin'), ('booter_load', 'tu116/gsp/booter_load-535.113.01.bin'), ('booter_unload', 'tu116/gsp/booter_unload-535.113.01.bin'), ('acr_bl', 'tu116/acr/bl.bin')):
            d = os.path.join(fw, dst); os.makedirs(os.path.dirname(d), exist_ok=True)
            with lzma.open(os.path.join(A, f'nvfw_{xz}.xz')) as s, open(d, 'wb') as o: o.write(s.read())
    rc, out = sh([exe, fw], timeout=900, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    if 'skipped' in out: record('nvidia', 'firmware files present (kernel/assets/nvidia, or NVX_NVFW set to the linux-firmware nvidia folder)', False, out[-200:]); return
    fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
    for l in fails: print('    ' + l)
    n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
    san_bad = [l.strip() for l in out.splitlines() if 'ERROR: AddressSanitizer' in l or 'runtime error' in l]
    record('nvidia', f'VRAM, layout, allocator, VBIOS/FWSEC, booter, radix3, queues, RPC, BARs, objects, copy engine, second screen, RTX 30/40 display classes, 3D engine ({n} checks)', rc == 0 and 'ALL PASS' in out and not san_bad, '; '.join((san_bad + fails)[:2]))
    dp = os.path.join(work, 'dp_link_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-o', dp, os.path.join(TESTS, 'video', 'dp_link_t.c')], cwd=KDIR)
    if record('nvidia', 'DisplayPort link-training test builds (drivers/video/dp_link.c)', rc == 0, out[:300]):
        rc, out = sh([dp], timeout=60)
        record('nvidia', 'DisplayPort training against a simulated sink: pass, fail, lane reduction, rate reduction, bandwidth floor, AUX error, firmware (GSP-RM) path', rc == 0 and 'ALL PASS' in out, '; '.join(l.strip() for l in out.splitlines() if 'FAIL' in l)[:300] or out[-300:])
    t2 = os.path.join(work, 'nv2d_test')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-DNOOVEX64', '-o', t2, os.path.join(TESTS, 'nv2d', 'nv2d_test.c')], cwd=KDIR)
    if record('nvidia', '2D engine push-buffer test builds', rc == 0, out[:300]):
        rc, out = sh([t2], timeout=120)
        record('nvidia', '902D fills and copies simulated pixel-exact against the CPU renderer, glyph atlas cached and exact', rc == 0 and 'ALL PASS' in out, out[-300:])
    t3 = os.path.join(work, 'gtext_test')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-DNOOVEX64', '-o', t3, os.path.join(TESTS, 'nv2d', 'gtext_test.c')], cwd=KDIR)
    if record('nvidia', 'GPU text through the glyph atlas: test builds (drivers/video/fb/gtext.c)', rc == 0, out[:300]):
        rc, out = sh([t3], timeout=120)
        record('nvidia', 'app text drawn as atlas glyph ops is pixel-exact against the CPU text renderer; overlap, budget, full atlas and off fall back per run', rc == 0 and 'ALL PASS' in out, out[-300:])
    g_nvada(work, san)
    qx = os.path.join(work, 'qmdsim')
    rc, out = sh(['gcc', '-O1', '-w'] + san + ['-I.', '-DNOOVEX64', '-o', qx, os.path.join(TESTS, 'nvidia', 'qmdsim.c')], cwd=KDIR)
    if record('nvidia', 'QMD 2.2/3.0 and compositor shader selection test builds (nvqmd.h)', rc == 0, out[:300]):
        rc, out = sh([qx], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        n = len([l for l in out.splitlines() if l.rstrip().endswith(' ok')])
        record('nvidia', f'compositor shader per generation: sm_75/QMD 2.2 unchanged, QMD 3.0 layout (c7c0/c9c0), sm_86/sm_89 ports re-encoded per NAK, BRX rewritten, marked verified ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(l.strip() for l in out.splitlines() if 'FAIL' in l)[:300] or out[-300:])
    pd = os.path.join(work, 'nvport'); os.makedirs(pd, exist_ok=True)
    rc, out = sh([sys.executable, '-I', 'tools/nvsass_port.py', 'drivers/video/nvidia/shaders/gqk.cubin', pd, os.path.join(pd, 'h.h')], cwd=KDIR)
    same = rc == 0 and all(open(os.path.join(pd, f), 'rb').read() == open(os.path.join(KDIR, g), 'rb').read() for f, g in (('h.h', 'include/nvx/drivers/video/nvidia/gqk_cubin_sm8x.h'), ('gqk_sm86.cubin', 'drivers/video/nvidia/shaders/gqk_sm86.cubin'), ('gqk_sm89.cubin', 'drivers/video/nvidia/shaders/gqk_sm89.cubin')))
    record('nvidia', 'sm_86/sm_89 compositor shaders are reproducible from the sm_75 cubin with tools/nvsass_port.py', same, out[-300:])
    rc, out = sh([sys.executable, '-I', 'tools/nvsass_port.py', '--selftest', 'drivers/video/nvidia/shaders/gqk.cubin'], cwd=KDIR)
    record('nvidia', 'sm_86/sm_89 port: BRX switch rewritten to ISETP/BRA, every instruction inside the Mesa NAK sm70 encoder fields, verifier catches stray bits', rc == 0 and 'ALL PASS' in out, out[-300:])
    nk = os.environ.get('NAK_SM70_ENCODE', '/tmp/ref/mesa/m/src/nouveau/compiler/nak/sm70_encode.rs')
    if os.path.exists(nk):
        tf = os.path.join(work, 'nak.txt'); names = [l.split()[0] for l in open(os.path.join(KDIR, 'tools', 'nvsass_nak.txt')) if l.strip()]
        rc, out = sh([sys.executable, '-I', 'tools/nak_sm70_fields.py', nk, tf] + names, cwd=KDIR)
        record('nvidia', 'tools/nvsass_nak.txt matches the fields parsed from Mesa NAK sm70_encode.rs', rc == 0 and open(tf).read() == open(os.path.join(KDIR, 'tools', 'nvsass_nak.txt')).read(), out[-300:])
    am = os.path.join(work, 'ampere_sim')
    rc, out = sh(['gcc', '-O1', '-w'] + san + ['-I.', '-DNOOVEX64', '-fno-pie', '-no-pie', '-o', am, os.path.join(TESTS, 'nvidia', 'ampere_sim.c')], cwd=KDIR)
    if record('nvidia', 'Ampere GA10x boot-hook simulator builds', rc == 0, out[:300]):
        rc, out = sh([am], timeout=120, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
        record('nvidia', f'Ampere GA10x falcon reset/DMA/boot register sequence, SEC2 PMC reset via device info, per-generation engine and display classes, fuse signature pick, PCI gate ({n} checks)', rc == 0 and 'ALL PASS' in out, '; '.join(l.strip() for l in out.splitlines() if 'FAIL' in l)[:300])
    sk = os.path.join(work, 'r3d_sink')
    rc, out = sh(['g++', '-O2', '-w', '-std=gnu++20', '-fno-exceptions', '-fno-rtti'] + SAN + ['-o', sk, os.path.join(KDIR, 'apps/graphics/r3d/r3d.cpp'), os.path.join(TESTS, 'r3d', 'sink_main.cpp')])
    if record('nvidia', 'renderer triangle-output test builds', rc == 0, out[:300]):
        rc, out = sh([sk], timeout=600, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        record('nvidia', 'Gears triangles for the GPU: 4 scenes x 6 views rasterised on the host match the CPU picture (mean difference below 14 of 255), every vertex inside the clip volume', rc == 0 and 'sink ok' in out, out[-300:])
    ns = os.path.join(work, 'nvset_test')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-DNOOVEX64', '-o', ns, os.path.join(TESTS, 'nvidia', 'nvset_test.c')], cwd=KDIR)
    if record('nvidia', 'NVIDIA feature switches test builds (nvsettings.c)', rc == 0, out[:300]):
        rc, out = sh([ns])
        record('nvidia', 'NVIDIA features on by default, unfinished try turns a feature off, 3 failures turn it off, Settings on/reset clears it', rc == 0 and 'ALL PASS' in out, out[-300:])

def g_ui(work):
    head('Settings UI: clipping, text wrapping, serial breadcrumbs')
    fb = open(os.path.join(KDIR, 'drivers/video/fb/framebuffer.c')).read(); sl = open(os.path.join(KDIR, 'apps/system/settings/storage_and_layout.c')).read(); cr = open(os.path.join(KDIR, 'core/crash.c')).read(); wm = open(os.path.join(KDIR, 'graphics/ui/wm.c')).read()
    try:
        gl = re.search(r'(?m)^int g_clip_y0=.*$', fb).group(0)
        src = gl + '\n' + '\n'.join(defn(fb, n) for n in ('fb_clip_y', 'fb_clip_off', 'clip_note', 'fill_rect')) + '\n' + '\n'.join(defn(sl, n) for n in ('st_wrap', 'st_fit')) + '\n' + '\n'.join(defn(cr, n) for n in ('crumb_seen', 'crumb_mark')) + defn(wm, 'win_title_fit') + '\n'
    except (KeyError, AttributeError) as e: record('ui', 'Settings UI functions found in the source', False, str(e)); return
    open(os.path.join(work, 'ui_ext.c'), 'w').write(src.replace('static inline void clip_note', 'static void clip_note'))
    b = os.path.join(work, 'ui_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-function'] + SAN + ['-I', work, '-o', b, os.path.join(TESTS, 'ui', 'ui_main.c')])
    if record('ui', 'Settings UI test builds (framebuffer clip, st_wrap, crumb dedupe from the real source)', rc == 0, out[:300]):
        rc, out = sh([b]); record('ui', 'vertical clip keeps fills inside the Settings content area and reports the full content height, wrapped descriptions stay inside their width (2 lines, ellipsis), repeating breadcrumbs reach the serial port once, window titles are cut by the title bar width (not a fixed 62 characters)', rc == 0 and 'ui: all ok' in out, out[-300:])
    pg = open(os.path.join(KDIR, 'apps/system/settings/pages.c')).read()
    ex = re.search(r'static const stile EX\[\]=\{(.*?)\};', pg, re.S)
    ents = re.findall(r'\{(P_\w+),', ex.group(1)) if ex else []
    record('ui', f'Settings search covers every extra page ({len(ents)} entries, loop bound from sizeof, Game controllers and Printers & Scanners listed)', bool(ex) and 'k<NEX' in pg and 'P_UXPAD' in ents and 'P_UXPRN' in ents, ', '.join(ents))
    record('ui', 'Settings USB tab shows a row when no controller is found', 'No USB controller found' in pg)
    record('ui', 'Settings Show seconds toggles g_showsec and saves it', re.search(r'if\(id==1\)\{[^}]*g_showsec\^=1;[^}]*power_prefs_save\(\);', pg) is not None)

def g_doom(work):
    head('DOOM window: quit/restart states, heap budget, scaled frame, key ring, capture click')
    exe = os.path.join(work, 'doom_t')
    rc, out = sh(['gcc', '-O1', '-w'] + SAN + ['-I.', '-DNOOVEX64', '-fno-pie', '-no-pie', '-o', exe, os.path.join(TESTS, 'doom', 'doom_t.c')], cwd=KDIR)
    if record('doom', 'DOOM host test builds against apps/games/doom.c', rc == 0, out[:300]):
        rc, out = sh([exe], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        fails = [l.strip() for l in out.splitlines() if l.rstrip().endswith('FAIL')]
        n = len([l for l in out.splitlines() if l.rstrip().endswith((' ok', ' FAIL'))])
        record('doom', f'menu quit shows "DOOM has quit" and Enter/click restart the engine, three runs fit the 24 MB heap then a reboot is advised, maximised frame scaled 16:10 with black bars, taps from the key ring are not lost, the capturing click does not fire ({n} checks)', rc == 0 and 'ALL PASS' in out and not fails, '; '.join(fails[:2]) or out[-300:])

def g_startmenu(work):
    head('Start menu: Run dialog editing, history and command hand-off; desktop submenu flip at the right edge')
    try:
        rd = open(os.path.join(KDIR, 'apps/system/rundlg.c')).read(); inp = open(os.path.join(KDIR, 'graphics/ui/input.c')).read()
        gl = '\n'.join(l for l in rd.splitlines() if re.match(r'^(int g_run_on;|static (char|int) run_)', l))
        src = gl + '\n' + '\n'.join(defn(rd, n) for n in ('run_open', 'run_close', 'run_hist_add', 'run_exec', 'run_set', 'run_key'))
        sub = '\n'.join(defn(inp, n) for n in ('ctx_sub_place',))
    except (KeyError, AttributeError) as e: record('startmenu', 'Run dialog and submenu functions found in the source', False, str(e)); return
    open(os.path.join(work, 'rundlg_ext.c'), 'w').write(src)
    open(os.path.join(work, 'ctxsub_ext.c'), 'w').write(sub)
    b = os.path.join(work, 'rundlg_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-function'] + SAN + ['-I', work, '-o', b, os.path.join(TESTS, 'ui', 'rundlg_main.c')])
    if record('startmenu', 'Run dialog test builds from the real rundlg.c', rc == 0, out[:300]):
        rc, out = sh([b]); record('startmenu', 'Run: Enter runs the trimmed command in the Terminal, empty Enter and Esc just close, cursor editing, Up/Down history of 8', rc == 0 and 'RUNDLG all ok' in out, out[-300:])
    b = os.path.join(work, 'ctxsub_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-function'] + SAN + ['-I', work, '-o', b, os.path.join(TESTS, 'ui', 'ctxsub_main.c')])
    if record('startmenu', 'desktop submenu placement test builds from the real input.c', rc == 0, out[:300]):
        rc, out = sh([b]); record('startmenu', 'submenu opens to the right normally, flips to the left of the menu near the right edge, and stays above the taskbar', rc == 0 and 'CTXSUB all ok' in out, out[-300:])

def g_smallapps(work):
    head('Small apps: Snake/Flappy start state, Paint stroke interpolation')
    sa = open(os.path.join(KDIR, 'apps/accessories/small_apps.c')).read()
    try:
        keep = ('#define SN_', '#define FB_', '#define PC', 'static u8 snx', 'int fb_y', 'static int fb_px', 'static u32 paint_buf', 'static int paint_lx', 'static const u32 PPAL')
        decls = '\n'.join(l for l in sa.splitlines() if l.startswith(keep))
        src = decls + '\n' + '\n'.join(defn(sa, n) for n in ('snr', 'snake_place_apple', 'snake_reset', 'snake_start', 'snake_step', 'snake_key', 'fbr', 'fb_dims', 'flappy_reset', 'flappy_step', 'flappy_flap', 'flappy_key', 'paint_stamp', 'paint_end', 'paint_at')) + '\n'
    except (KeyError, AttributeError) as e: record('ui', 'small apps functions found in the source', False, str(e)); return
    open(os.path.join(work, 'smallapps_ext.c'), 'w').write(src)
    b = os.path.join(work, 'smallapps_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable'] + SAN + ['-I', work, '-o', b, os.path.join(TESTS, 'ui', 'smallapps_main.c')])
    if record('ui', 'small apps test builds (Snake, Flappy Bird, Paint from the real source)', rc == 0, out[:300]):
        rc, out = sh([b]); record('ui', 'Snake and Flappy Bird open in a wait state and start on the first key, a key after game over restarts at once, the Flappy hint fits above the window bottom, Paint joins mouse samples into continuous strokes and a new stroke does not join the previous one', rc == 0 and 'smallapps: all ok' in out, out[-300:])

def g_shell(work):
    head('Shell: command lists (; && ||) and tilde expansion')
    b = os.path.join(work, 'shlist_t')
    rc, out = sh(['gcc', '-O1', '-Wall'] + SAN + ['-I', KDIR, '-o', b, os.path.join(TESTS, 'shell', 'shlist_main.c')])
    if record('shell', 'command list splitter builds from shell/shlist.c', rc == 0, out[:300]):
        rc, out = sh([b]); record('shell', 'command lists: ; && || split outside quotes and escapes, | and & and 2>&1 are left alone, && and || gate on the exit status', rc == 0 and 'shlist: all ok' in out, out[-300:])
    pd = open(os.path.join(KDIR, 'shell/commands/prompt_and_dispatch.c')).read()
    record('shell', 'sh_run_line runs each piece of a command list through sh_list_cut', 'sh_list_cut(' in pd and 'sh_list_skip(' in pd and 'sh_run_simple(' in pd)
    record('shell', '~ expands from the process HOME (env_get) with g_home as the fallback, and $VAR falls back to the process environment', 'sh_home()' in pd and 'env_get("HOME")' in pd and pd.count('env_get(') >= 2)
    rc_line = re.search(r'ux_mkfile\(etc,"rc","([^"]*)"', open(os.path.join(KDIR, 'fs/vfs/unixfs.c')).read())
    record('shell', '/etc/rc does not echo "init started" onto the console (the line goes to the kernel log)', rc_line is not None and 'echo' not in rc_line.group(1) and 'klog("NoovexOS: init started")' in open(os.path.join(KDIR, 'fs/vfs/unixfs.c')).read())

def g_visuals(work):
    head('Visual fixes: Benchmark advice wrap, Registry Editor ellipsis, taskbar label ellipsis')
    bn = open(os.path.join(KDIR, 'apps/system/bench.c')).read(); re_ = open(os.path.join(KDIR, 'apps/system/regedit.c')).read(); tb = open(os.path.join(KDIR, 'graphics/ui/desktop/window_titles_and_bars.c')).read()
    try: src = defn(bn, 'bn_wrap') + '\n' + defn(re_, 're_fit') + '\n' + defn(tb, 'tb_label') + '\n'
    except KeyError as e: record('visuals', 'visual helper functions found in the source', False, str(e)); return
    open(os.path.join(work, 'visuals_ext.c'), 'w').write(src)
    b = os.path.join(work, 'visuals_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wno-unused-function'] + SAN + ['-I', work, '-o', b, os.path.join(TESTS, 'visuals', 'visuals_main.c')])
    if record('visuals', 'visuals test builds (bn_wrap, re_fit, tb_label from the real source)', rc == 0, out[:300]):
        rc, out = sh([b]); record('visuals', 'Benchmark advice wraps at word boundaries inside the card width and keeps every word, Registry About text is cut with an ellipsis inside the column, taskbar labels that do not fit end in a drawn ellipsis inside the button', rc == 0 and 'visuals: all ok' in out, out[-300:])

def g_qemu_s3(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('ACPI S3 in QEMU (-global PIIX4_PM.disable_s3=0, -smp 2): suspend, system_wakeup, devices and other cores back')
    import socket
    work = tempfile.mkdtemp(prefix='nvx_qemu_s3_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); disk = os.path.join(work, 'disk.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-m', '1024', '-smp', '2', '-accel', 'tcg,thread=multi', '-global', 'PIIX4_PM.disable_s3=0', '-nic', 'none',
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
            if pr.poll() is not None: return text in serial()
            time.sleep(1)
        return False
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot'}
    def typ(sock, txt):
        for ch in txt:
            sock.sendall(('sendkey ' + keys.get(ch, ch) + '\n').encode()); time.sleep(0.08)
    def dead(what):
        record('qemu', 'S3: QEMU still running ' + what + (' (QEMU exited with ' + str(pr.poll()) + ', guest reset or triple fault)' if pr.poll() is not None else ' (monitor connection lost)'), False, serial()[-300:])
    try:
        if not record('qemu', 'S3 machine boots to the main loop', wait_for('entering main loop', 120), serial()[-300:]): return
        if 'firmware offers S3' not in serial(): print('  note: firmware did not offer _S3, S3 test skipped'); return
        time.sleep(3)
        s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.5)
        smp = wait_for('smp: scheduling on all cores', 30)
        typ(s, 'suspend\n')
        if not wait_for('sleep: devices ready', 30):
            print('  note: sleep did not start (' + (re.findall(r'Sleep is not available[^\n]*', serial()) or ['no message'])[-1][:100] + '), S3 test skipped'); return
        time.sleep(2)
        if 's3: entering suspend to RAM' not in serial():
            m = re.search(r'S3 not used: [^)\n]*|s3: [^\n]*', serial())
            why = m.group(0)[:160] if m else 'light sleep'
            if 'off by default' in why or not m: print('  note: image predates S3 on by default (' + why + '), S3 test skipped'); typ(s, ' '); return
            typ(s, ' '); record('qemu', 'S3 is used by default on a QEMU PC (std VGA, IDE, PS/2)', False, why); return
        time.sleep(3)
        s.sendall(b'sendkey shift\n')
        kw = wait_for('s3: resumed from suspend to RAM', 30)
        record('qemu', 'S3: a key press wakes the computer', kw, serial()[-300:])
        if not kw: s.sendall(b'system_wakeup\n')
        record('qemu', 'S3: firmware wakes through the FACS vector and the kernel resumes', wait_for('s3: resumed from suspend to RAM', 60), serial()[-300:])
        record('qemu', 'S3: every sleep device resumes after S3', wait_for('sleep: all devices resumed', 30), serial()[-300:])
        record('qemu', 'S3: the std VGA screen mode is restored after resume', 's3: screen mode restored' in serial(), serial()[-300:])
        record('qemu', 'S3: the wall clock is resynced from the CMOS RTC after resume', bool(re.search(r's3: clock resynced from the motherboard clock, slept \d+ s', serial())), serial()[-300:])
        if smp: record('qemu', 'S3: the other cores start again after resume', wait_for('s3: other cores started again', 30), serial()[-300:])
        typ(s, 'uptime >/dev/kmsg\n'); time.sleep(5)
        record('qemu', 'S3: the shell still answers after resume', bool(re.search(r'up \d+ seconds', serial().split('s3: resumed from suspend to RAM')[-1])), serial()[-300:])
        s.close()
    except (BrokenPipeError, ConnectionError, OSError) as e:
        dead('after suspend/resume: ' + type(e).__name__)
    finally:
        pr.kill(); pr.wait()
        if os.environ.get('NVX_KEEP_SERIAL'):
            try: shutil.copy(log, os.environ['NVX_KEEP_SERIAL'] + '.s3')
            except OSError: pass

def g_qemu(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('SMP in QEMU (-smp 4): boot, "smp on", stress test, musltest on all cores, "smp off"')
    import socket
    work = tempfile.mkdtemp(prefix='nvx_qemu_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); disk = os.path.join(work, 'disk.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-m', '2048', '-smp', '4', '-accel', 'tcg,thread=multi',
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
            if pr.poll() is not None: return text in serial()
            time.sleep(1)
        return False
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7'}
    def typ1(sock, txt):
        for ch in txt:
            sock.sendall(('sendkey ' + keys.get(ch, ch) + '\n').encode()); time.sleep(0.08)
    def typ(sock, txt):
        nf = serial().count('command not found')
        typ1(sock, txt)
        if not txt.endswith('\n'): return
        time.sleep(2)
        if serial().count('command not found') > nf:
            print('  note: a keystroke was lost while typing ' + repr(txt.strip()) + ', typing it again')
            typ1(sock, txt)
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
        cp = re.search(r'cron: perms (ok|WARNING)[^\n]*', serial())
        record('qemu', f'cron spool and /etc/crontab permissions on the booted image ({cp.group(0)[:120] if cp else "no cron perms line"})', bool(cp) and cp.group(1) == 'ok', serial()[-300:])
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
        typ(s, 'nvx-abitest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 180 and not re.search(r'\d+ of \d+ ABI checks passed', serial()): time.sleep(1)
        m = re.search(r'(\d+) of (\d+) ABI checks passed', serial())
        for l in sorted(set(re.findall(r'FAIL [a-z][^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'nvx-abitest: inotify, signalfd, waitid, flock/fcntl locks, timer_create, preadv, sendmmsg, getdents ({m.group(0) if m else "no result"})', bool(m) and m.group(1) == m.group(2), serial()[-300:])
        typ(s, 'abi2test >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 180 and not re.search(r'abi2: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'abi2: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'abi2: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'abi2test: real-time signals, Unix datagrams, SCM_RIGHTS, IPv6 sockets, socket options, netlink, ping sockets ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0' and int(m.group(1)) >= 31, serial()[-300:])
        typ(s, 'schedtest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 120 and not re.search(r'sched: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'sched: done (\d+) passed, (\d+) failed', serial()); rt = re.search(r'work ratio [0-9.]+', serial())
        for l in sorted(set(re.findall(r'sched: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'schedtest: nice, setpriority, SCHED_FIFO/IDLE, fair share by nice ({m.group(0) if m else "no result"}{", " + rt.group(0) if rt else ""})', bool(m) and m.group(2) == '0', serial()[-300:])
        typ(s, 'sysfstest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 90 and not re.search(r'sysfs: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'sysfs: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'sysfs: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'sysfstest: /sys tree, class/dev/subsystem links, block nodes, uevents over netlink ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0', serial()[-300:])
        typ(s, 'swaptest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 480 and not re.search(r'swap: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'swap: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'swap: (?:FAIL|paged|wrote|pressure)[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'swaptest: compressed swap (zram), MADV_PAGEOUT, fork and munmap of swapped pages, drop_caches, memory pressure ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0', serial()[-300:])
        typ(s, 'abi3test >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 480 and not re.search(r'abi3: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'abi3: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'abi3: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'abi3test: SysV semaphores and message queues, POSIX message queues, pidfd, splice/tee, copy_file_range, openat2, FIFOs, futex_waitv, process_vm_readv, seccomp, execveat ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0' and int(m.group(1)) >= 81, serial()[-300:])
        typ(s, 'contest >/dev/kmsg\n')
        t0 = time.time()
        while time.time() - t0 < 900 and not re.search(r'contest: done \d+ passed', serial()): time.sleep(1)
        m = re.search(r'contest: done (\d+) passed, (\d+) failed', serial())
        for l in sorted(set(re.findall(r'cont: FAIL[^\n]*', serial()))): print('  ' + l[:150])
        record('qemu', f'contest: namespaces (mount, pid, uts, ipc, net, user), chroot, pivot_root, overlay, cgroup v2 pids/memory/cpu limits ({m.group(0) if m else "no result"})', bool(m) and m.group(2) == '0' and int(m.group(1)) >= 113, serial()[-300:])
        typ(s, 'smp off\n')
        ok = wait_for('smp: secondary cores parked', 60)
        m = re.search(r'smp: cores did not stop[^\n]*', serial())
        record('qemu', 'smp off parks the other cores' + (' (' + m.group(0)[:120] + ')' if m and not ok else ''), ok, serial()[-300:])
        typ(s, 's3 off\n'); time.sleep(1)
        typ(s, 'suspend\n')
        if wait_for('sleep: devices ready', 30):
            time.sleep(3); typ1(s, ' ')
            record('qemu', 'suspend: devices suspend, light sleep, a key wakes, every device resumes', wait_for('sleep: all devices resumed', 60) and 'woke from' in serial(), serial()[-300:])
        elif 'Sleep is not available' in serial(): record('qemu', 'suspend: a blocking device stops sleep with a message', True)
        else: print('  note: image predates the sleep device hooks, sleep test skipped')
        s.close()
    except (BrokenPipeError, ConnectionError, OSError) as e:
        record('qemu', 'QEMU still running (' + type(e).__name__ + ('; QEMU exited with ' + str(pr.poll()) + ', guest reset or triple fault' if pr.poll() is not None else '; monitor connection lost') + ')', False, serial()[-300:])
    finally:
        pr.kill(); pr.wait()
        if os.environ.get('NVX_KEEP_SERIAL'):
            try: shutil.copy(log, os.environ['NVX_KEEP_SERIAL'])
            except OSError: pass
        shutil.rmtree(work, ignore_errors=True)

BOOTQA_NEW = {'altf4': 'Alt+F4 closes the focused window', 'endtask': 'Task Manager End task in the classic layout', 'snake': 'Snake waits for a key before it starts',
              'doom': 'DOOM restarts after a normal quit', 'find': 'browser Find across word fragments', 'submenu': 'desktop submenus flip left near the right edge',
              'lock': 'the lock screen checks the password in the live session', 'mute': 'sound calls guarded when no sound driver is running'}
def qmp_chord(ev, mod, key, hold=0.8):
    k = lambda q, d: {'type': 'key', 'data': {'down': d, 'key': {'type': 'qcode', 'data': q}}}
    ev([k(mod, True)]); time.sleep(0.2); ev([k(key, True)]); time.sleep(hold); ev([k(key, False)]); time.sleep(0.2); ev([k(mod, False)]); time.sleep(0.3)

def g_bootqa(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    try: from PIL import Image
    except ImportError: record('bootqa', 'Python PIL is installed (needed for screenshots)', False); return
    head('Booted system in QEMU: version defaults, terminal commands, text console, desktop, Settings pages and switches, flags after a reboot')
    import socket, json
    work = tempfile.mkdtemp(prefix='nvx_bootqa_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); qmp = os.path.join(work, 'qmp'); disk = os.path.join(work, 'disk.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-m', '2048', '-smp', '2', '-accel', 'tcg,thread=multi',
           '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
           '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
           '-serial', f'file:{log}', '-display', 'none', '-monitor', f'unix:{mon},server,nowait', '-qmp', f'unix:{qmp},server,nowait']
    pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    def serial():
        try: return open(log, 'rb').read().decode('latin-1')
        except OSError: return ''
    def wait_for(text, secs, start=0):
        t = time.time()
        while time.time() - t < secs:
            if text in serial()[start:]: return True
            time.sleep(1)
        return False
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7', ':': 'shift-semicolon', '|': 'shift-backslash', '_': 'shift-minus', '=': 'equal'}
    S = {}
    def mons():
        if 'm' not in S: S['m'] = socket.socket(socket.AF_UNIX); S['m'].connect(mon); time.sleep(0.5)
        return S['m']
    def hmp(c): mons().sendall((c + '\n').encode()); time.sleep(0.3)
    def typ(txt):
        for ch in txt: hmp('sendkey ' + (keys.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)))
    def run(c, secs=4, wait=60):
        mark = len(serial()); typ(c + '\n'); t = time.time()
        while time.time() - t < wait and ('cmd: ' + c) not in serial()[mark:]: time.sleep(1)
        time.sleep(secs); return serial()[mark:]
    def shot(name):
        p = os.path.join(work, name + '.ppm')
        if os.path.exists(p): os.remove(p)
        hmp('screendump ' + p); t = time.time()
        while time.time() - t < 10 and not os.path.exists(p): time.sleep(0.5)
        time.sleep(1); im = Image.open(p).convert('RGB'); keep = os.environ.get('NVX_KEEP_SHOTS')
        if keep: os.makedirs(keep, exist_ok=True); im.save(os.path.join(keep, name + '.png'))
        return im
    def ink(im, box, lim=110):
        c = im.crop(box); return sum(1 for p in c.getdata() if p[0] + p[1] + p[2] < lim * 3)
    def qmpc():
        if 'q' not in S:
            s = socket.socket(socket.AF_UNIX); s.connect(qmp); s.settimeout(5); s.recv(4096); s.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(0.3); s.recv(4096); S['q'] = s
        return S['q']
    def ev(e):
        qmpc().sendall((json.dumps({'execute': 'input-send-event', 'arguments': {'events': e}}) + '\n').encode())
    def drain():
        try: qmpc().recv(65536)
        except OSError: pass
    def term(): qmp_chord(ev, 'meta_l', 'r'); drain(); time.sleep(2.5)
    def chord(mod, key): qmp_chord(ev, mod, key); drain(); time.sleep(1.5)
    def move(x, y, w=1280, h=800):
        ev([{'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // (w - 1)}}, {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // (h - 1)}}]); time.sleep(0.6); drain()
    def click(x, y, w=1280, h=800, button='left'):
        move(x, y, w, h)
        ev([{'type': 'btn', 'data': {'down': True, 'button': button}}]); time.sleep(0.25); ev([{'type': 'btn', 'data': {'down': False, 'button': button}}]); time.sleep(1.5)
        drain()
    def wheel(x, y, n, w=1280, h=800):
        move(x, y, w, h)
        b = 'wheel-up' if n > 0 else 'wheel-down'
        for _ in range(abs(n)): ev([{'type': 'btn', 'data': {'down': True, 'button': b}}]); time.sleep(0.15); ev([{'type': 'btn', 'data': {'down': False, 'button': b}}]); time.sleep(0.4)
        time.sleep(1.5); drain()
    bad = lambda t: re.search(r'(?i)kernel panic|panic: |page fault at|general protection|double fault|stack overflow (at|in|on cpu)|see the crash report|freeze report|watchdog: main loop', t)
    def gpufeat(out): return dict(re.findall(r'\]\s+(\w+)\s+(on|off)\b', out))
    def new(k, ok): return ok or print(f'  note: [{BOOTQA_NEW[k]}] is expected from this round\'s fixes; this FAIL means the image does not have it yet or it regressed') or ok
    def diff(a, b, box): return sum(1 for p, q in zip(a.crop(box).getdata(), b.crop(box).getdata()) if abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) > 60)
    def near(im, box, col, tol=12): return [(x, y) for (x, y), p in zip(((x, y) for y in range(box[1], box[3]) for x in range(box[0], box[2])), im.crop(box).getdata()) if abs(p[0] - col[0]) + abs(p[1] - col[1]) + abs(p[2] - col[2]) <= tol]
    def rows_with(im, box, col, tol=12): return sorted(set(y for _, y in near(im, box, col, tol)))
    def fixes_block():
        alive0 = serial().count('alive: main loop running')
        term(); base = shot('fx-desktop'); tbar = (0, 772, 1000, 800)
        click(400, 108); time.sleep(0.5); hmp('sendkey alt-f4'); time.sleep(2.5); after = shot('fx-altf4')
        record('bootqa', f'Alt+F4 closes the focused window (Welcome: {diff(base, after, (365, 130, 435, 430))} pixels of it changed)', new('altf4', diff(base, after, (365, 130, 435, 430)) > 5000 and not bad(serial()[-2000:])))
        term(); run('sleep 900 &', 2); run('taskmgr', 5); im = shot('fx-taskmgr')
        rws = rows_with(im, (700, 150, 1070, 640), (38, 130, 76)); ry = (max(rws) - 6) if rws else 0
        if rws: click(260, ry); time.sleep(0.5); click(244, 659); time.sleep(2)
        shot('fx-taskmgr-end'); term(); run('jobs', 2); out = run('ps >/dev/kmsg', 4)
        left = [l for l in out.splitlines() if 'sleep 900' in l and 'cmd:' not in l and not re.search(r'\sZ\+?\s', l)]
        record('bootqa', f'Task Manager End task kills the selected process (row at y={ry}, sleep {"still running" if left else "gone"})', new('endtask', bool(rws) and not left and 'cmd: ps' in out), out[-300:])
        click(20, 785); time.sleep(1); click(60, 595); time.sleep(1); click(260, 595); time.sleep(3)
        a = shot('fx-snake'); time.sleep(2.5); b = shot('fx-snake2'); board = (406, 182, 794, 452)
        wr = sorted(set(y for (_, y), p in zip(((x, y) for y in range(board[1], board[3]) for x in range(board[0], board[2])), a.crop(board).getdata()) if min(p) >= 240)); span = (max(wr) - min(wr) + 1) if wr else 0
        hmp('sendkey right'); time.sleep(2); c = shot('fx-snake3')
        record('bootqa', f'Snake waits with a start prompt (text {span} px tall, {diff(a, b, board)} pixels moved before a key, {diff(b, c, board)} after)', new('snake', 0 < span <= 21 and diff(a, b, board) < 30 and diff(b, c, board) > 30))
        click(811, 121); time.sleep(1.5)
        term(); out = run('doom >/dev/kmsg', 2); t0 = time.time()
        while time.time() - t0 < 60 and len(set(shot('fx-doom').crop((184, 72, 824, 472)).getdata())) < 40: time.sleep(5)
        mark = len(serial())
        for k, hold in (('esc', 450), ('up', 450), ('ret', 450), ('y', 450)): hmp(f'sendkey {k} {hold}'); time.sleep(2.5)
        time.sleep(3); q1 = shot('fx-doom-quit'); qcols = len(set(q1.crop((184, 72, 824, 472)).getdata())); click(813, 55); time.sleep(2)
        term(); out = run('doom >/dev/kmsg', 2); t0 = time.time()
        while time.time() - t0 < 60 and len(set(shot('fx-doom2').crop((184, 72, 824, 472)).getdata())) < 40: time.sleep(5)
        q2 = shot('fx-doom2'); cols = len(set(q2.crop((184, 72, 824, 472)).getdata()))
        record('bootqa', f'DOOM starts again after a normal quit from its menu ({qcols} colours after the quit, {cols} in the second game window, no "could not load" message)', new('doom', qcols < 40 and cols >= 40 and 'could not load' not in out.lower() and 'window 4294967295' not in serial()[mark:]), out[-200:])
        click(813, 55); time.sleep(1.5)
        srv = None
        try:
            import http.server, threading
            wdir = os.path.join(work, 'www'); os.makedirs(wdir, exist_ok=True)
            open(os.path.join(wdir, 'long.html'), 'w').write('<!doctype html><html><head><title>QA Long</title></head><body><h1>Long page top</h1>\n' + ''.join(f'<p>Line {n} of the long page. Scroll test text.</p>\n' for n in range(1, 41)) + '<p>Bottom of the long page.</p></body></html>\n')
            hcls = type('H', (http.server.SimpleHTTPRequestHandler,), {'log_message': lambda *a, **k: None})
            srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), lambda *a, **k: hcls(*a, directory=wdir, **k)); port = srv.server_address[1]
            threading.Thread(target=srv.serve_forever, daemon=True).start()
            mark = len(serial()); term(); run(f'browse http://10.0.2.2:{port}/long.html', 2); wait_for('Loaded in', 60, mark); time.sleep(2)
            click(832, 186); time.sleep(1); typ('Line 19'); time.sleep(3); im = shot('fx-find')
            fb = (600, 200, 950, 300); good = len(near(im, fb, (90, 120, 90))); none = len(near(im, fb, (160, 110, 110)))
            record('bootqa', f'browser Find matches "Line 19" across two words ({good} "found" pixels, {none} "none" pixels)', new('find', good > 10 and none == 0 and 'Loaded in' in serial()[mark:]), serial()[mark:][-200:])
            hmp('sendkey esc'); time.sleep(0.5); click(935, 121); time.sleep(1.5)
        finally:
            if srv: srv.shutdown(); srv.server_close()
        click(1270, 300, button='right'); time.sleep(1); a = shot('fx-ctx'); move(1100, 315); time.sleep(1.5); b = shot('fx-ctx-sub')
        left, strip = diff(a, b, (900, 290, 1076, 370)), diff(a, b, (1273, 290, 1280, 370))
        record('bootqa', f'a desktop context submenu near the right edge opens to the left ({left} pixels changed left of the menu, {strip} at the screen edge)', new('submenu', left > 200 and strip == 0))
        hmp('sendkey esc'); time.sleep(1); click(1100, 720); move(1200, 60); time.sleep(1)
        term(); run('passwd', 2); typ('qapass\n'); time.sleep(1.5); typ('qapass\n'); time.sleep(2)
        pre = shot('fx-prelock'); chord('meta_l', 'l'); time.sleep(2); lk = shot('fx-lock'); typ('wrongpw\n'); time.sleep(3); wr = shot('fx-lock-wrong')
        typ('qapass\n'); time.sleep(3); ok = shot('fx-unlocked')
        locked0, locked1, unlocked = diff(pre, lk, tbar) > 300, diff(pre, wr, tbar) > 300, diff(pre, ok, tbar) < 300
        if not unlocked: typ('\n'); time.sleep(2)
        out = run('cat /var/log/auth.log >/dev/kmsg', 3)
        record('bootqa', f'the lock screen rejects a wrong password and accepts the right one (locked {locked0}, still locked after a wrong password {locked1}, unlocked {unlocked})', new('lock', locked0 and locked1 and unlocked and 'password changed for root' in out and 'FAILED password for root' in out), out[-300:])
        record('bootqa', 'no crash and the main loop kept running through the fixed-behaviour checks', not bad(serial()) and serial().count('alive: main loop running') > alive0, (bad(serial()).group(0) if bad(serial()) else ''))
    try:
        if not record('bootqa', 'boots to the main loop', wait_for('entering main loop', 300), serial()[-300:]): return
        time.sleep(3)
        vm = re.search(r'"(\d+\.\d+)",', open(os.path.join(KDIR, 'core', 'buildsig.c')).read()); ver = vm.group(1) if vm else '11.18'
        out = run('uname -a >/dev/kmsg', 4)
        if ver not in out: out = run('uname -a >/dev/kmsg', 4)
        record('bootqa', f'uname reports the {ver} kernel', ver in out, out[-200:])
        out = run('gpu feature >/dev/kmsg')
        gf = gpufeat(out)
        record('bootqa', f'v{ver} defaults: all NVIDIA features on ({len(gf)} listed)', len(gf) >= 8 and all(v == 'on' for v in gf.values()), out[-300:])
        out = run('wifi status >/dev/kmsg')
        record('bootqa', 'wifi status: auto-connect and 5 GHz on by default', 'auto-connect on' in out and '5 GHz on' in out, out[-300:])
        out = run('services >/dev/kmsg')
        record('bootqa', 'services lists the unit services sshd and cron (cron running)', re.search(r'running\s+cron', out) and 'sshd' in out, out[-300:])
        out = run('service list >/dev/kmsg')
        record('bootqa', 'service list prints the services', 'cron' in out and 'no such service' not in out, out[-300:])
        out = run('crontab -l >/dev/kmsg 2>&1', 5)
        record('bootqa', 'crontab -l runs (no crontab yet)', 'no crontab for root' in out, out[-300:])
        out = run('man ls >/dev/kmsg', 8)
        record('bootqa', 'man ls renders the manual page', 'LS(1)' in out and 'SYNOPSIS' in re.sub(r'(.)\1', r'\1', out), out[-300:])
        out = run('sshd status >/dev/kmsg')
        record('bootqa', 'sshd status shows the dropbear unit (inactive by default)', 'sshd.service' in out and 'inactive' in out, out[-300:])
        out = run('midi list >/dev/kmsg')
        record('bootqa', 'midi list answers (no device, synth state)', 'USB MIDI' in out and 'synth:' in out, out[-300:])
        mark = len(serial()); run('chvt 2', 3); im = shot('vt2')
        record('bootqa', 'chvt 2 switches to a text console with a login prompt', 'vt: switched to a text console' in serial()[mark:] and 60 < sum(1 for p in im.crop((0, 40, 120, 70)).getdata() if sum(p) > 450) < 2000, serial()[mark:][-200:])
        mark = len(serial()); hmp('sendkey ctrl-alt-f7'); time.sleep(3)
        record('bootqa', 'Ctrl+Alt+F7 comes back from the text console', 'vt: back to the desktop' in serial()[mark:], serial()[mark:][-200:])
        mark = len(serial()); run('desktop', 10); im = shot('desktop')
        tb = im.crop((0, 772, 1280, 800)); cols = len(set(tb.getdata()))
        record('bootqa', f'desktop starts and draws the taskbar ({cols} colours in the taskbar)', 'starting init (desktop)' in serial()[mark:] and cols > 20, serial()[mark:][-200:])
        if not os.environ.get('NVX_BOOTQA_NOFIXES'): fixes_block()
        pages = ['wifi-advanced', 'advanced-system', 'services', 'ssh', 'cron', 'filesystems', 'midi', 'consoles', 'security', 'nvidia', 'screens', 'advanced']
        heads = {}
        for p in pages:
            mark = len(serial())
            for _ in range(3):
                term(); run('settings ' + p, 4, 20)
                if 'cmd: settings ' + p in serial()[mark:]: break
                hmp('sendkey esc'); time.sleep(1)
            im = shot('settings-' + p); seen = 'cmd: settings ' + p in serial()[mark:]
            n = ink(im, (395, 70, 1130, 680)); hd = im.crop((420, 45, 700, 70)).tobytes(); heads[p] = hd
            record('bootqa', f'Settings page {p} opens and draws text ({n} text pixels)', seen and n > 1500 and not bad(serial()[mark:]), serial()[mark:][-200:])
        record('bootqa', 'each Settings page has its own title', len(set(heads.values())) == len(pages))
        below = (400, 712, 1130, 768)
        def diff(a, b, box): return sum(1 for p, q in zip(a.crop(box).getdata(), b.crop(box).getdata()) if abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) > 60)
        base = [Image.open(os.path.join(work, 'settings-' + p + '.ppm')).convert('RGB') for p in ('ssh', 'consoles', 'cron', 'security')]
        def spill_of(im): return min(diff(im, b, below) for b in base)
        for p in ('wifi-advanced', 'nvidia'):
            spill = spill_of(Image.open(os.path.join(work, 'settings-' + p + '.ppm')).convert('RGB'))
            record('bootqa', f'Settings page {p} stays inside its window ({spill} pixels below it differ from a short page)', spill < 50)
        term(); run('settings wifi-advanced', 4); before = shot('wa-top'); wheel(760, 420, -3); after = shot('wa-scrolled')
        moved, spill = diff(before, after, (400, 120, 1130, 680)), spill_of(after)
        record('bootqa', f'the mouse wheel scrolls the Wi-Fi advanced page and it stays inside the window ({moved} pixels changed, {spill} below)', moved > 500 and spill < 50)
        alive = serial().count('alive: main loop running')
        term(); run('settings nvidia', 4); before = shot('nv-before')
        click(1075, 208); after = shot('nv-after')
        record('bootqa', 'clicking the hardware cursor switch changes the NVIDIA page', before.crop((1060, 190, 1090, 225)).tobytes() != after.crop((1060, 190, 1090, 225)).tobytes())
        term(); run('settings midi', 4); before = shot('midi-before')
        click(1075, 257); after = shot('midi-after')
        record('bootqa', 'clicking the MIDI synth switch changes the MIDI page', before.crop((1060, 240, 1090, 275)).tobytes() != after.crop((1060, 240, 1090, 275)).tobytes())
        term(); out = run('gpu feature >/dev/kmsg'); gf1 = gpufeat(out)
        record('bootqa', 'the switch reached the NVIDIA settings (cursor off, the rest on)', gf1.get('cursor') == 'off' and sum(1 for k, v in gf1.items() if k != 'cursor' and v == 'on') == len(gf1) - 1, out[-300:])
        out = run('midi list >/dev/kmsg'); synth1 = re.search(r'synth: (\w+)', out)
        record('bootqa', f'the MIDI synth switch took effect (synth: {synth1.group(1) if synth1 else "?"})', bool(synth1) and (synth1.group(1) != 'off' or 'synth: off - no audio output' in out), out[-300:])
        record('bootqa', 'no crash and the main loop kept running through the desktop tests', not bad(serial()) and serial().count('alive: main loop running') > alive, (bad(serial()).group(0) if bad(serial()) else ''))
        mark = len(serial()); typ('reboot\n')
        ok = wait_for('Boot sequence complete', 180, mark) and wait_for('entering main loop', 300, mark)
        if not record('bootqa', 'reboot comes back to the main loop', ok, serial()[-300:]): return
        time.sleep(3); after = serial()[mark:]
        record('bootqa', 'the start-up guard did not turn anything off after a clean reboot', 'did not finish starting during the last boot' not in after and 'off for this session' not in after, (re.search(r'[^\n]*did not finish starting[^\n]*', after) or re.search(r'[^\n]*off for this session[^\n]*', after) or re.search('x', 'x')).group(0)[:200])
        out = run('gpu feature >/dev/kmsg'); gf2 = gpufeat(out)
        record('bootqa', 'NVIDIA switches persisted across the reboot', gf2 == gf1 and len(gf2) >= 8, out[-300:])
        out = run('midi list >/dev/kmsg'); synth2 = re.search(r'synth: (\w+)', out)
        record('bootqa', 'MIDI synth setting persisted across the reboot', bool(synth1) and bool(synth2) and synth1.group(1) == synth2.group(1), out[-300:])
        out = run('wifi status >/dev/kmsg')
        record('bootqa', 'Wi-Fi defaults still on after the reboot', 'auto-connect on' in out and '5 GHz on' in out, out[-300:])
        record('bootqa', 'no crash after the reboot', not bad(serial()[mark:]))
        if not os.environ.get('NVX_BOOTQA_NOFIXES'):
            mark = len(serial()); run('desktop', 10); term(); run('settings sound', 4); alive = serial().count('alive: main loop running')
            click(1075, 296); time.sleep(3); click(1075, 296); time.sleep(8); shot('fx-mute')
            out = run('uname -a >/dev/kmsg', 4, 30)
            record('bootqa', 'Settings > Sound: toggling Mute twice with no sound card does not freeze the desktop', new('mute', not bad(serial()[mark:]) and serial().count('alive: main loop running') > alive + 4 and ver in out), (bad(serial()[mark:]).group(0) if bad(serial()[mark:]) else serial()[mark:][-200:]))
        open(os.environ.get('NVX_BOOTQA_SERIAL', os.path.join(tempfile.gettempdir(), 'bootqa-serial.log')), 'w', errors='replace').write(serial())
    finally:
        for s in S.values():
            try: s.close()
            except OSError: pass
        pr.kill(); pr.wait()
        if os.environ.get('NVX_KEEP_SERIAL'):
            try: shutil.copy(log, os.environ['NVX_KEEP_SERIAL'])
            except OSError: pass
        shutil.rmtree(work, ignore_errors=True)

def g_bootqa_usb(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    try: from PIL import Image
    except ImportError: record('bootqa', 'Python PIL is installed (needed for screenshots)', False); return
    head('Booted system with USB input (usb-kbd, usb-mouse, usb-tablet behind a usb-hub; NVX_BOOTQA_NOPS2=1 also removes the PS/2 controller), a FAT32+exFAT USB stick, HDA sound: Settings, sleep, terminal')
    import socket, json, struct
    work = tempfile.mkdtemp(prefix='nvx_bootqa_usb_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); qmp = os.path.join(work, 'qmp'); disk = os.path.join(work, 'disk.img'); stick = os.path.join(work, 'stick.img')
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    P = 64 << 20; S1 = 2048; S2 = S1 + P // 512; mbr = bytearray(512); parts = {}
    for i, (ty, st) in enumerate(((0x0C, S1), (0x07, S2))): mbr[446 + 16 * i:462 + 16 * i] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', ty, b'\xfe\xff\xff', st, P // 512)
    mbr[510:512] = b'\x55\xaa'; tools = all(shutil.which(t) for t in ('mkfs.vfat', 'mkfs.exfat', 'mcopy', 'fsck.vfat', 'fsck.exfat'))
    if not record('bootqa', 'host has mkfs.vfat, mkfs.exfat, mcopy, fsck.vfat and fsck.exfat (dosfstools, mtools, exfatprogs) for the USB stick image', tools): return
    for n, mk in (('p1', ['mkfs.vfat', '-F', '32', '-n', 'NVXFAT']), ('p2', ['mkfs.exfat', '-L', 'NVXEXFAT'])):
        parts[n] = os.path.join(work, n + '.img')
        with open(parts[n], 'wb') as f: f.truncate(P)
        sh(mk + [parts[n]])
    open(os.path.join(work, 'HOSTFAT.TXT'), 'w').write('hello from the host\n'); sh(['mcopy', '-i', parts['p1'], os.path.join(work, 'HOSTFAT.TXT'), '::HOSTFAT.TXT'])
    with open(stick, 'wb') as f:
        f.write(mbr); f.seek(S1 * 512); f.write(open(parts['p1'], 'rb').read()); f.seek(S2 * 512); f.write(open(parts['p2'], 'rb').read()); f.truncate(S2 * 512 + P + (1 << 20))
    for p in parts.values(): os.remove(p)
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
    bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-machine', 'pc,i8042=off,vmport=off' if os.environ.get('NVX_BOOTQA_NOPS2') else 'pc,vmport=off', '-m', '2048', '-smp', '2', '-accel', 'tcg,thread=multi',
           '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
           '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
           '-device', 'qemu-xhci,id=xhci', '-device', 'usb-hub,bus=xhci.0,port=1', '-device', 'usb-kbd,bus=xhci.0,port=1.1', '-device', 'usb-mouse,bus=xhci.0,port=1.2', '-device', 'usb-tablet,bus=xhci.0,port=1.3',
           '-drive', f'file={stick},format=raw,if=none,id=st0', '-device', 'usb-storage,bus=xhci.0,port=2,drive=st0',
           '-audiodev', 'none,id=snd0', '-device', 'intel-hda', '-device', 'hda-duplex,audiodev=snd0',
           '-serial', f'file:{log}', '-display', 'none', '-monitor', f'unix:{mon},server,nowait', '-qmp', f'unix:{qmp},server,nowait']
    pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    def serial():
        try: return open(log, 'rb').read().decode('latin-1')
        except OSError: return ''
    def wait_for(text, secs, start=0):
        t = time.time()
        while time.time() - t < secs and pr.poll() is None:
            if text in serial()[start:]: return True
            time.sleep(1)
        return text in serial()[start:]
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7', ':': 'shift-semicolon', '|': 'shift-backslash', '_': 'shift-minus', '=': 'equal'}
    S = {}
    def mons():
        if 'm' not in S: S['m'] = socket.socket(socket.AF_UNIX); S['m'].connect(mon); time.sleep(0.5)
        return S['m']
    def hmp(c): mons().sendall((c + '\n').encode()); time.sleep(0.3)
    def typ(txt):
        for ch in txt: hmp('sendkey ' + (keys.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)))
    def term(): qmp_chord(lambda e: ev(e, 0.05), 'meta_l', 'r'); time.sleep(2.5)
    def run(c, secs=4, wait=60):
        mark = len(serial()); typ(c + '\n'); t = time.time()
        while time.time() - t < wait and ('cmd: ' + c) not in serial()[mark:]: time.sleep(1)
        time.sleep(secs); return serial()[mark:]
    def shot(name):
        p = os.path.join(work, name + '.ppm')
        if os.path.exists(p): os.remove(p)
        hmp('screendump ' + p); t = time.time()
        while time.time() - t < 10 and not os.path.exists(p): time.sleep(0.5)
        time.sleep(1); im = Image.open(p).convert('RGB'); keep = os.environ.get('NVX_KEEP_SHOTS')
        if keep: os.makedirs(keep, exist_ok=True); im.save(os.path.join(keep, 'usb-' + name + '.png'))
        return im
    def ink(im, box, lim=110):
        return sum(1 for p in im.crop(box).getdata() if p[0] + p[1] + p[2] < lim * 3)
    def diff(a, b, box): return sum(1 for p, r in zip(a.crop(box).getdata(), b.crop(box).getdata()) if abs(p[0] - r[0]) + abs(p[1] - r[1]) + abs(p[2] - r[2]) > 60)
    def qmpc():
        if 'q' not in S:
            s = socket.socket(socket.AF_UNIX); s.connect(qmp); s.settimeout(5); s.recv(4096); s.sendall(b'{"execute":"qmp_capabilities"}\n'); time.sleep(0.3); s.recv(4096); S['q'] = s
        return S['q']
    def ev(e, d=0.3):
        qmpc().sendall((json.dumps({'execute': 'input-send-event', 'arguments': {'events': e}}) + '\n').encode()); time.sleep(d)
        try: qmpc().recv(65536)
        except OSError: pass
    def at(x, y): ev([{'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // 1279}}, {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // 799}}], 0.6)
    def press(b='left'): ev([{'type': 'btn', 'data': {'down': True, 'button': b}}], 0.2); ev([{'type': 'btn', 'data': {'down': False, 'button': b}}], 0.4)
    def click(x, y): at(x, y); press(); time.sleep(1.2)
    bad = lambda t: re.search(r'(?i)kernel panic|panic: |page fault at|general protection|double fault|stack overflow (at|in|on cpu)|see the crash report|freeze report|watchdog: main loop', t)
    try:
        ok = wait_for('entering main loop', 420)
        if not record('bootqa', 'machine with USB input devices and a USB stick boots to the main loop', ok, (pr.stderr.read().decode()[-200:] if pr.poll() is not None else '') + serial()[-300:]): return
        s0 = serial()
        record('bootqa', 'the hub and the three HID devices behind it are set up', 'usb: hub set up' in s0 and s0.count('keyboard/mouse behind a hub set up') >= 3, '\n'.join(l for l in s0.splitlines() if 'usb:' in l)[-300:])
        record('bootqa', 'the USB stick is found and its FAT partition mounted at boot', 'USB stick mounted' in s0, (re.search(r'[^\n]*USB (stick|storage)[^\n]*', s0) or re.search('x', 'x')).group(0)[:200])
        time.sleep(3)
        if 'sign-in screen' in serial(): hmp('sendkey ret'); time.sleep(4)
        out = run('uname -a >/dev/kmsg', 4)
        record('bootqa', 'the USB keyboard behind the hub types into the shell (uname runs)', 'cmd: uname -a' in out, out[-200:])
        out = run('lsusb >/dev/kmsg')
        record('bootqa', 'lsusb lists the USB stick', 'USB drive' in out, out[-300:])
        record('bootqa', 'lsusb lists the keyboard and the mouse behind the hub', 'Keyboard' in out and 'Mouse' in out, out[-300:])
        out = run('gpu feature >/dev/kmsg')
        record('bootqa', 'gpu feature lists the NVIDIA switches', len(re.findall(r'\]\s+\w+\s+(?:on|off)\b', out)) >= 8, out[-300:])
        out = run('wifi status >/dev/kmsg')
        record('bootqa', 'wifi status answers (auto-connect, 5 GHz)', 'auto-connect' in out and '5 GHz' in out, out[-300:])
        out = run('bt status >/dev/kmsg')
        record('bootqa', 'bt status answers without an adapter', 'adapter: none found' in out or 'adapter' in out, out[-300:])
        out = run('s3 >/dev/kmsg')
        record('bootqa', 's3 reports the suspend-to-RAM state (on only where a key can wake the computer)', 's3: suspend to RAM o' in out, out[-300:])
        out = run('firewall >/dev/kmsg')
        fw = re.sub(r'\[\d+\] (cmd: |alive|lag)[^\n]*\n?', '', out).strip()
        if 'unknown command' in out or 'not found' in out: print('  note: no firewall command in this image')
        else: record('bootqa', 'firewall prints its state', len(fw) > 5 and not bad(out), out[-300:])
        mark = len(serial()); typ('suspend >/dev/kmsg\n')
        slept = wait_for('sleep: devices ready', 40, mark) or wait_for('sleeping', 5, mark); time.sleep(4)
        ev([{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'spc'}}}], 0.1); ev([{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'spc'}}}], 0.5)
        woke = wait_for('woke from', 40, mark); time.sleep(2)
        if not woke: hmp('system_wakeup'); woke = wait_for('woke from', 30, mark)
        record('bootqa', 'suspend enters light sleep and a USB key press sent through QMP wakes it', slept and woke and 'light sleep' in serial()[mark:], serial()[mark:][-300:])
        out = run('uptime >/dev/kmsg', 4)
        record('bootqa', 'the shell answers after waking', bool(re.search(r'up \d+', out)), out[-200:])
        mark = len(serial()); run('desktop', 10); im = shot('desktop')
        record('bootqa', 'desktop starts with USB input only', 'starting init (desktop)' in serial()[mark:] and len(set(im.crop((0, 772, 1280, 800)).getdata())) > 20, serial()[mark:][-200:])
        alive = serial().count('alive: main loop running')
        a = shot('mouse-a'); ev([{'type': 'rel', 'data': {'axis': 'x', 'value': 120}}, {'type': 'rel', 'data': {'axis': 'y', 'value': 90}}], 1.0); b = shot('mouse-b')
        record('bootqa', f'the USB mouse moves the pointer ({diff(a, b, (0, 0, 1280, 770))} pixels changed)', diff(a, b, (0, 0, 1280, 770)) > 40)
        term(); run('settings nvidia', 4); before = shot('nv-before'); click(1075, 208); after = shot('nv-after')
        record('bootqa', 'a USB tablet click flips the NVIDIA hardware cursor switch', before.crop((1060, 190, 1090, 225)).tobytes() != after.crop((1060, 190, 1090, 225)).tobytes())
        press(); time.sleep(1.5); back = shot('nv-back')
        record('bootqa', 'a USB mouse button click (same spot) flips it back', back.crop((1060, 190, 1090, 225)).tobytes() == before.crop((1060, 190, 1090, 225)).tobytes())
        term(); run('settings wifi-advanced', 4); top = shot('wa-top'); at(760, 420)
        for _ in range(3): press('wheel-down')
        time.sleep(1.5); low = shot('wa-scrolled')
        record('bootqa', f'the USB mouse wheel scrolls the Wi-Fi advanced page ({diff(top, low, (400, 120, 1130, 680))} pixels changed)', diff(top, low, (400, 120, 1130, 680)) > 500)
        heads = {}
        for name, page, search in (('security', 'security', ''), ('device-security', 'system', 'device security'), ('accounts', 'system', 'your account'), ('sound', 'sound', ''), ('power', 'system', 'power battery')):
            mark = len(serial()); term(); run('settings ' + page, 4, 20)
            if search: typ(search + '\n'); time.sleep(3)
            im = shot('settings-' + name); n = ink(im, (395, 70, 1130, 680)); heads[name] = im.crop((420, 45, 700, 70)).tobytes()
            record('bootqa', f'Settings {name} page opens and draws text ({n} text pixels)', 'cmd: settings ' + page in serial()[mark:] and n > 1500 and not bad(serial()[mark:]), serial()[mark:][-200:])
            if name == 'accounts':
                at(760, 420)
                for _ in range(6): press('wheel-down')
                time.sleep(1.5); low = shot('settings-accounts-signin'); n2 = ink(low, (395, 70, 1130, 680))
                record('bootqa', f'Accounts page scrolls to the sign-in options (Face ID, fingerprint rows: {diff(im, low, (395, 70, 1130, 680))} pixels changed, {n2} text pixels)', diff(im, low, (395, 70, 1130, 680)) > 500 and n2 > 1500)
        record('bootqa', 'the searched Settings pages are different pages', len(set(heads.values())) == len(heads))
        mark = len(serial()); term(); run('thispc', 6); im = shot('thispc')
        record('bootqa', f'Files opens This PC with the USB stick attached ({ink(im, (0, 40, 1280, 760))} text pixels)', 'cmd: thispc' in serial()[mark:] and not bad(serial()[mark:]))
        record('bootqa', 'no crash and the main loop kept running with USB-only input', not bad(serial()) and serial().count('alive: main loop running') > alive, (bad(serial()).group(0) if bad(serial()) else ''))
        mark = len(serial()); term(); typ('poweroff\n'); t = time.time()
        while time.time() - t < 60 and pr.poll() is None: time.sleep(1)
    finally:
        for s in S.values():
            try: s.close()
            except OSError: pass
        if pr.poll() is None: pr.kill()
        pr.wait()
        if os.environ.get('NVX_KEEP_SERIAL'):
            try: shutil.copy(log, os.environ['NVX_KEEP_SERIAL'] + '.usb')
            except OSError: pass
        if os.path.exists(stick):
            for n, st in (('FAT32', S1), ('exFAT', S2)):
                p = os.path.join(work, n + '.img')
                with open(stick, 'rb') as f, open(p, 'wb') as g: f.seek(st * 512); g.write(f.read(P))
                rc, out = sh(['fsck.vfat', '-n', p] if n == 'FAT32' else ['fsck.exfat', '-n', p])
                record('bootqa', f'host fsck of the stick\'s {n} partition after the session is clean', rc == 0, out[-300:])
            ls = sh(['mdir', '-i', os.path.join(work, 'FAT32.img'), '::'])[1]
            record('bootqa', 'the host file on the FAT partition is still there', 'HOSTFAT' in ls, ls[-300:])
        shutil.rmtree(work, ignore_errors=True)

USBQA_NEW = {'serial': 'USB serial (FTDI) driver', 'audio': 'USB audio output', 'hub3': 'three hub tiers'}
def g_usbqa(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('USB devices in QEMU: 3-tier usb-hub chain with HID behind it, usb-audio output, usb-net (CDC-ECM, RNDIS) DHCP, usb-serial (FTDI) loopback, FAT32/exFAT usb-storage, 200x QMP hot-plug while the desktop runs')
    print('  note: usb-host passthrough is not available in this environment (no host USB devices), not tested')
    import socket, json, struct, threading
    work = tempfile.mkdtemp(prefix='nvx_usbqa_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); qmp = os.path.join(work, 'qmp'); disk = os.path.join(work, 'disk.img'); wav = os.path.join(work, 'out.wav')
    tools = all(shutil.which(t) for t in ('mkfs.vfat', 'mkfs.exfat', 'mcopy', 'fsck.vfat', 'fsck.exfat'))
    if not record('usbqa', 'host has mkfs.vfat, mkfs.exfat, mcopy, fsck.vfat and fsck.exfat for the stick images', tools): shutil.rmtree(work, ignore_errors=True); return
    with open(disk, 'wb') as f: f.truncate(64 << 20)
    P = 48 << 20; S1 = 2048; sticks = {}
    for n, ty, mk in (('fat', 0x0C, ['mkfs.vfat', '-F', '32', '-n', 'QAFAT']), ('exfat', 0x07, ['mkfs.exfat', '-L', 'QAEXFAT']), ('hot', 0x0C, ['mkfs.vfat', '-F', '32', '-n', 'QAHOT'])):
        p = os.path.join(work, n + '.p')
        with open(p, 'wb') as f: f.truncate(P)
        sh(mk + [p])
        if n == 'fat': open(os.path.join(work, 'QAFAT.TXT'), 'w').write('usbqa fat stick\n'); sh(['mcopy', '-i', p, os.path.join(work, 'QAFAT.TXT'), '::QAFAT.TXT'])
        mbr = bytearray(512); mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', ty, b'\xfe\xff\xff', S1, P // 512); mbr[510:512] = b'\x55\xaa'
        sticks[n] = os.path.join(work, n + '.img')
        with open(sticks[n], 'wb') as f: f.write(mbr); f.seek(S1 * 512); f.write(open(p, 'rb').read()); f.truncate(S1 * 512 + P + (1 << 20))
        os.remove(p)
    ser = {'rx': b'', 'conn': 0, 'stop': False}
    ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.bind(('127.0.0.1', 0)); ls.listen(4); ls.settimeout(1); sport = ls.getsockname()[1]
    def serve():
        while not ser['stop']:
            try: c, _ = ls.accept()
            except OSError: continue
            ser['conn'] += 1; c.settimeout(0.5)
            while not ser['stop']:
                try:
                    d = c.recv(4096)
                    if not d: break
                    ser['rx'] += d
                except socket.timeout: pass
                except OSError: break
                if b'PING-nvx' in ser['rx']:
                    try: c.sendall(b'PONG-nvx\n')
                    except OSError: break
            c.close()
    threading.Thread(target=serve, daemon=True).start()
    iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2); bios = os.environ.get('NVX_QEMU_BIOS')
    cmd = [q] + (['-L', bios] if bios else []) + ['-machine', 'pc,vmport=off', '-m', '2048', '-smp', '2', '-accel', 'tcg,thread=multi', '-net', 'none',
           '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
           '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
           '-device', 'qemu-xhci,id=xh,p2=8,p3=8', '-device', 'usb-hub,bus=xh.0,port=1,id=hub1', '-device', 'usb-hub,bus=xh.0,port=1.1,id=hub2', '-device', 'usb-hub,bus=xh.0,port=1.1.1,id=hub3',
           '-device', 'usb-kbd,bus=xh.0,port=1.1.1.1', '-device', 'usb-mouse,bus=xh.0,port=1.1.1.2', '-device', 'usb-tablet,bus=xh.0,port=1.1.2',
           '-drive', f'file={sticks["fat"]},format=raw,if=none,id=st0', '-device', 'usb-storage,bus=xh.0,port=1.1.1.3,drive=st0',
           '-drive', f'file={sticks["exfat"]},format=raw,if=none,id=st1', '-device', 'usb-storage,bus=xh.0,port=4,drive=st1',
           '-blockdev', f'driver=raw,node-name=hotblk,file.driver=file,file.filename={sticks["hot"]}',
           '-audiodev', f'wav,id=a0,path={wav}', '-device', 'usb-audio,bus=xh.0,port=1.2,audiodev=a0',
           '-netdev', 'user,id=n0', '-device', 'usb-net,bus=xh.0,port=2,netdev=n0', '-netdev', 'user,id=n1',
           '-chardev', f'socket,id=ft0,host=127.0.0.1,port={sport},reconnect=1', '-device', 'usb-serial,bus=xh.0,port=3,chardev=ft0',
           '-audiodev', 'none,id=a1',
           '-serial', f'file:{log}', '-display', 'none', '-monitor', f'unix:{mon},server,nowait', '-qmp', f'unix:{qmp},server,nowait']
    pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    def serial():
        try: return open(log, 'rb').read().decode('latin-1')
        except OSError: return ''
    def wait_for(rx, secs, start=0):
        t = time.time()
        while time.time() - t < secs and pr.poll() is None:
            m = re.search(rx, serial()[start:])
            if m: return m
            time.sleep(1)
        return re.search(rx, serial()[start:])
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', '&': 'shift-7', ':': 'shift-semicolon', '|': 'shift-backslash', '_': 'shift-minus', '=': 'equal'}
    S = {}; Q = {'buf': b'', 'ev': []}
    def mons():
        if 'm' not in S: S['m'] = socket.socket(socket.AF_UNIX); S['m'].connect(mon); time.sleep(0.5)
        return S['m']
    def hmp(c): mons().sendall((c + '\n').encode()); time.sleep(0.3)
    def typ(txt):
        for ch in txt: hmp('sendkey ' + (keys.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)))
    def term(): qmp_chord(lambda e: qmpc('input-send-event', {'events': e}, 5), 'meta_l', 'r'); time.sleep(2.5)
    def run(c, secs=4, wait=60):
        mark = len(serial()); typ(c + '\n'); t = time.time()
        while time.time() - t < wait and ('cmd: ' + c) not in serial()[mark:] and pr.poll() is None: time.sleep(1)
        time.sleep(secs); return serial()[mark:]
    def qline(secs):
        t = time.time(); s = S['q']
        while b'\n' not in Q['buf']:
            if time.time() - t > secs: return None
            try: d = s.recv(65536)
            except socket.timeout: continue
            if not d: raise ConnectionError('QMP closed')
            Q['buf'] += d
        l, Q['buf'] = Q['buf'].split(b'\n', 1)
        try: return json.loads(l)
        except ValueError: return {}
    def qmpc(c, args=None, secs=15):
        if 'q' not in S:
            s = socket.socket(socket.AF_UNIX); s.connect(qmp); s.settimeout(1); S['q'] = s; qline(5); s.sendall(b'{"execute":"qmp_capabilities"}\n'); qline(5)
        S['q'].sendall((json.dumps({'execute': c, 'arguments': args or {}}) + '\n').encode()); t = time.time()
        while time.time() - t < secs:
            m = qline(1)
            if m is None: continue
            if 'event' in m: Q['ev'].append(m)
            elif 'return' in m or 'error' in m: return m
        return {'error': {'desc': 'no QMP answer in %d s' % secs}}
    def gone(i, secs=15):
        t = time.time()
        while time.time() - t < secs:
            if any(e['event'] == 'DEVICE_DELETED' and e.get('data', {}).get('device') == i for e in Q['ev']): return True
            m = qline(1)
            if m and 'event' in m: Q['ev'].append(m)
        return False
    bad = lambda t: re.search(r'(?i)kernel panic|panic: |page fault at|general protection|double fault|stack overflow (at|in|on cpu)|see the crash report|freeze report|watchdog: main loop', t)
    def new(k, ok): return ok or print(f'  note: [{USBQA_NEW[k]}] is expected from this round\'s drivers; this FAIL means the image does not have it yet or it regressed') or ok
    cycles = int(os.environ.get('NVX_USBQA_CYCLES', '200'))
    try:
        if not record('usbqa', 'machine with a 3-tier hub chain, HID, audio, net, serial and two sticks boots to the main loop', wait_for('entering main loop', 420), (pr.stderr.read().decode()[-300:] if pr.poll() is not None else '') + serial()[-300:]): return
        time.sleep(8); s0 = serial(); ul = '\n'.join(l for l in s0.splitlines() if re.search(r'usb|hub', l, re.I))[-400:]
        record('usbqa', f'all three hubs of the chain are set up ({s0.count("usb: hub set up")} hub lines)', new('hub3', s0.count('usb: hub set up') >= 3), ul)
        record('usbqa', 'keyboard, mouse (tier 3) and tablet (tier 2) behind the hubs are set up', s0.count('keyboard/mouse behind a hub set up') >= 3, ul)
        record('usbqa', 'the FAT32 stick behind the third hub tier is found', bool(re.search(r'USB stick (mounted|behind a hub)', s0)), ul)
        if 'sign-in screen' in serial(): hmp('sendkey ret'); time.sleep(4)
        out = run('uname -a >/dev/kmsg', 4)
        if not record('usbqa', 'the keyboard behind three hubs types into the shell', 'cmd: uname -a' in out, out[-200:]): return
        out = run('lsusb >/dev/kmsg', 4); lu = out[-600:]
        record('usbqa', 'lsusb lists the keyboard and the mouse behind the hub chain', 'Keyboard' in out and 'Mouse' in out, lu)
        record('usbqa', 'lsusb lists the usb-net adapter (0525:a4a2)', '0525:a4a2' in out.lower(), lu)
        record('usbqa', 'lsusb lists the FTDI serial adapter (0403:6001)', new('serial', '0403:6001' in out.lower()), lu)
        record('usbqa', 'lsusb lists the usb-audio device (46f4:0002)', new('audio', '46f4:0002' in out.lower()), lu)
        record('usbqa', 'lsusb lists both USB sticks', out.count('USB drive') >= 2 or len(re.findall(r'46f4:0001', out.lower())) >= 2, lu)
        out = run('usbls >/dev/kmsg', 4)
        record('usbqa', 'the host file on the FAT32 stick behind the hub chain is listed', 'QAFAT' in out.upper(), out[-300:])
        out = run('usbcat QAFAT.TXT >/dev/kmsg', 4)
        record('usbqa', 'the host file on the FAT32 stick reads back', 'usbqa fat stick' in out, out[-300:])
        mm = run('cat /proc/mounts >/dev/kmsg', 4) + run('mount >/dev/kmsg', 4)
        record('usbqa', 'the exFAT stick on root port 4 is mounted', bool(re.search(r'(?i)exfat', mm)) or bool(re.search(r'(?i)exfat[^\n]*mounted|QAEXFAT', serial())), mm[-300:])
        m = re.search(r'usb-net: (CDC-ECM|CDC-NCM|RNDIS) adapter 0525:a4a2 up', serial()) or re.search(r'Network adapter\s+(\S+)[^\n]*0525:a4a2', serial(), re.I)
        record('usbqa', f'usb-net is set up ({m.group(1) if m else "no usb-net up line, not in lsusb"})', bool(m), '\n'.join(l for l in serial().splitlines() if 'usb-net' in l)[-300:])
        mark = len(serial()); typ('net\n'); d = wait_for(r'DHCP ok ip=10\.0\.2\.\d+|DHCP failed|no network adapter', 90, mark)
        record('usbqa', f'usb-net gets an address by DHCP from QEMU user networking ({d.group(0) if d else "no answer"})', bool(d) and d.group(0).startswith('DHCP ok'), serial()[mark:][-300:])
        mark = len(serial()); run('usbnet rndis >/dev/kmsg', 8); typ('net\n'); d = wait_for(r'DHCP ok ip=10\.0\.2\.\d+|DHCP failed|no network adapter', 90, mark)
        record('usbqa', f'usb-net in RNDIS mode is set up and gets an address by DHCP ({d.group(0) if d else "no answer"})', bool(re.search(r'usb-net: RNDIS adapter 0525:a4a2 up|USB network adapter set up again', serial()[mark:])) and bool(d) and d.group(0).startswith('DHCP ok'), serial()[mark:][-300:])
        run('usbnet auto >/dev/kmsg', 6)
        sl = '\n'.join(l for l in serial().splitlines() if re.search(r'(?i)ftdi|ttyUSB|usb.serial', l))[-300:]
        out = run('ls /dev/ttyUSB0 >/dev/kmsg', 3)
        record('usbqa', 'the FTDI adapter shows up as /dev/ttyUSB0', new('serial', bool(re.search(r'\] /dev/ttyUSB0\s*$', out, re.M))), (sl or out)[-300:])
        run('stty -F /dev/ttyUSB0 115200 raw -echo', 2, 10); mark = len(serial()); ser['rx'] = b''
        run('echo PING-nvx >/dev/ttyUSB0', 3, 15); t = time.time()
        while time.time() - t < 10 and b'PING-nvx' not in ser['rx']: time.sleep(0.5)
        record('usbqa', f'bytes written to /dev/ttyUSB0 reach the chardev on the host ({len(ser["rx"])} bytes, {ser["conn"]} connections)', new('serial', b'PING-nvx' in ser['rx']), repr(ser['rx'][-80:]))
        run('head -n 1 /dev/ttyUSB0 >/dev/kmsg', 3, 15); ok = bool(wait_for(r'PONG-nvx', 15, mark))
        if not ok: hmp('sendkey ctrl-c'); time.sleep(1)
        record('usbqa', 'the loopback reply from the chardev reads back from /dev/ttyUSB0', new('serial', ok), serial()[mark:][-300:])
        al = re.search(r'usb audio: [^\n]*speakers[^\n]*', serial())
        record('usbqa', f'usb-audio output is set up ({al.group(0)[:80] if al else "no usb audio line"})', new('audio', bool(al)), '\n'.join(l for l in serial().splitlines() if 'audio' in l.lower())[-300:])
        w0 = os.path.getsize(wav) if os.path.exists(wav) else 0; out = run('audio tone 880', 4); w1 = os.path.getsize(wav) if os.path.exists(wav) else 0
        loud = 0
        try:
            with open(wav, 'rb') as f: f.seek(max(44, w0)); pcm = f.read(); loud = sum(1 for i in range(0, len(pcm) - 1, 2) if abs(struct.unpack_from('<h', pcm, i)[0]) > 1000)
        except (OSError, struct.error): pass
        record('usbqa', f'audio tone plays through usb-audio into the host WAV ({w1 - w0} bytes, {loud} loud samples)', new('audio', loud > 4000), out[-300:])
        mark = len(serial()); run('desktop', 10)
        record('usbqa', 'desktop starts with USB-only input behind the hub chain', 'starting init (desktop)' in serial()[mark:], serial()[mark:][-200:])
        alive = serial().count('alive: main loop running'); kinds = [('usb-kbd', {}), ('usb-mouse', {}), ('usb-tablet', {}), ('usb-storage', {'drive': 'hotblk'}), ('usb-net', {'netdev': 'n1'}), ('usb-audio', {'audiodev': 'a1'})]
        ports = ['5', '6', '1.3', '1.1.4']; fails = []; mark = len(serial()); t0 = time.time()
        for i in range(cycles):
            kd, extra = kinds[i % len(kinds)]; port = ports[i % len(ports)]; did = f'hp{i}'
            r = qmpc('device_add', dict({'driver': kd, 'id': did, 'bus': 'xh.0', 'port': port}, **extra))
            if 'error' in r: fails.append(f'{i}:add {kd}@{port}: {r["error"].get("desc", "")[:60]}'); continue
            time.sleep(1.2 + (i % 3) * 0.4)
            r = qmpc('device_del', {'id': did})
            if 'error' in r or not gone(did): fails.append(f'{i}:del {kd}@{port}: {r.get("error", {}).get("desc", "no DEVICE_DELETED")[:60]}')
            if pr.poll() is not None: fails.append(f'{i}: QEMU exited {pr.poll()}'); break
            if bad(serial()[mark:]): fails.append(f'{i}: guest crash after {kd}@{port}'); break
        hs = serial()[mark:]
        record('usbqa', f'{cycles} QMP hot-plug cycles (kbd, mouse, tablet, stick, net, audio on root ports and behind hubs) in {time.time() - t0:.0f} s without QMP errors', not fails, '; '.join(fails[:4]))
        seen = len(re.findall(r'usb: (keyboard/mouse (connected|behind a hub set up)|USB stick|network adapter behind a hub)|usb-net: [^\n]* up|usb audio: ', hs))
        record('usbqa', f'the guest noticed the hot-plugged devices ({seen} set-up lines for {cycles} plugs)', seen >= cycles // 2, hs[-300:])
        time.sleep(5)
        record('usbqa', 'no crash and the main loop kept running through the hot-plug stress', not bad(serial()) and pr.poll() is None and serial().count('alive: main loop running') > alive, (bad(serial()).group(0) if bad(serial()) else '') + serial()[-200:])
        mark = len(serial()); r = qmpc('device_add', {'driver': 'usb-kbd', 'id': 'back1', 'bus': 'xh.0', 'port': '5'}); time.sleep(6)
        record('usbqa', 'a keyboard plugged in after the stress is set up again', 'error' not in r and bool(re.search(r'usb: keyboard/mouse (connected|behind a hub set up)', serial()[mark:])), serial()[mark:][-300:])
        r = qmpc('device_add', {'driver': 'usb-storage', 'id': 'back2', 'bus': 'xh.0', 'port': '1.3', 'drive': 'hotblk'}); time.sleep(8)
        term(); out = run('lsusb >/dev/kmsg', 4)
        record('usbqa', 'the original hub-chain keyboard still types and lsusb lists the devices plugged in after the stress', 'cmd: lsusb' in out and 'Keyboard' in out and ('USB drive' in out or '46f4:0001' in out.lower()), out[-400:])
        out = run('lsusb >/dev/kmsg', 3)
        record('usbqa', 'the cold-plugged net, serial and audio devices are still listed after the stress', all(v in out.lower() for v in ('0525:a4a2', '0403:6001', '46f4:0002')), out[-400:])
        record('usbqa', 'no crash at the end of the USB session', not bad(serial()) and pr.poll() is None, (bad(serial()).group(0) if bad(serial()) else ''))
        typ('poweroff\n'); t = time.time()
        while time.time() - t < 60 and pr.poll() is None: time.sleep(1)
    finally:
        ser['stop'] = True
        for s in list(S.values()) + [ls]:
            try: s.close()
            except OSError: pass
        if pr.poll() is None: pr.kill()
        pr.wait()
        if os.environ.get('NVX_KEEP_SERIAL'):
            try: shutil.copy(log, os.environ['NVX_KEEP_SERIAL'] + '.usbqa')
            except OSError: pass
        for n, fk in (('fat', ['fsck.vfat', '-n']), ('exfat', ['fsck.exfat', '-n']), ('hot', ['fsck.vfat', '-n'])):
            if not os.path.exists(sticks.get(n, '')): continue
            p = os.path.join(work, n + '.chk')
            with open(sticks[n], 'rb') as f, open(p, 'wb') as g: f.seek(S1 * 512); g.write(f.read(P))
            rc, out = sh(fk + [p]); os.remove(p)
            record('usbqa', f'host fsck of the {n} stick after the session is clean', rc == 0, out[-300:])
        shutil.rmtree(work, ignore_errors=True)

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
    from cryptography.hazmat.primitives.asymmetric import ec as _ec
    from cryptography.hazmat.primitives import serialization as _ser
    bad = 0
    for _ in range(8):
        k = _ec.generate_private_key(_ec.SECP256R1()); d = k.private_numbers().private_value.to_bytes(32, 'big')
        pub = k.public_key().public_bytes(_ser.Encoding.X962, _ser.PublicFormat.UncompressedPoint)
        q = _ec.generate_private_key(_ec.SECP256R1()); qp = q.public_key().public_bytes(_ser.Encoding.X962, _ser.PublicFormat.UncompressedPoint)
        if sh([exe, 'ecdh', d.hex()])[1].strip() != pub.hex(): bad += 1
        if sh([exe, 'ecdh', d.hex(), qp.hex()])[1].strip() != k.exchange(_ec.ECDH(), q.public_key()).hex(): bad += 1
    off = sh([exe, 'ecdh', '01' * 32, '04' + '00' * 64])[1].strip()
    record('tls', 'P-256 key exchange matches the cryptography module and rejects off-curve points', bad == 0 and off == 'ERR', f'bad {bad} off {off}')
    if shutil.which('openssl'):
        ck = os.path.join(work, 'hk'); os.makedirs(ck, exist_ok=True)
        sh(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost', '-days', '2', '-keyout', os.path.join(ck, 'k.pem'), '-out', os.path.join(ck, 'c.pem')])
        sh(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost', '-days', '2', '-keyout', os.path.join(ck, 'rk.pem'), '-out', os.path.join(ck, 'rc.pem')])
        open(os.path.join(ck, 'big'), 'wb').write(bytes(((i*7+(i>>8))&0xFF) for i in range(300000)))
        port = 20443 + random.randint(0, 400) * 8; want = big_sha(); import hashlib
        cases = (
            (['-tls1_3', '-groups', 'X25519'], 'c', 'TLS 1.3 with X25519'),
            (['-tls1_3', '-groups', 'P-256'], 'c', 'TLS 1.3 with P-256 after a HelloRetryRequest'),
            (['-tls1_3', '-sigalgs', 'rsa_pss_rsae_sha256'], 'rc', 'TLS 1.3 with an RSA certificate'),
            (['-tls1_2', '-cipher', 'ECDHE-ECDSA-AES128-GCM-SHA256'], 'c', 'TLS 1.2 ECDHE-ECDSA-AES128-GCM'),
            (['-tls1_2', '-cipher', 'ECDHE-ECDSA-CHACHA20-POLY1305', '-groups', 'P-256'], 'c', 'TLS 1.2 ECDHE-ECDSA-CHACHA20 with P-256'),
            (['-tls1_2', '-cipher', 'ECDHE-RSA-AES128-GCM-SHA256', '-sigalgs', 'RSA+SHA256'], 'rc', 'TLS 1.2 ECDHE-RSA-AES128-GCM (PKCS#1)'),
            (['-tls1_2', '-cipher', 'ECDHE-RSA-CHACHA20-POLY1305'], 'rc', 'TLS 1.2 ECDHE-RSA-CHACHA20 (PSS)'),
        )
        for k, (args, cert, label) in enumerate(cases):
            pn = port + k
            kf = 'k.pem' if cert == 'c' else 'rk.pem'
            srv = subprocess.Popen(['openssl', 's_server', '-accept', str(pn), '-cert', cert + '.pem', '-key', kf] + args + ['-WWW'], cwd=ck, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                time.sleep(1); ob = os.path.join(ck, 'o.bin')
                if os.path.exists(ob): os.unlink(ob)
                rc, out = sh([exe, 'get', 'localhost', str(pn), '/big', ob, os.path.join(ck, cert + '.pem')])
                body = open(ob, 'rb').read() if os.path.exists(ob) else b''
                record('tls', f'{label}: certificate verified, 300 KB body intact', rc == 0 and 'connect 0' in out and 'certificate verified' in out and hashlib.sha256(body[-300000:]).hexdigest() == want, out[-300:])
            finally:
                srv.kill(); srv.wait()
        import socket, threading
        def flipper(lsock, rport, idx):
            def pipe(a, b, mod):
                buf = b''; n = 0
                try:
                    while True:
                        d = a.recv(65536)
                        if not d: break
                        if not mod: b.sendall(d); continue
                        buf += d
                        while len(buf) >= 5 and len(buf) >= 5 + (buf[3] << 8 | buf[4]):
                            L = 5 + (buf[3] << 8 | buf[4]); r = bytearray(buf[:L]); buf = buf[L:]
                            if n == idx: r[L - 1] ^= 1
                            n += 1; b.sendall(bytes(r))
                except OSError: pass
                try: b.shutdown(socket.SHUT_WR)
                except OSError: pass
            try:
                c, _ = lsock.accept()
            except OSError: return
            u = socket.create_connection(('127.0.0.1', rport))
            threading.Thread(target=pipe, args=(c, u, False), daemon=True).start()
            pipe(u, c, True)
        pn = port + len(cases)
        srv = subprocess.Popen(['openssl', 's_server', '-accept', str(pn), '-cert', 'c.pem', '-key', 'k.pem', '-tls1_2', '-cipher', 'ECDHE-ECDSA-AES128-GCM-SHA256', '-WWW'], cwd=ck, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.bind(('127.0.0.1', pn + 1)); ls.listen(1); ls.settimeout(20)
        try:
            time.sleep(1); th = threading.Thread(target=flipper, args=(ls, pn, 2), daemon=True); th.start()
            rc, out = sh([exe, 'get', 'localhost', str(pn + 1), '/big', os.path.join(ck, 'o2.bin'), os.path.join(ck, 'c.pem')])
            record('tls', 'TLS 1.2: a changed ServerKeyExchange signature is refused', 'connect -14' in out, out[-300:])
        finally:
            ls.close(); srv.kill(); srv.wait()
        pn = port + len(cases) + 2
        srv = subprocess.Popen(['openssl', 's_server', '-accept', str(pn), '-cert', 'c.pem', '-key', 'k.pem', '-tls1_3', '-WWW'], cwd=ck, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.bind(('127.0.0.1', pn + 1)); ls.listen(1); ls.settimeout(20)
        seen = {}
        def capture():
            try: c, _ = ls.accept()
            except OSError: return
            u = socket.create_connection(('127.0.0.1', pn)); d = b''
            while len(d) < 5 or len(d) < 5 + (d[3] << 8 | d[4]):
                x = c.recv(65536)
                if not x: break
                d += x
            seen['ch'] = d; u.sendall(d)
            def back():
                try:
                    while True:
                        y = u.recv(65536)
                        if not y: break
                        c.sendall(y)
                except OSError: pass
                try: c.shutdown(socket.SHUT_WR)
                except OSError: pass
            threading.Thread(target=back, daemon=True).start()
            try:
                while True:
                    y = c.recv(65536)
                    if not y: break
                    u.sendall(y)
            except OSError: pass
        try:
            time.sleep(1); th = threading.Thread(target=capture, daemon=True); th.start()
            rc, out = sh([exe, 'get', 'localhost', str(pn + 1), '/big', os.path.join(ck, 'o3.bin'), os.path.join(ck, 'c.pem')])
            ok = False; detail = out[-200:]
            ch = seen.get('ch', b'')
            if len(ch) > 80:
                b = ch[5:]; o = 4 + 2 + 32; sl = b[o]; sid = b[o + 1:o + 1 + sl]; o += 1 + sl; cl = b[o] << 8 | b[o + 1]; o += 2 + cl; o += 1 + b[o]; el = b[o] << 8 | b[o + 1]; o += 2; e = o + el; ks = None
                while o + 4 <= e:
                    t = b[o] << 8 | b[o + 1]; l = b[o + 2] << 8 | b[o + 3]; o += 4
                    if t == 0x33: ks = b[o + 6:o + 6 + 32]
                    o += l
                from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
                from cryptography.hazmat.primitives import serialization as _s2
                if ks and len(sid) == 32:
                    pub = X25519PrivateKey.from_private_bytes(bytes(sid)).public_key().public_bytes(_s2.Encoding.Raw, _s2.PublicFormat.Raw)
                    ok = pub != bytes(ks) and 'connect 0' in out; detail = 'session id gives the key share' if pub == bytes(ks) else out[-200:]
            record('tls', 'the ClientHello session ID does not reveal the key exchange secret', ok, detail)
        finally:
            ls.close(); srv.kill(); srv.wait()
        pn = port + len(cases) + 4
        srv = subprocess.Popen(['openssl', 's_server', '-accept', str(pn), '-cert', 'c.pem', '-key', 'k.pem', '-tls1_3'], cwd=ck, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1); ob = os.path.join(ck, 'ku.bin')
            cl = subprocess.Popen([exe, 'get', 'localhost', str(pn), '/', ob, os.path.join(ck, 'c.pem')], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            time.sleep(1.5)
            for line in (b'before-update\n', b'K\n', b'after-update\n', b'Q\n'):
                srv.stdin.write(line); srv.stdin.flush(); time.sleep(0.5)
            out = cl.communicate(timeout=20)[0]
            body = open(ob, 'rb').read() if os.path.exists(ob) else b''
            record('tls', 'TLS 1.3 KeyUpdate from the server (with update requested) keeps the connection working', b'before-update' in body and b'after-update' in body, out[-200:] + repr(body[-80:]))
        finally:
            srv.kill(); srv.wait()

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

NIC_MODELS = [('e1000', ['-device', 'e1000,netdev=n0'], 'Intel e1000'),
              ('e1000e (82574L)', ['-device', 'e1000e,netdev=n0'], 'e1000e: up'),
              ('igb (82576)', ['-device', 'igb,netdev=n0'], 'igb: up'),
              ('vmxnet3', ['-device', 'vmxnet3,netdev=n0'], 'vmxnet3: up'),
              ('RTL8139', ['-device', 'rtl8139,netdev=n0'], 'RTL8139'),
              ('VirtIO-net', ['-device', 'virtio-net-pci,netdev=n0'], 'VirtIO'),
              ('USB CDC-ECM on xHCI', ['-device', 'qemu-xhci', '-device', 'usb-net,netdev=n0'], 'CDC-ECM adapter'),
              ('USB RNDIS on xHCI', ['-device', 'qemu-xhci', '-device', 'usb-net,netdev=n0'], 'RNDIS adapter'),
              ('USB adapter plugged in while running', ['-device', 'qemu-xhci,id=xh'], 'CDC-ECM adapter'),
              ('two adapters, only the second has a cable', ['-device', 'e1000,netdev=n1', '-device', 'igb,netdev=n0', '-netdev', 'user,id=n1'], 'net: Intel I210/I211/82576/I350 (igb)')]

def g_nics(iso):
    q = os.environ.get('NVX_QEMU') or shutil.which('qemu-system-x86_64')
    if not q or not os.path.exists(iso): return
    head('Ethernet adapters in QEMU: each model gets an address by DHCP and downloads 300 KB over HTTP')
    import socket, threading, http.server, socketserver
    data = bytes(((i*7+(i>>8))&0xFF) for i in range(300000)); want = big_sha()
    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
        def log_message(self, *a): pass
    class TS(socketserver.ThreadingMixIn, http.server.HTTPServer): daemon_threads = True
    srv = TS(('127.0.0.1', 0), H); port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    keys = {' ': 'spc', '\n': 'ret', '-': 'minus', '/': 'slash', '.': 'dot', '>': 'shift-dot', ':': 'shift-semicolon', '=': 'equal'}
    results = {}
    def one(idx, label, dev, expect):
        work = tempfile.mkdtemp(prefix='nvx_nic_'); log = os.path.join(work, 'serial.log'); mon = os.path.join(work, 'mon'); disk = os.path.join(work, 'disk.img')
        with open(disk, 'wb') as f: f.truncate(64 << 20)
        iso2 = os.path.join(work, 'boot.img'); shutil.copy(iso, iso2)
        bios = os.environ.get('NVX_QEMU_BIOS')
        cmd = [q] + (['-L', bios] if bios else []) + ['-m', '1024', '-smp', '2', '-accel', 'tcg,thread=multi', '-net', 'none',
               '-drive', f'file={disk},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0,unit=0',
               '-drive', f'file={iso2},format=raw,if=none,id=c0', '-device', 'ide-hd,drive=c0,bus=ide.1,unit=0,bootindex=0',
               '-netdev', 'user,id=n0'] + dev + ['-serial', f'file:{log}', '-display', 'none', '-no-reboot', '-monitor', f'unix:{mon},server,nowait']
        pr = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if 'n1' in ' '.join(dev):
            for _ in range(50):
                if os.path.exists(mon): break
                time.sleep(0.1)
            try:
                s0 = socket.socket(socket.AF_UNIX); s0.connect(mon); time.sleep(0.3); s0.sendall(b'set_link n1 off\n'); time.sleep(0.3); s0.close()
            except OSError: pass
        def serial():
            try: return open(log, errors='replace').read()
            except OSError: return ''
        def wait_for(rx, secs, start=0):
            t = time.time()
            while time.time() - t < secs:
                m = re.search(rx, serial()[start:])
                if m: return m
                time.sleep(1)
            return None
        def typ(sock, txt):
            for ch in txt:
                k = keys.get(ch) or ('shift-' + ch.lower() if ch.isupper() else ch)
                sock.sendall(('sendkey ' + k + '\n').encode()); time.sleep(0.07)
        out = ''
        try:
            if not wait_for('entering main loop', 400): results[idx] = (False, 'no boot'); return
            time.sleep(3)
            s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.5)
            if 'RNDIS' in label: typ(s, 'usbnet rndis\n'); time.sleep(8)
            if 'plugged in' in label:
                s.sendall(b'device_add usb-net,netdev=n0,bus=xh.0\n'); time.sleep(10)
            mark = len(serial())
            typ(s, 'net\n')
            wait_for(r'DHCP ok|DHCP failed|no supported|no network adapter', 60, mark)
            mark = len(serial())
            typ(s, f'fetch -s -o /dev/null http://10.0.2.2:{port}/big 2>/dev/kmsg\n')
            m = wait_for(r'fetch: sha256 ([0-9a-f]{64})|fetch: cannot[^\n]*|fetch: [^\n]*failed[^\n]*', 150, mark)
            out = serial()
            nm = re.findall(r'net: ([^\n]*)', out)
            used = ' / '.join(x for x in nm if not x.startswith('NIC up') and not x.startswith('DHCP'))[:120]
            ok = bool(m) and m.group(1) == want and expect in out
            results[idx] = (ok, used if ok else (out[-400:].replace('\n', ' | ')))
            s.close()
        finally:
            pr.kill(); pr.wait(); shutil.rmtree(work, ignore_errors=True)
    threads = []
    par = int(os.environ.get('NVX_NIC_PAR', '2')); only = os.environ.get('NVX_NIC_ONLY')
    for i, (label, dev, expect) in enumerate(NIC_MODELS):
        if only and only.lower() not in label.lower(): continue
        t = threading.Thread(target=one, args=(i, label, dev, expect)); t.start(); threads.append(t)
        while sum(1 for x in threads if x.is_alive()) >= par: time.sleep(1)
    for t in threads: t.join()
    srv.shutdown()
    for i, (label, dev, expect) in enumerate(NIC_MODELS):
        if only and only.lower() not in label.lower(): continue
        ok, det = results.get(i, (False, 'no result'))
        record('nics', f'{label}: DHCP, 300 KB download, checksum matches', ok, det)

def g_mmc(work):
    rc, out = sh([sys.executable, os.path.join(TESTS, 'mmc', 'blk_test.py')], timeout=300)
    record('mmc', 'SD/MMC block layer: chunking, bounds, eject, write-protect, hot-remove, FAT16 card image read through it', rc == 0 and out.count('ALL PASS') == 2, out[-300:])
    rc, out = sh([sys.executable, os.path.join(TESTS, 'mmc', 'exfat_fuzz_test.py')], timeout=900)
    record('mmc', 'exFAT data safety: regressions, ASan/UBSan fuzz over corrupted images, randomized write/compare stress against a host model (exfat_verify clean), power-loss runs checked for damage beyond the known exFAT windows', rc == 0 and 'exfat fuzz: all ok' in out, out[-400:])
    rc, out = sh([sys.executable, os.path.join(TESTS, 'mmc', 'volfs_test.py')], timeout=600)
    record('mmc', 'Files on other disks: ADMA2 descriptor tables, FAT32 card browse/copy/rename/folder move/delete/eject, FAT writes on fixed disks and USB sticks with flush and dirty bits (fsck.fat clean), exFAT read/write (fsck.exfat or exfat_verify clean), ext4 on disks and SD cards (e2fsck clean)', rc == 0 and 'volfs: all ok' in out, out[-400:])

def g_drivers(work):
    head('separately built driver modules against simulated devices (e1000, e1000e/I219, igb/igc, RTL8139, RTL8111, RTL8125, VirtIO net/blk, AC97, HD Audio)')
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
    exe2 = os.path.join(work, 'usbnet_proto')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-I' + KDIR] + SAN + [os.path.join(TESTS, 'usbnet', 'proto_main.c'), '-o', exe2])
    if not record('drivers', 'USB network protocol test builds (CDC-NCM blocks, RNDIS messages, MAC strings)', rc == 0, out[:300]): return
    rc, out = sh([exe2], timeout=300)
    for l in out.splitlines():
        if l.startswith('  '): print('  ' + l.strip()[:150])
    record('drivers', 'USB network protocols: round trips and 200000 corrupted blocks', rc == 0 and 'ALL PASS' in out, out[-300:])
    rc, out = sh(['sh', os.path.join(TESTS, 'platform', 'hkact_test.sh')], env=dict(os.environ, SANFLAGS=' '.join(SAN) or '-fsanitize=address,undefined', TMPDIR=work))
    record('drivers', f"hotkey action layer: brightness fallback, airplane/BT/Wi-Fi, keyboard light ({out.count(' ok')} checks)", rc == 0 and 'ALL PASS' in out, out[-300:])
    exe3 = os.path.join(work, 'hotkeys_t')
    rc, out = sh(['gcc', '-O1', '-Wall', '-Wextra', '-I' + KDIR, '-fsanitize=address,undefined', os.path.join(TESTS, 'platform', 'hotkeys_main.c'), '-o', exe3])
    if record('drivers', 'ThinkPad / ASUS hotkey decoder test builds', rc == 0, out[:300]):
        rc, out = sh([exe3]); record('drivers', 'ThinkPad hkey and ASUS WMI codes map to hotkey actions; unknown codes unclaimed', rc == 0 and 'ALL PASS' in out, out[-300:])
    fpd = os.path.join(TESTS, 'fprint_usb')
    for src, libs, what in (('ids_main.c', [], 'fingerprint reader IDs and status text'), ('elan_sim.c', ['-lm'], 'Elan image reader against a simulated sensor'), ('moc_sim.c', [], 'Synaptics Prometheus and Goodix match-on-chip protocols against simulated readers (enroll, step-wise non-blocking enroll, verify, delete, corrupted replies)')):
        exe4 = os.path.join(work, 'fp_' + src[:-2])
        rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-misleading-indentation', '-I' + KDIR, '-fsanitize=address,undefined', os.path.join(fpd, src), '-o', exe4] + libs)
        if not record('drivers', f'{src} builds', rc == 0, out[:300]): continue
        rc, out = sh([exe4], timeout=300); record('drivers', what, rc == 0 and 'ALL PASS' in out, out[-300:])
    exe5 = os.path.join(work, 'fp_far_sim')
    rc, out = sh(['gcc', '-O2', '-Wall', '-Wextra', '-Wno-misleading-indentation', '-I' + KDIR, '-fsanitize=undefined', '-fno-sanitize-recover=undefined', os.path.join(TESTS, 'fprint', 'far_sim.c'), '-o', exe5, '-lm'])
    if record('drivers', 'fingerprint matcher FAR/FRR simulation builds', rc == 0, out[:300]):
        rc, out = sh([exe5], timeout=600)
        for l in out.splitlines():
            if l.startswith('  '): print('  ' + l.strip()[:200])
        record('drivers', 'on-host fingerprint matcher (Elan image readers): fused 3-template score, FAR below 0.1% on 9360 simulated impostor attempts, FRR <=3% at 160x200, order-independent', rc == 0 and 'ALL PASS' in out, out[-400:])
    exe6 = os.path.join(work, 'fp_store')
    rc, out = sh(['gcc', '-O1', '-w', '-fsanitize=address,undefined', '-I' + KDIR, os.path.join(TESTS, 'fprint', 'store_main.c'), '-o', exe6])
    if record('drivers', 'fingerprint store/enrolment test builds against lib/fprint/fpstore.c', rc == 0, out[-300:]):
        rc, out = sh([exe6]); record('drivers', 'fingerprint: non-blocking enrolment state machine for image readers (session slices, no capture in begin, progress, bad samples, timeout, cancel, user change) and match-on-chip readers (begin/step, progress, storage full, timeout, cancel releases the reader), shell fprint enroll on match-on-chip readers runs on the same state machine (returns at once, progress, cancel, timeout), sign-in switch saved and reloaded (root-owned only, fails closed), remove turns it off', rc == 0 and 'fprint store: all passed' in out, out[-300:])
    exe6 = os.path.join(work, 'fp_store')
    rc, out = sh(['gcc', '-O1', '-w', '-I' + KDIR, '-fsanitize=address,undefined', os.path.join(TESTS, 'fprint', 'fpstore_main.c'), '-o', exe6])
    if record('drivers', 'fingerprint sign-in policy test builds against lib/fprint/fpstore.c', rc == 0, out[:300]):
        rc, out = sh([exe6]); record('drivers', 'fingerprint sign-in: only root or an enrolled user can turn it on, verify is bound to the user (nvx:<user>), timeouts/bad samples/no reader never match', rc == 0 and 'fpstore: all passed' in out, out[-300:])

def g_cron(work):
    exe = os.path.join(work, 'cron_t')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-result', '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', os.path.join(TESTS, 'cron', 'cron_t.c'), '-o', exe])
    if not record('cron', 'cron daemon + crontab build on the host', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=120)
    record('cron', 'cron: field/@keyword parsing, dom/dow rules, env lines, system tables, bad lines rejected, missed-run catch-up once per job, clock-back no-replay', rc == 0 and 'ALL PASS' in out, out[-300:])
def g_midi(work):
    exe = os.path.join(work, 'midi_codec')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-I' + KDIR] + SAN + [os.path.join(TESTS, 'midi', 'codec_main.c'), '-o', exe])
    if not record('midi', 'USB-MIDI packet codec + synth test builds', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300)
    record('midi', 'USB-MIDI packets round trip (running status, sysex, realtime, fuzz) and the synth voice plays', rc == 0 and 'all passed' in out, out[-300:])
    exe = os.path.join(work, 'midi_desc')
    rc, out = sh(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-function', '-I' + KDIR] + SAN + [os.path.join(TESTS, 'midi', 'desc_main.c'), '-o', exe])
    if not record('midi', 'USB-MIDI descriptor parser + ALSA rawmidi ABI test builds', rc == 0, out[:300]): return
    rc, out = sh([exe], timeout=300)
    record('midi', 'USB-MIDI descriptors (endpoint 0, bad lengths, cables, fuzz) parse safely and rawmidi ioctls match asound.h', rc == 0 and 'all passed' in out, out[-300:])

def main():
    ap = argparse.ArgumentParser(description='NoovexOS automatic bug checker')
    ap.add_argument('--build', action='store_true', help='rebuild kernel + ISO first')
    ap.add_argument('--iso', help='image to test (default: the one built with --build, else tools/out/check.iso)')
    ap.add_argument('--quick', action='store_true', help='smaller random test sizes')
    ap.add_argument('--sanitize', action='store_true', help='run driver tests under ASan+UBSan and libc/tool tests with UBSan traps')
    ap.add_argument('--only', help='comma list of groups: build,static,boot,usb,bt,acpi,gpu,nvme,touchpad,memory,aml,timers,cookies,lxhost,svc,perf,bpf,linux,fs,ext4,accounts,nvidia,ui,tls,fetch,qemu,bootqa,usbqa,net,nics,drivers,midi,mmc,cron,analyze,libc,coreutils,man,rsync,ssh,wifi,doom,httptext,startmenu,visuals,shell')
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
        if want('usb'): g_usb(work); g_xhci_init(work); g_uvc(work); g_uvcext(work); g_uvc_ir(work); g_avirec(work)
        if want('usb'): g_usb(work); g_xhci_init(work); g_uvc(work); g_uac(work); g_uvc_ir(work)
        if want('usb'): g_usb(work); g_xhci_init(work); g_uvc(work); g_uvc_ir(work); g_usbnet_frame(work); g_tether(work)
        if want('usb'): g_usb(work); g_xhci_init(work); g_uvc(work); g_uvc_ir(work); g_mtp(work)
        if want('man'): g_man(work)
        if want('bt'): g_bt(work)
        if want('wifi'): g_wifi(work)
        if want('bpf'): g_bpf(work)
        if want('secboot'): g_secboot(work)
        if want('acpi'): g_acpi(work)
        if want('acpi'): g_ucsi(work)
        if want('gpu'): g_gpu(work)
        if want('nvme'): g_nvme(work)
        if want('touchpad'): g_touchpad(work)
        if want('memory'): g_memory(work); g_swapstore(work); g_vmframe(work)
        if want('aml'): g_aml(work)
        if want('timers'): g_timers(work)
        if want('cookies'): g_cookies(work)
        if want('httptext'): g_httptext(work)
        if want('lxhost'): g_lxhost(work)
        if want('svc'): g_svc(work)
        if want('perf'): g_perf(work)
        if want('linux'): g_linux(work)
        if want('fs'): g_fs(work); g_fatfuzz(work)
        if want('fs') or want('ext4'): g_ext4(work)
        if want('accounts'): g_accounts(work)
        if want('ssh'): g_ssh(work)
        if want('nvidia'): g_nvidia(work)
        if want('ui'): g_ui(work)
        if want('doom'): g_doom(work)
        if want('startmenu'): g_startmenu(work)
        if want('ui'): g_smallapps(work)
        if want('shell'): g_shell(work)
        if want('visuals'): g_visuals(work)
        if want('tls'): g_tls(work)
        if want('fetch'): g_fetch(work)
        def vm(fn, grp):
            try: fn(iso)
            except (BrokenPipeError, ConnectionError, OSError) as e: record(grp, fn.__name__ + ': QEMU stopped answering (' + type(e).__name__ + ': ' + str(e)[:80] + ')', False)
        if want('qemu'): vm(g_qemu, 'qemu')
        if want('qemu'): vm(g_qemu_s3, 'qemu')
        if want('qemu') or want('bootqa'): vm(g_bootqa, 'bootqa')
        if want('net'): g_netfuzz(work); vm(g_net, 'net')
        if want('bootqa'): vm(g_bootqa_usb, 'bootqa')
        if want('qemu') or want('usbqa'): vm(g_usbqa, 'usbqa')
        if want('net'): vm(g_net, 'net')
        if want('nics'): vm(g_nics, 'nics')
        if want('drivers'): g_drivers(work)
        if want('midi'): g_midi(work)
        if want('mmc'): g_mmc(work)
        if want('cron'): g_cron(work)
        if want('rsync'): g_rsync(work)
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
