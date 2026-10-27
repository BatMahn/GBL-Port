/*
 * inputmap.h - the original's two input files, as MECH2 reads them:
 *
 *   GAMEKEY.MAP  "COMMAND key" lines (GBL MW2.EXE 0x49060, base 0x484e0; '#' starts a comment anywhere): discrete commands. A key is
 *                one character ("t", "`", "\", ";", "/" ...) or a name - F1-F12, Esc, Tab, Enter, BSP, Insert, Pause,
 *                Plus, Pound (GBL's addition: '#', which otherwise starts a comment) - joined by '+' with CTRL / ALT / SHIFT in any order and case. One command per key: the first
 *                line naming a key wins, later ones are ignored; a command may have several keys (ALL_PT_MENU: b and
 *                CTRL+F1) and is unbound by deleting its lines. Modifiers must match exactly (t = NEXT_TARGET, CTRL+t =
 *                RESET_TARGETTING). An unknown command or a broken line ends the reading (the lines before it stay).
 *   INPUT.MAP    "control {" blocks of conditions "+ device key" (held) / "- device key" (not held), all of which
 *                must hold (up to 8); a control listed in several blocks is on when any block is (the Cockpit Controls
 *                screen writes this file; KEYBOARD.MAP, JOYSTICK.MAP, ... are templates in the same syntax). Key names
 *                are KEYBOARD.DLL's (IM_KEYNAMES): the plain cursor-pad names are the keypad (LeftArrow = keypad 4,
 *                Home = keypad 7, Insert = keypad 0, Delete = keypad .), "Grey" ones the separate cursor block and the
 *                keypad's grey operators (GreySlash, GreyStar, GreyPlus, GreyMinus); letters, One..Zero, LeftShift ...
 * Both: any case, CR LF or LF line ends, tabs or spaces; a UTF-8 byte order mark is skipped (port).
 * Stick and mouse lines are evaluated too (im_held: buttons and hat directions; im_axis: the axes - see g_im_dev).
 * The devices are the GIDDI drivers the shell's Cockpit Controls names (src/cockpitcfg.c): the keyboard; the sticks
 * (joystick, fltstck, msjstick, tmaster and GBL's wingman, vpilot, avenger) read from the first SDL joystick; the mouse;
 * the port's gamepad (an SDL game controller). The head trackers (vio1, vio2, vfx1) have no reader: their blocks never
 * hold (the rest of the file works as written).
 */
#ifndef MW2_INPUTMAP_H
#define MW2_INPUTMAP_H

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define IM_MAX_RULES 512
#define IM_MAX_BINDS 256

typedef struct { int sign, device, code, gate, raw; } im_cond;
                                                      /* device 0 keyboard (code: scancode, or -1/-2/-3 Ctrl/Shift/Alt),
                                                       * 1 a stick (joystick / fltstck / msjstick / tmaster / wingman / vpilot /
                                                       * avenger), 2 mouse, 3 a driver without a reader (vio1, vio2, vfx1),
                                                       * 4 the port's gamepad; stick / mouse / gamepad code: 100 + axis (sticks:
                                                       * 0 Left/Right, 1 Up/Down, 2 Rudder / Twist, 3 Throttle; the gamepad: SDL's
                                                       * LeftX LeftY RightX RightY LeftTrigger RightTrigger), 200 + hat x 4 +
                                                       * direction (0 up 1 right 2 down 3 left), 300 + axis x 2 + (1: positive)
                                                       * an axis past half way as a button, else the button number (the
                                                       * gamepad: SDL's A .. DPadRight, 15 / 16 the triggers past half way);
                                                       * gate: +n the stick's button n - 1 must be held, -n must not (the
                                                       * Spaceball's "&C" / "&D" axes); raw: no driver shaping */
/* the live state of the non-keyboard devices (set by the front end each frame) */
/* the mouse as the GIDDI driver keeps it (MOUSE.DLL): INT 33h reset, limits 0..(1024 - 1) x 8 on both axes (fn 7 / 8,
 * 0x10bc1 / 0x10cc7), 1 mickey per 8 pixels on both (fn 0Fh CX = DX = 1, 0x10dcd), the pointer centred (fn 4,
 * 0x10870); its event handler (fn 0Ch) leaves the position in 0x4f2 / 0x4f4, read >> 3 (0x107b0) - a 1024 x 1024 box
 * moved 1 unit a mickey and stopped at its edges; an axis reads ((2 pos - 1024) << 16) / 1024 (0x10350). Kept here as
 * the offset from the centre, -512..511; one SDL relative-motion unit is taken as one mickey (port) */
typedef struct { SDL_Joystick *joy; Uint32 mouse_buttons; int mouse_x, mouse_y; SDL_GameController *pad; } im_devstate;
static im_devstate g_im_dev;
#define IM_MAX_COND 8   /* conditions in a block: the sim takes 8 (0x47ed0, "chord too big" past them) */
typedef struct { char name[40]; int ncond; im_cond c[IM_MAX_COND]; } im_rule;
typedef struct { char action[40]; SDL_Keycode key; int ctrl, alt, shift; } im_bind;
typedef struct {
    im_rule rules[IM_MAX_RULES];
    int     nrules;
    im_bind binds[IM_MAX_BINDS];
    int     nbinds;
    int     have_gamekey, have_input;
} inputmap;

/* INPUT.MAP keyboard key names: GIDDI\KEYBOARD.DLL's list, in its order - entry i is DOS scan code i + 1 (the sim looks a
 * name up in the driver's list, any case, MW2.EXE 0x47950; a name starting with a digit is the entry number itself,
 * atoi). Plain cursor names are the keypad, "Grey" ones the cursor block and the keypad's operators. 0 = no PC key
 * (SDL has none); -1 / -2 / -3: either Control / Shift / Alt */
