/* cockpitcfg.c - see cockpitcfg.h */
#include "cockpitcfg.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const CC_LABEL[CC_CONTROLS] = {
    "Throttle ", "Chassis ", "Turret Turn", "Turret Tilt", "Eye Point ", "Eye Point ", "Eye Point ", "Recenter Torso",
    "Glance Left", "Glance Right", "Glance Up", "Glance Down", "Jump Jets On", "Jump Jets Front", "Jump Jets Back",
    "Jump Jets Left", "Jump Jets Right", "Fire Weapon", "Cycle Weapon", "Cycle Group", "Fire Group 1", "Fire Group 2",
    "Fire Group 3", "Group Toggle", "Target Next", "Target Prev", "Target Reticle", "Target Friendly", "Nearest Enemy",
    "Inspect Target", "Next Nav Point"};
const char *const CC_MAPNAME[CC_CONTROLS] = {
    "throttle", "legs_pan_delta", "torso_pan", "torso_tilt", "pilot_pan", "pilot_tilt", "zoom_factor", "torso_tilt_reset",
    "glance_left", "glance_right", "glance_up", "glance_down", "jumpjet_enabled", "jumpjet_fire_forward",
    "jumpjet_fire_backward", "jumpjet_fire_left", "jumpjet_fire_right", "weapon_fire", "weapon_cycle", "weapon_cycle_group",
    "weapon_fire_group_1", "weapon_fire_group_2", "weapon_fire_group_3", "toggle_group_fire", "advance_target",
    "previous_target", "target_reticle", "target_friendly", "nearest_enemy", "inspect_target", "advance_nav"};

static int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* fopen for reading with the file name in any case (the GIDDI files keep the disc's upper case, a copy may not; a
 * case-sensitive disk): the path as given, else its last component matched in its folder ignoring case */
static FILE *ci_fopen(const char *path)
{
    FILE *f = fopen(path, "rb");
#ifndef _WIN32
    if (!f) {
        char dir[1100];
        const char *sl = strrchr(path, '/');
        DIR *d;
        struct dirent *e;
        if (!sl || (size_t)(sl - path) >= sizeof dir) return NULL;
        memcpy(dir, path, (size_t)(sl - path)); dir[sl - path] = 0;
        if (!(d = opendir(dir[0] ? dir : "/"))) return NULL;
        while (!f && (e = readdir(d)))
            if (strcasecmp(e->d_name, sl + 1) == 0) { char q[1400]; snprintf(q, sizeof q, "%s/%s", dir, e->d_name); f = fopen(q, "rb"); }
        closedir(d);
    }
#endif
    return f;
}

/* the path of a file in dir by its DOS name, as stored (upper case on the disc / install) or in lower case; 0 if found */
static int dos_path(const char *dir, const char *name, char *p, size_t cap)
{
    char up[64], lo[64];
    size_t i;
    FILE *f;
    for (i = 0; name[i] && i + 1 < sizeof up; i++) { up[i] = (char)toupper((unsigned char)name[i]); lo[i] = (char)tolower((unsigned char)name[i]); }
    up[i] = lo[i] = 0;
    snprintf(p, cap, "%s/%s", dir, up);
    if ((f = fopen(p, "rb"))) { fclose(f); return 0; }
    snprintf(p, cap, "%s/%s", dir, lo);
    if ((f = fopen(p, "rb"))) { fclose(f); return 0; }
    snprintf(p, cap, "%s/%s", dir, name);
    if ((f = fopen(p, "rb"))) { fclose(f); return 0; }
#ifndef _WIN32
    {   /* any other case of the name (a copy made on a case-preserving disk) */
        DIR *d = opendir(dir);
        struct dirent *e;
        int ok = -1;
        while (d && ok < 0 && (e = readdir(d)))
            if (strcasecmp(e->d_name, name) == 0) { snprintf(p, cap, "%s/%s", dir, e->d_name); ok = 0; }
        if (d) closedir(d);
        if (ok == 0) return 0;
    }
#endif
    return -1;
}

/* ---- the drivers ---------------------------------------------------------------------------------------------------- */

