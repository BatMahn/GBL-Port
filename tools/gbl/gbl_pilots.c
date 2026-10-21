/*
 * gbl_pilots.c - the Sibko roster (screen 12), the Ghost Bear halls (1), the TSENG BLOODNAME roll (16), the developer
 * easter egg (17), and the shell's text pages: the BWD text loader, the text layout (FUN_0001c600 + FUN_0003f4d0) and
 * the paged full-screen text viewer (FUN_0002b0e0 + FUN_0002a590). Spec: docs/gbl/B_roster_hall.md, C_campaign.md 4.1 /
 * 4.5. Addresses are GBL MW2SHELL.EXE VAs.
 *
 * ---- Interface for the other screens (Mission Computer DETAILS, debriefing AFTERMATH) ---------------------------------
 *
 *   char *gbl_bwd_text(const char *record, const char *q)
 *       MW2.PRJ BWD record (e.g. "BASSBRF1", "FERREGG1"; case does not matter), its ORDR chunk as a malloc'd string
 *       (free() it), or NULL when the record / chunk is missing. With q != NULL the markup of FUN_0001c600 is replaced
 *       first: \h \H = the active pilot's honor ("%d"), \q \Q = q, \r0 \r1 \r2 (\R..) = the rank title of the active
 *       pilot's rank + 0 / 1 / 2 (capped at table entry 7, as the original). q == NULL: the text as stored.
 *
 *   int gbl_text_pages(const char *txt, int fe, int x, int y, int w, int h, gbl_text *out)
 *       Lays txt out as the original's page collection (FUN_0001c600's loop over FUN_0003f4d0): font DATABASE entry fe
 *       (the shell uses 33 for all its text pages), box (x, y) w x h, h < 0 = one unlimited page. \n line, \c centre the
 *       line at its \n, \s page break, \t next tab stop (36 px), \bNNN \gNNN kept for the renderer (their value is
 *       taken off the line width), \a skipped; words wrap when the word and its trailing spaces pass w; the text's
 *       last word (after its last escape) is lost, as in the original (see gp_layout_page). Returns the page count
 *       (also out->npages). out is large (make it static).
 *   void gbl_text_draw(const gbl_text *t, int page)        draws one page (the text objects at their positions)
 *
 *   int gbl_pager(int bg_idx, const char *text, int x, int y, int w, int h, const gbl_hot *hot4)
 *       The paged full-screen viewer: loads background bg_idx (1-based DATABASE index, e.g. 0x16 "DETAILS"), lays text
 *       out in the box (font 33), shows page 0 and runs until it ends. Buttons (clip slots 26-31, FUN_0002a590): previous
 *       wgbam001 / wgbamg01 (116,437), EXIT wgbam002 / wgbamg02 (278,437), next wgbam003 / wgbamg03 (436,437); the "g"
 *       picture is the one under the pointer, a button without a page to go to is not drawn. hot4 = the 4 hotspots
 *       (0 EXIT, 1 previous, 2 next, 3 off-screen; NULL = GBL_PAGER_HOT, the table 0x802a0 = 0x80260). Keys: Home = first
 *       page (on the first page: leaves, returning -2), PgUp = previous, PgDn = next; Return = a click (as everywhere).
 *       The shared Esc menu runs every frame. Returns -5 (EXIT), -2 (Home on the first page), or the Esc menu's result
 *       (8 / -3 ...). The caller's clip slots keep running and drawing (FUN_0003ca50 draws every slot) - hide your own
 *       slots before calling, as the original's callers do; reload your background afterwards.
 *   int gbl_pager_pages(int bg_idx, const gbl_text *t, const char *title, const gbl_hot *hot4)
 *       The same with an already laid-out collection (the developer egg's records) and the title FUN_0002a060 draws
 *       on page 0 (font 28, "~" centred at (320, 50); "" none).
 *   int gbl_pager_from(int bg_idx, const gbl_text *t, int first, const char *title, const gbl_hot *hot4)
 *       The same from page `first` of the collection (the Mission Computer's DETAILS and the debriefing's AFTERMATH: 1).
 * This is the shell's only text layout / pager: the Mission Computer and the debriefing (gbl_campaign.c) use it too.
 *
 * Also usable: gp_dialog(msg, def) - the FUN_00027a40 dialog ("Line|line#Btn|Btn", returns the button index) driven by
 * gbl_frame (scriptable headless); gp_menu() - the Esc menu call that restores the caller's background.
 */

/* ======================================================================================================================
 * shared tables */
/* campaign 0 (0x800ac, 9-byte records {scenario, trial, title}); entry 22 = Retired */
static const char *const GP_CAMP_SCEN[23] = {
    "bassSCN1", "jessSCN1", "claiSCN1", "newcSCN1", "jeehSCN1", "tim_SCN1", "andySCN1", "fostSCN1", "hoffSCN1", "zackSCN1",
    "seanSCN1", "franSCN1", "asahSCN1", "peteSCN1", "cathSCN1", "sachSCN1", "branSCN1", "gui1SCN1", "gui2SCN1", "gui3SCN1",
    "gui4SCN1", "guinSCN1", ""};
static const char *const GP_CAMP_TITLE[23] = {
    "Trial on Alshain", "Defend on Mannedorf", "Patrol on Mannedorf", "Trial on Alshain", "Defend on Chandler",
    "Strike on Chandler", "Strike on Wolcott", "Circle of Equals", "Raid on Jeanette", "Defend on Jumpship",
    "Trial of Possession", "Defend on Alshain", "Trial on Alshain", "Underwater Strike", "Strike on Moritz",
    "Strike on Moritz 2", "Destroy Orbital Platform", "Bloodright Trial 1", "Bloodright Trial 2", "Bloodright Trial 3",
    "Bloodright Trial 4", "Final Bloodright Trial", "Retired"};
/* rank titles 0x7e790 (7 ranks, then the clan names the \r markup can reach: entry 7 = "Wolf") */
static const char *const GP_RANK[12] = {"Cadet", "Mechwarrior", "Star Commander", "Star Captain", "Star Colonel",
    "Nova Commander", "Galaxy Commander", "Wolf", "Jade Falcon", "Ghost Bear", "Smoke Jaguar", "Nova Cat"};
static const char *gp_rank(int r) { return GP_RANK[r < 0 ? 0 : r > 11 ? 11 : r]; }
static const char *gp_title(int m) { return GP_CAMP_TITLE[m < 0 ? 0 : m > 22 ? 22 : m]; }

/* fonts (DATABASE entries): DAT_95ea8 = 28 (pilot names, values), DAT_95eac = 29 (labels: every hotspot object's font -
 * FUN_0002bce0 0x2bcf2 takes DAT_95eac whatever font the caller passes), DAT_95ea0/a4/b8/bc = 33 (lists, text pages) */
#define GP_F28 28
#define GP_F29 29
#define GP_F33 33
#define GP_GREY 6   /* the hotspot object's '<' label colour map: glyph colour 1 -> palette 6 (0x2bd32); hover: own */

static int gp_click(const gbl_input *in)   /* FUN_0003b1d0: a left press; Return counts as one (0x3b2f0, key 0x0d) */
{
    return in->lclick || in->key == SDLK_RETURN || in->key == SDLK_KP_ENTER;
}
static int gp_dbl_click(const gbl_input *in)   /* FUN_0003b1b0: this press came within 200 ticks of the last one */
{
    static Uint32 last;
    static int have;
    int r = 0;
    if (!gp_click(in)) return 0;
    if (have && g_now - last < 201) r = 1;   /* 0x3b2f0: (now - t) < 0xc9 ticks (INFERRED: ms) */
    else { last = g_now; have = 1; }
    return r;
}
/* the shared Esc menu (FUN_00026f10, once a frame); the port restores the caller's background if a page changed it */
static int gp_menu(void)
{
    int bg = g_bg_entry, r = gbl_global_menu();
    if (g_bg_entry != bg && bg >= 0) load_bg(bg);
    return r;
}
static void gp_frame_end(void) { gbl_draw_cursor(g_in.mx, g_in.my); }
/* a text object (FUN_0003ed20): a leading '~' centres it on x, nothing else is special (gtext also eats '<') */
static void gp_obj_text(int fe, int x, int y, const char *s)
{
    if (*s == '~') { s++; x -= gtext_w(fe, s) / 2; }
    draw_text(gfont(fe), x, y, s, gbl_pal(), -1);
}