static const struct { const char *name; int sc; } IM_KEYNAMES[] = {
    {"Esc", SDL_SCANCODE_ESCAPE}, {"One", SDL_SCANCODE_1}, {"Two", SDL_SCANCODE_2}, {"Three", SDL_SCANCODE_3},
    {"Four", SDL_SCANCODE_4}, {"Five", SDL_SCANCODE_5}, {"Six", SDL_SCANCODE_6}, {"Seven", SDL_SCANCODE_7},
    {"Eight", SDL_SCANCODE_8}, {"Nine", SDL_SCANCODE_9}, {"Zero", SDL_SCANCODE_0}, {"Minus", SDL_SCANCODE_MINUS},
    {"Equal", SDL_SCANCODE_EQUALS}, {"Backspace", SDL_SCANCODE_BACKSPACE}, {"Tab", SDL_SCANCODE_TAB}, {"Q", SDL_SCANCODE_Q},
    {"W", SDL_SCANCODE_W}, {"E", SDL_SCANCODE_E}, {"R", SDL_SCANCODE_R}, {"T", SDL_SCANCODE_T}, {"Y", SDL_SCANCODE_Y},
    {"U", SDL_SCANCODE_U}, {"I", SDL_SCANCODE_I}, {"O", SDL_SCANCODE_O}, {"P", SDL_SCANCODE_P},
    {"LeftBracket", SDL_SCANCODE_LEFTBRACKET}, {"RightBracket", SDL_SCANCODE_RIGHTBRACKET}, {"Enter", SDL_SCANCODE_RETURN},
    {"LeftControl", SDL_SCANCODE_LCTRL}, {"A", SDL_SCANCODE_A}, {"S", SDL_SCANCODE_S}, {"D", SDL_SCANCODE_D},
    {"F", SDL_SCANCODE_F}, {"G", SDL_SCANCODE_G}, {"H", SDL_SCANCODE_H}, {"J", SDL_SCANCODE_J}, {"K", SDL_SCANCODE_K},
    {"L", SDL_SCANCODE_L}, {"Semicolon", SDL_SCANCODE_SEMICOLON}, {"Quote", SDL_SCANCODE_APOSTROPHE},
    {"BackQuote", SDL_SCANCODE_GRAVE}, {"LeftShift", SDL_SCANCODE_LSHIFT}, {"BackSlash", SDL_SCANCODE_BACKSLASH},
    {"Z", SDL_SCANCODE_Z}, {"X", SDL_SCANCODE_X}, {"C", SDL_SCANCODE_C}, {"V", SDL_SCANCODE_V}, {"B", SDL_SCANCODE_B},
    {"N", SDL_SCANCODE_N}, {"M", SDL_SCANCODE_M}, {"Comma", SDL_SCANCODE_COMMA}, {"Period", SDL_SCANCODE_PERIOD},
    {"Slash", SDL_SCANCODE_SLASH}, {"RightShift", SDL_SCANCODE_RSHIFT}, {"GreyStar", SDL_SCANCODE_KP_MULTIPLY},
    {"LeftAlt", SDL_SCANCODE_LALT}, {"Space", SDL_SCANCODE_SPACE}, {"CAPSLock", SDL_SCANCODE_CAPSLOCK},
    {"F1", SDL_SCANCODE_F1}, {"F2", SDL_SCANCODE_F2}, {"F3", SDL_SCANCODE_F3}, {"F4", SDL_SCANCODE_F4}, {"F5", SDL_SCANCODE_F5},
    {"F6", SDL_SCANCODE_F6}, {"F7", SDL_SCANCODE_F7}, {"F8", SDL_SCANCODE_F8}, {"F9", SDL_SCANCODE_F9}, {"F10", SDL_SCANCODE_F10},
    {"NUMLock", SDL_SCANCODE_NUMLOCKCLEAR}, {"ScrollLock", SDL_SCANCODE_SCROLLLOCK}, {"Home", SDL_SCANCODE_KP_7},
    {"UpArrow", SDL_SCANCODE_KP_8}, {"PageUp", SDL_SCANCODE_KP_9}, {"GreyMinus", SDL_SCANCODE_KP_MINUS},
    {"LeftArrow", SDL_SCANCODE_KP_4}, {"Keypad5", SDL_SCANCODE_KP_5}, {"RightArrow", SDL_SCANCODE_KP_6},
    {"GreyPlus", SDL_SCANCODE_KP_PLUS}, {"End", SDL_SCANCODE_KP_1}, {"DownArrow", SDL_SCANCODE_KP_2},
    {"PageDown", SDL_SCANCODE_KP_3}, {"Insert", SDL_SCANCODE_KP_0}, {"Delete", SDL_SCANCODE_KP_PERIOD},
    {"SYSREQ", SDL_SCANCODE_SYSREQ}, {"KeypadEnter", SDL_SCANCODE_KP_ENTER}, {"LeftBackSlash", SDL_SCANCODE_NONUSBACKSLASH},
    {"F11", SDL_SCANCODE_F11}, {"F12", SDL_SCANCODE_F12}, {"GreySlash", SDL_SCANCODE_KP_DIVIDE}, {"PA1", 0},
    {"F13", SDL_SCANCODE_F13}, {"F14", SDL_SCANCODE_F14}, {"F15", SDL_SCANCODE_F15}, {"GreyHome", SDL_SCANCODE_HOME},
    {"GreyUpArrow", SDL_SCANCODE_UP}, {"GreyPageUp", SDL_SCANCODE_PAGEUP}, {"GreyLeftArrow", SDL_SCANCODE_LEFT},
    {"GreyRightArrow", SDL_SCANCODE_RIGHT}, {"GreyEnd", SDL_SCANCODE_END}, {"GreyDownArrow", SDL_SCANCODE_DOWN},
    {"GreyPageDown", SDL_SCANCODE_PAGEDOWN}, {"GreyInsert", SDL_SCANCODE_INSERT}, {"GreyDelete", SDL_SCANCODE_DELETE},
    {"F21", SDL_SCANCODE_F21}, {"F22", SDL_SCANCODE_F22}, {"F23", SDL_SCANCODE_F23}, {"F24", SDL_SCANCODE_F24},
    {"UNNAMED_1", 0}, {"EraseEOF", 0}, {"CopyPlay", 0}, {"RightCtrl", SDL_SCANCODE_RCTRL}, {"RightAlt", SDL_SCANCODE_RALT},
    {"CRSel", SDL_SCANCODE_CRSEL}, {"EXSel", SDL_SCANCODE_EXSEL}, {"UNAMED_2", 0}, {"Clear", SDL_SCANCODE_CLEAR},
    {"Shift", -2}, {"Control", -1}, {"Alt", -3},
};

