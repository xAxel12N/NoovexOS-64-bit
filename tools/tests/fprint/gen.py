import math, random, sys, os
W, H = 160, 200
def finger(seed):
    r = random.Random(seed)
    return dict(f=2*math.pi/r.uniform(7.5, 9.5), cx=r.uniform(60, 100), cy=r.uniform(70, 130), k=r.uniform(0.004, 0.012),
                sing=[(r.uniform(25, 135), r.uniform(25, 175), r.choice([-1, 1])) for _ in range(r.randint(30, 40))])
def render(fg, rot, tx, ty, noise, seed, path):
    r = random.Random(seed); c, s = math.cos(rot), math.sin(rot); out = bytearray()
    for y in range(H):
        for x in range(W):
            u = x - W/2 - tx; v = y - H/2 - ty
            X = c*u + s*v + W/2; Y = -s*u + c*v + H/2
            ex = (X-80)/70; ey = (Y-100)/92
            if ex*ex+ey*ey > 1: out.append(200); continue
            dx, dy = X-fg['cx'], Y-fg['cy']
            ph = fg['f']*(math.hypot(dx, dy*1.3)) + fg['k']*dx*dx
            for (sx, sy, sg) in fg['sing']: ph += sg*math.atan2(Y-sy, X-sx)
            g = 128 + 90*math.cos(ph) + r.gauss(0, noise)
            out.append(max(0, min(255, int(g))))
    open(path, 'wb').write(b'P5 %d %d 255\n' % (W, H) + bytes(out))
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
for f in range(6):
    fg = finger(1000+f)
    for i in range(3):
        rr = random.Random(f*10+i)
        render(fg, rr.uniform(-0.25, 0.25) if i else 0, rr.uniform(-8, 8) if i else 0, rr.uniform(-8, 8) if i else 0, 18, f*100+i, f'{d}/f{f}_{i}.pgm')
