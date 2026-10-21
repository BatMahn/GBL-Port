/*
 * gblshell - the MechWarrior 2: Ghost Bear's Legacy shell, rebuilt from GBL's MW2SHELL.EXE (1995 DOS expansion).
 *
 * Ghost Bear's Legacy shipped as its own program: GBL.EXE ran MW2SHELL.EXE (the menus) and MW2.EXE (the simulation) in a
 * loop. This is the port's equivalent: one executable "gbl" - the shell below, and the same simulation as MechWarrior 2
 * ("gbl --sim ...", tools/glview.c).
 *
 * The GBL shell is a modified copy of the base game's shell. This file includes the base shell (tools/mw2shell.c, in
 * library mode) for what GBL kept unchanged - fonts, text, dialogs, the cockpit controls page, the Mech Lab's editor
 * pages - and carries GBL's own screens and data. The specs it follows (addresses are GBL MW2SHELL.EXE VAs):
 *   docs/GBL_SHELL.md  (frame, DATABASE map, screens, files)
 * Screens (state machine FUN_00039ec0): 1 the Ghost Bear hall, 3 debriefing, 5 Mission Computer, 7 Combat Simulator,
 * 8 title, 9 Mech Lab, 10 launch, 12 Sibko roster, 15 (no video) -> 1, 16 Bloodname roll / "mend", 17 developer egg.
 *
 * Layout of the source:
 *   tools/gblshell.c          core: DATABASE numbering, frame / input, clip slots, hotspots, pilots, stars, launch, main
 *   tools/gbl/gbl_frame.c     title, global Esc menu and its pages (Combat Variables, Hall of Honor, The Keshik, Flee)
 *   tools/gbl/gbl_pilots.c    Sibko roster (12), the halls (1), Bloodname roll (16), developer egg (17), text pager
 *   tools/gbl/gbl_campaign.c  campaign tables, Mission Computer (5), mission clip lists, debriefing (3)
 *   tools/gbl/gbl_lab.c       Combat Simulator (7), Mech Lab (9)
 *
 * Tests (headless, SDL_VIDEODRIVER=offscreen): GBL_EVENTS="frame:cmd;..." drives the shell - cmd: m X Y (pointer),
 * c (left click), r (right click), k KEY (SDL key name), t TEXT, s FILE.ppm (screenshot of the frame), q (quit).
 * GBL_SCREEN=n starts on screen n (GBL_CTX context), GBL_PILOT=i preselects pilot record i. No effect when unset.
 */
#define MW2_SHELL_LIB
/* Files the shell writes (pilots, settings, the handoff) go to the install folder, never to the disc copy: GBL's
 * defaults (MW2SND.CFG, MW2PRM.CFG ...) are read from the disc's GBL\ folder while install/ may start empty, and
 * dp_fopen() would open an existing file where it found it. Reads are unchanged. */
