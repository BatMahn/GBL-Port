/* gbl_campaign.c - the Ghost Bear campaign: mission tables, the Mission Computer (screen 5, FUN_00028410), the mission
 * clip lists (state 10, 0x3ab71), the debriefing (screen 3, FUN_00024750). Included by tools/gblshell.c (one TU).
 *
 * Spec: docs/gbl/C_campaign.md (+ A_frame.md 1.6-1.8). Addresses are GBL MW2SHELL.EXE VAs. Compared with the DOSBox
 * captures ref/C_07mission, C_10..C_15, C_34debrief, C_35/36, C_44, C_52..C_59, C_70..C_75, D2_mission.
 *
 * Contents:
 *   GBL_MISSIONS / GBL_MX                          the campaign tables (0x800ac, 0x7fb8c)
 *   gc_star_slot / gc_star_mech                     the original star API FUN_0003e0b0 / FUN_0003e2e0 (see the note)
 *   gc_brf2                                         FUN_0002c810: BRF2 SUPS / SDSC
 *   gc_text_* / gc_fill / gc_draw_page              the text box engine (FUN_0001c600, FUN_0003f4d0, FUN_0003eaa0)
 *   gc_pager                                        the full-screen text pager (FUN_0002b0e0 + FUN_0002a590)
 *   gbl_mission_enter / gbl_mission_computer        state 5
 *   gbl_launch_clips                                state 10's list A
 *   gbl_debriefing                                  state 3 (scoring FUN_00024750 0x24750..0x25280)
 */

/* ======================================================================================================================
 * campaign tables (C_campaign.md 2.3) */
typedef struct { const char *scn, *title; unsigned char trial; } gbl_mission;     /* 0x800ac, 9-byte records */
static const gbl_mission GBL_MISSIONS[23] = {
  {"bassSCN1","Trial on Alshain",1}, {"jessSCN1","Defend on Mannedorf",0}, {"claiSCN1","Patrol on Mannedorf",0},
  {"newcSCN1","Trial on Alshain",0}, {"jeehSCN1","Defend on Chandler",0}, {"tim_SCN1","Strike on Chandler",0},
  {"andySCN1","Strike on Wolcott",0}, {"fostSCN1","Circle of Equals",0}, {"hoffSCN1","Raid on Jeanette",0},
  {"zackSCN1","Defend on Jumpship",0}, {"seanSCN1","Trial of Possession",0}, {"franSCN1","Defend on Alshain",0},
  {"asahSCN1","Trial on Alshain",1}, {"peteSCN1","Underwater Strike",0}, {"cathSCN1","Strike on Moritz",0},
  {"sachSCN1","Strike on Moritz 2",0}, {"branSCN1","Destroy Orbital Platform",0}, {"gui1SCN1","Bloodright Trial 1",0},
  {"gui2SCN1","Bloodright Trial 2",0}, {"gui3SCN1","Bloodright Trial 3",0}, {"gui4SCN1","Bloodright Trial 4",0},
  {"guinSCN1","Final Bloodright Trial",0}, {NULL,"Retired",0}};
typedef struct {                                   /* 0x7fb8c, stride 0x39 */
  const char *brf_video, *dbfs_video;              /* +0 / +8 */
  int brief_snd, debrief_snd, always_pass, location;   /* +0xc DBSOUND, +0x10 DBSOUND (-1 none), +0x14, +0x1d */
  const char *pre[6], *win[6], *exitwin[6];        /* +0x21 / +0x29 / +0x31 clip lists A / B / C, NULL-terminated */
} gbl_mission_x;
static const gbl_mission_x GBL_MX[23] = {
  {"bassbrf1","bassdbfs", 1,23,0,2, {0},{0},{"to","attach","warp","sep"}},
  {"jessbrf1","jessdbfs", 2,24,0,2, {0},{0},{0}},
  {"claibrf1","claidbfs", 3,25,0,0, {0},{"mraid"},{"attach","warp","sep","land"}},
  {"newcbrf1","newcdbfs", 4,26,0,1, {0},{0},{"to","attach","warp"}},
  {"jeehbrf1","jeehdbfs", 5,-1,0,1, {"sep"},{"attach"},{0}},
  {"tim_brf1","tim_dbfs", 6,28,0,0, {"sep"},{"attach"},{"warp","sep","land"}},
  {"andybrf1","andydbfs", 7,29,0,1, {"to","attach","warp","sep"},{"attach"},{0}},
  {"fostbrf1","fostdbfs", 8,30,0,1, {"sep"},{"attach"},{0}},
  {"hoffbrf1","hoffdbfs", 9,-1,0,1, {"sep"},{"attach"},{0}},
  {"zackbrf1","zackdbfs",10,32,0,1, {0},{0},{0}},
  {"seanbrf1","seandbfs",11,33,0,1, {"sep"},{"attach"},{"warp"}},
  {"franbrf1","frandbfs",12,34,0,0, {"sep"},{0},{0}},
  {"asahbrf1","asahdbfs",13,35,1,1, {0},{0},{"to","attach","warp"}},
  {"petebrf1","petedbfs",14,36,0,1, {"sep"},{"attach"},{0}},
  {"cathbrf1","cathdbfs",15,37,0,1, {"sep"},{0},{0}},
  {"sashbrf1","sashdbfs",16,38,0,1, {0},{"attach"},{0}},
  {"branbrf1","brandbfs",17,39,0,0, {"sep"},{"mend","attach","warp","sep","land"},{0}},
  {"gui1brf1","gui1dbfs",18,40,0,0,{0},{0},{0}}, {"gui2brf1","gui2dbfs",19,41,0,0,{0},{0},{0}},
  {"gui3brf1","gui3dbfs",20,42,0,0,{0},{0},{0}}, {"gui4brf1","gui4dbfs",21,43,0,0,{0},{0},{0}},
  {"guinbrf1","guindbfs",22,44,0,0,{0},{0},{0}}, {"","",-1,-1,0,0,{0},{0},{0}}};
