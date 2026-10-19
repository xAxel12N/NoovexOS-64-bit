#include "r3d.h"

inline void* operator new(unsigned long, void* p) noexcept { return p; }

namespace {

constexpr float kPi = 3.14159265358979f;

inline float fsqrt(float x) { return __builtin_sqrtf(x > 0.0f ? x : 0.0f); }
inline float fabs_(float x) { return x < 0.0f ? -x : x; }
inline float fminf_(float a, float b) { return a < b ? a : b; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline int ifloor(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }

float fsin(float x) {
    float k = x * (1.0f / (2.0f * kPi));
    x -= (float)ifloor(k + 0.5f) * (2.0f * kPi);
    if (x > 0.5f * kPi) x = kPi - x;
    else if (x < -0.5f * kPi) x = -kPi - x;
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}
inline float fcos(float x) { return fsin(x + 0.5f * kPi); }

struct V3 {
    float x, y, z;
    constexpr V3() : x(0), y(0), z(0) {}
    constexpr V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
    V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
    V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
    V3 operator*(const V3& o) const { return V3(x * o.x, y * o.y, z * o.z); }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline V3 norm(const V3& v) { float l = dot(v, v); return l > 1e-20f ? v * (1.0f / fsqrt(l)) : V3(0, 1, 0); }
inline V3 lerp(const V3& a, const V3& b, float t) { return a + (b - a) * t; }

struct V4 { float x, y, z, w; };

struct M4 {
    float m[16];
    static M4 identity() { M4 r; for (int i = 0; i < 16; ++i) r.m[i] = (i % 5 == 0) ? 1.0f : 0.0f; return r; }
    static M4 translate(const V3& t) { M4 r = identity(); r.m[3] = t.x; r.m[7] = t.y; r.m[11] = t.z; return r; }
    static M4 scale(float s) { M4 r = identity(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
    static M4 rot_y(float a) { M4 r = identity(); float c = fcos(a), s = fsin(a); r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; return r; }
    static M4 rot_x(float a) { M4 r = identity(); float c = fcos(a), s = fsin(a); r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; return r; }
    static M4 rot_z(float a) { M4 r = identity(); float c = fcos(a), s = fsin(a); r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; return r; }
    static M4 look_at(const V3& eye, const V3& at, const V3& up) {
        V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
        M4 r = identity();
        r.m[0] = s.x; r.m[1] = s.y; r.m[2] = s.z; r.m[3] = -dot(s, eye);
        r.m[4] = u.x; r.m[5] = u.y; r.m[6] = u.z; r.m[7] = -dot(u, eye);
        r.m[8] = -f.x; r.m[9] = -f.y; r.m[10] = -f.z; r.m[11] = dot(f, eye);
        return r;
    }
    static M4 perspective(float fovy, float aspect, float zn, float zf) {
        M4 r; for (int i = 0; i < 16; ++i) r.m[i] = 0;
        float f = fcos(fovy * 0.5f) / fsin(fovy * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = 2 * zf * zn / (zn - zf); r.m[14] = -1;
        return r;
    }
    M4 operator*(const M4& o) const {
        M4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[i * 4 + k] * o.m[k * 4 + j];
                r.m[i * 4 + j] = s;
            }
        return r;
    }
    V3 point(const V3& p) const { return V3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]); }
    V3 vec(const V3& p) const { return V3(m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z); }
    V4 clip(const V3& p) const {
        return V4{ m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                   m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11], m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15] };
    }
};

class Arena {
public:
    void init(void* p, unsigned long n) { base_ = (char*)p; size_ = n; used_ = 0; }
    template<typename T> T* alloc(unsigned long count) {
        unsigned long a = (used_ + 15) & ~15UL, b = a + count * sizeof(T);
        if (!base_ || b > size_) return nullptr;
        used_ = b;
        return (T*)(base_ + a);
    }
    unsigned long used() const { return used_; }
private:
    char* base_ = nullptr;
    unsigned long size_ = 0, used_ = 0;
};

struct Vert { V3 p, n; };

struct Mesh {
    Vert* v = nullptr;
    int nv = 0;
    unsigned* idx = nullptr;
    int nt = 0;
    bool ok() const { return v && idx; }
};

struct Material {
    V3 albedo;
    float spec;
    int shine;
    float rim;
    bool checker;
};

class MeshBuilder {
public:
    explicit MeshBuilder(Arena& a) : arena_(a) {}

