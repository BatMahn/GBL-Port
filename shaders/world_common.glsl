// world_common.glsl - the world renderer's (src/glr.c) resource layout and the DOS look's integer projection, shared by
// world.vert and world.frag.
//
// SDL_GPU's binding model (SDL_gpu.h, SDL_CreateGPUShader): SPIR-V vertex set 0 = storage buffers (no textures), set 1 =
// uniform buffers; fragment set 2 = the sampled textures (combined image samplers) then the storage buffer, set 3 = the
// uniform buffers. scripts/gen_shaders.py moves the storage buffers after the uniform buffers for Metal ([[buffer]]
// order) and reads the blocks' member offsets back (spirv-cross --reflect) to write the C structs in src/rb_shaders.h.
//
// The blocks are pushed to both stages with the same data: block 0 (WorldFrame) changes a few times a frame (the
// camera, lights, the DOS projection), block 1 (WorldDraw) per batch. Every member keeps the name the OpenGL program's
// uniform had; arrays of vec3 / int became vec4 / ivec4 (std140 strides).

layout(std140, set = UBO_SET, binding = 0) uniform WorldFrame {
    mat4  u_mvp;
    mat4  u_view;
    vec4  u_tifog;
    vec4  u_hz_c;
    vec3  u_dos_eye;   int   u_dos;
    vec3  u_dos_rx;    float u_far;
    vec3  u_dos_ry;    int   u_point;
    vec3  u_dos_rz;    int   u_tidn;
    ivec2 u_dos_ps;    vec2  u_vp;
    vec2  u_vpo;       float u_fbh;      // u_fbh: the target's height (GL's bottom-left gl_FragCoord from the top-left one)
    int   u_dos_smooth;
    vec3  u_lpos;      int   u_dos_dir;
    vec3  u_tiamb;     float u_ambient;
    vec3  u_tignd;     float u_hz_eye;
    vec3  u_light;     float u_pfade;
    vec3  u_fog_color; float u_dos_amb;
    vec3  u_dos_lpos;  float u_dos_fogdist;
    vec3  u_hz_r;      int   u_notex;
    vec3  u_hz_u;      int   u_dsign;
    vec3  u_hz_f;      float u_hz_band;
    vec2  u_hz_x;      ivec2 u_tilt;
    int   u_hz_smooth; int   u_dos_exact; int u_dos_tc; int u_dos_order;   // u_dos_order: the DOS draw order's depths
    vec4  u_tilv[2];   // xyz (was vec3[2])
    vec4  u_tilc[2];
    vec4  u_tidp[8];
    vec4  u_tidc[8];   // xyz (was vec3[8])
    ivec4 u_pit_rank4[32];   // u_pit_rank[128] (GLR_PIT_MAX), four to an ivec4
    int   u_dos_near;      // the DOS view's near plane, camera z in quarter units: (+0x3c = (zoom x half width >> 17) + 1) x 4 (DAT_001617f8)
};

layout(std140, set = UBO_SET, binding = 1) uniform WorldDraw {
    vec4  u_wire;
    vec4  u_mono;
    vec3  u_tint;      int   u_first;    // u_first: the draw's first vertex in the layer (gl_VertexID = u_first + gl_VertexIndex)
    vec2  u_texsize;   int   u_textured; int u_unlit;
    int   u_shadow;    float u_alpha;    int u_blend;  int u_tinton;
    int   u_horizon;   int   u_tilit;   int u_onlayer;   // u_onlayer: drawing from a layer (its vertex buffer bound: dvtx)
};

#define u_pit_rank(i) u_pit_rank4[(i) >> 2][(i) & 3]

// the layer's vertex buffer read as floats (13 a vertex) - the OpenGL program's R32F texture u_dos_vtx (GLR_VTX_W a
// row), now the buffer itself (both programs: the TITANIUM look's draw order walks the polygons too)
layout(std430, set = SSBO_SET, binding = SSBO_BINDING) readonly buffer DosVtx { float u_dos_vtx[]; };
float dvtx(int i) { return u_dos_vtx[i]; }

