import re, struct, sys
src, out = sys.argv[1], sys.argv[2]
chip = sys.argv[3] if len(sys.argv) > 3 else '8852b'
t = open(src).read()
names = [(1, 'rtw89_%s_phy_bb_regs'), (2, 'rtw89_%s_phy_bb_reg_gain'), (3, 'rtw89_%s_phy_radioa_regs'), (4, 'rtw89_%s_phy_radiob_regs'), (5, 'rtw89_%s_phy_nctl_regs')]
names = [(k, n % chip) for k, n in names]
blob = b'R89P' + struct.pack('<I', len(names))
for kind, nm in names:
    m = re.search(r'%s\[\]\s*=\s*\{(.*?)\n\};' % nm, t, re.S)
    pairs = re.findall(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}', m.group(1))
    blob += struct.pack('<BxxxI', kind, len(pairs)) + b''.join(struct.pack('<II', int(a, 16), int(d, 16)) for a, d in pairs)
    print(nm, len(pairs))
open(out, 'wb').write(blob)