    Mesh grid(int cols, int rows) {
        Mesh m; m.nv = (cols + 1) * (rows + 1); m.nt = cols * rows * 2;
        m.v = arena_.alloc<Vert>(m.nv); m.idx = arena_.alloc<unsigned>(m.nt * 3);
        return m;
    }
    void quads(Mesh& m, int cols, int rows, bool wrap_c) {
        int k = 0, stride = cols + 1;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                unsigned a = r * stride + c, b = r * stride + (wrap_c && c + 1 == cols ? 0 : c + 1);
                unsigned d = a + stride, e = b + stride;
                m.idx[k++] = a; m.idx[k++] = d; m.idx[k++] = b;
                m.idx[k++] = b; m.idx[k++] = d; m.idx[k++] = e;
            }
    }

    Mesh sphere(int seg, int rings) {
        Mesh m = grid(seg, rings);
        if (!m.ok()) return m;
        for (int r = 0; r <= rings; ++r)
            for (int s = 0; s <= seg; ++s) {
                float th = kPi * r / rings, ph = 2 * kPi * s / seg;
                V3 n(fsin(th) * fcos(ph), fcos(th), fsin(th) * fsin(ph));
                m.v[r * (seg + 1) + s] = Vert{ n, n };
            }
        quads(m, seg, rings, false);
        flip(m);
        return m;
    }

    Mesh torus_knot(int seg, int sides, int p, int q, float tube) {
        Mesh m = grid(sides, seg);
        if (!m.ok()) return m;
        auto curve = [&](float t) {
            float r = 2.0f + fcos(q * t);
            return V3(r * fcos(p * t), fsin(q * t), r * fsin(p * t)) * 0.45f;
        };
        for (int i = 0; i <= seg; ++i) {
            float t = 2 * kPi * i / seg;
            V3 c = curve(t), c2 = curve(t + 0.01f);
            V3 tng = norm(c2 - c), bn = norm(cross(tng, c2 + c)), nm = cross(bn, tng);
            for (int j = 0; j <= sides; ++j) {
                float a = 2 * kPi * j / sides;
                V3 n = norm(nm * fcos(a) + bn * fsin(a));
                m.v[i * (sides + 1) + j] = Vert{ c + n * tube, n };
            }
        }
        quads(m, sides, seg, false);
        return m;
    }

