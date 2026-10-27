/* the sim's input map on the GBL install (test_inputmap <install>): GAMEKEY.MAP commands, INPUT.MAP rules, the built-in
 * defaults (their command set must equal GAMEKEY.MAP's; GBL's INPUT.MAP has more rules than the built-in keyboard set,
 * so the rule counts are printed, not compared), the stick and mouse shaping, GBL's drivers' and the gamepad's input
 * names, the gamepad on a virtual SDL game controller */
#include <math.h>
#include <stdlib.h>
#include "inputmap.h"
#include "inputmap_defaults.h"
#include "datapath.h"
#include <sys/stat.h>
#include <unistd.h>
static Uint8 ks[SDL_NUM_SCANCODES];
static void parse_str(inputmap *m, const char *gk, const char *in)
{
    FILE *f;
    memset(m, 0, sizeof *m);
    if (gk) { f = fmemopen((void *)gk, strlen(gk), "r"); im_parse_gamekey(m, f); fclose(f); }
    if (in) { f = fmemopen((void *)in, strlen(in), "r"); im_parse_input(m, f); fclose(f); }
}
static void put(const char *path, const char *text) { FILE *f = fopen(path, "wb"); fputs(text, f); fclose(f); }
#define CHK(x) do { if (!(x)) { printf("FAIL %s\n", #x); fails++; } } while (0)
static int fails;
int main(int argc, char **argv){ inputmap m; char a[1100], b[1100]; snprintf(a, sizeof a, "%s/GAMEKEY.MAP", argc > 1 ? argv[1] : "."); snprintf(b, sizeof b, "%s/INPUT.MAP", argc > 1 ? argv[1] : "."); im_load(&m, a, b); printf("binds %d rules %d\n",m.nbinds,m.nrules);
 CHK(im_action(&m,"NEXT_TARGET",SDLK_t,0)); CHK(!im_action(&m,"NEXT_TARGET",SDLK_t,KMOD_LCTRL)); CHK(im_action(&m,"RESET_TARGETTING",SDLK_t,KMOD_LCTRL));
 CHK(im_action(&m,"ALL_PT_MENU",SDLK_F1,KMOD_RCTRL)); CHK(im_action(&m,"PREV_NAVPOINT",SDLK_n,KMOD_LSHIFT)); CHK(im_action(&m,"EXIT_SIM",SDLK_q,KMOD_LCTRL));
 CHK(im_action(&m,"REVERSE_DIRECTION",SDLK_BACKSPACE,0)); CHK(im_action(&m,"TOGGLE_GROUP_FIRE",SDLK_BACKSLASH,0)); CHK(im_action(&m,"SELF_DESTRUCT",SDLK_x,KMOD_LCTRL|KMOD_LALT));
 ks[SDL_SCANCODE_LEFT]=1; CHK(im_held(&m,"legs_pan_minus",ks)); ks[SDL_SCANCODE_LCTRL]=1; CHK(!im_held(&m,"legs_pan_minus",ks)); CHK(im_held(&m,"pilot_pan_minus",ks)); memset(ks,0,sizeof ks);
 ks[SDL_SCANCODE_KP_4]=1; CHK(im_held(&m,"legs_pan_minus",ks)); memset(ks,0,sizeof ks);
 ks[SDL_SCANCODE_KP_1]=1; CHK(im_held(&m,"torso_pan_minus",ks)); memset(ks,0,sizeof ks);
 ks[SDL_SCANCODE_DELETE]=1; CHK(im_held(&m,"legs_pan_minus",ks) && im_held(&m,"jumpjet_enabled",ks)); memset(ks,0,sizeof ks);
 ks[SDL_SCANCODE_Z]=1; CHK(im_held(&m,"zoom_factor_plus",ks) && !im_held(&m,"zoom_factor_minus",ks)); ks[SDL_SCANCODE_RSHIFT]=1; CHK(im_held(&m,"zoom_factor_minus",ks)); memset(ks,0,sizeof ks);
 ks[SDL_SCANCODE_SEMICOLON]=1; CHK(im_action_held(&m,"FIRE_WEAPON_GROUP",ks));
 { FILE *f = fmemopen((void *)IM_DEFAULT_GAMEKEY, strlen(IM_DEFAULT_GAMEKEY), "r"); inputmap d; memset(&d, 0, sizeof d); im_parse_gamekey(&d, f); fclose(f); f = fmemopen((void *)IM_DEFAULT_INPUT, strlen(IM_DEFAULT_INPUT), "r"); im_parse_input(&d, f); fclose(f); CHK(d.nbinds == m.nbinds); printf("built-in keyboard set: %d control rules (the install's INPUT.MAP: %d)\n", d.nrules, m.nrules); }
 /* the throttle axis (FLTSTCK / MSJSTICK 0x10a00): v / 0.8 rounded up to the next 1/32, no dead zone, full at 80 %; the
  * mouse (MOUSE.DLL 0x10350): the box offset / 512 */
 CHK(im_stick_shape(3, 0.0f) == 0.0f); CHK(fabsf(im_stick_shape(3, 0.4f) - 17.0f / 32.0f) < 1e-6f); CHK(fabsf(im_stick_shape(3, -0.4f) + 17.0f / 32.0f) < 1e-6f);
 CHK(im_stick_shape(3, 0.8f) == 1.0f); CHK(im_stick_shape(3, 1.0f) == 1.0f); CHK(fabsf(im_stick_shape(3, 0.01f) - 1.0f / 32.0f) < 1e-6f);
 { im_cond mc = {-1, 2, 101}; g_im_dev.mouse_y = 256; CHK(fabsf(im_cond_axis(&mc) + 0.5f) < 1e-6f); mc.sign = 1; mc.code = 100; g_im_dev.mouse_x = -512; CHK(im_cond_axis(&mc) == -1.0f); g_im_dev.mouse_x = g_im_dev.mouse_y = 0; }
 /* GBL's drivers and the gamepad (INPUT.MAP lines as the Cockpit Controls write them): their classes and input codes */
 {
   static const char T[] =
     "throttle {\n\t+ avenger\tTz&C\n}\npilot_tilt {\n\t+ avenger\tRx&D\n}\ntorso_tilt {\n\t+ avenger\tRx\n}\njumpjet_enabled {\n\t+ avenger\tJumpJet\n}\n"
     "weapon_fire {\n\t+ wingman\tButton1\n}\nglance_left {\n\t+ vpilot\tRHatLeft\n}\ntorso_pan {\n\t- vfx1\tLeft/Right\n}\n"
     "legs_pan_delta {\n\t+ gamepad\tLeftX\n}\nthrottle_plus {\n\t+ gamepad\tLeftStickUp\n}\nweapon_cycle {\n\t+ gamepad\tRightShoulder\n}\n"
     "weapon_fire {\n\t+ gamepad\tRT\n}\n";
   inputmap g; FILE *f = fmemopen((void *)T, strlen(T), "r"); memset(&g, 0, sizeof g); im_parse_input(&g, f); fclose(f);
   CHK(g.nrules == 11);
   CHK(g.rules[0].c[0].device == 1 && g.rules[0].c[0].code == 102 && g.rules[0].c[0].gate == 3 && g.rules[0].c[0].raw);
   CHK(g.rules[1].c[0].code == 103 && g.rules[1].c[0].gate == 4); CHK(g.rules[2].c[0].code == 103 && g.rules[2].c[0].gate == -4);
   CHK(g.rules[3].c[0].code == 302); CHK(g.rules[4].c[0].device == 1 && g.rules[4].c[0].code == 1);
   CHK(g.rules[5].c[0].code == 207); CHK(g.rules[6].c[0].device == 3);
   CHK(g.rules[7].c[0].device == 4 && g.rules[7].c[0].code == 100); CHK(g.rules[8].c[0].code == 302); CHK(g.rules[9].c[0].code == 10);
   CHK(g.rules[10].c[0].code == 16); CHK(im_uses(&g, 4) && im_uses(&g, 1));
#if SDL_VERSION_ATLEAST(2, 0, 14)
   /* a virtual game controller: the left stick right, the right trigger in, the right shoulder held */
   if (SDL_Init(SDL_INIT_GAMECONTROLLER) == 0) {
     int vi = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
     SDL_Joystick *vj = vi >= 0 ? SDL_JoystickOpen(vi) : NULL;
     g_im_dev.pad = vi >= 0 && SDL_IsGameController(vi) ? SDL_GameControllerOpen(vi) : NULL;
     if (vj && g_im_dev.pad) {
       int pr = 0; float a;
       SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTX, 32767); SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTY, -32768);
       SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
       SDL_GameControllerUpdate();
       a = im_axis(&g, "legs_pan_delta", ks, 4, &pr); CHK(pr == 2 && fabsf(a - 1.0f) < 1e-3f);
       CHK(im_held(&g, "throttle_plus", ks)); CHK(im_held(&g, "weapon_cycle", ks)); CHK(im_held(&g, "weapon_fire", ks));
       SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTX, 3000); SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
       SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0); SDL_GameControllerUpdate();
       a = im_axis(&g, "legs_pan_delta", ks, 4, &pr); CHK(a == 0.0f);   /* inside the dead zone */
       CHK(!im_held(&g, "weapon_fire", ks) && !im_held(&g, "weapon_cycle", ks));
       printf("virtual gamepad: checked\n");
     } else printf("virtual gamepad: not available - skipped\n");
     if (g_im_dev.pad) SDL_GameControllerClose(g_im_dev.pad);
     g_im_dev.pad = NULL;
     if (vj) SDL_JoystickClose(vj);
     /* a virtual six-axis stick with two hats: the Spaceball's gated axes, the yoke's right hat, the wingman's buttons */
     vi = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_FLIGHT_STICK, 6, 6, 2);
     g_im_dev.joy = vi >= 0 ? SDL_JoystickOpen(vi) : NULL;
     if (g_im_dev.joy) {
       int pr = 0;
       SDL_JoystickSetVirtualAxis(g_im_dev.joy, 2, 32767); SDL_JoystickSetVirtualAxis(g_im_dev.joy, 3, -32768);
       SDL_JoystickSetVirtualHat(g_im_dev.joy, 1, SDL_HAT_LEFT); SDL_JoystickSetVirtualButton(g_im_dev.joy, 1, 1);
       SDL_JoystickUpdate();
       CHK(im_axis(&g, "throttle", ks, 1, &pr) == 0.0f);                       /* Tz&C: C (button 3) not held */
       CHK(fabsf(im_axis(&g, "torso_tilt", ks, 1, &pr) + 1.0f) < 1e-3f);      /* Rx: D not held */
       CHK(im_axis(&g, "pilot_tilt", ks, 1, &pr) == 0.0f);
       SDL_JoystickSetVirtualButton(g_im_dev.joy, 2, 1); SDL_JoystickSetVirtualButton(g_im_dev.joy, 3, 1); SDL_JoystickUpdate();
       CHK(fabsf(im_axis(&g, "throttle", ks, 1, &pr) - 1.0f) < 1e-3f && fabsf(im_axis(&g, "pilot_tilt", ks, 1, &pr) + 1.0f) < 1e-3f);
       CHK(im_axis(&g, "torso_tilt", ks, 1, &pr) == 0.0f);
       CHK(im_held(&g, "glance_left", ks)); CHK(im_held(&g, "weapon_fire", ks));   /* vpilot RHatLeft (hat 1), wingman Button1 (button 1) */
       printf("virtual stick: checked\n");
       SDL_JoystickClose(g_im_dev.joy);
       g_im_dev.joy = NULL;
     }
     SDL_Quit();
   }
