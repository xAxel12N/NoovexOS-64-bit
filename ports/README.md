# Ports and distro profiles

A port is one piece of software that goes into the Linux files (the read-only
layer with glibc, Python, X11 apps and so on). A distro profile is a list of
ports. `build.sh` stages the ports of one profile; to make your own NoovexOS
variant you write a profile, not a new build script.

## Commands

```
python3 tools/nvxport.py list                 every port with version and kind
python3 tools/nvxport.py info python-web      one port and whether it is valid
python3 tools/nvxport.py check                check all ports and profiles
python3 tools/nvxport.py new my-tool --kind files
NVX_PROFILE=distro/noovexos-lite.profile sh build.sh
python3 tools/nvxport.py stage --profile P --root DIR --keep-going
```

`stage` stops at the first port that fails; with `--keep-going` it stages
the others and still exits with 1.

`tools/check.py` runs `nvxport.py check` in its static group.

## A port

Each port is a folder `ports/NAME/` with a file `PORT` (key=value, `#` starts
a comment):

| Key | Needed | Meaning |
|---|---|---|
| `name` | yes | same as the folder name |
| `version` | yes | shown in `/usr/share/noovex/ports.txt` and used for the .deb; raise it when the contents change, or apt will not update the package |
| `summary` | yes | one line |
| `kind` | yes | `files`, `python` or `script` (below) |
| `needs` | no | other ports that must be staged first; `python` means the base CPython |
| `packages` | `python` only | Python distributions, in install order |
| `scripts` | no | commands to create, `cmd:module` (the module must have `main()`) |
| `license`, `source` | no | where it comes from and under which licence |

Kinds:

- `files`: the folder `files/` is copied over the Linux files as it is
  (`files/usr/bin/x` becomes `/usr/bin/x`). Example: `desktop-scripts`.
- `python`: each name in `packages` is taken from the first place that has it:
  1. `pip` comes from the wheel CPython ships (`ensurepip`);
  2. `third_party/pylibs/NAME-VERSION/` (vendored source with a `PKG` file:
     `Name:`, `Version:`, `Requires-Dist:` lines, plus `LICENSE`);
  3. a manylinux wheel in `build/pywheels/` (newest version wins);
  4. the build machine's Python 3.13 installation (`importlib.metadata`),
     but not Debian's `/usr/lib/python3/` and not editable installs;
  5. `pip download` into `build/pywheels/` (messages in `pip.log`).
  A package found nowhere stops the build. Names are compared the PEP 503
  way (`charset-normalizer` = `charset_normalizer`).
  Folders named `tests` are left out, a correct `RECORD` is written (so `pip
  uninstall` and `pip list` work inside NoovexOS) and everything is compiled
  to `.pyc`. A `speedups.c` next to a vendored package is compiled against
  the NoovexOS CPython headers.
- any kind: a `files/` folder next to `PORT` is copied as well (the
  `python-pip` port uses it for `/etc/pip.conf`).
- `script`: `stage.sh ROOT BUILD` runs from the repository root with
  `NVX_ROOT`, `NVX_BUILD`, `NVX_TOP` and `NVX_PORT` set. Use it when files
  must be built or copied from the build machine.

## A profile

`distro/NAME.profile`:

| Key | Meaning |
|---|---|
| `name`, `id`, `pretty`, `url` | written to `/etc/os-release` and `/usr/lib/os-release` |
| `version` | `auto` takes the version from `core/buildsig.c` |
| `ports` | installed in the Linux files |
| `repo` | packed as `noovex-port-NAME` .deb files into the built-in apt repository instead; users add them with `sudo apt install noovex-port-NAME` |

`needs` are added automatically and in the right order. The profile itself is
copied to `/usr/share/noovex/profile` and the staged ports are listed in
`/usr/share/noovex/ports.txt`.

Two profiles ship: `noovexos.profile` (the release) and
`noovexos-lite.profile` (pip only; the web and data libraries are in the apt
repository).

## Adding a Python library

1. Pure Python on GitHub: put the package folder, `LICENSE` and a `PKG` file
   in `third_party/pylibs/NAME-VERSION/`.
2. Add the name to `packages=` of a port (or make a new `python` port).
3. `python3 tools/nvxport.py check`, then build.

Wheels with C code (numpy, pandas) must be manylinux x86_64 for CPython 3.13;
NoovexOS has glibc 2.44, so any `manylinux_2_17` or `manylinux_2_28` wheel
works. Libraries the wheel needs that are not inside it (for example
`libstdc++.so.6`) must already be in `/lib64`.