/* ---- the dialog (FUN_00027a40; base layout as shell_dialog: panel DATABASE entry 5 frame 2 at (172,165), lines
 * centred from y 197, 16 apart, buttons 61 apart at y 263) - driven by gbl_frame so scripted tests can answer it.
 * Return / Esc = the default, a button's first letter = that button. Returns the button index. */
static int gp_dialog(const char *msg, int def)
{
    char buf[256], *lines[8], *btn[4];
    int n = 0, nb = 0, i, chosen = -1, pressed = -1;
    char *p2;
    uint8_t *saved = malloc(sizeof g_fb);
    const uint8_t (*pal)[3] = gbl_pal();
    int n0 = g_ntext;
    static text_item t0[256];   /* the screen's queued text (windowed), whole: the covers below drop items from the queue */
    memcpy(t0, g_text, sizeof t0);
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
    if (saved) { memcpy(saved, g_fb, sizeof g_fb); gbl_cursor_unsave(saved); }
    while (chosen < 0) {
        gbl_input *in;
        int hit = -1;
        if (saved) memcpy(g_fb, saved, sizeof g_fb);
        memcpy(g_text, t0, sizeof t0); g_ntext = n0;
        db_shp_frame(5, 2, 172, 165);
        gbl_text_cover(172, 165, 172 + 296, 165 + 150);
        for (i = 0; i < n; i++) draw_text(&g_fthin, 320 - text_width(&g_fthin, lines[i]) / 2, 197 + 16 * i, lines[i], pal, -1);
        for (i = 0; i < nb; i++) {
            int cx = 320 - 30 * (nb - 1) + 61 * i;
            db_shp_frame(5, i == pressed ? 1 : 0, cx - 26, 263);
            draw_text(&g_fthin, cx - text_width(&g_fthin, btn[i]) / 2, 276 - font_height(&g_fthin) / 2, btn[i], pal, -1);
        }
        gp_frame_end();
        in = gbl_frame();
        if (in->quit) { chosen = def; break; }
        for (i = 0; i < nb; i++) { int cx = 320 - 30 * (nb - 1) + 61 * i; if (in->mx >= cx - 26 && in->mx <= cx + 26 && in->my >= 263 && in->my <= 289) hit = i; }
        if (in->key) {
            if (in->key == SDLK_RETURN || in->key == SDLK_KP_ENTER || in->key == SDLK_ESCAPE) chosen = def;
            else for (i = 0; i < nb; i++) if (toupper((unsigned char)btn[i][0]) == toupper((int)in->key & 0x7f)) chosen = i;
            if (nb == 0) chosen = 0;
        }
        if (in->lclick) pressed = hit;
        if (in->lup) { if (hit >= 0 && hit == pressed) chosen = hit; pressed = -1; }
    }
    if (saved) memcpy(g_fb, saved, sizeof g_fb);
    memcpy(g_text, t0, sizeof t0); g_ntext = n0;
    free(saved);
    return chosen;
}

/* ---- a full-screen clip at its own size and rate, not skippable (FUN_0003c660: the roster's "agball02" zoom) */
static void gp_play_full(const char *name)
{
    char path[1200];
    smk *s;
    int f, n;
    Uint32 t0 = g_now;
    gbl_open_smkdir(name, "SMK", path, sizeof path);
    if (!(s = smk_open_file(path))) return;   /* missing: nothing (the original returns) */
    n = smk_frames(s);
    for (f = 0; f < n && !g_quit_all; f++) {
        gbl_input *in;
        smk_next(s);
        blit_smk(s, (W - smk_width(s)) / 2, (H - smk_height(s)) / 2);
        text_layer_clear();
        in = gbl_frame();
        if (in->quit) break;
        if (!g_headless) {
            double due = (double)(f + 1) * smk_frame_ms(s);
            Uint32 now = g_now - t0;
            if ((double)now < due) SDL_Delay((Uint32)(due - (double)now));
        }
    }
    smk_close(s);
}

/* ======================================================================================================================
 * text pages */
static char *gbl_bwd_text(const char *record, const char *q)
{
    char err[256], rec[16];
    prj_archive *a;
    prj_record r;
    char *txt = NULL;
    int i;
    snprintf(rec, sizeof rec, "%s", record);
    for (i = 0; rec[i]; i++) rec[i] = (char)toupper((unsigned char)rec[i]);
    if (!(a = prj_open(gpath("MW2.PRJ"), err, sizeof err))) return NULL;
    if (prj_read_named(a, "BWD", rec, &r) == PRJ_OK) {
        uint32_t len;
        const uint8_t *p = chunk_find(&r, "ORDR", &len);
        if (p && len > 0 && (txt = malloc(len + 1))) {   /* 0x1c6a0: the chunk's last byte is cut (it is the NUL) */
            memcpy(txt, p, len - 1);
            txt[len - 1] = 0;
        }
        prj_record_free(&r);
    }
    prj_close(a);
    if (txt && q) {   /* FUN_0001c600 0x1c6c0..: the markup the shell fills in (into an 8 KB buffer, 0x8a0f4) */
        size_t n = strlen(txt), i2, o = 0, cap = n + 4096;
        char *out = malloc(cap);
        const gbl_pilot *p = pil();
        if (!out) return txt;
        for (i2 = 0; i2 < n && o + 256 < cap; ) {
            char c = txt[i2 + 1];
            const char *ins = NULL;
            char num[16];
            if (txt[i2] != '\\' || i2 + 1 >= n) { out[o++] = txt[i2++]; continue; }
            if (c == 'h' || c == 'H') { snprintf(num, sizeof num, "%d", p ? (int)p->honor : 0); ins = num; i2 += 2; }
            else if (c == 'q' || c == 'Q') { ins = q; i2 += 2; }
            else if (c == 'r' || c == 'R') {
                char d = i2 + 2 < n ? txt[i2 + 2] : 0;
                int rk = p ? (int)p->rank : 0;
                i2 += 2;
                if (d >= '0' && d <= '2') { int k = rk + (d - '0'); if (d != '0' && k > 7) k = 7; ins = gp_rank(k); i2++; }
                else ins = "";   /* another digit: "\r" vanishes, the digit stays (0x1c8cf) */
            } else { out[o++] = '\\'; out[o++] = c; i2 += 2; continue; }
            while (ins && *ins && o + 1 < cap) out[o++] = *ins++;
        }
        out[o] = 0;
        free(txt);
        txt = out;
    }
    return txt;
}

#define GBL_TEXT_LINES 600
typedef struct { int page, x, y; char s[320]; } gbl_text_line;
typedef struct { int fe, npages, nlines; gbl_text_line ln[GBL_TEXT_LINES]; } gbl_text;
static const int GP_TABS[19] = {0, 36, 72, 108, 144, 180, 216, 252, 288, 324, 360, 396, 432, 468, 504, 540, 576, 612, 640};   /* 0x7e848 */

