import os, sys, subprocess, shlex, concurrent.futures as cf

def obj_of(outdir, unit):
    return os.path.join(outdir, os.path.splitext(unit)[0].replace('/', '_') + '.o')

def deps_newer(o, d):
    if not os.path.exists(o) or not os.path.exists(d): return True
    t = os.path.getmtime(o)
    txt = open(d).read().replace('\\\n', ' ')
    files = txt.split(':', 1)[1].split() if ':' in txt else []
    for f in files:
        try:
            if os.path.getmtime(f) > t: return True
        except OSError:
            return True
    return False

def main():
    outdir = sys.argv[1]; cflags = shlex.split(os.environ['CFLAGS']); cxxflags = shlex.split(os.environ.get('CXXFLAGS', ''))
    force = '--all' in sys.argv
    jobs = int(os.environ.get('JOBS') or os.cpu_count() or 4)
    units = [l.strip() for l in open('units.list') if l.strip()]
    os.makedirs(outdir, exist_ok=True)
    flags_file = os.path.join(outdir, '.cflags'); fl = ' '.join(cflags) + ' | ' + ' '.join(cxxflags)
    if not os.path.exists(flags_file) or open(flags_file).read() != fl: force = True
    todo = []
    for u in units:
        o = obj_of(outdir, u); d = o[:-2] + '.d'
        if force or deps_newer(o, d): todo.append((u, o, d))
    def run(job):
        u, o, d = job
        cc = ['g++'] + cxxflags if u.endswith('.cpp') else ['gcc'] + cflags
        r = subprocess.run(cc + ['-MMD', '-MF', d, '-c', u, '-o', o], capture_output=True, text=True)
        return u, r.returncode, r.stderr
    failed = []
    with cf.ThreadPoolExecutor(jobs) as ex:
        for u, rc, err in ex.map(run, todo):
            if err: sys.stderr.write(err)
            if rc: failed.append(u)
    open(flags_file, 'w').write(fl)
    print(f'  {len(todo)} of {len(units)} units compiled' + (' (all: flags changed or --all)' if force and todo else ''))
    if failed:
        for u in failed: print(f'UNIT BUILD FAILED: {u}')
        sys.exit(1)
    open(os.path.join(outdir, 'objects.txt'), 'w').write(' '.join(obj_of(outdir, u) for u in units))

main()