#include <stdio.h>
#include "datapath.h"
static FILE *gbl_dp_fopen(const char *name, const char *mode);
#define dp_fopen gbl_dp_fopen
#include "mw2shell.c"
#undef dp_fopen
static FILE *gbl_dp_fopen(const char *name, const char *mode)
{
    char path[1400], alt[64];
    const char *base;
    size_t k;
    FILE *f;
    if (mode[0] != 'w' && mode[0] != 'a' && !strchr(mode, '+')) return dp_fopen(name, mode);
    if (!g_dir[0]) return dp_fopen(name, mode);
    for (base = name; strpbrk(base, "\\/"); base = strpbrk(base, "\\/") + 1) {}
    if (base != name) {   /* a subfolder (GIDDI\CONFIG00.CPC): the install's copy if it has that folder, else as before */
        snprintf(path, sizeof path, "%s/%.*s", g_dir, (int)(base - name - 1), name);
        for (k = strlen(g_dir); path[k]; k++) if (path[k] == '\\') path[k] = '/';
        if (!(f = fopen(path, "r"))) return dp_fopen(name, mode);   /* (a directory opens for reading on POSIX) */
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    for (k = strlen(g_dir); path[k]; k++) if (path[k] == '\\') path[k] = '/';
    if ((f = fopen(path, "rb"))) { fclose(f); return fopen(path, mode); }   /* exists in the install: as spelled */
    snprintf(alt, sizeof alt, "%s", name);
    for (k = 0; alt[k]; k++) alt[k] = (char)tolower((unsigned char)alt[k]);
    snprintf(path, sizeof path, "%s/%s", g_dir, alt);
    for (k = strlen(g_dir); path[k]; k++) if (path[k] == '\\') path[k] = '/';
    if ((f = fopen(path, "rb"))) { fclose(f); return fopen(path, mode); }   /* exists in lower case */
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    for (k = strlen(g_dir); path[k]; k++) if (path[k] == '\\') path[k] = '/';
    return fopen(path, mode);   /* new: the DOS spelling */
}
#define dp_fopen gbl_dp_fopen

/* ======================================================================================================================
 * DATABASE.MW2 (GBL: 127 entries). GBL kept the base entries 0-9 (title .. Mech Lab backgrounds and the panel SHPs)
 * and moved the rest: 23 backgrounds instead of 21, so the cursor, fonts, video DLL and songs are +2 (base 24-72 =
 * GBL 26-74), the first WAVs +2, 15 chassis voices inserted after the base's 17 (so base 99-102 = GBL 116-119), 7 new
 * WAVs at the end. g_db_remap maps the base numbers the reused base code asks for; GBL code uses GBL numbers.
 * (docs/GBL_SHELL.md "DATABASE.MW2") */
static int gbl_db_remap(int e)
{
    if (e >= 24 && e <= 98) return e + 2;
    if (e >= 99 && e <= 102) return e + 17;
    return e;
}
/* the getters are 1-based (FUN_00022920 / FUN_000229d0 read entry idx - 1) */
#define GDB(idx) ((idx) - 1)
static long gdb_entry(int entry, uint8_t **out) { return shelldb_entry(&g_db, entry, out); }

static shelldb g_dbsound;   /* DBSOUND.MW2: 22 briefing + 22 debriefing voice-overs (optional; only on the disc) */
static int g_have_dbsound;

/* GBL fonts by entry (lazily loaded): 28 (15 px), 29 (16 px), 31 / 32 (menu), 33 (10 px, the default) */
static font g_gfont[40];
static font *gfont(int entry)
{
    if (entry < 0 || entry >= 40) return &g_gfont[0];
    if (!g_gfont[entry].d) g_gfont[entry].len = gdb_entry(entry, &g_gfont[entry].d);
    return &g_gfont[entry];
}
#define GF_DEFAULT 33   /* idx 0x22: hotspot labels, dialogs, lists */

/* ======================================================================================================================
 * frame and input. One call per shell frame: draws the frame (with the queued text), reads the input. Windowed: about
 * 60 frames a second (vsync). Headless (no renderer): a virtual 30 Hz clock and the GBL_EVENTS script. */
typedef struct {
    int mx, my;          /* pointer, 640x480 space */
    int lclick, rclick;  /* a button went down this frame */
    int ldown, rdown;    /* held */
    int lup;             /* the left button went up this frame */
    SDL_Keycode key;     /* a key went down this frame (0 none) */
    char text[32];       /* typed text this frame */
    int quit;
} gbl_input;
static SDL_Renderer *g_grd;
static SDL_Texture *g_gtx;
static int g_headless;
static long g_frame_no;
static Uint32 g_now;     /* the shell's clock in ms (virtual when headless) */
static gbl_input g_in;   /* the last frame's input */
static int g_quit_all;   /* the window was closed */

static char g_grab[256];   /* TEST ONLY: a window grab requested by the event script */
static void gbl_shot(const char *path)
{
    FILE *o = fopen(path, "wb");
    if (o) { fprintf(o, "P6\n%d %d\n255\n", W, H); fwrite(g_fb, 1, sizeof g_fb, o); fclose(o); }
}
static SDL_Keycode gbl_keyname(const char *n) { SDL_Keycode k = SDL_GetKeyFromName(n); return k == SDLK_UNKNOWN && n[0] && !n[1] ? (SDL_Keycode)tolower((unsigned char)n[0]) : k; }
/* the scripted events for this frame (headless tests) */
static void gbl_script(gbl_input *in)
{
    const char *q = getenv("GBL_EVENTS");
    while (q && *q) {
        long fr = strtol(q, (char **)&q, 10);
        char cmd[256];
        int n = 0;
        if (*q == ':') q++;
        while (*q && *q != ';' && n < 255) cmd[n++] = *q++;
        cmd[n] = 0;
        if (*q == ';') q++;
        if (fr != g_frame_no) continue;
        switch (cmd[0]) {
        case 'm': sscanf(cmd + 1, "%d %d", &in->mx, &in->my); break;
        case 'c': in->lclick = 1; in->ldown = 1; break;
        case 'u': in->lup = 1; in->ldown = 0; break;
        case 'r': in->rclick = 1; in->rdown = 1; break;
        case 'k': { const char *a = cmd + 1; while (*a == ' ') a++; in->key = gbl_keyname(a); } break;
        case 't': { const char *a = cmd + 1; while (*a == ' ') a++; snprintf(in->text, sizeof in->text, "%.31s", a); } break;
        case 's': { const char *a = cmd + 1; while (*a == ' ') a++; gbl_shot(a); } break;
        case 'S': { const char *a = cmd + 1; while (*a == ' ') a++; snprintf(g_grab, sizeof g_grab, "%s", a); } break;
        case 'q': in->quit = 1; g_quit_all = 1; break;
        }
    }
}
/* text covers. Windowed, the text is not in the frame: it is queued and drawn over the frame at the display's resolution
 * (mw2shell.c draw_text). Whatever is painted into the frame over text queued before it - the Esc menu's panel over the
 * screen it opened on, a dialog, a filled box - must hide that text, or the screen's text shows through the panel (Mike's
 * report: the Mission Computer's briefing over the menu). gbl_text_cover(x0, y0, x1, y1) (x1 / y1 exclusive) drops the
 * queued items wholly inside the box and records the box for the items partly inside it, which gbl_present then draws
 * clipped to the area outside it. Text queued after the cover (the panel's own words) is not covered. The covers last
 * one frame (the screens repaint and cover again each pass). Headless the text is in the frame and nothing is needed. */
#define GT_COVERS 16
typedef struct { int x0, y0, x1, y1, n; } gtext_cover;   /* n: covers the queue items 0..n-1 */
static gtext_cover g_tcover[GT_COVERS];
static int g_ntcover;
static void gtext_box(const text_item *it, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = it->x; *y0 = it->y;
    *x1 = it->x + text_width(it->fo, it->t); *y1 = it->y + (it->fo->d ? it->fo->d[8] : 10);
}
static void gbl_text_cover(int x0, int y0, int x1, int y1)
{
    int i, k, n = 0, newn[GT_COVERS];
    if (!g_text_hires) return;
    for (k = 0; k < g_ntcover; k++) newn[k] = 0;
    for (i = 0; i < g_ntext; i++) {
        int a, b, c, d;
        gtext_box(&g_text[i], &a, &b, &c, &d);
        if (a >= x0 && b >= y0 && c <= x1 && d <= y1) continue;   /* wholly covered: gone */
        for (k = 0; k < g_ntcover; k++) if (i < g_tcover[k].n) newn[k]++;   /* the earlier covers' counts follow */
        g_text[n++] = g_text[i];
    }
    g_ntext = n;
    for (k = 0; k < g_ntcover; k++) g_tcover[k].n = newn[k];
    if (g_ntcover < GT_COVERS) {
        gtext_cover *c = &g_tcover[g_ntcover++];
        c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1; c->n = g_ntext;
    }
}
/* the text layer with the covers: the items no cover touches as usual, each partly covered one once per rectangle of
 * the screen outside its covers (SDL clip rectangles, in the 640x480 logical space) */
static void gbl_text_layer(SDL_Renderer *rd)
{
    static text_item all[256];
    int n = g_ntext, ntt = g_ntt, i, k, kept = 0;
    if (!g_ntcover) { text_layer_draw(rd); return; }
    for (k = 0; k < g_ntcover; k++) if (g_tcover[k].n > n) g_tcover[k].n = n;   /* the queue was cleared since */
    memcpy(all, g_text, sizeof all);
    for (i = 0; i < n; i++) {
        int a, b, c, d, hit = 0;
        gtext_box(&all[i], &a, &b, &c, &d);
        for (k = 0; k < g_ntcover && !hit; k++)
            hit = i < g_tcover[k].n && a < g_tcover[k].x1 && c > g_tcover[k].x0 && b < g_tcover[k].y1 && d > g_tcover[k].y0;
        if (!hit) g_text[kept++] = all[i];
    }
    g_ntext = kept;
    text_layer_draw(rd);   /* (and the TrueType items) */
    g_ntt = 0;
    for (i = 0; i < n; i++) {
        SDL_Rect r[64];
        int nr = 1, a, b, c, d, j;
        gtext_box(&all[i], &a, &b, &c, &d);
        r[0].x = a; r[0].y = b; r[0].w = c - a; r[0].h = d - b;
        for (k = 0; k < g_ntcover; k++) {   /* subtract each cover that applies to this item */
            const gtext_cover *cv = &g_tcover[k];
            SDL_Rect out[64];
            int no = 0;
            if (i >= cv->n) continue;
            for (j = 0; j < nr; j++) {
                int rx0 = r[j].x, ry0 = r[j].y, rx1 = r[j].x + r[j].w, ry1 = r[j].y + r[j].h;
                int ix0 = rx0 > cv->x0 ? rx0 : cv->x0, iy0 = ry0 > cv->y0 ? ry0 : cv->y0;
                int ix1 = rx1 < cv->x1 ? rx1 : cv->x1, iy1 = ry1 < cv->y1 ? ry1 : cv->y1;
                if (ix0 >= ix1 || iy0 >= iy1) { if (no < 64) out[no++] = r[j]; continue; }
                if (iy0 > ry0 && no < 64) { SDL_Rect q = {rx0, ry0, rx1 - rx0, iy0 - ry0}; out[no++] = q; }   /* above */
                if (ry1 > iy1 && no < 64) { SDL_Rect q = {rx0, iy1, rx1 - rx0, ry1 - iy1}; out[no++] = q; }   /* below */
                if (ix0 > rx0 && no < 64) { SDL_Rect q = {rx0, iy0, ix0 - rx0, iy1 - iy0}; out[no++] = q; }   /* left */
                if (rx1 > ix1 && no < 64) { SDL_Rect q = {ix1, iy0, rx1 - ix1, iy1 - iy0}; out[no++] = q; }   /* right */
            }
            memcpy(r, out, sizeof(SDL_Rect) * (size_t)no);
            nr = no;
        }
        if (nr == 1 && r[0].x == a && r[0].y == b && r[0].w == c - a && r[0].h == d - b) continue;   /* untouched: drawn */
        for (j = 0; j < nr; j++) {
            if (r[j].w <= 0 || r[j].h <= 0) continue;
            SDL_RenderSetClipRect(rd, &r[j]);
            g_text[0] = all[i]; g_ntext = 1;
            text_layer_draw(rd);
        }
    }
    SDL_RenderSetClipRect(rd, NULL);
    memcpy(g_text, all, sizeof all);
    g_ntext = n; g_ntt = ntt;
}
static SDL_Texture *g_gvid_tx;   /* gbl_video: the frame in the videos' look (video_fx_texture), presented instead of g_fb */
static void gbl_present(void)
{
    if (!g_grd) return;
    if (!g_gvid_tx) SDL_UpdateTexture(g_gtx, NULL, g_fb, W * 3);
    SDL_RenderClear(g_grd); SDL_RenderCopy(g_grd, g_gvid_tx ? g_gvid_tx : g_gtx, NULL, NULL); gbl_text_layer(g_grd);
    g_ntcover = 0;
    if (g_grab[0]) {   /* TEST ONLY (GBL_EVENTS "S file.ppm", windowed): the rendered window with its text layer */
        int ow, oh;
        float sx, sy;
        uint8_t *buf;
        SDL_RenderGetScale(g_grd, &sx, &sy);   /* (a NULL rect reads the viewport: the 640x480 picture, scaled) */
        ow = (int)(W * sx); oh = (int)(H * sy);
        if ((buf = malloc((size_t)ow * (size_t)oh * 3))) {
            if (SDL_RenderReadPixels(g_grd, NULL, SDL_PIXELFORMAT_RGB24, buf, ow * 3) == 0) {
                FILE *o = fopen(g_grab, "wb");
                if (o) { fprintf(o, "P6\n%d %d\n255\n", ow, oh); fwrite(buf, 1, (size_t)ow * (size_t)oh * 3, o); fclose(o); }
            }
            free(buf);
        }
        g_grab[0] = 0;
    }
    SDL_RenderPresent(g_grd);
}
/* present the frame built in g_fb, then gather the next frame's input into g_in (and return it) */
static gbl_input *gbl_frame(void)
{
    gbl_input *in = &g_in;
    int mx = in->mx, my = in->my, ld = in->ldown, rdn = in->rdown;
    gbl_present();
    memset(in, 0, sizeof *in);
    in->mx = mx; in->my = my; in->ldown = ld; in->rdown = rdn;
    g_frame_no++;
    if (g_headless) {
        g_now = (Uint32)(g_frame_no * 1000 / 30);
        if (in->lup) in->ldown = 0;
        gbl_script(in);
        if (!in->lclick && ld) { in->ldown = 0; in->lup = 1; }   /* a scripted click is one frame long */
        if (!in->rclick) in->rdown = 0;
        if (getenv("GBL_MAX_FRAMES") && g_frame_no > atol(getenv("GBL_MAX_FRAMES"))) { in->quit = 1; g_quit_all = 1; }
        return in;
    }
    {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: in->quit = 1; g_quit_all = 1; break;
            case SDL_MOUSEMOTION: in->mx = e.motion.x; in->my = e.motion.y; break;
            case SDL_MOUSEBUTTONDOWN:
                in->mx = e.button.x; in->my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in->lclick = 1; in->ldown = 1; }
                if (e.button.button == SDL_BUTTON_RIGHT) { in->rclick = 1; in->rdown = 1; }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) { in->ldown = 0; in->lup = 1; }
                if (e.button.button == SDL_BUTTON_RIGHT) in->rdown = 0;
                break;
            case SDL_KEYDOWN: if (!in->key) in->key = e.key.keysym.sym; break;
            case SDL_TEXTINPUT: if (strlen(in->text) + strlen(e.text.text) < sizeof in->text) strcat(in->text, e.text.text); break;
            }
        }
    }
    if (getenv("GBL_EVENTS")) {   /* TEST ONLY: the event script also drives a real window (one script frame per frame,
                                   * paced to 30 a second as the headless clock) */
        static Uint32 t_last;
        int ld2 = in->ldown;
        Uint32 t = SDL_GetTicks();
        if (t_last && t - t_last < 33) SDL_Delay(33 - (t - t_last));
        t_last = SDL_GetTicks();
        gbl_script(in);
        if (!in->lclick && ld2 && !SDL_GetMouseState(NULL, NULL)) { in->ldown = 0; in->lup = 1; }
    }
    if (in->mx < 0) in->mx = 0;
    if (in->my < 0) in->my = 0;
    if (in->mx >= W) in->mx = W - 1;
    if (in->my >= H) in->my = H - 1;
    g_now = SDL_GetTicks();
    return in;
}

