#!/usr/bin/env python
"""Bake the "Additional Dreams" mod (github.com/Figglewatts/LSDR-Additional-Dreams, MIT) for the Wii U app.

Usage:  python -I addl.py <out data dir> [--mod DIR] [--repo DIR] [--tex N] [--cell N] [--png] [--side] [--verify]
                                        [--nosound] [--only ADD00,ADD05,...]

  --mod    the cloned mod project (default: ../ref/LSDR-Additional-Dreams next to this tools dir)
  --repo   the LSDRevamped repo (default: ../src): SDK script/shader GUIDs; convert.py (next to this file) gives
           env_line / lua_lambdas so both converters write identical env lines and Lua
  --tex    maximum texture edge (default 512: the 1024x1024 atlases are box-filtered to 512x512; --tex 1024 keeps them)
  --cell   chunk cell size in units (default 16)
  --png    also write top-down preview PNGs into <out>/addl_preview/ (--side adds side views); slow (pure Python)
  --verify re-read every written binary with the reference readers below and cross-check dreams_addl.txt
  --nosound  do not convert the mod's .ogg sounds to .wav (needs ffmpeg otherwise)
  --only   bake only these dream folders (dreams_addl.txt then only lists them)

Writes <out>/dreams_addl.txt, <out>/ADD00..ADD13/{world.bin,amodels.bin}, <out>/ADDTEX/*.tex, <out>/ADDLUA/*.lua,
<out>/ADDSND/*.wav. The formats are described in ADDL_FORMAT.md (same folder as this file).
Does not touch dreams.txt or anything convert.py writes.
"""
import os, re, sys, glob, math, struct, zlib, ast, shutil, subprocess, collections

HERE = os.path.dirname(os.path.abspath(__file__))
MASK63 = 0x7FFFFFFFFFFFFFFF
FBX_ROOT_GO = 919132149155446097          # Unity's (hash-based, fileIdsGeneration 2) fileID of an imported model's root GameObject
FBX_ROOT_TR = -8679921383154817045        # ... and of its root Transform (identical for every FBX, verified in all 77 instances)

# ----------------------------------------------------------------------------------------------------------------------
# Unity YAML (the subset Unity writes: block mappings, "- " sequences at the key's indent, flow mappings {a: b, c: {..}})
# ----------------------------------------------------------------------------------------------------------------------
def _flow(s):
    """parse a flow value: {k: v, ...}, [a, b] or a scalar string"""
    s = s.strip()
    if not s: return ''
    if s[0] not in '{[':
        if len(s) >= 2 and s[0] == s[-1] and s[0] in '\'"': return s[1:-1]
        return s
    pos = 0
    def val():
        nonlocal pos
        while s[pos] == ' ': pos += 1
        if s[pos] == '{':
            pos += 1; d = {}
            while True:
                while s[pos] in ' ,': pos += 1
                if s[pos] == '}': pos += 1; return d
                k0 = pos
                while s[pos] != ':': pos += 1
                k = s[k0:pos].strip(); pos += 1
                d[k] = val()
        if s[pos] == '[':
            pos += 1; l = []
            while True:
                while s[pos] in ' ,': pos += 1
                if s[pos] == ']': pos += 1; return l
                l.append(val())
        k0 = pos
        while pos < len(s) and s[pos] not in ',}]': pos += 1
        return s[k0:pos].strip()
    return val()

def parse_yaml_docs(text):
    """-> list of (cls, fileID, stripped, typename, body dict)"""
    docs = []
    parts = re.split(r'^--- !u!(\d+) &(-?\d+)( stripped)?[^\n]*\n', text, flags=re.M)
    for i in range(1, len(parts), 4):
        cls, fid, stripped, body = int(parts[i]), int(parts[i + 1]), bool(parts[i + 2]), parts[i + 3]
        lines = [l for l in body.split('\n') if l.strip() and not l.lstrip().startswith('#')]
        if not lines: continue
        tname = lines[0].rstrip(':').strip()
        d, _ = _block(lines, 1, 2)
        docs.append((cls, fid, stripped, tname, d))
    return docs

def _indent(l): return len(l) - len(l.lstrip(' '))

def _block(lines, i, ind):
    """parse a block mapping at indent `ind` starting at line i; returns (dict, next i)"""
    d = {}
    while i < len(lines):
        l = lines[i]; li = _indent(l)
        if li < ind or (li == ind and l.lstrip().startswith('- ')): break
        if li > ind: i += 1; continue   # stray continuation
        m = re.match(r'\s*([^:]+?):(?: (.*))?$', l)
        if not m: i += 1; continue
        k, v = m.group(1), m.group(2)
        i += 1
        if v is not None and v.strip() != '':
            # multi-line plain/quoted scalar continuation
            while i < len(lines) and _indent(lines[i]) > ind and not re.match(r'\s*- ', lines[i]) and not re.match(r'\s*[\w\[\].]+:( |$)', lines[i]):
                v += ' ' + lines[i].strip(); i += 1
            d[k] = _flow(v)
        else:
            if i < len(lines) and _indent(lines[i]) == ind and lines[i].lstrip().startswith('- '):
                d[k], i = _seq(lines, i, ind)
            elif i < len(lines) and _indent(lines[i]) > ind:
                if lines[i].lstrip().startswith('- '): d[k], i = _seq(lines, i, _indent(lines[i]))
                else: d[k], i = _block(lines, i, _indent(lines[i]))
            else: d[k] = ''
    return d, i

def _seq(lines, i, ind):
    out = []
    while i < len(lines) and _indent(lines[i]) == ind and lines[i].lstrip().startswith('- '):
        l = lines[i]; item = l[ind + 2:]
        m = re.match(r'([^:{\[]+?):(?: (.*))?$', item)
        if m and not item.startswith('{'):
            # mapping item: first key on the dash line, rest indented by ind+2
            sub = [' ' * (ind + 2) + item] + []
            j = i + 1
            while j < len(lines) and (_indent(lines[j]) > ind + 1) and not (_indent(lines[j]) == ind and lines[j].lstrip().startswith('- ')):
                sub.append(lines[j]); j += 1
            dd, _ = _block(sub, 0, ind + 2)
            out.append(dd); i = j
        else:
            out.append(_flow(item)); i += 1
    return out, i

def fid(ref):
    """fileID of a {fileID: .., guid: ..} reference (0 if none)"""
    try: return int(ref.get('fileID', 0))
    except (AttributeError, ValueError): return 0
def gid(ref):
    return ref.get('guid') if isinstance(ref, dict) else None
def ffloat(x, d=0.0):
    try: return float(x)
    except (TypeError, ValueError): return d
def v3(d, default=(0.0, 0.0, 0.0)):
    if not isinstance(d, dict): return default
    return (ffloat(d.get('x')), ffloat(d.get('y')), ffloat(d.get('z')))
def q4(d):
    if not isinstance(d, dict): return (0.0, 0.0, 0.0, 1.0)
    return (ffloat(d.get('x')), ffloat(d.get('y')), ffloat(d.get('z')), ffloat(d.get('w'), 1.0))

# ----------------------------------------------------------------------------------------------------------------------
# math (Unity conventions: left-handed, Y up; quaternions (x, y, z, w); TRS p' = T R S p)
# ----------------------------------------------------------------------------------------------------------------------
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz)
def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)
def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-q[0], -q[1], -q[2], q[3]))
    return p[:3]
def qaxis(axis, deg):
    h = math.radians(deg) / 2; s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))
def qmat(q):
    x, y, z, w = q
    return [[1 - 2*(y*y + z*z), 2*(x*y - z*w), 2*(x*z + y*w)],
            [2*(x*y + z*w), 1 - 2*(x*x + z*z), 2*(y*z - x*w)],
            [2*(x*z - y*w), 2*(y*z + x*w), 1 - 2*(x*x + y*y)]]
def mat_from_quat3(m):
    """3x3 rotation matrix -> quaternion"""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return qnorm(((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s))
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return qnorm((0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s))
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return qnorm(((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s))
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return qnorm(((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s))
def trs_mat(p, q, s):
    r = qmat(q)
    return [[r[0][0]*s[0], r[0][1]*s[1], r[0][2]*s[2], p[0]],
            [r[1][0]*s[0], r[1][1]*s[1], r[1][2]*s[2], p[1]],
            [r[2][0]*s[0], r[2][1]*s[1], r[2][2]*s[2], p[2]],
            [0.0, 0.0, 0.0, 1.0]]
def mmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def mident(): return [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
def mxp(m, v):
    return (m[0][0]*v[0] + m[0][1]*v[1] + m[0][2]*v[2] + m[0][3],
            m[1][0]*v[0] + m[1][1]*v[1] + m[1][2]*v[2] + m[1][3],
            m[2][0]*v[0] + m[2][1]*v[1] + m[2][2]*v[2] + m[2][3])
def minv(m):
    """inverse of an affine 4x4"""
    a = [r[:3] for r in m[:3]]
    det = (a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1]) - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0]) + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]))
    if abs(det) < 1e-20: det = 1e-20
    inv = [[(a[1][1]*a[2][2]-a[1][2]*a[2][1])/det, (a[0][2]*a[2][1]-a[0][1]*a[2][2])/det, (a[0][1]*a[1][2]-a[0][2]*a[1][1])/det],
           [(a[1][2]*a[2][0]-a[1][0]*a[2][2])/det, (a[0][0]*a[2][2]-a[0][2]*a[2][0])/det, (a[0][2]*a[1][0]-a[0][0]*a[1][2])/det],
           [(a[1][0]*a[2][1]-a[1][1]*a[2][0])/det, (a[0][1]*a[2][0]-a[0][0]*a[2][1])/det, (a[0][0]*a[1][1]-a[0][1]*a[1][0])/det]]
    t = [m[0][3], m[1][3], m[2][3]]
    it = [-(inv[i][0]*t[0] + inv[i][1]*t[1] + inv[i][2]*t[2]) for i in range(3)]
    return [inv[0] + [it[0]], inv[1] + [it[1]], inv[2] + [it[2]], [0.0, 0.0, 0.0, 1.0]]
def mat_decompose(m):
    """affine 4x4 (no shear) -> (pos, quat, scale)"""
    p = (m[0][3], m[1][3], m[2][3])
    cols = [[m[r][c] for r in range(3)] for c in range(3)]
    s = [math.sqrt(sum(x * x for x in col)) or 1e-12 for col in cols]
    det = (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))
    if det < 0: s[0] = -s[0]
    r = [[m[i][j] / s[j] for j in range(3)] for i in range(3)]
    return p, mat_from_quat3(r), tuple(s)
def euler_unity(e):
    """Unity Euler angles (degrees, applied Z, then X, then Y) -> quaternion, as Quaternion.Euler"""
    return qmul(qmul(qaxis((0, 1, 0), e[1]), qaxis((1, 0, 0), e[0])), qaxis((0, 0, 1), e[2]))

# ----------------------------------------------------------------------------------------------------------------------
# binary FBX (7.x) reader and the Unity import of it
# ----------------------------------------------------------------------------------------------------------------------
class FNode:
    __slots__ = ('name', 'props', 'children')
    def __init__(s, n, p, c): s.name = n; s.props = p; s.children = c
    def find(s, n):
        for c in s.children:
            if c.name == n: return c
        return None
    def findall(s, n): return [c for c in s.children if c.name == n]
    def p70(s):
        """Properties70 -> {name: [values]}"""
        p = s.find('Properties70')
        return {c.props[0]: c.props[4:] for c in p.children} if p else {}

def fbx_parse(path):
    d = open(path, 'rb').read()
    if d[:20] != b'Kaydara FBX Binary  ': raise ValueError('not a binary FBX: ' + path)
    ver = struct.unpack_from('<I', d, 23)[0]; big = ver >= 7500
    def node(o):
        if big: end, nprop, _ = struct.unpack_from('<QQQ', d, o); o += 24
        else: end, nprop, _ = struct.unpack_from('<III', d, o); o += 12
        nl = d[o]; o += 1
        if end == 0: return None, o
        name = d[o:o + nl].decode('latin1'); o += nl
        props = []
        for _ in range(nprop):
            t = chr(d[o]); o += 1
            if t == 'Y': props.append(struct.unpack_from('<h', d, o)[0]); o += 2
            elif t == 'C': props.append(d[o] != 0); o += 1
            elif t == 'I': props.append(struct.unpack_from('<i', d, o)[0]); o += 4
            elif t == 'F': props.append(struct.unpack_from('<f', d, o)[0]); o += 4
            elif t == 'D': props.append(struct.unpack_from('<d', d, o)[0]); o += 8
            elif t == 'L': props.append(struct.unpack_from('<q', d, o)[0]); o += 8
            elif t in 'fdlib':
                n, enc, cl = struct.unpack_from('<III', d, o); o += 12
                raw = d[o:o + cl]; o += cl
                if enc == 1: raw = zlib.decompress(raw)
                props.append(list(struct.unpack('<%d%s' % (n, {'f': 'f', 'd': 'd', 'l': 'q', 'i': 'i', 'b': 'B'}[t]), raw)))
            elif t in 'SR':
                n = struct.unpack_from('<I', d, o)[0]; o += 4
                b = d[o:o + n]; o += n
                props.append(b.decode('utf8', 'replace') if t == 'S' else b)
            else: raise ValueError('FBX property type %r in %s' % (t, path))
        ch = []
        while o < end:
            c, o2 = node(o)
            if c is None: o = o2; break
            ch.append(c); o = o2
        return FNode(name, props, ch), end
    top = []; o = 27
    while o < len(d) - (25 if big else 13):
        n, o2 = node(o)
        if n is None: break
        top.append(n); o = o2
    return FNode('', [], top), ver

KTIME = 46186158000          # FBX time units per second

