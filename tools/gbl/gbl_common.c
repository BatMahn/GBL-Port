/*
 * gbl_common - the GBL shell's shared helpers: the frame buffer and DATABASE.MW2 access, the shell fonts and the text
 * layer (queued glyphs drawn at the display's resolution when windowed), backgrounds, Smacker clips and the full-screen
 * videos' look, the simulation's command line, mission records, the Mech Lab's mass table and design editor (CUSTOMIZE),
 * the Cockpit Controls editor and the base dialog. GBL's shell (MW2SHELL.EXE of Ghost Bear's Legacy) is a modified copy
 * of the base game's shell, and these pieces are the ones it kept unchanged. Included by tools/gblshell.c (one
 * translation unit with the screens in tools/gbl/); addresses cited as "base" are the base game's MW2SHELL.EXE.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "lance.h"
#include "mek.h"
#include "shp.h"
#include "bwd.h"
#include "cockpitcfg.h"
#include "portcfg.h"
#include "datapath.h"
#include "ttext.h"
#include "prj.h"
#include "sfx.h"
#include "shelldb.h"
#include "smk.h"
#include "vidfx.h"

#define W 640
#define H 480

static uint8_t g_fb[W * H * 3];
static shelldb g_db;
/* the base DATABASE numbers the kept code asks for, renumbered for GBL (gblshell.c gbl_db_remap) */
static int gbl_db_remap(int e);
static long shdb(const shelldb *db, int entry, uint8_t **out) { return shelldb_entry(db, gbl_db_remap(entry), out); }
static sfx *g_sfx;
static char g_dir[512];
static portcfg g_cfg;          /* the per-user configuration (paths, display) */
static char    g_exe_dir[1024];
static char    g_exe_path[1100];   /* this executable: the shell runs it again with --sim */
/* a game file for reading: the install folder first, then the disc (datapath roots); else <install>/name */
static const char *gpath(const char *name)
{
    static char out[4][1100];
    static int k;
    char *o = out[k = (k + 1) & 3];
    if (dp_resolve(name, o, sizeof out[0]) != 0) snprintf(o, sizeof out[0], "%s/%s", g_dir, name);
    return o;
}

/* ---- fonts ("1." format: glyph count, height, transparent index, offsets; glyph = u32 width + w*h)
 * entry 27 (16 px, getter index 0x1c): hotspot labels; entry 25 (8 px, index 0x1a): lists.
 * remap1 >= 0 draws glyph colour 1 as that palette index (Instant Action uses 0x22). */
typedef struct { uint8_t *d; long len; } font;
static font g_flabel, g_flist, g_fthin, g_fhead, g_fbig, g_f26, g_f28;   /* + 29: white large capitals (roster) */   /* DATABASE fonts 27, 25, 31 (thin), 30 (grey capitals) */
static int text_width(const font *fo, const char *t)
{
    int w = 0;
    if (!fo->d) return 0;
    for (; *t; t++) {
        int c = (unsigned char)*t & 127;
        uint32_t o = fo->d[16 + c * 4] | fo->d[17 + c * 4] << 8 | fo->d[18 + c * 4] << 16;
        w += (o + 4 <= (uint32_t)fo->len) ? (fo->d[o] | fo->d[o + 1] << 8) : 4;
    }
    return w;
}
static int font_height(const font *fo) { return fo->d && fo->len > 12 ? (fo->d[8] | fo->d[9] << 8) : 16; }   /* header: "1." , glyph count, height, transparent index */
/* ---- text. The original shell fonts, but not rasterised into the 640x480 frame: with a renderer, each call is queued
 * and drawn after the frame as glyph textures at the display's resolution - the glyph upscaled 3x with Scale3x (edge-
 * aware, keeps the letterforms) and linearly filtered - so text stays crisp in native fullscreen. Headless captures
 * still rasterise into the frame. The queue is cleared when a frame starts (draw_bg / text_layer_clear). */
static int g_text_hires;   /* set once a renderer exists */
typedef struct { const font *fo; int x, y, remap1; char t[96]; const uint8_t (*pal)[3]; uint32_t palsum; } text_item;
static text_item g_text[256];
static int g_ntext;
static void text_layer_clear(void) { g_ntext = 0; }
static void text_layer_cull(int x0, int y0, int x1, int y1)   /* a dimmed / covered area hides the text under it */
{
    int i, n = 0;
    for (i = 0; i < g_ntext; i++) {
        const text_item *it = &g_text[i];
        int fh = it->fo->d ? it->fo->d[8] : 10;
        if (it->x < x1 && it->x + 400 > x0 && it->y < y1 && it->y + fh > y0 && it->x >= x0 && it->y >= y0 && it->y + fh <= y1) continue;
        g_text[n++] = g_text[i];
    }
    g_ntext = n;
}
static void raster_text(const font *fo, int x, int y, const char *t, const uint8_t (*pal)[3], int remap1)
{
    int fh, key;
    if (!fo->d) return;
    fh = fo->d[8]; key = fo->d[12];
    for (; *t; t++) {
        int c = (unsigned char)*t & 127, gx, gy, gw;
        uint32_t o = fo->d[16 + c * 4] | fo->d[17 + c * 4] << 8 | fo->d[18 + c * 4] << 16;
        if (o + 4 > (uint32_t)fo->len) { x += 4; continue; }
        gw = fo->d[o] | fo->d[o + 1] << 8;
        for (gy = 0; gy < fh; gy++)
            for (gx = 0; gx < gw; gx++) {
                size_t si = o + 4 + (size_t)gy * (size_t)gw + (size_t)gx;
                int px = x + gx, py = y + gy;
                uint8_t v;
                if (si >= (size_t)fo->len || px < 0 || py < 0 || px >= W || py >= H) continue;
                v = fo->d[si];
                if (v == key) continue;
                if (remap1 >= 0 && v == 1) v = (uint8_t)remap1;
                memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, pal[v], 3);
            }
        x += gw;
    }
}
static void draw_text(const font *fo, int x, int y, const char *t, const uint8_t (*pal)[3], int remap1)
{
    if (!g_text_hires) { raster_text(fo, x, y, t, pal, remap1); return; }
    if (g_ntext < 256 && fo->d) {
        text_item *it = &g_text[g_ntext++];
        int k;
        it->fo = fo; it->x = x; it->y = y; it->remap1 = remap1; it->pal = pal;
        snprintf(it->t, sizeof it->t, "%s", t);
        it->palsum = 0;
        for (k = 0; k < 256; k++) it->palsum = it->palsum * 31u + (uint32_t)(pal[k][0] | pal[k][1] << 8 | pal[k][2] << 16);
    }
}

/* Scale3x of an RGBA image (pixels compared exactly) */
static void scale3x(const uint32_t *in, int w, int h, uint32_t *out)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
#define P(xx, yy) in[((yy) < 0 ? 0 : (yy) >= h ? h - 1 : (yy)) * w + ((xx) < 0 ? 0 : (xx) >= w ? w - 1 : (xx))]
            uint32_t A = P(x - 1, y - 1), B = P(x, y - 1), C = P(x + 1, y - 1), D = P(x - 1, y), E = P(x, y), F = P(x + 1, y),
                     G = P(x - 1, y + 1), H2 = P(x, y + 1), I = P(x + 1, y + 1), o[9];
