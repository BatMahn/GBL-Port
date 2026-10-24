/*
 * gbl_frame.c - the GBL title (state 8, FUN_0002dda0), the global Esc menu (FUN_00026f10) and its pages: COMBAT VARIABLES
 * (menu case 1, 0x27323), COCKPIT CONTROLS (FUN_00021f40 = the base page, reused), HALL OF HONOR (FUN_0002b910), THE KESHIK
 * (FUN_00022320), FLEE TO DOS (0x275f3). Spec: docs/gbl/A_frame.md sections 3 and 4 (GBL MW2SHELL.EXE VAs).
 * Included by tools/gblshell.c (after the core); every page runs on gbl_frame() (present + input, one call per frame).
 */

/* ======================================================================================================================
 * helpers */

/* the screen under the menu: the frame, the queued text (windowed mode draws text from a queue, see gbl_common.c), the
 * background (the pages load their own; the panel and the text use the background's palette) */
typedef struct {
    uint8_t *fb;
    text_item text[256]; int ntext;
    int bg;
} gbl_frame_screen;
static int gbl_frame_save(gbl_frame_screen *s)
{
    s->fb = malloc(sizeof g_fb);
    if (!s->fb) return -1;
    memcpy(s->fb, g_fb, sizeof g_fb);
    gbl_cursor_unsave(s->fb);   /* without the pointer of the frame it was taken from */
    memcpy(s->text, g_text, sizeof g_text); s->ntext = g_ntext;
    s->bg = g_bg_entry;
    return 0;
}
static void gbl_frame_restore(const gbl_frame_screen *s)
{
    if (s->bg != g_bg_entry && s->bg >= 0) load_bg(s->bg);
    memcpy(g_fb, s->fb, sizeof g_fb);
    memcpy(g_text, s->text, sizeof g_text); g_ntext = s->ntext;
}

/* the clip slots stand still while a modal page or the menu is up (the original does not call FUN_0003ca50 there): on
 * return their clocks are moved on by the time spent, so they do not race to catch up */
static void gbl_frame_slots_resume(Uint32 since)
{
    int i;
    Uint32 d = g_now - since;
    for (i = 0; i < GS_SLOTS; i++) if (g_gs[i].used) g_gs[i].last += d;
}

/* palette index nearest to a colour in the current background's palette */
static int gbl_frame_colour(int r, int g, int b) { return cc_colour(r, g, b); }

/* the clip amwlogo1 at (120, 4) the pages play (FUN_0003c7e0(obj, "amwlogo1", 0x78, 4); FUN_0003c9c0 steps it at the
 * clip's rate and starts it again after the last frame) - its own Smacker handle, so the screen's clip slots are left
 * alone */
typedef struct { smk *s; Uint32 last; } gbl_frame_logo;
static void gbl_frame_logo_open(gbl_frame_logo *l) { l->s = open_smk("AMWLOGO1"); l->last = g_now; if (l->s) smk_next(l->s); }
static void gbl_frame_logo_draw(gbl_frame_logo *l)
{
    if (!l->s) return;
    if (g_now - l->last >= (Uint32)smk_frame_ms(l->s)) { smk_next(l->s); l->last = g_now; }
    blit_smk(l->s, 120, 4);
}
static void gbl_frame_logo_close(gbl_frame_logo *l) { if (l->s) smk_close(l->s); l->s = NULL; }

/* fill a rectangle with palette index 0 (FUN_00041070 with the screen's +0x3a2 = 0: the pages fill, not restore) */
static void gbl_frame_fill(int x0, int y0, int w, int h)
{
    int x, y;
    const uint8_t *c = g_bg.pix ? g_bg.pal[0] : (const uint8_t *)"\0\0\0";
    for (y = y0; y < y0 + h && y < H; y++)
        for (x = x0; x < x0 + w && x < W; x++)
            if (x >= 0 && y >= 0) memcpy(g_fb + ((size_t)y * W + (size_t)x) * 3, c, 3);
    gbl_text_cover(x0, y0, x0 + w, y0 + h);
}

/* a line of text in a GBL font, as FUN_00041290 draws it (top-left at x, y; no markup) */
static void gbl_frame_text(int fe, int x, int y, const char *t, int remap) { draw_text(gfont(fe), x, y, t, gbl_pal(), remap); }

/* any key, a left or a right button (the pages' "leave" test) */
static int gbl_frame_any(const gbl_input *in) { return in->quit || in->lclick || in->rclick || in->key; }

/* ======================================================================================================================
 * the message dialog FUN_00027a40 ("Text|line#Button|Button", the default button's index): panel DATABASE idx 6 (entry 5)
 * frame 2 at (172, 165) 296x150; text lines centred on x 320 from y 197 (0xc5) every 16 in the default font; buttons
 * centred at 320 - 30 (n - 1) + 61 k, y 276 (0x114), boxes +-26 x +-13 drawn with the panel's frame 0 (up) / 1 (pressed)
 * at 64x24; text y = 276 - height / 2. A press on a button pushes it, the release over it picks it; Enter and Esc pick the
 * default; a letter key picks the button starting with it (upper-cased). The menu's FLEE TO DOS (0x275f3) is the same
 * code inline. The same layout as the base shell_dialog (gbl_common.c), here on gbl_frame's input. */
static int gbl_frame_dialog(const char *msg, int def)
{
    char buf[256], *lines[8], *btn[4], *p;
    int n = 0, nb = 0, i, chosen = -1, pressed = -1, held = -1;
    gbl_frame_screen sv;
    if (gbl_frame_save(&sv)) return def;
    snprintf(buf, sizeof buf, "%s", msg);
    if ((p = strchr(buf, '#'))) {
        char *b = p + 1;
        *p = 0;
        while (b && nb < 4) { btn[nb++] = b; b = strchr(b, '|'); if (b) *b++ = 0; }
    }
    p = buf;
    while (p && n < 8) { lines[n++] = p; p = strchr(p, '|'); if (p) *p++ = 0; }
    if (def < 0 || def >= nb) def = nb > 0 ? nb - 1 : 0;
    while (chosen < 0) {
        gbl_input *in;
        int hit = -1;
        memcpy(g_fb, sv.fb, sizeof g_fb);
        memcpy(g_text, sv.text, sizeof g_text); g_ntext = sv.ntext;
        db_shp_frame(5, 2, 172, 165);
        gbl_text_cover(172, 165, 172 + 296, 165 + 150);
        for (i = 0; i < n; i++) gbl_frame_text(GF_DEFAULT, 320 - gtext_w(GF_DEFAULT, lines[i]) / 2, 197 + 16 * i, lines[i], -1);
        for (i = 0; i < nb; i++) {
            int cx = 320 - 30 * (nb - 1) + 61 * i;
            db_shp_frame(5, i == pressed ? 1 : 0, cx - 26, 263);
            gbl_frame_text(GF_DEFAULT, cx - gtext_w(GF_DEFAULT, btn[i]) / 2, 276 - gtext_h(GF_DEFAULT) / 2, btn[i], -1);
        }
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) { chosen = def; break; }
        for (i = 0; i < nb; i++) {
            int cx = 320 - 30 * (nb - 1) + 61 * i;
            if (in->mx >= cx - 26 && in->mx <= cx + 26 && in->my >= 263 && in->my <= 289) hit = i;
        }
        if (in->key) {
            if (in->key == SDLK_RETURN || in->key == SDLK_KP_ENTER || in->key == SDLK_ESCAPE) chosen = def;
            else for (i = 0; i < nb; i++) if (toupper((unsigned char)btn[i][0]) == toupper((int)in->key & 0x7f)) { chosen = i; break; }
            if (nb == 0) chosen = 0;
        }
        if (in->lclick) held = pressed = hit;
        else if (held >= 0 && in->ldown) pressed = hit == held ? held : -1;   /* pushed while the pointer stays on it */
        if (in->lup && held >= 0) { if (hit == held) chosen = held; held = pressed = -1; }
    }
    gbl_frame_restore(&sv);
    free(sv.fb);
    return chosen;
}

/* ======================================================================================================================
 * THE KESHIK's text (copied from docs/gbl/A_frame_credits.h) */
/* GBL MW2SHELL.EXE "THE KESHIK" credits: table 0x7da3c, 642 (0x282) char* entries, NULL = blank line.
 * Traced: FUN_00022320. First char: '<' font idx 0x1d (entry 28), '>' idx 0x1e (entry 29),
 * '~' idx 0x1d with colour map 1->0x10, else idx 0x22 (entry 33, whole string). Lines 0-254 are new (GBL team);
 * 255-641 = the base MW2 list (0x7ca80) with its title changed and 4 names added (see A_frame.md). */