def _m3(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def _euler_mat(e, order=0):
    """FBX Euler (degrees) -> 3x3, column vectors; order 0 = eEulerXYZ (X applied first: R = Rz Ry Rx)"""
    def rx(a): c, s = math.cos(a), math.sin(a); return [[1, 0, 0], [0, c, -s], [0, s, c]]
    def ry(a): c, s = math.cos(a), math.sin(a); return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    def rz(a): c, s = math.cos(a), math.sin(a); return [[c, -s, 0], [s, c, 0], [0, 0, 1]]
    X, Y, Z = rx(math.radians(e[0])), ry(math.radians(e[1])), rz(math.radians(e[2]))
    seq = {0: (X, Y, Z), 1: (X, Z, Y), 2: (Y, Z, X), 3: (Y, X, Z), 4: (Z, X, Y), 5: (Z, Y, X)}.get(order, (X, Y, Z))
    r = seq[0]
    for m in seq[1:]: r = _m3(m, r)
    return r
def _m4(r3, t=(0, 0, 0)):
    return [[r3[0][0], r3[0][1], r3[0][2], t[0]], [r3[1][0], r3[1][1], r3[1][2], t[1]], [r3[2][0], r3[2][1], r3[2][2], t[2]], [0, 0, 0, 1]]
def _tm(t): return [[1, 0, 0, t[0]], [0, 1, 0, t[1]], [0, 0, 1, t[2]], [0, 0, 0, 1]]
def _sm(s): return [[s[0], 0, 0, 0], [0, s[1], 0, 0], [0, 0, s[2], 0], [0, 0, 0, 1]]

class FbxModel:
    """one FBX file as Unity imports it (ModelImporter settings used by the mod: Convert Units on, axis conversion not baked,
       generic rig). Unity space of a node: C (A) L C^-1 with C = diag(-s, s, s) (s = UnitScaleFactor / 100, X mirrored for
       right- to left-handed) and A = the file's axis system -> +X right, +Y up, +Z front (top-level nodes only)."""
    def __init__(self, path, add_colliders):
        self.path = path; self.name = os.path.splitext(os.path.basename(path))[0]
        root, self.version = fbx_parse(path)
        gs = root.find('GlobalSettings').p70()
        g = lambda k, d: gs.get(k, [d])[0]
        self.unit = g('UnitScaleFactor', 1.0)
        ax = [g('CoordAxis', 0), g('CoordAxisSign', 1), g('UpAxis', 1), g('UpAxisSign', 1), g('FrontAxis', 2), g('FrontAxisSign', 1)]
        self.axes = ax
        e = lambda a, s: [float(s) if i == a else 0.0 for i in range(3)]
        X, U, F = e(ax[0], ax[1]), e(ax[2], ax[3]), e(ax[4], ax[5])
        # A maps the file's (coord, up, front) to (+X, +Y, +Z); for orthonormal axes A = Msrc^T
        self.A = [[X[0], X[1], X[2], 0], [U[0], U[1], U[2], 0], [F[0], F[1], F[2], 0], [0, 0, 0, 1]]
        sc = self.unit / 100.0
        self.C = [[-sc, 0, 0, 0], [0, sc, 0, 0], [0, 0, sc, 0], [0, 0, 0, 1]]
        self.Ci = [[-1 / sc, 0, 0, 0], [0, 1 / sc, 0, 0], [0, 0, 1 / sc, 0], [0, 0, 0, 1]]
        self.add_colliders = add_colliders
        objs = root.find('Objects'); conns = root.find('Connections')
        self.obj = {o.props[0]: o for o in objs.children}
        self.parent = {}; self.kids = collections.defaultdict(list); self.op = collections.defaultdict(list); self.opsrc = collections.defaultdict(list)
        for c in conns.children:
            kind, a, b = c.props[0], c.props[1], c.props[2]
            if kind == 'OO': self.parent.setdefault(a, b); self.kids[b].append(a)
            elif kind == 'OP': self.op[b].append((a, c.props[3])); self.opsrc[a].append((b, c.props[3]))
        self.models = [o.props[0] for o in objs.findall('Model')]
        self.mname = {m: self.obj[m].props[1].split('\x00')[0] for m in self.models}
        self.mtype = {m: self.obj[m].props[2] for m in self.models}
        self.top = [m for m in self.models if self.parent.get(m, 0) == 0]
        self.mkids = {m: [k for k in self.kids[m] if k in self.mname] for m in self.models}
        self.props = {m: self.obj[m].p70() for m in self.models}
        self.geo = {}; self.mats = {}
        for m in self.models:
            gs_ = [k for k in self.kids[m] if k in self.obj and self.obj[k].name == 'Geometry']
            self.geo[m] = gs_[0] if gs_ else None
            self.mats[m] = [self.obj[k].props[1].split('\x00')[0] for k in self.kids[m] if k in self.obj and self.obj[k].name == 'Material']
        self._meshes = {}
        self.anims = self._read_anims()

    # --- transforms
    def lcl(self, m, over=None):
        """FBX local matrix of model m (file units and axes); over = {'T': (x,y,z), 'R': .., 'S': ..} animated values"""
        p = self.props[m]
        g = lambda k, d: tuple(p.get(k, d))
        T = over['T'] if over and 'T' in over else g('Lcl Translation', (0, 0, 0))
        R = over['R'] if over and 'R' in over else g('Lcl Rotation', (0, 0, 0))
        S = over['S'] if over and 'S' in over else g('Lcl Scaling', (1, 1, 1))
        order = int(p.get('RotationOrder', [0])[0])
        pre = _m4(_euler_mat(g('PreRotation', (0, 0, 0))))
        post = _m4(_euler_mat(g('PostRotation', (0, 0, 0))))
        roff, rp = _tm(g('RotationOffset', (0, 0, 0))), _tm(g('RotationPivot', (0, 0, 0)))
        soff, sp = _tm(g('ScalingOffset', (0, 0, 0))), _tm(g('ScalingPivot', (0, 0, 0)))
        r = _m4(_euler_mat(R, order))
        m4 = mmul(_tm(T), roff); m4 = mmul(m4, rp); m4 = mmul(m4, pre); m4 = mmul(m4, r); m4 = mmul(m4, minv(post))
        m4 = mmul(m4, minv(rp)); m4 = mmul(m4, soff); m4 = mmul(m4, sp); m4 = mmul(m4, _sm(S)); m4 = mmul(m4, minv(sp))
        return m4
    def fbx_global(self, m):
        """global matrix of model m in file space (no axis conversion), from the Lcl chain"""
        L = self.lcl(m); p = self.parent.get(m, 0)
        return mmul(self.fbx_global(p), L) if p in self.mname else L
    def geo_mat(self, m):
        p = self.props[m]
        g = lambda k, d: tuple(p.get(k, d))
        return mmul(mmul(_tm(g('GeometricTranslation', (0, 0, 0))), _m4(_euler_mat(g('GeometricRotation', (0, 0, 0))))), _sm(g('GeometricScaling', (1, 1, 1))))
    def unity_local(self, m, over=None):
        """Unity local matrix of the GameObject made from model m"""
        L = self.lcl(m, over)
        if self.parent.get(m, 0) == 0: L = mmul(self.A, L)
        return mmul(mmul(self.C, L), self.Ci)

    # --- meshes
    def mesh(self, m):
        """triangles of model m in Unity mesh space (node-local, mirrored, unit-scaled, winding reversed back):
           list of (material slot, (p0, p1, p2), (uv0, uv1, uv2), (col0, col1, col2) or Nones, (cp0, cp1, cp2))"""
        if m in self._meshes: return self._meshes[m]
        g = self.geo[m]
        if g is None: self._meshes[m] = []; return []
        gn = self.obj[g]
        V = gn.find('Vertices').props[0]; P = gn.find('PolygonVertexIndex').props[0]
        def layer(lname, dname, iname):
            le = gn.find(lname)
            if le is None or le.find(dname) is None: return None
            mp = le.find('MappingInformationType').props[0]; rt = le.find('ReferenceInformationType').props[0]
            data = le.find(dname).props[0]; idx = le.find(iname).props[0] if le.find(iname) is not None else None
            return mp, rt, data, idx
        uvl = layer('LayerElementUV', 'UV', 'UVIndex'); coll = layer('LayerElementColor', 'Colors', 'ColorIndex')
        matl = gn.find('LayerElementMaterial')
        mats = matl.find('Materials').props[0] if matl is not None else [0]
        matmap = matl.find('MappingInformationType').props[0] if matl is not None else 'AllSame'
        def get(l, comp, pvi, cpi, poly):
            if l is None: return None
            mp, rt, data, idx = l
            k = {'ByPolygonVertex': pvi, 'ByVertice': cpi, 'ByVertex': cpi, 'ByPolygon': poly, 'AllSame': 0}.get(mp, pvi)
            if rt in ('IndexToDirect', 'Index') and idx is not None: k = idx[k]
            return tuple(data[k * comp:(k + 1) * comp])
        GM = mmul(self.C, self.geo_mat(m))
        tris = []; poly = []; npoly = 0
        for pvi, raw in enumerate(P):
            cp = raw if raw >= 0 else ~raw
            poly.append((pvi, cp))
            if raw < 0:
                mslot = mats[npoly] if matmap == 'ByPolygon' and npoly < len(mats) else mats[0]
                vs = []
                for (pv, c) in poly:
                    pos = mxp(GM, (V[c * 3], V[c * 3 + 1], V[c * 3 + 2]))
                    uv = get(uvl, 2, pv, c, npoly) or (0.0, 0.0)
                    col = get(coll, 4, pv, c, npoly)
                    vs.append((pos, uv, col, c))
                for k in range(1, len(vs) - 1):       # fan; winding reversed because of the mirror
                    a, b, c_ = vs[0], vs[k + 1], vs[k]
                    tris.append((mslot, (a[0], b[0], c_[0]), (a[1], b[1], c_[1]), (a[2], b[2], c_[2]), (a[3], b[3], c_[3])))
                poly = []; npoly += 1
        self._meshes[m] = tris
        return tris

    def skin(self, m):
        """[(bone model id, {cp: weight}, Transform, TransformLink)] of the skinned geometry of model m (FBX space)"""
        g = self.geo[m]
        if g is None: return []
        out = []
        for d in self.kids[g]:
            dn = self.obj.get(d)
            if dn is None or dn.name != 'Deformer' or dn.props[2] != 'Skin': continue
            for cl in self.kids[d]:
                cn = self.obj.get(cl)
                if cn is None or cn.props[2] != 'Cluster': continue
                bones = [k for k in self.kids[cl] if k in self.mname]
                if not bones: continue
                idx = cn.find('Indexes').props[0] if cn.find('Indexes') else []
                w = cn.find('Weights').props[0] if cn.find('Weights') else []
                T = cn.find('Transform').props[0]; TL = cn.find('TransformLink').props[0]
                tm = lambda a: [[a[c * 4 + r] for c in range(4)] for r in range(4)]   # stored column-major
                out.append((bones[0], dict(zip(idx, w)), tm(T), tm(TL)))
        return out

    # --- animation
    def _read_anims(self):
        out = {}
        for st in [o for o in self.obj.values() if o.name == 'AnimationStack']:
            name = st.props[1].split('\x00')[0]
            p = st.p70()
            stop = p.get('LocalStop', p.get('ReferenceStop', [0]))[0]; start = p.get('LocalStart', p.get('ReferenceStart', [0]))[0]
            curves = collections.defaultdict(dict)      # model -> {'T': {comp: (times, values)}, 'T_d': defaults, ...}
            for layer in self.kids[st.props[0]]:
                if self.obj.get(layer) is None or self.obj[layer].name != 'AnimationLayer': continue
                for cnid in self.kids[layer]:
                    cn = self.obj.get(cnid)
                    if cn is None or cn.name != 'AnimationCurveNode': continue
                    tgt = [(b, prop) for (b, prop) in self.opsrc.get(cnid, []) if b in self.mname]
                    if not tgt: continue
                    model, prop = tgt[0]
                    key = {'Lcl Translation': 'T', 'Lcl Rotation': 'R', 'Lcl Scaling': 'S'}.get(prop)
                    if key is None: continue
                    for (cid, cprop) in self.op[cnid]:
                        cv = self.obj.get(cid)
                        if cv is None or cv.name != 'AnimationCurve': continue
                        comp = {'d|X': 0, 'd|Y': 1, 'd|Z': 2}.get(cprop)
                        if comp is None: continue
                        curves[model].setdefault(key, {})[comp] = (cv.find('KeyTime').props[0], cv.find('KeyValueFloat').props[0])
                    dflt = cn.p70()
                    curves[model][key + '_d'] = (dflt.get('d|X', [None])[0], dflt.get('d|Y', [None])[0], dflt.get('d|Z', [None])[0])
            out[name] = dict(start=start / KTIME, stop=stop / KTIME, curves=dict(curves))
        return out

    def sample(self, anim, m, t):
        """{'T','R','S'} values of model m at time t (seconds) of animation `anim` (linear between keys), None if not animated"""
        a = self.anims[anim]; c = a['curves'].get(m)
        if not c: return None
        out = {}
        base = {'T': tuple(self.props[m].get('Lcl Translation', (0, 0, 0))), 'R': tuple(self.props[m].get('Lcl Rotation', (0, 0, 0))),
                'S': tuple(self.props[m].get('Lcl Scaling', (1, 1, 1)))}
        kt = t * KTIME
        for key in ('T', 'R', 'S'):
            if key not in c and key + '_d' not in c: continue
            v = list(base[key]); dflt = c.get(key + '_d', (None, None, None))
            for comp in range(3):
                if dflt[comp] is not None: v[comp] = dflt[comp]
                if comp not in c.get(key, {}): continue
                times, vals = c[key][comp]
                if kt <= times[0]: v[comp] = vals[0]
                elif kt >= times[-1]: v[comp] = vals[-1]
                else:
                    lo, hi = 0, len(times) - 1
                    while hi - lo > 1:
                        mid = (lo + hi) // 2
                        if times[mid] <= kt: lo = mid
                        else: hi = mid
                    f = (kt - times[lo]) / (times[hi] - times[lo])
                    v[comp] = vals[lo] + (vals[hi] - vals[lo]) * f
            out[key] = tuple(v)
        return out

# ----------------------------------------------------------------------------------------------------------------------
# Unity project: GUID index, asset loading, prefab instantiation (nested prefabs, variants, model prefabs)
# ----------------------------------------------------------------------------------------------------------------------
class Project:
    def __init__(self, mod, repo):
        self.mod = mod; self.repo = repo
        self.guid = {}
        roots = [os.path.join(mod, 'Assets')]
        for sub in ('Packages', 'Assets'):
            p = os.path.join(repo, 'LSDR', sub)
            if os.path.isdir(p): roots.append(p)
        for r in roots:
            for m in glob.glob(os.path.join(r, '**', '*.meta'), recursive=True):
                try: t = open(m, encoding='utf8', errors='ignore').read(700)
                except OSError: continue
                g = re.search(r'guid: (\w+)', t)
                if g: self.guid.setdefault(g.group(1), m[:-5])
        self._docs = {}; self._fbx = {}; self._inst = {}
        self.unresolved = collections.Counter()     # (asset, property) of modifications whose target object is unknown

    def path(self, ref):
        g = gid(ref)
        return self.guid.get(g) if g else None
    def rel(self, p): return os.path.relpath(p, self.mod).replace(os.sep, '/') if p else '-'
    def script_name(self, d):
        p = self.path(d.get('m_Script'))
        return os.path.splitext(os.path.basename(p))[0] if p else None

    def docs(self, path):
        if path not in self._docs:
            self._docs[path] = parse_yaml_docs(open(path, encoding='utf8', errors='ignore').read())
        return self._docs[path]
    def meta(self, path):
        return open(path + '.meta', encoding='utf8', errors='ignore').read()
    def fbx(self, path):
        if path not in self._fbx:
            meta = self.meta(path)
            self._fbx[path] = FbxModel(path, bool(int(re.search(r'addColliders: (\d)', meta).group(1))) if 'addColliders' in meta else False)
            ext = {}
            for n, g in re.findall(r'name: (.*)\n\s+second: \{fileID: \d+, guid: (\w+)', meta): ext[n.strip()] = self.guid.get(g)
            self._fbx[path].external = ext
        return self._fbx[path]

    # --- instantiation: dict fileID -> object {'cls', 'type', 'd', 'asset'} in the asset's own fileID space
    def instantiate(self, path):
        if path not in self._inst:
            self._inst[path] = self._fbx_objects(path) if path.lower().endswith('.fbx') else self._prefab_objects(path)
        return _deep(self._inst[path])

    def _prefab_objects(self, path):
        objs = {}
        docs = self.docs(path)
        for cls, fid_, stripped, tname, d in docs:
            if stripped or cls == 1001: continue
            objs[fid_] = dict(cls=cls, type=tname, d=_deep(d), asset=path)
        for cls, fid_, stripped, tname, d in docs:
            if cls != 1001: continue
            mod = d.get('m_Modification', {})
            src = self.path(d.get('m_SourcePrefab'))
            if not src or not os.path.exists(src):
                print('  warning: missing source prefab', d.get('m_SourcePrefab'), 'in', path); continue
            sub = self.instantiate(src)
            P = fid_
            mapped = {}
            for i, o in sub.items():
                _remap(o['d'], P)
                mapped[(P ^ i) & MASK63] = o
            root_go = None
            for i, o in mapped.items():
                if o['cls'] == 4 and fid(o['d'].get('m_Father', {})) == 0: root_go = fid(o['d'].get('m_GameObject', {}))
            for m in mod.get('m_Modifications', []) or []:
                t = (P ^ fid(m.get('target', {}))) & MASK63
                o = mapped.get(t)
                if o is None:
                    # target inside an imported model (hash fileID); kept on the instance root for the name-based fixups
                    key = (self.rel(src), m.get('propertyPath'), str(m.get('value')))
                    self.unresolved[key] += 1
                    if root_go in mapped: mapped[root_go].setdefault('unres', []).append(key)
                    continue
                _set_prop(o['d'], m.get('propertyPath', ''), m.get('value'), m.get('objectReference'))
            parent = fid(mod.get('m_TransformParent', {}))
            for i, o in mapped.items():
                if o['cls'] == 4 and fid(o['d'].get('m_Father', {})) == 0:
                    o['d']['m_Father'] = {'fileID': str(parent)}
            objs.update(mapped)
        return objs

    def _fbx_objects(self, path):
        F = self.fbx(path)
        objs = {}
        def nid(key, cls):
            h = zlib.crc32(('%s|%s|%d' % (path, key, cls)).encode()) | (zlib.crc32(('x' + key + str(cls)).encode()) << 32)
            return h & MASK63
        def add_go(go_id, tr_id, name, father, mat, comps):
            p, q, s = mat_decompose(mat)
            objs[go_id] = dict(cls=1, type='GameObject', asset=path, d={'m_Name': name, 'm_IsActive': '1',
                               'm_Component': [{'component': {'fileID': str(tr_id)}}] + [{'component': {'fileID': str(c)}} for c in comps]})
            objs[tr_id] = dict(cls=4, type='Transform', asset=path, d={'m_GameObject': {'fileID': str(go_id)},
                               'm_LocalPosition': dict(x=p[0], y=p[1], z=p[2]), 'm_LocalRotation': dict(x=q[0], y=q[1], z=q[2], w=q[3]),
                               'm_LocalScale': dict(x=s[0], y=s[1], z=s[2]), 'm_Father': {'fileID': str(father)}, 'm_RootOrder': '0'})
        def mesh_comps(m, key, go_id):
            comps = []
            if F.geo[m] is None: return comps
            skinned = bool(F.skin(m))
            mats = [F.external.get(n) for n in F.mats[m]]
            c1 = nid(key, 33); objs[c1] = dict(cls=33, type='MeshFilter', asset=path, d={'m_GameObject': {'fileID': str(go_id)}, 'fbx': path, 'model': m})
            c2 = nid(key, 137 if skinned else 23)
            objs[c2] = dict(cls=137 if skinned else 23, type='SkinnedMeshRenderer' if skinned else 'MeshRenderer', asset=path,
                            d={'m_GameObject': {'fileID': str(go_id)}, 'm_Enabled': '1', 'materials': mats, 'fbx': path, 'model': m, 'matnames': F.mats[m]})
            comps += [c1, c2]
            if F.add_colliders and not skinned:
                c3 = nid(key, 64); objs[c3] = dict(cls=64, type='MeshCollider', asset=path, d={'m_GameObject': {'fileID': str(go_id)}, 'm_Enabled': '1', 'fbx': path, 'model': m})
                comps.append(c3)
            return comps
        def walk(m, father, key):
            go_id, tr_id = nid(key, 1), nid(key, 4)
            add_go(go_id, tr_id, F.mname[m], father, F.unity_local(m), mesh_comps(m, key, go_id))
            objs[go_id]['fbxnode'] = m; objs[tr_id]['fbxnode'] = m
            for k in F.mkids[m]: walk(k, tr_id, key + '/' + F.mname[k] + '#' + str(k))
        if len(F.top) == 1:
            m = F.top[0]
            add_go(FBX_ROOT_GO, FBX_ROOT_TR, F.name, 0, F.unity_local(m), mesh_comps(m, 'root', FBX_ROOT_GO))
            objs[FBX_ROOT_GO]['fbxnode'] = m; objs[FBX_ROOT_TR]['fbxnode'] = m
            for k in F.mkids[m]: walk(k, FBX_ROOT_TR, F.mname[k] + '#' + str(k))
        else:
            add_go(FBX_ROOT_GO, FBX_ROOT_TR, F.name, 0, mident(), [])
            for m in F.top: walk(m, FBX_ROOT_TR, F.mname[m] + '#' + str(m))
        objs[FBX_ROOT_GO]['fbxroot'] = path
        return objs

def _deep(x):
    if isinstance(x, dict): return {k: _deep(v) for k, v in x.items()}
    if isinstance(x, list): return [_deep(v) for v in x]
    return x

def _remap(d, P):
    """rewrite local fileID references of a nested object into the outer asset's ID space"""
    if isinstance(d, dict):
        if 'fileID' in d and 'guid' not in d and len(d) == 1:
            f = fid(d)
            if f: d['fileID'] = str((P ^ f) & MASK63)
            return
        for v in d.values(): _remap(v, P)
    elif isinstance(d, list):
        for v in d: _remap(v, P)

def _set_prop(d, path, value, objref):
    parts = path.split('.')
    cur = d
    i = 0
    while i < len(parts) - 1:
        k = parts[i]
        if k == 'Array' and i + 1 < len(parts) and parts[i + 1].startswith('data['):
            idx = int(parts[i + 1][5:-1])
            if not isinstance(cur, list): return
            while len(cur) <= idx: cur.append({})
            if i + 1 == len(parts) - 1:
                cur[idx] = objref if isinstance(objref, dict) and (fid(objref) or gid(objref)) else value
                return
            cur = cur[idx]; i += 2; continue
        if k == 'Array' and parts[i + 1] == 'size':
            if isinstance(cur, list):
                n = int(value); del cur[n:]
                while len(cur) < n: cur.append({})
            return
        if not isinstance(cur, dict): return
        if k not in cur or not isinstance(cur[k], (dict, list)): cur[k] = {}
        cur = cur[k]; i += 1
    last = parts[-1]
    if not isinstance(cur, dict): return
    if isinstance(cur.get(last), dict) and 'fileID' in cur.get(last) or (isinstance(objref, dict) and (fid(objref) or gid(objref))):
        cur[last] = objref
    else:
        cur[last] = value

# ----------------------------------------------------------------------------------------------------------------------
# scene graph of an instantiated dream prefab
# ----------------------------------------------------------------------------------------------------------------------
class Scene:
    def __init__(self, proj, objs):
        self.proj = proj; self.o = objs
        self.go = {i: o for i, o in objs.items() if o['cls'] == 1}
        self.tr = {i: o for i, o in objs.items() if o['cls'] in (4, 224)}
        self.go_tr = {}; self.comps = collections.defaultdict(list)
        for i, o in objs.items():
            g = fid(o['d'].get('m_GameObject', {}))
            if o['cls'] in (4, 224): self.go_tr[g] = i
            elif g: self.comps[g].append(i)
        self.children = collections.defaultdict(list)
        for i, t in self.tr.items():
            f = fid(t['d'].get('m_Father', {}))
            self.children[f].append(i)
        for f in self.children: self.children[f].sort(key=lambda i: int(ffloat(self.tr[i]['d'].get('m_RootOrder', 0))))
        self.tr_go = {t: g for g, t in self.go_tr.items()}
        self._world = {}
    def name(self, g): return str(self.go[g]['d'].get('m_Name', '')) if g in self.go else ''
    def active_self(self, g): return str(self.go[g]['d'].get('m_IsActive', '1')) == '1' if g in self.go else True
    def father_go(self, g):
        t = self.go_tr.get(g)
        if t is None: return None
        f = fid(self.tr[t]['d'].get('m_Father', {}))
        return self.tr_go.get(f)
    def local(self, g):
        t = self.tr[self.go_tr[g]]['d']
        p = v3(t.get('m_LocalPosition')); q = qnorm(q4(t.get('m_LocalRotation'))); s = v3(t.get('m_LocalScale'), (1, 1, 1))
        if not isinstance(t.get('m_LocalScale'), dict): s = (1.0, 1.0, 1.0)
        return p, q, s
    def world(self, g):
        if g in self._world: return self._world[g]
        p, q, s = self.local(g)
        m = trs_mat(p, q, s)
        f = self.father_go(g)
        if f is not None: m = mmul(self.world(f), m)
        self._world[g] = m
        return m
    def ancestors(self, g):
        out = []
        while g is not None:
            out.append(g); g = self.father_go(g)
        return out
    def active_in_hierarchy(self, g, stop=None):
        for a in self.ancestors(g):
            if not self.active_self(a): return False
            if a == stop: break
        return True
    def comp_list(self, g, cls=None):
        return [c for c in self.comps.get(g, []) if cls is None or self.o[c]['cls'] == cls]
    def scripts(self, g):
        return [(self.proj.script_name(self.o[c]['d']), c) for c in self.comp_list(g, 114)]
    def path_from(self, g, root):
        names = []
        while g is not None and g != root:
            names.append(self.name(g)); g = self.father_go(g)
        return '/'.join(reversed(names)) or '.'
    def roots(self):
        return [self.tr_go[t] for t in self.children.get(0, []) if t in self.tr_go]
    def walk(self, g=None):
        if g is None:
            for r in self.roots(): yield from self.walk(r)
            return
        yield g
        for t in self.children.get(self.go_tr.get(g), []):
            if t in self.tr_go: yield from self.walk(self.tr_go[t])

# ----------------------------------------------------------------------------------------------------------------------
# helpers shared with convert.py (taken from its source so both converters stay identical: env lines, Lua lambdas)
# ----------------------------------------------------------------------------------------------------------------------
def load_convert_helpers(guidmap):
    src = open(os.path.join(HERE, 'convert.py'), encoding='utf8').read()
    tree = ast.parse(src)
    want = {'ref', 'field', 'color', 'fl', 'refs_after', 'read', 'env_line', 'env_seq', 'lua_lambdas'}
    mod = ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in want], type_ignores=[])
    ns = {'re': re, 'os': os, 'guid': guidmap}
    exec(compile(mod, 'convert.py', 'exec'), ns)
    missing = want - set(ns)
    if missing: raise RuntimeError('convert.py helpers missing: %s' % missing)
    return ns