#undef P
            if (B != H2 && D != F) {
                o[0] = D == B ? D : E;
                o[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                o[2] = B == F ? F : E;
                o[3] = (D == B && E != G) || (D == H2 && E != A) ? D : E;
                o[4] = E;
                o[5] = (B == F && E != I) || (H2 == F && E != C) ? F : E;
                o[6] = D == H2 ? D : E;
                o[7] = (D == H2 && E != I) || (H2 == F && E != G) ? H2 : E;
                o[8] = H2 == F ? F : E;
            } else { int k; for (k = 0; k < 9; k++) o[k] = E; }
            {
                int k;
                for (k = 0; k < 9; k++) out[(y * 3 + k / 3) * (w * 3) + x * 3 + k % 3] = o[k];
            }
        }
}
typedef struct { const font *fo; int c, remap1; uint32_t palsum; SDL_Texture *tex; int w, h; } glyph_tex;
static glyph_tex g_gcache[1024];
static int g_ngcache;
static SDL_Texture *glyph_texture(SDL_Renderer *rd, const text_item *it, int c, int *gw_out)
{
    const font *fo = it->fo;
    int fh = fo->d[8], key = fo->d[12], gw, i, gx, gy;
    uint32_t o = fo->d[16 + c * 4] | fo->d[17 + c * 4] << 8 | fo->d[18 + c * 4] << 16;
    uint32_t *src, *dst;
    glyph_tex *g;
    if (o + 4 > (uint32_t)fo->len) { *gw_out = 4; return NULL; }
    gw = fo->d[o] | fo->d[o + 1] << 8;
    *gw_out = gw;
    for (i = 0; i < g_ngcache; i++) {
        g = &g_gcache[i];
        if (g->fo == fo && g->c == c && g->remap1 == it->remap1 && g->palsum == it->palsum) return g->tex;
    }
    if (gw <= 0) return NULL;
    if (g_ngcache == 1024) { for (i = 0; i < g_ngcache; i++) SDL_DestroyTexture(g_gcache[i].tex); g_ngcache = 0; }
    src = calloc((size_t)gw * (size_t)fh, 4); dst = calloc((size_t)gw * (size_t)fh * 9, 4);
    if (!src || !dst) { free(src); free(dst); return NULL; }
    for (gy = 0; gy < fh; gy++)
        for (gx = 0; gx < gw; gx++) {
            size_t si = o + 4 + (size_t)gy * (size_t)gw + (size_t)gx;
            uint8_t v = si < (size_t)fo->len ? fo->d[si] : (uint8_t)key;
            if (v == key) continue;
            if (it->remap1 >= 0 && v == 1) v = (uint8_t)it->remap1;
            src[gy * gw + gx] = 0xff000000u | (uint32_t)it->pal[v][2] << 16 | (uint32_t)it->pal[v][1] << 8 | it->pal[v][0];
        }
    /* the Win9x shell's look (default): the bitmap font at its own size, one texel per font pixel, scaled with the
     * 640 x 480 screen by whole pixels - every glyph pixel the same size, as DirectDraw drew it (a 3x texture squeezed
     * into a 2x screen made uneven strokes). MW2_TEXT_SMOOTH=1: Scale3x and linear filtering */
    int smooth = getenv("MW2_TEXT_SMOOTH") != NULL, k3 = smooth ? 3 : 1;
    if (smooth) scale3x(src, gw, fh, dst);
    else memcpy(dst, src, (size_t)gw * (size_t)fh * 4);
    g = &g_gcache[g_ngcache];
    g->tex = SDL_CreateTexture(rd, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, gw * k3, fh * k3);
    if (g->tex) {
        SDL_UpdateTexture(g->tex, NULL, dst, gw * k3 * 4);
        SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g->tex, getenv("MW2_TEXT_SMOOTH") ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        g->fo = fo; g->c = c; g->remap1 = it->remap1; g->palsum = it->palsum; g->w = gw; g->h = fh;
        g_ngcache++;
    }
    free(src); free(dst);
    return g->tex;
}
/* the menu text as a vector font (user request: cleaner than the bitmap font scaled up by whole pixels): each string
 * in Liberation Sans Bold at the display's physical resolution, its cap height and top taken from the bitmap font's
 * 'H', fitted to the bitmap string's width (letter spacing added, or condensed when wider), in the bitmap font's main
 * ink colour. Strings with the fonts' special glyphs (arrows, < 32) keep the bitmap font. MW2_BITMAP_TEXT=1: the
 * original bitmap font throughout */
typedef struct { const font *fo; char t[96]; uint8_t rgb[3]; float ps; SDL_Texture *tex; int w, h; float dx, dy; } vec_entry;
static vec_entry g_vcache[256];
static int g_nvcache;
static void font_caps(const font *fo, int *top, int *cap)
{
    static const font *last; static int lt, lc;
    const char *probe = "HEI0";
    int k;
    if (fo == last) { *top = lt; *cap = lc; return; }
    *top = 0; *cap = fo->d[8];
    for (k = 0; probe[k]; k++) {
        int c = probe[k], fh = fo->d[8], key = fo->d[12], gw, x, y, y0 = -1, y1 = -1;
        uint32_t o = fo->d[16 + c * 4] | fo->d[17 + c * 4] << 8 | fo->d[18 + c * 4] << 16;
        if (o + 4 > (uint32_t)fo->len) continue;
        gw = fo->d[o] | fo->d[o + 1] << 8;
        for (y = 0; y < fh; y++)
            for (x = 0; x < gw; x++) {
                size_t si = o + 4 + (size_t)y * (size_t)gw + (size_t)x;
                if (si < (size_t)fo->len && fo->d[si] != key) { if (y0 < 0) y0 = y; y1 = y; }
            }
        if (y0 >= 0) { *top = y0; *cap = y1 - y0 + 1; break; }
    }
    last = fo; lt = *top; lc = *cap;
}
static int vec_text(SDL_Renderer *rd, const text_item *it)
{
    const font *fo = it->fo;
    float ps = 1.0f, tw, target, hs = 1.0f, track = 0.0f;
    int top, cap, i, n = (int)strlen(it->t), counts[256], best = -1, w, h, by;
    uint8_t rgb[3];
    vec_entry *e = NULL;
    if (!ttext_available() || getenv("MW2_BITMAP_TEXT") || n == 0) return 0;
    for (i = 0; i < n; i++) if ((unsigned char)it->t[i] < 32 || (unsigned char)it->t[i] > 126) return 0;
    {   /* the physical pixels per 640x480 unit */
        int ow, oh, lw, lh;
        SDL_GetRendererOutputSize(rd, &ow, &oh);
        SDL_RenderGetLogicalSize(rd, &lw, &lh);
        if (lw > 0 && lh > 0) { float a = (float)ow / (float)lw, b = (float)oh / (float)lh; ps = a < b ? a : b; }
        if (SDL_RenderGetIntegerScale(rd) && ps >= 1.0f) ps = floorf(ps);
    }
    /* the ink colour: the most used palette index in the string's glyphs */
    memset(counts, 0, sizeof counts);
    for (i = 0; i < n; i++) {
        int c = (unsigned char)it->t[i], fh = fo->d[8], key = fo->d[12], gw, j;
        uint32_t o = fo->d[16 + c * 4] | fo->d[17 + c * 4] << 8 | fo->d[18 + c * 4] << 16;
        if (o + 4 > (uint32_t)fo->len) continue;
        gw = fo->d[o] | fo->d[o + 1] << 8;
        for (j = 0; j < gw * fh; j++) {
            size_t si = o + 4 + (size_t)j;
            int v;
            if (si >= (size_t)fo->len || fo->d[si] == key) continue;
            v = fo->d[si];
            if (it->remap1 >= 0 && v == 1) v = it->remap1;
            counts[v]++;
        }
    }
    for (i = 0; i < 256; i++) if (counts[i] && (best < 0 || counts[i] > counts[best])) best = i;
    if (best < 0) return 1;   /* spaces only */
    rgb[0] = it->pal[best][0]; rgb[1] = it->pal[best][1]; rgb[2] = it->pal[best][2];
    for (i = 0; i < g_nvcache; i++) {
        vec_entry *c = &g_vcache[i];
        if (c->fo == fo && c->ps == ps && !memcmp(c->rgb, rgb, 3) && !strcmp(c->t, it->t)) { e = c; break; }
    }
    if (!e) {
        uint8_t *img;
        uint32_t *rgba;
        font_caps(fo, &top, &cap);
        tw = ttext_width(it->t, (float)cap * ps, 1.0f);
        target = (float)text_width(fo, it->t) * ps;
        if (tw > target) { hs = target / tw; if (hs < 0.72f) hs = 0.72f; }
        else if (n > 1) track = (target - tw) / (float)n;
        if (track > (float)cap * ps * 0.6f) track = (float)cap * ps * 0.6f;
        if (!(img = ttext_render_track(it->t, (float)cap * ps, hs, track, &w, &h, &by))) return 0;
        if (g_nvcache == 256) { for (i = 0; i < g_nvcache; i++) SDL_DestroyTexture(g_vcache[i].tex); g_nvcache = 0; }
        e = &g_vcache[g_nvcache];
        e->tex = NULL;
        if ((rgba = malloc((size_t)w * (size_t)h * 4))) {
            for (i = 0; i < w * h; i++) rgba[i] = (uint32_t)img[i] << 24 | (uint32_t)rgb[2] << 16 | (uint32_t)rgb[1] << 8 | rgb[0];
            if ((e->tex = SDL_CreateTexture(rd, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h))) {
                SDL_UpdateTexture(e->tex, NULL, rgba, w * 4);
                SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(e->tex, SDL_ScaleModeLinear);
            }
            free(rgba);
        }
        free(img);
        if (!e->tex) return 0;
        e->fo = fo; snprintf(e->t, sizeof e->t, "%s", it->t); memcpy(e->rgb, rgb, 3); e->ps = ps; e->w = w; e->h = h;
        e->dx = -1.0f / ps;                                         /* the renderer's 1-pixel left margin */
        e->dy = (float)top - ((float)by - (float)cap * ps) / ps;    /* its cap top on the bitmap font's */
        g_nvcache++;
    }
    {
        SDL_FRect dr = {(float)it->x + e->dx, (float)it->y + e->dy, (float)e->w / ps, (float)e->h / ps};
        SDL_RenderCopyF(rd, e->tex, NULL, &dr);
    }
    return 1;
}
static void text_layer_draw(SDL_Renderer *rd)
{
    int i;
    for (i = 0; i < g_ntext; i++) {
        const text_item *it = &g_text[i];
        const char *t;
        float x = (float)it->x;
        int fh = it->fo->d[8];
        if (vec_text(rd, it)) continue;
        for (t = it->t; *t; t++) {
            int gw = 4;
            SDL_Texture *tex = glyph_texture(rd, it, (unsigned char)*t & 127, &gw);
            if (tex) { SDL_FRect dr = {x, (float)it->y, (float)gw, (float)fh}; SDL_RenderCopyF(rd, tex, NULL, &dr); }
            x += (float)gw;
        }
    }
}