static void add(cc_input *v, int *n, int max, const char *map, const char *label)
{
    if (*n >= max) return;
    snprintf(v[*n].map, sizeof v[*n].map, "%s", map);
    snprintf(v[*n].label, sizeof v[*n].label, "%s", label);
    (*n)++;
}

/* the keyboard's inputs from KEYBOARD.DLL: its list of map names, then the shown names, by scan code - 1 */
static void keyboard_from_dll(cc_device *d, const char *dir)
{
    char path[1100];
    FILE *f = dos_path(dir, "KEYBOARD.DLL", path, sizeof path) == 0 ? fopen(path, "rb") : NULL;
    uint8_t *b = NULL;
    long n = 0, i, start = -1;
    char maps[128][24];
    int nm = 0, nl = 0;
    if (!f) return;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && (b = malloc((size_t)n + 1)) && fread(b, 1, (size_t)n, f) == (size_t)n) {
        b[n] = 0;
        for (i = 0; i + 10 < n; i++) if (memcmp(b + i, "Keyboard\0", 9) == 0) { start = i + 9; break; }
        if (start >= 0) {
            long p = start;
            int phase = 0;
            while (p < n && nl < 128) {
                const char *s = (const char *)b + p;
                size_t len = strlen(s);
                if (len == 0) { p++; if (phase == 0 && nm == 0) continue; if (phase == 1) break; continue; }
                if (phase == 0 && strcmp(s, "Escape Key") == 0) phase = 1;
                if (phase == 0) { if (nm < 128) snprintf(maps[nm++], 24, "%s", s[0] == '*' ? s + 1 : s); }
                else { if (nl < nm) add(d->button, &d->nbutton, 128, maps[nl], s); nl++; if (nl >= nm) break; }
                p += (long)len + 1;
            }
        }
    }
    free(b);
    fclose(f);
}

/* each driver's inputs as its DLL lists them: the axes (shown name, map name), then the buttons. From the drivers'
 * string tables (GIDDI\*.DLL); the order is the index a binding stores (checked against the original shell's
 * GIDDI\CONFIG00.CPC / INPUT.MAP for AVENGER, VFX1, VPILOT, WINGMAN, the base drivers as before) */
