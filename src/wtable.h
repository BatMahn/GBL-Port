/* wtable.h - the simulation's own weapon table (3dfx MW2.DLL .data 0x1025a6c0, 0x58-byte
 * entries, same order as mek_weapons[]). Read by the fire routine 0x100437a0 and the
 * projectile spawn 0x10043cc0:
 *   type       +0x00  projectile kind (0-2 lasers L/M/S, 3 SRM/Narc, 4 LRM/Streak, 5 ballistic, 6 PPC, 7 Gauss/flamer)
 *   repeat     +0x10  re-fires while the trigger is held
 *   per_volley +0x1c  projectiles per trigger
 *   speed      +0x2c  projectile speed, cm per tick (182 Hz)
 *   damage     +0x30  damage per projectile (float)
 *   heat       +0x34  heat per projectile (engine heat scale; warn 65, shutdown 80)
 *   target_heat +0x38 heat added to the target on a hit (0x10044f60: ER PPC 3.5, flamer)
 *   ammo_per_ton +0x20 volleys per ton of ammunition (= the shell's figures)
 *   range      +0x40  maximum range, cm (LRM 1001 m, Gauss 1820 m)
 *   field3c    +0x3c  LRM 7500, Streak 25, else 0 (minimum range? not used)
 *   drop       +0x28  1 = the projectile falls under gravity (0x10043cc0)
 *   sound      +0x24  SNDS index played on a volley's first projectile (0x100437a0)
 *   visual     +0x04  projectile object: 0-2 LASER1-3, 3 missile, 5 MG bullet, 6 PPC, 8 Narc, 11 slug, 22 nuke
 *   refire     +0x44  ticks from the end of a volley until ready again
 *   interval   +0x48  ticks between projectiles in a volley
 *   life       +0x4c  projectile lifetime, ticks (speed x life: ER LL 1014 m, Gauss 1809 m) - less the volley timer
 *                     slot +0x10 at the release (0x10043cc0): an `interval`-60 laser loses ~54 ticks at 30 fps (ER ML
 *                     reaches ~358 m, not 509 m; DOSBox AQUASCN1: the Stormcrow's ER ML shots from 400-510 m miss)
 */
#ifndef MW2_WTABLE_H
#define MW2_WTABLE_H

#include "mek.h"   /* sim_game_gbl */

typedef struct { int type, repeat, per_volley, speed; float damage, heat; int refire, interval, life; float target_heat; int ammo_per_ton, range, field3c, drop, sound, visual; } sim_weapon;