static int gp_tw(int fe, const char *s) { return gtext_w(fe, s); }   /* FUN_00027ea0: every byte's glyph, escapes included */
static void gp_cat(char *d, size_t n, const char *s) { size_t l = strlen(d); if (l < n - 1) snprintf(d + l, n - l, "%s", s); }
static void gp_emit(gbl_text *t, int page, int x, int y, const char *s)
{
    gbl_text_line *l;
    if (!s[0] || t->nlines >= GBL_TEXT_LINES) return;
    l = &t->ln[t->nlines++];
    l->page = page; l->x = x; l->y = y;
    snprintf(l->s, sizeof l->s, "%s", s);
}
/* the token scanner of FUN_0003f4d0 (0x3f535..0x3f901) */
enum { GP_TK_END, GP_TK_NL, GP_TK_C, GP_TK_S, GP_TK_WORD, GP_TK_TAB, GP_TK_A, GP_TK_B, GP_TK_G };
static int gp_digits(const char *t, int len, int p, int n)
{
    int v = 0, k;
    for (k = 0; k < n && p + k < len && t[p + k] >= '0' && t[p + k] <= '9'; k++) v = v * 10 + (t[p + k] - '0');
    return v;
}
static int gp_token(const char *t, int len, int *pos, char *word, size_t wn, int *val)
{
    int p = *pos;
    size_t n = 0;
    word[0] = 0;
    if (p >= len) return GP_TK_END;
    for (;;) {
        while (p < len && t[p] != ' ' && t[p] != '\\') { if (n + 1 < wn) word[n++] = (char)(t[p] & 0x7f); p++; }
        word[n] = 0;
        if (p >= len) { *pos = p; return GP_TK_END; }   /* the text's last word: dropped by the layout (see below) */
        if (n > 0) {   /* a word and the spaces after it */
            while (p < len && t[p] == ' ') { if (n + 1 < wn) word[n++] = ' '; p++; }
            word[n] = 0;
            *pos = p;
            return GP_TK_WORD;
        }
        if (t[p] == ' ') { p++; continue; }   /* INFERRED: a space with no word before it (the original would not advance) */
        {
            char c = p + 1 < len ? t[p + 1] : 0;
            int tk = -1, q = p + 2;
            switch (c) {
            case 'n': case 'N': tk = GP_TK_NL; break;
            case 'c': case 'C': tk = GP_TK_C; break;
            case 's': case 'S': tk = GP_TK_S; break;
            case 't': case 'T': tk = GP_TK_TAB; break;
            case 'a': case 'A': tk = GP_TK_A; *val = gp_digits(t, len, q, 2); q += 2; break;
            case 'b': case 'B': tk = GP_TK_B; *val = gp_digits(t, len, q, 3); q += 3; break;
            case 'g': case 'G': tk = GP_TK_G; *val = gp_digits(t, len, q, 3); q += 3; break;
            }
            if (tk < 0) { if (n + 1 < wn) word[n++] = '\\'; p += 2; continue; }   /* 0x3f8e5: '\' kept, the letter dropped */
            while (q < len && t[q] == ' ') q++;
            *pos = q;
            return tk;
        }
    }
}
/* one page (FUN_0003ee70 box + FUN_0003f4d0): returns the rest of the text for the next page, or NULL at its end.
 * Line height = font height + 2; lines are text objects at (x, y). As the original: a word that would pass the box width
 * wraps (the line so far goes out, the word starts the next line - a word too wide for an empty line is lost); \n ends
 * the line (\c before it centres it with leading spaces); a line that would pass the box bottom ends the page and is
 * dropped (\n) or opens the next page (a word); \s ends the page; \t moves the width to the next stop (the next word
 * carries "\t" for the renderer); \bNNN / \gNNN become a "\b%03d" / "\g%03d" word whose value is taken off the width
 * once (0x3f6cf case 4); \a is skipped; at the end of the text the last scanned word is cleared before the pending line
 * goes out (0x3fff1 after the switch's default), so a text that does not end in an escape loses its last word. */
static const char *gp_layout_page(gbl_text *t, int page, const char *s, int x, int y0, int w, int h)
{
    int len = (int)strlen(s), pos = 0, lh = gtext_h(t->fe) + 2, y = y0 - lh, bottom = h >= 0 ? y0 + h : 0;
    int cur = 0, tabs = 0, centre = 0, pend = -1, synth = 0, sval = 0;
    char line[1024] = "", word[512], full[640];
    for (;;) {
        int tk, start = pos, val = 0;
        if (synth) { snprintf(word, sizeof word, "\\%c%03d", synth, sval); synth = 0; tk = GP_TK_WORD; }
        else tk = gp_token(s, len, &pos, word, sizeof word, &val);
        switch (tk) {
        case GP_TK_NL:
            if (centre) {   /* ((w - line width) / 2) / width(" ") spaces in front */
                int sp = gp_tw(t->fe, " "), k, pad = sp > 0 ? ((w - cur) / 2) / sp : 0;
                char tmp[1024];
                centre = 0;
                for (k = 0; k < pad && k < 200; k++) tmp[k] = ' ';
                snprintf(tmp + k, sizeof tmp - (size_t)k, "%s", line);
                snprintf(line, sizeof line, "%s", tmp);
            }
            if (bottom < 1 || y + lh <= bottom) {
                y += lh;
                gp_emit(t, page, x, y, line);
                line[0] = 0;
                cur = 0;
                break;
            }
            return s + pos;   /* the page is full: the pending line is dropped */
        case GP_TK_C: centre = 1; break;
        case GP_TK_S: return s + pos;
        case GP_TK_WORD: {
            int ww = gp_tw(t->fe, word), wl, k;
            full[0] = 0;
            for (k = 0; k < tabs && k < 18; k++) gp_cat(full, sizeof full, "\\t");
            tabs = 0;
            gp_cat(full, sizeof full, word);
            if (pend != -1) { cur -= pend; pend = -1; }
            wl = gp_tw(t->fe, full);
            if (w < wl + cur) {   /* wrap */
                y += lh;
                if (!line[0]) cur = 0;
                else { gp_emit(t, page, x, y, line); snprintf(line, sizeof line, "%s", full); cur = wl; }
            } else {
                gp_cat(line, sizeof line, full);
                cur += ww;
            }
            if (bottom > 0 && bottom < y + lh) return s + start;
            break;
        }
        case GP_TK_TAB: {
            int k;
            tabs++;
            for (k = 0; k < 18; k++) if (cur < GP_TABS[k]) break;
            cur = GP_TABS[k];
            break;
        }
        case GP_TK_B: case GP_TK_G: pend = val; synth = tk == GP_TK_B ? 'b' : 'g'; sval = val; break;
        case GP_TK_A: break;
        default:   /* the end: the pending line goes out */
            y += lh;
            gp_emit(t, page, x, y, line);
            return NULL;
        }
    }
}
static int gbl_text_pages(const char *txt, int fe, int x, int y, int w, int h, gbl_text *out)
{
    const char *p = txt ? txt : "";
    out->fe = fe; out->npages = 0; out->nlines = 0;
    do {   /* FUN_0001c600: a page object per call until the text ends (a trailing \s gives an empty last page) */
        p = gp_layout_page(out, out->npages, p, x, y, w, h);
        out->npages++;
    } while (p && out->npages < 64);
    return out->npages;
}
/* a line object (FUN_0003eaa0): glyph by glyph; "\t" -> the next stop from the line's x, "\bNNN" x -= NNN, "\gNNN" x =
 * line x + NNN, "\a" the link colours (not used by GBL texts), another "\x" skips the backslash */
static void gp_draw_line(int fe, int x0, int y, const char *s)
{
    char run[96];
    int rl = 0, x = x0;
    const char *p = s;
    if (*p == '~') { p++; x0 -= gp_tw(fe, p) / 2; x = x0; }
    for (;;) {
        if (!*p || *p == '\\' || rl >= 88) {
            if (rl) { run[rl] = 0; draw_text(gfont(fe), x, y, run, gbl_pal(), -1); x += gp_tw(fe, run); rl = 0; }
            if (!*p) break;
            if (*p == '\\') {
                char c = p[1];
                p++;
                if (!c) break;
                if (c == 't' || c == 'T') { int k; for (k = 0; k < 19; k++) if (x - x0 < GP_TABS[k]) { x = x0 + GP_TABS[k]; break; } p++; }
                else if (c == 'b' || c == 'B' || c == 'g' || c == 'G') {
                    int v = 0, k;
                    p++;
                    for (k = 0; k < 3 && *p; k++, p++) v = v * 10 + (*p >= '0' && *p <= '9' ? *p - '0' : 0);
                    x = (c == 'b' || c == 'B') ? x - v : x0 + v;
                } else if (c == 'a' || c == 'A') p++;
                continue;
            }
        }
        run[rl++] = *p++;
    }
}
static void gbl_text_draw(const gbl_text *t, int page)
{
    int i;
    for (i = 0; i < t->nlines; i++) if (t->ln[i].page == page) gp_draw_line(t->fe, t->ln[i].x, t->ln[i].y, t->ln[i].s);
}

/* ---- the pager (FUN_0002b0e0 constructor with a ready page collection + FUN_0002a590 once a frame) */
static const gbl_hot GBL_PAGER_HOT[4] = {   /* 0x802a0 -> 0x7f108 (screen 5, the egg); 0x80260 -> 0x7f1e8 is identical */
    {278, 437, 361, 465, 0, 0, ""}, {116, 437, 204, 465, 0, 0, ""}, {436, 437, 524, 465, 0, 0, ""}, {510, 550, 609, 574, 560, 555, ""}};
