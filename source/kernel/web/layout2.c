#include "web/layout.h"

static float pxonly(L l) { return l.k == LK_VAL && l.pct == 0 ? l.px : 0; }

static void table_cols(box* t, int* ncols_out, float** mins, float** maxs, float** pcts);

void intrinsic(box* b, float* mn, float* mx) {
    if (b->intr) { *mn = b->minc; *mx = b->maxc; return; }
    style* s = b->st;
    float pb = pxonly(s->padding[1]) + pxonly(s->padding[3]) + s->border[1] + s->border[3];
    if (b->kind == BX_ROW || b->kind == BX_ROWGROUP) pb = 0;
    float a = 0, z = 0;
    if (s->width.k == LK_VAL && s->width.pct == 0 && b->kind != BX_TABLE) {
        float w = s->width.px + (s->boxsizing ? 0 : pb);
        if (w < pb) w = pb;
        a = z = w;
        goto done;
    }
    switch (b->kind) {
    case BX_REPLACED: {
        box_edges(b, 0);
        float h;
        float w = replaced_size(b, -1, -1, &h, -1);
        a = z = w;
        if (s->maxw.k == LK_VAL && s->maxw.pct != 0) a = 0;
        if (s->width.k == LK_VAL && s->width.pct != 0) a = 0;
        goto done2;
    }
    case BX_FLEX: {
        int row = s->flexdir == FD_ROW || s->flexdir == FD_ROWREV;
        float gap = pxonly(s->gcolgap);
        int n = 0;
        for (box* c = b->first; c; c = c->next) {
            if (!in_flow(c)) continue;
            float cm, cx;
            intrinsic(c, &cm, &cx);
            float mg = pxonly(c->st->margin[1]) + pxonly(c->st->margin[3]);
            if (c->st->basis.k == LK_VAL && c->st->basis.pct == 0 && row) { float bs = c->st->basis.px + (c->st->boxsizing ? 0 : (cx - cx)); if (bs > cx && c->st->grow == 0) cx = bs; }
            if (row) {
                z += cx + mg;
                if (s->flexwrap) { if (cm + mg > a) a = cm + mg; }
                else a += (c->st->shrink > 0 ? cm : cx) + mg;
            } else {
                if (cx + mg > z) z = cx + mg;
                if (cm + mg > a) a = cm + mg;
            }
            n++;
        }
        if (row && n > 1) { z += gap * (n - 1); if (!s->flexwrap) a += gap * (n - 1); }
        break;
    }
    case BX_GRID: {
        float gap = pxonly(s->gcolgap);
        int nc = s->ngcols > 0 ? s->ngcols : 1;
        float fixed = 0;
        int nfixed = 0;
        for (int i = 0; i < s->ngcols; i++) if (s->gcols[i].k == GT_PX) { fixed += s->gcols[i].v; nfixed++; }
        float cmn = 0, cmx = 0;
        for (box* c = b->first; c; c = c->next) {
            if (!in_flow(c)) continue;
            float m1, m2;
            intrinsic(c, &m1, &m2);
            float mg = pxonly(c->st->margin[1]) + pxonly(c->st->margin[3]);
            if (m1 + mg > cmn) cmn = m1 + mg;
            if (m2 + mg > cmx) cmx = m2 + mg;
        }
        if (s->gautocols) nc = 1;
        z = fixed + (nc - nfixed) * cmx + gap * (nc - 1);
        a = fixed + (nc - nfixed) * cmn + gap * (nc - 1);
        break;
    }
    case BX_TABLE: {
        int nc;
        float *mins, *maxs, *pcts;
        table_cols(b, &nc, &mins, &maxs, &pcts);
        float sx = s->bcollapse ? 0 : s->bspacingx;
        for (int i = 0; i < nc; i++) { a += mins[i]; z += maxs[i]; }
        a += sx * (nc + 1);
        z += sx * (nc + 1);
        if (s->width.k == LK_VAL && s->width.pct == 0) { float w = s->width.px - (s->boxsizing ? pb : 0); if (w > a) a = z = w; else z = a; }
        break;
    }
    default:
        if (b->inl) inline_measure(b, &a, &z);
        else {
            for (box* c = b->first; c; c = c->next) {
                if (c->st && c->st->position != P_STATIC && c->st->position != P_RELATIVE && c->st->position != P_STICKY) continue;
                float m1, m2;
                intrinsic(c, &m1, &m2);
                float mg = pxonly(c->st->margin[1]) + pxonly(c->st->margin[3]);
                if (m1 + mg > a) a = m1 + mg;
                if (m2 + mg > z) z = m2 + mg;
            }
        }
        break;
    }
    a += pb;
    z += pb;
done:
    if (s->maxw.k == LK_VAL && s->maxw.pct == 0) { float m = s->maxw.px + (s->boxsizing ? 0 : pb); if (z > m) z = m; if (a > m) a = m; }
    if (s->minw.k == LK_VAL && s->minw.pct == 0) { float m = s->minw.px + (s->boxsizing ? 0 : pb); if (z < m) z = m; if (a < m) a = m; }
done2:
    if (z < a) z = a;
    b->minc = a;
    b->maxc = z;
    b->intr = 1;
    *mn = a;
    *mx = z;
}

typedef struct {
    box* b;
    float base, hyp, minm, maxm, target, cross;
    float mm0, mm1, mc0, mc1, mpb, cpb;
    int ma0, ma1, ca0, ca1, frozen, line;
    float pos, cpos;
} fitem;

static int ord_cmp(const void* a, const void* b) {
    const fitem* x = a;
    const fitem* y = b;
    return x->b->st->order - y->b->st->order;
}

