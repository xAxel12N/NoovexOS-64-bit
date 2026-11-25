#!/usr/bin/env python3
import os, re, sys

TOOLS = {
    "zstd": ("1", "compress or decompress .zst files", "zstd [-d|-c|-k|-#] [file ...]", "Zstandard compressor. Run zstd -h for every option."),
    "unzstd": ("1", "decompress .zst files", "unzstd [file ...]", "Same as zstd -d."),
    "sqlite3": ("1", "command line interface for SQLite databases", "sqlite3 [options] [database] [sql]", "Opens or creates an SQLite database and runs SQL typed or given on the command line. Type .help inside sqlite3 for the dot commands."),
    "lua": ("1", "Lua interpreter", "lua [options] [script [args]]", "Runs Lua scripts or an interactive Lua prompt. lua -v shows the version."),
    "bzip2": ("1", "block-sorting file compressor", "bzip2 [-cdfkqstvz123456789] [file ...]", "Compresses files to .bz2 or, with -d, decompresses them."),
    "gzip": ("1", "compress or expand files (zlib minigzip)", "gzip [-c] [-d] [-f] [-h] [-r] [-1 to -9] [file ...]", "Small gzip built from the zlib example program; it compresses to .gz or, with -d, decompresses."),
    "git": ("1", "the stupid content tracker", "git [--version] [--help] [-C path] command [args]", "Distributed version control. git help -a lists the commands; git command -h shows the options of one. The default pager is less and the default editor vim (/etc/gitconfig)."),
    "tcc": ("1", "Tiny C compiler", "tcc [options] [infile ...] [-run infile args ...]", "Compiles and links C programs for NoovexOS; tcc -run file.c compiles and runs in one step."),
    "kilo": ("1", "small text editor", "kilo file", "Ctrl-S saves, Ctrl-Q quits, Ctrl-F finds."),
    "qjs": ("1", "QuickJS JavaScript interpreter", "qjs [-e code] [file.js]", "Runs JavaScript files or an interactive prompt."),
    "fetch": ("1", "download a URL over HTTP or HTTPS", "fetch URL [file]", "Downloads one URL using the built-in TLS (BearSSL)."),
    "play": ("1", "play a media file or URL", "play file|URL [-b]", "Fullscreen player: space pauses, the arrow keys seek, q quits; -b runs a benchmark."),
}

def esc(s):
    s = s.replace("\\", "\\e")
    return "\\&" + s if s[:1] in ".'" else s

def page(name, sec, desc, syn, body, os_="NoovexOS", extra=None):
    L = [".Dd November 17, 2026", ".Dt %s %s" % (name.upper(), sec), ".Os %s" % os_, ".Sh NAME", ".Nm %s" % name, ".Nd %s" % esc(desc), ".Sh SYNOPSIS", ".Bd -literal", esc(syn), ".Ed", ".Sh DESCRIPTION"]
    for p in body:
        L += [".Pp", esc(p)] if p else []
    if extra:
        L += extra
    L += [".Sh SEE ALSO", ".Xr noovex-shell 7"]
    return "\n".join(L) + "\n"

def help_lines(src):
    t = open(src, encoding="utf-8", errors="replace").read()
    i = t.find('if(seq(cmd,"help")){')
    if i < 0:
        return []
    j = t.find('else if(seq(cmd,', i)
    out = []
    for m in re.finditer(r'term_puts\("((?:[^"\\]|\\.)*)"\)', t[i:j]):
        s = m.group(1).replace("\\n", "").replace('\\"', '"').strip()
        if s:
            out.append(s)
    return out

def commands(shdir):
    cmds = {}
    for root, _, files in os.walk(shdir):
        for f in sorted(files):
            if not f.endswith(".c"):
                continue
            t = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
            for m in re.finditer(r'seq\(cmd,"([a-z0-9][a-z0-9_.-]*)"\)', t):
                n = m.group(1)
                win = t[m.end():m.end() + 3000]
                syn = None
                for s in re.finditer(r'"((?:[^"\\]|\\.)*)"', win):
                    v = s.group(1).replace("\\n", " ").replace('\\"', '"').strip()
                    v2 = v[7:] if v.startswith("usage: ") else v
                    if v2.startswith(n + " ") and re.search(r"[\[<|]", v2) and len(v2) < 200:
                        syn = v2
                        break
                if n not in cmds or (syn and not cmds[n]):
                    cmds[n] = syn
    return cmds

def main():
    root, out = sys.argv[1], sys.argv[2]
    have = set()
    for d in range(1, 9):
        p = os.path.join(out, "man%d" % d)
        if os.path.isdir(p):
            for f in os.listdir(p):
                have.add(f.split(".")[0])
    os.makedirs(os.path.join(out, "man1"), exist_ok=True)
    os.makedirs(os.path.join(out, "man7"), exist_ok=True)
    n = 0
    for name, (sec, desc, syn, body) in TOOLS.items():
        if name in have:
            continue
        open(os.path.join(out, "man" + sec, name + "." + sec), "w").write(page(name, sec, desc, syn, [body], extra=[".Pp", "This short page was written for NoovexOS because the upstream source in the image has no manual page."]))
        have.add(name)
        n += 1
    hl = help_lines(os.path.join(root, "shell/commands/system_disks_packages_gpu.c"))
    cmds = commands(os.path.join(root, "shell"))
    for name in sorted(cmds):
        if name in have or name == "help":
            continue
        rel = [h for h in hl if re.search(r"(^|[\s:/(])" + re.escape(name) + r"($|[\s\[(,|])", h)]
        desc = "NoovexOS shell command"
        for h in rel:
            m = re.search(r"\(([^()]{4,120})\)", h)
            if m and h.split()[0] == name:
                desc = m.group(1)
                break
        syn = cmds[name] or name
        body = ["%s is built into the NoovexOS shell (the terminal and the text consoles); it does not exist as a file in /usr/bin." % name]
        if rel:
            body.append("From the shell help:")
        extra = []
        if rel:
            extra = [".Bd -literal -offset indent"] + [esc(h) for h in rel] + [".Ed"]
        extra += [".Pp", "Type help in the shell for the full list."]
        open(os.path.join(out, "man1", name + ".1"), "w").write(page(name, "1", desc, syn, body, extra=extra))
        n += 1
    ov = [".Bd -literal -offset indent"] + [esc(h) for h in hl] + [".Ed", ".Sh COMMANDS", ".Bl -tag -width Ds -compact"]
    for name in sorted(cmds):
        ov += [".It Xr %s 1" % name]
    ov += [".El"]
    L = page("noovex-shell", "7", "the built-in commands of the NoovexOS shell", "help", ["The NoovexOS shell has built-in commands that run inside the kernel terminal. Programs in /usr/bin and /bin (Linux programs such as vim, less, git, curl) run through the Linux compatibility layer. The shell help text:"], extra=ov)
    L = L.replace(".Sh SEE ALSO\n.Xr noovex-shell 7\n", ".Sh SEE ALSO\n.Xr man 1 ,\n.Xr apropos 1\n")
    open(os.path.join(out, "man7", "noovex-shell.7"), "w").write(L)
    print("mkman: %d pages generated, %d shell commands" % (n + 1, len(cmds)))

if __name__ == "__main__":
    main()
