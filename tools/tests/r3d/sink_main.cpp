#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../../apps/graphics/r3d/r3d.h"
static int W = 320, H = 200;
static unsigned* g_img; static float* g_z; static long g_tris, g_bad;
static void tri(void*, const float* v) {
    g_tris++;
    float X[3], Y[3], Z[3], iw[3];
    for (int k = 0; k < 3; k++) {
        const float* p = v + k * 8;
        for (int i = 0; i < 8; i++) if (!isfinite(p[i])) { g_bad++; return; }
        if (p[3] <= 0 || p[2] < 0 || p[2] > p[3] || p[4] < 0 || p[4] > 1 || p[5] < 0 || p[5] > 1 || p[6] < 0 || p[6] > 1) { g_bad++; return; }
        iw[k] = 1.0f / p[3]; X[k] = (p[0] * iw[k] * 0.5f + 0.5f) * W; Y[k] = (0.5f - p[1] * iw[k] * 0.5f) * H; Z[k] = p[2] * iw[k];
    }
    float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]); if (area == 0) return; float inv = 1.0f / area;
    float fx0 = fminf(X[0], fminf(X[1], X[2])), fx1 = fmaxf(X[0], fmaxf(X[1], X[2])), fy0 = fminf(Y[0], fminf(Y[1], Y[2])), fy1 = fmaxf(Y[0], fmaxf(Y[1], Y[2]));
    int x0 = (int)floorf(fx0), x1 = (int)floorf(fx1) + 1, y0 = (int)floorf(fy0), y1 = (int)floorf(fy1) + 1;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float b0 = ((X[1] - px) * (Y[2] - py) - (Y[1] - py) * (X[2] - px)) * inv, b1 = ((X[2] - px) * (Y[0] - py) - (Y[2] - py) * (X[0] - px)) * inv, b2 = 1 - b0 - b1;
        if (b0 < 0 || b1 < 0 || b2 < 0) continue;
        float z = b0 * Z[0] + b1 * Z[1] + b2 * Z[2]; if (!(z < g_z[y * W + x])) continue; g_z[y * W + x] = z;
        float w = b0 * iw[0] + b1 * iw[1] + b2 * iw[2]; unsigned c = 0;
        for (int i = 0; i < 3; i++) { float f = (b0 * v[4 + i] * iw[0] + b1 * v[12 + i] * iw[1] + b2 * v[20 + i] * iw[2]) / w; c |= (unsigned)(f * 255.0f + 0.5f) << (16 - 8 * i); }
        g_img[y * W + x] = c;
    }
}
int main(void) {
    unsigned long an = r3d_arena_need(); void* arena = malloc(an); if (r3d_init(arena, an) <= 0) { printf("FAIL init\n"); return 1; }
    unsigned* ref = (unsigned*)malloc(W * H * 4); g_img = (unsigned*)malloc(W * H * 4); g_z = (float*)malloc(W * H * 4); void* work = malloc(r3d_work_bytes(W, H));
    int ok = 1;
    for (int scene = 0; scene < R3D_SCENES; scene++) for (int f = 0; f < 6; f++) {
        r3d_view v; v.scene = scene; v.wire = 0; v.shadows = 0; v.yaw_md = scene == 0 ? f * 9000 : 30000 + f * 50000; v.pitch_md = scene == 0 ? 0 : 17000 + f * 3000; v.dist_mm = 6000 + f * 1500; v.t_us = 1000000ULL * (unsigned long long)f * 3;
        r3d_stats a, b; r3d_frame(ref, W, H, W, work, &v, &a);
        float rgb[3]; r3d_clear_colour(&v, rgb); unsigned cc = ((unsigned)(rgb[0] * 255 + 0.5f) << 16) | ((unsigned)(rgb[1] * 255 + 0.5f) << 8) | (unsigned)(rgb[2] * 255 + 0.5f);
        for (int i = 0; i < W * H; i++) { g_img[i] = cc; g_z[i] = 1.0f; } g_tris = 0; g_bad = 0;
        r3d_frame_tris(W, H, &v, &b, tri, 0);
        double diff = 0; long big = 0;
        for (int i = 0; i < W * H; i++) { int d = 0; for (int s = 0; s < 24; s += 8) { int x = (int)((ref[i] >> s) & 255) - (int)((g_img[i] >> s) & 255); d += x < 0 ? -x : x; } diff += d / 3.0; if (d > 180) big++; }
        diff /= (W * H);
        if (g_bad || g_tris < 3 || b.tris_drawn <= 0 || diff > (scene == 0 ? 1.0 : 14.0) || big > W * H / 12) { printf("FAIL scene %d frame %d: tris %ld bad %ld mean diff %.2f far pixels %ld\n", scene, f, g_tris, g_bad, diff, big); ok = 0; }
        else if (f == 0) printf("  scene %d: %ld triangles, mean difference to the CPU picture %.2f of 255\n", scene, g_tris, diff);
    }
    free(arena); free(ref); free(g_img); free(g_z); free(work);
    printf(ok ? "sink ok\n" : "sink FAILED\n"); return ok ? 0 : 1;
}
