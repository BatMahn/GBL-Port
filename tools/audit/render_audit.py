#!/usr/bin/env python3
"""DOS-look render audit: the original GBL (DOSBox) against the port, frame by frame, over every GBL scene.

Each pose moves the scene's start nav point (<SCENE>ST01) in a copy of MW2.PRJ; DOSBox and the port both read that
same copy, so the start pose is the same in both. One run per pose and size gives three frames: level, torso tilted
up (Down held 3 s, the servo at its stop) and down (Up held 6 s). The port renders headless (lavapipe) with
MW2_TEST_SPARSE (only the frames it writes are drawn), several candidate times per DOS frame (the DOS frame's
mission time is only known to a second or two); the best candidate is compared.

  render_audit.py plan   --out D                      poses (D/poses.json)
  render_audit.py dos    --out D [--only PAT]         DOSBox captures (serialised by /tmp/dosbox.lock)
  render_audit.py port   --out D --tag T [--bin B]    port frames (D/port_T)
  render_audit.py masks  --out D --tag T              HUD / cockpit masks from the port (per size and tilt)
  render_audit.py cmp    --out D --tag T              diffs, clusters, classes (D/cmp_T.tsv, D/clu_T.tsv, D/map_T/)
  render_audit.py report --out D --tag T [--base T0]  ranked clusters and the per-scene table (D/report_T.md)
  (cmp --only PAT --part NAME writes a part of the tables, for parallel runs; report joins the parts)

Data (no game data in the repo): --game DIR holds install/ (MW2.PRJ, the BWDs), cd/; DOSBox installs come from
--dosinstall; the DOSBox runner (--dosrun: lines sleep / key / down / up / shot) and its conf (--dosconf, whose
"mount c /tmp/gbldos" and "MW2 BASSSCN1" lines are replaced) are the project's DOSBox harness. --mkstar writes the
player's star (both sides use the same).

Clusters: pixels whose |dR|+|dG|+|dB| >= 12 (the threshold of the earlier eq.py), closed 3x3, 8-connected, >= 20
px. Auto classes (first that applies): hud (>= 50 % inside the port's HUD mask, or a message bar: not rendering), timing
(the port's candidates or the two DOS level frames disagree over >= 30 % of it: units, effects, shake; or the frame
differs over > 30 %: the simulation went elsewhere), edge (>= 70 % of its
pixels match within one pixel shift: polygon edge placement), shade (mean colour distance < 40 and the same edge
structure), else struct (geometry / clip / order / texture: looked at by hand, labels in D/labels.tsv).
"""
import argparse, glob, json, math, os, shutil, struct, subprocess, sys, time

SIZES = {'1024': (1024, 768), '640': (640, 480)}
# k: (forward m along the start heading, right m, heading offset in degrees); sizes
POSES = [(0, 0, 0), (0, 0, 90), (0, 0, 180), (0, 0, 270), (300, 0, 0), (0, 300, 45)]
POSE_SIZES = {0: ('1024', '640'), 1: ('1024',), 2: ('1024', '640'), 3: ('1024',), 4: ('1024', '640'), 5: ('1024',)}
PITCH = ('l', 'u', 'd')
# DOS script: level twice, then the tilt (after the start-up's 6-8 s)
DOS_SCRIPT = 'sleep 14\nshot {n}_l\nsleep 2\nshot {n}_l2\nsleep 6\ndown Down\nsleep 3\nup Down\nsleep 0.5\nshot {n}_u\n' \
             'down Up\nsleep 6\nup Up\nsleep 0.5\nshot {n}_d\n'
# DOSBox's frames come about 10 s into the mission (FRANSCN1's drop from the dropship ends at 9 s in both); the tilt keys
# then follow the DOS script's timing, the candidates bracket it
PORT_T = {'l': [7, 8, 9, 10, 11, 12, 13], 'u': [19.5, 20, 20.5, 21, 21.5], 'd': [26, 27, 28, 29, 30]}
PORT_HOLD = '18-21:down,21.6-27.6:up'
PORT_SECS = 30.1