typedef struct { const char *name, *title; const char *const *axes, *const *buttons; } drv_info;
#define S(...) (const char *const[]){__VA_ARGS__, NULL}
static const drv_info DRV[] = {
    {"avenger", "Spaceball Avenger",
     S("Pull_Forward_&_hold_C", "Tz&C", "Up/Down", "Ty", "Left/Right", "Tx", "Pitch", "Rx", "Yaw", "Ry", "Pitch_&_hold_D", "Rx&D",
       "Yaw_&_hold_D", "Ry&D"),
     S("Button A", "ButtonA", "Button B", "ButtonB", "Button C", "ButtonC", "Button D", "ButtonD", "Button E", "ButtonE",
       "Button F", "ButtonF", "Turn Left", "TurnLeft", "Turn Right", "TurnRight", "Jump Jet", "JumpJet", "Look Up", "LookUp",
       "Look Down", "LookDown")},
    {"fltstck", "Flightstick PRO",
     S("Joystick Up/Down Movement", "Up/Down", "Joystick Left/Right Movement", "Left/Right", "Rudder Pedals", "Rudder", "Throttle Dial", "Throttle"),
     S("Trigger", "Trigger", "Left Thumb Switch", "LeftBtn", "Right Thumb Switch", "RightBtn", "Middle Thumb Switch", "MiddleBtn",
       "Hat Up", "HatUp", "Hat Right", "HatRight", "Hat Down", "HatDown", "Hat Left", "HatLeft")},
    {"joystick", "Two Button Joystick",
     S("Joystick Up/Down Movement", "Up/Down", "Joystick Left/Right Movement", "Left/Right"),
     S("Button 1", "Button1", "Button 2", "Button2")},
    {"keyboard", "Keyboard", NULL, NULL},
    {"mouse", "Mouse",
     S("Mouse Up/Down Movement", "Up/Down", "Mouse Left/Right Movement", "Left/Right"),
     S("Left button", "LeftBtn", "Middle button", "MiddleBtn", "Right button", "RightBtn")},
    {"msjstick", "MS SideWinder (CH Pro Mode)",
     S("Joystick Up/Down Movement", "Up/Down", "Joystick Left/Right Movement", "Left/Right", "Joystick Twist", "Twist", "Throttle Slider", "Throttle"),
     S("Trigger", "Trigger", "Rear Switch", "RearBtn", "Top Thumb Switch", "TopBtn", "Bottom Thumb Switch", "BottomBtn",
       "Hat Up", "HatUp", "Hat Right", "HatRight", "Hat Down", "HatDown", "Hat Left", "HatLeft")},
    {"tmaster", "ThrustMaster Mark II",
     S("Joystick Up/Down Movement", "Up/Down", "Joystick Left/Right Movement", "Left/Right", "Rudder Pedals", "Rudder"),
     S("Trigger", "Trigger", "Top Switch", "TopBtn", "Middle Switch", "MiddleBtn", "Bottom Switch", "BottomBtn",
       "Hat Up", "HatUp", "Hat Right", "HatRight", "Hat Down", "HatDown", "Hat Left", "HatLeft")},
    {"vfx1", "VFX1 HEADGEAR V1.2",
     S("VFX1 Up/Down", "Up/Down", "VFX1 Left/Right", "Left/Right", "VFX1 Left/Right Roll", "Left/Right_Roll",
       "Puck Left/Right", "Puck_Left/Right", "Puck Up/Down", "Puck_Up/Down"),
     S("Puck Top button", "Puck_TopBtn", "Puck Middle button", "Puck_MiddleBtn", "Puck Bottom button", "Puck_BottomBtn")},
    {"vio1", "Virtual i/O i-glasses! on COM1:", S("Head Tilt (look Up/Down)", "Tilt", "Head Pan (look Left/Right)", "Pan"), NULL},
    {"vio2", "Virtual i/O i-glasses! on COM2:", S("Head Tilt (look Up/Down)", "Tilt", "Head Pan (look Left/Right)", "Pan"), NULL},
    {"vpilot", "CH Virtual Pilot PRO",
     S("Yoke In/Out Movement", "In/Out", "Yoke Left/Right Movement", "Left/Right", "Rudder Pedals", "Rudder", "Throttle", "Throttle"),
     S("Button 1", "Button1", "Button 2", "Button2", "Button 3", "Button3", "Button 4", "Button4", "Button 5", "Button5",
       "Button 6", "Button6", "Left Hat Up", "LHatUp", "Left Hat Right", "LHatRight", "Left Hat Down", "LHatDown",
       "Left Hat Left", "LHatLeft", "Right Hat Up", "RHatUp", "Right Hat Right", "RHatRight", "Right Hat Down", "RHatDown",
       "Right Hat Left", "RHatLeft")},
    {"wingman", "Wingman Extreme",
     S("Joystick Up/Down Movement", "Up/Down", "Joystick Left/Right Movement", "Left/Right", "Rudder Pedals", "Rudder"),
     S("Trigger", "Trigger", "Button 1", "Button1", "Button 2", "Button2", "Button 3", "Button3",
       "Hat Up", "HatUp", "Hat Right", "HatRight", "Hat Down", "HatDown", "Hat Left", "HatLeft")},
    /* port addition: an SDL game controller (standard layout; the face buttons by position, Xbox names) */
    {CC_GAMEPAD, "Gamepad (port addition)",
     S("Left Stick Left/Right", "LeftX", "Left Stick Up/Down", "LeftY", "Right Stick Left/Right", "RightX",
       "Right Stick Up/Down", "RightY", "Left Trigger", "LeftTrigger", "Right Trigger", "RightTrigger"),
     S("A (bottom)", "A", "B (right)", "B", "X (left)", "X", "Y (top)", "Y", "Back / Select", "Back", "Guide / Home", "Guide",
       "Start", "Start", "Left Stick Press", "LeftStick", "Right Stick Press", "RightStick", "Left Shoulder", "LeftShoulder",
       "Right Shoulder", "RightShoulder", "D-Pad Up", "DPadUp", "D-Pad Down", "DPadDown", "D-Pad Left", "DPadLeft",
       "D-Pad Right", "DPadRight", "Left Trigger", "LT", "Right Trigger", "RT", "Left Stick Up", "LeftStickUp",
       "Left Stick Down", "LeftStickDown", "Left Stick Left", "LeftStickLeft", "Left Stick Right", "LeftStickRight",
       "Right Stick Up", "RightStickUp", "Right Stick Down", "RightStickDown", "Right Stick Left", "RightStickLeft",
       "Right Stick Right", "RightStickRight")},
};
#undef S

