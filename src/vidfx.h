/* The full-screen videos' looks (portcfg videos=): a 320x200 palette-indexed Smacker frame drawn into an RGB24 picture
 * of any size (the window's real pixels), on the CPU.
 *
 * What the originals did (MW2SHELL.EXE: GBL FUN_0003c430 0x3c53d, base MW2 FUN_00038c70 0x38d74): the clip is opened
 * with only its audio-track flags (0xfe00, 0x3a1ec - no SMACKYINTERLACE / SMACKYDOUBLE; the files' own header flags
 * are 0), the video mode is switched to the first table mode that holds 320 x 200 (FUN_0004e4c8(200, 320): table
 * 0x87d74 / 0x87d88 = 320x200, 640x480, ...; entry 0 is BIOS mode 0x13, 0x4e284) and every frame goes 1:1 to the VGA
 * memory at (0, 0) (FUN_0005050c = SmackToScreen(smk, 0, 0, pitch 320, ...)). So DOS showed a plain mode-13h picture:
 * the VGA double-scans its 200 lines (each line drawn twice, 400 lines, no black lines in between) and the monitor
 * stretches them to its 4:3 tube. DOSBox capture (normal2x): 640x400, every row pair and column pair identical.
 *
 *   ORIGINAL   that picture: each source pixel a solid block, the 320x200 frame filling the 4:3 area (nearest)
 *   SMOOTH     filtered scaling (a separable Mitchell-Netravali cubic: smooth, no blocks, no ringing)
 *   SCANLINES  each source line a sharp bright band with dark gaps between them (the "interlaced" look)
 *   CRT        scanlines whose beam widens with brightness, a soft glow and an aperture-grille mask
 *   CRT_CURVED the CRT look on a slightly curved tube (rounded edges, corners dark)
 * The values' order is portcfg's PORTCFG_VID_*. */
#ifndef VIDFX_H
#define VIDFX_H
#include <stdint.h>
enum { VIDFX_ORIGINAL, VIDFX_SMOOTH, VIDFX_SCANLINES, VIDFX_CRT, VIDFX_CRT_CURVED, VIDFX_COUNT };
typedef struct vidfx vidfx;
vidfx *vidfx_new(void);
void   vidfx_free(vidfx *v);
/* Draw the frame (sw x sh indices, 256 RGB palette) into out: ow x oh RGB24, pitch bytes per row. */
void   vidfx_render(vidfx *v, int mode, const uint8_t *pix, int sw, int sh, const uint8_t (*pal)[3],
                    uint8_t *out, int ow, int oh, int pitch);
#endif
