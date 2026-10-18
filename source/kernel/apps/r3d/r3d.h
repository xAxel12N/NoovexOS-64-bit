#ifndef NVX_APPS_R3D_H
#define NVX_APPS_R3D_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int scene;
    int wire;
    int shadows;
    int yaw_md;
    int pitch_md;
    int dist_mm;
    unsigned long long t_us;
} r3d_view;

typedef struct {
    int tris_in;
    int tris_drawn;
    int pixels;
    int objects;
    int clipped;
} r3d_stats;

#define R3D_SCENES 4

unsigned long r3d_arena_need(void);
int r3d_init(void* arena, unsigned long size);
unsigned long r3d_work_bytes(int w, int h);
void r3d_frame(unsigned* color, int w, int h, int pitch, void* work, const r3d_view* v, r3d_stats* st);
const char* r3d_scene_name(int scene);

#ifdef __cplusplus
}
#endif

#endif
