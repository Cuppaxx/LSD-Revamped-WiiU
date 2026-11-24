"""models.py - parse the MOM models (TMD meshes + TOD animations) inside LBD files' MML section, reproduce how
LSD: Revamped's Unity importer turns them into GameObject hierarchies and AnimationClips, and bake them into a binary
file per dream (<out>/<STGxx>/models.bin). Imported by convert.py. Format: see MODELS_FORMAT.md next to this file
(the authoritative description; summary below).

Sources this follows (read them before changing anything):
  LSDR/Packages/com.figglewatts.lsdr.sdk/Editor/AssetImporters/LBDImporter.cs, MOMImporter.cs, MOMHelper.cs
  libLSD Formats/LBD.cs, MML.cs, MOM.cs, MOS.cs, TOD.cs, TMD.cs, Packets/TODPackets.cs
  app/src/lsd.c build_object (TMD primitives -> triangles; reproduced exactly in tmd_object_tris below)

models.bin (all integers/floats BIG-endian; str = u8 length + bytes, no terminator; no padding anywhere):
  char[4] "MODL"; u32 version (=1); u32 nmodels
  per model:
    str  key                 "<lbd path relative to the dream folder>#<mom index>", e.g. "M001.LBD#2", "N1/M024.LBD#0"
    u16  nmeshes             one per TMD object of the MOM, index = TMD object index (= Unity "MOM{i}Mesh{k}")
    per mesh: u32 ntris; ntris x tri
      tri = 3 x (f32 x,y,z, f32 u,v, u8 r,g,b,a), then u8 alpha_blend   = 73 bytes (LTri without the water byte)
    u16  nobjects            Unity GameObjects of the model, parents before children
    per object:
      u16 id                 TOD object id (0 = the animation root "0"; static MOMs: TMD index)
      i16 parent             index into this object list, -1 = child of the model root (the entity / instance root)
      str name               GameObject name ("0", "5", ...), what GetChildByName / mpose lines use
      f32 pos[3], quat[4] (x,y,z,w), scale[3]      initial local pose stored in the imported asset
      u16 nmeshes; u16 mesh index[nmeshes]         meshes drawn with this object's transform
    u16  nanims              = number of TODs = AnimatedObject.Clips count (Clips[k] = TOD k)
    per anim:
      u16 nframes            TOD frames (NumberOfFrames - 1)
      u16 length_frames      Unity clip length in frames (= latest key frame over all tracks); seconds = /20
      u16 ntracks
      per track: u16 object index; u16 nkeys; nkeys x (u16 frame; f32 pos[3], quat[4], scale[3])
                 absolute local TRS of the object after that frame's packets (Absolute/Differential resolved)
dreams.txt additions (written by convert.py through Baker.entity_lines): see MODELS_FORMAT.md.
"""
import os, struct, math

# ------------------------------------------------------------------ little-endian readers
def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]


# ------------------------------------------------------------------ TMD -> triangles (port of app/src/lsd.c build_object)
def uv_to_atlas(page, u, v):
    return float((page % 16) * 128 - 640 + u), float(v + (0 if page < 16 else 256))

def tmd_object_tris(buf, objhdr, fixp, obj_table_top):
    """Triangles of one TMD object. Each tri = ([(x,y,z,u,v,r,g,b,a) x3], alpha_blend). Returns (tris, bad)."""
    vaddr, nverts, paddr, nprims = u32(buf, objhdr), u32(buf, objhdr + 4), u32(buf, objhdr + 16), u32(buf, objhdr + 20)
    vpos = vaddr if fixp else vaddr + obj_table_top
    ppos = paddr if fixp else paddr + obj_table_top
    n = len(buf)
    if vpos + nverts * 8 > n or ppos > n: raise ValueError('TMD object out of range')
    p = ppos; bad = 0; out = []
    for _ in range(nprims):
        if p + 4 > n: raise ValueError('TMD primitive out of range')
        ilen, flag, mode = buf[p + 1], buf[p + 2], buf[p + 3]
        d = p + 4
        typ = mode >> 5
        if d + ilen * 4 > n: raise ValueError('TMD primitive data out of range')
        if typ == 1:                                            # polygon
            tex = (mode >> 2) & 1; quad = (mode >> 3) & 1; gour = (mode >> 4) & 1; ab = (mode >> 1) & 1
            unlit = flag & 1; grad = (flag >> 2) & 1
            nv = 4 if quad else 3
            uu = [0] * 4; vv = [0] * 4; page = 0; idx = [0] * 4; col = []
            q = d
            if tex:
                for k in range(nv):
                    uu[k] = buf[q]; vv[k] = buf[q + 1]
                    if k == 1: page = u16(buf, q + 2) & 0x1f
                    q += 4
            ncol = 0
            if not tex or unlit:
                ncol = (nv if gour else 1) if unlit else (nv if grad else 1)
                for k in range(ncol): col.append((buf[q], buf[q + 1], buf[q + 2])); q += 4
            if not unlit:
                if gour:
                    for k in range(nv): idx[k] = u16(buf, q + 2); q += 4
                else:
                    q += 2
                    for k in range(nv): idx[k] = u16(buf, q); q += 2
            else:
                for k in range(nv): idx[k] = u16(buf, q); q += 2
            if q - d > ilen * 4 + 2: bad += 1
            cv = []; ok = True
            for k in range(nv):
                if idx[k] >= nverts: ok = False; break
                vp = vpos + idx[k] * 8
                x, y, z = s16(buf, vp) / 2048.0, -s16(buf, vp + 2) / 2048.0, s16(buf, vp + 4) / 2048.0
                r, g, b_ = col[k if ncol > 1 else 0] if ncol else (128, 128, 128)
                tu, tv = uv_to_atlas(page, uu[k], vv[k]) if tex else (-1.0, -1.0)
                cv.append((x, y, z, tu, tv, r, g, b_, 255))
            if ok:
                for order in ((0, 1, 2), (2, 1, 3))[:2 if quad else 1]:
                    out.append(([cv[i] for i in order], ab))
        p = d + ilen * 4
    return out, bad

