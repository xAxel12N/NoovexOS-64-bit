#!/bin/sh
R="$1"; B="$2"; V=1.6.9
C="$B/vesktop"; mkdir -p "$C"
D="$C/vesktop_${V}_amd64.deb"
get() { [ -s "$2" ] && return 0; curl -fsSL --retry 2 -o "$2.part" "$1" && mv "$2.part" "$2"; }
get "https://github.com/Vencord/Vesktop/releases/download/v$V/vesktop_${V}_amd64.deb" "$D" || { echo "  vesktop: download failed - skipped"; exit 0; }
for f in vencordDesktopMain.js vencordDesktopPreload.js vencordDesktopRenderer.js vencordDesktopRenderer.css; do
  get "https://github.com/Vendicated/Vencord/releases/latest/download/$f" "$C/$f" || { echo "  vesktop: Vencord file $f could not be downloaded - skipped"; exit 0; }
done
P="$B/vesktop/root"; rm -rf "${P:?}"; mkdir -p "$P"; rm -rf "$B/packs-root/vesktop"
dpkg-deb -x "$D" "$P" || exit 1
rm -f "$P/opt/Vesktop/chrome-sandbox" "$P/opt/Vesktop/resources/apparmor-profile"
rm -rf "$P/usr/share/doc" "$P/usr/bin"
mkdir -p "$R/usr/share/applications" "$R/usr/bin"
cp -a "$P/usr/share/." "$R/usr/share/"; rm -rf "$P/usr"; mkdir -p "$R/opt"; cp -a "$P/opt/." "$R/opt/"
DT="$R/usr/share/applications/vesktop.desktop"
sed -i 's|^Exec=.*|Exec=/usr/bin/vesktop %U|; s|^Name=.*|Name=Discord (Vesktop)|' "$DT"
grep -q '^Categories=' "$DT" || echo 'Categories=Network;InstantMessaging;' >> "$DT"
S="$R/usr/share/noovex/seed/vesktop/sessionData/vencordFiles"; mkdir -p "$S"
for f in vencordDesktopMain.js vencordDesktopPreload.js vencordDesktopRenderer.js vencordDesktopRenderer.css; do cp "$C/$f" "$S/"; done
cat > "$R/usr/bin/vesktop" <<'SH'
#!/bin/bash
S=/usr/share/noovex/seed/vesktop/sessionData/vencordFiles
T="$HOME/.config/vesktop/sessionData/vencordFiles"
if [ ! -s "$T/vencordDesktopMain.js" ]; then mkdir -p "$T" && cp "$S"/* "$T"/; fi
exec /opt/Vesktop/vesktop "$@"
SH
chmod 755 "$R/usr/bin/vesktop"
mkdir -p "$R/usr/share/noovex"; echo "vesktop $V-1" >> "$R/usr/share/noovex/provided-extra.txt"
echo "  vesktop $V: $(du -sh "$R/opt/Vesktop" | cut -f1) in the Linux files, Vencord files seeded"
