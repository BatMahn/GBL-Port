/* gbl_lab.c - the Combat Simulator (state 7, FUN_0002cc80) and the Mech Lab (state 9, FUN_000374e0) of Ghost Bear's
 * Legacy. Spec: docs/gbl/D_ia_mechlab.md (addresses are GBL MW2SHELL.EXE VAs; re/shellgbl_decomp.c). DOSBox references:
 * /home/claude/gbl/ref/D_ia*.png, D2_*.png.
 *
 * Kept from the base shell (tools/gbl/gbl_common.c): the Mech Lab editor model (medit, ed_* helpers, mek_masses, the engine
 * table, armour rules), shell_dialog (FUN_00027a40), the fonts. GBL's own here: the screens' clip / hotspot layouts, the
 * 32-row mech table use, the 35-row weapon table (the four new weapons 31-34 come from a local table - the sim's
 * base table stops at 30), the 14-weapon limit, the variant slots (200 x 13 bytes at 0xacfdc).
 *
 * The clips use the core's gs_* slots (which animate SHP clips, anchor 0x80 clips at their bottom centre and blank empty
 * SHP shapes, as FUN_0003ca50 / FUN_0003d0c0 do).
 */

/* ======================================================================================================================
 * clip helpers */
static void gl_show(int i, int on) { gs_flags(i, GS_HIDDEN, !on); }   /* FUN_0003ce00 (show) / FUN_0003cdc0(i, 0x20) (hide) */
static const char *gl_save_dir(void) { const char *s = getenv("MW2_SAVE_DIR"); return s ? s : g_dir; }

/* FUN_0002cc00: the Combat Simulator's chassis count by the star-0 slot-0 pilot name (FUN_0003e330(0) + 0x28) */
static int gl_ia_count(void)
{
    const char *n = g_gstar[0].slot[0].name1;
    if (!strcmp(n, GBL_CHEATS[CH_WIDOW])) return 31;       /* + Elemental, Tarantula */
    if (!strcmp(n, GBL_CHEATS[CH_LITTLEGUY])) return 30;   /* + Elemental */
    return 29;                                             /* the 24 clan + 5 Inner Sphere 'Mechs */
}

/* ======================================================================================================================
 * Combat Simulator (state 7, FUN_0002cc80) */
static const char *const GIA_SCEN[9] = {"jackscn1", "chedscn1", "edamscn1", "provscn1", "goudscn1", "colbscn1", "goatscn1",
                                        "whizscn1", "swisscn1"};   /* 0x80360 (base had ricoscn1 too) */
static int gia_scen_i;   /* DAT_000adbe4 */
/* EBX of FUN_0002cc80 = the previous screen: 1 the hall, 9 the Mech Lab, 10 the simulation. The core's state machine
 * does not pass the previous state to the screens (g_prev is already 7 here), so this file tracks it: what screen 7 last
 * returned (9 / 10) decides where the next entry comes from; anything else means the hall (the only other way in). */
static int gia_from = 1;

/* FUN_0002c810(scenario, fill, planet): the scenario's BRF2 record - SUPS -> the -b= camo string, with fill the two SDSC
 * stars, PDSC -> the planet (0..11, returned). */
static int gl_brf2(const char *scen, int fill)
{
    prj_record r;
    size_t o = 12;
    int planet = 0, nsd = 0;
    if (mission_record(scen, "BRF2", &r) != 0) return 0;
    while (o + 8 <= r.size) {
        uint32_t sz = rd32le(r.data + o + 4), len;
        const uint8_t *p = r.data + o + 8;
        if (sz < 8 || o + sz > r.size) break;
        len = sz - 8;
        if (!memcmp(r.data + o, "SUPS", 4)) {
            snprintf(g_camo, sizeof g_camo, "%.*s", (int)(len < sizeof g_camo ? len : sizeof g_camo - 1), (const char *)p);
        } else if (!memcmp(r.data + o, "SDSC", 4) && fill && nsd < 2 && len >= 16) {
            /* {+0 ?, +4 tonnage, +8 ?, +0xc size} then 16-byte loadouts: FUN_0003e350(s, 0, +0xc, +8, +4), then
             * FUN_0003e0b0(k, loadout k) for k < +0xc - twice, as written (0x2c8xx) */
            int s = nsd++, first = (int32_t)rd32le(p), tons = (int32_t)rd32le(p + 4), v8 = (int32_t)rd32le(p + 8), vc = (int32_t)rd32le(p + 12), pass, k;
            gstar_set(s, 0, vc, v8, tons);
            for (pass = 0; pass < 2; pass++)
                for (k = 0; k < vc && k < 3 && 16 + 16 * (k + 1) <= (int)len; k++) {
                    char lo[17];
                    snprintf(lo, sizeof lo, "%.16s", (const char *)p + 16 + 16 * k);
                    gstar_slot(k, lo, NULL, NULL);   /* "hlp01std" (goudscn1) matches no chassis: the slot drops (ASSUMED) */
                }
            if (s == 1) g_skill_base = first;   /* DAT_0007b728 = the second SDSC's first int */
        } else if (!memcmp(r.data + o, "PDSC", 4) && len >= 4) {
            planet = (int)(p[0] | p[1] << 8) - 1;
            if (planet < 0 || planet > 11) planet = 0;
        }
        o += sz;
    }
    prj_record_free(&r);
    if (fill) { gstar_set(0, -1, -1, -1, -1); gstar_slot(0, NULL, NULL, NULL); }
    return planet;
}
/* leaving with EXIT (1) or -3: the campaign star back from pilot +0x132 (FUN_0003e350(0, -1, +0x13a, +0x13e, +0x142),
 * FUN_0003e0b0(k, +0x146 + 0x40k + 4, +0x14) for k < +0x13e) */