    Mesh cube() {
        Mesh m; m.nv = 24; m.nt = 12;
        m.v = arena_.alloc<Vert>(24); m.idx = arena_.alloc<unsigned>(36);
        if (!m.ok()) return m;
        static const float N[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        for (int f = 0; f < 6; ++f) {
            V3 n(N[f][0], N[f][1], N[f][2]);
            V3 u = fabs_(n.y) > 0.5f ? V3(1, 0, 0) : V3(0, 1, 0);
            V3 w = cross(n, u);
            V3 c[4] = { n - u - w, n + u - w, n + u + w, n - u + w };
            for (int k = 0; k < 4; ++k) m.v[f * 4 + k] = Vert{ c[k], n };
            unsigned b = f * 4;
            unsigned* t = m.idx + f * 6;
            t[0] = b; t[1] = b + 1; t[2] = b + 2; t[3] = b; t[4] = b + 2; t[5] = b + 3;
        }
        return m;
    }

    Mesh gear(float inner, float outer, float width, int teeth, float depth) {
        Mesh m; m.nv = teeth * 11 * 4; m.nt = teeth * 11 * 2;
        m.v = arena_.alloc<Vert>(m.nv); m.idx = arena_.alloc<unsigned>(m.nt * 3);
        if (!m.ok()) return m;
        int nv = 0, ni = 0;
        auto quad = [&](V3 a, V3 b, V3 c, V3 d, V3 na, V3 nb, V3 nc, V3 nd) {
            unsigned base = (unsigned)nv;
            m.v[nv++] = Vert{ a, na }; m.v[nv++] = Vert{ b, nb }; m.v[nv++] = Vert{ c, nc }; m.v[nv++] = Vert{ d, nd };
            unsigned* t = m.idx + ni; ni += 6;
            t[0] = base; t[1] = base + 1; t[2] = base + 2; t[3] = base; t[4] = base + 2; t[5] = base + 3;
        };
        auto flat = [&](V3 a, V3 b, V3 c, V3 d, V3 n) { quad(a, b, c, d, n, n, n, n); };
        float r0 = inner, r1 = outer - depth * 0.5f, r2 = outer + depth * 0.5f, h = width * 0.5f;
        float da = 2.0f * kPi / (float)teeth / 4.0f;
        auto at = [](float r, float a, float z) { return V3(r * fcos(a), r * fsin(a), z); };
        for (int i = 0; i < teeth; ++i) {
            float a = (float)i * 2.0f * kPi / (float)teeth;
            float a1 = a + da, a2 = a + 2 * da, a3 = a + 3 * da, a4 = a + 4 * da;
            for (int side = 0; side < 2; ++side) {
                float z = side ? -h : h;
                V3 n(0, 0, side ? -1.0f : 1.0f);
                flat(at(r0, a, z), at(r1, a, z), at(r1, a3, z), at(r0, a3, z), n);
                flat(at(r0, a3, z), at(r1, a3, z), at(r1, a4, z), at(r0, a4, z), n);
                flat(at(r1, a, z), at(r2, a1, z), at(r2, a2, z), at(r1, a3, z), n);
            }
            float ra[5] = { r1, r2, r2, r1, r1 }, aa[5] = { a, a1, a2, a3, a4 };
            for (int k = 0; k < 4; ++k) {
                V3 p = at(ra[k], aa[k], 0), q = at(ra[k + 1], aa[k + 1], 0);
                V3 d = q - p;
                V3 n = norm(V3(d.y, -d.x, 0));
                flat(at(ra[k], aa[k], h), at(ra[k + 1], aa[k + 1], h), at(ra[k + 1], aa[k + 1], -h), at(ra[k], aa[k], -h), n);
            }
            V3 n0(-fcos(a), -fsin(a), 0), n4(-fcos(a4), -fsin(a4), 0);
            quad(at(r0, a, h), at(r0, a4, h), at(r0, a4, -h), at(r0, a, -h), n0, n4, n4, n0);
        }
        return m;
    }

    Mesh floor(float half) {
        Mesh m = grid(8, 8);
        if (!m.ok()) return m;
        for (int r = 0; r <= 8; ++r)
            for (int c = 0; c <= 8; ++c)
                m.v[r * 9 + c] = Vert{ V3(-half + 2 * half * c / 8, 0, -half + 2 * half * r / 8), V3(0, 1, 0) };
        quads(m, 8, 8, false);
        flip(m);
        return m;
    }

    static void orient(Mesh& m) {
        for (int t = 0; t < m.nt; ++t) {
            unsigned* i = m.idx + t * 3;
            V3 a = m.v[i[0]].p, b = m.v[i[1]].p, c = m.v[i[2]].p;
            V3 g = cross(b - a, c - a);
            V3 n = m.v[i[0]].n + m.v[i[1]].n + m.v[i[2]].n;
            if (dot(g, n) < 0) { unsigned x = i[1]; i[1] = i[2]; i[2] = x; }
        }
    }

private:
    static void flip(Mesh& m) { for (int t = 0; t < m.nt; ++t) { unsigned x = m.idx[t * 3 + 1]; m.idx[t * 3 + 1] = m.idx[t * 3 + 2]; m.idx[t * 3 + 2] = x; } }
    Arena& arena_;
};

struct Light {
    V3 dir;
    V3 color;
    V3 sky, ground;
};

struct ClipVert {
    V4 c;
    V3 p, n;
};

struct ScreenVert {
    float x, y, iw;
    V3 pw, nw;
};

class Rasterizer {
public:
    void bind(unsigned* color, int w, int h, int pitch, void* work) {
        color_ = color; w_ = w; h_ = h; pitch_ = pitch;
        depth_ = (float*)work;
        mask_ = (unsigned char*)work + (unsigned long)w * h * sizeof(float);
    }
    void clear(const V3& top, const V3& bottom) {
        for (int y = 0; y < h_; ++y) {
            float t = (float)y / (float)(h_ > 1 ? h_ - 1 : 1);
            unsigned c = pack(lerp(top, bottom, t));
            unsigned* row = color_ + (unsigned long)y * pitch_;
            float* d = depth_ + (unsigned long)y * w_;
            unsigned char* mk = mask_ + (unsigned long)y * w_;
            for (int x = 0; x < w_; ++x) { row[x] = c; d[x] = 0.0f; mk[x] = 0; }
        }
    }