/* ---- images ---- */
static shell_image g_bg; static int g_bg_entry = -999;
static void load_bg(int entry)
{
    uint8_t *e;
    long n;
    if (entry == g_bg_entry) return;
    shell_image_free(&g_bg);
    g_bg_entry = entry;
    if (entry < 0) return;
    n = shdb(&g_db, entry, &e);
    if (n > 0) shell_pcx(e, (size_t)n, &g_bg, NULL);
    free(e);
}
static void draw_bg(void)
{
    int i;
    text_layer_clear();
    if (!g_bg.pix) { memset(g_fb, 0, sizeof g_fb); return; }
    for (i = 0; i < W * H && i < g_bg.w * g_bg.h; i++) memcpy(g_fb + (size_t)i * 3, g_bg.pal[g_bg.pix[i]], 3);
}
static smk *open_smk(const char *name)
{
    char p[700];
    {
        char dn[64];
        snprintf(dn, sizeof dn, "SMK\\%s.SMK", name);
        snprintf(p, sizeof p, "%s", gpath(dn));
    }
    return smk_open_file(p);
}
static void blit_smk(smk *s, int x, int y)
{
    int w = smk_width(s), h = smk_height(s), i, j;
    const uint8_t *px = smk_pixels(s);
    const uint8_t (*pal)[3] = smk_palette(s);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int X = x + i, Y = y + j;
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            memcpy(g_fb + ((size_t)Y * W + (size_t)X) * 3, pal[px[(size_t)j * (size_t)w + (size_t)i]], 3);
        }
}

/* ---- the full-screen videos' look (portcfg videos=, Combat Variables VIDEO; src/vidfx.h). The original player
 * (MW2SHELL FUN_00038c70 0x38d74, GBL FUN_0003c430 0x3c53d) sets the 320x200 mode (13h) and copies each frame 1:1: the
 * VGA double-scans it, the monitor fills its 4:3 tube - each pixel a solid block, no black lines. ORIGINAL keeps the
 * port's 640x480 stretch of that (g_fb, nearest, as before); the other looks are drawn at the renderer's real pixel size
 * (the 4:3 area, at most 1920x1440 - 1600x1200 curved - then scaled by the GPU) into their own texture */
static vidfx *g_vfx;
static SDL_Texture *g_vfx_tx;
static int g_vfx_tw, g_vfx_th;
static int video_look(void)
{
    portcfg pc;
    const char *e = getenv("MW2_VIDEOS");   /* tests: override the setting */
    int i;
    if (e) for (i = 0; i < PORTCFG_VID_COUNT; i++) if (!strcmp(e, PORTCFG_VID_NAME[i])) return i;
    portcfg_load(NULL, &pc);
    return pc.videos >= 0 && pc.videos < PORTCFG_VID_COUNT ? pc.videos : PORTCFG_VID_ORIGINAL;
}
/* the frame in the look, in g_vfx_tx (drawn over the whole 640x480 logical screen); NULL: draw g_fb (ORIGINAL) */
static SDL_Texture *video_fx_texture(SDL_Renderer *rd, int look, smk *s)
{
    float sx = 1.0f, sy = 1.0f;
    int ow, oh, maxh = look == PORTCFG_VID_CRT_CURVED ? 1200 : 1440, pitch;
    void *px;
    if (!rd || look == PORTCFG_VID_ORIGINAL || smk_width(s) > W || smk_height(s) > H) return NULL;
    SDL_RenderGetScale(rd, &sx, &sy);
    ow = (int)lroundf((float)W * sx); oh = (int)lroundf((float)H * sy);
    if (oh > maxh) { ow = ow * maxh / oh; oh = maxh; }
    if (ow < W || oh < H) { ow = W; oh = H; }
    if (!g_vfx && !(g_vfx = vidfx_new())) return NULL;
    if (!g_vfx_tx || g_vfx_tw != ow || g_vfx_th != oh) {
        if (g_vfx_tx) SDL_DestroyTexture(g_vfx_tx);
        g_vfx_tx = SDL_CreateTexture(rd, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, ow, oh);
        if (!g_vfx_tx) return NULL;
        SDL_SetTextureScaleMode(g_vfx_tx, SDL_ScaleModeLinear);   /* 1:1 unless capped */
        g_vfx_tw = ow; g_vfx_th = oh;
    }
    if (SDL_LockTexture(g_vfx_tx, NULL, &px, &pitch) != 0) return NULL;
    vidfx_render(g_vfx, look, smk_pixels(s), smk_width(s), smk_height(s), smk_palette(s), (uint8_t *)px, ow, oh, pitch);
    SDL_UnlockTexture(g_vfx_tx);
    return g_vfx_tx;
}
/* tests: MW2_VIDEO_SHOT=file.ppm writes the clip's last decoded frame in the look at MW2_VIDEO_SHOT_SIZE (WxH, default
 * 1280x960) - headless, where nothing is presented; MW2_VIDEO_FRAMES=n decodes n frames headless */
static void video_fx_shot(int look, smk *s)
{
    const char *path = getenv("MW2_VIDEO_SHOT"), *sz = getenv("MW2_VIDEO_SHOT_SIZE");
    int ow = 1280, oh = 960;
    uint8_t *buf;
    vidfx *v;
    FILE *o;
    if (!path || !s) return;
    if (sz && (sscanf(sz, "%dx%d", &ow, &oh) != 2 || ow < W || oh < H || ow > 4096 || oh > 4096)) { ow = 1280; oh = 960; }
    if (!(buf = malloc((size_t)ow * (size_t)oh * 3)) || !(v = vidfx_new())) { free(buf); return; }
    vidfx_render(v, look, smk_pixels(s), smk_width(s), smk_height(s), smk_palette(s), buf, ow, oh, ow * 3);
    if ((o = fopen(path, "wb"))) { fprintf(o, "P6\n%d %d\n255\n", ow, oh); fwrite(buf, 1, (size_t)ow * (size_t)oh * 3, o); fclose(o); }
    vidfx_free(v); free(buf);
}
static int video_headless_frames(int dflt) { const char *e = getenv("MW2_VIDEO_FRAMES"); return e && atoi(e) > 0 ? atoi(e) : dflt; }
static int mission_record(const char *scen, const char *suffix, prj_record *r);
static SDL_Window *g_win;

/* the chosen look's files (gbl's looks DOS / ENHANCED / TITANIUM, Combat Variables RENDERER; gblport.cfg renderer=, held
 * in portcfg as renderer + texset - portcfg_gbl_look), falling back to ENHANCED when that renderer's files are missing and
 * to the original textures when TITANIUM can't be had (no titanium/ folder) */
static struct { char models[PORTCFG_PATH], textures[PORTCFG_PATH], kind[16], sky[PORTCFG_PATH], fog[PORTCFG_PATH]; int ed, tx; } g_edf;
static void edition_files(void)
{
    char kind[PORTCFG_PATH], m[PORTCFG_PATH], t[PORTCFG_PATH], s[PORTCFG_PATH], f[PORTCFG_PATH];
    g_edf.ed = g_cfg.renderer;
    if (portcfg_edition(&g_cfg, g_edf.ed, m, t, kind, s, f) != 0) g_edf.ed = PORTCFG_ED_ENHANCED;
    g_edf.tx = portcfg_texset_ok(&g_cfg, g_edf.ed, g_cfg.texset) ? g_cfg.texset : PORTCFG_TX_ORIGINAL;
    if (portcfg_look(&g_cfg, g_edf.ed, g_edf.tx, g_edf.models, g_edf.textures, kind, g_edf.sky, g_edf.fog) != 0) {
        g_edf.tx = PORTCFG_TX_ORIGINAL;
        portcfg_look(&g_cfg, g_edf.ed, g_edf.tx, g_edf.models, g_edf.textures, kind, g_edf.sky, g_edf.fog);
    }
    snprintf(g_edf.kind, sizeof g_edf.kind, "%.15s", kind);
}
/* the archive the simulation will use (record ids differ between editions): MW2_SIM_PRJ (tests), else the edition's */
static const char *sim_archive(void)
{
    const char *e = getenv("MW2_SIM_PRJ");
    if (e) return e;
    edition_files();
    return g_edf.models[0] ? g_edf.models : gpath("MW2.PRJ");
}