def lua_ne(src):
    """MoonSharp's `!=` -> Lua's `~=` outside strings and comments (popup.lua uses it; plain Lua 5.2 does not parse it)"""
    out = []; i = 0; n = len(src)
    while i < n:
        c = src[i]
        if src.startswith('--', i):
            m = re.match(r'--\[(=*)\[', src[i:])
            if m:
                e = src.find(']' + m.group(1) + ']', i); e = n if e < 0 else e + len(m.group(1)) + 2
            else:
                e = src.find('\n', i); e = n if e < 0 else e
            out.append(src[i:e]); i = e; continue
        m = re.match(r'\[(=*)\[', src[i:])
        if m:
            e = src.find(']' + m.group(1) + ']', i); e = n if e < 0 else e + len(m.group(1)) + 2
            out.append(src[i:e]); i = e; continue
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c: j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1]); i = j + 1; continue
        if src.startswith('!=', i): out.append('~='); i += 2; continue
        out.append(c); i += 1
    return ''.join(out)

# ----------------------------------------------------------------------------------------------------------------------
# materials and textures
# ----------------------------------------------------------------------------------------------------------------------
SH_CUTOUT, SH_BLEND, SH_WATER = 0, 1, 2

class Textures:
    """texture table shared by all baked dreams: PNG -> raw RGBA .tex in <out>/ADDTEX"""
    def __init__(self, proj, out, maxdim):
        self.proj = proj; self.out = out; self.maxdim = maxdim
        self.files = {}          # source png path -> (rel file, w, h, src w, src h)
    def get(self, png):
        if png in self.files: return self.files[png]
        from PIL import Image
        im = Image.open(png)
        sw, sh = im.size
        im = im.convert('RGBA')
        w, h = sw, sh
        while max(w, h) > self.maxdim and w % 2 == 0 and h % 2 == 0: w //= 2; h //= 2
        if (w, h) != (sw, sh): im = _downscale_rgba(im, w, h)
        name = re.sub(r'[^\w.-]', '_', os.path.splitext(os.path.basename(png))[0]) + '.tex'
        rel = 'ADDTEX/' + name
        os.makedirs(os.path.join(self.out, 'ADDTEX'), exist_ok=True)
        with open(os.path.join(self.out, 'ADDTEX', name), 'wb') as f:
            f.write(b'ATEX' + struct.pack('>HH', w, h) + im.tobytes())
        self.files[png] = (rel, w, h, sw, sh, im)
        return self.files[png]

