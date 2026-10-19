#!/usr/bin/env python3
import hashlib, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)
TESTS = os.path.join(HERE, 'tests')
sys.path.insert(0, TESTS)
from fatck import Fat
import pecheck

class Report:
    def __init__(self): self.rows = []; self.section = ''
    def head(self, title):
        self.section = title; print(f"\n== {title} ==")
    def check(self, name, ok, detail=''):
        self.rows.append((self.section, name, bool(ok), detail))
        print(f"  {name:64s} {'ok' if ok else 'FAIL'}" + (f"   ({detail})" if detail and not ok else ''))
        return ok
    def failed(self): return [r for r in self.rows if not r[2]]

def sh(cmd, cwd=None, env=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, shell=isinstance(cmd, str), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr

def immutable_size_for(k):
    kb = os.path.join(SRC, 'build/kernel64.bin'); ke = os.path.join(SRC, 'build/kernel64.elf')
    if not (os.path.exists(kb) and os.path.exists(ke)) or open(kb, 'rb').read() != k: return None
    rc, out = sh(['nm', ke])
    for l in out.splitlines():
        p = l.split()
        if len(p) == 3 and p[2] == '__immutable_end': return int(p[0], 16) - 0x100000
    return None

def signature_ok(k):
    i = k.find(b'NVX64SIG')
    if i < 0: return False, 'no NVX64SIG signature'
    hash_off, full_off = i + 32, i + 88
    t = bytearray(k); stored_imm = bytes(t[hash_off:hash_off + 32]); stored_full = bytes(t[full_off:full_off + 32])
    t[hash_off:hash_off + 32] = bytes(32)
    imm = immutable_size_for(k)
    a = None if imm is None else hashlib.sha256(bytes(t[:imm])).digest() == stored_imm
    t[full_off:full_off + 32] = bytes(32)
    b = hashlib.sha256(bytes(t)).digest() == stored_full
    return (b and a is not False), f'full={b} immutable={"not checked (no matching ELF)" if a is None else a}'

def multiboot_header(k):
    for off in range(0, min(len(k), 8192) - 12, 4):
        m, f, c = struct.unpack_from('<III', k, off)
        if m == 0x1BADB002 and (m + f + c) & 0xFFFFFFFF == 0: return off
    return -1

def extract(iso):
    out = {}
    c = iso[19 * 2048:20 * 2048]
    br = iso[17 * 2048:18 * 2048]
    if br[1:6] == b'CD001':
        cat = struct.unpack_from('<I', br, 0x47)[0]; c = iso[cat * 2048:cat * 2048 + 2048]
        off = 64
        while off < 2048 and c[off] in (0x90, 0x91):
            hid, plat, cnt = c[off], c[off + 1], struct.unpack_from('<H', c, off + 2)[0]
            for k in range(cnt):
                e = c[off + 32 + k * 32:off + 64 + k * 32]
                if plat == 0xEF and e[0] == 0x88:
                    rba, n = struct.unpack_from('<I', e, 8)[0], struct.unpack_from('<H', e, 6)[0]
                    if n == 0:
                        tot = struct.unpack_from('<I', iso, rba * 2048 + 32)[0] or struct.unpack_from('<H', iso, rba * 2048 + 19)[0]
                        n = tot if tot else (len(iso) - rba * 2048) // 512
                    f = Fat(iso[rba * 2048:rba * 2048 + n * 512], 0); f.check(); f.errs0 = list(f.err)
                    out['cd'] = (f, f.read('/EFI/BOOT/BOOTX64.EFI'), f.read('/KERNEL64.BIN') or f.read('/boot/kernel64.bin'))
            off += 32 + cnt * 32
            if hid == 0x91: break
    if iso[510:512] == b'\x55\xaa':
        for i in range(4):
            pe = iso[0x1BE + i * 16:0x1CE + i * 16]
            if pe[4] in (0x0B, 0x0C, 0xEF):
                start = struct.unpack_from('<I', pe, 8)[0]
                g = Fat(iso, start * 512); g.check(); g.errs0 = list(g.err)
                out['usb'] = (g, g.read('/EFI/BOOT/BOOTX64.EFI'), g.read('/KERNEL64.BIN') or g.read('/boot/kernel64.bin'))
                break
    return out

def emulate_bios(R, image, work):
    R.head('BIOS boot (real stage1/stage2 code paths)')
    img = open(image, 'rb').read()
    R.check('MBR boot signature 0x55AA', img[510:512] == b'\x55\xaa')
    R.check('MBR has boot code (stage1 present)', any(img[0:440]))
    s2 = os.path.join(SRC, 'boot/build/stage2.bin')
    if os.path.exists(s2):
        st = open(s2, 'rb').read()
        R.check('stage2 at sector 1 matches build/stage2.bin byte-exact', img[512:512 + len(st)] == st)
    bsrc = os.path.join(SRC, 'boot/src')
    s2c = open(os.path.join(bsrc, 'stage2.c')).read()
    a = s2c.find('    uint32_t part_lba = 0;'); b = s2c.find('if(!part_lba) fail', a); b = s2c.find('\n', b) + 1
    if not R.check('partition-selection code found in stage2.c', a >= 0 and b > a): return
    emu = open(os.path.join(TESTS, 'bios_emu.c')).read().replace('@@PARTSEL@@', s2c[a:b])
    ec = os.path.join(work, 'bios_emu.c'); open(ec, 'w').write(emu)
    exe = os.path.join(work, 'bios_emu')
    flags = ['-m32', '-ffreestanding', '-nostdlib', '-fno-pie', '-fno-pic', '-fno-stack-protector', '-Os',
             '-march=i386', '-mno-sse', '-mno-mmx', '-mno-80387', '-static', '-I', bsrc, '-w']
    rc, out = sh(['gcc'] + flags + ['-o', exe, ec] + [os.path.join(bsrc, f) for f in ('fat.c', 'config.c', 'string.c')])
    if not R.check('built the real fat.c/config.c/string.c as a 32-bit program', rc == 0, out[:200]): return
    kout = os.path.join(work, 'bios_kernel.bin')
    rc, out = sh([exe, image, kout])
    lines = out.strip().splitlines(); print('     ' + '\n     '.join(lines))
    R.check('stage2 logic: partition found, FAT32 mounted', any(l.startswith('PART ') for l in lines) and 'MOUNT ok' in lines, out[-200:])
    R.check('boot.cfg read and parsed, default entry valid', any(l.startswith('CFG ') for l in lines) and rc == 0, out[-200:])
    if rc != 0 or not os.path.exists(kout): R.check('kernel loaded', False, out[-200:]); return
    k = open(kout, 'rb').read()
    R.check('kernel read through stage2 FAT code is a flat image', any('format=flat' in l for l in lines))
    mb = multiboot_header(k)
    R.check('kernel has a valid Multiboot header in its first 8 KB', mb >= 0, f'offset {mb}')
    ok, d = signature_ok(k); R.check('kernel build signature valid (' + d + ')', ok, d)
    return k

def emulate_uefi(R, image, work, kernel_ref):
    iso = open(image, 'rb').read(); paths = extract(iso)
    R.head('UEFI boot media (firmware discovery paths)')
    for name, label in (('cd', 'CD/DVD/VirtualBox (El Torito EFI entry)'), ('usb', 'USB stick (MBR ESP)')):
        if name not in paths: R.check(f'{label}: path present', False, 'not found'); continue
        f, efi, kern = paths[name]
        R.check(f'{label}: volume is FAT{f.type} by cluster count, no errors', not f.errs0 and (name == 'cd' or f.type == 32), f'{f.type} {f.errs0[:2]}')
        R.check(f'{label}: \\EFI\\BOOT\\BOOTX64.EFI present', bool(efi))
        R.check(f'{label}: kernel present', bool(kern))
        if efi:
            p = os.path.join(work, f'{name}_BOOTX64.EFI'); open(p, 'wb').write(efi)
            errs, _ = pecheck.check(p); R.check(f'{label}: BOOTX64.EFI passes PE checks', not errs, '; '.join(errs[:2]))
        if kern and kernel_ref is not None: R.check(f'{label}: kernel identical to the BIOS-path kernel', kern == kernel_ref)
    if 'cd' not in paths or not paths['cd'][1]: return
    R.head('UEFI loader run (real BOOTX64.EFI, simulated firmware)')
    f, efi, kern = paths['cd']
    open(os.path.join(work, 'kernel64.bin'), 'wb').write(kern)
    exe = os.path.join(work, 'pe_run')
    rc, out = sh(['gcc', '-O1', '-w', '-fshort-wchar', '-D_GNU_SOURCE', '-o', exe, os.path.join(TESTS, 'pe_run.c')])
    if R.check('built the PE loader / firmware simulator', rc == 0, out[:200]):
        rc, out = sh([exe, os.path.join(work, 'cd_BOOTX64.EFI')], cwd=work)
        for l in out.splitlines():
            if 'FAIL' in l: print('     ' + l)
        R.check('BOOTX64.EFI from the ISO boots to the kernel entry (all scenarios)', rc == 0 and 'ALL PASS' in out, out[-300:])
    ub = os.path.join(SRC, 'boot/uefi/uefiboot.c')
    sim = os.path.join(work, 'sim')
    rc, out = sh(f"gcc -O1 -w -DUEFI_TEST -fshort-wchar -D_GNU_SOURCE -c '{ub}' -o '{work}/lt.o' && gcc -O1 -w -fshort-wchar -D_GNU_SOURCE -c '{TESTS}/firmware_sim.c' -o '{work}/fs.o' && gcc -o '{sim}' '{work}/fs.o' '{work}/lt.o'")
    if R.check('built the loader C code against the firmware simulator', rc == 0, out[:200]):
        rc, out = sh([sim], cwd=work)
        for l in out.splitlines():
            if 'FAIL' in l: print('     ' + l)
        R.check('firmware scenarios (missing kernel, RGB, ExitBootServices retry, ...)', rc == 0 and 'ALL PASS' in out, out[-300:])

def emulate_kernel_entry(R, work):
    R.head('Kernel UEFI entry (boot64.S code against test memory)')
    s = open(os.path.join(SRC, 'arch/x86_64/boot/boot64.S')).read()
    u = s.find('uefi_entry:'); a = s.find('    cli\n    cld\n', u); m = s.find('    movl    $2, %edi', a); b = s.find('5:  movabsq $pml4, %rax', a)
    if not R.check('uefi_entry code located', u >= 0 and a > u and m > a and b > m): return
    body = s[a:b].replace('    cli\n', '').replace('    movq    %cr4, %rax\n    btrq    $23, %rax\n    movq    %rax, %cr4\n', '').replace('movq    0x5000, %rax', 'movq    t_fb(%rip), %rax')
    asm = ('.text\n.globl run_entry\nrun_entry:\n    push %rbx\n' + body +
           '5:  movabsq $pml4, %rax\n    movl    $4, %edi\n    movl    $0x003080FF, %esi\n    call    uefi_mark\n    pop %rbx\n    ret\n.section .note.GNU-stack,"",@progbits\n')
    open(os.path.join(work, 'entry.S'), 'w').write(asm)
    rc, out = sh(['gcc', '-O0', '-no-pie', '-fno-pie', '-o', os.path.join(work, 'entry_t'), os.path.join(TESTS, 'entry_t.c'), os.path.join(work, 'entry.S')])
    if R.check('built the entry-code test', rc == 0, out[:200]):
        rc, out = sh([os.path.join(work, 'entry_t')])
        R.check('BSS cleared, identity map 0-4 GB, framebuffer above 4 GB mapped, markers', rc == 0 and 'ALL PASS' in out, out[-300:])
    m0 = s.find('uefi_mark:'); m1 = s.find('    ret', m0) + len('    ret')
    open(os.path.join(work, 'mark.S'), 'w').write('.text\n.globl uefi_mark\n' + s[m0:m1] + '\n.section .note.GNU-stack,"",@progbits\n')
    rc, out = sh(['gcc', '-O0', '-no-pie', '-fno-pie', '-o', os.path.join(work, 'mark_t'), os.path.join(TESTS, 'mark_t.c'), os.path.join(work, 'mark.S')])
    if R.check('built the progress-marker test', rc == 0, out[:200]):
        rc, out = sh([os.path.join(work, 'mark_t')])
        R.check('progress markers drawn exactly in place', rc == 0 and 'FAIL' not in out, out[-200:])

def main(argv):
    image = argv[1] if len(argv) > 1 else None
    if not image:
        print('usage: emulate.py IMAGE.iso'); return 2
    R = Report()
    work = tempfile.mkdtemp(prefix='nvx_emu_')
    try:
        k = emulate_bios(R, image, work)
        emulate_uefi(R, image, work, k)
        emulate_kernel_entry(R, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    bad = R.failed()
    print(f"\n{len(R.rows) - len(bad)} of {len(R.rows)} checks passed" + ('' if not bad else ' -- FAILURES:'))
    for sec, name, ok, d in bad: print(f"  [{sec}] {name} {d}")
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
