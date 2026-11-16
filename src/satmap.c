/*
 * satmap.c - the satellite map (F3, radar mode 4) as GBL MW2.EXE draws it. See satmap.h.
 *
 * In mode 4 the main view's drawer (DAT_000a79b4) is 0x118e0 -> 0x11910: the 3D world is not drawn, the map is. 0x11910
 * sets up a straight-down view over the player (0x12c30: the camera at the player's x, z and at the height of the
 * descriptor's range +0x18 - 100000 cm, halved by RADAR_ZOOM_IN down to 6250 and back round - looking down (+0x10 =
 * 0x5a0000), north up; the window's cm a pixel b6704 = range / the window's width; the projection's mantissas 0x2000 /
 * +0x44 >> 3 and shifts 3 / 3), installs the map's hooks (the object test 0x124a0, the projection 0x13000, the colour
 * 0x12520, the polygon draw 0x12690), picks the units' levels of detail with DISPLAY DETAIL 6 (0x37fa0: the player's
 * own mech too), fills the window with the ground colour (0x57978, DAT_000a77ec) and walks, sorts and paints the object
 * list as the 3D view does (0x12db0(0) -> 0x3d9c0 / 0x3d830 / 0x3ec8e / 0x3d930 / 0x3dad0 / 0x3dd70 / 0x3d780), without
 * the horizon fill. Then the field-of-view wedge (0x120c0) unless the player's flags & 0x16, the nav and target sprites
 * (0x11b10: mode 4 only 0x11f60 / 0x11d80 - glview) and the range / bearing lines (0x121b0 - glview).
 *
 * The hooks:
 * - 0x124a0 (object test): a unit's object (type & 0xf00 == 0x100) whose unit has flags & 0x16 (destroyed, shut down)
 *   is left out; objects of class 0x30 and 0x70 are left out; the rest go through 0x12e50: hidden (flags 0x1000) out;
 *   depth = the z row . (centre - eye) >> 29 rounded; out when depth + r < near, depth - r > far (the range; less
 *   DAT_000a59e0 when that is negative), or the centre's |x| (|y|) beyond half the window's width (height) in cm by more
 *   than the radius (b66e8 / b66ec, b66f4 / b66f0).
 * - 0x13000 (projection): sx = ((x << 3) / b6704 + 2 >> 2) + the window's centre column, sy = (H - 1) - (((y << 3) /
 *   b6704 + 2 >> 2) + H / 2) (x, y: the camera integers of 0x3e80c with those mantissas); its outcode as 0x3ea67's.
 * - 0x12520 (colour, by the polygon's object): units (0x100) the descriptor's colour table +0x74 [side], things (0x200)
 *   [3 + side], shots (0x400) [6], class 0x40 [7], 0x50 [8], 0x80 [9] - all colour type 0, so 0x12690 fills them black
 *   and outlines them in that colour; classes 0x10 / 0x20 / 0x60 keep the polygon's own word; everything else (0x800 and
 *   the world) Gouraud by height: ((c & 0xf00) >> 4) | 0x4000 (a billboard, c & 0x7000 == 0x3000: [10] | 0x4000).
 *   GBL's DISP RADAR table: 0x0e 0x0a 0x06 | 0x0f 0x0b 0xf5 | 0x02 | 0x03 | 0xf9 | 0xff | 0xf0 | 0x01 (the wedge).
 * - 0x12690 (polygon): type 0 - 0x3c610 with colour 0 (0x5a900: black) then with c | 0x2000 (the outline: 0x54fa9 from
 *   each record back to the one before, then record 0 to the last, colour c & 0xff); type 4 - each record's shade 0x12790(z): ((range -
 *   (z >> 2) - DAT_000a59e0) << 16) / DAT_000a59e8 x 16, rounded, 0..15, in the row c & 0xf0, filled by the Gouraud filler
 *   0x5b5f7 (DAT_000a7964 = 1; its dither 0x7fff); type 3 - 0x3c420 (a billboard's bitmap: not reproduced, see below);
 *   others as 0x3c610 (type 1 flat, type 2 the outline; the textured types 5 - 7 are not reproduced: drawn flat in c &
 *   0xff, ASSUMED - none reach it in the GBL scenes checked).
 * One-record polygons are points (0x54df8, c & 0xff), two-record ones lines (0x54fa9), as 0x3d780 does for every view.
 * The fillers' rules are the ones the 3D view's exact fill uses (world.frag dos_gouraud / dos_gchain; 0x5a900 and 0x5b5f7
 * run under unicorn against them), the line 0x54fa9's (world_common.glsl dos_line_on), the paint's 2D cut 0x3de30 as
 * rb_gl12_world.c's dos_clip2d (with the map's shifts 3 / 3).
 * Not reproduced (ASSUMED): type 3 billboards (fire / explosion cards of classes 0x10 / 0x60) are left out; the levels of
 * detail of the world's objects take the WTBO +4 word as the level record's distance (wtb.h).
 */
#include "satmap.h"
#include "glr.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the descriptor's colour table (DISP RADAR, mode 4 +0x74) */
static const int SAT_COL[12] = {0x0e, 0x0a, 0x06, 0x0f, 0x0b, 0xf5, 0x02, 0x03, 0xf9, 0xff, 0xf0, 0x01};

typedef struct { const mech3d_part *p; int kind, side, order; int64_t link; } sat_obj;
typedef struct { int32_t x, y, z, sx, sy; uint8_t oc; } sat_rec;
typedef struct { int first, n, colour, key; } sat_ent;