/* an INPUT.MAP keyboard key name -> SDL scancode (or -1 / -2 / -3 for either Control / Shift / Alt); 0 none */
static int im_scancode(const char *n)
{
    static const struct { const char *name; int sc; } ALIAS[] = {   /* port: names the driver lacks, kept from earlier */
        {"Escape", SDL_SCANCODE_ESCAPE}, {"Tilde", SDL_SCANCODE_GRAVE}};
    size_t i, nk = sizeof IM_KEYNAMES / sizeof IM_KEYNAMES[0];
    if (isdigit((unsigned char)n[0])) {   /* 0x47950: ctype digit -> the entry number (atoi) */
        int e = atoi(n);
        return e >= 0 && (size_t)e < nk ? IM_KEYNAMES[e].sc : 0;
    }
    for (i = 0; i < nk; i++) if (strcasecmp(IM_KEYNAMES[i].name, n) == 0) return IM_KEYNAMES[i].sc;
    for (i = 0; i < sizeof ALIAS / sizeof ALIAS[0]; i++) if (strcasecmp(ALIAS[i].name, n) == 0) return ALIAS[i].sc;
    return 0;   /* unknown: the sim warns and takes entry 0 (Esc); the port never holds it */
}

/* GAMEKEY.MAP's commands, GBL MW2.EXE's table 0xae6d8 (155 names: the base game's 153 - table 0x9dca4 - plus CHAT_MODE and
 * TOGGLE_AMS; compared in any case); a line naming anything else ends the file's reading */
static const char *const IM_GAMEKEY_ACTIONS[] = {
    "RESET_INPUTS", "MFD_CYCLE", "TOGGLE_HTAL", "TOGGLE_REAR_VIEW", "TOGGLE_DOWN_VIEW", "TOGGLE_WEAPON_DISPLAY",
    "TOGGLE_DAMAGE_DISPLAY", "TOGGLE_TARGET_DISPLAY", "COCKPIT_VIEW", "FREEEYE_VIEW", "TRACK_VIEW", "TRACK_NEXT", "TRACK_PREV",
    "ORDINANCE_VIEW", "JUMP_DEBUGGER", "REQUEST_ORDINANCE", "FEET_TO_TORSO", "INSPECT_TARGET", "TOGGLE_HUD", "JETTISON_AMMO",
    "COCKPIT_ZOOM_IN", "COCKPIT_ZOOM_OUT", "COCKPIT_RESET_ZOOM", "DECREASE_THROTTLE", "INCREASE_THROTTLE", "THROTTLE_STOP",
    "THROTTLE_2", "THROTTLE_3", "THROTTLE_4", "THROTTLE_5", "THROTTLE_6", "THROTTLE_7", "THROTTLE_8", "THROTTLE_9",
    "THROTTLE_FULL", "NEXT_GAMETHING", "PREV_GAMETHING", "RESET_GAMETHING", "RESET_TARGETTING", "NEXT_NAVPOINT",
    "PREV_NAVPOINT", "RESET_NAVPOINT", "NEXT_GAMEPIECE", "PREV_GAMEPIECE", "RESET_GAMEPIECE", "NEXT_RADAR_MODE",
    "RADAR_ZOOM_IN", "RADAR_ZOOM_OUT", "RADAR_RESET_ZOOM", "RADAR_MAP_TOGGLE", "MAIN_MENU", "USER_MENU", "ALL_PT_MENU",
    "PT_2_MENU", "PT_3_MENU", "HELP_MENU", "EJECT", "TOGGLE_AUTOEJECT", "STARTUP_MECH", "SHUTDOWN_MECH", "REVERSE_DIRECTION",
    "OVERRIDE_SHUTDOWN", "INFRARED", "ENHANCED_VISION", "TOGGLE_AMS", "DISPLAY_OBJECTIVES", "CHAT_MODE", "TOGGLE_AUTOPILOT", "TOGGLE_GROUP_FIRE",
    "NEXT_TARGET", "PREV_TARGET", "RESET_TARGET", "TARGET_NEAREST_ENEMY", "TARGET_FRIENDLY", "TARGET_AT_RETICLE",
    "TARGET_LAST_SHOT", "NEXT_OBJECTIVE", "PUNCH_IN_AUTO_HDG", "TOGGLE_MASC", "SELF_DESTRUCT", "PAUSE_GAME", "EXIT_SIM",
    "ABOUT_BANNER", "DUMP_GIF", "DEBUG_CRITICAL_HITS_1", "DEBUG_CRITICAL_HITS_2", "DEBUG_CRITICAL_HITS_3",
    "DEBUG_CRITICAL_HITS_4", "DEBUG_CRITICAL_HITS_5", "DEBUG_CRITICAL_HITS_6", "DEBUG_CRITICAL_HITS_7",
    "DEBUG_CRITICAL_HITS_8", "DEBUG_CRITICAL_HITS_9", "DEBUG_CRITICAL_HITS_0", "SELECT_HEAD", "SELECT_RIGHT_TORSO",
    "SELECT_CENTER_TORSO", "SELECT_LEFT_TORSO", "SELECT_RIGHT_ARM", "SELECT_LEFT_ARM", "SELECT_RIGHT_LEG", "SELECT_LEFT_LEG",
    "AI_0_MONO_OBJECTIVES", "AI_1_MONO_OBJECTIVES", "AI_2_MONO_OBJECTIVES", "AI_3_MONO_OBJECTIVES", "AI_4_MONO_OBJECTIVES",
    "AI_5_MONO_OBJECTIVES", "AI_6_MONO_OBJECTIVES", "AI_7_MONO_OBJECTIVES", "AI_8_MONO_OBJECTIVES", "AI_9_MONO_OBJECTIVES",
    "OBJECTIVES_OFF", "NEXT_MONO_OBJECTIVE", "PREV_MONO_OBJECTIVE", "FAIL_MONO_OBJECTIVE", "SUCCEED_MONO_OBJECTIVE",
    "TOGGLE_GP_TEXT_MAPS", "TOGGLE_GT_TEXT_MAPS", "TOGGLE_GS_TEXT_MAPS", "TOGGLE_TERRAIN_TEXT_MAPS", "GO_TURBO_CONTROL",
    "TOGGLE_MEM_INFO", "TOGGLE_CACHE_INFO", "COLLISION_WIREFRAME", "TOGGLE_FRAMERATE_MAIN", "TOGGLE_SCENE_INFO_MAIN",
    "TOGGLE_EYE_POSITION_MAIN", "TOGGLE_VULNERABILITY", "TOGGLE_AMMO_LIMIT", "TOGGLE_SPLASH_DAMAGE",
    "TOGGLE_COLLISION_DAMAGE", "TOGGLE_HEAT_TRACKING", "TOGGLE_LOD_QUALITY", "DUMP_CACHE_ONCE", "DUMP_CACHE_REPEAT",
    "TOGGLE_RADAR_DAMAGE", "TOGGLE_FRAMERATE", "CRACK_MODE", "TOGGLE_EYE_POSITION", "TOGGLE_SCENE_INFO", "TOGGLE_WIREFRAME",
    "TOGGLE_PALETTE", "ADD_WEAPON_TO_GROUP_1", "ADD_WEAPON_TO_GROUP_2", "ADD_WEAPON_TO_GROUP_3", "NEXT_WEAPON_GROUP",
    "FIRE_WEAPON_GROUP", "CREATE_NAVPOINT_TEST", "DELETE_NAVPOINT_TEST", "TOGGLE_SAVE_FRAMES", "PLAY_CURRENT_MIDI",
    "PLAY_NEXT_MIDI", "HACK_JETS", "KILL_TARGETTED_MECH"};