float layout_flex(box* b, float cx, float cy, float cw, float ch) {
    style* s = b->st;
    int row = s->flexdir == FD_ROW || s->flexdir == FD_ROWREV;
    int rev = s->flexdir == FD_ROWREV || s->flexdir == FD_COLREV;
    float gmain = row ? lres(s->gcolgap, cw) : lres(s->growgap, cw);
    float gcross = row ? lres(s->growgap, cw) : lres(s->gcolgap, cw);
    if (s->gcolgap.k != LK_VAL) gmain = row ? 0 : gmain;
    float mainsz = row ? cw : ch;
    int n = 0;
    for (box* c = b->first; c; c = c->next) if (in_flow(c)) n++; else { c->staticx = cx; c->staticy = cy; add_abs(c); }
    if (n == 0) return ch >= 0 ? ch : 0;
    fitem* it = nzalloc(sizeof(fitem) * (size_t)n);
    int k = 0;
    for (box* c = b->first; c; c = c->next) if (in_flow(c)) it[k++].b = c;
    msort(it, n, sizeof(fitem), ord_cmp);
    for (int i = 0; i < n; i++) {
        fitem* f = &it[i];
        box* c = f->b;
        style* cs = c->st;
        box_edges(c, cw);
        if (row) {
            f->mpb = c->p[1] + c->p[3] + c->b[1] + c->b[3];
            f->cpb = c->p[0] + c->p[2] + c->b[0] + c->b[2];
            f->mm0 = c->m[3]; f->mm1 = c->m[1]; f->mc0 = c->m[0]; f->mc1 = c->m[2];
            f->ma0 = cs->margin[3].k == LK_AUTO; f->ma1 = cs->margin[1].k == LK_AUTO; f->ca0 = cs->margin[0].k == LK_AUTO; f->ca1 = cs->margin[2].k == LK_AUTO;
        } else {
            f->mpb = c->p[0] + c->p[2] + c->b[0] + c->b[2];
            f->cpb = c->p[1] + c->p[3] + c->b[1] + c->b[3];
            f->mm0 = c->m[0]; f->mm1 = c->m[2]; f->mc0 = c->m[3]; f->mc1 = c->m[1];
            f->ma0 = cs->margin[0].k == LK_AUTO; f->ma1 = cs->margin[2].k == LK_AUTO; f->ca0 = cs->margin[3].k == LK_AUTO; f->ca1 = cs->margin[1].k == LK_AUTO;
        }
        if (f->ma0) f->mm0 = 0;
        if (f->ma1) f->mm1 = 0;
        if (f->ca0) f->mc0 = 0;
        if (f->ca1) f->mc1 = 0;
        L msz = row ? cs->width : cs->height;
        float base;
        if (cs->basis.k == LK_VAL && !(cs->basis.pct != 0 && mainsz < 0)) {
            base = lres(cs->basis, mainsz < 0 ? 0 : mainsz);
            if (cs->boxsizing) base -= f->mpb;
        } else if (msz.k == LK_VAL && !(msz.pct != 0 && mainsz < 0)) {
            base = lres(msz, mainsz < 0 ? 0 : mainsz);
            if (cs->boxsizing) base -= f->mpb;
        } else if (row) {
            float mn, mx;
            if (c->kind == BX_REPLACED) { float h; mx = replaced_size(c, cw, ch, &h, -1); }
            else intrinsic(c, &mn, &mx);
            base = mx - f->mpb;
        } else {
            float wv;
            if (cs->width.k == LK_VAL) wv = -1;
            else {
                int al = cs->alignself == AL_AUTO ? s->alignitems : cs->alignself;
                if ((al == AL_STRETCH || al == AL_NORMAL) && !f->ca0 && !f->ca1) wv = cw - f->mc0 - f->mc1;
                else wv = shrink_fit(c, cw - f->mc0 - f->mc1);
                wv = clamp_w(c, wv, cw);
            }
            lay(c, 0, 0, cw, -1, 0, wv, -1);
            base = c->h - f->mpb;
        }
        if (base < 0) base = 0;
        f->base = base;
        float mn = 0, mx = 1e30f;
        L mnl = row ? cs->minw : cs->minh;
        L mxl = row ? cs->maxw : cs->maxh;
        if (mnl.k == LK_VAL) { mn = lres(mnl, mainsz < 0 ? 0 : mainsz); if (cs->boxsizing) mn -= f->mpb; }
        else if (row && (cs->ovx == OV_VISIBLE || cs->ovx == OV_CLIP) && c->kind != BX_REPLACED) {
            float a, z;
            intrinsic(c, &a, &z);
            mn = a - f->mpb;
            if (msz.k == LK_VAL && msz.pct == 0) { float sz = msz.px - (cs->boxsizing ? f->mpb : 0); if (sz < mn) mn = sz; }
        }
        if (mxl.k == LK_VAL && !(mxl.pct != 0 && mainsz < 0)) { mx = lres(mxl, mainsz < 0 ? 0 : mainsz); if (cs->boxsizing) mx -= f->mpb; }
        if (mn < 0) mn = 0;
        f->minm = mn;
        f->maxm = mx;
        f->hyp = base < mn ? mn : base > mx ? mx : base;
    }
    int nlines = 1;
    int* lstart = nalloc(sizeof(int) * (size_t)(n + 1));
    lstart[0] = 0;
    if (s->flexwrap && mainsz >= 0) {
        float used = 0;
        int cnt = 0;
        for (int i = 0; i < n; i++) {
            float o = it[i].hyp + it[i].mpb + it[i].mm0 + it[i].mm1;
            if (cnt > 0 && used + gmain + o > mainsz + 0.01f) { lstart[nlines++] = i; used = o; cnt = 1; }
            else { used += (cnt ? gmain : 0) + o; cnt++; }
        }
    }
    lstart[nlines] = n;
    float* lcross = nalloc(sizeof(float) * (size_t)nlines);
    float* lused = nalloc(sizeof(float) * (size_t)nlines);
    float maxmainused = 0;
    for (int li = 0; li < nlines; li++) {
        int a0 = lstart[li], a1 = lstart[li + 1];
        for (int i = a0; i < a1; i++) { it[i].line = li; it[i].frozen = 0; it[i].target = it[i].hyp; }
        if (mainsz >= 0) {
            float sumhyp = 0;
            for (int i = a0; i < a1; i++) sumhyp += it[i].hyp + it[i].mpb + it[i].mm0 + it[i].mm1;
            float gaps = gmain * (a1 - a0 - 1);
            int grow = mainsz - gaps - sumhyp > 0;
            for (int i = a0; i < a1; i++) {
                style* cs = it[i].b->st;
                float fac = grow ? cs->grow : cs->shrink;
                if (fac == 0 || (grow && it[i].base > it[i].hyp) || (!grow && it[i].base < it[i].hyp)) { it[i].frozen = 1; it[i].target = it[i].hyp; }
            }
            for (int iter = 0; iter < 10; iter++) {
                float used = gaps, sumf = 0, sums = 0;
                int unf = 0;
                for (int i = a0; i < a1; i++) {
                    fitem* f = &it[i];
                    if (f->frozen) used += f->target + f->mpb + f->mm0 + f->mm1;
                    else { used += f->base + f->mpb + f->mm0 + f->mm1; sumf += grow ? f->b->st->grow : f->b->st->shrink; sums += f->b->st->shrink * f->base; unf++; }
                }
                if (!unf) break;
                float freesp = mainsz - used;
                if (grow && sumf < 1) { float alt = 0; for (int i = a0; i < a1; i++) if (!it[i].frozen) alt += it[i].b->st->grow; freesp = freesp * (alt < 1 ? alt : 1); }
                float viol = 0;
                for (int i = a0; i < a1; i++) {
                    fitem* f = &it[i];
                    if (f->frozen) continue;
                    float t;
                    if (grow) t = f->base + (sumf > 0 ? freesp * f->b->st->grow / sumf : 0);
                    else t = f->base + (sums > 0 ? freesp * (f->b->st->shrink * f->base) / sums : 0);
                    float cl = t < f->minm ? f->minm : t > f->maxm ? f->maxm : t;
                    viol += cl - t;
                    f->target = cl;
                }
                if (fabsf(viol) < 0.01f) { for (int i = a0; i < a1; i++) it[i].frozen = 1; break; }
                for (int i = a0; i < a1; i++) {
                    fitem* f = &it[i];
                    if (f->frozen) continue;
                    if (viol > 0 && f->target == f->minm) f->frozen = 1;
                    if (viol < 0 && f->target == f->maxm) f->frozen = 1;
                }
            }
        }
        float lc = 0;
        float used = 0;
        for (int i = a0; i < a1; i++) {
            fitem* f = &it[i];
            box* c = f->b;
            style* cs = c->st;
            float mainB = f->target + f->mpb;
            if (row) {
                float fh = -1;
                lay(c, 0, 0, cw, ch, 0, mainB, fh);
                f->cross = c->h;
            } else {
                float wv;
                int al = cs->alignself == AL_AUTO ? s->alignitems : cs->alignself;
                if (cs->width.k == LK_VAL) wv = clamp_w(c, lres(cs->width, cw) + (cs->boxsizing ? 0 : f->cpb), cw);
                else if ((al == AL_STRETCH || al == AL_NORMAL) && !f->ca0 && !f->ca1) wv = clamp_w(c, cw - f->mc0 - f->mc1, cw);
                else wv = clamp_w(c, shrink_fit(c, cw - f->mc0 - f->mc1), cw);
                lay(c, 0, 0, cw, mainsz, 0, wv, mainB);
                f->cross = c->w;
            }
            float oc = f->cross + f->mc0 + f->mc1;
            if (oc > lc) lc = oc;
            used += mainB + f->mm0 + f->mm1 + (i > a0 ? gmain : 0);
        }
        lcross[li] = lc;
        lused[li] = used;
        if (used > maxmainused) maxmainused = used;
    }
    float crosssz = row ? ch : cw;
    if (nlines == 1 && crosssz >= 0) lcross[0] = crosssz;
    float totalcross = 0;
    for (int li = 0; li < nlines; li++) totalcross += lcross[li] + (li ? gcross : 0);
    float cstart = 0, cbetween = gcross;
    if (nlines > 1 && crosssz >= 0) {
        float freec = crosssz - totalcross;
        int ac = s->aligncontent;
        if (ac == AL_STRETCH || ac == AL_NORMAL) { if (freec > 0) for (int li = 0; li < nlines; li++) lcross[li] += freec / nlines; }
        else if (ac == AL_CENTER) cstart = freec / 2;
        else if (ac == AL_END) cstart = freec;
        else if (ac == AL_BETWEEN && nlines > 1) cbetween += freec > 0 ? freec / (nlines - 1) : 0;
        else if (ac == AL_AROUND) { cstart = freec / nlines / 2; cbetween += freec / nlines; }
        else if (ac == AL_EVENLY) { cstart = freec / (nlines + 1); cbetween += freec / (nlines + 1); }
    }
    float mainlen = mainsz >= 0 ? mainsz : maxmainused;
    float cpos = cstart;
    for (int li = 0; li < nlines; li++) {
        int lidx = s->flexwrap == 2 ? nlines - 1 - li : li;
        int a0 = lstart[lidx], a1 = lstart[lidx + 1];
        float freem = mainlen - lused[lidx];
        int nauto = 0;
        for (int i = a0; i < a1; i++) nauto += it[i].ma0 + it[i].ma1;
        float start = 0, between = gmain, autosz = 0;
        if (nauto > 0 && freem > 0) autosz = freem / nauto;
        else {
            int jc = s->justify;
            int cnt = a1 - a0;
            if (jc == AL_CENTER) start = freem / 2;
            else if (jc == AL_END) start = freem;
            else if (jc == AL_BETWEEN) { if (cnt > 1 && freem > 0) between += freem / (cnt - 1); }
            else if (jc == AL_AROUND) { if (freem > 0) { start = freem / cnt / 2; between += freem / cnt; } }
            else if (jc == AL_EVENLY) { if (freem > 0) { start = freem / (cnt + 1); between += freem / (cnt + 1); } }
            if (rev && (jc == AL_START || jc == AL_NORMAL || jc == AL_AUTO)) start = freem;
            if (rev && jc == AL_END) start = 0;
        }
        float mpos = start;
        for (int q = 0; q < a1 - a0; q++) {
            int i = rev ? a1 - 1 - q : a0 + q;
            fitem* f = &it[i];
            box* c = f->b;
            style* cs = c->st;
            float m0 = f->ma0 ? autosz : f->mm0, m1 = f->ma1 ? autosz : f->mm1;
            if (rev) { float t = m0; m0 = m1; m1 = t; }
            float mainB = f->target + f->mpb;
            float itemmain = mpos + m0;
            mpos += m0 + mainB + m1 + between;
            int al = cs->alignself == AL_AUTO ? s->alignitems : cs->alignself;
            if (al == AL_NORMAL) al = AL_STRETCH;
            float lc = lcross[lidx];
            float crossB = f->cross;
            L csz = row ? cs->height : cs->width;
            if (al == AL_STRETCH && csz.k != LK_VAL && !f->ca0 && !f->ca1) {
                float t = lc - f->mc0 - f->mc1;
                if (row) {
                    float th = clamp_h(c, t - f->cpb, -1) + f->cpb;
                    if (th > crossB + 0.5f || th < crossB - 0.5f) {
                        if (c->kind == BX_FLEX || c->kind == BX_GRID || (c->first && c->first->kind == BX_FLEX)) { lay(c, 0, 0, cw, ch, 0, mainB, th); }
                        else c->h = th;
                        crossB = th;
                    }
                } else {
                    float tw = clamp_w(c, t, cw);
                    if (tw != crossB) { lay(c, 0, 0, cw, mainsz, 0, tw, mainB); crossB = tw; }
                }
            }
            float cfree = lc - crossB - f->mc0 - f->mc1;
            float co = f->mc0;
            if (f->ca0 && f->ca1) co = cfree / 2 + 0;
            else if (f->ca0) co = cfree + f->mc0;
            else if (f->ca1) co = f->mc0;
            else if (al == AL_CENTER) co = f->mc0 + cfree / 2;
            else if (al == AL_END) co = f->mc0 + cfree;
            if (s->flexwrap == 2 && al != AL_CENTER && al != AL_STRETCH) co = f->mc0 + cfree - (co - f->mc0);
            float fx, fy;
            if (row) { fx = cx + itemmain; fy = cy + cpos + co; }
            else { fx = cx + cpos + co; fy = cy + itemmain; }
            shift_box(c, fx - c->x, fy - c->y);
        }
        cpos += lcross[lidx] + cbetween;
    }
    if (row) return ch >= 0 ? ch : totalcross;
    return ch >= 0 ? ch : maxmainused;
}