#endif
 }
 {   /* the original's GAMEKEY.MAP reading (GBL MW2.EXE 0x49060): a modern editor's file - byte order mark, CR LF, tabs, comments
      * after the words, lower-case commands and keys, modifiers in any order; one command per key (the first line wins);
      * a command with two keys; Space reads as s; an unknown command ends the reading */
   inputmap g;
   parse_str(&g, "\xef\xbb\xbfmain_menu\tesc\r\nNext_Target  T   # comment\r\nPREV_TARGET t\r\nRESET_TARGETTING t+Ctrl\r\n"
                 "\r\n   \t\r\nTOGGLE_HUD F11\r\nTOGGLE_HUD Tab\r\nFIRE_WEAPON_GROUP Space\r\nEXIT_SIM ctrl+q#x\r\nPAUSE_GAME Plus\r\n"
                 "TOGGLE_MASC Insert\r\nMFD_CYCLE Enter\r\nTOGGLE_AMS ctrl+a\r\nNOT_A_COMMAND k\r\nJETTISON_AMMO j\r\n", NULL);
   CHK(im_action(&g, "MAIN_MENU", SDLK_ESCAPE, 0)); CHK(im_action(&g, "NEXT_TARGET", SDLK_t, 0)); CHK(!im_action(&g, "PREV_TARGET", SDLK_t, 0));
   CHK(im_action(&g, "RESET_TARGETTING", SDLK_t, KMOD_LCTRL)); CHK(im_action(&g, "TOGGLE_HUD", SDLK_F11, 0)); CHK(im_action(&g, "TOGGLE_HUD", SDLK_TAB, 0));
   CHK(im_action(&g, "FIRE_WEAPON_GROUP", SDLK_s, 0)); CHK(im_action(&g, "EXIT_SIM", SDLK_q, KMOD_RCTRL)); CHK(im_action(&g, "PAUSE_GAME", SDLK_KP_PLUS, 0));
   CHK(im_action(&g, "TOGGLE_MASC", SDLK_INSERT, 0)); CHK(im_action(&g, "MFD_CYCLE", SDLK_RETURN, 0)); CHK(im_action(&g, "TOGGLE_AMS", SDLK_a, KMOD_LCTRL));   /* GBL's command */
   CHK(!im_action(&g, "JETTISON_AMMO", SDLK_j, 0));   /* after the unknown command: not read */
   CHK(g.nbinds == 11);
 }
 {   /* INPUT.MAP: KEYBOARD.DLL's names (LeftControl, One, a number = the list entry), any case of the control, CR LF and
      * a byte order mark, comments, up to 8 conditions; an unknown control ends the reading */
   inputmap g;
   parse_str(&g, NULL, "\xef\xbb\xbfWEAPON_FIRE {\r\n\t+ keyboard\tOne\r\n\t- keyboard\tLeftControl  # Just because!\r\n}\r\n"
                       "throttle_plus {\n + keyboard 16\n}\nlegs_pan_minus {\n+ keyboard a\n+ keyboard LeftShift\n- keyboard RightShift\n"
                       "- keyboard Control\n- keyboard Alt\n- keyboard CAPSLock\n- keyboard BackQuote\n- keyboard Esc\n}\n"
                       "bogus_control {\n+ keyboard x\n}\nlegs_pan_plus {\n+ keyboard d\n}\n");
   memset(ks, 0, sizeof ks); ks[SDL_SCANCODE_1] = 1; CHK(im_held(&g, "weapon_fire", ks)); ks[SDL_SCANCODE_LCTRL] = 1; CHK(!im_held(&g, "weapon_fire", ks));
   memset(ks, 0, sizeof ks); ks[SDL_SCANCODE_W] = 1; CHK(im_held(&g, "throttle_plus", ks));
   memset(ks, 0, sizeof ks); ks[SDL_SCANCODE_A] = 1; ks[SDL_SCANCODE_LSHIFT] = 1; CHK(im_held(&g, "legs_pan_minus", ks)); CHK(g.rules[2].ncond == 8);
   ks[SDL_SCANCODE_RSHIFT] = 1; CHK(!im_held(&g, "legs_pan_minus", ks));
   CHK(g.nrules == 3);   /* bogus_control and what follows: not read */
   memset(ks, 0, sizeof ks);
 }
 {   /* a WASD set: GAMEKEY.MAP's w / a / s moved off (a key does only what its line says), INPUT.MAP drives with W A S D */
   inputmap g;
   const Uint8 *k2 = ks;
   parse_str(&g, "TOGGLE_AUTOPILOT p\nSHUTDOWN_MECH ctrl+d\nSTARTUP_MECH CTRL+s\nENHANCED_VISION v\nTOGGLE_MASC ctrl+v\n",
             "throttle_plus {\n+ keyboard W\n}\nthrottle_minus {\n+ keyboard S\n}\nlegs_pan_minus {\n+ keyboard A\n}\nlegs_pan_plus {\n+ keyboard D\n}\n"
             "torso_pan {\n+ mouse Left/Right\n}\nweapon_fire {\n+ mouse LeftBtn\n}\n");
   CHK(!im_action(&g, "TOGGLE_AUTOPILOT", SDLK_a, 0)); CHK(!im_action(&g, "SHUTDOWN_MECH", SDLK_s, 0)); CHK(!im_action(&g, "ENHANCED_VISION", SDLK_w, 0));
   CHK(im_action(&g, "SHUTDOWN_MECH", SDLK_d, KMOD_LCTRL)); CHK(im_action(&g, "ENHANCED_VISION", SDLK_v, 0));
   ks[SDL_SCANCODE_W] = 1; CHK(im_held(&g, "throttle_plus", k2) && !im_held(&g, "throttle_minus", k2)); memset(ks, 0, sizeof ks);
   ks[SDL_SCANCODE_D] = 1; CHK(im_held(&g, "legs_pan_plus", k2)); memset(ks, 0, sizeof ks);
   CHK(im_uses(&g, 2));
 }
 {   /* the files as a player leaves them: lower-case names on a case-sensitive disk, read-only - found and read through the
      * game's file search (datapath: the install folder first, then the disc's) */
   char d[256], cd[300], sub[320], p1[400], p2[400], o1[2048], o2[2048];
   inputmap g;
   snprintf(d, sizeof d, "/tmp/test_inputmap_%d", (int)getpid());
   snprintf(cd, sizeof cd, "%s/cd", d); snprintf(sub, sizeof sub, "%s/MECH2", cd);
   mkdir(d, 0755); mkdir(cd, 0755); mkdir(sub, 0755);
   snprintf(p1, sizeof p1, "%s/gamekey.map", d); put(p1, "TOGGLE_AUTOPILOT p\n"); chmod(p1, 0444);
   snprintf(p2, sizeof p2, "%s/Input.Map", sub); put(p2, "throttle_plus {\n+ keyboard W\n}\n"); chmod(p2, 0444);
   dp_clear_roots(); dp_add_root(d); dp_add_root(sub); dp_add_root(cd);
   CHK(dp_resolve_in(d, "GAMEKEY.MAP", o1, sizeof o1) == 0 && strcmp(o1, p1) == 0);
   CHK(dp_resolve_in(d, "INPUT.MAP", o2, sizeof o2) != 0 && dp_resolve("INPUT.MAP", o2, sizeof o2) == 0 && strcmp(o2, p2) == 0);
   im_load(&g, o1, o2);
   CHK(g.have_gamekey && g.have_input && g.nbinds == 1 && g.nrules == 1 && im_action(&g, "TOGGLE_AUTOPILOT", SDLK_p, 0));
   ks[SDL_SCANCODE_W] = 1; CHK(im_held(&g, "throttle_plus", ks)); memset(ks, 0, sizeof ks);
   chmod(p1, 0644); chmod(p2, 0644); remove(p1); remove(p2); rmdir(sub); rmdir(cd); rmdir(d);
 }
 printf("inputmap: %d bindings, %d control rules, %s\n", m.nbinds, m.nrules, fails ? "FAILED" : "all checks passed"); return fails != 0;}
