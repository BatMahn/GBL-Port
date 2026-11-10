#!/usr/bin/env python3
"""lookswitch.py - the in-mission look switch (DOS / ENHANCED / TITANIUM) against missions started in each look.

For every GBL scene (the install's *SCN1.BWD), headless (SDL offscreen, MW2_AUTOPILOT fixed 1/30 s frames, MW2_TEST_SPARSE):
  A  the mission started in the first look, no switch;
  B  the same mission switching look mid-mission (MW2_TEST_LOOKSWITCH, default dos -> titanium -> enhanced -> dos);
  R  a mission started in each look B passes through (every look simulates as the DOS game: tools/glview.c g_sim_kind).
Checks: (a) the simulation - MW2_TEST_SIMHASH's per-frame hash of the game state - of B equals A's frame for frame (and
every R's too), and the sound: the sound log (MW2_SFX_LOG) and the whole mix rendered on the simulation's clock
(MW2_SFX_WAV) byte for byte; (b) B's frames after each switch equal R's at the same times.
--repeat N: one scene switched N times round the looks (and its simulation against the same mission without switches):
after each switch the backend's live textures / buffers / GPU objects (LOOKSWITCH lines) and the game's own heap (a
malloc tracker preloaded: the heap by the module that allocated it) must stay the same from each look's second visit
on; the graphics driver's own heap (Mesa's, in-process) is shown beside it.

    python3 tools/audit/lookswitch.py --bin ./gbl --game <GBL-game folder: install/ cd/ titanium/> [--scenes BASSSCN1,..]
           [--jobs 2] [--out /tmp/lookswitch] [--repeat 20 --repeat-scene BASSSCN1]

Vulkan headless: VK_ICD_FILENAMES (default Mesa lavapipe); the RB=gl12 build runs on Mesa llvmpipe (SDL offscreen EGL).
Needs a C compiler (a time shim: SDL_GetTicks / time() fixed, so the runs repeat). No game data is written outside --out
(the install folder gets the mission results, as from any mission run).
"""
import argparse, concurrent.futures as cf, glob, os, re, shutil, subprocess, sys, tempfile