/* INPUT.MAP's controls, GBL MW2.EXE's table 0xadbf8 (= the base game's 0x9d1c4: 87 names, any case); a block naming anything else ends the reading */
static const char *const IM_INPUT_CONTROLS[] = {
    "torso_tilt", "torso_tilt_plus", "torso_tilt_minus", "torso_tilt_reset", "torso_pan", "torso_pan_plus", "torso_pan_minus",
    "torso_pan_set", "torso_pan_reset", "throttle", "legs_pan_delta", "jumpjet_enabled", "jumpjet_fire_left",
    "jumpjet_fire_right", "jumpjet_fire_forward", "jumpjet_fire_backward", "throttle_plus", "throttle_minus", "throttle_set",
    "weapon_fire", "weapon_cycle", "weapon_fire_group", "weapon_fire_group_1", "weapon_fire_group_2", "weapon_fire_group_3",
    "weapon_cycle_group", "toggle_group_fire", "legs_pan_minus", "legs_pan_plus", "advance_nav", "previous_nav", "reset_nav",
    "advance_target", "previous_target", "reset_target", "target_reticle", "target_friendly", "nearest_enemy",
    "target_last_shot", "inspect_target", "advance_gamepiece", "previous_gamepiece", "reset_gamepiece", "advance_gamething",
    "previous_gamething", "reset_gamething", "self_destruct", "autopilot", "pilot_tilt", "pilot_tilt_plus", "pilot_tilt_minus",
    "pilot_tilt_reset", "pilot_pan", "pilot_pan_plus", "pilot_pan_minus", "pilot_pan_reset", "glance_left", "glance_right",
    "glance_up", "glance_down", "eyepoint_tilt", "eyepoint_tilt_plus", "eyepoint_tilt_minus", "eyepoint_tilt_reset",
    "eyepoint_pan_delta", "eyepoint_pan_plus", "eyepoint_pan_minus", "eyepoint_pan_reset", "eyepoint_slide_delta",
    "eyepoint_slide_plus", "eyepoint_slide_minus", "track_distance_delta", "track_distance_plus", "track_distance_minus",
    "track_height_delta", "track_height_plus", "track_height_minus", "zoom_factor", "zoom_factor_plus", "zoom_factor_minus",
    "zoom_factor_reset", "menu_item", "menu_item_reset", "menu_value", "menu_value_reset", "menu_enter", "menu_abort"};

static int im_find(const char *const *list, size_t n, const char *name)
{
    size_t i;
    for (i = 0; i < n; i++) if (strcasecmp(list[i], name) == 0) return (int)i;
    return -1;
}

/* a GAMEKEY.MAP key token (GBL MW2.EXE 0x49060: the names table 0xaf1e0 - 23 entries, the base's 0x9e7a4 plus POUND; any case; any other token is its first character,
 * lower case 0x75dee - so "Space" reads as s) -> SDL keycode; *mods |= 1 Ctrl, 2 Alt, 4 Shift */
static SDL_Keycode im_keytoken(const char *t, int *mods)
{
    static const struct { const char *name; SDL_Keycode k; } N[] = {
        {"TAB", SDLK_TAB}, {"INSERT", SDLK_INSERT}, {"ESC", SDLK_ESCAPE}, {"BSP", SDLK_BACKSPACE}, {"ENTER", SDLK_RETURN},
        {"PAUSE", SDLK_PAUSE}, {"POUND", '#'}, {"PLUS", '+'}};
    size_t i;
    if (strcasecmp(t, "CTRL") == 0) { *mods |= 1; return SDLK_UNKNOWN; }
    if (strcasecmp(t, "ALT") == 0) { *mods |= 2; return SDLK_UNKNOWN; }
    if (strcasecmp(t, "SHIFT") == 0) { *mods |= 4; return SDLK_UNKNOWN; }
    if ((t[0] == 'F' || t[0] == 'f') && t[1] && strspn(t + 1, "0123456789") == strlen(t + 1) && atoi(t + 1) >= 1 && atoi(t + 1) <= 12)
        return SDLK_F1 + atoi(t + 1) - 1;
    for (i = 0; i < sizeof N / sizeof N[0]; i++) if (strcasecmp(N[i].name, t) == 0) return N[i].k;
    return (SDL_Keycode)tolower((unsigned char)t[0]);
}