static const sim_weapon sim_weapons[31] = {
    {4, 0, 20, 100, 2.0f, 0.6000000238418579f, 2534, 11, 905, 0.0f, 6, 100100, 7500, 0, 189, 3},  /* LRM 20 */
    {4, 0, 15, 100, 2.0f, 0.6000000238418579f, 2172, 22, 905, 0.0f, 8, 100100, 7500, 0, 189, 3},  /* LRM 15 */
    {4, 0, 10, 100, 2.0f, 0.800000011920929f, 1810, 45, 905, 0.0f, 12, 100100, 7500, 0, 189, 3},  /* LRM 10 */
    {4, 0, 5, 100, 2.0f, 0.800000011920929f, 1448, 45, 905, 0.0f, 24, 100100, 7500, 0, 189, 3},  /* LRM 5 */
    {3, 0, 6, 70, 3.0f, 1.340000033378601f, 1448, 60, 543, 0.0f, 15, 49686, 0, 0, 258, 3},  /* SRM 6 */
    {3, 0, 4, 70, 3.0f, 1.5f, 1267, 45, 543, 0.0f, 25, 49686, 0, 0, 258, 3},  /* SRM 4 */
    {3, 0, 2, 100, 3.0f, 2.0f, 1086, 45, 543, 0.0f, 50, 49686, 0, 0, 258, 3},  /* SRM 2 */
    {4, 0, 6, 70, 3.0f, 1.3339999914169312f, 1086, 45, 543, 0.0f, 15, 49686, 25, 1, 189, 3},  /* Streak SRM-6 */
    {4, 0, 4, 70, 3.0f, 1.5f, 1448, 45, 543, 0.0f, 25, 49686, 25, 1, 189, 3},  /* Streak SRM-4 */
    {4, 0, 2, 70, 3.0f, 2.0f, 1448, 45, 543, 0.0f, 50, 49686, 25, 1, 189, 3},  /* Streak SRM-2 */
    {5, 1, 1, 300, 1.0f, 0.0f, 11, 11, 57, 0.0f, 200, 17500, 0, 1, 226, 5},  /* Machine Gun */
    {7, 0, 1, 280, 18.0f, 0.5f, 724, 90, 646, 0.0f, 8, 182000, 0, 0, 218, 11},  /* Gauss Rifle */
    {5, 1, 1, 240, 3.0f, 0.5f, 45, 45, 331, 0.0f, 45, 80000, 0, 0, 1, 11},  /* LB 2-X AC */
    {5, 1, 1, 240, 7.5f, 0.5f, 45, 45, 289, 0.0f, 20, 70000, 0, 0, 1, 11},  /* LB 5-X AC */
    {5, 1, 1, 240, 15.0f, 1.0f, 45, 11, 247, 0.0f, 10, 60000, 0, 0, 1, 11},  /* LB 10-X AC */
    {5, 1, 1, 240, 30.0f, 3.0f, 45, 11, 186, 0.0f, 5, 45000, 0, 0, 1, 11},  /* LB 20-X AC */
    {5, 1, 1, 220, 3.0f, 0.5f, 22, 11, 316, 0.0f, 45, 70000, 0, 0, 187, 11},  /* Ultra AC/2 */
    {5, 1, 1, 220, 7.5f, 0.5f, 22, 11, 271, 0.0f, 20, 60000, 0, 0, 187, 11},  /* Ultra AC/5 */
    {5, 1, 1, 220, 15.0f, 1.5f, 22, 11, 226, 0.0f, 10, 50000, 0, 0, 187, 11},  /* Ultra AC/10 */
    {5, 1, 1, 220, 30.0f, 3.5f, 22, 11, 181, 0.0f, 5, 40000, 0, 0, 187, 11},  /* Ultra AC/20 */
    {7, 0, 1, 80, 20.0f, 12.0f, 90, 181, 181, 0.0f, 8, 80000, 1000, 0, 227, -1},  /* Flamer */
    {6, 0, 1, 70, 17.0f, 12.5f, 452, 22, 1705, 3.5f, -1, 120000, 0, 0, 227, 6},  /* ER PPC */
    {0, 1, 1, 280, 8.0f, 6.0f, 108, 60, 362, 0.0f, -1, 101920, 0, 0, 199, 0},  /* ER Laser (Large) */
    {1, 1, 1, 280, 5.0f, 2.5f, 90, 60, 181, 0.0f, -1, 50960, 0, 0, 228, 1},  /* ER Laser (Medium) */
    {2, 1, 1, 280, 3.0f, 1.0f, 72, 60, 90, 0.0f, -1, 25480, 0, 0, 243, 2},  /* ER Laser (Small) */
    {0, 1, 2, 280, 4.0f, 3.5f, 45, 60, 289, 0.0f, -1, 81536, 0, 0, 277, 0},  /* Pulse Laser (Large) */
    {1, 1, 2, 280, 2.0f, 1.5f, 36, 11, 144, 0.0f, -1, 40768, 0, 0, 234, 1},  /* Pulse Laser (Medium) */
    {2, 1, 2, 280, 1.0f, 0.5f, 18, 11, 72, 0.0f, -1, 20384, 0, 0, 278, 2},  /* Pulse Laser (Small) */
    {3, 0, 1, 80, 0.0f, 4.0f, 2534, 45, 1448, 0.0f, -1, 200000, 0, 1, 189, 8},  /* Narc Missile Beacon */
    {5, 1, 1, 600, 1.0f, 0.0f, 11, 5, 271, 0.0f, 24, 10000, 0, 1, 1, 5},  /* Anti-Missile System */
    {22, 1, 1, 0, 100.0f, 0.0f, 36200, 181, 5430, 0.0f, 1, 200000, 0, 0, 189, 22},  /* Nuke */
};