def parse_tmd(buf, top):
    """TMD at absolute offset top -> list of meshes (one per TMD object)."""
    if u32(buf, top) != 0x41: raise ValueError('bad TMD magic')
    fixp = u32(buf, top + 4) & 1; nobj = u32(buf, top + 8); objtop = top + 12
    meshes = []; bad = 0
    for i in range(nobj):
        t, b_ = tmd_object_tris(buf, objtop + i * 28, fixp, objtop); meshes.append(t); bad += b_
    return meshes, bad


# ------------------------------------------------------------------ TOD (libLSD TOD.cs / TODPackets.cs: packets are read sequentially by type)
ATTRIBUTE, COORDINATE, TMDDATAID, PARENTOBJECTID, MATRIXVALUE, TMDDATA, LIGHTSOURCE, CAMERA, OBJECTCONTROL = range(9)

class Packet:
    __slots__ = ('obj', 'type', 'flag', 'length', 'data')

def parse_packet(buf, o, stats):
    p = Packet()
    p.obj = u16(buf, o); tf = buf[o + 2]; p.length = buf[o + 3]; p.type = tf & 0xF; p.flag = (tf >> 4) & 0xF
    start = o; o += 4; f = p.flag
    if p.type == ATTRIBUTE: p.data = (u32(buf, o), u32(buf, o + 4)); o += 8
    elif p.type == COORDINATE:
        d = dict(diff=f & 1, rot=(f >> 1) & 1, scale=(f >> 2) & 1, trans=(f >> 3) & 1)
        if d['rot']: d['r'] = (s32(buf, o), s32(buf, o + 4), s32(buf, o + 8)); o += 12
        if d['scale']: d['s'] = (s16(buf, o), s16(buf, o + 2), s16(buf, o + 4)); o += 8
        if d['trans']: d['t'] = (s32(buf, o), s32(buf, o + 4), s32(buf, o + 8)); o += 12
        p.data = d
    elif p.type in (TMDDATAID, PARENTOBJECTID): p.data = u16(buf, o); o += 4
    elif p.type == MATRIXVALUE: o += 18 + 2 + 12; p.data = None
    elif p.type == LIGHTSOURCE: o += 16; p.data = None
    elif p.type == CAMERA:
        pos_angle = (f & 1) == 0; b2 = (f >> 2) & 1; b3 = (f >> 3) & 1
        o += (24 if b2 else 0) + (4 if b3 else 0) if pos_angle else (12 if b2 else 0) + (12 if b3 else 0); p.data = None
    elif p.type == OBJECTCONTROL: p.data = f & 1          # 0 Create, 1 Kill
    else: raise ValueError('TOD packet type %d not supported (libLSD throws)' % p.type)
    if (o - start) != p.length * 4: stats['len_mismatch'] = stats.get('len_mismatch', 0) + 1
    return p, o

def parse_tod(buf, o, stats):
    if buf[o] != 0x50: raise ValueError('bad TOD magic')
    nframes_hdr = u32(buf, o + 4); o += 8
    frames = []
    for _ in range(nframes_hdr - 1):                        # libLSD reads NumberOfFrames - 1 frames
        npk = u16(buf, o + 2); o += 8; pk = []
        for _ in range(npk):
            p, o = parse_packet(buf, o, stats); pk.append(p)
        frames.append(pk)
    return frames