static void gia_restore(void)
{
    gbl_pilot *p = pil();
    int k;
    if (!p) return;
    gstar_set(0, -1, p->ia_star.max, p->ia_star.size, p->ia_star.tons);
    for (k = 0; k < p->ia_star.size && k < 3; k++) gstar_slot(k, p->ia_star.slot[k].loadout, p->ia_star.slot[k].name1, NULL);
}
static void gia_showcase(int idx)   /* clip 0x19: agbcs<pic> (103,238) 0x48, 20 fps */
{
    char nm[16];
    if (idx < 0 || idx >= 32) { gs_close(0x19); return; }   /* " MECH LAB " entry: no chassis (ASSUMED empty) */
    snprintf(nm, sizeof nm, "agbcs%s", GBL_MECHS[idx].pic);
    gs_load(0x19, nm, 103, 238, 0x48, 20);
}
static int gbl_combat_sim(void)
{
    /* hotspots 0x7f514 (5 used of 25) */
    static const gbl_hot HOT[5] = {
        {473, 9, 608, 80, 0, 0, NULL},          /* 0 LAUNCH */
        {408, 402, 496, 426, 0, 0, NULL},       /* 1 EXIT */
        {178, 406, 242, 430, 0, 0, NULL},       /* 2 the name box -> MECH LAB */
        {242, 406, 263, 430, 172, 414, NULL},   /* 3 NEXT chassis */
        {154, 406, 178, 430, 172, 414, NULL}};  /* 4 PREV chassis */
    int from = gia_from, cnt = gl_ia_count(), idx, planet, phase = 0, playing_a = 0, ret = -1;
    char pc[16];
    gia_from = 1;
    if (from == 1 && pil()) { pil()->ia_star = g_gstar[0]; pil_save(); }   /* the campaign star kept at +0x132 */
    /* 0x2ce30: "pinkscn1" (dead store); a new random scenario from the hall or after a mission, else the previous one */
    if (from == 1 || from == 10) gia_scen_i = (int)lround((double)(rand() & 0x7fff) / 32767.0 * 89.0) / 10;
    if (gia_scen_i < 0 || gia_scen_i > 8) gia_scen_i = 0;
    snprintf(g_scen, sizeof g_scen, "%s", GIA_SCEN[gia_scen_i]);
    planet = gl_brf2(g_scen, from == 1);
    /* the chassis label: from the hall FUN_0003e2e0(0); otherwise the original reads pilot +0x72 (the pilot's star copy,
     * slot 0). D2_ia_back.png (back from the lab) shows the IA star's chassis, so the live star 0 slot 0 is read in both
     * cases (INFERRED: same value whenever the pilot record holds the current star) */
    idx = g_gstar[0].size > 0 ? g_gstar[0].slot[0].mech : -1;
    if (getenv("GBL_LAB_DEBUG")) fprintf(stderr, "ia from %d scen %s camo %s skill %d chassis %d cnt %d star0 %s/%s/%s star1 %s/%s/%s size %d/%d\n", from, g_scen, g_camo, g_skill_base, idx, cnt,
        g_gstar[0].slot[0].loadout, g_gstar[0].slot[1].loadout, g_gstar[0].slot[2].loadout, g_gstar[1].slot[0].loadout, g_gstar[1].slot[1].loadout, g_gstar[1].slot[2].loadout, (int)g_gstar[0].size, (int)g_gstar[1].size);   /* TEST ONLY */
    gbl_bg(9);   /* DATABASE entry 8 */
    {
        char pn[16];
        snprintf(pn, sizeof pn, "aplan%02d", (planet + 1) % 100);
        snprintf(pc, sizeof pc, "aplan%02dc", (planet + 1) % 100);
        gs_load(0, pn, 156, 52, 0x42, 0);
    }
    gs_load(0x11, "wgbcs001", 344, 129, 0x261, 0);   /* grid } "COMBAT PARAMETERS" phase */
    gs_load(0x0f, "wgbcs010", 370, 284, 0x261, 0);   /*      } (wgbcs010 is on the disc only) */
    gs_load(0x12, "wgbcs002", 337, 127, 0x261, 0);   /* "RANDOMIZING CONDITIONS" grid */
    gs_load(0x14, "wgbcs005", 59, 237, 0x48, 0);     /* left gauge */
    gs_load(0x15, "wgbcs006", 540, 133, 0x48, 0);    /* right gauge */
    gs_load(0x16, "wgbcs007", 139, 36, 0x48, 0);     /* planet reticle */
    gs_load(0x17, "wgbcs008", 325, 35, 0x48, 0);     /* red text block */
    gs_load(0x10, "wgbcs004", 473, 9, 0x41, 0);      /* LAUNCH */
    gs_show_frame(0x10, 0);
    gs_load(0x18, "wgbcs009", 408, 402, 0x41, 0);    /* EXIT */
    gs_load(0x13, "wgbcs003", 154, 406, 0x44, 0);    /* the name box */
    gia_showcase(idx);
    gsnd_play(0x7a, 0x46, 1);   /* the screen's loop */
    while (ret == -1) {
        gbl_input *in;
        int h, lab_hover;
        /* the clip cycle (loop head): 0x11 + 0x0f, then 0x12, forever. It opens with 0x11 + 0x0f (ASSUMED: every
         * capture taken soon after entry - D_ia, D_ia2, D_ia_hover_lab, D2_ia_back - shows that phase) */
        if (!playing_a) { gs_restart(0x11); gs_restart(0x0f); playing_a = 1; }
        if (phase == 0 && gs_done(0x11) && gs_done(0x0f)) {
            gl_show(0x11, 0); gl_show(0x0f, 0);
            gs_restart(0x12); phase = 1;
        } else if (phase == 1 && gs_done(0x12)) {
            gl_show(0x12, 0);
            gs_restart(0x11); gs_restart(0x0f); phase = 0;
        }
        if (gs_done(0)) gs_load(0, pc, 156, 52, 0x4a, 0);   /* the planet: aplanNN once, then aplanNNc looping */
        gbl_bg(9);
        draw_bg();
        h = gh_hit(HOT, 5, NULL, g_in.mx, g_in.my);
        gs_show_frame(0x18, h == 1 ? 1 : 0);   /* EXIT lit while hovered */
        gs_update();
        lab_hover = h == 2;
        /* the label (font 0x22): the padded table name, " MECH LAB      " for none / the name box hovered; green 0x5a
         * (remap 0xa9558) over the box and its arrows, else 0xa9458 (identity) */
        gtext(GF_DEFAULT, 172, 414, lab_hover || idx < 0 || idx >= 32 ? " MECH LAB      " : GBL_MECHS[idx].name, h >= 2 ? 0x5a : -1);
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) { gia_restore(); return -3; }
        h = gh_hit(HOT, 5, NULL, in->mx, in->my);
        if (in->lclick && h >= 0) {
            switch (h) {
            case 0: {   /* LAUNCH: frame 1, flip, sound 0x50, INSTMAP1.BWD (FUN_0001d430(2, 1): l1gostbr vs l1jadefn) */
                char err[256];
                prj_archive *a;
                gs_show_frame(0x10, 1);
                gl_show(0x10, 1);
                draw_bg(); gs_update(); gbl_draw_cursor(in->mx, in->my); gbl_frame();
                gsnd_play(0x50, 0x32, 0);
                if ((a = prj_open(sim_archive(), err, sizeof err))) { lance_write_instmap(a, gl_save_dir(), 2, 1); prj_close(a); }
                else fprintf(stderr, "gbl: cannot open %s: %s\n", sim_archive(), err);
                gia_from = 10;
                ret = 10;
                break;
            }
            case 1: gia_restore(); ret = 1; break;                               /* EXIT -> the hall (context 0) */
            case 2: gstar_set(0, -1, -1, -1, -1); gia_from = 9; ret = 9; break;  /* the Mech Lab (context 2) */
            case 3: case 4: {   /* NEXT / PREV chassis: skip what the star's tonnage refuses; -1 = "" (MECH LAB) */
                int tries = 0;
                gstar_set(0, -1, -1, -1, -1);
                do {
                    if (h == 3) { if (++idx >= cnt) idx = -1; }
                    else { if (--idx == -2) idx = cnt - 1; }
                } while (!gstar_slot(0, idx < 0 ? "" : GBL_MECHS[idx].code, NULL, NULL) && ++tries < 64);
                gia_showcase(idx);
                if (getenv("GBL_LAB_DEBUG")) fprintf(stderr, "ia chassis %d (%s) star tons %d\n", idx, idx >= 0 ? gmech_name(idx) : "-", (int)g_gstar[0].tons);   /* TEST ONLY */
                break;
            }
            }
        }
        if (ret == -1) {
            int r = gbl_global_menu();   /* FUN_00026f10: Esc / the right button */
            if (r == -3) { gia_restore(); ret = -3; }
            else if (r != -1) ret = r;
        }
    }
    return ret;
}

/* ======================================================================================================================
 * the Mech Lab (state 9, FUN_000374e0; helpers FUN_00036720 list + turntable, FUN_00036c20 next variant, FUN_000365d0,
 * page code FUN_00034b30 = base FUN_00030db0 moved) */

/* the shell's weapon table 0x80ad4 (35 x 40 bytes {heat, damage, ?, short, medium, range m, tons x 100, crits, ammo / ton,
 * name}), read from GBL MW2SHELL.EXE. Rows 0-19 and 21-27 equal the port's mek_weapons[]; GBL changed 20 Flamer (2 / 4 /
 * 10 m), 28 "NARC" (450 m, 6 / t), 29 "AMS" (damage 2, 20 / t - D2_c17.png shows "AMS #1 (ammo 2T/40)"), 30 Nuke (20 t,
 * 5 crits, 1 / t) and added 31-34 */
static const mek_weapon GL_WTAB[35] = {
    /* name                   heat  dmg unk short med range  t x100 crits ammo/t */
    {"LRM 20",                  6,  -1, -1,   7,  14, 1000,  500,  4,   6},
    {"LRM 15",                  5,  -1, -1,   7,  14, 1000,  350,  2,   8},
    {"LRM 10",                  4,  -1, -1,   7,  14, 1000,  250,  1,  12},
    {"LRM 5",                   2,  -1, -1,   7,  14, 1000,  100,  1,  24},
    {"SRM 6",                   4,  -2, -1,   3,   6,  497,  150,  1,  15},
    {"SRM 4",                   3,  -2, -1,   3,   6,  497,  100,  1,  25},
    {"SRM 2",                   2,  -2, -1,   3,   6,  497,   50,  1,  50},
    {"Streak SRM-6",            4,  -2, -1,   4,   8,  497,  300,  2,  15},
    {"Streak SRM-4",            3,  -2, -1,   4,   8,  497,  200,  1,  25},
    {"Streak SRM-2",            2,  -2, -1,   4,   8,  497,  100,  1,  50},
    {"Machine Gun",             0,   2, -1,   1,   2,  175,   25,  1, 200},
    {"Gauss Rifle",             1,  15,  2,   7,  15, 1820, 1200,  6,   8},
    {"LB 2-X AC",               1,   2,  4,  10,  20,  800,  500,  3,  45},
    {"LB 5-X AC",               1,   5,  3,   8,  15,  700,  700,  4,  20},
    {"LB 10-X AC",              2,  10, -1,   6,  12,  600, 1000,  5,  10},
    {"LB 20-X AC",              6,  20, -1,   4,   8,  450, 1200,  9,   5},
    {"Ultra AC/2",              1,   2,  2,   9,  18,  700,  500,  2,  45},
    {"Ultra AC/5",              1,   5, -1,   7,  14,  600,  700,  3,  20},
    {"Ultra AC/10",             3,  10, -1,   6,  12,  500, 1000,  4,  10},
    {"Ultra AC/20",             7,  20, -1,   4,   8,  400, 1200,  8,   5},
    {"Flamer",                  3,   2, -1,   2,   4,   10,   50,  1,   0},
    {"ER PPC",                 15,  15, -1,   7,  14,  746,  600,  2,   0},
    {"ER Laser (Large)",       12,  10, -1,   8,  15, 1019,  400,  1,   0},
    {"ER Laser (Medium)",       5,   7, -1,   5,  10,  510,  100,  1,   0},
    {"ER Laser (Small)",        2,   5, -1,   2,   4,  255,   50,  1,   0},
    {"Pulse Laser (Large)",    10,  10, -1,   6,  14,  815,  600,  2,   0},
    {"Pulse Laser (Medium)",    4,   7, -1,   4,   8,  408,  200,  1,   0},
    {"Pulse Laser (Small)",     2,   3, -1,   2,   4,  204,  100,  1,   0},
    {"NARC",                    0,   0, -1,   4,   8,  450,  200,  1,   6},
    {"AMS",                     1,   2, -1,  -1,  -1,   -1,   50,  1,  20},
    {"Nuke",                    0,   0, -1,  -1,  -1,   -1, 2000,  5,   1},
    {"Inferno SRM-2",           2,  -2, -1,   3,   6,  497,   50,  1,  50},
    {"Torpedo",                 4,  -2, -1,   4,   8,  497,  300,  2,  15},
    {"Inferno SSRM-2",          2,  -2, -1,   4,   8,  497,  100,  1,  50},
    {"ARROW IV",               10, -20, -1,  -1,  -1, 1000, 1200, 12,   5}};