enum { GP_PG_NEXTG = 26, GP_PG_NEXT = 27, GP_PG_EXITG = 28, GP_PG_EXIT = 29, GP_PG_PREVG = 30, GP_PG_PREV = 31 };
/* first: the collection's page the pager starts on (FUN_0002b0e0's start index: the Mission Computer's DETAILS and the
 * debriefing's AFTERMATH show their collection from its second page - their first is the screen's own box); a collection
 * with no page from there on shows an empty page with EXIT */
static int gbl_pager_from(int bg_idx, const gbl_text *t, int first, const char *title, const gbl_hot *hot4)
{
    int page = 0, has_prev = 0, has_next, en[4] = {1, 1, 1, 1}, i, r = 5;
    const int np = t->npages - first > 0 ? t->npages - first : 1;
    if (!hot4) hot4 = GBL_PAGER_HOT;
    gbl_bg(bg_idx);
    /* 0x2a5dc: the six buttons (flags 0x44), first frame: EXIT, NEXT if there is a next page, the rest hidden */
    gs_load(GP_PG_PREV, "wgbam001", 116, 437, 0x44, 0);
    gs_load(GP_PG_PREVG, "wgbamg01", 116, 437, 0x44, 0);
    gs_load(GP_PG_EXIT, "wgbam002", 278, 437, 0x44, 0);
    gs_load(GP_PG_EXITG, "wgbamg02", 278, 437, 0x44, 0);
    gs_load(GP_PG_NEXT, "wgbam003", 436, 437, 0x44, 0);
    gs_load(GP_PG_NEXTG, "wgbamg03", 436, 437, 0x44, 0);
    has_next = np > 1;   /* FUN_0002a060 from the constructor: page 0, PREV off, NEXT (hotspot 2) only with more pages */
    en[1] = 0; en[2] = has_next;
    gs_flags(GP_PG_EXIT, GS_HIDDEN, 0);
    gs_flags(GP_PG_NEXT, GS_HIDDEN, !has_next);
    gs_flags(GP_PG_PREVG, GS_HIDDEN, 1); gs_flags(GP_PG_PREV, GS_HIDDEN, 1);
    gs_flags(GP_PG_EXITG, GS_HIDDEN, 1); gs_flags(GP_PG_NEXTG, GS_HIDDEN, 1);
    while (r == 5 && !g_quit_all) {
        gbl_input *in;
        int h, act;
        draw_bg();
        gs_update();
        if (first + page < t->npages) gbl_text_draw(t, first + page);
        if (page == 0 && title && title[0]) gp_obj_text(GP_F28, 320, 50, title);   /* FUN_0002a060 0x2a19b */
        gp_frame_end();
        in = gbl_frame();
        if (in->quit) { r = -3; break; }
        if ((act = gp_menu()) != -1) { r = act; break; }   /* 0x2a6e6: the Esc menu first */
        h = gh_hit(hot4, 4, en, in->mx, in->my);
        if (in->key == SDLK_HOME) h = 3;            /* 0x8047 */
        else if (in->key == SDLK_PAGEUP) h = en[1] ? 1 : -1;     /* 0x8049 (a disabled button's key does nothing) */
        else if (in->key == SDLK_PAGEDOWN) h = en[2] ? 2 : -1;   /* 0x8051 */
        act = gp_click(in) || in->key == SDLK_HOME || in->key == SDLK_PAGEUP || in->key == SDLK_PAGEDOWN;
        switch (h) {
        case 0:   /* EXIT: the glow picture under the pointer */
            gs_flags(GP_PG_EXIT, GS_HIDDEN, 1); gs_flags(GP_PG_EXITG, GS_HIDDEN, 0);
            if (act) r = -5;
            break;
        case 1:   /* previous (FUN_0002a060) */
            gs_flags(GP_PG_PREV, GS_HIDDEN, 1); gs_flags(GP_PG_PREVG, GS_HIDDEN, 0);
            if (act) {
                if (np == 1) { en[2] = 0; gs_flags(GP_PG_NEXT, GS_HIDDEN, 1); has_next = 0; }
                if (--page < 0) page = 0;
                if (page == 0) { en[1] = 0; gs_flags(GP_PG_PREV, GS_HIDDEN, 1); has_prev = 0; }
                if (page < np && np > 1) { en[2] = 1; gs_flags(GP_PG_NEXT, GS_HIDDEN, 0); has_next = 1; }
            }
            break;
        case 2:   /* next (0x2a885) */
            gs_flags(GP_PG_NEXT, GS_HIDDEN, 1); gs_flags(GP_PG_NEXTG, GS_HIDDEN, 0);
            if (act) {
                if (++page >= np) page = np - 1;
                if (page == np - 1) { en[2] = 0; gs_flags(GP_PG_NEXT, GS_HIDDEN, 1); has_next = 0; }
                if (page > 0) { en[1] = 1; gs_flags(GP_PG_PREV, GS_HIDDEN, 0); has_prev = 1; }
            }
            break;
        case 3:   /* Home (and the off-screen hotspot 3): the first page; already there: leave (-2) */
            if (act) {
                if (page != 0) {
                    page = 0;
                    en[1] = 0; gs_flags(GP_PG_PREV, GS_HIDDEN, 1); has_prev = 0;
                    if (np > 1) { en[2] = 1; gs_flags(GP_PG_NEXT, GS_HIDDEN, 0); has_next = 1; }
                } else r = -2;
            }
            break;
        default:   /* nothing under the pointer: the plain pictures of the buttons there are */
            if (has_prev) gs_flags(GP_PG_PREV, GS_HIDDEN, 0);
            gs_flags(GP_PG_EXIT, GS_HIDDEN, 0);
            if (has_next) gs_flags(GP_PG_NEXT, GS_HIDDEN, 0);
            gs_flags(GP_PG_EXITG, GS_HIDDEN, 1); gs_flags(GP_PG_NEXTG, GS_HIDDEN, 1); gs_flags(GP_PG_PREVG, GS_HIDDEN, 1);
            break;
        }
    }
    for (i = GP_PG_NEXTG; i <= GP_PG_PREV; i++) gs_close(i);   /* 0x2a7ff: flag 0x40000000 = freed */
    return r;
}
static int gbl_pager_pages(int bg_idx, const gbl_text *t, const char *title, const gbl_hot *hot4) { return gbl_pager_from(bg_idx, t, 0, title, hot4); }
static int gbl_pager(int bg_idx, const char *text, int x, int y, int w, int h, const gbl_hot *hot4)
{
    static gbl_text t;
    gbl_text_pages(text, GP_F33, x, y, w, h, &t);
    return gbl_pager_pages(bg_idx, &t, "", hot4);
}

/* ======================================================================================================================
 * BRF2 defaults (FUN_0002c810(scenario, 1, 0)): "<first 4>BRF2" - SUPS -> the "-b=" string; the first SDSC -> star 0
 * {formation 0, max +0xc, size +8, tonnage +4} and its loadouts (+0x10 + 16 k, k < max); a second SDSC the enemy star
 * and the skill base (none in GBL); then star 0, slot 0 current */
static void gp_brf2_defaults(const char *scen)
{
    prj_record r;
    size_t o;
    int nsd = 0;
    if (mission_record(scen, "BRF2", &r) != 0) return;
    for (o = 12; o + 8 <= r.size; ) {
        uint32_t sz = r.data[o + 4] | r.data[o + 5] << 8 | r.data[o + 6] << 16 | (uint32_t)r.data[o + 7] << 24;
        const uint8_t *p = r.data + o + 8;
        uint32_t len = sz >= 8 ? sz - 8 : 0;
        if (!memcmp(r.data + o, "SUPS", 4) && len > 0) snprintf(g_camo, sizeof g_camo, "%.*s", (int)(len < 15 ? len : 15), (const char *)p);
        if (!memcmp(r.data + o, "SDSC", 4) && len >= 16 && nsd < 2) {
            int tons = (int)(p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24);
            int size = (int)(p[8] | p[9] << 8 | p[10] << 16 | (uint32_t)p[11] << 24);
            int max = (int)(p[12] | p[13] << 8 | p[14] << 16 | (uint32_t)p[15] << 24), k;
            gstar_set(nsd, 0, max, size, tons);
            for (k = 0; k < max && k < 3 && 16 + (uint32_t)(k + 1) * 16 <= len; k++) {
                char lo[17];
                snprintf(lo, sizeof lo, "%.16s", (const char *)p + 16 + k * 16);
                gstar_slot(k, lo, NULL, NULL);
            }
            gstar_set(nsd, 0, max, size, tons);
            if (nsd == 1) g_skill_base = (int)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
            nsd++;
        }
        if (sz < 8) break;
        o += sz;
    }
    prj_record_free(&r);
    gstar_set(0, -1, -1, -1, -1);
    gstar_slot(0, NULL, NULL, NULL);
}