# ------------------------------------------------------------------ LBD -> MML -> MOMs
def parse_lbd_moms(path):
    """List of MOM dicts {meshes: [tris per TMD object], tods: [frames]} or [] when the LBD has no MML."""
    buf = open(path, 'rb').read()
    has_mml = u16(buf, 2) == 1
    if not has_mml: return [], {}
    mml = u32(buf, 16)
    if mml + 8 > len(buf) or buf[mml:mml + 4] != b'MML ': return [], {}
    n = u32(buf, mml + 4); moms = []; stats = {}
    for i in range(n):
        top = mml + u32(buf, mml + 8 + i * 4)
        if buf[top:top + 4] != b'MOM ': raise ValueError('bad MOM magic')
        tmdoff = u32(buf, top + 8); mos = top + 12
        if buf[mos:mos + 4] != b'MOS ': raise ValueError('bad MOS magic')
        ntod = u32(buf, mos + 4)
        tods = [parse_tod(buf, mos + u32(buf, mos + 8 + k * 4), stats) for k in range(ntod)]
        meshes, bad = parse_tmd(buf, top + tmdoff)
        stats['tmd_bad'] = stats.get('tmd_bad', 0) + bad
        moms.append(dict(meshes=meshes, tods=tods))
    return moms, stats


# ------------------------------------------------------------------ Unity maths (Quaternion, Transform.Rotate)
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)

def angle_axis(deg, axis):
    h = math.radians(deg) * 0.5; s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))

def qnorm(q):
    l = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / l for c in q)

def qrot(q, v):
    x, y, z, w = q
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w)); return p[:3]

RIGHT, UP, FWD = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)

class Xf:
    """A Unity Transform's local TRS."""
    __slots__ = ('p', 'q', 's')
    def __init__(self): self.p = (0.0, 0.0, 0.0); self.q = (0.0, 0.0, 0.0, 1.0); self.s = (1.0, 1.0, 1.0)
    def copy(self):
        x = Xf(); x.p, x.q, x.s = self.p, self.q, self.s; return x

def apply_coord(xf, d):
    """MOMHelper.PoseObjectInFirstFrame / FrameTransformation.PerformTransformation (identical logic): scale, then
    translation, then rotation; Absolute replaces, Differential accumulates."""
    absolute = d['diff'] == 0
    if d['scale']:
        sc = tuple(c / 4096.0 for c in d['s'])
        xf.s = sc if absolute else tuple(a * b for a, b in zip(xf.s, sc))
    if d['trans']:
        tx, ty, tz = d['t']; t = (tx / 2048.0, -ty / 2048.0, tz / 2048.0)
        xf.p = t if absolute else tuple(a + b for a, b in zip(xf.p, t))
    if d['rot']:
        rx, ry, rz = d['r']
        pitch, yaw, roll = -rx / 4096.0, ry / 4096.0, -rz / 4096.0
        if absolute:
            xf.q = qnorm(qmul(qmul(angle_axis(pitch, RIGHT), angle_axis(yaw, UP)), angle_axis(roll, FWD)))
        else:
            # Transform.Rotate(axis, angle) with Space.Self: localRotation = localRotation * AngleAxis(angle, axis)
            q = xf.q
            for ang, ax in ((pitch, RIGHT), (yaw, UP), (roll, FWD)):
                q = qnorm(qmul(q, angle_axis(ang, ax)))
            xf.q = q


