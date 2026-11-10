/* shim_msim.c - the port's msim.c with its statics reachable, for tools/audit/simdiff.py */
#include "../../src/msim.c"
float shim_leg_demand(float input, float spd, int player, float gain, int airborne, int autopilot, float prev)
{ return msim_leg_demand(input, spd, player, gain, airborne, autopilot, prev); }
float shim_servo(float cur, float tgt, float T, float ticks) { return drive_servo(cur, tgt, T, ticks); }
float shim_twist_round(float cur, float demand, float T, float ticks) { return msim_twist_round(cur, demand, T, ticks); }
/* one face against one segment (eseg_poly): v = n world vertices, nrm the face normal; returns 1 and the cut end */
int shim_seg_poly(const float *v, int n, const float *nrm, const float *p0, const float *p1, float *out)
{
    cpoly_t c;
    eseg_t g;
    float nn[3];
    int j, a;
    c.n = n;
    for (a = 0; a < 3; a++) { c.nrm[a] = nrm[a]; c.mn[a] = 3.4e38f; c.mx[a] = -3.4e38f; }
    for (j = 0; j < n; j++) for (a = 0; a < 3; a++) {
        c.v[j][a] = v[j * 3 + a];
        if (c.v[j][a] < c.mn[a]) c.mn[a] = c.v[j][a];
        if (c.v[j][a] > c.mx[a]) c.mx[a] = c.v[j][a];
    }
    eseg_set(&g, p0, p1);
    if (!eseg_poly(&c, &g, nn)) return 0;
    for (a = 0; a < 3; a++) out[a] = g.e[a];
    return 1;
}
