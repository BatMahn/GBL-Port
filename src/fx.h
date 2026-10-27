/*
 * fx.h - weapon effects (impacts, explosions, muzzle flashes), as the 3D editions' engine
 * builds them.
 *
 * Game data:
 *   MW2_SHT1 (BWD)  OBJ + XPLO pairs, one per pool slot: the OBJ's model is a flat card
 *                   (LAS0_0, XPL0_0, PPC0_0, MZL0_0, DEATH0_0...); XPLO payload
 *                   { i16 slot index, i16 texture slot, i16 effect type, i16 0 }.
 *   mw2_xpl1 (BWD)  texture slots: a run of BMPJ { u16 cel id, name } lists the frames, then
 *                   BMID { u16 slot } + BSEC { u16 slot, u16 rate } per slot.
 * Engine (3dfx MW2.DLL):
 *   0x10045d00      projectile class (weapon table visual) + what was hit -> effect type;
 *                   per type at most 2 alive within 5 m; a free pool slot of the type
 *   0x1025b168      per type: duration (ticks), sound, light flag (28-byte entries)
 *   0x1002ce50      frame stepping: every `rate` ticks the next frame, looping (32 max)
 */
#ifndef MW2_FX_H
#define MW2_FX_H

#include <stdint.h>
#include "prj.h"

#define FX_TYPES   36   /* the base engine's 32; GBL's table (0xb0d04) has 36 */
#define FX_LIVE    256
#define FX_FRAMES  32
#define FX_CARD_SLOTS 16   /* pool slots kept per type for the 3D cards (MZL0: 16) */

/* hit flags (engine projectile flags) */
#define FX_HIT_UNIT    0x100
#define FX_HIT_STRUCT  0x200
#define FX_HIT_GROUND  0x400
#define FX_HIT_PLAYER  0x2000

typedef struct {
    int   cel[FX_FRAMES];   /* texture archive (CEL) ids */
    int   frames, rate;     /* rate: ticks per frame */
} fx_anim;

typedef struct {
    int     slots[FX_TYPES];          /* pool slots of each type (XPLO count) */
    int     anim[FX_TYPES];           /* texture slot of the type's first pool object, -1 none */
    float   half_w[FX_TYPES], half_h[FX_TYPES];   /* card size (cm) */
    /* a colour type 3 card (0x3xxx, drawn by the billboard 0x1002a8e0): its foot (texel u 0, v > 0) and top (u 0, v 0)
     * corners, cm from the effect's position (x 0..2: game x, y, z; the pool objects are unrotated, 0x10045c30) */
    int     bb[FX_TYPES];
    float   bb_foot[FX_TYPES][3], bb_top[FX_TYPES][3];
    /* a card drawn as a 3D polygon (colour 0x8xxx: MZL / JET, 0xbxxx: DEATH - 0x1002a4b0 -> 0x10026e80 -> 0x1002c880, not
     * the billboard): each pool object's polygons (up to 2, 3-4 corners) x its OBJ scale, in the object's frame (game x,
     * y, z), texel u / v as 0-1 of the bitmap, and each polygon's normal as the engine holds it (the file's negated,
     * 0x10042140) - drawn when (corner 0 - eye) . normal < 0 (0x1002b9b0). Per pool slot (the type's XPLO entries in
     * MW2_SHT1 order, 0x1003ed70): the slots' models differ (MZL0_2 / MZL0_3: one polygon, the others two) */
    int     card_n[FX_TYPES][FX_CARD_SLOTS], card_nv[FX_TYPES][FX_CARD_SLOTS][2];
    float   card_v[FX_TYPES][FX_CARD_SLOTS][2][4][3], card_uv[FX_TYPES][FX_CARD_SLOTS][2][4][2], card_nrm[FX_TYPES][FX_CARD_SLOTS][2][3];
    fx_anim tex[512];                 /* texture slots (u16, engine 0x1002d060: < 0x200) */
    /* GBL MW2.EXE: each pool object shows the texture slot of its OWN card colour (bits 4-11 of a 0x3xxx word: XPL7_0..7
     * 0x3760..0x37d0 -> 118..125), while the effect's start restarts the animation of the XPLO's slot (0x52040 ->
     * FUN_00038880(+0x1c of the effect slot, 0) / FUN_00038830): per pool slot (MW2_SHT1 order) the shown slot, and 1 when
     * it runs from the effect's start (the restarted slot's own frames: every base type), else 0 - it runs on from its last
     * restart (fx_world.epoch): the burning effect 10 (XPLO 92-99 EXPF, cards 118-125 EXCF) and Arrow IV's 35 (XPLO
     * 186-189 EXP8, cards XPL0_0..3 92-95 EXPF); card_xplo: the XPLO's slot (restarted at the start). -1 none */
    int     card_slot[FX_TYPES][32], card_restart[FX_TYPES][32], card_xplo[FX_TYPES][32];
    /* GBL: each pool object's geometry radius (fx_model_radius), per type and pool slot - the reach of a damaging effect
     * started on that slot (0x52670: *(object +0x6c) +0x40) */
    float   radius[FX_TYPES][32];
} fx_defs;

