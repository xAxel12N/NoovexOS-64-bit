import os, sys, hashlib, subprocess
r = sys.argv[1]
out = []
for f in sorted(os.listdir(os.path.join(r, 'pool'))):
    p = os.path.join(r, 'pool', f)
    ctl = subprocess.run(['dpkg-deb', '-f', p], capture_output=True, text=True).stdout.rstrip('\n')
    d = open(p, 'rb').read()
    out.append(ctl + '\nFilename: pool/' + f + '\nSize: %d\nSHA256: %s\n' % (len(d), hashlib.sha256(d).hexdigest()))
open(os.path.join(r, 'Packages'), 'w').write('\n'.join(out))
print('  local repository: %d packages' % len(out))
