#!/usr/bin/env python
"""Convert LSD: Revamped's Unity dream data to a flat text table the Wii U app reads (data/dreams.txt) and stage the
files a dream needs. Usage:
  python convert.py <LSDRevamped repo> <out data dir> [--copy] [--music] [--music-all] [--audio] [--special] [--movies]
                    [--noaddl] [--mod DIR] [--stage DIR] [--tex N] [--cell N]
(tools/make_data.py wraps this with sensible defaults; see docs/DATA.md.)
Unless --noaddl, it then runs tools/addl.py into the same out dir (the Additional Dreams mod from --mod DIR, env LSDR_ADDL_DIR,
or ../ref/LSDR-Additional-Dreams: dreams_addl.txt, ADD00..ADD13, ADDTEX, ADDLUA, ADDSND; see docs/formats/ADDL_FORMAT.md),
which the app adds as dreams 14..27. --stage DIR: where --special / --audio stage their intermediate files (default:
tools/out_special and tools/out_audio next to this file; the movies end up in <stage>/out_special/MOVIE).

dreams.txt lines (one record per dream, in OriginalDreams order):
  dream <idx> <folder> <walk> <linkable> <weight> <firstday> <layout 0=horizontal 1=vertical> <tilewidth> <envoffset> <name...>
  lbd <file>                      one per LBD chunk of the dream (sorted by name, as Revamped loads them)
  group <g> <nenv>                environment group g (0 day, 1 pale, 2 night, 3 dark); g=-1 means the plain environment list
  env fogrgb(3) sub start end fogheight foggradient skyrgb(3) skyfogrgb(3)
  spawn x y z yawdeg dayone tunnel
  ent ... <firstkey> <nmodel> <nmesh> <child0>   + following model/mpose/mmesh lines: animated object models,
                                  see MODELS_FORMAT.md (models.bin per dream is written by models.py)
"""
import os, re, sys, glob, math, shutil, subprocess

if len(sys.argv) < 3 or sys.argv[1].startswith('--'): sys.exit(__doc__)
repo = sys.argv[1]; out = sys.argv[2]; copy = '--copy' in sys.argv; music = '--music' in sys.argv or '--music-all' in sys.argv
def _opt(k, d=None): return sys.argv[sys.argv.index(k) + 1] if k in sys.argv and sys.argv.index(k) + 1 < len(sys.argv) else d
_here = os.path.dirname(os.path.abspath(__file__))
STAGE = os.path.abspath(_opt('--stage') or _here)          # staging root for out_special / out_audio
MOD_DIR = _opt('--mod') or os.environ.get('LSDR_ADDL_DIR') or os.path.join(_here, '..', 'ref', 'LSDR-Additional-Dreams')
base = os.path.join(repo, 'LSDR', 'Assets', 'Resources')
orig = os.path.join(base, 'Original')
SPAWN_GUID = 'b66350b2ce324279b594531b66a49112'      # PlayerSpawn.cs

guid = {}
for m in [f for d in ('Assets', 'Packages') for f in glob.glob(os.path.join(repo, 'LSDR', d, '**', '*.meta'), recursive=True)]:
    t = open(m, encoding='utf8', errors='ignore').read(600)
    g = re.search(r'guid: (\w+)', t)
    if g: guid[g.group(1)] = m[:-5]

def ref(s):
    m = re.search(r'guid: (\w+)', s); return guid.get(m.group(1)) if m else None
def field(t, name, default=None):
    m = re.search(r'^\s*' + name + r': (.*)$', t, re.M); return m.group(1).strip() if m else default
def color(t, name):
    m = re.search(r'^\s*' + name + r': \{r: ([-\d.e]+), g: ([-\d.e]+), b: ([-\d.e]+)', t, re.M)
    return tuple(float(x) for x in m.groups()) if m else (1.0, 1.0, 1.0)
def fl(x): return float(x)
def refs_after(t, name):
    m = re.search(r'^\s*' + name + r':\n((?:\s+- \{.*\}\n?)+)', t, re.M)
    return [ref(l) for l in m.group(1).strip().split('\n')] if m else []

def read(p): return open(p, encoding='utf8', errors='ignore').read()