static const char *const GC_RANKS[7] = {"Cadet", "Mechwarrior", "Star Commander", "Star Captain", "Star Colonel",
                                        "Nova Commander", "Galaxy Commander"};   /* 0x7e790 */
static int gc_mission(void) { gbl_pilot *p = pil(); int m = p ? p->done : 0; return m < 0 ? 0 : m > 22 ? 22 : m; }

/* ======================================================================================================================
 * the star API as the original has it. FUN_0003e0b0(slot, code, name1, name2) differs from the core's gstar_slot:
 * an unknown / empty code removes the last slot only when slot != 0 and slot + 1 == size (returns -1); otherwise it
 * returns 1 when slot >= size, else 0 (refused, nothing changed) - so slot 0 never becomes empty (C_72: jess's empty
 * SDSC loadout keeps the Grizzly); a chassis over the tonnage limit is refused (0); the loadout is the first 8
 * characters, a 3-letter code gets "00std"; then (every call, names only too) slot == size && size < max grows the star.
 * FUN_0003e2e0(slot) = the 'Mech index only while slot < max and slot < size. (Core patch suggested in the report.) */
static int gc_star_slot(int slot, const char *code, const char *name1, const char *name2)
{
    gbl_star *st = star_rec(g_gstar_cur);
    if (slot < 0) slot = st->cur; else st->cur = slot;
    if (slot < 0 || slot > 2) return 0;
    if (name1) snprintf(st->slot[slot].name1, sizeof st->slot[slot].name1, "%s", name1);
    if (name2) snprintf(st->slot[slot].name2, sizeof st->slot[slot].name2, "%s", name2);
    if (code) {
        int m;
        for (m = 0; m < 32; m++) if (strlen(code) >= 3 && !strncmp(code, GBL_MECHS[m].code, 3)) break;   /* strncmp 0x4c888 */
        if (m == 32) {
            if (slot != 0 && slot + 1 == st->size) { st->size--; return -1; }
            return st->size <= slot ? 1 : 0;
        }
        if (st->tons < GBL_MECHS[m].tons) return 0;
        snprintf(st->slot[slot].loadout, sizeof st->slot[slot].loadout, "%.8s", code);
        if (strlen(code) == 3) strcat(st->slot[slot].loadout, "00std");
        st->slot[slot].mech = m;
    }
    if (slot == st->size && st->size < st->max) st->size++;
    return -1;
}
static int gc_star_mech(int slot)
{
    gbl_star *st = star_rec(g_gstar_cur);
    if (slot < 0) slot = st->cur;
    if (slot < 0 || slot > 2 || slot >= st->max || slot >= st->size) return -1;
    return st->slot[slot].mech;
}

/* ======================================================================================================================
 * mission records (MW2.PRJ BWD "<first 4 letters, '_' padded><suffix>", FUN_0002c810 / 0x28472 / FUN_00024590) */
static void gc_recname(char *out, size_t n, const char *scn, const char *suffix)
{
    char b[8];
    int k = 0;
    while (scn && k < 4 && scn[k] && scn[k] != '.') { b[k] = scn[k]; k++; }
    while (k < 4) b[k++] = '_';
    b[4] = 0;
    snprintf(out, n, "%s%s", b, suffix);
}
/* the ORDR text of a scenario's record (FUN_0001cba0: chunk size - 9 bytes, the last byte dropped) with FUN_0001c600's
 * markup filled in (q = the caller's \\q text): gbl_bwd_text (gbl_pilots.c); malloc'd, NULL if none */
static char *gc_ordr(const char *scn, const char *suffix, const char *q)
{
    char name[16];
    gc_recname(name, sizeof name, scn, suffix);
    return gbl_bwd_text(name, q);
}
/* FUN_0002c810(scenario, star): BRF2 SUPS -> the "-b=" string; with star: the first SDSC fills the friendly star
 * (formation 0, max +0x14, size +0x10, tonnage +0xc, loadouts +0x18 + 16k for k < max), a second SDSC would fill the enemy
 * star and the enemy skill base (no GBL mission has one); then star 0 and slot 0 are selected */