    void set_camera(const M4& vp, const V3& eye, const Light& l, const V3& fog) { vp_ = vp; eye_ = eye; light_ = l; fog_ = fog; }

    void draw(const Mesh& m, const M4& model, const Material& mat, r3d_stats& st, bool wire) {
        if (!m.ok()) return;
        st.objects++;
        for (int t = 0; t < m.nt; ++t) {
            ClipVert cv[3];
            for (int k = 0; k < 3; ++k) {
                const Vert& vx = m.v[m.idx[t * 3 + k]];
                cv[k].p = model.point(vx.p);
                cv[k].n = model.vec(vx.n);
                cv[k].c = vp_.clip(cv[k].p);
            }
            st.tris_in++;
            if (wire) wire_tri(cv, mat, st);
            else clip_and_fill(cv, mat, st, Pass::Shade);
        }
    }

    void draw_shadow(const Mesh& m, const M4& model, r3d_stats& st) {
        if (!m.ok()) return;
        V3 L = light_.dir;
        if (L.y < 0.05f) return;
        Material none{ V3(0, 0, 0), 0, 1, 0, false };
        for (int t = 0; t < m.nt; ++t) {
            ClipVert cv[3];
            for (int k = 0; k < 3; ++k) {
                V3 p = model.point(m.v[m.idx[t * 3 + k]].p);
                float s = p.y / L.y;
                p = V3(p.x - L.x * s, 0.004f, p.z - L.z * s);
                cv[k].p = p; cv[k].n = V3(0, 1, 0); cv[k].c = vp_.clip(p);
            }
            clip_and_fill(cv, none, st, Pass::Shadow);
        }
    }

private:
    enum class Pass { Shade, Shadow };

    static unsigned pack(const V3& c) {
        int r = (int)(fsqrt(clamp01(c.x)) * 255.0f + 0.5f), g = (int)(fsqrt(clamp01(c.y)) * 255.0f + 0.5f), b = (int)(fsqrt(clamp01(c.z)) * 255.0f + 0.5f);
        return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
    }

    void clip_and_fill(const ClipVert* in, const Material& mat, r3d_stats& st, Pass pass) {
        const float zn = 0.02f;
        ClipVert out[4]; int n = 0;
        for (int i = 0; i < 3; ++i) {
            const ClipVert& a = in[i]; const ClipVert& b = in[(i + 1) % 3];
            float da = a.c.w - zn, db = b.c.w - zn;
            if (da >= 0) out[n++] = a;
            if ((da >= 0) != (db >= 0)) {
                float t = da / (da - db);
                ClipVert m;
                m.c = V4{ a.c.x + (b.c.x - a.c.x) * t, a.c.y + (b.c.y - a.c.y) * t, a.c.z + (b.c.z - a.c.z) * t, a.c.w + (b.c.w - a.c.w) * t };
                m.p = lerp(a.p, b.p, t); m.n = lerp(a.n, b.n, t);
                out[n++] = m;
                if (n == 4) break;
            }
        }
        if (n < 3) return;
        if (n == 4) st.clipped++;
        ScreenVert sv[4];
        for (int i = 0; i < n; ++i) {
            float iw = 1.0f / out[i].c.w;
            sv[i].x = (out[i].c.x * iw * 0.5f + 0.5f) * (float)w_;
            sv[i].y = (0.5f - out[i].c.y * iw * 0.5f) * (float)h_;
            sv[i].iw = iw;
            sv[i].pw = out[i].p * iw;
            sv[i].nw = out[i].n * iw;
        }
        fill(sv[0], sv[1], sv[2], mat, st, pass);
        if (n == 4) fill(sv[0], sv[2], sv[3], mat, st, pass);
    }

