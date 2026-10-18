import sys
src = open(sys.argv[1], encoding='utf-8').read()
name = sys.argv[3]
out = ['const char %s[] =' % name]
for line in src.split('\n'):
    esc = line.replace('\\', '\\\\').replace('"', '\\"')
    out.append('"%s\\n"' % esc)
out[-1] += ';'
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
