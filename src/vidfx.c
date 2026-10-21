/* The full-screen videos' looks (see vidfx.h): CPU filters from a palette-indexed frame to the window's pixels.
 * Every per-column / per-row term (filter taps, line weights, the mask) is a table built when the sizes change, and
 * the horizontal filtering runs once per SOURCE row (200 rows), so a frame costs a few multiply-adds per output pixel.
 * CRT_CURVED: the flat CRT terms with each column's own vertical mapping, then each row rescaled across (the barrel
 * u' = u (1 + k v^2), v' = v (1 + k u^2) applied as two 1-D passes - for this small k the difference from the exact 2-D
 * mapping is a fraction of a pixel), the mask last, in screen space. */
#include "vidfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the looks' constants (tuned by eye on GBL's intro and mission clips; the gains keep the mean brightness near the
 * ORIGINAL picture's) */
#define SL_SIGMA     0.25f    /* SCANLINES: the beam's width (Gaussian sigma) in source lines */
#define SL_GAIN      1.55f    /*            and the gain making up for the dark gaps */
#define SL_SHARP     3.0f     /*            horizontally: linear between neighbours, steepened (1 = plain linear) */
#define CRT_SIGMA0   0.20f    /* CRT: the beam's sigma for a black pixel ... */
#define CRT_SIGMA1   0.38f    /*      ... and a white one (brighter lines bloom wider, closing the gaps) */
#define CRT_GAIN     1.06f
#define CRT_SHARP    1.6f
#define CRT_GLOW     0.15f    /*      the glow (the frame blurred, sigma 1.2 source pixels) added on top */
#define CRT_MASK_HI  0.70f    /*      the aperture grille's dimmed channels (3-pixel triads) on large pictures ... */
#define CRT_MASK_LO  0.86f    /*      ... and below 1200 pixels across, where the triads are coarse */
#define CRT_CURVE    0.045f   /* CRT_CURVED: the barrel term k */

struct vidfx {
    int sw, sh, ow, oh, mode;          /* what the tables were built for (mode -1: none) */
    uint8_t *src, *glow, *tmp;         /* the frame in RGB (sw x sh), blurred, scratch */
    uint8_t *ex, *exl, *gx;            /* every source row scaled across to ow: frame (RGB), its luminance bucket, glow */
    uint8_t *p1, *grow;                /* CRT_CURVED's first pass (ow x oh RGB); one output row of glow */
    int *ho, *hw;                      /* per output column: 4 taps' byte offsets in a source row and weights (sum 4096) */
    int *vx, *vw;                      /* per output row: first source row and 4 weights (SMOOTH) */
    int *l0, *dq, *gt;                 /* per output row: the line above the row's centre, the distance to it (1/64 line),
                                        * and the same in 1/256 (the glow's rows) */
    float *bx, *ay;                    /* CRT_CURVED: per column 1 + k u^2, per row 1 + k v^2 */
    int *qacc, *qstep;                 /* CRT_CURVED: per column the source line position (1/65536) and its step per row */
    uint16_t beam[65][16];             /* line weight (x256, gain folded in) by distance (1/64 line) and luminance bucket */
    uint16_t mask[3][3];               /* the mask's channel gains (x256) by column % 3 */
};

vidfx *vidfx_new(void)
{
    vidfx *v = calloc(1, sizeof *v);
    if (v) v->mode = -1;
    return v;
}
static void drop(vidfx *v)
{
    free(v->src); free(v->glow); free(v->tmp); free(v->ex); free(v->exl); free(v->gx); free(v->p1); free(v->grow);
    free(v->ho); free(v->hw); free(v->vx); free(v->vw); free(v->l0); free(v->dq); free(v->gt); free(v->bx); free(v->ay); free(v->qacc); free(v->qstep);
    v->src = v->glow = v->tmp = v->ex = v->exl = v->gx = v->p1 = v->grow = NULL;
    v->ho = v->hw = v->vx = v->vw = v->l0 = v->dq = v->gt = NULL;
    v->bx = v->ay = NULL;
    v->qacc = v->qstep = NULL;
    v->mode = -1;
}
void vidfx_free(vidfx *v) { if (v) { drop(v); free(v); } }

