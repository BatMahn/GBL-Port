/* The Cockpit Controls writer on the GBL install (test_cockpitcfg <install>): the keyboard's defaults (GIDDI\KEYBOARD.CPC)
 * give a generated INPUT.MAP; the Default merge of several devices; a CPC load / save round trip must be exact. (GBL ships
 * a hand-written INPUT.MAP, not one the shell generated, so the generated file is not compared with it.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cockpitcfg.h"
int main(int argc, char **argv)
{
    cc_device dev[CC_DEVICES];
    cc_config cfg, back;
    int sel[CC_DEVICES] = {0}, fails = 0;
    static char out[65536];
    char p[1100];
    size_t n;
    const char *dir = argc > 1 ? argv[1] : ".";
    snprintf(p, sizeof p, "%s/GIDDI", dir);
    cc_devices(dev, p);
    sel[2] = 1;
    if (cc_merge_defaults(&cfg, p, sel) != 0) { printf("cockpitcfg: no GIDDI defaults in %s\n", p); return 1; }
    n = cc_generate(&cfg, dev, sel, out, sizeof out);
    if (!n || strncmp(out, "# mw2shell CockPit Config generated map file\r\n", 46) != 0) { printf("FAIL generated INPUT.MAP (%zu bytes)\n", n); fails++; }
    {   /* the shell's Default (FUN_00020c10) with the joystick and the mouse: each device's own bindings, level for level
         * into the first free level from there - torso_tilt (slot 3) from both: the joystick's at level 0, the mouse's
         * at level 1; the jump-turn keys written whatever is selected (FUN_000201a0) */
        int s2[CC_DEVICES] = {0};
        s2[1] = 1; s2[3] = 1;
        snprintf(p, sizeof p, "%s/GIDDI", dir);
        if (cc_merge_defaults(&cfg, p, s2) != 0) { printf("FAIL merge joystick + mouse\n"); fails++; }
        else {
            if (cfg.b[0][3].device != 1 || cfg.b[1][3].device != 3 || cfg.b[0][2].device != 3 || cfg.b[0][1].device != 1 || cfg.b[2][3].device != -1) {
                printf("FAIL merge levels: tilt %d / %d / %d, pan %d, legs %d\n", cfg.b[0][3].device, cfg.b[1][3].device, cfg.b[2][3].device, cfg.b[0][2].device, cfg.b[0][1].device); fails++;
            }
            size_t n2 = cc_generate(&cfg, dev, s2, out, sizeof out);
            if (!n2 || !strstr(out, "legs_pan_minus {\r\n\t+ keyboard\tGreyDelete")) { printf("FAIL jump-turn keys without the keyboard\n"); fails++; }
        }
    }
    snprintf(p, sizeof p, "%s/GIDDI/DEFAULT.CPC", dir);
    if (cc_load(&cfg, p) != 0 || cc_save(&cfg, "/tmp/cc_rt.cpc") != 0 || cc_load(&back, "/tmp/cc_rt.cpc") != 0 || memcmp(&cfg, &back, sizeof cfg) != 0) { printf("FAIL CPC round trip\n"); fails++; }
    printf("cockpitcfg: %d keyboard inputs, INPUT.MAP %zu bytes, %s\n", dev[2].nbutton, n, fails ? "FAILED" : "all checks passed");
    return fails != 0;
}