static const char *const GBL_CREDITS[642] = {
    "<ACTIVISION presents",
    NULL,
    "<MECHWARRIOR 2: Ghost Bear's Legacy",
    NULL,
    "<based upon the BattleTech Universe created by",
    NULL,
    "<FASA CORPORATION",
    NULL,
    NULL,
    NULL,
    NULL,
    "<PRODUCTION",
    NULL,
    "Associate Producer",
    ">JACK MAMAIS",
    NULL,
    "Game Designed By",
    ">CHAD FINDLEY",
    NULL,
    "Original Story By",
    ">CHAD FINDLEY",
    ">JACK MAMAIS",
    ">ZACK NORMAN",
    NULL,
    "Written By",
    ">SACHA HOWELLS",
    NULL,
    "Producer",
    ">TIM MORTEN",
    NULL,
    "Executive Producer",
    ">JOHN SPINALE",
    NULL,
    NULL,
    "<PROGRAMMING",
    NULL,
    "Shell/Sim Programming",
    ">BILL FERRER",
    NULL,
    "Additonal Programming",
    ">JOHN CLARKE",
    ">SCOTT T. ETHERTON",
    NULL,
    "Tools/Drivers Licensed From",
    ">JOHN MILES",
    ">JOHN RATCLIFF",
    ">JOHN LEMBERGER",
    NULL,
    NULL,
    "<ART",
    NULL,
    "Art Director",
    ">JACK BURTON",
    NULL,
    "3D Animation",
    ">JJ FRANZEN",
    NULL,
    "3d Animation and Mission Geometry",
    ">SEAN KINNEAR",
    NULL,
    "Ice Clan and Temp Base Screens By",
    ">SCOTT GOFFMAN",
    NULL,
    "Jumpship Bridge By",
    ">ALAN IGLESIAS",
    NULL,
    "2d Art, Animations, and Palettes By",
    ">DANNY MATSON",
    NULL,
    "Additional Art By",
    ">BRIAN JENNINGS",
    NULL,
    NULL,
    "<AUDIO",
    NULL,
    "Original Musical Score",
    ">JEEHUN HWANG",
    NULL,
    "Sound Engineering and Design",
    ">Bill Black",
    NULL,
    "Intro Movie Soundtrack Created and Mixed By",
    ">SOUNDELUX MEDIA LABS:",
    "Sound Supervised & Mixed By",
    ">SCOTT MARTIN GERSHEN",
    ">ALAN RANKIN",
    NULL,
    "Weapon, Transition, and Intro Movie Sound Effects",
    ">ALAN RANKIN",
    ">GREGORY J. HAINER",
    NULL,
    "Voice-Over Recording",
    ">MICHAEL KELLER",
    NULL,
    "Dialogue Editting",
    ">GREGORY J. HAINER",
    ">MICHAEL KELLER",
    ">TERI MATSON",
    NULL,
    "Audio Conversions and Mastering",
    ">TERI MATSON",
    NULL,
    "Voice-Over Talent",
    ">ZACHARY NORMAN",
    ">SCOTT MARTIN GERSHEN",
    ">PETER BYRD",
    ">CAROLE RUGGIER",
    ">BILL BLACK",
    ">BARRETT LATHAM",
    ">PAULA WESTON",
    ">LEE HILL, JR.",
    ">PETER JESSOP",
    ">GRAHAM LAU",
    ">JJ FRANZEN",
    ">JACK MAMAIS",
    ">CHAD FINDLEY",
    NULL,
    NULL,
    "<PACKAGING AND PROMOTIONAL MATERIALS",
    NULL,
    "Documentation Manager",
    ">MIKE RIVERA",
    NULL,
    "Manual Writer",
    ">LORRAINE SUZKI",
    NULL,
    "Manual Layout",
    ">SYLVIA ORZEL",
    NULL,
    "Packaging",
    ">JONATHEN BROWN",
    ">RON GRAENING",
    NULL,
    "Additonal Cover Art",
    ">SCOTT GOFFMAN",
    NULL,
    NULL,
    "<FASA",
    NULL,
    "Original Battletech Designs",
    ">DANA KNUTSON",
    ">JAMIE NELSON",
    ">STEVE VENTERS",
    NULL,
    "Battletech Background Material",
    ">THE ADVENTURE ARCHITECTS",
    ">BRENT CARTER",
    ">ROB CRUZ",
    ">EVAN JAMESON",
    ">RODNEY KNOX",
    ">SAM LEWIS",
    ">BRYAN NYSTUL",
    ">MICHAEL PELLICCIOTTI ",
    ">BOY PETERSON",
    ">DIANE PIRON-GELMAN",
    ">SHARON TURNER-MULVHILL",
    ">CHRISTOPHER HUSSEY",
    ">LAURIE MAIR",
    ">GENE MARCIL",
    ">STEPHANE MATIS",
    NULL,
    "Ghost Bear Logo Designed By",
    ">DAVID R. DEITRICK",
    NULL,
    NULL,
    "<QUALITY ASSURANCE",
    NULL,
    "Quality Assurance Director",
    ">JON DOELLSTEDT",
    NULL,
    "Quality Assurance Manager",
    ">DAVE ARNSPIGER",
    NULL,
    "Lead Tester",
    ">MATT CANDLER",
    NULL,
    "Test Team",
    ">DAVID AVERY",
    ">CHRIS CAMPBELL",
    ">JOHN CIBULSKI",
    ">DAVID FIELDING",
    ">RUE HON",
    NULL,
    "Additional Testers",
    ">KEITH ALEXANDER",
    ">ROBERT BERGER",
    ">JUDITH CHLIPALA",
    ">CURTIS CROCKETT",
    ">MARK HARWOOD",
    ">GLENN JOST",
    ">NOAH KATZ",
    ">NADIA KISCHK",
    ">STEVEN MAU",
    ">DAVID OSPER",
    ">TYLER SCOTT",
    ">ELIZABETH STROZEWSKI",
    ">TIM VANLAW",
    ">ERIC ZALA",
    NULL,
    "External Beta Testers",
    ">CHARLES BOWLBY",
    ">BILL \"AXOR\" BROWN",
    ">PAUL CABANA",
    ">ALEX CHAN",
    ">DARREN CHANG",
    ">GARY and DOLLY COOK",
    ">RICH DEFRANCESCO",
    ">SVEN DOERSAM",
    ">BRIAN EICHLER",
    ">CARL FINLEY",
    ">RICHARD GROCHOWSKI",
    ">JODY JOHNSON",
    ">MARK KAELIN",
    ">BILL KIRKMAN",
    ">SHAWN KLOMP",
    ">MATTEW LEWIS",
    ">ED MILOVIC",
    ">ANTHONY PHAM",
    ">KAREN RAPCHAK",
    ">DONALD RINKER",
    ">JOSEPH RUFFOLO",
    ">JAMES SABLATURA",
    ">JULIE SMITH",
    ">MONTGOMERY SPENCER",
    ">MIKE UDOVIC",
    NULL,
    NULL,
    "Special Thanks To...",
    NULL,
    ">TIM MORTEN",
    ">TOM DOWD",
    ">JOHN LAFLEUR",
    ">JOSH RESNICK",
    ">KEN HULLETT",
    ">MICHAEL SHWARTZ",
    ">NATALIE DESCHATRES",
    ">RAY CHOI",
    ">SEAN ESPINOZA",
    ">INDRA GUNAWAN",
    ">DAVID GREENSPAN",
    ">BRYAN NYSTUL",
    ">ALAN GERSHENFELD",
    ">HOWARD MARKS",
    ">TREY WATKINS",
    ">SARAH HANLON",
    ">STEVE WILLSEY",
    ">DAN STANFILL",
    ">KELLY ROGERS",
    ">JOHN C. PECK, JR.",
    ">DAIVA VENCKUS",
    ">SCULLY",
    ">JOHN KEATING",
    ">GRAHAM LAU",
    NULL,
    NULL,
    NULL,
    "<MECHWARRIOR 2: 31ST CENTURY COMBAT",
    NULL,
    NULL,
    NULL,
    "<PRODUCTION",
    NULL,
    ">DIRECTOR",
    "John Spinale",
    NULL,
    ">PRODUCER",
    "Josh Resnick",
    NULL,
    ">LEAD GAME DESIGNER",
    "Sean Vesce",
    NULL,
    ">GAME DESIGNER AND WRITER",
    "Zachary Norman",
    NULL,
    ">ASSOCIATE GAME DESIGNERS",
    "Chad Findley",
    "Ken Hullett",
    "David White",
    NULL,
    ">PRODUCTION COORDINATORS",
    "Lars Fuhrken-Batista",
    "Andrew Held",
    NULL,
    ">PRODUCTION ASSISTANT",
    "Jeehun Hwang",
    NULL,
    NULL,
    "<PROGRAMMING",
    NULL,
    ">SIMULATION PROGRAMMING",
    "John A. Clarke",
    "Michael H. Douglas",
    "Scott T. Etherton",
    "John Keating",
    "Tim Morten",
    "Bob Mortensen",
    "Eric Peterson",
    "Dan Stanfill",
    "Dave Zobel",
    NULL,
    ">SHELL PROGRAMMING by REDLINE GAMES",
    "James E. Anhalt, III",
    "John C. Peck, Jr.",
    NULL,
    ">ADDITIONAL PROGRAMMING",
    "David White",
    NULL,
    ">PROGRAMMING INTERNS",
    "Brian Jennings",
    "Daniel Kamins",
    NULL,
    ">TOOLS/DRIVERS LICENSED FROM",
    "John Lemberger",
    "John Miles",
    "John Ratcliff",
    NULL,
    ">INSTALLER",
    "Greg Sanborn, Quicksilver Software",
    NULL,
    NULL,
    "<ART",
    NULL,
    ">SIMULATION COLOR, LIGHTING, TEXTURES AND ANIMATIONS",
    "Danny Matson",
    NULL,
    ">ADDITIONAL SIMULATION ART DIRECTION",
    "Danny Matson",
    NULL,
    ">3D SIMULATION MODELS AND ANIMATIONS",
    "J.J. Franzen",
    NULL,
    ">ADDITIONAL 3D SIMULATION GEOMETRY",
    "Sean Kinnear",
    NULL,
    ">SHELL STILLS AND ANIMATIONS DIRECTION",
    "Jefferson Elliot",
    NULL,
    ">SHELL STILLS AND ANIMATIONS",
    "Jack Burton",
    "Scott Goffman",
    "Jim Mitchell",
    NULL,
    ">CLAN INTRO MOVIES",
    "Scott Goffman",
    "Jim Mitchell",
    NULL,
    ">CLAN END MOVIES",
    "Scott Goffman",
    NULL,
    ">INTRO MOVIE by DIGITAL DOMAIN",
    "EXECUTIVE PRODUCER              Chris McKibbon",
    "DIRECTOR OF ANIMATIONS          Darnell Williams",
    "CHARACTER ANIMATION             Craig Caton",
    "COMPOSITING SUPERVISOR          Nathan Wilson",
    "ADDITIONAL COMPOSITING          Steve Gray",
    "COLOR & LIGHTING CONSULTANT     Daniel Robichaud",
    "UNIT PRODUCTION MANAGER         Susan Thurmond",
    NULL,
    NULL,
    "<AUDIO",
    NULL,
    ">MUSIC AND SOUND DIRECTOR",
    "Kelly Rogers",
    NULL,
    ">ORIGINAL MUSICAL SCORE",
    "Gregory Alper",
    NULL,
    ">ADDITIONAL MUSIC",
    "Jeehun Hwang",
    NULL,
    ">MUSICAL SCORES FOR ALL MOVIE SEQUENCES",
    ">AND TRIALS OF GRIEVANCE",
    "Jeehun Hwang",
    NULL,
    ">MUSICAL SCORES FOR JADE FALCON CLAN",
    "Jeehun Hwang",
    NULL,
    ">MUSICAL SCORES FOR WOLF CLAN",
    "Gregory Alper and Jeehun Hwang",
    NULL,
    ">AUDIO ENGINEER",
    "Michael Schwartz",
    NULL,
    ">MIDI ENGINEER",
    "Bill Black",
    NULL,
    ">SOUND DESIGN",
    "SOUNDELUX MEDIA LABS",
    NULL,
    ">SOUND EFFECTS SUPERVISION & SOUND DESIGN",
    "Scott Martin Gershen",
    "Michael Reagan",
    NULL,
    ">SOUND EFFECTS MASTERING",
    "Teri Madsen",
    "Caron Weidner",
    NULL,
    ">VOICE OVER TALENT",
    "Bryan Bowen",
    "Scott Martin Gershen",
    "John Keating",
    "Zachary Norman",
    "Michael Reagan",
    "Carol Ruggier",
    NULL,
    NULL,
    "<PACKAGING AND PROMOTIONAL MATERIALS",
    NULL,
    ">CREATIVE DIRECTOR",
    "Ron Gould",
    NULL,
    ">DOCUMENTATION MANAGER",
    "Mike Rivera",
    NULL,
    ">GRAPHIC DESIGNER",
    "Jonathan Brown",
    NULL,
    ">ADDITIONAL COVER ART",
    "Scott Goffman",
    "Danny Matson",
    NULL,
    ">PACKAGE COPYWRITER",
    "Veronica Milito",
    NULL,
    ">MANUAL DESIGNER",
    "Marcella Missirian",
    NULL,
    ">MANUAL WRITER",
    "Veronica Milito",
    NULL,
    ">MANUAL ILLUSTRATIONS",
    "Zachary Norman",
    NULL,
    ">MECHWARRIOR2 WEB SITE COORDINATOR",
    "Sean Lally",
    NULL,
    ">MECHWARRIOR2 WEB SITE",
    "http://www.activision.com",
    NULL,
    NULL,
    "<FASA",
    NULL,
    ">BATTLETECH UNIVERSE CREATORS",
    "Jordan Weisman and L. Ross Babcock III",
    NULL,
    ">ORIGINAL BATTLEMECH DESIGNS",
    "Dana Knutson",
    "Jim Nelson",
    "Steve Venters",
    NULL,
    ">FALCON AND THE WOLF SCENARIOS",
    "Rodney Knox",
    "Bryan Nystul",
    NULL,
    ">3D RENDERING ASSISTANCE",
    "Jamie Marshall",
    NULL,
    ">BATTLETECH BACKGROUND MATERIAL",
    "The Adventure Architects",
    "Brent Carter",
    "Rob Cruz",
    "Evan Jameson",
    "Sam Lewis",
    "Bryan Nystul",
    "Michael Pellicciotti",
    "Boy Peterson",
    "Diane Piron-Gelman",
    "Sharon Turrier-Mulvhill",
    NULL,
    NULL,
    NULL,
    "<QUALITY ASSURANCE",
    NULL,
    ">QUALITY ASSURANCE DIRECTOR",
    "Jon Doellstedt",
    NULL,
    ">QUALITY ASSURANCE MANAGER",
    "Dave Arnspiger",
    NULL,
    ">LEAD TESTERS",
    "John Lafleur",
    "Jack Mamais",
    NULL,
    ">TEST TEAM",
    "Keith Alexander",
    "David Avery",
    "Robert Berger",
    "Steven Bishop",
    "Tom Butler",
    "Chris Campbell",
    "Matthew Candler",
    "Linus Chen",
    "John Cibulski",
    "Curtis Crockett",
    "Matthew Gibbs",
    "Mark Harwood",
    "George Hoyt",
    "Nate Marston",
    "Elvis Murray",
    "Daiva Venckus",
    "Zack Wood",
    NULL,
    ">ADDITIONAL TESTERS",
    "Kurt Barker",
    "Rick Baumgartner",
    "Peter Blumel",
    "Willie Bolton",
    "Aaron Cammarata",
    "Judith Chlipala",
    "Ray Choi",
    "Tyron Chookolingo",
    "Ingrid de Beus",
    "Andre Emerson",
    "Sean Espinoza",
    "John Fair",
    "Greg Fulton",
    "Tony Grant",
    "Seth Grenald",
    "Daniel Kamins",
    "David King",
    "Danny Lee",
    "Jacque LaMaire",
    "Tim McMahon",
    "Michael O'Brien",
    "Brad Pickering",
    "Chris Strompolos",
    "Murali Tegulapalle",
    "Nadine Theuzillot",
    "David R. Tulo, Jr.",
    "Mark Volpe",
    "Matt Weinstein",
    "William Westwater",
    "Paul Wiley",
    NULL,
    ">EXTERNAL BETA TESTERS",
    "Weslee Bilodeau",
    "Todd Bilsborrow",
    "Dave Bourque",
    "Charles Bowlby",
    "Bill \"Axor\" Brown",
    "Paul Cabana",
    "Alex Chan",
    "Gary & Dolly Cook",
    "Rich De Francesco Jr.",
    "Jesse Derks",
    "Mark Dittenber",
    "Trent Ditto",
    "Sven Doersam",
    "Pamela Dreizen",
    "Brian Eichler",
    "David Ferrell",
    "Carl Finley",
    "Robert George",
    "Richard Grochowski",
    "Steven Hawley",
    "Cezzanne Huq",
    "James Jackson",
    "Brian James",
    "Jody Johnson",
    "Mark Kaelin",
    "Lenore Kaye",
    "Bill Kirkman",
    "Shaun Klomp",
    "Stephen Lafleur",
    "Matthew Lewis",
    "Ed Milovic",
    "Ford Maxim",
    "Steven Mo",
    "Dayle Moore",
    "Mat O'Connor",
    "Scooter Oehlerking",
    "Anthony Pham",
    "Richard Powell",
    "Alan Precourt",
    "Karen Rapchak",
    "Mark Reindl",
    "Donald Rinker",
    "Jason Robinson",
    "John Roper",
    "Loring Rose",
    "Joseph Ruffolo",
    "Jorja Rule",
    "James Sablatura",
    "Rick Salvador",
    "Julie Smith",
    "Montgomery Spencer",
    "Ryan Tykwinski",
    "Mike Udovic",
    "Bernard Yee",
    NULL,
    NULL,
    "<SPECIAL THANKS TO...",
    "All the wives and girlfriends",
    "John Clarke and Tim Morten (for the Second Coming)",
    "Dave and RJ (for their percussion talents)",
    "Doug Bambridge",
    "John Bruther",
    "Chris Campbell",
    "Ray Choi",
    "Tyron Chookolingo",
    "Brad Crystal",
    "Roy the Wonder Boy",
    "Nathalie Deschatres",
    "Tom Dowd, Denny Thorley and Mort Weisman (FASA)",
    "John Fair",
    "Michael Fletcher",
    "Alan Gershenfeld",
    "Larry Goldberg",
    "Vinod Gupta",
    "Sarah Hanlon",
    "Snoopy the Hamster",
    "Trials of Grievance: Hobbes",
    "Eric Johnson",
    "Kerstine Johnson",
    "Brian Kelly",
    "Edward Kilham and Kalani Streicher (Ronin Entertainment)",
    "Scott Lahman",
    "Mitch Lasky",
    "Maryanne Lataif",
    "Alan Lazar",
    "Howard Marks",
    "Mom",
    "Brad Pickering",
    "Barry Plaga",
    "Tom Sloper",
    "Smacker Boy",
    "Ben Tenn",
    "Trey Watkins",
    "The Original Production Crew",
    "...and all the other folks at Activision who helped this game come to life",
    NULL,
    NULL,
    "<THE LEGAL STUFF",
    NULL,
    "Mechwarrior, BattleTech, BattleMech and 'Mech are registered trademarks of FASA CORPORATION.  Used with permission.",
    NULL,
    "Activision is a registered trademark of Activision, Inc. (c) 1995 Activision, Inc.  All rights reserved.",
    NULL,
    NULL,
    NULL,
    "<Can't We All Just Get Along?",
    NULL,
};