static void setenv_or_unset(const char *k, const char *v) { if (v && v[0]) setenv(k, v, 1); else unsetenv(k); }

/* the simulation's exit: 42 = the in-game MAIN MENU's Flee to Windows (MW2.DLL 0x100642e0) - the game ends there */
static void sim_returned(int rc)
{
#ifdef _WIN32
    int code = rc;
#else
    int code = rc != -1 && WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    if (code == 42) { printf("launch: fled to Windows\n"); SDL_Quit(); exit(0); }
}

/* the simulation's command line: MW2_SIM if set (tests), else this executable with --sim and the look's archives; the
 * child's paths and look go in its environment */
static void sim_command(char *line, size_t n, const char *scen, const char *save)
{
    const char *cmd = getenv("MW2_SIM");
    setenv("MW2_INSTALL_DIR", save, 1);   /* (the only place the child gets its paths) */
    if (g_cfg.cd[0]) {
        size_t k = strlen(g_cfg.cd);
        if (k > 4 && strcasecmp(g_cfg.cd + k - 4, ".iso") == 0) setenv("MW2_CD_IMAGE", g_cfg.cd, 1); else setenv("MW2_CD_DIR", g_cfg.cd, 1);
    }
    edition_files();
    /* the look decides the sky file and the fog (ENHANCED: the Titanium SKYGND.PAR's fog; TITANIUM: its sky file) */
    setenv_or_unset("MW2_SKYGND", g_edf.sky);
    setenv_or_unset("MW2_SKYGND_FOG", g_edf.fog);
    setenv("MW2_EDITION", PORTCFG_ED_NAME[g_edf.ed], 1);
    setenv("MW2_TEXTURES", PORTCFG_TX_NAME[g_edf.tx], 1);
    if (g_cfg.music[0] && !getenv("MW2_MUSIC_DIR")) setenv("MW2_MUSIC_DIR", g_cfg.music, 1);
    if (cmd) snprintf(line, n, cmd, scen);
    else {
        snprintf(line, n, "\"%s\" --sim \"%s\" \"%s\" %s @ %s", g_exe_path, g_edf.models, g_edf.textures, g_edf.kind, scen);
    }
}
/* ---- per-mission records: <first 4 letters><suffix> (BRF1 briefing, BRF2 star setup SDSC / SUPS, DBFS / DBFF
 * aftermath after success / failure); ORDR text read into lines */
static int mission_record(const char *scen, const char *suffix, prj_record *r)
{
    char err[256], path[700], rec[16];
    prj_archive *a;
    int i, rc = -1;
    snprintf(path, sizeof path, "%s", gpath("MW2.PRJ"));
    snprintf(rec, sizeof rec, "%.4s%s", scen, suffix);
    for (i = 0; rec[i]; i++) if (rec[i] >= 'a' && rec[i] <= 'z') rec[i] = (char)(rec[i] - 32);
    if ((a = prj_open(path, err, sizeof err))) { rc = prj_read_named(a, "BWD", rec, r) == PRJ_OK ? 0 : -1; prj_close(a); }
    return rc;
}
static const uint8_t *chunk_find(const prj_record *r, const char *tag, uint32_t *len)
{
    size_t o = 12;
    while (o + 8 <= r->size) {
        uint32_t sz = r->data[o + 4] | r->data[o + 5] << 8 | r->data[o + 6] << 16 | (uint32_t)r->data[o + 7] << 24;
        if (memcmp(r->data + o, tag, 4) == 0) { *len = sz - 8; return r->data + o + 8; }
        if (sz < 8) break;
        o += sz;
    }
    return NULL;
}
static void shell_message(SDL_Renderer *rd, SDL_Texture *tx, int headless, const char *msg);
static int shell_dialog(SDL_Renderer *rd, SDL_Texture *tx, int headless, const char *msg, int def);
static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the engine table (0x7f6cc, 79 x {rating, mass x 100, manufacturer}: ratings 10..400 in steps of 5); the ENGINE page's
 * "Manufactur" line is the entry of the design's engine (0x2bf80: object +0x24 -> the index, + 10000 for an XL -> % 10000) */
static const char *const ENGINE_MAKER[79] = {
    "Omni", "GM", "Pitban", "Omni", "Nissan", "VOX", "GM", "GM", "DAV", "VOX", "Leenex", "Nissan", "Omni", "GM",
    "VOX", "DAV", "DAV", "Nissan", "Hermes", "DAV", "GM", "GM", "GM", "Vlar", "Magna", "Hermes", "Leenex", "Omni",
    "GM", "GM", "LTV", "VOX", "DAV", "Omni", "GM", "GM", "DAV", "Nissan", "Nissan", "Vlar", "GM", "Core Tex", "DAV",
    "VOX", "Leenex", "GM", "Pitban", "Magna", "Magna", "Strand", "Magna", "Vlar", "GM", "Core Tex", "VOX", "Pitban",
    "Omni", "GM", "Vlar", "GM", "Magna", "GM", "Pitban", "VOX", "VOX", "Leenex", "VOX", "Vlar", "Magna", "LTV",
    "Hermes", "Hermes", "Magna", "GM", "GM", "LTV", "Magna", "Hermes", "LTV"};
static const char *engine_maker(int rating) { int i = (rating - 10) / 5; return ENGINE_MAKER[i < 0 ? 0 : i > 78 ? 78 : i]; }
/* the component table (BattleTech construction, Clan technology): matches the original's panel for the retail designs
 * (Mad Dog: engine 300 XL 9.5, gyro 3, cockpit 3, heat sinks 12 (24) 2, internal Std 6, armour Ferro 8.5, weapons 26, ammo 2) */
static double engine_std_tons(int r)
{
    static const double T[] = {   /* standard fusion engine, ratings 10..400 step 5 */
        0.5,0.5,0.5,0.5,1,1,1,1,1.5,1.5,1.5,2,2,2,2.5,2.5,3,3,3,3.5,3.5,4,4,4,4.5,4.5,5,5,5.5,5.5,6,6,6,7,7,7.5,7.5,8,8.5,8.5,
        9,8.5,10,10,10.5,11,11.5,12,12.5,13,13.5,14,14.5,15.5,16,16.5,17.5,18,19,19.5,20.5,21.5,22.5,23.5,24,25.5,27,28.5,29.5,31.5,
        33,34.5,36.5,38.5,41,43.5,46,49,52.5};   /* MW2SHELL table 0x7f6cc {rating, tons x 100, name}: 215 -> 8.5, 330 -> 24 as stored */
    int k = r / 5 - 2;
    if (k < 0) k = 0;
    if (k >= (int)(sizeof T / sizeof T[0])) k = (int)(sizeof T / sizeof T[0]) - 1;
    return T[k];
}
/* armour tons as MW2SHELL (0x314a4): points x 100 / 16 (/ 19.2 Ferro) truncated, then to the NEAREST half ton */
static double armor_tons(int pts, int ferro)
{
    int x = (int)(pts * (ferro ? 1 / 19.2 : 0.0625) * 100.0);
    return ((2 * x + 50) / 100) * 0.5;
}
static int masc_tons(int tonnage) { return (tonnage * 4 + 50) / 100; }   /* 0x31661: tons / 25 rounded half up (tons and slots) */
typedef struct { double engine, gyro, cockpit, sinks, jets, internal, armor, weapons, ammo, equip, used; int rating, xl, nsinks, dissip, njets, endo, ferro, dbl; } mek_mass;
static void mek_masses(const mek_def *d, mek_mass *m)
{
    char nb[64];
    int k, sl, pts = 0;
    memset(m, 0, sizeof *m);
    m->rating = (int)(d->walk_mp * d->tonnage);
    for (k = 0; k < MEK_LOC_COUNT; k++) {
        pts += (int)(d->loc[k].armor + d->loc[k].rear_armor);
        for (sl = 0; sl < d->loc[k].slot_count && sl < MEK_SLOTS; sl++) {
            const char *n;
            if (!d->loc[k].slots[sl]) continue;
            n = mek_item_name(d->loc[k].slots[sl], nb, sizeof nb);
            if ((k == MEK_RT || k == MEK_LT) && strcmp(n, "Engine") == 0) m->xl = 1;
            if (strstr(n, "Ferro")) m->ferro = 1;
            if (strstr(n, "Endo")) m->endo = 1;
        }
    }
    m->engine = m->xl ? engine_std_tons(m->rating) / 2.0 : engine_std_tons(m->rating);   /* XL: exactly half (quarter tons) */
    m->gyro = ceil(m->rating / 100.0);
    m->cockpit = 3;
    /* heat sinks (MW2SHELL load, FUN_00030db0): double when any 6001 slot is present; else double only when the design
     * is over its mass with single sinks, the extra sinks weigh 10 t or more and the count is even; else single */
    m->dissip = (int)d->heat_sinking;
    m->dbl = 0;
    for (k = 0; k < MEK_LOC_COUNT && !m->dbl; k++)
        for (sl = 0; sl < d->loc[k].slot_count && sl < MEK_SLOTS; sl++) if (d->loc[k].slots[sl] == 6001) { m->dbl = 1; break; }
    m->nsinks = m->dbl ? m->dissip / 2 : m->dissip;
    m->sinks = m->nsinks > 10 ? m->nsinks - 10 : 0;
    m->njets = (int)d->jump_mp;
    m->jets = m->njets * (d->tonnage <= 55 ? 0.5 : d->tonnage <= 85 ? 1.0 : 2.0);
    m->internal = m->endo ? d->tonnage / 20.0 : d->tonnage / 10.0;   /* exact, as the shell */
    m->armor = armor_tons(pts, m->ferro);
    for (k = 0; k < d->item_count; k++) {   /* (gl_masses, gbl_lab.c, redoes the weapons, equipment and sinks GBL's way) */
        int w = (int)(d->items[k].id / 100);
        if (d->items[k].id >= 5000 || w >= mek_weapon_table_count()) continue;
        if (w == 29) m->equip += mek_weapon_table()[w].weight_x100 / 100.0;   /* the anti-missile system: equipment (ASSUMED) */
        else m->weapons += mek_weapon_table()[w].weight_x100 / 100.0;
    }
    m->ammo = d->link_count;
    m->used = m->engine + m->gyro + m->cockpit + m->sinks + m->jets + m->internal + m->armor + m->weapons + m->ammo + m->equip;
    if (!m->dbl && m->used > d->tonnage + 1e-9 && m->sinks >= 10 && m->dissip % 2 == 0) {
        m->dbl = 1; m->nsinks = m->dissip / 2; m->sinks = m->nsinks > 10 ? m->nsinks - 10 : 0;
        m->used = m->engine + m->gyro + m->cockpit + m->sinks + m->jets + m->internal + m->armor + m->weapons + m->ammo + m->equip;
    }
}
static int load_mek(const char *loadout, mek_def *d)
{
    char err[256], up[16];
    prj_archive *a;
    int k, rc;
    snprintf(up, sizeof up, "%s", loadout);
    for (k = 0; up[k]; k++) if (up[k] >= 'a' && up[k] <= 'z') up[k] = (char)(up[k] - 32);
    a = prj_open(gpath("MW2.PRJ"), err, sizeof err);
    rc = mek_load(a, up, d);   /* the archive, else MEK\<name>.MEK */
    if (a) prj_close(a);
    return rc;
}

