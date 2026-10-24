/* Ghost Bear's Legacy's configuration: one per-user file,
 *   Linux   $XDG_CONFIG_HOME/gblport/gblport.cfg  (~/.config/gblport/gblport.cfg)
 *   macOS   ~/Library/Application Support/gblport/gblport.cfg
 *   Windows %APPDATA%\gblport\gblport.cfg
 * plain "key=value" lines:
 *   game=<folder>          the game folder: install/ (settings, pilots, saves), cd/ (the disc's files) or an .iso,
 *                          music/ (CD tracks), titanium/ (the Titanium Edition's 16-bit files, for the TITANIUM look)
 *   install= cd= music= models=   optional overrides of the folder's parts (models: the DOS GBL MW2.PRJ, found in
 *                          install/ or the disc's GBL\ when not given; a textures= line is ignored)
 *   renderer=dos | enhanced | titanium   gbl's three looks (see PORTCFG_GBL_* below; DOS the default)
 *   titanium=<folder>      the Titanium edition's 16-bit files (MW2.PRJ, SKYGND.PAR, particle.PID from the disc's
 *                          Install/16bit); default: the game folder's titanium/
 *   resolution=desktop | window | <w>x<h>     (desktop: native fullscreen, the default)
 *   aa=0 | 2 | 4 | 8
 *   render=native | 320x200 | 640x480 | 1024x768   (the sim's picture: the display's resolution, or one of the
 *                          original's RESOLUTION choices, drawn at that size and scaled up in blocks to 4:3)
 *   hud_layout=wide | original   (wide, the default: on a window wider than 4:3 the HUD's left-hand instruments - radar,
 *                          altitude tape, target display - sit at the window's left edge and the right-hand ones - weapon
 *                          list, damage display / viewport, throttle bar, speed - at its right edge, each as far from
 *                          the edge as from the 4:3 area's edge; the compass, reticle, markers, bars and messages stay
 *                          centred. original: the whole HUD in the 4:3 middle, as the original drew it)
 *   dos_shading=dithered | smooth   (the DOS renderer's Gouraud shading - the shaded polygons (cockpit, mechs, buildings)
 *                          and the sky's haze band: dithered, the default, as the original drew them - each palette step a
 *                          flat band and a 1-pixel checkerboard of it and the next (MW2.EXE 0x59e77; the haze band in the
 *                          sky palette's 15 colours 0xE0-0xEE, 0x3c000); smooth: the same colours blended into an even
 *                          gradient. dos_sky=banded | smooth, its older name, is still read)
 *   videos=original | smooth | scanlines | crt | crt_curved   (the shell's full-screen videos, 320x200 clips: original, the
 *                          default, as DOS showed them - mode 13h, each pixel a solid block filling the 4:3 screen;
 *                          smooth: filtered scaling; scanlines: dark gaps between the lines (the interlaced look); crt:
 *                          scanlines, a soft glow and an aperture-grille mask; crt_curved: the same on a curved tube.
 *                          Combat Variables' VIDEO, src/vidfx.h)
 *   video_fit=4:3 | fill | stretch   (the full-screen videos on a wider screen: 4:3, the default - the 4:3 picture as large
 *                          as the screen allows; fill - the whole screen, the 4:3 picture scaled to the screen's width and cut at
 *                          top and bottom; stretch - the whole picture widened to the screen. Combat Variables' FMV size)
 *   system_shortcuts=block | allow   (macOS: block, the default, turns the system's keyboard shortcuts - Mission
 *                          Control's Ctrl+arrows, Cmd+Tab, Spotlight - off while a mission is played with the window in
 *                          front (not in the MAIN MENU or paused); allow leaves them on. Other systems: no effect)
 * The game folder is found on first run (the command line, the current folder, next to the executable - in a macOS
 * bundle: next to the .app -, one level up, ~/GBL-game) and remembered. */