static void gc_brf2(const char *scn, int star)
{
    char name[16];
    prj_record r;
    gc_recname(name, sizeof name, scn, "");
    if (mission_record(name, "BRF2", &r) != 0) return;
    {
        uint32_t len;
        const uint8_t *p = chunk_find(&r, "SUPS", &len);
        if (p && len > 0) snprintf(g_camo, sizeof g_camo, "%.*s", (int)(len < 15 ? len : 15), (const char *)p);
    }
    if (star) {
        size_t o = 12;
        int nsd = 0;
        while (o + 8 <= r.size && nsd < 2) {
            uint32_t sz = r.data[o + 4] | r.data[o + 5] << 8 | r.data[o + 6] << 16 | (uint32_t)r.data[o + 7] << 24;
            if (sz < 8 || o + sz > r.size) break;
            if (!memcmp(r.data + o, "SDSC", 4) && sz >= 24) {
                const uint8_t *c = r.data + o + 8;
                int32_t f0, tons, size, max, k;
                memcpy(&f0, c, 4); memcpy(&tons, c + 4, 4); memcpy(&size, c + 8, 4); memcpy(&max, c + 12, 4);
                gstar_set(nsd, 0, max, size, tons);
                for (k = 0; k < max && k < 3 && 16 + (k + 1) * 16 <= (int)sz - 8; k++) {
                    char lo[17];
                    snprintf(lo, sizeof lo, "%.16s", (const char *)c + 16 + 16 * k);
                    gc_star_slot(k, lo, NULL, NULL);
                }
                gstar_set(nsd, 0, max, size, tons);
                if (nsd == 1) g_skill_base = f0;
                nsd++;
            }
            o += sz;
        }
    }
    gstar_set(0, -1, -1, -1, -1);
    gc_star_slot(0, NULL, NULL, NULL);
    prj_record_free(&r);
}

/* ======================================================================================================================
 * text boxes and the full-screen pager: the shell's one implementation in gbl_pilots.c (gbl_text_pages = FUN_0001c600 +
 * FUN_0003f4d0, gbl_text_draw = FUN_0003eaa0, gbl_pager_from = FUN_0002b0e0 + FUN_0002a590), font idx 0x22 (GF_DEFAULT) */
/* ======================================================================================================================
 * small helpers */
static int gc_playing(int i) { return gs_loaded(i) && !g_gs[i].ended && !(g_gs[i].flags & GS_PAUSED); }   /* FUN_0003cd80 != 0 */
static void gc_hide(int i, int hide) { gs_flags(i, GS_HIDDEN, hide); }
static int gc_menu_key(void) { return g_in.key == SDLK_ESCAPE || g_in.rclick; }
/* a clip list (state 10 list A, debriefing lists B / C): full-screen clips in order; a skipped clip ends the list
 * (0xadee0); with TRANSITIONS off (MW2SND +0x3c == 0) the first clip that is not "mraid" / "mend" ends it */
static void gc_clips(const char *const *list)
{
    int k;
    for (k = 0; k < 6 && list[k] && !g_quit_all; k++) {
        if (!g_transitions && strcmp(list[k], "mraid") && strcmp(list[k], "mend")) break;
        if (gbl_video(list[k])) break;
    }
}
static int gc_alt_key(SDL_Keycode want)   /* Alt+W / Alt+L (getch 0 + scan 0x11 / 0x26) */
{
    if (g_in.key != want) return 0;
    if (g_headless) return getenv("GBL_ALT") != NULL;   /* TEST ONLY: headless events carry no modifiers */
    return (SDL_GetModState() & KMOD_ALT) != 0;
}

/* ======================================================================================================================
 * state 5's set-up in the state machine (FUN_00039ec0 0x3a9e1) */
static int g_gc_mc_prev = -1;   /* the screen state 5 was entered from (the state machine's prev; the core sets g_prev = 5) */
static void gbl_mission_enter(void)
{
    gbl_pilot *p = pil();
    g_gc_mc_prev = g_prev;
    if ((g_prev == 1 || g_prev == 3) && p) {
        int m = gc_mission(), k;
        gstar_set(0, 0, 3, 1, 100);
        /* the retry-alternative table 0xa93b8 is BSS and never written: the mission's own scenario */
        snprintf(g_scen, sizeof g_scen, "%s", GBL_MISSIONS[m].scn ? GBL_MISSIONS[m].scn : "");
        if (g_prev == 3) {   /* from the debriefing (REPLAY): the pilot's saved star */
            gstar_set(0, p->star.formation, p->star.max, p->star.size, p->star.tons);
            for (k = 0; k < p->star.size && k < 3; k++) gc_star_slot(k, p->star.slot[k].loadout, NULL, NULL);
        } else gc_brf2(g_scen, 1);
    }
    gstar_set(1, 0, 0, 0, 100);   /* the enemy star is empty in the campaign */
    gstar_set(0, -1, -1, -1, -1);
    gc_star_slot(0, NULL, NULL, NULL);
}

/* ======================================================================================================================
 * screen 5 - the Mission Computer (FUN_00028410(db, scenario, ctx, flag, prev)). Panels BRIEFING / INTELLIGENCE /
 * MECH PLANT are in background 14 (record 0x801c0). */