struct satmap {
    int w, h, mw, cx, cy;
    int32_t eye[3], range, cmpp, xlo, xhi, ylo, yhi, near, far, near4, far4, hmin, hrange;
    int64_t raw[3][3], Rx[3], Ry[3];
    int64_t b8;   /* 16.16 */
    sat_obj *obj; int nobj, objcap;
    sat_rec *rec; int nrec, reccap;
    sat_ent *ent; int ne, entcap;
    int *key, *id, kcap;
    int stat[2];
    /* per object: its vertices' world integers, camera z and record */
    int32_t (*wv)[3]; int32_t *vz; int *vrec; int vcap;
};

satmap *satmap_new(void) { return calloc(1, sizeof(satmap)); }
void satmap_free(satmap *s)
{
    if (!s) return;
    free(s->obj); free(s->rec); free(s->ent); free(s->key); free(s->id); free(s->wv); free(s->vz); free(s->vrec);
    free(s);
}

static int64_t r29(int64_t v) { return (v >> 29) + ((v >> 28) & 1); }
static int64_t r27(int64_t v) { return (v >> 27) + ((v >> 26) & 1); }
static int64_t r16(int64_t v) { return (v >> 16) + ((v >> 15) & 1); }
static int64_t tdiv(int64_t n, int64_t d) { int64_t q; if (d == 0) return 0; q = (n < 0 ? -n : n) / (d < 0 ? -d : d); return (n < 0) != (d < 0) ? -q : q; }
static int32_t w32(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }

void satmap_view(satmap *s, int w, int h, int mw, int32_t px, int32_t pz, int32_t range, uint32_t zoom, int lodq,
                 int32_t hmin, int32_t hrange)
{
    int c;
    uint32_t hw = (uint32_t)(w > 1 ? w >> 1 : 1);
    if (mw < 1) mw = w > 0 ? w : 1;
    s->w = w; s->h = h; s->mw = mw;
    s->cx = ((w - 1) + 1) >> 1;   /* +0x8c: (x0 + x1 + 1) >> 1 */
    s->cy = ((h - 1) + 1) >> 1;   /* +0x90 */
    s->range = range;
    s->cmpp = range / mw > 0 ? range / mw : 1;   /* b6704 */
    s->yhi = (int32_t)(((int64_t)s->cmpp * h) / 2); s->ylo = -s->yhi;   /* b66f0 / b66f4 */
    if (mw == w) { s->xlo = -range / 2; s->xhi = -(-range / 2); }   /* b66e8 / b66ec */
    else { s->xhi = (int32_t)(((int64_t)s->cmpp * w) / 2); s->xlo = -s->xhi; }   /* (a window wider than 4:3: ASSUMED) */
    s->eye[0] = px; s->eye[1] = range; s->eye[2] = pz;
    s->hmin = hmin; s->hrange = hrange > 0 ? hrange : 1;
    if (zoom > 0x100000u) zoom = 0x100000u;
    if (zoom < 0x8000u) zoom = 0x8000u;
    s->near = (int32_t)(((uint64_t)zoom * hw) >> 17) + 1;   /* 0x3f9e0: +0x3c */
    s->far = hmin < 0 ? range - hmin : range;                 /* b66fc -> +0x40 */
    s->near4 = s->near * 4;
    s->far4 = s->far < 0x1fffffff ? s->far * 4 : 0x7fffffff;
    s->b8 = (int64_t)((uint64_t)zoom * hw) / (int64_t)((lodq > 0 ? lodq : 1) * 0xa0);   /* +0xb8 */
    glr_dos_cam_rows(0.0f, -90.0f, 0.0f, s->raw);   /* +0xc / +0x10 / +0x14 = 0, 0x5a0000, 0 */
    for (c = 0; c < 3; c++) {   /* 0x3f790: x row x 0x2000 (+0x9c), y row x (+0x44 >> 3) (+0xa0), round16 */
        s->Rx[c] = r16(0x2000 * s->raw[0][c]);
        s->Ry[c] = r16((0xffff >> 3) * s->raw[1][c]);
    }
    s->nobj = 0; s->nrec = 0; s->ne = 0;
}

void satmap_part(satmap *s, const mech3d_part *p, int kind, int side, int64_t link)
{
    if (!p || p->model.object_count < 1 || p->hidden) return;
    if (s->nobj == s->objcap) {
        sat_obj *g = realloc(s->obj, (size_t)(s->objcap = s->objcap ? 2 * s->objcap : 1024) * sizeof *g);
        if (!g) { s->objcap = s->nobj; return; }
        s->obj = g;
    }
    s->obj[s->nobj].p = p; s->obj[s->nobj].kind = kind; s->obj[s->nobj].side = side; s->obj[s->nobj].link = link;
    s->obj[s->nobj].order = s->nobj;
    s->nobj++;
}

static int obj_cmp(const void *a, const void *b)   /* the walk: the larger link first, then the later given */
{
    const sat_obj *x = a, *y = b;
    if (x->link != y->link) return x->link > y->link ? -1 : 1;
    return x->order > y->order ? -1 : x->order < y->order ? 1 : 0;
}