/* ======================================================================================================================
 * the campaign titles for the Hall of Honor's "Last Mission" (0x800ac, 9-byte records {scenario, trial, title}: the
 * HoH reads 0x8017c[pilot +8][(done - 1) * 9 + 5], the title). A local copy of C_campaign.md 2.3 GBL_MISSIONS. */
static const char *gbl_frame_mission_title(int i)
{
    static const char *const T[23] = {
        "Trial on Alshain", "Defend on Mannedorf", "Patrol on Mannedorf", "Trial on Alshain", "Defend on Chandler",
        "Strike on Chandler", "Strike on Wolcott", "Circle of Equals", "Raid on Jeanette", "Defend on Jumpship",
        "Trial of Possession", "Defend on Alshain", "Trial on Alshain", "Underwater Strike", "Strike on Moritz",
        "Strike on Moritz 2", "Destroy Orbital Platform", "Bloodright Trial 1", "Bloodright Trial 2", "Bloodright Trial 3",
        "Bloodright Trial 4", "Final Bloodright Trial", "Retired"};
    return i >= 0 && i < 23 ? T[i] : "";
}

/* ======================================================================================================================
 * COMBAT VARIABLES (menu case 1, 0x27323). raw idx 8 (entry 7, the slider SHP) -> 0x95e84; background idx 3 (entry 2);
 * the value column (375, 124) 258x352 filled black (FUN_00041070 with +0x3a2 = 0); amwlogo1 at (120, 4); MW2SND.CFG
 * (0x40 bytes: GBL's +0x3c TRANSITIONS) and MW2DIF.CFG (8) read; the widget table 0x7e46c {x, y, w, h, .., draw, click,
 * data}: values drawn in font idx 0x1d (entry 28) at (x + w / 2, y) - '~' strings centred there, "ON (Dishonorable)"
 * left-aligned 14 px left of it - and a click on (x, y) w 100 x (the text's height) runs the widget's click:
 *   0 DIFFICULTY y 219 MW2DIF +5 (byte 0..2: ~EASY ~MEDIUM ~HARD)       1 HEAT TRACKING 239 MW2DIF +4 (byte: ~ON / ~OFF)
 *   2 OBJECT TEXTURES 277, 3 TERRAIN TEXTURES 297, 6 CHUNKY EXPLOSIONS 357: MW2SND +0x14 / +0x18 / +0x24 (int: ~ON / ~OFF)
 *   4 DISPLAY DETAIL 317, 5 OBJECT DENSITY 337: MW2SND +0x1c / +0x20 (int: ~HIGH / ~LOW)
 *   7 RESOLUTION 377 MW2SND +0x2c (driver name: "" ~320x200 -> vesa480.dll ~640x480 -> vesa768.dll ~1024x768 -> "")
 *   8 TRANSITIONS 397 MW2SND +0x3c (int: ~ON / ~OFF) - new in GBL (the mission clip lists, g_transitions)
 *   9 INVULNERABILITY 416 MW2DIF +1, 10 UNLIMITED AMMO 436 MW2DIF +0 (0x268b0: byte != 0 "ON (Dishonorable)" else ~OFF)
 *   11 NO COLLISION DAMAGE 456 MW2DIF +3 (0x26900, the other way round: byte == 0 "ON (Dishonorable)", else ~OFF)
 *   13-15 MUSIC / EFFECTS / VOICE sliders at (335, 128 / 152 / 176) 285x21: MW2SND +0xc / +4 / +8, 16.16 (0x26ab0 /
 *   0x26b20: knob at x + 15 + v - 7, v = level >> 8; a click sets v = mouse x - (x + 15), 0..256).
 * Leaves on Esc or the right button only (`while (FUN_0002e280(evq) != 3)`, FUN_0003b1e0 break; base: any key), then
 * writes both files back (MW2SND 0x40 bytes, MW2DIF 8).
 * The port's own settings, as the base port's combat_variables() has them: RENDERER (y 239) and TEXTURES (y 258) rows
 * heading the DETAIL group - the artwork's DIFFICULTY group (heading and HEAT TRACKING) drawn 20 px higher and the DETAIL
 * heading moved up to the RENDERER row, as the base port does; TEXTURES' right part (x 495 on) DOS DITHERED / SMOOTH;
 * RESOLUTION's NATIVE choices (the display's resolution, AA 2X / 4X / 8X). The base port's DISPLAY row sat at y 397,
 * where GBL has TRANSITIONS: here the port's DISPLAY (desktop / windowed / the display's modes) is the right part of the
 * HEAT TRACKING row (FULL SCREEN / WINDOWED / WxH) and the HUD layout (HUD WIDE / HUD ORIGINAL) the right part of the DIFFICULTY row (x 495 on, where
 * those rows' values leave room). The port's words are drawn in the values' own font (entry 28). */
