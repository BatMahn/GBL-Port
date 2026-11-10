/*
 * rb_gl12_world.c - the world and HUD programs (shaders/world.vert, world.frag, hud.frag) carried out on OpenGL 1.2's
 * fixed-function pipeline (the RB=gl12 backend, src/rb_gl12.c; docs/RENDERER_GL12.md).
 *
 * The vertex stage is world.vert in C, per vertex (both its DOS_LOOK variants: the DOS / ENHANCED looks' program and the
 * TITANIUM look's); its clip-space positions go to GL with identity matrices, so GL clips and rasterizes the triangles
 * as the GPU did. The fragment stage becomes per-triangle fixed-function classes:
 *  - texture x vertex colour (the lighting, tint and fades per vertex), the alpha test for the shader's discards,
 *    texture variants for its per-texel steps (the DOS shade levels, ENHANCED's true-colour levels);
 *  - the Titanium fog per pixel: a ramp texture on unit 1 (GL_DECAL; RGB the fog colour, A the factor) whose
 *    coordinate GL's perspective interpolation keeps linear in the view depth; one texture unit: a second pass;
 *  - the draw orders (DOS and TITANIUM): a polygon's pixels at its key's depth - its triangles clipped at the near
 *    plane here, then sent at that constant depth;
 *  - the DOS look's exact fill (u_dos_exact): world.frag's per-pixel integer span walk over the polygon data (the
 *    storage buffer) done on the CPU, row by row - each triangle's GL coverage (as the vertex stage pushed it out)
 *    cut to the DOS spans, sent as one-row quads: the palette colours and the Gouraud dither through a 2 x 256
 *    "palette pair" texture (the checkerboard from s = x + y), the textured types 5 / 6 with the original's texel
 *    walk (affine; perspective in runs of 16), the others with GL's perspective texels; ENHANCED's true-colour shades
 *    as vertex colours / blended level textures with the fog ramp, on quads at the triangle's own w.
 * Approximations (docs/RENDERER_GL12.md): a point light's direction per vertex (3D look) or per triangle (DOS looks);
 * ENHANCED's texture shade levels to an eighth of a level and its filtering as GL's bilinear (no footprint taps);
 * its Gouraud shade as a ramp within a pair run; a near-clipped polygon without the exact spans as GL covers it.
 */
#include "rb_gl12.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPV 13   /* floats a world vertex (pos 3, normal 3, uv 2, colour 3, texsel, edge) */

/* world.vert's outputs */
typedef struct {
    float clip[4];
    float normal[3], uv[2], color[3], viewpos[3], world[3], tic[3], sph[4];
    float dkey, dord;
    int texsel, emask, bb, tif, dnib, ticm, dtrust, tinofog, dosf;
} wv;

static const float *g_v0, *g_v1, *g_v2, *g_sb;   /* the bound buffers (vertex 0 at index 0), the storage buffer */
static const float *g_sb2;                       /* the DOS program's second storage buffer: the draw list's depths */
static size_t g_nsb2;
static size_t g_n0, g_n1, g_n2, g_nsb;           /* their sizes in vertices (floats for the storage buffer) */
static int g_dosprog;                            /* the DOS_LOOK program (RB_PROG_WORLD_DOS / _DOSDBG) */
static int g_dosdbg;                             /* MW2_DOS_DEBUG's program (DOS_DEBUG 1) */

/* a fused multiply-add (the product of two floats is exact in a double: the sum rounded once more - as a true fma
 * nearly always), fast without an FMA instruction (the legacy build's SSE2) */
static float fmad(float a, float b, float c) { return (float)((double)a * (double)b + (double)c); }
/* m x v as the shaders' matrix products come out of the SDL_GPU reference (Mesa lavapipe: fused, the last column first) -
 * the DOS integer projection and the near-plane tests decide whole pixels on these values */
static void mat4v(const float *m, const float v[4], float o[4])
{
    int r;
    for (r = 0; r < 4; r++) o[r] = fmad(m[r], v[0], fmad(m[4 + r], v[1], fmad(m[8 + r], v[2], m[12 + r] * v[3])));
}
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float *a) { return sqrtf(dot3(a, a)); }
static void cull(float c[4]) { c[0] = c[1] = c[2] = 2.0f; c[3] = 1.0f; }   /* gl_Position (2, 2, 2, 1): off screen */

/* ---- the storage buffer and the DOS look's integer projection (world_common.glsl) */
static float dvtx(int i) { return g_sb && i >= 0 && (size_t)i < g_nsb ? g_sb[i] : 0.0f; }
static void dpos(int k, float o[3]) { o[0] = dvtx(k * FPV); o[1] = dvtx(k * FPV + 1); o[2] = dvtx(k * FPV + 2); }
static int ebits(int b) { return ((int)(dvtx(b * FPV + 12) + 0.5f) % 32) / 3; }   /* a fan triangle's edge bits (4 first, 2 last) */
static int dos_idiv(int n, int d) { int q = abs(n) / abs(d); return (n < 0) != (d < 0) ? -q : q; }
static float dos_rnd(float v) { return floorf(v + 0.5f); }
static int dos_pq(int c, int s, int z)
{
    int q;
    if (abs(c) < (1 << (30 - s))) q = dos_idiv((int)((unsigned)c << s), z);
    else q = (int)truncf((float)c * exp2f((float)s) / (float)z);
    return (q + 2) >> 2;
}
static void dos_d(const float w[3], float d[3])
{
    const float *e = G.F.u_dos_eye;
    d[0] = dos_rnd(w[0]) - dos_rnd(e[0]); d[1] = dos_rnd(w[1]) - dos_rnd(e[1]); d[2] = dos_rnd(-w[2]) - dos_rnd(-e[2]);
}
static float dos_camz(const float w[3]) { float d[3]; dos_d(w, d); return dos_rnd(dot3(G.F.u_dos_rz, d)); }
static void dos_cam(const float w[3], int c[3])
{
    float d[3];
    dos_d(w, d);
    c[0] = (int)dos_rnd(dot3(G.F.u_dos_rx, d)); c[1] = (int)dos_rnd(dot3(G.F.u_dos_ry, d)); c[2] = (int)dos_rnd(dot3(G.F.u_dos_rz, d));
}
static void dos_camscr(const int c[3], int o[2])
{
    int hx = (int)G.F.u_vp[0] / 2, hy = (int)G.F.u_vp[1] / 2, z = c[2] > 1 ? c[2] : 1;
    o[0] = hx + dos_pq(c[0], G.F.u_dos_ps[0], z); o[1] = hy - dos_pq(c[1], G.F.u_dos_ps[1], z);
}
static void dos_scr(const float w[3], int o[2]) { int c[3]; dos_cam(w, c); dos_camscr(c, o); }
#define DOS_PIT_NEAR 32
/* (GBL 0x3e8fe: imul / idiv, a 64-bit product) */
static int64_t dos_ldiv(int64_t n, int64_t d) { int64_t q = (n < 0 ? -n : n) / (d < 0 ? -d : d); return (n < 0) != (d < 0) ? -q : q; }
static void dos_nclip(const int A[3], const int B[3], int nr, int o[3], float *t)
{
    if (B[2] <= A[2]) {
        int dz = A[2] - B[2], e2 = nr - B[2];
        *t = 1.0f - (float)e2 / (float)(dz > 1 ? dz : 1);
        o[0] = (int)(dos_ldiv((int64_t)(A[0] - B[0]) * e2, dz) + B[0]); o[1] = (int)(dos_ldiv((int64_t)(A[1] - B[1]) * e2, dz) + B[1]);
    } else {
        int dz = B[2] - A[2], e2 = nr - A[2];
        *t = (float)e2 / (float)(dz > 1 ? dz : 1);
        o[0] = (int)(dos_ldiv((int64_t)(B[0] - A[0]) * e2, dz) + A[0]); o[1] = (int)(dos_ldiv((int64_t)(B[1] - A[1]) * e2, dz) + A[1]);
    }
    o[2] = nr;
}
static int dos_ispit(int vtx) { int e = (int)(dvtx(vtx * FPV + 12) + 0.5f) / 65536; return e >= 1 && e < 200; }
/* world_common.glsl dos_nearof: the cockpit set's near plane, or the view's (with the DOS order on), else 0 */
static int dos_nearof(int vtx) { return dos_ispit(vtx) ? DOS_PIT_NEAR : G.F.u_dos_order != 0 ? G.F.u_dos_near : 0; }
/* world_common.glsl dos_line_ends / dos_line_on: a line's DOS points (the near plane's point for a corner behind it) and
 * GBL 0x54fa9's pixels - from corner 0 along the longer axis, ends included, the other coordinate by the carry of
 * (minor << 32) / major (0xffffffff when equal) started at 0x80000000 */
static int dos_line_ends(const float A[3], const float B[3], int p0[2], int p1[2])
{
    const int nr = G.F.u_dos_order != 0 && G.F.u_dos_near > 1 ? G.F.u_dos_near : 1;
    int a[3], b[3], q[3];
    float t;
    dos_cam(A, a); dos_cam(B, b);
    if (a[2] < nr && b[2] < nr) return 0;
    if (a[2] < nr) { dos_nclip(a, b, nr, q, &t); memcpy(a, q, sizeof a); }
    else if (b[2] < nr) { dos_nclip(a, b, nr, q, &t); memcpy(b, q, sizeof b); }
    dos_camscr(a, p0); dos_camscr(b, p1);
    return 1;
}
static int dos_line_on(int x, int y, const int a[2], const int b[2])
{
    const int dx = b[0] - a[0], dy = b[1] - a[1], ax = abs(dx), ay = abs(dy), sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    int xm, mj, k;
    uint32_t s, st;
    if (dx == 0) return x == a[0] && y >= (a[1] < b[1] ? a[1] : b[1]) && y <= (a[1] > b[1] ? a[1] : b[1]);
    if (dy == 0) return y == a[1] && x >= (a[0] < b[0] ? a[0] : b[0]) && x <= (a[0] > b[0] ? a[0] : b[0]);
    xm = ax > ay; mj = xm ? ax : ay;
    s = ax == ay ? 0xffffffffu : (uint32_t)(((uint64_t)(uint32_t)(xm ? ay : ax) << 32) / (uint32_t)mj);
    k = xm ? (x - a[0]) * sx : (y - a[1]) * sy;
    if (k < 0 || k > mj) return 0;
    st = (uint32_t)(((uint64_t)(uint32_t)k * s + 0x80000000u) >> 32);
    return xm ? y == a[1] + sy * (int)st : x == a[0] + sx * (int)st;
}
/* a polygon's corners in the layer (its fan from first vertex s: corner 0, each triangle's next, the closing one's last) */
static int poly_corners(int s, int *I, int cap)
{
    int k, c, n = 0;
    for (k = 0; k < 16; k++) {
        int b = s + 3 * k;
        for (c = k == 0 ? 0 : 1; c < 3; c++) {
            if (c == 2 && (ebits(b) & 2) == 0) break;
            if (n < cap) I[n++] = b + c;
        }
        if (ebits(b) & 2) break;
    }
    return n;
}

/* ---- the vertex stage (world.vert), per-polygon results kept while the vertices of one polygon follow each other */
static struct { int s, ok, n; float Q[18][2]; } pc_push;          /* the exact fill's corners on screen (polygon s) */
static struct { int s, isnear, any; int lo[2], hi[2]; } pc_pit;      /* a cockpit polygon's near-clipped screen box */
static struct { int t; float km, kn, ks, kcl; int kc, back, dosf; } pc_key;   /* the fog / order keys, light, facing (fan t) */
static struct { int t, wok; float wd, km; } pc_ti;                 /* TITANIUM: winding . (v0 - eye), deepest corner (fan t) */

static void poly_cache_reset(void) { pc_push.s = pc_pit.s = pc_key.t = pc_ti.t = -1; }