static const drv_info *drv_info_of(const char *shortname)
{
    size_t i;
    char lo[16];
    for (i = 0; shortname[i] && i + 1 < sizeof lo; i++) lo[i] = (char)tolower((unsigned char)shortname[i]);
    lo[i] = 0;
    for (i = 0; i < sizeof DRV / sizeof DRV[0]; i++) if (strcmp(lo, DRV[i].name) == 0) return &DRV[i];
    return NULL;
}

const char *cc_driver_title(const char *shortname)
{
    const drv_info *d = shortname ? drv_info_of(shortname) : NULL;
    return d && strcmp(d->name, CC_GAMEPAD) != 0 ? d->title : NULL;
}

static void fill_device(cc_device *d, const drv_info *in, const char *dir)
{
    int k;
    memset(d, 0, sizeof *d);
    snprintf(d->shortname, sizeof d->shortname, "%s", in->name);
    snprintf(d->title, sizeof d->title, "%s", in->title);
    d->port = strcmp(in->name, CC_GAMEPAD) == 0;
    if (strcmp(in->name, "keyboard") == 0) { keyboard_from_dll(d, dir); return; }
    for (k = 0; in->axes && in->axes[k] && in->axes[k + 1]; k += 2) add(d->axis, &d->naxis, 8, in->axes[k + 1], in->axes[k]);
    for (k = 0; in->buttons && in->buttons[k] && in->buttons[k + 1]; k += 2) add(d->button, &d->nbutton, 128, in->buttons[k + 1], in->buttons[k]);
}

static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int cc_drivers_load(cc_drivers *dv, const char *dir, int gamepad)
{
    char names[CC_MAXDEV][16];
    int n = 0, k;
    DIR *dd = opendir(dir);
    struct dirent *de;
    memset(dv, 0, sizeof *dv);
    while (dd && (de = readdir(dd)) && n < CC_MAXDEV) {
        size_t l = strlen(de->d_name);
        if (l < 5 || l > 12 || strcasecmp(de->d_name + l - 4, ".dll")) continue;
        snprintf(names[n], sizeof names[0], "%.*s", (int)(l - 4), de->d_name);
        for (k = 0; names[n][k]; k++) names[n][k] = (char)tolower((unsigned char)names[n][k]);
        if (!cc_driver_title(names[n])) continue;   /* (the original asks the DLL; one the port does not know is left out) */
        for (k = 0; k < n; k++) if (strcmp(names[k], names[n]) == 0) break;
        if (k == n) n++;
    }
    if (dd) closedir(dd);
    qsort(names, (size_t)n, sizeof names[0], name_cmp);
    for (k = 0; k < n; k++) fill_device(&dv->d[k], drv_info_of(names[k]), dir);
    if (gamepad && n < CC_MAXDEV) fill_device(&dv->d[n++], drv_info_of(CC_GAMEPAD), dir);
    dv->n = n;
    return n;
}

int cc_find(const cc_drivers *dv, const char *shortname)
{
    int k;
    for (k = 0; k < dv->n; k++) if (strcasecmp(dv->d[k].shortname, shortname) == 0) return k;
    return -1;
}

/* ---- configurations ------------------------------------------------------------------------------------------------- */