static void gbl_combat_variables(void)
{
    uint8_t snd[0x40], dif[8], snd0[0x40], dif0[8];
    FILE *f;
    portcfg pc, pc0;
    int i, nmodes = 0, mi = -1, grey;
    struct { int w, h; } modes[64];
    gbl_frame_logo logo;
    /* rows: the original's (DIFFICULTY and HEAT TRACKING 20 px up, see above), then the port's RENDERER, TEXTURES */
    static const int ROW_Y[14] = {199, 219, 277, 297, 317, 337, 357, 377, 397, 416, 436, 456, 239, 258};
    static const char *const GBL_LOOK_TEXT[PORTCFG_GBL_COUNT] = {"DOS", "ENHANCED", "TITANIUM"};
    static const char *const TX_TEXT[PORTCFG_TX_COUNT] = {"ORIGINAL", "TITANIUM"};
    static const char *const DIFF[3] = {"~EASY", "~MEDIUM", "~HARD"};
    static const int SY[3] = {128, 152, 176}, SO[3] = {0x0c, 0x04, 0x08};
    static uint8_t *sl_shp;
    static long sl_len = -1;
    memset(snd, 0, sizeof snd); memset(dif, 0, sizeof dif);   /* ASSUMED defaults when a file is missing (base port's) */
    wr32(snd + 0x0c, 65536); wr32(snd + 0x04, 65536); wr32(snd + 0x08, 65536);
    for (i = 0x14; i <= 0x24; i += 4) wr32(snd + i, 1);
    wr32(snd + 0x3c, 1);
    dif[4] = 1; dif[5] = 1; dif[3] = 1;
    if ((f = dp_fopen("MW2SND.CFG", "rb"))) { if (fread(snd, 1, sizeof snd, f)) {} fclose(f); }
    if ((f = dp_fopen("MW2DIF.CFG", "rb"))) { if (fread(dif, 1, sizeof dif, f)) {} fclose(f); }
    memcpy(snd0, snd, sizeof snd); memcpy(dif0, dif, sizeof dif);
    portcfg_load(g_dir, &pc);
    {   /* RENDERER shows the look the simulation will use: the configured one when its files are here (DOS / ENHANCED: the
         * DOS MW2.PRJ; TITANIUM: titanium/MW2.PRJ), else the core's choice (gblshell.c falls back to DOS) */
        char m1[PORTCFG_PATH], m2[PORTCFG_PATH], m3[PORTCFG_PATH], m4[PORTCFG_PATH], m5[PORTCFG_PATH];
        if (portcfg_edition(&g_cfg, pc.renderer, m1, m2, m3, m4, m5) != 0) pc.renderer = g_cfg.renderer;
        if (!portcfg_gbl_look_ok(&g_cfg, portcfg_gbl_look(&pc))) portcfg_gbl_set_look(&pc, PORTCFG_GBL_DOS);   /* (agent M) */
    }
    pc0 = pc;
    {
        int n = g_headless ? 0 : SDL_GetNumDisplayModes(0), k, j;
        for (k = 0; k < n && nmodes < 64; k++) {
            SDL_DisplayMode dm;
            if (SDL_GetDisplayMode(0, k, &dm) != 0) continue;
            for (j = 0; j < nmodes; j++) if (modes[j].w == dm.w && modes[j].h == dm.h) break;
            if (j == nmodes && dm.w >= 640 && dm.h >= 480) { modes[nmodes].w = dm.w; modes[nmodes].h = dm.h; nmodes++; }
        }
        if (pc.mode == 2) for (j = 0; j < nmodes; j++) if (modes[j].w == pc.w && modes[j].h == pc.h) mi = j;
    }
    gbl_bg(3);
    grey = gbl_frame_colour(110, 110, 110);
    if (sl_len < 0) sl_len = gdb_entry(GDB(8), &sl_shp);
    gbl_frame_logo_open(&logo);
    for (;;) {
        gbl_input *in;
        char res[32], disp[48];
        const char *val[14];
        int vx[14], vdim[14];
        draw_bg();
        {   /* the port's layout (see above): the DIFFICULTY group's artwork 20 px up, the DETAIL heading to the RENDERER row */
            int x, y;
            for (y = 219; y <= 252; y++) for (x = 0; x < 375; x++) {
                uint8_t *q = g_fb + ((size_t)y * W + (size_t)x) * 3;
                memcpy(q - (size_t)20 * W * 3, q, 3);
                if (y > 252 - 20) memcpy(q, g_bg.pal[0], 3);
            }
            for (y = 276; y <= 294; y++) for (x = 0; x < 171; x++) {
                uint8_t *q = g_fb + ((size_t)y * W + (size_t)x) * 3;
                memcpy(q - (size_t)38 * W * 3, q, 3);
                memcpy(q, g_bg.pal[0], 3);
            }
        }
        gbl_frame_fill(375, 124, 258, 352);   /* FUN_00041070(screen, 0x177, 0x7c, 0x102, 0x160), +0x3a2 = 0 */
        /* the values */
        val[0] = DIFF[dif[5] <= 2 ? dif[5] : 1];
        val[1] = dif[4] ? "~ON" : "~OFF";
        for (i = 0; i < 5; i++) {
            int v = rd32(snd + 0x14 + 4 * i);
            val[2 + i] = i == 2 || i == 3 ? (v ? "~HIGH" : "~LOW") : (v ? "~ON" : "~OFF");
        }
        if (pc.aa) snprintf(res, sizeof res, "~NATIVE %dX AA", pc.aa); else snprintf(res, sizeof res, "~NATIVE");
        /* RESOLUTION: the original's three words when the sim draws at one of them (render=), else the port's NATIVE */
        val[7] = pc.render_w == 320 ? "~320x200" : pc.render_w == 640 ? "~640x480" : pc.render_w == 1024 ? "~1024x768" : res;
        val[8] = rd32(snd + 0x3c) ? "~ON" : "~OFF";
        val[9] = dif[1] ? "ON (Dishonorable)" : "~OFF";
        val[10] = dif[0] ? "ON (Dishonorable)" : "~OFF";
        val[11] = dif[3] ? "~OFF" : "ON (Dishonorable)";
        val[12] = GBL_LOOK_TEXT[portcfg_gbl_look(&pc)];   /* gbl's three looks (agent M) */
        val[13] = TX_TEXT[pc.texset == PORTCFG_TX_TITANIUM ? 1 : 0];
        for (i = 0; i < 14; i++) {
            const char *t = val[i];
            int w;
            vdim[i] = i == 13;   /* TEXTURES follows RENDERER (DOS / ENHANCED original, TITANIUM titanium): shown, not chosen */
            if (t[0] == '~') { t++; w = gtext_w(28, t); vx[i] = 443 - w / 2; }
            else if (i < 12) vx[i] = 443 - 14;
            else { w = gtext_w(28, t); vx[i] = 443 - w / 2; }   /* the port's rows: centred too, kept off the labels */
            if (vx[i] < 380) vx[i] = 380;
            gbl_frame_text(28, vx[i], ROW_Y[i], t, vdim[i] ? grey : -1);
        }
        /* the port's labels (right-aligned at 362 like the artwork's) and its right-hand words */
        gbl_frame_text(28, 362 - gtext_w(28, "RENDERER"), ROW_Y[12], "RENDERER", -1);
        gbl_frame_text(28, 362 - gtext_w(28, "TEXTURES"), ROW_Y[13], "TEXTURES", grey);
        if (pc.mode == 0) snprintf(disp, sizeof disp, "FULL SCREEN");   /* the port's DISPLAY: desktop / windowed / a mode */
        else if (pc.mode == 1) snprintf(disp, sizeof disp, "WINDOWED");
        else snprintf(disp, sizeof disp, "%dX%d", pc.w, pc.h);
        gbl_frame_text(28, 500, ROW_Y[0], pc.hud_original ? "HUD ORIGINAL" : "HUD WIDE", -1);
        gbl_frame_text(28, 500, ROW_Y[1], disp, -1);
        gbl_frame_text(28, 500, ROW_Y[13], pc.dos_sky_smooth ? "DOS SMOOTH" : "DOS DITHERED", -1);
        {   /* the port's look of the full-screen videos (portcfg videos=): OBJECT TEXTURES' right part */
            static const char *const VID_TEXT[PORTCFG_VID_COUNT] = {"FMV ORIGINAL", "FMV SMOOTH", "FMV SCANLINE", "FMV CRT", "FMV CURVED"};
            static const char *const FIT_TEXT[PORTCFG_FIT_COUNT] = {"FMV 4:3", "FMV FILL", "FMV STRETCH"};   /* video_fit= */
            gbl_frame_text(28, 500, ROW_Y[2], VID_TEXT[pc.videos >= 0 && pc.videos < PORTCFG_VID_COUNT ? pc.videos : 0], -1);
            gbl_frame_text(28, 500, ROW_Y[3], FIT_TEXT[pc.video_fit >= 0 && pc.video_fit < PORTCFG_FIT_COUNT ? pc.video_fit : 0], -1);
        }
        gbl_frame_logo_draw(&logo);
        for (i = 0; i < 3 && sl_shp && sl_len > 0; i++) {   /* the sliders: frame 1 the track at (335, y), frame 0 the knob */
            int v = rd32(snd + SO[i]), fi;
            v = v < 0 ? 0 : v > 65536 ? 65536 : v;
            v >>= 8;
            for (fi = 1; fi >= 0; fi--) {
                shp_frame fr;
                int ox = fi ? 335 : 335 + 15 + v - 7, oy = fi ? SY[i] : SY[i] - 1, xx, yy;
                if (shp_decode(sl_shp, (size_t)sl_len, fi, &fr) != 0) continue;
                for (yy = 0; yy < fr.h; yy++)
                    for (xx = 0; xx < fr.w; xx++) {
                        int px = ox + fr.left + xx, py = oy + fr.top + yy;
                        if (!fr.mask[yy * fr.w + xx] || px < 0 || py < 0 || px >= W || py >= H) continue;
                        memcpy(g_fb + ((size_t)py * W + (size_t)px) * 3, g_bg.pal[fr.pix[yy * fr.w + xx]], 3);
                    }
                shp_frame_free(&fr);
            }
        }
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit || in->rclick || in->key == SDLK_ESCAPE) break;
        if (in->lclick) {
            int x = in->mx, y = in->my, fh = gtext_h(28);
            for (i = 0; i < 14; i++) {
                if (y < ROW_Y[i] || y >= ROW_Y[i] + fh) continue;
                if (x >= 495 && (i == 0 || i == 1 || i == 2 || i == 3 || i == 13)) {   /* the port's right-hand words */
                    if (i == 0) pc.hud_original ^= 1;
                    else if (i == 3) pc.video_fit = (pc.video_fit + 1) % PORTCFG_FIT_COUNT;   /* FMV size: 4:3 / fill / stretch */
                    else if (i == 2) pc.videos = (pc.videos + 1) % PORTCFG_VID_COUNT;   /* FMV: the full-screen videos' look */
                    else if (i == 1) {   /* DISPLAY: desktop -> windowed -> the display's modes -> desktop */
                        if (pc.mode == 0) pc.mode = 1;
                        else if (pc.mode == 1) { if (nmodes) { pc.mode = 2; mi = 0; pc.w = modes[0].w; pc.h = modes[0].h; } else pc.mode = 0; }
                        else if (++mi < nmodes) { pc.w = modes[mi].w; pc.h = modes[mi].h; }
                        else pc.mode = 0;
                    } else pc.dos_sky_smooth ^= 1;
                    continue;
                }
                if (i < 12 ? (x < 393 || x >= 493) : (x < 375 || x >= 495)) continue;
                switch (i) {
                case 0: dif[5] = (uint8_t)(dif[5] + 1 >= 3 ? 0 : dif[5] + 1); break;   /* 0x269f0 */
                case 1: dif[4] = !dif[4]; break;                                        /* 0x26a30 */
                case 2: case 3: case 4: case 5: case 6: wr32(snd + 0x14 + 4 * (i - 2), !rd32(snd + 0x14 + 4 * (i - 2))); break;   /* 0x26a10 */
                case 7:   /* RESOLUTION (0x26a40 on the driver name): the port's NATIVE (AA off, 2X, 4X, 8X) -> 320x200 -> 640x480 -> 1024x768 */
                    if (!pc.render_w) {
                        if (pc.aa < 8) pc.aa = pc.aa == 0 ? 2 : pc.aa * 2;
                        else { pc.render_w = 320; pc.render_h = 200; }
                    } else if (pc.render_w == 320) { pc.render_w = 640; pc.render_h = 480; }
                    else if (pc.render_w == 640) { pc.render_w = 1024; pc.render_h = 768; }
                    else { pc.render_w = pc.render_h = 0; pc.aa = 0; }
                    {   /* the original's field: "" (VGA) / vesa480.dll / vesa768.dll (strncpy 0xf) */
                        int hh = pc.render_w ? pc.render_h : (pc.mode == 2 ? pc.h : 768);
                        memset(snd + 0x2c, 0, 16);
                        if (hh >= 700) memcpy(snd + 0x2c, "vesa768.dll", 11); else if (hh >= 400) memcpy(snd + 0x2c, "vesa480.dll", 11);
                    }
                    break;
                case 8: wr32(snd + 0x3c, !rd32(snd + 0x3c)); g_transitions = rd32(snd + 0x3c); break;   /* TRANSITIONS */
                case 9: dif[1] = !dif[1]; break;
                case 10: dif[0] = !dif[0]; break;
                case 11: dif[3] = !dif[3]; break;
                case 12: {   /* the port's RENDERER: gbl's looks DOS -> ENHANCED -> TITANIUM (titanium/ needed) (agent M) */
                    int nl = portcfg_gbl_next(&g_cfg, portcfg_gbl_look(&pc));
                    portcfg_gbl_set_look(&pc, nl); portcfg_gbl_set_look(&g_cfg, nl);
                    break;
                }
                }
            }
            for (i = 0; i < 3; i++)   /* MUSIC / EFFECTS / VOICE (0x26b20) */
                if (x >= 335 && x < 335 + 285 && y >= SY[i] && y < SY[i] + 21) {
                    int xc = x - (335 + 15);
                    xc = xc < 0 ? 0 : xc > 256 ? 256 : xc;
                    wr32(snd + SO[i], xc << 8);
                }
        }
    }
    gbl_frame_logo_close(&logo);
    if (memcmp(snd, snd0, sizeof snd) || memcmp(dif, dif0, sizeof dif) || !g_headless) {   /* both written back on exit */
        if ((f = dp_fopen("MW2SND.CFG", "wb"))) { fwrite(snd, 1, sizeof snd, f); fclose(f); }
        if ((f = dp_fopen("MW2DIF.CFG", "wb"))) { fwrite(dif, 1, sizeof dif, f); fclose(f); }
    }
    g_transitions = rd32(snd + 0x3c);
    if (memcmp(&pc, &pc0, sizeof pc)) portcfg_save(g_dir, &pc);
}

