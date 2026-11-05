import sys, os, re, shutil, zipfile, glob, subprocess, hashlib, base64, csv, io, argparse, tempfile

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTS = os.path.join(TOP, "ports")
VENDOR = os.path.join(TOP, "third_party", "pylibs")
PYVER = "3.13"
SITE = "usr/lib/python3.13/site-packages"
KINDS = ("files", "python", "script")
KEYS = ("name", "version", "summary", "kind", "needs", "packages", "scripts", "license", "source")
SKIPDIRS = ("tests", "__pycache__")
PROFILE_KEYS = ("name", "id", "pretty", "url", "version", "ports", "repo")
DEBNAME = re.compile(r"^[a-z0-9][a-z0-9+.-]+$")


def canon(n):
    return re.sub(r"[-_.]+", "_", n).lower()


def vkey(v):
    return [(0, int(x), "") if x.isdigit() else (1, 0, x) for x in re.split(r"[.+-]", v)]


def read_kv(path):
    d = {}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise SystemExit("%s:%d: expected key=value" % (path, n))
        k, v = line.split("=", 1)
        d[k.strip()] = v.strip()
    return d


def load_port(name):
    p = os.path.join(PORTS, name, "PORT")
    if not os.path.isfile(p):
        raise SystemExit("no such port: %s (looked for %s)" % (name, p))
    d = read_kv(p)
    d["dir"] = os.path.dirname(p)
    return d


def all_ports():
    return sorted(n for n in os.listdir(PORTS) if os.path.isfile(os.path.join(PORTS, n, "PORT")))


def validate(name):
    d = load_port(name)
    errs = []
    for k in d:
        if k != "dir" and k not in KEYS:
            errs.append("unknown key %s" % k)
    for k in ("name", "version", "summary", "kind"):
        if not d.get(k):
            errs.append("missing %s" % k)
    if d.get("name") and d["name"] != name:
        errs.append("name=%s but the folder is %s" % (d["name"], name))
    if d.get("kind") and d["kind"] not in KINDS:
        errs.append("kind must be one of %s" % ", ".join(KINDS))
    if d.get("kind") == "files" and not os.path.isdir(os.path.join(d["dir"], "files")):
        errs.append("kind=files needs a files/ folder")
    if d.get("kind") == "script" and not os.path.isfile(os.path.join(d["dir"], "stage.sh")):
        errs.append("kind=script needs stage.sh")
    if d.get("kind") == "python" and not d.get("packages"):
        errs.append("kind=python needs packages=")
    for n in d.get("needs", "").split():
        if n != "python" and not os.path.isfile(os.path.join(PORTS, n, "PORT")):
            errs.append("needs unknown port %s" % n)
    if not DEBNAME.match(name):
        errs.append("the name must be lower case letters, digits, + - . (it becomes a Debian package name)")
    for spec in d.get("scripts", "").split():
        cmd, _, mod = spec.partition(":")
        if not cmd or not mod or "/" in cmd:
            errs.append("scripts entry %s must be command:module" % spec)
    if d.get("kind") == "python":
        for pkg in d.get("packages", "").split():
            v = vendor_dir(pkg)
            if v and not os.path.isfile(os.path.join(v, "LICENSE")):
                errs.append("%s has no LICENSE" % os.path.relpath(v, TOP))
    return errs


def vendor_dir(pkg):
    for d in sorted(glob.glob(os.path.join(VENDOR, "*"))):
        f = os.path.join(d, "PKG")
        if os.path.isfile(f):
            m = read_pkg(d)
            if m.get("Name") and m.get("Version") and canon(m["Name"]) == canon(pkg):
                return d
    return None


def order(names):
    out, seen = [], set()

    def visit(n, path):
        if n in seen or n == "python":
            return
        if n in path:
            raise SystemExit("ports depend on each other in a circle: %s" % " -> ".join(path + [n]))
        for m in load_port(n).get("needs", "").split():
            visit(m, path + [n])
        seen.add(n)
        out.append(n)

    for n in names:
        visit(n, [])
    return out