/* 0x13003: a record's window point and outcode */
static void project(const satmap *s, sat_rec *r)
{
    int64_t q;
    q = tdiv((int64_t)r->x * 8, s->cmpp);
    r->sx = w32(((q + 2) >> 2) + s->cx);
    q = tdiv((int64_t)r->y * 8, s->cmpp);
    r->sy = w32((int64_t)(s->h - 1) - (((q + 2) >> 2) + s->cy));
    r->oc = (uint8_t)((r->sx < 0 ? 1 : 0) | (r->sx > s->w - 1 ? 2 : 0) | (r->sy < 0 ? 4 : 0) | (r->sy > s->h - 1 ? 8 : 0));
}

static int rec_new(satmap *s)
{
    if (s->nrec == s->reccap) {
        sat_rec *g = realloc(s->rec, (size_t)(s->reccap = s->reccap ? 2 * s->reccap : 8192) * sizeof *g);
        if (!g) { s->reccap = s->nrec; return -1; }
        s->rec = g;
    }
    return s->nrec++;
}

/* a vertex's corner record (0x3e80c: camera x, y by the mantissa rows, once an object; projected by 0x13000) */
static int vrec(satmap *s, int vi)
{
    if (s->vrec[vi] < 0) {
        const int64_t e[3] = {(int64_t)s->wv[vi][0] - s->eye[0], (int64_t)s->wv[vi][1] - s->eye[1], (int64_t)s->wv[vi][2] - s->eye[2]};
        const int ri = rec_new(s);
        if (ri < 0) return -1;
        s->rec[ri].x = (int32_t)r27(s->Rx[0] * e[0] + s->Rx[1] * e[1] + s->Rx[2] * e[2]);
        s->rec[ri].y = (int32_t)r27(s->Ry[0] * e[0] + s->Ry[1] * e[1] + s->Ry[2] * e[2]);
        s->rec[ri].z = s->vz[vi];
        project(s, &s->rec[ri]);
        s->vrec[vi] = ri;
    }
    return s->vrec[vi];
}
/* 0x3e8fe: edge A -> B's point at the near plane (z = near x 4), x and y from the corner of the smaller z */
static int near_point(satmap *s, int va, int vb)
{
    sat_rec A, B;
    int ri, ra = vrec(s, va), rb = vrec(s, vb);
    if (ra < 0 || rb < 0 || (ri = rec_new(s)) < 0) return -1;
    A = s->rec[ra]; B = s->rec[rb];
    if (A.z < B.z) {
        const int64_t dz = (int64_t)B.z - A.z, e = (int64_t)s->near4 - A.z;
        s->rec[ri].x = dz ? w32(tdiv(((int64_t)B.x - A.x) * e, dz) + A.x) : A.x;
        s->rec[ri].y = dz ? w32(tdiv(((int64_t)B.y - A.y) * e, dz) + A.y) : A.y;
    } else {
        const int64_t dz = (int64_t)A.z - B.z, e = (int64_t)s->near4 - B.z;
        s->rec[ri].x = dz ? w32(tdiv(((int64_t)A.x - B.x) * e, dz) + B.x) : A.x;
        s->rec[ri].y = dz ? w32(tdiv(((int64_t)A.y - B.y) * e, dz) + B.y) : A.y;
    }
    s->rec[ri].z = s->near4;
    project(s, &s->rec[ri]);
    return ri;
}

/* 0x12520 */
static int sat_colour(int objtype, int side, int c)
{
    const int t = objtype & 0xf00, cls = objtype & 0xf0, sd = side < 0 || side > 2 ? 2 : side;
    if (t == 0x100) return SAT_COL[sd];
    if (t == 0x200) return SAT_COL[3 + sd];
    if (t == 0x400) return SAT_COL[6];
    if (t != 0x800) {
        if (cls == 0x40) return SAT_COL[7];
        if (cls == 0x50) return SAT_COL[8];
        if (cls == 0x80) return SAT_COL[9];
        if (cls == 0x10 || cls == 0x20 || cls == 0x60) return c;
    }
    if ((c & 0x7000) == 0x3000) return SAT_COL[10] | 0x4000;
    return ((c & 0xf00) >> 4) | 0x4000;
}

#define SAT_CAP_ENTRIES 0x5dc
#define SAT_CAP_POLYS 0x578