void cc_clear(cc_config *c)
{
    int l, s;
    for (l = 0; l < CC_LEVELS; l++)
        for (s = 0; s < CC_SLOTS; s++) {
            cc_binding *b = &c->b[l][s];
            b->device = -1; b->flags = 0; b->pad = 0; b->a = b->b = -1; b->type = s > 6 ? 1 : 0;
        }
}

/* FUN_00020fc0: the file's device numbers renumbered by name to the drivers present (-1 if absent) */
int cc_load(cc_config *c, const char *path, const cc_drivers *dv)
{
    uint8_t d[3872];
    FILE *f = ci_fopen(path);
    int i, l, s, map[16];
    if (!f) return -1;
    if (fread(d, 1, sizeof d, f) != sizeof d) { fclose(f); return -1; }
    fclose(f);
    memset(c, 0, sizeof *c);
    memcpy(c->name, d, 64);   /* all 64 bytes: what follows the name's end is kept too (written back as read) */
    memcpy(c->table, d + 64, sizeof c->table);
    for (i = 0; i < 16; i++) {
        char nm[13];
        memcpy(nm, d + 68 + i * 16, 12); nm[12] = 0;
        map[i] = -1;
        if (dv) { int k; for (k = 0; k < dv->n; k++) if (nm[0] && strcmp(nm, dv->d[k].shortname) == 0) map[i] = k; }
        else map[i] = rd32(d + 64 + i * 16);
    }
    for (l = 0; l < CC_LEVELS; l++)
        for (s = 0; s < CC_SLOTS; s++) {
            const uint8_t *p = d + 320 + (l * CC_SLOTS + s) * 24;
            cc_binding *b = &c->b[l][s];
            b->type = rd32(p); b->device = rd32(p + 4); b->flags = (uint32_t)rd32(p + 8); b->pad = rd32(p + 12);
            b->a = rd32(p + 16); b->b = rd32(p + 20);
            if (b->device >= 0) b->device = b->device < 16 ? map[b->device] : -1;
        }
    return 0;
}

/* FUN_000214a0: the name, the device table, the bindings. The table as FUN_00020fc0 leaves it: each driver present
 * its number and name (zero-filled), the other entries -1 with the name's first byte cleared - the rest of the entry
 * as the last file loaded had it. The port's gamepad counts as present only when a binding uses it, so a
 * configuration without it is the original's byte for byte */
int cc_save(const cc_config *c, const char *path, const cc_drivers *dv)
{
    uint8_t d[3872];
    FILE *f;
    int i, l, s;
    memset(d, 0, sizeof d);
    memcpy(d, c->name, 64);
    memcpy(d + 64, c->table, sizeof c->table);
    for (i = 0; i < 16; i++) {
        int present = dv && i < dv->n;
        if (present && dv->d[i].port) {
            present = 0;
            for (l = 0; l < CC_LEVELS; l++) for (s = 0; s < CC_SLOTS; s++) if (c->b[l][s].device == i) present = 1;
        }
        if (present) { memset(d + 64 + i * 16, 0, 16); wr32(d + 64 + i * 16, (uint32_t)i); memcpy(d + 68 + i * 16, dv->d[i].shortname, strlen(dv->d[i].shortname)); }
        else { wr32(d + 64 + i * 16, 0xffffffffu); d[68 + i * 16] = 0; }
    }
    for (l = 0; l < CC_LEVELS; l++)
        for (s = 0; s < CC_SLOTS; s++) {
            uint8_t *p = d + 320 + (l * CC_SLOTS + s) * 24;
            const cc_binding *b = &c->b[l][s];
            wr32(p, (uint32_t)b->type); wr32(p + 4, (uint32_t)b->device); wr32(p + 8, b->flags); wr32(p + 12, (uint32_t)b->pad);
            wr32(p + 16, (uint32_t)b->a); wr32(p + 20, (uint32_t)b->b);
        }
    if (!(f = fopen(path, "wb"))) return -1;
    if (fwrite(d, 1, sizeof d, f) != sizeof d) { fclose(f); return -1; }
    return fclose(f);
}