static void vertex_stage(int vid, int i, wv *o)
{
    const float *a = g_v0 && (size_t)i < g_n0 ? g_v0 + (size_t)i * FPV : NULL;
    static const float Z[FPV];
    const float *sph = g_v1 && (size_t)i < g_n1 ? g_v1 + (size_t)i * 4 : Z, *tc = g_v2 && (size_t)i < g_n2 ? g_v2 + (size_t)i * 4 : Z;
    const rb_world_frame *F = &G.F;
    float p[4], nrm[3], q[4];
    int e, pit, kmode = 0, tiwind = 0, hi;
    if (!a) a = Z;
    o->dord = 2.0f;
    e = (int)(a[12] + 0.5f); pit = e / 65536;
    e -= 65536 * pit;
    o->tinofog = 0;
    int tisky = 0, ptc = -1;
    if (pit >= 400) { ptc = pit - 400; pit = 0; }   /* a point's pixel corner, 10 + a line's box corner (world.vert) */
    else if (pit >= 300) { int qq = pit - 300; tisky = qq >= 8; qq -= 8 * tisky; o->tinofog = qq >= 4; qq -= 4 * o->tinofog; tiwind = qq == 1 ? 1 : qq == 0 ? -1 : 0; pit = 0; }
    else if (pit >= 200) { kmode = pit - 200; pit = 0; }
    /* the DOS draw list (world.vert): the polygon's depth along the painting order, or not listed */
    const int dli = g_dosprog && F->u_dos != 0 && F->u_dos_order != 0 && G.D.u_dlist != 0 && pit == 0 && tc[3] < 0.5f ? (int)(tc[0] + 0.5f) - 1 : -1;
    const float dld = dli >= 0 && g_sb2 && (size_t)dli < g_nsb2 ? g_sb2[dli] : 3.0f;
    o->dnib = e / 2048; e -= o->dnib * 2048;
    o->bb = e >= 1024 ? 1 : 0;
    o->tif = 0;
    if (e < 1024 && e >= 512) { o->tif |= 2; e -= 512; }
    if (e < 1024 && e >= 256) { o->tif |= 1; e -= 256; }
    p[0] = a[0]; p[1] = a[1]; p[2] = a[2]; p[3] = 1.0f;
    memcpy(nrm, a + 3, sizeof nrm);
    mat4v(F->u_mvp, p, o->clip);
    if (e >= 1024) {   /* a colour type 3 billboard (world.vert) */
        float cb[4], ca[4], pa[4] = {a[3], a[4], a[5], 1.0f};
        int k = (int)(a[11] + 0.5f), tib = k >= 4 ? 1 : 0;
        mat4v(F->u_mvp, p, cb); mat4v(F->u_mvp, pa, ca);
        e -= 1024; nrm[0] = nrm[1] = nrm[2] = 0.0f; k -= 4 * tib;
        if (ca[3] <= 1.0f || cb[3] <= 1.0f) cull(o->clip);
        else {
            float hx = 0.5f * F->u_vp[0], hy = 0.5f * F->u_vp[1], cx, cy;
            float Tx = ca[0] / ca[3] * hx, Ty = -(ca[1] / ca[3] * hy), Fx = cb[0] / cb[3] * hx, Fy = -(cb[1] / cb[3] * hy);
            if (tib) {
                float dx = 0.5f * (Fy - Ty), dy = 0.5f * (Fx - Tx);
                if (k == 0) { cx = Tx - dx; cy = Ty - dy; } else if (k == 1) { cx = Fx - dx; cy = Fy + dy; }
                else if (k == 2) { cx = Tx + dx; cy = Fy - dy; } else { cx = Fx + dx; cy = Ty + dy; }
                o->clip[2] = k == 0 || k == 3 ? ca[2] / ca[3] : cb[2] / cb[3];
                if (k == 0 || k == 3) { p[0] = a[3]; p[1] = a[4]; p[2] = a[5]; }
            } else {
                float dx = 0.5f * (Fx - Tx), dy = 0.5f * (Fy - Ty);
                if (k == 0) { cx = Tx - dy; cy = Ty + dx; } else if (k == 1) { cx = Tx + dy; cy = Ty - dx; }
                else if (k == 2) { cx = Fx + dy; cy = Fy - dx; } else { cx = Fx - dy; cy = Fy + dx; }
                o->clip[2] = k < 2 ? ca[2] / ca[3] : cb[2] / cb[3];
                if (k < 2) { p[0] = a[3]; p[1] = a[4]; p[2] = a[5]; }
            }
            o->clip[0] = cx / hx; o->clip[1] = -cy / hy; o->clip[3] = 1.0f;
        }
    }
    if (g_dosprog && g_sb && F->u_dos != 0 && F->u_dos_exact != 0 && (int)(a[8] + 0.5f) <= 3 && a[11] > 0.5f && e < 256) {
        /* the DOS fill's coverage: the polygon drawn 2 px larger at the DOS integer points, each corner pushed out
         * along its two edges' outward normals (miter, at most 8 px); the fragment keeps the DOS span's pixels */
        const int s = (int)(a[11] + 0.5f) - 1, rel = vid - s, t = rel / 3, cn = rel - 3 * t;
        const float hx = 0.5f * F->u_vp[0], hy = 0.5f * F->u_vp[1];
        int j;
        if (pc_push.s != s) {
            int I[18], n = poly_corners(s, I, 18), c;
            pc_push.s = s; pc_push.ok = 1; pc_push.n = n;
            for (c = 0; c < n; c++) {
                float w3[3], w4[4], cq[4];
                int sp[2];
                dpos(I[c], w3); w4[0] = w3[0]; w4[1] = w3[1]; w4[2] = w3[2]; w4[3] = 1.0f;
                mat4v(F->u_mvp, w4, cq);
                if (cq[2] + cq[3] < 0.0f || cq[3] <= 0.0f) pc_push.ok = 0;
                else { dos_scr(w3, sp); pc_push.Q[c][0] = (float)sp[0] + 0.5f - hx; pc_push.Q[c][1] = F->u_vp[1] - (float)sp[1] - 0.5f - hy; }
            }
        }
        j = cn == 0 ? 0 : t + cn;
        if (pc_push.ok && pc_push.n >= 3 && j < pc_push.n && o->clip[3] > 0.0f) {
            const int n = pc_push.n;
            float area = 0, A[2], B[2], C[2], e1[2], e2[2], sg, n1[2] = {0, 0}, n2[2] = {0, 0}, m[2], d, off[2], l1, l2, lo;
            int c;
            for (c = 0; c < n; c++) { const float *qa = pc_push.Q[c], *qb = pc_push.Q[(c + 1) % n]; area += qa[0] * qb[1] - qa[1] * qb[0]; }
            memcpy(A, pc_push.Q[(j + n - 1) % n], sizeof A); memcpy(B, pc_push.Q[j], sizeof B); memcpy(C, pc_push.Q[(j + 1) % n], sizeof C);
            e1[0] = B[0] - A[0]; e1[1] = B[1] - A[1]; e2[0] = C[0] - B[0]; e2[1] = C[1] - B[1];
            sg = area > 0.0f ? 1.0f : -1.0f;
            l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1]); l2 = sqrtf(e2[0] * e2[0] + e2[1] * e2[1]);
            if (l1 > 1e-4f) { n1[0] = sg * e1[1] / l1; n1[1] = -sg * e1[0] / l1; }
            if (l2 > 1e-4f) { n2[0] = sg * e2[1] / l2; n2[1] = -sg * e2[0] / l2; }
            m[0] = n1[0] + n2[0]; m[1] = n1[1] + n2[1]; d = 1.0f + n1[0] * n2[0] + n1[1] * n2[1];
            off[0] = d > 1e-3f ? 3.0f * m[0] / d : 3.0f * m[0]; off[1] = d > 1e-3f ? 3.0f * m[1] / d : 3.0f * m[1];
            lo = sqrtf(off[0] * off[0] + off[1] * off[1]);
            if (lo > 24.0f) { off[0] = off[0] / lo * 24.0f; off[1] = off[1] / lo * 24.0f; }
            o->clip[0] = (B[0] + off[0]) / hx * o->clip[3]; o->clip[1] = (B[1] + off[1]) / hy * o->clip[3];
        }
    }
    if (g_dosprog && g_sb && F->u_dos != 0 && F->u_dos_exact != 0 && (int)(a[8] + 0.5f) <= 3 && a[11] > 0.5f && !tiwind && dos_nearof((int)(a[11] + 0.5f) - 1) > 0) {
        /* a polygon with a corner nearer than its near plane (the cockpit set's, or the view's: world.vert): clipped
         * there in the camera's integers, each fan triangle laid over the clipped polygon's screen box (the fragment
         * keeps the DOS span's pixels) */
        const int s = (int)(a[11] + 0.5f) - 1, rel = vid - s, cn = rel - 3 * (rel / 3), nr = dos_nearof(s);
        if (pc_pit.s != s) {
            int I[18], n = poly_corners(s, I, 18), Cc[18][3], k;
            pc_pit.s = s; pc_pit.isnear = 0; pc_pit.any = 0;
            pc_pit.lo[0] = pc_pit.lo[1] = 1 << 30; pc_pit.hi[0] = pc_pit.hi[1] = -(1 << 30);
            for (k = 0; k < n; k++) { float w3[3]; dpos(I[k], w3); dos_cam(w3, Cc[k]); if (Cc[k][2] < nr) pc_pit.isnear = 1; }
            if (pc_pit.isnear)
                for (k = 0; k <= n && n >= 3; k++) {
                    const int cur = k % n, prv = (k + n - 1) % n, bc = Cc[cur][2] < nr, bp = Cc[prv][2] < nr;
                    int qc[3], qs[2], have = 0;
                    float tt;
                    if (k > 0 && bc != bp) { dos_nclip(Cc[prv], Cc[cur], nr, qc, &tt); dos_camscr(qc, qs); have = 1; }
                    if (have) {
                        if (qs[0] < pc_pit.lo[0]) pc_pit.lo[0] = qs[0];
                        if (qs[1] < pc_pit.lo[1]) pc_pit.lo[1] = qs[1];
                        if (qs[0] > pc_pit.hi[0]) pc_pit.hi[0] = qs[0];
                        if (qs[1] > pc_pit.hi[1]) pc_pit.hi[1] = qs[1];
                        pc_pit.any = 1;
                    }
                    if (k < n && !bc) {
                        dos_camscr(Cc[cur], qs);
                        if (qs[0] < pc_pit.lo[0]) pc_pit.lo[0] = qs[0];
                        if (qs[1] < pc_pit.lo[1]) pc_pit.lo[1] = qs[1];
                        if (qs[0] > pc_pit.hi[0]) pc_pit.hi[0] = qs[0];
                        if (qs[1] > pc_pit.hi[1]) pc_pit.hi[1] = qs[1];
                        pc_pit.any = 1;
                    }
                }
        }
        if (pc_pit.isnear) {
            const float hx = 0.5f * F->u_vp[0], hy = 0.5f * F->u_vp[1];
            int lo[2] = {pc_pit.lo[0], pc_pit.lo[1]}, hi2[2] = {pc_pit.hi[0], pc_pit.hi[1]};
            if (lo[0] < -2) lo[0] = -2;
            if (lo[1] < -2) lo[1] = -2;
            if (hi2[0] > (int)F->u_vp[0] + 2) hi2[0] = (int)F->u_vp[0] + 2;
            if (hi2[1] > (int)F->u_vp[1] + 2) hi2[1] = (int)F->u_vp[1] + 2;
            if (!pc_pit.any || hi2[0] < lo[0] || hi2[1] < lo[1]) cull(o->clip);
            else {
                float a0x = (float)lo[0] - 1.0f, a0y = (float)lo[1] - 1.0f, szx = (float)(hi2[0] - lo[0]) + 3.0f, szy = (float)(hi2[1] - lo[1]) + 3.0f;
                float ppx = cn == 1 ? a0x + 2.0f * szx : a0x, ppy = cn == 2 ? a0y + 2.0f * szy : a0y;
                o->clip[0] = (ppx - hx) / hx; o->clip[1] = (hy - ppy) / hy; o->clip[2] = 0.0f; o->clip[3] = 1.0f;
            }
        }
    }
    if (pit > 0 && F->u_dos != 0) {   /* the DOS cockpit set's draw order */
        int ix = pit - 1 < 127 ? pit - 1 : 127, rk = F->u_pit_rank4[ix >> 2][ix & 3];
        if (rk < 0) cull(o->clip);
        else o->clip[2] = (-1.0f + 0.01f * (1.0f - (float)(rk + 1) / 256.0f)) * o->clip[3];
    }
    if (dli < 0 && F->u_far > 0.0f && sph[3] > 0.0f) {   /* the view distance */
        float sc4[4] = {sph[0], sph[1], sph[2], 1.0f}, sc[4], rr = F->u_far + sph[3];
        mat4v(F->u_view, sc4, sc);
        if (sc[0] * sc[0] + sc[1] * sc[1] + sc[2] * sc[2] > rr * rr || -sc[2] - sph[3] > F->u_far) cull(o->clip);
    }
    o->dkey = -1.0f; o->dtrust = 1; o->dosf = 0;
    if (g_dosprog && g_sb && F->u_dos != 0) {
        /* the DOS integer projection checked against GL's for the triangle's last corner (more than 3 px apart: the
         * textured types' exact texels are not used) */
        int pv = 3 * (vid / 3) + 2, sp[2];
        float pp[4], q0[4];
        dpos(pv, pp); pp[3] = 1.0f;
        mat4v(F->u_mvp, pp, q0);
        if (q0[3] > 1.0f) {
            float gx = q0[0] / q0[3] * 0.5f * F->u_vp[0], gy = q0[1] / q0[3] * 0.5f * F->u_vp[1];
            float c0x = (float)((int)F->u_vp[0] / 2), c0y = (float)((int)F->u_vp[1] / 2), ex, ey;
            dos_scr(pp, sp);
            ex = (float)sp[0] - (c0x + gx); ey = (float)sp[1] - (c0y - gy);
            if (fabsf(ex) > 3.0f || fabsf(ey) > 3.0f) o->dtrust = 0;
        }
        if (fabsf(dvtx(vid * FPV) - a[0]) + fabsf(dvtx(vid * FPV + 2) - a[2]) < 0.01f) {
            int t = vid / 3, k, c;
            float key;
            for (k = 0; k < 16 && t > 0; k++) { if (ebits(3 * t) & 4) break; t--; }
            if (pc_key.t != t) {   /* the polygon's keys, its light intensity and facing (world.vert) */
                float P0[3], nn[3], nl;
                float zl[18];
                pc_key.t = t; pc_key.km = -1e9f; pc_key.kn = 1e9f; pc_key.ks = 0; pc_key.kc = 0; pc_key.back = 0; pc_key.dosf = 0;
                for (k = 0; k < 16; k++) {
                    int b = 3 * (t + k), last = (ebits(b) & 2) != 0;
                    for (c = 0; c < 3; c++) {
                        float w3[3], z;
                        dpos(b + c, w3); z = dos_camz(w3);
                        if (z > pc_key.km) pc_key.km = z;
                        if ((c == 0 && k == 0) || c == 1 || (c == 2 && last)) { if (z < pc_key.kn) pc_key.kn = z; if (pc_key.kc < 18) zl[pc_key.kc] = z; pc_key.kc++; }
                    }
                    if (last) break;
                }
                {   /* the mean over the near-clipped polygon's corners (world.vert) */
                    const float nz = F->u_dos_near > 0 ? (float)F->u_dos_near : 4.0f;
                    float kcl = 0.0f;
                    int i2;
                    if (pc_key.kc > 18) pc_key.kc = 18;
                    for (i2 = 0; i2 < pc_key.kc; i2++) {
                        const float za = zl[i2], zp = zl[(i2 + pc_key.kc - 1) % pc_key.kc];
                        if ((za < nz) != (zp < nz)) { pc_key.ks += nz; kcl += 1.0f; }
                        if (za >= nz) { pc_key.ks += za; kcl += 1.0f; }
                    }
                    pc_key.kcl = kcl;
                }
                dpos(3 * t, P0);
                nn[0] = dvtx(3 * t * FPV + 3); nn[1] = dvtx(3 * t * FPV + 4); nn[2] = dvtx(3 * t * FPV + 5);
                nl = len3(nn);
                if (nl > 0.0f) {   /* one-sided, once a polygon (GBL 0x3ec8e), whole cm */
                    const float *ey = F->u_dos_eye;
                    float dv[3] = {dos_rnd(P0[0]) - dos_rnd(ey[0]), dos_rnd(P0[1]) - dos_rnd(ey[1]), dos_rnd(-P0[2]) - dos_rnd(-ey[2])};
                    float Ng[3] = {-nn[0] / nl, -nn[1] / nl, nn[2] / nl};
                    if (dot3(dv, Ng) >= 0.0f) pc_key.back = 1;
                }
                if (nl > 0.0f) {   /* the light intensity f (GBL MW2.EXE 0x3eb36) */
                    float lg[3] = {F->u_dos_lpos[0], F->u_dos_lpos[1], -F->u_dos_lpos[2]}, vg[3] = {dos_rnd(P0[0]), dos_rnd(P0[1]), dos_rnd(-P0[2])};
                    float d[3], N[3] = {-nn[0] / nl * 8192.0f, -nn[1] / nl * 8192.0f, nn[2] / nl * 8192.0f}, ad[3], m, fi = 127.0f;
                    for (c = 0; c < 3; c++) { d[c] = dos_rnd(lg[c]) - (F->u_dos_dir != 0 ? 0.0f : vg[c]); ad[c] = fabsf(d[c]); }
                    m = ad[0] > ad[1] ? (ad[0] > ad[2] ? ad[0] : ad[2]) : (ad[1] > ad[2] ? ad[1] : ad[2]);
                    if (m > 0.0f) {
                        int mi = (int)ad[0] | (int)ad[1] | (int)ad[2], tb = 31;
                        float sc, cc[3], dot0, dots, S, dvv;
                        while (tb > 0 && (mi >> tb) == 0) tb--;
                        sc = exp2f((float)(7 - tb));
                        for (c = 0; c < 3; c++) cc[c] = floorf(ad[c] * sc);
                        dot0 = floorf(d[0] * N[0] + d[1] * N[1] + d[2] * N[2]); dots = floorf(dot0 * sc);
                        S = cc[0] * cc[0] + cc[1] * cc[1] + cc[2] * cc[2]; dvv = floorf(1024.0f * sqrtf(floorf(S / 256.0f)));
                        fi = dvv > 0.0f ? truncf(dots / dvv) : 127.0f;
                    }
                    pc_key.dosf = (int)fi + 1000;
                }
            }
            o->dkey = pc_key.km;
            if (g_dosprog) o->dosf = pc_key.dosf;
            if (pc_key.back && e < 1024 && dli < 0) cull(o->clip);   /* (back: only with a normal) */
            {
                const float nz = F->u_dos_near > 0 ? (float)F->u_dos_near : 4.0f;
                key = (kmode & 2) ? floorf(pc_key.ks / (pc_key.kcl > 1.0f ? pc_key.kcl : 1.0f)) : (kmode & 4) ? (pc_key.kn > nz ? pc_key.kn : nz) : pc_key.km;
            }
            if (pit == 0 && key > 0.0f) {   /* the DOS draw order: the polygon at its key's depth, on the view axis */
                const float *v = F->u_view;
                float fw[3] = {-v[2], -v[6], -v[10]}, ep[4] = {F->u_dos_eye[0] + fw[0] * key * 0.25f, F->u_dos_eye[1] + fw[1] * key * 0.25f, F->u_dos_eye[2] + fw[2] * key * 0.25f, 1.0f}, qq[4];
                float nd;
                mat4v(F->u_mvp, ep, qq);
                nd = qq[3] > 0.0f ? (qq[2] / qq[3] < 1.0f - 4e-6f ? qq[2] / qq[3] : 1.0f - 4e-6f) : 1.0f - 4e-6f;
                if (kmode & 1) nd = 1.0f - 2e-6f;
                o->dord = nd;
            }
        }
    }
    if (dli >= 0) {
        if (dld > 2.5f) cull(o->clip);
        else o->dord = dld;
    }
    if (!g_dosprog && g_sb && tiwind != 0 && G.D.u_onlayer != 0 && F->u_dos_order != 0 && e < 1024
        && fabsf(dvtx(vid * FPV) - a[0]) + fabsf(dvtx(vid * FPV + 2) - a[2]) < 0.01f) {
        /* the TITANIUM look's draw order (mercsw 0x100562ff): a polygon facing away not drawn, the others at their key's
         * depth (the deepest corner's view depth) */
        int t = vid / 3, k, c;
        const float *v = F->u_view;
        /* the eye: -(R^T x translation) */
        float eyeg[3] = {-(v[0] * v[12] + v[1] * v[13] + v[2] * v[14]), -(v[4] * v[12] + v[5] * v[13] + v[6] * v[14]), -(v[8] * v[12] + v[9] * v[13] + v[10] * v[14])};
        for (k = 0; k < 16 && t > 0; k++) { if (ebits(3 * t) & 4) break; t--; }
        if (pc_ti.t != t) {
            float P0[3], P1[3], P2[3], u1[3], u2[3], w[3], d0[3], km = -1e30f;
            pc_ti.t = t;
            dpos(3 * t, P0); dpos(3 * t + 1, P1); dpos(3 * t + 2, P2);
            for (c = 0; c < 3; c++) { u1[c] = P1[c] - P0[c]; u2[c] = P2[c] - P0[c]; d0[c] = P0[c] - eyeg[c]; }
            w[0] = u1[1] * u2[2] - u1[2] * u2[1]; w[1] = u1[2] * u2[0] - u1[0] * u2[2]; w[2] = u1[0] * u2[1] - u1[1] * u2[0];
            pc_ti.wok = len3(w) > 1e-3f;
            pc_ti.wd = dot3(w, d0);
            for (k = 0; k < 16; k++) {
                int b = 3 * (t + k);
                for (c = 0; c < 3; c++) {
                    float w4[4], vq[4];
                    dpos(b + c, w4); w4[3] = 1.0f;
                    mat4v(F->u_view, w4, vq);
                    if (-vq[2] > km) km = -vq[2];
                }
                if (ebits(b) & 2) break;
            }
            pc_ti.km = km;
        }
        if (pc_ti.wok && (float)tiwind * pc_ti.wd <= 0.0f) cull(o->clip);
        else if (pc_ti.km > 0.0f) {
            float fw[3] = {-v[2], -v[6], -v[10]}, ep[4] = {eyeg[0] + fw[0] * pc_ti.km, eyeg[1] + fw[1] * pc_ti.km, eyeg[2] + fw[2] * pc_ti.km, 1.0f}, qq[4];
            mat4v(F->u_mvp, ep, qq);
            o->dord = qq[3] > 0.0f ? (qq[2] / qq[3] < 1.0f - 4e-6f ? qq[2] / qq[3] : 1.0f - 4e-6f) : 1.0f - 4e-6f;
        }
    }
    if (!g_dosprog && tisky && G.D.u_onlayer != 0 && F->u_dos_order != 0) {   /* the sky object (world.vert) */
        float sd = 1.0f - 3e-6f + ((o->dord < 1.0f ? o->dord : 1.0f) - 1.0f) * 1e-3f;
        o->dord = sd < 1.0f - 3.9e-6f ? 1.0f - 3.9e-6f : sd > 1.0f - 1e-6f ? 1.0f - 1e-6f : sd;
    }
    if (ptc >= 10) {   /* a line: the box of its DOS points' pixels (world.vert); world_tri keeps 0x54fa9's */
        int la[2], lb[2];
        if (!g_dosprog || F->u_dos == 0 || dli < 0 || dld > 2.5f || !dos_line_ends(a, a + 3, la, lb)) cull(o->clip);
        else {
            const float hx = 0.5f * F->u_vp[0], hy = 0.5f * F->u_vp[1];
            const int lc = ptc - 10, vw = (int)F->u_vp[0], vh = (int)F->u_vp[1];
            int lo[2], hi[2], c;
            for (c = 0; c < 2; c++) {
                const int lim = c ? vh : vw;
                lo[c] = la[c] < lb[c] ? la[c] : lb[c]; hi[c] = la[c] > lb[c] ? la[c] : lb[c];
                lo[c] = lo[c] < -1 ? -1 : lo[c] > lim ? lim : lo[c]; hi[c] = hi[c] < -1 ? -1 : hi[c] > lim ? lim : hi[c];
            }
            {
                const float px = (float)(lc == 1 || lc == 2 || lc == 4 ? hi[0] + 1 : lo[0]), py = F->u_vp[1] - (float)(lc == 2 || lc == 4 || lc == 5 ? lo[1] : hi[1] + 1);
                o->clip[0] = (px - hx) / hx; o->clip[1] = (py - hy) / hy; o->clip[2] = 0.0f; o->clip[3] = 1.0f;
            }
        }
    }
    else if (ptc >= 0) {   /* a point: its pixel at the DOS integer projection (world.vert) */
        if (!g_dosprog || F->u_dos == 0 || dli < 0 || dld > 2.5f) cull(o->clip);
        else {
            int sp[2];
            const float hx = 0.5f * F->u_vp[0], hy = 0.5f * F->u_vp[1];
            float px, py;
            dos_scr(a, sp);
            px = (float)sp[0] + (ptc == 1 || ptc == 2 || ptc == 4 ? 1.0f : 0.0f);
            py = F->u_vp[1] - (float)sp[1] - 1.0f + (ptc == 2 || ptc == 4 || ptc == 5 ? 1.0f : 0.0f);
            o->clip[0] = (px - hx) / hx; o->clip[1] = (py - hy) / hy; o->clip[2] = 0.0f; o->clip[3] = 1.0f;
        }
    }
    mat4v(F->u_view, p, q);
    memcpy(o->viewpos, q, sizeof o->viewpos);
    memcpy(o->world, p, sizeof o->world);
    memcpy(o->normal, nrm, sizeof nrm);
    o->uv[0] = a[6]; o->uv[1] = a[7];
    memcpy(o->color, a + 8, sizeof o->color);
    o->texsel = (int)floorf(a[11] + 0.5f);
    memcpy(o->sph, sph, sizeof o->sph);
    memcpy(o->tic, tc, sizeof o->tic);
    o->ticm = (int)(tc[3] + 0.5f);
    hi = e / 32; e -= hi * 32;
    o->emask = (e / 3) | (hi << 3);
}