/* ======================================================================================================================
 * backgrounds (FUN_00040bb0(screen, db, idx): idx 1-based) and the palette */
static void gbl_bg(int idx) { load_bg(GDB(idx)); }
static const uint8_t (*gbl_pal(void))[3] { return (const uint8_t (*)[3])g_bg.pal; }

/* ======================================================================================================================
 * sounds (FUN_00022920 + FUN_0003db50 make, FUN_0003def0 volume 0-127, FUN_0003dd40 loop, FUN_0003dd70 play): DATABASE
 * WAVs by the 1-based getter index; DBSOUND.MW2 voice-overs likewise. Loops are the screen's ambience: gsnd_stop_all() on
 * leaving a screen. */
typedef struct { uint8_t *wav; const uint8_t *pcm; int len, rate; } gsound;
static gsound g_gsnd[200], g_gvoice[64];
static int wav_parse(gsound *z, long n)
{
    long i;
    for (i = 12; z->wav && i + 8 <= n; ) {
        uint32_t cl = z->wav[i + 4] | z->wav[i + 5] << 8 | z->wav[i + 6] << 16 | (uint32_t)z->wav[i + 7] << 24;
        if (!memcmp(z->wav + i, "data", 4)) {
            z->rate = z->wav[24] | z->wav[25] << 8;
            z->pcm = z->wav + i + 8;
            z->len = (int)(cl < (uint32_t)(n - i - 8) ? cl : (uint32_t)(n - i - 8));
            return 0;
        }
        i += 8 + (long)cl + (cl & 1);
    }
    return -1;
}
static gsound *gsnd_get(int idx, int voice)
{
    gsound *z;
    if (voice) { if (!g_have_dbsound || idx <= 0 || idx > 64) return NULL; z = &g_gvoice[idx - 1]; }
    else { if (idx <= 0 || idx > 200) return NULL; z = &g_gsnd[idx - 1]; }
    if (!z->wav) {
        long n = shelldb_entry(voice ? &g_dbsound : &g_db, idx - 1, &z->wav);
        if (n > 0) wav_parse(z, n);
    }
    return z->pcm && z->len > 0 ? z : NULL;
}
/* play once (restarting it if it is playing) at volume 0-127; loop = 1: repeat until stopped */
static void gsnd_play(int idx, int vol, int loop)
{
    gsound *z = gsnd_get(idx, 0);
    if (!z || !g_sfx || g_headless) return;
    sfx_stop_pcm(g_sfx, z->pcm);
    sfx_play_pcm(g_sfx, z->pcm, z->len, z->rate, (float)vol / 127.0f, loop);
}
static void gsnd_stop(int idx) { gsound *z = idx > 0 && idx <= 200 ? &g_gsnd[idx - 1] : NULL; if (z && z->pcm && g_sfx) sfx_stop_pcm(g_sfx, z->pcm); }
static void gvoice_play(int idx, int vol)   /* DBSOUND.MW2 (missing: no voice-over, as the original) */
{
    gsound *z = gsnd_get(idx, 1);
    if (!z || !g_sfx || g_headless) return;
    sfx_stop_pcm(g_sfx, z->pcm);
    sfx_play_pcm(g_sfx, z->pcm, z->len, z->rate, (float)vol / 127.0f, 0);
}
static void gvoice_stop(int idx) { gsound *z = idx > 0 && idx <= 64 ? &g_gvoice[idx - 1] : NULL; if (z && z->pcm && g_sfx) sfx_stop_pcm(g_sfx, z->pcm); }
static void gsnd_stop_all(void) { if (g_sfx) { sfx_stop_loops(g_sfx); } }

/* ======================================================================================================================
 * clip slots (FUN_0003d1a0(slot, name, x, y, flags, fps) and friends). A slot holds a Smacker clip or an SHP still /
 * button (smk\<name>.shp first, then .smk; install folder first, then the disc). fps 0 = 10 frames a second.
 * Flags (as the original uses them): 1 paused, 2 the background shows again under it when it is hidden / ends, 4 hold
 * the last frame, 8 loop, 0x20 hidden, 0x40 (always set by the callers), 0x200 at the end rewind to frame 1 and pause.
 * A clip without 8 / 0x200 that reaches its end stops: it keeps showing its last frame unless flag 2 is set and 4 is not.
 * 0x80: (x, y) is the clip's bottom centre (FUN_0003d0c0 / FUN_0003cf70: x -= width / 2, y -= height; an SHP's size is
 * its first shape's, FUN_0004aa43). Multi-frame SHP clips animate like the Smacker ones (FUN_0003ca50 0x3cb3b: one frame
 * a pass when the clip's period has passed - the next time is now + period, no catching up; at the end 8 loops to frame
 * 0, otherwise the clip stops on its last frame; 0x200 is Smacker-only). An empty SHP shape (left / top 0x7fffffff, e.g.
 * wgbcs008 frame 0) draws nothing.
 * gs_done() is the original's FUN_0003cd80 == 0 (the clip has ended). gs_show_frame() picks a frame of a button SHP
 * (FUN_0003d7c0: 0 idle, 1 pressed, 2 hover). */