static const mek_weapon *gl_wp(int w) { return w >= 0 && w < 35 ? &GL_WTAB[w] : NULL; }
static const char *gl_item_name(uint32_t id, char *b, size_t n)
{
    const mek_weapon *w;
    /* a weapon's criticals carry its instance: "Machine Gun #1" (MEASURED D2_cust_crit.png); ammo as the base (ASSUMED) */
    if (id && id < 5000 && id % 100 && (w = gl_wp((int)(id / 100)))) { snprintf(b, n, "%s #%u", w->name, (unsigned)(id % 100)); return b; }
    if (id >= 10000 && id < 13500 && (id - 10000) % 100 && (w = gl_wp((int)((id - 10000) / 100)))) { snprintf(b, n, "%s ammo", w->name); return b; }
    return mek_item_name(id, b, n);
}
/* WEAPONS TABLE 0x82a28: 35 rows 11 px apart from y 86, no group gaps; 30 Nuke only with DAT_000a9bd0 (DOOMSDAY),
 * 32 Torpedo only with DAT_000a9bd4 (LOVECRAFT) */
static const int GBL_WEAPONS_TABLE[35] = {22, 23, 24, 21, 20, 25, 26, 27, 29, 11, 12, 13, 14, 15, 10, 16, 17, 18, 19,
                                          6, 5, 4, 9, 8, 7, 3, 2, 1, 0, 28, 31, 33, 34, 30, 32};
static int gl_wtable(int *out)
{
    int i, n = 0;
    for (i = 0; i < 35; i++) {
        int w = GBL_WEAPONS_TABLE[i];
        if ((w == 30 && !g_cheat_nuke) || (w == 32 && !g_cheat_torpedo)) continue;
        out[n++] = w;
    }
    return n;
}
#define GL_MAXW 14   /* the design's weapon array 0xabf40: 14 entries (loops to 0x38 bytes, FUN_00032f50); base 10 */
/* the masses: the base's rules (mek_masses: engine, gyro, cockpit, jets, internal, armour, ammo, heat-sink guess) with
 * GBL's weapons (every weapon in the design's weapon array, the AMS included - FUN_00032f50 sums the table's mass over
 * 0xabf40) and equipment = MASC (tonnage / 25; MEASURED D2_c08 Horned Owl 1.00 T, D2_c28 Executioner 4.00 T) */
static void gl_masses(const mek_def *d, mek_mass *m)
{
    int k, l, s6001 = 0, masc = 0;
    mek_masses(d, m);
    m->weapons = 0;
    for (k = 0; k < d->item_count; k++) {
        const mek_weapon *w = d->items[k].id < 5000 && d->items[k].id % 100 ? gl_wp((int)(d->items[k].id / 100)) : NULL;
        if (w) m->weapons += w->weight_x100 / 100.0;
    }
    for (l = 0; l < MEK_LOC_COUNT; l++)
        for (k = 0; k < d->loc[l].slot_count && k < MEK_SLOTS; k++) { if (d->loc[l].slots[k] == 6001) s6001 = 1; if (d->loc[l].slots[k] == 5001) masc = 1; }
    m->equip = masc ? masc_tons((int)d->tonnage) : 0;
    m->dbl = s6001; m->nsinks = m->dbl ? m->dissip / 2 : m->dissip; m->sinks = m->nsinks > 10 ? m->nsinks - 10 : 0;
    m->used = m->engine + m->gyro + m->cockpit + m->sinks + m->jets + m->internal + m->armor + m->weapons + m->ammo + m->equip;
    if (!m->dbl && m->used > d->tonnage + 1e-9 && m->sinks >= 10 && m->dissip % 2 == 0) {   /* as mek_masses */
        m->dbl = 1; m->nsinks = m->dissip / 2; m->sinks = m->nsinks > 10 ? m->nsinks - 10 : 0;
        m->used = m->engine + m->gyro + m->cockpit + m->sinks + m->jets + m->internal + m->armor + m->weapons + m->ammo + m->equip;
    }
}
static void gl_ed_masses(const medit *e, mek_mass *m)   /* = ed_masses on gl_masses */
{
    gl_masses(&e->d, m);
    m->dbl = e->dbl; m->nsinks = e->dbl ? m->dissip / 2 : m->dissip; m->sinks = m->nsinks > 10 ? m->nsinks - 10 : 0;
    m->armor = e->halftons * 0.5;
    m->equip = e->masc;
    m->used = m->engine + m->gyro + m->cockpit + m->sinks + m->jets + m->internal + m->armor + m->weapons + m->ammo + m->equip;
}
static void gl_add_ammo(medit *e, int item)   /* FUN_000332b0: 10 bins a weapon, 25 bins, 78 unassigned */
{
    mek_def *d = &e->d;
    uint32_t id = d->items[item].id;
    const mek_weapon *w = gl_wp((int)(id / 100));
    if (!w || w->ammo_per_ton <= 0 || d->item_count + d->link_count >= MEK_MAX_ITEMS) return;
    if (d->link_count >= 25 || ed_ammo_bins(e, id) >= 10 || e->npool >= 78) return;
    d->items[d->item_count + d->link_count].id = 10000 + id;
    d->items[d->item_count + d->link_count].linked_id = (int32_t)id;
    d->link_count++;
    ed_pool_add(e, (uint16_t)(10000 + id), 1);
}
static void gl_add_weapon(medit *e, int w)   /* ADD WEAPON: the weapon, plus a ton of its ammo when it uses any */
{
    mek_def *d = &e->d;
    const mek_weapon *wp = gl_wp(w);
    uint32_t id;
    int inst, k, nw = 0;
    if (!wp) return;
    for (k = 0; k < d->item_count; k++) if (d->items[k].id < 5000) nw++;
    if (nw >= GL_MAXW || d->item_count + d->link_count >= MEK_MAX_ITEMS || e->npool + wp->crits > 78) return;
    for (inst = 1; inst < 99; inst++) {
        id = (uint32_t)(w * 100 + inst);
        for (k = 0; k < d->item_count; k++) if (d->items[k].id == id) break;
        if (k == d->item_count && !ed_count(e, (uint16_t)id)) break;
    }
    id = (uint32_t)(w * 100 + inst);
    memmove(d->items + d->item_count + 1, d->items + d->item_count, (size_t)d->link_count * sizeof d->items[0]);
    d->items[d->item_count].id = id; d->items[d->item_count].linked_id = -1;
    d->item_count++;
    ed_pool_add(e, (uint16_t)id, wp->crits);
    if (wp->ammo_per_ton > 0) gl_add_ammo(e, d->item_count - 1);
}

/* ---- the variant slots (0xacfdc: 200 x 13 bytes): 0 = <code>00std always, 1-99 <code>NNstd when MW2.PRJ has it,
 * 100-199 the files mek\<code>NNusr.mek (findfirst; slot 100 + NN) */
