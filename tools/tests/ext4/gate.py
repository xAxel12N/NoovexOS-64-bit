#!/usr/bin/env python3
import os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
fails = []
def sh(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=300); return p.returncode, p.stdout + p.stderr
def ok(name, cond, detail=''):
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else ' :: ' + detail[-400:])); cond or fails.append(name); return cond
def main():
    for t in ('mke2fs', 'e2fsck'):
        if not shutil.which(t): print(f'skipped: {t} missing'); return 0
    w = tempfile.mkdtemp(prefix='ext4g_'); g = os.path.join(w, 'gate')
    rc, out = sh(['gcc', '-O1', '-g', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-DEXT4_HOST', '-I', SRC, '-o', g, os.path.join(HERE, 'gate.c')] + [os.path.join(SRC, 'fs/ext4', f) for f in ('ext4.c', 'journal.c')])
    if not ok('ext4 driver + journal build on the host under ASan+UBSan', rc == 0, out): return 1
    for name, mk in (('ext4 4k metadata_csum', ['-t', 'ext4', '-b', '4096']), ('ext4 4k no csum', ['-t', 'ext4', '-b', '4096', '-O', '^metadata_csum']), ('ext4 1k', ['-t', 'ext4', '-b', '1024']), ('ext4 no journal', ['-t', 'ext4', '-O', '^has_journal'])):
        base = os.path.join(w, 'base.img'); img = os.path.join(w, 'img')
        if os.path.exists(base): os.unlink(base)
        sh(['mke2fs', '-q', '-F'] + mk + [base, '64M'])
        shutil.copy(base, img); rc, out = sh([g, img, 'mount']); ok(f'{name}: rw mount + umount', rc == 0, out)
        rc, fo = sh(['e2fsck', '-fn', img]); ok(f'{name}: e2fsck -fn clean after mount/umount', rc == 0, fo)
        shutil.copy(base, img); rc, out = sh([g, img, 'ops']); ok(f'{name}: mkdir/create/write/rename/unlink/truncate/read back', rc == 0 and 'ops ok' in out, out)
        rc, fo = sh(['e2fsck', '-fn', img]); ok(f'{name}: e2fsck -fn clean after writes', rc == 0, fo)
        m = re.search(r'writes (\d+)', out); total = int(m.group(1)) if m else 0
        if 'no journal' in name or not total: continue
        bad = []
        for c in sorted(set([1, 3, 7, total // 4, total // 2, total - 3, total - 1])):
            if c <= 0 or c >= total: continue
            shutil.copy(base, img); sh([g, img, 'ops', str(c)])
            rc, out = sh([g, img, 'mount'])
            rc2, fo = sh(['e2fsck', '-fn', img])
            if rc or rc2: bad.append(f'crash@{c}: {out[-120:]} {fo[-200:]}')
        ok(f'{name}: power cut at 7 points during writes, our mount replays, e2fsck -fn clean', not bad, ' | '.join(bad))
    shutil.rmtree(w, ignore_errors=True)
    print('ext4 gate: rw verified' if not fails else f'ext4 gate: {len(fails)} failed (keep EXT4_RW_VERIFIED 0)'); return 1 if fails else 0
sys.exit(main())
