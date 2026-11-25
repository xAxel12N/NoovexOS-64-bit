import gzip, os, shutil, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.abspath(os.path.join(here, '..', '..', '..'))
sys.path.insert(0, here); import exfat_verify
t = tempfile.mkdtemp(prefix='xfz'); exe = os.path.join(t, 'xfz'); img = os.path.join(t, 'x.img')
open(img, 'wb').write(gzip.open(os.path.join(here, 'exfat16m.img.gz')).read())
r = subprocess.run(['gcc', '-O1', '-g', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-I' + root, '-include', 'stdint.h', os.path.join(here, 'exfat_fuzz.c'), os.path.join(root, 'fs/fat/exfat.c'), '-o', exe], capture_output=True, text=True)
if r.returncode: print(r.stderr[-3000:]); sys.exit(1)
def run(*a): return subprocess.run([exe] + list(a), capture_output=True, text=True, timeout=600)
ok = True
r = run('regress', img); print('regress:', r.stdout.strip().splitlines()[-1] if r.stdout else r.stderr[-1500:])
if r.returncode or 'ALL PASS' not in r.stdout: print(r.stdout[-2000:], r.stderr[-1500:]); ok = False
n = int(os.environ.get('XF_FUZZ', '3000'))
for seed in (1, 2):
    r = run('fuzz', img, str(seed), str(n)); print('fuzz seed %d:' % seed, (r.stdout.strip().splitlines() or [''])[0], 'sanitizers clean' if not r.stderr.strip() else r.stderr[-1500:])
    ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout and not r.stderr.strip()
fx = shutil.which('fsck.exfat') or os.environ.get('FSCK_EXFAT')
for seed in range(1, 5):
    out = os.path.join(t, 's%d.img' % seed); r = run('stress', img, str(seed), '1500', out)
    good = r.returncode == 0 and 'ALL PASS' in r.stdout and not r.stderr.strip()
    e = exfat_verify.check(open(out, 'rb').read()) if os.path.exists(out) else ['no image']
    line = [l for l in r.stdout.splitlines() if l.startswith('stress:')]
    print('stress seed %d:' % seed, line[0] if line else r.stdout[-800:] + r.stderr[-800:], '| exfat_verify:', 'clean' if not e else e[:5])
    if fx and os.path.exists(out):
        f = subprocess.run([fx, '-n', out], capture_output=True, text=True); print('  fsck.exfat -n:', 'clean' if f.returncode == 0 else (f.stdout + f.stderr)[-600:]); good = good and f.returncode == 0
    ok = ok and good and not e
known = 0; worse = 0; dup = 0; loss = 0; fixed = 0
for seed in range(1, int(os.environ.get('XF_CRASH', '120'))):
    out = os.path.join(t, 'c.img'); r = run('crash', img, str(seed), '300', out)
    if 'DATALOSS' in r.stdout: loss += 1
    if r.returncode or r.stderr.strip(): print('crash seed %d:' % seed, r.stdout[-600:], r.stderr[-1000:]); worse += 1; continue
    if 'repair ok (0 fixes)' not in r.stdout: fixed += 1
    e = exfat_verify.check(open(out, 'rb').read())
    if any(' also used by ' in x for x in e): dup += 1
    if e: print('crash seed %d:' % seed, r.stdout.strip().splitlines()[0], e[:5]); worse += 1
print('power-loss runs: %d repaired at mount, %d duplicate entries left, %d with data loss, %d with other damage after repair' % (fixed, dup, loss, worse))
ok = ok and not worse
print('exfat fuzz: all ok' if ok else 'exfat fuzz: FAILED'); sys.exit(0 if ok else 1)