    V3 shade(const V3& P, const V3& Nin, const Material& mat) const {
        V3 N = norm(Nin);
        V3 V = norm(eye_ - P);
        if (dot(N, V) < 0) N = N * -1.0f;
        V3 albedo = mat.albedo;
        if (mat.checker) {
            int cx = ifloor(P.x * 0.5f), cz = ifloor(P.z * 0.5f);
            bool odd = ((cx + cz) & 1) != 0;
            albedo = odd ? V3(0.20f, 0.24f, 0.30f) : V3(0.62f, 0.66f, 0.72f);
            float lx = P.x * 0.5f - (float)cx, lz = P.z * 0.5f - (float)cz;
            float edge = fminf_(fminf_(lx, 1 - lx), fminf_(lz, 1 - lz));
            if (edge < 0.02f) albedo = V3(0.36f, 0.86f, 0.70f);
        }
        float hemi = N.y * 0.5f + 0.5f;
        V3 amb = lerp(light_.ground, light_.sky, hemi);
        float diff = fmaxf_(0.0f, dot(N, light_.dir));
        V3 H = norm(light_.dir + V);
        float sp = fmaxf_(0.0f, dot(N, H));
        for (int i = 0; i < mat.shine; ++i) sp *= sp;
        float fr = 1.0f - fmaxf_(0.0f, dot(N, V));
        float rim = fr * fr * fr * mat.rim;
        V3 c = albedo * amb + albedo * light_.color * diff + light_.color * (sp * mat.spec) + V3(0.35f, 0.75f, 1.0f) * rim;
        float dist = fsqrt(dot(P - eye_, P - eye_));
        float f = clamp01((dist - 9.0f) / 26.0f);
        return lerp(c, fog_, f * f);
    }