/* one object: 0x12e50, 0x3d830 (level of detail), 0x3ec8e per polygon; returns 0 when the list stops */
static int walk(satmap *s, const sat_obj *ob, int *multi)
{
    const mech3d_part *p = ob->p;
    const wtb_object *o0 = &p->model.objects[0], *o;
    const int objtype = ob->kind ? (ob->kind | (p->objtype & 0xf0)) : p->objtype;
    int32_t sph[4];
    int64_t mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0}, d[3];
    double best = 0;
    int k, c, depth, t, li;
    if (ob->kind == 0 && ((objtype & 0xf0) == 0x30 || (objtype & 0xf0) == 0x70)) return 1;   /* 0x124a0 */
    if (o0->vert_count < 1) return 1;
    if (s->vcap < o0->vert_count) {
        int nc = o0->vert_count + 256;
        free(s->wv); free(s->vz); free(s->vrec);
        s->wv = malloc((size_t)nc * sizeof *s->wv); s->vz = malloc((size_t)nc * sizeof *s->vz); s->vrec = malloc((size_t)nc * sizeof *s->vrec);
        if (!s->wv || !s->vz || !s->vrec) { s->vcap = 0; return 0; }
        s->vcap = nc;
    }
    /* the sphere (0x389f0 on the full model: the box middle, the farthest corner's distance chopped) */
    for (k = 0; k < o0->vert_count; k++) {
        glr_dos_vertex(p, &o0->verts[k], s->wv[k]);
        for (c = 0; c < 3; c++) {
            if (k == 0 || s->wv[k][c] < mn[c]) mn[c] = s->wv[k][c];
            if (k == 0 || s->wv[k][c] > mx[c]) mx[c] = s->wv[k][c];
        }
    }
    for (c = 0; c < 3; c++) sph[c] = (int32_t)((mn[c] + mx[c]) >> 1);
    for (k = 0; k < o0->vert_count; k++) {
        double dx = (double)(s->wv[k][0] - sph[0]), dy = (double)(s->wv[k][1] - sph[1]), dz = (double)(s->wv[k][2] - sph[2]);
        if (dx * dx + dy * dy + dz * dz > best) best = dx * dx + dy * dy + dz * dz;
    }
    sph[3] = (int32_t)sqrt(best);   /* (0x5db42: chopped) */
    for (c = 0; c < 3; c++) d[c] = (int64_t)sph[c] - s->eye[c];
    /* 0x12e50 */
    depth = (int)r29(s->raw[2][0] * d[0] + s->raw[2][1] * d[1] + s->raw[2][2] * d[2]);
    if (sph[3] + depth < s->near) return 1;
    if (s->far < depth - sph[3]) return 1;
    t = (int)r29(s->raw[0][0] * d[0] + s->raw[0][1] * d[1] + s->raw[0][2] * d[2]);
    t = t < 1 ? s->xlo - t : t - s->xhi;
    if (sph[3] < t) return 1;
    t = (int)r29(s->raw[1][0] * d[0] + s->raw[1][1] * d[1] + s->raw[1][2] * d[2]);
    t = t < 1 ? s->ylo - t : t - s->yhi;
    if (sph[3] < t) return 1;
    s->stat[0]++;
    /* 0x3d830: the level of detail - from the first, on while the current one's distance x +0xb8 is reached */
    li = 0;
    if (p->model.objects[0].lod != 0) {
        int cur = 0, prev = 0;
        for (;;) {
            if (cur >= p->model.object_count) { li = prev; break; }
            if (r16(s->b8 * (int64_t)p->model.objects[cur].lod) > depth) { li = prev; break; }
            prev = cur; cur++;
        }
    }
    o = &p->model.objects[li];
    if (li != 0) {
        if (s->vcap < o->vert_count) {
            int nc = o->vert_count + 256;
            free(s->wv); free(s->vz); free(s->vrec);
            s->wv = malloc((size_t)nc * sizeof *s->wv); s->vz = malloc((size_t)nc * sizeof *s->vz); s->vrec = malloc((size_t)nc * sizeof *s->vrec);
            if (!s->wv || !s->vz || !s->vrec) { s->vcap = 0; return 0; }
            s->vcap = nc;
        }
        for (k = 0; k < o->vert_count; k++) glr_dos_vertex(p, &o->verts[k], s->wv[k]);
    }
    for (k = 0; k < o->vert_count; k++) {   /* each vertex's camera z (0x3ec8e: the raw z row >> 27 rounded: quarter cm) */
        int64_t e[3] = {(int64_t)s->wv[k][0] - s->eye[0], (int64_t)s->wv[k][1] - s->eye[1], (int64_t)s->wv[k][2] - s->eye[2]};
        s->vz[k] = (int32_t)r27(s->raw[2][0] * e[0] + s->raw[2][1] * e[1] + s->raw[2][2] * e[2]);
        s->vrec[k] = -1;
    }
    for (k = 0; k < o->poly_count; k++) {
        const wtb_poly *q = &o->polys[k];
        int n = q->n > WTB_MAX_POLY_VERTS ? WTB_MAX_POLY_VERTS : q->n, j, fl_and = 3, cnt = 0, oand = 15, b0, bp, bk;
        int R[24];
        if (n < 1) continue;
        for (j = 0; j < n; j++) if (q->idx[j] >= o->vert_count) break;
        if (j < n) continue;
        if (n >= 3) {   /* one-sided: (corner 0 - the eye) . N < 0 */
            int a, b, cc;
            double e1[3], e2[3], nr[3], dt;
            const int32_t *P0, *P1, *C, *v0 = s->wv[q->idx[0]];
            glr_dos_normal_corners(o, q, &a, &b, &cc);
            P0 = s->wv[a]; P1 = s->wv[b]; C = s->wv[cc];
            for (c = 0; c < 3; c++) { e1[c] = (double)P1[c] - P0[c]; e2[c] = (double)C[c] - P1[c]; }
            nr[0] = -(e1[1] * e2[2] - e1[2] * e2[1]);
            nr[1] = -(e1[2] * e2[0] - e1[0] * e2[2]);
            nr[2] = -(e1[0] * e2[1] - e1[1] * e2[0]);
            dt = nr[0] * (double)(v0[0] - s->eye[0]) + nr[1] * (double)(v0[1] - s->eye[1]) + nr[2] * (double)(v0[2] - s->eye[2]);
            if (!(dt < 0.0)) continue;
        }
        for (j = 0; j < n; j++) {
            const int32_t z = s->vz[q->idx[j]];
            fl_and &= (z < s->near4 ? 1 : 0) | (z > s->far4 ? 2 : 0);
        }
        if (fl_and == 1 || fl_and == 2) continue;
#define SAT_VREC(vi) do { if (vrec(s, (vi)) < 0) return 0; } while (0)
#define SAT_EMIT(ri) do { if (cnt >= 20) return 0; R[cnt++] = (ri); oand &= s->rec[(ri)].oc; } while (0)
        b0 = s->vz[q->idx[0]] < s->near4;
        if (!b0) { SAT_VREC(q->idx[0]); SAT_EMIT(s->vrec[q->idx[0]]); }
        bp = b0;
        for (j = 1; j < n; j++) {
            bk = s->vz[q->idx[j]] < s->near4;
            if (bk != bp) { int ri = near_point(s, q->idx[j - 1], q->idx[j]); if (ri < 0) return 0; SAT_EMIT(ri); }
            if (!bk) { SAT_VREC(q->idx[j]); SAT_EMIT(s->vrec[q->idx[j]]); }
            bp = bk;
        }
        if (b0 != bp) { int ri = near_point(s, q->idx[n - 1], q->idx[0]); if (ri < 0) return 0; SAT_EMIT(ri); }
#undef SAT_VREC
#undef SAT_EMIT
        if ((cnt > 2 || n < 3) && oand == 0 && cnt > 0) {
            int64_t kk;
            const int km = p->dos_sort & 7;
            int i2, first;
            if (km & 2) { uint32_t sm = 0; for (i2 = 0; i2 < cnt; i2++) sm += (uint32_t)s->rec[R[i2]].z; kk = (int64_t)(sm / (uint32_t)cnt); }
            else if (km & 4) { kk = 0x7fffffff; for (i2 = 0; i2 < cnt; i2++) if (s->rec[R[i2]].z < kk) kk = s->rec[R[i2]].z; }
            else { kk = (int32_t)0x80000001; for (i2 = 0; i2 < cnt; i2++) if (s->rec[R[i2]].z > kk) kk = s->rec[R[i2]].z; }
            if (km & 1) kk |= 0x40000000;
            if (s->ne >= SAT_CAP_ENTRIES) return 0;
            if (s->ne == s->entcap) {
                sat_ent *g = realloc(s->ent, (size_t)(s->entcap = s->entcap ? 2 * s->entcap : 2048) * sizeof *g);
                if (!g) { s->entcap = s->ne; return 0; }
                s->ent = g;
            }
            /* the entry's own copies of its records (a record can be shared by polygons; painting reads them only) */
            first = s->nrec;
            for (i2 = 0; i2 < cnt; i2++) { int ri = rec_new(s); if (ri < 0) return 0; s->rec[ri] = s->rec[R[i2]]; }
            s->ent[s->ne].first = first; s->ent[s->ne].n = cnt; s->ent[s->ne].key = (int)kk;
            s->ent[s->ne].colour = sat_colour(objtype, ob->side, q->color);
            s->ne++;
            if (cnt > 1 && ++*multi >= SAT_CAP_POLYS) return 0;
        }
    }
    return 1;
}