static const gbl_hot MC_HOT[13] = {   /* 0x7ec00 */
  {  2,  8, 90,124,   0,  0, NULL}, /* 0 DETAILS */
  {  2,119, 90,235,   0,  0, NULL}, /* 1 LOGOFF */
  {335,306,422,323, 345,313, NULL}, /* 2 slot 0 name -> Mech Lab */
  {335,345,422,362, 345,352, NULL}, /* 3 slot 1 */
  {335,382,422,399, 345,389, NULL}, /* 4 slot 2 */
  {552,  8,640,232,   0,  0, NULL}, /* 5 LAUNCH */
  {396,324,422,335,   0,  0, NULL}, /* 6 slot 0 next 'Mech */
  {396,363,422,374,   0,  0, NULL}, /* 7 slot 1 next */
  {396,400,422,411,   0,  0, NULL}, /* 8 slot 2 next */
  {335,324,361,332,   0,  0, NULL}, /* 9 slot 0 previous 'Mech */
  {335,363,361,371,   0,  0, NULL}, /* 10 slot 1 previous */
  {335,400,361,408,   0,  0, NULL}, /* 11 slot 2 previous */
  { 14,326, 34,394,   0,  0, NULL}, /* 12 VIEW */
};
#define GC_CLAN_MECHS 24   /* 0x7e83c: the arrows step through the first 24 chassis */
static void gc_mc_wireframe(void)   /* slot 11: "wgbmc%s" of slot 0's 'Mech at (423,284), flags 0x48, 20 fps */
{
    int m = gc_star_mech(0);
    char nm[16];
    gs_close(11);
    if (m < 0 || m >= 32) return;   /* the original would index the table with -1 */
    snprintf(nm, sizeof nm, "wgbmc%s", GBL_MECHS[m].pic);
    gs_load(11, nm, 423, 284, GS_LOOP, 0x14);
}
static void gc_mc_clips(void)
{
    gs_load(12, GBL_MX[g_brief_m].brf_video, 40, 267, GS_REWIND, 0);
    gs_flags(12, GS_PAUSED, 1);   /* local_14: paused on its first frame until VIEW */
    gs_load(2, "wgbmc005", 335, 306, GS_HOLD, 0);
    gs_load(3, "wgbmc005", 335, 345, GS_HOLD, 0);
    gs_load(4, "wgbmc005", 335, 382, GS_HOLD, 0);
    gs_load(7, "wgbmc001", 2, 8, GS_PAUSED, 0);
    gs_load(6, "wgbmc002", 2, 119, GS_PAUSED, 0);
    gs_load(5, "wgbmc003", 552, 8, GS_PAUSED, 0);
    gs_load(0, "wgbmc004", 14, 326, GS_HOLD, 0);
    gs_load(1, "wgbmcg04", 14, 326, GS_HOLD | GS_HIDDEN, 0);
    gc_mc_wireframe();
}
static int gbl_mission_computer(void)
{
    static gbl_text A, B;
    gbl_pilot *p = pil();
    int prev = g_gc_mc_prev, loc, pete, en[13], k, next = -1, mech[3];
    char *txt;
    if (!p) return 12;
    loc = p->hall;
    pete = !strcmp(g_scen, "petescn1");   /* 0x76b85, strcmp: case-sensitive, so "peteSCN1" never matches - kept */
    txt = gc_ordr(g_scen, "BRF1", "");
    gbl_text_pages(txt, GF_DEFAULT, 85, 27, 469, 403, &A);   /* box A: page 0 in the BRIEFING panel */
    gbl_text_pages(txt, GF_DEFAULT, 85, 27, 500, 403, &B);   /* box B: the DETAILS pager */
    if (!txt) return 10;   /* no briefing at all: launch */
    free(txt);
    if (!g_old_mission) g_brief_m = gc_mission();
    if (prev == 1) {   /* from the hall: background 19 and the zoom into the computer, agbmcx01 (190,431), to its end */
        gbl_bg(19);
        draw_bg();
        if (gs_load(0, "agbmcx01", 190, 431, 0, 0) == 0) gs_play_to_end(0);
        gs_close_all();
    }
    gbl_bg(14);
    gc_mc_clips();
    if (prev == 1 || prev == 3) {
        gbl_star *st = star_rec(0);
        gstar_set(0, -1, -1, -1, -1);
        gc_star_slot(1, NULL, "GhostBear", "Starmate 2");
        gc_star_slot(2, NULL, "GhostBear", "Starmate 3");
        gstar_set(0, 0, st->max, st->size, st->tons);
    }
    gstar_set(0, -1, -1, -1, -1);
    for (k = 0; k < 13; k++) en[k] = 1;
    if (A.npages - 1 <= 0) en[0] = 0;   /* DETAILS only when the briefing has more than the first page */
    if (loc != 0) en[6] = en[9] = 0;   /* away from the Sibko plant: the player keeps his 'Mech */
    if (pete) { static const int off[9] = {6, 9, 7, 10, 8, 11, 2, 3, 4}; for (k = 0; k < 9; k++) en[off[k]] = 0; }
    gsnd_play(0x7a, 0x32, 1);
    while (next == -1) {
        gbl_input *in;
        int h, slot_hover = -1, name_hover = -1, click, kk;
        const gbl_star *st = star_rec(0);
        gstar_set(0, -1, -1, -1, -1);
        for (kk = 0; kk < 3; kk++) mech[kk] = gc_star_mech(kk);
        h = gh_hit(MC_HOT, 13, en, g_in.mx, g_in.my);
        click = g_in.lclick && h >= 0;
        /* button frames (FUN_0003d7c0: 0 idle, 2 under the pointer) */
        gs_show_frame(7, h == 0 ? 2 : 0);
        gs_show_frame(6, h == 1 ? 2 : 0);
        gs_show_frame(5, h == 5 ? 2 : 0);
        gc_hide(0, 0); gc_hide(1, 1);
        if (h == 12 && !gc_playing(12)) { gc_hide(1, 0); gc_hide(0, 1); }
        if (h >= 2 && h <= 4) slot_hover = h - 2;
        if (h >= 6 && h <= 8) name_hover = h - 6;
        if (h >= 9 && h <= 11) name_hover = h - 9;
        if (click) {
            switch (h) {
            case 0: {   /* DETAILS: the pager over box B (its pages after the first), background 22 */
                int r;
                gs_show_frame(7, 1);
                draw_bg(); gs_update(); gbl_frame();
                gs_close_all();
                r = gbl_pager_from(22, &B, 1, "", NULL);   /* from the collection's second page */
                if (r == -3 || r == 8) next = r;
                gbl_bg(14);
                gc_mc_clips();
                break;
            }
            case 1:   /* LOGOFF */
                if (!g_old_mission) { pil_save(); next = 1; } else next = 12;
                gs_show_frame(6, 1);
                draw_bg(); gs_update(); gbl_frame();
                break;
            case 2: case 3: case 4: {   /* a slot name: that slot into the Mech Lab */
                int s = h - 2;
                if (mech[s] != -1 && st->size - s >= 0 && s < st->max) { gc_star_slot(s, NULL, NULL, NULL); next = 9; }
                break;
            }
            case 5:   /* LAUNCH */
                next = 10;
                if (!g_old_mission) pil_save();
                gs_show_frame(5, 1);
                draw_bg(); gs_update(); gbl_frame();
                break;
            case 6: case 7: case 8: case 9: case 10: case 11: {   /* next / previous 'Mech */
                int s = h >= 9 ? h - 9 : h - 6, dir = h >= 9 ? -1 : 1, v = mech[s], ok;
                if (!(s < st->max) || !((uint32_t)s <= (uint32_t)p->slot_limit || loc == 0)) break;
                do {
                    const char *code;
                    if (loc == 0) v += dir;
                    else v = v == -1 ? p->star.slot[s].mech : -1;   /* toggle None / the pilot's saved 'Mech (+0x72 + 0x40k) */
                    if (dir < 0 && v == -2) v = GC_CLAN_MECHS - 1;   /* previous: below the first wraps to the last */
                    if (v == -1 || (dir > 0 && v >= GC_CLAN_MECHS)) { v = -1; code = ""; if (loc == 0) p->slot_limit = s - 1; }
                    else { code = GBL_MECHS[v].code; if (loc == 0) p->slot_limit = s; }
                    gstar_set(0, -1, -1, -1, -1);
                    ok = gc_star_slot(s, code, NULL, NULL);
                } while (ok == 0 && !g_quit_all);
                if (s == 0) gc_mc_wireframe();
                break;
            }
            case 12:   /* VIEW: the briefing video again, with the voice-over */
                if (!gc_playing(12)) {
                    if (GBL_MX[g_brief_m].brief_snd != -1) gvoice_play(GBL_MX[g_brief_m].brief_snd, 0x4b);
                    gs_restart(12);
                }
                break;
            }
        }
        if (!gc_playing(12)) gs_show_frame(12, 0);   /* FUN_0003d7c0(12, 0) while it is not playing */
        if (next == -1 && gc_menu_key()) next = gbl_global_menu();
        if (next != -1) break;
        draw_bg();
        gs_update();
        gbl_text_draw(&A, 0);
        if (!pete)   /* the slot boxes: the table name or "None", " MECH LAB " under the pointer; hover colour 0x50 */
            for (kk = 0; kk < 3; kk++) {
                const char *t = mech[kk] < 0 ? "None" : GBL_MECHS[mech[kk]].name;
                int hov = kk == slot_hover || kk == name_hover;
                if (kk == slot_hover && mech[kk] >= 0) t = " MECH LAB ";
                gtext(GF_DEFAULT, MC_HOT[2 + kk].lx, MC_HOT[2 + kk].ly, t, hov ? 0x50 : -1);
            }
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) next = -3;
    }
    if (GBL_MX[g_brief_m].brief_snd > 0) gvoice_stop(GBL_MX[g_brief_m].brief_snd);
    return next;
}

