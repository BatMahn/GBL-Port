/*
 * cockpitcfg.h - the shell's Cockpit Controls data (GBL MW2SHELL FUN_00021f40 = the base FUN_00021020, and its GIDDI drivers):
 *
 *   The drivers: every GIDDI\*.DLL that loads (FUN_0001a530, at most 16, in directory order), numbered 0.. in that
 *   order; a driver is known by its name (the DLL's base name: avenger, fltstck, joystick, keyboard, mouse, msjstick,
 *   tmaster, vfx1, vio1, vio2, vpilot, wingman on the GBL disc). A binding's device is that number.
 *
 *   GIDDI\*.CPC (3872 bytes): char name[64]; 16 x {i32 device number, char name[12]} - the drivers of the shell that
 *   wrote it (-1 / "" = none); then 4 levels ("Page One" .. "Page Four") x 37 control slots x {i32 type (0 axis, 1
 *   button, 2 two buttons), i32 device (-1 none), u32 flags (bit 0 Ctrl, 1 Alt, 2 Shift, 31 reverse; bits 4-6 the NOT
 *   modifiers while the map is written), i32 second flags (the same, for a two-button control's second input), i32 a,
 *   i32 b (the inputs, -1 none)}. Loading one renumbers its devices by name to the drivers present (FUN_00020fc0);
 *   saving writes the present drivers' table. <driver>.CPC are the drivers' defaults, CONFIG00.CPC the current
 *   configuration (read when the page opens, FUN_000213e0(0); written by ACCEPT), CONFIG01-04.CPC the four customs.
 *   The 31 controls (shell table 0x7b758 labels, 0x7b7ec map names): throttle, legs_pan_delta, torso_pan, torso_tilt,
 *   pilot_pan, pilot_tilt, zoom_factor (the 7 directional ones), torso_tilt_reset, glance_left .. advance_nav.
 *   An input is the driver's index: keyboard = DOS scan code - 1 (names from GIDDI\KEYBOARD.DLL), others per driver.
 *
 *   INPUT.MAP, as the shell writes it (FUN_00020b40 / FUN_000205e0; checked byte for byte against DOSBox runs of the
 *   original shell for each GBL driver): "# mw2shell CockPit Config generated map file", then every binding, level by
 *   level, slot by slot: an axis writes "<name>" with "+ dev input" ("-" when reversed); a button "<name>"; a
 *   two-button control <base>_minus / _plus (a then b; reversed: a is _plus), <base> = the name less "_delta". After each,
 *   the aliases (same binding): jumpjet_fire_* -> jumpjet_enabled, pilot_pan -> eyepoint_pan_delta, pilot_tilt ->
 *   track_height_delta, zoom_factor -> track_distance_delta (reversed), torso_tilt_reset -> pilot_tilt_reset,
 *   torso_pan_reset, pilot_pan_reset, glance_left / right -> eyepoint_pan_minus / plus, glance_up / down ->
 *   track_height_minus / plus. A key used elsewhere with a modifier gets "- keyboard <modifier>" where it is used
 *   without. Then the jump-turn keys GreyDelete / GreyPageDown, and the counts (at most 40 analog, 90 discrete).
 *
 *   Port addition: the "gamepad" driver (an SDL game controller), listed after the GIDDI drivers, with built-in
 *   defaults; its bindings use the same file formats (INPUT.MAP "+ gamepad LeftX", the CPC's device table).
 */
#ifndef MW2_COCKPITCFG_H
#define MW2_COCKPITCFG_H

#include <stdint.h>
#include <stddef.h>

#define CC_LEVELS   4
#define CC_SLOTS    37
#define CC_CONTROLS 31
#define CC_MAXDEV   16

typedef struct { int32_t type, device; uint32_t flags; int32_t pad, a, b; } cc_binding;
typedef struct {
    char       name[64];
    uint8_t    table[16][16];   /* the device table as the last file loaded into it had it (the shell keeps one, 0x8ec28,
                                 * and rewrites only the parts below when it saves) */
    cc_binding b[CC_LEVELS][CC_SLOTS];
} cc_config;

typedef struct { char map[24], label[40]; } cc_input;
typedef struct {
    char     shortname[12], title[48];
    int      port;                    /* the port's own driver (the gamepad), not a GIDDI file */
    cc_input axis[8];  int naxis;
    cc_input button[128]; int nbutton;
} cc_device;
typedef struct { cc_device d[CC_MAXDEV]; int n; } cc_drivers;

extern const char *const CC_LABEL[CC_CONTROLS];      /* "Throttle ", "Chassis ", ... (0x7b758) */
extern const char *const CC_MAPNAME[CC_CONTROLS];    /* "throttle", "legs_pan_delta", ... (0x7b7ec) */
#define CC_GAMEPAD "gamepad"

/* the drivers in the GIDDI directory dir (FUN_0001a530: its *.DLL files - the ones this port knows, see
 * cc_driver_title - in name order (ASSUMED: the directory order DOS gives, alphabetical on the disc and in the install);
 * gamepad: also the port's gamepad driver, last. Returns the number. */
int  cc_drivers_load(cc_drivers *dv, const char *dir, int gamepad);
int  cc_find(const cc_drivers *dv, const char *shortname);   /* the driver's number, -1 if absent */

void cc_clear(cc_config *c);                                  /* FUN_000210d0: no bindings */
int  cc_load(cc_config *c, const char *path, const cc_drivers *dv);
int  cc_save(const cc_config *c, const char *path, const cc_drivers *dv);
/* the drivers a configuration selects (FUN_00020ee0): the keyboard and the drivers its bindings use, at most 4 in
 * driver order; sel[CC_MAXDEV] */
void cc_select_from(const cc_config *c, const cc_drivers *dv, int sel[CC_MAXDEV]);
/* the selected drivers' defaults merged into one configuration (FUN_00021b30, ACCEPT / CUSTOM CONFIGURATION / RESET
 * DEFAULTS): cleared, named "Default Config"; then for each selected driver in driver order its GIDDI\<name>.CPC (the
 * gamepad: its built-in defaults), of which only that driver's bindings are taken: each to the first free level of
 * its slot (from Page One), unless an identical binding is met first; with no free level it is dropped */
int  cc_merge_defaults(cc_config *out, const char *dir, const cc_drivers *dv, const int sel[CC_MAXDEV]);
/* INPUT.MAP text for the configuration (every binding); returns the length written, 0 if the buffer is too small or
 * the counts are past the sim's limits ("Error: Sim can not|handle that many controls"). The configuration's NOT
 * modifier bits and second flags are cleared afterwards, as the shell leaves them (they are saved so). */
size_t cc_generate(cc_config *c, const cc_drivers *dv, char *out, size_t cap);
/* the text the editor shows for a binding: "key Equal/Minus", "joystick Left/Right", "----" */
void cc_describe(const cc_binding *b, const cc_drivers *dv, char *out, size_t cap);
/* the title of a GIDDI driver by its short name (the DLL's base name, any case), as each DLL reports it; NULL for an
 * unknown driver */
const char *cc_driver_title(const char *shortname);

#endif