static void expand_tracks(style* s, float cw, gtrack** out, int* nout) {
    if (s->ngcols == 0) { gtrack* g = nalloc(sizeof(gtrack)); g->k = GT_AUTO; g->mink = GT_AUTO; g->v = g->minv = 0; *out = g; *nout = 1; return; }
    int hasrep = 0;
    for (int i = 0; i < s->ngcols; i++) if (s->gcols[i].mink & 0x80) hasrep = 1;
    if (!hasrep) { *out = s->gcols; *nout = s->ngcols; return; }
    float gap = lres(s->gcolgap, cw);
    float fixed = 0, repw = 0;
    int nrep = 0;
    for (int i = 0; i < s->ngcols; i++) {
        gtrack g = s->gcols[i];
        int rep = g.mink & 0x80;
        int mk = g.mink & 0x7F;
        float w = g.k == GT_PX ? g.v : mk == GT_PX ? g.minv : g.k == GT_PCT ? g.v * cw / 100 : mk == GT_PCT ? g.minv * cw / 100 : 0;
        if (rep) { repw += w; nrep++; } else fixed += w + gap;
    }
    int cnt = 1;
    if (repw > 0) cnt = (int)((cw - fixed + gap) / (repw + gap * nrep));
    if (cnt < 1) cnt = 1;
    if (cnt > 64) cnt = 64;
    gtrack* g = nalloc(sizeof(gtrack) * (size_t)(s->ngcols + nrep * cnt));
    int k = 0;
    for (int i = 0; i < s->ngcols; i++) {
        if (s->gcols[i].mink & 0x80) {
            int st = i;
            while (i < s->ngcols && (s->gcols[i].mink & 0x80)) i++;
            for (int r = 0; r < cnt; r++) for (int j = st; j < i; j++) { g[k] = s->gcols[j]; g[k].mink &= 0x7F; k++; }
            i--;
        } else g[k++] = s->gcols[i];
    }
    *out = g;
    *nout = k;
}