#if DOS_LOOK
// a vertex as MW2.EXE projects it, in its integers: the world vertices (+0xc..+0x14) and the camera (DAT_0014fe94 /
// 90 / 98) are whole units (cm), so the vertex relative to the camera is round(W) - round(E) (the vertices'
// fixed-point transforms round to nearest, 0x3b522 / 0x3b20c); 0x3dd5c / 0x3e1de turn it by the camera matrix (2.29)
// whose x and y rows carry the zoom's mantissas (0x3ece0: DAT_0014fe40 / 44, 0x3ef30) - x = (R.d) x xm / 2^14, y =
// (U.d) x ym / 2^14, z = (F.d) x 4, each >> 27 rounded (the adc); then 0x3dfb7: ((x << xs) / z + 2) >> 2 from the
// window's centre (idiv: truncated). DOSBox (dosbox-debug, YELLSCN1 1024 x 768, the cockpit's draw list): every
// vertex's camera integers and screen point reproduced from the port's float world points this way. u_dos_rx / ry /
// rz: the three rows / 2^27 (game coordinates, dos_cam_rows); u_dos_ps: xs, ys (port: the zoom from the projection's
// x focal)
int dos_idiv(int n, int d) { int q = abs(n) / abs(d); return (n < 0) != (d < 0) ? -q : q; }   // idiv: truncated
float dos_rnd(float v) { return floor(v + 0.5); }
int dos_pq(int c, int s, int z) { int q; if (abs(c) < (1 << (30 - s))) q = dos_idiv(c << s, z); else q = int(trunc(float(c) * exp2(float(s)) / float(z))); return (q + 2) >> 2; }
float dos_camz(vec3 w) { vec3 d = vec3(dos_rnd(w.x) - dos_rnd(u_dos_eye.x), dos_rnd(w.y) - dos_rnd(u_dos_eye.y), dos_rnd(-w.z) - dos_rnd(-u_dos_eye.z)); return dos_rnd(dot(u_dos_rz, d)); }
// the camera integers alone (x, y with the zoom mantissas, z in quarter units) and their projection (0x3dfb7)
ivec3 dos_cam(vec3 w) {
  vec3 d = vec3(dos_rnd(w.x) - dos_rnd(u_dos_eye.x), dos_rnd(w.y) - dos_rnd(u_dos_eye.y), dos_rnd(-w.z) - dos_rnd(-u_dos_eye.z));
  return ivec3(int(dos_rnd(dot(u_dos_rx, d))), int(dos_rnd(dot(u_dos_ry, d))), int(dos_rnd(dot(u_dos_rz, d))));
}
ivec2 dos_camscr(ivec3 c) { ivec2 h = ivec2(u_vp) / 2; int z = max(c.z, 1); return ivec2(h.x + dos_pq(c.x, u_dos_ps.x, z), h.y - dos_pq(c.y, u_dos_ps.y, z)); }
// a polygon's corner on the near plane (GBL 0x3e8fe = base 0x3de4e) between a (behind) and b, or b and a: x, y
// interpolated to z = nr (the cockpit set's: 32 quarter units, DAT_0014feb0; the view's u_dos_near) with imul / idiv; t
// the share for the shade
const int DOS_PIT_NEAR = 32;
ivec3 dos_nclip(ivec3 A, ivec3 B, int nr, out float t) {
  if (B.z <= A.z) { int dz = A.z - B.z, e2 = nr - B.z; t = 1.0 - float(e2) / float(max(dz, 1));
    return ivec3(dos_idiv((A.x - B.x) * e2, dz) + B.x, dos_idiv((A.y - B.y) * e2, dz) + B.y, nr); }
  int dz = B.z - A.z, e2 = nr - A.z; t = float(e2) / float(max(dz, 1));
  return ivec3(dos_idiv((B.x - A.x) * e2, dz) + A.x, dos_idiv((B.y - A.y) * e2, dz) + A.y, nr);
}
bool dos_ispit(int vtx) { int e = int(dvtx(vtx * 13 + 12) + 0.5) / 65536; return e >= 1 && e < 200; }
// the near plane a polygon is clipped at in the camera's integers (GBL 0x3ec8e -> 0x3e8fe, DAT_001617f8): the cockpit
// set's, or the view's (u_dos_near, every listed polygon - world objects and units alike; with MW2_DOS_ORDER=0 the
// world polygons keep GL's near plane and depths: 0 here)
int dos_nearof(int vtx) { return dos_ispit(vtx) ? DOS_PIT_NEAR : u_dos_order != 0 ? u_dos_near : 0; }
ivec2 dos_scr(vec3 w) {
  vec3 d = vec3(dos_rnd(w.x) - dos_rnd(u_dos_eye.x), dos_rnd(w.y) - dos_rnd(u_dos_eye.y), dos_rnd(-w.z) - dos_rnd(-u_dos_eye.z));
  int xc = int(dos_rnd(dot(u_dos_rx, d))), yc = int(dos_rnd(dot(u_dos_ry, d))), zc = max(int(dos_rnd(dot(u_dos_rz, d))), 1);
  ivec2 c = ivec2(u_vp) / 2;
  return ivec2(c.x + dos_pq(xc, u_dos_ps.x, zc), c.y - dos_pq(yc, u_dos_ps.y, zc));
}
#endif

// the varyings, packed by interpolation (12 locations: within every backend's minimum of 16 vec4 outputs)
#define V_LOCATIONS \
    VLOC(0) VARY vec3 v_normal; \
    VLOC(1) VARY vec2 v_uv; \
    VLOC(2) VARY vec3 v_color; \
    VLOC(3) VARY vec3 v_viewpos; \
    VLOC(4) VARY vec3 v_world; \
    VLOC(5) VARY vec3 v_bary; \
    VLOC(6) VARY vec3 v_tic; \
    VLOC(7) flat VARY ivec4 v_i0; \
    VLOC(8) flat VARY ivec4 v_i1; \
    VLOC(9) flat VARY vec4 v_sph; \
    VLOC(10) flat VARY vec4 v_f0; \
    VLOC(11) noperspective VARY vec2 v_gs;
#define VLOC(n) layout(location = n)
#define v_texsel v_i0.x
#define v_emask  v_i0.y
#define v_bb     v_i0.z
#define v_tif    v_i0.w
#define v_dnib   v_i1.x
#define v_ticm   v_i1.y
#define v_dtrust v_i1.z   // the DOS integer projection agrees with GL's for this corner (within 3 px)
#define v_tinofog v_i1.w  // TITANIUM: a polygon the vertex fog leaves alone (colour kind 4: mercsw 0x10053280)
#define v_dosf   v_i1.w   // the DOS look: the polygon's light intensity f (GBL MW2.EXE 0x3eb36) + 1000; 0 not worked out
#define v_dkey   v_f0.x   // the DOS fog key (-1: the pixel depth)
#define v_derr   v_f0.yz  // MW2_DOS_DEBUG: the DOS-vs-GL offsets of the provoking corner
#define v_dord   v_f0.w   // the DOS look: the polygon's depth in the DOS draw order (GL's NDC z of its key; 2 none)
// v_gs: the DOS Gouraud shade (a_uv), screen-linear as MW2.EXE 0x59e77 steps it