static int clampi(int a, int lo, int hi) { return a < lo ? lo : a > hi ? hi : a; }
static uint8_t sat(int a) { return (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a); }
static size_t zs(int a) { return (size_t)a; }

/* Mitchell-Netravali (B = C = 1/3) */
static float mitchell(float x)
{
    const float B = 1.0f / 3.0f, C = 1.0f / 3.0f;
    x = fabsf(x);
    if (x < 1.0f) return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6.0f;
    if (x < 2.0f) return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6.0f;
    return 0.0f;
}
/* 4 taps for each of n output positions over m inputs: cubic (sharp < 0) or linear steepened by sharp around the
 * midpoint (the last two taps 0). first[o]: the first tap's input index (unclamped) */
static void taps(int n, int m, float sharp, int *first, int *w)
{
    int o, k;
    for (o = 0; o < n; o++) {
        float s = ((float)o + 0.5f) * (float)m / (float)n - 0.5f;
        int i0 = (int)floorf(s);
        float t = s - (float)i0;
        if (sharp < 0) {
            float f[4], sum = 0;
            int acc = 0;
            for (k = 0; k < 4; k++) { f[k] = mitchell(t - (float)(k - 1)); sum += f[k]; }
            for (k = 0; k < 3; k++) { w[o * 4 + k] = (int)lroundf(f[k] / sum * 4096.0f); acc += w[o * 4 + k]; }
            w[o * 4 + 3] = 4096 - acc;
            first[o] = i0 - 1;
        } else {
            int b;
            t = (t - 0.5f) * sharp + 0.5f;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            b = (int)lroundf(t * 4096.0f);
            first[o] = i0; w[o * 4] = 4096 - b; w[o * 4 + 1] = b; w[o * 4 + 2] = w[o * 4 + 3] = 0;
        }
    }
}

static int build(vidfx *v, int mode, int sw, int sh, int ow, int oh)
{
    int x, y, L, k, *first;
    if (v->mode == mode && v->sw == sw && v->sh == sh && v->ow == ow && v->oh == oh) return 0;
    drop(v);
    v->src = malloc(zs(sw) * zs(sh) * 3); v->glow = malloc(zs(sw) * zs(sh) * 3); v->tmp = malloc(zs(sw) * zs(sh) * 3);
    v->ex = malloc(zs(sh) * zs(ow) * 3); v->exl = malloc(zs(sh) * zs(ow)); v->gx = malloc(zs(sh) * zs(ow) * 3);
    v->p1 = mode == VIDFX_CRT_CURVED ? malloc(zs(ow) * zs(oh) * 3) : malloc(1);
    v->grow = malloc(zs(ow) * 3);
    v->ho = malloc(sizeof(int) * 4 * zs(ow)); v->hw = malloc(sizeof(int) * 4 * zs(ow));
    v->vx = malloc(sizeof(int) * zs(oh)); v->vw = malloc(sizeof(int) * 4 * zs(oh));
    v->l0 = malloc(sizeof(int) * zs(oh)); v->dq = malloc(sizeof(int) * zs(oh)); v->gt = malloc(sizeof(int) * zs(oh));
    v->bx = malloc(sizeof(float) * zs(ow)); v->ay = malloc(sizeof(float) * zs(oh));
    v->qacc = malloc(sizeof(int) * zs(ow)); v->qstep = malloc(sizeof(int) * zs(ow));
    first = malloc(sizeof(int) * zs(ow));
    if (!v->src || !v->glow || !v->tmp || !v->ex || !v->exl || !v->gx || !v->p1 || !v->grow || !v->ho || !v->hw ||
        !v->vx || !v->vw || !v->l0 || !v->dq || !v->gt || !v->bx || !v->ay || !v->qacc || !v->qstep || !first) { free(first); drop(v); return -1; }
    taps(ow, sw, mode == VIDFX_SMOOTH ? -1.0f : mode == VIDFX_SCANLINES ? SL_SHARP : CRT_SHARP, first, v->hw);
    for (x = 0; x < ow; x++) for (k = 0; k < 4; k++) v->ho[x * 4 + k] = clampi(first[x] + k, 0, sw - 1) * 3;
    if (mode == VIDFX_ORIGINAL) for (x = 0; x < ow; x++) v->ho[x * 4] = x * sw / ow * 3;   /* nearest */
    free(first);
    taps(oh, sh, -1.0f, v->vx, v->vw);
    for (y = 0; y < oh; y++) {   /* the scanlines: line l's centre at l + 0.5 source rows */
        float q = ((float)y + 0.5f) * (float)sh / (float)oh - 0.5f, vv = (((float)y + 0.5f) / (float)oh) * 2.0f - 1.0f;
        int l = (int)floorf(q);
        v->l0[y] = l; v->dq[y] = clampi((int)lroundf((q - (float)l) * 64.0f), 0, 64);
        v->gt[y] = clampi((int)lroundf((q - (float)l) * 256.0f), 0, 256);
        v->ay[y] = 1.0f + CRT_CURVE * vv * vv;
    }
    for (x = 0; x < ow; x++) { float uu = (((float)x + 0.5f) / (float)ow) * 2.0f - 1.0f; v->bx[x] = 1.0f + CRT_CURVE * uu * uu; }
    for (L = 0; L < 16; L++) {
        float sig = mode == VIDFX_SCANLINES ? SL_SIGMA : CRT_SIGMA0 + (CRT_SIGMA1 - CRT_SIGMA0) * (float)L / 15.0f;
        float gain = mode == VIDFX_SCANLINES ? SL_GAIN : CRT_GAIN;
        for (k = 0; k <= 64; k++) {
            float d = (float)k / 64.0f / sig;
            v->beam[k][L] = (uint16_t)lroundf(expf(-0.5f * d * d) * gain * 256.0f);
        }
    }
    {
        float m = ow >= 1200 ? CRT_MASK_HI : CRT_MASK_LO, norm = 3.0f / (1.0f + 2.0f * m);
        int c, ch;
        for (c = 0; c < 3; c++) for (ch = 0; ch < 3; ch++) v->mask[c][ch] = (uint16_t)lroundf((c == ch ? 1.0f : m) * norm * 256.0f);
    }
    v->sw = sw; v->sh = sh; v->ow = ow; v->oh = oh; v->mode = mode;
    return 0;
}