/* ---- colours */
static int dos_tab_ok(void) { int w = 0, h = 0; return gl12_tex_texels(G.tex[1], &w, &h) && w == 256 && h >= 17; }
static void dos_colour(float idx, float out[4])
{
    int w = 0, h = 0, i = (int)idx;
    const uint8_t *tab = gl12_tex_texels(G.tex[1], &w, &h);
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    out[3] = 1.0f;
    if (!tab || w != 256 || h < 17) { out[0] = out[1] = out[2] = 1.0f; return; }
    tab += ((size_t)16 * 256 + (size_t)i) * 4;
    out[0] = tab[0] / 255.0f; out[1] = tab[1] / 255.0f; out[2] = tab[2] / 255.0f;
}
/* palette colours e0, e0 + 1 blended at f (world.frag's smooth Gouraud / ENHANCED shades) */
static void pal_mix(int e0, float f, float out[4])
{
    float a[4], b[4];
    int c;
    dos_colour((float)e0, a); dos_colour((float)e0 + 1.0f, b);
    for (c = 0; c < 3; c++) out[c] = a[c] + (b[c] - a[c]) * f;
    out[3] = 1.0f;
}

/* the engine's vector length (0x100462fe) and the Titanium lights (world.frag ti_light), at a vertex */
static float ti_len(const float *v)
{
    float a = fabsf(v[0]), b = fabsf(v[1]), c = fabsf(v[2]), m = a > b ? (a > c ? a : c) : (b > c ? b : c);
    return m + (a + b + c - m) * 0.25f;
}
static void ti_light(const wv *v, const wv *flat, const float nn[3], float out[3])
{
    const rb_world_frame *F = &G.F;
    float c[3], acc[3];
    int i, j, k, hit = 0;
    memcpy(c, F->u_tiamb, sizeof c);
    for (i = 0; i < 2; i++) {
        if (F->u_tilt[i] == 1) { float d = -dot3(F->u_tilv[i], nn); if (d > 0) for (k = 0; k < 3; k++) c[k] += F->u_tilc[i][k] * d; }
        else if (F->u_tilt[i] == 2) {
            float l[3] = {F->u_tilv[i][0] - v->world[0], F->u_tilv[i][1] - v->world[1], F->u_tilv[i][2] - v->world[2]}, d = dot3(l, nn), ll = ti_len(l);
            if (d > 0) for (k = 0; k < 3; k++) c[k] += F->u_tilc[i][k] * d / (ll > 1e-3f ? ll : 1e-3f);
        } else for (k = 0; k < 3; k++) c[k] += F->u_tilc[i][k];
    }
    for (k = 0; k < 3; k++) if (c[k] > 1.0f) c[k] = 1.0f;
    memcpy(acc, c, sizeof acc);
    for (j = 0; j < 8 && j < F->u_tidn; j++) {
        float dd[3], l[3], d, ll;
        for (k = 0; k < 3; k++) dd[k] = fabsf(flat->sph[k] - F->u_tidp[j][k]);
        if (flat->sph[3] > 0 && (dd[0] > F->u_tidp[j][3] + flat->sph[3] || dd[1] > F->u_tidp[j][3] + flat->sph[3] || dd[2] > F->u_tidp[j][3] + flat->sph[3])) continue;
        for (k = 0; k < 3; k++) l[k] = F->u_tidp[j][k] - v->world[k];
        d = dot3(l, nn); ll = ti_len(l);
        hit = 1;
        if (d > 0) for (k = 0; k < 3; k++) acc[k] += F->u_tidc[j][k] * d / (ll > 1e-3f ? ll : 1e-3f);
    }
    for (k = 0; k < 3; k++) { float s = hit ? c[k] + acc[k] : c[k]; out[k] = s < 1.0f ? s : 1.0f; }
}

/* the Titanium fog factor at view depth z (world.frag: 1 - (1 - min(1, z / Z)) x the camera's factor) and its ramp
 * texel coordinate */
static float tifog_ff(float z)
{
    const float Z = G.F.u_tifog[1], W = G.F.u_tifog[3];
    float fz = z > 1.0f ? z : 1.0f;
    return 1.0f - (Z > 0.0f ? 1.0f - (fz / Z < 1.0f ? fz / Z : 1.0f) : 1.0f) * W;
}
static float ramp_s(float ff) { return (0.5f + 255.0f * ff) / 256.0f; }

/* ---- output */
static gl12_vtx *g_lines;   /* the wireframe's edges, drawn after the draw's fills */
static int g_nlines, g_caplines;
static gl12_key g_lkey;
static float g_dord = 2.0f;  /* the triangle's draw-order depth (NDC z; 2: its own depths) */

static void put_line(const gl12_vtx *a, const gl12_vtx *b, const float col[4])
{
    if (g_nlines + 2 > g_caplines) {
        int nc = g_caplines ? g_caplines * 2 : 256;
        gl12_vtx *q = realloc(g_lines, (size_t)nc * sizeof *q);
        if (!q) return;
        g_lines = q; g_caplines = nc;
    }
    g_lines[g_nlines] = *a; g_lines[g_nlines + 1] = *b;
    memcpy(g_lines[g_nlines].c, col, 4 * sizeof(float));
    memcpy(g_lines[g_nlines + 1].c, col, 4 * sizeof(float));
    g_nlines += 2;
}

static void key_init(gl12_key *k)
{
    memset(k, 0, sizeof *k);
    k->lod = -1;
    k->blend = -1;
}

static void out_vtx(gl12_vtx *d, const wv *s, const float col[4])
{
    memset(d, 0, sizeof *d);
    memcpy(d->p, s->clip, sizeof d->p);
    memcpy(d->c, col, sizeof d->c);
    d->t0[3] = 1.0f; d->t1[3] = 1.0f;
}

static void lerp_vtx(gl12_vtx *o, const gl12_vtx *a, const gl12_vtx *b, float t)
{
    int c;
    for (c = 0; c < 4; c++) {
        o->p[c] = a->p[c] + (b->p[c] - a->p[c]) * t;
        o->c[c] = a->c[c] + (b->c[c] - a->c[c]) * t;
        o->t0[c] = a->t0[c] + (b->t0[c] - a->t0[c]) * t;
        o->t1[c] = a->t1[c] + (b->t1[c] - a->t1[c]) * t;
    }
}
/* triangles out: with a draw-order depth (g_dord), each clipped at the near plane here (its own z) and then sent with
 * z = dord x w - every pixel at that depth, as world.frag's gl_FragDepth */
static void batch(const gl12_key *k, const gl12_vtx *v, int n)
{
    int t;
    if (!(g_dord < 1.5f && G.F.u_dos_order != 0)) { gl12_batch(k, v, n); return; }
    for (t = 0; t + 2 < n; t += 3) {
        gl12_vtx p[8];
        int m = 0, i, j;
        for (i = 0; i < 3; i++) {
            const gl12_vtx *a = &v[t + i], *b = &v[t + (i + 1) % 3];
            float da = a->p[2] + a->p[3], db = b->p[2] + b->p[3];
            if (da >= 0.0f) p[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f)) {   /* (a fixed order: shared edges cut alike) */
                const gl12_vtx *l = da < db ? a : b, *h = l == a ? b : a;
                float dl = l->p[2] + l->p[3], dh = h->p[2] + h->p[3];
                lerp_vtx(&p[m++], l, h, dl / (dl - dh));
            }
        }
        for (i = 0; i < m; i++) p[i].p[2] = g_dord * p[i].p[3];
        for (j = 1; j + 1 < m; j++) {
            gl12_vtx q[3];
            q[0] = p[0]; q[1] = p[j]; q[2] = p[j + 1];
            gl12_batch(k, q, 3);
        }
    }
}

/* the part of a convex polygon (n corners; z each corner's view depth, linear in clip space as every attribute sent)
 * with lo <= z < hi, into out / oz; its corner count */
static int depth_clip(const gl12_vtx *v, const float *z, int n, float lo, float hi, gl12_vtx *out, float *oz)
{
    gl12_vtx poly[12], tmp[12];
    float pz[12], tz[12];
    int s, i;
    memcpy(poly, v, (size_t)n * sizeof *v);
    memcpy(pz, z, (size_t)n * sizeof *z);
    for (s = 0; s < 2 && n >= 3; s++) {
        const float lim = s == 0 ? lo : hi;
        int m = 0;
        if ((s == 0 && lo <= -1e29f) || (s == 1 && hi >= 1e29f)) continue;
        for (i = 0; i < n && m < 10; i++) {
            int j = (i + 1) % n, ia = s == 0 ? pz[i] >= lim : pz[i] < lim, ib = s == 0 ? pz[j] >= lim : pz[j] < lim;
            if (ia) { tmp[m] = poly[i]; tz[m++] = pz[i]; }
            if (ia != ib) {
                const int l = pz[i] < pz[j] ? i : j, h = l == i ? j : i;
                lerp_vtx(&tmp[m], &poly[l], &poly[h], (lim - pz[l]) / (pz[h] - pz[l]));
                tz[m++] = lim;
            }
        }
        n = m;
        memcpy(poly, tmp, (size_t)n * sizeof *poly);
        memcpy(pz, tz, (size_t)n * sizeof *pz);
    }
    if (n >= 3) { memcpy(out, poly, (size_t)n * sizeof *out); memcpy(oz, pz, (size_t)n * sizeof *oz); }
    return n >= 3 ? n : 0;
}
static void put_fan(const gl12_key *k, const gl12_vtx *p, int n)
{
    int i;
    for (i = 1; i + 1 < n; i++) {
        gl12_vtx t3[3];
        t3[0] = p[0]; t3[1] = p[i]; t3[2] = p[i + 1];
        batch(k, t3, 3);
    }
}

/* the Titanium per-pixel fog: linear in the view depth up to Z (the ramp's coordinate from each corner's depth - GL's
 * perspective interpolation keeps it exact), 1 beyond (the triangle split at Z) */
static void tifog_split(const gl12_key *k, const gl12_vtx *v, const float *z)
{
    const float Z = G.F.u_tifog[1], W = G.F.u_tifog[3];
    gl12_vtx p[12];
    float pz[12];
    int n, i;
    if (Z <= 0.0f) {
        gl12_vtx q[3];
        memcpy(q, v, sizeof q);
        for (i = 0; i < 3; i++) { q[i].t1[0] = ramp_s(tifog_ff(1.0f)); q[i].t1[3] = 1.0f; }
        batch(k, q, 3);
        return;
    }
    if ((n = depth_clip(v, z, 3, -1e30f, Z, p, pz)) != 0) {   /* (unclamped, linear: a corner behind the eye included) */
        for (i = 0; i < n; i++) { p[i].t1[0] = ramp_s(1.0f - W + W * pz[i] / Z); p[i].t1[3] = 1.0f; }
        put_fan(k, p, n);
    }
    if ((n = depth_clip(v, z, 3, Z, 1e30f, p, pz)) != 0) {
        for (i = 0; i < n; i++) { p[i].t1[0] = ramp_s(tifog_ff(Z)); p[i].t1[3] = 1.0f; }
        put_fan(k, p, n);
    }
}

/* ---- screen-space polygons (the DOS horizon and the fallbacks): NDC x, y, z and a scalar, cut along its iso-lines */
typedef struct { float x, y, z, s; } sv;

static int clip_s(const sv *in, int n, sv *out, float lim, int keep_greater)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        const sv *a = &in[i], *b = &in[(i + 1) % n];
        int ia = keep_greater ? a->s >= lim : a->s < lim, ib = keep_greater ? b->s >= lim : b->s < lim;
        if (ia) out[m++] = *a;
        if (ia != ib) {
            const sv *p = a->s < b->s ? a : b, *q = p == a ? b : a;
            float t = (lim - p->s) / (q->s - p->s);
            sv *o = &out[m++];
            o->x = p->x + (q->x - p->x) * t; o->y = p->y + (q->y - p->y) * t; o->z = p->z + (q->z - p->z) * t; o->s = lim;
        }
    }
    return m;
}

static gl12_key g_ekey;   /* the wireframe's edge bands (deferred: drawn after the draw's fills) */
static gl12_vtx *g_edges;
static int g_nedges, g_capedges;
/* ENHANCED's horizon (u_dos_tc): every colour mixed toward the fog colour, c g + fog (1 - g) */
static int g_post;
static float g_post_g;

static void post_mix(float c[4])
{
    int q;
    if (!g_post) return;
    for (q = 0; q < 3; q++) c[q] = G.F.u_fog_color[q] + (c[q] - G.F.u_fog_color[q]) * g_post_g;
}

/* a convex polygon (NDC, w 1; or each corner's own w when ws) in one colour (stipple: 0, or 1 / 2 the pixels with x + y
 * even / odd), or each corner its own colour (cols) */
static void emit_poly_x(const gl12_key *k0, const sv *p, int n, const float col[4], const float (*cols)[4], const float *ws, int stipple)
{
    gl12_key k = *k0;
    int i;
    if (k0 == &g_ekey) {   /* deferred */
        for (i = 1; i + 1 < n; i++) {
            const sv *e[3] = {&p[0], &p[i], &p[i + 1]};
            int c;
            if (g_nedges + 3 > g_capedges) {
                int nc = g_capedges ? g_capedges * 2 : 768;
                gl12_vtx *q = realloc(g_edges, (size_t)nc * sizeof *q);
                if (!q) return;
                g_edges = q; g_capedges = nc;
            }
            for (c = 0; c < 3; c++) {
                gl12_vtx *t = &g_edges[g_nedges++];
                memset(t, 0, sizeof *t);
                t->p[0] = e[c]->x; t->p[1] = e[c]->y; t->p[2] = e[c]->z; t->p[3] = 1.0f;
                memcpy(t->c, col, sizeof t->c);
                t->t0[3] = t->t1[3] = 1.0f;
            }
        }
        return;
    }
    k.stipple = stipple;
    for (i = 1; i + 1 < n; i++) {
        gl12_vtx t[3];
        const int ix[3] = {0, i, i + 1};
        int c;
        for (c = 0; c < 3; c++) {
            const sv *e = &p[ix[c]];
            const float w = ws ? ws[ix[c]] : 1.0f;
            memset(&t[c], 0, sizeof t[c]);
            t[c].p[0] = e->x * w; t[c].p[1] = e->y * w; t[c].p[2] = e->z * w; t[c].p[3] = w;
            memcpy(t[c].c, cols ? cols[ix[c]] : col, sizeof t[c].c);
            post_mix(t[c].c);
            t[c].t0[3] = t[c].t1[3] = 1.0f;
        }
        batch(&k, t, 3);
    }
}
static void emit_poly(const gl12_key *k0, const sv *p, int n, const float col[4], int stipple) { emit_poly_x(k0, p, n, col, NULL, NULL, stipple); }
/* each corner's colour from its scalar: the palette blended between m and m + 1 (the piece's corners all within) */
static void emit_poly_blend(const gl12_key *k0, const sv *p, int n, int m)
{
    float cols[24][4];
    int i;
    for (i = 0; i < n && i < 24; i++) {
        float f = p[i].s - (float)m;
        pal_mix(m, f < 0 ? 0 : f > 1 ? 1 : f, cols[i]);
    }
    emit_poly_x(k0, p, n < 24 ? n : 24, NULL, (const float (*)[4])cols, NULL, 0);
}

/* the dithered fill of a polygon whose palette index runs screen-linearly with s (the cuts at s = j / 2): mode 0 (DOS
 * Gouraud, s = c + 0.5): [n, n + 0.5) flat n, [n + 0.5, n + 1) the checkerboard of n + 1 on the pixels of parity `even`
 * (1: x + y even) and n on the others; mode 1 (the horizon band): [n, n + 0.5) the checkerboard, [n + 0.5, n + 1) flat
 * n + 1; cap: the largest index */
static void dither_fill(const gl12_key *k, const sv *p, int n, int mode, int even, int cap)
{
    float lo = 1e30f, hi = -1e30f;
    int i, j0, j1, j;
    for (i = 0; i < n; i++) { if (p[i].s < lo) lo = p[i].s; if (p[i].s > hi) hi = p[i].s; }
    j0 = (int)floorf(lo * 2.0f); j1 = (int)floorf(hi * 2.0f);
    if (j1 - j0 > 600) j0 = j1 - 600;
    for (j = j0; j <= j1; j++) {
        sv a[24], b[24];
        int m = n, nn = j >> 1, odd = j & 1, chk, i0, i1;
        float c0[4], c1[4];
        memcpy(a, p, (size_t)n * sizeof *p);
        if (j > j0) { m = clip_s(a, m, b, (float)j * 0.5f, 1); memcpy(a, b, (size_t)m * sizeof *a); }
        if (j < j1 && m >= 3) { m = clip_s(a, m, b, (float)(j + 1) * 0.5f, 0); memcpy(a, b, (size_t)m * sizeof *a); }
        if (m < 3) continue;
        chk = mode == 0 ? odd : !odd;
        i0 = nn; i1 = nn + 1;
        if (mode == 1 && odd) i0 = nn + 1;
        if (i0 > cap) i0 = cap;
        if (i1 > cap) i1 = cap;
        dos_colour((float)(i0 & 255), c0);
        if (!chk || i0 == i1) { emit_poly(k, a, m, c0, 0); continue; }
        dos_colour((float)(i1 & 255), c1);
        emit_poly(k, a, m, c1, even ? 1 : 2);
        emit_poly(k, a, m, c0, even ? 2 : 1);
    }
}
/* the smooth fill (dos_shading / dos_sky = smooth, ENHANCED): the palette blended, cut at whole indices */
static void blend_fill(const gl12_key *k, const sv *p, int n)
{
    float lo = 1e30f, hi = -1e30f;
    int i, j0, j1, j;
    for (i = 0; i < n; i++) { if (p[i].s < lo) lo = p[i].s; if (p[i].s > hi) hi = p[i].s; }
    j0 = (int)floorf(lo); j1 = (int)floorf(hi);
    if (j1 - j0 > 300) j0 = j1 - 300;
    for (j = j0; j <= j1; j++) {
        sv a[24], b[24];
        int m = n;
        memcpy(a, p, (size_t)n * sizeof *p);
        if (j > j0) { m = clip_s(a, m, b, (float)j, 1); memcpy(a, b, (size_t)m * sizeof *a); }
        if (j < j1 && m >= 3) { m = clip_s(a, m, b, (float)(j + 1), 0); memcpy(a, b, (size_t)m * sizeof *a); }
        if (m >= 3) emit_poly_blend(k, a, m, j);
    }
}