void cc_select_from(const cc_config *c, const cc_drivers *dv, int sel[CC_MAXDEV])
{
    int i, l, s, count = 0;
    for (i = 0; i < CC_MAXDEV; i++) sel[i] = i < dv->n && strcmp(dv->d[i].shortname, "keyboard") == 0;
    for (l = 0; l < CC_LEVELS; l++) for (s = 0; s < CC_SLOTS; s++) if (c->b[l][s].device >= 0 && c->b[l][s].device < CC_MAXDEV) sel[c->b[l][s].device] = 1;
    for (i = 0; i < CC_MAXDEV; i++) { if (i >= dv->n || !sel[i] || count > 3) sel[i] = 0; else count++; }
}

/* the gamepad's defaults (port addition, Page One): throttle on the left stick up / down (as keys: the setting stays
 * where it is left), the chassis on the left stick, the torso on the right stick, zoom on the d-pad up / down, the
 * glances on the d-pad left / right, recentre on the right stick press; jump jets A, nearest enemy B, target at the
 * reticle X, next target Y, cycle weapon / group on the right / left shoulder, fire the selected weapon on the right
 * trigger, group 1 on the left trigger, inspect target on the left stick press, next nav point on Back */
static void gamepad_defaults(cc_config *c, int dev)
{
    static const struct { int slot, type, a, b; } G[] = {
        {0, 2, 18, 17}, {1, 0, 0, -1}, {2, 0, 2, -1}, {3, 0, 3, -1}, {6, 2, 12, 11}, {7, 1, 8, -1}, {8, 1, 13, -1},
        {9, 1, 14, -1}, {12, 1, 0, -1}, {17, 1, 16, -1}, {18, 1, 10, -1}, {19, 1, 9, -1}, {20, 1, 15, -1}, {24, 1, 3, -1},
        {26, 1, 2, -1}, {28, 1, 1, -1}, {29, 1, 7, -1}, {30, 1, 4, -1}};
    size_t i;
    cc_clear(c);
    for (i = 0; i < sizeof G / sizeof G[0]; i++) {
        cc_binding *b = &c->b[0][G[i].slot];
        b->type = G[i].type; b->device = dev; b->flags = 0; b->pad = 0; b->a = G[i].a; b->b = G[i].b;
    }
}

int cc_merge_defaults(cc_config *out, const char *dir, const cc_drivers *dv, const int sel[CC_MAXDEV])
{
    int k, l, s, ok = 0;
    static cc_config d;
    cc_clear(out);   /* (the name: the current one's bytes, each file's as it is read, then "Default Config" over them) */
    for (k = 0; k < dv->n && k < CC_MAXDEV; k++) {
        if (!sel[k]) continue;
        if (dv->d[k].port) gamepad_defaults(&d, k);
        else {
            char nm[32], p[1100];
            snprintf(nm, sizeof nm, "%s.CPC", dv->d[k].shortname);
            if (dos_path(dir, nm, p, sizeof p) != 0 || cc_load(&d, p, dv) != 0) continue;
            memcpy(out->name, d.name, sizeof out->name);
            memcpy(out->table, d.table, sizeof out->table);
        }
        ok = 1;
        for (l = 0; l < CC_LEVELS; l++)
            for (s = 0; s < CC_SLOTS; s++) {
                const cc_binding *nb = &d.b[l][s];
                int l2;
                if (nb->device != k) continue;
                for (l2 = 0; l2 < CC_LEVELS; l2++) {   /* from Page One (FUN_00021b30 0x21c65) */
                    cc_binding *t = &out->b[l2][s];
                    if (t->device == nb->device && t->type == nb->type && t->flags == nb->flags && t->a == nb->a && t->b == nb->b &&
                        t->pad == nb->pad) break;                    /* already there */
                    if (t->device < 0) { *t = *nb; break; }
                }
            }
    }
    memcpy(out->name, "Default Config", 15);   /* strcpy: the bytes after it stay */
    return ok ? 0 : -1;
}

static const char *in_map(const cc_drivers *dv, int device, int type, int idx)
{
    const cc_device *d;
    if (device < 0 || device >= dv->n) return NULL;
    d = &dv->d[device];
    if (type == 0) return idx >= 0 && idx < d->naxis ? d->axis[idx].map : NULL;
    return idx >= 0 && idx < d->nbutton ? d->button[idx].map : NULL;
}

