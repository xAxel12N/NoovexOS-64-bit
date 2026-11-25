# third_party

Code from other projects, each with its own licence: musl, BearSSL, xz, zstd, zlib, Lua, QuickJS, wasm3, SQLite, DOOM, Quake, ntfs-3g, FFmpeg, mandoc (ISC, the man formatter) and others. `src/` holds source archives for glibc, CPython and Tcl/Tk that `build.sh` unpacks.
Code from other projects, each with its own licence: musl, BearSSL, xz, zstd, zlib, Lua, QuickJS, wasm3, SQLite, DOOM, Quake, ntfs-3g, FFmpeg, rsync and others. `src/` holds source archives for glibc, CPython and Tcl/Tk that `build.sh` unpacks.

`rsync/` is rsync 3.4.1 (tag v3.4.1, commit 3305a7a0 of github.com/RsyncProject/rsync), GPL-3.0-or-later, see `rsync/COPYING`; its bundled zlib (zlib licence) and popt (MIT) are used. Built statically for musl by `tools/build_ports2.sh`.