/* ---- the DOS horizon fill (world.frag u_horizon; MW2.EXE 0x3c000): the viewport cut at the horizon, the band's
 * iso-lines; window coordinates in, NDC out. ENHANCED (u_dos_tc): the ground fogged at the depth its view ray meets the
 * ground plane, the band blended, the sky and band mixed toward the fog by u_tifog.z */
static void horizon(void)
{
    const rb_world_frame *F = &G.F;
    const float vx = (float)G.vp[0], vy = (float)G.vp[1], vw = (float)G.vp[2], vh = (float)G.vp[3];
    const int tc = F->u_dos_tc != 0, tfog = tc && F->u_tifog[0] > 0.5f;
    float X[4] = {vx, vx + vw, vx + vw, vx}, Y[4] = {vy, vy, vy + vh, vy + vh};
    sv q[4], g[24], s[24];
    int i, ng, ns;
    float col[4];
    gl12_key k;
    key_init(&k);
    k.no_depth_write = 1;
    k.depth_func = GL_ALWAYS;
    g_dord = 2.0f;
    for (i = 0; i < 4; i++) {
        q[i].x = (X[i] - vx) / vw * 2.0f - 1.0f; q[i].y = (Y[i] - vy) / vh * 2.0f - 1.0f; q[i].z = 0.999f;
        q[i].s = F->u_hz_r[1] * (X[i] - F->u_hz_c[0]) / F->u_hz_c[2] + F->u_hz_u[1] * (Y[i] - F->u_hz_c[1]) / F->u_hz_c[3] + F->u_hz_f[1];
    }
    ng = clip_s(q, 4, g, 0.0f, 0);   /* ray.y < 0: the ground (0xEF) */
    dos_colour(239.0f, col);
    if (ng >= 3 && !tfog) emit_poly(&k, g, ng, col, 0);
    else if (ng >= 3) {
        /* gz = eye height / -ray.y (1 / gz screen-linear): each corner sent at w = gz, so GL's perspective interpolation
         * keeps the colour linear in gz; beyond Z (ray.y > -eye / Z) the fog alone */
        const float Zf = F->u_tifog[1], eye = F->u_hz_eye;
        sv n1[24], n2[24];
        int m1 = Zf > 0.0f ? clip_s(g, ng, n1, -eye / Zf, 0) : 0, m2;
        float cols[24][4], ws[24], fogc[4] = {F->u_fog_color[0], F->u_fog_color[1], F->u_fog_color[2], 1.0f};
        if (Zf <= 0.0f) { float ff = tifog_ff(1e30f); for (i = 0; i < 3; i++) col[i] += (F->u_fog_color[i] - col[i]) * ff; emit_poly(&k, g, ng, col, 0); }
        else {
            for (i = 0; i < m1 && i < 24; i++) {
                float gz = eye / (-n1[i].s > 1e-9f ? -n1[i].s : 1e-9f), ff = tifog_ff(gz);
                int c;
                ws[i] = gz;
                for (c = 0; c < 3; c++) cols[i][c] = col[c] + (F->u_fog_color[c] - col[c]) * ff;
                cols[i][3] = 1.0f;
            }
            if (m1 >= 3) emit_poly_x(&k, n1, m1, NULL, (const float (*)[4])cols, ws, 0);
            m2 = clip_s(g, ng, n2, -eye / Zf, 1);
            if (m2 >= 3) emit_poly(&k, n2, m2, fogc, 0);
        }
    }
    ns = clip_s(q, 4, s, 0.0f, 1);   /* the sky (0xE0) and its band */
    if (ns < 3) return;
    g_post = tfog; g_post_g = F->u_tifog[2];
    if (F->u_hz_band > 0.0f && fabsf(F->u_hz_u[1]) > 1e-6f && !F->u_hz_smooth && !tc) {
        /* the DOS band as world.frag paints it: the sky, then over the whole window the rows r = band - floor(d + 0.5) in
         * 0 .. band (d: pixels above the line), colour 224 + r x step (the 0x8000 start less the pair's half: the checker
         * of floor(c + 1) / floor(c + 0.5)), the brighter where (row + col) is even, row from the quad's top (the higher
         * line end's row less band): the window rows' parity plus a constant */
        const float band = F->u_hz_band, stepf = floorf(917504.0f / band) / 65536.0f;
        const float Lmax = F->u_hz_L[0] > F->u_hz_L[1] ? F->u_hz_L[0] : F->u_hz_L[1], wd = F->u_hz_x[1] - 1.0f - F->u_hz_x[0];
        sv d[24], b[24], o[24];
        int nd, nb, B;
        dos_colour(224.0f, col);
        emit_poly(&k, s, ns, col, 0);
        for (i = 0; i < 4; i++) {
            float sx = (q[i].x + 1.0f) * 0.5f * vw + vx, sy = (q[i].y + 1.0f) * 0.5f * vh + vy;
            const float G = F->u_hz_L[0] + (F->u_hz_L[1] - F->u_hz_L[0]) * (sx - 0.5f - F->u_hz_x[0]) / (wd > 1.0f ? wd : 1.0f);
            d[i] = q[i]; d[i].s = sy - (G - 0.5f);   /* (the line's rows u_hz_L, world.frag) */
        }
        nd = clip_s(d, 4, o, -0.5f, 1);
        nb = clip_s(o, nd, b, band + 0.5f, 0);
        if (nb >= 3) {
            for (i = 0; i < nb; i++) b[i].s = 224.0f + (band - b[i].s - 0.5f) * stepf;
            B = (int)band - 1 + (int)Lmax - (int)F->u_hz_x[0];
            dither_fill(&k, b, nb, 1, (B & 1) == 0, 239);
        }
    } else if (F->u_hz_band > 0.0f && fabsf(F->u_hz_u[1]) > 1e-6f) {
        float shl = F->u_hz_c[1] - F->u_hz_c[3] * ((F->u_hz_x[0] - F->u_hz_c[0]) * F->u_hz_r[1] / F->u_hz_c[2] + F->u_hz_f[1]) / F->u_hz_u[1];
        float shr = F->u_hz_c[1] - F->u_hz_c[3] * ((F->u_hz_x[1] - F->u_hz_c[0]) * F->u_hz_r[1] / F->u_hz_c[2] + F->u_hz_f[1]) / F->u_hz_u[1];
        float K = (shl > shr ? shl : shr) + F->u_hz_band;
        sv d[24], b[24], o[24];
        int nd, nb, no, A, even;
        for (i = 0; i < ns; i++) {
            float sx = (s[i].x + 1.0f) * 0.5f * vw + vx, sy = (s[i].y + 1.0f) * 0.5f * vh + vy;
            float sh = F->u_hz_c[1] - F->u_hz_c[3] * ((sx - F->u_hz_c[0]) * F->u_hz_r[1] / F->u_hz_c[2] + F->u_hz_f[1]) / F->u_hz_u[1];
            d[i] = s[i]; d[i].s = sy - sh;
        }
        dos_colour(224.0f, col);
        nd = clip_s(d, ns, o, 0.0f, 0);   /* d < 0 (in the sky part: the line itself) */
        if (nd >= 3) emit_poly(&k, o, nd, col, 0);
        nd = clip_s(d, ns, o, 0.0f, 1);
        no = clip_s(o, nd, b, F->u_hz_band, 1);   /* d >= band: the sky */
        if (no >= 3) emit_poly(&k, b, no, col, 0);
        nb = clip_s(o, nd, b, F->u_hz_band, 0);
        if (nb >= 3) {
            for (i = 0; i < nb; i++) b[i].s = 238.0f - 14.0f * b[i].s / F->u_hz_band;
            if (F->u_hz_smooth || tc) {   /* e = clamp(c + 0.25, 224, 238) */
                sv e1[24], e2[24];
                int n1, n2;
                for (i = 0; i < nb; i++) b[i].s += 0.25f;
                n1 = clip_s(b, nb, e1, 238.0f, 0);
                if (n1 >= 3) blend_fill(&k, e1, n1);
                n2 = clip_s(b, nb, e2, 238.0f, 1);
                if (n2 >= 3) { dos_colour(238.0f, col); emit_poly(&k, e2, n2, col, 0); }
            } else {
                /* the checker: (row + col) odd -> n + 1, row = floor(K - sy), col = sx - x0 at pixel centres */
                A = (int)floorf(K - 0.5f) - (int)F->u_hz_x[0];
                even = (A & 1) != 0;
                dither_fill(&k, b, nb, 1, even, 238);
            }
        }
    } else { dos_colour(224.0f, col); emit_poly(&k, s, ns, col, 0); }
    g_post = 0;
}

/* ---- the DOS look's exact fill (world.frag dos_gouraud / dos_ttex), on the CPU */

/* the polygon's DOS points and shades as dos_gouraud makes them (for shade level lev) */
typedef struct {
    int s, ok, n, m, top, ybot, discard;
    float lev;
    int V[20][2], CF[20];
    float cmin, cmax;
    int dbg;                 /* MW2_DOS_DEBUG: the first corner's DOS-vs-GL offsets painted (no discards) */
    float dbgc[4];
} dpoly;

static void dos_gvtx(const float q[4], int o[2])   /* 0x3dfb7 from GL's point: quarter pixels, + 2 >> 2 */
{
    float px = q[0] / q[3] * 0.5f * G.F.u_vp[0], py = q[1] / q[3] * 0.5f * G.F.u_vp[1];
    o[0] = (int)G.F.u_vp[0] / 2 + (((int)truncf(px * 4.0f) + 2) >> 2);
    o[1] = (int)G.F.u_vp[1] / 2 - (((int)truncf(py * 4.0f) + 2) >> 2);
}

/* world.frag dos_clip2d: the paint's 2D clip (GBL MW2.EXE 0x3dd70 -> 0x3de30), when a corner is beyond -0x3fff..0x3ffe:
 * Sutherland-Hodgman on the corner records (camera x, y, z, screen x, y, two attributes) against x <= 0x3fff, x >= -0x4000,
 * y >= -0x4000, y <= 0x3fff in that order. A cut point (0x3e3d0 / 0x3e5f0): its camera z where the camera edge meets the
 * limit's screen line (0x3e330, from the corner of the smaller z), the other screen coordinate linear in this one (from
 * the corner of the larger), its camera x, y back from the screen point and z, the attributes linear along the camera
 * axis the edge runs farther in (0x3e3d0 ties: y; 0x3e5f0 ties: x; no run: the mean) - imul / idiv throughout, 32-bit
 * wrap kept (emulated under unicorn against 0x3de30: identical on 2854 random polygons) */
static int32_t c2_w(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }
static int32_t c2_lin(int32_t o, int32_t a0, int32_t b0, int32_t a1, int32_t b1)
{
    if (b0 <= a0) return c2_w(dos_ldiv((int64_t)c2_w((int64_t)o - a0) * c2_w((int64_t)b1 - a1), c2_w((int64_t)b0 - a0)) + a1);
    return c2_w(dos_ldiv((int64_t)c2_w((int64_t)o - b0) * c2_w((int64_t)a1 - b1), c2_w((int64_t)a0 - b0)) + b1);
}
static int32_t c2_zcut(int32_t a, int32_t b, int32_t L, int isx, int32_t za, int32_t zb)
{
    int32_t bx = a, dx = c2_w((int64_t)b - a), bz = za, dz = c2_w((int64_t)zb - za), k, t;
    const int sh = isx ? G.F.u_dos_ps[0] : G.F.u_dos_ps[1];
    if (dz < 0) { dz = c2_w(-(int64_t)dz); bx = b; dx = c2_w(-(int64_t)dx); bz = zb; }
    k = isx ? c2_w((int64_t)L - (int)G.F.u_vp[0] / 2) : c2_w((int64_t)((int)G.F.u_vp[1] / 2) - L);
    t = c2_w((int64_t)c2_w(((int64_t)(int32_t)((uint32_t)k << 2) * dz) >> sh) - dx);
    if (t == 0) return bz;
    return c2_w(((int64_t)bx * dz - (int64_t)bz * dx) / t);
}
static void c2_cut(const int32_t *A, const int32_t *B, int32_t L, int isx, int32_t *o)
{
    const int32_t z = c2_zcut(isx ? A[0] : A[1], isx ? B[0] : B[1], L, isx, A[2], B[2]);
    const int32_t adx = abs(c2_w((int64_t)A[0] - B[0])), ady = abs(c2_w((int64_t)A[1] - B[1]));
    int j, by;   /* 0 x, 1 y, 2 the mean */
    o[2] = z;
    if (isx) { o[3] = L; o[4] = c2_lin(L, A[3], B[3], A[4], B[4]); }
    else { o[4] = L; o[3] = c2_lin(L, A[4], B[4], A[3], B[3]); }
    o[0] = c2_w(((int64_t)(int32_t)((uint32_t)c2_w((int64_t)o[3] - (int)G.F.u_vp[0] / 2) << 2) * z) >> G.F.u_dos_ps[0]);
    o[1] = c2_w(((int64_t)(int32_t)((uint32_t)c2_w((int64_t)((int)G.F.u_vp[1] / 2) - o[4]) << 2) * z) >> G.F.u_dos_ps[1]);
    by = isx ? (adx > ady ? 0 : ady != 0 ? 1 : 2) : (ady > adx ? 1 : adx != 0 ? 0 : 2);
    for (j = 5; j < 7; j++)
        o[j] = by == 2 ? (int32_t)(((int64_t)A[j] + B[j]) >> 1) : by == 0 ? c2_lin(o[0], A[0], B[0], A[j], B[j]) : c2_lin(o[1], A[1], B[1], A[j], B[j]);
}
static int c2_out(const int32_t *r, int e) { return e == 0 ? r[3] > 0x3fff : e == 1 ? r[3] < -0x4000 : e == 2 ? r[4] < -0x4000 : r[4] > 0x3fff; }
/* R: m records {camera x, y, z, screen x, y, attribute 0, 1}; returns the corners left */
static int dos_clip2d(int32_t R[20][7], int m)
{
    int i, e, big = 0;
    for (i = 0; i < m; i++) big |= R[i][3] < -16383 || R[i][3] > 16382 || R[i][4] < -16383 || R[i][4] > 16382;
    if (!big) return m;
    for (e = 0; e < 4 && m >= 1; e++) {
        int32_t W[20][7];
        int n = 0;
        const int32_t L = e == 0 || e == 3 ? 0x3fff : -0x4000;
        for (i = 0; i < m && i < 20; i++) {
            const int pi = (i + m - 1) % m, po = c2_out(R[pi], e), qo = c2_out(R[i], e);
            if (po != qo && n < 20) c2_cut(R[pi], R[i], L, e < 2, W[n++]);
            if (!qo && n < 20) memcpy(W[n++], R[i], sizeof W[0]);
        }
        m = n;
        memcpy(R, W, sizeof W);
    }
    return m;
}
/* 0x3e8fe's attributes at the near plane: as x and y, from the corner of the smaller z (imul / idiv) */
static void dos_nclip_a(const int A[3], const int B[3], const int32_t *Aa, const int32_t *Ba, int nr, int32_t *o)
{
    int j;
    for (j = 0; j < 2; j++) {
        if (B[2] <= A[2]) o[j] = A[2] == B[2] ? Aa[j] : c2_w(dos_ldiv((int64_t)c2_w((int64_t)Aa[j] - Ba[j]) * (nr - B[2]), A[2] - B[2]) + Ba[j]);
        else o[j] = c2_w(dos_ldiv((int64_t)c2_w((int64_t)Ba[j] - Aa[j]) * (nr - A[2]), B[2] - A[2]) + Aa[j]);
    }
}
/* 0x3c610 type 4: a corner's baked index a (16.16) as the fill takes it - ramp + i (L + 1) / 16 when a >= 48, else a */
static int32_t dos_gsc(int32_t a16, int L1) { return a16 < 0x300000 ? a16 : (int32_t)((a16 & (int32_t)0xfff00000) + (((a16 >> 16) & 15) * L1 << 12)); }