#ifndef PORTCFG_H
#define PORTCFG_H
#include <stddef.h>
#define PORTCFG_PATH 1024
typedef struct {
    int mode;        /* 0 desktop fullscreen, 1 windowed, 2 exclusive fullscreen at w x h */
    int w, h;
    int aa;
    int render_w, render_h;   /* 0 = native */
    float speed;              /* game speed: 1.0 = real time (speed= in the file; default 0.85) */
    char game[PORTCFG_PATH], install[PORTCFG_PATH], cd[PORTCFG_PATH], music[PORTCFG_PATH];
    char models[PORTCFG_PATH], textures[PORTCFG_PATH];   /* the DOS GBL MW2.PRJ (portcfg_resolve) */
    char titanium[PORTCFG_PATH];     /* the Titanium edition's folder (MW2.PRJ + SKYGND.PAR of its 16-bit build) */
    int  renderer;            /* PORTCFG_ED_* and texset PORTCFG_TX_*: together gbl's look (portcfg_gbl_look) */
    int  texset;
    int  system_shortcuts;    /* 0 block (default): the system's shortcuts off while playing (macOS); 1 allow */
    int  hud_original;        /* hud_layout=: 0 wide (default), 1 original (the 4:3 layout centred) */
    int  dos_sky_smooth;      /* dos_shading= (dos_sky=): 0 dithered (default, the original), 1 smooth (the DOS renderer's
                               * Gouraud polygons and haze band blended) */
    int  videos;              /* videos=: PORTCFG_VID_* (0 original, the default) */
    int  video_fit;           /* video_fit=: PORTCFG_FIT_* (0 4:3, the default) */
} portcfg;
/* how a look is drawn, as the simulation is told (MW2_EDITION / MW2_TEXTURES): ENHANCED = the GL renderer in true colour
 * (ENHANCED with the DOS archive, or TITANIUM with the Titanium textures), DOS = the DOS renderer */
enum { PORTCFG_ED_ENHANCED, PORTCFG_ED_DOS, PORTCFG_ED_COUNT };
extern const char *const PORTCFG_ED_NAME[PORTCFG_ED_COUNT];   /* "enhanced", "dos" */
enum { PORTCFG_TX_ORIGINAL, PORTCFG_TX_TITANIUM, PORTCFG_TX_COUNT };
extern const char *const PORTCFG_TX_NAME[PORTCFG_TX_COUNT];   /* "original", "titanium" */
/* the full-screen videos' look (the order of vidfx.h's VIDFX_*) */
enum { PORTCFG_VID_ORIGINAL, PORTCFG_VID_SMOOTH, PORTCFG_VID_SCANLINES, PORTCFG_VID_CRT, PORTCFG_VID_CRT_CURVED, PORTCFG_VID_COUNT };
extern const char *const PORTCFG_VID_NAME[PORTCFG_VID_COUNT];   /* "original", "smooth", "scanlines", "crt", "crt_curved" */
/* how the full-screen videos fill a screen wider than 4:3 */
enum { PORTCFG_FIT_43, PORTCFG_FIT_FILL, PORTCFG_FIT_STRETCH, PORTCFG_FIT_COUNT };
extern const char *const PORTCFG_FIT_NAME[PORTCFG_FIT_COUNT];   /* "4:3", "fill", "stretch" */
/* A renderer's files: the archive for models and data (always the DOS GBL MW2.PRJ), the archive for textures and its
 * kind ("dos"), the sky / ground file ("") and the fog file (ENHANCED: the Titanium SKYGND.PAR when present, else "").
 * Returns 0 if the DOS archive is there (the install's, else the disc's GBL\). */
int  portcfg_edition(const portcfg *c, int ed, char *models, char *textures, char *kind, char *sky, char *fog);
/* The Titanium edition's files: its archive and its SKYGND.PAR (either may be NULL). Returns 0 if the archive is there. */
int  portcfg_titanium(const portcfg *c, char *prj, char *sky);
/* Can this texture set go with this renderer? ORIGINAL always; TITANIUM not with the DOS renderer and only with the
 * Titanium folder there. */
