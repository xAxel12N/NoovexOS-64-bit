#!/bin/bash
set -e
PR=$1; SR=$2
L=$PR/usr/lib/python3.13; D=$SR/usr/lib/python3.13
mkdir -p $SR/usr/bin $D/lib-dynload
cp $PR/usr/bin/python3.13 $SR/usr/bin/ && strip $SR/usr/bin/python3.13
ln -sf python3.13 $SR/usr/bin/python3; ln -sf python3.13 $SR/usr/bin/python
( cd $L && find . -name '*.py' -not -path './test/*' -not -path '*/tests/*' -not -path './idlelib/idle_test/*' -not -path './turtledemo/*' -not -path './lib2to3/*' -not -path './config-3.13*' -not -path './site-packages/*' -not -path '*/__pycache__/*' -not -name 'test_*' ) | while read f; do mkdir -p $D/$(dirname $f); cp $L/$f $D/$f; done
for f in $L/lib-dynload/*.so; do n=$(basename $f); case $n in _test*|_xxtest*|xx*|_ctypes_test*) continue;; esac; cp $f $D/lib-dynload/ && strip --strip-unneeded $D/lib-dynload/$n; done
mkdir -p $D/site-packages
PYTHONHOME=$PR/usr $PR/usr/bin/python3.13 -m compileall -q -j0 --invalidation-mode unchecked-hash -d /usr/lib/python3.13 $D >/dev/null || true
for b in $SR/usr/bin/python3.13 $D/lib-dynload/*.so; do ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done; done
printf '/usr/lib/python3/dist-packages\n/usr/local/lib/python3.13/dist-packages\n' > $D/site-packages/debian-dist-packages.pth
mkdir -p $D/ensurepip/_bundled && cp $L/ensurepip/_bundled/*.whl $D/ensurepip/_bundled/ 2>/dev/null || true
for f in $L/idlelib/*.def $L/idlelib/*.txt $L/idlelib/CREDITS.txt $L/idlelib/ChangeLog; do [ -f "$f" ] && cp "$f" $D/idlelib/; done
mkdir -p $D/idlelib/Icons && cp $L/idlelib/Icons/* $D/idlelib/Icons/ 2>/dev/null || true
printf '#!/usr/bin/python3\nfrom idlelib.pyshell import main\nif __name__ == "__main__":\n    main()\n' > $SR/usr/bin/idle3.13; chmod 755 $SR/usr/bin/idle3.13
ln -sf idle3.13 $SR/usr/bin/idle3; ln -sf idle3.13 $SR/usr/bin/idle