static void dpoly_make(int s, float lev, dpoly *P)
{
    const float g = (lev + 1.0f) / 16.0f;
    float Pc[18][4], C[18];
    int I[18], n = 0, m = 0, t, i, pitclip = 0, cam = 0;
    int32_t RA[18], R[20][7];   /* each corner's baked index a << 16 (the record's +0x14, 0x3e80c); the records */
    const int trust = G.F.u_dos_exact != 0, L1 = (int)floorf(lev) + 1;
    memset(P, 0, sizeof *P);
    P->s = s; P->lev = lev;
    for (t = 0; t < 16 && n < 17; t++) {
        int b = s + 3 * t, k;
        for (k = t == 0 ? 0 : 1; k < 3; k++) {
            float w4[4];
            if (k == 2 && !(ebits(b) & 2)) break;
            dpos(b + k, w4); w4[3] = 1.0f;
            mat4v(G.F.u_mvp, w4, Pc[n]);
            C[n] = dvtx((b + k) * FPV + 7) + dvtx((b + k) * FPV + 6) * g;
            RA[n] = (int32_t)((uint32_t)(int32_t)floorf(dvtx((b + k) * FPV + 7) + dvtx((b + k) * FPV + 6) + 0.5f) << 16);
            I[n] = b + k;
            n++;
        }
        if (ebits(b) & 2) break;
    }
    P->n = n;
    P->cmin = 1e9f; P->cmax = -1e9f;
    if (trust && dos_nearof(s) > 0) {   /* a polygon reaching nearer than its near plane: clipped in the camera's integers */
        int Cc[18][3], isnear = 0, k, nr = dos_nearof(s);
        for (i = 0; i < n; i++) {
            float w3[3];
            dpos(I[i], w3); dos_cam(w3, Cc[i]);
            if (Cc[i][2] < nr) isnear = 1;
            if (C[i] < P->cmin) P->cmin = C[i];
            if (C[i] > P->cmax) P->cmax = C[i];
        }
        if (isnear) {
            pitclip = 1; cam = 1;
            for (k = 0; k <= n; k++) {
                const int cur = k % n, prv = (k + n - 1) % n, bc = Cc[cur][2] < nr, bp = Cc[prv][2] < nr;
                float tt;
                if (k > 0 && bc != bp && m < 20) {   /* (0x3e8fe: x, y and the attributes from the corner of the smaller z) */
                    int qc[3];
                    int32_t a2[2], pa[2] = {RA[prv], 0}, ca[2] = {RA[cur], 0};
                    dos_nclip(Cc[prv], Cc[cur], nr, qc, &tt);
                    dos_nclip_a(Cc[prv], Cc[cur], pa, ca, nr, a2);
                    R[m][0] = qc[0]; R[m][1] = qc[1]; R[m][2] = qc[2]; dos_camscr(qc, &R[m][3]); R[m][5] = a2[0]; R[m][6] = 0;
                    m++;
                }
                if (k < n && !bc && m < 20) { R[m][0] = Cc[cur][0]; R[m][1] = Cc[cur][1]; R[m][2] = Cc[cur][2]; dos_camscr(Cc[cur], &R[m][3]); R[m][5] = RA[cur]; R[m][6] = 0; m++; }
            }
        }
    }
    for (i = 0; i < n && !pitclip; i++) {   /* the near plane (z + w >= 0), the shade carried */
        const float *a = Pc[i], *b = Pc[(i + 1) % n];
        float da = a[2] + a[3], db = b[2] + b[3];
        if (C[i] < P->cmin) P->cmin = C[i];
        if (C[i] > P->cmax) P->cmax = C[i];
        if (da >= 0.0f && m < 20) {
            float w3[3];
            int ds[2];
            dpos(I[i], w3);
            if (m == 0 && trust) {
                int dg[2];
                dos_scr(w3, ds); dos_gvtx(a, dg);
                if (g_dosdbg) {   /* (DOS_DEBUG: the offsets as the colour, no discards) */
                    float fx = 0.5f + (float)(ds[0] - dg[0]) / 64.0f, fy = 0.5f + (float)(ds[1] - dg[1]) / 64.0f;
                    P->dbg = 1;
                    P->dbgc[0] = fx < 0 ? 0 : fx > 1 ? 1 : fx; P->dbgc[1] = fy < 0 ? 0 : fy > 1 ? 1 : fy;
                    P->dbgc[2] = abs(ds[0] - dg[0]) <= 3 && abs(ds[1] - dg[1]) <= 3 ? 1.0f : 0.0f; P->dbgc[3] = 1.0f;
                    P->ok = 0;
                    return;
                }
            } else if (trust) dos_scr(w3, ds);
            else dos_gvtx(a, ds);
            if (trust) { int cc[3]; dos_cam(w3, cc); R[m][0] = cc[0]; R[m][1] = cc[1]; R[m][2] = cc[2]; }
            R[m][3] = ds[0]; R[m][4] = ds[1];
            R[m][5] = (int32_t)floorf(C[i] * 65536.0f + 0.5f); R[m][6] = 0;
            m++;
        }
        if ((da >= 0.0f) != (db >= 0.0f) && m < 20) {
            float tt = da / (da - db), q[4];
            int c;
            for (c = 0; c < 4; c++) q[c] = fmad(tt, b[c] - a[c], a[c]);   /* (mix, fused as the reference's) */
            dos_gvtx(q, &R[m][3]);
            R[m][0] = R[m][1] = 0; R[m][2] = 1;   /* (GL's cut: no camera integers - left out of the 2D clip) */
            R[m][5] = (int32_t)floorf((C[i] + (C[(i + 1) % n] - C[i]) * tt) * 65536.0f + 0.5f); R[m][6] = 0;
            m++;
        }
    }
    if (trust && m >= 3 && cam) m = dos_clip2d(R, m);
    if (trust && m >= 3 && !cam && m == n) {   /* (not near-clipped: the records' attribute the baked index) */
        for (i = 0; i < m; i++) R[i][5] = RA[i];
        m = dos_clip2d(R, m); cam = 1;
    }
    for (i = 0; i < m; i++) { P->V[i][0] = R[i][3]; P->V[i][1] = R[i][4]; P->CF[i] = cam ? dos_gsc(R[i][5], L1) : R[i][5]; }
    P->m = m;
    if (m >= 3) {
        P->ok = 1;
        P->top = 0; P->ybot = P->V[0][1];
        for (i = 0; i < m; i++) { if (P->V[i][1] <= P->V[P->top][1]) P->top = i; if (P->V[i][1] > P->ybot) P->ybot = P->V[i][1]; }
        P->discard = trust && (m == n || pitclip);
    }
}

/* world.frag dos_xstep: an edge's x step trunc((dx << 16) / rows) modulo 2^32, in parts (no overflow of dx << 16) */
static unsigned dos_xstep(int dx, int cnt)
{
    const int qd = dos_idiv(dx, cnt), rd = dx - qd * cnt;
    const int sf = abs(rd) < 32768 ? dos_idiv(rd * 65536, cnt) : (int)truncf((float)rd * 65536.0f / (float)cnt);
    return (unsigned)qd * 65536u + (unsigned)sf;
}
/* one chain (dir -1 backward, + 1 forward) at DOS row y: (x, c) in 16.16 (dos_gchain) */
static void dos_gchain(const dpoly *P, int dir, int y, int out[2])
{
    const int m = P->m, top = P->top, ybot = P->ybot;
    int a = top, b = (a + dir + m) % m, r0 = P->V[top][1], cnt, yq, g;
    unsigned t;
    for (g = 0; g < 20 && P->V[b][1] == P->V[a][1] && b != top; g++) { a = b; b = (a + dir + m) % m; }
    cnt = P->V[b][1] - P->V[a][1]; yq = y == ybot && y > r0 ? y - 1 : y;
    for (g = 0; g < 20 && yq >= r0 + cnt && b != top; g++) { r0 += cnt; a = b; b = (a + dir + m) % m; cnt = P->V[b][1] - P->V[a][1]; if (cnt == 0) cnt = 1; }
    if (cnt == 0) cnt = 1;
    t = (unsigned)(y - r0);
    out[0] = (int)((unsigned)P->V[a][0] * 65536u + 0x8000u + t * dos_xstep(P->V[b][0] - P->V[a][0], cnt));
    out[1] = (int)((unsigned)(P->CF[a] + 0x8000) + t * (unsigned)dos_idiv(P->CF[b] - P->CF[a], cnt));
}
/* DOS row y's span: its left end x0, w (the last pixel - the first), the shade at the left end and its step per pair */
typedef struct { int x0, w, cl, st, r; } drow;
static void dos_row(const dpoly *P, int y, drow *R)
{
    int e1[2], e2[2];
    dos_gchain(P, -1, y, e1); dos_gchain(P, 1, y, e2);
    if (e2[0] <= e1[0]) { int t0 = e1[0], t1 = e1[1]; e1[0] = e2[0]; e1[1] = e2[1]; e2[0] = t0; e2[1] = t1; }   /* 0x5a1d5 */
    R->x0 = e1[0] >> 16; R->w = (e2[0] >> 16) - R->x0;
    R->st = R->w != 0 ? dos_idiv(e2[1] - e1[1], (R->w >> 1) > 1 ? (R->w >> 1) : 1) : 0;
    R->cl = e1[1];
    R->r = y - P->V[P->top][1];
}

/* the textured types 5 / 6's texel walk (dos_ttex): the polygon's DOS points (their camera integers) and (u q, v q, q) or
 * (u, v), for every row */
typedef struct { int s, ok, persp, n, top, ybot; int V[20][2]; double A[20][3]; } tpoly;
static void tpoly_make(int s, int persp, tpoly *T)
{
    int t, n = 0, i, nc = 0, nr = dos_nearof(s), isnear = 0, k;
    int Cc[18][3];
    int32_t Uc[18][2], R[20][7];   /* the corners' texel u, v << 16 (the records' +0x14 / +0x18, 0x3e80c); the records */
    memset(T, 0, sizeof *T);
    T->s = s; T->persp = persp;
    for (t = 0; t < 16; t++) {
        int b = s + 3 * t, c;
        for (c = t == 0 ? 0 : 1; c < 3; c++) {
            float w4[4], q[4];
            if (c == 2 && !(ebits(b) & 2)) break;
            dpos(b + c, w4); w4[3] = 1.0f;
            mat4v(G.F.u_mvp, w4, q);
            if (nc >= 18 || (nr == 0 && q[2] + q[3] < 0.0f)) return;
            dos_cam(w4, Cc[nc]);
            Uc[nc][0] = (int32_t)((uint32_t)(int32_t)floorf(dvtx((b + c) * FPV + 6) + 0.5f) << 16);
            Uc[nc][1] = (int32_t)((uint32_t)(int32_t)floorf(dvtx((b + c) * FPV + 7) + 0.5f) << 16);
            if (Cc[nc][2] < (nr > 1 ? nr : 1)) isnear = 1;
            nc++;
        }
        if (ebits(b) & 2) break;
    }
    if (isnear && nr == 0) return;
    /* the corners in front, and on the near plane where an edge crosses it (0x3e8fe: x, y, u, v, imul / idiv) */
    for (k = 0; k <= nc && nc >= 3; k++) {
        const int cur = k % nc, prv = (k + nc - 1) % nc, n1 = nr > 1 ? nr : 1, bc = Cc[cur][2] < n1, bp = Cc[prv][2] < n1;
        int cc[3], pass;
        int32_t uu[2];
        float tt;
        for (pass = 0; pass < 2; pass++) {
            if (pass == 0) { if (!(k > 0 && bc != bp) || n >= 18) continue; dos_nclip(Cc[prv], Cc[cur], n1, cc, &tt); dos_nclip_a(Cc[prv], Cc[cur], Uc[prv], Uc[cur], n1, uu); }
            else { if (!(k < nc && !bc) || n >= 18) continue; memcpy(cc, Cc[cur], sizeof cc); memcpy(uu, Uc[cur], sizeof uu); }
            R[n][0] = cc[0]; R[n][1] = cc[1]; R[n][2] = cc[2]; dos_camscr(cc, &R[n][3]); R[n][5] = uu[0]; R[n][6] = uu[1];
            n++;
        }
    }
    if (n >= 3) n = dos_clip2d(R, n);   /* the 2D clip: the cut's z and u, v as the original's records */
    for (i = 0; i < n; i++) {
        T->V[i][0] = R[i][3]; T->V[i][1] = R[i][4];
        if (persp) { const float qq = 1.0f / (float)(R[i][2] > 1 ? R[i][2] : 1); T->A[i][0] = (float)R[i][5] * qq; T->A[i][1] = (float)R[i][6] * qq; T->A[i][2] = qq; }
        else { T->A[i][0] = R[i][5]; T->A[i][1] = R[i][6]; T->A[i][2] = 0; }
    }
    if (n < 3) return;
    T->n = n; T->ok = 1;
    T->ybot = T->V[0][1];
    for (i = 0; i < n; i++) { if (T->V[i][1] <= T->V[T->top][1]) T->top = i; if (T->V[i][1] > T->ybot) T->ybot = T->V[i][1]; }
}
static void tedge(const tpoly *T, int dir, int y, int o[4])   /* dos_gedge: a, b, rows into the edge, its row count */
{
    const int m = T->n, top = T->top, ybot = T->ybot;
    int a = top, b = (a + dir + m) % m, r0 = T->V[top][1], cnt, yq, g;
    for (g = 0; g < 20 && T->V[b][1] == T->V[a][1] && b != top; g++) { a = b; b = (a + dir + m) % m; }
    cnt = T->V[b][1] - T->V[a][1]; yq = y == ybot && y > r0 ? y - 1 : y;
    for (g = 0; g < 20 && yq >= r0 + cnt && b != top; g++) { r0 += cnt; a = b; b = (a + dir + m) % m; cnt = T->V[b][1] - T->V[a][1]; if (cnt == 0) cnt = 1; }
    if (cnt == 0) cnt = 1;
    o[0] = a; o[1] = b; o[2] = y - r0; o[3] = cnt;
}
/* the row's walk: xL, w and the ends' (u, v[, q]) as the shader keeps them (floats) */
typedef struct { int xL, w; float a1[3], a2[3]; } trow;
static void ttex_row(const tpoly *T, int y, trow *R)
{
    int e1[4], e2[4], x1, x2, c;
    float a1[3], a2[3];
    tedge(T, -1, y, e1); tedge(T, 1, y, e2);
    x1 = (int)((unsigned)T->V[e1[0]][0] * 65536u + 0x8000u + (unsigned)e1[2] * dos_xstep(T->V[e1[1]][0] - T->V[e1[0]][0], e1[3]));
    x2 = (int)((unsigned)T->V[e2[0]][0] * 65536u + 0x8000u + (unsigned)e2[2] * dos_xstep(T->V[e2[1]][0] - T->V[e2[0]][0], e2[3]));
    for (c = 0; c < 3; c++) {
        a1[c] = (float)T->A[e1[0]][c] + (float)e1[2] * ((float)T->A[e1[1]][c] - (float)T->A[e1[0]][c]) / (float)e1[3];
        a2[c] = (float)T->A[e2[0]][c] + (float)e2[2] * ((float)T->A[e2[1]][c] - (float)T->A[e2[0]][c]) / (float)e2[3];
    }
    if (x2 <= x1) { int ti = x1; x1 = x2; x2 = ti; for (c = 0; c < 3; c++) { float tf = a1[c]; a1[c] = a2[c]; a2[c] = tf; } }
    R->xL = x1 >> 16; R->w = (x2 >> 16) - R->xL + 1;
    memcpy(R->a1, a1, sizeof a1); memcpy(R->a2, a2, sizeof a2);
}
/* pixel k's texel coordinates (16.16, before the masks) in a run where they step linearly: k0 .. k1 (inclusive) with
 * u = base + (k - k0) x step; returns k1 */
static int ttex_run(const tpoly *T, const trow *R, int k, int kmax, int base[2], int step[2])
{
    const int w = R->w;
    if (!T->persp) {
        int c;
        for (c = 0; c < 2; c++) { step[c] = (int)truncf((R->a2[c] - R->a1[c]) / (float)w); base[c] = (int)R->a1[c] + 0x8000 + k * step[c]; }
        return kmax;
    } else {
        const int r = k >> 4, j = k & 15, nr = w >> 4;
        float d16[3], p0[3];
        int u0[2], u1[2], c, end;
        for (c = 0; c < 3; c++) { d16[c] = 16.0f * (R->a2[c] - R->a1[c]) / (float)w; p0[c] = R->a1[c] + (float)r * d16[c]; }
        for (c = 0; c < 2; c++) u0[c] = (int)truncf(p0[c] / p0[2]) + 0x8000;
        if (r < nr) {
            for (c = 0; c < 2; c++) { float p1 = p0[c] + d16[c], q1 = p0[2] + d16[2]; u1[c] = (int)truncf(p1 / q1) + 0x8000; step[c] = (u1[c] - u0[c]) >> 4; }
            end = r * 16 + 15;
        } else {
            const int rem = w & 15;
            for (c = 0; c < 2; c++) { u1[c] = (int)truncf(R->a2[c] / R->a2[2]) + 0x8000; step[c] = (int)truncf((float)(u1[c] - u0[c]) / (float)(rem > 1 ? rem : 1)); }
            end = w - 1;
        }
        for (c = 0; c < 2; c++) base[c] = u0[c] + j * step[c];
        return end < kmax ? end : kmax;
    }
}

/* ---- a triangle's GL coverage, row by row, and its interpolants (screen-affine planes of a / w and 1 / w) */
typedef struct { double a, b, c; } plane;   /* a X + b Y + c (window coordinates) */
static double pl_at(const plane *p, double x, double y) { return p->a * x + p->b * y + p->c; }
typedef struct {
    double X[10], Y[10];     /* the clipped polygon, window coordinates */
    int n;
    plane iw, zn, u, v, zv;  /* 1 / w, z / w (NDC z), u / w, v / w, (view depth) / w */
    double ymin, ymax;
} tcov;

static void plane3(const double X[3], const double Y[3], const double V[3], plane *p)
{
    double d = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (fabs(d) < 1e-12) { p->a = p->b = 0; p->c = V[0]; return; }
    p->a = ((V[1] - V[0]) * (Y[2] - Y[0]) - (V[2] - V[0]) * (Y[1] - Y[0])) / d;
    p->b = ((X[1] - X[0]) * (V[2] - V[0]) - (X[2] - X[0]) * (V[1] - V[0])) / d;
    p->c = V[0] - p->a * X[0] - p->b * Y[0];
}
/* 0: nothing to draw */
static int tcov_make(const wv *v[3], tcov *T)
{
    double P[10][8];   /* clip x y z w, u, v, view depth */
    double Q[10][8];
    int n = 0, m, i, k, s;
    const double vx = G.vp[0], vy = G.vp[1], vw = G.vp[2], vh = G.vp[3];
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4; k++) P[i][k] = v[i]->clip[k];
        P[i][4] = v[i]->uv[0]; P[i][5] = v[i]->uv[1]; P[i][6] = -v[i]->viewpos[2]; P[i][7] = 0;
    }
    n = 3;
    for (s = 0; s < 6 && n >= 3; s++) {   /* z >= -w, z <= w, x / y within +-w */
        m = 0;
        for (i = 0; i < n && m < 10; i++) {
            const double *a = P[i], *b = P[(i + 1) % n];
            double da, db;
            switch (s) {
            case 0: da = a[2] + a[3]; db = b[2] + b[3]; break;
            case 1: da = a[3] - a[2]; db = b[3] - b[2]; break;
            case 2: da = a[0] + a[3]; db = b[0] + b[3]; break;
            case 3: da = a[3] - a[0]; db = b[3] - b[0]; break;
            case 4: da = a[1] + a[3]; db = b[1] + b[3]; break;
            default: da = a[3] - a[1]; db = b[3] - b[1]; break;
            }
            if (da >= 0) memcpy(Q[m++], a, sizeof Q[0]);
            if ((da >= 0) != (db >= 0) && m < 10) { double t = da / (da - db); for (k = 0; k < 8; k++) Q[m][k] = a[k] + (b[k] - a[k]) * t; m++; }
        }
        n = m;
        memcpy(P, Q, (size_t)n * sizeof P[0]);
    }
    if (n < 3) return 0;
    T->n = n; T->ymin = 1e30; T->ymax = -1e30;
    for (i = 0; i < n; i++) {
        double w = P[i][3] > 1e-12 ? P[i][3] : 1e-12;
        T->X[i] = (P[i][0] / w * 0.5 + 0.5) * vw + vx; T->Y[i] = (P[i][1] / w * 0.5 + 0.5) * vh + vy;
        if (T->Y[i] < T->ymin) T->ymin = T->Y[i];
        if (T->Y[i] > T->ymax) T->ymax = T->Y[i];
    }
    {   /* the planes from the largest triangle of the clipped polygon's corners */
        int a, b, c, ba = 0, bb = 1, bc = 2;
        double best = -1, X[3], Y[3], V[3];
        for (a = 0; a < n; a++) for (b = a + 1; b < n; b++) for (c = b + 1; c < n; c++) {
            double d = fabs((T->X[b] - T->X[a]) * (T->Y[c] - T->Y[a]) - (T->X[c] - T->X[a]) * (T->Y[b] - T->Y[a]));
            if (d > best) { best = d; ba = a; bb = b; bc = c; }
        }
        if (best < 1e-9) return 0;   /* (no area: GL draws no pixel) */
        X[0] = T->X[ba]; X[1] = T->X[bb]; X[2] = T->X[bc]; Y[0] = T->Y[ba]; Y[1] = T->Y[bb]; Y[2] = T->Y[bc];
#define PL(dst, expr) do { int q_; for (q_ = 0; q_ < 3; q_++) { const int ix_ = q_ == 0 ? ba : q_ == 1 ? bb : bc; const double *R_ = P[ix_]; V[q_] = (expr); } plane3(X, Y, V, dst); } while (0)
        PL(&T->iw, 1.0 / R_[3]);
        PL(&T->zn, R_[2] / R_[3]);
        PL(&T->u, R_[4] / R_[3]);
        PL(&T->v, R_[5] / R_[3]);
        PL(&T->zv, R_[6] / R_[3]);
#undef PL
    }
    return 1;
}
/* row py's covered pixels (centres inside, left edges in): 0 none, else *x0 .. *x1 */
static int tcov_row(const tcov *T, int py, int *x0, int *x1)
{
    const double yc = (double)py + 0.5;
    double xl = 1e30, xr = -1e30;
    int i;
    if (yc < T->ymin || yc >= T->ymax) return 0;
    for (i = 0; i < T->n; i++) {
        const int j = (i + 1) % T->n;
        double ya = T->Y[i], yb = T->Y[j], x;
        if (ya == yb) continue;
        if ((yc < ya) == (yc < yb)) continue;
        x = T->X[i] + (T->X[j] - T->X[i]) * (yc - ya) / (yb - ya);
        if (x < xl) xl = x;
        if (x > xr) xr = x;
    }
    if (xr <= xl) return 0;
    *x0 = (int)ceil(xl - 0.5); *x1 = (int)ceil(xr - 0.5) - 1;
    return *x1 >= *x0;
}