class Python:
    def __init__(self, root, build, pyroot):
        self.root, self.build, self.pyroot = root, build, pyroot
        self.site = os.path.join(root, SITE)
        self.cache = os.path.join(build, "pywheels")
        self.done = set()

    def skip(self, rel):
        parts = rel.replace("\\", "/").split("/")
        return any(p in SKIPDIRS for p in parts[:-1]) or parts[-1].endswith(".pyc")

    def record(self, di, rels):
        rows = []
        for r in rels:
            p = os.path.join(self.site, r)
            if os.path.isfile(p):
                h = hashlib.sha256(open(p, "rb").read()).digest()
                rows.append([r, "sha256=" + base64.urlsafe_b64encode(h).rstrip(b"=").decode(), str(os.path.getsize(p))])
        rows.append([os.path.relpath(os.path.join(di, "RECORD"), self.site), "", ""])
        b = io.StringIO()
        csv.writer(b, lineterminator="\n").writerows(rows)
        open(os.path.join(di, "RECORD"), "w").write(b.getvalue())

    def wheel(self, path):
        z = zipfile.ZipFile(path)
        names = [n for n in z.namelist() if not n.endswith("/") and not self.skip(n)]
        for n in names:
            t = os.path.join(self.site, n)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            with z.open(n) as s, open(t, "wb") as d:
                shutil.copyfileobj(s, d)
        di = os.path.join(self.site, [n.split("/")[0] for n in names if n.split("/")[0].endswith(".dist-info")][0])
        open(os.path.join(di, "INSTALLER"), "w").write("noovex\n")
        rels = [n for n in names if not n.endswith("/RECORD")] + [os.path.relpath(os.path.join(di, "INSTALLER"), self.site)]
        self.record(di, rels)
        return os.path.basename(di)[:-len(".dist-info")]

    def vendored(self, pkg):
        d = vendor_dir(pkg)
        if not d:
            return None
        meta = read_pkg(d)
        rels = []
        for e in sorted(os.listdir(d)):
            if e in ("PKG", "LICENSE"):
                continue
            s, t = os.path.join(d, e), os.path.join(self.site, e)
            if os.path.isdir(s):
                shutil.copytree(s, t, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.c"))
                for r, _, fs in os.walk(t):
                    rels += [os.path.relpath(os.path.join(r, f), self.site) for f in fs]
                c = os.path.join(s, "speedups.c")
                if os.path.isfile(c):
                    so = os.path.join(t, "speedups.cpython-313-x86_64-linux-gnu.so")
                    inc = os.path.join(self.pyroot, "usr/include/python3.13")
                    if os.path.isdir(inc) and subprocess.call(["gcc", "-shared", "-fPIC", "-O2", "-I" + inc, c, "-o", so]) == 0:
                        subprocess.call(["strip", "--strip-unneeded", so])
                        rels.append(os.path.relpath(so, self.site))
            else:
                shutil.copy(s, t)
                rels.append(e)
        di = os.path.join(self.site, "%s-%s.dist-info" % (canon(meta["Name"]), meta["Version"]))
        os.makedirs(di, exist_ok=True)
        req = "".join("Requires-Dist: %s\n" % r for r in meta["Requires-Dist"])
        open(os.path.join(di, "METADATA"), "w").write("Metadata-Version: 2.1\nName: %s\nVersion: %s\n%s" % (meta["Name"], meta["Version"], req))
        open(os.path.join(di, "INSTALLER"), "w").write("noovex\n")
        open(os.path.join(di, "top_level.txt"), "w").write("\n".join(sorted({r.split("/")[0][:-3] if r.endswith(".py") and "/" not in r else r.split("/")[0] for r in rels})) + "\n")
        shutil.copy(os.path.join(d, "LICENSE"), os.path.join(di, "LICENSE"))
        rels += [os.path.relpath(os.path.join(di, f), self.site) for f in ("METADATA", "INSTALLER", "top_level.txt", "LICENSE")]
        self.record(di, rels)
        return "%s %s (vendored)" % (meta["Name"], meta["Version"])

    def host(self, pkg):
        if sys.version_info[:2] != (3, 13):
            return None
        import importlib.metadata as M
        try:
            d = M.distribution(pkg)
        except M.PackageNotFoundError:
            return None
        base = str(d.locate_file(""))
        if os.path.realpath(base).startswith("/usr/lib/python3/") or any(str(f).endswith(".dist-info/direct_url.json") for f in d.files or []):
            return None
        meta = [str(f) for f in d.files or [] if str(f).endswith(".dist-info/METADATA") and not str(f).startswith("..")]
        if not meta:
            return None
        rels = []
        for f in d.files or []:
            rel = str(f)
            s = os.path.join(base, rel)
            if rel.startswith("..") or self.skip(rel) or rel.endswith("/RECORD") or not os.path.isfile(s):
                continue
            t = os.path.join(self.site, rel)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copy2(s, t)
            rels.append(rel)
        di = os.path.join(self.site, os.path.dirname(meta[0]))
        os.makedirs(di, exist_ok=True)
        self.record(di, rels)
        return "%s %s (build machine)" % (pkg, d.version)

    def cached(self, pkg):
        if not os.path.isdir(self.cache):
            return []
        w = [f for f in os.listdir(self.cache) if f.endswith(".whl") and canon(f.split("-")[0]) == canon(pkg)]
        return [os.path.join(self.cache, f) for f in sorted(w, key=lambda f: vkey(f.split("-")[1]))]

    def from_cache(self, pkg):
        w = self.cached(pkg)
        return ("%s (wheel)" % self.wheel(w[-1])) if w else None

    def download(self, pkg):
        os.makedirs(self.cache, exist_ok=True)
        with open(os.path.join(self.cache, "pip.log"), "a") as log:
            subprocess.call([sys.executable, "-m", "pip", "download", "-q", "--no-deps", "--dest", self.cache, "--only-binary=:all:",
                             "--platform", "manylinux_2_28_x86_64", "--platform", "manylinux_2_17_x86_64", "--platform", "manylinux2014_x86_64",
                             "--python-version", PYVER, "--implementation", "cp", "--abi", "cp313", "--abi", "abi3", "--abi", "none", pkg],
                            stdout=log, stderr=log)
        return self.from_cache(pkg)

    def bundled_pip(self):
        w = glob.glob(os.path.join(self.root, "usr/lib/python3.13/ensurepip/_bundled/pip-*.whl")) or glob.glob(os.path.join(self.build, "sysroot-stage/usr/lib/python3.13/ensurepip/_bundled/pip-*.whl"))
        return ("%s (bundled with Python)" % self.wheel(w[0])) if w else None

    def install(self, pkg):
        key = canon(pkg)
        if key in self.done:
            return "%s (already staged)" % pkg
        os.makedirs(self.site, exist_ok=True)
        r = (self.bundled_pip() if key == "pip" else None) or self.vendored(pkg) or self.from_cache(pkg) or self.host(pkg) or self.download(pkg)
        if r:
            self.done.add(key)
        return r

    def compile(self):
        py = os.path.join(self.pyroot, "usr/bin/python3.13")
        if not os.path.isfile(py):
            return
        env = dict(os.environ, PYTHONHOME=os.path.join(self.pyroot, "usr"))
        subprocess.call([py, "-m", "compileall", "-q", "-j0", "--invalidation-mode", "unchecked-hash", "-d", "/" + SITE, self.site], env=env, stdout=subprocess.DEVNULL)


def read_pkg(d):
    m = {"Requires-Dist": []}
    for l in open(os.path.join(d, "PKG")).read().splitlines():
        if ": " in l:
            k, v = l.split(": ", 1)
            if k == "Requires-Dist":
                m[k].append(v)
            else:
                m[k] = v
    return m


def write_scripts(root, port):
    for spec in port.get("scripts", "").split():
        cmd, mod = spec.split(":", 1)
        p = os.path.join(root, "usr/bin", cmd)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write("#!/usr/bin/python3.13\nimport sys\nfrom %s import main\nif __name__ == \"__main__\":\n    sys.exit(main())\n" % mod)
        os.chmod(p, 0o755)


def tree_files(top):
    out = []
    for r, ds, fs in os.walk(top):
        for f in fs + [d for d in ds if os.path.islink(os.path.join(r, d))]:
            out.append(os.path.relpath(os.path.join(r, f), top))
    return out


def stage_port(port, root, build, pyroot):
    res = stage_kind(port, root, build, pyroot)
    extra = os.path.join(port["dir"], "files")
    if port["kind"] != "files" and os.path.isdir(extra):
        shutil.copytree(extra, root, dirs_exist_ok=True, symlinks=True)
        res.append("%d extra files" % len(tree_files(extra)))
    return res


def stage_kind(port, root, build, pyroot):
    kind = port["kind"]
    if kind == "files":
        src = os.path.join(port["dir"], "files")
        shutil.copytree(src, root, dirs_exist_ok=True, symlinks=True)
        return ["%d files" % len(tree_files(src))]
    if kind == "script":
        env = dict(os.environ, NVX_ROOT=root, NVX_BUILD=build, NVX_TOP=TOP, NVX_PORT=port["dir"])
        if subprocess.call(["bash", os.path.join(port["dir"], "stage.sh"), root, build], env=env, cwd=TOP) != 0:
            raise RuntimeError("stage.sh failed")
        return ["stage.sh done"]
    if not os.path.isfile(os.path.join(root, "usr/bin/python3.13")) and not os.path.isfile(os.path.join(build, "sysroot-stage/usr/bin/python3.13")):
        raise RuntimeError("Python is not in the Linux files")
    py = Python(root, build, pyroot)
    lines, missing = [], []
    for pkg in port["packages"].split():
        r = py.install(pkg)
        if r:
            lines.append(r)
        else:
            missing.append(pkg)
    if missing:
        raise RuntimeError("not found anywhere: %s (put a manylinux wheel in %s, or install it into the build machine's Python 3.13)" % (", ".join(missing), py.cache))
    write_scripts(root, port)
    py.compile()
    return lines


def make_deb(port, tree, pool, inrepo):
    ctl = os.path.join(tree, "DEBIAN")
    os.makedirs(ctl, exist_ok=True)
    deps = ", ".join(("noovex-port-" + n) for n in port.get("needs", "").split() if n in inrepo)
    open(os.path.join(ctl, "control"), "w").write(
        "Package: noovex-port-%s\nVersion: %s\nArchitecture: amd64\nMaintainer: NoovexOS\n%sDescription: %s\n" %
        (port["name"], port["version"], ("Depends: %s\n" % deps) if deps else "", port["summary"]))
    os.makedirs(pool, exist_ok=True)
    out = os.path.join(pool, "noovex-port-%s_%s_amd64.deb" % (port["name"], port["version"]))
    subprocess.check_call(["dpkg-deb", "--root-owner-group", "-Zxz", "-b", tree, out], stdout=subprocess.DEVNULL)
    return out


def version_auto():
    m = re.search(r'"(\d+\.\d+)"', open(os.path.join(TOP, "core/buildsig.c")).read())
    return m.group(1) if m else "0"


def cmd_stage(a):
    prof = read_kv(a.profile)
    ver = prof.get("version", "auto")
    ver = version_auto() if ver == "auto" else ver
    root = os.path.abspath(a.root)
    build = os.path.abspath(a.build)
    pyroot = os.path.abspath(a.python_root or os.path.join(build, "python-root"))
    inst = order(prof.get("ports", "").split())
    repo = [n for n in order(prof.get("repo", "").split()) if n not in inst]
    rows, failed = [], []
    for k in prof:
        if k not in PROFILE_KEYS:
            raise SystemExit("%s: unknown key %s" % (a.profile, k))
    for n in inst + repo:
        errs = validate(n)
        if errs:
            raise SystemExit("port %s: %s" % (n, "; ".join(errs)))
    for n in inst:
        p = load_port(n)
        try:
            res = stage_port(p, root, build, pyroot)
            print("  port %s %s: %s" % (n, p["version"], "; ".join(res)))
            rows.append((n, p["version"], "installed", p["summary"]))
        except Exception as e:
            print("  port %s FAILED: %s" % (n, e))
            failed.append(n)
            if not a.keep_going:
                raise SystemExit(1)
    for n in repo:
        p = load_port(n)
        with tempfile.TemporaryDirectory() as t:
            os.chmod(t, 0o755)
            try:
                stage_port(p, t, build, pyroot)
                deb = make_deb(p, t, os.path.join(root, "usr/share/noovex/repo/pool"), set(repo))
                print("  port %s %s: packed for apt (%s, %.1f MB)" % (n, p["version"], os.path.basename(deb), os.path.getsize(deb) / 1e6))
                rows.append((n, p["version"], "apt", p["summary"]))
            except Exception as e:
                print("  port %s FAILED: %s" % (n, e))
                failed.append(n)
                if not a.keep_going:
                    raise SystemExit(1)
    did = prof.get("id", "noovexos")
    name = prof.get("name", "NoovexOS")
    q = lambda v: v.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("`", "\\`")
    if not DEBNAME.match(did):
        raise SystemExit("%s: id must be lower case letters, digits, + - ." % a.profile)
    rel = ("NAME=\"%s\"\nID=%s\nID_LIKE=debian\nVERSION_ID=\"%s\"\nVERSION=\"%s\"\nPRETTY_NAME=\"%s %s\"\nHOME_URL=\"%s\"\n" %
           (q(name), did, q(ver), q(ver), q(prof.get("pretty", name)), q(ver), q(prof.get("url", "https://noovexos.site"))))
    for p in ("etc/os-release", "usr/lib/os-release"):
        f = os.path.join(root, p)
        os.makedirs(os.path.dirname(f), exist_ok=True)
        if os.path.islink(f):
            os.unlink(f)
        open(f, "w").write(rel)
    d = os.path.join(root, "usr/share/noovex")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "ports.txt"), "w").write("".join("%s %s %s %s\n" % r for r in rows))
    shutil.copy(a.profile, os.path.join(d, "profile"))
    print("  distro %s %s: %d ports installed, %d for apt" % (name, ver, sum(1 for r in rows if r[2] == "installed"), sum(1 for r in rows if r[2] == "apt")))
    if failed:
        print("  failed ports: " + ", ".join(failed))
        return 1