# ------------------------------------------------------------------ MOM -> Unity hierarchy + clips (MOMImporter / MOMHelper)
def build_model(mom):
    """Returns a dict: objects (list, parent-before-child), anims, warnings. Mirrors MOMImporter.ImportMOMAsset."""
    warn = []
    meshes = mom['meshes']; tods = mom['tods']
    has_anims = len(tods) > 0 and len(tods[0]) > 0
    objs = {}          # id -> dict(name, tparent (actual transform parent id or None = model root), aparent (AnimationObject.Parent id or None), meshes [tmd idx])
    if not has_anims:
        # createMomWithoutAnimations: every TMD object is a GameObject "{i}" directly under the MOM root, identity pose
        objs_l = [dict(id=i, name=str(i), parent=-1, xf=Xf(), meshes=[i]) for i in range(len(meshes))]
        return dict(objects=objs_l, anims=[], warnings=warn, static=True, paths={i: str(i) for i in range(len(meshes))})
    # object 0 = animRoot "0" (child of the MOM root)
    objs[0] = dict(name='0', tparent=None, aparent=None, meshes=[], xf=Xf(), order=0)
    order = 1
    for p in tods[0][0]:
        if p.type == OBJECTCONTROL and p.data == 0:
            if p.obj in objs and p.obj != 0: warn.append('object %d created twice' % p.obj)
            # CreateObject: new GameObject "{id}" parented to animRoot (replaces any table entry, incl. id 0)
            objs[p.obj] = dict(name=str(p.obj), tparent=0 if p.obj != 0 else None, aparent=None, meshes=[], xf=Xf(), order=order); order += 1
            if p.obj == 0: warn.append('TOD creates object 0 (replaces animRoot)')
        elif p.type == TMDDATAID:
            if p.obj not in objs: raise ValueError('TMDDataID for unknown object %d' % p.obj)
            if not (1 <= p.data <= len(meshes)): raise ValueError('TMDDataID %d out of range' % p.data)
            objs[p.obj]['meshes'].append(p.data - 1)
        elif p.type == PARENTOBJECTID:
            if p.obj not in objs or p.data not in objs: raise ValueError('ParentObjectID with unknown object %d->%d' % (p.obj, p.data))
            objs[p.obj]['tparent'] = p.data; objs[p.obj]['aparent'] = p.data
    # Unity transform path below the Animator root, and the path the clip curves are bound to (AnimationObject.GetPath)
    def tpath(i):
        o = objs[i]; return o['name'] if o['tparent'] is None else tpath(o['tparent']) + '/' + o['name']
    def apath(i):
        o = objs[i]; return o['name'] if o['aparent'] is None else apath(o['aparent']) + '/' + o['name']
    # cycle check
    for i in objs:
        seen = set(); j = i
        while j is not None:
            if j in seen: raise ValueError('parent cycle at object %d' % i)
            seen.add(j); j = objs[j]['tparent']
    # name collisions among siblings (Unity binds a path to the first child with that name); mesh GameObjects are named "{tmd index}"
    sib = {}
    for i, o in objs.items():
        sib.setdefault(o['tparent'], []).append(o['name'])
        for m in o['meshes']: sib.setdefault(i, []).append(str(m))
    for k, names in sib.items():
        if len(names) != len(set(names)): warn.append('duplicate child names under %s: %s' % (k, names))
    bound = {i: apath(i) == tpath(i) for i in objs}
    # clips: transforms are mutated in TOD order and state carries over from one TOD to the next (same Transforms)
    anims = []
    for ti, tod in enumerate(tods):
        tracks = {}
        for fn, frame in enumerate(tod):
            for p in frame:
                if p.type != COORDINATE: continue
                if p.obj not in objs: raise ValueError('coordinate packet for unknown object %d' % p.obj)
                o = objs[p.obj]; apply_coord(o['xf'], p.data)
                tracks.setdefault(p.obj, []).append((fn, o['xf'].copy()))
        unbound = sorted(i for i in tracks if not bound[i])
        if unbound: warn.append('anim %d: curves of objects %s are bound to paths that do not resolve (%s), so Unity does not animate them; dropped'
                                % (ti, unbound, ', '.join(apath(i) for i in unbound)))
        length = max((k[-1][0] for k in tracks.values()), default=0)
        dup = 0; tr = {}
        for i, keys in tracks.items():
            if not bound[i]: continue
            ded = []
            for fn, xf in keys:
                if ded and ded[-1][0] == fn: ded[-1] = (fn, xf); dup += 1
                else: ded.append((fn, xf))
            # AnimationClip.EnsureQuaternionContinuity: flip q to the hemisphere of the previous key (same rotation)
            for k in range(1, len(ded)):
                a, b = ded[k - 1][1].q, ded[k][1].q
                if sum(x * y for x, y in zip(a, b)) < 0: ded[k][1].q = tuple(-c for c in b)
            tr[i] = ded
        if dup: warn.append('anim %d: %d keys share a frame with an earlier key (kept the last)' % (ti, dup))
        anims.append(dict(nframes=len(tod), length=length, tracks=tr))
    # PoseObjectInFirstFrame runs AFTER all clips were made: it applies TOD 0 frame 0's coordinate packets on top of the
    # state the last clip left behind. This (not identity) is the pose stored in the imported asset.
    for p in tods[0][0]:
        if p.type == COORDINATE: apply_coord(objs[p.obj]['xf'], p.data)
    # flatten, parents before children
    ids = sorted(objs, key=lambda i: objs[i]['order'])
    out = []; index = {}
    def emit(i):
        if i in index: return
        o = objs[i]
        if o['tparent'] is not None: emit(o['tparent'])
        index[i] = len(out)
        out.append(dict(id=i, name=o['name'], parent=-1 if o['tparent'] is None else index[o['tparent']], xf=o['xf'], meshes=o['meshes']))
    for i in ids: emit(i)
    for a in anims:
        a['tracks'] = {index[i]: k for i, k in a['tracks'].items()}
    return dict(objects=out, anims=anims, warnings=warn, static=False, paths={index[i]: tpath(i) for i in objs})


# ------------------------------------------------------------------ dream prefab -> model instances
# The dream prefabs hold *unpacked copies* of the imported "MOM{i}MOM" GameObjects (no nested prefab instances: the only
# PrefabInstances in the dream prefabs are the LBD chunks themselves). A copy keeps the Animator (m_Controller -> the
# LBD's "MOM{i}AnimatorController" sub-asset), the AnimatedObject (Clips -> "MOM{i}MOMAnimation{k}" sub-assets, in TOD
# order as MOMImporter assigned them) and the child hierarchy "0"/<object ids>/<tmd index> with MeshFilters pointing at
# "MOM{i}Mesh{k}" sub-assets. The sub-asset fileIDs are hashes of the importer identifier only (the same fileID appears in
# every LBD), so a fileID -> MOM index table is solved from the data: LBDs with a single MOM pin index 0, the rest follows
# from clip counts == TOD counts, mesh object names (= TMD object index) and consistency across all prefabs.
AO_GUID = 'fbe69912072c42da9948ca8adc8f46ee'           # LSDR.SDK.Animation.AnimatedObject