typedef struct { box* b; int r0, c0, rs, cs; } gitem;
static int span_of(int v) { return v <= -1000 ? -1000 - v : 0; }

float layout_grid(box* b, float cx, float cy, float cw, float ch) {
    style* s = b->st;
    gtrack* cols;
    int nc;
    expand_tracks(s, cw, &cols, &nc);
    float cgap = lres(s->gcolgap, cw), rgap = lres(s->growgap, cw);
    int n = 0;
    for (box* c = b->first; c; c = c->next) if (in_flow(c)) n++; else { c->staticx = cx; c->staticy = cy; add_abs(c); }
    if (n == 0) return ch >= 0 ? ch : 0;
    gitem* it = nzalloc(sizeof(gitem) * (size_t)n);
    int k = 0;
    for (box* c = b->first; c; c = c->next) if (in_flow(c)) { it[k].b = c; k++; }
    for (int i = 1; i < n; i++) { gitem t = it[i]; int j = i - 1; while (j >= 0 && it[j].b->st->order > t.b->st->order) { it[j + 1] = it[j]; j--; } it[j + 1] = t; }
    int col_flow = (s->gautoflow & 1) != 0;
    int maxr = 2048;
    int ncap = nc;
    unsigned char* occ = nzalloc((size_t)maxr * (size_t)ncap);
    int nr = s->ngrows;
    int cr = 0, cc = 0;
    for (int i = 0; i < n; i++) {
        style* cs = it[i].b->st;
        int c0 = -1, csp = 1, r0 = -1, rsp = 1;
        if (cs->gcs > 0) c0 = cs->gcs - 1; else if (cs->gcs < 0 && cs->gcs > -1000) c0 = nc + 1 + cs->gcs;
        if (cs->gcs <= -1000) csp = span_of(cs->gcs);
        if (cs->gce > 0 && c0 >= 0) csp = cs->gce - 1 - c0; else if (cs->gce <= -1000) csp = span_of(cs->gce); else if (cs->gce < 0 && cs->gce > -1000 && c0 >= 0) csp = nc + 1 + cs->gce - c0;
        else if (cs->gce > 0 && c0 < 0) { c0 = cs->gce - 1 - csp; if (c0 < 0) c0 = 0; }
        if (cs->grs > 0) r0 = cs->grs - 1; else if (cs->grs < 0 && cs->grs > -1000) r0 = (nr > 0 ? nr : 1) + 1 + cs->grs;
        if (cs->grs <= -1000) rsp = span_of(cs->grs);
        if (cs->gre > 0 && r0 >= 0) rsp = cs->gre - 1 - r0; else if (cs->gre <= -1000) rsp = span_of(cs->gre); else if (cs->gre < 0 && cs->gre > -1000 && r0 >= 0) rsp = (nr > 0 ? nr : 1) + 1 + cs->gre - r0;
        if (csp < 1) csp = 1;
        if (rsp < 1) rsp = 1;
        if (csp > nc) csp = nc;
        if (c0 >= nc) c0 = nc - 1;
        if (c0 >= 0 && c0 + csp > nc) csp = nc - c0;
        if (r0 < 0 || c0 < 0) {
            int found = 0;
            int rr = r0 >= 0 ? r0 : (c0 >= 0 ? 0 : cr);
            int ccs = c0 >= 0 ? c0 : (r0 >= 0 ? 0 : cc);
            for (int guard = 0; guard < maxr * nc && !found; guard++) {
                if (rr + rsp > maxr) break;
                int ok = ccs + csp <= nc;
                for (int y = rr; ok && y < rr + rsp; y++) for (int x = ccs; ok && x < ccs + csp; x++) if (occ[y * ncap + x]) ok = 0;
                if (ok) { found = 1; break; }
                if (col_flow && c0 < 0 && r0 < 0) { rr++; if (rr + rsp > (nr > 0 ? nr : 1)) { rr = 0; ccs++; if (ccs + csp > nc) { ccs = 0; rr = nr > 0 ? nr : 1; } } }
                else if (c0 >= 0) rr++;
                else { ccs++; if (ccs + csp > nc) { ccs = 0; if (r0 >= 0) break; rr++; } }
            }
            if (!found) { rr = rr < maxr - rsp ? rr : maxr - rsp; }
            r0 = rr;
            c0 = ccs;
            if (!(s->gautoflow & 2) && !col_flow) { cr = r0; cc = c0 + csp; if (cc >= nc) { cc = 0; cr++; } }
        }
        if (r0 + rsp > maxr) r0 = maxr - rsp;
        for (int y = r0; y < r0 + rsp; y++) for (int x = c0; x < c0 + csp && x < nc; x++) occ[y * ncap + x] = 1;
        it[i].r0 = r0; it[i].c0 = c0; it[i].rs = rsp; it[i].cs = csp;
    }
    int rows = nr;
    for (int i = 0; i < n; i++) if (it[i].r0 + it[i].rs > rows) rows = it[i].r0 + it[i].rs;
    if (rows < 1) rows = 1;
    float* cwid = nzalloc(sizeof(float) * (size_t)nc);
    float avail = cw - cgap * (nc - 1);
    float used = 0, frsum = 0;
    float* cmn = nzalloc(sizeof(float) * (size_t)nc);
    float* cmx = nzalloc(sizeof(float) * (size_t)nc);
    for (int i = 0; i < n; i++) {
        if (it[i].cs != 1) continue;
        float a, z;
        intrinsic(it[i].b, &a, &z);
        float mg = lres(it[i].b->st->margin[1], cw) + lres(it[i].b->st->margin[3], cw);
        int c = it[i].c0;
        if (a + mg > cmn[c]) cmn[c] = a + mg;
        if (z + mg > cmx[c]) cmx[c] = z + mg;
    }
    for (int c = 0; c < nc; c++) {
        gtrack g = cols[c];
        if (g.k == GT_PX) cwid[c] = g.v;
        else if (g.k == GT_PCT) cwid[c] = g.v * cw / 100;
        else if (g.k == GT_FR) { frsum += g.v; cwid[c] = 0; continue; }
        else if (g.k == GT_MINC) cwid[c] = cmn[c];
        else cwid[c] = cmx[c];
        if ((g.mink & 0x7F) == GT_PX && cwid[c] < g.minv) cwid[c] = g.minv;
        used += cwid[c];
    }
    if (used > avail) {
        float over = used - avail, shrinkable = 0;
        for (int c = 0; c < nc; c++) if (cols[c].k == GT_AUTO || cols[c].k == GT_MAXC) shrinkable += cwid[c] - cmn[c];
        if (shrinkable > 0) for (int c = 0; c < nc; c++) if (cols[c].k == GT_AUTO || cols[c].k == GT_MAXC) { float d = (cwid[c] - cmn[c]) * (over < shrinkable ? over / shrinkable : 1); cwid[c] -= d; used -= d; }
    }
    if (frsum > 0) {
        float left = avail - used;
        if (left < 0) left = 0;
        float unit = frsum >= 1 ? left / frsum : left;
        for (int pass = 0; pass < 3; pass++) {
            float fixedpart = 0, fsum = 0;
            for (int c = 0; c < nc; c++) if (cols[c].k == GT_FR) {
                float mn = (cols[c].mink & 0x7F) == GT_PX ? cols[c].minv : (cols[c].mink & 0x7F) == GT_AUTO ? cmn[c] : 0;
                if (cols[c].v * unit < mn) fixedpart += mn; else fsum += cols[c].v;
            }
            float nu = fsum > 0 ? (left - fixedpart) / (fsum >= 1 ? fsum : 1) : unit;
            if (nu < 0) nu = 0;
            if (fabsf(nu - unit) < 0.01f) break;
            unit = nu;
        }
        for (int c = 0; c < nc; c++) if (cols[c].k == GT_FR) {
            float mn = (cols[c].mink & 0x7F) == GT_PX ? cols[c].minv : (cols[c].mink & 0x7F) == GT_AUTO ? cmn[c] : 0;
            cwid[c] = cols[c].v * unit;
            if (cwid[c] < mn) cwid[c] = mn;
        }
    } else if (used < avail && (s->justify == AL_STRETCH || s->justify == AL_NORMAL)) {
        int nauto = 0;
        for (int c = 0; c < nc; c++) if (cols[c].k == GT_AUTO) nauto++;
        if (nauto) for (int c = 0; c < nc; c++) if (cols[c].k == GT_AUTO) cwid[c] += (avail - used) / nauto;
    }
    float* cx0 = nalloc(sizeof(float) * (size_t)(nc + 1));
    float acc = 0;
    float totalw = 0;
    for (int c = 0; c < nc; c++) totalw += cwid[c] + (c ? cgap : 0);
    float jstart = 0, jbetween = 0;
    if (totalw < cw && frsum == 0) {
        float fr = cw - totalw;
        if (s->justify == AL_CENTER) jstart = fr / 2;
        else if (s->justify == AL_END) jstart = fr;
        else if (s->justify == AL_BETWEEN && nc > 1) jbetween = fr / (nc - 1);
        else if (s->justify == AL_AROUND) { jstart = fr / nc / 2; jbetween = fr / nc; }
        else if (s->justify == AL_EVENLY) { jstart = fr / (nc + 1); jbetween = fr / (nc + 1); }
    }
    acc = jstart;
    for (int c = 0; c < nc; c++) { cx0[c] = acc; acc += cwid[c] + cgap + jbetween; }
    cx0[nc] = acc;
    float* rh = nzalloc(sizeof(float) * (size_t)rows);
    int* rfix = nzalloc(sizeof(int) * (size_t)rows);
    float rfr = 0;
    for (int r = 0; r < rows; r++) {
        gtrack g = r < s->ngrows ? s->grows[r] : s->gautorow;
        if (g.k == GT_PX) { rh[r] = g.v; rfix[r] = 1; }
        else if (g.k == GT_PCT && ch >= 0) { rh[r] = g.v * ch / 100; rfix[r] = 1; }
        else if (g.k == GT_FR) { rfr += g.v; rfix[r] = 2; }
        if ((g.mink & 0x7F) == GT_PX && rh[r] < g.minv) rh[r] = g.minv;
    }
    for (int i = 0; i < n; i++) {
        box* c = it[i].b;
        style* cs = c->st;
        float aw = cx0[it[i].c0 + it[i].cs - 1] + cwid[it[i].c0 + it[i].cs - 1] - cx0[it[i].c0];
        box_edges(c, aw);
        int js = cs->justifyself == AL_AUTO ? s->justifyitems : cs->justifyself;
        float w;
        if (c->kind == BX_REPLACED) { float h; w = replaced_size(c, aw, -1, &h, -1); }
        else if (cs->width.k == LK_VAL) w = clamp_w(c, lres(cs->width, aw) + (cs->boxsizing ? 0 : c->p[1] + c->p[3] + c->b[1] + c->b[3]), aw);
        else if (js == AL_START || js == AL_END || js == AL_CENTER) w = clamp_w(c, shrink_fit(c, aw - c->m[1] - c->m[3]), aw);
        else w = clamp_w(c, aw - c->m[1] - c->m[3], aw);
        lay(c, 0, 0, aw, -1, 0, w, -1);
        float oh = c->h + c->m[0] + c->m[2];
        if (it[i].rs == 1) { int r = it[i].r0; if (rfix[r] != 1 && oh > rh[r]) rh[r] = oh; }
    }
    for (int i = 0; i < n; i++) {
        if (it[i].rs <= 1) continue;
        box* c = it[i].b;
        float oh = c->h + c->m[0] + c->m[2];
        float have = rgap * (it[i].rs - 1);
        for (int r = it[i].r0; r < it[i].r0 + it[i].rs && r < rows; r++) have += rh[r];
        if (oh > have) { float add = (oh - have) / it[i].rs; for (int r = it[i].r0; r < it[i].r0 + it[i].rs && r < rows; r++) if (rfix[r] != 1) rh[r] += add; }
    }
    if (rfr > 0 && ch >= 0) {
        float usedh = rgap * (rows - 1);
        for (int r = 0; r < rows; r++) if (rfix[r] != 2) usedh += rh[r];
        float left = ch - usedh;
        if (left > 0) for (int r = 0; r < rows; r++) if (rfix[r] == 2) { gtrack g = r < s->ngrows ? s->grows[r] : s->gautorow; float v = left * g.v / rfr; if (v > rh[r]) rh[r] = v; }
    }
    if (ch >= 0 && rows > 0 && (s->aligncontent == AL_NORMAL || s->aligncontent == AL_STRETCH)) {
        float usedh = rgap * (rows - 1);
        int nauto = 0;
        for (int r = 0; r < rows; r++) { usedh += rh[r]; gtrack g = r < s->ngrows ? s->grows[r] : s->gautorow; if (rfix[r] == 0 && g.k == GT_AUTO) nauto++; }
        float left = ch - usedh;
        if (left > 0.5f && nauto > 0) for (int r = 0; r < rows; r++) { gtrack g = r < s->ngrows ? s->grows[r] : s->gautorow; if (rfix[r] == 0 && g.k == GT_AUTO) rh[r] += left / nauto; }
    }
    float* ry0 = nalloc(sizeof(float) * (size_t)(rows + 1));
    acc = 0;
    for (int r = 0; r < rows; r++) { ry0[r] = acc; acc += rh[r] + rgap; }
    float totalh = rows ? acc - rgap : 0;
    for (int i = 0; i < n; i++) {
        box* c = it[i].b;
        style* cs = c->st;
        float ax = cx0[it[i].c0], aw = cx0[it[i].c0 + it[i].cs - 1] + cwid[it[i].c0 + it[i].cs - 1] - ax;
        int rl = it[i].r0 + it[i].rs - 1;
        if (rl >= rows) rl = rows - 1;
        float ay = ry0[it[i].r0], ah = ry0[rl] + rh[rl] - ay;
        int js = cs->justifyself == AL_AUTO ? s->justifyitems : cs->justifyself;
        int as = cs->alignself == AL_AUTO ? s->alignitems : cs->alignself;
        float x = ax + c->m[3];
        if (cs->margin[3].k == LK_AUTO && cs->margin[1].k == LK_AUTO) x = ax + (aw - c->w) / 2;
        else if (js == AL_CENTER) x = ax + (aw - c->w - c->m[1] - c->m[3]) / 2 + c->m[3];
        else if (js == AL_END) x = ax + aw - c->w - c->m[1];
        if ((as == AL_STRETCH || as == AL_NORMAL) && cs->height.k != LK_VAL && c->kind != BX_REPLACED) {
            float th = clamp_h(c, ah - c->m[0] - c->m[2] - c->p[0] - c->p[2] - c->b[0] - c->b[2], -1) + c->p[0] + c->p[2] + c->b[0] + c->b[2];
            if (th > c->h + 0.5f) {
                if (c->kind == BX_FLEX || c->kind == BX_GRID) lay(c, 0, 0, aw, ah, 0, c->w, th);
                else c->h = th;
            }
        }
        float y = ay + c->m[0];
        if (as == AL_CENTER) y = ay + (ah - c->h - c->m[0] - c->m[2]) / 2 + c->m[0];
        else if (as == AL_END) y = ay + ah - c->h - c->m[2];
        if (cs->margin[0].k == LK_AUTO && cs->margin[2].k == LK_AUTO) y = ay + (ah - c->h) / 2;
        shift_box(c, cx + x - c->x, cy + y - c->y);
    }
    return ch >= 0 ? ch : totalh;
}

