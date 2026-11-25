#!/usr/bin/env python3
import os, re, shutil, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
fails = []
def sh(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300); return p.returncode, p.stdout + p.stderr
def ok(name, cond, detail=''):
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else ' :: ' + detail[-600:])); cond or fails.append(name); return cond
def patch_sb(img, off, fmt, val):
    with open(img, 'r+b') as f: f.seek(1024 + off); f.write(struct.pack(fmt, val))
def main():
    if not shutil.which('mke2fs'): print('skipped: mke2fs missing'); return 0
    w = tempfile.mkdtemp(prefix='ext4f_'); x = os.path.join(w, 'fuzz')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-DEXT4_HOST', '-I', SRC, '-o', x, os.path.join(HERE, 'fuzz.c')] + [os.path.join(SRC, 'fs/ext4', f) for f in ('ext4.c', 'journal.c')])
    if not ok('ext4 fuzz harness builds under ASan+UBSan', rc == 0, out): return 1
    img = os.path.join(w, 'img'); base = os.path.join(w, 'base')
    for name, mk in (('4k csum', ['-t', 'ext4', '-b', '4096']), ('4k no csum', ['-t', 'ext4', '-b', '4096', '-O', '^metadata_csum']), ('1k', ['-t', 'ext4', '-b', '1024']), ('no journal', ['-t', 'ext4', '-O', '^has_journal,^metadata_csum'])):
        if os.path.exists(base): os.unlink(base)
        sh(['mke2fs', '-q', '-F'] + mk + [base, '32M'])
        bad = []
        for c in range(9):
            shutil.copy(base, img); rc, out = sh([x, img, 'ops', str(c)])
            if rc or 'ext4 fuzz: all ok' not in out: bad.append(f'case {c}: {out[-300:]}')
        ok(f'{name}: crafted extent/dirent/symlink/uninit/rename cases fail closed without memory errors', not bad, ' | '.join(bad))
    os.unlink(base)
    sh(['mke2fs', '-q', '-F', '-t', 'ext4', '-b', '1024', '-O', '^metadata_csum', base, '32M'])
    for name, off, fmt, val, mode in (('inode_size 768 on 1k blocks', 0x58, '<H', 768, 'mountfail'), ('inode_size not a power of two', 0x58, '<H', 384, 'mountfail'),
                                      ('inodes_count beyond groups', 0x0, '<I', 0xFFFFFFF0, 'mountfail'), ('first_data_block past end', 0x14, '<I', 0x7FFFFFFF, 'mountfail'),
                                      ('orphan list present', 0xE8, '<I', 12, 'ro')):
        shutil.copy(base, img); patch_sb(img, off, fmt, val)
        rc, out = sh([x, img, mode]); ok(f'superblock: {name}', rc == 0, out)
    for name, mk in (('4k sparse_super', ['-t', 'ext4', '-b', '4096']), ('1k sparse_super', ['-t', 'ext4', '-b', '1024']), ('4k sparse_super2', ['-t', 'ext4', '-b', '4096', '-O', 'sparse_super2', '-E', 'num_backup_sb=2']),
                     ('1k no sparse_super', ['-t', 'ext4', '-b', '1024', '-O', '^sparse_super,^resize_inode']), ('1k meta_bg', ['-t', 'ext4', '-b', '1024', '-O', 'meta_bg,^resize_inode']), ('4k no journal', ['-t', 'ext4', '-O', '^has_journal'])):
        if os.path.exists(base): os.unlink(base)
        rc, out = sh(['mke2fs', '-q', '-F'] + mk + [base, '300M'])
        if not ok(f'{name}: mke2fs', rc == 0, out): continue
        rc, d = sh(['dumpe2fs', base]); exp = {}; g = None
        for ln in d.splitlines():
            mg = re.match(r'Group (\d+): \(Blocks (\d+)-', ln)
            if mg: g = int(mg.group(1)); continue
            if g is not None and ('superblock at' in ln or 'descriptor' in ln or 'Reserved GDT' in ln):
                for a, b in re.findall(r'(\d+)(?:-(\d+))?', ln.split(':', 1)[-1] if 'Reserved' in ln else ln): exp.setdefault(g, set()).update(range(int(a), int(b or a) + 1))
        rc, out = sh([x, base, 'sblist']); got = {}; gd = []
        for ln in out.splitlines():
            p = ln.split()
            if p and p[0] == 'G': got[int(p[1])] = set(range(int(p[2]), int(p[2]) + int(p[3])))
            if p and p[0] == 'D': gd.append(int(p[2]))
        ok(f'{name}: backup superblock/GDT layout matches dumpe2fs', rc == 0 and got == exp and all(any(b in v for v in exp.values()) for b in gd), f'{out[-300:]} exp={sorted((k, min(v), len(v)) for k, v in exp.items())[:12]}')
        if 'meta_bg' in name: ok(f'{name}: mounts read-only', 'ro 1' in out, out); continue
        for mode in ('bkfree', 'bkalloc'):
            shutil.copy(base, img); rc, out = sh([x, img, mode]); ok(f'{name}: {mode} fails closed', rc == 0 and 'all ok' in out, out)
            if mode == 'bkfree': continue
            rc, fo = sh(['e2fsck', '-fn', img]); ok(f'{name}: backup superblock still valid after {mode}', 'Bad magic' not in fo, fo)
    shutil.rmtree(w, ignore_errors=True)
    print('ext4 fuzz: all ok' if not fails else f'ext4 fuzz: {len(fails)} failed'); return 1 if fails else 0
sys.exit(main())