def parse_unity_yaml(path):
    import re
    t = open(path, encoding='utf8', errors='ignore').read()
    docs = re.split(r'^--- !u!(\d+) &(-?\d+).*$', t, flags=re.M)
    return {docs[i + 1]: (int(docs[i]), docs[i + 2]) for i in range(1, len(docs), 3)}

class Scene:
    """GameObjects/Transforms of one prefab file."""
    def __init__(self, path):
        import re
        self.o = o = parse_unity_yaml(path)
        self.go = {}; self.tr = {}; self.go_tr = {}
        for id_, (c, b) in o.items():
            if c == 1:
                nm = re.search(r'm_Name: (.*)', b); act = re.search(r'm_IsActive: (\d)', b)
                self.go[id_] = dict(name=nm.group(1).strip() if nm else '', active=int(act.group(1)) if act else 1,
                                    comps=re.findall(r'component: \{fileID: (-?\d+)\}', b))
            elif c == 4:
                g = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', b)
                if not g or 'm_LocalPosition' not in b: continue
                def vec(n):
                    m = re.search(n + r': \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)(?:, w: ([-\d.e]+))?', b)
                    return tuple(float(v) for v in m.groups() if v is not None)
                fa = re.search(r'm_Father: \{fileID: (-?\d+)\}', b)
                chs = re.search(r'm_Children:((?:\s*- \{fileID: -?\d+\})*)', b)
                self.tr[id_] = dict(go=g.group(1), pos=vec('m_LocalPosition'), rot=vec('m_LocalRotation'), scale=vec('m_LocalScale'),
                                    father=fa.group(1) if fa else '0', children=re.findall(r'-?\d+', chs.group(1)) if chs else [])
                self.go_tr[g.group(1)] = id_
    def comp(self, gid, cls, guid=None):
        for c in self.go[gid]['comps']:
            if c in self.o and self.o[c][0] == cls and (guid is None or guid in self.o[c][1].split('m_EditorClassIdentifier')[0]):
                return self.o[c][1]
        return None
    def parent_go(self, gid):
        t = self.tr.get(self.go_tr.get(gid));
        if not t or t['father'] == '0' or t['father'] not in self.tr: return None
        return self.tr[t['father']]['go']
    def children(self, gid):
        t = self.tr.get(self.go_tr.get(gid))
        return [self.tr[c]['go'] for c in (t['children'] if t else []) if c in self.tr]
    def local(self, gid):
        t = self.tr[self.go_tr[gid]]; x = Xf(); x.p, x.q, x.s = t['pos'], t['rot'], t['scale']; return x

def _ref(body, field):
    import re
    m = re.search(field + r': \{fileID: (-?\d+), guid: (\w+)', body or '')
    return m.groups() if m else None

def collect_instances(scene, guid_path):
    """Every GameObject with an AnimatedObject whose clips live in an LBD -> raw instance dicts (MOM index unresolved)."""
    import re
    out = []
    for gid, g in scene.go.items():
        ao = scene.comp(gid, 114, AO_GUID)
        if ao is None: continue
        clips = re.findall(r'- \{fileID: (-?\d+), guid: (\w+)', ao.split('Clips:')[1].split('AutoPlay')[0]) if 'Clips:' in ao else []
        auto = re.search(r'AutoPlay: (-?\d+)', ao); auto = int(auto.group(1)) if auto else -1
        an = scene.comp(gid, 95); ctl = _ref(an, 'm_Controller')
        guids = set(gg for _, gg in clips) | ({ctl[1]} if ctl else set())
        lbd = [gg for gg in guids if guid_path.get(gg, '').upper().endswith('.LBD')]
        inst = dict(gid=gid, name=g['name'], clips=[c for c, _ in clips], autoplay=auto, ctl=ctl[0] if ctl else None,
                    guid=lbd[0] if len(lbd) == 1 else None, problems=[])
        if len(lbd) != 1: inst['problems'].append('clips/controller reference %d LBD files' % len(lbd))
        # mesh GameObjects below: (path below the instance root, mesh fileID, mesh name)
        meshes = []
        def walk(x, path):
            mf = scene.comp(x, 33); r = _ref(mf, 'm_Mesh')
            if r and r[1] == inst['guid']: meshes.append((path, r[0], scene.go[x]['name']))
            for c in scene.children(x): walk(c, (path + '/' if path else '') + scene.go[c]['name'])
        walk(gid, '')
        inst['meshes'] = meshes
        out.append(inst)
    return out