/* ======================================================================================================================
 * COCKPIT CONTROLS (menu case 2, FUN_00021f40 = the base FUN_00021020 line for line, A_cockpit.png). The page is the
 * widget table 0x7d61c (11 ints a widget: x, y, w, h, .., colour map, .., draw, click, data; a y of 0x80000000 | n is
 * n below the widget before): the red lines at x 172 / 477 from y 120; "Current Config:" (0, 110) and the config name
 * (80, 110) and "SELECT INPUT DEVICES" (0, 132) in grey (map 0x8ce5c: glyph colour 1 -> 7); twelve device rows from (0,
 * 154) every 11, 100 wide (draw 0x1ffe0: white selectable, red (1 -> 0x10) selected, grey unselectable; click 0x20040
 * toggles); ACCEPT (0, 400), CUSTOM CONFIGURATION (0, 422), ABORT (0, 444), 100 wide, white; the legend "~WHITE =
 * Selectable Option" (320, 250), "~RED = Selected Option" (320, 272, red), "~GREY = Unselectable" (320, 294, grey),
 * centred on 320. The rows are the GIDDI drivers (FUN_0001a530: every giddi\*.dll that loads, at most 16, in directory
 * order - alphabetical on the disc, as the capture shows: GBL's twelve with AVENGER, VFX1, VPILOT and WINGMAN; ASSUMED the
 * same order for an install folder), each titled as its DLL reports it (cc_driver_title); no keyboard.dll: "Error:
 * keyboard.dll not found.#Ok" and no page. A left or right press over a widget with a click runs it (FUN_00034320: x <=
 * mx < x + w, y <= my < y + h; h = the font height); Esc leaves. ACCEPT / CUSTOM CONFIGURATION / ABORT and the editor are
 * the base port's (cockpitcfg.c, gbl_common.c cc_*), which know the base's eight drivers: the four GBL-only ones can be
 * selected here but add nothing to INPUT.MAP (the port's simulation has no driver for them - port limitation). */