SHIM = r'''
#include <time.h>
#include <stdint.h>
time_t time(time_t *t) { time_t v = 1700000000; if (t) *t = v; return v; }
static uint64_t ticks = 1000;
uint64_t SDL_GetTicks(void) { ticks += 3; return ticks; }
'''
# --repeat: the heap by the module that allocated it (LD_PRELOAD; the game's own allocations against the graphics
# driver's), dumped by the game after each switch (tools/glview.c test_lookswitch_report: ltrack_dump)
TRACKER = r'''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TEST tool: live heap bytes by the module that called malloc / calloc / realloc; ltrack_dump() prints them */
#define NB (1u << 23)
static struct ent { void *p; size_t n; int mod; } tab[NB];
static char modname[128][96];
static int nmod;
static long long live[128];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static void *(*r_malloc)(size_t), *(*r_calloc)(size_t, size_t), *(*r_realloc)(void *, size_t);
static void (*r_free)(void *);
static __thread int inside;
static char boot[1 << 16];
static size_t bootn;
static struct { void *ra; int mod; } rc[4096];

static void init(void)
{
    if (r_malloc) return;
    inside++;
    r_calloc = dlsym(RTLD_NEXT, "calloc");
    r_realloc = dlsym(RTLD_NEXT, "realloc"); r_free = dlsym(RTLD_NEXT, "free");
    r_malloc = dlsym(RTLD_NEXT, "malloc");
    inside--;
}
static int mod_of(void *ra)
{
    unsigned h = (unsigned)(((uintptr_t)ra >> 4) & 4095);
    Dl_info di;
    int k;
    const char *n;
    if (rc[h].ra == ra) return rc[h].mod;
    n = dladdr(ra, &di) && di.dli_fname ? di.dli_fname : "?";
    { const char *s = strrchr(n, '/'); if (s) n = s + 1; }
    for (k = 0; k < nmod; k++) if (strcmp(modname[k], n) == 0) break;
    if (k == nmod && nmod < 127) { snprintf(modname[nmod], sizeof modname[0], "%s", n); nmod++; }
    if (k >= 127) k = 127;
    rc[h].ra = ra; rc[h].mod = k;
    return k;
}
static unsigned slot(void *p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) & (NB - 1); }
static void add(void *p, size_t n, void *ra)
{
    unsigned i;
    int m;
    if (!p || inside) return;
    inside++;
    pthread_mutex_lock(&mu);
    m = mod_of(ra);
    for (i = slot(p); tab[i].p; i = (i + 1) & (NB - 1)) ;
    tab[i].p = p; tab[i].n = n; tab[i].mod = m;
    live[m] += (long long)n;
    pthread_mutex_unlock(&mu);
    inside--;
}
static void del(void *p)
{
    unsigned i, j, k;
    if (!p || inside) return;
    pthread_mutex_lock(&mu);
    for (i = slot(p); tab[i].p && tab[i].p != p; i = (i + 1) & (NB - 1)) ;
    if (tab[i].p) {
        live[tab[i].mod] -= (long long)tab[i].n;
        tab[i].p = NULL;
        /* backward-shift deletion */
        j = i;
        for (;;) {
            j = (j + 1) & (NB - 1);
            if (!tab[j].p) break;
            k = slot(tab[j].p);
            if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) { tab[i] = tab[j]; tab[j].p = NULL; i = j; }
        }
    }
    pthread_mutex_unlock(&mu);
}
void *malloc(size_t n)
{
    void *p;
    if (!r_malloc) { if (inside) { void *b = boot + bootn; bootn += (n + 15) & ~(size_t)15; return b; } init(); }
    p = r_malloc(n);
    add(p, n, __builtin_return_address(0));
    return p;
}
void *calloc(size_t a, size_t b)
{
    void *p;
    if (!r_calloc) { void *q = boot + bootn; bootn += (a * b + 15) & ~(size_t)15; memset(q, 0, a * b); return q; }
    p = r_calloc(a, b);
    add(p, a * b, __builtin_return_address(0));
    return p;
}
void *realloc(void *o, size_t n)
{
    void *p;
    if (!r_realloc) init();
    if ((char *)o >= boot && (char *)o < boot + sizeof boot) { p = malloc(n); memcpy(p, o, n); return p; }
    del(o);
    p = r_realloc(o, n);
    add(p, n, __builtin_return_address(0));
    return p;
}
void free(void *p)
{
    if ((char *)p >= boot && (char *)p < boot + sizeof boot) return;
    if (!r_free) init();
    del(p);
    r_free(p);
}
void ltrack_dump(const char *tag)
{
    int k;
    pthread_mutex_lock(&mu);
    for (k = 0; k < nmod; k++) if (live[k] > 100000 || live[k] < -100000) fprintf(stderr, "LTRACK %s %s %lld\n", tag, modname[k], live[k]);
    pthread_mutex_unlock(&mu);
}
'''
LOOKS = ("dos", "enhanced", "titanium")


def env_for(look, game, cfg):
    e = {k: v for k, v in os.environ.items() if not k.startswith("MW2_")}
    e.update(MW2_CONFIG=cfg, MW2_INSTALL_DIR=game + "/install", MW2_CD_DIR=game + "/cd", MW2_CD_DATA="GBL",
             SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", MW2_NO_STARTUP="1", MW2_TEST_SIMHASH="1",
             MW2_TEST_SPARSE="1", MW2_SFX_LOG="1")
    e.setdefault("VK_ICD_FILENAMES", "/usr/share/vulkan/icd.d/lvp_icd.json")
    prj, tprj = game + "/install/MW2.PRJ", game + "/titanium/MW2.PRJ"
    if look == "titanium":
        e.update(MW2_TEXTURES="titanium", MW2_EDITION="enhanced", MW2_SKYGND=game + "/titanium/SKYGND.PAR")
        args = [prj, tprj, "titanium"]
    else:
        e.update(MW2_EDITION=look, MW2_TEXTURES="original")
        if look == "enhanced": e["MW2_SKYGND_FOG"] = game + "/titanium/SKYGND.PAR"
        args = [prj, prj, "dos"]
    return e, args


