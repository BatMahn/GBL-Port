/* test_titanium: the TITANIUM look's data paths against the GBL Titanium files (make check; skipped without them).
 *   ./test_titanium [TITANIUM_DIR]   (the folder with the GBL Titanium disc's INSTALL\16BIT MW2.PRJ and SKYGND.PAR)
 * Texture decoding (tex.c TEX_TITANIUM: the inverted alpha, the plain records refused), the SKYGND.PAR reader
 * (skygnd.c skygnd_ti_lookup), the folder convention (portcfg.c portcfg_titanium) and the looks (portcfg_texset_ok,
 * portcfg_look, renderer=). */
#include "portcfg.h"
#include "prj.h"
#include "skygnd.h"
#include "tex.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../GBL-game/titanium";
    char prj[1100], par[1100], err[256], cfg[64];
    prj_archive *a;
    texture t;
    skygnd_ti s;
    portcfg pc;
    FILE *f;
    snprintf(prj, sizeof prj, "%s/MW2.PRJ", dir);
    snprintf(par, sizeof par, "%s/SKYGND.PAR", dir);
    if (!(f = fopen(prj, "rb"))) { printf("test_titanium: no %s - skipped\n", prj); return 0; }
    fclose(f);
    if (!(a = prj_open(prj, err, sizeof err))) { printf("FAIL: %s\n", err); return 1; }

    /* every 1.2x record decodes: type 0 opaque; others ARGB4444 with the palette's alpha inverted (a = 255 transparent) */
    {
        int t_cel = prj_find_type(a, "CEL"), k, n = prj_symbol_count(a, t_cel), ok = 0, plain = 0, bad = 0;
        for (k = 0; k < n; k++) {
            prj_record r;
            if (prj_read(a, t_cel, k, &r) != PRJ_OK) continue;
            if (r.size >= 12 && memcmp(r.data, "1.2", 3) == 0) {
                unsigned cnt = (unsigned)(r.data[10] | r.data[11] << 8);
                if (tex_decode(r.data, r.size, TEX_TITANIUM, &t) == 0) {
                    size_t i, np = (size_t)t.w * (size_t)t.h;
                    for (i = 0; i < np && !bad; i++) {
                        unsigned ix = r.data[12 + (size_t)cnt * 4 + i], al = r.data[12 + (size_t)(ix < cnt ? ix : 0) * 4 + 3];
                        unsigned want = t.type == 0 ? 255u : 17u * (15u - (al >> 4));
                        if ((t.rgba[i] >> 24) != want) { bad = 1; printf("  %s texel %zu alpha %u want %u\n", r.name, i, t.rgba[i] >> 24, want); }
                    }
                    ok++;
                    tex_free(&t);
                } else if (cnt > 1) { bad = 1; printf("  %s: a 1.2x record refused\n", r.name); }
            } else {
                plain++;
                CHECK(tex_decode(r.data, r.size, TEX_TITANIUM, &t) == -1, "plain record %s refused (mercsw 0x10002867)", r.name);
            }
            prj_record_free(&r);
        }
        printf("  %d textures decoded, %d plain records\n", ok, plain);
        CHECK(ok > 0 && !bad, "the 1.2x textures decode with the plugin's alpha (%d)", ok);
    }
    prj_close(a);

    /* SKYGND.PAR: bass (BASSSCN1) - fog=200,200,200 density=0,0,1280000 sky=64,64,255 tile_ground=7500;
     * pete (underwater) fog=10,10,80 density=1,1,9000; clai ground fog density=-0.02,0.25,8000 */
    CHECK(skygnd_ti_lookup(par, "BASS", &s) == 0, "bass in SKYGND.PAR");
    CHECK(s.fog[0] == 200 && s.fog[1] == 200 && s.fog[2] == 200 && s.sky[0] == 64 && s.sky[2] == 255, "bass colours");
    CHECK(s.dens_y == 0 && s.dens_m == 0 && s.dens_z == 12800000.0f, "bass density (Z x 10 = %g)", s.dens_z);
    CHECK(fabsf(s.tile_ground - 7500.0f / 128.0f) < 1e-3f && s.ground_max == 320000.0f, "bass ground (ground_max x 2)");
    CHECK(fabsf(s.anim[1] * 181.0f - 0.05f) < 1e-5f, "bass skyAnim");
    CHECK(s.amb[0] == 160 && s.amb[2] == 165 && s.wa[0] == 255 && s.wa[2] == 200 && s.wb[1] == 155, "bass amb / wa / wb");
    CHECK(s.wa_type == 1 && s.wb_type == 1 && s.wa_vec[0] == -1 && s.wa_vec[1] == -1 && s.wa_vec[2] == 0 && s.wb_vec[0] == 1 && s.wb_vec[1] == 1,
          "bass light types and vectors");
    CHECK(skygnd_ti_lookup(par, "PETE", &s) == 0 && s.fog[2] == 80 && s.dens_z == 90000.0f, "pete: blue fog, Z x 10 = 90000 (%g)", s.dens_z);
    CHECK(skygnd_ti_lookup(par, "CLAI", &s) == 0 && fabsf(s.dens_y + 0.02f) < 1e-5f && fabsf(s.dens_m - 0.25f) < 1e-5f, "clai: ground fog");

    /* the folder convention: titanium= in the configuration, or the game folder's titanium/; renderer=titanium */
    snprintf(cfg, sizeof cfg, "/tmp/test_titanium_%d.cfg", (int)getpid());
    if ((f = fopen(cfg, "w"))) { fprintf(f, "game=/nonexistent\ntitanium=%s\nrenderer=titanium\n", dir); fclose(f); }
    setenv("MW2_CONFIG", cfg, 1);
    portcfg_load(NULL, &pc);
    CHECK(portcfg_gbl_look(&pc) == PORTCFG_GBL_TITANIUM && pc.renderer == PORTCFG_ED_ENHANCED && pc.texset == PORTCFG_TX_TITANIUM,
          "renderer=titanium -> ENHANCED + TITANIUM");
    CHECK(portcfg_titanium(&pc, prj, par) == 0 && strstr(prj, "MW2.PRJ") && par[0], "the Titanium files found");
    CHECK(portcfg_gbl_look_ok(&pc, PORTCFG_GBL_TITANIUM) && portcfg_texset_ok(&pc, PORTCFG_ED_ENHANCED, PORTCFG_TX_TITANIUM), "TITANIUM available");
    CHECK(!portcfg_texset_ok(&pc, PORTCFG_ED_DOS, PORTCFG_TX_TITANIUM) && portcfg_texset_ok(&pc, PORTCFG_ED_DOS, PORTCFG_TX_ORIGINAL),
          "the DOS renderer: ORIGINAL only");
    {
        static char m1[PORTCFG_PATH], m2[PORTCFG_PATH], m3[PORTCFG_PATH], m4[PORTCFG_PATH], m5[PORTCFG_PATH];
        CHECK(portcfg_look(&pc, PORTCFG_ED_ENHANCED, PORTCFG_TX_TITANIUM, m1, m2, m3, m4, m5) != 0, "no look without the DOS archive");
        snprintf(pc.install, sizeof pc.install, "%s", dir);   /* (the Titanium folder's MW2.PRJ stands in for the DOS archive) */
        CHECK(portcfg_look(&pc, PORTCFG_ED_ENHANCED, PORTCFG_TX_TITANIUM, m1, m2, m3, m4, m5) == 0 && strstr(m4, "SKYGND.PAR") && !m5[0] &&
              strcmp(m3, "titanium") == 0, "TITANIUM: the Titanium sky file, no fog file, kind titanium (%s %s)", m3, m4);
        CHECK(portcfg_look(&pc, PORTCFG_ED_ENHANCED, PORTCFG_TX_ORIGINAL, m1, m2, m3, m4, m5) == 0 && !m4[0] && strstr(m5, "SKYGND.PAR") &&
              strcmp(m3, "dos") == 0, "ENHANCED: the Titanium fog file, kind dos (%s)", m5);
        pc.install[0] = 0;
    }
    portcfg_save(NULL, &pc);
    {
        char line[256];
        int ren = 0, tex = 0;
        if ((f = fopen(cfg, "r"))) {
            while (fgets(line, sizeof line, f)) { ren += strcmp(line, "renderer=titanium\n") == 0; tex += strncmp(line, "textures=", 9) == 0; }
            fclose(f);
        }
        CHECK(ren == 1 && tex == 0, "saved as renderer=titanium (%d %d)", ren, tex);
    }
    pc.titanium[0] = 0;
    CHECK(portcfg_titanium(&pc, prj, par) != 0, "no folder: unavailable");
    CHECK(!portcfg_gbl_look_ok(&pc, PORTCFG_GBL_TITANIUM) && portcfg_gbl_next(&pc, PORTCFG_GBL_ENHANCED) == PORTCFG_GBL_DOS, "no folder: no TITANIUM look");
    remove(cfg);

    printf("test_titanium: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
