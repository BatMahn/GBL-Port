/*
 * audit_missions - the mission data audit: every mission scene loaded headless (the simulation without drawing), run
 * a few seconds so the units settle, with one line per finding.
 *
 *   audit_missions MW2.PRJ [-s seconds] [SCENE ...]
 *     -s n     how long the simulation runs before the unit checks (default 3)
 *   The shell's hand-off files (USERSTAR, EN01..05STAR, INSTMAP1) are written fresh in a temporary directory, the
 *   sweep's fixed lance (Timber Wolf, Mad Dog, Stormcrow vs Mad Dog, Stormcrow, Nova), so every scene has its units.
 *   env: MW2_INSTALL_DIR the install (searched after them)
 *        AUDIT_GT=<record>   every GT chunk of that record and its objects' state
 *        AUDIT_NEAR=x,z,r    the world parts within r cm of (x, z)
 *
 * With no scene, every *SCN1 record is audited, each in its own child process (a crash is a finding too).
 *
 * Findings (FIND lines: scene, kind, details):
 *   BOUNDARY  the player or a unit outside the mission area after settling: outside every boundary nav point - NAVP
 *             type (byte +27) 0x20 or 0x22, radius (u16 +24) x 100 cm, a sphere about the nav's point (the leave nodes
 *             test the distance in 3D). The type byte is the mission editor's: GBL MW2.EXE's NAVP handler (0x4f530,
 *             the NAVP branch at 0x50450) keeps the radius x 100 (nav +0x10) and never reads it; 0x23 is a shown nav
 *   LEADER    a unit more than 1 km from its group's leader (formation data)
 *   GROUND    a unit more than 2 m above or below the floor under it after settling (drop-ins fall about 46 m in 3 s;
 *             flying classes - hovercraft 2, aircraft 5, dropships 8 - are left out)
 *   OVERLAP   two units (the player too) within 3 m of each other after settling
 *   THING     a GT thing's intact object hidden at the start, or its destroyed variant shown (wrecks chained to another
 *             thing's destroyed variant and GT 0x8000 debris nodes left out)
 *   ORIGIN    a world object at the map origin (within 1 m in x / z) while its record's other objects lie over 500 m
 *             away (a stray: an object no block or path places)
 *   NAV       a nav point the player can select (NAVP +16 set, +22 0) outside every boundary
 *   CRASH     the scene's child process died
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msim.h"
#include "bwd.h"
#include "datapath.h"
#include "lance.h"

#define DT_MS 50

static float g_secs = 3.0f;
static int g_find;
static const char *g_dir;   /* the hand-off files' directory (removed at the end) */
static lance_slot g_fr[3];   /* the player's star (USERSTAR rewritten per scene at the mission's size) */