def prj_records(d):
    """(name, payload offset, payload) of every BWD record"""
    n = struct.unpack_from('<H', d, 24)[0]
    for i in range(n):
        tag = d[26 + i * 24:30 + i * 24].rstrip(b'\0').decode()
        io, isz, so, ssz = struct.unpack_from('<4I', d, 30 + i * 24)
        if tag != 'BWD':
            continue
        cap = struct.unpack_from('<H', d, io + 16)[0]
        scap, scnt = struct.unpack_from('<HH', d, so + 16)
        syms = {}
        for k in range(scnt):
            e = so + 22 + k * 18
            syms[struct.unpack_from('<H', d, e + 16)[0]] = d[e:e + 16].split(b'\0')[0].decode('latin1')
        for k in range(cap):
            o, s = struct.unpack_from('<2I', d, io + 22 + k * 8)
            if o and s >= 62:
                yield syms.get(k, ''), o + 62, d[o + 62:o + s]


def navp_offset(d, name):
    for nm, po, p in prj_records(d):
        if nm != name or p[:4] != b'BWD\0':
            continue
        o = 12
        while o + 8 <= len(p):
            tag, sz = p[o:o + 4], struct.unpack_from('<I', p, o + 4)[0]
            if sz < 8:
                break
            if tag == b'NAVP':
                return po + o + 8
            o += sz
    return None


def patch_prj(src, dst, off, x, y, z, h):
    d = bytearray(open(src, 'rb').read())
    hh = (h + 180.0) % 360.0 - 180.0
    d[off:off + 16] = struct.pack('<iiii', int(x), int(y), int(z), int(round(hh * 65536)))
    hd = d.rfind(b'DATA', 0, off)
    n = struct.unpack('<I', d[hd + 4:hd + 8])[0] + 8
    d[hd + 8:hd + 12] = struct.pack('<I', sum(d[hd + 12:hd + n]) & 0xffffffff)
    open(dst, 'wb').write(d)


def cmd_plan(a):
    prj = os.path.join(a.game, 'install', 'MW2.PRJ')
    d = open(prj, 'rb').read()
    scenes = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(a.game, 'install', '*SCN1.BWD')))
    poses = []
    for sc in scenes:
        off = navp_offset(d, sc[:4] + 'ST01')
        x, y, z, h = struct.unpack_from('<iiii', d, off)
        h /= 65536.0
        for k, (fw, rt, dh) in enumerate(POSES):
            r = math.radians(h)   # forward (sin h, cos h), right (cos h, -sin h): only a placement rule
            px = x + 100 * (fw * math.sin(r) + rt * math.cos(r))
            pz = z + 100 * (fw * math.cos(r) - rt * math.sin(r))
            for sz in POSE_SIZES[k]:
                poses.append(dict(id='%s_k%d_%s' % (sc[:4], k, sz), scene=sc, k=k, size=sz, off=off,
                                  x=int(px), y=y, z=int(pz), h=h + dh))
    os.makedirs(a.out, exist_ok=True)
    json.dump(poses, open(os.path.join(a.out, 'poses.json'), 'w'), indent=0)
    print('%d scenes, %d pose x size runs' % (len(scenes), len(poses)))


def load_poses(a):
    p = json.load(open(os.path.join(a.out, 'poses.json')))
    if a.only:
        import fnmatch
        p = [q for q in p if fnmatch.fnmatch(q['id'], a.only)]
    return p


def pose_prj(a, q):
    f = os.path.join(a.out, 'prj', q['id'][:-len(q['size']) - 1] + '.PRJ')
    if not os.path.exists(f):
        os.makedirs(os.path.dirname(f), exist_ok=True)
        patch_prj(os.path.join(a.game, 'install', 'MW2.PRJ'), f, q['off'], q['x'], q['y'], q['z'], q['h'])
    return f