int  portcfg_texset_ok(const portcfg *c, int renderer, int texset);
/* A renderer + texture set's files (portcfg_edition, then with TITANIUM: the Titanium archive as the textures archive,
 * kind "titanium", its SKYGND.PAR as the sky file, no fog file). Returns 0 if both are available. */
int  portcfg_look(const portcfg *c, int renderer, int texset, char *models, char *textures, char *kind, char *sky, char *fog);
void portcfg_defaults(portcfg *c);
/* gbl's three looks, the file's renderer=dos | enhanced | titanium (DOS the default, and the fallback when titanium/ is
 * missing):
 *   dos       the DOS software look (renderer DOS)
 *   enhanced  GBL's own DOS MW2.PRJ models and 8-bit textures in true colour, filtered, with each mission's fog from the
 *             Titanium SKYGND.PAR (renderer ENHANCED, textures ORIGINAL; the sim's kind "dos", MW2_EDITION=enhanced; the
 *             fog file = titanium/SKYGND.PAR when present, else no fog)
 *   titanium  the Titanium Edition's Direct3D look: titanium/MW2.PRJ's models and textures, its SKYGND.PAR and
 *             particle.PID (renderer ENHANCED, textures TITANIUM; the sim's kind "titanium", the textures archive
 *             titanium/MW2.PRJ)
 * The data archive (mission records, MEKs, AI - the models archive argument) is always the DOS GBL MW2.PRJ (the standing
 * rule: Titanium contributes looks only). */
enum { PORTCFG_GBL_DOS, PORTCFG_GBL_ENHANCED, PORTCFG_GBL_TITANIUM, PORTCFG_GBL_COUNT };
extern const char *const PORTCFG_GBL_NAME[PORTCFG_GBL_COUNT];   /* "dos", "enhanced", "titanium" */
int  portcfg_gbl_look(const portcfg *c);                     /* PORTCFG_GBL_* of renderer + texset */
void portcfg_gbl_set_look(portcfg *c, int look);
int  portcfg_gbl_look_ok(const portcfg *c, int look);        /* 1 if its files are here (titanium: titanium/MW2.PRJ) */
int  portcfg_gbl_next(const portcfg *c, int look);           /* the next available look after `look` (cycling) */
/* The program the configuration belongs to: config folder and file name, the game folder's default name, the disc's
 * data folder. Default (and gbl's): "gblport", "gblport.cfg", "GBL-game", "GBL". */
void portcfg_set_app(const char *cfgdir, const char *cfgfile, const char *game_folder, const char *cd_data);
const char *portcfg_cd_data(void);      /* "GBL" */
const char *portcfg_game_folder(void);  /* "GBL-game" */
const char *portcfg_file(void);                       /* the per-user file's path */
int  portcfg_load(const char *unused, portcfg *c);    /* 0 if read; defaults otherwise (the argument is ignored) */
int  portcfg_save(const char *unused, const portcfg *c);
/* Find the game folder (if not configured) and fill in every part left empty. exe_dir: the executable's folder;
 * hint: a folder from the command line, or NULL. Returns 0 if an install folder was found. */
int  portcfg_resolve(portcfg *c, const char *exe_dir, const char *hint);
/* Is this a game folder (or a plain DOS install)? */
int  portcfg_is_game_dir(const char *dir);
/* A chosen folder: the game folder itself, or a folder holding GBL-game/. Returns 1 with the game folder's absolute
 * path in out, 0 if neither. */
int  portcfg_game_in(const char *dir, char *out, size_t n);
/* exe_dir inside a macOS bundle (".../X.app/Contents/..."): 1 with the folder holding X.app in out, else 0 */
int  portcfg_app_folder(const char *exe_dir, char *out, size_t n);
/* exe_dir is a macOS App Translocation copy (a quarantined download run in place: a random read-only path) */
int  portcfg_translocated(const char *exe_dir);
#endif