void cc_describe(const cc_binding *b, const cc_drivers *dv, char *out, size_t cap)
{
    const char *x = in_map(dv, b->device, b->type, b->a), *y = in_map(dv, b->device, b->type, b->b);
    const char *dn = b->device >= 0 && b->device < dv->n ? (strcmp(dv->d[b->device].shortname, "keyboard") == 0 ? "key" : dv->d[b->device].shortname) : "";
    if (b->device < 0 || !x) { snprintf(out, cap, "----"); return; }
    if (b->type == 2) snprintf(out, cap, "%s %s/%s", dn, x, y ? y : "?");
    else snprintf(out, cap, "%s %s", dn, x);
}

/* ---- INPUT.MAP (FUN_00020b40) --------------------------------------------------------------------------------------- */

typedef struct { char *o; size_t n, cap; int over, analog, discrete; const cc_drivers *dv; } sb;
static void put(sb *s, const char *t) { size_t l = strlen(t); if (s->n + l + 1 > s->cap) { s->over = 1; return; } memcpy(s->o + s->n, t, l + 1); s->n += l; }

/* one block: "name {", "\t+ dev\tinput", the modifier lines (FUN_000201d0: bit 0 / 4 Control, 1 / 5 Alt, 2 / 6 Shift:
 * "+" held, "-" not held), "}" */
static void block(sb *s, const char *name, int sign, int device, const char *input, uint32_t flags)
{
    static const char *const MOD[3] = {"Control", "Alt", "Shift"};
    char line[160];
    int m;
    snprintf(line, sizeof line, "%s {\r\n\t%c %s\t%s\r\n", name, sign < 0 ? '-' : '+', s->dv->d[device].shortname, input);
    put(s, line);
    for (m = 0; m < 3; m++) {
        if (flags & (1u << m)) { snprintf(line, sizeof line, "\t+ keyboard\t%s\r\n", MOD[m]); put(s, line); }
        else if (flags & (0x10u << m)) { snprintf(line, sizeof line, "\t- keyboard\t%s\r\n", MOD[m]); put(s, line); }
    }
    put(s, "}\r\n");
}

/* a button block and, for jumpjet_fire_*, its jumpjet_enabled (FUN_000202d0) */
static void button_block(sb *s, const char *name, const cc_binding *b, int input, uint32_t flags)
{
    const char *in = in_map(s->dv, b->device, 1, input);
    if (input < 0 || !in) return;
    s->discrete++;
    block(s, name, 1, b->device, in, flags);
    if (strncmp(name, "jumpjet_fire_", 13) == 0) { s->discrete++; block(s, "jumpjet_enabled", 1, b->device, in, flags); }
}

/* FUN_000205e0: a binding under a name, then its aliases */
static void write_binding(sb *s, const char *name, cc_binding *b)
{
    if (b->a < 0 || b->device < 0 || b->device >= s->dv->n) return;
    if (b->type == 0) {
        const char *in = in_map(s->dv, b->device, 0, b->a);
        if (in) { s->analog++; block(s, name, (b->flags & 0x80000000u) ? -1 : 1, b->device, in, b->flags); }
    } else if (b->type == 1) button_block(s, name, b, b->a, b->flags);
    else if (b->type == 2) {
        char n1[80], n2[80], base[64], *dl;
        snprintf(base, sizeof base, "%s", name);
        if ((dl = strstr(base, "_delta"))) *dl = 0;
        if (!(b->flags & 0x80000000u)) { snprintf(n1, sizeof n1, "%s_minus", base); snprintf(n2, sizeof n2, "%s_plus", base); }
        else { snprintf(n1, sizeof n1, "%s_plus", base); snprintf(n2, sizeof n2, "%s_minus", base); }
        button_block(s, n1, b, b->a, b->flags);
        button_block(s, n2, b, b->b, (uint32_t)b->pad);   /* the second input with the second flags */
    }
    if (strcmp(name, "pilot_tilt") == 0) write_binding(s, "track_height_delta", b);
    else if (strcmp(name, "pilot_pan") == 0) write_binding(s, "eyepoint_pan_delta", b);
    else if (strcmp(name, "zoom_factor") == 0) { b->flags ^= 0x80000000u; write_binding(s, "track_distance_delta", b); b->flags ^= 0x80000000u; }
    else if (strcmp(name, "torso_tilt_reset") == 0) {
        write_binding(s, "pilot_tilt_reset", b); write_binding(s, "torso_pan_reset", b); write_binding(s, "pilot_pan_reset", b);
    } else if (strcmp(name, "glance_up") == 0) write_binding(s, "track_height_minus", b);
    else if (strcmp(name, "glance_down") == 0) write_binding(s, "track_height_plus", b);
    else if (strcmp(name, "glance_right") == 0) write_binding(s, "eyepoint_pan_plus", b);
    else if (strcmp(name, "glance_left") == 0) write_binding(s, "eyepoint_pan_minus", b);
}