/* short names (+0x50, 8 chars), as the HUD weapon list shows them */
static const char *const sim_weapon_short[31] = {
    "LRM20", "LRM15", "LRM10", "LRM5", "SRM6", "SRM4", "SRM2", "SSRM6", "SSRM4", "SSRM2", "MGun", "GAUSS", "xAC2", "xAC5", "xAC10", "xAC20", "uAC2", "uAC5", "uAC10", "uAC20", "FLAMER", "PPC", "LLASER", "MLASER", "SLASER", "LPLAS", "MPLAS", "SPLAS", "NARC", "AMS", "NUKE"
};

/* ---- Ghost Bear's Legacy (MW2.EXE "REL_11 Addon1"): the sim's 35-entry table at VA 0xafecc (file 0x90ecc), 0x68 bytes
 * each: 16 new bytes {+0x00 fn0 per-frame callback, +0x04 fn1 fire callback (0x53823 default), +0x08 flags, +0x0c
 * texture base} and the base DOS 0x58-byte record after them (+0x10 type, +0x14 visual, +0x18 / +0x1c muzzle effects,
 * +0x20 repeat, +0x24 f14 (0: the default fire spawns no projectile), +0x28 guided, +0x2c per volley, +0x30 ammo per ton,
 * +0x34 sound, +0x38 drop, +0x3c speed, +0x40 damage (int), +0x44 heat 16.16, +0x48 target heat 16.16, +0x4c f3c, +0x50
 * range, +0x54 refire, +0x58 interval, +0x5c life, +0x60 name). Entries 0-30: the port's base table above with the
 * fields GBL changed against the base DOS table (0x9ee58) set to GBL's values (the rest - timings from the 3Dfx table
 * the base port uses - kept); 31-34 new, GBL's figures as they stand (182 Hz ticks, as the port's clock).
 * Used only when sim_game_gbl() (MW2_GAME=gbl). */
