/* mtbl.c - see mtbl.h. */
#include "mtbl.h"

#include <stdlib.h>
#include <string.h>

#include "bwd.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int rd16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static void cstr(char *dst, size_t n, const uint8_t *src, size_t max)
{
    size_t i;
    for (i = 0; i + 1 < n && i < max && src[i] >= 32 && src[i] < 127; i++) dst[i] = (char)src[i];
    dst[i] = '\0';
    while (i > 0 && dst[i - 1] == ' ') dst[--i] = '\0';
}

int mtbl_decode(const uint8_t *c, size_t len, mtbl_table *t)
{
    int k, i;
    memset(t, 0, sizeof *t);
    if (len < 0x26 + 0x97 || memcmp(c, "MTBL", 4) != 0) return -1;
    t->index = (int)rd32(c + 8);
    t->time_limit = rd32(c + 12);
    cstr(t->win_sound, sizeof t->win_sound, c + 0x12, 9);
    cstr(t->lose_sound, sizeof t->lose_sound, c + 0x1d, 9);
    t->node_count = (int)((len - 0x26) / 0x97);
    if (t->node_count > MTBL_MAX_NODES) t->node_count = MTBL_MAX_NODES;
    for (k = 0; k < t->node_count; k++) {
        const uint8_t *L = c + 0x5a + (size_t)k * 0x97;
        mtbl_node *n = &t->nodes[k];
        if ((size_t)(L - c) + 0x23 + 1 > len) { t->node_count = k; break; }
        n->kind = rd32(L - 0x34);
        n->visible = L[-0x30] == 'V';
        n->require_all = L[-0x2e] != 0;
        for (i = 0; i < MTBL_ITEMS; i++) {
            const uint8_t *it = L - 0x2a + i * 4;
            char ty = (char)it[0];
            if (ty != 'C' && ty != 'S' && ty != 'F') break;   /* 'I' / other ends the list */
            n->items[i].type = ty;
            n->items[i].node = it[1];
            n->items[i].table = it[2];
            n->item_count++;
        }
        n->delay = rd32(L - 0x0a);
        n->objective = (char)L[-5];
        n->priority = L[-6];
        n->need = L[-4];   /* members to put on each target (0 = all; engine runtime node +0xb3) */
        n->lock = L[-3];   /* command lock bits (+0xb7: 1 -> 2, 2 -> 1, 0x100146e0) */
        cstr(n->sound_start, sizeof n->sound_start, L, 9);
        cstr(n->sound_finish, sizeof n->sound_finish, L + 0x0b, 9);
        n->target_id = rd16s(L + 0x14);
        cstr(n->target, sizeof n->target, L + 0x16, 9);
        n->other_table = rd16s(L + 0x1f);
        n->other_node = rd16s(L + 0x21);
        if ((size_t)(L - c) + 0x23 + 60 <= len) cstr(n->text, sizeof n->text, L + 0x23, 60);
    }
    return 0;
}

const char *mtbl_kind_name(uint32_t k)
{
    switch (k) {
    case MTBL_K_TIMER: return "timer";
    case MTBL_K_DESTROY1: case MTBL_K_DESTROY2: return "destroy";
    case MTBL_K_PROTECT: return "protect";
    case MTBL_K_SCAN: return "visit";
    case MTBL_K_START: return "start";
    case MTBL_K_ALLMAIN: return "finish";
    case MTBL_K_REACH: return "reach";
    case MTBL_K_TIMER2: return "timer2";
    case MTBL_K_ORDER: return "order";
    case MTBL_K_HOLD: return "hold";
    case MTBL_K_LEAVE: return "leave";
    case MTBL_K_STAR_WIN: return "star-win";
    case MTBL_K_STAR_LOSE: return "star-lose";
    case MTBL_K_TOGGLE: return "toggle";
    case MTBL_K_FAIL: return "force-fail";
    case MTBL_K_SUCCEED: return "force-ok";
    case MTBL_K_RESET: return "reset";
    case MTBL_K_INSTANT: return "instant";
    case MTBL_K_REARM: return "re-arm";
    default: return "?";
    }
}

