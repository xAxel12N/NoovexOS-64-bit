import os, subprocess, sys, stat, random
exe = sys.argv[1]; d = '/tmp/fidt/store'; os.system(f'rm -rf {d}'); os.makedirs(d, mode=0o700)
env = dict(os.environ, NVX_FACEID_STORE=d)
def run(u): r = subprocess.run([exe, 'store-check', u], env=env, capture_output=True, text=True); return r.returncode, r.stdout + r.stderr
random.seed(1); rows = [' '.join('%.6f' % random.uniform(-0.2, 0.2) for _ in range(128)) for _ in range(5)]
p = os.path.join(d, 'alice.face'); open(p, 'w').write('NVXFACE1 5\n' + '\n'.join(rows) + '\n'); os.chmod(p, 0o600)
rc, o = run('alice'); print('legacy', rc, o.strip()); assert rc == 0 and '5 template' in o and 'encrypted' in o
b = open(p, 'rb').read(); assert b[:8] == b'NVXFACE2' and b'0.' not in b[20:200], 'not encrypted'
k = os.path.join(d, '.key'); assert stat.S_IMODE(os.stat(k).st_mode) == 0o600 and os.path.getsize(k) == 32
assert stat.S_IMODE(os.stat(p).st_mode) == 0o600
rc, o = run('alice'); print('encrypted', rc, o.strip()); assert rc == 0 and '5 template' in o
bb = bytearray(b); bb[40] ^= 1; open(p, 'wb').write(bb); rc, o = run('alice'); print('tampered', rc, o.strip()); assert rc != 0
open(p, 'wb').write(b); os.rename(p, os.path.join(d, 'bob.face')); rc, o = run('bob'); print('renamed to bob', rc, o.strip()); assert rc != 0
os.rename(os.path.join(d, 'bob.face'), p); os.chmod(p, 0o644); rc, o = run('alice'); print('world-readable', rc, o.strip()); assert rc != 0
os.chmod(p, 0o600); kb = open(k, 'rb').read(); open(k, 'wb').write(os.urandom(32)); rc, o = run('alice'); print('other machine key', rc, o.strip()); assert rc != 0
open(k, 'wb').write(kb); os.chmod(k, 0o644); rc, o = run('alice'); print('key readable by others', rc, o.strip()); assert rc != 0
os.chmod(k, 0o600); rc, o = run('alice'); assert rc == 0
print('store: all passed')