/* every row of img (sw x sh RGB) across to ow with the column taps (2 or 4); lum: also each pixel's brightness bucket */
static void expand_rows(const vidfx *v, const uint8_t *img, uint8_t *dst, uint8_t *lum, int four)
{
    int y, x, ow = v->ow;
    for (y = 0; y < v->sh; y++) {
        const uint8_t *row = img + zs(y) * zs(v->sw) * 3;
        uint8_t *d = dst + zs(y) * zs(ow) * 3, *l = lum ? lum + zs(y) * zs(ow) : NULL;
        const int *o = v->ho, *w = v->hw;
        for (x = 0; x < ow; x++, d += 3, o += 4, w += 4) {
            const uint8_t *a = row + o[0], *b = row + o[1];
            int r, g, bl;
            if (four) {
                const uint8_t *c = row + o[2], *e = row + o[3];
                r = (a[0] * w[0] + b[0] * w[1] + c[0] * w[2] + e[0] * w[3] + 2048) >> 12;
                g = (a[1] * w[0] + b[1] * w[1] + c[1] * w[2] + e[1] * w[3] + 2048) >> 12;
                bl = (a[2] * w[0] + b[2] * w[1] + c[2] * w[2] + e[2] * w[3] + 2048) >> 12;
                d[0] = sat(r); d[1] = sat(g); d[2] = sat(bl);
            } else {
                d[0] = (uint8_t)((a[0] * w[0] + b[0] * w[1] + 2048) >> 12);
                d[1] = (uint8_t)((a[1] * w[0] + b[1] * w[1] + 2048) >> 12);
                d[2] = (uint8_t)((a[2] * w[0] + b[2] * w[1] + 2048) >> 12);
            }
            if (l) { int m = d[0] > d[1] ? d[0] : d[1]; m = m > d[2] ? m : d[2]; l[x] = (uint8_t)(m >> 4); }
        }
    }
}
/* the glow: the frame blurred (separable Gaussian, sigma 1.2 source pixels), then across to ow (linear: v->gx) */
static void blur(vidfx *v)
{
    static const int K[7] = {11, 50, 116, 158, 116, 50, 11};   /* sums to 512 */
    int x, y, k, c, sw = v->sw, sh = v->sh;
    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++) {
            int acc[3] = {0, 0, 0};
            for (k = 0; k < 7; k++) { const uint8_t *p = v->src + (zs(y) * zs(sw) + zs(clampi(x + k - 3, 0, sw - 1))) * 3; for (c = 0; c < 3; c++) acc[c] += p[c] * K[k]; }
            for (c = 0; c < 3; c++) v->tmp[(zs(y) * zs(sw) + zs(x)) * 3 + zs(c)] = (uint8_t)(acc[c] >> 9);
        }
    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++) {
            int acc[3] = {0, 0, 0};
            for (k = 0; k < 7; k++) { const uint8_t *p = v->tmp + (zs(clampi(y + k - 3, 0, sh - 1)) * zs(sw) + zs(x)) * 3; for (c = 0; c < 3; c++) acc[c] += p[c] * K[k]; }
            for (c = 0; c < 3; c++) v->glow[(zs(y) * zs(sw) + zs(x)) * 3 + zs(c)] = (uint8_t)(acc[c] >> 9);
        }
    {   /* across: the linear taps of a plain resample (independent of the frame's sharpened ones) */
        int ow = v->ow;
        for (y = 0; y < sh; y++) {
            const uint8_t *row = v->glow + zs(y) * zs(sw) * 3;
            uint8_t *d = v->gx + zs(y) * zs(ow) * 3;
            for (x = 0; x < ow; x++, d += 3) {
                int fx = (int)(((2 * x + 1) * sw * 128) / ow) - 128, i0 = fx >> 8, t = fx & 255;   /* (x + .5) sw / ow - .5, 1/256 */
                const uint8_t *a = row + clampi(i0, 0, sw - 1) * 3, *b = row + clampi(i0 + 1, 0, sw - 1) * 3;
                d[0] = (uint8_t)((a[0] * (256 - t) + b[0] * t) >> 8);
                d[1] = (uint8_t)((a[1] * (256 - t) + b[1] * t) >> 8);
                d[2] = (uint8_t)((a[2] * (256 - t) + b[2] * t) >> 8);
            }
        }
    }
}