typedef struct {
    int     type, alive;
    int     slot;                     /* the pool slot of its type: the first free one (0x10045d00) */
    float   pos[3];
    int32_t start, end;               /* ticks */
    int     has_rot;                  /* rot: the object's orientation (row-major, game coordinates) - a muzzle flash's or
                                       * jet flame's, its mount node's (0x10044a70); otherwise unrotated (0x10045c30) */
    float   rot[9];
    int     follow;                   /* GBL: an effect that follows its object (fx_flags 1, the burning one): the unit it rides
                                       * (-1 the player, an actor index; -2 none) - 0x52040 -> 0x1e870 hangs the effect object
                                       * on the unit's at offset 0 */
} fx_live;

typedef struct {
    fx_live e[FX_LIVE];
    /* GBL: when each texture slot's animation was last restarted (ticks; 0 = the mission's start, where the BSEC slots
     * start running, 0x4fae7): by an effect's start (its XPLO slot) and by a Flamer shot (flags 0x20: slots +0x0c..+7,
     * 0x53823 -> FUN_00038880 / FUN_00038830) */
    int32_t epoch[512];
} fx_world;

/* Load MW2_SHT1 and mw2_xpl1 from the models archive. 0 on success. */
int  fx_load(prj_archive *models, fx_defs *d);
/* The same with the scene's own pool: every record of the mission (the scene and its includes, bwd_mission_load) that holds
 * XPLO / BTHG chunks, in load order - the engine loads whatever the scene includes: GBL's PETESCN1 petesht1 (the torpedo
 * pool, no Flamer / Arrow IV / NARC, other effect scales), BRAN / ZACK zackSHT1, base GOAT / RUST / SABL MW2_SHT2, TEAL
 * tealsht1 + MW2_SHT1; scene NULL or none found: MW2_SHT1 */
int  fx_load_scene(prj_archive *models, fx_defs *d, const char *scene);
/* the mission's pool record names (as above), up to max; at least MW2_SHT1. Returns the count */
int  fx_pool_records(prj_archive *a, const char *scene, char names[][17], int max);
/* GBL MW2.EXE: a pool object's geometry radius +0x40 (cm) - its POLY model's vertices x the OBJ scale (0x50e10: 0 -> 1) as
 * the loader 0x50e60 builds the geometry (the objects after the first appended to it when the model's flags carry 0x1000,
 * a new geometry for 0x2000), then 0x39230 -> 0x389f0: the farthest vertex from the centre of the vertex box. -1 when the
 * model cannot be read */
float fx_model_radius(prj_archive *a, int model, const float scale[3]);

/* engine 0x10045d00: effect type for a projectile of weapon visual `visual` ending with `flags`;
 * -1 for none. */
int  fx_impact_type(int visual, int flags);

/* Muzzle effect types of weapon w (engine weapon table 0x1025a6c0 +0x08 / +0x0c), k = 0 or 1;
 * -1 none. Only types with pool objects show (0x17 ballistic, 0x1a missile, 0x1e PPC). */
int  fx_muzzle(int weapon, int k);
#define FX_JET 0x19   /* jump-jet flame (engine 0x1001bf90, at the two jet mounts while jetting) */

/* engine table 0x1025b168 */
int  fx_duration(int type);   /* ticks */
int  fx_sound(int type);      /* SNDS index, -1 none */
int  fx_light(int type);      /* lights its surroundings */
/* GBL's per-effect fields (0xb0d04, 0x2c bytes: +0x18 damaging, +0x20 damage per tick 16.16, +0x24 heat per tick 16.16,
 * +0x28 flags 1 follows its object / 2 splash); 0 / 256 / 0 / 0 in the base game (its 3Dfx row +0x18, the area damage's
 * 1/256 per tick) */
int  fx_damaging(int type);
int  fx_damage16(int type);
int  fx_heat16(int type);
int  fx_flags(int type);

/* Start an effect at pos (cm, game coordinates) at time `now` (ticks). Returns its index or -1
 * when the type has no free pool slot or two of it are already alive within 5 m. */
extern int fx_detail_low;   /* DISPLAY DETAIL low: no neighbouring effects of a type within 5 m */
int  fx_spawn(fx_world *w, const fx_defs *d, int type, const float pos[3], int32_t now);
void fx_update(fx_world *w, int32_t now);
/* show an effect another pool already started (msim's, fx_start): a copy of its entry (slot, times, follow, orientation) in
 * w, its XPLO slot's animation restarted at its start as fx_spawn does. Returns the index, -1 when w is full */
int  fx_adopt(fx_world *w, const fx_defs *d, const fx_live *src);
/* The cel id an effect shows at `now` (looping its animation), -1 if none. */
int  fx_frame(const fx_defs *d, const fx_world *w, const fx_live *e, int32_t now);
/* GBL: restart a texture slot's animation (FUN_00038880 frame 0 + FUN_00038830): fx_world.epoch */
void fx_anim_restart(fx_world *w, int slot, int32_t now);
/* the frame a texture slot shows at `now` as it runs from its last restart (the DOS animation table 0x13f850 stepping
 * every `rate` ticks, looping): a cel id, -1 none */
int  fx_slot_frame(const fx_defs *d, const fx_world *w, int slot, int32_t now);

#endif