/* ======================================================================================================================
 * state 10 coming from screen 5: clip list A of the pilot's mission (0x3abdf) */
static void gbl_launch_clips(void)
{
    if (!pil()) return;
    gc_clips(GBL_MX[gc_mission()].pre);
}

/* ======================================================================================================================
 * screen 3 - the debriefing (FUN_00024750(db, ctx, &scenario)) */
static const gbl_hot DB_HOT[4] = {   /* 0x7f178 */
  {  2,248, 90,476, 0,0, NULL},  /* 0 LOGOFF */
  {552,248,640,476, 0,0, NULL},  /* 1 AFTERMATH (win) */
  {552,248,640,476, 0,0, NULL},  /* 2 REPLAY (no win) */
  { 14, 86, 34,154, 0,0, NULL},  /* 3 VIEW (win) */
};
static int gc_u16(const uint8_t *b, int o) { return b[o] | b[o + 1] << 8; }
static int32_t gc_i32(const uint8_t *b, int o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static int gc_altered(const uint8_t *dif) { return dif[0] == 1 || dif[1] == 1 || dif[3] == 0; }
static FILE *gc_open_result(const char *name)   /* the sim's files: the launch's save folder, else the install folder */
{
    char path[1200];
    const char *save = getenv("MW2_SAVE_DIR");
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", save ? save : g_dir, name);
    if ((f = fopen(path, "rb"))) return f;
    return dp_fopen(name, "rb");
}
/* "<label>\g350\b<width of value><value>\n" (the value right-aligned at the box x + 350) */
static void gc_cat(char *out, size_t n, const char *s) { size_t l = strlen(out); snprintf(out + l, n - l, "%s", s); }
static void gc_val(char *out, size_t n, const char *label, int v)
{
    char val[32], line[256];
    snprintf(val, sizeof val, "%d", v);
    snprintf(line, sizeof line, "%s\\g%03d\\b%03d%s\\n", label, 350, gtext_w(GF_DEFAULT, val), val);
    gc_cat(out, n, line);
}
/* the STATISTICS text and the honor (0x24750 .. 0x25780); updates the pilot's kills / hits / shots / honor */
static void gc_score(const uint8_t *msn, const uint8_t *car, const uint8_t *dif, int status, gbl_pilot *p, char *out, size_t on)
{
    int no = gc_i32(msn, 4), k, honor = 5000, sec = 0, ter = 0, ord[48], n = 0;
    char label[160];
    if (no < 0) no = 0;
    if (no > 48) no = 48;
    out[0] = 0;
    gc_cat(out, on, "Time\\g040Type\\g110Objective\\g400Status\\n");
    /* sorted by time, DNF (-1) last (comparator 0x22db0); insertion order as Watcom's qsort leaves all-DNF lists
     * (MEASURED C_34: Tertiary, Secondary, Primary - reversed); INFERRED for mixed lists */
    for (k = 0; k < no; k++) {
        int j = n;
        while (j > 0) {
            int ta = gc_i32(msn, 0x14 + ord[j - 1] * 52 + 0xc), tb = gc_i32(msn, 0x14 + k * 52 + 0xc), c;
            c = ta < 0 ? 1 : tb < 0 ? -1 : ta > tb ? 1 : ta < tb ? -1 : 0;
            if (c <= 0) break;
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = k;
        n++;
    }
    for (k = 0; k < n; k++) {
        const uint8_t *o = msn + 0x14 + ord[k] * 52;
        int st = gc_i32(o, 0), ty = gc_i32(o, 4), t = gc_i32(o, 0xc), q, w = 0, sp = 0;
        const char *tn = ty == 0 ? "Default" : ty == 1 ? "Primary" : ty == 2 ? "Secondary" : ty == 4 ? "Tertiary" : ty == 8 ? "Return" : "Unknown";
        char tm[16], txt[40], line[200];
        for (q = 0; q < 32 && o[0x14 + q]; q++) {   /* runs of control characters / spaces -> one space */
            if (o[0x14 + q] < 0x21) { if (!sp) { txt[w++] = ' '; sp = 1; } }
            else { txt[w++] = (char)o[0x14 + q]; sp = 0; }
        }
        txt[w] = 0;
        if (t < 0) snprintf(tm, sizeof tm, "DNF");
        else snprintf(tm, sizeof tm, "%02d:%02d", (t / 60) % 60, t % 60);
        snprintf(line, sizeof line, "%s\\g040%s\\g110%s\\g400%s\\n", tm, tn, txt, st == 0 ? "Failed" : st == 1 ? "Successful" : "Unknown");
        gc_cat(out, on, line);
    }
    for (k = 0; k < no; k++) {
        const uint8_t *o = msn + 0x14 + k * 52;
        int st = gc_i32(o, 0), ty = gc_i32(o, 4);
        if (ty == 1 && st == 0) honor = 0;
        if (ty == 2 && st == 1) sec++;
        if (ty == 4 && st == 1) ter++;
    }
    gc_val(out, on, "Mission Completion:", honor);
    if (sec > 0) { honor += sec * 1500; snprintf(label, sizeof label, "Secondary Objective Completed:\\t\\t%d\\t(x%d)", 1500, sec); gc_val(out, on, label, sec * 1500); }
    if (ter > 0) { honor += ter * 500; snprintf(label, sizeof label, "Tertiary Objective Completed:\\t\\t%d\\t(x%d)", 500, ter); gc_val(out, on, label, ter * 500); }
    {
        int d = gc_u16(car, 0x34);
        if (d > 0 && status == 2) { honor += d * -4000; snprintf(label, sizeof label, "Friendly Unit Deaths:\\t\\t\\t\\t%d\\t(x%d)", -4000, d); gc_val(out, on, label, d * -4000); }
    }
    gc_cat(out, on, "\\t\\t\\t\\t\\t\\tdirect\\ttotal\\n");
    {
        int md = gc_u16(car, 0x07), mt = gc_u16(car, 0x1e), vd = gc_u16(car, 0x44), vt = gc_u16(car, 0x4a);
        p->kills += mt;
        honor += mt * 250;
        snprintf(label, sizeof label, "Enemy Mechs Destroyed:\\t\\t\\t%d\\t%d", md, mt); gc_val(out, on, label, mt * 250);
        p->kills += vd;
        honor += vt * 125;
        snprintf(label, sizeof label, "Enemy Vehicles Destroyed:\\t\\t\\t%d\\t%d", vd, vt); gc_val(out, on, label, vt * 125);
    }
    {   /* tonnage limit x MAX star size - the star's 'Mechs */
        const gbl_star *st = star_rec(0);
        int under = st->tons * st->max;
        for (k = 0; k < st->size && k < 3; k++) if (st->slot[k].mech >= 0 && st->slot[k].mech < 32) under -= GBL_MECHS[st->slot[k].mech].tons;
        if (under > 0) { honor += under * 25; snprintf(label, sizeof label, "Star Underweight Bonus:\\t\\t\\t%d\\t(x%d tons)", 25, under); gc_val(out, on, label, under * 25); }
    }
    {
        int shots = gc_u16(car, 0x13), hits = gc_u16(car, 0x15), bonus;
        double r = shots ? (double)hits / (double)shots : 0.0;
        bonus = r >= 0.7 ? 750 : 0;
        if (r <= 1.0) { snprintf(label, sizeof label, "Hit Percentage:\\t\\t\\t\\t%3.1f", r * 100.0); gc_val(out, on, label, bonus); honor += bonus; }
        p->hits += hits;
        p->shots += shots;
    }
    {
        const char *dn = dif[5] == 0 ? "EASY" : dif[5] == 1 ? "MEDIUM" : dif[5] == 2 ? "HARD" : "MEDIUM";   /* > 2: ASSUMED MEDIUM */
        double dm = dif[5] == 0 ? 0.8 : dif[5] == 2 ? 1.3 : 1.0;
        if (dif[4] == 0) { gc_cat(out, on, "\\cThe Keshik deems it Dishonorable to Alter Heat Tracking\\n"); honor = 0; }
        else if (status == 2) {
            gc_val(out, on, "Mission Honor:", honor);
            honor = (int)lrint((double)honor * dm);
            snprintf(label, sizeof label, "Difficulty Multiplier:\\t(%s = %1.1f)", dn, dm);
            gc_val(out, on, label, honor);
        }
    }
    if (gc_altered(dif)) { gc_cat(out, on, "\\cNo Career Advancement with Altered Reality Enabled\\n"); honor = 0; }
    if (status == 3) { gc_cat(out, on, "\\n\\cMission Failed:  NO HONOR ACQUIRED\\n"); honor = 0; }
    if (status == 2) p->honor += honor;
    gc_val(out, on, "Career Honor:", p->honor);
}
static void gc_db_clips(int status, int m, int trial)
{
    gbl_pilot *p = pil();
    if (status == 2) {
        gs_load(2, "wgbmc004", 14, 86, GS_HOLD, 0);
        gs_load(3, "wgbmcg04", 14, 86, GS_HOLD | GS_HIDDEN, 0);
        gs_load(0, "wgbdb001", 552, 248, GS_PAUSED, 0);   /* AFTERMATH */
        if (trial) {
            static const char *const RANKCLIP[6] = {"rankia", "rankia", "rankib", "rankic", "rankid", "rankie"};   /* 0x7e444 */
            int r = p ? (int)p->rank : 0;
            gs_load(4, RANKCLIP[r < 0 ? 0 : r > 5 ? 5 : r], 41, 28, GS_REWIND, 0);
        } else gs_load(4, GBL_MX[m].dbfs_video, 41, 28, GS_REWIND, 0);
        gs_flags(4, GS_PAUSED, 1);   /* one frame shown, then paused until VIEW */
    } else gs_load(0, "wgbdb004", 552, 248, GS_PAUSED, 0);   /* REPLAY */
    gs_load(1, "wgbdb002", 2, 248, GS_PAUSED, 0);   /* LOGOFF */
}
static int gbl_debriefing(void)
{
    static uint8_t msn[0x9d4], car[0x50];
    static char stats[16384];
    static gbl_text DBOX, AFT, SBOX;
    uint8_t dif[8] = {0, 0, 1, 1, 1, 1, 0, 0};   /* 0x7e804 static defaults */
    gbl_pilot saved, *p;
    int m, status, trial, en[4], next = -1, k;
    char q[160];
    FILE *f;
    DBOX.npages = DBOX.nlines = AFT.npages = AFT.nlines = 0;
    pil_load();   /* FUN_0001dfe0 */
    if (!(p = pil())) return 1;
    m = gc_mission();
    trial = GBL_MISSIONS[m].trial == 1;
    if (!g_scen[0] && GBL_MISSIONS[m].scn) snprintf(g_scen, sizeof g_scen, "%s", GBL_MISSIONS[m].scn);   /* port: a start on screen 3 (the original resumes with MW2PRM's scenario) */
    memset(msn, 0, sizeof msn); memset(car, 0, sizeof car);
    if ((f = gc_open_result("MW2CAR.CFG"))) { if (fread(car, 1, sizeof car, f)) {} fclose(f); }
    if ((f = gc_open_result("MW2MSN.CFG"))) { if (fread(msn, 1, sizeof msn, f)) {} fclose(f); }
    if ((f = dp_fopen("MW2DIF.CFG", "rb"))) { if (fread(dif, 1, sizeof dif, f)) {} fclose(f); }
    status = gc_i32(msn, 0x10);
    if (trial && (gc_u16(car, 0x1e) + gc_u16(car, 0x07) != 0 || GBL_MX[m].always_pass)) {   /* a trial with a kill is won */
        int32_t one = 1;
        status = 2;
        memcpy(msn + 0x14, &one, 4);   /* objective 0 succeeded */
    }
redo:
    gsnd_play(0x7a, 0x32, 1);
    if (g_ctx != 2) saved = *p;
    gc_score(msn, car, dif, status, p, stats, sizeof stats);
    q[0] = 0;
    if (g_ctx != 2) {
        int up = 0;
        if (g_ctx != 2 && trial && !gc_altered(dif) && status == 2 && dif[4] == 1) {   /* trial: one rank per objective won */
            int no = gc_i32(msn, 4);
            for (k = 0; k < no && k < 48; k++) if (gc_i32(msn, 0x14 + k * 52) == 1) up++;
        }
        p->rank += up;
        if (p->rank < 6) {
            if (trial && (gc_u16(car, 0x07) + gc_u16(car, 0x1e) != 0 || GBL_MX[m].always_pass))
                snprintf(q, sizeof q, "\\nYou have earned the rank of %s.\\n\\n", GC_RANKS[p->rank < 0 ? 0 : p->rank]);   /* 0x768b8: the new rank (0x25868) */
        } else p->rank = 6;
        if (status == 2 && !gc_altered(dif)) {
            gc_clips(GBL_MX[m].win);   /* list B */
            p->cur_fail = 0;
            p->done++;
            p->hall = GBL_MX[m].location;
            if (p->deaths != 0 && p->done > 16) p->done = 22;   /* a pilot who ever failed retires after the platform */
        } else if (status == 3) {
            p->cur_fail++;
            if ((uint32_t)p->cur_fail > 0u /* retry cap +0x19 (0) */ && !trial && m < 17 && m != 3 && !(car[0x1d] & 2) && (car[0x1d] & 4))
                gbl_video("deathmov");
            p->deaths++;
            {
                char nm[24];
                snprintf(nm, sizeof nm, "%s", p->name);
                snprintf(p->display, sizeof p->display, "%.17s %02d", nm, (int)p->deaths % 100);   /* 0x768f2 "%s %02d" */
            }
        }
        pil_save();   /* FUN_0001e1c0 */
        {
            char *t = gc_ordr(g_scen, status == 2 ? "DBFS" : "DBFF", q);
            if (t) {
                gbl_text_pages(t, GF_DEFAULT, 367, 29, 230, 187, &DBOX);   /* the DEBRIEFING panel (page 0) */
                gbl_text_pages(t, GF_DEFAULT, 88, 30, 454, 400, &AFT);     /* the AFTERMATH pager (pages after the first) */
            }
            free(t);
        }
    }
    gbl_text_pages(stats, GF_DEFAULT, 85, 268, 469, 196, &SBOX);   /* STATISTICS (one page; no markup to fill) */
    gbl_bg(21);
    for (k = 0; k < 4; k++) en[k] = 1;
    if (status == 2) en[2] = 0;   /* REPLAY: enabled on a failure, off on a win, as created otherwise */
    if (AFT.npages <= 1 || status != 2) en[1] = 0;
    if (status != 2) en[3] = 0;
    gs_close_all();
    gc_db_clips(status, m, trial);
    while (next == -1) {
        gbl_input *in;
        int h, click;
        if (g_ctx != 2 && !strcmp(p->name, GBL_CHEATS[CH_IMMORTAL]) && (gc_alt_key(SDLK_w) || gc_alt_key(SDLK_l))) {
            /* IMMORTAL: Alt+W redoes the debriefing as a win, Alt+L as a failure, from the pilot copy */
            status = g_in.key == SDLK_w ? 2 : 3;
            *p = saved;
            gs_close_all();
            gsnd_stop_all();
            g_in.key = 0;
            goto redo;
        }
        h = gh_hit(DB_HOT, 4, en, g_in.mx, g_in.my);
        click = g_in.lclick && h >= 0;
        gs_show_frame(1, h == 0 ? 2 : 0);
        gs_show_frame(0, h == 1 || h == 2 ? 2 : 0);
        if (status == 2) { gc_hide(2, 0); gc_hide(3, 1); }
        if (h == 3 && !gc_playing(4)) { gc_hide(3, 0); gc_hide(2, 1); }
        if (click) {
            switch (h) {
            case 0:   /* LOGOFF */
                gs_show_frame(1, 1);
                draw_bg(); gs_update(); gbl_frame();
                next = 1;
                if (g_ctx != 2 && status == 2) {
                    if (p->deaths == 0 && p->done > 21) next = 16;   /* the winner's ending */
                    else if (p->done < 22) {
                        snprintf(g_scen, sizeof g_scen, "%s", GBL_MISSIONS[p->done].scn);
                        gc_clips(GBL_MX[m].exitwin);   /* list C (mission +0x18: both branches return 1) */
                    }
                }
                break;
            case 1: {   /* AFTERMATH: the pager, background 20 */
                int r;
                gs_show_frame(0, 1);
                draw_bg(); gs_update(); gbl_frame();
                gs_close_all();
                r = gbl_pager_from(20, &AFT, 1, "", NULL);   /* from the collection's second page */
                if (r == -3 || r == 8) next = r;
                gbl_bg(21);
                gc_db_clips(status, m, trial);
                break;
            }
            case 2:   /* REPLAY: the same mission again (SUPS only) */
                gs_show_frame(0, 1);
                draw_bg(); gs_update(); gbl_frame();
                next = 5;
                snprintf(g_scen, sizeof g_scen, "%s", GBL_MISSIONS[m].scn ? GBL_MISSIONS[m].scn : "");
                gc_brf2(g_scen, 0);
                break;
            case 3:   /* VIEW: the debriefing / rank clip with the voice-over */
                if (!gc_playing(4)) {
                    if (GBL_MX[m].debrief_snd != -1) gvoice_play(GBL_MX[m].debrief_snd, 0x4b);
                    gs_restart(4);
                }
                break;
            }
        }
        if (next == -1 && gc_menu_key()) next = gbl_global_menu();
        if (next != -1) break;
        draw_bg();
        gs_update();
        if (DBOX.npages > 0) gbl_text_draw(&DBOX, 0);
        if (SBOX.npages > 0) gbl_text_draw(&SBOX, 0);
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) next = -3;
    }
    if (GBL_MX[m].debrief_snd > 0) gvoice_stop(GBL_MX[m].debrief_snd);
    return next;
}