/* ==== the Mech Lab editor (CUSTOMIZE): laid out from DOS captures (docs/reference/dos_customize_*.png).
 * A working copy of the design plus a pool of unassigned critical slots (one entry per slot). Clan rules:
 * engine rating = walk MP x tonnage (FASTER / SLOWER), rating / 25 heat sinks inside the engine (no slots), the first 10
 * sinks free; double heat sinks 2 slots; jump jets 1 slot each (at most walk MP); XL engine 2 slots in each side torso;
 * Endo-steel 7 slots (half the internal mass); Ferro-fibrous 7 slots (19.2 points a ton, else 16); armour bought in half
 * tons, at most 9 in the head and twice the internal structure elsewhere; weapons their table's slots; ammo 1 slot a ton;
 * MASC tonnage / 25 tons and slots (ASSUMED Clan figures). Slot ids as in the MEK data (equipment base + instance / side). */
typedef struct {
    mek_def  d;
    int      halftons;            /* armour bought, half tons */
    uint16_t pool[128];
    int      npool;
    int      page;                /* 0 engine, 3 heat sinks, 4 jump jets, 5 internal, 6 armor, 7 weapons, 8 ammo, 9 equipment, 10 criticals */
    int      armor_loc, crit_loc, sel_fit, sel_table, sel_pool;
    int      masc;                /* tons */
    int      dbl;                 /* double heat sinks (else single: 1 slot, dissipation 1 each) */
} medit;
/* MW2SHELL's location names (0x7f57c), used by index as they stand: 1 Right Torso, 3 Left Torso, 4 Right Arm, 5 Left Arm,
 * 6 Right Leg, 7 Left Leg (the engine's "... Selected" list agrees). mek.h's MEK_RT / MEK_LT etc. name the other way round. */