def solve_tables(all_insts, lbd_moms):
    """all_insts: instances of every prefab (with 'guid'); lbd_moms: guid -> list of MOM dicts.
    Returns (ctl_table fileID->mom index, mesh_table fileID->(mom, tmd index), clip_table fileID->(mom, k), problems)."""
    ctl, mesh, clip = {}, {}, {}; problems = []
    def cands(inst):
        moms = lbd_moms.get(inst['guid'], [])
        res = []
        for i, m in enumerate(moms):
            if len(m['tods']) != len(inst['clips']): continue
            if any(not nm.isdigit() or int(nm) >= len(m['meshes']) for _, _, nm in inst['meshes']): continue
            if inst['ctl'] in ctl and ctl[inst['ctl']] != i: continue
            if any(f in mesh and mesh[f] != (i, int(nm)) for _, f, nm in inst['meshes']): continue
            if any(f in clip and clip[f] != (i, k) for k, f in enumerate(inst['clips'])): continue
            res.append(i)
        return res
    changed = True
    while changed:
        changed = False
        for inst in all_insts:
            if inst['guid'] is None or 'mom' in inst: continue
            c = cands(inst)
            if len(c) == 1:
                i = inst['mom'] = c[0]; changed = True
                if inst['ctl']: ctl[inst['ctl']] = i
                for _, f, nm in inst['meshes']: mesh[f] = (i, int(nm))
                for k, f in enumerate(inst['clips']): clip[f] = (i, k)
    for inst in all_insts:
        if 'mom' not in inst and inst['guid'] is not None:
            c = cands(inst); inst['problems'].append('MOM index ambiguous/unresolved, candidates %s' % c)
    # global consistency: one MOM index per controller / mesh / clip fileID (the identifiers do not depend on the LBD)
    seen = {}
    for inst in all_insts:
        if 'mom' not in inst: continue
        for f, v in [(inst['ctl'], inst['mom'])] + [(f, (inst['mom'], int(nm))) for _, f, nm in inst['meshes']] + \
                    [(f, (inst['mom'], k)) for k, f in enumerate(inst['clips'])]:
            if f is None: continue
            if seen.setdefault(f, v) != v: problems.append('fileID %s maps to both %s and %s' % (f, seen[f], v))
    return ctl, mesh, clip, problems

def instance_overrides(scene, inst, model):
    """Compare the prefab copy with the imported model: per object (by Unity path below the instance root) the local
    pose and active flag stored in the prefab, where they differ from the model's initial pose. Also structural notes."""
    notes = []; over = []
    # resolve model object paths in the copy
    paths = model.get('paths') or {i: o['name'] for i, o in enumerate(model['objects'])}
    def find(path):
        x = inst['gid']
        for part in path.split('/'):
            nxt = [c for c in scene.children(x) if scene.go[c]['name'] == part]
            if not nxt: return None
            x = nxt[0]
        return x
    for i, o in enumerate(model['objects']):
        g = find(paths[i])
        if g is None: notes.append('object %s missing in prefab copy' % paths[i]); continue
        x = scene.local(g); m = o['xf']
        dq = min(sum((a - b) ** 2 for a, b in zip(x.q, m.q)), sum((a + b) ** 2 for a, b in zip(x.q, m.q)))
        diff = max(abs(a - b) for a, b in zip(x.p + x.s, m.p + m.s)) > 1e-4 or dq > 1e-7
        act = scene.go[g]['active']
        if diff or not act: over.append((o['name'], act, x))
        # meshes of this object in the copy
        want = sorted(str(k) for k in o['meshes'])
        have = sorted(scene.go[c]['name'] for c in scene.children(g) if scene.comp(c, 33) is not None)
        if want != have: notes.append('object %s meshes model %s prefab %s' % (paths[i], want, have))
        for c in scene.children(g):
            if scene.go[c]['name'] in want and not scene.go[c]['active']: notes.append('mesh %s/%s inactive in prefab' % (paths[i], scene.go[c]['name']))
    return over, notes


# ------------------------------------------------------------------ binary writer (layout: MODELS_FORMAT.md)
VERSION = 1

def _str(s):
    b = s.encode('utf8'); assert len(b) < 256, s; return struct.pack('>B', len(b)) + b

def _trs(xf):
    return struct.pack('>10f', *xf.p, *xf.q, *xf.s)