static char gl_var[200][13];
static int gl_vs;        /* DAT_000ada04 */
static int gl_m;         /* DAT_00084d60 the chassis */
static int gl_cnt = 24;  /* DAT_00084d38 */
static mek_def gl_d;     /* the shown design (FUN_00034b30) */
static int gl_have;
static int gl_mekdir(char *out, size_t n)   /* the install folder's mek directory, any case; 0 if found */
{
    DIR *dd = opendir(g_dir);
    struct dirent *de;
    int rc = -1;
    if (!dd) return -1;
    while ((de = readdir(dd))) if (!strcasecmp(de->d_name, "mek")) { snprintf(out, n, "%s/%s", g_dir, de->d_name); rc = 0; break; }
    closedir(dd);
    return rc;
}
static void gl_variants(void)
{
    const char *code = GBL_MECHS[gl_m].code;
    char err[256], dir[1100];
    prj_archive *a;
    int i;
    memset(gl_var, 0, sizeof gl_var);
    snprintf(gl_var[0], 13, "%s%02dstd", code, 0);
    if ((a = prj_open(gpath("MW2.PRJ"), err, sizeof err))) {   /* FUN_0001ca20 */
        int t = prj_find_type(a, "MEK");
        for (i = 1; i < 100; i++) {
            char nm[16];
            snprintf(nm, sizeof nm, "%s%02dstd", code, i);
            if (prj_find_id(a, t, nm) >= 0) snprintf(gl_var[i], 13, "%.12s", nm);
        }
        prj_close(a);
    }
    if (gl_mekdir(dir, sizeof dir) == 0) {   /* "mek\%s??usr.mek" */
        DIR *dd = opendir(dir);
        struct dirent *de;
        while (dd && (de = readdir(dd))) {
            const char *f = de->d_name;
            if (strlen(f) == 12 && !strncasecmp(f, code, 3) && isdigit((unsigned char)f[3]) && isdigit((unsigned char)f[4]) && !strncasecmp(f + 5, "usr.mek", 7)) {
                int s = 100 + (f[3] - '0') * 10 + (f[4] - '0'), k;
                snprintf(gl_var[s], 13, "%.8s", f);
                for (k = 0; gl_var[s][k]; k++) gl_var[s][k] = (char)tolower((unsigned char)gl_var[s][k]);
            }
        }
        if (dd) closedir(dd);
    }
}
static void gl_show_variant(void)   /* FUN_00034b30(slot name): load the MEK for the panels */
{
    gl_have = gl_var[gl_vs][0] && load_mek(gl_var[gl_vs], &gl_d) == 0;
}
static void gl_next_variant(void)   /* FUN_00036c20: the next non-empty slot, wrapping to 0 */
{
    int s = gl_vs + 1;
    while (s < 200 && !gl_var[s][0]) s++;
    gl_vs = s >= 200 ? 0 : s;
    gl_show_variant();
}
static void gl_prev_variant(void)   /* case 4: 0 -> 199, then down to the previous non-empty slot */
{
    int s = gl_vs == 0 ? 200 : gl_vs;
    s--;
    while (s >= 0 && !gl_var[s][0]) s--;
    gl_vs = s < 0 ? 0 : s;
    gl_show_variant();
}
static void gl_chassis(void)   /* FUN_00036720: turntable ashmp<pic> (310,383) 0x88 20 fps, the variant list, the design */
{
    char nm[16];
    snprintf(nm, sizeof nm, "ashmp%s", GBL_MECHS[gl_m].pic);
    gs_load(0x10, nm, 310, 383, 0x88, 20);   /* bottom-centre anchor: the 400x320 frames land at (110,63) + offsets */
    gl_variants();
    gl_show_variant();
}
/* the second header line: the table name cut at the first double space; 24-28 "I.S. - %s". Drawn as stored: the header
 * font's lower-case glyphs are capitals with their own widths (MEASURED: "Dire Wolf" matches D2_lab_main.png pixel for
 * pixel, "DIRE WOLF" is a pixel wider) */
static const char *gl_title(int m)
{
    static char t[48];
    if (m >= 24 && m <= 28) snprintf(t, sizeof t, "I.S. - %s", gmech_name(m));
    else snprintf(t, sizeof t, "%s", gmech_name(m));
    return t;
}
static int gl_voice;   /* DAT_000ada08 */
static void gl_voice_play(void)
{
    if (gl_voice > 0) gsnd_stop(gl_voice);
    gl_voice = GBL_MECHS[gl_m].voice;
    if (gl_voice > 0) gsnd_play(gl_voice, 0x28, 0);
}

/* ---- text */
static void gl_g(int x, int y, const char *t) { draw_text(&g_fthin, x, y, t, gbl_pal(), 6); }    /* 0xacbc8: 1 -> 6 grey */
static void gl_w(int x, int y, const char *t) { draw_text(&g_fthin, x, y, t, gbl_pal(), -1); }   /* 0xaccc8: 1 -> 1 white */
/* widget rows 0 / 1 (table 0x84130, callback 0x2fec0): centred at (320,4) / (320,28) in font 0x95ea8 = getter idx 0x1d
 * (entry 28, 0x3a2bb), remap 0xacbc8 (1 -> 6: grey 170 as the captures) */
static void gl_header(const char *l1, const char *l2)
{
    const font *fo = gfont(28);
    draw_text(fo, 320 - text_width(fo, l1) / 2, 4, l1, gbl_pal(), 6);
    draw_text(fo, 320 - text_width(fo, l2) / 2, 28, l2, gbl_pal(), 6);
}
/* heat sinks "count (dissipation)" for double sinks, the count alone for single ones (MEASURED D2_c03.png: Raven "11",
 * D2_lab_main.png: Dire Wolf "22 (44)"; the HEAT SINKS page's Count ASSUMED the same) */
static void gl_sinks_text(char *t, size_t n, const mek_mass *mm)
{
    if (mm->dbl) snprintf(t, n, "%d (%d)", mm->nsinks, mm->dissip); else snprintf(t, n, "%d", mm->nsinks);
}
static void gl_component_rows(const mek_mass *mm, int xl, int endo, int ferro, int tons, int edit)
{
    static const char *ROW[10] = {"Engine", "Gyro", "Cockpit", "Heat Sinks", "Jump Jets", "Internal", "Armor", "Weapons", "Ammo", "Equipment"};
    double v[10];
    char t[64], note[32];
    int i;
    v[0] = mm->engine; v[1] = mm->gyro; v[2] = mm->cockpit; v[3] = mm->sinks; v[4] = mm->jets; v[5] = mm->internal; v[6] = mm->armor;
    v[7] = mm->weapons; v[8] = mm->ammo; v[9] = mm->equip;
    gl_g(20, 86, "COMPONENT"); gl_g(98, 86, "MASS"); gl_g(147, 86, "NOTES");
    for (i = 0; i < 10; i++) {
        int y = 108 + 11 * i;
        if (edit && i != 1 && i != 2) gl_w(20, y, ROW[i]); else gl_g(20, y, ROW[i]);
        snprintf(t, sizeof t, "%.2f T", v[i]); gl_g(98, y, t);
        note[0] = 0;
        if (i == 0) snprintf(note, sizeof note, "%d%s", mm->rating, xl ? "XL" : "");
        if (i == 3) gl_sinks_text(note, sizeof note, mm);
        if (i == 4) snprintf(note, sizeof note, "%d", mm->njets);
        if (i == 5) snprintf(note, sizeof note, "%s", endo ? "Endo-S" : "Std");   /* GBL: "Endo-S" (captures) */
        if (i == 6) snprintf(note, sizeof note, "%s", ferro ? "Ferro-F" : "Std");
        if (note[0]) gl_g(147, y, note);
    }
    gl_g(20, 229, "Used Mass"); snprintf(t, sizeof t, "%.2f T", mm->used); gl_g(98, 229, t);
    gl_g(20, 240, "Max Mass"); snprintf(t, sizeof t, "%.2f T", (double)tons); gl_g(98, 240, t);
}
/* a design's weapon lines: "<name> #<n>" with inline "(ammo <t>T/<shots>)"; 14 lines from y0 every 11, "-" when empty.
 * sel: the item drawn white (-1 none) */
static void gl_weapon_lines(const mek_def *d, int x, int y0, int sel)
{
    int k, j, nw = 0;
    char t[96];
    for (k = 0; d && k < d->item_count && nw < GL_MAXW; k++) {
        const mek_weapon *w;
        int inst = 0, ammo = 0;
        if (d->items[k].id >= 5000 || !(w = gl_wp((int)(d->items[k].id / 100)))) continue;
        for (j = 0; j <= k; j++) if (d->items[j].id < 5000 && d->items[j].id / 100 == d->items[k].id / 100) inst++;
        for (j = d->item_count; j < d->item_count + d->link_count; j++) if (d->items[j].linked_id == (int32_t)d->items[k].id) ammo++;
        if (ammo) snprintf(t, sizeof t, "%s #%d (ammo %dT/%d)", w->name, inst, ammo, ammo * w->ammo_per_ton);
        else snprintf(t, sizeof t, "%s #%d", w->name, inst);
        if (k == sel) gl_w(x, y0 + 11 * nw, t); else gl_g(x, y0 + 11 * nw, t);
        nw++;
    }
    for (; nw < GL_MAXW; nw++) gl_g(x, y0 + 11 * nw, "-");
}

/* ---- the armour / criticals diagram: wgbmp4mp.shp (slot 12, hidden: the page code draws its 8 pieces). Pieces at the
 * base positions + 3 in y (the middle panel moved from y 52 to 55), MEASURED by template matching in D2_cust_crit.png.
 * The selected location as drawn (outline 32 (0,219,243), hatching 174), the others with the outline in entry 64
 * (0,182,224) and no hatching (MEASURED D2_cust_armor.png / D2_cust_crit.png: (0,182,227) on the VGA's 6-bit DAC) */
