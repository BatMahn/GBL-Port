/* gbl_score.h - the debriefing's difficulty multiplier as GBL MW2SHELL computes it (0x25567..0x2557f):
 *   fild honor; fld <multiplier double>; fmul st(1); fsubrp -> honor x m - honor; 0x4d382 (frndint with the control
 *   word's rounding set to chop); fistp; + honor
 * with the x87 at the control word the C runtime loads (0x5c711: 0x127f - double precision, round to nearest):
 * honor + trunc(fl(honor x m) - honor), m the doubles 0.8 (EASY), 1.0 (MEDIUM), 1.3 (HARD). So a product ending in .5
 * is cut, not rounded: HARD 5375 -> 6987 (the port had rounded to 6988), and the doubles' excess over 0.8 / 1.3 decides
 * where the product rounds to a whole number. tools/audit/honor_diff.py runs the original instructions on every value
 * of a range. The product is kept in its own (volatile) double so that no compiler fuses it with the subtraction. */
#ifndef GBL_SCORE_H
#define GBL_SCORE_H

/* dif: MW2DIF +5 (0 EASY, 1 MEDIUM, 2 HARD) */
static inline int gbl_honor_mult(int honor, int dif)
{
    volatile double p = (double)honor * (dif == 0 ? 0.8 : dif == 2 ? 1.3 : 1.0);
    double x = p - (double)honor;
    return honor + (int)x;   /* C truncates toward zero, as frndint with chop */
}

#endif