/* ---- interpreter ---------------------------------------------------------------- */

int mtbl_logic_init(mtbl_logic *ml, const uint8_t *scene, size_t len)
{
    int n = bwd_chunks(scene, len, NULL, 0), k, t, i;
    bwd_chunk *c;
    memset(ml, 0, sizeof *ml);
    if (n <= 0 || !(c = calloc((size_t)n, sizeof *c))) return -1;
    bwd_chunks(scene, len, c, n);
    for (k = 0; k < n; k++) {
        mtbl_table tmp;
        if (strcmp(c[k].tag, "MTBL") != 0) continue;
        if (mtbl_decode(c[k].data - 8, c[k].size + 8, &tmp) != 0) continue;
        if (tmp.index < 0 || tmp.index >= MTBL_MAX_TABLES) continue;
        ml->tables[tmp.index] = tmp;
        if (tmp.index + 1 > ml->table_count) ml->table_count = tmp.index + 1;
    }
    free(c);
    for (t = 0; t < ml->table_count; t++) {
        ml->current[t] = 0;   /* GBL 0x4dff0: DAT_000e5940[table] = 0 at load (the first frame picks the real one) */
        ml->result_time[t] = -1;
        for (i = 0; i < MTBL_MAX_NODES; i++) {
            ml->started[t][i] = -1;
            ml->ended[t][i] = -1;
            ml->shown[t][i] = ml->tables[t].nodes[i].visible;
        }
        ml->started[t][0] = 0;   /* 0x16050 at the mission start: node 0 armed at the clock (0) */
    }
    return 0;
}

/* GBL 0x16740 (with 0x166a0 for each item): a finished node (5 / 6) is not ready; a start node always is; else its
 * items in order until the first unused one - "any": ready at the first that holds, "all": not ready at the first that
 * fails ('C' the node finished, 'S' succeeded, 'F' failed - a node in state 8 has not failed yet) */
static int node_ready(const mtbl_logic *ml, int t, int i)
{
    const mtbl_node *n = &ml->tables[t].nodes[i];
    int k, all = 1;
    if (ml->state[t][i] == MTBL_OK || ml->state[t][i] == MTBL_FAILED) return 0;
    if (n->kind == MTBL_K_START) return 1;
    for (k = 0; k < n->item_count; k++) {
        const mtbl_item *it = &n->items[k];
        int st = (it->table < ml->table_count && it->node < MTBL_MAX_NODES) ? ml->state[it->table][it->node] : 0;
        int ok = (it->type == 'C' && (st == MTBL_OK || st == MTBL_FAILED)) || (it->type == 'S' && st == MTBL_OK) ||
                 (it->type == 'F' && st == MTBL_FAILED);
        if (!n->require_all && ok) return 1;
        all &= ok;
        if (n->require_all && !all) return 0;
    }
    return n->require_all;
}

/* a node's (or a forced node's) new state with its end time and its radio message (GBL 0x164f0 - the player's table
 * only, once per node index: msim's finished callback); silent: the original sets the state without the message call */
static void finish_s(mtbl_logic *ml, int t, int i, int ok, const mtbl_world *w, int silent)
{
    ml->state[t][i] = ok ? MTBL_OK : MTBL_FAILED;
    ml->ended[t][i] = ml->now;
    ml->silent = silent;
    if (w && w->finished) w->finished(w->user, t, i, ok);
    ml->silent = 0;
}

static int table_ok(const mtbl_logic *ml, int t) { return t >= 0 && t < ml->table_count; }
static int node_ok(const mtbl_logic *ml, int t, int i) { return table_ok(ml, t) && i >= 0 && i < MTBL_MAX_NODES && i <= ml->tables[t].node_count; }

