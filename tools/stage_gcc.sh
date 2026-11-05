#!/bin/bash
set -e
SR=$1
GR=$2
V=13
G=/usr/lib/gcc/x86_64-linux-gnu/$V
L=/usr/libexec/gcc/x86_64-linux-gnu/$V
[ -x $L/cc1 ] && [ -x /usr/bin/x86_64-linux-gnu-gcc-$V ] && [ -f $GR/usr/lib/x86_64-linux-gnu/crt1.o ] || { echo "  (gcc $V or glibc dev files not found - sysroot without gcc)"; exit 0; }
for x in /usr/bin/x86_64-linux-gnu-gcc-$V $L/cc1 $L/collect2; do
  t=$(readelf -h $x 2>/dev/null | awk '/Type:/{print $2}')
  [ "$t" = "DYN" ] || { echo "  (gcc $V on this build machine is not position independent ($x is $t) - NoovexOS can only run PIE programs; sysroot without gcc)"; exit 0; }
done
mkdir -p $SR/usr/bin $SR$L $SR$G $SR/usr/include $SR/lib64
cp -L /usr/bin/x86_64-linux-gnu-gcc-$V $SR/usr/bin/gcc
cp -L /usr/bin/x86_64-linux-gnu-as $SR/usr/bin/as
cp -L /usr/bin/x86_64-linux-gnu-ld.bfd $SR/usr/bin/ld
ln -sf gcc $SR/usr/bin/cc
ln -sf gcc $SR/usr/bin/x86_64-linux-gnu-gcc
ln -sf gcc $SR/usr/bin/gcc-$V
for f in cc1 collect2 lto-wrapper liblto_plugin.so; do cp -L $L/$f $SR$L/; done
for f in crtbegin.o crtbeginS.o crtbeginT.o crtend.o crtendS.o libgcc.a libgcc_eh.a libgcc_s.so libssp_nonshared.a; do cp -L $G/$f $SR$G/; done
cp -rL $G/include $SR$G/
strip $SR/usr/bin/gcc $SR/usr/bin/as $SR/usr/bin/ld $SR$L/cc1 $SR$L/collect2 $SR$L/lto-wrapper
strip --strip-unneeded $SR$L/liblto_plugin.so
for b in $SR/usr/bin/gcc $SR/usr/bin/as $SR/usr/bin/ld $SR$L/cc1 $SR$L/collect2 $SR$L/lto-wrapper; do
  ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done
done
cp -r $GR/usr/include/. $SR/usr/include/
cp -rL /usr/include/linux /usr/include/asm-generic $SR/usr/include/
cp -rL /usr/include/x86_64-linux-gnu/asm $SR/usr/include/asm
for f in crt1.o crti.o crtn.o Scrt1.o rcrt1.o libc_nonshared.a libc.so libm.so libpthread.a libdl.a librt.a libutil.a libresolv.so libanl.so libmvec.so; do
  [ -e $GR/usr/lib/x86_64-linux-gnu/$f ] && cp -L $GR/usr/lib/x86_64-linux-gnu/$f $SR/lib64/
done
mkdir -p $SR/usr/share/doc/gcc
printf 'GCC %s (C compiler) for NoovexOS, with GNU as and ld %s and glibc 2.44 headers.\nCompile: gcc hello.c -o hello   Run: ./hello\n' "$(gcc-$V -dumpfullversion)" "$(x86_64-linux-gnu-ld.bfd --version | head -1 | awk '{print $NF}')" > $SR/usr/share/doc/gcc/README
echo "  gcc $(gcc-$V -dumpfullversion) staged: $(du -sh $SR$L/cc1 | cut -f1) cc1"
