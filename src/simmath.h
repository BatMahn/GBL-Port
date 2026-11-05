/*
 * simmath.h - the simulation's atan2f, the same on every build. The C libraries' own differ in the last bit: the 32-bit
 * glibc's and MSVCRT's are x87 code, the 64-bit glibc's is fdlibm's single-precision one - so the legacy 32-bit build
 * (SSE2 maths otherwise as the 64-bit builds') took other angles, and a mission could play differently (the objective
 * sweep: HOFFSCN1). This is fdlibm's atan2f / atanf as glibc 2.39's x86-64 libm has it (sysdeps/ieee754/flt-32), so the
 * 64-bit Linux build's results are unchanged; every other build now takes the same. Included by the simulation's
 * sources after <math.h>: atan2f is redirected here.
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
#ifndef GBL_SIMMATH_H
#define GBL_SIMMATH_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef __clang__
#pragma STDC FP_CONTRACT OFF   /* (no fused multiply-adds: each operation rounded as on x86-64; gcc's -std=c99 has it off) */
#endif

static inline int32_t simm_fw(float f) { int32_t i; memcpy(&i, &f, 4); return i; }
static inline float simm_wf(int32_t i) { float f; memcpy(&f, &i, 4); return f; }

static inline float sim_atanf(float x)
{
    static const float atanhi[] = {4.6364760399e-01f, 7.8539812565e-01f, 9.8279368877e-01f, 1.5707962513e+00f};
    static const float atanlo[] = {5.0121582440e-09f, 3.7748947079e-08f, 3.4473217170e-08f, 7.5497894159e-08f};
    static const float aT[] = {3.3333334327e-01f, -2.0000000298e-01f, 1.4285714924e-01f, -1.1111110449e-01f, 9.0908870101e-02f,
                               -7.6918758452e-02f, 6.6610731184e-02f, -5.8335702866e-02f, 4.9768779427e-02f, -3.6531571299e-02f,
                               1.6285819933e-02f};
    const float one = 1.0f, huge = 1.0e30f;
    float w, s1, s2, z;
    int32_t ix, hx = simm_fw(x), id;
    ix = hx & 0x7fffffff;
    if (ix >= 0x4c000000) {   /* |x| >= 2^25 */
        if (ix > 0x7f800000) return x + x;   /* NaN */
        if (hx > 0) return atanhi[3] + atanlo[3];
        return -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3ee00000) {   /* |x| < 0.4375 */
        if (ix < 0x31000000 && huge + x > one) return x;   /* |x| < 2^-29 */
        id = -1;
    } else {
        x = fabsf(x);
        if (ix < 0x3f980000) {   /* |x| < 1.1875 */
            if (ix < 0x3f300000) { id = 0; x = (2.0f * x - one) / (2.0f + x); }   /* 7/16 <= |x| < 11/16 */
            else { id = 1; x = (x - one) / (x + one); }                          /* 11/16 <= |x| < 19/16 */
        } else {
            if (ix < 0x401c0000) { id = 2; x = (x - 1.5f) / (one + 1.5f * x); }  /* |x| < 2.4375 */
            else { id = 3; x = -1.0f / x; }                                      /* 2.4375 <= |x| < 2^66 */
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return hx < 0 ? -z : z;
}

static inline float sim_atan2f(float y, float x)
{
    const float tiny = 1.0e-30f, zero = 0.0f, pi_o_4 = 7.8539818525e-01f, pi_o_2 = 1.5707963705e+00f, pi = 3.1415927410e+00f,
                pi_lo = -8.7422776573e-08f;
    float z;
    int32_t k, m, hx = simm_fw(x), ix, hy = simm_fw(y), iy;
    ix = hx & 0x7fffffff;
    iy = hy & 0x7fffffff;
    if (ix > 0x7f800000 || iy > 0x7f800000) return x + y;   /* NaN */
    if (hx == 0x3f800000) return sim_atanf(y);               /* x = 1.0 */
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);                 /* 2 sign(x) + sign(y) */
    if (iy == 0) {
        switch (m) {
        case 0: case 1: return y;
        case 2: return pi + tiny;
        default: return -pi - tiny;
        }
    }
    if (ix == 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
    if (ix == 0x7f800000) {
        if (iy == 0x7f800000) {
            switch (m) {
            case 0: return pi_o_4 + tiny;
            case 1: return -pi_o_4 - tiny;
            case 2: return 3.0f * pi_o_4 + tiny;
            default: return -3.0f * pi_o_4 - tiny;
            }
        }
        switch (m) {
        case 0: return zero;
        case 1: return -zero;
        case 2: return pi + tiny;
        default: return -pi - tiny;
        }
    }
    if (iy == 0x7f800000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
    k = (iy - ix) >> 23;
    if (k > 60) z = pi_o_2 + 0.5f * pi_lo;   /* |y / x| > 2^60 */
    else if (hx < 0 && k < -60) z = 0.0f;   /* |y| / x < -2^60 */
    else z = sim_atanf(fabsf(y / x));
    switch (m) {
    case 0: return z;
    case 1: return simm_wf(simm_fw(z) ^ (int32_t)0x80000000);
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
    }
}
#define atan2f sim_atan2f

#endif