size_t cc_generate(cc_config *c, const cc_drivers *dv, char *out, size_t cap)
{
    cc_binding *all = &c->b[0][0];
    const int n = CC_LEVELS * CC_SLOTS;
    int i, j, kb = cc_find(dv, "keyboard");
    sb s;
    memset(&s, 0, sizeof s);
    s.o = out; s.cap = cap; s.dv = dv;
    /* the second flags: a two-button control on one key takes the first's modifiers as NOT modifiers, on two keys the
     * first's flags; others none */
    for (i = 0; i < n; i++) {
        cc_binding *k = &all[i];
        if (k->type < 2) k->pad = 0;
        else if (k->type == 2) k->pad = k->b == k->a ? (int32_t)((k->flags & 7) << 4) : (int32_t)k->flags;
    }
    /* the NOT modifiers: a binding with modifiers m marks them on every other binding of its kind on the same input
     * number (whatever the device) - the axes among the axes, the keys / buttons among those, a two-input control's
     * second input too */
    for (i = 0; i < n; i++) {
        const cc_binding *k = &all[i];
        uint32_t m = k->flags & 7, mb = (uint32_t)k->pad & 7;
        if (k->device < 0 || !m) continue;
        for (j = 0; j < n; j++) {
            cc_binding *o = &all[j];
            if (j == i || o->device < 0) continue;
            if (k->type == 0) { if (o->type == 0 && o->a == k->a) o->flags |= m << 4; continue; }
            if (k->type > 2 || o->type == 0) continue;
            if (o->a == k->a) o->flags |= m << 4;
            if (o->type == 2 && o->b == k->a) o->pad |= (int32_t)(m << 4);
            if (o->type == 2 && o->a == k->b) o->flags |= mb << 4;
            if (o->type == 2 && o->b == k->b) o->pad |= (int32_t)(mb << 4);
        }
    }
    put(&s, "# mw2shell CockPit Config generated map file\r\n");
    for (i = 0; i < n; i++) {
        cc_binding *k = &all[i];
        if (k->device < 0) continue;
        if (i % CC_SLOTS < CC_CONTROLS) write_binding(&s, CC_MAPNAME[i % CC_SLOTS], k);
        k->pad = 0;
        k->flags &= 0x80000007u;
    }
    if (kb >= 0) {   /* the jump-turn keys: keyboard inputs 0x66 GreyDelete / 100 GreyPageDown */
        cc_binding j = {1, kb, 0, -1, 0x66, -1};
        write_binding(&s, "legs_pan_minus", &j); write_binding(&s, "jumpjet_enabled", &j);
        j.a = 100;
        write_binding(&s, "legs_pan_plus", &j); write_binding(&s, "jumpjet_enabled", &j);
    }
    {
        char t[96];
        snprintf(t, sizeof t, "# analog count = %d\r\n# discrete count = %d\r\n", s.analog, s.discrete);
        put(&s, t);
    }
    if (s.over || s.analog >= 0x29 || s.discrete >= 0x5b) return 0;
    return s.n;
}