    void fill(const ScreenVert& a, const ScreenVert& b, const ScreenVert& c, const Material& mat, r3d_stats& st, Pass pass) {
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (pass == Pass::Shade && !mat.checker && area >= 0) return;
        if (fabs_(area) < 1e-6f) return;
        const ScreenVert* v0 = &a; const ScreenVert* v1 = &b; const ScreenVert* v2 = &c;
        if (area > 0) { v1 = &c; v2 = &b; area = -area; }
        float inv = 1.0f / -area;
        int x0 = ifloor(fminf_(v0->x, fminf_(v1->x, v2->x))), x1 = ifloor(fmaxf_(v0->x, fmaxf_(v1->x, v2->x))) + 1;
        int y0 = ifloor(fminf_(v0->y, fminf_(v1->y, v2->y))), y1 = ifloor(fmaxf_(v0->y, fmaxf_(v1->y, v2->y))) + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w_) x1 = w_;
        if (y1 > h_) y1 = h_;
        if (x0 >= x1 || y0 >= y1) return;
        st.tris_drawn++;
        float e0a = v2->y - v1->y, e0b = v1->x - v2->x;
        float e1a = v0->y - v2->y, e1b = v2->x - v0->x;
        float e2a = v1->y - v0->y, e2b = v0->x - v1->x;
        for (int y = y0; y < y1; ++y) {
            float py = (float)y + 0.5f, px = (float)x0 + 0.5f;
            float w0 = (px - v1->x) * e0a + (py - v1->y) * e0b;
            float w1 = (px - v2->x) * e1a + (py - v2->y) * e1b;
            float w2 = (px - v0->x) * e2a + (py - v0->y) * e2b;
            unsigned* row = color_ + (unsigned long)y * pitch_;
            float* drow = depth_ + (unsigned long)y * w_;
            unsigned char* mrow = mask_ + (unsigned long)y * w_;
            for (int x = x0; x < x1; ++x, w0 += e0a, w1 += e1a, w2 += e2a) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
                float iw = b0 * v0->iw + b1 * v1->iw + b2 * v2->iw;
                if (pass == Pass::Shadow) {
                    if (mrow[x] || iw < drow[x] * 0.999f) continue;
                    mrow[x] = 1;
                    unsigned p = row[x];
                    row[x] = ((p >> 1) & 0x7F7F7Fu) + ((p >> 3) & 0x1F1F1Fu);
                    st.pixels++;
                    continue;
                }
                if (iw <= drow[x]) continue;
                drow[x] = iw;
                float w = 1.0f / iw;
                V3 P = (v0->pw * b0 + v1->pw * b1 + v2->pw * b2) * w;
                V3 N = (v0->nw * b0 + v1->nw * b1 + v2->nw * b2) * w;
                row[x] = pack(shade(P, N, mat));
                st.pixels++;
            }
        }
    }

    void line(float xa, float ya, float xb, float yb, unsigned c) {
        float dx = xb - xa, dy = yb - ya;
        float len = fmaxf_(fabs_(dx), fabs_(dy));
        int n = (int)len + 1;
        if (n > 4096) return;
        float sx = dx / (float)n, sy = dy / (float)n;
        for (int i = 0; i <= n; ++i) {
            int x = (int)xa, y = (int)ya;
            if (x >= 0 && y >= 0 && x < w_ && y < h_) color_[(unsigned long)y * pitch_ + x] = c;
            xa += sx; ya += sy;
        }
    }

    void wire_tri(const ClipVert* cv, const Material& mat, r3d_stats& st) {
        for (int k = 0; k < 3; ++k) if (cv[k].c.w < 0.05f) return;
        float sx[3], sy[3];
        for (int k = 0; k < 3; ++k) {
            float iw = 1.0f / cv[k].c.w;
            sx[k] = (cv[k].c.x * iw * 0.5f + 0.5f) * (float)w_;
            sy[k] = (0.5f - cv[k].c.y * iw * 0.5f) * (float)h_;
        }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
        if (!mat.checker && area >= 0) return;
        V3 n = norm(cv[0].n + cv[1].n + cv[2].n);
        V3 col = mat.checker ? V3(0.12f, 0.35f, 0.30f) : lerp(mat.albedo, V3(1, 1, 1), 0.35f) * (0.45f + 0.55f * fmaxf_(0.0f, dot(n, light_.dir)));
        unsigned c = pack(col);
        line(sx[0], sy[0], sx[1], sy[1], c);
        line(sx[1], sy[1], sx[2], sy[2], c);
        line(sx[2], sy[2], sx[0], sy[0], c);
        st.tris_drawn++;
    }

    unsigned* color_ = nullptr;
    float* depth_ = nullptr;
    unsigned char* mask_ = nullptr;
    int w_ = 0, h_ = 0, pitch_ = 0;
    M4 vp_;
    V3 eye_, fog_;
    Light light_;
};

class World {
public:
    bool init(void* mem, unsigned long size) {
        arena_.init(mem, size);
        MeshBuilder mb(arena_);
        knot_ = mb.torus_knot(160, 14, 2, 3, 0.30f);
        sphere_ = mb.sphere(28, 18);
        cube_ = mb.cube();
        floor_ = mb.floor(40.0f);
        gear_[0] = mb.gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
        gear_[1] = mb.gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
        gear_[2] = mb.gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
        for (Mesh& g : gear_) if (g.ok()) MeshBuilder::orient(g);
        if (knot_.ok()) MeshBuilder::orient(knot_);
        if (sphere_.ok()) MeshBuilder::orient(sphere_);
        if (cube_.ok()) MeshBuilder::orient(cube_);
        ready_ = knot_.ok() && sphere_.ok() && cube_.ok() && floor_.ok() && gear_[0].ok() && gear_[1].ok() && gear_[2].ok();
        return ready_;
    }
    bool ready() const { return ready_; }
    unsigned long used() const { return arena_.used(); }