typedef struct { box* cell; int r, c, rs, cs; } tcell;
typedef struct { box** rows; int nrows; tcell* cells; int ncells; int ncols; } tgrid;

static void table_grid(box* t, tgrid* g) {
    box* rowlist[4096];
    int nr = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (box* grp = t->first; grp; grp = grp->next) {
            if (grp->kind != BX_ROWGROUP) continue;
            int d = grp->st->display;
            int want = d == D_THEADG ? 0 : d == D_TFOOTG ? 2 : 1;
            if (want != pass) continue;
            for (box* r = grp->first; r; r = r->next) if (nr < 4096) rowlist[nr++] = r;
        }
    }
    g->rows = nalloc(sizeof(box*) * (size_t)(nr ? nr : 1));
    memcpy(g->rows, rowlist, sizeof(box*) * (size_t)nr);
    g->nrows = nr;
    int total = 0;
    for (int r = 0; r < nr; r++) for (box* c = rowlist[r]->first; c; c = c->next) total++;
    g->cells = nalloc(sizeof(tcell) * (size_t)(total ? total : 1));
    g->ncells = 0;
    int maxc = 256;
    unsigned char* occ = nzalloc((size_t)(nr + 1) * (size_t)maxc);
    int ncols = 0;
    for (int r = 0; r < nr; r++) {
        int c = 0;
        for (box* cell = rowlist[r]->first; cell; cell = cell->next) {
            while (c < maxc && occ[r * maxc + c]) c++;
            if (c >= maxc) break;
            int cs = cell->st->colspan > 0 ? cell->st->colspan : 1;
            int rs = cell->st->rowspan > 0 ? cell->st->rowspan : 1;
            if (rs > nr - r) rs = nr - r;
            if (rs < 1) rs = 1;
            if (c + cs > maxc) cs = maxc - c;
            for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) occ[y * maxc + x] = 1;
            tcell* tc = &g->cells[g->ncells++];
            tc->cell = cell; tc->r = r; tc->c = c; tc->rs = rs; tc->cs = cs;
            c += cs;
            if (c > ncols) ncols = c;
        }
    }
    g->ncols = ncols;
}