def _downscale_rgba(im, w, h):
    """box filter, colour weighted by alpha (so transparent texels do not bleed black into cutout edges)"""
    from PIL import Image
    sw, sh = im.size; fx, fy = sw // w, sh // h
    px = im.load(); out = Image.new('RGBA', (w, h)); op = out.load()
    for y in range(h):
        for x in range(w):
            r = g = b = a = 0; n = fx * fy
            for yy in range(y * fy, y * fy + fy):
                for xx in range(x * fx, x * fx + fx):
                    pr, pg, pb, pa = px[xx, yy]
                    r += pr * pa; g += pg * pa; b += pb * pa; a += pa
            if a: op[x, y] = ((r + a // 2) // a, (g + a // 2) // a, (b + a // 2) // a, (a + n // 2) // n)
            else: op[x, y] = (0, 0, 0, 0)
    return out

class Materials:
    """per-dream material table (written into world.bin); index = position"""
    def __init__(self, proj, tex):
        self.proj = proj; self.tex = tex
        self.list = []; self.index = {}; self.notes = set()
    def _matinfo(self, matpath):
        docs = self.proj.docs(matpath)
        d = [x for x in docs if x[0] == 21][0][4]
        sh = self.proj.path(d.get('m_Shader')) or ''
        sname = os.path.splitext(os.path.basename(sh))[0]
        props = d.get('m_SavedProperties', {})
        texs = {}
        for e in props.get('m_TexEnvs', []) or []:
            for k, v in e.items():
                p = self.proj.path(v.get('m_Texture', {})) if isinstance(v, dict) else None
                if p: texs[k] = p
        floats = {}
        for e in props.get('m_Floats', []) or []:
            for k, v in e.items(): floats[k] = ffloat(v)
        return sname, texs, floats, d.get('m_Name', os.path.basename(matpath))
    def get(self, matpath, fbx_matname=None, fbx=None):
        key = matpath or ('fbx:' + (fbx.path if fbx else '') + ':' + str(fbx_matname))
        if key in self.index: return self.index[key]
        tex = [-1] * 4; pal = [-1] * 4; alpha = 1.0; speed = 0.0; cull = 0; shader = SH_CUTOUT; name = fbx_matname or 'default'
        if matpath:
            sname, texs, floats, name = self._matinfo(matpath)
            cull = int(floats.get('_Cull', 0))
            if 'Water' in sname:
                shader = SH_WATER
                maps = [texs.get('_MainTexture' + k) for k in 'ABCD']; pals = [texs.get('_WaterPalette' + k) for k in 'ABCD']
                maps = [m or maps[0] for m in maps]; pals = [p or pals[0] for p in pals]
                tex = [self._t(m) for m in maps]; pal = [self._t(p) for p in pals]
                alpha = floats.get('_Alpha', 0.5); speed = floats.get('_AnimationSpeed', 1.0)
                if not all(texs.get('_MainTexture' + k) for k in 'BCD'): self.notes.add('%s: water textures B-D missing, A used for every texture set' % name)
            else:
                shader = SH_BLEND if 'AlphaBlend' in sname else SH_CUTOUT
                if 'Set' in sname:
                    ts = [texs.get('_MainTex' + k) or texs.get('_MainTex') for k in 'ABCD']
                else:
                    ts = [texs.get('_MainTex')] * 4
                tex = [self._t(t) if t else -1 for t in ts]
                if sname.startswith('Revamped'): self.notes.add('%s: %s (Revamped shader) treated like the classic one' % (name, sname))
        elif fbx_matname == 'collider':
            name = 'collider'       # invisible collision-only triangles
        else:
            # FBX material without a remap: Unity builds a Standard-shader material from the FBX (texture found by name)
            png = None
            if fbx is not None:
                for o in fbx.obj.values():
                    if o.name == 'Texture':
                        fn = o.find('RelativeFilename') or o.find('FileName')
                        if fn is not None:
                            cand = glob.glob(os.path.join(self.proj.mod, 'Assets', '**', os.path.basename(fn.props[0].replace('\\', '/'))), recursive=True)
                            if cand: png = cand[0]; break
            if fbx_matname and png:
                tex = [self._t(png)] * 4
                self.notes.add('%s:%s: no material remap, Unity default material from the FBX approximated as %s, cutout' % (fbx.name, fbx_matname, os.path.basename(png)))
            else:
                self.notes.add('%s:%s: no material, Unity default material approximated as untextured' % (fbx.name if fbx else '?', fbx_matname))
        idx = len(self.list)
        self.list.append(dict(shader=shader, cull=cull, tex=tex, pal=pal, alpha=alpha, speed=speed, name=str(name)))
        self.index[key] = idx
        return idx
    def _t(self, png):
        rel = self.tex.get(png)[0]
        if rel not in self.tex_index: self.tex_index[rel] = len(self.tex_index)
        return self.tex_index[rel]
    tex_index = None

# ----------------------------------------------------------------------------------------------------------------------
# dream baking
# ----------------------------------------------------------------------------------------------------------------------
TRIG_KIND = {'TriggerLua': 'L', 'TriggerLink': 'K', 'TriggerTeleport': 'P', 'InteractiveObject': 'I'}
GATE_SCRIPTS = ('ChanceToAppear', 'AppearOnDay')
# Unity fileIDs of objects inside imported models are hashes this baker cannot reproduce (see ADDL_FORMAT.md); the few
# prefab overrides that target them are matched by node name instead, and only if the override count matches exactly
FBX_FIXUPS = [
    # (fbx file, property, value, node name regex, action)
    ('stg00.fbx', 'm_Enabled', '0', r'^(Flowers|FlowerPatch)(\.\d+)?$', 'nocollide'),    # 86 MeshColliders off: walk through flowers
    ('plant_pot.fbx', 'm_IsActive', '0', r'Shrub$', 'inactive'),                       # ChangingPlant: the 4 shrubs start hidden
    ('cat_bed.fbx', 'm_IsActive', '0', '<not root name>', 'inactive'),                 # STG13 cat beds renamed Lasagna / Marmite show that cat only
]

def tri_bytes(mat, flags, P, UV, COL):
    b = struct.pack('>HBB', mat, flags, 0)
    for k in range(3):
        b += struct.pack('>5f4B', P[k][0], P[k][1], P[k][2], UV[k][0], UV[k][1], *COL[k])
    return b

def col_bytes(c):
    if c is None: return (255, 255, 255, 255)
    return tuple(max(0, min(255, int(round(x * 255)))) for x in (c + (1.0,) * (4 - len(c)))[:4])

class DreamBaker:
    def __init__(self, proj, H, tex, out, cell, dream_paths, report):
        self.proj = proj; self.H = H; self.tex = tex; self.out = out; self.cell = cell
        self.dream_paths = dream_paths; self.report = report
        self.lua = {}; self.sounds = {}

    def ent_name(self, s):
        if '$$' in s:
            self.uniq += 1; s = s.replace('$$', 'u%d' % self.uniq)
        return s.replace(' ', chr(31)) or '_'

    def bake(self, idx, dpath, folder):
        P = self.proj; H = self.H
        dd = [x for x in P.docs(dpath) if x[0] == 114][0][4]
        prefab = os.path.join(P.mod, dd.get('DreamPrefabPath', '').replace('/', os.sep))
        R = dict(folder=folder, name=dd.get('Name'), notes=[], warn=[])
        self.R = R; self.uniq = 0
        P.unresolved.clear()
        objs = P.instantiate(prefab)
        S = Scene(P, objs)
        self.S = S
        self.apply_fixups(S, R)
        mats = Materials(P, self.tex); mats.tex_index = {}
        self.mats = mats
        # classify GameObjects
        ent_of = {}; gates = {}
        for g in S.walk():
            sc = dict((n, c) for n, c in S.scripts(g) if n)
            if any(n in sc for n in GATE_SCRIPTS):
                gd = {}
                for n in GATE_SCRIPTS:
                    if n in sc and str(S.o[sc[n]]['d'].get('m_Enabled', '1')) == '1':
                        gd[n] = S.o[sc[n]]['d']
                if gd: gates[g] = gd
            ids = [c for c in S.comp_list(g, 114) if 'ID' in S.o[c]['d']]
            if ids: ent_of[g] = ids
        self.gates = gates; self.ent_of = ent_of
        def owner(g):
            """nearest GameObject (self or ancestor) that is an entity or gated -> it owns g's geometry"""
            for a in S.ancestors(g):
                if a in ent_of or a in gates: return a
            return None
        # ---- static geometry and colliders
        tris = []      # (mat, flags, P3, UV3, COL3)
        stats = collections.Counter()
        dyn_renderers = collections.defaultdict(list)
        for g in S.walk():
            for c in S.comp_list(g):
                o = S.o[c]
                if o['cls'] not in (23, 137, 65, 136, 135, 64): continue
                own = owner(g)
                if o['cls'] in (23, 137):
                    if own is not None: dyn_renderers[own].append(c); continue
                    if not S.active_in_hierarchy(g) or str(o['d'].get('m_Enabled', '1')) != '1':
                        stats['static renderers inactive (dropped)'] += 1; continue
                    F = P.fbx(o['d']['fbx']); m = o['d']['model']
                    if S.name(g) == 'PlayerRef':
                        R['notes'].append('dropped FBX node PlayerRef (untextured size-reference box without material at %s)' % (
                            ' '.join('%.2f' % v for v in mxp(S.world(g), (0, 0, 0)))))
                        continue
                    coll = any(S.o[k]['cls'] == 64 and str(S.o[k]['d'].get('m_Enabled', '1')) == '1' for k in S.comp_list(g))
                    if o['cls'] == 137: R['notes'].append('static skinned mesh %s/%s baked in its bind pose' % (F.name, S.name(g)))
                    self.emit_mesh(tris, F, m, o['d'], S.world(g), coll, stats)
                elif o['cls'] in (65, 136, 135) and own is None:
                    if not S.active_in_hierarchy(g) or str(o['d'].get('m_Enabled', '1')) != '1': continue
                    if str(o['d'].get('m_IsTrigger', '0')) == '1': continue
                    self.emit_collider(tris, o, S.world(g), stats)
                elif o['cls'] == 64 and own is None:
                    if not any(S.o[k]['cls'] in (23, 137) for k in S.comp_list(g)):
                        R['warn'].append('MeshCollider without renderer on %s ignored' % S.name(g))
        R['stats'] = stats
        self.spawn_names = {}; self.ent_names = {}
        # ---- entities, models, spawns
        lines = []
        dl = lambda k, d: dd.get(k, d)
        lines.append('dream %d %s %s %s %s %s 2 0 %s %s' % (idx, folder, dl('WalkSpeed', '1.25'), dl('Linkable', '0'), dl('LinkWeighting', '1'),
                     dl('FirstDay', '0'), dl('EnvironmentOffset', '0'), dl('Name', folder)))
        lines.append('len %s' % dl('DreamLengthSeconds', '0'))
        lines.append('fx %s' % dl('HasEnvironmentEffects', '1'))
        lines.append('grey %s' % dl('GreyMan', '0'))
        lines.append('lgraph 0')
        lines.append('world %s/world.bin' % folder)
        lines.append('models %s/amodels.bin' % folder)
        if P.path(dd.get('SongLibrary')): R['warn'].append('dream has a SongLibrary; not converted')
        lines.append('songs revamped')
        if P.path(dd.get('DreamScript')): R['warn'].append('dream has a DreamScript; not converted')
        groups = [P.path(r) for r in (dd.get('EnvironmentGroups') or []) if isinstance(r, dict)]
        groups = [x for x in groups if x]
        if len(groups) == 4:
            for gi, gp in enumerate(groups):
                envs = H['env_seq'](gp); lines.append('group %d %d' % (gi, len(envs)))
                for e in envs: lines.append(H['env_line'](e))
        else:
            ep = P.path(dd.get('Environments'))
            envs = H['env_seq'](ep) if ep else []
            lines.append('group -1 %d' % len(envs))
            for e in envs: lines.append(H['env_line'](e))
        R['envs'] = len(envs)
        # gates
        gate_id = {}
        for g, gd in gates.items():
            gate_id[g] = len(gate_id)
            one_in = int(ffloat(gd['ChanceToAppear'].get('OneIn', 1), 1)) if 'ChanceToAppear' in gd else 1
            daymask = int(ffloat(gd['AppearOnDay'].get('Day', 0))) if 'AppearOnDay' in gd else -1
            lines.append('gate %d %d %d %s' % (gate_id[g], max(1, one_in), daymask, self.ent_name(S.name(g))))
        self.gate_id = gate_id
        amodels = AModels(self)
        self.amodels = amodels
        ents = []; spawns = []
        kinds = collections.Counter()
        # gated GameObjects that are no entity but own geometry, particles or a graph contribution: a pseudo entity
        # (kind T, name gate_<GameObject name>) carries them
        self.pseudo = set()
        for g in gates:
            if g in ent_of: continue
            own_pc = any(self._owner_entity(k) is None and S.comp_list(k, 198) for k in S.walk(g))
            if dyn_renderers.get(g) or own_pc or any(n == 'GraphContributor' for n, c in S.scripts(g)): self.pseudo.add(g)
        # runtime names first (BaseEntity renames its GameObject to its ID, '$$' made unique), so child0 / epath can use them
        for g in S.walk():
            if g in ent_of:
                comps = ent_of[g]
                body = next((S.o[c]['d'] for c in comps if P.script_name(S.o[c]['d']) in TRIG_KIND), S.o[comps[0]]['d'])
                self.ent_names[g] = self.ent_name(str(body.get('ID', '')) or S.name(g))
            elif g in self.pseudo:
                self.ent_names[g] = self.ent_name('gate_' + S.name(g))
        for g in S.walk():
            if g in ent_of or g in self.pseudo:
                e = self.entity(g, S, amodels, dyn_renderers, kinds, pseudo=g in self.pseudo)
                if e: ents.append(e)
            for n, c in S.scripts(g):
                if n == 'PlayerSpawn':
                    d = S.o[c]['d']; w = S.world(g)
                    p, q, s = mat_decompose(w)
                    yaw = math.degrees(2 * math.atan2(q[1], q[3]))
                    spawns.append('spawn %.4f %.4f %.4f %.1f %d %d %s' % (p[0], p[1], p[2], yaw, int(ffloat(d.get('DayOneSpawn', 0))),
                                  int(ffloat(d.get('TunnelEntrance', 0))), self.ent_names.get(g, '_')))
        lines += ents + spawns
        R['entities'] = dict(kinds); R['spawns'] = len(spawns); R['gates'] = len(gates)
        R['models'] = amodels.write(os.path.join(self.out, folder, 'amodels.bin'))
        # ---- chunks (after the models: they add to the material table that world.bin carries)
        self.write_world(folder, tris, mats, R)
        R['materials'] = mats.list; R['textures'] = dict(mats.tex_index)
        R['matnotes'] = sorted(mats.notes)
        R['unresolved'] = dict(P.unresolved)
        R['tris'] = tris
        R['lines'] = lines
        return lines

    # ---- fixups for overrides on objects inside imported models
    def apply_fixups(self, S, R):
        left = collections.Counter(); done = collections.Counter()
        for root, o in list(S.go.items()):
            if 'unres' not in o: continue
            cnt = collections.Counter(o['unres'])
            for (src, prop, val), n in cnt.items():
                if prop in ('m_IsActive', 'm_Enabled') and val == '1':
                    done[(src.split('/')[-1], prop, val, 'no-op, imported objects are active/enabled already')] += n; continue
                rule = next((r for r in FBX_FIXUPS if src.endswith('/' + r[0]) and r[1] == prop and r[2] == val), None)
                if rule is None: left[(src, prop, val)] += n; continue
                fbxname, _, _, rx, action = rule
                hits = [g for g in S.walk(root) if S.go[g]['asset'].endswith('/' + fbxname) or S.go[g]['asset'].endswith(os.sep + fbxname)]
                hits = [g for g in hits if 'fbxnode' in S.go[g] and 'fbxroot' not in S.go[g]]
                if rx == '<not root name>': hits = [g for g in hits if S.name(g) != S.name(root)]
                else: hits = [g for g in hits if re.search(rx, S.name(g))]
                if len(hits) != n:
                    R['warn'].append('fixup %s %s=%s: %d overrides but %d nodes match /%s/; not applied' % (fbxname, prop, val, n, len(hits), rx))
                    left[(src, prop, val)] += n; continue
                for g in hits:
                    if action == 'inactive': S.go[g]['d']['m_IsActive'] = '0'
                    elif action == 'nocollide':
                        for c in S.comp_list(g, 64): S.o[c]['d']['m_Enabled'] = '0'
                done[(fbxname, prop, val, action)] += n
        for k, n in done.items(): R['notes'].append('fixup %s: %d overrides %s=%s matched by node name (%s)' % (k[0], n, k[1], k[2], k[3]))
        P = self.proj
        P.unresolved.clear(); P.unresolved.update(left)

    # ---- geometry
    def mesh_tris(self, F, m, rd):
        """triangles of FBX model m with this renderer's materials: [(mat, P3 (mesh space), UV3, COL3)]"""
        mats = rd.get('materials') or []
        names = rd.get('matnames') or []
        out = []
        for (slot, Ps, UVs, COLs, cps) in F.mesh(m):
            mp = mats[slot] if slot < len(mats) else None
            mi = self.mats.get(mp, names[slot] if slot < len(names) else None, F)
            uv = tuple((u, 1.0 - v) for (u, v) in UVs)       # top-left origin
            out.append((mi, Ps, uv, tuple(col_bytes(c) for c in COLs), cps))
        return out

    def emit_mesh(self, tris, F, m, rd, world, collide, stats):
        for (mi, Ps, uv, col, cps) in self.mesh_tris(F, m, rd):
            wp = tuple(mxp(world, p) for p in Ps)
            if _det3(world) < 0: wp = (wp[0], wp[2], wp[1]); uv = (uv[0], uv[2], uv[1]); col = (col[0], col[2], col[1])
            flags = 1 | (2 if collide else 0)
            tris.append((mi, flags, wp, uv, col))
            stats['render tris'] += 1
            if collide: stats['render+collide tris'] += 1
            self.R.setdefault('bynode', collections.Counter())[F.name + ':' + re.sub(r'\.\d+$', '', F.mname[m])] += 1

    def emit_collider(self, tris, o, world, stats):
        d = o['d']; c = v3(d.get('m_Center'))
        mi = self.mats.get(None, 'collider', None)
        if o['cls'] == 65:
            s = v3(d.get('m_Size'), (1, 1, 1))
            box = [(c[0] + sx * s[0] / 2, c[1] + sy * s[1] / 2, c[2] + sz * s[2] / 2) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
            faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
            for f in faces:
                for (a, b, cc) in ((f[0], f[1], f[2]), (f[0], f[2], f[3])):
                    tris.append((mi, 2, tuple(mxp(world, box[k]) for k in (a, b, cc)), ((0, 0),) * 3, ((255, 255, 255, 255),) * 3))
                    stats['collider-only tris'] += 1
        else:
            r = ffloat(d.get('m_Radius'), 0.5); h = ffloat(d.get('m_Height'), 1.0) if o['cls'] == 136 else 2 * r
            ax = int(ffloat(d.get('m_Direction', 1), 1)) if o['cls'] == 136 else 1
            n = 8; ring = []
            for k in range(n):
                a = 2 * math.pi * k / n
                ring.append((math.cos(a) * r, math.sin(a) * r))
            def pt(u, v, w):      # (radial u, radial v, axial w) -> local
                if ax == 0: return (c[0] + w, c[1] + u, c[2] + v)
                if ax == 2: return (c[0] + u, c[1] + v, c[2] + w)
                return (c[0] + u, c[1] + w, c[2] + v)
            for k in range(n):
                u0, v0 = ring[k]; u1, v1 = ring[(k + 1) % n]
                q = [pt(u0, v0, -h / 2), pt(u1, v1, -h / 2), pt(u1, v1, h / 2), pt(u0, v0, h / 2)]
                for (a, b, cc) in ((0, 1, 2), (0, 2, 3)):
                    tris.append((mi, 2, tuple(mxp(world, q[x]) for x in (a, b, cc)), ((0, 0),) * 3, ((255, 255, 255, 255),) * 3)); stats['collider-only tris'] += 1
            for sgn in (-1, 1):
                for k in range(1, n - 1):
                    tri = (pt(*ring[0], sgn * h / 2), pt(*ring[k], sgn * h / 2), pt(*ring[k + 1], sgn * h / 2))
                    tris.append((mi, 2, tuple(mxp(world, p) for p in tri), ((0, 0),) * 3, ((255, 255, 255, 255),) * 3)); stats['collider-only tris'] += 1
            self.R['notes'].append('static %s collider approximated by an 8-sided prism (%d tris)' % ('capsule' if o['cls'] == 136 else 'sphere', 2 * n + 2 * (n - 2)))

    def write_world(self, folder, tris, mats, R):
        cell = self.cell
        chunks = collections.defaultdict(list)
        for t in tris:
            cx = sum(p[0] for p in t[2]) / 3; cz = sum(p[2] for p in t[2]) / 3
            chunks[(int(math.floor(cx / cell)), int(math.floor(cz / cell)))].append(t)
        allp = [p for t in tris for p in t[2]]
        bmin = tuple(min(p[k] for p in allp) for k in range(3)) if allp else (0, 0, 0)
        bmax = tuple(max(p[k] for p in allp) for k in range(3)) if allp else (0, 0, 0)
        texlist = sorted(mats.tex_index.items(), key=lambda kv: kv[1])
        b = bytearray(b'AWLD' + struct.pack('>I', 1) + struct.pack('>f', cell))
        b += struct.pack('>H', len(texlist))
        for rel, i in texlist:
            ent = [v for v in self.tex.files.values() if v[0] == rel][0]
            b += _str(rel) + struct.pack('>HH', ent[1], ent[2])
        b += struct.pack('>H', len(mats.list))
        for m in mats.list:
            b += struct.pack('>BB4h4hff', m['shader'], m['cull'], *m['tex'], *m['pal'], m['alpha'], m['speed']) + _str(m['name'])
        b += struct.pack('>6f', *bmin, *bmax)
        keys = sorted(chunks)
        b += struct.pack('>I', len(keys))
        R['chunks'] = []
        for k in keys:
            ts = chunks[k]
            ps = [p for t in ts for p in t[2]]
            cmin = tuple(min(p[i] for p in ps) for i in range(3)); cmax = tuple(max(p[i] for p in ps) for i in range(3))
            b += struct.pack('>hh6fI', k[0], k[1], *cmin, *cmax, len(ts))
            for t in ts: b += tri_bytes(*t)
            R['chunks'].append((k, len(ts), sum(1 for t in ts if t[1] & 1)))
        os.makedirs(os.path.join(self.out, folder), exist_ok=True)
        open(os.path.join(self.out, folder, 'world.bin'), 'wb').write(b)
        R['world_bytes'] = len(b); R['bbox'] = (bmin, bmax)

    # ---- entities
    def entity(self, g, S, amodels, dyn_renderers, kinds, pseudo=False):
        P = self.proj
        comps = self.ent_of.get(g, [])
        kind = None; body = None
        for c in comps:
            n = P.script_name(S.o[c]['d'])
            if n in TRIG_KIND and kind is None: kind = TRIG_KIND[n]; body = S.o[c]['d']
        if kind is None and comps: kind = 'T'; body = S.o[comps[0]]['d']
        if pseudo: kind = 'T'; body = {}
        w = S.world(g)
        p, q, s = mat_decompose(w)
        yaw = math.degrees(2 * math.atan2(q[1], q[3]))
        box = (0, 0, 0, 1, 1, 1)
        for c in S.comp_list(g, 65):
            d = S.o[c]['d']; box = v3(d.get('m_Center')) + v3(d.get('m_Size'), (1, 1, 1)); break
        nm = self.ent_names[g]
        extra = ''
        if kind == 'L':
            sc = self.lua_ref(body.get('Script'))
            extra = ' %s %s %s %s' % (sc, body.get('TriggerFunctionName') or 'onTrigger', body.get('TriggerExitFunctionName') or 'onTriggerExit', body.get('OnlyOnce', '0'))
        elif kind == 'K':
            lk = P.path(body.get('Linked')); li = self.dream_paths.index(lk) if lk in self.dream_paths else -1
            if lk and li < 0: self.R['warn'].append('link %s -> %s is not an additional dream' % (nm, lk))
            extra = ' %d %s %s %s %s' % (li, body.get('SpawnPointEntityID') or '-', body.get('LockInput', '0'), body.get('AffectsLinksWithin', '0'), body.get('OnlyOnce', '0'))
        elif kind == 'P':
            dst = v3(body.get('Destination'))
            extra = ' %.4f %.4f %.4f %s %s' % (*dst, body.get('AlwaysOn', '0'), body.get('OnlyOnce', '0'))
        elif kind == 'I':
            extra = ' %s %s %s' % (self.lua_ref(body.get('Script')), body.get('InteractionKind', '0'), body.get('InteractionDistance', '3'))
        kinds[kind] += 1
        active = 1 if S.active_in_hierarchy(g) else 0
        # models: every imported-model instance below this entity (not below a nested entity / gate)
        mlines, firstkey, nmodel, nmesh = amodels.entity_parts(g, w, p, yaw, s, dyn_renderers)
        ch = S.children.get(S.go_tr[g], [])
        child0 = self.rt_name(S.tr_go[ch[0]]) if ch else '-'
        line = 'ent %s %s %d %.4f %.4f %.4f %.2f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f%s %s %d %d %s' % (
            kind, nm, active, p[0], p[1], p[2], yaw, *s, *box, extra, firstkey, nmodel, nmesh, child0)
        out = [line] + mlines
        # extra lines (ADDL_FORMAT.md): nesting, gates, audio, graph contributions, colliders, animated object
        par = None
        for a in S.ancestors(g)[1:]:
            if a in self.ent_names: par = a; break
        if par is not None:
            out.append('epath %s %s' % (self.ent_names[par], self.rt_path(g, par)))
        gl = [self.gate_id[a] for a in S.ancestors(g) if a in self.gate_id]
        if gl: out.append('gates %d %s' % (len(gl), ' '.join(str(x) for x in gl)))
        for n, c in S.scripts(g):
            d = S.o[c]['d']
            if n == 'DreamAudio':
                clip = self.sound(d.get('Clip'))
                out.append('audio %s %s %s %s %s %s %s %s %s %s %s %s %s' % (clip, d.get('Spatial', '1'), d.get('FullVolumeRangeUnits', '3'), d.get('RangeUnits', '10'),
                           d.get('Pitch', '1'), d.get('Volume', '1'), d.get('DelayBetweenPlaysSeconds', '1'), d.get('PlayClipForSeconds', '0'),
                           d.get('DelayBeforePlayingSeconds', '0'), d.get('WaitUntilAudibleBeforePlaying', '1'), d.get('Loop', '0'), d.get('OneShot', '0'),
                           d.get('ControlledWithScript', '0')))
            elif n == 'GraphContributor':
                vec = d.get('Contribution', {}).get('_vector', {}) if isinstance(d.get('Contribution'), dict) else {}
                src = fid(d.get('SourceEntity', {}))
                out.append('gcontrib %d %d %s' % (int(ffloat(vec.get('x'))), int(ffloat(vec.get('y'))), d.get('ContributionRadius', '5')))
            elif n == 'InteractiveObject':
                ao = fid(d.get('AnimatedObject', {}))
                if ao and ao in S.o:
                    aog = fid(S.o[ao]['d'].get('m_GameObject', {}))
                    out.append('aobj %s' % self.rt_path(aog, g))
                if str(d.get('CallStartOnEnable', '0')) == '1': out.append('startonenable 1')
        for c in S.comp_list(g):
            o = S.o[c]
            # BaseTrigger (L, K, P) turns its BoxCollider into a trigger in Init(): that box is the ent line's box, not a solid
            if o['cls'] in (136, 135) or (o['cls'] == 65 and str(o['d'].get('m_IsTrigger', '0')) != '1' and kind not in 'LKP'):
                d = o['d']; cen = v3(d.get('m_Center'))
                if o['cls'] == 65: out.append('ecol box %d %.4f %.4f %.4f %.4f %.4f %.4f' % (int(ffloat(d.get('m_Enabled', 1), 1)), *cen, *v3(d.get('m_Size'), (1, 1, 1))))
                elif o['cls'] == 136: out.append('ecol capsule %d %.4f %.4f %.4f %s %s %s' % (int(ffloat(d.get('m_Enabled', 1), 1)), *cen, d.get('m_Radius', '0.5'), d.get('m_Height', '1'), d.get('m_Direction', '1')))
                else: out.append('ecol sphere %d %.4f %.4f %.4f %s' % (int(ffloat(d.get('m_Enabled', 1), 1)), *cen, d.get('m_Radius', '0.5')))
        # particle systems (Steam, DripPuddle): not geometry; the main emitter parameters for a runtime approximation
        for k in S.walk(g):
            if self._owner_entity(k) != g: continue
            for c in S.comp_list(k, 198):
                out.append(self.particles(S, c, k, g))
        # colliders on non-entity children of the entity (e.g. ChangingTree's capsule "collider" child)
        for k in S.walk(g):
            if k == g or self._owner_entity(k) != g: continue
            for c in S.comp_list(k):
                o = S.o[c]
                if o['cls'] in (65, 136, 135) and str(o['d'].get('m_IsTrigger', '0')) != '1':
                    rel = mmul(minv(S.world(g)), S.world(k))
                    d = o['d']; cen = mxp(rel, v3(d.get('m_Center')))
                    sc_ = mat_decompose(rel)[2]
                    if o['cls'] == 65:
                        sz = v3(d.get('m_Size'), (1, 1, 1)); sz = tuple(abs(a * b) for a, b in zip(sz, sc_))
                        out.append('ecol box %d %.4f %.4f %.4f %.4f %.4f %.4f %s' % (1 if S.active_in_hierarchy(k, g) else 0, *cen, *sz, self.rt_path(k, g)))
                    elif o['cls'] == 136:
                        out.append('ecol capsule %d %.4f %.4f %.4f %.4f %.4f %s %s' % (1 if S.active_in_hierarchy(k, g) else 0, *cen, ffloat(d.get('m_Radius'), .5) * abs(sc_[0]),
                                   ffloat(d.get('m_Height'), 1) * abs(sc_[1]), d.get('m_Direction', '1'), self.rt_path(k, g)))
                    else:
                        out.append('ecol sphere %d %.4f %.4f %.4f %.4f %s' % (1 if S.active_in_hierarchy(k, g) else 0, *cen, ffloat(d.get('m_Radius'), .5) * abs(sc_[0]), self.rt_path(k, g)))
        return '\n'.join(out)

    def particles(self, S, c, k, g):
        d = S.o[c]['d']
        def sc(mod, key, sub='scalar'):
            m = d.get(mod, {}) if mod else d
            v = m.get(key, {}) if isinstance(m, dict) else {}
            if isinstance(v, dict):
                if sub == 'range': return '%s,%s' % (v.get('minScalar', '0'), v.get('scalar', '0'))
                return str(v.get(sub, '0'))
            return str(v)
        def rng(mod, key):
            m = d.get(mod, {}); v = m.get(key, {}) if isinstance(m, dict) else {}
            if not isinstance(v, dict): return '%s %s' % (v, v)
            st = str(v.get('minMaxState', '0'))
            return '%s %s' % ((v.get('minScalar', '0') if st == '3' else v.get('scalar', '0')), v.get('scalar', '0'))   # state 3 = random between two constants
        ic = d.get('InitialModule', {}); col = ic.get('startColor', {}) if isinstance(ic, dict) else {}
        mc = col.get('maxColor', {}) if isinstance(col, dict) else {}
        tex = '-'
        for rc in S.comp_list(k, 199):
            mats = S.o[rc]['d'].get('m_Materials') or []
            mp = self.proj.path(mats[0]) if mats else None
            if mp:
                sname, texs, floats, nm = self.mats._matinfo(mp)
                if texs.get('_MainTex'): tex = self.tex.get(texs['_MainTex'])[0]
        sh = d.get('ShapeModule', {})
        rad = sh.get('radius', {}).get('value', '1') if isinstance(sh.get('radius'), dict) else sh.get('radius', '1')
        self.R['notes'].append('particle system %s: emitter parameters exported as a particles line, no geometry' % S.name(k))
        return 'particles %s %s %s %s %s %s %s %s %s %s %s %s %s %s %.3f %.3f %.3f %.3f' % (
            self.rt_path(k, g), tex, d.get('looping', '1'), d.get('prewarm', '0'), d.get('lengthInSec', '5'),
            rng('InitialModule', 'startLifetime').replace(' ', ' '), rng('InitialModule', 'startSpeed'), rng('InitialModule', 'startSize'),
            rng('EmissionModule', 'rateOverTime').split()[1], ic.get('maxNumParticles', '1000') if isinstance(ic, dict) else '1000',
            rng('InitialModule', 'gravityModifier').split()[1], sh.get('type', '4') if isinstance(sh, dict) else '4',
            sh.get('angle', '25') if isinstance(sh, dict) else '25', rad,
            ffloat(mc.get('r'), 1), ffloat(mc.get('g'), 1), ffloat(mc.get('b'), 1), ffloat(mc.get('a'), 1))

    def _owner_entity(self, g):
        for a in self.S.ancestors(g):
            if a in self.ent_of or a in getattr(self, 'pseudo', ()): return a
        return None

    def rt_name(self, g):
        """runtime GameObject name: an entity's GameObject is renamed to its ID"""
        return (self.ent_names.get(g) or self.S.name(g)).replace(' ', chr(31))

    def rt_path(self, g, root):
        names = []
        while g is not None and g != root:
            names.append(self.rt_name(g)); g = self.S.father_go(g)
        return '/'.join(reversed(names)) or '.'

    def lua_ref(self, ref):
        p = self.proj.path(ref)
        if not p: return '-'
        name = os.path.basename(p)
        self.lua[name] = p
        return 'ADDLUA/' + name

    def sound(self, ref):
        p = self.proj.path(ref)
        if not p: return '-'
        name = os.path.splitext(os.path.basename(p))[0] + '.wav'
        self.sounds[name] = p
        return 'ADDSND/' + name

def _det3(m):
    return (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1]) - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0]) + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))
