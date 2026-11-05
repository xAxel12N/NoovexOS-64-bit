import re, subprocess, sys, tempfile, os
a = sys.argv[1]
with tempfile.TemporaryDirectory() as d:
    o = os.path.join(d, 'r.o')
    subprocess.run(['ld', '-r', '-o', o, '-u', 'nvx_taffy_grid', '-u', 'nvx_rust_version', a], check=True)
    dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', o], capture_output=True, text=True, check=True).stdout
    und = subprocess.run(['nm', '-u', o], capture_output=True, text=True, check=True).stdout.split()
fn = '?'
bad = {}
for line in dis.splitlines():
    m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
    if m:
        fn = m.group(1)
        continue
    if re.search(r'-0x[0-9a-f]+\(%rsp\)', line):
        bad.setdefault(fn, line.strip())
allowed = {'U', 'nvx_rs_alloc', 'nvx_rs_free', 'nvx_rs_abort', 'nvx_grid_measure', 'rust_eh_personality', '_Unwind_Resume', 'memcpy', 'memset', 'memmove', 'memcmp', 'bcmp', 'floorf', 'ceilf', 'roundf', 'truncf', 'fmaxf', 'fminf', 'sqrtf', 'fabsf'}
extra = [u for u in und if u not in allowed]
for f, l in sorted(bad.items()):
    print('red zone used in', f, ':', l)
if extra:
    print('unexpected undefined symbols:', ' '.join(extra))
if bad or extra:
    sys.exit(1)
print('rust objects: no red zone use, undefined symbols ok')