def env_line(path):
    t = read(path)
    f = color(t, 'FogColor'); s = color(t, 'SkyColor'); sf = color(t, 'SkyFogColor')
    def n(name, d): return fl(field(t, name, str(d)))
    fx = [*color(t, 'SunColor'), n('SunChance', 1), n('SunHeight', 2), n('SunSize', 1),
          *color(t, 'SecondSunColor'), n('SecondSunSize', 1), n('SecondSunChance', 0.5), n('SecondSunOffset', -0.15), n('SecondSunHeight', 2),
          *color(t, 'SunBurstColor'), n('SunBurstSize', 1), n('SunBurstChance', 0.5),
          *color(t, 'CloudsColor'), n('NumberOfClouds', 6), n('CloudsChance', 0.5),
          *color(t, 'StarsColor'), n('NumberOfStars', 3), n('StarsChance', 0)]
    return 'env %.4f %.4f %.4f %d %.3f %.3f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f ' % (
        *f, int(field(t, 'SubtractiveFog', '0')), fl(field(t, 'FogStartDistance', '10')), fl(field(t, 'FogEndDistance', '30')),
        fl(field(t, 'FogHeight', '0.24')), fl(field(t, 'FogGradient', '0.24')), *s, *sf) + ' '.join('%.4f' % v for v in fx)

def env_seq(path):
    t = read(path)
    return [p for p in refs_after(t, 'Environments') if p]

# ---- prefab spawn points: world position through the Transform hierarchy
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz)
def qrot(q, v):
    x, y, z, w = q
    # v' = q * v * q^-1
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]

def prefab_spawns(path):
    t = read(path)
    docs = re.split(r'^--- !u!(\d+) &(-?\d+)', t, flags=re.M)
    objs = {}   # (cls,id) -> text
    for i in range(1, len(docs), 3): objs[(int(docs[i]), docs[i + 1])] = docs[i + 2]
    go_tr = {}; trs = {}; go = {}
    for (cls, id_), body in objs.items():
        if cls == 1:
            nm_ = re.search(r'm_Name: (.*)', body); go[id_] = dict(name=nm_.group(1).strip() if nm_ else '')
        if cls == 4:
            g = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', body)
            if not g or 'm_LocalPosition' not in body: continue
            def vec(n):
                m = re.search(n + r': \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)(?:, w: ([-\d.e]+))?', body); return tuple(float(v) for v in m.groups() if v is not None)
            fa = re.search(r'm_Father: \{fileID: (-?\d+)\}', body)
            trs[id_] = dict(go=g.group(1), pos=vec('m_LocalPosition'), rot=vec('m_LocalRotation'), scale=vec('m_LocalScale'), father=fa.group(1) if fa else '0')
            go_tr[g.group(1)] = id_
    def world(tid):
        t_ = trs.get(tid)
        if not t_: return (0, 0, 0), (0, 0, 0, 1)
        pp, pr = world(t_['father']) if t_['father'] != '0' else ((0, 0, 0), (0, 0, 0, 1))
        # parent scale ignored (all 1 in these prefabs)
        lp = qrot(pr, t_['pos'])
        return (pp[0] + lp[0], pp[1] + lp[1], pp[2] + lp[2]), qmul(pr, t_['rot'])
    res = []
    for (cls, id_), body in objs.items():
        if cls == 114 and SPAWN_GUID in body:
            g = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', body).group(1)
            tid = go_tr.get(g)
            if not tid: continue
            p, q = world(tid)
            yaw = math.degrees(2 * math.atan2(q[1], q[3]))   # rotation about Y (spawn points are only yawed)
            res.append((p, yaw, int(field(body, 'DayOneSpawn', '0')), int(field(body, 'TunnelEntrance', '0')), go[g]['name'].replace(' ', '_') if g in go else '_'))
    return res

# songs: OriginalSongLibrary.OriginalDreams[i] holds one song list per SongStyle
STYLE_KEYS = ['StandardSongs', 'AmbientSongs', 'LovelySongs', 'HumanSongs', 'ElectroSongs', 'EthnovaSongs', 'CartoonSongs']   # SongStyle enum order
song_text = read(os.path.join(orig, 'OriginalSongLibrary.asset'))
song_entries = re.split(r'\n  - AmbientSongs:', song_text)[1:]
def song_paths(list_asset):
    out_ = []
    for sp in refs_after(read(list_asset), 'Songs'):
        if not sp: continue
        c = re.search(r'Clip: \{fileID: \d+, guid: (\w+)', read(sp))
        wav = guid.get(c.group(1)) if c else None
        if wav: out_.append(os.path.relpath(wav, orig).replace(os.sep, '/'))
    return out_