/* the US layout's scancode for a GAMEKEY.MAP key (the original reads scan codes) */
static SDL_Scancode im_key_scancode(SDL_Keycode k)
{
    static const struct { SDL_Keycode k; SDL_Scancode s; } P[] = {
        {'`', SDL_SCANCODE_GRAVE}, {'\\', SDL_SCANCODE_BACKSLASH}, {'\'', SDL_SCANCODE_APOSTROPHE}, {';', SDL_SCANCODE_SEMICOLON},
        {'/', SDL_SCANCODE_SLASH}, {',', SDL_SCANCODE_COMMA}, {'.', SDL_SCANCODE_PERIOD}, {'-', SDL_SCANCODE_MINUS},
        {'=', SDL_SCANCODE_EQUALS}, {'[', SDL_SCANCODE_LEFTBRACKET}, {']', SDL_SCANCODE_RIGHTBRACKET}, {' ', SDL_SCANCODE_SPACE},
        {SDLK_ESCAPE, SDL_SCANCODE_ESCAPE}, {SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE}, {SDLK_PAUSE, SDL_SCANCODE_PAUSE},
        {SDLK_TAB, SDL_SCANCODE_TAB}, {SDLK_INSERT, SDL_SCANCODE_INSERT}, {SDLK_RETURN, SDL_SCANCODE_RETURN}, {'+', SDL_SCANCODE_KP_PLUS},
    };
    size_t i;
    if (k >= 'a' && k <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + (k - 'a'));
    if (k >= '1' && k <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (k - '1'));
    if (k == '0') return SDL_SCANCODE_0;
    if (k >= SDLK_F1 && k <= SDLK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (k - SDLK_F1));
    for (i = 0; i < sizeof P / sizeof P[0]; i++) if (P[i].k == k) return P[i].s;
    return SDL_SCANCODE_UNKNOWN;
}

/* an INPUT.MAP device name: the driver's device class (see im_cond) */
static int im_device(const char *b)
{
    static const char *const STICK[] = {"joystick", "fltstck", "msjstick", "tmaster", "wingman", "vpilot", "avenger"};
    size_t i;
    if (strcasecmp(b, "keyboard") == 0) return 0;
    if (strcasecmp(b, "mouse") == 0) return 2;
    if (strcasecmp(b, "gamepad") == 0) return 4;
    for (i = 0; i < sizeof STICK / sizeof STICK[0]; i++) if (strcasecmp(b, STICK[i]) == 0) return 1;
    return 3;
}