static void sat_qsort(int *id, int *key, int lo, int hi)   /* 0x3d930 (0x3cea0's rule, as glr's dos_qsort) */
{
    while (hi > lo) {
        int pv = key[lo], i = lo, j = hi + 1, t;
        for (;;) {
            do i++; while (i <= hi && key[i] > pv);
            do j--; while (key[j] < pv);
            if (j <= i) break;
            t = key[i]; key[i] = key[j]; key[j] = t;
            t = id[i]; id[i] = id[j]; id[j] = t;
        }
        t = key[lo]; key[lo] = key[j]; key[j] = t;
        t = id[lo]; id[lo] = id[j]; id[j] = t;
        if (j - lo < hi - j) { sat_qsort(id, key, lo, j - 1); lo = j + 1; }
        else { sat_qsort(id, key, j + 1, hi); hi = j - 1; }
    }
}

/* ---- the painters */

static void put(uint8_t *pix, int w, int h, int x, int y, int c) { if (x >= 0 && y >= 0 && x < w && y < h) pix[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)c; }

/* 0x54fa9: from corner 0 along the longer axis, both ends, the other coordinate stepped by the carry of a 0.32 fraction
 * (minor << 32) / major (0xffffffff when equal) started at 0x80000000; clipped without changing the walk */
void satmap_line(uint8_t *pix, int w, int h, int x0, int y0, int x1, int y1, int col)
{
    const int dx = x1 - x0, dy = y1 - y0, ax = abs(dx), ay = abs(dy), sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    int k;
    if (dx == 0) { for (k = 0; k <= ay; k++) put(pix, w, h, x0, y0 + sy * k, col); return; }
    if (dy == 0) { for (k = 0; k <= ax; k++) put(pix, w, h, x0 + sx * k, y0, col); return; }
    {
        const int xm = ax > ay, mj = xm ? ax : ay, mn = xm ? ay : ax;
        const uint32_t st = ax == ay ? 0xffffffffu : (uint32_t)(((uint64_t)(uint32_t)mn << 32) / (uint32_t)mj);
        for (k = 0; k <= mj; k++) {
            const uint64_t q = (uint64_t)(uint32_t)k * st + 0x80000000u;
            const int m = (int)(q >> 32);
            if (xm) put(pix, w, h, x0 + sx * k, y0 + sy * m, col); else put(pix, w, h, x0 + sx * m, y0 + sy * k, col);
        }
    }
}