/* ======================================================================================================================
 * screen 12 - the Sibko roster (FUN_0003b8d0)
 *
 * Panels (FUN_0002e870 show / FUN_0002e980 hide), 11-int records {x, y, w, h, .., obj, make, click, index}: a y of
 * 0x8000xxyy is relative, = previous y + ((xx >> 4) x previous height) + (yy & 15); a w / h of -1 takes the made text's
 * width / height, or, when no text was made, the previous record's height. Both are written back into the static
 * table, so the layout freezes the first time a panel is shown (the original's quirk: a hidden mission inherits the
 * previous record's height - B_r2_list22). The DOS shell restarts after every mission; the port's stays up, so here the
 * freeze lasts until the program ends (ASSUMED difference). */
enum { GPI_NAME, GPI_LABEL, GPI_RANK, GPI_HONOR, GPI_MTITLE, GPI_ITEM };
typedef struct { int x, y, w, h, rel, kind, idx; const char *text; int shown, fe; char s[48]; } gp_item;
static gp_item GP_RECORD[7] = {   /* 0x84e10 */
    {468, 92, -1, -1, 0, GPI_NAME, 0, NULL, 0, 0, ""}, {468, 209, -1, -1, 0, GPI_LABEL, 0, "~RANK", 0, 0, ""},
    {468, 0, -1, -1, 0x13, GPI_RANK, 0, NULL, 0, 0, ""}, {468, 0, -1, -1, 0x22, GPI_LABEL, 0, "~HONOR", 0, 0, ""},
    {468, 0, -1, -1, 0x13, GPI_HONOR, 0, NULL, 0, 0, ""}, {468, 0, -1, -1, 0x22, GPI_LABEL, 0, "~MISSION", 0, 0, ""},
    {468, 0, -1, -1, 0x13, GPI_MTITLE, 0, NULL, 0, 0, ""}};