static const sim_weapon sim_weapons_gbl[35] = {
    {4, 0, 20, 100, 2.0f, 0.6000000238418579f, 2534, 11, 905, 0.0f, 6, 100100, 7500, 0, 189, 3},  /* LRM 20 */
    {4, 0, 15, 100, 2.0f, 0.6000000238418579f, 2172, 22, 905, 0.0f, 8, 100100, 7500, 0, 189, 3},  /* LRM 15 */
    {4, 0, 10, 100, 2.0f, 0.800000011920929f, 1810, 45, 905, 0.0f, 12, 100100, 7500, 0, 189, 3},  /* LRM 10 */
    {4, 0, 5, 100, 2.0f, 0.800000011920929f, 1448, 45, 905, 0.0f, 24, 100100, 7500, 0, 189, 3},  /* LRM 5: ammo 24 (base DOS 12; the 3Dfx table already 24) */
    {3, 0, 6, 70, 3.0f, 1.340000033378601f, 1448, 60, 543, 0.0f, 15, 49686, 0, 0, 258, 3},  /* SRM 6 */
    {3, 0, 4, 70, 3.0f, 1.5f, 1267, 45, 543, 0.0f, 25, 49686, 0, 0, 258, 3},  /* SRM 4 */
    {3, 0, 2, 100, 3.0f, 2.0f, 1086, 45, 543, 0.0f, 50, 49686, 0, 0, 258, 3},  /* SRM 2 */
    {4, 0, 6, 70, 3.0f, 1.3339999914169312f, 1086, 45, 543, 0.0f, 15, 49686, 25, 1, 189, 3},  /* Streak SRM-6 */
    {4, 0, 4, 70, 3.0f, 1.5f, 1448, 45, 543, 0.0f, 25, 49686, 25, 1, 189, 3},  /* Streak SRM-4 */
    {4, 0, 2, 70, 3.0f, 2.0f, 1448, 45, 543, 0.0f, 50, 49686, 25, 1, 189, 3},  /* Streak SRM-2 */
    {5, 1, 1, 300, 1.0f, 0.0f, 11, 11, 57, 0.0f, 200, 17500, 0, 1, 226, 5},  /* Machine Gun */
    {7, 0, 1, 280, 18.0f, 0.5f, 724, 90, 646, 0.0f, 8, 182000, 0, 0, 218, 11},  /* Gauss Rifle */
    {5, 1, 1, 240, 4.0f, 0.5f, 45, 45, 331, 0.0f, 45, 80000, 0, 0, 1, 11},  /* LB 2-X AC: damage 4 (base 3) */
    {5, 1, 1, 240, 10.0f, 0.5f, 45, 45, 289, 0.0f, 20, 70000, 0, 0, 1, 11},  /* LB 5-X AC: 10 (7) */
    {5, 1, 1, 240, 20.0f, 1.0f, 45, 11, 247, 0.0f, 10, 60000, 0, 0, 1, 11},  /* LB 10-X AC: 20 (15) */
    {5, 1, 1, 240, 40.0f, 3.0f, 45, 11, 186, 0.0f, 5, 45000, 0, 0, 1, 11},  /* LB 20-X AC: 40 (30) */
    {5, 1, 1, 220, 4.0f, 0.5f, 22, 11, 316, 0.0f, 45, 70000, 0, 0, 187, 11},  /* Ultra AC/2: 4 (3) */
    {5, 1, 1, 220, 10.0f, 0.5f, 22, 11, 271, 0.0f, 20, 60000, 0, 0, 187, 11},  /* Ultra AC/5: 10 (7) */
    {5, 1, 1, 220, 25.0f, 1.5f, 22, 11, 226, 0.0f, 10, 50000, 0, 0, 187, 11},  /* Ultra AC/10: 25 (15) */
    {5, 1, 1, 220, 50.0f, 3.5f, 22, 11, 181, 0.0f, 5, 40000, 0, 0, 187, 11},  /* Ultra AC/20: 50 (30) */
    {8, 1, 10, 20, 0.0f, 3.5f, 182, 4, 364, 5.0f, -1, 5000, 0, 0, 456, 10},  /* Flamer: redone - kind 8, 10 per volley, 50 m, target heat 5 */
    {6, 0, 1, 70, 17.0f, 12.5f, 452, 22, 1705, 3.5f, -1, 120000, 0, 0, 227, 6},  /* ER PPC */
    {0, 1, 1, 280, 8.0f, 6.0f, 108, 60, 362, 0.0f, -1, 101920, 0, 0, 199, 0},  /* ER Laser (Large) */
    {1, 1, 1, 280, 5.0f, 2.5f, 90, 60, 181, 0.0f, -1, 50960, 0, 0, 228, 1},  /* ER Laser (Medium) */
    {2, 1, 1, 280, 3.0f, 1.0f, 72, 60, 90, 0.0f, -1, 25480, 0, 0, 243, 2},  /* ER Laser (Small) */
    {0, 1, 2, 280, 4.0f, 3.5f, 45, 60, 289, 0.0f, -1, 81536, 0, 0, 277, 0},  /* Pulse Laser (Large) */
    {1, 1, 2, 280, 2.0f, 1.5f, 36, 11, 144, 0.0f, -1, 40768, 0, 0, 234, 1},  /* Pulse Laser (Medium) */
    {2, 1, 2, 280, 1.0f, 0.5f, 18, 11, 72, 0.0f, -1, 20384, 0, 0, 278, 2},  /* Pulse Laser (Small) */
    {12, 0, 1, 80, 0.0f, 4.0f, 2534, 45, 1448, 0.0f, 6, 200000, 0, 1, 189, -1},  /* NARC: kind 12, no object, 6 per ton (base -1) */
    {5, 1, 24, 600, 0.0f, 0.0f, 22, 11, 182, 0.0f, 20, 10000, 0, 1, 455, 5},  /* AMS: the interceptor (fn0 0x54943 / fn1 0x54aa3) */
    {4, 1, 1, 100, 1000.0f, 0.0f, 182, 181, 5430, 50.0f, 1, 200000, 0, 0, 461, 22},  /* Nuke: kind 4, flies at 100 */
    {3, 0, 2, 70, 0.0f, 1.340000033378601f, 910, 60, 709, 0.0f, 50, 49686, 0, 0, 258, 10},  /* Inferno SRM-2 (new) */
    {9, 0, 2, 10, 50.0f, 1.340000033378601f, 910, 60, 2730, 0.0f, 50, 49686, 0, 0, 466, 34},  /* Torpedo (new: kind 9) */
    {3, 0, 2, 70, 0.0f, 1.340000033378601f, 910, 60, 709, 0.0f, 50, 49686, 0, 0, 258, 10},  /* Inferno SSRM-2 (new: guided) */
    {10, 0, 1, 100, 7.0f, 10.0f, 3640, 60, 2002, 0.0f, 5, 200200, 0, 0, 464, 35},  /* ARROW IV (new: kind 10) */
};
/* GBL's new per-entry fields and the base fields the port's sim_weapon does not carry: +0x08 flags (1 volley sound on
 * the first projectile, 2 on every one, 4 selectable - weapon cycling / groups 0x53af0 / 0x53bb0 / 0x53c50 / 0x53d00 and
 * the AI's choice skip the others, 0x10 sluggish guidance (0x51cd0: the steering eases in over 0x2d8 ticks), 0x20
 * animated textures (+0x0c..+7, the Flamer), 0x40 the object not stretched (0x54730), 0x80 NARC beacon (0x516b0)),
 * +0x0c the texture base, +0x18 / +0x1c the muzzle effects, +0x24 f14 (0: no projectile from the default fire path),
 * +0x28 guided; fn0 / fn1 not 0x53823: the AMS (msim.c gbl_ams_*) */