static const int GL_DIAG_XY[8][2] = {{291, 226}, {262, 261}, {302, 261}, {342, 261}, {226, 259}, {378, 259}, {255, 365}, {325, 365}};
static int gl_diag_hit(int x, int y)
{
    gslot *g = &g_gs[12];
    int p;
    if (!g->shp) return -1;
    for (p = 0; p < 8; p++) {
        shp_frame fr;
        int hit = 0;
        if (shp_decode(g->shp, g->shplen, p, &fr) != 0) continue;
        if (x >= GL_DIAG_XY[p][0] && y >= GL_DIAG_XY[p][1] && x < GL_DIAG_XY[p][0] + fr.w && y < GL_DIAG_XY[p][1] + fr.h) hit = fr.mask[(y - GL_DIAG_XY[p][1]) * fr.w + (x - GL_DIAG_XY[p][0])];
        shp_frame_free(&fr);
        if (hit) return p;
    }
    return -1;
}
static void gl_diag_draw(int sel)
{
    gslot *g = &g_gs[12];
    int o, x, y;
    if (!g->shp) return;
    for (o = 0; o < 8; o++) {
        int p = DIAG_ORDER[o];
        shp_frame fr;
        if (shp_decode(g->shp, g->shplen, p, &fr) != 0) continue;
        for (y = 0; y < fr.h; y++)
            for (x = 0; x < fr.w; x++) {
                int px = GL_DIAG_XY[p][0] + x, py = GL_DIAG_XY[p][1] + y;
                uint8_t ix = fr.pix[y * fr.w + x];
                if (!fr.mask[y * fr.w + x] || px >= W || py >= H) continue;
                if (p != sel) ix = ix == 32 ? 64 : ix == 174 ? 15 : ix;   /* not selected: outline 32 -> 64, hatching 174 -> 15 */
                memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, gbl_pal()[ix], 3);
            }
        shp_frame_free(&fr);
    }
}

/* ---- CUSTOMIZE: the editor pages, the base's (run_customize) with GBL's weapons table, 14 weapons and the WEAPONS AND
 * AMMO / WEAPON INFO block 4 lines lower (MEASURED D2_cust_weapons.png: ADD WEAPON y 251, WEAPON INFO 284, Type 306) */