static void find(const char *scene, const char *kind, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void find(const char *scene, const char *kind, const char *fmt, ...)
{
    va_list ap;
    printf("FIND %-9s %-8s ", scene, kind);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    g_find++;
}

typedef struct { char rec[17]; float x, y, z, r; int type, shown; char label[45]; } nav_t;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* the mission tree's NAVP chunks with their records' names */
static int load_navs(const bwd_mission *bm, nav_t *out, int max)
{
    int r, c, n = 0;
    for (r = 0; r < bm->record_count; r++)
        for (c = 0; c < bm->records[r].chunk_count && n < max; c++) {
            const bwd_chunk *ch = &bm->records[r].chunks[c];
            if (strcmp(ch->tag, "NAVP") != 0 || ch->size < 28) continue;
            snprintf(out[n].rec, sizeof out[n].rec, "%s", bm->records[r].name);
            out[n].x = (float)rd32(ch->data); out[n].y = (float)rd32(ch->data + 4); out[n].z = (float)rd32(ch->data + 8);
            out[n].r = (float)(ch->data[24] | ch->data[25] << 8) * 100.0f;
            out[n].type = ch->data[27];
            out[n].shown = rd16s(ch->data + 16) != 0 && rd16s(ch->data + 22) == 0;   /* (the player's: NAVP +16 set, +22 team 0) */
            out[n].label[0] = 0;
            if (ch->size > 28) {
                size_t k, l = ch->size - 28 < 44 ? ch->size - 28 : 44;
                for (k = 0; k < l && ch->data[28 + k]; k++) out[n].label[k] = (char)ch->data[28 + k];
                out[n].label[k] = 0;
                while (k > 0 && out[n].label[k - 1] == ' ') out[n].label[--k] = 0;
            }
            n++;
        }
    return n;
}

static int is_boundary(const nav_t *v) { return (v->type == 0x20 || v->type == 0x22) && v->r > 0; }
/* inside any boundary sphere (1 when the mission has none) */
static int inside_area(const nav_t *nv, int n, float x, float y, float z, float *worst)
{
    int k, any = 0;
    float best = 1e30f;
    for (k = 0; k < n; k++) {
        float dx, dy, dz, d;
        if (!is_boundary(&nv[k])) continue;
        any = 1;
        dx = x - nv[k].x; dy = y - nv[k].y; dz = z - nv[k].z;
        d = sqrtf(dx * dx + dy * dy + dz * dz) - nv[k].r;
        if (d < best) best = d;
    }
    if (worst) *worst = best;
    return !any || best <= 0.0f;
}

/* the GT chunks of the world's records: intact (A) / destroyed (B) objects' states at the start */
static void check_things(const char *scene, prj_archive *a, const mech3d *w)
{
    int i, k, q;
    const char *dump = getenv("AUDIT_GT");
    for (i = 0; i < w->part_count; i++) {
        prj_record r;
        int seen = 0, n;
        bwd_chunk *c;
        if (!w->parts[i].rec[0]) continue;
        for (k = 0; k < i && !seen; k++) if (strcmp(w->parts[k].rec, w->parts[i].rec) == 0) seen = 1;
        if (seen || prj_read_named(a, "BWD", w->parts[i].rec, &r) != PRJ_OK) continue;
        n = bwd_chunks(r.data, r.size, NULL, 0);
        if (n > 0 && (c = calloc((size_t)n, sizeof *c))) {
            int nb = 0, bobj[256];
            bwd_chunks(r.data, r.size, c, n);
            for (k = 0; k < n; k++)   /* every destroyed variant of the record (the chain test) */
                if (strcmp(c[k].tag, "GT") == 0 && c[k].size >= 68 && nb < 256) bobj[nb++] = rd16s(c[k].data + 2);
            for (k = 0; k < n; k++) {
                const uint8_t *d = c[k].data;
                int ga, gb, flags, ia = -1, ib = -1, chained = 0, j;
                char name[23];
                if (strcmp(c[k].tag, "GT") != 0 || c[k].size < 68) continue;
                ga = rd16s(d); gb = rd16s(d + 2); flags = d[20] | d[21] << 8;
                snprintf(name, sizeof name, "%.22s", (const char *)d + 24);
                for (j = 0; j < nb; j++) if (bobj[j] == ga && ga >= 0) chained = 1;
                for (q = 0; q < w->part_count; q++)
                    if (strcmp(w->parts[q].rec, w->parts[i].rec) == 0) {
                        if (w->parts[q].obj_index == ga) ia = q;
                        if (gb >= 0 && w->parts[q].obj_index == gb) ib = q;
                    }
                if (dump && strcasecmp(dump, w->parts[i].rec) == 0)
                    printf("  GT %s A %d (part %d %s hidden %d) B %d (part %d %s hidden %d) hp %d type %#x flags %#x%s \"%s\"\n", w->parts[i].rec, ga, ia,
                           ia >= 0 ? w->parts[ia].model_name : "-", ia >= 0 ? w->parts[ia].hidden : -1, gb, ib, ib >= 0 ? w->parts[ib].model_name : "-",
                           ib >= 0 ? w->parts[ib].hidden : -1, rd16s(d + 16), d[18] | d[19] << 8, flags, chained ? " chained" : "", name);
                if (flags & 0x8000) continue;   /* a debris node */
                if (ia >= 0 && w->parts[ia].hidden && !chained)
                    find(scene, "THING", "%s \"%s\": intact object %d (%s) hidden at the start", w->parts[i].rec, name, ga, w->parts[ia].model_name);
                if (ib >= 0 && !w->parts[ib].hidden && !w->parts[ib].moving)
                    find(scene, "THING", "%s \"%s\": destroyed variant %d (%s) shown at the start", w->parts[i].rec, name, gb, w->parts[ib].model_name);
            }
            free(c);
        }
        prj_record_free(&r);
    }
}

/* world objects at the map origin while their record lies elsewhere */
static void check_origin(const char *scene, const mech3d *w)
{
    int i, k;
    for (i = 0; i < w->part_count; i++) {
        const mech3d_part *p = &w->parts[i];
        int nfar = 0, nall = 0, seen = 0;
        if (!p->rec[0]) continue;
        for (k = 0; k < i && !seen; k++) if (strcmp(w->parts[k].rec, p->rec) == 0) seen = 1;
        if (seen) continue;
        for (k = 0; k < w->part_count; k++) {   /* the record's objects away from the origin */
            const mech3d_part *o = &w->parts[k];
            if (strcmp(o->rec, p->rec) != 0) continue;
            if (fabsf((float)o->pos[0]) > 100.0f || fabsf((float)o->pos[2]) > 100.0f) {
                nall++;
                if (sqrtf((float)o->pos[0] * (float)o->pos[0] + (float)o->pos[2] * (float)o->pos[2]) > 50000.0f) nfar++;
            }
        }
        if (nall == 0 || nfar * 2 < nall) continue;   /* (most of the record over 500 m out) */
        for (k = 0; k < w->part_count; k++) {
            const mech3d_part *o = &w->parts[k];
            int v, ext = 0;
            if (strcmp(o->rec, p->rec) != 0 || fabsf((float)o->pos[0]) > 100.0f || fabsf((float)o->pos[2]) > 100.0f) continue;
            for (v = 0; o->model.object_count > 0 && v < o->model.objects[0].vert_count; v++)   /* (its reach from its origin) */
                if (abs(o->model.objects[0].verts[v].x) > ext) ext = abs(o->model.objects[0].verts[v].x);
            for (v = 0; o->model.object_count > 0 && v < o->model.objects[0].vert_count; v++)
                if (abs(o->model.objects[0].verts[v].z) > ext) ext = abs(o->model.objects[0].verts[v].z);
            if (ext > 50000) continue;   /* (a ground piece about the origin, over 500 m across: placed there on purpose) */
            if (o->model.object_count < 1 || o->model.objects[0].poly_count < 1) continue;   /* (a dummy node: nothing drawn) */
            if (o->hidden && o->gt_side > 0) continue;   /* (a thing's destroyed variant, hidden until the swap puts it in the
                                                           * intact object's place: world3d_paths_destroyed) */
            find(scene, "ORIGIN", "%s object %d (%s) at %d,%d,%d%s - %d of the record's %d other objects over 500 m away", o->rec, o->obj_index,
                 o->model_name, o->pos[0], o->pos[1], o->pos[2], o->hidden ? " (hidden)" : o->moving ? " (path-carried)" : "", nfar, nall);
        }
    }
}

/* GBL's nine 8-player network arenas (sweep_objectives' list) */
static int net_arena(const char *nm)
{
    static const char *const NET[9] = {"CALI", "COLL", "DOBE", "HUSK", "PERS", "SHAR", "SIAM", "TABB", "TERR"};
    int k;
    for (k = 0; k < 9; k++) if (strncasecmp(nm, NET[k], 4) == 0) return 1;
    return 0;
}

static int audit_scene(prj_archive *a, const char *scene)
{
    msim s;
    world_actor *ac = NULL;
    int n = 0, i, j, nnav = 0, have_bm, steps;
    mech3d world;
    bwd_mission bm;
    nav_t nv[256];
    float y_then[512];
    g_find = 0;
    printf("=== %s\n", scene);
    {   /* the player's star at the mission's size (BRF2 SDSC +8, as the campaign shell writes it - sweep_objectives) */
        char brf[16], path[512];
        prj_record r;
        int size = 3;
        snprintf(brf, sizeof brf, "%.4sBRF2", scene);
        if (prj_read_named(a, "BWD", brf, &r) == PRJ_OK) {
            int nc = bwd_chunks(r.data, r.size, NULL, 0), q;
            bwd_chunk *cc = nc > 0 ? calloc((size_t)nc, sizeof *cc) : NULL;
            if (cc) {
                bwd_chunks(r.data, r.size, cc, nc);
                for (q = 0; q < nc; q++) if (strcmp(cc[q].tag, "SDSC") == 0 && cc[q].size >= 16) { size = cc[q].data[8] | cc[q].data[9] << 8; break; }
                free(cc);
            }
            prj_record_free(&r);
        }
        if (size < 1) size = 1;
        if (size > 3) size = 3;
        snprintf(path, sizeof path, "%s/USERSTAR.BWD", g_dir);
        lance_write_star(a, path, g_fr, size, 0, 0);
    }
    if (net_arena(scene)) printf("  a network arena (no single-player launch in GBL: docs/gbl/E_sim.md 3.1)\n");
    if (world3d_actors(a, scene, &ac, &n) != 0) { find(scene, "ERROR", "world3d_actors failed"); return 1; }
    if (msim_init(&s, a, scene, ac, n) != 0) { find(scene, "ERROR", "msim_init failed"); world3d_free_actors(ac, n); return 1; }
    memset(&world, 0, sizeof world);
    if (world3d_mission(a, scene, 1, &world) > 0) msim_set_world(&s, &world);
    have_bm = bwd_mission_load(a, scene, &bm) == 0;
    if (have_bm) nnav = load_navs(&bm, nv, 256);
    {
        int nb = 0;
        for (i = 0; i < nnav; i++) if (is_boundary(&nv[i])) nb++;
        printf("  %d actors, %d navs (%d boundary:", n, nnav, nb);
        for (i = 0; i < nnav; i++) if (is_boundary(&nv[i])) printf(" %s %#x %.0f m", nv[i].rec, nv[i].type, (double)nv[i].r / 100.0);
        printf("), %d world objects\n", world.part_count);
    }
    if (getenv("AUDIT_NEAR")) {   /* the world parts near a point */
        float px = 0, pz = 0, pr = 0;
        if (sscanf(getenv("AUDIT_NEAR"), "%f,%f,%f", &px, &pz, &pr) == 3)
            for (i = 0; i < world.part_count; i++) {
                const mech3d_part *p = &world.parts[i];
                float dx = (float)p->pos[0] - px, dz = (float)p->pos[2] - pz;
                if (dx * dx + dz * dz <= pr * pr)
                    printf("  near: part %d %s rec %s obj %d at %d,%d,%d type %#x hidden %d moving %d\n", i, p->model_name, p->rec, p->obj_index,
                           p->pos[0], p->pos[1], p->pos[2], p->objtype, p->hidden, p->moving);
            }
    }
    /* at the start: the things, strays, shown navs, leaders */
    check_things(scene, a, &world);
    for (i = 0; i < nnav; i++) {
        float d;
        if (!nv[i].shown || is_boundary(&nv[i])) continue;
        if (!inside_area(nv, nnav, nv[i].x, nv[i].y, nv[i].z, &d))
            find(scene, "NAV", "%s \"%s\" at %.0f,%.0f,%.0f: %.0f m outside the area", nv[i].rec, nv[i].label, (double)nv[i].x, (double)nv[i].y, (double)nv[i].z, (double)d / 100.0);
    }
    for (i = 0; i < n; i++) {   /* the leader: slot 0 of the unit's table, in its own record when there is one there */
        int l = -1;
        float dx, dz;
        for (j = 0; j < n && l < 0; j++) if (ac[j].leader && ac[j].table == ac[i].table && strcmp(ac[j].group, ac[i].group) == 0) l = j;
        for (j = 0; j < n && l < 0; j++) if (ac[j].leader && ac[j].table == ac[i].table) l = j;
        if (l < 0 || l == i || ac[i].leader) continue;
        dx = (float)(ac[i].mech.origin[0] - ac[l].mech.origin[0]); dz = (float)(ac[i].mech.origin[2] - ac[l].mech.origin[2]);
        if (dx * dx + dz * dz > 100000.0f * 100000.0f)
            find(scene, "LEADER", "actor %d %s (%s, %s) starts %.0f m from its leader %d %s", i, ac[i].name, ac[i].group, ac[i].skel,
                 (double)sqrtf(dx * dx + dz * dz) / 100.0, l, ac[l].name);
    }
    /* settle */
    s.debug_invulnerable = 1;
    s.player_unit.invulnerable = 1;
    steps = (int)(g_secs * 1000.0f / (float)DT_MS + 0.5f);
    for (i = 0; i < steps && !s.over; i++) msim_step(&s, DT_MS);
    for (i = 0; i < n && i < 512; i++) y_then[i] = s.units[i].y;
    for (i = 0; i < 10 && !s.over; i++) msim_step(&s, DT_MS);   /* half a second more: who is still falling */
    /* after settling: strays (the path-carried objects where their paths have put them), the area, the floor, overlaps */
    check_origin(scene, &world);
    {
        float d;
        if (!inside_area(nv, nnav, s.player[0], s.player_unit.y, s.player[2], &d))
            find(scene, "BOUNDARY", "the player at %.0f,%.0f,%.0f: %.0f m outside the area", (double)s.player[0], (double)s.player_unit.y, (double)s.player[2], (double)d / 100.0);
    }
    for (i = 0; i < n; i++) {
        const float x = (float)s.actors[i].mech.origin[0], z = (float)s.actors[i].mech.origin[2], y = s.units[i].y;
        const int cls = ac[i].unit_class;
        float d, g;
        if (s.armed[i] && s.units[i].destroyed) continue;
        if (!inside_area(nv, nnav, x, y, z, &d))
            find(scene, "BOUNDARY", "actor %d %s (%s, %s) at %.0f,%.0f,%.0f: %.0f m outside the area", i, ac[i].name, ac[i].group, ac[i].skel,
                 (double)x, (double)y, (double)z, (double)d / 100.0);
        if (cls != 2 && cls != 5 && cls != 8) {
            g = msim_ground(&s, x, z, y);
            if (y - g > 200.0f && i < 512 && y_then[i] - y > 50.0f)   /* falling (a drop-in: they start up high on purpose) */
                printf("  note: actor %d %s (%s, %s) %.1f m above the floor after %.0f s, falling (%.1f m in 0.5 s): a drop-in\n", i, ac[i].name, ac[i].group, ac[i].skel,
                       (double)(y - g) / 100.0, (double)g_secs, (double)(y_then[i] - y) / 100.0);
            else if (y - g > 200.0f) find(scene, "GROUND", "actor %d %s (%s, %s, class %d) %.1f m above the floor at %.0f,%.0f after %.0f s", i, ac[i].name, ac[i].group, ac[i].skel, cls,
                                     (double)(y - g) / 100.0, (double)x, (double)z, (double)g_secs);
            else if (g - y > 200.0f) find(scene, "GROUND", "actor %d %s (%s, %s, class %d) %.1f m below the floor at %.0f,%.0f after %.0f s", i, ac[i].name, ac[i].group, ac[i].skel, cls,
                                          (double)(g - y) / 100.0, (double)x, (double)z, (double)g_secs);
        }
    }
    for (i = -1; i < n; i++)
        for (j = i + 1; j < n; j++) {
            const float xi = i < 0 ? s.player[0] : (float)s.actors[i].mech.origin[0], zi = i < 0 ? s.player[2] : (float)s.actors[i].mech.origin[2];
            const float yi = i < 0 ? s.player_unit.y : s.units[i].y;
            const float dx = xi - (float)s.actors[j].mech.origin[0], dz = zi - (float)s.actors[j].mech.origin[2], dy = yi - s.units[j].y;
            if ((i >= 0 && s.armed[i] && s.units[i].destroyed) || (s.armed[j] && s.units[j].destroyed)) continue;
            if (dx * dx + dz * dz < 300.0f * 300.0f && fabsf(dy) < 1000.0f)
                find(scene, "OVERLAP", "%s%d %s and actor %d %s (%s) %.1f m apart after %.0f s", i < 0 ? "the player" : "actor ", i < 0 ? 0 : i,
                     i < 0 ? "" : ac[i].name, j, ac[j].name, ac[j].group, (double)sqrtf(dx * dx + dz * dz) / 100.0, (double)g_secs);
        }
    printf("RESULT %-9s findings %d\n", scene, g_find);
    if (have_bm) bwd_mission_free(&bm);
    msim_free(&s);
    mech3d_free(&world);
    world3d_free_actors(ac, n);
    return 0;
}

int main(int argc, char **argv)
{
    prj_archive *a;
    char err[256];
    const char *names[512];
    int i, k, nn = 0, single = 0;
    if (argc < 2) { fprintf(stderr, "usage: %s MW2.PRJ [-s seconds] [SCENE ...]\n", argv[0]); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    a = prj_open(argv[1], err, sizeof err);
    if (!a) { fprintf(stderr, "%s: %s\n", argv[1], err); return 1; }
    {   /* the hand-off files, as tools/sweep_objectives.c writes them */
        static char dir[256];
        lance_slot fr[3], en[3];
        static const char *const FR[3] = {"timbrwlf", "maddog", "strmcrow"}, *const EN[3] = {"maddog", "strmcrow", "nova"};
        int m;
        snprintf(dir, sizeof dir, "%s/mw2auditXXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
        memset(fr, 0, sizeof fr); memset(en, 0, sizeof en);
        for (k = 0; k < 3; k++) {
            fr[k].mech = en[k].mech = -1;
            for (m = 0; m < lance_mech_count(); m++) {
                if (strcmp(lance_mech_file(m), FR[k]) == 0) fr[k].mech = m;
                if (strcmp(lance_mech_file(m), EN[k]) == 0) en[k].mech = m;
            }
        }
        if (lance_write_all(a, dir, fr, en, 1) != 0 || lance_write_instmap(a, dir, 1, 0) != 0) { fprintf(stderr, "lance files: failed\n"); return 1; }
        dp_add_root(dir);
        g_dir = dir;
        memcpy(g_fr, fr, sizeof g_fr);
    }
    dp_add_roots_from_env();
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) g_secs = (float)atof(argv[++i]);
        else if (nn < 512) names[nn++] = argv[i];
    }
    if (nn == 0) {   /* every *SCN1 record */
        int t = prj_find_type(a, "BWD");
        for (k = 0; t >= 0 && k < prj_symbol_count(a, t) && nn < 512; k++) {
            const char *nm = prj_symbol_name(a, t, k);
            size_t l = nm ? strlen(nm) : 0;
            if (l == 8 && strcasecmp(nm + 4, "SCN1") == 0) names[nn++] = nm;
        }
    } else single = nn == 1;
    for (i = 0; i < nn; i++) {
        pid_t p;
        int st = 0;
        if (single) { audit_scene(a, names[i]); continue; }
        fflush(stdout);
        p = fork();
        if (p == 0) { alarm(300); audit_scene(a, names[i]); fflush(stdout); _exit(0); }
        if (p < 0 || waitpid(p, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
            printf("FIND %-9s %-8s the audit's process ended abnormally (status %#x)\nRESULT %-9s findings 1\n", names[i], "CRASH", st, names[i]);
    }
    prj_close(a);
    {
        static const char *const F[7] = {"USERSTAR.BWD", "EN01STAR.BWD", "EN02STAR.BWD", "EN03STAR.BWD", "EN04STAR.BWD", "EN05STAR.BWD", "INSTMAP1.BWD"};
        char p2[512];
        for (k = 0; k < 7; k++) { snprintf(p2, sizeof p2, "%s/%s", g_dir, F[k]); remove(p2); }
        rmdir(g_dir);
    }
    return 0;
}
