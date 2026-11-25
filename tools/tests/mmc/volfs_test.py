import atexit, glob, gzip, os, shutil, signal, struct, subprocess, sys, tempfile, time
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.abspath(os.path.join(here, '..', '..', '..'))
for old in glob.glob(os.path.join(tempfile.gettempdir(), 'volfs*')):
    try:
        if os.path.isdir(old) and time.time() - os.path.getmtime(old) > 3600: shutil.rmtree(old, ignore_errors=True)
    except OSError: pass
t = tempfile.mkdtemp(prefix='volfs'); exe = os.path.join(t, 'volfs')
atexit.register(shutil.rmtree, t, True)
for sg in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP): signal.signal(sg, lambda n, f: sys.exit(128 + n))
def rm(*ps):
    for p in ps:
        try: os.remove(p)
        except OSError: pass
def run(a, **k): return subprocess.run(a, capture_output=True, text=True, **k)
src = [os.path.join(here, 'volfs_main.c')] + [os.path.join(root, f) for f in ('fs/vfs/volfs.c', 'fs/fat/vfat.c', 'fs/fat/exfat.c', 'drivers/mmc/mmc_blk.c', 'fs/ext4/ext4_vfs.c', 'fs/ext4/ext4.c', 'fs/ext4/journal.c')]
r = run(['gcc', '-O1', '-g', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-DEXT4_HOST', '-I' + root, '-include', 'stdint.h'] + src + ['-o', exe])
if r.returncode: print(r.stderr[-3000:]); sys.exit(1)
ok = True
sec = 143360; start = 2048; card = os.path.join(t, 'card.img'); out = os.path.join(t, 'card.out')
with open(card, 'wb') as f: f.truncate(sec * 512)
r = run(['mkfs.fat', '-F', '32', '-n', 'SDCARD', '--offset', str(start), card, str((sec - start) // 2)])
if r.returncode: print(r.stdout, r.stderr); sys.exit(1)
with open(card, 'r+b') as f:
    f.seek(446); f.write(struct.pack('<B3sB3sII', 0, b'\0\0\0', 0x0C, b'\0\0\0', start, sec - start)); f.seek(510); f.write(b'\x55\xaa')
rd = os.path.join(t, 'readme'); open(rd, 'w').write('hello card!\n')
part = 'card.img@@' + str(start * 512)
env = dict(os.environ, MTOOLS_SKIP_CHECK='1')
for a in (['mcopy', '-i', card + '@@' + str(start * 512), rd, '::README.TXT'], ['mmd', '-i', card + '@@' + str(start * 512), '::PHOTOS']):
    r = run(a, env=env)
    if r.returncode: print('mtools:', r.stderr); sys.exit(1)
for mode in ('fixed', 'usb'):
    cp = os.path.join(t, mode + '.img'); shutil.copy(card, cp)
    r = run([exe, mode, cp]); print(mode + ':', r.stdout.strip().splitlines()[-2:] if r.stdout else r.stderr[-1500:]); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
    if r.returncode: print(r.stdout)
    pimg = os.path.join(t, mode + '.p')
    with open(cp, 'rb') as f: f.seek(start * 512); open(pimg, 'wb').write(f.read())
    rm(cp)
    r = run(['fsck.fat', '-n', pimg]); clean = r.returncode == 0; print('fsck.fat -n after ' + mode + ' writes:', 'clean' if clean else r.stdout[-800:]); ok = ok and clean
    rm(pimg)
r = run([exe, 'fat', card, out]); print(r.stdout.strip() or r.stderr[-1500:]); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
pimg = os.path.join(t, 'p.img')
with open(out, 'rb') as f: f.seek(start * 512); open(pimg, 'wb').write(f.read())
r = run(['fsck.fat', '-n', pimg]); clean = r.returncode == 0; print('fsck.fat -n after writes:', 'clean' if clean else r.stdout[-800:]); ok = ok and clean
rm(pimg, out, card)
xcard = os.path.join(t, 'xcard.img'); xout = os.path.join(t, 'xcard.out'); xpart = gzip.open(os.path.join(here, 'exfat16m.img.gz')).read()
with open(xcard, 'wb') as f:
    mbr = bytearray(512); mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\0\0\0', 0x07, b'\0\0\0', start, len(xpart) // 512); mbr[510:512] = b'\x55\xaa'
    f.write(mbr); f.seek(start * 512); f.write(xpart); f.truncate((start + 1024) * 512 + len(xpart))
r = run([exe, 'exfat', xcard, xout]); print(r.stdout.strip() or r.stderr[-1500:]); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
fx = shutil.which('fsck.exfat') or os.environ.get('FSCK_EXFAT')
if fx and os.path.exists(xout):
    ximg = os.path.join(t, 'x.p')
    with open(xout, 'rb') as f: f.seek(start * 512); open(ximg, 'wb').write(f.read(len(xpart)))
    r = run([fx, '-n', ximg]); clean = r.returncode == 0; print('fsck.exfat -n after writes:', ('clean: ' + r.stdout.strip().splitlines()[-1]) if clean else (r.stdout + r.stderr)[-1200:]); ok = ok and clean
else: print('fsck.exfat not found on the host: using exfat_verify.py only')
sys.path.insert(0, here); import exfat_verify
if os.path.exists(xout):
    e0 = exfat_verify.check(xpart); xo = open(xout, 'rb').read()[start * 512:start * 512 + len(xpart)]; e1 = exfat_verify.check(xo)
    bad = bytearray(xo); bad[106] |= 2; e2 = exfat_verify.check(bytes(bad))
    print('exfat_verify on the fresh image:', 'clean' if not e0 else e0[:5]); print('exfat_verify after writes:', 'clean' if not e1 else e1[:10]); print('exfat_verify notices a dirty volume:', bool(e2))
    ok = ok and not e0 and not e1 and bool(e2)
rm(xcard, xout, os.path.join(t, 'x.p'))
if shutil.which('mke2fs') and shutil.which('e2fsck'):
    d = os.path.join(t, 'root'); os.makedirs(os.path.join(d, 'docs')); open(os.path.join(d, 'hello.txt'), 'w').write('hello\n'); open(os.path.join(d, 'docs', 'a.txt'), 'w').write('abc')
    e = os.path.join(t, 'e4.img'); eo = os.path.join(t, 'e4.out')
    r = run(['mke2fs', '-q', '-F', '-t', 'ext4', '-d', d, e, '32M'])
    if r.returncode: print(r.stderr); sys.exit(1)
    r = run([exe, 'ext4', e, eo]); print(r.stdout.strip() or r.stderr[-1500:]); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
    r = run(['e2fsck', '-fn', eo]); clean = r.returncode == 0; print('e2fsck -fn after writes:', 'clean' if clean else r.stdout[-800:]); ok = ok and clean
    es = os.path.join(t, 'e4sd.out'); r = run([exe, 'ext4sd', e, es]); print(r.stdout.strip() or r.stderr[-1500:]); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
    r = run(['e2fsck', '-fn', es]); clean = r.returncode == 0; print('e2fsck -fn after SD writes:', 'clean' if clean else r.stdout[-800:]); ok = ok and clean
else: print('ext4 part skipped: no e2fsprogs')
print('volfs: all ok' if ok else 'volfs: FAILED'); sys.exit(0 if ok else 1)