#define GS_SLOTS 40
enum { GS_PAUSED = 1, GS_RESTORE = 2, GS_HOLD = 4, GS_LOOP = 8, GS_HIDDEN = 0x20, GS_BOTTOM = 0x80, GS_REWIND = 0x200 };
typedef struct {
    int used, x, y, flags, ms;
    smk *s;
    uint8_t *shp; size_t shplen; int nfr, cur; shp_frame fr; int have_fr;
    Uint32 last;
    int ended, frame;
    char name[16];
} gslot;
static gslot g_gs[GS_SLOTS];
static const uint8_t (*g_gs_pal)[3];   /* palette for SHP clips: NULL = the background's */
static FILE *gbl_open_smkdir(const char *name, const char *ext, char *path, size_t n)
{
    char dn[64];
    FILE *f;
    snprintf(dn, sizeof dn, "SMK\\%s.%s", name, ext);
    snprintf(path, n, "%s", gpath(dn));
    f = fopen(path, "rb");
    return f;
}
static void gs_close(int i)
{
    gslot *g;
    if (i < 0 || i >= GS_SLOTS) return;
    g = &g_gs[i];
    if (g->s) smk_close(g->s);
    if (g->have_fr) shp_frame_free(&g->fr);
    free(g->shp);
    memset(g, 0, sizeof *g);
}
static void gs_close_all(void) { int i; for (i = 0; i < GS_SLOTS; i++) gs_close(i); }
static int gs_shp_frame(gslot *g, int k)
{
    if (!g->shp) return -1;
    if (k < 0) k = 0;
    if (k >= g->nfr) k = g->nfr - 1;
    if (g->have_fr && g->cur == k) return 0;
    if (g->have_fr) { shp_frame_free(&g->fr); g->have_fr = 0; }
    if (shp_decode(g->shp, g->shplen, k, &g->fr) != 0) return -1;
    if (g->fr.left > 4096 || g->fr.top > 4096 || g->fr.left < -4096 || g->fr.top < -4096) {   /* an empty shape */
        memset(g->fr.mask, 0, (size_t)g->fr.w * (size_t)g->fr.h);
        g->fr.left = g->fr.top = 0;
    }
    g->have_fr = 1; g->cur = k;
    return 0;
}
/* load a clip into a slot; returns 0 if the file was there (a missing file leaves the slot empty, as the original) */
static int gs_load(int i, const char *name, int x, int y, int flags, int fps)
{
    gslot *g;
    char path[1200];
    FILE *f;
    if (i < 0 || i >= GS_SLOTS) return -1;
    gs_close(i);
    if (!name || !name[0]) return -1;
    g = &g_gs[i];
    g->x = x; g->y = y; g->flags = flags; g->ms = fps > 0 ? 1000 / fps : 100;
    snprintf(g->name, sizeof g->name, "%s", name);
    if ((f = gbl_open_smkdir(name, "SHP", path, sizeof path))) {
        long n;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0 && (g->shp = malloc((size_t)n)) && fread(g->shp, 1, (size_t)n, f) == (size_t)n) {
            g->shplen = (size_t)n;
            g->nfr = shp_count(g->shp, g->shplen);
            if (g->nfr <= 0 || gs_shp_frame(g, 0) != 0) { free(g->shp); g->shp = NULL; }
        } else { free(g->shp); g->shp = NULL; }
        fclose(f);
    }
    if (!g->shp) {
        gbl_open_smkdir(name, "SMK", path, sizeof path);
        g->s = smk_open_file(path);
        if (!g->s) { memset(g, 0, sizeof *g); return -1; }
        smk_next(g->s);
        g->nfr = smk_frames(g->s);
    }
    if (flags & GS_BOTTOM) {   /* the position is the clip's bottom centre */
        int w = 0, h = 0;
        if (g->shp && g->shplen >= 16) {   /* the first shape's header: u16 height - 1, u16 width - 1 */
            uint32_t off = g->shp[8] | g->shp[9] << 8 | g->shp[10] << 16 | (uint32_t)g->shp[11] << 24;
            if (off + 4 <= g->shplen) { h = (g->shp[off] | g->shp[off + 1] << 8) + 1; w = (g->shp[off + 2] | g->shp[off + 3] << 8) + 1; }
        } else if (g->s) { w = smk_width(g->s); h = smk_height(g->s); }
        g->x -= w / 2; g->y -= h;
    }
    g->used = 1; g->last = g_now; g->frame = 0; g->ended = 0;
    return 0;
}
static int gs_loaded(int i) { return i >= 0 && i < GS_SLOTS && g_gs[i].used; }
static void gs_flags(int i, int bits, int on) { if (gs_loaded(i)) { if (on) g_gs[i].flags |= bits; else g_gs[i].flags &= ~bits; } }
static int gs_done(int i) { return !gs_loaded(i) || g_gs[i].ended; }
static void gs_restart(int i)
{
    gslot *g;
    if (!gs_loaded(i)) return;
    g = &g_gs[i];
    if (g->s) { smk_rewind(g->s); smk_next(g->s); }
    if (g->shp) gs_shp_frame(g, 0);
    g->frame = 0; g->ended = 0; g->last = g_now;
    g->flags &= ~(GS_PAUSED | GS_HIDDEN);
}
static void gs_show_frame(int i, int k)   /* a button: show frame k, held */
{
    gslot *g;
    if (!gs_loaded(i)) return;
    g = &g_gs[i];
    if (g->shp) { gs_shp_frame(g, k); g->frame = g->cur; }
    else if (g->s && k != g->frame) { smk_rewind(g->s); smk_next(g->s); g->frame = 0; while (g->frame < k && g->frame + 1 < g->nfr) { smk_next(g->s); g->frame++; } }
    g->flags |= GS_PAUSED;
}
/* advance a slot by the clock */
static void gs_tick(int i)
{
    gslot *g = &g_gs[i];
    if (!g->used || g->ended || (g->flags & GS_PAUSED)) return;
    if (g->shp) {   /* an SHP clip (0x3cb3b): at most one frame a pass */
        if (g->nfr <= 1 || g_now - g->last < (Uint32)g->ms) return;
        g->last = g_now;
        if (g->frame + 1 >= g->nfr) {
            if (g->flags & GS_LOOP) g->frame = 0;
            else { g->ended = 1; return; }
        } else g->frame++;
        gs_shp_frame(g, g->frame);
        return;
    }
    if (!g->s) return;
    while (g_now - g->last >= (Uint32)g->ms) {
        g->last += (Uint32)g->ms;
        if (g->frame + 1 >= g->nfr) {
            if (g->flags & GS_LOOP) { smk_rewind(g->s); smk_next(g->s); g->frame = 0; continue; }
            if (g->flags & GS_REWIND) { smk_rewind(g->s); smk_next(g->s); g->frame = 0; g->flags |= GS_PAUSED; g->ended = 1; break; }
            g->ended = 1;
            break;
        }
        smk_next(g->s);
        g->frame++;
        if (g_headless) { g->last = g_now; break; }
    }
}
static void gs_blit(int i)
{
    gslot *g = &g_gs[i];
    if (!g->used || (g->flags & GS_HIDDEN)) return;
    if (g->ended && !(g->flags & (GS_LOOP | GS_REWIND)) && (g->flags & GS_RESTORE) && !(g->flags & GS_HOLD)) return;
    if (g->s) { blit_smk(g->s, g->x, g->y); return; }
    if (g->have_fr) {
        int X, Y;
        const uint8_t (*pal)[3] = g_gs_pal ? g_gs_pal : gbl_pal();
        for (Y = 0; Y < g->fr.h; Y++)
            for (X = 0; X < g->fr.w; X++) {
                int px = g->x + g->fr.left + X, py = g->y + g->fr.top + Y;
                if (!g->fr.mask[Y * g->fr.w + X] || px < 0 || py < 0 || px >= W || py >= H) continue;
                memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, pal[g->fr.pix[Y * g->fr.w + X]], 3);
            }
    }
}
/* advance and draw every slot, in slot order (the original draws its slot list in order) */
static void gs_update(void) { int i; for (i = 0; i < GS_SLOTS; i++) { gs_tick(i); gs_blit(i); } }
/* play a slot to its end, everything else frozen (FUN_0003d410: the blocking play, e.g. the hall's MISSION COMPUTER click) */
static void gs_play_to_end(int i)
{
    if (!gs_loaded(i)) return;
    gs_restart(i);
    g_gs[i].flags &= ~(GS_LOOP | GS_REWIND);
    while (!gs_done(i) && !g_quit_all) {
        gbl_input *in;
        gs_tick(i);
        gs_blit(i);
        in = gbl_frame();
        if (in->quit) break;
    }
}

/* full-screen video (FUN_0003c430: 320x200 SMK scaled; a key or a mouse button skips; returns 1 if skipped). The
 * original sets mode 13h for it (FUN_0004e4c8(200, 320) 0x3c547: the first mode table entry holding 320x200, BIOS mode
 * 0x13) and copies each frame 1:1 (SmackToScreen at 0, 0, 0x3c56b); the clip opened with only its sound-track flags
 * (0x95ecc = 0xfe00). The look: video_look() (mw2shell.c; ORIGINAL = the 640x480 stretch in g_fb) */
static int gbl_video(const char *name)
{
    char path[1200];
    smk *s;
    int f, n, skipped = 0, look = video_look();
    unsigned char *snd = NULL;
    Uint32 t0;
    if (!name || !name[0]) return 0;
    gbl_open_smkdir(name, "SMK", path, sizeof path);
    if (!(s = smk_open_file(path))) return 0;
    n = g_headless ? video_headless_frames(2) : smk_frames(s);
    if (!g_headless) {
        int16_t *a = NULL;
        size_t ns = 0;
        int rate = 0, ch = 1;
        if (smk_audio(s, &a, &ns, &rate, &ch) == 0 && ns > 0 && (snd = malloc(ns / (size_t)ch + 1))) {
            size_t i;
            for (i = 0; i < ns / (size_t)ch; i++) {
                int v = ch == 2 ? ((int)a[2 * i] + (int)a[2 * i + 1]) / 2 : a[i];
                snd[i] = (unsigned char)((v >> 8) + 128);
            }
            sfx_play_pcm(g_sfx, snd, (int)(ns / (size_t)ch), rate, 1.0f, 0);
        }
        free(a);
    }
    t0 = SDL_GetTicks();
    for (f = 0; f < n && !skipped; f++) {
        int x, y, sw, sh;
        gbl_input *in;
        smk_next(s);
        sw = smk_width(s); sh = smk_height(s);
        if (!g_headless && (double)(SDL_GetTicks() - t0) > (double)(f + 2) * smk_frame_ms(s) && f + 1 < n) continue;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int sx = x * sw / W, sy = y * sh / H;
                memcpy(g_fb + ((size_t)y * W + (size_t)x) * 3, smk_palette(s)[smk_pixels(s)[(size_t)sy * (size_t)sw + (size_t)sx]], 3);
            }
        text_layer_clear();
        g_gvid_tx = video_fx_texture(g_grd, look, s);
        in = gbl_frame();
        g_gvid_tx = NULL;
        if (in->quit || in->lclick || in->rclick || in->key) skipped = 1;
        if (!g_headless) {
            Uint32 now = SDL_GetTicks() - t0;
            if ((double)now < (double)(f + 1) * smk_frame_ms(s)) SDL_Delay((Uint32)((double)(f + 1) * smk_frame_ms(s) - (double)now));
        }
    }
    if (g_headless) video_fx_shot(look, s);
    if (snd) { sfx_stop_pcm(g_sfx, snd); free(snd); }
    smk_close(s);
    return skipped;
}

/* ======================================================================================================================
 * text. Labels: '~' centres on x, '<' (hotspot labels) = always drawn. remap: the palette index glyph colour 1 becomes
 * (-1 = the font's own). */
static int gtext_w(int fe, const char *t) { return text_width(gfont(fe), t); }
static int gtext_h(int fe) { return font_height(gfont(fe)); }
static void gtext(int fe, int x, int y, const char *t, int remap)
{
    while (*t == '<' || *t == '~') {
        if (*t == '~') x -= gtext_w(fe, t + 1) / 2;
        t++;
    }
    draw_text(gfont(fe), x, y, t, gbl_pal(), remap);
}

