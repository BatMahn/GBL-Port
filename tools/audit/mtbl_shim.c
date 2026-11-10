/* mtbl_shim.c - the port's mission-table interpreter (src/mtbl.c) behind a flat C API for the differential test
 * tools/audit/mtbl_diff.py, which runs the same tables in the original GBL MW2.EXE's runtime (0x170a0) under an
 * emulator. Build: cc -shared -fPIC -O1 -Isrc tools/audit/mtbl_shim.c src/mtbl.c src/bwd.c src/datapath.c src/prj.c
 * The targets callback answers from tables the test sets before each step; the finished callback applies the
 * announce rule of msim.c cb_finished (table 0, once per node index, not when the node finished silently) and logs
 * the radio lines it would queue. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mtbl.h"

static mtbl_logic g_ml;
static int g_cnt[MTBL_MAX_TABLES][MTBL_MAX_NODES], g_ok[MTBL_MAX_TABLES][MTBL_MAX_NODES];
static int g_log[4096][3], g_nlog;

static int cb_targets(void *u, int t, int i, uint32_t kind, const char *tg, int *n_ok)
{
    (void)u; (void)kind; (void)tg;
    *n_ok = g_ok[t][i];
    return g_cnt[t][i];
}
static void cb_finished(void *u, int t, int i, int ok)
{
    (void)u;
    if (t == 0 && !g_ml.announced[i] && !g_ml.silent) {
        g_ml.announced[i] = 1;
        if (g_nlog < 4096) { g_log[g_nlog][0] = t; g_log[g_nlog][1] = i; g_log[g_nlog][2] = ok ? 5 : 6; g_nlog++; }
    }
}

int shim_init(const uint8_t *scene, int len) { g_nlog = 0; memset(g_cnt, 0, sizeof g_cnt); memset(g_ok, 0, sizeof g_ok); return mtbl_logic_init(&g_ml, scene, (size_t)len); }
void shim_targets(int t, int i, int cnt, int nok) { g_cnt[t][i] = cnt; g_ok[t][i] = nok; }
void shim_step(int now_ms)
{
    mtbl_world w;
    memset(&w, 0, sizeof w);
    w.targets = cb_targets;
    w.finished = cb_finished;
    mtbl_logic_step(&g_ml, now_ms, &w);
}
int shim_tables(void) { return g_ml.table_count; }
int shim_nodes(int t) { return g_ml.tables[t].node_count; }
int shim_kind(int t, int i) { return (int)g_ml.tables[t].nodes[i].kind; }
int shim_state(int t, int i) { return g_ml.state[t][i]; }
int shim_started(int t, int i) { return g_ml.started[t][i]; }
int shim_ended(int t, int i) { return g_ml.ended[t][i]; }
int shim_shown(int t, int i) { return g_ml.shown[t][i]; }
int shim_current(int t) { return g_ml.current[t]; }
int shim_result(int t) { return g_ml.result[t]; }
int shim_result_time(int t) { return g_ml.result_time[t]; }
int shim_nlog(void) { return g_nlog; }
int shim_log(int k, int f) { return g_log[k][f]; }