static const int ARMOR_ORDER[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const char *ARMOR_NAME[8] = {"Head", "Right Torso", "Center Torso", "Left Torso", "Right Arm", "Left Arm", "Right Leg", "Left Leg"};
static const int INTERNAL_ORDER[8] = {0, 1, 2, 3, 4, 5, 6, 7};   /* as the Armor page (DOS) */
static const char *LOC_NAME[8] = {"Head", "Right Torso", "Center Torso", "Left Torso", "Right Arm", "Left Arm", "Right Leg", "Left Leg"};

static int ed_rating(const medit *e) { return (int)(e->d.walk_mp * e->d.tonnage); }
static double ed_ppt(const medit *e, int ferro) { (void)e; return ferro ? 19.2 : 16.0; }
static int ed_count(const medit *e, uint16_t id)   /* slots holding id, placed or pooled */
{
    int n = 0, l, k;
    for (l = 0; l < MEK_LOC_COUNT; l++) for (k = 0; k < e->d.loc[l].slot_count; k++) if (e->d.loc[l].slots[k] == id) n++;
    for (k = 0; k < e->npool; k++) if (e->pool[k] == id) n++;
    return n;
}
static int ed_has_base(const medit *e, int base)   /* any slot with an id in base+1 .. base+49 */
{
    int k;
    for (k = 1; k < 50; k++) if (ed_count(e, (uint16_t)(base + k))) return 1;
    return 0;
}
static uint16_t ed_free_id(const medit *e, int base)
{
    int k;
    for (k = 1; k < 99; k++) if (!ed_count(e, (uint16_t)(base + k))) return (uint16_t)(base + k);
    return (uint16_t)(base + 99);
}
static void ed_pool_add(medit *e, uint16_t id, int n) { while (n-- > 0 && e->npool < 128) e->pool[e->npool++] = id; }
static int ed_remove(medit *e, uint16_t id, int n)   /* n slots of id: the pool first, then the 'Mech; returns how many */
{
    int k, l, done = 0;
    for (k = e->npool - 1; k >= 0 && done < n; k--) if (e->pool[k] == id) { memmove(e->pool + k, e->pool + k + 1, (size_t)(e->npool - k - 1) * 2); e->npool--; done++; }
    for (l = MEK_LOC_COUNT - 1; l >= 0 && done < n; l--) for (k = e->d.loc[l].slot_count - 1; k >= 0 && done < n; k--) if (e->d.loc[l].slots[k] == id) { e->d.loc[l].slots[k] = 0; done++; }
    return done;
}
static int ed_is_xl(const medit *e)
{
    int k;
    for (k = 0; k < e->d.loc[MEK_RT].slot_count; k++) if (e->d.loc[MEK_RT].slots[k] == 5850) return 1;
    return 0;
}
static void ed_sync_sinks(medit *e)   /* heat sinks beyond the engine's capacity (rating / 25) take 2 slots each if double, else 1 */
{
    int per = e->dbl ? 2 : 1, need = (int)e->d.heat_sinking / per - ed_rating(e) / 25, have = 0, k;
    if (need < 0) need = 0;
    for (k = 1; k < 99; k++) if (ed_count(e, (uint16_t)(6000 + k))) have++;
    while (have < need) { ed_pool_add(e, ed_free_id(e, 6000), per); have++; }
    while (have > need) { for (k = 98; k >= 1; k--) if (ed_count(e, (uint16_t)(6000 + k))) { ed_remove(e, (uint16_t)(6000 + k), 9); break; } have--; }
}
static int ed_armor_alloc(const medit *e)
{
    int l, n = 0;
    for (l = 0; l < MEK_LOC_COUNT; l++) n += (int)(e->d.loc[l].armor + e->d.loc[l].rear_armor);
    return n;
}
static int ed_ferro(const medit *e) { return ed_has_base(e, 9000); }
static int ed_endo(const medit *e) { return ed_has_base(e, 8000); }
static int ed_factor(const medit *e)   /* points the bought armour gives (0x2bbd0): (round(tons x 100 x 16 or 19.2) + 49) / 100 */
{
    return ((int)lround(e->halftons * 50.0 * ed_ppt(e, ed_ferro(e))) + 49) / 100;
}
static void ed_trim_armor(medit *e)   /* FUN_0002bbd0: take points off until the allocation fits, round-robin from a
                                        * remembered location, front then rear in each */
{
    static int at;
    int alloc = 0, l, f = ed_factor(e);
    for (l = 0; l < MEK_LOC_COUNT; l++) alloc += (int)(e->d.loc[l].armor + e->d.loc[l].rear_armor);
    while (alloc > f) {
        mek_loc *L = &e->d.loc[at];
        if (L->armor > 0) { L->armor--; alloc--; }
        if (alloc <= f) break;
        if (L->rear_armor > 0) { L->rear_armor--; alloc--; }
        if (++at > 7) at = 0;
    }
}
static int ed_loc_max(const medit *e, int l) { return l == MEK_HEAD ? 9 : 2 * (int)e->d.loc[l].internal; }
static void ed_init(medit *e, const mek_def *d)
{
    memset(e, 0, sizeof *e);
    e->d = *d;
    e->halftons = (int)(armor_tons(ed_armor_alloc(e), ed_ferro(e)) * 2.0 + 0.5);   /* as the shell's load (nearest half ton) */
    e->sel_fit = e->sel_table = e->sel_pool = -1;
    e->crit_loc = MEK_HEAD;
    if (ed_count(e, 5001)) e->masc = masc_tons((int)e->d.tonnage);
    { mek_mass mm; mek_masses(&e->d, &mm); e->dbl = mm.dbl; }
}
/* the shell's limits (0x2efdc..): 10 weapons, 10 ammo bins a weapon, 25 bins in all, 78 unassigned entries */
static int ed_ammo_bins(const medit *e, uint32_t wid)
{
    int k, n = 0;
    for (k = e->d.item_count; k < e->d.item_count + e->d.link_count; k++) if (e->d.items[k].linked_id == (int32_t)wid) n++;
    return n;
}
static void ed_del_ammo(medit *e, uint32_t wid)
{
    mek_def *d = &e->d;
    int k;
    for (k = d->item_count + d->link_count - 1; k >= d->item_count; k--)
        if (d->items[k].linked_id == (int32_t)wid) {
            memmove(d->items + k, d->items + k + 1, (size_t)(d->item_count + d->link_count - k - 1) * sizeof d->items[0]);
            d->link_count--;
            ed_remove(e, (uint16_t)(10000 + wid), 1);
            return;
        }
}
static void ed_del_weapon(medit *e, int item)
{
    mek_def *d = &e->d;
    uint32_t id = d->items[item].id;
    int k;
    for (k = 0; k < 40; k++) ed_del_ammo(e, id);
    ed_remove(e, (uint16_t)id, 99);
    memmove(d->items + item, d->items + item + 1, (size_t)(d->item_count + d->link_count - item - 1) * sizeof d->items[0]);
    d->item_count--;
}
static int ed_removable(uint16_t id)   /* items the player may move: not the structure */
{
    return id && !(id >= 5300 && id < 6000 && !(id >= 5400 && id < 5500));
}
static int ed_toggle_actuator(medit *e, int arm, int hand)   /* arm: location 4 (right, 5401 / 5451) or 5 (left, 5402 / 5452) */
{   /* 0x300d0 / 0x30186: the lower arm always in slot 2, the hand in slot 3 - whatever is there goes to the unassigned
     * list; the two are independent (removing one leaves the other) */
    mek_loc *l = &e->d.loc[arm];
    uint16_t side = (uint16_t)(arm == 4 ? 1 : 2), want = (uint16_t)((hand ? 5450 : 5400) + side);
    int k, at = hand ? 3 : 2;
    for (k = 0; k < l->slot_count; k++) if (l->slots[k] == want) { l->slots[k] = 0; return 1; }
    if (at >= l->slot_count) return 0;
    if (l->slots[at]) {
        uint16_t was = l->slots[at];
        int n = 0;
        for (k = 0; k < l->slot_count; k++) if (l->slots[k] == was) { l->slots[k] = 0; n++; }
        ed_pool_add(e, was, n);
    }
    l->slots[at] = want;
    return 1;
}
static int ed_has_actuator(const medit *e, int arm, int hand)
{
    uint16_t id = (uint16_t)((hand ? 5450 : 5400) + (arm == 4 ? 1 : 2));
    int k;
    for (k = 0; k < e->d.loc[arm].slot_count; k++) if (e->d.loc[arm].slots[k] == id) return 1;
    return 0;
}
static int ed_single(uint16_t id);
/* assign the selected pool item into location loc (FUN_0002ae40): the whole item into the location's first free slots,
 * not necessarily together; ammo bins one at a time. Returns 1 placed, -1 not enough free slots, -2 jump jets outside
 * the torso / legs (0x2fbad) */
static int ed_assign(medit *e, int loc, int s)
{
    mek_loc *l = &e->d.loc[loc];
    uint16_t id;
    int n, k, need, nfree = 0;
    (void)s;
    if (e->sel_pool < 0 || e->sel_pool >= e->npool) return 0;
    id = e->pool[e->sel_pool];
    if (id >= 7000 && id < 8000 && (loc == 0 || loc == 4 || loc == 5)) return -2;
    for (n = 0, k = 0; k < e->npool; k++) if (e->pool[k] == id) n++;
    need = id >= 10000 ? 1 : n;
    for (k = 0; k < l->slot_count; k++) if (!l->slots[k]) nfree++;
    if (nfree < need) return -1;
    for (k = 0, n = 0; k < l->slot_count && n < need; k++) if (!l->slots[k]) { l->slots[k] = id; n++; }
    for (k = 0; k < need; k++) { int j; for (j = 0; j < e->npool; j++) if (e->pool[j] == id) { memmove(e->pool + j, e->pool + j + 1, (size_t)(e->npool - j - 1) * 2); e->npool--; break; } }
    if (e->sel_pool >= e->npool) e->sel_pool = e->npool - 1;
    return 1;
}
static int ed_single(uint16_t id) { return (id >= 7000 && id < 10000) || id >= 10000; }   /* jets, Endo, Ferro, ammo */
static void ed_unassign(medit *e, int loc, int s)
{
    uint16_t id = e->d.loc[loc].slots[s];
    int l, k;
    if (!ed_removable(id) || (id >= 5400 && id < 5500)) return;   /* structure; actuators belong to the EQUIPMENT page */
    if (ed_single(id)) { e->d.loc[loc].slots[s] = 0; ed_pool_add(e, id, 1); return; }
    for (l = 0; l < MEK_LOC_COUNT; l++) for (k = 0; k < e->d.loc[l].slot_count; k++)
        if (e->d.loc[l].slots[k] == id) { e->d.loc[l].slots[k] = 0; ed_pool_add(e, id, 1); }
}
static const int DIAG_ORDER[8] = {4,5,6,7,1,3,2,0};   /* the diagram's pieces: piece p is location p */

static void shell_message(SDL_Renderer *rd, SDL_Texture *tx, int headless, const char *msg);

/* little-endian fields (MW2DIF.CFG / MW2SND.CFG values) */
static int32_t rd32(const uint8_t *b) { return (int32_t)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24); }
static void wr32(uint8_t *b, int32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }

/* a DATABASE SHP entry's first frame at (x, y) in the screen's palette (the global menu panel is shell image 7 = entry 6,
 * drawn at its top-left: FUN_0003eac0(.., 0xc6, 0x74, 0xf4, 0xf8)) */
static void db_shp(int entry, int x, int y)
{
    uint8_t *e = NULL;
    long n = shdb(&g_db, entry, &e);
    shp_frame fr;
    if (n > 0 && shp_decode(e, (size_t)n, 0, &fr) == 0) {
        int X, Y;
        for (Y = 0; Y < fr.h; Y++)
            for (X = 0; X < fr.w; X++) {
                int px = x + X, py = y + Y;
                if (!fr.mask[Y * fr.w + X] || px < 0 || py < 0 || px >= W || py >= H) continue;
                memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, g_bg.pal[fr.pix[Y * fr.w + X]], 3);
            }
        shp_frame_free(&fr);
    }
    free(e);
}

