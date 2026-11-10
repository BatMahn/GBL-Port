/*
 * mtbl.h - mission logic: the scene's MTBL state tables as GBL MW2.EXE runs them
 * (loader 0x4dff0, the per-frame runner 0x170a0, the node 0x16810, activation
 * 0x16740 / 0x166a0, per-target tests 0x16120 / 0x16190 / 0x16280, messages
 * 0x164f0 / 0x165e0). tools/audit/mtbl_diff.py runs the original's runtime under
 * an emulator against this one on every GBL scene's tables.
 *
 * One table per group (table 0 = mission objectives and messages; table n =
 * the group whose GPS names state table n). A table is its chunk (counted from
 * the 'MTBL' tag) cut into 151-byte nodes; node k's anchor L = 0x5a + 151k:
 *   L-0x34 u32 kind        what the node does (see MTBL_K_*)
 *   L-0x30 'V'             shown on the objectives/nav list (else hidden)
 *   L-0x2e byte            1: all dependency items must hold; 0: any one
 *   L-0x2a 8 x {char type, u8 node, u8 table, pad}
 *                          'C' that node finished, 'S' succeeded, 'F' failed
 *   L-0x0a u32             timer delay (game ticks)
 *   L-0x05 'M' / 'O' / 'N' main / optional objective / internal step
 *   L-0x02 u16 id, L+0   start radio message (SNDS)
 *   L+0x09 u16 id, L+0x0b finish radio message
 *   L+0x14 u16 id, L+0x16 target record (nav point, group, area, UserStar)
 *   L+0x1f i16, L+0x21 i16 other table / node (for kinds that act on others)
 *   L+0x23 text            objective text
 * Node state at runtime: 0 loaded, 3 run, 5 succeeded, 6 failed, 8 (see MTBL_PENDING).
 * The timer (L-0x0a, seconds, -1 none) passes when delay < floor(now s) - floor(armed s); kinds 1, 2, 8, 0x20, 0x100,
 * 0x2000 (and every other kind with a condition) FAIL when it passes before their condition holds (0x16810).
 */
#ifndef MW2_MTBL_H
#define MW2_MTBL_H

#include <stddef.h>
#include <stdint.h>

#define MTBL_MAX_NODES 48
#define MTBL_ITEMS 8

enum {
    MTBL_K_TIMER     = 0x0,       /* succeeds when the delay has passed */
    MTBL_K_DESTROY1  = 0x1,       /* succeeds when all targets are destroyed */
    MTBL_K_DESTROY2  = 0x2,
    MTBL_K_PROTECT   = 0x4,       /* fails if a target is destroyed before the delay */
    MTBL_K_SCAN      = 0x8,       /* targets visited/seen by the side */
    MTBL_K_START     = 0x10,      /* always active (a table's start node) */
    MTBL_K_ALLMAIN   = 0x20,      /* targets visited and every other main objective done */
    MTBL_K_REACH     = 0x100,     /* the table's unit (player for table 0) is within the target's radius */
    MTBL_K_ORDER     = 0x200,     /* standing AI order (the engine's default when a group has no node);
                                     finishes when its delay passes */
    MTBL_K_HOLD      = 0x400,     /* standing AI order, behaviour code 10 in the dispatcher (0x10014520) */
    MTBL_K_TIMER2    = 0x800,
    MTBL_K_LEAVE     = 0x2000,    /* the player is outside every target's radius */
    MTBL_K_STAR_WIN  = 0x10000,   /* star `other_table`'s mission succeeds (engine 0x1000a1d0: status 2) */
    MTBL_K_STAR_LOSE = 0x20000,   /* star `other_table`'s mission fails (status 3) */
    MTBL_K_TOGGLE    = 0x40000,   /* toggle the visibility of node other_node */
    MTBL_K_FAIL      = 0x80000,   /* force node (other_table, other_node) to fail */
    MTBL_K_SUCCEED   = 0x100000,  /* force it to succeed */
    MTBL_K_RESET     = 0x200000,  /* reset table other_table */
    MTBL_K_INSTANT   = 0x1000,    /* no condition: succeeds as soon as it is ready (0x1000a1d0) */
    MTBL_K_REARM     = 0x400000   /* re-arm node (other_table, other_node), then succeed */
};

typedef struct { char type; int node, table; } mtbl_item;   /* type 0 = unused */

