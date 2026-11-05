#include "include/nvx/lib/cxx.h"

extern "C" {
void nvd_run_request(void);
int nvd_run_state(int* res);
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/drivers/video/fb/gq.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "apps/graphics/r3d/r3d.h"
#include "include/nvx/assets/fontaa.h"
extern volatile unsigned char kdown[128];
extern int mx, my, g_nightlight, g_fb_rgb, g_tby, DOCK_x, DOCK_y, DOCK_w, DOCK_h;
int gfx_app_surf(u32** px, int w, int h);
int desk_win_exposed(int i);
void present_rect(int x, int y, int w, int h);
void fb_shadow_note(int x, int y, int w, int h);
extern u32* SCENE_NC;
}

namespace {

enum class Present { Cpu, CpuScaled, Intel, Nvidia };
enum class Fast { None, Cpu, Gpu2d };

enum Key : int {
    K_MINUS = 0x0C, K_PLUS = 0x0D, K_UP = 0x48, K_LEFT = 0x4B, K_RIGHT = 0x4D, K_DOWN = 0x50
};

constexpr int kBarH = 62;
constexpr int kMaxW = 1600;
constexpr int kMaxH = 1000;
constexpr int kDefaultDist[R3D_SCENES] = { 5200, 7200, 7600, 12500 };

class TextLine {
public:
    TextLine() { clear(); }
    void clear() { p_ = buf_; *p_ = 0; }
    TextLine& operator<<(const char* s) { while(s && *s && room(1)) *p_++ = *s++; *p_ = 0; return *this; }
    TextLine& operator<<(u32 v) { if(room(12)) { p_ = udec(p_, v); *p_ = 0; } return *this; }
    const char* str() const { return buf_; }
private:
    bool room(int n) const { return (p_ - buf_) + n < (int)sizeof(buf_); }
    char buf_[200];
    char* p_;
};

void frame_text(u32* buf, int pitch, int bw, int bh, int x, int y, const char* s, u32 col, int sc) {
    for(; *s; ++s, x += 8 * sc) {
        int idx = (unsigned char)*s - 32;
        if(idx < 0 || idx >= 95) continue;
        const unsigned char* g = fontaa[idx];
        for(int r = 0; r < 14; ++r) for(int c = 0; c < 8; ++c) {
            int a = g[r * 8 + c];
            if(a < 6) continue;
            for(int dy = 0; dy < sc; ++dy) for(int dx = 0; dx < sc; ++dx) {
                int px = x + c * sc + dx, py = y + r * sc + dy;
                if(px < 0 || py < 0 || px >= bw || py >= bh) continue;
                u32* p = buf + (u64)py * pitch + px;
                *p = a >= 250 ? col : mix(*p, col, a);
            }
        }
    }
}

class Gpu3dApp {
public:
    Gpu3dApp() : magic_(0x3D3D3D3D) { reset_view(); }

    void draw(int win);
    void click() {}
    void key(int c);
    void closed() { nv_frame_va_ = 0; nv_frame_ptr_ = 0; nv_scene_va_ = 0; nv_scene_ptr_ = 0; nv_gen_ = 0; nv2d_va_ = 0; nv2d_ptr_ = 0; lay_.valid = false; }
    bool constructed() const { return magic_ == 0x3D3D3D3D; }
    bool live() const { return constructed() && init_state_ != 2 && (!paused_ || gpu_req_ != 0); }
    bool fast();

private:
    void reset_view() {
        yaw_md_ = scene_ == 0 ? 0 : 30000; pitch_md_ = scene_ == 0 ? 0 : 17000; dist_mm_ = kDefaultDist[scene_];
    }
    void set_scene(int s) { if(s != scene_) { scene_ = s; reset_view(); } }
    bool ensure_init();
    bool ensure_buffers(int w, int h);
    void input(u64 dt_us);
    Present choose_path() const;
    void present(Present p, int x, int y, int w, int h);
    void cpu_copy(int x, int y, int w, int h, int scale);
    void info_bar(int x, int y, int w);
    void message(int x, int y, int w, int h, const char* a, const char* b);
    void step_time();
    void render(u32* target, int rw, int rh, int pitch);
    void overlay_text(u32* target, int pitch, int rw, int rh, int scale);