static gp_item GP_LOM[25];   /* 0x84f70: name, "Select Mission", items 0-10, "Select Mission", items 11-21 */
static void gp_lom_init(void)
{
    int k, n = 0;
    if (GP_LOM[0].kind == GPI_NAME && GP_LOM[0].x) return;
    GP_LOM[n++] = (gp_item){468, 92, -1, -1, 0, GPI_NAME, 0, NULL, 0, 0, ""};
    GP_LOM[n++] = (gp_item){388, 200, -1, -1, 0, GPI_LABEL, 0, "~Select Mission", 0, 0, ""};
    for (k = 0; k < 11; k++) GP_LOM[n++] = (gp_item){338, 0, 100, -1, k ? 0x12 : 0x2f, GPI_ITEM, k, NULL, 0, 0, ""};
    GP_LOM[n++] = (gp_item){538, 200, -1, -1, 0, GPI_LABEL, 0, "~Select Mission", 0, 0, ""};
    for (k = 11; k < 22; k++) GP_LOM[n++] = (gp_item){488, 0, 100, -1, k > 11 ? 0x12 : 0x2f, GPI_ITEM, k, NULL, 0, 0, ""};
}
static int gp_is_watcher(const gbl_pilot *p) { return p && !strcmp(p->name, GBL_CHEATS[CH_THEWATCHER]); }
static void gp_panel_show(gp_item *it, int n)
{
    int i, py = 0, ph = 0;
    const gbl_pilot *p = pil();
    for (i = 0; i < n; i++, it++) {
        if (it->rel) { it->y = py + ((it->rel & 0xff0) >> 4) * ph + (it->rel & 0xf); it->rel = 0; }
        it->shown = 0;
        if (p) switch (it->kind) {   /* the make callbacks 0x3b790 / 0x3b6d0 / 0x3b7d0 / 0x3b820 / 0x3b860 / 0x3b6f0 */
        case GPI_NAME: snprintf(it->s, sizeof it->s, "~%s", p->display); it->fe = GP_F28; it->shown = 1; break;
        case GPI_LABEL: snprintf(it->s, sizeof it->s, "%s", it->text); it->fe = GP_F29; it->shown = 1; break;
        case GPI_RANK: snprintf(it->s, sizeof it->s, "~%s", gp_rank(p->rank)); it->fe = GP_F28; it->shown = 1; break;
        case GPI_HONOR: snprintf(it->s, sizeof it->s, "~%d", (int)p->honor); it->fe = GP_F28; it->shown = 1; break;
        case GPI_MTITLE: snprintf(it->s, sizeof it->s, "~%s", gp_title(p->done)); it->fe = GP_F28; it->shown = 1; break;
        case GPI_ITEM:
            if (gp_is_watcher(p) || (it->idx < p->done && it->idx < 17 && it->idx < 22)) {
                snprintf(it->s, sizeof it->s, "~%s", gp_title(it->idx)); it->fe = GP_F33; it->shown = 1;
            }
            break;
        }
        if (it->shown) {
            if (it->w == -1) it->w = gtext_w(it->fe, it->s + (it->s[0] == '~'));
            if (it->h == -1) it->h = gtext_h(it->fe);
        }
        if (it->w == -1) it->w = 0;
        if (it->h == -1) it->h = ph;
        py = it->y; ph = it->h;
    }
}
static void gp_panel_hide(gp_item *it, int n) { int i; for (i = 0; i < n; i++) it[i].shown = 0; }
static void gp_panel_draw(const gp_item *it, int n)
{
    int i;
    for (i = 0; i < n; i++, it++)
        if (it->shown) gp_obj_text(it->fe, it->kind == GPI_ITEM ? it->x + it->w / 2 : it->x, it->y, it->s);
}
/* FUN_00034320: the first record with a click callback whose box holds the point (x0 <= x < x0 + w, y <= y < y + h) */
static int gp_lom_hit(int x, int y)
{
    int i;
    for (i = 0; i < 25; i++) {
        const gp_item *it = &GP_LOM[i];
        if (it->kind == GPI_ITEM && x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return it->idx;
    }
    return -1;
}

static const gbl_hot GP_ROSTER_HOT[14] = {   /* 0x7eea0 */
    {32, 83, 297, 116, 41, 91, ""}, {32, 117, 297, 151, 41, 125, ""}, {32, 152, 297, 186, 41, 160, ""},
    {32, 187, 297, 221, 41, 195, ""}, {32, 222, 297, 256, 41, 230, ""}, {32, 257, 297, 291, 41, 265, ""},
    {32, 292, 297, 326, 41, 300, ""}, {32, 327, 297, 361, 41, 335, ""}, {32, 362, 297, 397, 41, 370, ""},
    {32, 398, 297, 432, 41, 406, ""},
    {294, 450, 393, 474, 344, 455, "<~ACCEPT"}, {20, 450, 221, 474, 121, 455, "<~DELETE MECHWARRIOR"},
    {373, 403, 562, 427, 468, 408, "<~LAUNCH OLD MISSION"}, {418, 403, 517, 427, 468, 408, "<~PILOT INFO"}};

typedef struct { int en[14]; int list; int entry_row; const char *entry_text; } gp_roster_st;
static void gp_roster_draw(const gp_roster_st *st)
{
    int i, h;
    draw_bg();
    gs_update();
    for (i = 0; i < 10; i++) {   /* the row texts (FUN_00027f00(font 28, 42, 92 + 35 i, +0x28)) */
        if (i == st->entry_row) { char b[40]; snprintf(b, sizeof b, "%s_", st->entry_text); draw_text(gfont(GP_F28), 42, 92 + 35 * i, b, gbl_pal(), -1); }
        else if (g_pil[i].in_use) gp_obj_text(GP_F28, 42, 92 + 35 * i, g_pil[i].display);
    }
    gp_panel_draw(GP_RECORD, 7);
    gp_panel_draw(GP_LOM, 25);
    h = st->entry_row >= 0 ? -1 : gh_hit(GP_ROSTER_HOT, 14, st->en, g_in.mx, g_in.my);
    gh_labels(GP_ROSTER_HOT, 14, st->en, h, GP_F29, GP_GREY, -1);
    gp_frame_end();
}
/* FUN_0002e4a0(font 28, 42, y, buffer, 0, 17, 300): "text_" - a char 0x20-0x7f (not '~') the font has, while the text
 * stays under 17 characters and the text with its "_" under 300 px; Backspace; Return or a click ends it (1), Esc too
 * (0) - the roster uses the text either way. Returns 1 when a click ended it. */
static int gp_name_entry(gp_roster_st *st, int row, char *out, size_t outn)
{
    char buf[24] = "";
    int clicked = 0, done = 0;
    st->entry_row = row; st->entry_text = buf;
    if (!g_headless) SDL_StartTextInput();
    while (!done && !g_quit_all) {
        gbl_input *in;
        const char *t;
        gp_roster_draw(st);
        in = gbl_frame();
        if (in->quit) break;
        if (in->lclick) { clicked = 1; break; }
        if (in->key == SDLK_RETURN || in->key == SDLK_KP_ENTER || in->key == SDLK_ESCAPE) break;
        if (in->key == SDLK_BACKSPACE && buf[0]) buf[strlen(buf) - 1] = 0;
        for (t = in->text; *t; t++) {
            unsigned char c = (unsigned char)*t;
            size_t n = strlen(buf);
            char tmp[32];
            if (c < 0x20 || c >= 0x80 || c == '~' || n >= 17) continue;
            { char one[2] = {(char)c, 0}; if (gtext_w(GP_F28, one) == 0) continue; }
            snprintf(tmp, sizeof tmp, "%s%c_", buf, c);
            if (gtext_w(GP_F28, tmp) < 300) { buf[n] = (char)c; buf[n + 1] = 0; }
        }
    }
    if (!g_headless) SDL_StopTextInput();
    st->entry_row = -1; st->entry_text = NULL;
    snprintf(out, outn, "%s", buf);
    return clicked;
}
static void gp_cheat_flags(const char *name, int on)
{
    if (!strcmp(name, GBL_CHEATS[CH_DOOMSDAY])) g_cheat_nuke = on;
    if (!strcmp(name, GBL_CHEATS[CH_LOVECRAFT])) g_cheat_torpedo = on;
    if (!strcmp(name, GBL_CHEATS[CH_RVWINDOW])) g_cheat_rvwindow = on;
}
static int gbl_roster(void)
{
    gp_roster_st st;
    int next = -1, i, reprocess = 0;
    memset(&st, 0, sizeof st);
    st.entry_row = -1;
    gp_lom_init();
    gp_play_full("agball02");   /* the zoom into the orb (0x3b902) */
    srand((unsigned)time(NULL));   /* 0x3b90c (INFERRED srand(time)) */
    gbl_bg(18);   /* 0x80208 + ctx x 16 */
    pil_load();   /* FUN_0001dfe0 */
    g_cur = -1;
    for (i = 0; i < 10; i++) {   /* the active pilot: the first used and selected record; the others' flag cleared */
        if (g_cur >= 0 || (g_pil[i].selected && g_pil[i].in_use != 1)) g_pil[i].selected = 0;
        else if (g_pil[i].selected) g_cur = i;
    }
    for (i = 0; i < 14; i++) st.en[i] = 1;
    g_flag = 0;   /* *EBX = 0 */
    if (!pil()) st.en[10] = st.en[11] = st.en[12] = st.en[13] = 0;
    else { gp_panel_show(GP_RECORD, 7); st.en[13] = 0; if (!pil()->done) st.en[12] = 0; }
    gsnd_play(0x51, 5, 1);   /* DATABASE idx 0x51, looped, the sample object's default volume 5 (0x3db50) */
    while (next == -1 && !g_quit_all) {
        gbl_input *in;
        int h;
        if (!reprocess) {
            gp_roster_draw(&st);
            in = gbl_frame();
            if (in->quit) { next = -3; break; }
        } else in = &g_in;
        h = gh_hit(GP_ROSTER_HOT, 14, st.en, in->mx, in->my);
        if (reprocess || gp_click(in)) {
            int dbl = reprocess ? 0 : gp_dbl_click(in);
            if (!reprocess && st.list && pil()) {   /* a LAUNCH OLD MISSION item (0x3bb60), before the hotspots */
                int k = gp_lom_hit(in->mx, in->my);
                if (k >= 0 && k < pil()->done && (k < 17 || gp_is_watcher(pil()))) {
                    snprintf(g_scen, sizeof g_scen, "%s", GP_CAMP_SCEN[k]);
                    g_brief_m = k;
                    gstar_set(0, 0, 3, 1, 100);
                    gp_brf2_defaults(g_scen);
                    gstar_set(1, 0, 0, 0, 100);
                    gstar_set(0, -1, -1, -1, -1);
                    gstar_slot(0, NULL, pil()->display, NULL);
                    next = 5;   /* the state machine sets g_old_mission (0x95e88) */
                }
            }
            reprocess = 0;
            if (h >= 0 && h < 10) {
                gbl_pilot *r = &g_pil[h];
                if (!r->in_use) {   /* an empty row: a new pilot's name */
                    char nm[21];
                    int clicked;
                    st.en[10] = st.en[11] = st.en[12] = st.en[13] = 0;
                    gp_panel_hide(GP_LOM, 25); gp_panel_hide(GP_RECORD, 7);
                    g_cur = -1;
                    r->display[0] = 0;
                    clicked = gp_name_entry(&st, h, nm, sizeof nm);
                    for (i = 0; nm[i]; i++) nm[i] = (char)toupper((unsigned char)nm[i]);   /* FUN_00010090 */
                    snprintf(r->display, sizeof r->display, "%s", nm);
                    snprintf(r->name, sizeof r->name, "%s", nm);
                    if (r->display[0]) {   /* 0x3c0e4: the new pilot */
                        r->in_use = 1; g_cur = h;
                        r->done = 0; r->rank = 0;
                        if (gp_is_watcher(r)) r->done = 22;
                        gp_cheat_flags(r->name, 1);
                        r->kills = r->hits = r->shots = r->unused24 = 0;
                        r->cur_fail = r->hall = r->deaths = 0;
                        r->honor = (int32_t)lrint(1000.0 + (double)(rand() & 0x7fff) * (1.0 / 32767.0) * 1000.0);
                        for (i = 0; i < 10; i++) g_pil[i].selected = 0;
                        r->selected = 1;
                        st.en[10] = st.en[11] = 1;
                        gp_panel_show(GP_RECORD, 7);
                    }
                    st.list = 0;
                    if (clicked && g_in.ldown) reprocess = 1;   /* the click that ended it is taken as a click (0x3be00) */
                    continue;
                }
                for (i = 0; i < 10; i++) g_pil[i].selected = 0;   /* a pilot: select it, its star back */
                r->selected = 1;
                g_cur = h;
                {
                    gbl_star s = r->star;
                    int k;
                    gstar_set(0, s.formation, s.max, s.size, s.tons);
                    for (k = 0; k < s.size && k < 3; k++) gstar_slot(k, s.slot[k].loadout, s.slot[k].name1, NULL);
                }
                if (dbl) { next = 1; g_flag = 1; }   /* a double click = ACCEPT */
                else {
                    st.en[10] = st.en[11] = 1;
                    st.en[12] = r->done != 0;
                    st.en[13] = 0;
                    gp_panel_hide(GP_LOM, 25); gp_panel_hide(GP_RECORD, 7);
                    st.list = 0;
                    gp_panel_show(GP_RECORD, 7);
                }
            } else if (h == 10) {   /* ACCEPT */
                if (pil()) { next = 1; g_flag = 1; }
            } else if (h == 11) {   /* DELETE MECHWARRIOR */
                if (pil() && gp_dialog("Terminate MechWarrior?#Yes|No", 1) != 1) {
                    gbl_pilot *r = pil();
                    gp_cheat_flags(r->name, 0);
                    r->in_use = r->selected = 0;
                    r->done = r->rank = r->honor = r->kills = r->hits = r->shots = r->unused24 = 0;
                    r->deaths = r->hall = r->slot_limit = 0;
                    r->display[0] = 0; r->name[0] = 0;
                    r->runtime = 0;
                    gp_panel_hide(GP_RECORD, 7); gp_panel_hide(GP_LOM, 25);
                    g_cur = -1;
                    st.en[10] = st.en[11] = st.en[12] = st.en[13] = 0;
                    st.list = 0;
                }
            } else if (h == 12) {   /* LAUNCH OLD MISSION */
                st.en[12] = 0; st.en[13] = 1;
                gp_panel_hide(GP_RECORD, 7);
                st.list = 1;
                gp_panel_show(GP_LOM, 25);
            } else if (h == 13) {   /* PILOT INFO */
                if (pil() && pil()->done) st.en[12] = 1;
                st.en[13] = 0;
                gp_panel_hide(GP_LOM, 25);
                st.list = 0;
                gp_panel_show(GP_RECORD, 7);
            }
        }
        if (next == -1) next = gp_menu();
    }
    gp_panel_hide(GP_LOM, 25); gp_panel_hide(GP_RECORD, 7);
    pil_save();   /* every exit (FUN_0001e1c0) */
    gsnd_stop(0x51);
    return next;
}

/* ======================================================================================================================
 * screen 1 - the Ghost Bear hall (FUN_0001e270), by the pilot's location +0x5a: 0 the ice cave (Sibko), 1 the DropShip
 * bridge, 2 the 72nd Assault Trinary barracks. Records 0x80180 + hall x 16 {hotspots, 5, background, 38}. */
static const gbl_hot GP_HALL_HOT[3][5] = {
    {{252, 244, 371, 377, 243, 350, "MISSION COMPUTER"}, {67, 288, 114, 372, 84, 298, "~EXIT"}, {149, 224, 219, 286, 153, 293, "REGISTER"},
     {443, 286, 481, 364, 406, 287, "COMBAT SIMULATOR"}, {97, 239, 113, 263, 0, 0, ""}},
    {{257, 179, 374, 248, 250, 255, "MISSION COMPUTER"}, {491, 0, 639, 479, 595, 290, "~EXIT"}, {-1, -1, -1, -1, 150, 200, ""},
     {0, 0, 147, 479, 13, 290, "COMBAT SIMULATOR"}, {439, 259, 460, 276, 0, 0, ""}},
    {{190, 97, 450, 306, 305, 304, "~MISSION COMPUTER"}, {0, 420, 639, 479, 320, 455, "~EXIT"}, {-1, -1, -1, -1, 150, 200, ""},
     {23, 121, 130, 376, 1, 156, "COMBAT SIMULATOR"}, {472, 49, 500, 162, 0, 0, ""}}};
typedef struct { const char *name; int x, y, flags; } gp_clip;
static const gp_clip GP_HALL_CLIPS[3][6] = {   /* 0x7ebe8 + hall x 8 = {count, list}; slot = list index + 1 */
    {{"agbcom01", 256, 359, 0x6a}, {"agball01", 130, 200, 0x261}, {"agbcom02", 252, 244, 0x261},
     {"agbcom03", 252, 244, 0x69}, {"agbcom04", 252, 244, 0x63}, {"agbchmc", 438, 295, 0x6a}},
    {{"agbbr001", 132, 214, 0x4a}, {"agbbr002", 252, 181, 0x4a}, {"agbbr003", 419, 221, 0x4a}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},
    {{"agbtb001", 160, 90, 0x6a}, {"agbtb002", 160, 90, 0x62}, {"agbtb003", 160, 90, 0x6a}, {"agbtb004", 160, 90, 0x6a},
     {"agbtb005", 380, 382, 0x4a}, {NULL, 0, 0, 0}}};
static const int GP_HALL_NCLIPS[3] = {6, 3, 5};
static void gp_show_play(int s) { gs_restart(s); }                      /* FUN_0003ce00 + FUN_0003cdc0(s, 1, 0) */
static void gp_pause_hide(int s) { gs_flags(s, GS_PAUSED | GS_HIDDEN, 1); }   /* FUN_0003cdc0(s, 1, 1) + (s, 0x20, 0x20) */
/* FUN_0003d410: one slot played to its end (its loop bit cleared), every other slot frozen but drawn, the screen's text
 * objects (the hover label) kept on top */
static void gp_play_slot(int slot, const gbl_hot *hs, int hover)
{
    int k;
    if (!gs_loaded(slot)) return;
    gs_restart(slot);
    gs_flags(slot, GS_LOOP | GS_REWIND, 0);
    while (!gs_done(slot) && !g_quit_all) {
        draw_bg();
        for (k = 0; k < GS_SLOTS; k++) {
            if (k == slot) gs_tick(k);
            if (k == slot && gs_done(k)) {   /* its last frame stays up until the screen is drawn again (the dialog) */
                int f = g_gs[k].flags;
                g_gs[k].flags |= GS_HOLD;
                gs_blit(k);
                g_gs[k].flags = f;
            } else gs_blit(k);
        }
        gh_labels(hs, 5, NULL, hover, GP_F29, GP_GREY, -1);
        gp_frame_end();
        if (gbl_frame()->quit) break;
    }
}
static int gbl_hall(void)
{
    gbl_pilot *p = pil();
    int hall, k, next = -1, hover = 0, snd, vol;
    const gbl_hot *hs;
    if (!g_flag || !p) return 12;   /* 0x1e28b: without the accepted flag, the roster at once */
    hall = p->hall >= 0 && p->hall <= 2 ? p->hall : 0;
    hs = GP_HALL_HOT[hall];
    gbl_bg(11 + hall);   /* 0x80188 + hall x 16: idx 11 / 12 / 13 */
    snd = hall == 0 ? 0x51 : hall == 1 ? 0x7b : 0x7c;
    vol = hall == 0 ? 10 : 40;
    gsnd_play(snd, vol, 1);   /* ambience, looped (0x3dd40 / 0x3dd70) */
    for (k = 0; k < GP_HALL_NCLIPS[hall]; k++)   /* missing clips leave their slot empty (FUN_0003d1a0) */
        gs_load(k + 1, GP_HALL_CLIPS[hall][k].name, GP_HALL_CLIPS[hall][k].x, GP_HALL_CLIPS[hall][k].y, GP_HALL_CLIPS[hall][k].flags, 0);
    if (hall == 0) {   /* 0x1e48e: com01, the orb and the hologram playing; 3-5 paused */
        gp_show_play(1); gp_show_play(2);
        gs_flags(3, GS_PAUSED, 1); gs_flags(4, GS_PAUSED, 1); gs_flags(5, GS_PAUSED, 1);
        gp_show_play(6);
    } else if (hall == 2) {   /* 0x1e549 */
        gp_show_play(1);
        gs_flags(2, GS_PAUSED, 1); gs_flags(3, GS_PAUSED, 1); gs_flags(4, GS_PAUSED, 1);
    }
    while (next == -1 && !g_quit_all) {
        gbl_input *in;
        int h;
        draw_bg();
        gs_update();
        h = gh_hit(hs, 5, NULL, g_in.mx, g_in.my);
        gh_labels(hs, 5, NULL, h, GP_F29, GP_GREY, -1);
        gp_frame_end();
        in = gbl_frame();
        if (in->quit) { next = -3; break; }
        h = gh_hit(hs, 5, NULL, in->mx, in->my);
        switch (h) {
        case 0:   /* MISSION COMPUTER */
            if (hall == 0 && !hover) { gp_show_play(3); gsnd_play(0x79, 100, 0); hover = 1; }
            else if (hall == 2 && !hover) { gp_show_play(2); gp_pause_hide(1); gsnd_play(0x79, 100, 0); hover = 1; }
            if (gp_click(in)) {
                if (hall == 0) { for (k = 1; k <= 6; k++) if (k != 5) gp_pause_hide(k); }
                else if (hall == 2) { for (k = 1; k <= 3; k++) gp_pause_hide(k); }
                if (hall != 1) gp_play_slot(hall == 0 ? 5 : 4, hs, h);   /* the click clip to its end */
                if (p->done >= 22 || (p->deaths != 0 && p->done >= 17)) {   /* 0x1e7a9: already won */
                    next = 16;
                    gp_dialog("This pilot has|already won the game.#Finale", 0);
                } else next = 5;
            }
            break;
        case 1:   /* EXIT */
            if (gp_click(in) && gp_dialog("Embrace cowardice?#Yes|No", 1) == 0) next = -3;
            break;
        case 2: if (gp_click(in)) next = 12; break;   /* REGISTER */
        case 3: if (gp_click(in)) next = 7; break;    /* COMBAT SIMULATOR */
        case 4: if (gp_click(in)) next = 17; break;   /* the hidden one: the developers' photo */
        }
        if (hall == 0) {   /* 0x1e864: after the beam (3) the turning emblem (4); the orb restarted when it rests */
            if (hover == 1 && gs_done(3)) { gp_pause_hide(3); gp_show_play(4); hover = 2; }
            if (gs_loaded(2) && gs_done(2)) gp_show_play(2);
        } else if (hall == 2 && hover == 1 && gs_done(2)) { gp_pause_hide(2); gp_show_play(3); hover = 2; }
        if (next == -1) next = gp_menu();
    }
    gs_close_all();
    gsnd_stop(snd);
    return next;
}

/* ======================================================================================================================
 * screen 16 - the TSENG BLOODNAME roll (FUN_0001dbd0): records 10-19 (read while in use, in order), the pilot added
 * unless an entry has his name AND honor, an exchange sort by honor (FUN_0001dbd0's: each entry swapped with the first
 * later one that has more honor), the top 10 written back (+0 = 1, +0x3d, +0x14 only - the display name is rebuilt on
 * the next load), the career file saved; every entry drawn (198, 145 + 23 k) in font 33. EXIT plate -> 1. */
static int gbl_bloodname(void)
{
    static const gbl_hot hs[11] = {
        {249, 406, 390, 438, 0, 0, ""}, {32, 83, 297, 116, 198, 143, ""}, {32, 117, 297, 151, 198, 166, ""},
        {32, 152, 297, 186, 198, 189, ""}, {32, 187, 297, 221, 198, 212, ""}, {32, 222, 297, 256, 198, 235, ""},
        {32, 257, 297, 291, 198, 258, ""}, {32, 292, 297, 326, 198, 281, ""}, {32, 327, 297, 361, 198, 304, ""},
        {32, 362, 297, 397, 198, 321, ""}, {32, 398, 297, 432, 198, 350, ""}};   /* 0x801e0 -> 0x7ed6c */
    struct { char name[21]; int32_t honor; } e[11];
    gbl_pilot *p = pil();
    int n = 0, i, j, found = 0, next = -1;
    if (!p || p->deaths != 0) return 1;
    gbl_bg(23);
    for (i = 10; i < 20 && g_pil[i].in_use == 1; i++) {
        if (!strcmp(g_pil[i].name, p->name) && p->honor == g_pil[i].honor) found = 1;
        snprintf(e[n].name, sizeof e[n].name, "%s", g_pil[i].name);
        e[n++].honor = g_pil[i].honor;
    }
    if (!found) { snprintf(e[n].name, sizeof e[n].name, "%s", p->name); e[n++].honor = p->honor; }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n && !(e[i].honor < e[j].honor); j++) {}
        if (j < n && e[i].honor < e[j].honor) { char t[21]; int32_t th = e[i].honor; memcpy(t, e[i].name, 21); memcpy(e[i].name, e[j].name, 21); e[i].honor = e[j].honor; memcpy(e[j].name, t, 21); e[j].honor = th; }
    }
    for (i = 10; i < 20; i++) g_pil[i].in_use = 0;
    for (i = 0; i < n && i < 10; i++) {
        gbl_pilot *r = &g_pil[10 + i];
        memcpy(r->name, e[i].name, sizeof r->name); r->name[20] = 0;
        r->in_use = 1;
        r->honor = e[i].honor;
    }
    pil_save();
    while (next == -1 && !g_quit_all) {
        gbl_input *in;
        draw_bg();
        gs_update();
        for (i = 0; i < n; i++) gp_obj_text(GP_F33, 198, 145 + 23 * i, e[i].name);
        gp_frame_end();
        in = gbl_frame();
        if (in->quit) { next = -3; break; }
        if (gh_hit(hs, 11, NULL, in->mx, in->my) == 0 && gp_click(in)) next = 1;
        if (next == -1) next = gp_menu();
    }
    return next;
}

