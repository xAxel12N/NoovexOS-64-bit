import sys
src, name, dst = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(src, 'rb').read()
out = ['unsigned char %s[] = {' % name]
for i in range(0, len(d), 16):
    out.append('  ' + ', '.join('0x%02x' % b for b in d[i:i+16]) + ',')
out.append('};')
out.append('unsigned int %s_len = %d;' % (name, len(d)))
open(dst, 'w').write('\n'.join(out) + '\n')