/* an input name of a stick, the mouse or the gamepad (the names each GIDDI driver gives its inputs; src/cockpitcfg.c) */
static void im_input_code(im_cond *k, const char *dev, const char *c)
{
    static const struct { const char *n; int code; } IN[] = {   /* the base drivers (joystick, fltstck, msjstick, tmaster) */
        {"Left/Right", 100}, {"Up/Down", 101}, {"Rudder", 102}, {"Twist", 102}, {"Throttle", 103},
        {"HatUp", 200}, {"HatRight", 201}, {"HatDown", 202}, {"HatLeft", 203},
        {"Button1", 0}, {"Trigger", 0}, {"LeftBtn", 0}, {"Button2", 1}, {"TopBtn", 1}, {"RearBtn", 1},
        {"MiddleBtn", 2}, {"BottomBtn", 3}, {"RightBtn", 3}};
    /* WINGMAN.DLL (TMASTER.DLL's code with four buttons): Trigger, Button1-3, the hat */
    static const struct { const char *n; int code; } WING[] = {
        {"Left/Right", 100}, {"Up/Down", 101}, {"Rudder", 102}, {"Trigger", 0}, {"Button1", 1}, {"Button2", 2}, {"Button3", 3},
        {"HatUp", 200}, {"HatRight", 201}, {"HatDown", 202}, {"HatLeft", 203}};
    /* VPILOT.DLL (FLTSTCK.DLL's code): the yoke In/Out and Left/Right, rudder, throttle; six buttons, two hats */
    static const struct { const char *n; int code; } VP[] = {
        {"Left/Right", 100}, {"In/Out", 101}, {"Rudder", 102}, {"Throttle", 103},
        {"Button1", 0}, {"Button2", 1}, {"Button3", 2}, {"Button4", 3}, {"Button5", 4}, {"Button6", 5},
        {"LHatUp", 200}, {"LHatRight", 201}, {"LHatDown", 202}, {"LHatLeft", 203},
        {"RHatUp", 204}, {"RHatRight", 205}, {"RHatDown", 206}, {"RHatLeft", 207}};
    /* AVENGER.DLL (Spaceball Avenger, six axes Tx Ty Tz Rx Ry Rz and buttons A-F; read as a six-axis SDL joystick in
     * that axis order - ASSUMED). AVENGER.TXT: Tz is the throttle only while C is held, Rx / Ry turn the torso unless D
     * is held, the pilot's view while it is; TurnLeft / TurnRight / JumpJet / LookUp / LookDown are the motions as
     * buttons (half way - ASSUMED; JumpJet = Ty up, taken as negative - ASSUMED); no shaping (ASSUMED) */
    static const struct { const char *n; int code, gate; } AV[] = {
        {"Tx", 100, 0}, {"Ty", 101, 0}, {"Tz&C", 102, 3}, {"Rx", 103, -4}, {"Ry", 104, -4}, {"Rx&D", 103, 4}, {"Ry&D", 104, 4},
        {"ButtonA", 0, 0}, {"ButtonB", 1, 0}, {"ButtonC", 2, 0}, {"ButtonD", 3, 0}, {"ButtonE", 4, 0}, {"ButtonF", 5, 0},
        {"TurnLeft", 300, 0}, {"TurnRight", 301, 0}, {"JumpJet", 302, 0}, {"LookUp", 306, 0}, {"LookDown", 307, 0}};
    /* the gamepad (port addition): SDL_GameController's axes and buttons, then the triggers and the stick directions as
     * buttons */
    static const char *const PADA[] = {"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
    static const char *const PADB[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LeftStick", "RightStick", "LeftShoulder",
        "RightShoulder", "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "LT", "RT"};
    static const struct { const char *n; int code; } PADD[] = {
        {"LeftStickLeft", 300}, {"LeftStickRight", 301}, {"LeftStickUp", 302}, {"LeftStickDown", 303},
        {"RightStickLeft", 304}, {"RightStickRight", 305}, {"RightStickUp", 306}, {"RightStickDown", 307}};
    size_t q;
    k->code = -100;
    if (k->device == 4) {
        k->raw = 1;
        for (q = 0; q < sizeof PADA / sizeof PADA[0]; q++) if (strcasecmp(PADA[q], c) == 0) k->code = 100 + (int)q;
        for (q = 0; q < sizeof PADB / sizeof PADB[0]; q++) if (strcasecmp(PADB[q], c) == 0) k->code = (int)q;
        for (q = 0; q < sizeof PADD / sizeof PADD[0]; q++) if (strcasecmp(PADD[q].n, c) == 0) k->code = PADD[q].code;
        return;
    }
    if (strcasecmp(dev, "wingman") == 0) { for (q = 0; q < sizeof WING / sizeof WING[0]; q++) if (strcasecmp(WING[q].n, c) == 0) k->code = WING[q].code; return; }
    if (strcasecmp(dev, "vpilot") == 0) { for (q = 0; q < sizeof VP / sizeof VP[0]; q++) if (strcasecmp(VP[q].n, c) == 0) k->code = VP[q].code; return; }
    if (strcasecmp(dev, "avenger") == 0) {
        k->raw = 1;
        for (q = 0; q < sizeof AV / sizeof AV[0]; q++) if (strcasecmp(AV[q].n, c) == 0) { k->code = AV[q].code; k->gate = AV[q].gate; }
        return;
    }
    for (q = 0; q < sizeof IN / sizeof IN[0]; q++) if (strcasecmp(IN[q].n, c) == 0) k->code = IN[q].code;
    if (k->device == 2) {   /* the mouse: LeftBtn 1, MiddleBtn 2, RightBtn 3 (SDL numbering) */
        if (strcasecmp(c, "LeftBtn") == 0) k->code = SDL_BUTTON_LEFT;
        else if (strcasecmp(c, "MiddleBtn") == 0) k->code = SDL_BUTTON_MIDDLE;
        else if (strcasecmp(c, "RightBtn") == 0) k->code = SDL_BUTTON_RIGHT;
    }
}

/* a line as the sim's reader takes it (GBL 0x48580, base 0x47a00): cut at the first '#' or newline; *lineno counts every line; a leading
 * UTF-8 byte order mark (a modern editor's) is dropped (port); returns 0 for a line with nothing but blanks */
static int im_line(char *line, int *lineno)
{
    char *p;
    if (++*lineno == 1 && (unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf)
        memmove(line, line + 3, strlen(line + 3) + 1);
    if ((p = strpbrk(line, "#\n"))) *p = 0;
    for (p = line; *p; p++) if (!isspace((unsigned char)*p)) return 1;
    return 0;
}

/* GAMEKEY.MAP (MW2.EXE 0x484e0): each line "COMMAND key" ("%s %s"); the key is tokens joined by '+' in any order (CTRL,
 * ALT, SHIFT and one key). One command per key: a key already taken keeps its first line ("remapping key" warning, the
 * line ignored); a command may have several keys. Reading stops - the lines before it kept - at a line without two
 * words, a key with no token, or an unknown command */
static void im_parse_gamekey(inputmap *m, FILE *f)
{
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        char act[64], key[64], *t;
        im_bind nb;
        int i, ai, mods = 0, dup = 0;
        if (!im_line(line, &lineno)) continue;
        if (sscanf(line, "%63s %63s", act, key) != 2) { fprintf(stderr, "GAMEKEY.MAP line %d: read error - the rest is ignored\n", lineno); break; }
        memset(&nb, 0, sizeof nb);
        nb.key = SDLK_UNKNOWN;
        for (t = strtok(key, "+"), i = 0; t; t = strtok(NULL, "+"), i++) { SDL_Keycode k = im_keytoken(t, &mods); if (k != SDLK_UNKNOWN) nb.key = k; }
        if (!i) { fprintf(stderr, "GAMEKEY.MAP line %d: bad sequence - the rest is ignored\n", lineno); break; }
        nb.ctrl = mods & 1; nb.alt = (mods >> 1) & 1; nb.shift = (mods >> 2) & 1;
        for (i = 0; i < m->nbinds; i++)
            if (m->binds[i].key == nb.key && m->binds[i].ctrl == nb.ctrl && m->binds[i].alt == nb.alt && m->binds[i].shift == nb.shift) dup = 1;
        if (dup) { fprintf(stderr, "GAMEKEY.MAP line %d: key '%s' already taken - line ignored\n", lineno, act); continue; }
        ai = im_find(IM_GAMEKEY_ACTIONS, sizeof IM_GAMEKEY_ACTIONS / sizeof IM_GAMEKEY_ACTIONS[0], act);
        if (ai < 0) { fprintf(stderr, "GAMEKEY.MAP line %d: no gamekey %s - the rest is ignored\n", lineno, act); break; }
        snprintf(nb.action, sizeof nb.action, "%s", IM_GAMEKEY_ACTIONS[ai]);
        if (nb.key != SDLK_UNKNOWN && m->nbinds < IM_MAX_BINDS) m->binds[m->nbinds++] = nb;
    }
    m->have_gamekey = 1;
}

/* INPUT.MAP (GBL MW2.EXE 0x48c10, base 0x48090 - the same code): "control {" (the name in any case; an unknown one ends the reading), then up to 8
 * conditions "+|- device key" (more: "chord too big", the reading ends), "}" closes the block */
static void im_parse_input(inputmap *m, FILE *f)
{
    char line[256];
    im_rule *r = NULL;
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line, a[64], b[64], c[64];
        if (!im_line(line, &lineno)) continue;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '}') { if (r) { m->nrules++; r = NULL; } continue; }
        if (!r && sscanf(p, "%63s %63s", a, b) >= 1 && strchr(p, '{')) {
            char *br = strchr(a, '{');
            int q;
            if (br) *br = 0;
            if (im_find(IM_INPUT_CONTROLS, sizeof IM_INPUT_CONTROLS / sizeof IM_INPUT_CONTROLS[0], a) < 0) {
                fprintf(stderr, "INPUT.MAP line %d: unknown control %s - the rest is ignored\n", lineno, a);
                break;
            }
            if (m->nrules >= IM_MAX_RULES) break;
            r = &m->rules[m->nrules];
            memset(r, 0, sizeof *r);
            for (q = 0; a[q] && q < 39; q++) r->name[q] = (char)tolower((unsigned char)a[q]);
            continue;
        }
        if (r && sscanf(p, "%63s %63s %63s", a, b, c) == 3) {
            im_cond *k;
            if (r->ncond >= IM_MAX_COND) { fprintf(stderr, "INPUT.MAP line %d: chord too big - the rest is ignored\n", lineno); r = NULL; break; }
            k = &r->c[r->ncond++];
            k->sign = a[0] == '-' ? -1 : 1;
            k->device = im_device(b);
            if (k->device == 0) k->code = im_scancode(c);
            else im_input_code(k, b, c);
        }
    }
    m->have_input = 1;
}

