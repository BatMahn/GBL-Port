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
/* GBL 0x2d350's point: slot k (16-way), d cm, round (cx, cz) facing h degrees */
void shim_slot_point(float cx, float cz, float h, int k, float d, float *xz)
{ slot_point(cx, cz, h, k, d, &xz[0], &xz[1]); }
/* manoeuvre 7's start (start_manoeuvre case 7) for a unit at (sx, sz) facing h, target dist away: the point steered for */
void shim_man7(float sx, float sz, float h, float dist, unsigned seed, float *xz)
{
    ai_mind m; ai_view v;
    ai_init(&m, 0); memset(&v, 0, sizeof v);
    m.rng = seed;
    v.self_x = sx; v.self_z = sz; v.heading = h; v.enemy_dist = dist; v.enemy_x = sx + 50000.0f; v.enemy_z = sz - 30000.0f;
    start_manoeuvre(&m, &v, 7);
    xz[0] = m.flank_x; xz[1] = m.flank_z;
}
/* the manoeuvre chooser's first answer for a class 1 unit (target a nav point: no jump pre-check, the table pick
 * skipped) given contact, the reverse input, +0x194 and the current manoeuvre; and the contact end rule */
int shim_contact_pick(int contact, int reverse, int jump_man, int man, int *ends)
{
    ai_mind m; ai_view v;
    ai_init(&m, 0); memset(&v, 0, sizeof v);
    m.mech_rules = 1; m.reverse = reverse; m.jump_man = jump_man; m.man = man;
    v.contact = contact; v.target_not_unit = 1;
    *ends = contact_rule(&m, &v);
    return choose_manoeuvre(&m, &v);
}
/* manoeuvre 1's slot pick (flank_pick) for a bearing off the target's facing, rand(2) result r2 and the counters */
int shim_flank_pick(float rel, int r2, const int *own, const int *tgt)
{ ai_mind m; ai_init(&m, 0); return flank_pick(&m, rel, r2, own, tgt); }