/* ---- the runs: one-row quads */
static gl12_key g_k;            /* the draw's base key */
static gl12_key g_pkey;         /* the DOS palette pair texture (unit 0: the DOS table's GV_DOSPAIR, s repeating) */
static int g_pal_ok, g_tex_ok;

/* pixels x0 .. x1 of window row py: the left (index 0) and right (1) values at the run's outer edges, on the row's centre
 * line. W: the quad's corners at that w (the triangle's own: GL interpolates perspective-correctly as on the triangle;
 * NULL: w 1). phase >= 0: unit 0's s from the window position, the palette pair checkerboard (column 1 where x + y +
 * phase is odd) */
static void row_quad(const gl12_key *k, int x0, int x1, int py, const float zn[2], const float *W, float (*col)[4], float (*t0)[4], float (*t1)[4], int phase)
{
    static const int QI[6] = {0, 1, 2, 0, 2, 3};
    const float vx = (float)G.vp[0], vy = (float)G.vp[1], vw = (float)G.vp[2], vh = (float)G.vp[3];
    const float X[4] = {(float)x0, (float)(x1 + 1), (float)(x1 + 1), (float)x0}, Y[4] = {(float)py, (float)py, (float)(py + 1), (float)(py + 1)};
    const int side[4] = {0, 1, 1, 0};
    gl12_vtx q[6];
    int i;
    for (i = 0; i < 6; i++) {
        const int c = QI[i], sd = side[c];
        const float w = W ? W[sd] : 1.0f;
        gl12_vtx *o = &q[i];
        memset(o, 0, sizeof *o);
        o->p[0] = ((X[c] - vx) / vw * 2.0f - 1.0f) * w; o->p[1] = ((Y[c] - vy) / vh * 2.0f - 1.0f) * w; o->p[2] = zn[sd] * w; o->p[3] = w;
        if (col) memcpy(o->c, col[sd], sizeof o->c); else o->c[0] = o->c[1] = o->c[2] = o->c[3] = 1.0f;
        if (t0) memcpy(o->t0, t0[sd], sizeof o->t0); else o->t0[3] = 1.0f;
        if (phase >= 0) o->t0[0] = (X[c] + Y[c] + (float)phase - 0.5f) * 0.5f;
        if (t1) memcpy(o->t1, t1[sd], sizeof o->t1); else o->t1[3] = 1.0f;
    }
    gl12_batch(k, q, 6);
}

/* a palette index (or the checkerboard of lo and hi = lo + 1) on pixels x0 .. x1 of row py, through the pair texture */
static void pal_run(int x0, int x1, int py, const float zn[2], int lo, int hi, int phase)
{
    float t0[2][4];
    int s;
    for (s = 0; s < 2; s++) { t0[s][0] = 0.25f; t0[s][1] = ((float)(lo & 255) + 0.5f) / 256.0f; t0[s][2] = 0; t0[s][3] = 1.0f; }
    row_quad(&g_pkey, x0, x1, py, zn, NULL, NULL, t0, NULL, hi != lo ? phase : -1);
}

/* what a triangle's DOS pixels show */
enum { DM_FLAT, DM_DITHER, DM_TEX, DM_TC_FLAT, DM_TC_LF, DM_TC_GOUR, DM_SMOOTH, DM_TC_TEX, DM_DBG };
typedef struct {
    int mode, idx, base, ptype;
    float A, fd;           /* ENHANCED: lf = A - max(z, 0) / fd (fd 0: none), clamped 0..15 */
    int fog;               /* ENHANCED: the Titanium fog */
    float col[4];          /* DM_DBG */
    gl12_key key;          /* DM_TEX: GV_DOS of the level, nearest, the alpha test */
    const tpoly *tp;       /* DM_TEX with the types 5 / 6 texel walk */
} dmode;

static float lf_at(const dmode *M, double z)
{
    float lf = M->A - (M->fd > 0.0f ? (float)(z > 0.0 ? z : 0.0) / M->fd : 0.0f);
    return lf < 0.0f ? 0.0f : lf > 15.0f ? 15.0f : lf;
}
/* ENHANCED: a piece's shade key (whole levels, or eighths for the textures) and its side of the fog's full depth */
static int tc_piece(const dmode *M, double z)
{
    const int Q = M->mode == DM_TC_TEX ? 8 : 1;
    int key = M->mode == DM_TC_LF || M->mode == DM_TC_TEX ? (int)floorf(lf_at(M, z) * (float)Q) : 0;
    if (M->fog && G.F.u_tifog[1] > 0.0f && z >= G.F.u_tifog[1]) key += 100000;
    return key;
}

/* the pixels x0 .. x1 of row py (centre yc) of triangle T in mode M; k: DOS span data (NULL: none) - the pixel's span
 * position kfix >= 0 for all of them (a clamped segment), else px - kbase */
static void seg_out(const tcov *T, const dmode *M, const dpoly *P, const drow *R, int x0, int x1, int py, int kbase, int kfix)
{
    const double yc = (double)py + 0.5;
    const int order = g_dord < 1.5f && G.F.u_dos_order != 0;
    float zn[2];
    int s;
    if (x1 < x0) return;
    for (s = 0; s < 2; s++) {
        const double X = s ? (double)(x1 + 1) : (double)x0;
        zn[s] = order ? g_dord : (float)pl_at(&T->zn, X, yc);
    }
    switch (M->mode) {
    case DM_FLAT: pal_run(x0, x1, py, zn, M->idx, M->idx, -1); return;
    case DM_DBG: {
        float c2[2][4];
        gl12_key k = g_k;
        k.tex = 0;
        memcpy(c2[0], M->col, sizeof c2[0]); memcpy(c2[1], M->col, sizeof c2[1]);
        row_quad(&k, x0, x1, py, zn, NULL, c2, NULL, NULL, -1);
        return;
    }
    case DM_DITHER: {   /* pixel k of the span on its row r: (cl + (k >> 1) st + (k + r even ? 0x7fff : 0)) >> 16 */
        if (kfix >= 0) {
            const unsigned val = (unsigned)R->cl + (unsigned)(kfix >> 1) * (unsigned)R->st;
            const int idx = (int)(((val + (((kfix + R->r) & 1) == 0 ? 0x7fffu : 0u)) >> 16) & 0xffu);
            pal_run(x0, x1, py, zn, idx, idx, -1);
            return;
        }
        {
            const int c = R->r - kbase, phase = (c + 1 + py) & 1;   /* hi where (px + c) is even */
            int px = x0;
            while (px <= x1) {
                const int k = px - kbase, j = k >> 1;
                const unsigned val = (unsigned)R->cl + (unsigned)j * (unsigned)R->st;
                const int lo = (int)((val >> 16) & 0xffu), hi = (int)(((val + 0x7fffu) >> 16) & 0xffu);
                int j1 = j + 1, pe;
                float zr[2];
                while (kbase + 2 * j1 <= x1) {
                    const unsigned v2 = (unsigned)R->cl + (unsigned)j1 * (unsigned)R->st;
                    if ((int)((v2 >> 16) & 0xffu) != lo || (int)(((v2 + 0x7fffu) >> 16) & 0xffu) != hi) break;
                    j1++;
                }
                pe = kbase + 2 * j1 - 1;
                if (pe > x1) pe = x1;
                zr[0] = order ? g_dord : (float)pl_at(&T->zn, (double)px, yc);
                zr[1] = order ? g_dord : (float)pl_at(&T->zn, (double)(pe + 1), yc);
                pal_run(px, pe, py, zr, lo, hi, phase);
                px = pe + 1;
            }
        }
        return;
    }
    case DM_TEX: {
        const float tsx = G.D.u_texsize[0] > 0 ? G.D.u_texsize[0] : 1.0f, tsy = G.D.u_texsize[1] > 0 ? G.D.u_texsize[1] : 1.0f;
        if (M->tp) {   /* the types 5 / 6: the original's texel walk, linear in runs */
            const tpoly *TP = M->tp;
            trow TR;
            const int y0 = (int)G.F.u_vp[1] - 1 - (py - (int)G.F.u_vpo[1]);
            int y = y0 < TP->V[TP->top][1] ? TP->V[TP->top][1] : y0 > TP->ybot ? TP->ybot : y0, px = x0;
            const long long PX = 65536LL * (long long)tsx, PY = 65536LL * (long long)tsy;
            ttex_row(TP, y, &TR);
            while (px <= x1) {
                int k = px - (int)G.F.u_vpo[0] - TR.xL, kk = k < 0 ? 0 : k > TR.w - 1 ? TR.w - 1 : k, end, base[2], step[2], pe, c;
                float t0[2][4], zr[2];
                if (k != kk) {   /* clamped: one texel over the pixels that clamp to the same end */
                    pe = k < 0 ? x1 < px - k - 1 ? x1 : px - k - 1 : x1;
                    ttex_run(TP, &TR, kk, kk, base, step);
                    step[0] = step[1] = 0;
                } else {
                    int kmax = kk + (x1 - px);
                    if (kmax > TR.w - 1) kmax = TR.w - 1;
                    end = ttex_run(TP, &TR, kk, kmax, base, step);
                    pe = px + (end - kk);
                }
                for (c = 0; c < 2; c++) {   /* (whole periods taken off: the masks wrap) */
                    long long b = base[c], P2 = c ? PY : PX;
                    b -= (b >= 0 ? b / P2 : (b - P2 + 1) / P2) * P2;
                    t0[0][c] = (float)(((double)b - 0.5 * step[c] + 0.5) / 65536.0 / (c ? tsy : tsx));
                    t0[1][c] = (float)(((double)b + ((double)(pe - px) + 0.5) * step[c] + 0.5) / 65536.0 / (c ? tsy : tsx));
                }
                t0[0][2] = t0[1][2] = 0; t0[0][3] = t0[1][3] = 1.0f;
                zr[0] = order ? g_dord : (float)pl_at(&T->zn, (double)px, yc);
                zr[1] = order ? g_dord : (float)pl_at(&T->zn, (double)(pe + 1), yc);
                row_quad(&M->key, px, pe, py, zr, NULL, NULL, t0, NULL, -1);
                px = pe + 1;
            }
            return;
        }
        {   /* GL's perspective texels: (u / w, v / w, 0, 1 / w) per corner, w 1 */
            float t0[2][4];
            for (s = 0; s < 2; s++) {
                const double X = s ? (double)(x1 + 1) : (double)x0;
                t0[s][0] = (float)(pl_at(&T->u, X, yc) / tsx); t0[s][1] = (float)(pl_at(&T->v, X, yc) / tsy); t0[s][2] = 0;
                t0[s][3] = (float)pl_at(&T->iw, X, yc);
            }
            row_quad(&M->key, x0, x1, py, zn, NULL, NULL, t0, NULL, -1);
        }
        return;
    }
    default: break;
    }
    /* the true-colour modes (ENHANCED, dos_shading=smooth): pieces of one shade key / fog side, the quads at the triangle's
     * own w, colours and the fog coordinate per corner */
    {
        const int tc = M->mode != DM_SMOOTH;
        int px = x0;
        while (px <= x1) {
            int pe = px, key0 = tc ? tc_piece(M, pl_at(&T->zv, px + 0.5, yc) / pl_at(&T->iw, px + 0.5, yc)) : 0;
            if (tc && (M->fd > 0.0f || M->fog)) {   /* the piece's end: the last pixel with the same key (binary search: z is monotonic in x) */
                int lo = px, hi = x1;
                if (tc_piece(M, pl_at(&T->zv, x1 + 0.5, yc) / pl_at(&T->iw, x1 + 0.5, yc)) == key0) lo = x1;
                else while (hi - lo > 1) {
                    const int mid = (lo + hi) / 2;
                    if (tc_piece(M, pl_at(&T->zv, mid + 0.5, yc) / pl_at(&T->iw, mid + 0.5, yc)) == key0) lo = mid; else hi = mid;
                }
                pe = lo;
            } else pe = x1;
            if (M->mode == DM_TC_GOUR || M->mode == DM_SMOOTH) {   /* within the piece: runs of pixel pairs of one e0 */
                int qx = px;
                while (qx <= pe) {
                    const int k = kfix >= 0 ? kfix : qx - kbase;
                    float e, ee[2];
                    int e0, qe, i2;
                    {
                        const unsigned val = (unsigned)R->cl + (unsigned)(k >> 1) * (unsigned)R->st;
                        e = (float)(int)val / 65536.0f - 0.25f;
                        e = e < P->cmin ? P->cmin : e > P->cmax ? P->cmax : e;
                    }
                    e0 = (int)floorf(e);
                    qe = qx;
                    if (kfix < 0) {
                        while (qe + 1 <= pe) {
                            const int k2 = qe + 1 - kbase;
                            const unsigned v2 = (unsigned)R->cl + (unsigned)(k2 >> 1) * (unsigned)R->st;
                            float e2 = (float)(int)v2 / 65536.0f - 0.25f;
                            e2 = e2 < P->cmin ? P->cmin : e2 > P->cmax ? P->cmax : e2;
                            if ((int)floorf(e2) != e0) break;
                            qe++;
                        }
                    } else qe = pe;
                    for (i2 = 0; i2 < 2; i2++) {   /* e at the run's two ends, extended to its outer edges */
                        const int kk = kfix >= 0 ? kfix : (i2 ? qe : qx) - kbase;
                        const unsigned v2 = (unsigned)R->cl + (unsigned)(kk >> 1) * (unsigned)R->st;
                        float e2 = (float)(int)v2 / 65536.0f - 0.25f;
                        ee[i2] = e2 < P->cmin ? P->cmin : e2 > P->cmax ? P->cmax : e2;
                    }
                    {
                        float col[2][4], t1[2][4], W[2], zr[2], d = qe > qx ? (ee[1] - ee[0]) / (float)(qe - qx) : 0.0f;
                        int sd;
                        for (sd = 0; sd < 2; sd++) {
                            const double X = sd ? (double)(qe + 1) : (double)qx;
                            float ev = (sd ? ee[1] + 0.5f * d : ee[0] - 0.5f * d) - (float)e0;
                            double iw = pl_at(&T->iw, X, yc);
                            W[sd] = iw > 1e-12 ? (float)(1.0 / iw) : 1.0f;
                            pal_mix(e0, ev, col[sd]);
                            zr[sd] = order ? g_dord : (float)pl_at(&T->zn, X, yc);
                            t1[sd][0] = M->fog ? ramp_s(tifog_ff(pl_at(&T->zv, X, yc) / (iw > 1e-12 ? iw : 1e-12))) : 0; t1[sd][1] = t1[sd][2] = 0; t1[sd][3] = 1.0f;
                        }
                        {
                            gl12_key k2 = g_k;
                            k2.tex = 0;
                            if (M->fog) { k2.fog = GF_RAMP; gl12_fog_table(G.F.u_fog_color, GF_RAMP); }
                            row_quad(&k2, qx, qe, py, zr, tc ? W : NULL, col, NULL, t1, -1);
                        }
                    }
                    qx = qe + 1;
                }
                px = pe + 1;
                continue;
            }
            {
                float col[2][4], t0[2][4], t1[2][4], W[2], zr[2];
                gl12_key k2 = g_k;
                int sd;
                const float lfm = lf_at(M, pl_at(&T->zv, (px + pe + 1) * 0.5, yc) / pl_at(&T->iw, (px + pe + 1) * 0.5, yc));
                if (M->mode == DM_TC_TEX) {
                    const int l0 = lfm >= 15.0f ? 15 : (int)floorf(lfm), q = l0 >= 15 ? 0 : (int)floorf((lfm - (float)l0) * 8.0f);
                    k2.var = GV_DOSTC; k2.param = l0 * 8 + (q > 7 ? 7 : q); k2.filt = GL_LINEAR;
                    k2.alpha_func = GL_GEQUAL; k2.alpha_ref = 0.5f;
                } else k2.tex = 0;
                if (M->fog) { k2.fog = GF_RAMP; gl12_fog_table(G.F.u_fog_color, GF_RAMP); }
                for (sd = 0; sd < 2; sd++) {
                    const double X = sd ? (double)(pe + 1) : (double)px;
                    const double iw = pl_at(&T->iw, X, yc), z = pl_at(&T->zv, X, yc) / (iw > 1e-12 ? iw : 1e-12);
                    W[sd] = iw > 1e-12 ? (float)(1.0 / iw) : 1.0f;
                    zr[sd] = order ? g_dord : (float)pl_at(&T->zn, X, yc);
                    if (M->mode == DM_TC_FLAT) dos_colour((float)M->idx, col[sd]);
                    else if (M->mode == DM_TC_LF) {   /* kind 1: the ramp's colours n, n + 1 at the unrounded level (extended past the
                                                     * piece's centres to its edges: linear in z) */
                        const int l0 = lfm >= 15.0f ? 14 : (int)floorf(lfm) > 14 ? 14 : (int)floorf(lfm);
                        float lf = M->A - (M->fd > 0.0f ? (float)(z > 0.0 ? z : 0.0) / M->fd : 0.0f);
                        if (lfm <= 0.0f) lf = 0.0f;
                        if (lfm >= 15.0f) lf = 15.0f;
                        pal_mix(M->base + l0, lf - (float)l0, col[sd]);
                    } else col[sd][0] = col[sd][1] = col[sd][2] = col[sd][3] = 1.0f;
                    col[sd][3] = 1.0f;
                    t0[sd][0] = (float)(pl_at(&T->u, X, yc) / (iw > 1e-12 ? iw : 1e-12) / (G.D.u_texsize[0] > 0 ? G.D.u_texsize[0] : 1.0f));
                    t0[sd][1] = (float)(pl_at(&T->v, X, yc) / (iw > 1e-12 ? iw : 1e-12) / (G.D.u_texsize[1] > 0 ? G.D.u_texsize[1] : 1.0f));
                    t0[sd][2] = 0; t0[sd][3] = 1.0f;
                    t1[sd][0] = M->fog ? ramp_s(tifog_ff(z)) : 0; t1[sd][1] = t1[sd][2] = 0; t1[sd][3] = 1.0f;
                    if (M->fog && G.F.u_tifog[1] > 0.0f && tc_piece(M, z) >= 100000) t1[sd][0] = ramp_s(tifog_ff(G.F.u_tifog[1]));
                }
                row_quad(&k2, px, pe, py, zr, W, col, t0, t1, -1);
            }
            px = pe + 1;
        }
    }
}