/* an edge's x step (dx << 16) / rows, as the 32-bit registers hold it */
static uint32_t xstep(int dx, int cnt) { return (uint32_t)w32(tdiv((int64_t)dx * 65536, cnt)); }
/* one chain (dir -1 backward, + 1 forward) at row y: x, c (16.16) - world.frag dos_gchain */
static void chain(const int V[][2], const int *C, int m, int top, int dir, int y, int ybot, int32_t *xo, int32_t *co)
{
    int a = top, b = (a + dir + m) % m, r0 = V[top][1], cnt, yq, g;
    uint32_t t;
    for (g = 0; g < 20 && V[b][1] == V[a][1] && b != top; g++) { a = b; b = (a + dir + m) % m; }
    cnt = V[b][1] - V[a][1];
    yq = y == ybot && y > r0 ? y - 1 : y;
    for (g = 0; g < 20 && yq >= r0 + cnt && b != top; g++) { r0 += cnt; a = b; b = (a + dir + m) % m; cnt = V[b][1] - V[a][1]; if (cnt == 0) cnt = 1; }
    if (cnt == 0) cnt = 1;
    t = (uint32_t)(y - r0);
    *xo = (int32_t)((uint32_t)V[a][0] * 65536u + 0x8000u + t * xstep(V[b][0] - V[a][0], cnt));
    *co = (int32_t)((uint32_t)(C[a] + 0x8000) + t * (uint32_t)w32(tdiv((int64_t)C[b] - C[a], cnt)));
}
/* 0x5a900 (dither 0: flat - every record the same colour here) and 0x5b5f7 (Gouraud, dither 0x7fff: the even pixel of a
 * pair from the span's own left end gets it, the odd one not, swapped every row from the polygon's top) */
static void fill(uint8_t *pix, int w, int h, const int V[][2], const int *C, int m, int dither)
{
    int top = 0, ybot = V[0][1], i, y;
    for (i = 0; i < m; i++) { if (V[i][1] <= V[top][1]) top = i; if (V[i][1] > ybot) ybot = V[i][1]; }
    if (ybot == V[top][1]) return;   /* (one row: no pixel - both fillers under unicorn) */
    for (y = V[top][1] < 0 ? 0 : V[top][1]; y <= ybot && y < h; y++) {
        int32_t x1, c1, x2, c2, st;
        int x0, wd, a, b, k;
        const int r = y - V[top][1];
        chain(V, C, m, top, -1, y, ybot, &x1, &c1);
        chain(V, C, m, top, 1, y, ybot, &x2, &c2);
        if (x2 <= x1) { int32_t t = x1; x1 = x2; x2 = t; t = c1; c1 = c2; c2 = t; }
        x0 = x1 >> 16; wd = (x2 >> 16) - x0;
        st = wd ? w32(tdiv((int64_t)c2 - c1, (wd >> 1) > 1 ? (wd >> 1) : 1)) : 0;
        a = x0 < 0 ? 0 : x0; b = x0 + wd > w - 1 ? w - 1 : x0 + wd;
        for (k = a - x0; k <= b - x0; k++) {
            const uint32_t val = (uint32_t)c1 + (uint32_t)(k >> 1) * (uint32_t)st + (dither && ((k + r) & 1) == 0 ? (uint32_t)dither : 0u);
            pix[(size_t)y * (size_t)w + (size_t)(x0 + k)] = (uint8_t)(val >> 16);
        }
    }
}

/* the paint's 2D cut (0x3dd70 -> 0x3de30) for records beyond -0x3fff..0x3ffe: rb_gl12_world.c's dos_clip2d with the map's
 * centre and shifts. R: {camera x, y, z, screen x, y} */
static int32_t c2_lin(int32_t o, int32_t a0, int32_t b0, int32_t a1, int32_t b1)
{
    if (b0 <= a0) return w32(tdiv((int64_t)w32((int64_t)o - a0) * w32((int64_t)b1 - a1), w32((int64_t)b0 - a0)) + a1);
    return w32(tdiv((int64_t)w32((int64_t)o - b0) * w32((int64_t)a1 - b1), w32((int64_t)a0 - b0)) + b1);
}
static int32_t c2_zcut(const satmap *s, int32_t a, int32_t b, int32_t L, int isx, int32_t za, int32_t zb)
{
    int32_t bx = a, dx = w32((int64_t)b - a), bz = za, dz = w32((int64_t)zb - za), k, t;
    if (dz < 0) { dz = w32(-(int64_t)dz); bx = b; dx = w32(-(int64_t)dx); bz = zb; }
    k = isx ? w32((int64_t)L - s->w / 2) : w32((int64_t)(s->h / 2) - L);
    t = w32((int64_t)w32(((int64_t)(int32_t)((uint32_t)k << 2) * dz) >> 3) - dx);
    if (t == 0) return bz;
    return w32(tdiv((int64_t)bx * dz - (int64_t)bz * dx, t));
}
static int c2_out(const int32_t *r, int e) { return e == 0 ? r[3] > 0x3fff : e == 1 ? r[3] < -0x4000 : e == 2 ? r[4] < -0x4000 : r[4] > 0x3fff; }
static int clip2d(const satmap *s, int32_t R[24][5], int m)
{
    int i, e, big = 0;
    for (i = 0; i < m; i++) big |= R[i][3] < -16383 || R[i][3] > 16382 || R[i][4] < -16383 || R[i][4] > 16382;
    if (!big) return m;
    for (e = 0; e < 4 && m >= 1; e++) {
        int32_t W[24][5];
        int n = 0;
        const int32_t L = e == 0 || e == 3 ? 0x3fff : -0x4000;
        for (i = 0; i < m && i < 20; i++) {
            const int pi = (i + m - 1) % m, po = c2_out(R[pi], e), qo = c2_out(R[i], e);
            if (po != qo && n < 20) {
                const int32_t *A = R[pi], *B = R[i];
                int32_t *o = W[n++];
                o[2] = c2_zcut(s, e < 2 ? A[0] : A[1], e < 2 ? B[0] : B[1], L, e < 2, A[2], B[2]);
                if (e < 2) { o[3] = L; o[4] = c2_lin(L, A[3], B[3], A[4], B[4]); }
                else { o[4] = L; o[3] = c2_lin(L, A[4], B[4], A[3], B[3]); }
                o[0] = w32(((int64_t)(int32_t)((uint32_t)w32((int64_t)o[3] - s->w / 2) << 2) * o[2]) >> 3);
                o[1] = w32(((int64_t)(int32_t)((uint32_t)w32((int64_t)(s->h / 2) - o[4]) << 2) * o[2]) >> 3);
            }
            if (!qo && n < 20) memcpy(W[n++], R[i], sizeof W[0]);
        }
        m = n;
        memcpy(R, W, sizeof W);
    }
    return m;
}