/* GBL 0x16810: one node, once a frame */
static void run_node(mtbl_logic *ml, int t, int i, const mtbl_world *w)
{
    const mtbl_node *n = &ml->tables[t].nodes[i];
    const char *tg = (n->target[0] && strcmp(n->target, "NULL") != 0) ? n->target : NULL;
    int32_t now_s = ml->now / 1000;
    int elapsed, test = 1, all_lost = 0, cnt = 0, nok = 0, st = MTBL_ACTIVE, msg = 0, j;
    if (!node_ready(ml, t, i)) return;
    if (ml->started[t][i] == -1) {   /* armed: the time it first ran (+0x4f; a RESET keeps it, a RE-ARM clears it) */
        ml->started[t][i] = ml->now;
        if (w && w->started) w->started(w->user, t, i);
    }
    /* the condition: every attached target passes its kind's test; NO target attached passes (the flag starts true) */
    switch (n->kind) {
    case MTBL_K_DESTROY1: case MTBL_K_DESTROY2: case MTBL_K_SCAN: case MTBL_K_ALLMAIN: case MTBL_K_REACH: case MTBL_K_LEAVE:
    case MTBL_K_PROTECT:
        cnt = w && w->targets ? w->targets(w->user, t, i, n->kind, tg, &nok) : 0;
        test = n->kind == MTBL_K_LEAVE ? nok == 0 : n->kind == MTBL_K_PROTECT ? nok == 0 : nok >= cnt;
        all_lost = n->kind == MTBL_K_PROTECT && nok >= cnt;   /* protect: test = none lost, all_lost = every one */
        break;
    default: break;
    }
    if (n->kind == MTBL_K_REACH && t != 0 && ml->current[t] != i) test = 0;   /* a group's reach: its current node only */
    if (n->kind == MTBL_K_ALLMAIN)   /* and every OTHER main ('M', node +0x5d) node of the table succeeded */
        for (j = 0; j < ml->tables[t].node_count && test; j++)
            if (j != i && ml->tables[t].nodes[j].objective == 'M' && ml->state[t][j] != MTBL_OK) test = 0;
    /* the timer: delay < now - armed, whole seconds (clock / 182); a negative delay never passes */
    elapsed = (int32_t)n->delay >= 0 && ml->started[t][i] >= 0 && (int32_t)n->delay < now_s - ml->started[t][i] / 1000;
    switch (n->kind) {
    case MTBL_K_TIMER: case MTBL_K_ORDER: case MTBL_K_HOLD: case MTBL_K_TIMER2:
        if (elapsed) { st = MTBL_OK; msg = 1; }
        break;
    case MTBL_K_PROTECT:
        /* before the timer: a target lost fails it - on the player's table at once (6, with the failure message), on
         * another table state 8 (unfinished, re-tested; the star's result counts it failed) until ALL are lost (6, no
         * second message; none attached: 6 at once, silently). The timer passed: none lost -> 5 with the message, any
         * lost -> 6 without */
        if (elapsed) { if (test) { st = MTBL_OK; msg = 1; } else st = MTBL_FAILED; }
        else {
            if (!test) { st = t == 0 ? MTBL_FAILED : MTBL_PENDING; msg = 1; }
            if (all_lost) st = MTBL_FAILED;
        }
        break;
    case MTBL_K_STAR_WIN: case MTBL_K_STAR_LOSE:
        /* the star's result outright (no "already set" test) with its outcome line (0x165e0); this node succeeds */
        if (!table_ok(ml, n->other_table)) break;   /* out of range: stays pending */
        ml->result[n->other_table] = n->kind == MTBL_K_STAR_WIN ? 2 : 3;
        ml->result_time[n->other_table] = ml->now;
        st = MTBL_OK; msg = 1;
        break;
    case MTBL_K_TOGGLE:   /* flips node (other_table, other_node)'s shown flag (+0x5c) */
        if (!node_ok(ml, n->other_table, n->other_node)) break;
        ml->shown[n->other_table][n->other_node] = !ml->shown[n->other_table][n->other_node];
        st = MTBL_OK; msg = 1;
        break;
    case MTBL_K_FAIL: case MTBL_K_SUCCEED: {
        /* out of range, or the target finished (5 / 6): this node stays pending; else the target's state and end time,
         * its message (0x164f0 for ITS table), then this node succeeds with its own */
        int ot = n->other_table, on = n->other_node;
        if (!node_ok(ml, ot, on) || ml->state[ot][on] == MTBL_OK || ml->state[ot][on] == MTBL_FAILED) break;
        finish_s(ml, ot, on, n->kind == MTBL_K_SUCCEED, w, 0);
        st = MTBL_OK; msg = 1;
        break;
    }
    case MTBL_K_RESET:
        /* every node of the table back to unfinished (3) with no end time - their armed times kept, the announce flags
         * kept - the star's result and its time cleared; this node succeeds */
        if (!table_ok(ml, n->other_table)) break;
        for (j = 0; j < ml->tables[n->other_table].node_count; j++) {
            ml->state[n->other_table][j] = MTBL_ACTIVE;
            ml->ended[n->other_table][j] = -1;
        }
        ml->result[n->other_table] = 0;
        ml->result_time[n->other_table] = -1;
        st = MTBL_OK; msg = 1;
        break;
    case MTBL_K_REARM:
        /* node (other_table, other_node) back to unfinished (3) with no armed / end time (its announce flag kept) */
        if (!node_ok(ml, n->other_table, n->other_node)) break;
        ml->state[n->other_table][n->other_node] = MTBL_ACTIVE;
        ml->started[n->other_table][n->other_node] = -1;
        ml->ended[n->other_table][n->other_node] = -1;
        st = MTBL_OK; msg = 1;
        break;
    default:
        /* every other kind (destroy, visit, finish, reach, leave, start, instant, unknown): the condition -> 5, else the
         * timer -> 6 (timed out) */
        if (test) { st = MTBL_OK; msg = 1; }
        else if (elapsed) { st = MTBL_FAILED; msg = 1; }
        break;
    }
    if (st == MTBL_OK || st == MTBL_FAILED) { finish_s(ml, t, i, st == MTBL_OK, w, !msg); return; }
    ml->state[t][i] = (uint8_t)st;   /* 3 (from 0 / 3), or 8 */
    if (st != MTBL_ACTIVE) ml->ended[t][i] = ml->now;   /* 8: the end time, each frame */
}