def pack_model(key, model, meshes):
    out = [_str(key), struct.pack('>H', len(meshes))]
    for tris in meshes:
        out.append(struct.pack('>I', len(tris)))
        for vs, ab in tris:
            for (x, y, z, u, v, r, g, b, a) in vs: out.append(struct.pack('>5f4B', x, y, z, u, v, r, g, b, a))
            out.append(struct.pack('>B', ab))
    out.append(struct.pack('>H', len(model['objects'])))
    for o in model['objects']:
        out.append(struct.pack('>Hh', o['id'], o['parent'])); out.append(_str(o['name'])); out.append(_trs(o['xf']))
        out.append(struct.pack('>H', len(o['meshes'])) + b''.join(struct.pack('>H', m) for m in o['meshes']))
    out.append(struct.pack('>H', len(model['anims'])))
    for a in model['anims']:
        out.append(struct.pack('>HHH', a['nframes'], a['length'], len(a['tracks'])))
        for oi in sorted(a['tracks']):
            keys = a['tracks'][oi]
            out.append(struct.pack('>HH', oi, len(keys)))
            for fn, xf in keys: out.append(struct.pack('>H', fn) + _trs(xf))
    return b''.join(out)

def write_models_bin(path, entries):
    """entries: list of (key, model, meshes)."""
    data = b'MODL' + struct.pack('>II', VERSION, len(entries)) + b''.join(pack_model(k, m, me) for k, m, me in entries)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)
    return len(data)


# ------------------------------------------------------------------ convert.py interface
def qinv(q): return (-q[0], -q[1], -q[2], q[3])

def _fmt_trs(x):
    return ' '.join('%.5f' % v for v in x.p) + ' ' + ' '.join('%.6f' % v for v in x.q) + ' ' + ' '.join('%.5f' % v for v in x.s)