/* both files (full paths, already resolved by the caller - any case of the names, the install folder before the disc's);
 * returns 0 if both load */
static int im_load(inputmap *m, const char *gamekey, const char *input)
{
    FILE *f;
    memset(m, 0, sizeof *m);
    if (gamekey && (f = fopen(gamekey, "rb"))) { im_parse_gamekey(m, f); fclose(f); }
    if (input && (f = fopen(input, "rb"))) { im_parse_input(m, f); fclose(f); }
    return m->have_gamekey && m->have_input ? 0 : -1;
}

/* any INPUT.MAP condition on device d (2 = the mouse) */
static int im_uses(const inputmap *m, int d)
{
    int i, k;
    for (i = 0; i < m->nrules; i++) for (k = 0; k < m->rules[i].ncond; k++) if (m->rules[i].c[k].device == d) return 1;
    return 0;
}

/* the key event (sym, mod) is bound to action: the key's one command (GAMEKEY.MAP keeps a key's first line). The
 * keypad's + is the PLUS key */
static int im_action(const inputmap *m, const char *action, SDL_Keycode sym, Uint16 mod)
{
    int i, ctrl = (mod & KMOD_CTRL) != 0, alt = (mod & KMOD_ALT) != 0, shift = (mod & KMOD_SHIFT) != 0;
    if (sym == SDLK_KP_PLUS) sym = '+';
    for (i = 0; i < m->nbinds; i++) {
        const im_bind *b = &m->binds[i];
        if (b->key == sym && b->ctrl == ctrl && b->alt == alt && b->shift == shift) return strcmp(b->action, action) == 0;
    }
    return 0;
}

/* an action's key is held now (with its modifiers): for actions the sim treats as held, e.g. FIRE_WEAPON_GROUP */
static int im_action_held(const inputmap *m, const char *action, const Uint8 *ks)
{
    int i, ctrl = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL], alt = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];
    int shift = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    for (i = 0; i < m->nbinds; i++) {
        const im_bind *b = &m->binds[i];
        SDL_Scancode sc;
        if (strcmp(b->action, action) != 0) continue;
        sc = im_key_scancode(b->key);
        if (sc != SDL_SCANCODE_UNKNOWN && ks[sc] && b->ctrl == ctrl && b->alt == alt && b->shift == shift) return 1;
    }
    return 0;
}

/* an axis condition's value -1..1 (sign applied); 0 if not an axis */
/* a stick axis as the GIDDI joystick driver reports it (JOYSTICK.DLL, read 0x10500 -> 0x10660, calibration 0x10eb0):
 * the deflection from the calibrated centre within a dead zone of span / 16 (axes 1 and 2: 12.5 % of the half travel;
 * the third axis span / 10, 20 %; the fourth raw) - or twice the centre's jitter if more - reads 0; beyond it the output
 * grows linearly to full (0x10000) span / 16 short of the end (87.5 % of the half travel), its magnitude rounded up to
 * the next 1/32 (>> 11, + 1, << 11), and each read is smoothed: (2 x the last + the new) / 3 (at the 30 fps reference) */
static float im_stick_shape(int ax, float v)
{
    static float sm[8];
    static Uint32 last[8];
    float dz = ax == 2 ? 0.2f : 0.125f, a = v < 0 ? -v : v, y;
    Uint32 now = SDL_GetTicks();
    int k = ax & 7;
    if (ax == 3) {   /* "Throttle" (FLTSTCK.DLL / MSJSTICK.DLL, the only drivers naming it): calibrated from its two ends
                      * (0x10540 / MSJ 0x10560): the centre between them, the ends pulled in by a fifth of the half travel,
                      * so full (0x10000) at 80 % of the half travel; no dead zone (+0x40 = 0); the magnitude rounded up to
                      * the next 1/32 (0x10a00); stored as read - no (2 last + new) / 3 smoothing (0x109ca) */
        y = a <= 0.0f ? 0.0f : a / 0.8f;
        if (y > 0) { y = (float)((int)(y * 32.0f) + 1) / 32.0f; if (y > 1) y = 1; }
        return v < 0 ? -y : y;
    }
    if (ax > 3) y = v;   /* further axes: no calibration (stored raw) */
    else {
        y = a <= dz ? 0.0f : (a - dz) / (1.0f - dz - 0.125f);
        if (y > 0) { y = (float)((int)(y * 32.0f) + 1) / 32.0f; if (y > 1) y = 1; }
        if (v < 0) y = -y;
    }
    {   /* (2 x last + new) / 3 for each 1/30 s frame passed */
        static Uint32 acc[8];
        int n;
        if (!last[k] || now - last[k] > 1000) { sm[k] = y; acc[k] = 0; last[k] = now; return y; }
        acc[k] += (now - last[k]) * 30u; last[k] = now;
        for (n = 0; acc[k] >= 1000u && n < 30; n++) { acc[k] -= 1000u; sm[k] = (2.0f * sm[k] + y) / 3.0f; }
    }
    return sm[k];
}

/* the gamepad's axis (port addition): sticks -1..1 with a dead zone of 15 % (the rest scaled to full), the triggers
 * 0..1 (5 %) - ASSUMED, the usual SDL game controller treatment */
