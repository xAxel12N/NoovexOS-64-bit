#!/usr/bin/env python3
import sys

def field(v, lo, hi, x):
    w = hi - lo
    assert 0 <= x < (1 << w), (lo, hi, x)
    return v | (x << lo)

class Asm:
    def __init__(self):
        self.code = []
        self.wt = 0
    def emit(self, v, has_dst, has_src, branch=False, delay=1, yld=0):
        v = field(v, 12, 15, 7)
        wr = 7; rd = 7; wt = self.wt
        if branch:
            wt = 0x3f
        else:
            if has_dst:
                wr = 0; self.wt |= 1
            if has_src:
                rd = 1; self.wt |= 2
        v = field(v, 105, 109, delay)
        v = field(v, 109, 110, yld)
        v = field(v, 110, 113, wr)
        v = field(v, 113, 116, rd)
        v = field(v, 116, 122, wt)
        for i in range(4):
            self.code.append((v >> (32 * i)) & 0xffffffff)
    def ald(self, dst, addr, comps):
        v = 0x321
        v = field(v, 16, 24, dst)
        v = field(v, 32, 40, 255)
        v = field(v, 24, 32, 255)
        v = field(v, 40, 50, addr)
        v = field(v, 74, 76, comps - 1)
        self.emit(v, True, True)
    def ast(self, addr, src, comps):
        v = 0x322
        v = field(v, 32, 40, src)
        v = field(v, 64, 72, 255)
        v = field(v, 24, 32, 255)
        v = field(v, 40, 50, addr)
        v = field(v, 74, 76, comps - 1)
        self.emit(v, False, True)
    def ipa(self, dst, addr):
        assert addr % 4 == 0
        v = 0x326
        v = field(v, 16, 24, dst)
        v = field(v, 64, 72, addr >> 2)
        v = field(v, 76, 78, 0)
        v = field(v, 78, 80, 0)
        v = field(v, 32, 40, 255)
        v = field(v, 81, 84, 7)
        self.emit(v, True, True)
    def exit(self):
        v = 0x94d
        v = field(v, 87, 90, 7)
        self.emit(v, False, False, branch=True)

def sph_set(h, lo, hi, x):
    assert 0 <= x < (1 << (hi - lo))
    for b in range(lo, hi):
        if (x >> (b - lo)) & 1:
            h[b // 32] |= 1 << (b % 32)

def sph_vs():
    h = [0] * 32
    sph_set(h, 0, 5, 1)
    sph_set(h, 5, 10, 4)
    sph_set(h, 10, 14, 1)
    sph_set(h, 17, 21, 1)
    sph_set(h, 192, 200, 0xff)
    sph_set(h, 400, 432, 0xf0000000)
    sph_set(h, 432, 436, 0xf)
    sph_set(h, 140, 148, 0x70 // 4)
    sph_set(h, 152, 160, (0x90 - 1) // 4)
    return h

def sph_fs():
    h = [0] * 32
    sph_set(h, 0, 5, 2)
    sph_set(h, 5, 10, 4)
    sph_set(h, 10, 14, 5)
    sph_set(h, 14, 15, 1)
    sph_set(h, 17, 21, 1)
    sph_set(h, 191, 192, 1)
    sph_set(h, 192, 200, 0xaa)
    sph_set(h, 576, 608, 0xf)
    return h

def build():
    vs = Asm()
    vs.ald(0, 0x80, 4)
    vs.ald(4, 0x90, 4)
    vs.ast(0x70, 0, 4)
    vs.ast(0x80, 4, 4)
    vs.exit()
    fs = Asm()
    for c in range(4):
        fs.ipa(c, 0x80 + 4 * c)
    fs.exit()
    return (sph_vs() + vs.code, 10), (sph_fs() + fs.code, 6)

def arr(name, w):
    out = 'static const u32 %s[%d]={' % (name, len(w))
    out += ','.join('0x%08Xu' % x for x in w)
    return out + '};\n'

def main():
    (vs, vg), (fs, fg) = build()
    out = '#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NV3D_SHADERS_H\n#define NVX_DRIVERS_VIDEO_NVIDIA_NV3D_SHADERS_H\n'
    out += '#define NV3D_SPH_WORDS 32\n#define NV3D_VS_GPRS %d\n#define NV3D_FS_GPRS %d\n' % (vg, fg)
    out += arr('nv3d_vs', vs) + arr('nv3d_fs', fs)
    out += '#endif\n'
    path = sys.argv[1] if len(sys.argv) > 1 else 'include/nvx/drivers/video/nvidia/nv3d_shaders.h'
    open(path, 'w').write(out)
    print('wrote', path, len(vs), len(fs))

if __name__ == '__main__':
    main()
