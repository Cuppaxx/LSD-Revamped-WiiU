#!/usr/bin/env python
"""Bake LSD: Revamped's sky effect textures for the Wii U app (app/src/sky.c).

Sources (resolved through the Unity prefabs/materials, not hard-coded file names):
  Prefabs/Sky/Sun.prefab        Sun.SunTexture            -> sky/sun.bin     (LSDR/Sun shader: sun disc and sunburst rays)
  Prefabs/Sky/Clouds.prefab     material Cloud.mat _MainTex -> sky/clouds.bin  (LSDR/Background shader)
  Prefabs/Sky/Stars.prefab      material Stars.mat _MainTex -> sky/stars.bin   (LSDR/Background shader)

File format (big-endian):  'SKYT' u16 width u16 height u8 nlevels u8 0 u8 0 u8 0, then nlevels images of RGBA8 rows
(top row first, as in the PNG; level k is (w>>k) x (h>>k)). Level 0 is the texture exactly as imported (point filtered, no
Unity mipmaps). The smaller levels are only used by the 320x240 renderer when a quad is minified, to show what a
high-resolution render of the original looks like scaled down: box average for the sun and clouds, per-channel maximum
for the stars (single-texel stars would otherwise vanish between pixel centres).

Usage: python sky_assets.py <LSDRevamped repo> <out data dir>    (convert.py calls write(repo, out))
"""
import os, re, sys, glob, struct

def _guid_map(roots):
    g = {}
    for root in roots:
        for m in glob.glob(os.path.join(root, '**', '*.meta'), recursive=True):
            t = open(m, encoding='utf8', errors='ignore').read(400)
            k = re.search(r'guid: (\w+)', t)
            if k: g[k.group(1)] = m[:-5]
    return g

def _read(p): return open(p, encoding='utf8', errors='ignore').read()

def _ref(guid, s):
    m = re.search(r'guid: (\w+)', s or ''); return guid.get(m.group(1)) if m else None

def find_sources(repo):
    a = os.path.join(repo, 'LSDR', 'Assets')
    ue = os.path.join(a, 'UnityEditor', 'Resources')
    guid = _guid_map([os.path.join(ue, 'Materials', 'Sky'), os.path.join(ue, 'Textures', 'Sky'), os.path.join(a, 'Resources', 'Original', 'ETC')])
    pre = os.path.join(ue, 'Prefabs', 'Sky')
    out = {}
    sun = _read(os.path.join(pre, 'Sun.prefab'))
    out['sun'] = _ref(guid, re.search(r'SunTexture: (\{.*\})', sun).group(1))
    for key, prefab in (('clouds', 'Clouds.prefab'), ('stars', 'Stars.prefab')):
        t = _read(os.path.join(pre, prefab))
        mat = _ref(guid, re.search(r'm_Materials:\s*\n\s*- (\{.*\})', t).group(1))
        mt = _read(mat)
        tex = re.search(r'_MainTex:\s*\n\s*m_Texture: (\{.*\})', mt)
        out[key] = _ref(guid, tex.group(1))
    return out

def _levels(im, mode):
    from PIL import Image
    w, h = im.size
    lv = [list(getattr(im, 'get_flattened_data', im.getdata)())]
    while w > 4 and h > 4:
        src = lv[-1]; nw, nh = w // 2, h // 2; dst = []
        for y in range(nh):
            for x in range(nw):
                px = [src[(2 * y + dy) * w + 2 * x + dx] for dy in (0, 1) for dx in (0, 1)]
                if mode == 'max':
                    dst.append(tuple(max(p[c] for p in px) for c in range(4)))
                else:   # alpha-weighted colour average, plain alpha average
                    asum = sum(p[3] for p in px)
                    col = tuple((sum(p[c] * p[3] for p in px) + asum // 2) // asum if asum else 0 for c in range(3))
                    dst.append(col + ((asum + 2) // 4,))
        lv.append(dst); w, h = nw, nh
    return lv

def bake(path, mode, nmax):
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    lv = _levels(im, mode)[:nmax]
    data = b'SKYT' + struct.pack('>HHBBBB', w, h, len(lv), 0, 0, 0)
    for l in lv: data += b''.join(bytes(p) for p in l)
    return data

def write(repo, out):
    src = find_sources(repo)
    d = os.path.join(out, 'sky'); os.makedirs(d, exist_ok=True)
    for key, mode, nmax in (('sun', 'avg', 3), ('clouds', 'avg', 5), ('stars', 'max', 5)):
        if not src.get(key) or not os.path.exists(src[key]):
            print('sky: missing texture for', key, src.get(key)); continue
        b = bake(src[key], mode, nmax)
        open(os.path.join(d, key + '.bin'), 'wb').write(b)
        print('sky/%s.bin from %s (%d bytes)' % (key, os.path.relpath(src[key], repo), len(b)))

if __name__ == '__main__':
    write(sys.argv[1], sys.argv[2])
