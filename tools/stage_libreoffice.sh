#!/bin/bash
OUT=$1; SR=$2
LO=/usr/lib/libreoffice
[ -x $LO/program/soffice.bin ] || { echo "  (LibreOffice is not on this build machine - no built-in LibreOffice)"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/opt/libreoffice/nvxlib"
cp -rL $LO/. "$OUT/opt/libreoffice/" 2>/dev/null
rm -rf "$OUT/opt/libreoffice/program/__pycache__"
find "$OUT/opt/libreoffice" -xtype l -delete
P="$OUT/opt/libreoffice/program"
sed -i -e 's|^BRAND_BASE_DIR=.*|BRAND_BASE_DIR=${ORIGIN}/..|' -e 's|file:///etc/libreoffice/registry|${BRAND_BASE_DIR}/share/registry|g' -e 's| dconf:\*||' "$P/fundamentalrc"
[ -f "$P/sofficerc" ] && sed -i '/FHS_CONFIG_FILE/d' "$P/sofficerc"
! grep -rqs "file:///usr/lib/libreoffice\|file:///etc/libreoffice" "$P"/*rc || { echo "  (LibreOffice still points at /usr/lib/libreoffice or /etc/libreoffice)"; exit 1; }
for f in $(find "$OUT/opt/libreoffice/program" -type f \( -name '*.so*' -o -name 'soffice.bin' -o -name 'oosplash' -o -name 'javaldx' \)); do ldd "$f" 2>/dev/null | awk '/=> \//{print $3}'; done | sort -u | while read l; do
  n=$(basename "$l")
  [ -e "$SR/lib64/$n" ] && continue
  [ -e "$OUT/opt/libreoffice/program/$n" ] && continue
  cp -L "$l" "$OUT/opt/libreoffice/nvxlib/$n"
done
find "$OUT/opt/libreoffice" -type f \( -name '*.so*' -o -name '*.bin' \) -exec strip --strip-unneeded {} \; 2>/dev/null
cp -L /usr/share/doc/libreoffice-common/copyright "$OUT/opt/libreoffice/COPYRIGHT-debian" 2>/dev/null
echo "  built-in LibreOffice: $(du -sh "$OUT" | cut -f1), $(ls "$OUT/opt/libreoffice/nvxlib" | wc -l) extra libraries"