/* GBL 0x170a0, once a frame: each table in order - its nodes in order (each once), then its star result, then its
 * current order node */
void mtbl_logic_step(mtbl_logic *ml, int32_t now, const mtbl_world *w)
{
    int t, i;
    ml->now = now;
    for (t = 0; t < ml->table_count; t++) {
        for (i = 0; i < ml->tables[t].node_count; i++) run_node(ml, t, i, w);
        if (!ml->result[t]) {
            /* every main ('M') node succeeded (none: at once) -> 2; any failed (6, or 8 on another table) -> 3; the
             * time limit (header +0x0c, seconds, > 0) reached -> 4: now - the table's start time (+0x04: the loader's
             * -1, then 0x16050 at the mission start sets it, with node 0's armed time, to the clock - 0, as the clock
             * only runs in the main loop (0x15820) and 0x15750 zeroed it before the load) */
            int all = 1, failed = 0;
            for (i = 0; i < ml->tables[t].node_count; i++) {
                int st = ml->state[t][i];
                if (ml->tables[t].nodes[i].objective != 'M') continue;
                if (st == MTBL_FAILED || st == MTBL_PENDING) { all = 0; failed = 1; }
                else if (st != MTBL_OK) all = 0;
            }
            if (all) ml->result[t] = 2;
            else if (failed) ml->result[t] = 3;
            else if ((int32_t)ml->tables[t].time_limit > 0 && (int64_t)(now / 1000) >= (int64_t)(int32_t)ml->tables[t].time_limit) ml->result[t] = 4;
            if (ml->result[t]) ml->result_time[t] = now;
        }
        /* the current order: the first ready node whose kind has no bit above 0xffff, else -1 */
        ml->current[t] = -1;
        for (i = 0; i < ml->tables[t].node_count; i++)
            if (node_ready(ml, t, i) && (ml->tables[t].nodes[i].kind & 0xFFFF0000u) == 0) { ml->current[t] = i; break; }
    }
}