typedef struct {
    uint32_t  kind;
    int       visible, require_all;
    char      objective;            /* 'M', 'O' or 'N' */
    int       priority;             /* the byte before it (L-6), written raw to MW2MSN.CFG: 0 default, 1 primary, 2 secondary,
                                     * 4 tertiary, 8 return (the shell's labels and scoring) */
    mtbl_item items[MTBL_ITEMS];
    int       item_count;
    int       need, lock;           /* L-4 / L-3: members per target (0 = all), command lock (engine 0x10014ba0) */
    uint32_t  delay;
    char      sound_start[10], sound_finish[10], target[10];
    int       target_id;
    int       other_table, other_node;
    char      text[64];
} mtbl_node;

typedef struct {
    int       index;                /* table number (0 = objectives) */
    uint32_t  time_limit;           /* seconds, 0 = none (header +0x0c, engine 0x1003f968) */
    char      win_sound[10], lose_sound[10];   /* header +0x12 / +0x1d: the star's outcome lines (gene001S / gene001F;
                                                * engine 0x10009f50 queues them; time exceeded says BET68) */
    int       node_count;
    mtbl_node nodes[MTBL_MAX_NODES];
} mtbl_table;

/* chunk: the MTBL chunk *including* its 8-byte tag/size header. */
int  mtbl_decode(const uint8_t *chunk, size_t len, mtbl_table *out);
const char *mtbl_kind_name(uint32_t kind);

/* ---- interpreter (runs the tables the way the engine's runtime does) ---- */

#define MTBL_MAX_TABLES 64
/* node states (GBL node +0): 0 loaded, 3 run at least once (or re-armed / reset), 5 succeeded, 6 failed, 8 a protect
 * node of another table with a target lost (unfinished; its star's result counts it failed) */
enum { MTBL_WAITING = 0, MTBL_ACTIVE = 3, MTBL_OK = 5, MTBL_FAILED = 6, MTBL_PENDING = 8 };

typedef struct {
    void *user;
    /* The node's targets (GBL 0x4e5a0: as it is created, every unit, nav point and thing whose record name is the node's
     * target and whose class, widened (|0x730 / |3), shares a bit with the node's kind is attached to it - up to 16).
     * Returns how many are attached and in *n_ok how many pass the kind's per-target test (0x16810): destroyed (kinds
     * 1, 2, 4: 0x16120), inspected / visited (8, 0x20: 0x16190), the table's unit within reach (0x100, 0x2000: 0x16280).
     * target is NULL for none ("NULL"). */
    int (*targets)(void *user, int table, int node, uint32_t kind, const char *target, int *n_ok);
    /* a node finished (for logs, radio messages, objectives); mtbl_logic.silent is set when the original sets the state
     * without its message call (0x164f0) */
    void (*finished)(void *user, int table, int node, int ok);
    /* a node was armed (its start time set) */
    void (*started)(void *user, int table, int node);
} mtbl_world;

typedef struct {
    mtbl_table tables[MTBL_MAX_TABLES];
    int        table_count;
    uint8_t    state[MTBL_MAX_TABLES][MTBL_MAX_NODES];
    int32_t    started[MTBL_MAX_TABLES][MTBL_MAX_NODES];   /* time the node first ran (+0x4f), -1 */
    int32_t    ended[MTBL_MAX_TABLES][MTBL_MAX_NODES];     /* time it succeeded / failed (+0x53), -1 */
    int        shown[MTBL_MAX_TABLES][MTBL_MAX_NODES];
    /* per star (table) mission result (+0x38), GBL 0x170a0: 0 running, 2 success, 3 failed, 4 time exceeded; set by
     * STAR_WIN / STAR_LOSE nodes or: all main ('M') nodes done -> 2, any failed -> 3, the time limit (header +0x0c, s)
     * reached -> 4 */
    int        result[MTBL_MAX_TABLES];
    int32_t    result_time[MTBL_MAX_TABLES];
    int        current[MTBL_MAX_TABLES];                   /* the group's current order node, -1 (0 at load) */
    /* radio announce flags, per node INDEX (DAT_000b6fe0 - one array, the player's table only speaks): a node's success /
     * failure line plays once (0x164f0); nothing clears them (RESET and RE-ARM leave them) */
    uint8_t    announced[MTBL_MAX_NODES];
    int32_t    now;                                        /* the step's time (ms) */
    int        silent;                                     /* set during a finished() call that has no message */
} mtbl_logic;

/* scene: payload of the scene record (e.g. CYANSCN1). */
int  mtbl_logic_init(mtbl_logic *ml, const uint8_t *scene, size_t len);
/* One frame at time `now` (ms): each table's nodes once, in order, then its result and current node (0x170a0). Timer
 * delays are mission seconds (clock / 182). */
void mtbl_logic_step(mtbl_logic *ml, int32_t now, const mtbl_world *w);

#endif