/* a triangle of a DOS polygon through the span walk: its GL coverage (as the vertex stage placed it) row by row, cut to the
 * DOS spans when the shader discards outside them (P->discard); outside the span the shader takes its end (clamped) */
static void dos_tri_spans(const wv *v[3], const dmode *M, const dpoly *P)
{
    tcov T;
    const int vx = G.vp[0], vy = G.vp[1], vw = G.vp[2], vh = G.vp[3];
    const int vph = (int)G.F.u_vp[1], vpo_x = (int)G.F.u_vpo[0], vpo_y = (int)G.F.u_vpo[1];
    int ylo, yhi, py, cx0 = vx, cx1 = vx + vw - 1;
    if (!tcov_make(v, &T)) return;
    ylo = (int)ceil(T.ymin - 0.5); yhi = (int)ceil(T.ymax - 0.5) - 1;
    if (ylo < vy) ylo = vy;
    if (yhi > vy + vh - 1) yhi = vy + vh - 1;
    if (G.sc_on) {
        if (ylo < G.sc[1]) ylo = G.sc[1];
        if (yhi > G.sc[1] + G.sc[3] - 1) yhi = G.sc[1] + G.sc[3] - 1;
        if (cx0 < G.sc[0]) cx0 = G.sc[0];
        if (cx1 > G.sc[0] + G.sc[2] - 1) cx1 = G.sc[0] + G.sc[2] - 1;
    }
    for (py = ylo; py <= yhi; py++) {
        int gx0, gx1;
        if (!tcov_row(&T, py, &gx0, &gx1)) continue;
        if (gx0 < cx0) gx0 = cx0;
        if (gx1 > cx1) gx1 = cx1;
        if (gx1 < gx0) continue;
        if (P && P->ok) {
            const int y0 = vph - 1 - (py - vpo_y), y = y0 < P->V[P->top][1] ? P->V[P->top][1] : y0 > P->ybot ? P->ybot : y0;
            drow R;
            int kL, kR;
            if (P->discard && (y0 != y || P->ybot == P->V[P->top][1])) continue;   /* (one row: nothing, world.frag) */
            dos_row(P, y, &R);
            kL = vpo_x + R.x0; kR = kL + R.w;
            if (P->discard) {
                if (gx0 < kL) gx0 = kL;
                if (gx1 > kR) gx1 = kR;
                seg_out(&T, M, P, &R, gx0, gx1, py, kL, -1);
            } else {
                seg_out(&T, M, P, &R, gx0, gx1 < kL - 1 ? gx1 : kL - 1, py, kL, 0);
                seg_out(&T, M, P, &R, gx0 > kL ? gx0 : kL, gx1 < kR ? gx1 : kR, py, kL, -1);
                seg_out(&T, M, P, &R, gx0 > kR + 1 ? gx0 : kR + 1, gx1, py, kL, R.w > 0 ? R.w : 0);
            }
        } else seg_out(&T, M, NULL, NULL, gx0, gx1, py, 0, -1);
    }
}

/* ---- one triangle of the world program */
static dpoly g_dp;      /* the last DOS polygon's span data (s, lev) */
static tpoly g_tp;      /* the last textured type 5 / 6 polygon's walk */

static const dpoly *dpoly_get(int s, float lev)
{
    if (g_dp.s != s || g_dp.lev != lev || g_dp.s < 0) dpoly_make(s, lev, &g_dp);
    return &g_dp;
}

/* the DOS Gouraud fill of a triangle without the polygon's span data (world.frag: the shade interpolated, screen parity):
 * cut along its iso-lines in screen space (the checkerboards through polygon stipples) */
static void dos_gouraud_tri(const wv *v[3], int lev)
{
    const float g = ((float)lev + 1.0f) / 16.0f;
    float P[3][5], Q[6][5];
    int n = 0, i, k;
    sv s[8];
    gl12_key key = g_k;
    for (i = 0; i < 3; i++) { memcpy(P[i], v[i]->clip, 4 * sizeof(float)); P[i][4] = v[i]->uv[1] + v[i]->uv[0] * g; }
    for (i = 0; i < 3; i++) {   /* z + w >= 0 */
        const float *a = P[i], *b = P[(i + 1) % 3];
        float da = a[2] + a[3], db = b[2] + b[3];
        if (da >= 0) memcpy(Q[n++], a, sizeof Q[0]);
        if ((da >= 0) != (db >= 0)) { float t = da / (da - db); for (k = 0; k < 5; k++) Q[n][k] = a[k] + (b[k] - a[k]) * t; n++; }
    }
    if (n < 3) return;
    for (i = 0; i < n; i++) {
        float w = Q[i][3];
        if (w <= 1e-9f) return;
        s[i].x = Q[i][0] / w; s[i].y = Q[i][1] / w; s[i].z = Q[i][2] / w; s[i].s = Q[i][4] + 0.5f;
    }
    key.tex = 0;
    if (G.F.u_dos_smooth || G.F.u_dos_tc) {   /* e = cc - 0.25 */
        for (i = 0; i < n; i++) s[i].s -= 0.25f;
        blend_fill(&key, s, n);
        return;
    }
    dither_fill(&key, s, n, 0, 1, 1 << 20);   /* (k + r = window x + y: n + 1 on the pixels with x + y even) */
}

/* a DOS flat-coloured triangle through GL's coverage (the exact spans off / not applicable): palette index idx */
static void dos_flat(const wv *v[3], int idx)
{
    gl12_vtx o[3];
    int i;
    if (g_pal_ok) {
        const float tt = ((float)(idx < 0 ? 0 : idx > 255 ? 255 : idx) + 0.5f) / 256.0f;
        static const float wh[4] = {1, 1, 1, 1};
        for (i = 0; i < 3; i++) { out_vtx(&o[i], v[i], wh); o[i].t0[0] = 0.25f; o[i].t0[1] = tt; }
        batch(&g_pkey, o, 3);
        return;
    }
    {
        float cc[4];
        gl12_key k = g_k;
        dos_colour((float)idx, cc);
        k.tex = 0;
        for (i = 0; i < 3; i++) out_vtx(&o[i], v[i], cc);
        batch(&k, o, 3);
    }
}

/* world.frag dos_line_recs / dos_line_px: a line's records as the original lists and paints them (the walk 0x3ec8e: the
 * near plane's point once from each edge; the paint's 2D cut 0x3de30) and their pixels by count (0x3d780: one a point,
 * two 0x54fa9's line, more the flat filler 0x5a900 - nothing when every record is on one row) */
static int dos_line_recs(const float A[3], const float B[3], dpoly *P)
{
    const int nr = G.F.u_dos_order != 0 && G.F.u_dos_near > 1 ? G.F.u_dos_near : 1;
    int a[3], b[3], q[3], i, m = 0, ba, bb;
    int32_t R[20][7];
    float t;
    memset(R, 0, sizeof R);
    dos_cam(A, a); dos_cam(B, b);
    ba = a[2] < nr; bb = b[2] < nr;
    P->m = 0; P->ok = 0;
    if (ba && bb) return 0;
    if (!ba) { R[m][0] = a[0]; R[m][1] = a[1]; R[m][2] = a[2]; m++; }
    if (ba != bb) { dos_nclip(a, b, nr, q, &t); R[m][0] = q[0]; R[m][1] = q[1]; R[m][2] = q[2]; m++; }
    if (!bb) { R[m][0] = b[0]; R[m][1] = b[1]; R[m][2] = b[2]; m++; }
    if (ba != bb) { dos_nclip(b, a, nr, q, &t); R[m][0] = q[0]; R[m][1] = q[1]; R[m][2] = q[2]; m++; }
    for (i = 0; i < m; i++) { int c[3] = {R[i][0], R[i][1], R[i][2]}; dos_camscr(c, &R[i][3]); }
    m = dos_clip2d(R, m);
    for (i = 0; i < m; i++) { P->V[i][0] = R[i][3]; P->V[i][1] = R[i][4]; P->CF[i] = 0; }
    P->m = m;
    if (m >= 3) {
        P->top = 0; P->ybot = P->V[0][1];
        for (i = 0; i < m; i++) { if (P->V[i][1] <= P->V[P->top][1]) P->top = i; if (P->V[i][1] > P->ybot) P->ybot = P->V[i][1]; }
        P->ok = P->ybot != P->V[P->top][1];
    }
    return m;
}
static int dos_line_px(const dpoly *P, int x, int y)
{
    drow R;
    if (P->m == 1) return x == P->V[0][0] && y == P->V[0][1];
    if (P->m == 2) return dos_line_on(x, y, P->V[0], P->V[1]);
    if (P->m < 3 || !P->ok || y < P->V[P->top][1] || y > P->ybot) return 0;
    dos_row(P, y, &R);
    return x >= R.x0 && x <= R.x0 + R.w;
}
/* a line's box triangle (world.frag kind 5): within its GL coverage, the original's pixels (dos_line_px) in the flat colour */
static void dos_line_tri(const wv *v[3], const dmode *M, int s)
{
    tcov T;
    const int vx = G.vp[0], vy = G.vp[1], vw = G.vp[2], vh = G.vp[3];
    const int vph = (int)G.F.u_vp[1], vpo_x = (int)G.F.u_vpo[0], vpo_y = (int)G.F.u_vpo[1];
    float A[3], B[3];
    dpoly LP;
    int ylo, yhi, py, cx0 = vx, cx1 = vx + vw - 1;
    dpos(s, A); B[0] = dvtx(s * FPV + 3); B[1] = dvtx(s * FPV + 4); B[2] = dvtx(s * FPV + 5);
    if (s < 0 || dos_line_recs(A, B, &LP) == 0 || !tcov_make(v, &T)) return;
    ylo = (int)ceil(T.ymin - 0.5); yhi = (int)ceil(T.ymax - 0.5) - 1;
    if (ylo < vy) ylo = vy;
    if (yhi > vy + vh - 1) yhi = vy + vh - 1;
    if (G.sc_on) {
        if (ylo < G.sc[1]) ylo = G.sc[1];
        if (yhi > G.sc[1] + G.sc[3] - 1) yhi = G.sc[1] + G.sc[3] - 1;
        if (cx0 < G.sc[0]) cx0 = G.sc[0];
        if (cx1 > G.sc[0] + G.sc[2] - 1) cx1 = G.sc[0] + G.sc[2] - 1;
    }
    for (py = ylo; py <= yhi; py++) {
        int gx0, gx1, x, run = -1;
        const int y = vph - 1 - (py - vpo_y);
        if (!tcov_row(&T, py, &gx0, &gx1)) continue;
        if (gx0 < cx0) gx0 = cx0;
        if (gx1 > cx1) gx1 = cx1;
        for (x = gx0; x <= gx1 + 1; x++) {
            const int on = x <= gx1 && dos_line_px(&LP, x - vpo_x, y);
            if (on && run < 0) run = x;
            if (!on && run >= 0) { seg_out(&T, M, NULL, NULL, run, x - 1, py, 0, -1); run = -1; }
        }
    }
}