static float im_pad_axis(int ax)
{
    float v, a, dz;
    if (!g_im_dev.pad || ax < 0 || ax >= SDL_CONTROLLER_AXIS_MAX) return 0;
    v = (float)SDL_GameControllerGetAxis(g_im_dev.pad, (SDL_GameControllerAxis)ax) / 32767.0f;
    if (v < -1) v = -1;
    if (v > 1) v = 1;
    dz = ax >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0.05f : 0.15f;
    a = v < 0 ? -v : v;
    a = a <= dz ? 0.0f : (a - dz) / (1.0f - dz);
    return v < 0 ? -a : a;
}

/* a stick's raw axis -1..1 */
static float im_joy_axis(int ax)
{
    if (!g_im_dev.joy || ax < 0 || ax >= SDL_JoystickNumAxes(g_im_dev.joy)) return 0;
    return (float)SDL_JoystickGetAxis(g_im_dev.joy, ax) / 32767.0f;
}

/* the condition's gate (the Spaceball's "&C" / "&D" axes): the stick button it needs held / released */
static int im_gate_ok(const im_cond *c)
{
    int b, on;
    if (!c->gate) return 1;
    b = (c->gate < 0 ? -c->gate : c->gate) - 1;
    on = g_im_dev.joy && b < SDL_JoystickNumButtons(g_im_dev.joy) && SDL_JoystickGetButton(g_im_dev.joy, b);
    return c->gate > 0 ? on : !on;
}

static float im_cond_axis(const im_cond *c)
{
    float v = 0;
    if (c->code < 100 || c->code >= 200) return 0;
    if (c->device == 1 && g_im_dev.joy) {
        int ax = c->code - 100;
        v = im_joy_axis(ax);
        if (!c->raw) v = im_stick_shape(ax, v);
        if (!im_gate_ok(c)) v = 0;
    } else if (c->device == 2) v = c->code == 100 ? (float)g_im_dev.mouse_x / 512.0f : c->code == 101 ? (float)g_im_dev.mouse_y / 512.0f : 0;   /* MOUSE.DLL 0x10350 */
    else if (c->device == 4) v = im_pad_axis(c->code - 100);
    return c->sign < 0 ? -v : v;
}

static int im_cond_held(const im_cond *c, const Uint8 *ks)
{
    int on;
    if (c->device != 0) {
        if (c->code >= 100 && c->code < 200) return 1;   /* an axis: the block's other conditions decide */
        if (c->code >= 300 && c->code < 400) {           /* an axis past half way, as a button */
            int ax = (c->code - 300) / 2, pos = (c->code - 300) & 1;
            float v = c->device == 1 ? im_joy_axis(ax) : c->device == 4 ? im_pad_axis(ax) : 0;
            on = pos ? v > 0.5f : v < -0.5f;
        } else if (c->device == 1 && g_im_dev.joy) {
            if (c->code >= 200) { int hat = (c->code - 200) / 4;
                                  Uint8 h = SDL_JoystickNumHats(g_im_dev.joy) > hat ? SDL_JoystickGetHat(g_im_dev.joy, hat) : 0;
                                  static const Uint8 HB[4] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT}; on = (h & HB[(c->code - 200) & 3]) != 0; }
            else on = c->code >= 0 && c->code < SDL_JoystickNumButtons(g_im_dev.joy) && SDL_JoystickGetButton(g_im_dev.joy, c->code);
        } else if (c->device == 2) on = c->code > 0 && (g_im_dev.mouse_buttons & SDL_BUTTON(c->code)) != 0;
        else if (c->device == 4 && g_im_dev.pad) {
            if (c->code == 15 || c->code == 16) on = im_pad_axis(c->code == 15 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f;
            else on = c->code >= 0 && c->code < SDL_CONTROLLER_BUTTON_MAX && SDL_GameControllerGetButton(g_im_dev.pad, (SDL_GameControllerButton)c->code);
        }
        else on = 0;
        return c->sign > 0 ? on : !on;
    }
    if (c->code == -1) on = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
    else if (c->code == -2) on = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    else if (c->code == -3) on = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];
    else on = c->code > 0 && ks[c->code] != 0;   /* an unknown key (0): never held */
    return c->sign > 0 ? on : !on;
}

/* the control's axis value now: the sum over its blocks whose other conditions hold (an analog control in INPUT.MAP:
 * throttle, legs_pan_delta, torso_pan, torso_tilt, ...) on device 1 (stick), 2 (mouse) or 4 (gamepad); *present = it has such a block
 * (2: and one whose other conditions hold now - the engine reads an axis only then, 0x100396e0) */
static float im_axis(const inputmap *m, const char *control, const Uint8 *ks, int device, int *present)
{
    int i, k;
    float v = 0;
    if (present) *present = 0;
    for (i = 0; i < m->nrules; i++) {
        const im_rule *r = &m->rules[i];
        int ok = 1, axis = -1;
        if (strcmp(r->name, control) != 0) continue;
        for (k = 0; k < r->ncond; k++) {
            if (r->c[k].device != 0 && r->c[k].code >= 100 && r->c[k].code < 200) { if (r->c[k].device == device) axis = k; }
            else if (!im_cond_held(&r->c[k], ks)) ok = 0;
        }
        if (axis < 0) continue;
        if (present) *present = 1;
        if (ok) { v += im_cond_axis(&r->c[axis]); if (present) *present = 2; }   /* 2: a block's other conditions hold */
    }
    return v;
}

/* the control is on now (any of its blocks holds) */
static int im_held(const inputmap *m, const char *control, const Uint8 *ks)
{
    int i, k;
    for (i = 0; i < m->nrules; i++) {
        const im_rule *r = &m->rules[i];
        int ok = r->ncond > 0;
        if (strcmp(r->name, control) != 0) continue;
        for (k = 0; k < r->ncond && ok; k++) {
            if (r->c[k].device != 0 && r->c[k].code >= 100 && r->c[k].code < 200) ok = 0;   /* axis blocks: im_axis */
            else if (!im_cond_held(&r->c[k], ks)) ok = 0;
        }
        if (ok) return 1;
    }
    return 0;
}

#endif