static medit gl_E;
static int gl_cust_slot;   /* the user slot the design will take (100..199) */
static void gl_cust_click(int x, int y)
{
    medit *e = &gl_E;
    mek_mass mm;
    int pg = e->page, row, k;
    gl_ed_masses(e, &mm);
    if (x >= 14 && x <= 200 && y >= 108 && y < 218) {   /* the component rows */
        static const int PAGE[10] = {0, -1, -1, 3, 4, 5, 6, 7, 8, 9};
        row = (y - 108) / 11;
        if (row >= 0 && row < 10 && PAGE[row] >= 0) e->page = PAGE[row];
        return;
    }
    if (x >= 14 && x <= 200 && y >= 260 && y < 273) { e->page = 10; return; }   /* Assign Criticals */
    if (x >= 440 && x <= 626) {   /* the right panel */
        if (pg == 0) {
            int r = ed_rating(e);
            if (y >= 97 && y < 108) {   /* Type: XL <-> Std */
                if (ed_is_xl(e)) { for (k = 0; k < 12; k++) { if (e->d.loc[MEK_RT].slots[k] == 5850) e->d.loc[MEK_RT].slots[k] = 0; if (e->d.loc[MEK_LT].slots[k] == 5850) e->d.loc[MEK_LT].slots[k] = 0; } }
                else {
                    int sides[2] = {MEK_RT, MEK_LT}, sd, q;
                    for (sd = 0; sd < 2; sd++) for (q = 0; q < 2 && q < e->d.loc[sides[sd]].slot_count; q++) {
                        mek_loc *L = &e->d.loc[sides[sd]];
                        uint16_t was = L->slots[q];
                        if (was && was != 5850) { int kk, n = 0; for (kk = 0; kk < L->slot_count; kk++) if (L->slots[kk] == was) { L->slots[kk] = 0; n++; } ed_pool_add(e, was, n); }
                        L->slots[q] = 5850;
                    }
                }
            }
            if (y >= 163 && y < 174 && r + (int)e->d.tonnage <= 400) { e->d.walk_mp++; ed_sync_sinks(e); }   /* FASTER */
            if (y >= 174 && y < 185 && e->d.walk_mp > 1) {                                                 /* SLOWER */
                e->d.walk_mp--;
                while (e->d.jump_mp > e->d.walk_mp) { int kk; e->d.jump_mp--; for (kk = 98; kk >= 1; kk--) if (ed_count(e, (uint16_t)(7000 + kk))) { ed_remove(e, (uint16_t)(7000 + kk), 1); break; } }
                ed_sync_sinks(e);
            }
        } else if (pg == 3) {
            int per = e->dbl ? 2 : 1;
            if (y >= 97 && y < 108) {
                int cnt = (int)e->d.heat_sinking / per;
                for (k = 1; k < 99; k++) ed_remove(e, (uint16_t)(6000 + k), 9);
                e->dbl = !e->dbl; e->d.heat_sinking = (uint32_t)(cnt * (e->dbl ? 2 : 1)); ed_sync_sinks(e);
            }
            if (y >= 130 && y < 141) { e->d.heat_sinking += (uint32_t)per; ed_sync_sinks(e); }
            if (y >= 141 && y < 152 && (int)e->d.heat_sinking / per > 10) { e->d.heat_sinking -= (uint32_t)per; ed_sync_sinks(e); }
        } else if (pg == 4) {
            if (y >= 119 && y < 130 && e->d.jump_mp < e->d.walk_mp) { e->d.jump_mp++; ed_pool_add(e, ed_free_id(e, 7000), 1); }
            if (y >= 130 && y < 141 && e->d.jump_mp > 0) { int kk; e->d.jump_mp--; for (kk = 98; kk >= 1; kk--) if (ed_count(e, (uint16_t)(7000 + kk))) { ed_remove(e, (uint16_t)(7000 + kk), 1); break; } }
        } else if (pg == 5 && y >= 86 && y < 97) {
            if (ed_endo(e)) { for (k = 1; k < 50; k++) ed_remove(e, (uint16_t)(8000 + k), 9); }
            else for (k = 0; k < 7; k++) ed_pool_add(e, (uint16_t)(8001 + k), 1);
        } else if (pg == 6) {
            int loc = ARMOR_ORDER[e->armor_loc], alloc = ed_armor_alloc(e), torso = loc == MEK_RT || loc == MEK_LT || loc == MEK_CT;
            mek_loc *L = &e->d.loc[loc];
            if (y >= 119 && y < 130) {
                if (ed_ferro(e)) { for (k = 1; k < 50; k++) ed_remove(e, (uint16_t)(9000 + k), 9); }
                else for (k = 0; k < 7; k++) ed_pool_add(e, (uint16_t)(9001 + k), 1);
                ed_trim_armor(e);
            }
            if (y >= 141 && y < 152) e->halftons++;
            if (y >= 152 && y < 163 && e->halftons > 0) { e->halftons--; ed_trim_armor(e); }
            if (y >= 163 && y < 180) {
                int full = (int)(L->armor + L->rear_armor) >= ed_loc_max(e, loc), nofree = alloc >= ed_factor(e);
                if (!full && !nofree) { if (x < 566) L->armor++; else if (torso) L->rear_armor++; }
                else if (torso) { if (x < 566 && L->rear_armor > 0) { L->rear_armor--; L->armor++; } else if (x >= 566 && L->armor > 0) { L->armor--; L->rear_armor++; } }
            }
            if (y >= 197 && y < 212) { if (x < 566 && L->armor > 0) L->armor--; else if (x >= 566 && torso && L->rear_armor > 0) L->rear_armor--; }
        } else if (pg == 7 || pg == 8) {   /* the WEAPONS TABLE (click 0x33810, sound 0x52 DAT_000acfd0) */
            int list[35], n = gl_wtable(list);
            for (k = 0; k < n; k++) if (y >= 86 + 11 * k && y < 97 + 11 * k) { e->sel_table = k; gsnd_play(0x52, 0x28, 0); }
        } else if (pg == 9) {
            if (y >= 97 && y < 108) {
                int t = masc_tons((int)e->d.tonnage);
                if (e->masc) { ed_remove(e, 5001, 99); e->masc = 0; } else { ed_pool_add(e, 5001, t); e->masc = t; }
            }
            if (y >= 108 && y < 119) ed_toggle_actuator(e, 4, 0);
            if (y >= 119 && y < 130) ed_toggle_actuator(e, 4, 1);
            if (y >= 130 && y < 141) ed_toggle_actuator(e, 5, 0);
            if (y >= 141 && y < 152) ed_toggle_actuator(e, 5, 1);
        } else if (pg == 10) {
            int g = (y - 86) / 11, seen = 0, j;
            for (j = 0; j < e->npool; j++) {
                int first = 1, q;
                for (q = 0; q < j; q++) if (e->pool[q] == e->pool[j]) first = 0;
                if (!first) continue;
                if (seen == g) { e->sel_pool = j; break; }
                seen++;
            }
            if (y >= 86 && e->sel_pool >= 0 && e->sel_pool < e->npool) {
                int r2 = ed_assign(e, e->crit_loc, 0);
                if (r2 == -1) shell_message(g_grd, g_gtx, g_headless, "Insufficient criticals|for item placement.#Ok");
                if (r2 == -2) shell_message(g_grd, g_gtx, g_headless, "Jump Jets may only|be assigned to torso|or leg sections.#Ok");
                e->sel_pool = -1;
            }
        }
    }
    if (x >= 218 && x <= 422) {   /* the middle panel */
        if (pg == 6) {
            if (y >= 86 && y < 174) e->armor_loc = (y - 86) / 11;
            { int l = gl_diag_hit(x, y); if (l >= 0) { gsnd_play(0x52, 0x28, 0); for (k = 0; k < 8; k++) if (ARMOR_ORDER[k] == l) e->armor_loc = k; } }   /* sound INFERRED: base 0x50 + 2 */
        } else if (pg == 7 || pg == 8) {
            int nw = 0, list[35], n = gl_wtable(list);
            for (k = 0; k < e->d.item_count; k++) if (e->d.items[k].id < 5000) { if (nw < GL_MAXW && y >= 86 + 11 * nw && y < 97 + 11 * nw) e->sel_fit = k; nw++; }
            if (y >= 251 && y < 262 && x < 322 && e->sel_table >= 0 && e->sel_table < n) gl_add_weapon(e, list[e->sel_table]);   /* ADD WEAPON */
            if (y >= 251 && y < 262 && x >= 322 && e->sel_fit >= 0 && e->sel_fit < e->d.item_count) gl_add_ammo(e, e->sel_fit);   /* ADD AMMO */
            if (y >= 262 && y < 273 && x < 322 && e->sel_fit >= 0 && e->sel_fit < e->d.item_count) { ed_del_weapon(e, e->sel_fit); e->sel_fit = -1; }   /* DELETE WEAPON */
            if (y >= 262 && y < 273 && x >= 322 && e->sel_fit >= 0 && e->sel_fit < e->d.item_count) ed_del_ammo(e, e->d.items[e->sel_fit].id);          /* DELETE AMMO */
        } else if (pg == 10) {
            mek_loc *L = &e->d.loc[e->crit_loc];
            int sl = (y - 79) / 11;
            if (y >= 79 && sl < L->slot_count) {
                uint16_t at = L->slots[sl];
                if (at >= 5300 && at < 6000) shell_message(g_grd, g_gtx, g_headless, "Selected critical|can not be removed.#Ok");
                else if (at) ed_unassign(e, e->crit_loc, sl);
            }
            { int l = gl_diag_hit(x, y); if (l >= 0) { gsnd_play(0x52, 0x28, 0); e->crit_loc = l; } }
        }
    }
}
static void gl_cust_draw(int mx, int my)
{
    medit *e = &gl_E;
    mek_mass mm;
    char t[96];
    int pg = e->page, k;
    gl_ed_masses(e, &mm);
    if (pg == 6 || pg == 10) gl_diag_draw(pg == 6 ? ARMOR_ORDER[e->armor_loc] : e->crit_loc);
    gl_header("CUSTOMIZING", gl_title(gl_m));
    gl_g(20, 64, "Variant:");
    snprintf(t, sizeof t, "User Variant #%d", gl_cust_slot - 99); gl_w(65, 64, t);
    gl_component_rows(&mm, ed_is_xl(e), ed_endo(e), ed_ferro(e), (int)e->d.tonnage, 1);
    gl_w(20, 262, "Assign Criticals");
    if (pg == 0) {
        /* running MP = (walk x 3 + 2) / 2 (FUN_00032f50: 0xabed4 = (0xabed0 x 3 + 2) / 2; D2_cust.png Kodiak 4 -> 75.6 kph) */
        double walk = e->d.walk_mp * 10.8, run = (double)(((int)e->d.walk_mp * 3 + 2) / 2) * 10.8;
        gl_g(444, 64, "ENGINE");
        gl_g(444, 86, "Rating"); snprintf(t, sizeof t, "%d%s", ed_rating(e), ed_is_xl(e) ? "XL" : ""); gl_g(544, 86, t);
        gl_g(444, 97, "Type"); gl_w(544, 97, ed_is_xl(e) ? "XL" : "Std");
        gl_g(444, 108, "Manufacturer"); gl_g(544, 108, engine_maker(ed_rating(e)));
        gl_g(444, 119, "Mass"); snprintf(t, sizeof t, "%.2f T", mm.engine); gl_g(544, 119, t);
        gl_g(444, 130, "Walking Speed"); snprintf(t, sizeof t, "%.1f kph", walk); gl_g(544, 130, t);
        gl_g(444, 141, "Running Speed"); snprintf(t, sizeof t, "%.1f kph", run); gl_g(544, 141, t);
        gl_w(444, 163, "FASTER"); gl_w(444, 174, "SLOWER");
    } else if (pg == 3) {
        gl_g(444, 64, "HEAT SINKS");
        gl_g(444, 86, "Count"); gl_sinks_text(t, sizeof t, &mm); gl_g(544, 86, t);
        gl_g(444, 97, "Type"); gl_w(544, 97, e->dbl ? "Double" : "Single");
        gl_g(444, 108, "Mass"); snprintf(t, sizeof t, "%.2f T", mm.sinks); gl_g(544, 108, t);
        gl_w(444, 130, "ADD"); gl_w(444, 141, "DELETE");
    } else if (pg == 4) {
        gl_g(444, 64, "JUMP JETS");
        gl_g(444, 86, "Count"); snprintf(t, sizeof t, "%d", mm.njets); gl_g(544, 86, t);
        gl_g(444, 97, "Mass"); snprintf(t, sizeof t, "%.2f T", mm.jets); gl_g(544, 97, t);
        gl_w(444, 119, "ADD"); gl_w(444, 130, "DELETE");
    } else if (pg == 5) {
        gl_g(444, 64, "INTERNAL STRUCTURE");
        gl_g(444, 86, "Type"); gl_w(544, 86, ed_endo(e) ? "Endo-S" : "Std");
        gl_g(444, 97, "Mass"); snprintf(t, sizeof t, "%.2f T", mm.internal); gl_g(544, 97, t);
        for (k = 0; k < 8; k++) { gl_g(444, 119 + 11 * k, ARMOR_NAME[k]); snprintf(t, sizeof t, "%u", e->d.loc[INTERNAL_ORDER[k]].internal); gl_g(544, 119 + 11 * k, t); }
    } else if (pg == 6) {
        int loc = ARMOR_ORDER[e->armor_loc], torso = loc == MEK_RT || loc == MEK_LT || loc == MEK_CT;
        gl_g(224, 64, "ARMOR ALLOCATION");
        for (k = 0; k < 8; k++) {
            const mek_loc *L = &e->d.loc[ARMOR_ORDER[k]];
            int tor = ARMOR_ORDER[k] == MEK_RT || ARMOR_ORDER[k] == MEK_LT || ARMOR_ORDER[k] == MEK_CT;
            snprintf(t, sizeof t, "%s (%d)", ARMOR_NAME[k], ed_loc_max(e, ARMOR_ORDER[k])); gl_g(224, 86 + 11 * k, t);
            if (tor) snprintf(t, sizeof t, "%u/%u", L->armor, L->rear_armor); else snprintf(t, sizeof t, "%u", L->armor);
            gl_g(344 - text_width(&g_fthin, t) / 2, 86 + 11 * k, t);
        }
        gl_g(444, 64, "ARMOR");
        gl_g(444, 86, "Factor"); snprintf(t, sizeof t, "%d", ed_factor(e)); gl_g(544, 86, t);
        gl_g(444, 97, "Allocated"); snprintf(t, sizeof t, "%d", ed_armor_alloc(e)); gl_g(544, 97, t);
        gl_g(444, 108, "Mass"); snprintf(t, sizeof t, "%.2f T", mm.armor); gl_g(544, 108, t);
        gl_g(444, 119, "Type"); gl_w(544, 119, ed_ferro(e) ? "Ferro-F" : "Std");
        gl_w(444, 141, "ADD"); gl_w(444, 152, "DELETE");
        gl_w(551, 168, "\x01"); gl_w(581, 168, "\x01");   /* the font's arrows: 1 up, 2 down */
        snprintf(t, sizeof t, "%s (%d)", ARMOR_NAME[e->armor_loc], ed_loc_max(e, loc)); gl_w(444, 185, t);
        snprintf(t, sizeof t, "%u", e->d.loc[loc].armor); gl_g(551, 185, t);
        if (torso) { snprintf(t, sizeof t, "%u", e->d.loc[loc].rear_armor); gl_g(580, 185, t); } else gl_g(580, 185, "--");
        gl_w(551, 202, "\x02"); gl_w(581, 202, "\x02");
    } else if (pg == 7 || pg == 8) {
        const mek_weapon *wi = NULL;
        int list[35], n = gl_wtable(list);
        gl_g(224, 64, "WEAPONS AND AMMO");
        gl_weapon_lines(&e->d, 224, 86, e->sel_fit);
        gl_w(224, 251, "ADD WEAPON"); gl_w(324, 251, "ADD AMMO"); gl_w(224, 262, "DELETE WEAPON"); gl_w(324, 262, "DELETE AMMO");
        gl_g(224, 284, "WEAPON INFO");
        if (e->sel_table >= 0 && e->sel_table < n) wi = gl_wp(list[e->sel_table]);
        else if (e->sel_fit >= 0 && e->sel_fit < e->d.item_count) wi = gl_wp((int)(e->d.items[e->sel_fit].id / 100));
        gl_g(224, 306, "Type"); if (wi) gl_g(274, 306, wi->name);
        gl_g(224, 328, "Heat"); gl_g(324, 328, "Mass"); gl_g(224, 339, "Damage"); gl_g(324, 339, "Crit"); gl_g(224, 350, "Range"); gl_g(324, 350, "Ammo");
        if (wi) {
            snprintf(t, sizeof t, "%d", wi->heat); gl_g(274, 328, t);
            snprintf(t, sizeof t, "%.2f T", wi->weight_x100 / 100.0); gl_g(374, 328, t);
            if (wi->damage < 0) snprintf(t, sizeof t, "%d / msl", -wi->damage); else snprintf(t, sizeof t, "%d", wi->damage); gl_g(274, 339, t);
            snprintf(t, sizeof t, "%d", wi->crits); gl_g(374, 339, t);
            snprintf(t, sizeof t, "%d m", wi->max_range_m); gl_g(274, 350, t);
            if (wi->ammo_per_ton) snprintf(t, sizeof t, "%d / T", wi->ammo_per_ton); else snprintf(t, sizeof t, "-"); gl_g(374, 350, t);
        }
        gl_g(444, 64, "WEAPONS TABLE");
        for (k = 0; k < n; k++) { if (k == e->sel_table) gl_w(444, 86 + 11 * k, gl_wp(list[k])->name); else gl_g(444, 86 + 11 * k, gl_wp(list[k])->name); }
    } else if (pg == 9) {
        static const char *EQ[5] = {"MASC", "Right Lower Arm Actuator", "Right Hand Actuator", "Left Lower Arm Actuator", "Left Hand Actuator"};
        int on[5];
        on[0] = e->masc > 0; on[1] = ed_has_actuator(e, 4, 0); on[2] = ed_has_actuator(e, 4, 1); on[3] = ed_has_actuator(e, 5, 0); on[4] = ed_has_actuator(e, 5, 1);
        gl_g(444, 64, "EQUIPMENT");
        gl_g(444, 86, "Yes"); gl_g(474, 86, "CASE");
        for (k = 0; k < 5; k++) { gl_w(444, 97 + 11 * k, on[k] ? "Yes" : "No"); gl_g(474, 97 + 11 * k, EQ[k]); }
    } else if (pg == 10) {
        const mek_loc *L = &e->d.loc[e->crit_loc];
        char nb[64];
        int g = 0, j;
        gl_w(224, 64, LOC_NAME[e->crit_loc]);
        for (k = 0; k < L->slot_count; k++) gl_g(224, 79 + 11 * k, L->slots[k] ? gl_item_name(L->slots[k], nb, sizeof nb) : "-");
        gl_g(444, 64, "UNASSIGNED CRITICALS");
        for (j = 0; j < e->npool; j++) {
            int first = 1, q, cnt = 0;
            for (q = 0; q < j; q++) if (e->pool[q] == e->pool[j]) first = 0;
            if (!first) continue;
            for (q = 0; q < e->npool; q++) if (e->pool[q] == e->pool[j]) cnt++;
            snprintf(t, sizeof t, "%s (%d)", gl_item_name(e->pool[j], nb, sizeof nb), cnt);
            if (mx >= 440 && mx <= 626 && my >= 86 + 11 * g && my < 97 + 11 * g) gl_w(444, 86 + 11 * g, t); else gl_g(444, 86 + 11 * g, t);
            g++;
        }
    }
}
/* the panels of the page: 1 left always; 14 short right, or 13 + 11 (weapons), 10 + 11 (criticals), 10 + 14 (armour) */
static void gl_panels(int cust, int pg)
{
    int mid4 = cust && (pg == 6 || pg == 10), mid5 = cust && (pg == 7 || pg == 8), tall = cust && (pg == 7 || pg == 8 || pg == 10);
    gl_show(10, mid4); gl_show(13, mid5); gl_show(11, tall); gl_show(14, !tall); gl_show(12, 0);
}
/* SAVE (case 7, 0x387xx..0x38d40). Returns 1 when saved */
static int gl_cust_save(void)
{
    medit *e = &gl_E;
    mek_mass mm;
    static uint8_t buf[8192];
    char dir[1100], path[1200], nm[16];
    size_t n;
    FILE *f;
    int s;
    gl_ed_masses(e, &mm);
    if (e->npool > 0) { shell_message(g_grd, g_gtx, g_headless, "Invalid 'Mech specification:|Unassigned criticals detected.#Ok"); return 0; }
    if (mm.used > (double)e->d.tonnage + 1e-6) { shell_message(g_grd, g_gtx, g_headless, "Invalid 'Mech specification:|Chassis can not support|current mass.#Ok"); return 0; }
    for (s = 100; s < 200 && gl_var[s][0]; s++) {}
    if (s >= 200) { shell_message(g_grd, g_gtx, g_headless, "Error: Too many mechs|of this variant to save.#Ok"); return 0; }
    snprintf(nm, sizeof nm, "%s%02dusr", GBL_MECHS[gl_m].code, s - 100);   /* "%s%02dusr.mek": the first design is 00 */
    memset(e->d.config_raw, 0, sizeof e->d.config_raw);
    snprintf(e->d.config_name, sizeof e->d.config_name, "User Variant #%d", s - 99);
    n = mek_write(&e->d, buf, sizeof buf);
    if (gl_mekdir(dir, sizeof dir) != 0) { snprintf(dir, sizeof dir, "%s/mek", g_dir); mkdir(dir, 0755); }   /* fopen fails -> mkdir("mek") */
    snprintf(path, sizeof path, "%s/%s.mek", dir, nm);
    if (!n || !(f = fopen(path, "wb"))) { shell_message(g_grd, g_gtx, g_headless, "Error saving 'Mech.#Ok"); return 1; }
    if (fwrite(buf, 1, n, f) != n) { fclose(f); shell_message(g_grd, g_gtx, g_headless, "Error saving 'Mech.#Ok"); return 1; }
    fclose(f);
    snprintf(gl_var[s], 13, "%.12s", nm);
    gl_vs = s;
    return 1;
}
static int gbl_mech_lab(void)
{
    /* hotspots 0x7f354 (10, every context) */
    static const gbl_hot HOT[10] = {
        {24, 434, 112, 458, 0, 0, NULL},    /* 0 EXIT LAB */
        {287, 425, 307, 457, 0, 0, NULL},   /* 1 NEXT CHASSIS */
        {171, 425, 189, 457, 0, 0, NULL},   /* 2 PREV CHASSIS */
        {449, 425, 469, 457, 0, 0, NULL},   /* 3 NEXT VARIANT */
        {333, 425, 351, 457, 0, 0, NULL},   /* 4 PREV VARIANT */
        {24, 401, 112, 425, 0, 0, NULL},    /* 5 CUSTOMIZE */
        {24, 368, 112, 392, 0, 0, NULL},    /* 6 ACCEPT MECH */
        {24, 418, 112, 442, 0, 0, NULL},    /* 7 SAVE */
        {24, 385, 112, 409, 0, 0, NULL},    /* 8 ABORT */
        {493, 434, 581, 458, 0, 0, NULL}};  /* 9 DELETE */
    int ctx = g_ctx, hall = pil() ? pil()->hall : 0, lock, cust = 0, ret = -1, en[10], k, bg = ctx == 2 ? 10 : 16;
    const int caller = ctx == 2 ? 7 : 5;   /* EBX: the screen that opened the lab (ctx 2 only from the Combat Simulator) */
    gbl_star *st = star_rec(g_gstar_cur);
    lock = ctx == 0 && hall != 0;   /* the chassis is fixed in the campaign once the pilot left the first location */
    gl_cnt = ctx == 2 ? gl_ia_count() : 24;
    gl_vs = 0;
    gl_m = gstar_mech(-1);
    if (gl_m < 0) gl_m = 0;
    else {   /* the variant slot from the loadout: digits [3][4], + 100 unless [5..7] is "std" */
        const char *lo = st->slot[st->cur >= 0 && st->cur < 3 ? st->cur : 0].loadout;
        if (strlen(lo) >= 8 && isdigit((unsigned char)lo[3]) && isdigit((unsigned char)lo[4])) gl_vs = (lo[3] - '0') * 10 + (lo[4] - '0') + (strncasecmp(lo + 5, "std", 3) ? 100 : 0);
    }
    if (gl_m >= gl_cnt) gl_m = 0;
    gbl_bg(bg);   /* slot 16 (ctx 0/1) / 10 (ctx 2): the same all-black PCX */
    gs_load(1, "wgbmp1", 13, 55, 0x46, 0);
    gs_load(10, "wgbmp4ar", 216, 55, 0x64, 0);
    gs_load(11, "wgbmp7cr", 436, 55, 0x64, 0);
    gs_load(12, "wgbmp4mp", 75, 171, 0x21, 0);
    gs_load(13, "wgbmp5wa", 216, 55, 0x64, 0);
    gs_load(14, "wgbmp1es", 436, 55, 0x46, 0);
    gs_load(4, "wgbcsl", 171, 425, 1, 0);
    gs_load(0, "wgbvrl", 333, 425, 1, 0);
    gs_load(0x11, "wgbacl", 24, 368, 0x44, 0);
    gs_load(0x12, "wgbcul", 24, 401, 0x44, 0);
    gs_load(0x13, "wgbexl", 24, 434, 0x44, 0);
    gs_load(0x14, "wgbacd", 24, 368, 0x64, 0);
    gs_load(0x15, "wgbcud", 24, 401, 0x64, 0);
    gs_load(0x16, "wgbexd", 24, 434, 0x64, 0);
    gs_load(0x17, "wgbdll", 493, 434, 0x64, 0);
    gs_load(0x18, "wgbabl", 24, 385, 0x64, 0);
    gs_load(0x19, "wgbsvl", 24, 418, 0x64, 0);
    gs_load(0x1a, "wgbdld", 493, 434, 0x64, 0);
    gs_load(0x1b, "wgbabd", 24, 385, 0x64, 0);
    gs_load(0x1c, "wgbsvd", 24, 418, 0x64, 0);
    gl_chassis();
    if (!gl_var[gl_vs][0]) {   /* a loadout the list does not offer (e.g. a PRJ-only *USR record): shown as is (ASSUMED) */
        const char *lo = st->slot[st->cur >= 0 && st->cur < 3 ? st->cur : 0].loadout;
        snprintf(gl_var[gl_vs], 13, "%.12s", lo[0] ? lo : gl_var[0]);
        gl_show_variant();
    }
    gsnd_play(0x4d, 0x11, 1);   /* lab ambience */
    gl_voice = 0;
    gl_voice_play();
    while (ret == -1) {
        gbl_input *in;
        int h;
        char t[96];
        for (k = 0; k < 10; k++) en[k] = cust ? (k == 7 || k == 8) : (k != 7 && k != 8 && (k != 9 || gl_vs >= 100) && !(lock && (k == 1 || k == 2)));
        h = gh_hit(HOT, 10, en, g_in.mx, g_in.my);
        /* the per-frame clip states (0x37d.. switch on the hotspot under the pointer) */
        switch (h) {
        case 0: gl_show(0x11, 1); gl_show(0x12, 1); gl_show(0x13, 0); gl_show(0x14, 0); gl_show(0x15, 0); gl_show(0x16, 1); break;
        case 1: case 2: gs_show_frame(4, 1); break;
        case 3: case 4: gs_show_frame(0, 1); break;
        case 5: gl_show(0x11, 1); gl_show(0x12, 0); gl_show(0x13, 1); gl_show(0x14, 0); gl_show(0x15, 1); gl_show(0x16, 0); break;
        case 6: gl_show(0x11, 0); gl_show(0x12, 1); gl_show(0x13, 1); gl_show(0x14, 1); gl_show(0x15, 0); gl_show(0x16, 0); break;
        case 7: gl_show(0x1c, 1); gl_show(0x19, 0); gl_show(0x1b, 0); gl_show(0x18, 1); break;
        case 8: gl_show(0x1b, 1); gl_show(0x18, 0); gl_show(0x1c, 0); gl_show(0x19, 1); break;
        case 9: gl_show(0x1a, 1); gl_show(0x17, 0); break;
        default:
            if (cust) { gl_show(0x18, 1); gl_show(0x19, 1); gl_show(0x1b, 0); gl_show(0x1c, 0); }
            else {
                gl_show(0x11, 1); gl_show(0x12, 1); gl_show(0x13, 1);
                if (gl_vs >= 100) gl_show(0x17, 1);
                gl_show(0x14, 0); gl_show(0x15, 0); gl_show(0x16, 0); gl_show(0x1a, 0);
                gs_show_frame(0, 0); gs_show_frame(4, 0);
            }
        }
        gl_panels(cust, gl_E.page);
        gbl_bg(bg);
        draw_bg();
        gs_update();
        if (cust) gl_cust_draw(g_in.mx, g_in.my);
        else {
            mek_mass mm;
            snprintf(t, sizeof t, "CALLSIGN: %s (%d.00 T MAX)", st->slot[st->cur >= 0 && st->cur < 3 ? st->cur : 0].name1, (int)st->tons);   /* 0xabbd8 */
            gl_header(t, gl_title(gl_m));
            if (gl_have) {
                gl_masses(&gl_d, &mm);
                gl_g(20, 64, "Variant:");
                gl_g(65, 64, gl_d.config_name[0] ? gl_d.config_name : gl_var[gl_vs]);
                gl_component_rows(&mm, mm.xl, mm.endo, mm.ferro, (int)gl_d.tonnage, 0);
            }
            gl_g(444, 64, "WEAPONS AND AMMO");
            gl_weapon_lines(gl_have ? &gl_d : NULL, 444, 86, -1);
        }
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) { ret = -3; break; }
        h = gh_hit(HOT, 10, en, in->mx, in->my);
        if (in->lclick && h >= 0) {
            switch (h) {
            case 0: ret = caller; break;   /* EXIT LAB: 7 in context 2, else 5 */
            case 1: case 2:
                gl_m = h == 1 ? (gl_m + 1 >= gl_cnt ? 0 : gl_m + 1) : (gl_m - 1 < 0 ? gl_cnt - 1 : gl_m - 1);
                gl_vs = 0;
                gl_chassis();
                gl_voice_play();
                break;
            case 3: gsnd_play(0x76, 0x32, 0); gl_next_variant(); break;
            case 4: gsnd_play(0x76, 0x32, 0); gl_prev_variant(); break;
            case 5: {   /* CUSTOMIZE: the next free user slot names the design */
                int s;
                for (s = 100; s < 200 && gl_var[s][0]; s++) {}
                if (s >= 200) { shell_message(g_grd, g_gtx, g_headless, "Error: Too many mechs|of this variant to save.#Ok"); break; }
                if (!gl_have) break;
                gl_cust_slot = s;
                ed_init(&gl_E, &gl_d);
                { mek_mass mm; gl_masses(&gl_E.d, &mm); gl_E.dbl = mm.dbl; }   /* ed_init's double-sink guess with the new weapons' mass */
                gl_E.page = 0;
                cust = 1;
                gl_show(0, 0); gl_show(4, 0); gl_show(0x10, 0);
                for (k = 0x11; k <= 0x17; k++) gl_show(k, 0);
                gl_show(0x1a, 0); gl_show(0x18, 1); gl_show(0x19, 1);
                break;
            }
            case 6:   /* ACCEPT MECH: FUN_0003e0b0(-1, variant) refuses a chassis over the star's tonnage */
                if (!gstar_slot(-1, gl_var[gl_vs], NULL, NULL)) shell_message(g_grd, g_gtx, g_headless, "'Mech exceeds|Keshik Defined Maximum Tonnage (KDMT)|for mission.#Ok");
                else ret = caller;
                break;
            case 7: case 8:
                if (h == 7 && !gl_cust_save()) break;
                cust = 0;   /* back to the main page: FUN_00036720 (list, turntable), the main widgets */
                gl_chassis();
                gl_show(0x10, 1); gl_show(0, 1); gl_show(4, 1);
                for (k = 0x18; k <= 0x1c; k++) gl_show(k, 0);
                gl_show(0x11, 1); gl_show(0x12, 1); gl_show(0x13, 1);
                break;
            case 9:
                if (shell_dialog(g_grd, g_gtx, g_headless, "Delete this 'Mech?|Are you sure?#Yes|No", 1) == 0) {
                    char dn[64], path[1100];
                    snprintf(dn, sizeof dn, "MEK\\%s.MEK", gl_var[gl_vs]);
                    if (dp_resolve(dn, path, sizeof path) == 0) remove(path);
                    gl_var[gl_vs][0] = 0;
                    gl_next_variant();
                }
                break;
            }
        } else if (in->lclick && cust) gl_cust_click(in->mx, in->my);
        if (ret == -1) {
            int r = gbl_global_menu();   /* FUN_00026f10 every pass (local_64 == -1) */
            if (r != -1) ret = r;
        }
    }
    if (gl_voice > 0) gsnd_stop(gl_voice);
    if (ret != 7) gia_from = 1;   /* not back to the Combat Simulator: its next entry is from the hall */
    return ret;
}