/* 0x12790: a record's shade from its camera z */
static int shade(const satmap *s, int32_t z)
{
    const int64_t t = (int64_t)w32((int64_t)s->range - (z >> 2) - s->hmin);
    const int64_t q = w32(tdiv(t * 65536, s->hrange));
    int64_t v = r16(q * 16);
    return v < 0 ? 0 : v > 15 ? 15 : (int)v;
}

/* 0x3c610 type 2: a line (0x54fa9) from each record k = 1 .. n - 1 back to the one before it, then from record 0 to the last */
static void outline(const satmap *s, uint8_t *pix, const int V[][2], int m, int col)
{
    int k;
    for (k = 1; k < m; k++) satmap_line(pix, s->w, s->h, V[k][0], V[k][1], V[k - 1][0], V[k - 1][1], col);
    satmap_line(pix, s->w, s->h, V[0][0], V[0][1], V[m - 1][0], V[m - 1][1], col);
}

static void paint_one(const satmap *s, uint8_t *pix, const sat_ent *e)
{
    int32_t R[24][5];
    int V[24][2], C[24], m = e->n, i;
    const int col = e->colour;
    for (i = 0; i < m && i < 20; i++) { R[i][0] = s->rec[e->first + i].x; R[i][1] = s->rec[e->first + i].y; R[i][2] = s->rec[e->first + i].z; R[i][3] = s->rec[e->first + i].sx; R[i][4] = s->rec[e->first + i].sy; }
    m = clip2d(s, R, m);
    if (m <= 0) return;
    for (i = 0; i < m; i++) { V[i][0] = R[i][3]; V[i][1] = R[i][4]; }
    if (m == 1) { if (col != 0x1000) put(pix, s->w, s->h, V[0][0], V[0][1], col & 0xff); return; }   /* 0x54df8 */
    if (m == 2) { satmap_line(pix, s->w, s->h, V[0][0], V[0][1], V[1][0], V[1][1], col & 0xff); return; }   /* 0x54fa9 */
    switch (col & 0x7000) {   /* 0x12690 */
    case 0x0000:   /* black, then the outline */
        for (i = 0; i < m; i++) C[i] = 0;
        fill(pix, s->w, s->h, (const int (*)[2])V, C, m, 0);
        outline(s, pix, (const int (*)[2])V, m, col & 0xff);
        return;
    case 0x2000:   /* the outline (0x3c610 type 2: each record to the next, the last to the first) */
        outline(s, pix, (const int (*)[2])V, m, col & 0xff);
        return;
    case 0x3000:
        return;   /* (0x3c420: a billboard's bitmap - not reproduced) */
    case 0x4000:
        for (i = 0; i < m; i++) C[i] = ((col & 0xf0) | shade(s, R[i][2])) << 16;
        fill(pix, s->w, s->h, (const int (*)[2])V, C, m, 0x7fff);
        return;
    default:   /* 0x3c610: type 1 flat (0x5a900); the textured types flat (ASSUMED) */
        for (i = 0; i < m; i++) C[i] = (col & 0xff) << 16;
        fill(pix, s->w, s->h, (const int (*)[2])V, C, m, 0);
        return;
    }
}

void satmap_paint(satmap *s, uint8_t *pix, int ground)
{
    int k, multi = 0;
    memset(pix, ground & 0xff, (size_t)s->w * (size_t)s->h);   /* 0x57978 */
    s->stat[0] = 0;
    qsort(s->obj, (size_t)s->nobj, sizeof *s->obj, obj_cmp);
    s->nrec = 0; s->ne = 0;
    for (k = 0; k < s->nobj; k++) if (!walk(s, &s->obj[k], &multi)) break;
    s->stat[1] = s->ne;
    if (s->kcap < s->ne) {
        free(s->key); free(s->id);
        s->kcap = s->ne + 1024;
        s->key = malloc((size_t)s->kcap * sizeof *s->key); s->id = malloc((size_t)s->kcap * sizeof *s->id);
        if (!s->key || !s->id) { s->kcap = 0; return; }
    }
    for (k = 0; k < s->ne; k++) { s->key[k] = s->ent[k].key; s->id[k] = k; }
    if (s->ne > 1) sat_qsort(s->id, s->key, 0, s->ne - 1);
    for (k = 0; k < s->ne; k++) paint_one(s, pix, &s->ent[s->id[k]]);
}

