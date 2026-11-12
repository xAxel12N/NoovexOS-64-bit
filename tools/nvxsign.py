import argparse, hashlib, os, struct, sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

MAGIC = bytes([0x4E, 0x56, 0x58, 0x36, 0x34, 0x53, 0x49, 0x47])
O_HASH, O_FULL, O_SBVER, O_SVN, O_KEYID, O_SRHASH, O_SRLEN, O_IMM, O_SIG, O_PUB = 32, 88, 128, 132, 136, 144, 176, 180, 184, 248
BLOCK = 320
SB_VERSION = 1
KEY_DIR = 'build/keys'
KEY_NAME = 'kernel-sign.pem'


def find_block(b):
    off = bytes(b).find(MAGIC)
    if off < 0 or off + BLOCK > len(b):
        raise SystemExit('no signature block in the kernel file')
    return off


def load_key(path):
    k = serialization.load_pem_private_key(open(path, 'rb').read(), password=None)
    if not isinstance(k, ec.EllipticCurvePrivateKey) or k.curve.name != 'secp256r1':
        raise SystemExit('%s: not a P-256 private key' % path)
    return k


def pub_raw(k):
    return k.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def key_id(pub):
    return hashlib.sha256(pub).digest()[:8]


def key_path(arg=None):
    return arg or os.environ.get('NVX_SIGN_KEY') or os.path.join(KEY_DIR, KEY_NAME)


def ensure_key(path):
    if os.path.exists(path):
        return load_key(path), False
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    k = ec.generate_private_key(ec.SECP256R1())
    pem = k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as f:
        f.write(pem)
    return k, True


def full_digest(b, off):
    t = bytearray(b)
    for o, n in ((O_HASH, 32), (O_FULL, 32), (O_SIG, 64)):
        t[off + o:off + o + n] = bytes(n)
    return hashlib.sha256(bytes(t)).digest()