    void frame(unsigned* color, int w, int h, int pitch, void* work, const r3d_view& v, r3d_stats& st) {
        float t = (float)(v.t_us % 3600000000ULL) * 1e-6f;
        float yaw = (float)v.yaw_md * (kPi / 180000.0f);
        float pitch_a = (float)v.pitch_md * (kPi / 180000.0f);
        float dist = (float)v.dist_mm * 0.001f;
        V3 target(0, 1.1f, 0);
        V3 eye = target + V3(fcos(pitch_a) * fsin(yaw), fsin(pitch_a), fcos(pitch_a) * fcos(yaw)) * dist;
        M4 view = M4::look_at(eye, target, V3(0, 1, 0));
        M4 proj = M4::perspective(55.0f * kPi / 180.0f, (float)w / (float)(h > 0 ? h : 1), 0.05f, 80.0f);
        bool gears = v.scene == 0;
        Light light;
        float la = v.scene == 2 ? t * 0.6f : 0.9f;
        light.dir = gears ? norm(V3(0.5f, 0.5f, 1.0f)) : norm(V3(fcos(la) * 0.6f, 1.0f, fsin(la) * 0.6f + 0.35f));
        light.color = V3(1.0f, 0.95f, 0.86f);
        light.sky = V3(0.30f, 0.38f, 0.52f);
        light.ground = V3(0.10f, 0.09f, 0.11f);
        V3 fog(0.07f, 0.09f, 0.14f);
        rast_.bind(color, w, h, pitch, work);
        if (gears) { light.sky = V3(0.16f, 0.16f, 0.18f); light.ground = V3(0.08f, 0.08f, 0.09f); fog = V3(0.012f, 0.012f, 0.03f); }
        if (gears) rast_.clear(V3(0.010f, 0.010f, 0.026f), V3(0.016f, 0.016f, 0.036f));
        else rast_.clear(V3(0.03f, 0.04f, 0.08f), V3(0.10f, 0.13f, 0.20f));
        rast_.set_camera(proj * view, eye, light, fog);

        Material floor_mat{ V3(0.5f, 0.5f, 0.5f), 0.15f, 3, 0.0f, true };
        if (!gears) rast_.draw(floor_, M4::identity(), floor_mat, st, v.wire != 0);

        int n = 0;
        Item* items = items_;
        build(v.scene, t, items, n);
        if (v.shadows && !v.wire && !gears)
            for (int i = 0; i < n; ++i) rast_.draw_shadow(*items[i].mesh, items[i].model, st);
        for (int i = 0; i < n; ++i) rast_.draw(*items[i].mesh, items[i].model, items[i].mat, st, v.wire != 0);
    }

private:
    static constexpr int kMaxItems = 240;
    struct Item { const Mesh* mesh; M4 model; Material mat; };