class Baker:
    """Collects model instances for the entities of every dream prefab and writes models.bin per dream.
    Usage (convert.py): b = Baker(guid_path, prefab_paths); per entity: b.entity_lines(...); per dream: b.write(...)."""
    def __init__(self, guid_path, prefabs):
        self.guid_path = guid_path
        self.scenes = {}; self.insts = {}; self.lbd_moms = {}; self.built = {}
        self.used = {}           # dream folder -> {key: (abs lbd path, mom index)}
        self.report = []         # human-readable problems
        self.linked_ao = set()   # (prefab, gid) of AnimatedObject instances that were attached to an entity
        allinst = []
        for pf in prefabs:
            s = self.scenes[pf] = Scene(pf)
            ins = collect_instances(s, guid_path)
            for i in ins:
                i['prefab'] = pf
                if i['guid']: self.moms(i['guid'])
            self.insts[pf] = {i['gid']: i for i in ins}
            allinst += ins
        self.ctl, self.mesh, self.clip, probs = solve_tables(allinst, self.lbd_moms)
        self.report += probs
        for i in allinst:
            for p in i['problems']: self.report.append('%s %s: %s' % (os.path.basename(i['prefab']), i['name'], p))

    def moms(self, guid):
        if guid not in self.lbd_moms:
            try: self.lbd_moms[guid] = parse_lbd_moms(self.guid_path[guid])[0]
            except Exception as e:
                self.report.append('LBD %s: %s' % (self.guid_path[guid], e)); self.lbd_moms[guid] = []
        return self.lbd_moms[guid]

    def model(self, guid, mom):
        k = (guid, mom)
        if k not in self.built:
            m = self.built[k] = build_model(self.lbd_moms[guid][mom])
            for w in m['warnings']: self.report.append('%s#%d: %s' % (self.guid_path[guid], mom, w))
        return self.built[k]

    def _world(self, s, gid):
        """Unity-style world TRS of a GameObject (parent scale applied component-wise, like convert.py)."""
        chain = []
        g = gid
        while g is not None: chain.append(g); g = s.parent_go(g)
        w = Xf()
        for g in reversed(chain):
            l = s.local(g); sp = tuple(a * b for a, b in zip(l.p, w.s))
            n = Xf(); r = qrot(w.q, sp)
            n.p = tuple(a + b for a, b in zip(w.p, r)); n.q = qmul(w.q, l.q); n.s = tuple(a * b for a, b in zip(w.s, l.s))
            w = n
        return w

    def entity_lines(self, prefab, gid, ent_gids, ddir, folder):
        """-> (suffix for the ent line, [extra lines]). ent_gids: GameObject ids of every entity of this prefab
        (nested entities own their own models)."""
        s = self.scenes[prefab]; insts = self.insts[prefab]
        ew = self._world(s, gid)
        yaw = round(math.degrees(2 * math.atan2(ew.q[1], ew.q[3])), 2)          # exactly what convert.py writes (%.2f)
        fq = angle_axis(yaw, UP); fs = ew.s
        if max(fs) - min(fs) > 1e-4 * max(abs(v) for v in fs) and abs(abs(sum(a * b for a, b in zip(fq, ew.q))) - 1) > 1e-6:
            self.report.append('%s %s: non-uniform scale with a non-yaw rotation, model placement approximate' % (folder, s.go[gid]['name']))
        def rel(node):
            """node world TRS expressed in the runtime's entity frame T(pos) * R(yaw) * S(scale)."""
            w = self._world(s, node); r = Xf()
            iq = qinv(fq)
            d = qrot(iq, tuple(a - b for a, b in zip(w.p, ew.p)))
            r.p = tuple(a / b if b else 0.0 for a, b in zip(d, fs)); r.q = qnorm(qmul(iq, w.q)); r.s = tuple(a / b if b else 0.0 for a, b in zip(w.s, fs))
            return r
        def path_active(node):
            names = []; act = 1; g = node
            while g is not None and g != gid:
                names.append(s.go[g]['name'].replace(' ', chr(31))); act &= s.go[g]['active']; g = s.parent_go(g)
            return ('/'.join(reversed(names)) or '.'), act
        def key_of(guid, mom):
            p = self.guid_path[guid]; rk = os.path.relpath(p, ddir).replace(os.sep, '/') + '#%d' % mom
            if rk.startswith('..'): self.report.append('%s: model %s lives outside the dream folder' % (folder, rk))
            self.used.setdefault(folder, {})[rk] = (guid, mom); return rk
        models_ = []; meshes_ = []
        def walk(g, top):
            if not top and g in ent_gids: return                     # a nested entity owns its subtree
            if g in insts:
                i = insts[g]; self.linked_ao.add((prefab, g))
                if 'mom' not in i:
                    self.report.append('%s %s: AnimatedObject %s not linked (%s)' % (folder, s.go[gid]['name'], i['name'], '; '.join(i['problems']) or 'unresolved'))
                    return
                md = self.model(i['guid'], i['mom'])
                if len(i['clips']) != len(md['anims']):
                    self.report.append('%s %s: %d clips but %d TODs' % (folder, i['name'], len(i['clips']), len(md['anims'])))
                for k, f in enumerate(i['clips']):
                    if self.clip.get(f) != (i['mom'], k): self.report.append('%s %s: Clips[%d] is not TOD %d (fileID %s -> %s)' % (folder, i['name'], k, k, f, self.clip.get(f)))
                over, notes = instance_overrides(s, i, md)
                for n_ in notes: self.report.append('%s %s: %s' % (folder, i['name'], n_))
                pth, act = path_active(g)
                models_.append((key_of(i['guid'], i['mom']), pth, i['autoplay'], act, rel(g), over))
                # the model's own GameObjects are not loose meshes; other children (audio, a rider model...) are walked
                own = set(o['name'] for o in md['objects'] if o['parent'] == -1)
                for c in s.children(g):
                    if s.go[c]['name'] not in own: walk(c, False)
                return
            r = _ref(s.comp(g, 33), 'm_Mesh')
            if r and self.guid_path.get(r[1], '').upper().endswith('.LBD'):
                mk = self.mesh.get(r[0])
                moms = self.moms(r[1])
                if mk is None or mk[0] >= len(moms) or mk[1] >= len(moms[mk[0]]['meshes']):
                    self.report.append('%s %s: mesh %s (fileID %s) not resolved' % (folder, s.go[gid]['name'], s.go[g]['name'], r[0]))
                else:
                    pth, act = path_active(g)
                    meshes_.append((key_of(r[1], mk[0]), mk[1], pth, act, rel(g)))
            for c in s.children(g): walk(c, False)
        walk(gid, True)
        kids = s.children(gid)
        child0 = s.go[kids[0]]['name'].replace(' ', chr(31)) if kids else '-'
        first = models_[0][0] if models_ else meshes_[0][0] if meshes_ else '-'
        lines = []
        for key, pth, auto, act, x, over in models_:
            lines.append('model %s %s %d %d %s %d' % (key, pth, auto, act, _fmt_trs(x), len(over)))
            for name, oact, ox in over: lines.append('mpose %s %d %s' % (name, oact, _fmt_trs(ox)))
        for key, tmd, pth, act, x in meshes_:
            lines.append('mmesh %s %d %s %d %s' % (key, tmd, pth, act, _fmt_trs(x)))
        return ' %s %d %d %s' % (first, len(models_), len(meshes_), child0), lines

    def unlinked(self):
        """AnimatedObject instances that no entity claimed."""
        out = []
        for pf, ins in self.insts.items():
            for g, i in ins.items():
                if (pf, g) not in self.linked_ao: out.append('%s %s' % (os.path.basename(pf), i['name']))
        return out

    def write(self, out_dir, folder):
        """Write <out_dir>/<folder>/models.bin with every model the dream's entities use. Returns stats or None."""
        used = self.used.get(folder)
        if not used: return None
        entries = []; st = dict(models=0, objects=0, tris=0, anims=0, keys=0)
        for key in sorted(used):
            guid, mom = used[key]
            md = self.model(guid, mom); meshes = self.lbd_moms[guid][mom]['meshes']
            entries.append((key, md, meshes))
            st['models'] += 1; st['objects'] += len(md['objects']); st['tris'] += sum(len(m) for m in meshes)
            st['anims'] += len(md['anims']); st['keys'] += sum(len(k) for a in md['anims'] for k in a['tracks'].values())
        st['bytes'] = write_models_bin(os.path.join(out_dir, folder, 'models.bin'), entries)
        return st
