/*
 * satmap.h - the satellite map (F3, radar mode 4) as GBL MW2.EXE draws it: the DOS renderer's object walk and painter
 * with the map's own hooks (0x11910 with mode 4: the view 0x12c30, the object test 0x124a0 -> 0x12e50, the straight-down
 * projection 0x13000, the colour 0x12520, the polygon draw 0x12690), into an 8-bit picture of the window, and its
 * field-of-view wedge (0x120c0 with the rim 0x14200 / 0x14350, the line 0x54fa9). See satmap.c.
 */
#ifndef GBL_SATMAP_H
#define GBL_SATMAP_H

#include <stdint.h>
#include "mech3d.h"

/* an object's kind for the map's colour (0x12520 by the object's type word: obj +2 & 0xf00, its class & 0xf0) */
enum { SATMAP_WORLD = 0, SATMAP_UNIT = 0x100, SATMAP_THING = 0x200, SATMAP_SHOT = 0x400 };

typedef struct satmap satmap;

satmap *satmap_new(void);
void    satmap_free(satmap *s);
/* the view for this frame (0x12c30): the window w x h (pixels), the map's width in pixels mw (the window's width in the
 * original; the 4:3 part of a wider window), the camera straight down over (px, pz) at the descriptor's range (cm, also
 * the camera's height), zoom = the main view's zoom (16.16, 1.0 = 0x10000), lodq = DISPLAY DETAIL's quality (1 / 2),
 * hmin / hrange = the planet's lowest height less 10 m and the height range (DAT_000a59e0 / 59e8, the PLNT chunk) */
void    satmap_view(satmap *s, int w, int h, int mw, int32_t px, int32_t pz, int32_t range, uint32_t zoom, int lodq,
                    int32_t hmin, int32_t hrange);
/* one object (a part) into the walk: kind SATMAP_*, side 0 friendly / 1 enemy / 2 neutral (units and things); link: its
 * place in the object list (the larger walked first, as glr's draw list); its model's level of detail by the camera */
void    satmap_part(satmap *s, const mech3d_part *p, int kind, int side, int64_t link);
/* sort and paint everything given since satmap_view into pix (w x h palette indices): the window filled with the ground
 * colour first (0x57978) */
void    satmap_paint(satmap *s, uint8_t *pix, int ground);
/* the field-of-view wedge (0x120c0): two lines in colour col from the window's centre to its edge at the bearing
 * (heading + twist, 16.16 degrees) +- atan(1 / zoom) */
void    satmap_wedge(const satmap *s, uint8_t *pix, int32_t bearing, uint32_t zoom, int col);
/* 0x54fa9: a line's pixels in colour col, clipped to the w x h picture */
void    satmap_line(uint8_t *pix, int w, int h, int x0, int y0, int x1, int y1, int col);
/* a world point (game cm) -> the map's pixel (0x13140); 1 below the camera and inside the window */
int     satmap_point(const satmap *s, const float p[3], int *sx, int *sy);
/* 0x14130 (+0x84): the window's edge point toward the window pixel (px, py) from the centre (a target off the map) */
void    satmap_edge(const satmap *s, int px, int py, int *ex, int *ey);
/* counts of the last paint (tests): objects walked, listed polygons */
void    satmap_stats(const satmap *s, int out[2]);

#endif
