#!/bin/sh
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=$R/third_party
W=${1:-$R/build/ports2}; mkdir -p "$W"; W=$(cd "$W" && pwd)
export NVX_MUSL=${NVX_MUSL:-$R/build/musl} NVX_MUSL_SH=${NVX_MUSL_SH:-$R/build/musl-sh} NVX_PSYS=$W/sys
MCC=$R/tools/mcc-port
J=$(nproc 2>/dev/null || echo 4)
HB="--host=x86_64-linux-musl --build=x86_64-pc-linux-gnu"
S=$W/sys; mkdir -p "$S/usr/lib" "$S/usr/include"
export PKG_CONFIG=false
step(){ [ -f "$W/.$1" ]; }
done_(){ touch "$W/.$1"; }

if ! step khdr; then
  cp -a /usr/include/linux /usr/include/asm-generic "$S/usr/include/"
  rm -rf "$S/usr/include/asm"; cp -a /usr/include/x86_64-linux-gnu/asm "$S/usr/include/asm"
  done_ khdr
fi

if ! step zlib; then
  rm -rf "$W/zlib"; mkdir -p "$W/zlib"
  for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
    $MCC -O2 -fPIC -DHAVE_UNISTD_H -DHAVE_STDARG_H -D_LARGEFILE64_SOURCE=1 -c "$T/zlib/$f.c" -o "$W/zlib/$f.o"
  done
  rm -f "$S/usr/lib/libz.a"; ar rcs "$S/usr/lib/libz.a" "$W"/zlib/*.o
  cp "$T/zlib/zlib.h" "$T/zlib/zconf.h" "$S/usr/include/"
  mkdir -p "$S/usr/lib/pkgconfig"; printf 'prefix=/usr\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: zlib\nDescription: zlib compression library\nVersion: 1.3.2\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' > "$S/usr/lib/pkgconfig/zlib.pc"
  done_ zlib
fi

if ! step ncurses; then
  rm -rf "$W/ncurses"; mkdir -p "$W/ncurses"; cd "$W/ncurses"
  "$T/ncurses/configure" $HB CC=$MCC CFLAGS=-O2 --prefix=/usr --without-shared --without-cxx --without-cxx-binding --without-ada --without-tests --without-manpages --without-progs --disable-db-install --with-fallbacks=xterm-256color,xterm,vt100,linux,ansi,dumb,screen,tmux-256color --with-tic-path=/usr/bin/tic --with-infocmp-path=/usr/bin/infocmp --enable-widec --disable-stripping --with-build-cc=gcc --with-termlib > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ ncurses
fi

if ! step mbedtls; then
  rm -rf "$W/mbedtls"; cp -a "$T/mbedtls" "$W/mbedtls"; cd "$W/mbedtls"; mkdir -p tests
  make -j$J lib CC=$MCC AR=ar CFLAGS="-O2 -fPIC" > make.log 2>&1
  cp library/libmbedcrypto.a library/libmbedtls.a library/libmbedx509.a "$S/usr/lib/"
  cp -a include/mbedtls include/psa "$S/usr/include/"
  done_ mbedtls
fi

if ! step curl; then
  rm -rf "$W/curl"; mkdir -p "$W/curl"; cd "$W/curl"
  CC=$MCC CFLAGS=-O2 "$T/curl/configure" $HB --prefix=/usr --with-mbedtls="$S/usr" --with-zlib="$S/usr" --without-libpsl --without-brotli --without-zstd --without-libidn2 --without-nghttp2 --without-libssh2 --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt --disable-manual --disable-docs --enable-shared --disable-static --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt --with-ca-path=no --enable-ipv6 --enable-threaded-resolver > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ curl
fi

if ! step vim; then
  rm -rf "$W/vim"; cp -a "$T/vim" "$W/vim"; cd "$W/vim/src"
  export vim_cv_toupper_broken=no vim_cv_terminfo=yes vim_cv_tgetent=zero vim_cv_tty_group=world vim_cv_tty_mode=0620 vim_cv_getcwd_broken=no vim_cv_stat_ignores_slash=no vim_cv_memmove_handles_overlap=yes vim_cv_timer_create=no vim_cv_timer_create_works=no
  CC=$MCC CFLAGS=-O2 ./configure $HB --prefix=/usr --with-features=huge --disable-gui --without-x --disable-nls --disable-netbeans --disable-channel --disable-canberra --disable-libsodium --disable-gpm --disable-sysmouse --disable-selinux --disable-xsmp --disable-smack --with-tlib=tinfow > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$W/root-vim" > inst.log 2>&1
  done_ vim
fi

if ! step less; then
  rm -rf "$W/less"; mkdir -p "$W/less"; cd "$W/less"
  CC=$MCC CFLAGS=-O2 "$T/less/configure" $HB --prefix=/usr --with-regex=posix > conf.log 2>&1
  cp "$T/less/lessmsg.inc" .
  make -j$J > make.log 2>&1
  done_ less
fi

if ! step git; then
  rm -rf "$W/git" "$W/root-git"; cp -a "$T/git" "$W/git"; cd "$W/git"
  G="prefix=/usr gitexecdir=/usr/libexec/git-core CC=$MCC AR=ar uname_S=Linux uname_M=x86_64 CFLAGS=-O2 CURLDIR=$S/usr CURL_LDFLAGS=-lcurl ZLIB_PATH=$S/usr NO_OPENSSL=1 NO_EXPAT=1 NO_GETTEXT=1 NO_TCLTK=1 NO_PERL=1 NO_PYTHON=1 NO_REGEX=YesPlease NO_ICONV=1 INSTALL_SYMLINKS=1 SKIP_DASHED_BUILT_INS=1 NO_INSTALL_HARDLINKS=1 HOST_CPU=x86_64"
  make -j$J $G all > make.log 2>&1; make $G install DESTDIR="$W/root-git" > inst.log 2>&1
  done_ git
fi

if ! step dropbear; then
  rm -rf "$W/dropbear"; cp -a "$T/dropbear" "$W/dropbear"; cd "$W/dropbear"
  CC=$MCC CFLAGS=-O2 ./configure $HB --prefix=/usr --with-zlib="$S/usr" --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx --disable-pututline --disable-pututxline --disable-pam --disable-syslog > conf.log 2>&1
  make -j$J PROGRAMS="dropbear dbclient dropbearkey dropbearconvert scp" MULTI=1 SCPPROGRESS=1 > make.log 2>&1
  done_ dropbear
fi

if ! step tcc; then
  sh "$R/tools/build_tcc.sh" "$T/tinycc" "$W/tcc" "$R/tools/mcc-dyn"
  done_ tcc
fi

O=$W/root; rm -rf "$O"; mkdir -p "$O/usr/bin" "$O/usr/lib" "$O/usr/libexec" "$O/usr/share" "$O/etc/ssl/certs" "$O/usr/include"
cp -a "$W/tcc/root/." "$O/"
cp "$W/root-vim/usr/bin/vim" "$W/root-vim/usr/bin/xxd" "$O/usr/bin/"
for l in vi view vimdiff ex rview; do ln -sf vim "$O/usr/bin/$l"; done
mkdir -p "$O/usr/share/vim"; cp -a "$W/root-vim/usr/share/vim/vim92" "$O/usr/share/vim/"
V=$O/usr/share/vim/vim92; rm -rf "$V/tutor" "$V/lang" "$V/spell" "$V/print" "$V/tools" "$V/macros" "$V/pack/dist/opt/netrw/doc"
find "$V/doc" -type f ! -name 'help.txt' ! -name 'tags' ! -name 'intro.txt' ! -name 'quickref.txt' ! -name 'index.txt' ! -name 'usr_toc.txt' ! -name 'usr_0*.txt' ! -name 'motion.txt' ! -name 'change.txt' ! -name 'insert.txt' ! -name 'cmdline.txt' ! -name 'options.txt' ! -name 'editing.txt' ! -name 'pattern.txt' ! -name 'various.txt' -delete
cp "$W/less/less" "$W/less/lessecho" "$O/usr/bin/"
cp "$S/usr/bin/curl" "$O/usr/bin/"; cp -a "$S"/usr/lib/libcurl.so* "$O/usr/lib/"
cp -a "$W/root-git/usr/bin/git" "$O/usr/bin/"; for l in git-receive-pack git-upload-archive git-upload-pack; do ln -sf git "$O/usr/bin/$l"; done
cp -a "$W/root-git/usr/libexec/git-core" "$O/usr/libexec/"
G=$O/usr/libexec/git-core; for f in git-archimport git-cvsexportcommit git-cvsimport git-cvsserver git-svn git-send-email git-p4 git-instaweb git-daemon git-http-backend git-imap-send git-http-fetch scalar git-shell; do rm -f "$G/$f"; done
mkdir -p "$O/usr/share/git-core"; cp -a "$W/root-git/usr/share/git-core/templates" "$O/usr/share/git-core/"
cp "$W/dropbear/dropbearmulti" "$O/usr/bin/"; for l in ssh dbclient scp dropbear dropbearkey dropbearconvert; do ln -sf dropbearmulti "$O/usr/bin/$l"; done
cp "$R/lib/crypto/roots/mozilla-cacert.pem" "$O/etc/ssl/certs/ca-certificates.crt"
printf '/lib\n/usr/local/lib\n/usr/lib\n' > "$O/etc/ld-musl-x86_64.path"
printf '/bin/sh\n/bin/bash\n/opt/gnu/bin/bash\n' > "$O/etc/shells"
mkdir -p "$O/etc/dropbear"; printf 'Dropbear host keys are created here the first time the server starts with: dropbear -R\n' > "$O/etc/dropbear/README"
printf '[core]\n\tpager = less -FRX\n\teditor = vim\n[init]\n\tdefaultBranch = main\n[http]\n\tsslCAInfo = /etc/ssl/certs/ca-certificates.crt\n' > "$O/etc/gitconfig"
cp -a "$S/usr/include/." "$O/usr/include/"
for f in libz.a libncursesw.a libtinfow.a libpanelw.a libmenuw.a libformw.a libmbedtls.a libmbedx509.a libmbedcrypto.a; do cp "$S/usr/lib/$f" "$O/usr/lib/"; done
ln -sf libncursesw.a "$O/usr/lib/libncurses.a"; ln -sf libtinfow.a "$O/usr/lib/libtinfo.a"
for f in "$O"/usr/bin/* "$O"/usr/lib/*.so* "$G"/*; do [ -L "$f" ] || [ ! -f "$f" ] || { head -c4 "$f" | grep -q ELF && strip --strip-unneeded "$f" 2>/dev/null || true; }; done
du -sh "$O"