typedef struct { int flags, tex, fx0, fx1, f14, guided; } sim_weapon_gx;
static const sim_weapon_gx sim_weapons_gbl_x[35] = {
    {5, 0, 26, 27, 1, 1}, {5, 0, 26, 27, 1, 1}, {5, 0, 26, 27, 1, 1}, {5, 0, 26, 27, 1, 1},
    {5, 0, 26, 27, 1, 0}, {5, 0, 26, 27, 1, 0}, {5, 0, 26, 27, 1, 0},
    {5, 0, 26, 27, 1, 1}, {5, 0, 26, 27, 1, 1}, {5, 0, 26, 27, 1, 1},
    {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0},
    {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0},
    {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0}, {5, 0, 23, 24, 1, 0},
    {0x65, 118, 26, 27, 1, 0},                                           /* Flamer */
    {5, 0, 30, 31, 1, 0},
    {5, 0, 28, 29, 1, 0}, {5, 0, 28, 29, 1, 0}, {5, 0, 28, 29, 1, 0},
    {5, 0, 28, 29, 1, 0}, {5, 0, 28, 29, 1, 0}, {5, 0, 28, 29, 1, 0},
    {0x85, 0, 23, 24, 1, 1},                                             /* NARC */
    {0x0a, 0, 23, 24, 0, 0},                                             /* AMS */
    {5, 0, -1, -1, 1, 0},                                                /* Nuke */
    {5, 0, 26, 27, 1, 0}, {0x15, 0, 32, 33, 1, 1}, {5, 0, 26, 27, 1, 1}, {5, 0, -1, -1, 1, 1},   /* Inf SRM, Torpedo, Inf SSRM, Arrow IV */
};
static const char *const sim_weapon_short_gbl[35] = {
    "LRM20", "LRM15", "LRM10", "LRM5", "SRM6", "SRM4", "SRM2", "SSRM6", "SSRM4", "SSRM2", "MGun", "GAUSS", "xAC2", "xAC5", "xAC10", "xAC20", "uAC2", "uAC5", "uAC10", "uAC20", "FLAMER", "PPC", "LLASER", "MLASER", "SLASER", "LPLAS", "MPLAS", "SPLAS", "NARC", "AMS", "NUKE",
    "Inf SRM", "Torpedo", "InfSSRM", "ArrowIV"
};
#define SIM_W_AMS 29          /* the anti-missile system's index (GBL 0x548d0 tests slot +4 == 0x1d) */
#define SIM_W_NARC 28

/* the game's table (mek.h sim_game_gbl: MW2_GAME=gbl) */
static inline const sim_weapon *sim_wtab(void) { return sim_game_gbl() ? sim_weapons_gbl : sim_weapons; }
static inline int sim_wcount(void) { return sim_game_gbl() ? 35 : 31; }
static inline const char *sim_weapon_name(int w) { return w >= 0 && w < sim_wcount() ? (sim_game_gbl() ? sim_weapon_short_gbl[w] : sim_weapon_short[w]) : "?"; }
/* GBL only: the extra fields (NULL in the base game) */
static inline const sim_weapon_gx *sim_wx(int w) { return sim_game_gbl() && w >= 0 && w < 35 ? &sim_weapons_gbl_x[w] : (const sim_weapon_gx *)0; }

#endif
