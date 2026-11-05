import sys
src, dst = sys.argv[1], sys.argv[2]
b = open(src, 'rb').read()
out = ['#ifndef NVX_GQK_CUBIN_H', '#define NVX_GQK_CUBIN_H', 'static const unsigned char g_gqk_cubin[] __attribute__((aligned(16))) = {']
for i in range(0, len(b), 24):
    out.append('    ' + ','.join('0x%02x' % c for c in b[i:i + 24]) + ',')
out += ['};', 'static const unsigned int g_gqk_cubin_len = %d;' % len(b), '#endif', '']
open(dst, 'w').write('\n'.join(out))
