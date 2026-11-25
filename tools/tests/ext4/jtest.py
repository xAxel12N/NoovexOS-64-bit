#!/usr/bin/env python3
import os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
fails = []
def sh(cmd, inp=None):
    p = subprocess.run(cmd, input=inp, capture_output=True, text=True, timeout=300); return p.returncode, p.stdout + p.stderr
def ok(name, cond, detail=''):
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else ' :: ' + detail[-400:])); cond or fails.append(name)
def blk(img, b, bs):
    with open(img, 'rb') as f: f.seek(b * bs); return f.read(bs)
def pat(b, x, bs, m=False):
    d = bytearray([x]) * bs
    for k in range(0, bs, 64): d[k] = (b + k // 64) & 0xFF
    if m: d[0:4] = bytes([0xC0, 0x3B, 0x39, 0x98])
    return bytes(d)
def fsck(img, fix=False):
    rc, out = sh(['e2fsck', '-fy' if fix else '-fn', img]); return rc, out
def main():
    for t in ('mke2fs', 'e2fsck', 'debugfs'):
        if not shutil.which(t): print(f'skipped: {t} missing'); return 0
    w = tempfile.mkdtemp(prefix='ext4j_'); jt = os.path.join(w, 'jtest')
    rc, out = sh(['gcc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-DEXT4_HOST', '-I', SRC, '-o', jt, os.path.join(HERE, 'jtest.c'), os.path.join(SRC, 'fs/ext4/journal.c'), os.path.join(SRC, 'fs/ext4/ext4.c')])
    ok('harness builds against fs/ext4/journal.c under ASan+UBSan', rc == 0, out)
    if rc: return 1
    cfgs = [('ext4 4k csum_v3', ['-t', 'ext4', '-b', '4096'], '64M', '-c'), ('ext4 4k no journal csum', ['-t', 'ext4', '-b', '4096'], '64M', ''),
            ('ext4 1k csum_v3', ['-t', 'ext4', '-b', '1024'], '32M', '-c'), ('ext3 32-bit tags', ['-t', 'ext3', '-b', '4096'], '64M', '')]
    for name, mk, size, jc in cfgs:
        base = os.path.join(w, 'base.img'); 
        if os.path.exists(base): os.unlink(base)
        rc, out = sh(['mke2fs', '-q', '-F'] + mk + [base, size]); ok(f'{name}: mke2fs', rc == 0, out)
        if jc: sh(['debugfs', '-w', '-f', '-', base], 'jo -c\njc\n')
        bs = int(re.search(r'Block size:\s+(\d+)', sh(['dumpe2fs', '-h', base])[1]).group(1))
        nb = int(re.search(r'Block count:\s+(\d+)', sh(['dumpe2fs', '-h', base])[1]).group(1))
        free = [int(x) for x in re.findall(r'\d+', sh(['debugfs', '-R', f'ffb 4 {nb // 2}', base])[1].split('found:')[-1])][:4]
        bl = os.path.join(w, 'bl'); open(bl, 'w').write(subprocess.run(['debugfs', '-R', 'blocks <8>', base], capture_output=True, text=True).stdout)
        X, Y, Z, V = free
        img, ref = os.path.join(w, 'img'), os.path.join(w, 'ref')
        data = os.path.join(w, 'd'); open(data, 'wb').write(os.urandom(2 * bs)); zd = os.path.join(w, 'z'); open(zd, 'wb').write(pat(Z, 0x5A, bs, True))
        shutil.copy(base, img); sh(['debugfs', '-w', '-f', '-', img], f'jo\njw -b {X},{Y} {data}\njw -r {Y}\njw -b {Z} {zd}\njc\n'); shutil.copy(img, ref); fsck(ref, True)
        rc, out = sh([jt, img, bl, 'replay'])
        ok(f'{name}: replays a journal written by debugfs (2 tx + revoke + escaped block)', rc == 0 and 'replayed 2 revoked 1' in out, out)
        ok(f'{name}: replayed blocks match e2fsck replay', all(blk(img, b, bs) == blk(ref, b, bs) for b in (X, Y, Z)) and blk(img, X, bs) == open(data, 'rb').read()[:bs] and blk(img, Z, bs) == pat(Z, 0x5A, bs, True), out)
        rc, out = fsck(img); ok(f'{name}: e2fsck -fn clean after our replay', rc == 0, out)
        args = [f'{X}:a1', f'{Y}:b2:m', f'{Z}:c3']
        shutil.copy(base, img); rc, out = sh([jt, img, bl, 'commit', '-1'] + args)
        ok(f'{name}: journaled commit + checkpoint', rc == 0 and 'commit 0 ok 1' in out, out)
        ok(f'{name}: committed blocks at home location', blk(img, X, bs) == pat(X, 0xA1, bs) and blk(img, Y, bs) == pat(Y, 0xB2, bs, True) and blk(img, Z, bs) == pat(Z, 0xC3, bs))
        rc, out = fsck(img); ok(f'{name}: e2fsck -fn clean after journaled commit', rc == 0, out)
        for crash, label, want in ((6, 'after commit block', True), (5, 'before commit block', False)):
            shutil.copy(base, img); sh([jt, img, bl, 'commit', str(crash)] + args); shutil.copy(img, ref)
            rc, out = sh(['dumpe2fs', '-h', img]); ok(f'{name}: crash {label} leaves needs_recovery set', 'needs_recovery' in out, out)
            rc, fo = fsck(ref, True)
            exp = [pat(X, 0xA1, bs), pat(Y, 0xB2, bs, True), pat(Z, 0xC3, bs)] if want else [blk(base, b, bs) for b in (X, Y, Z)]
            ok(f'{name}: crash {label}: e2fsck replays our journal as expected', [blk(ref, b, bs) for b in (X, Y, Z)] == exp, fo)
            rc, out = sh([jt, img, bl, 'replay'])
            ok(f'{name}: crash {label}: our replay gives the same blocks', rc == 0 and [blk(img, b, bs) for b in (X, Y, Z)] == exp, out)
            rc, out = fsck(img); ok(f'{name}: crash {label}: e2fsck -fn clean after our replay', rc == 0, out)
        if jc:
            shutil.copy(base, img); sh([jt, img, bl, 'commit', '6'] + args)
            jb = [int(x) for x in open(bl).read().split()]
            with open(img, 'r+b') as f: f.seek(jb[3] * bs + 100); c = f.read(1); f.seek(jb[3] * bs + 100); f.write(bytes([c[0] ^ 0xFF]))
            rc, out = sh([jt, img, bl, 'replay'])
            ok(f'{name}: corrupt journal data block is skipped (tag checksum)', rc == 0 and 'replayed 2' in out and 'skipped 1' in out and blk(img, Y, bs) == blk(base, Y, bs), out)
            shutil.copy(base, img); sh([jt, img, bl, 'commit', '6'] + args)
            with open(img, 'r+b') as f: f.seek(jb[5] * bs + 40); f.write(b'\x01')
            rc, out = sh([jt, img, bl, 'replay'])
            ok(f'{name}: corrupt commit block ends the log (nothing replayed)', rc == 0 and 'replayed 0' in out and blk(img, X, bs) == blk(base, X, bs), out)
            rc, out = fsck(img); ok(f'{name}: e2fsck -fn clean after rejected transaction', rc == 0, out)
    shutil.rmtree(w, ignore_errors=True)
    print('ext4 journal: all ok' if not fails else f'ext4 journal: {len(fails)} failed'); return 1 if fails else 0
sys.exit(main())