/* ======================================================================================================================
 * hotspots (FUN_0002bce0 make / FUN_0002bf30 test: x0 <= x <= x1 and y0 <= y <= y1; a -1 rectangle never hits) */
typedef struct { int x0, y0, x1, y1, lx, ly; const char *text; } gbl_hot;
static int gh_hit(const gbl_hot *h, int n, const int *enabled, int x, int y)
{
    int i;
    for (i = 0; i < n; i++) {
        if (enabled && !enabled[i]) continue;
        if (h[i].x0 < 0) continue;
        if (x >= h[i].x0 && x <= h[i].x1 && y >= h[i].y0 && y <= h[i].y1) return i;
    }
    return -1;
}
/* the labels: '<' ones always, the others only under the pointer; col_idle / col_hover = palette index for glyph
 * colour 1 (-1 = the font's own) */
static void gh_labels(const gbl_hot *h, int n, const int *enabled, int hover, int fe, int col_idle, int col_hover)
{
    int i;
    for (i = 0; i < n; i++) {
        const char *t = h[i].text;
        if (!t || !t[0] || (enabled && !enabled[i])) continue;
        if (t[0] != '<' && i != hover) continue;
        gtext(fe, h[i].lx, h[i].ly, t, i == hover ? col_hover : col_idle);
    }
}

/* ======================================================================================================================
 * the pointer: DATABASE idx 0x1b (entry 26, an SHP) drawn at the mouse position in the frame (DOS draws its own) */
static shp_frame g_cursor;
static int g_have_cursor;
static void gbl_cursor_load(void)
{
    uint8_t *d = NULL;
    long n = gdb_entry(GDB(0x1b), &d);
    if (n > 0 && shp_decode(d, (size_t)n, 0, &g_cursor) == 0) g_have_cursor = 1;
    free(d);
}
/* the pixels under the pointer drawn last (headless): a screen keeps the presented frame as the picture under a panel
 * (the Esc menu, a dialog) - gbl_cursor_unsave() takes the pointer of the previous frame out of such a copy */
static uint8_t g_cur_under[96 * 96 * 3];
static int g_cur_ux, g_cur_uy, g_cur_uw, g_cur_uh;
static long g_cur_uframe = -10;
static void gbl_draw_cursor(int x, int y)
{
    int X, Y;
    g_cur_uw = 0;
    if (!g_have_cursor || !g_headless) return;   /* windowed: the system pointer (SDL) */
    if (g_cursor.w <= 96 && g_cursor.h <= 96) {
        g_cur_ux = x + g_cursor.left; g_cur_uy = y + g_cursor.top; g_cur_uw = g_cursor.w; g_cur_uh = g_cursor.h;
        for (Y = 0; Y < g_cur_uh; Y++)
            for (X = 0; X < g_cur_uw; X++) {
                int px = g_cur_ux + X, py = g_cur_uy + Y;
                if (px < 0 || py < 0 || px >= W || py >= H) continue;
                memcpy(g_cur_under + ((size_t)Y * 96 + (size_t)X) * 3, g_fb + ((size_t)py * W + (size_t)px) * 3, 3);
            }
        g_cur_uframe = g_frame_no;
    }
    for (Y = 0; Y < g_cursor.h; Y++)
        for (X = 0; X < g_cursor.w; X++) {
            int px = x + g_cursor.left + X, py = y + g_cursor.top + Y;
            if (!g_cursor.mask[Y * g_cursor.w + X] || px < 0 || py < 0 || px >= W || py >= H) continue;
            memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, gbl_pal()[g_cursor.pix[Y * g_cursor.w + X]], 3);
        }
}
/* put the pixels under the last presented frame's pointer back into fb (a copy of that frame; no-op windowed) */
static void gbl_cursor_unsave(uint8_t *fb)
{
    int X, Y;
    if (g_cur_uframe != g_frame_no - 1 || !g_cur_uw) return;
    for (Y = 0; Y < g_cur_uh; Y++)
        for (X = 0; X < g_cur_uw; X++) {
            int px = g_cur_ux + X, py = g_cur_uy + Y;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            memcpy(fb + ((size_t)py * W + (size_t)px) * 3, g_cur_under + ((size_t)Y * 96 + (size_t)X) * 3, 3);
        }
}

/* ======================================================================================================================
 * pilots: MW2REG.CFG, 20 records x 0x20e bytes, read / written raw (FUN_0001dfe0 load, FUN_0001e1c0 save). Records
 * 0-9 the Sibko roster, 10-19 the TSENG BLOODNAME roll. The star block (0xd4 bytes) is the shell's star record. */
#pragma pack(push, 1)
typedef struct {                 /* a star slot, 0x40 bytes */
    int32_t mech;                /* +0 index into the mech table (GBL_MECHS), -1 empty */
    char loadout[16];            /* +4 MEK record, "grz00std" */
    char name1[22];              /* +0x14 pilot name */
    char name2[22];              /* +0x2a second name ("Starmate 2") */
} gbl_slot;
typedef struct {                 /* a star (lance) record, 0xd4 bytes (0x856b0 friendly, 0x85784 enemy) */
    int32_t formation;           /* +0 0-5 echelonl echelonr lineabreast lineastern vform wedge */
    int32_t cur;                 /* +4 slot being edited */
    int32_t max;                 /* +8 max size (<= 3) */
    int32_t size;                /* +0xc */
    int32_t tons;                /* +0x10 tonnage limit */
    gbl_slot slot[3];            /* +0x14 */
} gbl_star;
typedef struct {                 /* MW2REG.CFG record, 0x20e bytes */
    int32_t in_use, selected, campaign, done, rank, honor, kills, hits, shots, unused24;
    char display[21];            /* 0x28: name, or "name %02d" (failures) - rebuilt on load */
    char name[21];               /* 0x3d: as typed, upper case; the cheat names are compared with this */
    int32_t deaths;              /* 0x52 failed missions, career */
    int32_t cur_fail;            /* 0x56 failures of the current mission */
    int32_t hall;                /* 0x5a location: which hall screen 1 shows (0 ice cave, 1 DropShip, 2 barracks) */
    gbl_star star;               /* 0x5e the friendly star, saved on every save */
    gbl_star ia_star;            /* 0x132 the campaign star while in the Combat Simulator */
    int32_t slot_limit;          /* 0x206 highest starmate slot in use (Mission Computer) */
    int32_t runtime;             /* 0x20a (runtime garbage in the original's file) */
} gbl_pilot;
#pragma pack(pop)
typedef char gbl_pilot_size_check[sizeof(gbl_pilot) == 0x20e && sizeof(gbl_star) == 0xd4 ? 1 : -1];
static gbl_pilot g_pil[20];
static int g_cur = -1;           /* the active pilot (DAT_000987e8), -1 none */
static gbl_star g_gstar[2];      /* 0 friendly (0x856b0), 1 enemy (0x85784) */
static int g_gstar_cur;          /* the current star record (0x85858) */

/* cheat names (0x7e80c: XOR 7, then upper-cased at start-up) */
enum { CH_DOOMSDAY, CH_LOVECRAFT, CH_RVWINDOW, CH_IMMORTAL, CH_TOLKEIN, CH_WIDOW, CH_LITTLEGUY, CH_THEWATCHER };
static const char *const GBL_CHEATS[8] = {"DOOMSDAY", "LOVECRAFT", "RVWINDOW", "IMMORTAL", "TOLKEIN", "WIDOW", "LITTLEGUY", "THEWATCHER"};
static int g_cheat_nuke, g_cheat_torpedo, g_cheat_rvwindow;   /* 0xa9bd0, 0xa9bd4, 0xadcec */

static void pil_rebuild(gbl_pilot *p)
{
    if (!p->in_use) return;
    p->runtime = 0;
    if (p->deaths) snprintf(p->display, sizeof p->display, "%.17s %02d", p->name, (int)p->deaths);
    else snprintf(p->display, sizeof p->display, "%s", p->name);
}
static void pil_cheats(void)   /* FUN_0001dfe0: any roster pilot named DOOMSDAY / LOVECRAFT / RVWINDOW switches its flag on */
{
    int i;
    for (i = 0; i < 10; i++) {
        if (!g_pil[i].in_use) continue;
        if (!strcmp(g_pil[i].name, GBL_CHEATS[CH_DOOMSDAY])) g_cheat_nuke = 1;
        if (!strcmp(g_pil[i].name, GBL_CHEATS[CH_LOVECRAFT])) g_cheat_torpedo = 1;
        if (!strcmp(g_pil[i].name, GBL_CHEATS[CH_RVWINDOW])) g_cheat_rvwindow = 1;
    }
}
static void pil_load(void)
{
    FILE *f = dp_fopen("MW2REG.CFG", "rb");
    int i;
    memset(g_pil, 0, sizeof g_pil);
    if (!f || fread(g_pil, sizeof g_pil[0], 20, f) != 20) {   /* missing: 20 default records, +8 = 1 for 10-19 */
        memset(g_pil, 0, sizeof g_pil);
        for (i = 10; i < 20; i++) g_pil[i].campaign = 1;
    }
    if (f) fclose(f);
    for (i = 0; i < 20; i++) pil_rebuild(&g_pil[i]);
    pil_cheats();
}
static void pil_save(void)
{
    FILE *f;
    if (g_cur >= 0 && g_cur < 20) g_pil[g_cur].star = g_gstar[0];   /* FUN_0001e1c0: memcpy(pilot + 0x5e, star 0, 212) */
    f = dp_fopen("MW2REG.CFG", "wb");
    if (!f) { fprintf(stderr, "Error Opening Career File\n"); return; }
    fwrite(g_pil, sizeof g_pil[0], 20, f);
    fclose(f);
}
static gbl_pilot *pil(void) { return g_cur >= 0 && g_cur < 20 ? &g_pil[g_cur] : NULL; }

