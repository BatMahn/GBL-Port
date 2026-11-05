# The OpenGL 1.2 renderer (`RB=gl12`)

The legacy build's renderer: `src/rb.h` on OpenGL 1.2's fixed-function pipeline, for the graphics cards of the Windows
XP years. `src/rb_gl12.c` (context, textures, buffers, off-screen targets, state, batching), `src/rb_gl12_world.c`
(the world and HUD programs), `src/rb_gl12.h` (its internal state). Chosen when building: `make gbl RB=gl12` (the legacy
Windows build adds `SDL=2`, README "Build"). It draws at once, call by call; nothing is recorded.

The SDL_GPU backend runs the shaders in `shaders/`; this one carries out **the same shaders in C** (the vertex stage
per vertex, the fragment stage as fixed-function classes and CPU span walks). It matches the shaders' output, not some
idea of its own: **a change to `shaders/world.vert`, `world.frag`, `world_common.glsl` or `hud.frag` needs the same
change in `src/rb_gl12_world.c`**, or the two builds drift apart.

## What it needs from the driver

- **OpenGL 1.2** - or **1.1 with `GL_EXT_texture_edge_clamp` / `GL_SGIS_texture_edge_clamp`** (the same
  `GL_CLAMP_TO_EDGE`). Anything less is refused with the reason (`rb_init`, the game's message box).
- **`GL_ARB_multitexture`, optional**: two units give the fog and the HUD flash in the same pass; with one unit they are
  a second pass.
- Only OpenGL **1.1** entry points are linked (on Windows `opengl32.dll` has them all); `glActiveTextureARB` and
  `glClientActiveTextureARB` are looked up. No buffer objects, framebuffer objects, shaders, non-power-of-two or float
  textures. The 40 GL 1.1 calls: `glAlphaFunc glBindTexture glBlendFunc glClear glClearColor glClearDepth glColorPointer
  glCopyTexSubImage2D glDeleteTextures glDepthFunc glDepthMask glDepthRange glDisable glDisableClientState glDrawArrays
  glEnable glEnableClientState glFinish glGenTextures glGetError glGetIntegerv glGetString glHint glLineWidth
  glLoadIdentity glMatrixMode glPixelStorei glPolygonOffset glPolygonStipple glReadPixels glScissor glShadeModel
  glTexCoordPointer glTexEnvfv glTexEnvi glTexImage1D glTexImage2D glTexParameteri glVertexPointer glViewport` + the two
  ARB ones (`glGetError` only for `MW2_GL_CHECK`).
- **The context**: SDL's default (no version asked: a legacy context, whatever 1.x the driver has), double-buffered,
  8-8-8 colour with a 24-bit depth buffer, else 5-6-5 with 16 (`rb_window_flags(low)`, tried in that order by
  `tools/glview.c`), swap interval 1 (0 on SDL's offscreen driver). The largest texture is the driver's
  `GL_MAX_TEXTURE_SIZE`.

## How it works

- **Vertex stage on the CPU**: `shaders/world.vert` in C (`vertex_stage`): the billboards' screen squares, the cockpit
  set's order, the view-distance cull, and for the DOS look the exact fill's 2 px push-out at the DOS integer points,
  the cockpit polygons' near-plane box, the projection check, the fog key, the light intensity (GBL MW2.EXE 0x3eb36),
  the one-sided test once a polygon and the DOS draw order's key depth; for the TITANIUM look its back-face test and its
  draw order's key depth. Its per-polygon results are kept while one polygon's vertices follow each other. The
  clip-space positions go to GL with identity matrices, so GL clips and rasterizes as the GPU does; the depth stays
  OpenGL's (-w..w, depth range 0..1). The matrix products are fused multiply-adds in the order the SDL_GPU reference
  (Mesa's lavapipe) computes them: the DOS integer projection decides whole pixels on those values.
- **The draw orders** (DOS and TITANIUM: world.frag's `gl_FragDepth = v_dord`): a listed polygon's triangles are
  clipped at the near plane here (by their own depth) and then sent with z = key x w - every pixel at the key's depth.
- **The DOS look's exact fill** (`u_dos_exact`, on by default; `MW2_DOS_EXACT=0` turns it off as on the GPU): world.frag
  walks each pixel's polygon in the storage buffer - the integer projection (0x3dfb7), the two chains (0x59e77), the
  row's span from the left chain's x to the right's inclusive - and discards the pixels outside it. Here the same walk
  runs on the CPU per triangle and row: the triangle's GL coverage (as the vertex stage placed it) cut to the DOS span,
  sent as one-row quads at the polygon's depth:
  - flat colours (types 0 / 1) and the Gouraud dither (type 4: the shade stepped per pixel pair, the 0x7fff on the
    pixels with k + r even) through a 2 x 256 "palette pair" texture (row n: colours n and n + 1) whose s coordinate is
    window x + y + phase - the checkerboard comes from the texture, no state change between runs;
  - the textured types 5 / 6 with the original's texel walk (0x68ac2 affine: linear along the row; 0x673b0 perspective:
    exact every 16th pixel, linear between - each run a quad with linear texel coordinates, nearest-sampled), the
    other textured types with GL's perspective texels;
  - cockpit polygons clipped at their own near plane in the camera's integers (0x3e8fe) as the shader does;
  - near-clipped polygons as the shader has them: GL's coverage, each pixel taking the span's end (no discards).
- **ENHANCED** (`u_dos_tc`): the same coverage; the shade level unrounded - a flat ramp's two palette colours as vertex
  colours on quads at the triangle's own w (GL's perspective interpolation keeps them linear in the view depth), cut
  where the level crosses a whole number; textures through a "true colour" variant (each texel's two LUMA rows' colours
  blended, at the level to an eighth; transparent texels taking their neighbours' colour, so GL's bilinear filter and
  the alpha test at 0.5 are the shader's weighted taps), mip-mapped where the shader takes footprint taps; the Titanium
  fog on unit 1. The horizon fill: the ground fogged at the depth its view ray meets the ground (each corner sent at w =
  that depth), the band blended.
- **TITANIUM**: texture x vertex colour (the lights per vertex), the alpha test for the shader's discards, the
  colour kind 2 / no-bitmap polygons dropped, the fog per pixel: a ramp texture (RGB the fog colour, A the factor) on
  unit 1 with `GL_DECAL`, its coordinate linear in the view depth (also past the eye: GL's perspective interpolation
  keeps it exact), the triangle split at the full-fog depth Z; colour kind 4 unfogged.
- **Wireframe** (image enhancement): the insides black (pushed back with a polygon offset), each edge as the shader has
  it - the band of the triangle within |n.x| + |n.y| pixels of the edge, cut in screen space; `GL_LINES` (2 px) for a
  triangle that crosses the near plane.
- **HUD**: textures that aren't a power of two, or are larger than the card's limit, are kept at their own size in
  power-of-two tiles with a one-texel border; each HUD triangle is cut at the tiles' and the wrap's edges - every texel
  stays in place, point-sampled or bilinear. The flash: unit 1 in `GL_BLEND` (one unit: an added second pass). World
  textures are powers of two; one that isn't, or is larger than the limit, is resampled (bilinear) - `MW2_GL12_TRACE`
  lists them.
- **Off-screen targets** (the low-resolution frame, 640 x 480 / 320 x 200): drawn in the back buffer and kept in
  textures with `glCopyTexSubImage2D` (tiled the same way) while another is drawn; the window's picture is kept and put
  back the same way if it was drawn first. `rb_blit` draws from those textures. A target must fit in the window.

## Accuracy

Against the SDL_GPU renderer (Vulkan, Mesa lavapipe; the 2026-11-04 main build) on Mesa llvmpipe (OpenGL 4.5
compatibility profile, used as 1.2), aa=0, the same frames of `MW2_AUTOPILOT` runs (1.5 s standing, 6 s walking and
firing): mean per-pixel difference (largest channel, 0-255), the share of pixels more than 32 levels off and of
pixels that differ at all. BASSSCN1, JESSSCN1, JESSSCN1's cliff turrets close up (`MW2_START=2500,-9000,8500`),
PETESCN1, SACHSCN1; 1024 x 768 (1.5 s and 6 s) and 640 x 480 (1.5 s) - 15 frames a look:

| Look | mean (worst frame) | > 32 levels (worst) | pixels different (worst) | identical frames |
|---|---|---|---|---|
| DOS | 0.000 (0.002) | 0.001 % | 0.002 % | 4 of 15 |
| ENHANCED | 0.089 (0.167) | 0.005 % | 14.9 % (1-2 levels) | 0 |
| TITANIUM | 0.037 (0.127) | 0.013 % | 12.7 % (1-2 levels) | 0 |
| `MW2_GL12_MAXTEX=256` (640 x 480, BASS / JESS / PETE / SACH): DOS / ENHANCED / TITANIUM | 0.000 / 0.114 / 0.046 | 0.004 % | | |
| `MW2_GL12_NO_MULTITEX=1` (the same): DOS / ENHANCED / TITANIUM | 0.000 / 0.194 / 0.119 | 0.004 % | | |

What differs: the DOS look a few isolated pixels a frame (most frames none to a dozen; GL's and Vulkan's float rounding
of a corner far off screen); ENHANCED one or two levels where GL's bilinear filter and the eighth-level textures round
otherwise than the shader's taps, a few polygon edges; TITANIUM polygon edges (GL's rasterizer vs Vulkan's) and one or
two levels of the fog ramp. The SDL=2 build on Linux (SDL2 2.30) draws the same frames byte for byte as `RB=gl12` on
SDL3.

**Approximations**: a point light's direction per vertex (TITANIUM) or per triangle (the DOS looks); ENHANCED's
texture shade levels to an eighth of a level, its filtering as GL's bilinear / mip levels instead of the shader's
footprint taps, its Gouraud shade as a ramp within a run of equal levels; one texture unit: the fog as a second pass over what was drawn (a blended polygon's by its vertex
alpha); no multisampling; a texture resampled before filtering, not after.

## Tests and switches

- A/B against the SDL_GPU build: the same `MW2_AUTOPILOT` run on both (`SDL_VIDEODRIVER=offscreen`; the GL build on Mesa's
  llvmpipe, the GPU build on lavapipe), frames compared per pixel.
- `MW2_GL12_MAXTEX=n` (pretend the card's largest texture is n - 256 as a Voodoo 3), `MW2_GL12_NO_MULTITEX=1` (one
  texture unit), `MW2_GL12_NOFOG=1` (no fog), `MW2_GL12_TRACE=1` (the extensions, textures tiled / resampled, fog).
- `MW2_DOS_EXACT=0`, `MW2_DOS_ORDER=0`, `MW2_TI_ORDER=0`, `MW2_DOS_DEBUG=1` behave as on the GPU build.

## Limits

- No multisampling (the RESOLUTION row's AA steps do nothing); `GPU OPENGL` in Combat Variables.
- CPU cost: the vertex stage runs per vertex per frame and the DOS look's exact fill walks every polygon's spans on the
  CPU (a quad per run of a row); on a period CPU those dominate. First places to optimise: static geometry's vertex
  stage cached between frames, the spans of polygons that did not move.
- Not yet run on a real OpenGL 1.x driver or Windows XP (KNOWN_ISSUES.md).