def run(opt, shim, scene, look, tag, extra, shots):
    d = os.path.join(opt.out, scene)
    os.makedirs(d, exist_ok=True)
    cfg = os.path.join(d, tag + ".cfg")
    with open(cfg, "w") as f: f.write("game=%s\naa=0\n" % opt.game)
    e, args = env_for(look, opt.game, cfg)
    e.update(extra)
    e["LD_PRELOAD"] = shim + (" " + opt.tracker if tag == "REP" and getattr(opt, "tracker", None) else "")
    e["MW2_SIZE"] = opt.size
    e["MW2_AUTOPILOT"] = "%g,0.6,0,1,%s" % (opt.secs, os.path.join(d, tag + "_end.ppm"))
    wav = os.path.join(d, tag + ".raw")
    e["MW2_SFX_WAV"] = wav   # the mix on the simulation's clock (src/sfx.c sfx_test_clock): the sound, comparable
    if shots: e["MW2_SHOT"] = ",".join("%g:%s" % (t, os.path.join(d, "%s_%g.ppm" % (tag, t))) for t in shots)
    p = subprocess.run([opt.bin, "--sim"] + args + ["@", scene], env=e, cwd=d, capture_output=True, text=True,
                       errors="replace", timeout=opt.timeout)
    out = p.stdout.splitlines()
    sim = [l for l in out if l.startswith("SIMHASH ")]
    snd = [l for l in p.stderr.splitlines() if l.startswith("SFXLOG ")]
    sw = [l for l in out if l.startswith("LOOKSWITCH ") or l.startswith("video mode:")]
    lt = [l for l in p.stderr.splitlines() if l.startswith("LTRACK ")]
    os.remove(cfg)
    h = None
    if os.path.exists(wav):
        import hashlib
        with open(wav, "rb") as f: h = (os.path.getsize(wav), hashlib.sha1(f.read()).hexdigest())
        os.remove(wav)
    return dict(rc=p.returncode, sim=sim, snd=snd, wav=h, sw=sw, lt=lt, dir=d, tag=tag, err=p.stderr[-2000:] if p.returncode else "")


def ppm(path):
    with open(path, "rb") as f: b = f.read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", b)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, b[m.end():m.end() + w * h * 3]


def frame_diff(a, b):
    """(pixels differing, total, the largest channel difference)"""
    wa, ha, pa = ppm(a); wb, hb, pb = ppm(b)
    if (wa, ha) != (wb, hb): return (-1, 0, 255)
    n = wa * ha; bad = 0; mx = 0
    if pa == pb: return (0, n, 0)
    for i in range(0, n * 3, 3):
        d = max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2]))
        if d: bad += 1; mx = max(mx, d)
    return (bad, n, mx)


def first_diff(x, y):
    for i, (p, q) in enumerate(zip(x, y)):
        if p != q: return "line %d: %s | %s" % (i, p, q)
    return None if len(x) == len(y) else "length %d vs %d" % (len(x), len(y))


def plan(opt):
    segs = []   # (look, from t, to t)
    sw = [(float(t), l) for t, l in (s.split(":") for s in opt.switch.split(","))]
    t0, cur = 0.0, opt.start
    for t, l in sw:
        segs.append((cur, t0, t)); t0, cur = t, l
    segs.append((cur, t0, opt.secs))
    return sw, segs