    void build(int scene, float t, Item* it, int& n) {
        auto add = [&](const Mesh& m, const M4& model, const Material& mat) { if (n < kMaxItems) it[n++] = Item{ &m, model, mat }; };
        if (scene == 0) {
            float ang = t * 70.0f * kPi / 180.0f;
            M4 base = M4::translate(V3(0.35f, 1.05f, 0)) * M4::rot_x(20.0f * kPi / 180.0f) * M4::rot_y(30.0f * kPi / 180.0f) * M4::scale(0.32f);
            add(gear_[0], base * M4::translate(V3(-3.0f, -2.0f, 0)) * M4::rot_z(ang), Material{ V3(0.62f, 0.012f, 0.0f), 0.30f, 4, 0.0f, false });
            add(gear_[1], base * M4::translate(V3(3.1f, -2.0f, 0)) * M4::rot_z(-2.0f * ang - 9.0f * kPi / 180.0f), Material{ V3(0.0f, 0.55f, 0.04f), 0.30f, 4, 0.0f, false });
            add(gear_[2], base * M4::translate(V3(-3.1f, 4.2f, 0)) * M4::rot_z(-2.0f * ang - 25.0f * kPi / 180.0f), Material{ V3(0.04f, 0.04f, 0.85f), 0.30f, 4, 0.0f, false });
        } else if (scene == 1) {
            add(knot_, M4::translate(V3(0, 1.55f, 0)) * M4::rot_y(t * 0.7f) * M4::rot_x(t * 0.4f), Material{ V3(0.95f, 0.45f, 0.18f), 0.9f, 6, 0.6f, false });
            static const Material orb[3] = {
                { V3(0.95f, 0.75f, 0.25f), 1.0f, 6, 0.3f, false },
                { V3(0.20f, 0.85f, 0.70f), 0.8f, 5, 0.8f, false },
                { V3(0.55f, 0.45f, 0.95f), 0.7f, 4, 0.9f, false } };
            for (int i = 0; i < 3; ++i) {
                float a = t * 0.9f + (float)i * 2.0944f;
                add(sphere_, M4::translate(V3(fcos(a) * 2.6f, 0.55f + 0.25f * fsin(t * 2.0f + i), fsin(a) * 2.6f)) * M4::scale(0.5f), orb[i]);
            }
            add(cube_, M4::translate(V3(0, 0.35f, 0)) * M4::rot_y(-t * 0.5f) * M4::scale(0.35f), Material{ V3(0.80f, 0.82f, 0.88f), 0.5f, 4, 0.2f, false });
        } else if (scene == 2) {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) {
                    float rough = (float)c / 3.0f;
                    V3 col = r == 0 ? V3(0.90f, 0.30f, 0.30f) : (r == 1 ? V3(0.30f, 0.80f, 0.55f) : V3(0.35f, 0.50f, 0.95f));
                    Material m{ col, 1.0f - rough * 0.8f, 7 - c * 2, 0.4f, false };
                    add(sphere_, M4::translate(V3(-2.4f + c * 1.6f, 0.55f + r * 1.15f, -0.8f + r * 0.4f)) * M4::scale(0.52f), m);
                }
        } else {
            for (int z = 0; z < 8; ++z)
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 3; ++y) {
                        float px = -3.5f + x, pz = -3.5f + z;
                        float wave = fsin(t * 2.0f + px * 0.7f + pz * 0.5f);
                        float hy = 0.35f + y * 0.9f + wave * 0.3f;
                        V3 col(0.5f + 0.5f * fsin(px * 0.6f + t), 0.5f + 0.5f * fsin(pz * 0.6f + 2.0f), 0.6f + 0.4f * fcos(y + t * 0.5f));
                        add(cube_, M4::translate(V3(px, hy, pz)) * M4::rot_y(t + x * 0.3f) * M4::rot_x(t * 0.7f + z * 0.2f) * M4::scale(0.28f), Material{ col, 0.6f, 4, 0.3f, false });
                    }
        }
    }

    Arena arena_;
    Item items_[kMaxItems];
    Mesh knot_, sphere_, cube_, floor_, gear_[3];
    Rasterizer rast_;
    bool ready_ = false;
};

World* g_world = nullptr;
alignas(16) unsigned char g_world_store[sizeof(World)];

}

extern "C" {

unsigned long r3d_arena_need(void) { return 2UL * 1024 * 1024; }

unsigned long r3d_work_bytes(int w, int h) { return (unsigned long)w * h * (sizeof(float) + 1) + 64; }

int r3d_init(void* arena, unsigned long size) {
    if (!g_world) {
        unsigned char* p = g_world_store;
        for (unsigned long i = 0; i < sizeof(World); ++i) p[i] = 0;
        g_world = new (g_world_store) World();
    }
    return g_world->init(arena, size) ? (int)g_world->used() : 0;
}

void r3d_frame(unsigned* color, int w, int h, int pitch, void* work, const r3d_view* v, r3d_stats* st) {
    r3d_stats s{ 0, 0, 0, 0, 0 };
    if (g_world && g_world->ready() && color && work && v && w > 0 && h > 0 && pitch >= w)
        g_world->frame(color, w, h, pitch, work, *v, s);
    if (st) *st = s;
}

const char* r3d_scene_name(int scene) {
    static const char* const names[R3D_SCENES] = { "Gears", "Showcase: torus knot, orbs and shadows", "Materials: 12 spheres, moving light", "Stress: 192 spinning cubes" };
    return scene >= 0 && scene < R3D_SCENES ? names[scene] : "?";
}

}