static void world_tri(const wv *v0, const wv *v1, const wv *v2)
{
    const rb_world_frame *F = &G.F;
    const rb_world_draw *D = &G.D;
    const wv *v[3] = {v0, v1, v2}, *fl = v0;   /* fl: the provoking vertex (SDL_GPU's flat outputs: the first) */
    gl12_vtx o[3];
    float col[3][4];
    int i, c;
    gl12_key k = g_k;
    g_dord = F->u_dos_order != 0 && fl->dord < 1.5f ? fl->dord : 2.0f;
    if (D->u_wire[3] > 0.0f) {   /* the wireframe: the inside black, the polygon's own edges in the layer's colour */
        static const float black[4] = {0, 0, 0, 1};
        int wc = (fl->emask >> 3) & 7;
        float ec[4] = {D->u_wire[0], D->u_wire[1], D->u_wire[2], 1.0f};
        if (wc == 1) { ec[0] = 0.0f; ec[1] = 48.0f / 255.0f; ec[2] = 212.0f / 255.0f; }
        else if (wc == 2) { ec[0] = 252.0f / 255.0f; ec[1] = 24.0f / 255.0f; ec[2] = 24.0f / 255.0f; }
        else if (wc == 3) { ec[0] = 252.0f / 255.0f; ec[1] = 204.0f / 255.0f; ec[2] = 0.0f; }
        else if (wc == 4) { ec[0] = 60.0f / 255.0f; ec[1] = 0.0f; ec[2] = 0.0f; }
        k.tex = 0; k.offset_fill = 1;
        for (i = 0; i < 3; i++) out_vtx(&o[i], v[i], black);
        batch(&k, o, 3);
        if (v0->clip[3] > 1e-6f && v1->clip[3] > 1e-6f && v2->clip[3] > 1e-6f && v0->clip[2] >= -v0->clip[3] && v1->clip[2] >= -v1->clip[3] && v2->clip[2] >= -v2->clip[3]) {
            /* the shader's edge pixels (bary / fwidth(bary) <= 1): the band of the triangle within |n.x| + |n.y| pixels of
             * the edge, cut in screen space */
            const float hx = 0.5f * (float)G.vp[2], hy = 0.5f * (float)G.vp[3];
            sv T[3], A[8];
            int e;
            for (i = 0; i < 3; i++) { T[i].x = v[i]->clip[0] / v[i]->clip[3] * hx; T[i].y = v[i]->clip[1] / v[i]->clip[3] * hy; T[i].z = v[i]->clip[2] / v[i]->clip[3]; }
            for (e = 0; e < 3; e++) {
                const int a = e == 0 ? 1 : 0, b = e == 2 ? 1 : 2;
                float nx = T[b].y - T[a].y, ny = T[a].x - T[b].x, nl = sqrtf(nx * nx + ny * ny), side, lim;
                int m, q;
                if (!(fl->emask & (1 << e)) || nl < 1e-6f) continue;
                nx /= nl; ny /= nl;
                side = (T[e].x - T[a].x) * nx + (T[e].y - T[a].y) * ny;
                if (side < 0) { nx = -nx; ny = -ny; }
                lim = fabsf(nx) + fabsf(ny);
                for (q = 0; q < 3; q++) T[q].s = (T[q].x - T[a].x) * nx + (T[q].y - T[a].y) * ny;
                m = clip_s(T, 3, A, lim, 0);
                if (m < 3) continue;
                for (q = 0; q < m; q++) { A[q].x /= hx; A[q].y /= hy; }
                emit_poly(&g_ekey, A, m, ec, 0);
            }
            return;
        }
        if (fl->emask & 1) put_line(&o[1], &o[2], ec);
        if (fl->emask & 2) put_line(&o[0], &o[2], ec);
        if (fl->emask & 4) put_line(&o[0], &o[1], ec);
        return;
    }
    if (D->u_shadow) {
        static const float sh[4] = {0, 0, 0, 0.45f};
        k.tex = 0;
        for (i = 0; i < 3; i++) out_vtx(&o[i], v[i], sh);
        batch(&k, o, 3);
        return;
    }
    if (F->u_dos != 0 && g_dosprog) {   /* the DOS edition (world.frag): the DOS and ENHANCED looks */
        const int kind = (int)(fl->color[0] + 0.5f), tc = F->u_dos_tc != 0, tfog = tc && F->u_tifog[0] > 0.5f;
        const int exactcov = kind <= 2 && fl->texsel > 0 && F->u_dos_exact != 0 && g_sb && g_pal_ok;
        const float base = floorf(fl->color[1] + 0.5f), Cc = floorf(fl->color[2] + 0.5f);
        float n[3], ctr[3], wc[3], nl, Ld[3], nf[3], f, lev;
        dmode M;
        const dpoly *P = NULL;
        memset(&M, 0, sizeof M);
        M.fog = tfog;
        if (kind == 5) {   /* a line (world.frag) */
            if (!g_sb || (!tc && !g_pal_ok)) return;
            M.mode = tc ? DM_TC_FLAT : DM_FLAT; M.idx = (int)base;
            dos_line_tri(v, &M, fl->texsel - 1);
            return;
        }
        if (exactcov) {
            P = dpoly_get(fl->texsel - 1, 0.0f);
            if (P->dbg) P = NULL;   /* (DOS_DEBUG: dos_gouraud's early return - no discards) */
        }
        if (kind == 0) {
            if (tc) { M.mode = DM_TC_FLAT; M.idx = (int)base; }
            else { M.mode = DM_FLAT; M.idx = (int)base; }
            if (exactcov) { dos_tri_spans(v, &M, P); return; }
            if (!tc) { dos_flat(v, (int)base); return; }
            {   /* ENHANCED without the span walk: GL's coverage, fogged */
                float cc[4], fz[3];
                gl12_key k2 = g_k;
                dos_colour(base, cc);
                k2.tex = 0;
                for (i = 0; i < 3; i++) { out_vtx(&o[i], v[i], cc); fz[i] = -v[i]->viewpos[2]; }
                if (tfog) { k2.fog = GF_RAMP; gl12_fog_table(F->u_fog_color, GF_RAMP); tifog_split(&k2, o, fz); return; }
                batch(&k2, o, 3);
                return;
            }
        }
        if (kind == 4) {   /* a billboard's texel indices as they are (ENHANCED: bilinear, fogged) */
            static const float wh[4] = {1, 1, 1, 1};
            float fz[3];
            if (!g_tex_ok) return;
            if (tc) { k.var = GV_DOSTC; k.param = 15 * 8; k.filt = GL_LINEAR; } else { k.var = GV_DOS; k.param = 15; }
            k.alpha_func = GL_GEQUAL; k.alpha_ref = 0.5f;
            for (i = 0; i < 3; i++) { out_vtx(&o[i], v[i], wh); o[i].t0[0] = v[i]->uv[0] / D->u_texsize[0]; o[i].t0[1] = v[i]->uv[1] / D->u_texsize[1]; fz[i] = -v[i]->viewpos[2]; }
            if (tfog) { k.fog = GF_RAMP; gl12_fog_table(F->u_fog_color, GF_RAMP); tifog_split(&k, o, fz); return; }
            batch(&k, o, 3);
            return;
        }
        for (c = 0; c < 3; c++) {
            n[c] = (v0->normal[c] + v1->normal[c] + v2->normal[c]) / 3.0f;
            ctr[c] = (v0->viewpos[c] + v1->viewpos[c] + v2->viewpos[c]) / 3.0f;
            wc[c] = (v0->world[c] + v1->world[c] + v2->world[c]) / 3.0f;
        }
        nl = len3(n);
        if (F->u_dos_dir) memcpy(Ld, F->u_dos_lpos, sizeof Ld);
        else for (c = 0; c < 3; c++) Ld[c] = F->u_dos_lpos[c] - wc[c];
        { float l = len3(Ld); if (l > 0) for (c = 0; c < 3; c++) Ld[c] /= l; }
        for (c = 0; c < 3; c++) nf[c] = nl > 0 ? -n[c] / nl : 0.0f;
        if (nl > 0 && fl->dosf == 0) {   /* one-sided per pixel when the vertex stage didn't test the polygon */
            const float *m = F->u_view;
            float vn[3];
            for (c = 0; c < 3; c++) vn[c] = m[c] * nf[0] + m[4 + c] * nf[1] + m[8 + c] * nf[2];
            if (dot3(ctr, vn) >= 0.0f) return;
        }
        f = fl->dosf != 0 ? (float)(fl->dosf - 1000) : nl > 0 ? truncf(127.0f * dot3(nf, Ld)) : 127.0f;
        lev = floorf(floorf(Cc / 2.0f) * (floorf(f * (128.0f - F->u_dos_amb) / 128.0f) + F->u_dos_amb) / 1088.0f);
        if (F->u_dos_fogdist > 0.0f)
            lev -= fl->dkey >= 0.0f ? (float)(((int)fl->dkey * 16 / (int)F->u_dos_fogdist) >> 4) : floorf((-ctr[2] > 0 ? -ctr[2] : 0.0f) / F->u_dos_fogdist);
        lev = lev < 0 ? 0 : lev > 15 ? 15 : lev;
        if (tc) {   /* the same level unrounded: lf = A - z / fogdist */
            const float fli = nl > 0 ? 127.0f * dot3(nf, Ld) : 127.0f;
            M.A = floorf(Cc / 2.0f) * (fli * (128.0f - F->u_dos_amb) / 128.0f + F->u_dos_amb) / 1088.0f;
            M.fd = F->u_dos_fogdist > 0.0f ? F->u_dos_fogdist : 0.0f;
        }
        if (kind == 1) {
            if (tc) { M.mode = DM_TC_LF; M.base = (int)base; }
            else { M.mode = DM_FLAT; M.idx = (int)(base + lev); }
            if (exactcov) { dos_tri_spans(v, &M, P); return; }
            if (!tc) { dos_flat(v, (int)(base + lev)); return; }
            {   /* ENHANCED without the span walk: the colour per corner */
                gl12_key k2 = g_k;
                float fz[3];
                k2.tex = 0;
                for (i = 0; i < 3; i++) {
                    float z = -v[i]->viewpos[2], lf = M.A - (M.fd > 0 ? (z > 0 ? z : 0) / M.fd : 0.0f);
                    int l0;
                    lf = lf < 0 ? 0 : lf > 15 ? 15 : lf;
                    l0 = (int)floorf(lf) > 14 ? 14 : (int)floorf(lf);
                    pal_mix((int)base + l0, lf - (float)l0, col[i]);
                    out_vtx(&o[i], v[i], col[i]);
                    fz[i] = z;
                }
                if (tfog) { k2.fog = GF_RAMP; gl12_fog_table(F->u_fog_color, GF_RAMP); tifog_split(&k2, o, fz); return; }
                batch(&k2, o, 3);
                return;
            }
        }
        if (kind == 3) {
            const dpoly *PG = fl->texsel > 0 && g_sb ? dpoly_get(fl->texsel - 1, lev) : NULL;
            if (g_dosdbg && fl->texsel <= 0) { M.mode = DM_DBG; M.col[0] = M.col[1] = M.col[2] = 0.5f; M.col[3] = 1.0f; dos_tri_spans(v, &M, NULL); return; }
            if (PG && PG->dbg) { M.mode = DM_DBG; memcpy(M.col, PG->dbgc, sizeof M.col); dos_tri_spans(v, &M, NULL); return; }
            if (!PG || !PG->ok || !g_pal_ok) { dos_gouraud_tri(v, (int)lev); return; }
            M.mode = tc ? DM_TC_GOUR : F->u_dos_smooth ? DM_SMOOTH : DM_DITHER;
            dos_tri_spans(v, &M, PG);
            return;
        }
        if (!g_tex_ok) return;
        if (tc) {   /* ENHANCED textures: the level blended (eighths), bilinear, fogged */
            M.mode = DM_TC_TEX;
            if (exactcov) { dos_tri_spans(v, &M, P); return; }
            {
                static const float wh[4] = {1, 1, 1, 1};
                float fz[3], lf;
                int l0, q;
                k.var = GV_DOSTC; k.filt = GL_LINEAR; k.alpha_func = GL_GEQUAL; k.alpha_ref = 0.5f;
                lf = M.A - (M.fd > 0 ? (-ctr[2] > 0 ? -ctr[2] : 0) / M.fd : 0.0f);
                lf = lf < 0 ? 0 : lf > 15 ? 15 : lf;
                l0 = lf >= 15.0f ? 15 : (int)floorf(lf); q = l0 >= 15 ? 0 : (int)floorf((lf - (float)l0) * 8.0f);
                k.param = l0 * 8 + (q > 7 ? 7 : q);
                for (i = 0; i < 3; i++) { out_vtx(&o[i], v[i], wh); o[i].t0[0] = v[i]->uv[0] / D->u_texsize[0]; o[i].t0[1] = v[i]->uv[1] / D->u_texsize[1]; fz[i] = -v[i]->viewpos[2]; }
                if (tfog) { k.fog = GF_RAMP; gl12_fog_table(F->u_fog_color, GF_RAMP); tifog_split(&k, o, fz); return; }
                batch(&k, o, 3);
                return;
            }
        }
        k.var = GV_DOS; k.param = (int)lev; k.alpha_func = GL_GEQUAL; k.alpha_ref = 0.5f;
        if (exactcov) {
            const int ptype = (int)base;
            M.mode = DM_TEX; M.key = k; M.key.filt = GL_NEAREST;
            if ((ptype == 5 || ptype == 6) && fl->dtrust != 0) {
                if (g_tp.s != fl->texsel - 1 || g_tp.persp != (ptype == 5)) tpoly_make(fl->texsel - 1, ptype == 5, &g_tp);
                if (g_tp.ok) M.tp = &g_tp;
            }
            dos_tri_spans(v, &M, P);
            return;
        }
        {
            static const float wh[4] = {1, 1, 1, 1};
            for (i = 0; i < 3; i++) { out_vtx(&o[i], v[i], wh); o[i].t0[0] = v[i]->uv[0] / D->u_texsize[0]; o[i].t0[1] = v[i]->uv[1] / D->u_texsize[1]; }
            batch(&k, o, 3);
        }
        return;
    }
    /* the TITANIUM look (and the wireframe-free 3D path) */
    if (fl->ticm == 3 && ((-fl->texsel) & F->u_notex) == 0) return;
    {
        int textured = D->u_textured != 0 && D->u_mono[3] <= 0.0f && (fl->texsel >= 0 || ((-fl->texsel) & F->u_notex) == 0) && g_tex_ok, tinted = 0;
        float ds = fl->dnib > 0 ? (F->u_dsign ? 1.0f + (float)fl->dnib / 15.0f : 1.0f - (float)fl->dnib / 15.0f) : 1.0f;
        float pf[3] = {0, 0, 0}, fz[3];
        int pfade = 0;
        if (!textured && F->u_pfade > 0.0f && fl->texsel > 0 && fl->bb == 0) {   /* toward the fade's palette */
            int w = 0, h = 0, ix = fl->texsel - 1;
            const uint8_t *pt = gl12_tex_texels(G.tex[2], &w, &h);
            if (pt && ix < w) { for (c = 0; c < 3; c++) pf[c] = pt[(size_t)ix * 4 + (size_t)c] / 255.0f; pfade = 1; }
        }
        if (textured) {
            k.var = GV_PLAIN;
            if (D->u_blend != 0) { k.alpha_func = GL_GREATER; k.alpha_ref = 1.5f / 255.0f * D->u_alpha; }   /* (t.a < 0.004 discarded) */
            else { k.alpha_func = GL_GEQUAL; k.alpha_ref = 0.5f * D->u_alpha; }
            tinted = D->u_tinton != 0;
        } else k.tex = 0;
        for (i = 0; i < 3; i++) {
            const wv *s = v[i];
            float base[3], lit[3], nn = len3(s->normal), L[3], diff;
            if (D->u_mono[3] > 1.5f) memcpy(base, s->color, sizeof base);
            else if (D->u_mono[3] > 0.0f) memcpy(base, D->u_mono, sizeof base);
            else if (fl->ticm >= 2) memcpy(base, s->tic, sizeof base);
            else memcpy(base, s->color, sizeof base);
            if (textured) memcpy(base, D->u_tint, sizeof base);   /* (x the texel: GL_MODULATE) */
            else if (pfade) for (c = 0; c < 3; c++) base[c] += (pf[c] - base[c]) * F->u_pfade;
            if (F->u_point) { for (c = 0; c < 3; c++) L[c] = F->u_lpos[c] - s->world[c]; { float l = len3(L); if (l > 0) for (c = 0; c < 3; c++) L[c] /= l; } }
            else memcpy(L, F->u_light, sizeof L);
            diff = nn > 0 ? fabsf(dot3(s->normal, L) / nn) : 0.5f;
            if (D->u_unlit || fl->bb || tinted || (fl->tif & 2) || fl->ticm == 3) memcpy(lit, base, sizeof lit);
            else if (D->u_tilit == 1) {
                float tl[3];
                if (fl->ticm == 1) memcpy(tl, s->tic, sizeof tl);
                else {
                    float n3[3];
                    for (c = 0; c < 3; c++) n3[c] = (fl->tif & 1) ? s->normal[c] : nn > 0 ? s->normal[c] / nn : 0.0f;
                    ti_light(s, fl, n3, tl);
                }
                for (c = 0; c < 3; c++) lit[c] = base[c] * tl[c];
            } else if (D->u_tilit == 2) for (c = 0; c < 3; c++) lit[c] = base[c] * F->u_tignd[c];
            else { float m = (F->u_ambient + diff) * ds; if (m > 1.0f) m = 1.0f; for (c = 0; c < 3; c++) lit[c] = base[c] * m; }
            for (c = 0; c < 3; c++) col[i][c] = lit[c];
            col[i][3] = D->u_alpha;
            out_vtx(&o[i], s, col[i]);
            fz[i] = -s->viewpos[2];
            if (textured) { o[i].t0[0] = s->uv[0] / D->u_texsize[0]; o[i].t0[1] = s->uv[1] / D->u_texsize[1]; }
        }
        if (F->u_tifog[0] > 0.5f && fl->tinofog == 0) {   /* the Titanium fog, per pixel from the view depth */
            k.fog = GF_RAMP;
            gl12_fog_table(F->u_fog_color, GF_RAMP);
            tifog_split(&k, o, fz);
            return;
        }
        batch(&k, o, 3);
    }
}

void gl12_world_draw(int first, int count)
{
    static wv *vs;
    static int cap;
    size_t bytes;
    const uint8_t *b;
    int i;
    gl12_tex *t0;
    if (count > cap) {
        wv *q = realloc(vs, (size_t)count * sizeof *q);
        if (!q) return;
        vs = q; cap = count;
    }
    /* the bound buffers, vertex `first` of each at its offset */
    b = gl12_buf_ptr(G.vb[0], &bytes);
    g_v0 = b && G.voff[0] + (size_t)first * FPV * 4 <= bytes ? (const float *)(b + G.voff[0]) + (size_t)first * FPV : NULL;
    g_n0 = g_v0 ? (bytes - G.voff[0]) / (FPV * 4) - (size_t)first : 0;
    b = gl12_buf_ptr(G.vb[1], &bytes);
    g_v1 = b && G.voff[1] + (size_t)first * 16 <= bytes ? (const float *)(b + G.voff[1]) + (size_t)first * 4 : NULL;
    g_n1 = g_v1 ? (bytes - G.voff[1]) / 16 - (size_t)first : 0;
    b = gl12_buf_ptr(G.vb[2], &bytes);
    g_v2 = b && G.voff[2] + (size_t)first * 16 <= bytes ? (const float *)(b + G.voff[2]) + (size_t)first * 4 : NULL;
    g_n2 = g_v2 ? (bytes - G.voff[2]) / 16 - (size_t)first : 0;
    b = G.sb && G.sb != RB_STREAM ? gl12_buf_ptr(G.sb, &bytes) : NULL;
    g_sb = b ? (const float *)b : NULL;
    g_nsb = b ? bytes / 4 : 0;
    b = G.sb2 && G.sb2 != RB_STREAM ? gl12_buf_ptr(G.sb2, &bytes) : NULL;
    g_sb2 = b ? (const float *)b : NULL;
    g_nsb2 = b ? bytes / 4 : 0;
    g_dosprog = G.prog == RB_PROG_WORLD_DOS || G.prog == RB_PROG_WORLD_DOSDBG;
    g_dosdbg = G.prog == RB_PROG_WORLD_DOSDBG;
    g_dord = 2.0f;
    key_init(&g_k);
    t0 = gl12_T(G.tex[0]);
    g_tex_ok = t0 && t0->rgba != NULL;
    g_k.tex = G.tex[0];
    if (!g_tex_ok) {   /* (nothing bound: SDL_GPU's backend samples a white texel) */
        static rb_tex white;
        static const uint32_t W = 0xffffffffu;
        if (!white || !gl12_T(white)) { white = rb_tex_new(); rb_tex_image(white, 1, 1, &W, 0); rb_tex_filter(white, RB_NEAREST, RB_NEAREST); }
        g_k.tex = white; g_tex_ok = 1;
    }
    key_init(&g_pkey);   /* the DOS palette-pair texture (the DOS table on unit 1) */
    g_pkey.tex = G.tex[1]; g_pkey.var = GV_DOSPAIR;
    g_pal_ok = g_dosprog && dos_tab_ok();
    if (G.D.u_horizon) { horizon(); return; }
    poly_cache_reset();
    g_dp.s = -1; g_tp.s = -1;
    for (i = 0; i < count; i++) vertex_stage(G.D.u_first + i, i, &vs[i]);
    g_nlines = g_nedges = 0;
    key_init(&g_ekey);
    g_ekey.depth_func = GL_LEQUAL;
    for (i = 0; i + 2 < count; i += 3) world_tri(&vs[i], &vs[i + 1], &vs[i + 2]);
    g_dord = 2.0f;
    if (g_nedges) gl12_batch(&g_ekey, g_edges, g_nedges);   /* the wireframe's edges, over the fills (pushed back) */
    if (g_nlines) {
        key_init(&g_lkey);
        g_lkey.lines = 1;
        g_lkey.depth_func = GL_LEQUAL;
        gl12_batch(&g_lkey, g_lines, g_nlines);
    }
}

/* ---- the HUD program (hud.frag): the texel (or white) x the tint, the alpha test at 0.01, the flash blended in */
void gl12_hud_draw(int first, int count)
{
    const rb_hud_draw *H = &G.H;
    size_t bytes;
    const uint8_t *b = gl12_buf_ptr(G.vb[0], &bytes);
    const float *v;
    gl12_vtx *o;
    gl12_key k;
    gl12_tex *t = gl12_T(G.tex[0]);
    int i, n = count - count % 3, textured, flash = H->u_flash[3] > 0.0f;
    float fa = H->u_flash[3] > 1.0f ? 1.0f : H->u_flash[3];
    if (!b || G.voff[0] + (size_t)(first + n) * 16 > bytes || n <= 0) return;
    v = (const float *)(b + G.voff[0]) + (size_t)first * 4;
    o = malloc((size_t)n * sizeof *o);
    if (!o) return;
    key_init(&k);
    textured = H->u_textured != 0 && t && t->rgba;
    if (textured) {
        int sharp = H->u_sharp > 0.0f ? (int)(H->u_sharp + 0.5f) : 0;
        k.tex = G.tex[0];
        k.var = sharp > 0 ? (H->u_smooth ? GV_SHARP_SQRTA : GV_SHARP) : H->u_smooth ? GV_SQRTA : GV_PLAIN;
        k.param = sharp;
    }
    k.alpha_func = GL_GEQUAL; k.alpha_ref = 0.01f;
    if (flash && g12_units > 1) { k.env_blend = 1; memcpy(k.flash, H->u_flash, sizeof k.flash); }
    for (i = 0; i < n; i++) {
        memset(&o[i], 0, sizeof o[i]);
        o[i].p[0] = v[i * 4]; o[i].p[1] = v[i * 4 + 1]; o[i].p[2] = 0.5f; o[i].p[3] = 1.0f;
        memcpy(o[i].c, H->u_tint, sizeof o[i].c);
        if (flash && g12_units == 1) { o[i].c[0] *= 1.0f - fa; o[i].c[1] *= 1.0f - fa; o[i].c[2] *= 1.0f - fa; }
        o[i].t0[0] = v[i * 4 + 2]; o[i].t0[1] = v[i * 4 + 3]; o[i].t0[3] = 1.0f;
        o[i].t1[0] = (floorf(fa * 255.0f + 0.5f) + 0.5f) / 256.0f; o[i].t1[3] = 1.0f;
    }
    if (textured) gl12_tiled_tris(&k, G.tex[0], o, n);
    else gl12_batch(&k, o, n);
    if (flash && g12_units == 1 && G.blend == RB_BLEND_ALPHA) {   /* one unit: + flash x a, added (the texel's alpha, white) */
        for (i = 0; i < n; i++) { o[i].c[0] = H->u_flash[0] * fa; o[i].c[1] = H->u_flash[1] * fa; o[i].c[2] = H->u_flash[2] * fa; o[i].c[3] = H->u_tint[3]; }
        k.blend = 4;
        if (textured) { k.var = GV_WHITE; k.param = 0; gl12_tiled_tris(&k, G.tex[0], o, n); }
        else gl12_batch(&k, o, n);
    }
    free(o);
}
