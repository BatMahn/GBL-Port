/* shim_ai.c - the port's ai.c with its statics reachable, for tools/audit/simdiff.py */
#include "../../src/ai.c"
float shim_nav_turn(float err) { return ai_nav_turn(err); }
/* the manoeuvre record ai_set_class / ai_gbl_profile give a unit: rows, first-pick range, the table's rows (9 shorts each) */
int shim_man_record(int unit_class, int tons, int gps_skill, short *rows_out, int *first)
{
    ai_mind m; int sk, lv, gf, r, k;
    ai_init(&m, 0);
    ai_gbl_profile(&m, gps_skill, &sk, &lv, &gf);
    ai_set_class(&m, unit_class, tons);
    if (!m.man_table) { for (r = 0; r < 12; r++) for (k = 0; k < 9; k++) rows_out[r * 9 + k] = MAN_TABLE[r][k];
                        for (k = 0; k < 9; k++) rows_out[12 * 9 + k] = 0; *first = 7; return 13; }
    for (r = 0; r < m.man_rows; r++) for (k = 0; k < 9; k++) rows_out[r * 9 + k] = m.man_table[r][k];
    *first = m.man_first;
    return m.man_rows;
}
/* the patrol torso sweep over a run of frames: per frame its ticks and the throttle input of the last tick; the torso
 * (degrees) after each frame */
void shim_patrol_seq(int n, const float *ticks, const float *thr, float *torso_out, int dir0, float torso0, float rem0)
{
    ai_mind m; int k;
    ai_init(&m, 0);
    m.sweep_dir = dir0; m.torso = torso0; m.sweep_ticks = rem0;
    for (k = 0; k < n; k++) { patrol_sweep(&m, thr[k], ticks[k]); torso_out[k] = m.torso; }
}