def _str(s):
    b = str(s).encode('utf8')[:255]
    return bytes([len(b)]) + b
def _trs_close(a, b, eps=1e-4):
    return all(abs(x - y) < eps for x, y in zip(a, b))

FPS_BY_TIMEMODE = {1: 120, 2: 100, 3: 60, 4: 50, 5: 48, 6: 30, 7: 30, 8: 25, 9: 29.97, 10: 24, 12: 96, 13: 72, 14: 59.94}

def fbx_fps(F):
    root, _ = fbx_parse(F.path)
    gs = root.find('GlobalSettings').p70()
    tm = int(gs.get('TimeMode', [0])[0])
    if tm in (0, 11): return float(gs.get('CustomFrameRate', [24.0])[0]) if gs.get('CustomFrameRate', [-1])[0] > 0 else 24.0
    return float(FPS_BY_TIMEMODE.get(tm, 24))

class AModels:
    """animated / scripted object models of one dream (amodels.bin) and the model/mpose lines of its entities"""
    def __init__(self, db):
        self.db = db; self.proj = db.proj; self.S = db.S
        self.models = collections.OrderedDict()       # key -> dict(meshes, objects, anims, nodeidx)
        self.notes = db.R['notes']

    def clips_of(self, F):
        """clip definitions of an FBX (Unity ModelImporter): [(fileID or None, name, take, first, last, loop)]"""
        meta = self.proj.meta(F.path)
        out = []
        sec = meta.split('clipAnimations:', 1)[1] if 'clipAnimations:' in meta else ''
        sec = sec.split('isReadable:', 1)[0]
        if sec.strip().startswith('[]') or not sec.strip():
            fps = fbx_fps(F)
            for name, a in F.anims.items():
                out.append((None, name, name, int(round(a['start'] * fps)), int(round(a['stop'] * fps)), False))
            return out
        for blk in re.split(r'\n\s+- serializedVersion: \d+\n', sec)[1:]:
            g = lambda k, d=None: (re.search(r'\n?\s+' + k + r': (.*)', blk).group(1).strip() if re.search(r'\n?\s+' + k + r': (.*)', blk) else d)
            out.append((int(g('internalID', '0')) or None, g('name'), g('takeName'), int(float(g('firstFrame', '0'))), int(float(g('lastFrame', '0'))), g('loopTime', '0') == '1'))
        return out

    def build_model(self, F, root_go, key):
        """objects / meshes / anims of the imported model F, from the instance rooted at root_go (structure only)"""
        S = self.S
        objs = []; nodeidx = {}
        def add(g, parent):
            m = S.go[g].get('fbxnode')
            if g == root_go: pos, rot, sc = (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
            else:
                L = F.unity_local(m) if m is not None else mident()
                pos, rot, sc = mat_decompose(L)
            idx = len(objs)
            objs.append(dict(parent=parent, name=S.name(g) if g != root_go else F.name, node=m, pos=pos, rot=rot, scale=sc, active=1, meshes=[]))
            if m is not None: nodeidx[m] = idx
            for t in S.children.get(S.go_tr[g], []):
                k = S.tr_go.get(t)
                if k is not None and S.go[k]['asset'] == F.path and 'fbxnode' in S.go[k]: add(k, idx)
        add(root_go, -1)
        # names of the model's objects are the FBX node names (the instance may rename the root only)
        for o in objs:
            if o['node'] is not None and o is not objs[0]: o['name'] = F.mname[o['node']]
        meshes = []
        for o_i, o in enumerate(objs):
            m = o['node']
            if m is None or F.geo[m] is None: continue
            rd = None
            for g in S.go:
                pass
            sk = F.skin(m)
            # renderer data (materials) from the synthesized component of this node in any instance: same per FBX
            rd = dict(materials=[F.external.get(n) for n in F.mats[m]], matnames=F.mats[m])
            tris = self.db.mesh_tris(F, m, rd)
            if not sk:
                meshes.append([(mi, 1, Ps, uv, col) for (mi, Ps, uv, col, cps) in tris]); o['meshes'].append(len(meshes) - 1)
                continue
            # skinned: rigid binding, each triangle follows the bone with the largest summed weight of its corners
            wt = collections.defaultdict(dict)
            for (bone, ws, T, TL) in sk:
                for cp, w in ws.items(): wt[cp][bone] = w
            bind = {bone: (T, TL) for (bone, ws, T, TL) in sk}
            per_bone = collections.defaultdict(list)
            for (mi, Ps, uv, col, cps) in tris:
                score = collections.Counter()
                for cp in cps:
                    for b, w in wt.get(cp, {}).items(): score[b] += w
                b = score.most_common(1)[0][0] if score else None
                per_bone[b].append((mi, Ps, uv, col))
            for b, ts in per_bone.items():
                if b is None or b not in nodeidx:
                    meshes.append([(mi, 1, Ps, uv, col) for (mi, Ps, uv, col) in ts]); o['meshes'].append(len(meshes) - 1); continue
                T, TL = bind[b]
                # mesh-local (Unity) -> bone-local (Unity) at bind time: C TL^-1 MeshGlobal C^-1. The cluster's Transform
                # is not used: Blender writes TL^-1 * MeshGlobal there instead of the mesh's global matrix FBX specifies
                Mx = mmul(mmul(F.C, mmul(minv(TL), F.fbx_global(m))), F.Ci)
                meshes.append([(mi, 1, tuple(mxp(Mx, p) for p in Ps), uv, col) for (mi, Ps, uv, col) in ts])
                objs[nodeidx[b]]['meshes'].append(len(meshes) - 1)
            self.notes.append('%s: skinned mesh %s rigidly bound to %d bones (%d tris)' % (F.name, F.mname[m], len(per_bone), len(tris)))
        return dict(key=key, F=F, objects=objs, meshes=meshes, nodeidx=nodeidx, anims=[], animkey={})

    def add_anim(self, M, clip):
        """sample clip (fileID, name, take, first, last, loop) into tracks; returns anim index"""
        F = M['F']
        cid, name, take, first, last, loop = clip
        if name in M['animkey']: return M['animkey'][name]
        if take not in F.anims:
            cand = [t for t in F.anims if t.split('|')[-1] == take or t.rstrip('0123456789').rstrip('.') == take]
            if not cand: self.notes.append('%s: animation take %s not found' % (F.name, take)); return -1
            take = cand[0]
        fps = fbx_fps(F)
        tracks = []
        for m, oi in M['nodeidx'].items():
            if m not in F.anims[take]['curves']: continue
            keys = []
            # object 0 is the model root, whose placement is the model line's TRS: its keys are made relative to the
            # imported rest pose (identity when the clip only holds the rest pose, e.g. Blender's armature object)
            rest_inv = minv(F.unity_local(m)) if oi == 0 else None
            for f in range(first, max(first, last) + 1):
                ov = F.sample(take, m, f / fps)
                L = F.unity_local(m, ov)
                if rest_inv is not None: L = mmul(rest_inv, L)
                keys.append((f - first,) + mat_decompose(L))
            # drop keys inside constant runs (a key is kept when it differs from its previous or next key)
            same = lambda a, b: _trs_close(a[1], b[1], 1e-6) and _trs_close(a[2], b[2], 1e-6) and _trs_close(a[3], b[3], 1e-6)
            keys = [k for i, k in enumerate(keys) if i == 0 or i == len(keys) - 1 or not same(k, keys[i - 1]) or not same(k, keys[i + 1])]
            if len(keys) == 2 and same(keys[0], keys[1]): keys = keys[:1]
            tracks.append((oi, keys))
        M['anims'].append(dict(name=name, fps=fps, length=max(0, last - first), loop=1 if loop else 0, tracks=tracks))
        M['animkey'][name] = len(M['anims']) - 1
        return M['animkey'][name]

    def entity_parts(self, ent_g, ent_world, ent_pos, ent_yaw, ent_scale, dyn_renderers):
        S = self.S; P = self.proj
        roots = []
        for c in dyn_renderers.get(ent_g, []):
            g = S.o[c]['d']['m_GameObject']; g = fid(g)
            r = None
            for a in S.ancestors(g):
                if 'fbxroot' in S.go[a]: r = a; break
            if r is None: self.db.R['warn'].append('renderer %s without model root' % S.name(g)); continue
            if r not in roots: roots.append(r)
        # an entity can also own a model with no renderer of its own being owned (all owned by nested entities): skip
        roots.sort(key=lambda r: list(S.walk(ent_g)).index(r) if r in set(S.walk(ent_g)) else 1 << 30)
        frame = trs_mat(ent_pos, qaxis((0, 1, 0), ent_yaw), ent_scale)
        inv_frame = minv(frame)
        lines = []; first = '-'
        for r in roots:
            F = P.fbx(S.go[r]['fbxroot'])
            key = P.rel(F.path)
            if key not in self.models: self.models[key] = self.build_model(F, r, key)
            M = self.models[key]
            # animator / AnimatedObject on the model root (Unity binds clip curves relative to the Animator's GameObject)
            autoplay = -1
            ao = [S.o[c]['d'] for n, c in S.scripts(r) if n == 'AnimatedObject']
            anim_go = [c for c in S.comp_list(r, 95)]
            ctrl = P.path(S.o[anim_go[0]]['d'].get('m_Controller')) if anim_go else None
            clips = self.clips_of(F)
            byid = {c[0]: c for c in clips if c[0]}
            states = []; default_state = None
            if ctrl:
                cdocs = P.docs(ctrl)
                st = {fid_: d for cls, fid_, _, _, d in cdocs if cls == 1102}
                for cls, fid_, _, _, d in cdocs:
                    if cls == 1107: default_state = fid(d.get('m_DefaultState', {}))
                for sid, d in st.items():
                    mo = d.get('m_Motion', {})
                    # clip by the meta's internalID, else (clips of an FBX without clipAnimations have hash IDs) by
                    # state name = clip name (AnimatedObject.Play does Animator.Play(clip.name))
                    clip = byid.get(fid(mo)) or next((c for c in clips if c[1] == d.get('m_Name')), None)
                    states.append((sid, d.get('m_Name'), clip, fid(mo)))
            idx_of = {}
            if ao:
                for k, cref in enumerate(ao[0].get('Clips') or []):
                    clip = byid.get(fid(cref))
                    if clip is None:     # clip fileID not in the meta (hash-generated): find the controller state that plays it
                        st_ = next((s for s in states if s[2] and fid(cref) and s[3] == fid(cref)), None)
                        clip = st_[2] if st_ else None
                    if clip is None:
                        self.db.R['warn'].append('%s: AnimatedObject clip %d (%s) not resolved' % (F.name, k, fid(cref))); continue
                    idx_of[k] = self.add_anim(M, clip)
                ap = int(ffloat(ao[0].get('AutoPlay', -1), -1))
                if ap >= 0 and ap in idx_of: autoplay = idx_of[ap]
            for sid, sname, clip, _ in states:
                if clip: ai = self.add_anim(M, clip)
                if sid == default_state and clip and autoplay < 0 and anim_go and str(S.o[anim_go[0]]['d'].get('m_Enabled', '1')) == '1':
                    autoplay = M['animkey'][clip[1]]      # an enabled Animator plays its default state on enable
            rel = mmul(inv_frame, S.world(r))
            p, q, s = mat_decompose(rel)
            active = 1 if all(S.active_self(a) for a in S.ancestors(r)[:S.ancestors(r).index(ent_g)] if True) else 0
            poses = []
            for g in S.walk(r):
                if g == r or S.go[g].get('fbxnode') is None or S.go[g]['asset'] != F.path: continue
                oi = M['nodeidx'].get(S.go[g]['fbxnode'])
                if oi is None: continue
                o = M['objects'][oi]
                pp, qq, ss = S.local(g)
                act = 1 if S.active_self(g) else 0
                if not (_trs_close(pp, o['pos']) and (_trs_close(qq, o['rot']) or _trs_close(qq, tuple(-x for x in o['rot']))) and _trs_close(ss, o['scale'])) or act != o['active']:
                    poses.append('mpose %s %d %.5f %.5f %.5f %.6f %.6f %.6f %.6f %.5f %.5f %.5f' % (o['name'].replace(' ', chr(31)), act, *pp, *qq, *ss))
            path = self.db.rt_path(r, ent_g)
            lines.append('model %s %s %d %d %.5f %.5f %.5f %.6f %.6f %.6f %.6f %.5f %.5f %.5f %d' % (key, path, autoplay, active, *p, *q, *s, len(poses)))
            lines += poses
            if ao:     # AnimatedObject.Clips[k] -> model anim index (PlayAnimation(k)); -1 = clip not found
                n = len(ao[0].get('Clips') or [])
                lines.append('mclips %d%s' % (n, ''.join(' %d' % idx_of.get(k, -1) for k in range(n))))
            if first == '-': first = key
        return lines, first, len(roots), 0

    def write(self, path):
        b = bytearray(b'AMDL' + struct.pack('>II', 1, len(self.models)))
        st = dict(models=len(self.models), tris=0, anims=0, keys=0)
        for key, M in self.models.items():
            b += _str(key) + struct.pack('>H', len(M['meshes']))
            for ms in M['meshes']:
                b += struct.pack('>I', len(ms))
                for t in ms: b += tri_bytes(*t)
                st['tris'] += len(ms)
            b += struct.pack('>H', len(M['objects']))
            for o in M['objects']:
                b += struct.pack('>h', o['parent']) + _str(o['name']) + struct.pack('>10fBH', *o['pos'], *o['rot'], *o['scale'], o['active'], len(o['meshes']))
                b += b''.join(struct.pack('>H', x) for x in o['meshes'])
            b += struct.pack('>H', len(M['anims']))
            for a in M['anims']:
                b += _str(a['name']) + struct.pack('>fHBH', a['fps'], a['length'], a['loop'], len(a['tracks']))
                st['anims'] += 1
                for oi, keys in a['tracks']:
                    b += struct.pack('>HH', oi, len(keys))
                    for (f, p, q, s) in keys: b += struct.pack('>H10f', f, *p, *q, *s); st['keys'] += 1
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(b)
        st['bytes'] = len(b)
        st['list'] = [(k, sum(len(m) for m in M['meshes']), len(M['objects']), [a['name'] for a in M['anims']]) for k, M in self.models.items()]
        return st

# ----------------------------------------------------------------------------------------------------------------------
# previews (top-down orthographic, z-buffered, textured with texture set A)
# ----------------------------------------------------------------------------------------------------------------------
def preview(R, mats, tex, path, scale=None, side=False, extra_pts=()):
    from PIL import Image, ImageDraw
    tris = [t for t in R['tris'] if t[1] & 1]
    if not tris: return
    (x0, y0, z0), (x1, y1, z1) = R['bbox']
    W = 1024
    if side: ax0, ax1, dep = 0, 1, 2       # side view: x right, y up, looking along +z
    else: ax0, ax1, dep = 0, 2, 1          # top-down: x right, z up (north), looking down -y
    lo = (x0, y0, z0); hi = (x1, y1, z1)
    span = max(hi[ax0] - lo[ax0], hi[ax1] - lo[ax1], 1e-3)
    sc = scale or (W - 20) / span
    w = int((hi[ax0] - lo[ax0]) * sc) + 20; h = int((hi[ax1] - lo[ax1]) * sc) + 20
    img = Image.new('RGB', (w, h), (40, 40, 48)); px = img.load()
    zb = [[-1e30] * w for _ in range(h)]
    texs = {}
    for rel, (r_, tw, th, sw, sh_, im) in [(v[0], v) for v in tex.files.values()]:
        texs[rel] = (tw, th, im.load())
    texlist = {i: rel for rel, i in R['textures'].items()}
    def to2(p): return ((p[ax0] - lo[ax0]) * sc + 10, h - 10 - (p[ax1] - lo[ax1]) * sc, (p[dep] if not side else -p[dep]))
    for (mi, fl, P3, UV, COL) in tris:
        m = mats[mi]
        # back-face cull against the view direction (front side = cross(b - a, c - a)), so ceilings vanish from above;
        # if the winding were wrong the floors would vanish instead
        e1 = [P3[1][k] - P3[0][k] for k in range(3)]; e2 = [P3[2][k] - P3[0][k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if (not side and n[1] < 0) or (side and n[2] > 0): continue
        t = texlist.get(m['tex'][0]) if m['tex'][0] >= 0 else None
        tx = texs.get(t) if t else None
        a, b, c = [to2(p) for p in P3]
        minx = max(0, int(min(a[0], b[0], c[0]))); maxx = min(w - 1, int(max(a[0], b[0], c[0])) + 1)
        miny = max(0, int(min(a[1], b[1], c[1]))); maxy = min(h - 1, int(max(a[1], b[1], c[1])) + 1)
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-9: continue
        for y in range(miny, maxy + 1):
            for x in range(minx, maxx + 1):
                l0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / den
                l1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / den
                l2 = 1 - l0 - l1
                if l0 < -0.01 or l1 < -0.01 or l2 < -0.01: continue
                z = l0 * a[2] + l1 * b[2] + l2 * c[2]
                if z <= zb[y][x]: continue
                col = [l0 * COL[0][k] + l1 * COL[1][k] + l2 * COL[2][k] for k in range(3)]
                if tx:
                    u = l0 * UV[0][0] + l1 * UV[1][0] + l2 * UV[2][0]; v = l0 * UV[0][1] + l1 * UV[1][1] + l2 * UV[2][1]
                    tw, th, tp = tx
                    tr, tg, tb, ta = tp[int(u * tw) % tw, int(v * th) % th]
                    if ta <= 25 and m['shader'] == SH_CUTOUT: continue
                    rgb = tuple(min(255, int(tc * cc * 2 / 255)) for tc, cc in zip((tr, tg, tb), col))
                else:
                    rgb = tuple(min(255, int(255 * cc * 2 / 255)) for cc in col)
                if m['shader'] == SH_WATER: rgb = (60, 110, 200)
                zb[y][x] = z; px[x, y] = rgb
    d = ImageDraw.Draw(img)
    for (p, colr, label) in extra_pts:
        x, y, _ = to2(p)
        d.ellipse((x - 3, y - 3, x + 3, y + 3), outline=colr)
        if label: d.text((x + 4, y - 4), label, fill=colr)
    # 1-unit / 10-unit scale bar
    d.line((10, h - 5, 10 + 10 * sc, h - 5), fill=(255, 255, 255)); d.text((12 + 10 * sc, h - 14), '10 units', fill=(255, 255, 255))
    img.save(path)

# ----------------------------------------------------------------------------------------------------------------------
# reference readers for the binary files (used by --verify; mirrors ADDL_FORMAT.md for whoever writes the C side)
# ----------------------------------------------------------------------------------------------------------------------
class _Rd:
    def __init__(self, b): self.b = b; self.o = 0
    def u(self, fmt):
        v = struct.unpack_from('>' + fmt, self.b, self.o); self.o += struct.calcsize('>' + fmt)
        return v if len(v) > 1 else v[0]
    def s(self):
        n = self.b[self.o]; self.o += 1
        v = self.b[self.o:self.o + n].decode('utf8'); self.o += n
        return v
    def tri(self):
        mat, flags, _ = self.u('HBB')
        vs = [self.u('5f4B') for _ in range(3)]
        return (mat, flags, tuple(v[:3] for v in vs), tuple(v[3:5] for v in vs), tuple(v[5:9] for v in vs))

def load_world(path):
    r = _Rd(open(path, 'rb').read())
    assert r.b[:4] == b'AWLD'; r.o = 4
    ver = r.u('I'); cell = r.u('f')
    texs = [(r.s(), r.u('H'), r.u('H')) for _ in range(r.u('H'))]
    mats = []
    for _ in range(r.u('H')):
        v = r.u('BB4h4hff'); mats.append(dict(shader=v[0], cull=v[1], tex=list(v[2:6]), pal=list(v[6:10]), alpha=v[10], speed=v[11], name=r.s()))
    bbox = r.u('6f')
    chunks = []
    for _ in range(r.u('I')):
        cx, cz, *bb, n = r.u('hh6fI')
        chunks.append(dict(cx=cx, cz=cz, bbox=bb, tris=[r.tri() for _ in range(n)]))
    assert r.o == len(r.b), 'trailing bytes in %s' % path
    return dict(version=ver, cell=cell, textures=texs, materials=mats, bbox=bbox, chunks=chunks)

def load_amodels(path):
    r = _Rd(open(path, 'rb').read())
    assert r.b[:4] == b'AMDL'; r.o = 4
    ver, n = r.u('II')
    models = {}
    for _ in range(n):
        key = r.s()
        meshes = [[r.tri() for _ in range(r.u('I'))] for _ in range(r.u('H'))]
        objs = []
        for _ in range(r.u('H')):
            parent = r.u('h'); name = r.s(); v = r.u('10fBH')
            objs.append(dict(parent=parent, name=name, pos=v[0:3], rot=v[3:7], scale=v[7:10], active=v[10], meshes=[r.u('H') for _ in range(v[11])]))
        anims = []
        for _ in range(r.u('H')):
            name = r.s(); fps, length, loop, nt = r.u('fHBH')
            tracks = []
            for _ in range(nt):
                oi, nk = r.u('HH')
                tracks.append((oi, [(lambda v: (v[0], v[1:4], v[4:8], v[8:11]))(r.u('H10f')) for _ in range(nk)]))
            anims.append(dict(name=name, fps=fps, length=length, loop=loop, tracks=tracks))
        models[key] = dict(meshes=meshes, objects=objs, anims=anims)
    assert r.o == len(r.b), 'trailing bytes in %s' % path
    return models

def model_pose(M, anim=None, frame=0, poses=None):
    """world matrices (model space) of the objects of a loaded amodels model, optionally at a frame of an animation"""
    loc = [(o['pos'], o['rot'], o['scale']) for o in M['objects']]
    if poses:
        for i, o in enumerate(M['objects']):
            if o['name'] in poses: loc[i] = poses[o['name']]
    if anim is not None:
        for oi, keys in M['anims'][anim]['tracks']:
            k = [k for k in keys if k[0] <= frame] or keys[:1]
            loc[oi] = k[-1][1:]
    W = []
    for i, o in enumerate(M['objects']):
        m = trs_mat(*loc[i])
        W.append(mmul(W[o['parent']], m) if o['parent'] >= 0 else m)
    return W

def verify(out):
    """re-read everything that was written and cross-check it against dreams_addl.txt"""
    txt = open(os.path.join(out, 'dreams_addl.txt'), encoding='utf8').read().split('\n')
    cur = None; errs = 0; models = {}
    for l in txt:
        t = l.split()
        if not t: continue
        if t[0] == 'world':
            W = load_world(os.path.join(out, t[1]))
            for tx in W['textures']:
                p = os.path.join(out, tx[0]); b = open(p, 'rb').read()
                if b[:4] != b'ATEX' or struct.unpack('>HH', b[4:8]) != (tx[1], tx[2]) or len(b) != 8 + tx[1] * tx[2] * 4: print('bad texture', p); errs += 1
            nt = sum(len(c['tris']) for c in W['chunks'])
            for c in W['chunks']:
                for tr in c['tris']:
                    if tr[0] >= len(W['materials']): print('bad material index'); errs += 1; break
            print('verify %s: %d textures, %d materials, %d chunks, %d tris' % (t[1], len(W['textures']), len(W['materials']), len(W['chunks']), nt))
        elif t[0] == 'models':
            models = load_amodels(os.path.join(out, t[1]))
            print('verify %s: %d models' % (t[1], len(models)))
        elif t[0] == 'model':
            if t[1] not in models: print('model key not in amodels.bin:', t[1]); errs += 1
        elif t[0] == 'ent' and t[1] in 'IL' and t[17] != '-' and not os.path.exists(os.path.join(out, t[17])): print('missing script', t[17]); errs += 1
        elif t[0] == 'audio' and t[1] != '-' and not os.path.exists(os.path.join(out, t[1])): print('missing sound', t[1]); errs += 1
    try:       # syntax check of the converted scripts if the lupa module (Lua in Python) is installed
        from lupa import LuaRuntime
        ld = LuaRuntime().eval('function(s, n) local f, e = load(s, n) return e end')
        for f in glob.glob(os.path.join(out, 'ADDLUA', '**', '*.lua'), recursive=True):
            e = ld(open(f, encoding='utf8').read(), os.path.basename(f))
            if e: print('lua syntax:', e); errs += 1
    except ImportError: pass
    print('verify: %d errors' % errs)
    return errs

# ----------------------------------------------------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------------------------------------------------
def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    opt = {}
    i = 1
    while i < len(argv):
        if argv[i].startswith('--'):
            k = argv[i][2:]
            if k in ('mod', 'repo', 'tex', 'cell', 'only') and i + 1 < len(argv): opt[k] = argv[i + 1]; args.remove(argv[i + 1]); i += 2; continue
            opt[k] = True
        i += 1
    if not args: print(__doc__); return 1
    out = args[0]
    mod = opt.get('mod') or os.path.join(HERE, '..', 'ref', 'LSDR-Additional-Dreams')
    repo = opt.get('repo') or os.path.join(HERE, '..', 'src')
    mod, repo = os.path.abspath(mod), os.path.abspath(repo)
    proj = Project(mod, repo)
    H = load_convert_helpers(proj.guid)
    tex = Textures(proj, out, int(opt.get('tex', 512)))
    os.makedirs(out, exist_ok=True)
    lst = [x for x in proj.docs(os.path.join(mod, 'Assets', 'AdditionalDreams.asset')) if x[0] == 114][0][4]
    dream_paths = [proj.path(r) for r in lst.get('Dreams', [])]
    only = set(opt['only'].split(',')) if 'only' in opt else None
    baker = DreamBaker(proj, H, tex, out, float(opt.get('cell', 16)), dream_paths, None)
    lines = []; reports = []
    for idx, dpath in enumerate(dream_paths):
        folder = 'ADD%02d' % idx
        if only and folder not in only: continue
        print('baking', folder, os.path.basename(dpath))
        ls = baker.bake(idx, dpath, folder)
        lines += ls
        reports.append(baker.R)
    # the mod's dream graph (GraphSpawnMap): 19x19, values are indices into the additional dream list
    gm = [x for x in proj.docs(os.path.join(mod, 'Assets', 'GraphSpawnMap.asset')) if x[0] == 114][0][4]
    order = [dream_paths.index(proj.path(e.get('Dream'))) if proj.path(e.get('Dream')) in dream_paths else -1 for e in gm.get('Dreams', [])]
    hexs = gm.get('_graph', '')
    vals = []
    for k in range(0, len(hexs), 8):
        v = int.from_bytes(bytes.fromhex(hexs[k:k + 8]), 'little', signed=True)
        vals.append(order[v] if 0 <= v < len(order) else -1)
    lines.append('graph ' + ' '.join(str(v) for v in vals))
    for k, e in enumerate(gm.get('Dreams', [])):
        c = e.get('Display', {})
        lines.append('gcolor %d %.4f %.4f %.4f' % (order[k], ffloat(c.get('r')), ffloat(c.get('g')), ffloat(c.get('b'))))
    # Lua: the dreams' scripts and the mod's include (require "dreams"), MoonSharp lambdas rewritten like convert.py does
    os.makedirs(os.path.join(out, 'ADDLUA', 'include'), exist_ok=True)
    conv = lambda p: lua_ne(H['lua_lambdas'](open(p, encoding='utf8').read()))
    for name, p in sorted(baker.lua.items()):
        open(os.path.join(out, 'ADDLUA', name), 'w', encoding='utf8').write(conv(p))
    for ref in lst.get('LuaScriptIncludes', []) or []:
        p = proj.path(ref)
        if p: open(os.path.join(out, 'ADDLUA', 'include', os.path.basename(p)), 'w', encoding='utf8').write(conv(p))
    lines.insert(0, 'luainclude ADDLUA/include')
    # sounds referenced by DreamAudio entities -> 22.05 kHz mono 16-bit WAV
    nsnd = 0
    if not opt.get('nosound'):
        ff = shutil.which('ffmpeg')
        os.makedirs(os.path.join(out, 'ADDSND'), exist_ok=True)
        for name, p in sorted(baker.sounds.items()):
            dst = os.path.join(out, 'ADDSND', name)
            if os.path.exists(dst): nsnd += 1; continue
            if ff:
                subprocess.run([ff, '-v', 'error', '-y', '-i', p, '-ar', '22050', '-ac', '1', '-acodec', 'pcm_s16le', dst], check=False)
                nsnd += os.path.exists(dst)
            else: print('ffmpeg not found, sound not converted:', name)
    open(os.path.join(out, 'dreams_addl.txt'), 'w', encoding='utf8').write('\n'.join(lines) + '\n')
    if opt.get('verify'): verify(out)
    # report
    print()
    print('textures:', ', '.join('%s %dx%d (from %dx%d)' % (v[0], v[1], v[2], v[3], v[4]) for v in tex.files.values()))
    print('lua scripts:', len(baker.lua), ' sounds:', nsnd, '/', len(baker.sounds))
    for R in reports:
        st = R['stats']
        nch = len(R['chunks']); vis = [c[2] for c in R['chunks']]
        print('%s %-15s tris %6d (visible %d, collide %d, collider-only %d) chunks %3d (max %d tris/chunk) world.bin %d KB  bbox %s..%s' % (
            R['folder'], R['name'], len(R['tris']), st['render tris'], st['render+collide tris'], st['collider-only tris'], nch, max(vis or [0]),
            R['world_bytes'] // 1024, tuple(round(x, 1) for x in R['bbox'][0]), tuple(round(x, 1) for x in R['bbox'][1])))
        print('      entities %s  spawns %d  gates %d  envs %d  models %d (%d tris, %d anims, %d keys, %d KB)' % (
            R['entities'], R['spawns'], R['gates'], R['envs'], R['models']['models'], R['models']['tris'], R['models']['anims'], R['models']['keys'], R['models']['bytes'] // 1024))
        # per-view load estimate: visible triangles with a corner within the fog end distance of each spawn point
        fogs = [float(l.split()[6]) for l in R['lines'] if l.startswith('env ')]
        sps = [tuple(float(x) for x in l.split()[1:4]) for l in R['lines'] if l.startswith('spawn ')]
        if fogs and sps:
            cent = [(sum(p[0] for p in t[2]) / 3, sum(p[2] for p in t[2]) / 3) for t in R['tris'] if t[1] & 1]
            for fe in sorted(set([min(fogs), max(fogs)])):
                cnt = [sum(1 for (x, z) in cent if (x - s[0]) ** 2 + (z - s[2]) ** 2 < (fe + 1) ** 2) for s in sps]
                print('      view load: visible tris within fog end %.0f of a spawn point: avg %d, max %d (all directions; a 90 deg frustum sees ~1/3)' % (
                    fe, sum(cnt) // len(cnt), max(cnt)))
        if len(R['tris']) > 10000:
            print('      heaviest static parts:', ', '.join('%s %d' % kv for kv in R.get('bynode', collections.Counter()).most_common(6)))
        for n in sorted(set(R['notes'])): print('      note:', n)
        for n in R['matnotes']: print('      material:', n)
        for n in sorted(set(R['warn'])): print('      WARNING:', n)
        un = collections.Counter()
        for (src, prop, val), v in R['unresolved'].items(): un[(src, prop.split('.')[0])] += v
        for (src, prop), v in sorted(un.items()): print('      unresolved override on an object inside an imported model (ignored): %s %s x%d' % (src, prop, v))
    if opt.get('png'):
        pv = os.path.join(out, 'addl_preview'); os.makedirs(pv, exist_ok=True)
        for R in reports:
            pts = []
            for l in R['lines']:
                for l2 in l.split('\n'):
                    t = l2.split()
                    if t[0] == 'spawn': pts.append(((float(t[1]), float(t[2]), float(t[3])), (255, 255, 0), ''))
                    elif t[0] == 'ent' and t[1] in 'KLP': pts.append(((float(t[4]), float(t[5]), float(t[6])), (255, 0, 255), t[1]))
                    elif t[0] == 'ent' and int(t[-3]) > 0: pts.append(((float(t[4]), float(t[5]), float(t[6])), (255, 64, 64), ''))
            preview(R, R['materials'], tex, os.path.join(pv, R['folder'] + '_top.png'), extra_pts=pts)
            if 'side' in opt: preview(R, R['materials'], tex, os.path.join(pv, R['folder'] + '_side.png'), side=True)
            print('preview', R['folder'])
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