# ---- entities (triggers, interactive objects, targets) from the dream prefab, and Lua scripts (MoonSharp "|a| expr" lambdas rewritten)
TRIG = {'eb87da8162174fe3be019fb7462fe1cf': 'L', 'c2064ad0257e4cb478058e31e87d2613': 'K', 'a6756dc41dbcffeadb913dcedce78d6f': 'P',
        'c5b0923921724b18b17afa40a2d01bf7': 'I', 'aadec67881284206abea7f2dc299e32e': 'T'}

def lua_lambdas(src):
    """MoonSharp lambda shorthand: |a, b| expr  ->  function(a, b) return expr end (body runs to the closing paren/comma/brace at depth 0)."""
    out = []; i = 0; n = len(src)
    def skip_string(i):
        q = src[i]; j = i + 1
        while j < n and src[j] != q:
            j += 2 if src[j] == chr(92) else 1
        return j + 1
    def long_bracket(i):
        m = re.match(r'\[(=*)\[', src[i:])
        if not m: return None
        end = src.find(']' + m.group(1) + ']', i + len(m.group(0)))
        return (end + len(m.group(1)) + 2) if end >= 0 else n
    while i < n:
        c = src[i]
        if src.startswith('--', i):
            lb = long_bracket(i + 2)
            j = lb if lb else (src.find('\n', i) if src.find('\n', i) >= 0 else n)
            out.append(src[i:j]); i = j; continue
        if c in '"\'':
            j = skip_string(i); out.append(src[i:j]); i = j; continue
        if c == '[':
            lb = long_bracket(i)
            if lb: out.append(src[i:lb]); i = lb; continue
        if c == '!' and src.startswith('!=', i):          # MoonSharp accepts != for ~=
            out.append('~='); i += 2; continue
        if c == '|':
            m = re.match(r'\|([\w\s,]*)\|', src[i:])
            if m:
                params = m.group(1).strip(); i += len(m.group(0)); depth = 0; j = i; k = i
                while k < n:
                    ch = src[k]
                    if ch in '"\'': k = skip_string(k); continue
                    if ch in '([{': depth += 1
                    elif ch in ')]}':
                        if depth == 0: break
                        depth -= 1
                    elif ch == ',' and depth == 0: break
                    elif ch == '\n' and depth == 0: break
                    k += 1
                body = lua_lambdas(src[i:k]).strip()
                out.append('function(%s) return %s end' % (params, body)); i = k; continue
        out.append(c); i += 1
    return ''.join(out)

