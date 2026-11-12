import hashlib, os, struct, subprocess, sys, random
HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(TOP, 'tools'))
import nvxsign
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

work = sys.argv[1] if len(sys.argv) > 1 else '/tmp/nvx-secboot-test'
os.makedirs(work, exist_ok=True)
exe = os.path.join(work, 'sv_t')
cc = os.environ.get('CC', 'gcc')
flags = os.environ.get('SANFLAGS', '').split()
subprocess.check_call([cc, '-O1', '-g', '-Wall', '-Werror'] + flags + ['-o', exe, os.path.join(HERE, 'sv_t.c')])
ok = 0; bad = 0
def check(name, cond):
    global ok, bad
    if cond: ok += 1
    else: bad += 1; print('FAIL', name)
def run(*a):
    p = subprocess.run([exe] + list(a), capture_output=True, text=True)
    return p.returncode, p.stdout.strip()
rnd = random.Random(7)
for n in (0, 1, 55, 56, 63, 64, 65, 119, 120, 1000, 70000):
    d = bytes(rnd.getrandbits(8) for _ in range(n)); f = os.path.join(work, 'd.bin'); open(f, 'wb').write(d)
    check('sha %d' % n, run('sha', f)[1] == hashlib.sha256(d).hexdigest())
k = ec.generate_private_key(ec.SECP256R1()); pub = nvxsign.pub_raw(k)
for i in range(12):
    h = hashlib.sha256(bytes([i]) * 40).digest()
    r, s = utils.decode_dss_signature(k.sign(h, ec.ECDSA(utils.Prehashed(hashes.SHA256()))))
    sig = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
    check('ecdsa good %d' % i, run('ecdsa', pub.hex(), h.hex(), sig.hex())[1] == '0')
    hb = bytearray(h); hb[i] ^= 1
    check('ecdsa hash %d' % i, run('ecdsa', pub.hex(), bytes(hb).hex(), sig.hex())[1] == '-1')
    sb = bytearray(sig); sb[i * 5] ^= 0x10
    check('ecdsa sig %d' % i, run('ecdsa', pub.hex(), h.hex(), bytes(sb).hex())[1] == '-1')
n_order = int('ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551', 16)
check('ecdsa s=0', run('ecdsa', pub.hex(), h.hex(), (sig[:32] + bytes(32)).hex())[1] == '-1')
check('ecdsa r=n', run('ecdsa', pub.hex(), h.hex(), (n_order.to_bytes(32, 'big') + sig[32:]).hex())[1] == '-1')
offpt = bytearray(pub); offpt[64] ^= 1
check('ecdsa off-curve key', run('ecdsa', bytes(offpt).hex(), h.hex(), sig.hex())[1] == '-1')
mg = bytes([0x4E, 0x56, 0x58, 0x36, 0x34, 0x53, 0x49, 0x47])
img = bytearray(rnd.getrandbits(8) for _ in range(300000))
img = img.replace(mg, b'XXXXXXXX')
off = 200000; img[off:off + 8] = mg; img[off + 128:off + 320] = bytes(192)
sr = bytes(rnd.getrandbits(8) for _ in range(5000))
nvxsign.sign_image(img, k, 3, sr, 250000)
kf = os.path.join(work, 'k.bin')
def kv(b, p=pub):
    open(kf, 'wb').write(b); return run('kernel', kf, p.hex())[1].split()[0]
check('kernel good', kv(img) == '0')
check('python verify good', nvxsign.verify_image(img, pub, sr) is None)
for pos, want in ((10, '-6'), (off + 8, '-6'), (off + 132, '-6'), (off + 150, '-6'), (290000, '-6'), (off + 190, '-7'), (off + 40, '0'), (off + 249, '-4')):
    t = bytearray(img); t[pos] ^= 0x40
    check('kernel flip %d -> %s' % (pos, want), kv(t) == want)
k2 = ec.generate_private_key(ec.SECP256R1())
t = bytearray(img); nvxsign.sign_image(t, k2)
check('kernel other key', kv(t) == '-4')
check('kernel other key, key not pinned', run('kernel', kf)[1].split()[0] == '0')
t = bytearray(img); struct.pack_into('<I', t, off + 128, 0)
check('kernel unsigned', kv(t) == '-3')
check('python verify sysroot changed', nvxsign.verify_image(img, pub, sr[:-1] + b'x') is not None)
print('secboot: %d passed, %d failed' % (ok, bad))
sys.exit(1 if bad else 0)