def sign_image(b, k, svn=None, sysroot=None, imm=None):
    off = find_block(b)
    pub = pub_raw(k)
    struct.pack_into('<I', b, off + O_SBVER, SB_VERSION)
    if svn is not None:
        struct.pack_into('<I', b, off + O_SVN, svn)
    b[off + O_KEYID:off + O_KEYID + 8] = key_id(pub)
    b[off + O_PUB:off + O_PUB + 65] = pub
    if sysroot is not None:
        b[off + O_SRHASH:off + O_SRHASH + 32] = hashlib.sha256(sysroot).digest() if sysroot else bytes(32)
        struct.pack_into('<I', b, off + O_SRLEN, len(sysroot))
    if imm is not None:
        struct.pack_into('<I', b, off + O_IMM, imm)
    imm_sz = struct.unpack_from('<I', b, off + O_IMM)[0]
    if not imm_sz or imm_sz > len(b) or off + BLOCK > imm_sz:
        raise SystemExit('immutable size %d does not cover the signature block' % imm_sz)
    for o, n in ((O_HASH, 32), (O_FULL, 32), (O_SIG, 64)):
        b[off + o:off + o + n] = bytes(n)
    full = hashlib.sha256(bytes(b)).digest()
    b[off + O_FULL:off + O_FULL + 32] = full
    der = k.sign(full, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    b[off + O_SIG:off + O_SIG + 64] = r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
    b[off + O_HASH:off + O_HASH + 32] = hashlib.sha256(bytes(b[:imm_sz])).digest()
    return off, full


def verify_image(b, pub=None, sysroot=None):
    off = find_block(b)
    if struct.unpack_from('<I', b, off + O_SBVER)[0] != SB_VERSION:
        return 'not signed'
    kp = bytes(b[off + O_PUB:off + O_PUB + 65])
    if pub is not None and kp != pub:
        return 'signed with another key (%s)' % key_id(kp).hex()
    if bytes(b[off + O_KEYID:off + O_KEYID + 8]) != key_id(kp):
        return 'key id does not match the public key'
    full = full_digest(b, off)
    if full != bytes(b[off + O_FULL:off + O_FULL + 32]):
        return 'file changed after signing (whole-file hash differs)'
    sig = bytes(b[off + O_SIG:off + O_SIG + 64])
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), kp).verify(
            utils.encode_dss_signature(int.from_bytes(sig[:32], 'big'), int.from_bytes(sig[32:], 'big')),
            full, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    except Exception:
        return 'signature is wrong'
    imm_sz = struct.unpack_from('<I', b, off + O_IMM)[0]
    if not imm_sz or imm_sz > len(b) or off + BLOCK > imm_sz:
        return 'signature block damaged'
    t = bytearray(b[:imm_sz])
    t[off + O_HASH:off + O_HASH + 32] = bytes(32)
    if hashlib.sha256(bytes(t)).digest() != bytes(b[off + O_HASH:off + O_HASH + 32]):
        return 'code hash differs'
    if sysroot is not None:
        n = struct.unpack_from('<I', b, off + O_SRLEN)[0]
        h = bytes(b[off + O_SRHASH:off + O_SRHASH + 32])
        if n != len(sysroot) or (n and hashlib.sha256(sysroot).digest() != h):
            return 'Linux files do not match the signed hash'
    return None


FILE_MAGIC = b'NVXFILE1'


def sign_file(data, k):
    pub = pub_raw(k)
    der = k.sign(hashlib.sha256(data).digest(), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    return data + r.to_bytes(32, 'big') + s.to_bytes(32, 'big') + key_id(pub) + struct.pack('<II', len(data), 1) + FILE_MAGIC


def verify_file(blob, pub):
    if len(blob) < 152 or blob[-8:] != FILE_MAGIC:
        return 'not signed'
    t = blob[-88:]
    n, v = struct.unpack_from('<II', t, 72)
    if n != len(blob) - 88 or v != 1:
        return 'signature block damaged'
    if t[64:72] != key_id(pub):
        return 'signed with another key'
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), pub).verify(
            utils.encode_dss_signature(int.from_bytes(t[:32], 'big'), int.from_bytes(t[32:64], 'big')),
            hashlib.sha256(blob[:n]).digest(), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    except Exception:
        return 'file changed after signing'
    return None


def info(b):
    off = find_block(b)
    g = lambda o: struct.unpack_from('<I', b, off + o)[0]
    print('signature block at %d' % off)
    print('  signed     %s' % ('yes' if g(O_SBVER) == SB_VERSION else 'no'))
    print('  svn        %d' % g(O_SVN))
    print('  key id     %s' % bytes(b[off + O_KEYID:off + O_KEYID + 8]).hex())
    print('  code part  %d bytes' % g(O_IMM))
    print('  Linux files %d bytes, sha256 %s' % (g(O_SRLEN), bytes(b[off + O_SRHASH:off + O_SRHASH + 32]).hex()))
    print('  file hash  %s' % bytes(b[off + O_FULL:off + O_FULL + 32]).hex())


def c_header(pub):
    rows = ', '.join('0x%02x' % x for x in pub)
    return 'static const u8 g_nvx_pub[65]={%s};\nstatic const u8 g_nvx_keyid[8]={%s};\n' % (
        rows, ', '.join('0x%02x' % x for x in key_id(pub)))


def main():
    ap = argparse.ArgumentParser(prog='nvxsign.py')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('newkey'); p.add_argument('--key')
    p = sub.add_parser('pub'); p.add_argument('--key'); p.add_argument('--header')
    p = sub.add_parser('sign'); p.add_argument('kernel'); p.add_argument('--key'); p.add_argument('--svn', type=int); p.add_argument('--sysroot')
    p = sub.add_parser('verify'); p.add_argument('kernel'); p.add_argument('--key'); p.add_argument('--pub'); p.add_argument('--sysroot')
    p = sub.add_parser('info'); p.add_argument('kernel')
    p = sub.add_parser('signfile'); p.add_argument('infile'); p.add_argument('outfile'); p.add_argument('--key')
    p = sub.add_parser('verifyfile'); p.add_argument('file'); p.add_argument('--key')
    a = ap.parse_args()
    if a.cmd == 'newkey':
        path = key_path(a.key)
        k, made = ensure_key(path)
        print('%s %s, key id %s' % ('made' if made else 'kept', path, key_id(pub_raw(k)).hex()))
        return 0
    if a.cmd == 'pub':
        k, made = ensure_key(key_path(a.key))
        pub = pub_raw(k)
        if made:
            print('  new signing key %s (key id %s) - keep a copy, releases signed with another key cannot update this one' % (key_path(a.key), key_id(pub).hex()), file=sys.stderr)
        if a.header:
            open(a.header, 'w').write(c_header(pub))
        else:
            print(pub.hex())
        return 0
    if a.cmd == 'signfile':
        k = load_key(key_path(a.key))
        open(a.outfile, 'wb').write(sign_file(open(a.infile, 'rb').read(), k))
        print('signed %s -> %s with key %s' % (a.infile, a.outfile, key_id(pub_raw(k)).hex()))
        return 0
    if a.cmd == 'verifyfile':
        err = verify_file(open(a.file, 'rb').read(), pub_raw(load_key(key_path(a.key))))
        print(err or 'OK: file signature valid')
        return 1 if err else 0
    if a.cmd == 'info':
        info(bytearray(open(a.kernel, 'rb').read()))
        return 0
    b = bytearray(open(a.kernel, 'rb').read())
    sr = open(a.sysroot, 'rb').read() if a.sysroot else None
    if a.cmd == 'sign':
        k = load_key(key_path(a.key))
        off, full = sign_image(b, k, a.svn, sr)
        open(a.kernel, 'wb').write(b)
        print('signed %s with key %s, svn %d' % (a.kernel, key_id(pub_raw(k)).hex(), struct.unpack_from('<I', b, off + O_SVN)[0]))
        return 0
    pub = bytes.fromhex(a.pub) if a.pub else (pub_raw(load_key(key_path(a.key))) if (a.key or os.path.exists(key_path())) else None)
    err = verify_image(b, pub, sr)
    print(err or 'OK: signature valid (key %s)' % key_id(bytes(b[find_block(b) + O_PUB:find_block(b) + O_PUB + 65])).hex())
    return 1 if err else 0


if __name__ == '__main__':
    sys.exit(main())