/* ======================================================================================================================
 * the mech table 0x84a20 (32 x 0x18): picture code (ashmp<pic>, agbcs<pic>, wgbmc<pic>), archive code, file stem, name
 * as stored (padded), tons, chassis voice (DATABASE idx, -1 none). Rows 24-28 Inner Sphere ("I.S. - %s"). */
typedef struct { const char *pic, *code, *file, *name; int tons, voice; } gbl_mech;
static const gbl_mech GBL_MECHS[32] = {
 {"fm","frm","firemoth","Firemoth           ",20, 87}, {"vx","vxn","vixen","Incubus             ",30,102},
 {"kf","ktf","kitfox","Kit Fox              ",30, 91}, {"pg","prg","peregrin","Horned Owl     ",35,103},
 {"jn","jnr","jenner","Jenner IIC          ",35, 90},  {"pt","ptm","phantom","Phantom           ",40,104},
 {"nv","nva","nova","Nova                 ",50, 94},   {"sc","stm","strmcrow","Stormcrow       ",55, 96},
 {"md","mdg","maddog","Mad Dog          ",60, 92},     {"hb","hlb","hellbrgr","Hellbringer        ",65, 89},
 {"lb","lbk","lineback","LineBacker        ",65,105},  {"rf","rfl","rifleman","Rifleman IIC      ",65, 95},
 {"gr","grz","grizzly","Grizzly              ",70,106},{"su","smn","summoner","Summoner       ",70, 97},
 {"tw","tbr","timbrwlf","Timber Wolf    ",75, 98},     {"gg","grg","gargoyle","Gargoyle          ",80, 88},
 {"ng","nga","naga","Naga                 ",80,107},   {"wh","whm","warhammr","Warhammer    ",80, 99},
 {"mr","mrd","marauder","Marauder IIC    ",85, 93},    {"wk","whk","warhawk","Warhawk         ",85,100},
 {"ex","exe","executnr","Executioner       ",95,108},  {"dw","drw","direwolf","Dire Wolf         ",100,85},
 {"kd","kdk","kodiak","Kodiak               ",100,109},{"bh","bhm","behemoth","Stone Rhino      ",100,110},
 {"rv","rvn","raven","Raven                ",35,111},  {"hc","hat","hatamoto","Hatamoto-Chi      ",80,112},
 {"vt","vct","victor","Victor               ",80,113}, {"an","ani","anihilat","Anihilator           ",100,114},
 {"at","atl","atlas","Atlas                   ",100,115},
 {"el","ele","elementl","Elemental",100,86}, {"tr","tar","tarantul","Tarantula",100,-1}, {"bm","btm","bttlmstr","Battle Master IIC",100,101},
};
/* the name as shown: cut at the first double space (the table pads with spaces) */
static const char *gmech_name(int i)
{
    static char b[4][40];
    static int k;
    char *o = b[k = (k + 1) & 3], *d;
    if (i < 0 || i >= 32) return "None";
    snprintf(o, 40, "%s", GBL_MECHS[i].name);
    if ((d = strstr(o, "  "))) *d = 0;
    while (*o && o[strlen(o) - 1] == ' ') o[strlen(o) - 1] = 0;
    return o;
}
static int gmech_by_code(const char *loadout)
{
    int i;
    if (!loadout || strlen(loadout) < 3) return -1;
    for (i = 0; i < 32; i++) if (!strncasecmp(GBL_MECHS[i].code, loadout, 3)) return i;
    return -1;
}

/* ---- star API (FUN_0003e350 / FUN_0003e0b0 / FUN_0003e2e0 / FUN_0003e330) */
static gbl_star *star_rec(int s) { return &g_gstar[s ? 1 : 0]; }
/* FUN_0003e350(star, formation, max, size, tonnage): select star s as the current record; -1 keeps a field */
static void gstar_set(int s, int formation, int max, int size, int tons)
{
    gbl_star *st = star_rec(s);
    g_gstar_cur = s ? 1 : 0;
    if (formation >= 0) st->formation = formation;
    if (max >= 0) st->max = max > 3 ? 3 : max;
    if (size >= 0) st->size = size > 3 ? 3 : size;
    if (tons >= 0) st->tons = tons;
}
/* FUN_0003e0b0(slot, code, name1, name2) on the current star (slot -1 = the current slot): the names are set first;
 * an unknown / empty code removes the last slot only when slot != 0 and slot + 1 == size (-1), else returns 1 when slot
 * >= size, 0 (refused) otherwise - so slot 0 never becomes empty; a chassis over the tonnage limit is refused (0); the
 * loadout is the first 8 characters, a 3-letter code gets "00std"; then (every call) slot == size && size < max grows
 * the star. Returns -1 when done, 0 refused. (C_campaign.md 4.4; agent H's trace) */
static int gstar_slot(int slot, const char *code, const char *name1, const char *name2)
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
static int gstar_mech(int slot)   /* FUN_0003e2e0: the slot's mech index while slot < max and slot < size, else -1 */
{
    gbl_star *st = star_rec(g_gstar_cur);
    if (slot < 0) slot = st->cur;
    if (slot < 0 || slot > 2 || slot >= st->max || slot >= st->size) return -1;
    return st->slot[slot].mech;
}
/* the static defaults of the star records (0x856b0 / 0x85784, as in the install's MW2PRM.CFG) */
static void gstar_defaults(void)
{
    static const char *lo[3] = {"tbr00std", "drw00std", "frm00std"};
    static const char *fn[3] = {"MechWarrior", "Friend 1", "Friend 2"};
    int k;
    memset(g_gstar, 0, sizeof g_gstar);
    for (k = 0; k < 3; k++) {
        g_gstar[0].slot[k].mech = gmech_by_code(lo[k]);
        snprintf(g_gstar[0].slot[k].loadout, 16, "%s", lo[k]);
        snprintf(g_gstar[0].slot[k].name1, 22, "%s", fn[k]);
        g_gstar[1].slot[k].mech = gmech_by_code("tbr");
        snprintf(g_gstar[1].slot[k].loadout, 16, "tbr00std");
        snprintf(g_gstar[1].slot[k].name1, 22, "Enemy %d", k + 1);
    }
    g_gstar[0].max = 3; g_gstar[0].size = 3; g_gstar[0].tons = 100;
    g_gstar[1].tons = 100;
}

/* ======================================================================================================================
 * MW2PRM.CFG (0x2c0 bytes; FUN_0003aeb0 read, FUN_0003af90 write before every shell exit): +0 return state, +4 context,
 * +8 flag, +0xc friendly star is current, +0x10 friendly star, +0xe4 enemy star, +0x1bc pilot index, +0x1c0 the sim's
 * command line. The port's shell stays up across missions; the file is still written as the original does. */
static char g_camo[16] = "xxxxxxxx.xxx";   /* 0x84dfc: the scenario's BRF2 SUPS string ("-b=") */
static const char *const GBL_FORM[6] = {"echelonl", "echelonr", "lineabreast", "lineastern", "vform", "wedge"};
static void prm_write(int state, int ctx, int flag, const char *scen)
{
    uint8_t b[0x2c0];
    char line[256];
    FILE *f;
    int32_t v;
    memset(b, 0, sizeof b);
    v = state; memcpy(b + 0, &v, 4);
    v = ctx; memcpy(b + 4, &v, 4);
    b[8] = (uint8_t)flag;
    v = g_gstar_cur == 0; memcpy(b + 0xc, &v, 4);
    memcpy(b + 0x10, &g_gstar[0], 0xd4);
    memcpy(b + 0xe4, &g_gstar[1], 0xd4);
    v = g_cur; memcpy(b + 0x1bc, &v, 4);
    snprintf(line, sizeof line, "%s -b=%s%s", scen ? scen : "", g_camo, g_cheat_rvwindow ? " -x" : "");
    if (state != 14 && state != -3) {   /* FUN_0003e3b0 */
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, " -of=%s", GBL_FORM[g_gstar[0].formation >= 0 && g_gstar[0].formation < 6 ? g_gstar[0].formation : 0]);
        if (g_gstar[1].size) { n = strlen(line); snprintf(line + n, sizeof line - n, " -oe=%s", GBL_FORM[g_gstar[1].formation >= 0 && g_gstar[1].formation < 6 ? g_gstar[1].formation : 0]); }
    }
    memcpy(b + 0x1c0, line, strlen(line) < 255 ? strlen(line) : 255);
    if ((f = dp_fopen("MW2PRM.CFG", "wb"))) { fwrite(b, 1, sizeof b, f); fclose(f); }
}