/* ---- Cockpit Controls (FUN_00021020; data and the INPUT.MAP writer in src/cockpitcfg.c). Background shell image 4
 * (entry 3) above y 95 (the logo and "COCKPIT CONTROLS"), two red column rules at x 172 and 477, the thin font (31):
 * white, grey (154) and red (255,16,24), rows 11 px apart.
 * Page 1 (table 0x7c6c0): "Current Config:" (0,110), SELECT INPUT DEVICES (0,132), the drivers from (0,154) - red =
 *   selected, a click toggles; ACCEPT (0,297): the selected devices' default configs merged, INPUT.MAP written;
 *   CUSTOM CONFIGURATION (0,319): the editor; ABORT (0,440).
 * Page 2 (table 0x7a9b4): INPUT DEVICES (0,110), drivers from (0,132) (red = the one listed on the right); Current
 *   Config (0,275) and its name (8,286); RESET DEFAULTS (0,308); Load Custom 1-4 (0,330..); Save Custom 1-4 (0,385..);
 *   ACCEPT CONFIG AND EXIT (0,440); ABORT (0,462). GAME CONTROLS (176,110) and the level ("Primary Controls" ..
 *   "Quaternary Controls", centred on 370) - a click on it changes the level; from (176,132) each control: label,
 *   direction ("+/-", "L/R", ...: a click reverses), modifier at 261 ("--", "Ctrl", "Alt", "Shft": a click cycles),
 *   the binding at 285. A click on a control then on an input at the right assigns it (two-button controls take two
 *   inputs: the first then the second); right button on a control clears it. Right: the device (480,110),
 *   "Directional" (480,132) and its axes, then "Buttons" and its buttons (the keyboard's list scrolls with the wheel).
 * Layout measured on DOS captures (docs/reference/dos_cockpit_*.png); the colours of selected / current items and the
 * click behaviour inferred from the captures. */
static cc_device g_ccdev[CC_DEVICES];
static int g_ccsel[CC_DEVICES], g_ccsel_init;
static char g_ccname[64] = "Default Config";
static int cc_colour(int r, int g, int b)
{
    int i, best = 0, bd = 1 << 30;
    for (i = 0; i < 256; i++) { int d = abs(g_bg.pal[i][0] - r) + abs(g_bg.pal[i][1] - g) + abs(g_bg.pal[i][2] - b); if (d < bd) { bd = d; best = i; } }
    return best;
}
static const char *giddi_dir(void) { static char p[1100]; snprintf(p, sizeof p, "%s", gpath("GIDDI")); return p; }
static void cc_init(void)
{
    FILE *f;
    char line[256];
    int k;
    cc_devices(g_ccdev, giddi_dir());
    if (g_ccsel_init) return;
    g_ccsel_init = 1;
    if ((f = fopen(gpath("INPUT.MAP"), "rb"))) {   /* the devices the current INPUT.MAP uses */
        while (fgets(line, sizeof line, f)) {
            char sg[8], dv[32];
            if (sscanf(line, " %7s %31s", sg, dv) == 2 && (sg[0] == '+' || sg[0] == '-'))
                for (k = 0; k < CC_DEVICES; k++) if (strcmp(dv, CC_DEVNAME[k]) == 0) g_ccsel[k] = 1;
        }
        fclose(f);
    } else g_ccsel[2] = 1;
}
static int cc_write_inputmap(SDL_Renderer *rd, SDL_Texture *tx, int headless, const cc_config *c)
{
    static char out[65536];
    size_t n = cc_generate(c, g_ccdev, g_ccsel, out, sizeof out);
    char path[1100], bak[1100];
    FILE *f;
    if (!n) { shell_message(rd, tx, headless, "Error: Sim can not|handle that many controls.#Ok"); return -1; }
    snprintf(path, sizeof path, "%s/INPUT.MAP", g_dir);
    snprintf(bak, sizeof bak, "%s/INPUT.BAK", g_dir);
    remove(bak);
    rename(path, bak);                               /* input.map -> input.bak, as the original */
    if (!(f = fopen(path, "wb")) || fwrite(out, 1, n, f) != n) { if (f) fclose(f); rename(bak, path); return -1; }
    fclose(f);
    shell_message(rd, tx, headless, "Cockpit Control Configured.#Ok");
    return 0;
}
static smk *g_cclogo;          /* the amwlogo1 clip at (120,4), as FUN_00021020 plays it on these pages */
static Uint32 g_cclogo_t;
static void cc_frame(int lines)
{
    int x, y;
    draw_bg();
    if (g_cclogo) {
        Uint32 now = SDL_GetTicks();
        if (!g_cclogo_t || now - g_cclogo_t >= (Uint32)smk_frame_ms(g_cclogo)) { smk_next(g_cclogo); g_cclogo_t = now; }
        blit_smk(g_cclogo, 120, 4);
    }
    for (y = 105; y < H; y++) memset(g_fb + (size_t)y * W * 3, 0, (size_t)W * 3);
    text_layer_cull(0, 105, W, H);
    if (lines) {
        int red = cc_colour(255, 16, 24);
        for (y = 120; y <= 470; y++) for (x = 0; x < 2; x++) memcpy(g_fb + ((size_t)y * W + (size_t)(x ? 477 : 172)) * 3, g_bg.pal[red], 3);
    }
}
static void cc_present(SDL_Renderer *rd, SDL_Texture *tx)
{
    SDL_UpdateTexture(tx, NULL, g_fb, W * 3);
    SDL_RenderClear(rd); SDL_RenderCopy(rd, tx, NULL, NULL); text_layer_draw(rd); SDL_RenderPresent(rd);
    SDL_Delay(15);
}
static void cc_text(int x, int y, const char *t, int col) { draw_text(&g_fthin, x, y, t, (const uint8_t (*)[3])g_bg.pal, col); }