void vidfx_render(vidfx *v, int mode, const uint8_t *pix, int sw, int sh, const uint8_t (*pal)[3],
                  uint8_t *out, int ow, int oh, int pitch)
{
    int x, y, i;
    if (mode < 0 || mode >= VIDFX_COUNT) mode = VIDFX_ORIGINAL;
    if (!v || !pix || !pal || !out || sw <= 0 || sh <= 0 || ow < sw || oh < sh || build(v, mode, sw, sh, ow, oh) != 0) {
        if (out && ow > 0 && oh > 0) for (y = 0; y < oh; y++) memset(out + zs(y) * zs(pitch), 0, zs(ow) * 3);
        return;
    }
    for (i = 0; i < sw * sh; i++) memcpy(v->src + zs(i) * 3, pal[pix[i]], 3);

    if (mode == VIDFX_ORIGINAL) {   /* each source pixel a block, as the shells' 640x480 stretch (x * sw / ow) */
        for (y = 0; y < oh; y++) {
            const uint8_t *row = v->src + zs(y * sh / oh) * zs(sw) * 3;
            uint8_t *d = out + zs(y) * zs(pitch);
            for (x = 0; x < ow; x++, d += 3) { const uint8_t *p = row + v->ho[x * 4]; d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; }
        }
        return;
    }
    if (mode == VIDFX_SMOOTH) {
        expand_rows(v, v->src, v->ex, NULL, 1);
        for (y = 0; y < oh; y++) {
            const uint8_t *r[4];
            const int *w = v->vw + y * 4;
            uint8_t *d = out + zs(y) * zs(pitch);
            for (i = 0; i < 4; i++) r[i] = v->ex + zs(clampi(v->vx[y] + i, 0, sh - 1)) * zs(ow) * 3;
            for (x = 0; x < ow * 3; x++) d[x] = sat((r[0][x] * w[0] + r[1][x] * w[1] + r[2][x] * w[2] + r[3][x] * w[3] + 2048) >> 12);
        }
        return;
    }
    if (mode == VIDFX_SCANLINES) {   /* the two lines nearest the row, each weighted by its distance */
        expand_rows(v, v->src, v->ex, NULL, 0);
        for (y = 0; y < oh; y++) {
            int l0 = v->l0[y], dq = v->dq[y];
            int w0 = l0 >= 0 ? v->beam[dq][0] : 0, w1 = l0 + 1 < sh ? v->beam[64 - dq][0] : 0;
            const uint8_t *r0 = v->ex + zs(clampi(l0, 0, sh - 1)) * zs(ow) * 3, *r1 = v->ex + zs(clampi(l0 + 1, 0, sh - 1)) * zs(ow) * 3;
            uint8_t *d = out + zs(y) * zs(pitch);
            for (x = 0; x < ow * 3; x++) d[x] = sat((r0[x] * w0 + r1[x] * w1 + 128) >> 8);
        }
        return;
    }
    /* CRT, CRT_CURVED */
    expand_rows(v, v->src, v->ex, v->exl, 0);
    blur(v);
    {
        const int glow = (int)lroundf(CRT_GLOW * 256.0f);
        if (mode == VIDFX_CRT) {
            for (y = 0; y < oh; y++) {
                int l0 = v->l0[y], dq = v->dq[y], gt = v->gt[y], c0 = clampi(l0, 0, sh - 1), c1 = clampi(l0 + 1, 0, sh - 1);
                const uint8_t *r0 = v->ex + zs(c0) * zs(ow) * 3, *r1 = v->ex + zs(c1) * zs(ow) * 3;
                const uint8_t *L0 = v->exl + zs(c0) * zs(ow), *L1 = v->exl + zs(c1) * zs(ow);
                const uint8_t *g0 = v->gx + zs(c0) * zs(ow) * 3, *g1 = v->gx + zs(c1) * zs(ow) * 3;
                static const uint16_t zero[16];
                const uint16_t *b0 = l0 >= 0 ? v->beam[dq] : zero, *b1 = l0 + 1 < sh ? v->beam[64 - dq] : zero;
                const int ga = (256 - gt) * glow, gb = gt * glow;
                uint8_t *d = out + zs(y) * zs(pitch);
                int ph = 0;
                for (x = 0; x < ow; x++, d += 3, r0 += 3, r1 += 3, g0 += 3, g1 += 3) {
                    int w0 = b0[L0[x]], w1 = b1[L1[x]];
                    const uint16_t *m = v->mask[ph];
                    d[0] = sat((((r0[0] * w0 + r1[0] * w1) >> 8) * m[0] >> 8) + ((g0[0] * ga + g1[0] * gb) >> 16));
                    d[1] = sat((((r0[1] * w0 + r1[1] * w1) >> 8) * m[1] >> 8) + ((g0[1] * ga + g1[1] * gb) >> 16));
                    d[2] = sat((((r0[2] * w0 + r1[2] * w1) >> 8) * m[2] >> 8) + ((g0[2] * ga + g1[2] * gb) >> 16));
                    if (++ph == 3) ph = 0;
                }
            }
            return;
        }
        /* CRT_CURVED, pass 1: each column's vertical mapping v' = v (1 + k u^2) - linear in the row, so each column keeps
         * its source position q (1/65536 line) and steps it per row -, rows outside the tube black with a 1.5-pixel soft
         * edge, the beam and the glow sampled there, into p1 */
        {
            int *acc = v->qacc, *stp = v->qstep;
            const int top = -32 * 1024, bot = (sh * 64 - 32) * 1024;   /* q = -0.5 and sh - 0.5, in 1/65536 */
            for (x = 0; x < ow; x++) {   /* q(y) = ((v(y) b + 1) sh / 2 - 0.5), v(y) = (2 y + 1) / oh - 1 */
                double b = (double)v->bx[x], q0 = ((1.0 / oh - 1.0) * b + 1.0) * sh * 0.5 - 0.5;
                acc[x] = (int)lround(q0 * 65536.0); stp[x] = (int)lround(2.0 / oh * b * sh * 0.5 * 65536.0);
            }
            for (y = 0; y < oh; y++) {
                uint8_t *d = v->p1 + zs(y) * zs(ow) * 3;
                for (x = 0; x < ow; x++, d += 3) {
                    int qa = acc[x], qi, l, dq, c0, c1, w0, w1, fade = 256, gt;
                    size_t r0o, r1o, o = zs(x) * 3;
                    const uint8_t *r0, *r1, *g0, *g1;
                    acc[x] = qa + stp[x];
                    if (qa < top || qa >= bot) { d[0] = d[1] = d[2] = 0; continue; }
                    if (qa - top < 3 * stp[x] / 2) fade = (int)((long long)(qa - top) * 512 / (3 * stp[x]));
                    else if (bot - qa < 3 * stp[x] / 2) fade = (int)((long long)(bot - qa) * 512 / (3 * stp[x]));
                    qi = (qa + 64 * 1024 * 4) >> 10; qi -= 64 * 4;   /* 1/64 line, floor */
                    l = qi >> 6; dq = qi & 63; gt = dq << 2;
                    c0 = clampi(l, 0, sh - 1); c1 = clampi(l + 1, 0, sh - 1);
                    r0o = zs(c0) * zs(ow); r1o = zs(c1) * zs(ow);
                    r0 = v->ex + r0o * 3 + o; r1 = v->ex + r1o * 3 + o;
                    g0 = v->gx + r0o * 3 + o; g1 = v->gx + r1o * 3 + o;
                    w0 = l >= 0 ? v->beam[dq][v->exl[r0o + zs(x)]] : 0;
                    w1 = l + 1 < sh ? v->beam[64 - dq][v->exl[r1o + zs(x)]] : 0;
                    {
                        int ga = (256 - gt) * glow, gb = gt * glow;
                        d[0] = sat(((((r0[0] * w0 + r1[0] * w1) >> 8) + ((g0[0] * ga + g1[0] * gb) >> 16)) * fade) >> 8);
                        d[1] = sat(((((r0[1] * w0 + r1[1] * w1) >> 8) + ((g0[1] * ga + g1[1] * gb) >> 16)) * fade) >> 8);
                        d[2] = sat(((((r0[2] * w0 + r1[2] * w1) >> 8) + ((g0[2] * ga + g1[2] * gb) >> 16)) * fade) >> 8);
                    }
                }
            }
        }
        /* pass 2: each row across by u' = u (1 + k v^2) (linear between p1's pixels, the sides' 1.5-pixel soft edge), the
         * mask */
        for (y = 0; y < oh; y++) {
            const uint8_t *row = v->p1 + zs(y) * zs(ow) * 3;
            uint8_t *d = out + zs(y) * zs(pitch);
            double a = (double)v->ay[y], cx = ow * 0.5;
            /* x' = cx + (x + .5 - cx) a - .5, in 1/65536, stepping by a */
            long long fx = llround((cx + (0.5 - cx) * a - 0.5) * 65536.0), step = llround(a * 65536.0);
            const long long lo = -32768, hi = (long long)ow * 65536 - 32768, soft = 3 * 32768;   /* -0.5, ow - 0.5, 1.5 px */
            int ph = 0;
            for (x = 0; x < ow; x++, d += 3, fx += step) {
                const uint16_t *m = v->mask[ph];
                int i0, t, fade = 256;
                const uint8_t *p, *q;
                if (++ph == 3) ph = 0;
                if (fx <= lo || fx >= hi) { d[0] = d[1] = d[2] = 0; continue; }
                if (fx - lo < soft) fade = (int)((fx - lo) * 256 / soft);
                else if (hi - fx < soft) fade = (int)((hi - fx) * 256 / soft);
                i0 = (int)(fx >> 16); t = (int)((fx >> 8) & 255);
                p = row + clampi(i0, 0, ow - 1) * 3; q = row + clampi(i0 + 1, 0, ow - 1) * 3;
                d[0] = sat((((((p[0] * (256 - t) + q[0] * t) >> 8) * m[0]) >> 8) * fade) >> 8);
                d[1] = sat((((((p[1] * (256 - t) + q[1] * t) >> 8) * m[1]) >> 8) * fade) >> 8);
                d[2] = sat((((((p[2] * (256 - t) + q[2] * t) >> 8) * m[2]) >> 8) * fade) >> 8);
            }
        }
    }
}