static void table_cols(box* t, int* ncols_out, float** mins, float** maxs, float** pcts) {
    tgrid g;
    table_grid(t, &g);
    int nc = g.ncols;
    float* mn = nzalloc(sizeof(float) * (size_t)(nc + 1));
    float* mx = nzalloc(sizeof(float) * (size_t)(nc + 1));
    float* pc = nzalloc(sizeof(float) * (size_t)(nc + 1));
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g.ncells; i++) {
            tcell* tc = &g.cells[i];
            if ((pass == 0) != (tc->cs == 1)) continue;
            float a, z;
            box_edges(tc->cell, 0);
            intrinsic(tc->cell, &a, &z);
            style* cs = tc->cell->st;
            if (cs->width.k == LK_VAL && cs->width.pct != 0 && tc->cs == 1) { if (cs->width.pct > pc[tc->c]) pc[tc->c] = cs->width.pct; }
            if (tc->cs == 1) {
                if (a > mn[tc->c]) mn[tc->c] = a;
                if (z > mx[tc->c]) mx[tc->c] = z;
            } else {
                float smn = 0, smx = 0;
                for (int c = tc->c; c < tc->c + tc->cs; c++) { smn += mn[c]; smx += mx[c]; }
                if (a > smn) { float add = (a - smn) / tc->cs; for (int c = tc->c; c < tc->c + tc->cs; c++) mn[c] += add; }
                if (z > smx) { for (int c = tc->c; c < tc->c + tc->cs; c++) { float share = smx > 0 ? mx[c] / smx : 1.0f / tc->cs; mx[c] += (z - smx) * share; } }
            }
        }
    }
    for (int c = 0; c < nc; c++) if (mx[c] < mn[c]) mx[c] = mn[c];
    *ncols_out = nc;
    *mins = mn;
    *maxs = mx;
    *pcts = pc;
}