/* ======================================================================================================================
 * the handoff files (FUN_0001d0b0 / FUN_0001ce30 / FUN_0001d430): USERSTAR.BWD (the friendly star), EN01..05STAR.BWD
 * (the enemy star, skill falling by file), INSTMAP1.BWD (camo; Combat Simulator only). Container as the base (lance.c);
 * the GPS record is 100 bytes in GBL: +54 name 1 (21 chars), +76 name 2 (21). */
static int g_skill_base = 8;   /* DAT_0007b728 (a mission's second SDSC +0 could set it; none in GBL) */
/* skill rows (table 0x7b6cc = base 0x7a7a8, same values): skill, range, 1, range, pilot level */
static const short GBL_SKILL[9][5] = {
    {0, 0, 0, 0, 0}, {1, 850, 1, 850, 1}, {2, 700, 1, 700, 2}, {3, 650, 1, 650, 2}, {3, 550, 1, 550, 3},
    {4, 450, 1, 450, 3}, {4, 350, 1, 350, 4}, {5, 300, 1, 300, 4}, {5, 250, 1, 250, 5}};
static int gbl_rd_id(prj_archive *a, const char *tag, const char *name)
{
    int t = prj_find_type(a, tag);
    return t < 0 ? -1 : prj_find_id(a, t, name);
}
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* one star file: file 0 = USERSTAR (the friendly star, skill 0), 1..5 the enemy files */
static int gbl_write_star(prj_archive *a, const char *path, const gbl_star *st, int file, int skill)
{
    uint8_t buf[4096];
    size_t n = 0;
    int k, largest = 28;
    FILE *f;
    memcpy(buf, "BWD\0", 4); n = 12;
    memcpy(buf + n, "REV\0", 4); put32(buf + n + 4, 12); memcpy(buf + n + 8, "1.22", 4); n += 12;
    memcpy(buf + n, "DTBL", 4); put32(buf + n + 4, 28); memset(buf + n + 8, 0, 20); n += 28;
    for (k = 0; st && k < st->size && k < 3; k++) {
        const gbl_slot *sl = &st->slot[k];
        uint8_t *g = buf + n;
        int mek, chassis, m = sl->mech;
        char lo[16];
        if (m < 0 || m >= 32) continue;
        snprintf(lo, sizeof lo, "%s", sl->loadout[0] ? sl->loadout : "");
        if (!lo[0]) snprintf(lo, sizeof lo, "%s00std", GBL_MECHS[m].code);
        memset(g, 0, 100);
        memcpy(g, "GPS\0", 4); put32(g + 4, 100);
        mek = strlen(lo) >= 8 && !strncasecmp(lo + 5, "std", 3) ? gbl_rd_id(a, "MEK", lo) : -1;
        put16(g + 8, mek >= 0 ? mek : 0xfffe);
        chassis = gbl_rd_id(a, "BWD", GBL_MECHS[m].file);
        put16(g + 10, chassis >= 0 ? chassis : 0);
        g[12] = (uint8_t)file;
        g[13] = (uint8_t)(k == 0);
        g[14] = (uint8_t)(file == 0 && k == 0 ? 0 : 2);
        {   /* +16..+25 the skill row (file 0: skill 0; else clamped 1..8) */
            int q, sk = file == 0 ? 0 : skill < 1 ? 1 : skill > 8 ? 8 : skill;
            for (q = 0; q < 5; q++) put16(g + 16 + 2 * q, GBL_SKILL[sk][q]);
        }
        put16(g + 32, 6); put16(g + 34, 0x400);
        snprintf((char *)g + 36, 9, "%.8s", GBL_MECHS[m].file);
        snprintf((char *)g + 45, 9, "%.8s", lo);
        snprintf((char *)g + 54, 22, "%.21s", sl->name1);
        snprintf((char *)g + 76, 22, "%.21s", sl->name2);
        n += 100;
        if (100 > largest) largest = 100;
    }
    put32(buf + 4, (uint32_t)n); put32(buf + 8, (uint32_t)largest);
    if (!(f = fopen(path, "wb"))) return -1;
    fwrite(buf, 1, n, f);
    fclose(f);
    return 0;
}

/* ======================================================================================================================
 * the shared state the screens use (the state machine's locals) */
static int g_state = 8, g_prev = -1, g_ctx = 0, g_flag = 0;   /* screen, previous screen, context, "pilot accepted" */
static char g_scen[16] = "";        /* local_34: the scenario of the current mission */
static int g_old_mission = 0;       /* 0x95e88: a roster LAUNCH OLD MISSION is under way (skip the debriefing) */
static int g_brief_m = 0;           /* 0x7e840: the mission being briefed */
static int g_transitions = 1;       /* MW2SND.CFG +0x3c: the mission clip lists (TRANSITIONS) */
static int g_result_status;         /* MW2MSN status after a mission: 2 success, 3 failed, 4 time, 0 aborted */

/* run the simulation for the scenario (state 10): the handoff files, the sim, the results back. 0 if it ran. */
static int gbl_launch(const char *scen, int ret_state)
{
    const char *save = getenv("MW2_SAVE_DIR");
    char err[256], line[4200], path[1200], up[16];
    prj_archive *a;
    int k, rc;
    if (!save) save = g_dir;
    prm_write(ret_state, g_ctx, g_flag, scen);
    a = prj_open(sim_archive(), err, sizeof err);
    if (!a) { fprintf(stderr, "gbl: cannot open %s: %s\n", sim_archive(), err); return -1; }
    snprintf(path, sizeof path, "%s/USERSTAR.BWD", save);
    gbl_write_star(a, path, &g_gstar[0], 0, 0);
    {   /* MW2DIF +5: EASY -1, MEDIUM -2, HARD -3 from the skill base; one lower per file */
        int d = 1;
        FILE *f = dp_fopen("MW2DIF.CFG", "rb");
        uint8_t dif[8] = {0};
        if (f) { if (fread(dif, 1, 8, f) == 8) d = dif[5]; fclose(f); }
        for (k = 1; k <= 5; k++) {
            int sk = g_skill_base - (d == 0 ? 1 : d == 2 ? 3 : 2) - (k - 1);
            snprintf(path, sizeof path, "%s/EN%02dSTAR.BWD", save, k);
            gbl_write_star(a, path, &g_gstar[1], k, sk);
        }
    }
    prj_close(a);
    snprintf(up, sizeof up, "%s", scen);
    for (k = 0; up[k]; k++) up[k] = (char)toupper((unsigned char)up[k]);
    printf("gbl: launch %s (-b=%s -of=%s)\n", up, g_camo, GBL_FORM[g_gstar[0].formation >= 0 && g_gstar[0].formation < 6 ? g_gstar[0].formation : 0]);
    snprintf(g_load_scene, sizeof g_load_scene, "%s", scen);
    setenv("MW2_OF", GBL_FORM[g_gstar[0].formation >= 0 && g_gstar[0].formation < 6 ? g_gstar[0].formation : 0], 1);
    if (g_gstar[1].size) setenv("MW2_OE", GBL_FORM[g_gstar[1].formation >= 0 && g_gstar[1].formation < 6 ? g_gstar[1].formation : 0], 1);
    else unsetenv("MW2_OE");
    setenv("MW2_PILOT", g_gstar[0].slot[0].name1, 1);
    setenv("MW2_CD_DATA", portcfg_cd_data(), 1);
    sim_command(line, sizeof line, up, save);
    gsnd_stop_all();
    snprintf(path, sizeof path, "%s/MW2MSN.CFG", save);
    remove(path);
    if (getenv("GBL_NO_SIM")) { printf("gbl: sim command: %s\n", line); return 0; }   /* TEST ONLY */
    if (g_win) SDL_HideWindow(g_win);
    rc = system(line);
    sim_returned(rc);
    if (g_win) { SDL_ShowWindow(g_win); SDL_RaiseWindow(g_win); }
    {
        FILE *f = fopen(path, "rb");
        uint8_t b[20];
        g_result_status = 0;
        if (f) { if (fread(b, 1, 20, f) == 20) g_result_status = b[16] | b[17] << 8; fclose(f); }
    }
    return 0;
}

/* ======================================================================================================================
 * the screens (each returns the next state; the shared Esc menu returns 8 / -3 / -1 = stay) */
static int gbl_global_menu(void);          /* gbl_frame.c: FUN_00026f10 (Esc or the right button on any screen) */
static int gbl_title(void);                /* gbl_frame.c: state 8 */
static int gbl_roster(void);               /* gbl_pilots.c: state 12 */
static int gbl_hall(void);                 /* gbl_pilots.c: state 1 */
static int gbl_bloodname(void);            /* gbl_pilots.c: state 16 (FUN_0001dbd0) */
static int gbl_egg(void);                  /* gbl_pilots.c: state 17 */
static int gbl_mission_computer(void);     /* gbl_campaign.c: state 5 */
static int gbl_debriefing(void);           /* gbl_campaign.c: state 3 */
static void gbl_mission_enter(void);       /* gbl_campaign.c: state 5's set-up in the state machine (star, scenario) */
static void gbl_launch_clips(void);        /* gbl_campaign.c: state 10's clip list A */
static int gbl_combat_sim(void);           /* gbl_lab.c: state 7 */
static int gbl_mech_lab(void);             /* gbl_lab.c: state 9 */