def scene_job(opt, shim, scene):
    sw, segs = plan(opt)
    shots = {}
    for look, a, b in segs[1:]:
        shots.setdefault(look, []).extend([round(a + 0.05, 3), round(b - 0.2, 3)])
    allshots = sorted(t for v in shots.values() for t in v)
    A = run(opt, shim, scene, opt.start, "A", {}, allshots)
    B = run(opt, shim, scene, opt.start, "B", {"MW2_TEST_LOOKSWITCH": opt.switch}, allshots)
    refs = {}
    for look in shots:
        if look == opt.start: refs[look] = A; continue
        refs[look] = run(opt, shim, scene, look, "R_" + look, {}, shots[look])
    res = dict(scene=scene, rc=[A["rc"], B["rc"]] + [r["rc"] for r in refs.values()], frames=[], sim=len(A["sim"]))
    res["sim_ab"] = first_diff(A["sim"], B["sim"])
    res["snd_ab"] = first_diff(A["snd"], B["snd"]) or (None if A["wav"] == B["wav"] and A["wav"] else "the mix: %s vs %s" % (A["wav"], B["wav"]))
    res["sim_ref"] = {l: first_diff(A["sim"], r["sim"]) for l, r in refs.items() if r is not A}
    res["switched"] = [l for l in B["sw"] if l.startswith("video mode:")]
    res["errs"] = [x["err"] for x in [A, B] + list(refs.values()) if x["err"]]
    for look, ts in shots.items():
        R = refs[look]
        for t in ts:
            pa = os.path.join(B["dir"], "B_%g.ppm" % t)
            pr = os.path.join(R["dir"], "%s_%g.ppm" % (R["tag"], t))
            if not (os.path.exists(pa) and os.path.exists(pr)): res["frames"].append((look, t, None)); continue
            res["frames"].append((look, t, frame_diff(pa, pr)))
    if not opt.keep: shutil.rmtree(os.path.join(opt.out, scene), ignore_errors=True)
    return res