float layout_table(box* b, float cx, float cy, float cw, float ch) {
    style* s = b->st;
    float sx = s->bcollapse ? 0 : s->bspacingx, sy = s->bcollapse ? 0 : s->bspacingy;
    float y = cy;
    for (box* cap = b->first; cap; cap = cap->next) {
        if (cap->kind != BX_CAPTION) continue;
        box_edges(cap, cw);
        lay(cap, cx + cap->m[3], y + cap->m[0], cw, -1, 0, cw - cap->m[1] - cap->m[3], -1);
        y = cap->y + cap->h + cap->m[2];
    }
    tgrid g;
    table_grid(b, &g);
    int nc;
    float *mn, *mx, *pc;
    table_cols(b, &nc, &mn, &mx, &pc);
    if (nc == 0) return y - cy;
    float total = cw - sx * (nc + 1);
    if (total < 0) total = 0;
    float* w = nzalloc(sizeof(float) * (size_t)nc);
    float summn = 0, summx = 0, pcsum = 0;
    for (int c = 0; c < nc; c++) { summn += mn[c]; summx += mx[c]; pcsum += pc[c]; }
    if (pcsum > 0) {
        float rest = total;
        float nmn = 0, nmx = 0;
        for (int c = 0; c < nc; c++) if (pc[c] > 0) { w[c] = pc[c] * total / 100; if (w[c] < mn[c]) w[c] = mn[c]; rest -= w[c]; } else { nmn += mn[c]; nmx += mx[c]; }
        for (int c = 0; c < nc; c++) if (pc[c] == 0) {
            if (rest >= nmx) w[c] = nmx > 0 ? mx[c] + (rest - nmx) * mx[c] / nmx : rest / nc;
            else if (rest >= nmn && nmx > nmn) w[c] = mn[c] + (mx[c] - mn[c]) * (rest - nmn) / (nmx - nmn);
            else w[c] = mn[c];
        }
    } else if (total >= summx) {
        float extra = total - summx;
        int nz = 0;
        for (int c = 0; c < nc; c++) if (mx[c] > 0) nz++;
        for (int c = 0; c < nc; c++) w[c] = mx[c] + (summx > 0 ? extra * mx[c] / summx : extra / nc);
        (void)nz;
    } else if (total >= summn && summx > summn) {
        for (int c = 0; c < nc; c++) w[c] = mn[c] + (mx[c] - mn[c]) * (total - summn) / (summx - summn);
    } else {
        for (int c = 0; c < nc; c++) w[c] = mn[c];
    }
    float* x0 = nalloc(sizeof(float) * (size_t)(nc + 1));
    float acc = cx + sx;
    for (int c = 0; c < nc; c++) { x0[c] = acc; acc += w[c] + sx; }
    x0[nc] = acc;
    float* rh = nzalloc(sizeof(float) * (size_t)(g.nrows + 1));
    for (int r = 0; r < g.nrows; r++) {
        box* row = g.rows[r];
        if (row->st->height.k == LK_VAL && row->st->height.pct == 0) rh[r] = row->st->height.px;
    }
    for (int i = 0; i < g.ncells; i++) {
        tcell* tc = &g.cells[i];
        float cwid = x0[tc->c + tc->cs - 1] + w[tc->c + tc->cs - 1] - x0[tc->c];
        lay(tc->cell, x0[tc->c], 0, cwid, -1, 0, cwid, -1);
        if (tc->rs == 1 && tc->cell->h > rh[tc->r]) rh[tc->r] = tc->cell->h;
    }
    for (int i = 0; i < g.ncells; i++) {
        tcell* tc = &g.cells[i];
        if (tc->rs <= 1) continue;
        float have = sy * (tc->rs - 1);
        for (int r = tc->r; r < tc->r + tc->rs; r++) have += rh[r];
        if (tc->cell->h > have) { float add = (tc->cell->h - have) / tc->rs; for (int r = tc->r; r < tc->r + tc->rs; r++) rh[r] += add; }
    }
    float* ry = nalloc(sizeof(float) * (size_t)(g.nrows + 1));
    y += sy;
    for (int r = 0; r < g.nrows; r++) { ry[r] = y; y += rh[r] + sy; }
    for (int r = 0; r < g.nrows; r++) {
        box* row = g.rows[r];
        row->x = cx + sx;
        row->y = ry[r];
        row->w = acc - cx - 2 * sx;
        row->h = rh[r];
        if (row->w < 0) row->w = 0;
    }
    for (box* grp = b->first; grp; grp = grp->next) {
        if (grp->kind != BX_ROWGROUP) continue;
        float t0 = 1e30f, t1 = -1e30f;
        for (box* r = grp->first; r; r = r->next) { if (r->y < t0) t0 = r->y; if (r->y + r->h > t1) t1 = r->y + r->h; }
        if (t0 < t1) { grp->x = cx + sx; grp->y = t0; grp->w = acc - cx - 2 * sx; grp->h = t1 - t0; }
    }
    for (int i = 0; i < g.ncells; i++) {
        tcell* tc = &g.cells[i];
        box* cell = tc->cell;
        int rl = tc->r + tc->rs - 1;
        float hh = ry[rl] + rh[rl] - ry[tc->r];
        float contenth = cell->h;
        shift_box(cell, 0, ry[tc->r] - cell->y);
        float slack = hh - contenth;
        int va = cell->st->valign;
        float dy = va == VA_MIDDLE ? slack / 2 : va == VA_BOTTOM ? slack : 0;
        if (va == VA_BASELINE) dy = 0;
        if (dy > 0) {
            for (int k = 0; k < cell->nfr; k++) { cell->fr[k].y += dy; cell->fr[k].base += dy; }
            for (box* c = cell->first; c; c = c->next) {
                if (c->kind == BX_TEXT || c->kind == BX_BR) continue;
                if (c->st && (c->st->position == P_ABSOLUTE || c->st->position == P_FIXED)) { c->staticy += dy; continue; }
                shift_box(c, 0, dy);
            }
            if (cell->firstbase >= 0) cell->firstbase += dy;
        }
        cell->h = hh;
    }
    if (ch >= 0 && y - cy < ch) {
        float extra = ch - (y - cy);
        (void)extra;
        return ch;
    }
    return y - cy;
}
