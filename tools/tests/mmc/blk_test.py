import os, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.abspath(os.path.join(here, '..', '..', '..'))
t = tempfile.mkdtemp(prefix='mmcblk'); img = os.path.join(t, 'sd.img'); exe = os.path.join(t, 'blk')
sec = 131072; start = 2048
with open(img, 'wb') as f: f.truncate(sec * 512)
r = subprocess.run(['mkfs.fat', '-F', '16', '-n', 'SDCARD', '--offset', str(start), img, str((sec - start) // 2)], capture_output=True, text=True)
if r.returncode: print(r.stdout, r.stderr); sys.exit(1)
with open(img, 'r+b') as f:
    f.seek(446); f.write(struct.pack('<B3sB3sII', 0, b'\0\0\0', 6, b'\0\0\0', start, sec - start)); f.seek(510); f.write(b'\x55\xaa')
r = subprocess.run(['gcc', '-O1', '-g', '-Wall', '-Wextra', '-fsanitize=address,undefined', '-I' + root, '-include', 'stdint.h', os.path.join(here, 'blk_main.c'), os.path.join(root, 'drivers/mmc/mmc_blk.c'), '-o', exe], capture_output=True, text=True)
if r.returncode: print(r.stderr[-2000:]); sys.exit(1)
ok = True
for args in ([exe], [exe, img]):
    r = subprocess.run(args, capture_output=True, text=True); print(r.stdout.strip()); ok = ok and r.returncode == 0 and 'ALL PASS' in r.stdout
sys.exit(0 if ok else 1)