static int gbl_cc_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void gbl_cockpit_controls(void)
{
    char drv[16][16];
    int ndrv = 0, base_k[16], k, done = 0, prev_bg = g_bg_entry, has_kbd = 0;
    int sel_extra[16];
    cc_config cfg;
    {   /* the drivers: GIDDI\*.DLL */
        DIR *dd = opendir(giddi_dir());
        struct dirent *de;
        while (dd && (de = readdir(dd)) && ndrv < 16) {
            size_t n = strlen(de->d_name);
            if (n < 5 || n > 12 || strcasecmp(de->d_name + n - 4, ".dll")) continue;
            snprintf(drv[ndrv], sizeof drv[0], "%.*s", (int)(n - 4), de->d_name);
            for (k = 0; drv[ndrv][k]; k++) drv[ndrv][k] = (char)tolower((unsigned char)drv[ndrv][k]);
            if (!cc_driver_title(drv[ndrv])) continue;   /* (the original asks the driver; an unknown one is not shown) */
            ndrv++;
        }
        if (dd) closedir(dd);
        qsort(drv, (size_t)ndrv, sizeof drv[0], gbl_cc_cmp);
    }
    for (k = 0; k < ndrv; k++) {
        int j;
        base_k[k] = -1; sel_extra[k] = 0;
        for (j = 0; j < CC_DEVICES; j++) if (!strcmp(drv[k], CC_DEVNAME[j])) base_k[k] = j;
        if (!strcmp(drv[k], "keyboard")) has_kbd = 1;
    }
    if (!has_kbd) { gbl_frame_dialog("Error: keyboard.dll not found.#Ok", 0); return; }
    load_bg(3);   /* background idx 4 (entry 3), 0x21f49 */
    g_cclogo = open_smk("AMWLOGO1");
    g_cclogo_t = 0;
    cc_init();
    while (!done) {
        gbl_input *in;
        int mx, my, fh = gtext_h(GF_DEFAULT);
        cc_frame(1);
        gbl_frame_text(GF_DEFAULT, 0, 110, "Current Config:", 7);
        gbl_frame_text(GF_DEFAULT, 80, 110, g_ccname, 7);
        gbl_frame_text(GF_DEFAULT, 0, 132, "SELECT INPUT DEVICES", 7);
        for (k = 0; k < ndrv; k++) {
            int on = base_k[k] >= 0 ? g_ccsel[base_k[k]] : sel_extra[k];
            gbl_frame_text(GF_DEFAULT, 0, 154 + 11 * k, cc_driver_title(drv[k]), on ? 0x10 : -1);
        }
        gbl_frame_text(GF_DEFAULT, 0, 400, "ACCEPT", -1);
        gbl_frame_text(GF_DEFAULT, 0, 422, "CUSTOM CONFIGURATION", -1);
        gbl_frame_text(GF_DEFAULT, 0, 444, "ABORT", -1);
        gtext(GF_DEFAULT, 320, 250, "~WHITE = Selectable Option", -1);
        gtext(GF_DEFAULT, 320, 272, "~RED = Selected Option", 0x10);
        gtext(GF_DEFAULT, 320, 294, "~GREY = Unselectable", 7);
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit || in->key == SDLK_ESCAPE) break;
        if (!in->lclick && !in->rclick) continue;
        mx = in->mx; my = in->my;
        if (mx < 0 || mx >= 100) continue;
        if (my >= 154 && my < 154 + 11 * (ndrv - 1) + fh && (my - 154) % 11 < fh) {   /* 0x20040: a device on / off */
            k = (my - 154) / 11;
            if (k < ndrv) {
                if (base_k[k] >= 0) { if (g_ccdev[base_k[k]].present || base_k[k] == 2) g_ccsel[base_k[k]] = !g_ccsel[base_k[k]]; }
                else sel_extra[k] = !sel_extra[k];
            }
        } else if (my >= 400 && my < 400 + fh) {   /* ACCEPT (0x20180) */
            if (cc_merge_defaults(&cfg, giddi_dir(), g_ccsel) == 0 && cc_write_inputmap(g_grd, g_gtx, g_headless, &cfg) == 0) {
                snprintf(g_ccname, sizeof g_ccname, "Default Config");
                done = 1;
            }
        } else if (my >= 422 && my < 422 + fh) {   /* CUSTOM CONFIGURATION (0x201b0): the editor page */
            cc_merge_defaults(&cfg, giddi_dir(), g_ccsel);
            snprintf(cfg.name, sizeof cfg.name, "%s", "Default Config");
            cc_editor(g_grd, g_gtx, g_headless, &cfg);
            g_in.lclick = g_in.rclick = g_in.ldown = g_in.rdown = g_in.lup = 0; g_in.key = 0;   /* its loop took the events */
            done = 1;
        } else if (my >= 444 && my < 444 + fh) done = 1;   /* ABORT (0x1f470) */
    }
    if (g_cclogo) smk_close(g_cclogo);
    g_cclogo = NULL;
    load_bg(prev_bg);
}