/* ======================================================================================================================
 * screen 17 - the developers' photo (FUN_0004b1a0): the team in the pilot's hall (background idx 0x18 / 0x19 / 0x1a),
 * 9 hotspots ('~' names on hover, "<~EXIT" always), each name opens its text page (MW2.PRJ BWD FERREGG1 .., laid out once
 * a session in (85,27) 469 x 403, font 33) in the pager over background idx 0x16. EXIT -> the previous screen. */
static const gbl_hot GP_EGG_HOT[3][9] = {
    {{0, 420, 639, 479, 320, 455, "<~EXIT"}, {228, 265, 379, 398, 307, 376, "~Bill Ferrer"}, {77, 133, 163, 243, 122, 212, "~Jack Mamais"},
     {486, 152, 568, 270, 536, 208, "~Jack Burton"}, {43, 243, 140, 383, 87, 312, "~JJ Franzen"}, {0, 133, 77, 236, 55, 192, "~Danny Matson"},
     {570, 152, 614, 273, 590, 238, "~Chad Findley"}, {255, 74, 379, 264, 323, 159, "~Sean Kinnear"}, {493, 274, 614, 386, 546, 347, "~Matt Candler"}},
    {{0, 420, 639, 479, 320, 455, "<~EXIT"}, {258, 248, 383, 419, 303, 372, "~Bill Ferrer"}, {30, 99, 148, 247, 100, 166, "~Jack Mamais"},
     {385, 70, 566, 247, 444, 164, "~Jack Burton"}, {51, 248, 205, 419, 132, 347, "~JJ Franzen"}, {150, 58, 257, 247, 198, 160, "~Danny Matson"},
     {567, 90, 639, 419, 569, 195, "~Chad Findley"}, {258, 69, 383, 247, 321, 169, "~Sean Kinnear"}, {385, 248, 566, 419, 466, 361, "~Matt Candler"}},
    {{0, 420, 639, 479, 320, 455, "<~EXIT"}, {226, 262, 349, 419, 292, 342, "~Bill Ferrer"}, {107, 159, 190, 261, 151, 214, "~Jack Mamais"},
     {350, 145, 454, 261, 387, 214, "~Jack Burton"}, {108, 262, 225, 408, 176, 335, "~JJ Franzen"}, {191, 144, 225, 261, 220, 198, "~Danny Matson"},
     {455, 164, 521, 376, 491, 230, "~Chad Findley"}, {226, 147, 349, 261, 302, 208, "~Sean Kinnear"}, {350, 262, 454, 398, 401, 334, "~Matt Candler"}}};
