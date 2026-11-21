/* glr_dosrec.h - the DOS fill's per-polygon records, made once a polygon a frame on the CPU (glr.c glr_dos_list) for the
 * lean DOS program (shaders/world.frag DOS_REC): the corner records world.frag's fill used to work out again for every
 * pixel - the camera integers, the near-plane cut (GBL 0x3e8fe), the paint's 2D clip (0x3de30) - and the two chains'
 * edges the span fill (0x59e77 / 0x5a900 / 0x405d0) steps down. The shader's own arithmetic, the float parts included
 * (glr_dosrec.c), so that every pixel comes out as the per-pixel fill made it. */
#ifndef MW2_GLR_DOSREC_H
#define MW2_GLR_DOSREC_H

#include <stdint.h>

/* the frame's DOS projection, as world_common.glsl reads it from rb_world_frame */
typedef struct {
    float eye[3], rx[3], ry[3], rz[3];   /* u_dos_eye, u_dos_rx / ry / rz */
    int   ps[2];                         /* u_dos_ps */
    float vp[2];                         /* u_vp */
    float mvp[16];                       /* u_mvp (column-major) */
    int   nr;                            /* the polygon's near plane (dos_nearof) */
} dosrec_view;

#define DOSREC_MAXR 20
#define DOSREC_MAXE 22
/* one polygon's records and chains. An edge: thr (the row from which the chain walks on to the next edge: r0 + its
 * row count as dos_gedge compares it), r0 (its first row), cnt (its row count for the steps, never 0), xstep (dos_xstep),
 * xa (its upper corner's x), a / b (its corners' records) */
typedef struct {
    int     m, top, ytop, ybot, nodiscard;
    int32_t rec[DOSREC_MAXR][4];          /* the Gouraud corner index a << 16 (dos_gsc's input), u << 16, v << 16, camera z */
    int     ne[2];                        /* the left (dir -1) and right (+1) chains' edge counts */
    int32_t edge[2][DOSREC_MAXE][8];      /* thr, r0, cnt, xstep, xa, a, b, 0 */
    int     nc;                           /* the polygon's corners (their u, v: the ENHANCED / smooth Gouraud's range) */
    float   cuv[18][2];
} dosrec_poly;

/* the records of the polygon whose fan starts at layer vertex s (vf: the layer's vertex floats, 13 a vertex, nv
 * vertices): 1, or 0 when the per-pixel fill would not take its exact path (the caller then leaves the polygon to it) */
int dosrec_build(const dosrec_view *V, const float *vf, int nv, int s, dosrec_poly *o);

#endif
