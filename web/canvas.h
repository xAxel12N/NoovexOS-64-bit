#pragma once
nimg* cv_get(node* n, int w, int h);
void cv_fill(nimg* im, const float* pts, const int* cnt, int nsub, const float* pnt, int np, float alpha, int mode, const int* clip);
void cv_text(nimg* im, const char* s, int n, float x, float base, int face, float size, const float* pnt, int np, float alpha, const int* clip);
void cv_image(nimg* dst, const nimg* src, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float alpha, const int* clip);
void cv_getdata(const nimg* im, int x, int y, int w, int h, unsigned char* out);
void cv_putdata(nimg* im, const unsigned char* in, int w, int h, int x, int y);
void cv_reset(void);