/* 0x14350: the window's edge point (window-relative) from its centre along a direction given by flags (1: left of the
 * centre, 2: above it, 4: vertical) and the slope (16.16, -dy / dx with y up) */
static void edge(const satmap *s, int fl, int32_t slope, int *ox, int *oy)
{
    const int32_t hx = ((s->w - 1) + 1) >> 1, hy = ((s->h - 1) + 1) >> 1;
    const int32_t w2 = hx * 2, h2 = hy * 2, e = hx == 0 ? 0 : w32(tdiv((int64_t)hy * 65536, hx)), ne = w32(-(int64_t)e);
    int32_t x = 0, y = 0;
    switch (fl) {
    case 3: if (slope > e) { y = 0; x = hx - w32(tdiv((int64_t)hy * 65536, slope)); } else { x = 0; y = hy - w32(r16((int64_t)hx * slope)); } break;
    case 2: if (slope < ne) { y = 0; x = hx - w32(tdiv((int64_t)hy * 65536, slope)); } else { x = w2; y = hy + w32(r16((int64_t)hx * slope)); } break;
    case 0: if (slope <= e) { x = w2; y = hy + w32(r16((int64_t)hx * slope)); } else { y = h2; x = hx + w32(tdiv((int64_t)hy * 65536, slope)); } break;
    case 1: if (slope < ne) { y = h2; x = hx + w32(tdiv((int64_t)hy * 65536, slope)); } else { x = 0; y = hy - w32(r16((int64_t)hx * slope)); } break;
    case 7: x = hx; y = 0; break;    /* (the jump table 0x14330: 4 / 6 -> 0x14504, 5 -> 0x144ea, 7 -> 0x144d1) */
    case 5: x = hx; y = h2; break;
    default: x = 0; y = 0; break;
    }
    *ox = x; *oy = y;
}

/* 0x14200 + 0x14350: the window's edge point (window-relative) along screen angle a (16.16 degrees, counter-clockwise
 * from the right; y down on the window) from its centre */
static void rim(const satmap *s, int32_t a, int *ox, int *oy)
{
    int32_t cs, sn, slope = 0, q;
    int fl = 0, ovf;
    a = (int32_t)((((int64_t)a % 0x1680000) + 0x1680000) % 0x1680000);
    fl |= a >= 0x5a0000 && a <= 0x10e0000 ? 1 : 0;
    fl |= a <= 0xb40000 ? 2 : 0;
    cs = glr_dos_cos(a) >> 13;
    sn = w32(r16((int64_t)(glr_dos_sin(a) >> 13) * 0xffff));
    ovf = cs == 0;
    if (!ovf) { q = w32(tdiv(sn, cs)); ovf = q > 0x7fff || q < -0x8000; }
    if (!ovf) slope = w32(tdiv((int64_t)sn * 65536, cs));
    fl |= ovf ? 4 : 0;
    slope = w32(-(int64_t)slope);
    edge(s, fl, slope, ox, oy);
}

void satmap_wedge(const satmap *s, uint8_t *pix, int32_t bearing, uint32_t zoom, int col)
{
    const int32_t half = glr_dos_atan2(0x10000, (int32_t)zoom);   /* 0x3b475(0x10000, +0x18) */
    const int32_t a0 = 0x5a0000 - (int32_t)((((int64_t)bearing % 0x1680000) + 0x1680000) % 0x1680000);
    const int cx = ((s->w - 1) + 1) >> 1, cy = ((s->h - 1) + 1) >> 1;
    int x, y;
    rim(s, a0 - half, &x, &y);
    satmap_line(pix, s->w, s->h, cx, cy, x, y, col);
    rim(s, a0 + half, &x, &y);
    satmap_line(pix, s->w, s->h, cx, cy, x, y, col);
}

int satmap_point(const satmap *s, const float p[3], int *sx, int *sy)
{
    const int64_t d[3] = {(int64_t)lrintf(p[0]) - s->eye[0], (int64_t)lrintf(p[1]) - s->eye[1], (int64_t)lrintf(p[2]) - s->eye[2]};
    sat_rec r;
    r.x = (int32_t)r27(s->Rx[0] * d[0] + s->Rx[1] * d[1] + s->Rx[2] * d[2]);
    r.y = (int32_t)r27(s->Ry[0] * d[0] + s->Ry[1] * d[1] + s->Ry[2] * d[2]);
    r.z = (int32_t)r27(s->raw[2][0] * d[0] + s->raw[2][1] * d[1] + s->raw[2][2] * d[2]);
    project(s, &r);
    *sx = r.sx; *sy = r.sy;
    return r.z > 0 && r.oc == 0;   /* (0x13140: in front, inside the window) */
}

void satmap_edge(const satmap *s, int px, int py, int *ex, int *ey)
{
    const int32_t hx = ((s->w - 1) + 1) >> 1, hy = ((s->h - 1) + 1) >> 1, dx = hx - px, dy = hy - py;
    int fl = (dx >= 0 ? 1 : 0) | (dy >= 0 ? 2 : 0), ovf;
    int32_t slope = 0;
    ovf = dx == 0 || tdiv(dy, dx) > 0x7fff || tdiv(dy, dx) < -0x8000;
    if (!ovf) slope = w32(tdiv((int64_t)dy * 65536, dx));
    edge(s, fl | (ovf ? 4 : 0), slope, ex, ey);
}

void satmap_stats(const satmap *s, int out[2]) { out[0] = s->stat[0]; out[1] = s->stat[1]; }