static void cc_editor(SDL_Renderer *rd, SDL_Texture *tx, int headless, cc_config *cfg)
{
    static const char *LEVEL[4] = {"Primary Controls", "Secondary Controls", "Tertiary Controls", "Quaternary Controls"};
    static const char *DIRL[7][2] = {{"-/+", "+/-"}, {"L/R", "R/L"}, {"L/R", "R/L"}, {"D/U", "U/D"}, {"L/R", "R/L"}, {"D/U", "U/D"}, {"I/O", "O/I"}};
    static const char *MODL[5] = {"--", "Ctrl", "Alt", "--", "Shft"};
    int done = 0, level = 0, shown = 2, sel = -1, half = 0, scroll = 0, k, f = 0;
    int white = -1, grey = cc_colour(154, 154, 154), red = cc_colour(255, 16, 24);
    char msg[64] = "";
    while (!done) {
        SDL_Event e;
        const cc_device *d = &g_ccdev[shown];
        int mx = -1, my = -1, button = 0, wheel = 0;
        cc_frame(1);
        cc_text(0, 110, "INPUT DEVICES", grey);
        for (k = 0; k < CC_DEVICES; k++) cc_text(0, 132 + 11 * k, g_ccdev[k].title, k == shown ? red : g_ccsel[k] ? white : grey);   /* DOS: the selected white, others grey */
        cc_text(0, 275, "Current Config:", grey);
        cc_text(8, 286, cfg->name, white);
        cc_text(0, 308, "RESET DEFAULTS", white);
        for (k = 0; k < 4; k++) { char t[24]; snprintf(t, sizeof t, "Load Custom %d", k + 1); cc_text(0, 330 + 11 * k, t, white);
                                  snprintf(t, sizeof t, "Save Custom %d", k + 1); cc_text(0, 385 + 11 * k, t, white); }
        cc_text(0, 440, "ACCEPT CONFIG AND EXIT", white);
        cc_text(0, 462, "ABORT", white);
        cc_text(175, 110, "GAME CONTROLS", grey);
        cc_text(367 - text_width(&g_fthin, LEVEL[level]) / 2, 110, LEVEL[level], white);
        for (k = 0; k < CC_CONTROLS; k++) {
            const cc_binding *b = &cfg->b[level][k];
            char t[96];
            int y = 132 + 11 * k, x = 175 + text_width(&g_fthin, CC_LABEL[k]);
            cc_text(175, y, CC_LABEL[k], k == sel ? red : grey);
            if (k <= 6) cc_text(x + 1, y, DIRL[k][(b->flags & 0x80000000u) ? 1 : 0], k == sel ? red : white);   /* 1 px after the label */
            { const char *mt = b->device >= 0 ? MODL[b->flags & 7] : "--"; cc_text(270 - text_width(&g_fthin, mt) / 2, y, mt, white); }   /* centred on 270 */
            cc_describe(b, g_ccdev, t, sizeof t);
            if (k == sel && half) cc_text(285, y, "?", white);
            else {   /* a pair: "dev a" then "/b" one pixel further (DOS capture) */
                char *sl = b->type == 2 ? strchr(t, '/') : NULL;
                if (sl) { *sl = 0; cc_text(285, y, t, white); cc_text(285 + text_width(&g_fthin, t) + 1, y, "/", white); cc_text(285 + text_width(&g_fthin, t) + 1 + text_width(&g_fthin, "/"), y, sl + 1, white); }
                else cc_text(285, y, t, white);
            }
        }
        cc_text(480, 110, d->title, grey);
        {
            int y = 132, i;
            cc_text(480, y, "Directional", grey); y += 22;
            for (i = 0; i < d->naxis; i++, y += 11) cc_text(480, y, d->axis[i].label, grey);
            y = 242;
            cc_text(480, y, "Buttons", grey); y += 22;
            for (i = scroll; i < d->nbutton && y < 470; i++, y += 11) cc_text(480, y, d->button[i].label, grey);
        }
        if (msg[0]) cc_text(176, 470, msg, red);
        f++;
        if (headless) { if (f >= 2) done = 1; continue; }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = 1; break; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) done = 1;
            if (e.type == SDL_MOUSEWHEEL) wheel = e.wheel.y;
            if (e.type == SDL_MOUSEBUTTONDOWN) { mx = e.button.x; my = e.button.y; button = e.button.button; }
        }
        if (wheel && mx < 0) { scroll -= wheel * 3; if (scroll < 0) scroll = 0; if (scroll > d->nbutton - 10) scroll = d->nbutton > 10 ? d->nbutton - 10 : 0; }
        if (mx >= 0) {
            int row = (my - 132) / 11;
            msg[0] = 0;
            if (mx < 172) {
                if (my >= 132 && my < 132 + 11 * CC_DEVICES) { shown = row; scroll = 0; }
                else if (my >= 308 && my < 319) { cc_merge_defaults(cfg, giddi_dir(), g_ccsel); }
                else if (my >= 330 && my < 374) {
                    char p[1100];
                    snprintf(p, sizeof p, "%.1000s/CONFIG%02d.CPC", giddi_dir(), (my - 330) / 11 + 1);
                    if (cc_load(cfg, p) == 0) snprintf(g_ccname, sizeof g_ccname, "%s", cfg->name); else snprintf(msg, sizeof msg, "No configuration saved there.");
                } else if (my >= 385 && my < 429) {
                    char p[1100], m[64];
                    int n = (my - 385) / 11 + 1;
                    snprintf(cfg->name, sizeof cfg->name, "Custom Config #%d", n);
                    snprintf(p, sizeof p, "%.1000s/CONFIG%02d.CPC", giddi_dir(), n);
                    if (cc_save(cfg, p) == 0) { snprintf(m, sizeof m, "Configuration %d Saved.#Ok", n); shell_message(rd, tx, headless, m); }
                } else if (my >= 440 && my < 451) { if (cc_write_inputmap(rd, tx, headless, cfg) == 0) { snprintf(g_ccname, sizeof g_ccname, "%s", cfg->name); done = 1; } }
                else if (my >= 462 && my < 473) done = 1;
            } else if (mx < 477) {
                if (my >= 110 && my < 121) level = (level + 1) % CC_LEVELS;
                else if (row >= 0 && row < CC_CONTROLS && my >= 132) {
                    cc_binding *b = &cfg->b[level][row];
                    int lw = 175 + text_width(&g_fthin, CC_LABEL[row]);
                    if (button == SDL_BUTTON_RIGHT) { b->device = -1; b->flags = 0; sel = -1; }
                    else if (mx >= 250 && mx < 285) { int m = (int)(b->flags & 7); m = m == 0 ? 1 : m == 1 ? 2 : m == 2 ? 4 : 0; b->flags = (b->flags & ~7u) | (uint32_t)m; }
                    else if (row <= 6 && mx >= lw && mx < lw + 24) b->flags ^= 0x80000000u;
                    else { sel = row; half = 0; }
                }
            } else if (sel >= 0) {   /* an input of the shown device */
                cc_binding *b = &cfg->b[level][sel];
                int y = 154, i, hit = -1, axis = 0;
                for (i = 0; i < d->naxis; i++, y += 11) if (my >= y && my < y + 11) { hit = i; axis = 1; }
                y = 264;
                for (i = scroll; i < d->nbutton && y < 470; i++, y += 11) if (my >= y && my < y + 11) { hit = i; axis = 0; }
                if (hit >= 0) {
                    if (axis && sel <= 6) { b->type = 0; b->device = shown; b->a = hit; b->b = 0; sel = -1; }
                    else if (!axis && sel <= 6) {
                        if (!half) { b->type = 2; b->device = shown; b->a = hit; b->b = hit; half = 1; }
                        else { b->b = hit; half = 0; sel = -1; }
                    } else if (!axis) { b->type = 1; b->device = shown; b->a = hit; b->b = shown == 2 ? -1 : 0; sel = -1; }
                    else snprintf(msg, sizeof msg, "That control takes a button.");
                }
            }
        }
        cc_present(rd, tx);
    }
}

/* The shell's dialog (FUN_000263d0; the global menu's FLEE TO DOS the same): "line|line#Button|Button". Panel = shell
 * image 6 (DATABASE entry 5) frame 2, 296 x 150 at (172,165); lines centred on x 320 from y 197, 16 apart, in shell font
 * 0x20 (entry 31); n buttons centred on y 276 at x 320 - 30 (n - 1) + 61 k, each frame 0 (frame 1 while pressed) at
 * (x - 26, 263) with its label centred. Enter / Esc take the default button (the last for Yes / No, as FLEE TO DOS's
 * local default 1), a key equal to a label's first letter that button, a click a button. Returns the button. */
static void db_shp_frame(int entry, int frame, int x, int y)
{
    uint8_t *e = NULL;
    long n = shdb(&g_db, entry, &e);
    shp_frame fr;
    if (n > 0 && shp_decode(e, (size_t)n, frame, &fr) == 0) {
        int X, Y;
        for (Y = 0; Y < fr.h; Y++)
            for (X = 0; X < fr.w; X++) {
                int px = x + X, py = y + Y;
                if (!fr.mask[Y * fr.w + X] || px < 0 || py < 0 || px >= W || py >= H) continue;
                memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, g_bg.pal[fr.pix[Y * fr.w + X]], 3);
            }
        shp_frame_free(&fr);
    }
    free(e);
}
static int shell_dialog(SDL_Renderer *rd, SDL_Texture *tx, int headless, const char *msg, int def)
{
    char buf[256], *lines[8], *btn[4];
    int n = 0, nb = 0, i, chosen = -1, pressed = -1;
    char *p2;
    uint8_t *saved = malloc(sizeof g_fb);
    const uint8_t (*pal)[3] = (const uint8_t (*)[3])g_bg.pal;
    int n0 = g_ntext;
    snprintf(buf, sizeof buf, "%s", msg);
    p2 = strchr(buf, '#');
    if (p2) {
        char *b = p2 + 1;
        *p2 = 0;
        while (b && nb < 4) { btn[nb++] = b; b = strchr(b, '|'); if (b) *b++ = 0; }
    }
    p2 = buf;
    while (p2 && n < 8) { lines[n++] = p2; p2 = strchr(p2, '|'); if (p2) *p2++ = 0; }
    if (def < 0 || def >= nb) def = nb > 0 ? nb - 1 : 0;
    if (saved) memcpy(saved, g_fb, sizeof g_fb);
    while (chosen < 0) {
        SDL_Event e;
        if (saved) memcpy(g_fb, saved, sizeof g_fb);
        g_ntext = n0;
        db_shp_frame(5, 2, 172, 165);
        text_layer_cull(172, 165, 172 + 296, 165 + 150);
        for (i = 0; i < n; i++) draw_text(&g_fthin, 320 - text_width(&g_fthin, lines[i]) / 2, 197 + 16 * i, lines[i], pal, -1);
        for (i = 0; i < nb; i++) {
            int cx = 320 - 30 * (nb - 1) + 61 * i;
            db_shp_frame(5, i == pressed ? 1 : 0, cx - 26, 263);
            draw_text(&g_fthin, cx - text_width(&g_fthin, btn[i]) / 2, 276 - font_height(&g_fthin) / 2, btn[i], pal, -1);
        }
        if (headless) { chosen = def; break; }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) chosen = def;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) chosen = def;
                else for (i = 0; i < nb; i++) if (toupper((unsigned char)btn[i][0]) == toupper((int)k & 0x7f)) chosen = i;
                if (nb == 0) chosen = 0;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                int hit = -1;
                for (i = 0; i < nb; i++) { int cx = 320 - 30 * (nb - 1) + 61 * i; if (e.button.x >= cx - 26 && e.button.x <= cx + 26 && e.button.y >= 263 && e.button.y <= 289) hit = i; }
                if (e.type == SDL_MOUSEBUTTONDOWN) pressed = hit;
                else { if (hit >= 0 && hit == pressed) chosen = hit; pressed = -1; }
            }
        }
        SDL_UpdateTexture(tx, NULL, g_fb, W * 3);
        SDL_RenderClear(rd); SDL_RenderCopy(rd, tx, NULL, NULL); text_layer_draw(rd); SDL_RenderPresent(rd);
        SDL_Delay(10);
    }
    if (saved && !headless) { memcpy(g_fb, saved, sizeof g_fb); g_ntext = n0; }
    free(saved);
    return chosen;
}
static void shell_message(SDL_Renderer *rd, SDL_Texture *tx, int headless, const char *msg) { shell_dialog(rd, tx, headless, msg, 0); }