def repeat_job(opt, shim):
    seq = ["titanium", "enhanced", "dos"]
    n = opt.repeat
    sw = ",".join("%g:%s" % (1.0 + 0.5 * i, seq[i % 3]) for i in range(n))
    secs = 1.0 + 0.5 * n + 0.5
    o2 = argparse.Namespace(**vars(opt)); o2.secs = secs
    B = run(o2, shim, opt.repeat_scene, "dos", "REP", {"MW2_TEST_LOOKSWITCH": sw}, [])
    A = run(o2, shim, opt.repeat_scene, "dos", "REPA", {}, [])
    own = os.path.basename(opt.bin)
    mods = {}
    for l in B["lt"]:
        m = re.match(r"LTRACK (\d+) (\S+) (-?\d+)", l)
        if m: mods.setdefault(int(m.group(1)), {})[m.group(2)] = int(m.group(3))
    rows = []
    for l in B["sw"]:
        m = re.match(r"LOOKSWITCH (\d+) asked (\w+) now (\w+) textures (\d+) buffers (\d+) gpu (\d+) heap (-?\d+)", l)
        if m:
            t = int(m.group(1)); md = mods.get(t, {})
            rows.append((t, m.group(3), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7)),
                         md.get(own, -1), sum(v for k, v in md.items() if k != own) if md else -1))
    if not opt.keep: shutil.rmtree(os.path.join(opt.out, opt.repeat_scene), ignore_errors=True)
    return rows, first_diff(A["sim"], B["sim"]), len(A["sim"]), B["rc"], A["rc"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--game", required=True, help="the GBL-game folder (install/, cd/, titanium/)")
    ap.add_argument("--scenes", default="", help="comma list (default: every *SCN1.BWD in install/; none: only --repeat)")
    ap.add_argument("--start", default="dos", choices=LOOKS)
    ap.add_argument("--switch", default="2:titanium,4:enhanced,6:dos", help="t:look,... (autopilot seconds)")
    ap.add_argument("--secs", type=float, default=8.0)
    ap.add_argument("--size", default="640x480")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--out", default=os.path.join(tempfile.gettempdir(), "lookswitch"))
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--repeat", type=int, default=0, help="also: N switches in one mission (resource counts)")
    ap.add_argument("--repeat-scene", default="BASSSCN1")
    ap.add_argument("--keep", action="store_true", help="keep the frames")
    opt = ap.parse_args()
    opt.bin = os.path.abspath(opt.bin); opt.game = os.path.abspath(opt.game)
    os.makedirs(opt.out, exist_ok=True)
    shim = os.path.join(opt.out, "fixtime.so")
    with open(shim + ".c", "w") as f: f.write(SHIM)
    subprocess.run(["cc", "-shared", "-fPIC", "-O1", shim + ".c", "-o", shim], check=True)
    opt.tracker = None
    if opt.repeat:
        opt.tracker = os.path.join(opt.out, "ltrack.so")
        with open(opt.tracker + ".c", "w") as f: f.write(TRACKER)
        if subprocess.run(["cc", "-shared", "-fPIC", "-O2", opt.tracker + ".c", "-o", opt.tracker, "-ldl", "-lpthread"]).returncode: opt.tracker = None
    scenes = [] if opt.scenes == "none" else opt.scenes.split(",") if opt.scenes else sorted(os.path.basename(p)[:-4].upper() for p in glob.glob(opt.game + "/install/*SCN1.BWD"))
    fails = 0
    if scenes:
        print("%-9s %6s  %-6s %-6s %-26s  frames after the switches (look@t: differing pixels / max channel difference)" % ("scene", "frames", "sim", "sound", "refs' sim"))
        with cf.ThreadPoolExecutor(max_workers=max(1, opt.jobs)) as ex:
            for r in ex.map(lambda s: scene_job(opt, shim, s), scenes):
                ok = all(c == 0 for c in r["rc"]) and not r["sim_ab"] and not r["snd_ab"] and all(v is None for v in r["sim_ref"].values()) \
                     and all(fd and fd[0] == 0 for _, _, fd in r["frames"]) and len(r["switched"]) == len(opt.switch.split(","))
                fails += not ok
                fr = " ".join("%s@%g:%s" % (l[:3], t, "none" if fd is None else "=" if fd[0] == 0 else "%d/%d" % (fd[0], fd[2])) for l, t, fd in r["frames"])
                refs = ",".join("%s %s" % (l[:3], "=" if v is None else "DIFF") for l, v in r["sim_ref"].items())
                print("%-9s %6d  %-6s %-6s %-26s  %s%s" % (r["scene"], r["sim"], "=" if not r["sim_ab"] else "DIFF", "=" if not r["snd_ab"] else "DIFF",
                                                      refs, fr, "" if ok else "  FAIL"), flush=True)
                for k in ("sim_ab", "snd_ab"):
                    if r[k]: print("    %s: %s" % (k, r[k][:300]))
                for l, v in r["sim_ref"].items():
                    if v: print("    sim ref %s: %s" % (l, v[:300]))
                if len(r["switched"]) != len(opt.switch.split(",")): print("    switches: %s" % r["switched"])
                for e in r["errs"]: print("    rc != 0: " + e.replace("\n", "\n      ")[-600:])
    if opt.repeat:
        rows, simd, nsim, rcb, rca = repeat_job(opt, shim)
        print("\n%d switches in %s (sim %s over %d frames vs the mission without switches; rc %d / %d):" % (len(rows), opt.repeat_scene, "=" if not simd else "DIFF " + simd, nsim, rcb, rca))
        print("  %7s %-9s %8s %7s %6s %12s %12s %12s" % ("ms", "look", "textures", "buffers", "gpu", "heap", "game's heap", "libraries'"))
        for row in rows: print("  %7d %-9s %8d %7d %6d %12d %12d %12d" % row)
        by = {}
        for row in rows: by.setdefault(row[1], []).append(row)
        grow = []
        for l, rs in by.items():
            rs = rs[1:]   # from the second visit on
            if len(rs) >= 2:
                if len({(x[2], x[3], x[4]) for x in rs}) != 1: grow.append("%s: counts change" % l)
                hg = rs[-1][5] - rs[0][5]
                og = rs[-1][6] - rs[0][6] if rs[0][6] >= 0 else None
                lg = rs[-1][7] - rs[0][7] if rs[0][7] >= 0 else None
                print("  %-9s from its 2nd switch on: textures %s buffers %s gpu %s; heap %+d bytes over %d switches%s" % (
                    l, sorted({x[2] for x in rs}), sorted({x[3] for x in rs}), sorted({x[4] for x in rs}), hg, len(rs) - 1,
                    "" if og is None else " (the game's own %+d, the libraries' - graphics driver - %+d)" % (og, lg)))
                # the game's own allocations must not grow (the graphics driver's caches are its own business; the
                # GPU objects the game holds are the counts above)
                if og is not None and og > 256 << 10: grow.append("%s: the game's heap +%d" % (l, og))
                if og is None and hg > 16 << 20: grow.append("%s: heap +%d" % (l, hg))
        if grow or simd or rcb or len(rows) != opt.repeat: fails += 1; print("  FAIL: %s" % ("; ".join(grow) or simd or "rc / switches"))
    print("\n%s" % ("all equal" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
