#version 450
#extension GL_GOOGLE_include_directive : require
// world.vert - the world renderer's vertex shader (src/glr.c; the OpenGL 3.3 program's VS, ported to SDL_GPU).
// Compiled twice: DOS_LOOK 0 (the TITANIUM look) and DOS_LOOK 1 (the DOS and ENHANCED looks: the DOS Gouraud fill's
// exact coverage, the integer projection and its check, the fog key). Differences from the OpenGL program, none of
// them in what it computes:
//  - gl_VertexID is u_first + gl_VertexIndex: every draw starts at vertex 0 of its buffers bound at the batch's offset
//    (SV_VertexID / [[vertex_id]] / gl_VertexIndex agree only then), u_first being the batch's first vertex in the layer
//  - the layer's vertices for the DOS look are a storage buffer (dvtx), no longer an R32F texture
//  - flat varyings: SDL_GPU's APIs take them from a triangle's first vertex, OpenGL from its last; the one that differs
//    per corner (the projection check, v_dtrust / v_derr) is worked out for the triangle's last vertex, as GL had it
//  - the depth: GL's clip z (-w..w) to SDL_GPU's 0..w at the end, so the depth buffer holds GL's window depth
#define UBO_SET 1
#define SSBO_SET 0
#define SSBO_BINDING 0
#include "world_common.glsl"
#if DOS_LOOK
// the DOS draw list's depths (glr.c glr_dos_list), per polygon of the layer (its index + 1 in a_tic.x): GL NDC z, 3 for a
// polygon the original does not list - the vertex stage's second storage buffer
layout(std430, set = SSBO_SET, binding = 1) readonly buffer DosRank { float u_dos_rank[]; };
#endif

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
layout(location = 4) in float a_texsel;
layout(location = 5) in float a_edge;
layout(location = 6) in vec4 a_sphere;
layout(location = 7) in vec4 a_tic;
#define VARY out
V_LOCATIONS