static const char *const GP_EGG_PAGES[8] = {"FERREGG1", "MAMAEGG1", "BURTEGG1", "FRANEGG1", "MATSEGG1", "FINDEGG1", "KINNEGG1", "CANDEGG1"};   /* 0x87794 */
static int gbl_egg(void)
{
    static gbl_text pages[8];
    static int loaded;   /* DAT_000af9d4: once a session */
    const gbl_pilot *p = pil();
    int hall = p && p->hall >= 1 && p->hall <= 2 ? p->hall : 0, next = -1, k;
    /* the screen returns to the state it came from (FUN_0004b1a0(db, prev)); the port's state machine has already set
     * g_prev to 17 here, and the hall (1) is the only way in - so 1 (ASSUMED; see the report's core note) */
    int back = g_prev >= 0 && g_prev != 17 ? g_prev : 1;
    if (!loaded) {
        for (k = 0; k < 8; k++) {
            char *t = gbl_bwd_text(GP_EGG_PAGES[k], "");
            gbl_text_pages(t, GP_F33, 85, 27, 469, 403, &pages[k]);
            free(t);
        }
        loaded = 1;
    }
    gs_close_all();
    gbl_bg(0x18 + hall);
    while (next == -1 && !g_quit_all) {
        gbl_input *in;
        int h;
        draw_bg();
        gs_update();
        h = gh_hit(GP_EGG_HOT[hall], 9, NULL, g_in.mx, g_in.my);
        gh_labels(GP_EGG_HOT[hall], 9, NULL, h, GP_F29, GP_GREY, -1);
        gp_frame_end();
        in = gbl_frame();
        if (in->quit) { next = -3; break; }
        h = gh_hit(GP_EGG_HOT[hall], 9, NULL, in->mx, in->my);
        if (h == 0 && gp_click(in)) next = back;
        else if (h >= 1 && h <= 8 && gp_click(in)) {
            int r = gbl_pager_pages(0x16, &pages[h - 1], "", GBL_PAGER_HOT);
            if (r == -3) next = -3;
            if (r == 8) next = 8;
            gbl_bg(0x18 + hall);
        }
        if (next == -1) next = gp_menu();
    }
    gs_close_all();
    return next;
}
