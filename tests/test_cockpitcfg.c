/* The Cockpit Controls on the GBL install (test_cockpitcfg <install> [fixtures]): for each selection of GIDDI drivers
 * the original shell was run with in DOSBox (GBL MW2SHELL.EXE, Cockpit Controls from the disc's CONFIG00.CPC: the
 * drivers toggled, ACCEPT; <COCKPIT_REF>/<name>.MAP / .CPC (not in the repo: files the original wrote) are the INPUT.MAP and GIDDI\CONFIG00.CPC), the
 * port's merge + INPUT.MAP + CONFIG00.CPC must be the same bytes - with and without the port's gamepad driver listed
 * (not selected). Also: the selection a configuration gives (the disc's CONFIG00: keyboard + mouse), the 4-driver
 * limit, a CPC round trip, the gamepad's defaults (written in the same format, CONFIG00 lists it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cockpitcfg.h"

static int fails;
#define CHK(x, ...) do { if (!(x)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static long slurp(const char *p, char *buf, long cap)
{
    FILE *f = fopen(p, "rb");
    long n;
    if (!f) return -1;
    n = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static const struct { const char *name, *sel[4]; } RUN[] = {
        {"avenger", {"keyboard", "avenger"}}, {"vfx1", {"keyboard", "vfx1"}}, {"vpilot", {"keyboard", "vpilot"}},
        {"wingman", {"keyboard", "wingman"}}, {"vio1", {"keyboard", "vio1"}}, {"vio2", {"keyboard", "vio2"}},
        {"fltstck_mouse_wingman", {"keyboard", "fltstck", "mouse", "wingman"}}, {"mouse", {"keyboard", "mouse"}},
        {"joystick_msjstick", {"keyboard", "joystick", "msjstick"}}, {"mouse_tmaster_vpilot", {"keyboard", "mouse", "tmaster", "vpilot"}}};
    static char out[65536], ref[65536];
    static cc_config cfg, back;
    static cc_drivers dv;
    char giddi[1100], p[1200];
    const char *dir = argc > 1 ? argv[1] : ".", *fx = argc > 2 ? argv[2] : (getenv("COCKPIT_REF") ? getenv("COCKPIT_REF") : "cockpit-ref");
    int g, i, k, sel[CC_MAXDEV], nrun = 0;
    size_t n = 0;
    snprintf(giddi, sizeof giddi, "%s/GIDDI", dir);
    for (g = 0; g < 2; g++) {
        cc_drivers_load(&dv, giddi, g);
        if (dv.n < 12 + g || cc_find(&dv, "keyboard") < 0) { printf("cockpitcfg: no GBL GIDDI drivers in %s\n", giddi); return 1; }
        if (g == 0) {   /* the disc's CONFIG00.CPC: Default Config, keyboard + mouse selected (A_cockpit.png) */
            snprintf(p, sizeof p, "%s/CONFIG00.CPC", giddi);
            CHK(cc_load(&cfg, p, &dv) == 0, "load CONFIG00.CPC");
            cc_select_from(&cfg, &dv, sel);
            for (k = 0; k < dv.n; k++) CHK(sel[k] == (!strcmp(dv.d[k].shortname, "keyboard") || !strcmp(dv.d[k].shortname, "mouse")), "selection %s", dv.d[k].shortname);
            CHK(!strcmp(cfg.name, "Default Config"), "config name %s", cfg.name);
        }
        for (i = 0; i < (int)(sizeof RUN / sizeof RUN[0]); i++) {
            long rn;
            memset(sel, 0, sizeof sel);
            for (k = 0; k < 4 && RUN[i].sel[k]; k++) sel[cc_find(&dv, RUN[i].sel[k])] = 1;
            CHK(cc_merge_defaults(&cfg, giddi, &dv, sel) == 0, "merge %s", RUN[i].name);
            n = cc_generate(&cfg, &dv, out, sizeof out);
            snprintf(p, sizeof p, "%s/%s.MAP", fx, RUN[i].name);
            if ((rn = slurp(p, ref, sizeof ref)) < 0) {   /* the original shell's captures are game-derived: kept outside the repo */
                if (i == 0 && g == 0) printf("cockpitcfg: no captures of the original shell in %s - comparisons skipped\n", fx);
                break;
            }
            CHK(n == (size_t)rn && memcmp(out, ref, n) == 0, "INPUT.MAP %s (gamepad listed %d): %zu bytes, original %ld", RUN[i].name, g, n, rn);
            if (n != (size_t)rn || memcmp(out, ref, n)) { FILE *f = fopen("/tmp/cc_fail.map", "wb"); if (f) { fwrite(out, 1, n, f); fclose(f); } }
            CHK(cc_save(&cfg, "/tmp/cc_test.cpc", &dv) == 0, "save");
            snprintf(p, sizeof p, "%s/%s.CPC", fx, RUN[i].name);
            rn = slurp(p, ref, sizeof ref);
            CHK(rn == 3872 && slurp("/tmp/cc_test.cpc", out, sizeof out) == 3872 && memcmp(out, ref, 3872) == 0, "CONFIG00.CPC %s (gamepad listed %d)", RUN[i].name, g);
            nrun++;
        }
    }
    /* the 4-driver limit: a configuration using five drivers selects the keyboard and the first three others */
    cc_clear(&cfg);
    for (k = 0, i = 0; k < dv.n && i < 5; k++) if (strcmp(dv.d[k].shortname, "keyboard")) { cfg.b[0][17 + i].device = k; cfg.b[0][17 + i].a = 0; i++; }
    cc_select_from(&cfg, &dv, sel);
    for (k = 0, i = 0; k < dv.n; k++) i += sel[k];
    CHK(i == 4 && sel[cc_find(&dv, "keyboard")] && sel[0] && sel[1] && sel[2], "4-driver limit (%d)", i);
    /* round trip */
    snprintf(p, sizeof p, "%s/DEFAULT.CPC", giddi);
    CHK(cc_load(&cfg, p, &dv) == 0 && cc_save(&cfg, "/tmp/cc_rt.cpc", &dv) == 0 && cc_load(&back, "/tmp/cc_rt.cpc", &dv) == 0 && memcmp(cfg.b, back.b, sizeof cfg.b) == 0
        && memcmp(cfg.name, back.name, sizeof cfg.name) == 0, "CPC round trip");
    /* the gamepad (port addition): its defaults with the keyboard */
    memset(sel, 0, sizeof sel);
    sel[cc_find(&dv, "keyboard")] = 1; sel[cc_find(&dv, CC_GAMEPAD)] = 1;
    CHK(cc_merge_defaults(&cfg, giddi, &dv, sel) == 0, "merge gamepad");
    n = cc_generate(&cfg, &dv, out, sizeof out);
    CHK(n && strstr(out, "legs_pan_delta {\r\n\t+ gamepad\tLeftX\r\n}") && strstr(out, "weapon_fire {\r\n\t+ gamepad\tRT\r\n}")
        && strstr(out, "throttle_plus {\r\n\t+ gamepad\tLeftStickUp\r\n}") && strstr(out, "torso_tilt {\r\n\t+ gamepad\tRightY\r\n}"), "gamepad INPUT.MAP");
    CHK(cc_save(&cfg, "/tmp/cc_pad.cpc", &dv) == 0 && cc_load(&back, "/tmp/cc_pad.cpc", &dv) == 0 && back.b[2][1].device == cc_find(&dv, CC_GAMEPAD)
        && back.b[0][17].device == cc_find(&dv, "keyboard") && back.b[2][17].device == cc_find(&dv, CC_GAMEPAD), "gamepad CPC (each binding in the first free level)");
    printf("cockpitcfg: %d drivers (+ gamepad), %d keyboard inputs, %d original runs compared, %s\n", dv.n - 1,
           dv.d[cc_find(&dv, "keyboard")].nbutton, nrun, fails ? "FAILED" : "all checks passed");
    return fails != 0;
}