void main() {
  int vertex_id = u_first + gl_VertexIndex;
  v_dord = 2.0;
  int e = int(a_edge + 0.5), pit = e / 65536, kmode = 0;
  e -= 65536 * pit;
  // 200 + m: a world object's DOS key mode m (mech3d_part.dos_sort), not a cockpit polygon; 300 + w + 4 x f: the TITANIUM
  // look's polygon normal against the fan's first triangle's winding (glr set_layer: w 1 the same way, 0 opposite, 2
  // unknown) and f 1 for a polygon the Titanium fog leaves alone
  int tiwind = 0;
  v_tinofog = 0;
  int tisky = 0, ptc = -1;
  if (pit >= 400) { ptc = pit - 400; pit = 0; }   // a point's pixel corner (glr set_layer)
  else if (pit >= 300) { int q = pit - 300; tisky = q >= 8 ? 1 : 0; q -= 8 * tisky; v_tinofog = q >= 4 ? 1 : 0; q -= 4 * v_tinofog; tiwind = q == 1 ? 1 : q == 0 ? -1 : 0; pit = 0; }
  else if (pit >= 200) { kmode = pit - 200; pit = 0; }
  // the DOS draw list (glr_dos_list: the original's list built on the CPU, sorted by its own quicksort): this polygon's
  // depth along the painting order, or not listed - it then decides, not the tests below (the one-sided test, the view
  // distance)
#if DOS_LOOK
  int dli = u_dos != 0 && u_dos_order != 0 && u_dlist != 0 && pit == 0 && a_tic.w < 0.5 ? int(a_tic.x + 0.5) - 1 : -1;
  float dld = dli >= 0 ? u_dos_rank[dli] : 0.0;
#else
  const int dli = -1;
#endif
  v_dnib = e / 2048; e -= v_dnib * 2048;
  // a mech location's damage nibble (glr_set_damage_shade)
  v_bb = e >= 1024 ? 1 : 0;
  // Titanium (edge code + 256: its model normal, length kept; + 512: a flat palette colour, unlit)
  v_tif = 0;
  if (e < 1024 && e >= 512) { v_tif |= 2; e -= 512; }
  if (e < 1024 && e >= 256) { v_tif |= 1; e -= 256; }
  vec3 p = a_pos, nrm = a_normal;
  gl_Position = u_mvp * vec4(p, 1.0);
  // a colour type 3 billboard (edge code + 1024; engine 0x10026fc7 -> 0x1002a8e0, mode 0): a_pos the foot (texel u 0,
  // v > 0), a_normal the top (u 0, v 0), a_texsel the corner 0-3. Both are projected; with A the top's and B the foot's
  // screen point (y down) and d = (B - A) / 2 the corners are A + (-d.y, d.x), A + (d.y, -d.x), B + (d.y, -d.x),
  // B + (-d.y, d.x) - a square of the edge's screen length, centred on it, whatever the view's pitch or roll - texel
  // corners (0,0) (1,0) (1,1) (0,1); screen-linear (q = 1), no lighting (0x1002c880 with intensity -1: colour 255)
  if (e >= 1024) {
    vec4 cb = u_mvp * vec4(a_pos, 1.0), ca = u_mvp * vec4(a_normal, 1.0);
    int k = int(a_texsel + 0.5), tib = k >= 4 ? 1 : 0;
    e -= 1024; nrm = vec3(0.0); k -= 4 * tib;
    if (ca.w <= 1.0 || cb.w <= 1.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    else if (tib != 0) {
  // Titanium (mercsw 0x1004ef4c, mode 0): T the top's and F the foot's screen point, dx = (F.y - T.y) / 2, dy = (F.x -
  // T.x) / 2: corner 0 (T.x - dx, T.y - dy), 1 (F.x - dx, F.y + dy), 2 (T.x + dx, F.y - dy), 3 (F.x + dx, T.y + dy) -
  // the square of 0x10026fc7 for an upright edge, not square once it leans (the engine's own mix of T and F); the
  // corners' sz: 0 and 3 the top's, 1 and 2 the foot's (+0x18 of each, 0x1004f121..0x1004f1a5)
      vec2 h = 0.5 * u_vp, T = ca.xy / ca.w * h, F = cb.xy / cb.w * h;
      T.y = -T.y; F.y = -F.y;
      float dx = 0.5 * (F.y - T.y), dy = 0.5 * (F.x - T.x);
      vec2 c = k == 0 ? vec2(T.x - dx, T.y - dy) : k == 1 ? vec2(F.x - dx, F.y + dy) : k == 2 ? vec2(T.x + dx, F.y - dy) : vec2(F.x + dx, T.y + dy);
      gl_Position = vec4(c.x / h.x, -c.y / h.y, k == 0 || k == 3 ? ca.z / ca.w : cb.z / cb.w, 1.0);
      p = k == 0 || k == 3 ? a_normal : a_pos;
    } else {
      vec2 h = 0.5 * u_vp, A = ca.xy / ca.w * h, B = cb.xy / cb.w * h;
      A.y = -A.y; B.y = -B.y;
      vec2 d = 0.5 * (B - A), c = k == 0 ? A + vec2(-d.y, d.x) : k == 1 ? A + vec2(d.y, -d.x) : k == 2 ? B + vec2(d.y, -d.x) : B + vec2(-d.y, d.x);
      gl_Position = vec4(c.x / h.x, -c.y / h.y, k < 2 ? ca.z / ca.w : cb.z / cb.w, 1.0);
      p = k < 2 ? a_normal : a_pos;
    }
  }
  // the DOS Gouraud fill's coverage (MW2.EXE 0x59e77): a row's span runs from the left chain's x >> 16 to the right
  // chain's INCLUSIVE (0x5a1dc..0x5a213: w = xR - xL, w + 1 pixels) and the rows from the top vertex's to the bottom
  // vertex's inclusive (0xa178c = ybot - ytop, 0x5a337 / 0x5a33d), each chain from its vertex's x + 0.5 (0x5a01f) - about
  // half a pixel more on the right and below than GL's centre rule. So the polygon (its fan, every triangle) is drawn
  // 2 px larger on screen - each vertex pushed out along its two edges' outward normals (miter, at most 8 px) - and the
  // fragment keeps exactly the DOS span's pixels (dos_gouraud); polygons crossing the near plane are left as they are
#if DOS_LOOK
  if (u_dos != 0 && u_dos_exact != 0 && int(a_color.r + 0.5) <= 3 && a_texsel > 0.5 && e < 256) {
    int s = int(a_texsel + 0.5) - 1, rel = vertex_id - s, t = rel / 3, cn = rel - 3 * t;
    vec2 Q[18]; int n = 0; bool ok = true; vec2 h = 0.5 * u_vp;
    for (int k = 0; k < 16; k++) {
      int b = s + 3 * k;
      for (int c = (k == 0 ? 0 : 1); c < 3; c++) {
        if (c == 2 && ((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) == 0) break;
        vec4 wp = vec4(dvtx((b + c) * 13), dvtx((b + c) * 13 + 1), dvtx((b + c) * 13 + 2), 1.0), q = u_mvp * wp;
        if (q.z + q.w < 0.0 || q.w <= 0.0) ok = false; else { ivec2 sp = dos_scr(wp.xyz); Q[n] = vec2(float(sp.x) + 0.5, u_vp.y - float(sp.y) - 0.5) - h; }
        n++;
      }
      if (((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) != 0) break;
    }
    int j = cn == 0 ? 0 : t + cn;
    if (ok && n >= 3 && j < n && gl_Position.w > 0.0) {
      float area = 0.0;
      for (int i = 0; i < n; i++) { vec2 a = Q[i], b = Q[(i + 1) % n]; area += a.x * b.y - a.y * b.x; }
      vec2 A = Q[(j + n - 1) % n], B = Q[j], C = Q[(j + 1) % n], e1 = B - A, e2 = C - B;
      float sg = area > 0.0 ? 1.0 : -1.0;
      vec2 n1 = length(e1) > 1e-4 ? sg * vec2(e1.y, -e1.x) / length(e1) : vec2(0.0), n2 = length(e2) > 1e-4 ? sg * vec2(e2.y, -e2.x) / length(e2) : vec2(0.0);
      vec2 m = n1 + n2; float d = 1.0 + dot(n1, n2);
      vec2 off = d > 1e-3 ? 3.0 * m / d : 3.0 * m;
      if (length(off) > 24.0) off = off / length(off) * 24.0;
      // at the DOS integer point (not GL's): the original's projection decides the coverage - for a corner a few cm
      // from the eye (the cockpit struts) the whole-cm rounding moves it more than 3 px from GL's (DOSBox BASSSCN1 Timber
      // Wolf: the struts' shared edges showed the sky between them where GL's coverage was kept)
      gl_Position.xy = (Q[j] + off) / h * gl_Position.w;
    }
  }
  // a cockpit polygon (the DOS cockpit set) with a corner nearer than its near plane (32 quarter units): clipped there in
  // the camera's integers as the original does (0x3ec8e -> 0x3e8fe) - GL's own near plane (60 cm) would cut it far
  // short and leave its coverage unaligned (DOSBox BASSSCN1 Timber Wolf: the sill's seams showed the sky). Each fan
  // triangle is laid over the clipped polygon's screen box (w 1: not clipped by GL) and the fragment shader keeps the
  // DOS span's pixels
  // World polygons the same, at the view's near plane (DAT_001617f8 = +0x3c x 4, (zoom x half width >> 17) + 1 cm - 257
  // cm at 1024 wide): GBL 0x3ec8e clips every listed polygon there. Before, only GL's near plane (60 cm) cut them, and a
  // corner between the two projected far outside the window - beyond 32767 px at high resolutions, where the span
  // integers (x << 16) wrapped and the wall's pixels were discarded (JESS hangar walls flickering in and out)
  if (u_dos != 0 && u_dos_exact != 0 && int(a_color.r + 0.5) <= 3 && a_texsel > 0.5 && pit < 300 && dos_nearof(int(a_texsel + 0.5) - 1) > 0) {
    int s = int(a_texsel + 0.5) - 1, rel = vertex_id - s, cn = rel - 3 * (rel / 3), nr = dos_nearof(s);
    ivec3 Cc[18]; int n = 0; bool near = false;
    for (int k = 0; k < 16; k++) {
      int b = s + 3 * k;
      for (int c = (k == 0 ? 0 : 1); c < 3; c++) {
        if (c == 2 && ((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) == 0) break;
        Cc[n] = dos_cam(vec3(dvtx((b + c) * 13), dvtx((b + c) * 13 + 1), dvtx((b + c) * 13 + 2)));
        if (Cc[n].z < nr) near = true;
        n++;
      }
      if (((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) != 0) break;
    }
    if (near) {
      ivec2 lo = ivec2(1 << 30), hi = ivec2(-(1 << 30)); bool any = false; float tt;
      for (int k = 0; k <= n && n >= 3; k++) {
        int cur = k % n, prv = (k + n - 1) % n;
        bool bc = Cc[cur].z < nr, bp = Cc[prv].z < nr;
        if (k > 0 && bc != bp) { ivec2 q = dos_camscr(dos_nclip(Cc[prv], Cc[cur], nr, tt)); lo = min(lo, q); hi = max(hi, q); any = true; }
        if (k < n && !bc) { ivec2 q = dos_camscr(Cc[cur]); lo = min(lo, q); hi = max(hi, q); any = true; }
      }
      vec2 h = 0.5 * u_vp;
      lo = max(lo, ivec2(-2)); hi = min(hi, ivec2(u_vp) + 2);
      if (!any || hi.x < lo.x || hi.y < lo.y) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      else {   // (pixels, y down) -> a triangle holding the box: (lo), (lo.x + 2 w, lo.y), (lo.x, lo.y + 2 h)
        vec2 a0 = vec2(lo) - 1.0, sz = vec2(hi - lo) + 3.0;
        vec2 pp = cn == 0 ? a0 : cn == 1 ? a0 + vec2(2.0 * sz.x, 0.0) : a0 + vec2(0.0, 2.0 * sz.y);
        gl_Position = vec4((pp.x - h.x) / h.x, (h.y - pp.y) / h.y, 0.0, 1.0);
      }
    }
  }
#endif
  // the DOS cockpit set: painted over everything, in the DOS draw order (MW2.EXE 0x3cea0 / 0x3d040: its list sorted
  // by each polygon's key, the farthest first, and drawn in turn) - a later polygon nearer in depth; one the original
  // leaves out (wholly behind the eye or off the window, 0x3e1de) is not drawn
  if (pit > 0 && u_dos != 0) {
    int rk = u_pit_rank(min(pit - 1, 127));
    if (rk < 0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    else gl_Position.z = (-1.0 + 0.01 * (1.0 - float(rk + 1) / 256.0)) * gl_Position.w;
  }
  // the view distance (the planet's VIEW far; DOS MW2.EXE 0x3f500, 3Dfx 0x1002fba0): an object whose bounding sphere
  // lies wholly beyond it - centre distance > far + r, or depth - r > far - is not drawn (all its vertices off screen)
  if (dli < 0 && u_far > 0.0 && a_sphere.w > 0.0) {
    vec3 sc = (u_view * vec4(a_sphere.xyz, 1.0)).xyz;
    if (dot(sc, sc) > (u_far + a_sphere.w) * (u_far + a_sphere.w) || -sc.z - a_sphere.w > u_far) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
  }
  // the DOS fog key (MW2.EXE 0x3e1de -> *0x97080 = 0x38ae0 -> 0x38ee0): one per polygon - the largest of its corners'
  // camera z (quarter units: dos_camz; the list key DAT_0014fcc0 = 0) - and the level less ((key << 4) / LITE +24) >> 4,
  // not the pixel's depth in cm. The polygon's corners from this layer's buffer: its fan triangles back to the one that
  // starts it (edge bit 4) and on to the one that closes it (bit 2); -1 (the pixel depth) for a vertex not found there
  v_dkey = -1.0; v_dtrust = 1; v_derr = vec2(0.0);
#if DOS_LOOK
  v_dosf = 0;
#endif
  // the DOS integer projection checked against GL's for this corner: more than 3 px apart (seen on macOS) -> the fill
  // draws the polygon from GL's points without the exact-span discards (dos_gouraud)
#if DOS_LOOK
  if (u_dos != 0) {
    int pv = 3 * (vertex_id / 3) + 2;   // GL's provoking vertex (the triangle's last) gives the flat v_dtrust / v_derr
    vec3 pp = vec3(dvtx(pv * 13), dvtx(pv * 13 + 1), dvtx(pv * 13 + 2));
    vec4 q0 = u_mvp * vec4(pp, 1.0);
    if (q0.w > 1.0) {
      ivec2 sp = dos_scr(pp); vec2 g = q0.xy / q0.w * 0.5 * u_vp; vec2 c0 = vec2(ivec2(u_vp) / 2);
      v_derr = vec2(float(sp.x) - (c0.x + g.x), float(sp.y) - (c0.y - g.y));
      if (abs(v_derr.x) > 3.0 || abs(v_derr.y) > 3.0) v_dtrust = 0;
    }
  }
#endif
#if DOS_LOOK
  if (u_dos != 0) {
    int vid = vertex_id, t = vid / 3, k, c;
    if (abs(dvtx(vid * 13) - a_pos.x) + abs(dvtx(vid * 13 + 2) - a_pos.z) < 0.01) {
      for (k = 0; k < 16 && t > 0; k++) { if (((int(dvtx(3 * t * 13 + 12) + 0.5) % 32 / 3) & 4) != 0) break; t--; }
      float km = -1e9, kn = 1e9, ks = 0.0, zl[18]; int kc = 0;
      for (k = 0; k < 16; k++) {
        int b = 3 * (t + k);
        bool last = ((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) != 0;
        for (c = 0; c < 3; c++) {
          float z = dos_camz(vec3(dvtx((b + c) * 13), dvtx((b + c) * 13 + 1), dvtx((b + c) * 13 + 2)));
          km = max(km, z);
          // the polygon's corners once each (the fan: corner 0, each triangle's next, the closing one's last)
          if ((c == 0 && k == 0) || c == 1 || (c == 2 && last)) { kn = min(kn, z); if (kc < 18) zl[kc] = z; kc++; }
        }
        if (last) break;
      }
      v_dkey = km;
      // the light intensity f (GBL MW2.EXE 0x3eb36), once a polygon: d = the light - its first vertex (whole cm; the
      // light alone when directional, +0x28), the normal N in 2.29 (0x38b30); dot = (d . N) >> 16; d's |x|, |y|, |z| and
      // dot shifted so that the largest component's top bit is bit 7; f = dot / chop(1024 sqrt((x^2 + y^2 + z^2) >> 8))
      // (the table 0x3b630), truncated - about 128 cos, a little more than 127 cos (DOSBox BASSSCN1 / Timber Wolf
      // cockpit struts: one LUMA step)
      {
        vec3 P0 = vec3(dvtx(3 * t * 13), dvtx(3 * t * 13 + 1), dvtx(3 * t * 13 + 2)), nn = vec3(dvtx(3 * t * 13 + 3), dvtx(3 * t * 13 + 4), dvtx(3 * t * 13 + 5));
        float nl = length(nn);
        // one-sided, once a polygon (GBL 0x3ec8e): listed only when (its first vertex - the eye) . N < 0, both in whole
        // cm (the world vertex +0xc..+0x14, the camera DAT_00161824..28) - the plane's own test, not each pixel's (a
        // non-planar polygon's fan triangles each had their own: the Timber Wolf cockpit sill's seams showed the sky)
        if (nl > 0.0 && e < 1024 && dli < 0) {
          vec3 dv = vec3(dos_rnd(P0.x) - dos_rnd(u_dos_eye.x), dos_rnd(P0.y) - dos_rnd(u_dos_eye.y), dos_rnd(-P0.z) - dos_rnd(-u_dos_eye.z));
          vec3 Ng = -vec3(nn.x, nn.y, -nn.z) / nl;
          if (dot(dv, Ng) >= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        }
        if (nl > 0.0) {
          vec3 lg = vec3(u_dos_lpos.x, u_dos_lpos.y, -u_dos_lpos.z), vg = vec3(dos_rnd(P0.x), dos_rnd(P0.y), dos_rnd(-P0.z));
          vec3 d = u_dos_dir != 0 ? vec3(dos_rnd(lg.x), dos_rnd(lg.y), dos_rnd(lg.z)) : vec3(dos_rnd(lg.x), dos_rnd(lg.y), dos_rnd(lg.z)) - vg;
          vec3 N = -vec3(nn.x, nn.y, -nn.z) / nl * 8192.0;   // 2.29 / 2^16
          vec3 ad = abs(d);
          float m = max(ad.x, max(ad.y, ad.z)), fi = 127.0;
          if (m > 0.0) {
            int mi = int(ad.x) | int(ad.y) | int(ad.z), tb = 31;
            while (tb > 0 && (mi >> tb) == 0) tb--;
            float sc = exp2(float(7 - tb));
            vec3 c = floor(ad * sc);
            float dot0 = floor(d.x * N.x + d.y * N.y + d.z * N.z), dots = floor(dot0 * sc);
            float S = c.x * c.x + c.y * c.y + c.z * c.z, dv = floor(1024.0 * sqrt(floor(S / 256.0)));
            fi = dv > 0.0 ? trunc(dots / dv) : 127.0;
          }
          v_dosf = int(fi) + 1000;
        }
      }
      // the DOS draw order (MW2.EXE 0x3d9c0 -> 0x3d830 -> 0x3ec8e, sorted by 0x3d930 - largest key first - and drawn in
      // turn by 0x3dad0): every polygon facing the eye is listed with one key - its farthest corner's camera z (quarter
      // units, as the fog key), the average for key mode 2, the nearest for 4 (the listed corners: those behind the near
      // plane replaced by plane corners, so at least the plane's z), + 0x40000000 for 1 (listed first: behind all).
      // Units' pieces are listed the same way (their models loaded with mode 0, 0x37bb0). No depth buffer: a turret on a
      // cliff is painted over the cliff's faces whose farthest corners lie beyond it, wherever its pieces pass into the
      // rock (DOSBox JESSSCN1). Here: each polygon's fragments at one depth (world.frag) - that of its key, on the view
      // axis - so the depth test keeps the polygon with the smaller key, as painting in that order does (equal keys:
      // the first drawn; the original's quicksort keeps no order either).
      // the listed corners are the polygon's clipped at the near plane (0x3e8fe: those behind it replaced by the edge's
      // points on it): the nearest at least the plane's z, the mean over the clipped polygon's corners
      float nz = u_dos_near > 0 ? float(u_dos_near) : 4.0, kcl = 0.0;
      kc = min(kc, 18);
      for (int i = 0; i < kc; i++) {
        float za = zl[i], zp = zl[(i + kc - 1) % kc];
        if ((za < nz) != (zp < nz)) { ks += nz; kcl += 1.0; }
        if (za >= nz) { ks += za; kcl += 1.0; }
      }
      float key = (kmode & 2) != 0 ? floor(ks / max(kcl, 1.0)) : (kmode & 4) != 0 ? max(kn, nz) : km;
      if (pit == 0 && key > 0.0) {
        vec3 fwd = -vec3(u_view[0][2], u_view[1][2], u_view[2][2]);
        vec4 q = u_mvp * vec4(u_dos_eye + fwd * (key * 0.25), 1.0);
        float nd = q.w > 0.0 ? min(q.z / q.w, 1.0 - 4e-6) : 1.0 - 4e-6;
        if ((kmode & 1) != 0) nd = 1.0 - 2e-6;
        v_dord = nd;
      }
    }
  }
  if (dli >= 0) {
    if (dld > 2.5) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    else v_dord = dld;
  }
#endif
#if !DOS_LOOK
  // the TITANIUM look's draw order (mercsw 0x100562ff -> 0x10055b90 -> 0x100570f6, sorted by 0x10056022 - comparator
  // 0x10055d19: the larger key first - and drawn in turn by 0x1005615f), as the DOS look's: no working depth test. Each
  // polygon: not listed when its normal faces away - n . (v0 - eye) >= 0 (0x100570f6, DAT_100b011c never set); its key
  // the deepest listed corner's view depth (the node flags always 0x8000: no mean / nearest / +2^28). Here: a polygon
  // facing away is not drawn, the others at their key's depth (world.frag)
  if (tiwind != 0 && u_onlayer != 0 && u_dos_order != 0 && e < 1024) {
    int vid = vertex_id, t = vid / 3, k, c;
    if (abs(dvtx(vid * 13) - a_pos.x) + abs(dvtx(vid * 13 + 2) - a_pos.z) < 0.01) {
      for (k = 0; k < 16 && t > 0; k++) { if (((int(dvtx(3 * t * 13 + 12) + 0.5) % 32 / 3) & 4) != 0) break; t--; }
      vec3 P0 = vec3(dvtx(3 * t * 13), dvtx(3 * t * 13 + 1), dvtx(3 * t * 13 + 2));
      vec3 P1 = vec3(dvtx((3 * t + 1) * 13), dvtx((3 * t + 1) * 13 + 1), dvtx((3 * t + 1) * 13 + 2));
      vec3 P2 = vec3(dvtx((3 * t + 2) * 13), dvtx((3 * t + 2) * 13 + 1), dvtx((3 * t + 2) * 13 + 2));
      mat3 vr = mat3(u_view);
      vec3 eyeg = -(transpose(vr) * u_view[3].xyz);
      vec3 w = cross(P1 - P0, P2 - P0);
      // (GL coordinates: the z mirror turns the winding's cross product but not the normal - the normal is -tiwind x w)
      if (length(w) > 1e-3 && float(tiwind) * dot(w, P0 - eyeg) <= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      else {
        float km = -1e30;
        for (k = 0; k < 16; k++) {
          int b = 3 * (t + k);
          for (c = 0; c < 3; c++) km = max(km, -(u_view * vec4(dvtx((b + c) * 13), dvtx((b + c) * 13 + 1), dvtx((b + c) * 13 + 2), 1.0)).z);
          if (((int(dvtx(b * 13 + 12) + 0.5) % 32 / 3) & 2) != 0) break;
        }
        if (km > 0.0) {
          vec3 fwd = -vec3(u_view[0][2], u_view[1][2], u_view[2][2]);
          vec4 q = u_mvp * vec4(eyeg + fwd * km, 1.0);
          v_dord = q.w > 0.0 ? min(q.z / q.w, 1.0 - 4e-6) : 1.0 - 4e-6;
        }
      }
    }
  }
  // the sky object (+ 8): its own list, painted before the world (mercsw 0x1001c0xx: 0x10056589 over DAT_100a67ac, the far
  // distance off, then the main lists) - behind every other polygon, its own polygons by their keys
  if (tisky != 0 && u_onlayer != 0 && u_dos_order != 0) v_dord = clamp(1.0 - 3e-6 + (min(v_dord, 1.0) - 1.0) * 1e-3, 1.0 - 3.9e-6, 1.0 - 1e-6);
#endif
#if DOS_LOOK
  // a point (one vertex, colour type 0): the pixel at its DOS integer projection, painted at its place in the draw list
  // (corners 0..5: two triangles over the pixel's square); not drawn outside the list
  if (ptc >= 0) {
    if (u_dos == 0 || dli < 0 || dld > 2.5) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    else {
      ivec2 sp = dos_scr(a_pos); vec2 h = 0.5 * u_vp;
      float px = float(sp.x) + (ptc == 1 || ptc == 2 || ptc == 4 ? 1.0 : 0.0), py = u_vp.y - float(sp.y) - 1.0 + (ptc == 2 || ptc == 4 || ptc == 5 ? 1.0 : 0.0);
      gl_Position = vec4((px - h.x) / h.x, (py - h.y) / h.y, 0.0, 1.0);
    }
  }
#else
  if (ptc >= 0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
#endif
  v_viewpos = (u_view * vec4(p, 1.0)).xyz; v_world = p;
  v_normal = nrm; v_uv = a_uv; v_color = a_color; v_texsel = int(floor(a_texsel + 0.5)); v_sph = a_sphere;
  v_tic = a_tic.rgb; v_ticm = int(a_tic.a + 0.5); v_gs = a_uv;
  int hi = e / 32; e -= hi * 32; int corner = e - 3 * (e / 3);
  v_bary = vec3(corner == 0 ? 1.0 : 0.0, corner == 1 ? 1.0 : 0.0, corner == 2 ? 1.0 : 0.0); v_emask = (e / 3) | (hi << 3);
  // the depth: GL's window z (z / w x 0.5 + 0.5, its viewport transform for the depth range 0..1) times w - SDL_GPU's
  // clip z runs 0..w and its rasterizer divides by w again, giving GL's depth back to within the last bit
  gl_Position.z = (gl_Position.z * (1.0 / gl_Position.w) * 0.5 + 0.5) * gl_Position.w;
}