def cmd_list(a):
    for n in all_ports():
        p = load_port(n)
        print("%-16s %-10s %-7s %s" % (n, p["version"], p["kind"], p["summary"]))


def cmd_info(a):
    p = load_port(a.name)
    for k in KEYS:
        if p.get(k):
            print("%-9s %s" % (k + ":", p[k]))
    e = validate(a.name)
    print("status:   " + ("ok" if not e else "; ".join(e)))


def cmd_check(a):
    bad = 0
    for n in all_ports():
        e = validate(n)
        if e:
            bad += 1
            print("%s: %s" % (n, "; ".join(e)))
    for f in sorted(glob.glob(os.path.join(TOP, "distro", "*.profile"))):
        prof = read_kv(f)
        for k in prof:
            if k not in PROFILE_KEYS:
                bad += 1
                print("%s: unknown key %s" % (os.path.basename(f), k))
        for k in ("name", "id", "ports"):
            if k not in prof:
                bad += 1
                print("%s: missing %s" % (os.path.basename(f), k))
        for n in (prof.get("ports", "") + " " + prof.get("repo", "")).split():
            if not os.path.isfile(os.path.join(PORTS, n, "PORT")):
                bad += 1
                print("%s: unknown port %s" % (os.path.basename(f), n))
        try:
            order(prof.get("ports", "").split() + prof.get("repo", "").split())
        except SystemExit as e:
            bad += 1
            print("%s: %s" % (os.path.basename(f), e))
    print("%d ports, %d profiles, %d problems" % (len(all_ports()), len(glob.glob(os.path.join(TOP, "distro", "*.profile"))), bad))
    return 1 if bad else 0