#include "gbl/gbl_frame.c"
#include "gbl/gbl_pilots.c"
#include "gbl/gbl_campaign.c"
#include "gbl/gbl_lab.c"

/* ======================================================================================================================
 * the state machine (FUN_00039ec0) */
static int gbl_run(void)
{
    int next;
    while (g_state != -3 && !g_quit_all) {
        int prev = g_prev;
        if (g_state == 5 && g_ctx == 2) g_state = 7;
        if (g_state == -2) g_state = prev;
        g_prev = g_state;
        gsnd_stop_all();
        gs_close_all();
        text_layer_clear();
        switch (g_state) {
        case 1:
            if (prev == 12) { gstar_set(0, 0, 3, 1, 100); if (pil()) gstar_slot(0, NULL, pil()->display, NULL); }
            next = gbl_hall();
            break;
        case 3: next = gbl_debriefing(); break;
        case 5:
            g_prev = prev;
            gbl_mission_enter();
            g_prev = 5;
            next = gbl_mission_computer();
            break;
        case 7:
            g_ctx = 2;
            next = gbl_combat_sim();
            if (next == 1) g_ctx = 0;
            break;
        case 8: gstar_set(1, 0, 0, 0, 100); g_flag = 0; next = gbl_title(); break;
        case 9: next = gbl_mech_lab(); break;
        case 10:
            if ((prev == 5 || prev == 9) && !g_old_mission) next = 3;
            else if (g_old_mission) { next = 12; g_old_mission = 0; }
            else next = prev;
            if (prev == 5) gbl_launch_clips();
            gbl_launch(g_scen, next);
            break;
        case 12:
            g_old_mission = 0;
            next = gbl_roster();
            if (next == 5) g_old_mission = 1;
            break;
        case 15: next = 1; break;   /* FUN_0003c430(NULL): no video */
        case 16:
            if (pil() && pil()->deaths == 0 && pil()->done != 17) next = gbl_bloodname();
            else { gbl_video("mend"); next = 1; }
            break;
        case 17: g_prev = prev; next = gbl_egg(); g_prev = 17; break;
        default:
            fprintf(stderr, "Unknown Transition Requested: %d\n", g_state);
            return 1;
        }
        if (g_state == 10) g_prev = 10;
        g_state = next;
    }
    prm_write(-3, g_ctx, g_flag, "exittos");
    return 0;
}

int glview_main(int argc, char **argv);
int main(int argc, char **argv)
{
    char *bp;
    int headless;
    setenv("MW2_GAME", "gbl", 1);   /* the simulation's GBL rules (sim_game_gbl: weapons, AMS, NARC, bosses) - the
                                     * shell's Mech Lab reads the same tables */
    portcfg_set_app("gblport", "gblport.cfg", "GBL-game", "GBL");   /* (before --sim too: the sim reads and writes gblport.cfg) */
    if (argc >= 2 && strcmp(argv[1], "--sim") == 0) return glview_main(argc - 1, argv + 1);   /* the simulation */
    snprintf(g_exe_path, sizeof g_exe_path, "%s", argv[0]);
    if (!strchr(argv[0], '/')) { bp = SDL_GetBasePath(); snprintf(g_exe_path, sizeof g_exe_path, "%s%s", bp ? bp : "./", argv[0]); SDL_free(bp); }
    headless = g_headless = getenv("SDL_VIDEODRIVER") && (!strcmp(getenv("SDL_VIDEODRIVER"), "offscreen") || !strcmp(getenv("SDL_VIDEODRIVER"), "dummy"));
    bp = SDL_GetBasePath();
    snprintf(g_exe_dir, sizeof g_exe_dir, "%s", bp ? bp : "./");
    SDL_free(bp);
    portcfg_load(NULL, &g_cfg);
    if (argc >= 2) { g_cfg.game[0] = 0; g_cfg.install[0] = 0; g_cfg.cd[0] = 0; g_cfg.models[0] = 0; g_cfg.textures[0] = 0; }
    if (portcfg_resolve(&g_cfg, g_exe_dir, argc >= 2 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "Ghost Bear's Legacy: no game folder found.\nRun:  gbl <folder>\nwhere <folder> is the GBL-game folder (cd/ = a copy of the Ghost Bear's Legacy disc, install/, music/). See FOLDER_SETUP_GBL.md.\n");
        if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ghost Bear's Legacy", "No game folder found.\n\nPut your GBL-game folder (cd/ = a copy of the Ghost Bear's Legacy disc) next to the program or in your home folder - see FOLDER_SETUP_GBL.md.", NULL);
        return 2;
    }
    if (argc < 2 && !g_cfg.game[0]) portcfg_save(NULL, &g_cfg);
    /* gbl's looks (portcfg.h): DOS | ENHANCED | TITANIUM; TITANIUM needs titanium/MW2.PRJ - DOS without it */
    if (!portcfg_gbl_look_ok(&g_cfg, portcfg_gbl_look(&g_cfg))) portcfg_gbl_set_look(&g_cfg, PORTCFG_GBL_DOS);
    snprintf(g_dir, sizeof g_dir, "%s", g_cfg.install);
    dp_clear_roots();
    dp_add_root(g_cfg.install);   /* pilots, settings, saves: first (and where writes go) */
    if (g_cfg.cd[0]) {
        size_t n = strlen(g_cfg.cd);
        if (n > 4 && strcasecmp(g_cfg.cd + n - 4, ".iso") == 0) dp_add_iso_root(g_cfg.cd, "GBL");
        else { char m2[1100]; snprintf(m2, sizeof m2, "%s/GBL", g_cfg.cd); dp_add_root(m2); dp_add_root(g_cfg.cd); }
    }
    g_db_remap = gbl_db_remap;
    if (shelldb_open(gpath("DATABASE.MW2"), &g_db)) { fprintf(stderr, "gbl: cannot open DATABASE.MW2 (%s)\n", gpath("DATABASE.MW2")); return 1; }
    g_have_dbsound = shelldb_open(gpath("SMK\\DBSOUND.MW2"), &g_dbsound) == 0 || shelldb_open(gpath("DBSOUND.MW2"), &g_dbsound) == 0;
    /* the base screens' fonts, renumbered (g_db_remap): base 27 / 25 / 31 / 30 / 29 / 26 = GBL 29 / 27 / 33 / 32 / 31 / 28 */
    g_flabel.len = shdb(&g_db, 27, &g_flabel.d);
    g_flist.len = shdb(&g_db, 25, &g_flist.d);
    g_fthin.len = shdb(&g_db, 31, &g_fthin.d);
    g_fhead.len = shdb(&g_db, 30, &g_fhead.d);
    g_fbig.len = shdb(&g_db, 29, &g_fbig.d);
    g_f26.len = shdb(&g_db, 26, &g_f26.d);
    g_f28.len = shdb(&g_db, 27, &g_f28.d);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0 && SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    g_sfx = sfx_create(NULL);
    if (!headless) {
        portcfg pc;
        Uint32 wf = SDL_WINDOW_RESIZABLE;
        portcfg_load(NULL, &pc);
        if (pc.mode == 0 || pc.mode == 2) wf |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        g_win = SDL_CreateWindow("MechWarrior 2: Ghost Bear's Legacy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, wf);
        SDL_StopTextInput();
        g_grd = g_rd = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_PRESENTVSYNC);
        SDL_RenderSetLogicalSize(g_grd, W, H);
        g_gtx = g_tx = SDL_CreateTexture(g_grd, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (!getenv("MW2_TEXT_SMOOTH")) { SDL_SetTextureScaleMode(g_gtx, SDL_ScaleModeNearest); SDL_RenderSetIntegerScale(g_grd, SDL_TRUE); }
        g_text_hires = 1;
    }
    { bp = SDL_GetBasePath(); ttext_init(bp); SDL_free(bp); }
    gbl_cursor_load();
    gstar_defaults();
    pil_load();
    {   /* MW2SND.CFG (0x40 bytes): +0x3c TRANSITIONS */
        FILE *f = dp_fopen("MW2SND.CFG", "rb");
        uint8_t b[0x40] = {0};
        if (f) { if (fread(b, 1, 0x40, f) >= 0x40) g_transitions = b[0x3c] | b[0x3d] << 8; fclose(f); }
    }
    srand((unsigned)time(NULL));
    if (getenv("GBL_PILOT")) { g_cur = atoi(getenv("GBL_PILOT")); if (pil() && pil()->in_use) g_gstar[0] = pil()->star; else g_cur = -1; }
    if (getenv("GBL_SCREEN")) { g_state = atoi(getenv("GBL_SCREEN")); g_ctx = getenv("GBL_CTX") ? atoi(getenv("GBL_CTX")) : 0; g_flag = 1; }
    else if (!getenv("MW2_NO_INTRO") && !(argc > 2 && !strcmp(argv[argc - 1], "sim"))) gbl_video("mintro");   /* 0x3a207 */
    gbl_run();
    gs_close_all();
    if (g_sfx) sfx_destroy(g_sfx);
    if (g_grd) SDL_DestroyRenderer(g_grd);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