/* ======================================================================================================================
 * HALL OF HONOR (menu case 3, FUN_0002b910): background idx 2 (entry 1), amwlogo1 at (120, 4). The 20 MW2REG.CFG records
 * sorted with FUN_0002b8a0 (rank +0x10 desc, honor +0x14 desc, missions done +0xc desc - all 20, in use or not), the
 * first 8 of the order drawn when in use. Headers in font idx 0x1e (entry 29) at y 150: Pilot 0, Rank 200, Honor 325,
 * Kills 400, Hit % 460, Last Mission 520 (GBL has no Clan column). Rows from y 182 (0xb6), 16 apart, default font:
 * name (+0x28), rank title 0x7e790[+0x10], honor "%d", kills "%d" (+0x18), "%d%%" = +0x1c * 100 / +0x20 or "-" when
 * +0x20 is 0, last mission = the campaign title of (done - 1) or "----" when done is 0. Any key, left or right button. */
static int gbl_frame_hoh_cmp(const void *a, const void *b)
{
    const gbl_pilot *x = *(const gbl_pilot *const *)a, *y = *(const gbl_pilot *const *)b;
    if (x->rank != y->rank) return x->rank > y->rank ? -1 : 1;
    if (x->honor != y->honor) return x->honor > y->honor ? -1 : 1;
    if (x->done != y->done) return x->done > y->done ? -1 : 1;
    return x < y ? -1 : x > y;   /* ties: record order (Watcom's qsort order is not known) - ASSUMED */
}
static void gbl_hall_of_honor(void)
{
    static const char *const RANK[7] = {"Cadet", "Mechwarrior", "Star Commander", "Star Captain", "Star Colonel", "Nova Commander", "Galaxy Commander"};
    static const int CX[6] = {0, 200, 325, 400, 460, 520};
    static const char *const HD[6] = {"Pilot", "Rank", "Honor", "Kills", "Hit %", "Last Mission"};
    const gbl_pilot *list[20];
    gbl_frame_logo logo;
    int i;
    for (i = 0; i < 20; i++) list[i] = &g_pil[i];
    qsort(list, 20, sizeof list[0], gbl_frame_hoh_cmp);
    gbl_bg(2);
    gbl_frame_logo_open(&logo);
    for (;;) {
        gbl_input *in;
        int y = 182;
        draw_bg();
        for (i = 0; i < 6; i++) gbl_frame_text(29, CX[i], 150, HD[i], -1);
        for (i = 0; i < 8; i++) {
            const gbl_pilot *r = list[i];
            char v[48];
            if (!r->in_use) continue;
            gbl_frame_text(GF_DEFAULT, CX[0], y, r->display, -1);
            gbl_frame_text(GF_DEFAULT, CX[1], y, RANK[r->rank >= 0 && r->rank < 7 ? r->rank : 0], -1);
            snprintf(v, sizeof v, "%d", (int)r->honor); gbl_frame_text(GF_DEFAULT, CX[2], y, v, -1);
            snprintf(v, sizeof v, "%d", (int)r->kills); gbl_frame_text(GF_DEFAULT, CX[3], y, v, -1);
            if (r->shots) snprintf(v, sizeof v, "%d%%", (int)(r->hits * 100 / r->shots)); else snprintf(v, sizeof v, "-");
            gbl_frame_text(GF_DEFAULT, CX[4], y, v, -1);
            /* records 10-19 carry +8 = 1 (0x8017c[1] is not a mission list); the GBL list is used for every record - ASSUMED */
            gbl_frame_text(GF_DEFAULT, CX[5], y, r->done > 0 ? gbl_frame_mission_title(r->done - 1) : "----", -1);
            y += 16;
        }
        gbl_frame_logo_draw(&logo);
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (gbl_frame_any(in)) break;
    }
    gbl_frame_logo_close(&logo);
}

/* ======================================================================================================================
 * THE KESHIK (menu case 4, FUN_00022320): background idx 5 (entry 4) "THE KESHIK", amwlogo1 at (120, 4). The table
 * 0x7da3c (GBL_CREDITS above, 642 lines, NULL = blank): line k at y = top + 20 k, drawn when 0x68 < y <= 0x1cb, centred on
 * x 320; first character '<' font idx 0x1d (entry 28), '>' idx 0x1e (entry 29), '~' idx 0x1d with the colour map
 * {0 -> 0xff, 1 -> 0x10} (no '~' line in GBL's text), anything else the whole line in idx 0x22 (entry 33, the default).
 * GBL writes the role plain and the name with '>' (the base the other way round). Each pass the window (0, 125) 640x335
 * is filled black, the text drawn, then the bands (0, 105) 640x20 and (0, 460) 640x20 filled black (+0x3a2 = 0). top
 * starts at 0x1cc and drops 1 px a pass of the loop - CPU-paced in the original (no timer); pinned like the base port's
 * the_keshik() to 79 px/s, its period-machine measurement (ASSUMED the same for GBL: the loop is the base's). No date
 * easter egg in GBL. The list is not started again. Any key, left or right button ends it. */
static void gbl_the_keshik(void)
{
    gbl_frame_logo logo;
    Uint32 t0 = g_now;
    int k;
    gbl_bg(5);
    gbl_frame_logo_open(&logo);
    for (;;) {
        gbl_input *in;
        int top = 0x1cc - (int)((g_now - t0) * 79u / 1000u);
        draw_bg();
        gbl_frame_fill(0, 0x7d, 640, 0x14f);
        for (k = 0; k < 642; k++) {
            int y = top + 20 * k, fe, remap = -1;
            const char *t = GBL_CREDITS[k];
            if (y > 0x1cb) break;
            if (y <= 0x68 || !t) continue;
            if (t[0] == '<') { fe = 28; t++; }
            else if (t[0] == '>') { fe = 29; t++; }
            else if (t[0] == '~') { fe = 28; t++; remap = 0x10; }
            else fe = GF_DEFAULT;
            gbl_frame_text(fe, 320 - gtext_w(fe, t) / 2, y, t, remap);
        }
        gbl_frame_fill(0, 0x69, 640, 0x14);
        gbl_frame_fill(0, 0x1cc, 640, 0x14);
        gbl_frame_logo_draw(&logo);
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (gbl_frame_any(in)) break;
    }
    gbl_frame_logo_close(&logo);
}

/* ======================================================================================================================
 * the global menu FUN_00026f10 - called once a pass by every screen; it acts on the frame's input: Esc (the event
 * queue's 3) or the right button (FUN_0003b1e0) opens it, else it returns -1 at once. The screen as it is stays under
 * the panel (saved, FUN_000409c0 -> 0x95b80, and restored after each page and on leaving). Panel raw idx 7 (entry 6) at
 * (198, 116) 244x248; items 0x7f93c NEW MECHWARRIOR, COMBAT VARIABLES, COCKPIT CONTROLS, HALL OF HONOR, THE KESHIK,
 * FLEE TO DOS centred on x 320 at y 157 + 33 k - height / 2, font idx 0x21 (entry 32), the selected one idx 0x20 (entry
 * 31); hit boxes x 198..441, y 157 + 33 k +-16. Each time the menu is (re)drawn the selection starts at 0, then follows
 * the pointer when it moves onto an item. Keys: Enter picks; Up / Left / '8' / '4' the previous item, Down / Right / '2' /
 * '6' the next (wrapping; the digits are the keypad without NumLock, new in GBL). A left click picks the selected item
 * when the pointer is on it, otherwise closes the menu, as do Esc and the right button (-1). NEW MECHWARRIOR -> 8; the
 * pages return to the menu; FLEE TO DOS -> "Embrace cowardice?#Yes|No" (No by default), Yes -> -3, No -> the menu. The
 * clips of the screen under it stand still meanwhile. */