def cmd_new(a):
    d = os.path.join(PORTS, a.name)
    if os.path.exists(d):
        raise SystemExit("port %s already exists" % a.name)
    os.makedirs(d)
    body = "name=%s\nversion=1.0\nsummary=describe the port in one line\nkind=%s\n" % (a.name, a.kind)
    if a.kind == "python":
        body += "needs=python-pip\npackages=\n"
    open(os.path.join(d, "PORT"), "w").write(body)
    if a.kind == "files":
        os.makedirs(os.path.join(d, "files/usr/bin"))
    if a.kind == "script":
        open(os.path.join(d, "stage.sh"), "w").write("set -e\nROOT=$1\nBUILD=$2\n")
    print("created %s - edit PORT, then add %s to a profile in distro/" % (os.path.relpath(d, TOP), a.name))


def main():
    ap = argparse.ArgumentParser(prog="nvxport", description="NoovexOS ports and distro profiles")
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("stage", help="put the ports of a profile into the Linux files")
    s.add_argument("--profile", required=True)
    s.add_argument("--root", required=True)
    s.add_argument("--build", default="build")
    s.add_argument("--python-root")
    s.add_argument("--keep-going", action="store_true", help="stage the other ports when one fails (exit code is still 1)")
    s.set_defaults(f=cmd_stage)
    sp.add_parser("list", help="list all ports").set_defaults(f=cmd_list)
    s = sp.add_parser("info", help="show one port")
    s.add_argument("name")
    s.set_defaults(f=cmd_info)
    sp.add_parser("check", help="check every port and profile").set_defaults(f=cmd_check)
    s = sp.add_parser("new", help="create a new port")
    s.add_argument("name")
    s.add_argument("--kind", choices=KINDS, default="files")
    s.set_defaults(f=cmd_new)
    a = ap.parse_args()
    sys.exit(a.f(a) or 0)


if __name__ == "__main__":
    main()
