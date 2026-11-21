/* glr_dosrec.c - see glr_dosrec.h. world.frag's (and world_common.glsl's) per-polygon arithmetic of the DOS fill, line
 * for line: the integers wrap at 32 bits as the shader's do; the floats are single precision, rounded where the shader's
 * are (no contraction here - the one product the reference driver fuses, dosrec_dot, is fused by hand). The shader's
 * 64-bit quotients (dos_muldiv, i64_div: a float estimate and a few correction steps) are exact while the quotient is
 * small; beyond 2^24 - where the estimate might not settle - and for a conversion GLSL leaves undefined the polygon is
 * not recorded (left to the per-pixel fill, which keeps whatever its GPU gives). */
#include "glr_dosrec.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __clang__
#pragma STDC FP_CONTRACT OFF   /* (GCC: off already in ISO C mode, -std=c99) */
#endif

#define FPV 13
#define DOS_PIT_NEAR 32
#define QLIM (1u << 24)

typedef struct { const dosrec_view *V; const float *vf; int nv, bad; } dctx;

static int32_t wadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t wsub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t wmul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t wshl(int32_t a, int s) { return (int32_t)((uint32_t)a << s); }
static int32_t wneg(uint32_t q) { return (int32_t)(0u - q); }
static uint32_t uabs(int32_t a) { return a < 0 ? 0u - (uint32_t)a : (uint32_t)a; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

static float dvtx(dctx *X, int i)
{
    if (i < 0 || i >= X->nv * FPV) { X->bad = 1; return 0.0f; }
    return X->vf[i];
}
/* int(v) / uint(v): GLSL leaves an out-of-range conversion undefined */
static int32_t f2i(dctx *X, float v)
{
    if (!(v > -2147483904.0f && v < 2147483648.0f)) { X->bad = 1; return 0; }
    return (int32_t)v;
}
static float rnd(float v) { return floorf(v + 0.5f); }
/* dos_idiv: abs(n) / abs(d), truncated */
static int32_t idiv(dctx *X, int32_t n, int32_t d)
{
    uint32_t q;
    if (d == 0 || n == INT32_MIN || d == INT32_MIN) { X->bad = 1; return 0; }
    q = uabs(n) / uabs(d);
    return (n < 0) != (d < 0) ? wneg(q) : (int32_t)q;
}

/* dot(a, b) as the reference driver (Mesa lavapipe) computes GLSL's dot: fused, the last component first (the order
 * rb_gl12_world.c's matrix products found; the 23 scenes' frames agree only with it) */
static float dosrec_dot(const float a[3], const float b[3]) { return fmaf(a[0], b[0], fmaf(a[1], b[1], a[2] * b[2])); }
/* world_common.glsl dos_cam: the camera integers of world point w */
static void dos_cam(dctx *X, const float w[3], int32_t c[3])
{
    const dosrec_view *V = X->V;
    float d[3];
    d[0] = rnd(w[0]) - rnd(V->eye[0]); d[1] = rnd(w[1]) - rnd(V->eye[1]); d[2] = rnd(-w[2]) - rnd(-V->eye[2]);
    c[0] = f2i(X, rnd(dosrec_dot(V->rx, d)));
    c[1] = f2i(X, rnd(dosrec_dot(V->ry, d)));
    c[2] = f2i(X, rnd(dosrec_dot(V->rz, d)));
}
static int32_t dos_pq(dctx *X, int32_t c, int s, int32_t z)
{
    int32_t q;
    if (s < 0 || s > 30 || c == INT32_MIN) { X->bad = 1; return 0; }
    if ((int32_t)uabs(c) < (1 << (30 - s))) q = idiv(X, wshl(c, s), z);
    else q = f2i(X, truncf((float)c * exp2f((float)s) / (float)z));
    return wadd(q, 2) >> 2;
}
static void dos_camscr(dctx *X, const int32_t c[3], int32_t o[2])
{
    const dosrec_view *V = X->V;
    int32_t hx = (int32_t)V->vp[0] / 2, hy = (int32_t)V->vp[1] / 2, z = imax(c[2], 1);
    o[0] = wadd(hx, dos_pq(X, c[0], V->ps[0], z));
    o[1] = wsub(hy, dos_pq(X, c[1], V->ps[1], z));
}
/* dos_muldiv: trunc(a b / c), a b 64-bit */
static int32_t muldiv(dctx *X, int32_t a, int32_t b, int32_t c)
{
    uint32_t ua = uabs(a), ub = uabs(b), uc = uabs(c);
    int neg = ((a < 0) != (b < 0)) != (c < 0);
    uint64_t q;
    if (c == 0) return 0;
    if (ua < 46341u && ub < 46341u) { uint32_t q0 = (ua * ub) / uc; return neg ? wneg(q0) : (int32_t)q0; }
    q = (uint64_t)ua * ub / uc;
    if (q >= QLIM) { X->bad = 1; return 0; }
    return neg ? wneg((uint32_t)q) : (int32_t)q;
}
/* dos_nclip: the near-plane point of edge A - B (x, y; z = nr) */
static void nclip(dctx *X, const int32_t A[3], const int32_t B[3], int32_t nr, int32_t o[3])
{
    if (B[2] <= A[2]) {
        int32_t dz = wsub(A[2], B[2]), e2 = wsub(nr, B[2]);
        o[0] = wadd(muldiv(X, wsub(A[0], B[0]), e2, dz), B[0]); o[1] = wadd(muldiv(X, wsub(A[1], B[1]), e2, dz), B[1]);
    } else {
        int32_t dz = wsub(B[2], A[2]), e2 = wsub(nr, A[2]);
        o[0] = wadd(muldiv(X, wsub(B[0], A[0]), e2, dz), A[0]); o[1] = wadd(muldiv(X, wsub(B[1], A[1]), e2, dz), A[1]);
    }
    o[2] = nr;
}
/* dos_nclip_a, for each of the three attributes */
static void nclip_a(dctx *X, const int32_t A[3], const int32_t B[3], const int32_t *Aa, const int32_t *Ba, int32_t nr, int32_t *o)
{
    int j;
    for (j = 0; j < 3; j++) {
        if (B[2] <= A[2]) o[j] = A[2] == B[2] ? Aa[j] : wadd(muldiv(X, wsub(Aa[j], Ba[j]), wsub(nr, B[2]), wsub(A[2], B[2])), Ba[j]);
        else o[j] = wadd(muldiv(X, wsub(Ba[j], Aa[j]), wsub(nr, A[2]), wsub(B[2], A[2])), Aa[j]);
    }
}

/* ---- the paint's 2D clip (world.frag dos_clip2d and its i64 helpers) */
static int32_t i64_sar(int64_t a, int s) { return s <= 0 ? (int32_t)(uint32_t)(uint64_t)a : (int32_t)(uint32_t)((uint64_t)a >> s); }
static int32_t i64_div(dctx *X, int64_t n, int32_t d)
{
    uint64_t P, q;
    int ng = n < 0;
    if (d == 0) return 0;
    if (n == INT64_MIN) { X->bad = 1; return 0; }
    P = ng ? (uint64_t)-n : (uint64_t)n;
    q = P / uabs(d);
    if (q >= QLIM) { X->bad = 1; return 0; }
    return ng != (d < 0) ? wneg((uint32_t)q) : (int32_t)q;
}
static int32_t c2lin(dctx *X, int32_t o, int32_t a0, int32_t b0, int32_t a1, int32_t b1)
{
    return b0 <= a0 ? wadd(muldiv(X, wsub(o, a0), wsub(b1, a1), wsub(b0, a0)), a1) : wadd(muldiv(X, wsub(o, b0), wsub(a1, b1), wsub(a0, b0)), b1);
}
static int32_t c2z(dctx *X, int32_t a, int32_t b, int32_t L, int isx, int32_t za, int32_t zb)
{
    const dosrec_view *V = X->V;
    int32_t bx = a, dx = wsub(b, a), bz = za, dz = wsub(zb, za), k, t;
    int32_t cx = (int32_t)V->vp[0] / 2, cy = (int32_t)V->vp[1] / 2;
    if (dz < 0) { dz = wneg((uint32_t)dz); bx = b; dx = wneg((uint32_t)dx); bz = zb; }
    k = isx ? wsub(L, cx) : wsub(cy, L);
    t = wsub(i64_sar((int64_t)wshl(k, 2) * dz, isx ? V->ps[0] : V->ps[1]), dx);
    return t == 0 ? bz : i64_div(X, (int64_t)((uint64_t)((int64_t)bx * dz) - (uint64_t)((int64_t)bz * dx)), t);
}
static int c2out(const int32_t p[2], int e) { return e == 0 ? p[0] > 16383 : e == 1 ? p[0] < -16384 : e == 2 ? p[1] < -16384 : p[1] > 16383; }
/* Rc: camera x, y, z; Rs: screen x, y; Ra: the three attributes. Returns the new count; a cap reached: bad */
static int clip2d(dctx *X, int32_t Rc[DOSREC_MAXR][3], int32_t Rs[DOSREC_MAXR][2], int32_t Ra[DOSREC_MAXR][3], int m)
{
    const dosrec_view *V = X->V;
    int i, e, j, big = 0;
    int32_t cx = (int32_t)V->vp[0] / 2, cy = (int32_t)V->vp[1] / 2;
    for (i = 0; i < m; i++) big |= Rs[i][0] < -16383 || Rs[i][0] > 16382 || Rs[i][1] < -16383 || Rs[i][1] > 16382;
    if (!big) return m;
    for (e = 0; e < 4; e++) {
        int32_t Wc[DOSREC_MAXR][3], Ws[DOSREC_MAXR][2], Wa[DOSREC_MAXR][3];
        int n = 0, isx = e < 2;
        int32_t L = e == 0 || e == 3 ? 16383 : -16384;
        for (i = 0; i < m && i < 20; i++) {
            int pi = (i + m - 1) % m, po = c2out(Rs[pi], e), qo = c2out(Rs[i], e);
            if (po != qo) {
                const int32_t *A = Rc[pi], *B = Rc[i];
                int32_t z, sp[2], oc[3], adx, ady;
                int by;
                if (n >= 20) { X->bad = 1; return 0; }
                z = c2z(X, isx ? A[0] : A[1], isx ? B[0] : B[1], L, isx, A[2], B[2]);
                if (isx) { sp[0] = L; sp[1] = c2lin(X, L, Rs[pi][0], Rs[i][0], Rs[pi][1], Rs[i][1]); }
                else { sp[0] = c2lin(X, L, Rs[pi][1], Rs[i][1], Rs[pi][0], Rs[i][0]); sp[1] = L; }
                oc[0] = i64_sar((int64_t)wshl(wsub(sp[0], cx), 2) * z, V->ps[0]);
                oc[1] = i64_sar((int64_t)wshl(wsub(cy, sp[1]), 2) * z, V->ps[1]);
                oc[2] = z;
                adx = (int32_t)uabs(wsub(A[0], B[0])); ady = (int32_t)uabs(wsub(A[1], B[1]));
                by = isx ? (adx > ady ? 0 : ady != 0 ? 1 : 2) : (ady > adx ? 1 : adx != 0 ? 0 : 2);
                for (j = 0; j < 3; j++)
                    Wa[n][j] = by == 2 ? wadd(Ra[pi][j], Ra[i][j]) >> 1 : by == 0 ? c2lin(X, oc[0], A[0], B[0], Ra[pi][j], Ra[i][j]) : c2lin(X, oc[1], A[1], B[1], Ra[pi][j], Ra[i][j]);
                memcpy(Wc[n], oc, sizeof oc); memcpy(Ws[n], sp, sizeof sp); n++;
            }
            if (!qo) {
                if (n >= 20) { X->bad = 1; return 0; }
                memcpy(Wc[n], Rc[i], sizeof Wc[n]); memcpy(Ws[n], Rs[i], sizeof Ws[n]); memcpy(Wa[n], Ra[i], sizeof Wa[n]); n++;
            }
        }
        m = n;
        memcpy(Rc, Wc, sizeof Wc); memcpy(Rs, Ws, sizeof Ws); memcpy(Ra, Wa, sizeof Wa);
    }
    return m;
}

/* dos_xstep */
static uint32_t xstep(dctx *X, int32_t dx, int32_t cnt)
{
    int32_t qd = idiv(X, dx, cnt), rd = wsub(dx, wmul(qd, cnt)), sf;
    if ((int32_t)uabs(rd) >= 32768) { X->bad = 1; return 0; }   /* (the float quotient: never with the clipped records) */
    sf = idiv(X, wmul(rd, 65536), cnt);
    return (uint32_t)qd * 65536u + (uint32_t)sf;
}
/* one chain from the top record (dos_gedge's walk, every edge it can reach) */
static void chain(dctx *X, const int32_t Rs[DOSREC_MAXR][2], int m, int top, int dir, int ci, dosrec_poly *o)
{
    int a = top, b = (a + dir + m) % m, g, k = 0;
    int32_t r0 = Rs[top][1], cnt;
    for (g = 0; g < 20 && Rs[b][1] == Rs[a][1] && b != top; g++) { a = b; b = (a + dir + m) % m; }
    cnt = wsub(Rs[b][1], Rs[a][1]);
    for (g = 0;; g++) {
        int32_t *E = o->edge[ci][k], sc = cnt == 0 ? 1 : cnt;
        E[0] = wadd(r0, cnt); E[1] = r0; E[2] = sc; E[3] = (int32_t)xstep(X, wsub(Rs[b][0], Rs[a][0]), sc);
        E[4] = Rs[a][0]; E[5] = a; E[6] = b; E[7] = 0;
        k++;
        if (g >= 20 || b == top) break;   /* (the walk's own limits: 20 steps, the top corner again) */
        r0 = wadd(r0, cnt); a = b; b = (a + dir + m) % m; cnt = wsub(Rs[b][1], Rs[a][1]);
        if (cnt == 0) cnt = 1;
    }
    o->ne[ci] = k;
}

int dosrec_build(const dosrec_view *V, const float *vf, int nv, int s, dosrec_poly *o)
{
    dctx X;
    int I[18], n = 0, t, i, k, near = 0, glnear = 0, m = 0, top;
    int32_t Cc[18][3], Ca[18][3], Rc[DOSREC_MAXR][3], Rs[DOSREC_MAXR][2], Ra[DOSREC_MAXR][3], nr;
    X.V = V; X.vf = vf; X.nv = nv; X.bad = 0;
    /* the polygon's corners: its fan's triangles from s to the one whose edge code has the closing bit */
    for (t = 0; t < 16; t++) {
        int b = s + 3 * t, eb;
        if (t == 0) I[n++] = b;
        I[n++] = b + 1;
        eb = (int)(dvtx(&X, b * FPV + 12) + 0.5f);
        if (((eb % 32 / 3) & 2) != 0) { I[n++] = b + 2; break; }
    }
    {   /* dos_nearof: the cockpit set's near plane, or the view's */
        int e = (int)(dvtx(&X, s * FPV + 12) + 0.5f) / 65536;
        nr = e >= 1 && e < 200 ? DOS_PIT_NEAR : V->nr;
    }
    if (nr <= 0 || n < 3) return 0;
    o->nc = n;
    for (i = 0; i < n; i++) {
        const int b2 = I[i] * FPV;
        float w[3], u, v;
        w[0] = dvtx(&X, b2); w[1] = dvtx(&X, b2 + 1); w[2] = dvtx(&X, b2 + 2);
        u = dvtx(&X, b2 + 6); v = dvtx(&X, b2 + 7);
        dos_cam(&X, w, Cc[i]);
        if (Cc[i][2] < nr) near = 1;
        Ca[i][0] = wshl(f2i(&X, floorf((v + u) + 0.5f)), 16);   /* (the Gouraud corner's baked index a << 16: dvtx(+7) + dvtx(+6)) */
        Ca[i][1] = wshl(f2i(&X, floorf(u + 0.5f)), 16);
        Ca[i][2] = wshl(f2i(&X, floorf(v + 0.5f)), 16);
        o->cuv[i][0] = u; o->cuv[i][1] = v;
        {   /* (GL's near plane - z + w >= 0 - must keep every corner of a polygon not near-clipped, as the per-pixel
             * fill's test does: z + w in doubles, with a margin for the shader's floats) */
            const float *M = V->mvp;
            double z = (double)M[2] * w[0] + (double)M[6] * w[1] + (double)M[10] * w[2] + M[14];
            double ww = (double)M[3] * w[0] + (double)M[7] * w[1] + (double)M[11] * w[2] + M[15];
            double mag = fabs((double)M[2] * w[0]) + fabs((double)M[6] * w[1]) + fabs((double)M[10] * w[2]) + fabs((double)M[14])
                       + fabs((double)M[3] * w[0]) + fabs((double)M[7] * w[1]) + fabs((double)M[11] * w[2]) + fabs((double)M[15]);
            if (!(z + ww > 1e-4 * mag)) glnear = 1;
        }
    }
    if (!near && glnear) return 0;
    if (X.bad) return 0;
    /* the records: the corners in front, the near plane's points where an edge crosses it (0x3ec8e -> 0x3e8fe) */
    for (k = 0; k <= n; k++) {
        int cur = k % n, prv = (k + n - 1) % n, bc = Cc[cur][2] < nr, bp = Cc[prv][2] < nr;
        if (k > 0 && bc != bp) {
            if (m >= 18) return 0;
            nclip(&X, Cc[prv], Cc[cur], nr, Rc[m]); nclip_a(&X, Cc[prv], Cc[cur], Ca[prv], Ca[cur], nr, Ra[m]); m++;
        }
        if (k < n && !bc) {
            if (m >= 18) return 0;
            memcpy(Rc[m], Cc[cur], sizeof Rc[m]); memcpy(Ra[m], Ca[cur], sizeof Ra[m]); m++;
        }
    }
    for (i = 0; i < m; i++) dos_camscr(&X, Rc[i], Rs[i]);
    if (X.bad || m < 3) return 0;
    k = m;
    m = clip2d(&X, Rc, Rs, Ra, m);
    if (X.bad || m < 3) return 0;
    /* (the Gouraud fill's discards: off for a polygon not near-clipped whose 2D clip changed its record count) */
    o->nodiscard = !near && m != n;
    (void)k;
    o->m = m;
    top = 0; o->ybot = Rs[0][1];
    for (i = 0; i < m; i++) {
        if (Rs[i][1] <= Rs[top][1]) top = i;
        if (Rs[i][1] > o->ybot) o->ybot = Rs[i][1];
        o->rec[i][0] = Ra[i][0]; o->rec[i][1] = Ra[i][1]; o->rec[i][2] = Ra[i][2]; o->rec[i][3] = Rc[i][2];
    }
    o->top = top; o->ytop = Rs[top][1];
    chain(&X, Rs, m, top, -1, 0, o);
    chain(&X, Rs, m, top, 1, 1, o);
    return !X.bad;
}