def drop_prj(a, q):
    """the pose's PRJ copy (30 MB) goes once its runs are made; pose_prj makes it again when needed"""
    f = os.path.join(a.out, 'prj', q['id'][:-len(q['size']) - 1] + '.PRJ')
    if os.environ.get('AUDIT_KEEP_PRJ') or not os.path.exists(f):
        return
    for p in json.load(open(os.path.join(a.out, 'poses.json'))):
        if p['id'][:-len(p['size']) - 1] == q['id'][:-len(q['size']) - 1]:
            if not os.path.exists(os.path.join(a.out, 'dos', '%s_d.png' % p['id'])):
                return
            if not glob.glob(os.path.join(a.out, 'port_*', '%s_end.png' % p['id'])):
                return
    os.remove(f)


def cmd_dos(a):
    od = os.path.join(a.out, 'dos')
    os.makedirs(od, exist_ok=True)
    for q in load_poses(a):
        n = q['id']
        if all(os.path.exists(os.path.join(od, '%s_%s.png' % (n, p))) for p in ('l', 'l2', 'u', 'd')):
            continue
        prj = pose_prj(a, q)
        w, h = SIZES[q['size']]
        res = '768' if q['size'] == '1024' else '480'
        work = '/tmp/gbldos_audit'
        shutil.rmtree(work, ignore_errors=True)
        shutil.copytree(a.dosinstall, work)
        shutil.copy(prj, os.path.join(work, 'MW2.PRJ'))
        subprocess.run([a.mkstar, 'MW2.PRJ', '.'], cwd=work, check=True)
        c = bytearray(open(os.path.join(work, 'MW2SND.CFG'), 'rb').read())   # the video driver (setres)
        nm = {'480': b'vesa480.dll', '768': b'vesa768.dll'}[res]
        c[0x2c:0x3c] = nm + bytes(16 - len(nm))
        open(os.path.join(work, 'MW2SND.CFG'), 'wb').write(c)
        conf = '/tmp/gbl_audit.conf'
        txt = open(a.dosconf).read().replace('mount c /tmp/gbldos', 'mount c ' + work)
        txt = '\n'.join('MW2 ' + q['scene'] if l.strip() == 'MW2 BASSSCN1' else l for l in txt.split('\n'))
        open(conf, 'w').write(txt)
        script = '/tmp/gbl_audit.ds'
        open(script, 'w').write(DOS_SCRIPT.format(n='audit'))
        t0 = time.time()
        subprocess.run(['flock', '/tmp/dosbox.lock', 'env', 'DOSCONF=' + conf, 'timeout', '200', a.dosrun, script],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for p in ('l', 'l2', 'u', 'd'):
            src = '/tmp/ds_audit_%s.png' % p
            if os.path.exists(src):
                subprocess.run(['convert', src, '-crop', '%dx%d+0+0' % (w, h), '+repage',
                                os.path.join(od, '%s_%s.png' % (n, p))])
                os.remove(src)
        shutil.rmtree(work, ignore_errors=True)
        print('%s %.0f s' % (n, time.time() - t0), flush=True)
        drop_prj(a, q)


def port_env(a, q, extra=None):
    g = a.game
    e = dict(os.environ)
    e.pop('MW2_NO_STARTUP', None)
    e.update(MW2_INSTALL_DIR=os.path.join(g, 'install'), MW2_CD_DIR=os.path.join(g, 'cd'), MW2_CD_DATA='GBL',
             SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy', VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json',
             MW2_SIZE='%dx%d' % SIZES[q['size']], MW2_EDITION='dos', MW2_TEST_SPARSE='1', MW2_HOLD=PORT_HOLD)
    if extra:
        e.update(extra)
    return e


def run_port(a, q, od, extra=None, times=None, secs=PORT_SECS):
    prj = pose_prj(a, q)
    shots = []
    for p, ts in (times or PORT_T).items():
        for t in ts:
            shots.append('%g:%s/%s_%s_%g.ppm' % (t, od, q['id'], p, t))
    e = port_env(a, q, extra)
    e['MW2_SHOT'] = ','.join(shots)
    e['MW2_AUTOPILOT'] = '%g,0,0,0,%s/%s_end.ppm,1' % (secs, od, q['id'])
    with open('%s/%s.log' % (od, q['id']), 'w') as lf:
        subprocess.run(['timeout', '600', a.bin, '--sim', prj, prj, 'dos', '@', q['scene']], cwd=a.game, env=e,
                       stdout=lf, stderr=subprocess.STDOUT)
    for f in glob.glob('%s/%s_*.ppm' % (od, q['id'])):   # keep png (small)
        subprocess.run(['convert', f, f[:-4] + '.png'])
        os.remove(f)


def cmd_port(a):
    od = os.path.join(a.out, 'port_' + a.tag)
    os.makedirs(od, exist_ok=True)
    from concurrent.futures import ThreadPoolExecutor
    todo = [q for q in load_poses(a) if not os.path.exists('%s/%s_end.png' % (od, q['id']))]

    def one(q):
        t0 = time.time()
        run_port(a, q, od)
        print('%s %.0f s' % (q['id'], time.time() - t0), flush=True)
        drop_prj(a, q)
    with ThreadPoolExecutor(a.jobs) as ex:
        list(ex.map(one, todo))


def cmd_masks(a):
    """per size and tilt: HUD = normal vs MW2_TEST_NOHUD; cockpit = NOHUD vs NOHUD + MW2_COCKPIT_FRAME=-1 (one scene)"""
    import numpy as np
    from PIL import Image
    od = os.path.join(a.out, 'masks')
    os.makedirs(od, exist_ok=True)
    T = {'l': [4], 'u': [14], 'd': [22.5]}
    for sz in SIZES:
        q = [p for p in load_poses(argparse.Namespace(out=a.out, only=None)) if p['size'] == sz and p['k'] == 0
             and p['scene'] == a.mask_scene][0]
        for v, ex in (('n', {}), ('h', {'MW2_TEST_NOHUD': '1'}), ('c', {'MW2_TEST_NOHUD': '1', 'MW2_COCKPIT_FRAME': '-1'})):
            vd = os.path.join(od, v)
            os.makedirs(vd, exist_ok=True)
            if not os.path.exists('%s/%s_end.png' % (vd, q['id'])):
                run_port(a, q, vd, ex, T)
        for p, ts in T.items():
            L = lambda v: np.asarray(Image.open('%s/%s/%s_%s_%g.png' % (od, v, q['id'], p, ts[0])).convert('RGB')).astype(int)
            n, h, c = L('n'), L('h'), L('c')
            hud = np.abs(n - h).sum(2) > 0
            pit = np.abs(h - c).sum(2) > 0
            Image.fromarray((hud * 255).astype(np.uint8)).save('%s/hud_%s_%s.png' % (od, sz, p))
            Image.fromarray((pit * 255).astype(np.uint8)).save('%s/pit_%s_%s.png' % (od, sz, p))
            print(sz, p, 'hud %.1f %%, cockpit %.1f %%' % (100 * hud.mean(), 100 * pit.mean()))


def cmd_cmp(a):
    import numpy as np
    from PIL import Image, ImageDraw
    from scipy import ndimage
    od = os.path.join(a.out, 'port_' + a.tag)
    md = os.path.join(a.out, 'map_' + a.tag)
    os.makedirs(md, exist_ok=True)
    L = lambda f: np.asarray(Image.open(f).convert('RGB')).astype(np.int16)
    masks = {}
    for sz in SIZES:
        for p in PITCH:
            def mk(kind):
                f = os.path.join(a.out, 'masks', '%s_%s_%s.png' % (kind, sz, p))
                m = np.asarray(Image.open(f)) > 0 if os.path.exists(f) else np.zeros(SIZES[sz][::-1], bool)
                return ndimage.binary_dilation(m, iterations=3)
            masks[sz, p] = (mk('hud'), mk('pit'))
    labels = {}
    lf = os.path.join(a.out, 'labels.tsv')
    if os.path.exists(lf):
        for l in open(lf):
            f = l.rstrip('\n').split('\t')
            if len(f) >= 3 and not l.startswith('#'):
                labels[f[0], f[1]] = f[2:]
    rows, clus = [], []
    for q in load_poses(a):
        for p in PITCH:
            df = os.path.join(a.out, 'dos', '%s_%s.png' % (q['id'], p))
            cands = sorted(glob.glob('%s/%s_%s_*.png' % (od, q['id'], p)))
            if not os.path.exists(df) or not cands:
                continue
            dimg = L(df)
            best, bs, imgs = None, -1, []
            for c in cands:
                im = L(c)
                if im.shape != dimg.shape:
                    continue
                s = (np.abs(dimg - im).sum(2) < 12).mean()
                imgs.append(im)
                if s > bs:
                    best, bs, bimg = c, s, im
            if best is None:
                continue
            diff = np.abs(dimg - bimg).sum(2) >= 12
            var = np.zeros_like(diff)
            for im in imgs:
                var |= np.abs(im - bimg).sum(2) >= 12
            if p == 'l' and os.path.exists(df.replace('_l.png', '_l2.png')):
                d2 = L(df.replace('_l.png', '_l2.png'))
                var |= np.abs(d2 - dimg).sum(2) >= 12
            # one-pixel shift test: the port within one pixel of the DOS colour
            sh = np.ones_like(diff)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    s2 = np.roll(np.roll(bimg, dy, 0), dx, 1)
                    sh &= np.abs(dimg - s2).sum(2) >= 12
            hud, pit = masks[q['size'], p]
            diverged = diff.mean() > 0.3   # the simulation went elsewhere (the player dead, another state): no rendering verdict
            lab, n = ndimage.label(ndimage.binary_closing(diff, np.ones((3, 3))), np.ones((3, 3)))
            objs = ndimage.find_objects(lab)
            frame_cl = []
            for i, sl in enumerate(objs):
                reg = (lab[sl] == i + 1) & diff[sl]
                npx = int(reg.sum())
                if npx < 20:
                    continue
                f_hud = float(hud[sl][reg].mean())
                f_var = float(var[sl][reg].mean())
                f_edge = 1.0 - float(sh[sl][reg].mean())
                f_pit = float(pit[sl][reg].mean())
                cd = float(np.abs(dimg[sl][reg] - bimg[sl][reg]).sum(1).mean())
                H, W = diff.shape
                bar = sl[1].stop - sl[1].start >= 0.9 * W and (sl[0].stop <= 0.07 * H or sl[0].start >= 0.93 * H)
                cls = 'hud' if f_hud >= 0.5 or bar else 'timing' if f_var >= 0.3 or diverged else 'edge' if f_edge >= 0.7 else \
                    'shade' if cd < 40 else 'struct'
                bb = '%d,%d,%d,%d' % (sl[1].start, sl[0].start, sl[1].stop - sl[1].start, sl[0].stop - sl[0].start)
                key = ('%s_%s' % (q['id'], p), bb)
                lab_ = labels.get(key, ['', ''])
                frame_cl.append((npx, bb, cls, f_hud, f_var, f_edge, f_pit, cd, lab_))
                dm, pm = dimg[sl][reg].mean(0), bimg[sl][reg].mean(0)
                clus.append(dict(frame='%s_%s' % (q['id'], p), scene=q['scene'], pose=q['id'], pitch=p, px=npx, bbox=bb,
                                 cls=cls, hud=round(f_hud, 2), var=round(f_var, 2), edge=round(f_edge, 2),
                                 pit=round(f_pit, 2), cdist=round(cd), dos_rgb='%d/%d/%d' % tuple(dm),
                                 port_rgb='%d/%d/%d' % tuple(pm), label=lab_[0], note=lab_[1] if len(lab_) > 1 else ''))
            eq = 100.0 * (1 - diff.mean())
            by = {}
            for c in frame_cl:
                by[c[2]] = by.get(c[2], 0) + c[0]
            rows.append(dict(frame='%s_%s' % (q['id'], p), scene=q['scene'], pose=q['id'], pitch=p, eq=round(eq, 2),
                             best=os.path.basename(best), clusters=len(frame_cl), **{'px_' + k: by.get(k, 0) for k in
                                                                                     ('hud', 'timing', 'edge', 'shade', 'struct')}))
            # the map: DOS | port | diff (red: struct / shade, yellow: edge, blue: timing, grey: hud), half size
            dm_ = (dimg * 0.35).astype(np.uint8)
            col = {'hud': (128, 128, 128), 'timing': (60, 120, 255), 'edge': (255, 220, 0), 'shade': (255, 120, 0),
                   'struct': (255, 0, 0)}
            for npx, bb, cls, *_ in frame_cl:
                x, y, w, h = map(int, bb.split(','))
                sub = diff[y:y + h, x:x + w]
                dm_[y:y + h, x:x + w][sub] = col[cls]
            im = Image.fromarray(np.concatenate([dimg.astype(np.uint8), bimg.astype(np.uint8), dm_], 1))
            dr = ImageDraw.Draw(im)
            W = dimg.shape[1]
            for npx, bb, cls, *_ in frame_cl:
                if cls in ('struct', 'shade'):
                    x, y, w, h = map(int, bb.split(','))
                    dr.rectangle([2 * W + x, y, 2 * W + x + w, y + h], outline=col[cls])
            im.resize((im.width // 2, im.height // 2)).save(os.path.join(md, '%s_%s.png' % (q['id'], p)))
            print('%s_%s %.2f %% %s' % (q['id'], p, eq, os.path.basename(best)), flush=True)
    def wr(fn, rs):
        if not rs:
            return
        with open(fn, 'w') as f:
            f.write('\t'.join(rs[0].keys()) + '\n')
            for r in rs:
                f.write('\t'.join(str(v) for v in r.values()) + '\n')
    part = '_' + a.part if a.part else ''
    wr(os.path.join(a.out, 'cmp_%s%s.tsv' % (a.tag, part)), rows)
    wr(os.path.join(a.out, 'clu_%s%s.tsv' % (a.tag, part)), sorted(clus, key=lambda c: -c['px']))


def rd_tsv(fn):
    """a cmp / clu table, or its parts (cmp --part) joined"""
    fs = [fn] if os.path.exists(fn) else sorted(glob.glob(fn[:-4] + '_*.tsv'))
    out = []
    for f in fs:
        ls = open(f).read().split('\n')
        h = ls[0].split('\t')
        out += [dict(zip(h, l.split('\t'))) for l in ls[1:] if l]
    return out


def cmd_report(a):
    rows = rd_tsv(os.path.join(a.out, 'cmp_%s.tsv' % a.tag))
    clus = sorted(rd_tsv(os.path.join(a.out, 'clu_%s.tsv' % a.tag)), key=lambda c: -int(c['px']))
    base = {r['frame']: r for r in rd_tsv(os.path.join(a.out, 'cmp_%s.tsv' % a.base))} if a.base else {}
    eqs = [float(r['eq']) for r in rows]
    beqs = [float(base[r['frame']]['eq']) for r in rows if r['frame'] in base]
    def summ(v):
        return '%d frames, mean %.2f %%, >= 99 %%: %d, >= 95 %%: %d, < 80 %% (the simulation elsewhere): %d' % (
            len(v), sum(v) / max(len(v), 1), sum(x >= 99 for x in v), sum(x >= 95 for x in v), sum(x < 80 for x in v))
    head = ['# Render audit %s' % a.tag, '', '- %s: %s' % (a.tag, summ(eqs))]
    if beqs:
        head.append('- %s (same frames): %s' % (a.base, summ(beqs)))
    for sz in SIZES:
        v = [float(r['eq']) for r in rows if r['pose'].endswith('_' + sz)]
        bv = [float(base[r['frame']]['eq']) for r in rows if r['pose'].endswith('_' + sz) and r['frame'] in base]
        head.append('- %s: %s%s' % (sz, summ(v), ' (base mean %.2f %%)' % (sum(bv) / len(bv)) if bv else ''))
    tot = {k: sum(int(r['px_' + k]) for r in rows) for k in ('hud', 'timing', 'edge', 'shade', 'struct')}
    head.append('- differing pixels by class: ' + ', '.join('%s %d' % kv for kv in tot.items()))
    out = head + ['', '## Per scene (mean %% pixel-equal over its frames; px per class)', '',
           '| scene | frames | eq %s| hud | timing | edge | shade | struct |' % ('(base) ' if base else ''),
           '|---|---|---|---|---|---|---|---|']
    sc = {}
    for r in rows:
        sc.setdefault(r['scene'], []).append(r)
    for s in sorted(sc):
        rs = sc[s]
        eq = sum(float(r['eq']) for r in rs) / len(rs)
        bq = [float(base[r['frame']]['eq']) for r in rs if r['frame'] in base]
        tot = lambda k: sum(int(r['px_' + k]) for r in rs)
        out.append('| %s | %d | %.2f%s | %d | %d | %d | %d | %d |' % (
            s, len(rs), eq, ' (%.2f)' % (sum(bq) / len(bq)) if bq else '', tot('hud'), tot('timing'), tot('edge'),
            tot('shade'), tot('struct')))
    out += ['', '## Largest struct / shade clusters', '', '| frame | px | bbox | class | label | dos rgb | port rgb | note |',
            '|---|---|---|---|---|---|---|---|']
    for c in [c for c in clus if c['cls'] in ('struct', 'shade')][:a.top]:
        out.append('| %s | %s | %s | %s | %s | %s | %s | %s |' % (c['frame'], c['px'], c['bbox'], c['cls'], c['label'],
                                                                 c['dos_rgb'], c['port_rgb'], c['note']))
    open(os.path.join(a.out, 'report_%s.md' % a.tag), 'w').write('\n'.join(out) + '\n')
    print('\n'.join(out[:40]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd')
    ap.add_argument('--out', required=True)
    ap.add_argument('--tag', default='port')
    ap.add_argument('--base')
    ap.add_argument('--only')
    ap.add_argument('--part', help='cmp: write cmp_<tag>_<part>.tsv (parallel runs over --only subsets; report joins them)')
    ap.add_argument('--top', type=int, default=60)
    ap.add_argument('--jobs', type=int, default=1)
    ap.add_argument('--game', default=os.environ.get('AUDIT_GAME', '/tmp/AM_game'))
    ap.add_argument('--dosinstall', default=os.environ.get('AUDIT_DOSINSTALL', '/home/claude/GBL-game/install'))
    ap.add_argument('--dosrun', default=os.environ.get('AUDIT_DOSRUN', '/tmp/claude-0/s/dosrun_c.sh'))
    ap.add_argument('--dosconf', default=os.environ.get('AUDIT_DOSCONF', '/tmp/gbl_dosbox.conf'))
    ap.add_argument('--mkstar', default=os.environ.get('AUDIT_MKSTAR', '/home/claude/gbl/tools/mkstar'))
    ap.add_argument('--bin', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gbl'))
    ap.add_argument('--mask-scene', default='JESSSCN1')
    a = ap.parse_args()
    a.bin = os.path.abspath(a.bin)
    a.out = os.path.abspath(a.out)
    {'plan': cmd_plan, 'dos': cmd_dos, 'port': cmd_port, 'masks': cmd_masks, 'cmp': cmd_cmp, 'report': cmd_report}[a.cmd](a)


if __name__ == '__main__':
    main()
