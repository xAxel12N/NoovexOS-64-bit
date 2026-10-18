import sys, zlib, struct
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
z = c.compress(data) + c.flush()
open(dst, 'wb').write(b'NVZ1' + struct.pack('<I', len(data)) + z)
