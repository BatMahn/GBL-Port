/* tex.c - see tex.h. */
#include "tex.h"

#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t pack(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}
static unsigned x5(unsigned v) { return (v << 3) | (v >> 2); }
static unsigned x4(unsigned v) { return v * 17u; }

int tex_decode(const uint8_t *d, size_t len, int enc, texture *t)
{
    size_t n, i;

    memset(t, 0, sizeof *t);
    if (enc == TEX_DOS) {
        if (len < 4) return -1;
        t->w = rd16(d); t->h = rd16(d + 2);
        n = (size_t)t->w * (size_t)t->h;
        if (!n || n > len - 4) return -1;
        if (!(t->rgba = malloc(n * sizeof *t->rgba))) return -1;
        for (i = 0; i < n; i++) t->rgba[i] = pack(d[4 + i], d[4 + i], d[4 + i], d[4 + i] == 0xff ? 0 : 255);
        return 0;
    }
    if (enc == TEX_TITANIUM) {   /* see tex.h */
        unsigned type, cnt;
        if (len < 12 || d[0] != '1' || d[1] != '.' || d[2] != '2') return -1;
        type = rd16(d + 4); t->w = rd16(d + 6); t->h = rd16(d + 8); cnt = rd16(d + 10);
        t->type = (int)type;
        n = (size_t)t->w * (size_t)t->h;
        if (!n || cnt > 256 || 12 + (size_t)cnt * 4 > len || n > len - 12 - (size_t)cnt * 4) return -1;
        /* a one-colour record (45 of them, type 0: the last frames of four explosion sequences - A9EXP222-232,
         * A9EXP622-632, A9EXBR28-32, A9EXPF15-32 - and MCC1SKY) is taken for a placeholder (INFERRED: shortened
         * sequences); the 3D editions' effect tables, which the port plays, run to those frames: they count as missing */
        if (cnt <= 1) return -1;
        if (!(t->rgba = malloc(n * sizeof *t->rgba))) return -1;
        for (i = 0; i < n; i++) {
            unsigned ix = d[12 + (size_t)cnt * 4 + i];
            const uint8_t *e = d + 12 + (size_t)(ix < cnt ? ix : 0) * 4;   /* (every index is below n in the archive) */
            if (!cnt) t->rgba[i] = 0;
            else if (type == 0)   /* format 0: R >> 3, G >> 2, B >> 3 (the dword >> 19, >> 10, >> 3; 0x1000634f) */
                t->rgba[i] = pack(x5(e[2] >> 3), (unsigned)((e[1] >> 2) << 2 | (e[1] >> 6)), x5(e[0] >> 3), 255);
            else                  /* format 1: each channel's top 4 bits, alpha inverted */
                t->rgba[i] = pack(x4(e[2] >> 4), x4(e[1] >> 4), x4(e[0] >> 4), x4(15u - (e[3] >> 4)));
        }
        return 0;
    }
    /* TEX_4444: u16 w, u16 h, w x h ARGB4444 */
    if (len < 4) return -1;
    t->w = rd16(d);
    t->h = rd16(d + 2);
    n = (size_t)t->w * (size_t)t->h;
    if (!n || n * 2 > len - 4) return -1;
    t->rgba = malloc(n * sizeof *t->rgba);
    if (!t->rgba) return -1;
    for (i = 0; i < n; i++) {
        unsigned p = rd16(d + 4 + i * 2);
        t->rgba[i] = pack(x4((p >> 8) & 15), x4((p >> 4) & 15), x4(p & 15), x4((p >> 12) & 15));
    }
    return 0;
}

void tex_free(texture *t)
{
    free(t->rgba);
    t->rgba = NULL;
}