static int gbl_global_menu(void)
{
    static const char *const ITEM[6] = {"NEW MECHWARRIOR", "COMBAT VARIABLES", "COCKPIT CONTROLS", "HALL OF HONOR", "THE KESHIK", "FLEE TO DOS"};
    gbl_frame_screen sv;
    int result = -1, done = 0;
    Uint32 t_in = g_now;
    if (!(g_in.key == SDLK_ESCAPE || g_in.rclick)) return -1;
    if (g_in.quit) return -3;
    if (gbl_frame_save(&sv)) return -1;
    while (!done) {
        int sel = 0, lx = -1, ly = -1, choose = -1, i;
        while (choose < 0 && !done) {
            gbl_input *in;
            gbl_frame_restore(&sv);
            db_shp(6, 198, 116);   /* FUN_00041170(screen, shp, 0, 0xc6, 0x74, 0xf4, 0xf8) */
            gbl_text_cover(198, 116, 442, 364);
            for (i = 0; i < 6; i++) {
                int fe = i == sel ? 31 : 32;
                gbl_frame_text(fe, 320 - gtext_w(fe, ITEM[i]) / 2, 157 + 33 * i - gtext_h(32) / 2, ITEM[i], -1);
            }
            gbl_draw_cursor(g_in.mx, g_in.my);
            if (lx < 0) {   /* the first pass: the item under the pointer (0x270c9 with the remembered position -1) */
                for (i = 0; i < 6; i++) if (g_in.mx >= 198 && g_in.mx <= 441 && g_in.my >= 157 + 33 * i - 16 && g_in.my <= 157 + 33 * i + 16) { sel = i; break; }
                lx = g_in.mx; ly = g_in.my;
                continue;   /* redraw with it */
            }
            in = gbl_frame();
            if (in->quit) { result = -3; done = 1; break; }
            if (in->rclick) { done = 1; break; }
            if (in->mx != lx || in->my != ly) {
                for (i = 0; i < 6; i++) if (in->mx >= 198 && in->mx <= 441 && in->my >= 157 + 33 * i - 16 && in->my <= 157 + 33 * i + 16) { sel = i; break; }
                lx = in->mx; ly = in->my;
            }
            if (in->lclick) {
                if (in->mx >= 198 && in->mx <= 441 && in->my >= 157 + 33 * sel - 16 && in->my <= 157 + 33 * sel + 16) choose = sel;
                else done = 1;
                break;
            }
            switch (in->key) {
            case SDLK_ESCAPE: done = 1; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: choose = sel; break;
            case SDLK_UP: case SDLK_LEFT: case SDLK_8: case SDLK_4: case SDLK_KP_8: case SDLK_KP_4: sel = sel > 0 ? sel - 1 : 5; break;
            case SDLK_DOWN: case SDLK_RIGHT: case SDLK_2: case SDLK_6: case SDLK_KP_2: case SDLK_KP_6: sel = sel < 5 ? sel + 1 : 0; break;
            default: break;
            }
        }
        if (done) break;
        switch (choose) {
        case 0: result = 8; done = 1; break;
        case 1: gbl_combat_variables(); break;
        case 2: gbl_cockpit_controls(); break;
        case 3: gbl_hall_of_honor(); break;
        case 4: gbl_the_keshik(); break;
        case 5:   /* FLEE TO DOS: the dialog over the menu (its panel stays under it) */
            if (gbl_frame_dialog("Embrace cowardice?#Yes|No", 1) == 0) { result = -3; done = 1; }
            break;
        }
        if (g_quit_all) { result = -3; done = 1; }
        g_in.lclick = g_in.rclick = 0; g_in.key = 0;
    }
    gbl_frame_restore(&sv);
    free(sv.fb);
    g_in.lclick = g_in.rclick = 0; g_in.key = 0;
    gbl_frame_slots_resume(t_in);
    return result;
}

/* ======================================================================================================================
 * the title, state 8 (FUN_0002dda0). Background idx 1 (entry 0). Hotspots 0x7f46c (FUN_0002bce0, the default font):
 *   0 TRIALS OF GRIEVANCE (rect -1: never hit; label (320, 395); would be 7 / ctx 2)
 *   1 REGISTRATION (117,71)-(530,419), label (320, 318) -> 15, ctx 0
 *   2 JADE FALCON CLAN HALL (rect -1; label (124, 374); would be 15 / ctx 1)
 *   3 EXIT (0,420)-(639,479), label (320, 455) -> "Embrace cowardice?#Yes|No" (FUN_00027a40, default No), Yes -> -3
 * actions on the left button; the label of the hotspot under the pointer drawn centred ('~'). FUN_0002bce0 is passed the
 * default font (0x95ea0) but ignores it: the object uses 0x95eac = idx 0x1e (entry 29) for every label (0x2bcf3; DOSBox
 * A_t_hover_reg.png shows entry 29's thin wide capitals, not entry 33).
 * Clips (FUN_0003d1a0(slot, name, x, y, flags, 0) - 10 frames a second): slot 1 aintro01 (118, 2) flags 2 and slot 3
 * aintro03 (99, 78) flags 6 on entry; once slot 1 has ended it gets 0x20 + 1 and slot 2 aintro02 (118, 2) flags 10 (loop)
 * starts (0x2df8a); once slot 3 has ended it gets 0x20 + 1 and slot 4 aintro04 (122, 256) flags 6 starts (0x2dfe5).
 * (0x20 stops the slot's drawing, but the DOS screen keeps its last pixels - the shell does not repaint the background
 * each pass, A_t2.png shows the emblem; the port repaints every frame, so the ended slot is only paused here and its
 * last frame - flags 4 - keeps showing.) Sounds: idx 0x51 (entry 80) looping, its volume ramped 0 -> 30 one step per
 * 500 passes (FUN_0003dc50(snd, 500, 1000, 0, 0x1e) / FUN_0003dcb0 per pass - CPU-paced; here one step every 250 ms,
 * ASSUMED); idx 0x7e (entry 125) at 0x32 once. The pointer starts at (546, 29) (FUN_0003b170). No hotspot acted: the
 * global menu's result (8 / -3; -1 stays). */
static int gbl_title(void)
{
    static const gbl_hot hs[4] = {
        {-1, -1, -1, -1, 320, 395, "~TRIALS OF GRIEVANCE"}, {117, 71, 530, 419, 320, 318, "~REGISTRATION"},
        {-1, -1, -1, -1, 124, 374, "~JADE FALCON CLAN HALL"}, {0, 420, 639, 479, 320, 455, "~EXIT"}};
    int logo2 = 0, plate = 0, next = -1;
    Uint32 t0 = g_now;
    gsound *amb;
    gbl_bg(1);
    gs_load(1, "aintro01", 118, 2, GS_RESTORE, 0);
    gs_load(3, "aintro03", 99, 78, GS_RESTORE | GS_HOLD, 0);
    gsnd_play(0x51, 0, 1);
    amb = gsnd_get(0x51, 0);
    gsnd_play(0x7e, 0x32, 0);
    g_in.mx = 0x222; g_in.my = 0x1d;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (g_win && g_grd && SDL_GetMouseFocus() == g_win) {   /* windowed: the system pointer, moved to the same place */
        int wx, wy;
        SDL_RenderLogicalToWindow(g_grd, 0x222, 0x1d, &wx, &wy);
        SDL_WarpMouseInWindow(g_win, wx, wy);
    }
#endif
    while (next == -1) {
        gbl_input *in;
        int h, vol = (int)((g_now - t0) / 250u);
        if (amb && g_sfx) sfx_pcm_level(g_sfx, amb->pcm, (float)(vol > 30 ? 30 : vol) / 127.0f, 0.0f);
        draw_bg();
        gs_update();
        if (!logo2 && gs_done(1)) {
            gs_flags(1, GS_PAUSED, 1);
            gs_load(2, "aintro02", 118, 2, GS_RESTORE | GS_LOOP, 0);
            logo2 = 1;
        }
        if (!plate && gs_done(3)) {
            gs_flags(3, GS_PAUSED, 1);
            gs_load(4, "aintro04", 122, 256, GS_RESTORE | GS_HOLD, 0);
            plate = 1;
        }
        h = gh_hit(hs, 4, NULL, g_in.mx, g_in.my);
        gh_labels(hs, 4, NULL, h, 29, -1, -1);   /* the hotspot object's own font: 0x95eac = idx 0x1e (entry 29), 0x2bcf3 */
        gbl_draw_cursor(g_in.mx, g_in.my);
        in = gbl_frame();
        if (in->quit) return -3;
        h = gh_hit(hs, 4, NULL, in->mx, in->my);
        if (in->lclick && h == 1) { g_ctx = 0; next = 15; }
        else if (in->lclick && h == 3) {
            Uint32 t1 = g_now;
            if (gbl_frame_dialog("Embrace cowardice?#Yes|No", 1) == 0) next = -3;
            gbl_frame_slots_resume(t1);
        }
        if (next == -1) next = gbl_global_menu();
    }
    gsnd_stop(0x51);   /* both sounds freed on leaving (FUN_0003dc10) */
    gsnd_stop(0x7e);
    return next;
}