def prefab_entities(path, dreams_list, ddir=None, folder=None):
    t = read(path)
    docs = re.split(r'^--- !u!(\d+) &(-?\d+)', t, flags=re.M)
    objs = {}
    for i in range(1, len(docs), 3): objs[(int(docs[i]), docs[i + 1])] = docs[i + 2]
    go = {}; trs = {}; go_tr = {}
    for (cls, id_), body in objs.items():
        if cls == 1:
            nm = re.search(r'm_Name: (.*)', body); act = re.search(r'm_IsActive: (\d)', body)
            comps = re.findall(r'component: \{fileID: (-?\d+)\}', body)
            go[id_] = dict(name=nm.group(1).strip() if nm else '', active=int(act.group(1)) if act else 1, comps=comps)
        elif cls == 4:
            g = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', body)
            if not g or 'm_LocalPosition' not in body: continue
            def vec(n_):
                m = re.search(n_ + r': \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)(?:, w: ([-\d.e]+))?', body); return tuple(float(v) for v in m.groups() if v is not None)
            fa = re.search(r'm_Father: \{fileID: (-?\d+)\}', body)
            trs[id_] = dict(pos=vec('m_LocalPosition'), rot=vec('m_LocalRotation'), scale=vec('m_LocalScale'), father=fa.group(1) if fa else '0')
            go_tr[g.group(1)] = id_
    def world(tid):
        t_ = trs.get(tid)
        if not t_: return (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
        if t_['father'] != '0': pp, pr, ps = world(t_['father'])
        else: pp, pr, ps = (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
        lp = qrot(pr, tuple(a * b for a, b in zip(t_['pos'], ps)))
        return (pp[0] + lp[0], pp[1] + lp[1], pp[2] + lp[2]), qmul(pr, t_['rot']), tuple(a * b for a, b in zip(t_['scale'], ps))
    def active(gid):
        return go[gid]['active']
    ents = []; found = []
    for gid, g in go.items():
        kind = None; body_ = None; box = None
        for cid in g['comps']:
            for (cls, id_), body in objs.items():
                if id_ != cid: continue
                if cls == 65:
                    c = re.search(r'm_Center: \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)', body); sz = re.search(r'm_Size: \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)', body)
                    if c and sz: box = tuple(float(v) for v in c.groups()) + tuple(float(v) for v in sz.groups())
                elif cls == 114:
                    for guid_, k in TRIG.items():
                        if guid_ in body.split('m_EditorClassIdentifier')[0]: kind = k; body_ = body
        if not kind:
            for cid in g['comps']:
                for (cls, id_), body in objs.items():
                    if id_ == cid and cls == 114 and re.search(r'^  ID: ', body, re.M): kind = 'T'; body_ = body
        if not kind: continue
        tid = go_tr.get(gid)
        if not tid: continue
        found.append((gid, g, kind, body_, box, tid))
    ent_gids = set(f[0] for f in found)
    for gid, g, kind, body_, box, tid in found:
        p_, q_, s_ = world(tid)
        yaw = math.degrees(2 * math.atan2(q_[1], q_[3]))
        extra = ''
        if kind == 'L':
            sc = ref(field(body_, 'Script', '')); extra = ' %s %s %s %s' % (tc(os.path.relpath(sc, orig).replace(os.sep, '/')) if sc else '-', field(body_, 'TriggerFunctionName', 'onTrigger') or 'onTrigger', field(body_, 'TriggerExitFunctionName', 'onTriggerExit') or 'onTriggerExit', field(body_, 'OnlyOnce', '0'))
        elif kind == 'K':
            lk = ref(field(body_, 'Linked', '')); li = dreams_list.index(lk) if lk in dreams_list else -1
            extra = ' %d %s %s %s %s' % (li, field(body_, 'SpawnPointEntityID', '') or '-', field(body_, 'LockInput', '0'), field(body_, 'AffectsLinksWithin', '0'), field(body_, 'OnlyOnce', '0'))
        elif kind == 'P':
            d = re.search(r'Destination: \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)', body_)
            extra = ' %s %s %s %s %s' % (*(d.groups() if d else ('0', '0', '0')), field(body_, 'AlwaysOn', '0'), field(body_, 'OnlyOnce', '0'))
        elif kind == 'I':
            sc = ref(field(body_, 'Script', '')); extra = ' %s %s %s' % (tc(os.path.relpath(sc, orig).replace(os.sep, '/')) if sc else '-', field(body_, 'InteractionKind', '0'), field(body_, 'InteractionDistance', '3'))
        b = box or (0, 0, 0, 1, 1, 1)
        idv = field(body_, 'ID', '') if body_ else ''
        nm = ((idv or g['name']).replace(' ', chr(31))) or '_'
        # animated object models (tools/models.py, MODELS_FORMAT.md): " <first model key|-> <nmodel> <nmesh> <child0|->" on the
        # ent line, then the 'model' / 'mpose' / 'mmesh' lines of this entity (joined into the same list item)
        msuffix, mlines = baker.entity_lines(path, gid, ent_gids, ddir, folder) if baker and ddir else (' - 0 0 -', [])
        ents.append('\n'.join(['ent %s %s %d %.4f %.4f %.4f %.2f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f%s%s' % (kind, nm, g['active'], *p_, yaw, *s_, *b, extra, msuffix)] + mlines))
    return ents

def tc(rel):
    """true on-disk case of a relative path (the Wii U filesystem is case-sensitive, Windows is not)"""
    cur = orig; out_ = []
    for part in rel.split('/'):
        try: names = os.listdir(cur)
        except OSError: names = []
        hit = next((n_ for n_ in names if n_.lower() == part.lower()), part)
        out_.append(hit); cur = os.path.join(cur, hit)
    return '/'.join(out_)

lua_out = {}
def collect_lua(rel):
    if rel and rel != '-' and rel not in lua_out:
        src = read(os.path.join(orig, rel)); lua_out[rel] = lua_lambdas(src)


dreams_list = refs_after(read(os.path.join(orig, 'OriginalDreams.asset')), 'Dreams')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import models
baker = models.Baker(guid, [os.path.join(base, field(read(d), 'DreamPrefabPath') + '.prefab') for d in dreams_list])
lines = []
need = []
songs_all = []
for idx, dpath in enumerate(dreams_list):
    t = read(dpath)
    folder = os.path.basename(os.path.dirname(dpath))
    prefab = os.path.join(base, field(t, 'DreamPrefabPath') + '.prefab')
    pt = read(prefab)
    lt = re.search(r'LayoutType: (\d+)\n\s+TileWidth: (\d+)', pt)
    layout, tw = (int(lt.group(1)), int(lt.group(2))) if lt else (1, 1)
    ddir = os.path.dirname(dpath)
    # Revamped loads every *.LBD under the dream folder (subfolders included); chunk numbers in the names give the order
    lbds = sorted((os.path.relpath(p, ddir).replace(os.sep, '/') for p in glob.glob(os.path.join(ddir, '**', '*.LBD'), recursive=True)),
                  key=lambda r: os.path.basename(r))
    lines.append('dream %d %s %s %s %s %s %d %d %s %s' % (idx, folder, field(t, 'WalkSpeed', '1.25'), field(t, 'Linkable', '0'),
                 field(t, 'LinkWeighting', '1'), field(t, 'FirstDay', '0'), layout, tw, field(t, 'EnvironmentOffset', '0'), field(t, 'Name', folder)))
    lines.append('len %s' % field(t, 'DreamLengthSeconds', '0'))
    lines.append('fx %s' % field(t, 'HasEnvironmentEffects', '1'))
    lines.append('grey %s' % field(t, 'GreyMan', '0'))
    lg = re.findall(r'_vector: \{x: (-?\d+), y: (-?\d+)\}', pt)
    lines.append('lgraph %d %s' % (len(lg), ' '.join('%s %s' % v for v in lg)))
    if idx < len(song_entries):
        ent = 'AmbientSongs:' + song_entries[idx]
        for si, key in enumerate(STYLE_KEYS):
            m = re.search(key + r': (\{.*\})', ent)
            la = ref(m.group(1)) if m else None
            for w in (song_paths(la) if la else []):
                lines.append('song %d %s' % (si, w)); songs_all.append((si, w))
    for l in lbds: lines.append('lbd ' + l)
    groups = [p for p in refs_after(t, 'EnvironmentGroups') if p]
    if len(groups) == 4:
        for g, gp in enumerate(groups):
            envs = env_seq(gp); lines.append('group %d %d' % (g, len(envs)))
            for e in envs: lines.append(env_line(e))
    else:
        em = re.search(r'^\s*Environments: (\{.*\})', t, re.M)
        envs = env_seq(ref(em.group(1))) if em and ref(em.group(1)) else []
        lines.append('group -1 %d' % len(envs))
        for e in envs: lines.append(env_line(e))
    for e_ in prefab_entities(prefab, dreams_list, ddir, folder):
        lines.append(e_)
        parts_ = e_.split()
        if parts_[1] in 'LI': collect_lua(parts_[17])
    for (p, yaw, d1, tun, snm) in prefab_spawns(prefab):
        lines.append('spawn %.4f %.4f %.4f %.1f %d %d %s' % (p[0], p[1], p[2], yaw, d1, tun, snm))
    need.append((folder, dpath, lbds))

# the 19x19 dream graph (x = dynamicness, y = upperness, each -9..9), values are indices into the dream list above
sm = read(os.path.join(orig, 'OriginalDreamsSpawnMap.asset'))
sm_dreams = [ref('guid: ' + g) for g in re.findall(r'Dream: \{fileID: \d+, guid: (\w+)', sm)]
order = [dreams_list.index(p) if p in dreams_list else -1 for p in sm_dreams]
hexs = re.search(r'_graph: (\w+)', sm).group(1)
vals = []
for i in range(0, len(hexs), 8):
    v = int.from_bytes(bytes.fromhex(hexs[i:i + 8]), 'little', signed=True)
    vals.append(order[v] if 0 <= v < len(order) else -1)
lines.append('graph ' + ' '.join(str(v) for v in vals))
# ---- water: UV-rect material overrides -> water.bin per dream (see lsd.c / raster.c)
import struct
from PIL import Image
def img_rgba(path):
    return Image.open(path).convert('RGBA')
def water_bin(dpath):
    ddir = os.path.dirname(dpath)
    ov = glob.glob(os.path.join(ddir, '*Override.asset'))
    if not ov: return None
    t = read(ov[0]); entries = []
    pat = r'UVRect:\s*\n\s*serializedVersion: 2\s*\n\s*x: ([-\d.e]+)\s*\n\s*y: ([-\d.e]+)\s*\n\s*width: ([-\d.e]+)\s*\n\s*height: ([-\d.e]+)\s*\n\s*Material: (\{.*\})'
    for m in re.finditer(pat, t):
        rect = [float(m.group(i)) for i in range(1, 5)]
        matp = ref(m.group(5))
        if not matp: continue
        mt = read(matp)
        def tex(name):
            mm = re.search(name + r':\s*\n\s*m_Texture: (\{.*\})', mt)
            return ref(mm.group(1)) if mm else None
        wm = tex('_WaterMapTexture'); pals = [tex('_WaterPalette' + k) for k in 'ABCD']
        if not wm or not all(pals): continue
        alpha = fl(field(mt, '_Alpha', '0.5')); speed = fl(field(mt, '_AnimationSpeed', '1'))
        im = img_rgba(wm); mw, mh = im.size
        data = bytearray()
        for (r, g, b, a) in im.getdata(): data += bytes((r, a))
        pal0 = img_rgba(pals[0]); pw = pal0.size[0]
        pdata = bytearray()
        for p in pals:
            pi = img_rgba(p); ph = pi.size[1]
            row = [pi.getpixel((int(i * pi.size[0] / pw), ph // 2)) for i in range(pw)]
            for (r, g, b, a) in row: pdata += bytes((r, g, b))
        entries.append(struct.pack('>4f2f3H', *rect, alpha, speed, mw, mh, pw) + bytes(data) + bytes(pdata))
    if not entries: return None
    return b'WATR' + struct.pack('>I', len(entries)) + b''.join(entries)

os.makedirs(out, exist_ok=True)
for f_ in sorted(glob.glob(os.path.join(orig, 'LuaInclude', '*.lua'))):   # `require` targets of the dream scripts (dreams.lua, train.lua)
    collect_lua('LuaInclude/' + os.path.basename(f_))
for rel, src in lua_out.items():
    dst = os.path.join(out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); open(dst, 'w', encoding='utf8').write(src)
print('lua scripts:', len(lua_out))
for folder, dpath, lbds in need:
    wb = water_bin(dpath)
    if wb:
        os.makedirs(os.path.join(out, folder), exist_ok=True)
        open(os.path.join(out, folder, 'water.bin'), 'wb').write(wb); print('water.bin', folder, len(wb), 'bytes')
    ms = baker.write(out, folder)
    if ms: print('models.bin %s: %d models, %d objects, %d triangles, %d animations, %d keys, %d bytes' % (
        folder, ms['models'], ms['objects'], ms['tris'], ms['anims'], ms['keys'], ms['bytes']))
for r_ in baker.report: print('models:', r_)
for u_ in baker.unlinked(): print('models: AnimatedObject not attached to any entity:', u_)
open(os.path.join(out, 'dreams.txt'), 'w').write('\n'.join(lines) + '\n')
print('dreams.txt:', len(dreams_list), 'dreams,', len(lines), 'lines')
import sky_assets; sky_assets.write(repo, out)        # sky effect textures for src/sky.c: data/sky/{sun,clouds,stars}.bin
if copy:
    os.makedirs(os.path.join(out, 'ETC'), exist_ok=True)
    shutil.copy2(os.path.join(orig, 'ETC', 'SYMSPY.MOM'), os.path.join(out, 'ETC', 'SYMSPY.MOM'))   # the grey man
    for f in ('TITLE.TIM', 'HGRAPH.TIM'):                   # front end art: title sprite sheet, dream chart
        shutil.copy2(os.path.join(orig, 'ETC', f), os.path.join(out, 'ETC', f))
    # front end sounds (Revamped's title screen: UIMainMenuMusic clips by day number, UIButtonSounds hover/press) as WAV,
    # because the app only plays WAV. Needs ffmpeg for the .ogg files; skipped without it.
    menu_src = os.path.join(repo, 'LSDR', 'Assets', 'UnityEditor', 'Resources', 'Sound', 'Menu')
    menu_out = os.path.join(out, 'MENU'); os.makedirs(menu_out, exist_ok=True)
    menu_files = {'normal.wav': 'In Linking the Sapient Dream.ogg', 'kanji.wav': 'In Leisure the Sonorous Dream.ogg',
                  'downer.wav': 'In Lability the Shifting Dream.ogg', 'upper.wav': 'In Lucidity the Scintillating Dream.ogg',
                  'hover.wav': 'select.ogg', 'press.wav': 'click.wav'}
    for dst, src_name in menu_files.items():
        s = os.path.join(menu_src, src_name); d = os.path.join(menu_out, dst)
        if not os.path.exists(s) or os.path.exists(d): continue
        if s.endswith('.wav'): shutil.copy2(s, d); continue
        ff = shutil.which('ffmpeg')
        if not ff: print('ffmpeg not found, skipping', src_name); continue
        rate = ['-ar', '22050', '-ac', '1'] if dst in ('hover.wav',) else ['-ar', '44100', '-ac', '2']
        subprocess.run([ff, '-v', 'error', '-y', '-i', s] + rate + ['-acodec', 'pcm_s16le', d], check=False)
    for folder, dpath, lbds in need:
        d = os.path.join(out, folder); os.makedirs(d, exist_ok=True)
        src = os.path.dirname(dpath)
        for f in lbds + ['TEXA.TIX', 'TEXB.TIX', 'TEXC.TIX', 'TEXD.TIX']:
            if os.path.exists(os.path.join(src, f)):
                os.makedirs(os.path.dirname(os.path.join(d, f)), exist_ok=True); shutil.copy2(os.path.join(src, f), os.path.join(d, f))
    if music:
        for si, w in songs_all:
            if si == 0 or '--music-all' in sys.argv:       # the standard style for every dream (all styles with --music-all)
                dst = os.path.join(out, w); os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst): shutil.copy2(os.path.join(orig, w), dst)
    print('copied dream data to', out)

# The Additional Dreams mod (tools/addl.py, format in ADDL_FORMAT.md): dreams_addl.txt + ADD00..ADD13, ADDTEX, ADDLUA, ADDSND
# in the same out dir (no name clashes with the STGxx folders); the app appends them to dreams.txt's list as dreams 14..27.
# Skipped with --noaddl or when the mod clone is missing. --tex N / --cell N are passed on (default here: --cell 8).
if '--noaddl' not in sys.argv:
    here = os.path.dirname(os.path.abspath(__file__))
    mod = os.path.normpath(os.path.abspath(MOD_DIR))
    if os.path.isdir(mod):
        cmd = [sys.executable, '-I', os.path.join(here, 'addl.py'), out, '--mod', mod, '--repo', repo]
        for k in ('--tex', '--cell'):
            if k in sys.argv and sys.argv.index(k) + 1 < len(sys.argv): cmd += [k, sys.argv[sys.argv.index(k) + 1]]
        if '--cell' not in cmd: cmd += ['--cell', '8']        # 8x8-unit chunks: finer view culling (Dreamscape: 2-5 ms instead of 5+)
        print('additional dreams:', ' '.join(cmd))
        r_ = subprocess.run(cmd)
        if r_.returncode: print('addl.py failed with exit code', r_.returncode)
    else:
        print('Additional Dreams mod not found at', mod, '- skipped (clone github.com/Figglewatts/LSDR-Additional-Dreams and pass --mod)')
if '--special' in sys.argv:   # opt-in: special days, movies, credits (SPECIAL_FORMAT.md)
    # staged in <stage>/out_special (movies only with --movies: ~214 MB, they go on the SD card, never in the wuhb), then the
    # small part the app bundles (specials.txt, SPDAY/texts_en.txt, CREDITS/) is copied into the data dir
    here = os.path.dirname(os.path.abspath(__file__)); stage = os.path.join(STAGE, 'out_special')
    cmd = [sys.executable, '-I', os.path.join(here, 'special_assets.py'), repo, stage] + ([] if '--movies' in sys.argv else ['--no-movies'])
    subprocess.run(cmd)
    subprocess.run([sys.executable, '-I', os.path.join(here, 'special_assets.py'), '--bundle', stage, out])
if '--audio' in sys.argv: subprocess.run([sys.executable, '-I', os.path.join(_here, 'audio_assets.py'), repo, os.path.join(STAGE, 'out_audio'), '--merge', out]
                                         + (['--mod', os.path.abspath(MOD_DIR)] if os.path.isdir(MOD_DIR) else []))   # opt-in: footsteps, SFX, DreamAudio, link sounds (AUDIO_FORMAT.md)