    u32 magic_;
    int scene_ = 0;
    bool wire_ = false, shadows_ = true, half_ = false, paused_ = false;
    int yaw_md_ = 0, pitch_md_ = 0, dist_mm_ = 0;
    int init_state_ = 0;
    nvx::KBuf<u8> arena_, work_;
    nvx::KBuf<u32> frame_;
    int fw_ = 0, fh_ = 0, fp_ = 0;
    u64 t_us_ = 0, last_ns_ = 0, fps_t0_ = 0;
    u32 frames_ = 0, fps_ = 0, render_us_ = 0;
    r3d_stats st_ = { 0, 0, 0, 0, 0 };
    Present path_ = Present::Cpu;
    u64 nv_frame_va_ = 0, nv_scene_va_ = 0;
    const void* nv_frame_ptr_ = 0;
    const void* nv_scene_ptr_ = 0;
    u32 nv_fail_ = 0;
    u32 nv_gen_ = 0;
    int intel_sid_ = 0;
    int gpu_req_ = 0;
    char gpu_msg_[96] = { 0 };
    struct Layout { bool valid; int wx, wy, ww, wh, ox, oy, rw, rh, scale; bool half; int scene; } lay_ = { false, 0, 0, 0, 0, 0, 0, 0, 0, 1, false, 0 };
    u64 t_full_ns_ = 0;
    u32 fast_frames_ = 0, fast_gpu_ = 0;
    Fast fast_path_ = Fast::None;
    u64 nv2d_va_ = 0;
    const void* nv2d_ptr_ = 0;
    u32 nv2d_gen_ = 0;
};

Gpu3dApp g_app;

bool Gpu3dApp::ensure_init() {
    if(init_state_ == 1) return true;
    if(init_state_ == 2) return false;
    if(!arena_.alloc(r3d_arena_need())) { init_state_ = 2; return false; }
    int used = r3d_init(arena_.get(), arena_.size());
    if(used <= 0) { init_state_ = 2; return false; }
    TextLine t; t << "gpu3d: meshes built, " << (u32)(used / 1024) << " KB of the scene arena used";
    klog(t.str());
    init_state_ = 1;
    last_ns_ = hr_ns();
    fps_t0_ = last_ns_;
    return true;
}

bool Gpu3dApp::ensure_buffers(int w, int h) {
    if(w == fw_ && h == fh_ && frame_ && work_) return true;
    frame_.reset(); work_.reset(); fw_ = fh_ = 0;
    nv_frame_va_ = 0; nv_frame_ptr_ = 0; nv2d_va_ = 0; nv2d_ptr_ = 0; lay_.valid = false;
    int fp = (w + 15) & ~15;
    if(!frame_.alloc((unsigned long)fp * h)) return false;
    if(!work_.alloc(r3d_work_bytes(w, h))) { frame_.reset(); return false; }
    fw_ = w; fh_ = h; fp_ = fp;
    return true;
}

void Gpu3dApp::input(u64 dt_us) {
    const volatile unsigned char* k = kdown;
    int ms = (int)nvx::min<u64>(dt_us / 1000, 100);
    if(k[K_LEFT]) yaw_md_ -= 90 * ms;
    if(k[K_RIGHT]) yaw_md_ += 90 * ms;
    if(k[K_UP]) pitch_md_ += 60 * ms;
    if(k[K_DOWN]) pitch_md_ -= 60 * ms;
    if(k[K_PLUS]) dist_mm_ -= 6 * ms;
    if(k[K_MINUS]) dist_mm_ += 6 * ms;
    pitch_md_ = nvx::clamp(pitch_md_, scene_ == 0 ? -80000 : 3000, 80000);
    dist_mm_ = nvx::clamp(dist_mm_, 2500, 30000);
}

void Gpu3dApp::key(int c) {
    if(c >= 'A' && c <= 'Z') c += 32;
    switch(c) {
    case '1': case '2': case '3': case '4': set_scene(c - '1'); break;
    case 'w': wire_ = !wire_; break;
    case 's': shadows_ = !shadows_; break;
    case 'h': half_ = !half_; break;
    case ' ': paused_ = !paused_; break;
    case 'r': reset_view(); break;
    case 'g': if(!gpu_req_) gpu_req_ = 1; break;
    case '+': case '=': dist_mm_ = nvx::max(dist_mm_ - 400, 2500); break;
    case '-': dist_mm_ = nvx::min(dist_mm_ + 400, 30000); break;
    default: break;
    }
}

Present Gpu3dApp::choose_path() const {
    if(half_) return Present::CpuScaled;
    if(nvgsp_ce_ready() && nv_fail_ < 3) return Present::Nvidia;
    if(g_gfx) return Present::Intel;
    return Present::Cpu;
}

void Gpu3dApp::cpu_copy(int x, int y, int w, int h, int scale) {
    u32* sc = SCENE;
    const u32* src = frame_.get();
    for(int yy = 0; yy < h * scale; ++yy) {
        int dy = y + yy;
        if((u32)dy >= H) continue;
        u32* d = sc + (u64)dy * SW + x;
        const u32* s = src + (u64)(yy / scale) * fp_;
        int xm = w * scale;
        if(x + xm > (int)W) xm = (int)W - x;
        if(scale == 1) for(int xx = 0; xx < xm; ++xx) d[xx] = s[xx];
        else for(int xx = 0; xx < xm; ++xx) d[xx] = s[xx / scale];
    }
}

void Gpu3dApp::present(Present p, int x, int y, int w, int h) {
    if(p == Present::Nvidia) {
        u32* sc = SCENE;
        if(nv_gen_ != nvgsp_ce_gen()) { nv_gen_ = nvgsp_ce_gen(); nv_frame_ptr_ = 0; nv_scene_ptr_ = 0; }
        if(nv_frame_ptr_ != frame_.get()) { nv_frame_va_ = nvgsp_ce_map((u64)frame_.get(), (u64)fp_ * fh_ * 4); nv_frame_ptr_ = frame_.get(); }
        if(nv_scene_ptr_ != sc) { nv_scene_va_ = nvgsp_ce_map((u64)sc, (u64)SW * H * 4); nv_scene_ptr_ = sc; }
        int cw = nvx::min(w, (int)W - x), ch = nvx::min(h, (int)H - y);
        if(nv_frame_va_ && nv_scene_va_ && cw > 0 && ch > 0 &&
           nvgsp_ce_copy2d(nv_frame_va_, (u32)fp_ * 4, nv_scene_va_ + ((u64)y * SW + x) * 4, SW * 4, (u32)cw * 4, (u32)ch)) {
            path_ = Present::Nvidia;
            return;
        }
        ++nv_fail_;
        if(nv_fail_ == 3) klog("gpu3d: the NVIDIA copy engine did not finish three copies, using the CPU from now on");
        cpu_copy(x, y, w, h, 1);
        path_ = Present::Cpu;
        return;
    }
    if(p == Present::Intel) {
        gq_copy(g_rt, x, y, w, h, intel_sid_, 0, 0, 256, 0);
        path_ = Present::Intel;
        return;
    }
    cpu_copy(x, y, w, h, p == Present::CpuScaled ? 2 : 1);
    path_ = p;
}

void Gpu3dApp::message(int x, int y, int w, int h, const char* a, const char* b) {
    fill_rect(x, y, w, h, rgb(10, 13, 22));
    dstr_ctr(x + w / 2, y + h / 2 - 24, a, rgb(94, 224, 176), 2);
    if(b) dstr_ctr(x + w / 2, y + h / 2 + 10, b, rgb(200, 205, 220), 1);
}

void Gpu3dApp::info_bar(int x, int y, int w) {
    fill_rect(x, y, w, kBarH, rgb(17, 21, 31));
    fill_rect(x, y, w, 1, rgb(38, 45, 63));
    TextLine a;
    a << r3d_scene_name(scene_) << "   " << fps_ << " fps   " << render_us_ / 1000 << "." << (render_us_ / 100) % 10 << " ms   "
      << (u32)st_.tris_drawn << " triangles   " << (u32)fw_ << "x" << (u32)fh_;
    dstr(x + 10, y + 6, a.str(), rgb(230, 233, 242), 1);
    TextLine b;
    const char* how = path_ == Present::Nvidia ? "NVIDIA copy engine: " : path_ == Present::Intel ? "Intel GPU blitter" : path_ == Present::CpuScaled ? "CPU copy, 2x scaled" : "CPU copy";
    b << "to screen: " << how;
    if(path_ == Present::Nvidia) b << nvgsp_gpu_name();
    if(path_ != Present::Nvidia && !nvgsp_ce_ready()) {
        if(nvgsp_off_why()[0]) {
            char why[64];
            int i = 0;
            for(; i < 63 && nvgsp_off_why()[i]; ++i) why[i] = nvgsp_off_why()[i];
            why[i] = 0;
            b << "   NVIDIA off: " << why;
        } else if(g_nv_step > 0) {
            char why[64];
            int i = 0;
            for(; i < 63 && g_nv_why[i]; ++i) why[i] = g_nv_why[i];
            why[i] = 0;
            b << "   NVIDIA start failed at step " << (u32)g_nv_step << ": " << why;
        } else {
            b << "   (press G to start the NVIDIA card)";
        }
    }
    if(nvgsp_present_on()) b << "   desktop: copy engine";
    if(fast_frames_) b << "   window frames: " << (fast_path_ == Fast::Gpu2d ? "NVIDIA 2D engine" : "CPU");
    dstr(x + 10, y + 24, b.str(), path_ == Present::Nvidia ? rgb(118, 185, 0) : rgb(154, 163, 184), 1);
    if(w >= 800) dstr(x + 10, y + 42, "1 gears  2 3 4 more scenes | W wire | S shadows | H half | Space pause | arrows + - camera | R reset | G NVIDIA", rgb(100, 109, 132), 1);
}

void Gpu3dApp::draw(int win) {
    TextLine title;
    title << "Gears - C++ 3D renderer" << (paused_ ? " (paused)" : "");
    win_frame(win, title.str());
    int cx = WN[win].x + 2, cy = WN[win].y + TITLEH, cw = WN[win].w - 4, ch = WN[win].h - TITLEH - 2;
    int rh_area = ch - kBarH;
    if(cw < 64 || rh_area < 48) return;
    if(!constructed()) { message(cx, cy, cw, ch, "C++ constructors did not run", "cxx_run_ctors() was not called at boot"); return; }
    if(!ensure_init()) { message(cx, cy, cw, ch, "Not enough memory for the 3D scene", 0); return; }

    if(gpu_req_ == 1) {
        message(cx, cy, cw, ch, "Starting the NVIDIA card...", "nvgsp all: FWSEC, booter, GSP-RM, channels. This can take up to 30 seconds.");
        gpu_req_ = 2;
        return;
    }
    if(gpu_req_ == 2) { nvd_run_request(); gpu_req_ = 3; }
    if(gpu_req_ == 3) {
        int r = 0;
        if(nvd_run_state(&r)) { message(cx, cy, cw, ch, "Starting the NVIDIA card...", "nvgsp all: FWSEC, booter, GSP-RM, channels. This can take up to 30 seconds."); return; }
        if(r > 0) scopy(gpu_msg_, nv2d_ready() ? "NVIDIA card ready: copy engine and 2D engine working" : "NVIDIA card ready: copy engine working, 2D engine not verified");
        else scopy(gpu_msg_, "NVIDIA card not started - the reason is below, full log in nvgsp-log.txt");
        klog(gpu_msg_);
        nv_fail_ = 0;
        gpu_req_ = 0;
        last_ns_ = hr_ns();
    }

    int scale = half_ ? 2 : 1;
    int rw = nvx::min(cw / scale, kMaxW), rh = nvx::min(rh_area / scale, kMaxH);
    Present want = choose_path();
    u32* target = 0;
    int pitch = rw;
    if(want == Present::Intel) {
        u32* px = 0;
        intel_sid_ = gfx_app_surf(&px, rw, rh);
        if(intel_sid_ > 0 && px) { gfx_sync(); target = px; }
        else want = Present::Cpu;
    }
    if(!ensure_buffers(rw, rh)) { message(cx, cy, cw, ch, "Not enough memory for a frame this size", "make the window smaller"); return; }
    if(!target) { target = frame_.get(); pitch = fp_; }

    step_time();
    render(target, rw, rh, pitch);
    overlay_text(target, pitch, rw, rh, scale);

    int ox = cx + (cw - rw * scale) / 2, oy = cy + (rh_area - rh * scale) / 2;
    if(ox > cx || oy > cy) fill_rect(cx, cy, cw, rh_area, rgb(8, 10, 16));
    present(want, ox, oy, rw, rh);
    info_bar(cx, cy + rh_area, cw);
    lay_ = { want != Present::Intel, WN[win].x, WN[win].y, WN[win].w, WN[win].h, ox, oy, rw, rh, scale, half_, scene_ };
    t_full_ns_ = hr_ns();
}

void Gpu3dApp::step_time() {
    u64 now = hr_ns();
    u64 dt = (now - last_ns_) / 1000;
    last_ns_ = now;
    if(dt > 200000) dt = 200000;
    if(focus == W_GPU3D) input(dt);
    if(!paused_) t_us_ += dt;
    ++frames_;
    if(now - fps_t0_ >= 1000000000ULL) { fps_ = frames_; frames_ = 0; fps_t0_ = now; }
}

void Gpu3dApp::render(u32* target, int rw, int rh, int pitch) {
    r3d_view v;
    v.scene = scene_;
    v.wire = wire_ ? 1 : 0;
    v.shadows = shadows_ ? 1 : 0;
    v.yaw_md = scene_ == 0 ? yaw_md_ : yaw_md_ + (int)((t_us_ / 1000) * 12 % 360000);
    v.pitch_md = pitch_md_;
    v.dist_mm = dist_mm_;
    v.t_us = t_us_;
    u64 r0 = hr_ns();
    r3d_frame(target, rw, rh, pitch, work_.get(), &v, &st_);
    render_us_ = (u32)((hr_ns() - r0) / 1000);
}

void Gpu3dApp::overlay_text(u32* target, int pitch, int rw, int rh, int scale) {
    TextLine fps;
    fps << "FPS: " << fps_;
    frame_text(target, pitch, rw, rh, 14 / scale, 12 / scale, fps.str(), rgb(60, 255, 60), scale == 1 ? 3 : 1);
    if(gpu_msg_[0]) frame_text(target, pitch, rw, rh, 14 / scale, 50 / scale, gpu_msg_, rgb(255, 196, 107), 1);
}

bool Gpu3dApp::fast() {
    if(!lay_.valid || !constructed() || init_state_ != 1 || paused_ || gpu_req_) return false;
    const Win& w = WN[W_GPU3D];
    if(w.x != lay_.wx || w.y != lay_.wy || w.w != lay_.ww || w.h != lay_.wh || half_ != lay_.half || scene_ != lay_.scene) return false;
    if(hr_ns() - t_full_ns_ > 500000000ULL) return false;
    if(g_nightlight || !desk_win_exposed(W_GPU3D)) return false;
    if(choose_path() == Present::Intel) return false;
    int s = lay_.scale, x0 = lay_.ox, y0 = lay_.oy, dw = lay_.rw * s, dh = lay_.rh * s;
    if(x0 < 0 || y0 < 0 || x0 + dw > (int)W || y0 + dh > (int)H) return false;
    const int cb = 128;
    if(mx - cb / 2 < x0 + dw && mx + cb / 2 > x0 && my - cb / 2 < y0 + dh && my + cb / 2 > y0) return false;
    int bar = g_tby > 0 ? g_tby : (int)H - 64;
    if(y0 + dh > bar) return false;
    if(DOCK_w > 0 && DOCK_h > 0 && DOCK_x < x0 + dw && DOCK_x + DOCK_w > x0 && DOCK_y < y0 + dh && DOCK_y + DOCK_h > y0) return false;
    if(!ensure_buffers(lay_.rw, lay_.rh)) return false;
    step_time();
    u32* f = frame_.get();
    render(f, lay_.rw, lay_.rh, fp_);
    overlay_text(f, fp_, lay_.rw, lay_.rh, s);
    cpu_copy(x0, y0, lay_.rw, lay_.rh, s);
    bool gpu = false;
    if(nv2d_ready() && !g_fb_rgb && BYPP == 4) {
        if(nv2d_gen_ != nvgsp_ce_gen()) { nv2d_gen_ = nvgsp_ce_gen(); nv2d_va_ = 0; nv2d_ptr_ = 0; }
        gpu = nv2d_to_screen(f, (u32)fp_, lay_.rw, lay_.rh, x0, y0, s, &nv2d_va_, &nv2d_ptr_) != 0;
    }
    if(gpu) {
        u32* sc = SCENE;
        for(int r = 0; r < dh; ++r) { u32* d = SCENE_NC + (u64)(y0 + r) * SW + x0; const u32* q = sc + (u64)(y0 + r) * SW + x0; for(int k = 0; k < dw; ++k) d[k] = q[k]; }
        fb_shadow_note(x0, y0, dw, dh);
    } else {
        present_rect(x0, y0, dw, dh);
    }
    fast_path_ = gpu ? Fast::Gpu2d : Fast::Cpu;
    ++fast_frames_;
    if(gpu) ++fast_gpu_;
    return true;
}

}

extern "C" void gpu3d_draw(int i) { g_app.draw(i); }
extern "C" void gpu3d_click(void) { g_app.click(); }
extern "C" void gpu3d_key(int c) { g_app.key(c); }
extern "C" void gpu3d_closed(void) { g_app.closed(); }
extern "C" int gpu3d_live(void) { return g_app.live() ? 1 : 0; }
extern "C" int gpu3d_fast(void) { return g_app.fast() ? 1 : 0; }
